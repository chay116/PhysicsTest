// PhysicsTypesTests.cpp - the PhysicsCore data model: UE field names / defaults, FKBoxElem / FKSphylElem conventions,
// combine rules, the 32-channel collision filter, constraint presets (03_WorkPlan M1, 02_Architecture 5).
#include "CoreMinimal.h"

#include "HAL/IConsoleManager.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintUtils.h"
#include "PhysicsEngine/PhysicsSettings.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

using namespace PhysicsTest;

TEST_CASE("PhysicsTest.Types.BoxElemFullExtent")
{
	// X / Y / Z are FULL extents (BoxElem.h:35-44): a 100 x 50 x 20 box spans +-50, +-25, +-10.
	FKBoxElem Box(100.0, 50.0, 20.0);
	CHECK(Box.GetScaledVolume(FVector(1.0)) == 100000.0);
	CHECK(Box.GetHalfExtents() == FVector(50.0, 25.0, 10.0));
	FKAggregateGeom Geom;
	Geom.BoxElems.push_back(Box);
	const FBox Bounds = Geom.CalcAABB(FTransform::Identity);
	CHECK(Bounds.Min.Equals(FVector(-50.0, -25.0, -10.0), 1e-12));
	CHECK(Bounds.Max.Equals(FVector(50.0, 25.0, 10.0), 1e-12));

	// Rotation is an FRotator; GetTransform converts it once (UE FTransform(Rotation, Center)).
	Box.Center = FVector(1.0, 2.0, 3.0);
	Box.Rotation = FRotator(0.0, 90.0, 0.0);
	const FTransform ElemTM = Box.GetTransform();
	CHECK(ElemTM.GetRotation().Equals(FRotator(0.0, 90.0, 0.0).Quaternion(), 1e-15));
	CHECK(ElemTM.GetTranslation() == FVector(1.0, 2.0, 3.0));
	Geom.BoxElems[0] = Box;
	const FBox Rotated = Geom.CalcAABB(FTransform::Identity);
	CHECK(Rotated.GetExtent().Equals(FVector(25.0, 50.0, 10.0), 1e-9));   // yaw 90 swaps X and Y

	// Scaling (UE GetFinalScaled): extents * |S|, centre * S.
	const FKBoxElem Scaled = Box.GetFinalScaled(FVector(2.0, 1.0, 0.5));
	CHECK(Scaled.X == 200.0);
	CHECK(Scaled.Y == 50.0);
	CHECK(Scaled.Z == 10.0);
	CHECK(Scaled.Center.Equals(FVector(2.0, 2.0, 1.5), 1e-12));
	CHECK(Scaled.Rotation.Equals(Box.Rotation, 1e-9));
}

TEST_CASE("PhysicsTest.Types.SphylElemLengthIsLocalZSegment")
{
	// Length = segment along LOCAL Z; total height Length + 2R (SphylElem.h:36-40). S03.CapsuleRest: R25 L100 stands
	// 150 cm tall (COM 75 cm above the floor) and lies 50 cm tall (COM 25 cm).
	FKSphylElem Capsule(25.0, 100.0);
	FVector A, B;
	Capsule.GetSegment(A, B);
	CHECK(A.Equals(FVector(0.0, 0.0, -50.0), 1e-12));
	CHECK(B.Equals(FVector(0.0, 0.0, 50.0), 1e-12));
	FKAggregateGeom Geom;
	Geom.SphylElems.push_back(Capsule);
	const FBox Standing = Geom.CalcAABB(FTransform::Identity);
	CHECK(Standing.Min.Z == doctest::Approx(-75.0));
	CHECK(Standing.Max.Z == doctest::Approx(75.0));
	CHECK(Standing.GetExtent().X == doctest::Approx(25.0));

	// The element's Rotation turns the local Z axis: lying on its side, the segment runs along world X.
	Capsule.Rotation = FRotator(90.0, 0.0, 0.0);
	Capsule.GetSegment(A, B);
	CHECK(B.Equals(FRotator(90.0, 0.0, 0.0).RotateVector(FVector(0.0, 0.0, 50.0)), 1e-12));
	CHECK(FMath::Abs(B.X) == doctest::Approx(50.0));
	CHECK(FMath::Abs(B.Z) < 1e-9);
	Geom.SphylElems[0] = Capsule;
	CHECK(Geom.CalcAABB(FTransform::Identity).GetExtent().Equals(FVector(75.0, 25.0, 25.0), 1e-9));

	// UE scaling helpers and their clamps (BodySetup.cpp:2167-2182).
	const FKSphylElem Upright(25.0, 100.0);
	CHECK(Upright.GetScaledHalfLength(FVector(1.0)) == doctest::Approx(75.0));
	CHECK(Upright.GetScaledRadius(FVector(2.0, 1.0, 1.0)) == doctest::Approx(50.0));        // max(|Sx|, |Sy|)
	CHECK(Upright.GetScaledCylinderLength(FVector(2.0, 1.0, 1.0)) == doctest::Approx(50.0)); // 2 (75 - 50)
	CHECK(FKSphylElem(25.0, 0.0).GetScaledCylinderLength(FVector(1.0)) == doctest::Approx(0.1));   // >= 0.1
	const FKSphylElem Scaled = Upright.GetFinalScaled(FVector(1.0, 1.0, 2.0));
	CHECK(Scaled.Radius == doctest::Approx(25.0));
	CHECK(Scaled.Length == doctest::Approx(250.0));   // half length (100 + 50) * 2 / 2 = 150 -> 2 (150 - 25)
}

