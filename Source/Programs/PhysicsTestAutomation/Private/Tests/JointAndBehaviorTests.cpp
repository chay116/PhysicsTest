// JointAndBehaviorTests.cpp - M9 prep (03_WorkPlan M9, docs/Abi.md 9.4, docs/Scenes.md 2.16-2.22): the joint ABI semantics on
// the host side (FConstraintInstance -> PtConstraintCreateInfo, the Kit's joint checks, host-only joints on a backend
// without the capability, the joint contract every backend that declares PT_CAP_JOINT_* must meet), the behaviour scenes
// (registry, engine-independent desc_sha256, initial poses, the Perturb replicas) and the [/Script/PhysicsTest.ForcedOverrides]
// section (02 10).
#include "CoreMinimal.h"

#include "BehaviorScenes.h"
#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "DebugDrawQueue.h"
#include "DebugDrawSettings.h"
#include "Gates/PhysicsGates.h"
#include "Physics/AnalyticReference.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsDebugDraw.h"
#include "PhysicsEngine/ConstraintUtils.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	std::filesystem::path JointTestConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	FResolvedPhysicsConfig LoadJointTestConfig(const std::string& Backend, const std::string& Profile)
	{
		FBenchRunSpec Spec;
		Spec.Backend = Backend;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = JointTestConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	// Every registry backend except the DLL twin of Null and Chaos (default variants, as the bench resolves them).
	std::vector<std::string> GetJointTestBackends()
	{
		std::vector<std::string> Names;
		for (const std::string& Name : FPhysicsBackendRegistry::Get().GetAvailableNames())
		{
			if (Name != "NullDll" && Name != "Chaos")
			{
				Names.push_back(Name);
			}
		}
		return Names;
	}

	struct FBuilt
	{
		std::unique_ptr<FPhysScene> Scene;
		std::string DescSha256;
		FScenarioParams Params;
	};

	// The scenario built as the bench builds it (forced overrides per the scenario, capacity hints).
	FBuilt Build(const char* ScenarioName, const FScenarioParams& Overrides, const std::string& Backend, const FResolvedPhysicsConfig& Config)
	{
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FBuilt Built;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Built.Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Built.Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = Backend;
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = Capacity.MaxBodies;
		InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		InitParams.bApplyForcedOverrides = Scenario->AllowsForcedOverrides();
		Built.Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Built.Params));
		const FScenarioContext Context{ Built.Params, Random, &Config };
		Scenario->BuildScene(*Built.Scene, Context);
		const std::vector<float> Dts(static_cast<size_t>(Scenario->GetDefaultFrames(Built.Params)), Config.TestSettings.FixedDeltaTime);
		Built.DescSha256 = Built.Scene->ComputeDescSha256(Dts);
		return Built;
	}

	PtTransform PtIdentityAt(const double X, const double Y, const double Z)
	{
		return PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ X, Y, Z } };
	}

	// The world frames of a joint at the host's pulled poses: origin distance and, for a hinge, the angle between the X axes.
	struct FJointError
	{
		double Stretch = 0.0;      // cm
		double AxisAngle = 0.0;    // rad
	};

	FJointError MeasureJoint(const FConstraintInstance& Joint)
	{
		const FTransform A = Joint.GetWorldFrame(EConstraintFrame::Frame1);
		const FTransform B = Joint.GetWorldFrame(EConstraintFrame::Frame2);
		FJointError Error;
		Error.Stretch = (A.GetTranslation() - B.GetTranslation()).Size();
		const double Dot = FMath::Clamp(A.GetRotation().GetAxisX() | B.GetRotation().GetAxisX(), -1.0, 1.0);
		Error.AxisAngle = std::acos(Dot);
		return Error;
	}

	// The tolerances of the joint gate J1 (Config/Golden/tolerances.json; the contract's stretch bound and its registered engine
	// exceptions live there since M9 fix round 0, with their sign-off).
	const Gates::FTolerances& GetJointTolerances()
	{
		static Gates::FTolerances Tolerances;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			std::string Error;
			const bool bOk = Tolerances.LoadFile(Gates::GetTolerancesPath(JointTestConfigDir()), Error);
			INFO(Error);
			REQUIRE(bOk);
			bLoaded = true;
		}
		return Tolerances;
	}

	const char* const GBehaviorScenes[] ={ "M2a.Tower", "M4.InclineFriction", "M5.BouncingBalls", "M6a.Pendulum", "M6b.Chain", "M6c.HeavyChain", "M7.UEDefaults" };
}

// ---- joints: host marshalling (docs/Abi.md 9.4) ----------------------------------------------------------------------

