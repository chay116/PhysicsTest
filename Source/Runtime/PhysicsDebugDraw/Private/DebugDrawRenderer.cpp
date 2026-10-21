// DebugDrawRenderer.cpp - VisitCommands and FDebugDrawRenderStage (see DebugDrawRenderer.h). The command -> primitive
// mapping follows UE's DebugDrawChaosCommand (ChaosDebugDrawComponent.cpp:78-114 [V]): a sphere's centre is LineStart, a
// point's size is Thickness, a circle's axes are FRotationMatrix::MakeFromYZ(YAxis, ZAxis). The render stage's costs and
// bounds follow FChaosDDRenderer (ChaosDebugDrawSubsystem.cpp:94-235 [V]).
#include "DebugDrawRenderer.h"

#include "DebugDrawQueue.h"

namespace PhysicsTest
{
	namespace
	{
		// The frame of a circle command (UE builds it with FRotationMatrix::MakeFromYZ, ChaosDebugDrawComponent.cpp:78-114 [V]):
		// the first in-plane axis as given (unit), the second with its component along the first removed (unit), and the
		// circle normal completing a right-handed frame. Degenerate input (a zero axis, or two parallel axes) still gives an
		// orthonormal frame: the first axis falls back to world Y and the second to a perpendicular of the first
		// (FVector::GetPerpendicularUnit), so the renderer never sees zero or NaN axes.
		FMatrix MakeCircleFrame(const FVector& FirstInPlane, const FVector& SecondInPlane)
		{
			FVector First = FirstInPlane.GetSafeNormal();
			if (First.IsZero())
			{
				First = FVector::RightVector;
			}
			const FVector SecondUnit = SecondInPlane.GetSafeNormal();
			FVector Second = (SecondUnit - First * (First | SecondUnit)).GetSafeNormal();
			if (Second.IsZero())
			{
				Second = First.GetPerpendicularUnit();
			}
			return FMatrix(First ^ Second, First, Second, FVector::ZeroVector);
		}

