// PtShapeWrapping.h - the shared COM-wrapping decision (02_Architecture 8.1, DL42, A.4). [MVP M3]
// Every SDK backend applies the same rule: a body with ONE collision element whose element frame and COM frame are both
// (nearly) the actor frame uses the engine's native primitive directly (Jolt BoxShape / SphereShape, btBoxShape,
// PxBoxGeometry). Anything else is wrapped (Jolt OffsetCenterOfMassShape / StaticCompoundShape / RotatedTranslatedShape,
// btCompoundShape). The wrappers cost compound dispatch and child transforms that the other engines do not pay, so the
// headline scenes (M3.BoxPile, M2b.Pyramid1240: uniform cubes with the COM at the centre) must create none. Backends
// count what they created per kind into the effective key shape.wrapping (G9 checks 0 on M3.BoxPile).
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
	inline constexpr double GComIdentityMaxOffsetCm = 1e-6;          // |t| < 1e-6 cm
	inline constexpr double GComIdentityMinQuatDot = 1.0 - 1e-9;     // |q . identity| = |q.W| > 1 - 1e-9

	// A frame (COM frame or element frame in actor space) that is the actor frame within the DL42 tolerances.
	inline bool IsNearlyIdentityTransform(const PtTransform& Transform)
	{
		const PtVec3& T = Transform.Translation;
		const double OffsetSquared = T.X * T.X + T.Y * T.Y + T.Z * T.Z;
		const double W = Transform.Rotation.W < 0.0 ? -Transform.Rotation.W : Transform.Rotation.W;
		return OffsetSquared < GComIdentityMaxOffsetCm * GComIdentityMaxOffsetCm && W > GComIdentityMinQuatDot;
	}

	// The COM frame test of DL42 (PtActorCreateInfo::CenterOfMassLocal).
	inline bool IsComLocalIdentity(const PtTransform& CenterOfMassLocal)
	{
		return IsNearlyIdentityTransform(CenterOfMassLocal);
	}

	// true : one element, element frame and COM frame ~ the actor frame -> the native primitive as the body shape.
	// false: the backend wraps (compound / offset COM / local transform). A capsule whose engine axis differs from UE's Z
	//        additionally needs an axis remap (counted separately as axis_remap) even when this returns true.
	inline bool CanUseNativePrimitive(const PtActorCreateInfo& Actor, const PtShapeElem* FirstShape)
	{
		return Actor.NumShapes == 1 && FirstShape != nullptr && IsComLocalIdentity(Actor.CenterOfMassLocal)
			&& IsNearlyIdentityTransform(FirstShape->LocalTransform);
	}

	// What a backend wrapped, per kind; Total() is the canonical value of the effective key shape.wrapping.
	struct FShapeWrappingCounts
	{
		uint32 Compound = 0;         // several elements in one engine shape (Jolt StaticCompoundShape, btCompoundShape)
		uint32 LocalTransform = 0;   // one element placed away from the actor frame (Jolt RotatedTranslatedShape)
		uint32 OffsetCom = 0;        // COM moved to the harness COM (Jolt OffsetCenterOfMassShape, Bullet COM frame)
		uint32 AxisRemap = 0;        // capsule axis turned to UE's Z (Jolt RotatedTranslatedShape, PhysX U2PSphylBasis)

		uint32 Total() const { return Compound + LocalTransform + OffsetCom + AxisRemap; }
	};
}
}
