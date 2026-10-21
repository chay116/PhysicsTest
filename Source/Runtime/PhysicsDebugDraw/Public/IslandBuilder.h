// IslandBuilder.h - FIslandBuilder, the harness island graph used for every engine (02_Architecture 11.1, A.11). [MVP M4]
// Jolt has no public island API (Body::GetIslandIndexInternal is internal) and the other SDKs differ, so the debug draw
// colours islands from one harness rule:
//   - edges: contact points with Phi <= 0 or a solver impulse (PT_CONTACT_ACTIVE / ACTIVE_APPROX; M5: a resting PhysX
//     point has Phi ~ +0 in float, a Bullet point may end its step at Phi > 0 after pushing; speculative points without
//     an impulse are not edges) and the scene's joints;
//   - nodes: dynamic bodies (ObjectState Dynamic or Sleeping). Static and kinematic bodies never join two islands (as in
//     Chaos, where they are not island nodes), but a dynamic body touching one is in the graph;
//   - a dynamic body without any edge is isolated (INDEX_NONE), as Chaos' particles outside the constraint graph
//     (FPBDIslandManager::DebugDrawSleepState treats them apart, IslandManager.cpp:4194-4224 [V]);
//   - island id = the lowest body index of the island, so the result depends only on the snapshot (deterministic).
// Jolt reports no contacts for sleeping bodies; its snapshot keeps the contacts of a sleeping island from the step it
// fell asleep in (PT_CONTACT_SLEEPING), so a sleeping island stays one island (Progress.md M4). Bullet keeps the manifolds
// of sleeping pairs itself; PhysX reports nothing in the step an island falls asleep, so its snapshot keeps the previous
// step's points (Progress.md M5). Particles whose BodyIndex is not below GetBodyIndexBound (PT_INDEX_NONE, a garbage index)
// are skipped, never used to size an array (M5).
#pragma once

#include "CoreTypes.h"
#include "Math/Box.h"

#include <span>
#include <vector>

namespace PhysicsTest
{
	struct FConstraintInstance;
	struct FPhysicsDebugSnapshot;

	struct FHarnessIsland
	{
		int32 Id = INDEX_NONE;        // lowest body index of the island
		bool bAwake = false;          // any member not asleep
		FBox Bounds;                  // union of the members' snapshot bounds
		int32 NumParticles = 0;
	};

	class FIslandBuilder
	{
	public:
		// Fills InOut.HarnessIslandIndex, parallel to InOut.Particles (INDEX_NONE for static, kinematic and isolated bodies).
		// Joints: the scene's constraints (FPhysScene::GetConstraints); a joint to the world or to a body without a
		// particle is an edge to nothing.
		static void Build(FPhysicsDebugSnapshot& InOut, std::span<FConstraintInstance* const> Joints);
		// The islands of a built snapshot, sorted by id.
		static void GatherIslands(const FPhysicsDebugSnapshot& Snapshot, std::vector<FHarnessIsland>& OutIslands);
	};
}
