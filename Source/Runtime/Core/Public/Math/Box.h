// Box.h - FBox (axis-aligned box). UE: Core/Public/Math/Box.h (TBox<double>); the validity field is IsValid (DL50).
#pragma once

#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FBox
	{
		FVector Min;
		FVector Max;
		uint8 IsValid = 0;

		constexpr FBox() = default;
		constexpr FBox(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax), IsValid(1) {}

		// Grows the box to contain Other (an invalid box becomes the point box).
		FBox& operator+=(const FVector& Other)
		{
			if (IsValid)
			{
				Min = FVector(FMath::Min(Min.X, Other.X), FMath::Min(Min.Y, Other.Y), FMath::Min(Min.Z, Other.Z));
				Max = FVector(FMath::Max(Max.X, Other.X), FMath::Max(Max.Y, Other.Y), FMath::Max(Max.Z, Other.Z));
			}
			else
			{
				Min = Other;
				Max = Other;
				IsValid = 1;
			}
			return *this;
		}

		[[nodiscard]] constexpr FVector GetCenter() const { return (Min + Max) * 0.5; }
		// Half size (UE: GetExtent).
		[[nodiscard]] constexpr FVector GetExtent() const { return (Max - Min) * 0.5; }
		[[nodiscard]] constexpr FVector GetSize() const { return Max - Min; }
		[[nodiscard]] constexpr FBox ExpandBy(const double W) const { return FBox(Min - FVector(W), Max + FVector(W)); }
		[[nodiscard]] constexpr bool IsInside(const FVector& P) const
		{
			return P.X > Min.X && P.X < Max.X && P.Y > Min.Y && P.Y < Max.Y && P.Z > Min.Z && P.Z < Max.Z;
		}
	};
}
