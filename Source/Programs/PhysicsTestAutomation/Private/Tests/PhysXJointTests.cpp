// PhysXJointTests.cpp - M9 PhysX joints, joint snapshot and native debug draw (03_WorkPlan M9, docs/Abi.md 9.4; built only
// with PT_WITH_PHYSX). The shared contract (PhysicsTest.Joint.BackendContract, A4, PreflightSweep) takes PhysX through the
// registry once it declares PT_CAP_JOINT_*; this file holds what only PhysX has: the per-variant joint recipe (UE4Compat
// family PxD6Joint with the actors swapped [M], SdkDefault PxSphericalJoint / PxRevoluteJoint) and its variant keys, the
// impulse convention of the joint snapshot (PxConstraint::getForce on actor0 -> the impulse on Body1), bDisableCollision as
// eCOLLISION_ENABLED, the joints of released bodies, the PxRenderBuffer -> PtDebugDrawCmd round trip of viz runs, that
// visualization changes no result, and the single-thread dispatcher against the inline one on joint and CCD scenes
// (02_Architecture 8.2 M5 note: "M9: D6 and CCD scenes"). Test case names stay under PhysicsTest.PhysXBackend.* (CTest area
// PhysicsTest.Unit.PhysXBackend).
//
// The PhysX module is GLOBAL_SCENE_SETTINGS (one tolerances scale per initialisation): as PhysXBackendTests.cpp, a test that
// uses SdkDefault terminates an idle module with other init options first and leaves it terminated (FJointModuleScope).
#include "CoreMinimal.h"

#include "BehaviorScenes.h"
#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "Physics/AnalyticReference.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsEngine/ConstraintUtils.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace PhysicsTest;

namespace
{
	constexpr const char* GJointVariants[] = { "UE4Compat", "UE4Compat-TGS", "UE4Compat-Stab", "SdkDefault" };
	constexpr double GPi = 3.14159265358979323846;
	constexpr uint32 GArgbMagenta = 0xffff00ffu;   // PxDebugColor::eARGB_MAGENTA: PhysX's collision shapes
	constexpr uint32 GArgbBlue = 0xff0000ffu;      // eARGB_BLUE: contact normals

	std::filesystem::path JointConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	FResolvedPhysicsConfig LoadVariant(const std::string& Variant, const std::string& Profile)
	{
		FBenchRunSpec Spec;
		Spec.Backend = "PhysX";
		Spec.PhysicsProfile = Profile;
		Spec.BackendProfile = Variant;
		Spec.ConfigDir = JointConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	FPhysicsBackendModule& PhysXModule()
	{
		std::string Error;
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("PhysX", Error);
		REQUIRE_MESSAGE(Module != nullptr, Error);
		return *Module;
	}

	void PrepareModuleFor(const FResolvedPhysicsConfig& Config)
	{
		FPhysicsBackendModule& Module = PhysXModule();
		if (Module.IsInitialized() && Module.GetInitParams().InitOptions != Config.InitOptions)
		{
			REQUIRE(Module.GetNumLiveScenes() == 0);
			REQUIRE(Module.TermGamePhys() == PT_OK);
		}
	}

	// Leaves a module initialised with variant options terminated (the shared suites initialise it without options).
	struct FJointModuleScope
	{
		FJointModuleScope() = default;
		FJointModuleScope(const FJointModuleScope&) = delete;
		FJointModuleScope& operator=(const FJointModuleScope&) = delete;
		~FJointModuleScope()
		{
			std::string Error;
			FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule("PhysX", Error);
			if (Module != nullptr && Module->IsInitialized() && Module->GetNumLiveScenes() == 0 && !Module->GetInitParams().InitOptions.empty())
			{
				Module->TermGamePhys();
			}
		}
	};

	std::unique_ptr<FPhysScene> MakeScenario(const char* ScenarioName, const FScenarioParams& Overrides, const FResolvedPhysicsConfig& Config)
	{
		PrepareModuleFor(Config);
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = "PhysX";
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = Capacity.MaxBodies;
		InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		InitParams.bApplyForcedOverrides = Scenario->AllowsForcedOverrides();
		auto Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		Scene->PrepareForSimulation();
		return Scene;
	}

	std::unique_ptr<FPhysScene> MakeEmptyScene(const char* DebugName, const FResolvedPhysicsConfig& Config)
	{
		PrepareModuleFor(Config);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = DebugName;
		InitParams.BackendName = "PhysX";
		InitParams.Config = &Config;
		return std::make_unique<FPhysScene>(InitParams);
	}

	std::vector<FEffectiveSetting> Effective(const FPhysScene& Scene)
	{
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		const bool bParsed = ParseEffectiveSettings(Scene.DumpEffectiveSettings(), Settings, Error);
		INFO(Error);
		REQUIRE(bParsed);
		return Settings;
	}

	const FEffectiveSetting& Setting(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
		const std::string KeyText(Key);
		INFO(KeyText);
		REQUIRE(Found != Settings.end());
		return *Found;
	}

	void Step(FPhysScene& Scene, const int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
			REQUIRE(Scene.GetLastFrameTiming().EngineErrorBits == 0u);
		}
	}

	double Stretch(const FConstraintInstance& Joint)
	{
		return (Joint.GetWorldFrame(EConstraintFrame::Frame1).GetTranslation() - Joint.GetWorldFrame(EConstraintFrame::Frame2).GetTranslation()).Size();
	}

	double AxisError(const FConstraintInstance& Joint)
	{
		const double Dot = FMath::Clamp(Joint.GetWorldFrame(EConstraintFrame::Frame1).GetRotation().GetAxisX() | Joint.GetWorldFrame(EConstraintFrame::Frame2).GetRotation().GetAxisX(),
			-1.0, 1.0);
		return std::acos(Dot);
	}

