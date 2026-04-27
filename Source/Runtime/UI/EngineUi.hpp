#pragma once

#include "../Renderer/RenderUtilities/setup.hpp" 
#include <backends/imgui_impl_vulkan.h>
#undef Bool
#include <flecs.h>
#include <glm/glm.hpp>
#include <string>
#include <imgui.h>
#include <format>
#include <print>


namespace engine {

    class RenderSystem;
    class SceneManager;
    class AudioSystem;

    class EngineUi {
    public:
        //================Logging System 日志系统=============================
        //printf
        static void LogPrintf(const char* fmt, ...);
        //std::print
        template <typename... Args>
        static void LogPrint(std::string_view fmt, Args&&... args) {
            std::string msg = std::vformat(fmt, std::make_format_args(args...));
            PushLogMessage(msg);
        }
		//推送到控制台的底层接口//push to console underlying interface
        static void PushLogMessage(const std::string& msg);


        //================Project Management 项目管理==========================
		// 绘制顶部主菜单栏//Draw the top main menu bar
        static void DrawMainMenuBar(RenderSystem* renderSys, SceneManager* sceneManager, UserState& state, bool& appRunning);
		
        // 保存项目到 JSON//Save project to JSON
        static void SaveProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath);
		
        // 从 JSON 加载项目//Load project from JSON
        static void LoadProject(SceneManager* sceneManager, RenderSystem* renderSys, const std::string& filepath);


        //================Notification System 提示通知=========================
		// 创建一个提示消息窗口//Create a toast message window
        static void ShowToast(const std::string& message);

		// 绘制提示消息窗口//Draw the toast message window
        static void DrawToast(float dt);


        //================Scene Viewport 场景视口==============================
		//视口拖拽接收器//viewport drag-and-drop receiver
        static void DrawViewportDropTarget(RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj);
		
        static ImVec2 GetSceneViewportSize();//获取视口在 UI 中的实时尺寸//Get the real-time size of the viewport in the UI
		
        static ImVec2 GetSceneViewportPos();//获取视口在 UI 中的实时位置//Get the real-time position of the viewport in the UI
        
		//绘制 3D 渲染画面、图标、Gizmo 坐标轴//Draw the 3D rendered scene, icons, and Gizmo axes
        static void DrawSceneViewport(VkDescriptorSet sceneTexId, RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, UserState& state);


        //================Editor Panels 编辑器面板=============================
		//内容浏览器面板（显示 Assets 资源）//Content Browser panel (showing Assets)
        static void DrawContentBrowser(RenderSystem* renderSys, SceneManager* sceneManager);

		//控制面板（粒子设置、渲染模式切换）//Control panel (particle settings, render mode switching)
        static void DrawControlPanel(UserState& state, RenderSystem* renderSys, SceneManager* sceneManager);

        //light UI灯光调节面板
        static void DrawLightPanel(SceneManager* sceneManager, UserState& state);

        //camera UI相机调节面板
        static void DrawCameraPanel(UserState& state);
		//调试信息面板//Debug info panel
        static void DrawDebugPanel(UserState& state);
		//音频系统面板//Audio system panel
        static void DrawAudioPanel(UserState& state, AudioSystem* audioSystem);

		// 场景层级面板与属性检查器（Inspector）//Scene Hierarchy panel with property inspector (Inspector)
        static void DrawSceneHierarchy(RenderSystem* renderSys, SceneManager* sceneManager, const glm::mat4& view, const glm::mat4& proj, flecs::entity_t& selected_id, UserState& state);
		

        //================Game UI 游戏流程界面=================================
        //Game start menu 游戏开始菜单
        static void DrawMainMenu(RenderSystem* renderSys, bool& appRunning, bool& isGameStarted);
		//Game over menu 游戏结束菜单
        static void DrawGameOver(RenderSystem* renderSys, UserState& state, bool& appRunning);
		//Pause menu 暂停菜单
        static void DrawGamePause(RenderSystem* renderSys, UserState& state, bool& appRunning);


        
    private:
        // 编辑器 UI 的状态缓存当前选中的实体、位移、旋转、缩放的数值
		// State cache for the editor UI: currently selected entity, translation, rotation, and scale values
        inline static flecs::entity_t m_current_inspected_id = 0;
        inline static float m_ui_translation[3] = { 0.0f, 0.0f, 0.0f };
        inline static float m_ui_rotation[3] = { 0.0f, 0.0f, 0.0f };
        inline static float m_ui_scale[3] = { 1.0f, 1.0f, 1.0f };
        inline static flecs::entity_t m_selected_light_id = 0;

        //粒子图标显示
        // 用于存储图标贴图的 Vulkan 资源描述符集合 (Vulkan Descriptor Set)
        static VkDescriptorSet s_ParticleIconTexId;
        

    };

} // namespace engine