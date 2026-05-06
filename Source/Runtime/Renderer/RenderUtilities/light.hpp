#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace engine {
    constexpr uint32_t count = 4;

    enum class LightType : uint32_t {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    // ECS 组件
    struct LightComponent {
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        LightType type = LightType::Point;
        float range = 10.0f; // 点光源影响范围

        // 【新增】：高光强度系数。1.0 为正常，0.0 为纯漫反射（不产生刺眼反光）
        float specularMultiplier = 1.0f;

        // === 聚光灯专属参数 ===
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f); // 默认朝向 -Z (车头正前方)
        float innerCutOff = 12.5f; // 内锥角 (度数)，光线最亮的区域
        float outerCutOff = 17.5f; // 外锥角 (度数)，光线开始变暗直到消失的边界
    };

    const uint32_t MAX_LIGHTS = 16;

    // 对齐 Shader 的结构体 (必须 16 字节对齐)
    // 依然是 64 字节 (4个vec4)，完美适配 Vulkan
    struct alignas(16) GpuLight {
        glm::vec4 position;  // xyz: 位置(Point/Spot)或方向(Dir), w: 类型(0:Dir, 1:Point, 2:Spot)
        glm::vec4 color;     // rgb: 颜色, a: 强度
        glm::vec4 direction; // xyz: 聚光灯朝向, w: range (影响范围)
        glm::vec4 params;    // 【修改】x: cos(inner), y: cos(outer), z: specularMultiplier, w: pad
    };

    //compute data for cascaded shadow mapping HERE！！
    struct ShadowData {
        glm::vec4 cascadeSplits;
        glm::mat4 lightVP[count];
    };

    // (已清理重复声明)
    ShadowData compute_csm_matrices(
        const glm::vec3& lightDir,
        const glm::mat4& cameraView,
        float fov, float aspect,
        float nearP, float farP
    );

    // --- 专门计算聚光灯阴影矩阵的函数 ---
    glm::mat4 compute_spotlight_matrix(
        const glm::vec3& pos,
        const glm::vec3& dir,
        float outerCutOff,
        float range
    );
} // namespace engine