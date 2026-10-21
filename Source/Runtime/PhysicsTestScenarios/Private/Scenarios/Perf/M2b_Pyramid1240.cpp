// M2b_Pyramid1240.cpp - M2b.Pyramid1240 (docs/Scenes.md 2.15, methodology catalog M2b, 03_WorkPlan M7): Jolt's
// PerformanceTest PyramidScene (JoltPhysics/PerformanceTest/PyramidScene.h:24-43 [V], MIT) re-authored in cm and Z-up
// through the harness data model - one very large island. 15 layers of 200 cm cubes; layer i (0 = bottom) holds
// (15 - i)^2 cubes, 1240 in total; horizontal pitch 200 cm (neighbours touch face to face), odd layers offset by 100 cm in
// x and y, layer centres at z = 100 + 250 i cm (a 50 cm gap between layers, the bottom layer resting on the ground).
// Jolt's (x, y up, z) maps to the harness (x, z, y up) x 100: cube (i, j, k) of the Jolt loop sits at
// x = 100 (-15 + 2 j + (i odd ? 1 : 0)), y = 100 (-15 + 2 k + (i odd ? 1 : 0)), z = 100 (1 + 2.5 i), for j and k in
// [i / 2, 15 - (i + 1) / 2). Mass 8000 kg (Jolt's default density 1000 kg/m^3 x 8 m^3), pinned like every methodology
// scene (02 10). Differences from the Jolt scene, by design: the material is the profile's default (M3.BoxPile's rule),
// sleeping follows the profile (Normalized: off, as Jolt's mAllowSleeping = false) and the box convex radius is the
// variant's (Jolt's 0 is the M9 variant Jolt.SharpBoxes). 500 frames (the methodology's 500 steps); windows headline
// 1-499, impact 1-119, settled 300-499.
#include "Misc/AssertionMacros.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FM2bPyramid1240 final : public IPhysicsTestScenario
		{
		public:
			static constexpr int32 Frames = 500;
			static constexpr int32 Height = 15;            // layers (cPyramidHeight)
			static constexpr double Edge = 200.0;          // cm (cBoxSize 2 m)
			static constexpr double LayerPitch = 250.0;    // cm (cBoxSize + cBoxSeparation)
			static constexpr double FirstLayerZ = 100.0;   // cm (1 m)
			static constexpr float MassKg = 8000.0f;
			static constexpr uint32 NumBoxes = 1240;

			const char* GetName() const override { return "M2b.Pyramid1240"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Perf; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "Jolt PerformanceTest Pyramid in cm: 15 layers of 200 cm cubes (8000 kg), (15 - i)^2 per layer = 1240, one big island, 500 frames";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, Frames - 1 }, FStepWindow{ "impact", 1, 119 }, FStepWindow{ "settled", 300, Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override
			{
				return ScenarioHelpers::MakePileCapacity(NumBoxes);
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				const FPhysicalMaterial* Material = Scene.GetDefaultPhysicalMaterial();
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube200", Edge, Material);
				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = MassKg;
				uint32 Index = 0;
				for (int32 Layer = 0; Layer < Height; ++Layer)
				{
					const double Offset = (Layer & 1) != 0 ? 0.5 * Edge : 0.0;
					for (int32 J = Layer / 2; J < Height - (Layer + 1) / 2; ++J)
					{
						for (int32 K = Layer / 2; K < Height - (Layer + 1) / 2; ++K)
						{
							const FVector Location(-100.0 * Height + Edge * J + Offset, -100.0 * Height + Edge * K + Offset, FirstLayerZ + LayerPitch * Layer);
							Template.BodyName = "Box" + std::to_string(Index++);
							Scene.SpawnBody(Template, CubeSetup, FTransform(Location));
						}
					}
				}
				checkf(Index == NumBoxes, "M2b.Pyramid1240 spawned %u cubes, expected %u", Index, NumBoxes);
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM2bPyramid1240, M2bPyramid1240)
