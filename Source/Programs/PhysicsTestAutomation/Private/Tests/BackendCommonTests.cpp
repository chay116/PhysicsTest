// BackendCommonTests.cpp - BackendCommon (03_WorkPlan M2 task 3 skeleton, M5 task 1 completion; 02_Architecture 8.1, 14.1):
// the unit-scale table (D1), the capsule axis remap with the D1 amendment (DL47: Bullet's btCapsuleShapeZ needs none) and
// UE4's U2PSphylBasis, the Bullet damping conversion and UE's damping order, the UE / Chaos combine rules with DL41's static
// friction rule, the packed combine modes, the contact convention (Jolt flip, Bullet's native signs, Body0 = the more
// mobile body), the compound-wrapping classification, FNV-1a and the ODR separation (every definition sits in the inline
// namespace PT_BACKEND_NS). Compiled with PT_BACKEND_NS=AutomationTest (BackendCommon headers need a backend namespace).
#include "CoreMinimal.h"

#include "PtCombine.h"
#include "PtContactConvention.h"
#include "PtDamping.h"
#include "PtShapeWrapping.h"
#include "PtUnitScale.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace PhysicsTest;

namespace
{
	FVector Rotate(const PtQuat& Q, const FVector& V)
	{
		return FQuat(Q.X, Q.Y, Q.Z, Q.W).RotateVector(V);
	}

	void CheckVector(const FVector& Got, const FVector& Expected, const double Tolerance = 1e-15)
	{
		CHECK(std::abs(Got.X - Expected.X) <= Tolerance);
		CHECK(std::abs(Got.Y - Expected.Y) <= Tolerance);
		CHECK(std::abs(Got.Z - Expected.Z) <= Tolerance);
	}

	PtPhysicalMaterial Material(const double Friction, const double StaticFriction, const double Restitution, const uint8 Mode = PT_COMBINE_AVERAGE)
	{
		PtPhysicalMaterial M = {};
		M.Friction = Friction;
		M.StaticFriction = StaticFriction;
		M.Restitution = Restitution;
		M.FrictionCombineMode = Mode;
		M.RestitutionCombineMode = Mode;
		return M;
	}

	PtTransform IdentityTransform()
	{
		PtTransform T = {};
		T.Rotation.W = 1.0;
		return T;
	}
}

TEST_CASE("PhysicsTest.BackendCommon.UnitScale")
{
	// D1: L = native length units per cm. x L for length / velocity / force / linear impulse, x L^2 for inertia / torque.
	CHECK(Abi::GCentimetres.L == 1.0);
	CHECK(Abi::GMetres.L == 0.01);
	CHECK(Abi::GMetres.Length(980.0) == doctest::Approx(9.8).epsilon(1e-15));
	CHECK(Abi::GMetres.Inertia(296380.0) == doctest::Approx(29.638).epsilon(1e-15));   // kg*cm^2 -> kg*m^2
	CHECK(Abi::GMetres.ToCm(5.0183328) == doctest::Approx(501.83328).epsilon(1e-15));
	CHECK(Abi::GMetres.ToKgCm2(29.638) == doctest::Approx(296380.0).epsilon(1e-15));
	const PtVec3 Gravity = Abi::GMetres.Length(PtVec3{ 0.0, 0.0, -980.0 });
	CHECK(Gravity.Z == doctest::Approx(-9.8).epsilon(1e-15));
	CHECK(Abi::GCentimetres.Length(123.25) == 123.25);
	// Torque (kg*cm^2/s^2) and angular impulse scale with L^2, force and linear impulse with L.
	CHECK(Abi::GMetres.Inertia(1.0e4) == doctest::Approx(1.0).epsilon(1e-15));
	CHECK(Abi::GMetres.Length(100.0) == doctest::Approx(1.0).epsilon(1e-15));
}

