#include "../Renderer/RenderSystem.hpp"
#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cctype>

namespace engine {

    namespace {
        std::string ToLowerCopy(std::string value) { //统一小写扩展名判断
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

        bool IsTexturePreviewExtension(const std::filesystem::path& path) { //常见贴图格式
            const std::string ext = ToLowerCopy(path.extension().string());
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
        }

        bool IsModelPreviewExtension(const std::filesystem::path& path) { //模型缩略图
            const std::string ext = ToLowerCopy(path.extension().string());
            return ext == ".glb";
        }

        bool ComputePreviewBounds(const std::vector<RenderBatch>& batches, const EngineModel& model, glm::vec3& outMin, glm::vec3& outMax) {
            outMin = glm::vec3(FLT_MAX); outMax = glm::vec3(-FLT_MAX);
            bool hasVertex = false;
            for (const auto& batch : batches) {
                if (batch.meshIndex >= model.meshes.size()) continue;
                const auto& mesh = model.meshes[batch.meshIndex];
                if (mesh.positions.empty()) continue;
                for (const auto& p : mesh.positions) {
                    glm::vec4 wp = batch.transform * glm::vec4(p, 1.0f);
                    glm::vec3 v = glm::vec3(wp);
                    outMin = glm::min(outMin, v); outMax = glm::max(outMax, v);
                    hasVertex = true;
                }
            }
            return hasVertex;
        }
    }

    void RenderSystem::InitThumbnailPipeline() {
        VkImageCreateInfo dImageInfo{};
        dImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dImageInfo.imageType = VK_IMAGE_TYPE_2D;
        dImageInfo.format = cfg::kDepthFormat;
        dImageInfo.extent = { 256, 256, 1 };
        dImageInfo.mipLevels = 1;
        dImageInfo.arrayLayers = 1;
        dImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        dImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        dImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo dAllocInfo{};
        dAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage rawDepthImage = VK_NULL_HANDLE;
        VmaAllocation rawDepthAlloc = VK_NULL_HANDLE;
        vmaCreateImage(mAllocator.allocator, &dImageInfo, &dAllocInfo, &rawDepthImage, &rawDepthAlloc, nullptr);
        mThumbnailDepthImg = lut::Image(mAllocator.allocator, rawDepthImage, rawDepthAlloc);

        VkImageViewCreateInfo dviewInfo{};
        dviewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dviewInfo.image = mThumbnailDepthImg.image;
        dviewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dviewInfo.format = cfg::kDepthFormat;
        dviewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dviewInfo.subresourceRange.baseMipLevel = 0;
        dviewInfo.subresourceRange.levelCount = 1;
        dviewInfo.subresourceRange.baseArrayLayer = 0;
        dviewInfo.subresourceRange.layerCount = 1;

        VkImageView rawDepthView = VK_NULL_HANDLE;
        vkCreateImageView(mWindow.device, &dviewInfo, nullptr, &rawDepthView);
        mThumbnailDepthView = lut::ImageView(mWindow.device, rawDepthView);
    }

