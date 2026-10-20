// PhysicalMaterial.cpp - combine-mode resolution and the reference pair rule (Chaos semantics, DL41).
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "Math/UnrealMathUtility.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace PhysicsTest
{
	EFrictionCombineMode FPhysicalMaterial::GetEffectiveFrictionCombineMode(const FPhysicsSettingsCore& Project) const
	{
		return bOverrideFrictionCombineMode ? FrictionCombineMode : Project.FrictionCombineMode;
	}

	EFrictionCombineMode FPhysicalMaterial::GetEffectiveRestitutionCombineMode(const FPhysicsSettingsCore& Project) const
	{
		return bOverrideRestitutionCombineMode ? RestitutionCombineMode : Project.RestitutionCombineMode;
	}

	EFrictionCombineMode FPhysicalMaterial::ResolveCombineMode(const EFrictionCombineMode A, const EFrictionCombineMode B)
	{
		return static_cast<uint8>(A) > static_cast<uint8>(B) ? A : B;
	}

	double FPhysicalMaterial::Combine(const EFrictionCombineMode Mode, const double A, const double B)
	{
		switch (Mode)
		{
		case EFrictionCombineMode::Average:  return 0.5 * (A + B);
		case EFrictionCombineMode::Min:      return FMath::Min(A, B);
		case EFrictionCombineMode::Multiply: return A * B;
		case EFrictionCombineMode::Max:      return FMath::Max(A, B);
		}
		return 0.5 * (A + B);
	}

	void FPhysicalMaterial::CombinePairFriction(const FPhysicalMaterial& A, const FPhysicalMaterial& B, const FPhysicsSettingsCore& Project,
		double& OutDynamic, double& OutStatic)
	{
		const EFrictionCombineMode Mode = ResolveCombineMode(A.GetEffectiveFrictionCombineMode(Project), B.GetEffectiveFrictionCombineMode(Project));
		OutDynamic = Combine(Mode, A.Friction, B.Friction);
		const double StaticA = FMath::Max<double>(A.Friction, A.StaticFriction);
		const double StaticB = FMath::Max<double>(B.Friction, B.StaticFriction);
		OutStatic = FMath::Max(Combine(Mode, StaticA, StaticB), OutDynamic);
	}

	double FPhysicalMaterial::CombinePairRestitution(const FPhysicalMaterial& A, const FPhysicalMaterial& B, const FPhysicsSettingsCore& Project)
	{
		const EFrictionCombineMode Mode = ResolveCombineMode(A.GetEffectiveRestitutionCombineMode(Project), B.GetEffectiveRestitutionCombineMode(Project));
		return Combine(Mode, A.Restitution, B.Restitution);
	}
}
