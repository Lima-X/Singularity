// This file describes and implements the base intermediate representation
// and the "decompiler" engine used for translation of x64 code to the IR
//
module;

#include "sof/sof.h"
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <span>
#include <vector>

export module sof.amd64.disasm;
import sof.llir;
import sof.image.load;
export namespace sof {

	// Statistics and issue tracker interface, overload operator() to implement your own captcha
	export class IDisassemblerTracker {
	public:
		enum InformationUpdateType {
			TRACKER_CFGNODE_COUNT,
			TRACKER_OVERLAYING_COUNT,
			TRACKER_INSTRUCTION_COUNT,
			TRACKER_DECODE_ERRORS,
			TRACKER_SPLICED_NODES,
			TRACKER_STRIPPED_NODES,
			TRACKER_HEURISTICS_TRIGGER,


		};
		enum UpdateInformation {
			UPDATE_INCREMENT_COUNTER,
			UPDATE_RESET_COUNTER
		};

		virtual void UpdateTrackerOrAbortCheck(
			IN InformationUpdateType TrackerTpye
		) {
			TRACE_FUNCTION_PROTO;
		}


		virtual void operator()(                  // This function is called from the disassembler engine context, stacktraces can be taken
			IN InformationUpdateType UpdateType,  // The specific counter or tracker entry the disassembler engine wants to modify
			IN UpdateInformation     Operation    // How the caller wants to modify the counter, what it actually does is up to the implementor
			) {
			TRACE_FUNCTION_PROTO;
		}
	};



	// The primary image disassembly engine, this is used to generate a workable,
	// intermediate representation of the existing code.
	// This works (just like a compiler aside from LTO/LTCG) on a function level only,
	// it simply isn't really possible to decompile an image on a whole program level,
	// at least not without writing millions of heuristics and special edge case handlers.
	export class CfgGenerator {
		struct DisasseblerEngineState {                             // Defines the pass down stack state for the recursive disassembly engine
			IN       LlirControlFlowGraph& ControlFlowGraphContext; // Reference to the cfg used by the current engines stack   (passed down)
			IN const FunctionAddress       PredictedFunctionBounds; // Constants of the function frame itself, start and size   (passed down)		
			IN       IDisassemblerTracker& DataTracker;

			struct NextScanAddress {
				IN    byte_t*          NextCodeScanAddress; // Virtual address for the next engine frame to disassemble (passed down)
				IN    LlirBasicBlock*  PreviousNodeForLink; // A pointer to the callers node of its frame of the cfg    (passed down)
															// The callee has to link itself into the passed node
				INOUT LlirBasicBlock** NextFrameHookInLink; // A field for the callee to fill with its node of frame    (shared)
			};
			std::deque<NextScanAddress> NextCodeScanAddressDeque;
		};

	public:
		using FunctionAddress = FunctionAddress;

		CfgGenerator(
			IN const ImageProcessor& ImageMapping,
			IN const xed_state_t&      IntelXedState
		)
			: ImageMapping(ImageMapping),
			  IntelXedState(IntelXedState) {
			TRACE_FUNCTION_PROTO;
			SPDLOG_INFO("Configured control flow graph generator for image at {}",
				static_cast<void*>(ImageMapping.GetImageFileMapping()));
		}

		// -- Previous function comment --
		// Tries to generate a cfg using the underlying disassembler engine,
		// this function serves as a driver to start the disassembler,
		// which then is self reliant, additionally provides a default tracker
		// Presumed function bounds in which the engine should try to decompile
		// A data tracker interface instance, this is used to trace and track
		LlirControlFlowGraph GenerateCfgFromFunction2( // Tries to generate a cfg using the underlying disassembler engine,
															   // this function serves as a driver to start the disassembler,
															   // which then is self reliant, additionally provides a default tracker
			IN const FunctionAddress& PossibleFunction         // Presumed function bounds in which the engine should try to decompile
		) {
			IDisassemblerTracker ThrowAwayTrackerDummy{};
			return GenerateCfgFromFunction2(PossibleFunction,
				ThrowAwayTrackerDummy);
		}

