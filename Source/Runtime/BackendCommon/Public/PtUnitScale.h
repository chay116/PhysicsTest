// PtUnitScale.h - the only place where backends convert units (D1, 02_Architecture 8.1, A.4). [MVP M2, table M5]
// The ABI is cm, kg, s, rad. L = native length units per cm: Chaos and PhysX UE4Compat / -TGS / -Stab use 1 (cm),
// Jolt, Bullet and PhysX SdkDefault use 0.01 (m).
//   x L    length, velocity, acceleration, force, linear impulse
//   x L^2  inertia, torque, angular impulse
//   x 1    mass, angle, angular velocity, damping
// Capsule axis (D1, amended by DL47): UE's FKSphylElem lies along its local Z. PhysX's PxCapsuleGeometry lies along X
// (UE4 turns it with U2PSphylBasis), Jolt's CapsuleShape along Y (RotatedTranslatedShape), and Bullet uses btCapsuleShapeZ
// (btCapsuleShape.h:128-131 [V]), so Bullet needs no remap at all - the D1 amendment (DL47; the original D1 said "Bullet Y").
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
	struct FUnitScale
	{
		double L = 1.0;   // native length units per cm

		constexpr double Length(const double Cm) const { return Cm * L; }             // also velocity, acceleration, force, linear impulse
		constexpr double Inertia(const double KgCm2) const { return KgCm2 * L * L; }  // also torque and angular impulse
		constexpr double ToCm(const double Native) const { return Native / L; }
		constexpr double ToKgCm2(const double Native) const { return Native / (L * L); }
		constexpr PtVec3 Length(const PtVec3& Cm) const { return PtVec3{ Cm.X * L, Cm.Y * L, Cm.Z * L }; }
		constexpr PtVec3 ToCm(const PtVec3& Native) const { return PtVec3{ Native.X / L, Native.Y / L, Native.Z / L }; }
	};

	inline constexpr FUnitScale GCentimetres{ 1.0 };   // Chaos, PhysX UE4Compat / -TGS / -Stab
	inline constexpr FUnitScale GMetres{ 0.01 };       // Jolt, Bullet, PhysX SdkDefault

	// The engines (and PhysX tolerance families) of the unit-scale table (D1, 02 8.1).
	enum class EEngineUnits : uint8
	{
		Chaos,             // cm: UE's own units
		PhysXUE4Compat,    // cm: PxTolerancesScale(100, 1000), shared by UE4Compat / -TGS / -Stab (02 8.2)
		PhysXSdkDefault,   // m:  PxTolerancesScale(1, 10), the SDK's own defaults
		Jolt,              // m:  cm input hits the 500 m/s clamp and gives 1e9 kg masses (01 3.3 [V])
		Bullet,            // m:  Bullet's constants are metres (box margin 0.04, breaking threshold 0.02, sleep 0.8 m/s [V])
	};

	// The unit scale an engine runs in (the D1 table). Every backend converts with exactly this one value.
	inline constexpr FUnitScale GetEngineUnitScale(const EEngineUnits Engine)
	{
		switch (Engine)
		{
		case EEngineUnits::Chaos:
		case EEngineUnits::PhysXUE4Compat:
			return GCentimetres;
		case EEngineUnits::PhysXSdkDefault:
		case EEngineUnits::Jolt:
		case EEngineUnits::Bullet:
			return GMetres;
		}
		return GCentimetres;
	}

	// The axis a backend's native capsule shape lies along.
	enum class ECapsuleAxis : uint8
	{
		X,   // PhysX PxCapsuleGeometry
		Y,   // Jolt CapsuleShape
		Z,   // Bullet btCapsuleShapeZ (no remap, D1 amendment / DL47), UE FKSphylElem
	};

	// The rotation that turns the native capsule axis into the UE capsule axis (+Z of the element frame):
	// native shape pose = element pose * CapsuleAxisRemap(NativeAxis). X: -90 deg about Y; Y: +90 deg about X; Z: identity.
	// A capsule is symmetric, so mapping the native axis to -Z instead would describe the same shape.
	inline PtQuat CapsuleAxisRemap(const ECapsuleAxis NativeAxis)
	{
		const double HalfSqrt2 = 0.70710678118654752440;
		switch (NativeAxis)
		{
		case ECapsuleAxis::X:
			return PtQuat{ 0.0, -HalfSqrt2, 0.0, HalfSqrt2 };
		case ECapsuleAxis::Y:
			return PtQuat{ HalfSqrt2, 0.0, 0.0, HalfSqrt2 };
		case ECapsuleAxis::Z:
			break;
		}
		return PtQuat{ 0.0, 0.0, 0.0, 1.0 };
	}

	// UE4's capsule basis for PhysX (02 8.2, "U2PSphylBasis"): 180 deg about (1, 0, 1)/sqrt(2). It swaps X and Z (and turns
	// Y into -Y), so the PhysX capsule's X axis becomes the element's Z, as CapsuleAxisRemap(X) does with another rotation of
	// the same symmetric shape. The quaternion of a half turn about the unit axis a is (a, 0).
	inline PtQuat CapsuleAxisRemapUE4SphylBasis()
	{
		const double HalfSqrt2 = 0.70710678118654752440;
		return PtQuat{ HalfSqrt2, 0.0, HalfSqrt2, 0.0 };
	}
}
}
