// Project core, this implements the combination of all other modules into the virtualizer controller
//
module;

#include "sof/sof.h"
#include <imgui.h>
#include <shlobj_core.h>
#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>

export module sof.control.gui;

import ImageHelp;
import SymbolHelp;
import DisassemblerEngine;

import sof.control.tasks;


using namespace std::literals::chrono_literals;

#pragma region Visual mode interface

#pragma region COM OpenFileDialog
// Highly inefficient and horribly ugly shitty code, but its user driven anyways...
// TODO: Exchange some stuff here for std::filesystem
export struct OpenFileDialogConfig {
	bool        ClearClientData = false;
	std::string DefaultExtension;	
	std::string DefaultFolder;
	bool        ForceDefaultFolder = false;
	std::string DefaultSelection;
	std::string FileNameLabel;

	struct ComFileFilter {
		std::string FriendlyName;
		std::string FilterExpression;
	};
	std::vector<ComFileFilter> FileFilters;
	uint32_t                   DefaultTypeIndex;

	std::string ButtonText;
	uint32_t    RemoveFlagsMask;
	uint32_t    AddFlagsMask;
	std::string DialogTitle;
};

export std::string ComOpenFileDialogWithConfig(
	IN const OpenFileDialogConfig& DialogConfig
) {
	TRACE_FUNCTION_PROTO;

	CComPtr<IFileOpenDialog> OpenFileDialog;
	auto ComResult = OpenFileDialog.CoCreateInstance(CLSID_FileOpenDialog,
		nullptr,
		CLSCTX_INPROC_SERVER);
	if (FAILED(ComResult)) {

		SPDLOG_ERROR("COM failed to open IFileOpenDialog");
		return {};
	}

	#define FILEDLG_CALL_COMMON(ConfigMember, FunctionPointer) \
	if (DialogConfig.ConfigMember.size()) {\
	auto CommonString = ConvertAnsiToUnicode(DialogConfig.ConfigMember);\
	ComResult = OpenFileDialog->FunctionPointer(CommonString.c_str());\
	if (FAILED(ComResult))\
		return {}; }

	if (DialogConfig.ClearClientData)
		OpenFileDialog->ClearClientData();
	FILEDLG_CALL_COMMON(DefaultExtension, SetDefaultExtension)

	if (DialogConfig.DefaultFolder.size()) {

		SPDLOG_ERROR("Default Folders are currently not supported, ignored");
#if 0
		auto DefaultFolder2 = ConvertAnsiToUnicode(DialogConfig.DefaultFolder);
		ComResult = DialogConfig.ForceDefaultFolder ? 
			OpenFileDialog->SetFolder()
#endif
	}

	FILEDLG_CALL_COMMON(DefaultSelection, SetFileName)
	FILEDLG_CALL_COMMON(FileNameLabel, SetFileNameLabel)

	if (DialogConfig.FileFilters.size()) {

		// An atrocious monster have i created...
		std::vector<COMDLG_FILTERSPEC> Filters;
		std::vector<UnicodeString> StringRetainer;
		for (auto& AbstractFilter : DialogConfig.FileFilters) {
			StringRetainer.emplace_back(
				ConvertAnsiToUnicode(AbstractFilter.FriendlyName));
			StringRetainer.emplace_back(
				ConvertAnsiToUnicode(AbstractFilter.FilterExpression));
		}
		for (auto i = 0; i < StringRetainer.size(); i += 2)
			Filters.emplace_back(StringRetainer[i].c_str(),
				StringRetainer[i + 1].c_str());
	
		ComResult = OpenFileDialog->SetFileTypes(static_cast<uint32_t>(Filters.size()),
			Filters.data());
		if (FAILED(ComResult))
			return {};
		
		if (DialogConfig.DefaultTypeIndex) {

			ComResult = OpenFileDialog->SetFileTypeIndex(DialogConfig.DefaultTypeIndex);
			if (FAILED(ComResult))
				return {};
		}
	}

	FILEDLG_CALL_COMMON(ButtonText, SetOkButtonLabel)

	if (DialogConfig.RemoveFlagsMask ||
		DialogConfig.AddFlagsMask) {

		FILEOPENDIALOGOPTIONS PreviousFlags;
		ComResult = OpenFileDialog->GetOptions(&PreviousFlags);
		if (FAILED(ComResult))
			return {};
		PreviousFlags &= ~DialogConfig.RemoveFlagsMask;
		PreviousFlags |= DialogConfig.AddFlagsMask;
		ComResult = OpenFileDialog->SetOptions(PreviousFlags);
		if (FAILED(ComResult))
			return {};
	}

	FILEDLG_CALL_COMMON(DialogTitle, SetTitle)


	ComResult = OpenFileDialog->Show(NULL);
	if (FAILED(ComResult))
		return {};
	CComPtr<IShellItem> SelectedShellItem;
	ComResult = OpenFileDialog->GetResult(&SelectedShellItem);
	if (FAILED(ComResult))
		return {};
	
	LPWSTR SelectedFileName;
	ComResult = SelectedShellItem->GetDisplayName(SIGDN_FILESYSPATH,
		&SelectedFileName);
	if (FAILED(ComResult))
		return {};

	auto SelectedFileName2 = ConvertUnicodeToAnsi(SelectedFileName);
	CoTaskMemFree(SelectedFileName);
	return SelectedFileName2;
}
#pragma endregion




