#include "Runtime/UI/GameCameraInspector.hpp"
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace {
struct Item {
    ImRect bounds;
    ImRect totalBounds;
    ImGuiItemFlags flags = 0;
    std::string label;
};
std::unordered_map<ImGuiID, Item> items;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}
void Near(float actual, float expected, const char* message) {
    Require(std::abs(actual - expected) < 0.01f, std::string(message) +
        " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}

struct Fixture {
    flecs::world world;
    engine::CameraController controller;
    flecs::entity entity;
    engine::CameraState live;
    ImGuiContext* context;
    int changedFrames = 0;
    float panelWidth;

    explicit Fixture(float width = 850.0f) : entity(engine::EnsureGameCameraEntity(world, controller)), panelWidth(width) {
        engine::Translator::SetLanguage(engine::Language::English);
        context = ImGui::CreateContext();
        context->TestEngineHookItems = true;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1200, 1300);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels;
        int atlasWidth, atlasHeight;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
        Require(pixels && atlasWidth > 0 && atlasHeight > 0, "real font atlas built");
        Frame(); Frame();
    }
    ~Fixture() {
        ImGui::DestroyContext(context);
        engine::Translator::SetLanguage(engine::Language::English);
    }
    engine::GameCameraComponent& Camera() { return entity.get_mut<engine::GameCameraComponent>(); }
    void Frame() {
        items.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, 1250), ImGuiCond_Always);
        ImGui::Begin("Entity Inspector###EntityInspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::PushItemWidth(360.0f);
        changedFrames += engine::DrawGameCameraInspector(entity, live) ? 1 : 0;
        ImGui::PopItemWidth();
        ImGui::End();
        ImGui::Render();
    }
    ImGuiID Id(const char* suffix) const {
        ImGuiWindow* window = ImGui::FindWindowByName("Entity Inspector###EntityInspector");
        Require(window != nullptr, "production inspector window exists");
        return window->GetID((std::string("###GameCamera") + suffix).c_str());
    }
    Item Control(const char* suffix) const {
        const auto it = items.find(Id(suffix));
        Require(it != items.end(), std::string("production control exists: ") + suffix);
        return it->second;
    }
    void ClickPoint(ImVec2 point) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        Frame(); Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Frame(); Frame();
    }
    void Click(const char* suffix, float fraction = 0.02f) {
        const ImRect bounds = Control(suffix).bounds;
        ClickPoint(ImVec2(bounds.Min.x + bounds.GetWidth() * fraction, bounds.GetCenter().y));
    }
    void Number(const char* suffix, const char* value) {
        const ImVec2 point = Control(suffix).bounds.GetCenter();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        Frame(); Frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        Frame();
        io.AddInputCharactersUTF8(value);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame();
    }
};

void LensAndOrbit() {
    Fixture f;
    const auto untouchedLive = f.live;
    f.Click("AutomaticFov");
    Require(!f.Camera().settings.automaticFov && f.Camera().settingsDirty, "checkbox edits authored automatic FOV and marks dirty");
    f.Camera().settingsDirty = false;
    f.Click("AutomaticFov");
    f.Click("Fov", 0.80f);
    Require(f.Camera().settings.fov > 90.0f && !f.Camera().settings.automaticFov && f.Camera().settingsDirty,
        "manual FOV slider writes authoring and disables automatic FOV");
    f.Number("NearClip", "0.35");
    f.Number("FarClip", "2400");
    Near(f.Camera().settings.nearPlane, 0.35f, "near clip is editable through real numeric input");
    Near(f.Camera().settings.farPlane, 2400.0f, "far clip is editable through real numeric input");
    f.Click("Yaw", 0.75f);
    f.Click("Pitch", 0.70f);
    f.Click("Distance", 0.60f);
    Require(f.Camera().settings.yaw > 0.5f && f.Camera().settings.pitch > 0.2f && f.Camera().settings.distance > 30.0f,
        "orbit sliders publish radians and distance to authored settings");
    f.Click("ResetOrbit", 0.5f);
    Near(f.Camera().settings.yaw, 0.0f, "reset yaw");
    Near(f.Camera().settings.pitch, 0.0f, "reset pitch");
    Near(f.Camera().settings.distance, 5.0f, "reset distance");
    Require(f.live.cameraFov == untouchedLive.cameraFov && f.live.camera2world == untouchedLive.camera2world,
        "UI does not mutate the published runtime state");
    Require(f.changedFrames > 5 && f.Camera().settings.IsValid(), "real changes reported and settings remain valid");
    std::puts("PASS real camera lens, clipping, orbit and reset controls author dirty settings");
}

void Occlusion() {
    Fixture f;
    f.Click("Coverage", 0.7f);
    f.Click("FadeSpeed", 0.6f);
    f.Click("TargetHeight", 0.6f);
    Require(f.Camera().settings.occlusionFadeCoverage > 0.6f && f.Camera().settings.occlusionFadeSpeed > 15.0f &&
        f.Camera().settings.occlusionTargetHeight > 1.5f, "occlusion sliders update all three authored settings");
    f.Click("OcclusionEnabled");
    Require(!f.Camera().settings.occlusionFadeEnabled, "occlusion toggle writes setting");
    f.Camera().settingsDirty = false;
    const auto before = f.Camera().settings;
    f.Click("Coverage", 0.2f);
    Require(f.Camera().settings == before && !f.Camera().settingsDirty, "disabled fade controls cannot mutate authoring");
    std::puts("PASS occlusion controls and disabled-state input");
}

