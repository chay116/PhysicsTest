// PhysicsInterface.cpp - FPhysicsInterface: each verb becomes one PtBodyCommand in the handle's scene proxy, except forces,
// torques and kinematic targets, which go to the owning FPhysScene (its per-frame rule, PhysScene.h).
#include "Physics/PhysicsInterface.h"

#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Physics/PhysicsBackend.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysScene.h"

namespace PhysicsTest
{
	namespace
	{
		PtBodyCommand MakeCommand(const uint32 Type, const FPhysicsActorHandle& Handle, const uint32 Flags)
		{
			PtBodyCommand Command = {};
			Command.Type = Type;
			Command.Body = Handle.Index;
			Command.Flags = Flags;
			Command.Transform.Rotation.W = 1.0;
			return Command;
		}

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

		void Enqueue(const FPhysicsActorHandle& Handle, const PtBodyCommand& Command)
		{
			checkf(Handle.IsValid() && Handle.Scene != nullptr, "FPhysicsInterface: command %u on an invalid actor handle", Command.Type);
			if (Handle.IsValid() && Handle.Scene != nullptr)
			{
				Handle.Scene->EnqueueCommand(Command);
			}
		}

		// The body and its owning FPhysScene behind a handle; nullptr when the handle is invalid or the body has no scene.
		FBodyInstance* FindOwnedBody(const FPhysicsActorHandle& Handle, FPhysScene*& OutScene)
		{
			FBodyInstance* Body = Handle.IsValid() && Handle.Scene != nullptr ? Handle.Scene->GetBody(Handle.Index) : nullptr;
			OutScene = Body != nullptr ? Body->GetPhysicsScene() : nullptr;
			return OutScene != nullptr ? Body : nullptr;
		}

		// The same for the verbs that only an owning FPhysScene can carry (forces, kinematic targets): checkf otherwise.
		FBodyInstance* GetOwnedBody(const FPhysicsActorHandle& Handle, const char* Verb, FPhysScene*& OutScene)
		{
			static_cast<void>(Verb);   // read by checkf only (compiled out when PT_DO_CHECK is 0)
			checkf(Handle.IsValid() && Handle.Scene != nullptr, "FPhysicsInterface: %s on an invalid actor handle", Verb);
			FBodyInstance* Body = FindOwnedBody(Handle, OutScene);
			checkf(Body != nullptr, "FPhysicsInterface: %s: body %u has no owning FPhysScene", Verb, Handle.Index);
			return Body;
		}

		// An impulse or velocity change writes the body's game-thread dynamics: forces held since a PutToSleep are pushed
		// again (FPhysScene::MarkDynamicsDirty_AssumesLocked). Without an owning scene there are no held forces.
		void MarkDynamicsDirty(const FPhysicsActorHandle& Handle)
		{
			FPhysScene* Scene = nullptr;
			if (const FBodyInstance* Body = FindOwnedBody(Handle, Scene))
			{
				Scene->MarkDynamicsDirty_AssumesLocked(Body);
			}
		}

		// Flush the scene's buffer and pull every body state, then return the handle's body.
		const FBodyInstance* Refresh(const FPhysicsActorHandle& Handle)
		{
			if (!Handle.IsValid() || Handle.Scene == nullptr)
			{
				return nullptr;
			}
			FPhysicsBackendScene& Scene = *Handle.Scene;
			PtResult Result = Scene.FlushCommands();
			uint32 Count = 0;
			if (Result >= 0)
			{
				Result = Scene.PullBodyStates(PT_READ_ALL, Count);
			}
			if (Result < 0)
			{
				UE_LOG(LogPhysicsTest, Fatal, "FPhysicsInterface: reading body %u failed with %s: %s", Handle.Index, GetPtResultName(Result),
					Scene.GetLastErrorMessage().c_str());
			}
			return Scene.GetBody(Handle.Index);
		}
	}

