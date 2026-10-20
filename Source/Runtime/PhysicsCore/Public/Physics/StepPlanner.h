// StepPlanner.h - how one frame becomes engine steps, for every tick mode (02_Architecture 9.2, DL09). [MVP M1]
// All modes are implemented and pinned by unit tests (PhysicsTest.StepPlanner.*); MVP scenes use FixedStep and Sync.
//
// The planner replays UE's own arithmetic, in UE's types:
//   FChaosScene::SetUpForFrame (ChaosScene.cpp:331-361 [V])        float: MDeltaTime = min(dt, MaxDt) or, when
//                                                                   substepping, min(dt, MaxSubsteps * MaxSubDt);
//                                                                   solver MaxDt / MaxSubSteps / MinDt (>= UE_SMALL_NUMBER)
//   FPhysicsSolverBase::AdvanceAndDispatch_External                 FReal (double): fixed-dt accumulator (AsyncFixed)
//     (PhysicsSolverBase.cpp:581-621 [V])                           or N = ceil(dt / MaxDt), N > MaxSubSteps ->
//                                                                   N = MaxSubSteps steps of MaxDt (time lost)
//   FPBDRigidsSolver advance loop (PBDRigidsSolver.cpp:554-575 [V]) per pushed step: step only while the remaining
//                                                                   time exceeds MinDeltaTime; the remainder is
//                                                                   dropped, never carried to the next frame
//   UE4 FPhysSubstepTask::UpdateTime (PhysSubstepTasks.cpp:211-227 [V], dead code in 5.8.3) for
//     ESubstepClampRule::UE4PhysSubstep, after the UE4 pre-clamp UseDelta = min(dt, MaxPhysicsDeltaTime) [M]
//
// Fixed values (03_WorkPlan M1): dt 0.05 Sync -> 1 x 1/30; UE5 substep -> 3 x 1/60 (ceil input 2.99999989);
// UE4 substep -> 2 x 1/60; dt 0.2 UE5 -> 6 x 1/60 with 0.1 s lost; AsyncFixed 1/30 at 1/60 frames -> 0, 1, 0, 1, ...
//
// The harness itself is compiled with /fp:fast like a UE game target (VCToolChain.cs:1324-1335, docs/04 Q12 note):
// the planner works on runtime values, so no float -> double conversion here can be constant-folded.
// AsyncFixed note: Chaos runs its fixed-dt accumulator only when IsUsingFixedDt() = IsUsingAsyncResults() &&
// p.UseAsyncInterpolation (PhysicsSolverBase.h:712-720 [V]); the game thread then sees state interpolated one step
// late (PhysicsSolverBaseImpl.h:51-67). SDK backends step exactly NumSteps; the plan carries bAsyncInterpolated so
// results can be labelled and excluded from per-body comparison (02 9.2, DL40).
#pragma once

#include "PhysicsEngine/PhysicsSettings.h"

namespace PhysicsTest
{
	// The arguments of FChaosScene::SetUpForFrame as UWorld passes them (PhysLevel.cpp:194-212 [V]). A Native-driver
	// backend (Chaos, Null) receives exactly these once per frame.
	struct FSetUpForFrameParams
	{
		float DeltaSeconds = 0.0f;
		float MinPhysicsDeltaTime = 0.0f;
		float MaxPhysicsDeltaTime = 0.0f;
		float MaxSubstepDeltaTime = 0.0f;
		int32 MaxSubsteps = 1;
		bool bSubstepping = false;
	};

	struct FStepPlan
	{
		int32 NumSteps = 0;               // engine steps this frame (UE: AdvanceOneTimeStep calls)
		int32 NumExternalSteps = 0;       // UE: steps pushed by AdvanceAndDispatch_External (a pushed step whose dt is
		                                  // not above MinDeltaTime simulates nothing)
		float FrameDeltaTime = 0.0f;      // the frame dt after SetUpForFrame's clamp (UE5: FChaosScene::MDeltaTime, the dt
		                                  // OnPhysScenePreTick receives, ChaosScene.cpp:331-374 [V]; UE4 rule: UseDelta [M])
		float StepDeltaTime = 0.0f;       // dt of every engine step as the engines receive it (the ABI carries a float)
		float ConsumedTime = 0.0f;        // NumSteps * StepDeltaTime
		float LostTime = 0.0f;            // frame time removed by the clamps or dropped below MinDeltaTime (not carried)
		bool bAsyncInterpolated = false;  // AsyncFixed: Chaos GT state is interpolated one step late
		FSetUpForFrameParams Params;      // the UE arguments this plan was derived from
	};

	class FStepPlanner
	{
	public:
		// UWorld's arguments for the harness tick mode:
		//   FixedStep   (FixedDeltaTime, MinDt, 0, 0, 1, false)        - frame dt ignored, no clamp (MaxDt <= 0)
		//   Sync        (dt, MinDt, MaxPhysicsDeltaTime, MaxSubDt, MaxSubsteps, false)
		//   Substepping (dt, MinDt, MaxPhysicsDeltaTime, MaxSubDt, MaxSubsteps, true)
		//   AsyncFixed  (dt, MinDt, MaxPhysicsDeltaTime, MaxSubDt, MaxSubsteps, false) + the scene's AsyncDt
		[[nodiscard]] static FSetUpForFrameParams MakeSetUpForFrameParams(float FrameDeltaTime, const FPhysicsSettings& Settings,
			const FPhysicsTestSettings& TestSettings);

		// The scene's async dt (FChaosScene(nullptr, AsyncDt)): AsyncFixedTimeStepSize for AsyncFixed, otherwise -1
		// (sync; the C ABI rejects 0, DL40).
		[[nodiscard]] static double GetAsyncDeltaTime(const FPhysicsSettings& Settings, const FPhysicsTestSettings& TestSettings);

		// One frame. InOutAccumulatedTime is the AsyncFixed accumulator (Chaos AccumulatedTime, FReal); it is untouched
		// in the other modes. UE4PhysSubstep applies to substepping without async only; otherwise UE5 rules apply.
		[[nodiscard]] static FStepPlan PlanFrame(const FSetUpForFrameParams& Params, double AsyncDeltaTime, ESubstepClampRule ClampRule,
			double& InOutAccumulatedTime);
		[[nodiscard]] static FStepPlan PlanFrame(float FrameDeltaTime, const FPhysicsSettings& Settings, const FPhysicsTestSettings& TestSettings,
			double& InOutAccumulatedTime);
	};
}
