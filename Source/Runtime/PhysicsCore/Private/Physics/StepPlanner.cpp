// StepPlanner.cpp - UE's frame-to-step arithmetic in UE's types (float where UE uses float, FReal = double where Chaos
// does). The sources each block mirrors are cited inline; the code is our own.
#include "Physics/StepPlanner.h"

#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace PhysicsTest
{
	namespace
	{
		// One pushed step through the solver's advance loop (PBDRigidsSolver.cpp:560-575 [V]). Returns the number of
		// engine steps (0 or 1 in every configuration UE builds: StepsRemaining is 1 when the solver substeps, and
		// MaxSubSteps is 1 otherwise) and their dt; OutRemaining is the dropped remainder.
		int32 AdvancePushedStep(const double InternalDt, const float SolverMaxDt, const float SolverMinDt, const int32 SolverMaxSubSteps,
			const bool bSolverSubstepped, double& OutStepDt, double& OutRemaining)
		{
			int32 StepsRemaining = bSolverSubstepped ? 1 : SolverMaxSubSteps;
			double TimeRemaining = InternalDt;
			const double MinDeltaTime = SolverMinDt;
			const double MaxDeltaTime = SolverMaxDt;
			int32 Steps = 0;
			OutStepDt = 0.0;
			while (StepsRemaining > 0 && TimeRemaining > MinDeltaTime)
			{
				--StepsRemaining;
				const double DeltaTime = MaxDeltaTime > 0.0 ? FMath::Min(TimeRemaining, MaxDeltaTime) : TimeRemaining;
				TimeRemaining -= DeltaTime;
				if (Steps == 0)
				{
					OutStepDt = DeltaTime;
				}
				++Steps;
			}
			OutRemaining = TimeRemaining;
			return Steps;
		}

		void Finish(FStepPlan& Plan, const int32 StepsPerPushed, const double StepDt, const double LostSeconds)
		{
			Plan.NumSteps = Plan.NumExternalSteps * StepsPerPushed;
			Plan.StepDeltaTime = Plan.NumSteps > 0 ? static_cast<float>(StepDt) : 0.0f;
			Plan.ConsumedTime = static_cast<float>(static_cast<double>(Plan.NumSteps) * static_cast<double>(Plan.StepDeltaTime));
			Plan.LostTime = static_cast<float>(LostSeconds > 0.0 ? LostSeconds : 0.0);
		}

		// UE5: FChaosScene::SetUpForFrame + FPhysicsSolverBase::AdvanceAndDispatch_External + the advance loop.
		FStepPlan PlanUE5(const FSetUpForFrameParams& Params, const double AsyncDeltaTime, double& InOutAccumulatedTime)
		{
			FStepPlan Plan;
			Plan.Params = Params;

			// ChaosScene.cpp:331-361 [V] (float). MNetworkDeltaTimeScale is 1.
			const float InDeltaSeconds = Params.DeltaSeconds;
			float MDeltaTime = 0.0f;
			if (Params.bSubstepping)
			{
				MDeltaTime = Params.MaxSubstepDeltaTime > 0.0f
					? FMath::Min(InDeltaSeconds, static_cast<float>(Params.MaxSubsteps) * Params.MaxSubstepDeltaTime)
					: InDeltaSeconds;
			}
			else
			{
				MDeltaTime = Params.MaxPhysicsDeltaTime > 0.0f ? FMath::Min(InDeltaSeconds, Params.MaxPhysicsDeltaTime) : InDeltaSeconds;
			}
			Plan.FrameDeltaTime = MDeltaTime;
			const float SolverMaxDt = Params.bSubstepping ? Params.MaxSubstepDeltaTime : Params.MaxPhysicsDeltaTime;
			const int32 SolverMaxSubSteps = Params.bSubstepping ? Params.MaxSubsteps : 1;
			const float SolverMinDt = FMath::Max(Params.MinPhysicsDeltaTime, UE_SMALL_NUMBER);   // PhysicsSolverBase.h:727-730

			// PhysicsSolverBase.cpp:581-621 [V] (FReal = double).
			const bool bSolverSubstepped = SolverMaxSubSteps > 1;
			const double InDt = MDeltaTime;
			double InternalDt = InDt;
			int32 NumSteps = 1;
			double Lost = static_cast<double>(InDeltaSeconds) - InDt;   // removed by the SetUpForFrame clamp
			if (AsyncDeltaTime >= 0.0)
			{
				// IsUsingFixedDt(): async results with p.UseAsyncInterpolation = 1 (the UE default).
				Plan.bAsyncInterpolated = true;
				InOutAccumulatedTime += InDt;
				if (InDt == 0.0)
				{
					// Special flush case: use the remaining time and sync up.
					InternalDt = InOutAccumulatedTime;
					NumSteps = 1;
					InOutAccumulatedTime = 0.0;
				}
				else
				{
					InternalDt = AsyncDeltaTime;
					NumSteps = static_cast<int32>(std::floor(InOutAccumulatedTime / InternalDt));
					InOutAccumulatedTime -= InternalDt * static_cast<double>(NumSteps);
				}
			}
			else if (bSolverSubstepped && InDt > 0.0)
			{
				if (SolverMaxDt > 0.0f)
				{
					NumSteps = static_cast<int32>(std::ceil(InDt / static_cast<double>(SolverMaxDt)));
					if (NumSteps > SolverMaxSubSteps)
					{
						// Losing time: MaxSubSteps steps of MaxDt cannot cover dt.
						NumSteps = SolverMaxSubSteps;
						InternalDt = SolverMaxDt;
					}
					else
					{
						InternalDt = InDt / static_cast<double>(NumSteps);
					}
				}
				// MaxSubstepDeltaTime <= 0 while substepping divides by zero in UE; the harness takes one step of dt.
			}
			Plan.NumExternalSteps = NumSteps > 0 ? NumSteps : 0;
			if (AsyncDeltaTime < 0.0)
			{
				Lost += InDt - InternalDt * static_cast<double>(Plan.NumExternalSteps);
			}

			double StepDt = 0.0;
			double Remaining = 0.0;
			const int32 StepsPerPushed = Plan.NumExternalSteps > 0
				? AdvancePushedStep(InternalDt, SolverMaxDt, SolverMinDt, SolverMaxSubSteps, bSolverSubstepped, StepDt, Remaining)
				: 0;
			Lost += Remaining * static_cast<double>(Plan.NumExternalSteps);
			Finish(Plan, StepsPerPushed, StepDt, Lost);
			return Plan;
		}

		// UE4: FPhysScene_PhysX::SetUpForFrame pre-clamp [M] + FPhysSubstepTask::UpdateTime (PhysSubstepTasks.cpp:211-227 [V]),
		// all float. The skip below MinPhysicsDeltaTime is applied as in the UE5 advance loop [R].
		FStepPlan PlanUE4Substep(const FSetUpForFrameParams& Params)
		{
			FStepPlan Plan;
			Plan.Params = Params;
			const float UseDelta = Params.MaxPhysicsDeltaTime > 0.0f ? FMath::Min(Params.DeltaSeconds, Params.MaxPhysicsDeltaTime) : Params.DeltaSeconds;
			Plan.FrameDeltaTime = UseDelta;   // UE4 FPhysScene_PhysX broadcast UseDelta = min(dt, MaxPhysicsDeltaTime) [M]
			const float FrameRate = Params.MaxSubstepDeltaTime;
			const uint32 MaxSubSteps = static_cast<uint32>(FMath::Max(Params.MaxSubsteps, 1));
			const float FrameRateInv = 1.0f / FrameRate;
			const float DeltaSeconds = FMath::Min(UseDelta, static_cast<float>(MaxSubSteps) * FrameRate);
			uint32 NumSubsteps = static_cast<uint32>(static_cast<int32>(std::ceil(DeltaSeconds * FrameRateInv)));
			NumSubsteps = FMath::Max(NumSubsteps > MaxSubSteps ? MaxSubSteps : NumSubsteps, 1u);
			const float SubTime = DeltaSeconds / static_cast<float>(NumSubsteps);

			const float MinDt = FMath::Max(Params.MinPhysicsDeltaTime, UE_SMALL_NUMBER);
			Plan.NumExternalSteps = static_cast<int32>(NumSubsteps);
			const bool bStep = static_cast<double>(SubTime) > static_cast<double>(MinDt);
			double Lost = static_cast<double>(Params.DeltaSeconds) - static_cast<double>(DeltaSeconds);
			if (!bStep)
			{
				Lost += static_cast<double>(DeltaSeconds);
			}
			Finish(Plan, bStep ? 1 : 0, SubTime, Lost);
			return Plan;
		}
	}

	FSetUpForFrameParams FStepPlanner::MakeSetUpForFrameParams(const float FrameDeltaTime, const FPhysicsSettings& Settings,
		const FPhysicsTestSettings& TestSettings)
	{
		FSetUpForFrameParams Params;
		Params.MinPhysicsDeltaTime = Settings.MinPhysicsDeltaTime;
		switch (TestSettings.TickMode)
		{
		case EPhysicsTickMode::FixedStep:
			// Harness mode: the engine always gets FixedDeltaTime; MaxDt <= 0 means "no clamp" in Chaos
			// (ChaosScene.cpp:341-345, PBDRigidsSolver.cpp:569 [V]).
			Params.DeltaSeconds = TestSettings.FixedDeltaTime;
			Params.MaxPhysicsDeltaTime = 0.0f;
			Params.MaxSubstepDeltaTime = 0.0f;
			Params.MaxSubsteps = 1;
			Params.bSubstepping = false;
			break;
		case EPhysicsTickMode::Sync:
		case EPhysicsTickMode::AsyncFixed:
		case EPhysicsTickMode::Substepping:
			Params.DeltaSeconds = FrameDeltaTime;
			Params.MaxPhysicsDeltaTime = Settings.MaxPhysicsDeltaTime;
			Params.MaxSubstepDeltaTime = Settings.MaxSubstepDeltaTime;
			Params.MaxSubsteps = Settings.MaxSubsteps;
			Params.bSubstepping = TestSettings.TickMode == EPhysicsTickMode::Substepping;
			break;
		}
		return Params;
	}

	double FStepPlanner::GetAsyncDeltaTime(const FPhysicsSettings& Settings, const FPhysicsTestSettings& TestSettings)
	{
		return TestSettings.TickMode == EPhysicsTickMode::AsyncFixed ? static_cast<double>(Settings.AsyncFixedTimeStepSize) : -1.0;
	}

	FStepPlan FStepPlanner::PlanFrame(const FSetUpForFrameParams& Params, const double AsyncDeltaTime, const ESubstepClampRule ClampRule,
		double& InOutAccumulatedTime)
	{
		if (ClampRule == ESubstepClampRule::UE4PhysSubstep && Params.bSubstepping && AsyncDeltaTime < 0.0 && Params.MaxSubstepDeltaTime > 0.0f)
		{
			return PlanUE4Substep(Params);
		}
		return PlanUE5(Params, AsyncDeltaTime, InOutAccumulatedTime);
	}

	FStepPlan FStepPlanner::PlanFrame(const float FrameDeltaTime, const FPhysicsSettings& Settings, const FPhysicsTestSettings& TestSettings,
		double& InOutAccumulatedTime)
	{
		return PlanFrame(MakeSetUpForFrameParams(FrameDeltaTime, Settings, TestSettings), GetAsyncDeltaTime(Settings, TestSettings),
			TestSettings.SubstepClampRule, InOutAccumulatedTime);
	}
}
