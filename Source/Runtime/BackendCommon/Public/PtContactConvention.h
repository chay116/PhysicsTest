// PtContactConvention.h - Chaos contact convention for debug snapshots (02_Architecture 8.1, A.4). [MVP M2]
// Chaos: the normal lies on Body1 and points toward Body0, Phi < 0 is penetrating (ContactPoint.h:49-53 [V]); Body0 is
// the more mobile body (Dynamic > Kinematic > Static), ties go to the lower index. Jolt reports the opposite normal
// direction and a positive penetration depth; CanonicalizeContact turns any native report into this convention.
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"
#include "PtTypes.h"

#ifndef PT_BACKEND_NS
	#error "PT_BACKEND_NS must be defined per backend target"
#endif

namespace PhysicsTest::Abi
{
inline namespace PT_BACKEND_NS
{
	// Mobility ranks for CanonicalizeContact (indexed by body index).
	inline constexpr uint8 GMobilityStatic = 0;
	inline constexpr uint8 GMobilityKinematic = 1;
	inline constexpr uint8 GMobilityDynamic = 2;

	// bNativeNormalTowardBody1: the native normal points from Body0 to Body1 (it is negated).
	// bNativeDepthPositive: the native value is a penetration depth (> 0 = penetrating; Phi = -depth).
	// Then Body0 / Body1 are swapped when Body1 is more mobile (or equally mobile with a lower index): points, shapes and
	// the normal swap / flip, the friction impulse (on Body0) flips, and PT_CONTACT_SWAPPED toggles.
	inline void CanonicalizeContact(PtContactPoint& InOut, const uint8* MobilityRank, const bool bNativeNormalTowardBody1, const bool bNativeDepthPositive)
	{
		if (bNativeDepthPositive)
		{
			InOut.Phi = -InOut.Phi;
		}
		if (bNativeNormalTowardBody1)
		{
			InOut.Normal = PtVec3{ -InOut.Normal.X, -InOut.Normal.Y, -InOut.Normal.Z };
		}
		const uint8 Rank0 = MobilityRank[InOut.Body0];
		const uint8 Rank1 = MobilityRank[InOut.Body1];
		const bool bSwap = Rank1 > Rank0 || (Rank1 == Rank0 && InOut.Body1 < InOut.Body0);
		if (!bSwap)
		{
			return;
		}
		const uint32 Body = InOut.Body0;
		InOut.Body0 = InOut.Body1;
		InOut.Body1 = Body;
		const uint16 Shape = InOut.Shape0;
		InOut.Shape0 = InOut.Shape1;
		InOut.Shape1 = Shape;
		const PtVec3 Point = InOut.PointOn0;
		InOut.PointOn0 = InOut.PointOn1;
		InOut.PointOn1 = Point;
		InOut.Normal = PtVec3{ -InOut.Normal.X, -InOut.Normal.Y, -InOut.Normal.Z };
		// NaN (n/a) stays NaN under negation.
		InOut.FrictionImpulse = PtVec3{ -InOut.FrictionImpulse.X, -InOut.FrictionImpulse.Y, -InOut.FrictionImpulse.Z };
		InOut.Flags ^= PT_CONTACT_SWAPPED;
	}

	// FNV-1a 64 (native state hashes are FNV-1a over doubles in harness body order, 02_Architecture 7.2 #21).
	inline uint64 Fnv1a64(const void* Data, const size_t Bytes, uint64 Seed = 0xcbf29ce484222325ull)
	{
		const uint8* Cursor = static_cast<const uint8*>(Data);
		for (size_t Index = 0; Index < Bytes; ++Index)
		{
			Seed ^= Cursor[Index];
			Seed *= 0x100000001b3ull;
		}
		return Seed;
	}
}
}
