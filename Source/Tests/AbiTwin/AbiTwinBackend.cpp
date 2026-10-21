// AbiTwinBackend.cpp - a second static test backend (PT_BACKEND_NS=AbiTwin), linked into PhysicsTestAutomation next to
// the Null backend (03_WorkPlan M2 criterion: two static backends in one exe keep separate Kit state, and the linker map
// shows the Kit symbols split by backend inline namespace). It also misbehaves on request so the Kit's guards can be
// tested: module options
//   twin.fail=<text>        returns PT_E_ENGINE with <text> as the last error
//   twin.clobber_mxcsr=1    sets FTZ + DAZ inside the call (the Kit restores MXCSR and warns once)
//   twin.throw=1            throws a C++ exception (the Kit returns PT_E_INTERNAL)
#include "PhysicsTestAbi/PtBackendKit.h"

#include <stdexcept>
#include <xmmintrin.h>

namespace PtAbiTwin
{
	using namespace ::PhysicsTest::Abi;

	class FTwinScene final : public IPhysicsBackendScene
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
			Out.AppendEffective("twin.scene", "1", "", 1.0, "", "COMPILE_TIME");
			return PT_OK;
		}
	};

	class FTwinBackend final : public IPhysicsBackend
	{
	public:
		static constexpr const char* ModuleName = "AbiTwin";

		void GetInfo(PtBackendInfo& Out) const override
		{
			Out.Name = ModuleName;
			Out.DisplayName = "AbiTwin (test backend)";
			Out.Capabilities = 0;
		}
		PtResult InitGamePhys(const PtInitParams&) override { return PT_OK; }
		PtResult TermGamePhys() override { return PT_OK; }
		PtResult CreateScene(const PtSceneDesc&, IPhysicsBackendScene*& OutScene) override
		{
			OutScene = new FTwinScene();
			return PT_OK;
		}
		void DestroyScene(IPhysicsBackendScene* Scene) override { delete Scene; }
		PtResult SetModuleOption(const FStringRef Key, const FStringRef Value) override
		{
			if (Key.Equals("twin.fail"))
			{
				return GetKit().SetLastError(PT_E_ENGINE, "%.*s", static_cast<int>(Value.Length), Value.Data);
			}
			if (Key.Equals("twin.clobber_mxcsr"))
			{
				_mm_setcsr(_mm_getcsr() | 0x8040u);
				return PT_OK;
			}
			if (Key.Equals("twin.throw"))
			{
				throw std::runtime_error("AbiTwin: requested exception");
			}
			return GetKit().SetLastError(PT_E_INVALID_ARG, "AbiTwin: unknown module option '%.*s'", static_cast<int>(Key.Length), Key.Data);
		}
	};
}

PT_IMPLEMENT_BACKEND_MODULE_STATIC(PtAbiTwin::FTwinBackend, PT_GetBackendApi_AbiTwin)
