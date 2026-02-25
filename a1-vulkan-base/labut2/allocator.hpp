#ifndef ALLOCATOR_HPP_D0F7EB2D_D4D9_43AB_9F23_56D0EE9A4DF6
#define ALLOCATOR_HPP_D0F7EB2D_D4D9_43AB_9F23_56D0EE9A4DF6
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <volk/volk.h>
#include <vk_mem_alloc.h>

#include "vulkan_context.hpp"

namespace labut2
{
	class Allocator
	{
		public:
			Allocator() noexcept, ~Allocator();

			explicit Allocator( VmaAllocator ) noexcept;

			Allocator( Allocator const& ) = delete;
			Allocator& operator= (Allocator const&) = delete;

			Allocator( Allocator&& ) noexcept;
			Allocator& operator = (Allocator&&) noexcept;

		public:
			VmaAllocator allocator = VK_NULL_HANDLE;
	};

	Allocator create_allocator( VulkanContext const& );
}

#endif // ALLOCATOR_HPP_D0F7EB2D_D4D9_43AB_9F23_56D0EE9A4DF6
