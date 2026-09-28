#pragma once

#include "../UserState/GameplayState.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Particle/ParticleSystem.hpp"
#include <memory>
#include <vector>

namespace engine {

// The demo owns entity selection and particle attachments. Rendering receives
// an effect snapshot and the already resolved emitter positions.
inline void UpdateBikersPresentation(float dt, GameplayState& state,
    SceneManager& scene, std::vector<std::unique_ptr<ParticleSystem>>& particles) {
    const auto bike = scene.find_entity("Bike_0");
    uint32_t bodyId = UINT32_MAX;
    uint64_t entityId = 0;
    if (bike.is_valid()) {
        entityId = bike.id();
        if (bike.has<CompoundParent>()) bodyId = bike.get<CompoundParent>().bodyID;
        else if (bike.has<PhysicsBody>()) bodyId = bike.get<PhysicsBody>().bodyID;
    }
    state.hud.Update(dt, state.player.State(), state.frame);
    state.vehiclePresentation.Update(dt, state.player.State(), state.level,
        state.frame, entityId, bodyId);

    const auto bat = scene.find_entity("BaseballBat.nails_0");
    for (const auto& particle : particles) {
        if (!particle) continue;
        auto& config = particle->config;
        config.scenePositionControlled = bat.is_valid() && bat.has<LocalTransform>() &&
            particle->getEmitterShape() == EmitterShape::Sphere && !config.triggerControlled;
        if (config.scenePositionControlled) {
            // Preserve the existing demo attachment position convention.
            config.emitterPos = glm::vec3(bat.get<LocalTransform>().matrix[3]);
        }
    }
}

} // namespace engine