TEST_CASE("PhysicsTest.Types.SphereElemScaling")
{
	const FKSphereElem Sphere(50.0);
	const FKSphereElem Scaled = Sphere.GetFinalScaled(FVector(2.0, 3.0, 0.5));
	CHECK(Scaled.Radius == doctest::Approx(25.0));   // min |S|
	double MinScale = 0.0, MinScaleAbs = 0.0;
	FVector Abs;
	SetupNonUniformHelper(FVector(0.0), MinScale, MinScaleAbs, Abs);   // nearly zero -> 0.1 (PhysicsInterfaceTypesCore.h:113-117)
	CHECK(MinScaleAbs == doctest::Approx(0.1));
	SetupNonUniformHelper(FVector(-2.0, -3.0, -4.0), MinScale, MinScaleAbs, Abs);
	CHECK(MinScale == doctest::Approx(-2.0));   // all negative -> negative
}

TEST_CASE("PhysicsTest.Types.BodyInstanceDefaultsMatchUE")
{
	const FBodyInstance Body;
	// BodyInstanceCore.cpp:13-24
	CHECK_FALSE(Body.bSimulatePhysics);
	CHECK_FALSE(Body.bOverrideMass);
	CHECK(Body.bEnableGravity);
	CHECK_FALSE(Body.bGyroscopicTorqueEnabled);
	CHECK(Body.bStartAwake);
	// BodyInstance.cpp:357-418
	CHECK(Body.PositionSolverIterationCount == 8);
	CHECK(Body.VelocitySolverIterationCount == 2);
	CHECK(Body.ProjectionSolverIterationCount == 1);
	CHECK(Body.MassInKgOverride == 100.0f);
	CHECK(Body.LinearDamping == 0.01f);
	CHECK(Body.AngularDamping == 0.0f);
	CHECK(Body.MassScale == 1.0f);
	CHECK(Body.InertiaTensorScale == FVector(1.0));
	CHECK(Body.COMNudge == FVector::ZeroVector);
	CHECK(Body.bInertiaConditioning);
	CHECK_FALSE(Body.bUseCCD);
	CHECK(Body.SleepFamily == ESleepFamily::Normal);
	CHECK(Body.CustomSleepThresholdMultiplier == 1.0f);
	CHECK(Body.StabilizationThresholdMultiplier == 1.0f);
	CHECK(Body.MaxAngularVelocity == 3600.0f);   // = UPhysicsSettings::MaxAngularVelocity
	CHECK(Body.MaxDepenetrationVelocity == 0.0f);
	CHECK(Body.CollisionFilter.ObjectType == ECC_WorldStatic);

	// Mobility replacement: kinematic = !bStatic && !bSimulatePhysics.
	FBodyInstance Kinematic;
	CHECK(Kinematic.IsKinematic());
	CHECK(Kinematic.GetInitialObjectState() == EObjectStateType::Kinematic);
	Kinematic.bSimulatePhysics = true;
	CHECK(Kinematic.IsDynamic());
	CHECK(Kinematic.GetInitialObjectState() == EObjectStateType::Dynamic);
	Kinematic.bStatic = true;
	CHECK(Kinematic.IsStatic());
	CHECK_FALSE(Kinematic.IsDynamic());
	CHECK(Kinematic.GetInitialObjectState() == EObjectStateType::Static);

	FBodyInstance Sleepy;
	CHECK(Sleepy.GetSleepThresholdMultiplier() == 1.0f);
	Sleepy.SleepFamily = ESleepFamily::Sensitive;
	CHECK(Sleepy.GetSleepThresholdMultiplier() == 1.0f / 20.0f);   // p.SensitiveSleepThresholdMultiplier, BodyInstance.cpp:106-110
	{
		IConsoleVariable* Sensitive = IConsoleManager::Get().FindConsoleVariable("p.SensitiveSleepThresholdMultiplier");
		REQUIRE(Sensitive != nullptr);
		CHECK(Sensitive->GetDefaultString() == "0.05");
		REQUIRE(Sensitive->Set("0.25", ECVF_SetByConsole));
		CHECK(Sleepy.GetSleepThresholdMultiplier() == 0.25f);
		Sensitive->ResetToDefault();
		CHECK(Sleepy.GetSleepThresholdMultiplier() == 1.0f / 20.0f);
	}
	Sleepy.SleepFamily = ESleepFamily::Custom;
	Sleepy.CustomSleepThresholdMultiplier = 3.0f;
	CHECK(Sleepy.GetSleepThresholdMultiplier() == 3.0f);

	const FPhysicsSettings Project;
	CHECK(Body.GetMaxAngularVelocityInRadians(Project) == doctest::Approx(62.83185307179586));
	FBodyInstance Override;
	Override.bOverrideMaxAngularVelocity = true;
	Override.MaxAngularVelocity = 180.0f;
	CHECK(Override.GetMaxAngularVelocityInRadians(Project) == doctest::Approx(UE_DOUBLE_PI));
}

