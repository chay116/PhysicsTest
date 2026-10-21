// BackendCommonTests.cpp - the BackendCommon skeleton (03_WorkPlan M2 task 3, 02_Architecture 8.1, 14.1): unit scale
// factors, capsule axis remap, the Bullet damping conversion, the Chaos combine rules (static friction with SF < F and
// SF > F materials), the contact convention (Jolt flip and the Body0 swap) and FNV-1a. Compiled with
// PT_BACKEND_NS=AutomationTest (BackendCommon headers need a backend namespace). The full BackendCommon set is M5.
#include "CoreMinimal.h"

#include "PtCombine.h"
#include "PtContactConvention.h"
#include "PtDamping.h"
#include "PtUnitScale.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <cmath>
#include <cstring>
#include <limits>

using namespace PhysicsTest;

namespace
{
	FVector Rotate(const PtQuat& Q, const FVector& V)
	{
		return FQuat(Q.X, Q.Y, Q.Z, Q.W).RotateVector(V);
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
	const PtQuat Identity = Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::Z);
	CHECK(Identity.W == 1.0);   // Bullet btCapsuleShapeZ: no remap (DL47)
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
	}
	CHECK(Abi::UEDampingToBullet(0.0, Dt) == 0.0);
	CHECK(Abi::UEDampingToBullet(120.0, Dt) == 1.0);   // c dt >= 1: UE stops the body, Bullet's coefficient saturates
	CHECK(Abi::UEDampingMultiplier(120.0, Dt) == 0.0);
}

TEST_CASE("PhysicsTest.BackendCommon.CombineRules")
{
	// Chaos CombineHelper and ChooseCombineMode (Defines.h:146-164): the larger mode wins.
	CHECK(Abi::ResolveCombineMode(PT_COMBINE_AVERAGE, PT_COMBINE_MAX) == PT_COMBINE_MAX);
	CHECK(Abi::ResolveCombineMode(PT_COMBINE_MULTIPLY, PT_COMBINE_MIN) == PT_COMBINE_MULTIPLY);
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_AVERAGE) == doctest::Approx(0.4));
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MIN) == 0.2);
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MULTIPLY) == doctest::Approx(0.12));
	CHECK(Abi::CombineValues(0.2, 0.6, PT_COMBINE_MAX) == 0.6);

	// Static friction (DL41): max(C(max(F0, SF0), max(F1, SF1)), C(F0, F1)). One material with SF < F, one with SF > F:
	// Average of max(0.6, 0.2) = 0.6 and max(0.3, 0.9) = 0.9 -> 0.75 (the naive max(C(SF), C(F)) would give 0.55).
	const PtPhysicalMaterial A = Material(0.6, 0.2, 0.1);
	const PtPhysicalMaterial B = Material(0.3, 0.9, 0.7);
	const Abi::FPairFriction Pair = Abi::CombinePairFriction(A, B);
	CHECK(Pair.Dynamic == doctest::Approx(0.45));
	CHECK(Pair.Static == doctest::Approx(0.75));
	CHECK(Abi::CombinePairFriction(B, A).Static == Pair.Static);
	// Both SF <= F: static = dynamic (what Jolt / Bullet reproduce with one coefficient, dynamic_only).
	const Abi::FPairFriction Plain = Abi::CombinePairFriction(Material(0.5, 0.0, 0.0), Material(0.7, 0.3, 0.0));
	CHECK(Plain.Static == Plain.Dynamic);
	// Modes resolve per pair; restitution uses its own mode.
	const PtPhysicalMaterial Bouncy = Material(0.5, 0.0, 0.8, PT_COMBINE_MAX);
	const PtPhysicalMaterial Floor = Material(0.5, 0.0, 0.0, PT_COMBINE_AVERAGE);
	CHECK(Abi::CombinePairRestitution(Bouncy, Floor) == 0.8);
	CHECK(Abi::CombinePairRestitution(Material(0.5, 0.0, 0.8), Floor) == doctest::Approx(0.4));
	CHECK(Abi::CombinePairRestitution(Material(0.5, 0.0, 0.8, PT_COMBINE_MULTIPLY), Floor) == 0.0);
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

TEST_CASE("PhysicsTest.BackendCommon.Fnv1a64")
{
	// FNV-1a 64 reference vectors.
	CHECK(Abi::Fnv1a64("", 0) == 0xcbf29ce484222325ull);
	CHECK(Abi::Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
	CHECK(Abi::Fnv1a64("foobar", 6) == 0x85944171f73967e8ull);
	// Chained hashing equals hashing the concatenation.
	CHECK(Abi::Fnv1a64("bar", 3, Abi::Fnv1a64("foo", 3)) == Abi::Fnv1a64("foobar", 6));
}
