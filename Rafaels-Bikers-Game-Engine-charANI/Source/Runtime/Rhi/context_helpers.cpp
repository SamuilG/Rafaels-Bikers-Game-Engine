#include "context_helpers.hxx"

#include <print>

#include "error.hpp"
#include "to_string.hpp"
//namespace lut = labut2;

// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

namespace labut2::detail
{
	std::unordered_set<std::string> get_instance_layers()
	{
		std::uint32_t numLayers = 0;
		if( auto const res = vkEnumerateInstanceLayerProperties( &numLayers, nullptr ); VK_SUCCESS != res )
		{
			throw Error( "Unable to enumerate layers\n"
				"vkEnumerateInstanceLayerProperties() returned {}\n", to_string(res)
			);
		}

		std::vector<VkLayerProperties> layers( numLayers );
		if( auto const res = vkEnumerateInstanceLayerProperties( &numLayers, layers.data() ); VK_SUCCESS != res )
		{
			throw Error( "Unable to get layer properties\n"
				"vkEnumerateInstanceLayerProperties() returned {}", to_string(res)
			);
		}

		std::unordered_set<std::string> res;
		for( auto const& layer : layers )
			res.insert( layer.layerName );

		return res;
	}
	std::unordered_set<std::string> get_instance_extensions()
	{
		std::uint32_t numExtensions = 0;
		if( auto const res = vkEnumerateInstanceExtensionProperties( nullptr, &numExtensions, nullptr ); VK_SUCCESS != res )
		{	
			throw Error( "Unable to enumerate extensions\n"
				"vkEnumerateInstanceExtensionProperties() returned {}", to_string(res)
			);
		}

		std::vector<VkExtensionProperties> extensions( numExtensions );
		if( auto const res = vkEnumerateInstanceExtensionProperties( nullptr, &numExtensions, extensions.data() ); VK_SUCCESS != res )
		{	
			throw Error( "Unable to get extension properties\n" 
				"vkEnumerateInstanceExtensionProperties() returned {}", to_string(res)
			);
		}

		std::unordered_set<std::string> res;
		for( auto const& extension : extensions )
			res.insert( extension.extensionName );

		return res;
	}

	VkInstance create_instance( std::vector<char const*> const& aEnabledLayers, std::vector<char const*> const& aEnabledExtensions, bool aEnableDebugUtils )
	{
		// Prepare debug messenger info
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{};

		if( aEnableDebugUtils )
		{
			debugInfo.sType  = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugInfo.messageSeverity  = /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | */VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugInfo.messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugInfo.pfnUserCallback  = &debug_util_callback;
			debugInfo.pUserData        = nullptr;
		}

		// Prepare application info
		// The `apiVersion` is the *highest* version of Vulkan than the
		// application can use. We can therefore safely set it to 1.3, even if
		// we are not intending to use any 1.3 features (and want to run on
		// pre-1.3 implementations).
		VkApplicationInfo appInfo{};
		appInfo.sType  = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName    = "COMP5892M-application";
		appInfo.applicationVersion  = 2025; // academic year of 2025/26
		appInfo.apiVersion          = VK_MAKE_API_VERSION( 0, 1, 4, 0 ); // Version 1.4

		// Create instance
		VkInstanceCreateInfo instanceInfo{};
		instanceInfo.sType  = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

		instanceInfo.enabledLayerCount        = std::uint32_t(aEnabledLayers.size());
		instanceInfo.ppEnabledLayerNames      = aEnabledLayers.data();

		instanceInfo.enabledExtensionCount    = std::uint32_t(aEnabledExtensions.size());
		instanceInfo.ppEnabledExtensionNames  = aEnabledExtensions.data();

		instanceInfo.pApplicationInfo = &appInfo;

		if( aEnableDebugUtils )
		{
			debugInfo.pNext = instanceInfo.pNext;
			instanceInfo.pNext = &debugInfo; 
		}

		VkInstance instance;
		if( auto const res = vkCreateInstance( &instanceInfo, nullptr, &instance ); VK_SUCCESS != res )
		{
			throw Error( "Unable to create Vulkan instance\n"
				"vkCreateInstance() returned {}", to_string(res)
			);
		}

		return instance;
	}
}

namespace labut2::detail
{
	VkDebugUtilsMessengerEXT create_debug_messenger( VkInstance aInstance )
	{
		// Set up the debug messaging for the rest of the application
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
		debugInfo.sType            = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugInfo.messageSeverity  = /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | */ VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback  = &debug_util_callback;
		debugInfo.pUserData        = nullptr;

		VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
		if( auto const res = vkCreateDebugUtilsMessengerEXT( aInstance, &debugInfo, nullptr, &messenger ); VK_SUCCESS != res )
		{
			throw Error( "Unable to set up debug messenger\n" 
				"vkCreateDebugUtilsMessengerEXT() returned {}", to_string(res)
			);
		}

		return messenger;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL debug_util_callback( VkDebugUtilsMessageSeverityFlagBitsEXT aSeverity, VkDebugUtilsMessageTypeFlagsEXT aType, VkDebugUtilsMessengerCallbackDataEXT const* aData, void* /*aUserPtr*/ )
	{
		std::print( stderr, "{} ({}): {} ({})\n{}\n--\n", to_string(aSeverity), message_type_flags(aType), aData->pMessageIdName, aData->messageIdNumber, aData->pMessage );

		return VK_FALSE;
	}
}

namespace labut2::detail
{
	std::unordered_set<std::string> get_device_extensions( VkPhysicalDevice aPhysicalDev )
	{
		std::uint32_t extensionCount = 0;
		if( auto const res = vkEnumerateDeviceExtensionProperties( aPhysicalDev, nullptr, &extensionCount, nullptr ); VK_SUCCESS != res )
		{
			throw Error( "Unable to get device extension count\n"
				"vkEnumerateDeviceExtensionProperties() returned {}", to_string(res)
			);
		}

		std::vector<VkExtensionProperties> extensions( extensionCount );
		if( auto const res = vkEnumerateDeviceExtensionProperties( aPhysicalDev, nullptr, &extensionCount, extensions.data() ); VK_SUCCESS != res )
		{
			throw Error( "Unable to get device extensions\n"
				"vkEnumerateDeviceExtensionProperties() returned {}", to_string(res)
			);
		}

		std::unordered_set<std::string> ret;
		for( auto const& ext : extensions )
			ret.emplace( ext.extensionName );

		return ret;
	}

	std::vector<char const*> check_required_device_features( VkPhysicalDevice aPhysicalDev )
	{
		VkPhysicalDeviceVulkan13Features vk13{};
		vk13.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		
		VkPhysicalDeviceVulkan14Features vk14{};
		vk14.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
		vk14.pNext  = &vk13;

		VkPhysicalDeviceFeatures2 feat{};
		feat.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		feat.pNext  = &vk14;

		vkGetPhysicalDeviceFeatures2( aPhysicalDev, &feat );

		std::vector<char const*> missingFeat;
		if( !vk13.synchronization2 )
		{
			missingFeat.emplace_back( "synchronization2" );
		}
		if( !vk13.dynamicRendering )
		{
			missingFeat.emplace_back( "dynamicRendering" );
		}
		if( !vk14.maintenance5 )
		{
			missingFeat.emplace_back( "maintenance5" );
		}

		return missingFeat;
	}
}
