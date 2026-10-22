// JoltLayers.h - collision layers and the UE filter for the Jolt backend (02_Architecture 8.3, DL27). [MVP M3]
//   Broad phase: two layers, NON_MOVING (static bodies) and MOVING (dynamic and kinematic), the layout Jolt recommends.
//   Object layers: the same two. Static-static pairs never collide; Jolt itself never makes contacts between two
//   non-dynamic bodies (BodyCreationSettings::mCollideKinematicVsNonDynamic stays false), as Chaos.
//   UE channels: every body carries a CollisionGroup with the shared FJoltUEGroupFilter. GroupID = the body's 32-bit
//   BlockMask, SubGroupID = its ObjectType in the low 5 bits and its harness body index above them (M9), so the filter is
//   UE's mutual-block test without a table lookup:
//     block = (A.BlockMask & (1 << B.ObjectType)) && (B.BlockMask & (1 << A.ObjectType))
//   (Chaos FShapeFilterData::NarrowFilter, CollisionFilterData.cpp:263-272 [V]).
//   Joints (M9, docs/Abi.md 9.4 item 5): PT_JOINTF_DISABLE_COLLISION (UE bDisableCollision) adds the pair of body indices to
//   the filter's disabled-pair list; a pair on it never collides, in both directions, and every other filter result is
//   unchanged. Jolt never disables the collision of constrained bodies itself (a constraint does not touch the filters), so
//   a joint without the flag leaves the pair colliding, as UE's default bDisableCollision = false.
#pragma once

#include "JoltIncludes.h"
#include "PtTypes.h"

namespace PhysicsTest::JoltBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	namespace Layers
	{
		inline constexpr JPH::ObjectLayer NonMoving = 0;
		inline constexpr JPH::ObjectLayer Moving = 1;
		inline constexpr JPH::uint NumLayers = 2;
	}

	class FJoltBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return Layers::NumLayers; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer Layer) const override
		{
			return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(Layer == Layers::NonMoving ? 0 : 1));
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer Layer) const override
		{
			return Layer.GetValue() == 0 ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class FJoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer Layer, const JPH::BroadPhaseLayer BroadPhaseLayer) const override
		{
			return Layer == Layers::Moving || BroadPhaseLayer.GetValue() == 1;
		}
	};

	class FJoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer Layer0, const JPH::ObjectLayer Layer1) const override
		{
			return Layer0 == Layers::Moving || Layer1 == Layers::Moving;
		}
	};

	// SubGroupID layout (see the file comment): ObjectType in bits 0-4 (PT_NUM_COLLISION_CHANNELS = 32), the body index above.
	// Jolt's body index has 23 bits (BodyID::cMaxBodyIndex), so every scene's index fits in the remaining 27.
	inline constexpr uint32 GSubGroupChannelBits = 5;
	inline constexpr uint32 GSubGroupChannelMask = (1u << GSubGroupChannelBits) - 1u;
	inline constexpr uint32 GSubGroupMaxBodyIndex = 0xFFFFFFFFu >> GSubGroupChannelBits;
	inline uint32 MakeSubGroupID(const uint32 ObjectType, const uint32 BodyIndex) { return (ObjectType & GSubGroupChannelMask) | (BodyIndex << GSubGroupChannelBits); }

	// UE's mutual-block test on the CollisionGroup fields, then the joints' disabled pairs (see the file comment). One instance
	// per scene, ref-counted by Jolt through the bodies' CollisionGroup. CanCollide runs on Jolt's job threads during
	// PhysicsSystem::Update; the pair list changes only between steps (CreateConstraints / ReleaseActors in Idle).
	class FJoltUEGroupFilter final : public JPH::GroupFilter
	{
	public:
		bool CanCollide(const JPH::CollisionGroup& Group0, const JPH::CollisionGroup& Group1) const override
		{
			const uint32 Mask0 = Group0.GetGroupID();
			const uint32 Mask1 = Group1.GetGroupID();
			const uint32 Sub0 = Group0.GetSubGroupID();
			const uint32 Sub1 = Group1.GetSubGroupID();
			if ((Mask0 & (1u << (Sub1 & GSubGroupChannelMask))) == 0u || (Mask1 & (1u << (Sub0 & GSubGroupChannelMask))) == 0u)
			{
				return false;
			}
			return DisabledPairs.empty() || !IsPairDisabled(Sub0 >> GSubGroupChannelBits, Sub1 >> GSubGroupChannelBits);
		}

		// One PT_JOINTF_DISABLE_COLLISION joint between two bodies. Counted per pair, so the pair collides again only when its
		// last such joint is released.
		void AddDisabledPair(const uint32 BodyA, const uint32 BodyB)
		{
			const uint64 Key = MakeKey(BodyA, BodyB);
			const uint32 Slot = LowerBound(Key);
			if (Slot < DisabledPairs.size() && DisabledPairs[Slot] == Key)
			{
				++PairJointCounts[Slot];
				return;
			}
			DisabledPairs.insert(DisabledPairs.begin() + Slot, Key);
			PairJointCounts.insert(PairJointCounts.begin() + Slot, 1u);
		}

		void RemoveDisabledPair(const uint32 BodyA, const uint32 BodyB)
		{
			const uint64 Key = MakeKey(BodyA, BodyB);
			const uint32 Slot = LowerBound(Key);
			if (Slot < DisabledPairs.size() && DisabledPairs[Slot] == Key && --PairJointCounts[Slot] == 0u)
			{
				DisabledPairs.erase(DisabledPairs.begin() + Slot);
				PairJointCounts.erase(PairJointCounts.begin() + Slot);
			}
		}

		bool IsPairDisabled(const uint32 BodyA, const uint32 BodyB) const
		{
			const uint64 Key = MakeKey(BodyA, BodyB);
			const uint32 Slot = LowerBound(Key);
			return Slot < DisabledPairs.size() && DisabledPairs[Slot] == Key;
		}

		uint32 GetNumDisabledPairs() const { return static_cast<uint32>(DisabledPairs.size()); }

	private:
		static uint64 MakeKey(const uint32 BodyA, const uint32 BodyB)
		{
			return BodyA < BodyB ? (static_cast<uint64>(BodyA) << 32) | BodyB : (static_cast<uint64>(BodyB) << 32) | BodyA;
		}

		// The first slot whose key is not below Key (a hand-written binary search: std::lower_bound would be a COMDAT shared
		// with harness code, docs/Abi.md 5.13).
		uint32 LowerBound(const uint64 Key) const
		{
			uint32 Low = 0;
			uint32 High = static_cast<uint32>(DisabledPairs.size());
			while (Low < High)
			{
				const uint32 Middle = Low + (High - Low) / 2u;
				if (DisabledPairs[Middle] < Key)
				{
					Low = Middle + 1u;
				}
				else
				{
					High = Middle;
				}
			}
			return Low;
		}

		JPH::Array<uint64> DisabledPairs;     // sorted (lower index << 32 | higher index)
		JPH::Array<uint32> PairJointCounts;   // per DisabledPairs slot: the live DISABLE_COLLISION joints of that pair
	};
}