	FVector ToVector(const PtVec3& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	std::string HexText(const uint64 Value)
	{
		char Text[24];
		std::snprintf(Text, sizeof(Text), "0x%016llx", static_cast<unsigned long long>(Value));
		return Text;
	}

	// A joint between two bodies (Body2 nullptr = the world) whose two frames coincide at Anchor (world) with X = Axis
	// (world) and Y = Secondary, written into each body's actor space from the bodies' spawn transforms.
	FConstraintInstance MakeWorldAnchoredJoint(const char* Name, const bool bHinge, const FTransform& Body1Pose, const FTransform* Body2Pose, const FVector& Anchor,
		const FVector& Axis, const FVector& Secondary, const bool bDisableCollision)
	{
		FConstraintInstance Joint;
		Joint.JointName = Name;
		Joint.Pos1 = Body1Pose.InverseTransformPosition(Anchor);
		Joint.PriAxis1 = Body1Pose.InverseTransformVectorNoScale(Axis);
		Joint.SecAxis1 = Body1Pose.InverseTransformVectorNoScale(Secondary);
		Joint.Pos2 = Body2Pose != nullptr ? Body2Pose->InverseTransformPosition(Anchor) : Anchor;
		Joint.PriAxis2 = Body2Pose != nullptr ? Body2Pose->InverseTransformVectorNoScale(Axis) : Axis;
		Joint.SecAxis2 = Body2Pose != nullptr ? Body2Pose->InverseTransformVectorNoScale(Secondary) : Secondary;
		if (bHinge)
		{
			ConstraintUtils::ConfigureAsHinge(Joint);
		}
		else
		{
			ConstraintUtils::ConfigureAsBallAndSocket(Joint);
		}
		Joint.ProfileInstance.bDisableCollision = bDisableCollision;
		return Joint;
	}

	// A body template with the profile's defaults (as Context.MakeBodyTemplate) and an explicit mass.
	FBodyInstance MakeTemplate(const FResolvedPhysicsConfig& Config, const bool bSimulate, const float MassKg)
	{
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = bSimulate;
		Template.bStatic = !bSimulate;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = MassKg;
		return Template;
	}

	// The pendulum period from the bob's x(t): the mean spacing of same-direction zero crossings (linear interpolation).
	double MeasurePeriod(const std::vector<double>& X, const double Dt)
	{
		std::vector<double> Down;
		for (size_t Index = 1; Index < X.size(); ++Index)
		{
			if (X[Index - 1] > 0.0 && X[Index] <= 0.0)
			{
				Down.push_back((static_cast<double>(Index) + X[Index - 1] / (X[Index - 1] - X[Index])) * Dt);
			}
		}
		if (Down.size() < 2)
		{
			return 0.0;
		}
		return (Down.back() - Down.front()) / static_cast<double>(Down.size() - 1);
	}
}

// docs/Abi.md 9.4 engine mapping, per variant: UE4Compat / -TGS / -Stab build PxD6Joints with the parent as actor0 [M],
// SdkDefault PxSphericalJoint / PxRevoluteJoint; every engine joint holds the recipe (read back from the joints), joint
// projection is recorded as absent, and the M6a 5 deg pendulum meets 03 M9's criterion on every variant: the period within
// 0.5 % of the physical pendulum (Normalized). 30 and 90 deg are reported.
TEST_CASE("PhysicsTest.PhysXBackend.JointRecipePerVariant")
{
	const FJointModuleScope Scope;
	for (const char* Variant : GJointVariants)
	{
		const FResolvedPhysicsConfig Config = LoadVariant(Variant, "Normalized");
		const bool bSdk = std::string(Variant) == "SdkDefault";
		for (const char* JointType : { "Hinge", "Ball" })
		{
			for (const char* Angle : { "5", "30", "90" })
			{
				const std::string Label = std::string(Variant) + " " + JointType + " " + Angle + " deg";
				CAPTURE(Label);
				std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", { { "Angle", Angle }, { "Joint", JointType } }, Config);
				const FConstraintInstance& Joint = *Scene->GetConstraints()[0];
				REQUIRE(Joint.IsSimulated());
				const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
				const FEffectiveSetting& Joints = Setting(Settings, "physx.joints");
				CHECK(Joints.Source == "SET_ONLY");
				CHECK(Joints.Canonical == 0.0);   // every engine joint holds the variant's recipe
				CHECK(Joints.Native.find(bSdk ? "JointRecipe=Native" : "JointRecipe=D6") != std::string::npos);
				CHECK(Joints.Native.find("JointActorOrder=ParentFirst") != std::string::npos);
				CHECK(Joints.Native.find("1 PhysX joints") != std::string::npos);
				CHECK(Setting(Settings, "joint.projection").Canonical == 0.0);
				CHECK(Setting(Settings, "joint.projection").Source == "COMPILE_TIME");
				CHECK(Setting(Settings, "joint.solver").Native.find(bSdk ? (std::string(JointType) == "Ball" ? "PxSphericalJoint" : "PxRevoluteJoint") : "PxD6Joint")
					!= std::string::npos);
				CHECK(Setting(Settings, "physx.debug_visualization").Native.find("not a viz run") != std::string::npos);
				std::vector<double> X;
				double MaxStretch = 0.0;
				double MaxAxis = 0.0;
				const int32 Frames = BehaviorScenes::M6a::Frames;
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
					X.push_back(Scene->GetBodies()[0]->State.BodyTM.GetTranslation().X);
					MaxStretch = std::max(MaxStretch, Stretch(Joint));
					MaxAxis = std::max(MaxAxis, AxisError(Joint));
				}
				CHECK(Scene->GetLastFrameTiming().EngineErrorBits == 0u);
				const double Reference = Analytic::PendulumPeriod(BehaviorScenes::M6a::Length, BehaviorScenes::M6a::BobRadius, 980.0, std::stod(Angle) * GPi / 180.0);
				const double Period = MeasurePeriod(X, static_cast<double>(Scene->GetTestSettings().FixedDeltaTime));
				const double Error = (Period - Reference) / Reference;
				MESSAGE(Label << ": period " << Period << " s (reference " << Reference << " s, error " << Error * 100.0 << " %), max stretch " << MaxStretch
					<< " cm, max hinge axis error " << MaxAxis << " rad, final x " << X.back() << " cm");
				// The joint holds (PhysicsTest.Joint.BackendContract's 0.5 cm at 5 deg); at 90 deg the bob passes the bottom at
				// 4.4 m/s and TGS stretches about 1 cm (M9 finding, Progress notes).
				CHECK(MaxStretch < (std::string(Angle) == "90" ? 2.0 : 0.5));
				if (std::string(JointType) == "Hinge")
				{
					CHECK(MaxAxis < 1e-2);
				}
				if (std::string(Angle) == "5")
				{
					CHECK(std::abs(Error) < 0.005);   // 03 M9 criterion (Normalized)
				}
			}
		}
	}
}