TEST_CASE("PhysicsTest.Types.EnumValuesCrossTheAbi")
{
	// Chaos::EObjectStateType (Chaos/Particle/ObjectState.h:9-18) and EFrictionCombineMode (= PxCombineMode) values.
	CHECK(static_cast<int>(EObjectStateType::Uninitialized) == 0);
	CHECK(static_cast<int>(EObjectStateType::Sleeping) == 1);
	CHECK(static_cast<int>(EObjectStateType::Kinematic) == 2);
	CHECK(static_cast<int>(EObjectStateType::Static) == 3);
	CHECK(static_cast<int>(EObjectStateType::Dynamic) == 4);
	CHECK(static_cast<int>(EFrictionCombineMode::Average) == 0);
	CHECK(static_cast<int>(EFrictionCombineMode::Min) == 1);
	CHECK(static_cast<int>(EFrictionCombineMode::Multiply) == 2);
	CHECK(static_cast<int>(EFrictionCombineMode::Max) == 3);
	CHECK(NumCollisionChannels == 32u);
}

TEST_CASE("PhysicsTest.Types.PhysicalMaterialDefaultsAndCombine")
{
	const FPhysicalMaterial Default;   // PhysicalMaterial.cpp:42-60
	CHECK(Default.Friction == 0.7f);
	CHECK(Default.StaticFriction == 0.0f);
	CHECK(Default.Restitution == 0.3f);
	CHECK(Default.Density == 1.0f);
	CHECK(Default.RaiseMassToPower == 0.75f);
	CHECK(Default.SleepLinearVelocityThreshold == 1.0f);
	CHECK(Default.SleepAngularVelocityThreshold == 0.05f);
	CHECK(Default.SleepCounterThreshold == 4);
	CHECK(Default.FrictionCombineMode == EFrictionCombineMode::Average);
	CHECK_FALSE(Default.bOverrideFrictionCombineMode);

	// The larger mode wins.
	CHECK(FPhysicalMaterial::ResolveCombineMode(EFrictionCombineMode::Average, EFrictionCombineMode::Max) == EFrictionCombineMode::Max);
	CHECK(FPhysicalMaterial::ResolveCombineMode(EFrictionCombineMode::Multiply, EFrictionCombineMode::Min) == EFrictionCombineMode::Multiply);

	// Chaos static-friction rule (DL41): per material max(F, SF) first, then the friction mode, then max with dynamic.
	// DL41's example: A F=0.5 SF=0, B F=0.5 SF=1.0, Average -> static 0.75 (the review's formula gave 0.5).
	const FPhysicsSettings Project;
	FPhysicalMaterial A;
	A.Friction = 0.5f;
	A.StaticFriction = 0.0f;
	FPhysicalMaterial B;
	B.Friction = 0.5f;
	B.StaticFriction = 1.0f;
	double Dynamic = 0.0, Static = 0.0;
	FPhysicalMaterial::CombinePairFriction(A, B, Project, Dynamic, Static);
	CHECK(Dynamic == doctest::Approx(0.5));
	CHECK(Static == doctest::Approx(0.75));

	// Material override beats the project mode; Multiply: dynamic 0.25, static max(0.5 * 1.0, 0.25) = 0.5.
	B.FrictionCombineMode = EFrictionCombineMode::Multiply;
	B.bOverrideFrictionCombineMode = true;
	FPhysicalMaterial::CombinePairFriction(A, B, Project, Dynamic, Static);
	CHECK(Dynamic == doctest::Approx(0.25));
	CHECK(Static == doctest::Approx(0.5));

	// SF <= F on both: static = dynamic (Jolt / Bullet use the dynamic value only, DL41).
	FPhysicalMaterial C;
	FPhysicalMaterial D;
	D.Friction = 0.3f;
	FPhysicalMaterial::CombinePairFriction(C, D, Project, Dynamic, Static);
	CHECK(Static == doctest::Approx(Dynamic));

	FPhysicalMaterial Bouncy;
	Bouncy.Restitution = 0.8f;
	FPhysicalMaterial Dead;
	Dead.Restitution = 0.0f;
	CHECK(FPhysicalMaterial::CombinePairRestitution(Bouncy, Dead, Project) == doctest::Approx(0.4));   // G4 Average
	Dead.RestitutionCombineMode = EFrictionCombineMode::Max;
	Dead.bOverrideRestitutionCombineMode = true;
	CHECK(FPhysicalMaterial::CombinePairRestitution(Bouncy, Dead, Project) == doctest::Approx(0.8));   // G4 Max
}

