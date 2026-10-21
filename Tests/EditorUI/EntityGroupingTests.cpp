#include "Runtime/Core/Application.hpp"
#include "Runtime/Renderer/RenderSystem.hpp"
#include "Runtime/Scene/blank.hpp"
#include "Runtime/Scene/EditorSceneAdapter.hpp"
#include "Runtime/UI/EditorHierarchyPanel.hpp"
#include "Runtime/Project/SceneDocument.hpp"
#include <imgui_internal.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace engine;
using Json = nlohmann::json;
namespace fs = std::filesystem;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void Near(const glm::mat4& a, const glm::mat4& b) {
    for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row)
        Check(std::abs(a[col][row] - b[col][row]) <= 2e-4f * (1 + std::abs(a[col][row])), "world transform changed unexpectedly");
}
struct Context { SceneManager* scene = nullptr; RenderSystem* renderer = nullptr; };
class TestScene final : public GameScene {
public:
    explicit TestScene(Context& context) : context(context) {}
    void Init(RenderSystem* renderer, SceneManager* scene, PhysicsSystem* physics, InputSystem* input,
        EventSystem* events, GameplayState* state, AnimationSystem* animation, AudioSystem* audio) override {
        context.scene = scene; context.renderer = renderer;
        blankScene.Init(renderer, scene, physics, input, events, state, animation, audio);
    }
    void Update(float dt) override { blankScene.Update(dt); }
    void Shutdown() override { blankScene.Shutdown(); }
private: Context& context; blank blankScene;
};
ApplicationOptions Options(Context& context, const fs::path& output) {
    ApplicationOptions options;
    options.createScene = [&] { return std::make_unique<TestScene>(context); };
    options.editorAvailable = options.showEditorInitially = options.editingProject = options.genericScene = true;
    options.imguiIniPath = (output / "grouping-layout.ini").string();
    options.windowTitle = "Entity grouping integration (hidden)";
    return options;
}
std::vector<flecs::entity> Parts(SceneManager& scene, flecs::entity first) {
    const auto import = first.get<ModelPartSource>().instance;
    std::vector<flecs::entity> result;
    scene.get_world().each([&](flecs::entity entity, const ModelPartSource& source) { if (source.instance == import) result.push_back(entity); });
    return result;
}
void BodyMatches(SceneManager& scene, flecs::entity entity) {
    if (!entity.has<PhysicsBody>()) return;
    const auto world = scene_hierarchy::World(entity);
    auto& bodies = scene.get_physics_system()->get_body_interface();
    const auto body = JPH::BodyID(entity.get<PhysicsBody>().bodyID);
    const auto position = bodies.GetPosition(body);
    Check(glm::length(glm::vec3(position.GetX(), position.GetY(), position.GetZ()) - glm::vec3(world[3])) < 0.002f,
        "child physics body must follow the group's world position");
    const auto shape = bodies.GetShape(body);
    if (entity.has<StaticObject>() && shape->GetSubType() == JPH::EShapeSubType::Scaled) {
        const auto scale = static_cast<const JPH::ScaledShape*>(shape.GetPtr())->GetScale();
        const auto pose = DecomposePhysicsTransform(world);
        Check(pose.has_value(), "a valid grouped collider must decompose");
        Check(glm::length(pose->scale - glm::vec3(scale.GetX(), scale.GetY(), scale.GetZ())) < 0.000002f,
            "child collision shape must follow the group scale");
    }
}

void TreeWidget(EditorSceneAdapter& adapter) {
    // Reuse the actual initialized editor ImGui context, without presenting a window.
    auto frame = [&] {
        unsigned char* pixels; int width, height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::GetIO().DisplaySize = ImVec2(900, 700);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(320, 650));
        ImGui::Begin("HierarchyFixture");
        editor_hierarchy::Draw(adapter);
        ImGui::End(); ImGui::Render();
    };
    frame(); frame();
    auto* hierarchy = ImGui::FindWindowByName("HierarchyFixture");
    Check(hierarchy != nullptr, "production hierarchy UI must render");
    const auto count = adapter.EntityCount();
    ImGui::ActivateItemByID(hierarchy->GetID("New Group")); frame();
    Check(adapter.EntityCount() == count + 1 && scene_hierarchy::IsGroup(adapter.SelectedEntity()), "New Group button must create a real container");
    Check(ImGui::GetDrawData()->TotalVtxCount > 0, "hierarchy emits real ImGui draw data");
    adapter.Destroy(adapter.Selection());
    std::cout << "PASS production hierarchy widget creates a real container\n";
}