// The joint variant keys switch the recipe without a code change (Q10: the user checks UE4's FConstraintInstance shape in
// 4.27 and flips a key): ChildFirst and Native on the UE4Compat row are accepted, read back from the engine, and still keep
// the joint together; bad values fail CreateScene with the reason.
TEST_CASE("PhysicsTest.PhysXBackend.JointVariantKeys")
{
	const FJointModuleScope Scope;
	const FResolvedPhysicsConfig Base = LoadVariant("UE4Compat", "Normalized");
	const std::pair<const char*, const char*> Switches[][2] = {
		{ { "JointActorOrder", "ChildFirst" }, { "JointRecipe", "D6" } },
		{ { "JointActorOrder", "ParentFirst" }, { "JointRecipe", "Native" } },
		{ { "JointActorOrder", "childfirst" }, { "JointRecipe", "native" } },
	};
	for (const auto& Pair : Switches)
	{
		FResolvedPhysicsConfig Config = Base;
		for (const auto& [Key, Value] : Pair)
		{
			Config.SceneOptions.emplace_back(Key, Value);   // the last value of a key wins
		}
		const std::string Label = std::string(Pair[0].second) + " / " + Pair[1].second;
		CAPTURE(Label);
		for (const char* JointType : { "Hinge", "Ball" })
		{
			CAPTURE(JointType);
			std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", { { "Joint", JointType } }, Config);
			const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
			CHECK(Setting(Settings, "physx.joints").Canonical == 0.0);
			const bool bChildFirst = std::string(Pair[0].second) == "ChildFirst" || std::string(Pair[0].second) == "childfirst";
			CHECK(Setting(Settings, "physx.joints").Native.find(bChildFirst ? "JointActorOrder=ChildFirst" : "JointActorOrder=ParentFirst") != std::string::npos);
			double MaxStretch = 0.0;
			double MinX = 1e30;
			for (int32 Frame = 0; Frame < 120; ++Frame)
			{
				Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
				MaxStretch = std::max(MaxStretch, Stretch(*Scene->GetConstraints()[0]));
				MinX = std::min(MinX, Scene->GetBodies()[0]->State.BodyTM.GetTranslation().X);
			}
			MESSAGE(Label << " " << std::string(JointType) << ": max stretch " << MaxStretch << " cm, min bob x " << MinX << " cm");
			CHECK(MaxStretch < 0.5);
			CHECK(MinX < -5.0);
		}
	}
	FPhysicsBackendModule& Module = PhysXModule();
	PrepareModuleFor(Base);
	if (!Module.IsInitialized())
	{
		REQUIRE(Module.InitGamePhys(FPhysScene::MakeBackendInitParams(&Base)) == PT_OK);
	}
	const PtKeyValue Bad[] = { { "JointRecipe", "Fixed" }, { "JointActorOrder", "Swapped" }, { "DebugVisualization", "" }, { "DebugVisualization", "Shapes" },
		{ "DebugVisualization", "CollisionShapes,CollisionShapes" }, { "DebugVisualization", "CollisionShapes," } };
	for (const PtKeyValue& Option : Bad)
	{
		const std::string Text = std::string(Option.Key) + "=" + Option.Value;
		CAPTURE(Text);
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_VIZ;
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		Desc.DebugName = "JointVariantKeys";
		Desc.Options = &Option;
		Desc.NumOptions = 1;
		std::string Error;
		CHECK(FPhysicsBackendScene::Create(Module, Desc, Error) == nullptr);
		MESSAGE(Error);
	}
	const PtKeyValue Good[] = { { "DebugVisualization", "None" }, { "DebugVisualization", " collisionshapes , ContactNormals " },
		{ "DebugVisualization", "CollisionShapes,CollisionAABBs,ActorAxes,BodyMassAxes,BodyLinearVelocity,JointLocalFrames,JointLimits,ContactPoints,ContactNormals" } };
	for (const PtKeyValue& Option : Good)
	{
		const std::string Text = std::string(Option.Key) + "=" + Option.Value;
		CAPTURE(Text);
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_VIZ;
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		Desc.DebugName = "JointVariantKeys";
		Desc.Options = &Option;
		Desc.NumOptions = 1;
		std::string Error;
		CHECK_MESSAGE(FPhysicsBackendScene::Create(Module, Desc, Error) != nullptr, Error);
	}
}

