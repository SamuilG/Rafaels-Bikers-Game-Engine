// Included inside RenderSystem. Model slots and descriptors are device-owned;
// only leases and mutable scene state move between editor and play worlds.
private:
    struct GpuModelAsset {
        std::shared_ptr<const ModelAsset> asset;
        lut::DescriptorPool descriptors;
    };
    struct RetiredModelAsset {
        uint64_t afterSubmission = 0;
        uint64_t bytes = 0;
        std::vector<lut::Image> images;
        std::vector<lut::ImageView> views;
        std::vector<lut::Buffer> buffers;
        lut::DescriptorPool descriptors;
    };
    std::unordered_map<std::string, GpuModelAsset> mSharedModelAssets;
    std::vector<RetiredModelAsset> mRetiredModelAssets;
    std::vector<std::shared_ptr<const ModelAsset>> mWorldModelAssets;
    std::shared_ptr<const ModelAsset> mPreviewModelAsset;
    ModelAssetSlots mTextureSlots, mMaterialSlots, mMeshSlots;
    uint64_t mLastModelSubmission = 0;
    uint64_t mCompletedModelSubmission = 0;
    std::vector<uint64_t> mModelFrameSubmissions;
    uint64_t mModelUploads = 0;
    uint64_t mProceduralAssetId = 0;

    static std::string ModelAssetKey(const std::string& path) {
        const auto file = std::filesystem::weakly_canonical(std::filesystem::u8path(path));
        const auto utf8 = file.generic_u8string();
        std::string key(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#ifdef _WIN32
        // Windows paths are case insensitive. Preserve UTF-8 bytes outside ASCII.
        for (auto& c : key) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
        return key + ":" + std::to_string(std::filesystem::file_size(file)) + ":" +
            std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count());
    }

    uint64_t ModelAllocationBytes(VmaAllocation allocation) const {
        if (!allocation) return 0;
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(mAllocator.allocator, allocation, &info);
        return info.size;
    }

    // Only called for an asset with no entity, preview, or world leases.
    void RetireModelAsset(GpuModelAsset& entry) {
        const auto& asset = *entry.asset;
        RetiredModelAsset retired;
        retired.afterSubmission = mLastModelSubmission;
        retired.bytes = asset.gpuBytes;
        retired.descriptors = std::move(entry.descriptors);
        for (std::size_t i = 0; i < asset.model.textures.size(); ++i) {
            const auto slot = asset.firstTexture + i;
            retired.images.push_back(std::move(mModelTextures[slot]));
            retired.views.push_back(std::move(mModelTextureViews[slot]));
        }
        for (std::size_t i = 0; i < asset.model.meshes.size(); ++i) {
            const auto slot = asset.firstMesh + i;
            retired.buffers.push_back(std::move(mMeshPositions[slot]));
            retired.buffers.push_back(std::move(mMeshTexCoords[slot]));
            retired.buffers.push_back(std::move(mMeshNormals[slot]));
            retired.buffers.push_back(std::move(mMeshIndices[slot]));
            if (auto it = mMeshJointIndices.find(static_cast<uint32_t>(slot)); it != mMeshJointIndices.end()) {
                retired.buffers.push_back(std::move(it->second)); mMeshJointIndices.erase(it);
            }
            if (auto it = mMeshJointWeights.find(static_cast<uint32_t>(slot)); it != mMeshJointWeights.end()) {
                retired.buffers.push_back(std::move(it->second)); mMeshJointWeights.erase(it);
            }
            mModel.meshes[slot] = EngineMesh{};
        }
        for (std::size_t i = 0; i < asset.model.materials.size(); ++i) {
            const auto slot = asset.firstMaterial + i;
            mModel.materials[slot] = EngineMaterial{};
            mMaterialDescriptors[slot] = VK_NULL_HANDLE;
            mDebugMaterialDescriptors[slot] = VK_NULL_HANDLE;
        }
        mTextureSlots.Release(asset.firstTexture, asset.model.textures.size());
        mMaterialSlots.Release(asset.firstMaterial, asset.model.materials.size());
        mMeshSlots.Release(asset.firstMesh, asset.model.meshes.size());
        mRetiredModelAssets.push_back(std::move(retired));
    }

    std::shared_ptr<const ModelAsset> UploadModelAsset(std::string key, EngineModel model) {
        // Keep ownership registered through failed/partial uploads too, so the
        // ordinary retirement path can clean every successfully created object.
        auto asset = std::make_shared<ModelAsset>();
        asset->key = key;
        asset->model = std::move(model);
        const auto textures = asset->model.textures.size();
        const auto materials = asset->model.materials.size();
        const auto meshes = asset->model.meshes.size();
        asset->firstTexture = mTextureSlots.Allocate(textures, mModelTextures.size());
        asset->firstMaterial = mMaterialSlots.Allocate(materials, mModel.materials.size());
        asset->firstMesh = mMeshSlots.Allocate(meshes, mModel.meshes.size());
        mModelTextures.resize(std::max(mModelTextures.size(), asset->firstTexture + textures));
        mModelTextureViews.resize(mModelTextures.size());
        mModel.materials.resize(std::max(mModel.materials.size(), asset->firstMaterial + materials));
        mMaterialDescriptors.resize(mModel.materials.size(), VK_NULL_HANDLE);
        mDebugMaterialDescriptors.resize(mModel.materials.size(), VK_NULL_HANDLE);
        mModel.meshes.resize(std::max(mModel.meshes.size(), asset->firstMesh + meshes));
        mMeshPositions.resize(mModel.meshes.size());
        mMeshTexCoords.resize(mModel.meshes.size());
        mMeshNormals.resize(mModel.meshes.size());
        mMeshIndices.resize(mModel.meshes.size());
        GpuModelAsset entry;
        entry.asset = asset;
        entry.descriptors = lut::create_descriptor_pool(mWindow,
            static_cast<uint32_t>(std::max<std::size_t>(12, materials * 12)),
            static_cast<uint32_t>(std::max<std::size_t>(2, materials * 2)));
        auto [position, inserted] = mSharedModelAssets.emplace(key, std::move(entry));
        try {
            for (std::size_t i = 0; i < textures; ++i) {
                auto& texture = asset->model.textures[i];
                const auto slot = asset->firstTexture + i;
                const auto format = texture.space == ETextureSpace::srgb
                    ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                mModelTextures[slot] = lut::load_image_texture2d_from_memory(texture.pixels.data(),
                    texture.width, texture.height, mWindow, mCmdPool.handle, mAllocator, format);
                asset->gpuBytes += ModelAllocationBytes(mModelTextures[slot].allocation);
                mModelTextureViews[slot] = lut::create_image_view_texture2d(mWindow, mModelTextures[slot].image, format);
                std::vector<uint8_t>().swap(texture.pixels);
                glfwPollEvents();
            }
            for (std::size_t i = 0; i < materials; ++i) {
                auto material = asset->model.materials[i];
                for (int* index : {&material.baseColorTexture, &material.normalTexture,
                    &material.metalRoughTexture, &material.occlusionTexture,
                    &material.emissiveTexture, &material.alphaMaskTexture})
                    if (*index >= 0) *index += asset->firstTexture;
                const auto slot = asset->firstMaterial + static_cast<uint32_t>(i);
                mModel.materials[slot] = material;
                AddOneMaterialDescriptor(mDefaultSampler.handle, mMaterialDescriptors, material,
                    position->second.descriptors.handle, slot);
                AddOneMaterialDescriptor(mDebugSampler.handle, mDebugMaterialDescriptors, material,
                    position->second.descriptors.handle, slot);
            }
            for (std::size_t i = 0; i < meshes; ++i) {
                auto mesh = asset->model.meshes[i];
                mesh.materialIndex += asset->firstMaterial;
                const auto slot = asset->firstMesh + static_cast<uint32_t>(i);
                mModel.meshes[slot] = mesh;
                UploadSingleMesh(mesh, slot);
                if (mesh.isSkinned) UploadSkinningBuffers(mesh, slot);
                for (auto* buffer : {&mMeshPositions[slot], &mMeshTexCoords[slot], &mMeshNormals[slot], &mMeshIndices[slot]})
                    asset->gpuBytes += ModelAllocationBytes(buffer->allocation);
                if (mesh.isSkinned) {
                    asset->gpuBytes += ModelAllocationBytes(mMeshJointIndices.at(slot).allocation);
                    asset->gpuBytes += ModelAllocationBytes(mMeshJointWeights.at(slot).allocation);
                }
            }
            ++mModelUploads;
            ResetPerformanceHistory();
            return asset;
        } catch (...) {
            // Upload helpers wait for their own submissions. Also complete any
            // partial submission before releasing a failed asset's allocations.
            WaitForGpuIdle();
            RetireModelAsset(position->second);
            mSharedModelAssets.erase(position);
            mRetiredModelAssets.clear();
            throw;
        }
    }

