#ifndef LOAD_HPP_919B2254_760B_4897_8713_43A67C2F69F4
#define LOAD_HPP_919B2254_760B_4897_8713_43A67C2F69F4
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <vector>
#include <filesystem>

#include <cstdint>

namespace labut2
{
	// Bind engine-owned SPIR-V before mounting a project's working directory.
	// Explicit shader paths (for example GPU test fixtures) remain unchanged.
	void set_shader_directory( const std::filesystem::path& directory );
	std::vector<std::uint32_t> load_file_u32( char const* aPath );
}

#endif // LOAD_HPP_919B2254_760B_4897_8713_43A67C2F69F4
