#pragma once
#include <cstdint>

namespace engine {
struct ModelResourceStatistics {
    uint32_t assets = 0;
    uint32_t textures = 0;
    uint32_t meshes = 0;
    uint32_t pendingAssets = 0;
    uint64_t residentBytes = 0;
    uint64_t pendingBytes = 0;
    uint64_t uploads = 0;
};
}
