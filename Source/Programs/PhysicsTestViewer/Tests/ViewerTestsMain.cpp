// PhysicsTestViewerTests.exe - doctest runner for the viewer-side tests (PhysicsTest.Viewer.*; PT_WITH_VIEWER presets).
// Renderer-free tests of the debug draw pipeline live in PhysicsTestAutomation (PhysicsTest.DebugDraw.*, every preset).
#include "CoreMinimal.h"

#include "PhysicsTestScenario.h"
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
	FScenarioRegistry::CheckNotEmpty("PhysicsTestViewerTests");
	UE_LOG(LogPhysicsTest, Display, "PhysicsTestViewerTests | preset %s | %s", PT_BUILD_PRESET, PT_BUILD_CONFIGURATION);
	doctest::Context Context;
	Context.applyCommandLine(Args.Num(), Args.Get());
	return Context.run();
}