// 03 M5: "단위 계수표(Chaos·PhysX-UE4Compat는 1, Jolt·Bullet·PhysX-SdkDefault는 0.01)".
TEST_CASE("PhysicsTest.BackendCommon.UnitScaleTable")
{
	struct FRow
	{
		Abi::EEngineUnits Engine;
		double L;
		const char* Name;
	};
	const FRow Rows[] = {
		{ Abi::EEngineUnits::Chaos, 1.0, "Chaos" },
		{ Abi::EEngineUnits::PhysXUE4Compat, 1.0, "PhysX UE4Compat / -TGS / -Stab" },
		{ Abi::EEngineUnits::PhysXSdkDefault, 0.01, "PhysX SdkDefault" },
		{ Abi::EEngineUnits::Jolt, 0.01, "Jolt" },
		{ Abi::EEngineUnits::Bullet, 0.01, "Bullet" },
	};
	for (const FRow& Row : Rows)
	{
		const std::string Name(Row.Name);
		CAPTURE(Name);
		CHECK(Abi::GetEngineUnitScale(Row.Engine).L == Row.L);
	}
	// The table is constexpr: a backend's one conversion constant is fixed at compile time.
	static_assert(Abi::GetEngineUnitScale(Abi::EEngineUnits::Bullet).L == 0.01, "Bullet runs in metres (D1)");
	static_assert(Abi::GetEngineUnitScale(Abi::EEngineUnits::PhysXUE4Compat).L == 1.0, "PhysX UE4Compat runs in cm (D1)");
}

TEST_CASE("PhysicsTest.BackendCommon.CapsuleAxisRemap")
{
	// The remap turns each engine's native capsule axis into the UE capsule axis (local +Z).
	const FVector Z(0.0, 0.0, 1.0);
	const FVector FromX = Rotate(Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::X), FVector(1.0, 0.0, 0.0));
	const FVector FromY = Rotate(Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::Y), FVector(0.0, 1.0, 0.0));
	const FVector FromZ = Rotate(Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::Z), FVector(0.0, 0.0, 1.0));
	for (const FVector& V : { FromX, FromY, FromZ })
	{
		CHECK(V.X == doctest::Approx(Z.X).epsilon(1e-15));
		CHECK(V.Y == doctest::Approx(Z.Y).epsilon(1e-15));
		CHECK(V.Z == doctest::Approx(Z.Z).epsilon(1e-15));
	}
	// D1 amendment (DL47): Bullet's btCapsuleShapeZ lies along Z, so its remap is exactly the identity (no wrapper).
	const PtQuat Identity = Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::Z);
	CHECK(Identity.X == 0.0);
	CHECK(Identity.Y == 0.0);
	CHECK(Identity.Z == 0.0);
	CHECK(Identity.W == 1.0);
	// UE4's U2PSphylBasis (180 deg about (1, 0, 1)/sqrt(2), 02 8.2): another rotation that takes PhysX's X axis to Z. It is
	// its own inverse (a half turn) and turns Y into -Y, which a capsule does not see.
	const PtQuat Sphyl = Abi::CapsuleAxisRemapUE4SphylBasis();
	CHECK(Sphyl.X * Sphyl.X + Sphyl.Y * Sphyl.Y + Sphyl.Z * Sphyl.Z + Sphyl.W * Sphyl.W == doctest::Approx(1.0).epsilon(1e-15));
	CheckVector(Rotate(Sphyl, FVector(1.0, 0.0, 0.0)), FVector(0.0, 0.0, 1.0));
	CheckVector(Rotate(Sphyl, FVector(0.0, 0.0, 1.0)), FVector(1.0, 0.0, 0.0));
	CheckVector(Rotate(Sphyl, FVector(0.0, 1.0, 0.0)), FVector(0.0, -1.0, 0.0));
}

