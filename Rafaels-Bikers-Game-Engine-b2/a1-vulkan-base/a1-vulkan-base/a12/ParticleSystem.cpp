#include "ParticleSystem.hpp"
#include "../labut2/allocator.hpp"

#include <volk/volk.h>

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

static float rand01() { return float(std::rand()) / float(RAND_MAX); }
static float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

Particle ParticleSystem::spawn(const glm::vec3& emitterPos)
{
    Particle p{};

    // 持续时间和大小的随机值
    p.maxLife = config.lifeMin + rand01() * (config.lifeMax - config.lifeMin);
    p.life = p.maxLife;
    p.baseSize = config.sizeMin + rand01() * (config.sizeMax - config.sizeMin);

    float speed = config.speedMin + rand01() * (config.speedMax - config.speedMin);

    // 随机角度
    float rotDegrees = config.rotationMin + rand01() * (config.rotationMax - config.rotationMin);
    p.rotation = glm::radians(rotDegrees);

    ////发射器形状选择
    switch (m_shape)
    {
        
        case EmitterShape::Cone: {
        p.pos = emitterPos;
        glm::vec3 dir((rand01() * 2.f - 1.f) * config.coneSpread, 1.0f, (rand01() * 2.f - 1.f) * config.coneSpread);
        p.vel = glm::normalize(dir) * speed;
        break;
    }
        case EmitterShape::Sphere: {
        glm::vec3 dir(rand01() * 2.f - 1.f, rand01() * 2.f - 1.f, rand01() * 2.f - 1.f);
        if (glm::length(dir) < 1e-6f) dir = glm::vec3(0, 1, 0);
        dir = glm::normalize(dir);
        p.pos = emitterPos + dir * config.sphereRadius;
        p.vel = dir * speed;
        break;
    }
        case EmitterShape::Box: {
        p.pos = emitterPos + glm::vec3(
            (rand01() * 2.f - 1.f) * config.boxArea.x,
            (rand01() * 2.f - 1.f) * config.boxArea.y,
            (rand01() * 2.f - 1.f) * config.boxArea.z
        );
        // 假设盒形是下雨/雪，给一个默认向下的速度向量
        p.vel = glm::vec3(0.5f, -1.0f, 0.0f) * speed;
        break;
    }
    }
    return p;
}

void ParticleSystem::init(labut2::Allocator const& alloc, uint32_t maxParticles, const glm::vec3& initialEmitterPos)
{
    // 记录分配器
    m_vmaAllocator = alloc.allocator;

    m_maxParticles = maxParticles;
    m_particles.resize(m_maxParticles);
    m_vertices.resize(m_maxParticles);

	for (auto& p : m_particles) p = spawn(initialEmitterPos);//初始发射器位置
    

    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = sizeof(ParticleVertex) * m_maxParticles;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VmaAllocator vma = alloc.allocator;
    VkResult res = vmaCreateBuffer(vma, &bi, &ai, &m_vb, &m_vbAlloc, nullptr);

    //debug线框
    VkBufferCreateInfo dbgBi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    dbgBi.size = sizeof(ParticleVertex) * 8000;
    dbgBi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo dbgAi{};
    dbgAi.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    vmaCreateBuffer(alloc.allocator, &dbgBi, &dbgAi, &m_debugVb, &m_debugVbAlloc, nullptr);

    if (res != VK_SUCCESS) {
        throw std::runtime_error("ParticleSystem: vmaCreateBuffer failed");
    }
}

void ParticleSystem::update(float dt, const glm::vec3& emitterPos)
{
    for (auto& p : m_particles) {
        p.life -= dt;
        if (p.life <= 0.0f) {
            p = spawn(emitterPos);
            continue;
        }
        // 应用配置中的重力
        p.vel += config.gravity * dt;
        p.pos += p.vel * dt;
    }
}

