#include "GameFlowUi.hpp"

#include "../../Runtime/Renderer/RenderSystem.hpp"
#include "../../Runtime/UI/EngineUi.hpp"
#include "../../Runtime/UI/SwitchLanguage.hpp"
#include "../../Runtime/UserState/UserState.hpp"

#include <imgui.h>

namespace engine {
    void GameFlowUi::DrawMainMenu(RenderSystem* renderSys, bool& appRunning, bool& isGameStarted) {
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        std::string bgName = cfg::ParticleTextures[0];
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

            if (ImGui::Begin("BackgroundWindow", nullptr, bgFlags)) {
                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
                    ImVec4(0, 0, 0, 0));
            }
            ImGui::End();

            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        float windowWidth = screenWidth * 0.20f;
        float windowHeight = screenHeight * 0.30f;
        if (windowWidth < 280.0f) windowWidth = 280.0f;
        if (windowHeight < 240.0f) windowHeight = 240.0f;

        float posX = (screenWidth - windowWidth) * 0.5f;
        float posY = (screenHeight - windowHeight) * 0.5f;

        ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.7f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

        ImGuiWindowFlags buttonFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoScrollbar;

        if (ImGui::Begin("MainMenuButtons", nullptr, buttonFlags)) {
            float availWidth = ImGui::GetContentRegionAvail().x;
            float textWidth = ImGui::CalcTextSize(_SL("[ MAIN MENU ]")).x;

            ImGui::SetCursorPosX((availWidth - textWidth) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), _SL("[ MAIN MENU ]"));

            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(_SL("Start Game"), ImVec2(-1, 60))) {
                isGameStarted = true;
                EngineUi::LogPrint("Game Started!\n");
            }

            ImGui::Spacing();
            if (ImGui::Button(_SL("Setting"), ImVec2(-1, 60))) {
                EngineUi::LogPrint("Setting...(coming soon)\n");
            }

            ImGui::Spacing();
            if (ImGui::Button(_SL("Exit Game"), ImVec2(-1, 60))) {
                appRunning = false;
                EngineUi::LogPrint("Exiting Game...\n");
            }

            ImGui::Spacing();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void GameFlowUi::DrawGamePause(RenderSystem* renderSys, UserState& state, bool& appRunning) {
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        std::string bgName = "Assets/Textures/GamePause_Bg.png";
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("PauseBackground", nullptr, bgFlags)) {
                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    ImVec4(0, 0, 0, 0));
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        float winW = screenWidth * 0.25f;
        float winH = screenHeight * 0.45f;
        if (winW < 300.0f) winW = 300.0f;

        ImGui::SetNextWindowPos(ImVec2((screenWidth - winW) * 0.5f, (screenHeight - winH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.0f, 0.0f, 0.8f));

        if (ImGui::Begin("GamePauseMenu", nullptr, ImGuiWindowFlags_NoDecoration)) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), _SL("Pause"));
            ImGui::Separator();
            ImGui::Spacing();

            float btnW = ImGui::GetContentRegionAvail().x;

            if (ImGui::Button(_SL("Restart Game"), ImVec2(btnW, 60))) {
                state.gameplay.isGameOver = false;
                state.gameplay.isGamePause = false;
                state.gameplay.isGameStarted = true;
                EngineUi::LogPrint("Restarting Level...\n");
            }

            ImGui::Spacing();

            if (ImGui::Button(_SL("Back to Main Menu"), ImVec2(btnW, 60))) {
                state.gameplay.isGameOver = false;
                state.gameplay.isGamePause = false;
                state.gameplay.isGameStarted = false;
            }

            ImGui::Spacing();

            if (ImGui::Button(_SL("Exit Game"), ImVec2(btnW, 60))) {
                appRunning = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    void GameFlowUi::DrawGameOver(RenderSystem* renderSys, UserState& state, bool& appRunning) {
        ImGuiIO& io = ImGui::GetIO();
        float screenWidth = io.DisplaySize.x;
        float screenHeight = io.DisplaySize.y;

        std::string bgName = "Assets/Textures/GameOver_Bg.png";
        VkDescriptorSet imguiBgTex = renderSys->GetImGuiTextureDescriptor(bgName);

        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;

        if (imguiBgTex) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

            if (ImGui::Begin("GameOverBackground", nullptr, bgFlags)) {
                ImGui::Image(
                    (ImTextureID)imguiBgTex,
                    ImVec2(screenWidth, screenHeight),
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    ImVec4(0, 0, 0, 0));
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        float winW = screenWidth * 0.25f;
        float winH = screenHeight * 0.45f;
        if (winW < 300.0f) winW = 300.0f;

        ImGui::SetNextWindowPos(ImVec2((screenWidth - winW) * 0.5f, (screenHeight - winH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.0f, 0.0f, 0.8f));
        if (ImGui::Begin("GameOverMenu", nullptr, ImGuiWindowFlags_NoDecoration)) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), _SL("   GAME OVER"));
            ImGui::Separator();
            ImGui::Spacing();

            float btnW = ImGui::GetContentRegionAvail().x;

            if (ImGui::Button(_SL("Restart Game"), ImVec2(btnW, 60))) {
                state.gameplay.isGameOver = false;
                state.gameplay.isGamePause = false;
                state.gameplay.isGameStarted = true;
                EngineUi::LogPrint("Restarting Level...\n");
            }

            ImGui::Spacing();

            if (ImGui::Button(_SL("Back to Main Menu"), ImVec2(btnW, 60))) {
                state.gameplay.isGameOver = false;
                state.gameplay.isGamePause = false;
                state.gameplay.isGameStarted = false;
            }

            ImGui::Spacing();

            if (ImGui::Button(_SL("Exit Game"), ImVec2(btnW, 60))) {
                appRunning = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
} // namespace engine