TEST_CASE("PhysicsTest.Joint.MarshalPresets")
{
	FConstraintInstance Joint;
	Joint.JointName = "J";
	Joint.Pos1 = FVector(0.0, 0.0, 100.0);
	Joint.Pos2 = FVector(10.0, 20.0, 30.0);
	Joint.PriAxis2 = FVector(0.0, 1.0, 0.0);
	Joint.SecAxis2 = FVector(0.0, 0.0, 1.0);
	PtConstraintCreateInfo Info = {};
	std::string Error;

	ConstraintUtils::ConfigureAsBallAndSocket(Joint);
	REQUIRE_MESSAGE(MarshalConstraint(Joint, 3, PT_INDEX_NONE, Info, Error), Error);
	CHECK(Info.Type == PT_JOINT_BALL);
	CHECK(Info.Body1 == 3u);
	CHECK(Info.Body2 == PT_INDEX_NONE);
	CHECK(Info.Flags == 0u);
	CHECK(GetJointCapability(Info) == PT_CAP_JOINT_BALL);
	CHECK(Info.Frame1.Translation.Z == 100.0);
	CHECK(Info.Frame1.Rotation.W == doctest::Approx(1.0));
	// Frame2: X = (0, 1, 0), Y = (0, 0, 1) -> the rotation maps X to Y and Y to Z.
	const FQuat Q2(Info.Frame2.Rotation.X, Info.Frame2.Rotation.Y, Info.Frame2.Rotation.Z, Info.Frame2.Rotation.W);
	CHECK((Q2.GetAxisX() - FVector(0.0, 1.0, 0.0)).Size() < 1e-12);
	CHECK((Q2.GetAxisY() - FVector(0.0, 0.0, 1.0)).Size() < 1e-12);
	CHECK(Info.Reserved0 == 0.0);
	CHECK(Info.Reserved1 == 0.0);

	ConstraintUtils::ConfigureAsHinge(Joint);   // twist free (45 deg limit ignored: the motion is free), swings locked
	Joint.ProfileInstance.bDisableCollision = true;
	REQUIRE_MESSAGE(MarshalConstraint(Joint, 3, 4, Info, Error), Error);
	CHECK(Info.Type == PT_JOINT_HINGE);
	CHECK(Info.Body2 == 4u);
	CHECK(Info.Flags == PT_JOINTF_DISABLE_COLLISION);
	CHECK(GetJointCapability(Info) == PT_CAP_JOINT_HINGE);

	// Not Phase 1 (DL31): limited motions, a hinge about a swing axis, a free linear axis.
	const auto ExpectRejected = [&](FConstraintInstance Variant, const char* Fragment)
	{
		CHECK_FALSE(MarshalConstraint(Variant, 3, PT_INDEX_NONE, Info, Error));
		CAPTURE(Error);
		CHECK(Error.find(Fragment) != std::string::npos);
	};
	FConstraintInstance Limited = Joint;
	Limited.ProfileInstance.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Limited;
	ExpectRejected(Limited, "Phase 1");
	FConstraintInstance SwingHinge = Joint;
	SwingHinge.ProfileInstance.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Locked;
	SwingHinge.ProfileInstance.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Free;
	CHECK(ConstraintUtils::IsHinge(SwingHinge));   // UE calls it a hinge too; the ABI hinge is the twist axis only
	ExpectRejected(SwingHinge, "swing axis");
	FConstraintInstance FreeLinear = Joint;
	FreeLinear.ProfileInstance.LinearLimit.ZMotion = ELinearConstraintMotion::LCM_Free;
	ExpectRejected(FreeLinear, "linear motion");
	FConstraintInstance Skewed = Joint;
	Skewed.SecAxis1 = FVector(0.5, 1.0, 0.0);
	ExpectRejected(Skewed, "not orthonormal");
	CHECK_FALSE(MarshalConstraint(Joint, PT_INDEX_NONE, 4, Info, Error));
	CHECK(Error.find("Body1") != std::string::npos);
	CHECK_FALSE(MarshalConstraint(Joint, 4, 4, Info, Error));
	CHECK(Error.find("same body") != std::string::npos);
}

