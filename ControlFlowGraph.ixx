// This file implements the recursive descent disassembler engine
module;

#include "VirtualizerBase.h"
#include <vector>
#include <tuple>

export module ControlFlowGraph;
export import ImageHelp;

// Implements the cfg exception model used to report exceptions from this module
export class CfgException {
public:
	enum ExceptionCode {
		STATUS_INDETERMINED_FUNCTION = -2000,
		STATUS_CODE_LEAVING_FUNCTION,
		STATUS_CFGNODE_WAS_TERMINATED,
		STATUS_XED_NO_MEMORY_BAD_POINTER,
		STATUS_XED_BAD_CALLBACKS_OR_MISSING,
		STATUS_XED_UNSUPPORTED_ERROR,
		STATUS_MISMATCHING_CFG_OBJECT,

		STATUS_INVALID_STATUS = 0
	};

	CfgException(
		IN const std::string_view& ExceptionText,
		IN       ExceptionCode     StatusCode
	)
		: ExceptionText(ExceptionText),
		StatusCode(StatusCode) {
		TRACE_FUNCTION_PROTO;
	}

	const std::string_view ExceptionText;
	const ExceptionCode    StatusCode;
};

// Function address abstraction used to denote addresses
export using FunctionAddress = std::pair<byte_t*, size_t>;

export class ControlFlowGraph {
	friend class CfgNode;
	friend class CfgGenerator;
public:
	union CfgGlobalFlags {
		using FlagsType = uint8_t;

#pragma warning(push)
#pragma warning(disable : 4201)
		struct {
			FlagsType ContainsIncompletePaths : 1;
		};
		FlagsType Flags;
#pragma warning(pop)
	};

	struct CfgEntry {
		xed_decoded_inst_t DecodedInstruction;
		byte_t*            OriginalAddress;
		byte_t             InstructionText[15];

	};