void ParticleSystem::upload(labut2::Allocator const& alloc)
{
    int cols = std::max(1, config.atlasCols);
    int rows = std::max(1, config.atlasRows);
    int totalFrames = cols * rows;

    for (size_t i = 0; i < m_particles.size(); ++i) {
        const Particle& p = m_particles[i];
        ParticleVertex& v = m_vertices[i];

        if (p.life <= 0.0f) {
            v.size = 0.0f; 
            continue; 
        }

        v.pos = p.pos;

        v.rotation = p.rotation; // 旋转

        // 计算生命周期比例
        float t = std::clamp(p.life / p.maxLife, 0.0f, 1.0f);

        // 1. 大小
        float currentScale = config.startSizeScale * t + config.endSizeScale * (1.0f - t);
        v.size = p.baseSize * currentScale;
        // 2. 颜色
        v.color = config.startColor * t + config.endColor * (1.0f - t);

        
        // UV Atlas
        int currentFrame = 0;

        if (totalFrames > 1) {
            if (config.animateAtlas) {
                // 模式 A：随生命周期播放动画 (从出生到死亡播放一轮)
                float ageProgress = 1.0f - t; 
                currentFrame = (int)(ageProgress * totalFrames);
                currentFrame = std::clamp(currentFrame, 0, totalFrames - 1);
            }
            else {
                // 模式 B：静态抽选（随机一张）
                currentFrame = (int)(p.randomSeed * totalFrames);
                currentFrame = std::clamp(currentFrame, 0, totalFrames - 1);
            }
        }

        // Scale
        v.uvRect.x = 1.0f / (float)cols;
        v.uvRect.y = 1.0f / (float)rows;

        // Offset
        v.uvRect.z = (float)(currentFrame % cols) * v.uvRect.x; // Offset X
        v.uvRect.w = (float)(currentFrame / cols) * v.uvRect.y; // Offset Y
    }

    VmaAllocator vma = alloc.allocator;
    void* mapped = nullptr;
    vmaMapMemory(vma, m_vbAlloc, &mapped);
    std::memcpy(mapped, m_vertices.data(), sizeof(ParticleVertex) * m_vertices.size());
    vmaUnmapMemory(vma, m_vbAlloc);
}

void ParticleSystem::draw(VkCommandBuffer cmd) const
{
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vb, &offset);
    vkCmdDraw(cmd, (uint32_t)m_particles.size(), 1, 0, 0);
}

