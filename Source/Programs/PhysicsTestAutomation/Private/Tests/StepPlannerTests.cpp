// StepPlannerTests.cpp - FStepPlanner fixed values (03_WorkPlan M1, 02_Architecture 9.2) and the dt_engine analytic
// references (DL23). Times are compared as the floats the engines receive.
#include "CoreMinimal.h"

#include "Physics/AnalyticReference.h"
#include "Physics/StepPlanner.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <string>

using namespace PhysicsTest;

namespace
{
	// Runtime float constants: the test compares against the values the engines see, not a folded 1/60.
	float RuntimeFloat(const float Value)
	{
		volatile float Stored = Value;
		return Stored;
	}

	const float Sixtieth = RuntimeFloat(1.0f / 60.0f);
	const float Thirtieth = RuntimeFloat(1.0f / 30.0f);

	FStepPlan Plan(const EPhysicsTickMode Mode, const float FrameDt, const ESubstepClampRule Rule = ESubstepClampRule::UE5ChaosScene,
		const FPhysicsSettings& Settings = FPhysicsSettings())
	{
		FPhysicsTestSettings TestSettings;
		TestSettings.TickMode = Mode;
		TestSettings.SubstepClampRule = Rule;
		double Accumulator = 0.0;
		return FStepPlanner::PlanFrame(FrameDt, Settings, TestSettings, Accumulator);
	}
}

TEST_CASE("PhysicsTest.StepPlanner.SyncClampsToMaxPhysicsDeltaTime")
{
	// dt 0.05, Sync -> 1 x 1/30 (MaxPhysicsDeltaTime, ChaosScene.cpp:343).
	const FStepPlan P = Plan(EPhysicsTickMode::Sync, RuntimeFloat(0.05f));
	CHECK(P.NumSteps == 1);
	CHECK(P.StepDeltaTime == Thirtieth);
	CHECK(P.FrameDeltaTime == Thirtieth);   // MDeltaTime = min(dt, MaxPhysicsDeltaTime): what PreTick receives
	CHECK(P.LostTime == doctest::Approx(0.05 - 1.0 / 30.0).epsilon(1e-6));
	CHECK_FALSE(P.bAsyncInterpolated);
	// Below the clamp the frame dt passes through.
	const FStepPlan Small = Plan(EPhysicsTickMode::Sync, Sixtieth);
	CHECK(Small.NumSteps == 1);
	CHECK(Small.StepDeltaTime == Sixtieth);
	CHECK(Small.FrameDeltaTime == Sixtieth);
	CHECK(Small.LostTime == 0.0f);
}

TEST_CASE("PhysicsTest.StepPlanner.UE5SubstepCeil")
{
	// dt 0.05, UE5ChaosScene -> 3 x 1/60. The ceil input is (double)0.05f / (double)(float)(1/60) = 2.99999989.
	const double CeilInput = static_cast<double>(RuntimeFloat(0.05f)) / static_cast<double>(Sixtieth);
	CHECK(CeilInput < 3.0);
	CHECK(CeilInput == doctest::Approx(2.99999989).epsilon(1e-9));
	const FStepPlan P = Plan(EPhysicsTickMode::Substepping, RuntimeFloat(0.05f));
	CHECK(P.NumSteps == 3);
	CHECK(P.NumPushedSteps == 3);
	CHECK(P.StepDeltaTime == Sixtieth);
	// What Chaos actually steps with is the FReal D / N (PhysicsSolverBase.cpp:609-612): 0.016666666915, not the
	// float 0.016666667536 the Harness driver sends through the ABI (relative difference 3.7e-8).
	volatile double Three = 3.0;   // a runtime divisor: /fp:fast may turn a division by a constant into a multiplication
	const double InternalDt = static_cast<double>(RuntimeFloat(0.05f)) / Three;
	CHECK(P.InternalStepDeltaTime == InternalDt);
	CHECK(P.StepDeltaTime == static_cast<float>(InternalDt));
	CHECK(P.InternalStepDeltaTime != static_cast<double>(P.StepDeltaTime));
	CHECK(std::abs(P.InternalStepDeltaTime / static_cast<double>(P.StepDeltaTime) - 1.0) == doctest::Approx(3.7e-8).epsilon(0.05));
	CHECK(P.FrameDeltaTime == RuntimeFloat(0.05f));   // min(dt, MaxSubsteps * MaxSubstepDeltaTime) = dt
	CHECK(P.LostTime < 1e-7f);
	CHECK(P.Params.bSubstepping);
	CHECK(P.Params.MaxSubsteps == 6);
}

