// BulletCombine.h - UE material combine rules inside Bullet (02_Architecture 8.4, DL41). [MVP M5]
// Bullet computes a contact point's friction and restitution through process-wide function pointers
// (gCalculateCombinedFrictionCallback / gCalculateCombinedRestitutionCallback, btManifoldResult.h:37-44, .cpp:24-25,
// 137-138 [V]) that get the two btCollisionObjects only. That is why the Bullet module is PT_MODULE_GLOBAL_SCENE_SETTINGS:
// the rule is a module option (CombineRule, InitGamePhys) and one process holds one rule. Each object therefore carries
// what the callback needs: getFriction() / getRestitution() are the raw UE material values (PtPhysicalMaterial Friction /
// Restitution of the body's first element) and the user index 2 holds the host-resolved combine modes
// (PackCombineModes: bits 0-1 friction, bits 2-3 restitution). The user index is the harness body index.
// The callbacks return BackendCommon's UE / Chaos values: C(F0, F1) with the larger mode (PtCombine.h). Bullet keeps ONE
// friction coefficient per point (m_combinedFriction), so static friction is reported as material.static_friction_rule =
// dynamic_only, as for Jolt (DL41). Bullet's own rule (CombineRule=Bullet: F0 * F1 clamped to [-10, 10], R0 * R1,
// btManifoldResult.cpp:56-71 [V]) stays selectable for a native-rule variant.
#pragma once

#include "BulletIncludes.h"
#include "PtCombine.h"
#include "PtTypes.h"

namespace PhysicsTest::BulletBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	inline btScalar CombineFrictionUE(const btCollisionObject* Body0, const btCollisionObject* Body1)
	{
		const uint8 Mode = ::PhysicsTest::Abi::ResolveCombineMode(::PhysicsTest::Abi::GetPackedFrictionMode(static_cast<uint32>(Body0->getUserIndex2())),
			::PhysicsTest::Abi::GetPackedFrictionMode(static_cast<uint32>(Body1->getUserIndex2())));
		return btScalar(::PhysicsTest::Abi::CombineValues(double(Body0->getFriction()), double(Body1->getFriction()), Mode));
	}

	inline btScalar CombineRestitutionUE(const btCollisionObject* Body0, const btCollisionObject* Body1)
	{
		const uint8 Mode = ::PhysicsTest::Abi::ResolveCombineMode(::PhysicsTest::Abi::GetPackedRestitutionMode(static_cast<uint32>(Body0->getUserIndex2())),
			::PhysicsTest::Abi::GetPackedRestitutionMode(static_cast<uint32>(Body1->getUserIndex2())));
		return btScalar(::PhysicsTest::Abi::CombineValues(double(Body0->getRestitution()), double(Body1->getRestitution()), Mode));
	}
}
