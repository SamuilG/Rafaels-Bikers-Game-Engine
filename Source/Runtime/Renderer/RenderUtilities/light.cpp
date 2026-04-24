#include "light.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace engine {

    ShadowData compute_csm_matrices(const glm::vec3& lightDir, const glm::mat4& cameraView, float fov, float aspect, float nearP, float farP)
    {
        ShadowData data;
        float count = 3;
        float cascadeSplits[3];
        float lambda = 0.75f;

        // --- 原有分割逻辑 ---
        for (uint32_t i = 0; i < count; i++) {
            float p = (i + 1) / static_cast<float>(count);
            float logSplit = nearP * std::pow(farP / nearP, p);
            float linSplit = nearP + (farP - nearP) * p;
            cascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * linSplit;
        }
        data.cascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], 0);

        // --- 原有 LightVP 计算逻辑 ---
        float lastSplit = nearP;
        //glm::normalize(lightDir);
        glm::vec3 normalizedLightDir = glm::normalize(lightDir);
        for (uint32_t i = 0; i < count; i++) {
            float currentSplit = cascadeSplits[i];

            // 构建该段视锥体的投影矩阵
            glm::mat4 segProj = glm::perspectiveRH_ZO(fov, aspect, lastSplit, currentSplit);
            glm::mat4 invVP = glm::inverse(segProj * cameraView);
           
            // 计算 8 个顶点和中心点
            glm::vec4 frustumCorners[8] = {
                {-1, -1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}, {-1, 1, 0, 1},
                {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
            };

            glm::vec3 center(0.0f);
            for (int j = 0; j < 8; j++) {
                frustumCorners[j] = invVP * frustumCorners[j];
                frustumCorners[j] /= frustumCorners[j].w;
                center += glm::vec3(frustumCorners[j]);
            }
            center /= 8.0f;
            
            // 计算半径（稳定化）
            float radius = 0.0f;
			float stability = 32.0f; 
            for (int j = 0; j < 8; j++) {
                radius = glm::max(radius, glm::length(glm::vec3(frustumCorners[j]) - center));
            }
            radius = std::ceil(radius * stability) / stability;

            // 构建 View & Ortho
          
            glm::mat4 lightOrtho = glm::orthoRH_ZO(-radius, radius, -radius, radius, -1000.0f, 1000.0f);
            glm::mat4 lightView = glm::lookAt(center + normalizedLightDir * radius * 2.0f, center, glm::vec3(0, 1, 0));
            lightOrtho[1][1] *= -1.f; // Vulkan 适配

            data.lightVP[i] = lightOrtho * lightView;
            lastSplit = currentSplit;
        }
        // 第 4 层 (lightVP[3]) 初始化为单位矩阵，等会儿会在 RenderSystem 里被车头灯覆盖
        data.lightVP[3] = glm::mat4(1.0f);

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