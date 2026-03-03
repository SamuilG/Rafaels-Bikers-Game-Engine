#ifndef VULKAN_CONTEXT_HPP_4E11FEA9_5528_47C1_A2AA_D95904707A21
#define VULKAN_CONTEXT_HPP_4E11FEA9_5528_47C1_A2AA_D95904707A21
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <volk/volk.h>

#include <cstdint>

namespace labut2
{
	class VulkanContext
	{
		public:
			VulkanContext(), ~VulkanContext();

			// Move-only
			VulkanContext( VulkanContext const& ) = delete;
			VulkanContext& operator= (VulkanContext const&) = delete;

			VulkanContext( VulkanContext&& ) noexcept;
			VulkanContext& operator= (VulkanContext&&) noexcept;

		public:
			VkInstance instance = VK_NULL_HANDLE;
			VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;


			VkDevice device = VK_NULL_HANDLE;

			std::uint32_t graphicsFamilyIndex = 0;
			VkQueue graphicsQueue = VK_NULL_HANDLE;

			
			VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	};

	VulkanContext make_vulkan_context();
}

#endif // VULKAN_CONTEXT_HPP_4E11FEA9_5528_47C1_A2AA_D95904707A21