TEST_CASE("PhysicsTest.StepPlanner.UE4SubstepPreClamp")
{
	// dt 0.05, UE4PhysSubstep -> 2 x 1/60: UseDelta = min(dt, MaxPhysicsDeltaTime) = 1/30 first [M], then
	// ceil(1/30 * (1 / (1/60))) = 2 (PhysSubstepTasks.cpp:211-227).
	const FStepPlan P = Plan(EPhysicsTickMode::Substepping, RuntimeFloat(0.05f), ESubstepClampRule::UE4PhysSubstep);
	CHECK(P.NumSteps == 2);
	CHECK(P.StepDeltaTime == Sixtieth);
	CHECK(P.FrameDeltaTime == Thirtieth);   // UE4 UseDelta [M]
	CHECK(P.LostTime == doctest::Approx(0.05 - 1.0 / 30.0).epsilon(1e-6));
	// The rule only matters for substepping: Sync is identical under both rules.
	const FStepPlan Sync4 = Plan(EPhysicsTickMode::Sync, RuntimeFloat(0.05f), ESubstepClampRule::UE4PhysSubstep);
	const FStepPlan Sync5 = Plan(EPhysicsTickMode::Sync, RuntimeFloat(0.05f), ESubstepClampRule::UE5ChaosScene);
	CHECK(Sync4.NumSteps == Sync5.NumSteps);
	CHECK(Sync4.StepDeltaTime == Sync5.StepDeltaTime);
}

TEST_CASE("PhysicsTest.StepPlanner.UE5LosesTimeBeyondMaxSubsteps")
{
	// dt 0.2, UE5ChaosScene -> 6 x 1/60 and 0.1 s lost (PhysicsSolverBase.cpp:606-611: NumSteps > MaxSubSteps ->
	// MaxSubSteps steps of MaxDt).
	const FStepPlan P = Plan(EPhysicsTickMode::Substepping, RuntimeFloat(0.2f));
	CHECK(P.NumSteps == 6);
	CHECK(P.StepDeltaTime == Sixtieth);
	CHECK(P.ConsumedTime == doctest::Approx(0.1).epsilon(1e-6));
	CHECK(P.LostTime == doctest::Approx(0.1).epsilon(1e-5));
	CHECK(P.FrameDeltaTime == doctest::Approx(0.1).epsilon(1e-6));   // min(0.2, 6 * 1/60) in float
	// UE4 for the same frame: UseDelta = min(0.2, 1/30) first, so 2 x 1/60 - the rules differ here too.
	const FStepPlan P4 = Plan(EPhysicsTickMode::Substepping, RuntimeFloat(0.2f), ESubstepClampRule::UE4PhysSubstep);
	CHECK(P4.NumSteps == 2);
	CHECK(P4.StepDeltaTime == Sixtieth);
}

TEST_CASE("PhysicsTest.StepPlanner.AsyncFixedAlternates")
{
	// AsyncFixed 1/30 at 1/60 frames -> 0, 1, 0, 1, ... steps of 1/30 (PhysicsSolverBase.cpp:591-605).
	FPhysicsSettings Settings;
	FPhysicsTestSettings TestSettings;
	TestSettings.TickMode = EPhysicsTickMode::AsyncFixed;
	CHECK(FStepPlanner::GetAsyncDeltaTime(Settings, TestSettings) == static_cast<double>(Thirtieth));
	double Accumulator = 0.0;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		const FStepPlan P = FStepPlanner::PlanFrame(Sixtieth, Settings, TestSettings, Accumulator);
		CAPTURE(Frame);
		CHECK(P.NumSteps == (Frame % 2 == 0 ? 0 : 1));
		CHECK(P.bAsyncInterpolated);
		if (P.NumSteps > 0)
		{
			CHECK(P.StepDeltaTime == Thirtieth);
			CHECK(Accumulator == 0.0);
		}
		else
		{
			CHECK(Accumulator == static_cast<double>(Sixtieth));
		}
		CHECK(P.LostTime == 0.0f);   // time waits in the accumulator, it is not lost
	}
	// A 0 dt frame is UE's flush case: one step of whatever has accumulated.
	(void)FStepPlanner::PlanFrame(Sixtieth, Settings, TestSettings, Accumulator);
	const FStepPlan Flush = FStepPlanner::PlanFrame(0.0f, Settings, TestSettings, Accumulator);
	CHECK(Flush.NumSteps == 1);
	CHECK(Flush.StepDeltaTime == Sixtieth);
	CHECK(Accumulator == 0.0);
	// Sync scenes pass AsyncDt = -1, never 0 (DL40).
	TestSettings.TickMode = EPhysicsTickMode::Sync;
	CHECK(FStepPlanner::GetAsyncDeltaTime(Settings, TestSettings) == -1.0);
}

