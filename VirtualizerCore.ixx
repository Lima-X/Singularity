// Project core, this implements the combination of all other modules into the virtulaizer controller
//

#include "VirtualizerBase.h"
#include <chrono>

import StatisticsTracker;
import ImageHelp;
import DecompilerEngine;

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
	xed_state_t IntelXedConfiguration;
	ConsoleModifier ScopedConsole();
	try {
		// Initialize xed's tables and configure encoder, decoder
		xed_tables_init();
		xed_state_init2(&IntelXedConfiguration,
			XED_MACHINE_MODE_LONG_64,
			XED_ADDRESS_WIDTH_64b);

		// Initialize and configure spdlog/fmt
		spdlog::set_pattern(SPDLOG_SINGULARITY_SMALL_PATTERN);
		spdlog::set_level(spdlog::level::debug);

		// Configure windows terminal console


	}
	catch (const spdlog::spdlog_ex& ExceptionInformation) {

		SPDLOG_ERROR("SpdLog failed to initilizer with: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_INITILIZATION;
	}
	
	// Parse arguments (for now we just check for the file name and try to open it)
	if (argc != 2) {

		SPDLOG_ERROR("Not enough or too many arguments supplied, call like \"Singularity.exe TargetExecutable\"");
		return STATUS_FAILED_ARGUMENTS;
	}
	
	// Primary processing closure
	try {
		StatisticsTracker ProfilerSummary(64ms);

		ImageHelp TargetImage(argv[1]);

		TargetImage.MapImageIntoMemory();
		TargetImage.RelocateImageToMappedOrOverrideBase();
		
		CfgGenerator ControlFlowGraphGenerator(TargetImage,
			IntelXedConfiguration);
		
		// Enumerate all function entries here
		auto RuntimeFunctionsView = TargetImage.GetRuntimeFunctionTable();
		for (const RUNTIME_FUNCTION& RuntimeFunction : RuntimeFunctionsView) {

			// Generate Cfg from function
			auto GraphForRuntimeFunction = ControlFlowGraphGenerator.GenerateControlFlowGraphFromFunction(
				FunctionAddress(
					RuntimeFunction.BeginAddress + TargetImage.GetImageFileMapping(),
					RuntimeFunction.EndAddress - RuntimeFunction.BeginAddress));

		}

		TargetImage.ReconstructOptionalAndUnmapImage();




	}
	catch (const ImageHelpException& MapperException) {

		SPDLOG_ERROR("ImageHelp failed with [{}] : \"{}\"",
			MapperException.StatusCode,
			MapperException.ExceptionText);
		return STATUS_FAILED_IMAGEHELP;
	}
	catch (const CfgException& GraphException) {

		SPDLOG_ERROR("A CFG tool failed with [{}] : \"{}\"",
			GraphException.StatusCode,
			GraphException.ExceptionText);
		return STATUS_FAILED_CFGTOOLS;
	}
	catch (const std::exception& ExceptionInformation) {

		SPDLOG_ERROR("Primary execution failed with standard exception: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_PROCESSING;
	}


	return STATUS_SUCCESS;
}
