// PhysicsInitialization.h - UE4's InitGamePhys / TermGamePhys for PhysX, re-implemented clean room (02_Architecture 8.2,
// A.14): GPhysXFoundation and GPhysXSDK, one per process. [MVP M5]   PT_BACKEND_NS=PhysX
//
// PhysX keeps one PxFoundation and one PxPhysics per process, and the PxTolerancesScale is a property of the PxPhysics
// (PxPhysics.h:997 getTolerancesScale; every PxSceneDesc is built from it, PxSceneDesc.h:541 [V]). The scale therefore
// arrives as the process-wide InitGamePhys option ToleranceScale (UE: p.ToleranceScale_Length / _Speed,
// PhysicsInitialization.cpp:18-28 [V]): UE4Compat, -TGS and -Stab share (100, 1000) and run as separate PxScenes of one
// PxPhysics; SdkDefault (1, 10) needs its own process. While the module is initialised the scale cannot change: the
// host registry refuses a second set of init options (PT_MODULE_GLOBAL_SCENE_SETTINGS) and CreateScene refuses a scene
// whose LengthScale contradicts the live scale. TermGamePhys releases both objects, so a later InitGamePhys (the module
// declares no PT_MODULE_NO_REINIT; gate A0 re-initialises) starts a fresh PxPhysics with the scale it is given.
#pragma once

#include "PhysXSupport.h"

namespace PhysicsTest::PhysXBackend
{
	struct FPhysXInitOptions
	{
		float ToleranceLength = 100.0f;   // UE4 PxTolerancesScale (100, 1000): cm, cm/s (PhysicsInitialization.cpp:18-28 [V])
		float ToleranceSpeed = 1000.0f;
		bool bToleranceScaleGiven = false;   // false: the UE4Compat default above (no [PhysicsTest.Backend.PhysX.Init] key)
	};

	// One [PhysicsTest.Backend.PhysX.Init] key: ToleranceScale=(Length=<l>,Speed=<s>) (UE struct syntax, keys in any case
	// and order). PT_OK or PT_E_INVALID_ARG with a message.
	PtResult ApplyInitOption(FPhysXInitOptions& Options, ::PhysicsTest::Abi::FStringRef Key, ::PhysicsTest::Abi::FStringRef Value, char (&Error)[256]);

	// PxCreateFoundation -> PxCreatePhysics(PxTolerancesScale) -> PxInitExtensions. PT_OK or PT_E_ENGINE with a message.
	PtResult InitGamePhys(const FPhysXInitOptions& Options, ::PhysicsTest::Abi::FKitServices& Kit, char (&Error)[256]);
	// PxCloseExtensions -> PxPhysics::release -> PxFoundation::release (every scene is gone already).
	void TermGamePhys();

	bool IsPhysXInitialized();
	physx::PxPhysics& GetPhysXSDK();
	physx::PxFoundation& GetPhysXFoundation();
	const FPhysXAllocator& GetPhysXAllocator();
	const FPhysXErrorCallback& GetPhysXErrorCallback();
	// How many PxPhysics this process has created so far (1 per InitGamePhys).
	uint32 GetNumPhysXInitializations();
}
