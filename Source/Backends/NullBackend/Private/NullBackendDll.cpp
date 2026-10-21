// NullBackendDll.cpp - the DLL entry of PtBackend_Null.dll (A.13): the only export is PT_GetBackendApi, as for the Chaos
// DLL (02_Architecture 7.1). Used by the DLL-path ABI gates (A0-A7, A9) and as the Chaos bridge's load-path template.
// The DLL has its own TBackendApiProvider<FNullBackend> state (module state, scenes, last error), separate from the
// static backend linked into the same process.
#include "NullBackend.h"

PT_IMPLEMENT_BACKEND_MODULE(PhysicsTest::NullBackend::FNullBackend)

// Test doubles read from the environment once, when the host first asks for the table (KERNEL32 only, gate A9). Windows.h
// comes after the module macro so its macros cannot touch the Kit's code (WIN32_LEAN_AND_MEAN and NOMINMAX come from the
// toolchain, PTToolchain.cmake).
//   PT_NULL_DLL_NO_REINIT=1   (M6 round 1, docs/Abi.md 9.1 decision 7) the DLL declares PT_MODULE_NO_REINIT and the Kit refuses
//                             an InitGamePhys after TermGamePhys, as the Chaos bridge must. PhysicsTest.Bench.Conformance runs
//                             every ABI gate and the physics gates on it, one process each.
//   PT_NULL_DLL_UE_RUNTIME=1  (M7, 03 M7: "UE_RUNTIME 플래그를 켠 Null DLL 시험 빌드로 TerminateProcess 경로를 확인한다") the DLL
//                             declares PT_MODULE_UE_RUNTIME: the bench writes every output and exit.json, then ends the
//                             process with TerminateProcess (02 12.2 step 13), so this DLL never sees DLL_PROCESS_DETACH.
//   PT_NULL_DLL_REPORT_DETACH=1  DllMain writes one line to stderr at DLL_PROCESS_DETACH during process exit: the CTest pair
//                             PhysicsTest.Bench.UeRuntimeTerminate / UeRuntimeControl tells the two process ends apart with it.
//   PT_NULL_DLL_PHASE_TIMERS=1  (M7 fix round 0) the DLL declares PT_CAP_PHASE_TIMERS with one timer ("null.steps", the wall
//                             time of a frame's steps, QueryPerformanceCounter) for scenes created with PT_INSTR_PHASES: the
//                             bench's phases tier fills timings.csv solver_ns from it (CTest PhysicsTest.Bench.InstrumentPhases).
#include <Windows.h>

namespace PhysicsTest::NullBackend
{
	namespace
	{
		bool IsEnvironmentFlagSet(const char* Name)
		{
			char Value[8] = {};
			const DWORD Length = GetEnvironmentVariableA(Name, Value, static_cast<DWORD>(sizeof(Value)));
			return Length == 1 && Value[0] == '1';
		}
	}

	bool GetNullPhaseTimersEnabled()
	{
		return IsEnvironmentFlagSet("PT_NULL_DLL_PHASE_TIMERS");
	}

	int64 GetNullClockNs()
	{
		LARGE_INTEGER Frequency = {};
		LARGE_INTEGER Counter = {};
		QueryPerformanceFrequency(&Frequency);
		QueryPerformanceCounter(&Counter);
		return Frequency.QuadPart > 0 ? static_cast<int64>(static_cast<double>(Counter.QuadPart) * (1.0e9 / static_cast<double>(Frequency.QuadPart))) : 0;
	}

	uint32 GetNullModuleFlags()
	{
		return (IsEnvironmentFlagSet("PT_NULL_DLL_NO_REINIT") ? PT_MODULE_NO_REINIT : 0u) | (IsEnvironmentFlagSet("PT_NULL_DLL_UE_RUNTIME") ? PT_MODULE_UE_RUNTIME : 0u);
	}
}

BOOL WINAPI DllMain(HINSTANCE /*Instance*/, const DWORD Reason, LPVOID Reserved)
{
	// Reserved != NULL at DLL_PROCESS_DETACH: the process is exiting (ExitProcess). TerminateProcess skips this call.
	if (Reason == DLL_PROCESS_DETACH && Reserved != nullptr && PhysicsTest::NullBackend::IsEnvironmentFlagSet("PT_NULL_DLL_REPORT_DETACH"))
	{
		static const char Line[] = "PtBackend_Null.dll: DLL_PROCESS_DETACH at process exit (a normal ExitProcess, not TerminateProcess)\n";
		DWORD Written = 0;
		WriteFile(GetStdHandle(STD_ERROR_HANDLE), Line, static_cast<DWORD>(sizeof(Line) - 1), &Written, nullptr);
	}
	return TRUE;
}
