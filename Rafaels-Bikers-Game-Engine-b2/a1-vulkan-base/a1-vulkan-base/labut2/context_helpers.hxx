#ifndef CONTEXT_HELPERS_HXX_1193322F_D0C1_490F_9808_07E59BF3C51B
#define CONTEXT_HELPERS_HXX_1193322F_D0C1_490F_9808_07E59BF3C51B
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

/* context_helpers.hxx is an internal header that defines functions used
 * internally by both vulkan_context.cpp and vulkan_window.cpp. These
 * functions were previously defined locally in vulkan_context.cpp
 */

#include <volk/volk.h>

#include <string>
#include <vector>
#include <unordered_set>

namespace labut2
{
	namespace detail
	{
		std::unordered_set<std::string> get_instance_layers();
		std::unordered_set<std::string> get_instance_extensions();

		VkInstance create_instance(
			std::vector<char const*> const& aEnabledLayers = {}, 
			std::vector<char const*> const& aEnabledInstanceExtensions = {} ,
			bool aEnableDebugUtils = false
		);

		VkDebugUtilsMessengerEXT create_debug_messenger( VkInstance );

		VKAPI_ATTR VkBool32 VKAPI_CALL debug_util_callback( VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, VkDebugUtilsMessengerCallbackDataEXT const*, void* );


		std::unordered_set<std::string> get_device_extensions( VkPhysicalDevice );

		std::vector<char const*> check_required_device_features( VkPhysicalDevice );
	}
}

#endif // CONTEXT_HELPERS_HXX_1193322F_D0C1_490F_9808_07E59BF3C51B
