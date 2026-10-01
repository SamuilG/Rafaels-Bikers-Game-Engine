#pragma once
#include "../../Rhi/vkimage.hpp"
#include "../../Rhi/vkobject.hpp"
#include <cstdint>

struct IblResources {
    labut2::ImageWithView prefilteredEnvMap;
    labut2::ImageWithView irradianceMap;
    labut2::ImageWithView brdfLut;
    labut2::Sampler sampler;
    std::uint32_t prefilteredMipLevels = 0;
};

struct IblBakeSettings {
    std::uint32_t prefilterSize = 128;
    std::uint32_t irradianceSize = 32;
    std::uint32_t brdfSize = 128;
    std::uint32_t environmentSamples = 512;
    std::uint32_t brdfSamples = 1024;
};

// One-time graphics-queue bake, independent of the swapchain and frame size.
// All outputs are linear RGBA16F; source sRGB decoding happens during sampling.
IblResources create_ibl_resources(const labut2::VulkanContext& context,
    const labut2::Allocator& allocator, VkCommandPool commandPool, VkImageView sourceCube,
    const char* shaderDirectory = "Assets/Shaders/spirv/", const IblBakeSettings& settings = {});