export class VisualTrackerApp
	: public IImageLoaderTracker,
	  public IDisassemblerTracker {
public:
	VisualTrackerApp(
		IN const VisualSingularityApp& ControllerApp
	)
		: ApplicationController(ControllerApp) {
		TRACE_FUNCTION_PROTO;
	}


	void DoCommonAbortChecksAndDispatch() {

	}
	void DoWaitTimeInterval() {
		TRACE_FUNCTION_PROTO;

		// A thread safe and automatically cleaning up timer raiser, based on raii and static initialization
		static class PeriodBeginHighResolutionClock {
		public:
			PeriodBeginHighResolutionClock() {
				TRACE_FUNCTION_PROTO;

				// Get system timer resolution bounds and try to raise the clock
				auto Status = timeGetDevCaps(&TimerCaps,
					sizeof(TimerCaps));
				switch (Status) {
				case MMSYSERR_ERROR:
				case TIMERR_NOCANDO:
					SPDLOG_WARN("Failed to query system resolution bounds, assume 2000hz - 64hz");
					TimerCaps.wPeriodMax = 15;
					TimerCaps.wPeriodMin = 1;
					break;
				}
			}
			~PeriodBeginHighResolutionClock() {
				TRACE_FUNCTION_PROTO;

				if (RaisedSystemClock) {

					auto Status = timeEndPeriod(TimerCaps.wPeriodMin);
					if (Status == TIMERR_NOCANDO)
						SPDLOG_ERROR("Failed to lower clock manually");
				}
			}

			void RaiseSystemTimerResolutionOnce() {
				TRACE_FUNCTION_PROTO;

				if (RaisedSystemClock)
					return;
				auto Status = timeBeginPeriod(TimerCaps.wPeriodMin);
				if (Status == TIMERR_NOCANDO)
					SPDLOG_ERROR("Failed to raise systemclock, assuming {}ms",
						TimerCaps.wPeriodMin);
				RaisedSystemClock = true;
			}

			const TIMECAPS& GetTimeCaps() const {
				TRACE_FUNCTION_PROTO; return TimerCaps;
			}

		private:
			TIMECAPS TimerCaps{};
			bool     RaisedSystemClock = false;

		} ClockLift;

		// If the sleep interval is not big enough we just spinlock the cpu,
		// this is the reason the virtual slow down function should not be used
		// this is simply just there to exist and allow something that is 
		// impractical anyways, just useful to more closely examine the process
		for (auto TimeBegin = chrono::steady_clock::now(),
			TimeNow = chrono::steady_clock::now();
			chrono::duration_cast<chrono::microseconds>(TimeNow - TimeBegin)
				<= chrono::microseconds(VirtualSleepSliderInUs);
			TimeNow = chrono::steady_clock::now()) {

			// In the rapid loop we additionally check if we can physically sleep
			if (chrono::milliseconds(VirtualSleepSliderInUs / 1000) >=
				chrono::milliseconds(ClockLift.GetTimeCaps().wPeriodMin)) {

				// The wait time is big enough to physically sleep
				ClockLift.RaiseSystemTimerResolutionOnce();
				SleepEx(1, false);
			}
		}

		while (PauseSeriveExecution)
			SleepEx(1, false);
	}

	virtual void SetReadSectionCountOfView(
		uint32_t NumberOfSections
	) {
		TRACE_FUNCTION_PROTO; 
		NumberOfSectionsInImage = NumberOfSections;
	}


	void SetAddressOfInterest(
		IN byte_t* VirtualAddress,
		IN size_t  VirtualSize
	) override {
		TRACE_FUNCTION_PROTO;
		CurrentAddressOfInterest = VirtualAddress;
		VirtualSizeOfInterets = VirtualSize;
	}

	void UpdateTrackerOrAbortCheck(
		IN IImageLoaderTracker::TrackerInfoTag TrackerType
	) override {
		TRACE_FUNCTION_PROTO;

		switch (TrackerType) {
		case  IImageLoaderTracker::TRACKER_UPDATE_SECTIONS:
			++NumberOfSectionsLoaded;
			break;
		case IImageLoaderTracker::TRACKER_UPDATE_RELOCS:
			++RelocsApplied;
			break;
		}

		DoCommonAbortChecksAndDispatch();
		DoWaitTimeInterval();
	}

	void UpdateTrackerOrAbortCheck(
		IN IDisassemblerTracker::InformationUpdateType TrackerType
	) override {
		TRACE_FUNCTION_PROTO;

		//switch (TrackerType) {
		//
		//default:
		//}

		DoCommonAbortChecksAndDispatch();
		DoWaitTimeInterval();
	}

	void ResetData() {
		TRACE_FUNCTION_PROTO;

		CurrentAddressOfInterest = 0;
		VirtualSizeOfInterets = 0;
		RelocsApplied = 0;
		NumberOfSectionsInImage = 0;
		NumberOfSectionsLoaded = 0;
		NumberOfFramesProcessed = 0;
		TotalNumberOfFrames = 0;
		NumberOfAllocatedCfgNodes = 0;
		NumebrOfInstructionsDecoded = 0;
	}

	std::atomic<byte_t*> CurrentAddressOfInterest = nullptr;
	std::atomic_size_t   VirtualSizeOfInterets = 0;

	// Load Statistics
	std::atomic_uint32_t RelocsApplied = 0;
	std::atomic_uint32_t NumberOfSectionsInImage = 0;
	std::atomic_uint32_t NumberOfSectionsLoaded = 0;

	// SOB pass statistics
	std::atomic_uint32_t NumberOfFramesProcessed = 0;
	std::atomic_uint32_t TotalNumberOfFrames = 0;
	std::atomic_uint32_t NumberOfAllocatedCfgNodes = 0;
	std::atomic_uint32_t NumebrOfInstructionsDecoded = 0;


	// Utility for thrashing
	std::atomic_bool     PauseSeriveExecution = false;
	std::atomic_uint32_t VirtualSleepSliderInUs;

private:
	const VisualSingularityApp& ApplicationController;
};
#pragma endregion



