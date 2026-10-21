// NullBackendDll.cpp - the DLL entry of PtBackend_Null.dll (A.13): the only export is PT_GetBackendApi, as for the Chaos
// DLL (02_Architecture 7.1). Used by the DLL-path ABI gates (A0-A7, A9) and as the Chaos bridge's load-path template.
// The DLL has its own TBackendApiProvider<FNullBackend> state (module state, scenes, last error), separate from the
// static backend linked into the same process.
#include "NullBackend.h"

PT_IMPLEMENT_BACKEND_MODULE(PhysicsTest::NullBackend::FNullBackend)

// The NO_REINIT test double (M6 round 1, docs/Abi.md 9.1 decision 7): PT_NULL_DLL_NO_REINIT=1 in the environment makes this
// DLL declare PT_MODULE_NO_REINIT, and the Kit then refuses an InitGamePhys after TermGamePhys (PtBackendKit.h) as the
// Chaos bridge must. PhysicsTest.Bench.Conformance runs every ABI gate and the physics gates on it, one process each.
// Read once, when the host first asks for the table; KERNEL32 only (gate A9). Windows.h comes after the module macro so
// its macros cannot touch the Kit's code (WIN32_LEAN_AND_MEAN and NOMINMAX come from the toolchain, PTToolchain.cmake).
#include <Windows.h>

namespace PhysicsTest::NullBackend
{
	uint32 GetNullModuleFlags()
	{
		char Value[8] = {};
		const DWORD Length = GetEnvironmentVariableA("PT_NULL_DLL_NO_REINIT", Value, static_cast<DWORD>(sizeof(Value)));
		return Length == 1 && Value[0] == '1' ? PT_MODULE_NO_REINIT : 0u;
	}
}
