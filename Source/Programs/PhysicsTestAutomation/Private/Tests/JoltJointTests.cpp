// JoltJointTests.cpp - M9 Jolt joints and Jolt profile items (03_WorkPlan M9, docs/Abi.md 9.4): PointConstraint /
// HingeConstraint behind CreateConstraints (frames, Body1 / Body2, the world), the joint snapshot (frames, impulses on Body1
// with the right sign and units, the sleeping flag), PT_JOINTF_DISABLE_COLLISION through the group filter, the release
// rules, the M9 completion figures on Jolt (M6a period within 0.5 % of the physical pendulum, M6b / M6c stretch and energy),
// the UEParity sleep threshold mapping (SleepThresholdMapping) and the Jolt.SharpBoxes variant. Every case skips (passing,
// with a message) in a build without the Jolt backend in the registry.
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
#include <vector>

using namespace PhysicsTest;

namespace
{
	constexpr double GPi = 3.14159265358979323846;

	bool HasJolt()
	{
		const std::vector<std::string> Names = FPhysicsBackendRegistry::Get().GetAvailableNames();
		if (std::find(Names.begin(), Names.end(), "Jolt") != Names.end())
		{
			return true;
		}
		MESSAGE("Jolt is not in this build's backend registry: skipped");
		return false;
	}

	std::filesystem::path JoltJointConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	// Jolt.<Variant> ("" = Jolt.Default) under Profile, as the bench resolves it.
	FResolvedPhysicsConfig LoadJoltConfig(const std::string& Profile, const std::string& Variant = {})
	{
		FBenchRunSpec Spec;
		Spec.Backend = "Jolt";
		Spec.BackendProfile = Variant;
		Spec.PhysicsProfile = Profile;
		Spec.ConfigDir = JoltJointConfigDir();
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		return Config;
	}

	void SetSceneOption(FResolvedPhysicsConfig& Config, const std::string& Key, const std::string& Value)
	{
		for (auto& [Name, Current] : Config.SceneOptions)
		{
			if (Name == Key)
			{
				Current = Value;
				return;
			}
		}
		Config.SceneOptions.emplace_back(Key, Value);
	}

	// A scenario built as the bench builds it, prepared for simulation.
	std::unique_ptr<FPhysScene> BuildScenario(const char* ScenarioName, const FScenarioParams& Overrides, const FResolvedPhysicsConfig& Config)
	{
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(ScenarioName);
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		std::string Error;
		REQUIRE_MESSAGE(Scenario->ResolveParams(Overrides, Params, Error), Error);
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = ScenarioName;
		InitParams.BackendName = "Jolt";
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

	// A hand-built Jolt scene (the tests below author their own bodies and joints).
	std::unique_ptr<FPhysScene> MakeJoltScene(const char* Name, const FResolvedPhysicsConfig& Config, const uint32 InjectedInstrumentFlags = 0)
	{
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = Name;
		InitParams.BackendName = "Jolt";
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = 16;
		InitParams.MaxBodyPairsHint = 256;
		InitParams.MaxContactConstraintsHint = 256;
		InitParams.InjectedSceneInstrumentFlags = InjectedInstrumentFlags;
		return std::make_unique<FPhysScene>(InitParams);
	}

	FBodyInstance MakeTemplate(const FResolvedPhysicsConfig& Config, const bool bDynamic, const float MassKg = 1.0f)
	{
		FBodyInstance Template;
		FProfileResolver::ApplyBodyInstanceDefaults(Config, Template);
		Template.bSimulatePhysics = bDynamic;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = MassKg;
		return Template;
	}

	const FBodySetup* MakeSphereSetup(FPhysScene& Scene, const double Radius)
	{
		FBodySetup Setup;
		Setup.AggGeom.SphereElems.emplace_back(Radius);
		Setup.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		return Scene.CreateBodySetup(std::move(Setup));
	}

	const FBodySetup* MakeBoxSetup(FPhysScene& Scene, const double X, const double Y, const double Z)
	{
		FBodySetup Setup;
		Setup.AggGeom.BoxElems.emplace_back(X, Y, Z);
		Setup.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
		return Scene.CreateBodySetup(std::move(Setup));
	}

	// A ball (ConfigureAsBallAndSocket) or hinge (ConfigureAsHinge: twist about the frames' X) joint with the given frames.
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
		double Stretch = 0.0;      // cm
		double AxisAngle = 0.0;    // rad
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

	FVector ToVector(const PtVec3& V)
	{
		return FVector(V.X, V.Y, V.Z);
	}

	// The rotation angle between two orientations (q and -q are the same rotation).
	double QuatAngle(const FQuat& A, const FQuat& B)
	{
		const double Dot = std::abs(A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W);
		return 2.0 * std::acos(std::min(1.0, Dot));
	}

	// Ticks the scene and returns the OR of the engine error bits of those frames.
	uint32 TickFrames(FPhysScene& Scene, const float Dt, const int32 NumFrames)
	{
		uint32 Bits = 0;
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			Scene.Tick(Dt);
			Bits |= Scene.GetStats().Last.EngineErrorBits;
		}
		return Bits;
	}

