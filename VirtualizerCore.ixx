// Project core, this implements the combination of all other modules into the virtulaizer controller
//

#include "VirtualizerBase.h"
#include <imgui.h>
#include <backends\imgui_impl_glfw.h>
#include <backends\imgui_impl_opengl3.h>
#include <glfw\glfw3.h>
#include <shlobj_core.h>

#include <chrono>
#include <ranges>
#include <span>
#include <variant>
#include <functional>
#include <deque>
#include <mutex>
#include <filesystem>

import ImageHelp;
import DisassemblerEngine;
import SymbolHelp;

using namespace std::literals::chrono_literals;
namespace chrono = std::chrono;
namespace filesystem = std::filesystem;


// This defines a base interface type that every async queue object must inherit from
class IVisualWorkObject {
public:
	virtual int32_t GetCurrentObjectState() const = 0;

};

#pragma region Console mode interface
class ConsoleModifier {
public:
	ConsoleModifier(
		IN HANDLE ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE)
	) 
		: ConsoleHandle(ConsoleHandle) {
		TRACE_FUNCTION_PROTO;

		// Save previous console mode and switch to nicer mode
		GetConsoleMode(ConsoleHandle, &PreviousConsoleMode);

		auto NewConsoleMode = PreviousConsoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(ConsoleHandle, NewConsoleMode);
	}

	~ConsoleModifier() {
		TRACE_FUNCTION_PROTO;

		SetConsoleMode(ConsoleHandle, PreviousConsoleMode);
	}

	HANDLE ConsoleHandle;
	DWORD  PreviousConsoleMode;
};
#pragma endregion

#pragma region Visual mode interface

class VisualAppException
	: public CommonExceptionType {
public:
	enum ExceptionCode : UnderlyingType {
		STATUS_FAILED_QUEUE_WORKITEM = -2000,
		STATUS_CANNOT_FREE_IN_PROGRESS,
		STATUS_FREED_UNREGISTERED_OBJ,

	};

	VisualAppException(
		IN const std::string_view& ExceptionText,
		IN       ExceptionCode     StatusCode
	)
		: CommonExceptionType(ExceptionText,
			StatusCode,
			CommonExceptionType::EXCEPTION_VISUAL_APP) {
		TRACE_FUNCTION_PROTO;
	}
};


