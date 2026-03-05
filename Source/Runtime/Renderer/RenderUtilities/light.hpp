#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "setup.hpp"


namespace engine {

	//compute data for cascaded shadow mapping HERE£¡£¡
    struct ShadowData {
        glm::vec4 cascadeSplits;
        glm::mat4 lightVP[kCascadeCount];
    };

    
    ShadowData compute_csm_matrices(
        const glm::vec3& lightDir,
        const glm::mat4& cameraView,
        float fov, float aspect,
        float nearP, float farP
    );

} // namespace engine