module;

#include "sof/sof.h"

export module sof.control.tasks;
import sof.control.gui; // quick hack to get the the IOpenFileDialog object

import sof.llir;
import sof.image.load;
import sof.image.edit;
import sof.amd64.disasm;
import sof.amd64.lift;
import sof.help.symbol;

// Special public name for the image loader and image editor (etc.) tool
export using ImageHelp = ImageLoader<ImageEditor>;

// This defines a base interface type that every async queue object must inherit from
export class IVisualWorkObject {
	struct ThrowAbortTag : public SingularityException {
		ThrowAbortTag()
			: SingularityException("Aborted object on call by user dispatch",
				SingularityException::STATUS_ABORTED_OBJECT_IN_CALL) {
			TRACE_FUNCTION_PROTO;
		}
	};
	friend class VisualSingularityApp;

public:
	IVisualWorkObject() = default;


	// Another msvc bug, pure virtual destructors dont work when being exported from modules
	// https://developercommunity.visualstudio.com/t/c-20-module-problem-on-pure-virtual-and-abstract-k/1441612
	// no real workaround is available, so instead of abstarct it will just be virtual ig
	// virtual ~IVisualWorkObject() = 0;
	virtual ~IVisualWorkObject() {
		TRACE_FUNCTION_PROTO;
	}
	IVisualWorkObject(IN const IVisualWorkObject&) = delete;
	IVisualWorkObject& operator=(IN const IVisualWorkObject&) = delete;

	using enum_t = int32_t;
	enum WorkStateBase : enum_t {        // All abstracts of this interface must implement these state enums,
										 // they are used to determine the current state of the abstract object.
										 // Failing to implement them will result in undfined behaviour,
										 // the implementor can freely add other object states later, these are required.
		OPT STATE_ABORTING_CALL = -2000, // The async call was schedule with an abort, the abort is in progress
		OPT STATE_ABORTED_FULLY,         // The call finished its abort, the object was effectively "handled" by dispatch
			STATE_CALL_ERROR,            // An error occurred during a async call, the object may communicate more info
										 // about the error in a object specific way.
										 // All values after here indicate implementation specific errors

			STATE_CALL_SCHEDULED = 0,    // The object is currently scheduled and waiting to be serviced
										 // All values after here indicate ongoing handling to dispatch

			STATE_CALL_FINISHED = 2000,  // The async object call was fully handled by dispatch
										 // All values after here indicate successful handling to dispatch
	};
	virtual enum_t GetWorkObjectState() const = 0;

	void DispatchAbortCheck() { // Dispatches an abort exception in case the object should be trashed
								// This will unwind back to the dispatcher that will then catch
								// said exception, and undo the objects work returning it in a aborted state
		TRACE_FUNCTION_PROTO;
		if (GetWorkObjectState() == STATE_ABORTING_CALL)
			throw ThrowAbortTag();
	}
};
// IVisualWorkObject::~IVisualWorkObject() {
// 	TRACE_FUNCTION_PROTO;
// }

#pragma region VisualSingularity task objects
export class VisualOpenFileObject
	: public IVisualWorkObject,
	  public OpenFileDialogConfig {
	friend class VisualSingularityApp;
public:
	enum CurrentOpenState {
		STATE_COM_ERROR = IVisualWorkObject::STATE_CALL_ERROR,
		STATE_SCHEDULED = IVisualWorkObject::STATE_CALL_SCHEDULED,
		STATE_OPENING,
		STATE_DIALOG_DONE = IVisualWorkObject::STATE_CALL_FINISHED,
		STATE_CANCELED
	};

	VisualOpenFileObject(
		IN const OpenFileDialogConfig& FileDialogConfig
	) 
		: OpenFileDialogConfig(FileDialogConfig) {
		TRACE_FUNCTION_PROTO;
	}

	IVisualWorkObject::enum_t
	GetWorkObjectState() const override {
		TRACE_FUNCTION_PROTO; return OpenDlgState;
	}
	std::string_view GetResultString() const {
		TRACE_FUNCTION_PROTO; 
		static_cast<void>(OpenFileResult.c_str()); // ensure null terminator
		return OpenFileResult;
	}

private:
	void DoOpenFileProcessObject() {
		TRACE_FUNCTION_PROTO;

		// Process IFileOpenDialog async in object
		OpenDlgState = VisualOpenFileObject::STATE_OPENING;
		OpenFileResult = ComOpenFileDialogWithConfig(
			*static_cast<OpenFileDialogConfig*>(this));
		OpenFileResult.empty() ?
			SPDLOG_WARN("The user selected to cancel the file selection prompt") :
			SPDLOG_INFO("User selected "ESC_BRIGHTYELLOW"\"{}\""ESC_RESET", for image",
				OpenFileResult);

		// Set the State of the object, no lock required, state is atomic
		OpenDlgState = OpenFileResult.empty() ? VisualOpenFileObject::STATE_CANCELED
			: VisualOpenFileObject::STATE_DIALOG_DONE;
	}

	// The result string is not protected by any lock, 
	// it assumes that it will be set by the worker 
	// and will be only be read by multiple threads if at all
	std::string OpenFileResult;
	std::atomic<CurrentOpenState> OpenDlgState;
};