// The Kit's CreateConstraints checks (docs/Abi.md 9.4 item 7): every argument error is PT_E_INVALID_ARG even on a module without
// joints (A4 relies on it); a well-formed call to a module without joint capabilities is PT_E_UNSUPPORTED, Num 0 included.
TEST_CASE("PhysicsTest.Joint.KitValidation")
{
	FPhysSceneInitParams Params;
	Params.DebugName = "KitJoints";
	Params.BackendName = "Null";
	FPhysScene Scene(Params);
	FBodySetup Cube;
	Cube.AggGeom.BoxElems.emplace_back(10.0);
	const FBodySetup* CubeSetup = Scene.CreateBodySetup(std::move(Cube));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 100.0)));
	FBodyInstance* Second = Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 200.0)));
	FBodyInstance* Third = Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 300.0)));
	Scene.FlushDeferredActors();
	const PtBackendApi& Api = Scene.GetBackendScene().GetApi();
	const PtSceneId Id = Scene.GetBackendScene().GetSceneId();
	REQUIRE_FALSE(Scene.GetBackendInfo().Has(PT_CAP_JOINT_BALL));

	PtConstraintCreateInfo Good = {};
	Good.Type = PT_JOINT_BALL;
	Good.Body1 = 0;
	Good.Body2 = 1;
	Good.Frame1 = PtIdentityAt(0.0, 0.0, 50.0);
	Good.Frame2 = PtIdentityAt(0.0, 0.0, -50.0);
	CHECK(Api.CreateConstraints(Id, &Good, 1, sizeof(Good)) == PT_E_UNSUPPORTED);
	CHECK(Api.CreateConstraints(Id, nullptr, 0, sizeof(Good)) == PT_E_UNSUPPORTED);
	const auto Expect = [&](const char* What, PtConstraintCreateInfo Info, const uint32 Stride = sizeof(PtConstraintCreateInfo))
	{
		CAPTURE(What);
		CHECK(Api.CreateConstraints(Id, &Info, 1, Stride) == PT_E_INVALID_ARG);
	};
	PtConstraintCreateInfo Bad = Good;
	Bad.Type = PT_JOINT_FIXED;
	Expect("reserved type FIXED", Bad);
	Bad = Good;
	Bad.Body1 = PT_INDEX_NONE;
	Expect("Body1 = the world", Bad);
	Bad = Good;
	Bad.Body2 = 0;
	Expect("Body1 == Body2", Bad);
	Bad = Good;
	Bad.Body2 = 99;
	Expect("Body2 out of range", Bad);
	Bad = Good;
	Bad.Flags = 0x2u;
	Expect("unknown flag", Bad);
	Bad = Good;
	Bad.Reserved0 = 1.0;
	Expect("Reserved0 1", Bad);
	Bad = Good;
	Bad.Reserved1 = -0.0;   // bit test: -0.0 is not the 0 the host writes
	Expect("Reserved1 -0.0", Bad);
	Bad = Good;
	Bad.Frame2.Rotation = PtQuat{ 0.0, 0.0, 0.0, 2.0 };
	Expect("non-unit frame", Bad);
	Expect("stride 8", Good, 8u);
	// A released body (index 2 stays taken: indices are never reused).
	REQUIRE(Third->ActorHandle.Index == 2u);
	Scene.ReleaseBody(Third);
	Bad = Good;
	Bad.Body2 = 2u;
	Expect("released Body2", Bad);
	Bad = Good;
	Bad.Body1 = 2u;
	Expect("released Body1", Bad);
	CHECK(Second->ActorHandle.Index == 1u);
}

// A backend without the joint's capability keeps it host-only: not sent, not simulated, counted; desc_sha256 still hashes it
// (engine-independent), and SpawnConstraint refuses nothing that MarshalConstraint accepts.
TEST_CASE("PhysicsTest.Joint.HostOnlyWithoutCapability")
{
	const FResolvedPhysicsConfig Config = LoadJointTestConfig("Null", "Normalized");
	const FBuilt Pendulum = Build("M6a.Pendulum", {}, "Null", Config);
	REQUIRE(Pendulum.Scene->GetConstraints().size() == 1u);
	CHECK_FALSE(Pendulum.Scene->GetConstraints()[0]->IsSimulated());
	CHECK(Pendulum.Scene->GetStats().NumConstraints == 1);
	CHECK(Pendulum.Scene->GetStats().NumUnsupportedConstraints == 1);
	CHECK(Pendulum.Scene->GetBackendScene().GetNumJoints() == 0u);
	REQUIRE(Pendulum.Scene->GetMarshalledConstraints().size() == 1u);
	const PtConstraintCreateInfo& Info = Pendulum.Scene->GetMarshalledConstraints()[0];
	CHECK(Info.Type == PT_JOINT_HINGE);
	CHECK(Info.Body1 == 0u);
	CHECK(Info.Body2 == PT_INDEX_NONE);
	// The ball variant is a different scene (another desc).
	const FBuilt Ball = Build("M6a.Pendulum", { { "Joint", "Ball" } }, "Null", Config);
	CHECK(Ball.DescSha256 != Pendulum.DescSha256);
	CHECK(Ball.Scene->GetMarshalledConstraints()[0].Type == PT_JOINT_BALL);
	// The chains: every joint has bDisableCollision, the first hangs on the world.
	const FBuilt Chain = Build("M6b.Chain", {}, "Null", Config);
	REQUIRE(Chain.Scene->GetMarshalledConstraints().size() == static_cast<size_t>(BehaviorScenes::M6b::NumLinks));
	CHECK(Chain.Scene->GetMarshalledConstraints()[0].Body2 == PT_INDEX_NONE);
	for (const PtConstraintCreateInfo& Joint : Chain.Scene->GetMarshalledConstraints())
	{
		CHECK(Joint.Type == PT_JOINT_BALL);
		CHECK(Joint.Flags == PT_JOINTF_DISABLE_COLLISION);
	}
	const FBuilt Heavy = Build("M6c.HeavyChain", {}, "Null", Config);
	CHECK(Heavy.Scene->GetMarshalledConstraints().size() == static_cast<size_t>(BehaviorScenes::M6c::NumLinks + 1));
}