		// One Render* call for one command.
		void DispatchCommand(const FLatentDrawCommand& Command, IDebugDrawRenderer& Renderer)
		{
			switch (Command.Type)
			{
			case FLatentDrawCommand::EDrawType::Point:
				Renderer.RenderPoint(Command.LineStart, Command.Color, Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::Line:
				Renderer.RenderLine(Command.LineStart, Command.LineEnd, Command.Color, Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::DirectionalArrow:
				Renderer.RenderArrow(Command.LineStart, Command.LineEnd, Command.ArrowSize, Command.Color, Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::Sphere:
				Renderer.RenderSphere(Command.LineStart, static_cast<float>(Command.Radius), Command.Color, Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::Box:
				Renderer.RenderBox(Command.Center, Command.Rotation, Command.Extent, Command.Color, Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::String:
				Renderer.RenderString(Command.TextLocation, Command.Text, Command.Color, Command.FontScale, Command.bDrawShadow, Command.Duration);
				break;
			case FLatentDrawCommand::EDrawType::Circle:
				Renderer.RenderCircle(Command.Center, MakeCircleFrame(Command.YAxis, Command.ZAxis), static_cast<float>(Command.Radius), Command.Color, Command.Thickness,
					Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::Capsule:
				Renderer.RenderCapsule(Command.Center, Command.Rotation, static_cast<float>(Command.HalfHeight), static_cast<float>(Command.Radius), Command.Color,
					Command.Thickness, Command.LifeTime);
				break;
			case FLatentDrawCommand::EDrawType::Triangle:
				Renderer.RenderTriangle(Command.LineStart, Command.LineEnd, Command.TriangleC, Command.Color, Command.Thickness, Command.LifeTime);
				break;
			default:
				break;
			}
		}

		// The render costs of FChaosDDRenderer (ChaosDebugDrawSubsystem.cpp:102, 113, 124, 135, 148, 159, 173, 184, 197 [V]).
		constexpr int32 GRenderPointCost = 1;
		constexpr int32 GRenderLineCost = 1;
		constexpr int32 GRenderArrowCost = 3;
		constexpr int32 GRenderCircleCost = 8;
		constexpr int32 GRenderSphereCost = 16;
		constexpr int32 GRenderCapsuleCost = 16;
		constexpr int32 GRenderBoxCost = 12;
		constexpr int32 GRenderTriangleCost = 3;
		constexpr int32 GRenderStringCost = 10;

		FBox MinMax(const FVector& A, const FVector& B)
		{
			return FBox(FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)), FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)));
		}
	}

	void IDebugDrawRenderer::RenderLatentCommand(const FLatentDrawCommand& Command)
	{
		DispatchCommand(Command, *this);
	}

	void VisitCommands(const std::span<const FLatentDrawCommand> Commands, IDebugDrawRenderer& Renderer)
	{
		for (const FLatentDrawCommand& Command : Commands)
		{
			if (Command.bLatent)
			{
				Renderer.RenderLatentCommand(Command);
			}
		}
		for (const FLatentDrawCommand& Command : Commands)
		{
			if (!Command.bLatent)
			{
				DispatchCommand(Command, Renderer);
			}
		}
	}

	// ---- FDebugDrawRenderStage --------------------------------------------------------------------------------------------
	FDebugDrawRenderStage::FDebugDrawRenderStage(IDebugDrawRenderer& InTarget, const FSphere& InDrawRegion, const int32 InRenderBudget)
		: Target(InTarget)
		, DrawRegion(InDrawRegion)
	{
		Stats.RenderBudget = InRenderBudget;
	}

	int32 FDebugDrawRenderStage::GetRenderCost(const FLatentDrawCommand& Command)
	{
		switch (Command.Type)
		{
		case FLatentDrawCommand::EDrawType::Point: return GRenderPointCost;
		case FLatentDrawCommand::EDrawType::Line: return GRenderLineCost;
		case FLatentDrawCommand::EDrawType::DirectionalArrow: return GRenderArrowCost;
		case FLatentDrawCommand::EDrawType::Circle: return GRenderCircleCost;
		case FLatentDrawCommand::EDrawType::Sphere: return GRenderSphereCost;
		case FLatentDrawCommand::EDrawType::Capsule: return GRenderCapsuleCost;
		case FLatentDrawCommand::EDrawType::Box: return GRenderBoxCost;
		case FLatentDrawCommand::EDrawType::Triangle: return GRenderTriangleCost;
		case FLatentDrawCommand::EDrawType::String: return GRenderStringCost;
		default: return 1;
		}
	}

	bool FDebugDrawRenderStage::IsInDrawRegion(const FBox& Bounds) const
	{
		// FChaosDDRenderer::IsInDrawRegion: squared distance <= W^2 (ChaosDebugDrawSubsystem.cpp:94-98 [V]). UE has no W > 0
		// guard here; the harness keeps W = 0 = everywhere (see the header).
		if (DrawRegion.W <= 0.0)
		{
			return true;
		}
		return Bounds.ComputeSquaredDistanceToPoint(DrawRegion.Center) <= DrawRegion.W * DrawRegion.W;
	}

	bool FDebugDrawRenderStage::TryAddToCost(const int32 Cost)
	{
		// FChaosDDRenderer::TryAddToCost (ChaosDebugDrawSubsystem.cpp:223-235 [V]): the cost is added, and added again when the
		// command fits - UE behaviour, mirrored as it is (an accepted command uses twice its cost of the budget).
		Stats.RenderCost += Cost;
		if (Stats.RenderCost <= Stats.RenderBudget || Stats.RenderBudget == 0)
		{
			Stats.RenderCost += Cost;
			return true;
		}
		return false;
	}

	bool FDebugDrawRenderStage::Accept(const FBox& Bounds, const int32 Cost)
	{
		if (!IsInDrawRegion(Bounds))
		{
			++Stats.NumRejectedRegion;
			return false;
		}
		if (!TryAddToCost(Cost))
		{
			++Stats.NumRejectedBudget;
			return false;
		}
		++Stats.NumRendered;
		return true;
	}

	void FDebugDrawRenderStage::RenderPoint(const FVector& Position, const FColor& Color, const float PointSize, const float Lifetime)
	{
		if (Accept(FBox(Position, Position), GRenderPointCost))
		{
			Target.RenderPoint(Position, Color, PointSize, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderLine(const FVector& A, const FVector& B, const FColor& Color, const float LineThickness, const float Lifetime)
	{
		if (Accept(MinMax(A, B), GRenderLineCost))
		{
			Target.RenderLine(A, B, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderArrow(const FVector& A, const FVector& B, const float ArrowSize, const FColor& Color, const float LineThickness,
		const float Lifetime)
	{
		if (Accept(MinMax(A, B), GRenderArrowCost))
		{
			Target.RenderArrow(A, B, ArrowSize, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderCircle(const FVector& Center, const FMatrix& Axes, const float Radius, const FColor& Color, const float LineThickness,
		const float Lifetime)
	{
		const double R = static_cast<double>(Radius);
		if (Accept(FBox(Center - FVector(R), Center + FVector(R)), GRenderCircleCost))
		{
			Target.RenderCircle(Center, Axes, Radius, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderSphere(const FVector& Center, const float Radius, const FColor& Color, const float LineThickness, const float Lifetime)
	{
		const double R = static_cast<double>(Radius);
		if (Accept(FBox(Center - FVector(R), Center + FVector(R)), GRenderSphereCost))
		{
			Target.RenderSphere(Center, Radius, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderCapsule(const FVector& Center, const FQuat& Rotation, const float HalfHeight, const float Radius, const FColor& Color,
		const float LineThickness, const float Lifetime)
	{
		const FVector EndOffset = Rotation.GetAxisZ() * static_cast<double>(HalfHeight);
		if (Accept(MinMax(Center - EndOffset, Center + EndOffset), GRenderCapsuleCost))
		{
			Target.RenderCapsule(Center, Rotation, HalfHeight, Radius, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, const float LineThickness,
		const float Lifetime)
	{
		if (Accept(DebugDraw::GetRotatedBoxBounds(Position, Rotation, Size * 0.5), GRenderBoxCost))
		{
			Target.RenderBox(Position, Rotation, Size, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, const float LineThickness,
		const float Lifetime)
	{
		FBox Bounds = MinMax(A, B);
		Bounds += C;
		if (Accept(Bounds, GRenderTriangleCost))
		{
			Target.RenderTriangle(A, B, C, Color, LineThickness, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderString(const FVector& TextLocation, const std::string_view Text, const FColor& Color, const float FontScale,
		const bool bDrawShadow, const float Lifetime)
	{
		if (Accept(FBox(TextLocation, TextLocation), GRenderStringCost))
		{
			Target.RenderString(TextLocation, Text, Color, FontScale, bDrawShadow, Lifetime);
		}
	}

	void FDebugDrawRenderStage::RenderLatentCommand(const FLatentDrawCommand& Command)
	{
		// Legacy commands paid their cost when they were captured; UE renders them without a second region or budget test
		// (ChaosDebugDrawSubsystem.cpp:206-220 [V]).
		++Stats.NumLatent;
		Target.RenderLatentCommand(Command);
	}
}
