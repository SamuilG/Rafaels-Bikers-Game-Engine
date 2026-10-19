#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <glm/glm.hpp>

namespace engine {

// Persistent authoring IDs are independent of Flecs' recycled runtime IDs.
inline std::string NewSceneIdentity() {
    static const std::uint64_t session = [] {
        std::random_device random;
        return (static_cast<std::uint64_t>(random()) << 32) ^ random() ^
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }();
    static std::atomic<std::uint64_t> sequence{0};
    std::ostringstream result;
    result << std::hex << session << '-' << ++sequence;
    return result.str();
}

struct SceneEntityIdentity { std::string value; };

// `part` is the index in EngineModel::scenes, not a GPU mesh index. Two
// instances of the same file and two nodes using the same mesh remain distinct.
struct ModelPartSource {
    std::string instance;
    std::uint32_t part = 0;
    std::uint32_t meshIndex = 0;
    std::uint32_t materialIndex = 0;
};

struct ModelImportSource {
    std::string id;
    std::string asset;
    int physicsType = 0;
    float mass = 1.0f;
    glm::mat4 transform{1.0f};
};

} // namespace engine
