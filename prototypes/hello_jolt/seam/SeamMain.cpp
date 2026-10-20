#include "BackendJolt.h"
#include <cstdio>

int main()
{
#if defined(__AVX2__)
	const char *isa = "consumer TU compiled WITH /arch:AVX2 (leak!)";
#else
	const char *isa = "consumer TU compiled without /arch:AVX2 (no leak)";
#endif
#if defined(JPH_OBJECT_STREAM) || defined(JPH_PROFILE_ENABLED) || defined(JPH_DEBUG_RENDERER)
	const char *defs = "JPH_* defines leaked into consumer";
#else
	const char *defs = "no JPH_* defines in consumer";
#endif
	const PTDropResult r = PT_JoltDropBox(60, 0);
	std::printf("%s; %s\nbackend config: %s\nfinal y after 60 steps = %.6f m, median step = %.1f us\n", isa, defs, r.Config, r.FinalY, r.MedianStepUs);
	return 0;
}
