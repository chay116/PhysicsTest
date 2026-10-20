// PhysXSmoke.cpp - PhysX 5.11 smoke test (03_WorkPlan M0 criterion 6). Ported from prototypes/hello_physx,
// changed to Z-up, g = 9.80, no damping, sleeping disabled (PxSceneFlag::eDISABLE_SLEEPING, PxSceneDesc.h:359) and an
// inline CPU dispatcher (PxDefaultCpuDispatcherCreate(0): tasks run on the calling thread). Native SI tolerances
// (PxTolerancesScale defaults: length 1 m, speed 10 m/s) - this is the SdkDefault flavour, not UE4Compat.
#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <PxPhysicsAPI.h>
PT_THIRD_PARTY_INCLUDES_END

#include "SmokeCommon.h"

#include <string>

namespace
{
	using namespace PhysicsTest::SdkSmoke;

#if defined(PX_PHYSX_STATIC_LIB)
	#define PT_SMOKE_PX_STATIC "\"PX_PHYSX_STATIC_LIB\":1,"
#else
	#define PT_SMOKE_PX_STATIC "\"PX_PHYSX_STATIC_LIB\":0,"
#endif

	// PX_PHYSICS_VERSION is an expression ((5<<24) + (11<<16) + ...), not a literal, so stringising it would put the
	// expression into the JSON. Report "major.minor.bugfix" built from the three literal macros instead.
	static_assert(PX_PHYSICS_VERSION == ((PX_PHYSICS_VERSION_MAJOR << 24) + (PX_PHYSICS_VERSION_MINOR << 16) + (PX_PHYSICS_VERSION_BUGFIX << 8)),
		"PX_PHYSICS_VERSION layout changed: update the PT_BUILD_REPORT_JSON version field");
	PT_BUILD_REPORT_JSON(GBuildReport,
		"\"PX_PHYSICS_VERSION\":\"" PT_BR_STR(PX_PHYSICS_VERSION_MAJOR) "." PT_BR_STR(PX_PHYSICS_VERSION_MINOR) "."
		PT_BR_STR(PX_PHYSICS_VERSION_BUGFIX) "\"," PT_SMOKE_PX_STATIC);

	class FSmokeErrorCallback final : public physx::PxErrorCallback
	{
	public:
		void reportError(const physx::PxErrorCode::Enum Code, const char* Message, const char* File, const int Line) override
		{
			std::printf("[PhysX] error %d: %s (%s:%d)\n", static_cast<int>(Code), Message, File, Line);
			++NumErrors;
		}
		int NumErrors = 0;
	};
}

int main()
{
	using namespace physx;
	PrepareUnattended();

	static PxDefaultAllocator Allocator;
	static FSmokeErrorCallback ErrorCallback;

	FSmokeResult Result;
	Result.Engine = "PhysX";

	PxFoundation* Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, Allocator, ErrorCallback);
	PxPhysics* Physics = Foundation != nullptr ? PxCreatePhysics(PX_PHYSICS_VERSION, *Foundation, PxTolerancesScale()) : nullptr;
	if (Physics == nullptr)
	{
		std::printf("[PtSmoke] PhysX: PxCreateFoundation / PxCreatePhysics failed\n");
		return 1;
	}

	PxDefaultCpuDispatcher* Dispatcher = PxDefaultCpuDispatcherCreate(0);
	PxSceneDesc SceneDesc(Physics->getTolerancesScale());
	SceneDesc.gravity = PxVec3(0.0f, 0.0f, static_cast<PxReal>(GravityZ));
	SceneDesc.cpuDispatcher = Dispatcher;
	SceneDesc.filterShader = PxDefaultSimulationFilterShader;
	SceneDesc.flags |= PxSceneFlag::eDISABLE_SLEEPING;
	PxScene* Scene = Physics->createScene(SceneDesc);

	PxMaterial* Material = Physics->createMaterial(0.5f, 0.5f, 0.0f);   // (staticFriction, dynamicFriction, restitution)

	// Ground: 100 x 100 x 2 m static box, top face at z = 0.
	PxRigidStatic* Ground = PxCreateStatic(*Physics, PxTransform(PxVec3(0.0f, 0.0f, -1.0f)), PxBoxGeometry(50.0f, 50.0f, 1.0f), *Material);
	Scene->addActor(*Ground);

	// 1 m cube at z0 = 10 m (density 1 -> 1 kg); PhysX's default linear damping is 0, angular 0.05.
	const PxReal Half = static_cast<PxReal>(CubeHalfExtent);
	PxRigidDynamic* Box = PxCreateDynamic(*Physics, PxTransform(PxVec3(0.0f, 0.0f, static_cast<PxReal>(StartZ))),
		PxBoxGeometry(Half, Half, Half), *Material, 1.0f);
	Box->setLinearDamping(0.0f);
	Box->setAngularDamping(0.0f);
	Scene->addActor(*Box);

	FStepTimer Timer;
	for (int Step = 0; Step < NumSteps; ++Step)
	{
		Timer.Begin();
		Scene->simulate(DeltaTime);
		Scene->fetchResults(true);
		Timer.End();
	}
	Result.FinalZ = static_cast<double>(Box->getGlobalPose().p.z);
	Result.FinalVz = static_cast<double>(Box->getLinearVelocity().z);
	Result.MedianStepMicroseconds = Timer.MedianMicroseconds();
	Result.bEngineChecksPassed = ErrorCallback.NumErrors == 0;
	Result.EngineCheckNote = "PhysX error callback count is 0";

	Scene->release();
	Dispatcher->release();
	Physics->release();
	Foundation->release();

	const std::string SdkJson = std::string("{\"version\":\"") + std::to_string(PX_PHYSICS_VERSION_MAJOR) + "."
		+ std::to_string(PX_PHYSICS_VERSION_MINOR) + "." + std::to_string(PX_PHYSICS_VERSION_BUGFIX) + "\"}";
	return Report(Result, GBuildReport, SdkJson.c_str());
}