	// 生成模型缩略图输出 Vulkan 图像资源// Generate model thumbnail output Vulkan image resource
    void RenderSystem::GenerateModelThumbnail(const std::string& modelPath) {
        if (mThumbnailAssets.count(modelPath)) return;
        auto it = m_previewPrefabCache.find(modelPath);
        if (it == m_previewPrefabCache.end()) return;
        const auto& batches = it->second;
        if (batches.empty()) return;

        glm::vec3 bmin, bmax;
        if (!ComputePreviewBounds(batches, mModel, bmin, bmax)) return;

        glm::vec3 center = (bmin + bmax) * 0.5f;
        glm::vec3 extent = (bmax - bmin) * 0.5f;
        float radius = std::max({ extent.x, extent.y, extent.z, 0.1f });

        float fov = glm::radians(60.0f);
        float dist = std::max(radius / std::tan(fov * 0.5f) + radius * 1.2f, 1.5f);
        glm::vec3 eye = center + glm::normalize(glm::vec3(1.0f, 0.55f, 1.0f)) * dist;

        glsl::SceneUniform thumbUbo{};
        thumbUbo.camera = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        thumbUbo.projection = glm::perspective(fov, 1.0f, std::max(0.01f, radius * 0.02f), std::max(dist + radius * 4.0f, 10.0f));
        thumbUbo.projection[1][1] *= -1.0f;
        thumbUbo.projCam = thumbUbo.projection * thumbUbo.camera;
        thumbUbo.cameraPos = glm::vec4(eye, 1.0f);
        thumbUbo.renderMode = 0;
        thumbUbo.lightPos = glm::vec4(center.x + radius * 2.f, center.y + radius * 3.f, center.z + radius * 2.f, 1.0f);
        
        thumbUbo.lightCount = 1;
        thumbUbo.lights[0].position = glm::vec4(center.x + radius * 2.f, center.y + radius * 3.f, center.z + radius * 2.f, 1.0f);
        thumbUbo.lights[0].color = glm::vec4(2.5f, 2.5f, 2.5f, 1.0f); //liangdu
        

        for (int i = 0; i < 4; ++i) {
            thumbUbo.lightVP[i] = glm::mat4(1.0f);
            thumbUbo.lightVP[i][3][2] = -100.0f;
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = { 256, 256, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkImage rawImage = VK_NULL_HANDLE;
        VmaAllocation rawAlloc = VK_NULL_HANDLE;
        vmaCreateImage(mAllocator.allocator, &imageInfo, &allocInfo, &rawImage, &rawAlloc, nullptr);

        ThumbnailAsset asset{};
        asset.image = lut::Image(mAllocator.allocator, rawImage, rawAlloc);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = asset.image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView rawView = VK_NULL_HANDLE;
        vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
        asset.view = lut::ImageView(mWindow.device, rawView);

        VkCommandBuffer cmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(cmd, &bi);

        lut::buffer_barrier(cmd, mSceneUBO.buffer,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        vkCmdUpdateBuffer(cmd, mSceneUBO.buffer, 0, sizeof(glsl::SceneUniform), &thumbUbo);

        lut::buffer_barrier(cmd, mSceneUBO.buffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);

        VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

        lut::image_barrier(cmd, asset.image.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorRange);

        lut::image_barrier(cmd, mThumbnailDepthImg.image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthRange);

        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = asset.view.handle;
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };//背景颜色

        VkRenderingAttachmentInfo depthAttach{};
        depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttach.imageView = mThumbnailDepthView.handle;
        depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent = { 256, 256 };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttach;
        renderInfo.pDepthAttachment = &depthAttach;

        vkCmdBeginRendering(cmd, &renderInfo);

        VkViewport vp{ 0.0f, 0.0f, 256.0f, 256.0f, 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{ {0, 0}, { 256, 256 } };
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout.handle, 0, 1, &mSceneDescriptors, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mThumbnailAlphaPipe.handle);

        VkDeviceSize offset = 0;
        for (const auto& batch : batches) {
            if (batch.meshIndex >= mModel.meshes.size() || batch.materialIndex >= mMaterialDescriptors.size()) continue;

            vkCmdPushConstants(cmd, mPipeLayout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &batch.transform);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout.handle, 1, 1, &mMaterialDescriptors[batch.materialIndex], 0, nullptr);

            vkCmdBindVertexBuffers(cmd, 0, 1, &mMeshPositions[batch.meshIndex].buffer, &offset);
            vkCmdBindVertexBuffers(cmd, 1, 1, &mMeshTexCoords[batch.meshIndex].buffer, &offset);
            vkCmdBindVertexBuffers(cmd, 2, 1, &mMeshNormals[batch.meshIndex].buffer, &offset);
            vkCmdBindIndexBuffer(cmd, mMeshIndices[batch.meshIndex].buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mModel.meshes[batch.meshIndex].indices.size()), 1, 0, 0, 0);
        }

        vkCmdEndRendering(cmd);

        lut::image_barrier(cmd, asset.image.image,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorRange);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd };
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &csi, 0, nullptr };
        vkQueueSubmit2(mWindow.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(mWindow.graphicsQueue);

        asset.guiSet = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, asset.view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mThumbnailAssets[modelPath] = std::move(asset);
    }

