// PhysScene.cpp - FPhysScene over the C ABI (see PhysScene.h): backend module + scene proxy, the two frame drivers,
// timing buckets and the harness console-variable guard.
#include "PhysScene.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Hash/Sha256.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Physics/AnalyticReference.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC(LogPhysScene, Log, All);

namespace PhysicsTest
{
	namespace
	{
		struct FPendingForce
		{
			FBodyInstance* Body = nullptr;
			FVector Value;
			uint32 FirstStep = 0;            // UE4PhysSubstep: the substep it was issued in (OnPhysSceneStep); 0 otherwise
			bool bAllowSubstepping = false;
			bool bAccelChange = false;
			bool bIsTorque = false;
			// The body was put to sleep after this force and nothing wrote its dynamics again: UE marks the game-thread
			// dynamics clean, so the force is never pushed (ParticleHandle.h:3755-3766 [V]); the push drops it.
			bool bClean = false;
		};

		struct FPendingKinematicTarget
		{
			FBodyInstance* Body = nullptr;
			FTransform Target;
			FTransform Start;     // the body pose at the frame's first step
			FTransform Current;   // the pose sent for the previous step (the kinematic body reaches every sent target)
			bool bAllowSubstepping = true;
			// A teleport this frame fixed Start / Current (the pulled pose is stale until the step runs).
			bool bStartIsTeleport = false;
		};

		// One harness physics console variable as this scene requires it (PhysScene.h).
		struct FSceneConsoleVariable
		{
			IConsoleVariable* Variable = nullptr;
			std::string Value;   // the variable's text after the scene applied it
		};

		PtVec3 ToPt(const FVector& V)
		{
			return PtVec3{ V.X, V.Y, V.Z };
		}

		void SetTransform(PtBodyCommand& Command, const FTransform& Transform)
		{
			const FQuat& Q = Transform.GetRotation();
			Command.Transform.Rotation = PtQuat{ Q.X, Q.Y, Q.Z, Q.W };
			Command.Transform.Translation = ToPt(Transform.GetTranslation());
		}

		// FMath::Lerp(A, B, Alpha) on a pose: A + Alpha * (B - A) and FQuat::Slerp (kinematic targets per step, 02 9.1).
		FTransform LerpPose(const FTransform& A, const FTransform& B, const double Alpha)
		{
			const FVector From = A.GetTranslation();
			return FTransform(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha), From + (B.GetTranslation() - From) * Alpha);
		}

		uint64 TicksToNs(const uint64 Ticks)
		{
			return static_cast<uint64>(static_cast<double>(Ticks) * 1.0e9 * FPlatformTime::GetSecondsPerCycle64() + 0.5);
		}

		// Canonical byte stream for the run hashes (02 12.3): every value as a fixed-width little-endian field (doubles and
		// floats by their bit pattern), so the hash never depends on struct padding, text formatting or the flag profile.
		struct FHashWriter
		{
			FSHA256 Sha;

			void Tag(const char* Text)
			{
				const uint32 Length = static_cast<uint32>(std::strlen(Text));
				U32(Length);
				Sha.Update(Text, Length);
			}
			void Str(const std::string& Text)
			{
				U32(static_cast<uint32>(Text.size()));
				Sha.Update(Text.data(), Text.size());
			}
			void U32(const uint32 Value) { Sha.Update(&Value, sizeof(Value)); }
			void I32(const int32 Value) { Sha.Update(&Value, sizeof(Value)); }
			void U64(const uint64 Value) { Sha.Update(&Value, sizeof(Value)); }
			void F64(const double Value)
			{
				uint64 Bits = 0;
				std::memcpy(&Bits, &Value, sizeof(Bits));
				U64(Bits);
			}
			void F32(const float Value)
			{
				uint32 Bits = 0;
				std::memcpy(&Bits, &Value, sizeof(Bits));
				U32(Bits);
			}
			void Vec(const PtVec3& V) { F64(V.X); F64(V.Y); F64(V.Z); }
			void Vec(const FVector& V) { F64(V.X); F64(V.Y); F64(V.Z); }
			void Quat(const PtQuat& Q) { F64(Q.X); F64(Q.Y); F64(Q.Z); F64(Q.W); }
			void Transform(const PtTransform& T) { Quat(T.Rotation); Vec(T.Translation); }
		};

		// The engine-independent part of one flush (desc_sha256, DL43). Per-body solver iteration counts and the
		// PT_BODY_OVERRIDE_ITERATIONS flag are left out: the physics profiles mark them EngineDefault (a backend variant may
		// set them, 02 10), so they go to options_sha256 with the other engine-specific inputs.
		void HashBatchDescription(FHashWriter& W, const FMarshalledActorBatch& Batch)
		{
			W.Tag("flush");
			W.U32(static_cast<uint32>(Batch.NewMaterials.size()));
			for (const PtPhysicalMaterial& M : Batch.NewMaterials)
			{
				W.F64(M.Friction);
				W.F64(M.StaticFriction);
				W.F64(M.Restitution);
				W.F64(M.SleepLinearVelocityThreshold);
				W.F64(M.SleepAngularVelocityThreshold);
				W.U32(M.SleepCounterThreshold);
				W.U32(M.FrictionCombineMode);
				W.U32(M.RestitutionCombineMode);
			}
			W.U32(static_cast<uint32>(Batch.Actors.size()));
			for (const PtActorCreateInfo& A : Batch.Actors)
			{
				W.U32(A.BodyIndex);
				W.U32(A.Motion);
				W.U32(A.Flags & ~PT_BODY_OVERRIDE_ITERATIONS);
				W.U32(A.FirstShape);
				W.U32(A.NumShapes);
				W.U32(A.ObjectType);
				W.U32(A.BlockMask);
				W.Transform(A.ActorTransform);
				W.Vec(A.InitialLinearVelocity);
				W.Vec(A.InitialAngularVelocity);
				W.F64(A.Mass);
				W.Vec(A.InertiaDiagonal);
				W.Transform(A.CenterOfMassLocal);
				W.F64(A.LinearDamping);
				W.F64(A.AngularDamping);
				W.F64(A.MaxAngularVelocity);
				W.F64(A.MaxLinearVelocity);
				W.F64(A.MaxDepenetrationVelocity);
				W.F64(A.SleepThresholdMultiplier);
				W.F64(A.StabilizationThresholdMultiplier);
			}
			W.U32(static_cast<uint32>(Batch.Shapes.size()));
			for (const PtShapeElem& E : Batch.Shapes)
			{
				W.Transform(E.LocalTransform);
				W.U32(E.Type);
				W.U32(E.MaterialIndex);
				W.F64(E.Radius);
				W.F64(E.HalfLength);
				W.Vec(E.BoxHalfExtent);
				W.U32(E.FirstVertex);
				W.U32(E.NumVertices);
				W.F64(E.ContactOffset);
				W.F64(E.RestOffset);
				W.U32(E.Flags);
			}
			W.U32(static_cast<uint32>(Batch.ConvexVertices.size()));
			for (const PtVec3& V : Batch.ConvexVertices)
			{
				W.Vec(V);
			}
		}