// Every joint scene starts with its joints satisfied (docs/Abi.md 9.4 item 3): world frames coincide (origins; hinge X axes).
TEST_CASE("PhysicsTest.Joint.SceneFramesCoincideAtRest")
{
	const FResolvedPhysicsConfig Config = LoadJointTestConfig("Null", "Normalized");
	std::vector<std::pair<const char*, FScenarioParams>> Cases;
	for (const char* Angle : { "5", "30", "90" })
	{
		for (const char* Joint : { "Hinge", "Ball" })
		{
			Cases.push_back({ "M6a.Pendulum", { { "Angle", Angle }, { "Joint", Joint } } });
		}
	}
	Cases.push_back({ "M6b.Chain", {} });
	Cases.push_back({ "M6c.HeavyChain", {} });
	for (const auto& [Name, Params] : Cases)
	{
		const std::string Text = std::string(Name) + (Params.empty() ? "" : " " + Params.begin()->second + " " + Params.rbegin()->second);
		CAPTURE(Text);
		const FBuilt Built = Build(Name, Params, "Null", Config);
		for (const FConstraintInstance* Joint : Built.Scene->GetConstraints())
		{
			const FJointError Error = MeasureJoint(*Joint);
			CHECK(Error.Stretch < 1e-9);
			if (ConstraintUtils::IsHinge(*Joint))
			{
				CHECK(Error.AxisAngle < 1e-7);
			}
		}
	}
	// The pendulum bob hangs Length below the pivot at the release angle.
	const FBuilt Thirty = Build("M6a.Pendulum", { { "Angle", "30" } }, "Null", Config);
	const FVector Bob = Thirty.Scene->GetBodies()[0]->State.BodyTM.GetTranslation();
	CHECK(Bob.X == doctest::Approx(BehaviorScenes::M6a::Length * 0.5));
	CHECK(Bob.Z == doctest::Approx(BehaviorScenes::M6a::PivotZ - BehaviorScenes::M6a::Length * std::sqrt(3.0) / 2.0));
}

// The contract every backend that declares a joint capability must meet (docs/Abi.md 9.4 item 13) is the physics gate J1
// (Gates/PhysicsGates.h, M9 fix round 0): M6a 5 deg ball / hinge frames, swing, snapshot frames and release, the couple of a
// moving joint, a COM-offset body, the collision switch and (sleep on) a sleeping joint (M9 fix round 2), the impulse convention
// on a hinged cantilever. A DLL backend (the Chaos bridge) runs it in its
// own process through PhysicsTestBench -Conformance (CTest PhysicsTest.Bench.Conformance runs it on every target);
// PhysicsTest.Gates.J1 runs it on every registry target and variant in both profiles. This case is the in-process fast path
// on the registry's default variants in Normalized, plus what only the SDK engines owe: both joint types declared (M9), Null
// host-only (a registered Skip). A pass that rests on a post-run registration still waiting for the lead's sign-off is listed
// ("PENDING LEAD SIGN-OFF") and fails the case: since the M9 commit the list is empty (the lead signed J1.ball_stretch_cm for
// Bullet, M9 J-1, on 2026-10-22), so a new unsigned deviation cannot slip in green.
TEST_CASE("PhysicsTest.Joint.BackendContract")
{
	std::vector<std::string> Pending;
	bool bBullet = false;
	for (const std::string& Backend : GetJointTestBackends())
	{
		CAPTURE(Backend);
		bBullet = bBullet || Backend == "Bullet";
		const FResolvedPhysicsConfig Config = LoadJointTestConfig(Backend, "Normalized");
		for (const char* JointType : { "Hinge", "Ball" })
		{
			const uint64 Needed = std::string(JointType) == "Ball" ? PT_CAP_JOINT_BALL : PT_CAP_JOINT_HINGE;
			const FBuilt Built = Build("M6a.Pendulum", { { "Joint", JointType } }, Backend, Config);
			CAPTURE(std::string(JointType));
			CHECK_MESSAGE((Backend == "Null") != Built.Scene->GetBackendInfo().Has(Needed), "M9: every SDK backend declares PT_CAP_JOINT_BALL and PT_CAP_JOINT_HINGE; Null none");
		}
		Gates::FGateTarget Target;
		Target.Backend = Backend;
		Target.Variant = Config.VariantName;
		Target.Profile = Config.ProfileName;
		Target.Config = &Config;
		Target.Tolerances = &GetJointTolerances();
		std::vector<FGateReport> Reports;
		std::string Error;
		const bool bRan = Gates::RunPhysicsGates(Target, { "J1" }, Reports, Error);
		INFO(Error);
		REQUIRE(bRan);
		REQUIRE(Reports.size() == 1u);
		const FGateReport& Report = Reports[0];
		MESSAGE(Report.Summary());
		for (const std::string& Note : Report.Notes)
		{
			MESSAGE(Backend << " J1: " << Note);
		}
		CHECK(Report.Result == (Backend == "Null" ? EGateResult::Skip : EGateResult::Pass));
		for (const FGatePostRunRegistration& Used : Report.PostRunRegistrations)
		{
			if (Used.SignOff == "pending")
			{
				Pending.push_back(Used.Key + " " + Used.Engine);
				MESSAGE("PENDING LEAD SIGN-OFF: " << Backend << " passes J1 on the post-run registration " << Used.Key << " = " << Used.Value
					<< " (Config/Golden/tolerances.json; docs/Progress.md M9)");
			}
		}
	}
	FPhysicsBackendRegistry::Get().TermIdleModules();
	// M9 commit: the lead signed the one post-run registration J1 uses (Bullet J-1, ball stretch 3 cm; 2026-10-22), so no J1 pass
	// rests on an unsigned value - a new pending registration fails here (and in Gates.ToleranceFileIsComplete) until it is signed.
	CHECK(Pending == std::vector<std::string>{});
	if (bBullet)
	{
		const Gates::FToleranceOverride* BallException = GetJointTolerances().FindOverride("J1", "ball_stretch_cm", "Bullet", "Default", "Normalized");
		REQUIRE(BallException != nullptr);
		CHECK(BallException->bPostRun);
		CHECK(BallException->SignOff.rfind("lead 2026-10-22, M9 commit", 0) == 0);
	}
}

