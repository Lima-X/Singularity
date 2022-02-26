// This file describes and implements the base intermediate representation
// and the "decompiler" engine used for translation of x64 code to the IR
//
module;

#include "VirtualizerBase.h"
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <span>
#include <vector>

export module DisassemblerEngine;
export import ImageHelp;

// Implements the cfg exception model used to report exceptions from this module
export class CfgToolException
	: public CommonExceptionType {
public:
	enum ExceptionCode {
		STATUS_INDETERMINED_FUNCTION = -2000,
		STATUS_CODE_LEAVING_FUNCTION,
		STATUS_CFGNODE_WAS_TERMINATED,
		STATUS_XED_NO_MEMORY_BAD_POINTER,
		STATUS_XED_BAD_CALLBACKS_OR_MISSING,
		STATUS_XED_UNSUPPORTED_ERROR,
		STATUS_MISMATCHING_CFG_OBJECT,
		STATUS_DFS_INVALID_SEARCH_CALL,
		STATUS_OVERLAYING_CODE_DETECTED,
		STATUS_NESTED_TRAVERSAL_DISALLOWED,
		STATUS_UNSUPPORTED_RETURN_VALUE,
		STATUS_SPLICING_FAILED_EMPTY_NODE,
		STATUS_STRIPPING_FAILED_EMPTY_NODE,

		STATUS_INVALID_STATUS = 0
	};

	CfgToolException(
		IN const std::string_view& ExceptionText,
		IN       ExceptionCode     StatusCode
	)
		: CommonExceptionType(ExceptionText,
			StatusCode,
			CommonExceptionType::EXCEPTION_CFG_TOOLSET ) {
		TRACE_FUNCTION_PROTO;
	}
};

// Function address abstraction used to denote addresses
export using FunctionAddress = std::pair<byte_t*, size_t>;



// The projects core component, this is the intermediate representation format the engine translates code into.
// This representation can be used to transform and modify, adding and removing inclusive, the code loaded.
// The IR is optimized for taking form compatible to x86-64 assembly code and being expendable to allow 
// other modules to add functionality to it in order to work with it more easily, this is just the base work done.
// This IR takes the form of a control flow graph (cfg).
export class ControlFlowGraph {
	friend class CfgNode;
	friend class CfgGenerator;
public:
	union CfgFlagsUnion {
		using FlagsType = uint8_t;

		FlagsType Flags;
		struct {
			FlagsType ContainsIncompletePaths : 1; // Set to indicate that a node contained was not fully resolved

			// Internal BFlags
			FlagsType ReservedCfgFlags        : 6; // Reserved flags (for completions sake)
			FlagsType TreeIsBeingTraversed    : 1; // Indicates that the graph is currently being traversed, 
			                                       // this flag is used to check for invocation of another traversal, 
			                                       // as due to the locking system nested traversals aren't possible.
		};
	};

	struct CfgEntry {
		xed_decoded_inst_t DecodedInstruction;
		byte_t*            OriginalAddress;
		byte_t             InstructionText[15];

	};

