module;

#include "sof/sof.h"

export module sof.amd64.lift;
import sof.llir;
import sof.amd64.disasm;
export namespace sof {

	// Do you even lift bro?
	class Amd64X64Lift {
	public:
		Amd64X64Lift(
			IN LlirControlFlowGraph& ApplyOnCfg
		)
			: LiftingObject(ApplyOnCfg) {
			TRACE_FUNCTION_PROTO;
		}

	private:
		void LiftCfgIntoIrForAmd64() {
			TRACE_FUNCTION_PROTO;


		}

		void LiftCfgNodeIntoIrForAmd64(
			IN LlirBasicBlock GraphNodeToLift
		) {
			TRACE_FUNCTION_PROTO;


		}

		LlirControlFlowGraph& LiftingObject;
	};




}
