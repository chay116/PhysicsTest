// JoltProfiler.cpp - the prof preset's bridge from Jolt's profiler to Tracy (03_WorkPlan M10 task 6: the prof preset with the Jolt profiler
// and Tracy). Built only with PT_WITH_PROFILER (library PtJoltProfiler, linked through PT::Jolt), where Jolt is configured with
// JPH_USE_EXTERNAL_PROFILE: every JPH_PROFILE scope inside Jolt constructs a JPH::ExternalProfileMeasurement, whose constructor
// and destructor a static Jolt build leaves to the user (Jolt/Core/Profiler.h:28-46 [V]). Here they open and close a Tracy
// zone named after the scope through Tracy's C API (allocated source locations: the scope names are runtime strings), so the
// object holds no C++ inline code that the harness could share (docs/Abi.md 5.13: an /arch:AVX2 object never supplies a
// harness COMDAT). Never in a measurement build: the bench marks every run of a profiler build as not headline-valid.
#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <Jolt/Jolt.h>
#include <Jolt/Core/Profiler.h>
#include <tracy/TracyC.h>
PT_THIRD_PARTY_INCLUDES_END

#include <cstring>

#if !defined(JPH_EXTERNAL_PROFILE)
	#error "JoltProfiler.cpp belongs to the prof preset (Jolt built with JPH_USE_EXTERNAL_PROFILE, cmake/ThirdParty/Jolt.cmake)"
#endif

JPH_NAMESPACE_BEGIN

static_assert(sizeof(TracyCZoneCtx) <= 64, "ExternalProfileMeasurement::mUserData holds the Tracy zone context");

ExternalProfileMeasurement::ExternalProfileMeasurement(const char* inName, const uint32 inColor)
{
	// M10 fix round 1: with TRACY_ON_DEMAND and no server connected, ___tracy_emit_zone_begin_alloc frees the source location it
	// is given and returns an inactive context (TracyProfiler.cpp, ___tracy_emit_zone_begin_alloc). Allocating it first would put
	// a tracy_malloc / tracy_free pair into every Jolt scope of a run nobody records - inside the timed buckets whose phase
	// breakdown is the prof preset's purpose. Without a server the zone stays inactive (___tracy_emit_zone_end returns at once).
	TracyCZoneCtx Context = {};
	if (___tracy_connected() != 0)
	{
		const size_t Length = std::strlen(inName);
		const uint64_t SourceLocation = ___tracy_alloc_srcloc_name(0, "Jolt", 4, inName, Length, inName, Length, inColor);
		Context = ___tracy_emit_zone_begin_alloc(SourceLocation, 1);
	}
	std::memcpy(mUserData, &Context, sizeof(Context));
}

ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
	TracyCZoneCtx Context;
	std::memcpy(&Context, mUserData, sizeof(Context));
	___tracy_emit_zone_end(Context);
}

JPH_NAMESPACE_END
