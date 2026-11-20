#pragma once

#include "PortalGroundSurface.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace engine {

struct PortalGroundSlab {
    static constexpr int kMaxCorners = 12;
    static constexpr uint32_t kVerticesPerCorner = 12;
    static constexpr uint32_t kFissureVerticesPerCorner = 3;
    glm::vec4 centerAndHinge;
    std::array<glm::vec2, kMaxCorners> corners{};
};
static_assert(sizeof(PortalGroundSlab) == 112);

inline std::vector<PortalGroundSlab> BuildPortalGroundSlabs() {
    constexpr int cells = 35;
    constexpr float limit = float(PortalGroundSurface::kResolution - 1);
    constexpr float cellSize = limit / float(cells);
    struct Sites {
        std::array<glm::vec2, 2> points;
        int count = 0;
    };
    const auto random = [](uint32_t index, uint32_t salt) {
        uint32_t value = (index + salt * 0x9e3779b9u) ^ 0xa53a6c19u;
        value = (value ^ (value >> 16u)) * 0x7feb352du;
        value = (value ^ (value >> 15u)) * 0x846ca68bu;
        return float((value ^ (value >> 16u)) & 0xffffu) / 65536.0f;
    };

    std::array<Sites, cells * cells> sites{};
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const auto index = uint32_t(y * cells + x);
            // Even sites keep the partition bounded. Missing sites form larger
            // slabs and paired sites create narrow wedges next to them.
            if ((x % 2 || y % 2) && random(index, 1) < 0.44f) continue;
            auto& site = sites[index];
            const glm::vec2 center(x, y);
            const glm::vec2 primary(0.13f + 0.74f * random(index, 2), 0.13f + 0.74f * random(index, 3));
            site.points[0] = (center + primary) * cellSize;
            site.count = 1;
            if (random(index, 4) < 0.23f) {
                const glm::vec2 secondary = glm::vec2(1.0f) - primary;
                if (glm::distance(primary, secondary) > 0.22f)
                    site.points[site.count++] = (center + secondary) * cellSize;
            }
        }
    }

    std::vector<PortalGroundSlab> slabs;
    std::vector<glm::vec2> polygon, clipped;
    polygon.reserve(16);
    clipped.reserve(16);
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const auto index = uint32_t(y * cells + x);
            const auto& site = sites[index];
            for (int piece = 0; piece < site.count; ++piece) {
                const glm::vec2 seed = site.points[piece];
                polygon = {{0, 0}, {limit, 0}, {limit, limit}, {0, limit}};
                for (int ny = std::max(0, y - 4); ny <= std::min(cells - 1, y + 4); ++ny) {
                    for (int nx = std::max(0, x - 4); nx <= std::min(cells - 1, x + 4); ++nx) {
                        const auto& other = sites[ny * cells + nx];
                        for (int n = 0; n < other.count; ++n) {
                            const glm::vec2 neighbor = other.points[n];
                            if (neighbor == seed) continue;
                            const glm::vec2 axis = neighbor - seed;
                            const glm::vec2 bisector = (neighbor + seed) * 0.5f;
                            clipped.clear();
                            for (size_t a = 0; a < polygon.size(); ++a) {
                                const glm::vec2 from = polygon[a], to = polygon[(a + 1) % polygon.size()];
                                const float f = glm::dot(from - bisector, axis), t = glm::dot(to - bisector, axis);
                                if (f <= 0.0f) clipped.push_back(from);
                                if ((f < 0.0f && t > 0.0f) || (f > 0.0f && t < 0.0f))
                                    clipped.push_back(glm::mix(from, to, f / (f - t)));
                            }
                            polygon.swap(clipped);
                        }
                    }
                }
                if (polygon.size() < 3) continue;
                // Remove the least significant corner if a rare cell exceeds the GPU record.
                while (polygon.size() > PortalGroundSlab::kMaxCorners) {
                    size_t smallest = 0;
                    float smallestArea = std::numeric_limits<float>::max();
                    for (size_t i = 0; i < polygon.size(); ++i) {
                        const glm::vec2 a = polygon[(i + polygon.size() - 1) % polygon.size()] - polygon[i];
                        const glm::vec2 b = polygon[(i + 1) % polygon.size()] - polygon[i];
                        const float area = std::abs(a.x * b.y - a.y * b.x);
                        if (area < smallestArea) {
                            smallest = i;
                            smallestArea = area;
                        }
                    }
                    polygon.erase(polygon.begin() + smallest);
                }

                glm::vec2 center(0.0f);
                for (const auto point : polygon) center += point;
                center /= float(polygon.size());
                const glm::vec2 outward = center - limit * 0.5f +
                    glm::vec2(random(index, 11) - 0.5f, random(index, 12) - 0.5f) * 7.0f;
                size_t hinge = 0;
                float best = -std::numeric_limits<float>::max();
                for (size_t i = 0; i < polygon.size(); ++i) {
                    const glm::vec2 edge = (polygon[i] + polygon[(i + 1) % polygon.size()]) * 0.5f - center;
                    const float alignment = glm::dot(edge, outward);
                    if (alignment > best) {
                        best = alignment;
                        hinge = i;
                    }
                }
                PortalGroundSlab slab;
                slab.centerAndHinge = {center, float(polygon.size()), float(hinge)};
                std::copy(polygon.begin(), polygon.end(), slab.corners.begin());
                slabs.push_back(slab);
            }
        }
    }
    return slabs;
}

} // namespace engine
