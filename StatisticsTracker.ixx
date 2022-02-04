// This module implements a statistics tracker used to generate a summeray 
module;

#include "VirtualizerBase.h"
#include <chrono>
#include <cmath>
#include <algorithm>

export module StatisticsTracker;
import DecompilerEngine;

using namespace std::chrono_literals;
namespace chrono = std::chrono;

// Text progress bar, temp, will be used later for statstics when im done with the CFG generator
export class TextProgressBar {
public:
	TextProgressBar(
		OPT            size_t       DesiredProgressBarWidth = 50,
		OPT    chrono::milliseconds IntervalBetweenRotation = 400ms,
		OPT const std::string_view& UpdateIndicator = "|\0/\0-\0\\\0Done...\0"
	)
		: ProgressBarWidth(DesiredProgressBarWidth),
		IntervalBetweenRotation(IntervalBetweenRotation),
		UpdateIndicator(UpdateIndicator) {
		TRACE_FUNCTION_PROTO;
	}

	std::string UpdateProgress( // Updates the progress and by default returns a formatted string,
								// the new progress cannot be less then the previous one,
								// however it can be 0 to indicate the previous value
		IN  float NewProgress,
		OPT bool  ReturnFormatted = true
	) {
		NewProgress = std::clamp<float>(NewProgress, 0, 1);
		if (NewProgress == 0)
			NewProgress = CurrentProgress;
		if (NewProgress < CurrentProgress)
			throw std::logic_error("Cannot update progress to a smaller value");
		CurrentProgress = NewProgress;

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

			auto WidthOfProgress = std::lround(ProgressBarWidth * CurrentProgress);
			// Desired result: "[=====         ] xx,xx% (MM:ss:mmmm)"
			return fmt::format("[[{0:=<{2}}{0: <{3}}] {4}% [{5}]]", "",
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
	// A timepoint of when the progress bar was created 
	chrono::time_point<chrono::steady_clock> 
		TimeOfCreation = chrono::steady_clock::now();
	
	size_t ProgressBarWidth;
	size_t RotationIndex = 0;
	float  CurrentProgress = 0;

	chrono::time_point<chrono::steady_clock>
		TimerSinceLastRotation{};
};


// Statistics summary required parameters:
// @0: The progress of the whole programm
// @1: The progress on the current analyzed frame
// @2: width of summary, headerline
// @3:
// @4:
// @5:
// @6:
// @7:
// @8:

// @9
#define STATISTICS_SUMMARY_FORMAT \
"Global Progress: {0}\n"\
"Frame  Progress: {1}\n"\
"{0:-<{3}}\n"\
"Frames analyzed: {} |"\
"Total CFG-Nodes: {} |"\
": {} |"\
": {} |"\
": {} |"\
": {} |"\
"Virtual frames:  {} |"\
"{0:-<{3}}\n"\
"Analyzing {}:{}"



export class StatisticsTracker 
	: public IDecompilerTracker {
public:
	using IntergerCountRatio = std::pair<uint32_t, uint32_t>;

	StatisticsTracker(
		IN chrono::milliseconds UpdateRefreshRateInterval
	) {
		TRACE_FUNCTION_PROTO;


	}

	void operator()(
		IN IDecompilerTracker::InformationUpdateType InformationType,
		IN IDecompilerTracker::UpdateInformation     UpdateType
		) const override {
		TRACE_FUNCTION_PROTO;

		switch (InformationType) {
		case IDecompilerTracker::TRACKER_CFGNODE_COUNT:
			++NumberOfNodesAnalyzed; break;
		case IDecompilerTracker::TRACKER_OVERLAYING_COUNT:
			++NumberOfOverlayDefects; break;
		case IDecompilerTracker::TRACKER_INSTRUCTION_COUNT:
			++NumberOfInstructions; break;
		case IDecompilerTracker::TRACKER_DECODE_ERRORS:
			++NumberOfDecodeErrors; break;
		case IDecompilerTracker::TRACKER_HEURISTICS_TRIGGER:
			++NoHeuristicsTriggered; break;
		case IDecompilerTracker::TRACKER_SPLICED_NODES:
			++NumberOfSlicedNodes; break;
		case IDecompilerTracker::TRACKER_STRIPPED_NODES:
			++NumberOfStrippedNodes; break;
		}
	}

	void TryPrint() {
		TRACE_FUNCTION_PROTO;



	}

private:
	IntergerCountRatio NumberOfFunctions;
	TextProgressBar    GlobalProgress;
	TextProgressBar    VirtualFrameProgress;

	// Standard expected 
	mutable uint32_t NumberOfNodesAnalyzed;
	mutable uint32_t NumberOfInstructions;
	mutable uint32_t NumberOfStrippedNodes;
	mutable uint32_t NumberOfSlicedNodes;
	mutable uint32_t NoHeuristicsTriggered;

	// Defects and errors and issues
	mutable uint32_t NumberOfOverlayDefects;
	mutable uint32_t NumberOfUnresolvedBr;
	mutable uint32_t NumberOfDecodeErrors;
	
};
