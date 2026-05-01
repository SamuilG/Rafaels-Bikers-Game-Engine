#include "../Renderer/RenderSystem.hpp"
#include "../Scene/model_loader/stb_image_write.h"

#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace engine {

    namespace {
        namespace fs = std::filesystem;

        // 缩略图统一配置：
        // 1. 分辨率拉到 512，避免内容浏览器里看起来太糊
        // 2. 使用 RGBA8，方便直接读回 CPU 并保存为 PNG
        constexpr uint32_t kThumbnailSize = 512;
        constexpr VkFormat kThumbnailFormat = VK_FORMAT_R8G8B8A8_UNORM;
        // 缓存版本号参与哈希命名，用来强制旧缓存失效
        constexpr std::string_view kThumbnailCacheVersion = "thumb_v2_512";

        // 用于判断磁盘缓存是否仍然对应当前源模型文件
        struct ThumbnailSourceStamp {
            std::string normalizedPath;
            std::uintmax_t fileSize = 0;
            std::int64_t writeTime = 0;
        };

        // 统一小写扩展名判断
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
                });
            return value;
        }

        // 规范化路径，确保同一资源在哈希和 meta 里使用统一表示
        std::string NormalizePathString(const std::string& value) {
            fs::path normalized = fs::path(value).lexically_normal();
            return normalized.generic_string();
        }

        // 常见贴图格式
        bool IsTexturePreviewExtension(const fs::path& path) {
            const std::string ext = ToLowerCopy(path.extension().string());
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
        }

        // 模型缩略图目前只处理 .glb
        bool IsModelPreviewExtension(const fs::path& path) {
            return ToLowerCopy(path.extension().string()) == ".glb";
        }

        // 根据所有 batch 的顶点包围盒估算缩略图相机位置
        bool ComputePreviewBounds(const std::vector<RenderBatch>& batches, const EngineModel& model, glm::vec3& outMin, glm::vec3& outMax) {
            outMin = glm::vec3(FLT_MAX);
            outMax = glm::vec3(-FLT_MAX);

            bool hasVertex = false;
            for (const auto& batch : batches) {
                if (batch.meshIndex >= model.meshes.size()) continue;

                const auto& mesh = model.meshes[batch.meshIndex];
                if (mesh.positions.empty()) continue;

                for (const auto& p : mesh.positions) {
                    const glm::vec4 worldPos = batch.transform * glm::vec4(p, 1.0f);
                    const glm::vec3 v = glm::vec3(worldPos);
                    outMin = glm::min(outMin, v);
                    outMax = glm::max(outMax, v);
                    hasVertex = true;
                }
            }

            return hasVertex;
        }

        // FNV-1a 64-bit：用于给缩略图缓存文件生成稳定短文件名
        std::uint64_t HashFNV1a64(std::string_view value) {
            std::uint64_t hash = 14695981039346656037ull;
            for (unsigned char ch : value) {
                hash ^= ch;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        // 缩略图缓存 key 基于：规范化路径 + 缓存版本号
        // 这样一旦分辨率或生成策略变化，可以整体切换到新缓存
        std::string MakeThumbnailCacheKey(const std::string& normalizedPath) {
            std::ostringstream oss;
            oss << std::hex << HashFNV1a64(normalizedPath + "#" + std::string(kThumbnailCacheVersion));
            return oss.str();
        }

        // 缩略图缓存目录：PNG + meta 都落在这里
        fs::path GetThumbnailCacheDirectory() {
            return fs::path("Intermediate") / "ThumbnailCache";
        }

        // 缓存缩略图图片路径
        fs::path GetThumbnailCacheImagePath(const std::string& cacheKey) {
            return GetThumbnailCacheDirectory() / (cacheKey + ".png");
        }

        // 缓存元数据路径，记录源文件路径/大小/修改时间
        fs::path GetThumbnailCacheMetaPath(const std::string& cacheKey) {
            return GetThumbnailCacheDirectory() / (cacheKey + ".meta");
        }

        // 采集当前源模型文件的“指纹”，用于缓存失效判断
        bool TryBuildSourceStamp(const std::string& modelPath, ThumbnailSourceStamp& outStamp) {
            const fs::path path(modelPath);
            if (!fs::exists(path) || !fs::is_regular_file(path)) return false;

            outStamp.normalizedPath = NormalizePathString(modelPath);
            outStamp.fileSize = fs::file_size(path);
            outStamp.writeTime = static_cast<std::int64_t>(fs::last_write_time(path).time_since_epoch().count());
            return true;
        }

        // 读取 sidecar meta 文件
        bool ReadThumbnailMeta(const fs::path& metaPath, ThumbnailSourceStamp& outStamp) {
            std::ifstream input(metaPath);
            if (!input) return false;

            std::string line;
            while (std::getline(input, line)) {
                const std::size_t equalsPos = line.find('=');
                if (equalsPos == std::string::npos) continue;

                const std::string key = line.substr(0, equalsPos);
                const std::string value = line.substr(equalsPos + 1);

                if (key == "path") {
                    outStamp.normalizedPath = value;
                }
                else if (key == "size") {
                    outStamp.fileSize = static_cast<std::uintmax_t>(std::stoull(value));
                }
                else if (key == "write_time") {
                    outStamp.writeTime = static_cast<std::int64_t>(std::stoll(value));
                }
            }

            return !outStamp.normalizedPath.empty();
        }

        // 写入 sidecar meta 文件
        bool WriteThumbnailMeta(const fs::path& metaPath, const ThumbnailSourceStamp& stamp) {
            fs::create_directories(metaPath.parent_path());

            std::ofstream output(metaPath, std::ios::trunc);
            if (!output) return false;

            output << "path=" << stamp.normalizedPath << '\n';
            output << "size=" << stamp.fileSize << '\n';
            output << "write_time=" << stamp.writeTime << '\n';
            return output.good();
        }

        // 判断缓存 meta 是否仍然匹配当前模型文件
        bool SourceStampMatches(const ThumbnailSourceStamp& lhs, const ThumbnailSourceStamp& rhs) {
            return lhs.normalizedPath == rhs.normalizedPath
                && lhs.fileSize == rhs.fileSize
                && lhs.writeTime == rhs.writeTime;
        }

        // 如需手动翻转读回像素，可在这里做行交换。
        // 当前缩略图缓存改为“按原始读回方向写盘”，
        // 后续读取缓存时也按同样方向上传，保证磁盘文件和浏览器显示一致。
        void FlipRgbaRowsInPlace(std::vector<std::uint8_t>& pixels, uint32_t width, uint32_t height) {
            if (pixels.empty()) return;

            const std::size_t stride = static_cast<std::size_t>(width) * 4u;
            std::vector<std::uint8_t> row(stride);

            for (uint32_t y = 0; y < height / 2; ++y) {
                std::uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * stride;
                std::uint8_t* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * stride;

                std::memcpy(row.data(), top, stride);
                std::memcpy(top, bottom, stride);
                std::memcpy(bottom, row.data(), stride);
            }
        }
    }

    // 初始化缩略图离屏渲染所需的深度资源
    void RenderSystem::InitThumbnailPipeline() {
        VkImageCreateInfo dImageInfo{};
        dImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dImageInfo.imageType = VK_IMAGE_TYPE_2D;
        dImageInfo.format = cfg::kDepthFormat;
        dImageInfo.extent = { kThumbnailSize, kThumbnailSize, 1 };
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

        VkCommandBuffer cmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        lut::image_barrier(cmd, mThumbnailDepthImg.image,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthRange);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd };
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &csi, 0, nullptr };
        vkQueueSubmit2(mWindow.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(mWindow.graphicsQueue);
    }

    // 尝试从磁盘缓存恢复模型缩略图：
    // 1. 检查 PNG 和 meta 是否存在
    // 2. 检查 meta 是否仍然匹配当前模型文件
    // 3. 若匹配则直接加载 PNG 进 GPU，避免重新渲染
    bool RenderSystem::TryLoadModelThumbnailFromCache(const std::string& modelPath) {
        if (mThumbnailAssets.count(modelPath)) return true;

        ThumbnailSourceStamp currentStamp{};
        if (!TryBuildSourceStamp(modelPath, currentStamp)) return false;

        const std::string cacheKey = MakeThumbnailCacheKey(currentStamp.normalizedPath);
        const fs::path imagePath = GetThumbnailCacheImagePath(cacheKey);
        const fs::path metaPath = GetThumbnailCacheMetaPath(cacheKey);
        if (!fs::exists(imagePath) || !fs::exists(metaPath)) return false;

        ThumbnailSourceStamp cachedStamp{};
        if (!ReadThumbnailMeta(metaPath, cachedStamp) || !SourceStampMatches(cachedStamp, currentStamp)) {
            return false;
        }

        try {
            // 缩略图缓存 PNG 读取时不走默认磁盘贴图加载函数，
            // 因为通用路径会在内部强制做一次垂直翻转。
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(0);
            stbi_uc* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);
            stbi_set_flip_vertically_on_load(0);
            if (!pixels) return false;

            lut::Image image = lut::load_image_texture2d_from_memory(
                pixels,
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                mWindow,
                mCmdPool.handle,
                mAllocator,
                kThumbnailFormat);
            stbi_image_free(pixels);

            ThumbnailAsset asset{};
            asset.image = std::move(image);
            asset.view = lut::create_image_view_texture2d(mWindow, asset.image.image, kThumbnailFormat);
            asset.guiSet = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, asset.view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            mThumbnailAssets[modelPath] = std::move(asset);
            return true;
        }
        catch (...) {
            stbi_set_flip_vertically_on_load(0);
            return false;
        }
    }

    // 启动时只“预热已有缓存”，不强制重渲所有模型。
    // 这样启动更稳，也避免浏览器一打开就大量吃 GPU 资源。
    void RenderSystem::WarmModelThumbnailCache() {
        namespace fs = std::filesystem;
        const fs::path modelFolder = fs::path("Assets") / "Models";
        if (!fs::exists(modelFolder)) return;

        for (const auto& entry : fs::directory_iterator(modelFolder)) {
            if (!entry.is_regular_file() || !IsModelPreviewExtension(entry.path())) continue;

            const std::string modelPath = entry.path().generic_string();
            TryLoadModelThumbnailFromCache(modelPath);
        }
    }

    // 生成模型缩略图输出 Vulkan 图像资源
    // 新逻辑：
    // 1. 缩略图渲染使用临时 GPU 资源，不污染主场景资源数组
    // 2. 渲染后读回 CPU，保存成 PNG + meta
    // 3. 最终保留一张给 ImGui 用的小图资源
    void RenderSystem::GenerateModelThumbnail(const std::string& modelPath) {
        if (mThumbnailAssets.count(modelPath)) return;
        if (TryLoadModelThumbnailFromCache(modelPath)) return;

        ThumbnailSourceStamp sourceStamp{};
        if (!TryBuildSourceStamp(modelPath, sourceStamp)) return;

        EngineModel previewModel = load_engine_model_glb(modelPath.c_str());

        std::vector<RenderBatch> batches;
        batches.reserve(previewModel.scenes.size());
        for (const auto& instance : previewModel.scenes) {
            RenderBatch batch{};
            batch.meshIndex = instance.meshIndex;
            batch.transform = instance.transform;
            if (batch.meshIndex >= previewModel.meshes.size()) continue;

            batch.materialIndex = previewModel.meshes[batch.meshIndex].materialIndex;
            batches.push_back(batch);
        }

        if (batches.empty()) return;

        glm::vec3 bmin{};
        glm::vec3 bmax{};
        if (!ComputePreviewBounds(batches, previewModel, bmin, bmax)) return;

        // 临时描述符池按当前模型材质数动态分配，
        // 避免材质多的模型在缩略图生成时把池打爆。
        const std::uint32_t previewMaterialSetCount = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(previewModel.materials.size()));
        const std::uint32_t previewDescriptorCount = std::max<std::uint32_t>(32u, previewMaterialSetCount * 6u + 16u);
        lut::DescriptorPool previewDescPool = lut::create_descriptor_pool(mWindow, previewDescriptorCount, previewMaterialSetCount + 4u);
        std::vector<lut::Image> previewTextures;
        std::vector<lut::ImageView> previewTextureViews;
        std::vector<VkDescriptorSet> previewMaterialDescriptors;
        std::vector<lut::Buffer> previewMeshPositions;
        std::vector<lut::Buffer> previewMeshTexCoords;
        std::vector<lut::Buffer> previewMeshNormals;
        std::vector<lut::Buffer> previewMeshIndices;

        previewTextures.reserve(previewModel.textures.size());
        previewTextureViews.reserve(previewModel.textures.size());
        previewMaterialDescriptors.reserve(previewModel.materials.size());
        previewMeshPositions.reserve(previewModel.meshes.size());
        previewMeshTexCoords.reserve(previewModel.meshes.size());
        previewMeshNormals.reserve(previewModel.meshes.size());
        previewMeshIndices.reserve(previewModel.meshes.size());

        // 先把预览模型自己的贴图上传成临时 GPU 纹理
        for (const auto& tex : previewModel.textures) {
            const VkFormat fmt = tex.space == ETextureSpace::srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            previewTextures.emplace_back(
                lut::load_image_texture2d_from_memory(
                    tex.pixels.data(),
                    static_cast<std::uint32_t>(tex.width),
                    static_cast<std::uint32_t>(tex.height),
                    mWindow,
                    mCmdPool.handle,
                    mAllocator,
                    fmt));
            previewTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, previewTextures.back().image, fmt));
        }

        // 为预览模型单独创建材质描述符，和主场景材质池分离
        auto createPreviewMaterialDescriptor = [&](const EngineMaterial& mat) {
            VkDescriptorSet ds = lut::alloc_desc_set(mWindow, previewDescPool.handle, mObjectLayout.handle);

            VkImageView baseView = mDefaultGrayView.handle;
            if (mat.baseColorTexture >= 0 && static_cast<std::size_t>(mat.baseColorTexture) < previewTextureViews.size()) {
                baseView = previewTextureViews[mat.baseColorTexture].handle;
            }

            VkImageView mrView = mDefaultGrayView.handle;
            if (mat.metalRoughTexture >= 0 && static_cast<std::size_t>(mat.metalRoughTexture) < previewTextureViews.size()) {
                mrView = previewTextureViews[mat.metalRoughTexture].handle;
            }

            VkImageView emissiveView = mDefaultBlackView.handle;
            if (mat.emissiveTexture >= 0 && static_cast<std::size_t>(mat.emissiveTexture) < previewTextureViews.size()) {
                emissiveView = previewTextureViews[mat.emissiveTexture].handle;
            }

            VkImageView normalView = mDefaultNormalView.handle;
            if (mat.normalTexture >= 0 && static_cast<std::size_t>(mat.normalTexture) < previewTextureViews.size()) {
                normalView = previewTextureViews[mat.normalTexture].handle;
            }

            VkImageView aoView = mDefaultGrayView.handle;
            if (mat.occlusionTexture >= 0 && static_cast<std::size_t>(mat.occlusionTexture) < previewTextureViews.size()) {
                aoView = previewTextureViews[mat.occlusionTexture].handle;
            }

            VkDescriptorImageInfo imgs[6]{};
            imgs[0] = { mDefaultSampler.handle, baseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[1] = { mDefaultSampler.handle, mrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[2] = { mDefaultSampler.handle, mrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[3] = { mDefaultSampler.handle, emissiveView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[4] = { mDefaultSampler.handle, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            imgs[5] = { mDefaultSampler.handle, aoView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet writes[6]{};
            for (uint32_t i = 0; i < 6; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = ds;
                writes[i].dstBinding = i;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].descriptorCount = 1;
                writes[i].pImageInfo = &imgs[i];
            }

            vkUpdateDescriptorSets(mWindow.device, 6, writes, 0, nullptr);
            return ds;
            };

        // 为预览模型所有材质准备独立描述符
        for (const auto& material : previewModel.materials) {
            previewMaterialDescriptors.push_back(createPreviewMaterialDescriptor(material));
        }

        // 预览 mesh 也走临时 GPU buffer，渲染完后由局部 RAII 自动释放
        auto uploadPreviewMesh = [&](const EngineMesh& mesh) {
            VkCommandBuffer uploadCmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(uploadCmd, &beginInfo);

            const VkDeviceSize posSz = mesh.positions.size() * sizeof(glm::vec3);
            const VkDeviceSize texSz = mesh.texcoords.size() * sizeof(glm::vec2);
            const VkDeviceSize normSz = mesh.normals.size() * sizeof(glm::vec3);
            const VkDeviceSize idxSz = mesh.indices.size() * sizeof(std::uint32_t);

            auto makeGpuBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage) {
                return lut::create_buffer(
                    mAllocator,
                    size,
                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
                };

            previewMeshPositions.emplace_back(makeGpuBuffer(posSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            previewMeshTexCoords.emplace_back(makeGpuBuffer(texSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            previewMeshNormals.emplace_back(makeGpuBuffer(normSz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            previewMeshIndices.emplace_back(makeGpuBuffer(idxSz, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

            auto makeStagingBuffer = [&](VkDeviceSize size) {
                return lut::create_buffer(
                    mAllocator,
                    size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                };

            lut::Buffer posStaging = makeStagingBuffer(posSz);
            lut::Buffer texStaging = makeStagingBuffer(texSz);
            lut::Buffer normStaging = makeStagingBuffer(normSz);
            lut::Buffer idxStaging = makeStagingBuffer(idxSz);

            auto copyToStaging = [&](lut::Buffer& buffer, const void* src, VkDeviceSize size) {
                void* ptr = nullptr;
                vmaMapMemory(mAllocator.allocator, buffer.allocation, &ptr);
                std::memcpy(ptr, src, static_cast<std::size_t>(size));
                vmaUnmapMemory(mAllocator.allocator, buffer.allocation);
                };

            copyToStaging(posStaging, mesh.positions.data(), posSz);
            copyToStaging(texStaging, mesh.texcoords.data(), texSz);
            copyToStaging(normStaging, mesh.normals.data(), normSz);
            copyToStaging(idxStaging, mesh.indices.data(), idxSz);

            auto copyBuffer = [&](lut::Buffer& src, lut::Buffer& dst, VkDeviceSize size) {
                VkBufferCopy region{ 0, 0, size };
                vkCmdCopyBuffer(uploadCmd, src.buffer, dst.buffer, 1, &region);
                };

            copyBuffer(posStaging, previewMeshPositions.back(), posSz);
            copyBuffer(texStaging, previewMeshTexCoords.back(), texSz);
            copyBuffer(normStaging, previewMeshNormals.back(), normSz);
            copyBuffer(idxStaging, previewMeshIndices.back(), idxSz);

            lut::buffer_barrier(uploadCmd, previewMeshPositions.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, previewMeshTexCoords.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, previewMeshNormals.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            lut::buffer_barrier(uploadCmd, previewMeshIndices.back().buffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);

            vkEndCommandBuffer(uploadCmd);

            VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            csi.commandBuffer = uploadCmd;
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &csi;
            vkQueueSubmit2(mWindow.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(mWindow.graphicsQueue);
            };

        // 上传当前模型所有 mesh
        for (const auto& mesh : previewModel.meshes) {
            uploadPreviewMesh(mesh);
        }

        // 根据包围盒自动摆放缩略图相机
        const glm::vec3 center = (bmin + bmax) * 0.5f;
        const glm::vec3 extent = (bmax - bmin) * 0.5f;
        const float radius = std::max({ extent.x, extent.y, extent.z, 0.1f });

        const float fov = glm::radians(60.0f);
        const float dist = std::max(radius / std::tan(fov * 0.5f) + radius * 1.2f, 1.5f);
        const glm::vec3 eye = center + glm::normalize(glm::vec3(1.0f, 0.55f, 1.0f)) * dist;

        glsl::SceneUniform thumbUbo{};
        thumbUbo.camera = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
        thumbUbo.projection = glm::perspective(fov, 1.0f, std::max(0.01f, radius * 0.02f), std::max(dist + radius * 4.0f, 10.0f));
        thumbUbo.projection[1][1] *= -1.0f;
        thumbUbo.projCam = thumbUbo.projection * thumbUbo.camera;
        thumbUbo.cameraPos = glm::vec4(eye, 1.0f);
        thumbUbo.renderMode = 0;
        thumbUbo.lightPos = glm::vec4(center.x + radius * 1.8f, center.y + radius * 2.6f, center.z + radius * 1.8f, 1.0f);
        thumbUbo.lightColor = glm::vec4(6.0f, 6.0f, 6.0f, 1.0f);
        thumbUbo.lightCount = 1;
        thumbUbo.lights[0].position = glm::vec4(center.x + radius * 1.8f, center.y + radius * 2.6f, center.z + radius * 1.8f, 1.0f);
        thumbUbo.lights[0].color = glm::vec4(6.0f, 6.0f, 6.0f, 100.f);

        for (int i = 0; i < 4; ++i) {
            thumbUbo.lightVP[i] = glm::mat4(1.0f);
            thumbUbo.lightVP[i][3][2] = -100.0f;
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = kThumbnailFormat;
        imageInfo.extent = { kThumbnailSize, kThumbnailSize, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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
        viewInfo.format = kThumbnailFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView rawView = VK_NULL_HANDLE;
        vkCreateImageView(mWindow.device, &viewInfo, nullptr, &rawView);
        asset.view = lut::ImageView(mWindow.device, rawView);

        // 用于把 GPU 缩略图读回 CPU，再写成 PNG 缓存
        lut::Buffer readbackBuffer = lut::create_buffer(
            mAllocator,
            static_cast<VkDeviceSize>(kThumbnailSize) * static_cast<VkDeviceSize>(kThumbnailSize) * 4u,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

        VkCommandBuffer cmd = lut::alloc_command_buffer(mWindow, mCmdPool.handle);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        vkBeginCommandBuffer(cmd, &beginInfo);

        lut::buffer_barrier(cmd, mSceneUBO.buffer,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdUpdateBuffer(cmd, mSceneUBO.buffer, 0, sizeof(glsl::SceneUniform), &thumbUbo);
        lut::buffer_barrier(cmd, mSceneUBO.buffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);

        const VkImageSubresourceRange colorRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

        lut::image_barrier(cmd, asset.image.image,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colorRange);

        lut::image_barrier(cmd, mThumbnailDepthImg.image,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthRange);

        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = asset.view.handle;
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };

        VkRenderingAttachmentInfo depthAttach{};
        depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttach.imageView = mThumbnailDepthView.handle;
        depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttach.clearValue.depthStencil = { 1.0f, 0 };

        VkRenderingInfo renderInfo{};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent = { kThumbnailSize, kThumbnailSize };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttach;
        renderInfo.pDepthAttachment = &depthAttach;

        vkCmdBeginRendering(cmd, &renderInfo);

        VkViewport vp{ 0.0f, 0.0f, static_cast<float>(kThumbnailSize), static_cast<float>(kThumbnailSize), 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{ {0, 0}, { kThumbnailSize, kThumbnailSize } };
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout.handle, 0, 1, &mSceneDescriptors, 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mThumbnailAlphaPipe.handle);

        const VkDeviceSize offset = 0;
        // 真正绘制缩略图：push 完整材质常量，保证颜色/粗糙度等和主渲染保持一致
        for (const auto& batch : batches) {
            if (batch.meshIndex >= previewModel.meshes.size()
                || batch.meshIndex >= previewMeshPositions.size()
                || batch.meshIndex >= previewMeshTexCoords.size()
                || batch.meshIndex >= previewMeshNormals.size()
                || batch.meshIndex >= previewMeshIndices.size()
                || batch.materialIndex >= previewMaterialDescriptors.size()) {
                continue;
            }

            PushConstants pc{};
            pc.transform = batch.transform;

            if (batch.materialIndex < previewModel.materials.size()) {
                const auto& material = previewModel.materials[batch.materialIndex];
                pc.baseColorFactor = material.baseColorFactor;
                pc.emissiveFactor = material.emissiveFactor;
                pc.metallicFactor = material.metallicFactor;
                pc.roughnessFactor = material.roughnessFactor;
                pc.alphaCutoff = material.alphaCutoff;
            }
            else {
                pc.baseColorFactor = glm::vec4(1.0f);
                pc.emissiveFactor = glm::vec4(0.0f);
                pc.metallicFactor = 1.0f;
                pc.roughnessFactor = 1.0f;
                pc.alphaCutoff = 0.5f;
            }

            vkCmdPushConstants(cmd, mPipeLayout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout.handle, 1, 1, &previewMaterialDescriptors[batch.materialIndex], 0, nullptr);
            vkCmdBindVertexBuffers(cmd, 0, 1, &previewMeshPositions[batch.meshIndex].buffer, &offset);
            vkCmdBindVertexBuffers(cmd, 1, 1, &previewMeshTexCoords[batch.meshIndex].buffer, &offset);
            vkCmdBindVertexBuffers(cmd, 2, 1, &previewMeshNormals[batch.meshIndex].buffer, &offset);
            vkCmdBindIndexBuffer(cmd, previewMeshIndices[batch.meshIndex].buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(previewModel.meshes[batch.meshIndex].indices.size()), 1, 0, 0, 0);
        }

        vkCmdEndRendering(cmd);

        lut::image_barrier(cmd, asset.image.image,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colorRange);

        VkBufferImageCopy readbackRegion{};
        readbackRegion.bufferOffset = 0;
        readbackRegion.bufferRowLength = 0;
        readbackRegion.bufferImageHeight = 0;
        readbackRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        readbackRegion.imageSubresource.mipLevel = 0;
        readbackRegion.imageSubresource.baseArrayLayer = 0;
        readbackRegion.imageSubresource.layerCount = 1;
        readbackRegion.imageExtent = { kThumbnailSize, kThumbnailSize, 1 };

        vkCmdCopyImageToBuffer(cmd, asset.image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer.buffer, 1, &readbackRegion);

        lut::buffer_barrier(cmd, readbackBuffer.buffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);

        lut::image_barrier(cmd, asset.image.image,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorRange);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, cmd };
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr, 0, 0, nullptr, 1, &csi, 0, nullptr };
        vkQueueSubmit2(mWindow.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(mWindow.graphicsQueue);

        // 读回 RGBA8 像素并写盘为 PNG，同时写 meta
        std::vector<std::uint8_t> pngPixels(static_cast<std::size_t>(kThumbnailSize) * static_cast<std::size_t>(kThumbnailSize) * 4u);
        void* mapped = nullptr;
        if (VK_SUCCESS == vmaMapMemory(mAllocator.allocator, readbackBuffer.allocation, &mapped)) {
            std::memcpy(pngPixels.data(), mapped, pngPixels.size());
            vmaUnmapMemory(mAllocator.allocator, readbackBuffer.allocation);

            const std::string cacheKey = MakeThumbnailCacheKey(sourceStamp.normalizedPath);
            const fs::path imagePath = GetThumbnailCacheImagePath(cacheKey);
            const fs::path metaPath = GetThumbnailCacheMetaPath(cacheKey);
            fs::create_directories(imagePath.parent_path());
            if (0 != stbi_write_png(imagePath.string().c_str(), static_cast<int>(kThumbnailSize), static_cast<int>(kThumbnailSize), 4, pngPixels.data(), static_cast<int>(kThumbnailSize * 4u))) {
                WriteThumbnailMeta(metaPath, sourceStamp);
            }
        }

        // 给 ImGui 注册一张本次运行可直接显示的 GPU 贴图
        asset.guiSet = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, asset.view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mThumbnailAssets[modelPath] = std::move(asset);
    }

    // 预加载模型数据以供预览使用
    void RenderSystem::PreloadModelForPreview(const std::string& path) {
        if (m_previewPrefabCache.count(path)) return;

        EngineModel newModel = load_engine_model_glb(path.c_str());
        uint32_t baseTextureIdx = static_cast<uint32_t>(mModelTextures.size());
        uint32_t baseMaterialIdx = static_cast<uint32_t>(mModel.materials.size());
        uint32_t baseMeshIdx = static_cast<uint32_t>(mModel.meshes.size());

        for (const auto& tex : newModel.textures) {
            const VkFormat fmt = tex.space == ETextureSpace::srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            mModelTextures.emplace_back(lut::load_image_texture2d_from_memory(tex.pixels.data(), tex.width, tex.height, mWindow, mCmdPool.handle, mAllocator, fmt));
            mModelTextureViews.emplace_back(lut::create_image_view_texture2d(mWindow, mModelTextures.back().image, fmt));
        }

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

        for (auto mesh : newModel.meshes) {
            mesh.materialIndex += baseMaterialIdx;
            mModel.meshes.push_back(mesh);
            UploadSingleMesh(mesh);
        }

        std::vector<RenderBatch> batches;
        batches.reserve(newModel.scenes.size());
        for (const auto& instance : newModel.scenes) {
            RenderBatch batch{};
            batch.meshIndex = instance.meshIndex + baseMeshIdx;
            if (batch.meshIndex >= mModel.meshes.size()) continue;

            batch.materialIndex = mModel.meshes[batch.meshIndex].materialIndex;
            batch.transform = instance.transform;
            batches.push_back(batch);
        }

        m_previewPrefabCache[path] = std::move(batches);
    }

    // 设置模型预览
    void RenderSystem::SetModelPreview(const std::string& path, const glm::mat4& transform) {
        m_previewModelPath = path;
        m_previewTransform = transform;
        PreloadModelForPreview(path);
    }

    // 清空当前模型预览
    void RenderSystem::ClearModelPreview() {
        m_previewModelPath.clear();
    }

    // 获取模型缩略图：
    // 1. 先查内存缓存
    // 2. 再查磁盘缓存
    // 3. 最后按需重渲
    VkDescriptorSet RenderSystem::GetModelThumbnail(const std::string& modelPath) {
        auto it = mThumbnailAssets.find(modelPath);
        if (it != mThumbnailAssets.end()) return it->second.guiSet;

        if (TryLoadModelThumbnailFromCache(modelPath)) {
            return mThumbnailAssets[modelPath].guiSet;
        }

        try {
            GenerateModelThumbnail(modelPath);
        }
        catch (const std::exception&) {
            // 缩略图生成失败时退回默认图标，避免整个内容浏览器崩掉
            return GetImGuiTextureDescriptor(cfg::ParticleTextures[0]);
        }

        it = mThumbnailAssets.find(modelPath);
        if (it != mThumbnailAssets.end()) return it->second.guiSet;

        return GetImGuiTextureDescriptor(cfg::ParticleTextures[0]);
    }

    // 获取内容浏览器资源缩略图：
    // 1. 模型走离屏渲染/磁盘缓存路径
    // 2. 普通图片直接加载成小纹理并做内存缓存
    VkDescriptorSet RenderSystem::GetContentBrowserThumbnail(const std::string& assetPath) {
        const fs::path normalizedPath(assetPath);

        if (IsModelPreviewExtension(normalizedPath)) {
            return GetModelThumbnail(assetPath);
        }

        if (!IsTexturePreviewExtension(normalizedPath)) {
            return VK_NULL_HANDLE;
        }

        if (VkDescriptorSet existingDescriptor = GetImGuiTextureDescriptor(assetPath); existingDescriptor != VK_NULL_HANDLE) {
            return existingDescriptor;
        }

        auto existingPreview = mContentBrowserImageAssets.find(assetPath);
        if (existingPreview != mContentBrowserImageAssets.end()) {
            return existingPreview->second.guiSet;
        }

        if (!fs::exists(normalizedPath) || !fs::is_regular_file(normalizedPath)) {
            return VK_NULL_HANDLE;
        }

        stbi_set_flip_vertically_on_load(0);
        lut::Image image = lut::load_image_texture2d(assetPath.c_str(), mWindow, mCmdPool.handle, mAllocator, VK_FORMAT_R8G8B8A8_UNORM);
        stbi_set_flip_vertically_on_load(0);

        // 保存图片资源和 ImGui 句柄，避免每帧重复加载
        ThumbnailAsset previewAsset{};
        previewAsset.image = std::move(image);
        previewAsset.view = lut::create_image_view_texture2d(mWindow, previewAsset.image.image, VK_FORMAT_R8G8B8A8_UNORM);
        previewAsset.guiSet = ImGui_ImplVulkan_AddTexture(mDefaultSampler.handle, previewAsset.view.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto [insertedIt, inserted] = mContentBrowserImageAssets.emplace(assetPath, std::move(previewAsset));
        (void)inserted;
        return insertedIt->second.guiSet;
    }

} // namespace engine