	void FPhysicsInterface::SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewPose, const bool bAutoWake)
	{
		PtBodyCommand Command = MakeCommand(PT_CMD_SET_GLOBAL_POSE, InActorReference, bAutoWake ? PT_CMDF_AUTO_WAKE : 0u);
		SetTransform(Command, InNewPose);
		Enqueue(InActorReference, Command);
		// UE5: the teleport of a kinematic body also becomes its kinematic target (ChaosEngineInterface.cpp:2550-2557 [V]);
		// the owning scene replaces a pending target of the body with the pose.
		FPhysScene* Scene = nullptr;
		FBodyInstance* Body = FindOwnedBody(InActorReference, Scene);
		if (Body != nullptr && Body->IsKinematic())
		{
			Scene->TeleportKinematic_AssumesLocked(Body, InNewPose);
		}
	}

	void FPhysicsInterface::SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewTarget)
	{
		// UE5's FPhysScene_Chaos::SetKinematicTarget_AssumesLocked forwards to this verb (PhysScene_Chaos.cpp:1708-1713 [V]);
		// the harness routes the other way, so that every kinematic target follows the scene's per-step rule (Harness
		// driver). bAllowSubstepping false: UE4's direct FPhysicsInterface call was not interpolated by the substepper [M].
		FPhysScene* Scene = nullptr;
		if (FBodyInstance* Body = GetOwnedBody(InActorReference, "SetKinematicTarget_AssumesLocked", Scene))
		{
			Scene->SetKinematicTarget_AssumesLocked(Body, InNewTarget, false);
		}
	}

	void FPhysicsInterface::SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, const bool bAutoWake)
	{
		PtBodyCommand Command = MakeCommand(PT_CMD_SET_LINEAR_VELOCITY, InActorReference, bAutoWake ? PT_CMDF_AUTO_WAKE : 0u);
		Command.Vector = ToPt(InNewVelocity);
		Enqueue(InActorReference, Command);
	}

	void FPhysicsInterface::SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewAngularVelocity, const bool bAutoWake)
	{
		PtBodyCommand Command = MakeCommand(PT_CMD_SET_ANGULAR_VELOCITY, InActorReference, bAutoWake ? PT_CMDF_AUTO_WAKE : 0u);
		Command.Vector = ToPt(InNewAngularVelocity);
		Enqueue(InActorReference, Command);
	}

	void FPhysicsInterface::AddForce_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Force, const bool bAllowSubstepping,
		const bool bAccelChange, const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		// UE5's FBodyInstance::AddForce calls this verb (BodyInstance.cpp:3769-3779 [V]) and FPhysScene_Chaos::AddForce_AssumesLocked
		// forwards to it; the harness hands every force to the owning scene, so that all entry points share its frame rule.
		FPhysScene* Scene = nullptr;
		if (FBodyInstance* Body = GetOwnedBody(InActorReference, "AddForce_AssumesLocked", Scene))
		{
			Scene->AddForce_AssumesLocked(Body, Force, bAllowSubstepping, bAccelChange);
		}
	}

	void FPhysicsInterface::AddTorque_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Torque, const bool bAllowSubstepping,
		const bool bAccelChange, const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		FPhysScene* Scene = nullptr;
		if (FBodyInstance* Body = GetOwnedBody(InActorReference, "AddTorque_AssumesLocked", Scene))
		{
			Scene->AddTorque_AssumesLocked(Body, Torque, bAllowSubstepping, bAccelChange);
		}
	}

	void FPhysicsInterface::AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce, const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		PtBodyCommand Command = MakeCommand(PT_CMD_ADD_IMPULSE, InActorReference, 0u);
		Command.Vector = ToPt(InForce);
		Enqueue(InActorReference, Command);
		MarkDynamicsDirty(InActorReference);
	}

	void FPhysicsInterface::AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque, const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		PtBodyCommand Command = MakeCommand(PT_CMD_ADD_ANGULAR_IMPULSE, InActorReference, 0u);
		Command.Vector = ToPt(InTorque);
		Enqueue(InActorReference, Command);
		MarkDynamicsDirty(InActorReference);
	}

	void FPhysicsInterface::AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InVelocityDelta, const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		PtBodyCommand Command = MakeCommand(PT_CMD_ADD_VELOCITY, InActorReference, 0u);
		Command.Vector = ToPt(InVelocityDelta);
		Enqueue(InActorReference, Command);
		MarkDynamicsDirty(InActorReference);
	}

	void FPhysicsInterface::AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InAngularVelocityDeltaRad,
		const bool bIsInternal)
	{
		checkf(!bIsInternal, "FPhysicsInterface: bIsInternal is not supported (no physics-thread side)");
		PtBodyCommand Command = MakeCommand(PT_CMD_ADD_ANGULAR_VELOCITY, InActorReference, 0u);
		Command.Vector = ToPt(InAngularVelocityDeltaRad);
		Enqueue(InActorReference, Command);
		MarkDynamicsDirty(InActorReference);
	}

	void FPhysicsInterface::WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference)
	{
		Enqueue(InActorReference, MakeCommand(PT_CMD_WAKE_UP, InActorReference, 0u));
	}

	void FPhysicsInterface::PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference)
	{
		Enqueue(InActorReference, MakeCommand(PT_CMD_PUT_TO_SLEEP, InActorReference, 0u));
		// UE: putting a body to sleep marks its game-thread dynamics clean, so the forces it holds are not pushed
		// (ParticleHandle.h:3755-3766 [V]); the scene drops them at the push unless a later force or impulse re-dirties them.
		FPhysScene* Scene = nullptr;
		if (const FBodyInstance* Body = FindOwnedBody(InActorReference, Scene))
		{
			Scene->MarkDynamicsClean_AssumesLocked(Body);
		}
	}

	void FPhysicsInterface::SetBodyState_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InPose, const FVector& InLinearVelocity,
		const FVector& InAngularVelocityRad)
	{
		PtBodyCommand Command = MakeCommand(PT_CMD_SET_BODY_STATE, InActorReference, 0u);
		SetTransform(Command, InPose);
		Command.Vector = ToPt(InLinearVelocity);
		Command.Vector2 = ToPt(InAngularVelocityRad);
		Enqueue(InActorReference, Command);
	}

	FTransform FPhysicsInterface::GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference)
	{
		const FBodyInstance* Body = Refresh(InActorReference);
		return Body != nullptr ? Body->State.BodyTM : FTransform::Identity;
	}

	FVector FPhysicsInterface::GetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
	{
		const FBodyInstance* Body = Refresh(InActorReference);
		return Body != nullptr ? Body->State.LinearVelocity : FVector::ZeroVector;
	}

	FVector FPhysicsInterface::GetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference)
	{
		const FBodyInstance* Body = Refresh(InActorReference);
		return Body != nullptr ? Body->State.AngularVelocityInRadians : FVector::ZeroVector;
	}

	bool FPhysicsInterface::IsSleeping(const FPhysicsActorHandle& InActorReference)
	{
		const FBodyInstance* Body = Refresh(InActorReference);
		return Body != nullptr && Body->State.ObjectState == EObjectStateType::Sleeping;
	}
}