TEST_CASE("PhysicsTest.BackendCommon.BulletDampingConversion")
{
	// UE: v *= max(0, 1 - c dt) per step. Bullet: v *= (1 - c_b)^dt. c_b = 1 - (1 - c dt)^(1/dt) reproduces UE exactly.
	const double Dt = 1.0 / 60.0;
	for (const double C : { 0.0, 0.01, 0.05, 0.5, 10.0 })
	{
		const double Bullet = Abi::UEDampingToBullet(C, Dt);
		CAPTURE(C);
		CHECK(std::pow(1.0 - Bullet, Dt) == doctest::Approx(Abi::UEDampingMultiplier(C, Dt)).epsilon(1e-12));
		// The readback inverse returns the UE coefficient.
		CHECK(Abi::BulletDampingToUE(Bullet, Dt) == doctest::Approx(C).epsilon(1e-9));
		// Bullet clamps its coefficient to [0, 1] (btRigidBody::setDamping): every converted value is inside.
		CHECK(Bullet >= 0.0);
		CHECK(Bullet <= 1.0);
	}
	CHECK(Abi::UEDampingToBullet(0.0, Dt) == 0.0);
	CHECK(Abi::UEDampingToBullet(120.0, Dt) == 1.0);   // c dt >= 1: UE stops the body, Bullet's coefficient saturates
	CHECK(Abi::UEDampingMultiplier(120.0, Dt) == 0.0);
	CHECK(Abi::BulletDampingToUE(1.0, Dt) == doctest::Approx(60.0));
	// Another step length gives another coefficient (the Bullet backend re-converts when the step dt changes).
	CHECK(Abi::UEDampingToBullet(0.5, 1.0 / 30.0) != doctest::Approx(Abi::UEDampingToBullet(0.5, Dt)));
}

// M5: the conversion fixes Bullet's multiplier but not its order. Bullet damps before its solver adds the force impulse
// (v1 = v0 m + a dt), UE / Chaos after (v1 = (v0 + a dt) m). UEOrderPreSolveVelocity is the velocity to set before such a
// solver so that it ends at UE's value; the difference it removes is g dt (1 - m^N) after N steps of free fall.
TEST_CASE("PhysicsTest.BackendCommon.DampingOrder")
{
	const double Dt = 1.0 / 60.0;
	const double G = -980.0;
	for (const double C : { 0.01, 0.5, 2.0, 59.0, 75.0 })
	{
		CAPTURE(C);
		const double M = Abi::UEDampingMultiplier(C, Dt);
		double Ue = 0.0;
		double BulletOrder = 0.0;
		double Emulated = 0.0;
		for (int32 Step = 0; Step < 30; ++Step)
		{
			Ue = (Ue + G * Dt) * M;
			BulletOrder = BulletOrder * M + G * Dt;
			Emulated = Abi::UEOrderPreSolveVelocity(Emulated, G * Dt, M) + G * Dt;   // the pre-tick, then the solver's "+ a dt"
		}
		CHECK(Emulated == doctest::Approx(Ue).epsilon(1e-12));
		if (M > 0.0 && M < 1.0)
		{
			CHECK(BulletOrder - Ue == doctest::Approx(G * Dt * (1.0 - std::pow(M, 30))).epsilon(1e-9));
		}
	}
	// UE's default c = 0.01: 30 frames of free fall differ by 0.08 cm/s in Bullet's order (beyond the 0.05 cm/s of
	// SdkBackend.CommandsMatchNull), which is why the Bullet backend emulates UE's order.
	const double M = Abi::UEDampingMultiplier(0.01, Dt);
	CHECK(std::abs(G * Dt * (1.0 - std::pow(M, 30))) == doctest::Approx(0.0815).epsilon(0.01));
	// c dt >= 1 stops the body in UE; the emulation does too (a converted Bullet coefficient of 1 would leave a dt).
	CHECK(Abi::UEOrderPreSolveVelocity(500.0, G * Dt, 0.0) + G * Dt == 0.0);
}

