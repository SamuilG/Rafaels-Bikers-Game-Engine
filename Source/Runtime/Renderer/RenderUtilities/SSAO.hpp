#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <volk/volk.h>
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
// 引入你引擎的基础头文件，比如包含 lut::VulkanWindow, lut::Buffer, lut::ImageAndView 的定义
namespace lut = labut2;

namespace engine
{
    // 打包 SSAO 需要的静态资源
    struct SSAOResources
    {
        lut::Buffer kernelBuffer;       // 64个采样点的 UBO
        lut::ImageWithView noiseImage;   // 4x4 的随机旋转噪声贴图
        VkSampler noiseSampler;         // 专属的 REPEAT 采样器
    };

    // 声明初始化函数
    // 注意：根据你引擎实际情况，如果上传贴图需要 CommandBuffer 或 Allocator，请在参数里加上
    SSAOResources create_ssao_resources(lut::VulkanWindow const& aWindow, VkCommandPool aCmdPool);

    // 记得在程序退出时销毁资源
    void destroy_ssao_resources(lut::VulkanWindow const& aWindow, SSAOResources& aResources);
}