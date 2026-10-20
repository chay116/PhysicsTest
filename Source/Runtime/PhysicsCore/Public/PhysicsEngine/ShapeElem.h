// ShapeElem.h - FKShapeElem, the common part of the simple collision elements (UE: Engine/Classes/PhysicsEngine/
// ShapeElem.h). [MVP M1]
// Differences from UE (A.0.4): Name is std::string (no FName), dimensions are double (D1), no UObject reflection,
// no user data / draw functions.
#pragma once

#include "Math/Vector.h"
#include "Physics/PhysicsInterfaceTypes.h"

#include <string>

namespace PhysicsTest
{
	struct FKShapeElem
	{
		std::string Name;
		// Shape rest offset; defaults as in UE's FKShapeElem() (ShapeElem.h:38-46 [V]).
		double RestOffset = 0.0;
		// Whether the element contributes to the body's mass (UE: bContributeToMass, ShapeElem.h:158).
		bool bContributeToMass = true;
		ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

		[[nodiscard]] bool GetContributeToMass() const { return bContributeToMass; }
		[[nodiscard]] ECollisionEnabled GetCollisionEnabled() const { return CollisionEnabled; }
	};

	// UE: SetupNonUniformHelper (PhysicsCore/PhysicsInterfaceTypesCore.h:109-130 [V]): a nearly-zero scale becomes
	// 0.1, OutMinScaleAbs is the smallest |component| (0.1 when it is nearly zero). Used by the elements' GetFinalScaled.
	void SetupNonUniformHelper(FVector InScale3D, double& OutMinScale, double& OutMinScaleAbs, FVector& OutScale3DAbs);
}
