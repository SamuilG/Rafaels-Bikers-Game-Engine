#include "MousePicker.hpp"
#include "../Scene/SceneManager.hpp"

namespace engine {

    flecs::entity MousePicker::PickEntity(
        float mouseX, float mouseY,
        float screenWidth, float screenHeight,
        const glm::mat4& camera2world,
        const glm::mat4& projection,
        SceneManager* sceneManager)
    {
        if (!sceneManager) return flecs::entity::null();

        float ndcX = (2.0f * mouseX) / screenWidth - 1.0f;
        float ndcY = 1.0f - (2.0f * mouseY) / screenHeight;

        // NDC -> View Space
        glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 rayEye = glm::inverse(projection) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f); 

        glm::vec3 rayDirWorld = glm::normalize(glm::vec3(camera2world * rayEye));
        glm::vec3 rayOriginWorld = glm::vec3(camera2world[3]); // 相机的世界坐标就是射线的起点

        //SceneManager 的 Jolt 物理射线检测接口
        return sceneManager->raycast_entity(rayOriginWorld, rayDirWorld);
    }

} // namespace engine