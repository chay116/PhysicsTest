// BulletBackendEntry.h - the static entry of the Bullet backend (PT::Backend::Bullet), for the generated backend registry
// and for tests. Only the C ABI is visible here: no Bullet header, no BT_* definition reaches a TU that includes this.
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

extern "C" const PtBackendApi* PT_CALL PT_GetBackendApi_Bullet(uint32_t RequestedAbiMajor);
