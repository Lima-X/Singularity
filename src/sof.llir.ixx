// Singularity obfuscation Framework . Low Level Intermediate Representaiton
//
module;

#include <sof/sof.h>
#include <span>
#include <vector>

export module sof.llir;
import sof.image.load;

// LLIR syntax specification, this follows the LLVM-IR design in a simplified manner:
// as LLIR targets a 3 TAC
//
// Syntax designations:
// @[Label]   : @ designates a global symbol, such as a function
// %[Label]   : % refers to a local that is unique to a function
// [Constant] : no symbol followed by a number indicates a constant
//
// LLIR Opcode:
// move
// add,
// subtract,
// multiply,
// divide,
// modulus,
// shiftleft,
// rotateleft,
// deref,
// store,
//
// LLIR types:
// An llir type can be a fundamental type such as
// i8, i16, i32, i64, u8, u16, u32, or u64,
// or it could represent a user declared type (unused for this project)
//
// Statement syntax:
// @/%[Destination] = [Opcode] [type] [Source1], [Source2]
//

export namespace sof {

	// Symbol identifier for cfg local lookup
	class LlirSymbolType {
		friend class Llir3acOperandSymbolRegistry;
	public:
		friend auto operator<=>(
			IN const LlirSymbolType&,
			IN const LlirSymbolType&) = default;

	private:
		LlirSymbolType(
			IN uint32_t UniqueIdentifier
		)
			: UniqueIdentifier(UniqueIdentifier) {
			TRACE_FUNCTION_PROTO;
		}

		uint32_t& operator++() {
			TRACE_FUNCTION_PROTO; return ++UniqueIdentifier;
		}

		uint32_t UniqueIdentifier;
	};
	
	// A polymorphic base type stored as a list in cfg nodes used to identify the and define abstractions
	class ICfgInstruction {
	public:
		enum CfgStatementType : int8_t {
			TYPE_PURE_NATIVE,
			TYPE_AMD64_LIFTED,
		};

		ICfgInstruction(
			IN CfgStatementType TypeOfStatement,
			IN LlirSymbolType   UniqueIdentifier
		)
			: TypeOfStatement(TypeOfStatement),
			  UniqueIdentifier(UniqueIdentifier) {
			TRACE_FUNCTION_PROTO;
		}
		virtual ~ICfgInstruction() {
			TRACE_FUNCTION_PROTO;
		}

		CfgStatementType GetTypeOfEntry() const {
			TRACE_FUNCTION_PROTO; return TypeOfStatement;
		}

		const LlirSymbolType UniqueIdentifier;

	private:
		const CfgStatementType TypeOfStatement;
	};

	// Basic native encoding container for xed to store its data in
	class LlirAmd64NativeEncoded
		: public ICfgInstruction {
	public:
		xed_decoded_inst_t DecodedInstruction;
		byte_t*			   OriginalAddress;
	};

#pragma region LLIR_3ac_major_ir_format_implementation
	union LlirLogicalFlag {
		enum FlagActionType {
			LLIR_FLAG_SET = 0,
			LLIR_FLAG_RESET,
			LLIR_FLAG_WRITE,
			LLIR_FLAG_TEST,
		} FlagAction;
		enum FlagFieldType {
			LLIR_FLAG_NOT_IMPLEMENTED = 0,
			LLIR_FLAG_CARRY,
			LLIR_FLAG_AUXILARRY_CARRY,
			LLIR_FLAG_PARITY_BIT,
			LLIR_FLAG_ZERO,
			LLIR_FLAG_SIGNED,
			LLIR_FLAG_OVERFLOW,
		} FlagField;
	};

	class Llir3acOperand {
	public:
		Llir3acOperand(
			IN LlirSymbolType UniqueIdentifier
		)
			: UniqueIdentifier(UniqueIdentifier) {
			TRACE_FUNCTION_PROTO;
		}

