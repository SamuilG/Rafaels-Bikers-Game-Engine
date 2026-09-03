#pragma once

#include <glm/mat4x4.hpp>

namespace engine::scene_transform
{
    inline glm::mat4 ComposeWorldTransform(const glm::mat4* parentWorld,
                                           const glm::mat4& localTransform)
    {
        return parentWorld ? (*parentWorld * localTransform) : localTransform;
    }
}
