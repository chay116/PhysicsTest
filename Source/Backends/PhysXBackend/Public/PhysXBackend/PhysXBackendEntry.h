// PhysXBackendEntry.h - the static entry of the PhysX backend (PT::Backend::PhysX), for the generated backend registry
// and for tests. Only the C ABI is visible here: no PhysX header, no PX_* definition reaches a TU that includes this.
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

extern "C" const PtBackendApi* PT_CALL PT_GetBackendApi_PhysX(uint32_t RequestedAbiMajor);
