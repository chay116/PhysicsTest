// CoreMathTests.cpp - UE conventions of the Core math types (02_Architecture 14.1: quat/transform composition
// order, FRotator -> FQuat). Expected values are derived independently (rotation matrices written out per axis).
#include "CoreMinimal.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <limits>

using namespace PhysicsTest;

namespace
{
	constexpr double Tolerance = 1e-9;

	// Images of the unit axes under a rotator, from the closed-form rotation matrix of
	// R = Rz(Yaw) * Ry(-Pitch) * Rx(-Roll) (column vectors), i.e. UE's FRotationMatrix rows.
	void RotatorAxes(const FRotator& R, FVector& OutX, FVector& OutY, FVector& OutZ)
	{
		const double P = FMath::DegreesToRadians(R.Pitch);
		const double Y = FMath::DegreesToRadians(R.Yaw);
		const double Rl = FMath::DegreesToRadians(R.Roll);
		const double SP = FMath::Sin(P), CP = FMath::Cos(P);
		const double SY = FMath::Sin(Y), CY = FMath::Cos(Y);
		const double SR = FMath::Sin(Rl), CR = FMath::Cos(Rl);
		OutX = FVector(CP * CY, CP * SY, SP);
		OutY = FVector(SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP);
		OutZ = FVector(-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP);
	}
}

TEST_CASE("PhysicsTest.Core.VectorOperators")
{
	const FVector A(1.0, 2.0, 3.0);
	const FVector B(-4.0, 5.0, 0.5);
	CHECK((A | B) == doctest::Approx(1.0 * -4.0 + 2.0 * 5.0 + 3.0 * 0.5));
	CHECK((A ^ B).Equals(FVector(2.0 * 0.5 - 3.0 * 5.0, 3.0 * -4.0 - 1.0 * 0.5, 1.0 * 5.0 - 2.0 * -4.0), Tolerance));
	CHECK((FVector::ForwardVector ^ FVector::RightVector) == FVector::UpVector);   // X ^ Y = Z
	CHECK(FVector(3.0, 4.0, 0.0).Size() == doctest::Approx(5.0));
	CHECK(FVector(0.0, 0.0, 2.0).GetSafeNormal() == FVector::UpVector);
	CHECK(FVector(1e-6, 0.0, 0.0).GetSafeNormal() == FVector::ZeroVector);   // squared size below UE_SMALL_NUMBER
	CHECK((A * B).Equals(FVector(-4.0, 10.0, 1.5), Tolerance));
}

TEST_CASE("PhysicsTest.Core.RotatorAxisConventions")
{
	// Yaw turns +X toward +Y, Pitch lifts +X toward +Z, Roll turns +Y toward -Z (UE FRotationMatrix).
	CHECK(FRotator(0.0, 90.0, 0.0).RotateVector(FVector::ForwardVector).Equals(FVector(0.0, 1.0, 0.0), Tolerance));
	CHECK(FRotator(90.0, 0.0, 0.0).RotateVector(FVector::ForwardVector).Equals(FVector(0.0, 0.0, 1.0), Tolerance));
	CHECK(FRotator(0.0, 0.0, 90.0).RotateVector(FVector::RightVector).Equals(FVector(0.0, 0.0, -1.0), Tolerance));
}

TEST_CASE("PhysicsTest.Core.RotatorToQuatMatchesMatrix")
{
	const FRotator Rotators[] = {
		FRotator(10.0, 20.0, 30.0), FRotator(-45.0, 170.0, -100.0), FRotator(89.9, 10.0, 20.0),
		FRotator(90.0, 30.0, 40.0), FRotator(-90.0, -30.0, 15.0), FRotator(400.0, -725.0, 3.0),
	};
	for (const FRotator& R : Rotators)
	{
		FVector X, Y, Z;
		RotatorAxes(R, X, Y, Z);
		const FQuat Q = R.Quaternion();
		CAPTURE(R.Pitch);
		CAPTURE(R.Yaw);
		CAPTURE(R.Roll);
		CHECK(Q.IsNormalized());
		CHECK(Q.RotateVector(FVector::ForwardVector).Equals(X, Tolerance));
		CHECK(Q.RotateVector(FVector::RightVector).Equals(Y, Tolerance));
		CHECK(Q.RotateVector(FVector::UpVector).Equals(Z, Tolerance));
		CHECK(Q.GetAxisX().Equals(X, Tolerance));
	}
}

