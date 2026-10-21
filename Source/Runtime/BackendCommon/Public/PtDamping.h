// PtDamping.h - damping conversions (02_Architecture 8.1). [MVP M2, completed M5]
// UE / Chaos (PBDRigidsEvolutionGBF.cpp:928-934 [V]) and Jolt apply v *= max(0, 1 - c*dt) once per step, AFTER the step's
// forces (gravity included) were integrated: v1 = (v0 + a*dt) * m, m = max(0, 1 - c*dt).
// Bullet applies v *= (1 - c_b)^dt (btRigidBody::applyDamping, btRigidBody.cpp:149-160 [V]), so the coefficient that gives
// the same per-step multiplier is c_b = 1 - (1 - c*dt)^(1/dt), and 1 when c*dt >= 1 (UE clamps the multiplier at 0).
// The conversion alone does not reproduce UE, though: btDiscreteDynamicsWorld damps in predictUnconstraintMotion, BEFORE
// the solver adds the force impulse (btDiscreteDynamicsWorld.cpp:1091-1105, btSequentialImpulseConstraintSolver.cpp:477,
// 1820-1826 [V]), i.e. v1 = v0 * m + a*dt. The difference g*dt*(1 - m^N) after N steps is 0.08 cm/s after 30 frames of
// free fall at UE's default c = 0.01 (M5). UEOrderPreSolveVelocity gives the velocity a backend sets before such a solver
// so that the solver's own "+ a*dt" ends at UE's (v0 + a*dt) * m (the Bullet backend's internal pre-tick, M5).
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

	// The inverse for a readback: the UE coefficient whose multiplier equals Bullet's (1 - Cb)^Dt for steps of Dt
	// (exact for 0 <= Cb < 1; Cb >= 1 stops the body, which is any UE coefficient >= 1 / Dt).
	inline double BulletDampingToUE(const double Cb, const double Dt)
	{
		if (Cb >= 1.0)
		{
			return 1.0 / Dt;
		}
		return (1.0 - std::pow(1.0 - Cb, Dt)) / Dt;
	}

	// UE's damping order on an engine that integrates the forces after its damping hook: given the velocity V before the
	// step, the force's velocity change ForceDeltaV = a * dt that the engine will add itself, and the UE multiplier, the
	// velocity to set now so that V' + ForceDeltaV = (V + ForceDeltaV) * Multiplier.
	inline double UEOrderPreSolveVelocity(const double V, const double ForceDeltaV, const double Multiplier)
	{
		return V * Multiplier - ForceDeltaV * (1.0 - Multiplier);
	}
}
}
