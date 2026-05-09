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
#include <chrono>
#include <functional>
#include <string_view>
#include <array>
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


// ================= UI System =================
#include "../UI/ui.hpp"
#include "../UI/EngineUi.hpp"
#include "../UI/GameUi.hpp"
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
#include "../UI/RenderSystemUiEditor.hpp"
#include "../UI/VisualUIEditor/RuntimeUiController.hpp"
// ================= debug =================
#include "../Debug/DebugRenderer.hpp"
#include "../Trigger/trigger.hpp"
#include "../Debug/PhysicsDebugDraw.hpp"
#include "../Animation/AnimationSystem.hpp"
#include <unordered_map>
#include "RenderUtilities/frustum.hpp"
#include "RenderUtilities/SSAO.hpp"
namespace glsl {
    struct MosaicUniform {
        int   mosaicOn;
        float pad[3];
    };
}

namespace engine {

    class AudioSystem;

    class RenderSystem final : public System
    {
 
    public:
        using InitProgressCallback = std::function<void(float, std::string_view)>;

		//==============camera follow =======================
		// name of the entity to follow in the scene
		//TODO 把这个做到ui里面，允许用户输入想要跟随的实体名字，或者在场景视口直接点击选中一个实体进行跟随
		const char* player = "Bike_0"; 



        explicit RenderSystem(bool& appRunning, SceneManager* sceneManager = nullptr)
            : mAppRunning(appRunning), mSceneManager(sceneManager) {
        }

        void SetInitProgressCallback(InitProgressCallback callback) {
            mInitProgressCallback = std::move(callback);
        }

    private:
        bool& mAppRunning;
        SceneManager* mSceneManager;
        engine::AnimationSystem* mAnimationSystem = nullptr;
        engine::AudioSystem* mAudioSystem = nullptr;
        InitProgressCallback mInitProgressCallback;

        lut::VulkanWindow  mWindow;
        lut::Allocator     mAllocator;

        void ReportInitProgress(float progress, std::string_view stage)
        {
            if (!mInitProgressCallback) {
                return;
            }

            if (mWindow.window) {
                glfwPollEvents();
            }

            mInitProgressCallback(progress, stage);
        }

    public:



        GLFWwindow* GetGLFWWindow() const {
            if (mWindow.window) return mWindow.window;
            return nullptr;
        }
       
        void ShowMainWindow() const {
            if (!mWindow.window) {
                return;
            }

            glfwShowWindow(mWindow.window);
            glfwFocusWindow(mWindow.window);
        }

        //==============UI System=========
        RuntimeUiController* GetRuntimeUiController() const {
            return mRuntimeUiController.get();
        }
        //Draw the main menu UI
      
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

        bool HasRuntimeUiScreen() const;
        bool ShouldRenderRuntimeUi() const;
        bool ShouldRenderRuntimeUiDebugPreview() const;
        bool BuildRuntimeUiRenderContext(UIRenderContext& outContext, ImVec2& outCanvasMin, ImVec2& outCanvasMax) const;
        void RefreshRuntimeUiDataContext(float dt);
        void UpdateRuntimeUi(float dt);
        void ForwardRuntimeUiMouseInput();
        void RenderRuntimeUi();

        struct RuntimeUiDebugRect {
            const UIScreen* screen = nullptr;
            const UIElement* element = nullptr;
            UIRect rect{};
            bool interactive = false;
        };

        bool ShouldShowRuntimeUiDebugPanel() const;
        bool ShouldDrawRuntimeUiDebugOverlay() const;
        static std::string FormatUiValueForDebug(const UIValue* value);
        static bool IsRuntimeUiElementInteractive(const UIElement& element);

