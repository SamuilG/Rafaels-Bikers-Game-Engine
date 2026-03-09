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
        explicit RenderSystem(bool& appRunning, SceneManager* sceneManager = nullptr)
            : mAppRunning(appRunning), mSceneManager(sceneManager) {
        }


        void Init() override
        {
            // Create Vulkan Window
            mWindow = lut::make_vulkan_window();

            // Initialize state
            glfwSetWindowUserPointer(mWindow.window, &mState);
            glfwSetKeyCallback(mWindow.window, &glfw_callback_key_press);
            glfwSetMouseButtonCallback(mWindow.window, &glfw_callback_button);
            glfwSetCursorPosCallback(mWindow.window, &glfw_callback_motion);

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
            mOverdrawPipe = create_overdraw_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
            mOvershadingPipe = create_overshading_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
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
            mState.camera2world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 10.0f));

            {
                // just for objects without texture to set a default texture
                // RGBA: 128, 128, 128, 255 (grey)
                std::uint8_t grey[4] = { 128, 128, 128, 255 };

                // uploda 1x1 pixel to GPU
                mDefaultGrayTex = lut::load_image_texture2d_from_memory(
                    grey, 1, 1,
                    mWindow, mCmdPool.handle, mAllocator,
                    VK_FORMAT_R8G8B8A8_UNORM);

                // create grey imageview
                mDefaultGrayView = lut::create_image_view_texture2d(
                    mWindow, mDefaultGrayTex.image, VK_FORMAT_R8G8B8A8_UNORM);


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

            // p2_1.5 Shadow Resources
            mShadowMap = create_shadow_map(mWindow, mAllocator);
            mShadowSampler = create_shadow_sampler(mWindow);

            // Create cascade views for shadow mapping
            for (uint32_t i = 0; i < kCascadeCount; ++i) {
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

            if (glfwWindowShouldClose(mWindow.window)) {
                mAppRunning = false;
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
                    mMipPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mDepthPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mDerivPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
                    mPostProcPipe = create_post_proc_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);

                    // Recreate (p2_1.1)
                    mOverdrawPipe = create_overdraw_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
                    mOvershadingPipe = create_overshading_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
                    mVisResolvePipe = create_vis_resolve_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);
                }

                if (changes.changedSize) {
                    mDepthBuffer = create_depth_buffer(mWindow, mAllocator);
                    mOffscreenImage = create_offscreen_buffer(mWindow, mAllocator);
                    mVisImage = create_vis_image(mWindow, mAllocator);

                    // Update descriptor set
                    UpdatePostDescImage(mPostDescriptors, mOffscreenImage.view);

                    // p2 1.1: update vis descriptors
                    UpdatePostDescImage(mVisDescriptors, mVisImage.view);
                }

                mRecreateSwapchain = false;
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
                return;
            }
            if (acquireRes != VK_SUCCESS)
                throw lut::Error("vkAcquireNextImageKHR: {}", lut::to_string(acquireRes));

            // Reset fence
            if (auto res = vkResetFences(mWindow.device, 1,
                &mFrameDone[mFrameIndex].handle); VK_SUCCESS != res)
                throw lut::Error("vkResetFences: {}", lut::to_string(res));

            // Update state
            update_user_state(mState, dt);

            // Prepare data for this frame
            glsl::SceneUniform sceneUniforms{};
            update_scene_uniforms(sceneUniforms,
                mWindow.swapchainExtent.width,
                mWindow.swapchainExtent.height,
                mState);

            VkPipeline  currentOpaque = mPipe.handle;
            VkPipeline  currentAlpha = mAlphaPipe.handle;
            auto const* currentDescs = &mMaterialDescriptors;

            // Task 1.4
            // Debug Visualization Pipeline Switching
            // keys 1-4: switch the pipeline used for drawing
            // Sitch the descriptor set to 'debugMaterialDescriptors'
            // because the debug pipeline requires a sampler with anisotropic filtering DISABLED
            // setup.cpp
            switch (mState.renderMode) {
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

            if (mState.renderMode == 4 || mState.renderMode == 5) {
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
            if (mState.particlesEnabled)
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
                glsl::MosaicUniform mu{ mState.mosaicEnabled ? 1 : 0, {} };
                void* ptr;
                vmaMapMemory(mAllocator.allocator, mMosaicUBOs[mFrameIndex].allocation, &ptr);
                std::memcpy(ptr, &mu, sizeof(mu));
                vmaUnmapMemory(mAllocator.allocator, mMosaicUBOs[mFrameIndex].allocation);
            }

            ImageAndView colorTarget = { mWindow.swapImages[imageIndex], mWindow.swapViews[imageIndex] };
            ImageAndView depthTarget = { mDepthBuffer.image, mDepthBuffer.view };
            ImageAndView shadowTarget = { mShadowMap.image,   mShadowMap.view };
            std::vector<engine::GpuLight> lights;
            if (mSceneManager) {
                mSceneManager->get_light_data(lights);
                sceneUniforms.lightCount = static_cast<uint32_t>(lights.size());
                for (size_t i = 0; i < lights.size() && i < 16; ++i) {
                    sceneUniforms.lights[i] = lights[i];
                }
            }
            // Record and submit commands for this frame
            record_commands(
                mCmdBuffers[mFrameIndex],
                currentOpaque, currentAlpha,
                colorTarget, depthTarget,
                mWindow.swapchainExtent,
                mSceneUBO.buffer, sceneUniforms,
                mPipeLayout.handle, mSceneDescriptors,
                mMeshPositions, mMeshTexCoords, mMeshNormals, mMeshIndices,
                mModel.meshes, mModel.materials,
                *currentDescs,
                mSceneManager ? mSceneManager->get_render_batches() : std::vector<RenderBatch>{},
                resolvePipeline, resolveDescs, resolveLayout,
                offscreenTarget, clearColor,
                mShadowPipe.handle, shadowTarget,
                mShadowCascadeViews,
                mState.particlesEnabled&& mState.renderMode == 0,
                mParticlePipe.handle,
                allParticles
            );

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
            for (auto view : mShadowCascadeViews) {
                vkDestroyImageView(mWindow.device, view, nullptr);
            }


            vkDeviceWaitIdle(mWindow.device);

            allParticles.clear();
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
        void load_additional_model(const char* path, bool isStatic, float mass = 1.0f, const glm::mat4& initialTransform = glm::mat4(1.0f))
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
                if (isStatic) {
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

    private:
        void AddOneMaterialDescriptor(VkSampler sampler, std::vector<VkDescriptorSet>& out, const EngineMaterial& mat)
        {



            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mObjectLayout.handle);

            VkImageView baseView = mDefaultGrayView.handle;
            if (mat.baseColorTexture >= 0) baseView = mModelTextureViews[mat.baseColorTexture].handle;

            VkImageView mrView = mDefaultGrayView.handle;
            if (mat.metalRoughTexture >= 0) mrView = mModelTextureViews[mat.metalRoughTexture].handle;

			//exract normal map
            VkImageView normView = mDefaultNormalView.handle;
            if (mat.normalTexture >= 0) normView = mModelTextureViews[mat.normalTexture].handle;

            VkDescriptorImageInfo imgs[3]{};
            imgs[0] = { sampler, baseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { sampler, mrView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            //bind normal map
            imgs[2] = { sampler, normView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

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
        }

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

        bool& mAppRunning;
        SceneManager* mSceneManager;

        lut::VulkanWindow  mWindow;
        lut::Allocator     mAllocator;

        UserState  mState{};
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

        lut::Image     mDefaultNormalTex;  // 【新增】：正确的法线占位图
        lut::ImageView mDefaultNormalView; // 【新增】

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

        // Index of most recently added runtime mesh's material descriptor
        uint32_t mRuntimeMatIndex = 0;
    };

} // namespace engine