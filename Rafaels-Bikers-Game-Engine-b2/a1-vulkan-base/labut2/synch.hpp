#ifndef SYCH_HPP_38B52A38_F7ED_426D_9E10_D72C082648D5
#define SYCH_HPP_38B52A38_F7ED_426D_9E10_D72C082648D5
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <volk/volk.h>

#include <cstdint>

#include "vkobject.hpp"


namespace labut2
{
	Fence create_fence( VkDevice, VkFenceCreateFlags = 0 );
	Semaphore create_semaphore( VkDevice );

	void image_barrier(
		VkCommandBuffer,
		VkImage,
		VkPipelineStageFlags2 aSrcStageMask,
		VkAccessFlags2 aSrcAccessMask,
		VkImageLayout aSrcLayout,
		VkPipelineStageFlags2 aDstStageMask,
		VkAccessFlags2 aDstAccessMask,
		VkImageLayout aDstLayout,
		VkImageSubresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
		std::uint32_t aSrcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		std::uint32_t aDstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
	);

	void buffer_barrier(
		VkCommandBuffer,
		VkBuffer,
		VkPipelineStageFlags2 aSrcStageMask,
		VkAccessFlags2 aSrcAccessMask,
		VkPipelineStageFlags2 aDstStageMask,
		VkAccessFlags2 aDstAccessMask,
		VkDeviceSize aSize = VK_WHOLE_SIZE,
		VkDeviceSize aOffset = 0,
		std::uint32_t aSrcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		std::uint32_t aDstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
	);

}

#endif // SYNCH_HPP_38B52A38_F7ED_426D_9E10_D72C082648D5
