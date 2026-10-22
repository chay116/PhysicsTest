// BulletJointTests.cpp - Bullet's M9 work (03_WorkPlan M9; built only with PT_WITH_BULLET, next to BulletBackendTests.cpp):
// ball / hinge joints (btPoint2PointConstraint / btHingeConstraint, docs/Abi.md 9.4) with COM-frame handling, collision
// disabling and the joint snapshot; the UEParity sleep mapping (SleepThresholds=UE); the native debug draw of viz runs and its
// result neutrality (03 M9: "Bullet viz 드로어를 붙인 run과 붙이지 않은 run의 최종 h_native가 같다 (UEParity, sleep 켬)"); the
// Bullet.StaticAabbOff variant against Bullet.Default (G11, Progress M9 F-1); and the behaviour findings this backend pins
// (J-1 ball row coupling, J-2 the 100:1 chain, the M6a period). Test names stay in the BulletBackend area (CTest
// PhysicsTest.Unit.BulletBackend). No Bullet header is visible here: everything goes through FPhysScene and the C ABI.
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
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace PhysicsTest;

namespace
{
	std::filesystem::path BulletJointConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	FResolvedPhysicsConfig LoadConfig(const std::string& Profile, const std::string& Variant = "")
	{
		FBenchRunSpec Spec;
		Spec.Backend = "Bullet";
		Spec.BackendProfile = Variant;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = BulletJointConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	// One [PhysicsTest.Backend.Bullet] key of a loaded config, replaced or added (CreateScene options).
	void SetSceneOption(FResolvedPhysicsConfig& Config, const std::string& Key, const std::string& Value)
	{
		for (auto& Option : Config.SceneOptions)
		{
			if (Option.first == Key)
			{
				Option.second = Value;
				return;
			}
		}
		Config.SceneOptions.emplace_back(Key, Value);
	}

	std::unique_ptr<FPhysScene> MakeScenario(const char* ScenarioName, const FScenarioParams& Overrides, const FResolvedPhysicsConfig& Config)
	{
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = "Bullet";
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

	std::unique_ptr<FPhysScene> MakeEmptyScene(const char* Name, const FResolvedPhysicsConfig& Config)
	{
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = Name;
		InitParams.BackendName = "Bullet";
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = 64;
		InitParams.MaxBodyPairsHint = 256;
		InitParams.MaxContactConstraintsHint = 256;
		return std::make_unique<FPhysScene>(InitParams);
	}

	void Tick(FPhysScene& Scene, const int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		}
	}

	std::vector<FEffectiveSetting> ReadEffective(const std::string& Text)
	{
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		const bool bParsed = ParseEffectiveSettings(Text, Settings, Error);
		INFO(Error);
		REQUIRE(bParsed);
		return Settings;
	}

	const FEffectiveSetting& RequireSetting(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
		const std::string KeyText(Key);
		INFO(KeyText);
		REQUIRE(Found != Settings.end());
		return *Found;
	}

	// The module-level effective settings (DumpEffectiveSettings(0), two-call protocol).
	std::vector<FEffectiveSetting> ReadModuleSettings(const FPhysScene& Scene)
	{
		const PtBackendApi& Api = Scene.GetBackendScene().GetModule().GetApi();
		PtTextBuffer Query = {};
		Api.DumpEffectiveSettings(0, &Query);
		std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		REQUIRE(Api.DumpEffectiveSettings(0, &Buffer) == PT_OK);
		Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
		return ReadEffective(Text);
	}

	FVector ToVector(const PtVec3& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	// A joint as ScenarioHelpers::MakeJoint builds it (positions and X / Y axes of both frames in their bodies' spaces).
	FConstraintInstance MakeJoint(const char* Name, const bool bHinge, const FVector& Pos1, const FVector& Pri1, const FVector& Sec1, const FVector& Pos2,
		const FVector& Pri2, const FVector& Sec2, const bool bDisableCollision)
	{
		FConstraintInstance Joint;
		Joint.JointName = Name;
		Joint.Pos1 = Pos1;
		Joint.PriAxis1 = Pri1;
		Joint.SecAxis1 = Sec1;
		Joint.Pos2 = Pos2;
		Joint.PriAxis2 = Pri2;
		Joint.SecAxis2 = Sec2;
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

	struct FJointError
	{
		double Stretch = 0.0;    // cm
		double AxisAngle = 0.0;  // rad, between the two frames' X axes
	};

	FJointError MeasureJoint(const FConstraintInstance& Joint)
	{
		const FTransform A = Joint.GetWorldFrame(EConstraintFrame::Frame1);
		const FTransform B = Joint.GetWorldFrame(EConstraintFrame::Frame2);
		FJointError Error;
		Error.Stretch = (A.GetTranslation() - B.GetTranslation()).Size();
		Error.AxisAngle = std::acos(FMath::Clamp(A.GetRotation().GetAxisX() | B.GetRotation().GetAxisX(), -1.0, 1.0));
		return Error;
	}

	// The largest joint stretch of a scene over Frames frames (and the frame it happened in).
	std::pair<double, int32> RunAndMeasureStretch(FPhysScene& Scene, const int32 Frames)
	{
		double MaxStretch = 0.0;
		int32 MaxFrame = -1;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
			for (const FConstraintInstance* Joint : Scene.GetConstraints())
			{
				const double Stretch = MeasureJoint(*Joint).Stretch;
				if (Stretch > MaxStretch)
				{
					MaxStretch = Stretch;
					MaxFrame = Frame;
				}
			}
		}
		return { MaxStretch, MaxFrame };
	}

	int32 CountPairContacts(const FPhysicsDebugSnapshot& Snapshot, const uint32 BodyA, const uint32 BodyB)
	{
		int32 Count = 0;
		for (const PtContactPoint& Contact : Snapshot.Contacts)
		{
			Count += ((Contact.Body0 == BodyA && Contact.Body1 == BodyB) || (Contact.Body0 == BodyB && Contact.Body1 == BodyA)) ? 1 : 0;
		}
		return Count;
	}

	int32 CountNative(const FPhysicsDebugSnapshot& Snapshot, const uint32 Type)
	{
		return static_cast<int32>(std::count_if(Snapshot.NativeCommands.begin(), Snapshot.NativeCommands.end(), [Type](const PtDebugDrawCmd& C) { return C.Type == Type; }));
	}

	// The scene's mechanical energy (J): translational and rotational kinetic energy plus gravity's potential (z = 0 reference).
	// The bodies' principal frames are their actor frames here (spheres and capsules along local Z).
	double MechanicalEnergyJ(const FPhysScene& Scene)
	{
		double Energy = 0.0;
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			if (Body == nullptr || !Body->IsDynamic() || !Body->ActorHandle.IsValid())
			{
				continue;
			}
			const double Mass = Body->GetBodyMass();
			const FVector Inertia = Body->GetBodyInertiaTensor();
			const FQuat Rotation = Body->State.BodyTM.GetRotation();
			const FVector W = Body->State.AngularVelocityInRadians;
			const double Wx = W | Rotation.GetAxisX();
			const double Wy = W | Rotation.GetAxisY();
			const double Wz = W | Rotation.GetAxisZ();
			Energy += 0.5 * Mass * Body->State.LinearVelocity.SizeSquared() + 0.5 * (Inertia.X * Wx * Wx + Inertia.Y * Wy * Wy + Inertia.Z * Wz * Wz)
				- Mass * Scene.GetGravity().Z * Body->State.BodyTM.GetTranslation().Z;
		}
		return Energy * 1.0e-4;   // kg cm^2 / s^2 -> J
	}

	FBodyInstance* SpawnGround(FPhysScene& Scene)
	{
		FBodySetup Setup;
		Setup.AggGeom.BoxElems.emplace_back(4000.0, 4000.0, 100.0);
		Setup.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		const FBodySetup* GroundSetup = Scene.CreateBodySetup(std::move(Setup));
		FBodyInstance Static;
		Static.bStatic = true;
		return Scene.SpawnBody(Static, GroundSetup, FTransform(FVector(0.0, 0.0, -50.0)));
	}
}

// ---- capabilities, options, effective keys ---------------------------------------------------------------------------

// M9: Bullet declares both joint types, their snapshot and the native debug draw; the joint keys of docs/Abi.md 9.4 item 4 and
// the sleep rule are in the effective dump; the new scene options validate their values and are creation-time only.
TEST_CASE("PhysicsTest.BulletBackend.JointCapabilitiesAndOptions")
{
	const FResolvedPhysicsConfig Config = LoadConfig("UEParity");
	std::unique_ptr<FPhysScene> Scene = MakeScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Config);
	const auto& Info = Scene->GetBackendInfo();
	for (const uint64 Cap : { PT_CAP_JOINT_BALL, PT_CAP_JOINT_HINGE, PT_CAP_JOINT_SNAPSHOT, PT_CAP_NATIVE_DEBUG_DRAW })
	{
		CHECK(Info.Has(Cap));
	}
	const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
	CHECK(RequireSetting(Settings, "joint.projection").Canonical == 0.0);
	CHECK(RequireSetting(Settings, "joint.projection").Source == "COMPILE_TIME");
	CHECK(RequireSetting(Settings, "joint.solver").Source == "COMPILE_TIME");
	CHECK_FALSE(RequireSetting(Settings, "joint.solver").bHasCanonical);
	CHECK(RequireSetting(Settings, "bullet.sleep_rule").Canonical == 1.0);   // SleepThresholds=UE (Bullet.Default.ini)
	CHECK(RequireSetting(Settings, "bullet.sleep_rule").Source == "SET_ONLY");
	// sleep.criteria (M9): a rule key - the dynamic bodies not holding the rule's thresholds, 0 here.
	CHECK(RequireSetting(Settings, "sleep.criteria").Source == "READBACK");
	CHECK(RequireSetting(Settings, "sleep.criteria").Canonical == 0.0);
	CHECK(RequireSetting(Settings, "sleep.criteria").Native.find("UE rule") == 0u);
	CHECK(RequireSetting(Settings, "bullet.debug_drawer").Canonical == 0.0);   // a bench-mode scene: no drawer
	MESSAGE("Bullet UEParity sleep.criteria: " << RequireSetting(Settings, "sleep.criteria").Native);

