// BoxElem.h - FKBoxElem (UE: Engine/Classes/PhysicsEngine/BoxElem.h). [MVP M1]
// X / Y / Z are FULL extents, as in UE (BoxElem.h:35-44 [V]). The C ABI carries half extents; the host halves them
// at one marshalling point (M2, DL06). Rotation is an FRotator as in UE (BoxElem.h:32 [V]); there is no FQuat field -
// GetTransform() converts once per body when the host marshals it (A.0.4).
#pragma once

#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "PhysicsEngine/ShapeElem.h"

namespace PhysicsTest
{
	struct FKBoxElem : FKShapeElem
	{
		FVector Center;        // cm (BoxElem.h:28 [V])
		FRotator Rotation;     // degrees (BoxElem.h:32 [V])
		double X = 1.0;        // full extent along local X, cm (BoxElem.h:36, default 1 at :55 [V])
		double Y = 1.0;
		double Z = 1.0;

		FKBoxElem() = default;
		explicit FKBoxElem(const double S) : X(S), Y(S), Z(S) {}
		FKBoxElem(const double InX, const double InY, const double InZ) : X(InX), Y(InY), Z(InZ) {}

		// UE: FTransform(Rotation, Center) (BoxElem.h:102-105 [V]).
		[[nodiscard]] FTransform GetTransform() const { return FTransform(Rotation, Center); }
		void SetTransform(const FTransform& InTransform);
		// |Sx Sy Sz X Y Z| (BoxElem.h:117 [V]).
		[[nodiscard]] double GetScaledVolume(const FVector& Scale3D) const;
		// UE FKBoxElem::GetFinalScaled with RelativeTM = Identity (BodySetup.cpp:2048-2065 [V]): extents * |S|,
		// Center * S, rotation unchanged (negative scale is Fatal in the harness, see Math/Transform.h).
		[[nodiscard]] FKBoxElem GetFinalScaled(const FVector& Scale3D) const;
		// Half extents (X/2, Y/2, Z/2) - what every engine and the C ABI consume.
		[[nodiscard]] FVector GetHalfExtents() const { return FVector(X * 0.5, Y * 0.5, Z * 0.5); }
	};
}
