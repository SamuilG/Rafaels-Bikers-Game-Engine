#ifndef VKBUFFER_HPP_02BECD1E_FBBC_4095_BA09_C10C43078FD8
#define VKBUFFER_HPP_02BECD1E_FBBC_4095_BA09_C10C43078FD8
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <volk/volk.h>
#include <vk_mem_alloc.h>

#include "allocator.hpp"

namespace labut2
{
	class Buffer final
	{
		public:
			Buffer() noexcept, ~Buffer();

			explicit Buffer( VmaAllocator, VkBuffer = VK_NULL_HANDLE, VmaAllocation = VK_NULL_HANDLE ) noexcept;

			Buffer( Buffer const& ) = delete;
			Buffer& operator= (Buffer const&) = delete;

			Buffer( Buffer&& ) noexcept;
			Buffer& operator = (Buffer&&) noexcept;

		public:
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;

		private:
			VmaAllocator mAllocator = VK_NULL_HANDLE;
	};

	Buffer create_buffer( Allocator const&, VkDeviceSize, VkBufferUsageFlags, VmaAllocationCreateFlags, VmaMemoryUsage = VMA_MEMORY_USAGE_AUTO );
}

#endif // VKBUFFER_HPP_02BECD1E_FBBC_4095_BA09_C10C43078FD8
