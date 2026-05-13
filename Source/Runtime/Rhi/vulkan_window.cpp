#include "vulkan_window.hpp"

// SOLUTION_TAGS: vulkan-(ex-[^12]|cw-.)

#include <print>
#include <tuple>
#include <limits>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <unordered_set>

#include <cassert>

#include "error.hpp"
#include "to_string.hpp"
#include "context_helpers.hxx"
#include <stb_image.h>
namespace lut = labut2;

namespace
{
	// The device selection process has changed somewhat w.r.t. the one used 
	// earlier (e.g., with VulkanContext.
	VkPhysicalDevice select_device( VkInstance, VkSurfaceKHR );
	float score_device( VkPhysicalDevice, VkSurfaceKHR );

	std::optional<std::uint32_t> find_queue_family( VkPhysicalDevice, VkQueueFlags, VkSurfaceKHR = VK_NULL_HANDLE );

	VkDevice create_device( 
		VkPhysicalDevice,
		std::vector<std::uint32_t> const& aQueueFamilies,
		std::vector<char const*> const& aEnabledDeviceExtensions = {}
	);

	std::vector<VkSurfaceFormatKHR> get_surface_formats( VkPhysicalDevice, VkSurfaceKHR );
	std::unordered_set<VkPresentModeKHR> get_present_modes( VkPhysicalDevice, VkSurfaceKHR );

	std::tuple<VkSwapchainKHR,VkFormat,VkExtent2D> create_swapchain(
		VkPhysicalDevice,
		VkSurfaceKHR,
		VkDevice,
		GLFWwindow*,
		std::vector<std::uint32_t> const& aQueueFamilyIndices = {},
		VkSwapchainKHR aOldSwapchain = VK_NULL_HANDLE
	);

	void get_swapchain_images( VkDevice, VkSwapchainKHR, std::vector<VkImage>& );
	void create_swapchain_image_views( VkDevice, VkFormat, std::vector<VkImage> const&, std::vector<VkImageView>& );
}

namespace labut2
{
	// VulkanWindow
	VulkanWindow::VulkanWindow() = default;

	VulkanWindow::~VulkanWindow()
	{
		// Device-related objects
		for( auto const view : swapViews )
			vkDestroyImageView( device, view, nullptr );

		if( VK_NULL_HANDLE != swapchain )
			vkDestroySwapchainKHR( device, swapchain, nullptr );

		// Window and related objects
		if( VK_NULL_HANDLE != surface )
			vkDestroySurfaceKHR( instance, surface, nullptr );

		if( window )
		{
			glfwDestroyWindow( window );

			// The following assumes that we never create more than one window;
			// if there are multiple windows, destroying one of them would
			// unload the whole GLFW library. Nevertheless, this solution is
			// convenient when only dealing with one window (which we will do
			// in the exercises), as it ensure that GLFW is unloaded after all
			// window-related resources are.
			glfwTerminate();
		}
	}

	VulkanWindow::VulkanWindow( VulkanWindow&& aOther ) noexcept
		: VulkanContext( std::move(aOther) )
		, window( std::exchange( aOther.window, VK_NULL_HANDLE ) )
		, surface( std::exchange( aOther.surface, VK_NULL_HANDLE ) )
		, presentFamilyIndex( aOther.presentFamilyIndex )
		, presentQueue( std::exchange( aOther.presentQueue, VK_NULL_HANDLE ) )
		, swapchain( std::exchange( aOther.swapchain, VK_NULL_HANDLE ) )
		, swapImages( std::move( aOther.swapImages ) )
		, swapViews( std::move( aOther.swapViews ) )
		, swapchainFormat( aOther.swapchainFormat )
		, swapchainExtent( aOther.swapchainExtent )
	{}

