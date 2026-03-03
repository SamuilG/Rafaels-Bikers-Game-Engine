#ifndef COMMANDS_HPP_DBB0D0EE_AC4E_44A8_800B_DE17E07E1536
#define COMMANDS_HPP_DBB0D0EE_AC4E_44A8_800B_DE17E07E1536
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <volk/volk.h>

#include "vkobject.hpp"
#include "vulkan_context.hpp"

namespace labut2
{
	CommandPool create_command_pool( VulkanContext const&, VkCommandPoolCreateFlags = 0 );
	VkCommandBuffer alloc_command_buffer( VulkanContext const&, VkCommandPool );
}

#endif // COMMANDS_HPP_DBB0D0EE_AC4E_44A8_800B_DE17E07E1536
