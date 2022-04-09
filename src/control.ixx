// This is the primary controller of VisualSingularity and clisof,
// it implements the entrypoint, startup, and main display.
// On top of that it semi acts as a proxy for its other submodules

#include "sof/sof.h"
#include <glfw/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_glfw.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>

import sof.control.gui;
import sof.control.tasks;



enum EntryPointReturn : int32_t {
	STATUS_FAILED_INITILIZATION = -2000,
	STATUS_FAILED_PROCESSING,
	STATUS_FAILED_ARGUMENTS,
	STATUS_FAILED_IMAGEHELP,
	STATUS_FAILED_CFGTOOLS,
	STATUS_FAILED_GUI_LOGIC,

	STATUS_SUCCESS = 0,
};

void GlfwErrorHandlerCallback(
	IN       int32_t ErrorCode,
	IN const char* Description
) {
	TRACE_FUNCTION_PROTO;
	SPDLOG_ERROR("glfw failed with " ESC_BRIGHTRED"{}" ESC_RESET": " ESC_BRIGHTYELLOW"\"{}\"",
		ErrorCode,
		Description);
}
export int32_t WinMain(
	IN  HINSTANCE hInstance,
	OPT HINSTANCE hPrevInstance,
	IN  LPSTR     lpCmdLine,
	IN  int32_t   nShowCmd
) {
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nShowCmd);
	TRACE_FUNCTION_PROTO;
	// AttachConsole(ATTACH_PARENT_PROCESS);

	// Instantiate prerequisites such as loggers and external libraries
	// ConsoleModifier ScopedConsole;
	std::shared_ptr<spdlog::logger> VisualSingularityLogger;
	try {
		// Initialize and configure spdlog/fmt logger
		spdlog::init_thread_pool(65536, 1);
		VisualSingularityLogger = spdlog::create_async<sof::ImGuiSpdLogAdaptor>("VSOFL");
		spdlog::set_default_logger(VisualSingularityLogger);
		spdlog::set_pattern(SPDLOG_SINGULARITY_SMALL_PATTERN);
#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
#else
		spdlog::set_level(spdlog::level::debug);
#endif
		SPDLOG_INFO("Initilialized singularity obfuscation framework logger");
	}
	catch (const spdlog::spdlog_ex& ExceptionInformation) {

		SPDLOG_ERROR("SpdLog failed to initilizer with: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_INITILIZATION;
	}

	// Initialize COM / OLE Components
	auto ComResult = CoInitializeEx(NULL,
		COINIT_MULTITHREADED);
	if (FAILED(ComResult)) {

		SPDLOG_ERROR("COM failed to initilialize with {}",
			ComResult);
		return STATUS_FAILED_INITILIZATION;
	}
	SPDLOG_INFO("Fully initialized COM/OLE");

	// Initialize xed's tables and configure encoder, decoder
	xed_state_t XedConfiguration;
	xed_tables_init();
	xed_state_init2(&XedConfiguration,
		XED_MACHINE_MODE_LONG_64,
		XED_ADDRESS_WIDTH_64b);
	SPDLOG_INFO("Initialized intelxed's decoder/encoder tables");
	xed_format_options_t XedFormatConfig{
		.hex_address_before_symbolic_name = true,
		.xml_a = false,
		.xml_f = false,
		.omit_unit_scale = true,
		.no_sign_extend_signed_immediates = true,
		.write_mask_curly_k0 = false,
		.lowercase_hex = true,
	};
	xed_format_set_options(XedFormatConfig);

	// Configure and initialize glfw and imgui
	glfwSetErrorCallback(GlfwErrorHandlerCallback);
	if (!glfwInit())
		return STATUS_FAILED_INITILIZATION;
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	SPDLOG_INFO("Initialized glfw opengl backend and installed callback");

	// Create window with graphics context and enable v-sync
	GLFWwindow* PrimaryViewPort = glfwCreateWindow(1280, 720,
		"VisualSingularitry : Bones",
		nullptr, nullptr);
	if (!PrimaryViewPort)
		return STATUS_FAILED_INITILIZATION;
	glfwMakeContextCurrent(PrimaryViewPort);
	glfwSwapInterval(1);
	SPDLOG_INFO("Created glfw window context (vsync enabled)");

	// Setup Dear ImGui context and style
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& ImguiIo = ImGui::GetIO(); (void)ImguiIo;
	ImguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImguiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// ImguiIo.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	SPDLOG_INFO("Created and initialized Dear-ImGui context");
	
	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& ImguiStyle = ImGui::GetStyle();
	ImGui::StyleColorsDark();
	ImguiStyle.FrameBorderSize = 1;
	if (ImguiIo.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		
		ImguiStyle.WindowRounding = 0.0f;
		ImguiStyle.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
	SPDLOG_INFO("Configured Dear-ImGui style options");

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(PrimaryViewPort, true);
	ImGui_ImplOpenGL3_Init(glsl_version);
	SPDLOG_INFO("Initialized Dear-ImGui's glfw/opengl3 backend");

	// These are the core components of the singularity backend to ui translation interface.
	// They provide a simplified desynchronized interface for singularities api in the context
	// of being usable from the ui, this obviously means that the things you can do through the ui
	// are partially restricted in the sense that they don't allow full control of the api.
	sof::VisualSingularityApp SingularityApiController(0);
	sof::VisualTrackerApp DataTrackLog(SingularityApiController);
	auto VisualLogger = static_cast<sof::ImGuiSpdLogAdaptor*>(VisualSingularityLogger->sinks()[0].get());
	enum ImmediateProgramPhase {
		PHASE_NEUTRAL_STARTUP = 0,
		STATE_WAITING_INPUTFILE_DLG,
		STATE_WAITING_PDBFILE_DLG,
		STATE_IMAGE_LOADING_MEMORY,
		STATE_IMAGE_LOADED_WAITING,
		STATE_IMAGE_PROCESSING_AUTO,
		STATE_IMAGE_WAS_PROCESSED

	} ProgramPhase = PHASE_NEUTRAL_STARTUP;
	SPDLOG_INFO("Initialized and configured VisualSingularity' interface");	
	
	// SPDLOG_INFO("Test print with newline\nand "ESC_MAGENTA"magente colored text"ESC_RESET" going back to normal");
	// SPDLOG_TRACE("This is a test print "ESC_BRIGHTBLUE"with bright blue text\nwrapping around lines,\nand automatic color reset");
	// SPDLOG_DEBUG("Debug messages with default color");
	// SPDLOG_WARN("A working with "ESC_RED"dangerous red text"ESC_RESET);
	// SPDLOG_ERROR("Uhh a "ESC_BRIGHTRED"Error"ESC_RESET" occured!!");
	// SPDLOG_CRITICAL("Well even worse now\n\n with even more errors...");

	// Primary render loop, this takes care of all the drawing and user io, it takes full control of the backend
	try {
		while (!glfwWindowShouldClose(PrimaryViewPort)) {

			// Poll and handle events (inputs, window resize, etc.)
			// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
			// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
			// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
			// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
			glfwPollEvents();

			// Start the Dear ImGui frame
			static ImVec4 ClearBackgroundColor(0.45f, 0.55f, 0.60f, 1.00f);
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			ImGui::DockSpaceOverViewport(); // draw main window dock space

			// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
			static bool EnableImguiDemoWindow = true;
			if (EnableImguiDemoWindow)
				ImGui::ShowDemoWindow(&EnableImguiDemoWindow);

			// 2. Primary Controller window, shows statistics and
			ImGui::Begin("GeneralProgress tracker");                          // Create a window called "Hello, world!" and append into it.
			{
				static const sof::VisualLoadImageObject* LoaderObjectState = nullptr;

				// File selection header 
				static char InputFilePath[MAX_PATH]{};
				static char PathToPdb[MAX_PATH]{};
				static char OutputPath[MAX_PATH]{};
				static bool LoadWithPdb = true;
				{
					static const sof::VisualOpenFileObject* LastFileOpenDialog = nullptr;
					static bool EnableLastErrorMessage = false;
					static std::string LastErrorMessage{};
					static ImVec4 LastErrorColor{};

					// Check if we are currently waiting for a file selection by user
					switch (ProgramPhase) {
					case STATE_WAITING_INPUTFILE_DLG:
					case STATE_WAITING_PDBFILE_DLG:

						// Handle last requested IFileOpenDialog
						switch (LastFileOpenDialog->GetWorkObjectState()) {
						case sof::VisualOpenFileObject::STATE_COM_ERROR:
							LastErrorColor = ImVec4(1, 0, 0, 1);
							LastErrorMessage = "Com interface failed to select file";
							break;

						case sof::VisualOpenFileObject::STATE_DIALOG_DONE:
							if (!LastFileOpenDialog->GetResultString().empty())
								strcpy(std::array{ InputFilePath, PathToPdb }
									[ProgramPhase - STATE_WAITING_INPUTFILE_DLG],
									LastFileOpenDialog->GetResultString().data());
						}
						if (LastFileOpenDialog->GetWorkObjectState() < sof::IVisualWorkObject::STATE_CALL_SCHEDULED ||
							LastFileOpenDialog->GetWorkObjectState() >= sof::IVisualWorkObject::STATE_CALL_FINISHED) {

							// Close the dialog dispatch object
							SingularityApiController.FreeWorkItemFromPool(LastFileOpenDialog);
							LastFileOpenDialog = nullptr;
							ProgramPhase = PHASE_NEUTRAL_STARTUP;
						}
						break;

					case STATE_IMAGE_LOADING_MEMORY:

						// Check processing state for errors and reset
						switch (LoaderObjectState->GetWorkObjectState()) {
						case sof::VisualLoadImageObject::STATE_ABORTED:
							LastErrorColor = ImVec4(1, 1, 0, 1);
							break;
						case sof::VisualLoadImageObject::STATE_ERROR:
							LastErrorColor = ImVec4(1, 0, 0, 1);
						}
						if (LoaderObjectState->GetWorkObjectState() < sof::IVisualWorkObject::STATE_CALL_SCHEDULED) {

							LastErrorMessage = LoaderObjectState->GetExceptionReport()->ExceptionText;
							ProgramPhase = PHASE_NEUTRAL_STARTUP;
							SingularityApiController.FreeWorkItemFromPool(LoaderObjectState);
							LoaderObjectState = nullptr;
						}
					}

					// Image load text input line
					ImGui::BeginDisabled(ProgramPhase != PHASE_NEUTRAL_STARTUP);
					ImGui::InputTextWithHint("##InputFile", "Executable image to load...",
						InputFilePath, MAX_PATH);
					ImGui::SameLine();
					if (ImGui::Button("...##ImageSelect")) {

						sof::OpenFileDialogConfig ConfigRetainer{};
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
						ProgramPhase = STATE_WAITING_INPUTFILE_DLG;
					}
					ImGui::SameLine();
					ImGui::Text("Input-File");

					// Image load provide pdb or override
					ImGui::BeginDisabled(!LoadWithPdb);
					ImGui::InputTextWithHint("##PdbFile", "PDB to load... (keep this empty to locate pdb)",
						PathToPdb, MAX_PATH);
					ImGui::SameLine();
					if (ImGui::Button("...##PdbSelect")) {

						sof::OpenFileDialogConfig ConfigRetainer{};
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
						ProgramPhase = STATE_WAITING_PDBFILE_DLG;
					}
					ImGui::SameLine();
					ImGui::Text("PDB-File");
					ImGui::EndDisabled();
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
					ImGui::BeginDisabled(ProgramPhase == STATE_IMAGE_LOADING_MEMORY);
					ImGui::Checkbox("Load with Symbols", &LoadWithPdb);
					ImGui::SameLine();
					if (ImGui::Button(ProgramPhase >= STATE_IMAGE_LOADED_WAITING ?
						"Unload Image###LoadImageTrigger" : "Load Image###LoadImageTrigger")) {


						if (ProgramPhase >= STATE_IMAGE_LOADED_WAITING) {

							// Need to unload the image
							SingularityApiController.FreeWorkItemFromPool(LoaderObjectState);
							SPDLOG_INFO("Unloaded image object " ESC_BRIGHTRED"{}",
								static_cast<const void*>(LoaderObjectState));
							LoaderObjectState = nullptr;
							DataTrackLog.ResetData();
							ProgramPhase = PHASE_NEUTRAL_STARTUP;
						}
						else {

							// Load image here, this schedules a load and stores the load object.
							// This object is temporary and valid only for the load,
							// it has to be trashed and returned to a cleanup function to free it,
							// after that contained information is no longer valid and must be preserved.
							LoaderObjectState = SingularityApiController.QueueLoadImageRequestWithTracker2(
								std::make_unique<sof::VisualLoadImageObject>(
									InputFilePath,
									!LoadWithPdb,
									PathToPdb,
									&DataTrackLog));
							ProgramPhase = STATE_IMAGE_LOADING_MEMORY;
						}
					}
					ImGui::EndDisabled();
					if (EnableLastErrorMessage)
						ImGui::TextColored(LastErrorColor, LastErrorMessage.c_str());
					ImGui::Spacing();

					// Virtual spooling and load controls
					static const ImU32 SliderMax = UINT32_MAX / 2;
					static const ImU32 SliderMin = 0;
					static ImU32 SliderValue = 0;
					ImGui::SliderScalar("Virtual wait timer...", ImGuiDataType_U32,
						&SliderValue, &SliderMin, &SliderMax,
						"%d us", ImGuiSliderFlags_Logarithmic);
					DataTrackLog.VirtualSleepSliderInUs = SliderValue;
					ImGui::SameLine();
					sof::HelpMarker("Virtual time spooling can make use of an sleeping spinlocking hybrid interval timer, "
						"this can result in cpu thrashing as its extremely hard on a core while waiting a very short interval.\n"
						"Anything below the minimum system clock resolution will be spinlocked, "
						"switch to blocking mode to avoid thrashing, this will however result in inaccurate timings.\n\n"
						"This feature is disabled in multithreaded work mode, use single threading to slow down time!");
					if (ImGui::Button(DataTrackLog.PauseSeriveExecution ? "Resume###ToggleSvrExec" : "Pause###ToggleSvrExec")) {

						DataTrackLog.PauseSeriveExecution = !DataTrackLog.PauseSeriveExecution;
						SPDLOG_INFO("Toggled serive execution, currently {}",
							DataTrackLog.PauseSeriveExecution ? "paused" : "running");
					}
					ImGui::SameLine();
					static bool SpinLockDisable = false;
					ImGui::Checkbox("Disable spinlocking", &SpinLockDisable);
				}
				ImGui::Separator();

				// The progress section, a quick lookup for some data while doing things
				static const sof::VisualSOPassObject* MainSOBPassObject = nullptr;
				{
					static const char* SelectionStateText;

					static std::string GlobalProgressOverlay{};
					static float GlobalProgress = 0.0f;
					static std::string SubFrameProgressOverlay{};
					static float SubFrameProgress = 0.0f;

					switch (ProgramPhase) {
					case PHASE_NEUTRAL_STARTUP:
						SelectionStateText = "Waiting for file selection...";
						GlobalProgressOverlay = {};
						GlobalProgress = 0;
						SubFrameProgressOverlay = {};
						SubFrameProgress = 0;
						break;

					case STATE_WAITING_INPUTFILE_DLG:
					case STATE_WAITING_PDBFILE_DLG:
						break;

					case STATE_IMAGE_LOADING_MEMORY: // The backend is loading the file, we can print load information

						switch (LoaderObjectState->GetWorkObjectState()) {
						case sof::VisualLoadImageObject::STATE_SCHEDULED:
							SelectionStateText = "File load is being scheduled...";
							GlobalProgressOverlay = LoadWithPdb ? "0 / 3" : "0 / 2";
							break;

						case sof::VisualLoadImageObject::STATE_LAODING_SYMBOLS:
							SelectionStateText = "Loading symbols for image";
							GlobalProgressOverlay = LoadWithPdb ? "0 / 3" : "0 /2";
							SubFrameProgressOverlay = "Cannot track sub processing, waiting...";
							SubFrameProgress = 0;
							break;

						case sof::VisualLoadImageObject::STATE_LOADING_MEMORY:
							SelectionStateText = "Loading image into primary system memory";
							GlobalProgressOverlay = LoadWithPdb ? "1 / 3" : "1 / 2";
							GlobalProgress = 1.f / (LoadWithPdb ? 3 : 2);
							SubFrameProgressOverlay = fmt::format("Loaded {} / {}",
								DataTrackLog.NumberOfSectionsLoaded,
								DataTrackLog.NumberOfSectionsInImage);
							if (DataTrackLog.NumberOfSectionsInImage)
								SubFrameProgress = static_cast<float>(DataTrackLog.NumberOfSectionsLoaded) /
								DataTrackLog.NumberOfSectionsInImage;
							break;

						case sof::VisualLoadImageObject::STATE_RELOCATING:
							SelectionStateText = "Relocating image in memory view";
							GlobalProgressOverlay = LoadWithPdb ? "2 / 3" : "1 /2";
							GlobalProgress = (2.f - !LoadWithPdb) / (LoadWithPdb ? 3 : 2);
							SubFrameProgressOverlay = DataTrackLog.RelocsApplied > LoaderObjectState->GetPolyXNumber() ?
								fmt::format("{} / {}+",
								DataTrackLog.RelocsApplied,
								LoaderObjectState->GetPolyXNumber())
								: fmt::format("{} / {}",
								DataTrackLog.RelocsApplied,
								LoaderObjectState->GetPolyXNumber());
							SubFrameProgress = static_cast<float>(DataTrackLog.RelocsApplied) / LoaderObjectState->GetPolyXNumber();
							break;

						case sof::VisualLoadImageObject::STATE_FULLY_LOADED:
							SelectionStateText = "Fully loaded image into view";
							GlobalProgressOverlay = LoadWithPdb ? "3 / 3" : "2 / 2";
							GlobalProgress = 1;
							SubFrameProgressOverlay = fmt::format("{0} / {0}",
								DataTrackLog.RelocsApplied);
							SubFrameProgress = 1;
							ProgramPhase = STATE_IMAGE_LOADED_WAITING;
							break;

						default:
							SPDLOG_CRITICAL("May not be able to end up here, faults have to be handled at a different position");
							__fastfail(STATUS_FAILED_GUI_LOGIC);
						}
						break;

					case STATE_IMAGE_PROCESSING_AUTO:
						switch (MainSOBPassObject->GetWorkObjectState()) {

						case sof::VisualSOPassObject::STATE_PROCESSING:
							SelectionStateText = "Processing image data";

							GlobalProgressOverlay = fmt::format("{} / {}",
								MainSOBPassObject->FramesProcessed,
								MainSOBPassObject->NumberOfFunctions);
							GlobalProgress = static_cast<float>(MainSOBPassObject->FramesProcessed) /
								MainSOBPassObject->NumberOfFunctions;
							SubFrameProgressOverlay = "Processing sub frames, to be implemented";
							SubFrameProgress = 0;
							break;


						case sof::VisualSOPassObject::STATE_ALL_PASSES_APLIED_DONE:
							ProgramPhase = STATE_IMAGE_WAS_PROCESSED;
						default:
							break;
						}

					default:
						break;
					}

					ImGui::ProgressBar(GlobalProgress,
						ImVec2(-FLT_MIN, 0),
						GlobalProgressOverlay.c_str());
					ImGui::ProgressBar(SubFrameProgress,
						ImVec2(-FLT_MIN, 0),
						SubFrameProgressOverlay.c_str());
					ImGui::TextUnformatted(SelectionStateText);
				}
				ImGui::Separator();

				// Analysis and work configuration controls
				{
					ImGui::BeginDisabled(ProgramPhase != STATE_IMAGE_LOADED_WAITING);


					// ImGui::BeginTable("AnalysisConfig", 2, ImGuiTableFlags_BordersInnerH
					// 	| ImGuiTableFlags_BordersInnerV);

					static const char* CodeDiscoveryModePreview = "Select mode";
					static int CodeDiscoveryMode = 0; // 1 based index, 0 uses the highest available
					const char* CodeDiscoverModes[]{
						"Recursive search queue",
						"Enumerate SEH exception table",
						"Enumerate PDB Symbol tables"
					};
					if (ImGui::BeginCombo("Code-Discovery", CodeDiscoveryModePreview)) {
						for (auto i = 0; i < IM_ARRAYSIZE(CodeDiscoverModes); ++i) {

							bool IsSelected = CodeDiscoveryMode - 1 == i;
							if (i != 2 || LoadWithPdb) // Quick shorut cuircuit to exclude pdb search when pdbs arent loaded
								ImGui::Selectable(CodeDiscoverModes[i], &IsSelected);

							if (IsSelected) {

								ImGui::SetItemDefaultFocus();
								CodeDiscoveryModePreview = CodeDiscoverModes[i];
								CodeDiscoveryMode = i + 1;
							}
						}
					
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					sof::HelpMarker("Selects how the engine should search for code, "
						"if non is selected the highest available is chosen by default");

					if (ImGui::Button("Run Analysis")) {

						// Initialalize substation

						auto SOBPassUnqueuedObject = std::make_unique<sof::VisualSOPassObject>(&DataTrackLog);
						SOBPassUnqueuedObject->LoaderObject = LoaderObjectState;

						MainSOBPassObject = SingularityApiController.QueueMainPassProcessObject(
							std::move(SOBPassUnqueuedObject));
						SPDLOG_INFO("Queued SOB processor pass " ESC_BRIGHTRED"{}",
							static_cast<const void*>(MainSOBPassObject));
						ProgramPhase = STATE_IMAGE_PROCESSING_AUTO;
					}

					// ImGui::EndTable();
					ImGui::EndDisabled();
					ImGui::Spacing();

					// Statistics:

					// ImGui::BeginTable("StatisticsData", 2, ImGuiTableFlags_BordersInnerH
					// 	| ImGuiTableFlags_BordersInnerV);
					// ImGui::TextUnformatted("Frames analyzed:");
					// ImGui::TableNextColumn();
					// ImGui::Text("%d / %d", DataTrackLog.NumberOfFramesProcessed.load(),
					// 	DataTrackLog.TotalNumberOfFrames.load());


				}
				ImGui::Separator();



				static bool EnableLogView = true;
				ImGui::Checkbox("Show Logger", &EnableLogView);
				if (EnableLogView)
					VisualLogger->DrawLogWindow(PrimaryViewPort, EnableLogView);
				ImGui::SameLine();
				ImGui::Checkbox("Enable DemoWindow", &EnableImguiDemoWindow);
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
					1000.0f / ImGui::GetIO().Framerate,
					ImGui::GetIO().Framerate);
			}
			ImGui::End();


			


			// Rendering imgui and copying to view port
			ImGui::Render();
			int WindowSizeW, WindowSizeH;
			glfwGetFramebufferSize(PrimaryViewPort, &WindowSizeW, &WindowSizeH);
			glViewport(0, 0, WindowSizeW, WindowSizeH);
			glClearColor(ClearBackgroundColor.x * ClearBackgroundColor.w,
				ClearBackgroundColor.y * ClearBackgroundColor.w,
				ClearBackgroundColor.z * ClearBackgroundColor.w,
				ClearBackgroundColor.w);
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
	}
	catch (const std::exception& Exception) {
		__debugbreak(); // cant do anything here anyways, well not really
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(PrimaryViewPort);
	glfwTerminate();
	spdlog::shutdown();

	return STATUS_SUCCESS;
}
