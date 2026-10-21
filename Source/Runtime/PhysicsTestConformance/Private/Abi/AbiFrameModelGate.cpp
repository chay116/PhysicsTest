// AbiFrameModelGate.cpp - A10, the frame-model gate (see Abi/AbiGates.h, 02_Architecture 9.1 / 14.2, DL08, DL29).
// The harness FStepPlanner and the backend's own planner (Null: FNullFramePlanner, written independently, R4) must agree
// on the engine step count of every frame, the Native and Harness frame drivers must produce bit-identical results
// where UE's arguments make them equivalent (FixedStep, Sync - frames that run no step included), and in the multi-step
// modes (UE5 substepping, AsyncFixed) the two drivers must follow the same UE5 frame rules for forces, impulses, sleep /
// wake and kinematic targets: their results differ only by the step dt (D / N in double vs its float), the torque's
// world inertia per step and rounding.
#include "Abi/AbiGates.h"
#include "AbiGateHelpers.h"

#include "Physics/PhysicsInterface.h"
#include "Physics/PhysicsProfile.h"
#include "Physics/StepPlanner.h"
#include "PhysScene.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <tuple>
#include <utility>
#include <vector>

namespace PhysicsTest::AbiGates
{
	using namespace AbiGateHelpers;

	namespace
	{
		struct FPlanCase
		{
			const char* Name;
			EPhysicsTickMode Mode;
			float MinPhysicsDeltaTime;   // < 0: the project default
			std::vector<float> FrameDeltaTimes;
		};

		float RuntimeFloat(const float Value)
		{
			volatile float Stored = Value;
			return Stored;
		}

		// The free-fall sphere, a spinning damped box with a COM nudge, a kinematic box and two spheres that start asleep.
		struct FDriverScene
		{
			FBodyInstance* Ground = nullptr;
			FBodyInstance* Sphere = nullptr;
			FBodyInstance* Box = nullptr;
			FBodyInstance* Kinematic = nullptr;
			FBodyInstance* Sleeper = nullptr;
			FBodyInstance* Dozer = nullptr;
		};

		FDriverScene BuildDriverScene(FPhysScene& Scene)
		{
			FDriverScene Out;
			FBodySetup GroundSetup;
			GroundSetup.AggGeom.BoxElems.emplace_back(2000.0, 2000.0, 20.0);
			FBodySetup SphereSetup;
			SphereSetup.AggGeom.SphereElems.emplace_back(50.0);
			FBodySetup BoxSetup;
			BoxSetup.AggGeom.BoxElems.emplace_back(100.0, 50.0, 20.0);
			const FBodySetup* GroundPtr = Scene.CreateBodySetup(std::move(GroundSetup));
			const FBodySetup* SpherePtr = Scene.CreateBodySetup(std::move(SphereSetup));
			const FBodySetup* BoxPtr = Scene.CreateBodySetup(std::move(BoxSetup));

			FBodyInstance Static;
			Static.bStatic = true;
			Out.Ground = Scene.SpawnBody(Static, GroundPtr, FTransform(FVector(0.0, 0.0, -10.0)));
			FBodyInstance Falling;
			Falling.bSimulatePhysics = true;
			Falling.bOverrideMass = true;
			Falling.MassInKgOverride = 1.0f;
			Falling.LinearDamping = 0.0f;
			Out.Sphere = Scene.SpawnBody(Falling, SpherePtr, FTransform(FVector(0.0, 0.0, 1000.0)));
			FBodyInstance Spinning;
			Spinning.bSimulatePhysics = true;
			Spinning.LinearDamping = 0.1f;
			Spinning.AngularDamping = 0.05f;
			Spinning.COMNudge = FVector(0.0, 0.0, 5.0);
			Spinning.InitialLinearVelocity = FVector(100.0, 0.0, 50.0);
			Spinning.InitialAngularVelocityInRadians = FVector(0.5, 1.0, 2.0);
			Out.Box = Scene.SpawnBody(Spinning, BoxPtr, FTransform(FRotator(10.0, 20.0, 30.0), FVector(300.0, -200.0, 600.0)));
			FBodyInstance Moving;   // kinematic: neither static nor simulated
			Out.Kinematic = Scene.SpawnBody(Moving, BoxPtr, FTransform(FVector(-300.0, 0.0, 100.0)));
			FBodyInstance Asleep;
			Asleep.bSimulatePhysics = true;
			Asleep.bStartAwake = false;
			Out.Sleeper = Scene.SpawnBody(Asleep, SpherePtr, FTransform(FVector(0.0, 300.0, 200.0)));
			Out.Dozer = Scene.SpawnBody(Asleep, SpherePtr, FTransform(FVector(0.0, -300.0, 200.0)));
			Scene.FlushDeferredActors();
			Scene.PrepareForSimulation();
			return Out;
		}

		struct FDriverRun
		{
			std::vector<FBodyInstanceState> States;
			uint64 Hash = 0;
			double SimTime = 0.0;
			uint64 Steps = 0;
			EFrameDriver Driver = EFrameDriver::Auto;
		};