	// Values (CreateScene options) and their phase (SetOption after creation is refused: they shape the bodies and joints).
	FPhysicsBackendScene& Backend = Scene->GetBackendScene();
	for (const char* Key : { "SleepThresholds", "BallJoint" })
	{
		const std::string KeyText(Key);
		CAPTURE(KeyText);
		CHECK(Backend.SetOption(Key, std::string(Key) == "BallJoint" ? "Point2Point" : "UE") == PT_E_INVALID_ARG);
	}
	FPhysicsBackendModule& Module = Backend.GetModule();
	{
		PtKeyValue Option = { "SleepThresholds", "Chaos" };
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = PT_RUN_BENCH;
		Desc.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
		Desc.NominalDeltaTime = 1.0 / 60.0;
		Desc.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = -1.0;
		Desc.NumThreads = 1;
		Desc.Options = &Option;
		Desc.NumOptions = 1;
		PtSceneId Id = 0;
		CHECK(Module.GetApi().CreateScene(&Desc, &Id) == PT_E_INVALID_ARG);
		Option = PtKeyValue{ "BallJoint", "ConeTwist" };
		CHECK(Module.GetApi().CreateScene(&Desc, &Id) == PT_E_INVALID_ARG);
		for (const PtKeyValue Good : { PtKeyValue{ "SleepThresholds", "Bullet" }, PtKeyValue{ "BallJoint", "Generic6DofSpring2" } })
		{
			Option = Good;
			REQUIRE(Module.GetApi().CreateScene(&Desc, &Id) == PT_OK);
			CHECK(Module.GetApi().DestroyScene(Id) == PT_OK);
		}
	}
}

// ---- joints ----------------------------------------------------------------------------------------------------------