	std::vector<FEffectiveSetting> ReadEffective(const FPhysScene& Scene)
	{
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		const bool bParsed = ParseEffectiveSettings(Scene.DumpEffectiveSettings(), Settings, Error);
		INFO(Error);
		REQUIRE(bParsed);
		return Settings;
	}

	const FEffectiveSetting& FindSetting(const std::vector<FEffectiveSetting>& Settings, const char* Key)
	{
		const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
		INFO(Key);
		REQUIRE(Found != Settings.end());
		return *Found;
	}

	// Mechanical energy of every dynamic body (kg cm^2 / s^2): translation, rotation about the principal axes, gravity.
	double MechanicalEnergy(const FPhysScene& Scene, const double GravityZ)
	{
		double Energy = 0.0;
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			if (!Body->bSimulatePhysics || Body->State.ObjectState == EObjectStateType::Uninitialized)
			{
				continue;
			}
			const FMassProperties& Mass = Body->MassProperties;
			const FQuat Rotation = Body->State.BodyTM.GetRotation();
			const FVector Com = Body->State.BodyTM.GetTranslation() + Rotation.RotateVector(Mass.CenterOfMass);
			const FVector V = Body->State.LinearVelocity;
			const FVector W = Body->State.AngularVelocityInRadians;
			const FQuat Principal = Rotation * Mass.RotationOfMass;
			const FVector Axes[3] = { Principal.GetAxisX(), Principal.GetAxisY(), Principal.GetAxisZ() };
			const double Moments[3] = { Mass.InertiaTensor.X, Mass.InertiaTensor.Y, Mass.InertiaTensor.Z };
			double Rotational = 0.0;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const double Component = W | Axes[Axis];
				Rotational += Moments[Axis] * Component * Component;
			}
			Energy += 0.5 * Mass.Mass * (V | V) + 0.5 * Rotational - Mass.Mass * GravityZ * Com.Z;
		}
		return Energy;
	}

	// The period from the bob's x crossings of 0 (linear interpolation inside the frame): (last - first) / (crossings - 1) x 2.
	double MeasurePeriod(const std::vector<double>& X, const double Dt)
	{
		std::vector<double> Crossings;
		for (size_t Frame = 1; Frame < X.size(); ++Frame)
		{
			if ((X[Frame - 1] > 0.0) != (X[Frame] > 0.0))
			{
				const double Fraction = X[Frame - 1] / (X[Frame - 1] - X[Frame]);
				Crossings.push_back((static_cast<double>(Frame) + Fraction) * Dt);   // frame f's state is at the end of step f + 1
			}
		}
		REQUIRE(Crossings.size() >= 3u);
		return 2.0 * (Crossings.back() - Crossings.front()) / static_cast<double>(Crossings.size() - 1);
	}
}

// 03 M9 completion: M6a.Pendulum at 5 deg has a period within 0.5 % of the physical-pendulum reference (2.00905 s, g 980, L 100,
// r 5), hinge and ball, Normalized. 30 deg is held to the same bound. 90 deg is reported, not bounded: Jolt's velocity-level
// joint solve projects the velocity onto the new tangent every step and loses about (omega dt)^2 of the kinetic energy per
// step (0.5 % at the bottom of a 90 deg swing, 2e-5 at 5 deg), so the amplitude decays and the measured mean period falls
// toward the small-angle value (engine behaviour, tagged algorithmic in the M9 notes). The energy loss after 10 s is reported
// for every angle.
TEST_CASE("PhysicsTest.JoltJoint.PendulumPeriod")
{
	if (!HasJolt())
	{
		return;
	}
	const FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized");
	const double Dt = Analytic::EngineDeltaTime(Config.TestSettings.FixedDeltaTime);
	const double GravityZ = Config.Settings.DefaultGravityZ;
	for (const std::string Angle : { "5", "30", "90" })
	{
		for (const std::string JointType : { "Hinge", "Ball" })
		{
			CAPTURE(Angle);
			CAPTURE(JointType);
			std::unique_ptr<FPhysScene> Scene = BuildScenario(BehaviorScenes::M6a::Name, { { "Angle", Angle }, { "Joint", JointType } }, Config);
			REQUIRE(Scene->GetConstraints()[0]->IsSimulated());
			const double E0 = MechanicalEnergy(*Scene, GravityZ);
			const double Drop = BehaviorScenes::M6a::Length * (1.0 - std::cos(std::stod(Angle) * GPi / 180.0)) * std::abs(GravityZ);   // per kg
			std::vector<double> X;
			double MaxStretch = 0.0;
			double MaxY = 0.0;
			double MaxEnergyRise = -1e300;   // symplectic Euler: the frame-end energy oscillates by O(dt) around the shadow energy
			for (int32 Frame = 0; Frame < BehaviorScenes::M6a::Frames; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
				const FVector Bob = Scene->GetBodies()[0]->State.BodyTM.GetTranslation();
				X.push_back(Bob.X);
				MaxY = std::max(MaxY, std::abs(Bob.Y));
				MaxStretch = std::max(MaxStretch, MeasureJoint(*Scene->GetConstraints()[0]).Stretch);
				MaxEnergyRise = std::max(MaxEnergyRise, MechanicalEnergy(*Scene, GravityZ) - E0);
			}
			const double EnergyLoss = (E0 - MechanicalEnergy(*Scene, GravityZ)) / (BehaviorScenes::M6a::BobMassKg * Drop);
			const double Period = MeasurePeriod(X, Dt);
			const double Reference = Analytic::PendulumPeriod(BehaviorScenes::M6a::Length, BehaviorScenes::M6a::BobRadius, 980.0, std::stod(Angle) * GPi / 180.0);
			const double RelError = (Period - Reference) / Reference;
			MESSAGE("M6a " << Angle << " deg " << JointType << ": period " << Period << " s, reference " << Reference << " s, error " << RelError * 100.0
				<< " %, max stretch " << MaxStretch << " cm, max |y| " << MaxY << " cm, energy lost after 10 s " << EnergyLoss * 100.0
				<< " % of the swing energy m g L (1 - cos a)");
			if (Angle != "90")
			{
				CHECK(std::abs(RelError) < 0.005);
			}
			CHECK(MaxStretch < 0.5);
			CHECK(MaxY < 1e-3);                                 // the swing stays in the XZ plane (the ball has no reason to leave it either)
			CHECK(MaxEnergyRise < 0.05 * BehaviorScenes::M6a::BobMassKg * Drop);   // no growth beyond the integrator's oscillation
		}
	}
}