// ---- behaviour scenes ------------------------------------------------------------------------------------------------

TEST_CASE("PhysicsTest.Behavior.ScenesRegistered")
{
	for (const char* Name : GBehaviorScenes)
	{
		CAPTURE(Name);
		const IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(Name);
		REQUIRE(Scenario != nullptr);
		CHECK(Scenario->GetKind() == EScenarioKind::Behavior);
		CHECK(Scenario->AllowsForcedOverrides());
		FScenarioParams Params;
		std::string Error;
		REQUIRE(Scenario->ResolveParams({}, Params, Error));
		CHECK(Scenario->GetDefaultFrames(Params) > 0);
		CHECK_FALSE(Scenario->GetWindows(Params).empty());
		const std::vector<FScenarioParamDesc> Descs = Scenario->GetParamDescs();
		const bool bPerturb = std::any_of(Descs.begin(), Descs.end(), [](const FScenarioParamDesc& D) { return D.Name == BehaviorScenes::PerturbParam; });
		CHECK(bPerturb == Scenario->IsChaotic());   // the chaotic scenes take the same-engine perturbation replicas
	}
	const auto Required = [](const char* Name, const FScenarioParams& Overrides)
	{
		const IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(Name);
		FScenarioParams Params;
		std::string Error;
		REQUIRE(Scenario->ResolveParams(Overrides, Params, Error));
		return Scenario->GetRequiredCapabilities(Params);
	};
	CHECK(Required("M6a.Pendulum", {}) == PT_CAP_JOINT_HINGE);
	CHECK(Required("M6a.Pendulum", { { "Joint", "Ball" } }) == PT_CAP_JOINT_BALL);
	CHECK(Required("M6b.Chain", {}) == PT_CAP_JOINT_BALL);
	CHECK(Required("M6c.HeavyChain", {}) == PT_CAP_JOINT_BALL);
	CHECK(Required("M4.InclineFriction", {}) == 0u);
	CHECK_FALSE(FScenarioRegistry::Get().Find("S05.RestitutionCombine")->AllowsForcedOverrides());   // gate scenes keep their modes
}

// DL43: the M9 scenes' desc_sha256 is equal across engines (joint scenes too: host-only joints are hashed as marshalled).
TEST_CASE("PhysicsTest.Behavior.DescEqualAcrossEngines")
{
	for (const char* Profile : { "UEParity", "Normalized" })
	{
		const std::string ProfileText(Profile);
		CAPTURE(ProfileText);
		const FResolvedPhysicsConfig NullConfig = LoadJointTestConfig("Null", Profile);
		for (const char* Name : GBehaviorScenes)
		{
			CAPTURE(Name);
			const FBuilt Null = Build(Name, {}, "Null", NullConfig);
			for (const std::string& Backend : GetJointTestBackends())
			{
				if (Backend == "Null")
				{
					continue;
				}
				CAPTURE(Backend);
				const FBuilt Sdk = Build(Name, {}, Backend, LoadJointTestConfig(Backend, Profile));
				CHECK(Sdk.DescSha256 == Null.DescSha256);
			}
		}
	}
}

// The same-engine perturbation (BehaviorScenes::PerturbationCm): every dynamic body moves by exactly 1e-4 cm, the replicas are
// reproducible and distinct, and they are other scenes (scene_sha256, desc_sha256).
TEST_CASE("PhysicsTest.Behavior.Perturbation")
{
	const FResolvedPhysicsConfig Config = LoadJointTestConfig("Null", "Normalized");
	const IPhysicsTestScenario* Tower = FScenarioRegistry::Get().Find("M2a.Tower");
	const FBuilt Base = Build("M2a.Tower", { { "N", "5" } }, "Null", Config);
	const FBuilt One = Build("M2a.Tower", { { "N", "5" }, { "Perturb", "1" } }, "Null", Config);
	const FBuilt OneAgain = Build("M2a.Tower", { { "N", "5" }, { "Perturb", "1" } }, "Null", Config);
	const FBuilt Two = Build("M2a.Tower", { { "N", "5" }, { "Perturb", "2" } }, "Null", Config);
	CHECK(One.DescSha256 != Base.DescSha256);
	CHECK(One.DescSha256 == OneAgain.DescSha256);
	CHECK(Two.DescSha256 != One.DescSha256);
	CHECK(Tower->ComputeSceneSha256(One.Params) != Tower->ComputeSceneSha256(Base.Params));
	const std::span<FBodyInstance* const> A = Base.Scene->GetBodies();
	const std::span<FBodyInstance* const> B = One.Scene->GetBodies();
	REQUIRE(A.size() == B.size());
	CHECK((A[0]->State.BodyTM.GetTranslation() - B[0]->State.BodyTM.GetTranslation()).Size() == 0.0);   // the static ground stays
	for (size_t Index = 1; Index < A.size(); ++Index)
	{
		const double Offset = (A[Index]->State.BodyTM.GetTranslation() - B[Index]->State.BodyTM.GetTranslation()).Size();
		CHECK(Offset == doctest::Approx(BehaviorScenes::PerturbationCm).epsilon(1e-6));
	}
}

