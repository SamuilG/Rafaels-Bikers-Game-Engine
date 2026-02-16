#ifndef VKIMAGE_HPP_A6C9F4C6_C25F_4B9D_B9E9_3D81400A2AF1
#define VKIMAGE_HPP_A6C9F4C6_C25F_4B9D_B9E9_3D81400A2AF1
// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)

#include <volk/volk.h>
#include <vk_mem_alloc.h>

#include "allocator.hpp"

namespace labut2
{
	class Image
	{
		public:
			Image() noexcept, ~Image();

			explicit Image( VmaAllocator, VkImage = VK_NULL_HANDLE, VmaAllocation = VK_NULL_HANDLE ) noexcept;

			Image( Image const& ) = delete;
			Image& operator= (Image const&) = delete;

			Image( Image&& ) noexcept;
			Image& operator = (Image&&) noexcept;

		public:
			VkImage image = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;

		protected:
			VmaAllocator mAllocator = VK_NULL_HANDLE;
	};

	// For convenience: ImageWithView. This is useful in Exercise 4 and the
	// courseworks. You are unlikely to need it before that.
	class ImageWithView final : public Image
	{
		public:
			ImageWithView() noexcept, ~ImageWithView();

			explicit ImageWithView( Image&&, VkImageView = VK_NULL_HANDLE ) noexcept;
			explicit ImageWithView( VmaAllocator, VkImage = VK_NULL_HANDLE, VmaAllocation = VK_NULL_HANDLE, VkImageView = VK_NULL_HANDLE ) noexcept;

			ImageWithView( ImageWithView&& ) noexcept;
			ImageWithView& operator = (ImageWithView&&) noexcept;

		public:
			VkImageView view = VK_NULL_HANDLE;
	};


	Image load_image_texture2d( char const* aPath, VulkanContext const&, VkCommandPool, Allocator const&, VkFormat format );

	Image create_image_texture2d( Allocator const&, std::uint32_t aWidth, std::uint32_t aHeight, VkFormat, VkImageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );

	Image load_image_texture2d_from_memory(
		void const* aPixels, std::uint32_t aWidth, std::uint32_t aHeight,
		VulkanContext const&, VkCommandPool, Allocator const&, VkFormat format);

	std::uint32_t compute_mip_level_count( std::uint32_t aWidth, std::uint32_t aHeight );

}

#endif // VKIMAGE_HPP_A6C9F4C6_C25F_4B9D_B9E9_3D81400A2AF1
