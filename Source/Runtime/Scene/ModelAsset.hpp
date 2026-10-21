#pragma once

#include "model_loader/engine_model.hpp"
#include <memory>
#include <string>

namespace engine {

// Immutable geometry/materials are shared by instances and editor/play worlds.
// Texture pixels are discarded after upload; dimensions remain for diagnostics.
struct ModelAsset {
    std::string key;
    EngineModel model;
    uint32_t firstTexture = 0;
    uint32_t firstMaterial = 0;
    uint32_t firstMesh = 0;
    uint64_t gpuBytes = 0;
};

// Flecs owns this reference with the mesh entity, including direct destruction
// and world shutdown. The renderer retires GPU objects after the last lease.
struct ModelAssetReference {
    std::shared_ptr<const ModelAsset> asset;
};

} // namespace engine
