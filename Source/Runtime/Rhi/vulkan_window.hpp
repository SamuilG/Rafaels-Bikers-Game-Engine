#ifndef VULKAN_WINDOW_HPP_231E888F_8BBB_4DB8_A216_7B6D795ED497
#define VULKAN_WINDOW_HPP_231E888F_8BBB_4DB8_A216_7B6D795ED497
// SOLUTION_TAGS: vulkan-(ex-[^12]|cw-.)

#include <volk/volk.h>

#if !defined(GLFW_INCLUDE_NONE)
#	define GLFW_INCLUDE_NONE 1
#endif
#include <GLFW/glfw3.h>

#include <vector>
#include <cstdint>
#include <string>
#include <functional>

#include "vulkan_context.hpp"

namespace labut2
{
	struct VulkanCreateRequirements
	{
		std::uint32_t apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 0);
		std::vector<std::string> instanceExtensions;
		std::vector<std::string> deviceExtensions;
		std::function<bool(VkInstanceCreateInfo const&, VkInstance&, VkResult&)> createInstanceOverride;
		std::function<bool(VkInstance, VkPhysicalDevice&)> selectPhysicalDeviceOverride;
		std::function<bool(VkPhysicalDevice, VkDeviceCreateInfo const&, VkDevice&, VkResult&)> createDeviceOverride;
	};

	class VulkanWindow final : public VulkanContext
	{
		public:
			VulkanWindow(), ~VulkanWindow();

			// Move-only
			VulkanWindow( VulkanWindow const& ) = delete;
			VulkanWindow& operator= (VulkanWindow const&) = delete;

			VulkanWindow( VulkanWindow&& ) noexcept;
			VulkanWindow& operator= (VulkanWindow&&) noexcept;

		public:
			GLFWwindow* window = nullptr;
			VkSurfaceKHR surface = VK_NULL_HANDLE;

			std::uint32_t presentFamilyIndex = 0;
			VkQueue presentQueue = VK_NULL_HANDLE;

			VkSwapchainKHR swapchain = VK_NULL_HANDLE;
			std::vector<VkImage> swapImages;
			std::vector<VkImageView> swapViews;

			VkFormat swapchainFormat;
			VkExtent2D swapchainExtent;
	};

	VulkanWindow make_vulkan_window(bool aVisible = true, VulkanCreateRequirements const& aRequirements = {});
	void set_default_vulkan_create_requirements(VulkanCreateRequirements requirements);
	VulkanCreateRequirements const& get_default_vulkan_create_requirements();


	struct SwapChanges
	{
		bool changedSize : 1;
		bool changedFormat: 1;
	};

	SwapChanges recreate_swapchain( VulkanWindow& );
}

#endif // VULKAN_WINDOW_HPP_231E888F_8BBB_4DB8_A216_7B6D795ED497
