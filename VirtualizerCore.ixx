// Project core, this implements the combination of all other modules into the virtulaizer controller
//

#include "VirtualizerBase.h"

import ImageHelp;
import ControlFlowGraph;



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
	try {
		xed_tables_init();
		spdlog::set_pattern(SPDLOG_SINGULARITY_SMALL_PATTERN);
		spdlog::set_level(spdlog::level::debug);
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
		ImageHelp TargetImage(argv[1]);

		TargetImage.MapImageIntoMemory();
		TargetImage.RelocateImageToMappedOrOverrideBase();
		
		xed_state_t IntelXedConfiguration;
		xed_state_init2(&IntelXedConfiguration,
			XED_MACHINE_MODE_LONG_64,
			XED_ADDRESS_WIDTH_64b);
		
		CfgGenerator ControlFlowGraphGenerator(TargetImage,
			IntelXedConfiguration);
		



		TargetImage.ReconstructOptionalAndUnmapImage();




	}
	catch (const ImageHelpException& MapperException) {

		SPDLOG_ERROR("ImageHelp failed with [{}] : \"{}\"",
			MapperException.StatusCode,
			MapperException.ExceptionText);
		return STATUS_FAILED_IMAGEHELP;
	}
	catch (const CfgException& CfgException) {

		SPDLOG_ERROR("A CFG tool failed with [{}] : \"{}\"",
			CfgException.StatusCode,
			CfgException.ExceptionText);
		return STATUS_FAILED_IMAGEHELP;
	}
	catch (const std::exception& ExceptionInformation) {

		SPDLOG_ERROR("Primary execution failed with standard exception: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_PROCESSING;
	}

	return STATUS_SUCCESS;
}
