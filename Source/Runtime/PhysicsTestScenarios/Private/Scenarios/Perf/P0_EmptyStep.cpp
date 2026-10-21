// P0_EmptyStep.cpp - P0.EmptyStep (docs/Scenes.md 2.13, 03_WorkPlan M7), a harness catalog extension (DL34): the ground box
// alone (body index 0, static), 600 frames. What a frame costs when there is nothing to simulate - the ABI's four frame
// calls, the engine's fixed per-step work and the empty state pull - i.e. the intercept a of the overhead model
// t = a + b * N that M8 fits over P0, P1.SmallScene and M3.BoxPile (02 13). Windows: headline 1-599.
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		class FP0EmptyStep final : public IPhysicsTestScenario
		{
		public:
			static constexpr int32 Frames = 600;

			const char* GetName() const override { return "P0.EmptyStep"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Perf; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "the ground box alone (no dynamic body): the per-frame cost of an empty step, 600 frames";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, Frames - 1 } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override
			{
				return ScenarioHelpers::MakePileCapacity(0);
			}

			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				ScenarioHelpers::SpawnGround(Scene, Context, Scene.GetDefaultPhysicalMaterial());
				Scene.FlushDeferredActors();
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FP0EmptyStep, P0EmptyStep)