		const LlirSymbolType UniqueIdentifier; // An id used to identify a token or variable
		                                       // a better or more common name would be a symbol
		enum OperandWidthType : int8_t {
			OPERAND_UNSIGNED_64_BIT,
			OPERAND_SIGNED_64_BIT,
			OPERAND_UNSIGNED_32_BIT,
			OPERAND_SIGNED_32_BIT,
			OPERAND_UNSIGNED_8_BIT,
			OPERAND_SIGNED_8_BIT,
			OPERAND_UNSIGNED_16_BIT,
			OPERAND_SIGNED_16_BIT,

			OPERAND_TYPE_NON_EXISTENT = -1,
		} OperandWidth;

		enum OperandTypeType : int8_t {
			OPERAND_VIRTUAL_REGISTER,
			OPERAND_STACK_LOCATION,
			OPERAND_MEMORY_LOCATION,
			OPERAND_IS_CONSTANT,
			OPERAND_LOGICAL_FLAGS,
		} OperandType;
		union {
			int64_t  StackLocationOffset;   // OPERAND_STACK_LOCATION
			uint64_t MemoryLocation;	    // OPERAND_MEMORY_LOCATION
			uint64_t OptionalConstantValue; // OPERAND_IS_CONSTANT
		};
	};



	// Second level component of the cfg, contains all the necessary metadata
	// as well as the logical code, as a polymorphic type
	class LlirBasicBlock;
	class Llir3AddressCode :
		public ICfgInstruction {
	public:
		enum LlirOpcode : int8_t {
			LLIR_OPCODE_NOP = 0, // No command execute

			LLIR_OPCODE_MOVE,        // [lhs]   = move  _t [rhs]           |
			LLIR_OPCODE_DEREFERENCE, // [lhs]   = deref _t [rhs]           |
			LLIR_OPCODE_STORE,		 // [lhs]   = store _t [rhs]           |
			LLIR_OPCODE_COMPARE,	 //         = cmp   _t [lhs],  [rhs]   | [flags]
			LLIR_OPCODE_JUMP,        // [ip]    = jmp   _t [addr]          |
			LLIR_OPCODE_JUMP_IF,     // [ip]    = jcc   _t [addr], [fmask] | [flags]
			LLIR_OPCODE_CALL,        // [ip/sp] = call  _t [addr]          |
			LLIR_OPCODE_RETURN,      // [ip/sp] = ret   _t                 |

			LLIR_OPCODE_ADD,
			LLIR_OPCODE_SUBTARCT,
			LLIR_OPCODE_MULTIPLY,
			LLIR_OPCODE_DIVIDE,
			LLIR_OPCODE_MODULUS,
			LLIR_OPCODE_SHIFT,
			LLIR_OPCODE_ROTATE,
		};
		using LlirFlagActions = std::vector<LlirLogicalFlag>;

		Llir3AddressCode(
			IN LlirBasicBlock& OwningBasicBlock,
			IN uint32_t		   StatementGroupId
		)
			: OwningBasicBlock(OwningBasicBlock),
			  StatementGroupId(StatementGroupId) {
			TRACE_FUNCTION_PROTO;
		}

		uint32_t        StatementGroupId;
		LlirFlagActions FlagsActionStates;
		LlirSymbolType  TargetOperand;
		LlirOpcode		OperationCode;

		enum {
			OPERAND_SOURCE_ONE = 0,
			OPERAND_SOURCE_TWO,
		};
		LlirSymbolType SourceOperands[2];

		LlirAmd64NativeEncoded* GetOptionalInitialEncoding() const {
			TRACE_FUNCTION_PROTO; return OptionalInitialNative.get();
		}

		const LlirBasicBlock& OwningBasicBlock;

	private:
		std::unique_ptr<LlirAmd64NativeEncoded> OptionalInitialNative;
	};

	

#pragma endregion



