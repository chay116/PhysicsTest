// Quat.h - FQuat (double). UE: Core/Public/Math/Quat.h (TQuat<double>).
// operator* is the Hamilton product: (A * B).RotateVector(V) == A.RotateVector(B.RotateVector(V)), i.e. B is applied
// first, exactly as in UE.
#pragma once

#include "Math/Rotator.h"
#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FQuat
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double W = 1.0;

		static const FQuat Identity;

		constexpr FQuat() = default;
		constexpr FQuat(const double InX, const double InY, const double InZ, const double InW) : X(InX), Y(InY), Z(InZ), W(InW) {}
		// Rotation of AngleRad radians about the (normalized) Axis.
		FQuat(const FVector& Axis, double AngleRad);
		explicit FQuat(const FRotator& R);

		[[nodiscard]] FQuat operator*(const FQuat& Q) const;
		FQuat& operator*=(const FQuat& Q);
		// Same as RotateVector (UE: TQuat::operator*(const TVector&)).
		[[nodiscard]] FVector operator*(const FVector& V) const { return RotateVector(V); }
		[[nodiscard]] constexpr bool operator==(const FQuat& Q) const { return X == Q.X && Y == Q.Y && Z == Q.Z && W == Q.W; }
		[[nodiscard]] constexpr bool operator!=(const FQuat& Q) const { return !(*this == Q); }
		// 4D dot product (UE: operator|).
		[[nodiscard]] constexpr double operator|(const FQuat& Q) const { return X * Q.X + Y * Q.Y + Z * Q.Z + W * Q.W; }

		[[nodiscard]] FVector RotateVector(const FVector& V) const;
		[[nodiscard]] FVector UnrotateVector(const FVector& V) const;
		// Conjugate; valid for unit quaternions (UE checks IsNormalized()).
		[[nodiscard]] FQuat Inverse() const;

		[[nodiscard]] double Size() const;
		[[nodiscard]] constexpr double SizeSquared() const { return X * X + Y * Y + Z * Z + W * W; }
		[[nodiscard]] bool IsNormalized() const;
		// Normalizes in place; becomes Identity when the squared size is below Tolerance (UE semantics).
		void Normalize(double Tolerance = UE_SMALL_NUMBER);
		[[nodiscard]] FQuat GetNormalized(double Tolerance = UE_SMALL_NUMBER) const;

		// Same rotation (q and -q are equal) within Tolerance per component.
		[[nodiscard]] bool Equals(const FQuat& Q, double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] bool ContainsNaN() const;

		[[nodiscard]] double GetAngle() const;
		[[nodiscard]] FVector GetRotationAxis() const;
		void ToAxisAndAngle(FVector& Axis, double& Angle) const;
		[[nodiscard]] FVector GetAxisX() const { return RotateVector(FVector(1.0, 0.0, 0.0)); }
		[[nodiscard]] FVector GetAxisY() const { return RotateVector(FVector(0.0, 1.0, 0.0)); }
		[[nodiscard]] FVector GetAxisZ() const { return RotateVector(FVector(0.0, 0.0, 1.0)); }

		[[nodiscard]] FRotator Rotator() const;
		// (Roll, Pitch, Yaw) in degrees.
		[[nodiscard]] FVector Euler() const;
		[[nodiscard]] static FQuat MakeFromEuler(const FVector& Euler);
	};

	inline const FQuat FQuat::Identity(0.0, 0.0, 0.0, 1.0);
}
