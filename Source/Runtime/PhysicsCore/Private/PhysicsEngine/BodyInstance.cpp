// BodyInstance.cpp - FBodyInstance data-model operations (M1), commands (M2) and the collision filter test.
#include "PhysicsEngine/BodyInstance.h"

#include "Math/UnrealMathUtility.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "Physics/PhysicsInterface.h"
#include "PhysScene.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyUtils.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace PhysicsTest
{
	bool FBodyCollisionFilter::CanCollideWith(const FBodyCollisionFilter& Other) const
	{
		if (!CollisionEnabledHasPhysics(CollisionEnabled) || !CollisionEnabledHasPhysics(Other.CollisionEnabled))
		{
			return false;
		}
		// Chaos FShapeFilterData::NarrowFilter (CollisionFilterData.cpp:263-272 [V]).
		return (GetChannelMask() & Other.BlockMask) != 0 && (Other.GetChannelMask() & BlockMask) != 0;
	}

	void FBodyInstance::InitBody(const FBodySetup* Setup, const FTransform& Transform, FPhysScene* InRBScene)
	{
		BodySetup = Setup;
		Scene = InRBScene;
		Scale3D = Transform.GetScale3D();
		State.BodyTM = FTransform(Transform.GetRotation(), Transform.GetTranslation());   // the scale stays in Scale3D
		State.LinearVelocity = InitialLinearVelocity;
		State.AngularVelocityInRadians = InitialAngularVelocityInRadians;
		State.ObjectState = EObjectStateType::Uninitialized;
		UpdateMassProperties();
		if (Scene != nullptr)
		{
			Scene->DeferAddActor(this);
		}
	}

	void FBodyInstance::TermBody()
	{
		if (Scene != nullptr && ActorHandle.IsValid())
		{
			Scene->ReleaseBody(this);   // ReleaseActors through the scene proxy (phase Idle)
		}
		ActorHandle = FPhysicsActorHandle();
		State.ObjectState = EObjectStateType::Uninitialized;
		Scene = nullptr;
	}

	void FBodyInstance::SetBodyTransform(const FTransform& NewTransform, const ETeleportType Teleport)
	{
		if (!ActorHandle.IsValid())
		{
			return;
		}
		// The rigid pose only: the scale stays in Scale3D (02 5.2 #26).
		const FTransform Pose(NewTransform.GetRotation(), NewTransform.GetTranslation());
		// UE5 FBodyInstance::SetBodyTransform (BodyInstance.cpp:2804-2827 [V]).
		if (IsKinematic() && Teleport == ETeleportType::None && Scene != nullptr)
		{
			Scene->SetKinematicTarget_AssumesLocked(this, Pose, true);
		}
		else
		{
			if (IsKinematic())
			{
				FPhysicsInterface::SetKinematicTarget_AssumesLocked(ActorHandle, Pose);
			}
			FPhysicsInterface::SetGlobalPose_AssumesLocked(ActorHandle, Pose);
		}
	}

	void FBodyInstance::SetLinearVelocity(const FVector& NewVel, const bool bAddToCurrent)
	{
		if (ActorHandle.IsValid())
		{
			// UE: FinalVelocity = NewVel (+ FPhysicsInterface::GetLinearVelocity_AssumesLocked), then set
			// (BodyInstance.cpp:3630-3646 [V]). The read sees the writes issued since the last pull, as UE's game-thread
			// particle does.
			FVector FinalVelocity = NewVel;
			if (bAddToCurrent)
			{
				FinalVelocity += FPhysicsInterface::GetLinearVelocity_AssumesLocked(ActorHandle);
			}
			FPhysicsInterface::SetLinearVelocity_AssumesLocked(ActorHandle, FinalVelocity);
		}
	}

	void FBodyInstance::SetAngularVelocityInRadians(const FVector& NewAngVel, const bool bAddToCurrent)
	{
		if (ActorHandle.IsValid())
		{
			FVector FinalAngularVelocity = NewAngVel;
			if (bAddToCurrent)
			{
				FinalAngularVelocity += FPhysicsInterface::GetAngularVelocity_AssumesLocked(ActorHandle);
			}
			FPhysicsInterface::SetAngularVelocity_AssumesLocked(ActorHandle, FinalAngularVelocity);
		}
	}

	void FBodyInstance::AddForce(const FVector& Force, const bool bAllowSubstepping, const bool bAccelChange)
	{
		// UE5: FPhysicsInterface::AddForce_AssumesLocked (BodyInstance.cpp:3769-3779 [V]), which queues the force in the
		// owning FPhysScene.
		if (ActorHandle.IsValid() && Scene != nullptr)
		{
			FPhysicsInterface::AddForce_AssumesLocked(ActorHandle, Force, bAllowSubstepping, bAccelChange);
		}
	}

	void FBodyInstance::AddTorqueInRadians(const FVector& Torque, const bool bAllowSubstepping, const bool bAccelChange)
	{
		if (ActorHandle.IsValid() && Scene != nullptr)
		{
			FPhysicsInterface::AddTorque_AssumesLocked(ActorHandle, Torque, bAllowSubstepping, bAccelChange);
		}
	}

	void FBodyInstance::AddImpulse(const FVector& Impulse, const bool bVelChange)
	{
		if (ActorHandle.IsValid())
		{
			if (bVelChange)
			{
				FPhysicsInterface::AddVelocity_AssumesLocked(ActorHandle, Impulse);
			}
			else
			{
				FPhysicsInterface::AddImpulse_AssumesLocked(ActorHandle, Impulse);
			}
		}
	}

	void FBodyInstance::AddAngularImpulseInRadians(const FVector& Impulse, const bool bVelChange)
	{
		if (ActorHandle.IsValid())
		{
			if (bVelChange)
			{
				FPhysicsInterface::AddAngularVelocityInRadians_AssumesLocked(ActorHandle, Impulse);
			}
			else
			{
				FPhysicsInterface::AddAngularImpulseInRadians_AssumesLocked(ActorHandle, Impulse);
			}
		}
	}

	void FBodyInstance::WakeInstance()
	{
		if (ActorHandle.IsValid())
		{
			FPhysicsInterface::WakeUp_AssumesLocked(ActorHandle);
		}
	}

	void FBodyInstance::PutInstanceToSleep()
	{
		if (ActorHandle.IsValid())
		{
			FPhysicsInterface::PutToSleep_AssumesLocked(ActorHandle);
		}
	}

	void FBodyInstance::UpdateMassProperties()
	{
		if (BodySetup == nullptr)
		{
			MassProperties = FMassProperties();
			return;
		}
		const EMassRule Rule = Scene != nullptr ? Scene->GetTestSettings().MassRule : EMassRule::UEBodyUtils;
		MassProperties = BodyUtils::ComputeMassProperties(*this, *BodySetup, GetSimplePhysicalMaterial(), Rule);
	}

	const FPhysicalMaterial* FBodyInstance::GetSimplePhysicalMaterial() const
	{
		if (PhysMaterialOverride != nullptr)
		{
			return PhysMaterialOverride;
		}
		if (BodySetup != nullptr && BodySetup->PhysMaterial != nullptr)
		{
			return BodySetup->PhysMaterial;
		}
		return Scene != nullptr ? Scene->GetDefaultPhysicalMaterial() : nullptr;
	}

	EObjectStateType FBodyInstance::GetInitialObjectState() const
	{
		if (IsStatic())
		{
			return EObjectStateType::Static;
		}
		return IsKinematic() ? EObjectStateType::Kinematic : EObjectStateType::Dynamic;
	}

	float FBodyInstance::GetSleepThresholdMultiplier() const
	{
		if (SleepFamily == ESleepFamily::Sensitive)
		{
			return PhysicsCVars::GetSensitiveSleepThresholdMultiplier();   // p.SensitiveSleepThresholdMultiplier
		}
		if (SleepFamily == ESleepFamily::Custom)
		{
			return CustomSleepThresholdMultiplier;
		}
		return 1.0f;
	}

	double FBodyInstance::GetMaxAngularVelocityInRadians(const FPhysicsSettingsCore& Project) const
	{
		const double Degrees = bOverrideMaxAngularVelocity ? MaxAngularVelocity : Project.MaxAngularVelocity;
		return FMath::DegreesToRadians(Degrees);
	}
}