	class CfgNode : 
		protected std::vector<CfgEntry> {
		friend ControlFlowGraph;
	public:
		using UnderlyingType = std::vector<CfgEntry>;

		union CfgFlagsUnion {
			using FlagsType = uint8_t;

#pragma warning(push)
#pragma warning(disable : 4201)
			struct {
				FlagsType NodeTerminatesPath : 1; // Set if the node terminates the current branch of the path,
				                                  // a path may have more than a single terminating node.
				FlagsType NodeIsIncomplete   : 1; // Defines that this node terminates in an undesirable way,
				                                  // the CFG will also have its incomplete path flag set.
				                                  // A cfg containing a node with this flag will likely not be
				                                  // analyzed and processed fully and therefore get removed
				                                  // from the virtualization pool.
				FlagsType ReservedFlags      : 6; // Reserved flags (for completions sake)
			};
			FlagsType Flags;
#pragma warning(pop)
		};


		UnderlyingType::value_type& AllocateCfgNodeEntry() {
			TRACE_FUNCTION_PROTO;	
#if 0
			// Check if node as been terminated, if so throw exception similar to bad alloc but emulated
			if (DeterminNodeTypeDynamic() != NODE_IS_INDETERMINED_TYPE)
				throw CfgException(
					fmt::format("CfgNode for rva{:x} was already marked terminated at +{:x}, cannot push new entry",
						NodeHolder->FunctionLimits.first -
							NodeHolder->OwningImageMapping.GetImageFileMapping(),
						this->back().OriginalAddress -
							NodeHolder->OwningImageMapping.GetImageFileMapping()),
					CfgException::STATUS_CFGNODE_WAS_TERMINATED);
#endif
			return this->emplace_back();
		}

		void LinkNodeIntoRemoteNodeInputList( // Hooks a this cfg node into the inbound list of the remote node
			IN CfgNode& ReferencingCfgNode    // The remote node of the same cfg to be linked into
		) {
			TRACE_FUNCTION_PROTO;

			// Check if remote node is part of the same control flow graph allocation
			if (ReferencingCfgNode.NodeHolder != NodeHolder)
				throw CfgException(
					fmt::format("Could not link in rmeote node, remote is not part of the same cfg [{}:{}]",
						static_cast<void*>(ReferencingCfgNode.NodeHolder),
						static_cast<void*>(NodeHolder)),
					CfgException::STATUS_MISMATCHING_CFG_OBJECT);

			ReferencingCfgNode.InputFlowNodeLinks.push_back(this);
		}



		void TerminateThisNodeAsCfgExit() {
			TRACE_FUNCTION_PROTO;

			CFlags.NodeTerminatesPath = true;
		}
		void SetNodeIsIncomplete() {
			TRACE_FUNCTION_PROTO;

			// If this function is called the node will be set as incomplete, be terminated
			// and the holding code flow graph will be set as incomplete
			TerminateThisNodeAsCfgExit();
			CFlags.NodeIsIncomplete = true;
			NodeHolder->BFlags.ContainsIncompletePaths = true;
			SPDLOG_WARN("CfgNode for rva{:x} was marked incomplete at +{:x}",
				NodeHolder->FunctionLimits.first -
					NodeHolder->OwningImageMapping.GetImageFileMapping(),
				this->back().OriginalAddress -
					NodeHolder->OwningImageMapping.GetImageFileMapping());
		}
		

		enum NodeTerminationType {
			NODE_IS_TERMINATOR_DEFECTIVE = -1, // The node is incomplete and contains a non canonical exit location
			NODE_IS_INDETERMINED_TYPE,         // The type of node is not yet known, this is the default for unlinked
			NODE_IS_TERMINATOR_CANONICAL,      // Node has no branches, but is canonical, it terminates the path
			NODE_IS_UNCONDITIONAL_BRANCH,      // 
			NODE_IS_CONDITIONAL_BRANCHIN,
			NODE_IS_FALLTHROUGH_BRANCHIN
		};
		NodeTerminationType DeterminNodeTypeDynamic() {
			TRACE_FUNCTION_PROTO;

			// Check if this node is linked, if not its type is indeterminable 
			if (this->empty())
				return NODE_IS_INDETERMINED_TYPE;
			
			// Otherwise determine node type by path configuration
			if (BranchingOutRightNode) {
				if (NonBranchingLeftNode)
					return NODE_IS_CONDITIONAL_BRANCHIN;
				return NODE_IS_UNCONDITIONAL_BRANCH;
			}
			return CFlags.NodeIsIncomplete ? NODE_IS_TERMINATOR_DEFECTIVE 
				: NODE_IS_TERMINATOR_CANONICAL;
		}

		CfgFlagsUnion GetCFlagsForNode() {
			TRACE_FUNCTION_PROTO; return CFlags;
		}

	private:
		CfgNode(
			IN ControlFlowGraph* NodeHolder
		)
			: NodeHolder(NodeHolder) {
			TRACE_FUNCTION_PROTO;
		}

		// CFG-Node metadata and others
		ControlFlowGraph* const NodeHolder;        // The ControlFlowGraph that allocated and owns this node
		CfgFlagsUnion           CFlags{};          // A set of flags describing properties of this node
		uint32_t                RTLDFSearchTage{}; // An identifier used by the right to left depth first search
		                                           // to locate a node without traversing the same node twice

#pragma region CFG-Node in/out linkage
		// Control flow node head, a list of nodes that can flow into this,
		// a linked non floating node will always have at least one entry.
		std::vector<CfgNode*> InputFlowNodeLinks;

		// There are 4 configurations in which ways this LR-pair could be set
		// 1. Both are set, this nodes last entry is a conditional jump (jcc)
		// 2. Left node is set, this node has a fall through into the next
		// 3. The right node is set, this nodes last entry is a unconditional
		// 4. Non of the nodes are set, this node terminates a the node (ret)
		CfgNode* NonBranchingLeftNode = nullptr;
		CfgNode* BranchingOutRightNode = nullptr;
#pragma endregion
	};

	CfgNode* AllocateFloatingCfgNode() {
		TRACE_FUNCTION_PROTO;

		// The returned object is not managed, also doesnt have to be,
		// allocation is inaccessible from outside of CfgGenerator anyways.
		CfgNode* FloatingNode = new CfgNode(this);
		if (!InitialControlFlowGraphHead)
			InitialControlFlowGraphHead = FloatingNode;
		return FloatingNode;
	}

	CfgNode* GetInitialCfgNode() {
		TRACE_FUNCTION_PROTO; return InitialControlFlowGraphHead;
	}

private:
	ControlFlowGraph(
		IN const ImageHelp&       ImageMapping,
		IN const FunctionAddress& FunctionLimits
	)
		: OwningImageMapping(ImageMapping),
		  FunctionLimits(FunctionLimits) {
		TRACE_FUNCTION_PROTO;
	}