// 03 M9 completion: M6b / M6c report the chain stretch, and the energy does not diverge (Normalized: no damping, so the
// mechanical energy may only fall by the solver's dissipation; bound: it never rises above the start by more than 0.1 % of the
// energy the chain can release, sum m g x chain length). The stretch is reported, and with Jolt.Default's steps (10 velocity,
// 2 position) bounded only by "the chain did not come apart" (one link pitch, 40 cm): Jolt's iterative solve does not converge
// at 100 : 1 (M6c). Two more M6c runs (findings for the M9 notes, not defaults): 40 velocity + 10 position steps shrink the
// late stretch (convergence, not a frame error), while 40 velocity + 2 position steps let the chain come apart - the whipping
// light links drift faster than two position steps per frame correct (reported only).
TEST_CASE("PhysicsTest.JoltJoint.ChainStretchAndEnergy")
{
	if (!HasJolt())
	{
		return;
	}
	struct FCase
	{
		const char* Name;
		const char* VelocitySteps;
		const char* PositionSteps;
		bool bBounded;
	};
	double HeavyLateStretch[2] = { 0.0, 0.0 };   // Jolt.Default steps, 40 / 10
	const FCase Cases[] = { FCase{ BehaviorScenes::M6b::Name, "10", "2", true }, FCase{ BehaviorScenes::M6c::Name, "10", "2", true },
		FCase{ BehaviorScenes::M6c::Name, "40", "10", true }, FCase{ BehaviorScenes::M6c::Name, "40", "2", false } };
	for (const FCase& Case : Cases)
	{
		const std::string Name = Case.Name;
		const std::string Steps = std::string(Case.VelocitySteps) + " / " + Case.PositionSteps;
		CAPTURE(Name);
		CAPTURE(Steps);
		FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized");
		SetSceneOption(Config, "NumVelocitySteps", Case.VelocitySteps);
		SetSceneOption(Config, "NumPositionSteps", Case.PositionSteps);
		const double GravityZ = Config.Settings.DefaultGravityZ;
		std::unique_ptr<FPhysScene> Scene = BuildScenario(Case.Name, {}, Config);
		for (const FConstraintInstance* Joint : Scene->GetConstraints())
		{
			REQUIRE(Joint->IsSimulated());
		}
		double TotalMass = 0.0;
		for (const FBodyInstance* Body : Scene->GetBodies())
		{
			TotalMass += Body->MassProperties.Mass;
		}
		const double ChainLength = 2.0 * BehaviorScenes::Chain::LinkHalfPitch * static_cast<double>(Scene->GetBodies().size());
		const double EnergyScale = TotalMass * std::abs(GravityZ) * ChainLength;
		const double E0 = MechanicalEnergy(*Scene, GravityZ);
		double MaxEnergyRise = -1e300;
		double MaxStretch = 0.0;
		double MaxStretchFirstSecond = 0.0;
		double MaxStretchLate = 0.0;
		size_t WorstJoint = 0;
		bool bFinite = true;
		for (int32 Frame = 0; Frame < BehaviorScenes::Chain::Frames; ++Frame)
		{
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
			double FrameStretch = 0.0;
			for (size_t Index = 0; Index < Scene->GetConstraints().size(); ++Index)
			{
				const double Stretch = MeasureJoint(*Scene->GetConstraints()[Index]).Stretch;
				WorstJoint = Stretch > MaxStretch ? Index : WorstJoint;
				FrameStretch = std::max(FrameStretch, Stretch);
				MaxStretch = std::max(MaxStretch, Stretch);
			}
			const double Energy = MechanicalEnergy(*Scene, GravityZ);
			bFinite = bFinite && std::isfinite(Energy) && std::isfinite(FrameStretch);
			MaxEnergyRise = std::max(MaxEnergyRise, Energy - E0);
			MaxStretchFirstSecond = Frame < 60 ? std::max(MaxStretchFirstSecond, FrameStretch) : MaxStretchFirstSecond;
			MaxStretchLate = Frame >= 300 ? std::max(MaxStretchLate, FrameStretch) : MaxStretchLate;
		}
		const double EnergyEnd = MechanicalEnergy(*Scene, GravityZ);
		MESSAGE(Name << " (velocity / position steps " << Steps << std::string(Case.bBounded ? "" : ", reported only") << "): max stretch " << MaxStretch << " cm at joint "
			<< WorstJoint << " (first second " << MaxStretchFirstSecond << ", 5-10 s " << MaxStretchLate << "); energy: max rise above E0 "
			<< 100.0 * MaxEnergyRise / EnergyScale << " %, end - E0 " << 100.0 * (EnergyEnd - E0) / EnergyScale << " % of sum m g L = " << EnergyScale
			<< " kg cm^2/s^2");
		CHECK(bFinite);
		if (!Case.bBounded)
		{
			continue;
		}
		CHECK(MaxEnergyRise < 1e-3 * EnergyScale);
		CHECK(MaxStretch < 2.0 * BehaviorScenes::Chain::LinkHalfPitch);
		if (Name == BehaviorScenes::M6c::Name)
		{
			HeavyLateStretch[std::string(Case.VelocitySteps) == "10" ? 0 : 1] = MaxStretchLate;
		}
	}
	CHECK(HeavyLateStretch[1] < 0.5 * HeavyLateStretch[0]);
}