// docs/Abi.md 9.4 items 1-3 on bodies whose Bullet frame is not their actor frame: a hinge about an oblique world axis on a
// compound body (two boxes: the COM lies off the actor origin, a btCompoundShape) and ball joints from a body with a COM nudge
// (a one-child compound) to a sphere and from the sphere to the world. The frames are given in actor space.
//   Mapping (weightless): the frames coincide at rest, so the joints must apply nothing - every body stays exactly where it is. A
//   pivot or axis taken in the wrong frame (actor instead of COM, a wrong hinge axis) would show as a joint error the solver
//   corrects at once, i.e. motion. Both ball constraints (BallJoint) take the same pivots.
//   Dynamics (gravity): the hinged body turns about the hinge axis only (frame X). Its stretch is Bullet's Baumgarte lag of a fast
//   rotation, not a mapping error (finding J-3): the velocity rows hold, semi-implicit Euler moves the pivot outward by
//   (w dt)^2 r / 2 per step and ERP 0.2 corrects a fifth per step, so the steady stretch is about 2 (w dt)^2 r whatever the
//   iteration count (10, 50 and 200 iterations all give 1.14 cm here; M6a's 5 deg hinge 0.005 cm). The ball pair's stretch is
//   reported (J-1: the light sphere on a long lever). The snapshot reports the host's world frames.
TEST_CASE("PhysicsTest.BulletBackend.JointFramesAndComOffsets")
{
	struct FBuiltScene
	{
		std::unique_ptr<FPhysScene> Scene;
		FBodyInstance* Hinged = nullptr;
		FConstraintInstance* Joints[3] = {};   // the hinge, the low ball joint, the high ball joint
	};
	const FVector PivotLocal(-30.0, 10.0, 0.0);
	const FVector AxisLocal = FVector(0.0, 1.0, 1.0).GetSafeNormal();
	const FVector SecLocal(1.0, 0.0, 0.0);
	const FTransform PairPose(FRotator(10.0, 20.0, 30.0), FVector(0.0, 0.0, 500.0));
	const FTransform ChildPose(FRotator(0.0, 45.0, 0.0), FVector(300.0, 0.0, 400.0));
	const auto BuildScene = [&](const FResolvedPhysicsConfig& Config, const bool bGravity)
	{
		FBuiltScene Built;
		Built.Scene = MakeEmptyScene("BulletJointFrames", Config);
		FPhysScene& Scene = *Built.Scene;
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = bGravity;
		// Body 0: two boxes side by side (actor origin at the left box) -> COM 30 cm along local +X, principal axes = actor axes.
		FBodySetup Pair;
		Pair.AggGeom.BoxElems.emplace_back(40.0, 20.0, 20.0);
		Pair.AggGeom.BoxElems.emplace_back(40.0, 20.0, 20.0);
		Pair.AggGeom.BoxElems.back().Center = FVector(60.0, 0.0, 0.0);
		Pair.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		const FBodySetup* PairSetup = Scene.CreateBodySetup(std::move(Pair));
		Built.Hinged = Scene.SpawnBody(Template, PairSetup, PairPose);
		// Body 1: a cube with a COM nudge (one-child compound); body 2: a sphere it hangs from, hanging from the world.
		FBodySetup Cube;
		Cube.AggGeom.BoxElems.emplace_back(30.0);
		Cube.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		const FBodySetup* CubeSetup = Scene.CreateBodySetup(std::move(Cube));
		FBodyInstance Nudged = Template;
		Nudged.COMNudge = FVector(5.0, -3.0, 4.0);
		FBodyInstance* Child = Scene.SpawnBody(Nudged, CubeSetup, ChildPose);
		FBodySetup Ball;
		Ball.AggGeom.SphereElems.emplace_back(10.0);
		Ball.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		const FBodySetup* BallSetup = Scene.CreateBodySetup(std::move(Ball));
		FBodyInstance* Parent = Scene.SpawnBody(Template, BallSetup, FTransform(FVector(300.0, 0.0, 500.0)));
		// The hinge: pivot at actor-local (-30, 10, 0) (60 cm from the COM), axis = actor-local (0, 1, 1) normalised; the world
		// frame takes the same pose, so the frames coincide at rest.
		const FQuat PairQuat = PairPose.GetRotation();
		Built.Joints[0] = Scene.SpawnConstraint(MakeJoint("ObliqueHinge", true, PivotLocal, AxisLocal, SecLocal, PairPose.TransformPosition(PivotLocal),
			PairQuat.RotateVector(AxisLocal), PairQuat.RotateVector(SecLocal), false), Built.Hinged, nullptr);
		// The ball joints: the cube's actor-local (10, 10, 15) to the sphere; the sphere's top to the world.
		const FVector ChildAnchorLocal(10.0, 10.0, 15.0);
		const FVector X(1.0, 0.0, 0.0);
		const FVector Y(0.0, 1.0, 0.0);
		Built.Joints[1] = Scene.SpawnConstraint(MakeJoint("BallLow", false, ChildAnchorLocal, X, Y, ChildPose.TransformPosition(ChildAnchorLocal) - FVector(300.0, 0.0, 500.0),
			X, Y, true), Child, Parent);
		Built.Joints[2] = Scene.SpawnConstraint(MakeJoint("BallHigh", false, FVector(0.0, 0.0, 10.0), X, Y, FVector(300.0, 0.0, 510.0), X, Y, false), Parent, nullptr);
		Scene.FlushDeferredActors();
		Scene.PrepareForSimulation();
		for (uint32 Index = 0; Index < 3u; ++Index)
		{
			REQUIRE(Built.Joints[Index]->IsSimulated());
			CHECK(Built.Joints[Index]->ConstraintHandle.Index == Index);
		}
		return Built;
	};

	// Mapping: weightless, at rest, nothing may move.
	for (const char* BallJoint : { "Point2Point", "Generic6DofSpring2" })
	{
		CAPTURE(BallJoint);
		FResolvedPhysicsConfig Config = LoadConfig("Normalized");
		SetSceneOption(Config, "BallJoint", BallJoint);
		FBuiltScene Built = BuildScene(Config, false);
		// The shapes really are compounds (the COM frames differ from the actor frames).
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Built.Scene->DumpEffectiveSettings());
		CHECK(RequireSetting(Settings, "shape.wrapping").Native.find("compound 1") != std::string::npos);
		CHECK(RequireSetting(Settings, "shape.wrapping").Native.find("offset_com 1") != std::string::npos);
		Tick(*Built.Scene, 30);
		double MaxSpeed = 0.0;
		double MaxSpin = 0.0;
		for (const FBodyInstance* Body : Built.Scene->GetBodies())
		{
			MaxSpeed = std::max(MaxSpeed, Body->State.LinearVelocity.Size());
			MaxSpin = std::max(MaxSpin, Body->State.AngularVelocityInRadians.Size());
		}
		double MaxStretch = 0.0;
		double MaxAxis = 0.0;
		for (const FConstraintInstance* Joint : Built.Joints)
		{
			MaxStretch = std::max(MaxStretch, MeasureJoint(*Joint).Stretch);
		}
		MaxAxis = MeasureJoint(*Built.Joints[0]).AxisAngle;
		MESSAGE(BallJoint << ", weightless at rest, 30 frames: max speed " << MaxSpeed << " cm/s, spin " << MaxSpin << " rad/s, stretch " << MaxStretch << " cm, hinge axis "
			<< MaxAxis << " rad");
		CHECK(MaxSpeed < 1.0e-3);
		CHECK(MaxSpin < 1.0e-5);
		CHECK(MaxStretch < 1.0e-4);
		CHECK(MaxAxis < 1.0e-5);
	}

	// Dynamics: Bullet.Default (Point2Point, 10 iterations), under gravity.
	const FResolvedPhysicsConfig Config = LoadConfig("Normalized");
	FBuiltScene Built = BuildScene(Config, true);
	FPhysScene& Scene = *Built.Scene;
	const FVector WorldAxis = PairPose.GetRotation().RotateVector(AxisLocal);
	// The COM's distance from the hinge axis: the boxes' COM is actor-local (30, 0, 0).
	const FVector PivotToCom = FVector(30.0, 0.0, 0.0) - PivotLocal;
	const double ComRadius = (PivotToCom - AxisLocal * (PivotToCom | AxisLocal)).Size();
	const double Dt = static_cast<double>(Scene.GetTestSettings().FixedDeltaTime);
	double MaxHingeStretch = 0.0;
	double MaxHingeAxis = 0.0;
	double MaxOffAxisSpin = 0.0;
	double MaxBallStretch = 0.0;
	double MaxSpin = 0.0;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		Scene.Tick(Scene.GetTestSettings().FixedDeltaTime);
		const FJointError HingeError = MeasureJoint(*Built.Joints[0]);
		MaxHingeStretch = std::max(MaxHingeStretch, HingeError.Stretch);
		MaxHingeAxis = std::max(MaxHingeAxis, HingeError.AxisAngle);
		const FVector W = Built.Hinged->State.AngularVelocityInRadians;
		MaxSpin = std::max(MaxSpin, W.Size());
		MaxOffAxisSpin = std::max(MaxOffAxisSpin, (W - WorldAxis * (W | WorldAxis)).Size());
		MaxBallStretch = std::max({ MaxBallStretch, MeasureJoint(*Built.Joints[1]).Stretch, MeasureJoint(*Built.Joints[2]).Stretch });
	}
	const double BaumgarteStretch = 2.0 * (MaxSpin * Dt) * (MaxSpin * Dt) * ComRadius;
	MESSAGE("oblique hinge on a compound, gravity: max stretch " << MaxHingeStretch << " cm (Baumgarte estimate 2 (w dt)^2 r = " << BaumgarteStretch << " cm, r "
		<< ComRadius << " cm), axis error " << MaxHingeAxis << " rad, spin " << MaxSpin << " rad/s (off-axis " << MaxOffAxisSpin
		<< "); ball joints on a COM-nudged cube and a sphere: max stretch " << MaxBallStretch << " cm");
	CHECK(MaxSpin > 1.0);   // it swings about the hinge
	CHECK(MaxHingeStretch < 1.3 * BaumgarteStretch + 0.01);
	CHECK(MaxHingeAxis < 1.0e-3);
	CHECK(MaxOffAxisSpin < 1.0e-2 * MaxSpin);

	// The snapshot reports the joints in JointIndex order with the host's world frames.
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	for (uint32 Index = 0; Index < 3u; ++Index)
	{
		CAPTURE(Index);
		const FConstraintInstance& Host = *Built.Joints[Index];
		const PtJointSnapshot& Joint = Snapshot.Joints[Index];
		CHECK(Joint.JointIndex == Index);
		CHECK(Snapshot.JointHostIndex[Index] == static_cast<int32>(Index));
		CHECK((ToVector(Joint.Frame1World.Translation) - Host.GetWorldFrame(EConstraintFrame::Frame1).GetTranslation()).Size() < 1.0e-6);
		CHECK((ToVector(Joint.Frame2World.Translation) - Host.GetWorldFrame(EConstraintFrame::Frame2).GetTranslation()).Size() < 1.0e-6);
		const FQuat Engine1(Joint.Frame1World.Rotation.X, Joint.Frame1World.Rotation.Y, Joint.Frame1World.Rotation.Z, Joint.Frame1World.Rotation.W);
		CHECK((Engine1.GetAxisX() - Host.GetWorldFrame(EConstraintFrame::Frame1).GetRotation().GetAxisX()).Size() < 1.0e-6);
		CHECK((Joint.Flags & PT_JOINTSNAP_IMPULSE_NA) == 0u);
		CHECK(std::isfinite(Joint.LinearImpulse.X));
	}
}

