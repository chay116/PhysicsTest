// AnalyticReference.cpp - kept out of line on purpose: EngineDeltaTime must not be inlined into a caller that passes a
// constant (no LTO in any preset, 02_Architecture 4.1), and the volatile store forces the float rounding even if it were.
#include "Physics/AnalyticReference.h"

#include <cmath>

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

	FFreeFallState DampedFreeFall(const double Z0, const double GravityZ, const double LinearDamping, const double Dt, const int32 Steps,
		const EDampingFormula Formula)
	{
		const double Multiplier = LinearDamping * Dt < 1.0 ? 1.0 - LinearDamping * Dt : 0.0;
		FFreeFallState State;
		State.Z = Z0;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			State.Vz = Formula == EDampingFormula::UEOrder ? (State.Vz + GravityZ * Dt) * Multiplier : State.Vz * Multiplier + GravityZ * Dt;
			State.Z += State.Vz * Dt;
		}
		return State;
	}

	double FrictionSlideStopDistance(const double V0, const double Mu, const double G, const double Dt)
	{
		const double Decrement = Mu * G * Dt;
		double Speed = V0;
		double Distance = 0.0;
		while (Speed > 0.0 && Decrement > 0.0)
		{
			Speed = Speed > Decrement ? Speed - Decrement : 0.0;
			Distance += Speed * Dt;
		}
		return Distance;
	}

	double StepBoundaryDropHeight(const double G, const double Dt, const int32 K)
	{
		const double N = static_cast<double>(K);
		return G * Dt * Dt * N * (N + 1.0) * 0.5;
	}

	double CompleteEllipticK(const double Modulus)
	{
		double A = 1.0;
		double B = std::sqrt(1.0 - Modulus * Modulus);
		for (int32 Iteration = 0; Iteration < 64 && std::abs(A - B) > 1e-16 * A; ++Iteration)
		{
			const double Mean = 0.5 * (A + B);
			B = std::sqrt(A * B);
			A = Mean;
		}
		return 3.14159265358979323846 / (2.0 * A);
	}

	double PendulumPeriod(const double Length, const double BobRadius, const double G, const double AmplitudeRad)
	{
		const double Pi = 3.14159265358979323846;
		const double SmallAngle = 2.0 * Pi * std::sqrt((Length * Length + 0.4 * BobRadius * BobRadius) / (G * Length));
		return SmallAngle * 2.0 * CompleteEllipticK(std::sin(0.5 * AmplitudeRad)) / Pi;
	}

	double InclineSlideDistance(const double Mu, const double AngleRad, const double G, const double Dt, const int32 Steps)
	{
		const double Acceleration = G * (std::sin(AngleRad) - Mu * std::cos(AngleRad));
		if (!(Acceleration > 0.0))
		{
			return 0.0;
		}
		const double N = static_cast<double>(Steps);
		return Acceleration * Dt * Dt * N * (N + 1.0) * 0.5;
	}
}
