// PhysXIncludes.h - the ONLY file of the PhysX backend that includes the PhysX SDK (02_Architecture 8.2, A.14; UE4 had
// the same single entry point, PhysXIncludes.h). PxPhysicsAPI.h pulls in the core SDK and the extensions
// (PxDefaultCpuDispatcherCreate, PxInitExtensions). Backend side only: PT_THIRD_PARTY_INCLUDES_START / END push and pop
// the warning level, nothing else (backend TUs never include harness Core, DL03).
//
// Configuration macros (03_WorkPlan M5 decision, cmake/ThirdParty/PhysX.cmake "OPEN" note): PT::PhysX forwards only
// PX_PHYSX_STATIC_LIB and DISABLE_CUDA_PHYSX, so an adapter TU sees PX_DEBUG = PX_CHECKED = 0 (PxPreprocessor.h:170-177
// defaults) while the dbg libraries are built with PX_DEBUG=1 PX_CHECKED=1 PX_SUPPORT_PVD=1. That is deliberate: no
// public PhysX header changes a type layout on these macros (they only gate inline parameter checks PX_CHECK_*,
// PX_WARN_ONCE, PX_ASSERT and profile zones: PxErrors.h:60-90, PxAssert.h, PxProfileZone.h, PxPhysXCommonConfig.h:11
// [V]), so the adapter sees release-style inline helpers in every configuration and the libraries keep their own checks.
#pragma once

#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <PxPhysicsAPI.h>
PT_THIRD_PARTY_INCLUDES_END
