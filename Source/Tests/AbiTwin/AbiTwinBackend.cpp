// AbiTwinBackend.cpp - a second static test backend (PT_BACKEND_NS=AbiTwin), linked into PhysicsTestAutomation next to
// the Null backend (03_WorkPlan M2 criterion: two static backends in one exe keep separate Kit state, and the linker map
// shows the Kit symbols split by backend inline namespace). It also misbehaves on request so the Kit's guards can be
// tested: module options
//   twin.fail=<text>        returns PT_E_ENGINE with <text> as the last error
//   twin.clobber_mxcsr=1    sets FTZ + DAZ inside the call (the Kit restores MXCSR and warns once)
//   twin.throw=1            throws a C++ exception (the Kit returns PT_E_INTERNAL)
// Its debug snapshot (M5 round 0) misbehaves too: besides one particle per body it creates, it reports particles with the
// BodyIndex values PT_INDEX_NONE, the body count (one past the last body) and 0x7FFFFFFE, so the host capture's rejection
// (FPhysicsBackendScene::CaptureDebugSnapshot: drop, count, log once) is a unit test.
#include "PhysicsTestAbi/PtBackendKit.h"

#include <string.h>

#include <stdexcept>
#include <xmmintrin.h>

namespace PtAbiTwin
{
	using namespace ::PhysicsTest::Abi;

	class FTwinScene final : public IPhysicsBackendScene
	{
	public:
		PtResult CreateMaterials(TStridedView<PtPhysicalMaterial>) override { return PT_OK; }
		PtResult CreateActors(const FActorBatchView& Batch) override
		{
			NumBodies += Batch.Actors.Num();
			return PT_OK;
		}
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
		PtResult CaptureDebugSnapshot(const PtDebugSnapshotRequest& Req, PtDebugSnapshotCounts& Out) override
		{
			NumStaged = 0;
			if ((Req.Flags & PT_SNAP_PARTICLES) != 0)
			{
				for (uint32_t Body = 0; Body < NumBodies && NumStaged < MaxValidParticles; ++Body)
				{
					Stage(Body);
				}
				Stage(PT_INDEX_NONE);
				Stage(NumBodies);
				Stage(0x7FFFFFFEu);
			}
			Out.NumParticles = NumStaged;
			Out.NumContactPoints = 0;
			Out.NumJoints = 0;
			Out.NumNativeCommands = 0;
			Out.NumStringBytes = 0;
			Out.bTruncated = 0;
			return PT_OK;
		}
		PtResult ReadDebugSnapshot(const PtDebugSnapshotBuffers& Buffers) override
		{
			if (Buffers.ParticleCapacity < NumStaged || (NumStaged > 0 && (Buffers.Particles == nullptr || Buffers.ParticleStride < sizeof(PtParticleSnapshot))))
			{
				return GetKit().SetLastError(PT_E_CAPACITY, "AbiTwin: ReadDebugSnapshot needs %u particles", NumStaged);
			}
			for (uint32_t Index = 0; Index < NumStaged; ++Index)
			{
				memcpy(reinterpret_cast<unsigned char*>(Buffers.Particles) + static_cast<size_t>(Index) * Buffers.ParticleStride, &Staged[Index], sizeof(PtParticleSnapshot));
			}
			return PT_OK;
		}

	private:
		static constexpr uint32_t MaxValidParticles = 8;
		void Stage(const uint32_t BodyIndex)
		{
			PtParticleSnapshot Particle = {};
			Particle.ActorTransform.Rotation.W = 1.0;
			Particle.CenterOfMassWorld.Rotation.W = 1.0;
			Particle.BodyIndex = BodyIndex;
			Particle.ObjectState = static_cast<uint8_t>(PT_OBJECT_STATE_STATIC);
			Particle.NativeIslandId = -1;
			Staged[NumStaged++] = Particle;
		}

		uint32_t NumBodies = 0;
		uint32_t NumStaged = 0;
		PtParticleSnapshot Staged[MaxValidParticles + 3] = {};
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
