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

        // 假设你的阴影贴图分辨率是 4096，根据实际情况修改
        const float SHADOW_MAP_RESOLUTION = 4096.0f;

        // =========================================================
        // 【修正位置】：把手工分割放到最前面！完全替代原有的公式
        // =========================================================
        cascadeSplits[0] = 15.0f;                // 第 0 级：0 ~ 15 米 (超高清)
        cascadeSplits[1] = 220.0f;                // 第 1 级：15 ~ 80 米 (中距)
        cascadeSplits[2] = std::min(farP, 2000.0f); // 第 2 级：80 ~ 远景 (兜底)

        // 立刻存入 data 准备传给 Shader
        data.cascadeSplits = glm::vec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], 0);

        float lastSplit = nearP;
        glm::vec3 normalizedLightDir = glm::normalize(lightDir);

        // 动态获取 Up 向量，防止光源直射导致 lookAt 矩阵崩溃
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(normalizedLightDir, up)) > 0.999f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // =========================================================
        // 开始用正确的手工分割点计算 lightVP 矩阵
        // =========================================================
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

            // 计算包围球半径
            float radius = 0.0f;
            for (int j = 0; j < 8; j++) {
                radius = glm::max(radius, glm::length(glm::vec3(frustumCorners[j]) - center));
            }
            // 为了进一步稳定，将半径向上取整为某个步长
            radius = std::ceil(radius * 16.0f) / 16.0f;

            // 消除阴影生硬边缘与闪烁 (Texel Snapping 像素对齐)
            glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -normalizedLightDir, up);
            glm::vec3 centerLightSpace = glm::vec3(lightView * glm::vec4(center, 1.0f));

            // 计算每个像素代表的世界单位长度
            float unitsPerTexel = (radius * 2.0f) / SHADOW_MAP_RESOLUTION;
            // 将中心的 XY 坐标严格对齐到像素网格
            centerLightSpace.x = std::floor(centerLightSpace.x / unitsPerTexel) * unitsPerTexel;
            centerLightSpace.y = std::floor(centerLightSpace.y / unitsPerTexel) * unitsPerTexel;

            // 将对齐后的中心重新转换回世界空间
            center = glm::vec3(glm::inverse(lightView) * glm::vec4(centerLightSpace, 1.0f));

            // 修复远距离裁剪失效 (Dynamic Z-Bounds)
            float zMult = 10.0f;
            glm::mat4 lightOrtho = glm::orthoRH_ZO(-radius, radius, -radius, radius, -radius * zMult, radius * zMult);

            // 使用对齐后的 center，将灯光相机就放在中心位置
            lightView = glm::lookAt(center, center - normalizedLightDir, up);

            // Vulkan Y轴适配
            lightOrtho[1][1] *= -1.f;

            data.lightVP[i] = lightOrtho * lightView;
            lastSplit = currentSplit;
        }

        // 第 4 层 (lightVP[3]) 初始化为单位矩阵，给车头灯用
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