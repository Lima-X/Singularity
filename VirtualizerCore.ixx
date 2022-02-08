// Project core, this implements the combination of all other modules into the virtulaizer controller
//

#include "VirtualizerBase.h"
#include <chrono>
#include <ranges>
#include <span>
#include <variant>
#include <functional>

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
	ConsoleModifier ScopedConsole;
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
		xed_state_init2(&IntelXedConfiguration,
			XED_MACHINE_MODE_LONG_64,
			XED_ADDRESS_WIDTH_64b);

		// Initialize and configure spdlog/fmt
		spdlog::set_pattern(SPDLOG_SINGULARITY_SMALL_PATTERN);
		spdlog::set_level(spdlog::level::debug);

		// Configure windows terminal console


		// Configure program options
		// Configuration.AddArgumentToList()

	}
	catch (const spdlog::spdlog_ex& ExceptionInformation) {

		SPDLOG_ERROR("SpdLog failed to initilizer with: {}",
			ExceptionInformation.what());
		return STATUS_FAILED_INITILIZATION;
	}

	// Define and parse program options (TODO: do it)
	ProgramOptions Configuration;
	{
		// Parse arguments (for now we just check for the file name and try to open it)
		if (argc != 2) {

			SPDLOG_ERROR("Not enough or too many arguments supplied, call like \"Singularity.exe TargetExecutable\"");
			return STATUS_FAILED_ARGUMENTS;
		}
	}

	// Primary processing closure
	try {
		StatisticsTracker ProfilerSummary(64ms);
		
		constexpr auto FileName = ".\\ntoskrnl.exe";
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
			
			auto SymbolOfFunction = SymbolServer.FindSymbolForAddress(
				RuntimeFunction.BeginAddress + TargetImage.GetImageFileMapping());
			auto SymbolAddresesAddress = SymbolServer.FindAddressForSymbol(
				SymbolOfFunction);



			try {

				auto GraphForRuntimeFunction = ControlFlowGraphGenerator.GenerateControlFlowGraphFromFunction(
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


	return STATUS_SUCCESS;
}
