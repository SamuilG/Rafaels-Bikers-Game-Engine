#pragma once

#include <initializer_list>

#include <imgui.h>
#include <imgui_internal.h>

namespace engine::editor_layout {

    inline constexpr const char* kDockspaceName = "SteerEditorDockspaceV2";
    inline constexpr const char* kSceneHierarchy = "Scene Hierarchy###SceneHierarchy";
    inline constexpr const char* kEntityInspector = "Entity Inspector###EntityInspector";
    inline constexpr const char* kRenderSettings = "Render Settings###RenderSettings";
    inline constexpr const char* kLighting = "Lighting###Lighting";
    inline constexpr const char* kCamera = "Camera###Camera";
    inline constexpr const char* kAudio = "Audio###Audio";
    inline constexpr const char* kDiagnostics = "Diagnostics###Diagnostics";
    inline constexpr const char* kParticles = "Particles###Particles";
    inline constexpr const char* kRuntimeUiDebug = "Runtime UI Debug###RuntimeUiDebug";
    inline constexpr const char* kSceneViewport = "Scene Viewport###SceneViewportEditor";
    inline constexpr const char* kGameViewport = "Game Viewport###GameViewportEditor";
    inline constexpr const char* kAssets = "Assets###ContentBrowser";
    inline constexpr const char* kConsole = "Console###OutputConsole";

    // Submit once per editor frame, before the docked windows. This replaces the
    // old DockSpaceOverViewport(0, ...) call; it already creates the viewport host.
    // ImGui owns persistence. A new versioned root ignores old default layouts,
    // while an existing root is left intact until the user explicitly resets it.
    // False means the viewport is not ready; callers must retain pending reset
    // requests. Keep existing nodes alive until they can be submitted normally.
    inline bool DrawDockspace(bool reset = false) {
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return false;
        }

