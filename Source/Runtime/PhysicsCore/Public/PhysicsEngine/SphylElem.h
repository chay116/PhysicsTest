// SphylElem.h - FKSphylElem, the capsule (UE: Engine/Classes/PhysicsEngine/SphylElem.h). [MVP M1]
// Length is the length of the line segment along the element's LOCAL Z axis; the radius is added at both ends, so the
// total height is Length + 2 * Radius (SphylElem.h:36-40 [V]). The C ABI carries Radius + HalfLength = Length / 2
// along local Z (DL06). Rotation is an FRotator as in UE (SphylElem.h:32 [V]).
#pragma once

#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "PhysicsEngine/ShapeElem.h"

namespace PhysicsTest
{
	struct FKSphylElem : FKShapeElem
	{
		FVector Center;        // cm (SphylElem.h:28 [V])
		FRotator Rotation;     // degrees (SphylElem.h:32 [V])
		double Radius = 1.0;   // cm (SphylElem.h:36, default 1 at :50 [V])
		double Length = 1.0;   // segment length along local Z, cm (SphylElem.h:40 [V])

		FKSphylElem() = default;
		FKSphylElem(const double InRadius, const double InLength) : Radius(InRadius), Length(InLength) {}

		// UE: FTransform(Rotation, Center) (SphylElem.h:83-86 [V]).
		[[nodiscard]] FTransform GetTransform() const { return FTransform(Rotation, Center); }
		void SetTransform(const FTransform& InTransform);
		// End points of the segment in body-setup space: Center -/+ Rotation * (0, 0, Length / 2), as UE builds the
		// Chaos capsule (ChaosInterfaceUtils.cpp:184-185 [V]).
		void GetSegment(FVector& OutStart, FVector& OutEnd) const;

		// UE helpers with their clamps (BodySetup.cpp:2167-2182 [V]): HalfLength = max((Length + 2R) |Sz| / 2, 0.1),
		// Radius = clamp(R * max(|Sx|, |Sy|), 0.1, HalfLength), CylinderLength = max(0.1, 2 (HalfLength - Radius)).
		[[nodiscard]] double GetScaledRadius(const FVector& Scale3D) const;
		[[nodiscard]] double GetScaledCylinderLength(const FVector& Scale3D) const;
		[[nodiscard]] double GetScaledHalfLength(const FVector& Scale3D) const;
		// UE FKSphylElem::GetScaledVolume (SphylElem.h:98-103 [V]), including its 1.3333f factor (element volume as UE
		// reports it; mass integration uses the Chaos capsule volume, BodyUtils).
		[[nodiscard]] double GetScaledVolume(const FVector& Scale3D) const;
		// UE FKSphylElem::GetFinalScaled with RelativeTM = Identity (BodySetup.cpp:2144-2165 [V]).
		[[nodiscard]] FKSphylElem GetFinalScaled(const FVector& Scale3D) const;
	};
}