// The impulses of the joint snapshot (docs/Abi.md 9.4 item 9): what the joint applied to Body1 in the last step, world space,
// kg cm/s and kg cm^2/s. A 1 kg rod (100 x 10 x 10 cm along X) at rest on a world hinge at its end whose axis (world X) runs
// through its centre 50 cm away: the joint carries the weight (+m g dt up at the pivot) and the swing lock carries the weight's
// moment about Y (a couple of -L m g dt about Y); a ball joint straight above a hanging sphere carries only the weight and has
// no angular part. The snapshot frames match the host's world frames (both sides). (A rod, not a sphere: a 1 kg sphere of
// radius 5 has I = 10 kg cm^2 against m L^2 = 2500 on the 50 cm lever, and Jolt's sequential point / rotation parts converge
// so slowly at that ratio that the sphere sags - engine behaviour, M9 notes.)
TEST_CASE("PhysicsTest.JoltJoint.ImpulsesAtRest")
{
	if (!HasJolt())
	{
		return;
	}
	const FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized");
	const double Dt = Analytic::EngineDeltaTime(Config.TestSettings.FixedDeltaTime);
	const double G = std::abs(Config.Settings.DefaultGravityZ);
	constexpr double L = 50.0;
	std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltJointImpulses", Config);
	const FBodySetup* Sphere = MakeSphereSetup(*Scene, 5.0);
	const FBodySetup* Rod = MakeBoxSetup(*Scene, 2.0 * L, 10.0, 10.0);
	const FBodyInstance Template = MakeTemplate(Config, true, 1.0f);
	FBodyInstance* Beam = Scene->SpawnBody(Template, Rod, FTransform(FVector(L, 0.0, 500.0)));
	FBodyInstance* Hanging = Scene->SpawnBody(Template, Sphere, FTransform(FVector(300.0, 0.0, 500.0 - L)));
	const FVector X(1.0, 0.0, 0.0);
	const FVector Y(0.0, 1.0, 0.0);
	FConstraintInstance* Hinge = Scene->SpawnConstraint(MakeJoint("Hinge", true, FVector(-L, 0.0, 0.0), X, Y, FVector(0.0, 0.0, 500.0), X, Y, false), Beam, nullptr);
	FConstraintInstance* Ball = Scene->SpawnConstraint(MakeJoint("Ball", false, FVector(0.0, 0.0, L), X, Y, FVector(300.0, 0.0, 500.0), X, Y, false), Hanging, nullptr);
	Scene->FlushDeferredActors();
	Scene->PrepareForSimulation();
	REQUIRE(Hinge->IsSimulated());
	REQUIRE(Ball->IsSimulated());
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
	}
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 2u);
	REQUIRE(Snapshot.JointHostIndex.size() == 2u);
	const double Weight = 1.0 * G * Dt;   // kg cm/s
	for (size_t Index = 0; Index < Snapshot.Joints.size(); ++Index)
	{
		const PtJointSnapshot& Joint = Snapshot.Joints[Index];
		CAPTURE(Index);
		CHECK(Joint.JointIndex == static_cast<uint32>(Index));
		CHECK(Snapshot.JointHostIndex[Index] == static_cast<int32>(Index));
		CHECK(Joint.Flags == 0u);
		const FConstraintInstance& Host = *Scene->GetConstraints()[Index];
		for (const EConstraintFrame Frame : { EConstraintFrame::Frame1, EConstraintFrame::Frame2 })
		{
			const FTransform HostFrame = Host.GetWorldFrame(Frame);
			const PtTransform& Engine = Frame == EConstraintFrame::Frame1 ? Joint.Frame1World : Joint.Frame2World;
			CHECK((HostFrame.GetTranslation() - ToVector(Engine.Translation)).Size() < 0.1);
			const FQuat EngineRotation(Engine.Rotation.X, Engine.Rotation.Y, Engine.Rotation.Z, Engine.Rotation.W);
			CHECK(QuatAngle(HostFrame.GetRotation(), EngineRotation) < 1e-4);
		}
		const FVector Linear = ToVector(Joint.LinearImpulse);
		const FVector Angular = ToVector(Joint.AngularImpulse);
		MESSAGE(Host.JointName << ": linear impulse (" << Linear.X << ", " << Linear.Y << ", " << Linear.Z << ") kg cm/s, angular (" << Angular.X << ", "
			<< Angular.Y << ", " << Angular.Z << ") kg cm^2/s; m g dt = " << Weight << ", L m g dt = " << L * Weight);
		CHECK(Linear.Z == doctest::Approx(Weight).epsilon(0.01));
		CHECK(std::abs(Linear.X) < 0.01 * Weight);
		CHECK(std::abs(Linear.Y) < 0.01 * Weight);
		if (Index == 0)
		{
			CHECK(Angular.Y == doctest::Approx(-L * Weight).epsilon(0.01));
			CHECK(std::abs(Angular.X) < 0.01 * L * Weight);
			CHECK(std::abs(Angular.Z) < 0.01 * L * Weight);
		}
		else
		{
			CHECK(Angular.X == 0.0);
			CHECK(Angular.Y == 0.0);
			CHECK(Angular.Z == 0.0);
		}
	}
	// Both stay where they were put (the hinge does not let the beam pitch, the ball holds the bob).
	CHECK((Beam->State.BodyTM.GetTranslation() - FVector(L, 0.0, 500.0)).Size() < 0.05);
	CHECK((Hanging->State.BodyTM.GetTranslation() - FVector(300.0, 0.0, 500.0 - L)).Size() < 0.05);
}

