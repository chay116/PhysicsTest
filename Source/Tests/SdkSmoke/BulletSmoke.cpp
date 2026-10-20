// BulletSmoke.cpp - Bullet 3.25 smoke test (03_WorkPlan M0 criterion 6). Ported from prototypes/hello_bullet,
// changed to Z-up, g = 9.80, no damping, no deactivation, single-threaded world (BT_THREADSAFE off).
#include "PtThirdPartyIncludes.h"

PT_THIRD_PARTY_INCLUDES_START
#include <btBulletDynamicsCommon.h>
#include <LinearMath/btThreads.h>
PT_THIRD_PARTY_INCLUDES_END

#include "SmokeCommon.h"

#include <string>

namespace
{
	using namespace PhysicsTest::SdkSmoke;

#if defined(BT_USE_DOUBLE_PRECISION)
	#define PT_SMOKE_BT_DOUBLE "\"BT_USE_DOUBLE_PRECISION\":1,"
#else
	#define PT_SMOKE_BT_DOUBLE "\"BT_USE_DOUBLE_PRECISION\":0,"
#endif
#if defined(BT_USE_SSE)
	#define PT_SMOKE_BT_SSE "\"BT_USE_SSE\":1,"
#else
	#define PT_SMOKE_BT_SSE "\"BT_USE_SSE\":0,"
#endif
#if BT_THREADSAFE
	#define PT_SMOKE_BT_THREADSAFE "\"BT_THREADSAFE\":1,"
#else
	#define PT_SMOKE_BT_THREADSAFE "\"BT_THREADSAFE\":0,"
#endif

	PT_BUILD_REPORT_JSON(GBuildReport, "\"BT_BULLET_VERSION\":" PT_BR_STR(BT_BULLET_VERSION) "," PT_SMOKE_BT_DOUBLE PT_SMOKE_BT_SSE PT_SMOKE_BT_THREADSAFE);
}

int main()
{
	PrepareUnattended();

	FSmokeResult Result;
	Result.Engine = "Bullet";
	{
		btDefaultCollisionConfiguration CollisionConfiguration;
		btCollisionDispatcher Dispatcher(&CollisionConfiguration);
		btDbvtBroadphase Broadphase;
		btSequentialImpulseConstraintSolver Solver;
		btDiscreteDynamicsWorld World(&Dispatcher, &Broadphase, &Solver, &CollisionConfiguration);
		World.setGravity(btVector3(0, 0, btScalar(GravityZ)));   // Bullet's default is (0, -10, 0)

		// Ground: 100 x 100 x 2 m static box, top face at z = 0.
		btBoxShape GroundShape(btVector3(50, 50, 1));
		btRigidBody::btRigidBodyConstructionInfo GroundInfo(0, nullptr, &GroundShape);
		GroundInfo.m_startWorldTransform.setIdentity();
		GroundInfo.m_startWorldTransform.setOrigin(btVector3(0, 0, -1));
		btRigidBody Ground(GroundInfo);
		World.addRigidBody(&Ground);

		// 1 m cube, 1 kg, at z0 = 10 m.
		const btScalar Half = static_cast<btScalar>(CubeHalfExtent);
		btBoxShape BoxShape(btVector3(Half, Half, Half));
		const btScalar Mass = 1;
		btVector3 LocalInertia(0, 0, 0);
		BoxShape.calculateLocalInertia(Mass, LocalInertia);
		btRigidBody::btRigidBodyConstructionInfo BoxInfo(Mass, nullptr, &BoxShape, LocalInertia);
		BoxInfo.m_startWorldTransform.setIdentity();
		BoxInfo.m_startWorldTransform.setOrigin(btVector3(0, 0, btScalar(StartZ)));
		BoxInfo.m_linearDamping = 0;
		BoxInfo.m_angularDamping = 0;
		btRigidBody Box(BoxInfo);
		Box.setActivationState(DISABLE_DEACTIVATION);
		World.addRigidBody(&Box);

		FStepTimer Timer;
		for (int Step = 0; Step < NumSteps; ++Step)
		{
			Timer.Begin();
			World.stepSimulation(DeltaTime, 0, DeltaTime);   // maxSubSteps = 0: exactly one internal step of dt (02 8.4)
			Timer.End();
		}
		Result.FinalZ = static_cast<double>(Box.getWorldTransform().getOrigin().z());
		Result.FinalVz = static_cast<double>(Box.getLinearVelocity().z());
		Result.MedianStepMicroseconds = Timer.MedianMicroseconds();

		World.removeRigidBody(&Box);
		World.removeRigidBody(&Ground);
	}

	const std::string SdkJson = std::string("{\"version\":") + std::to_string(BT_BULLET_VERSION) + "}";
	return Report(Result, GBuildReport, SdkJson.c_str());
}
