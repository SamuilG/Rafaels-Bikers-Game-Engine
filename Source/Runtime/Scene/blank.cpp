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
    }
}

void blank::Update(float /*dt*/) {
    // Generic scene entities are instantiated from a scene document by the
    // application. Engine physics, animation and particles run in their usual
    // frame phases; no demo-specific gameplay is attached to this entry scene.
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
