// PtAbiUeCompatCheck.cpp - OBJECT target compiled with UE-like flags: /W4 /WX /GR- /EHs-c- _HAS_EXCEPTIONS=0
// PT_ABI_NO_EXCEPTIONS, PT_BACKEND_NS=UeCompatCheck (03_WorkPlan M2 criterion 1, 02_Architecture 7.3, A.3).
// Instantiates a trivial backend so that every Kit default body and the whole TBackendApiProvider template are compiled
// on every build, exactly as a UBT module (the Chaos bridge) will compile them.
// The backend classes live in a named namespace: in an anonymous namespace they would be tied to template members
// defined in other TUs and MSVC reports C5046 (seen while compiling the A.3 sketch).
#include "PhysicsTestAbi/PtBackendKit.h"

#if PT_KIT_CATCH_EXCEPTIONS
	#error "PtAbiUeCompatCheck must compile the Kit without exception handling (/EHs-c- or PT_ABI_NO_EXCEPTIONS)"
#endif
#if defined(_CPPRTTI)
	#error "PtAbiUeCompatCheck must compile without RTTI (/GR-), as UE modules do"
#endif

namespace PtUeCompatCheck
{
	using namespace ::PhysicsTest::Abi;

	class FTrivialScene final : public IPhysicsBackendScene
	{
	public:
		PtResult CreateMaterials(TStridedView<PtPhysicalMaterial>) override { return PT_OK; }
		PtResult CreateActors(const FActorBatchView&) override { return PT_OK; }
		PtResult ReleaseActors(const uint32_t*, uint32_t) override { return PT_OK; }
		PtResult SetUpForFrame(const FFrameParams&) override { return PT_OK; }
		PtResult StartFrame() override { return PT_OK; }
		PtResult WaitPhysScenes() override { return PT_OK; }
		PtResult EndFrame(PtFrameResult& Out) override
		{
			Out.NumStepsTaken = 1;
			Out.EngineErrorBits = 0;
			return PT_OK;
		}
		PtResult ApplyBodyCommand(const PtBodyCommand&) override { return PT_OK; }
		PtResult ReadBodyStates(uint32_t, uint32_t*, PtBodyState*, uint32_t, uint32_t& OutCount) override
		{
			OutCount = 0;
			return PT_OK;
		}
		PtResult ReadMassProperties(uint32_t, uint32_t, PtMassProperties*) override { return PT_OK; }
		PtResult GetStepStats(PtStepStats&) override { return PT_OK; }
		PtResult DumpEffectiveSettings(FTextWriter& Out) override
		{
			Out.AppendEffective("world.gravity", "n/a", "", 0.0, "cm/s^2", "COMPILE_TIME");
			return PT_OK;
		}
	};

	class FTrivialBackend final : public IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "UeCompatCheck";

		void GetInfo(PtBackendInfo& Out) const override { Out.Name = ModuleName; }
		PtResult InitGamePhys(const PtInitParams&) override { return PT_OK; }
		PtResult TermGamePhys() override { return PT_OK; }
		PtResult CreateScene(const PtSceneDesc&, IPhysicsBackendScene*& OutScene) override
		{
			OutScene = &Scene;
			return PT_OK;
		}
		void DestroyScene(IPhysicsBackendScene*) override {}

	private:
		FTrivialScene Scene;
	};
}

PT_IMPLEMENT_BACKEND_MODULE_STATIC(PtUeCompatCheck::FTrivialBackend, PT_GetBackendApi_UeCompatCheck)