	// 预加载模型数据以供缩略图和预览使用// Preload model data for thumbnail and preview usage
    void RenderSystem::PreloadModelForPreview(const std::string& path) {
        if (m_previewPrefabCache.count(path)) {
            if (!mThumbnailAssets.count(path)) GenerateModelThumbnail(path);
            return;
        }

        EngineModel newModel = load_engine_model_glb(path.c_str());
        uint32_t baseTextureIdx = static_cast<uint32_t>(mModelTextures.size());
        uint32_t baseMaterialIdx = static_cast<uint32_t>(mModel.materials.size());
        uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

        for (auto const& tex : newModel.textures) {
            VkFormat fmt = (tex.space == ETextureSpace::srgb) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(tex.pixels.data(), tex.width, tex.height, mWindow, mCmdPool.handle, mAllocator, fmt));
            mModelTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, mModelTextures.back().image, fmt));
        }

        for (auto mat : newModel.materials) {
            if (mat.baseColorTexture >= 0)   mat.baseColorTexture += baseTextureIdx;
            if (mat.normalTexture >= 0)      mat.normalTexture += baseTextureIdx;
            if (mat.metalRoughTexture >= 0)  mat.metalRoughTexture += baseTextureIdx;
            if (mat.occlusionTexture >= 0)   mat.occlusionTexture += baseTextureIdx;
            if (mat.emissiveTexture >= 0)    mat.emissiveTexture += baseTextureIdx;
            if (mat.alphaMaskTexture >= 0)   mat.alphaMaskTexture += baseTextureIdx;

            mModel.materials.push_back(mat);
            AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, mat);
            AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, mat);
        }

        for (auto mesh : newModel.meshes) {
            mesh.materialIndex += baseMaterialIdx;
            mModel.meshes.push_back(mesh);
            UploadSingleMesh(mesh);
        }

        std::vector<RenderBatch> batches;
        batches.reserve(newModel.scenes.size());

        for (auto& instance : newModel.scenes) {
            RenderBatch b{};
            b.meshIndex = instance.meshIndex + baseMeshIdx;
            b.materialIndex = mModel.meshes[b.meshIndex].materialIndex;
            b.transform = instance.transform;
            batches.push_back(b);
        }

        m_previewPrefabCache[path] = std::move(batches);
        GenerateModelThumbnail(path);
    }

	// 设置模型预览// Set model preview
    void RenderSystem::SetModelPreview(const std::string& path, const glm::mat4& transform) {
        m_previewModelPath = path; m_previewTransform = transform; PreloadModelForPreview(path);
    }

    void RenderSystem::ClearModelPreview() { m_previewModelPath.clear(); }

    VkDescriptorSet RenderSystem::GetModelThumbnail(const std::string& modelPath) {
        auto it = mThumbnailAssets.find(modelPath);
        if (it != mThumbnailAssets.end()) return it->second.guiSet;
        PreloadModelForPreview(modelPath);
        it = mThumbnailAssets.find(modelPath);
        if (it != mThumbnailAssets.end()) return it->second.guiSet;
        return GetImGuiTextureDescriptor(cfg::ParticleTextures[0]);
    }

    VkDescriptorSet RenderSystem::GetContentBrowserThumbnail(const std::string& assetPath) { //显示模型和贴图预览
        const std::filesystem::path normalizedPath(assetPath); //保持和 UI 层一致的相对资源路径

        if (IsModelPreviewExtension(normalizedPath)) { //模型缩略图
            return GetModelThumbnail(assetPath);
        }

        if (!IsTexturePreviewExtension(normalizedPath)) { //非贴图文字图标
            return VK_NULL_HANDLE;
        }

        if (VkDescriptorSet existingDescriptor = GetImGuiTextureDescriptor(assetPath); existingDescriptor != VK_NULL_HANDLE) { //ImGui 的贴图直接复用
            return existingDescriptor;
        }

        auto existingPreview = mContentBrowserImageAssets.find(assetPath); //已经懒加载过的贴图预览
        if (existingPreview != mContentBrowserImageAssets.end()) {
            return existingPreview->second.guiSet;
        }

        if (!std::filesystem::exists(normalizedPath) || !std::filesystem::is_regular_file(normalizedPath)) {//文件不存在返回空
            return VK_NULL_HANDLE;
        }

        stbi_set_flip_vertically_on_load(0);
        lut::Image image = lut::load_image_texture2d(assetPath.c_str(), mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM); //加载普通贴图
        stbi_set_flip_vertically_on_load(0);

        ThumbnailAsset previewAsset{}; //保存图片资源和 ImGui 句柄
        previewAsset.image = std::move(image);
        previewAsset.view = lut::create_image_view_texture2d(mWindow, previewAsset.image.image, VK_FORMAT_R8G8B8A8_UNORM);
        previewAsset.guiSet = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, previewAsset.view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto [insertedIt, inserted] = mContentBrowserImageAssets.emplace(assetPath, std::move(previewAsset)); //缓存按路径生成的贴图缩略图
        (void)inserted; 
        return insertedIt->second.guiSet;
    }

} // namespace engine
