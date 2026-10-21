// PhysicsViewportClient.cpp - FPhysicsViewportClient (see the header).
#include "PhysicsViewportClient.h"

#include "DebugDrawRenderer.h"
#include "HAL/PlatformTime.h"
#include "PhysScene.h"
#include "SceneView.h"

#include <limits>

namespace PhysicsTest
{
	namespace
	{
		double MillisecondsSince(const uint64 Begin)
		{
			return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Begin) * 1000.0;
		}
	}

	void FPhysicsViewportClient::BuildFrame(const FPhysScene& Scene, const FSceneView& View, IDebugDrawRenderer& Renderer)
	{
		DebugDraw::ReadConsoleVariables(Settings, Flags);

		uint64 Begin = FPlatformTime::Cycles64();
		uint32 SnapshotFlags = DebugDraw::GetRequiredSnapshotFlags(Flags);
		if (bAlwaysCaptureContacts)
		{
			SnapshotFlags |= PT_SNAP_CONTACTS;
		}
		DebugDraw::CaptureSnapshot(Scene, SnapshotFlags, Snapshot);
		Timings.SnapshotMs = MillisecondsSince(Begin);

		Begin = FPlatformTime::Cycles64();
		bHasIslands = (SnapshotFlags & PT_SNAP_CONTACTS) != 0;
		if (bHasIslands)
		{
			FIslandBuilder::Build(Snapshot, Scene.GetConstraints());
			FIslandBuilder::GatherIslands(Snapshot, Islands);
		}
		else
		{
			Snapshot.HarnessIslandIndex.clear();
			Islands.clear();
		}
		DebugDraw::BuildParticleLookup(Snapshot, ParticleOfBody);
		Timings.IslandsMs = MillisecondsSince(Begin);

		Begin = FPlatformTime::Cycles64();
		Queue.SetEnabled(Flags.bEnabled);
		Queue.SetMaxCost(Flags.MaxLines);
		Queue.SetRegionOfInterest(View.ViewOrigin, Flags.Radius);
		const DebugDraw::FDrawContext Context{ Scene, Snapshot, Settings, Flags, ParticleOfBody, bHasIslands ? &Islands : nullptr };
		DrawStats = DebugDraw::DrawAll(Queue, Context);
		Timings.DrawAllMs = MillisecondsSince(Begin);

		Begin = FPlatformTime::Cycles64();
		QueueStats = Queue.GetStats();
		Queue.ExtractAllElements(Commands);
		// UChaosDebugDrawSubsystem::RenderScene: one FChaosDDRenderer per frame with the scene's draw region and the command
		// budget as the render budget (ChaosDebugDrawSubsystem.cpp:483-513 [V]).
		FDebugDrawRenderStage RenderStage(Renderer, FSphere(View.ViewOrigin, static_cast<double>(Flags.Radius)), Flags.MaxLines);
		VisitCommands(Commands, RenderStage);
		RenderStageStats = RenderStage.GetStats();
		if (SelectedBody != INDEX_NONE && SelectedBody >= 0 && static_cast<size_t>(SelectedBody) < ParticleOfBody.size()
			&& ParticleOfBody[static_cast<size_t>(SelectedBody)] != INDEX_NONE)
		{
			const PtParticleSnapshot& Particle = Snapshot.Particles[static_cast<size_t>(ParticleOfBody[static_cast<size_t>(SelectedBody)])];
			const FVector Min(Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z);
			const FVector Max(Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z);
			Renderer.RenderBox((Min + Max) * 0.5, FQuat::Identity, (Max - Min) * 0.5 + FVector(2.0), FColor::White, 2.0f * Settings.LineThickness, 0.0f);
		}
		Timings.VisitMs = MillisecondsSince(Begin);
	}

	int32 FPhysicsViewportClient::PickBody(const FVector& RayOrigin, const FVector& RayDirection) const
	{
		// Slab test against every particle's bounds; the nearest entry wins. Statics (the ground) only when nothing else hits.
		double BestT = std::numeric_limits<double>::max();
		int32 Best = INDEX_NONE;
		double BestStaticT = std::numeric_limits<double>::max();
		int32 BestStatic = INDEX_NONE;
		for (const PtParticleSnapshot& Particle : Snapshot.Particles)
		{
			double TMin = 0.0;
			double TMax = std::numeric_limits<double>::max();
			const double Min[3] = { Particle.BoundsMin.X, Particle.BoundsMin.Y, Particle.BoundsMin.Z };
			const double Max[3] = { Particle.BoundsMax.X, Particle.BoundsMax.Y, Particle.BoundsMax.Z };
			bool bHit = true;
			for (int32 Axis = 0; Axis < 3 && bHit; ++Axis)
			{
				const double Origin = RayOrigin[Axis];
				const double Direction = RayDirection[Axis];
				if (FMath::Abs(Direction) < 1e-12)
				{
					bHit = Origin >= Min[Axis] && Origin <= Max[Axis];
					continue;
				}
				double T0 = (Min[Axis] - Origin) / Direction;
				double T1 = (Max[Axis] - Origin) / Direction;
				if (T0 > T1)
				{
					const double Swap = T0;
					T0 = T1;
					T1 = Swap;
				}
				TMin = FMath::Max(TMin, T0);
				TMax = FMath::Min(TMax, T1);
				bHit = TMin <= TMax;
			}
			if (!bHit)
			{
				continue;
			}
			if (Particle.ObjectState == static_cast<uint8>(EObjectStateType::Static))
			{
				if (TMin < BestStaticT)
				{
					BestStaticT = TMin;
					BestStatic = static_cast<int32>(Particle.BodyIndex);
				}
			}
			else if (TMin < BestT)
			{
				BestT = TMin;
				Best = static_cast<int32>(Particle.BodyIndex);
			}
		}
		return Best != INDEX_NONE ? Best : BestStatic;
	}
}
