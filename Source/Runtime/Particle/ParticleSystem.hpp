#pragma once

#include <volk/volk.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include <vk_mem_alloc.h>

namespace labut2 { class Allocator; }

//发射器形态 Emitter Shape
enum class EmitterShape 
{
    Cone,   // 锥形
    Sphere, // 球形
    Box     // 盒形
};

// 粒子参数配置
struct ParticleConfig {

    char name[64] = "Particle name";
    bool isVisible = true;
	int useTexture = 0; // 是否使用纹理（0 = 默认小圆球不使用，1 = 使用贴图）

    // life time 持续时间
    float lifeMin = 1.0f;
    float lifeMax = 2.0f;

    // size 大小
    float sizeMin = 15.0f;
    float sizeMax = 40.0f;

    // speed 速度
    float speedMin = 2.0f;
    float speedMax = 6.0f;

    // 颜色渐变 (RGBA)
    glm::vec4 startColor = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f); // 出生时
    glm::vec4 endColor = glm::vec4(1.0f, 0.1f, 0.0f, 0.0f); // 死亡时

    // 物理与形态参数
    glm::vec3 gravity = glm::vec3(0.0f, 1.5f, 0.0f);
	float coneSpread = 0.35f;// coneSpread 锥形发射器的扩散度
	float sphereRadius = 1.5f;//sphereRadius 球形发射器的半径
	glm::vec3 boxArea = glm::vec3(30.0f, 15.0f, 30.0f);// box area盒形发射器的范围（宽、高、深）

    // Atlas 序列帧设置
    int atlasCols = 1;         // 贴图有几列（默认 1，即单图）
    int atlasRows = 1;         // 贴图有几行（默认 1，即单图）
    bool animateAtlas = false; // 是否随生命周期播放序列帧动画

    // 生命周期大小缩放控制
    float startSizeScale = 1.0f; // 出生时的缩放比例 
    float endSizeScale = 0.0f;   // 死亡时的缩放比例 
    //随机初始旋转角度
    float rotationMin = 0.0f;
    float rotationMax = 0.0f;

	glm::vec3 emitterPos = glm::vec3(0.0f, 0.0f, 0.0f);// 发射器位置

    bool particleDebug = false; 
    VkDescriptorSet textureDescriptor = VK_NULL_HANDLE;

};

struct Particle 
{
	glm::vec3 pos;
	glm::vec3 vel;
	float life;     // 剩余寿命
	float maxLife;  // 初始寿命
	float baseSize;     // 点大小
    float randomSeed; 
    float rotation; //出生时随机的旋转

};

struct ParticleVertex {
	glm::vec3 pos;
	float size;
	glm::vec4 color; // color & alpha
    glm::vec4 uvRect; //xy = UV Scale, zw = UV Offset
    float rotation;  //旋转角
};

class ParticleSystem {
public:

    //析构函数，保证对象销毁时自动退还 GPU 内存
    ~ParticleSystem() {
        if (m_vb != VK_NULL_HANDLE && m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_vb, m_vbAlloc);
            m_vb = VK_NULL_HANDLE;
            m_vbAlloc = VK_NULL_HANDLE;
        }

        //销毁 Debug 线框 Buffer
        if (m_debugVb != VK_NULL_HANDLE && m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_debugVb, m_debugVbAlloc);
            m_debugVb = VK_NULL_HANDLE;
            m_debugVbAlloc = VK_NULL_HANDLE;
        }
    }
    void init(labut2::Allocator const& alloc, uint32_t maxParticles, const glm::vec3& initialEmitterPos);
    void shutdown(labut2::Allocator const& alloc);

    void update(float dt, const glm::vec3& emitterPos);
    void upload(labut2::Allocator const& alloc);
    void draw(VkCommandBuffer cmd) const;

    uint32_t count() const { return (uint32_t)m_particles.size(); }

    ParticleConfig config;

    // 切换和获取发射器形态的方法
    void setEmitterShape(EmitterShape shape) { m_shape = shape; }
    EmitterShape getEmitterShape() const { return m_shape; }

    //debug线框
    void uploadDebug(labut2::Allocator const& alloc, const glm::vec3& emitterPos);
    void drawDebug(VkCommandBuffer cmd, VkPipelineLayout layout) const;

private:
    Particle spawn(const glm::vec3& emitterPos);

private:
    uint32_t m_maxParticles = 0;

	EmitterShape m_shape = EmitterShape::Cone; // 默认发射器形态

    std::vector<Particle> m_particles;
    std::vector<ParticleVertex> m_vertices;

    VkBuffer m_vb = VK_NULL_HANDLE;
    VmaAllocation m_vbAlloc = VK_NULL_HANDLE;

    //保存 VmaAllocator，以便析构函数使用
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    // debug发射器线框
    VkBuffer m_debugVb = VK_NULL_HANDLE;
    VmaAllocation m_debugVbAlloc = VK_NULL_HANDLE;
    uint32_t m_debugVertexCount = 0;
};