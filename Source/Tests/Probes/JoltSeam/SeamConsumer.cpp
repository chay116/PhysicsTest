// SeamConsumer.cpp - consumer side of the flag-leak probe (03_WorkPlan M0 criterion 5).
// This TU links a static library that links PT::Jolt PRIVATE. Jolt's PUBLIC /arch:AVX2 and JPH_* defines must NOT
// reach it (CMake propagates a PRIVATE dependency of a static library as $<LINK_ONLY:...>).
#include "PtJoltSeam.h"

#include <cstdio>
#include <cstring>

int main()
{
#if defined(__AVX2__)
	constexpr bool bConsumerHasAvx2 = true;
#else
	constexpr bool bConsumerHasAvx2 = false;
#endif
#if defined(JPH_OBJECT_STREAM) || defined(JPH_OBJECT_LAYER_BITS) || defined(JPH_PROFILE_ENABLED) || defined(JPH_DEBUG_RENDERER) \
	|| defined(JPH_FLOATING_POINT_EXCEPTIONS_ENABLED) || defined(JPH_ENABLE_ASSERTS) || defined(JPH_DOUBLE_PRECISION) \
	|| defined(JPH_USE_AVX2) || defined(JPH_CROSS_PLATFORM_DETERMINISTIC)
	constexpr bool bConsumerHasJphDefines = true;
#else
	constexpr bool bConsumerHasJphDefines = false;
#endif

	const FPtJoltSeamResult Result = PtJoltSeamDropBox(60);
	const bool bLibraryHasAvx2 = std::strstr(Result.BuildReportJson, "\"__AVX2__\":1") != nullptr
		&& std::strstr(Result.JoltConfiguration, "AVX2") != nullptr;

	std::printf("[PtProbe] consumer TU: __AVX2__ %s, JPH_* %s\n", bConsumerHasAvx2 ? "DEFINED (leak)" : "not defined",
		bConsumerHasJphDefines ? "DEFINED (leak)" : "not defined");
	std::printf("[PtProbe] library TU : %s | Jolt: %s\n", Result.BuildReportJson, Result.JoltConfiguration);
	std::printf("[PtProbe] library drop: z(60) = %.6f m\n", Result.FinalZ);

	const bool bPassed = !bConsumerHasAvx2 && !bConsumerHasJphDefines && bLibraryHasAvx2;
	std::printf("[PtProbe] FlagLeak: %s\n", bPassed ? "PASS" : "FAIL");
	return bPassed ? 0 : 1;
}
