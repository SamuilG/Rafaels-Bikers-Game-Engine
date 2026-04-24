#pragma once

#include <glm/glm.hpp>
#include <flecs.h>

namespace engine {

    class SceneManager; 

    class MousePicker {
    public:
        static flecs::entity PickEntity(
            float mouseX,
            float mouseY,
            float screenWidth,
            float screenHeight,
            const glm::mat4& camera2world,
            const glm::mat4& projection,
            SceneManager* sceneManager
        );
    };

} // namespace engine