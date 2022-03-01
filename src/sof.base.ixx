// The newer, modern module based, second component of the sof base,
// this is automatically imported by anyone using sof/sof.h
// and provides all the non meta implementations for base types and others,
// that are not provides through the old legacy type header.
module;

#define SOFP_DONT_IMPLEMENT_BASE
#include "sof/sof.h"
#include <exception>
#include <string>
#include <unordered_map>

export module sof.base;
export namespace sof {


#pragma region Singularity types and helpers
	export enum {
		PAGE_SIZE = 4096,
		LARGE_PAGE = 2097152,
		PAGE_GRANULARITY = 65536,
	};

	// Project specific types with special meaning
	export using ulong_t = unsigned long; //
	export using long_t = signed long;	  //
	export using offset_t = ptrdiff_t;    // Type used to store an offset with a maximum capacity of the architectures size
	export using byte_t = uint8_t;        // Type used to store arbitrary data in the form of a byte with 8 bits
	export using rva_t = long_t;          // Type used to describe a 31bit image relative offset, anything negative is invalid
	export using disp_t = long_t;         // Type used to represent a 32bit displacement
	export using token_t = size_t;        // Type used for tokenized ids, each token is unique to in the process lifetime

	export using FunctionAddress = std::pair<byte_t*, size_t>;
	export using func_addr_t = std::pair<byte_t*, disp_t>;
#pragma endregion



#pragma region Singularitys common exception type
	// Internally uses the format of HRESULT
	export class SingularityException
		: public std::exception {
	public:
	#pragma region Status_code_components_and_translation_table
		enum ExceptionSeverity {
			SEVERITY_ERROR_NONE = 0,
			SEVERITY_ERROR_YES
		};
		enum ExceptionFacility {
			FACILITY_UNSPECIFIED = 0,

			FACILITY_DISASSEMBLER = 0x400,
			FACILITY_LLIR_LIFTER,
			FACILITY_CLI_APP,
			FACILITY_GUI_APP,
			FACILITY_CONTROL,
			FACILITY_TASKS_MGR,
			FACILITY_SYMBOL_HELPER,
			FACILITY_IMAGE_EDITOR,
			FACILITY_IMAGE_LOADER,
			FACILITY_RESERVED,
			FACILITY_SOF_BASE,
			FACILITY_SOF_LLIR,
		};
	#define EXCEPTION_TABLE_CONTENT \
		EXCEPTION_TABLE_ENTRY(STATUS_EXCEPTION_NO_ERROR, = MAKE_HRESULT(SEVERITY_ERROR_NONE, FACILITY_UNSPECIFIED, 0))\
		EXCEPTION_TABLE_ENTRY(STATUS_INDETERMINED_FUNCTION, = MAKE_HRESULT(SEVERITY_ERROR_YES, FACILITY_DISASSEMBLER, 0xffff))\
		EXCEPTION_TABLE_ENTRY(STATUS_CODE_LEAVING_FUNCTION)\
		EXCEPTION_TABLE_ENTRY(STATUS_CFGNODE_WAS_TERMINATED)\
		EXCEPTION_TABLE_ENTRY(STATUS_XED_NO_MEMORY_BAD_POINTER)\
		EXCEPTION_TABLE_ENTRY(STATUS_XED_BAD_CALLBACKS_OR_MISSING)\
		EXCEPTION_TABLE_ENTRY(STATUS_XED_UNSUPPORTED_ERROR)\
		EXCEPTION_TABLE_ENTRY(STATUS_MISMATCHING_CFG_OBJECT)\
		EXCEPTION_TABLE_ENTRY(STATUS_DFS_INVALID_SEARCH_CALL)\
		EXCEPTION_TABLE_ENTRY(STATUS_OVERLAYING_CODE_DETECTED)\
		EXCEPTION_TABLE_ENTRY(STATUS_NESTED_TRAVERSAL_DISALLOWED)\
		EXCEPTION_TABLE_ENTRY(STATUS_UNSUPPORTED_RETURN_VALUE)\
		EXCEPTION_TABLE_ENTRY(STATUS_SPLICING_FAILED_EMPTY_NODE)\
		EXCEPTION_TABLE_ENTRY(STATUS_STRIPPING_FAILED_EMPTY_NODE)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_QUEUE_WORKITEM, = MAKE_HRESULT(SEVERITY_ERROR_YES, FACILITY_TASKS_MGR, 0xffff))\
		EXCEPTION_TABLE_ENTRY(STATUS_CANNOT_FREE_IN_PROGRESS)\
		EXCEPTION_TABLE_ENTRY(STATUS_FREED_UNREGISTERED_OBJ)\
		EXCEPTION_TABLE_ENTRY(STATUS_ABORTED_OBJECT_IN_CALL)\
		EXCEPTION_TABLE_ENTRY(EXCEPTION_CANNOT_BE_NON_STATIC, = MAKE_HRESULT(SEVERITY_ERROR_YES, FACILITY_SYMBOL_HELPER, 0xffff))\
		EXCEPTION_TABLE_ENTRY(EXCEPTION_FAILED_TO_FETCH_TYPE)\
		EXCEPTION_TABLE_ENTRY(EXCEPTION_FAILED_RVA_FOR_SYMBOL)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_TO_OPEN_FILE, = MAKE_HRESULT(SEVERITY_ERROR_YES, FACILITY_IMAGE_LOADER, 0xffff))\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_TO_CLOSE_FILE)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_FILE_POINTER)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_IO_OPERATION)\
		EXCEPTION_TABLE_ENTRY(STATUS_SIZE_NOT_BIGGER_OR_EQUAL)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_SIGNATURE_TEST)\
		EXCEPTION_TABLE_ENTRY(STATUS_IMAGEHELP_REFUSE_WORK)\
		EXCEPTION_TABLE_ENTRY(STATUS_FAILED_TO_CREATE_MAP)\
		EXCEPTION_TABLE_ENTRY(STATUS_INVALID_MEMORY_ADDRESS)\
		EXCEPTION_TABLE_ENTRY(STATUS_IMAGE_NOT_SUITABLE)\
		EXCEPTION_TABLE_ENTRY(STATUS_UNSUPPORTED_RELOCATION)\
		EXCEPTION_TABLE_ENTRY(STATUS_HANDLE_ALREADY_REJECTED)\
		EXCEPTION_TABLE_ENTRY(STATUS_IMAGE_ALREADY_MAPPED)\
		EXCEPTION_TABLE_ENTRY(STATUS_ALREADY_UNMAPPED_VIEW)\
		EXCEPTION_TABLE_ENTRY(STATUS_ABORTED_BY_ITRACKER)

	#define EXCEPTION_TABLE_ENTRY(Exception, Assignment) \
		Exception Assignment,
		enum ExceptionCode : int32_t {
			EXCEPTION_TABLE_CONTENT
		};
	#undef EXCEPTION_TABLE_ENTRY
	#define EXCEPTION_TABLE_ENTRY(Exception, Assignment) \
		{ Exception, #Exception },
		inline static const
		std::unordered_map<int32_t, std::string_view>
		ExceptionTranslationTable{
			EXCEPTION_TABLE_CONTENT
		};
	#undef EXCEPTION_TABLE_ENTRY
	#pragma endregion

		explicit SingularityException(
			IN const std::string_view& ExceptionText,
			IN       int32_t           StatusCode
		) noexcept
			: ExceptionText(ExceptionText),
			StatusCode(static_cast<ExceptionCode>(StatusCode)) {
			TRACE_FUNCTION_PROTO;
		}

		const char* what() const noexcept final {
			TRACE_FUNCTION_PROTO;
			auto StringForKey = ExceptionTranslationTable.find(StatusCode);
			return StringForKey != ExceptionTranslationTable.end() ? StringForKey->second.data() :
				(WhatMessageContainer = fmt::format("{:08x}", StatusCode)).c_str();
		}

		bool GetExceptionSeverity() const noexcept {
			TRACE_FUNCTION_PROTO; return StatusCode < 0;
		}
		ExceptionFacility GetExceptionFacility() const noexcept {
			TRACE_FUNCTION_PROTO; return static_cast<ExceptionFacility>(StatusCode >> 16 & 0x7ff);
		}
		int_fast16_t GetRawExceptionCode() const noexcept {
			TRACE_FUNCTION_PROTO; return StatusCode & 0xffff;
		}

		const std::string   ExceptionText;
		const ExceptionCode StatusCode;

	private:
		mutable std::string WhatMessageContainer;
	};
