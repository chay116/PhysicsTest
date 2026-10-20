// BodyInstance.cpp - FBodyInstance data-model operations (M1) and the collision filter test.
#include "PhysicsEngine/BodyInstance.h"

#include "Math/UnrealMathUtility.h"
#include "Physics/PhysicsConsoleVariables.h"
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
		// M1: no backend actor yet; M2 releases the actor through the scene proxy.
		ActorHandle = FPhysicsActorHandle();
		State.ObjectState = EObjectStateType::Uninitialized;
		Scene = nullptr;
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
