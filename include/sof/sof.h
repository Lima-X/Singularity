// The legacy support base, this file delcares and imports all legacy type libraries
// and serves as a configuration file for the project.
// Has to be included into every single module file making use of any of the support libraries
#pragma once

#pragma region Singularity parameter annotations
// Optional function parameter annotations
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef INOUT
#define INOUT
#endif
#ifndef OPT
#define OPT
#endif
#pragma endregion

#pragma region Disabled warnings and other pragmas
#ifndef SOFP_DONT_DISABLE_WARNINGS

// Disable anonymous class type warning, supported by all major compilers
#pragma warning(disable : 4201)

#endif
#pragma endregion

#pragma region Import and configure windows.h
#ifndef SOFP_NO_WINDOWS_DOT_H

// Disable specific API sets to reduce header size
//	#define NOGDICAPMASKS     // CC_*, LC_*, PC_*, CP_*, TC_*, RC_
	#define NOVIRTUALKEYCODES // VK_*
//	#define NOWINMESSAGES     // WM_*, EM_*, LB_*, CB_*
	#define NOWINSTYLES       // WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
	#define NOSYSMETRICS      // SM_*
	#define NOMENUS           // MF_*
	#define NOICONS           // IDI_*
	#define NOKEYSTATES       // MK_*
	#define NOSYSCOMMANDS     // SC_*
	#define NORASTEROPS       // Binary and Tertiary raster ops
//	#define NOSHOWWINDOW      // SW_*
	#define OEMRESOURCE       // OEM Resource values
	#define NOATOM            // Atom Manager routines
	#define NOCLIPBOARD       // Clipboard routines
	#define NOCOLOR           // Screen colors
//	#define NOCTLMGR          // Control and Dialog routines
	#define NODRAWTEXT        // DrawText() and DT_*
	#define NOGDI             // All GDI defines and routines
//	#define NOKERNEL          // All KERNEL defines and routines
// 	#define NOUSER            // All USER defines and routines
//	#define NONLS             // All NLS defines and routines
	#define NOMB              // MB_* and MessageBox()
	#define NOMEMMGR          // GMEM_*, LMEM_*, GHND, LHND, associated routines
	#define NOMETAFILE        // typedef METAFILEPICT
	#define NOMINMAX          // Macros min(a,b) and max(a,b)
// 	#define NOMSG             // typedef MSG and associated routines
	#define NOOPENFILE        // OpenFile(), OemToAnsi, AnsiToOem, and OF_*
	#define NOSCROLL          // SB_* and scrolling routines
	#define NOSERVICE         // All Service Controller routines, SERVICE_ equates, etc.
	#define NOSOUND           // Sound driver routines
	#define NOTEXTMETRIC      // typedef TEXTMETRIC and associated routines
	#define NOWH              // SetWindowsHook and WH_*
	#define NOWINOFFSETS      // GWL_*, GCL_*, associated routines
	#define NOCOMM            // COMM driver routines
	#define NOKANJI           // Kanji support stuff.
	#define NOHELP            // Help engine interface.
	#define NOPROFILER        // Profiler interface.
	#define NODEFERWINDOWPOS  // DeferWindowPos routines
	#define NOMCX             // Modem Configuration Extensions

// Specific supported windows versions and misc
#define NTDDI_VERSION NTDDI_WINBLUE
#define _WIN32_WINNT _WIN32_WINNT_WINBLUE
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
// #define WIN32_NO_STATUS
#include <Windows.h>
// #undef WIN32_NO_STATUS

// #include <winternl.h>
// #include <ntstatus.h>
#include <atlbase.h>
#include <timeapi.h>

#undef FACILITY_CONTROL
#endif
#pragma endregion

#pragma region LibC and STL base includes
// Configure base includes for shared base and libc
#ifndef SOFP_NO_CRT_SECURE
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <intrin.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#pragma endregion

#pragma region Import external dependencies
#ifndef SOFP_NO_EXTERNAL_INCLUDES

// Configure and import external legacy libraries
#ifndef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif
#define SPDLOG_LEVEL_NAMES { "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "OFF" }
#define SPDLOG_FMT_EXTERNAL
#define SPDLOG_DISABLE_DEFAULT_LOGGER
#include <spdlog/spdlog.h>
#include <spdlog/async.h>

extern "C" {
#include <xed/xed-interface.h>
}

#endif
#pragma endregion

#pragma region Logging macros and enums
#ifndef SOFP_NO_LOGGER_ESCAPE

// Special function trace logger insert/implant, use this in every functions head.
// Currently does nothing, to be implemented.
#define TRACE_FUNCTION_PROTO static_cast<void>(0)

// Define and declare the different types of supported logging escape sequences
#define ESC_COLOR(Color) "\x1b["#Color"m"
enum AnsiEscapeSequenceTag : int32_t {
	FORMAT_RESET_COLORS = 0,
	COLOR_BLACK = 30,
	COLOR_RED = 31,
	COLOR_GREEN = 32,
	COLOR_YELLOW = 33,
	COLOR_BLUE = 34,
	COLOR_MAGENTA = 35,
	COLOR_CYAN = 36,
	COLOR_WHITE = 37,
	COLOR_BRIGHTBLACK = 90,
	COLOR_BRIGHTRED = 91,
	COLOR_BRIGHTGREEN = 92,
	COLOR_BRIGHTYELLOW = 93,
	COLOR_BRIGHTBLUE = 94,
	COLOR_BRIGHTMAGENTA = 95,
	COLOR_BRIGHTCYAN = 96,
	COLOR_BRIGHTWHITE = 97,
}; 
#define ESC_RESET         "\x1b[0m"
#define ESC_BLACK         "\x1b[30m"
#define ESC_RED           "\x1b[31m"
#define ESC_GREEN         "\x1b[32m"
#define ESC_YELLOW        "\x1b[33m"
#define ESC_BLUE          "\x1b[34m"
#define ESC_MAGENTA       "\x1b[35m"
#define ESC_CYAN          "\x1b[36m"
#define ESC_WHITE         "\x1b[37m"
#define ESC_BRIGHTBLACK   "\x1b[90m"
#define ESC_BRIGHTRED     "\x1b[91m"
#define ESC_BRIGHTGREEN   "\x1b[92m"
#define ESC_BRIGHTYELLOW  "\x1b[93m"
#define ESC_BRIGHTBLUE    "\x1b[94m"
#define ESC_BRIGHTMAGENTA "\x1b[95m"
#define ESC_BRIGHTCYAN    "\x1b[96m"
#define ESC_BRIGHTWHITE   "\x1b[97m"

// Define the general logger pattern used by the library
#define SPDLOG_SINGULARITY_SMALL_PATTERN \
	"[ %^%=l%$ : " ESC_YELLOW"%t" ESC_RESET" ] %v"

#endif
#pragma endregion

// Legacy style type helpers
#define OFFSET_OF offsetof

namespace chrono = std::chrono;
namespace filesystem = std::filesystem;

#ifndef SOFP_DONT_IMPLEMENT_BASE
import sof.base;
#endif

#define MAKE_HRESULT(Severity, Facility, Code) \
		((Severity) << 31 | 1 << 29 | \
		(Facility) << 16 & 0x7ff0000 | \
		(Code) & 0xffff)
