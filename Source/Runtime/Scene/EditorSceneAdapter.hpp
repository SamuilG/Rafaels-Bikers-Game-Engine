#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <flecs.h>
#include "../UI/MousePicker.hpp"

namespace engine {

class SceneManager;

// Editor-only scene operations. Runtime gameplay and SceneManager remain the
// owners of the world; this adapter owns only editor selection and picking.
class EditorSceneAdapter final {
public:
    explicit EditorSceneAdapter(SceneManager* scene = nullptr) : mScene(scene) {}

    void SetScene(SceneManager* scene) { mScene = scene; }
    SceneManager* Scene() const { return mScene; }

    flecs::entity Pick(float mouseX, float mouseY, float viewportWidth,
        float viewportHeight, const glm::mat4& camera2world,
        const glm::mat4& projection) {
        if (!mScene) {
            return flecs::entity::null();
        }
        return MousePicker::PickEntity(mouseX, mouseY, viewportWidth,
            viewportHeight, camera2world, projection, mScene);
    }

    flecs::entity_t& Selection() { return mSelectedEntityId; }
    flecs::entity_t Selection() const { return mSelectedEntityId; }
    void ClearSelection() { mSelectedEntityId = 0; }

private:
    SceneManager* mScene = nullptr;
    flecs::entity_t mSelectedEntityId = 0;
};

} // namespace engine
