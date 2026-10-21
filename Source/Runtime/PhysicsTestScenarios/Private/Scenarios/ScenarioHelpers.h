// ScenarioHelpers.h - small authoring helpers shared by the scenarios (docs/Scenes.md conventions).
#pragma once

#include "PhysicsTestScenario.h"
#include "PhysScene.h"

#include <string>

namespace PhysicsTest::ScenarioHelpers
{
	// The ground of every scene with one: a static box 200 m x 200 m x 1 m whose top face is z = 0 (body index 0).
	inline constexpr double GroundHalfExtentXY = 10000.0;   // cm
	inline constexpr double GroundHalfThickness = 50.0;     // cm

	inline FBodyInstance* SpawnGround(FPhysScene& Scene, const FScenarioContext& Context, const FPhysicalMaterial* Material)
	{
		FBodySetup Setup;
		Setup.BodySetupName = "Ground";
		Setup.AggGeom.BoxElems.emplace_back(2.0 * GroundHalfExtentXY, 2.0 * GroundHalfExtentXY, 2.0 * GroundHalfThickness);   // full extents
		Setup.PhysMaterial = Material;
		const FBodySetup* GroundSetup = Scene.CreateBodySetup(std::move(Setup));
		FBodyInstance Template = Context.MakeBodyTemplate();
		Template.BodyName = "Ground";
		Template.bStatic = true;
		Template.bSimulatePhysics = false;
		return Scene.SpawnBody(Template, GroundSetup, FTransform(FVector(0.0, 0.0, -GroundHalfThickness)));
	}

	// A cube body setup of full edge Edge (cm), centred on the actor origin.
	inline const FBodySetup* MakeCubeSetup(FPhysScene& Scene, const char* Name, const double Edge, const FPhysicalMaterial* Material)
	{
		FBodySetup Setup;
		Setup.BodySetupName = Name;
		Setup.AggGeom.BoxElems.emplace_back(Edge);
		Setup.PhysMaterial = Material;
		return Scene.CreateBodySetup(std::move(Setup));
	}

	// Parses a non-negative integer parameter (already validated against the scenario's allowed values).
	inline uint32 ParseUnsignedParam(const std::string& Text, const uint32 Fallback)
	{
		uint32 Value = 0;
		if (Text.empty())
		{
			return Fallback;
		}
		for (const char C : Text)
		{
			if (C < '0' || C > '9')
			{
				return Fallback;
			}
			Value = Value * 10u + static_cast<uint32>(C - '0');
		}
		return Value;
	}
}