// docs/Abi.md 9.4 item 9 on PhysX: LinearImpulse / AngularImpulse are what the joint gave Body1 in the last step (world,
// kg cm/s and kg cm^2/s, about Frame1World's origin), from PxConstraint::getForce (the force on actor0) x dt. At rest the
// joint carries the weight below it: a 1 kg bob hanging from the world gets +m g dt up; a horizontal 100 cm cantilever on a
// vertical hinge gets the swing rows' counter-torque -d m g dt about Y. Both actor orders, cm (UE4Compat) and m (SdkDefault).
// TGS (UE4Compat-TGS): the linear impulse is exact, the angular one is NaN - PhysX 5.11's TGS writeback carries no usable
// torque (PhysXConstraint.cpp: -2 r x F and no angular-row share, measured here before the adapter dropped it).
TEST_CASE("PhysicsTest.PhysXBackend.JointSnapshotImpulses")
{
	const FJointModuleScope Scope;
	struct FCase
	{
		const char* Variant;
		const char* ActorOrder;
	};
	const FCase Cases[] = { { "UE4Compat", "ParentFirst" }, { "UE4Compat", "ChildFirst" }, { "SdkDefault", "ParentFirst" }, { "SdkDefault", "ChildFirst" },
		{ "UE4Compat-TGS", "ParentFirst" }, { "UE4Compat-TGS", "ChildFirst" } };
	for (const FCase& Case : Cases)
	{
		const std::string Label = std::string(Case.Variant) + " " + Case.ActorOrder;
		CAPTURE(Label);
		FResolvedPhysicsConfig Config = LoadVariant(Case.Variant, "Normalized");
		Config.SceneOptions.emplace_back("JointActorOrder", Case.ActorOrder);
		std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("JointSnapshotImpulses", Config);
		FBodySetup Ball;
		Ball.AggGeom.SphereElems.emplace_back(5.0);
		const FBodySetup* BallSetup = Scene->CreateBodySetup(std::move(Ball));
		const FBodyInstance Template = MakeTemplate(Config, true, 1.0f);
		const FBodyInstance HeavyTemplate = MakeTemplate(Config, true, 2.0f);
		// 1. A bob hanging 100 cm below a world pivot (ball), the bob's local frame turned 90 deg about Z.
		const FTransform BobPose(FRotator(0.0, 90.0, 0.0), FVector(0.0, 0.0, 900.0));
		FBodyInstance* Bob = Scene->SpawnBody(Template, BallSetup, BobPose);
		Scene->SpawnConstraint(MakeWorldAnchoredJoint("Hang", false, BobPose, nullptr, FVector(0.0, 0.0, 1000.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false), Bob,
			nullptr);
		// 2. A cantilever: a bob 100 cm to the side of a world pivot on a vertical hinge (frame X = world Z).
		const FTransform ArmPose(FVector(600.0, 0.0, 1000.0));
		FBodyInstance* Arm = Scene->SpawnBody(Template, BallSetup, ArmPose);
		Scene->SpawnConstraint(MakeWorldAnchoredJoint("Cantilever", true, ArmPose, nullptr, FVector(500.0, 0.0, 1000.0), FVector(0.0, 0.0, 1.0), FVector(1.0, 0.0, 0.0),
			false), Arm, nullptr);
		// 3. Two dynamic bodies: a 2 kg parent hanging from the world, a 1 kg child hanging from the parent.
		const FTransform ParentPose(FVector(-500.0, 0.0, 900.0));
		const FTransform ChildPose(FVector(-500.0, 0.0, 800.0));
		FBodyInstance* Parent = Scene->SpawnBody(HeavyTemplate, BallSetup, ParentPose);
		FBodyInstance* Child = Scene->SpawnBody(Template, BallSetup, ChildPose);
		Scene->SpawnConstraint(MakeWorldAnchoredJoint("Upper", false, ParentPose, nullptr, FVector(-500.0, 0.0, 1000.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), true),
			Parent, nullptr);
		Scene->SpawnConstraint(MakeWorldAnchoredJoint("Lower", false, ChildPose, &ParentPose, FVector(-500.0, 0.0, 850.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), true),
			Child, Parent);
		Scene->FlushDeferredActors();
		REQUIRE(Scene->GetConstraints().size() == 4u);
		for (const FConstraintInstance* Joint : Scene->GetConstraints())
		{
			REQUIRE(Joint->IsSimulated());
		}
		// No step yet: zero impulses, not asleep (sleep is off in Normalized).
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
		REQUIRE(Snapshot.Joints.size() == 4u);
		CHECK(ToVector(Snapshot.Joints[0].LinearImpulse).Size() == 0.0);
		CHECK(Snapshot.Joints[0].Flags == 0u);
		Step(*Scene, 90);
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
		REQUIRE(Snapshot.Joints.size() == 4u);
		REQUIRE(Snapshot.JointHostIndex.size() == 4u);
		const double Dt = static_cast<double>(Scene->GetTestSettings().FixedDeltaTime);
		const double Weight = 980.0 * Dt;   // m g dt of 1 kg, kg cm/s
		const bool bTgs = std::string(Case.Variant) == "UE4Compat-TGS";
		for (size_t Index = 0; Index < Snapshot.Joints.size(); ++Index)
		{
			const PtJointSnapshot& Joint = Snapshot.Joints[Index];
			CAPTURE(Index);
			CHECK(Joint.JointIndex == static_cast<uint32>(Index));
			CHECK(Snapshot.JointHostIndex[Index] == static_cast<int32>(Index));
			// Awake, impulses reported; TGS: the angular impulse only is unknown (abi-v1.1 PT_JOINTSNAP_ANGULAR_IMPULSE_NA).
			CHECK(Joint.Flags == (bTgs ? PT_JOINTSNAP_ANGULAR_IMPULSE_NA : 0u));
			const FConstraintInstance& Host = *Scene->GetConstraints()[Index];
			CHECK((ToVector(Joint.Frame1World.Translation) - Host.GetWorldFrame(EConstraintFrame::Frame1).GetTranslation()).Size() < 0.01);
			CHECK((ToVector(Joint.Frame2World.Translation) - Host.GetWorldFrame(EConstraintFrame::Frame2).GetTranslation()).Size() < 0.01);
			const FQuat Engine1(Joint.Frame1World.Rotation.X, Joint.Frame1World.Rotation.Y, Joint.Frame1World.Rotation.Z, Joint.Frame1World.Rotation.W);
			CHECK((Engine1.GetAxisX() - Host.GetWorldFrame(EConstraintFrame::Frame1).GetRotation().GetAxisX()).Size() < 1e-4);
			const FVector Linear = ToVector(Joint.LinearImpulse);
			const FVector Angular = ToVector(Joint.AngularImpulse);
			MESSAGE(Label << " joint " << Index << " (" << Host.JointName << "): linear (" << Linear.X << ", " << Linear.Y << ", " << Linear.Z << ") kg cm/s, angular ("
				<< Angular.X << ", " << Angular.Y << ", " << Angular.Z << ") kg cm^2/s, stretch " << Stretch(Host) << " cm");
		}
		const auto ExpectImpulse = [&](const size_t Index, const FVector& ExpectedLinear, const FVector& ExpectedAngular, const double AngularTolerance)
		{
			CAPTURE(Index);
			const FVector Linear = ToVector(Snapshot.Joints[Index].LinearImpulse);
			const FVector Angular = ToVector(Snapshot.Joints[Index].AngularImpulse);
			CHECK((Linear - ExpectedLinear).Size() < 0.01 * ExpectedLinear.Size());
			if (bTgs)
			{
				CHECK((std::isnan(Angular.X) && std::isnan(Angular.Y) && std::isnan(Angular.Z)));
				CHECK((Snapshot.Joints[Index].Flags & PT_JOINTSNAP_IMPULSE_NA) == 0u);   // the linear impulse is reported
				CHECK((Snapshot.Joints[Index].Flags & PT_JOINTSNAP_ANGULAR_IMPULSE_NA) != 0u);
			}
			else
			{
				CHECK((Angular - ExpectedAngular).Size() < AngularTolerance);
			}
		};
		ExpectImpulse(0, FVector(0.0, 0.0, Weight), FVector::ZeroVector, 0.01 * Weight * 100.0);
		// The cantilever: gravity's torque about the pivot is (0, d m g, 0); the swing rows give the bob the opposite.
		ExpectImpulse(1, FVector(0.0, 0.0, Weight), FVector(0.0, -100.0 * Weight, 0.0), 0.02 * 100.0 * Weight);
		ExpectImpulse(2, FVector(0.0, 0.0, 3.0 * Weight), FVector::ZeroVector, 0.01 * Weight * 100.0);   // carries 2 kg + 1 kg
		ExpectImpulse(3, FVector(0.0, 0.0, Weight), FVector::ZeroVector, 0.01 * Weight * 100.0);         // Body1 = the 1 kg child
	}
}

// The joint lifecycle on PhysX (docs/Abi.md 9.4 items 6, 9): a sleeping joint reports zero impulses with
// PT_JOINTSNAP_SLEEPING, a joint without a dynamic body is accepted with no engine joint, releasing a body releases its
// joints (the snapshot drops them, the index stays taken), and an empty CreateConstraints call is fine.
TEST_CASE("PhysicsTest.PhysXBackend.JointLifecycle")
{
	const FJointModuleScope Scope;
	const FResolvedPhysicsConfig Config = LoadVariant("UE4Compat", "UEParity");   // sleep on
	std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("JointLifecycle", Config);
	FBodySetup Ball;
	Ball.AggGeom.SphereElems.emplace_back(5.0);
	const FBodySetup* BallSetup = Scene->CreateBodySetup(std::move(Ball));
	const FBodyInstance Dynamic = MakeTemplate(Config, true, 1.0f);
	const FBodyInstance Static = MakeTemplate(Config, false, 1.0f);
	const FTransform BobPose(FVector(0.0, 0.0, 900.0));
	FBodyInstance* Bob = Scene->SpawnBody(Dynamic, BallSetup, BobPose);
	Scene->SpawnConstraint(MakeWorldAnchoredJoint("Hang", false, BobPose, nullptr, FVector(0.0, 0.0, 1000.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false), Bob,
		nullptr);
	const FTransform PostAPose(FVector(300.0, 0.0, 100.0));
	const FTransform PostBPose(FVector(300.0, 0.0, 200.0));
	FBodyInstance* PostA = Scene->SpawnBody(Static, BallSetup, PostAPose);
	FBodyInstance* PostB = Scene->SpawnBody(Static, BallSetup, PostBPose);
	Scene->SpawnConstraint(MakeWorldAnchoredJoint("Statics", true, PostAPose, &PostBPose, FVector(300.0, 0.0, 150.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), false),
		PostA, PostB);
	const FTransform SwingPose(FVector(-300.0, 0.0, 900.0));
	FBodyInstance* Swing = Scene->SpawnBody(Dynamic, BallSetup, SwingPose);
	Scene->SpawnConstraint(MakeWorldAnchoredJoint("Swing", false, SwingPose, nullptr, FVector(-300.0, 0.0, 1000.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false),
		Swing, nullptr);
	Scene->FlushDeferredActors();
	REQUIRE(Scene->GetBackendScene().GetNumJoints() == 3u);
	{
		const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
		const FEffectiveSetting& Joints = Setting(Settings, "physx.joints");
		MESSAGE(Joints.Native);
		CHECK(Joints.Native.find("3 live joints (2 ball, 1 hinge): 2 PhysX joints") != std::string::npos);
		CHECK(Joints.Native.find("1 without a dynamic body") != std::string::npos);
		CHECK(Joints.Canonical == 0.0);
	}
	// An empty call is accepted (the module declares joints) and creates nothing.
	const PtBackendApi& Api = Scene->GetBackendScene().GetApi();
	const PtSceneId Id = Scene->GetBackendScene().GetSceneId();
	CHECK(Api.CreateConstraints(Id, nullptr, 0, sizeof(PtConstraintCreateInfo)) == PT_OK);
	CHECK(Scene->GetBackendScene().GetNumJoints() == 3u);
	// The swing never sleeps while it swings; the hanging bob falls asleep at rest.
	FPhysicsDebugSnapshot Snapshot;
	Swing->SetLinearVelocity(FVector(100.0, 0.0, 0.0), false);
	int32 Frame = 0;
	for (; Frame < 600 && Bob->State.ObjectState != EObjectStateType::Sleeping; ++Frame)
	{
		Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
	}
	MESSAGE("UE4Compat UEParity: the hanging bob sleeps after " << Frame << " frames");
	REQUIRE(Bob->State.ObjectState == EObjectStateType::Sleeping);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	CHECK(Snapshot.Joints[0].Flags == PT_JOINTSNAP_SLEEPING);
	CHECK(ToVector(Snapshot.Joints[0].LinearImpulse).Size() == 0.0);
	CHECK(ToVector(Snapshot.Joints[0].AngularImpulse).Size() == 0.0);
	CHECK(Snapshot.Joints[1].Flags == PT_JOINTSNAP_SLEEPING);   // two static bodies: never runs
	CHECK(ToVector(Snapshot.Joints[1].LinearImpulse).Size() == 0.0);
	CHECK((ToVector(Snapshot.Joints[1].Frame1World.Translation) - FVector(300.0, 0.0, 150.0)).Size() < 1e-3);
	CHECK((ToVector(Snapshot.Joints[1].Frame2World.Translation) - FVector(300.0, 0.0, 150.0)).Size() < 1e-3);
	if (Swing->State.ObjectState != EObjectStateType::Sleeping)
	{
		CHECK(Snapshot.Joints[2].Flags == 0u);
		CHECK(ToVector(Snapshot.Joints[2].LinearImpulse).Size() > 0.0);
	}
	// Releasing the swing releases its joint: the snapshot drops index 2, a later joint gets index 3.
	Scene->ReleaseBody(Swing);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 2u);
	CHECK(Snapshot.Joints[0].JointIndex == 0u);
	CHECK(Snapshot.Joints[1].JointIndex == 1u);
	const FTransform LatePose(FVector(-600.0, 0.0, 900.0));
	FBodyInstance* Late = Scene->SpawnBody(Dynamic, BallSetup, LatePose);
	Scene->SpawnConstraint(MakeWorldAnchoredJoint("Late", true, LatePose, nullptr, FVector(-600.0, 0.0, 1000.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), false), Late,
		nullptr);
	Scene->FlushDeferredActors();
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	CHECK(Snapshot.Joints[2].JointIndex == 3u);
	CHECK(Snapshot.JointHostIndex[2] == 3);
	CHECK(ToVector(Snapshot.Joints[2].LinearImpulse).Size() == 0.0);   // no step has run it yet
	Step(*Scene, 2);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	CHECK(ToVector(Snapshot.Joints[2].LinearImpulse).Z > 0.0);
	// Releasing the bob (Body1 of joint 0) and a static post (Body2 of joint 1).
	Scene->ReleaseBody(Bob);
	Scene->ReleaseBody(PostB);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 1u);
	CHECK(Snapshot.Joints[0].JointIndex == 3u);
	const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
	MESSAGE(Setting(Settings, "physx.joints").Native);
	CHECK(Setting(Settings, "physx.joints").Native.find("3 released with their bodies") != std::string::npos);
	Step(*Scene, 2);
}

// PT_JOINTF_DISABLE_COLLISION (docs/Abi.md 9.4 item 5) is PhysX's eCOLLISION_ENABLED inverted: two overlapping boxes joined
// with bDisableCollision report no contact between them (and the child hangs still); without it they collide.
TEST_CASE("PhysicsTest.PhysXBackend.JointDisableCollision")
{
	const FJointModuleScope Scope;
	for (const char* Variant : { "UE4Compat", "SdkDefault" })
	{
		FResolvedPhysicsConfig Config = LoadVariant(Variant, "Normalized");
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;   // the contact listener
		for (const bool bDisable : { true, false })
		{
			const std::string Label = std::string(Variant) + (bDisable ? " bDisableCollision" : " colliding");
			CAPTURE(Label);
			std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("JointDisableCollision", Config);
			FBodySetup Box;
			Box.AggGeom.BoxElems.emplace_back(100.0);
			const FBodySetup* BoxSetup = Scene->CreateBodySetup(std::move(Box));
			const FTransform ParentPose(FVector(0.0, 0.0, 1000.0));
			const FTransform ChildPose(FVector(0.0, 0.0, 950.0));   // overlaps the parent by 50 cm
			FBodyInstance* Parent = Scene->SpawnBody(MakeTemplate(Config, false, 1.0f), BoxSetup, ParentPose);
			FBodyInstance* Child = Scene->SpawnBody(MakeTemplate(Config, true, 10.0f), BoxSetup, ChildPose);
			Scene->SpawnConstraint(MakeWorldAnchoredJoint("Glue", false, ChildPose, &ParentPose, FVector(0.0, 0.0, 975.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0),
				bDisable), Child, Parent);
			Scene->FlushDeferredActors();
			Step(*Scene, 10);
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS | PT_SNAP_JOINTS));
			uint32 PairPoints = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				PairPoints += (Contact.Body0 == 1u && Contact.Body1 == 0u) || (Contact.Body0 == 0u && Contact.Body1 == 1u) ? 1u : 0u;
			}
			const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
			MESSAGE(Label << ": " << PairPoints << " contact points between the jointed boxes; " << Setting(Settings, "physx.joints").Native);
			CHECK((PairPoints == 0u) == bDisable);
			CHECK(Setting(Settings, "physx.joints").Native.find(bDisable ? "0 of them with eCOLLISION_ENABLED" : "1 of them with eCOLLISION_ENABLED") != std::string::npos);
			CHECK(Setting(Settings, "physx.joints").Canonical == 0.0);
		}
	}
}

