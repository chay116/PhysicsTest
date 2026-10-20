// JoltSmoke.cpp - Jolt 5.6.0 smoke test (03_WorkPlan M0 criteria 6 and 7). Ported from prototypes/hello_jolt,
// changed to Z-up, g = 9.80, no damping, no sleeping. This TU links PT::Jolt, so it sees Jolt's PUBLIC /arch:AVX2 and
// JPH_* defines: JPH::GetConfigurationString() is inline and reports the configuration of THIS TU (02 4.6).
#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <Jolt/Jolt.h>
#include <Jolt/ConfigurationString.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
PT_THIRD_PARTY_INCLUDES_END

#include "SmokeCommon.h"

#include <cstdarg>
#include <string>

namespace
{
	using namespace PhysicsTest::SdkSmoke;

	namespace Layers
	{
		constexpr JPH::ObjectLayer NonMoving = 0;
		constexpr JPH::ObjectLayer Moving = 1;
		constexpr JPH::uint NumLayers = 2;
	}

	class FObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer Object1, const JPH::ObjectLayer Object2) const override
		{
			return Object1 == Layers::Moving || Object2 == Layers::Moving;
		}
	};

	class FBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return Layers::NumLayers; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer Layer) const override
		{
			return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(Layer));
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer Layer) const override
		{
			return Layer.GetValue() == Layers::NonMoving ? "NON_MOVING" : "MOVING";
		}
#endif
	};

	class FObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer Layer1, const JPH::BroadPhaseLayer Layer2) const override
		{
			return Layer1 == Layers::Moving || Layer2.GetValue() == Layers::Moving;
		}
	};

	void TraceImpl(const char* Format, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Format);
		std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
		va_end(Args);
		std::printf("[Jolt] %s\n", Buffer);
	}

#ifdef JPH_ENABLE_ASSERTS
	bool AssertFailedImpl(const char* Expression, const char* Message, const char* File, const JPH::uint Line)
	{
		std::printf("[Jolt assert] %s:%u: (%s) %s\n", File, Line, Expression, Message != nullptr ? Message : "");
		return true;   // break
	}
#endif

#if defined(JPH_PROFILE_ENABLED)
	#define PT_SMOKE_JPH_PROFILE "\"JPH_PROFILE_ENABLED\":1,"
#else
	#define PT_SMOKE_JPH_PROFILE "\"JPH_PROFILE_ENABLED\":0,"
#endif
#if defined(JPH_DEBUG_RENDERER)
	#define PT_SMOKE_JPH_DEBUG_RENDERER "\"JPH_DEBUG_RENDERER\":1,"
#else
	#define PT_SMOKE_JPH_DEBUG_RENDERER "\"JPH_DEBUG_RENDERER\":0,"
#endif
#if defined(JPH_FLOATING_POINT_EXCEPTIONS_ENABLED)
	#define PT_SMOKE_JPH_FPE "\"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED\":1,"
#else
	#define PT_SMOKE_JPH_FPE "\"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED\":0,"
#endif
#if defined(JPH_USE_AVX2)
	#define PT_SMOKE_JPH_AVX2 "\"JPH_USE_AVX2\":1,"
#else
	#define PT_SMOKE_JPH_AVX2 "\"JPH_USE_AVX2\":0,"
#endif

	PT_BUILD_REPORT_JSON(GBuildReport, PT_SMOKE_JPH_PROFILE PT_SMOKE_JPH_DEBUG_RENDERER PT_SMOKE_JPH_FPE PT_SMOKE_JPH_AVX2);
}