#pragma region COM OpenFileDialog
// Highly inefficient and horribly ugly shitty code, but its user driven anyways...
// TODO: Exchange some stuff here for std::filesystem
struct OpenFileDialogConfig {
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

std::string ComOpenFileDialogWithConfig(
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
	
		ComResult = OpenFileDialog->SetFileTypes(Filters.size(),
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

class VisualOpenFileObject 
	: public IVisualWorkObject,
	  public OpenFileDialogConfig {
	friend class VisualSingularityApp;
public:
	enum CurrentOpenState {
		STATE_CANCELED = -2000,
		STATE_ERROR,

		STATE_SCHEDULED = 0,
		STATE_OPENING,
		STATE_DIALOG_DONE,
	};

	VisualOpenFileObject(
		IN const OpenFileDialogConfig& FileDialogConfig
	) 
		: OpenFileDialogConfig(FileDialogConfig) {
		TRACE_FUNCTION_PROTO;
	}

	VisualOpenFileObject(IN const VisualOpenFileObject&) = delete;
	VisualOpenFileObject& operator=(IN const VisualOpenFileObject&) = delete;

	std::underlying_type_t<CurrentOpenState>
	GetCurrentObjectState() const override {
		TRACE_FUNCTION_PROTO; return OpenDlgState;
	}
	std::string_view GetResultString() const {
		TRACE_FUNCTION_PROTO; 
		OpenFileResult.c_str(); // ensure null terminator
		return OpenFileResult;
	}

private:
	// The result string is not protected by any lock, 
	// it assumes that it will be set by the worker 
	// and will be only be read by multiple threads if at all
	std::string OpenFileResult;
	std::atomic<CurrentOpenState> OpenDlgState;
};

class VisualLoadImageObject 
	: public IVisualWorkObject {
	friend class VisualSingularityApp;
public:
	enum CurrentLoadState {
		STATE_ABORTED = -2000,
		STATE_ERROR,

		STATE_SCHEDULED = 0,
		STATE_LOADING_MEMORY,
		STATE_LAODING_SYMBOLS,
		STATE_RELOCATING,
		STATE_FULLY_LOADED,
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
	
	// Disallow copying of this object, the object must be moved,
	// as this has strict ownership semantics due to multi threading badness
	VisualLoadImageObject(IN const VisualLoadImageObject&) = delete;
	VisualLoadImageObject& operator=(IN const VisualLoadImageObject&) = delete;
	VisualLoadImageObject(IN VisualLoadImageObject&& Other) {
		TRACE_FUNCTION_PROTO; *this = std::move(Other);
	}
	VisualLoadImageObject& operator=(IN VisualLoadImageObject&& Other) {
		TRACE_FUNCTION_PROTO;
		ExitExceptionCopy = std::move(Other.ExitExceptionCopy);
		ImageLoadState.store(Other.ImageLoadState);
		NumberOfPolyX.store(Other.NumberOfPolyX);
		InfoTracker = Other.InfoTracker;
		ImageFile = std::move(Other.ImageFile);
		PdbFile = std::move(Other.PdbFile);
		DisableSymbolServer = Other.DisableSymbolServer;
		return *this;
	}


	std::underlying_type_t<CurrentLoadState>
	GetCurrentObjectState() const override {
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
		ImageLoadState = STATE_ABORTED;
		SPDLOG_WARN("Scheduled abort on remote thread, cleaning up asap");
	}

private:
	std::unique_ptr<CommonExceptionType>  ExitExceptionCopy; // under some states this maybe valid and contains exact information
															 // as to why the load could have failed
	mutable std::atomic<CurrentLoadState> ImageLoadState;    // Describes the current load state
	std::atomic<uint32_t>                 NumberOfPolyX;     // STATE_LOADING_MEMORY: NumberOfSections in image
															 // STATE_RELOCATING:     NumberOfRelocations in image

	IImageLoaderTracker* InfoTracker;
	filesystem::path     ImageFile;
	filesystem::path     PdbFile;
				bool     DisableSymbolServer;
};




class VisualSingularityApp {
public:
	enum ControlRequest {
		CWI_OPEN_FILE,
		CWI_LOAD_FILE,
		CWI_DO_PROCESSING_FINAL
	};
	struct QueueWorkItem {
		VisualSingularityApp* OwnerController;
		token_t               ObjectToken;

		ControlRequest ControlCommand;
		bool           WorkItemhandled;
		uint32_t       StatusResult;

		IVisualWorkObject* WorkingObject;
	};

	VisualSingularityApp(
		IN uint32_t NumberOfThreads
	) {
		TRACE_FUNCTION_PROTO;
		InitializeThreadpoolEnvironment(&ThreadPoolEnvironment);
		SPDLOG_INFO("Initilialized thread pool for async work");
	}


	const VisualOpenFileObject* QueueOpenFileRequestDialogWithTag( // Open a IFileOpenDialog interface,
																   // the result can later be retrieved using the returned token
		IN const OpenFileDialogConfig& DialogConfig                // Move in the Dialog Object
	) {
		TRACE_FUNCTION_PROTO;

		const std::lock_guard Lock(ThreadPoolLock);
		auto TokenForRequest = GenerateGlobalUniqueTokenId();
		auto& WorkItemEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ObjectToken = TokenForRequest,
			.ControlCommand = CWI_OPEN_FILE,
			.WorkingObject = new VisualOpenFileObject(DialogConfig) });
		QueueEnlistWorkItem(&WorkItemEntry);
		return static_cast<VisualOpenFileObject*>(WorkItemEntry.WorkingObject);
	}
	const VisualLoadImageObject* QueueLoadImageRequestWithTracker( // Schedules a load operation on the working threadpool
																   // and returns the schedules request by pointer,
																   // the returned object contains up to date information about the loading procedure
		IN VisualLoadImageObject&& LoadImageInformation            // The information used to try to schedule the image
	) {
		TRACE_FUNCTION_PROTO;

	
		const std::lock_guard Lock(ThreadPoolLock);
		auto TokenForRequest = GenerateGlobalUniqueTokenId();
		auto& WorkItemEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ObjectToken = TokenForRequest,
			.ControlCommand = CWI_LOAD_FILE,
			.WorkingObject = new VisualLoadImageObject(
				std::move(LoadImageInformation)) });
		QueueEnlistWorkItem(&WorkItemEntry);
		return static_cast<VisualLoadImageObject*>(WorkItemEntry.WorkingObject);
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

					if (WorkItem.WorkingObject == WorkItemEntry) {

						// Found deque item for object
						if (WorkItem.WorkItemhandled)
							return true;

						throw VisualAppException("Tried to free in progress object, illegal",
							VisualAppException::STATUS_CANNOT_FREE_IN_PROGRESS);
					}

					return false;
			});
		if (DequeEntry == WorkResponseList.end())
			throw VisualAppException("Tried to free unregistered object from pool",
				VisualAppException::STATUS_FREED_UNREGISTERED_OBJ);
		WorkResponseList.erase(DequeEntry);
		SPDLOG_INFO("Freed workitem from working pool");
	}

	static void DispatchAbortCheck() { // TODO: implement
		TRACE_FUNCTION_PROTO;
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
			throw VisualAppException("Failed to enlist a work queue item on the thread pool",
				VisualAppException::STATUS_FAILED_QUEUE_WORKITEM);
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

			auto OpenObject = static_cast<VisualOpenFileObject*>(WorkObject->WorkingObject);
			OpenObject->OpenDlgState = VisualOpenFileObject::STATE_OPENING;

			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");
			OpenObject->OpenFileResult = ComOpenFileDialogWithConfig(
				*static_cast<OpenFileDialogConfig*>(OpenObject));

			if (OpenObject->OpenFileResult.empty()) {

				OpenObject->OpenDlgState = VisualOpenFileObject::STATE_CANCELED;
				SPDLOG_WARN("The user selected to cancel the file selection prompt");
				break;
			}

			OpenObject->OpenDlgState = VisualOpenFileObject::STATE_DIALOG_DONE;
			SPDLOG_INFO("User selected \"{}\", for image",
				OpenObject->OpenFileResult);
			break;
		}
		case CWI_LOAD_FILE: {

			auto LoadObject = static_cast<VisualLoadImageObject*>(WorkObject->WorkingObject);
			LoadObject->ImageLoadState = VisualLoadImageObject::STATE_LOADING_MEMORY;

			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");

			// Try to load the image, the path is assumed to be checked before passing,
			// ImageLoader will "check" it regardless, as in not being able to open it.
			auto& TargetImage = WorkObject->OwnerController->TargetImage;
			auto& SymbolServer = WorkObject->OwnerController->SymbolServer;
			try {
				TargetImage = std::make_unique<ImageHelp>(LoadObject->ImageFile.string());
				TargetImage->MapImageIntoMemory();

				// Check if we can try to load symbols
				if (!LoadObject->DisableSymbolServer) {

					// Decide how to load the pdb dependent on how the path is set
					LoadObject->ImageLoadState = VisualLoadImageObject::STATE_LAODING_SYMBOLS;
					TargetImage->RejectFileCloseHandles();
					if (!LoadObject->PdbFile.empty()) {

						SymbolServer = std::make_unique<SymbolHelp>(
							LoadObject->PdbFile.string());
						SymbolServer->InstallPdbFileMappingVirtualAddress(
							TargetImage->GetImageFileMapping());
					}
					else
						SymbolServer = std::make_unique<SymbolHelp>(
							*static_cast<IImageLoaderHelp*>(TargetImage.get()));
				}

				// Relocate Image in memory
				LoadObject->ImageLoadState = VisualLoadImageObject::STATE_RELOCATING;
				LoadObject->InfoTracker->SetApproximatedRelocCount(
					TargetImage->ApproximateNumberOfRelocationsInImage());
				TargetImage->RelocateImageToMappedOrOverrideBase();

				LoadObject->ImageLoadState = VisualLoadImageObject::STATE_FULLY_LOADED;
			}
			catch (const CommonExceptionType& ExceptionInforamtion) {

				switch (ExceptionInforamtion.ExceptionTag) {
				case CommonExceptionType::EXCEPTION_IMAGE_HELP:
					LoadObject->ImageLoadState = VisualLoadImageObject::STATE_ERROR;
					SPDLOG_ERROR("The image load failed due to [{}]: \"{}\"",
						ExceptionInforamtion.StatusCode,
						ExceptionInforamtion.ExceptionText);
					break;
					
				case CommonExceptionType::EXCEPTION_COMOLE_EXP:
					LoadObject->ImageLoadState = VisualLoadImageObject::STATE_ERROR;
					break;

				case CommonExceptionType::EXCEPTION_VISUAL_APP:
					LoadObject->ImageLoadState = VisualLoadImageObject::STATE_ABORTED;
					SPDLOG_WARN("the current ongiung op was aborted by callback");
					break;

				default:
					__fastfail(-247628); // TODO: Temporary will be fixed soon
				}

				// Do cleanup
				SymbolServer.release();
				TargetImage.release();
			}
			break;
		}

		case CWI_DO_PROCESSING_FINAL:
			break;
		}

		WorkObject->WorkItemhandled = true;
	}

	std::deque<QueueWorkItem>   WorkResponseList;
	std::mutex                  ThreadPoolLock;
	TP_CALLBACK_ENVIRON         ThreadPoolEnvironment{};

	std::unique_ptr<ImageHelp>  TargetImage;
	std::unique_ptr<SymbolHelp> SymbolServer;
};
#pragma endregion