// 03 M9 native draw: PxScene::getRenderBuffer -> PtDebugDrawCmd round trip in a viz run (no rendering). M1.SingleBoxDrop a
// at rest: PhysX's magenta collision-shape lines span the resting cube and the ground box in cm (both unit systems), blue
// contact normals are 10 cm long, the red contact-point crosses appear only when the variant adds ContactPoints, the
// pendulum's joint frames are drawn at the pivot; a bench run draws nothing; the two-call protocol, truncation and the
// effective record hold.
TEST_CASE("PhysicsTest.PhysXBackend.NativeDebugDrawRoundTrip")
{
	const FJointModuleScope Scope;
	for (const char* Variant : { "UE4Compat", "SdkDefault" })
	{
		CAPTURE(Variant);
		FResolvedPhysicsConfig Config = LoadVariant(Variant, "Normalized");
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
		Config.TestSettings.InstrumentFlags = PT_INSTR_NATIVE_DEBUG_DRAW;
		const bool bContactPoints = std::string(Variant) == "UE4Compat";   // the default set plus ContactPoints on one variant
		if (bContactPoints)
		{
			Config.SceneOptions.emplace_back("DebugVisualization", "CollisionShapes,JointLocalFrames,JointLimits,ContactPoints,ContactNormals");
		}
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
		REQUIRE(Scene->GetBackendInfo().Has(PT_CAP_NATIVE_DEBUG_DRAW));
		Step(*Scene, 120);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW));
		CHECK_FALSE(Snapshot.bTruncated);
		CHECK(Snapshot.StringPool.empty());
		uint32 NumMagenta = 0;
		uint32 NumNormals = 0;
		uint32 NumOther = 0;
		FVector CubeMin(1e30, 1e30, 1e30);
		FVector CubeMax(-1e30, -1e30, -1e30);
		FVector GroundMin(1e30, 1e30, 1e30);
		FVector GroundMax(-1e30, -1e30, -1e30);
		bool bAllFinite = true;
		for (const PtDebugDrawCmd& Command : Snapshot.NativeCommands)
		{
			bAllFinite = bAllFinite && std::isfinite(Command.A.X) && std::isfinite(Command.A.Y) && std::isfinite(Command.A.Z) && std::isfinite(Command.B.X)
				&& std::isfinite(Command.B.Y) && std::isfinite(Command.B.Z);
			CHECK((Command.Type == PT_DD_LINE || Command.Type == PT_DD_POINT || Command.Type == PT_DD_TRIANGLE));
			CHECK(Command.Rotation.W == 1.0);
			CHECK(Command.Lifetime == 0.0f);
			if (Command.Type == PT_DD_LINE && Command.ColorBGRA == GArgbMagenta)
			{
				++NumMagenta;
				// The cube's edges lie within 60 cm of the Z axis; the ground's corners are 100 m out.
				const bool bCube = std::abs(Command.A.X) < 60.0 && std::abs(Command.A.Y) < 60.0 && std::abs(Command.B.X) < 60.0 && std::abs(Command.B.Y) < 60.0;
				for (const PtVec3& End : { Command.A, Command.B })
				{
					const FVector P = ToVector(End);
					FVector& Min = bCube ? CubeMin : GroundMin;
					FVector& Max = bCube ? CubeMax : GroundMax;
					Min = FVector(std::min(Min.X, P.X), std::min(Min.Y, P.Y), std::min(Min.Z, P.Z));
					Max = FVector(std::max(Max.X, P.X), std::max(Max.Y, P.Y), std::max(Max.Z, P.Z));
				}
			}
			else if (Command.Type == PT_DD_LINE && Command.ColorBGRA == GArgbBlue)
			{
				++NumNormals;
				CHECK((ToVector(Command.B) - ToVector(Command.A)).Size() == doctest::Approx(10.0).epsilon(1e-3));
				CHECK(std::abs((ToVector(Command.B) - ToVector(Command.A)).GetSafeNormal().Z) == doctest::Approx(1.0).epsilon(1e-3));   // the ground's normal
			}
			else
			{
				++NumOther;
			}
		}
		const FVector Box = Scene->GetBodies()[1]->State.BodyTM.GetTranslation();
		MESSAGE(std::string(Variant) << ": " << Snapshot.NativeCommands.size() << " native commands (" << NumMagenta << " collision-shape lines, " << NumNormals << " contact normals, "
			<< NumOther << " other); cube lines span (" << CubeMin.X << ", " << CubeMin.Y << ", " << CubeMin.Z << ") - (" << CubeMax.X << ", " << CubeMax.Y << ", " << CubeMax.Z
			<< "), box at z " << Box.Z);
		CHECK(bAllFinite);
		CHECK(NumMagenta == 24u);   // 12 box edges x (cube + ground)
		CHECK(NumNormals >= 1u);
		CHECK(bContactPoints ? NumOther == 3u * NumNormals : NumOther == 0u);   // three red cross lines per contact point
		// The cube is at rest: the start-of-step pose the shapes were drawn at is its pose now.
		CHECK((CubeMin - (Box - FVector(50.0, 50.0, 50.0))).Size() < 0.05);
		CHECK((CubeMax - (Box + FVector(50.0, 50.0, 50.0))).Size() < 0.05);
		CHECK(std::abs(GroundMax.Z) < 1e-3);                                // the ground box's top (ScenarioHelpers::SpawnGround: 1 m thick)
		CHECK(GroundMin.Z == doctest::Approx(-100.0).epsilon(1e-6));
		CHECK(GroundMax.X == doctest::Approx(10000.0).epsilon(1e-6));
		const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
		const FEffectiveSetting& Viz = Setting(Settings, "physx.debug_visualization");
		MESSAGE(Viz.Native);
		CHECK(Viz.Native.find(bContactPoints ? "on (viz run): DebugVisualization=CollisionShapes,JointLocalFrames,JointLimits,ContactPoints,ContactNormals"
			: "on (viz run): DebugVisualization=CollisionShapes,JointLocalFrames,JointLimits,ContactNormals") != std::string::npos);
		CHECK(Viz.Canonical == (bContactPoints ? 5.0 : 4.0));
		CHECK(Viz.Source == "SET_ONLY");

		// The raw protocol: counts first, then a read with too little room is PT_E_CAPACITY, then a truncated capture.
		const PtBackendApi& Api = Scene->GetBackendScene().GetApi();
		const PtSceneId Id = Scene->GetBackendScene().GetSceneId();
		PtDebugSnapshotRequest Request = {};
		Request.StructSize = sizeof(Request);
		Request.Flags = PT_SNAP_NATIVE_DRAW;
		Request.MaxNativeCommands = 1u << 20;
		PtDebugSnapshotCounts Counts = {};
		REQUIRE(Api.CaptureDebugSnapshot(Id, &Request, &Counts) == PT_OK);
		CHECK(Counts.NumNativeCommands == Snapshot.NativeCommands.size());
		CHECK(Counts.NumParticles == 0u);
		std::vector<PtDebugDrawCmd> Commands(Counts.NumNativeCommands);
		PtDebugSnapshotBuffers Buffers = {};
		Buffers.NativeCommands = Commands.data();
		Buffers.CommandCapacity = Counts.NumNativeCommands - 1u;
		Buffers.CommandStride = sizeof(PtDebugDrawCmd);
		CHECK(Api.ReadDebugSnapshot(Id, &Buffers) == PT_E_CAPACITY);
		Buffers.CommandCapacity = Counts.NumNativeCommands;
		REQUIRE(Api.ReadDebugSnapshot(Id, &Buffers) == PT_OK);
		CHECK(std::memcmp(Commands.data(), Snapshot.NativeCommands.data(), Commands.size() * sizeof(PtDebugDrawCmd)) == 0);
		Request.MaxNativeCommands = 5;
		REQUIRE(Api.CaptureDebugSnapshot(Id, &Request, &Counts) == PT_OK);
		CHECK(Counts.NumNativeCommands == 5u);
		CHECK(Counts.bTruncated == 1u);
	}
	// Joint frames: the hinge pendulum's two frames are drawn at the pivot (eJOINT_LOCAL_FRAMES): actor1's (the bob's, child)
	// with 20 cm axis arrows, actor0's (the world's, parent) 1.5 x longer (Cm::visualizeJointFrames, CmVisualization.cpp:13-21
	// [V]). SdkDefault draws the same in m.
	for (const char* Variant : { "UE4Compat", "SdkDefault" })
	{
		CAPTURE(Variant);
		FResolvedPhysicsConfig Config = LoadVariant(Variant, "Normalized");
		Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", {}, Config);
		Step(*Scene, 1);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW | PT_SNAP_JOINTS));
		const FVector Pivot(0.0, 0.0, BehaviorScenes::M6a::PivotZ);
		uint32 NumChildAxes = 0;
		uint32 NumParentAxes = 0;
		for (const PtDebugDrawCmd& Command : Snapshot.NativeCommands)
		{
			const bool bAtPivot = (ToVector(Command.A) - Pivot).Size() < 0.1 || (ToVector(Command.B) - Pivot).Size() < 0.1;
			if (Command.Type == PT_DD_LINE && bAtPivot)
			{
				const double Length = (ToVector(Command.B) - ToVector(Command.A)).Size();
				NumChildAxes += std::abs(Length - 20.0) < 0.01 ? 1u : 0u;
				NumParentAxes += std::abs(Length - 30.0) < 0.01 ? 1u : 0u;
			}
		}
		MESSAGE(std::string(Variant) << " M6a hinge viz: " << Snapshot.NativeCommands.size() << " native commands, " << NumChildAxes << " 20 cm and " << NumParentAxes
			<< " 30 cm axes at the pivot");
		CHECK(NumChildAxes == 3u);
		CHECK(NumParentAxes == 3u);
		REQUIRE(Snapshot.Joints.size() == 1u);
	}
	// A bench run never sets a visualization parameter: no command, the record says why.
	{
		const FResolvedPhysicsConfig Config = LoadVariant("UE4Compat", "Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
		Step(*Scene, 5);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW));
		CHECK(Snapshot.NativeCommands.empty());
		CHECK(Setting(Effective(*Scene), "physx.debug_visualization").Canonical == 0.0);
	}
}

