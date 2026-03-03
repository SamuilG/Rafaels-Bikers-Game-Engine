// record_params.hpp
#pragma once
#include <volk/volk.h>
#include <vector>
#include "setup.hpp"
#include "camera.hpp"
#include "engine_model.hpp"
#include "../labut2/vkobject.hpp"
#include "../labut2/vulkan_window.hpp"
#include "../labut2/vkbuffer.hpp" 

// 这些类型用你的工程已有定义即可
struct ImageAndView;          // 你的类型
namespace glsl { struct SceneUniform; }
struct EngineMesh;
struct EngineMaterial;
struct EngineInstance;

struct RecordParams
{
    // --- per-frame / global ---
    VkBuffer              sceneUboBuffer{};     // sceneUBO.buffer
    glsl::SceneUniform    sceneUniforms{};      // 按你的类型/拷贝成本决定：也可以改成 const glsl::SceneUniform&
    VkPipelineLayout      pipeLayout{};         // pipeLayout.handle
    VkDescriptorSet       sceneDescriptors{};   // sceneDescriptors（如果是 set）
    VkExtent2D            swapchainExtent{};    // window.swapchainExtent

    // --- targets / passes ---
    VkPipeline            currentOpaque{};      // currentOpaque
    VkPipeline            currentAlpha{};       // currentAlpha
    ImageAndView          colorTarget{};        // colorTarget
    ImageAndView          depthTarget{};        // depthTarget
    ImageAndView          offscreenTarget{};    // offscreenTarget
    VkClearColorValue     clearColor{};         // clearColor

    // --- mesh buffers (per-frame arrays) ---
    // 你现在是 meshPositions / meshTexCoords / meshNormals / meshIndices 的 vector<Buffer>，
    // 这里用引用避免拷贝
    const std::vector<labut2::Buffer>* meshPositions = nullptr;
    const std::vector<labut2::Buffer>* meshTexCoords = nullptr;
    const std::vector<labut2::Buffer>* meshNormals = nullptr;
    const std::vector<labut2::Buffer>* meshIndices = nullptr;

    // cbuffers[frameIndex] 这种（如果你需要）也可以一起塞进来
    const labut2::Buffer* cbuffers = nullptr;

    // --- model data ---
    const std::vector<EngineMesh>* meshes = nullptr; // model.meshes
    const std::vector<EngineMaterial>* materials = nullptr; // model.materials
    const std::vector<EngineInstance>* scenes = nullptr; // model.scenes（看你实际类型）

    // --- descriptor sets (currentDescriptors 解引用后的容器) ---
    // 你写的是 *currentDescriptors，所以这里放一个指向“容器”的指针
    const std::vector<VkDescriptorSet>* currentDescriptors = nullptr;

    // --- resolve pass ---
    VkPipeline       resolvePipeline{};
    VkDescriptorSet  resolveDescriptors{};
    VkPipelineLayout resolveLayout{};

    // --- shadow pass ---
    VkPipelineLayout shadowPipeLayout{};     // shadowPipe.handle (看你 handle 实际类型)
    ImageAndView     shadowTarget{};
    const std::vector<VkImageView>* shadowCascadeViews = nullptr;
};