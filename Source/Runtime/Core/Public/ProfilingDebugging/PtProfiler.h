// PtProfiler.h - harness profiling zones of the prof preset (03_WorkPlan M10 task 6; 02_Architecture 4.4). [확장 M10]
// PT_PROFILE_SCOPE("Name") is a Tracy zone (ZoneScopedN) in a build with PT_WITH_PROFILER=1 - cmake -DPT_WITH_PROFILER=ON, the
// prof preset, which also routes Jolt's JPH_PROFILE scopes to Tracy (JoltProfiler.cpp) - and expands to nothing otherwise, so
// test / dev / dbg compile exactly as before. A profiler build is for phase breakdown only: every run of it says
// timing_valid_for_headline false ("profiler build"), never a headline number (03 M10).
// PT_WITH_PROFILER is a PUBLIC compile definition of PT::Core (0 or 1), like PT_DO_CHECK: every TU sees the same value.
#pragma once

#ifndef PT_WITH_PROFILER
	#error "PT_WITH_PROFILER is not defined: link PT::Core (it carries PT_WITH_PROFILER=0|1 as a PUBLIC compile definition)"
#endif

#if PT_WITH_PROFILER
	#include "Misc/PtThirdPartyIncludesStart.h"
	#include <tracy/Tracy.hpp>
	#include "Misc/PtThirdPartyIncludesEnd.h"
	#define PT_PROFILE_SCOPE(Name) ZoneScopedN(Name)
	#define PT_PROFILE_FRAME_MARK() FrameMark
#else
	#define PT_PROFILE_SCOPE(Name)
	#define PT_PROFILE_FRAME_MARK()
#endif