public:
    std::shared_ptr<const ModelAsset> AcquireModelAsset(const std::string& path, EngineModel* decoded = nullptr) {
        const auto key = ModelAssetKey(path);
        if (auto it = mSharedModelAssets.find(key); it != mSharedModelAssets.end()) return it->second.asset;
        // Cold imports already synchronously upload. Drain unused old assets
        // before allocating the replacement, avoiding a temporary double budget.
        CollectModelResources(true);
        return UploadModelAsset(key, decoded ? std::move(*decoded) : load_engine_model_glb(path.c_str()));
    }

    // Run after UI deletion/preview cancellation and before recording the next
    // frame. Recorded commands hold raw handles; retired objects outlive every
    // submission that could still use those handles, even if slots are reused.
    void CollectModelResources(bool waitForGpu = false) {
        bool changed = false;
        for (auto it = mSharedModelAssets.begin(); it != mSharedModelAssets.end();) {
            if (it->second.asset.use_count() == 1) {
                RetireModelAsset(it->second);
                it = mSharedModelAssets.erase(it);
                changed = true;
            } else ++it;
        }
        if (waitForGpu) {
            WaitForGpuIdle();
            mCompletedModelSubmission = mLastModelSubmission;
        }
        std::erase_if(mRetiredModelAssets, [&](const auto& retired) {
            return retired.afterSubmission <= mCompletedModelSubmission;
        });
        if (changed) ResetPerformanceHistory();
        if (mState) mState->renderStats.modelResources = GetModelResourceStatistics();
    }

    ModelResourceStatistics GetModelResourceStatistics() const {
        ModelResourceStatistics result;
        result.assets = static_cast<uint32_t>(mSharedModelAssets.size());
        result.pendingAssets = static_cast<uint32_t>(mRetiredModelAssets.size());
        result.uploads = mModelUploads;
        for (const auto& [key, entry] : mSharedModelAssets) {
            result.textures += static_cast<uint32_t>(entry.asset->model.textures.size());
            result.meshes += static_cast<uint32_t>(entry.asset->model.meshes.size());
            result.residentBytes += entry.asset->gpuBytes;
        }
        for (const auto& retired : mRetiredModelAssets) result.pendingBytes += retired.bytes;
        return result;
    }

    std::size_t ModelMeshSlotCount() const { return mModel.meshes.size(); }

    // Allocated bytes rather than VMA's retained heap-block capacity. Used by
    // lifecycle regressions to verify that Vulkan allocations really disappear.
    uint64_t AllocatedGpuResourceBytes() const {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(mAllocator.allocator, &statistics);
        return statistics.total.statistics.allocationBytes;
    }
