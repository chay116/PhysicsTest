// BodyUtilsTests.cpp - harness mass properties (03_WorkPlan M1 "BodyUtils", 02_Architecture 5.3, DL17).
// Expected values are written out by hand from the documented formulas, independently of BodyUtils.cpp.
#include "CoreMinimal.h"

#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyUtils.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>

using namespace PhysicsTest;

namespace
{
	constexpr double FloatPi = 3.1415927410125732421875;   // (double)(float)UE_PI, as Chaos' shapes evaluate it

	bool RelativelyEqual(const double A, const double B, const double Tolerance)
	{
		const double Scale = FMath::Max(FMath::Abs(A), FMath::Abs(B));
		return FMath::Abs(A - B) <= Tolerance * (Scale > 0.0 ? Scale : 1.0);
	}

	FBodySetup MakeBoxSetup(const double X, const double Y, const double Z)
	{
		FBodySetup Setup;
		Setup.BodySetupName = "Box";
		Setup.AggGeom.BoxElems.emplace_back(X, Y, Z);
		return Setup;
	}

	FBodyInstance MakeDynamicBody()
	{
		FBodyInstance Body;
		Body.bSimulatePhysics = true;
		return Body;
	}

	// RAII: p.ComNudgeAffectsInertia for one test, restored afterwards.
	struct FComNudgeCVarScope
	{
		explicit FComNudgeCVarScope(const bool bValue)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable("p.ComNudgeAffectsInertia");
			REQUIRE(Variable != nullptr);
			REQUIRE(Variable->Set(bValue ? "1" : "0", ECVF_SetByConsole));
		}
		~FComNudgeCVarScope() { Variable->ResetToDefault(); }
		IConsoleVariable* Variable = nullptr;
	};
}

TEST_CASE("PhysicsTest.BodyUtils.CubeMassesFromTheWorkPlan")
{
	const FPhysicalMaterial DefaultMaterial;   // 1 g/cm^3, RaiseMassToPower 0.75
	const FBodyInstance Body = MakeDynamicBody();

	// 100 cm cube: raw 1e6 cm^3 * 0.001 kg/cm^3 = 1000 kg -> 1000^0.75 = 177.83 kg; I = M (a^2 + a^2) / 12.
	const FMassProperties Cube100 = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &DefaultMaterial, EMassRule::UEBodyUtils);
	const double M100 = std::pow(1000.0, 0.75);
	CHECK(RelativelyEqual(Cube100.Mass, M100, 1e-12));
	CHECK(FMath::Abs(Cube100.Mass - 177.83) < 0.005);
	CHECK(RelativelyEqual(Cube100.InertiaTensor.X, M100 * (100.0 * 100.0 + 100.0 * 100.0) / 12.0, 1e-12));
	CHECK(FMath::Abs(Cube100.InertiaTensor.X - 296380.0) < 0.5);
	CHECK(Cube100.InertiaTensor.X == Cube100.InertiaTensor.Y);
	CHECK(Cube100.InertiaTensor.Y == Cube100.InertiaTensor.Z);
	CHECK(Cube100.CenterOfMass == FVector::ZeroVector);
	CHECK(Cube100.RotationOfMass.Equals(FQuat::Identity, 0.0));
	CHECK(Cube100.Volume == 1.0e6);

	// 50 cm cube: 125 kg -> 37.384 kg. 10 cm cube: 1 kg -> 1 kg.
	const FMassProperties Cube50 = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(50, 50, 50), &DefaultMaterial, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Cube50.Mass, std::pow(125.0, 0.75), 1e-12));
	CHECK(FMath::Abs(Cube50.Mass - 37.384) < 0.0005);
	const FMassProperties Cube10 = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(10, 10, 10), &DefaultMaterial, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Cube10.Mass, 1.0, 1e-12));
}

