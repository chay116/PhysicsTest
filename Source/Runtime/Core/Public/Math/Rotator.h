// Rotator.h - FRotator (degrees, UE field order Pitch, Yaw, Roll). UE: Core/Public/Math/Rotator.h.
// Conventions (UE): positive Yaw turns +X toward +Y, positive Pitch lifts +X toward +Z, positive Roll turns +Y
// toward -Z (UE FRotationMatrix; CoreMathTests RotatorAxisConventions).
// FRotator::Quaternion() composes Yaw * Pitch * Roll, i.e. Roll is applied first (UnrealMath.cpp:483).
// FQuat::Rotator() is its inverse; at |Pitch| = 90 it returns Roll = 0 and puts the whole twist into Yaw (UE 5.8.3).
#pragma once

#include "Math/Vector.h"

namespace PhysicsTest
{
	struct FQuat;

	struct FRotator
	{
		double Pitch = 0.0;   // degrees about the right axis (Y)
		double Yaw = 0.0;     // degrees about the up axis (Z)
		double Roll = 0.0;    // degrees about the forward axis (X)

		static const FRotator ZeroRotator;

		constexpr FRotator() = default;
		constexpr FRotator(const double InPitch, const double InYaw, const double InRoll) : Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
		explicit FRotator(const FQuat& Quat);

		[[nodiscard]] constexpr FRotator operator+(const FRotator& R) const { return FRotator(Pitch + R.Pitch, Yaw + R.Yaw, Roll + R.Roll); }
		[[nodiscard]] constexpr FRotator operator-(const FRotator& R) const { return FRotator(Pitch - R.Pitch, Yaw - R.Yaw, Roll - R.Roll); }
		[[nodiscard]] constexpr FRotator operator*(const double Scale) const { return FRotator(Pitch * Scale, Yaw * Scale, Roll * Scale); }
		[[nodiscard]] constexpr bool operator==(const FRotator& R) const { return Pitch == R.Pitch && Yaw == R.Yaw && Roll == R.Roll; }
		[[nodiscard]] constexpr bool operator!=(const FRotator& R) const { return !(*this == R); }

		[[nodiscard]] FQuat Quaternion() const;
		// (Roll, Pitch, Yaw) in degrees, UE order.
		[[nodiscard]] constexpr FVector Euler() const { return FVector(Roll, Pitch, Yaw); }
		[[nodiscard]] static constexpr FRotator MakeFromEuler(const FVector& Euler) { return FRotator(Euler.Y, Euler.Z, Euler.X); }

		[[nodiscard]] FVector RotateVector(const FVector& V) const;
		[[nodiscard]] FVector UnrotateVector(const FVector& V) const;

		// Equality of the normalized axis differences (UE: TRotator::Equals).
		[[nodiscard]] bool Equals(const FRotator& R, double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] bool IsNearlyZero(double Tolerance = UE_KINDA_SMALL_NUMBER) const;
		[[nodiscard]] FRotator GetNormalized() const;

		// Angle in [0, 360).
		[[nodiscard]] static double ClampAxis(double Angle);
		// Angle in (-180, 180].
		[[nodiscard]] static double NormalizeAxis(double Angle);
	};

	inline const FRotator FRotator::ZeroRotator(0.0, 0.0, 0.0);
}