void SelectionAndRenameRules() {
    SceneManager fixture; fixture.Init();
    EditorSceneAdapter adapter(&fixture);
    const auto a = adapter.CreateGroup({}, "A"), b = adapter.CreateGroup({}, "B");
    const auto c = adapter.CreateGroup({}, "C"), d = adapter.CreateGroup({}, "D");
    const auto hidden = adapter.CreateGroup({}, "Hidden");
    Check(adapter.Reparent({hidden.id()}, a.id()), "selection fixture needs a collapsed child");
    const std::vector<flecs::entity_t> rows{a.id(), b.id(), c.id(), d.id()};
    adapter.Select(b.id()); adapter.SelectRange(d.id(), rows);
    Check(adapter.SelectedIds().size() == 3 && adapter.IsSelected(b.id()) && adapter.IsSelected(c.id()) && adapter.Selection() == d.id(), "forward range selection failed");
    adapter.SelectRange(a.id(), rows);
    Check(adapter.SelectedIds().size() == 2 && adapter.IsSelected(a.id()) && adapter.IsSelected(b.id()) && !adapter.IsSelected(c.id()), "reverse Shift range must retain the original anchor");
    adapter.SelectRange(c.id(), rows);
    Check(adapter.SelectedIds().size() == 2 && adapter.IsSelected(b.id()) && adapter.IsSelected(c.id()), "repeated Shift must shrink from the original anchor");
    adapter.Select(a.id()); adapter.Select(d.id(), true); adapter.SelectRange(c.id(), rows, true);
    Check(adapter.SelectedIds().size() == 3 && adapter.IsSelected(a.id()) && adapter.IsSelected(c.id()) && adapter.IsSelected(d.id()) && !adapter.IsSelected(b.id()), "Ctrl+Shift must add a range without clearing other selection");
    adapter.Select(hidden.id()); adapter.SelectRange(c.id(), rows);
    Check(adapter.SelectedIds().size() == 1 && adapter.IsSelected(c.id()) && !adapter.IsSelected(hidden.id()), "collapsed anchor must fall back to the clicked row");
    adapter.Select(b.id()); fixture.DestroyEntity(b); adapter.SelectRange(d.id(), rows);
    Check(adapter.SelectedIds().size() == 1 && adapter.IsSelected(d.id()), "deleted range anchor must not leave stale selection");
    const auto identity = a.get<SceneEntityIdentity>().value;
    const auto transform = scene_hierarchy::World(hidden);
    Check(adapter.Rename(a.id(), "  Environment  ") && std::string(a.name().c_str()) == "Environment", "rename must trim surrounding whitespace");
    Check(!adapter.Rename(a.id(), "   ") && !adapter.Rename(a.id(), "Invalid::Name"), "invalid group names must be rejected");
    Check(adapter.Rename(a.id(), "C") && std::string(a.name().c_str()) == "C_1", "duplicate sibling names must receive a unique suffix");
    Check(a.get<SceneEntityIdentity>().value == identity && hidden.parent() == a, "rename must preserve identity and children");
    Near(scene_hierarchy::World(hidden), transform);
    std::cout << "PASS range anchors, reverse/additive ranges, collapsed/deleted rows and safe group rename\n";
}

