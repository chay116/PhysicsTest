// PhysicsTestBench.exe - one bench run per process (02_Architecture 12, A.12). [MVP M3 minimal; M6 -Conformance; M7 full]
//
//   PhysicsTestBench.exe -Backend=Jolt -Profile=Normalized -Scenario=M3.BoxPile -Param:N=5000 [-Frames=600] [-Threads=1]
//                        [-Affinity=0x1] [-RunMode=Bench] [-Instrument=none] [-HashEvery=0] [-StatesEvery=0] [-Out=<run dir>] ...
//   PhysicsTestBench.exe -Gate=G8 [-Runs=3] <run arguments>  |  -Gate=G11 <run arguments>        (M7 multi-process gates)
//   PhysicsTestBench.exe -BusyHelper=<seconds> -Affinity=0x1                                       (M7 exit-4 test load)
//   PhysicsTestBench.exe -List
//   PhysicsTestBench.exe -Describe -Backend=Jolt [-Profile=..] [-Scenario=.. -Param:..]
//   PhysicsTestBench.exe -Conformance -Backend=PhysX [-BackendProfile=UE4Compat] [-Profile=UEParity] [-Gates=G1,G9] [-Out=<dir>]
//   PhysicsTestBench.exe -SceneHashes=<path>
// -List, -Describe and -SceneHashes print pure JSON on stdout; their log lines go to stderr (M6, for the M7 / M8 tools).
// Every option is in BenchmarkRunner.h.
//
// No renderer is linked (the exe imports neither d3d11.dll nor dxgi.dll: CTest PhysicsTest.Probe.BenchNoRenderer).
// Exit codes: see BenchmarkRunner.h (0 ok, 2 gate / preflight / usage, 3 engine error, 4 contaminated host, 5 capacity,
// 6 no backend); a run (and -Conformance, -Gate=) also writes them to exit.json. A process that loaded a PT_MODULE_UE_RUNTIME
// module ends with TerminateProcess after its outputs, in every mode (02 12.2 step 13).
// wmain: the command line arrives as UTF-16 and reaches FCommandLine as UTF-8 (FUtf8ArgV), like PhysicsTestAutomation.
// AllocationHook.cpp in this exe replaces the global operator new / delete (the timed loop's allocation count, M7).
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
		// A usage error still leaves exit.json when the run directory is known (M7: the runner trusts exit.json).
		if (!Spec.OutDir.empty())
		{
			std::error_code Ignored;
			std::filesystem::create_directories(Spec.OutDir, Ignored);
			FExitRecord Record;
			Record.Code = EBenchExit::GateFailed;
			Record.Status = "usage_error";
			Record.Category = "usage";
			Record.Reason = Error;
			Record.WriteAndFlush(Spec.OutDir);
		}
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
	else if (Spec.BusyHelperSeconds > 0.0)
	{
		Exit = FBenchmarkRunner::RunBusyHelper(Spec);
	}
	else if (!Spec.MultiRunGate.empty())
	{
		Exit = FBenchmarkRunner::RunMultiProcessGate(Spec);
	}
	else
	{
		FBenchmarkRunner Runner;
		Exit = Runner.Run(Spec);
	}
	// 02 12.2 step 13: a UE_RUNTIME module (the Chaos DLL) is never unloaded; its singletons must not run in the CRT's DLL
	// detach after the outputs are written. Run() already terminated; the other modes end here.
	if (FBenchmarkRunner::HasUeRuntimeModule())
	{
		FBenchmarkRunner::TerminateProcessNow(static_cast<int32>(Exit));
	}
	return static_cast<int>(Exit);
}
