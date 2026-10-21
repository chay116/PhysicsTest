// PhysicsTestViewer.exe - the viewer, phase 1 (02_Architecture 11.2, 03 M4). See PhysicsTestViewerApp.h for the command
// line. wmain: the command line arrives as UTF-16 and reaches FCommandLine as UTF-8, like the other programs.
// Exit codes: 0 ok, 2 invalid command line, 3 init failure (device, scene) or a failed screenshot.
#include "CoreMinimal.h"

#include "PhysicsTestScenario.h"
#include "PhysicsTestViewerApp.h"
#include "PTBuildInfo.h"

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
	FScenarioRegistry::CheckNotEmpty("PhysicsTestViewer");   // WHOLE_ARCHIVE reached the registrars (A.12)
	UE_LOG(LogPhysicsTest, Display, "PhysicsTestViewer | preset %s | %s | viz mode, not a benchmark", PT_BUILD_PRESET, PT_BUILD_CONFIGURATION);

	// FEngineLoop phases: PreInit -> Init -> Tick until exit -> Exit.
	FPhysicsTestViewerApp App;
	const int32 PreInitExit = App.PreInit();
	if (PreInitExit >= 0)
	{
		return PreInitExit;
	}
	if (!App.Init())
	{
		App.Exit();
		return 3;
	}
	while (!App.IsRequestingExit())
	{
		App.Tick();
	}
	App.Exit();
	return App.GetExitCode();
}