// UEParity: a joint whose dynamic bodies are asleep is reported with PT_JOINTSNAP_SLEEPING and zero impulses (docs/Abi.md 9.4
// item 9); a constraint wakes nothing by itself, and the bob at rest falls asleep with the mapped UE thresholds.
TEST_CASE("PhysicsTest.JoltJoint.SleepingJoint")
{
	if (!HasJolt())
	{
		return;
	}
	const FResolvedPhysicsConfig Config = LoadJoltConfig("UEParity");
	REQUIRE(Config.TestSettings.bAllowSleeping);
	std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltJointSleeping", Config);
	const FBodySetup* Sphere = MakeSphereSetup(*Scene, 5.0);
	FBodyInstance* Bob = Scene->SpawnBody(MakeTemplate(Config, true), Sphere, FTransform(FVector(0.0, 0.0, 450.0)));
	const FVector X(1.0, 0.0, 0.0);
	const FVector Y(0.0, 1.0, 0.0);
	Scene->SpawnConstraint(MakeJoint("Ball", false, FVector(0.0, 0.0, 50.0), X, Y, FVector(0.0, 0.0, 500.0), X, Y, false), Bob, nullptr);
	Scene->FlushDeferredActors();
	Scene->PrepareForSimulation();
	int32 SleptAt = -1;
	for (int32 Frame = 0; Frame < 120 && SleptAt < 0; ++Frame)
	{
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
		SleptAt = Bob->State.ObjectState == EObjectStateType::Sleeping ? Frame : -1;
	}
	MESSAGE("UEParity: the bob hanging at rest on a ball joint sleeps at frame " << SleptAt);
	REQUIRE(SleptAt >= 0);
	CHECK(SleptAt < 30);   // Jolt's own 0.5 s would need 30 frames
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 1u);
	CHECK(Snapshot.Joints[0].Flags == PT_JOINTSNAP_SLEEPING);
	CHECK(ToVector(Snapshot.Joints[0].LinearImpulse).Size() == 0.0);
	CHECK(ToVector(Snapshot.Joints[0].AngularImpulse).Size() == 0.0);
}

