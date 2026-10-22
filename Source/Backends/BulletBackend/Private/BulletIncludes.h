// BulletIncludes.h - the only file of the Bullet backend that includes Bullet headers (02_Architecture 8.1, 8.4). PT::Bullet
// is linked PRIVATE, so its include path and its BT_* definitions reach the backend TUs only; harness TUs never see them
// (CTest PhysicsTest.Probe.HarnessSdkIsolation). Bullet 3.25 @ 2c204c49, single precision by default, BT_THREADSAFE=0
// (cmake/ThirdParty/PtBullet.cmake): btScalar is float and btVector3 the 16-byte SSE vector (BT_USE_SSE on MSVC x64). The
// adapter keeps no btVector3 / btTransform by value in its own records (they are 16-byte aligned: C4324 padding), only in
// locals.
#pragma once

#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>
#include <BulletCollision/CollisionDispatch/btSimulationIslandManager.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/NarrowPhaseCollision/btPersistentManifold.h>
#include <LinearMath/btIDebugDraw.h>
#include <LinearMath/btPoolAllocator.h>
#include <LinearMath/btTransformUtil.h>
PT_THIRD_PARTY_INCLUDES_END
