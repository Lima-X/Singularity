// 
module;

#include "VirtualizerBase.h"
#include <memory>
#include <utility>

export module VirtualizerBase;

export class XedAbstractor {
public:
	XedAbstractor() {

	}

private:


};

// unique_ptr support for VirtualAlloc
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

// using MakeVirtualUniquePointer = decltype(MakeSmartPointerWithVirtualAlloc<std::unique_ptr>);
// using MakeVirtualSharedPointer = decltype(MakeSmartPointerWithVirtualAlloc<std::shared_ptr>);
