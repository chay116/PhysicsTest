// Sphere.h - FSphere (centre + radius W). UE: Core/Public/Math/Sphere.h (TSphere<double>). [MVP M4: the debug draw region]
#pragma once

#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FSphere
	{
		FVector Center;
		double W = 0.0;   // radius, cm (UE field name)

		constexpr FSphere() = default;
		constexpr FSphere(const FVector& InCenter, const double InW) : Center(InCenter), W(InW) {}

		[[nodiscard]] bool IsInside(const FVector& Point, const double Tolerance = UE_KINDA_SMALL_NUMBER) const
		{
			return (Center - Point).SizeSquared() <= (W + Tolerance) * (W + Tolerance);
		}
	};
}
