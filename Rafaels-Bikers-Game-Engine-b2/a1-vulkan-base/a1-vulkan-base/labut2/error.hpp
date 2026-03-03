#ifndef ERROR_HPP_5DA319F7_EA27_492B_8F48_C99E4201D720
#define ERROR_HPP_5DA319F7_EA27_492B_8F48_C99E4201D720
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <string>
#include <format>
#include <exception>

#include "defaults.hpp"

#if COMP5892_CONF_USE_STACKTRACE
#	include <stacktrace>
#endif // ~ USE_STACKTRACE

namespace labut2
{
	// Class used for exceptions. Unlike e.g. std::runtime_error, which only
	// accepts a "fixed" string, Error provides std::print()-like formatting.
	//
	// Example:
	//
	//	throw Error( "vkCreateInstance() returned {}", to_string(result) );
	//
	class Error : public std::exception
	{
		public:
			template< typename... tArgs >
			explicit Error( std::format_string<tArgs...>, tArgs&&... );

		public:
			char const* what() const noexcept override;

		private:
			std::string mMsg;
#			if COMP5892_CONF_USE_STACKTRACE
			std::stacktrace mTrace = std::stacktrace::current();
			mutable std::string mBuffer;
#			endif // ~ USE_STACKTRACE
	};



	template< typename... tArgs > inline
	Error::Error( std::format_string<tArgs...> aFmt, tArgs&&... aArgs )
		: mMsg( std::format( aFmt, std::forward<tArgs>(aArgs)... ) )
	{}
}

#endif // ERROR_HPP_5DA319F7_EA27_492B_8F48_C99E4201D720
