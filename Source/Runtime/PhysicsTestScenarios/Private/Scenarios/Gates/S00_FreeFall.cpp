// S00_FreeFall.cpp - S00.FreeFall (docs/Scenes.md; G1 in M6). The M2 free-fall fixture as a scenario: the methodology
// catalog's SI S00 (5.1) redefined in cm with g = 980: a 1 kg sphere of radius 50 cm released at z = 1000 cm with no
// damping falls for 60 frames of FixedStep 1/60. Reference (DL23, dt_engine = (double)(float)(1/60)):
// z60 = 501.8332813705 cm, v60 = -980.0000511 cm/s. 03 M3 criterion: |z60 - 501.83328| <= 2e-3 cm and
// |v60 + 980.00005| <= 1e-2 cm/s through the ABI; Null matches within 1e-9.
// No ground: the sphere never reaches z = 50 cm within the 60 frames, and the scene stays exactly the M2 fixture.
#include "Fixtures/FreeFallFixture.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"

namespace PhysicsTest
{
	namespace
	{
		class FS00FreeFall final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return "S00.FreeFall"; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Gate; }
			bool IsChaotic() const override { return false; }
			const char* GetDescription() const override
			{
				return "1 kg sphere r 50 cm from z 1000 cm, damping 0, gravity -980 cm/s^2, 60 frames of 1/60 s (M2 fixture; G1)";
			}
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return Fixtures::FFreeFall::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override
			{
				return { FStepWindow{ "headline", 1, static_cast<uint32>(Fixtures::FFreeFall::Frames - 1) } };
			}
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override { return FScenarioCapacity{ 1, 64, 64 }; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override
			{
				// The profile's body defaults first, then the fixture pins its own values (damping 0, 1 kg, gravity on).
				const FBodyInstance Base = Context.MakeBodyTemplate();
				Fixtures::FFreeFall::Build(Scene, &Base);
			}
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FS00FreeFall, S00FreeFall)