void RealHierarchyInteractions() {
    SceneManager fixture; fixture.Init();
    EditorSceneAdapter adapter(&fixture);
    const auto a = adapter.CreateGroup({}, "A"), b = adapter.CreateGroup({}, "B");
    const auto c = adapter.CreateGroup({}, "C"), d = adapter.CreateGroup({}, "D");
    const auto hidden = adapter.CreateGroup({}, "Hidden");
    Check(adapter.Reparent({hidden.id()}, a.id()), "UI fixture needs a collapsed child");
    adapter.Select(d.id());
    auto& io = ImGui::GetIO();
    const auto frame = [&] {
        unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.DisplaySize = ImVec2(900, 700); io.DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(380, 650));
        ImGui::Begin("InteractionFixture"); editor_hierarchy::Draw(adapter); ImGui::End(); ImGui::Render();
    };
    frame(); frame();
    ImGuiWindow* list = nullptr;
    for (auto* window : GImGui->Windows)
        if (window->ParentWindow && std::string(window->ParentWindow->Name) == "InteractionFixture" && std::strstr(window->Name, "EntityList")) list = window;
    Check(list != nullptr, "real hierarchy list must exist");
    const auto click = [&](int row, bool shift = false, bool ctrl = false, int button = 0) {
        io.AddKeyEvent(ImGuiMod_Shift, shift); io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddMousePosEvent(list->DC.CursorStartPos.x + 40, list->DC.CursorStartPos.y + row * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight() * 0.5f);
        frame(); io.AddMouseButtonEvent(button, true); frame(); io.AddMouseButtonEvent(button, false); frame();
        io.AddKeyEvent(ImGuiMod_Shift, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
    };
    click(1);
    Check(adapter.Selection() == b.id(), "real row click must select B");
    click(3, true);
    Check(adapter.SelectedIds().size() == 3 && adapter.IsSelected(b.id()) && adapter.IsSelected(c.id()) && adapter.IsSelected(d.id()), "real Shift+left-click must select the visible B-D range");
    Check(!adapter.IsSelected(hidden.id()), "collapsed children must not be selected by a visible range");
    click(0, true);
    Check(adapter.SelectedIds().size() == 2 && adapter.IsSelected(a.id()) && adapter.IsSelected(b.id()), "real reverse Shift click must retain anchor B");
    click(3, false, true); click(2, true, true);
    Check(adapter.SelectedIds().size() == 4, "real Ctrl+Shift click must append a range");
    click(2);
    Check(adapter.SelectedIds().size() == 1 && adapter.Selection() == c.id(), "a plain click must collapse the previous range on release");
    const auto press = [&](ImGuiKey key) { io.AddKeyEvent(key, true); frame(); io.AddKeyEvent(key, false); frame(); frame(); };
    const auto popup = [&] { return ImGui::FindWindowByName(editor_hierarchy::kRenamePopup); };
    press(ImGuiKey_F2);
    if (!popup() || !popup()->Active || !io.WantTextInput)
        std::cerr << "Rename focus: popup=" << (popup() != nullptr) << " active=" << (popup() && popup()->Active)
            << " text=" << io.WantTextInput << " nav=" << (GImGui->NavWindow ? GImGui->NavWindow->Name : "none") << '\n';
    Check(popup() && popup()->Active && io.WantTextInput, "F2 must open and focus the actual rename dialog");
    io.AddInputCharactersUTF8("RenamedGroup"); frame(); press(ImGuiKey_Enter);
    Check(std::string(c.name().c_str()) == "RenamedGroup", "Enter must commit the actual group name");
    press(ImGuiKey_F2);
    io.AddInputCharactersUTF8("Cancelled"); frame(); press(ImGuiKey_Escape);
    Check(std::string(c.name().c_str()) == "RenamedGroup", "Escape must cancel the rename without mutation");
    click(2, false, false, 1);
    ImGuiWindow* context = nullptr;
    for (auto* window : GImGui->Windows)
        if (window->Active && (window->Flags & ImGuiWindowFlags_Popup) && !(window->Flags & ImGuiWindowFlags_Modal)) context = window;
    Check(context != nullptr, "right-click must expose the hierarchy context menu");
    ImGui::ActivateItemByID(context->GetID("Rename Group")); frame(); frame();
    Check(popup() && popup()->Active, "context menu must open the same group rename dialog");
    press(ImGuiKey_Escape);
    std::cout << "PASS real ImGui Shift/Ctrl+Shift clicks, F2, Enter/Escape and context-menu rename\n";
}

