#include "vkimage.hpp"

#include <bit>
#include <print>
#include <limits>
#include <vector>
#include <utility>
#include <algorithm>

#include <cassert>
#include <cstring> // for std::memcpy()

#include <stb_image.h>

#include "error.hpp"
#include "synch.hpp"
#include "commands.hpp"
#include "vkbuffer.hpp"
#include "to_string.hpp"

// SOLUTION_TAGS: vulkan-(ex-[^123]|cw-.)


namespace labut2
{
	Image::Image() noexcept = default;

	Image::~Image()
	{
		if( VK_NULL_HANDLE != image )
		{
			assert( VK_NULL_HANDLE != mAllocator );
			assert( VK_NULL_HANDLE != allocation );
			vmaDestroyImage( mAllocator, image, allocation );
		}
	}

	Image::Image( VmaAllocator aAllocator, VkImage aImage, VmaAllocation aAllocation ) noexcept
		: image( aImage )
		, allocation( aAllocation )
		, mAllocator( aAllocator )
	{}

	Image::Image( Image&& aOther ) noexcept
		: image( std::exchange( aOther.image, VK_NULL_HANDLE ) )
		, allocation( std::exchange( aOther.allocation, VK_NULL_HANDLE ) )
		, mAllocator( std::exchange( aOther.mAllocator, VK_NULL_HANDLE ) )
	{}
	Image& Image::operator=( Image&& aOther ) noexcept
	{
		std::swap( image, aOther.image );
		std::swap( allocation, aOther.allocation );
		std::swap( mAllocator, aOther.mAllocator );
		return *this;
	}


	ImageWithView::ImageWithView() noexcept = default;

	ImageWithView::~ImageWithView()
	{
		if( VK_NULL_HANDLE != view )
		{
			// This is a bit of a hack, but means we can just keep the
			// VmaAllocator handle, without also having to store a VkDevice
			// handle (which is indeed already stored in the allocator).
			assert( VK_NULL_HANDLE != mAllocator );

			VmaAllocatorInfo ainfo{};
			vmaGetAllocatorInfo( mAllocator, &ainfo );

			vkDestroyImageView( ainfo.device, view, nullptr );
		}
	}

	ImageWithView::ImageWithView( Image&& aImage, VkImageView aView ) noexcept
		: Image( std::move(aImage) )
		, view( aView )
	{}
	ImageWithView::ImageWithView( VmaAllocator aAllocator, VkImage aImage, VmaAllocation aAllocation, VkImageView aView ) noexcept
		: Image( aAllocator, aImage, aAllocation )
		, view( aView )
	{}

	ImageWithView::ImageWithView( ImageWithView&& aOther ) noexcept
		: Image( std::move(aOther) )
		, view( std::exchange( aOther.view, VK_NULL_HANDLE ) )
	{}

	ImageWithView& ImageWithView::operator= (ImageWithView&& aOther) noexcept
	{
		static_cast<Image&>(*this) = std::move(aOther);
		std::swap( view, aOther.view );
		return *this;
	}
}

