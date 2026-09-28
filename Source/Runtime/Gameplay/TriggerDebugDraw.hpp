#pragma once

#include "../Trigger/trigger.hpp"
#include "../Debug/DebugRenderer.hpp"

namespace engine {
    // Game-owned visualization consumes a read-only geometry snapshot; it cannot
    // evaluate triggers or invoke gameplay callbacks.
    inline void DrawTriggerVolumes(const std::vector<TriggerVolume>& volumes, DebugRenderer& renderer) {
        for (const auto& volume : volumes) {
            if (!volume.isVisible) continue;
            switch (volume.shape) {
            case TriggerShape::Box:
                renderer.DrawBox(volume.center, volume.extents, volume.color, volume.transform);
                break;
            case TriggerShape::Sphere:
                renderer.DrawSphere(volume.center, volume.radius, volume.color, volume.segments);
                break;
            case TriggerShape::Capsule:
                renderer.DrawCapsule(volume.center, volume.radius, volume.halfHeight, volume.color, volume.segments);
                break;
            }
        }
    }
}
