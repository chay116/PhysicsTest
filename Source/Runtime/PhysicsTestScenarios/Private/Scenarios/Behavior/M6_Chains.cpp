// M6_Chains.cpp - M6b.Chain and M6c.HeavyChain (docs/Scenes.md 2.20, 2.21, 03_WorkPlan M9). Capsule links (radius 5 cm,
// segment 30 cm, 40 cm tip to tip, 1 kg) laid along +X from a world pivot at z = 1000 cm, joined tip to tip by ball joints
// (ConstraintUtils::ConfigureAsBallAndSocket, bDisableCollision: adjacent links touch at the tips), released horizontal from
// rest. M6b: 20 links. M6c: 10 links ending in a 100 kg sphere (100 : 1). Chaotic (statistics, the Perturb replicas); the
// analysis reports the chain stretch (joint frame separation from states.ptst and scene_desc.json) and the energy (03 M9: it
// must not diverge). Needs PT_CAP_JOINT_BALL.
#include "BehaviorScenes.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "ScenarioHelpers.h"

namespace PhysicsTest
{
	namespace
	{
		// Spawns the links (and M6c's end sphere), joints them and flushes. Every link is rotated by Pitch 90, so its local +Z
		// (the capsule axis) points at -X, toward its parent: Frame1 at local (0, 0, +HalfPitch), Frame2 on the parent at
		// (0, 0, -HalfPitch). The frames of a link and its parent coincide at rest (same rotation); the world pivot's frame
		// takes the first link's rotation.
		void BuildChain(FPhysScene& Scene, const FScenarioContext& Context, const int32 NumLinks, const bool bHeavyEnd)
		{
			using namespace BehaviorScenes::Chain;
			FBodySetup LinkGeometry;
			LinkGeometry.BodySetupName = "ChainLink";
			LinkGeometry.AggGeom.SphylElems.emplace_back(LinkRadius, LinkSegment);
			LinkGeometry.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
			const FBodySetup* LinkSetup = Scene.CreateBodySetup(std::move(LinkGeometry));
			FBodyInstance Template = Context.MakeBodyTemplate();
			Template.bSimulatePhysics = true;
			Template.bOverrideMass = true;
			Template.MassInKgOverride = LinkMassKg;
			const FRotator LinkRotation(90.0, 0.0, 0.0);
			const FQuat LinkQuat = LinkRotation.Quaternion();
			ScenarioHelpers::FPerturbation Perturbation(Context);
			std::vector<FBodyInstance*> Links;
			for (int32 Index = 0; Index < NumLinks; ++Index)
			{
				Template.BodyName = "Link" + std::to_string(Index);
				const FVector Centre(LinkHalfPitch + 2.0 * LinkHalfPitch * Index, 0.0, PivotZ);
				Links.push_back(Scene.SpawnBody(Template, LinkSetup, FTransform(LinkRotation, Centre + Perturbation.Next())));
			}
			FBodyInstance* End = nullptr;
			if (bHeavyEnd)
			{
				using namespace BehaviorScenes::M6c;
				FBodySetup EndGeometry;
				EndGeometry.BodySetupName = "ChainEnd";
				EndGeometry.AggGeom.SphereElems.emplace_back(EndRadius);
				EndGeometry.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
				const FBodySetup* EndSetup = Scene.CreateBodySetup(std::move(EndGeometry));
				FBodyInstance EndTemplate = Template;
				EndTemplate.BodyName = "HeavyEnd";
				EndTemplate.MassInKgOverride = EndMassKg;
				const double LastTipX = 2.0 * LinkHalfPitch * NumLinks;
				End = Scene.SpawnBody(EndTemplate, EndSetup, FTransform(FVector(LastTipX + EndRadius, 0.0, PivotZ) + Perturbation.Next()));
			}
			// Frame axes: identity in each link's space (X = link X); the world pivot's frame uses the first link's world axes.
			const FVector X(1.0, 0.0, 0.0);
			const FVector Y(0.0, 1.0, 0.0);
			const FVector ToParent(0.0, 0.0, LinkHalfPitch);
			const FVector ToChild(0.0, 0.0, -LinkHalfPitch);
			for (int32 Index = 0; Index < NumLinks; ++Index)
			{
				const std::string Name = "ChainJoint" + std::to_string(Index);
				const FConstraintInstance Joint = Index == 0
					? ScenarioHelpers::MakeJoint(Name.c_str(), false, ToParent, X, Y, FVector(0.0, 0.0, PivotZ), LinkQuat.RotateVector(X), LinkQuat.RotateVector(Y), true)
					: ScenarioHelpers::MakeJoint(Name.c_str(), false, ToParent, X, Y, ToChild, X, Y, true);
				Scene.SpawnConstraint(Joint, Links[static_cast<size_t>(Index)], Index == 0 ? nullptr : Links[static_cast<size_t>(Index - 1)]);
			}
			if (End != nullptr)
			{
				// The sphere (identity rotation) hangs on the last link's far tip: its Frame1 at local (-EndRadius, 0, 0), axes in
				// the link's world orientation so the frames coincide at rest.
				const FVector EndToParent(-BehaviorScenes::M6c::EndRadius, 0.0, 0.0);
				const FConstraintInstance Joint = ScenarioHelpers::MakeJoint("ChainEndJoint", false, EndToParent, LinkQuat.RotateVector(X), LinkQuat.RotateVector(Y),
					ToChild, X, Y, true);
				Scene.SpawnConstraint(Joint, End, Links.back());
			}
			Scene.FlushDeferredActors();
		}

