# Tracy.cmake - Tracy Profiler v0.14.1 client -> PT::Tracy (prof preset only, 03_WorkPlan M10 task 6; 02_Architecture 4.3, 4.4).
#
# Pinned by URL + SHA-256 like the other small dependencies (SmallDeps.cmake; THIRD_PARTY_NOTICES.md #9, BSD-3-Clause). Only
# TracyClient.cpp is built (the client, no server / GUI): a static library compiled with the client options below, which are
# PUBLIC so that every TU that includes Tracy.hpp / TracyC.h sees the same configuration.
#   TRACY_ENABLE            zones are compiled in (the prof preset is the only build that includes this file)
#   TRACY_ON_DEMAND         nothing is collected until a Tracy server connects
#   TRACY_ONLY_LOCALHOST    the client listens on the loopback interface only
#   TRACY_NO_BROADCAST      no UDP presence broadcast to the local network
#   TRACY_NO_CRASH_HANDLER  Tracy installs no SEH / signal handler (the bench's Fatal hook writes exit.json, 02 12.2)
#   TRACY_NO_SYSTEM_TRACING, TRACY_NO_CONTEXT_SWITCH, TRACY_NO_SAMPLING: no ETW kernel tracing (that would need elevation)
#   TRACY_NO_FRAME_IMAGE, TRACY_NO_VSYNC_CAPTURE, TRACY_NO_CODE_TRANSFER: no extra threads or data the bench does not use
#   TRACY_NO_CALLSTACK      no call stacks (M10 fix round 0): no "Tracy Symbol Worker" thread - it resolved symbols for 30-55 ms
#                           right after start, inside short census windows (G10b) - and no dbghelp. The client keeps one
#                           thread, "Tracy Profiler", which waits for a server (TRACY_ON_DEMAND) and is idle in a run.
# The client still starts at static initialisation, before the bench's host controls. TRACY_DELAYED_INIT + TRACY_MANUAL_LIFETIME
# could start it after them, but every mode and child process would then need tracy::StartupProfiler before its first zone, for
# one thread that is idle in a run (the prof CTests check its cycles through G10b).
# Tracy's own warnings are not the harness's: the client TU is compiled with /W0 (it is never a harness TU, 02 4.1).
include_guard(GLOBAL)
include(PtFetch)

pt_fetch_url_pinned(tracy
	URL        https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
	SHA256     bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
	CACHE_FILE tracy-v0.14.1.tar.gz)

add_library(pt_tracy_client STATIC "${PT_TRACY_SOURCE_DIR}/public/TracyClient.cpp")
add_library(PT::Tracy ALIAS pt_tracy_client)
target_include_directories(pt_tracy_client SYSTEM PUBLIC "${PT_TRACY_SOURCE_DIR}/public")
target_compile_definitions(pt_tracy_client PUBLIC
	TRACY_ENABLE TRACY_ON_DEMAND TRACY_ONLY_LOCALHOST TRACY_NO_BROADCAST TRACY_NO_CRASH_HANDLER TRACY_NO_SYSTEM_TRACING
	TRACY_NO_CONTEXT_SWITCH TRACY_NO_SAMPLING TRACY_NO_FRAME_IMAGE TRACY_NO_VSYNC_CAPTURE TRACY_NO_CODE_TRANSFER TRACY_NO_CALLSTACK)
target_compile_features(pt_tracy_client PRIVATE cxx_std_17)
target_compile_options(pt_tracy_client PRIVATE /W0 /utf-8)
target_link_libraries(pt_tracy_client PUBLIC ws2_32 secur32 advapi32)
set_property(TARGET pt_tracy_client PROPERTY FOLDER "ThirdParty/Tracy")
