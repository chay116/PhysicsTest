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
		// UE semantics: an exactly unit vector comes back unchanged, a too short one as zero.
		const double LengthSquared = SizeSquared();
		if (LengthSquared == 1.0)
		{
			return *this;
		}
		if (LengthSquared < Tolerance)
		{
			return ZeroVector;
		}
		const double InvLength = FMath::InvSqrt(LengthSquared);
		return FVector(X * InvLength, Y * InvLength, Z * InvLength);
	}

	bool FVector::Normalize(const double Tolerance)
	{
		const double LengthSquared = SizeSquared();
		if (!(LengthSquared > Tolerance))
		{
			return false;
		}
		*this *= FMath::InvSqrt(LengthSquared);
		return true;
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

	FVector FVector::GetPerpendicularUnit() const
	{
		// Crossing with a world axis that is far from this direction: world X when the direction is mostly vertical, world
		// Z otherwise. Either cross product keeps at least sqrt(1/3) of a unit direction's length, and the choice changes
		// only where the vertical component stops being the largest, so the result is stable between frames.
		const bool bMostlyVertical = FMath::Abs(Z) > FMath::Max(FMath::Abs(X), FMath::Abs(Y));
		return (*this ^ (bMostlyVertical ? ForwardVector : UpVector)).GetSafeNormal();
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

	namespace
	{
		// Each angle is compared after wrapping into (-180, 180], so 359.9 and -0.1 degrees are the same; NaN is never near.
		bool AreAnglesNearlyZero(const double Tolerance, const double First, const double Second, const double Third)
		{
			const double Angles[3] = { First, Second, Third };
			for (const double Angle : Angles)
			{
				if (!(FMath::Abs(FRotator::NormalizeAxis(Angle)) <= Tolerance))
				{
					return false;
				}
			}
			return true;
		}
	}

	bool FRotator::Equals(const FRotator& R, const double Tolerance) const
	{
		return AreAnglesNearlyZero(Tolerance, Pitch - R.Pitch, Yaw - R.Yaw, Roll - R.Roll);
	}

	bool FRotator::IsNearlyZero(const double Tolerance) const
	{
		return AreAnglesNearlyZero(Tolerance, Pitch, Yaw, Roll);
	}

	FRotator FRotator::GetNormalized() const
	{
		return FRotator(NormalizeAxis(Pitch), NormalizeAxis(Yaw), NormalizeAxis(Roll));
	}

	double FRotator::ClampAxis(const double Angle)
	{
		// Into [0, 360): fmod keeps the sign of its first argument, so a negative remainder moves up one turn.
		const double Remainder = FMath::Fmod(Angle, 360.0);
		return Remainder < 0.0 ? Remainder + 360.0 : Remainder;
	}

	double FRotator::NormalizeAxis(const double Angle)
	{
		// Into (-180, 180].
		const double Wrapped = ClampAxis(Angle);
		return Wrapped > 180.0 ? Wrapped - 360.0 : Wrapped;
	}

	// ------------------------------------------------------------------------------------------------------------
	// FQuat
	// ------------------------------------------------------------------------------------------------------------
	FQuat::FQuat(const FVector& Axis, const double AngleRad)
	{
		// Unit quaternion of a rotation by AngleRad about the unit Axis: (sin(a/2) Axis, cos(a/2)).
		double SinHalf = 0.0;
		double CosHalf = 0.0;
		FMath::SinCos(&SinHalf, &CosHalf, 0.5 * AngleRad);
		X = SinHalf * Axis.X;
		Y = SinHalf * Axis.Y;
		Z = SinHalf * Axis.Z;
		W = CosHalf;
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
		// A quaternion too short to normalise becomes the identity (UE semantics).
		const double LengthSquared = SizeSquared();
		if (!(LengthSquared >= Tolerance))
		{
			*this = Identity;
			return;
		}
		const double InvLength = FMath::InvSqrt(LengthSquared);
		X *= InvLength;
		Y *= InvLength;
		Z *= InvLength;
		W *= InvLength;
	}

	FQuat FQuat::GetNormalized(const double Tolerance) const
	{
		FQuat Copy(*this);
		Copy.Normalize(Tolerance);
		return Copy;
	}

	FQuat FQuat::Slerp_NotNormalized(const FQuat& Quat1, const FQuat& Quat2, const double Alpha)
	{
		// Shoemake's slerp on the shorter arc: with c = <Quat1, Quat2> and Quat2' = sign(c) Quat2,
		//   q(t) = (sin((1 - t) theta) Quat1 + sin(t theta) Quat2') / sin(theta),  theta = acos |c|;
		// the weights tend to (1 - t, t) as theta -> 0 and are used as such for |c| >= 1 - 1e-4. The behavioural reference
		// is UE's TQuat::Slerp_NotNormalized (UnrealMath.cpp:1226-1255 [V]: shortest arc, the same linear band); the Null
		// backend's Slerp uses this same formulation, so the two frame drivers interpolate kinematic targets alike.
		const double Dot4 = Quat1 | Quat2;
		const double CosTheta = Dot4 < 0.0 ? -Dot4 : Dot4;
		const double ToSign = Dot4 < 0.0 ? -1.0 : 1.0;
		double WeightFrom = 1.0 - Alpha;
		double WeightTo = Alpha;
		if (CosTheta < 1.0 - 1.0e-4)
		{
			const double Theta = FMath::Acos(CosTheta);
			const double SinTheta = FMath::Sin(Theta);
			WeightFrom = FMath::Sin(WeightFrom * Theta) / SinTheta;
			WeightTo = FMath::Sin(WeightTo * Theta) / SinTheta;
		}
		WeightTo *= ToSign;
		return FQuat(WeightFrom * Quat1.X + WeightTo * Quat2.X, WeightFrom * Quat1.Y + WeightTo * Quat2.Y, WeightFrom * Quat1.Z + WeightTo * Quat2.Z,
			WeightFrom * Quat1.W + WeightTo * Quat2.W);
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
		// The normalised vector part; +X for a (near) identity rotation, whose axis is undefined (UE semantics).
		const FVector Imaginary(X, Y, Z);
		const double LengthSquared = Imaginary.SizeSquared();
		if (LengthSquared < UE_SMALL_NUMBER)
		{
			return FVector::ForwardVector;
		}
		return Imaginary * FMath::InvSqrt(LengthSquared);
	}

	void FQuat::ToAxisAndAngle(FVector& Axis, double& Angle) const
	{
		Angle = GetAngle();
		Axis = GetRotationAxis();
	}

	FRotator FQuat::Rotator() const
	{
		// Inverse of FRotator::Quaternion() (Z-Y-X Euler extraction with UE's pitch/roll signs). Inside the singularity
		// band (|sin(Pitch) / 2| > 0.4999995, |Pitch| > ~89.92 deg) the yaw/roll split is not unique; UE 5.8.3 snaps
		// Pitch to +-90, sets Roll = 0 and puts the whole twist into Yaw = NormalizeAxis(-+2 * atan2(X, W))
		// (FQuat4d::Rotator, UnrealMath.cpp:691-717).
		// HalfSinPitch = sin(Pitch) / 2 in UE's sign convention; the yaw and roll terms are the matrix entries of the rotation.
		const double HalfSinPitch = Z * X - W * Y;
		FRotator Result;
		if (FMath::Abs(HalfSinPitch) > QuatSingularityThreshold)
		{
			const double Sign = HalfSinPitch > 0.0 ? 1.0 : -1.0;
			Result.Pitch = 90.0 * Sign;
			Result.Yaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(Sign * 2.0 * FMath::Atan2(X, W)));
			Result.Roll = 0.0;
			return Result;
		}
		const double YawSine = 2.0 * (W * Z + X * Y);
		const double YawCosine = 1.0 - 2.0 * (FMath::Square(Y) + FMath::Square(Z));
		const double RollSine = -2.0 * (W * X + Y * Z);
		const double RollCosine = 1.0 - 2.0 * (FMath::Square(X) + FMath::Square(Y));
		Result.Pitch = FMath::RadiansToDegrees(FMath::Asin(2.0 * HalfSinPitch));
		Result.Yaw = FMath::RadiansToDegrees(FMath::Atan2(YawSine, YawCosine));
		Result.Roll = FMath::RadiansToDegrees(FMath::Atan2(RollSine, RollCosine));
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
		// TransformVectorized.cpp:320-324), which the harness does not implement (no FMatrix yet). Fail in EVERY build, not
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
		// Win64 UE (TransformVectorized.h:363-374): InverseFast() only when some |Scale3D| component exceeds
		// SMALL_NUMBER, otherwise Identity. (The scalar TransformNonVectorized.h path would return the inverse
		// rotation with zero scale instead; UE does not compile it on Win64.)
		if (!(FMath::Abs(Scale3D.X) > UE_SMALL_NUMBER || FMath::Abs(Scale3D.Y) > UE_SMALL_NUMBER || FMath::Abs(Scale3D.Z) > UE_SMALL_NUMBER))
		{
			return FTransform::Identity;
		}
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
