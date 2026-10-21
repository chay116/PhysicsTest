// AnalyticReference.h - analytic reference values for gates and unit tests (DL23, 02_Architecture 13 "analytic").
// [MVP M1: dt_engine and free fall; M6: the gate references (damped fall, discrete friction stop, restitution drop
// height); M8: the Python counterpart Tools/ptbench/analytic.py reproduces every value within 1e-9 (PhysicsTestBench
// -AnalyticReferences, CTest PhysicsTest.Py.AnalyticMatchesCpp)]
//
// Every analytic reference is computed with the dt the engines actually receive: dt_engine = (double)(float)dt. The
// +-1e-9 tolerances apply only against references computed this way (DL23). Example: S00.FreeFall (z0 = 1000 cm,
// g = -980 cm/s^2, 60 steps of float(1/60)) ends at z60 = 501.8332813705 cm; with the exact 1/60 it would be
// 501.8333333333 cm, 5.2e-5 cm higher - an exact double integrator would fail +-1e-9 against the wrong reference.
//
// MSVC /fp:fast hazard (docs/Progress.md M0 4): static_cast<double>(constexpr float Dt = 1.0f / 60.0f) is folded to the
// exact 1/60 (0.016666666666666666) instead of 0.01666666753590107. EngineDeltaTime() therefore rounds through a
// volatile float in a separate translation unit; PhysicsTest.Analytic.EngineDeltaTimeSurvivesFastMath pins the bits.
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest::Analytic
{
	// dt_engine = (double)(float)Dt, robust against constant folding under /fp:fast.
	[[nodiscard]] double EngineDeltaTime(float Dt);
	[[nodiscard]] double EngineDeltaTime(double Dt);

	struct FFreeFallState
	{
		double Z = 0.0;    // cm
		double Vz = 0.0;   // cm/s
	};

	// Semi-implicit (symplectic) Euler with constant acceleration, closed form after Steps steps:
	//   v_n = v_0 + n g dt,   z_n = z_0 + n v_0 dt + g dt^2 n (n + 1) / 2
	// (every engine in the harness integrates velocity first, then position). Dt is used as given; pass
	// EngineDeltaTime(...) for gate references.
	[[nodiscard]] FFreeFallState SemiImplicitEulerFreeFall(double Z0, double V0, double GravityZ, double Dt, int32 Steps);

	// The damping formula a backend declares in its effective key body.damping_formula (canonical value, docs/Abi.md 5.14):
	//   UEOrder (1)            v <- (v + g dt) * max(0, 1 - c dt), then z <- z + v dt   (Chaos Integrate, Null, Jolt, PhysX,
	//                          Bullet DampingModel=UE)
	//   BeforeForces (2)       v <- v * (1 - c_b)^dt + g dt with c_b = 1 - (1 - c dt)^(1/dt), then z <- z + v dt
	//                          (Bullet's own order, DampingModel=Converted: the per-step factor equals UE's)
	enum class EDampingFormula : int32
	{
		UEOrder = 1,
		BeforeForces = 2,
	};

	// G1b (S00d.FreeFallDamped): Steps steps of a linearly damped fall from rest at Z0 with the declared formula.
	[[nodiscard]] FFreeFallState DampedFreeFall(double Z0, double GravityZ, double LinearDamping, double Dt, int32 Steps, EDampingFormula Formula);

	// G5 (S06.FrictionSlide): the discrete stop distance of a body sliding on a flat ground with initial speed V0 under
	// Coulomb friction Mu and gravity G (> 0): every step removes Mu G dt of speed (never below zero), then the position
	// advances by the new speed (semi-implicit Euler). 500 cm/s, mu 0.5, g 980, dt_engine(1/60): 250.947 cm (the
	// continuous v0^2 / (2 mu g) = 255.10 cm is the wrong reference, DL23).
	[[nodiscard]] double FrictionSlideStopDistance(double V0, double Mu, double G, double Dt);

	// G4 (S05.RestitutionCombine): the drop of a body from rest that touches exactly at the end of step K of the discrete
	// trajectory: G dt^2 K (K + 1) / 2 (K = 38, dt_engine(1/60), g 980: 201.7167 cm; the speed then is G dt K = 620.67 cm/s).
	[[nodiscard]] double StepBoundaryDropHeight(double G, double Dt, int32 K);
}