TEST_CASE("PhysicsTest.Core.QuatRotatorRoundTrip")
{
	const FRotator Rotators[] = {
		FRotator(0.0, 0.0, 0.0), FRotator(10.0, 20.0, 30.0), FRotator(-45.0, 170.0, -100.0), FRotator(89.9, 10.0, 20.0),
		FRotator(90.0, 30.0, 40.0), FRotator(-90.0, -30.0, 15.0), FRotator(-89.5, 120.0, -170.0),
	};
	for (const FRotator& R : Rotators)
	{
		const FQuat Q = R.Quaternion();
		const FRotator Back = Q.Rotator();
		CAPTURE(R.Pitch);
		CAPTURE(R.Yaw);
		CAPTURE(R.Roll);
		CHECK(Back.Quaternion().Equals(Q, 1e-6));   // the rotator itself is not unique at Pitch = +-90
		CHECK(FQuat::MakeFromEuler(R.Euler()).Equals(Q, Tolerance));
	}
	CHECK(FRotator(10.0, 20.0, 30.0).Quaternion().Rotator().Equals(FRotator(10.0, 20.0, 30.0), 1e-9));

	// Inside the singularity band (|SingularityTest| > 0.4999995, |Pitch| > ~89.92 deg) UE 5.8.3 snaps Pitch to +-90,
	// sets Roll = 0 and puts the whole twist into Yaw (FQuat4d::Rotator, UnrealMath.cpp:691-717), so a round trip is
	// only approximate there. Independent expectation: at Pitch = -90 only Yaw + Roll is observable, at +90 only
	// Yaw - Roll (the X axis is vertical and Yaw / Roll turn about the same line).
	const FQuat SouthPole = FRotator(-90.0, -30.0, 15.0).Quaternion();
	const FRotator SnappedSouth = SouthPole.Rotator();
	CHECK(SnappedSouth.Pitch == -90.0);
	CHECK(SnappedSouth.Roll == 0.0);
	CHECK(SnappedSouth.Yaw == doctest::Approx(-30.0 + 15.0).epsilon(1e-12));
	CHECK(SnappedSouth.Quaternion().Equals(SouthPole, 1e-9));

	const FQuat NorthPole = FRotator(90.0, 30.0, 40.0).Quaternion();
	const FRotator SnappedNorth = NorthPole.Rotator();
	CHECK(SnappedNorth.Pitch == 90.0);
	CHECK(SnappedNorth.Roll == 0.0);
	CHECK(SnappedNorth.Yaw == doctest::Approx(30.0 - 40.0).epsilon(1e-12));
	CHECK(SnappedNorth.Quaternion().Equals(NorthPole, 1e-9));

	const FQuat NearPole = FRotator(-89.99, 120.0, -170.0).Quaternion();
	const FRotator Snapped = NearPole.Rotator();
	CHECK(Snapped.Pitch == -90.0);
	CHECK(Snapped.Roll == 0.0);
	CHECK(Snapped.Yaw == doctest::Approx(FRotator::NormalizeAxis(FMath::RadiansToDegrees(-2.0 * FMath::Atan2(NearPole.X, NearPole.W)))));
	CHECK(FMath::Abs(Snapped.Yaw - (120.0 - 170.0)) < 0.05);
	CHECK(Snapped.Quaternion().Equals(NearPole, 1e-3));
}

TEST_CASE("PhysicsTest.Core.MathClampAndNonFiniteRotator")
{
	// Clamp / Acos / Asin with finite input (UE: Asin/Acos clamp to [-1, 1], GenericPlatformMath.h:524-534).
	// NaN input is deliberately not tested: under /fp:fast (harness and UE game builds alike) MSVC does not keep IEEE
	// NaN comparison semantics, e.g. MSVC 14.44 folds Clamp(NaN, -1, 1) to -1 in the test preset.
	CHECK(FMath::Clamp(0.25, -1.0, 1.0) == 0.25);
	CHECK(FMath::Clamp(3.0, -1.0, 1.0) == 1.0);
	CHECK(FMath::Clamp(-3.0, -1.0, 1.0) == -1.0);
	CHECK(FMath::Min(2.0, 1.0) == 1.0);
	CHECK(FMath::Max(2.0, 1.0) == 2.0);
	CHECK(FMath::Acos(1.5) == 0.0);
	CHECK(FMath::Asin(-2.0) == doctest::Approx(-UE_DOUBLE_PI / 2.0));

	// FRotator::Quaternion: a non-finite result becomes Identity when checks are on (UE: DO_CHECK, UnrealMath.cpp:538-545).
	// std::fmod(inf, 360) is a library call and returns NaN regardless of /fp:fast; ContainsNaN tests the bit pattern.
	const FQuat FromInfinite = FRotator(std::numeric_limits<double>::infinity(), 0.0, 0.0).Quaternion();
#if PT_DO_CHECK
	CHECK(FromInfinite.Equals(FQuat::Identity, 0.0));
#else
	CHECK(FromInfinite.ContainsNaN());
#endif
}