TEST_CASE("PhysicsTest.BodyUtils.NoMaterialMeansOneKgPerCm3")
{
	// BodyUtils.cpp:37-45: no material -> 1.0 kg/cm^3 (and the exponent stays 0.75). 10 cm cube -> 1000 kg -> 177.83 kg.
	CHECK(BodyUtils::GetBodyInstanceDensity(nullptr) == 1.0);
	const FMassProperties Mass = BodyUtils::ComputeMassProperties(MakeDynamicBody(), MakeBoxSetup(10, 10, 10), nullptr, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Mass.Mass, std::pow(1000.0, 0.75), 1e-12));
	CHECK(FMath::Abs(Mass.Mass - 177.83) < 0.005);

	// Density floor: nothing weighs less than hydrogen, 0.09 kg/m^3 = 9e-8 kg/cm^3.
	FPhysicalMaterial Light;
	Light.Density = 1e-6f;   // g/cm^3 -> 1e-9 kg/cm^3
	CHECK(BodyUtils::GetBodyInstanceDensity(&Light) == doctest::Approx(9e-8).epsilon(1e-12));
	FPhysicalMaterial Steel;
	Steel.Density = 7.85f;
	CHECK(BodyUtils::GetBodyInstanceDensity(&Steel) == doctest::Approx(7.85 / 1000.0).epsilon(1e-7));
}

TEST_CASE("PhysicsTest.BodyUtils.OverrideMassFloor")
{
	// bOverrideMass: M' = max(MassInKgOverride, 0.001) - 0.0001 kg becomes 0.001 kg; inertia follows M'/M.
	FBodyInstance Body = MakeDynamicBody();
	Body.bOverrideMass = true;
	Body.MassInKgOverride = 0.0001f;
	const FPhysicalMaterial Material;
	const FMassProperties Mass = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(10, 10, 10), &Material, EMassRule::UEBodyUtils);
	CHECK(Mass.Mass == doctest::Approx(0.001).epsilon(1e-12));
	CHECK(RelativelyEqual(Mass.InertiaTensor.X, 0.001 * 200.0 / 12.0, 1e-12));

	Body.MassInKgOverride = 10.0f;
	CHECK(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(10, 10, 10), &Material, EMassRule::UEBodyUtils).Mass == doctest::Approx(10.0));

	// !bOverrideMass also has the 1 g floor: max(MassScale * M^p, 0.001).
	FBodyInstance Tiny = MakeDynamicBody();
	Tiny.MassScale = 1e-6f;
	CHECK(BodyUtils::ComputeMassProperties(Tiny, MakeBoxSetup(10, 10, 10), &Material, EMassRule::UEBodyUtils).Mass == doctest::Approx(0.001).epsilon(1e-12));
}

TEST_CASE("PhysicsTest.BodyUtils.MassRuleAxis")
{
	const FPhysicalMaterial Material;
	FBodyInstance Body = MakeDynamicBody();
	// EMassRule::Physical: exponent 1 (DL17).
	CHECK(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::Physical).Mass == doctest::Approx(1000.0).epsilon(1e-14));
	// MassScale applies to both rules.
	Body.MassScale = 2.0f;
	CHECK(RelativelyEqual(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::UEBodyUtils).Mass,
		2.0 * std::pow(1000.0, 0.75), 1e-12));
	// RaiseMassToPower is clamped to [1e-4, 1] (BodyUtils.cpp:63).
	FPhysicalMaterial Heavy;
	Heavy.RaiseMassToPower = 2.0f;
	Body.MassScale = 1.0f;
	CHECK(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Heavy, EMassRule::UEBodyUtils).Mass == doctest::Approx(1000.0));
	FPhysicalMaterial Flat;
	Flat.RaiseMassToPower = 0.0f;
	CHECK(RelativelyEqual(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Flat, EMassRule::UEBodyUtils).Mass,
		std::pow(1000.0, 1e-4), 1e-6));
}

