#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

struct PortalShardMaterial {
    float opacity = 1.0f;
    float ior = 1.46f;
    float refraction = 0.075f;
    float dispersion = 0.08f;
};

struct PortalShardEmitter {
    static constexpr int kMaxCount = 256;
    static constexpr float kMaxDuration = 30.0f;
    static constexpr float kMaxDistance = 100.0f;
    static constexpr float kMaxSize = 10.0f;
    static constexpr float kMaxSpeed = 20.0f;

    bool followRider = true;
    int count = 32;
    float duration = 4.0f;
    float emissionDuration = 0.5f;
    float fadeIn = 0.22f;
    float fadeOut = 1.2f;
    glm::vec2 spawnRadius{1.55f, 3.0f};
    glm::vec2 travelDistance{2.55f, 5.2f};
    glm::vec2 spawnHeight{0.55f, 4.2f};
    glm::vec2 riseDistance{0.45f, 1.6f};
    glm::vec2 size{0.34f, 0.82f};
    float expansionSpeed = 1.6f;
    float orbitSpeed = 0.3f;
    glm::vec2 spinSpeed{2.4f, 5.1f};
    PortalShardMaterial material;

    bool IsValid() const {
        const auto in = [](float value, float min, float max) {
            return value >= min && value <= max;
        };
        const auto range = [&](const glm::vec2& values, float min, float max) {
            return in(values.x, min, values.y) && in(values.y, min, max);
        };
        return count >= 1 && count <= kMaxCount && in(duration, 0.1f, kMaxDuration) &&
            in(emissionDuration, 0, kMaxDuration) && in(fadeIn, 0, kMaxDuration) && in(fadeOut, 0, kMaxDuration) &&
            range(spawnRadius, 0, kMaxDistance) && range(travelDistance, 0, kMaxDistance) &&
            range(spawnHeight, -kMaxDistance, kMaxDistance) && range(riseDistance, -kMaxDistance, kMaxDistance) &&
            range(size, 0.01f, kMaxSize) && in(expansionSpeed, 0, kMaxSpeed) &&
            in(orbitSpeed, -kMaxSpeed, kMaxSpeed) && range(spinSpeed, -kMaxSpeed, kMaxSpeed) &&
            in(material.opacity, 0, 1) && in(material.ior, 1, 3) && in(material.refraction, 0, 0.5f) &&
            in(material.dispersion, 0, 1);
    }
};

struct PortalShardVisual {
    glm::mat4 transform;
    PortalShardMaterial material;
};

inline void AppendPortalShardVisuals(const PortalShardEmitter& emitter, float elapsed,
    const glm::mat4& emitterToWorld, std::vector<PortalShardVisual>& shards) {
    if (elapsed < 0.0f || elapsed >= emitter.duration || emitter.material.opacity <= 0.0f) return;
    const auto step = [&](int preferred) {
        while (std::gcd(preferred, emitter.count) != 1) ++preferred;
        return preferred;
    };
    const int delayStep = step(7);
    const int variationStep = step(11);
    const int heightStep = step(5);
    const float lastIndex = float(std::max(emitter.count - 1, 1));
    const float fadeOut = emitter.fadeOut > 0.0f
        ? 1.0f - glm::smoothstep(std::max(0.0f, emitter.duration - emitter.fadeOut), emitter.duration, elapsed)
        : 1.0f;
    const auto sample = [](const glm::vec2& range, float factor) {
        return glm::mix(range.x, range.y, factor);
    };
    shards.reserve(shards.size() + emitter.count);
    for (int i = 0; i < emitter.count; ++i) {
        const float age = elapsed - emitter.emissionDuration * float((i * delayStep) % emitter.count) / lastIndex;
        if (age <= 0.0f) continue;

        const float variation = float((i * variationStep) % emitter.count) / lastIndex;
        const float angle = float(i) * glm::two_pi<float>() / float(emitter.count) + emitter.orbitSpeed * age;
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        const float expansion = 1.0f - std::exp(-emitter.expansionSpeed * age);
        const float radius = sample(emitter.spawnRadius, variation) + expansion * sample(emitter.travelDistance, variation);
        const float height = sample(emitter.spawnHeight, float((i * heightStep) % emitter.count) / lastIndex)
            + expansion * sample(emitter.riseDistance, variation);
        const float fadeIn = emitter.fadeIn > 0.0f ? glm::smoothstep(0.0f, emitter.fadeIn, age) : 1.0f;
        const float fade = fadeIn * fadeOut;
        const float size = sample(emitter.size, variation) * glm::mix(0.5f, 1.0f, fade);
        auto transform = glm::translate(emitterToWorld, radial * radius + glm::vec3(0.0f, height, 0.0f));
        transform = glm::rotate(transform, 1.8f * angle + age * sample(emitter.spinSpeed, variation),
            radial + glm::vec3(0.0f, 0.7f, 0.0f));
        auto material = emitter.material;
        material.opacity *= fade;
        shards.push_back({glm::scale(transform, glm::vec3(size)), material});
    }
}

} // namespace engine