// PT_JOINTF_DISABLE_COLLISION (docs/Abi.md 9.4 item 5): two overlapping spheres joined at their midpoint, gravity off, plus a third
// sphere overlapping the second. With the flag the jointed pair has no contact while the unjointed pair keeps colliding; without
// it both pairs collide. Releasing a body removes its joint and the pair's filter entry.
TEST_CASE("PhysicsTest.JoltJoint.DisableCollision")
{
	if (!HasJolt())
	{
		return;
	}
	const FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized");
	for (const bool bDisable : { true, false })
	{
		CAPTURE(bDisable);
		std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltJointDisableCollision", Config, PT_INSTR_CONTACTS);
		const FBodySetup* Sphere = MakeSphereSetup(*Scene, 10.0);
		FBodyInstance Template = MakeTemplate(Config, true);
		Template.bEnableGravity = false;
		FBodyInstance* A = Scene->SpawnBody(Template, Sphere, FTransform(FVector(0.0, 0.0, 100.0)));
		FBodyInstance* B = Scene->SpawnBody(Template, Sphere, FTransform(FVector(15.0, 0.0, 100.0)));
		FBodyInstance* C = Scene->SpawnBody(Template, Sphere, FTransform(FVector(30.0, 0.0, 100.0)));
		const FVector X(1.0, 0.0, 0.0);
		const FVector Y(0.0, 1.0, 0.0);
		FConstraintInstance* Joint = Scene->SpawnConstraint(MakeJoint("Pair", false, FVector(-7.5, 0.0, 0.0), X, Y, FVector(7.5, 0.0, 0.0), X, Y, bDisable), B, A);
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		REQUIRE(Joint->IsSimulated());
		Scene->Tick(Config.TestSettings.FixedDeltaTime);
		const auto CountPairContacts = [&](const FBodyInstance* First, const FBodyInstance* Second)
		{
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_CONTACTS));
			int32 Count = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				const uint32 I0 = First->ActorHandle.Index;
				const uint32 I1 = Second->ActorHandle.Index;
				Count += (Contact.Body0 == I0 && Contact.Body1 == I1) || (Contact.Body0 == I1 && Contact.Body1 == I0) ? 1 : 0;
			}
			return Count;
		};
		const int32 JointedPair = CountPairContacts(A, B);
		const int32 FreePair = CountPairContacts(B, C);
		MESSAGE(std::string(bDisable ? "bDisableCollision" : "collision on") << ": contacts A-B " << JointedPair << ", B-C " << FreePair);
		CHECK(FreePair > 0);
		CHECK((JointedPair == 0) == bDisable);
		const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
		CHECK(FindSetting(Settings, "jolt.joints").Native.find(bDisable ? "1 disable-collision" : "0 disable-collision") != std::string::npos);
		if (bDisable)
		{
			// Releasing A releases the joint (the index stays taken, the snapshot drops it) and the pair's filter entry.
			Scene->ReleaseBody(A);
			CHECK_FALSE(Joint->IsSimulated());
			Scene->Tick(Config.TestSettings.FixedDeltaTime);
			FPhysicsDebugSnapshot Snapshot;
			REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
			CHECK(Snapshot.Joints.empty());
			const std::vector<FEffectiveSetting> After = ReadEffective(*Scene);
			CHECK(FindSetting(After, "jolt.joints").Native.find("0 live joints (1 released); 0 disable-collision") != std::string::npos);
			CHECK(CountPairContacts(B, C) > 0);
		}
	}
}

