#ifndef TO_STRING_HPP_81001657_75D6_46D8_8C52_74DCB41D2631
#define TO_STRING_HPP_81001657_75D6_46D8_8C52_74DCB41D2631

#include <volk/volk.h>

#include <string>

#include <cstdint>

namespace labut2
{
	std::string to_string( VkResult );
	std::string to_string( VkPhysicalDeviceType );
	std::string to_string( VkDebugUtilsMessageSeverityFlagBitsEXT );

	std::string queue_flags( VkQueueFlags );
	std::string message_type_flags( VkDebugUtilsMessageTypeFlagsEXT );
	std::string memory_heap_flags( VkMemoryHeapFlags );
	std::string memory_property_flags( VkMemoryPropertyFlags );

	std::string driver_version( std::uint32_t aVendorId, std::uint32_t aDriverVersion );
}

#endif // TO_STRING_HPP_81001657_75D6_46D8_8C52_74DCB41D2631
