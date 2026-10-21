// AbiGates.h - the ABI gates of 03_WorkPlan M2 (02_Architecture 14.2, DL28, docs/Abi.md). [MVP M2; re-run at the M6 freeze]
//   A0 lifecycle            GetInfo, Init / Term state machine, double Init -> BAD_STATE, module-level entries before Init
//                           -> BAD_STATE, create / destroy x10, 16 slots
//   A1 version handshake    the host's major -> the table, major + 1 -> NULL, StructSize / AbiVersion / entries checked
//   A2 other thread         every entry from another thread -> PT_E_WRONG_THREAD, no output written, no state change
//   A3 phase state machine  Idle -> SetUp -> Started -> Waited -> Idle; anything else -> BAD_STATE without a side effect
//   A4 input validation     a table of 70 negative cases (4 module + 66 scene; no fuzzing: the 10k fuzz is M15); all
//                           rejected, no crash, the scene unchanged afterwards (AsyncDt = 0 -> PT_E_INVALID_ARG, released
//                           bodies, double and repeated releases, ACTIVE_ONLY without OutIndices among them)
//   A5 capacity / stride    the two-call protocol (count / bytes written back, buffers untouched), the dense PT_READ_ALL
//                           after a release, and padded strides
//   A6 FP environment       MXCSR control bits and the x87 control word survive every call, and the Kit had nothing to
//                           restore (no 'changed MXCSR' warning, kit.mxcsr_restores unchanged)
//   A7 effective keys       the generic set + the backend's declared set, each line well-formed
//   A9 (DLL hygiene) is Tools/check_modules.py + the PtAbiUeCompatCheck build (CTest), not a function here.
//   A10 frame model         see RunA10FrameModel.
// Every A0-A7 gate drives a backend's raw C table. Precondition: the module is not initialised; postcondition: it is
// terminated again (PrepareAbiGateTarget gets there through the registry module).
#pragma once

#include "ConformanceGate.h"
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <string>
#include <string_view>

namespace PhysicsTest
{
	class FPhysicsBackendModule;

	namespace AbiGates
	{
		struct FAbiGateTarget
		{
			std::string Label;                       // "Null (static)", "NullDll (DLL)"
			std::string BackendName;                 // registry name
			FPhysicsBackendModule* Module = nullptr;
			const PtBackendApi* Api = nullptr;
			PtGetBackendApiFn Entry = nullptr;       // the static entry or the DLL's PT_GetBackendApi
		};

		// Loads BackendName through FPhysicsBackendRegistry and terminates the module if a scene of an earlier test left it
		// initialised (no scene proxy may be alive). False + OutError when the backend is not available.
		bool PrepareAbiGateTarget(std::string_view BackendName, FAbiGateTarget& Out, std::string& OutError);

		FGateReport RunA0Lifecycle(const FAbiGateTarget& Target);
		FGateReport RunA1VersionHandshake(const FAbiGateTarget& Target);
		FGateReport RunA2OtherThread(const FAbiGateTarget& Target);
		FGateReport RunA3PhaseStateMachine(const FAbiGateTarget& Target);
		FGateReport RunA4NegativeCases(const FAbiGateTarget& Target);
		FGateReport RunA5CapacityAndStride(const FAbiGateTarget& Target);
		FGateReport RunA6FloatingPointState(const FAbiGateTarget& Target);
		FGateReport RunA7EffectiveKeys(const FAbiGateTarget& Target);

		// A10 frame model (needs PT_CAP_NATIVE_FRAME_MODEL; M2 runs it on Null, M6 re-runs it as a freeze condition):
		//   (a) Sync, Substepping (UE5ChaosScene rule) and AsyncFixed fixed inputs through the raw ABI: every frame's
		//       PtFrameResult::NumStepsTaken equals the harness FStepPlanner's FStepPlan::NumSteps
		//   (b) FixedStep and Sync through FPhysScene: the Native driver and the forced Harness driver end in bit-identical
		//       body states, native hash and SimTime (bodies with gravity, damping, spin, impulses, forces, sleep / wake,
		//       kinematic targets through every entry point), frames that run no step (dt 1e-9, 0) included, plus random
		//       command scripts over random frame dts (6 seeds)
		//   (c) Substepping and AsyncFixed through FPhysScene with the Native driver run without a step-count mismatch
		//   (d) Substepping and AsyncFixed, Native vs Harness driver: the same steps and object states, states within the
		//       tolerances of the step dt (and, with torques in the random scripts, of the per-step world inertia)
		// The module is initialised once, through the registry module with FPhysScene's default init arguments, and (a)-(d) all
		// run on that initialisation (a PT_MODULE_NO_REINIT backend cannot be initialised again after a Term); it is left
		// terminated.
		FGateReport RunA10FrameModel(std::string_view BackendName);
	}
}