TEST_CASE("PhysicsTest.BackendCommon.CombineRules")
{
	// Chaos CombineHelper and ChooseCombineMode (Defines.h:146-164): the larger mode wins.
	CHECK(Abi::ResolveCombineMode(PT_COMBINE_AVERAGE, PT_COMBINE_MAX) == PT_COMBINE_MAX);
	CHECK(Abi::ResolveCombineMode(PT_COMBINE_MULTIPLY, PT_COMBINE_MIN) == PT_COMBINE_MULTIPLY);
	CHECK(Abi::ResolveCombineMode(PT_COMBINE_MIN, PT_COMBINE_AVERAGE) == PT_COMBINE_MIN);
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_AVERAGE) == doctest::Approx(0.4));
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MIN) == 0.2);
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MULTIPLY) == doctest::Approx(0.12));
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MAX) == 0.6);
	// No clamp: UE does not limit a combined coefficient (Bullet's own callback clamps friction to 10, btManifoldResult.cpp).
	CHECK(Abi::CombineValues(4.0, 5.0, PT_COMBINE_MULTIPLY) == 20.0);

	// Static friction (DL41): max(C(max(F0, SF0), max(F1, SF1)), C(F0, F1)). One material with SF < F, one with SF > F:
	// Average of max(0.6, 0.2) = 0.6 and max(0.3, 0.9) = 0.9 -> 0.75 (the naive max(C(SF), C(F)) would give 0.55).
	const PtPhysicalMaterial A = Material(0.6, 0.2, 0.1);
	const PtPhysicalMaterial B = Material(0.3, 0.9, 0.7);
	const Abi::FPairFriction Pair = Abi::CombinePairFriction(A, B);
	CHECK(Pair.Dynamic == doctest::Approx(0.45));
	CHECK(Pair.Static == doctest::Approx(0.75));
	CHECK(Abi::CombinePairFriction(B, A).Static == Pair.Static);
	// DL41's own example: A (F 0.5, SF 0) and B (F 0.5, SF 1.0), Average. The review's formula max(C(SF0, SF1), C(F0, F1))
	// gives 0.5; Chaos (per-material max first) gives 0.75. A one-coefficient engine (Jolt, Bullet) returns the dynamic 0.5
	// (material.static_friction_rule = dynamic_only).
	const Abi::FPairFriction Example = Abi::CombinePairFriction(Material(0.5, 0.0, 0.0), Material(0.5, 1.0, 0.0));
	CHECK(Example.Static == doctest::Approx(0.75));
	CHECK(Example.Dynamic == doctest::Approx(0.5));
	// The outer max matters only for a non-monotonic combine; for the four UE modes it never lowers the static value.
	for (const uint32 ModeValue : { PT_COMBINE_AVERAGE, PT_COMBINE_MIN, PT_COMBINE_MULTIPLY, PT_COMBINE_MAX })
	{
		const uint8 Mode = static_cast<uint8>(ModeValue);
		const Abi::FPairFriction Modes = Abi::CombinePairFriction(Material(0.4, 0.8, 0.0, Mode), Material(0.6, 0.1, 0.0, Mode));
		CHECK(Modes.Static >= Modes.Dynamic);
	}
	// Both SF <= F: static = dynamic (what Jolt / Bullet reproduce with one coefficient, dynamic_only).
	const Abi::FPairFriction Plain = Abi::CombinePairFriction(Material(0.5, 0.0, 0.0), Material(0.7, 0.3, 0.0));
	CHECK(Plain.Static == Plain.Dynamic);
	// Modes resolve per pair; restitution uses its own mode.
	const PtPhysicalMaterial Bouncy = Material(0.5, 0.0, 0.8, PT_COMBINE_MAX);
	const PtPhysicalMaterial Floor = Material(0.5, 0.0, 0.0, PT_COMBINE_AVERAGE);
	CHECK(Abi::CombinePairRestitution(Bouncy, Floor) == 0.8);
	CHECK(Abi::CombinePairRestitution(Material(0.5, 0.0, 0.8), Floor) == doctest::Approx(0.4));
	CHECK(Abi::CombinePairRestitution(Material(0.5, 0.0, 0.8, PT_COMBINE_MULTIPLY), Floor) == 0.0);
	PtPhysicalMaterial MixedModes = Material(0.5, 0.0, 0.8, PT_COMBINE_MIN);
	MixedModes.RestitutionCombineMode = PT_COMBINE_MAX;
	CHECK(Abi::CombinePairFriction(MixedModes, Material(0.9, 0.0, 0.2)).Dynamic == 0.5);   // friction: Min
	CHECK(Abi::CombinePairRestitution(MixedModes, Material(0.9, 0.0, 0.2)) == 0.8);      // restitution: Max
}

// The modes a per-object combine callback reads back (Bullet: the collision object's user index 2).
TEST_CASE("PhysicsTest.BackendCommon.PackedCombineModes")
{
	for (uint8 Friction = 0; Friction <= PT_COMBINE_MAX; ++Friction)
	{
		for (uint8 Restitution = 0; Restitution <= PT_COMBINE_MAX; ++Restitution)
		{
			const uint8 Packed = Abi::PackCombineModes(Friction, Restitution);
			CHECK(Packed < 16u);
			CHECK(Abi::GetPackedFrictionMode(Packed) == Friction);
			CHECK(Abi::GetPackedRestitutionMode(Packed) == Restitution);
		}
	}
	// Read from an int user index (sign-extended through uint32): the low 4 bits only.
	CHECK(Abi::GetPackedFrictionMode(static_cast<uint32>(static_cast<int32>(Abi::PackCombineModes(PT_COMBINE_MAX, PT_COMBINE_MIN)))) == PT_COMBINE_MAX);
}