		void Capture(FPhysScene& Scene, FDriverRun& Run)
		{
			for (const FBodyInstance* Body : Scene.GetBodies())
			{
				Run.States.push_back(Body->State);
			}
			Run.Hash = Scene.ComputeNativeStateHash();
			Run.SimTime = Scene.GetSimTime();
			Run.Steps = Scene.GetStepCount();
			Run.Driver = Scene.GetFrameDriver();
		}

		FPhysSceneInitParams MakeSceneParams(const std::string& BackendName, const FResolvedPhysicsConfig& Config)
		{
			FPhysSceneInitParams Params;
			Params.DebugName = "A10";
			Params.BackendName = BackendName;
			Params.Config = &Config;
			return Params;
		}

		// Commands issued from OnPhysScenePreTick (and once from OnPhysSceneStep) at fixed frames, through every host
		// path (FBodyInstance, FPhysScene, FPhysicsInterface). With the zero-step Sync list (A10 (a)'s Sync frames, period
		// 7: 0.05, 1/60, 0.01, 0.2, 0.0334, 1e-9, 0) frames 12 and 13 run no step: an AddForce on a sleeping body there
		// must wake it, and a velocity set there must be what frame 14's bAddToCurrent adds to (UE's game thread).
		FDriverRun RunDriver(const std::string& BackendName, const EPhysicsTickMode Mode, const EFrameDriver Driver, const std::vector<float>& FrameDts)
		{
			FResolvedPhysicsConfig Config;
			Config.TestSettings.TickMode = Mode;
			Config.TestSettings.FrameDriver = Driver;
			FDriverRun Run;
			{
				FPhysScene Scene(MakeSceneParams(BackendName, Config));
				const FDriverScene Bodies = BuildDriverScene(Scene);
				int32 Frame = 0;
				Scene.OnPhysScenePreTick = [&Frame, &Bodies](FPhysScene* InScene, float)
				{
					if (Frame == 5)
					{
						Bodies.Box->AddImpulse(FVector(0.0, 0.0, 1000.0), false);
						Bodies.Sphere->AddAngularImpulseInRadians(FVector(0.0, 50.0, 0.0), true);
					}
					if (Frame == 10)
					{
						Bodies.Sphere->SetLinearVelocity(FVector(10.0, 0.0, 0.0), true);
					}
					if (Frame == 12)
					{
						Bodies.Dozer->AddForce(FVector(0.0, 0.0, 5000.0));   // asleep: the game-thread AddForce wakes it
					}
					if (Frame == 13)
					{
						Bodies.Sphere->SetLinearVelocity(FVector(100.0, 0.0, 0.0), false);
					}
					if (Frame == 14)
					{
						Bodies.Sphere->SetLinearVelocity(FVector(0.0, 0.0, 0.0), true);   // adds the velocity set at frame 13
					}
					if (Frame >= 20 && Frame < 30)
					{
						// bAllowSubstepping varies: UE5 (and both drivers' UE5 rule) ignores it.
						Bodies.Box->AddForce(FVector(0.0, 500.0, 2000.0), Frame % 3 != 0);
						Bodies.Box->AddTorqueInRadians(FVector(0.0, 0.0, 300.0), Frame % 4 != 0, Frame % 2 == 0);
					}
					if (Frame == 26)
					{
						Bodies.Box->PutInstanceToSleep();   // after this frame's force and torque: they are dropped, the box sleeps
					}
					if (Frame >= 30 && Frame < 36)
					{
						// The FPhysicsInterface entry point shares FBodyInstance::AddForce's per-frame rule.
						FPhysicsInterface::AddForce_AssumesLocked(Bodies.Sphere->ActorHandle, FVector(200.0, 0.0, 0.0), false, true);
					}
					if (Frame >= 15 && Frame < 25)
					{
						InScene->SetKinematicTarget_AssumesLocked(Bodies.Kinematic,
							FTransform(FRotator(0.0, 3.0 * (Frame - 14), 0.0), FVector(-300.0 + 4.0 * (Frame - 14), 0.0, 100.0)), true);
					}
					if (Frame == 40)
					{
						Bodies.Sleeper->WakeInstance();
					}
					if (Frame == 45)
					{
						Bodies.Dozer->PutInstanceToSleep();
						Bodies.Dozer->AddTorqueInRadians(FVector(0.0, 0.0, 0.0));   // after the sleep: wakes it again
					}
					if (Frame >= 50 && Frame < 53)
					{
						// Kinematic targets through FBodyInstance::SetBodyTransform (the scene verb) and FPhysicsInterface.
						const FTransform Goal(FRotator(0.0, 10.0 * (Frame - 49), 0.0), FVector(-250.0 + 10.0 * (Frame - 49), 20.0, 100.0));
						if (Frame == 51)
						{
							FPhysicsInterface::SetKinematicTarget_AssumesLocked(Bodies.Kinematic->ActorHandle, Goal);
						}
						else
						{
							Bodies.Kinematic->SetBodyTransform(Goal, ETeleportType::None);
						}
					}
					if (Frame == 55)
					{
						// A teleport, then a target in the same frame: the target starts from the teleported pose.
						Bodies.Kinematic->SetBodyTransform(FTransform(FVector(-400.0, 0.0, 100.0)), ETeleportType::TeleportPhysics);
						Bodies.Kinematic->SetBodyTransform(FTransform(FRotator(0.0, 30.0, 0.0), FVector(-380.0, 10.0, 100.0)), ETeleportType::None);
					}
					if (Frame == 60)
					{
						Bodies.Box->SetBodyTransform(FTransform(FRotator(0.0, 45.0, 0.0), FVector(0.0, 0.0, 800.0)), ETeleportType::TeleportPhysics);
					}
				};
				// UE5 broadcasts OnPhysSceneStep once per frame: a force added there acts like one added in PreTick.
				Scene.OnPhysSceneStep = [&Frame, &Bodies](FPhysScene*, float)
				{
					if (Frame >= 64 && Frame < 70)
					{
						Bodies.Sleeper->AddForce(FVector(30.0, 0.0, 0.0));
					}
				};
				for (const float Dt : FrameDts)
				{
					Scene.Tick(Dt);
					++Frame;
				}
				Capture(Scene, Run);
			}
			return Run;
		}

