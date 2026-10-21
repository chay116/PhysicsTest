// PhysXBackendTestHooks.h - plain C test hooks of the PhysX backend (PT::Backend::PhysX) for the automation exe's
// PhysicsTest.PhysXBackend.* tests: the UE4 filter shader's word layout and its truth table, evaluated inside the backend.
// No PhysX header and no PX_* definition reaches a TU that includes this (CTest PhysicsTest.Probe.HarnessSdkIsolation):
// the constants below are the PhysX values, and the backend static_asserts that they are (PhysXBackend.cpp).
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

#define PT_PHYSX_TEST_OBJECT_STATIC     0u
#define PT_PHYSX_TEST_OBJECT_KINEMATIC  1u
#define PT_PHYSX_TEST_OBJECT_DYNAMIC    2u

/* PxFilterFlag values (PxFiltering.h) */
#define PT_PHYSX_TEST_FILTER_DEFAULT    0u
#define PT_PHYSX_TEST_FILTER_KILL       (1u << 0)
#define PT_PHYSX_TEST_FILTER_SUPPRESS   (1u << 1)

/* PxPairFlag values (PxFiltering.h) */
#define PT_PHYSX_TEST_PAIR_SOLVE_CONTACT            (1u << 0)
#define PT_PHYSX_TEST_PAIR_MODIFY_CONTACTS          (1u << 1)
#define PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_FOUND       (1u << 2)
#define PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_PERSISTS    (1u << 3)
#define PT_PHYSX_TEST_PAIR_NOTIFY_CONTACT_POINTS    (1u << 9)
#define PT_PHYSX_TEST_PAIR_DETECT_DISCRETE_CONTACT  (1u << 10)
#define PT_PHYSX_TEST_PAIR_DETECT_CCD_CONTACT       (1u << 11)

/* EPhysXFilterDataFlags (UE5 PhysicsFiltering.h:18-28) */
#define PT_PHYSX_TEST_EPDF_SIMPLE_COLLISION          0x0001u
#define PT_PHYSX_TEST_EPDF_COMPLEX_COLLISION         0x0002u
#define PT_PHYSX_TEST_EPDF_CCD                       0x0004u
#define PT_PHYSX_TEST_EPDF_CONTACT_NOTIFY            0x0008u
#define PT_PHYSX_TEST_EPDF_STATIC_SHAPE              0x0010u
#define PT_PHYSX_TEST_EPDF_MODIFY_CONTACTS           0x0020u
#define PT_PHYSX_TEST_EPDF_KINEMATIC_KINEMATIC_PAIRS 0x0040u

typedef struct PtPhysXTestFilterObject
{
	uint32_t ObjectKind;   /* PT_PHYSX_TEST_OBJECT_* */
	uint32_t Words[4];     /* PxFilterData word0..word3 */
} PtPhysXTestFilterObject;

typedef struct PtPhysXTestFilterResult
{
	uint32_t FilterFlags;  /* PT_PHYSX_TEST_FILTER_* */
	uint32_t PairFlags;    /* PT_PHYSX_TEST_PAIR_* (written only for a pair the shader keeps) */
} PtPhysXTestFilterResult;

#ifdef __cplusplus
extern "C" {
#endif
/* The simulation filter words the backend gives a shape: word0 body index, word1 block mask, word2 component id (the body
   index), word3 [6 bits extra filter][5 bits channel][21 bits EPDF flags]. */
void PT_CALL PT_PhysXTest_MakeSimFilterData(uint32_t BodyIndex, uint32_t BlockMask, uint32_t Channel, uint32_t EpdfFlags, uint32_t OutWords[4]);
/* Runs the backend's PhysXSimFilterShader on the two objects (the default filter rules: no constant block). */
void PT_CALL PT_PhysXTest_RunSimFilterShader(const PtPhysXTestFilterObject* A, const PtPhysXTestFilterObject* B, PtPhysXTestFilterResult* Out);
/* The same with the variant's [M] filter rules in the constant block (PhysicsFiltering.h FPhysXFilterShaderRules):
   NonBlockingSuppress 0 = NonBlockingPairFilter=Kill, 1 = Suppress; KinematicStaticAlways 0 = KinematicStaticPairFilter=
   UnlessKinematicPairs, 1 = Always. */
void PT_CALL PT_PhysXTest_RunSimFilterShaderWithRules(const PtPhysXTestFilterObject* A, const PtPhysXTestFilterObject* B, uint32_t NonBlockingSuppress,
	uint32_t KinematicStaticAlways, PtPhysXTestFilterResult* Out);
#ifdef __cplusplus
}
#endif