	const ImageHelp&      OwningImageMapping;
	const FunctionAddress FunctionLimits;

	CfgNode*       InitialControlFlowGraphHead = nullptr;
	CfgGlobalFlags BFlags{};
	uint32_t       RTLDFInitialSearchTagValue{};
};

// The primary image disassembly engine, this is used to generate a workable,
// intermediate representation of the existing code.
// This works (just like a compiler aside from LTO/LTCG) on a function level only,
// it simply isn't really possible to decompile an image on a whole program level,
// at least not without writing millions of heuristics and special edge case handlers.
export class CfgGenerator {
	struct RecursiveDescentState {                          // Defines the pass down stack state for the recursive disassembly engine
		ControlFlowGraph&          CfgContext;              // Reference to the cfg used by the current engines stack   (passed down)
		      byte_t*              NextCodeAddress;         // Virtual address for the next engine frame to disassemble (passed down)
		const FunctionAddress      PredictedFunctionBounds; // Constants of the function frame itself, start and size   (passed down)
		ControlFlowGraph::CfgNode* ReturnedFloatingTree;    // A pointer to a linked floating assembled tree fragment   (passed up)
		                                                    // The caller receiving will have to hook this into its node

		uint32_t BranchingFrameStack; // Number of times the a branch has been taken on the stack (passed down)
		enum {
			RECURSIVE_FALL_THROUGH,   // The invocation has passed the address of a fall through
			RECURSIVE_BRANCH_TAKEN    // A branch was taken by the engine, BFS was incremented
		} CallerBranchType;           // whether or not the caller invoked as is branched or not  (passed down)
	};

public:
	using FunctionAddress = FunctionAddress;

	CfgGenerator(
		IN const ImageHelp&  ImageMapping,
		IN       xed_state_t IntelXedState
	)
		: ImageMapping(ImageMapping),
		  IntelXedState(IntelXedState) {
		TRACE_FUNCTION_PROTO;
		SPDLOG_INFO("Configured control flow graph generator for image at {}",
			static_cast<void*>(ImageMapping.GetImageFileMapping()));
	}

