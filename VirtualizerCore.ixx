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

import StatisticsTracker;
import ImageHelp;
import DisassemblerEngine;
import SymbolHelp;

using namespace std::chrono;

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


// Syntax rules for program options:
// - We accept a argv/agrc style tokenized commandline as input (provided by main)
// - The first entry always referrers to the invocation of this software (?.exe)
// - Non annotated arguments are interpreted as a path (how they are used is unspecified)
// - Arguments annotated with a single dash "-" are interpreted as flags:
//   They can consist of a single case sensitive letter followed by an +/- to enable/disable
//   said flag, or consist of flags followed by an +/- to enabled/disable all of them listed
// - Arguments annotated with a double dash "--" are interpreted as long arguments:
//   They can be interpreted as long names for flags (single annotated arguments),
//   but cannot be an array of those.
//   They can also represent a key-value pair that can be separated by a space, equal or colon
//   (possibly also not be separated at all by anything, idk have to decide)
// 
class ProgramOptions {
public:
	enum ArgumentType {
		ARGUMENT_PURE,
		ARGUMENT_FLAG,
		ARGUMENT_UNSPECIFIED
	};
	struct AcceptedArgument {
		ArgumentType TypeOfArgument;
		bool         ArgumentWasParsed;

		char8_t     ShortArgumentForm;
		std::string LongArgumentForm;
		std::string ArgumentDescription;

		std::variant<std::string,
			bool> ArgumentValue;
	};
	using ArgumentList = std::vector<AcceptedArgument>;


	void AddArgumentToList(
		IN        ArgumentType      TypeOfArgument,
		OPT       bool              DefaultFlagState,
		OPT       char8_t           ShortForm,
		OPT const std::string_view& LongForm,
		OPT const std::string_view& Description
	) {
		TRACE_FUNCTION_PROTO;

		AcceptedArgumentList.emplace_back(AcceptedArgument{
			.TypeOfArgument = TypeOfArgument,
			.ShortArgumentForm = ShortForm,
			.LongArgumentForm = std::string(LongForm),
			.ArgumentDescription = std::string(Description),
			.ArgumentValue = DefaultFlagState
			});
	}

	void ReparseArguments(
		IN std::span<const char*> ArgumentVector
	) {
		TRACE_FUNCTION_PROTO;
	}

private:
	// TODO: Create function that exposes view of unspecified argument types 
	// (unannotated arguments not part of key-value pairs)
	class UnspecifiedArgumentFilterForView {
	public:
		bool operator()(
			IN const AcceptedArgument& ListEntry
			) {

			return false;
		}
	};
	template<ArgumentType TypeTag>
	struct TypeEnum;
	#define MAP_ENUM_TO_TYPE(Enum, Type) template<>\
	struct TypeEnum<Enum> {\
		using type = Type;\
	}
	MAP_ENUM_TO_TYPE(ARGUMENT_PURE, std::string);
	MAP_ENUM_TO_TYPE(ARGUMENT_FLAG, bool);
	#undef MAP_ENUM_TO_TYPE
public:
	template<ArgumentType TypeTag>
	TypeEnum<TypeTag>::type GetPropertyByTemplateSpecification(
		IN        char8_t           ShortFlagName,
		OPT const std::string_view& OptionalLongName = {}
	) {
		using namespace std::placeholders;
		TRACE_FUNCTION_PROTO;

		auto FlagsIterator = std::find_if(AcceptedArgumentList.begin(),
			AcceptedArgumentList.end(),
			std::bind(FilterComparitorBindable<TypeTag>,
				_1,
				ShortFlagName,
				OptionalLongName));
		if (FlagsIterator == AcceptedArgumentList.end())
			return false;

		return std::get<TypeEnum<TypeTag>::type>(FlagsIterator->ArgumentValue);
	}

private:
	template<ArgumentType TypeOfArgument>
	static bool FilterComparitorBindable(
		IN  ArgumentList::const_reference ListEntry,
		IN        char8_t                 ShortFlagName,
		OPT const std::string_view&       OptionalLongName
	) {
		TRACE_FUNCTION_PROTO;

		if (ListEntry.TypeOfArgument != TypeOfArgument)
			return false;
		if (ShortFlagName)
			return ListEntry.ShortArgumentForm == ShortFlagName;
		if (!OptionalLongName.empty())
			return ListEntry.LongArgumentForm.compare(OptionalLongName);
		return false;
	}



	ArgumentList AcceptedArgumentList;
};