TEST_CASE("PhysicsTest.BodyUtils.InertiaScaleThenComNudgeOrder")
{
	// Steps 4-8 with a non-unit InertiaTensorScale AND a non-zero COMNudge, hand-computed in the documented order
	// (BodyUtils.cpp:53-112): mass rule -> inertia * M'/M -> COM += nudge -> ScaleInertia(no mass) -> + M' n_i^2.
	FBodyInstance Body = MakeDynamicBody();
	Body.InertiaTensorScale = FVector(2.0, 1.0, 0.5);
	Body.COMNudge = FVector(4.0, -3.0, 10.0);   // n_x = 5 would make I_xx coincide with the unscaled value (0.75 c^2 / 12 = 25)
	const FPhysicalMaterial Material;
	const FMassProperties Result = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 50, 20), &Material, EMassRule::UEBodyUtils);

	const double M = 100.0 * 50.0 * 20.0 * 0.001;   // 100 kg
	const double MPrime = std::pow(M, 0.75);
	const double Ratio = MPrime / M;
	const double I0x = Ratio * M / 12.0 * (50.0 * 50.0 + 20.0 * 20.0);
	const double I0y = Ratio * M / 12.0 * (100.0 * 100.0 + 20.0 * 20.0);
	const double I0z = Ratio * M / 12.0 * (100.0 * 100.0 + 50.0 * 50.0);
	// ScaleInertia (Utilities.h:537-560): "equivalent box" half-sums times |S|^2.
	const double Half = 0.5 * (I0x + I0y + I0z);
	const double Sx = (Half - I0x) * 4.0, Sy = (Half - I0y) * 1.0, Sz = (Half - I0z) * 0.25;
	const double I7x = Sy + Sz, I7y = Sx + Sz, I7z = Sx + Sy;
	// p.ComNudgeAffectsInertia: diagonal terms exactly as UE writes them.
	const double I8x = I7x + MPrime * 16.0, I8y = I7y + MPrime * 9.0, I8z = I7z + MPrime * 100.0;

	CHECK(RelativelyEqual(Result.Mass, MPrime, 1e-9));
	CHECK(Result.CenterOfMass.Equals(FVector(4.0, -3.0, 10.0), 1e-12));
	CHECK(Result.RotationOfMass.Equals(FQuat::Identity, 1e-12));
	CHECK(RelativelyEqual(Result.InertiaTensor.X, I8x, 1e-9));
	CHECK(RelativelyEqual(Result.InertiaTensor.Y, I8y, 1e-9));
	CHECK(RelativelyEqual(Result.InertiaTensor.Z, I8z, 1e-9));

	// The test discriminates the order: nudge before scale would give a clearly different tensor.
	const double Hb = 0.5 * ((I0x + MPrime * 16.0) + (I0y + MPrime * 9.0) + (I0z + MPrime * 100.0));
	const double WrongX = (Hb - (I0y + MPrime * 9.0)) * 1.0 + (Hb - (I0z + MPrime * 100.0)) * 0.25;
	const double WrongZ = (Hb - (I0x + MPrime * 16.0)) * 4.0 + (Hb - (I0y + MPrime * 9.0)) * 1.0;
	CHECK_FALSE(RelativelyEqual(Result.InertiaTensor.X, WrongX, 1e-3));
	CHECK_FALSE(RelativelyEqual(Result.InertiaTensor.Z, WrongZ, 1e-3));
	// ... and every component differs from the unscaled / un-nudged tensor.
	CHECK_FALSE(RelativelyEqual(Result.InertiaTensor.X, I0x, 1e-3));
	CHECK_FALSE(RelativelyEqual(Result.InertiaTensor.Y, I0y, 1e-3));
	CHECK_FALSE(RelativelyEqual(Result.InertiaTensor.Z, I0z, 1e-3));

	// An InertiaTensorScale within 1e-3 of one is skipped (IsNearlyZero(1e-3f), BodyUtils.cpp:90).
	FBodyInstance NearlyOne = MakeDynamicBody();
	NearlyOne.InertiaTensorScale = FVector(1.0005, 1.0, 0.9995);
	const FMassProperties Unscaled = BodyUtils::ComputeMassProperties(NearlyOne, MakeBoxSetup(100, 50, 20), &Material, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Unscaled.InertiaTensor.X, I0x, 1e-12));
}

