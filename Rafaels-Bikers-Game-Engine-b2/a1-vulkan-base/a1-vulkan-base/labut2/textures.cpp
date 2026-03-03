#include "textures.hpp"

#include "error.hpp"
#include "to_string.hpp"

// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

namespace labut2
{

	ImageView create_image_view_texture2d( VulkanContext const& aContext, VkImage aImage, VkFormat aFormat )
	{   // create 2d image view

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = aImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = aFormat;
		viewInfo.components = VkComponentMapping{};
		viewInfo.subresourceRange = VkImageSubresourceRange{
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, VK_REMAINING_MIP_LEVELS,
			0, 1
		};

		VkImageView view = VK_NULL_HANDLE;
        // create view
		if( auto const res = vkCreateImageView( aContext.device, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create image view\n"
				"vkCreateImageView() returned {}", to_string(res)
			);
		}

        // wrap and return
		return ImageView( aContext.device, view );
	}

	Sampler create_default_sampler( VulkanContext const& aContext )
	{
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.minLod = 0.f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.f;

		VkSampler sampler = VK_NULL_HANDLE;
        // create sampler
		if( auto const res = vkCreateSampler( aContext.device, &samplerInfo, nullptr, &sampler ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create sampler\n"
				"vkCreateSampler() returned {}", to_string(res)
			);
		}

        // wrap and return
		return Sampler( aContext.device, sampler );
	}


}