TEST_CASE("PhysicsTest.BackendCommon.ContactConvention")
{
	// Jolt-like report: normal from Body0 toward Body1, positive penetration depth. Body0 is the static floor (index 0),
	// Body1 a dynamic box (index 1): the canonical form swaps them so Body0 is the dynamic one, the normal lies on the
	// floor and points at the box (+Z), and Phi < 0 means penetrating.
	const uint8 Mobility[2] = { Abi::GMobilityStatic, Abi::GMobilityDynamic };
	PtContactPoint Contact = {};
	Contact.Body0 = 0;
	Contact.Body1 = 1;
	Contact.Shape0 = 3;
	Contact.Shape1 = 4;
	Contact.PointOn0 = PtVec3{ 0.0, 0.0, 0.0 };
	Contact.PointOn1 = PtVec3{ 0.0, 0.0, -0.5 };
	Contact.Normal = PtVec3{ 0.0, 0.0, 1.0 };   // floor -> box, i.e. toward Body1
	Contact.Phi = 0.5;                          // depth
	Contact.FrictionImpulse = PtVec3{ 1.0, 0.0, 0.0 };
	Contact.NormalImpulse = std::numeric_limits<double>::quiet_NaN();
	Abi::CanonicalizeContact(Contact, Mobility, true, true);
	CHECK(Contact.Body0 == 1u);
	CHECK(Contact.Body1 == 0u);
	CHECK(Contact.Shape0 == 4u);
	CHECK(Contact.Shape1 == 3u);
	CHECK(Contact.Phi == -0.5);
	CHECK(Contact.Normal.Z == 1.0);   // flipped for "toward Body1", flipped again by the swap: on the floor, toward the box
	CHECK(Contact.PointOn0.Z == -0.5);
	CHECK(Contact.FrictionImpulse.X == -1.0);
	CHECK((Contact.Flags & PT_CONTACT_SWAPPED) != 0u);
	// Equal mobility: the lower index is Body0; already canonical input stays as it is.
	const uint8 Both[3] = { Abi::GMobilityDynamic, Abi::GMobilityDynamic, Abi::GMobilityDynamic };
	PtContactPoint Pair = {};
	Pair.Body0 = 2;
	Pair.Body1 = 1;
	Pair.Normal = PtVec3{ 1.0, 0.0, 0.0 };
	Pair.Phi = -0.25;
	Abi::CanonicalizeContact(Pair, Both, false, false);
	CHECK(Pair.Body0 == 1u);
	CHECK(Pair.Normal.X == -1.0);
	CHECK(Pair.Phi == -0.25);
	Abi::CanonicalizeContact(Pair, Both, false, false);
	CHECK(Pair.Body0 == 1u);
	CHECK((Pair.Flags & PT_CONTACT_SWAPPED) != 0u);
}

