// BulletDebugDrawer.h - FBulletDebugDrawer: Bullet's btIDebugDraw recorded as PtDebugDrawCmd (PT_CAP_NATIVE_DEBUG_DRAW, M9;
// 02_Architecture 11, docs/Abi.md 9.2; 03_WorkPlan M9 "Bullet viz 드로어").
//
// Attached to the world in viz runs only (FPhysScene_Bullet constructor). Bullet looks at an attached drawer in two places that
// matter to the simulation and nowhere else [V]:
//   - stepSimulation copies the DBG_NoDeactivation bit of its mode into the process global gDisableDeactivation
//     (btDiscreteDynamicsWorld.cpp:418-421), which btRigidBody::wantsSleeping reads (btRigidBody.h:522). The mode therefore
//     carries DBG_NoDeactivation exactly when the profile turns sleeping off (every dynamic and kinematic body is then
//     DISABLE_DEACTIVATION anyway, DL48), and the scene restores the global after each step (other scenes of the process).
//   - the narrow phase gets it as btDispatcherInfo::m_debugDraw (btDiscreteDynamicsWorld.cpp:468); in Bullet 3.25 the
//     algorithms draw only under #if 0 / DEBUG_CONTACTS or for concave triangle meshes (btConvexConcaveCollisionAlgorithm.cpp:93,
//     no PT_SHAPE_TRIMESH in Phase 1), and no other mode bit is read by the library (DBG_EnableCCD / DisableBulletLCP /
//     EnableSatComparison are demo flags). Draw calls outside a capture are dropped (bRecording).
// So attaching it does not change the results: PhysicsTest.BulletBackend.VizDrawerKeepsResults (final h_native with and without
// the drawer, UEParity, sleep on) and the bench's -Gate=G11 -G11Knob=viz.
//
// A capture (FPhysScene_Bullet::CaptureDebugSnapshot with PT_SNAP_NATIVE_DRAW) runs btDiscreteDynamicsWorld::debugDrawWorld,
// which only reads the world (shapes by activation state, the manifolds' points, the joint frames). Lengths become cm; colours
// 0xAARRGGBB with A = 255 (PtDebugDrawCmd::ColorBGRA, FColor::FromPackedARGB); Thickness 0 (1 px); Lifetime 0 (this capture only:
// the v1.1 rule of docs/Abi.md 9.2, as PhysX; M9 fix round 2 - the drawer wrote 1e-4 s before). Shapes keep their primitive type (box, sphere, capsule); the
// helper shapes (transforms, arcs, sphere patches, cones, cylinders, planes, AABBs) arrive as Bullet's own line decompositions.
// PtDebugSnapshotRequest::NativeDrawCategories is not interpreted (no category set is defined; 0 = the drawer's whole mode).
#pragma once

#include "BulletIncludes.h"
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"

namespace PhysicsTest::BulletBackend
{
	using namespace ::PhysicsTest::Abi::FixedWidth;

	class FPhysScene_Bullet;

	class FBulletDebugDrawer final : public btIDebugDraw
	{
	public:
		// What a viz run draws (Bullet demo defaults: shapes, contact points, joint frames and limits).
		static constexpr int DrawModes = DBG_DrawWireframe | DBG_DrawContactPoints | DBG_DrawConstraints | DBG_DrawConstraintLimits;

		// bNoDeactivation: the profile turns sleeping off (DBG_NoDeactivation mirrors it, see the header comment).
		FBulletDebugDrawer(FPhysScene_Bullet& InOwner, bool bNoDeactivation);

		// One capture: Begin clears the records, every draw call in between is recorded (up to MaxCommands), End stops.
		void Begin(uint32 MaxCommands);
		void End();
		const btAlignedObjectArray<PtDebugDrawCmd>& GetCommands() const { return Commands; }
		const btAlignedObjectArray<char>& GetStrings() const { return Strings; }
		bool IsTruncated() const { return bTruncated; }
		uint64 GetNumWarnings() const { return NumWarnings; }

		// btIDebugDraw. Every overload of a name is overridden, so none is hidden.
		void drawLine(const btVector3& From, const btVector3& To, const btVector3& Color) override;
		void drawLine(const btVector3& From, const btVector3& To, const btVector3& FromColor, const btVector3& ToColor) override;
		void drawSphere(btScalar Radius, const btTransform& Transform, const btVector3& Color) override;
		void drawSphere(const btVector3& Position, btScalar Radius, const btVector3& Color) override;
		void drawTriangle(const btVector3& V0, const btVector3& V1, const btVector3& V2, const btVector3& N0, const btVector3& N1, const btVector3& N2,
			const btVector3& Color, btScalar Alpha) override;
		void drawTriangle(const btVector3& V0, const btVector3& V1, const btVector3& V2, const btVector3& Color, btScalar Alpha) override;
		void drawContactPoint(const btVector3& PointOnB, const btVector3& NormalOnB, btScalar Distance, int LifeTime, const btVector3& Color) override;
		void reportErrorWarning(const char* WarningString) override;
		void draw3dText(const btVector3& Location, const char* TextString) override;
		void setDebugMode(int InMode) override { Mode = InMode; }
		int getDebugMode() const override { return Mode; }
		void drawBox(const btVector3& BoundsMin, const btVector3& BoundsMax, const btVector3& Color) override;
		void drawBox(const btVector3& BoundsMin, const btVector3& BoundsMax, const btTransform& Transform, const btVector3& Color) override;
		void drawCapsule(btScalar Radius, btScalar HalfHeight, int UpAxis, const btTransform& Transform, const btVector3& Color) override;
		void clearLines() override {}   // Begin owns the reset (debugDrawWorld calls this first)
		void flushLines() override {}

	private:
		// The next record, or nullptr outside a capture / past MaxCommands (then bTruncated).
		PtDebugDrawCmd* AddCommand(uint32 Type, const btVector3& Color);

		FPhysScene_Bullet& Owner;
		btAlignedObjectArray<PtDebugDrawCmd> Commands;
		btAlignedObjectArray<char> Strings;
		uint32 MaxCommands = 0;
		uint64 NumWarnings = 0;
		int Mode = DrawModes;
		bool bRecording = false;
		bool bTruncated = false;
	};
}