TEST_CASE("PhysicsTest.BodyUtils.ComNudgeCVarAndScale")
{
	const FPhysicalMaterial Material;
	FBodyInstance Body = MakeDynamicBody();
	Body.COMNudge = FVector(0.0, 0.0, 10.0);
	const double MPrime = std::pow(1000.0, 0.75);
	const double I0 = MPrime * 20000.0 / 12.0;
	{
		const FMassProperties With = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::UEBodyUtils);
		CHECK(RelativelyEqual(With.InertiaTensor.Z, I0 + MPrime * 100.0, 1e-12));
		CHECK(RelativelyEqual(With.InertiaTensor.X, I0, 1e-12));   // UE adds n_x^2 to I_xx only (not n_y^2 + n_z^2)
	}
	{
		const FComNudgeCVarScope Off(false);
		CHECK_FALSE(PhysicsCVars::GetComNudgeAffectsInertia());
		const FMassProperties Without = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::UEBodyUtils);
		CHECK(RelativelyEqual(Without.InertiaTensor.Z, I0, 1e-12));
		CHECK(Without.CenterOfMass.Equals(FVector(0.0, 0.0, 10.0), 1e-12));
	}
	CHECK(PhysicsCVars::GetComNudgeAffectsInertia());

	// The body scale scales the geometry and the nudge (MassModifierTransform = (I, 0, Scale3D), BodyInstance.cpp:3490).
	Body.Scale3D = FVector(2.0, 1.0, 1.0);
	const FMassProperties Scaled = BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Scaled.Mass, std::pow(2000.0, 0.75), 1e-12));
	CHECK(Scaled.CenterOfMass.Equals(FVector(0.0, 0.0, 10.0), 1e-12));
	Body.COMNudge = FVector(10.0, 0.0, 0.0);
	CHECK(BodyUtils::ComputeMassProperties(Body, MakeBoxSetup(100, 100, 100), &Material, EMassRule::UEBodyUtils).CenterOfMass.Equals(FVector(20.0, 0.0, 0.0), 1e-12));

	// BuildScale3D is metadata, as in UE (the scale RescaleSimpleCollision already baked into AggGeom): not applied.
	FBodySetup Built = MakeBoxSetup(100, 100, 100);
	Built.BuildScale3D = FVector(1.0, 1.0, 3.0);
	FBodyInstance Plain = MakeDynamicBody();
	CHECK(RelativelyEqual(BodyUtils::ComputeMassProperties(Plain, Built, &Material, EMassRule::UEBodyUtils).Mass, std::pow(1000.0, 0.75), 1e-12));
	CHECK(Built.GetScaledVolume(FVector(1.0)) == doctest::Approx(1.0e6).epsilon(1e-12));
	CHECK(Built.GetScaledVolume(FVector(2.0, 1.0, 1.0)) == doctest::Approx(2.0e6).epsilon(1e-12));
}