#pragma endregion


#pragma region Active template library components
#pragma region Smartpointer abstractions for VirtualAlloc
	class VirtualDeleter {
	public:
		void operator()(
			IN byte_t* VirtualAddress
			) {
			VirtualFree(VirtualAddress,
				0,
				MEM_RELEASE);
		}
	};
	export template<template<typename T2, class D> class T>
		class VirtualPointer final :
		public T<byte_t[], VirtualDeleter> {
		using BaseType = T<byte_t[], VirtualDeleter>;
		public:
			// BUG: Due to a bug in MSVC version 19.30.30709 (shipped with VS 17.0.5) and down,
			// the using below causes a internal compiler bug, resulting in an error
			// as a workaround for now a forwarding constructor template is provided:
			// using BaseType::BaseType;
			template<typename... Args>
			VirtualPointer(Args... Arguments) :
				BaseType(std::forward<Args>(Arguments)...) {
				TRACE_FUNCTION_PROTO;
			}

			void* CommitVirtualRange(
				IN byte_t* VirtualAddress,
				IN size_t  VirtualRange,
				IN DWORD   Win32PageProtection = PAGE_READWRITE
			) {
				return VirtualAlloc(VirtualAddress,
					VirtualRange,
					MEM_COMMIT,
					Win32PageProtection);
			}
	};

	export template<template<typename, class> class T>
		VirtualPointer<T>
		MakeSmartPointerWithVirtualAlloc(
			IN void* DesiredAddress,
			IN size_t SizeOfBuffer,
			IN DWORD  Wint32AllocationType = MEM_RESERVE | MEM_COMMIT,
			IN DWORD  Win32PageProtection = PAGE_READWRITE
		) {
		return VirtualPointer<T>(
			static_cast<byte_t*>(
				VirtualAlloc(DesiredAddress,
					SizeOfBuffer,
					Wint32AllocationType,
					Win32PageProtection)));
	}
