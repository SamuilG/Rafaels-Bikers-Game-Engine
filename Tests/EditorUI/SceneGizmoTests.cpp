#include "Runtime/UI/EditorGizmoGeometry.hpp"
#include "Runtime/UI/EditorGizmoToolbar.hpp"
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace engine;
namespace gizmo = engine::editor_gizmo;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(float a, float b) { Check(std::isfinite(a) && std::abs(a - b) < 0.001f, "geometry mismatch"); }

void LightGuidesMatchRendering() {
    LightComponent light;
    light.range = 7;
    light.direction = {0, 0, -1};
    const glm::mat4 pose = glm::translate(glm::mat4(1), {13, -4, 2}) *
        glm::rotate(glm::mat4(1), 0.8f, glm::vec3(0, 1, 0));
    const glm::vec3 origin(pose[3]);
    for (auto type : {LightType::Point, LightType::Spot, LightType::Directional}) {
        light.type = type;
        const auto lines = gizmo::LightGuide(light, pose);
        Check(!lines.empty(), "every supported light must have a guide");
        const glm::vec3 rays(MakeGpuLight(light, pose).direction);
        if (type == LightType::Point) {
            for (auto line : lines) {
                Near(glm::length(line.start - origin), light.range);
                Near(glm::length(line.end - origin), light.range);
            }
        } else if (type == LightType::Spot) {
            // Cone circles lie on the radial range sphere, including rotated lights.
            for (int i = 0; i < 48; ++i) {
                Near(glm::length(lines[i].end - origin), light.range);
                Near(glm::dot(glm::normalize(lines[i].end - origin), rays), std::cos(glm::radians(light.innerCutOff)));
            }
            Near(glm::length(lines.back().end - origin), light.range);
            Near(glm::dot(glm::normalize(lines.back().end - origin), rays), 1);
        } else {
            Check(lines.size() == 9, "directional guide has three parallel arrows");
            for (int i = 0; i < 9; i += 3) {
                Near(glm::length(lines[i].end - lines[i].start), 2);
                Near(glm::dot(glm::normalize(lines[i].end - lines[i].start), rays), 1);
            }
        }
    }
    light.range = std::numeric_limits<float>::quiet_NaN();
    Check(gizmo::LightGuide(light, pose).empty(), "invalid lights must not poison ImGui vertices");
    std::cout << "PASS light guides match uploaded direction, cone angle and radial range\n";
}

void FocusFitsCompleteBounds() {
    const glm::mat4 pose = glm::translate(glm::mat4(1), {30, -8, 40}) *
        glm::rotate(glm::mat4(1), 0.8f, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-2, 3, 1});
    const glm::vec3 minimum(2, -1, -4), maximum(7, 3, 2);
    for (float aspect : {0.4f, 1.0f, 16.0f / 9.0f, 3.0f}) {
        const auto camera = gizmo::FrameBounds(glm::mat4(1), pose, minimum, maximum, 60, aspect, 0.1f, 1000);
        Check(camera.has_value(), "valid bounds must frame");
        const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 1000.0f) * glm::inverse(*camera);
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner(pose * glm::vec4(i & 1 ? maximum.x : minimum.x,
                i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z, 1));
            Check(editor_camera_gizmo::ProjectPoint(projection, corner).has_value(), "framing must fit every transformed corner");
        }
    }
    Check(!gizmo::FrameBounds(glm::mat4(1), pose, minimum, maximum, 60, 1, 0.1f, 5), "clip range overflow must be rejected");
    Check(!gizmo::FrameBounds(glm::mat4(1), pose, maximum, minimum, 60, 1, 0.1f, 1000), "inverted bounds must be rejected");
    Check(!gizmo::FrameBounds(glm::mat4(1), pose, minimum, maximum, 60, 0, 0.1f, 1000), "zero viewport must be rejected");
    std::cout << "PASS framing fits rotated, reflected, offset bounds in portrait and wide viewports\n";
}

