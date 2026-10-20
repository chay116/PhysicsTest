// PtJoltSeam.cpp - the library side of the flag-leak probe: links PT::Jolt PRIVATE, exactly like a backend library
// (02_Architecture 4.5: PT::Backend::Jolt -> PRIVATE PT::Jolt). Ported from prototypes/hello_jolt/seam.
#include "PtJoltSeam.h"

#include "PtBuildReport.h"
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

namespace
{
	class FPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer A, const JPH::ObjectLayer B) const override { return A == 1 || B == 1; }
	};

	class FBroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
	{
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer Layer) const override
		{
			return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(Layer));
		}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer) const override { return "?"; }
#endif
	};

	class FObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		bool ShouldCollide(const JPH::ObjectLayer Layer, const JPH::BroadPhaseLayer BroadPhase) const override
		{
			return Layer == 1 || BroadPhase.GetValue() == 1;
		}
	};

	PT_BUILD_REPORT_JSON(GSeamBuildReport, "\"tu\":\"PtJoltSeam.cpp\",");
}

FPtJoltSeamResult PtJoltSeamDropBox(const int NumSteps)
{
	JPH::RegisterDefaultAllocator();
	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	FPtJoltSeamResult Result;
	{
		JPH::TempAllocatorImpl TempAllocator(4 * 1024 * 1024);
		JPH::JobSystemSingleThreaded JobSystem(JPH::cMaxPhysicsJobs);
		FBroadPhaseLayers BroadPhaseLayers;
		FObjectVsBroadPhase ObjectVsBroadPhase;
		FPairFilter PairFilter;
		JPH::PhysicsSystem PhysicsSystem;
		PhysicsSystem.Init(16, 0, 16, 16, BroadPhaseLayers, ObjectVsBroadPhase, PairFilter);
		PhysicsSystem.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.80f));
		JPH::BodyInterface& Bodies = PhysicsSystem.GetBodyInterface();
		JPH::BodyCreationSettings Settings(new JPH::BoxShape(JPH::Vec3::sReplicate(0.5f)),
			JPH::RVec3(JPH::Real(0), JPH::Real(0), JPH::Real(10)), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, 1);
		Settings.mLinearDamping = 0.0f;
		Settings.mAllowSleeping = false;
		const JPH::BodyID Box = Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate);
		for (int Step = 0; Step < NumSteps; ++Step)
		{
			PhysicsSystem.Update(1.0f / 60.0f, 1, &TempAllocator, &JobSystem);
		}
		Result.FinalZ = static_cast<double>(Bodies.GetCenterOfMassPosition(Box).GetZ());
		Bodies.RemoveBody(Box);
		Bodies.DestroyBody(Box);
	}
	Result.JoltConfiguration = JPH::GetConfigurationString();
	Result.BuildReportJson = GSeamBuildReport;

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;
	return Result;
}
