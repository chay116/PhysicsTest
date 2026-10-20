// AnalyticReference.h - analytic reference values for gates and unit tests (DL23, 02_Architecture 13 "analytic").
// [MVP M1: dt_engine and free fall; the Python counterpart Tools/ptbench/analysis/analytic.py arrives in M8]
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
}