// Joint indices across CreateConstraints calls (creation order, never reused), a joint whose child is kinematic (accepted, no
// effect, no Jolt assert), and Body2 = a body: releasing the parent releases the joint and the child falls freely.
TEST_CASE("PhysicsTest.JoltJoint.IndicesAndRelease")
{
	if (!HasJolt())
	{
		return;
	}
	const FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized");
	std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltJointIndices", Config);
	const FBodySetup* Sphere = MakeSphereSetup(*Scene, 5.0);
	const FVector X(1.0, 0.0, 0.0);
	const FVector Y(0.0, 1.0, 0.0);
	FBodyInstance KinematicTemplate = MakeTemplate(Config, false);
	FBodyInstance* Kinematic = Scene->SpawnBody(KinematicTemplate, Sphere, FTransform(FVector(-200.0, 0.0, 500.0)));
	FBodyInstance* Parent = Scene->SpawnBody(MakeTemplate(Config, true), Sphere, FTransform(FVector(0.0, 0.0, 500.0)));
	FConstraintInstance* First = Scene->SpawnConstraint(MakeJoint("KinematicToWorld", true, FVector(0.0, 0.0, 10.0), X, Y, FVector(-200.0, 0.0, 510.0), X, Y, false),
		Kinematic, nullptr);
	FConstraintInstance* Second = Scene->SpawnConstraint(MakeJoint("ParentToWorld", false, FVector(0.0, 0.0, 10.0), X, Y, FVector(0.0, 0.0, 510.0), X, Y, false),
		Parent, nullptr);
	Scene->FlushDeferredActors();
	FBodyInstance* Child = Scene->SpawnBody(MakeTemplate(Config, true), Sphere, FTransform(FVector(0.0, 0.0, 480.0)));
	FConstraintInstance* Third = Scene->SpawnConstraint(MakeJoint("ChildToParent", false, FVector(0.0, 0.0, 10.0), X, Y, FVector(0.0, 0.0, -10.0), X, Y, true),
		Child, Parent);
	Scene->FlushDeferredActors();
	Scene->PrepareForSimulation();
	REQUIRE(First->IsSimulated());
	REQUIRE(Third->IsSimulated());
	CHECK(First->ConstraintHandle.Index == 0u);
	CHECK(Second->ConstraintHandle.Index == 1u);
	CHECK(Third->ConstraintHandle.Index == 2u);   // the second CreateConstraints call continues the order
	uint32 ErrorBits = TickFrames(*Scene, Config.TestSettings.FixedDeltaTime, 10);
	CHECK((Child->State.BodyTM.GetTranslation() - FVector(0.0, 0.0, 480.0)).Size() < 0.1);   // hangs below the hanging parent
	FPhysicsDebugSnapshot Snapshot;
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 3u);
	CHECK(Snapshot.Joints[0].Flags == PT_JOINTSNAP_SLEEPING);   // no dynamic body: never runs, 0 impulses (abi-v1.1: SLEEPING)
	CHECK(ToVector(Snapshot.Joints[0].LinearImpulse).Size() == 0.0);
	// The child's joint carries the child's weight, the parent's the weight of both.
	const double Weight = std::abs(Config.Settings.DefaultGravityZ) * Analytic::EngineDeltaTime(Config.TestSettings.FixedDeltaTime);
	CHECK(Snapshot.Joints[1].LinearImpulse.Z == doctest::Approx(2.0 * Weight).epsilon(0.02));
	CHECK(Snapshot.Joints[2].LinearImpulse.Z == doctest::Approx(Weight).epsilon(0.02));
	Scene->ReleaseBody(Parent);
	CHECK_FALSE(Second->IsSimulated());
	CHECK_FALSE(Third->IsSimulated());
	CHECK(First->IsSimulated());
	const double ZBefore = Child->State.BodyTM.GetTranslation().Z;
	ErrorBits |= TickFrames(*Scene, Config.TestSettings.FixedDeltaTime, 30);
	CHECK(Child->State.BodyTM.GetTranslation().Z < ZBefore - 100.0);   // free fall: about 122 cm in 0.5 s
	REQUIRE(Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS));
	REQUIRE(Snapshot.Joints.size() == 1u);
	CHECK(Snapshot.Joints[0].JointIndex == 0u);
	CHECK(ErrorBits == 0u);   // no Jolt assert (dbg) or capacity error
}

