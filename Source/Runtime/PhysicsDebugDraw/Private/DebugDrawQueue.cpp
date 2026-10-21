// DebugDrawQueue.cpp - FDebugDrawQueue (see DebugDrawQueue.h). The harness's own code; the costs, bounds, argument order and
// the region / budget rules follow FChaosDDPrimitives, FChaosDDParticleShape and FChaosDDFrame (cited in the header).
#include "DebugDrawQueue.h"

#include <utility>

namespace PhysicsTest
{
	namespace
	{
		// Appends a command constructed in place (no temporary of the ~0.5 KB command per call).
		FLatentDrawCommand& AddCommand(std::vector<FLatentDrawCommand>& Commands, const FLatentDrawCommand::EDrawType Type, const FColor& Color,
			const bool bPersistentLines, const float LifeTime, const uint8 DepthPriority, const float Thickness)
		{
			FLatentDrawCommand& Command = Commands.emplace_back();
			Command.Type = Type;
			Command.Color = Color;
			Command.bPersistentLines = bPersistentLines;
			Command.LifeTime = LifeTime;
			Command.DepthPriority = DepthPriority;
			Command.Thickness = Thickness;
			return Command;
		}

		FBox MinMax(const FVector& A, const FVector& B)
		{
			return FBox(FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)), FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)));
		}

		FBox CapsuleSegmentBounds(const FVector& Center, const FQuat& Rotation, const double HalfHeight)
		{
			// FChaosDDPrimitives::DrawCapsule / FChaosDDRenderer::RenderCapsule: the axis segment, without the radius.
			const FVector EndOffset = Rotation.GetAxisZ() * HalfHeight;
			return MinMax(Center - EndOffset, Center + EndOffset);
		}

		// Capture costs of FChaosDDPrimitives (DebugDrawPrimitives.cpp:13, 25, 37, 49, 61, 73, 88, 100, 112 [V]).
		constexpr int32 GPointCost = 1;
		constexpr int32 GLineCost = 1;
		constexpr int32 GArrowCost = 1;
		constexpr int32 GCircleCost = 1;
		constexpr int32 GSphereCost = 64;
		constexpr int32 GCapsuleCost = 16;
		constexpr int32 GBoxCost = 12;
		constexpr int32 GTriangleCost = 3;
		constexpr int32 GStringCost = 10;
		// FChaosDDParticleShape::GetParticleCommandCost (DebugDrawParticle.cpp:283-286 [V]).
		constexpr int32 GParticleShapeCost = 1;
	}

	namespace DebugDraw
	{
		FBox GetRotatedBoxBounds(const FVector& Center, const FQuat& Rotation, const FVector& HalfExtent)
		{
			const FVector X = Rotation.GetAxisX();
			const FVector Y = Rotation.GetAxisY();
			const FVector Z = Rotation.GetAxisZ();
			const FVector Half(FMath::Abs(X.X) * HalfExtent.X + FMath::Abs(Y.X) * HalfExtent.Y + FMath::Abs(Z.X) * HalfExtent.Z,
				FMath::Abs(X.Y) * HalfExtent.X + FMath::Abs(Y.Y) * HalfExtent.Y + FMath::Abs(Z.Y) * HalfExtent.Z,
				FMath::Abs(X.Z) * HalfExtent.X + FMath::Abs(Y.Z) * HalfExtent.Y + FMath::Abs(Z.Z) * HalfExtent.Z);
			return FBox(Center - Half, Center + Half);
		}
	}

	void FDebugDrawQueue::ExtractAllElements(std::vector<FLatentDrawCommand>& OutDrawCommands, const bool /*bPaused*/)
	{
		// UE moves the buffer out and reserves the old capacity in the new one (DebugDrawQueue.h:103-111 [V]); swapping
		// keeps the caller's capacity as well, so a steady-state frame allocates nothing.
		OutDrawCommands.clear();
		std::swap(OutDrawCommands, Commands);
		Stats = FDebugDrawQueueStats();
		Stats.Budget = MaxCost;
	}

	void FDebugDrawQueue::Reset()
	{
		Commands.clear();
		Stats = FDebugDrawQueueStats();
		Stats.Budget = MaxCost;
	}

	void FDebugDrawQueue::SetRegionOfInterest(const FVector& Pos, const float InRadius)
	{
		CenterOfInterest = Pos;
		RadiusOfInterest = static_cast<double>(InRadius);
	}

	bool FDebugDrawQueue::IsInRegionOfInterest(const FVector& Pos) const
	{
		// FChaosDDFrame::IsInDrawRegion(FVector): W > 0 -> FSphere::IsInside, else everywhere (ChaosDDFrame.h:88-95 [V]).
		if (RadiusOfInterest > 0.0)
		{
			return (Pos - CenterOfInterest).SizeSquared() <= (RadiusOfInterest + UE_KINDA_SMALL_NUMBER) * (RadiusOfInterest + UE_KINDA_SMALL_NUMBER);
		}
		return true;
	}

	bool FDebugDrawQueue::IsInRegionOfInterest(const FVector& Pos, const double Radius) const
	{
		// FChaosDDFrame::IsInDrawRegion(FSphere): the spheres intersect (ChaosDDFrame.h:97-104 [V]).
		if (RadiusOfInterest > 0.0)
		{
			return (Pos - CenterOfInterest).SizeSquared() <= (RadiusOfInterest + Radius) * (RadiusOfInterest + Radius);
		}
		return true;
	}

	bool FDebugDrawQueue::IsInRegionOfInterest(const FBox& Bounds) const
	{
		// FChaosDDFrame::IsInDrawRegion(FBox): squared distance from the box to the centre < W^2 (ChaosDDFrame.h:106-114 [V]).
		if (RadiusOfInterest > 0.0 && Bounds.IsValid)
		{
			return Bounds.ComputeSquaredDistanceToPoint(CenterOfInterest) < RadiusOfInterest * RadiusOfInterest;
		}
		return true;
	}

	bool FDebugDrawQueue::AddToCost(const int32 Cost)
	{
		// FChaosDDFrame::AddToCost: every command that reaches it adds its cost, accepted or not; MaxLines 0 = no limit
		// (ChaosDDFrame.h:136-149 [V]).
		Stats.Budget = MaxCost;
		Stats.RequestedCost += Cost;
		if (MaxCost == 0 || Stats.RequestedCost <= MaxCost)
		{
			++Stats.NumAccepted;
			return true;
		}
		++Stats.NumRejectedBudget;
		return false;
	}

	bool FDebugDrawQueue::AcceptCommand(const int32 Cost, const FBox& Bounds)
	{
		// FChaosDDFrameWriter::TryEnqueueCommand: the region (the command's bounds), then the cost (ChaosDDFrame.h:377-388 [V]).
		if (!bEnabled)
		{
			return false;
		}
		if (!IsInRegionOfInterest(Bounds))
		{
			++Stats.NumRejectedRegion;
			return false;
		}
		return AddToCost(Cost);
	}

	bool FDebugDrawQueue::AcceptLegacyCommand(const int32 Cost, const FVector& Position)
	{
		// FDebugDrawQueue::AcceptCommand, new-queue branch: the position only (DebugDrawQueue.h:351-361 [V]).
		if (!bEnabled)
		{
			return false;
		}
		if (!IsInRegionOfInterest(Position))
		{
			++Stats.NumRejectedRegion;
			return false;
		}
		return AddToCost(Cost);
	}

	int32 FDebugDrawQueue::GetCommandCost(const FLatentDrawCommand& Command)
	{
		switch (Command.Type)
		{
		case FLatentDrawCommand::EDrawType::Point: return GPointCost;
		case FLatentDrawCommand::EDrawType::Line: return GLineCost;
		case FLatentDrawCommand::EDrawType::DirectionalArrow: return GArrowCost;
		case FLatentDrawCommand::EDrawType::Sphere: return GSphereCost;
		case FLatentDrawCommand::EDrawType::Box: return GBoxCost;
		case FLatentDrawCommand::EDrawType::String: return GStringCost;
		case FLatentDrawCommand::EDrawType::Circle: return GCircleCost;
		case FLatentDrawCommand::EDrawType::Capsule: return GCapsuleCost;
		case FLatentDrawCommand::EDrawType::Triangle: return GTriangleCost;
		default: return 1;
		}
	}

	FBox FDebugDrawQueue::GetCommandBounds(const FLatentDrawCommand& Command)
	{
		switch (Command.Type)
		{
		case FLatentDrawCommand::EDrawType::Point: return FBox(Command.LineStart, Command.LineStart);
		case FLatentDrawCommand::EDrawType::Line:
		case FLatentDrawCommand::EDrawType::DirectionalArrow: return MinMax(Command.LineStart, Command.LineEnd);
		case FLatentDrawCommand::EDrawType::Sphere: return FBox(Command.LineStart - FVector(Command.Radius), Command.LineStart + FVector(Command.Radius));
		case FLatentDrawCommand::EDrawType::Circle: return FBox(Command.Center - FVector(Command.Radius), Command.Center + FVector(Command.Radius));
		// FBox(-0.5 * Size, 0.5 * Size).TransformBy(FTransform(Rotation, Center)) with Size = the half extent the callers pass
		// (DebugDrawPrimitives.cpp:89, ChaosDebugDrawSubsystem.cpp:174 [V]): half of the drawn box, as in UE.
		case FLatentDrawCommand::EDrawType::Box: return DebugDraw::GetRotatedBoxBounds(Command.Center, Command.Rotation, Command.Extent * 0.5);
		case FLatentDrawCommand::EDrawType::Capsule: return CapsuleSegmentBounds(Command.Center, Command.Rotation, Command.HalfHeight);
		case FLatentDrawCommand::EDrawType::Triangle:
		{
			FBox Bounds = MinMax(Command.LineStart, Command.LineEnd);
			Bounds += Command.TriangleC;
			return Bounds;
		}
		case FLatentDrawCommand::EDrawType::String: return FBox(Command.TextLocation, Command.TextLocation);
		default: return FBox(Command.LineStart, Command.LineStart);
		}
	}

	void FDebugDrawQueue::DrawDebugPoint(const FVector& Position, const FColor& Color, const bool bPersistentLines, const float LifeTime, const uint8 DepthPriority,
		const float Thickness)
	{
		if (AcceptCommand(GPointCost, FBox(Position, Position)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Point, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.LineStart = Position;
		}
	}

	void FDebugDrawQueue::DrawDebugLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, const bool bPersistentLines, const float LifeTime,
		const uint8 DepthPriority, const float Thickness)
	{
		if (AcceptCommand(GLineCost, MinMax(LineStart, LineEnd)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Line, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.LineStart = LineStart;
			Command.LineEnd = LineEnd;
		}
	}

	void FDebugDrawQueue::DrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, const float ArrowSize, const FColor& Color,
		const bool bPersistentLines, const float LifeTime, const uint8 DepthPriority, const float Thickness)
	{
		if (AcceptCommand(GArrowCost, MinMax(LineStart, LineEnd)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::DirectionalArrow, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.LineStart = LineStart;
			Command.LineEnd = LineEnd;
			Command.ArrowSize = ArrowSize;
		}
	}

	void FDebugDrawQueue::DrawDebugCoordinateSystem(const FVector& Position, const FRotator& AxisRot, const double Scale, const bool bPersistentLines,
		const float LifeTime, const uint8 DepthPriority, const float Thickness)
	{
		// Legacy API: one acceptance for the three lines, which become latent commands (DebugDrawQueue.h:150-167, :378-385 [V]).
		if (AcceptLegacyCommand(3, Position))
		{
			const FQuat Q = AxisRot.Quaternion();
			const FVector Axes[3] = { Q.GetAxisX(), Q.GetAxisY(), Q.GetAxisZ() };
			const FColor Colors[3] = { FColor::Red, FColor::Green, FColor::Blue };
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Line, Colors[Axis], bPersistentLines, LifeTime, DepthPriority, Thickness);
				Command.LineStart = Position;
				Command.LineEnd = Position + Axes[Axis] * Scale;
				Command.bLatent = true;
			}
		}
	}

	void FDebugDrawQueue::DrawDebugSphere(const FVector& Center, const double Radius, const int32 Segments, const FColor& Color, const bool bPersistentLines,
		const float LifeTime, const uint8 DepthPriority, const float Thickness)
	{
		if (AcceptCommand(GSphereCost, FBox(Center - FVector(Radius), Center + FVector(Radius))))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Sphere, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.LineStart = Center;   // UE's latent sphere keeps the centre in LineStart (FLatentDrawCommand::DrawDebugSphere)
			Command.Center = Center;
			Command.Radius = Radius;
			Command.Segments = Segments;
		}
	}

	void FDebugDrawQueue::DrawDebugBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, const bool bPersistentLines,
		const float LifeTime, const uint8 DepthPriority, const float Thickness)
	{
		if (AcceptCommand(GBoxCost, DebugDraw::GetRotatedBoxBounds(Center, Rotation, Extent * 0.5)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Box, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.Center = Center;
			Command.Extent = Extent;
			Command.Rotation = Rotation;
		}
	}

	void FDebugDrawQueue::DrawDebugString(const FVector& TextLocation, const std::string_view Text, const FColor& Color, const float Duration,
		const bool bDrawShadow, const float FontScale)
	{
		if (AcceptCommand(GStringCost, FBox(TextLocation, TextLocation)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::String, Color, false, Duration, 0, 0.0f);
			Command.TextLocation = TextLocation;
			Command.Text.assign(Text);
			Command.bDrawShadow = bDrawShadow;
			Command.FontScale = FontScale;
			Command.Duration = Duration;
		}
	}

	void FDebugDrawQueue::DrawDebugCircle(const FVector& Center, const double Radius, const int32 Segments, const FColor& Color, const bool bPersistentLines,
		const float LifeTime, const uint8 DepthPriority, const float Thickness, const FVector& YAxis, const FVector& ZAxis, const bool bDrawAxis)
	{
		if (AcceptCommand(GCircleCost, FBox(Center - FVector(Radius), Center + FVector(Radius))))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Circle, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.Center = Center;
			Command.Radius = Radius;
			Command.Segments = Segments;
			Command.YAxis = YAxis;
			Command.ZAxis = ZAxis;
			Command.bDrawAxis = bDrawAxis;
		}
	}

	void FDebugDrawQueue::DrawDebugCapsule(const FVector& Center, const double HalfHeight, const double Radius, const FQuat& Rotation, const FColor& Color,
		const bool bPersistentLines, const float LifeTime, const uint8 DepthPriority, const float Thickness)
	{
		if (AcceptCommand(GCapsuleCost, CapsuleSegmentBounds(Center, Rotation, HalfHeight)))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Capsule, Color, bPersistentLines, LifeTime, DepthPriority, Thickness);
			Command.Center = Center;
			Command.HalfHeight = HalfHeight;
			Command.Radius = Radius;
			Command.Rotation = Rotation;
		}
	}

	void FDebugDrawQueue::DrawDebugTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, const float LifeTime, const float Thickness)
	{
		FBox Bounds = MinMax(A, B);
		Bounds += C;
		if (AcceptCommand(GTriangleCost, Bounds))
		{
			FLatentDrawCommand& Command = AddCommand(Commands, FLatentDrawCommand::EDrawType::Triangle, Color, false, LifeTime, 0, Thickness);
			Command.LineStart = A;
			Command.LineEnd = B;
			Command.TriangleC = C;
		}
	}

	void FDebugDrawQueue::DrawParticleShape(const FBox& ParticleBounds, const FLatentDrawCommand::EDrawType Type, const FVector& Center, const FQuat& Rotation,
		const FVector& HalfExtent, const double Radius, const double HalfHeight, const FColor& Color, const float Thickness, const bool bShouldRender)
	{
		// FChaosDDParticleShape::Draw: the particle's bounds, then the cost of one shape, then the command; what the shape looks
		// like and whether it is shown is decided when it is rendered (DebugDrawParticle.cpp:308-348 [V]).
		if (!AcceptCommand(GParticleShapeCost, ParticleBounds) || !bShouldRender)
		{
			return;
		}
		FLatentDrawCommand& Command = AddCommand(Commands, Type, Color, false, 0.0f, 0, Thickness);
		Command.Center = Center;
		Command.Rotation = Rotation;
		switch (Type)
		{
		case FLatentDrawCommand::EDrawType::Sphere:
			Command.LineStart = Center;
			Command.Radius = Radius;
			Command.Segments = 8;
			break;
		case FLatentDrawCommand::EDrawType::Capsule:
			Command.Radius = Radius;
			Command.HalfHeight = HalfHeight;
			break;
		default:
			Command.Extent = HalfExtent;
			break;
		}
	}

	void FDebugDrawQueue::Enqueue(const FLatentDrawCommand& Command)
	{
		if (AcceptCommand(GetCommandCost(Command), GetCommandBounds(Command)))
		{
			Commands.push_back(Command);
		}
	}
}
