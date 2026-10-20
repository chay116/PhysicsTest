// PtBuildReport.h - adapter-TU self-report for G0 (02_Architecture 4.6, DL49). Header-only, no PT_BACKEND_NS needed.
//
// Only facts visible to THIS translation unit: compiler and flags (__AVX2__, /fp model, CRT, _MSC_FULL_VER) and the
// SDK's PUBLIC defines (Jolt JPH_*, PT::Bullet BT_*, PT::PhysX PX_PHYSX_STATIC_LIB / PX_PHYSICS_VERSION).
// SDK-internal defines (e.g. PhysX PX_SUPPORT_PVD, a PRIVATE per-configuration define) are NOT visible here; they come
// from BuildFlags.json (compile_commands.json entries of the SDK's own TUs, cmake/DumpBuildFlags.cmake).
//
//   PT_BUILD_REPORT_JSON(GBuildReport, "\"jolt_config\":\"...\",");
//   -> {"jolt_config":"...","__AVX2__":1,"fp":"fast","_DLL":1,"_MSC_FULL_VER":194435xxx}
// SdkFields is a string literal of "key":value pairs, each one followed by a comma (may be empty: "").
// Every value must be valid JSON: PT_BR_STR() only macros that expand to a single literal. A macro that expands to an
// expression (PX_PHYSICS_VERSION = ((5<<24) + ...)) must be reported through its literal parts, e.g. as a quoted
// "5.11.0" (PhysXSmoke.cpp). CTest validates every PT_SMOKE_JSON line with string(JSON) (SdkSmoke/RunSmoke.cmake).
#pragma once

#define PT_BR_STR2(x) #x
#define PT_BR_STR(x) PT_BR_STR2(x)

#if defined(__AVX2__)
	#define PT_BR_AVX2 "\"__AVX2__\":1,"
#else
	#define PT_BR_AVX2 "\"__AVX2__\":0,"
#endif

#if defined(_M_FP_FAST)
	#define PT_BR_FP "\"fp\":\"fast\","
#elif defined(_M_FP_PRECISE)
	#define PT_BR_FP "\"fp\":\"precise\","
#elif defined(_M_FP_STRICT)
	#define PT_BR_FP "\"fp\":\"strict\","
#else
	#define PT_BR_FP "\"fp\":\"other\","
#endif

#if defined(_DLL)
	#define PT_BR_CRT "\"_DLL\":1,"
#else
	#define PT_BR_CRT "\"_DLL\":0,"
#endif

#if defined(_DEBUG)
	#define PT_BR_DEBUG "\"_DEBUG\":1,"
#else
	#define PT_BR_DEBUG "\"_DEBUG\":0,"
#endif

#define PT_BUILD_REPORT_JSON(Name, SdkFields) \
	static const char Name[] = "{" SdkFields PT_BR_AVX2 PT_BR_FP PT_BR_CRT PT_BR_DEBUG "\"_MSC_FULL_VER\":" PT_BR_STR(_MSC_FULL_VER) "}"