// docs/Abi.md 9.4 items 5 and 6: PT_JOINTF_DISABLE_COLLISION removes every contact between the two bodies (Bullet: the bodies'
// ignore lists, both ways) and a pair that already had a manifold loses it when the joint arrives after the bodies stepped;
// without the flag the pair collides. A world joint ignores the flag. Viz run: the snapshot reports contacts.
TEST_CASE("PhysicsTest.BulletBackend.JointDisableCollision")
{
	FResolvedPhysicsConfig Config = LoadConfig("Normalized");
	Config.TestSettings.RunMode = EPhysicsRunMode::Viz;
	// Two weightless 100 cm cubes overlapping by 20 cm, joined at the middle of the overlap.
	const auto Build = [&Config](const char* Name, FBodyInstance*& OutA, FBodyInstance*& OutB) -> std::unique_ptr<FPhysScene>
	{
		std::unique_ptr<FPhysScene> Scene = MakeEmptyScene(Name, Config);
		FBodySetup Cube;
		Cube.AggGeom.BoxElems.emplace_back(100.0);
		Cube.PhysMaterial = Scene->GetDefaultPhysicalMaterial();
		const FBodySetup* CubeSetup = Scene->CreateBodySetup(std::move(Cube));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.bEnableGravity = false;
		OutA = Scene->SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 500.0)));
		OutB = Scene->SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 580.0)));
		return Scene;
	};
	const FVector X(1.0, 0.0, 0.0);
	const FVector Y(0.0, 1.0, 0.0);
	const auto JoinAtOverlap = [&](FPhysScene& Scene, FBodyInstance* A, FBodyInstance* B, const bool bDisable)
	{
		// Frame on A at (0, 0, 40), on B at (0, 0, -40): both at world z = 540.
		return Scene.SpawnConstraint(MakeJoint("Overlap", false, FVector(0.0, 0.0, 40.0), X, Y, FVector(0.0, 0.0, -40.0), X, Y, bDisable), A, B);
	};
	for (const bool bDisable : { true, false })
	{
		CAPTURE(bDisable);
		FBodyInstance* A = nullptr;
		FBodyInstance* B = nullptr;
		std::unique_ptr<FPhysScene> Scene = Build(bDisable ? "DisableCollision" : "KeepCollision", A, B);
		JoinAtOverlap(*Scene, A, B, bDisable);
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		Tick(*Scene, 10);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS));
		const int32 Points = CountPairContacts(Snapshot, A->ActorHandle.Index, B->ActorHandle.Index);
		const double Moved = (A->State.BodyTM.GetTranslation() - FVector(0.0, 0.0, 500.0)).Size() + (B->State.BodyTM.GetTranslation() - FVector(0.0, 0.0, 580.0)).Size();
		MESSAGE((bDisable ? "bDisableCollision" : "colliding") << ": " << Points << " contact points between the jointed cubes, moved " << Moved << " cm");
		if (bDisable)
		{
			CHECK(Points == 0);
			CHECK(Moved < 1.0e-3);   // nothing pushes them apart
		}
		else
		{
			CHECK(Points > 0);
		}
	}
	// The joint arrives after two steps of the overlapping pair (its manifold exists): the flag drops it at once.
	{
		FBodyInstance* A = nullptr;
		FBodyInstance* B = nullptr;
		std::unique_ptr<FPhysScene> Scene = Build("LateDisableCollision", A, B);
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		Tick(*Scene, 2);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS));
		CHECK(CountPairContacts(Snapshot, A->ActorHandle.Index, B->ActorHandle.Index) > 0);
		JoinAtOverlap(*Scene, A, B, true);
		Scene->FlushDeferredActors();
		REQUIRE(Scene->GetConstraints()[0]->IsSimulated());
		const FVector VelocityA = A->State.LinearVelocity;
		Tick(*Scene, 3);
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS));
		CHECK(CountPairContacts(Snapshot, A->ActorHandle.Index, B->ActorHandle.Index) == 0);
		MESSAGE("joint after 2 steps: " << CountPairContacts(Snapshot, A->ActorHandle.Index, B->ActorHandle.Index) << " points left; A's velocity before the joint "
			<< VelocityA.Size() << " cm/s");
	}
	// A world joint with the flag: no effect (no ignore entry anywhere), accepted.
	{
		std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("WorldJointFlag", Config);
		FBodySetup Cube;
		Cube.AggGeom.BoxElems.emplace_back(10.0);
		Cube.PhysMaterial = Scene->GetDefaultPhysicalMaterial();
		const FBodySetup* CubeSetup = Scene->CreateBodySetup(std::move(Cube));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		FBodyInstance* Body = Scene->SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 100.0)));
		FConstraintInstance* Joint = Scene->SpawnConstraint(MakeJoint("WorldFlag", false, FVector(0.0, 0.0, 5.0), X, Y, FVector(0.0, 0.0, 105.0), X, Y, true), Body, nullptr);
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		REQUIRE(Joint->IsSimulated());
		Tick(*Scene, 30);
		CHECK(MeasureJoint(*Joint).Stretch < 0.05);
	}
}

