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
// [V]), so no layout differs. In dbg only (M6, M5 review): the adapter and the checked libraries instantiate the same
// templates and inline functions (physx::PxArray<uint32>, PxArray<uint8>, PxVec3 / PxQuat / PxTransform inlines) with
// different PX_ASSERT content (PxArray.h:155, :166), ODR-different COMDATs that /Od keeps out of line; the linker keeps one
// copy, so either the libraries lose some checks or adapter call sites get the checked __debugbreak versions. Accepted:
// dbg is not a measurement build and the behaviour is benign; dev / test link the release libraries (no asserts on either
// side). Probe.AdapterComdatsPhysX cannot see it (the physx namespace is allowed). Revisit when a preset gives the adapter
// another /arch (M10 test-physx).
#pragma once

#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <PxPhysicsAPI.h>
PT_THIRD_PARTY_INCLUDES_END
