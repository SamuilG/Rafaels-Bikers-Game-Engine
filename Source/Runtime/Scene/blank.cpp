#include "blank.hpp"

#include "../Renderer/RenderSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Physics/PhysicsSystem.hpp"
#include "../Input/InputSystem.hpp"
#include "../Event/EventSystem.hpp"
#include "../Animation/AnimationSystem.hpp"
#include "../AudioSystem/AudioSystem.hpp"
#include "../UserState/GameplayState.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

void blank::Init(RenderSystem* render, SceneManager* scene, PhysicsSystem* physics,
    InputSystem* input, EventSystem* eventSys, GameplayState* state,
    AnimationSystem* anima, AudioSystem* audio) {
    InitBase(render);
    m_render = render;
    if (m_render) m_render->SetGameContentUiEnabled(false);
    m_scene = scene;
    m_physics = physics;
    m_input = input;
    m_event = eventSys;
    m_state = state;
    m_animation = anima;
    m_audio = audio;

    if (m_state) {
        m_state->player.SetControlEnabled(true);
        m_state->camera.ResetForNewRun();
        m_state->camera.RequestFree();
        m_state->camera.SetFov(75.0f);
        m_state->camera.SetFreeTransform(glm::inverse(glm::lookAt(
            glm::vec3(0.0f, 1.8f, 5.0f),
            glm::vec3(0.0f, 1.8f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f))));
    }
}

void blank::Update(float /*dt*/) {
    // The blank scene deliberately has no game entities or simulation. Input
    // and camera presentation remain owned by the engine renderer.
}

void blank::RefreshPlayerMotion(float /*dt*/) {
    // No vehicle exists in the blank scene.
}

void blank::Shutdown() {
    if (m_state) {
        m_state->camera.CancelPortal();
        m_state->camera.EndCinematic();
        m_state->camera.RequestFree();
        m_state->player.SetControlEnabled(true);
    }
    m_render = nullptr;
    m_scene = nullptr;
    m_physics = nullptr;
    m_input = nullptr;
    m_event = nullptr;
    m_state = nullptr;
    m_animation = nullptr;
    m_audio = nullptr;
}

} // namespace engine