void ModeAndTakeover() {
    Fixture f;
    f.live.camera2world[3] = glm::vec4(12, 7, 19, 1);
    f.Click("Mode", 0.5f);
    ImRect freeChoice;
    bool found = false;
    for (const auto& [id, item] : items) {
        if (item.label == "Free Camera") { freeChoice = item.bounds; found = true; }
    }
    Require(found, "real mode popup offers Free Camera");
    f.ClickPoint(freeChoice.GetCenter());
    Require(f.Camera().settings.mode == engine::CameraMode::Free && f.Camera().settingsDirty,
        "mode combo selects authored Free mode");
    Require(f.Camera().freeTransform == f.live.camera2world, "switching to Free retains the visible camera pose");
    Require(!items.contains(f.Id("Yaw")), "Free mode hides Follow orbit controls");
    for (const auto mode : {engine::CameraMode::Portal, engine::CameraMode::Cinematic}) {
        f.live.mode = mode;
        f.Frame();
        const auto before = f.Camera().settings;
        const auto pose = f.Camera().freeTransform;
        f.Camera().settingsDirty = false;
        const int changed = f.changedFrames;
        f.Click("AutomaticFov");
        f.Click("Fov", 0.25f);
        f.Click("OcclusionEnabled");
        f.Click("Mode", 0.5f);
        Require(f.Camera().settings == before && f.Camera().freeTransform == pose && !f.Camera().settingsDirty &&
            f.changedFrames == changed, "portal/cinematic ownership blocks actual inspector clicks");
    }
    std::puts("PASS mode popup preserves pose; temporary camera owners lock real controls");
}

void Language() {
    Fixture f;
    const char* controls[] = {"Mode", "AutomaticFov", "Fov", "NearClip", "FarClip", "Yaw", "Pitch",
        "Distance", "ResetOrbit", "OcclusionEnabled", "Coverage", "FadeSpeed", "TargetHeight"};
    std::unordered_map<ImGuiID, std::string> english;
    for (const char* control : controls) english.emplace(f.Id(control), f.Control(control).label);
    engine::Translator::SetLanguage(engine::Language::Chinese);
    f.Frame(); f.Frame();
    for (const char* control : controls) {
        const auto current = f.Control(control);
        Require(english.contains(f.Id(control)), "translated control retains its ImGui ID");
        // BeginCombo does not emit ItemInfo in this ImGui version. Its ID is
        // still observed by ItemAdd; test its translated popup choices below.
        if (std::string(control) != "Mode")
            Require(current.label != english.at(f.Id(control)), std::string("visible control text is translated: ") + control);
    }
    f.Click("AutomaticFov");
    Require(!f.Camera().settings.automaticFov && f.Camera().settingsDirty, "translated checkbox remains interactive");
    f.Click("Mode", 0.5f);
    bool translatedChoice = false;
    for (const auto& [id, item] : items)
        translatedChoice |= item.label == engine::Translator::SL("Free Camera");
    Require(translatedChoice, "mode popup submits the translated Free Camera choice");
    std::puts("PASS Chinese labels retain stable widget IDs and working input");
}

void NarrowLayout() {
    Fixture f(300.0f);
    const char* controls[] = {"Mode", "AutomaticFov", "Fov", "NearClip", "FarClip", "Yaw", "Pitch",
        "Distance", "ResetOrbit", "OcclusionEnabled", "Coverage", "FadeSpeed", "TargetHeight"};
    for (const auto language : {engine::Language::English, engine::Language::Chinese}) {
        engine::Translator::SetLanguage(language);
        f.Frame(); f.Frame();
        const auto* window = ImGui::FindWindowByName("Entity Inspector###EntityInspector");
        for (const char* control : controls)
            Require(f.Control(control).totalBounds.Max.x <= window->WorkRect.Max.x + 1.0f,
                std::string("narrow inspector keeps the full control label visible: ") + control);
    }
    std::puts("PASS all control labels fit the 300-pixel inspector in English and Chinese");
}
} // namespace

// Use Dear ImGui's observation hooks only to discover submitted rectangles.
// Every change above is driven through real mouse/keyboard events and the
// production inspector, not by a mocked widget result or component assignment.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* data) {
    if (!id) return;
    auto& item = items[id];
    item.bounds = data ? data->NavRect : bb;
    item.totalBounds = bb;
    item.flags = data ? data->ItemFlags : 0;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (id) items[id].label = label ? label : "";
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    const auto item = items.find(id);
    return item == items.end() ? nullptr : item->second.label.c_str();
}

int main() {
    LensAndOrbit();
    Occlusion();
    ModeAndTakeover();
    Language();
    NarrowLayout();
    std::puts("PASS all camera inspector tests (real ImGui and Flecs, no graphics backend)");
}
