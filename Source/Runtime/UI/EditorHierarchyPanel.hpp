#pragma once

#include "../Scene/EditorSceneAdapter.hpp"
#include <imgui.h>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cstdio>

namespace engine::editor_hierarchy {
inline constexpr const char* kRenamePopup = "Rename Group###HierarchyRename";
struct InteractionState {
    SceneManager* owner = nullptr;
    flecs::entity_t lastPrimary = 0, renameId = 0, plainClick = 0;
    std::array<char, 1024> name{};
    bool focusName = false, draggingSelection = false;
    std::string renameError;
};
// Called inside the hierarchy window. Resource drops remain owned by EngineUi.
inline std::string Draw(EditorSceneAdapter& scene) {
    static InteractionState state;
    if (state.owner != scene.Scene()) { state = {}; state.owner = scene.Scene(); }
    scene.SyncSelection();
    std::string error;
    const auto selected = scene.SelectedIds();
    const bool canGroup = !selected.empty() && scene.CanReparent(selected);
    const std::string disabledReason = scene.Error();
    enum class Action { None, New, Group, Ungroup, Reparent, ImportGroup, Rename };
    Action action = Action::None;
    flecs::entity_t subject = 0, parent = 0;
    std::vector<flecs::entity_t> move;
    if (ImGui::Button("New Group")) action = Action::New;
    ImGui::SameLine();
    ImGui::BeginDisabled(!canGroup);
    if (ImGui::Button("Group")) action = Action::Group;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", canGroup ? "Group selection (Ctrl+G). Ctrl+click selects multiple entities." : disabledReason.c_str());
    const bool shortcuts = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (shortcuts && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        if (ImGui::GetIO().KeyShift) { action = Action::Ungroup; subject = scene.Selection(); }
        else if (canGroup) action = Action::Group;
    }
    if (shortcuts && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_F2, false) && scene_hierarchy::IsGroup(scene.SelectedEntity())) {
        action = Action::Rename; subject = scene.Selection();
    }
    ImGui::TextDisabled("Ctrl: toggle | Shift: range");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+click: toggle selection\nShift+click: select a visible range\nCtrl+Shift+click: add a range\nF2: rename selected group");
    const auto entities = scene.ListEntities();
    std::unordered_map<flecs::entity_t, std::vector<const EditorEntitySnapshot*>> children;
    std::unordered_set<flecs::entity_t> available, expand;
    for (const auto& node : entities) available.insert(node.id);
    for (const auto& node : entities) children[available.contains(node.parent) ? node.parent : 0].push_back(&node);
    if (state.lastPrimary != scene.Selection() && scene.IsAlive(scene.Selection())) {
        auto ancestor = scene.SelectedEntity().parent();
        while (IsEntityAlive(ancestor)) { expand.insert(ancestor.id()); ancestor = ancestor.parent(); }
    }
    state.lastPrimary = scene.Selection();
    std::vector<flecs::entity_t> visibleOrder;
    flecs::entity_t rangeTarget = 0;
    bool additiveRange = false;
    if (ImGui::BeginChild("EntityList", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        std::function<void(const EditorEntitySnapshot&)> drawNode = [&](const auto& node) {
            visibleOrder.push_back(node.id);
            const bool hasChildren = children.contains(node.id);
            auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (scene.IsSelected(node.id)) flags |= ImGuiTreeNodeFlags_Selected;
            if (expand.contains(node.id)) ImGui::SetNextItemOpen(true);
            ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(node.id)));
            const bool open = ImGui::TreeNodeEx("Node", flags, "%s", node.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                if (ImGui::GetIO().KeyShift) { rangeTarget = node.id; additiveRange = ImGui::GetIO().KeyCtrl; }
                else if (!ImGui::GetIO().KeyCtrl && scene.IsSelected(node.id) && scene.SelectedIds().size() > 1) {
                    state.plainClick = node.id;
                    state.draggingSelection = false;
                } else
                    scene.Select(node.id, ImGui::GetIO().KeyCtrl);
            }
            if (ImGui::BeginDragDropSource()) {
                state.draggingSelection = true;
                if (!scene.IsSelected(node.id)) scene.Select(node.id);
                ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", &node.id, sizeof(node.id));
                ImGui::Text("Move %zu selected entities", scene.SelectedIds().size());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
                    action = Action::Reparent; parent = node.id;
                    move = scene.SelectedIds();
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("EntityActions")) {
                if (!scene.IsSelected(node.id)) scene.Select(node.id);
                if (ImGui::MenuItem("Rename Group", "F2", false, node.group)) { action = Action::Rename; subject = node.id; }
                if (ImGui::MenuItem("Group selection", "Ctrl+G", false, scene.CanReparent(scene.SelectedIds()))) action = Action::Group;
                if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, node.group)) { action = Action::Ungroup; subject = node.id; }
                if (ImGui::MenuItem("Move to scene root", nullptr, false, node.parent != 0)) {
                    action = Action::Reparent; parent = 0; move = scene.SelectedIds();
                }
                const auto entity = scene.Scene()->get_world().entity(node.id);
                if (ImGui::MenuItem("Group imported model", nullptr, false, entity.has<ModelPartSource>())) {
                    action = Action::ImportGroup; subject = node.id;
                }
                ImGui::EndPopup();
            }
            if (hasChildren && open) {
                for (const auto* child : children[node.id]) drawNode(*child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        if (children.contains(0)) for (const auto* root : children[0]) drawNode(*root);
    }
    ImGui::EndChild();
    // Build the complete expanded-row order before applying a range. Descendants
    // of collapsed groups are omitted, including when the anchor was collapsed.
    if (rangeTarget) scene.SelectRange(rangeTarget, visibleOrder, additiveRange);
    // Keep a multi-selection while dragging. A plain click without a drag
    // collapses it on release and establishes a new Shift-range anchor.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (state.plainClick && !state.draggingSelection) scene.Select(state.plainClick);
        state.plainClick = 0;
        state.draggingSelection = false;
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state.plainClick = 0;
        state.draggingSelection = false;
    }
    // A drop on empty list space detaches the selection to the scene root.
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
            action = Action::Reparent; parent = 0; move = scene.SelectedIds();
        }
        ImGui::EndDragDropTarget();
    }
    switch (action) {
    case Action::New: scene.CreateGroup(); break;
    case Action::Group: if (!IsEntityAlive(scene.CreateGroup(scene.SelectedIds()))) error = scene.Error(); break;
    case Action::Ungroup: if (!scene.Ungroup(subject)) error = scene.Error(); break;
    case Action::Reparent: if (!scene.Reparent(move, parent)) error = scene.Error(); break;
    case Action::ImportGroup: if (!IsEntityAlive(scene.GroupImportedModel(subject))) error = scene.Error(); break;
    case Action::Rename:
        state.renameId = subject;
        state.focusName = true;
        state.renameError.clear();
        std::snprintf(state.name.data(), state.name.size(), "%s", scene.Inspect(subject).name.c_str());
        ImGui::OpenPopup(kRenamePopup);
        break;
    default: break;
    }
    if (ImGui::BeginPopupModal(kRenamePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!scene.IsAlive(state.renameId)) ImGui::CloseCurrentPopup();
        else {
            if (state.focusName) { ImGui::SetKeyboardFocusHere(); state.focusName = false; }
            ImGui::SetNextItemWidth(300);
            const bool entered = ImGui::InputText("Name", state.name.data(), state.name.size(),
                ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
            if (!state.renameError.empty()) ImGui::TextWrapped("%s", state.renameError.c_str());
            const bool save = ImGui::Button("Rename");
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (cancel) ImGui::CloseCurrentPopup();
            else if (entered || save) {
                if (scene.Rename(state.renameId, state.name.data())) ImGui::CloseCurrentPopup();
                else state.renameError = scene.Error();
            }
        }
        ImGui::EndPopup();
    }
    return error;
}
}