		// Time for some design theory, the short version
		// - recursive descent engine
		// - left (aka fall through) branch is always taken first
		// - the next taken path is also fully analyzed on the left side first
		// - when no fall troughs are found the right path (aka branch) is taken,
		//   and the above process repeats recursively
		// 
		// Due to the above points, 2 states that would result in a fall through entering a discovered node are effectively eliminated,
		// this leaves a single case in which a node is branched to which falls into another node,
		// that was discovered by a previous branching node.
		// In order to eliminate that case, each time a node is discovered through a fall through case
		// and the callstack contains at least a single branching frame,
		// a RTL-DFS (right to left, depth first search) is attempted to locate another node,
		// that has an identical end address as the newly discovered node, 
		// in which case the new node is stripped to the point of the overlaying nodes start address,
		// finally the new node is linked into the graph and the overlaying node is referenced.
		// 
		LlirControlFlowGraph GenerateCfgFromFunction2(         // A deque driven reimplementation of the legacy version,
															   // this avoids huge callstack frames by emulating the recusion with a deque
															   // sacrificing heap memory and simplicity for efficiency and stability
			IN  const FunctionAddress&      PossibleFunction,
			OPT       IDisassemblerTracker& ExternDataTracker  // A data tracker interface instance, this is used to trace and track
		) {
			TRACE_FUNCTION_PROTO;
		
			// Check if the addresses passed are within the configured image
			auto ImageMappingBegin = ImageMapping.GetImageFileMapping();
			auto ImageMappingEnd = ImageMappingBegin +
				ImageMapping.GetImageOptionalHeader().SizeOfImage;
			if (PossibleFunction.first < ImageMappingBegin ||
				PossibleFunction.first +
				PossibleFunction.second > ImageMappingEnd)
				throw SingularityException(
					fmt::format("Passed function [{}:{}], exceeds or is not within the configured module [{}:{}]",
						static_cast<void*>(PossibleFunction.first),
						PossibleFunction.second,
						static_cast<void*>(ImageMappingBegin),
						ImageMappingEnd - ImageMappingBegin),
					SingularityException::STATUS_INDETERMINED_FUNCTION);
			SPDLOG_DEBUG("Function to be analyzed at {}",
				static_cast<void*>(PossibleFunction.first));

			// Setup virtual recursive stack for virtual framing
			LlirControlFlowGraph CurrentCfgContext(ImageMapping,
				PossibleFunction);
			DisasseblerEngineState CfgTraversalStack{
				.ControlFlowGraphContext = CurrentCfgContext,
				.PredictedFunctionBounds = PossibleFunction,
				.DataTracker = ExternDataTracker,
				.NextCodeScanAddressDeque = { { PossibleFunction.first } }
			};

			// Recursion emulation pass, this frame emulates the recursiveness of the function
			// with a deque stack based implementation
			do {

				// This is a immediate jump location in order to directly handle fall through case
				// decode can just continue with a few changes made to CfgTraversalStack
				auto CfgNodeForCurrentFrame = CfgTraversalStack.ControlFlowGraphContext.AllocateFloatingCfgNode();
				if (CfgTraversalStack.NextCodeScanAddressDeque.back().PreviousNodeForLink)
					(*CfgTraversalStack.NextCodeScanAddressDeque.back().NextFrameHookInLink = CfgNodeForCurrentFrame),
						CfgTraversalStack.NextCodeScanAddressDeque.back().PreviousNodeForLink->LinkNodeIntoRemoteNodeInputList(*CfgNodeForCurrentFrame);
				// CfgTraversalStack.NextCodeScanAddressDeque.back().PreviousNodeForLink = nullptr;

				// This is the inner loop, it actually decodes until it cannot fall through anymore
				// Initialize decoder loop and begin recursive descent
				auto VirtualInstructionPointer = CfgTraversalStack.NextCodeScanAddressDeque.back().NextCodeScanAddress;
				CfgTraversalStack.NextCodeScanAddressDeque.pop_back();
				DoDecodeLoopTillNodeEdge(CfgTraversalStack,
					VirtualInstructionPointer,
					CfgNodeForCurrentFrame);

				// Virtual frame endpoint, this "terminates" the current virtually flattened frame and starts a new one if available
				// this is done to avoid issues like having multi megabyte big callstacks and other issues that previously caused issues.
				// This is 100% not the cleanest way to do things, but goto is the smallest price to pay right now

			} while (!CfgTraversalStack.NextCodeScanAddressDeque.empty());

			return CurrentCfgContext;
		}

		// Public modifiers for the generator
		bool AllowJumptableParser = true; // Turn this off incase it causes issues

	private:
		__forceinline void DoDecodeLoopTillNodeEdge(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN byte_t*                 VirtualInstructionPointer,
			IN LlirBasicBlock*         CfgNodeForCurrentFrame
		) {
			TRACE_FUNCTION_PROTO;
			for (;;) {

				// Allocate instruction entry for current cfg node frame
				// and decode current selected opcode with maximum size
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_CFGNODE_COUNT,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
				auto& IrStatementNative = CfgNodeForCurrentFrame->AllocateLlir3acObject();
				IrStatementNative.TypeOfStatement = LlirInstructionStatement::TYPE_PURE_NATIVE;
				xed_decoded_inst_zero_set_mode(&IrStatementNative.NativeEncoding.DecodedInstruction,
					&IntelXedState);
				auto MaxDecodeLength = std::min<size_t>(15,
					static_cast<size_t>((CfgTraversalStack.PredictedFunctionBounds.first +
						CfgTraversalStack.PredictedFunctionBounds.second) -
						VirtualInstructionPointer));
				auto XedDecodeResult = xed_decode(&IrStatementNative.NativeEncoding.DecodedInstruction,
					VirtualInstructionPointer,
					MaxDecodeLength);
				xed_decoded_inst_set_user_data(&IrStatementNative.NativeEncoding.DecodedInstruction,
					reinterpret_cast<uintptr_t&>(VirtualInstructionPointer));

				// Handle xed decode errors and others or continue 
				#define CFGEXCEPTION_FOR_XED_ERROR(ErrorType, StatusCode, ExceptionText, ...)\
					case (ErrorType):\
						throw SingularityException(\
							fmt::format(ExceptionText, __VA_ARGS__),\
							(StatusCode))
				switch (XedDecodeResult) {
				case XED_ERROR_NONE:
					break;
				CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_BUFFER_TOO_SHORT,
					SingularityException::STATUS_CODE_LEAVING_FUNCTION,
					"Intel xed could not read full instruction at {}, possibly invalid function [{}:{}]",
					static_cast<void*>(VirtualInstructionPointer),
					static_cast<void*>(CfgTraversalStack.PredictedFunctionBounds.first),
					CfgTraversalStack.PredictedFunctionBounds.second);

				case XED_ERROR_GENERAL_ERROR:
				case XED_ERROR_INVALID_FOR_CHIP:
				case XED_ERROR_BAD_REGISTER:
				case XED_ERROR_BAD_LOCK_PREFIX:
				case XED_ERROR_BAD_REP_PREFIX:
				case XED_ERROR_BAD_LEGACY_PREFIX:
				case XED_ERROR_BAD_REX_PREFIX:
				case XED_ERROR_BAD_EVEX_UBIT:
				case XED_ERROR_BAD_MAP:
				case XED_ERROR_BAD_EVEX_V_PRIME:
				case XED_ERROR_BAD_EVEX_Z_NO_MASKING:
				case XED_ERROR_BAD_MEMOP_INDEX:
				case XED_ERROR_GATHER_REGS:
				case XED_ERROR_INSTR_TOO_LONG:
				case XED_ERROR_INVALID_MODE:
				case XED_ERROR_BAD_EVEX_LL:
				case XED_ERROR_BAD_REG_MATCH:
					CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_DECODE_ERRORS,
						IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
					CfgNodeForCurrentFrame->SetNodeIsIncomplete();
					SPDLOG_WARN("A bad decode of type {} was raised by xed, state could be invalid",
						xed_error_enum_t2str(XedDecodeResult));
					return;

				// These errors are theoretically impossible to reach
				CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_NO_OUTPUT_POINTER,
					SingularityException::STATUS_XED_NO_MEMORY_BAD_POINTER,
					"The output parameter to the decoded instruction type for xed was null");
				CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_NO_AGEN_CALL_BACK_REGISTERED,
					SingularityException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
					"One of both of xeds AGEN callbacks were missing during decode");
				CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_CALLBACK_PROBLEM,
					SingularityException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
					"Xeds AGEN register or segment callback issued an error during decode");

