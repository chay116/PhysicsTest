// JoltLayers.h - collision layers and the UE filter for the Jolt backend (02_Architecture 8.3, DL27). [MVP M3]
//   Broad phase: two layers, NON_MOVING (static bodies) and MOVING (dynamic and kinematic), the layout Jolt recommends.
//   Object layers: the same two. Static-static pairs never collide; Jolt itself never makes contacts between two
//   non-dynamic bodies (BodyCreationSettings::mCollideKinematicVsNonDynamic stays false), as Chaos.
//   UE channels: every body carries a CollisionGroup with the shared FJoltUEGroupFilter. GroupID = the body's 32-bit
//   BlockMask, SubGroupID = its ObjectType, so the filter is UE's mutual-block test without a table lookup:
//     block = (A.BlockMask & (1 << B.ObjectType)) && (B.BlockMask & (1 << A.ObjectType))
//   (Chaos FShapeFilterData::NarrowFilter, CollisionFilterData.cpp:263-272 [V]).
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

	// UE's mutual-block test on the CollisionGroup fields (see the file comment). One instance per scene, ref-counted by
	// Jolt through the bodies' CollisionGroup.
	class FJoltUEGroupFilter final : public JPH::GroupFilter
	{
	public:
		bool CanCollide(const JPH::CollisionGroup& Group0, const JPH::CollisionGroup& Group1) const override
		{
			const uint32 Mask0 = Group0.GetGroupID();
			const uint32 Mask1 = Group1.GetGroupID();
			const uint32 Channel0 = Group0.GetSubGroupID();
			const uint32 Channel1 = Group1.GetSubGroupID();
			return (Mask0 & (1u << Channel1)) != 0u && (Mask1 & (1u << Channel0)) != 0u;
		}
	};
}