// The UEParity sleep threshold mapping (02 10, approximation flagged): the readback and the flag key in both profiles, Native
// for comparison, and the behaviour - a box at rest on the ground sleeps after UE's SleepCounterThreshold + 1 = 5 calm frames
// with the mapping and after Jolt's own 0.5 s (30 frames) without it.
TEST_CASE("PhysicsTest.JoltJoint.SleepThresholdMapping")
{
	if (!HasJolt())
	{
		return;
	}
	for (const std::string Profile : { "UEParity", "Normalized" })
	{
		for (const bool bNative : { false, true })
		{
			CAPTURE(Profile);
			CAPTURE(bNative);
			FResolvedPhysicsConfig Config = LoadJoltConfig(Profile);
			if (bNative)
			{
				SetSceneOption(Config, "SleepThresholdMapping", "Native");
			}
			std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltSleepMapping", Config);
			FBodyInstance GroundTemplate = MakeTemplate(Config, false);
			GroundTemplate.bStatic = true;
			Scene->SpawnBody(GroundTemplate, MakeBoxSetup(*Scene, 2000.0, 2000.0, 100.0), FTransform(FVector(0.0, 0.0, -50.0)));
			FBodyInstance* Box = Scene->SpawnBody(MakeTemplate(Config, true, 10.0f), MakeBoxSetup(*Scene, 50.0, 50.0, 50.0), FTransform(FVector(0.0, 0.0, 25.0)));
			Scene->FlushDeferredActors();
			Scene->PrepareForSimulation();
			const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
			const FEffectiveSetting& Criteria = FindSetting(Settings, "sleep.criteria");
			const FEffectiveSetting& Threshold = FindSetting(Settings, "jolt.sleep_threshold");
			const FEffectiveSetting& Mapping = FindSetting(Settings, "jolt.sleep_mapping");
			MESSAGE((Profile + (bNative ? " Native" : " UE")) << ": sleep.criteria " << Criteria.Canonical << " (" << Criteria.Native << "); jolt.sleep_threshold "
				<< Threshold.Canonical << " cm/s; jolt.sleep_mapping " << Mapping.Canonical << " (" << Mapping.Native << ")");
			CHECK(Criteria.Source == "READBACK");
			CHECK(Threshold.Source == "READBACK");
			CHECK(Mapping.Source == "SET_ONLY");
			REQUIRE(Criteria.bHasCanonical);
			REQUIRE(Threshold.bHasCanonical);
			// M9 fix round 0: sleep.criteria is the rule key (the settings not holding the variant's rule: 0); the value is
			// jolt.sleep_threshold - UE default material 1 cm/s x multiplier 1 / 2 (both profiles: the mapping does not depend on the
			// sleep axis), Jolt's own 3 cm/s with Native.
			CHECK(Criteria.Canonical == 0.0);
			CHECK(Threshold.Canonical == doctest::Approx(bNative ? 3.0 : 0.5).epsilon(1e-6));
			CHECK(Mapping.Canonical == (bNative ? 0.0 : 1.0));
			{
				// The rule key is scene independent (G9 const:0): a body of UE's Sensitive sleep family (multiplier 1/20) lowers the
				// mapped threshold to 0.025 cm/s and the key stays 0 (the M9 const:0.5 expectation failed such a scene).
				std::unique_ptr<FPhysScene> Sensitive = MakeJoltScene("JoltSleepMappingSensitive", Config);
				FBodyInstance SensitiveTemplate = MakeTemplate(Config, true, 10.0f);
				SensitiveTemplate.SleepFamily = ESleepFamily::Sensitive;
				Sensitive->SpawnBody(SensitiveTemplate, MakeBoxSetup(*Sensitive, 50.0, 50.0, 50.0), FTransform(FVector(0.0, 0.0, 25.0)));
				Sensitive->FlushDeferredActors();
				Sensitive->PrepareForSimulation();
				const std::vector<FEffectiveSetting> SensitiveSettings = ReadEffective(*Sensitive);
				CHECK(FindSetting(SensitiveSettings, "sleep.criteria").Canonical == 0.0);
				CHECK(FindSetting(SensitiveSettings, "jolt.sleep_threshold").Canonical == doctest::Approx(bNative ? 3.0 : 0.025).epsilon(1e-5));
			}
			if (Profile != "UEParity")
			{
				continue;
			}
			int32 SleptAt = -1;
			for (int32 Frame = 0; Frame < 90 && SleptAt < 0; ++Frame)
			{
				Scene->Tick(Config.TestSettings.FixedDeltaTime);
				SleptAt = Box->State.ObjectState == EObjectStateType::Sleeping ? Frame : -1;
			}
			MESSAGE((std::string("UEParity") + (bNative ? " Native" : " UE")) << ": a box at rest sleeps at frame " << SleptAt);
			REQUIRE(SleptAt >= 0);
			if (bNative)
			{
				CHECK(SleptAt >= 28);   // 0.5 s = 30 frames of calm (after the first contact frames)
			}
			else
			{
				CHECK(SleptAt <= 8);    // 5 calm frames (UE SleepCounterThreshold 4 + 1)
			}
		}
	}
}

// Jolt.SharpBoxes (03 M9 profile completion): every box of the scene is built with convex radius 0 (shape.box_margin reads it
// back from the shapes), the rest of the variant is Jolt.Default's, and a dropped cube comes to rest face down on the ground.
TEST_CASE("PhysicsTest.JoltJoint.SharpBoxesVariant")
{
	if (!HasJolt())
	{
		return;
	}
	for (const std::string Variant : { "Default", "SharpBoxes" })
	{
		CAPTURE(Variant);
		const FResolvedPhysicsConfig Config = LoadJoltConfig("Normalized", Variant);
		CHECK(Config.VariantName == Variant);
		std::unique_ptr<FPhysScene> Scene = MakeJoltScene("JoltSharpBoxes", Config);
		FBodyInstance GroundTemplate = MakeTemplate(Config, false);
		GroundTemplate.bStatic = true;
		Scene->SpawnBody(GroundTemplate, MakeBoxSetup(*Scene, 2000.0, 2000.0, 100.0), FTransform(FVector(0.0, 0.0, -50.0)));
		FBodyInstance* Cube = Scene->SpawnBody(MakeTemplate(Config, true, 10.0f), MakeBoxSetup(*Scene, 50.0, 50.0, 50.0), FTransform(FVector(0.0, 0.0, 100.0)));
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		const std::vector<FEffectiveSetting> Settings = ReadEffective(*Scene);
		const double ExpectedRadius = Variant == "SharpBoxes" ? 0.0 : 5.0;
		CHECK(FindSetting(Settings, "shape.box_margin").Canonical == doctest::Approx(ExpectedRadius));
		CHECK(FindSetting(Settings, "shape.convex_radius").Canonical == doctest::Approx(ExpectedRadius));
		const uint32 ErrorBits = TickFrames(*Scene, Config.TestSettings.FixedDeltaTime, 120);
		const FVector Rest = Cube->State.BodyTM.GetTranslation();
		MESSAGE(Variant << ": cube at rest, centre z " << Rest.Z << " cm (25 = touching)");
		CHECK(Rest.Z > 25.0 - 2.05);   // within Jolt's penetration slop (2 cm)
		CHECK(Rest.Z < 25.0 + 0.05);
		CHECK(ErrorBits == 0u);
	}
}
