#pragma once

#include <volk/volk.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

#include <vk_mem_alloc.h>

namespace labut2 { class Allocator; }

//��������̬ Emitter Shape
enum class EmitterShape
{
    Cone,   // ׶��
    Sphere, // ����
    Box,    // ����
    Disk    // XZ 平面圆盘，粒子从半径 sphereRadius 的圆盘随机位置生成，方向由 emitDir 决定
};

// ���Ӳ�������
struct ParticleConfig {
    char name[64] = "Particle name";
    bool isVisible = true;
    //�Ƿ�trigger box����//marks whether this particle system is currently controlled by trigger visibility logic.
    bool triggerControlled = false;
	int useTexture = 0; // �Ƿ�ʹ��������0 = Ĭ��СԲ��ʹ�ã�1 = ʹ����ͼ��

    // life time ����ʱ��
    float lifeMin = 1.0f;
    float lifeMax = 2.0f;

    // size ��С
    float sizeMin = 15.0f;
    float sizeMax = 40.0f;

    // speed �ٶ�
    float speedMin = 2.0f;
    float speedMax = 6.0f;

    // ��ɫ���� (RGBA)
    glm::vec4 startColor = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f); // ����ʱ
    glm::vec4 endColor = glm::vec4(1.0f, 0.1f, 0.0f, 0.0f); // ����ʱ

    // ��������̬����
    glm::vec3 gravity = glm::vec3(0.0f, 1.5f, 0.0f);
	float coneSpread = 0.35f;// coneSpread ׶�η���������ɢ��
	float sphereRadius = 1.5f;//sphereRadius ���η������İ뾶
	glm::vec3 boxArea = glm::vec3(30.0f, 15.0f, 30.0f);// box area���η������ķ�Χ�������ߡ��

    // Atlas ����֡����
    int atlasCols = 1;         // ��ͼ�м��У�Ĭ�� 1������ͼ��
    int atlasRows = 1;         // ��ͼ�м��У�Ĭ�� 1������ͼ��
    bool animateAtlas = false; // �Ƿ����������ڲ�������֡����

    // �������ڴ�С���ſ���
    float startSizeScale = 1.0f; // ����ʱ�����ű��� 
    float endSizeScale = 0.0f;   // ����ʱ�����ű��� 
    //�����ʼ��ת�Ƕ�
    float rotationMin = 0.0f;
    float rotationMax = 0.0f;

	glm::vec3 emitterPos = glm::vec3(0.0f, 0.0f, 0.0f);// ������λ��
    glm::vec3 emitDir   = glm::vec3(0.0f, 1.0f, 0.0f); // 发射方向（单位向量，Cone 模式使用）

    bool particleDebug = false; 
    VkDescriptorSet textureDescriptor = VK_NULL_HANDLE;
    // �� 2D UI (ImGui �ӿ�ͼ��) �õ���ͼ ID
    VkDescriptorSet uiIconDescriptor = VK_NULL_HANDLE;
};

struct Particle 
{
	glm::vec3 pos;
	glm::vec3 vel;
	float life;     // ʣ������
	float maxLife;  // ��ʼ����
	float baseSize;     // ���С
    float randomSeed; 
    float rotation; //����ʱ�������ת

};

struct ParticleVertex {
	glm::vec3 pos;
	float size;
	glm::vec4 color; // color & alpha
    glm::vec4 uvRect; //xy = UV Scale, zw = UV Offset
    float rotation;  //��ת��
};

class ParticleSystem {
public:

    //������������֤��������ʱ�Զ��˻� GPU �ڴ�
    ~ParticleSystem() {
        if (m_vb != VK_NULL_HANDLE && m_vmaAllocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_vb, m_vbAlloc);
            m_vb = VK_NULL_HANDLE;
            m_vbAlloc = VK_NULL_HANDLE;
        }

        //���� Debug �߿� Buffer
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

    // �л��ͻ�ȡ��������̬�ķ���
    void setEmitterShape(EmitterShape shape) { m_shape = shape; }
    EmitterShape getEmitterShape() const { return m_shape; }

    //debug�߿�
    void uploadDebug(labut2::Allocator const& alloc, const glm::vec3& emitterPos);
    void drawDebug(VkCommandBuffer cmd, VkPipelineLayout layout) const;

private:
    Particle spawn(const glm::vec3& emitterPos);

private:
    uint32_t m_maxParticles = 0;

	EmitterShape m_shape = EmitterShape::Cone; // Ĭ�Ϸ�������̬

    std::vector<Particle> m_particles;
    std::vector<ParticleVertex> m_vertices;

    VkBuffer m_vb = VK_NULL_HANDLE;
    VmaAllocation m_vbAlloc = VK_NULL_HANDLE;

    //���� VmaAllocator���Ա���������ʹ��
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

    // debug�������߿�
    VkBuffer m_debugVb = VK_NULL_HANDLE;
    VmaAllocation m_debugVbAlloc = VK_NULL_HANDLE;
    uint32_t m_debugVertexCount = 0;
};