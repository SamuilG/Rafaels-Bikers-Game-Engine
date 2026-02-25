#ifndef DEFAULTS_HPP_56407BDD_3DED_4A09_ACE7_D531BA8151B3
#define DEFAULTS_HPP_56407BDD_3DED_4A09_ACE7_D531BA8151B3
// SOLUTION_TAGS: vulkan-(ex-[^1]|cw-.)

#include <version>

/* Compile time config: Use <stacktrace>?
 *
 * Enabled by default unless NDEBUG is specified. If your compiler doesn't
 * support it, you can disable it by defining COMP5892_CONF_USE_STACKTRACE=0.
 */
#if !defined(COMP5892_CONF_USE_STACKTRACE)
#	if defined(NDEBUG)
#		define COMP5892_CONF_USE_STACKTRACE 0
#	else
#		if defined(__cpp_lib_stacktrace) && __cpp_lib_stacktrace >= 202011L
#			define COMP5892_CONF_USE_STACKTRACE 1
#		else /* unsupported */
#			define COMP5892_CONF_USE_STACKTRACE 0
#		endif
#	endif
#endif // ~ COMP5892_CONF_USE_STACKTRACE

#endif // DEFAULTS_HPP_56407BDD_3DED_4A09_ACE7_D531BA8151B3