		std::vector<FStepWindow> MakeChainWindows()
		{
			using namespace BehaviorScenes::Chain;
			// "swing1" = the first second (the fall to the lowest point and back: the peak tension), "late" = 5-10 s.
			return { FStepWindow{ "headline", 1, Frames - 1 }, FStepWindow{ "swing1", 1, 60 }, FStepWindow{ "late", 300, Frames - 1 } };
		}

		class FM6bChain final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M6b::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "20 capsule links (r 5, 40 cm tip to tip, 1 kg) on ball joints from a world pivot, released horizontal; 600 frames; Perturb replicas";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override { return { ScenarioHelpers::MakePerturbParamDesc() }; }
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::Chain::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override { return MakeChainWindows(); }
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override
			{
				return ScenarioHelpers::MakePileCapacity(static_cast<uint32>(BehaviorScenes::M6b::NumLinks));
			}
			uint64 GetRequiredCapabilities(const FScenarioParams& /*Resolved*/) const override { return PT_CAP_JOINT_BALL; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override { BuildChain(Scene, Context, BehaviorScenes::M6b::NumLinks, false); }
		};

		class FM6cHeavyChain final : public IPhysicsTestScenario
		{
		public:
			const char* GetName() const override { return BehaviorScenes::M6c::Name; }
			int32 GetVersion() const override { return 1; }
			EScenarioKind GetKind() const override { return EScenarioKind::Behavior; }
			bool IsChaotic() const override { return true; }
			const char* GetDescription() const override
			{
				return "10 capsule links (1 kg) on ball joints from a world pivot ending in a 100 kg sphere (100:1), released horizontal; 600 frames; Perturb replicas";
			}
			std::vector<FScenarioParamDesc> GetParamDescs() const override { return { ScenarioHelpers::MakePerturbParamDesc() }; }
			int32 GetDefaultFrames(const FScenarioParams& /*Params*/) const override { return BehaviorScenes::Chain::Frames; }
			std::vector<FStepWindow> GetWindows(const FScenarioParams& /*Params*/) const override { return MakeChainWindows(); }
			FScenarioCapacity GetCapacity(const FScenarioParams& /*Params*/) const override
			{
				return ScenarioHelpers::MakePileCapacity(static_cast<uint32>(BehaviorScenes::M6c::NumLinks + 1));
			}
			uint64 GetRequiredCapabilities(const FScenarioParams& /*Resolved*/) const override { return PT_CAP_JOINT_BALL; }
			void BuildScene(FPhysScene& Scene, const FScenarioContext& Context) override { BuildChain(Scene, Context, BehaviorScenes::M6c::NumLinks, true); }
		};
	}
}

IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM6bChain, M6bChain)
IMPLEMENT_PHYSICS_TEST_SCENARIO(PhysicsTest::FM6cHeavyChain, M6cHeavyChain)