// The initial poses the analysis references assume (docs/Scenes.md 2.16-2.22).
TEST_CASE("PhysicsTest.Behavior.InitialPoses")
{
	const FResolvedPhysicsConfig Config = LoadJointTestConfig("Null", "Normalized");
	// M4: the cube's bottom face lies on the incline's top face (every corner at signed distance 0 from it).
	for (const char* Yaw : { "0", "45" })
	{
		CAPTURE(Yaw);
		const FBuilt Incline = Build("M4.InclineFriction", { { "BoxYaw", Yaw } }, "Null", Config);
		const FTransform& InclineTM = Incline.Scene->GetBodies()[BehaviorScenes::M4::InclineBody]->State.BodyTM;
		const FTransform& CubeTM = Incline.Scene->GetBodies()[BehaviorScenes::M4::CubeBody]->State.BodyTM;
		const FVector Normal = InclineTM.GetRotation().GetAxisZ();
		const FVector Top = InclineTM.GetTranslation() + Normal * (0.5 * BehaviorScenes::M4::InclineThickness);
		double Lowest = 1e30;
		for (const double X : { -1.0, 1.0 })
		{
			for (const double Y : { -1.0, 1.0 })
			{
				for (const double Z : { -1.0, 1.0 })
				{
					const FVector Corner = CubeTM.TransformPosition(FVector(X, Y, Z) * (0.5 * BehaviorScenes::M4::Edge));
					Lowest = std::min(Lowest, (Corner - Top) | Normal);
				}
			}
		}
		CHECK(std::abs(Lowest) < 1e-9);
	}
	// M5: the balls' bottoms at the G4 step-boundary height + 0.01 cm.
	const FBuilt Balls = Build("M5.BouncingBalls", {}, "Null", Config);
	const double H0 = Analytic::StepBoundaryDropHeight(980.0, Analytic::EngineDeltaTime(Config.TestSettings.FixedDeltaTime), BehaviorScenes::M5::TouchStep);
	CHECK(H0 == doctest::Approx(201.7167).epsilon(1e-6));
	for (int32 Ball = 0; Ball < BehaviorScenes::M5::NumBalls; ++Ball)
	{
		const double Bottom = Balls.Scene->GetBodies()[static_cast<size_t>(BehaviorScenes::M5::NumBalls + Ball)]->State.BodyTM.GetTranslation().Z - BehaviorScenes::M5::Radius;
		CHECK(Bottom == doctest::Approx(H0 + BehaviorScenes::M5::TouchGap).epsilon(1e-12));
	}
	// M1 "fall" windows: one frame before the first contact step of the discrete fall of the lowest point (frame f is the
	// state after step f + 1).
	for (const char* Case : { "a", "b" })
	{
		CAPTURE(Case);
		const FBuilt Drop = Build("M1.SingleBoxDrop", { { "Case", Case } }, "Null", Config);
		const FTransform& BoxTM = Drop.Scene->GetBodies()[1]->State.BodyTM;
		double Lowest = 1e30;
		for (const double X : { -50.0, 50.0 })
		{
			for (const double Y : { -50.0, 50.0 })
			{
				for (const double Z : { -50.0, 50.0 })
				{
					Lowest = std::min(Lowest, BoxTM.TransformPosition(FVector(X, Y, Z)).Z);
				}
			}
		}
		const double Dt = Analytic::EngineDeltaTime(Config.TestSettings.FixedDeltaTime);
		int32 ContactStep = 1;
		while (980.0 * Dt * Dt * ContactStep * (ContactStep + 1) / 2.0 < Lowest)
		{
			++ContactStep;
		}
		const IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find("M1.SingleBoxDrop");
		const std::vector<FStepWindow> Windows = Scenario->GetWindows(Drop.Params);
		const auto Fall = std::find_if(Windows.begin(), Windows.end(), [](const FStepWindow& W) { return W.Name == "fall"; });
		REQUIRE(Fall != Windows.end());
		CHECK(Fall->Last == static_cast<uint32>(ContactStep - 2));
	}
	// M7: the UE mass rule (no override): the 100 cm cube is 177.83 kg (02 5.3), the 50 cm cube 37.384 kg.
	const FBuilt Defaults = Build("M7.UEDefaults", {}, "Null", Config);
	CHECK(Defaults.Scene->GetBodies()[1]->MassProperties.Mass == doctest::Approx(37.384).epsilon(1e-4));
	CHECK(Defaults.Scene->GetBodies()[2]->MassProperties.Mass == doctest::Approx(177.83).epsilon(1e-4));
}