// docs/Abi.md 9.4 items 6 and 9: the snapshot impulse is what the joint gave Body1 in the last step (a bob hanging at rest: m g dt
// up, no angular impulse; abi-v1.1: a rod held level by a world hinge at its end gets the couple -d m g dt about the anchor, not
// the zero net impulse about its COM), a sleeping joint reports zero with PT_JOINTSNAP_SLEEPING, a joint between two non-dynamic
// bodies is accepted without effect, JointIndex counts across CreateConstraints calls, and releasing a body releases its joints
// only.
TEST_CASE("PhysicsTest.BulletBackend.JointSnapshot")
{
	const FResolvedPhysicsConfig Config = LoadConfig("UEParity");
	std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("BulletJointSnapshot", Config);
	FBodySetup Ball;
	Ball.AggGeom.SphereElems.emplace_back(5.0);
	Ball.PhysMaterial = Scene->GetDefaultPhysicalMaterial();
	const FBodySetup* BallSetup = Scene->CreateBodySetup(std::move(Ball));
	FBodyInstance Template;
	Template.bSimulatePhysics = true;
	Template.bOverrideMass = true;
	Template.MassInKgOverride = 1.0f;
	Template.LinearDamping = 0.0f;   // m g dt exactly (the UEParity template damps)
	FBodyInstance* Bob = Scene->SpawnBody(Template, BallSetup, FTransform(FVector(0.0, 0.0, 900.0)));
	FBodySetup Rod;
	Rod.AggGeom.BoxElems.emplace_back(100.0, 4.0, 4.0);   // 1 kg, 100 cm along X, its COM at the actor origin
	Rod.PhysMaterial = Scene->GetDefaultPhysicalMaterial();
	const FBodySetup* RodSetup = Scene->CreateBodySetup(std::move(Rod));
	FBodyInstance* Cantilever = Scene->SpawnBody(Template, RodSetup, FTransform(FVector(0.0, 300.0, 900.0)));
	FBodyInstance Static;
	Static.bStatic = true;
	FBodyInstance* StaticA = Scene->SpawnBody(Static, BallSetup, FTransform(FVector(500.0, 0.0, 0.0)));
	FBodyInstance* StaticB = Scene->SpawnBody(Static, BallSetup, FTransform(FVector(520.0, 0.0, 0.0)));
	const FVector X(1.0, 0.0, 0.0);
	const FVector Y(0.0, 1.0, 0.0);
	FConstraintInstance* Hang = Scene->SpawnConstraint(MakeJoint("Hang", true, FVector(0.0, 0.0, 100.0), X, Y, FVector(0.0, 0.0, 1000.0), X, Y, false), Bob, nullptr);
	// The rod's end at x -50 on a world hinge about X: the swing rows hold the rod level against gravity's torque about the
	// anchor (0, 50 m g, 0); index 1 (the same call).
	FConstraintInstance* Arm = Scene->SpawnConstraint(MakeJoint("Arm", true, FVector(-50.0, 0.0, 0.0), X, Y, FVector(-50.0, 300.0, 900.0), X, Y, false), Cantilever, nullptr);
	Scene->FlushDeferredActors();
	// A second CreateConstraints call: two static bodies (accepted, no effect, index 2).
	FConstraintInstance* Still = Scene->SpawnConstraint(MakeJoint("Still", false, FVector(10.0, 0.0, 0.0), X, Y, FVector(-10.0, 0.0, 0.0), X, Y, false), StaticA, StaticB);
	Scene->FlushDeferredActors();
	Scene->PrepareForSimulation();
	REQUIRE(Hang->IsSimulated());
	REQUIRE(Arm->IsSimulated());
	REQUIRE(Still->IsSimulated());
	CHECK(Hang->ConstraintHandle.Index == 0u);
	CHECK(Arm->ConstraintHandle.Index == 1u);
	CHECK(Still->ConstraintHandle.Index == 2u);

	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	CHECK(Snapshot.Joints[0].LinearImpulse.Z == 0.0);   // nothing solved before the first step
	CHECK(Snapshot.Joints[2].Flags == PT_JOINTSNAP_SLEEPING);   // no dynamic body: never runs
	CHECK(Snapshot.Joints[2].LinearImpulse.X == 0.0);

	Tick(*Scene, 3);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	const double Weight = 1.0 * 980.0 * static_cast<double>(Scene->GetTestSettings().FixedDeltaTime);   // kg cm/s per step
	const PtJointSnapshot& Hanging = Snapshot.Joints[0];
	MESSAGE("hanging bob: joint impulse on Body1 (" << Hanging.LinearImpulse.X << ", " << Hanging.LinearImpulse.Y << ", " << Hanging.LinearImpulse.Z << ") kg cm/s, angular |"
		<< ToVector(Hanging.AngularImpulse).Size() << "| kg cm^2/s; m g dt = " << Weight);
	CHECK(Hanging.Flags == 0u);
	CHECK(Hanging.LinearImpulse.Z == doctest::Approx(Weight).epsilon(5e-3));
	CHECK(std::abs(Hanging.LinearImpulse.X) < 1e-3 * Weight);
	CHECK(ToVector(Hanging.AngularImpulse).Size() < 1e-3 * Weight * 100.0);
	// The world frame is Frame2 as given; Frame1World follows the bob.
	CHECK((ToVector(Hanging.Frame2World.Translation) - FVector(0.0, 0.0, 1000.0)).Size() < 1e-9);
	CHECK((ToVector(Hanging.Frame1World.Translation) - Hang->GetWorldFrame(EConstraintFrame::Frame1).GetTranslation()).Size() < 1e-6);
	// abi-v1.1: the angular impulse is about the anchor (Frame1World's origin) - the couple of the swing rows, -50 m g dt about Y
	// (about the rod's COM the joint's net angular impulse is 0: the linear impulse's moment cancels the couple).
	const PtJointSnapshot& Held = Snapshot.Joints[1];
	MESSAGE("level rod: joint impulse on Body1 (" << Held.LinearImpulse.X << ", " << Held.LinearImpulse.Y << ", " << Held.LinearImpulse.Z << ") kg cm/s, angular ("
		<< Held.AngularImpulse.X << ", " << Held.AngularImpulse.Y << ", " << Held.AngularImpulse.Z << ") kg cm^2/s; -50 m g dt = " << -50.0 * Weight);
	CHECK(Held.Flags == 0u);
	CHECK(Held.LinearImpulse.Z == doctest::Approx(Weight).epsilon(0.05));
	CHECK(Held.AngularImpulse.Y == doctest::Approx(-50.0 * Weight).epsilon(0.1));
	CHECK(std::abs(Held.AngularImpulse.X) < 0.05 * 50.0 * Weight);
	CHECK(std::abs(Held.AngularImpulse.Z) < 0.05 * 50.0 * Weight);

	// UEParity sleep: the resting bob falls asleep (UE thresholds, Bullet's island); its joint then reports zero and SLEEPING.
	int32 AsleepFrame = -1;
	for (int32 Frame = 0; Frame < 120 && AsleepFrame < 0; ++Frame)
	{
		Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
		AsleepFrame = Bob->State.ObjectState == EObjectStateType::Sleeping ? Frame : -1;
	}
	REQUIRE(AsleepFrame >= 0);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	CHECK(Snapshot.Joints[0].Flags == PT_JOINTSNAP_SLEEPING);
	CHECK(Snapshot.Joints[0].LinearImpulse.Z == 0.0);
	MESSAGE("the hanging bob slept after " << (AsleepFrame + 4) << " frames");

	// Releasing a static body releases the joint attached to it, not the bob's (indices stay).
	Scene->ReleaseBody(StaticB);
	CHECK_FALSE(Still->IsSimulated());
	CHECK(Hang->IsSimulated());
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 2u);
	CHECK(Snapshot.Joints[0].JointIndex == 0u);
	CHECK(Snapshot.Joints[1].JointIndex == 1u);
	Scene->ReleaseBody(Bob);
	Scene->ReleaseBody(Cantilever);
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	CHECK(Snapshot.Joints.empty());
	Tick(*Scene, 2);   // the world steps without the released constraints
}

// M9 finding J-1 (Progress M9, PhysScene_BulletJoints.cpp): btPoint2PointConstraint, three scalar rows coupled through the bob's
// rotation (m L^2 / I = 1000 for M6a's bob), needs more than Bullet's 10 iterations; the hinge's frame-offset rows and
// btGeneric6DofSpring2Constraint with the ball's freedoms do not. Pinned so that a Bullet or mapping change shows up.
TEST_CASE("PhysicsTest.BulletBackend.BallJointRowCoupling")
{
	struct FCase
	{
		const char* Label;
		const char* Joint;
		const char* BallJoint;
		const char* Iterations;
	};
	const FCase Cases[] = { { "point2point, 10 iterations", "Ball", "Point2Point", "10" }, { "point2point, 50 iterations", "Ball", "Point2Point", "50" },
		{ "generic6dofspring2, 10 iterations", "Ball", "Generic6DofSpring2", "10" }, { "hinge, 10 iterations", "Hinge", "Point2Point", "10" } };
	double Stretch[4] = {};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FCase& Case = Cases[Index];
		CAPTURE(Case.Label);
		FResolvedPhysicsConfig Config = LoadConfig("Normalized");
		SetSceneOption(Config, "BallJoint", Case.BallJoint);
		SetSceneOption(Config, "NumIterations", Case.Iterations);
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", { { "Joint", Case.Joint } }, Config);
		const auto [MaxStretch, MaxFrame] = RunAndMeasureStretch(*Scene, 120);
		Stretch[Index] = MaxStretch;
		MESSAGE("M6a 5 deg " << Case.Label << ": max stretch " << MaxStretch << " cm (frame " << MaxFrame << ")");
	}
	CHECK(Stretch[0] > 1.0);     // J-1: about 2.3 cm, above the 0.5 cm of docs/Abi.md 9.4 item 13
	CHECK(Stretch[1] < 0.1);
	CHECK(Stretch[2] < 0.05);
	CHECK(Stretch[3] < 0.05);
}

// 03 M9 criterion (Normalized): the M6a hinge period at 5 deg within 0.5 % of the physical pendulum (2.00905 s); 30 and 90 deg
// reported. Period = mean interval between the bob's upward crossings of x = 0 (linear interpolation within the step).
TEST_CASE("PhysicsTest.BulletBackend.PendulumPeriod")
{
	const FResolvedPhysicsConfig Config = LoadConfig("Normalized");
	for (const char* Angle : { "5", "30", "90" })
	{
		CAPTURE(Angle);
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", { { "Angle", Angle } }, Config);
		const double Dt = Analytic::EngineDeltaTime(Scene->GetTestSettings().FixedDeltaTime);
		std::vector<double> Crossings;
		double PreviousX = Scene->GetBodies()[0]->State.BodyTM.GetTranslation().X;
		for (int32 Frame = 0; Frame < BehaviorScenes::M6a::Frames; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			const double CurrentX = Scene->GetBodies()[0]->State.BodyTM.GetTranslation().X;
			if (PreviousX < 0.0 && CurrentX >= 0.0)
			{
				Crossings.push_back((Frame + (-PreviousX) / (CurrentX - PreviousX)) * Dt);
			}
			PreviousX = CurrentX;
		}
		REQUIRE(Crossings.size() >= 3u);
		const double Period = (Crossings.back() - Crossings.front()) / static_cast<double>(Crossings.size() - 1);
		const double Reference = Analytic::PendulumPeriod(BehaviorScenes::M6a::Length, BehaviorScenes::M6a::BobRadius, 980.0, std::stod(Angle) * 3.14159265358979323846 / 180.0);
		const double Error = (Period - Reference) / Reference;
		MESSAGE("M6a hinge " << Angle << " deg: period " << Period << " s over " << (Crossings.size() - 1) << " periods, reference " << Reference << " s, error " << Error * 100.0
			<< " %");
		if (std::string(Angle) == "5")
		{
			CHECK(std::abs(Error) < 0.005);
		}
	}
}