		// The engine-specific part of one flush (options_sha256): the per-body iteration counts.
		void HashBatchOptions(FHashWriter& W, const FMarshalledActorBatch& Batch)
		{
			W.Tag("flush");
			W.U32(static_cast<uint32>(Batch.Actors.size()));
			for (const PtActorCreateInfo& A : Batch.Actors)
			{
				W.U32((A.Flags & PT_BODY_OVERRIDE_ITERATIONS) != 0 ? 1u : 0u);
				W.U32(A.PositionIterations);
				W.U32(A.VelocityIterations);
				W.U32(A.ProjectionIterations);
			}
		}
	}

	struct FPhysScene::FImpl
	{
		std::string DebugName;
		std::string BackendName;
		FPhysicsSettings Settings;
		FPhysicsTestSettings TestSettings;
		FPhysicalMaterial DefaultMaterial;
		double AsyncDeltaTime = -1.0;

		std::deque<FPhysicalMaterial> Materials;
		std::deque<FBodySetup> BodySetups;
		std::deque<FBodyInstance> BodyStorage;
		std::deque<FConstraintInstance> ConstraintStorage;
		std::vector<FBodyInstance*> Bodies;               // creation order
		std::vector<FConstraintInstance*> Constraints;
		std::vector<FBodyInstance*> DeferredBodies;

		FPhysicsBackendModule* Module = nullptr;
		std::unique_ptr<FPhysicsBackendScene> Backend;
		EFrameDriver Driver = EFrameDriver::Harness;
		std::vector<FSceneConsoleVariable> ConsoleVariables;
		std::vector<FPendingForce> Forces;               // issue order; held until a frame pushes steps (both drivers)
		uint32 NumCleanForces = 0;
		std::vector<FPendingKinematicTarget> KinematicTargets;
		uint32 CurrentStep = 0;                         // Harness driver: the step being pushed (UE4 OnPhysSceneStep forces)
		// Harness driver, UE5 rule: FPhysicsSolverBase::ExternalSteps (frames with dt > 0 since the last push) and the weight
		// 1 / ExternalSteps the forces of the pushing frame are scaled with (PhysicsSolverBase.cpp:623-646 [V]).
		uint32 ExternalSteps = 0;
		double ForceWeight = 1.0;

		// The scene description as created (pointers cleared) and the engine options it was created with.
		PtSceneDesc SceneDesc = {};
		std::vector<std::pair<std::string, std::string>> SceneOptions;
		// Running hashes of every flush (ComputeDescSha256 / ComputeOptionsSha256 finish copies of them).
		FHashWriter DescFlushes;
		FHashWriter OptionsFlushes;
		uint32 NumFlushes = 0;

		EPhysSceneFramePhase Phase = EPhysSceneFramePhase::Idle;
		bool bIsSimulating = false;
		FVector Gravity;
		FStepPlan LastPlan;
		double AccumulatedTime = 0.0;   // AsyncFixed accumulator of the harness plan (Chaos AccumulatedTime)
		double SimTime = 0.0;
		uint64 FrameCount = 0;
		uint64 StepCount = 0;
		FPhysSceneStats Stats;
		FFrameTiming LastTiming;
		FFrameTiming Timing;            // the frame in progress

		// 02 7.1: creation, flushes and preparation happen only in Idle (not between SetUpForFrame and EndFrame).
		[[nodiscard]] bool IsIdle() const { return Phase == EPhysSceneFramePhase::Idle; }

		// Fatal with the backend's last error (a failed ABI call on a valid request is a bug, not a recoverable state).
		void CheckBackend(const PtResult Result, const char* What) const
		{
			if (Result < 0)
			{
				UE_LOG(LogPhysScene, Fatal, "scene '%s' (backend '%s'): %s failed with %s: %s", DebugName.c_str(), BackendName.c_str(), What,
					GetPtResultName(Result), Backend->GetLastErrorMessage().c_str());
			}
		}

		void ApplyConsoleVariables(const FResolvedPhysicsConfig* Config);
		void VerifyConsoleVariables(const char* Where) const;
		void PushFrameCommands(uint32 StepIndex, uint32 NumSteps);
		void RunBackendFrame(const FVector& FrameGravity, const FSetUpForFrameParams& Params, uint32 ExpectedSteps, uint32 ExpectedPushedSteps);
		void FlushQueueOnly();
		void AddForce(FBodyInstance* Body, const FVector& Value, bool bAllowSubstepping, bool bAccelChange, bool bIsTorque);
		void SetForcesClean(const FBodyInstance* Body, bool bClean);
		FPendingKinematicTarget& FindOrAddKinematicTarget(FBodyInstance* Body);
	};

	void FPhysScene::FImpl::ApplyConsoleVariables(const FResolvedPhysicsConfig* Config)
	{
		// Every harness physics console variable gets the resolved config's value (the built-in default when the config
		// has none): the scene's masses and sleep thresholds must not depend on what an earlier scene, test or -ExecCmds
		// left in the process-global variables (M1 review, docs/Progress.md M2).
		for (IConsoleVariable* Variable : PhysicsCVars::GetHarnessConsoleVariables())
		{
			std::string Expected = Variable->GetDefaultString();
			EConsoleVariableFlags SetBy = ECVF_SetByConstructor;
			std::string OriginName = "built-in";
			if (Config != nullptr)
			{
				if (const FResolvedConfigValue* Resolved = Config->Find(std::string("ConsoleVariables.") + Variable->GetName()))
				{
					Expected = Resolved->Value;
					SetBy = FProfileResolver::GetConsoleVariableSetBy(*Resolved);
					OriginName = Resolved->OriginName;
				}
			}
			const std::string Before = Variable->GetString();
			const EConsoleVariableFlags BeforeSetBy = Variable->GetSetBy();
			if (SetBy == ECVF_SetByConstructor)
			{
				if (Before != Expected || BeforeSetBy != ECVF_SetByConstructor)
				{
					Variable->ResetToDefault();
				}
			}
			else if (Before != Expected && !Variable->Set(Expected.c_str(), SetBy))
			{
				// A stale value with a higher priority (e.g. an earlier -ExecCmds): the scene's config wins.
				Variable->ResetToDefault();
				if (!Variable->Set(Expected.c_str(), SetBy))
				{
					UE_LOG(LogPhysScene, Fatal, "scene '%s': cannot set %s to the config value '%s' (%s)", DebugName.c_str(), Variable->GetName(),
						Expected.c_str(), OriginName.c_str());
				}
			}
			if (Variable->GetString() != Before)
			{
				UE_LOG(LogPhysScene, Warning, "scene '%s': %s was '%s' (set by %s); applied the scene config value '%s' (%s)", DebugName.c_str(),
					Variable->GetName(), Before.c_str(), GetConsoleVariableSetByName(BeforeSetBy), Variable->GetString().c_str(), OriginName.c_str());
			}
			ConsoleVariables.push_back(FSceneConsoleVariable{ Variable, Variable->GetString() });
		}
	}

	void FPhysScene::FImpl::VerifyConsoleVariables(const char* Where) const
	{
		for (const FSceneConsoleVariable& Entry : ConsoleVariables)
		{
			const std::string Live = Entry.Variable->GetString();
			if (Live != Entry.Value)
			{
				UE_LOG(LogPhysScene, Fatal, "scene '%s' (%s): harness console variable %s changed from '%s' to '%s' (set by %s) while the scene exists; "
					"scenes alive at the same time must agree on the harness physics console variables", DebugName.c_str(), Where, Entry.Variable->GetName(),
					Entry.Value.c_str(), Live.c_str(), GetConsoleVariableSetByName(Entry.Variable->GetSetBy()));
			}
		}
	}

	// [T_push] Scene-level per-frame commands for one backend frame, appended to the proxy's queue (which already holds the
	// FPhysicsInterface commands and the forces' wakes issued since the last flush).
	//   Native   one backend frame of all steps (StepIndex 0, NumSteps 1): targets as given every frame, forces at a frame
	//            that pushes steps; the backend applies UE5's frame rules itself (PtBackendAbi.h body commands, docs/Abi.md
	//            5.10)
	//   Harness  step StepIndex of NumSteps, by the rule the plan followed (02 9.1):
	//            UE5ChaosScene   every force on every step, scaled by ForceWeight (Chaos CopySubstepData and
	//                            DynamicsWeight, ChaosMarshallingManager.cpp:105-111, :325-342, PBDRigidsSolver.cpp:1582 [V]).
	//                            Kinematic targets: solver substepping moves 1 / (N - k) of the way from the last sent pose
	//                            (Lerp / Slerp, PhysicsSolverBase.cpp:250, PBDRigidsEvolutionGBF.cpp:1203-1220 [V]), the
	//                            last step reaches the target; otherwise the target goes with the first step and the later
	//                            steps send none (the backend zeroes the kinematic velocity, Chaos mode Reset)
	//            UE4PhysSubstep  bAllowSubstepping forces on every substep from the one they were issued in, the others on
	//                            that one only; kinematic targets with bAllowSubstepping Lerp / Slerp from the frame's start
	//                            pose at alpha (k + 1) / N, the others on the first substep (UE4 FPhysSubstepTask [M])
	// Every force carries PT_CMDF_NO_WAKE: its game-thread wake went into the queue when it was issued (FImpl::AddForce).
	void FPhysScene::FImpl::PushFrameCommands(const uint32 StepIndex, const uint32 NumSteps)
	{
		const bool bNative = Driver == EFrameDriver::Native;
		const bool bUE4Rule = !bNative && LastPlan.ClampRule == ESubstepClampRule::UE4PhysSubstep;
		const bool bLastStep = StepIndex + 1 >= NumSteps;
		for (FPendingKinematicTarget& Target : KinematicTargets)
		{
			if (StepIndex == 0 && !Target.bStartIsTeleport)
			{
				Target.Start = Target.Body->State.BodyTM;
				Target.Current = Target.Start;
			}
			const bool bInterpolate = bUE4Rule ? Target.bAllowSubstepping : LastPlan.bSolverSubstepped;
			if (!bNative && !bInterpolate && StepIndex > 0)
			{
				continue;   // not interpolated: reached on the first step
			}
			FTransform Pose = Target.Target;
			if (!bNative && bInterpolate && !bLastStep)
			{
				Pose = bUE4Rule ? LerpPose(Target.Start, Target.Target, static_cast<double>(StepIndex + 1) / static_cast<double>(NumSteps))
					: LerpPose(Target.Current, Target.Target, 1.0 / static_cast<double>(NumSteps - StepIndex));
			}
			Target.Current = Pose;
			PtBodyCommand Command = {};
			Command.Type = PT_CMD_SET_KINEMATIC_TARGET;
			Command.Body = Target.Body->ActorHandle.Index;
			SetTransform(Command, Pose);
			Backend->EnqueueCommand(Command);
		}
		if (LastPlan.NumPushedSteps <= 0)
		{
			return;   // Native, a frame that pushes nothing (AsyncFixed accumulating): the forces wait for the push
		}
		const double Weight = bNative || bUE4Rule ? 1.0 : ForceWeight;
		for (const FPendingForce& Force : Forces)
		{
			if (Force.bClean)
			{
				continue;
			}
			if (bUE4Rule && (StepIndex < Force.FirstStep || (StepIndex > Force.FirstStep && !Force.bAllowSubstepping)))
			{
				continue;
			}
			PtBodyCommand Command = {};
			Command.Type = Force.bIsTorque ? PT_CMD_ADD_TORQUE : PT_CMD_ADD_FORCE;
			Command.Body = Force.Body->ActorHandle.Index;
			Command.Transform.Rotation.W = 1.0;
			// ALLOW_SUBSTEPPING is informational for a backend (PtBackendAbi.h); the Harness driver has applied the rule above.
			Command.Flags = PT_CMDF_NO_WAKE | (Force.bAccelChange ? PT_CMDF_ACCEL_CHANGE : 0u) | (Force.bAllowSubstepping ? PT_CMDF_ALLOW_SUBSTEPPING : 0u);
			// Chaos scales the accumulated acceleration, the harness each force (equal for weight 1; AsyncFixed may differ
			// in the last bit).
			Command.Vector = ToPt(Weight == 1.0 ? Force.Value : Force.Value * Weight);
			Backend->EnqueueCommand(Command);
		}
	}

	// [T_push] Harness driver, a frame without steps: what was issued during the frame reaches the backend now (UE5's
	// game-thread writes take effect when they are made). Forces and kinematic targets stay with the scene (frame rules).
	void FPhysScene::FImpl::FlushQueueOnly()
	{
		if (!Backend->HasPendingCommands())
		{
			return;   // no commands: no call, T_push 0 (02 9.3)
		}
		const uint64 PushBegin = FPlatformTime::Cycles64();
		const PtResult FlushResult = Backend->FlushCommands();
		const uint64 PushEnd = FPlatformTime::Cycles64();
		CheckBackend(FlushResult, "ApplyBodyCommands");
		Timing.PushNs += TicksToNs(PushEnd - PushBegin);
		Timing.FrameQpcTicks += PushEnd - PushBegin;
	}

	// UE's AddForce / AddTorque (SingleParticlePhysicsProxy.h:706-720, :757-771 [V]): a simulated body wakes when the
	// call is made - the WAKE_UP goes into the queue in issue order - and the force joins the scene's list, which the push
	// sends with PT_CMDF_NO_WAKE. A new force re-dirties the body's dynamics: forces it had before a PutToSleep are pushed
	// with it ("the old forces will remain", ParticleHandle.h:3761-3764 [V]).
	void FPhysScene::FImpl::AddForce(FBodyInstance* Body, const FVector& Value, const bool bAllowSubstepping, const bool bAccelChange, const bool bIsTorque)
	{
		if (Body->IsDynamic())
		{
			PtBodyCommand Wake = {};
			Wake.Type = PT_CMD_WAKE_UP;
			Wake.Body = Body->ActorHandle.Index;
			Wake.Transform.Rotation.W = 1.0;
			Backend->EnqueueCommand(Wake);
			SetForcesClean(Body, false);
		}
		const uint32 FirstStep = Phase == EPhysSceneFramePhase::Started ? CurrentStep : 0u;
		Forces.push_back(FPendingForce{ Body, Value, FirstStep, bAllowSubstepping, bAccelChange, bIsTorque, false });
	}

	void FPhysScene::FImpl::SetForcesClean(const FBodyInstance* Body, const bool bClean)
	{
		if (Forces.empty() || (!bClean && NumCleanForces == 0))
		{
			return;
		}
		for (FPendingForce& Force : Forces)
		{
			if (Force.Body == Body && Force.bClean != bClean)
			{
				Force.bClean = bClean;
				NumCleanForces = bClean ? NumCleanForces + 1u : NumCleanForces - 1u;
			}
		}
	}

	FPendingKinematicTarget& FPhysScene::FImpl::FindOrAddKinematicTarget(FBodyInstance* Body)
	{
		for (FPendingKinematicTarget& Existing : KinematicTargets)
		{
			if (Existing.Body == Body)
			{
				return Existing;
			}
		}
		return KinematicTargets.emplace_back(FPendingKinematicTarget{ Body, Body->State.BodyTM, Body->State.BodyTM, Body->State.BodyTM, true, false });
	}

	// One backend frame: [T_push] flush, [T_step] SetUpForFrame -> StartFrame -> WaitPhysScenes -> EndFrame. ABI v1.0: a backend
	// that reports PtFrameResult::NumPushedSteps (not PT_INDEX_NONE) must have pushed what the harness planned too.
	void FPhysScene::FImpl::RunBackendFrame(const FVector& FrameGravity, const FSetUpForFrameParams& Params, const uint32 ExpectedSteps,
		const uint32 ExpectedPushedSteps)
	{
		const uint64 PushBegin = FPlatformTime::Cycles64();
		const bool bHasCommands = Backend->HasPendingCommands();
		const PtResult FlushResult = bHasCommands ? Backend->FlushCommands() : PT_OK;   // no commands: no call, T_push 0 (02 9.3)
		const uint64 PushEnd = FPlatformTime::Cycles64();
		CheckBackend(FlushResult, "ApplyBodyCommands");

		const PtResult SetUpResult = Backend->SetUpForFrame(&FrameGravity, Params.DeltaSeconds, Params.MinPhysicsDeltaTime, Params.MaxPhysicsDeltaTime,
			Params.MaxSubstepDeltaTime, Params.MaxSubsteps, Params.bSubstepping);
		const uint64 SetUpEnd = FPlatformTime::Cycles64();
		const PtResult StartResult = SetUpResult >= 0 ? Backend->StartFrame() : SetUpResult;
		const uint64 StartEnd = FPlatformTime::Cycles64();
		const PtResult WaitResult = StartResult >= 0 ? Backend->WaitPhysScenes() : StartResult;
		const uint64 WaitEnd = FPlatformTime::Cycles64();
		// EndFrame writes all four fields (docs/Abi.md 5.8); the host still pre-fills NumPushedSteps with "not reported", as the
		// Kit does, so a backend outside the Kit that writes only the M5 fields cannot trip the push check below.
		PtFrameResult Frame = {};
		Frame.NumPushedSteps = PT_INDEX_NONE;
		const PtResult EndResult = WaitResult >= 0 ? Backend->EndFrame(Frame) : WaitResult;
		const uint64 EndEnd = FPlatformTime::Cycles64();
		CheckBackend(SetUpResult, "SetUpForFrame");
		CheckBackend(StartResult, "StartFrame");
		CheckBackend(WaitResult, "WaitPhysScenes");
		CheckBackend(EndResult, "EndFrame");

		Timing.PushNs += bHasCommands ? TicksToNs(PushEnd - PushBegin) : 0u;
		Timing.SetupNs += TicksToNs(SetUpEnd - PushEnd);
		Timing.StartNs += TicksToNs(StartEnd - SetUpEnd);
		Timing.WaitNs += TicksToNs(WaitEnd - StartEnd);
		Timing.EndNs += TicksToNs(EndEnd - WaitEnd);
		Timing.StepNs += TicksToNs(EndEnd - PushEnd);
		Timing.FrameQpcTicks += (bHasCommands ? PushEnd - PushBegin : 0u) + (EndEnd - PushEnd);
		Timing.EngineErrorBits |= Frame.EngineErrorBits;
		if (Frame.NumStepsTaken != ExpectedSteps)
		{
			// The Native-driver contract (02 9.1, A10): the engine steps exactly what the harness planned.
			UE_LOG(LogPhysScene, Fatal, "scene '%s' (backend '%s', %s driver): the backend took %u steps, the harness planned %u (dt %.9g, min %.9g, max %.9g, "
				"max substep %.9g x %d, substepping %d, async dt %.9g)", DebugName.c_str(), BackendName.c_str(), ToString(Driver), Frame.NumStepsTaken,
				ExpectedSteps, Params.DeltaSeconds, Params.MinPhysicsDeltaTime, Params.MaxPhysicsDeltaTime, Params.MaxSubstepDeltaTime, Params.MaxSubsteps,
				Params.bSubstepping ? 1 : 0, AsyncDeltaTime);
		}
		if (Frame.NumPushedSteps != PT_INDEX_NONE && Frame.NumPushedSteps != ExpectedPushedSteps)
		{
			UE_LOG(LogPhysScene, Fatal, "scene '%s' (backend '%s', %s driver): the backend pushed %u steps, the harness planned %u (dt %.9g, async dt %.9g)",
				DebugName.c_str(), BackendName.c_str(), ToString(Driver), Frame.NumPushedSteps, ExpectedPushedSteps, Params.DeltaSeconds, AsyncDeltaTime);
		}
	}

	FBackendInitParams FPhysScene::MakeBackendInitParams(const FResolvedPhysicsConfig* Config)
	{
		const FPhysicsTestSettings TestSettings = Config != nullptr ? Config->TestSettings : FPhysicsTestSettings();
		FBackendInitParams InitParams;
		InitParams.MaxThreads = static_cast<uint32>(TestSettings.NumThreads > 0 ? TestSettings.NumThreads : 1);
		if (Config != nullptr)
		{
			InitParams.InitOptions = Config->InitOptions;
		}
		return InitParams;
	}

	FPhysScene::FPhysScene(const FPhysSceneInitParams& Params)
		: Impl(std::make_unique<FImpl>())
	{
		Impl->DebugName = Params.DebugName;
		Impl->BackendName = Params.BackendName;
		if (Impl->BackendName.empty())
		{
			Impl->BackendName = Params.Config != nullptr && !Params.Config->Engine.empty() ? Params.Config->Engine : std::string("Null");
		}
		if (Params.Config != nullptr)
		{
			Impl->Settings = Params.Config->Settings;
			Impl->TestSettings = Params.Config->TestSettings;
			FProfileResolver::ApplyMaterialDefaults(*Params.Config, Impl->DefaultMaterial);
		}
		Impl->DefaultMaterial.Name = "DefaultPhysicalMaterial";
		Impl->Gravity = FVector(0.0, 0.0, Impl->Settings.DefaultGravityZ);
		// The scene's async dt (FChaosScene(nullptr, AsyncDt)): -1 = sync (DL40).
		Impl->AsyncDeltaTime = FStepPlanner::GetAsyncDeltaTime(Impl->Settings, Impl->TestSettings);
		Impl->ApplyConsoleVariables(Params.Config);

		// Backend module (loaded and initialised once per process) and the frame driver.
		const FBackendInitParams InitParams = MakeBackendInitParams(Params.Config);
		std::string Error;
		Impl->Module = FPhysicsBackendRegistry::Get().FindOrLoad(Impl->BackendName, InitParams, Error);
		if (Impl->Module == nullptr)
		{
			UE_LOG(LogPhysScene, Fatal, "scene '%s': backend '%s' is not available: %s", Impl->DebugName.c_str(), Impl->BackendName.c_str(), Error.c_str());
		}
		const bool bNativeCapable = Impl->Module->GetInfo().Has(PT_CAP_NATIVE_FRAME_MODEL);
		// The UE4 substep formula (ESubstepClampRule::UE4PhysSubstep, PhysX UE4Compat) exists only in the harness planner:
		// a native backend applies UE5's rules, so that combination is Harness-driven.
		const bool bHarnessOnlyRule = Impl->TestSettings.TickMode == EPhysicsTickMode::Substepping
			&& Impl->TestSettings.SubstepClampRule == ESubstepClampRule::UE4PhysSubstep;
		switch (Impl->TestSettings.FrameDriver)
		{
		case EFrameDriver::Auto:
			Impl->Driver = bNativeCapable && !bHarnessOnlyRule ? EFrameDriver::Native : EFrameDriver::Harness;
			break;
		case EFrameDriver::Harness:
			Impl->Driver = EFrameDriver::Harness;   // forcing it on Chaos is diagnostic only (02 9.1)
			break;
		case EFrameDriver::Native:
			if (!bNativeCapable)
			{
				UE_LOG(LogPhysScene, Fatal, "scene '%s': FrameDriver=Native but backend '%s' does not declare PT_CAP_NATIVE_FRAME_MODEL",
					Impl->DebugName.c_str(), Impl->BackendName.c_str());
			}
			if (bHarnessOnlyRule)
			{
				UE_LOG(LogPhysScene, Fatal, "scene '%s': FrameDriver=Native cannot follow SubstepClampRule=UE4PhysSubstep (a harness-only planner rule; "
					"native backends substep with UE5's FChaosScene rules)", Impl->DebugName.c_str());
			}
			Impl->Driver = EFrameDriver::Native;
			break;
		}

		std::vector<PtKeyValue> Options;
		if (Params.Config != nullptr)
		{
			Impl->SceneOptions = Params.Config->SceneOptions;
		}
		for (const auto& [Key, Value] : Impl->SceneOptions)
		{
			Options.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
		}
		PtSceneDesc Desc = {};
		Desc.StructSize = sizeof(PtSceneDesc);
		Desc.RunMode = static_cast<uint32>(Impl->TestSettings.RunMode);   // Bench / Behavior / Viz = PT_RUN_*
		Desc.InstrumentFlags = Impl->TestSettings.InstrumentFlags;
		Desc.SimFlags = (Impl->TestSettings.bAllowSleeping ? PT_SIM_ALLOW_SLEEPING : 0u)
			| (Impl->Settings.bEnableEnhancedDeterminism ? PT_SIM_ENHANCED_DETERMINISM : 0u);
		Desc.DebugName = Impl->DebugName.c_str();
		Desc.Gravity = ToPt(Impl->Gravity);
		Desc.NominalDeltaTime = static_cast<double>(Impl->TestSettings.FixedDeltaTime);
		// Native: the backend runs the async accumulator itself; Harness: the harness plans the steps, the scene is sync.
		Desc.AsyncFixedTimeStepSize = Impl->Driver == EFrameDriver::Native && Impl->AsyncDeltaTime > 0.0 ? Impl->AsyncDeltaTime : PT_ASYNC_DT_SYNC;
		Desc.RestitutionThreshold = Impl->TestSettings.RestitutionThresholdVelocity > 0.0f ? static_cast<double>(Impl->TestSettings.RestitutionThresholdVelocity) : -1.0;
		Desc.MaxLinearVelocity = 0.0;
		Desc.NumThreads = InitParams.MaxThreads;
		Desc.MaxBodiesHint = Params.MaxBodiesHint;
		Desc.MaxBodyPairsHint = Params.MaxBodyPairsHint;
		Desc.MaxContactConstraintsHint = Params.MaxContactConstraintsHint;
		Desc.Options = Options.empty() ? nullptr : Options.data();
		Desc.NumOptions = static_cast<uint32>(Options.size());
		Impl->Backend = FPhysicsBackendScene::Create(*Impl->Module, Desc, Error);
		if (!Impl->Backend)
		{
			UE_LOG(LogPhysScene, Fatal, "scene '%s': %s", Impl->DebugName.c_str(), Error.c_str());
		}
		Impl->SceneDesc = Desc;
		Impl->SceneDesc.DebugName = nullptr;
		Impl->SceneDesc.Options = nullptr;
		Impl->Forces.reserve(64);
		Impl->KinematicTargets.reserve(64);
	}

	FPhysScene::~FPhysScene() = default;

	FPhysicalMaterial* FPhysScene::CreatePhysicalMaterial(const FPhysicalMaterial& Template)
	{
		checkf(Impl->IsIdle(), "FPhysScene::CreatePhysicalMaterial outside the Idle phase");
		return &Impl->Materials.emplace_back(Template);
	}

	FBodySetup* FPhysScene::CreateBodySetup(FBodySetup&& Setup)
	{
		checkf(Impl->IsIdle(), "FPhysScene::CreateBodySetup outside the Idle phase");
		return &Impl->BodySetups.emplace_back(std::move(Setup));
	}

	const FPhysicalMaterial* FPhysScene::GetDefaultPhysicalMaterial() const
	{
		return &Impl->DefaultMaterial;
	}

	FBodyInstance* FPhysScene::SpawnBody(const FBodyInstance& Template, const FBodySetup* Setup, const FTransform& Transform)
	{
		checkf(Impl->IsIdle(), "FPhysScene::SpawnBody outside the Idle phase");
		checkf(Setup != nullptr, "FPhysScene::SpawnBody needs a body setup");
		checkf(Template.CollisionFilter.IsValid(), "FPhysScene::SpawnBody: collision channel %d of '%s' is not below NumCollisionChannels (%u)",
			static_cast<int32>(Template.CollisionFilter.ObjectType), Template.BodyName.c_str(), NumCollisionChannels);
		Impl->VerifyConsoleVariables("SpawnBody");   // the mass below reads p.ComNudgeAffectsInertia
		// Creation order is ActorHandle.Index (assigned by the flush); InstanceBodyIndex keeps UE's meaning (INDEX_NONE).
		FBodyInstance& Body = Impl->BodyStorage.emplace_back(Template);
		// The copy carries none of the template's registration: a template that is (or was) a body of a scene would
		// otherwise make InitBody's TermBody unregister the copy from that scene, which may be gone.
		Body.ActorHandle = FPhysicsActorHandle();
		Body.Scene = nullptr;
		Body.bDeferred = false;
		Body.State.ObjectState = EObjectStateType::Uninitialized;
		Body.SpawnedBy = this;
		Impl->Bodies.push_back(&Body);
		Body.InitBody(Setup, Transform, this);
		return &Body;
	}

	FConstraintInstance* FPhysScene::SpawnConstraint(const FConstraintInstance& Template, FBodyInstance* Body1, FBodyInstance* Body2)
	{
		checkf(Impl->IsIdle(), "FPhysScene::SpawnConstraint outside the Idle phase");
		checkf((Body1 == nullptr || Body1->SpawnedBy == this) && (Body2 == nullptr || Body2->SpawnedBy == this),
			"FPhysScene::SpawnConstraint: both bodies must be spawned by this scene");
		FConstraintInstance& Constraint = Impl->ConstraintStorage.emplace_back(Template);
		Constraint.Body1 = Body1;
		Constraint.Body2 = Body2;
		Impl->Constraints.push_back(&Constraint);
		Impl->Stats.NumConstraints = static_cast<int32>(Impl->Constraints.size());
		return &Constraint;
	}

	void FPhysScene::DeferAddActor(FBodyInstance* Body)
	{
		checkf(Impl->IsIdle(), "FPhysScene::DeferAddActor outside the Idle phase");
		checkf(Body != nullptr && Body->GetPhysicsScene() == this, "FPhysScene::DeferAddActor: the body belongs to another scene");
		checkf(Body->SpawnedBy == this, "FPhysScene::DeferAddActor: only bodies spawned by this scene (SpawnBody) can be added");
		// One FBodyInstance never gets two backend actors (M2 review): Fatal, not a checkf, which the test preset compiles out.
		if (Body->ActorHandle.IsValid() || Body->bDeferred)
		{
			UE_LOG(LogPhysScene, Fatal, "scene '%s': body '%s' is already %s; TermBody it before adding it again", Impl->DebugName.c_str(), Body->BodyName.c_str(),
				Body->ActorHandle.IsValid() ? "in the backend" : "deferred");
		}
		Body->bDeferred = true;
		Impl->DeferredBodies.push_back(Body);
	}

	void FPhysScene::RemoveBodyInstanceFromPendingLists_AssumesLocked(FBodyInstance* BodyInstance)
	{
		checkf(Impl->IsIdle(), "FPhysScene::RemoveBodyInstanceFromPendingLists_AssumesLocked outside the Idle phase");
		if (BodyInstance != nullptr && BodyInstance->bDeferred)
		{
			std::erase(Impl->DeferredBodies, BodyInstance);
			BodyInstance->bDeferred = false;
		}
	}

	void FPhysScene::FlushDeferredActors()
	{
		checkf(Impl->IsIdle(), "FPhysScene::FlushDeferredActors outside the Idle phase");
		if (Impl->DeferredBodies.empty())
		{
			return;
		}
		Impl->VerifyConsoleVariables("FlushDeferredActors");   // the marshalling reads p.SensitiveSleepThresholdMultiplier
		FMarshalledActorBatch Batch;
		std::string Error;
		if (!Impl->Backend->MarshalBodies(Impl->DeferredBodies, Impl->Backend->GetNumBodies(), Impl->Settings, Batch, Error))
		{
			UE_LOG(LogPhysScene, Fatal, "scene '%s': cannot create the deferred bodies: %s", Impl->DebugName.c_str(), Error.c_str());
		}
		Impl->CheckBackend(Impl->Backend->CreateActors(Impl->DeferredBodies, Batch), "CreateMaterials / CreateActors");
		HashBatchDescription(Impl->DescFlushes, Batch);
		HashBatchOptions(Impl->OptionsFlushes, Batch);
		++Impl->NumFlushes;
		// Initial object states as the engine has them after step 5 (put to sleep), e.g. Sleeping for !bStartAwake.
		uint32 Count = 0;
		Impl->CheckBackend(Impl->Backend->PullBodyStates(PT_READ_ALL, Count), "ReadBodyStates");
		for (FBodyInstance* Body : Impl->DeferredBodies)
		{
			Body->bDeferred = false;
			++Impl->Stats.NumBodies;
			if (Body->IsStatic())
			{
				++Impl->Stats.NumStaticBodies;
			}
			else if (Body->IsKinematic())
			{
				++Impl->Stats.NumKinematicBodies;
			}
			else
			{
				++Impl->Stats.NumDynamicBodies;
				if (!Body->bStartAwake && !Impl->TestSettings.bAllowSleeping)
				{
					++Impl->Stats.NumStartAsleepSkipped;   // 02 6 step 5 skipped by every engine (recorded in the manifest)
				}
			}
		}
		Impl->DeferredBodies.clear();
	}

	void FPhysScene::ReleaseBody(FBodyInstance* Body)
	{
		checkf(Impl->IsIdle(), "FPhysScene::ReleaseBody outside the Idle phase");
		checkf(Body != nullptr && Body->SpawnedBy == this, "FPhysScene::ReleaseBody: the body was not spawned by this scene");
		if (!Body->ActorHandle.IsValid())
		{
			return;
		}
		// UE drops what was queued for a released body: its forces and kinematic target here, its FPhysicsInterface
		// commands in the proxy (ReleaseActor).
		Impl->NumCleanForces -= static_cast<uint32>(std::count_if(Impl->Forces.begin(), Impl->Forces.end(),
			[Body](const FPendingForce& Force) { return Force.Body == Body && Force.bClean; }));
		std::erase_if(Impl->Forces, [Body](const FPendingForce& Force) { return Force.Body == Body; });
		std::erase_if(Impl->KinematicTargets, [Body](const FPendingKinematicTarget& Target) { return Target.Body == Body; });
		Impl->CheckBackend(Impl->Backend->ReleaseActor(*Body), "ReleaseActors");
		--Impl->Stats.NumBodies;
		if (Body->IsStatic())
		{
			--Impl->Stats.NumStaticBodies;
		}
		else if (Body->IsKinematic())
		{
			--Impl->Stats.NumKinematicBodies;
		}
		else
		{
			--Impl->Stats.NumDynamicBodies;
		}
		Body->ActorHandle = FPhysicsActorHandle();
		Body->State.ObjectState = EObjectStateType::Uninitialized;
	}

	void FPhysScene::PrepareForSimulation()
	{
		checkf(Impl->IsIdle(), "FPhysScene::PrepareForSimulation outside the Idle phase");
		checkf(Impl->DeferredBodies.empty(), "FPhysScene::PrepareForSimulation with %d deferred bodies: call FlushDeferredActors first",
			static_cast<int32>(Impl->DeferredBodies.size()));
		std::string Actions;
		Impl->CheckBackend(Impl->Backend->PrepareForSimulation(Actions), "PrepareForSimulation");
		UE_LOG(LogPhysScene, Log, "scene '%s': PrepareForSimulation (%s): %s", Impl->DebugName.c_str(), Impl->BackendName.c_str(), Actions.c_str());
	}

	void FPhysScene::Tick(const float DeltaSeconds)
	{
		// UWorld::Tick physics part (LevelTick.cpp:1740-1778, PhysLevel.cpp:194-212 [V]): SetUpForFrame first, then
		// StartFrame, which broadcasts OnPhysScenePreTick (UE5 FPhysScene_Chaos::OnStartFrame).
		const FSetUpForFrameParams Params = FStepPlanner::MakeSetUpForFrameParams(DeltaSeconds, Impl->Settings, Impl->TestSettings);
		const FVector Gravity(0.0, 0.0, Impl->Settings.DefaultGravityZ);
		SetUpForFrame(&Gravity, Params.DeltaSeconds, Params.MinPhysicsDeltaTime, Params.MaxPhysicsDeltaTime, Params.MaxSubstepDeltaTime,
			Params.MaxSubsteps, Params.bSubstepping);
		StartFrame();
		WaitPhysScenes();
		EndFrame();
	}

	void FPhysScene::SetUpForFrame(const FVector* NewGrav, const float InDeltaSeconds, const float InMinPhysicsDeltaTime,
		const float InMaxPhysicsDeltaTime, const float InMaxSubstepDeltaTime, const int32 InMaxSubsteps, const bool bSubstepping)
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Idle, "FPhysScene::SetUpForFrame outside the Idle phase");
		checkf(Impl->DeferredBodies.empty(), "FPhysScene::SetUpForFrame with deferred bodies: call FlushDeferredActors first");
		if (NewGrav != nullptr)
		{
			Impl->Gravity = *NewGrav;
		}
		FSetUpForFrameParams Params;
		Params.DeltaSeconds = InDeltaSeconds;
		Params.MinPhysicsDeltaTime = InMinPhysicsDeltaTime;
		Params.MaxPhysicsDeltaTime = InMaxPhysicsDeltaTime;
		Params.MaxSubstepDeltaTime = InMaxSubstepDeltaTime;
		Params.MaxSubsteps = InMaxSubsteps;
		Params.bSubstepping = bSubstepping;
		Impl->LastPlan = FStepPlanner::PlanFrame(Params, Impl->AsyncDeltaTime, Impl->TestSettings.SubstepClampRule, Impl->AccumulatedTime);
		Impl->Timing = FFrameTiming();
		Impl->Phase = EPhysSceneFramePhase::SetUp;
	}

	void FPhysScene::StartFrame()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::SetUp, "FPhysScene::StartFrame without SetUpForFrame");
		const FStepPlan& Plan = Impl->LastPlan;
		// UE5: FChaosScene::StartFrame -> FPhysScene_Chaos::OnStartFrame(MDeltaTime) broadcasts OnPhysScenePreTick with
		// the clamped frame dt before the solver advances (ChaosScene.cpp:363-374, PhysScene_Chaos.cpp:2272-2316 [V]).
		// The phase is still SetUp here: per-frame work issued from PreTick applies to this frame's steps.
		if (OnPhysScenePreTick)
		{
			OnPhysScenePreTick(this, Plan.FrameDeltaTime);
		}
		Impl->bIsSimulating = true;
		Impl->Phase = EPhysSceneFramePhase::Started;
		const uint32 NumSteps = static_cast<uint32>(Plan.NumSteps > 0 ? Plan.NumSteps : 0);
		if (Impl->Driver == EFrameDriver::Native)
		{
			// UE5 broadcasts OnPhysSceneStep once per frame right after PreTick, with the frame dt (PhysScene_Chaos.cpp:2272-2316).
			if (OnPhysSceneStep)
			{
				OnPhysSceneStep(this, Plan.FrameDeltaTime);
			}
			Impl->PushFrameCommands(0, 1);
			// The original UE arguments, once; the backend plans and steps (NATIVE_FRAME_MODEL).
			Impl->RunBackendFrame(Impl->Gravity, Plan.Params, NumSteps, static_cast<uint32>(Plan.NumPushedSteps > 0 ? Plan.NumPushedSteps : 0));
			for (uint32 Step = 0; Step < NumSteps; ++Step)
			{
				Impl->SimTime += Plan.InternalStepDeltaTime;
			}
			Impl->StepCount += NumSteps;
			return;
		}
		// Harness driver: one engine step per planned step.
		const bool bUE4Rule = Plan.ClampRule == ESubstepClampRule::UE4PhysSubstep;
		if (!bUE4Rule)
		{
			// UE5 force averaging (PhysicsSolverBase.cpp:623-646 [V]): a frame with dt > 0 counts; the frame that pushes
			// steps applies 1 / count and resets it. Frames that push nothing keep the forces (EndFrame).
			if (Plan.FrameDeltaTime > 0.0f)
			{
				++Impl->ExternalSteps;
			}
			if (Plan.NumPushedSteps > 0)
			{
				Impl->ForceWeight = 1.0 / static_cast<double>(Impl->ExternalSteps > 1u ? Impl->ExternalSteps : 1u);
				Impl->ExternalSteps = 0;
			}
			// UE5 broadcasts OnPhysSceneStep once per frame (PhysScene_Chaos.cpp:2311-2312 [V]), as the Native driver does:
			// a force added there acts on the frame's steps like one added in PreTick.
			if (OnPhysSceneStep)
			{
				OnPhysSceneStep(this, Plan.FrameDeltaTime);
			}
		}
		if (NumSteps == 0)
		{
			Impl->FlushQueueOnly();   // the frame's writes reach the backend now (Native pushes every frame too)
		}
		FSetUpForFrameParams StepParams;
		StepParams.DeltaSeconds = Plan.StepDeltaTime;
		StepParams.MinPhysicsDeltaTime = 0.0f;
		StepParams.MaxPhysicsDeltaTime = 0.0f;
		StepParams.MaxSubstepDeltaTime = 0.0f;
		StepParams.MaxSubsteps = 1;
		StepParams.bSubstepping = false;
		const double StepDtEngine = Analytic::EngineDeltaTime(Plan.StepDeltaTime);
		for (uint32 Step = 0; Step < NumSteps; ++Step)
		{
			Impl->CurrentStep = Step;
			// UE4 PhysX substepping broadcast OnPhysSceneStep before every substep with the substep dt [M].
			if (bUE4Rule && OnPhysSceneStep)
			{
				OnPhysSceneStep(this, Plan.StepDeltaTime);
			}
			Impl->PushFrameCommands(Step, NumSteps);
			Impl->RunBackendFrame(Impl->Gravity, StepParams, 1u, 1u);
			++Impl->StepCount;
			Impl->SimTime += StepDtEngine;
		}
		Impl->CurrentStep = 0;
	}

	void FPhysScene::WaitPhysScenes()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Started, "FPhysScene::WaitPhysScenes without StartFrame");
		Impl->Phase = EPhysSceneFramePhase::Waited;   // the backend's WaitPhysScenes ran inside the driver; kept for API parity
	}

	void FPhysScene::EndFrame()
	{
		checkf(Impl->Phase == EPhysSceneFramePhase::Waited, "FPhysScene::EndFrame without WaitPhysScenes");
		Impl->Phase = EPhysSceneFramePhase::Idle;
		Impl->bIsSimulating = false;
		// [T_pull] once per frame: the bodies whose state may have changed -> FBodyInstance::State.
		const uint64 PullBegin = FPlatformTime::Cycles64();
		uint32 Count = 0;
		const PtResult PullResult = Impl->Backend->PullBodyStates(PT_READ_ACTIVE_ONLY, Count);
		const uint64 PullEnd = FPlatformTime::Cycles64();
		Impl->CheckBackend(PullResult, "ReadBodyStates");
		// Forces live until a frame pushes steps (both drivers; the Harness driver's UE4 rule keeps them for one frame).
		// Kinematic targets: Native, the backend holds what it was sent; Harness, until a step runs (PtBackendAbi.h body
		// commands).
		const FStepPlan& Plan = Impl->LastPlan;
		const bool bNative = Impl->Driver == EFrameDriver::Native;
		if (Plan.ClampRule == ESubstepClampRule::UE4PhysSubstep || Plan.NumPushedSteps > 0)
		{
			Impl->Forces.clear();
			Impl->NumCleanForces = 0;
		}
		if (bNative || Plan.NumSteps > 0)
		{
			Impl->KinematicTargets.clear();
		}

		FFrameTiming& Timing = Impl->Timing;
		Timing.PullNs = TicksToNs(PullEnd - PullBegin);
		Timing.FrameQpcTicks += PullEnd - PullBegin;
		Timing.FrameNs = Timing.PushNs + Timing.StepNs + Timing.PullNs;   // T_frame = sum(T_push) + sum(T_step) + T_pull
		Timing.NumSteps = static_cast<uint32>(Impl->LastPlan.NumSteps > 0 ? Impl->LastPlan.NumSteps : 0);
		Impl->LastTiming = Timing;
		Impl->Stats.Last = Timing;
		int32 Awake = 0;
		for (const FBodyInstance* Body : Impl->Bodies)
		{
			// Simulated bodies that are not asleep (kinematic and static bodies are never counted).
			Awake += Body->State.ObjectState == EObjectStateType::Dynamic ? 1 : 0;
		}
		Impl->Stats.NumAwakeBodies = Awake;
		++Impl->FrameCount;
		if (OnPhysScenePostTick)
		{
			// UE5 broadcasts PostTick without a dt (ChaosScene.cpp:576 [V]); the harness passes the same frame dt as PreTick.
			OnPhysScenePostTick(this, Impl->LastPlan.FrameDeltaTime);
		}
	}

	bool FPhysScene::IsSimulating() const
	{
		return Impl->bIsSimulating;
	}

	EPhysSceneFramePhase FPhysScene::GetFramePhase() const
	{
		return Impl->Phase;
	}

	void FPhysScene::AddForce_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, const bool bAllowSubstepping, const bool bAccelChange)
	{
		checkf(BodyInstance != nullptr && BodyInstance->SpawnedBy == this && BodyInstance->ActorHandle.IsValid(),
			"FPhysScene::AddForce_AssumesLocked: not a live body of this scene");
		Impl->AddForce(BodyInstance, Force, bAllowSubstepping, bAccelChange, false);
	}

	void FPhysScene::AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, const bool bAllowSubstepping, const bool bAccelChange)
	{
		checkf(BodyInstance != nullptr && BodyInstance->SpawnedBy == this && BodyInstance->ActorHandle.IsValid(),
			"FPhysScene::AddTorque_AssumesLocked: not a live body of this scene");
		Impl->AddForce(BodyInstance, Torque, bAllowSubstepping, bAccelChange, true);
	}

	void FPhysScene::SetKinematicTarget_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& TargetTM, const bool bAllowSubstepping)
	{
		checkf(BodyInstance != nullptr && BodyInstance->SpawnedBy == this && BodyInstance->ActorHandle.IsValid() && BodyInstance->IsKinematic(),
			"FPhysScene::SetKinematicTarget_AssumesLocked: not a live kinematic body of this scene");
		FPendingKinematicTarget& Target = Impl->FindOrAddKinematicTarget(BodyInstance);
		Target.Target = FTransform(TargetTM.GetRotation(), TargetTM.GetTranslation());   // the last target of the frame wins
		Target.bAllowSubstepping = bAllowSubstepping;
	}

	void FPhysScene::TeleportKinematic_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& Pose)
	{
		checkf(BodyInstance != nullptr && BodyInstance->SpawnedBy == this && BodyInstance->ActorHandle.IsValid() && BodyInstance->IsKinematic(),
			"FPhysScene::TeleportKinematic_AssumesLocked: not a live kinematic body of this scene");
		FPendingKinematicTarget& Target = Impl->FindOrAddKinematicTarget(BodyInstance);
		const FTransform RigidPose(Pose.GetRotation(), Pose.GetTranslation());
		Target.Target = RigidPose;
		Target.Start = RigidPose;
		Target.Current = RigidPose;
		Target.bStartIsTeleport = true;
	}

	void FPhysScene::MarkDynamicsClean_AssumesLocked(const FBodyInstance* BodyInstance)
	{
		// A scene without sleeping ignores PUT_TO_SLEEP (docs/Abi.md 5.10): the body stays awake and keeps its forces.
		if (Impl->TestSettings.bAllowSleeping && BodyInstance != nullptr && BodyInstance->IsDynamic())
		{
			Impl->SetForcesClean(BodyInstance, true);
		}
	}

	void FPhysScene::MarkDynamicsDirty_AssumesLocked(const FBodyInstance* BodyInstance)
	{
		Impl->SetForcesClean(BodyInstance, false);
	}

	std::span<FBodyInstance* const> FPhysScene::GetBodies() const
	{
		return Impl->Bodies;
	}

	std::span<FConstraintInstance* const> FPhysScene::GetConstraints() const
	{
		return Impl->Constraints;
	}

	int32 FPhysScene::GetNumDeferredActors() const
	{
		return static_cast<int32>(Impl->DeferredBodies.size());
	}

	const FPhysicsSettings& FPhysScene::GetSettings() const
	{
		return Impl->Settings;
	}

	const FPhysicsTestSettings& FPhysScene::GetTestSettings() const
	{
		return Impl->TestSettings;
	}

	FVector FPhysScene::GetGravity() const
	{
		return Impl->Gravity;
	}

	double FPhysScene::GetSimTime() const
	{
		return Impl->SimTime;
	}

	uint64 FPhysScene::GetFrameCount() const
	{
		return Impl->FrameCount;
	}

	uint64 FPhysScene::GetStepCount() const
	{
		return Impl->StepCount;
	}

	const FPhysSceneStats& FPhysScene::GetStats() const
	{
		return Impl->Stats;
	}

	const FFrameTiming& FPhysScene::GetLastFrameTiming() const
	{
		return Impl->LastTiming;
	}

	const FStepPlan& FPhysScene::GetLastPlan() const
	{
		return Impl->LastPlan;
	}

	const std::string& FPhysScene::GetDebugName() const
	{
		return Impl->DebugName;
	}

	const std::string& FPhysScene::GetBackendName() const
	{
		return Impl->BackendName;
	}

	EFrameDriver FPhysScene::GetFrameDriver() const
	{
		return Impl->Driver;
	}

	FPhysicsBackendScene& FPhysScene::GetBackendScene() const
	{
		return *Impl->Backend;
	}

	const FBackendInfo& FPhysScene::GetBackendInfo() const
	{
		return Impl->Module->GetInfo();
	}

	uint64 FPhysScene::ComputeNativeStateHash() const
	{
		checkf(Impl->IsIdle(), "FPhysScene::ComputeNativeStateHash outside the Idle phase");
		uint64 Hash = 0;
		Impl->CheckBackend(Impl->Backend->ComputeNativeStateHash(Hash), "ComputeNativeStateHash");
		return Hash;
	}

	std::string FPhysScene::DumpEffectiveSettings() const
	{
		checkf(Impl->IsIdle(), "FPhysScene::DumpEffectiveSettings outside the Idle phase");
		std::string Lines;
		Impl->CheckBackend(Impl->Backend->DumpEffectiveSettings(Lines), "DumpEffectiveSettings");
		return Lines;
	}

	bool FPhysScene::CaptureDebugSnapshot(FPhysicsDebugSnapshot& Out, const uint32 SnapshotFlags) const
	{
		checkf(Impl->IsIdle(), "FPhysScene::CaptureDebugSnapshot outside the Idle phase");
		const PtResult Result = Impl->Backend->CaptureDebugSnapshot(SnapshotFlags, Out);
		if (Result == PT_E_UNSUPPORTED)
		{
			Out.Reset();
			return false;
		}
		Impl->CheckBackend(Result, "CaptureDebugSnapshot / ReadDebugSnapshot");
		Out.FrameIndex = Impl->FrameCount;
		Out.SimTime = Impl->SimTime;
		return true;
	}

	std::string FPhysScene::ComputeDescSha256(const std::span<const float> FrameDeltaSeconds) const
	{
		checkf(Impl->DeferredBodies.empty(), "FPhysScene::ComputeDescSha256 with deferred bodies: call FlushDeferredActors first");
		FHashWriter W = Impl->DescFlushes;   // the flushes so far, then the rest
		W.Tag("physicstest.desc/1");
		W.U32(Impl->NumFlushes);
		// Constraints (data only until M9): bodies by creation order, frames, profile.
		const auto BodyOrder = [this](const FBodyInstance* Body) -> uint32
		{
			const auto Found = std::find(Impl->Bodies.begin(), Impl->Bodies.end(), Body);
			return Body == nullptr || Found == Impl->Bodies.end() ? PT_INDEX_NONE : static_cast<uint32>(Found - Impl->Bodies.begin());
		};
		W.Tag("constraints");
		W.U32(static_cast<uint32>(Impl->Constraints.size()));
		for (const FConstraintInstance* C : Impl->Constraints)
		{
			W.U32(BodyOrder(C->Body1));
			W.U32(BodyOrder(C->Body2));
			W.Vec(C->Pos1);
			W.Vec(C->PriAxis1);
			W.Vec(C->SecAxis1);
			W.Vec(C->Pos2);
			W.Vec(C->PriAxis2);
			W.Vec(C->SecAxis2);
			const FConstraintProfileProperties& P = C->ProfileInstance;
			W.F32(P.LinearLimit.Limit);
			W.U32(static_cast<uint32>(P.LinearLimit.XMotion) | (static_cast<uint32>(P.LinearLimit.YMotion) << 8) | (static_cast<uint32>(P.LinearLimit.ZMotion) << 16));
			W.F32(P.ConeLimit.Swing1LimitDegrees);
			W.F32(P.ConeLimit.Swing2LimitDegrees);
			W.F32(P.TwistLimit.TwistLimitDegrees);
			W.U32(static_cast<uint32>(P.ConeLimit.Swing1Motion) | (static_cast<uint32>(P.ConeLimit.Swing2Motion) << 8)
				| (static_cast<uint32>(P.TwistLimit.TwistMotion) << 16));
			W.U32(P.bDisableCollision ? 1u : 0u);
		}
		// Gravity and the dt sequence: NominalDeltaTime and the harness plan of every frame (from a fresh accumulator).
		W.Tag("gravity");
		W.Vec(FVector(0.0, 0.0, Impl->Settings.DefaultGravityZ));
		W.Tag("dt");
		W.F64(Impl->SceneDesc.NominalDeltaTime);
		W.U32(static_cast<uint32>(Impl->TestSettings.TickMode));
		W.U32(static_cast<uint32>(FrameDeltaSeconds.size()));
		double Accumulator = 0.0;
		for (const float FrameDt : FrameDeltaSeconds)
		{
			const FSetUpForFrameParams Params = FStepPlanner::MakeSetUpForFrameParams(FrameDt, Impl->Settings, Impl->TestSettings);
			const FStepPlan Plan = FStepPlanner::PlanFrame(Params, Impl->AsyncDeltaTime, Impl->TestSettings.SubstepClampRule, Accumulator);
			W.I32(Plan.NumSteps);
			W.F32(Plan.StepDeltaTime);
		}
		return W.Sha.Final().ToString();
	}

	std::string FPhysScene::ComputeOptionsSha256() const
	{
		FHashWriter W = Impl->OptionsFlushes;
		W.Tag("physicstest.options/1");
		const PtSceneDesc& D = Impl->SceneDesc;
		W.U32(D.RunMode);
		W.U32(D.InstrumentFlags);
		W.U32(D.SimFlags);
		W.F64(D.AsyncFixedTimeStepSize);
		W.F64(D.RestitutionThreshold);
		W.F64(D.MaxLinearVelocity);
		W.I32(D.PositionIterations);
		W.I32(D.VelocityIterations);
		W.I32(D.ProjectionIterations);
		W.U32(D.NumThreads);
		W.U32(D.MaxBodiesHint);
		W.U32(D.MaxBodyPairsHint);
		W.U32(D.MaxContactConstraintsHint);
		W.U32(static_cast<uint32>(Impl->Driver));
		W.U32(static_cast<uint32>(Impl->SceneOptions.size()));
		for (const auto& [Key, Value] : Impl->SceneOptions)
		{
			W.Str(Key);
			W.Str(Value);
		}
		return W.Sha.Final().ToString();
	}

	const PtSceneDesc& FPhysScene::GetSceneDesc() const
	{
		return Impl->SceneDesc;
	}

	const std::vector<std::pair<std::string, std::string>>& FPhysScene::GetSceneOptions() const
	{
		return Impl->SceneOptions;
	}
}
