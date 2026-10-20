// PhysicsTestAutomation.exe - doctest runner for unit tests (M1+) and ABI / conformance gates (M2+).
// Skeleton in M0: Core idiom tests. Test names follow PhysicsTest.<Area>.<Name> (02_Architecture 14.1).
//
//   PhysicsTestAutomation.exe                     run everything
//   PhysicsTestAutomation.exe -tc=PhysicsTest.Core.*
//   PhysicsTestAutomation.exe -BuildInfo          print the build self-report and exit
#include "CoreMinimal.h"

#include "BuildFlagsSha.h"
#include "PTBuildInfo.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

int main(int ArgC, char** ArgV)
{
	using namespace PhysicsTest;

	const std::string CommandLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, nullptr);
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
	Context.applyCommandLine(ArgC, ArgV);
	return Context.run();
}