export class VisualLoadImageObject
	: public IVisualWorkObject {
	friend class VisualSingularityApp;
	friend class VisualSOPassObject;
public:
	enum CurrentLoadState {
		STATE_ABORTING = IVisualWorkObject::STATE_ABORTING_CALL,
		STATE_ABORTED = IVisualWorkObject::STATE_ABORTED_FULLY,
		STATE_ERROR = IVisualWorkObject::STATE_CALL_ERROR,
		STATE_SCHEDULED = IVisualWorkObject::STATE_CALL_SCHEDULED,
		STATE_LOADING_MEMORY,
		STATE_LAODING_SYMBOLS,
		STATE_RELOCATING,
		STATE_UNLOADING,
		STATE_FULLY_LOADED = IVisualWorkObject::STATE_CALL_FINISHED,
		STATE_FULLY_UNLOADED
	};

	VisualLoadImageObject(
		IN  filesystem::path     ImageFileName,
		IN              bool     ForceDisableSymbols = false,
		IN  filesystem::path     PdbFileName = {},
		OPT IImageLoaderTracker* ExternTracker = nullptr)
		: ImageFile(ImageFileName),
		  PdbFile(PdbFileName),
		  InfoTracker(ExternTracker),
		  DisableSymbolServer(ForceDisableSymbols) {
		TRACE_FUNCTION_PROTO;
	}
	
	IVisualWorkObject::enum_t
	GetWorkObjectState() const override {
		TRACE_FUNCTION_PROTO; return ImageLoadState;
	}
	uint32_t GetPolyXNumber() const {
		TRACE_FUNCTION_PROTO; return NumberOfPolyX;
	}
	CommonExceptionType* GetExceptionReport() const {
		TRACE_FUNCTION_PROTO; return ExitExceptionCopy.get();
	}

	void AbortImageLoadAsync() const { // This schedules an abort by overriding the state of this object
									   // The abort is handled by throwing an abort exception from the tracker interface
		TRACE_FUNCTION_PROTO; 
		ImageLoadState = STATE_ABORTING;
		SPDLOG_WARN("Scheduled abort on remote thread, cleaning up asap");
	}

private:
	void DoLoadFileAsyncProcessing() {
		TRACE_FUNCTION_PROTO;

		// Try to load the image, the path is assumed to be checked before passing,
		// ImageLoader will "check" it regardless, as in not being able to open it.
		try {
			// Check if we can try to load symbols
			if (!DisableSymbolServer) {

				// Decide how to load the pdb dependent on how the path is set
				ImageLoadState = VisualLoadImageObject::STATE_LAODING_SYMBOLS;
				SymbolServer = std::make_unique<SymbolHelp>(
					PdbFile.empty() ? ImageFile : PdbFile,
					!PdbFile.empty());
			}

			// Load the image, this has to be done after loading the symbols
			ImageLoadState = VisualLoadImageObject::STATE_LOADING_MEMORY;
			TargetImage = std::make_unique<ImageHelp>(ImageFile.string());
			TargetImage->MapImageIntoMemory(InfoTracker);

			// Register image address for symbol server
			if (!DisableSymbolServer)
				SymbolServer->InstallPdbFileMappingVirtualAddress(
					TargetImage.get());
			TargetImage->RejectFileCloseHandles();
			NumberOfPolyX = TargetImage->ApproximateNumberOfRelocationsInImage();

			// Relocate Image in memory confirm changes and notify of finished state
			ImageLoadState = VisualLoadImageObject::STATE_RELOCATING;
			TargetImage->RelocateImageToMappedOrOverrideBase(InfoTracker);
			ImageLoadState = VisualLoadImageObject::STATE_FULLY_LOADED;
		}
		catch (const CommonExceptionType& ExceptionInforamtion) {

			// Do internal cleanup and complete this request
			ExitExceptionCopy = std::make_unique<CommonExceptionType>(ExceptionInforamtion);
			SymbolServer.release();
			TargetImage.release();
			switch (ExceptionInforamtion.ExceptionTag) {
			case CommonExceptionType::EXCEPTION_IMAGE_HELP:
				SPDLOG_ERROR("The image load failed due to [{}]: \"{}\"",
					ExceptionInforamtion.StatusCode,
					ExceptionInforamtion.ExceptionText);
				ImageLoadState = VisualLoadImageObject::STATE_ERROR;
				break;

			case CommonExceptionType::EXCEPTION_COMOLE_EXP:
				SPDLOG_ERROR("MS DIA failed to load the pdb for "ESC_BRIGHTBLUE"\"{}\"",
					ImageFile.string());
				ImageLoadState = VisualLoadImageObject::STATE_ERROR;
				break;

			case CommonExceptionType::EXCEPTION_VISUAL_APP:
				SPDLOG_WARN("The ongoing Load " ESC_BRIGHTRED"{}" ESC_RESET" was aborted by callback",
					static_cast<void*>(this));
				ImageLoadState = VisualLoadImageObject::STATE_ABORTED;
				break;

			default:
				__fastfail(static_cast<uint32_t>(-247628)); // TODO: Temporary will be fixed soon
			}
		}
	}

	std::unique_ptr<CommonExceptionType>  ExitExceptionCopy; // under some states this maybe valid and contains exact information
															 // as to why the load could have failed
	mutable 
	std::atomic<CurrentLoadState> ImageLoadState; // Describes the current load state
	std::atomic_uint32_t          NumberOfPolyX;  // STATE_LOADING_MEMORY: NumberOfSections in image
												  // STATE_RELOCATING:     NumberOfRelocations in image

	std::unique_ptr<ImageHelp>  TargetImage;  // Containers for the image and symbols, if available
	std::unique_ptr<SymbolHelp> SymbolServer;

	IImageLoaderTracker* InfoTracker;         // Datatracker reference interface pointer, this is hosted by another instance
	filesystem::path     ImageFile;           // Image and pdb file names to load (pdb only if explicit)
	filesystem::path     PdbFile;
				bool     DisableSymbolServer; // Specifies whether or not the loader should not load the pdb if regardeless of availability
};

