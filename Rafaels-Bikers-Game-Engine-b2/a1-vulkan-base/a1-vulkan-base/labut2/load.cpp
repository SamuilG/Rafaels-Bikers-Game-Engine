#include "load.hpp"

#include <cstdio>
#include <cassert>

// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include "error.hpp"

namespace labut2
{
	std::vector<std::uint32_t> load_file_u32( char const* aPath )
	{
		assert( aPath );

		if( std::FILE* fin = std::fopen( aPath, "rb" ) )
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

					throw Error( "Error reading '%s': ferror = %d, feof = %d", aPath, err, eof );
				}

				offset += read;
			}

			std::fclose( fin );

			return code;
		}

		throw Error( "Cannont open '{}' for reading", aPath );
	}
}
