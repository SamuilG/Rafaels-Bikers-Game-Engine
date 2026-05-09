#include "UITransform.hpp"

#include <algorithm>

namespace engine {

    UIRect UITransform::ComputeRect(const UIRect& parentRect) const {
        // 先把锚点映射到父矩形，再叠加本地偏移、尺寸和缩放，
        // 最后依据 pivot 反推出左上角位置。
        const glm::vec2 anchorStart = parentRect.position + parentRect.size * anchorMin;
        const glm::vec2 anchorEnd = parentRect.position + parentRect.size * anchorMax;
        const glm::vec2 anchorSpan = anchorEnd - anchorStart;
        const glm::vec2 finalSize = glm::max(glm::vec2(0.0f), (anchorSpan + size) * scale);
        const glm::vec2 finalPosition = anchorStart + position - finalSize * pivot;

        return UIRect{
            finalPosition,
            finalSize,
            rotation
        };
    }

} // namespace engine
