// NullBackend.cpp - FNullFramePlanner, FNullScene, FNullBackend and the static entry PT_GetBackendApi_Null (A.13).
// UE behaviour mirrored here is cited inline; the code is our own (clean room, no harness code: R4).
#include "NullBackend.h"

#include "PtBuildReport.h"
#include "PtContactConvention.h"
#include "PtDamping.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace PhysicsTest::NullBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		PT_BUILD_REPORT_JSON(GNullBuildReport, "\"backend\":\"Null\",");

		// UE_SMALL_NUMBER (1.e-8f): FPhysicsSolverBase::SetMinDeltaTime_External floors MinDeltaTime with it
		// (PhysicsSolverBase.h:727-730 [V]).
		constexpr float GSmallNumber = 1.e-8f;

		// ---- small vector / quaternion helpers (Chaos FVec3 / FRotation3 conventions, Hamilton product) -----------
		PtVec3 Add(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
		PtVec3 Sub(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
		PtVec3 Scale(const PtVec3& A, const double S) { return PtVec3{ A.X * S, A.Y * S, A.Z * S }; }
		PtVec3 Mul(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.X * B.X, A.Y * B.Y, A.Z * B.Z }; }
		double Dot(const PtVec3& A, const PtVec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
		PtVec3 Cross(const PtVec3& A, const PtVec3& B) { return PtVec3{ A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
		bool IsZero(const PtVec3& A) { return A.X == 0.0 && A.Y == 0.0 && A.Z == 0.0; }
		const PtVec3 GZero = { 0.0, 0.0, 0.0 };

		PtQuat QuatMul(const PtQuat& A, const PtQuat& B)
		{
			return PtQuat{
				A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
				A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
				A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
				A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z };
		}
		PtQuat QuatConj(const PtQuat& Q) { return PtQuat{ -Q.X, -Q.Y, -Q.Z, Q.W }; }
		PtQuat QuatNormalize(const PtQuat& Q)
		{
			const double SizeSquared = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
			if (!(SizeSquared > 0.0))
			{
				return PtQuat{ 0.0, 0.0, 0.0, 1.0 };
			}
			const double InvSize = 1.0 / sqrt(SizeSquared);
			return PtQuat{ Q.X * InvSize, Q.Y * InvSize, Q.Z * InvSize, Q.W * InvSize };
		}
		// v' = v + w t + u x t, t = 2 u x v (u = the vector part).
		PtVec3 Rotate(const PtQuat& Q, const PtVec3& V)
		{
			const PtVec3 U = { Q.X, Q.Y, Q.Z };
			const PtVec3 T = Scale(Cross(U, V), 2.0);
			return Add(Add(V, Scale(T, Q.W)), Cross(U, T));
		}
		PtVec3 UnrotateVector(const PtQuat& Q, const PtVec3& V) { return Rotate(QuatConj(Q), V); }

		// Chaos TRotation::IntegrateRotationWithAngularVelocity (ChaosCore Rotation.cpp:158-162 [V]):
		// q1 = normalize(q0 + (w, 0) q0 * dt / 2).
		PtQuat IntegrateRotation(const PtQuat& Q0, const PtVec3& W, const double Dt)
		{
			const PtQuat Spin = QuatMul(PtQuat{ W.X, W.Y, W.Z, 0.0 }, Q0);
			const double Half = Dt * 0.5;
			return QuatNormalize(PtQuat{ Q0.X + Spin.X * Half, Q0.Y + Spin.Y * Half, Q0.Z + Spin.Z * Half, Q0.W + Spin.W * Half });
		}

		// World-space inverse inertia applied to V: R diag(InvI) R^T V, R = the principal frame Q.
		PtVec3 ApplyInvInertia(const FNullBody& Body, const PtVec3& V) { return Rotate(Body.Q, Mul(Body.InvInertia, UnrotateVector(Body.Q, V))); }
		PtVec3 ApplyInertia(const FNullBody& Body, const PtVec3& V) { return Rotate(Body.Q, Mul(Body.InertiaDiagonal, UnrotateVector(Body.Q, V))); }

		// Actor pose <-> COM frame (ComLocal is the COM frame in actor space).
		void SetActorPose(FNullBody& Body, const PtTransform& Actor)
		{
			Body.Q = QuatMul(Actor.Rotation, Body.ComLocal.Rotation);
			Body.P = Add(Actor.Translation, Rotate(Actor.Rotation, Body.ComLocal.Translation));
		}
		PtTransform GetActorPose(const FNullBody& Body)
		{
			PtTransform Actor;
			Actor.Rotation = QuatMul(Body.Q, QuatConj(Body.ComLocal.Rotation));
			Actor.Translation = Sub(Body.P, Rotate(Actor.Rotation, Body.ComLocal.Translation));
			return Actor;
		}

		// Spherical interpolation on the shorter arc (Shoemake 1985): with c = <From, To> and To' = sign(c) To,
		//   q(t) = (sin((1 - t) theta) From + sin(t theta) To') / sin(theta),  theta = acos |c|,
		// falling back to the linear weights (1 - t, t) when the two rotations are within |c| >= 1 - 1e-4, then normalised.
		// The behavioural reference is UE's FQuat::Slerp (UnrealMath.cpp:1226-1255, Quat.h:655-660 [V]: shortest arc, the
		// same linear band). Chaos interpolates kinematic rotations with FRotation3f::Slerp (float); Null in double
		// (docs/Progress.md M2).
		PtQuat Slerp(const PtQuat& From, const PtQuat& To, const double Alpha)
		{
			const double Dot4 = From.X * To.X + From.Y * To.Y + From.Z * To.Z + From.W * To.W;
			const double CosTheta = Dot4 < 0.0 ? -Dot4 : Dot4;
			const double ToSign = Dot4 < 0.0 ? -1.0 : 1.0;
			double WeightFrom = 1.0 - Alpha;
			double WeightTo = Alpha;
			if (CosTheta < 1.0 - 1.0e-4)
			{
				const double Theta = acos(CosTheta);
				const double SinTheta = sin(Theta);
				WeightFrom = sin(WeightFrom * Theta) / SinTheta;
				WeightTo = sin(WeightTo * Theta) / SinTheta;
			}
			WeightTo *= ToSign;
			return QuatNormalize(PtQuat{ WeightFrom * From.X + WeightTo * To.X, WeightFrom * From.Y + WeightTo * To.Y, WeightFrom * From.Z + WeightTo * To.Z,
				WeightFrom * From.W + WeightTo * To.W });
		}
		// FMath::Lerp: A + Alpha * (B - A).
		PtVec3 Lerp(const PtVec3& A, const PtVec3& B, const double Alpha) { return Add(A, Scale(Sub(B, A), Alpha)); }
		// Angular velocity that rotates From into To in Dt (shortest arc).
		PtVec3 AngularVelocityBetween(const PtQuat& From, const PtQuat& To, const double Dt)
		{
			PtQuat Delta = QuatMul(To, QuatConj(From));
			if (Delta.W < 0.0)
			{
				Delta = PtQuat{ -Delta.X, -Delta.Y, -Delta.Z, -Delta.W };
			}
			const PtVec3 Axis = { Delta.X, Delta.Y, Delta.Z };
			const double SinHalf = sqrt(Dot(Axis, Axis));
			if (!(SinHalf > 0.0) || !(Dt > 0.0))
			{
				return GZero;
			}
			const double Angle = 2.0 * atan2(SinHalf, Delta.W);
			return Scale(Axis, Angle / (SinHalf * Dt));
		}

		void ClampSpeed(PtVec3& V, const double MaxSpeed)
		{
			if (MaxSpeed > 0.0)
			{
				// Chaos clamps on the squared speed (PBDRigidsEvolutionGBF.cpp:944-954 [V]).
				const double SpeedSquared = Dot(V, V);
				const double MaxSquared = MaxSpeed * MaxSpeed;
				if (SpeedSquared > MaxSquared)
				{
					V = Scale(V, sqrt(MaxSquared / SpeedSquared));
				}
			}
		}

		bool IsDynamic(const FNullBody& Body) { return Body.Motion == PT_MOTION_DYNAMIC; }

		void Wake(FNullBody& Body)
		{
			if (IsDynamic(Body) && Body.ObjectState == PT_OBJECT_STATE_SLEEPING)
			{
				Body.ObjectState = PT_OBJECT_STATE_DYNAMIC;
				Body.bDirty = true;
			}
		}

		void FormatVec(char (&Out)[128], const PtVec3& V)
		{
			snprintf(Out, sizeof(Out), "(%.17g, %.17g, %.17g)", V.X, V.Y, V.Z);
		}

		const char* GetBuildConfig()
		{
#if defined(_DEBUG)
			return "Debug";
#else
			return "Release";
#endif
		}
	}

	// ---- TNullArray --------------------------------------------------------------------------------------------------
	template <class T>
	bool TNullArray<T>::Reserve(const uint32 NewCapacity)
	{
		if (NewCapacity <= Capacity)
		{
			return true;
		}
		T* NewData = static_cast<T*>(realloc(Data, static_cast<size_t>(NewCapacity) * sizeof(T)));
		if (NewData == nullptr)
		{
			return false;
		}
		if (Counter != nullptr)
		{
			Counter->LiveBytes += static_cast<int64>(NewCapacity - Capacity) * static_cast<int64>(sizeof(T));
			Counter->PeakBytes = Counter->LiveBytes > Counter->PeakBytes ? Counter->LiveBytes : Counter->PeakBytes;
			++Counter->NumAllocs;
			if (Data != nullptr)
			{
				++Counter->NumFrees;
			}
		}
		Data = NewData;
		Capacity = NewCapacity;
		return true;
	}

	template <class T>
	void TNullArray<T>::Reset()
	{
		if (Data != nullptr)
		{
			free(Data);
			if (Counter != nullptr)
			{
				Counter->LiveBytes -= static_cast<int64>(Capacity) * static_cast<int64>(sizeof(T));
				++Counter->NumFrees;
			}
		}
		Data = nullptr;
		Count = 0;
		Capacity = 0;
	}

	// ---- FNullFramePlanner -------------------------------------------------------------------------------------------
	FNullStepPlan FNullFramePlanner::Plan(const Abi::FFrameParams& Params, const double AsyncDt, double& InOutAccumulatedTime)
	{
		// FChaosScene::SetUpForFrame (ChaosScene.cpp:331-361 [V]), float; MNetworkDeltaTimeScale = 1.
		const float InDeltaSeconds = Params.DeltaSeconds;
		float MDeltaTime = InDeltaSeconds;
		if (Params.bSubstepping)
		{
			if (Params.MaxSubstepDeltaTime > 0.0f)
			{
				const float Limit = static_cast<float>(Params.MaxSubsteps) * Params.MaxSubstepDeltaTime;
				MDeltaTime = InDeltaSeconds < Limit ? InDeltaSeconds : Limit;
			}
		}
		else if (Params.MaxDeltaTime > 0.0f)
		{
			MDeltaTime = InDeltaSeconds < Params.MaxDeltaTime ? InDeltaSeconds : Params.MaxDeltaTime;
		}
		// The solver settings SetUpForFrame pushes: SetMaxDeltaTime / SetMaxSubSteps / SetMinDeltaTime.
		const float SolverMaxDeltaTime = Params.bSubstepping ? Params.MaxSubstepDeltaTime : Params.MaxDeltaTime;
		const int32 SolverMaxSubSteps = Params.bSubstepping ? Params.MaxSubsteps : 1;
		const float SolverMinDeltaTime = Params.MinDeltaTime > GSmallNumber ? Params.MinDeltaTime : GSmallNumber;

		// FPhysicsSolverBase::AdvanceAndDispatch_External (PhysicsSolverBase.cpp:581-621 [V]), FReal = double.
		const bool bSolverSubstepped = SolverMaxSubSteps > 1;   // SetSolverSubstep_External(MMaxSubSteps > 1)
		const double Dt = MDeltaTime;
		double InternalDt = Dt;
		int32 PushedSteps = 1;
		if (AsyncDt >= 0.0)
		{
			// IsUsingFixedDt(): async results with p.UseAsyncInterpolation = 1 (the default).
			InOutAccumulatedTime += Dt;
			if (Dt == 0.0)
			{
				// The flush case: whatever time is left, in one step.
				InternalDt = InOutAccumulatedTime;
				PushedSteps = 1;
				InOutAccumulatedTime = 0.0;
			}
			else
			{
				InternalDt = AsyncDt;
				PushedSteps = static_cast<int32>(floor(InOutAccumulatedTime / InternalDt));
				InOutAccumulatedTime -= InternalDt * static_cast<double>(PushedSteps);
			}
		}
		else if (bSolverSubstepped && Dt > 0.0 && SolverMaxDeltaTime > 0.0f)
		{
			// (UE divides by a zero MaxSubstepDeltaTime; Null then keeps one step of dt.)
			PushedSteps = static_cast<int32>(ceil(Dt / static_cast<double>(SolverMaxDeltaTime)));
			if (PushedSteps > SolverMaxSubSteps)
			{
				PushedSteps = SolverMaxSubSteps;   // time is lost
				InternalDt = SolverMaxDeltaTime;
			}
			else
			{
				InternalDt = Dt / static_cast<double>(PushedSteps);
			}
		}

		// Each pushed step through the solver's advance loop (PBDRigidsSolver.cpp:560-575 [V]): steps only while the
		// remaining time exceeds MinDeltaTime; the rest is dropped, never carried.
		FNullStepPlan Plan;
		Plan.FrameDeltaTime = Dt;
		Plan.bSolverSubstepped = bSolverSubstepped;
		Plan.NumPushedSteps = PushedSteps > 0 ? static_cast<uint32>(PushedSteps) : 0u;
		for (uint32 Pushed = 0; Pushed < Plan.NumPushedSteps; ++Pushed)
		{
			// At most SolverStepBudget solver steps of at most StepCap each, while more than StepFloor of the interval is left.
			int32 SolverStepBudget = bSolverSubstepped ? 1 : SolverMaxSubSteps;
			double IntervalLeft = InternalDt;
			const double StepFloor = SolverMinDeltaTime;
			const double StepCap = SolverMaxDeltaTime;
			for (; SolverStepBudget > 0 && IntervalLeft > StepFloor; --SolverStepBudget)
			{
				const double SolverDt = StepCap > 0.0 && StepCap < IntervalLeft ? StepCap : IntervalLeft;
				IntervalLeft -= SolverDt;
				if (Plan.NumSteps == 0)
				{
					Plan.StepDeltaTime = SolverDt;
				}
				++Plan.NumSteps;
			}
		}
		return Plan;
	}

	// ---- FNullScene --------------------------------------------------------------------------------------------------
	FNullScene::FNullScene(const PtSceneDesc& Desc)
		: Gravity(Desc.Gravity)
		, NominalDeltaTime(Desc.NominalDeltaTime)
		, AsyncDeltaTime(Desc.AsyncFixedTimeStepSize)
		, SimFlags(Desc.SimFlags)
		, MaxBodiesHint(Desc.MaxBodiesHint)
	{
		Bodies.SetCounter(&Memory);
		Materials.SetCounter(&Memory);
		if (Desc.MaxBodiesHint > 0)
		{
			Bodies.Reserve(Desc.MaxBodiesHint);
		}
	}

	PtResult FNullScene::CreateMaterials(const Abi::TStridedView<PtPhysicalMaterial> InMaterials)
	{
		if (!Materials.Reserve(Materials.Num() + InMaterials.Num()))
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "Null: cannot allocate %u materials", InMaterials.Num());
		}
		for (uint32 Index = 0; Index < InMaterials.Num(); ++Index)
		{
			Materials.Add(InMaterials[Index]);   // no collision: kept for the effective settings only
		}
		return PT_OK;
	}

	PtResult FNullScene::CreateActors(const Abi::FActorBatchView& Batch)
	{
		const uint32 First = Bodies.Num();
		if (!Bodies.Reserve(First + Batch.Actors.Num()))
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "Null: cannot allocate %u bodies", Batch.Actors.Num());
		}
		// UE order (A.1 CreateActors, 02 6): 1. per body CreateActor with the creation-time flags, 2. geometry / filter /
		// material (Null keeps no shapes), 3. one AddActorsToScene, 4. per body InitDynamicProperties, 5. put to sleep.
		for (uint32 Index = 0; Index < Batch.Actors.Num(); ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			FNullBody Body;
			Body.Motion = Info.Motion;
			Body.Flags = Info.Flags;
			Body.ComLocal = Info.CenterOfMassLocal;
			Body.ObjectState = Info.Motion == PT_MOTION_STATIC ? PT_OBJECT_STATE_STATIC
				: (Info.Motion == PT_MOTION_KINEMATIC ? PT_OBJECT_STATE_KINEMATIC : PT_OBJECT_STATE_DYNAMIC);
			SetActorPose(Body, Info.ActorTransform);
			Bodies.Add(Body);
		}
		// (3: AddActorsToScene - nothing to do for Null.)
		for (uint32 Index = 0; Index < Batch.Actors.Num(); ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			FNullBody& Body = Bodies[First + Index];
			// 4. InitDynamicProperties order (BodyInstance.cpp:4648-4743 [V]): mass / inertia / COM, damping, max angular
			// velocity, max depenetration (no contacts: ignored), initial velocity, sleep / stabilisation thresholds
			// (no automatic sleep: ignored), iteration counts (no solver: ignored).
			Body.Mass = Info.Mass;
			Body.InertiaDiagonal = Info.InertiaDiagonal;
			if (IsDynamic(Body))
			{
				Body.InvMass = 1.0 / Info.Mass;
				Body.InvInertia = PtVec3{ 1.0 / Info.InertiaDiagonal.X, 1.0 / Info.InertiaDiagonal.Y, 1.0 / Info.InertiaDiagonal.Z };
				Body.LinearDamping = Info.LinearDamping;
				Body.AngularDamping = Info.AngularDamping;
				Body.MaxAngularVelocity = Info.MaxAngularVelocity;
				Body.MaxLinearVelocity = Info.MaxLinearVelocity;
				Body.V = Info.InitialLinearVelocity;
				Body.W = Info.InitialAngularVelocity;
				// 5. !bStartAwake: put to sleep after the add (BodyInstance.cpp:4723-4733 [V]); skipped when the scene
				// disables sleeping (02 6), and counted.
				if ((Info.Flags & PT_BODY_START_AWAKE) == 0)
				{
					if ((SimFlags & PT_SIM_ALLOW_SLEEPING) != 0)
					{
						Body.ObjectState = PT_OBJECT_STATE_SLEEPING;
						Body.V = GZero;
						Body.W = GZero;
					}
					else
					{
						++NumStartAsleepSkipped;
					}
				}
			}
		}
		return PT_OK;
	}

	PtResult FNullScene::ReleaseActors(const uint32_t* BodyIndices, const uint32_t Num)
	{
		// The kit has rejected out-of-range, already released and repeated indices (all or nothing).
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			Bodies[BodyIndices[Index]].bReleased = true;
		}
		return PT_OK;
	}

	PtResult FNullScene::PrepareForSimulation(Abi::FTextWriter& OutActions)
	{
		OutActions.Append("null: nothing to prepare (no broadphase)\n");
		return PT_OK;
	}

	PtResult FNullScene::SetUpForFrame(const Abi::FFrameParams& Params)
	{
		// UE passes the gravity every frame (ChaosScene.cpp:333 [V]).
		if (Params.bNewGravity)
		{
			Gravity = Params.Gravity;
		}
		FrameParams = Params;
		return PT_OK;
	}

	PtResult FNullScene::StartFrame()
	{
		// FChaosScene::StartFrame -> AdvanceAndDispatch_External: plan, push, then run the steps (single-threaded, inline).
		FramePlan = FNullFramePlanner::Plan(FrameParams, AsyncDeltaTime, AccumulatedTime);
		StepsTakenThisFrame = 0;
		// ExternalSteps counts the frames with dt > 0 since the last push; a dt of 0 "has nothing to do with force"
		// (PhysicsSolverBase.cpp:623-626 [V]).
		if (FramePlan.FrameDeltaTime > 0.0)
		{
			++ExternalSteps;
		}
		if (FramePlan.NumPushedSteps == 0)
		{
			// Nothing pushed (AsyncFixed accumulating): forces, impulses and kinematic targets stay where the game thread
			// left them and go with the next push.
			return PT_OK;
		}
		// PushPhysicsState(InternalDt, NumSteps, Max(ExternalSteps, 1)): the accumulated forces are averaged over those
		// frames, DynamicsWeight = 1 / NumExternalSteps (PhysicsSolverBase.cpp:642-646, PBDRigidsSolver.cpp:1582,
		// ParticleHandle.h:3604-3613 [V]), and every pushed step of the frame applies them (CopySubstepData).
		const double DynamicsWeight = 1.0 / static_cast<double>(ExternalSteps > 1u ? ExternalSteps : 1u);
		ExternalSteps = 0;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			FNullBody& Body = Bodies[Index];
			Body.LinearAccel = Scale(Body.LinearAccel, DynamicsWeight);
			Body.AngularAccel = Scale(Body.AngularAccel, DynamicsWeight);
		}
		for (uint32 StepIndex = 0; StepIndex < FramePlan.NumSteps; ++StepIndex)
		{
			Step(StepIndex);
			++StepsTakenThisFrame;
			++TotalSteps;
		}
		// The push consumed the forces (FSingleParticlePhysicsProxy::ClearAccumulatedData, SingleParticlePhysicsProxy.cpp:258-267
		// [V]), also when no pushed step ran (dt <= MinDeltaTime). Impulses not integrated yet stay pending.
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			Bodies[Index].LinearAccel = GZero;
			Bodies[Index].AngularAccel = GZero;
		}
		return PT_OK;
	}

	PtResult FNullScene::WaitPhysScenes()
	{
		return PT_OK;
	}

	PtResult FNullScene::EndFrame(PtFrameResult& Out)
	{
		// Forces were consumed by the push (StartFrame), kinematic targets by the step that reached them.
		Out.NumStepsTaken = StepsTakenThisFrame;
		Out.EngineErrorBits = 0;
		return PT_OK;
	}

	void FNullScene::Step(const uint32 StepIndex)
	{
		const uint32 NumSteps = FramePlan.NumSteps;
		const double Dt = FramePlan.StepDeltaTime;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			FNullBody& Body = Bodies[Index];
			if (Body.bReleased || Body.Motion == PT_MOTION_STATIC)
			{
				continue;
			}
			if (Body.Motion == PT_MOTION_KINEMATIC)
			{
				if (Body.bHasKinematicTarget)
				{
					// EKinematicTargetMode::Position (PBDRigidsEvolutionGBF.cpp:1203-1220 [V]) with StepFraction = the
					// PseudoFraction 1 / (N - k) of a solver-substepped frame, else 1 (PhysicsSolverBase.cpp:250 [V]): the
					// target is reached when the fraction is 1, and the steps after it zero the velocity (mode Reset).
					PtTransform Pose = Body.KinematicTarget;
					if (FramePlan.bSolverSubstepped && StepIndex + 1 < NumSteps)
					{
						const double Fraction = 1.0 / static_cast<double>(NumSteps - StepIndex);
						const PtTransform Current = GetActorPose(Body);
						Pose.Translation = Lerp(Current.Translation, Body.KinematicTarget.Translation, Fraction);
						Pose.Rotation = Slerp(Current.Rotation, Body.KinematicTarget.Rotation, Fraction);
					}
					else
					{
						Body.bHasKinematicTarget = false;
					}
					const PtVec3 OldP = Body.P;
					const PtQuat OldQ = Body.Q;
					SetActorPose(Body, Pose);
					Body.V = Scale(Sub(Body.P, OldP), 1.0 / Dt);
					Body.W = AngularVelocityBetween(OldQ, Body.Q, Dt);
					Body.bDirty = true;
				}
				else if (!IsZero(Body.V) || !IsZero(Body.W))
				{
					Body.V = GZero;
					Body.W = GZero;
					Body.bDirty = true;
				}
				continue;
			}
			if (Body.ObjectState != PT_OBJECT_STATE_DYNAMIC)
			{
				continue;   // asleep
			}
			// FPBDRigidsEvolutionGBF::Integrate order (PBDRigidsEvolutionGBF.cpp:904-982 [V]): the pushed acceleration,
			// to which the gravity force rule adds.
			PtVec3 V = Body.V;
			PtVec3 W = Body.W;
			PtVec3 Acceleration = Body.LinearAccel;
			const PtVec3 AngularAcceleration = Body.AngularAccel;
			if ((Body.Flags & PT_BODY_ENABLE_GRAVITY) != 0)
			{
				Acceleration = Add(Acceleration, Gravity);
			}
			V = Add(V, Scale(Acceleration, Dt));
			W = Add(W, Scale(AngularAcceleration, Dt));
			V = Add(V, Body.LinearImpulseVelocity);
			W = Add(W, Body.AngularImpulseVelocity);
			Body.LinearImpulseVelocity = GZero;
			Body.AngularImpulseVelocity = GZero;
			V = Scale(V, Abi::UEDampingMultiplier(Body.LinearDamping, Dt));
			W = Scale(W, Abi::UEDampingMultiplier(Body.AngularDamping, Dt));
			ClampSpeed(V, Body.MaxLinearVelocity);
			ClampSpeed(W, Body.MaxAngularVelocity);
			Body.P = Add(Body.P, Scale(V, Dt));
			Body.Q = IntegrateRotation(Body.Q, W, Dt);
			Body.V = V;
			Body.W = W;
			Body.bDirty = true;
		}
	}

	PtResult FNullScene::ValidateBodyCommand(const PtBodyCommand& Cmd)
	{
		// (Commands to released bodies were rejected by the kit.)
		const FNullBody& Body = Bodies[Cmd.Body];
		if (Cmd.Type == PT_CMD_SET_KINEMATIC_TARGET && Body.Motion != PT_MOTION_KINEMATIC)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: SET_KINEMATIC_TARGET on body %u, which is not kinematic", Cmd.Body);
		}
		return PT_OK;
	}

	PtResult FNullScene::ApplyBodyCommand(const PtBodyCommand& Cmd)
	{
		FNullBody& Body = Bodies[Cmd.Body];
		const bool bAutoWake = (Cmd.Flags & PT_CMDF_AUTO_WAKE) != 0;
		const bool bAccelChange = (Cmd.Flags & PT_CMDF_ACCEL_CHANGE) != 0;
		switch (Cmd.Type)
		{
		case PT_CMD_SET_GLOBAL_POSE:
			SetActorPose(Body, Cmd.Transform);
			if (Body.Motion == PT_MOTION_KINEMATIC)
			{
				// UE5's teleport of a kinematic body also makes the pose its kinematic target and zeroes the velocity at once
				// (ChaosEngineInterface.cpp:2550-2557 [V]): the next step keeps it in place.
				Body.KinematicTarget = Cmd.Transform;
				Body.bHasKinematicTarget = true;
				Body.V = GZero;
				Body.W = GZero;
			}
			Body.bDirty = true;
			// The game-thread SetX wakes a sleeping body whatever bAutoWake says (SingleParticlePhysicsProxy.h:240-251 [V]):
			// PT_CMDF_AUTO_WAKE is informational for the teleport.
			Wake(Body);
			break;
		case PT_CMD_SET_KINEMATIC_TARGET:
			Body.KinematicTarget = Cmd.Transform;   // the last target before a step wins
			Body.bHasKinematicTarget = true;
			break;
		case PT_CMD_SET_LINEAR_VELOCITY:
		case PT_CMD_SET_ANGULAR_VELOCITY:
			if (IsDynamic(Body))
			{
				(Cmd.Type == PT_CMD_SET_LINEAR_VELOCITY ? Body.V : Body.W) = Cmd.Vector;
				Body.bDirty = true;
				if (bAutoWake)
				{
					Wake(Body);
				}
			}
			break;
		case PT_CMD_SET_BODY_STATE:
			SetActorPose(Body, Cmd.Transform);
			if (IsDynamic(Body))
			{
				Body.V = Cmd.Vector;
				Body.W = Cmd.Vector2;
			}
			Body.bDirty = true;
			break;
		case PT_CMD_ADD_IMPULSE:
		case PT_CMD_ADD_VELOCITY:
			if (IsDynamic(Body))
			{
				// FChaosStateOps::AddVelocity is AddImpulse(M * dv) (ChaosEngineInterface.cpp:861-864 [V]).
				const PtVec3 Impulse = Cmd.Type == PT_CMD_ADD_VELOCITY ? Scale(Cmd.Vector, Body.Mass) : Cmd.Vector;
				Body.LinearImpulseVelocity = Add(Body.LinearImpulseVelocity, Scale(Impulse, Body.InvMass));
				Wake(Body);   // [R] the Chaos GT impulse setter wakes the particle
			}
			break;
		case PT_CMD_ADD_ANGULAR_IMPULSE:
		case PT_CMD_ADD_ANGULAR_VELOCITY:
			if (IsDynamic(Body))
			{
				// AddAngularVelocityInRadians is an angular impulse of I_world * dw (ChaosEngineInterface.cpp:866-870 [V]).
				const PtVec3 Impulse = Cmd.Type == PT_CMD_ADD_ANGULAR_VELOCITY ? ApplyInertia(Body, Cmd.Vector) : Cmd.Vector;
				Body.AngularImpulseVelocity = Add(Body.AngularImpulseVelocity, ApplyInvInertia(Body, Impulse));
				Wake(Body);
			}
			break;
		case PT_CMD_ADD_FORCE:
		case PT_CMD_ADD_TORQUE:
			if (IsDynamic(Body))
			{
				const bool bNoWake = (Cmd.Flags & PT_CMDF_NO_WAKE) != 0;
				if (bNoWake && Body.ObjectState != PT_OBJECT_STATE_DYNAMIC)
				{
					// Asleep when the force arrives: the host sent UE's game-thread wake where the force was issued, so a body
					// put to sleep after that stays asleep and is not integrated (UE: a sleeping particle does not move).
					break;
				}
				PtVec3 Accel;
				if (Cmd.Type == PT_CMD_ADD_FORCE)
				{
					// bAccelChange adds Force * M (ChaosEngineInterface.cpp:940-944 [V]); the integrator divides by M.
					Accel = Scale(bAccelChange ? Scale(Cmd.Vector, Body.Mass) : Cmd.Vector, Body.InvMass);
				}
				else
				{
					Accel = ApplyInvInertia(Body, bAccelChange ? ApplyInertia(Body, Cmd.Vector) : Cmd.Vector);
				}
				// PT_CMDF_ALLOW_SUBSTEPPING is ignored: UE5 does not use bAllowSubstepping (ChaosEngineInterface.cpp:932-950,
				// :1024-1037 [V]); every step of the pushing frame applies the force (StartFrame).
				PtVec3& Accumulated = Cmd.Type == PT_CMD_ADD_FORCE ? Body.LinearAccel : Body.AngularAccel;
				Accumulated = Add(Accumulated, Accel);
				// Without PT_CMDF_NO_WAKE this is UE's AddForce / AddTorque: the game-thread proxy wakes a sleeping body on
				// every call, a zero vector included (SingleParticlePhysicsProxy.h:706-720, :757-771 [V]).
				if (!bNoWake)
				{
					Wake(Body);
				}
			}
			break;
		case PT_CMD_WAKE_UP:
			Wake(Body);
			break;
		case PT_CMD_PUT_TO_SLEEP:
			// Ignored when the scene disables sleeping (PhysX rejects putToSleep in an eDISABLE_SLEEPING scene).
			if (IsDynamic(Body) && (SimFlags & PT_SIM_ALLOW_SLEEPING) != 0 && Body.ObjectState == PT_OBJECT_STATE_DYNAMIC)
			{
				Body.ObjectState = PT_OBJECT_STATE_SLEEPING;
				Body.V = GZero;
				Body.W = GZero;
				Body.bDirty = true;
			}
			break;
		default:
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: command type %u", Cmd.Type);
		}
		return PT_OK;
	}

	PtBodyState FNullScene::MakeState(const FNullBody& Body) const
	{
		PtBodyState State = {};
		State.ActorTransform = GetActorPose(Body);
		State.LinearVelocity = Body.V;
		State.AngularVelocity = Body.W;
		State.ObjectState = Body.ObjectState;
		State.Flags = 0;
		return State;
	}

	PtResult FNullScene::ReadBodyStates(const uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, const uint32_t Capacity, uint32_t& OutCount)
	{
		// PT_READ_ALL is dense (a released body reads as UNINITIALIZED); PT_READ_ACTIVE_ONLY reports live bodies whose state
		// may have changed since the previous read.
		const bool bActiveOnly = ReadFlags == PT_READ_ACTIVE_ONLY;
		const auto IsReported = [bActiveOnly](const FNullBody& Body)
		{
			return !bActiveOnly || (!Body.bReleased && (Body.bDirty || (IsDynamic(Body) && Body.ObjectState == PT_OBJECT_STATE_DYNAMIC)));
		};
		uint32 Count = 0;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			Count += IsReported(Bodies[Index]) ? 1u : 0u;
		}
		OutCount = Count;
		if (Capacity < Count)
		{
			return PT_E_CAPACITY;
		}
		uint32 Written = 0;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			FNullBody& Body = Bodies[Index];
			if (!IsReported(Body))
			{
				continue;
			}
			if (OutIndices != nullptr)
			{
				OutIndices[Written] = Index;
			}
			if (Body.bReleased)
			{
				PtBodyState Released = {};
				Released.ActorTransform.Rotation.W = 1.0;
				Released.ObjectState = PT_OBJECT_STATE_UNINITIALIZED;
				Out[Written] = Released;
			}
			else
			{
				Out[Written] = MakeState(Body);
				Body.bDirty = false;
			}
			++Written;
		}
		return PT_OK;
	}

	PtResult FNullScene::ReadMassProperties(const uint32_t First, const uint32_t Num, PtMassProperties* Out)
	{
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const FNullBody& Body = Bodies[First + Index];
			Out[Index].Mass = Body.Mass;
			Out[Index].InertiaDiagonal = Body.InertiaDiagonal;
			Out[Index].CenterOfMassLocal = Body.ComLocal;
		}
		return PT_OK;
	}

	PtResult FNullScene::GetStepStats(PtStepStats& Out)
	{
		int32 Active = 0;
		int32 Sleeping = 0;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FNullBody& Body = Bodies[Index];
			if (!Body.bReleased && IsDynamic(Body))
			{
				Active += Body.ObjectState == PT_OBJECT_STATE_DYNAMIC ? 1 : 0;
				Sleeping += Body.ObjectState == PT_OBJECT_STATE_SLEEPING ? 1 : 0;
			}
		}
		Out.NumActiveBodies = Active;
		Out.NumSleepingBodies = Sleeping;
		Out.NumContactPairs = 0;
		Out.NumContactPoints = 0;
		Out.NumIslands = -1;
		Out.EngineErrorBits = 0;
		Out.NumPhaseTimes = 0;
		return PT_OK;
	}

	PtResult FNullScene::ComputeNativeStateHash(uint64_t& OutHash)
	{
		// FNV-1a over each live body's actor translation and rotation (doubles), in body index order (02 7.2 #21).
		uint64 Hash = 0xcbf29ce484222325ull;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			if (Bodies[Index].bReleased)
			{
				continue;
			}
			const PtTransform Pose = GetActorPose(Bodies[Index]);
			const double Values[7] = { Pose.Translation.X, Pose.Translation.Y, Pose.Translation.Z, Pose.Rotation.X, Pose.Rotation.Y, Pose.Rotation.Z,
				Pose.Rotation.W };
			Hash = Abi::Fnv1a64(Values, sizeof(Values), Hash);
		}
		OutHash = Hash;
		return PT_OK;
	}

	PtResult FNullScene::SetOption(const Abi::FStringRef Key, const Abi::FStringRef /*Value*/)
	{
		return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: unknown scene option '%.*s' (Null has none)", static_cast<int>(Key.Length), Key.Data);
	}

	PtResult FNullScene::DumpEffectiveSettings(Abi::FTextWriter& Out)
	{
		const double NaN = nan("");
		char Text[128];
		FormatVec(Text, Gravity);
		// The generic set (02 10, A.10). Concepts Null does not have are native "n/a", COMPILE_TIME.
		Out.AppendEffective("world.gravity", Text, "cm/s^2", Gravity.Z, "cm/s^2", "READBACK");
		Out.AppendEffective("solver.type", "semi-implicit Euler, no collision", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("solver.iterations", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("broadphase.type", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("contact.restitution_threshold", "n/a", "cm/s", NaN, "cm/s", "COMPILE_TIME");
		Out.AppendEffective("contact.offset_rule", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("contact.speculative_distance", "n/a", "cm", NaN, "cm", "COMPILE_TIME");
		Out.AppendEffective("material.friction_combine", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("material.restitution_combine", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("material.static_friction_rule", "n/a", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("body.damping_formula", "v *= max(0, 1 - c*dt) per step", "", NaN, "", "COMPILE_TIME");
		double MinLinear = 0.0, MaxLinear = 0.0, MinAngular = 0.0, MaxAngular = 0.0, MinMaxAng = 0.0, MaxMaxAng = 0.0, MinMaxLin = 0.0, MaxMaxLin = 0.0;
		bool bFirst = true;
		for (uint32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FNullBody& Body = Bodies[Index];
			if (Body.bReleased || !IsDynamic(Body))
			{
				continue;
			}
			if (bFirst)
			{
				MinLinear = MaxLinear = Body.LinearDamping;
				MinAngular = MaxAngular = Body.AngularDamping;
				MinMaxAng = MaxMaxAng = Body.MaxAngularVelocity;
				MinMaxLin = MaxMaxLin = Body.MaxLinearVelocity;
				bFirst = false;
				continue;
			}
			MinLinear = Body.LinearDamping < MinLinear ? Body.LinearDamping : MinLinear;
			MaxLinear = Body.LinearDamping > MaxLinear ? Body.LinearDamping : MaxLinear;
			MinAngular = Body.AngularDamping < MinAngular ? Body.AngularDamping : MinAngular;
			MaxAngular = Body.AngularDamping > MaxAngular ? Body.AngularDamping : MaxAngular;
			MinMaxAng = Body.MaxAngularVelocity < MinMaxAng ? Body.MaxAngularVelocity : MinMaxAng;
			MaxMaxAng = Body.MaxAngularVelocity > MaxMaxAng ? Body.MaxAngularVelocity : MaxMaxAng;
			MinMaxLin = Body.MaxLinearVelocity < MinMaxLin ? Body.MaxLinearVelocity : MinMaxLin;
			MaxMaxLin = Body.MaxLinearVelocity > MaxMaxLin ? Body.MaxLinearVelocity : MaxMaxLin;
		}
		snprintf(Text, sizeof(Text), "linear [%.9g, %.9g] angular [%.9g, %.9g] over dynamic bodies", MinLinear, MaxLinear, MinAngular, MaxAngular);
		Out.AppendEffective("body.damping", Text, "1/s", bFirst ? NaN : MaxLinear, "1/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] (<= 0 = unclamped)", MinMaxAng, MaxMaxAng);
		Out.AppendEffective("body.max_angular_velocity", Text, "rad/s", bFirst ? NaN : MaxMaxAng, "rad/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] (<= 0 = unclamped)", MinMaxLin, MaxMaxLin);
		Out.AppendEffective("body.max_linear_velocity", Text, "cm/s", bFirst ? NaN : MaxMaxLin, "cm/s", "READBACK");
		Out.AppendEffective("body.gyroscopic", "unsupported (flag ignored)", "", 0.0, "bool", "COMPILE_TIME");
		const bool bSleep = (SimFlags & PT_SIM_ALLOW_SLEEPING) != 0;
		Out.AppendEffective("sleep.enabled", bSleep ? "true" : "false", "bool", bSleep ? 1.0 : 0.0, "bool", "READBACK");
		Out.AppendEffective("sleep.criteria", "explicit only (start asleep, PUT_TO_SLEEP); no automatic sleep", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("ccd.enabled", "false", "bool", 0.0, "bool", "COMPILE_TIME");
		Out.AppendEffective("threads.workers", "0 (inline on the calling thread)", "threads", 0.0, "threads", "COMPILE_TIME");
		const bool bDeterministic = (SimFlags & PT_SIM_ENHANCED_DETERMINISM) != 0;
		Out.AppendEffective("determinism.flag", bDeterministic ? "true (Null is always deterministic)" : "false (Null is always deterministic)", "bool",
			bDeterministic ? 1.0 : 0.0, "bool", "READBACK");
		Out.AppendEffective("shape.box_margin", "n/a", "cm", NaN, "cm", "COMPILE_TIME");
		Out.AppendEffective("shape.convex_radius", "n/a", "cm", NaN, "cm", "COMPILE_TIME");
		Out.AppendEffective("shape.wrapping", "0", "shapes", 0.0, "shapes", "COMPILE_TIME");
		Out.AppendEffective("listeners.installed", "0", "listeners", 0.0, "listeners", "COMPILE_TIME");
		// Null-specific (not required).
		snprintf(Text, sizeof(Text), "%.17g", AsyncDeltaTime);
		Out.AppendEffective("null.async_dt", Text, "s", AsyncDeltaTime, "s", "READBACK");
		Out.AppendEffective("null.frame_model", "native: UE 5.8.3 FChaosScene / FPhysicsSolverBase rules", "", NaN, "", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "%u", NumStartAsleepSkipped);
		Out.AppendEffective("null.start_asleep_skipped", Text, "bodies", static_cast<double>(NumStartAsleepSkipped), "bodies", "READBACK");
		// PtSceneDesc::MaxBodiesHint: the body array is reserved to it at CreateScene (0 = no hint).
		snprintf(Text, sizeof(Text), "%u", MaxBodiesHint);
		Out.AppendEffective("null.max_bodies_hint", Text, "bodies", static_cast<double>(MaxBodiesHint), "bodies", "READBACK");
		return PT_OK;
	}

	PtResult FNullScene::GetMemoryStats(PtMemoryStats& Out)
	{
		Out.LiveBytes = Memory.LiveBytes;
		Out.PeakBytes = Memory.PeakBytes;
		Out.NumAllocs = Memory.NumAllocs;
		Out.NumFrees = Memory.NumFrees;
		Out.AllocatorKind = 0;   // CRT malloc
		return PT_OK;
	}

	// ---- FNullBackend ------------------------------------------------------------------------------------------------
	void FNullBackend::GetInfo(PtBackendInfo& Out) const
	{
		Out.Name = ModuleName;
		Out.DisplayName = "Null (SDK-free reference backend)";
		Out.EngineVersion = "PhysicsTest Null, ABI v0.9";
		Out.SourceCommit = "n/a";
		Out.BuildConfig = GetBuildConfig();
		Out.BuildFlagsJson = GNullBuildReport;
		Out.Capabilities = PT_CAP_KINEMATIC | PT_CAP_NATIVE_FRAME_MODEL | PT_CAP_ACTIVE_LIST | PT_CAP_STATE_WRITE | PT_CAP_NATIVE_HASH;
		Out.ModuleFlags = 0;
		Out.NumPhaseTimers = 0;
		Out.PhaseTimerNames = nullptr;
		Out.NumRequiredEffectiveKeys = 0;   // the generic set only (A.13)
		Out.RequiredEffectiveKeys = nullptr;
	}

	PtResult FNullBackend::InitGamePhys(const PtInitParams& Params)
	{
		if (Params.NumOptions > 0)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: unknown module option '%s' (Null has none)", Params.Options[0].Key);
		}
		MaxThreads = Params.MaxThreads;
		GetKit().LogToHost(PT_LOG_LOG, "LogNullBackend", "Null backend initialised (MaxThreads %u, ignored: Null runs inline)", MaxThreads);
		return PT_OK;
	}

	PtResult FNullBackend::TermGamePhys()
	{
		return PT_OK;
	}

	PtResult FNullBackend::CreateScene(const PtSceneDesc& Desc, Abi::IPhysicsBackendScene*& OutScene)
	{
		if (Desc.NumOptions > 0)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: unknown scene option '%s' (Null has none)", Desc.Options[0].Key);
		}
		FNullScene* Scene = new (::std::nothrow) FNullScene(Desc);
		if (Scene == nullptr)
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "Null: cannot allocate a scene");
		}
		++NumLiveScenes;
		OutScene = Scene;
		return PT_OK;
	}

	void FNullBackend::DestroyScene(Abi::IPhysicsBackendScene* Scene)
	{
		delete Scene;
		--NumLiveScenes;
	}

	PtResult FNullBackend::SetModuleOption(const Abi::FStringRef Key, const Abi::FStringRef /*Value*/)
	{
		return GetKit().SetLastError(PT_E_INVALID_ARG, "Null: unknown module option '%.*s' (Null has none)", static_cast<int>(Key.Length), Key.Data);
	}

	PtResult FNullBackend::DumpModuleSettings(Abi::FTextWriter& Out)
	{
		char Text[64];
		snprintf(Text, sizeof(Text), "%u", MaxThreads);
		Out.AppendEffective("null.max_threads", Text, "threads", static_cast<double>(MaxThreads), "threads", "READBACK");
		snprintf(Text, sizeof(Text), "%u", NumLiveScenes);
		Out.AppendEffective("null.live_scenes", Text, "scenes", static_cast<double>(NumLiveScenes), "scenes", "READBACK");
		return PT_OK;
	}

	PtResult FNullBackend::GetMemoryStats(PtMemoryStats& Out)
	{
		Out.AllocatorKind = 0;   // CRT malloc; per-scene numbers come from GetMemoryStats(Scene)
		return PT_OK;
	}
}

// The static entry the generated backend registry references (PTBackendRegistry.gen.cpp); the DLL entry is in
// NullBackendDll.cpp.
PT_IMPLEMENT_BACKEND_MODULE_STATIC(PhysicsTest::NullBackend::FNullBackend, PT_GetBackendApi_Null)
