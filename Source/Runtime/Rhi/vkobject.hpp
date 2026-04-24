#ifndef VKOBJECT_HPP_CDE07357_88D2_424A_AD7A_AA8BA87075A3
#define VKOBJECT_HPP_CDE07357_88D2_424A_AD7A_AA8BA87075A3
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <volk/volk.h>

namespace labut2
{
	// Provide a small wrapper around Vulkan handles that destroys these when
	// the object goes out of scope. 
	//
	// The wrappers are move-only, i.e., we cannot make copies of them. We are
	// only allowed to pass ownership ("move") of the underlying handle to a 
	// different object. This is similar to how std::unique_ptr<> works. See
	// https://en.cppreference.com/w/cpp/memory/unique_ptr
	// for a detailed description and examples of unique_ptr.
	//
	// We could implement an indivdual class for each type of Vulkan handle.
	//
	// Example for VkPipeline:
#	if 0
	class Pipeline final
	{
		public:
			Pipeline() noexcept, ~Pipeline();

			explicit Pipeline( VkDevice, VkPipeline = VK_NULL_HANDLE ) noexcept;

			Pipeline( Pipeline const& ) = delete;
			Pipeline& operator= (Pipeline const&) = delete;

			Pipeline( Pipeline&& ) noexcept;
			Pipeline& operator = (Pipeline&&) noexcept;

		public:
			VkPipeline handle = VK_NULL_HANDLE;

		private:
			VkDevice mParent = VK_NULL_HANDLE;
	};
#	endif

	// Defining a custom class like the one shown above for the various Vulkan 
	// object types is quite tedious. Instead, we can solve the problem with a 
	// C++ template.
	//
	// If you're unfamiliar with C++ templates, think of the UniqueHandle<>
	// template below as a receipe to create a class such as the one shown above.
	// 
	// We can then define instances (=classes) of the template, as
	//
	// using Pipeline = UniqueHandle< VkPipeline, VkDevice, vkDestroyPipeline >;
	//
	// (see list below the UniqueHandle template definition).
	//
	// The template takes three arguments:
	//   - The type of Vulkan handle we want to wrap (VkPipeline in the example)
	//   - The type of the parent object (here VkDevice)
	//   - The function that is used to destroy the Vulkan handle, which is 
	//     vkDestroyPipeline in this case. (Recall that this function takes
	//     a VkDevice handle as its first argument.)
	
	template< typename tParent, typename tHandle >
	using DestroyFn = void (*)( tParent, tHandle, VkAllocationCallbacks const* );

	template< typename tHandle, typename tParent, DestroyFn<tParent,tHandle>& tDestroyFn >
	class UniqueHandle final
	{
		public:
			UniqueHandle() noexcept = default;
			explicit UniqueHandle( tParent, tHandle = VK_NULL_HANDLE ) noexcept;

			~UniqueHandle();

			UniqueHandle( UniqueHandle const& ) = delete;
			UniqueHandle& operator= (UniqueHandle const&) = delete;

			UniqueHandle( UniqueHandle&& ) noexcept;
			UniqueHandle& operator = (UniqueHandle&&) noexcept;

		public:
			tHandle handle = VK_NULL_HANDLE;

		private:
			tParent mParent = VK_NULL_HANDLE;
	};

	// Pre-defined wrapper types
	using DescriptorPool = UniqueHandle< VkDescriptorPool, VkDevice, vkDestroyDescriptorPool >;
	using DescriptorSetLayout = UniqueHandle< VkDescriptorSetLayout, VkDevice, vkDestroyDescriptorSetLayout >;

	using Pipeline = UniqueHandle< VkPipeline, VkDevice, vkDestroyPipeline >;
	using PipelineLayout = UniqueHandle< VkPipelineLayout, VkDevice, vkDestroyPipelineLayout >;

	using CommandPool = UniqueHandle< VkCommandPool, VkDevice, vkDestroyCommandPool >;

	using Fence = UniqueHandle< VkFence, VkDevice, vkDestroyFence >;
	using Semaphore = UniqueHandle< VkSemaphore, VkDevice, vkDestroySemaphore >;

	using ImageView = UniqueHandle< VkImageView, VkDevice, vkDestroyImageView >;
	using Sampler = UniqueHandle< VkSampler, VkDevice, vkDestroySampler >;
}

#include "vkobject.inl"
#endif // VKOBJECT_HPP_CDE07357_88D2_424A_AD7A_AA8BA87075A3
