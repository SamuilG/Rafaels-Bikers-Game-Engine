#pragma once
#include <glm/glm.hpp>
#include <cstdint>



namespace engine {
    constexpr uint32_t kCascadeCount = 4;

    enum class LightType : uint32_t {
        Directional = 0,
        Point = 1,
        
    };

    // ECS 组件
    struct LightComponent {
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        LightType type = LightType::Point;
        float range = 10.0f; // 点光源影响范围
    };
    const uint32_t MAX_LIGHTS = 16; // Shader 支持的最大光源数



    // 对齐 Shader 的结构体 (必须 16 字节对齐)
    struct GpuLight {
        glm::vec4 position;
        glm::vec4 color;
        glm::vec4 params;
    };
	//compute data for cascaded shadow mapping HERE！！
    struct ShadowData {
        glm::vec4 cascadeSplits;
        glm::mat4 lightVP[kCascadeCount];
		//glm::mat4 lightVP[4]; // CSM 数组
    };
   
    
    ShadowData compute_csm_matrices(
        const glm::vec3& lightDir,
        const glm::mat4& cameraView,
        float fov, float aspect,
        float nearP, float farP
    );

} // namespace engine