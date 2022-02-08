// This module implements a statistics tracker used to generate a summeray 
module;

#include "VirtualizerBase.h"
#include <chrono>
#include <cmath>
#include <algorithm>

export module StatisticsTracker;
import DisassemblerEngine;

using namespace std::chrono_literals;
namespace chrono = std::chrono;
export using uint24_t = uint32_t;

// Text progress bar, temp, will be used later for statstics when im done with the CFG generator
export class TextProgressBar {
public:
	TextProgressBar(
		OPT       size_t               DesiredProgressBarWidth = 50,
		OPT       chrono::milliseconds IntervalBetweenRotation = 400ms,
		OPT const std::string_view&    UpdateIndicator = "|\0/\0-\0\\\0Done...\0"
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

			// Desired result: "[=====         ] xx,xx% (MM:ss:mmmm)"
			auto WidthOfProgress = std::lround(ProgressBarWidth * CurrentProgress);
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
	const std::string          UpdateIndicator;
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

/*
Statically analyzing image and processing passes...
-------------------------------------------------------------------------------------
Global Progress: [######----------------------------------------] 17,24% (00:14:0789)
Sub Processing:  [####################################----------] 78,63% (00:01:0024)
--------------------------+----------------------------------------------------------
0000ffff72ab52ec:+00d2000 | FindObjectInNodeGraphByTokenAndReference
.text:+0036f30            |
--------------------------+----------+--------------------------------------------------
Frames analyzed: 742 / 3836          | Frames Virtualized: 4
Total CFG-Nodes: 2783                |      Nodes Mutated: 28
Opcodes decoded: 684902              |     Opcodes Lifted: 278
-------------------------------------+--------------------------------------------------
*/


export class StatisticsTracker 
	: public IDisassemblerTracker {

	static constexpr const char* SummaryCurrentStates[]{
		"Initializing process and virtualizer...",
		"Trying to load symbol file for executable...",
		"Trying to load and map image into process memory...",
		"Relocating image in memory...",
		"",
		"Compiling new virtual/mutated comdat, and link into image",
		"Regenerating relocation table for mutations"
	};
	static constexpr const char* SummaryViewLines[]{
		// Primary header: Current status line and progress
		"{0:-<86}",
		"Primary-Progress: {}",
		"    Sub-Progress: {}",

		// Non primary header: location of interest + symbol (if available)
		"{0:-<27}+{0:-<58}",
		"{:016x}:+{:07x} | {:67s}"
		"{:<8s}:+{:07x}{: <{}}|"

		// Nph: Meta statistics for a quick over view
		"Frames analyzed: {} / {}{: <{}}|",
		"Total CFG-Nodes: {:}|",
	};



public:
	using IntergerCountRatio = std::pair<uint32_t, uint32_t>;

	StatisticsTracker(
		IN chrono::milliseconds UpdateRefreshRateInterval
	) {
		TRACE_FUNCTION_PROTO;


	}

	void operator()(
		IN IDisassemblerTracker::InformationUpdateType InformationType,
		IN IDisassemblerTracker::UpdateInformation     UpdateType
		) override {
		TRACE_FUNCTION_PROTO;

		switch (InformationType) {
		case IDisassemblerTracker::TRACKER_CFGNODE_COUNT:
			++NumberOfNodesAnalyzed; break;
		case IDisassemblerTracker::TRACKER_OVERLAYING_COUNT:
			++NumberOfOverlayDefects; break;
		case IDisassemblerTracker::TRACKER_INSTRUCTION_COUNT:
			++NumberOfInstructions; break;
		case IDisassemblerTracker::TRACKER_DECODE_ERRORS:
			++NumberOfDecodeErrors; break;
		case IDisassemblerTracker::TRACKER_HEURISTICS_TRIGGER:
			++NoHeuristicsTriggered; break;
		case IDisassemblerTracker::TRACKER_SPLICED_NODES:
			++NumberOfSlicedNodes; break;
		case IDisassemblerTracker::TRACKER_STRIPPED_NODES:
			++NumberOfStrippedNodes; break;
		}
	}

	void TryPrint() {
		TRACE_FUNCTION_PROTO;

		// Move back the cursor to the previous location 
		fmt::print("\x1B[{}F", NumberOfLinesPrinted);

		// Print Progressbars 

	}

private:

	IntergerCountRatio NumberOfFunctions;
	TextProgressBar    GlobalProgress;
	TextProgressBar    VirtualFrameProgress;

	// Standard expected 
	uint32_t NumberOfNodesAnalyzed;
	uint32_t NumberOfInstructions;
	uint32_t NumberOfStrippedNodes;
	uint32_t NumberOfSlicedNodes;
	uint32_t NoHeuristicsTriggered;

	// Defects and errors and issues
	uint32_t NumberOfOverlayDefects;
	uint32_t NumberOfUnresolvedBr;
	uint32_t NumberOfDecodeErrors;
	
	uint32_t NumberOfLinesPrinted = 0;


};

