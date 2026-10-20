// UnrealMathUtility.h - FMath subset and math constants with UE names (UE: Core/Public/Math/UnrealMathUtility.h).
// The harness is double precision throughout (D1); the float constants exist for API parity.
#pragma once

#include "CoreTypes.h"

#include <cmath>

namespace PhysicsTest
{
	inline constexpr float UE_PI = 3.1415926535897932f;
	inline constexpr double UE_DOUBLE_PI = 3.141592653589793238462643383279502884197169399;
	inline constexpr float UE_SMALL_NUMBER = 1.e-8f;
	inline constexpr float UE_KINDA_SMALL_NUMBER = 1.e-4f;
	inline constexpr double UE_DOUBLE_SMALL_NUMBER = 1.e-8;
	inline constexpr double UE_DOUBLE_KINDA_SMALL_NUMBER = 1.e-4;
	inline constexpr float UE_BIG_NUMBER = 3.4e+38f;
	// Allowed error for a normalized quaternion (UE: UE_THRESH_QUAT_NORMALIZED).
	inline constexpr double UE_THRESH_QUAT_NORMALIZED = 0.01;

	struct FMath
	{
		template <typename T>
		[[nodiscard]] static constexpr T Abs(const T A)
		{
			return A < T(0) ? -A : A;
		}

		// Min / Max / Clamp use UE's exact comparison forms (GenericPlatformMath.h:944-972, UnrealMathUtility.h:592-595),
		// so ties (-0 / +0) and NaN go through the same expressions as in UE. Under /fp:fast the compiler does not keep
		// IEEE NaN semantics (in UE game builds neither), so no code may rely on a NaN result.
		template <typename T>
		[[nodiscard]] static constexpr T Min(const T A, const T B)
		{
			return (A < B) ? A : B;
		}

		template <typename T>
		[[nodiscard]] static constexpr T Max(const T A, const T B)
		{
			return (B < A) ? A : B;
		}

		template <typename T>
		[[nodiscard]] static constexpr T Clamp(const T X, const T MinValue, const T MaxValue)
		{
			return Max(Min(X, MaxValue), MinValue);
		}

		template <typename T>
		[[nodiscard]] static constexpr T Square(const T A)
		{
			return A * A;
		}

		[[nodiscard]] static double Sqrt(const double Value) { return std::sqrt(Value); }
		[[nodiscard]] static double InvSqrt(const double Value) { return 1.0 / std::sqrt(Value); }
		[[nodiscard]] static double Sin(const double Value) { return std::sin(Value); }
		[[nodiscard]] static double Cos(const double Value) { return std::cos(Value); }
		[[nodiscard]] static double Tan(const double Value) { return std::tan(Value); }
		// Input clamped to [-1, 1] as UE does (GenericPlatformMath.h:524-534).
		[[nodiscard]] static double Asin(const double Value) { return std::asin(Clamp(Value, -1.0, 1.0)); }
		[[nodiscard]] static double Acos(const double Value) { return std::acos(Clamp(Value, -1.0, 1.0)); }
		[[nodiscard]] static double Atan(const double Value) { return std::atan(Value); }
		[[nodiscard]] static double Atan2(const double Y, const double X) { return std::atan2(Y, X); }
		[[nodiscard]] static double Fmod(const double X, const double Y) { return std::fmod(X, Y); }

		static void SinCos(double* ScalarSin, double* ScalarCos, const double Value)
		{
			*ScalarSin = std::sin(Value);
			*ScalarCos = std::cos(Value);
		}

		[[nodiscard]] static constexpr double DegreesToRadians(const double Degrees) { return Degrees * (UE_DOUBLE_PI / 180.0); }
		[[nodiscard]] static constexpr double RadiansToDegrees(const double Radians) { return Radians * (180.0 / UE_DOUBLE_PI); }

		[[nodiscard]] static bool IsNearlyEqual(const double A, const double B, const double ErrorTolerance = UE_DOUBLE_SMALL_NUMBER)
		{
			return Abs(A - B) <= ErrorTolerance;
		}

		[[nodiscard]] static bool IsNearlyZero(const double Value, const double ErrorTolerance = UE_DOUBLE_SMALL_NUMBER)
		{
			return Abs(Value) <= ErrorTolerance;
		}

		[[nodiscard]] static bool IsFinite(const double Value) { return std::isfinite(Value); }
		[[nodiscard]] static bool IsNaN(const double Value) { return std::isnan(Value); }
	};
}
