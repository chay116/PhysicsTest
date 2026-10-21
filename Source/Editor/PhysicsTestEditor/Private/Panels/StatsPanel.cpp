// StatsPanel.cpp - the Stats panel: "stat ChaosCounters" names (PBDRigidsSolver.cpp:49-72 [V]) and, in their own group, the
// harness-only counters; the viewer timings (every one labelled "viz mode, not a benchmark", 03 M4) and the debug draw budget.
#include "PanelCommon.h"

#include <algorithm>

namespace PhysicsTest
{
	void FPhysicsTestEditor::DrawStatsPanel(FEditorContext& Context)
	{
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(Context.ViewWidth) - 498.0f, 28.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(490.0f, std::max(640.0f, static_cast<float>(Context.ViewHeight) - 40.0f)), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Stats", &bShowStats))
		{
			ImGui::End();
			return;
		}
		const FViewerSession& Session = Context.Session;
		const FPhysicsViewportClient& Viewport = Context.Viewport;
		const FViewerFrameStats& Frame = Context.FrameStats;
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "All times: %s", GVizTimingLabel);

		if (Session.HasScene() && ImGui::CollapsingHeader("stat ChaosCounters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			FPhysScene& Scene = *Session.GetScene();
			const FPhysSceneStats& SceneStats = Scene.GetStats();
			PtStepStats StepStats = {};
			StepStats.StructSize = sizeof(PtStepStats);
			const bool bStepStats = Scene.GetBackendScene().GetStepStats(StepStats) == PT_OK;
			int64 NumActiveManifold = -1;
			int64 NumContactPoints = -1;
			int64 NumContactPairs = -1;
			if (Viewport.HasIslands())
			{
				// From the snapshot: every point, the active ones (PT_CONTACT_ACTIVE or the Phi <= 0 approximation of engines without
			// impulses, sleeping contacts included), and the body pairs.
				NumContactPoints = static_cast<int64>(Viewport.GetSnapshot().Contacts.size());
				NumActiveManifold = 0;
				std::vector<uint64> Pairs;
				Pairs.reserve(Viewport.GetSnapshot().Contacts.size());
				for (const PtContactPoint& Contact : Viewport.GetSnapshot().Contacts)
				{
					NumActiveManifold += (Contact.Flags & (PT_CONTACT_ACTIVE | PT_CONTACT_ACTIVE_APPROX)) != 0 ? 1 : 0;
					Pairs.push_back((static_cast<uint64>(Contact.Body0) << 32) | Contact.Body1);
				}
				std::sort(Pairs.begin(), Pairs.end());
				NumContactPairs = static_cast<int64>(std::unique(Pairs.begin(), Pairs.end()) - Pairs.begin());
			}
			else if (bStepStats)
			{
				NumContactPoints = StepStats.NumContactPoints;
				NumContactPairs = StepStats.NumContactPairs;
			}
			using EditorPanels::CounterRow;
			CounterRow("NumBodies", SceneStats.NumBodies);
			CounterRow("NumDynamicBodies", SceneStats.NumDynamicBodies);
			CounterRow("NumKinematicBodies", SceneStats.NumKinematicBodies);
			CounterRow("NumStaticBodies", SceneStats.NumStaticBodies);
			CounterRow("NumMovingBodies", bStepStats && StepStats.NumActiveBodies >= 0 ? StepStats.NumActiveBodies : SceneStats.NumAwakeBodies);
			CounterRow("NumIslands", bStepStats ? StepStats.NumIslands : -1);   // the engine's own islands (-1: not reported)
			CounterRow("NumContacts", NumContactPairs);
			CounterRow("NumManifoldPoints", NumContactPoints);
			CounterRow("NumActiveManifoldPoints", NumActiveManifold);
			CounterRow("NumJoints", SceneStats.NumConstraints);
			// Not STATGROUP_ChaosCounters names: the harness's own islands (FIslandBuilder, what Islands / Sleep state draw) and
			// the sleeping bodies of the step stats.
			ImGui::TextDisabled("harness (not in stat ChaosCounters)");
			CounterRow("NumIslands (harness)", Viewport.HasIslands() ? static_cast<int64>(Viewport.GetIslands().size()) : -1);
			CounterRow("NumSleepingBodies", bStepStats ? StepStats.NumSleepingBodies : -1);
			ImGui::Text("Engine error bits 0x%08x", Scene.GetLastFrameTiming().EngineErrorBits | (bStepStats ? StepStats.EngineErrorBits : 0u));
			if (!Viewport.HasIslands())
			{
				ImGui::TextDisabled("contacts / islands: enable Collisions, Islands or Sleep state (Show menu),");
				ImGui::TextDisabled("or tick 'capture contacts' below");
			}
		}

		if (ImGui::CollapsingHeader("Viewer timings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			using EditorPanels::TimingRow;
			const FViewportDrawTimings& Draw = Viewport.GetTimings();
			TimingRow("Physics frame (Tick)", Session.IsPaused() ? -1.0 : Frame.PhysicsMs);
			if (Session.HasScene())
			{
				const FFrameTiming& Last = Session.GetScene()->GetLastFrameTiming();
				TimingRow("  T_frame (scene)", static_cast<double>(Last.FrameNs) * 1.0e-6);
			}
			TimingRow("Snapshot", Draw.SnapshotMs);
			TimingRow("Islands", Draw.IslandsMs);
			TimingRow("DrawAll (queue)", Draw.DrawAllMs);
			TimingRow("Visit (renderer)", Draw.VisitMs);
			TimingRow("Renderer flush", Frame.RendererFlushCpuMs);
			TimingRow("Snapshot + draw (CPU)", Draw.GetTotalMs() + Frame.RendererFlushCpuMs);
			if (Frame.SnapshotDrawMedianFrames > 0)
			{
				const std::string MedianLabel = "  median of " + std::to_string(Frame.SnapshotDrawMedianFrames) + " frames";
				TimingRow(MedianLabel.c_str(), Frame.SnapshotDrawMedianMs);
			}
			if (Frame.bWindowed && Frame.bVSync)
			{
				// Progress.md M4: the 4 ms criterion is measured under continuous load (headless, or windowed with -NoVSync); with
				// vsync the thread idles ~12 ms per frame and the same work reads 2-3x slower on the same P-core.
				ImGui::TextDisabled("  with vsync this reads 2-3x the continuous-load figure (-NoVSync / headless)");
			}
			TimingRow("UI (ImGui)", Frame.UiCpuMs);
			TimingRow("GPU", Frame.GpuMs);
			TimingRow("Frame", Frame.FrameMs);
			ImGui::Text("%-22s %8.1f", "FPS", Frame.Fps);
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", GVizTimingLabel);
			ImGui::TextDisabled("Physics runs on the message-loop thread: live fps is reported, not a criterion (03 M4).");
		}

		if (ImGui::CollapsingHeader("Debug draw", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// The two stages of UE 5.8.3: capture (FChaosDDFrame) and render (FChaosDDRenderer), each with the region test and
			// p.Chaos.DebugDraw.MaxLines as its budget; the over-budget lines are UE's on-screen messages.
			const FDebugDrawQueueStats& Queue = Viewport.GetQueueStats();
			const FDebugDrawRenderStats& Render = Viewport.GetRenderStageStats();
			const FDebugDrawFlags& Flags = Viewport.GetFlags();
			const std::string Budget = Flags.MaxLines > 0 ? std::to_string(Flags.MaxLines) : std::string("inf");
			ImGui::Text("Capture: %d commands, cost %d / %s", Viewport.GetNumCommands(), Queue.RequestedCost, Budget.c_str());
			ImGui::Text("  rejected: %d over budget, %d outside radius %.0f cm", Queue.NumRejectedBudget, Queue.NumRejectedRegion, Flags.Radius);
			ImGui::Text("Render: %d drawn (+%d latent), cost %d / %s", Render.NumRendered, Render.NumLatent, Render.RenderCost, Budget.c_str());
			ImGui::Text("  rejected: %d over budget, %d outside radius", Render.NumRejectedBudget, Render.NumRejectedRegion);
			if (Queue.WasBudgetExceeded())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Debug Draw Capture Budget Exceeded [%d / %d]", Queue.RequestedCost, Flags.MaxLines);
			}
			if (Render.WasRenderBudgetExceeded())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Debug Draw Render Budget Exceeded [%d / %d]", Render.RenderCost, Flags.MaxLines);
			}
			if (Queue.NumRejectedBudget > 0 || Render.NumRejectedBudget > 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "over p.Chaos.DebugDraw.MaxLines (0 = no limit; render counts a drawn command twice, as UE)");
			}
			if (Queue.NumRejectedRegion > 0 || Render.NumRejectedRegion > 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "outside p.Chaos.DebugDraw.Radius around the camera (0 = everywhere)");
			}
			if (Viewport.GetDrawStats().NumSkippedConvexElements > 0)
			{
				ImGui::Text("Convex elements not drawn (M4): %d", Viewport.GetDrawStats().NumSkippedConvexElements);
			}
			const FD3D11DebugDrawStats& Gpu = Context.RenderStats;
			ImGui::Text("Boxes %d  spheres %d  capsules %d  circles %d", Gpu.NumBoxes, Gpu.NumSpheres, Gpu.NumCapsules, Gpu.NumCircles);
			ImGui::Text("Lines %d  points %d  strings %d", Gpu.NumLines, Gpu.NumPoints, Gpu.NumStrings);
			ImGui::Text("Segments %lld   upload %.1f KB", static_cast<long long>(Gpu.NumSegments), static_cast<double>(Gpu.UploadBytes) / 1024.0);
			ImGui::Text("%dx%d  MSAA %dx  %s", Context.ViewWidth, Context.ViewHeight, Context.MsaaSamples, Context.AdapterName.c_str());
			ImGui::Checkbox("capture contacts (counters, islands)", &Context.Viewport.bAlwaysCaptureContacts);
		}
		ImGui::End();
	}
}
