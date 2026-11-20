#pragma once

#include "../../Scene/RenderBatch.hpp"
#include "../../Scene/model_loader/engine_model.hpp"
#include "SceneEffects.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace engine {

struct PortalGroundMaterialSample {
    uint32_t materialIndex = UINT32_MAX;
    float height = std::numeric_limits<float>::lowest();
    glm::vec2 uv{0.0f};
    glm::vec2 uvPerX{0.0f};
    glm::vec2 uvPerZ{0.0f};
};

struct PortalGroundSurface {
    static constexpr int kResolution = 128;
    static constexpr float kMissing = -1000.0f;
    std::array<float, kResolution * kResolution> heights{};
    std::vector<PortalGroundMaterialSample> materials;
    float spacing = 0.0f;

    bool Build(const PortalWaveVisual& wave, std::span<const RenderBatch> batches,
        const EngineModel& model, uint32_t riderBody, std::span<const glm::vec2> materialPoints = {}) {
        heights.fill(kMissing);
        materials.assign(materialPoints.size(), {});
        const float extent = wave.maxRadius + 1.7f * wave.width;
        spacing = std::max(0.32f, 2.0f * extent / float(kResolution - 1));
        const float half = spacing * float(kResolution - 1) * 0.5f;
        // Use the nearby walking surface. This avoids projecting a ground effect onto
        // overhead roofs or into a different floor far below the portal exit.
        constexpr float bottom = -12.0f;
        constexpr float top = 1.6f;
        const glm::vec3 limitMin(-half, bottom, -half);
        const glm::vec3 limitMax(half, top, half);

        // The fixed slab centers are binned in the same coordinates as the
        // height field, so each triangle just visits the centers it can cover.
        std::array<int, kResolution * kResolution> firstSample;
        firstSample.fill(-1);
        std::vector<int> nextSample(materialPoints.size());
        for (size_t i = 0; i < materialPoints.size(); ++i) {
            const auto point = glm::clamp(glm::ivec2(materialPoints[i]), glm::ivec2(0), glm::ivec2(kResolution - 1));
            const auto cell = size_t(point.y * kResolution + point.x);
            nextSample[i] = firstSample[cell];
            firstSample[cell] = int(i);
        }

        bool hasSurface = false;
        std::vector<glm::vec3> vertices;
        for (const auto& batch : batches) {
            if (batch.isSkinned || batch.isPortalClone || batch.meshIndex >= model.meshes.size() ||
                (riderBody != UINT32_MAX && batch.compoundBodyID == riderBody)) continue;
            if (batch.materialIndex < model.materials.size() && model.materials[batch.materialIndex].alphaBlend) continue;
            const auto& mesh = model.meshes[batch.meshIndex];
            const glm::vec3 localCenter = (mesh.localAabbMin + mesh.localAabbMax) * 0.5f;
            const glm::vec3 localRadius = mesh.localAabbMax - localCenter;
            const glm::vec3 center = glm::vec3(batch.transform * glm::vec4(localCenter, 1.0f)) - wave.origin;
            const glm::vec3 radius = glm::abs(glm::vec3(batch.transform[0])) * localRadius.x +
                glm::abs(glm::vec3(batch.transform[1])) * localRadius.y +
                glm::abs(glm::vec3(batch.transform[2])) * localRadius.z;
            if (glm::any(glm::lessThan(center + radius, limitMin)) ||
                glm::any(glm::greaterThan(center - radius, limitMax))) continue;

            vertices.resize(mesh.positions.size());
            for (size_t i = 0; i < vertices.size(); ++i)
                vertices[i] = glm::vec3(batch.transform * glm::vec4(mesh.positions[i], 1.0f)) - wave.origin;
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const auto ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
                if (std::max({ia, ib, ic}) >= vertices.size()) continue;
                const glm::vec3 a = vertices[ia], b = vertices[ib], c = vertices[ic];
                const glm::vec3 low = glm::min(a, glm::min(b, c));
                const glm::vec3 high = glm::max(a, glm::max(b, c));
                if (glm::any(glm::lessThan(high, limitMin)) || glm::any(glm::greaterThan(low, limitMax))) continue;
                const glm::vec3 face = glm::cross(b - a, c - a);
                if (std::abs(face.y) < 0.000001f || face.y * face.y < 0.48f * glm::dot(face, face)) continue;
                const auto gridFloor = [half, this](float x) { return int(std::floor((x + half) / spacing)); };
                const int xMin = std::max(0, gridFloor(low.x - 0.0001f));
                const int xMax = std::min(kResolution - 1, gridFloor(high.x + 0.0001f));
                const int zMin = std::max(0, gridFloor(low.z - 0.0001f));
                const int zMax = std::min(kResolution - 1, gridFloor(high.z + 0.0001f));
                const float scale = -1.0f / face.y;
                const auto barycentrics = [a, b, c, scale](float dx, float dz) {
                    return glm::vec2((dx * (c.z - a.z) - dz * (c.x - a.x)) * scale,
                        ((b.x - a.x) * dz - (b.z - a.z) * dx) * scale);
                };
                const auto heightAt = [a, b, c, bottom, top](glm::vec2 bary) {
                    if (bary.x < -0.0001f || bary.y < -0.0001f || bary.x + bary.y > 1.0001f) return kMissing;
                    const float height = a.y + (b.y - a.y) * bary.x + (c.y - a.y) * bary.y;
                    return height >= bottom && height <= top ? height : kMissing;
                };
                const bool hasUv = std::max({ia, ib, ic}) < mesh.texcoords.size();
                const glm::vec2 uv = hasUv ? mesh.texcoords[ia] : glm::vec2(0.0f);
                const glm::vec2 uvB = hasUv ? mesh.texcoords[ib] - uv : glm::vec2(0.0f);
                const glm::vec2 uvC = hasUv ? mesh.texcoords[ic] - uv : glm::vec2(0.0f);
                for (int z = zMin; z <= zMax; ++z) {
                    const float dz = float(z) * spacing - half - a.z;
                    for (int x = xMin; x <= xMax; ++x) {
                        const float dx = float(x) * spacing - half - a.x;
                        const auto cell = size_t(z * kResolution + x);
                        const float height = heightAt(barycentrics(dx, dz));
                        if (height > kMissing) {
                            heights[cell] = std::max(heights[cell], height);
                            hasSurface = true;
                        }
                        for (int point = firstSample[cell]; point >= 0; point = nextSample[point]) {
                            const glm::vec2 world = materialPoints[point] * spacing - half;
                            const glm::vec2 bary = barycentrics(world.x - a.x, world.y - a.z);
                            const float sampleHeight = heightAt(bary);
                            auto& sample = materials[point];
                            if (sampleHeight == kMissing || sampleHeight < sample.height) continue;
                            const auto dxBary = barycentrics(1.0f, 0.0f);
                            const auto dzBary = barycentrics(0.0f, 1.0f);
                            sample = {batch.materialIndex, sampleHeight, uv + uvB * bary.x + uvC * bary.y,
                                uvB * dxBary.x + uvC * dxBary.y, uvB * dzBary.x + uvC * dzBary.y};
                        }
                    }
                }
            }
        }
        return hasSurface;
    }
};

} // namespace engine
