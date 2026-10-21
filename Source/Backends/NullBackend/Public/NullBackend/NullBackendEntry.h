// NullBackendEntry.h - the static entry of the Null backend (PT::Backend::Null), for the generated backend registry and
// for ABI gate tests. The DLL build exports PT_GetBackendApi instead (PtBackend_Null.dll).
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

extern "C" const PtBackendApi* PT_CALL PT_GetBackendApi_Null(uint32_t RequestedAbiMajor);