	VulkanWindow& VulkanWindow::operator=( VulkanWindow&& aOther ) noexcept
	{
		VulkanContext::operator=( std::move(aOther) );
		std::swap( window, aOther.window );
		std::swap( surface, aOther.surface );
		std::swap( presentFamilyIndex, aOther.presentFamilyIndex );
		std::swap( presentQueue, aOther.presentQueue );
		std::swap( swapchain, aOther.swapchain );
		std::swap( swapImages, aOther.swapImages );
		std::swap( swapViews, aOther.swapViews );
		std::swap( swapchainFormat, aOther.swapchainFormat );
		std::swap( swapchainExtent, aOther.swapchainExtent );
		return *this;
	}

	// make_vulkan_window()
	VulkanWindow make_vulkan_window(bool aVisible, bool aFullscreen)
	{
		VulkanWindow ret;

		// Initialize Volk
		if( auto const res = volkInitialize(); VK_SUCCESS != res )
		{
			throw Error( "Unable to load Vulkan API\n" 
				"Volk returned error {}", to_string(res)
			);
		}

		//done TODO: initialize GLFW
		// init GLFW and check Vulkan support
		if( GLFW_TRUE != glfwInit() )
		{
			char const* errMsg = nullptr;
			glfwGetError( &errMsg );
			throw lut::Error( "GLFW initialization failed: {}", errMsg );
		}

		if( !glfwVulkanSupported() )
		{
			throw lut::Error( "GLFW: Vulkan not supported." );
		}

		// Check for instance layers and extensions
		auto const supportedLayers = detail::get_instance_layers();
		auto const supportedExtensions = detail::get_instance_extensions();

		bool enableDebugUtils = false;

		std::vector<char const*> enabledLayers, enabledExensions;

		//done TODO: check that the instance extensions required by GLFW are available,
		//done TODO: and if so, request these to be enabled in the instance creation.
		// get glfw instance extensions
        std::uint32_t reqExtCount = 0;
        char const** requiredExt = glfwGetRequiredInstanceExtensions( &reqExtCount );

        for( std::uint32_t i = 0; i < reqExtCount; ++i )
        {
            // check extensions supported
            if( !supportedExtensions.count( requiredExt[i] ) )
            {
                throw lut::Error( "GLFW/Vulkan: required instance extension {} not supported", requiredExt[i] );
            }

            // enable exextensions
            enabledExensions.emplace_back( requiredExt[i] );
        }


		// Validation layers support.
#		if !defined(NDEBUG) // debug builds only
		if( supportedLayers.count( "VK_LAYER_KHRONOS_validation" ) )
		{
			enabledLayers.emplace_back( "VK_LAYER_KHRONOS_validation" );
		}

		if( supportedExtensions.count( "VK_EXT_debug_utils" ) )
		{
			enableDebugUtils = true;
			enabledExensions.emplace_back( "VK_EXT_debug_utils" );
		}
#		endif // ~ debug builds

		for( auto const& layer : enabledLayers )
			std::print( stderr, "Enabling layer: {}\n", layer );

		for( auto const& extension : enabledExensions )
			std::print( stderr, "Enabling instance extension: {}\n", extension );

		// Create Vulkan instance
		ret.instance = detail::create_instance( enabledLayers, enabledExensions, enableDebugUtils );

		// Load rest of the Vulkan API
		volkLoadInstance( ret.instance );

		// Setup debug messenger
		if( enableDebugUtils )
			ret.debugMessenger = detail::create_debug_messenger( ret.instance );

		//done TODO: create GLFW window
		//done TODO: get VkSurfaceKHR from the window
		glfwDefaultWindowHints();
		// disable OpenGL context
        glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
		glfwWindowHint(GLFW_VISIBLE, aVisible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
		// ѡ�� monitor �� �ֱ��ʣ������Ҫȫ����ʹ������ʾ�����䵱ǰ video mode
		GLFWmonitor* monitor = nullptr;
		int width = 1280;
		int height = 720;
		if (aFullscreen) {
			monitor = glfwGetPrimaryMonitor();
			if (monitor) {
				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				if (mode) {
					width = mode->width;
					height = mode->height;
				}
			}
		}

		// create glfw window (monitor == nullptr -> windowed; otherwise fullscreen on that monitor)
		ret.window = glfwCreateWindow(width, height, "Steer Engine", monitor, nullptr);
		if (!ret.window)
		{
			// get glfw error and throw
			char const* errMsg = nullptr;
			glfwGetError(&errMsg);
			throw lut::Error("Unable to create GLFW window\n"
				"Last error = {}", errMsg
			);
		}

		if (ret.window) {
			GLFWimage icon;
			int channels;
			icon.pixels = stbi_load("Assets/Icons/ICON.png", &icon.width, &icon.height, &channels, 4);
			if (icon.pixels) {
				glfwSetWindowIcon(ret.window, 1, &icon);
				stbi_image_free(icon.pixels);
			}
		}



        // create vulkan surface
        if( auto const res = glfwCreateWindowSurface( ret.instance, ret.window, nullptr, &ret.surface ); VK_SUCCESS != res )
        {
            // failed
            throw lut::Error( "Unable to create VkSurfaceKHR\n"
                "glfwCreateWindowSurface() returned {}", lut::to_string(res)
            );
        }


		// Select appropriate Vulkan device
		ret.physicalDevice = select_device( ret.instance, ret.surface );
		if( VK_NULL_HANDLE == ret.physicalDevice )
			throw Error( "No suitable physical device found!" );

		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties( ret.physicalDevice, &props );
			std::print( stderr, "Selected device: {} ({}.{}.{})\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion) );
		}

		// Create a logical device
		// Enable required extensions. The device selection method ensures that
		// the VK_KHR_swapchain extension is present, so we can safely just
		// request it without further checks.
		std::vector<char const*> enabledDevExensions;

		//done TODO: list necessary extensions here
		enabledDevExensions.emplace_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

		for( auto const& ext : enabledDevExensions )
			std::print( stderr, "Enabling device extension: {}\n", ext );

		// We need one or two queues:
		// - best case: one GRAPHICS queue that can present
		// - otherwise: one GRAPHICS queue and any queue that can present
		std::vector<std::uint32_t> queueFamilyIndices;

		//done TODO: logic to select necessary queue families to instantiate
		// try find single queue supporting graphics + present
        if( auto const index = find_queue_family( ret.physicalDevice, VK_QUEUE_GRAPHICS_BIT, ret.surface ) )
        {
            // store combined queue index
            ret.graphicsFamilyIndex = *index;
            queueFamilyIndices.emplace_back( *index );
        }
        else
        {
            // find separate graphics and present queues
            auto graphics = find_queue_family( ret.physicalDevice, VK_QUEUE_GRAPHICS_BIT );
            auto present  = find_queue_family( ret.physicalDevice, 0, ret.surface );

            assert( graphics && present );

            // store graphics and present indices
            ret.graphicsFamilyIndex = *graphics;
            ret.presentFamilyIndex  = *present;

            // add both indices
            queueFamilyIndices.emplace_back( *graphics );
            queueFamilyIndices.emplace_back( *present );
        }


		ret.device = create_device( ret.physicalDevice, queueFamilyIndices, enabledDevExensions );

		// Retrieve VkQueues
		vkGetDeviceQueue( ret.device, ret.graphicsFamilyIndex, 0, &ret.graphicsQueue );

		assert( VK_NULL_HANDLE != ret.graphicsQueue );

		if( queueFamilyIndices.size() >= 2 )
			vkGetDeviceQueue( ret.device, ret.presentFamilyIndex, 0, &ret.presentQueue );
		else
		{
			ret.presentFamilyIndex = ret.graphicsFamilyIndex;
			ret.presentQueue = ret.graphicsQueue;
		}

		// Create swap chain
		std::tie(ret.swapchain, ret.swapchainFormat, ret.swapchainExtent) = create_swapchain(ret.physicalDevice, ret.surface, ret.device, ret.window, queueFamilyIndices);

		// Get swap chain images & create associated image views
		get_swapchain_images(ret.device, ret.swapchain, ret.swapImages);
		create_swapchain_image_views(ret.device, ret.swapchainFormat, ret.swapImages, ret.swapViews);

		// Done
		return ret;
	}

	SwapChanges recreate_swapchain( VulkanWindow& aWindow )
	{
        // store previous format and extent
        auto const oldFormat = aWindow.swapchainFormat;
        auto const oldExtent = aWindow.swapchainExtent;

        // keep old swapchain for creation call
        VkSwapchainKHR oldSwapchain = aWindow.swapchain;

        // destroy old image views
        for( auto view : aWindow.swapViews )
            vkDestroyImageView( aWindow.device, view, nullptr );

        // clear old swap data
        aWindow.swapViews.clear();
        aWindow.swapImages.clear();

        // prepare queue families if separate graphics/present queues
        std::vector<std::uint32_t> queueFamilyIndices;
        if( aWindow.presentFamilyIndex != aWindow.graphicsFamilyIndex )
        {
            queueFamilyIndices.emplace_back( aWindow.graphicsFamilyIndex );
            queueFamilyIndices.emplace_back( aWindow.presentFamilyIndex );
        }

        try
        {
            // create new swapchain
            std::tie(aWindow.swapchain, aWindow.swapchainFormat, aWindow.swapchainExtent) = create_swapchain( aWindow.physicalDevice, aWindow.surface, aWindow.device, aWindow.window, queueFamilyIndices, oldSwapchain );
        }
        catch( ... )
        {
            // restore old swapchain on failure
            aWindow.swapchain = oldSwapchain;
            throw;
        }

        // destroy old swapchain
        vkDestroySwapchainKHR( aWindow.device, oldSwapchain, nullptr );

        // retrieve new swapchain images + create views
        get_swapchain_images( aWindow.device, aWindow.swapchain, aWindow.swapImages );
        create_swapchain_image_views( aWindow.device, aWindow.swapchainFormat, aWindow.swapImages, aWindow.swapViews );

        // report changes in size or format
        return SwapChanges{
            oldExtent.width  != aWindow.swapchainExtent.width || oldExtent.height != aWindow.swapchainExtent.height,
            oldFormat != aWindow.swapchainFormat
        };

	}

}

namespace
{
	std::vector<VkSurfaceFormatKHR> get_surface_formats( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{	// query number of surface formats

		std::uint32_t numFormats = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR( aPhysicalDev, aSurface, &numFormats, nullptr );

		// get formats
		std::vector<VkSurfaceFormatKHR> formats( numFormats );
		vkGetPhysicalDeviceSurfaceFormatsKHR( aPhysicalDev, aSurface, &numFormats, formats.data() );

		// list
		return formats;
	}

	std::unordered_set<VkPresentModeKHR> get_present_modes( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{	// query supported present modes

		std::uint32_t numModes = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR( aPhysicalDev, aSurface, &numModes, nullptr );

		// get present modes
		std::vector<VkPresentModeKHR> modes( numModes );
		vkGetPhysicalDeviceSurfacePresentModesKHR( aPhysicalDev, aSurface, &numModes, modes.data() );

		// store modes
		std::unordered_set<VkPresentModeKHR> ret;
		for( auto const mode : modes )
			ret.insert( mode );

		return ret;
	}


	std::tuple<VkSwapchainKHR,VkFormat,VkExtent2D> create_swapchain( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface, VkDevice aDevice, GLFWwindow* aWindow, std::vector<std::uint32_t> const& aQueueFamilyIndices, VkSwapchainKHR aOldSwapchain )
	{	// get supported formats and present modes

		auto const formats = get_surface_formats( aPhysicalDev, aSurface );
		auto const modes = get_present_modes( aPhysicalDev, aSurface );

		// select surface format (prefer SRGB)
		assert( !formats.empty() );

		VkSurfaceFormatKHR format = formats[0];
		for( auto const fmt : formats )
		{
			if( VK_FORMAT_R8G8B8A8_SRGB == fmt.format && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == fmt.colorSpace )
			{
				format = fmt;
				break;
			}

			if( VK_FORMAT_B8G8R8A8_SRGB == fmt.format && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == fmt.colorSpace )
			{
				format = fmt;
				break;
			}
		}

		// choose presentation mode
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

		// prefer relaxed FIFO if available
		if( modes.count( VK_PRESENT_MODE_FIFO_RELAXED_KHR ) )
			presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;

		// query capabilities for image count and extent
		VkSurfaceCapabilitiesKHR caps;
		if( auto const res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( aPhysicalDev, aSurface, &caps ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get surface capabilities\n"
				"vkGetPhysicalDeviceSurfaceCapabilitiesKHR() returned {}", lut::to_string(res)
			);
		}

		// choose image count
		std::uint32_t imageCount = 2;

		if( imageCount < caps.minImageCount+1 )
			imageCount = caps.minImageCount+1;

		if( caps.maxImageCount > 0 && imageCount > caps.maxImageCount )
			imageCount = caps.maxImageCount;

		// compute swapchain extent (framebuffer size if undefined)
		VkExtent2D extent = caps.currentExtent;
		if( std::numeric_limits<std::uint32_t>::max() == extent.width )
		{
			int width, height;
			glfwGetFramebufferSize( aWindow, &width, &height );

			// ensure the extent is within the range defined by [ minImageExtent, maxImageExtent ]
			auto const& min = caps.minImageExtent;
			auto const& max = caps.maxImageExtent;

			extent.width = std::clamp( std::uint32_t(width), min.width, max.width );
			extent.height = std::clamp( std::uint32_t(height), min.height, max.height );
		}

		// fill swapchain create info
		VkSwapchainCreateInfoKHR chainInfo{};
		chainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		chainInfo.surface = aSurface;
		chainInfo.minImageCount = imageCount;
		chainInfo.imageFormat = format.format;
		chainInfo.imageColorSpace = format.colorSpace;
		chainInfo.imageExtent = extent;
		chainInfo.imageArrayLayers = 1;
		chainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		chainInfo.preTransform = caps.currentTransform;
		chainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		chainInfo.presentMode = presentMode;
		chainInfo.clipped = VK_TRUE;
		chainInfo.oldSwapchain = aOldSwapchain;

		// set sharing mode (exclusive or concurrent)
		if( aQueueFamilyIndices.size() <= 1 )
		{
			chainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
		else
		{
			chainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			chainInfo.queueFamilyIndexCount = std::uint32_t(aQueueFamilyIndices.size());
			chainInfo.pQueueFamilyIndices = aQueueFamilyIndices.data();
		}

		// create swapchain
		VkSwapchainKHR chain = VK_NULL_HANDLE;
		if( auto const res = vkCreateSwapchainKHR( aDevice, &chainInfo, nullptr, &chain ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to create swap chain\n"
				"vkCreateSwapchainKHR() returned {}", lut::to_string(res)
			);
		}

		// return swapchain, format, extent
		return { chain, format.format, extent };
	}



	void get_swapchain_images( VkDevice aDevice, VkSwapchainKHR aSwapchain, std::vector<VkImage>& aImages )
	{	// get images from swapchain

		assert( 0 == aImages.size() );

		std::uint32_t numImages = 0;
		if( auto const res = vkGetSwapchainImagesKHR( aDevice, aSwapchain, &numImages, nullptr ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get swapchain image count\n"
				"vkGetSwapchainImagesKHR() returned {}", lut::to_string(res)
			);
		}

		// resize and fetch images
		aImages.resize( numImages );
		if( auto const res = vkGetSwapchainImagesKHR( aDevice, aSwapchain, &numImages, aImages.data() ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get swapchain images\n"
				"vkGetSwapchainImagesKHR() returned {}", lut::to_string(res)
			);
		}

	}


	void create_swapchain_image_views( VkDevice aDevice, VkFormat aSwapchainFormat, std::vector<VkImage> const& aImages, std::vector<VkImageView>& aViews )
	{	// create image views for each swapchain image
		assert( 0 == aViews.size() );

		for( std::size_t i = 0; i < aImages.size(); ++i )
		{
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = aImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = aSwapchainFormat;
			viewInfo.components = VkComponentMapping{
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY
			};
			viewInfo.subresourceRange = VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			};

			VkImageView view = VK_NULL_HANDLE;
			// create image view
			if( auto const res = vkCreateImageView( aDevice, &viewInfo, nullptr, &view ); VK_SUCCESS != res )
			{
				throw lut::Error( "Unable to create image view for swap chain image {}\n"
					"vkCreateImageView() returned {}", i, lut::to_string(res)
				);
			}

			aViews.emplace_back( view );
		}

		assert( aViews.size() == aImages.size() );
	
	}


}

namespace
{
	// Note: this finds *any* queue that supports the aQueueFlags. As such,
	//   find_queue_family( ..., VK_QUEUE_TRANSFER_BIT, ... );
	// might return a GRAPHICS queue family, since GRAPHICS queues typically
	// also set TRANSFER (and indeed most other operations; GRAPHICS queues are
	// required to support those operations regardless). If you wanted to find
	// a dedicated TRANSFER queue (e.g., such as those that exist on NVIDIA
	// GPUs), you would need to use different logic.
	std::optional<std::uint32_t> find_queue_family( VkPhysicalDevice aPhysicalDev, VkQueueFlags aQueueFlags, VkSurfaceKHR aSurface )
	{
		//done TODO: find queue family with the specified queue flags that can 
		//done TODO: present to the surface (if specified)

		std::uint32_t numQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( aPhysicalDev, &numQueues, nullptr );

		// get queue family properties
		std::vector<VkQueueFamilyProperties> families( numQueues );
		vkGetPhysicalDeviceQueueFamilyProperties( aPhysicalDev, &numQueues, families.data() );

		for( std::uint32_t i = 0; i < numQueues; ++i )
		{
			auto const& family = families[i];

			// check required queue flags
			if( aQueueFlags == (aQueueFlags & family.queueFlags) )
			{
				// if no surface requested:flags match is enough
				if( VK_NULL_HANDLE == aSurface )
					return i;

				// check present support
				VkBool32 supported = VK_FALSE;
				auto const res = vkGetPhysicalDeviceSurfaceSupportKHR( aPhysicalDev, i, aSurface, &supported );

				if( VK_SUCCESS == res && supported )
					return i;
			}

		}

		return {};
	}

	VkDevice create_device( VkPhysicalDevice aPhysicalDev, std::vector<std::uint32_t> const& aQueues, std::vector<char const*> const& aEnabledExtensions )
	{
		if( aQueues.empty() )
			throw lut::Error( "create_device(): no queues requested" );

		float queuePriorities[1] = { 1.f };

		std::vector<VkDeviceQueueCreateInfo> queueInfos( aQueues.size() );
		for( std::size_t i = 0; i < aQueues.size(); ++i )
		{
			auto& queueInfo = queueInfos[i];
			queueInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex  = aQueues[i];
			queueInfo.queueCount        = 1;
			queueInfo.pQueuePriorities  = queuePriorities;
		}

		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.independentBlend = VK_TRUE; // ���������������
		// No extra Vulkan 1.0 features for now.

		VkPhysicalDeviceVulkan13Features vk13{};
		vk13.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		vk13.synchronization2  = VK_TRUE;
		vk13.dynamicRendering  = VK_TRUE;

		VkPhysicalDeviceVulkan14Features vk14{};
		vk14.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
		vk14.pNext  = &vk13;
		vk14.maintenance5  = VK_TRUE; // Required in Vulkan 1.4, but we need to say that we want it.

		VkDeviceCreateInfo deviceInfo{};
		deviceInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		deviceInfo.queueCreateInfoCount     = std::uint32_t(queueInfos.size());
		deviceInfo.pQueueCreateInfos        = queueInfos.data();

		deviceInfo.enabledExtensionCount    = std::uint32_t(aEnabledExtensions.size());
		deviceInfo.ppEnabledExtensionNames  = aEnabledExtensions.data();

		deviceInfo.pEnabledFeatures         = &deviceFeatures;

		deviceInfo.pNext                    = &vk14;

		VkDevice device = VK_NULL_HANDLE;
		if( auto const res = vkCreateDevice( aPhysicalDev, &deviceInfo, nullptr, &device ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to create logical device\n"
				"vkCreateDevice() returned {}", lut::to_string(res)
			);
		}

		return device;
	}
}

namespace
{
	float score_device( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties( aPhysicalDev, &props );

		// Only consider Vulkan 1.4 devices ...
		auto const major = VK_API_VERSION_MAJOR( props.apiVersion );
		auto const minor = VK_API_VERSION_MINOR( props.apiVersion );

		if( major < 1 || (major == 1 && minor < 4) )
		{
			std::print( stderr, "Info: Discarding device '{}': insufficient vulkan version at {}.{}\n", props.deviceName, major, minor );
			return -1.f;
		}

		// ... with the required device features
		if( auto const missing = lut::detail::check_required_device_features( aPhysicalDev ); !missing.empty() )
		{
			std::print( stderr, "Info: Discarding device '{}': {} required features missing:\n", props.deviceName, missing.size() );
			for( auto const* feat : missing )
				std::print( "  - {}\n", feat );
			return -1.f;
		}

		//done TODO: additional checks
		//done TODO:  - check that the VK_KHR_swapchain extension is supported
		//done TODO:  - check that there is a queue family that can present to the
		//done TODO:    given surface
		//done TODO:  - check that there is a queue family that supports graphics
		//done TODO:    commands

		// check VK_KHR_swapchain extension is supported
        auto const exts = lut::detail::get_device_extensions( aPhysicalDev );

        if( !exts.count( VK_KHR_SWAPCHAIN_EXTENSION_NAME ) )
        {
            std::print( stderr, "Info: Discarding device '{}': extension {} missing\n", props.deviceName, VK_KHR_SWAPCHAIN_EXTENSION_NAME );
            return -1.f;
        }

        // check present queue available
        if( !find_queue_family( aPhysicalDev, 0, aSurface ) )
        {
            std::print( stderr, "Info: Discarding device '{}': can't present to surface\n", props.deviceName );
            return -1.f;
        }

        // check graphics queue available
        if( !find_queue_family( aPhysicalDev, VK_QUEUE_GRAPHICS_BIT ) )
        {
            std::print( stderr, "Info: Discarding device '{}': no graphics queue family\n", props.deviceName );
			return -1.f;
		}

		// Discrete GPU > Integrated GPU > others
		float score = 0.f;

		if( VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType )
			score += 500.f;
		else if( VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU == props.deviceType )
			score += 100.f;

		return score;
	}
	
	VkPhysicalDevice select_device( VkInstance aInstance, VkSurfaceKHR aSurface )
	{
		std::uint32_t numDevices = 0;
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, nullptr ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device count\n"
				"vkEnumeratePhysicalDevices() returned {}", lut::to_string(res)
			);
		}

		std::vector<VkPhysicalDevice> devices( numDevices, VK_NULL_HANDLE );
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, devices.data() ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device list\n"
				"vkEnumeratePhysicalDevices() returned {}", lut::to_string(res)
			);
		}

		float bestScore = -1.f;
		VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

		for( auto const device : devices )
		{
			auto const score = score_device( device, aSurface );
			if( score > bestScore )
			{
				bestScore = score;
				bestDevice = device;
			}
		}

		return bestDevice;
	}
}

