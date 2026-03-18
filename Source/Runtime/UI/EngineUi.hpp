#pragma once

#include "../Renderer/RenderUtilities/setup.hpp" 

#include <flecs.h>
#include <glm/glm.hpp>

namespace engine {

    class RenderSystem;
    class SceneManager;

    class EngineUi {
    public:
        static void DrawControlPanel(UserState& state, RenderSystem* renderSys, SceneManager* sceneManager);

        //static void DrawSceneHierarchy(SceneManager* sceneManager);
        static void DrawSceneHierarchy(SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id);
		
        //Game start menu 游戏开始菜单
        static void DrawMainMenu(RenderSystem* renderSys, bool& appRunning, bool& isGameStarted);
		//Game over menu 游戏结束菜单
        static void DrawGameOver(RenderSystem* renderSys, UserState& state, bool& appRunning);
		//Pause menu 暂停菜单
        static void DrawGamePause(RenderSystem* renderSys, UserState& state, bool& appRunning);
    private:
        // 编辑器 UI 的状态缓存
        inline static flecs::entity_t m_current_inspected_id = 0;
        inline static float m_ui_translation[3] = { 0.0f, 0.0f, 0.0f };
        inline static float m_ui_rotation[3] = { 0.0f, 0.0f, 0.0f };
        inline static float m_ui_scale[3] = { 1.0f, 1.0f, 1.0f };

    };

} // namespace engine