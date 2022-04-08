// Singularity obfuscation Framework . Low Level Intermediate Representaiton
//
module;

#include <sof/sof.h>
#include <span>
#include <vector>
#include <ranges>
#include <iterator>

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

// The structure in memory:
// LLIR works on function level, each function is converted into its own LLIR module through control flow recovery,
// each module represents a strongly linked cyclic graph, where each node of the graph is a code block.
// Each Block represents a vector/list of statements, a statement in said list is a dynamic random access interface.
// The Statement must identify itself, and is addressed by either its physical or virtual address,
// where the physical address is its address in the image or a reserved value / undetermined value.
// A statement can be a representation of the native object or a translated object, in all cases the original may
// still be accessiable through any translation, a native object is convertable.
// translated objects (llir statements) describe a three address code form of a risc isa that can represent the
// isa derived from, lifters may be used to translate existing code and code can be manually inserted in post.
// Code within a block can be of mixed statements, and the block can be identified via its first entry.
// Each graph cna then be identified by its first block.
// Each graph owns a registry for symbols, said symbols are used to abstarct logic in the 3ac representation.
//

export namespace sof {

	// Basic native encoding container for xed to store its data in
	class LlirAmd64NativeEncoded {
	public:
		xed_decoded_inst_t DecodedInstruction;
		byte_t*            PhysicalVirtualAddress;

		union {
			uint8_t FlatFlags;
			struct {
				uint8_t InstructionPatched : 1; // The instruction was patched needs patching in memory
				uint8_t InstructionDirty   : 1; // instruction is completely dirty, needs re encode
				uint8_t InstructionLifted  : 1; // this instruction is kept for redundancy, and shall be ignored
				                                // as its lifted by the following entries
			};
		};

		const byte_t* GetUniquePhysicalAddress() const {
			TRACE_FUNCTION_PROTO;
			return reinterpret_cast<const byte_t*>(
				xed_decoded_inst_get_user_data(const_cast<xed_decoded_inst_t*>(
					&DecodedInstruction)));
		}


	};



	// Flag action statement, describes a flag option operation, made by instructions
	struct LlirLogicalFlag {
		enum FlagActionType : uint8_t {
			LLIR_FLAG_SET = 0,
			LLIR_FLAG_RESET,
			LLIR_FLAG_WRITE,
			LLIR_FLAG_TEST,
		} FlagAction;

		enum FlagFieldType : uint8_t {
			LLIR_FLAG_NOT_IMPLEMENTED = 0,
			LLIR_FLAG_CARRY,
			LLIR_FLAG_AUXILARRY_CARRY,
			LLIR_FLAG_PARITY_BIT,
			LLIR_FLAG_ZERO,
			LLIR_FLAG_SIGNED,
			LLIR_FLAG_OVERFLOW,
		} FlagField;
	};
	using LlirFlagActions = LlirLogicalFlag[12];

	// Abstracted Llir 3ac encoded operation
	class LlirBasicBlock;
	class Llir3AddressCode {
	public:
		enum LlirOpcode : int8_t {
			LLIR_OPCODE_NOP = 0,     // No command execute

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

		const byte_t*         PhysicalVirtualAddress;
		const LlirBasicBlock* OwningBasicBlock;
		      uint32_t        StatementGroupId;

		LlirFlagActions FlagsActionStates;
		LlirOpcode		OperationCode;

		enum {
			OPERAND_TARGET_ZERO = 0,
			OPERAND_SOURCE_ONE,
			OPERAND_SOURCE_TWO,
		};
		uint32_t SourceOperands[3];

		Llir3AddressCode* GetInitialSequenceStart() const;

		LlirAmd64NativeEncoded* GetOptionalInitialEncoding() const {
			TRACE_FUNCTION_PROTO; return nullptr;
		}
	};

	// A polymorphic base type stored as a list in cfg nodes used to identify the and define abstractions
	struct LlirInstructionStatement {
		enum CfgStatementType : int8_t {
			TYPE_NOT_ALLCOATED = 0,
			TYPE_PURE_NATIVE,
			TYPE_AMD64_LIFTED,
		};

