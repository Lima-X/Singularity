// This file implements the recursive descent disassembler engine
module;

#include "VirtualizerBase.h"
#include <vector>

export module ControlFlowGraph;
export import ImageHelp;

// Implements the cfg exception model used to report exceptions from this module
export class CfgException {
public:
	enum ExceptionCode {
		STATUS_INDETERMINED_FUNCTION = -2000,
		STATUS_CODE_LEAVING_FUNCTION,
		STATUS_CFGNODE_WAS_TERMINATED,

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
using FunctionAddress = std::pair<byte_t*, size_t>;

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
				FlagsType NodeIsIncomplete : 1;
				FlagsType NodeIsTerminator : 1;
			};
			FlagsType Flags;
#pragma warning(pop)
		};

		UnderlyingType::value_type& AllocateCfgNodeEntry() {
			TRACE_FUNCTION_PROTO;

			// Check if node as been terminated, if so throw exception similar to bad alloc but emulated
			if (DeterminNodeTypeDynamic() != NODE_IS_INDETERMINED_TYPE_E0)
				throw CfgException(
					fmt::format("CfgNode for rva{:x} was already marked terminated at +{:x}, cannot push new entry",
						NodeHolder->FunctionLimits.first -
							NodeHolder->OwningImageMapping.GetImageFileMapping(),
						this->back().OriginalAddress -
							NodeHolder->OwningImageMapping.GetImageFileMapping()),
					CfgException::STATUS_CFGNODE_WAS_TERMINATED);

			return this->emplace_back();
		}

		void SetNodeIsIncomplete() {
			TRACE_FUNCTION_PROTO;

			// If this function is called the node will be set as incomplete, be terminated
			// and the holding code flow graph will be set as incomplete
			CFlags.NodeIsIncomplete = true;
			NodeHolder->BFlags.ContainsIncompletePaths = true;
			SPDLOG_WARN("CfgNode for rva{:x} was marked incomplete at +{:x}",
				NodeHolder->FunctionLimits.first -
					NodeHolder->OwningImageMapping.GetImageFileMapping(),
				this->back().OriginalAddress -
					NodeHolder->OwningImageMapping.GetImageFileMapping());
		}

		enum NodeTerminationType {
			NODE_IS_TERMINATOR_DEFECTIVE = -1,
			NODE_IS_INDETERMINED_TYPE_E0,
			NODE_IS_TERMINATOR_CANONICAL,
			NODE_IS_UNCONDITIONAL_BRANCH,
			NODE_IS_CONDITIONAL_BRANCHIN,
		};
		NodeTerminationType DeterminNodeTypeDynamic() {
			TRACE_FUNCTION_PROTO;

			// Check if entry list has been terminated yet
			if (!CFlags.NodeIsTerminator)
				return NODE_IS_INDETERMINED_TYPE_E0;

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

		// There are 3 configurations in which ways this LR-pair could be set
		// 1. Both are set, this nodes last entry is a conditional jump (jcc)
		// 2. The right node is set, this nodes last entry is a unconditional
		// 3. Non of the nodes are set, this node terminates a the node (ret)
		CfgNode*      NonBranchingLeftNode = nullptr;
		CfgNode*      BranchingOutRightNode = nullptr;
		CfgFlagsUnion CFlags{};
		
		// The ControlFlowGraph that allocated and owns this node
		ControlFlowGraph* const NodeHolder;
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

	CfgNode* InitialControlFlowGraphHead = nullptr;
	CfgGlobalFlags BFlags{};
};

// The primary image disassembly engine, this is used to generate a workable,
// intermediate representation of the existing code.
// This works (just like a compiler aside from LTO/LTCG) on a function level only,
// it simply isn't really possible to decompile an image on a whole program level,
// at least not without writing millions of hysterics and special edge case handlers.
export class CfgGenerator {
	struct RecursiveDecentState {
		ControlFlowGraph& CfgContext;
		byte_t*           NextCodeAddress;
		FunctionAddress   PredictedFunctionBounds;
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
		IN FunctionAddress PossibleFunction
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
		RecursiveDecentState RecursiveDecentStack(CurrentCfgContext);
		RecursiveDecentStack.NextCodeAddress = PossibleFunction.first;
		RecursiveDecentStack.PredictedFunctionBounds = PossibleFunction;
		RecursiveDecentDisassembler(RecursiveDecentStack);
		
		return CurrentCfgContext;
	}



private:
	enum RecursiveDecentStatus {

		STATUS_RECURSIVE_OK = 0,
	};
	RecursiveDecentStatus RecursiveDecentDisassembler(
		IN RecursiveDecentState& CfgTraversalStack
	) {
		TRACE_FUNCTION_PROTO;

		auto CfgNodeForCurrentFrame = CfgTraversalStack.CfgContext.AllocateFloatingCfgNode();
		auto VirtualInstructionPointer = CfgTraversalStack.NextCodeAddress;

		xed_error_enum_t IntelXedResult{};
		do {

			// Decode current instruction location

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
			
			switch (IntelXedResult) {
			case XED_ERROR_NONE: // Continue execution, no errors
				break;
			case XED_ERROR_BUFFER_TOO_SHORT: // Critical, xed detected code outside of the allowed frame
				throw CfgException(
					fmt::format("Intel xed could not read full instruction at {}, possibly invalid function [{}:{}]",
						static_cast<void*>(VirtualInstructionPointer),
						static_cast<void*>(CfgTraversalStack.PredictedFunctionBounds.first),
						CfgTraversalStack.PredictedFunctionBounds.second),
					CfgException::STATUS_CODE_LEAVING_FUNCTION);
			case XED_ERROR_GENERAL_ERROR:
				// CfgNodeEntry.

			default:
				break;
			}


		} while (1);




	}

	const ImageHelp&  ImageMapping;
	const xed_state_t IntelXedState;
};