TEST_CASE("PhysicsTest.Types.CollisionFilterTruthTable32")
{
	// 32 channels, uint32 masks, UE's mutual-block test (CollisionFilterData.cpp:263-272): A and B collide iff B blocks
	// A's channel and A blocks B's channel. Every (channel, channel) pair is checked against that formula for random
	// masks, plus the hand-picked edges.
	FRandomStream Random(1234);
	uint32 Masks[NumCollisionChannels];
	for (uint32& Mask : Masks)
	{
		Mask = Random.GetUnsignedInt();
	}
	Masks[0] = 0xFFFFFFFFu;   // blocks everything
	Masks[1] = 0u;            // blocks nothing
	int32 Collisions = 0;
	for (uint32 I = 0; I < NumCollisionChannels; ++I)
	{
		for (uint32 J = 0; J < NumCollisionChannels; ++J)
		{
			FBodyCollisionFilter A;
			A.ObjectType = static_cast<ECollisionChannel>(I);
			A.BlockMask = Masks[I];
			FBodyCollisionFilter B;
			B.ObjectType = static_cast<ECollisionChannel>(J);
			B.BlockMask = Masks[J];
			const bool bExpected = ((Masks[J] >> I) & 1u) != 0 && ((Masks[I] >> J) & 1u) != 0;
			const bool bActual = A.CanCollideWith(B);
			if (bActual != bExpected)
			{
				FAIL_CHECK("channel pair " << I << ", " << J);
			}
			CHECK(bActual == B.CanCollideWith(A));   // symmetric
			Collisions += bActual ? 1 : 0;
		}
	}
	CHECK(Collisions > 0);
	CHECK(Collisions < static_cast<int32>(NumCollisionChannels * NumCollisionChannels));

	// Channel 31 is reachable (uint32 mask, no off-by-one).
	FBodyCollisionFilter High;
	High.ObjectType = 31;
	High.BlockMask = 1u << 31;
	CHECK(High.CanCollideWith(High));
	High.BlockMask = 0x7FFFFFFFu;
	CHECK_FALSE(High.CanCollideWith(High));

	// Channels 32..255 are invalid and never alias to ObjectType mod 32 (32 would otherwise behave like channel 0).
	FBodyCollisionFilter Zero;
	Zero.ObjectType = 0;
	CHECK(Zero.IsValid());
	for (const uint32 Channel : { 32u, 33u, 63u, 255u })
	{
		CAPTURE(Channel);
		FBodyCollisionFilter Invalid;
		Invalid.ObjectType = static_cast<ECollisionChannel>(Channel);
		CHECK_FALSE(Invalid.IsValid());
		CHECK(Invalid.GetChannelMask() == 0u);
		CHECK_FALSE(Invalid.CanCollideWith(Zero));   // both block everything, but the invalid channel matches nothing
		CHECK_FALSE(Zero.CanCollideWith(Invalid));
	}

	// Physics collision must be enabled on both sides.
	FBodyCollisionFilter Query;
	Query.CollisionEnabled = ECollisionEnabled::QueryOnly;
	FBodyCollisionFilter Physics;
	Physics.CollisionEnabled = ECollisionEnabled::PhysicsOnly;
	CHECK(Physics.CanCollideWith(Physics));
	CHECK_FALSE(Query.CanCollideWith(Physics));
	FBodyCollisionFilter None;
	None.CollisionEnabled = ECollisionEnabled::NoCollision;
	CHECK_FALSE(None.CanCollideWith(Physics));
}

