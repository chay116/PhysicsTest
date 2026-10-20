// PhysicalMaterial.h - FPhysicalMaterial (UE: PhysicsCore/Public/PhysicalMaterials/PhysicalMaterial.h,
// UPhysicalMaterial). [MVP M1]
// Defaults: PhysicalMaterial.cpp:42-60 [V]. Friction and StaticFriction are kept separately and passed through the ABI
// unchanged (DL41): the pair rule is applied by the engines' combine callbacks (M2 PtCombine), never pre-combined by
// the host. CombinePairFriction below is the reference used by unit tests.
#pragma once

#include "Physics/PhysicsInterfaceTypes.h"

#include <string>

namespace PhysicsTest
{
	struct FPhysicsSettingsCore;

	struct FPhysicalMaterial
	{
		std::string Name;

		float Friction = 0.7f;
		float StaticFriction = 0.0f;
		float Restitution = 0.3f;
		EFrictionCombineMode FrictionCombineMode = EFrictionCombineMode::Average;
		bool bOverrideFrictionCombineMode = false;
		EFrictionCombineMode RestitutionCombineMode = EFrictionCombineMode::Average;
		bool bOverrideRestitutionCombineMode = false;
		float Density = 1.0f;              // g/cm^3
		float RaiseMassToPower = 0.75f;    // mass rule exponent (BodyUtils)
		float SleepLinearVelocityThreshold = 1.0f;     // cm/s
		float SleepAngularVelocityThreshold = 0.05f;   // rad/s
		int32 SleepCounterThreshold = 4;               // ticks

		// Own mode when overridden, otherwise the project mode (UPhysicsSettingsCore::FrictionCombineMode).
		[[nodiscard]] EFrictionCombineMode GetEffectiveFrictionCombineMode(const FPhysicsSettingsCore& Project) const;
		[[nodiscard]] EFrictionCombineMode GetEffectiveRestitutionCombineMode(const FPhysicsSettingsCore& Project) const;

		// When two materials disagree the larger enum value wins (Chaos FChaosPhysicsMaterial::ChooseCombineMode).
		[[nodiscard]] static EFrictionCombineMode ResolveCombineMode(EFrictionCombineMode A, EFrictionCombineMode B);
		[[nodiscard]] static double Combine(EFrictionCombineMode Mode, double A, double B);

		// Reference pair rule (Chaos 5.8.3, DL41; PBDCollisionConstraints.cpp:469-476, PBDCollisionConstraint.h:486 [V]):
		//   dynamic = C(F0, F1), static = max(C(max(F0, SF0), max(F1, SF1)), dynamic), C = the resolved friction mode.
		static void CombinePairFriction(const FPhysicalMaterial& A, const FPhysicalMaterial& B, const FPhysicsSettingsCore& Project,
			double& OutDynamic, double& OutStatic);
		[[nodiscard]] static double CombinePairRestitution(const FPhysicalMaterial& A, const FPhysicalMaterial& B, const FPhysicsSettingsCore& Project);
	};
}