#pragma endregion

	// CRTP helper for abstracting and adding skills to a CRTP base
	export template<typename T>
		class CrtpHelp {
		public:
			T& GetUnderlyingCrtpBase() {
				TRACE_FUNCTION_PROTO; return static_cast<T&>(*this);
			}
			const T& GetUnderlyingCrtpBase() const {
				TRACE_FUNCTION_PROTO; return static_cast<const T&>(*this);
			}
	};
#pragma endregion

#pragma region Unicode and partial-utf8/ansi conversions
	export using UnicodeString = std::wstring;
	export using UnicodeView = std::wstring_view;

	// Beyond ugly, partially utf8 aware, ansi to and from unicode converters
	export UnicodeString ConvertAnsiToUnicode(
		IN const std::string_view& AnsiToUnicodeString
	) {
		TRACE_FUNCTION_PROTO;

		if (AnsiToUnicodeString.empty())
			return {};
		auto RequiredBufferSize = MultiByteToWideChar(CP_UTF8, 0,
			AnsiToUnicodeString.data(),
			static_cast<int32_t>(AnsiToUnicodeString.size()),
			nullptr, 0);
		if (!RequiredBufferSize)
			throw std::runtime_error("Could not calculate required string length");
		UnicodeString ResultString;
		ResultString.resize(RequiredBufferSize);
		RequiredBufferSize = MultiByteToWideChar(CP_UTF8, 0,
			AnsiToUnicodeString.data(),
			static_cast<int32_t>(AnsiToUnicodeString.size()),
			ResultString.data(),
			static_cast<int32_t>(ResultString.capacity()));
		return ResultString;
	}
	export std::string ConvertUnicodeToAnsi(
		IN const UnicodeView& UnicodeToAnsiString
	) {
		TRACE_FUNCTION_PROTO;
		if (UnicodeToAnsiString.empty())
			return {};
		auto RequiredBufferSize = WideCharToMultiByte(CP_UTF8, 0,
			UnicodeToAnsiString.data(),
			static_cast<int32_t>(UnicodeToAnsiString.size()),
			nullptr, 0,
			nullptr, nullptr);
		if (!RequiredBufferSize)
			throw std::runtime_error("Could not calculate required string length");

		std::string ResultString;
		ResultString.resize(RequiredBufferSize);
		RequiredBufferSize = WideCharToMultiByte(CP_UTF8, 0,
			UnicodeToAnsiString.data(),
			static_cast<int32_t>(UnicodeToAnsiString.size()),
			ResultString.data(),
			static_cast<int32_t>(ResultString.capacity()),
			nullptr, nullptr);
		return ResultString;
	}
#pragma endregion


	export token_t GenerateGlobalUniqueTokenId() { // Generates a global token in a thread safe manner, 
												   // the implementation is free to change at any time
		TRACE_FUNCTION_PROTO;

		static std::mutex TokenGenLock;
		std::lock_guard ScopedLock(TokenGenLock);
		static token_t InitialToken = 0;
		return ++InitialToken;
	}

	export std::string FormatWin32ErrorMessage(
		IN HRESULT Win32ErrorCode
	) {
		TRACE_FUNCTION_PROTO;

		return "FORMAT NOT IMPLEMENTED";
	}
}
