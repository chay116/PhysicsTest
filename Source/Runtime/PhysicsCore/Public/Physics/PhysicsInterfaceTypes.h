// PhysicsInterfaceTypes.h - enums and handles shared by the PhysicsCore data model (UE: Engine/EngineTypes.h,
// PhysicsCore/PhysicsInterfaceTypesCore.h, Chaos/Particle/ObjectState.h). [MVP M1]
// Enum values keep UE's numbering where a value crosses the backend boundary (EFrictionCombineMode = PT_COMBINE_*,
// EObjectStateType = PT_OBJECT_STATE_*). Enums are enum class : uint8 instead of UE's TEnumAsByte / namespace enums.
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest
{
	// UE: ECollisionEnabled::Type (EngineTypes.h). ProbeOnly / QueryAndProbe are not part of Phase 1.
	enum class ECollisionEnabled : uint8
	{
		NoCollision = 0,
		QueryOnly = 1,
		PhysicsOnly = 2,
		QueryAndPhysics = 3,
	};

	[[nodiscard]] constexpr bool CollisionEnabledHasPhysics(const ECollisionEnabled Value)
	{
		return Value == ECollisionEnabled::PhysicsOnly || Value == ECollisionEnabled::QueryAndPhysics;
	}

	// UE: EFrictionCombineMode::Type (PhysicsCore/PhysicalMaterial.h). Same values as PxCombineMode and PT_COMBINE_*;
	// when two materials disagree the larger value wins (Chaos FChaosPhysicsMaterial::ChooseCombineMode).
	enum class EFrictionCombineMode : uint8
	{
		Average = 0,
		Min = 1,
		Multiply = 2,
		Max = 3,
	};

	// UE: Chaos::EObjectStateType (Chaos/Particle/ObjectState.h:9-18 [V]); the values cross the ABI unchanged.
	enum class EObjectStateType : uint8
	{
		Uninitialized = 0,
		Sleeping = 1,
		Kinematic = 2,
		Static = 3,
		Dynamic = 4,
	};

	// UE: ETeleportType (EngineTypes.h).
	enum class ETeleportType : uint8
	{
		None,
		TeleportPhysics,
		ResetPhysics,
	};

	// UE: ESleepFamily (EngineTypes.h). Sensitive multiplies the material thresholds by 1/20 (BodyInstance.cpp:106 [V]).
	enum class ESleepFamily : uint8
	{
		Normal,
		Sensitive,
		Custom,
	};

	// UE: EPhysicsType (BodySetupEnums.h).
	enum class EPhysicsType : uint8
	{
		PhysType_Default,
		PhysType_Kinematic,
		PhysType_Simulated,
	};

	// Collision channel 0..31. UE 5.8.3 has ECC_MAX = 65 with uint64 masks (EngineTypes.h:1101-1176 [V]); the harness
	// deliberately limits Phase 1 to 32 channels with uint32 masks (UE4 PhysX word3 holds a 5-bit channel, Bullet uses
	// int group/mask). The Chaos bridge widens the masks to uint64 (M13). 02_Architecture 5.1, DL27.
	// An ObjectType >= NumCollisionChannels is invalid: FBodyCollisionFilter::IsValid() is false, its channel mask is 0
	// (never aliased to ObjectType mod 32) and FPhysScene::SpawnBody rejects it.
	// The C ABI (M2) defines the same number as the macro PT_NUM_COLLISION_CHANNELS; the C++ constant has its own
	// name so that including PtBackendAbi.h before this header cannot rewrite the declaration. The ABI side asserts
	// static_assert(NumCollisionChannels == PT_NUM_COLLISION_CHANNELS) where both are visible.
	using ECollisionChannel = uint8;
	inline constexpr uint32 NumCollisionChannels = 32;

	// Host-side handles into a backend scene (02_Architecture 5.1): Index = creation order = insertion order = hash
	// order. In M1 the scene's stub backend fills Index / Serial; from M2 FPhysicsBackendScene does.
	class FPhysicsBackendScene;

	struct FPhysicsActorHandle
	{
		FPhysicsBackendScene* Scene = nullptr;
		uint32 Index = ~0u;
		uint32 Serial = 0;

		[[nodiscard]] bool IsValid() const { return Index != ~0u && Serial != 0; }
	};

	struct FPhysicsConstraintHandle
	{
		FPhysicsBackendScene* Scene = nullptr;
		uint32 Index = ~0u;
		uint32 Serial = 0;

		[[nodiscard]] bool IsValid() const { return Index != ~0u && Serial != 0; }
	};
}