// M5: Bullet's manifold point already has Chaos' signs (m_normalWorldOnB on B toward A, distance < 0 penetrating, A = B +
// n * distance), so only the mobility swap applies. Ranks follow PT_MOTION_*: Dynamic > Kinematic > Static.
TEST_CASE("PhysicsTest.BackendCommon.ContactConventionBulletAndMobility")
{
	CHECK(Abi::GetMobilityRank(PT_MOTION_STATIC) == Abi::GMobilityStatic);
	CHECK(Abi::GetMobilityRank(PT_MOTION_KINEMATIC) == Abi::GMobilityKinematic);
	CHECK(Abi::GetMobilityRank(PT_MOTION_DYNAMIC) == Abi::GMobilityDynamic);
	CHECK(Abi::GetMobilityRank(7u) == Abi::GMobilityStatic);
	// A Bullet manifold with A = the static ground (index 0) and B = a dynamic box (index 1) resting 0.2 cm deep: the normal
	// on B points toward A (down), PointOnA = PointOnB + n * distance.
	const uint8 Mobility[3] = { Abi::GetMobilityRank(PT_MOTION_STATIC), Abi::GetMobilityRank(PT_MOTION_DYNAMIC), Abi::GetMobilityRank(PT_MOTION_KINEMATIC) };
	PtContactPoint Contact = {};
	Contact.Body0 = 0;
	Contact.Body1 = 1;
	Contact.Normal = PtVec3{ 0.0, 0.0, -1.0 };
	Contact.Phi = -0.2;
	Contact.PointOn1 = PtVec3{ 10.0, 0.0, -0.1 };   // on the box (B)
	Contact.PointOn0 = PtVec3{ 10.0, 0.0, 0.1 };    // on the ground (A) = B + n * distance
	Contact.NormalImpulse = 3.0;
	Contact.FrictionImpulse = PtVec3{ 0.5, 0.0, 0.0 };   // on A
	Abi::CanonicalizeContact(Contact, Mobility, false, false);
	CHECK(Contact.Body0 == 1u);   // the dynamic box
	CHECK(Contact.Body1 == 0u);
	CHECK(Contact.Normal.Z == 1.0);   // on the ground (Body1), toward the box (Body0)
	CHECK(Contact.Phi == -0.2);
	CHECK(Contact.NormalImpulse == 3.0);   // a scalar: unchanged
	CHECK(Contact.FrictionImpulse.X == -0.5);   // now the impulse on the box
	const double Separation = (Contact.PointOn0.X - Contact.PointOn1.X) * Contact.Normal.X + (Contact.PointOn0.Y - Contact.PointOn1.Y) * Contact.Normal.Y
		+ (Contact.PointOn0.Z - Contact.PointOn1.Z) * Contact.Normal.Z;
	CHECK(Separation == doctest::Approx(Contact.Phi));   // (PointOn0 - PointOn1) . Normal = Phi in the canonical form
	// Kinematic against static: the kinematic body is Body0; dynamic against kinematic: the dynamic one.
	PtContactPoint Platform = {};
	Platform.Body0 = 0;
	Platform.Body1 = 2;
	Platform.Normal = PtVec3{ 0.0, 0.0, 1.0 };
	Abi::CanonicalizeContact(Platform, Mobility, false, false);
	CHECK(Platform.Body0 == 2u);
	PtContactPoint Rider = {};
	Rider.Body0 = 2;
	Rider.Body1 = 1;
	Abi::CanonicalizeContact(Rider, Mobility, false, false);
	CHECK(Rider.Body0 == 1u);
}

// M5: the wrapper kinds of an engine whose only wrapper is a compound with placed children (Bullet, 02 8.4).
TEST_CASE("PhysicsTest.BackendCommon.CompoundWrapping")
{
	PtActorCreateInfo Actor = {};
	Actor.NumShapes = 1;
	Actor.CenterOfMassLocal = IdentityTransform();
	PtShapeElem Element = {};
	Element.LocalTransform = IdentityTransform();
	CHECK(Abi::ClassifyCompoundWrapping(Actor, &Element) == Abi::EShapeWrapKind::None);
	CHECK(Abi::CanUseNativePrimitive(Actor, &Element));
	// The COM 10 cm off: a one-child compound for the COM frame.
	PtActorCreateInfo Nudged = Actor;
	Nudged.CenterOfMassLocal.Translation.X = 10.0;
	CHECK(Abi::ClassifyCompoundWrapping(Nudged, &Element) == Abi::EShapeWrapKind::OffsetCom);
	// A rotated principal frame is an offset COM frame too (the body frame is the principal frame).
	PtActorCreateInfo Rotated = Actor;
	Rotated.CenterOfMassLocal.Rotation = PtQuat{ 0.0, 0.0, 0.38268343236508977, 0.92387953251128674 };
	CHECK(Abi::ClassifyCompoundWrapping(Rotated, &Element) == Abi::EShapeWrapKind::OffsetCom);
	// The element placed, the COM frame at the actor frame (e.g. a static body): LocalTransform.
	PtShapeElem Placed = Element;
	Placed.LocalTransform.Translation.Z = 25.0;
	CHECK(Abi::ClassifyCompoundWrapping(Actor, &Placed) == Abi::EShapeWrapKind::LocalTransform);
	// Several elements: Compound (the children carry the COM offset; nothing else is counted).
	PtActorCreateInfo Two = Nudged;
	Two.NumShapes = 2;
	CHECK(Abi::ClassifyCompoundWrapping(Two, &Element) == Abi::EShapeWrapKind::Compound);
	// The DL42 tolerances: below 1e-6 cm and within 1 - 1e-9 of the identity quaternion is still the native primitive.
	PtActorCreateInfo Tiny = Actor;
	Tiny.CenterOfMassLocal.Translation.X = 5e-7;
	CHECK(Abi::ClassifyCompoundWrapping(Tiny, &Element) == Abi::EShapeWrapKind::None);
	Abi::FShapeWrappingCounts Counts;
	Abi::CountWrapping(Counts, Abi::EShapeWrapKind::None);
	Abi::CountWrapping(Counts, Abi::EShapeWrapKind::OffsetCom);
	Abi::CountWrapping(Counts, Abi::EShapeWrapKind::Compound);
	Abi::CountWrapping(Counts, Abi::EShapeWrapKind::LocalTransform);
	CHECK(Counts.Total() == 3u);
	CHECK(Counts.OffsetCom == 1u);
	CHECK(Counts.Compound == 1u);
	CHECK(Counts.LocalTransform == 1u);
	CHECK(Counts.AxisRemap == 0u);
}