Json ExerciseGroups(Context& ctx) {
    auto& scene = *ctx.scene; auto& renderer = *ctx.renderer;
    EditorSceneAdapter adapter(&scene);
    const auto initial = glm::translate(glm::mat4(1), {3, 2, -4}) * glm::scale(glm::mat4(1), glm::vec3(0.028f));
    const auto first = scene.LoadModel(&renderer, "Assets/Models/sponza.glb", ModelPhysicsType::Static, 0, initial);
    Check(IsEntityAlive(first), "Sponza import failed");
    const auto parts = Parts(scene, first);
    Check(parts.size() > 1, "Sponza must contain multiple mesh entities");
    for (auto part : parts) Check(part.has<PhysicsBody>(), "every Sponza part needs its real collision body");
    std::unordered_map<flecs::entity_t, glm::mat4> before;
    for (auto part : parts) before[part.id()] = scene_hierarchy::World(part);
    const auto root = adapter.GroupImportedModel(first.id(), &initial);
    Check(IsEntityAlive(root) && std::string(root.name().c_str()) == "sponza", "the imported model needs a named root");
    Near(root.get<LocalTransform>().matrix, initial);
    for (auto part : parts) {
        Check(part.parent() == root, "every imported mesh must be under its model root");
        Near(scene_hierarchy::World(part), before.at(part.id())); BodyMatches(scene, part);
    }
    Check(adapter.GroupImportedModel(parts.front().id()) == root, "grouping an already grouped import should select its existing root");
    auto moved = glm::translate(glm::mat4(1), {1, 3, 2}) * glm::rotate(glm::mat4(1), 0.4f, glm::vec3(0, 1, 0)) *
        glm::scale(glm::mat4(1), glm::vec3(2)) * initial;
    Check(adapter.SetTransform(root.id(), moved), "group transform edit failed");
    for (auto part : parts) { Near(scene_hierarchy::World(part), moved * glm::inverse(initial) * before.at(part.id())); BodyMatches(scene, part); }
    const auto countVisible = scene.get_render_batches().size();
    Check(countVisible >= parts.size(), "grouped meshes still render");
    adapter.SetVisible(root.id(), false);
    Check(scene.get_render_batches().size() == countVisible - parts.size(), "parent visibility must hide all children");
    adapter.SetVisible(root.id(), true);
    Check(scene.get_render_batches().size() == countVisible, "showing group must restore child rendering");
    std::cout << "PASS real Sponza import: one root, preserved poses, group TRS, Jolt bodies and inherited visibility\n";

    const auto branch = adapter.CreateGroup({}, "Other");
    Check(adapter.SetTransform(branch.id(), glm::translate(glm::mat4(1), {-8, 1, 5}) *
        glm::rotate(glm::mat4(1), 0.3f, glm::vec3(0, 1, 0))), "branch transform failed");
    const auto child = parts.front(); const auto originalWorld = scene_hierarchy::World(child);
    Check(adapter.Reparent({child.id()}, branch.id()), "moving existing mesh into another group failed");
    Near(scene_hierarchy::World(child), originalWorld); BodyMatches(scene, child);
    auto childWorld = glm::translate(glm::mat4(1), {2, 0, 0}) * originalWorld;
    Check(adapter.SetWorldTransform(child.id(), childWorld), "world-space gizmo failed under a rotated parent");
    Near(scene_hierarchy::World(child), childWorld); BodyMatches(scene, child);
    const auto oldLocal = branch.get<LocalTransform>().matrix;
    Check(!adapter.Reparent({branch.id()}, child.id()), "parent cycle must be rejected"); Near(branch.get<LocalTransform>().matrix, oldLocal);
    const auto camera = FindGameCameraEntity(scene.get_world());
    const auto unchangedCount = adapter.EntityCount();
    Check(!IsEntityAlive(adapter.CreateGroup({camera.id(), child.id()})), "reserved game camera must stay at root");
    Check(adapter.EntityCount() == unchangedCount && !IsEntityAlive(camera.parent()), "rejected grouping must leave the world untouched");
    auto singular = glm::mat4(1); singular[0][0] = 0;
    Check(!adapter.SetTransform(branch.id(), singular), "singular group transform must be rejected");
    Near(scene_hierarchy::World(child), childWorld);
    Check(adapter.Ungroup(branch.id()), "ungroup failed"); Near(scene_hierarchy::World(child), childWorld); BodyMatches(scene, child);
    Check(!IsEntityAlive(child.parent()), "ungroup must restore children to the original parent");
    adapter.Select(child.id()); adapter.Select(parts[1].id(), true);
    Check(adapter.SelectedIds().size() == 2, "multi-selection must retain both entities");
    const auto mixed = adapter.CreateGroup(adapter.SelectedIds(), "Selection");
    Check(IsEntityAlive(mixed) && child.parent() == mixed && parts[1].parent() == mixed, "manual multi-entity grouping failed");
    Near(scene_hierarchy::World(child), childWorld);
    const auto nested = adapter.CreateGroup({root.id(), mixed.id()}, "Environment");
    Check(IsEntityAlive(nested) && root.parent() == nested && mixed.parent() == nested, "nested grouping failed");
    Check(adapter.Rename(mixed.id(), "SelectedParts"), "group rename failed");
    glm::vec3 minimum, maximum;
    Check(adapter.WorldBounds(nested.id(), minimum, maximum) && glm::length(maximum - minimum) > 1, "group framing needs combined child bounds");
    const auto dynamic = scene.LoadModel(&renderer, "Assets/Models/Beacon.glb", ModelPhysicsType::Dynamic, 1,
        glm::translate(glm::mat4(1), {2, 5, 0}) * glm::scale(glm::mat4(1), glm::vec3(0.5f)));
    const auto dynamicGroup = adapter.CreateGroup({dynamic.id()}, "DynamicGroup");
    Check(adapter.SetTransform(dynamicGroup.id(), dynamicGroup.get<LocalTransform>().matrix * glm::scale(glm::mat4(1), glm::vec3(2))), "dynamic group scaling failed");
    const auto dynamicShape = scene.get_physics_system()->get_body_interface().GetShape(JPH::BodyID(dynamic.get<PhysicsBody>().bodyID));
    Check(dynamicShape->GetSubType() == JPH::EShapeSubType::Scaled, "dynamic group needs a scaled collision wrapper");
    const auto dynamicScale = static_cast<const JPH::ScaledShape*>(dynamicShape.GetPtr())->GetScale();
    Check(std::abs(dynamicScale.GetX() - 2) < 1e-4f, "dynamic collider must scale relative to its baked import scale");
    Check(adapter.Reparent({dynamicGroup.id()}, nested.id()), "dynamic group nesting failed");
    TreeWidget(adapter);
    std::cout << "PASS multi-selection, nested groups, world gizmos, reparent/ungroup, cycles and camera protection\n";
    return CaptureSceneDocument(scene, renderer);
}

