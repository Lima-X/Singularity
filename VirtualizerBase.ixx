// Implements and declares basic helpers and utilities,
// including active template components and abstraction layers.
// 
module;

#include "VirtualizerBase.h"
#include <memory>
#include <utility>
#include <chrono>
#include <cmath>
#include <algorithm>

export module VirtualizerBase;

#pragma region Smart pointer abstractions for VirtualAlloc
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

// Text progress bar used for the console window title
using namespace std::chrono_literals;
namespace chrono = std::chrono;
export class TextProgressBar {
public:
	
	TextProgressBar(
		OPT            size_t       DesiredProgressBarWidth = 24,
		OPT    chrono::milliseconds IntervalBetweenRotation = 400ms,
		OPT const std::string_view& UpdateIndicator = "|\0/\0-\0\\0\\0Done...\0"
	)
		: ProgressBarWidth(DesiredProgressBarWidth),
		  IntervalBetweenRotation(IntervalBetweenRotation),
		  UpdateIndicator(UpdateIndicator) {
		TRACE_FUNCTION_PROTO;
	}
	
	std::string UpdateProgress( // Updates the progress and by default returns a formatted string,
								// the new progess cannot be less then the previous one,
		                        // however it can be 0 to indecate the previous value
		IN  float NewProgress,
		OPT bool  ReturnFormatted = true
	) {
		if (!NewProgress)
			NewProgress = CurrentProgress;
		if (NewProgress < CurrentProgress)
			throw std::logic_error("Cannot update progress to a smaller value");

		CurrentProgress = std::clamp<float>(NewProgress, 0, 1);

		if (ReturnFormatted) {

			auto TimePointNow = chrono::steady_clock::now();
			auto TimePassedIndicator = chrono::duration_cast<
				chrono::milliseconds>(
					TimePointNow - TimerSinceLastRotation);
			if (TimePassedIndicator >= IntervalBetweenRotation) {

				// Update work indicator
				++RotationIndex;
				TimerSinceLastRotation = TimePointNow;
			}

			// Format the progress bar and return string,
			// the format spec for this function accepts specific values
			// @0: An empty string used as a filler
			// @1: The width of the total progress bar
			// @2: The width for the current progress of the total width
			// @3: The inverse of the above field
			// @4: The the progress as a floating point from 0-100
			// @5: The work indicator symbol
			auto WidthOfProgress = std::lround(ProgressBarWidth * CurrentProgress);
			return fmt::format("[[{0:#<{2}}{0:-<{3}}] {4}% [{5}]]", "",
				ProgressBarWidth,
				WidthOfProgress,
				ProgressBarWidth - WidthOfProgress,
				CurrentProgress * 100,
				&UpdateIndicator.data()[(RotationIndex % 4) * 2]);
		}

		return{};
	}

	// constant progress format configuration
	const std::string_view UpdateIndicator;
	const chrono::milliseconds IntervalBetweenRotation;

private:
	size_t ProgressBarWidth;
	size_t RotationIndex = 0;
	float  CurrentProgress = 0;
	
	chrono::time_point<chrono::steady_clock>
	TimerSinceLastRotation{};
};