export class VisualSOPassObject :
	public IVisualWorkObject {
	friend class VisualSingularityApp;
public:
	class FunctionTableEntry {
		byte_t* FunctionAddress;
		size_t  FunctionSize;

		std::unique_ptr<ControlFlowGraph> FunctionCfg;

		std::mutex FunctionLock;
	};


	enum CurrentSOMPassState {
		STATE_ABORTING = IVisualWorkObject::STATE_ABORTING_CALL,
		STATE_ABORTED = IVisualWorkObject::STATE_ABORTED_FULLY,
		STATE_ERROR = IVisualWorkObject::STATE_CALL_ERROR,
		STATE_SCHEDULED = IVisualWorkObject::STATE_CALL_SCHEDULED,
		
		STATE_PROCESSING,

		STATE_ALL_PASSES_APLIED_DONE = IVisualWorkObject::STATE_CALL_FINISHED,
	};
	enum CodeDiscoveryMode {
		DISCOVER_USE_HIGHEST_AVAILABLE = 0,
		DISCOVER_ACTIVE_SET_SEARCH,
		DISCOVER_USE_SEH_EXCEPTION_TABLE,
		DISCOVER_USE_PDB_FUNCTION_SYMBOLS,
	};

	VisualSOPassObject(
		IN IDisassemblerTracker* ExternDisassemblerTracker
	) 
		: DisassmeblerTracker(ExternDisassemblerTracker) {
		TRACE_FUNCTION_PROTO;
	}

	IVisualWorkObject::enum_t
	GetWorkObjectState() const override {
		TRACE_FUNCTION_PROTO; return ProcessingState;
	}


	void DoImageProcessingRoutine() {
		TRACE_FUNCTION_PROTO;

		// Generate Cfgs....
		ProcessingState = STATE_PROCESSING;

		xed_state_t XedConfig;
		xed_state_init2(&XedConfig,
			XED_MACHINE_MODE_LONG_64,
			XED_ADDRESS_WIDTH_64b);
		CfgGenerator XCfgGenerator(*LoaderObject->TargetImage,
			XedConfig);

		// Enumerate all function entries here
		auto RuntimeFunctionsView = LoaderObject->TargetImage->GetRuntimeFunctionTable();
		NumberOfFunctions = RuntimeFunctionsView.size();
		for (const RUNTIME_FUNCTION& RuntimeFunction : RuntimeFunctionsView) {

			// Generate Cfg from function

			// Tests
			// auto FunctionAddres = SymbolServer.FindFunctionFrameByName("StubTestFunction2");
			// auto GraphForFunction = ControlFlowGraphGenerator.GenerateCfgFromFunction2(
			// 	FunctionAddres);
			// auto InvalidXRefs = GraphForFunction.ValidateCfgOverCrossReferences();
			// __debugbreak();

			try {

				auto GraphForRuntimeFunction = XCfgGenerator.GenerateCfgFromFunction2(
					FunctionAddress(
						RuntimeFunction.BeginAddress + LoaderObject->TargetImage->GetImageFileMapping(),
						RuntimeFunction.EndAddress - RuntimeFunction.BeginAddress),
					*DisassmeblerTracker);
				auto Failures = GraphForRuntimeFunction.ValidateCfgOverCrossReferences();
				if (Failures) {

					__debugbreak();

				}

				++FramesProcessed;
			}
			catch (const SingularityException& GraphException) {

				SPDLOG_ERROR("Graph generation failed failed with [{}] : \"{}\", skipping frame",
					GraphException.StatusCode,
					GraphException.ExceptionText);
				++FramesProcessed;

				//if (IsDebuggerPresent())
				// 	__debugbreak();
			}


		}

		ProcessingState = STATE_ALL_PASSES_APLIED_DONE;

	}

	std::atomic_uint32_t NumberOfFunctions = 0;
	std::atomic_uint32_t FramesProcessed = 0;


	// Oneshot initialization variables, set these before scheduling the object
	CodeDiscoveryMode Mode = DISCOVER_USE_HIGHEST_AVAILABLE;
	const VisualLoadImageObject* LoaderObject = nullptr;

private:
	mutable
	std::atomic<CurrentSOMPassState> ProcessingState;
	
	IDisassemblerTracker* DisassmeblerTracker;

	// Function table data and lookup
	std::vector<FunctionTableEntry>      FunctionTableData;
	std::unordered_map<byte_t*, int32_t> FunctionTableIndices;
};
#pragma endregion