// ---- [/Script/PhysicsTest.ForcedOverrides] (02 10, M9) --------------------------------------------------------------------

TEST_CASE("PhysicsTest.Profile.ForcedOverrides")
{
	FConfigFile Base;
	std::string Error;
	REQUIRE(FConfigFile::LoadFile(StringConv::PathToUtf8(JointTestConfigDir() / "BasePhysicsTest.ini"), Base, Error));
	const auto ResolveProfile = [&](const char* Text, FResolvedPhysicsConfig& Out, const FBackendVariant* Variant = nullptr) -> bool
	{
		FPhysicsProfile Profile;
		REQUIRE_MESSAGE(FPhysicsProfile::FromText(Text, "Forced.ini", Profile, Error), Error);
		FProfileResolveInputs Inputs;
		Inputs.Base = &Base;
		Inputs.Profile = &Profile;
		Inputs.Variant = Variant;
		return FProfileResolver::Resolve(Inputs, Out, Error);
	};
	FResolvedPhysicsConfig Config;
	REQUIRE_MESSAGE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n[/Script/PhysicsTest.ForcedOverrides]\nphysicalmaterial.frictioncombinemode=Max\n"
		"PhysicalMaterial.bOverrideFrictionCombineMode=True\nPhysicalMaterial.StaticFriction=friction\nBodyInstance.LinearDamping=0.25\n", Config), Error);
	REQUIRE(Config.ForcedOverrides.size() == 4u);
	CHECK(Config.ForcedOverrides[0].GetKey() == "PhysicalMaterial.FrictionCombineMode");   // canonical spelling
	CHECK(Config.ForcedOverrides[0].Value == "Max");
	CHECK(Config.ForcedOverrides[2].Value == "Friction");
	const FResolvedConfigValue* Value = Config.Find("ForcedOverrides.PhysicalMaterial.FrictionCombineMode");
	REQUIRE(Value != nullptr);
	CHECK(Value->Origin == EConfigOrigin::ForcedOverride);
	CHECK(Value->bPhysicsHash);
	FResolvedPhysicsConfig Plain;
	REQUIRE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n", Plain));
	CHECK(Plain.PhysicsSha256() != Config.PhysicsSha256());

	// Applied to an authored material and body: plain values in order, then SF := F.
	FPhysicalMaterial Material;
	Material.Friction = 0.4f;
	Material.StaticFriction = 0.0f;
	std::vector<std::string> Changes;
	CHECK(FProfileResolver::ApplyForcedOverrides(Config.ForcedOverrides, Material, &Changes) == 3);
	CHECK(Material.FrictionCombineMode == EFrictionCombineMode::Max);
	CHECK(Material.bOverrideFrictionCombineMode);
	CHECK(Material.StaticFriction == 0.4f);
	CHECK(FProfileResolver::ApplyForcedOverrides(Config.ForcedOverrides, Material, nullptr) == 0);   // idempotent
	FBodyInstance Body;
	CHECK(FProfileResolver::ApplyForcedOverrides(Config.ForcedOverrides, Body, &Changes) == 1);
	CHECK(Body.LinearDamping == 0.25f);

	// Errors: a mass field, an unknown field, a bad value, a backend variant forcing anything.
	FResolvedPhysicsConfig Ignored;
	CHECK_FALSE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n[/Script/PhysicsTest.ForcedOverrides]\nPhysicalMaterial.Density=2\n", Ignored));
	CHECK(Error.find("mass computation") != std::string::npos);
	CHECK_FALSE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n[/Script/PhysicsTest.ForcedOverrides]\nBodyInstance.MassScale=2\n", Ignored));
	CHECK_FALSE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n[/Script/PhysicsTest.ForcedOverrides]\nPhysicalMaterial.Shininess=2\n", Ignored));
	CHECK(Error.find("unknown field") != std::string::npos);
	CHECK_FALSE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n[/Script/PhysicsTest.ForcedOverrides]\nPhysicalMaterial.Friction=abc\n", Ignored));
	FBackendVariant Variant;
	REQUIRE(FBackendVariant::FromText("[PhysicsTest.Variant]\nEngine=Jolt\nName=T\n[/Script/PhysicsTest.ForcedOverrides]\nPhysicalMaterial.Friction=0.1\n", "V.ini", Variant, Error));
	CHECK_FALSE(ResolveProfile("[PhysicsTest.Profile]\nName=F\n", Ignored, &Variant));
	CHECK(Error.find("not in a backend variant") != std::string::npos);

	// Normalized forces Average and SF := F (02 10); a scene applies them after authoring, a gate scene does not.
	const FResolvedPhysicsConfig Normalized = LoadJointTestConfig("Null", "Normalized");
	CHECK(Normalized.ForcedOverrides.size() == 5u);
	const FBuilt Incline = Build("M4.InclineFriction", { { "Mu", "0.30" } }, "Null", Normalized);
	const FForcedOverrideReport& Report = Incline.Scene->GetForcedOverrideReport();
	CHECK(Report.bEnabled);
	CHECK(Report.NumMaterials == 1);
	CHECK(Report.NumMaterialsChanged == 1);
	CHECK(Report.NumForeignMaterials == 0);
	const FPhysicalMaterial* Forced = Incline.Scene->GetBodies()[1]->GetSimplePhysicalMaterial();
	CHECK(Forced->StaticFriction == Forced->Friction);
	CHECK(Forced->bOverrideFrictionCombineMode);
	const FBuilt Gate = Build("S05.RestitutionCombine", {}, "Null", Normalized);
	CHECK_FALSE(Gate.Scene->GetForcedOverrideReport().bEnabled);
	CHECK(Gate.Scene->GetBodies()[2]->GetSimplePhysicalMaterial()->RestitutionCombineMode == EFrictionCombineMode::Multiply);
}