		// SplitMix64 (Steele, Lea, Flood 2014): a small deterministic generator for the random command scripts.
		struct FScriptRandom
		{
			uint64 State = 0;
			explicit FScriptRandom(const uint64 Seed) : State(Seed) {}
			uint64 Next()
			{
				uint64 Z = (State += 0x9E3779B97F4A7C15ull);
				Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
				Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
				return Z ^ (Z >> 31);
			}
			uint32 Below(const uint32 Count) { return static_cast<uint32>(Next() % Count); }
			bool Coin() { return Below(2) != 0; }
			double Signed() { return static_cast<double>(Next() >> 11) * (2.0 / 9007199254740992.0) - 1.0; }   // [-1, 1)
			// Three draws in a fixed order (function arguments would be evaluated in an unspecified order).
			FVector Vector(const double Scale)
			{
				const double X = Signed();
				const double Y = Signed();
				const double Z = Signed();
				return FVector(X, Y, Z) * Scale;
			}
			FRotator Rotator(const double Degrees)
			{
				const FVector Angles = Vector(Degrees);
				return FRotator(Angles.X, Angles.Y, Angles.Z);
			}
		};

		// Random frame dts with zero-step frames: 0, 1e-9 and short frames among the usual ones.
		std::vector<float> MakeRandomDts(const uint64 Seed, const int32 NumFrames)
		{
			FScriptRandom Random(Seed * 7919u + 13u);
			const float Pattern[] = { RuntimeFloat(1.0f / 60.0f), RuntimeFloat(0.05f), RuntimeFloat(0.01f), RuntimeFloat(1.0f / 30.0f), 0.0f,
				RuntimeFloat(1e-9f), RuntimeFloat(0.02f), RuntimeFloat(0.0125f), RuntimeFloat(0.004f) };
			std::vector<float> Dts;
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				Dts.push_back(Random.Below(4) == 0 ? RuntimeFloat(static_cast<float>(Random.Below(100000)) * 1.0e-6f) : Pattern[Random.Below(9)]);
			}
			return Dts;
		}

