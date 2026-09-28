#include "GameScene.hpp"
#include "../Gameplay/TriggerDebugDraw.hpp"
#include "../UI/EngineUi.hpp"
#include "../Renderer/RenderSystem.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"

namespace engine {

    GameScene::~GameScene() {
        DetachRenderCallbacks();
    }

    void GameScene::DetachRenderCallbacks() {
        if (!m_baseRender) return;
        m_baseRender->SetSceneDebugDraw({});
        m_baseRender->SetParticleGroupRemovedCallback({});
        m_baseRender->SetParticleGroupTriggerQuery({});
        m_baseRender->SetParticleGroupOwnershipQuery({});
        m_baseRender = nullptr;
    }

    void GameScene::Log(const std::string& msg) {
        EngineUi::PushLogMessage(msg);
    }

    void GameScene::Toast(const std::string& msg) {
        EngineUi::ShowToast(msg);
    }

    void GameScene::InitBase(RenderSystem* render) {
        DetachRenderCallbacks();
        m_baseRender = render;
        if (!m_baseRender) return;
        m_baseRender->SetSceneDebugDraw([this](DebugRenderer& debugRenderer) {
            DrawTriggerVolumes(mTriggers.Volumes(), debugRenderer);
        });
        m_baseRender->SetParticleGroupRemovedCallback([this](size_t particleIndex) {
            mTriggers.OnParticleGroupRemoved(particleIndex);
            OnParticleGroupRemoved(particleIndex);
        });
        m_baseRender->SetParticleGroupTriggerQuery([this](size_t particleIndex) {
            return mTriggers.HasParticleBinding(particleIndex);
        });
        m_baseRender->SetParticleGroupOwnershipQuery([this](size_t particleIndex) {
            return IsParticleGroupSceneControlled(particleIndex);
        });
    }

    void GameScene::UpdateTriggers(const glm::vec3& probePosition) {
        mTriggers.Update(probePosition);
        if (!m_baseRender) return;
        auto& particles = m_baseRender->GetParticles();
        for (auto& particle : particles) {
            if (particle) particle->config.triggerControlled = false;
        }
        for (const auto& state : mTriggers.ParticleStates()) {
            if (state.particleIndex >= particles.size() || !particles[state.particleIndex]) continue;
            auto& config = particles[state.particleIndex]->config;
            config.triggerControlled = true;
            config.isVisible = state.active;
        }
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