// 03 M9 (Normalized, M6b / M6c): the chain stretch is reported and the energy does not diverge (never above the release energy).
// M9 finding J-2: the 100:1 chain does not hold its end in Bullet (the stretch reaches metres): when the chain snaps taut the
// light links cannot pass the end's momentum on (sequential impulse, mass ratio 100), they spin far above the pi/4-per-step limit
// (bullet.angular_motion_limit_hits: Bullet under-rotates them, the joint rows do not see it) and 50 or 200 iterations change
// nothing. UEParity is reported only: there the M6c energy peaks at 2.3 x the release energy (frame 273). Single runs (chaotic) with one
// UEParity factor removed - the 62.83 rad/s clamp (project MaxAngularVelocity 1e7 deg/s) or the damping 0.01 - stay below it: the
// gain comes with the pre-tick's clamp and damping acting on links that spin past 47-63 rad/s (the pre-tick works on the predicted
// velocity, the joint solve then changes it; PhysScene_Bullet.h). Not isolated further.
TEST_CASE("PhysicsTest.BulletBackend.ChainStretchAndEnergy")
{
	for (const char* Profile : { "Normalized", "UEParity" })
	{
		const FResolvedPhysicsConfig Config = LoadConfig(Profile);
		for (const char* Name : { "M6b.Chain", "M6c.HeavyChain" })
		{
			const std::string Label = std::string(Profile) + " " + Name;
			CAPTURE(Label);
			std::unique_ptr<FPhysScene> Scene = MakeScenario(Name, {}, Config);
			const double Energy0 = MechanicalEnergyJ(*Scene);
			double MaxEnergy = Energy0;
			double MinEnergy = Energy0;
			double MaxStretch = 0.0;
			double LateStretch = 0.0;
			for (int32 Frame = 0; Frame < BehaviorScenes::Chain::Frames; ++Frame)
			{
				Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
				for (const FConstraintInstance* Joint : Scene->GetConstraints())
				{
					const double Stretch = MeasureJoint(*Joint).Stretch;
					MaxStretch = std::max(MaxStretch, Stretch);
					LateStretch = Frame >= 300 ? std::max(LateStretch, Stretch) : LateStretch;
				}
				const double Energy = MechanicalEnergyJ(*Scene);
				MaxEnergy = std::max(MaxEnergy, Energy);
				MinEnergy = std::min(MinEnergy, Energy);
			}
			const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
			MESSAGE(Label << ": max joint stretch " << MaxStretch << " cm (5-10 s: " << LateStretch << " cm); energy " << Energy0 << " J at release, max " << MaxEnergy
				<< " J, min " << MinEnergy << " J; " << RequireSetting(Settings, "bullet.angular_motion_limit_hits").Native);
			if (std::string(Profile) == "Normalized")
			{
				CHECK(MaxEnergy <= Energy0 * 1.01);   // no divergence (03 M9, Normalized)
			}
			if (std::string(Name) == "M6b.Chain")
			{
				CHECK(MaxStretch < 50.0);
			}
		}
	}
}

// ---- sleep -----------------------------------------------------------------------------------------------------------

// SleepThresholds=UE (UEParity's mapping, approximate): a box coming to rest on the ground becomes WANTS_DEACTIVATION in the step that
// ends its SleepCounterThreshold-th (N) consecutive step under the material thresholds, and its island sleeps in the next step: it
// reads back Sleeping at the end of the (N+1)-th step, as Chaos' island counter > N (IslandManager.cpp:2773-2783 [V]). The readback
// velocity of a step is the one Bullet tested. Sensitive bodies (x 1/20) and a material with another counter follow the rule;
// SleepThresholds=Bullet keeps the M5 rule (80 cm/s, 1 rad/s, 2 s).
TEST_CASE("PhysicsTest.BulletBackend.UESleepMapping")
{
	struct FCase
	{
		const char* Label;
		ESleepFamily Family;
		int32 Counter;            // the material's SleepCounterThreshold
		const char* Rule;
		double LinearCm;          // the thresholds the body must get
		double Angular;
	};
	const FCase Cases[] = { { "UE, Normal family, counter 4", ESleepFamily::Normal, 4, "UE", 1.0, 0.05 },
		{ "UE, Sensitive family (x 1/20), counter 4", ESleepFamily::Sensitive, 4, "UE", 1.0 / 20.0, 0.05 / 20.0 },
		{ "UE, Normal family, counter 30", ESleepFamily::Normal, 30, "UE", 1.0, 0.05 },
		{ "Bullet rule", ESleepFamily::Normal, 4, "Bullet", 80.0, 1.0 } };
	for (const FCase& Case : Cases)
	{
		CAPTURE(Case.Label);
		FResolvedPhysicsConfig Config = LoadConfig("UEParity");
		SetSceneOption(Config, "SleepThresholds", Case.Rule);
		std::unique_ptr<FPhysScene> Scene = MakeEmptyScene("BulletSleepRule", Config);
		SpawnGround(*Scene);
		FPhysicalMaterial MaterialTemplate = *Scene->GetDefaultPhysicalMaterial();
		MaterialTemplate.Name = "SleepRule";
		MaterialTemplate.SleepCounterThreshold = Case.Counter;
		const FPhysicalMaterial* Material = Scene->CreatePhysicalMaterial(MaterialTemplate);
		FBodySetup Cube;
		Cube.AggGeom.BoxElems.emplace_back(50.0);
		Cube.PhysMaterial = Material;
		const FBodySetup* CubeSetup = Scene->CreateBodySetup(std::move(Cube));
		FBodyInstance Template;
		Template.bSimulatePhysics = true;
		Template.SleepFamily = Case.Family;
		FBodyInstance* Box = Scene->SpawnBody(Template, CubeSetup, FTransform(FVector(0.0, 0.0, 35.0)));
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
		CHECK(RequireSetting(Settings, "sleep.criteria").Canonical == 0.0);
		const std::string& Criteria = RequireSetting(Settings, "sleep.criteria").Native;
		MESSAGE(Case.Label << ": " << Criteria.substr(0, 200));

		int32 Under = 0;           // consecutive frames under the thresholds before the current one
		int32 UnderAtSleep = -1;
		int32 SleepFrame = -1;
		for (int32 Frame = 0; Frame < 400 && SleepFrame < 0; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			if (Box->State.ObjectState == EObjectStateType::Sleeping)
			{
				SleepFrame = Frame;
				UnderAtSleep = Under;
				break;
			}
			const bool bUnder = Box->State.LinearVelocity.Size() < Case.LinearCm * (1.0 - 1e-6) && Box->State.AngularVelocityInRadians.Size() < Case.Angular * (1.0 - 1e-6);
			const bool bOver = Box->State.LinearVelocity.Size() > Case.LinearCm * (1.0 + 1e-6) || Box->State.AngularVelocityInRadians.Size() > Case.Angular * (1.0 + 1e-6);
			Under = bUnder ? Under + 1 : (bOver ? 0 : Under);
		}
		MESSAGE(Case.Label << ": asleep at frame " << SleepFrame << " after " << UnderAtSleep << " frames under the thresholds");
		REQUIRE(SleepFrame >= 0);
		if (std::string(Case.Rule) == "UE")
		{
			// N consecutive steps under the thresholds, then the step the island sleeps in (it reads back Sleeping).
			CHECK(UnderAtSleep == Case.Counter);
		}
		else
		{
			// Bullet's own rule: gDeactivationTime (2 s) under 80 cm/s and 1 rad/s, then one more step.
			CHECK(UnderAtSleep >= static_cast<int32>(2.0 / Scene->GetTestSettings().FixedDeltaTime));
		}
	}
}

