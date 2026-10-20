// Math.cpp - out-of-line members of FVector, FRotator, FQuat and FTransform.
// Behaviour mirrors UE 5.8.3 (Core/Public/Math/*.h, Core/Private/Math/UnrealMath.cpp); the code is our own.
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"

namespace PhysicsTest
{
	namespace
	{
		constexpr double ThreshVectorNormalized = 0.01;   // UE_THRESH_VECTOR_NORMALIZED
		constexpr double QuatSingularityThreshold = 0.4999995;
	}

	// ------------------------------------------------------------------------------------------------------------
	// FVector
	// ------------------------------------------------------------------------------------------------------------
	FVector FVector::GetSafeNormal(const double Tolerance) const
	{
		const double SquareSum = SizeSquared();
		if (SquareSum == 1.0)
		{
			return *this;
		}
		if (SquareSum < Tolerance)
		{
			return ZeroVector;
		}
		const double Scale = FMath::InvSqrt(SquareSum);
		return FVector(X * Scale, Y * Scale, Z * Scale);
	}

	bool FVector::Normalize(const double Tolerance)
	{
		const double SquareSum = SizeSquared();
		if (SquareSum > Tolerance)
		{
			const double Scale = FMath::InvSqrt(SquareSum);
			X *= Scale;
			Y *= Scale;
			Z *= Scale;
			return true;
		}
		return false;
	}

	bool FVector::IsNearlyZero(const double Tolerance) const
	{
		return FMath::Abs(X) <= Tolerance && FMath::Abs(Y) <= Tolerance && FMath::Abs(Z) <= Tolerance;
	}

	bool FVector::IsNormalized() const
	{
		return FMath::Abs(1.0 - SizeSquared()) < ThreshVectorNormalized;
	}

	bool FVector::Equals(const FVector& V, const double Tolerance) const
	{
		return FMath::Abs(X - V.X) <= Tolerance && FMath::Abs(Y - V.Y) <= Tolerance && FMath::Abs(Z - V.Z) <= Tolerance;
	}

