// PtCombine.h - UE/Chaos material combine rules for backend combine callbacks (02_Architecture 5.1, 8.1, DL41). [MVP M2]
// Jolt and Bullet keep ONE friction coefficient per pair (mCombinedFriction, m_combinedFriction): their callbacks
// return FPairFriction::Dynamic and report material.static_friction_rule=dynamic_only (DL41). Static is the reference
// value for tests and for the effective-settings readback.
//
// Chaos 5.8.3:
//   CombineHelper    Avg (A+B)*0.5, Min, Multiply A*B, Max      (Chaos/Defines.h:146-158 [V])
//   ChooseCombineMode the larger enum value wins                (Chaos/Defines.h:160-164 [V])
//   per pair         static uses max(F, SF) per material BEFORE combining, then max(static, dynamic)
//                    (PBDCollisionConstraints.cpp:472-476, PBDCollisionConstraint.h:486 [V])
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
	inline uint8 ResolveCombineMode(const uint8 ModeA, const uint8 ModeB)
	{
		return ModeA > ModeB ? ModeA : ModeB;
	}

	inline double CombineValues(const double A, const double B, const uint8 Mode)
	{
		switch (Mode)
		{
		case PT_COMBINE_AVERAGE: return (A + B) * 0.5;
		case PT_COMBINE_MIN: return A < B ? A : B;
		case PT_COMBINE_MULTIPLY: return A * B;
		case PT_COMBINE_MAX: return A > B ? A : B;
		default: break;
		}
		return 0.0;
	}

	struct FPairFriction
	{
		double Dynamic = 0.0;
		double Static = 0.0;
	};

	// Dynamic = C(F0, F1); Static = max(C(max(F0, SF0), max(F1, SF1)), Dynamic), C = CombineValues with the resolved
	// friction mode.
	inline FPairFriction CombinePairFriction(const PtPhysicalMaterial& M0, const PtPhysicalMaterial& M1)
	{
		const uint8 Mode = ResolveCombineMode(M0.FrictionCombineMode, M1.FrictionCombineMode);
		const double Static0 = M0.StaticFriction > M0.Friction ? M0.StaticFriction : M0.Friction;
		const double Static1 = M1.StaticFriction > M1.Friction ? M1.StaticFriction : M1.Friction;
		FPairFriction Pair;
		Pair.Dynamic = CombineValues(M0.Friction, M1.Friction, Mode);
		const double CombinedStatic = CombineValues(Static0, Static1, Mode);
		Pair.Static = CombinedStatic > Pair.Dynamic ? CombinedStatic : Pair.Dynamic;
		return Pair;
	}

	inline double CombinePairRestitution(const PtPhysicalMaterial& M0, const PtPhysicalMaterial& M1)
	{
		return CombineValues(M0.Restitution, M1.Restitution, ResolveCombineMode(M0.RestitutionCombineMode, M1.RestitutionCombineMode));
	}

	// The two combine modes of a body's material in 4 bits, for engines whose combine callback sees only the two collision
	// objects (Bullet's gCalculateCombined*Callback gets the btCollisionObjects, btManifoldResult.h:37 [V]): bits 0-1 friction
	// mode, bits 2-3 restitution mode (PT_COMBINE_* are 0..3). The raw F / R values travel in the engine's own per-object fields.
	inline uint8 PackCombineModes(const uint8 FrictionMode, const uint8 RestitutionMode)
	{
		return static_cast<uint8>((FrictionMode & 3u) | ((RestitutionMode & 3u) << 2u));
	}
	inline uint8 GetPackedFrictionMode(const uint32 Packed) { return static_cast<uint8>(Packed & 3u); }
	inline uint8 GetPackedRestitutionMode(const uint32 Packed) { return static_cast<uint8>((Packed >> 2u) & 3u); }
}
}
