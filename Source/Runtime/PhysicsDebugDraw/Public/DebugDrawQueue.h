// DebugDrawQueue.h - FLatentDrawCommand and FDebugDrawQueue (02_Architecture 11.1, A.11). [MVP M4]
// The capture stage of the Chaos debug draw as UE 5.8.3 runs it with its default bChaosDebugDraw_UseNewQueue = true
// (DebugDrawQueue.cpp:10 [V]). The method names, argument order and defaults are Chaos::FDebugDrawQueue's
// (Chaos/DebugDrawQueue.h:103-231 [V]), but what a call costs and how it is culled follows the path the solver drawers
// actually take in 5.8.3:
//   - Draw* (point, line, arrow, sphere, box, string, circle, capsule, triangle) = the file-local wrappers of
//     ChaosDebugDraw.cpp:151-245 [V] -> FChaosDDPrimitives (DebugDrawPrimitives.cpp:11-120 [V]): capture cost point / line /
//     arrow / circle 1, sphere 64, capsule 16, box 12, triangle 3, string 10, and every command is culled by its own bounds
//     (FChaosDDFrame::IsInDrawRegion(FBox): squared distance from the bounds to the centre < W^2, W = 0 everywhere;
//     ChaosDDFrame.h:106-114 [V]). The bounds are UE's, quirks included: a box's bounds use half of the size it is drawn
//     with, a capsule's bounds are its axis segment only (GetCommandBounds).
//   - DrawParticleShape = FChaosDDParticleShape::Draw (DebugDrawParticle.cpp:288-349 [V]), the 5.8.3 path of
//     p.Chaos.Solver.DebugDrawShapes (PBDRigidsSolver.cpp:3445-3453 [V]): cost 1 per shape, culled by the particle's bounds.
//   - DrawDebugCoordinateSystem has no new-queue wrapper and stays on the legacy API: cost 3, culled by its position
//     (FDebugDrawQueue::AcceptCommand, DebugDrawQueue.h:351-361 [V]); its lines are latent commands (bLatent) that the
//     render stage draws without a budget (FChaosDDRenderer::RenderLatentCommand, ChaosDebugDrawSubsystem.cpp:206-220 [V]).
//   - the budget is FChaosDDFrame::AddToCost: the cost accumulates even when a command is rejected; budget 0 = infinite
//     (ChaosDDFrame.h:136-149 [V]).
// The render stage (FChaosDDRenderer's second budget and region test) is FDebugDrawRenderStage (DebugDrawRenderer.h).
// Differences (Progress.md M4):
//   - one queue per viewer scene instead of the process singleton GetInstance(); single-threaded (the debug draw runs on
//     the message-loop thread after EndFrame), so there is no lock.
//   - DrawDebugString takes no AActor* (UE: TestBaseActor); DrawDebugTriangle, DrawParticleShape and Enqueue are harness
//     entry points for FChaosDDPrimitives::DrawTriangle, FChaosDDParticleShape::Draw and native commands.
#pragma once

#include "CoreTypes.h"
#include "Math/Box.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

#include <string>
#include <string_view>
#include <vector>

namespace PhysicsTest
{
	struct FLatentDrawCommand
	{
		enum class EDrawType : uint8
		{
			Point,
			Line,
			DirectionalArrow,
			Sphere,
			Box,
			String,
			Circle,
			Capsule,
			Triangle,   // harness extension (FChaosDDPrimitives::DrawTriangle / RenderTriangle)
		};

		FVector LineStart;
		FVector LineEnd;
		FColor Color;
		int32 Segments = 0;
		bool bPersistentLines = false;
		float ArrowSize = 0.0f;
		float LifeTime = 0.0f;
		uint8 DepthPriority = 0;
		float Thickness = 0.0f;
		double Radius = 0.0;
		double HalfHeight = 0.0;
		FVector Center;
		FVector Extent;
		FQuat Rotation;
		FVector TextLocation;
		std::string Text;
		bool bDrawShadow = false;
		float FontScale = 1.0f;
		float Duration = 0.0f;
		FMatrix TransformMatrix;
		bool bDrawAxis = false;
		FVector YAxis;
		FVector ZAxis;
		FVector TriangleC;   // harness extension
		EDrawType Type = EDrawType::Point;
		// Harness: queued through the legacy FDebugDrawQueue API (UE: FChaosDDFrame::LatentCommands). Rendered first and
		// outside the render budget (UChaosDebugDrawSubsystem::RenderFrame, ChaosDebugDrawSubsystem.cpp:539-560 [V]).
		bool bLatent = false;
	};