	bool FVector::ContainsNaN() const
	{
		return !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z);
	}

	// ------------------------------------------------------------------------------------------------------------
	// FRotator
	// ------------------------------------------------------------------------------------------------------------
	FRotator::FRotator(const FQuat& Quat)
	{
		*this = Quat.Rotator();
	}

	FQuat FRotator::Quaternion() const
	{
		// Q = QYaw(+Z) * QPitch(-Y) * QRoll(-X): positive Pitch lifts +X toward +Z and positive Roll turns +Y toward
		// -Z (UE's left-handed angle conventions expressed in right-handed quaternion algebra). The product is
		// expanded below. Angles are wrapped by 360 first, as UE does (UnrealMath.cpp:483, VectorMod360).
		const double HalfDegToRad = UE_DOUBLE_PI / 360.0;
		double SP = 0.0, CP = 0.0, SY = 0.0, CY = 0.0, SR = 0.0, CR = 0.0;
		FMath::SinCos(&SP, &CP, FMath::Fmod(Pitch, 360.0) * HalfDegToRad);
		FMath::SinCos(&SY, &CY, FMath::Fmod(Yaw, 360.0) * HalfDegToRad);
		FMath::SinCos(&SR, &CR, FMath::Fmod(Roll, 360.0) * HalfDegToRad);
		const FQuat Result(
			CR * SP * SY - SR * CP * CY,
			-CR * SP * CY - SR * CP * SY,
			CR * CP * SY - SR * SP * CY,
			CR * CP * CY + SR * SP * SY);
#if PT_DO_CHECK
		// UE (ENABLE_NAN_DIAGNOSTIC || DO_CHECK, UnrealMath.cpp:538-545): a non-finite result (e.g. an infinite angle)
		// is logged once and replaced by Identity. With PT_DO_CHECK=0, as in UE without DO_CHECK, it is returned as is.
		if (Result.ContainsNaN()) [[unlikely]]
		{
			static bool bLogged = false;
			if (!bLogged)
			{
				bLogged = true;
				UE_LOG(LogCore, Error, "FRotator::Quaternion: invalid input (P=%f Y=%f R=%f) generated a non-finite quaternion; using Identity",
					Pitch, Yaw, Roll);
			}
			return FQuat::Identity;
		}
#endif
		return Result;
	}

	FVector FRotator::RotateVector(const FVector& V) const
	{
		return Quaternion().RotateVector(V);
	}

	FVector FRotator::UnrotateVector(const FVector& V) const
	{
		return Quaternion().UnrotateVector(V);
	}

	bool FRotator::Equals(const FRotator& R, const double Tolerance) const
	{
		return FMath::Abs(NormalizeAxis(Pitch - R.Pitch)) <= Tolerance
			&& FMath::Abs(NormalizeAxis(Yaw - R.Yaw)) <= Tolerance
			&& FMath::Abs(NormalizeAxis(Roll - R.Roll)) <= Tolerance;
	}

	bool FRotator::IsNearlyZero(const double Tolerance) const
	{
		return FMath::Abs(NormalizeAxis(Pitch)) <= Tolerance
			&& FMath::Abs(NormalizeAxis(Yaw)) <= Tolerance
			&& FMath::Abs(NormalizeAxis(Roll)) <= Tolerance;
	}

	FRotator FRotator::GetNormalized() const
	{
		return FRotator(NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll));
	}

	double FRotator::ClampAxis(double Angle)
	{
		Angle = FMath::Fmod(Angle, 360.0);
		if (Angle < 0.0)
		{
			Angle += 360.0;
		}
		return Angle;
	}

	double FRotator::NormalizeAxis(double Angle)
	{
		Angle = ClampAxis(Angle);
		if (Angle > 180.0)
		{
			Angle -= 360.0;
		}
		return Angle;
	}

	// ------------------------------------------------------------------------------------------------------------
	// FQuat
	// ------------------------------------------------------------------------------------------------------------
	FQuat::FQuat(const FVector& Axis, const double AngleRad)
	{
		double S = 0.0, C = 0.0;
		FMath::SinCos(&S, &C, 0.5 * AngleRad);
		X = S * Axis.X;
		Y = S * Axis.Y;
		Z = S * Axis.Z;
		W = C;
	}

	FQuat::FQuat(const FRotator& R)
	{
		*this = R.Quaternion();
	}

	FQuat FQuat::operator*(const FQuat& Q) const
	{
		// Hamilton product; the right operand is applied first.
		return FQuat(
			W * Q.X + X * Q.W + Y * Q.Z - Z * Q.Y,
			W * Q.Y - X * Q.Z + Y * Q.W + Z * Q.X,
			W * Q.Z + X * Q.Y - Y * Q.X + Z * Q.W,
			W * Q.W - X * Q.X - Y * Q.Y - Z * Q.Z);
	}

	FQuat& FQuat::operator*=(const FQuat& Q)
	{
		*this = *this * Q;
		return *this;
	}

	FVector FQuat::RotateVector(const FVector& V) const
	{
		// v' = v + 2w(q x v) + 2 q x (q x v)
		const FVector Q(X, Y, Z);
		const FVector T = 2.0 * (Q ^ V);
		return V + (W * T) + (Q ^ T);
	}

	FVector FQuat::UnrotateVector(const FVector& V) const
	{
		const FVector Q(-X, -Y, -Z);
		const FVector T = 2.0 * (Q ^ V);
		return V + (W * T) + (Q ^ T);
	}

	FQuat FQuat::Inverse() const
	{
		return FQuat(-X, -Y, -Z, W);
	}

	double FQuat::Size() const
	{
		return FMath::Sqrt(SizeSquared());
	}

	bool FQuat::IsNormalized() const
	{
		return FMath::Abs(1.0 - SizeSquared()) < UE_THRESH_QUAT_NORMALIZED;
	}

	void FQuat::Normalize(const double Tolerance)
	{
		const double SquareSum = SizeSquared();
		if (SquareSum >= Tolerance)
		{
			const double Scale = FMath::InvSqrt(SquareSum);
			X *= Scale;
			Y *= Scale;
			Z *= Scale;
			W *= Scale;
		}
		else
		{
			*this = Identity;
		}
	}

	FQuat FQuat::GetNormalized(const double Tolerance) const
	{
		FQuat Result(*this);
		Result.Normalize(Tolerance);
		return Result;
	}

	bool FQuat::Equals(const FQuat& Q, const double Tolerance) const
	{
		const bool bSame = FMath::Abs(X - Q.X) <= Tolerance && FMath::Abs(Y - Q.Y) <= Tolerance
			&& FMath::Abs(Z - Q.Z) <= Tolerance && FMath::Abs(W - Q.W) <= Tolerance;
		const bool bNegated = FMath::Abs(X + Q.X) <= Tolerance && FMath::Abs(Y + Q.Y) <= Tolerance
			&& FMath::Abs(Z + Q.Z) <= Tolerance && FMath::Abs(W + Q.W) <= Tolerance;
		return bSame || bNegated;
	}

	bool FQuat::ContainsNaN() const
	{
		return !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z) || !FMath::IsFinite(W);
	}

	double FQuat::GetAngle() const
	{
		return 2.0 * FMath::Acos(W);
	}

	FVector FQuat::GetRotationAxis() const
	{
		const double SquareSum = X * X + Y * Y + Z * Z;
		if (SquareSum < UE_SMALL_NUMBER)
		{
			return FVector(1.0, 0.0, 0.0);
		}
		const double Scale = FMath::InvSqrt(SquareSum);
		return FVector(X * Scale, Y * Scale, Z * Scale);
	}

	void FQuat::ToAxisAndAngle(FVector& Axis, double& Angle) const
	{
		Angle = GetAngle();
		Axis = GetRotationAxis();
	}

	FRotator FQuat::Rotator() const
	{
		// Inverse of FRotator::Quaternion() (Z-Y-X Euler extraction with UE's pitch/roll signs). Inside the singularity
		// band (|SingularityTest| > 0.4999995, |Pitch| > ~89.92 deg) the yaw/roll split is not unique; UE 5.8.3 snaps
		// Pitch to +-90, sets Roll = 0 and puts the whole twist into Yaw = NormalizeAxis(-+2 * atan2(X, W))
		// (FQuat4d::Rotator, UnrealMath.cpp:691-717).
		const double SingularityTest = Z * X - W * Y;
		const double YawY = 2.0 * (W * Z + X * Y);
		const double YawX = 1.0 - 2.0 * (FMath::Square(Y) + FMath::Square(Z));

		FRotator Result;
		if (SingularityTest < -QuatSingularityThreshold)
		{
			Result.Pitch = -90.0;
			Result.Yaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(-2.0 * FMath::Atan2(X, W)));
			Result.Roll = 0.0;
		}
		else if (SingularityTest > QuatSingularityThreshold)
		{
			Result.Pitch = 90.0;
			Result.Yaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(2.0 * FMath::Atan2(X, W)));
			Result.Roll = 0.0;
		}
		else
		{
			Result.Pitch = FMath::RadiansToDegrees(FMath::Asin(2.0 * SingularityTest));
			Result.Yaw = FMath::RadiansToDegrees(FMath::Atan2(YawY, YawX));
			Result.Roll = FMath::RadiansToDegrees(FMath::Atan2(-2.0 * (W * X + Y * Z), 1.0 - 2.0 * (FMath::Square(X) + FMath::Square(Y))));
		}
		return Result;
	}

	FVector FQuat::Euler() const
	{
		return Rotator().Euler();
	}

	FQuat FQuat::MakeFromEuler(const FVector& Euler)
	{
		return FRotator::MakeFromEuler(Euler).Quaternion();
	}

	// ------------------------------------------------------------------------------------------------------------
	// FTransform
	// ------------------------------------------------------------------------------------------------------------
	void FTransform::Multiply(FTransform* OutTransform, const FTransform* A, const FTransform* B)
	{
		// UE composes transforms with any negative scale component through matrices (MultiplyUsingMatrixWithScale,
		// TransformNonVectorized.h), which the harness does not implement (no FMatrix yet). Fail in EVERY build, not
		// through checkf: with PT_DO_CHECK=0 (test preset) a check would be compiled out and the composition below
		// would silently give a non-UE result. Fatal logs and terminates (exit code 3).
		const bool bNegativeScale = A->Scale3D.X < 0.0 || A->Scale3D.Y < 0.0 || A->Scale3D.Z < 0.0
			|| B->Scale3D.X < 0.0 || B->Scale3D.Y < 0.0 || B->Scale3D.Z < 0.0;
		if (bNegativeScale) [[unlikely]]
		{
			UE_LOG(LogCore, Fatal, "FTransform::Multiply: negative scale composition (UE's matrix path) is not supported by the harness");
		}

		// Temporaries first: OutTransform may alias A or B.
		const FQuat Rotation = B->Rotation * A->Rotation;
		const FVector Scale3D = A->Scale3D * B->Scale3D;
		const FVector Translation = B->Rotation.RotateVector(B->Scale3D * A->Translation) + B->Translation;
		OutTransform->Rotation = Rotation;
		OutTransform->Scale3D = Scale3D;
		OutTransform->Translation = Translation;
	}

	FTransform FTransform::operator*(const FTransform& Other) const
	{
		FTransform Result;
		Multiply(&Result, this, &Other);
		return Result;
	}

	FTransform& FTransform::operator*=(const FTransform& Other)
	{
		Multiply(this, this, &Other);
		return *this;
	}

	FVector FTransform::TransformPosition(const FVector& V) const
	{
		return Rotation.RotateVector(Scale3D * V) + Translation;
	}

	FVector FTransform::TransformPositionNoScale(const FVector& V) const
	{
		return Rotation.RotateVector(V) + Translation;
	}

	FVector FTransform::TransformVector(const FVector& V) const
	{
		return Rotation.RotateVector(Scale3D * V);
	}

	FVector FTransform::TransformVectorNoScale(const FVector& V) const
	{
		return Rotation.RotateVector(V);
	}

	FVector FTransform::InverseTransformPosition(const FVector& V) const
	{
		return Rotation.UnrotateVector(V - Translation) * GetSafeScaleReciprocal(Scale3D);
	}

	FVector FTransform::InverseTransformPositionNoScale(const FVector& V) const
	{
		return Rotation.UnrotateVector(V - Translation);
	}

	FVector FTransform::InverseTransformVector(const FVector& V) const
	{
		return Rotation.UnrotateVector(V) * GetSafeScaleReciprocal(Scale3D);
	}

	FVector FTransform::InverseTransformVectorNoScale(const FVector& V) const
	{
		return Rotation.UnrotateVector(V);
	}

	FTransform FTransform::Inverse() const
	{
		const FQuat InvRotation = Rotation.Inverse();
		const FVector InvScale3D = GetSafeScaleReciprocal(Scale3D);
		const FVector InvTranslation = InvRotation.RotateVector(InvScale3D * -Translation);
		return FTransform(InvRotation, InvTranslation, InvScale3D);
	}

	bool FTransform::Equals(const FTransform& Other, const double Tolerance) const
	{
		return Rotation.Equals(Other.Rotation, Tolerance) && Translation.Equals(Other.Translation, Tolerance)
			&& Scale3D.Equals(Other.Scale3D, Tolerance);
	}

	bool FTransform::ContainsNaN() const
	{
		return Rotation.ContainsNaN() || Translation.ContainsNaN() || Scale3D.ContainsNaN();
	}

	FVector FTransform::GetSafeScaleReciprocal(const FVector& InScale, const double Tolerance)
	{
		const auto SafeReciprocal = [Tolerance](const double Value)
		{
			return FMath::Abs(Value) <= Tolerance ? 0.0 : 1.0 / Value;
		};
		return FVector(SafeReciprocal(InScale.X), SafeReciprocal(InScale.Y), SafeReciprocal(InScale.Z));
	}
}
