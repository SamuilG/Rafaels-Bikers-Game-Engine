#include "error.hpp"

#include <sstream>

// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

namespace labut2
{
	char const* Error::what() const noexcept
	{
#		if !COMP5892_CONF_USE_STACKTRACE
		return mMsg.c_str();
#		else // USE_STACKTRACE
		if( mBuffer.empty() )
		{
			//mBuffer = std::format( "{}\n-- from --\n{}\n--\n", mMsg, mTrace );

			std::ostringstream oss;
			oss << mMsg << "\n";
		
			// Skip the first stack frame, since that's always inside the
			// Error::Error constructor, and therefore not all that useful.
			// Plus Error::Error() is a templated function, so it has quite an
			// ugly signature.
			std::size_t i = 1;
			for( auto it = std::next( mTrace.begin(), 1 ); it != mTrace.end(); ++it )
				oss << " | " << (i++) << "# " << *it << "\n";
			mBuffer = oss.str();
		}
		return mBuffer.c_str();
#		endif // ~ USE_STACKTRACE
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
