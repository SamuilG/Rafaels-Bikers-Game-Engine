#ifndef TEXTURES_HPP_968F958F_C7E1_4BBF_BF19_21AD8F20A51D
#define TEXTURES_HPP_968F958F_C7E1_4BBF_BF19_21AD8F20A51D
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <volk/volk.h>

#include "vkobject.hpp"
#include "vulkan_context.hpp"


namespace labut2
{
	ImageView create_image_view_texture2d( VulkanContext const&, VkImage, VkFormat );
	Sampler create_default_sampler( VulkanContext const& );
}

#endif // TEXTURES_HPP_968F958F_C7E1_4BBF_BF19_21AD8F20A51D
