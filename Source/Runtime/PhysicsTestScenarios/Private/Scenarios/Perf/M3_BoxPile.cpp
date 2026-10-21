// M3_BoxPile.cpp - M3.BoxPile(N) (docs/Scenes.md, 03_WorkPlan M3), the headline performance scene. N = 1000 / 5000 /
// 10000 cubes of 100 cm in a grid of 10 x 10 / 25 x 20 / 40 x 25 per layer, 10 layers, 150 cm between centres in x, y
// and z, the lowest layer's centres at z = 100 cm (50 cm above the ground), each cube yawed by U(-15, 15) deg and shifted
// by U(-5, 5) cm in x and y (FRandomStream, seed parameter, 0x5EED by default; three draws per cube in layer, row,
// column order: yaw, dx, dy). The ground is the static box of every scene. Material: the profile's default physical
// material (UE: F 0.7, SF 0, R 0.3); mass fixed at 1000 kg (bOverrideMass, the methodology scenes pin masses so that
// engines and profiles agree, 02 10). 600 frames; windows headline 1-599, impact 1-179, settled 300-599 (02 9.5).
// Every cube is one box element with its COM at the centre: no engine may wrap it (shape.wrapping = 0, DL42, G9).
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		struct FBoxPileLayout
		{
			uint32 Columns = 10;   // along X
			uint32 Rows = 10;      // along Y
			uint32 Layers = 10;
			uint32 Count() const { return Columns * Rows * Layers; }
		};

		FBoxPileLayout GetLayout(const uint32 N)
		{
			switch (N)
			{
			case 5000: return FBoxPileLayout{ 25, 20, 10 };
			case 10000: return FBoxPileLayout{ 40, 25, 10 };
			default: return FBoxPileLayout{ 10, 10, 10 };
			}
		}

		class FM3BoxPile final : public IPhysicsTestScenario
		{
		public:
			static constexpr double Edge = 100.0;          // cm
			static constexpr double Spacing = 150.0;       // cm between centres
			static constexpr double FirstLayerZ = 100.0;   // cm, centre of the lowest layer
			static constexpr double MaxYawDegrees = 15.0;
			static constexpr double MaxShift = 5.0;        // cm
			static constexpr float MassKg = 1000.0f;
			static constexpr int32 Frames = 600;

			const char* GetName() const override { return "M3.BoxPile"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Perf; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "N 100 cm cubes (1000 kg) in 10 layers, 150 cm spacing, yaw U(+-15 deg), xy jitter U(+-5 cm), onto the ground box, 600 frames";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return {
					FScenarioParamDesc{ "N", "1000", { "1000", "5000", "10000" }, "number of cubes (10 x 10, 25 x 20, 40 x 25 per layer, 10 layers)" },
					FScenarioParamDesc{ "Seed", "24301", {}, "FRandomStream seed (decimal int32; 24301 = 0x5EED)", EScenarioParamType::Int32 },
				};
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, Frames - 1 }, FStepWindow{ "impact", 1, 179 }, FStepWindow{ "settled", 300, Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& Params) const override
			{
				// Bodies + ground; broad-phase pairs and contact manifolds with margin for a collapsed pile (each cube
				// touches at most a handful of neighbours). Overflow is the run's G12 failure (02 9.4).
				const auto Found = Params.find("N");
				const uint32 N = GetLayout(ScenarioHelpers::ParseUnsignedParam(Found != Params.end() ? Found->second : "1000", 1000)).Count();
				return FScenarioCapacity{ N + 1, 16u * N + 1024u, 8u * N + 1024u };
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				const FBoxPileLayout Layout = GetLayout(ScenarioHelpers::ParseUnsignedParam(Context.GetParam("N"), 1000));
				const FPhysicalMaterial* Material = Scene.GetDefaultPhysicalMaterial();
				ScenarioHelpers::SpawnGround(Scene, Context, Material);
				const FBodySetup* CubeSetup = ScenarioHelpers::MakeCubeSetup(Scene, "Cube100", Edge, Material);

				FBodyInstance Template = Context.MakeBodyTemplate();
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = MassKg;
				const double OriginX = -0.5 * Spacing * static_cast<double>(Layout.Columns - 1);
				const double OriginY = -0.5 * Spacing * static_cast<double>(Layout.Rows - 1);
				uint32 Index = 0;
				for (uint32 Layer = 0; Layer < Layout.Layers; ++Layer)
				{
					for (uint32 Row = 0; Row < Layout.Rows; ++Row)
					{
						for (uint32 Column = 0; Column < Layout.Columns; ++Column)
						{
							const double Yaw = Context.Random.FRandRange(-MaxYawDegrees, MaxYawDegrees);
							const double ShiftX = Context.Random.FRandRange(-MaxShift, MaxShift);
							const double ShiftY = Context.Random.FRandRange(-MaxShift, MaxShift);
							const FVector Location(OriginX + Spacing * static_cast<double>(Column) + ShiftX, OriginY + Spacing * static_cast<double>(Row) + ShiftY,
								FirstLayerZ + Spacing * static_cast<double>(Layer));
							Template.BodyName = "Box" + std::to_string(Index++);
							Scene.SpawnBody(Template, CubeSetup, FTransform(FRotator(0.0, Yaw, 0.0), Location));
						}
					}
				}
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM3BoxPile, M3BoxPile)