int main()
{
	PrepareUnattended();

	JPH::RegisterDefaultAllocator();
	JPH::Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	// Criterion 7: the configuration seen inside a Jolt-linking TU has AVX2 and no profiler / debug renderer / FP traps.
	const std::string Configuration = JPH::GetConfigurationString();
	const bool bConfigurationOk = Configuration.find("AVX2") != std::string::npos
		&& Configuration.find("(Profile)") == std::string::npos
		&& Configuration.find("(Debug Renderer)") == std::string::npos
		&& Configuration.find("(FP Exceptions)") == std::string::npos;
	std::printf("[PtSmoke] Jolt %d.%d.%d | %s\n", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, Configuration.c_str());

	FSmokeResult Result;
	Result.Engine = "Jolt";
	{
		JPH::TempAllocatorImpl TempAllocator(10 * 1024 * 1024);
		JPH::JobSystemSingleThreaded JobSystem(JPH::cMaxPhysicsJobs);
		FBroadPhaseLayerInterface BroadPhaseLayers;
		FObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseFilter;
		FObjectLayerPairFilter ObjectPairFilter;

		JPH::PhysicsSystem PhysicsSystem;
		PhysicsSystem.Init(64, 0, 64, 64, BroadPhaseLayers, ObjectVsBroadPhaseFilter, ObjectPairFilter);
		PhysicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, static_cast<float>(GravityZ)));
		JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();

		// Ground: 100 x 100 x 2 m static box, top face at z = 0 (same as BulletSmoke.cpp / PhysXSmoke.cpp).
		const JPH::BodyCreationSettings GroundSettings(new JPH::BoxShape(JPH::Vec3(50.0f, 50.0f, 1.0f)),
			JPH::RVec3(JPH::Real(0), JPH::Real(0), JPH::Real(-1)), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NonMoving);
		const JPH::BodyID GroundId = Bodies.CreateAndAddBody(GroundSettings, JPH::EActivation::DontActivate);

		// 1 m cube at z0 = 10 m: no damping (Jolt default 0.05), no sleeping.
		JPH::BodyCreationSettings BoxSettings(new JPH::BoxShape(JPH::Vec3::sReplicate(static_cast<float>(CubeHalfExtent))),
			JPH::RVec3(JPH::Real(0), JPH::Real(0), JPH::Real(StartZ)), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::Moving);
		BoxSettings.mLinearDamping = 0.0f;
		BoxSettings.mAngularDamping = 0.0f;
		BoxSettings.mAllowSleeping = false;
		const JPH::BodyID BoxId = Bodies.CreateAndAddBody(BoxSettings, JPH::EActivation::Activate);
		PhysicsSystem.OptimizeBroadPhase();

		FStepTimer Timer;
		for (int Step = 0; Step < NumSteps; ++Step)
		{
			Timer.Begin();
			const JPH::EPhysicsUpdateError Error = PhysicsSystem.Update(DeltaTime, 1, &TempAllocator, &JobSystem);
			Timer.End();
			if (Error != JPH::EPhysicsUpdateError::None)
			{
				std::printf("[PtSmoke] Jolt: update error 0x%x at step %d\n", static_cast<unsigned>(Error), Step + 1);
				Result.bEngineChecksPassed = false;
			}
		}
		Result.FinalZ = static_cast<double>(Bodies.GetCenterOfMassPosition(BoxId).GetZ());
		Result.FinalVz = static_cast<double>(Bodies.GetLinearVelocity(BoxId).GetZ());
		Result.MedianStepMicroseconds = Timer.MedianMicroseconds();

		Bodies.RemoveBody(BoxId);
		Bodies.DestroyBody(BoxId);
		Bodies.RemoveBody(GroundId);
		Bodies.DestroyBody(GroundId);
	}
	Result.bEngineChecksPassed = Result.bEngineChecksPassed && bConfigurationOk;
	Result.EngineCheckNote = "configuration string has AVX2 and no (Profile) / (Debug Renderer) / (FP Exceptions)";

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;

	const std::string SdkJson = std::string("{\"version\":\"") + std::to_string(JPH_VERSION_MAJOR) + "." + std::to_string(JPH_VERSION_MINOR)
		+ "." + std::to_string(JPH_VERSION_PATCH) + "\",\"configuration\":\"" + Configuration + "\"}";
	return Report(Result, GBuildReport, SdkJson.c_str());
}