	// Per-frame bookkeeping of the capture stage (the viewer's Stats panel).
	struct FDebugDrawQueueStats
	{
		int32 NumAccepted = 0;
		int32 NumRejectedRegion = 0;    // outside p.Chaos.DebugDraw.Radius around the camera
		int32 NumRejectedBudget = 0;    // over p.Chaos.DebugDraw.MaxLines
		int32 RequestedCost = 0;        // cost of every command that passed the region test (UE: FChaosDDFrame::CommandCost)
		int32 Budget = 0;               // p.Chaos.DebugDraw.MaxLines of the frame (0 = infinite)
		// UE: FChaosDDFrame::WasCommandBudgetExceeded -> "Debug Draw Capture Budget Exceeded" (ChaosDebugDrawSubsystem.cpp:528-533 [V]).
		[[nodiscard]] bool WasBudgetExceeded() const { return RequestedCost > Budget && Budget > 0; }
	};

	class FDebugDrawQueue
	{
	public:
		// UE: moves the queue out and keeps its capacity; resets the cost. bPaused is accepted for signature parity.
		void ExtractAllElements(std::vector<FLatentDrawCommand>& OutDrawCommands, bool bPaused = false);

		// No Size argument: Thickness is the point size (DebugDrawQueue.h:114 [V]).
		void DrawDebugPoint(const FVector& Position, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness);
		void DrawDebugLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.0f,
			uint8 DepthPriority = 0, float Thickness = 0.0f);
		void DrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines = false,
			float LifeTime = -1.0f, uint8 DepthPriority = 0, float Thickness = 0.0f);
		// Red / Green / Blue lines of length Scale along the rotated axes; one legacy acceptance of cost 3 (DebugDrawQueue.h:150-167 [V]).
		void DrawDebugCoordinateSystem(const FVector& Position, const FRotator& AxisRot, double Scale, bool bPersistentLines = false, float LifeTime = -1.0f,
			uint8 DepthPriority = 0, float Thickness = 0.0f);
		// Segments is recorded; the render stage draws every sphere with 8 (FChaosDDRenderer::SphereSegments).
		void DrawDebugSphere(const FVector& Center, double Radius, int32 Segments, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.0f,
			uint8 DepthPriority = 0, float Thickness = 0.0f);
		// Extent is a half extent (Chaos callers pass 0.5 * Extents, ChaosDebugDraw.cpp:1080 [V]).
		void DrawDebugBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime,
			uint8 DepthPriority, float Thickness);
		void DrawDebugString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float Duration, bool bDrawShadow, float FontScale);
		// The circle lies in the plane of YAxis and ZAxis (DebugDrawQueue.h:209 [V]). bDrawAxis is recorded but never drawn, as in
		// 5.8.3's default path: its DrawDebugCircle wrapper calls FChaosDDPrimitives::DrawCircle, which has no axis flag
		// (ChaosDebugDraw.cpp:211-215 [V]), and IDebugDrawRenderer::RenderCircle has none either.
		void DrawDebugCircle(const FVector& Center, double Radius, int32 Segments, const FColor& Color, bool bPersistentLines, float LifeTime,
			uint8 DepthPriority, float Thickness, const FVector& YAxis, const FVector& ZAxis, bool bDrawAxis);
		// HalfHeight includes the radius (Chaos passes 0.5 * Height + Radius, ChaosDebugDraw.cpp:1091 [V]).
		void DrawDebugCapsule(const FVector& Center, double HalfHeight, double Radius, const FQuat& Rotation, const FColor& Color, bool bPersistentLines,
			float LifeTime, uint8 DepthPriority, float Thickness);

		// Harness entry points.
		void DrawDebugTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LifeTime, float Thickness);
		// One shape element of a particle (Type Box / Sphere / Capsule with the Draw* meanings above): cost 1, culled by
		// ParticleBounds. bShouldRender = false (FChaosDDParticleShape::ShouldRender says no, e.g. ShowStatics 0) still
		// charges the capture cost but queues nothing, as UE decides that only at render time.
		void DrawParticleShape(const FBox& ParticleBounds, FLatentDrawCommand::EDrawType Type, const FVector& Center, const FQuat& Rotation,
			const FVector& HalfExtent, double Radius, double HalfHeight, const FColor& Color, float Thickness, bool bShouldRender = true);
		// A native PtDebugDrawCmd translated by the caller; cost and bounds by type as above.
		void Enqueue(const FLatentDrawCommand& Command);

		void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
		[[nodiscard]] bool IsEnabled() const { return bEnabled; }
		// p.Chaos.DebugDraw.MaxLines (0 = infinite).
		void SetMaxCost(int32 InMaxCost) { MaxCost = InMaxCost; }
		[[nodiscard]] int32 GetMaxCost() const { return MaxCost; }
		// The camera location and p.Chaos.DebugDraw.Radius (0 = infinite).
		void SetRegionOfInterest(const FVector& Pos, float InRadius);
		[[nodiscard]] FVector GetCenterOfInterest() const { return CenterOfInterest; }
		[[nodiscard]] double GetRadiusOfInterest() const { return RadiusOfInterest; }
		// FChaosDDFrame::IsInDrawRegion for a point (inside the sphere), a sphere (intersects) and a box (squared distance < W^2).
		[[nodiscard]] bool IsInRegionOfInterest(const FVector& Pos) const;
		[[nodiscard]] bool IsInRegionOfInterest(const FVector& Pos, double Radius) const;
		[[nodiscard]] bool IsInRegionOfInterest(const FBox& Bounds) const;

		// A drawer that culls by bounds before queueing (Chaos' DrawShapesImpl / DrawCollisionImpl region tests) reports the
		// rejection here, so the Stats panel counts it with the queue's own region rejections.
		void NoteRegionRejection() { ++Stats.NumRejectedRegion; }
		[[nodiscard]] const FDebugDrawQueueStats& GetStats() const { return Stats; }
		[[nodiscard]] int32 Num() const { return static_cast<int32>(Commands.size()); }
		// Drops the queued commands and the statistics (the capacity is kept).
		void Reset();

		// The capture cost of a command of this type (FChaosDDPrimitives, DebugDrawPrimitives.cpp:13-112 [V]).
		[[nodiscard]] static int32 GetCommandCost(const FLatentDrawCommand& Command);
		// The bounds FChaosDDPrimitives and FChaosDDRenderer test for a command (DebugDrawPrimitives.cpp:14-113,
		// ChaosDebugDrawSubsystem.cpp:103-198 [V]): a box uses FBox(-0.5 * Size, 0.5 * Size) of the half extent it is given,
		// a capsule the segment Center +- HalfHeight along its Z axis, a circle and a sphere Center +- Radius.
		[[nodiscard]] static FBox GetCommandBounds(const FLatentDrawCommand& Command);

	private:
		bool AcceptCommand(int32 Cost, const FBox& Bounds);
		bool AcceptLegacyCommand(int32 Cost, const FVector& Position);
		bool AddToCost(int32 Cost);

		std::vector<FLatentDrawCommand> Commands;
		FDebugDrawQueueStats Stats;
		FVector CenterOfInterest;
		double RadiusOfInterest = 0.0;
		int32 MaxCost = 0;
		bool bEnabled = true;
	};

	namespace DebugDraw
	{
		// The world AABB of a box of half extent HalfExtent rotated by Rotation about Center (UE FBox::TransformBy).
		[[nodiscard]] FBox GetRotatedBoxBounds(const FVector& Center, const FQuat& Rotation, const FVector& HalfExtent);
	}
}