TEST_CASE("PhysicsTest.StepPlanner.MinDeltaTimeRemainderIsDropped")
{
	// Remainders not above MinDeltaTime are dropped, never carried (PBDRigidsSolver.cpp:554-568).
	FPhysicsSettings Settings;
	Settings.MinPhysicsDeltaTime = RuntimeFloat(0.008f);
	FPhysicsTestSettings TestSettings;
	TestSettings.TickMode = EPhysicsTickMode::Sync;
	double Accumulator = 0.0;
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		// Two 5 ms frames would add up to 10 ms > 8 ms if carried; they are not.
		const FStepPlan P = FStepPlanner::PlanFrame(RuntimeFloat(0.005f), Settings, TestSettings, Accumulator);
		CAPTURE(Frame);
		CHECK(P.NumSteps == 0);
		CHECK(P.NumPushedSteps == 1);
		CHECK(P.LostTime == doctest::Approx(0.005).epsilon(1e-6));
		CHECK(Accumulator == 0.0);
	}
	const FStepPlan Above = FStepPlanner::PlanFrame(RuntimeFloat(0.009f), Settings, TestSettings, Accumulator);
	CHECK(Above.NumSteps == 1);

	// Substepping: every substep of 0.05 / 3 = 16.7 ms is below a 20 ms MinDeltaTime -> nothing is simulated.
	Settings.MinPhysicsDeltaTime = RuntimeFloat(0.02f);
	TestSettings.TickMode = EPhysicsTickMode::Substepping;
	const FStepPlan Substeps = FStepPlanner::PlanFrame(RuntimeFloat(0.05f), Settings, TestSettings, Accumulator);
	CHECK(Substeps.NumPushedSteps == 3);
	CHECK(Substeps.NumSteps == 0);
	CHECK(Substeps.LostTime == doctest::Approx(0.05).epsilon(1e-6));

	// The solver floor is UE_SMALL_NUMBER even when MinPhysicsDeltaTime is 0 (PhysicsSolverBase.h:727-730).
	Settings.MinPhysicsDeltaTime = 0.0f;
	TestSettings.TickMode = EPhysicsTickMode::Sync;
	CHECK(FStepPlanner::PlanFrame(RuntimeFloat(1e-9f), Settings, TestSettings, Accumulator).NumSteps == 0);
}

TEST_CASE("PhysicsTest.StepPlanner.FixedStepIgnoresFrameDt")
{
	// FixedStep: N = 1, FixedDeltaTime, no clamp (MaxDt <= 0, ChaosScene.cpp:341-345); the frame dt is ignored.
	for (const float FrameDt : { 0.0f, Sixtieth, RuntimeFloat(0.1f), RuntimeFloat(1.0f) })
	{
		const FStepPlan P = Plan(EPhysicsTickMode::FixedStep, FrameDt);
		CHECK(P.NumSteps == 1);
		CHECK(P.StepDeltaTime == Sixtieth);
		CHECK(P.Params.DeltaSeconds == Sixtieth);
		CHECK(P.Params.MaxPhysicsDeltaTime == 0.0f);
		CHECK_FALSE(P.Params.bSubstepping);
	}
	// A FixedDeltaTime above MaxPhysicsDeltaTime is not clamped in FixedStep (the harness mode passes MaxDt = 0).
	FPhysicsTestSettings TestSettings;
	TestSettings.FixedDeltaTime = RuntimeFloat(0.05f);
	double Accumulator = 0.0;
	const FStepPlan Long = FStepPlanner::PlanFrame(Sixtieth, FPhysicsSettings(), TestSettings, Accumulator);
	CHECK(Long.NumSteps == 1);
	CHECK(Long.StepDeltaTime == RuntimeFloat(0.05f));
}