void CompareDocuments(const Json& a, const Json& b) {
    if (a.is_number() && b.is_number()) {
        Check(std::abs(a.get<double>() - b.get<double>()) < 1e-3 * (1 + std::abs(a.get<double>())), "scene document number changed after reload"); return;
    }
    Check(a.type() == b.type() && a.size() == b.size(), "scene document structure changed after reload");
    if (a.is_object()) for (auto it = a.begin(); it != a.end(); ++it) CompareDocuments(it.value(), b.at(it.key()));
    else if (a.is_array()) for (std::size_t i = 0; i < a.size(); ++i) CompareDocuments(a[i], b[i]);
    else Check(a == b, "scene document value changed after reload");
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        Check(argc == 2, "Expected test output directory");
        const auto output = fs::absolute(argv[1]); fs::create_directories(output);
        const auto sceneFile = output / "grouped.scene.json";
        Json document;
        { Context ctx; Application app(Options(ctx, output)); SelectionAndRenameRules(); RealHierarchyInteractions();
          document = ExerciseGroups(ctx); std::ofstream(sceneFile) << document.dump(2); }
        { Context ctx; auto options = Options(ctx, output); options.sceneSnapshotPath = sceneFile.string(); Application app(std::move(options));
          CompareDocuments(document, CaptureSceneDocument(*ctx.scene, *ctx.renderer));
          EditorSceneAdapter adapter(ctx.scene);
          const auto group = ctx.scene->get_world().lookup("Environment");
          Check(IsEntityAlive(group), "saved group root was not restored");
          std::vector<uint32_t> bodies;
          const auto subtree = scene_hierarchy::Subtree(group);
          for (auto node : subtree) if (node.has<PhysicsBody>()) { BodyMatches(*ctx.scene, node); bodies.push_back(node.get<PhysicsBody>().bodyID); }
          Check(!bodies.empty() && adapter.Destroy(group.id()), "deleting a restored group failed");
          for (auto node : subtree) Check(!IsEntityAlive(node), "deleting a group must delete every child");
          Check(ctx.scene->get_world().count<PhysicsBody>() == 0, "group deletion must release every child body"); }
        std::cout << "PASS saved hierarchy round trip, restored physics and recursive group deletion\nAll entity grouping integration tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