namespace labut2
{
	Image load_image_texture2d( char const* aPath, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator, VkFormat format )
	{
		// load base image
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load( true );
		stbi_uc* data = stbi_load( aPath, &width, &height, &channels, 4 );

		if( !data )
		{
			throw Error( "Unable to load image '{}'\n"
				"stbi_load() returned NULL", aPath
			);
		}

        // create staging buffer
		std::size_t const size = std::size_t(width) * std::size_t(height) * 4;
		Buffer staging = create_buffer(
			aAllocator,
			size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

        // copy data to staging buffer
		void* ptr = nullptr;
		if( auto const res = vmaMapMemory( aAllocator.allocator, staging.allocation, &ptr ); VK_SUCCESS != res )
		{
			stbi_image_free( data );
			throw Error( "Mapping memory for writing\n"
				"vmaMapMemory() returned {}", to_string(res)
			);
		}

		std::memcpy( ptr, data, size );
		vmaUnmapMemory( aAllocator.allocator, staging.allocation );
		stbi_image_free( data );

        // create gpu image
		Image image = create_image_texture2d(
			aAllocator,
			std::uint32_t(width),
			std::uint32_t(height),
			format,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		);

        // begin command buffer
		VkCommandBuffer cmdBuff = alloc_command_buffer( aContext, aCmdPool );

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		
		if( auto const res = vkBeginCommandBuffer( cmdBuff, &beginInfo ); VK_SUCCESS != res )
		{
			throw Error( "Beginning command buffer recording\n"
				"vkBeginCommandBuffer() returned {}", to_string(res)
			);
		}


        // transition entire image to transfer destination
		// to TRANSFER_DST_OPTIMAL
		image_barrier(
			cmdBuff,
			image.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_COPY_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, VK_REMAINING_MIP_LEVELS,
				0, 1
			}
		);

        // copy staging buffer to image level 0
		VkBufferImageCopy copy{};
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = 0;
		copy.imageSubresource.baseArrayLayer = 0;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset = { 0, 0, 0 };
       	copy.imageExtent = { std::uint32_t(width), std::uint32_t(height), 1 };

		vkCmdCopyBufferToImage( cmdBuff, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );

        // compute mip count
		std::uint32_t const mipLevels = compute_mip_level_count( width, height );
		
        // barrier before blits


        // transition base level for reading
		// to TRANSFER_SRC_OPTIMAL
		image_barrier(
			cmdBuff,
			image.image,
			VK_PIPELINE_STAGE_2_COPY_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_BLIT_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

        
		// generate mip levels
		for( std::uint32_t i = 1; i < mipLevels; ++i )
		{
			VkImageBlit blit{};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.layerCount = 1;
			blit.srcSubresource.mipLevel = i-1;
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = {
				std::int32_t(width >> (i-1)) > 0 ? std::int32_t(width >> (i-1)) : 1,
				std::int32_t(height >> (i-1)) > 0 ? std::int32_t(height >> (i-1)) : 1,
				1
			};

			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.layerCount = 1;
			blit.dstSubresource.mipLevel = i;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = {
				std::int32_t(width >> i) > 0 ? std::int32_t(width >> i) : 1,
				std::int32_t(height >> i) > 0 ? std::int32_t(height >> i) : 1,
				1
			};
			
            // blit previous mip to next
			vkCmdBlitImage(
				cmdBuff,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR
			);

            
			// transition level for next read
			// to TRANSFER_SRC_OPTIMAL
			image_barrier(
				cmdBuff,
				image.image,
				VK_PIPELINE_STAGE_2_BLIT_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_BLIT_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageSubresourceRange{
					VK_IMAGE_ASPECT_COLOR_BIT,
					i, 1,
					0, 1
				}
			);
		}

        // final transition for shader reads
		// to SHADER_READ_ONLY_OPTIMAL
		image_barrier(
			cmdBuff,
			image.image,
			VK_PIPELINE_STAGE_2_BLIT_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);


        // end command buffer
		if( auto const res = vkEndCommandBuffer( cmdBuff ); VK_SUCCESS != res )
		{
			throw Error( "Ending command buffer recording\n"
				"vkEndCommandBuffer() returned {}", to_string(res)
			);
		}

        // submit and wait
		Fence uploadComplete = create_fence( aContext.device );
		
		VkCommandBufferSubmitInfo submit[1]{};
		submit[0].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		submit[0].commandBuffer = cmdBuff;

		VkSubmitInfo2 submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = submit;

		if( auto const res = vkQueueSubmit2( aContext.graphicsQueue, 1, &submitInfo, uploadComplete.handle ); VK_SUCCESS != res )
		{
			throw Error( "Unable to submit command buffer to queue\n"
				"vkQueueSubmit2() returned {}", to_string(res)
			);
		}

		if( auto const res = vkWaitForFences( aContext.device, 1, &uploadComplete.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max() ); VK_SUCCESS != res )
		{
			throw Error( "Waiting for upload to complete\n"
				"vkWaitForFences() returned {}", to_string(res)
			);
		}

		return image;
	}

	Image create_image_texture2d( Allocator const& aAllocator, std::uint32_t aWidth, std::uint32_t aHeight, VkFormat aFormat, VkImageUsageFlags aUsage )
	{	// create empty gpu image with mipmaps

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = aFormat;
		imageInfo.extent.width = aWidth;
		imageInfo.extent.height = aHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = compute_mip_level_count( aWidth, aHeight );
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = aUsage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // set allocation info
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.flags = 0;
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        // allocate image and memory
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;

		if( auto const res = vmaCreateImage( aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr ); VK_SUCCESS != res )
		{
			throw Error( "Unable to allocate image.\n"
				"vmaCreateImage() returned {}", to_string(res)
			);
		}

        // return wrapped image
		return Image( aAllocator.allocator, image, allocation );

	}

	Image load_image_texture2d_from_memory(
		void const* aPixels, std::uint32_t aWidth, std::uint32_t aHeight,
		VulkanContext const& aContext, VkCommandPool aCmdPool,
		Allocator const& aAllocator, VkFormat format)
	{
		//pixels size
		std::size_t const size = std::size_t(aWidth) * std::size_t(aHeight) * 4;

		// staging buffer
		Buffer staging = create_buffer(
			aAllocator, size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		);

		void* ptr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &ptr); VK_SUCCESS != res)
			throw Error("Mapping staging memory\nvmaMapMemory() returned {}", to_string(res));

