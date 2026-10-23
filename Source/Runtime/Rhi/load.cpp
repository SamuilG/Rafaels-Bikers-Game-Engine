#include "load.hpp"

#include <cstdio>
#include <cassert>

// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include "error.hpp"

namespace labut2
{
	namespace {
		std::filesystem::path shaderDirectory;
	}

	void set_shader_directory( const std::filesystem::path& directory )
	{
		shaderDirectory = directory.empty() ? directory : std::filesystem::absolute(directory).lexically_normal();
	}

	std::vector<std::uint32_t> load_file_u32( char const* aPath )
	{
		assert( aPath );
		auto path = std::filesystem::path(aPath).lexically_normal();
		if (!shaderDirectory.empty() && !path.is_absolute()) {
			const auto relative = path.lexically_relative("Assets/Shaders/spirv");
			if (!relative.empty() && relative != "." && *relative.begin() != "..")
				path = shaderDirectory / relative;
		}
		const auto displayPath = path.generic_u8string();
		const std::string pathText(displayPath.begin(), displayPath.end());

#ifdef _WIN32
		if( std::FILE* fin = _wfopen( path.c_str(), L"rb" ) )
#else
		if( std::FILE* fin = std::fopen( path.c_str(), "rb" ) )
#endif
		{
			std::fseek( fin, 0, SEEK_END );
			auto const bytes = std::size_t(std::ftell( fin ));
			std::fseek( fin, 0, SEEK_SET );

			// SpirV consists of a number of 32-bit = 4 byte words
			assert( 0 == bytes % 4 );
			auto const words = bytes / 4;

			std::vector<std::uint32_t> code( words );
			
			std::size_t offset = 0;
			while( offset != words )
			{
				auto const read = std::fread( code.data()+offset, sizeof(std::uint32_t), words-offset, fin );

				if( 0 == read )
				{
					auto const err = std::ferror(fin), eof = std::feof(fin);
					std::fclose( fin );

					throw Error( "Error reading '{}': ferror = {}, feof = {}", pathText, err, eof );
				}

				offset += read;
			}

			std::fclose( fin );

			return code;
		}

		throw Error( "Cannot open '{}' for reading", pathText );
	}
}
