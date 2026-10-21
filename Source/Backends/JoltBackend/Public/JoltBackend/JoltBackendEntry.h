// JoltBackendEntry.h - the static entry of the Jolt backend (PT::Backend::Jolt), for the generated backend registry and for
// tests. Only the C ABI is visible here: no Jolt header, no JPH_* definition reaches a TU that includes this.
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

extern "C" const PtBackendApi* PT_CALL PT_GetBackendApi_Jolt(uint32_t RequestedAbiMajor);