TEST_CASE("PhysicsTest.Core.QuatCompositionOrder")
{
	// (A * B) applies B first, then A (UE TQuat::operator*).
	const FQuat A(FVector(0.0, 0.0, 1.0), FMath::DegreesToRadians(90.0));
	const FQuat B(FVector(1.0, 0.0, 0.0), FMath::DegreesToRadians(90.0));
	const FVector V(0.0, 1.0, 0.0);
	CHECK((A * B).RotateVector(V).Equals(A.RotateVector(B.RotateVector(V)), Tolerance));
	CHECK((A * B).RotateVector(V).Equals(FVector(0.0, 0.0, 1.0), Tolerance));   // B: Y -> Z, A leaves Z
	CHECK((B * A).RotateVector(V).Equals(FVector(-1.0, 0.0, 0.0), Tolerance));  // A: Y -> -X, B leaves X
	CHECK(A.UnrotateVector(A.RotateVector(V)).Equals(V, Tolerance));
	CHECK((A * A.Inverse()).Equals(FQuat::Identity, Tolerance));
	CHECK(A.Equals(FQuat(-A.X, -A.Y, -A.Z, -A.W), Tolerance));   // q and -q are the same rotation
	CHECK(A.GetAngle() == doctest::Approx(FMath::DegreesToRadians(90.0)));
	CHECK(A.GetRotationAxis().Equals(FVector::UpVector, Tolerance));
}

TEST_CASE("PhysicsTest.Core.TransformCompositionOrder")
{
	// A * B applies A first, then B (UE FTransform::Multiply, TransformNonVectorized.h:1308-1342).
	const FTransform A(FRotator(10.0, 20.0, 30.0), FVector(1.0, 2.0, 3.0), FVector(2.0));
	const FTransform B(FRotator(-40.0, 5.0, 60.0), FVector(-5.0, 7.0, 1.0), FVector(1.5));
	const FVector P(3.0, -4.0, 5.0);
	const FTransform AB = A * B;
	CHECK(AB.TransformPosition(P).Equals(B.TransformPosition(A.TransformPosition(P)), 1e-9));
	CHECK(AB.TransformVector(P).Equals(B.TransformVector(A.TransformVector(P)), 1e-9));
	CHECK(AB.GetScale3D().Equals(FVector(3.0), Tolerance));
	CHECK(AB.GetRotation().Equals(B.GetRotation() * A.GetRotation(), Tolerance));

	FTransform Accumulated = A;
	Accumulated *= B;
	CHECK(Accumulated.Equals(AB, Tolerance));

	// Inverse (uniform scale is exact).
	CHECK(A.Inverse().TransformPosition(A.TransformPosition(P)).Equals(P, 1e-9));
	CHECK(A.InverseTransformPosition(A.TransformPosition(P)).Equals(P, 1e-9));
	CHECK((A * A.Inverse()).Equals(FTransform::Identity, 1e-9));

	// Translation-only and rotation-only transforms.
	const FTransform Move(FVector(0.0, 0.0, 10.0));
	const FTransform Turn(FRotator(0.0, 90.0, 0.0));
	CHECK((Move * Turn).TransformPosition(FVector::ForwardVector).Equals(FVector(0.0, 1.0, 10.0), Tolerance));
	CHECK((Turn * Move).TransformPosition(FVector::ForwardVector).Equals(FVector(0.0, 1.0, 10.0), Tolerance));
	CHECK((Turn * Move).TransformPosition(FVector(0.0, 0.0, 1.0)).Equals(FVector(0.0, 0.0, 11.0), Tolerance));
	CHECK((Move * Turn).GetTranslation().Equals(FVector(0.0, 0.0, 10.0), Tolerance));
}

TEST_CASE("PhysicsTest.Core.BoxAccumulation")
{
	FBox Box;
	CHECK(Box.IsValid == 0);
	Box += FVector(1.0, -2.0, 3.0);
	Box += FVector(-1.0, 4.0, 0.0);
	CHECK(Box.IsValid == 1);
	CHECK(Box.Min == FVector(-1.0, -2.0, 0.0));
	CHECK(Box.Max == FVector(1.0, 4.0, 3.0));
	CHECK(Box.GetExtent().Equals(FVector(1.0, 3.0, 1.5), Tolerance));
	CHECK(Box.GetCenter().Equals(FVector(0.0, 1.0, 1.5), Tolerance));
}