TEST_CASE("PhysicsTest.StepPlanner.SetUpForFrameParams")
{
	// UWorld's SetUpForFrame arguments per tick mode (PhysLevel.cpp:194-212): what a Native-driver backend receives.
	const FPhysicsSettings Settings;
	FPhysicsTestSettings TestSettings;
	TestSettings.TickMode = EPhysicsTickMode::Sync;
	FSetUpForFrameParams P = FStepPlanner::MakeSetUpForFrameParams(RuntimeFloat(0.02f), Settings, TestSettings);
	CHECK(P.DeltaSeconds == RuntimeFloat(0.02f));
	CHECK(P.MinPhysicsDeltaTime == Settings.MinPhysicsDeltaTime);
	CHECK(P.MaxPhysicsDeltaTime == Settings.MaxPhysicsDeltaTime);
	CHECK(P.MaxSubstepDeltaTime == Settings.MaxSubstepDeltaTime);
	CHECK(P.MaxSubsteps == Settings.MaxSubsteps);
	CHECK_FALSE(P.bSubstepping);
	TestSettings.TickMode = EPhysicsTickMode::Substepping;
	P = FStepPlanner::MakeSetUpForFrameParams(RuntimeFloat(0.02f), Settings, TestSettings);
	CHECK(P.bSubstepping);
	TestSettings.TickMode = EPhysicsTickMode::AsyncFixed;
	P = FStepPlanner::MakeSetUpForFrameParams(RuntimeFloat(0.02f), Settings, TestSettings);
	CHECK_FALSE(P.bSubstepping);
	CHECK(P.MaxPhysicsDeltaTime == Settings.MaxPhysicsDeltaTime);
}

TEST_CASE("PhysicsTest.Analytic.EngineDeltaTimeSurvivesFastMath")
{
	// DL23: dt_engine = (double)(float)dt = 0.01666666753590106964111328125 for 1/60, NOT 1/60 exactly. Under /fp:fast,
	// MSVC 14.44 folds static_cast<double>(constexpr float 1.0f / 60.0f) to the exact 1/60 (docs/Progress.md M0 4);
	// EngineDeltaTime must not. These checks fail if it ever does.
	constexpr float ConstexprDt = 1.0f / 60.0f;
	const double Engine = Analytic::EngineDeltaTime(ConstexprDt);
	CHECK(std::bit_cast<uint64>(Engine) == std::bit_cast<uint64>(0x1.111112p-6));
	CHECK(Engine == 0.01666666753590106964111328125);
	CHECK(Engine != 1.0 / 60.0);
	CHECK(Analytic::EngineDeltaTime(1.0 / 60.0) == Engine);   // double overload rounds through float as well
	CHECK(Analytic::EngineDeltaTime(FPhysicsTestSettings().FixedDeltaTime) == Engine);
	CHECK(Analytic::EngineDeltaTime(1.0f / 30.0f) == 0x1.111112p-5);

	// Informational: what a plain conversion of the constexpr float gives in this build (the M0 hazard).
	const double Naive = static_cast<double>(ConstexprDt);
	char Text[160];
	std::snprintf(Text, sizeof(Text), "static_cast<double>(constexpr 1.0f/60.0f) = %.17g (%s); EngineDeltaTime = %.17g", Naive,
		Naive == Engine ? "float-rounded, not folded" : "exact 1/60: folded by /fp:fast", Engine);
	MESSAGE(std::string(Text));
}

TEST_CASE("PhysicsTest.Analytic.FreeFallReferenceUsesEngineDt")
{
	// S00.FreeFall: z0 = 1000 cm, v0 = 0, g = -980 cm/s^2, 60 steps of FixedDeltaTime: z60 = 501.8332813705 cm with
	// dt_engine; 501.8333333333 with the exact 1/60 (5.2e-5 cm apart; G1 tolerance 2e-3 cm, Null +-1e-9).
	const double DtEngine = Analytic::EngineDeltaTime(FPhysicsTestSettings().FixedDeltaTime);
	const Analytic::FFreeFallState S00 = Analytic::SemiImplicitEulerFreeFall(1000.0, 0.0, -980.0, DtEngine, 60);
	CHECK(FMath::Abs(S00.Z - 501.8332813705) < 1e-9);
	CHECK(FMath::Abs(S00.Vz - (-980.0000511110)) < 1e-9);
	const Analytic::FFreeFallState Exact = Analytic::SemiImplicitEulerFreeFall(1000.0, 0.0, -980.0, 1.0 / 60.0, 60);
	CHECK(FMath::Abs(Exact.Z - 501.8333333333) < 1e-9);
	CHECK(FMath::Abs((Exact.Z - S00.Z) - 5.196e-5) < 1e-7);
	CHECK(FMath::Abs(S00.Z - 510.0) > 8.0);   // the continuous solution (510) is the wrong reference

	// The closed form equals an explicit semi-implicit Euler loop with the same dt.
	double Z = 1000.0, V = 0.0;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		V += -980.0 * DtEngine;
		Z += V * DtEngine;
	}
	CHECK(FMath::Abs(Z - S00.Z) < 1e-9);
	CHECK(FMath::Abs(V - S00.Vz) < 1e-9);
}
