#include "SSAO.hpp"
#include <random>
// #include <cstring> // 如果需要 memcpy

namespace engine
{
    SSAOResources create_ssao_resources(lut::VulkanWindow const& aWindow, VkCommandPool aCmdPool)
    {
        SSAOResources resources{};
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator;

        // =================================================================
        // 1. 生成半球采样核 (Hemisphere Kernel)
        // ============================================== ===================
        std::vector<glm::vec4> ssaoKernel;
        for (unsigned int i = 0; i < 64; ++i)
        {
            glm::vec3 sample(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) // Z 取正数，代表半球朝上
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);

            // 越靠近原点越密集
            float scale = (float)i / 64.0f;
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            ssaoKernel.push_back(glm::vec4(sample, 0.0f));
        }

        // TODO: 调用你引擎的方法创建 UBO 并把 ssaoKernel.data() 传进显存
        // resources.kernelBuffer = create_buffer(aWindow, sizeof(glm::vec4) * 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT...);
        // upload_data_to_buffer(resources.kernelBuffer, ssaoKernel.data(), ...);


        // =================================================================
        // 2. 生成 4x4 随机噪声图 (Noise Texture)
        // =================================================================
        std::vector<glm::vec4> ssaoNoise;
        for (unsigned int i = 0; i < 16; i++)
        {
            // 绕 Z 轴旋转
            glm::vec3 noise(
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                0.0f);
            ssaoNoise.push_back(glm::vec4(glm::normalize(noise), 0.0f));
        }

        // TODO: 调用你引擎的方法创建 4x4 的 Texture2D
        // resources.noiseImage = create_image_and_view(aWindow, 4, 4, VK_FORMAT_R32G32B32A32_SFLOAT, ...);
        // upload_data_to_image(resources.noiseImage, ssaoNoise.data(), ...);


        // =================================================================
        // 3. 创建 REPEAT 模式的 Sampler
        // =================================================================
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        // 极其重要：必须是 REPEAT 模式，把 4x4 铺满屏幕
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

        if (vkCreateSampler(aWindow.device, &samplerInfo, nullptr, &resources.noiseSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create SSAO noise sampler!");
        }

        return resources;
    }

    void destroy_ssao_resources(lut::VulkanWindow const& aWindow, SSAOResources& aResources)
    {
        // TODO: 调用你引擎的释放内存逻辑
        vkDestroySampler(aWindow.device, aResources.noiseSampler, nullptr);
        // destroy_image_and_view(aWindow, aResources.noiseImage);
        // destroy_buffer(aWindow, aResources.kernelBuffer);
    }
}