#pragma once

struct EngineModel;

namespace tinygltf {
struct Model;
}

namespace engine::detail {

void postprocess_skinned_wheels(const tinygltf::Model& gltf, EngineModel& model);

}