		std::memcpy(ptr, aPixels, size);

		vmaUnmapMemory(aAllocator.allocator, staging.allocation);

		// following is the same as load_image_texture2d 
		Image image = create_image_texture2d(
			aAllocator, aWidth, aHeight, format,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		);

		VkCommandBuffer cmdBuff = alloc_command_buffer(aContext, aCmdPool);
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmdBuff, &beginInfo);

		image_barrier(cmdBuff, image.image,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1 }
		);

		VkBufferImageCopy copy{};
		copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageExtent = { aWidth, aHeight, 1 };
		vkCmdCopyBufferToImage(cmdBuff, staging.buffer, image.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		std::uint32_t const mipLevels = compute_mip_level_count(aWidth, aHeight);

		image_barrier(cmdBuff, image.image,
			VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
		);

		for (std::uint32_t i = 1; i < mipLevels; ++i)
		{
			VkImageBlit blit{};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
			blit.srcOffsets[1] = {
				std::max(1, std::int32_t(aWidth >> (i - 1))),
				std::max(1, std::int32_t(aHeight >> (i - 1))), 1
			};
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
			blit.dstOffsets[1] = {
				std::max(1, std::int32_t(aWidth >> i)),
				std::max(1, std::int32_t(aHeight >> i)), 1
			};
			vkCmdBlitImage(cmdBuff,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			image_barrier(cmdBuff, image.image,
				VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1 }
			);
		}

		image_barrier(cmdBuff, image.image,
			VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 }
		);

		vkEndCommandBuffer(cmdBuff);

		Fence uploadComplete = create_fence(aContext.device);
		VkCommandBufferSubmitInfo submit[1]{};
		submit[0].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		submit[0].commandBuffer = cmdBuff;
		VkSubmitInfo2 submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = submit;
		vkQueueSubmit2(aContext.graphicsQueue, 1, &submitInfo, uploadComplete.handle);
		vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE,
			std::numeric_limits<std::uint64_t>::max());

		return image;
	}
	std::uint32_t compute_mip_level_count( std::uint32_t aWidth, std::uint32_t aHeight )
	{
		std::uint32_t const bits = aWidth | aHeight;
		std::uint32_t const leadingZeros = std::countl_zero( bits );
		return 32-leadingZeros;
	}
}
