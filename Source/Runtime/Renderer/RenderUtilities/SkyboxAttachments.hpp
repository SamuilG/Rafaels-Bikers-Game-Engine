#pragma once
#include <volk/volk.h>
#include <array>

inline constexpr std::array<VkDescriptorSetLayoutBinding, 2> SkyboxDescriptorBindings() {
    return {
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
}

// The skybox shares the scene MRT pass: color, Bloom and world normals.
// Only color is written; the other buffers retain their existing contents.
struct SkyboxAttachments {
    std::array<VkFormat, 3> formats;
    std::array<VkPipelineColorBlendAttachmentState, 3> blends{};

    explicit SkyboxAttachments(VkFormat sceneColorFormat)
        : formats{sceneColorFormat, sceneColorFormat, VK_FORMAT_R16G16B16A16_SFLOAT}
    {
        blends[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
};
