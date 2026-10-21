// DebugDrawRenderer.h - IDebugDrawRenderer, FDebugDrawRenderStage and VisitCommands (02_Architecture 11.1, A.11). [MVP M4]
// IDebugDrawRenderer mirrors ChaosDD::Private::IChaosDDRenderer (ChaosDDRenderer.h:19-44 [V]; method and parameter order
// verified): IsServer, GetDrawRegion, the nine Render* primitives and RenderLatentCommand. VisitCommands plays the role of
// UChaosDebugDrawSubsystem::RenderFrame for the harness queue (ChaosDebugDrawSubsystem.cpp:539-560 [V]): the latent
// (legacy-API) commands first through RenderLatentCommand, then every other command as one Render* call.
// FDebugDrawRenderStage is the harness counterpart of FChaosDDRenderer (ChaosDebugDrawSubsystem.cpp:50-246 [V]), the second
// region test and budget UE 5.8.3 applies when it renders a captured frame (render budget = p.Chaos.DebugDraw.MaxLines,
// region = the camera and p.Chaos.DebugDraw.Radius); it forwards what passes to the real renderer.
// Two meanings are fixed (unit tests PhysicsTest.DebugDraw.*):
//   - RenderCapsule's HalfHeight includes the radius: Chaos passes Scale.Z * (0.5 * Height + Radius)
//     (DebugDrawImplicitObject.cpp:62 [V]); an ABI capsule (Radius, HalfLength) is drawn with HalfHeight = HalfLength + Radius.
//   - RenderBox's Size is a half extent: Chaos passes 0.5 * Scale * Extents (DebugDrawImplicitObject.cpp:73-76 [V]).
#pragma once

#include "CoreTypes.h"
#include "Math/Box.h"
#include "Math/Color.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Sphere.h"
#include "Math/Vector.h"

#include <span>
#include <string_view>

namespace PhysicsTest
{
	struct FLatentDrawCommand;

	class IDebugDrawRenderer
	{
	public:
		virtual ~IDebugDrawRenderer() = default;

		// Are we rendering a server scene? (always false in the harness)
		[[nodiscard]] virtual bool IsServer() const = 0;
		// The region of interest (centre = the camera, W = p.Chaos.DebugDraw.Radius; 0 = everywhere).
		[[nodiscard]] virtual FSphere GetDrawRegion() const = 0;

		virtual void RenderPoint(const FVector& Position, const FColor& Color, float PointSize, float Lifetime) = 0;
		virtual void RenderLine(const FVector& A, const FVector& B, const FColor& Color, float LineThickness, float Lifetime) = 0;
		virtual void RenderArrow(const FVector& A, const FVector& B, float ArrowSize, const FColor& Color, float LineThickness, float Lifetime) = 0;
		// The circle lies in the plane of Axes' Y and Z axes (X is the normal).
		virtual void RenderCircle(const FVector& Center, const FMatrix& Axes, float Radius, const FColor& Color, float LineThickness, float Lifetime) = 0;
		virtual void RenderSphere(const FVector& Center, float Radius, const FColor& Color, float LineThickness, float Lifetime) = 0;
		virtual void RenderCapsule(const FVector& Center, const FQuat& Rotation, float HalfHeight, float Radius, const FColor& Color, float LineThickness,
			float Lifetime) = 0;
		virtual void RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, float LineThickness, float Lifetime) = 0;
		virtual void RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LineThickness, float Lifetime) = 0;
		virtual void RenderString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float FontScale, bool bDrawShadow, float Lifetime) = 0;

		// A legacy-API command (FLatentDrawCommand::bLatent). UE draws it with DebugDrawChaosCommand, outside the render
		// budget (ChaosDebugDrawSubsystem.cpp:206-220 [V]); the default maps it onto this renderer's Render* calls.
		virtual void RenderLatentCommand(const FLatentDrawCommand& Command);
	};

	// What the render stage did in one frame (the viewer's Stats panel).
	struct FDebugDrawRenderStats
	{
		int32 NumRendered = 0;
		int32 NumRejectedRegion = 0;
		int32 NumRejectedBudget = 0;
		int32 NumLatent = 0;       // legacy commands, drawn outside the budget
		int32 RenderCost = 0;      // UE FChaosDDRenderer::RenderCost (an accepted command is counted twice, see TryAddToCost)
		int32 RenderBudget = 0;    // 0 = infinite
		// UE: "Debug Draw Render Budget Exceeded" (ChaosDebugDrawSubsystem.cpp:508-513 [V]).
		[[nodiscard]] bool WasRenderBudgetExceeded() const { return RenderCost > RenderBudget && RenderBudget > 0; }
	};

	// FChaosDDRenderer's region test and budget in front of a real renderer. Per primitive (ChaosDebugDrawSubsystem.cpp:100-204 [V]):
	// the bounds of FDebugDrawQueue::GetCommandBounds, render cost point / line 1, arrow 3, circle 8, sphere 16, capsule 16,
	// box 12, triangle 3, string 10; IsInDrawRegion is "squared distance <= W^2"; TryAddToCost adds the cost once and, when
	// the command is accepted, a second time - mirrored, so a box takes 24 of the budget. One deliberate difference: UE's
	// render-stage IsInDrawRegion has no "W > 0" guard, so p.Chaos.DebugDraw.Radius 0 (documented as "no limit")
	// draws only commands whose bounds contain the camera; the harness treats W = 0 as everywhere in both stages
	// (Progress.md M4).
	class FDebugDrawRenderStage final : public IDebugDrawRenderer
	{
	public:
		FDebugDrawRenderStage(IDebugDrawRenderer& InTarget, const FSphere& InDrawRegion, int32 InRenderBudget);

		[[nodiscard]] const FDebugDrawRenderStats& GetStats() const { return Stats; }
		[[nodiscard]] bool IsInDrawRegion(const FBox& Bounds) const;
		// The render cost of a command type (FChaosDDRenderer::Render*).
		[[nodiscard]] static int32 GetRenderCost(const FLatentDrawCommand& Command);

		// IDebugDrawRenderer
		[[nodiscard]] bool IsServer() const override { return Target.IsServer(); }
		[[nodiscard]] FSphere GetDrawRegion() const override { return DrawRegion; }
		void RenderPoint(const FVector& Position, const FColor& Color, float PointSize, float Lifetime) override;
		void RenderLine(const FVector& A, const FVector& B, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderArrow(const FVector& A, const FVector& B, float ArrowSize, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderCircle(const FVector& Center, const FMatrix& Axes, float Radius, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderSphere(const FVector& Center, float Radius, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderCapsule(const FVector& Center, const FQuat& Rotation, float HalfHeight, float Radius, const FColor& Color, float LineThickness,
			float Lifetime) override;
		void RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LineThickness, float Lifetime) override;
		void RenderString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float FontScale, bool bDrawShadow, float Lifetime) override;
		void RenderLatentCommand(const FLatentDrawCommand& Command) override;

	private:
		bool Accept(const FBox& Bounds, int32 Cost);
		bool TryAddToCost(int32 Cost);

		IDebugDrawRenderer& Target;
		FSphere DrawRegion;
		FDebugDrawRenderStats Stats;
	};

	// UE RenderFrame order: the latent commands (RenderLatentCommand), then the others (one Render* call each), each group in
	// queue order.
	void VisitCommands(std::span<const FLatentDrawCommand> Commands, IDebugDrawRenderer& Renderer);
}