#pragma region VisualSingularity controller objects
export class VisualSingularityApp {
public:
	enum ControlRequest {
		CWI_OPEN_FILE,
		CWI_LOAD_FILE,
		CWI_DO_PROCESSING_FINAL,
	};
	struct QueueWorkItem {
		VisualSingularityApp*              OwnerController;
		ControlRequest                     ControlCommand;
		std::unique_ptr<IVisualWorkObject> WorkingObject;
	};

	VisualSingularityApp(
		IN uint32_t NumberOfThreads = 0
	) {
		UNREFERENCED_PARAMETER(NumberOfThreads);
		TRACE_FUNCTION_PROTO;
		InitializeThreadpoolEnvironment(&ThreadPoolEnvironment);
		SPDLOG_INFO("Initialized thread pool for async work");
	}


	const VisualOpenFileObject* QueueOpenFileRequestDialogWithTag( // Open a IFileOpenDialog interface,
																   // the result can later be retrieved using the returned token
		IN const OpenFileDialogConfig& DialogConfig                // Move in the Dialog Object
	) {
		TRACE_FUNCTION_PROTO;

		const std::lock_guard Lock(ThreadPoolLock);
		auto& WorkItemEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ControlCommand = CWI_OPEN_FILE,
			.WorkingObject = std::make_unique<VisualOpenFileObject>(
				std::move(DialogConfig)) });
		QueueEnlistWorkItem(&WorkItemEntry);
		return static_cast<VisualOpenFileObject*>(WorkItemEntry.WorkingObject.get());
	}
	const VisualLoadImageObject* QueueLoadImageRequestWithTracker2(    // Schedules a load operation on the working threadpool
																	   // and returns the schedules request by pointer,
																	   // the returned object contains up to date information about the loading procedure
		IN std::unique_ptr<VisualLoadImageObject> LoadImageInformation // The information used to try to schedule the image
	) {
		TRACE_FUNCTION_PROTO;

		const std::lock_guard Lock(ThreadPoolLock);
		auto& WorkItemEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ControlCommand = CWI_LOAD_FILE,
			.WorkingObject = std::move(LoadImageInformation) });
		QueueEnlistWorkItem(&WorkItemEntry);
		return static_cast<VisualLoadImageObject*>(WorkItemEntry.WorkingObject.get());
	}
	const VisualSOPassObject* QueueMainPassProcessObject(
		IN std::unique_ptr<VisualSOPassObject> SOBPassInformation
	) {
		TRACE_FUNCTION_PROTO;

		const std::lock_guard Lock(ThreadPoolLock);
		auto& WorkItemEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ControlCommand = CWI_DO_PROCESSING_FINAL,
			.WorkingObject = std::move(SOBPassInformation) });
		QueueEnlistWorkItem(&WorkItemEntry);
		return static_cast<VisualSOPassObject*>(WorkItemEntry.WorkingObject.get());

	}

	void FreeWorkItemFromPool(
		IN const IVisualWorkObject* WorkItemEntry
	) {
		TRACE_FUNCTION_PROTO;

		// Locate work item in pool, if not found or not completed throw error
		const std::lock_guard Lock(ThreadPoolLock);
		auto DequeEntry = std::find_if(WorkResponseList.begin(),
			WorkResponseList.end(),
			[WorkItemEntry](
				IN const QueueWorkItem& WorkItem
				) -> bool {
					TRACE_FUNCTION_PROTO;

					if (WorkItem.WorkingObject.get() == WorkItemEntry) {

						// Found deque item for object, determine state
						auto WorkState = WorkItem.WorkingObject->GetWorkObjectState();	
						if (WorkState >= IVisualWorkObject::STATE_CALL_SCHEDULED &&
							WorkState < IVisualWorkObject::STATE_CALL_FINISHED)
							throw SingularityException(
								fmt::format("Tried to free in progress object " ESC_BRIGHTRED"{}",
									static_cast<const void*>(WorkItemEntry)),
								SingularityException::STATUS_CANNOT_FREE_IN_PROGRESS);
						return true;
					}
					return false;
			});
		if (DequeEntry == WorkResponseList.end())
			throw SingularityException(
				fmt::format("Tried to free unregistered object " ESC_BRIGHTRED"{}",
					static_cast<const void*>(WorkItemEntry)),
				SingularityException::STATUS_FREED_UNREGISTERED_OBJ);
		WorkResponseList.erase(DequeEntry);
		SPDLOG_INFO("Freed workitem " ESC_BRIGHTRED"{}" ESC_RESET" from working pool",
			static_cast<const void*>(WorkItemEntry));
	}

	