		// Core data container, tagged
		// MSVC bug, the current ifc implementation is fucked and doesnt correctly inject
		// the union names into the correct visibility scope,
		// causing issues with access protections outside of this unit
		union {
		    LlirAmd64NativeEncoded NativeEncoding;
			Llir3AddressCode       IrEncoding;
		};

		CfgStatementType TypeOfStatement;

		const byte_t* GetUniquePhysicalAddress() const {
			TRACE_FUNCTION_PROTO;

			switch (TypeOfStatement) {
			case TYPE_PURE_NATIVE:
				return NativeEncoding.GetUniquePhysicalAddress();
			case TYPE_AMD64_LIFTED:
				return IrEncoding.PhysicalVirtualAddress;
			}
		}
	};



	class Llir3acOperand {
	public:
		Llir3acOperand(
			IN uint32_t UniqueIdentifier
		)
			: UniqueIdentifier(UniqueIdentifier) {
			TRACE_FUNCTION_PROTO;
		}

		const uint32_t UniqueIdentifier;

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



	// Describes a cfg's node, a core subcontainer of the whole cfg
	class LlirBasicBlock {
		friend class LlirControlFlowGraph;
		friend class CfgGenerator;
		friend Llir3AddressCode;
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
				uint8_t IsPureNativeNoLift : 1; // Indicates that there was no lifting being processed
												// on this node yet, the cfg container owns a similar flag

				// Internal CFlags
				uint8_t ReservedFlags : 5; // Reserved flags (for completions sake)
			};
		};


		LlirInstructionStatement& AllocateLlir3acObject() {
			TRACE_FUNCTION_PROTO;
			auto& Object = EncodeHybridStream.emplace_back();
			Object.TypeOfStatement = LlirInstructionStatement::TYPE_NOT_ALLCOATED;
			return Object;
		}



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
			IN byte_t* UidOfEntryToSplice
		);
		bool TrySpliceOfHeadAtAddressForInsert(       // Tries to splice of the node head at specified address and rebinds,
													  // returns true if successful or false if address was not found.
													  // Could also splice of the bottom but that would be even more work
			IN byte_t* OriginalAddressOfEntryToSplice // The physical address of the decode contained to splice the node at
		) {
			TRACE_FUNCTION_PROTO; throw std::runtime_error("THIS IS NO LONGER IMPLEMENTED");
		}


		void TerminateThisNodeAsCfgExit() {
			TRACE_FUNCTION_PROTO; CFlags.NodeTerminatesPath = true;
		}
		void SetNodeIsIncomplete();


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

		std::span<LlirInstructionStatement> GetCfgDecodeDataList() {
			TRACE_FUNCTION_PROTO; return EncodeHybridStream;
		}
		LlirBlockFlags GetCFlagsForNode() {
			TRACE_FUNCTION_PROTO; return CFlags;
		}
		const byte_t* GetPhysicalNodeStartAddress() const {
			TRACE_FUNCTION_PROTO; return EncodeHybridStream.front().GetUniquePhysicalAddress();
		}
		const byte_t* GetPhysicalNodeEndAddress() const { // End address includes the size of the instruction, 
														  // therefore point to the next instruction after this node
			TRACE_FUNCTION_PROTO;

			for (auto& SelectedBlock : EncodeHybridStream | std::views::reverse)
				switch (SelectedBlock.TypeOfStatement) {
				case LlirInstructionStatement::TYPE_PURE_NATIVE: {

					auto InstructionLength = xed_decoded_inst_get_length(&SelectedBlock.NativeEncoding.DecodedInstruction);
					return SelectedBlock.GetUniquePhysicalAddress() + InstructionLength;
				}
				case LlirInstructionStatement::TYPE_AMD64_LIFTED: {

					// Search for the next node that that has a native encoding associated
					auto NativeInstruction = SelectedBlock.IrEncoding.GetOptionalInitialEncoding();
					auto InstructionLength = xed_decoded_inst_get_length(
						&NativeInstruction->DecodedInstruction);
					return NativeInstruction->GetUniquePhysicalAddress() + InstructionLength;
				}}

			return nullptr;
		}

	private:
		LlirBasicBlock(
			IN LlirControlFlowGraph& OwningCfgContainer
		)
			: OwningCfgContainer(OwningCfgContainer) {
			TRACE_FUNCTION_PROTO;
		}