TEST_CASE("PhysicsTest.BodyUtils.SphereAndCapsuleAsChaos")
{
	const FPhysicalMaterial Material;
	const FBodyInstance Body = MakeDynamicBody();

	// Sphere r = 50: V = 4/3 * (float)pi * r^3 (Sphere.h:213-217), I = 2/5 m r^2.
	FBodySetup SphereSetup;
	SphereSetup.AggGeom.SphereElems.emplace_back(50.0);
	const FMassProperties Uniform = BodyUtils::ComputeUniformMassProperties(SphereSetup, FVector(1.0), 0.001);
	const double SphereVolume = 4.0 / 3.0 * FloatPi * 125000.0;
	CHECK(RelativelyEqual(Uniform.Volume, SphereVolume, 1e-14));
	CHECK_FALSE(RelativelyEqual(Uniform.Volume, 4.0 / 3.0 * UE_DOUBLE_PI * 125000.0, 1e-10));   // float pi, not double pi
	CHECK(RelativelyEqual(Uniform.InertiaTensor.X, 0.4 * Uniform.Mass * 2500.0, 1e-14));
	const FMassProperties Sphere = BodyUtils::ComputeMassProperties(Body, SphereSetup, &Material, EMassRule::UEBodyUtils);
	CHECK(RelativelyEqual(Sphere.Mass, std::pow(SphereVolume * 0.001, 0.75), 1e-12));

	// Capsule R 25, L 100 (S03): V = pi R^2 (H + 4/3 R) with Chaos' float constants (Capsule.h:279-297).
	FBodySetup CapsuleSetup;
	CapsuleSetup.AggGeom.SphylElems.emplace_back(25.0, 100.0);
	const FMassProperties Capsule = BodyUtils::ComputeUniformMassProperties(CapsuleSetup, FVector(1.0), 0.001);
	const double R = 25.0, H = 100.0;
	const double CapsuleVolume = FloatPi * R * R * (H + 1.33333337306976318359375 * R);
	const double CapsuleMass = CapsuleVolume * 0.001;
	const double Diag12 = CapsuleMass * (5.0 * H * H * H + 20.0 * H * H * R + 45.0 * H * R * R + 32.0 * R * R * R) / (60.0 * H + 80.0 * R);
	const double Diag3 = CapsuleMass * (R * R * (15.0 * H + 16.0 * R)) / (30.0 * H + 40.0 * R);
	CHECK(RelativelyEqual(Capsule.Volume, CapsuleVolume, 1e-14));
	CHECK(RelativelyEqual(Capsule.InertiaTensor.X, Diag12, 1e-14));
	CHECK(RelativelyEqual(Capsule.InertiaTensor.Y, Diag12, 1e-14));
	CHECK(RelativelyEqual(Capsule.InertiaTensor.Z, Diag3, 1e-14));
	CHECK(Capsule.RotationOfMass.Equals(FQuat::Identity, 1e-15));

	// Lying capsule (Pitch 90): the same principal moments; the world-space tensor has Diag3 about world X.
	CapsuleSetup.AggGeom.SphylElems[0].Rotation = FRotator(90.0, 0.0, 0.0);
	const FMassProperties Lying = BodyUtils::ComputeUniformMassProperties(CapsuleSetup, FVector(1.0), 0.001);
	const FVector WorldX = Lying.RotationOfMass.RotateVector(FVector(1.0, 0.0, 0.0));
	const FVector WorldY = Lying.RotationOfMass.RotateVector(FVector(0.0, 1.0, 0.0));
	const FVector WorldZ = Lying.RotationOfMass.RotateVector(FVector(0.0, 0.0, 1.0));
	// I_world,xx = sum_k I_k (e_x . axis_k)^2
	const double Ixx = Lying.InertiaTensor.X * FMath::Square(WorldX.X) + Lying.InertiaTensor.Y * FMath::Square(WorldY.X) + Lying.InertiaTensor.Z * FMath::Square(WorldZ.X);
	const double Izz = Lying.InertiaTensor.X * FMath::Square(WorldX.Z) + Lying.InertiaTensor.Y * FMath::Square(WorldY.Z) + Lying.InertiaTensor.Z * FMath::Square(WorldZ.Z);
	CHECK(RelativelyEqual(Ixx, Diag3, 1e-9));
	CHECK(RelativelyEqual(Izz, Diag12, 1e-9));
}