		// Eight dynamic bodies (spheres and boxes, damping, COM nudges, initial velocities, one without gravity, one asleep)
		// and two kinematic boxes; every frame 0-3 random commands through every host path, including sleep / wake,
		// bAddToCurrent velocity sets, teleports and kinematic targets (scene verb, FBodyInstance, FPhysicsInterface).
		// bNoTorques draws the same script but skips its torques.
		FDriverRun RunRandomScript(const std::string& BackendName, const EPhysicsTickMode Mode, const EFrameDriver Driver, const uint64 Seed,
			const std::vector<float>& FrameDts, const bool bNoTorques = false)
		{
			FResolvedPhysicsConfig Config;
			Config.TestSettings.TickMode = Mode;
			Config.TestSettings.FrameDriver = Driver;
			FDriverRun Run;
			{
				FPhysScene Scene(MakeSceneParams(BackendName, Config));
				FBodySetup SphereSetup;
				SphereSetup.AggGeom.SphereElems.emplace_back(30.0);
				FBodySetup BoxSetup;
				BoxSetup.AggGeom.BoxElems.emplace_back(80.0, 40.0, 25.0);
				const FBodySetup* Sphere = Scene.CreateBodySetup(std::move(SphereSetup));
				const FBodySetup* Box = Scene.CreateBodySetup(std::move(BoxSetup));
				FScriptRandom Random(Seed);
				std::vector<FBodyInstance*> Dynamic;
				std::vector<FBodyInstance*> Kinematic;
				for (int32 Index = 0; Index < 8; ++Index)
				{
					FBodyInstance Template;
					Template.bSimulatePhysics = true;
					Template.LinearDamping = static_cast<float>(0.2 * (Index % 3));
					Template.AngularDamping = static_cast<float>(0.1 * (Index % 4));
					Template.bEnableGravity = Index != 5;
					Template.bStartAwake = Index != 6;
					Template.COMNudge = Random.Vector(5.0);
					Template.InitialLinearVelocity = Random.Vector(200.0);
					Template.InitialAngularVelocityInRadians = Random.Vector(3.0);
					const FRotator Rotation = Random.Rotator(90.0);
					const FVector Offset = Random.Vector(100.0);
					const FVector Location(150.0 * Index, Offset.Y, 500.0 + Offset.Z);
					Dynamic.push_back(Scene.SpawnBody(Template, Index % 2 != 0 ? Box : Sphere, FTransform(Rotation, Location)));
				}
				for (int32 Index = 0; Index < 2; ++Index)
				{
					Kinematic.push_back(Scene.SpawnBody(FBodyInstance(), Box, FTransform(FVector(-500.0 * (Index + 1), 0.0, 100.0))));
				}
				Scene.FlushDeferredActors();
				Scene.PrepareForSimulation();
				FScriptRandom Commands(Seed * 31u + 7u);
				Scene.OnPhysScenePreTick = [&Commands, &Dynamic, &Kinematic, bNoTorques](FPhysScene* InScene, float)
				{
					const uint32 NumOps = Commands.Below(4);
					for (uint32 Op = 0; Op < NumOps; ++Op)
					{
						FBodyInstance* Body = Dynamic[Commands.Below(static_cast<uint32>(Dynamic.size()))];
						const FVector Value = Commands.Vector(1000.0);
						const uint32 Kind = Commands.Below(11);
						const bool bFlagA = Commands.Coin();
						const bool bFlagB = Commands.Coin();
						if (bNoTorques && (Kind == 1 || Kind == 10))
						{
							continue;
						}
						switch (Kind)
						{
						case 0: Body->AddForce(Value, bFlagA, bFlagB); break;
						case 1: Body->AddTorqueInRadians(Value * 10.0, bFlagA, bFlagB); break;
						case 2: Body->AddImpulse(Value, bFlagA); break;
						case 3: Body->AddAngularImpulseInRadians(Value, bFlagA); break;
						case 4: Body->SetLinearVelocity(Value * 0.1, bFlagA); break;
						case 5: Body->SetAngularVelocityInRadians(Value * 0.001, bFlagA); break;
						case 6: Body->PutInstanceToSleep(); break;
						case 7: Body->WakeInstance(); break;
						case 8: Body->SetBodyTransform(FTransform(Commands.Rotator(180.0), Value), ETeleportType::TeleportPhysics); break;
						case 9: FPhysicsInterface::AddForce_AssumesLocked(Body->ActorHandle, Value, false, bFlagA); break;
						default: FPhysicsInterface::AddTorque_AssumesLocked(Body->ActorHandle, Value * 10.0, true, false); break;
						}
					}
					if (Commands.Below(3) == 0)
					{
						FBodyInstance* Body = Kinematic[Commands.Below(static_cast<uint32>(Kinematic.size()))];
						const FRotator Rotation = Commands.Rotator(30.0);
						const FVector Offset = Commands.Vector(50.0);
						const FTransform Goal(Rotation, FVector(-500.0, 0.0, 100.0) + Offset);
						switch (Commands.Below(4))
						{
						case 0: InScene->SetKinematicTarget_AssumesLocked(Body, Goal, true); break;
						case 1: Body->SetBodyTransform(Goal, ETeleportType::None); break;
						case 2: FPhysicsInterface::SetKinematicTarget_AssumesLocked(Body->ActorHandle, Goal); break;
						default: Body->SetBodyTransform(Goal, ETeleportType::TeleportPhysics); break;
						}
					}
				};
				for (const float Dt : FrameDts)
				{
					Scene.Tick(Dt);
				}
				Capture(Scene, Run);
			}
			return Run;
		}

		bool BitEqual(const double A, const double B)
		{
			return std::memcmp(&A, &B, sizeof(double)) == 0;
		}

		// The largest difference of two runs' body states: position (cm), rotation (quaternion components), linear (cm/s)
		// and angular (rad/s) velocity, plus whether every object state agrees.
		struct FStateDifference
		{
			double Position = 0.0;
			double Rotation = 0.0;
			double LinearVelocity = 0.0;
			double AngularVelocity = 0.0;
			bool bSameObjectStates = true;
		};

		FStateDifference CompareStates(const std::vector<FBodyInstanceState>& A, const std::vector<FBodyInstanceState>& B)
		{
			FStateDifference Out;
			Out.bSameObjectStates = A.size() == B.size();
			const auto Max = [](double& Into, const double Value) { Into = Value > Into ? Value : Into; };
			for (size_t Index = 0; Out.bSameObjectStates && Index < A.size(); ++Index)
			{
				const FVector DP = A[Index].BodyTM.GetTranslation() - B[Index].BodyTM.GetTranslation();
				const FQuat QA = A[Index].BodyTM.GetRotation();
				FQuat QB = B[Index].BodyTM.GetRotation();
				if ((QA | QB) < 0.0)
				{
					QB = FQuat(-QB.X, -QB.Y, -QB.Z, -QB.W);
				}
				const FVector DV = A[Index].LinearVelocity - B[Index].LinearVelocity;
				const FVector DW = A[Index].AngularVelocityInRadians - B[Index].AngularVelocityInRadians;
				Max(Out.Position, std::fabs(DP.X) > std::fabs(DP.Y) ? (std::fabs(DP.X) > std::fabs(DP.Z) ? std::fabs(DP.X) : std::fabs(DP.Z))
					: (std::fabs(DP.Y) > std::fabs(DP.Z) ? std::fabs(DP.Y) : std::fabs(DP.Z)));
				Max(Out.Rotation, std::fabs(QA.X - QB.X));
				Max(Out.Rotation, std::fabs(QA.Y - QB.Y));
				Max(Out.Rotation, std::fabs(QA.Z - QB.Z));
				Max(Out.Rotation, std::fabs(QA.W - QB.W));
				Max(Out.LinearVelocity, DV.Size());
				Max(Out.AngularVelocity, DW.Size());
				Out.bSameObjectStates = A[Index].ObjectState == B[Index].ObjectState;
			}
			return Out;
		}