TEST_CASE("PhysicsTest.BackendCommon.Fnv1a64")
{
	// FNV-1a 64 reference vectors.
	CHECK(Abi::Fnv1a64("", 0) == 0xcbf29ce484222325ull);
	CHECK(Abi::Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
	CHECK(Abi::Fnv1a64("foobar", 6) == 0x85944171f73967e8ull);
	// Chained hashing equals hashing the concatenation.
	CHECK(Abi::Fnv1a64("bar", 3, Abi::Fnv1a64("foo", 3)) == Abi::Fnv1a64("foobar", 6));
}

// DL46 / 02 7.3: every BackendCommon definition lives in namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS }, so
// non-template inline functions compiled into backends with different /arch never fold into one COMDAT. This TU's
// PT_BACKEND_NS is AutomationTest: the names resolve through the inline namespace, and the types are distinct per backend
// namespace. The linker-map side (Null, AbiTwin, Jolt, Bullet, PhysX each with their own copies) is
// PhysicsTest.Probe.KitNamespaces.
TEST_CASE("PhysicsTest.BackendCommon.OdrNamespaceSeparation")
{
	static_assert(std::is_same_v<Abi::FUnitScale, Abi::AutomationTest::FUnitScale>, "FUnitScale is in the inline backend namespace");
	static_assert(std::is_same_v<Abi::FPairFriction, Abi::AutomationTest::FPairFriction>, "FPairFriction is in the inline backend namespace");
	static_assert(std::is_same_v<Abi::FShapeWrappingCounts, Abi::AutomationTest::FShapeWrappingCounts>, "FShapeWrappingCounts is in the inline backend namespace");
	static_assert(std::is_same_v<Abi::EEngineUnits, Abi::AutomationTest::EEngineUnits>, "EEngineUnits is in the inline backend namespace");
	CHECK(&Abi::CombineValues == &Abi::AutomationTest::CombineValues);
	CHECK(&Abi::UEDampingToBullet == &Abi::AutomationTest::UEDampingToBullet);
	CHECK(&Abi::UEOrderPreSolveVelocity == &Abi::AutomationTest::UEOrderPreSolveVelocity);
	CHECK(&Abi::CanonicalizeContact == &Abi::AutomationTest::CanonicalizeContact);
	CHECK(&Abi::ClassifyCompoundWrapping == &Abi::AutomationTest::ClassifyCompoundWrapping);
	CHECK(&Abi::Fnv1a64 == &Abi::AutomationTest::Fnv1a64);
	// The fixed-width typedefs (PtTypes.h) are the one exception: plain aliases, the same types for everyone.
	static_assert(std::is_same_v<Abi::FixedWidth::uint32, std::uint32_t>, "FixedWidth::uint32 is std::uint32_t");
}
