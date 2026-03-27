#pragma once

#if !defined(GLM_ENABLE_EXPERIMENTAL)
#   define GLM_ENABLE_EXPERIMENTAL   
#endif

#if !defined(GLM_FORCE_RADIANS)
#   define GLM_FORCE_RADIANS
#endif

#include "../Core/System.h"

#include <volk/volk.h>

#include <print>
#include <chrono>
#include <limits>
#include <vector>
#include <stdexcept>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#define GLFW_INCLUDE_NONE

#include <GLFW/glfw3.h>


#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../Rhi/angle.hpp"
using namespace labut2::literals;

#include "../Rhi/load.hpp"
#include "../Rhi/error.hpp"
#include "../Rhi/synch.hpp"
#include "../Rhi/vkimage.hpp"
#include "../Rhi/commands.hpp"
#include "../Rhi/textures.hpp"
#include "../Rhi/vkbuffer.hpp"
#include "../Rhi/vkobject.hpp"
#include "../Rhi/to_string.hpp"
#include "../Rhi/descriptors.hpp"
#include "../Rhi/vulkan_window.hpp"
#include "../Scene/model_loader/stb_image.h"


#include "../Particle/ParticleSystem.hpp"

namespace lut = labut2;

#include "RenderUtilities/camera.hpp"
#include "RenderUtilities/setup.hpp"
#include "RenderUtilities/rendering.hpp"

#include "../Input/InputSystem.hpp"
#include <chrono> // 确保顶部包含了这个

// ================= UI System =================
#include "../UI/ui.hpp"
#include "../UI/EngineUi.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include "../UI/MousePicker.hpp"
#include "../../ThirdParty/imgui/ImGuizmo/ImGuizmo.h"
#include "../Physics/PhysicsSystem.hpp"
#include "../UserState/UserState.hpp"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <algorithm>
#include <flecs.h>
// ================= debug =================
#include "../debug/DebugRenderer.hpp"
namespace glsl {
    struct MosaicUniform {
        int   mosaicOn;
        float pad[3];
    };
}

namespace engine {

    class RenderSystem final : public System
    {
 
    public:

		//==============camera follow =======================
		// name of the entity to follow in the scene
		//TODO 把这个做到ui里面，允许用户输入想要跟随的实体名字，或者在场景视口直接点击选中一个实体进行跟随
		const char* player = "立方体_0"; 



        explicit RenderSystem(bool& appRunning, SceneManager* sceneManager = nullptr)
            : mAppRunning(appRunning), mSceneManager(sceneManager) {
        }

    private:
        bool& mAppRunning;
        SceneManager* mSceneManager;

        lut::VulkanWindow  mWindow;
        lut::Allocator     mAllocator;

    public:



        GLFWwindow* GetGLFWWindow() const {
            if (mWindow.window) return mWindow.window;
            return nullptr;
        }
       

        //==============UI System========= Draw the main menu UI
        void DrawMainMenuUI() {
            // 游戏未开始// Game not started
            if (!mState->isGameStarted) {
                // Draw the main menu UI主菜单
                EngineUi::DrawMainMenu(this, mAppRunning, mState->isGameStarted);
            }
        }

        //==========UI System（particle）======================
        // 存储 ImGui 专用的贴图描述符// Store ImGui-specific texture descriptors
        std::unordered_map<std::string, VkDescriptorSet> particleImGuiTextureDict;

        // 获取 ImGui 专用的渲染句柄
        // Get the rendering handle for ImGui-specific textures
        VkDescriptorSet GetImGuiTextureDescriptor(const std::string& name) {
            if (particleImGuiTextureDict.count(name)) {
                return particleImGuiTextureDict[name];
            }
            return VK_NULL_HANDLE;
        }

        //获取所有粒子贴图的路径/名字
        // Get the paths/names of all particle textures
        std::vector<std::string> GetParticleTextureNames() const {
            std::vector<std::string> names;
            for (const auto& pair : particleTextureDict) {
                names.push_back(pair.first);
            }
            return names;
        }

        //根据名字获取对应的贴图描述符
        // Get the corresponding texture descriptor based on the name
        VkDescriptorSet GetParticleTextureDescriptor(const std::string& name) {
            if (particleTextureDict.count(name)) {
                return particleTextureDict[name];
            }
            return VK_NULL_HANDLE;
        }
        //调整最大粒子数量（重建粒子系统）
        //adjust the maximum number of particles (rebuild the particle system)
        void ResizeParticleGroup(size_t index, uint32_t newMaxParticles) {
            if (index < allParticles.size()) {
                vkDeviceWaitIdle(mWindow.device);

                auto& ps = allParticles[index];

                //备份参数
                //backup parameters
                ParticleConfig savedConfig = ps->config;
                EmitterShape savedShape = ps->getEmitterShape();
                glm::vec3 savedPos = savedConfig.emitterPos;

                ps->shutdown(mAllocator);

                //重新分配显存并初始化
                //reallocate GPU memory and initialize
                ps->init(mAllocator, newMaxParticles, savedPos);

                //还原参数
                //restore parameters
                ps->config = savedConfig;
                ps->setEmitterShape(savedShape);
            }
        }

        //UI System get particle system reference
        std::vector<std::unique_ptr<ParticleSystem>>& GetParticles() { return allParticles; }
        //动态安全创建粒子组
        //create particle group
        void AddParticleGroup() {
            vkDeviceWaitIdle(mWindow.device);

            auto ps = std::make_unique<ParticleSystem>();
            ps->setEmitterShape(EmitterShape::Sphere); // default to sphere emitter

            // 绑定默认贴图
            //blind default texture
            if (particleTextureDict.count(cfg::ParticleTextures[0])) {
                ps->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[0]];
                // 新增：同时绑定 UI 专用的贴图！
                ps->config.uiIconDescriptor = particleImGuiTextureDict[cfg::ParticleTextures[5]];
                ps->config.useTexture = 1;
                ps->config.atlasCols = 4;
                ps->config.atlasRows = 4;
                ps->config.animateAtlas = true;
            }

            ps->config.emitterPos = glm::vec3(0.0f, 5.0f, 0.0f);

            //generate a default name based on the current number of particle groups
            std::string defaultName = "Group " + std::to_string(allParticles.size() + 1);
            snprintf(ps->config.name, sizeof(ps->config.name), "%s", defaultName.c_str());

            ps->init(mAllocator, 500, ps->config.emitterPos);
            allParticles.push_back(std::move(ps));
        }

        //删除粒子组
        //delete particle group
        void RemoveParticleGroup(size_t index) {
            if (index < allParticles.size()) {
                vkDeviceWaitIdle(mWindow.device);
                allParticles.erase(allParticles.begin() + index);
            }
        }
        //==========UI System（particle）======================


        void Init() override
        {

            


            // Create Vulkan Window
            mWindow = lut::make_vulkan_window();

            // Initialize state
            glfwSetWindowUserPointer(mWindow.window, mState);
            // Key and mouse callbacks handled entirely by the centralised engine::InputSystem

            // Create VMA allocator
            mAllocator = lut::create_allocator(mWindow);

            // Intialize resources
            mSceneLayout = create_scene_descriptor_layout(mWindow);
            mObjectLayout = create_object_descriptor_layout(mWindow);
            mPostLayout = create_post_proc_descriptor_layout(mWindow);

            mPipeLayout = create_triangle_pipeline_layout(mWindow, mSceneLayout.handle, mObjectLayout.handle);
            mPostPipeLayout = create_post_proc_pipeline_layout(mWindow, mPostLayout.handle);

            mPipe = create_triangle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);