struct Fixture {
    EditorGizmoSettings settings;
    bool cameraIcon = true, frustum = true, full = false, canTransform = true, noScale = false;
    float preview = 5;
    ImGuiWindow* child = nullptr;
    ImVec2 imageOrigin;
    gizmo::ToolbarResult toolbar;
    void Frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(746, 420));
        ImGui::Begin("Fixture", nullptr, ImGuiWindowFlags_NoDecoration);
        toolbar = gizmo::DrawToolbar(settings, true, canTransform, false, noScale, cameraIcon, frustum, full, preview);
        imageOrigin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        for (auto* window : GImGui->Windows)
            if (window->ParentWindow == ImGui::GetCurrentWindow() && std::strstr(window->Name, "SceneTools")) child = window;
        ImGui::End();
        ImGui::Render();
    }
    void Activate(const char* label) {
        Check(child != nullptr, "toolbar child must exist");
        ImGui::ActivateItemByID(child->GetID(label));
        Frame();
    }
};

void RealToolbarInput() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(746, 420);
    io.DeltaTime = 1.0f / 60;
    io.Fonts->AddFontFromFileTTF("Assets/Fonts/simhei.ttf", 18.0f);
    unsigned char* data; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);
    editor_theme::Apply();
    Fixture f;
    f.Frame(); f.Frame();
    Check(f.child->ContentSize.x < f.child->InnerRect.GetWidth(), "toolbar must fit the screenshot's viewport width");
    Check(f.child->ContentSize.y <= f.child->InnerRect.GetHeight(), "both toolbar rows must fit without clipping");
    Check(f.imageOrigin.y >= f.child->Pos.y + f.child->Size.y, "toolbar must reserve pixels outside the scene picking rectangle");
    Check(ImGui::GetDrawData()->TotalVtxCount > 0, "toolbar must produce actual draw data");
    f.Activate("E Rotate"); Check(f.settings.tool == EditorTransformTool::Rotate, "rotate button must change production tool");
    f.Activate("World"); Check(f.settings.localSpace, "World button must change production space");
    f.Activate("Snap"); Check(f.settings.snap, "snap checkbox must change production snapping");
    f.Activate("F Frame"); Check(f.toolbar.focus, "frame button must emit the camera framing request");
    f.noScale = true;
    f.Activate("R Scale"); Check(f.settings.tool == EditorTransformTool::Rotate, "camera/light scale button must be disabled");
    f.canTransform = false;
    f.Activate("W Move"); Check(f.settings.tool == EditorTransformTool::Rotate, "read-only selection must not enable move");
    f.Activate("Q Select"); Check(f.settings.tool == EditorTransformTool::Select, "select-only remains usable");
    io.AddMousePosEvent(f.child->Pos.x + 5, f.child->Pos.y + 5);
    f.Frame(); f.Frame();
    Check(f.toolbar.hovered, "real pointer over toolbar must block scene picking");
    f.Activate("Gizmos");
    Check(ImGui::IsPopupOpen("GizmoDisplay", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel), "Gizmos button must open actual display controls");
    Check(f.toolbar.hovered, "open settings must block scene input");
    ImGui::DestroyContext();
    std::cout << "PASS real ImGui toolbar actions, disabled tools, popup and pointer isolation\n";
}

void ManipulatorConfiguration() {
    EditorGizmoSettings settings;
    Check(gizmo::Space(settings, EditorTransformTool::Move) == ImGuizmo::WORLD, "move defaults to world axes");
    Check(gizmo::Space(settings, EditorTransformTool::Scale) == ImGuizmo::LOCAL, "scale uses supported local axes");
    settings.localSpace = true;
    Check(gizmo::Space(settings, EditorTransformTool::Rotate) == ImGuizmo::LOCAL, "local rotation setting reaches ImGuizmo");
    settings.moveSnap = 0.25f; settings.rotateSnap = 30; settings.scaleSnap = 0.2f;
    Near(gizmo::SnapValues(settings, EditorTransformTool::Move)[2], 0.25f);
    Near(gizmo::SnapValues(settings, EditorTransformTool::Rotate)[0], 30);
    Near(gizmo::SnapValues(settings, EditorTransformTool::Scale)[0], 0.2f);
    settings.moveSnap = std::numeric_limits<float>::quiet_NaN();
    Check(std::isfinite(gizmo::SnapValues(settings, EditorTransformTool::Move)[0]), "invalid snap setting must be sanitized");
    std::cout << "PASS operation-specific snapping and supported coordinate spaces\n";
}

