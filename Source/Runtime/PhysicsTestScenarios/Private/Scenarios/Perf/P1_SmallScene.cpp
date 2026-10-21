// P1_SmallScene.cpp - P1.SmallScene(N) (docs/Scenes.md 2.14, 03_WorkPlan M7), a harness catalog extension (DL34): the
// M3.BoxPile layout rule at small N - N = 10 is one column of 10 cubes (1 x 1 per layer), N = 100 is 5 x 2 per layer, 10
// layers each (ScenarioHelpers::SpawnBoxPile: 100 cm cubes of 1000 kg, 150 cm spacing, yaw U(+-15 deg), xy jitter U(+-5 cm),
// seed 0x5EED). Every cube does what an M3.BoxPile cube does, so P0, P1 and M3 span the overhead model t = a + b * N
// (02 13) with the same per-body work. 600 frames; windows headline 1-599, impact 1-179, settled 300-599 (as M3.BoxPile).
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		ScenarioHelpers::FBoxPileLayout GetSmallLayout(const uint32 N)
		{
			return N == 100 ? ScenarioHelpers::FBoxPileLayout{ 5, 2, 10 } : ScenarioHelpers::FBoxPileLayout{ 1, 1, 10 };
		}

		class FP1SmallScene final : public IPhysicsTestScenario
		{
		public:
			static constexpr int32 Frames = 600;

			const char* GetName() const override { return "P1.SmallScene"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Perf; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "M3.BoxPile at small N: 10 (one column) or 100 (5 x 2) cubes in 10 layers, 600 frames";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override
			{
				return {
					FScenarioParamDesc{ "N", "10", { "10", "100" }, "number of cubes (1 x 1 or 5 x 2 per layer, 10 layers)" },
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
				const auto Found = Params.find("N");
				return ScenarioHelpers::MakePileCapacity(GetSmallLayout(ScenarioHelpers::ParseUnsignedParam(Found != Params.end() ? Found->second : "10", 10)).Count());
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				ScenarioHelpers::SpawnBoxPile(Scene, Context, GetSmallLayout(ScenarioHelpers::ParseUnsignedParam(Context.GetParam("N"), 10)));
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FP1SmallScene, P1SmallScene)
