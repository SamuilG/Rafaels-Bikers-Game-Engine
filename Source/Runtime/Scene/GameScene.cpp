#include "GameScene.hpp"
#include "../UI/EngineUi.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"

namespace engine {

    void GameScene::Log(const std::string& msg) {
        EngineUi::PushLogMessage(msg);
    }

    void GameScene::Toast(const std::string& msg) {
        EngineUi::ShowToast(msg);
    }

    void GameScene::InitBase(RenderSystem* render) {
        m_baseRender = render;
    }

    bool GameScene::AddWidget(const std::string& path) {
        if (m_baseRender) {
            if (auto* ui = m_baseRender->GetRuntimeUiController()) {
                return ui->AddWidgetToViewPort(path);
            }
        }
        return false;
    }

    bool GameScene::RemoveWidget(const std::string& path) {
        if (m_baseRender) {
            if (auto* ui = m_baseRender->GetRuntimeUiController()) {
                return ui->RemoveWidgetFromViewPort(path);
            }
        }
        return false;
    }

} // namespace engine
