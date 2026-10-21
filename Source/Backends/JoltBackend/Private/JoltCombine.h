// JoltCombine.h - UE material combine rules inside Jolt (02_Architecture 8.3, DL41). [MVP M3]
// Jolt keeps ONE friction coefficient per contact pair (ContactConstraintManager mCombinedFriction) and calls a plain
// function pointer per pair (PhysicsSystem::SetCombineFriction / SetCombineRestitution), without user context. Each body
// therefore carries what the callback needs: Body::GetFriction() / GetRestitution() are the raw UE material values
// (PtPhysicalMaterial Friction / Restitution) and the body user data holds the host-resolved combine modes:
//   bits  0-31  harness body index
//   bits 32-33  friction combine mode   (PT_COMBINE_*)
//   bits 34-35  restitution combine mode
// The callbacks return BackendCommon's UE / Chaos values: C(F0, F1) with the larger mode (PtCombine.h). Static friction
// cannot be represented (one coefficient), so the rule is reported as material.static_friction_rule = dynamic_only; with
// UE's default StaticFriction 0 <= Friction the dynamic value equals Chaos' pair static friction anyway (DL41).
#pragma once

#include "JoltIncludes.h"
#include "PtCombine.h"
#include "PtTypes.h"

namespace PhysicsTest::JoltBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	inline uint64 MakeBodyUserData(const uint32 BodyIndex, const uint8 FrictionMode, const uint8 RestitutionMode)
	{
		return static_cast<uint64>(BodyIndex) | (static_cast<uint64>(FrictionMode & 3u) << 32) | (static_cast<uint64>(RestitutionMode & 3u) << 34);
	}
	inline uint32 GetBodyIndex(const uint64 UserData) { return static_cast<uint32>(UserData & 0xFFFFFFFFull); }
	inline uint8 GetFrictionMode(const uint64 UserData) { return static_cast<uint8>((UserData >> 32) & 3u); }
	inline uint8 GetRestitutionMode(const uint64 UserData) { return static_cast<uint8>((UserData >> 34) & 3u); }

	inline float CombineFrictionUE(const JPH::Body& Body0, const JPH::SubShapeID& /*SubShape0*/, const JPH::Body& Body1, const JPH::SubShapeID& /*SubShape1*/)
	{
		const uint8 Mode = ::PhysicsTest::Abi::ResolveCombineMode(GetFrictionMode(Body0.GetUserData()), GetFrictionMode(Body1.GetUserData()));
		return static_cast<float>(::PhysicsTest::Abi::CombineValues(Body0.GetFriction(), Body1.GetFriction(), Mode));
	}

	inline float CombineRestitutionUE(const JPH::Body& Body0, const JPH::SubShapeID& /*SubShape0*/, const JPH::Body& Body1, const JPH::SubShapeID& /*SubShape1*/)
	{
		const uint8 Mode = ::PhysicsTest::Abi::ResolveCombineMode(GetRestitutionMode(Body0.GetUserData()), GetRestitutionMode(Body1.GetUserData()));
		return static_cast<float>(::PhysicsTest::Abi::CombineValues(Body0.GetRestitution(), Body1.GetRestitution(), Mode));
	}
}