				default:
					throw SingularityException(
						fmt::format("Singularity's intelxed error handler encountered an unsupported xed error type {}",
							XedDecodeResult),
						SingularityException::STATUS_XED_UNSUPPORTED_ERROR);
				}
				#undef CFGEXCEPTION_FOR_XED_ERROR

				// Xed successfully decoded the instruction, copy the instruction to the cfg entry
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_INSTRUCTION_COUNT,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
				auto InstructionLength = xed_decoded_inst_get_length(&IrStatementNative.NativeEncoding.DecodedInstruction);
				
				// TODO: use formatter here instead of the iclass bullshit value i would have to look up
				{
					auto FormatCallback = [](
						IN  xed_uint64_t  VirtualAddress,
						OUT char*         SymbolBuffer,
						IN  xed_uint32_t  BufferLength,
						OUT xed_uint64_t* SymbolOffset,
						OPT void*         UserContext) -> int32_t {
							TRACE_FUNCTION_PROTO;

							// TODO: get symbols

							return false;
					};
					char XedOutputBuffer[255];
					xed_print_info_t DisasmPrinter{
						.p = &IrStatementNative.NativeEncoding.DecodedInstruction,
						.buf = XedOutputBuffer,
						.blen = 255,
						.runtime_address = reinterpret_cast<uintptr_t&>(VirtualInstructionPointer),
						.disassembly_callback = FormatCallback,
						.context = nullptr,
						.syntax = XED_SYNTAX_INTEL,
						.format_options_valid = false,
					};

					auto FormatStatus = xed_format_generic(&DisasmPrinter);
					auto XedInstrctionClass = xed_decoded_inst_get_iform_enum(&IrStatementNative.NativeEncoding.DecodedInstruction);
					SPDLOG_DEBUG("" ESC_BRIGHTRED"{}" ESC_RESET" : " ESC_BRIGHTMAGENTA"{}" ESC_RESET" | " ESC_BRIGHTBLUE"{}",
						static_cast<void*>(VirtualInstructionPointer),
						XedOutputBuffer,
						xed_iform_enum_t2str(XedInstrctionClass));
				}

				// Determine the type of instruction and with that if we have to specifically treat it
				// there are several special cases such as trailing function calls that all need heuristic checks
				auto InstructionCategory = xed_decoded_inst_get_category(&IrStatementNative.NativeEncoding.DecodedInstruction);
				switch (InstructionCategory) {
				case XED_CATEGORY_RET:

					// Check if engine is in a state that could have caused a fall through into an existing node, 
					// if so we have to strip the current node and ignore handling for its terminator.
					// This piece will reoccur in multiple locations of this handler...
					if (!CheckFallthroughIntoNodeStripExistingAndRebind(CfgTraversalStack,
						*CfgNodeForCurrentFrame)) {

						CfgNodeForCurrentFrame->TerminateThisNodeAsCfgExit();
						SPDLOG_INFO("decoded return instruction");
					} return;

				case XED_CATEGORY_UNCOND_BR:
				case XED_CATEGORY_COND_BR: {

					// Same as in XED_CATEGORY_RET case...
					if (CheckFallthroughIntoNodeStripExistingAndRebind(CfgTraversalStack,
						*CfgNodeForCurrentFrame)) return;
					SPDLOG_INFO("Docoded branching instruction");

					// TODO: heuristics for "emulated call", "trailing function call" / "jump outside",
					//       have to be added here, there are possibly more to improve decoding
					//       Most basic handler that redispatches the disassembly engine in the standard expected use case for jmp
					//       does not support rip relative yet, also slight in efficiencies as there could be 2 path traversals,
					//       currently there is no better way as the addresses checked are not the same.
					auto InstructionForm = xed_decoded_inst_get_iform_enum(&IrStatementNative.NativeEncoding.DecodedInstruction);
					switch (InstructionForm) {
					case XED_IFORM_JMP_FAR_PTRp_IMMw:
					case XED_IFORM_JMP_FAR_MEMp2:
					case XED_IFORM_JMP_MEMv:
						SPDLOG_ERROR("Detected possible jumptable or dynamic dispatch, cannot follow");
						CfgNodeForCurrentFrame->SetNodeIsIncomplete();
						return;

					case XED_IFORM_JMP_GPRv:
						if (HeuristicDetectMsvcX64RvaJumptableCommon(CfgTraversalStack,
							VirtualInstructionPointer,
							CfgNodeForCurrentFrame,
							IrStatementNative.NativeEncoding)) return;
					
						SPDLOG_ERROR("All heuristics for JMP_GPRv failed");
						CfgNodeForCurrentFrame->SetNodeIsIncomplete();
						return;
					}

					// Check if this is a branching or conditionally branching instruction
					HandleRemoteInternodeLongJumpBranch(CfgTraversalStack,
						VirtualInstructionPointer,
						CfgNodeForCurrentFrame,
						IrStatementNative.NativeEncoding);
					if (InstructionCategory == XED_CATEGORY_COND_BR) {

						// This effectively virtually calls this function, as it has adapted the traversal stack for the next call
						// Calculate fall through address then dispatch new disassembly frame
						CfgTraversalStack.NextCodeScanAddressDeque.emplace_back(VirtualInstructionPointer + InstructionLength,
							CfgNodeForCurrentFrame,
							&CfgNodeForCurrentFrame->NonBranchingLeftNode);
					}
				} return;

				case XED_CATEGORY_INTERRUPT:
					if (HeuristicDetectInterrupt29h(CfgTraversalStack,
						CfgNodeForCurrentFrame,
						IrStatementNative.NativeEncoding))
						return;
				}

				// move the disassembly iterator
				VirtualInstructionPointer += InstructionLength;
			}
		}

		bool HeuristicDetectInterrupt29h(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN LlirBasicBlock*         CfgNodeForCurrentFrame,
			IN LlirAmd64NativeEncoded& CfgNodeEntry
		) {
			TRACE_FUNCTION_PROTO;

			// Detect possible function exit points
			// Get Operand iform to detect special interrupt types
			auto InterruptIform = xed_decoded_inst_get_iform_enum(&CfgNodeEntry.DecodedInstruction);
			if (InterruptIform != XED_IFORM_INT_IMMb)
				return false;

			// interrupt has imm, locate imm and get value
			auto XedOperands = xed_decoded_inst_operands_const(&CfgNodeEntry.DecodedInstruction);
			auto Immediate0 = xed_operand_values_get_immediate_uint64(XedOperands);

			// Handle specific immediate
			switch (Immediate0) {
			case 0x29: {

				// RtlFastFail interrupt, treat as function exit

				// The __fastfail interrupt will terminate the node and mark it as a cfg exit point
				// therefore we also have to check possible fallthrough cases via stub
				if (!CheckFallthroughIntoNodeStripExistingAndRebind(CfgTraversalStack,
					*CfgNodeForCurrentFrame))
					CfgNodeForCurrentFrame->TerminateThisNodeAsCfgExit();

				// Track and log, then return
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_HEURISTICS_TRIGGER,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
				SPDLOG_WARN("Located __fastfail interrupt 29h, treating as function exit");
				return true;
			}}

			return false;
		}

		bool HeuristicDetectMsvcX64RvaJumptableCommon(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN byte_t*                 VirtualInstructionPointer,
			IN LlirBasicBlock*         CfgNodeForCurrentFrame,
			IN LlirAmd64NativeEncoded& CfgNodeEntry
		) {
			TRACE_FUNCTION_PROTO;

			// MSVC has a common jump table form/layout based on indices of into a table of rva's into code.
			// It is build in 2 components, first the bounds checks and default / exit case handler,
			// second is the jump table lookup and actual jump command.
			// There may exist mutations, these have to be partially lifted to be interpreted properly,
			// its important to filter out as many wrong interpretations as possible to guarantee working code.
			// The movsxd is always present in some form however its order and location is not defined,
			// the cmp will also always be present but is guaranteed to be in the parents node.
			// Stack and globals may also be used to store parts of dispatch, these need to be followed in order
			// to exclude false positives and tracing independent registers
			// 
			// add    %TO, %LowerBoundOffset%     ; inverse of the smallest case's value
			// ???
			// movsxd %T0, %T0 (OPT location)     ; the input value of the switch
			// ???
			// cmp    %T0, %TableCount%           ; number of entries in the table
			// ja    short %DefaultCase%          ; location of default case / exit
			//
			// movsxd %T0, %T0 (OPT location)     ; the input value of the switch
			// lea    %T2, __ImageBase            ; get module image base address
			// ???
			// mov    %T3, %Table%[%T2 + %T0 * 4] ; get jump table target rva address
			// add    %T3, %T2                    ; calculate absolute address for jump
			// jmp    %T3                         ;

			// Get jmp target register as symbol for %T3
			// auto jmpInstructionInst = xed_decoded_inst_inst(&CfgNodeEntry.DecodedInstruction);
			// auto jmpTargetOperand = xed_inst_operand(jmpInstructionInst, 0);
			// auto jmpTargetRegName = xed_operand_name(jmpTargetOperand);

			auto T3JumpTargetRegister = xed_decoded_inst_get_reg(
				&CfgNodeEntry.DecodedInstruction,
				XED_OPERAND_REG0);
			SPDLOG_DEBUG("Jumptable dispatch register: " ESC_BRIGHTBLUE"{}",
				xed_reg_enum_t2str(T3JumpTargetRegister));

			struct XOperandLocation {
				enum {
					TAG_INVALID_STATE = 0,
					TAG_REGISTER,
					TAG_MEMORYLOC
				} OperandTag;

				union {
					xed_reg_enum_t XRegister;

					struct {
						byte_t* VirtualAddress;
						uint8_t OperandWidth;
					} XMemory;
				};
			};
			enum {
				SEARCH_FIND_ABSOLUTE,
				SEARCH_LOAD_TABLE_RVA,
				SEARCH_LOAD_IMAGEBASE,
				SEARCH_FIND_RVA_BOUNDS,
				DISPATCH_COMPLETE_PROC,
				DISPATCH_COMPLETE_ERROR,
			} DispatchFrameState = SEARCH_FIND_ABSOLUTE;

			xed_reg_enum_t T2ImagebaseRegister = XED_REG_INVALID; // just like T3 this will also always be a register

			XOperandLocation T0RvaTableSize{};
			int64_t        BondsCheckVal = 0;
			rva_t*         RvaTableLocation = nullptr;
			auto           StartIndexOfIndex = 0;

			auto OriginalCfgNodeForFrame = CfgNodeForCurrentFrame;
			do {
				size_t NumberOfDecodes = 0;

				if (DispatchFrameState == SEARCH_FIND_RVA_BOUNDS &&
					OriginalCfgNodeForFrame == CfgNodeForCurrentFrame) {

					// we are reentering assuming the movsxd check may have failed, so we dispatch the second level loop
					CfgNodeForCurrentFrame = CfgNodeForCurrentFrame->InputFlowNodeLinks[0];
					NumberOfDecodes = CfgNodeForCurrentFrame->EncodeHybridStream.size();
					SPDLOG_DEBUG("Locating bounds checks in parent node " ESC_BRIGHTRED"{}" ESC_RESET" of " ESC_BRIGHTRED"{}",
						static_cast<void*>(CfgNodeForCurrentFrame),
						static_cast<void*>(OriginalCfgNodeForFrame));
				}

				NumberOfDecodes = CfgNodeForCurrentFrame->EncodeHybridStream.size();
				for (int32_t i = NumberOfDecodes - 2; i > -1; --i) {

					auto CurrentInstrructionStruct = &CfgNodeForCurrentFrame->EncodeHybridStream[i].NativeEncoding.DecodedInstruction;
					auto CurrentInstructionForm = xed_decoded_inst_get_iform_enum(
						CurrentInstrructionStruct);
					switch (DispatchFrameState) {
					case SEARCH_FIND_ABSOLUTE: {

						switch (CurrentInstructionForm) {
						case XED_IFORM_ADD_GPRv_GPRv_01:
						case XED_IFORM_ADD_GPRv_GPRv_03:

							// Check if target register is jump target
							if (xed_decoded_inst_get_reg(CurrentInstrructionStruct,
								XED_OPERAND_REG0) != T3JumpTargetRegister)
								break;

							T2ImagebaseRegister = xed_decoded_inst_get_reg(CurrentInstrructionStruct,
								XED_OPERAND_REG1);
							DispatchFrameState = SEARCH_LOAD_TABLE_RVA;
							SPDLOG_DEBUG("Predicted imagebase register " ESC_BRIGHTBLUE"{}",
								xed_reg_enum_t2str(T2ImagebaseRegister));
							break;

						default:
							break;
						}
						break;
					}
					case SEARCH_LOAD_TABLE_RVA: {

						switch (CurrentInstructionForm) {
						case XED_IFORM_MOV_GPRv_MEMv:

							// Check if target register is jump target
							if (xed_get_largest_enclosing_register(
									xed_decoded_inst_get_reg(CurrentInstrructionStruct,
										XED_OPERAND_REG0)) != T3JumpTargetRegister)
								break;
							SPDLOG_DEBUG("Verified register " ESC_BRIGHTBLUE"{}" ESC_RESET" for dispatch",
								xed_reg_enum_t2str(T3JumpTargetRegister));
							if (xed_decoded_inst_get_base_reg(CurrentInstrructionStruct, 0) != T2ImagebaseRegister)
								break; // not the right base register

							// If index exists the scale must match 4 in oder to prove its a rva table
							T0RvaTableSize.OperandTag = XOperandLocation::TAG_REGISTER;
							T0RvaTableSize.XRegister = xed_decoded_inst_get_index_reg(CurrentInstrructionStruct, 0);
							if (T0RvaTableSize.XRegister == XED_REG_INVALID)
								break;
							if (xed_decoded_inst_get_scale(CurrentInstrructionStruct, 0) != 4)
								break;
							SPDLOG_DEBUG("Predicted jump index reg " ESC_BRIGHTBLUE"{}",
								xed_reg_enum_t2str(T0RvaTableSize.XRegister));
							StartIndexOfIndex = i; // Save the iterator index, this is needed for the second pass

							// Locate rva tables location (T2 + displacement)
							if (!xed_operand_values_has_memory_displacement(CurrentInstrructionStruct))
								break;
							RvaTableLocation = reinterpret_cast<rva_t*>(ImageMapping.GetImageFileMapping() +
								xed_decoded_inst_get_memory_displacement(CurrentInstrructionStruct, 0));
							DispatchFrameState = SEARCH_LOAD_IMAGEBASE; // Correct instruction we have the base index register to follow
							SPDLOG_DEBUG("Predicting jumptabled location at " ESC_BRIGHTRED"{}",
								static_cast<void*>(RvaTableLocation));
							break;

						default:
							break;
						}
						break;
					}
					case SEARCH_LOAD_IMAGEBASE: {

						if (CurrentInstructionForm != XED_IFORM_LEA_GPRv_AGEN)
							break;

						// Check target register is T2
						if (xed_decoded_inst_get_reg(CurrentInstrructionStruct,
							XED_OPERAND_REG0) != T2ImagebaseRegister)
							break;
						// reg will be rip
						// if (xed_decoded_inst_get_base_reg(CurrentInstrructionStruct, 0) != XED_REG_INVALID)
						// 	break; // not the required agen, we need [rip + disp32]

						// Calculate image base and check that we end up at the dos header
						auto Displacment = xed_decoded_inst_get_memory_displacement(CurrentInstrructionStruct, 0);
						auto Differential = reinterpret_cast<byte_t*>(xed_decoded_inst_get_user_data(CurrentInstrructionStruct)) +
							xed_decoded_inst_get_length(CurrentInstrructionStruct) -
							ImageMapping.GetImageFileMapping() + Displacment;
						if (Differential != 0)
							break;

						// Enter second level pass, and track index register to bounds check
						DispatchFrameState = SEARCH_FIND_RVA_BOUNDS;
						i = StartIndexOfIndex - 1;
						SPDLOG_DEBUG("Verified register " ESC_BRIGHTBLUE"{}" ESC_RESET" as imagebase container",
							xed_reg_enum_t2str(T2ImagebaseRegister));
						break;
					}
					
					case SEARCH_FIND_RVA_BOUNDS: {

						switch (CurrentInstructionForm) {
						case XED_IFORM_CMP_GPRv_IMMb:
						case XED_IFORM_CMP_AL_IMMb:
						case XED_IFORM_CMP_GPRv_IMMz:
						case XED_IFORM_CMP_OrAX_IMMz:

							// Check that we are in the parent node and the target registers match
							if (OriginalCfgNodeForFrame == CfgNodeForCurrentFrame)
								break;
							if (T0RvaTableSize.OperandTag != XOperandLocation::TAG_REGISTER)
								break;
							if (xed_get_largest_enclosing_register(
									xed_decoded_inst_get_reg(CurrentInstrructionStruct,
										XED_OPERAND_REG0)) != T0RvaTableSize.XRegister)
											break;
							SPDLOG_DEBUG("Confirmed register " ESC_BRIGHTBLUE"{}" ESC_RESET" for index",
								xed_reg_enum_t2str(T0RvaTableSize.XRegister));
							if (!xed_decoded_inst_get_immediate_is_signed(CurrentInstrructionStruct))
								break;

							BondsCheckVal = xed_decoded_inst_get_signed_immediate(CurrentInstrructionStruct) + 1;
							DispatchFrameState = DISPATCH_COMPLETE_PROC;
							SPDLOG_DEBUG("Calculated rva table at " ESC_BRIGHTRED"{}" ESC_RESET " with "ESC_BRIGHTGREEN"{}" ESC_RESET" entries",
								static_cast<void*>(RvaTableLocation),
								BondsCheckVal);
							break;

						case XED_IFORM_CMP_MEMb_IMMb_80r7:
						case XED_IFORM_CMP_MEMb_IMMb_82r7:

							// Similar checks as above
							if (OriginalCfgNodeForFrame == CfgNodeForCurrentFrame)
								break;
							if (T0RvaTableSize.OperandTag != XOperandLocation::TAG_MEMORYLOC)
								break;




						// All these are up to the future me to implement, rip me
						case XED_IFORM_CMP_GPR8_GPR8_38:
						case XED_IFORM_CMP_GPR8_GPR8_3A:
						case XED_IFORM_CMP_GPR8_IMMb_80r7:
						case XED_IFORM_CMP_GPR8_IMMb_82r7:
						case XED_IFORM_CMP_GPR8_MEMb:
						case XED_IFORM_CMP_GPRv_GPRv_39:
						case XED_IFORM_CMP_GPRv_GPRv_3B:
						case XED_IFORM_CMP_GPRv_MEMv:
						case XED_IFORM_CMP_MEMb_GPR8:
						case XED_IFORM_CMP_MEMv_GPRv:
						case XED_IFORM_CMP_MEMv_IMMb:
						case XED_IFORM_CMP_MEMv_IMMz:
							break;

						case XED_IFORM_MOVSXD_GPRv_GPRz:
						case XED_IFORM_CDQE:

							// Check that the current storage is within
							if (T0RvaTableSize.OperandTag != XOperandLocation::TAG_REGISTER)
								break;
							if (xed_decoded_inst_get_reg(CurrentInstrructionStruct,
								XED_OPERAND_REG0) != T0RvaTableSize.XRegister)
								break;
							T0RvaTableSize.XRegister = xed_decoded_inst_get_reg(CurrentInstrructionStruct,
								XED_OPERAND_REG1);

							SPDLOG_DEBUG("Predicting register " ESC_BRIGHTBLUE"{}" ESC_RESET" as pure index",
								xed_reg_enum_t2str(T0RvaTableSize.XRegister));
							break;

						case XED_IFORM_MOVSXD_GPRv_MEMz:
						{

							// Check that the current storage is within
							if (T0RvaTableSize.OperandTag != XOperandLocation::TAG_REGISTER)
								break;
							if (xed_decoded_inst_get_reg(CurrentInstrructionStruct,
								XED_OPERAND_REG0) != T0RvaTableSize.XRegister)
								break;

							// calculate memory address using intelxed agen
							struct AgenCallbackContext {
								uint64_t RIP;
								uint64_t RSP;
							};
							auto XedAgenCallback = [](
								IN  xed_reg_enum_t reg,
								OPT void* context,
								OUT xed_bool_t* error) -> xed_uint64_t {
									TRACE_FUNCTION_PROTO;

									auto AgenContext = static_cast<AgenCallbackContext*>(context);
									switch (reg) {
									case XED_REG_CS:
									case XED_REG_DS:
									case XED_REG_ES:
									case XED_REG_SS:
										return NULL;

									case XED_REG_RIP:
										return AgenContext->RIP;
									case XED_REG_RSP:
										return AgenContext->RSP;

									default:
										*error = true;
										return NULL;
									}
							};
							xed_agen_register_callback(XedAgenCallback, XedAgenCallback);
							AgenCallbackContext AgenContext{ xed_decoded_inst_get_user_data(CurrentInstrructionStruct) +
								xed_decoded_inst_get_length(CurrentInstrructionStruct),
								NULL };
							uint64_t AgenResult = NULL;
							auto AgenStatus = xed_agen(CurrentInstrructionStruct,
								0,
								static_cast<void*>(&AgenContext),
								&AgenResult);

							if (AgenStatus != XED_ERROR_NONE) {

								// Error occured in agen, log and exit cause our register was associated with this location
								DispatchFrameState = DISPATCH_COMPLETE_ERROR;
								i = -1;
								SPDLOG_ERROR("Xed Agen failed with [" ESC_BRIGHTRED"{}" ESC_RESET"] at rip=" ESC_BRIGHTRED"{}",
									xed_error_enum_t2str(AgenStatus),
									reinterpret_cast<void*>(xed_decoded_inst_get_user_data(CurrentInstrructionStruct)));
								break;
							}

							// re associate memory location to index reg
							auto OrignalIndexRegister = T0RvaTableSize.XRegister;
							T0RvaTableSize.OperandTag = XOperandLocation::TAG_MEMORYLOC;
							T0RvaTableSize.XMemory.OperandWidth = 32;
							T0RvaTableSize.XMemory.VirtualAddress = reinterpret_cast<byte_t*>(AgenResult);
							SPDLOG_DEBUG("Reassociated index-reg " ESC_BRIGHTBLUE"{}" ESC_RESET" to stacklocation " ESC_BRIGHTRED"{}",
								xed_reg_enum_t2str(OrignalIndexRegister),
								fmt::ptr(T0RvaTableSize.XMemory.VirtualAddress));
							break;
						}
						default:
							break;
						}
						break;
					}
					default:
						break;
					}
				}
			} while (DispatchFrameState == SEARCH_FIND_RVA_BOUNDS);

			if (DispatchFrameState != DISPATCH_COMPLETE_PROC) {

				SPDLOG_WARN("Could not resolve jumptable, continuing to resolve tree partially");
				return false;
			}

			// We have all the variables we need, we can enque all the new jump locations into the cfgr processor stack
			OriginalCfgNodeForFrame->BranchingOutRightNode.reserve(
				OriginalCfgNodeForFrame->BranchingOutRightNode.size() + BondsCheckVal);
			for (auto i = 0; i < BondsCheckVal; ++i)
				CfgTraversalStack.NextCodeScanAddressDeque.emplace_back(
					ImageMapping.GetImageFileMapping() + RvaTableLocation[i],
					OriginalCfgNodeForFrame,
					&OriginalCfgNodeForFrame->BranchingOutRightNode.emplace_back());

			return true;
		}


	
		__forceinline void HandleRemoteInternodeLongJumpBranch(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN byte_t*                 VirtualInstructionPointer,
			IN LlirBasicBlock*         CfgNodeForCurrentFrame,
			IN LlirAmd64NativeEncoded& CfgNodeEntry
		) {
			TRACE_FUNCTION_PROTO;

			// Calculate target jump location and reconfigure current traversal stack
			auto BranchDisplacement = xed_decoded_inst_get_branch_displacement(&CfgNodeEntry.DecodedInstruction);
			auto LenghtOfInstruction = xed_decoded_inst_get_length(&CfgNodeEntry.DecodedInstruction);
			auto VirtualBranchLocation = VirtualInstructionPointer +
				LenghtOfInstruction + BranchDisplacement;

			// Attempt to find an colliding node with the new address, if found the type of collision has to be determined
			do {
				auto OptionalPossibleCollidingNode = CfgTraversalStack.ControlFlowGraphContext.
					FindNodeContainingVirtualAddress(LlirControlFlowGraph::SEARCH_REVERSE_PREORDER_NRL,
						VirtualBranchLocation);

				// Check if a colliding node was found and if their heads match, aka identical node
				if (OptionalPossibleCollidingNode &&
					OptionalPossibleCollidingNode->GetPhysicalNodeStartAddress() == VirtualBranchLocation) {

					// We need to link our node into the remote node and skip
					SPDLOG_DEBUG("A node for said jump location already exists, hooking in"); // TODO: proper
					CfgNodeForCurrentFrame->LinkNodeIntoRemoteNodeInputList(*OptionalPossibleCollidingNode);
					CfgNodeForCurrentFrame->BranchingOutRightNode.push_back(OptionalPossibleCollidingNode);
					break;
				}

				// If the above did not execute, but a node was found, we have a collision
				if (OptionalPossibleCollidingNode) {

					SPDLOG_DEBUG("A colliding node for said jump was found, fixing up graph");

					// Need to check if the new address overlays with any of the registered decodes,
					// if true, the node needs to be spliced, otherwise we have an overlaying assembly.
					// NOTE: Overlaying assembly is not yet supported it will throw an exception!
					auto Inserted = OptionalPossibleCollidingNode->TrySpliceOfHeadAtAddressForInsert2(VirtualBranchLocation);
					if (Inserted) {

						// Track data and link into spliced node
						CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_SPLICED_NODES,
							IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
						CfgNodeForCurrentFrame->LinkNodeIntoRemoteNodeInputList(*OptionalPossibleCollidingNode);
						CfgNodeForCurrentFrame->BranchingOutRightNode.push_back(OptionalPossibleCollidingNode);
						break;
					}

					// Execution continues here if overlaying assembly was found
					CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_OVERLAYING_COUNT,
						IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
					throw SingularityException("Overlaying assembly was found, not yet supported, fatal",
						SingularityException::STATUS_OVERLAYING_CODE_DETECTED);
				}

				// No node was found, the address we received is unknown, we can now reinvoke us and discover it
				CfgTraversalStack.NextCodeScanAddressDeque.emplace_back(VirtualBranchLocation,
					CfgNodeForCurrentFrame,
					&CfgNodeForCurrentFrame->BranchingOutRightNode.emplace_back());
			} while (0);
		}



		bool CheckFallthroughIntoNodeStripExistingAndRebind(     // Checks if the selected node is falling into an existing node
																 // and eliminates overlaying identical code (assumptions),
																 // returns true if rebound, otherwise false to indicate no work
			IN DisasseblerEngineState& CfgTraversalStack,     // The stack location of the disassembly engine for internal use
			IN LlirBasicBlock& CfgNodeForCurrentFrame // The node to check for overlays and stripping/rebinding
		) {
			TRACE_FUNCTION_PROTO;

			// Potentially fell into a existing node already, verify and or strip and rebind,
			// find possibly overlaying node and check
			auto NodeTerminatorLocation = CfgNodeForCurrentFrame.EncodeHybridStream.back().GetUniquePhysicalAddress();
			const LlirBasicBlock* CfgNodeExclusions[1]{ &CfgNodeForCurrentFrame };
			auto OptionalCollidingNode = CfgTraversalStack.ControlFlowGraphContext.FindNodeContainingVirtualAddressAndExclude(
				LlirControlFlowGraph::SEARCH_REVERSE_PREORDER_NRL,
				NodeTerminatorLocation,
				CfgNodeExclusions);
			if (OptionalCollidingNode) {

				// Found node gotta strip it and relink, first find the location of where they touch
				auto StartAddressOfNode = OptionalCollidingNode->GetPhysicalNodeStartAddress();
				auto TouchIterator = std::find_if(CfgNodeForCurrentFrame.EncodeHybridStream.begin(),
					CfgNodeForCurrentFrame.EncodeHybridStream.end(),
					[StartAddressOfNode](
						IN const LlirInstructionStatement& DecodeEntry
						) -> bool {
							TRACE_FUNCTION_PROTO;
							return DecodeEntry.GetUniquePhysicalAddress() == StartAddressOfNode;
					});
				if (TouchIterator == CfgNodeForCurrentFrame.EncodeHybridStream.end()) {

					// Overlaying code somehow, track and throw as exception
					CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_OVERLAYING_COUNT,
						IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
					throw SingularityException("An identical terminator address was matched but no matching overlay found",
						SingularityException::STATUS_OVERLAYING_CODE_DETECTED);
				}

				// Rebind nodes, first erase overlaying data then merge links
				SPDLOG_INFO("Found fallthrough into existing node, fixing graph");
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_STRIPPED_NODES,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
				CfgNodeForCurrentFrame.EncodeHybridStream.erase(TouchIterator, 
					CfgNodeForCurrentFrame.EncodeHybridStream.end());
				CfgNodeForCurrentFrame.NonBranchingLeftNode = OptionalCollidingNode;
				CfgNodeForCurrentFrame.LinkNodeIntoRemoteNodeInputList(*OptionalCollidingNode);

				// Security checks
				if (CfgNodeForCurrentFrame.EncodeHybridStream.empty())
					throw SingularityException(
						fmt::format("Stripping of node {} cannot result in empty node",
							static_cast<void*>(&CfgNodeForCurrentFrame)),
						SingularityException::STATUS_STRIPPING_FAILED_EMPTY_NODE);
			}
			
			return OptionalCollidingNode;
		}


		const ImageProcessor& ImageMapping;
		const xed_state_t       IntelXedState;
	};
}
