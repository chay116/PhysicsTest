// PhysicsViewportClient.h - FPhysicsViewportClient: one viewport's debug draw frame (02_Architecture 11.1, 11.2). [MVP M4]
// After EndFrame, untimed for physics, timed for the viewer's Stats (every number labelled "viz mode, not a benchmark"):
//   CaptureSnapshot (backend snapshot, or the host state for Null) -> FIslandBuilder (when contacts were captured) ->
//   DebugDraw::DrawAll into the FDebugDrawQueue (capture: camera region, p.Chaos.DebugDraw.MaxLines budget) ->
//   ExtractAllElements -> VisitCommands through FDebugDrawRenderStage (render: UE's second region test and budget) -> the
//   renderer. The selected body gets a white bounds box drawn straight to the renderer (outside both budgets).
//   Renderer-independent: the renderer is an IDebugDrawRenderer.
#pragma once

#include "CoreTypes.h"
#include "DebugDrawQueue.h"
#include "DebugDrawRenderer.h"
#include "DebugDrawSettings.h"
#include "IslandBuilder.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "PhysicsDebugDraw.h"

#include <vector>

namespace PhysicsTest
{
	class FPhysScene;
	class IDebugDrawRenderer;
	struct FSceneView;

	struct FViewportDrawTimings
	{
		double SnapshotMs = 0.0;   // CaptureSnapshot
		double IslandsMs = 0.0;    // FIslandBuilder::Build (+ GatherIslands)
		double DrawAllMs = 0.0;    // DebugDraw::DrawAll into the queue
		double VisitMs = 0.0;      // ExtractAllElements + VisitCommands (render stage included)
		[[nodiscard]] double GetTotalMs() const { return SnapshotMs + IslandsMs + DrawAllMs + VisitMs; }
	};

	class FPhysicsViewportClient
	{
	public:
		// One frame of debug draw for Scene, seen from View, into Renderer.
		void BuildFrame(const FPhysScene& Scene, const FSceneView& View, IDebugDrawRenderer& Renderer);
		// The body under a pixel ray (closest particle bounds hit), INDEX_NONE for none. Uses the last snapshot.
		[[nodiscard]] int32 PickBody(const FVector& RayOrigin, const FVector& RayDirection) const;

		[[nodiscard]] const FPhysicsDebugSnapshot& GetSnapshot() const { return Snapshot; }
		[[nodiscard]] const FDebugDrawQueueStats& GetQueueStats() const { return QueueStats; }
		[[nodiscard]] const FDebugDrawRenderStats& GetRenderStageStats() const { return RenderStageStats; }
		[[nodiscard]] const DebugDraw::FDrawStats& GetDrawStats() const { return DrawStats; }
		[[nodiscard]] const FViewportDrawTimings& GetTimings() const { return Timings; }
		[[nodiscard]] const std::vector<FHarnessIsland>& GetIslands() const { return Islands; }
		[[nodiscard]] bool HasIslands() const { return bHasIslands; }
		[[nodiscard]] int32 GetNumCommands() const { return static_cast<int32>(Commands.size()); }
		[[nodiscard]] const FDebugDrawFlags& GetFlags() const { return Flags; }
		[[nodiscard]] const std::vector<int32>& GetParticleOfBody() const { return ParticleOfBody; }

		int32 SelectedBody = INDEX_NONE;   // body index (ActorHandle.Index)
		bool bAlwaysCaptureContacts = false;   // the Inspector / Stats want contact counts and islands even when no drawer needs them

	private:
		FDebugDrawQueue Queue;
		FPhysicsDebugSnapshot Snapshot;
		std::vector<FLatentDrawCommand> Commands;
		std::vector<int32> ParticleOfBody;
		std::vector<FHarnessIsland> Islands;
		FDebugDrawSettings Settings;
		FDebugDrawFlags Flags;
		FDebugDrawQueueStats QueueStats;
		FDebugDrawRenderStats RenderStageStats;
		DebugDraw::FDrawStats DrawStats;
		FViewportDrawTimings Timings;
		bool bHasIslands = false;
	};
}
