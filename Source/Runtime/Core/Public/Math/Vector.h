// Vector.h - FVector (double, UE units: cm). UE: Core/Public/Math/Vector.h (TVector<double>).
// Operators follow UE: '|' is the dot product, '^' the cross product, '*' and '/' are component-wise for vectors.
#pragma once

#include "Math/UnrealMathUtility.h"

namespace PhysicsTest
{
	struct FVector
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		static const FVector ZeroVector;
		static const FVector OneVector;
		static const FVector UpVector;       // (0, 0, 1)
		static const FVector ForwardVector;  // (1, 0, 0)
		static const FVector RightVector;    // (0, 1, 0)

		constexpr FVector() = default;
		constexpr FVector(const double InX, const double InY, const double InZ) : X(InX), Y(InY), Z(InZ) {}
		explicit constexpr FVector(const double InF) : X(InF), Y(InF), Z(InF) {}

		[[nodiscard]] constexpr FVector operator+(const FVector& V) const { return FVector(X + V.X, Y + V.Y, Z + V.Z); }
		[[nodiscard]] constexpr FVector operator-(const FVector& V) const { return FVector(X - V.X, Y - V.Y, Z - V.Z); }
		[[nodiscard]] constexpr FVector operator-() const { return FVector(-X, -Y, -Z); }
		[[nodiscard]] constexpr FVector operator*(const double Scale) const { return FVector(X * Scale, Y * Scale, Z * Scale); }
		[[nodiscard]] constexpr FVector operator/(const double Scale) const { return FVector(X / Scale, Y / Scale, Z / Scale); }
		[[nodiscard]] constexpr FVector operator*(const FVector& V) const { return FVector(X * V.X, Y * V.Y, Z * V.Z); }
		[[nodiscard]] constexpr FVector operator/(const FVector& V) const { return FVector(X / V.X, Y / V.Y, Z / V.Z); }
		// Dot product (UE: operator|).
		[[nodiscard]] constexpr double operator|(const FVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		// Cross product (UE: operator^).
		[[nodiscard]] constexpr FVector operator^(const FVector& V) const
		{
			return FVector(Y * V.Z - Z * V.Y, Z * V.X - X * V.Z, X * V.Y - Y * V.X);
		}

		constexpr FVector& operator+=(const FVector& V) { X += V.X; Y += V.Y; Z += V.Z; return *this; }
		constexpr FVector& operator-=(const FVector& V) { X -= V.X; Y -= V.Y; Z -= V.Z; return *this; }
		constexpr FVector& operator*=(const double Scale) { X *= Scale; Y *= Scale; Z *= Scale; return *this; }
		constexpr FVector& operator/=(const double Scale) { X /= Scale; Y /= Scale; Z /= Scale; return *this; }

		[[nodiscard]] constexpr bool operator==(const FVector& V) const { return X == V.X && Y == V.Y && Z == V.Z; }
		[[nodiscard]] constexpr bool operator!=(const FVector& V) const { return !(*this == V); }

		[[nodiscard]] double& operator[](const int32 Index) { return Index == 0 ? X : (Index == 1 ? Y : Z); }
		[[nodiscard]] double operator[](const int32 Index) const { return Index == 0 ? X : (Index == 1 ? Y : Z); }

		[[nodiscard]] static constexpr double DotProduct(const FVector& A, const FVector& B) { return A | B; }
		[[nodiscard]] static constexpr FVector CrossProduct(const FVector& A, const FVector& B) { return A ^ B; }
		[[nodiscard]] static double Dist(const FVector& A, const FVector& B) { return (A - B).Size(); }
		[[nodiscard]] static constexpr double DistSquared(const FVector& A, const FVector& B) { return (A - B).SizeSquared(); }

		[[nodiscard]] double Size() const { return FMath::Sqrt(SizeSquared()); }
		[[nodiscard]] constexpr double SizeSquared() const { return X * X + Y * Y + Z * Z; }
		[[nodiscard]] double GetMax() const { return FMath::Max(FMath::Max(X, Y), Z); }
		[[nodiscard]] double GetMin() const { return FMath::Min(FMath::Min(X, Y), Z); }
		[[nodiscard]] double GetAbsMax() const { return FMath::Max(FMath::Max(FMath::Abs(X), FMath::Abs(Y)), FMath::Abs(Z)); }
		[[nodiscard]] double GetAbsMin() const { return FMath::Min(FMath::Min(FMath::Abs(X), FMath::Abs(Y)), FMath::Abs(Z)); }

		// Unit vector, or ZeroVector when the squared length is below Tolerance (UE semantics).
		[[nodiscard]] FVector GetSafeNormal(double Tolerance = UE_SMALL_NUMBER) const;
		// Normalizes in place when the squared length exceeds Tolerance; returns false otherwise (vector unchanged).
		bool Normalize(double Tolerance = UE_SMALL_NUMBER);

		// Every component within Tolerance of zero.
		[[nodiscard]] bool IsNearlyZero(double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] bool IsZero() const { return X == 0.0 && Y == 0.0 && Z == 0.0; }
		[[nodiscard]] bool IsNormalized() const;
		// Every component within Tolerance of the other vector's component.
		[[nodiscard]] bool Equals(const FVector& V, double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] bool ContainsNaN() const;
	};

	[[nodiscard]] constexpr FVector operator*(const double Scale, const FVector& V)
	{
		return V * Scale;
	}

	inline const FVector FVector::ZeroVector(0.0, 0.0, 0.0);
	inline const FVector FVector::OneVector(1.0, 1.0, 1.0);
	inline const FVector FVector::UpVector(0.0, 0.0, 1.0);
	inline const FVector FVector::ForwardVector(1.0, 0.0, 0.0);
	inline const FVector FVector::RightVector(0.0, 1.0, 0.0);
}