	class CfgNode : 
		protected std::vector<CfgEntry> {
		friend ControlFlowGraph;
		friend class CfgGenerator;
	public:
		using UnderlyingType = std::vector<CfgEntry>;

		union NodeFlagsUnion {
			using FlagsType = uint8_t;

			FlagsType Flags;
			struct {
				FlagsType NodeTerminatesPath : 1; // Set if the node terminates the current branch of the path,
				                                  // a path may have more than a single terminating node.
				FlagsType NodeIsIncomplete   : 1; // Defines that this node terminates in an undesirable way,
				                                  // the CFG will also have its incomplete path flag set.
				                                  // A cfg containing a node with this flag will likely not be
				                                  // analyzed and processed fully and therefore get removed
				                                  // from the virtualization pool.
				
				// Internal CFlags
				FlagsType ReservedFlags      : 6; // Reserved flags (for completions sake)
			};
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

		void LinkNodeIntoRemoteNodeInputList( // Hooks this cfg node into the inbound list of the remote node
			IN CfgNode& ReferencingCfgNode    // The remote node of the same cfg to be linked into
		) {
			TRACE_FUNCTION_PROTO;

			// Check if remote node is part of the same control flow graph allocation
			if (ReferencingCfgNode.NodeHolder != NodeHolder)
				throw CfgToolException(
					fmt::format("Could not link in rmeote node, remote is not part of the same cfg [{}:{}]",
						static_cast<void*>(ReferencingCfgNode.NodeHolder),
						static_cast<void*>(NodeHolder)),
					CfgToolException::STATUS_MISMATCHING_CFG_OBJECT);

			ReferencingCfgNode.InputFlowNodeLinks.push_back(this);
		}
		
		bool TrySpliceOfHeadAtAddressForInsert(       // Tries to splice of the node head at specified address and rebinds,
			                                          // returns true if successful or false if address was not found.
			                                          // Could also splice of the bottom but that would be even more work
			IN byte_t* OriginalAddressOfEntryToSplice // The physical address of the decode contained to splice the node at
		) {
			TRACE_FUNCTION_PROTO;

			// Find the iterator of the entry containing the splice address
			auto SpliceIterator = std::find_if(this->begin(),
				this->end(),
				[OriginalAddressOfEntryToSplice](
					IN const CfgEntry& CfgNodeEntry
					) -> bool {
						TRACE_FUNCTION_PROTO; 
						return CfgNodeEntry.OriginalAddress == OriginalAddressOfEntryToSplice;
				});
			if (SpliceIterator == this->end())
				return false;

			// Found node, splice of the head, rebind cfg and copy data
			auto SplicedNodeHead = NodeHolder->AllocateFloatingCfgNode();
			for (auto Node : InputFlowNodeLinks) {

				if (Node->NonBranchingLeftNode == this)
					Node->NonBranchingLeftNode = SplicedNodeHead;
				for (auto& Link : Node->BranchingOutRightNode)
					if (Link == this)
						Link = SplicedNodeHead;
			}
			SplicedNodeHead->InputFlowNodeLinks = std::move(InputFlowNodeLinks);
			InputFlowNodeLinks.push_back(SplicedNodeHead);
			SplicedNodeHead->insert(SplicedNodeHead->begin(),
				this->begin(),
				SpliceIterator);
			this->erase(this->begin(),
				SpliceIterator);
			SplicedNodeHead->NonBranchingLeftNode = this;

			// Security checks, verify nodes contain data
			if (SplicedNodeHead->empty() || this->empty())
				throw CfgToolException(
					fmt::format("Splicing of node {} into {}, cannot result in empty node",
						static_cast<void*>(this),
						static_cast<void*>(SplicedNodeHead)),
					CfgToolException::STATUS_SPLICING_FAILED_EMPTY_NODE);

			return true;
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
			NODE_IS_UNCONDITIONAL_BRANCH,      // Unconditionally jumps to the new location, fallthrough unknown
			NODE_IS_CONDITIONAL_BRANCHIN,      // Can fall trhough left or jump to the right, branching terminator
			NODE_IS_FALLTHROUGH_BRANCHIN,      // No branch and no terminator, the node falls through due to a input
			NODE_IS_JUMPTABLE_BRANCHING        // The right node is no a single branch but an array of branches
		};
		NodeTerminationType DeterminNodeTypeDynamic() {
			TRACE_FUNCTION_PROTO;

			// Check if this node is linked, if not its type is indeterminable
			if (InputFlowNodeLinks.empty())
				return NODE_IS_INDETERMINED_TYPE;
			
			// Otherwise determine node type by path configuration
			if (BranchingOutRightNode.size()) {
				if (NonBranchingLeftNode)
					return NODE_IS_CONDITIONAL_BRANCHIN;

				return BranchingOutRightNode.size() > 1 ? NODE_IS_JUMPTABLE_BRANCHING
					: NODE_IS_UNCONDITIONAL_BRANCH;
			}
			return CFlags.NodeIsIncomplete ? NODE_IS_TERMINATOR_DEFECTIVE 
				: NODE_IS_TERMINATOR_CANONICAL;
		}

		std::span<CfgEntry> GetCfgDecodeDataList() {
			TRACE_FUNCTION_PROTO; return(*static_cast<UnderlyingType*>(this)); // inheritance is kinda evil i guess
		}
		NodeFlagsUnion GetCFlagsForNode() {
			TRACE_FUNCTION_PROTO; return CFlags;
		}
		byte_t* GetPhysicalNodeStartAddress() const {
			TRACE_FUNCTION_PROTO; return this->front().OriginalAddress;
		}
		byte_t* GetPhysicalNodeEndAddress() const { // End address includes the size of the instruction, 
			                                        // therefore point to the next instruction after this node
			TRACE_FUNCTION_PROTO; 
			auto& CfgNodeEndEntry = this->back();
			auto InstructionLength = xed_decoded_inst_get_length(&CfgNodeEndEntry.DecodedInstruction);
			return CfgNodeEndEntry.OriginalAddress + InstructionLength;
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
		NodeFlagsUnion          CFlags{};          // A set of flags describing properties of this node
		uint32_t                NodeDFSearchTag{}; // An identifier used by the right to left depth first search
		                                           // to locate a node without traversing the same node twice

#pragma region CFG-Node in/out linkage
		// Control flow node head, a list of nodes that can flow into this,
		// a linked non floating node will always have at least one entry.
		std::vector<CfgNode*> InputFlowNodeLinks;

		// There are 4(5) configurations in which ways this LR-pair could be set
		// 1.    Both are set, this nodes last entry is a conditional jump (jcc)
		// 2.    Left node is set, this node has a fall through into the next
		// 3.    The right node is set, this nodes last entry is a unconditional
		// 3.Ex: The right node is a node, node points to a jump table (jmp [+])
		// 4.    Non of the nodes are set, this node terminates a the node (ret)
		CfgNode* NonBranchingLeftNode = nullptr;
		std::vector<CfgNode*> BranchingOutRightNode{};
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


	uint32_t ValidateCfgOverCrossReferences() { // This function validates the graph by walking over every single entry
											    // and checking its cross references, and returns the number of violations
		                                        // NOTE: this function is not efficient and should not be called in hot code paths
		TRACE_FUNCTION_PROTO;

		uint32_t ViolationCount{};
		TraverseOrLocateNodeBySpecializedDFS(SEARCH_PREORDER_NLR,
			[&ViolationCount](
				IN const CfgNode* TraversalNode
				) -> bool {
					TRACE_FUNCTION_PROTO;

					auto NodeXrefCheck = [&ViolationCount, TraversalNode](
						IN const CfgNode* RemoteNode
						) -> void {
							TRACE_FUNCTION_PROTO;
							
							if (std::find(RemoteNode->InputFlowNodeLinks.begin(),
								RemoteNode->InputFlowNodeLinks.end(),
								TraversalNode) == RemoteNode->InputFlowNodeLinks.end()) {

								SPDLOG_ERROR("Node {}, does not properly corss reference {}",
									static_cast<const void*>(RemoteNode),
									static_cast<const void*>(TraversalNode));
								++ViolationCount;
							}
					};

					// Check my nodes reference and cross verify with input list
					if (TraversalNode->NonBranchingLeftNode)
						NodeXrefCheck(TraversalNode->NonBranchingLeftNode);
					for (auto RemoteNode : TraversalNode->BranchingOutRightNode)
						NodeXrefCheck(RemoteNode);

					return false;
			});

		return ViolationCount;
	}


	enum TreeTraversalMode { // Adding 2 of modulus 6 to each value of the enum will result in the next logical operation,
		                     // DO NOT MODIFY THE ORDER OF THESE ENUMS, IT WILL BREAK ANY ALOGRITHM RELYING ON THEM!
		SEARCH_PREORDER_NLR, 
		SEARCH_INORDER_LNR,
		SEARCH_POSTORDER_LRN,
		SEARCH_REVERSE_PREORDER_NRL,
		SEARCH_REVERSE_INORDER_RNL,
		SEARCH_REVERSE_POSTORDER_RLN,
	};
	using NodeModifierCallback = bool(INOUT CfgNode*); 
	CfgNode* TraverseOrLocateNodeBySpecializedDFS(
		TreeTraversalMode                   SearchMode,
		std::function<NodeModifierCallback> CfgCallback
	) {
		TRACE_FUNCTION_PROTO;
		
		// Check if we are running a nested traversal, this is due to the locking mechanism not using a stack for each node,
		// this was done in order to aid with performance as a bit stack would allow nested traversal,
		// but would require a repaint before or after each search, in order to keep track of visited nodes.
		if (BFlags.TreeIsBeingTraversed)
			throw CfgToolException("Cannot traverse graph within a traversal process, nested traversal is not supported",
				CfgToolException::STATUS_NESTED_TRAVERSAL_DISALLOWED);
		BFlags.TreeIsBeingTraversed = true;

		// Search driver: call into recursive function and start the actual search, then handle errors,
		RecursiveTraversalState TraversalStack{
			.SelectedTraversalMode = SearchMode,
			.SearchBindTag = ++RTLDFInitialSearchTagValue,
			.NextCfgNode = InitialControlFlowGraphHead,
			.CfgCallback = std::move(CfgCallback)
		};
		auto Result = RecursiveUnspecializedDFSearch(TraversalStack);
		BFlags.TreeIsBeingTraversed = false;

		switch (Result) {
		case STATUS_RECURSIVE_OK:
			return nullptr;
		case STATUS_PREMATURE_RETURN:
			return TraversalStack.ReturnTargetValue;
		case STATUS_NULLPOINTER_CALL:
		case STATUS_ALREADY_EXPLORED:
			throw CfgToolException("DFS cannot return null pointer call or already explored node, to the driver",
				CfgToolException::STATUS_DFS_INVALID_SEARCH_CALL);
		default:
			throw std::logic_error("DFS cannot return unsupported search result");
		}
	}
	CfgNode* FindNodeContainingVirtualAddress(
		IN       TreeTraversalMode SearchEvaluationMode,
		IN const byte_t*           VirtualAddress
	) {
		TRACE_FUNCTION_PROTO;

		return TraverseOrLocateNodeBySpecializedDFS(
			SearchEvaluationMode,
			[VirtualAddress](
				IN ControlFlowGraph::CfgNode* TraversalNode
				) -> bool {
					TRACE_FUNCTION_PROTO;
					
					return TraversalNode->GetPhysicalNodeStartAddress() <= VirtualAddress
						&& TraversalNode->GetPhysicalNodeEndAddress() > VirtualAddress;
			});
	}
	CfgNode* FindNodeContainingVirtualAddressAndExclude(
		IN       TreeTraversalMode         SearchEvaluationMode,
		IN const byte_t*                   VirtualAddress,
		IN       std::span<const CfgNode*> NodeExclusions
	) {
		TRACE_FUNCTION_PROTO;

		return TraverseOrLocateNodeBySpecializedDFS(
			SearchEvaluationMode,
			[VirtualAddress, &NodeExclusions](
				IN ControlFlowGraph::CfgNode* TraversalNode
				) -> bool {
					TRACE_FUNCTION_PROTO;

					if (std::find(NodeExclusions.begin(),
						NodeExclusions.end(),
						TraversalNode) != NodeExclusions.end())
						return false;
					return TraversalNode->GetPhysicalNodeStartAddress() <= VirtualAddress
						&& TraversalNode->GetPhysicalNodeEndAddress() > VirtualAddress;
			});
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

#pragma region Graph treversal internal interfaces
	struct RecursiveTraversalState {
		IN  const TreeTraversalMode             SelectedTraversalMode;
		IN  const uint32_t                      SearchBindTag;
		IN  CfgNode*                            NextCfgNode;
		IN  std::function<NodeModifierCallback> CfgCallback;
		OUT CfgNode*                            ReturnTargetValue;
	};
	enum RecursiveDescentStatus {
		STATUS_RECURSIVE_OK = 0,

		STATUS_PREMATURE_RETURN,
		STATUS_NULLPOINTER_CALL,
		STATUS_ALREADY_EXPLORED
	};
	RecursiveDescentStatus RecursiveUnspecializedDFSearch(
		INOUT RecursiveTraversalState& SearchTraversalStack
	) {
		TRACE_FUNCTION_PROTO;

		// Checkout stack arguments, the function may internally call with nullptr cause it makes the rest of the code easy 
		if (!SearchTraversalStack.NextCfgNode)
			return STATUS_NULLPOINTER_CALL;

		// Get the node for the current frame and check if it was already explored
		auto SelectedNodeForFrame = SearchTraversalStack.NextCfgNode;
		if (SelectedNodeForFrame->NodeDFSearchTag == SearchTraversalStack.SearchBindTag)
			return STATUS_ALREADY_EXPLORED;
		SelectedNodeForFrame->NodeDFSearchTag = SearchTraversalStack.SearchBindTag;

		// Traverse transformation loop, this is a cursed common increment chain based on the values of the enum
		for (auto i = 0; i < 6; i += 2)
			switch ((SearchTraversalStack.SelectedTraversalMode + i) % 6) {
			case SEARCH_REVERSE_PREORDER_NRL:
			case SEARCH_PREORDER_NLR:
				if (SearchTraversalStack.CfgCallback(
					SelectedNodeForFrame)) {

					SearchTraversalStack.ReturnTargetValue = SelectedNodeForFrame;
					SPDLOG_DEBUG("Found CFG-Node {} for request",
						static_cast<void*>(SelectedNodeForFrame));
					return STATUS_PREMATURE_RETURN;
				}
				break;

			case SEARCH_POSTORDER_LRN:
			case SEARCH_INORDER_LNR:
				SearchTraversalStack.NextCfgNode = SelectedNodeForFrame->NonBranchingLeftNode;
				if (RecursiveUnspecializedDFSearch(SearchTraversalStack) == STATUS_PREMATURE_RETURN)
					return STATUS_PREMATURE_RETURN;
				break;

			case SEARCH_REVERSE_POSTORDER_RLN:
			case SEARCH_REVERSE_INORDER_RNL:
				for (auto NodeEntry : SelectedNodeForFrame->BranchingOutRightNode) {

					SearchTraversalStack.NextCfgNode = NodeEntry;
					if (RecursiveUnspecializedDFSearch(SearchTraversalStack) == STATUS_PREMATURE_RETURN)
						return STATUS_PREMATURE_RETURN;
				}
			}

		return STATUS_RECURSIVE_OK;
	}
#pragma endregion


	const ImageHelp&      OwningImageMapping;
	const FunctionAddress FunctionLimits;

	CfgNode*      InitialControlFlowGraphHead = nullptr;
	CfgFlagsUnion BFlags{};
	uint32_t      RTLDFInitialSearchTagValue{};
};



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
		IN ControlFlowGraph&     ControlFlowGraphContext; // Reference to the cfg used by the current engines stack   (passed down)
		IN const FunctionAddress PredictedFunctionBounds; // Constants of the function frame itself, start and size   (passed down)		
		IN IDisassemblerTracker& DataTracker;

		struct NextScanAddress {
			IN    byte_t*                     NextCodeScanAddress; // Virtual address for the next engine frame to disassemble (passed down)
			IN    ControlFlowGraph::CfgNode*  PreviousNodeForLink; // A pointer to the callers node of its frame of the cfg    (passed down)
																   // The callee has to link itself into the passed node
			INOUT ControlFlowGraph::CfgNode** NextFrameHookInLink; // A field for the callee to fill with its node of frame    (shared)
		};
		std::deque<NextScanAddress> NextCodeScanAddressDeque;
	};

public:
	using FunctionAddress = FunctionAddress;

	CfgGenerator(
		IN const ImageHelp&   ImageMapping,
		IN const xed_state_t& IntelXedState
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
	ControlFlowGraph GenerateCfgFromFunction2( // Tries to generate a cfg using the underlying disassembler engine,
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
	ControlFlowGraph GenerateCfgFromFunction2(             // A deque driven reimplementation of the legacy version,
														   // this avoids huge callstack frames by emulating the recusion with a deque
														   // sacrificing heap memory and simplicity for efficiency and stability
		IN  const FunctionAddress&      PossibleFunction,
		OPT       IDisassemblerTracker& ExternDataTracker  // A data tracker interface instance, this is used to trace and track
	) {
		TRACE_FUNCTION_PROTO;
		
		// Check if the addresses passed are within the configured image
		auto ImageMappingBegin = ImageMapping.GetImageFileMapping();
		auto ImageMappingEnd = ImageMappingBegin +
			ImageMapping.GetCoffMemberByTemplateId<ImageHelp::GET_OPTIONAL_HEADER>().SizeOfImage;
		if (PossibleFunction.first < ImageMappingBegin ||
			PossibleFunction.first +
			PossibleFunction.second > ImageMappingEnd)
			throw CfgToolException(
				fmt::format("Passed function [{}:{}], exceeds or is not within the configured module [{}:{}]",
					static_cast<void*>(PossibleFunction.first),
					PossibleFunction.second,
					static_cast<void*>(ImageMappingBegin),
					ImageMappingEnd - ImageMappingBegin),
				CfgToolException::STATUS_INDETERMINED_FUNCTION);
		SPDLOG_DEBUG("Function to be analyzed at {}",
			static_cast<void*>(PossibleFunction.first));

		// Setup virtual recursive stack for virtual framing
		ControlFlowGraph CurrentCfgContext(ImageMapping,
			PossibleFunction);
		DisasseblerEngineState CfgTraversalStack{
			.ControlFlowGraphContext = CurrentCfgContext,
			.PredictedFunctionBounds = PossibleFunction,
		//	.BranchingFrameStack = -1,
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
		IN ControlFlowGraph::CfgNode* CfgNodeForCurrentFrame
	) {
		TRACE_FUNCTION_PROTO;
		for (;;) {

			// Allocate instruction entry for current cfg node frame
			CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_CFGNODE_COUNT,
				IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
			auto& CfgNodeEntry = CfgNodeForCurrentFrame->AllocateCfgNodeEntry();
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
					throw CfgToolException(\
						fmt::format(ExceptionText, __VA_ARGS__),\
						(StatusCode))
			switch (XedDecodeResult) {
			case XED_ERROR_NONE:
				break;
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_BUFFER_TOO_SHORT,
				CfgToolException::STATUS_CODE_LEAVING_FUNCTION,
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
				CfgToolException::STATUS_XED_NO_MEMORY_BAD_POINTER,
				"The output parameter to the decoded instruction type for xed was null");
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_NO_AGEN_CALL_BACK_REGISTERED,
				CfgToolException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
				"One of both of xeds AGEN callbacks were missing during decode");
			CFGEXCEPTION_FOR_XED_ERROR(XED_ERROR_CALLBACK_PROBLEM,
				CfgToolException::STATUS_XED_BAD_CALLBACKS_OR_MISSING,
				"Xeds AGEN register or segment callback issued an error during decode");

			default:
				throw CfgToolException(
					fmt::format("Singularity's intelxed error handler encountered an unsupported xed error type {}",
						XedDecodeResult),
					CfgToolException::STATUS_XED_UNSUPPORTED_ERROR);
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
		IN ControlFlowGraph::CfgNode*  CfgNodeForCurrentFrame,
		IN ControlFlowGraph::CfgEntry& CfgNodeEntry
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
		IN ControlFlowGraph::CfgNode* CfgNodeForCurrentFrame,
		IN ControlFlowGraph::CfgEntry& CfgNodeEntry
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
		IN ControlFlowGraph::CfgNode* CfgNodeForCurrentFrame,
		IN ControlFlowGraph::CfgEntry& CfgNodeEntry
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
				FindNodeContainingVirtualAddress(ControlFlowGraph::SEARCH_REVERSE_PREORDER_NRL,
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
				throw CfgToolException("Overlaying assembly was found, not yet supported, fatal",
					CfgToolException::STATUS_OVERLAYING_CODE_DETECTED);
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
		IN ControlFlowGraph::CfgNode& CfgNodeForCurrentFrame // The node to check for overlays and stripping/rebinding
	) {
		TRACE_FUNCTION_PROTO;

		// Potentially fell into a existing node already, verify and or strip and rebind,
		// find possibly overlaying node and check
		auto NodeTerminatorLocation = CfgNodeForCurrentFrame.back().OriginalAddress;
		std::array<const ControlFlowGraph::CfgNode*, 1> CfgNodeExclusions{ &CfgNodeForCurrentFrame };
		auto OptionalCollidingNode = CfgTraversalStack.ControlFlowGraphContext.FindNodeContainingVirtualAddressAndExclude(
			ControlFlowGraph::SEARCH_REVERSE_PREORDER_NRL,
			NodeTerminatorLocation,
			std::span{ CfgNodeExclusions });
		if (OptionalCollidingNode) {

			// Found node gotta strip it and relink, first find the location of where they touch
			auto StartAddressOfNode = OptionalCollidingNode->front().OriginalAddress;
			auto TouchIterator = std::find_if(CfgNodeForCurrentFrame.begin(),
				CfgNodeForCurrentFrame.end(),
				[StartAddressOfNode](
					IN const ControlFlowGraph::CfgEntry& DecodeEntry
					) -> bool {
						TRACE_FUNCTION_PROTO;
						return DecodeEntry.OriginalAddress == StartAddressOfNode;
				});
			if (TouchIterator == CfgNodeForCurrentFrame.end()) {

				// Overlaying code somehow, track and throw as exception
				CfgTraversalStack.DataTracker(IDisassemblerTracker::TRACKER_OVERLAYING_COUNT,
					IDisassemblerTracker::UPDATE_INCREMENT_COUNTER);
				throw CfgToolException("An identical terminator address was matched but no matching overlay found",
					CfgToolException::STATUS_OVERLAYING_CODE_DETECTED);
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
				throw CfgToolException(
					fmt::format("Stripping of node {} cannot result in empty node",
						static_cast<void*>(&CfgNodeForCurrentFrame)),
					CfgToolException::STATUS_STRIPPING_FAILED_EMPTY_NODE);
		}
			
		return OptionalCollidingNode;
	}


	const ImageHelp&  ImageMapping;
	const xed_state_t IntelXedState;
};