export void HelpMarker(
	IN const char* Description
) {
	TRACE_FUNCTION_PROTO;

	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(Description);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// TODO: work on filters, they are really needed, did optimize the memory storage quite a bit tho
export class ImGuiSpdLogAdaptor
	: public spdlog::sinks::base_sink<std::mutex> {
	using sink_t = spdlog::sinks::base_sink<std::mutex>;

	class SinkLineContent {
	public:
		spdlog::level::level_enum LogLevel;   // If n_levels, the message pushed counts to the previous pushed line
		int32_t                   BeginIndex; // Base offset into the text buffer

		struct ColorDataRanges {
			uint32_t SubStringBegin : 12;
			uint32_t SubStringEnd   : 12;
			uint32_t FormatTag      : 8;
		};
		ImVector<ColorDataRanges> FormattedStringRanges;
	};

public:
	void DrawLogWindow(
		IN    GLFWwindow* ClipBoardOwner,
		INOUT bool&       ShowWindow
	) {
		TRACE_FUNCTION_PROTO;

		if (ImGui::Begin("Singularity Log", &ShowWindow)) {

			// Options submenu menu
			if (ImGui::BeginPopup("Options")) {

				ImGui::Checkbox("Auto-scroll", &EnableAutoScrolling);
				ImGui::EndPopup();
			}
			if (ImGui::Button("Options"))
				ImGui::OpenPopup("Options");
			ImGui::SameLine();
			if (ImGui::Button("Copy"))
				glfwSetClipboardString(ClipBoardOwner, LoggedContent.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
				ClearLogBuffers();
			ImGui::SameLine();
			ImGui::Text("%d messages logged, using %dmb memory",
				NumberOfLogEntries,
				(LoggedContent.size() + IndicesInBytes) / (1024 * 1024));

			// Filter out physical messgaes through logger
			static const char* LogLevels[] = SPDLOG_LEVEL_NAMES;
			static const auto LogSelectionWidth = []() -> float {
				TRACE_FUNCTION_PROTO;

				float LongestTextWidth = 0;
				for (auto LogLevelText : LogLevels) {

					auto TextWidth = ImGui::CalcTextSize(LogLevelText).x;
					if (TextWidth > LongestTextWidth)
						LongestTextWidth = TextWidth;
				}

				return LongestTextWidth +
					ImGui::GetStyle().FramePadding.x * 2 +
					ImGui::GetFrameHeight();
			}();
			auto ComboBoxRightAlignment = ImGui::GetWindowSize().x -
				(LogSelectionWidth + ImGui::GetStyle().WindowPadding.x);
			auto ActiveLogLevel = spdlog::get_level();
			ImGui::SetNextItemWidth(LogSelectionWidth);
			ImGui::SameLine(ComboBoxRightAlignment);
			ImGui::Combo("##ActiveLogLevel",
				reinterpret_cast<int32_t*>(&ActiveLogLevel),
				LogLevels,
				sizeof(LogLevels) / sizeof(LogLevels[0]));
			spdlog::set_level(ActiveLogLevel);

			// Filter out messages on display
			const std::lock_guard LogLock(sink_t::mutex_);
			FilterTextMatch.Draw("##LogFilter",
				ImGui::GetWindowSize().x - (LogSelectionWidth + ImGui::GetStyle().WindowPadding.x * 2 +
					ImGui::GetStyle().FramePadding.x));
			ImGui::SetNextItemWidth(LogSelectionWidth);
			ImGui::SameLine(ComboBoxRightAlignment);
			ImGui::Combo("##FilterLogLevel",
				&FilterLogLevel,
				LogLevels,
				sizeof(LogLevels) / sizeof(LogLevels[0]));
			RebuildFilterWithPreviousStates();

			// Draw main log window
			ImGui::Separator();
			ImGui::BeginChild("LogTextView", ImVec2(0, 0), false,
				ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			ImGuiListClipper ViewClipper;
			ViewClipper.Begin(FilteredView.size());
			while (ViewClipper.Step()) {

				int32_t StylesPushedToStack = 0;
				for (auto ClipperLineNumber = ViewClipper.DisplayStart;
					ClipperLineNumber < ViewClipper.DisplayEnd;
					++ClipperLineNumber) {

					auto& LogMetaDataEntry = LogMetaData[FilteredView[ClipperLineNumber]];
					if (LogMetaDataEntry.LogLevel == spdlog::level::n_levels)
						ImGui::Indent();

					for (auto i = 0; i < LogMetaDataEntry.FormattedStringRanges.size(); ++i) {

						switch (LogMetaDataEntry.FormattedStringRanges[i].FormatTag) {
						case FORMAT_RESET_COLORS:
							ImGui::PopStyleColor(StylesPushedToStack);
							StylesPushedToStack = 0;
							break;

						case COLOR_BLACK:
						case COLOR_RED:
						case COLOR_GREEN:
						case COLOR_YELLOW:
						case COLOR_BLUE:
						case COLOR_MAGENTA:
						case COLOR_CYAN:
						case COLOR_WHITE: {
							static const ImVec4 BasicColorsToVec[]{
								{ 0.0f, 0.0f, 0.0f, 1 }, // COLOR_BLACK
								{ 0.5f, 0.0f, 0.0f, 1 }, // COLOR_RED
								{ 0.0f, 0.7f, 0.0f, 1 }, // COLOR_GREEN
								{ 0.7f, 0.7f, 0.0f, 1 }, // COLOR_YELLOW
								{ 0.0f, 0.0f, 0.7f, 1 }, // COLOR_BLUE
								{ 0.7f, 0.0f, 0.7f, 1 }, // COLOR_MAGENTA
								{ 0.0f, 0.7f, 0.7f, 1 }, // COLOR_CYAN
								{ 0.7f, 0.7f, 0.7f, 1 }  // COLOR_WHITE
							};
							ImGui::PushStyleColor(ImGuiCol_Text,
								BasicColorsToVec[LogMetaDataEntry.FormattedStringRanges[i].FormatTag - COLOR_BLACK]);
							++StylesPushedToStack;
						} break;

						case COLOR_BRIGHTBLACK:
						case COLOR_BRIGHTRED:
						case COLOR_BRIGHTGREEN:
						case COLOR_BRIGHTYELLOW:
						case COLOR_BRIGHTBLUE:
						case COLOR_BRIGHTMAGENTA:
						case COLOR_BRIGHTCYAN:
						case COLOR_BRIGHTWHITE: {
							static const ImVec4 BrightColorsToVec[]{
								{ 0, 0, 0, 1 }, // COLOR_BRIGHTBLACK
								{ 1, 0, 0, 1 }, // COLOR_BRIGHTRED
								{ 0, 1, 0, 1 }, // COLOR_BRIGHTGREEN
								{ 1, 1, 0, 1 }, // COLOR_BRIGHTYELLOW
								{ 0, 0, 1, 1 }, // COLOR_BRIGHTBLUE
								{ 1, 0, 1, 1 }, // COLOR_BRIGHTMAGENTA
								{ 0, 1, 1, 1 }, // COLOR_BRIGHTCYAN
								{ 1, 1, 1, 1 }  // COLOR_BRIGHTWHITE
							};
							ImGui::PushStyleColor(ImGuiCol_Text,
								BrightColorsToVec[LogMetaDataEntry.FormattedStringRanges[i].FormatTag - COLOR_BRIGHTBLACK]);
							++StylesPushedToStack;
						}}

						auto FormatRangeBegin = LoggedContent.begin() +
							LogMetaDataEntry.BeginIndex +
						LogMetaDataEntry.FormattedStringRanges[i].SubStringBegin;
						auto FormatRangeEnd = LoggedContent.begin() +
							LogMetaDataEntry.BeginIndex +
							LogMetaDataEntry.FormattedStringRanges[i].SubStringEnd;
						ImGui::TextUnformatted(FormatRangeBegin, FormatRangeEnd);
						if (LogMetaDataEntry.FormattedStringRanges.size() - (i + 1))
							ImGui::SameLine();
					}

					if (LogMetaDataEntry.LogLevel == spdlog::level::n_levels)
						ImGui::Unindent();
				}
				ImGui::PopStyleColor(StylesPushedToStack);
			}
			ViewClipper.End();
			ImGui::PopStyleVar();

			if (EnableAutoScrolling &&
				ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndChild();
		}

		ImGui::End();
	}

	void ClearLogBuffers(
		IN bool DisableLock = false
	) {
		TRACE_FUNCTION_PROTO;

		if (!DisableLock)
			sink_t::mutex_.lock();

		LoggedContent.clear();
		LogMetaData.clear();
		NumberOfLogEntries = 0;
		IndicesInBytes = 0;
		
		if (!DisableLock)
			sink_t::mutex_.unlock();
	}

protected:

	// Writing version 2, this will accept ansi escape sequences for colors
	void sink_it_(
		IN const spdlog::details::log_msg& LogMessage
	) noexcept {
		// This is all protected by the base sink under a mutex
		TRACE_FUNCTION_PROTO;
		++NumberOfLogEntries;

		// Format the logged message and push it into the text buffer
		spdlog::memory_buf_t FormattedBuffer;
		sink_t::formatter_->format(
			LogMessage, FormattedBuffer);
		std::string FormattedText = fmt::to_string(FormattedBuffer);
		
		// Process string by converting the color range passed to an escape sequence first,
		// yes may not be the nicest way of doing this but its way easier to process later.
		const char* ColorToEscapeSequence[]{
			ESC_BRIGHTMAGENTA,
			ESC_CYAN,
			ESC_BRIGHTGREEN,
			ESC_BRIGHTYELLOW,
			ESC_BRIGHTRED,
			ESC_RED
		};
		FormattedText.insert(LogMessage.color_range_start,
			ColorToEscapeSequence[LogMessage.level]);
		FormattedText.insert(LogMessage.color_range_end + 5,
			"\x1b[0m");
		bool FilterPassing = LogMessage.level >= FilterLogLevel;
		FilterPassing &= FilterTextMatch.PassFilter(FormattedText.c_str(),
			FormattedText.c_str() + FormattedText.size());

		// Parse formatted logged string for ansi escape sequences
		auto OldTextBufferSize = LoggedContent.size();
		SinkLineContent MessageData2 {
			LogMessage.level,
			OldTextBufferSize
		};
		AnsiEscapeSequenceTag LastSequenceTagSinceBegin = FORMAT_RESET_COLORS;
		
		// Prematurely filter out immediately starting non default formats,
		// and then enter the main processing loop
		switch (FormattedText[0]) {
		case '\x1b':
		case '\n':
			break;
		default: {

			SinkLineContent::ColorDataRanges FormatPush{
				0, 0, LastSequenceTagSinceBegin
			};
			MessageData2.FormattedStringRanges.push_back(FormatPush);
		}}
		for (auto i = 0; i < FormattedText.size(); ++i)
			switch (FormattedText[i]) {
			case '\n': {

				// Handle new line bullshit, spdlog will terminate any logged message witha  new line
				// we can also assume this may not be the last line, so we continue the previous sequence into
				// the next logically text line if necessary and reconfigure pushstate
				if (MessageData2.FormattedStringRanges.size())
					MessageData2.FormattedStringRanges.back().SubStringEnd = i;
				if (FilterPassing)
					FilteredView.push_back(LogMetaData.size());
				LogMetaData.push_back(MessageData2);

				IndicesInBytes += MessageData2.FormattedStringRanges.size() *
					sizeof(SinkLineContent::ColorDataRanges) +
					sizeof(SinkLineContent);
				MessageData2.LogLevel = spdlog::level::n_levels;
				MessageData2.BeginIndex = OldTextBufferSize + i + 1;
				MessageData2.FormattedStringRanges.clear();

				// Continue previous escape sequences pushed in the previous line
				SinkLineContent::ColorDataRanges FormatPush{
					i + 1, 0, LastSequenceTagSinceBegin
				};
				MessageData2.FormattedStringRanges.push_back(FormatPush);
			} break;
			case '\x1b': {

				// Handle ansi escape sequence, convert textual to operand
				if (FormattedText[i + 1] != '[')
					throw std::runtime_error("Invalid ansi escape sequence passed");

				size_t PositionProcessed = 0;
				auto EscapeSequenceCode = static_cast<AnsiEscapeSequenceTag>(
					std::stoi(&FormattedText[i + 2],
						&PositionProcessed)); // this may throw, in which case we let it pass down

				if (FormattedText[i + 2 + PositionProcessed] != 'm')
					throw std::runtime_error("Invalid ansi escape sequence operand was passed");
				++PositionProcessed;
				LastSequenceTagSinceBegin = EscapeSequenceCode;

				SinkLineContent::ColorDataRanges FormatPush{
					i, 0, EscapeSequenceCode
				};
				if (MessageData2.FormattedStringRanges.size())
					MessageData2.FormattedStringRanges.back().SubStringEnd = FormatPush.SubStringBegin;
				MessageData2.FormattedStringRanges.push_back(FormatPush);

				// Now the escape code has to be removed from the string,
				// the iterator has to be kept stable, otherwise the next round could skip a char
				FormattedText.erase(FormattedText.begin() + i--,
					FormattedText.begin() + (i + 2 + PositionProcessed));
			}}

		// Append processed string to log text buffer
		LoggedContent.append(FormattedText.c_str(),
			FormattedText.c_str() + FormattedText.size());
	}
	void flush_() { TRACE_FUNCTION_PROTO; }

private:

	// TODO: Need to implement double buffering for the filter
	void RebuildFilterWithPreviousStates() {
		TRACE_FUNCTION_PROTO;

		int32_t RebuildType = PreviousFilterLevel != FilterLogLevel;
		RebuildType |= memcmp(PreviousFilterText,
			FilterTextMatch.InputBuf,
			sizeof(PreviousFilterText));

		if (RebuildType) {

			// Filter was completely changed, have to rebuild array (very expensive,
			//	this may result in short freezes or stuttering on really large logs,
			//	one way to solve this could be to defer the calculation of the filter view
			//	to a thread pool and use a double buffering mechanism)

			auto NewLinePasses = false;
			FilteredView.clear();
			for (auto i = 0; i < LogMetaData.size(); ++i) {

				if (LogMetaData[i].LogLevel == spdlog::level::n_levels) {

					if (NewLinePasses)
						FilteredView.push_back(i);
					continue;
				}

				if (LogMetaData[i].LogLevel < FilterLogLevel) {
					NewLinePasses = false; continue;
				}

				auto LineBegin = LoggedContent.begin() +
					LogMetaData[i].BeginIndex +
					LogMetaData[i].FormattedStringRanges.front().SubStringBegin;
				auto LineEnd = LoggedContent.begin() +
					LogMetaData[i].BeginIndex +
					LogMetaData[i].FormattedStringRanges.back().SubStringEnd;
				if (!FilterTextMatch.PassFilter(LineBegin, LineEnd)) {
					NewLinePasses = false; continue;
				}

				NewLinePasses = true;
				FilteredView.push_back(i);
			}
		}
		
		PreviousFilterLevel = FilterLogLevel;
		memcpy(PreviousFilterText,
			FilterTextMatch.InputBuf,
			sizeof(PreviousFilterText));
	}

	// Using faster more efficient replacements of stl types for rendering
	ImGuiTextBuffer            LoggedContent;
	std::vector<SinkLineContent> LogMetaData; // Cannot use ImVetctor here as the type is not trivially copyable 
											  // the type has to be moved into, slightly more expensive
											  // but overall totally fine, at least no weird hacks
	ImGuiTextFilter   FilterTextMatch;
	ImVector<int32_t> FilteredView;           // A filtered array of indexes into the LogMetaData vector
											  // this view is calculated once any filter changes
	int32_t  FilterLogLevel = spdlog::level::trace;
	uint32_t NumberOfLogEntries = 0;          // Counts the number of entries logged
	uint32_t IndicesInBytes = 0;              // Keeps track of the amount of memory allocated by indices
	bool     EnableAutoScrolling = true;

	// Previous Frame's filterdata, if any of these change to the new values the filter has to be recalculated
	int32_t PreviousFilterLevel = spdlog::level::trace;
	decltype(ImGuiTextFilter::InputBuf)
		PreviousFilterText{};
};




