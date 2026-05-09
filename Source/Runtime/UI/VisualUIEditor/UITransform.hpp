#pragma once

#include "UICommon.hpp"

namespace engine {

    // 计算成父矩形下的最终 UI 矩形。
    struct UITransform {
        glm::vec2 position = glm::vec2(0.0f);
        glm::vec2 size = glm::vec2(100.0f, 100.0f);
        glm::vec2 anchorMin = glm::vec2(0.0f);
        glm::vec2 anchorMax = glm::vec2(0.0f);
        glm::vec2 pivot = glm::vec2(0.5f, 0.5f);
        float rotation = 0.0f;
        glm::vec2 scale = glm::vec2(1.0f);

        // 基于父矩形计算当前元素的最终矩形。
        UIRect ComputeRect(const UIRect& parentRect) const;
    };

} // namespace engine
