// PhysicsDebugDraw.h - the Chaos-style drawers, colour tables and DrawAll (02_Architecture 11.1, A.11). [MVP M4]
// ~ Chaos::DebugDraw (ChaosDebugDraw.cpp [V]): every drawer writes FDebugDrawQueue commands, in the order of
// FPBDRigidsSolver::PostTickDebugDraw (PBDRigidsSolver.cpp:3417-3546 [V]):
//   Shapes -> Collisions -> Mass -> Bounds -> Transforms -> ConstraintGraph (islands) -> SleepState -> CollidingShapes -> Joints
// then the harness's native-command passthrough. UE draws density, BVH and cluster graphs in between; Phase 1 does not.
// Geometry comes from the harness data model (FBodyInstance -> FBodySetup::AggGeom, scaled exactly as the ABI marshalling
// does: FK*Elem::GetFinalScaled(Scale3D)), poses from the snapshot. Particles of one mobility are drawn in body-index order,
// statics first, then kinematics, then dynamics (the particle views of PBDRigidsSolver.cpp:3337-3353, 3445-3503 [V]).
// Shapes take the 5.8.3 particle path (cost 1 per shape, particle bounds); every other drawer queues FChaosDDPrimitives-style
// commands (DebugDrawQueue.h), and the viewer renders them through FDebugDrawRenderStage (DebugDrawRenderer.h).
#pragma once

#include "CoreTypes.h"
#include "DebugDrawQueue.h"
#include "DebugDrawSettings.h"
#include "IslandBuilder.h"
#include "Math/Color.h"
#include "Physics/PhysicsInterfaceTypes.h"

#include <span>
#include <vector>

namespace PhysicsTest
{
	class FPhysScene;
	struct FPhysicsDebugSnapshot;
}

namespace PhysicsTest::DebugDraw
{
	// Chaos colour tables (ChaosDebugDraw.cpp:384-411 [V]): shapes Dynamic (255,255,0), Sleeping (128,128,128), Kinematic
	// (0,128,255), Static (255,0,0); bounds the darker set. Anything else is Purple (GetColorFromState's default).
	[[nodiscard]] FColor GetShapeColorByState(EObjectStateType State);
	[[nodiscard]] FColor GetBoundsColorByState(EObjectStateType State);
	// Red, Orange, Yellow, Green, Emerald, Cyan, Turquoise, Blue, Magenta, Purple by Index % 10 (ChaosDebugDraw.cpp:459-476 [V]).
	[[nodiscard]] FColor GetIndexColor(int32 Index);
	// INDEX_NONE -> White, asleep -> Black, else GetIndexColor (ChaosDebugDraw.cpp:480-495 [V]).
	[[nodiscard]] FColor GetIslandColor(int32 IslandIndex, bool bIsAwake);

	struct FDrawContext
	{
		const FPhysScene& Scene;
		const FPhysicsDebugSnapshot& Snapshot;
		const FDebugDrawSettings& Settings;
		const FDebugDrawFlags& Flags;
		// Body index -> particle slot (BuildParticleLookup). Required by the drawers that resolve contact and joint bodies.
		std::span<const int32> ParticleOfBody;
		// FIslandBuilder::GatherIslands of the snapshot when the caller has it already (the viewer); null = gathered on demand.
		const std::vector<FHarnessIsland>* Islands = nullptr;
	};

	void BuildParticleLookup(const FPhysicsDebugSnapshot& Snapshot, std::vector<int32>& OutParticleOfBody);

	void DrawParticleShapes(FDebugDrawQueue& Queue, const FDrawContext& Context);       // ~ FChaosDDParticle::DrawShapes (5.8.3 new queue)
	void DrawCollisions(FDebugDrawQueue& Queue, const FDrawContext& Context);           // ~ DebugDraw::DrawCollisions
	void DrawParticleMass(FDebugDrawQueue& Queue, const FDrawContext& Context);
	void DrawParticleBounds(FDebugDrawQueue& Queue, const FDrawContext& Context);
	void DrawParticleTransforms(FDebugDrawQueue& Queue, const FDrawContext& Context);
	void DrawConstraintGraph(FDebugDrawQueue& Queue, const FDrawContext& Context);      // islands (p.Chaos.Solver.DebugDrawIslands)
	void DrawSleepState(FDebugDrawQueue& Queue, const FDrawContext& Context);           // ~ FPBDIslandManager::DebugDrawSleepState
	void DrawCollidingShapes(FDebugDrawQueue& Queue, const FDrawContext& Context);
	void DrawJointConstraints(FDebugDrawQueue& Queue, const FDrawContext& Context);
	void DrawNativeCommands(FDebugDrawQueue& Queue, const FDrawContext& Context);       // harness: PtDebugDrawCmd passthrough
	// What one DrawAll did besides queueing commands (the viewer's Stats panel).
	struct FDrawStats
	{
		int32 NumIslands = 0;                  // FIslandBuilder islands of the snapshot (when the island drawer ran)
		int32 NumSkippedConvexElements = 0;    // convex elements are not drawn in M4 (no Phase-1 scene has one; Chaos draws
		                                       // their edges, ChaosDebugDraw.cpp:679-720 [V])
	};

	// The nine stages above in PostTickDebugDraw order (each when its flag is on, nothing when p.Chaos.DebugDraw.Enabled is
	// off), then the native commands.
	FDrawStats DrawAll(FDebugDrawQueue& Queue, const FDrawContext& Context);

	// The snapshot flags the enabled drawers need (contacts only for collisions, islands, sleep state and colliding shapes).
	[[nodiscard]] uint32 GetRequiredSnapshotFlags(const FDebugDrawFlags& Flags);
	// FPhysScene::CaptureDebugSnapshot, or - for a backend without a snapshot (PT_E_UNSUPPORTED, e.g. Null) - particles from
	// the host's pulled body states (bFromHostState): actor pose and velocities from FBodyInstance::State, COM from the
	// harness mass properties, bounds from the scaled geometry; no contacts. Untimed, Idle phase.
	void CaptureSnapshot(const FPhysScene& Scene, uint32 SnapshotFlags, FPhysicsDebugSnapshot& Out);
}
