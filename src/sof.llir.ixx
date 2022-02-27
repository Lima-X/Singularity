// Singularity obfuscation Framework . Low Level Intermediate Representaiton
//
module;

#include <sof/sof.h>
#include <span>
#include <vector>

export module sof.llir;

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
			FlagsType ReservedCfgFlags : 6; // Reserved flags (for completions sake)
			FlagsType TreeIsBeingTraversed : 1; // Indicates that the graph is currently being traversed, 
												   // this flag is used to check for invocation of another traversal, 
												   // as due to the locking system nested traversals aren't possible.
		};
	};

	struct CfgEntry {
		xed_decoded_inst_t DecodedInstruction;
		byte_t* OriginalAddress;
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
				FlagsType NodeIsIncomplete : 1; // Defines that this node terminates in an undesirable way,
												  // the CFG will also have its incomplete path flag set.
												  // A cfg containing a node with this flag will likely not be
												  // analyzed and processed fully and therefore get removed
												  // from the virtualization pool.

				// Internal CFlags
				FlagsType ReservedFlags : 6; // Reserved flags (for completions sake)
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
		IN const byte_t* VirtualAddress
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
		IN const byte_t* VirtualAddress,
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
		IN const IImageLoaderHelp& ImageMapping,
		IN const FunctionAddress&  FunctionLimits
	)
		: OwningImageMapping(ImageMapping),
		FunctionLimits(FunctionLimits) {
		TRACE_FUNCTION_PROTO;
	}

#pragma region Graph treversal internal interfaces
	struct RecursiveTraversalState {
		IN  const TreeTraversalMode             SelectedTraversalMode;
		IN  const uint32_t                      SearchBindTag;
		IN  CfgNode* NextCfgNode;
		IN  std::function<NodeModifierCallback> CfgCallback;
		OUT CfgNode* ReturnTargetValue;
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


	const IImageLoaderHelp& OwningImageMapping;
	const FunctionAddress   FunctionLimits;

	CfgNode* InitialControlFlowGraphHead = nullptr;
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