glm::vec3 DragHandle(bool local, bool snapping) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(600, 400); io.DeltaTime = 1.0f / 60;
    io.Fonts->AddFontDefault();
    unsigned char* atlas; int w, h; io.Fonts->GetTexDataAsRGBA32(&atlas, &w, &h);
    EditorGizmoSettings settings;
    settings.localSpace = local; settings.moveSnap = 0.5f;
    const glm::mat4 view = glm::translate(glm::mat4(1), {0, 0, -10});
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.5f, 0.1f, 100.0f);
    auto model = glm::rotate(glm::mat4(1), glm::radians(45.0f), glm::vec3(0, 0, 1));
    const auto frame = [&](ImVec2 mouse) {
        io.AddMousePosEvent(mouse.x, mouse.y);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(600, 400));
        ImGui::Begin("Manipulator", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGuizmo::BeginFrame(); ImGuizmo::Enable(true); ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList()); ImGuizmo::SetRect(0, 0, 600, 400);
        ImGuizmo::SetGizmoSizeClipSpace(settings.handleSize);
        const auto snap = gizmo::SnapValues(settings, EditorTransformTool::Move);
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), gizmo::Operation(EditorTransformTool::Move),
            gizmo::Space(settings, EditorTransformTool::Move), glm::value_ptr(model), nullptr, snapping ? snap.data() : nullptr);
        ImGui::End(); ImGui::Render();
    };
    const ImVec2 direction = local ? ImVec2(0.70710678f, -0.70710678f) : ImVec2(1, 0);
    ImVec2 start;
    bool found = false;
    frame(ImVec2(0, 0)); frame(ImVec2(0, 0));
    for (float pixels : {24.0f, 32.0f, 40.0f, 48.0f, 56.0f}) {
        start = ImVec2(300 + direction.x * pixels, 200 + direction.y * pixels);
        frame(start);
        if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)) { found = true; break; }
    }
    Check(found, "actual ImGuizmo X handle must be pickable");
    io.AddMouseButtonEvent(0, true); frame(start);
    Check(ImGuizmo::IsUsing(), "mouse press must acquire the real transform handle");
    const ImVec2 end(start.x + direction.x * 37, start.y + direction.y * 37);
    frame(end);
    io.AddMouseButtonEvent(0, false); frame(end);
    Check(!ImGuizmo::IsUsing(), "mouse release must end transform drag");
    const glm::vec3 translation(model[3]);
    ImGui::DestroyContext();
    return translation;
}

void RealManipulatorDrag() {
    const auto world = DragHandle(false, false);
    Check(world.x > 0.1f, "world drag must move the entity"); Near(world.y, 0); Near(world.z, 0);
    const auto local = DragHandle(true, false);
    Check(local.x > 0.1f && local.y > 0.1f, "local drag follows rotated object axes"); Near(local.x, local.y); Near(local.z, 0);
    const auto snapped = DragHandle(false, true);
    Check(snapped.x > 0.1f, "snapped drag must move the entity");
    Near(snapped.x / 0.5f, std::round(snapped.x / 0.5f)); Near(snapped.y, 0);
    std::cout << "PASS real ImGuizmo mouse drags in world/local space and 0.5 m snapping\n";
}

int main() {
    try { LightGuidesMatchRendering(); FocusFitsCompleteBounds(); ManipulatorConfiguration(); RealToolbarInput(); RealManipulatorDrag(); }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    std::cout << "All Scene Gizmo test groups passed.\n";
}
