// ConvexElem.h - FKConvexElem (UE: Engine/Classes/PhysicsEngine/ConvexElem.h). [MVP M1: data model only]
// Phase 1 keeps convex hulls in the data model and the ABI only (DL31): the harness quickhull (BuildHull) and convex
// mass integration are backlog items (M15), so no scene creates a convex body before then. BodyUtils rejects convex
// elements that contribute to mass (UE_LOG Error) instead of guessing.
// UE stores VertexData, IndexData, ElemBox and Transform (ConvexElem.h:37-49 [V]); the harness keeps HullTriangles
// (index triples, filled by the M15 hull builder) instead of IndexData.
#pragma once

#include "Math/Box.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "PhysicsEngine/ShapeElem.h"

#include <array>
#include <vector>

namespace PhysicsTest
{
	struct FKConvexElem : FKShapeElem
	{
		std::vector<FVector> VertexData;                     // element space, cm
		std::vector<std::array<int32, 3>> HullTriangles;      // filled by BuildHull() (M15)
		FBox ElemBox;                                         // bounds of VertexData (UpdateElemBox)
		FTransform Transform;                                 // element -> body-setup space

		[[nodiscard]] const FTransform& GetTransform() const { return Transform; }
		void SetTransform(const FTransform& InTransform) { Transform = InTransform; }
		// UE FKConvexElem::UpdateElemBox: the box around VertexData.
		void UpdateElemBox();
	};
}