	ControlFlowGraph GenerateControlFlowGraphFromFunction(
		IN const FunctionAddress& PossibleFunction
	) {
		TRACE_FUNCTION_PROTO;

		// Check if the addresses passed are within the configured image
		auto ImageMappingBegin = ImageMapping.GetImageFileMapping();
		auto ImageMappingEnd = ImageMappingBegin +
			ImageMapping.GetCoffMemberByTemplateId<
				ImageHelp::GET_OPTIONAL_HEADER>().SizeOfImage;
		if (PossibleFunction.first < ImageMappingBegin ||
			PossibleFunction.first +
			PossibleFunction.second > ImageMappingEnd)
			throw CfgException(
				fmt::format("Passed function [{}:{}], exceeds or is not within the configured module [{}:{}]",
					static_cast<void*>(PossibleFunction.first),
					PossibleFunction.second,
					static_cast<void*>(ImageMappingBegin),
					ImageMappingEnd - ImageMappingBegin),
				CfgException::STATUS_INDETERMINED_FUNCTION);
		SPDLOG_DEBUG("Function to be analyzed at {}",
			static_cast<void*>(PossibleFunction.first));


		// This function will have to go to the presumed start address of the function then recursively start decending analyzing the function
		

		// Initialize contexts and start the traversal process, yup this function is legit just a jumper
		// Cannot return any non exception type status that would indicate a failure
		ControlFlowGraph CurrentCfgContext(ImageMapping, 
			PossibleFunction);
		RecursiveDescentState RecursiveDescentStack{
			.CfgContext = CurrentCfgContext ,
			.NextCodeAddress = PossibleFunction.first,
			.PredictedFunctionBounds = PossibleFunction,
		};
		RecursiveDescentDisassembler(RecursiveDescentStack);
		
		return CurrentCfgContext;
	}

#pragma region Minimal utilities
	bool IsDecodedInstructionBranching(
		IN xed_iclass_enum_t XedInstructionClass
	) {
		TRACE_FUNCTION_PROTO;
		return XedInstructionClass >= XED_ICLASS_JB 
			&& XedInstructionClass <= XED_ICLASS_JZ;
	}
	bool IsDecodedInstructionReturning(
		IN xed_iclass_enum_t XedInstructionClass
	) {
		// IMPROVE: Probably really fucking inefficient but what gives, don't care right now
		TRACE_FUNCTION_PROTO;
		
		static const xed_iclass_enum_t XedReturningClasses[]{
			XED_ICLASS_IRET,
			XED_ICLASS_IRETD,
			XED_ICLASS_IRETQ,
			XED_ICLASS_RET_FAR,
			XED_ICLASS_RET_NEAR,
			XED_ICLASS_SYSEXIT,
			XED_ICLASS_SYSRET,
			XED_ICLASS_SYSRET64,
			XED_ICLASS_SYSRET_AMD
		};
		for (auto ReturningClass : XedReturningClasses)
			if (ReturningClass == XedInstructionClass)
				return true;
		return false;		
	}
#pragma endregion




private:
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
	enum RecursiveDescentStatus {
		STATUS_RECURSIVE_OK = 0,
		STATUS_POTENTIALLY_BAD_DECODE,
	};
	RecursiveDescentStatus RecursiveDescentDisassembler(
		IN RecursiveDescentState& CfgTraversalStack
	) {
		TRACE_FUNCTION_PROTO;

		auto CfgNodeForCurrentFrame = CfgTraversalStack.CfgContext.AllocateFloatingCfgNode();
		auto VirtualInstructionPointer = CfgTraversalStack.NextCodeAddress;

		xed_error_enum_t IntelXedResult{};
		do {
			// Allocate instruction entry for current cfg node frame
			auto& CfgNodeEntry = CfgNodeForCurrentFrame->AllocateCfgNodeEntry();
			xed_decoded_inst_zero_set_mode(&CfgNodeEntry.DecodedInstruction,
				&IntelXedState);
			CfgNodeEntry.OriginalAddress = VirtualInstructionPointer;

			// Decode current selected opcode with maximum size
			IntelXedResult = xed_decode(&CfgNodeEntry.DecodedInstruction,
				VirtualInstructionPointer,
				static_cast<uintptr_t>(CfgTraversalStack.PredictedFunctionBounds.first +
					CfgTraversalStack.PredictedFunctionBounds.second -
					VirtualInstructionPointer) & 15);

#define CFGEXCEPTION_FOR_XED_ERROR(ErrorType, StatusCode, ExceptionText, ...)\
			case (ErrorType):\
				throw CfgException(\
					fmt::format((ExceptionText), __VA_ARGS__),\
					(StatusCode))
			static const char* XedBadDecode[]{
				"XED_ERROR_GENERAL_ERROR",
				"XED_ERROR_INVALID_FOR_CHIP",
				"XED_ERROR_BAD_REGISTER",
				"XED_ERROR_BAD_LOCK_PREFIX",
				"XED_ERROR_BAD_REP_PREFIX",
				"XED_ERROR_BAD_LEGACY_PREFIX",
				"XED_ERROR_BAD_REX_PREFIX",
				"XED_ERROR_BAD_EVEX_UBIT",
				"XED_ERROR_BAD_MAP",
				"XED_ERROR_BAD_EVEX_V_PRIME",
				"XED_ERROR_BAD_EVEX_Z_NO_MASKING" };
			static const char* XedTextError[]{
				"A memory operand index was not 0 or 1 during decode",
				nullptr,
				"Xed encountered AVX2 gathers with invalid index, destination and mask register combinations",
				"Xed encountered an instruction that exceeds the physical instruction limit for x64",
				"Xed encountered an instruction that is invalid on the current specified mode of x64",
				"Xed encountered an EVEX.LL that cannot be equal to 3 unless embedded rounding is enabled",
				"Xed encountered an instruction which cannot have the same register for destination and source"
			};
			switch (IntelXedResult) {
			case XED_ERROR_NONE: {

				// Xed successfully decoded the instruction, copy the instruction to the cfg entry
				auto InstructionLength = xed_decoded_inst_get_length(&CfgNodeEntry.DecodedInstruction);
				memcpy(&CfgNodeEntry.InstructionText,
					VirtualInstructionPointer,
					InstructionLength);
				memset(&CfgNodeEntry.InstructionText + InstructionLength,
					0,
					15 - InstructionLength);

				// TODO: use formatter here instead of the iclass bullshit value i would have to look up
				auto XedInstrctionClass = xed_decoded_inst_get_iclass(&CfgNodeEntry.DecodedInstruction);
				SPDLOG_DEBUG("Decoded instruction at rip={} to iclass={}",
					static_cast<void*>(VirtualInstructionPointer),
					XedInstrctionClass);

				// Determine the type of instruction and with that if we have to specifically treat it
				// there are several special cases such as trailing function c
#pragma region Recursive disasm engine reinvoke and detect
				// Case 1: check if the instruction is returning and could therefore terminate the node
				if (xed_decoded_inst_get_category(&CfgNodeEntry.DecodedInstruction) == XED_CATEGORY_RET) {
					
					// Terminate the node and return constructed tree
					CfgNodeForCurrentFrame->TerminateThisNodeAsCfgExit();
					CfgTraversalStack.ReturnedFloatingTree = CfgNodeForCurrentFrame;
					return STATUS_RECURSIVE_OK;
				}

				// Case 2: The instruction is unconditionally branching (jmp) and
				if (xed_decoded_inst_get_category(&CfgNodeEntry.DecodedInstruction) == XED_CATEGORY_UNCOND_BR) {

					// TODO: heuristics for "emulated call", "trailing function call" / "jump outside",
					//       have to be added here, there are possibly more to improve decoding



					// Most basic handler that redispatches the disassembly engine in the standard expected usecase for jmp
					// does not support rip relative yet
					
					 
					// calculate target jump location and reconfigure current traversal stack
					auto BranchDisplacment = xed_decoded_inst_get_branch_displacement(&CfgNodeEntry.DecodedInstruction);
					auto LenghtOfInstruction = xed_decoded_inst_get_length(&CfgNodeEntry.DecodedInstruction);
					CfgTraversalStack.NextCodeAddress = VirtualInstructionPointer + 
						LenghtOfInstruction +
						BranchDisplacment;
					

				}


				// After that we check if the instruction could branch out and invoke ourself for the branch
				if (IsDecodedInstructionBranching(XedInstrctionClass)) {

					// TODO: We need to check the type of branch we encountered, then determine the new locations
					//       branch out and analyze those parts, the returned assembled tree will then be hooked into our tree
					//       and we then return that or continue depending on state



					// Detect the type of branch instruction, which will determine how the engine proceeds


					// Case 1: the type of branch is unconditional, the engine will only analyze the right path
					if (XedInstrctionClass == XED_ICLASS_JMP ||
						XedInstrctionClass == XED_ICLASS_JMP_FAR) {

						// Find the jump traget location of the instruction
						

					}





#pragma endregion
				}
								
				// No special instruction decoded, continue decoding node linearly
				break;
			}
			
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_BUFFER_TOO_SHORT,
				CfgException::STATUS_CODE_LEAVING_FUNCTION,
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
				CfgNodeForCurrentFrame->SetNodeIsIncomplete();
				SPDLOG_WARN("A bad decode of type {} was raised by xed, state could be invalid",
					XedBadDecode[IntelXedResult - XED_ERROR_GENERAL_ERROR]);
				break;

			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_NO_OUTPUT_POINTER,
				CfgException::STATUS_XED_NO_MEMORY_BAD_POINTER,
				"The output parameter to the decoded instruction type for xed was null");
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_NO_AGEN_CALL_BACK_REGISTERED,
				CfgException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
				"One of both of xeds AGEN callbacks were missing during decode");
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_CALLBACK_PROBLEM,
				CfgException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
				"Xeds AGEN register or segment callback issued an error during decode");

			case XED_ERROR_BAD_MEMOP_INDEX:
			case XED_ERROR_GATHER_REGS:
			case XED_ERROR_INSTR_TOO_LONG:
			case XED_ERROR_INVALID_MODE:
			case XED_ERROR_BAD_EVEX_LL:
			case XED_ERROR_BAD_REG_MATCH:
				CfgNodeForCurrentFrame->SetNodeIsIncomplete();
				SPDLOG_WARN(XedTextError[IntelXedResult - XED_ERROR_BAD_MEMOP_INDEX]);
				break;
			
			default:
				throw CfgException(
					fmt::format("Singularity's intelxed error handler encountered an unsupported xed error type {}",
						IntelXedResult),
					CfgException::STATUS_XED_UNSUPPORTED_ERROR);				
			}
			#undef CFGEXCEPTION_FOR_XED_ERROR

		} while (1);
	
	QUICK_EXIT_DECODER_LOOP:;


		// 


	}



	const ImageHelp&  ImageMapping;
	const xed_state_t IntelXedState;
};


