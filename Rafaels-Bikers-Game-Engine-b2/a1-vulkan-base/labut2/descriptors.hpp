#ifndef DESCRIPTORS_HPP_FAB9C33C_AB8E_4D20_A9F0_73648EADD693
#define DESCRIPTORS_HPP_FAB9C33C_AB8E_4D20_A9F0_73648EADD693
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <volk/volk.h>

#include "vkobject.hpp"
#include "vulkan_context.hpp"


namespace labut2
{
	DescriptorPool create_descriptor_pool( VulkanContext const& aContext, std::uint32_t aMaxDescriptors = 2048, std::uint32_t aMaxSets = 1024 );

	VkDescriptorSet alloc_desc_set( VulkanContext const& aContext, VkDescriptorPool aPool, VkDescriptorSetLayout aSetLayout );
}

#endif // DESCRIPTORS_HPP_FAB9C33C_AB8E_4D20_A9F0_73648EADD693