private:
	void QueueEnlistWorkItem(
		IN QueueWorkItem* WorkItem
	) {
		TRACE_FUNCTION_PROTO;

		auto Status = TrySubmitThreadpoolCallback(
			WorkQueueDisptachThread,
			static_cast<void*>(WorkItem),
			&ThreadPoolEnvironment);
		if (!Status) {

			WorkResponseList.pop_back();
			throw SingularityException(
				fmt::format("Failed to enlist " ESC_BRIGHTRED"{}" ESC_RESET" workitem on the threadpool work queue",
					static_cast<void*>(WorkItem)),
				SingularityException::STATUS_FAILED_QUEUE_WORKITEM);
		}
	}

	static void WorkQueueDisptachThread(
		IN    PTP_CALLBACK_INSTANCE CallbackInstance,
		INOUT void*                 UserContext
	) {
		TRACE_FUNCTION_PROTO;

		auto WorkObject = static_cast<QueueWorkItem*>(UserContext);
		switch (WorkObject->ControlCommand) {
		case CWI_OPEN_FILE: {

			// Notify of long running function and run obejct processor, this object type is self processing
			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");
			auto OpenObject = static_cast<VisualOpenFileObject*>(WorkObject->WorkingObject.get());
			OpenObject->DoOpenFileProcessObject();
			break;
		}
		case CWI_LOAD_FILE: {

			// Initialize load and inform threadpool of possibly long running function
			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");
			auto LoadObject = static_cast<VisualLoadImageObject*>(WorkObject->WorkingObject.get());
			LoadObject->ImageLoadState = VisualLoadImageObject::STATE_LOADING_MEMORY;
			LoadObject->DoLoadFileAsyncProcessing();
			break;
		}

		case CWI_DO_PROCESSING_FINAL: {
			
			// This is just temporary as i just need something running
			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");
			auto ProcessingObject = static_cast<VisualSOPassObject*>(WorkObject->WorkingObject.get());
			ProcessingObject->DoImageProcessingRoutine();
			
			
			break;
		}}
	}

	std::deque<QueueWorkItem>   WorkResponseList;
	std::mutex                  ThreadPoolLock;
	TP_CALLBACK_ENVIRON         ThreadPoolEnvironment{};
};
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
