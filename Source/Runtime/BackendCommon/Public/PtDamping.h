// PtDamping.h - damping conversions (02_Architecture 8.1). [MVP M2]
// UE / Chaos (PBDRigidsEvolutionGBF.cpp:928-934 [V]) and Jolt apply v *= max(0, 1 - c*dt) once per step.
// Bullet applies v *= (1 - c_b)^dt (btRigidBody::applyDamping), so the equivalent coefficient is
// c_b = 1 - (1 - c*dt)^(1/dt), and 1 when c*dt >= 1 (UE clamps the multiplier at 0).
#pragma once

#include <cmath>

#ifndef PT_BACKEND_NS
	#error "PT_BACKEND_NS must be defined per backend target"
#endif

namespace PhysicsTest::Abi
{
inline namespace PT_BACKEND_NS
{
	// The UE per-step velocity multiplier max(0, 1 - C * Dt).
	inline double UEDampingMultiplier(const double C, const double Dt)
	{
		const double Multiplier = 1.0 - C * Dt;
		return Multiplier > 0.0 ? Multiplier : 0.0;
	}

	// The Bullet damping coefficient that reproduces UEDampingMultiplier(C, Dt) for steps of Dt.
	inline double UEDampingToBullet(const double C, const double Dt)
	{
		const double Multiplier = UEDampingMultiplier(C, Dt);
		if (Multiplier <= 0.0)
		{
			return 1.0;
		}
		return 1.0 - std::pow(Multiplier, 1.0 / Dt);
	}
}
}