		bool SameState(const FBodyInstanceState& A, const FBodyInstanceState& B);

		bool SameRun(const FDriverRun& A, const FDriverRun& B)
		{
			bool bSame = A.States.size() == B.States.size() && A.Steps == B.Steps && BitEqual(A.SimTime, B.SimTime) && A.Hash == B.Hash;
			for (size_t Index = 0; bSame && Index < A.States.size(); ++Index)
			{
				bSame = SameState(A.States[Index], B.States[Index]);
			}
			return bSame;
		}

		bool SameState(const FBodyInstanceState& A, const FBodyInstanceState& B)
		{
			const FQuat& QA = A.BodyTM.GetRotation();
			const FQuat& QB = B.BodyTM.GetRotation();
			const FVector& TA = A.BodyTM.GetTranslation();
			const FVector& TB = B.BodyTM.GetTranslation();
			return BitEqual(QA.X, QB.X) && BitEqual(QA.Y, QB.Y) && BitEqual(QA.Z, QB.Z) && BitEqual(QA.W, QB.W) && BitEqual(TA.X, TB.X) && BitEqual(TA.Y, TB.Y)
				&& BitEqual(TA.Z, TB.Z) && BitEqual(A.LinearVelocity.X, B.LinearVelocity.X) && BitEqual(A.LinearVelocity.Y, B.LinearVelocity.Y)
				&& BitEqual(A.LinearVelocity.Z, B.LinearVelocity.Z) && BitEqual(A.AngularVelocityInRadians.X, B.AngularVelocityInRadians.X)
				&& BitEqual(A.AngularVelocityInRadians.Y, B.AngularVelocityInRadians.Y) && BitEqual(A.AngularVelocityInRadians.Z, B.AngularVelocityInRadians.Z)
				&& A.ObjectState == B.ObjectState;
		}
	}

	FGateReport RunA10FrameModel(const std::string_view BackendName)
	{
		FGateReport R;
		R.Gate = "A10";
		FAbiGateTarget Target;
		std::string Error;
		if (!PrepareAbiGateTarget(BackendName, Target, Error))
		{
			R.Target = std::string(BackendName);
			R.Check(false, "backend available: " + Error);
			return R;
		}
		R.Target = Target.Label;
		if (!Target.Module->GetInfo().Has(PT_CAP_NATIVE_FRAME_MODEL))
		{
			R.Result = EGateResult::Skip;
			R.Note("the backend does not declare PT_CAP_NATIVE_FRAME_MODEL");
			return R;
		}
		const PtBackendApi& Api = *Target.Api;

		// (a) NumStepsTaken == FStepPlan::NumSteps through the raw ABI, frame by frame.
		const float Sixtieth = RuntimeFloat(1.0f / 60.0f);
		const std::vector<FPlanCase> Cases = {
			{ "Sync", EPhysicsTickMode::Sync, -1.0f, { RuntimeFloat(0.05f), Sixtieth, RuntimeFloat(0.01f), RuntimeFloat(0.2f), RuntimeFloat(0.0334f), RuntimeFloat(1e-9f), 0.0f } },
			{ "Substepping (UE5ChaosScene)", EPhysicsTickMode::Substepping, -1.0f,
				{ RuntimeFloat(0.05f), RuntimeFloat(0.2f), Sixtieth, RuntimeFloat(0.016f), RuntimeFloat(0.0334f), RuntimeFloat(0.1f), RuntimeFloat(0.07f), 0.0f } },
			{ "Substepping, MinDeltaTime 20 ms", EPhysicsTickMode::Substepping, RuntimeFloat(0.02f), { RuntimeFloat(0.05f), RuntimeFloat(0.1f), RuntimeFloat(0.03f) } },
			{ "AsyncFixed 1/30", EPhysicsTickMode::AsyncFixed, -1.0f,
				{ Sixtieth, Sixtieth, Sixtieth, Sixtieth, Sixtieth, Sixtieth, RuntimeFloat(0.1f), RuntimeFloat(0.05f), 0.0f, Sixtieth, RuntimeFloat(1.0f / 120.0f),
					RuntimeFloat(1.0f / 120.0f), RuntimeFloat(1.0f / 120.0f), RuntimeFloat(1.0f / 120.0f) } },
			{ "FixedStep", EPhysicsTickMode::FixedStep, -1.0f, { RuntimeFloat(0.1f), RuntimeFloat(0.01f), Sixtieth } },
		};
		{
			const FTerminateOnExit Terminate(Api);
			const PtInitParams Init = MakeInitParams();
			R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys");
			for (const FPlanCase& Case : Cases)
			{
				FPhysicsSettings Settings;
				FPhysicsTestSettings TestSettings;
				TestSettings.TickMode = Case.Mode;
				if (Case.MinPhysicsDeltaTime >= 0.0f)
				{
					Settings.MinPhysicsDeltaTime = Case.MinPhysicsDeltaTime;
				}
				const double AsyncDt = FStepPlanner::GetAsyncDeltaTime(Settings, TestSettings);
				const PtSceneDesc Desc = MakeSceneDesc(PT_SIM_ALLOW_SLEEPING, AsyncDt > 0.0 ? AsyncDt : PT_ASYNC_DT_SYNC);
				PtSceneId Scene = 0;
				if (!R.Check(Api.CreateScene(&Desc, &Scene) == PT_OK && PopulateScene(Api, Scene).Result == PT_OK, std::string(Case.Name) + ": scene"))
				{
					continue;
				}
				double Accumulator = 0.0;
				std::string Sequence;
				for (size_t Frame = 0; Frame < Case.FrameDeltaTimes.size(); ++Frame)
				{
					const FSetUpForFrameParams Params = FStepPlanner::MakeSetUpForFrameParams(Case.FrameDeltaTimes[Frame], Settings, TestSettings);
					const FStepPlan Plan = FStepPlanner::PlanFrame(Params, AsyncDt, TestSettings.SubstepClampRule, Accumulator);
					const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
					PtFrameResult Result = { 0xFFFFFFFFu, 0 };
					const bool bOk = Api.SetUpForFrame(Scene, &Gravity, Params.DeltaSeconds, Params.MinPhysicsDeltaTime, Params.MaxPhysicsDeltaTime,
										 Params.MaxSubstepDeltaTime, Params.MaxSubsteps, Params.bSubstepping ? 1u : 0u) == PT_OK
						&& Api.StartFrame(Scene) == PT_OK && Api.WaitPhysScenes(Scene) == PT_OK && Api.EndFrame(Scene, &Result) == PT_OK;
					R.Check(bOk && static_cast<int32>(Result.NumStepsTaken) == Plan.NumSteps,
						std::string(Case.Name) + " frame " + std::to_string(Frame) + " dt " + std::to_string(Case.FrameDeltaTimes[Frame]) + ": NumStepsTaken "
							+ std::to_string(Result.NumStepsTaken) + " == FStepPlan::NumSteps " + std::to_string(Plan.NumSteps));
					Sequence += (Sequence.empty() ? "" : " ") + std::to_string(Plan.NumSteps);
				}
				R.Note(std::string(Case.Name) + " steps per frame: " + Sequence);
				R.Check(Api.DestroyScene(Scene) == PT_OK, std::string(Case.Name) + ": DestroyScene");
			}
		}

		// (b) Native driver vs forced Harness driver through FPhysScene: bit-identical in FixedStep and Sync, frames that run
		// no step included (Sync dt 1e-9 and 0: the commands of such a frame reach the backend in that frame).
		std::vector<float> FixedDts(120, Sixtieth);
		std::vector<float> SyncDts;
		std::vector<float> ZeroStepSyncDts;
		const float SyncPattern[] = { Sixtieth, RuntimeFloat(0.05f), RuntimeFloat(0.01f), RuntimeFloat(1.0f / 30.0f), RuntimeFloat(0.02f), RuntimeFloat(0.0125f) };
		const std::vector<float>& ZeroStepPattern = Cases[0].FrameDeltaTimes;   // (a)'s Sync frames: 0.05 ... 1e-9, 0
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			SyncDts.push_back(SyncPattern[Frame % 6]);
			ZeroStepSyncDts.push_back(ZeroStepPattern[static_cast<size_t>(Frame) % ZeroStepPattern.size()]);
		}
		const std::string Name(BackendName);
		using FDtCase = std::tuple<const char*, EPhysicsTickMode, const std::vector<float>*>;
		for (const auto& [ModeName, Mode, Dts] : { FDtCase{ "FixedStep", EPhysicsTickMode::FixedStep, &FixedDts }, FDtCase{ "Sync", EPhysicsTickMode::Sync, &SyncDts },
				 FDtCase{ "Sync with zero-step frames", EPhysicsTickMode::Sync, &ZeroStepSyncDts } })
		{
			const FDriverRun Native = RunDriver(Name, Mode, EFrameDriver::Native, *Dts);
			const FDriverRun Harness = RunDriver(Name, Mode, EFrameDriver::Harness, *Dts);
			const std::string What = std::string(ModeName) + ", 120 frames: ";
			R.Check(Native.Driver == EFrameDriver::Native && Harness.Driver == EFrameDriver::Harness, What + "the drivers are the requested ones");
			R.Check(Native.Steps == Harness.Steps, What + "same step count (" + std::to_string(Native.Steps) + " / " + std::to_string(Harness.Steps) + ")");
			R.Check(BitEqual(Native.SimTime, Harness.SimTime), What + "bit-identical SimTime");
			R.Check(Native.Hash == Harness.Hash && Native.Hash != 0, What + "identical native state hash");
			bool bSame = Native.States.size() == Harness.States.size();
			for (size_t Index = 0; bSame && Index < Native.States.size(); ++Index)
			{
				bSame = SameState(Native.States[Index], Harness.States[Index]);
			}
			R.Check(bSame, What + "bit-identical body states (pose, velocities, object state) of all " + std::to_string(Native.States.size()) + " bodies");
		}
		// Random command scripts (forces, torques, impulses, velocity sets with bAddToCurrent, sleep / wake, teleports and
		// kinematic targets through every entry point) over random frame dts with zero-step frames.
		constexpr int32 RandomFrames = 150;
		constexpr uint64 RandomSeeds = 6;
		for (const auto& [ModeName, Mode] : { std::pair<const char*, EPhysicsTickMode>{ "FixedStep", EPhysicsTickMode::FixedStep },
				 std::pair<const char*, EPhysicsTickMode>{ "Sync", EPhysicsTickMode::Sync } })
		{
			uint64 Identical = 0;
			std::string Mismatches;
			for (uint64 Seed = 1; Seed <= RandomSeeds; ++Seed)
			{
				const std::vector<float> Dts = MakeRandomDts(Seed, RandomFrames);
				const FDriverRun Native = RunRandomScript(Name, Mode, EFrameDriver::Native, Seed, Dts);
				const FDriverRun Harness = RunRandomScript(Name, Mode, EFrameDriver::Harness, Seed, Dts);
				const bool bSame = Native.Driver == EFrameDriver::Native && Harness.Driver == EFrameDriver::Harness && SameRun(Native, Harness);
				Identical += bSame ? 1u : 0u;
				Mismatches += bSame ? "" : (Mismatches.empty() ? "" : ", ") + std::to_string(Seed);
			}
			R.Check(Identical == RandomSeeds, std::string(ModeName) + ", random command scripts: " + std::to_string(Identical) + " / " + std::to_string(RandomSeeds)
				+ " seeds bit-identical (states, hash, SimTime, steps; " + std::to_string(RandomFrames) + " frames)" + (Mismatches.empty() ? "" : ", differing seeds " + Mismatches));
		}

		// (c) Substepping and AsyncFixed through FPhysScene with the Native driver: FPhysScene stops on any step-count
		// mismatch (Fatal); the totals match an independent plan of the same frames.
		for (const auto& [ModeName, Mode] : { std::pair<const char*, EPhysicsTickMode>{ "Substepping", EPhysicsTickMode::Substepping },
				 std::pair<const char*, EPhysicsTickMode>{ "AsyncFixed", EPhysicsTickMode::AsyncFixed } })
		{
			std::vector<float> Dts;
			const float Pattern[] = { RuntimeFloat(0.05f), Sixtieth, RuntimeFloat(0.2f), RuntimeFloat(0.01f), RuntimeFloat(0.0334f), RuntimeFloat(1.0f / 120.0f) };
			for (int32 Frame = 0; Frame < 60; ++Frame)
			{
				Dts.push_back(Pattern[Frame % 6]);
			}
			FPhysicsSettings Settings;
			FPhysicsTestSettings TestSettings;
			TestSettings.TickMode = Mode;
			double Accumulator = 0.0;
			uint64 Expected = 0;
			for (const float Dt : Dts)
			{
				Expected += static_cast<uint64>(FStepPlanner::PlanFrame(Dt, Settings, TestSettings, Accumulator).NumSteps);
			}
			const FDriverRun Native = RunDriver(Name, Mode, EFrameDriver::Native, Dts);
			R.Check(Native.Steps == Expected, std::string(ModeName) + " (Native driver, 60 frames): " + std::to_string(Native.Steps) + " steps == planned "
				+ std::to_string(Expected));
		}

		// Random-script tolerances of (d) (the measured worst cases on Null are in docs/Progress.md M2 section 7).
		FStateDifference RandomToleranceNoTorques;
		RandomToleranceNoTorques.Position = 1e-3;
		RandomToleranceNoTorques.Rotation = 1e-6;
		RandomToleranceNoTorques.LinearVelocity = 1e-3;
		RandomToleranceNoTorques.AngularVelocity = 1e-6;
		FStateDifference RandomTolerance;
		RandomTolerance.Position = 0.2;
		RandomTolerance.Rotation = 2e-2;
		RandomTolerance.LinearVelocity = 1e-3;
		RandomTolerance.AngularVelocity = 0.2;

		// (d) The multi-step modes, Native vs forced Harness driver through FPhysScene (the (b) scene and commands, 60
		// frames): the same steps, and the same UE5 frame rules for forces (every step of a pushing frame, averaged over the
		// frames without a push), impulses (first step) and kinematic targets (Lerp / Slerp by 1 / (N - k) when the solver
		// substeps, else the first step). The remaining difference is the step dt: Native steps with D / N in double, the
		// Harness driver with its float (UE5 substepping, relative 3.7e-8), plus rounding (force weight, pose round trips).
		// A driver that applied a force on the first substep only would miss its other 2-5 substeps over 10 frames: the box
		// velocity would be off by centimetres per second, far above the tolerance. AsyncFixed weights of 1 and 1/2 are exact,
		// so that mode comes out bit-identical here.
		for (const auto& [ModeName, Mode] : { std::pair<const char*, EPhysicsTickMode>{ "Substepping", EPhysicsTickMode::Substepping },
				 std::pair<const char*, EPhysicsTickMode>{ "AsyncFixed", EPhysicsTickMode::AsyncFixed } })
		{
			std::vector<float> Dts;
			const float Pattern[] = { RuntimeFloat(0.05f), Sixtieth, RuntimeFloat(0.2f), RuntimeFloat(0.01f), RuntimeFloat(0.0334f), RuntimeFloat(1.0f / 120.0f) };
			for (int32 Frame = 0; Frame < 60; ++Frame)
			{
				Dts.push_back(Pattern[Frame % 6]);
			}
			const FDriverRun Native = RunDriver(Name, Mode, EFrameDriver::Native, Dts);
			const FDriverRun Harness = RunDriver(Name, Mode, EFrameDriver::Harness, Dts);
			const std::string What = std::string(ModeName) + ", 60 frames, Native vs Harness: ";
			R.Check(Native.Driver == EFrameDriver::Native && Harness.Driver == EFrameDriver::Harness, What + "the drivers are the requested ones");
			R.Check(Native.Steps == Harness.Steps, What + "same step count (" + std::to_string(Native.Steps) + " / " + std::to_string(Harness.Steps) + ")");
			const FStateDifference Difference = CompareStates(Native.States, Harness.States);
			char Text[256];
			std::snprintf(Text, sizeof(Text), "max |dx| %.3g cm, |dq| %.3g, |dv| %.3g cm/s, |dw| %.3g rad/s", Difference.Position, Difference.Rotation,
				Difference.LinearVelocity, Difference.AngularVelocity);
			R.Note(What + Text);
			R.Check(Difference.bSameObjectStates, What + "the same object state for every body");
			R.Check(Difference.Position <= 1e-3 && Difference.Rotation <= 1e-6 && Difference.LinearVelocity <= 1e-3 && Difference.AngularVelocity <= 1e-6,
				What + "states agree within 1e-3 cm, 1e-6 (quaternion), 1e-3 cm/s, 1e-6 rad/s (" + Text + ")");

			// The random command scripts of (b): the same step count and object states (sleep / wake follow issue order in
			// both drivers) and agreement within a tolerance. Without torques the drivers differ only by the step dt and
			// rounding, as above. With torques the Harness driver converts a torque to an angular acceleration with each
			// step's world inertia (an SDK engine's AddTorque), Native once at the push (Chaos' game-thread AddTorque); a body
			// spinning at a few rad/s drifts apart by a few 1e-2 rad/s over 150 frames, and its actor position by the
			// rotation difference times its COM offset. The linear velocity (the COM's) stays tight in both: a force on the
			// wrong steps or a sleep / wake out of order differs by centimetres per second or in object state.
			for (const bool bNoTorques : { true, false })
			{
				FStateDifference Worst;
				bool bSameStates = true;
				for (uint64 Seed = 1; Seed <= RandomSeeds; ++Seed)
				{
					const std::vector<float> RandomDts = MakeRandomDts(Seed, RandomFrames);
					const FDriverRun RandomNative = RunRandomScript(Name, Mode, EFrameDriver::Native, Seed, RandomDts, bNoTorques);
					const FDriverRun RandomHarness = RunRandomScript(Name, Mode, EFrameDriver::Harness, Seed, RandomDts, bNoTorques);
					const FStateDifference Seeded = CompareStates(RandomNative.States, RandomHarness.States);
					bSameStates = bSameStates && Seeded.bSameObjectStates && RandomNative.Steps == RandomHarness.Steps;
					Worst.Position = Seeded.Position > Worst.Position ? Seeded.Position : Worst.Position;
					Worst.Rotation = Seeded.Rotation > Worst.Rotation ? Seeded.Rotation : Worst.Rotation;
					Worst.LinearVelocity = Seeded.LinearVelocity > Worst.LinearVelocity ? Seeded.LinearVelocity : Worst.LinearVelocity;
					Worst.AngularVelocity = Seeded.AngularVelocity > Worst.AngularVelocity ? Seeded.AngularVelocity : Worst.AngularVelocity;
				}
				const FStateDifference& Tolerance = bNoTorques ? RandomToleranceNoTorques : RandomTolerance;
				std::snprintf(Text, sizeof(Text), "max |dx| %.3g cm, |dq| %.3g, |dv| %.3g cm/s, |dw| %.3g rad/s", Worst.Position, Worst.Rotation, Worst.LinearVelocity,
					Worst.AngularVelocity);
				char Limits[160];
				std::snprintf(Limits, sizeof(Limits), "%.3g cm, %.3g, %.3g cm/s, %.3g rad/s", Tolerance.Position, Tolerance.Rotation, Tolerance.LinearVelocity,
					Tolerance.AngularVelocity);
				const std::string RandomWhat = std::string(ModeName) + ", random command scripts" + (bNoTorques ? " without torques" : "") + " ("
					+ std::to_string(RandomSeeds) + " seeds), Native vs Harness: ";
				R.Note(RandomWhat + Text);
				R.Check(bSameStates, RandomWhat + "the same step count and object state for every body");
				R.Check(Worst.Position <= Tolerance.Position && Worst.Rotation <= Tolerance.Rotation && Worst.LinearVelocity <= Tolerance.LinearVelocity
						&& Worst.AngularVelocity <= Tolerance.AngularVelocity,
					RandomWhat + "states agree within " + Limits + " (" + Text + ")");
			}
		}

		// The registry module is initialised by the FPhysScene runs: leave it terminated, as the other gates do.
		if (Target.Module->IsInitialized())
		{
			Target.Module->TermGamePhys();
		}
		return R;
	}
}
