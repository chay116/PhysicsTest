// SmokeCommon.h - shared scene constants and reporting for the SDK smoke tests (03_WorkPlan M0 task 6, criterion 6).
//
// Scene (identical for the three SDKs): native SI units, Z-up, gravity (0, 0, -9.80) m/s^2, a 1 m dynamic cube
// released at z0 = 10 m above a static 100 x 100 x 2 m ground box (top face at z = 0), no damping, no sleeping,
// 60 steps of dt = (float)(1/60) s. The cube never reaches the ground within 60 steps.
//
// Reference: all three SDKs integrate with semi-implicit Euler (v += g dt; x += v dt), so with dt_e = (double)dt
//   z_n = z0 + g * dt_e^2 * n (n + 1) / 2  ->  z_60 = 5.0183328 m
// Tolerance 2e-5 m: the G1 tolerance of 2e-3 cm expressed in metres (02_Architecture 14.3, DL23).
//
// Smoke tests are SDK-side code (like backends): they include SDK headers through BackendCommon's
// PtThirdPartyIncludes.h and never include harness Core.
#pragma once

#include "PtBuildReport.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace PhysicsTest::SdkSmoke
{
	inline constexpr double GravityZ = -9.80;
	inline constexpr double StartZ = 10.0;
	inline constexpr int NumSteps = 60;
	inline constexpr float DeltaTime = 1.0f / 60.0f;
	inline constexpr double ToleranceMetres = 2e-5;
	inline constexpr double CubeHalfExtent = 0.5;

	// dt_engine = (double)(float)dt (DL23). Under /fp:fast MSVC 14.44 constant-folds static_cast<double>(constexpr float)
	// WITHOUT rounding to float (0.016666666666666666 instead of 0.01666666753590107, measured in M0); a store through
	// a volatile float forces the rounding the engines actually see.
	[[nodiscard]] inline double GetEngineDeltaTime()
	{
		volatile float RoundedDeltaTime = DeltaTime;
		return static_cast<double>(RoundedDeltaTime);
	}

	[[nodiscard]] inline double GetExpectedZ()
	{
		const double Dt = GetEngineDeltaTime();
		return StartZ + GravityZ * Dt * Dt * static_cast<double>(NumSteps) * static_cast<double>(NumSteps + 1) / 2.0;
	}

	[[nodiscard]] inline double GetExpectedVz()
	{
		return GravityZ * GetEngineDeltaTime() * static_cast<double>(NumSteps);
	}

	// CTest runs unattended: no WER / abort / CRT assert dialogs.
	inline void PrepareUnattended()
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	}

	class FStepTimer
	{
	public:
		FStepTimer() { StepMicroseconds.reserve(NumSteps); }

		void Begin() { QueryPerformanceCounter(&Start); }
		void End()
		{
			LARGE_INTEGER Stop;
			LARGE_INTEGER Frequency;
			QueryPerformanceCounter(&Stop);
			QueryPerformanceFrequency(&Frequency);
			StepMicroseconds.push_back(static_cast<double>(Stop.QuadPart - Start.QuadPart) * 1e6 / static_cast<double>(Frequency.QuadPart));
		}
		[[nodiscard]] double MedianMicroseconds() const
		{
			if (StepMicroseconds.empty())
			{
				return 0.0;
			}
			std::vector<double> Sorted = StepMicroseconds;
			std::sort(Sorted.begin(), Sorted.end());
			return Sorted[Sorted.size() / 2];
		}

	private:
		LARGE_INTEGER Start{};
		std::vector<double> StepMicroseconds;
	};

	struct FSmokeResult
	{
		const char* Engine = "";
		double FinalZ = 0.0;
		double FinalVz = 0.0;
		double MedianStepMicroseconds = 0.0;
		bool bEngineChecksPassed = true;   // engine-specific checks (e.g. Jolt configuration string)
		const char* EngineCheckNote = "";
	};

	// Prints a human-readable line and one machine-readable PT_SMOKE_JSON line; returns the process exit code.
	inline int Report(const FSmokeResult& Result, const char* BuildReportJson, const char* SdkJson)
	{
		const double ExpectedZ = GetExpectedZ();
		const double AbsError = std::fabs(Result.FinalZ - ExpectedZ);
		const bool bTrajectoryPassed = AbsError <= ToleranceMetres;
		const bool bPassed = bTrajectoryPassed && Result.bEngineChecksPassed;

		std::printf("[PtSmoke] %s: z(%d) = %.7f m, expected %.7f m, |dz| = %.3e m (tolerance %.0e) -> %s\n",
			Result.Engine, NumSteps, Result.FinalZ, ExpectedZ, AbsError, ToleranceMetres, bTrajectoryPassed ? "ok" : "FAIL");
		std::printf("[PtSmoke] %s: vz(%d) = %.7f m/s (reference %.7f), median step %.2f us\n",
			Result.Engine, NumSteps, Result.FinalVz, GetExpectedVz(), Result.MedianStepMicroseconds);
		if (Result.EngineCheckNote[0] != '\0')
		{
			std::printf("[PtSmoke] %s: %s -> %s\n", Result.Engine, Result.EngineCheckNote, Result.bEngineChecksPassed ? "ok" : "FAIL");
		}
		std::printf("PT_SMOKE_JSON {\"engine\":\"%s\",\"z60\":%.9f,\"expected_z60\":%.9f,\"abs_error\":%.3e,\"tolerance\":%.1e,"
			"\"vz60\":%.9f,\"median_step_us\":%.3f,\"pass\":%s,\"build\":%s,\"sdk\":%s}\n",
			Result.Engine, Result.FinalZ, ExpectedZ, AbsError, ToleranceMetres, Result.FinalVz, Result.MedianStepMicroseconds,
			bPassed ? "true" : "false", BuildReportJson, SdkJson);
		std::printf("[PtSmoke] %s: %s\n", Result.Engine, bPassed ? "PASS" : "FAIL");
		std::fflush(stdout);
		return bPassed ? 0 : 1;
	}
}
