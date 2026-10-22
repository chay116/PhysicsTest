// PhysSceneStats.h - per-frame timing buckets and scene counters (02_Architecture 9.3; names after `stat ChaosCounters`).
// [MVP M1 types; the buckets are filled from M2 (FPhysScene -> ABI) and recorded from M3]
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	// [MVP] FPlatformTime (QPC) ticks converted to ns; [확장 M10] FCycleCounter (TSC, rdtscp + lfence) per bucket: the
	// *Tsc fields are the raw tick counts, the *Ns fields those ticks converted with the process-wide calibration
	// (FCycleCounter::GetNsPerTick). The bench converts the raw ticks again with its per-run calibration (TimingContract 2.1).
	// T_frame = sum(T_push) + sum(T_step) + T_pull; T_step = setup + start + wait + end.
	struct FFrameTiming
	{
		uint64 PushNs = 0;
		uint64 SetupNs = 0;
		uint64 StartNs = 0;
		uint64 WaitNs = 0;
		uint64 EndNs = 0;
		uint64 StepNs = 0;
		uint64 PullNs = 0;
		uint64 FrameNs = 0;
		// M10: raw TSC ticks of the same buckets (FrameTsc = PushTsc + StepTsc + PullTsc).
		uint64 PushTsc = 0;
		uint64 SetupTsc = 0;
		uint64 StartTsc = 0;
		uint64 WaitTsc = 0;
		uint64 EndTsc = 0;
		uint64 StepTsc = 0;
		uint64 PullTsc = 0;
		uint64 FrameTsc = 0;
		// M7-M9: the QPC ticks of the buckets. M10: the buckets read the TSC; the bench fills this with the QPC ticks of the
		// whole Tick (two QPC reads around FPhysScene::Tick, outside the buckets: timings.csv frame_qpc_ticks). 0 in a scene's
		// own timing.
		uint64 FrameQpcTicks = 0;
		uint32 NumSteps = 0;
		uint32 EngineErrorBits = 0;
	};

	struct FPhysSceneStats
	{
		int32 NumBodies = 0;
		int32 NumDynamicBodies = 0;
		int32 NumKinematicBodies = 0;
		int32 NumStaticBodies = 0;
		int32 NumAwakeBodies = 0;            // dynamic bodies not asleep (object state Dynamic)
		int32 NumIslands = -1;               // -1 = not available
		int32 NumManifoldPoints = -1;
		int32 NumActiveManifoldPoints = -1;
		int32 NumConstraints = 0;            // spawned (FPhysScene::SpawnConstraint)
		// M9: spawned joints the backend cannot simulate (its PT_CAP_JOINT_* lacks their type): kept on the host only, never
		// sent (a scenario declares the capability it needs, so a bench run refuses such a pairing before it starts).
		int32 NumUnsupportedConstraints = 0;
		// Dynamic bodies with bStartAwake = false whose put-to-sleep step was skipped because the profile disables
		// sleeping (02 6: recorded in the manifest).
		int32 NumStartAsleepSkipped = 0;
		FFrameTiming Last;
		double FrameMsMin = 0.0;
		double FrameMsAvg = 0.0;
		double FrameMsP95 = 0.0;
		double FrameMsMax = 0.0;
	};
}
