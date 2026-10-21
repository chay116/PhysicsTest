// PtUnitScale.h - the only place where backends convert units (D1, 02_Architecture 8.1, A.4). [MVP M2]
// The ABI is cm, kg, s, rad. L = native length units per cm: Chaos and PhysX UE4Compat / -TGS / -Stab use 1 (cm),
// Jolt, Bullet and PhysX SdkDefault use 0.01 (m).
//   x L    length, velocity, acceleration, force, linear impulse
//   x L^2  inertia, torque, angular impulse
//   x 1    mass, angle, angular velocity, damping
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
}
}