// 03 M9 G11 viz: a viz run with PhysX's visualization (and, for the joints, eVISUALIZATION on every joint) ends every frame
// on the same engine state as the bench run: the native hash is equal frame by frame (the bench's -G11Knob=viz is the
// process-level twin of this check).
TEST_CASE("PhysicsTest.PhysXBackend.VisualizationChangesNoResult")
{
	const FJointModuleScope Scope;
	const std::pair<const char*, FScenarioParams> Cases[] = { { "M1.SingleBoxDrop", { { "Case", "b" } } }, { "M6b.Chain", {} }, { "M6a.Pendulum", { { "Joint", "Ball" } } } };
	for (const char* Variant : { "UE4Compat", "UE4Compat-TGS" })
	{
		for (const auto& [Name, Params] : Cases)
		{
			const std::string Label = std::string(Variant) + " " + Name;
			CAPTURE(Label);
			const FResolvedPhysicsConfig Bench = LoadVariant(Variant, "UEParity");
			FResolvedPhysicsConfig Viz = Bench;
			Viz.TestSettings.RunMode = EPhysicsRunMode::Viz;
			Viz.TestSettings.InstrumentFlags = PT_INSTR_NATIVE_DEBUG_DRAW;
			std::unique_ptr<FPhysScene> A = MakeScenario(Name, Params, Bench);
			std::unique_ptr<FPhysScene> B = MakeScenario(Name, Params, Viz);
			int32 FirstDiff = -1;
			for (int32 Frame = 0; Frame < 180 && FirstDiff < 0; ++Frame)
			{
				A->Tick(A->GetTestSettings().FixedDeltaTime);
				B->Tick(B->GetTestSettings().FixedDeltaTime);
				FPhysicsDebugSnapshot Snapshot;
				REQUIRE(B->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW | PT_SNAP_JOINTS));   // the viewer's capture
				FirstDiff = A->ComputeNativeStateHash() != B->ComputeNativeStateHash() ? Frame : -1;
			}
			MESSAGE(Label << ": bench and viz native hashes " << (FirstDiff < 0 ? std::string("equal for 180 frames") : "differ from frame " + std::to_string(FirstDiff)));
			CHECK(FirstDiff < 0);
		}
	}
}