		LlirControlFlowGraph& OwningCfgContainer; // The ControlFlowGraph that allocated and owns this node
		LlirBlockFlags        CFlags{};           // A set of flags describing properties of this node
		uint32_t              NodeDFSearchTag{};  // An identifier used by the right to left depth first search
												  // to locate a node without traversing the same node twice

		// Hybrid Encoded data stream, this represents the code of the node
		// in a highly abstracted way that makes both native and lifted abstractions
		// easily available in a highly customizable way
		std::vector<LlirInstructionStatement> EncodeHybridStream;

		// Control flow node head, a list of nodes that can flow into this,
		// a linked non floating node will always have at least one entry.
		std::vector<LlirBasicBlock*> InputFlowNodeLinks;

		// There are 4(5) configurations in which ways this LR-pair could be set
		// 1.    Both are set, this nodes last entry is a conditional jump (jcc)
		// 2.    Left node is set, this node has a fall through into the next
		// 3.    The right node is set, this nodes last entry is a unconditional
		// 3.Ex: The right node is a node, node points to a jump table (jmp [+])
		// 4.    Non of the nodes are set, this node terminates a the node (ret)
		LlirBasicBlock*              NonBranchingLeftNode = nullptr;
		std::vector<LlirBasicBlock*> BranchingOutRightNode{};
	};

	Llir3AddressCode* Llir3AddressCode::GetInitialSequenceStart() const {
		TRACE_FUNCTION_PROTO;

		auto CurrentLocation = OwningBasicBlock->EncodeHybridStream.cbegin() +
			std::distance(OwningBasicBlock->EncodeHybridStream.data(),
				CONTAINING_RECORD(this,
					const LlirInstructionStatement,
					IrEncoding));
		return nullptr;
		
	}



	// The projects core component, this is the intermediate representation format the engine translates code into.
	// This representation can be used to transform and modify, adding and removing inclusive, the code loaded.
	// The IR is optimized for taking form compatible to x86-64 assembly code and being expendable to allow 
	// other modules to add functionality to it in order to work with it more easily, this is just the base work done.
	// This IR takes the form of a control flow graph (cfg).
	class LlirControlFlowGraph {
		friend class CfgGenerator;
		friend LlirBasicBlock;
	public:
		union CfgFlagsUnion {
			uint8_t FlatFlags;
			struct {
				uint8_t ContainsIncompletePaths : 1; // Set to indicate that a node contained was not fully resolved

				// Internal BFlags
				uint8_t ReservedCfgFlags : 6; // Reserved flags (for completions sake)
				uint8_t TreeIsBeingTraversed : 1; // Indicates that the graph is currently being traversed, 
													 // this flag is used to check for invocation of another traversal, 
													 // as due to the locking system nested traversals aren't possible.
			};
		};

		LlirControlFlowGraph(IN const LlirControlFlowGraph&) = delete;
		LlirControlFlowGraph& operator=(IN const LlirControlFlowGraph&) = delete;
		LlirControlFlowGraph(IN LlirControlFlowGraph&& Other) {
			TRACE_FUNCTION_PROTO; *this = std::move(Other);
		};
		LlirControlFlowGraph& operator=(IN LlirControlFlowGraph&& Other) {
			TRACE_FUNCTION_PROTO;
			OwningImageMapping = Other.OwningImageMapping;
			const_cast<FunctionAddress&>(FunctionLimits) = std::move(Other.FunctionLimits);
			InitialControlFlowGraphHead = Other.InitialControlFlowGraphHead;
			BFlags = Other.BFlags;
			RTLDFInitialSearchTagValue = Other.RTLDFInitialSearchTagValue;
			Llir3acOperandRegistry = std::move(Other.Llir3acOperandRegistry);
			NextSymbolAllocator = Other.NextSymbolAllocator;
			Other.InitialControlFlowGraphHead = nullptr;
			return *this;
		}

		~LlirControlFlowGraph() {
			TRACE_FUNCTION_PROTO;

			// Filter out move constructors
			if (!InitialControlFlowGraphHead)
				return;

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
			LlirBasicBlock* FloatingNode = new LlirBasicBlock(*this);
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
					IN LlirBasicBlock* TraversalNode
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
						return VirtualAddress >= TraversalNode->GetPhysicalNodeStartAddress()
							&& VirtualAddress < TraversalNode->GetPhysicalNodeEndAddress();
				});
		}

		Llir3acOperand* LocateAndRetrieve3acOperandForSymbol(
			IN uint32_t UniqueIdentifier
		) {
			TRACE_FUNCTION_PROTO;
			auto OperandIterator = std::find_if(Llir3acOperandRegistry.begin(),
				Llir3acOperandRegistry.end(),
				[UniqueIdentifier](
					IN const decltype(Llir3acOperandRegistry)::value_type& OperandRegistryEntry
					) -> bool {
						TRACE_FUNCTION_PROTO; return OperandRegistryEntry.UniqueIdentifier == UniqueIdentifier;
				});
			return OperandIterator == Llir3acOperandRegistry.end() ? nullptr : &*OperandIterator;
		}

	private:
		LlirControlFlowGraph(
			IN const ImageProcessor&  ImageMapping,
			IN const FunctionAddress& FunctionLimits
		)
			: OwningImageMapping(&ImageMapping),
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

		Llir3acOperand& Allocate3acOperandEnlist() {
			TRACE_FUNCTION_PROTO; return Llir3acOperandRegistry.emplace_back(++NextSymbolAllocator);
		}
		const ImageProcessor* OwningImageMapping;
		const FunctionAddress FunctionLimits;

		LlirBasicBlock* InitialControlFlowGraphHead = nullptr;
		CfgFlagsUnion   BFlags{};
		uint32_t        RTLDFInitialSearchTagValue{};

		std::vector<Llir3acOperand> Llir3acOperandRegistry;
		uint32_t                    NextSymbolAllocator = -1;
	};

	bool LlirBasicBlock::TrySpliceOfHeadAtAddressForInsert2(
		IN byte_t* UidOfEntryToSplice
	) {
		TRACE_FUNCTION_PROTO;

		// Find the iterator of the entry containing the splice address
		auto SpliceIterator = std::find_if(EncodeHybridStream.begin(),
			EncodeHybridStream.end(),
			[UidOfEntryToSplice](
				IN const LlirInstructionStatement& CfgNodeEntry
				) -> bool {
					// TODO: INVALID, needs fixups, addresses shall not be used for identification purposes
					TRACE_FUNCTION_PROTO; return CfgNodeEntry.GetUniquePhysicalAddress() == UidOfEntryToSplice;
			});
		if (SpliceIterator == EncodeHybridStream.end())
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
		SplicedNodeHead->EncodeHybridStream.insert(SplicedNodeHead->EncodeHybridStream.begin(),
			std::make_move_iterator(EncodeHybridStream.begin()),
			std::make_move_iterator(SpliceIterator));
		EncodeHybridStream.erase(EncodeHybridStream.begin(),
			SpliceIterator);
		SplicedNodeHead->NonBranchingLeftNode = this;

		// Security checks, verify nodes contain data
		if (SplicedNodeHead->EncodeHybridStream.empty() || EncodeHybridStream.empty())
			throw SingularityException(
				fmt::format("Splicing of node {} into {}, cannot result in empty node",
					static_cast<void*>(this),
					static_cast<void*>(SplicedNodeHead)),
				SingularityException::STATUS_SPLICING_FAILED_EMPTY_NODE);

		return true;
	}
	void LlirBasicBlock::SetNodeIsIncomplete() {
		TRACE_FUNCTION_PROTO;

		// If this function is called the node will be set as incomplete, be terminated
		// and the holding code flow graph will be set as incomplete
		TerminateThisNodeAsCfgExit();
		CFlags.NodeIsIncomplete = true;
		OwningCfgContainer.BFlags.ContainsIncompletePaths = true;
		SPDLOG_WARN("CfgNode for rva{:x} was marked incomplete at +{:x}",
			OwningCfgContainer.FunctionLimits.first -
			OwningCfgContainer.OwningImageMapping->GetImageFileMapping(),
			EncodeHybridStream.back().GetUniquePhysicalAddress() - // TODO: this is hardcoded for x86, may need to change this up in the future
				OwningCfgContainer.OwningImageMapping->GetImageFileMapping());
	}
}