// M9: the joint drawer names each joint's FConstraintInstance through JointHostIndex (the backend's JointIndex counts only the
// joints it created).
TEST_CASE("PhysicsTest.DebugDraw.JointHostIndex")
{
	const FResolvedPhysicsConfig Config = LoadJointTestConfig("Null", "Normalized");
	const FBuilt Chain = Build("M6b.Chain", {}, "Null", Config);
	FPhysicsDebugSnapshot Snapshot;
	DebugDraw::CaptureSnapshot(*Chain.Scene, PT_SNAP_PARTICLES | PT_SNAP_JOINTS, Snapshot);
	CHECK(Snapshot.Joints.empty());   // Null simulates no joint: no host joint either
	CHECK(Snapshot.bJointsFromHostState);
	// A joint whose backend index (7) is not its host index (3): drawn at constraint 3's actors.
	const FConstraintInstance* Third = Chain.Scene->GetConstraints()[3];
	PtJointSnapshot Joint = {};
	Joint.JointIndex = 7;
	const FTransform F1 = Third->GetWorldFrame(EConstraintFrame::Frame1);
	const FTransform F2 = Third->GetWorldFrame(EConstraintFrame::Frame2);
	Joint.Frame1World = PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ F1.GetTranslation().X, F1.GetTranslation().Y, F1.GetTranslation().Z } };
	Joint.Frame2World = PtTransform{ PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ F2.GetTranslation().X, F2.GetTranslation().Y, F2.GetTranslation().Z } };
	Snapshot.Joints.push_back(Joint);
	Snapshot.JointHostIndex.push_back(3);
	std::vector<int32> ParticleOfBody;
	DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
	const FDebugDrawSettings Settings;
	FDebugDrawFlags Flags;
	Flags.Radius = 0.0f;
	const DebugDraw::FDrawContext Context{ *Chain.Scene, Snapshot, Settings, Flags, ParticleOfBody };
	FDebugDrawQueue Queue;
	Queue.SetMaxCost(0);
	Queue.SetRegionOfInterest(FVector::ZeroVector, 0.0f);
	DebugDraw::DrawJointConstraints(Queue, Context);
	std::vector<FLatentDrawCommand> Commands;
	Queue.ExtractAllElements(Commands);
	REQUIRE(Commands.size() == 5u);
	// The white connector of end b starts at Body1 = link 3's actor.
	const FVector Link3 = Third->Body1->State.BodyTM.GetTranslation();
	CHECK((Commands[1].LineStart - Link3).Size() < 1e-9);
}

// ---- M9 analytic references (Physics/AnalyticReference.h; Tools/ptbench/analytic.py reproduces them) -----------------------

TEST_CASE("PhysicsTest.Analytic.BehaviorReferences")
{
	const double Pi = 3.14159265358979323846;
	// 03 M9: g 980, L 100, r 5 (a solid sphere bob on a massless rod).
	CHECK(Analytic::PendulumPeriod(100.0, 5.0, 980.0, 5.0 * Pi / 180.0) == doctest::Approx(2.00905).epsilon(2.5e-6));
	CHECK(Analytic::PendulumPeriod(100.0, 5.0, 980.0, 30.0 * Pi / 180.0) == doctest::Approx(2.04305).epsilon(2.5e-6));
	CHECK(Analytic::PendulumPeriod(100.0, 5.0, 980.0, 90.0 * Pi / 180.0) == doctest::Approx(2.37023).epsilon(2.5e-6));
	CHECK(Analytic::CompleteEllipticK(0.0) == doctest::Approx(Pi / 2.0).epsilon(1e-15));
	CHECK(Analytic::CompleteEllipticK(std::sqrt(0.5)) == doctest::Approx(1.8540746773013719).epsilon(1e-14));
	// M4: a = g (sin 30 - mu cos 30); mu 0.6 > tan 30 holds.
	const double Dt = Analytic::EngineDeltaTime(1.0f / 60.0f);
	const double A = 980.0 * (0.5 - 0.3 * std::cos(Pi / 6.0));
	CHECK(Analytic::InclineSlideDistance(0.3, Pi / 6.0, 980.0, Dt, 120) == doctest::Approx(A * Dt * Dt * 120.0 * 121.0 / 2.0).epsilon(1e-12));
	CHECK(Analytic::InclineSlideDistance(0.6, Pi / 6.0, 980.0, Dt, 120) == 0.0);
}