// 02_Architecture 8.2 (M5 note): the single-thread LIFO dispatcher and PxDefaultCpuDispatcherCreate(0) run follow-up tasks in
// another order; M5 showed equal hashes on M3.BoxPile only and asked M9 to repeat it on D6 and CCD scenes. M6b.Chain and
// M6c.HeavyChain (PxD6Joints) and a CCD scene (small fast spheres onto a thin plate, bUseCCD) end on the same native hash
// with both dispatchers; the single-thread one never re-enters deeper than 1.
TEST_CASE("PhysicsTest.PhysXBackend.DispatcherMatchesInlineWithJointsAndCcd")
{
	const FJointModuleScope Scope;
#if defined(_DEBUG)
	constexpr int32 Frames = 120;
#else
	constexpr int32 Frames = 600;
#endif
	const FResolvedPhysicsConfig Single = LoadVariant("UE4Compat", "UEParity");
	FResolvedPhysicsConfig Inline = Single;
	Inline.SceneOptions.emplace_back("CpuDispatcher", "PxDefault");
	const auto BuildCcd = [](const FResolvedPhysicsConfig& Config) -> std::unique_ptr<FPhysScene>
	{
		std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("CcdSpheres", Config);
		FBodySetup Plate;
		Plate.AggGeom.BoxElems.emplace_back(2000.0, 2000.0, 2.0);
		const FBodySetup* PlateSetup = Scene->CreateBodySetup(std::move(Plate));
		FBodySetup Pellet;
		Pellet.AggGeom.SphereElems.emplace_back(2.0);
		const FBodySetup* PelletSetup = Scene->CreateBodySetup(std::move(Pellet));
		Scene->SpawnBody(MakeTemplate(Config, false, 1.0f), PlateSetup, FTransform(FVector(0.0, 0.0, 0.0)));
		FBodyInstance Template = MakeTemplate(Config, true, 0.1f);
		Template.bUseCCD = true;
		for (int32 Index = 0; Index < 20; ++Index)
		{
			FBodyInstance* Pellet0 = Scene->SpawnBody(Template, PelletSetup, FTransform(FVector(-500.0 + 50.0 * Index, 0.0, 200.0 + 10.0 * Index)));
			Pellet0->SetLinearVelocity(FVector(37.0 * Index, 0.0, -6000.0), false);   // 100 cm per step: through 2 cm without CCD
		}
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		return Scene;
	};
	struct FRun
	{
		uint64 Hash = 0;
		double MaxReentrancy = -1.0;
		double MinZ = 1e30;
	};
	const auto Run = [&](const char* Name, const FResolvedPhysicsConfig& Config) -> FRun
	{
		std::unique_ptr<FPhysScene> Scene = std::string(Name) == "CCD" ? BuildCcd(Config) : MakeScenario(Name, {}, Config);
		FRun Result;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			REQUIRE(Scene->GetLastFrameTiming().EngineErrorBits == 0u);
			if (std::string(Name) == "CCD")
			{
				for (const FBodyInstance* Body : Scene->GetBodies())
				{
					Result.MinZ = Body->bSimulatePhysics ? std::min(Result.MinZ, Body->State.BodyTM.GetTranslation().Z) : Result.MinZ;
				}
			}
		}
		Result.Hash = Scene->ComputeNativeStateHash();
		const std::vector<FEffectiveSetting> Settings = Effective(*Scene);
		Result.MaxReentrancy = Setting(Settings, "physx.dispatcher_max_reentrancy").Canonical;
		if (std::string(Name) == "CCD")
		{
			CHECK(Setting(Settings, "ccd.enabled").Canonical == 1.0);
		}
		return Result;
	};
	for (const char* Name : { "M6b.Chain", "M6c.HeavyChain", "CCD" })
	{
		CAPTURE(Name);
		const FRun A = Run(Name, Single);
		const FRun B = Run(Name, Inline);
		MESSAGE(std::string(Name) << ", " << Frames << " frames: single-thread h_native " << HexText(A.Hash) << ", PxDefaultCpuDispatcherCreate(0) " << HexText(B.Hash)
			<< "; re-entrancy depth " << A.MaxReentrancy << " / " << B.MaxReentrancy << (std::string(Name) == "CCD" ? "; lowest pellet z " + std::to_string(A.MinZ) + " cm" : ""));
		CHECK(A.Hash == B.Hash);
		CHECK(A.MaxReentrancy == 1.0);
		if (std::string(Name) == "CCD")
		{
			CHECK(A.MinZ > -1.0);   // CCD keeps the pellets above the 2 cm plate (its bottom is at z -1)
		}
	}
}