	// Describes a cfg's node, a core subcontainer of the whole cfg
	class LlirBasicBlock :
		protected std::vector<std::unique_ptr<ICfgInstruction>> {
		friend ControlFlowGraph;
		friend class CfgGenerator;
	public:
		union LlirBlockFlags {
			uint8_t FlatFlags;
			struct {
				uint8_t NodeTerminatesPath : 1; // Set if the node terminates the current branch of the path,
												// a path may have more than a single terminating node.
				uint8_t NodeIsIncomplete : 1;	// Defines that this node terminates in an undesirable way,
												// the CFG will also have its incomplete path flag set.
												// A cfg containing a node with this flag will likely not be
												// analyzed and processed fully and therefore get removed
												// from the virtualization pool.
				// Internal CFlags
				uint8_t ReservedFlags : 6; // Reserved flags (for completions sake)
			};
		};
		using type_t = std::vector<std::unique_ptr<ICfgInstruction>>;

	#if 0
		type_t::value_type& AllocateCfgNodeEntry() {
			TRACE_FUNCTION_PROTO;
			// Check if node as been terminated, if so throw exception similar to bad alloc but emulated
			if (DeterminNodeTypeDynamic() != NODE_IS_INDETERMINED_TYPE)
				throw SingularityException(
					fmt::format("CfgNode for rva{:x} was already marked terminated at +{:x}, cannot push new entry",
						OwningCfgContainer.FunctionLimits.first -
							OwningCfgContainer.OwningImageMapping.GetImageFileMapping(),
						this->back().OriginalAddress -
							OwningCfgContainer.OwningImageMapping.GetImageFileMapping()),
					SingularityException::STATUS_CFGNODE_WAS_TERMINATED);
			return this->emplace_back();
		}
	#endif

		void LinkNodeIntoRemoteNodeInputList(     // Hooks this cfg node into the inbound list of the remote node
			IN LlirBasicBlock& ReferencingCfgNode // The remote node of the same cfg to be linked into
		) {
			TRACE_FUNCTION_PROTO;

			// Check if remote node is part of the same control flow graph allocation
			if (&ReferencingCfgNode.OwningCfgContainer != &OwningCfgContainer)
				throw SingularityException(
					fmt::format("Could not link blocks, not part of the same cfg [" ESC_BRIGHTRED"{}" ESC_RESET"|" ESC_BRIGHTRED"{}]",
						static_cast<void*>(&ReferencingCfgNode.OwningCfgContainer),
						static_cast<void*>(&OwningCfgContainer)),
					SingularityException::STATUS_MISMATCHING_CFG_OBJECT);

			ReferencingCfgNode.InputFlowNodeLinks.push_back(this);
		}


		bool TrySpliceOfHeadAtAddressForInsert2(
			IN LlirSymbolType UidOfEntryToSplice
		) {
			TRACE_FUNCTION_PROTO;

			// Find the iterator of the entry containing the splice address
			auto SpliceIterator = std::find_if(this->begin(),
				this->end(),
				[UidOfEntryToSplice](
					IN const ICfgInstruction& CfgNodeEntry
					) -> bool {
						// TODO: INVALID, needs fixups, addresses shall not be used for identification purposes
						TRACE_FUNCTION_PROTO; return CfgNodeEntry.UniqueIdentifier == UidOfEntryToSplice;
				});
			if (SpliceIterator == this->end())
				return false;

			// Found node, splice of the head, rebind cfg and copy data
			auto SplicedNodeHead = OwningCfgContainer.AllocateFloatingCfgNode();
			for (auto Block : InputFlowNodeLinks) {

				if (Block->NonBranchingLeftNode == this)
					Block->NonBranchingLeftNode = SplicedNodeHead;
				for (auto& Link : Block->BranchingOutRightNode)
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
				throw SingularityException(
					fmt::format("Splicing of node {} into {}, cannot result in empty node",
						static_cast<void*>(this),
						static_cast<void*>(SplicedNodeHead)),
					SingularityException::STATUS_SPLICING_FAILED_EMPTY_NODE);

			return true;
		}
		bool TrySpliceOfHeadAtAddressForInsert(       // Tries to splice of the node head at specified address and rebinds,
													  // returns true if successful or false if address was not found.
													  // Could also splice of the bottom but that would be even more work
			IN byte_t* OriginalAddressOfEntryToSplice // The physical address of the decode contained to splice the node at
		) {
			TRACE_FUNCTION_PROTO;

			throw std::runtime_error("THIS IS NO LONGER IMPLEMENTED");
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
			TRACE_FUNCTION_PROTO; return(*static_cast<type_t*>(this)); // inheritance is kinda evil i guess
		}
		LlirBlockFlags GetCFlagsForNode() {
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
		LlirBasicBlock(
			IN ControlFlowGraph& OwningCfgContainer,
			IN LlirSymbolType    UniqueIdentifier
		)
			: OwningCfgContainer(OwningCfgContainer),
			  UniqueIdentifier(UniqueIdentifier) {
			TRACE_FUNCTION_PROTO;
		}

		// CFG-Node metadata and others
		ControlFlowGraph&    OwningCfgContainer; // The ControlFlowGraph that allocated and owns this node
		const LlirSymbolType UniqueIdentifier;   // used to identify the node uniquely 
		LlirBlockFlags       CFlags{};           // A set of flags describing properties of this node
		uint32_t             NodeDFSearchTag{};  // An identifier used by the right to left depth first search
												 // to locate a node without traversing the same node twice

	#pragma region CFG-Node in/out linkage
		// Control flow node head, a list of nodes that can flow into this,
		// a linked non floating node will always have at least one entry.
		std::vector<LlirBasicBlock*> InputFlowNodeLinks;

		// There are 4(5) configurations in which ways this LR-pair could be set
		// 1.    Both are set, this nodes last entry is a conditional jump (jcc)
		// 2.    Left node is set, this node has a fall through into the next
		// 3.    The right node is set, this nodes last entry is a unconditional
		// 3.Ex: The right node is a node, node points to a jump table (jmp [+])
		// 4.    Non of the nodes are set, this node terminates a the node (ret)
		LlirBasicBlock*			     NonBranchingLeftNode = nullptr;
		std::vector<LlirBasicBlock*> BranchingOutRightNode{};
	#pragma endregion
	};



	class Llir3acOperandSymbolRegistry
		: private std::vector<Llir3acOperand> {
		using vector_t = std::vector<Llir3acOperand>;
	public:
		LlirSymbolType GenNewUniqueSymbolId() {
			TRACE_FUNCTION_PROTO; return ++CurrentSymbolId;
		}

		Llir3acOperand& Allocate3acOperandEnlist() {
			TRACE_FUNCTION_PROTO; return this->emplace_back(
				GenNewUniqueSymbolId());
		}

		Llir3acOperand* LocateAndRetrieve3acOperandForSymbol(
			IN LlirSymbolType UniqueIdentifier
		) {
			TRACE_FUNCTION_PROTO;
			auto OperandIterator = std::find_if(this->begin(),
				this->end(),
				[UniqueIdentifier](
					IN const value_type& OperandRegistryEntry
					) -> bool {
						TRACE_FUNCTION_PROTO; return OperandRegistryEntry.UniqueIdentifier == UniqueIdentifier;
				});
			return OperandIterator == this->end() ? nullptr : &*OperandIterator;
		}

		using vector_t::begin;
		using vector_t::cbegin;
		using vector_t::end;
		using vector_t::cend;
		using vector_t::front;
		using vector_t::back;

	private:
		LlirSymbolType CurrentSymbolId(0);
	};



	// The projects core component, this is the intermediate representation format the engine translates code into.
	// This representation can be used to transform and modify, adding and removing inclusive, the code loaded.
	// The IR is optimized for taking form compatible to x86-64 assembly code and being expendable to allow 
	// other modules to add functionality to it in order to work with it more easily, this is just the base work done.
	// This IR takes the form of a control flow graph (cfg).
	class ControlFlowGraph {
		friend class LlirBasicBlock;
		friend class CfgGenerator;
	public:
		union CfgFlagsUnion {
			uint8_t FlatFlags;
			struct {
				uint8_t ContainsIncompletePaths : 1; // Set to indicate that a node contained was not fully resolved
	
				// Internal BFlags
				uint8_t ReservedCfgFlags        : 6; // Reserved flags (for completions sake)
				uint8_t TreeIsBeingTraversed    : 1; // Indicates that the graph is currently being traversed, 
													 // this flag is used to check for invocation of another traversal, 
													 // as due to the locking system nested traversals aren't possible.
			};
		};
		
		~ControlFlowGraph() {
			TRACE_FUNCTION_PROTO;

			std::vector<LlirBasicBlock*> Deleteables;
			TraverseOrLocateNodeBySpecializedDFS(SEARCH_PREORDER_NLR,
				[&Deleteables](
					IN LlirBasicBlock* BlockOfCfg
					) -> bool {
						TRACE_FUNCTION_PROTO;
						Deleteables.push_back(BlockOfCfg);
						return false;
				});
			for (auto Deletable : Deleteables)
				delete Deletable;
			SPDLOG_INFO("Released " ESC_BRIGHTCYAN"{}" ESC_RESET" blocks from cfg " ESC_BRIGHTRED"{}",
				Deleteables.size(),
				static_cast<void*>(this));
		}


		LlirBasicBlock* AllocateFloatingCfgNode() {
			TRACE_FUNCTION_PROTO;
	
			// The returned object is not managed, also doesnt have to be,
			// allocation is inaccessible from outside of CfgGenerator anyways.
			LlirBasicBlock* FloatingNode = new LlirBasicBlock(this);
			if (!InitialControlFlowGraphHead)
				InitialControlFlowGraphHead = FloatingNode;
			return FloatingNode;
		}
	
		LlirBasicBlock* GetInitialCfgNode() {
			TRACE_FUNCTION_PROTO; return InitialControlFlowGraphHead;
		}
	
	
		uint32_t ValidateCfgOverCrossReferences() { // This function validates the graph by walking over every single entry
													// and checking its cross references, and returns the number of violations
													// NOTE: this function is not efficient and should not be called in hot code paths
			TRACE_FUNCTION_PROTO;
	
			uint32_t ViolationCount{};
			TraverseOrLocateNodeBySpecializedDFS(SEARCH_PREORDER_NLR,
				[&ViolationCount](
					IN const LlirBasicBlock* TraversalNode
					) -> bool {
						TRACE_FUNCTION_PROTO;
	
						auto NodeXrefCheck = [&ViolationCount, TraversalNode](
							IN const LlirBasicBlock* RemoteNode
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
		using NodeModifierCallback = bool(INOUT LlirBasicBlock*);
		LlirBasicBlock* TraverseOrLocateNodeBySpecializedDFS(
			TreeTraversalMode                   SearchMode,
			std::function<NodeModifierCallback> CfgCallback
		) {
			TRACE_FUNCTION_PROTO;
	
			// Check if we are running a nested traversal, this is due to the locking mechanism not using a stack for each node,
			// this was done in order to aid with performance as a bit stack would allow nested traversal,
			// but would require a repaint before or after each search, in order to keep track of visited nodes.
			if (BFlags.TreeIsBeingTraversed)
				throw SingularityException("Cannot traverse graph within a traversal process, nested traversal is not supported",
					SingularityException::STATUS_NESTED_TRAVERSAL_DISALLOWED);
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
				throw SingularityException("DFS cannot return null pointer call or already explored node, to the driver",
					SingularityException::STATUS_DFS_INVALID_SEARCH_CALL);
			default:
				throw std::logic_error("DFS cannot return unsupported search result");
			}
		}
		LlirBasicBlock* FindNodeContainingVirtualAddress(
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
		LlirBasicBlock* FindNodeContainingVirtualAddressAndExclude(
			IN       TreeTraversalMode         SearchEvaluationMode,
			IN const byte_t* VirtualAddress,
			IN       std::span<const LlirBasicBlock*> NodeExclusions
		) {
			TRACE_FUNCTION_PROTO;
	
			return TraverseOrLocateNodeBySpecializedDFS(
				SearchEvaluationMode,
				[VirtualAddress, &NodeExclusions](
					IN const LlirBasicBlock* TraversalNode
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
			IN        LlirBasicBlock*               NextCfgNode;
			IN  std::function<NodeModifierCallback> CfgCallback;
			OUT LlirBasicBlock*                     ReturnTargetValue;
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
	
		LlirBasicBlock* InitialControlFlowGraphHead = nullptr;
		CfgFlagsUnion   BFlags{};
		uint32_t        RTLDFInitialSearchTagValue{};


		Llir3acOperandSymbolRegistry Llir3acOperandRegistry;
	};
}