        // A context-independent hash avoids changing the dockspace ID if its
        // caller has an ImGui window or PushID scope active.
        const ImGuiID dockspaceId = ImHashStr(kDockspaceName);
        constexpr ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_PassthruCentralNode;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float splitWidth = viewport->WorkSize.x - style.DockingSeparatorSize;
        const float inspectorWidth = ImTrunc(splitWidth * 0.25f);
        const float leftWidth = splitWidth - inspectorWidth - style.DockingSeparatorSize;
        const float hierarchyWidth = ImTrunc(leftWidth * (0.18f / 0.75f));
        const float splitHeight = viewport->WorkSize.y - style.DockingSeparatorSize;
        const float bottomHeight = ImTrunc(splitHeight * 0.22f);
        // Do not commit default ratios from a hidden/minimized window's tiny
        // dimensions. Derive readiness from the actual minimum pane sizes, not
        // a fixed display resolution, so scaled styles work as well.
        const bool ready = inspectorWidth >= style.WindowMinSize.x &&
            hierarchyWidth >= style.WindowMinSize.x &&
            leftWidth - hierarchyWidth >= style.WindowMinSize.x &&
            bottomHeight >= style.WindowMinSize.y &&
            splitHeight - bottomHeight >= style.WindowMinSize.y;
        if (!ready) {
            if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
                ImGui::DockSpace(dockspaceId, ImVec2(0, 0), flags | ImGuiDockNodeFlags_KeepAliveOnly);
            }
            return false;
        }
        const bool rebuild = reset || ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
        if (rebuild) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | flags);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID centerId = dockspaceId;
            const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(
                centerId, ImGuiDir_Right, 0.25f, nullptr, &centerId);
            const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
                centerId, ImGuiDir_Down, 0.22f, nullptr, &centerId);
            // The remaining region is 75% of the viewport width. Make the left
            // hierarchy 18% of the whole viewport, rather than 18% of that region.
            const ImGuiID hierarchyId = ImGui::DockBuilderSplitNode(
                centerId, ImGuiDir_Left, 0.18f / 0.75f, nullptr, &centerId);

            ImGui::DockBuilderDockWindow(kSceneHierarchy, hierarchyId);
            ImGui::DockBuilderDockWindow(kSceneViewport, centerId);
            ImGui::DockBuilderDockWindow(kGameViewport, centerId);
            ImGui::DockBuilderDockWindow(kEntityInspector, inspectorId);
            for (const char* title : {kRenderSettings, kLighting, kCamera, kAudio, kDiagnostics, kParticles, kRuntimeUiDebug}) {
                ImGui::DockBuilderDockWindow(title, inspectorId);
            }
            ImGui::DockBuilderDockWindow(kAssets, bottomId);
            ImGui::DockBuilderDockWindow(kConsole, bottomId);
            ImGui::DockBuilderFinish(dockspaceId);
        }

        // Scene and Game select the single world-rendering surface. Keep them
        // as peer tabs while preserving the rest of an existing workspace.
        {
            ImGuiID sceneDockId = 0;
            if (const ImGuiWindow* scene = ImGui::FindWindowByName(kSceneViewport))
                sceneDockId = scene->DockId;
            else if (const ImGuiWindowSettings* scene = ImGui::FindWindowSettingsByID(ImHashStr(kSceneViewport)))
                sceneDockId = scene->DockId;
            if (sceneDockId == 0) {
                if (const ImGuiDockNode* center = ImGui::DockBuilderGetCentralNode(dockspaceId))
                    sceneDockId = center->ID;
            }
            if (sceneDockId != 0) {
                const ImGuiWindow* game = ImGui::FindWindowByName(kGameViewport);
                const ImGuiWindowSettings* gameSettings = ImGui::FindWindowSettingsByID(ImHashStr(kGameViewport));
                const ImGuiID gameDockId = game ? game->DockId : (gameSettings ? gameSettings->DockId : 0);
                if (gameDockId != sceneDockId) {
                    ImGui::DockBuilderDockWindow(kGameViewport, sceneDockId);
                    ImGui::DockBuilderFinish(dockspaceId);
                }
                if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(sceneDockId))
                    node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoUndocking);
            }
        }

        ImGui::DockSpaceOverViewport(dockspaceId, viewport, flags);

        // Newly created windows auto-select their tabs on the first frame. Wait
        // until real tabs exist before applying the default selections once.
        // Host storage belongs to this ImGui context and is not saved in the ini;
        // loaded layouts and subsequent user tab choices are never overwritten.
        ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspaceId);
        if (root == nullptr || root->HostWindow == nullptr) return true;
        ImGuiStorage& storage = root->HostWindow->StateStorage;
        const ImGuiID pendingKey = ImHashStr("SteerEditorDockspaceV2.DefaultTabsPending");
        const ImGuiID expiryKey = ImHashStr("SteerEditorDockspaceV2.DefaultTabsExpiry");
        int pending = rebuild ? 7 : storage.GetInt(pendingKey, 0);
        // A hidden default panel must not leave a future focus request behind.
        if (rebuild) storage.SetInt(expiryKey, ImGui::GetFrameCount() + 2);
        if (ImGui::GetFrameCount() > storage.GetInt(expiryKey, 0)) pending = 0;
        const char* defaultTabs[] = {kEntityInspector, kAssets, kSceneViewport};
        for (int index = 0; index < 3; ++index) {
            if ((pending & (1 << index)) == 0) continue;
            ImGuiWindow* window = ImGui::FindWindowByName(defaultTabs[index]);
            if (window == nullptr || window->DockNode == nullptr) continue;
            ImGuiDockNode* node = window->DockNode;
            if (ImGui::DockNodeGetRootNode(node) != root || node->TabBar == nullptr ||
                ImGui::TabBarFindTabByID(node->TabBar, window->TabId) == nullptr) continue;
            node->SelectedTabId = window->TabId;
            node->TabBar->SelectedTabId = node->TabBar->NextSelectedTabId = window->TabId;
            // Otherwise a tool auto-focused during its first Begin() would
            // reselect itself next frame, overriding the inspector selection.
            if (index == 0) ImGui::FocusWindow(window);
            pending &= ~(1 << index);
        }
        storage.SetInt(pendingKey, pending);
        return true;
    }

} // namespace engine::editor_layout