TEST_CASE("PhysicsTest.Types.ConstraintPresets")
{
	// UE defaults (ConstraintTypes.cpp:43-70): linear locked, cone / twist free with 45 degree limits.
	FConstraintInstance Joint;
	CHECK(Joint.ProfileInstance.LinearLimit.XMotion == ELinearConstraintMotion::LCM_Locked);
	CHECK(Joint.ProfileInstance.ConeLimit.Swing1Motion == EAngularConstraintMotion::ACM_Free);
	CHECK(Joint.ProfileInstance.ConeLimit.Swing1LimitDegrees == 45.0f);
	CHECK(Joint.ProfileInstance.TwistLimit.TwistLimitDegrees == 45.0f);
	CHECK_FALSE(Joint.ProfileInstance.bDisableCollision);
	CHECK(ConstraintUtils::IsBallAndSocket(Joint));

	ConstraintUtils::ConfigureAsHinge(Joint);
	CHECK(ConstraintUtils::IsHinge(Joint));
	CHECK_FALSE(ConstraintUtils::IsBallAndSocket(Joint));
	CHECK(Joint.ProfileInstance.ConeLimit.Swing1Motion == EAngularConstraintMotion::ACM_Locked);
	CHECK(Joint.ProfileInstance.ConeLimit.Swing2Motion == EAngularConstraintMotion::ACM_Locked);
	CHECK(Joint.ProfileInstance.TwistLimit.TwistMotion == EAngularConstraintMotion::ACM_Free);
	CHECK(Joint.ProfileInstance.TwistLimit.TwistLimitDegrees == 45.0f);
	CHECK(Joint.ProfileInstance.ConeLimit.Swing1LimitDegrees == 0.0f);

	ConstraintUtils::ConfigureAsBallAndSocket(Joint);
	CHECK(ConstraintUtils::IsBallAndSocket(Joint));
	CHECK(Joint.ProfileInstance.TwistLimit.TwistLimitDegrees == 0.0f);

	Joint.ProfileInstance.ConeLimit.Swing1LimitDegrees = 30.0f;
	ConstraintUtils::ConfigureAsHinge(Joint, false);   // bOverwriteLimits = false keeps the limits
	CHECK(Joint.ProfileInstance.ConeLimit.Swing1LimitDegrees == 30.0f);

	// Reference frame: rotation with columns (Pri, Sec, Pri ^ Sec).
	Joint.Pos1 = FVector(0.0, 0.0, 50.0);
	Joint.PriAxis1 = FVector(0.0, 0.0, 1.0);
	Joint.SecAxis1 = FVector(1.0, 0.0, 0.0);
	const FTransform Frame = Joint.GetRefFrame(EConstraintFrame::Frame1);
	CHECK(Frame.GetTranslation() == FVector(0.0, 0.0, 50.0));
	CHECK(Frame.TransformVectorNoScale(FVector(1.0, 0.0, 0.0)).Equals(FVector(0.0, 0.0, 1.0), 1e-12));
	CHECK(Frame.TransformVectorNoScale(FVector(0.0, 1.0, 0.0)).Equals(FVector(1.0, 0.0, 0.0), 1e-12));
	CHECK(Frame.TransformVectorNoScale(FVector(0.0, 0.0, 1.0)).Equals(FVector(0.0, 1.0, 0.0), 1e-12));
	CHECK(Joint.GetRefFrame(EConstraintFrame::Frame2).Equals(FTransform::Identity, 1e-12));
}
