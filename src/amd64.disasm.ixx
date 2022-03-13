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
		struct DisasseblerEngineState {                       // Defines the pass down stack state for the recursive disassembly engine
			IN LlirControlFlowGraph&     ControlFlowGraphContext; // Reference to the cfg used by the current engines stack   (passed down)
			IN const FunctionAddress PredictedFunctionBounds; // Constants of the function frame itself, start and size   (passed down)		
			IN IDisassemblerTracker& DataTracker;

			struct NextScanAddress {
				IN    byte_t*                     NextCodeScanAddress; // Virtual address for the next engine frame to disassemble (passed down)
				IN    LlirControlFlowGraph::CfgNode*  PreviousNodeForLink; // A pointer to the callers node of its frame of the cfg    (passed down)
																	   // The callee has to link itself into the passed node
				INOUT LlirControlFlowGraph::CfgNode** NextFrameHookInLink; // A field for the callee to fill with its node of frame    (shared)
			};
			std::deque<NextScanAddress> NextCodeScanAddressDeque;
		};

	public:
		using FunctionAddress = FunctionAddress;

		CfgGenerator(
			IN const IImageLoaderHelp& ImageMapping,
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
			IN DisasseblerEngineState&    CfgTraversalStack,
			IN byte_t*                    VirtualInstructionPointer,
			IN LlirControlFlowGraph::CfgNode* CfgNodeForCurrentFrame
		) {
			TRACE_FUNCTION_PROTO;
			for (;;) {

				// Allocate instruction entry for current cfg node frame
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_CFGNODE_COUNT,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);

				LlirAmd64NativeEncoded* CfgNodeEntry = new LlirAmd64NativeEncoded(
					CfgTraversalStack.ControlFlowGraphContext.GenNextSymbolIdentifier());
				xed_decoded_inst_zero_set_mode(&CfgNodeEntry.DecodedInstruction,
					&IntelXedState);
				CfgNodeEntry.OriginalAddress = VirtualInstructionPointer;

				// Decode current selected opcode with maximum size
				auto MaxDecodeLength = std::min<size_t>(15,
					static_cast<size_t>((CfgTraversalStack.PredictedFunctionBounds.first +
						CfgTraversalStack.PredictedFunctionBounds.second) -
						VirtualInstructionPointer));
				auto XedDecodeResult = xed_decode(&CfgNodeEntry.DecodedInstruction,
					VirtualInstructionPointer,
					MaxDecodeLength);

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
				auto InstructionLength = xed_decoded_inst_get_length(&CfgNodeEntry.DecodedInstruction);
				memcpy(&CfgNodeEntry.InstructionText,
					VirtualInstructionPointer,
					InstructionLength);
				memset(CfgNodeEntry.InstructionText + InstructionLength,
					0,
					15 - InstructionLength);

				// TODO: use formatter here instead of the iclass bullshit value i would have to look up
				{
					auto XedInstrctionClass = xed_decoded_inst_get_iform_enum(&CfgNodeEntry.DecodedInstruction);
					SPDLOG_DEBUG("rip={} : {}",
						static_cast<void*>(VirtualInstructionPointer),
						xed_iform_enum_t2str(XedInstrctionClass));
				}

				// Determine the type of instruction and with that if we have to specifically treat it
				// there are several special cases such as trailing function calls that all need heuristic checks
				auto InstructionCategory = xed_decoded_inst_get_category(&CfgNodeEntry.DecodedInstruction);
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
					auto InstructionForm = xed_decoded_inst_get_iform_enum(&CfgNodeEntry.DecodedInstruction);
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
							CfgNodeEntry)) return;
					
						SPDLOG_ERROR("All heuristics for JMP_GPRv failed");
						CfgNodeForCurrentFrame->SetNodeIsIncomplete();
						return;
					}


					// Check if this is a branching or conditionally branching instruction
					HandleRemoteInternodeLongJumpBranch(CfgTraversalStack,
						VirtualInstructionPointer,
						CfgNodeForCurrentFrame,
						CfgNodeEntry);
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
						CfgNodeEntry))
						return;
				}

				// move the disassembly iterator
				VirtualInstructionPointer += InstructionLength;
			}
		}

		__forceinline bool HeuristicDetectInterrupt29h(
			IN DisasseblerEngineState&     CfgTraversalStack,
			IN LlirControlFlowGraph::CfgNode*  CfgNodeForCurrentFrame,
			IN LlirControlFlowGraph::CfgEntry& CfgNodeEntry
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

		__forceinline bool HeuristicDetectMsvcX64RvaJumptableCommon(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN byte_t* VirtualInstructionPointer,
			IN LlirControlFlowGraph::CfgNode* CfgNodeForCurrentFrame,
			IN LlirControlFlowGraph::CfgEntry& CfgNodeEntry
		) {

			// MSVC has a common jumptable form/layout based on indices of into a table of rva's into code.
			// It is build in 2 components, first the bounds checks and default / exit case handler,
			// second is the jumptable lookup and actual jump command.
			// There may exist mutations, these have to be partially lifted to be interpreted properly,
			// its important to filter out as many wrong interpretations as possible to guarantee working code
			// 
			// add    %TO, %LowerBoundOffset%   ; inverse of the smallest case's value
			// cmp    %T0, %TableCount%         ; number of entries in the table
			// ja    short %DefaultCase%        ; location of default case / exit
			//
			// movsxd %T2, %T0                  ; the input value of the switch
			// lea    %T1, __ImageBase          ; get module image base address
			// mov    %T3, %Table%[%T2 + %T1*4] ; get jump table target rva address
			// add    %T3, %T2                  ; calculate absolute address for jump
			// jmp    %T3                       ;

			// Get jmp target register as symbol for %T3
			// auto jmpInstructionInst = xed_decoded_inst_inst(&CfgNodeEntry.DecodedInstruction);
			// auto jmpTargetOperand = xed_inst_operand(jmpInstructionInst, 0);
			// auto jmpTargetRegName = xed_operand_name(jmpTargetOperand);
			auto T3JumpTargetRegister = xed_decoded_inst_get_reg(
				&CfgNodeEntry.DecodedInstruction,
				XED_OPERAND_REG0);


			// Reverse walk up and find T2 "add" and T2
			auto NumberOfDecodes = CfgNodeForCurrentFrame->size();
			for (auto i = NumberOfDecodes - 2; i > -1; --i) {

				auto T2LocatorClass = xed_decoded_inst_get_iform_enum(&CfgNodeForCurrentFrame->at(i).DecodedInstruction);
				switch (T2LocatorClass) {
				case XED_IFORM_ADD_GPRv_GPRv_01:

					// Handle association shit

					goto T2ImagebaseAccumilationFound;
				}
			}

			SPDLOG_WARN("Could not resolve jumptable, continuing to resolve tree partially");
			return false;

		T2ImagebaseAccumilationFound:

			return false;
		}


	
		__forceinline void HandleRemoteInternodeLongJumpBranch(
			IN DisasseblerEngineState& CfgTraversalStack,
			IN byte_t*                 VirtualInstructionPointer,
			IN LlirControlFlowGraph::CfgNode* CfgNodeForCurrentFrame,
			IN LlirControlFlowGraph::CfgEntry& CfgNodeEntry
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
					auto Inserted = OptionalPossibleCollidingNode->TrySpliceOfHeadAtAddressForInsert(VirtualBranchLocation);
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
			IN LlirControlFlowGraph::CfgNode& CfgNodeForCurrentFrame // The node to check for overlays and stripping/rebinding
		) {
			TRACE_FUNCTION_PROTO;

			// Potentially fell into a existing node already, verify and or strip and rebind,
			// find possibly overlaying node and check
			auto NodeTerminatorLocation = CfgNodeForCurrentFrame.back().OriginalAddress;
			std::array<const LlirControlFlowGraph::CfgNode*, 1> CfgNodeExclusions{ &CfgNodeForCurrentFrame };
			auto OptionalCollidingNode = CfgTraversalStack.ControlFlowGraphContext.FindNodeContainingVirtualAddressAndExclude(
				LlirControlFlowGraph::SEARCH_REVERSE_PREORDER_NRL,
				NodeTerminatorLocation,
				std::span{ CfgNodeExclusions });
			if (OptionalCollidingNode) {

				// Found node gotta strip it and relink, first find the location of where they touch
				auto StartAddressOfNode = OptionalCollidingNode->front().OriginalAddress;
				auto TouchIterator = std::find_if(CfgNodeForCurrentFrame.begin(),
					CfgNodeForCurrentFrame.end(),
					[StartAddressOfNode](
						IN const LlirControlFlowGraph::CfgEntry& DecodeEntry
						) -> bool {
							TRACE_FUNCTION_PROTO;
							return DecodeEntry.OriginalAddress == StartAddressOfNode;
					});
				if (TouchIterator == CfgNodeForCurrentFrame.end()) {

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
				CfgNodeForCurrentFrame.erase(TouchIterator,
					CfgNodeForCurrentFrame.end());
				CfgNodeForCurrentFrame.NonBranchingLeftNode = OptionalCollidingNode;
				CfgNodeForCurrentFrame.LinkNodeIntoRemoteNodeInputList(*OptionalCollidingNode);

				// Security checks
				if (CfgNodeForCurrentFrame.empty())
					throw SingularityException(
						fmt::format("Stripping of node {} cannot result in empty node",
							static_cast<void*>(&CfgNodeForCurrentFrame)),
						SingularityException::STATUS_STRIPPING_FAILED_EMPTY_NODE);
			}
			
			return OptionalCollidingNode;
		}


		const IImageLoaderHelp& ImageMapping;
		const xed_state_t       IntelXedState;
	};
}
