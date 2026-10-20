// AggregateGeom.h - FKAggregateGeom, the simple collision of a body setup (UE: Engine/Classes/PhysicsEngine/
// AggregateGeom.h:28-37 [V]). [MVP M1]
// Phase 1 elements: spheres, boxes, sphyls (capsules); convex hulls are data only (ConvexElem.h). UE's tapered
// capsules and level sets are not part of the harness (02_Architecture 5.1 "제거한 것").
#pragma once

#include "Math/Box.h"
#include "Math/Transform.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/SphereElem.h"
#include "PhysicsEngine/SphylElem.h"

#include <vector>

namespace PhysicsTest
{
	struct FKAggregateGeom
	{
		std::vector<FKSphereElem> SphereElems;
		std::vector<FKBoxElem> BoxElems;
		std::vector<FKSphylElem> SphylElems;
		std::vector<FKConvexElem> ConvexElems;

		[[nodiscard]] int32 GetElementCount() const;
		// Sum of the elements' GetScaledVolume (UE FKAggregateGeom::GetScaledVolume, BodySetup.cpp:1768-1778 [V]).
		// Convex elements contribute 0 until the M15 hull builder exists.
		[[nodiscard]] double GetScaledVolume(const FVector& Scale3D) const;
		// World AABB of the elements under Transform (scale applied through the elements' GetFinalScaled).
		[[nodiscard]] FBox CalcAABB(const FTransform& Transform) const;
	};
}
