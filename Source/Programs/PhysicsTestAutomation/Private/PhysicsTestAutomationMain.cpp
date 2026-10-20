// PhysicsTestAutomation.exe - doctest runner for unit tests (M1+) and ABI / conformance gates (M2+).
// Test names follow PhysicsTest.<Area>.<Name> (02_Architecture 14.1); CTest runs everything (PhysicsTest.Automation)
// and each area separately (PhysicsTest.Unit.<Area>).
//
//   PhysicsTestAutomation.exe                     run everything
//   PhysicsTestAutomation.exe -tc=PhysicsTest.BodyUtils.*
//   PhysicsTestAutomation.exe -BuildInfo          print the build self-report and exit
//   PhysicsTestAutomation.exe -ConfigDir=<dir>    use another Config/ directory (default: the source tree's)
//
// Entry point is wmain: Windows passes the command line as UTF-16 and FUtf8ArgV converts it to UTF-8, so non-ASCII
// arguments (e.g. Korean paths) reach FCommandLine intact; main(int, char**) would get the ANSI code page (cp949).
#include "CoreMinimal.h"

#include "BuildFlagsSha.h"
#include "PTBuildInfo.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

int wmain(int ArgC, wchar_t** ArgVW)
{
	using namespace PhysicsTest;

	const FUtf8ArgV Args(ArgC, ArgVW);
	const std::string CommandLine = FCommandLine::BuildFromArgV(nullptr, Args.Num(), Args.Get(), nullptr);
	FCommandLine::Set(CommandLine.c_str());
	if (!FPlatformMisc::IsDebuggerPresent())
	{
		FPlatformMisc::SuppressErrorDialogs();
	}

	UE_LOG(LogPhysicsTest, Display, "PhysicsTestAutomation | preset %s | %s | flags %s | %s %s",
		PT_BUILD_PRESET, PT_BUILD_CONFIGURATION, PT_BUILD_FLAG_PROFILE, PT_BUILD_GENERATOR, PT_BUILD_CXX_COMPILER_VERSION);
	UE_LOG(LogPhysicsTest, Display, "BuildFlags.json sha256 %s (%d TUs, %d violations), PT_DO_CHECK=%d",
		PT_BUILD_FLAGS_SHA256, PT_BUILD_FLAGS_TU_COUNT, PT_BUILD_FLAGS_VIOLATIONS, PT_DO_CHECK);
	UE_LOG(LogPhysicsTest, Display, "SDKs: Jolt %d (%s) | Bullet %d (%s) | PhysX %d (%s, %s) | Chaos %d",
		PT_BUILD_WITH_JOLT, PT_BUILD_JOLT_SHA, PT_BUILD_WITH_BULLET, PT_BUILD_BULLET_SHA,
		PT_BUILD_WITH_PHYSX, PT_BUILD_PHYSX_SHA, PT_BUILD_PHYSX_PATH, PT_BUILD_WITH_CHAOS);
	if (FParse::Param(FCommandLine::Get(), "BuildInfo"))
	{
		return 0;
	}

	doctest::Context Context;
	Context.applyCommandLine(Args.Num(), Args.Get());
	return Context.run();
}
