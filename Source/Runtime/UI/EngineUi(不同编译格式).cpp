#include "EngineUi.hpp"
#include <imgui.h>
#include <cstdio>
#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp" 

namespace engine {

    void EngineUi::DrawControlPanel(UserState& state, RenderSystem* renderSys, SceneManager* sceneManager)
    {
        // 创建主面板
        if (ImGui::Begin("Engine Control Panel"))
        {
            // --- 性能区 ---
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ Performance ]");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

            ImGui::Separator();

            // --- 渲染设置区 ---
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ Render Settings ]");
            const char* modes[] = { "默认", "Mipmaps", "Depth", "Derivatives", "Overdraw", "Overshading" };

            // 直接修改传入的 state 引用
            ImGui::Combo("View Mode", &state.renderMode, modes, IM_ARRAYSIZE(modes));
            ImGui::Checkbox("Enable Mosaic Post-Process", &state.mosaicEnabled);

            ImGui::Separator();

            // --- 场景与物理控制区 ---
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ Scene & Physics ]");

            // 静态变量，用来保存滑块的值
            static float spawnHeight = 5.0f;
            ImGui::SliderFloat("Spawn Height", &spawnHeight, 1.0f, 20.0f);

            // 一键生成小球按钮
            if (ImGui::Button("Spawn Sphere", ImVec2(120, 30)))
            {
                std::printf("UI triggered: Spawning sphere at Y = %.1f\n", spawnHeight);

                // 我们确保 sceneManager 存在后再调用生成逻辑
                if (sceneManager && renderSys) {
                    // 在下一步中，我们会把 Application.cpp 里的生成小球代码搬到这里！
                }
                else {
                    std::printf("Warning: SceneManager or RenderSystem is null!\n");
                }
            }
        }
        ImGui::End();
    }

} // namespace engine