// 03 M9 M6b / M6c on PhysX (Normalized, every variant), the backend side of the analysis criterion ("report the chain
// stretch; the energy does not diverge"). Energy here = translational kinetic + potential (rotational energy is >= 0, so a
// gain of this bounds a gain of the total from below). M6b (1:1) on every variant: no gain above 1 % of the chain's drop
// scale and the joints hold within 50 cm. M6c (100:1): TGS stays bounded; every PGS variant (UE4Compat / -Stab 8/1 D6,
// SdkDefault 4/1 spherical) diverges about 0.75 s in - joints torn metres apart, speeds of hundreds of m/s - with or without
// contacts and with 32 position iterations (M9 finding, Progress notes: PGS mass-ratio behaviour; UE4's joint projection,
// which PhysX 5.11 no longer has, is what UE4 relied on there). The test records it and only requires finite states.
TEST_CASE("PhysicsTest.PhysXBackend.JointChains")
{
	const FJointModuleScope Scope;
	for (const char* Variant : GJointVariants)
	{
		const FResolvedPhysicsConfig Config = LoadVariant(Variant, "Normalized");
		const bool bTgs = std::string(Variant) == "UE4Compat-TGS";
		for (const char* Name : { "M6b.Chain", "M6c.HeavyChain" })
		{
			const std::string Label = std::string(Variant) + " " + Name;
			CAPTURE(Label);
			const bool bHeavy = std::string(Name) == "M6c.HeavyChain";
			std::unique_ptr<FPhysScene> Scene = MakeScenario(Name, {}, Config);
			const auto Energy = [&Scene]()
			{
				double E = 0.0;
				for (const FBodyInstance* Body : Scene->GetBodies())
				{
					const double M = Body->MassProperties.Mass;
					E += 0.5 * M * Body->State.LinearVelocity.SizeSquared() + M * 980.0 * Body->State.BodyTM.GetTranslation().Z;
				}
				return E;
			};
			double TotalMass = 0.0;
			for (const FBodyInstance* Body : Scene->GetBodies())
			{
				TotalMass += Body->MassProperties.Mass;
			}
			const double E0 = Energy();
			const double Scale = TotalMass * 980.0 * 800.0;   // the drop scale of a 800 cm chain
			double MaxGain = -1e30;
			double MaxStretch = 0.0;
			double LateMaxStretch = 0.0;
			int32 FirstTorn = -1;   // the first frame a joint stretched beyond 20 cm (half a link)
			bool bFinite = true;
			for (int32 Frame = 0; Frame < BehaviorScenes::Chain::Frames; ++Frame)
			{
				Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
				REQUIRE(Scene->GetLastFrameTiming().EngineErrorBits == 0u);
				const double Gain = Energy() - E0;
				bFinite = bFinite && std::isfinite(Gain);
				MaxGain = std::max(MaxGain, Gain);
				for (const FConstraintInstance* Joint : Scene->GetConstraints())
				{
					const double S = Stretch(*Joint);
					MaxStretch = std::max(MaxStretch, S);
					LateMaxStretch = Frame >= 300 ? std::max(LateMaxStretch, S) : LateMaxStretch;
					FirstTorn = FirstTorn < 0 && S > 20.0 ? Frame : FirstTorn;
				}
			}
			MESSAGE(Label << ": max joint stretch " << MaxStretch << " cm (frames 300-599: " << LateMaxStretch << " cm, first > 20 cm at frame " << FirstTorn
				<< "), max energy gain " << MaxGain / Scale * 100.0 << " % of m g 800 cm, final energy change " << (Energy() - E0) / Scale * 100.0 << " %");
			CHECK(bFinite);
			if (!bHeavy)
			{
				CHECK(MaxGain < 0.01 * Scale);
				CHECK(MaxStretch < 50.0);   // SdkDefault's 4 / 1 iterations: about 27 cm in the first swing
			}
			else if (bTgs)
			{
				CHECK(MaxStretch < 50.0);
				CHECK(MaxGain < 0.2 * Scale);
			}
		}
	}
}