#if 0
void legacy_context() {

	// Primary processing closure
	try {
		StatisticsTracker ProfilerSummary(64ms);

		const auto FileName = argv[1];
		ImageHelp TargetImage(FileName);
		TargetImage.MapImageIntoMemory();
		TargetImage.RejectFileCloseHandles();
		TargetImage.RelocateImageToMappedOrOverrideBase();

		SymbolHelp SymbolServer(TargetImage,
			"SRV*C:\\Symbols*https://msdl.microsoft.com/download/symbols");

		CfgGenerator ControlFlowGraphGenerator(TargetImage,
			IntelXedConfiguration);




		// Enumerate all function entries here
		auto RuntimeFunctionsView = TargetImage.GetRuntimeFunctionTable();
		for (const RUNTIME_FUNCTION& RuntimeFunction : RuntimeFunctionsView) {

			// Generate Cfg from function

			// Tests
			// auto FunctionAddres = SymbolServer.FindFunctionFrameByName("StubTestFunction2");
			// auto GraphForFunction = ControlFlowGraphGenerator.GenerateCfgFromFunction2(
			// 	FunctionAddres);
			// auto InvalidXRefs = GraphForFunction.ValidateCfgOverCrossReferences();
			// __debugbreak();

			try {

				auto GraphForRuntimeFunction = ControlFlowGraphGenerator.GenerateCfgFromFunction2(
					FunctionAddress(
						RuntimeFunction.BeginAddress + TargetImage.GetImageFileMapping(),
						RuntimeFunction.EndAddress - RuntimeFunction.BeginAddress));
				auto Failures = GraphForRuntimeFunction.ValidateCfgOverCrossReferences();
				if (Failures) {

					__debugbreak();

				}
			}
			catch (const CfgToolException& GraphException) {

				SPDLOG_ERROR("Graph generation failed failed with [{}] : \"{}\", skipping frame",
					GraphException.StatusCode,
					GraphException.ExceptionText);

				// if (IsDebuggerPresent())
				// 	__debugbreak();
			}


		}



		// Discard changes for now
		TargetImage.RejectAndDiscardFileChanges();
	}
	catch (const ImageHelpException& MapperException) {

		SPDLOG_ERROR("ImageHelp failed with [{}] : \"{}\"",
			MapperException.StatusCode,
			MapperException.ExceptionText);
		return STATUS_FAILED_IMAGEHELP;
	}
	catch (const std::exception& ExceptionInformation) {

		SPDLOG_ERROR("Primary execution failed with standard exception: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_PROCESSING;
	}


}


#endif


#pragma region COM OpenFileDialog
// Highly inefficient and horribly ugly shitty code, but its user driven anyways...
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

	static std::unique_ptr<OpenFileDialogConfig> AllocateFileDialogConfig() {
		TRACE_FUNCTION_PROTO;
		struct EnableMakeUnique : public OpenFileDialogConfig {};
		return std::make_unique<EnableMakeUnique>();
	} 

private:
	OpenFileDialogConfig() = default;
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


class VisualSingularityApp {
public:
	enum ControlRequest {
		CWI_OPEN_FILE,
		CWI_LOAD_FILE,
		CWI_LOAD_SYMBOLS,
		CWI_DO_PROCESSING_FINAL
	};
	struct QueueWorkItem {
		VisualSingularityApp* OwnerController;
		token_t               ObjectToken;

		ControlRequest ControlCommand;
		bool           WorkItemhandled;
		uint32_t       StatusResult;

		// Special case context data, this struct is incredibly inefficient but whatever
		std::string FilePathResultOrDesiredPath;
		
		std::unique_ptr<OpenFileDialogConfig> PossibleOpenDialogConfig;

	};

	VisualSingularityApp(
		IN uint32_t NumberOfThreads
	) {
		TRACE_FUNCTION_PROTO;

		InitializeThreadpoolEnvironment(&ThreadPoolEnvironment);

	}


	token_t QueueOpenFileRequestDialogWithTag(                // Open a IFileOpenDialog interface,
		                                                      // the result can later be retrieved using the returned token
		IN std::unique_ptr<OpenFileDialogConfig> DialogConfig // A pointer to holding the DialogConfig
	) {
		TRACE_FUNCTION_PROTO;

		const std::lock_guard Lock(ThreadPoolLock);
		auto TokenForRequest = GenerateGlobalUniqueTokenId();
		auto& WorkEntry = WorkResponseList.emplace_back(QueueWorkItem{
			.OwnerController = this,
			.ObjectToken = TokenForRequest,
			.ControlCommand = CWI_OPEN_FILE,
			.PossibleOpenDialogConfig = std::move(DialogConfig) });
		
		auto Status = TrySubmitThreadpoolCallback(WorkQueueDisptachThread,
			static_cast<void*>(&WorkEntry),
			&ThreadPoolEnvironment);
		if (!Status) {
		
			WorkResponseList.pop_back();
			throw std::runtime_error("failed to post work to queue");
		}

		return TokenForRequest;
	}

	bool QueueCheckoutTokenDialogResult(
		IN  token_t      MatchableToken,
		OUT std::string& ResultString
	) {
		TRACE_FUNCTION_PROTO;

		// Lock list and search for token / property match
		const std::lock_guard Lock(ThreadPoolLock);
		auto SearchIterator = std::find_if(WorkResponseList.begin(),
			WorkResponseList.end(),
			[MatchableToken](
				IN const QueueWorkItem& QueueEntry
				) -> bool {
					TRACE_FUNCTION_PROTO;
					return QueueEntry.ControlCommand == CWI_OPEN_FILE
						&& QueueEntry.ObjectToken == MatchableToken
						&& QueueEntry.WorkItemhandled;
			});
		if (SearchIterator == WorkResponseList.end())
			return false;

		// Found entry, get the content and remove entry from que
		ResultString = std::move(SearchIterator->FilePathResultOrDesiredPath);
		WorkResponseList.erase(SearchIterator);
		return true;
	}


private:
	static void WorkQueueDisptachThread(
		IN    PTP_CALLBACK_INSTANCE CallbackInstance,
		INOUT void*                 UserContext
	) {
		TRACE_FUNCTION_PROTO;

		auto WorkObject = static_cast<QueueWorkItem*>(UserContext);

		switch (WorkObject->ControlCommand) {
		case CWI_OPEN_FILE: {

			auto Status = CallbackMayRunLong(CallbackInstance);
			if (!Status)
				SPDLOG_WARN("Long running function in quick threadpool, failed to created dedicated thread");
			WorkObject->FilePathResultOrDesiredPath = ComOpenFileDialogWithConfig(*WorkObject->PossibleOpenDialogConfig);
			SPDLOG_INFO("User selected \"{}\", for image",
				WorkObject->FilePathResultOrDesiredPath);
			
			WorkObject->WorkItemhandled = true;
			break;
		}
		case CWI_LOAD_FILE:
			break;
		case CWI_LOAD_SYMBOLS:
			break;
		case CWI_DO_PROCESSING_FINAL:
			break;


		default:
			break;
		}

	}

	std::deque<QueueWorkItem> WorkResponseList;
	std::mutex                ThreadPoolLock;
	TP_CALLBACK_ENVIRON       ThreadPoolEnvironment{};


	ImageHelp*    TargetImage = nullptr;
	SymbolHelp*   SymbolServer = nullptr;
	CfgGenerator* CfgGenContext = nullptr;
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



		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);


		// 2. Primary Controller window, shows statistics and
		ImGui::Begin("GeneralProgress tracker");                          // Create a window called "Hello, world!" and append into it.
		{

			// File selection header 
			static char InputFilePath[MAX_PATH]{};
			static char PathToPdb[MAX_PATH]{};
			static char OutputPath[MAX_PATH]{};
			{
				static enum FileSelectionState {
					STATE_NONE,
					STATE_LOCKED,

					STATE_WAITING_IMAGE,
					STATE_WAITING_PDB,
					STATE_WAiTING_OUT,
				} SelectionState = STATE_NONE;
				static token_t LastRequestToken = 0;

				// Check if we are currently waiting for a file selection by user
				if (SelectionState >= STATE_WAITING_IMAGE) {

					std::string SelectedFileRetainer;
					auto Status = SingularityApiController.QueueCheckoutTokenDialogResult(LastRequestToken,
						SelectedFileRetainer);
					if (Status) {
						
						if (SelectedFileRetainer.size())
							strcpy(std::array{ InputFilePath, PathToPdb, OutputPath }
								[SelectionState - STATE_WAITING_IMAGE],
								SelectedFileRetainer.c_str());

						SelectionState = STATE_NONE;
					}
				}

				// Image load text input line
				ImGui::BeginDisabled(SelectionState != STATE_NONE);
				ImGui::InputTextWithHint("Input-File", "Executable image to load...",
					InputFilePath, MAX_PATH);
				ImGui::SameLine();
				if (ImGui::Button("...")) {

					auto ConfigRetainer = OpenFileDialogConfig::AllocateFileDialogConfig();
					// ConfigRetainer->FileNameLabel = ""
					ConfigRetainer->ButtonText = "Select executable";

					LastRequestToken = SingularityApiController.QueueOpenFileRequestDialogWithTag(
						std::move(ConfigRetainer));
					SelectionState = STATE_WAITING_IMAGE;
				}

				// image load provide pdb or override
				static bool LoadWithPdb = true;
				if (LoadWithPdb) {

					ImGui::InputTextWithHint("PDB-File", "PDB to load... (keep this empty to locate pdb)",
						PathToPdb, MAX_PATH);
					ImGui::SameLine();
					if (ImGui::Button("..")) {

						auto ConfigRetainer = OpenFileDialogConfig::AllocateFileDialogConfig();
						// ConfigRetainer->FileNameLabel = ""
						ConfigRetainer->ButtonText = "Select executable";

						LastRequestToken = SingularityApiController.QueueOpenFileRequestDialogWithTag(
							std::move(ConfigRetainer));
						SelectionState = STATE_WAITING_PDB;
					}
				}

				// Provide target output path
				ImGui::InputTextWithHint("Target", "Optional Filename to store the file as...",
					OutputPath, MAX_PATH);
				ImGui::Checkbox("Load with Symbols", &LoadWithPdb);
				ImGui::SameLine();
				if (ImGui::Button("Load Image")) {

					SelectionState = STATE_WAiTING_OUT;
				}
				ImGui::EndDisabled();
			}


			ImGui::Separator();



			static float GlobalProgress = 0.0f;
			static float SubFrameProgress = 0.0f;
			
			// File Input output selection
			ImGui::ProgressBar(GlobalProgress);
			ImGui::SameLine();
			ImGui::Text("GlobalProgress");

			ImGui::ProgressBar(SubFrameProgress);
			ImGui::SameLine();
			ImGui::Text("SubFrame");


			

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
