// ScenarioHelpers.h - small authoring helpers shared by the scenarios (docs/Scenes.md conventions).
#pragma once

#include "BehaviorScenes.h"
#include "PhysicsEngine/ConstraintUtils.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
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

	// A material from the profile's [PhysicalMaterialDefaults] with the scene's friction / restitution pinned (M6 gate scenes).
	inline const FPhysicalMaterial* MakeMaterial(FPhysScene& Scene, const FScenarioContext& Context, const char* Name, const float Friction,
		const float Restitution)
	{
		FPhysicalMaterial Template = Context.MakeMaterialTemplate();
		Template.Name = Name;
		Template.Friction = Friction;
		Template.StaticFriction = 0.0f;   // UE default: the dynamic value is the pair's static value too (DL41)
		Template.Restitution = Restitution;
		return Scene.CreatePhysicalMaterial(Template);
	}

	// A simulated body template from the profile's [BodyInstanceDefaults] with a pinned mass and zero damping (the gate
	// scenes' references assume no damping unless they pin one themselves).
	inline FBodyInstance MakeGateBodyTemplate(const FScenarioContext& Context, const char* Name, const float MassKg, const bool bGravity)
	{
		FBodyInstance Template = Context.MakeBodyTemplate();
		Template.BodyName = Name;
		Template.bSimulatePhysics = true;
		Template.bStatic = false;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = MassKg;
		Template.LinearDamping = 0.0f;
		Template.AngularDamping = 0.0f;
		Template.bEnableGravity = bGravity;
		return Template;
	}

	// The M3.BoxPile layout rule (docs/Scenes.md 2.3), shared with P1.SmallScene (M7): Columns x Rows x Layers cubes of
	// 100 cm and 1000 kg on the profile's default material, 150 cm between centres in x, y and z, the lowest layer's centres
	// at z = 100 cm, each cube yawed by U(-15, 15) deg and shifted by U(-5, 5) cm in x and y (three draws per cube in layer,
	// row, column order: yaw, dx, dy). Spawns the ground first and flushes. The draw order and the body names are those of
	// M3.BoxPile v1, so its desc_sha256 is unchanged by the move into this helper.
	struct FBoxPileLayout
	{
		uint32 Columns = 10;   // along X
		uint32 Rows = 10;      // along Y
		uint32 Layers = 10;
		[[nodiscard]] uint32 Count() const { return Columns * Rows * Layers; }
	};

	inline constexpr double BoxPileEdge = 100.0;          // cm
	inline constexpr double BoxPileSpacing = 150.0;       // cm between centres
	inline constexpr double BoxPileFirstLayerZ = 100.0;   // cm, centre of the lowest layer
	inline constexpr double BoxPileMaxYawDegrees = 15.0;
	inline constexpr double BoxPileMaxShift = 5.0;        // cm
	inline constexpr float BoxPileMassKg = 1000.0f;

	inline void SpawnBoxPile(FPhysScene& Scene, const FScenarioContext& Context, const FBoxPileLayout& Layout)
	{
		const FPhysicalMaterial* Material = Scene.GetDefaultPhysicalMaterial();
		SpawnGround(Scene, Context, Material);
		const FBodySetup* CubeSetup = MakeCubeSetup(Scene, "Cube100", BoxPileEdge, Material);

		FBodyInstance Template = Context.MakeBodyTemplate();
		Template.bSimulatePhysics = true;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = BoxPileMassKg;
		const double OriginX = -0.5 * BoxPileSpacing * static_cast<double>(Layout.Columns - 1);
		const double OriginY = -0.5 * BoxPileSpacing * static_cast<double>(Layout.Rows - 1);
		uint32 Index = 0;
		for (uint32 Layer = 0; Layer < Layout.Layers; ++Layer)
		{
			for (uint32 Row = 0; Row < Layout.Rows; ++Row)
			{
				for (uint32 Column = 0; Column < Layout.Columns; ++Column)
				{
					const double Yaw = Context.Random.FRandRange(-BoxPileMaxYawDegrees, BoxPileMaxYawDegrees);
					const double ShiftX = Context.Random.FRandRange(-BoxPileMaxShift, BoxPileMaxShift);
					const double ShiftY = Context.Random.FRandRange(-BoxPileMaxShift, BoxPileMaxShift);
					const FVector Location(OriginX + BoxPileSpacing * static_cast<double>(Column) + ShiftX, OriginY + BoxPileSpacing * static_cast<double>(Row) + ShiftY,
						BoxPileFirstLayerZ + BoxPileSpacing * static_cast<double>(Layer));
					Template.BodyName = "Box" + std::to_string(Index++);
					Scene.SpawnBody(Template, CubeSetup, FTransform(FRotator(0.0, Yaw, 0.0), Location));
				}
			}
		}
		Scene.FlushDeferredActors();
	}

	// Capacity hints of a pile of N cubes (bodies + ground; broad-phase pairs and contact manifolds with margin for a
	// collapsed pile: each cube touches at most a handful of neighbours). Overflow is the run's G12 failure (02 9.4).
	inline FScenarioCapacity MakePileCapacity(const uint32 N)
	{
		return FScenarioCapacity{ N + 1, 16u * N + 1024u, 8u * N + 1024u };
	}

	// ---- M9 behaviour scenes ----

	// The same-engine perturbation parameter of the chaotic behaviour scenes (BehaviorScenes.h, docs/Scenes.md 1).
	inline FScenarioParamDesc MakePerturbParamDesc()
	{
		return FScenarioParamDesc{ BehaviorScenes::PerturbParam, "0", {},
			"0 = none; k != 0 = every dynamic body starts 1e-4 cm off along a unit direction drawn from FRandomStream(k) (same-engine sensitivity replicas)",
			EScenarioParamType::Int32 };
	}

	// One draw per dynamic body, in spawn order, from a stream of its own (the layout's draws are unchanged).
	class FPerturbation
	{
	public:
		explicit FPerturbation(const FScenarioContext& Context)
			: Seed(ParseSeed(Context.GetParam(BehaviorScenes::PerturbParam)))
			, Stream(Seed)
		{
		}
		[[nodiscard]] bool IsActive() const { return Seed != 0; }
		// The next body's offset: PerturbationCm along a unit vector uniform on the sphere (z = U(-1, 1), phi = U(0, 2 pi)).
		FVector Next()
		{
			if (Seed == 0)
			{
				return FVector(0.0, 0.0, 0.0);
			}
			const double Z = Stream.FRandRange(-1.0, 1.0);
			const double Phi = Stream.FRandRange(0.0, 2.0 * 3.14159265358979323846);
			const double R = std::sqrt(std::max(0.0, 1.0 - Z * Z));
			return FVector(R * std::cos(Phi), R * std::sin(Phi), Z) * BehaviorScenes::PerturbationCm;
		}

	private:
		static int32 ParseSeed(const std::string& Text)
		{
			int32 Value = 0;
			const std::from_chars_result Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
			return Result.ec == std::errc() ? Value : 0;   // validated as Int32 by ResolveParams
		}
		int32 Seed = 0;
		FRandomStream Stream;
	};

	// A ball or hinge joint (ConstraintUtils presets, UE ConstraintUtils.h:10, :19 [V]) between Child (Body1) and Parent (Body2,
	// nullptr = the world) with frames given by position and primary / secondary axes in each body's actor space.
	inline FConstraintInstance MakeJoint(const char* Name, const bool bHinge, const FVector& Pos1, const FVector& Pri1, const FVector& Sec1, const FVector& Pos2,
		const FVector& Pri2, const FVector& Sec2, const bool bDisableCollision)
	{
		FConstraintInstance Joint;
		Joint.JointName = Name;
		Joint.Pos1 = Pos1;
		Joint.PriAxis1 = Pri1;
		Joint.SecAxis1 = Sec1;
		Joint.Pos2 = Pos2;
		Joint.PriAxis2 = Pri2;
		Joint.SecAxis2 = Sec2;
		if (bHinge)
		{
			ConstraintUtils::ConfigureAsHinge(Joint);
		}
		else
		{
			ConstraintUtils::ConfigureAsBallAndSocket(Joint);
		}
		Joint.ProfileInstance.bDisableCollision = bDisableCollision;
		return Joint;
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
