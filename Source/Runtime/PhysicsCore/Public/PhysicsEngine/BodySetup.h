// BodySetup.h - FBodySetup, the shared collision geometry of bodies (UE: Engine/Classes/PhysicsEngine/BodySetup.h,
// UBodySetup without cooking or UObject). [MVP M1]
#pragma once

#include "Math/Vector.h"
#include "PhysicsEngine/AggregateGeom.h"

#include <string>

namespace PhysicsTest
{
	struct FPhysicalMaterial;

	class FBodySetup
	{
	public:
		std::string BodySetupName;
		FKAggregateGeom AggGeom;
		// Default material of the setup (UE: UBodySetup::PhysMaterial); a body's PhysMaterialOverride wins.
		const FPhysicalMaterial* PhysMaterial = nullptr;
		EPhysicsType PhysicsType = EPhysicsType::PhysType_Default;
		// Metadata, as in UE: the build scale UBodySetup::RescaleSimpleCollision has already baked into AggGeom
		// (BodySetup.cpp:756-812 [V]). Neither BodyUtils nor the marshalling applies it again (UE's GetScaledVolume and
		// ChaosInterfaceUtils ignore it too); only FBodyInstance::Scale3D scales the geometry (02_Architecture 7.1).
		FVector BuildScale3D = FVector(1.0);

		// Volume of the elements at the body scale Scale3D (UE: UBodySetup::GetScaledVolume -> FKAggregateGeom,
		// BodySetup.cpp:2417-2425 [V]).
		[[nodiscard]] double GetScaledVolume(const FVector& Scale3D) const { return AggGeom.GetScaledVolume(Scale3D); }
	};
}