        void CollectRuntimeUiDebugRects(
            const UIScreen& screen,
            const UIElement& element,
            const UIRect& parentRect,
            std::vector<RuntimeUiDebugRect>& outRects) const;
        void DrawRuntimeUiDebugOverlay();
        bool HandleRuntimeUiDebugSelection();
        void DrawRuntimeUiDebugPanel();

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
        // trigger:
        TriggerSystem& GetTriggerSystem() { return mTriggerSystem; }
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
            ReportInitProgress(0.02f, "Creating render window...");
            // ==========================================
            // 1. 核心设备与内存池初始化
            // ==========================================
			mWindow = lut::make_vulkan_window(false,true);//不立即显示窗口，等加载完毕再显示// Create a Vulkan window when  it  loading is complete
            glfwSetWindowUserPointer(mWindow.window, mState);
            mState->camera2world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 10.0f));

            mAllocator = lut::create_allocator(mWindow);
            mCmdPool = lut::create_command_pool(mWindow, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
            mDescPool = lut::create_descriptor_pool(mWindow);
            mParticleDescPool = lut::create_descriptor_pool(mWindow);
            ReportInitProgress(0.14f, "Allocating render memory...");
            // ==========================================
            // 2. 基础布局与管线布局初始化
            // ==========================================
            mSceneLayout = create_scene_descriptor_layout(mWindow);
            mObjectLayout = create_object_descriptor_layout(mWindow);
            mPostLayout = create_post_proc_descriptor_layout(mWindow);
            mPipeLayout = create_triangle_pipeline_layout(mWindow, mSceneLayout.handle, mObjectLayout.handle);
            mPostPipeLayout = create_post_proc_pipeline_layout(mWindow, mPostLayout.handle);
            ReportInitProgress(0.24f, "Creating descriptor layouts...");
            // ==========================================
            // 3. 采样器初始化 (必须在绑定描述符之前！)
            // ==========================================
            mDefaultSampler = lut::create_default_sampler(mWindow);
            mDebugSampler = create_debug_sampler(mWindow);
            mPostSampler = create_post_proc_sampler(mWindow);
            mShadowSampler = create_shadow_sampler(mWindow);
            ReportInitProgress(0.32f, "Creating samplers...");
            // ==========================================
            // 4. 默认占位贴图初始化
            // ==========================================
            {
                std::uint8_t grey[4] = { 255, 255, 255, 255 };
                mDefaultGrayTex = lut::load_image_texture2d_from_memory(grey, 1, 1, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
                mDefaultGrayView = lut::create_image_view_texture2d(mWindow, mDefaultGrayTex.image, VK_FORMAT_R8G8B8A8_UNORM);

                std::uint8_t black[4] = { 0, 0, 0, 255 };
                mDefaultBlackTex = lut::load_image_texture2d_from_memory(black, 1, 1, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
                mDefaultBlackView = lut::create_image_view_texture2d(mWindow, mDefaultBlackTex.image, VK_FORMAT_R8G8B8A8_UNORM);

                std::uint8_t normalBlue[4] = { 128, 128, 255, 255 };
                mDefaultNormalTex = lut::load_image_texture2d_from_memory(normalBlue, 1, 1, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
                mDefaultNormalView = lut::create_image_view_texture2d(mWindow, mDefaultNormalTex.image, VK_FORMAT_R8G8B8A8_UNORM);
            }
            ReportInitProgress(0.40f, "Preparing default textures...");
            // ==========================================
            // 5. 核心渲染目标与天空盒初始化
            // ==========================================
            mSceneUBO = lut::create_buffer(mAllocator, sizeof(glsl::SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
            mShadowMap = create_shadow_map(mWindow, mAllocator);

            for (uint32_t i = 0; i < kCascadeCount; ++i) {
                VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                viewInfo.image = mShadowMap.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = cfg::kShadowMapFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = i;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView view = VK_NULL_HANDLE;
                vkCreateImageView(mWindow.device, &viewInfo, nullptr, &view);
                mShadowCascadeViews.emplace_back(view);
            }

            // 【关键】：这里调用 InitSkybox，依赖了刚才创建的 mAllocator, mCmdPool 和 mDefaultSampler
            InitSkybox();
            ReportInitProgress(0.48f, "Setting up scene buffers...");
            // ==========================================
            // 6. 主场景描述符集绑定 (UBO + Shadow + Skybox)
            // ==========================================
            mSceneDescriptors = lut::alloc_desc_set(mWindow, mDescPool.handle, mSceneLayout.handle);
            {
                VkDescriptorBufferInfo bi{ mSceneUBO.buffer, 0, VK_WHOLE_SIZE };
                VkDescriptorImageInfo  si{ mShadowSampler.handle, mShadowMap.view, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL };
                VkDescriptorImageInfo  ki{ mDefaultSampler.handle, mSkyboxView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

                VkWriteDescriptorSet w[3]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[0].dstSet = mSceneDescriptors; w[0].dstBinding = 0; w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[0].descriptorCount = 1; w[0].pBufferInfo = &bi;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[1].dstSet = mSceneDescriptors; w[1].dstBinding = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].descriptorCount = 1; w[1].pImageInfo = &si;
                w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[2].dstSet = mSceneDescriptors; w[2].dstBinding = 2; w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[2].descriptorCount = 1; w[2].pImageInfo = &ki;

                vkUpdateDescriptorSets(mWindow.device, 3, w, 0, nullptr);
            }
            ReportInitProgress(0.56f, "Binding scene descriptors...");
            // ==========================================
            // 7. 同步对象与管线初始化
            // ==========================================
            for (std::size_t i = 0; i < mWindow.swapImages.size(); ++i) {
                mCmdBuffers.emplace_back(lut::alloc_command_buffer(mWindow, mCmdPool.handle));
                mFrameDone.emplace_back(lut::create_fence(mWindow.device, VK_FENCE_CREATE_SIGNALED_BIT));
                mImageAvailable.emplace_back(lut::create_semaphore(mWindow.device));
                mRenderFinished.emplace_back(lut::create_semaphore(mWindow.device));
            }

            mPipe = create_triangle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mAlphaPipe = create_alpha_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mThumbnailAlphaPipe = create_alpha_pipeline_1_attachment(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
            mMipPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugMipFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
            mDepthPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDepthFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
            mDerivPipe = create_debug_pipeline(mWindow, mPipeLayout.handle, cfg::kDebugVertShaderPath, cfg::kDebugDerivFragShaderPath, VK_FORMAT_R16G16B16A16_SFLOAT);
            mOverdrawPipe = create_overdraw_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mOvershadingPipe = create_overshading_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mShadowPipe = create_shadow_pipeline(mWindow, mPipeLayout.handle);
            mParticlePipe = create_particle_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mDebugLinePipe = create_debug_line_pipeline(mWindow, mPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);

            // Post Processing Pipelines
            mVisResolvePipe = create_vis_resolve_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);
            mPostProcPipe = create_post_proc_pipeline(mWindow, mPostPipeLayout.handle, mPostLayout.handle);
            ReportInitProgress(0.70f, "Compiling render pipelines...");
            // ==========================================
     // 8. 骨骼蒙皮与后处理缓冲区初始化
     // ==========================================
            mBoneLayout = create_bone_descriptor_layout(mWindow);
            mSkinnedPipeLayout = create_skinned_pipeline_layout(mWindow, mSceneLayout.handle, mObjectLayout.handle, mBoneLayout.handle);
            mSkinnedPipe = create_skinned_pipeline(mWindow, mSkinnedPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mSkinnedAlphaPipe = create_skinned_alpha_pipeline(mWindow, mSkinnedPipeLayout.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
            mShadowSkinnedPipe = create_shadow_skinned_pipeline(mWindow, mSkinnedPipeLayout.handle);

            mBoneSSBO = lut::create_buffer(mAllocator, kMaxBoneMatrices * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            {
                mBoneDescriptorSet = lut::alloc_desc_set(mWindow, mDescPool.handle, mBoneLayout.handle);
                VkDescriptorBufferInfo boneBI{ mBoneSSBO.buffer, 0, kMaxBoneMatrices * sizeof(glm::mat4) };
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mBoneDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &boneBI, nullptr };
                vkUpdateDescriptorSets(mWindow.device, 1, &w, 0, nullptr);
            }

            mDepthBuffer = create_depth_buffer(mWindow, mAllocator);
            mOffscreenImage = create_offscreen_buffer(mWindow, mAllocator);

            // 【清理修复】：SSR 只初始化这一次！
            mNormalImage = create_normal_buffer(mWindow, mAllocator);
            mSsrOutputImage = create_offscreen_buffer(mWindow, mAllocator);
            mSsrDescLayout = create_ssr_descriptor_layout(mWindow);
            mSsrPipeLayout = create_ssr_pipeline_layout(mWindow, mSsrDescLayout.handle);
            mSsrPipe = create_ssr_pipeline(mWindow, mSsrPipeLayout.handle);

            mVisImage = create_vis_image(mWindow, mAllocator);
            mBrightImage = create_offscreen_buffer(mWindow, mAllocator);
            mBlurTempImage = create_offscreen_buffer(mWindow, mAllocator);
            mFinalBloomImage = create_offscreen_buffer(mWindow, mAllocator);
            mCompositeOutputImage = create_offscreen_buffer(mWindow, mAllocator);

            // ==========================================
            // 【清理修复】：SSAO 资源与管线初始化
            // ==========================================
            mSsaoRawImage = create_ssao_raw_buffer(mWindow, mAllocator);
            mSsaoResources = engine::create_ssao_resources(mWindow, mCmdPool.handle);
            mSsaoDescLayout = create_ssao_descriptor_layout(mWindow);
            mSsaoPipeLayout = create_ssao_pipeline_layout(mWindow, mSsaoDescLayout.handle);
            mSsaoPipe = create_ssao_pipeline(mWindow, mSsaoPipeLayout.handle);
            ReportInitProgress(0.82f, "Creating offscreen render targets...");
            // ==========================================
            // 后处理管线初始化
            // ==========================================
            mBlurDescLayout = create_blur_descriptor_layout(mWindow);
            mCompDescLayout = create_composite_descriptor_layout(mWindow);
            mBlurPipeLayout = create_blur_pipeline_layout(mWindow, mBlurDescLayout.handle);
            mCompPipeLayout = create_composite_pipeline_layout(mWindow, mCompDescLayout.handle);
            mSpeedPostPipeLayout = create_speed_post_pipeline_layout(mWindow, mBlurDescLayout.handle);

            mBlurPipe = create_blur_pipeline(mWindow, mBlurPipeLayout.handle);
            mCompositePipe = create_composite_pipeline(mWindow, mCompPipeLayout.handle);
            mSpeedPostPipe = create_speed_post_pipeline(mWindow, mSpeedPostPipeLayout.handle);

            // =====================================================================
            // 【致命雷区修复】：全场唯一的一个描述符分配循环！里面只有 push_back！
            // =====================================================================
            for (std::size_t i = 0; i < mCmdBuffers.size(); ++i) {
                mMosaicUBOs.emplace_back(lut::create_buffer(mAllocator, sizeof(glsl::MosaicUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT));

                mPostDescriptors.push_back(BuildPostDesc(mOffscreenImage.view, mMosaicUBOs.back().buffer));
                mVisDescriptors.push_back(BuildPostDesc(mVisImage.view, mMosaicUBOs[i].buffer));
                mBlurHorizDescriptors.push_back(BuildBlurDesc(mBlurDescLayout.handle, mBrightImage.view));
                mBlurVertDescriptors.push_back(BuildBlurDesc(mBlurDescLayout.handle, mBlurTempImage.view));

                mCompositeDescriptors.push_back(BuildCompositeDesc(
                    mCompDescLayout.handle,
                    mOffscreenImage.view,
                    mFinalBloomImage.view,
                    mSsrOutputImage.view,
                    mMosaicUBOs[i].buffer,
                    mSsaoRawImage.view
                ));

                mSpeedPostDescriptors.push_back(BuildSpeedDesc(mCompositeOutputImage.view));

                mSsrDescriptors.push_back(BuildSsrDesc(
                    mOffscreenImage.view,
                    mDepthBuffer.view,
                    mNormalImage.view,
                    mSceneUBO.buffer
                ));

                mSsaoDescriptors.push_back(BuildSsaoDesc(
                    mDepthBuffer.view,
                    mNormalImage.view,
                    mSceneUBO.buffer
                ));
            }
            ReportInitProgress(0.90f, "Preparing post-processing passes...");
    
            // ==========================================
            // 9. UI 与调试初始化
            // ==========================================
            mDebugRenderer.Init(mAllocator);

            ImGuiRenderer::InitInfo uiInfo{};
            uiInfo.window = mWindow.window; uiInfo.instance = mWindow.instance; uiInfo.physicalDevice = mWindow.physicalDevice;
            uiInfo.device = mWindow.device; uiInfo.queue = mWindow.graphicsQueue; uiInfo.queueFamily = mWindow.graphicsFamilyIndex;
            uiInfo.colorFormat = mWindow.swapchainFormat; uiInfo.depthFormat = cfg::kDepthFormat; uiInfo.imageCount = (uint32_t)mWindow.swapImages.size();
            imguiRenderer.Init(uiInfo);

            VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = mWindow.swapchainFormat;
            imageInfo.extent = { mWindow.swapchainExtent.width, mWindow.swapchainExtent.height, 1 };
            imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_GPU_ONLY };
            VkImage rawImage; VmaAllocation rawAlloc;
            vmaCreateImage(mAllocator.allocator, &imageInfo, &allocInfo, &rawImage, &rawAlloc, nullptr);
            mFinalSceneImg = lut::Image(mAllocator.allocator, rawImage, rawAlloc);

            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = mFinalSceneImg.image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = mWindow.swapchainFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; viewInfo.subresourceRange.baseMipLevel = 0; viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0; viewInfo.subresourceRange.layerCount = 1;
            VkImageView rawView; vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
            mFinalSceneView = lut::ImageView(mWindow.device, rawView);

            m_sceneViewportTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, mFinalSceneView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            ReportInitProgress(0.96f, "Starting editor UI...");

            // Game UI Editor
            render_system_ui_editor::Configure(
                [this](const std::string& assetPath) -> void* {
                    return reinterpret_cast<void*>(GetContentBrowserThumbnail(assetPath));
                },
                [this](const std::filesystem::path& path) {
                    if (!mRuntimeUiController || !mRuntimeUiController->ReloadWidget(path)) {
                        EngineUi::LogPrint("[RuntimeUI] File changed but screen is not currently loaded: {}\n", path.generic_string());
                    }
                });

            if (mState) {
                mRuntimeUiController = std::make_unique<RuntimeUiController>(mAppRunning, *mState);
                mRuntimeUiController->Initialize([this](const std::string& assetPath) -> void* {
                    return reinterpret_cast<void*>(GetContentBrowserThumbnail(assetPath));
                    });
                mRuntimeUiManager = mRuntimeUiController->GetManager();
                mRuntimeUiRenderer = mRuntimeUiController->GetRendererShared();
            }
            else {
                EngineUi::LogPrint("[RuntimeUI] Skipped RuntimeUiController init because UserState is null\n");
            }
            // ==========================================
            // 10. 粒子初始化与缩略图系统
            // ==========================================
            for (const auto& path : cfg::ParticleTextures) {
                stbi_set_flip_vertically_on_load(0);
                lut::Image img = lut::load_image_texture2d(path, mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
                stbi_set_flip_vertically_on_load(0);
                lut::ImageView view = lut::create_image_view_texture2d(mWindow, img.image, VK_FORMAT_R8G8B8A8_UNORM);

                VkDescriptorSet descSet = lut::alloc_desc_set(mWindow, mParticleDescPool.handle, mObjectLayout.handle);
                VkDescriptorImageInfo mainImgInfo{ mDefaultSampler.handle, view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                VkDescriptorImageInfo dummyImgInfo{ mDefaultSampler.handle, mDefaultGrayView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

                VkWriteDescriptorSet writes[3]{};
                writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &mainImgInfo, nullptr, nullptr };
                writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dummyImgInfo, nullptr, nullptr };
                writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &dummyImgInfo, nullptr, nullptr };
                vkUpdateDescriptorSets(mWindow.device, 3, writes, 0, nullptr);

                particleTextureDict[path] = descSet;
                particleImages.push_back(std::move(img));
                particleImageViews.push_back(std::move(view));

                VkDescriptorSet imguiTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                particleImGuiTextureDict[path] = imguiTexId;
            }

            // 初始化默认粒子群组
            glm::vec3 ePos[] = { {2, 0.5, 2}, {3, 0.5, 2}, {4, 0.5, 2}, {2, 0.8, 2} };
            for (int i = 0; i < 4; i++) {
                auto ps = std::make_unique<ParticleSystem>();
                ps->setEmitterShape(i == 3 ? EmitterShape::Sphere : EmitterShape::Cone);
                ps->config.textureDescriptor = particleTextureDict[cfg::ParticleTextures[i % 2 == 0 ? 0 : 1]];
                ps->config.uiIconDescriptor = particleImGuiTextureDict[cfg::ParticleTextures[5]];
                ps->config.useTexture = 1;
                ps->config.emitterPos = ePos[i];
                ps->init(mAllocator, 500, ePos[i]);
                allParticles.push_back(std::move(ps));
            }

            InitThumbnailPipeline();

            namespace fs = std::filesystem;
            if (fs::exists("Assets/Models")) {
                for (const auto& entry : fs::directory_iterator("Assets/Models")) {
                    if (entry.path().extension() == ".glb") {
                        std::string p = entry.path().string();
                        std::replace(p.begin(), p.end(), '\\', '/');
                        TryLoadModelThumbnailFromCache(p);
                    }
                }
            }
            ReportInitProgress(1.0f, "Renderer ready");
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

        // 在 RenderSystem.hpp 中更新该函数：

        // 在 RenderSystem.hpp 中更新该函数：
        // 在 RenderSystem.hpp 中彻底替换该函数：
        VkDescriptorSet BuildCompositeDesc(VkDescriptorSetLayout layout, VkImageView sceneView, VkImageView bloomView, VkImageView ssrView, VkBuffer mosaicUbo, VkImageView ssaoView) {

            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, layout);

            // 4 张贴图 (0:Scene, 1:Bloom, 2:SSR, 3:SSAO)
            VkDescriptorImageInfo imgs[4]{};
            imgs[0] = { mPostSampler.handle, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mPostSampler.handle, ssrView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[3] = { mPostSampler.handle, ssaoView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            // 1 个 UBO (Binding 2)
            VkDescriptorBufferInfo bi{ mosaicUbo, 0, VK_WHOLE_SIZE };


            VkWriteDescriptorSet w[5]{};

            // Binding 0: Scene Color
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };

            // Binding 1: Bloom Color
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };

            // Binding 2: Mosaic UBO
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr };

            // Binding 3: SSR Color
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };

            // Binding 4: SSAO Color
            w[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[3], nullptr, nullptr };

     
            vkUpdateDescriptorSets(mWindow.device, 5, w, 0, nullptr);

            return ds;
        }
        // 【新增】：只更新，不分配！解决缩放崩溃

        void UpdateCompositeDesc(VkDescriptorSet ds, VkImageView sceneView, VkImageView bloomView, VkImageView ssrView, VkBuffer mosaicUbo, VkImageView ssaoView) {
            VkDescriptorImageInfo imgs[4]{};
            imgs[0] = { mPostSampler.handle, sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mPostSampler.handle, ssrView,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[3] = { mPostSampler.handle, ssaoView,  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo bi{ mosaicUbo, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet w[5]{};
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr };
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };
            w[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 4, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[3], nullptr, nullptr };

            vkUpdateDescriptorSets(mWindow.device, 5, w, 0, nullptr);
        }
        // 【新增】：只更新，不分配！
        void UpdateSsrDesc(VkDescriptorSet ds, VkImageView colorView, VkImageView depthView, VkImageView normalView, VkBuffer uboBuffer) {
            VkDescriptorImageInfo imgs[3]{};
            imgs[0] = { mPostSampler.handle, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mPostSampler.handle, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo ubo{ uboBuffer, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet w[4]{};
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo, nullptr };

            vkUpdateDescriptorSets(mWindow.device, 4, w, 0, nullptr);
        }
        // 【新增】：只更新，不分配！
        void UpdateSsaoDesc(VkDescriptorSet ds, VkImageView depthView, VkImageView normalView, VkBuffer uboBuffer) {
            VkDescriptorImageInfo imgs[3]{};
            imgs[0] = { mPostSampler.handle, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mSsaoResources.noiseSampler, mSsaoResources.noiseImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo ubos[2]{};
            ubos[0] = { uboBuffer, 0, sizeof(glsl::SceneUniform) };
            ubos[1] = { mSsaoResources.kernelBuffer.buffer, 0, sizeof(glm::vec4) * 64 };

            VkWriteDescriptorSet w[5]{};
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubos[0], nullptr };
            w[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubos[1], nullptr };

            vkUpdateDescriptorSets(mWindow.device, 5, w, 0, nullptr);
        }

        std::chrono::time_point<std::chrono::high_resolution_clock> lastTime = std::chrono::high_resolution_clock::now();
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
            // =======================================================
            // 2. 计算本帧的 deltaTime (单位：秒)
            // =======================================================
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;

            // 可选：为了防止断点调试时 deltaTime 变得极其巨大导致物理穿模或动画飞跃，通常会加一个上限约束
            if (deltaTime > 0.1f) {
                deltaTime = 0.1f;
            }

            // =======================================================
       // 基于时间轴（Timeline）的死亡特效计算
       // =======================================================
            if (!mState->isAlive) {
                // 1. 累加死亡时间
                mState->deathTimer += deltaTime;

                // 2. 定义动画曲线参数 (导演控制台)
                const float totalDuration = 8.0f;    // 整个特效持续 5 秒
                const float spikeRatio = 0.02f;       // 前 10% 的时间用来暴击变黑白 (0.5秒)

                // 计算分界点的时间
                const float spikeTime = totalDuration * spikeRatio;

                // 3. 根据当前时间，用数学分段计算 deathFactor
                if (mState->deathTimer <= spikeTime) {
                    // 前半段：从 0.0 快速飙升到 1.0
                    // math: current_time / target_time
                    mState->deathFactor = mState->deathTimer / spikeTime;
                }
                else if (mState->deathTimer <= totalDuration) {
                    // 后半段：从 1.0 缓慢回落到 0.0
                    // 算出在后半段里经过了多少时间
                    float decayTimePassed = mState->deathTimer - spikeTime;
                    float decayTotalTime = totalDuration - spikeTime;

                    // math: 1.0 - (passed / total)
                    mState->deathFactor = 1.0f - (decayTimePassed / decayTotalTime);
                }
                else {
                    // 特效结束，彻底黑死或者维持某一个底色 (这里设为 0.0 完全恢复彩色)
                    mState->deathFactor = 0.0f;
                }

            }
            else {
                // 存活状态：重置参数
                mState->deathTimer = 0.0f;

                // 快速恢复彩色（防止复活瞬间画面突变）
                mState->deathFactor -= deltaTime * 5.0f;
                if (mState->deathFactor < 0.0f) {
                    mState->deathFactor = 0.0f;
                }
            }
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
                    if (mRuntimeUiController) {
                        mRuntimeUiController->RemoveWidgetFromViewPort("Assets/ui/SettingsMenu.ui.json");
                        mRuntimeUiController->AddWidgetToViewPort("Assets/ui/PauseMenu.ui.json");
                    }
                    engine::EngineUi::LogPrintf("Test: Game pause triggered via 'H' key.\n");
                }
                else
                {
                    if (mRuntimeUiController) {
                        mRuntimeUiController->RemoveWidgetFromViewPort("Assets/ui/SettingsMenu.ui.json");
                        mRuntimeUiController->RemoveWidgetFromViewPort("Assets/ui/PauseMenu.ui.json");
                        if (mRuntimeUiController->IsWidgetLoaded("Assets/ui/HUD.ui.json")) {
                            mRuntimeUiController->AddWidgetToViewPort("Assets/ui/HUD.ui.json");
                        }
                    }
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
            if (mState->showEngineUi)
            {
                ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            }

            //ImGui::DockSpace(ImGui::GetID("MyDockSpace"), ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
            //调用顶部菜单栏！
            if (mState->showEngineUi) 
            {
                EngineUi::DrawMainMenuBar(this, mSceneManager, *mState, mAppRunning);
            }

            const bool runtimeUiViewportActive = ShouldRenderRuntimeUi();

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
                void update_scene_uniforms(glsl::SceneUniform & aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, const engine::UserState & aState);

                //View 矩阵
                glm::mat4 view = glm::inverse(mState->camera2world);
                //Aspect Ratio
                //float aspect = (float)mWindow.swapchainExtent.width / (float)mWindow.swapchainExtent.height;
                float aspect = width / height;

                //FOV: use the live camera FOV so picking and drag placement match the rendered scene.
                float fovRadians = glm::radians(std::clamp(mState->cameraFov, 10.0f, 120.0f));

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
				
				//game HUD============================
                GameUi::DrawHud(this, *mState, EngineUi::GetSceneViewportPos(), EngineUi::GetSceneViewportSize());


                // Runtime UI
                ForwardRuntimeUiMouseInput();
                UpdateRuntimeUi(dt);
                RenderRuntimeUi();
                DrawRuntimeUiDebugOverlay();


                glm::mat4 viewProj = gizmoProj * view;
                glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]); // 提取逆 view 矩阵第 4 列作为位置

                // 给面板加上开关判断：
                if (mState->showEngineUi && mState->showControlPanel) {
                    EngineUi::DrawControlPanel(*mState, this, mSceneManager);
                }

                if (mState->showEngineUi && mState->showContentBrowser) {
                    EngineUi::DrawContentBrowser(this, mSceneManager);
                }

                if (mState->showEngineUi && (mState->showSceneHierarchy || mState->showEntityInspector)) {
                    EngineUi::DrawSceneHierarchy(this, mSceneManager, view, gizmoProj, mSelectedEntityId, *mState);
                }
                //light UI
                if (mState->showEngineUi && mState->showLightPanel) {
                    EngineUi::DrawLightPanel(mSceneManager, *mState);
                }
				//camera UI
                if (mState->showEngineUi && mState->showCameraPanel) {
                    EngineUi::DrawCameraPanel(*mState);
                }
				//debug UI
                if (mState->showEngineUi && mState->showDebugPanel) {
                    EngineUi::DrawDebugPanel(*mState);
                    DrawRuntimeUiDebugPanel();
                }
				//audio UI
                if (mState->showEngineUi && mState->showAudioPanel) {
                    EngineUi::DrawAudioPanel(*mState, mAudioSystem);
                }

                // Game UI Editor
                render_system_ui_editor::Draw(*mState);

                // mouse capture
                const bool runtimeUiDebugSelectionConsumed =
                    mState->showEngineUi &&
                    !render_system_ui_editor::WantsMouseCapture() &&
                    isMouseInViewport &&
                    HandleRuntimeUiDebugSelection();

                if (mState->showEngineUi &&
                    !render_system_ui_editor::WantsMouseCapture() &&
                    !runtimeUiDebugSelectionConsumed &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    isMouseInViewport &&
                    !ImGuizmo::IsOver())
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
                    auto* physics = mSceneManager->get_physics_system();
                    uint32_t selectedBodyID = JPH::BodyID::cInvalidBodyID;
                    bool hasDebugBody = selectedEntity.is_alive() && physics && TryGetDebugBodyID(selectedEntity, selectedBodyID);

                    if (mState->showEngineUi && hasDebugBody && (mState->debugSelectionBounds || mState->debugCollisionShapes)) {
                        JPH::BodyInterface& bodyInterface = physics->get_body_interface();
                        JPH::TransformedShape ts = bodyInterface.GetTransformedShape(JPH::BodyID(selectedBodyID));

                        if (mState->debugSelectionBounds) {
                            physics_debug::DrawSelectionBounds(mDebugRenderer, ts, glm::vec3(0.0f, 1.0f, 0.0f));
                        }

                        if (mState->debugCollisionShapes) {
                            physics_debug::DrawCollisionShapeWireframe(mDebugRenderer, ts, glm::vec3(1.0f, 0.75f, 0.15f));
                        }
                    }

                    if (selectedEntity.is_alive() && ImGuizmo::IsUsing() && selectedEntity.has<PhysicsBody>() && physics) {
                        const auto& lt = selectedEntity.get<LocalTransform>();
                        auto pb = selectedEntity.get<PhysicsBody>();
                        //auto* physics = mSceneManager->get_physics_system();

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
                        //mDebugRenderer.DrawBox(center, extents, glm::vec3(0.0f, 1.0f, 0.0f));
                        // =========================================================================
                        
                        //debug
                        if (false) engine::EngineUi::LogPrint("[Physics Debug] Entity: {} | Size: ({:.2f}, {:.2f}, {:.2f})\n",
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
            // 【新增】：处理 IBL 开关
            if (mInputSystem->IsActionPressed("IBLToggle")) {
                mState->iblEnabled = !mState->iblEnabled;
                std::printf("IBL Reflection: %s\n", mState->iblEnabled ? "ON" : "OFF");
            }
            // 【新增】：处理 SSR 开关
            if (mInputSystem->IsActionPressed("SSRToggle")) {
                mState->ssrEnabled = !mState->ssrEnabled;
                std::printf("Screen Space Reflection (SSR): %s\n", mState->ssrEnabled ? "ON" : "OFF");
            }

            if (mInputSystem->IsActionPressed("SSAOToggle")) {
                mState->ssaoEnabled = !mState->ssaoEnabled;
                std::printf("Screen Space Ambient Occlusion (SSAO): %s\n", mState->ssaoEnabled ? "ON" : "OFF");
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
                    mThumbnailAlphaPipe = create_alpha_pipeline_1_attachment(mWindow, mPipeLayout.handle, VK_FORMAT_R8G8B8A8_UNORM);
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
                    // 1. 重建所有与屏幕尺寸绑定的画板 (Image)
                    mDepthBuffer = create_depth_buffer(mWindow, mAllocator);
                    mOffscreenImage = create_offscreen_buffer(mWindow, mAllocator);
                    mVisImage = create_vis_image(mWindow, mAllocator);
                    mNormalImage = create_normal_buffer(mWindow, mAllocator);
                    mSsrOutputImage = create_offscreen_buffer(mWindow, mAllocator);
                    mCompositeOutputImage = create_offscreen_buffer(mWindow, mAllocator);
                    mBrightImage = create_offscreen_buffer(mWindow, mAllocator);
                    mBlurTempImage = create_offscreen_buffer(mWindow, mAllocator);
                    mFinalBloomImage = create_offscreen_buffer(mWindow, mAllocator);
                    mSsaoRawImage = create_ssao_raw_buffer(mWindow, mAllocator);

                    // 2. 重建相纸 (最终 UI 贴图)
                    VkImageCreateInfo imageInfo{};
                    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                    imageInfo.imageType = VK_IMAGE_TYPE_2D;
                    imageInfo.format = mWindow.swapchainFormat;
                    imageInfo.extent = { mWindow.swapchainExtent.width, mWindow.swapchainExtent.height, 1 };
                    imageInfo.mipLevels = 1;
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

                    VkImageViewCreateInfo viewInfo{};
                    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    viewInfo.image = mFinalSceneImg.image;
                    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    viewInfo.format = mWindow.swapchainFormat;
                    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    viewInfo.subresourceRange.baseMipLevel = 0;
                    viewInfo.subresourceRange.levelCount = 1;
                    viewInfo.subresourceRange.baseArrayLayer = 0;
                    viewInfo.subresourceRange.layerCount = 1;

                    VkImageView rawView = VK_NULL_HANDLE;
                    vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
                    mFinalSceneView = lut::ImageView(mWindow.device, rawView);

                    if (m_sceneViewportTexId) ImGui_ImplVulkan_RemoveTexture(m_sceneViewportTexId);
                    m_sceneViewportTexId = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, mFinalSceneView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                    // =====================================================================
                    // 3. 极其关键！更新旧的描述符，绝不分配新内存！
                    // =====================================================================

                    // 更新所有简单的单图后处理描述符
                    UpdatePostDescImage(mPostDescriptors, mOffscreenImage.view);
                    UpdatePostDescImage(mVisDescriptors, mVisImage.view);
                    UpdatePostDescImage(mBlurHorizDescriptors, mBrightImage.view);
                    UpdatePostDescImage(mBlurVertDescriptors, mBlurTempImage.view);
                    UpdatePostDescImage(mSpeedPostDescriptors, mCompositeOutputImage.view);

                    // 只有这一个循环！更新我们写的复合描述符
                    for (size_t i = 0; i < mCmdBuffers.size(); ++i) {
                        UpdateCompositeDesc(
                            mCompositeDescriptors[i],
                            mOffscreenImage.view,
                            mFinalBloomImage.view,
                            mSsrOutputImage.view,
                            mMosaicUBOs[i].buffer,
                            mSsaoRawImage.view
                        );

                        UpdateSsrDesc(
                            mSsrDescriptors[i],
                            mOffscreenImage.view,
                            mDepthBuffer.view,
                            mNormalImage.view,
                            mSceneUBO.buffer
                        );

                        UpdateSsaoDesc(
                            mSsaoDescriptors[i],
                            mDepthBuffer.view,
                            mNormalImage.view,
                            mSceneUBO.buffer
                        );
                    }

                    UpdateSceneDescriptorSkybox();
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

                    // 1. 获取角色脚底底座的原始世界坐标
                    glm::vec3 basePos = glm::vec3(wt.matrix[3]);

                    // =========================================================
                    // 
					// yaw for offset direction:
                    //  (-sin(Yaw), 0, -cos(Yaw))
                    // 水平向量: (cos(Yaw), 0, -sin(Yaw))
                    // =========================================================
                    glm::vec3 camRight = glm::vec3(std::cos(mState->Yaw), 0.0f, -std::sin(mState->Yaw));

                    // 2. 设置越肩的偏移量 
                    float shoulderOffsetX = 1.0f; //left and right
                    float shoulderOffsetY = -1.0f; // height
                    float shoulderOffsetZ = 0.0f; // 调整注视点前后

                    // 3. 计算出最终的越肩目标点
                    mState->followTargetPos = basePos
                        + (camRight * shoulderOffsetX)
                        + glm::vec3(0.0f, shoulderOffsetY, 0.0f);

                    // printf("Follow Target Pos: (%.2f, %.2f, %.2f)\n", mState->followTargetPos.x, mState->followTargetPos.y, mState->followTargetPos.z);
                }
            }

            // --- Toggle Inputs via InputSystem ---
            if (mInputSystem) {
                if (mInputSystem->IsActionPressed("ToggleParticles")) {
                    mState->particlesEnabled = !mState->particlesEnabled;
                    std::printf("Particles: %s\n", mState->particlesEnabled ? "ON" : "OFF");
                }
                if (mInputSystem->IsActionPressed("CameraThirdPersonToggle" ) ) {
                    // T
                    mState->thirdPersonMode = !mState->thirdPersonMode;
                    std::printf("Camera: %s\n", mState->thirdPersonMode ? "Third Person" : "Free Fly");
                    if(mState->isAlive == false ) mState->isAlive = true; 
                }
              
                if (mInputSystem->IsActionPressed("ToggleEngineUi")) {
                    mState->showEngineUi = !mState->showEngineUi;
                    EngineUi::ShowToast(mState->showEngineUi ? "[ Engine UI Visible ]" : "[ Engine UI Hidden ]");
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
            // 【关键修复】：调用函数！
            update_scene_uniforms(
                sceneUniforms,
                static_cast<uint32_t>(finalWidth),
                static_cast<uint32_t>(finalHeight),
                *mState
            );

            // 【新增】：将 IBL 状态同步给 Shader
            sceneUniforms.iblEnabled = mState->iblEnabled ? 1 : 0;
            //frustum culling: keep separate smoothed FPS samples for culling ON vs OFF.
            if (dt > 0.0001f) {
                float currentFps = 1.0f / dt;
                auto smoothFrustumFpsSample = [](float currentSample, float latestSample) {
                    return currentSample <= 0.0f ? latestSample : currentSample + (latestSample - currentSample) * 0.1f;
                    };

                if (mState->frustumCullingEnabled) {
                    mState->frustumCullingOnFps = smoothFrustumFpsSample(mState->frustumCullingOnFps, currentFps);
                }
                else {
                    mState->frustumCullingOffFps = smoothFrustumFpsSample(mState->frustumCullingOffFps, currentFps);
                }
            }

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

            
            //================  particle system===================================================

             // trigger
            mTriggerSystem.ProcessParticleTriggers(mState->followTargetPos, allParticles);

            if (mState->particlesEnabled)
            {
                for (const auto& ps  : allParticles)
                {
                    // trigger: skip particle updates when the trigger system has hidden this particle group.
                    if (!ps->config.isVisible) {
                        continue;
                    }

                    if (ps->getEmitterShape() == EmitterShape::Sphere && !ps->config.triggerControlled)
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
            //std::vector<RenderBatch> finalBatches = mSceneManager ? mSceneManager->get_render_batches() : std::vector<RenderBatch>{};
            const Frustum* activeFrustum = nullptr; // new frustum culling
            Frustum cameraFrustum{}; // new frustum culling
            if (mSceneManager && mState->frustumCullingEnabled) {
                cameraFrustum = BuildFrustum(sceneUniforms.projCam); // new frustum culling
                activeFrustum = &cameraFrustum; // new frustum culling
            }

            //std::vector<RenderBatch> finalBatches = mSceneManager ? mSceneManager->get_render_batches(activeFrustum) : std::vector<RenderBatch>{};
            glm::vec3 camPosWorld = glm::vec3(sceneUniforms.cameraPos);
            std::vector<RenderBatch> finalBatches = mSceneManager ? mSceneManager->get_render_batches(activeFrustum, mState->frustumCullingPadding, camPosWorld) : std::vector<RenderBatch>{};
            if (mSceneManager) {
                mState->frustumCullingTotalCandidates = mSceneManager->get_last_frustum_culling_candidates(); // new frustum culling
                mState->frustumCullingVisibleCandidates = mSceneManager->get_last_frustum_culling_visible(); // new frustum culling
            }
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
            // trigger: draw every visible trigger volume through DebugRendere
            mTriggerSystem.DrawTriggers(mDebugRenderer);
            // 1. 在提交命令前，把这一帧收集的线上传到 GPU
            mDebugRenderer.Upload(mAllocator);

            float currentBloomStrength = mState->bloomEnabled ? 2.2f : 0.0f;

            // =========================================================
            // 计算极速特效的平滑系数 (Speed Factor)
            // =========================================================
            float effectStartSpeed = 20.0f; // 开始出现特效的最低速度
            float effectMaxSpeed = 40.0f;  // 特效拉满的极限速度
            float currentSpeed = std::abs(mState->bikeSpeed);

            float targetSpeedFactor = 0.0f;
            if (currentSpeed > effectStartSpeed) {
                targetSpeedFactor = (currentSpeed - effectStartSpeed) / (effectMaxSpeed - effectStartSpeed);
                targetSpeedFactor = std::clamp(targetSpeedFactor, 0.0f, 1.0f);
            }

            // 使用 static 变量进行平滑插值，防止掉帧或特效闪烁
            // 2. 特效弹簧阻尼 (防闪烁)
            // 【修改这里的 5.0f】：
            // 调大 (比如 10.0f)：特效响应极其灵敏，一踩油门特效瞬间拉满。
            // 调小 (比如 2.0f) ：特效会非常缓慢地浮现，有种“逐渐进入超空间”的深邃感
            static float smoothedSpeedFactor = 1.0f;
            smoothedSpeedFactor += (targetSpeedFactor - smoothedSpeedFactor) * 5.0f * dt;
            std::vector<RenderBatch> skinnedBatches;
            if (mSceneManager && mBoneSSBO.buffer != VK_NULL_HANDLE) {
                void* ptr;
                vmaMapMemory(mAllocator.allocator, mBoneSSBO.allocation, &ptr);
                size_t boneCount = 0;
                skinnedBatches = mSceneManager->get_skinned_batches(
                    static_cast<glm::mat4*>(ptr), kMaxBoneMatrices, boneCount);
                vmaUnmapMemory(mAllocator.allocator, mBoneSSBO.allocation);
            }
            // =========================================================
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

                // ==========================================================
            // 【新增】：传递给 rendering.cpp 的 SSR 资源参数
            // ==========================================================
                ImageAndView{ mNormalImage.image, mNormalImage.view },  // aNormalImage
                ImageAndView{ mSsrOutputImage.image, mSsrOutputImage.view }, // aSsrOutput
                mSsrPipe.handle,                                        // aSsrPipe
                mSsrPipeLayout.handle,                                  // aSsrLayout
                mSsrDescriptors[mFrameIndex],                           // aSsrDS
                // ==========================================================
                // ==========================================================
            // 【新增】：传递给 rendering.cpp 的 SSAO 资源参数
            // ==========================================================
                ImageAndView{ mSsaoRawImage.image, mSsaoRawImage.view }, // aSsaoRawOutput
                mSsaoPipe.handle,                                        // aSsaoPipe
                mSsaoPipeLayout.handle,                                  // aSsaoLayout
                mSsaoDescriptors[mFrameIndex],                           // aSsaoDS
                mState->ssaoEnabled,                                     // aSsaoEnabled (记得在状态机或UI里加上这个布尔值)
                // ==========================================================
                mState->ssrEnabled,
                ImageAndView{ mBlurTempImage.image, mBlurTempImage.view },
                ImageAndView{ mFinalBloomImage.image, mFinalBloomImage.view },
                // 【注意这里的变化】：
                // 原本这里传的是 finalSceneTarget，现在 Composite 必须输出到 mCompositeOutputImage
                ImageAndView{ mCompositeOutputImage.image, mCompositeOutputImage.view },

                clearColor,                    // VkClearColorValue aClearColor
                currentBloomStrength,

                // 【新增】：将极速管线和目标传给 rendering.cpp
                mSpeedPostPipe.handle,
                mSpeedPostPipeLayout.handle,
                mSpeedPostDescriptors[mFrameIndex],
                smoothedSpeedFactor, // 传递我们刚算好的平滑因子
                mState->isAlive,       // <--- 直接把 userState 里的变量喂给渲染器！
                mState->deathFactor,
                finalSceneTarget,    // 极速特效输出到最终

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
                mDebugRenderer,
                // Skeletal skinning
                mSkinnedPipe.handle,
                mSkinnedAlphaPipe.handle,
                mSkinnedPipeLayout.handle,
                mBoneDescriptorSet,
                & mMeshJointIndices,
                & mMeshJointWeights,
                & skinnedBatches,
                mShadowSkinnedPipe.handle,
                mSkyboxPipe.handle,
                mSkyboxPipeLayout.handle,
                mSkyboxDescSet,
                mSkyboxVBO.buffer
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
            // trigger: clear all trigger volumes on shutdown
            mTriggerSystem.ClearTriggers();
            mDebugRenderer.Shutdown(mAllocator);
            ::imguiRenderer.Shutdown();

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

        // 定义一个结构体用于返回偏移量
        struct ModelAssetOffsets {
            uint32_t baseMeshIdx;
            uint32_t baseMaterialIdx;
        };

        // 【新增】：专注 GPU 上传的纯粹渲染接口
        ModelAssetOffsets RegisterModelAssets(EngineModel& newModel)
        {
            uint32_t baseTextureIdx = static_cast<uint32_t>(mModelTextures.size());
            uint32_t baseMaterialIdx = static_cast<uint32_t>(mModel.materials.size());
            uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

            // 1. 上传贴图 (不变)
            for (auto const& tex : newModel.textures) {
                glfwPollEvents();
                VkFormat fmt = (tex.space == ETextureSpace::srgb) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(tex.pixels.data(), tex.width, tex.height, mWindow, mCmdPool.handle, mAllocator, fmt));
                mModelTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, mModelTextures.back().image, fmt));
            }

            // 2. 追加材质并修复贴图索引 (不变)
            for (auto mat : newModel.materials) {
                if (mat.baseColorTexture >= 0) mat.baseColorTexture += baseTextureIdx;
                if (mat.normalTexture >= 0) mat.normalTexture += baseTextureIdx;
                if (mat.metalRoughTexture >= 0) mat.metalRoughTexture += baseTextureIdx;
                if (mat.occlusionTexture >= 0) mat.occlusionTexture += baseTextureIdx;
                if (mat.emissiveTexture >= 0) mat.emissiveTexture += baseTextureIdx;
                if (mat.alphaMaskTexture >= 0) mat.alphaMaskTexture += baseTextureIdx;

                mModel.materials.push_back(mat);
                AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, mat);
                AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, mat);
            }

            // 3. 追加网格并上传 VBO/IBO (不变)
            for (auto mesh : newModel.meshes) {
                mesh.materialIndex += baseMaterialIdx;
                mModel.meshes.push_back(mesh);
                UploadSingleMesh(mesh);
            }

            // 4. 更新渲染器维护的全局场景实例列表 (剥离了物理逻辑)
            //for (auto& instance : newModel.scenes) {
            for (auto instance : newModel.scenes) {
                instance.meshIndex += baseMeshIdx;
                mModel.scenes.push_back(instance);
            }

            // 把偏移量返回给外面的 SceneManager，让它去配置 ECS
            return { baseMeshIdx, baseMaterialIdx };
        }
        // add an entire model file to the renderer and physics scene
    //    void load_additional_model(const char* path, bool isStatic, float mass = 1.0f, const glm::mat4& initialTransform = glm::mat4(1.0f), bool isCompound = false, bool isC = false)
    //    {
    //        EngineModel newModel = load_engine_model_glb(path);
    //        uint32_t baseTextureIdx = static_cast<uint32_t>(mModelTextures.size());
    //        uint32_t baseMaterialIdx = static_cast<uint32_t>(mModel.materials.size());
    //        uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

    //        // 1. Appends textures
    //        for (auto const& tex : newModel.textures) {



    //            glfwPollEvents();
    //            VkFormat fmt = (tex.space == ETextureSpace::srgb) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    //            mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(tex.pixels.data(), tex.width, tex.height, mWindow, mCmdPool.handle, mAllocator, fmt));
    //            mModelTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, mModelTextures.back().image, fmt));
    //        }

    //        for (size_t i = 0; i < newModel.textures.size(); ++i) {
    //            std::printf("[Tex %zu] name='%s' %dx%d\n",
    //                i + baseTextureIdx,
    //                newModel.textures[i].name.c_str(),
    //                newModel.textures[i].width,
    //                newModel.textures[i].height);
    //        }

    //        // 2. Append materials (fixing texture references)
    //        for (auto mat : newModel.materials) {
    //            if (mat.baseColorTexture >= 0) mat.baseColorTexture += baseTextureIdx;
    //            if (mat.normalTexture >= 0) mat.normalTexture += baseTextureIdx;
    //            if (mat.metalRoughTexture >= 0) mat.metalRoughTexture += baseTextureIdx;
    //            if (mat.occlusionTexture >= 0) mat.occlusionTexture += baseTextureIdx;
    //            if (mat.emissiveTexture >= 0) mat.emissiveTexture += baseTextureIdx;
    //            if (mat.alphaMaskTexture >= 0) mat.alphaMaskTexture += baseTextureIdx;
    //            
    //            mModel.materials.push_back(mat);
    //            
    //            // Add descriptors for new materials
    //            AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, mat);
    //            AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, mat);
    //        }

    //        std::printf("[LoadModel] %s -> baseMat=%u, added %zu mats, total mats=%zu, total descs=%zu\n",
    //            path, baseMaterialIdx,
    //            newModel.materials.size(),
    //            mModel.materials.size(),
    //            mMaterialDescriptors.size());

    //        // 3. Append meshes (fixing material references)
    //        for (auto mesh : newModel.meshes) {
    //            mesh.materialIndex += baseMaterialIdx;
    //            mModel.meshes.push_back(mesh);
    //            UploadSingleMesh(mesh);
    //              
    //        }



    //        // 4.1 apply initial transform and create entities via SceneManager, using local mesh indices to build physics.
    //        for (auto& instance : newModel.scenes) {
    //            instance.transform = initialTransform * instance.transform;
    //        }
    //        
    //        // 4.2 update the scenes to use global mesh indices for the renderer.
    //        if (mSceneManager) {
    //            if (isCompound) {
    //                mSceneManager->load_compound_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
				//}
    //            else if (isC)
    //            {
    //                mSceneManager->load_C_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
    //            }
    //            else if (isStatic) {
    //                mSceneManager->load_static_model(newModel, baseMeshIdx, baseMaterialIdx);
    //            } else {
    //                mSceneManager->load_dynamic_model(newModel, mass, baseMeshIdx, baseMaterialIdx);
    //            }
    //        }


    //        // 5. Update model scenes (fixing mesh references for the global renderer array)
    //        for (auto& instance : newModel.scenes) {
    //            instance.meshIndex += baseMeshIdx;
    //            mModel.scenes.push_back(instance);
    //        }
    //    }

        // returns the material descriptor index assigned to the last add_runtime_mesh() call
        uint32_t get_runtime_mat_index() const { return mRuntimeMatIndex; }

        uint32_t get_material_count() const
        {
            return static_cast<uint32_t>(mModel.materials.size());
        }

        // Allow application to pass in the input system
        void SetInputSystem(engine::InputSystem* sys) { mInputSystem = sys; }

        // Wire up the animation system so we can call register_model
        void set_animation_system(engine::AnimationSystem* anim) { mAnimationSystem = anim; }

        // Load a skinned GLB and register it with the animation system.
        // Creates entities with AnimationComponent + SkinComponent.
        void load_animated_model(const char* path,
            const glm::mat4& initialTransform = glm::mat4(1.0f))
        {
            EngineModel newModel = load_engine_model_glb(path);
            uint32_t baseTexIdx = static_cast<uint32_t>(mModelTextures.size());
            uint32_t baseMatIdx = static_cast<uint32_t>(mModel.materials.size());
            uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

            // 1. Upload textures
            for (auto const& tex : newModel.textures) {
                glfwPollEvents();
                VkFormat fmt = (tex.space == ETextureSpace::srgb)
                    ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(
                    tex.pixels.data(), tex.width, tex.height,
                    mWindow, mCmdPool.handle, mAllocator, fmt));
                mModelTextureViews.emplace_back(lut::create_image_view_texture2d(
                    mWindow, mModelTextures.back().image, fmt));
            }

            // 2. Upload materials
            for (auto mat : newModel.materials) {
                if (mat.baseColorTexture >= 0) mat.baseColorTexture += baseTexIdx;
                if (mat.normalTexture >= 0) mat.normalTexture += baseTexIdx;
                if (mat.metalRoughTexture >= 0) mat.metalRoughTexture += baseTexIdx;
                if (mat.occlusionTexture >= 0) mat.occlusionTexture += baseTexIdx;
                if (mat.emissiveTexture >= 0) mat.emissiveTexture += baseTexIdx;
                if (mat.alphaMaskTexture >= 0) mat.alphaMaskTexture += baseTexIdx;
                mModel.materials.push_back(mat);
                AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, mat);
                AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, mat);
            }

            // 3. Upload meshes (position/normal/texcoord + skinning buffers)
            for (auto mesh : newModel.meshes) {
                mesh.materialIndex += baseMatIdx;
                mModel.meshes.push_back(mesh);
                uint32_t meshIdx = static_cast<uint32_t>(mModel.meshes.size() - 1);
                UploadSingleMesh(mesh);
                if (mesh.isSkinned) {
                    UploadSkinningBuffers(mesh, meshIdx);
                }
            }

            // 4. Apply initial transform
            for (auto& inst : newModel.scenes)
                inst.transform = initialTransform * inst.transform;

            // 5. Register with AnimationSystem and create ECS entities
            uint32_t baseSkinIdx = 0, baseAnimIdx = 0;
            if (mAnimationSystem) {
                auto reg = mAnimationSystem->register_model(newModel);
                baseSkinIdx = reg.baseSkinIndex;
                baseAnimIdx = reg.baseAnimIndex;
            }

            if (mSceneManager)
                mSceneManager->load_animated_model(newModel, baseMeshIdx, baseMatIdx,
                    baseSkinIdx, baseAnimIdx);

            // 6. Merge scenes into global model
            for (auto& inst : newModel.scenes) {
                inst.meshIndex += baseMeshIdx;
                mModel.scenes.push_back(inst);
            }
        }

        void SetUserState(UserState* state) { this->mState = state; }
        void SetAudioSystem(AudioSystem* audioSystem) { this->mAudioSystem = audioSystem; }
        AudioSystem* GetAudioSystem() const { return mAudioSystem; }
        // 在 RenderSystem.hpp 内部添加该辅助函数
        void UpdateSceneDescriptorSkybox()
        {
            if (mSceneDescriptors == VK_NULL_HANDLE || mSkyboxView.handle == VK_NULL_HANDLE) return;

            VkDescriptorImageInfo skyboxInfo{};
            skyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            skyboxInfo.imageView = mSkyboxView.handle;
            skyboxInfo.sampler = mDefaultSampler.handle;

            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = mSceneDescriptors;
            write.dstBinding = 2; // 重新覆盖 Binding 2
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &skyboxInfo;

            vkUpdateDescriptorSets(mWindow.device, 1, &write, 0, nullptr);
        }
    private:

        UserState* mState = nullptr;

        engine::InputSystem* mInputSystem = nullptr;

        bool TryGetDebugBodyID(flecs::entity entity, uint32_t& outBodyID) const
        {
            if (entity.has<PhysicsBody>()) {
                outBodyID = entity.get<PhysicsBody>().bodyID;
                return true;
            }

            if (entity.has<CompoundParent>()) {
                outBodyID = entity.get<CompoundParent>().bodyID;
                return true;
            }

            return false;
        }

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

        void AddOneMaterialDescriptor(VkSampler sampler, std::vector<VkDescriptorSet>& out, const EngineMaterial& mat)
        {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mObjectLayout.handle);

            // 0: BaseColor (基础色)
            VkImageView baseView = mDefaultGrayView.handle;
            if (mat.baseColorTexture >= 0) baseView = mModelTextureViews[mat.baseColorTexture].handle;

            // 1 & 2: MetalRoughness / ORM (粗糙度与金属度)
            // glTF 中它们打包在同一张图里，所以这里提取一次，给后面复用
            VkImageView mrView = mDefaultGrayView.handle;
            if (mat.metalRoughTexture >= 0) mrView = mModelTextureViews[mat.metalRoughTexture].handle;

            // 3: Emissive (自发光)
            VkImageView emissiveView = mDefaultBlackView.handle;
            if (mat.emissiveTexture >= 0) emissiveView = mModelTextureViews[mat.emissiveTexture].handle;

            // 4: Normal (法线)
            VkImageView normView = mDefaultNormalView.handle;
            if (mat.normalTexture >= 0) normView = mModelTextureViews[mat.normalTexture].handle;

            // 5: Occlusion (AO 环境光遮蔽) -> 【这是我们新增的！】
            // 如果没有 AO，默认传灰色或黑色都行，我们在 Shader 里已经写了智能回退机制
            VkImageView aoView = mDefaultGrayView.handle;
            if (mat.occlusionTexture >= 0) aoView = mModelTextureViews[mat.occlusionTexture].handle;

            // --- 【关键修改：数组大小必须是 6，严格对应 Shader 的 binding 0~5】 ---
            VkDescriptorImageInfo imgs[6]{};
            imgs[0] = { sampler, baseView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 0
            imgs[1] = { sampler, mrView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 1 (Roughness)
            imgs[2] = { sampler, mrView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 2 (Metalness - 直接复用 ORM 图)
            imgs[3] = { sampler, emissiveView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 3
            imgs[4] = { sampler, normView,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 4 (Normal)
            imgs[5] = { sampler, aoView,       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; // Binding 5 (AO)

            VkWriteDescriptorSet w[6]{}; // --- 【关键修改：数组大小改成 6】 ---
            for (int j = 0; j < 6; ++j) { // --- 【关键修改：循环条件改成 j < 6】 ---
                w[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[j].dstSet = ds;
                w[j].dstBinding = (uint32_t)j; // 这会自动生成 0, 1, 2, 3, 4, 5
                w[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[j].descriptorCount = 1;
                w[j].pImageInfo = &imgs[j];
            }

            // --- 【关键修改：更新数量改成 6】 ---
            vkUpdateDescriptorSets(mWindow.device, 6, w, 0, nullptr);
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




        // 【新增】：加载并初始化整个天空盒
void InitSkybox() 
{
    // 1. 加载单张十字天空盒图片
    stbi_set_flip_vertically_on_load(0);
    int fullWidth, fullHeight, channels;
    // 换成你的十字图路径
    //stbi_uc* pixels = stbi_load("Assets/Skybox/skybox.png", &fullWidth, &fullHeight, &channels, STBI_rgb_alpha);
    stbi_uc* pixels = stbi_load("Assets/Skybox/StandardCubeMap.png", &fullWidth, &fullHeight, &channels, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("Failed to load cross skybox image!");

    // 计算单个面的大小 (十字图是 4 列 3 行)
    uint32_t faceW = fullWidth / 4;
    uint32_t faceH = fullHeight / 3;
    VkDeviceSize layerSize = faceW * faceH * 4;
    VkDeviceSize imageSize = layerSize * 6;

    // 2. 创建 Staging Buffer 并手工“切图”
    lut::Buffer stgBuf = lut::create_buffer(mAllocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    void* data;
    vmaMapMemory(mAllocator.allocator, stgBuf.allocation, &data);
    stbi_uc* dst = static_cast<stbi_uc*>(data);

   

// 定义 Vulkan 6个面在十字图中的 (列, 行) 坐标
    // 顺序必须是: +X(右), -X(左), +Y(上), -Y(下), +Z(前), -Z(后)
    struct FaceCoord { int col, row; };
    FaceCoord coords[6] = {
        {2, 1}, // Right
        {0, 1}, // Left
        {1, 0}, // Top
        {1, 2}, // Bottom
        {1, 1}, // Front
        {3, 1}  // Back
    };
    // 逐个面、逐行拷贝像素
    for (int i = 0; i < 6; i++) {
        int startX = coords[i].col * faceW;
        int startY = coords[i].row * faceH;
        stbi_uc* faceDst = dst + (i * layerSize);

        for (uint32_t y = 0; y < faceH; y++) {
            stbi_uc* srcRow = pixels + ((startY + y) * fullWidth + startX) * 4;
            stbi_uc* dstRow = faceDst + (y * faceW) * 4;
            memcpy(dstRow, srcRow, faceW * 4);
        }
    }

    vmaUnmapMemory(mAllocator.allocator, stgBuf.allocation);
    stbi_image_free(pixels); // 释放原始图片内存

    // ==========================================
    // ⚠️ 注意：接下来的第 3 步创建 Image 时，
    // imgInfo.extent 必须改成单个面的尺寸：
    // imgInfo.extent = { faceW, faceH, 1 }; 
    // ==========================================

  // 3. 创建 Cube Image (必须带 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
    VkImageCreateInfo imgInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_SRGB;

    // 【致命修复】：把原来的 width 和 height 换成刚才算好的 faceW 和 faceH！
    // 因为对于 Vulkan 的 Cube Image 来说，它需要知道的是【单个面】的尺寸！
    imgInfo.extent = { faceW, faceH, 1 };

    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 6;
  
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocInfo{ .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    VkImage rawImg; VmaAllocation rawAlloc;
    vmaCreateImage(mAllocator.allocator, &imgInfo, &allocInfo, &rawImg, &rawAlloc, nullptr);
    mSkyboxTex = lut::Image(mAllocator.allocator, rawImg, rawAlloc);

    // 4. 录制命令拷贝数据
    VkCommandBuffer cmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    // 【修复】：显式创建 Range 结构体
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 6;

    // 第一次屏障：UNDEFINED -> TRANSFER_DST
    lut::image_barrier(cmd, mSkyboxTex.image,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        range);
    
    std::vector<VkBufferImageCopy> regions;
    for (uint32_t i = 0; i < 6; i++) {
        VkBufferImageCopy region{};
        region.bufferOffset = layerSize * i;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.baseArrayLayer = i;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = imgInfo.extent;
        regions.push_back(region);
    }
    vkCmdCopyBufferToImage(cmd, stgBuf.buffer, mSkyboxTex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions.data());

    // 第二次屏障：TRANSFER_DST -> SHADER_READ_ONLY
    lut::image_barrier(cmd, mSkyboxTex.image,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        range);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(mWindow.graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(mWindow.graphicsQueue);

    // 5. 创建 Cube ImageView
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = mSkyboxTex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // 关键！
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 6;
    VkImageView rawView;
    vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
    mSkyboxView = lut::ImageView(mWindow.device, rawView);

    // 6. 创建 1x1x1 的极简 VBO
    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
    };
    VkDeviceSize vboSize = sizeof(skyboxVertices);

    mSkyboxVBO = lut::create_buffer(mAllocator, vboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    lut::Buffer stgVbo = lut::create_buffer(mAllocator, vboSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

    void* vboData;
    vmaMapMemory(mAllocator.allocator, stgVbo.allocation, &vboData);
    memcpy(vboData, skyboxVertices, (size_t)vboSize);
    vmaUnmapMemory(mAllocator.allocator, stgVbo.allocation);

    // 【关键修复】：正式将顶点数据推送到 GPU
    VkCommandBuffer cmdVbo = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
    VkCommandBufferBeginInfo biVbo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    biVbo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdVbo, &biVbo);

    VkBufferCopy copyRegion{};
    copyRegion.size = vboSize;
    vkCmdCopyBuffer(cmdVbo, stgVbo.buffer, mSkyboxVBO.buffer, 1, &copyRegion);

    lut::buffer_barrier(cmdVbo, mSkyboxVBO.buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

    vkEndCommandBuffer(cmdVbo);

    VkSubmitInfo siVbo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    siVbo.commandBufferCount = 1;
    siVbo.pCommandBuffers = &cmdVbo;
    vkQueueSubmit(mWindow.graphicsQueue, 1, &siVbo, VK_NULL_HANDLE);
    vkQueueWaitIdle(mWindow.graphicsQueue);
    
    // 7. 在 setup.cpp 中获取并调用 Layout 和 Pipeline (见下一步)
    mSkyboxDescLayout = create_skybox_descriptor_layout(mWindow);
    mSkyboxPipeLayout = create_skybox_pipeline_layout(mWindow, mSkyboxDescLayout.handle);
    mSkyboxPipe = create_skybox_pipeline(mWindow, mSkyboxPipeLayout.handle, mWindow.swapchainFormat);

    // 8. 填充描述符
    mSkyboxDescSet = lut::alloc_desc_set(mWindow, mDescPool.handle, mSkyboxDescLayout.handle);
    VkDescriptorBufferInfo uboInfo{ mSceneUBO.buffer, 0, VK_WHOLE_SIZE };
// --- 修改为： ---
    VkDescriptorImageInfo descImgInfo{ mDefaultSampler.handle, mSkyboxView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mSkyboxDescSet; writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1; writes[0].pBufferInfo = &uboInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mSkyboxDescSet; writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1; 
    writes[1].pImageInfo = &descImgInfo; // <--- 名字换成 descImgInfo！

    vkUpdateDescriptorSets(mWindow.device, 2, writes, 0, nullptr);
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

        void UploadSkinningBuffers(const EngineMesh& mesh, uint32_t meshIdx)
        {
            if (mesh.jointIndices.empty() || mesh.jointWeights.empty()) return;

            VkCommandBuffer uploadCmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(uploadCmd, &bi);

            VkDeviceSize jSz = mesh.jointIndices.size() * sizeof(glm::uvec4);
            VkDeviceSize wSz = mesh.jointWeights.size() * sizeof(glm::vec4);

            auto mkGpu = [&](VkDeviceSize sz, VkBufferUsageFlags usage) {
                return lut::create_buffer(mAllocator, sz,
                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
                };
            auto mkStg = [&](VkDeviceSize sz) {
                return lut::create_buffer(mAllocator, sz,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                };

            lut::Buffer jGpu = mkGpu(jSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            lut::Buffer wGpu = mkGpu(wSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            lut::Buffer jStg = mkStg(jSz);
            lut::Buffer wStg = mkStg(wSz);

            auto up = [&](lut::Buffer& b, const void* src, VkDeviceSize sz) {
                void* ptr;
                vmaMapMemory(mAllocator.allocator, b.allocation, &ptr);
                std::memcpy(ptr, src, static_cast<std::size_t>(sz));
                vmaUnmapMemory(mAllocator.allocator, b.allocation);
                };
            up(jStg, mesh.jointIndices.data(), jSz);
            up(wStg, mesh.jointWeights.data(), wSz);

            VkBufferCopy cj{ 0, 0, jSz }, cw{ 0, 0, wSz };
            vkCmdCopyBuffer(uploadCmd, jStg.buffer, jGpu.buffer, 1, &cj);
            vkCmdCopyBuffer(uploadCmd, wStg.buffer, wGpu.buffer, 1, &cw);

            lut::buffer_barrier(uploadCmd, jGpu.buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, wGpu.buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);

            vkEndCommandBuffer(uploadCmd);

            VkCommandBufferSubmitInfo ci{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            ci.commandBuffer = uploadCmd;
            VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            si.commandBufferInfoCount = 1; si.pCommandBufferInfos = &ci;
            vkQueueSubmit2(mWindow.graphicsQueue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(mWindow.graphicsQueue);

            mMeshJointIndices.emplace(meshIdx, std::move(jGpu));
            mMeshJointWeights.emplace(meshIdx, std::move(wGpu));
            glfwPollEvents();
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
        lut::Pipeline mShadowSkinnedPipe;
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


        // =========================================================
        // 天空盒资源 (Skybox Resources)
        // =========================================================
        lut::Image               mSkyboxTex;
        lut::ImageView           mSkyboxView;
        lut::Buffer              mSkyboxVBO;
        lut::DescriptorSetLayout mSkyboxDescLayout;
        lut::PipelineLayout      mSkyboxPipeLayout;
        lut::Pipeline            mSkyboxPipe;
        VkDescriptorSet          mSkyboxDescSet = VK_NULL_HANDLE;

        lut::Pipeline mThumbnailAlphaPipe;

        // Samplers
        lut::Sampler mDefaultSampler, mDebugSampler;
        lut::Sampler mPostSampler, mShadowSampler;

        // Mesh GPU buffers
        std::vector<lut::Buffer> mMeshPositions;
        std::vector<lut::Buffer> mMeshTexCoords;
        std::vector<lut::Buffer> mMeshNormals;
        std::vector<lut::Buffer> mMeshIndices;

        // Skinning vertex buffers 
        std::unordered_map<uint32_t, lut::Buffer> mMeshJointIndices;
        std::unordered_map<uint32_t, lut::Buffer> mMeshJointWeights;

        // Skeletal animation / skinning GPU resources
        static constexpr size_t kMaxBoneMatrices = 16 * 128; // 16 entities * 128 joints
        lut::Buffer              mBoneSSBO;           // host-visible, updated each frame
        lut::DescriptorSetLayout mBoneLayout;
        VkDescriptorSet          mBoneDescriptorSet = VK_NULL_HANDLE;
        lut::PipelineLayout      mSkinnedPipeLayout;
        lut::Pipeline            mSkinnedPipe;
        lut::Pipeline            mSkinnedAlphaPipe;

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
        UIManager* mRuntimeUiManager = nullptr;
        // RenderSystem ?????? UI ?????????? RuntimeUiController?
        std::shared_ptr<ImGuiPreviewRenderer> mRuntimeUiRenderer;
        std::unique_ptr<RuntimeUiController> mRuntimeUiController;
        float mRuntimeUiLapTimeSeconds = 0.0f;
        float mRuntimeUiTravelDistanceMeters = 0.0f;
        UIElementId mRuntimeUiDebugSelectedElementId = 0;
        std::string mRuntimeUiDebugSelectedScreenName;
        bool mRuntimeUiDebugEnablePicking = false;
        bool mRuntimeUiDebugShowLivePreview = false;
        bool mRuntimeUiDebugShowStack = false;
        bool mRuntimeUiDebugShowBounds = false;
        bool mRuntimeUiDebugShowHitRects = false;
        bool mRuntimeUiDebugShowBindingValues = true;
        bool mRuntimeUiDebugShowAnimationDebug = false;
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
        // =========================================================
        // (Speed Post-Process) 的句柄和资源
        // =========================================================
        lut::ImageWithView mCompositeOutputImage; // 存放 Composite 合成结果的中间缓冲
        lut::Pipeline mSpeedPostPipe;
        lut::PipelineLayout mSpeedPostPipeLayout;
        std::vector<VkDescriptorSet> mSpeedPostDescriptors;

		// =========================================================
		//ssr资源
		// =========================================================
        // =========================================================
        // SSR (Screen Space Reflection) 资源
        // =========================================================
        lut::ImageWithView mNormalImage;        // 存法线和粗糙度的 G-Buffer
        lut::ImageWithView mSsrOutputImage;     // 存 SSR 计算出的反射画面
        lut::DescriptorSetLayout mSsrDescLayout;
        lut::PipelineLayout mSsrPipeLayout;
        lut::Pipeline mSsrPipe;
        std::vector<VkDescriptorSet> mSsrDescriptors;

        // =========================================================
        // 【新增】SSAO (Screen Space Ambient Occlusion) 资源
        // =========================================================
        // 创建 SSAO 的单通道 R8_UNORM 渲染目标
       

                lut::ImageWithView mSsaoRawImage;       // 存 SSAO 计算出的原始黑白遮蔽图
                lut::DescriptorSetLayout mSsaoDescLayout;
                lut::PipelineLayout mSsaoPipeLayout;
                lut::Pipeline mSsaoPipe;
                std::vector<VkDescriptorSet> mSsaoDescriptors;
                engine::SSAOResources mSsaoResources;   // 我们在 SSAO.hpp 里打包好的噪声图和半球核


        // 【新增】：构建 SSR 的描述符
        VkDescriptorSet BuildSsrDesc(VkImageView colorView, VkImageView depthView, VkImageView normalView, VkBuffer uboBuffer) {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mSsrDescLayout.handle);

            VkDescriptorImageInfo imgs[3]{};
            imgs[0] = { mPostSampler.handle, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mPostSampler.handle, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mPostSampler.handle, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo ubo{ uboBuffer, 0, VK_WHOLE_SIZE };

            VkWriteDescriptorSet w[4]{};
            // 0: Scene Color
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };
            // 1: Depth
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };
            // 2: Normal
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };
            // 3: Scene UBO (为了拿到相机的投影和反投影矩阵)
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubo, nullptr };

            vkUpdateDescriptorSets(mWindow.device, 4, w, 0, nullptr);
            return ds;
        }
        // 【新增】：构建 SSAO 的描述符
        VkDescriptorSet BuildSsaoDesc(VkImageView depthView, VkImageView normalView, VkBuffer uboBuffer) {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mSsaoDescLayout.handle);

            VkDescriptorImageInfo imgs[3]{};
            // 0: Depth
            imgs[0] = { mPostSampler.handle, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            // 1: Normal
            imgs[1] = { mPostSampler.handle, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            // 2: Noise Texture (极其关键：必须使用我们自己在 SSAO.cpp 里创建的 REPEAT 模式采样器)
            imgs[2] = { mSsaoResources.noiseSampler, mSsaoResources.noiseImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkDescriptorBufferInfo ubos[2]{};
            // 3: Scene UBO (相机矩阵)
            ubos[0] = { uboBuffer, 0, sizeof(glsl::SceneUniform) };
            // 【关键修复】：绝对不要用 VK_WHOLE_SIZE！精准指定 64 个 vec4 的大小 (1024 字节)
            ubos[1] = { mSsaoResources.kernelBuffer.buffer, 0, sizeof(glm::vec4) * 64 };
            VkWriteDescriptorSet w[5]{};
            w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[0], nullptr, nullptr };
            w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[1], nullptr, nullptr };
            w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[2], nullptr, nullptr };
            w[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubos[0], nullptr };
            w[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ds, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubos[1], nullptr };

            vkUpdateDescriptorSets(mWindow.device, 5, w, 0, nullptr);
            return ds;
        }
        private:
            // 存储每个模型专属的照片
            std::unordered_map<std::string, ThumbnailAsset> mThumbnailAssets;
            std::unordered_map<std::string, ThumbnailAsset> mContentBrowserImageAssets; //缓存内容浏览器按路径加载的普通图片

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
            bool TryLoadModelThumbnailFromCache(const std::string& modelPath);
            void WarmModelThumbnailCache();
            void PreloadModelForPreview(const std::string& path);

            // 【新增】：为极速特效构建 Descriptor Set
            VkDescriptorSet BuildSpeedDesc(VkImageView inputView) {
                // 复用 mBlurDescLayout，因为它也是一个 Binding 0 的 Sampler
                VkDescriptorSet ds = lut::alloc_desc_set(mWindow, mDescPool.handle, mBlurDescLayout.handle);

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

    public:

        VkDescriptorSet GetModelThumbnail(const std::string& modelPath);
        VkDescriptorSet GetContentBrowserThumbnail(const std::string& assetPath);
        void SetModelPreview(const std::string& path, const glm::mat4& transform);
        void ClearModelPreview();
        DebugRenderer mDebugRenderer;
        // trigger
        TriggerSystem mTriggerSystem;
    
    };

#include "../UI/RenderSystemRuntimeUi.inl"
} // namespace engine