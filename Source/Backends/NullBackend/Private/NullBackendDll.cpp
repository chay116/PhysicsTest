// NullBackendDll.cpp - the DLL entry of PtBackend_Null.dll (A.13): the only export is PT_GetBackendApi, as for the Chaos
// DLL (02_Architecture 7.1). Used by the DLL-path ABI gates (A0-A7, A9) and as the Chaos bridge's load-path template.
// The DLL has its own TBackendApiProvider<FNullBackend> state (module state, scenes, last error), separate from the
// static backend linked into the same process.
#include "NullBackend.h"

PT_IMPLEMENT_BACKEND_MODULE(PhysicsTest::NullBackend::FNullBackend)
