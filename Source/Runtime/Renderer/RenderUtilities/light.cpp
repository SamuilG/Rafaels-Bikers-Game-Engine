#include "light.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace engine {

    ShadowData compute_csm_matrices(const glm::vec3& lightDir, const glm::mat4& cameraView, float fov, float aspect, float nearP, float farP)
    {
        ShadowData data;
        uint32_t count = 3;
        float cascadeSplits[3];

        const float SHADOW_MAP_RESOLUTION = 4096.0f;

        // 【坚守甜点距离】：不要轻易改动，这是保证包围球清晰度的核心！
        cascadeSplits[0] = 15.0f;
        cascadeSplits[1] = 70.0f;
        cascadeSplits[2] = 800.0f;

        data.cascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], 0);

        float lastSplit = nearP;
        glm::vec3 normalizedLightDir = glm::normalize(lightDir);

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(normalizedLightDir, up)) > 0.999f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        for (uint32_t i = 0; i < count; i++) {
            float currentSplit = cascadeSplits[i];

            glm::mat4 segProj = glm::perspectiveRH_ZO(fov, aspect, lastSplit, currentSplit);
            glm::mat4 invVP = glm::inverse(segProj * cameraView);

            glm::vec4 frustumCorners[8] = {
                {-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
                {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
            };

            // 1. 求出视锥体的绝对中心点
            glm::vec3 center(0.0f);
            for (int j = 0; j < 8; j++) {
                frustumCorners[j] = invVP * frustumCorners[j];
                frustumCorners[j] /= frustumCorners[j].w;
                center += glm::vec3(frustumCorners[j]);
            }
            center /= 8.0f;

            // =========================================================
            // 【终极修复 1】：锁定恒定半径 (Constant Bounding Sphere)
            // 彻底杀死“阴影呼吸效应”！无论相机怎么转，正交矩阵大小死死锁住！
            // =========================================================
            float radius = 0.0f;
            for (int j = 0; j < 8; j++) {
                radius = glm::max(radius, glm::length(glm::vec3(frustumCorners[j]) - center));
            }
            // 向上取整，抹平浮点数微小波动
            radius = std::ceil(radius);

            // 2. 初始灯光视图
            glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -normalizedLightDir, up);

            // =========================================================
            // 【终极修复 2】：Texel Snapping (像素网格对齐)
            // 锁定半径后，还要防止相机位移导致的“阴影边缘微小蠕动”
            // =========================================================
            glm::vec3 centerLightSpace = glm::vec3(lightView * glm::vec4(center, 1.0f));
            float unitsPerTexel = (radius * 2.0f) / SHADOW_MAP_RESOLUTION;

            centerLightSpace.x = std::floor(centerLightSpace.x / unitsPerTexel) * unitsPerTexel;
            centerLightSpace.y = std::floor(centerLightSpace.y / unitsPerTexel) * unitsPerTexel;

            // 逆推回世界坐标的中心
            center = glm::vec3(glm::inverse(lightView) * glm::vec4(centerLightSpace, 1.0f));

            // 3. 根据对齐后的中心生成最终的 Light View
            lightView = glm::lookAt(center, center - normalizedLightDir, up);

            // =========================================================
            // 【终极修复 3】：暴力向后推远 Near Plane
            // 完全保留防止大楼漏光、截断阴影的功能
            // =========================================================
            float zNear = -3000.0f;
            float zFar = radius + 500.0f;

            // 注意这里的 x, y 边界全部换成了锁死的 radius，不再用动态的 minX/maxX
            glm::mat4 lightOrtho = glm::orthoRH_ZO(-radius, radius, -radius, radius, zNear, zFar);

            // Vulkan Y轴适配
            lightOrtho[1][1] *= -1.f;

            data.lightVP[i] = lightOrtho * lightView;
            lastSplit = currentSplit;
        }

        data.lightVP[3] = glm::mat4(1.0f); // 预留给车头灯
        return data;
    }
    // --- 【新增】计算车灯透视投影矩阵的实现 ---
    glm::mat4 compute_spotlight_matrix(const glm::vec3& pos, const glm::vec3& dir, float outerCutOff, float range)
    {
        // 1. 投影矩阵 (透视投影)。视角就是外锥角的 2 倍。
        float fovY = glm::radians(outerCutOff * 2.0f);
        glm::mat4 proj = glm::perspectiveRH_ZO(fovY, 1.0f, 0.1f, range);
        proj[1][1] *= -1.0f; // Vulkan 的 Y 轴翻转

        // 2. 视图矩阵 (车灯在 pos，看向 pos + dir)
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        // 防止车灯刚好垂直朝上或朝下导致 lookAt 崩溃的经典保护
        if (std::abs(glm::dot(dir, up)) > 0.999f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        glm::mat4 view = glm::lookAt(pos, pos + dir, up);

        // 返回最终的 VP 矩阵
        return proj * view;
    }

} // namespace engineamespace engine