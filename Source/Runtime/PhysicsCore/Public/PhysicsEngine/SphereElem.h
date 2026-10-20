// SphereElem.h - FKSphereElem (UE: Engine/Classes/PhysicsEngine/SphereElem.h). [MVP M1]
#pragma once

#include "Math/Vector.h"
#include "PhysicsEngine/ShapeElem.h"

namespace PhysicsTest
{
	struct FKSphereElem : FKShapeElem
	{
		FVector Center;        // body-setup space, cm (SphereElem.h:25 [V])
		double Radius = 1.0;   // cm, default 1 (SphereElem.h:29, :37 [V])

		FKSphereElem() = default;
		explicit FKSphereElem(const double InRadius) : Radius(InRadius) {}

		// UE FKSphereElem::GetScaledVolume (SphereElem.h:80 [V]), including its 1.3333f factor. This is the element
		// volume UE reports; mass integration uses the Chaos sphere volume (BodyUtils).
		[[nodiscard]] double GetScaledVolume(const FVector& Scale3D) const;
		// UE FKSphereElem::GetFinalScaled with RelativeTM = Identity (BodySetup.cpp:2006-2019 [V]): Radius * min|S|,
		// Center * S.
		[[nodiscard]] FKSphereElem GetFinalScaled(const FVector& Scale3D) const;
	};
}