TEST_CASE("PhysicsTest.BodyUtils.ParallelAxisAndRotatedElements")
{
	// Two 10 cm cubes at x = -50 / +50: each 1 kg raw. COM 0; I_xx = 2 * (1/12)(200); I_yy = I_zz = 2 * (1/12)(200) + 2 * 1 * 50^2.
	FBodySetup Setup;
	Setup.AggGeom.BoxElems.emplace_back(10.0);
	Setup.AggGeom.BoxElems.emplace_back(10.0);
	Setup.AggGeom.BoxElems[0].Center = FVector(-50.0, 0.0, 0.0);
	Setup.AggGeom.BoxElems[1].Center = FVector(50.0, 0.0, 0.0);
	const FMassProperties Uniform = BodyUtils::ComputeUniformMassProperties(Setup, FVector(1.0), 0.001);
	CHECK(Uniform.Mass == doctest::Approx(2.0).epsilon(1e-14));
	CHECK(Uniform.CenterOfMass.Equals(FVector::ZeroVector, 1e-12));
	CHECK(RelativelyEqual(Uniform.InertiaTensor.X, 2.0 * 200.0 / 12.0, 1e-12));
	CHECK(RelativelyEqual(Uniform.InertiaTensor.Y, 2.0 * 200.0 / 12.0 + 2.0 * 2500.0, 1e-12));
	CHECK(RelativelyEqual(Uniform.InertiaTensor.Z, 2.0 * 200.0 / 12.0 + 2.0 * 2500.0, 1e-12));

	// Off-centre: the second cube at (50, 0, 0) only -> COM moves to (25, 0, 0) for equal masses at (0,0,0)/(50,0,0).
	Setup.AggGeom.BoxElems[0].Center = FVector::ZeroVector;
	const FMassProperties Shifted = BodyUtils::ComputeUniformMassProperties(Setup, FVector(1.0), 0.001);
	CHECK(Shifted.CenterOfMass.Equals(FVector(25.0, 0.0, 0.0), 1e-12));

	// bContributeToMass = false removes an element.
	Setup.AggGeom.BoxElems[1].bContributeToMass = false;
	CHECK(BodyUtils::ComputeUniformMassProperties(Setup, FVector(1.0), 0.001).Mass == doctest::Approx(1.0).epsilon(1e-14));

	// A rotated (yaw 30) box: the principal moments are the box's, and R diag R^T reproduces the world tensor
	// Rbox diag0 Rbox^T (Jacobi diagonalisation; the axis order may differ from Chaos' solver, the tensor may not).
	FBodySetup Rotated = MakeBoxSetup(100.0, 50.0, 20.0);
	Rotated.AggGeom.BoxElems[0].Rotation = FRotator(0.0, 30.0, 0.0);
	const FMassProperties R = BodyUtils::ComputeUniformMassProperties(Rotated, FVector(1.0), 0.001);
	const double M = 100.0;
	const FVector Diag0(M / 12.0 * 2900.0, M / 12.0 * 10400.0, M / 12.0 * 12500.0);
	const FQuat BoxRotation = FRotator(0.0, 30.0, 0.0).Quaternion();
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Column = 0; Column < 3; ++Column)
		{
			// e_r^T (Q diag Q^T) e_c = sum_k diag_k (Q e_k)_r (Q e_k)_c
			double Expected = 0.0, Actual = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				FVector Ek;
				Ek[K] = 1.0;
				const FVector A = BoxRotation.RotateVector(Ek);
				const FVector B = R.RotationOfMass.RotateVector(Ek);
				Expected += Diag0[K] * A[Row] * A[Column];
				Actual += R.InertiaTensor[K] * B[Row] * B[Column];
			}
			CHECK(FMath::Abs(Actual - Expected) <= 1e-9 * Diag0.Z);
		}
	}
	CHECK(R.RotationOfMass.IsNormalized());
}

TEST_CASE("PhysicsTest.BodyUtils.NoContributingShape")
{
	// Chaos' fallback when nothing contributes: a 50 cm sphere of 1 g/cc, 523 kg, 5.24e5 kg*cm^2
	// (MassProperties.cpp:481-497), logged as an Error; the mass rule applies on top as usual. UE's FBodyInstance would
	// skip the mass update instead (02 5.2 #22).
	FBodySetup Setup;
	Setup.AggGeom.BoxElems.emplace_back(100.0);
	Setup.AggGeom.BoxElems[0].bContributeToMass = false;
	const FMassProperties Uniform = BodyUtils::ComputeUniformMassProperties(Setup, FVector(1.0), 0.001);
	CHECK(Uniform.Mass == 523.0);
	CHECK(Uniform.InertiaTensor.X == 5.24e5);
	CHECK(Uniform.Volume == 523000.0);
}
