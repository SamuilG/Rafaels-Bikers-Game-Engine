#include "synch.hpp"

#include <cassert>

#include "error.hpp"
#include "to_string.hpp"

// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

namespace labut2
{
	Fence create_fence( VkDevice aDevice, VkFenceCreateFlags aFlags )
	{	// create fence info

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = aFlags;

		VkFence fence = VK_NULL_HANDLE;
		// create fence
		if( auto const res = vkCreateFence( aDevice, &fenceInfo, nullptr, &fence ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create fence\n"
				"vkCreateFence() returned {}", to_string(res)
			);
		}

		// wrap and return
		return Fence( aDevice, fence );
	}

	Semaphore create_semaphore( VkDevice aDevice )
	{	// create semaphore info

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkSemaphore semaphore = VK_NULL_HANDLE;
		// create semaphore
		if( auto const res = vkCreateSemaphore( aDevice, &semaphoreInfo, nullptr, &semaphore ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create semaphore\n"
				"vkCreateSemaphore() returned {}", to_string(res)
			);
		}

		// wrap and return
		return Semaphore( aDevice, semaphore );
	}

	void image_barrier( VkCommandBuffer aCmdBuff, VkImage aImage, VkPipelineStageFlags2 aSrcStageMask, VkAccessFlags2 aSrcAccessMask, VkImageLayout aSrcLayout, VkPipelineStageFlags2 aDstStageMask, VkAccessFlags2 aDstAccessMask, VkImageLayout aDstLayout, VkImageSubresourceRange aRange, std::uint32_t aSrcQueueFamilyIndex, std::uint32_t aDstQueueFamilyIndex )
	{	// create image barrier
		assert( VK_NULL_HANDLE != aCmdBuff );
		assert( VK_NULL_HANDLE != aImage );

		VkImageMemoryBarrier2 ibarrier{};
		ibarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		ibarrier.srcStageMask = aSrcStageMask;
		ibarrier.srcAccessMask = aSrcAccessMask;
		ibarrier.dstStageMask = aDstStageMask;
		ibarrier.dstAccessMask = aDstAccessMask;
		ibarrier.oldLayout = aSrcLayout;
		ibarrier.newLayout = aDstLayout;
		ibarrier.srcQueueFamilyIndex = aSrcQueueFamilyIndex;
		ibarrier.dstQueueFamilyIndex = aDstQueueFamilyIndex;
		ibarrier.image = aImage;
		ibarrier.subresourceRange = aRange;

		VkDependencyInfo deps{};
		deps.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		deps.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps.imageMemoryBarrierCount = 1;
		deps.pImageMemoryBarriers = &ibarrier;

		vkCmdPipelineBarrier2( aCmdBuff, &deps );
	}

	void buffer_barrier( VkCommandBuffer aCmdBuff, VkBuffer aBuffer, VkPipelineStageFlags2 aSrcStageMask, VkAccessFlags2 aSrcAccessMask, VkPipelineStageFlags2 aDstStageMask, VkAccessFlags2 aDstAccessMask, VkDeviceSize aSize, VkDeviceSize aOffset, uint32_t aSrcQueueFamilyIndex, uint32_t aDstQueueFamilyIndex )
	{	// create buffer barrier

		VkBufferMemoryBarrier2 bbarrier{};
		bbarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		bbarrier.srcStageMask = aSrcStageMask;
		bbarrier.srcAccessMask = aSrcAccessMask;
		bbarrier.dstStageMask = aDstStageMask;
		bbarrier.dstAccessMask = aDstAccessMask;
		bbarrier.srcQueueFamilyIndex = aSrcQueueFamilyIndex;
		bbarrier.dstQueueFamilyIndex = aDstQueueFamilyIndex;
		bbarrier.buffer = aBuffer;
		bbarrier.offset = aOffset;
		bbarrier.size = aSize;

		VkDependencyInfo deps{};
		deps.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		deps.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps.bufferMemoryBarrierCount = 1;
		deps.pBufferMemoryBarriers = &bbarrier;

		vkCmdPipelineBarrier2( aCmdBuff, &deps );
	}




}