void ParticleSystem::uploadDebug(labut2::Allocator const& alloc, const glm::vec3& emitterPos)
{
    if (!config.particleDebug) return;

    std::vector<ParticleVertex> debugVerts;

    auto addLine = [&](glm::vec3 p1, glm::vec3 p2) {
        float dist = glm::length(p2 - p1);
		int steps = std::max(2, (int)(dist * 20.0f));// 每单位长度20个点

        for (int i = 0; i < steps; ++i) {
            float t = (float)i / (steps - 1);
            ParticleVertex v{};
            v.pos = glm::mix(p1, p2, t);
            v.size = 2.5f; 
            v.color = glm::vec4(0.0f, 0.6f, 1.0f, 1.0f); 
            v.uvRect = glm::vec4(1, 1, 0, 0);
            v.rotation = 0.0f;
            if (debugVerts.size() < 8000) debugVerts.push_back(v);
        }
        };

    const int segments = 24;
    // 根据发射器形状生成点阵
    if (m_shape == EmitterShape::Cone) {
        float h = 5.0f;
        float r = h * config.coneSpread;
        glm::vec3 tip = emitterPos;
        glm::vec3 prevPt = tip + glm::vec3(r, h, 0);
        for (int i = 1; i <= segments; ++i) {
            float angle = (float)i / segments * 2.0f * 3.1415926f;
            glm::vec3 pt = tip + glm::vec3(cos(angle) * r, h, sin(angle) * r);
            addLine(prevPt, pt); // 画顶部的圈
            if (i % (segments / 4) == 0) addLine(tip, pt); // 画四条边
            prevPt = pt;
        }
    }
    else if (m_shape == EmitterShape::Sphere) {
        float r = config.sphereRadius;
        for (int i = 0; i < segments; ++i) {
            float a1 = (float)i / segments * 2.0f * 3.1415926f;
            float a2 = (float)(i + 1) / segments * 2.0f * 3.1415926f;
            addLine(emitterPos + glm::vec3(cos(a1) * r, sin(a1) * r, 0), emitterPos + glm::vec3(cos(a2) * r, sin(a2) * r, 0));
            addLine(emitterPos + glm::vec3(cos(a1) * r, 0, sin(a1) * r), emitterPos + glm::vec3(cos(a2) * r, 0, sin(a2) * r));
            addLine(emitterPos + glm::vec3(0, cos(a1) * r, sin(a1) * r), emitterPos + glm::vec3(0, cos(a2) * r, sin(a2) * r));
        }
    }
    else if (m_shape == EmitterShape::Box) {
        float hx = config.boxArea.x, hy = config.boxArea.y, hz = config.boxArea.z;
        glm::vec3 center = emitterPos; // 现在的中心点就是发射器的原点

        // 定义顶面的 4 个角
        glm::vec3 t1 = center + glm::vec3(-hx, hy, -hz);
        glm::vec3 t2 = center + glm::vec3(hx, hy, -hz);
        glm::vec3 t3 = center + glm::vec3(hx, hy, hz);
        glm::vec3 t4 = center + glm::vec3(-hx, hy, hz);

        // 定义底面的 4 个角
        glm::vec3 b1 = center + glm::vec3(-hx, -hy, -hz);
        glm::vec3 b2 = center + glm::vec3(hx, -hy, -hz);
        glm::vec3 b3 = center + glm::vec3(hx, -hy, hz);
        glm::vec3 b4 = center + glm::vec3(-hx, -hy, hz);

        // 1. 画顶面的 4 条边
        addLine(t1, t2); addLine(t2, t3); addLine(t3, t4); addLine(t4, t1);
        // 2. 画底面的 4 条边
        addLine(b1, b2); addLine(b2, b3); addLine(b3, b4); addLine(b4, b1);
        // 3. 画连接上下的 4 条竖边
        addLine(t1, b1); addLine(t2, b2); addLine(t3, b3); addLine(t4, b4);
    }

    m_debugVertexCount = (uint32_t)debugVerts.size();
    if (m_debugVertexCount > 0) {
        VmaAllocator vma = alloc.allocator;
        void* mapped = nullptr;
        vmaMapMemory(m_vmaAllocator, m_debugVbAlloc, &mapped);
        std::memcpy(mapped, debugVerts.data(), sizeof(ParticleVertex) * m_debugVertexCount);
        vmaUnmapMemory(m_vmaAllocator, m_debugVbAlloc);
    }
}

void ParticleSystem::drawDebug(VkCommandBuffer cmd, VkPipelineLayout layout) const
{
    if (!config.particleDebug || m_debugVertexCount == 0) return;

    struct ParticlePC { int useTex; int dbg; int pad[2]; glm::mat4 transform; } pc{};
    pc.useTex = 0; 
    pc.dbg = 0;
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ParticlePC), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_debugVb, &offset);
    vkCmdDraw(cmd, m_debugVertexCount, 1, 0, 0);
}

void ParticleSystem::shutdown(labut2::Allocator const& alloc)
{
    VmaAllocator vma = alloc.allocator;

    if (m_vb != VK_NULL_HANDLE) {
        
        vmaDestroyBuffer(vma, m_vb, m_vbAlloc);
        m_vb = VK_NULL_HANDLE;
        m_vbAlloc = VK_NULL_HANDLE;
    }
   
    // 销毁 Debug线框
    if (m_debugVb != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vma, m_debugVb, m_debugVbAlloc);
        m_debugVb = VK_NULL_HANDLE;
        m_debugVbAlloc = VK_NULL_HANDLE;
    }

    m_particles.clear();
    m_vertices.clear();
    m_maxParticles = 0;
    m_debugVertexCount = 0;
}