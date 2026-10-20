// AnalyticReference.cpp - kept out of line on purpose: EngineDeltaTime must not be inlined into a caller that passes a
// constant (no LTO in any preset, 02_Architecture 4.1), and the volatile store forces the float rounding even if it were.
#include "Physics/AnalyticReference.h"

namespace PhysicsTest::Analytic
{
	double EngineDeltaTime(const float Dt)
	{
		volatile float Rounded = Dt;   // a 32-bit store: the value is a float from here on, whatever /fp:fast assumed
		const float Value = Rounded;
		return static_cast<double>(Value);
	}

	double EngineDeltaTime(const double Dt)
	{
		volatile float Rounded = static_cast<float>(Dt);
		const float Value = Rounded;
		return static_cast<double>(Value);
	}

	FFreeFallState SemiImplicitEulerFreeFall(const double Z0, const double V0, const double GravityZ, const double Dt, const int32 Steps)
	{
		const double N = static_cast<double>(Steps);
		FFreeFallState State;
		State.Vz = V0 + N * GravityZ * Dt;
		State.Z = Z0 + N * V0 * Dt + GravityZ * Dt * Dt * N * (N + 1.0) * 0.5;
		return State;
	}
}