class VisualTrackerApp 
	: public IImageLoaderTracker {
public:

	void UpdateTrackerOrAbortCheck(
		IN TrackerInfoTag TrackerType
	) {
		TRACE_FUNCTION_PROTO;

		switch (TrackerType)
		{
		case TrackerInfoTag::TRACKER_UPDATE_RELOCS:
			++RelocsApplied;
			break;
		}

		// Check abort flag
		// TODO: implement
	}

	uint32_t              ApproximatedRelocs = 0;
	std::atomic<uint32_t> RelocsApplied = 0;
};


void GlfwErrorHandlerCallback(
	IN       int32_t ErrorCode,
	IN const char*   Description
) {
	TRACE_FUNCTION_PROTO;

	SPDLOG_ERROR("glfw failed with {}: \"{}\"",
		ErrorCode,
		Description);
}

enum EntryPointReturn : int32_t {
	STATUS_FAILED_INITILIZATION = -2000,
	STATUS_FAILED_PROCESSING,
	STATUS_FAILED_ARGUMENTS,
	STATUS_FAILED_IMAGEHELP,
	STATUS_FAILED_CFGTOOLS,

	STATUS_SUCCESS = 0,
};
export int32_t main(
		  int   argc,
	const char* argv[]
) {
	TRACE_FUNCTION_PROTO;

	// Instantiate prerequisites such as loggers and external libraries
	// ConsoleModifier ScopedConsole;
	GLFWwindow* PrimaryViewPort;
	xed_state_t XedConfiguration;
	try {
		// Initialize COM / OLE Components
		auto ComResult = CoInitializeEx(NULL,
			COINIT_MULTITHREADED);
		if (FAILED(ComResult)) {

			SPDLOG_ERROR("COM failed to initilialize with {}",
				ComResult);
			return STATUS_FAILED_INITILIZATION;
		}

		// Initialize xed's tables and configure encoder, decoder
		xed_tables_init();
		xed_state_init2(&XedConfiguration,
			XED_MACHINE_MODE_LONG_64,
			XED_ADDRESS_WIDTH_64b);

		// Initialize and configure spdlog/fmt
		spdlog::set_pattern(SPDLOG_SINGULARITY_SMALL_PATTERN);
		spdlog::set_level(spdlog::level::debug);



		// Configure and initialize glfw and imgui
		glfwSetErrorCallback(GlfwErrorHandlerCallback);
		if (!glfwInit())
			return STATUS_FAILED_INITILIZATION;
		const char* glsl_version = "#version 130";
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

		// Create window with graphics context and enable v-sync
		PrimaryViewPort = glfwCreateWindow(1280, 720, "VisualSingularitry : Bones", NULL, NULL);
		if (!PrimaryViewPort)
			return STATUS_FAILED_INITILIZATION;
		glfwMakeContextCurrent(PrimaryViewPort);
		glfwSwapInterval(1);

		// Setup Dear ImGui context and style
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& ImguiIo = ImGui::GetIO(); (void)ImguiIo;
		ImguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImguiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// ImguiIo.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		
		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGui::StyleColorsDark();
		ImGuiStyle& ImguiStyle = ImGui::GetStyle();
		if (ImguiIo.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			
			ImguiStyle.WindowRounding = 0.0f;
			ImguiStyle.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForOpenGL(PrimaryViewPort, true);
		ImGui_ImplOpenGL3_Init(glsl_version);
	}
	catch (const spdlog::spdlog_ex& ExceptionInformation) {

		SPDLOG_ERROR("SpdLog failed to initilizer with: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_INITILIZATION;
	}

	// Test
	// OpenFileDialogConfig Config{};
	// Config.ButtonText = "Load Executable";
	// Config.DefaultExtension = "exe";
	// Config.DialogTitle = "Select executable image to load into view";
	// Config.DefaultTypeIndex = 1;
	// Config.FileFilters = { { "Executable", "*.exe;*.dll;*.sys" },
	// 	{ "Symbols", "*.pdb" },
	// 	{"Any files", "*.*" } };
	// Config.FileNameLabel = "A file to load";
	// auto FileName = ComOpenFileDialogWithConfig(Config);
	// 
	// SPDLOG_INFO(FileName);
	// __debugbreak();


	VisualSingularityApp SingularityApiController(0);
	
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	SPDLOG_ERROR("TEST ERROR LOG");

	// Main loop
	while (!glfwWindowShouldClose(PrimaryViewPort)) {
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		glfwPollEvents();

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport();

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Primary Controller window, shows statistics and
		ImGui::Begin("GeneralProgress tracker");                          // Create a window called "Hello, world!" and append into it.
		{
			static enum FileSelectionState {
				STATE_LOCKED,
				STATE_NONE,
				STATE_WAITING_IMAGE,
				STATE_WAITING_PDB,
				STATE_LOADING,

			} ProgramState = STATE_NONE;
			static VisualTrackerApp SofTrackerObject{};
			static const VisualLoadImageObject* LoaderObjectState = nullptr;

			// File selection header 
			static char InputFilePath[MAX_PATH]{};
			static char PathToPdb[MAX_PATH]{};
			static char OutputPath[MAX_PATH]{};
			{
				static const VisualOpenFileObject* LastFileOpenDialog = nullptr;
				static bool EnableLastErrorMessage = false;
				static std::string LastErrorMessage{};
				static ImVec4 LastErrorColor{};

				// Check if we are currently waiting for a file selection by user
				switch (ProgramState) {
				case STATE_WAITING_IMAGE:
				case STATE_WAITING_PDB:

					// Handle last requested IFileOpenDialog
					switch (LastFileOpenDialog->GetCurrentObjectState()) {
					case VisualOpenFileObject::STATE_ERROR:
						LastErrorColor = ImVec4(1, 0, 0, 1);
						LastErrorMessage = "Com interface failed to select file";	
						break;

					case VisualOpenFileObject::STATE_DIALOG_DONE:

						if (!LastFileOpenDialog->GetResultString().empty())
							strcpy(std::array{ InputFilePath, PathToPdb }
								[ProgramState - STATE_WAITING_IMAGE],
								LastFileOpenDialog->GetResultString().data());
					}
					if (LastFileOpenDialog->GetCurrentObjectState() < 0 ||
						LastFileOpenDialog->GetCurrentObjectState() == 
						VisualOpenFileObject::STATE_DIALOG_DONE) {

						SingularityApiController.FreeWorkItemFromPool(LastFileOpenDialog);
						LastFileOpenDialog = nullptr;
						ProgramState = STATE_NONE;
					}
					break;

				case STATE_LOADING:

					// Check processing state for errors and reset
					switch (LoaderObjectState->GetCurrentObjectState()) {
					case VisualLoadImageObject::STATE_ABORTED:
						LastErrorColor = ImVec4(1, 1, 0, 1);
						break;
					case VisualLoadImageObject::STATE_ERROR:
						LastErrorColor = ImVec4(1, 0, 0, 1);
					}
					if (LoaderObjectState->GetCurrentObjectState() < 0) {

						LastErrorMessage = LoaderObjectState->GetExceptionReport()->ExceptionText;
						ProgramState = STATE_NONE;
						SingularityApiController.FreeWorkItemFromPool(LoaderObjectState);
						LoaderObjectState = nullptr;
					}
				}

				// Image load text input line
				ImGui::BeginDisabled(ProgramState != STATE_NONE);
				ImGui::InputTextWithHint("##InputFile", "Executable image to load...",
					InputFilePath, MAX_PATH);
				ImGui::SameLine();
				if (ImGui::Button("...##ImageSelect")) {

					OpenFileDialogConfig ConfigRetainer{};
					ConfigRetainer.DefaultExtension = ".exe;.dll;.sys";
					ConfigRetainer.FileNameLabel = "PE-COFF executable image:";
					ConfigRetainer.FileFilters = {
						{ "PE-COFF", "*.exe;*.dll;*.sys" },
						{ "PE-COFF2", "*.bin;*.scr"},
						{ "Any file", "*.*" } };
					ConfigRetainer.DefaultTypeIndex = 1;
					ConfigRetainer.ButtonText = "Select Executable";

					LastFileOpenDialog = SingularityApiController.QueueOpenFileRequestDialogWithTag(
						std::move(ConfigRetainer));
					ProgramState = STATE_WAITING_IMAGE;
				}
				ImGui::SameLine();
				ImGui::Text("Input-File");

				// Image load provide pdb or override
				static bool LoadWithPdb = true;
				ImGui::BeginDisabled(!LoadWithPdb);
				ImGui::InputTextWithHint("##PdbFile", "PDB to load... (keep this empty to locate pdb)",
					PathToPdb, MAX_PATH);
				ImGui::SameLine();
				if (ImGui::Button("...##PdbSelect")) {

					OpenFileDialogConfig ConfigRetainer{};
					ConfigRetainer.DefaultExtension = ".pdb";
					ConfigRetainer.DefaultSelection = filesystem::path(InputFilePath).replace_extension(".pdb").filename().string();
					ConfigRetainer.FileNameLabel = "Program database file:";
					ConfigRetainer.FileFilters = {
						{ "Symbols", "*.pdb" },
						{ "Any file", "*.*" } };
					ConfigRetainer.DefaultTypeIndex = 1;
					ConfigRetainer.ButtonText = "Select PDB";

					filesystem::path InputFilePath2(InputFilePath);
					ConfigRetainer.DialogTitle = InputFilePath2.empty() ? "Select program database file to load"
						: fmt::format("Select program database file to load for \"{}\"",
							InputFilePath2.filename().string());

					LastFileOpenDialog = SingularityApiController.QueueOpenFileRequestDialogWithTag(
						std::move(ConfigRetainer));
					ProgramState = STATE_WAITING_PDB;
				}
				ImGui::SameLine();
				ImGui::Text("PDB-File");
				ImGui::EndDisabled();

				// Provide target output path
				ImGui::InputTextWithHint("##TargetFile", "Optional Filename to store the file as...",
					OutputPath, MAX_PATH);
				ImGui::SameLine();
				if (ImGui::Button("Gen")) {

					// Generate a suitable path name from input path
					// TODO: optimize this and deuglify
					filesystem::path InputFilePath2(InputFilePath);
					InputFilePath2.replace_filename(
						InputFilePath2.stem().string().append(
							"-Mutated").append(
								InputFilePath2.extension().string()));
					strcpy(OutputPath, InputFilePath2.string().c_str());
				}
				ImGui::SameLine();
				ImGui::Text("Target");

				// Draw small config and load button, including logic
				ImGui::Checkbox("Load with Symbols", &LoadWithPdb);
				ImGui::SameLine();
				if (ImGui::Button("Load Image###LoadImageTrigger")) {

					// Load image here
					VisualLoadImageObject LoaderObject(
						InputFilePath,
						!LoadWithPdb,
						PathToPdb,
						&SofTrackerObject);
					LoaderObjectState = SingularityApiController.QueueLoadImageRequestWithTracker(
						std::move(LoaderObject));
					ProgramState = STATE_LOADING;
				}
				ImGui::EndDisabled();

				if (EnableLastErrorMessage)
					ImGui::TextColored(LastErrorColor, LastErrorMessage.c_str());
			}
			ImGui::Separator();

			// The progress section, a quick lookup for some data while doing things
			{
				static float GlobalProgress = 0.0f;
				static float SubFrameProgress = 0.0f;

				// File Input output selection
				ImGui::ProgressBar(GlobalProgress);
				ImGui::SameLine();
				ImGui::Text("GlobalProgress");

				ImGui::ProgressBar(SubFrameProgress);
				ImGui::SameLine();
				ImGui::Text("SubFrame");
			}
			ImGui::Separator();

			ImGui::Checkbox("Enable DemoWindow", &show_demo_window);
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
				1000.0f / ImGui::GetIO().Framerate,
				ImGui::GetIO().Framerate);
		}
		ImGui::End();

		// 3. Show another simple window.
		if (show_another_window)
		{
			ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
			ImGui::Text("Hello from another window!");
			if (ImGui::Button("Close Me"))
				show_another_window = false;
			ImGui::End();
		}

		// Rendering imgui and copying to view port
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(PrimaryViewPort, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w,
			clear_color.y * clear_color.w,
			clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		// (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
		//  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {

			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		glfwSwapBuffers(PrimaryViewPort);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(PrimaryViewPort);
	glfwTerminate();

	return STATUS_SUCCESS;
}
