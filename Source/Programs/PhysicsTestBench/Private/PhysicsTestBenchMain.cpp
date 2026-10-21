// PhysicsTestBench.exe - one bench run per process (02_Architecture 12, A.12). [MVP M3 minimal]
//
//   PhysicsTestBench.exe -Backend=Jolt -Profile=Normalized -Scenario=M3.BoxPile -Param:N=5000 [-Frames=600] [-Threads=1]
//                        [-BackendProfile=Default] [-Out=<run dir>] [-Session=<name>] [-ConfigDir=<dir>] [-ini:...] [-ExecCmds=...]
//   PhysicsTestBench.exe -List
//   PhysicsTestBench.exe -Describe -Backend=Jolt [-Profile=..] [-Scenario=.. -Param:..]
//   PhysicsTestBench.exe -Conformance -Backend=PhysX [-BackendProfile=UE4Compat] [-Profile=UEParity] [-Gates=G1,G9] [-Out=<dir>]
//   PhysicsTestBench.exe -SceneHashes=<path>
// -List, -Describe and -SceneHashes print pure JSON on stdout; their log lines go to stderr (M6, for the M7 / M8 tools).
//
// No renderer is linked (the exe imports neither d3d11.dll nor dxgi.dll: CTest PhysicsTest.Probe.BenchNoRenderer).
// Exit codes: see BenchmarkRunner.h (0 ok, 2 gate / preflight / command line, 3 engine error, 5 capacity, 6 no backend).
// wmain: the command line arrives as UTF-16 and reaches FCommandLine as UTF-8 (FUtf8ArgV), like PhysicsTestAutomation.
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "PhysicsTestScenario.h"
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
	FScenarioRegistry::CheckNotEmpty("PhysicsTestBench");   // WHOLE_ARCHIVE reached the registrars (A.12)

	FBenchRunSpec Spec;
	std::string Error;
	const bool bParsed = FBenchRunSpec::FromCommandLine(FCommandLine::Get(), Spec, Error);
	// A JSON mode owns stdout from the first line on: every log line (ours and the backends') goes to stderr (M6, lead (g)).
	if (Spec.bList || Spec.bDescribe || !Spec.SceneHashesPath.empty())
	{
		Logging::SetAllLinesToStderr(true);
	}
	if (!bParsed)
	{
		UE_LOG(LogPhysicsTest, Error, "PhysicsTestBench: %s", Error.c_str());
		return static_cast<int>(EBenchExit::GateFailed);
	}
	UE_LOG(LogPhysicsTest, Display, "PhysicsTestBench | preset %s | %s | flags %s", PT_BUILD_PRESET, PT_BUILD_CONFIGURATION, PT_BUILD_FLAG_PROFILE);
	EBenchExit Exit = EBenchExit::Ok;
	if (Spec.bList)
	{
		Exit = FBenchmarkRunner::List();
	}
	else if (Spec.bDescribe)
	{
		Exit = FBenchmarkRunner::Describe(Spec);
	}
	else if (Spec.bConformance)
	{
		Exit = FBenchmarkRunner::Conformance(Spec);
	}
	else if (!Spec.SceneHashesPath.empty())
	{
		Exit = FBenchmarkRunner::WriteSceneHashes(Spec);
	}
	else
	{
		FBenchmarkRunner Runner;
		Exit = Runner.Run(Spec);
	}
	return static_cast<int>(Exit);
}
