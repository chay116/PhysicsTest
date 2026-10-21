// PhysicsInterface.h - FPhysicsInterface, the static _AssumesLocked facade (UE: FPhysInterface_Chaos / FChaosEngineInterface,
// ChaosEngineInterface.h:358-471 [V]; 02_Architecture 5.1, 7.4, A.8). [MVP M2]
// Verbs and parameter names are UE's. Impulse and velocity change are separate verbs, as in UE
// (AddImpulse_AssumesLocked vs AddVelocity_AssumesLocked, ChaosEngineInterface.h:465-467 [V]).
// Writes are buffered in the handle's scene proxy (FPhysicsBackendScene) and reach the backend in one ApplyBodyCommands
// call at the next T_push; reads flush the buffer first and pull every body's state (an untimed path). AddForce /
// AddTorque and SetKinematicTarget go to the owning FPhysScene instead (FPhysScene::AddForce_AssumesLocked,
// SetKinematicTarget_AssumesLocked: one frame rule for every entry point, 02 9.1), and a teleport of a kinematic body
// also replaces the scene's pending target (FPhysScene::TeleportKinematic_AssumesLocked, UE5's SetGlobalPose).
// bIsInternal is kept for signature parity; the harness has no physics-thread side, so only false is accepted (checkf).
// ExecuteRead / ExecuteWrite are lock-free pass-throughs: every call happens on the game thread (02 6).
#pragma once

#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/PhysicsInterfaceTypes.h"

namespace PhysicsTest
{
	class FPhysicsInterface
	{
	public:
		static void SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewPose, bool bAutoWake = true);
		static void SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewTarget);
		static void SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);
		static void SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewAngularVelocity, bool bAutoWake = true);
		static void AddForce_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Force, bool bAllowSubstepping, bool bAccelChange,
			bool bIsInternal = false);
		static void AddTorque_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange,
			bool bIsInternal = false);
		static void AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce, bool bIsInternal = false);
		static void AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque, bool bIsInternal = false);
		static void AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InVelocityDelta, bool bIsInternal = false);
		static void AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InAngularVelocityDeltaRad,
			bool bIsInternal = false);
		static void WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference);
		static void PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference);
		// Harness-only full state write (PT_CAP_STATE_WRITE).
		static void SetBodyState_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InPose, const FVector& InLinearVelocity,
			const FVector& InAngularVelocityRad);

		// Reads flush the handle's command buffer and pull every body state first (untimed).
		[[nodiscard]] static FTransform GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference);
		[[nodiscard]] static FVector GetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
		[[nodiscard]] static FVector GetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference);
		[[nodiscard]] static bool IsSleeping(const FPhysicsActorHandle& InActorReference);

		template <class FFunc>
		static bool ExecuteRead(const FPhysicsActorHandle& InHandle, FFunc&& InFunc)
		{
			if (!InHandle.IsValid())
			{
				return false;
			}
			InFunc(InHandle);
			return true;
		}
		template <class FFunc>
		static bool ExecuteWrite(const FPhysicsActorHandle& InHandle, FFunc&& InFunc)
		{
			if (!InHandle.IsValid())
			{
				return false;
			}
			InFunc(InHandle);
			return true;
		}

	private:
		// An impulse or velocity change writes the body's game-thread dynamics: forces the owning scene holds since a
		// PutToSleep are pushed again (FPhysScene::MarkDynamicsDirty_AssumesLocked, a hook only this class may call).
		static void MarkDynamicsDirty(const FPhysicsActorHandle& Handle);
	};
}
