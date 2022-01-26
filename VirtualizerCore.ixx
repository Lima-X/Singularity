// Project core, this implements the combination of all other modules into the virtulaizer controller
//

#include "VirtualizerBase.h"
#include <chrono>

import StatisticsTracker;
import ImageHelp;
import ControlFlowGraph;

using namespace std::chrono;


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
		// Test agen
		byte_t instruction[]{ "\xe8\x04\x44\x00\x00" };
		xed_decoded_inst_t Instruction;
		xed_decoded_inst_zero_set_mode(&Instruction, &IntelXedConfiguration);
		auto decode_result = xed_decode(&Instruction, instruction, 5);

		// manually decode
		auto displacment = xed_decoded_inst_get_branch_displacement(&Instruction);


		// register agen callbacks
#if 0
		auto registercallack = [](
			IN xed_reg_enum_t reg,
			INOUT void* context,
			OUT xed_bool_t* error
			) -> xed_uint64_t {

				if (reg == XED_REG_RIP)
					return reinterpret_cast<xed_uint64_t>(context);
				return 0;
		};
		auto segmentcallback = [](
			IN xed_reg_enum_t reg,
			INOUT void* context,
			OUT xed_bool_t* error)
			-> xed_uint64_t {

			return 0;
		};
		xed_agen_register_callback(registercallack,
			segmentcallback);


		uintptr_t outaddress;
		auto agen_result = xed_agen(&Instruction, 0, nullptr, &outaddress);
			 agen_result = xed_agen(&Instruction, 1, nullptr, &outaddress);
	
		__debugbreak();
#endif

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