            // Create multiple debug pipelines
            mMipPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
            mDepthPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
            mDerivPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);

            // overdraw/overshading pipelines
            // pipelines for part 2 task 1
            mOverdrawPipe = create_overdraw_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mOvershadingPipe = create_overshading_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            // resolve pass
            mVisResolvePipe = create_vis_resolve_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);

            //particle pipeline
            mParticlePipe = create_particle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);

            mCmdPool = lut::create_command_pool(mWindow,
                VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

            for (std::size_t i = 0; i < mWindow.swapImages.size(); ++i) {
                mCmdBuffers.emplace_back(lut::alloc_command_buffer(mWindow, mCmdPool.handle));
                mFrameDone.emplace_back(lut::create_fence(mWindow.device, VK_FENCE_CREATE_SIGNALED_BIT));
                mImageAvailable.emplace_back(lut::create_semaphore(mWindow.device));
                mRenderFinished.emplace_back(lut::create_semaphore(mWindow.device));
            }
           


            
            // set up initial textures and descriptor pools
            // load actual models via load_additional_model
            
            // Set initial camera position
            // Move camera back (z+) and up (y+) to see the scene
            mState->camera2world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 10.0f));

            {
                // just for objects without texture to set a default texture
                // RGBA: 128, 128, 128, 255 (grey)
                //std::uint8_t grey[4] = { 128, 128, 128, 255 };
				//need it be white for pure color models, otherwise the default grey will darken the colors in the shader (multiplying by 128/255)
                std::uint8_t grey[4] = { 255, 255, 255, 255 };
                // uploda 1x1 pixel to GPU
                mDefaultGrayTex = lut::load_image_texture2d_from_memory(
                    grey, 1, 1,
                    mWindow, mCmdPool.handle, mAllocator,
                    VK_FORMAT_R8G8B8A8_UNORM);

                // create grey imageview
                mDefaultGrayView = lut::create_image_view_texture2d(
                    mWindow, mDefaultGrayTex.image, VK_FORMAT_R8G8B8A8_UNORM);

                std::uint8_t black[4] = { 0, 0, 0, 255 };
                mDefaultBlackTex = lut::load_image_texture2d_from_memory(
                    black, 1, 1,
                    mWindow, mCmdPool.handle, mAllocator,
                    VK_FORMAT_R8G8B8A8_UNORM);
                mDefaultBlackView = lut::create_image_view_texture2d(
                    mWindow, mDefaultBlackTex.image, VK_FORMAT_R8G8B8A8_UNORM);


                // 2. 【新增】标准的蓝色法线图 (朝向 Z 轴)
                std::uint8_t normalBlue[4] = { 128, 128, 255, 255 };
                mDefaultNormalTex = lut::load_image_texture2d_from_memory(
                    normalBlue, 1, 1, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
                mDefaultNormalView = lut::create_image_view_texture2d(
                    mWindow, mDefaultNormalTex.image, VK_FORMAT_R8G8B8A8_UNORM);
            }





            // sampler
            mDefaultSampler = lut::create_default_sampler(mWindow);
            mDebugSampler = create_debug_sampler(mWindow);
            mDescPool = lut::create_descriptor_pool(mWindow);
            mParticleDescPool = lut::create_descriptor_pool(mWindow);
            // allocate an initial empty descriptor array so adding runtime models works
            // BuildMaterialDescriptors(mDefaultSampler.handle, mMaterialDescriptors);
            // BuildMaterialDescriptors(mDebugSampler.handle, mDebugMaterialDescriptors);

            // UploadMeshes() will be driven by load_additional_model

			// Debug Renderer============================
            mDebugRenderer.Init(mAllocator);
            
            mDebugLinePipe = create_debug_line_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
           

            //================particle system===================================================
            // 粒子系统
           

            //particle textures
            for (const auto& path : cfg::ParticleTextures) {

                // BAD: load_image_texture2d() may set stbi flip flag as a side effect.
                // TinyGLTF shares the same stb_image instance (STB_IMAGE_IMPLEMENTATION
                // is defined in engine_model.cpp), so any leftover flip state will corrupt
                // subsequent GLB texture decoding.

                // FIX: Always load particle textures AFTER all load_additional_model() calls,
                // or explicitly reset the flag with stbi_set_flip_vertically_on_load(0)
                // before and after loading particle textures from disk.
                stbi_set_flip_vertically_on_load(0);

                //load Image
                
                lut::Image img = lut::load_image_texture2d(path, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);

                stbi_set_flip_vertically_on_load(0);
                //View
                lut::ImageView view = lut::create_image_view_texture2d(mWindow, img.image, VK_FORMAT_R8G8B8A8_UNORM);

                //Descriptor Set
                VkDescriptorSet descSet = lut::alloc_desc_set(mWindow, mParticleDescPool.handle, mObjectLayout.handle);

                VkDescriptorImageInfo mainImgInfo{};
                mainImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                mainImgInfo.imageView = view.handle;
                mainImgInfo.sampler = mDefaultSampler.handle;

                VkDescriptorImageInfo dummyImgInfo{};
                dummyImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                dummyImgInfo.imageView = mDefaultGrayView.handle;
                dummyImgInfo.sampler = mDefaultSampler.handle;

                VkWriteDescriptorSet writes[3]{};
                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = descSet; writes[0].dstBinding = 0;
                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].descriptorCount = 1; writes[0].pImageInfo = &mainImgInfo;

                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = descSet; writes[1].dstBinding = 1;
                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].descriptorCount = 1; writes[1].pImageInfo = &dummyImgInfo;

                writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[2].dstSet = descSet; writes[2].dstBinding = 2;
                writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[2].descriptorCount = 1; writes[2].pImageInfo = &dummyImgInfo;

                vkUpdateDescriptorSets(mWindow.device, 3, writes, 0, nullptr);

                particleTextureDict[path] = descSet;

                particleImages.push_back(std::move(img));
                particleImageViews.push_back(std::move(view));
            }
            //================particle system===================================================




             //================particle system===================================================
            //emitter pos;
            glm::vec3 emitterPos1(2, 0.5, 2);
            glm::vec3 emitterPos2(3, 0.5, 2);
            glm::vec3 emitterPos3(4, 0.5, 2);
            glm::vec3 emitterPos4(2, 0.8, 2);

            // 創建第 1 組：火焰
            {
                auto fire = std::make_unique<ParticleSystem>();
                //發射器形狀
                fire->setEmitterShape(EmitterShape::Cone);
                fire->config.coneSpread = 0.1f;// 控制锥形的开口大小
                //debug
                fire->config.particleDebug = false; // 开启粒子调试输出
                //貼圖設定
                fire->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[0]]; // 綁定貼圖
                fire->config.useTexture = 1;
                fire->config.atlasCols = 4;   // 贴图切成 4 列
                fire->config.atlasRows = 4;   // 贴图切成 4 行
                fire->config.animateAtlas = true;
                //顔色
                fire->config.startColor = glm::vec4(255.0f, 125.8f, 0.3f, .05f);
                fire->config.endColor = glm::vec4(0.05f, 0.05f, 0.05f, 0.1f);
                //旋轉
                fire->config.rotationMin = -360.0f;
                fire->config.rotationMax = 360.0f;
                //重力
                fire->config.gravity = glm::vec3(0.0f, 0.01f, 0.0f);
                //持续时间
                fire->config.lifeMin = 1.f;  // 最短存活时间
                fire->config.lifeMax = 3.0f;  // 最长存活时间
                //粒子尺寸
                fire->config.sizeMin = 50.0f;
                fire->config.sizeMax = 130.0f;
                //粒子尺寸缩放：出生时和死亡时的放大倍数
                fire->config.startSizeScale = 3.0f;
                fire->config.endSizeScale = 0.f;
                //初始速度
                fire->config.speedMin = 0.1f; // 最小初速度
                fire->config.speedMax = 0.5f; // 最大初速度
                //位置
                fire->config.emitterPos = emitterPos1;;
                fire->init(mAllocator, 300, emitterPos1);
                allParticles.push_back(std::move(fire)); 
            }
            //創建第 2組：灰煙
            {
                auto smoke = std::make_unique<ParticleSystem>();
                //發射器形狀
                smoke->setEmitterShape(EmitterShape::Cone);
                smoke->config.coneSpread = 0.1f;// 控制锥形的开口大小
                //debug
                smoke->config.particleDebug = false; // 开启粒子调试输出
                //貼圖設定
                smoke->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[0]]; // 綁定貼圖
                smoke->config.useTexture = 1;
                smoke->config.atlasCols = 4;   // 贴图切成 4 列
                smoke->config.atlasRows = 4;   // 贴图切成 4 行
                smoke->config.animateAtlas = true;
                //顔色
                smoke->config.startColor = glm::vec4(.5f, .5f, .5f, .01f);
                smoke->config.endColor = glm::vec4(0.05f, 0.05f, 0.05f, .08f);
                //旋轉
                smoke->config.rotationMin = -2.0f;
                smoke->config.rotationMax = 2.0f;
                //重力
                smoke->config.gravity = glm::vec3(0.0f, 0.01f, 0.0f);
                //持续时间
                smoke->config.lifeMin = 1.f;  // 最短存活时间
                smoke->config.lifeMax = 3.0f;  // 最长存活时间
                //粒子尺寸（像素
                smoke->config.sizeMin = 80.0f;
                smoke->config.sizeMax = 200.0f;
                //粒子尺寸缩放：出生时和死亡时的放大倍数
                smoke->config.startSizeScale = 3.0f;
                smoke->config.endSizeScale = 0.f;
                //初始速度
                smoke->config.speedMin = 0.1f; // 最小初速度
                smoke->config.speedMax = 0.5f; // 最大初速度
                //位置
                smoke->config.emitterPos = emitterPos4;;
                smoke->init(mAllocator, 800, emitterPos4);
                allParticles.push_back(std::move(smoke));
            }

            // 創建第 3組：火花
            {
				auto magic = std::make_unique<ParticleSystem>();
                magic->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[1]];// 綁定貼圖
                magic->config.useTexture = 1;
                magic->config.sizeMin = 500.0f;
                magic->config.sizeMax = 500.0f;
                magic->config.emitterPos = emitterPos2;
                magic->init(mAllocator, 1, emitterPos2);
                allParticles.push_back(std::move(magic));
            }

            // 創建第 4組：火焰黑
            {
				auto c = std::make_unique<ParticleSystem>();
                c->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[0]]; // TODO:綁定星星貼圖 这里是错的
                c->config.emitterPos = emitterPos3;
                c->init(mAllocator, 1000, emitterPos3);
                allParticles.push_back(std::move(c));
            }

            // 創建第 五 組：爆炸火焰
            {
                auto boom = std::make_unique<ParticleSystem>();
                //發射器形狀
                boom->setEmitterShape(EmitterShape::Sphere);
                boom->config.sphereRadius = 0.3f;// 控制锥形的开口大小
                //debug
                boom->config.particleDebug = false; // 开启粒子调试输出
                //貼圖設定
                boom->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[0]]; // 綁定貼圖
                boom->config.useTexture = 1;
                boom->config.atlasCols = 4;   // 贴图切成 4 列
                boom->config.atlasRows = 4;   // 贴图切成 4 行
                boom->config.animateAtlas = true;
                //顔色
                boom->config.startColor = glm::vec4(255.0f, 125.8f, 0.3f, .05f);
                boom->config.endColor = glm::vec4(0.05f, 0.05f, 0.05f, 0.1f);
                //旋轉
                boom->config.rotationMin = -360.0f;
                boom->config.rotationMax = 360.0f;
                //重力
                boom->config.gravity = glm::vec3(0.0f, 0.01f, 0.0f);
                //持续时间
                boom->config.lifeMin = 1.f;  // 最短存活时间
                boom->config.lifeMax = 3.0f;  // 最长存活时间
                //粒子尺寸
                boom->config.sizeMin = 50.0f;
                boom->config.sizeMax = 130.0f;
                //粒子尺寸缩放：出生时和死亡时的放大倍数
                boom->config.startSizeScale = 3.0f;
                boom->config.endSizeScale = 0.f;
                //初始速度
                boom->config.speedMin = 0.1f; // 最小初速度
                boom->config.speedMax = 0.5f; // 最大初速度
                //位置
                boom->config.emitterPos = emitterPos1;;
                boom->init(mAllocator, 600, emitterPos1);
                allParticles.push_back(std::move(boom));
            }
            //================particle system===================================================

 

            mSceneUBO = lut::create_buffer(mAllocator,
                sizeof(glsl::SceneUniform),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                0,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

            mSceneDescriptors = lut::alloc_desc_set(mWindow, mDescPool.handle, mSceneLayout.handle);
            {
                VkDescriptorBufferInfo bi{ mSceneUBO.buffer, 0, VK_WHOLE_SIZE };
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = mSceneDescriptors; w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.descriptorCount = 1; w.pBufferInfo = &bi;
                vkUpdateDescriptorSets(mWindow.device, 1, &w, 0, nullptr);
            }

            mAlphaPipe = create_alpha_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mThumbnailAlphaPipe = create_alpha_pipeline_1_attachment(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);

            // p2_1.5 Shadow Resources
            mShadowMap = create_shadow_map(mWindow, mAllocator);
            mShadowSampler = create_shadow_sampler(mWindow);

            // Create cascade views for shadow mapping
            for (uint32_t i = 0; i < count; ++i) {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = mShadowMap.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = cfg::kShadowMapFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = i;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView view = VK_NULL_HANDLE;
                if (auto const res = vkCreateImageView(mWindow.device, &viewInfo, nullptr, &view); VK_SUCCESS != res) {
                    throw lut::Error("Unable to create shadow map cascade view: {}", lut::to_string(res));
                }
                mShadowCascadeViews.emplace_back(view);
            }

            mShadowPipe = create_shadow_pipeline(mWindow, mPipeLayout.handle);

            mDepthBuffer = create_depth_buffer(mWindow, mAllocator);
            mPostProcPipe = create_post_proc_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);
            mOffscreenImage = create_offscreen_buffer(mWindow, mAllocator);
            mVisImage = create_vis_image(mWindow, mAllocator); // p2_1.1
            mPostSampler = create_post_proc_sampler(mWindow);

            // main scene descriptors need shadow map
            // update scene descriptors
            {
                VkDescriptorBufferInfo bi{ mSceneUBO.buffer, 0, VK_WHOLE_SIZE };
                VkDescriptorImageInfo  si{};
                si.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
                si.imageView = mShadowMap.view;
                si.sampler = mShadowSampler.handle;

                VkWriteDescriptorSet w[2]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = mSceneDescriptors; w[0].dstBinding = 0;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w[0].descriptorCount = 1; w[0].pBufferInfo = &bi;

                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = mSceneDescriptors; w[1].dstBinding = 1; // shadow map binding
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[1].descriptorCount = 1; w[1].pImageInfo = &si;

                vkUpdateDescriptorSets(mWindow.device, 2, w, 0, nullptr);
            }

            // mosaic UBOs
            for (std::size_t i = 0; i < mCmdBuffers.size(); ++i) {
                mMosaicUBOs.emplace_back(lut::create_buffer(mAllocator,
                    sizeof(glsl::MosaicUniform),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT));

                mPostDescriptors.emplace_back(
                    BuildPostDesc(mOffscreenImage.view, mMosaicUBOs.back().buffer));

                // p2 1.1: vis descriptors
                // reuse postProcLayout (2 bindings) but passthrough shader only uses binding 0
                mVisDescriptors.emplace_back(
                    BuildPostDesc(mVisImage.view, mMosaicUBOs[i].buffer));
            }

            //for (auto& [path, ds] : particleTextureDict)
                //std::printf("ParticleTex [%s] descSet=%p\n", path.c_str(), (void*)ds);



            // --- 1. 创建专用布局 ---
            mBlurDescLayout = create_blur_descriptor_layout(mWindow);
            mCompDescLayout = create_composite_descriptor_layout(mWindow);



            mCompPipeLayout = create_composite_pipeline_layout(mWindow, mCompDescLayout.handle);
            // --- 2. 创建图像资源 (HDR 格式) ---
            mBrightImage = create_offscreen_buffer(mWindow, mAllocator);
            mBlurTempImage = create_offscreen_buffer(mWindow, mAllocator);
            mFinalBloomImage = create_offscreen_buffer(mWindow, mAllocator);

            // --- 3. 创建管线布局与管线 ---

            mBlurPipeLayout = create_blur_pipeline_layout(mWindow, mBlurDescLayout.handle);
            mBlurPipe = create_blur_pipeline(mWindow, mBlurPipeLayout.handle);
            mCompositePipe = create_composite_pipeline(mWindow, mCompPipeLayout.handle);
            // --- 4. 填充描述符集 ---
            for (size_t i = 0; i < mCmdBuffers.size(); ++i) {
                mBlurHorizDescriptors.push_back(BuildBlurDesc(mBlurDescLayout.handle, mBrightImage.view));
                mBlurVertDescriptors.push_back(BuildBlurDesc(mBlurDescLayout.handle, mBlurTempImage.view));
                mCompositeDescriptors.push_back(BuildCompositeDesc(mOffscreenImage.view, mFinalBloomImage.view, mMosaicUBOs[i].buffer));
            }


            // 在 Init() 里面：
            auto t_start = std::chrono::high_resolution_clock::now();
            auto print_time = [&](const char* name) {
                auto t_now = std::chrono::high_resolution_clock::now();
                float ms = std::chrono::duration<float, std::milli>(t_now - t_start).count();
                std::printf("[Pipeline] %s took %.2f ms\n", name, ms);
                t_start = t_now;
                };

            // 挨个测试：
            mPipe = create_triangle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            print_time("Main Triangle Pipe");

            mBlurPipe = create_blur_pipeline(mWindow, mBlurPipeLayout.handle);
            print_time("Blur Pipe");

            mCompositePipe = create_composite_pipeline(mWindow, mCompPipeLayout.handle);
            print_time("Composite Pipe");

            //===========================UI System================================
            ImGuiRenderer::InitInfo uiInfo{};
            uiInfo.window = mWindow.window;
            uiInfo.instance = mWindow.instance;
            uiInfo.physicalDevice = mWindow.physicalDevice;
            uiInfo.device = mWindow.device;
            uiInfo.queue = mWindow.graphicsQueue;
            uiInfo.queueFamily = mWindow.graphicsFamilyIndex;
            uiInfo.colorFormat = mWindow.swapchainFormat;
            uiInfo.depthFormat = cfg::kDepthFormat;
            uiInfo.imageCount = (uint32_t)mWindow.swapImages.size();

            imguiRenderer.Init(uiInfo);


            //1.为最终view scene port创建一个专用的单层 Image// Create a dedicated single-layer image for the final view scene port
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = mWindow.swapchainFormat;
            imageInfo.extent = { mWindow.swapchainExtent.width, mWindow.swapchainExtent.height, 1 };
            imageInfo.mipLevels = 1; //强制只有 1 层
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

            VkImage rawImage = VK_NULL_HANDLE;
            VmaAllocation rawAlloc = VK_NULL_HANDLE;
            vmaCreateImage(mAllocator.allocator, &imageInfo, &allocInfo, &rawImage, &rawAlloc, nullptr);
            mFinalSceneImg = lut::Image(mAllocator.allocator, rawImage, rawAlloc);

            // 2. 创建配套的单层 ImageView
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = mFinalSceneImg.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = mWindow.swapchainFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1; //强制 1 层
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView rawView = VK_NULL_HANDLE;
            vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
            mFinalSceneView = lut::ImageView(mWindow.device, rawView);

            // 3.注册给 ImGui，拿到 ID
            m_sceneViewportTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, mFinalSceneView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            