// M9 finding S-2 (UEParity, M2a.Tower N = 10): Bullet's ten-box tower never comes to rest - its fastest box creeps faster and
// faster (about 2 cm/s after 1 s, 8-9 cm/s after 10 s), so under the UE thresholds (1 cm/s, 0.05 rad/s) it never sleeps, while
// Bullet's own rule (80 cm/s, 2 s) puts it to sleep at frame ~121 and freezes the creep (Progress M9 prep F-5's 121 frames).
// The creep is the Bullet stack behaviour of Checkpoints N-1 (10 iterations, restitution 0.3), not the sleep mapping.
TEST_CASE("PhysicsTest.BulletBackend.TowerRestUnderBothRules")
{
	for (const char* Rule : { "UE", "Bullet" })
	{
		CAPTURE(Rule);
		FResolvedPhysicsConfig Config = LoadConfig("UEParity");
		SetSceneOption(Config, "SleepThresholds", Rule);
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M2a.Tower", { { "N", "10" } }, Config);
		int32 AllAsleep = -1;
		double SpeedAt1s = 0.0;
		double SpeedAt10s = 0.0;
		for (int32 Frame = 0; Frame < BehaviorScenes::M2a::Frames; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			double MaxSpeed = 0.0;
			int32 Asleep = 0;
			int32 Dynamic = 0;
			for (const FBodyInstance* Body : Scene->GetBodies())
			{
				if (Body->IsDynamic())
				{
					++Dynamic;
					Asleep += Body->State.ObjectState == EObjectStateType::Sleeping ? 1 : 0;
					MaxSpeed = std::max(MaxSpeed, Body->State.LinearVelocity.Size());
				}
			}
			AllAsleep = AllAsleep < 0 && Asleep == Dynamic ? Frame : AllAsleep;
			SpeedAt1s = Frame == 59 ? MaxSpeed : SpeedAt1s;
			SpeedAt10s = Frame == BehaviorScenes::M2a::Frames - 1 ? MaxSpeed : SpeedAt10s;
		}
		MESSAGE("M2a.Tower N=10 UEParity, SleepThresholds=" << Rule << ": every box asleep first at frame " << AllAsleep << "; fastest box " << SpeedAt1s << " cm/s at 1 s, "
			<< SpeedAt10s << " cm/s at 10 s");
		if (std::string(Rule) == "UE")
		{
			CHECK(AllAsleep < 0);
			CHECK(SpeedAt10s > SpeedAt1s);
		}
		else
		{
			CHECK(AllAsleep >= 0);
			CHECK(AllAsleep < 200);
		}
	}
}

// ---- native debug draw (viz runs) --------------------------------------------------------------------------------------

// 03 M9 criterion: the viz drawer does not change the results (final h_native equal with and without it, UEParity, sleep on) -
// here every frame of M7.UEDefaults (its six bodies fall asleep under the UE rule, frame 98) with the native draw captured every
// frame (the Bullet tower of M2a never comes to rest under the UE thresholds: finding S-2). The Normalized run (DBG_NoDeactivation
// set) leaves the process global gDisableDeactivation as it found it, so a later UEParity scene in the same process still sleeps on
// the same frame.
TEST_CASE("PhysicsTest.BulletBackend.VizDrawerKeepsResults")
{
	const auto RunScene = [](const FResolvedPhysicsConfig& Config, const bool bCapture, std::vector<uint64>& OutHashes, int32& OutAsleep, int32& OutCommands)
	{
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M7.UEDefaults", {}, Config);
		OutHashes.clear();
		OutAsleep = 0;
		OutCommands = 0;
		FPhysicsDebugSnapshot Snapshot;
		for (int32 Frame = 0; Frame < BehaviorScenes::M7::Frames; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			if (bCapture)
			{
				REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS | PT_SNAP_NATIVE_DRAW));
				OutCommands = std::max(OutCommands, static_cast<int32>(Snapshot.NativeCommands.size()));
			}
			OutHashes.push_back(Scene->ComputeNativeStateHash());
		}
		for (const FBodyInstance* Body : Scene->GetBodies())
		{
			OutAsleep += Body->State.ObjectState == EObjectStateType::Sleeping ? 1 : 0;
		}
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
		return RequireSetting(Settings, "bullet.debug_drawer").Canonical;
	};
	const FResolvedPhysicsConfig Bench = LoadConfig("UEParity");
	FResolvedPhysicsConfig Viz = Bench;
	Viz.TestSettings.RunMode = EPhysicsRunMode::Viz;
	std::vector<uint64> BenchHashes;
	std::vector<uint64> VizHashes;
	int32 BenchAsleep = 0;
	int32 VizAsleep = 0;
	int32 BenchCommands = 0;
	int32 VizCommands = 0;
	CHECK(RunScene(Bench, false, BenchHashes, BenchAsleep, BenchCommands) == 0.0);
	CHECK(RunScene(Viz, true, VizHashes, VizAsleep, VizCommands) == 1.0);
	MESSAGE("M7.UEDefaults UEParity: " << BenchAsleep << " / " << VizAsleep << " bodies asleep at the last frame (bench / viz), up to " << VizCommands
		<< " native commands per capture; final h_native 0x" << std::hex << BenchHashes.back() << " / 0x" << VizHashes.back() << std::dec);
	CHECK(BenchAsleep >= 6);    // sleep really happened: the drawer's DBG_NoDeactivation must have been clear
	CHECK(VizCommands > 10);
	REQUIRE(BenchHashes.size() == VizHashes.size());
	size_t FirstDifference = BenchHashes.size();
	for (size_t Frame = 0; Frame < BenchHashes.size() && FirstDifference == BenchHashes.size(); ++Frame)
	{
		FirstDifference = BenchHashes[Frame] != VizHashes[Frame] ? Frame : FirstDifference;
	}
	CHECK(FirstDifference == BenchHashes.size());

	// Normalized viz (sleep off): the drawer carries DBG_NoDeactivation, stepSimulation sets gDisableDeactivation for this scene's
	// steps, and the scene restores it.
	{
		FResolvedPhysicsConfig Normalized = LoadConfig("Normalized");
		Normalized.TestSettings.RunMode = EPhysicsRunMode::Viz;
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M6a.Pendulum", {}, Normalized);
		Tick(*Scene, 10);
		const std::vector<FEffectiveSetting> Settings = ReadEffective(Scene->DumpEffectiveSettings());
		CHECK(RequireSetting(Settings, "bullet.debug_drawer").Native.find("DBG_NoDeactivation set") != std::string::npos);
		CHECK(RequireSetting(ReadModuleSettings(*Scene), "bullet.disable_deactivation_global").Canonical == 0.0);
	}
	std::vector<uint64> AfterHashes;
	int32 AfterAsleep = 0;
	int32 AfterCommands = 0;
	RunScene(Bench, false, AfterHashes, AfterAsleep, AfterCommands);
	CHECK(AfterAsleep == BenchAsleep);
	CHECK(AfterHashes.back() == BenchHashes.back());
}

