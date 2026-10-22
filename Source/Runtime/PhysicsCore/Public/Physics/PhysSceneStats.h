// PhysSceneStats.h - per-frame timing buckets and scene counters (02_Architecture 9.3; names after `stat ChaosCounters`).
// [MVP M1 types; the buckets are filled from M2 (FPhysScene -> ABI) and recorded from M3]
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	// [MVP] FPlatformTime (QPC) ticks converted to ns; [확장 M10] FCycleCounter (TSC) per bucket.
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
