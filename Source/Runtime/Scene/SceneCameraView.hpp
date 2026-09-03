#pragma once

#include <algorithm>
#include <optional>
#include <glm/gtc/matrix_transform.hpp>
#include "RenderSnapshot.hpp"

namespace engine::scene_camera
{
    inline std::optional<CameraView> MakeCameraView(const glm::mat4* worldTransform,
        float verticalFovDegrees, float nearPlane, float farPlane, float aspectRatio)
    {
        if (!worldTransform) return std::nullopt;
        CameraView view{};
        view.worldTransform = *worldTransform;
        view.view = glm::inverse(*worldTransform);
        view.projection = glm::perspectiveRH_ZO(glm::radians(verticalFovDegrees), std::max(aspectRatio, 0.001f), nearPlane, farPlane);
        view.projection[1][1] *= -1.0f;
        view.projView = view.projection * view.view;
        view.worldPosition = glm::vec3((*worldTransform)[3]);
        return view;
    }
}