// The native draw commands of a viz capture: Bullet's shapes keep their primitives (boxes with their full extents, spheres,
// capsules with UE's Extent (radius, half height + radius) along the link axis), contact points come as a point and a normal
// arrow, joints as frame lines; colours are opaque; a bench-mode scene and a request for none give no command; a small
// MaxNativeCommands truncates.
TEST_CASE("PhysicsTest.BulletBackend.NativeDrawCommands")
{
	FResolvedPhysicsConfig Viz = LoadConfig("Normalized");
	Viz.TestSettings.RunMode = EPhysicsRunMode::Viz;
	// A box resting on the ground (M1 a): two boxes and the resting contact.
	{
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Viz);
		Tick(*Scene, 120);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW));
		CHECK_FALSE(Snapshot.bTruncated);
		CHECK(CountNative(Snapshot, PT_DD_BOX) == 2);
		CHECK(CountNative(Snapshot, PT_DD_POINT) >= 4);
		CHECK(CountNative(Snapshot, PT_DD_ARROW) == CountNative(Snapshot, PT_DD_POINT));
		const FBodyInstance& Box = *Scene->GetBodies().back();
		bool bFoundBox = false;
		for (const PtDebugDrawCmd& Command : Snapshot.NativeCommands)
		{
			CHECK((Command.ColorBGRA >> 24) == 0xFFu);
			CHECK(Command.Lifetime == 0.0f);   // docs/Abi.md 9.2 (M9 fix round 2): 0 = this capture only, as PhysX
			CHECK(Command.Reserved == 0u);
			if (Command.Type == PT_DD_BOX && (ToVector(Command.A) - Box.State.BodyTM.GetTranslation()).Size() < 0.01)
			{
				bFoundBox = true;
				CHECK(Command.Extent.X == doctest::Approx(50.0).epsilon(1e-5));   // the M1 cube's half extent (Bullet's box keeps its margin inside)
				const FQuat Rotation(Command.Rotation.X, Command.Rotation.Y, Command.Rotation.Z, Command.Rotation.W);
				CHECK((Rotation.GetAxisZ() - Box.State.BodyTM.GetRotation().GetAxisZ()).Size() < 1e-5);
			}
			if (Command.Type == PT_DD_ARROW)
			{
				CHECK((ToVector(Command.B) - ToVector(Command.A)).Size() == doctest::Approx(5.0).epsilon(1e-5));
			}
		}
		CHECK(bFoundBox);
		MESSAGE("M1 a viz capture: " << Snapshot.NativeCommands.size() << " native commands (" << CountNative(Snapshot, PT_DD_BOX) << " boxes, " << CountNative(Snapshot, PT_DD_POINT)
			<< " contact points)");
		// MaxNativeCommands truncates (the raw ABI request).
		const PtBackendApi& Api = Scene->GetBackendScene().GetApi();
		PtDebugSnapshotRequest Request = {};
		Request.StructSize = sizeof(Request);
		Request.Flags = PT_SNAP_NATIVE_DRAW;
		Request.MaxNativeCommands = 3;
		PtDebugSnapshotCounts Counts = {};
		REQUIRE(Api.CaptureDebugSnapshot(Scene->GetBackendScene().GetSceneId(), &Request, &Counts) == PT_OK);
		CHECK(Counts.NumNativeCommands == 3u);
		CHECK(Counts.bTruncated == 1u);
		std::vector<PtDebugDrawCmd> Commands(3);
		PtDebugSnapshotBuffers Buffers = {};
		Buffers.NativeCommands = Commands.data();
		Buffers.CommandCapacity = 2;
		Buffers.CommandStride = sizeof(PtDebugDrawCmd);
		CHECK(Api.ReadDebugSnapshot(Scene->GetBackendScene().GetSceneId(), &Buffers) == PT_E_CAPACITY);
		Buffers.CommandCapacity = 3;
		CHECK(Api.ReadDebugSnapshot(Scene->GetBackendScene().GetSceneId(), &Buffers) == PT_OK);
		// Without PT_SNAP_NATIVE_DRAW: none.
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES));
		CHECK(Snapshot.NativeCommands.empty());
	}
	// The chain: one capsule per link along the link axis, the ball joints' frames as lines.
	{
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M6b.Chain", {}, Viz);
		Tick(*Scene, 5);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW));
		REQUIRE(CountNative(Snapshot, PT_DD_CAPSULE) == BehaviorScenes::M6b::NumLinks);
		CHECK(CountNative(Snapshot, PT_DD_LINE) >= 3 * BehaviorScenes::M6b::NumLinks);   // the joint frames (DBG_DrawConstraints)
		for (const PtDebugDrawCmd& Command : Snapshot.NativeCommands)
		{
			if (Command.Type != PT_DD_CAPSULE)
			{
				continue;
			}
			CHECK(Command.Extent.X == doctest::Approx(BehaviorScenes::Chain::LinkRadius).epsilon(1e-5));
			CHECK(Command.Extent.Y == doctest::Approx(BehaviorScenes::Chain::LinkSegment / 2.0 + BehaviorScenes::Chain::LinkRadius).epsilon(1e-5));
			// The link whose centre this is: the capsule axis is the link's local Z.
			const auto Link = std::find_if(Scene->GetBodies().begin(), Scene->GetBodies().end(),
				[&Command](const FBodyInstance* Body) { return (Body->State.BodyTM.GetTranslation() - ToVector(Command.A)).Size() < 0.01; });
			REQUIRE(Link != Scene->GetBodies().end());
			const FQuat Rotation(Command.Rotation.X, Command.Rotation.Y, Command.Rotation.Z, Command.Rotation.W);
			CHECK(std::abs(Rotation.GetAxisZ() | (*Link)->State.BodyTM.GetRotation().GetAxisZ()) > 1.0 - 1e-6);
		}
	}
	// A bench-mode scene has no drawer: no native command even when asked.
	{
		const FResolvedPhysicsConfig Bench = LoadConfig("Normalized");
		std::unique_ptr<FPhysScene> Scene = MakeScenario("M1.SingleBoxDrop", { { "Case", "a" } }, Bench);
		Tick(*Scene, 2);
		FPhysicsDebugSnapshot Snapshot;
		REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW));
		CHECK(Snapshot.NativeCommands.empty());
	}
}

// ---- Bullet.StaticAabbOff (G11, Progress M9 F-1) ------------------------------------------------------------------------

// ForceUpdateAllAabbs=false recomputes only the active objects' AABBs. Where the solver order does not follow the broad phase's
// pair discovery (one body, joint chains, a small tower) the two variants are bit-identical every frame in both profiles. On
// M3.BoxPile the static ground's proxy moves to Bullet's fixed tree after two steps without an update (btDbvtBroadphase::collide,
// setAabb [V]), the trees then find pairs in another order and the solver follows that order: the hashes part (F-1). With
// bEnableEnhancedDeterminism (btDispatcherInfo::m_deterministicOverlappingPairs: pairs processed and manifolds sorted by the
// bodies' ids, btOverlappingPairCache.cpp:374, btSimulationIslandManager.cpp:376-378 [V]) they stay identical - in Normalized;
// in UEParity the pile still parts later (bench G11: frame 408 of 600), when bodies sleep and their AABBs are no longer refreshed
// ([R] a sleeping body keeps the swept AABB of its last awake step: other pairs, other islands).
TEST_CASE("PhysicsTest.BulletBackend.StaticAabbOffNeutrality")
{
	const auto Hashes = [](const char* ScenarioName, const FScenarioParams& Params, FResolvedPhysicsConfig Config, const bool bStaticAabbOff, const int32 Frames)
	{
		SetSceneOption(Config, "ForceUpdateAllAabbs", bStaticAabbOff ? "false" : "true");
		std::unique_ptr<FPhysScene> Scene = MakeScenario(ScenarioName, Params, Config);
		std::vector<uint64> Out;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Scene->Tick(Scene->GetTestSettings().FixedDeltaTime);
			Out.push_back(Scene->ComputeNativeStateHash());
		}
		return Out;
	};
	const auto FirstDifference = [](const std::vector<uint64>& A, const std::vector<uint64>& B) -> int32
	{
		for (size_t Frame = 0; Frame < A.size(); ++Frame)
		{
			if (A[Frame] != B[Frame])
			{
				return static_cast<int32>(Frame);
			}
		}
		return -1;
	};
	struct FCase
	{
		const char* Scenario;
		FScenarioParams Params;
		const char* Profile;
		bool bDeterminism;
		int32 Frames;
		bool bExpectEqual;
	};
	const FCase Cases[] = { { "M1.SingleBoxDrop", { { "Case", "b" } }, "UEParity", false, 300, true }, { "M6b.Chain", {}, "UEParity", false, 300, true },
		{ "M2a.Tower", { { "N", "10" } }, "UEParity", false, 600, true }, { "M3.BoxPile", { { "N", "1000" } }, "Normalized", false, 120, false },
		{ "M3.BoxPile", { { "N", "1000" } }, "Normalized", true, 300, true } };
	for (const FCase& Case : Cases)
	{
		const std::string Label = std::string(Case.Scenario) + " " + Case.Profile + (Case.bDeterminism ? " + bEnableEnhancedDeterminism" : "");
		CAPTURE(Label);
		FResolvedPhysicsConfig Config = LoadConfig(Case.Profile);
		Config.Settings.bEnableEnhancedDeterminism = Case.bDeterminism;
		const std::vector<uint64> Default = Hashes(Case.Scenario, Case.Params, Config, false, Case.Frames);
		const std::vector<uint64> Off = Hashes(Case.Scenario, Case.Params, Config, true, Case.Frames);
		const int32 Difference = FirstDifference(Default, Off);
		MESSAGE("StaticAabbOff vs Default, " << Label << ": " << (Difference < 0 ? std::string("identical for ") + std::to_string(Case.Frames) + " frames"
			: "first different frame " + std::to_string(Difference)));
		CHECK((Difference < 0) == Case.bExpectEqual);
	}
}