//为粒子系统的贴图创建 ImGui 专用的描述符// Create ImGui-specific descriptors for particle system textures
            for (size_t i = 0; i < particleImageViews.size(); ++i) {
                std::string path = cfg::ParticleTextures[i];
                VkImageView view = particleImageViews[i].handle;
                VkDescriptorSet imguiTexId = ImGui_ImplVulkan_AddTexture(
                    mDefaultSampler.handle,
                    view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                );
                particleImGuiTextureDict[path] = imguiTexId;
            }

            // ImGui 贴图 ID 补发给已经建好的粒子！
            for (auto& ps : allParticles) {
                // 通过粒子系统当前绑定的 3D 贴图描述符找到对应的 ImGui 贴图描述符
                for (const auto& pair : particleTextureDict) {
                    if (ps->config.textureDescriptor == pair.second) {
                        ps->config.uiIconDescriptor = particleImGuiTextureDict[pair.first];
                        break;
                    }
                }
            }

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; //开启停靠功能核心开关

            //初始化缩略图管线
            InitThumbnailPipeline();

            //扫描 Assets/Models 生成缩略图
            namespace fs = std::filesystem;
            std::string modelFolder = "Assets/Models";
            if (fs::exists(modelFolder)) {
                for (const auto& entry : fs::directory_iterator(modelFolder)) {
                    if (entry.path().extension() == ".glb") {
                        std::string p = entry.path().string();
                        std::replace(p.begin(), p.end(), '\\', '/');
                        PreloadModelForPreview(p);
                    }
                }
            }
        }
        // 辅助函数：构建模糊阶段的描述符集
        VkDescriptorSet BuildBlurDesc(VkDescriptorSetLayout layout, VkImageView inputView) {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, layout);
            VkDescriptorImageInfo ii{ mPostSampler.handle, inputView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = ds; w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.descriptorCount = 1; w.pImageInfo = &ii;

            vkUpdateDescriptorSets(mWindow.device, 1, &w, 0, nullptr);
            return ds;
        }

        VkDescriptorSet BuildCompositeDesc(VkDescriptorSetLayout layout, VkImageView sceneView, VkImageView bloomView) {
            // 确保直接 alloc 并返回，不要在函数内部操作成员 vector
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, layout);

            VkDescriptorImageInfo imgs[2]{};
            imgs[0] = { mPostSampler.handle, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet w[2]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = ds; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].descriptorCount = 1; w[0].pImageInfo = &imgs[0];

            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = ds; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].descriptorCount = 1; w[1].pImageInfo = &imgs[1];

            vkUpdateDescriptorSets(mWindow.device, 2, w, 0, nullptr);
            return ds;
        }

        void Update(float dt) override
        {
            // Let GLFW process events.
            // glfwPollEvents() checks for events, processes them. If there are no
            // events, it will return immediately. Alternatively, glfwWaitEvents()
            // will wait for any event to occur, process it, and only return at
            // that point. The former is useful for applications where you want to
            // render as fast as possible, whereas the latter is useful for
            // input-driven applications, where redrawing is only needed in
            // reaction to user input (or similar).
            glfwPollEvents(); // or: glfwWaitEvents()

            //===========================UI System================================
            // game over debug
            if (ImGui::IsKeyPressed(ImGuiKey_G))
            {
                mState->isGameOver = !mState->isGameOver; // 切换死亡状态进行测试// Toggle game over state for testing

                if (mState->isGameOver)
                {
                    engine::EngineUi::LogPrintf("Test: Game Over triggered via 'G' key.\n");

                }
                else
                {
                    engine::EngineUi::LogPrintf("Test: Back to Game/Menu.\n");
                }
            }// game over debug
            // game pause debug
            if (ImGui::IsKeyPressed(ImGuiKey_H))
            {
                mState->isGamePause = !mState->isGamePause; // 切换死亡状态进行测试// Toggle game pause state for testing

                if (mState->isGamePause)
                {
                    engine::EngineUi::LogPrintf("Test: Game pause triggered via 'H' key.\n");
                }
                else
                {
                    engine::EngineUi::LogPrintf("Test: Back to Game/Menu.\n");
                }
            }


            // 1. Ctrl + S 保存项目
            // io.KeyCtrl 会在左 Ctrl 或右 Ctrl 按下时为 true
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                // 调用我们刚刚写好的高精度 JSON 保存函数
                EngineUi::SaveProject(mSceneManager, this, "Assets/MySceneSave.json");
                EngineUi::ShowToast("[ Project Saved Successfully ]");
                engine::EngineUi::LogPrintf("Project Saved \n");
            }
			//debug draw box
            //mDebugRenderer.DrawBox(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

            // 

            //启动 ImGui 帧// Start ImGui frame
            imguiRenderer.BeginFrame();

            //铺设全屏底层 DockSpace
            // 必须在绘制任何其他 ImGui 窗口（如 MainMenu, ContentBrowser）之前调用！传 0 表示让 ImGui 自动为我们生成主窗口的 ID
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

            //ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
            //调用顶部菜单栏！
            EngineUi::DrawMainMenuBar(this, mSceneManager, *mState, mAppRunning);
            //start gmae menu
           // 如果游戏还没开始，只画主菜单
            if (!mState->isGameStarted) {
                EngineUi::DrawMainMenu(this, mAppRunning, mState->isGameStarted);
            }
            else if (mState->isGameOver) {
                // gameover UI
                EngineUi::DrawGameOver(this, *mState, mAppRunning);
            }
            else if (mState->isGamePause) {
                // gameover UI 
                EngineUi::DrawGamePause(this, *mState, mAppRunning);
            }
            else
            {
                // Prepare data for this frame
                glsl::SceneUniform sceneUniforms{};
                // 1. 获取真实的 UI 视口大小
                ImVec2 vpSize = EngineUi::GetSceneViewportSize();
                float width = std::abs(vpSize.x);
                float height = std::abs(vpSize.y);

                // 确保高度不为 0
                if (height < 0.1f) height = 0.1f;
                // 2. 替换掉原来的 mWindow.swapchainExtent
                update_scene_uniforms(sceneUniforms,
                    (uint32_t)vpSize.x,  // 使用 UI 宽度
                    (uint32_t)vpSize.y,  // 使用 UI 高度
                    *mState);

                //View 矩阵
                glm::mat4 view = glm::inverse(mState->camera2world);
                //Aspect Ratio
                //float aspect = (float)mWindow.swapchainExtent.width / (float)mWindow.swapchainExtent.height;
                float aspect = width / height;
                //FOV
                float fovRadians = lut::Radians(cfg::kCameraFov).value();
                glm::mat4 gizmoProj = glm::perspective(
                    fovRadians,
                    aspect, // 用算好的 aspect 替换原来的计算
                    cfg::kCameraNear,
                    cfg::kCameraFar
                );
                //3D 场景拖放接收器绘制视口上的拖放目标
                //EngineUi::DrawViewportDropTarget(this, mSceneManager, view, gizmoProj);

                static flecs::entity_t lastSelectedId = 0;
                static uint32_t originalMaterialIdx = 0;

                // debug: 选中更换材质方便观察==============
                //if (mSelectedEntityId != lastSelectedId) {
                //    auto& world = mSceneManager->get_world();

                //    // 1. 恢复材质
                //    if (lastSelectedId != 0) {
                //        flecs::entity lastEntity = world.entity(lastSelectedId);
                //        if (lastEntity.is_alive() && lastEntity.has<MaterialComponent>()) {
                //            lastEntity.set<MaterialComponent>({ originalMaterialIdx });
                //        }
                //    }

                //    // 2. 选中高亮
                //    if (mSelectedEntityId != 0) {
                //        flecs::entity currentEntity = world.entity(mSelectedEntityId);
                //        if (currentEntity.is_alive() && currentEntity.has<MaterialComponent>()) {
                //            const MaterialComponent& matComp = currentEntity.get<MaterialComponent>();

                //            
                //            originalMaterialIdx = matComp.materialIndex;

                //            uint32_t highlightIdx = static_cast<uint32_t>(mMaterialDescriptors.size() - 1);
                //            currentEntity.set<MaterialComponent>({ highlightIdx });
                //        }
                //    }
                //    lastSelectedId = mSelectedEntityId;
                //}
            // debug: 选中更换材质（方便观察==============

                // 获取全局鼠标位置和 Viewport 数据
                ImVec2 mousePosAbs = ImGui::GetMousePos();
                ImVec2 vpPos = EngineUi::GetSceneViewportPos();
                //ImVec2 vpSize = EngineUi::GetSceneViewportSize();

                // 计算出鼠标在 3D 画面内部的“局部坐标”
                float localMouseX = mousePosAbs.x - vpPos.x;
                float localMouseY = mousePosAbs.y - vpPos.y;

                //判断鼠标是不是真的悬停在 3D 画面内部
                bool isMouseInViewport = (localMouseX >= 0.0f && localMouseX <= vpSize.x &&
                    localMouseY >= 0.0f && localMouseY <= vpSize.y);

                //EngineUi::DrawSceneViewport(m_sceneViewportTexId, this, mSceneManager, view, gizmoProj, mSelectedEntityId);
                EngineUi::DrawSceneViewport(m_sceneViewportTexId, this, mSceneManager, view, gizmoProj, mSelectedEntityId, *mState);

                glm::mat4 viewProj = gizmoProj * view;
                glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]); // 提取逆 view 矩阵第 4 列作为位置

                // 给面板加上开关判断：
                if (mState->showControlPanel) {
                    EngineUi::DrawControlPanel(*mState, this, mSceneManager);
                }

                if (mState->showContentBrowser) {
                    EngineUi::DrawContentBrowser(this, mSceneManager);
                }

                if (mState->showSceneHierarchy || mState->showEntityInspector) {
                    EngineUi::DrawSceneHierarchy(this, mSceneManager, view, gizmoProj, mSelectedEntityId, *mState);
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isMouseInViewport && !ImGuizmo::IsOver())
                {
                    flecs::entity hitEntity = MousePicker::PickEntity(
                        localMouseX, localMouseY,  // 传局部鼠标坐标
                        vpSize.x, vpSize.y,        // 传真实的视口大小
                        mState->camera2world, gizmoProj, mSceneManager
                    );

                    if (hitEntity.is_alive()) {
                        mSelectedEntityId = hitEntity.id();
                        mState->activeParticleIndex = -1;
                        engine::EngineUi::LogPrint("[Raycast] Hit Object ID: {}\n", hitEntity.id());
                    }
                    else {
                        engine::EngineUi::LogPrint("[Raycast] Hit Nothing\n");
                        mState->activeParticleIndex = -1;
                        mSelectedEntityId = 0;
                    }
                }


                // EngineUi::DrawSceneHierarchy(mSceneManager);
                if (mSelectedEntityId != 0 && mSceneManager) {
                    auto& world = mSceneManager->get_world();
                    flecs::entity selectedEntity = world.entity(mSelectedEntityId);

                    if (selectedEntity.is_alive() && ImGuizmo::IsUsing()) {
                        const auto& lt = selectedEntity.get<LocalTransform>();
                        auto pb = selectedEntity.get<PhysicsBody>();
                        auto* physics = mSceneManager->get_physics_system();

                        JPH::BodyInterface& bodyInterface = physics->get_body_interface();
                        JPH::BodyID joltBodyID(pb.bodyID);

                        //获取包围盒
                        //get AABB from Jolt
                        JPH::TransformedShape ts = bodyInterface.GetTransformedShape(joltBodyID);
                        JPH::AABox aabb = ts.GetWorldSpaceBounds();
                        JPH::Vec3 size = aabb.GetExtent() * 2.0f;
                        
						//Debug Renderer 画包围盒 draw AABB from Jolt using Debug Renderer==========================
                        glm::vec3 center(aabb.GetCenter().GetX(), aabb.GetCenter().GetY(), aabb.GetCenter().GetZ());
                        glm::vec3 extents(aabb.GetExtent().GetX(), aabb.GetExtent().GetY(), aabb.GetExtent().GetZ());

                        //draw
                        mDebugRenderer.DrawBox(center, extents, glm::vec3(0.0f, 1.0f, 0.0f));
                        // =========================================================================
                        
                        //debug
                        engine::EngineUi::LogPrint("[Physics Debug] Entity: {} | Size: ({:.2f}, {:.2f}, {:.2f})\n",
                            selectedEntity.name() ? selectedEntity.name() : "Unknown",
                            size.GetX(), size.GetY(), size.GetZ());

                        if (physics) {
                            physics->set_body_transform(pb.bodyID, lt.matrix);//transform同步synchronous

                            // SCALE缩放同步
                            glm::vec3 currentScale, translation, skew;
                            glm::quat rotation;
                            glm::vec4 perspective;

                            if (glm::decompose(lt.matrix, currentScale, rotation, translation, skew, perspective)) {

                                static std::unordered_map<flecs::entity_t, glm::vec3> scaleCache;
                                if (scaleCache.find(mSelectedEntityId) == scaleCache.end()) {
                                    scaleCache[mSelectedEntityId] = currentScale;
                                }

                                glm::vec3& lastSyncedScale = scaleCache[mSelectedEntityId];
                                float delta = glm::distance(currentScale, lastSyncedScale);
                                if (delta > 0.001f) {
                                    glm::vec3 safeScale = currentScale;
                                    //negative or zero scale can cause Jolt to break, so clamp it to a small positive value
                                    for (int i = 0; i < 3; ++i) {
                                        if (std::abs(safeScale[i]) < 0.001f) {
                                            safeScale[i] = (safeScale[i] >= 0.0f) ? 0.001f : -0.001f;
                                        }
                                    }

                                    physics->set_body_scale(pb.bodyID, safeScale, translation, rotation);
                                    lastSyncedScale = safeScale;
                                }
                            }

                            if (bodyInterface.GetMotionType(joltBodyID) != JPH::EMotionType::Static) {
                                bodyInterface.SetLinearAndAngularVelocity(
                                    joltBodyID, JPH::Vec3::sZero(), JPH::Vec3::sZero()
                                );
                            }

                            selectedEntity.modified<LocalTransform>();
                        }
                    }
                }

                // //官方 Demo
                // //imguiRenderer.BuildDemoUI();
                // //===========================UI System================================
            }
            //保存成功提示
            EngineUi::DrawToast(dt);

            if (mInputSystem && mInputSystem->IsActionPressed("Quit")) {
                glfwSetWindowShouldClose(mWindow.window, GLFW_TRUE);
            }

            if (mInputSystem->IsActionPressed("BloomToggle")) {
                mState->bloomEnabled = !mState->bloomEnabled;
                std::printf("Bloom Effect: %s\n", mState->bloomEnabled ? "ON" : "OFF");
            }

            if (glfwWindowShouldClose(mWindow.window)) {
                mAppRunning = false;
                //===========================UI System================================
                ImGui::EndFrame(); // 结束 ImGui 帧
                //===========================UI System================================
                return;
            }

            if (mRecreateSwapchain) {
                // We need to destroy several objects, which may still be in use by the GPU
                vkDeviceWaitIdle(mWindow.device);

                // Recreate them
                auto const changes = lut::recreate_swapchain(mWindow);

                if (changes.changedFormat) {
                    mPipe = create_triangle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mAlphaPipe = create_alpha_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mThumbnailAlphaPipe = create_alpha_pipeline_1_attachment(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mMipPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mDepthPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mDerivPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mPostProcPipe = create_post_proc_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);

                    // Recreate (p2_1.1)
                    mOverdrawPipe = create_overdraw_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mOvershadingPipe = create_overshading_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mVisResolvePipe = create_vis_resolve_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);
                }

                if (changes.changedSize) {
                    mDepthBuffer = create_depth_buffer(mWindow, mAllocator);
                    mOffscreenImage = create_offscreen_buffer(mWindow, mAllocator);
                    mVisImage = create_vis_image(mWindow, mAllocator);

                    // 1。创建严格 1 层 Mipmap 的图像，
                    VkImageCreateInfo imageInfo{};
                    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                    imageInfo.imageType = VK_IMAGE_TYPE_2D;
                    imageInfo.format = mWindow.swapchainFormat;
                    imageInfo.extent = { mWindow.swapchainExtent.width, mWindow.swapchainExtent.height, 1 };
                    imageInfo.mipLevels = 1; // 强制只有 1 层！
                    imageInfo.arrayLayers = 1;
                    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                    VmaAllocationCreateInfo allocInfo{};
                    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

                    VkImage rawImage = VK_NULL_HANDLE;
                    VmaAllocation rawAlloc = VK_NULL_HANDLE;
                    vmaCreateImage(mAllocator.allocator, &imageInfo, &allocInfo, &rawImage, &rawAlloc, nullptr);
                    mFinalSceneImg = lut::Image(mAllocator.allocator, rawImage, rawAlloc);

                    // 2. 创建配套的单层 ImageView
                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = mFinalSceneImg.image;
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = mWindow.swapchainFormat;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.baseMipLevel = 0;
                    viewInfo.subresourceRange.levelCount = 1; //强制 1 层
                    viewInfo.subresourceRange.baseArrayLayer = 0;
                    viewInfo.subresourceRange.layerCount = 1;

                    VkImageView rawView = VK_NULL_HANDLE;
                    vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
                    mFinalSceneView = lut::ImageView(mWindow.device, rawView);

                    // 更新 ImGui 的图片绑定
                   /* if (m_sceneViewportTexId) ImGui_ImplVulkan_RemoveTexture(m_sceneViewportTexId);
                    m_sceneViewportTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, mFinalSceneView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);*/
                    if (m_sceneViewportTexId) ImGui_ImplVulkan_RemoveTexture(m_sceneViewportTexId);
                    m_sceneViewportTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, mFinalSceneView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    // ----------------------------------------

                    // =====================================================================
                    // 【新增核心修复】：必须重新创建 Bloom 相关的离屏缓冲 
                    // =====================================================================
                    mBrightImage = create_offscreen_buffer(mWindow, mAllocator);
                    mBlurTempImage = create_offscreen_buffer(mWindow, mAllocator);
                    mFinalBloomImage = create_offscreen_buffer(mWindow, mAllocator);

                    // 更新 Blur 水平和垂直阶段的描述符 (绑定 0 为 inputView)
                    UpdatePostDescImage(mBlurHorizDescriptors, mBrightImage.view);
                    UpdatePostDescImage(mBlurVertDescriptors, mBlurTempImage.view);

                    // 更新 Composite (合成) 阶段的描述符，它需要绑定两张图 (0: 场景图, 1: Bloom图)
                    for (size_t i = 0; i < mCmdBuffers.size(); ++i) {
                        VkDescriptorImageInfo imgs[2]{};
                        imgs[0] = { mPostSampler.handle, mOffscreenImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                        imgs[1] = { mPostSampler.handle, mFinalBloomImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

                        VkWriteDescriptorSet w[2]{};
                        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        w[0].dstSet = mCompositeDescriptors[i];
                        w[0].dstBinding = 0;
                        w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        w[0].descriptorCount = 1;
                        w[0].pImageInfo = &imgs[0];

                        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        w[1].dstSet = mCompositeDescriptors[i];
                        w[1].dstBinding = 1;
                        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        w[1].descriptorCount = 1;
                        w[1].pImageInfo = &imgs[1];

                        vkUpdateDescriptorSets(mWindow.device, 2, w, 0, nullptr);
                    }
                    

                    // Update descriptor set
                    UpdatePostDescImage(mPostDescriptors, mOffscreenImage.view);

                    // p2 1.1: update vis descriptors
                    UpdatePostDescImage(mVisDescriptors, mVisImage.view);
                }

                mRecreateSwapchain = false;
                //===========================UI System================================
                ImGui::EndFrame(); // 结束 ImGui 帧
                //===========================UI System================================
                return;
            }

            // Advance to next frame
            mFrameIndex = (mFrameIndex + 1) % mCmdBuffers.size();

            // Make sure that the frame resources are no longer in use
            if (auto res = vkWaitForFences(mWindow.device, 1,
                &mFrameDone[mFrameIndex].handle, VK_TRUE,
                std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
                throw lut::Error("vkWaitForFences: {}", lut::to_string(res));

            // Acquire next swap chain image
            std::uint32_t imageIndex = 0;
            auto acquireRes = vkAcquireNextImageKHR(
                mWindow.device, mWindow.swapchain,
                std::numeric_limits<std::uint64_t>::max(),
                mImageAvailable[mFrameIndex].handle,
                VK_NULL_HANDLE, &imageIndex);

            if (acquireRes == VK_SUBOPTIMAL_KHR || acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
                mRecreateSwapchain = true;
                mFrameIndex = (mFrameIndex + mCmdBuffers.size() - 1) % mCmdBuffers.size();
                //===========================UI System================================
                ImGui::EndFrame(); // 结束 ImGui 帧
                //===========================UI System================================
                return;
            }
            if (acquireRes != VK_SUCCESS)
                throw lut::Error("vkAcquireNextImageKHR: {}", lut::to_string(acquireRes));

            // Reset fence
            if (auto res = vkResetFences(mWindow.device, 1,
                &mFrameDone[mFrameIndex].handle); VK_SUCCESS != res)
                throw lut::Error("vkResetFences: {}", lut::to_string(res));

            //camera follow
            //find character pos
            if (mSceneManager && mState->thirdPersonMode) {
                
                auto target = mSceneManager->find_entity(player);
                if (target.is_valid() && target.has<WorldTransform>()) {
                    const auto& wt = target.get<WorldTransform>();
                    mState->followTargetPos = glm::vec3(wt.matrix[3]);
					// printf("Follow Target Pos: (%.2f, %.2f, %.2f)\n", mState->followTargetPos.x, mState->followTargetPos.y, mState->followTargetPos.z);
                }
            }

            // --- Toggle Inputs via InputSystem ---
            if (mInputSystem) {
                if (mInputSystem->IsActionPressed("ToggleParticles")) {
                    mState->particlesEnabled = !mState->particlesEnabled;
                    std::printf("Particles: %s\n", mState->particlesEnabled ? "ON" : "OFF");
                }
                if (mInputSystem->IsActionPressed("CameraThirdPersonToggle")) {
                    mState->thirdPersonMode = !mState->thirdPersonMode;
                    std::printf("Camera: %s\n", mState->thirdPersonMode ? "Third Person" : "Free Fly");
                }
                
                // Debug Render Modes
                if (mInputSystem->IsActionPressed("Default")) mState->renderMode = 0;
                if (mInputSystem->IsActionPressed("DebugMipmap")) mState->renderMode = 1;
                if (mInputSystem->IsActionPressed("DebugDepth")) mState->renderMode = 2;
                if (mInputSystem->IsActionPressed("DebugDerivatives")) mState->renderMode = 3;
                if (mInputSystem->IsActionPressed("DebugMosaic")) mState->mosaicEnabled = !mState->mosaicEnabled;
                if (mInputSystem->IsActionPressed("DebugOverdraw")) mState->renderMode = 4;
                if (mInputSystem->IsActionPressed("DebugOvershading")) mState->renderMode = 5;
                if (mInputSystem->IsActionPressed("DebugShadows")) mState->renderMode = 6;
                
                if (mInputSystem->IsActionPressed("PrintCameraPos")) {
                    auto const pos = mState->camera2world[3];
                    std::printf("Camera Pos: %.4f, %.4f, %.4f\n", pos.x, pos.y, pos.z);
                }
            }
            //

            // Update state
            update_user_state(*mState, dt, mInputSystem);

            //// Prepare data for this frame
            //glsl::SceneUniform sceneUniforms{};
            //update_scene_uniforms(sceneUniforms,
            //    mWindow.swapchainExtent.width,
            //    mWindow.swapchainExtent.height,
            //    *mState);
            // Prepare data for this frame
            glsl::SceneUniform sceneUniforms{};

			//重新获取一次 UI 视口尺寸//re-fetch UI viewport size
            ImVec2 finalVpSize = EngineUi::GetSceneViewportSize();
            float finalWidth = std::max(1.0f, std::abs(finalVpSize.x));
            float finalHeight = std::max(1.0f, std::abs(finalVpSize.y));

            update_scene_uniforms(sceneUniforms,
                (uint32_t)finalWidth,
                (uint32_t)finalHeight,
                *mState);

            VkPipeline  currentOpaque = mPipe.handle;
            VkPipeline  currentAlpha = mAlphaPipe.handle;
            auto const* currentDescs = &mMaterialDescriptors;

            // Task 1.4
            // Debug Visualization Pipeline Switching
            // keys 1-4: switch the pipeline used for drawing
            // Sitch the descriptor set to 'debugMaterialDescriptors'
            // because the debug pipeline requires a sampler with anisotropic filtering DISABLED
            // setup.cpp
            switch (mState->renderMode) {
            case 1: // Mode 1: Mipmap Visualization
                // Visualizes texture LOD levels (colored).
                currentOpaque = currentAlpha = mMipPipe.handle;
                currentDescs = &mDebugMaterialDescriptors;
                break;
            case 2: // Mode 2: Depth Visualization
                // Visualizes fragment depth (non-linear grayscale)
                currentOpaque = currentAlpha = mDepthPipe.handle;
                currentDescs = &mDebugMaterialDescriptors;
                break;
            case 3: // Mode 3: Derivatives Visualization
                // Visualizes partial derivatives of depth (dFdx, dFdy)
                currentOpaque = currentAlpha = mDerivPipe.handle;
                currentDescs = &mDebugMaterialDescriptors;
                break;
            case 4: // Mode 4: Overdraw
                currentOpaque = currentAlpha = mOverdrawPipe.handle;
                currentDescs = &mDebugMaterialDescriptors;
                // rendering.cpp binds descriptors (i think)
                break;
            case 5: // Mode 5: Overshading
                currentOpaque = currentAlpha = mOvershadingPipe.handle;
                currentDescs = &mDebugMaterialDescriptors;
                break;
            default:
                break;
            }

            ImageAndView    offscreenTarget;
            VkPipeline      resolvePipeline = mPostProcPipe.handle;
            VkDescriptorSet resolveDescs = mPostDescriptors[mFrameIndex];
            VkPipelineLayout resolveLayout = mPostPipeLayout.handle;
            VkClearColorValue clearColor = { 0.1f, 0.1f, 0.1f, 1.f };

            if (mState->renderMode == 4 || mState->renderMode == 5) {
                // Visualization Mode
                offscreenTarget = { mVisImage.image, mVisImage.view };
                resolvePipeline = mVisResolvePipe.handle;
                resolveDescs = mVisDescriptors[mFrameIndex]; // same layout (postProcPipelineLayout)
                clearColor = { 0.f, 0.1f, 0.f, 1.f }; // dark green
            }
            else {
                // Normal Mode
                offscreenTarget = { mOffscreenImage.image, mOffscreenImage.view };
            }

            
            //================   system===================================================
            if (mState->particlesEnabled)
            {
                for (const auto& ps  : allParticles)
                {
                   
                    if (ps->getEmitterShape() == EmitterShape::Sphere)
                    {

                        auto bat = mSceneManager->find_entity("BaseballBat.nails_0");
                        if (bat.is_valid() && bat.has<LocalTransform>()) 
                        {
                            
                            const LocalTransform& lt = bat.get<LocalTransform>();

                            glm::vec3 localPos = glm::vec3(lt.matrix[3]);
                            ps->update(dt, localPos);
                            ps->upload(mAllocator);
                            ps->uploadDebug(mAllocator, ps->config.emitterPos);
                         }
                    }
                    else 
                    {
                        //直接讀取它自己身上存的位置！
                        ps->update(dt, ps->config.emitterPos);
                        ps->upload(mAllocator);
                        ps->uploadDebug(mAllocator, ps->config.emitterPos);
                    }

                }
            }
            
            //================particle system===================================================

            // update mosaic ubo
            {
                glsl::MosaicUniform mu{ mState->mosaicEnabled ? 1 : 0, {} };
                void* ptr;
                vmaMapMemory(mAllocator.allocator, mMosaicUBOs[mFrameIndex].allocation, &ptr);
                std::memcpy(ptr, &mu, sizeof(mu));
                vmaUnmapMemory(mAllocator.allocator, mMosaicUBOs[mFrameIndex].allocation);
            }

            ImageAndView colorTarget = { mWindow.swapImages[imageIndex], mWindow.swapViews[imageIndex] };
            ImageAndView depthTarget = { mDepthBuffer.image, mDepthBuffer.view };
            ImageAndView shadowTarget = { mShadowMap.image,   mShadowMap.view };

            ImageAndView finalSceneTarget = { mFinalSceneImg.image, mFinalSceneView.handle };
            // =========================================================
            //UI system 拖拽
            // =========================================================
            // 1. 获取正常场景里的所有实体渲染批次
            std::vector<RenderBatch> finalBatches = mSceneManager ? mSceneManager->get_render_batches() : std::vector<RenderBatch>{};

            // 2. 如果正在拖拽预览，把预览的 Batch 强行加进列表最后面！
            if (!m_previewModelPath.empty() && m_previewPrefabCache.count(m_previewModelPath)) {
                for (const auto& originalBatch : m_previewPrefabCache[m_previewModelPath]) {
                    RenderBatch previewBatch = originalBatch;
                    // 用鼠标的矩阵 * 模型部件自身的原始偏移矩阵
                    previewBatch.transform = m_previewTransform * originalBatch.transform;
                    finalBatches.push_back(previewBatch);
                }
            }
            // =========================================================
            std::vector<engine::GpuLight> lights;
            if (mSceneManager) {
                mSceneManager->get_light_data(lights);
                sceneUniforms.lightCount = static_cast<uint32_t>(lights.size());
                for (size_t i = 0; i < lights.size() && i < 16; ++i) {
                    sceneUniforms.lights[i] = lights[i];
                }
            }
            // --- 【新增】：为车头灯计算独有的 Shadow 矩阵，并塞进第 4 个槽位 (索引 3) ---
            for (size_t i = 0; i < sceneUniforms.lightCount; ++i) {
                if (sceneUniforms.lights[i].position.w == 2.0f) { // 2.0 代表聚光灯
                    // 我们之前把 outerCutOff 的 cos 值存在了 params.y，现在反算回角度
                    float outerCutOff = glm::degrees(glm::acos(sceneUniforms.lights[i].params.y));

                    sceneUniforms.lightVP[3] = engine::compute_spotlight_matrix(
                        glm::vec3(sceneUniforms.lights[i].position), // 光源位置
                        glm::vec3(sceneUniforms.lights[i].direction), // 光源朝向
                        outerCutOff,                                 // 外锥角
                        sceneUniforms.lights[i].direction.w          // 范围 (Range)
                    );
                    break; // 假设场景目前只有一个车灯投射阴影
                }
            }

            // 1. 在提交命令前，把这一帧收集的线上传到 GPU
            mDebugRenderer.Upload(mAllocator);

            float currentBloomStrength = mState->bloomEnabled ? 0.0f : 1.2f;
            // Record and submit commands for this frame
            // 在 Update 函数末尾找到 record_commands 调用，修改如下：
            record_commands(
                mCmdBuffers[mFrameIndex],
                currentOpaque,
                currentAlpha,
                colorTarget,           // 现在的 Swapchain 目标
                depthTarget,
                mWindow.swapchainExtent,
                mSceneUBO.buffer,
                sceneUniforms,
                mPipeLayout.handle,
                mSceneDescriptors,
                mMeshPositions,
                mMeshTexCoords,
                mMeshNormals,
                mMeshIndices,
                mModel.meshes,
                mModel.materials,
                *currentDescs,
                finalBatches,
                //mSceneManager ? mSceneManager->get_render_batches() : std::vector<RenderBatch>{},
                // --- 新增 Bloom 参数 (必须与 rendering.cpp 顺序一致) ---
                mBlurPipe.handle,              // VkPipeline aBlurPipe
                mBlurPipeLayout.handle,        // VkPipelineLayout aBlurLayout
                mCompositePipe.handle,         // VkPipeline aCompositePipe
                mCompPipeLayout.handle,
                mBlurHorizDescriptors[mFrameIndex], // VkDescriptorSet aBlurHorizDS
                mBlurVertDescriptors[mFrameIndex],  // VkDescriptorSet aBlurVertDS
                mCompositeDescriptors[mFrameIndex], // VkDescriptorSet aCompositeDS
                ImageAndView{ mOffscreenImage.image, mOffscreenImage.view },
                ImageAndView{ mBrightImage.image, mBrightImage.view },
                ImageAndView{ mBlurTempImage.image, mBlurTempImage.view },
                ImageAndView{ mFinalBloomImage.image, mFinalBloomImage.view },
                finalSceneTarget,
                clearColor,                    // VkClearColorValue aClearColor
                currentBloomStrength,
                // --- 剩下的原有参数 ---
                mPostProcPipe.handle,          // 这里的顺序要核对你的 rendering.cpp
                mPostDescriptors[mFrameIndex],
                mPostPipeLayout.handle,
                mShadowPipe.handle,
                shadowTarget,
                mShadowCascadeViews,
                mState->particlesEnabled&& mState->renderMode == 0,
                mParticlePipe.handle,
                allParticles,
                mDebugLinePipe.handle,
                mDebugRenderer
            );
			//clear debug renderer data after uploading
            mDebugRenderer.Clear();

            submit_commands(mWindow,
                mCmdBuffers[mFrameIndex],
                mFrameDone[mFrameIndex].handle,
                mImageAvailable[mFrameIndex].handle,
                mRenderFinished[imageIndex].handle);

            present_results(mWindow.presentQueue, mWindow.swapchain,
                imageIndex, mRenderFinished[imageIndex].handle,
                mRecreateSwapchain);
        }

        void Shutdown() override
        {
            // 1. 确保 GPU 已经完全停下，再开始拆除资源
            vkDeviceWaitIdle(mWindow.device);

            // (Removed redundant RAII wrapper destructions)

            // ================= 原有逻辑保持不变 =================
            for (auto view : mShadowCascadeViews) {
                vkDestroyImageView(mWindow.device, view, nullptr);
            }

            allParticles.clear();

            ::imguiRenderer.Shutdown();
            mDebugRenderer.Shutdown(mAllocator);
        }

        // add runtime-generated mesh (like the sphere) after Init()
        // returns the mesh index to pass to SceneManager::create_dynamic_entity()
        uint32_t add_runtime_mesh(const EngineMesh& mesh)
        {
            // Push mesh data into the model (for index lookups in rendering)
            mModel.meshes.emplace_back(mesh);
            uint32_t meshIdx = static_cast<uint32_t>(mModel.meshes.size() - 1);

            // Upload the new mesh to GPU
            UploadSingleMesh(mesh);

            // add ONE gray descriptor set for this new mesh
            AddOneGrayDescriptor(mDefaultSampler.handle, mMaterialDescriptors);
            AddOneGrayDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors);


            // Track the material index for the caller
            mRuntimeMatIndex = static_cast<uint32_t>(mMaterialDescriptors.size() - 1);
            return meshIdx;
        }

        // add an entire model file to the renderer and physics scene
        void load_additional_model(const char* path, bool isStatic, float mass = 1.0f, const glm::mat4& initialTransform = glm::mat4(1.0f), bool isCompound = false, bool isC = false)
        {
            EngineModel newModel = load_engine_model_glb(path);
            uint32_t baseTextureIdx = static_cast<uint32_t>(mModelTextures.size());
            uint32_t baseMaterialIdx = static_cast<uint32_t>(mModel.materials.size());
            uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

            // 1. Appends textures
            for (auto const& tex : newModel.textures) {



                glfwPollEvents();
                VkFormat fmt = (tex.space == ETextureSpace::srgb) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(tex.pixels.data(), tex.width, tex.height, mWindow, mCmdPool.handle, mAllocator, fmt));
                mModelTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, mModelTextures.back().image, fmt));
            }

            for (size_t i = 0; i < newModel.textures.size(); ++i) {
                std::printf("[Tex %zu] name='%s' %dx%d\n",
                    i + baseTextureIdx,
                    newModel.textures[i].name.c_str(),
                    newModel.textures[i].width,
                    newModel.textures[i].height);
            }

            // 2. Append materials (fixing texture references)
            for (auto mat : newModel.materials) {
                if (mat.baseColorTexture >= 0) mat.baseColorTexture += baseTextureIdx;
                if (mat.normalTexture >= 0) mat.normalTexture += baseTextureIdx;
                if (mat.metalRoughTexture >= 0) mat.metalRoughTexture += baseTextureIdx;
                if (mat.occlusionTexture >= 0) mat.occlusionTexture += baseTextureIdx;
                if (mat.emissiveTexture >= 0) mat.emissiveTexture += baseTextureIdx;
                if (mat.alphaMaskTexture >= 0) mat.alphaMaskTexture += baseTextureIdx;
                
                mModel.materials.push_back(mat);
                
                // Add descriptors for new materials
                AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, mat);
                AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, mat);
            }

            std::printf("[LoadModel] %s -> baseMat=%u, added %zu mats, total mats=%zu, total descs=%zu\n",
                path, baseMaterialIdx,
                newModel.materials.size(),
                mModel.materials.size(),
                mMaterialDescriptors.size());

            // 3. Append meshes (fixing material references)
            for (auto mesh : newModel.meshes) {
                mesh.materialIndex += baseMaterialIdx;
                mModel.meshes.push_back(mesh);
                UploadSingleMesh(mesh);
                  
            }



            // 4.1 apply initial transform and create entities via SceneManager, using local mesh indices to build physics.
            for (auto& instance : newModel.scenes) {
                instance.transform = initialTransform * instance.transform;
            }
            
            // 4.2 update the scenes to use global mesh indices for the renderer.
            if (mSceneManager) {
                if (isCompound) {
                    mSceneManager->load_compound_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
				}
                else if (isC)
                {
                    mSceneManager->load_C_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
                }
                else if (isStatic) {
                    mSceneManager->load_static_model(newModel, baseMeshIdx, baseMaterialIdx);
                } else {
                    mSceneManager->load_dynamic_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
                }
            }


            // 5. Update model scenes (fixing mesh references for the global renderer array)
            for (auto& instance : newModel.scenes) {
                instance.meshIndex += baseMeshIdx;
                mModel.scenes.push_back(instance);
            }
        }

        // returns the material descriptor index assigned to the last add_runtime_mesh() call
        uint32_t get_runtime_mat_index() const { return mRuntimeMatIndex; }

        uint32_t get_material_count() const
        {
            return static_cast<uint32_t>(mModel.materials.size());
        }

        // Allow application to pass in the input system
        void SetInputSystem(engine::InputSystem* sys) { mInputSystem = sys; }

        void SetUserState(UserState* state) { this->mState = state; }

    private:

        UserState* mState = nullptr;

        engine::InputSystem* mInputSystem = nullptr;

        // (Bloom/Composite members moved below mAllocator for correct RAII destruction order)
        
        VkDescriptorSet BuildBlurDesc(VkImageView inputView) {
            // 模糊阶段只需要 1 个输入纹理 (binding 0)
            // 我们可以复用 mPostLayout，但如果报错，建议创建一个专用的单纹理 Layout
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mPostLayout.handle);

            VkDescriptorImageInfo ii{};
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii.imageView = inputView;
            ii.sampler = mPostSampler.handle;

            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = ds;
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.descriptorCount = 1;
            w.pImageInfo = &ii;

            vkUpdateDescriptorSets(mWindow.device, 1, &w, 0, nullptr);
            return ds;
        }

        VkDescriptorSet BuildCompositeDesc(VkImageView sceneView, VkImageView bloomView, VkBuffer mosaicUbo) {
            // The compositing stage requires 2 input textures (binding 0: scene, binding 1: blur result) + 1 UniformBuffer (binding 2: Mosaic)
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mCompDescLayout.handle);

            VkDescriptorImageInfo imgs[2]{};
            imgs[0] = { mPostSampler.handle, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo bi{};
            bi.buffer = mosaicUbo;
            bi.offset = 0;
            bi.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet w[3]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = ds; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].descriptorCount = 1; w[0].pImageInfo = &imgs[0];

            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = ds; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[1].descriptorCount = 1; w[1].pImageInfo = &imgs[1];

            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = ds; w[2].dstBinding = 2;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[2].descriptorCount = 1; w[2].pBufferInfo = &bi;

            vkUpdateDescriptorSets(mWindow.device, 3, w, 0, nullptr);
            return ds;
        }
        void AddOneMaterialDescriptor(VkSampler sampler, std::vector<VkDescriptorSet>& out, const EngineMaterial& mat)
        {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mObjectLayout.handle);

            VkImageView baseView = mDefaultGrayView.handle;
            if (mat.baseColorTexture >= 0) baseView = mModelTextureViews[mat.baseColorTexture].handle;

            VkImageView mrView = mDefaultGrayView.handle;
            if (mat.metalRoughTexture >= 0) mrView = mModelTextureViews[mat.metalRoughTexture].handle;

            VkImageView normView = mDefaultNormalView.handle;
            if (mat.normalTexture >= 0) normView = mModelTextureViews[mat.normalTexture].handle;

            // --- 【关键1：提取自发光图】 ---
            VkImageView emissiveView = mDefaultBlackView.handle;
            if (mat.emissiveTexture >= 0) emissiveView = mModelTextureViews[mat.emissiveTexture].handle;

            // --- 【关键2：数组大小必须是 4 ！！！】 ---
            VkDescriptorImageInfo imgs[4]{};
            imgs[0] = { sampler, baseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { sampler, mrView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { sampler, normView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[3] = { sampler, emissiveView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // 绑定第 4 张

            VkWriteDescriptorSet w[4]{}; // --- 【关键3：数组大小必须是 4 ！！！】 ---
            for (int j = 0; j < 4; ++j) { // --- 【关键4：循环条件改成 j < 4 ！！！】 ---
                w[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[j].dstSet = ds; w[j].dstBinding = (uint32_t)j;
                w[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[j].descriptorCount = 1; w[j].pImageInfo = &imgs[j];
            }

            // --- 【关键5：更新数量改成 4 ！！！】 ---
            vkUpdateDescriptorSets(mWindow.device, 4, w, 0, nullptr);
            out.emplace_back(ds);
        }

        // allocate exactly one gray descriptor set (for runtime meshes)
        void AddOneGrayDescriptor(VkSampler sampler, std::vector<VkDescriptorSet>& out)
        {
            VkDescriptorSet ds = lut::alloc_desc_set(
                mWindow, mDescPool.handle, mObjectLayout.handle);

            VkImageView grayView = mDefaultGrayView.handle;
            VkDescriptorImageInfo imgs[3]{};
            imgs[0] = { sampler, grayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { sampler, grayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { sampler, mDefaultNormalView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet w[3]{};
            for (int j = 0; j < 3; ++j) {
                w[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[j].dstSet = ds; w[j].dstBinding = (uint32_t)j;
                w[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[j].descriptorCount = 1; w[j].pImageInfo = &imgs[j];
            }
            vkUpdateDescriptorSets(mWindow.device, 3, w, 0, nullptr);
            out.emplace_back(ds);
        }

        // upload a single mesh to GPU, appending to the mesh buffer vectors
        void UploadSingleMesh(const EngineMesh& mesh)
        {
            VkCommandBuffer uploadCmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(uploadCmd, &bi);

            VkDeviceSize posSz  = mesh.positions.size() * sizeof(glm::vec3);
            VkDeviceSize texSz  = mesh.texcoords.size() * sizeof(glm::vec2);
            VkDeviceSize normSz = mesh.normals.size()   * sizeof(glm::vec3);
            VkDeviceSize idxSz  = mesh.indices.size()   * sizeof(std::uint32_t);

            auto mkGpu = [&](VkDeviceSize sz, VkBufferUsageFlags usage) {
                return lut::create_buffer(mAllocator, sz,
                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
            };
            mMeshPositions.emplace_back(mkGpu(posSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            mMeshTexCoords.emplace_back(mkGpu(texSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            mMeshNormals.emplace_back(mkGpu(normSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            mMeshIndices.emplace_back(mkGpu(idxSz, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

            auto mkStg = [&](VkDeviceSize sz) {
                return lut::create_buffer(mAllocator, sz,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            };
            lut::Buffer ps = mkStg(posSz), ts = mkStg(texSz),
                        ns = mkStg(normSz), is = mkStg(idxSz);

            auto up = [&](lut::Buffer& b, const void* src, VkDeviceSize sz) {
                void* ptr;
                vmaMapMemory(mAllocator.allocator, b.allocation, &ptr);
                std::memcpy(ptr, src, static_cast<std::size_t>(sz));
                vmaUnmapMemory(mAllocator.allocator, b.allocation);
            };
            up(ps, mesh.positions.data(), posSz);
            up(ts, mesh.texcoords.data(), texSz);
            up(ns, mesh.normals.data(),   normSz);
            up(is, mesh.indices.data(),   idxSz);

            auto cpy = [&](lut::Buffer& src, lut::Buffer& dst, VkDeviceSize sz) {
                VkBufferCopy c{ 0, 0, sz };
                vkCmdCopyBuffer(uploadCmd, src.buffer, dst.buffer, 1, &c);
            };
            cpy(ps, mMeshPositions.back(), posSz);
            cpy(ts, mMeshTexCoords.back(), texSz);
            cpy(ns, mMeshNormals.back(),   normSz);
            cpy(is, mMeshIndices.back(),   idxSz);

            lut::buffer_barrier(uploadCmd, mMeshPositions.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, mMeshTexCoords.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, mMeshNormals.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, mMeshIndices.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);

            vkEndCommandBuffer(uploadCmd);

            VkCommandBufferSubmitInfo ci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            ci.commandBuffer = uploadCmd;
            VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            si.commandBufferInfoCount = 1; si.pCommandBufferInfos = &ci;
            vkQueueSubmit2(mWindow.graphicsQueue, 1, &si, VK_NULL_HANDLE);

            vkQueueWaitIdle(mWindow.graphicsQueue);

            glfwPollEvents(); // 保持窗口心跳
        } // 函数结束，旧的 ps, ts 等变成空壳被安全销毁，真正的显存已经归 vector 管了

        VkDescriptorSet BuildPostDesc(VkImageView imageView, VkBuffer mosaicBuf)
        {
            VkDescriptorSet ds = lut::alloc_desc_set(
                mWindow, mDescPool.handle, mPostLayout.handle);

            VkDescriptorImageInfo ii{};
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii.imageView = imageView;
            ii.sampler = mPostSampler.handle;

            VkDescriptorBufferInfo bi{ mosaicBuf, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet w[2]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = ds; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[0].descriptorCount = 1; w[0].pImageInfo = &ii;

            // Mosaic UBO Binding
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = ds; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w[1].descriptorCount = 1; w[1].pBufferInfo = &bi;

            vkUpdateDescriptorSets(mWindow.device, 2, w, 0, nullptr);
            return ds;
        }

        void UpdatePostDescImage(std::vector<VkDescriptorSet>& sets, VkImageView newView)
        {
            VkDescriptorImageInfo ii{};
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii.imageView = newView;
            ii.sampler = mPostSampler.handle;

            for (auto ds : sets) {
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = ds; w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.descriptorCount = 1; w.pImageInfo = &ii;
                vkUpdateDescriptorSets(mWindow.device, 1, &w, 0, nullptr);
            }
        }

        //UserState  mState{};

        bool       mRecreateSwapchain = false;
        std::size_t mFrameIndex = 0;

        lut::CommandPool    mCmdPool;
        lut::DescriptorPool mDescPool;
        lut::DescriptorPool mParticleDescPool;

        std::vector<VkCommandBuffer>  mCmdBuffers;
        std::vector<lut::Fence>       mFrameDone;
        std::vector<lut::Semaphore>   mImageAvailable;
        std::vector<lut::Semaphore>   mRenderFinished;

        lut::DescriptorSetLayout mSceneLayout, mObjectLayout, mPostLayout;
        lut::PipelineLayout      mPipeLayout, mPostPipeLayout;

        lut::Pipeline mPipe, mAlphaPipe;
        lut::Pipeline mMipPipe, mDepthPipe, mDerivPipe;
        lut::Pipeline mOverdrawPipe, mOvershadingPipe;
        lut::Pipeline mPostProcPipe, mVisResolvePipe;
        lut::Pipeline mShadowPipe;
        lut::Pipeline mParticlePipe;


        EngineModel                    mModel;
        std::vector<lut::Image>        mModelTextures;
        std::vector<lut::ImageView>    mModelTextureViews;

        lut::Image     mDefaultGrayTex;
        lut::ImageView mDefaultGrayView;
        lut::Image     mDefaultBlackTex;
        lut::ImageView mDefaultBlackView;

        lut::Image     mDefaultNormalTex;  // 【新增】：正确的法线占位图
        lut::ImageView mDefaultNormalView; // 【新增】

        lut::Pipeline mThumbnailAlphaPipe;

        // Samplers
        lut::Sampler mDefaultSampler, mDebugSampler;
        lut::Sampler mPostSampler, mShadowSampler;

        // Mesh GPU buffers
        std::vector<lut::Buffer> mMeshPositions;
        std::vector<lut::Buffer> mMeshTexCoords;
        std::vector<lut::Buffer> mMeshNormals;
        std::vector<lut::Buffer> mMeshIndices;

        // UBOs
        lut::Buffer              mSceneUBO;
        std::vector<lut::Buffer> mMosaicUBOs;

        // Descriptor sets
        VkDescriptorSet                mSceneDescriptors = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet>   mMaterialDescriptors;
        std::vector<VkDescriptorSet>   mDebugMaterialDescriptors;
        std::vector<VkDescriptorSet>   mPostDescriptors;
        std::vector<VkDescriptorSet>   mVisDescriptors;

        // Render targets
        lut::ImageWithView mDepthBuffer;
        lut::ImageWithView mOffscreenImage;
        lut::ImageWithView mVisImage;
        lut::ImageWithView mShadowMap;
        std::vector<VkImageView> mShadowCascadeViews;

        // Particles
        std::vector<std::unique_ptr<ParticleSystem>> allParticles;
        std::vector<lut::Image> particleImages;
        std::vector<lut::ImageView> particleImageViews;
        std::unordered_map<std::string, VkDescriptorSet> particleTextureDict;

        //===========================UI System================================
        // UI System 保存当前选中的实体 ID saved selected entity ID for UI system
        flecs::entity_t mSelectedEntityId = 0;
        //===========================UI System================================

        // 最终 3D 画面相纸
        lut::Image mFinalSceneImg;
        lut::ImageView mFinalSceneView;
        // 给 ImGui 用的 UI 贴纸 ID
        VkDescriptorSet m_sceneViewportTexId = VK_NULL_HANDLE;

        // UI System 预览专用的照片和描述符集
        struct ThumbnailAsset {
            lut::Image image;
            lut::ImageView view;
            VkDescriptorSet guiSet;
        };

       
        lut::Pipeline mDebugLinePipe;

        // Index of most recently added runtime mesh's material descriptor
        uint32_t mRuntimeMatIndex = 0;

        //
        // Bloom/Composite Handle Members
        //
        // inserted here (bottom of class) so they correctly destruct BEFORE mAllocator
        lut::ImageWithView mBrightImage;
        lut::ImageWithView mBlurTempImage;
        lut::ImageWithView mFinalBloomImage;

        lut::Pipeline mBlurPipe;
        lut::Pipeline mCompositePipe;
        lut::PipelineLayout mBlurPipeLayout;
        lut::PipelineLayout mCompPipeLayout;

        lut::DescriptorSetLayout mBlurDescLayout;
        lut::DescriptorSetLayout mCompDescLayout;

        std::vector<VkDescriptorSet> mBlurHorizDescriptors;
        std::vector<VkDescriptorSet> mBlurVertDescriptors;
        std::vector<VkDescriptorSet> mCompositeDescriptors;
        private:
            // 存储每个模型专属的照片
            std::unordered_map<std::string, ThumbnailAsset> mThumbnailAssets;

            // 共用的深度缓冲
            lut::Image mThumbnailDepthImg;
            lut::ImageView mThumbnailDepthView;

            // 缓存渲染批次
            std::unordered_map<std::string, std::vector<RenderBatch>> m_previewPrefabCache;

            // 预览状态
            std::string m_previewModelPath = "";
            glm::mat4   m_previewTransform = glm::mat4(1.0f);


            void InitThumbnailPipeline();
            void GenerateModelThumbnail(const std::string& modelPath);
            void PreloadModelForPreview(const std::string& path);


    public:

        VkDescriptorSet GetModelThumbnail(const std::string& modelPath);
        void SetModelPreview(const std::string& path, const glm::mat4& transform);
        void ClearModelPreview();
        DebugRenderer mDebugRenderer;
    
    };


} // namespace engine