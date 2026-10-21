// PhysicsDebugDraw.cpp - the drawers of PhysicsDebugDraw.h. The harness's own code; what is drawn, with which colours,
// sizes and in which order follows Chaos::DebugDraw (ChaosDebugDraw.cpp), FPBDIslandManager::DebugDrawSleepState
// (IslandManager.cpp:4192-4258) and FPBDRigidsSolver (PBDRigidsSolver.cpp:3337-3546), cited per drawer [V].
#include "PhysicsDebugDraw.h"

#include "IslandBuilder.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysScene.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>

namespace PhysicsTest::DebugDraw
{
	namespace
	{
		constexpr float GKindaSmallLifeTime = UE_KINDA_SMALL_NUMBER;   // UE passes UE_KINDA_SMALL_NUMBER as the life time

		FVector ToVector(const PtVec3& V) { return FVector(V.X, V.Y, V.Z); }
		FQuat ToQuat(const PtQuat& Q) { return FQuat(Q.X, Q.Y, Q.Z, Q.W); }
		FTransform ToTransform(const PtTransform& T) { return FTransform(ToQuat(T.Rotation), ToVector(T.Translation)); }
		FBox ToBox(const PtVec3& Min, const PtVec3& Max) { return FBox(ToVector(Min), ToVector(Max)); }
		EObjectStateType GetState(const PtParticleSnapshot& Particle) { return static_cast<EObjectStateType>(Particle.ObjectState); }
		bool IsDynamicOrSleeping(const PtParticleSnapshot& Particle)
		{
			return GetState(Particle) == EObjectStateType::Dynamic || GetState(Particle) == EObjectStateType::Sleeping;
		}

		// One shape element of a body in world space, scaled as the ABI marshalling does (PhysicsBackend.cpp MarshalBodies:
		// spheres, boxes, capsules, convexes, in that order - the order of PtContactPoint::Shape0 / Shape1).
		struct FWorldShape
		{
			enum class EType : uint8 { Sphere, Box, Capsule, Convex } Type = EType::Box;
			FTransform World;        // rigid
			FVector HalfExtent;      // box
			double Radius = 0.0;     // sphere, capsule
			double HalfLength = 0.0; // capsule segment half length (ABI HalfLength)
		};

		template <typename TVisitor>
		void ForEachShape(const FBodyInstance& Body, const FTransform& ActorTM, TVisitor&& Visitor)
		{
			if (Body.BodySetup == nullptr)
			{
				return;
			}
			const FKAggregateGeom& Geom = Body.BodySetup->AggGeom;
			const FVector& Scale = Body.Scale3D;
			// Unit scale (every Phase-1 scene): GetFinalScaled is the identity, so the element is used as is and an element
			// without rotation skips the FRotator -> FQuat conversion (10k bodies per frame).
			const bool bUnitScale = Scale == FVector::OneVector;
			const auto LocalTransform = [](const FRotator& Rotation, const FVector& Center)
			{
				return Rotation.IsNearlyZero(0.0) ? FTransform(Center) : FTransform(Rotation, Center);
			};
			int32 Index = 0;
			for (const FKSphereElem& Elem : Geom.SphereElems)
			{
				FKSphereElem Temp;
				const FKSphereElem& Scaled = bUnitScale ? Elem : (Temp = Elem.GetFinalScaled(Scale));
				FWorldShape Shape;
				Shape.Type = FWorldShape::EType::Sphere;
				Shape.World = FTransform(Scaled.Center) * ActorTM;
				Shape.Radius = Scaled.Radius;
				Visitor(Index++, Shape);
			}
			for (const FKBoxElem& Elem : Geom.BoxElems)
			{
				FKBoxElem Temp;
				const FKBoxElem& Scaled = bUnitScale ? Elem : (Temp = Elem.GetFinalScaled(Scale));
				FWorldShape Shape;
				Shape.Type = FWorldShape::EType::Box;
				Shape.World = LocalTransform(Scaled.Rotation, Scaled.Center) * ActorTM;
				Shape.HalfExtent = Scaled.GetHalfExtents();
				Visitor(Index++, Shape);
			}
			for (const FKSphylElem& Elem : Geom.SphylElems)
			{
				FKSphylElem Temp;
				const FKSphylElem& Scaled = bUnitScale ? Elem : (Temp = Elem.GetFinalScaled(Scale));
				FWorldShape Shape;
				Shape.Type = FWorldShape::EType::Capsule;
				Shape.World = LocalTransform(Scaled.Rotation, Scaled.Center) * ActorTM;
				Shape.Radius = Scaled.Radius;
				Shape.HalfLength = Scaled.Length * 0.5;
				Visitor(Index++, Shape);
			}
			for (size_t Convex = 0; Convex < Geom.ConvexElems.size(); ++Convex)
			{
				FWorldShape Shape;
				Shape.Type = FWorldShape::EType::Convex;
				Shape.World = ActorTM;
				Visitor(Index++, Shape);
			}
		}

		FBox GetWorldBounds(const FWorldShape& Shape)
		{
			const FVector Center = Shape.World.GetTranslation();
			FVector Half;
			switch (Shape.Type)
			{
			case FWorldShape::EType::Sphere:
				Half = FVector(Shape.Radius);
				break;
			case FWorldShape::EType::Box:
			{
				const FVector X = Shape.World.Rotation.GetAxisX();
				const FVector Y = Shape.World.Rotation.GetAxisY();
				const FVector Z = Shape.World.Rotation.GetAxisZ();
				Half = FVector(FMath::Abs(X.X) * Shape.HalfExtent.X + FMath::Abs(Y.X) * Shape.HalfExtent.Y + FMath::Abs(Z.X) * Shape.HalfExtent.Z,
					FMath::Abs(X.Y) * Shape.HalfExtent.X + FMath::Abs(Y.Y) * Shape.HalfExtent.Y + FMath::Abs(Z.Y) * Shape.HalfExtent.Z,
					FMath::Abs(X.Z) * Shape.HalfExtent.X + FMath::Abs(Y.Z) * Shape.HalfExtent.Y + FMath::Abs(Z.Z) * Shape.HalfExtent.Z);
				break;
			}
			case FWorldShape::EType::Capsule:
			{
				const FVector Axis = Shape.World.Rotation.GetAxisZ() * Shape.HalfLength;
				Half = FVector(FMath::Abs(Axis.X) + Shape.Radius, FMath::Abs(Axis.Y) + Shape.Radius, FMath::Abs(Axis.Z) + Shape.Radius);
				break;
			}
			default:
				return FBox();
			}
			return FBox(Center - Half, Center + Half);
		}

		const FBodyInstance* GetBody(const FDrawContext& Context, const PtParticleSnapshot& Particle)
		{
			return Context.Scene.GetBackendScene().GetBody(Particle.BodyIndex);
		}

		const PtParticleSnapshot* FindParticle(const FDrawContext& Context, const uint32 BodyIndex)
		{
			if (BodyIndex < Context.ParticleOfBody.size() && Context.ParticleOfBody[BodyIndex] != INDEX_NONE)
			{
				return &Context.Snapshot.Particles[static_cast<size_t>(Context.ParticleOfBody[BodyIndex])];
			}
			return nullptr;
		}

		// ChaosDebugDraw.cpp ShouldIgnoreParticle: p.Chaos.Solver.DebugDraw.ShowStatics / ShowKinematics / ShowDynamics.
		bool ShouldIgnoreParticle(const FDrawContext& Context, const PtParticleSnapshot& Particle)
		{
			switch (GetState(Particle))
			{
			case EObjectStateType::Static: return !Context.Flags.bShowStatics;
			case EObjectStateType::Kinematic: return !Context.Flags.bShowKinematics;
			case EObjectStateType::Dynamic:
			case EObjectStateType::Sleeping: return !Context.Flags.bShowDynamics;
			default: return true;
			}
		}

		// DrawShapesImpl for the simple types (ChaosDebugDraw.cpp:1067-1094 [V]), the path of DebugDraw::DrawParticleShapes
		// (sleep state) and DrawCollidingShapes: sphere 8 segments, box half extent, capsule HalfHeight = HalfLength + Radius;
		// all with ShapeThicknesScale * LineThickness and the DrawPriority; each element culled by its own world bounds first,
		// then queued through the FChaosDDPrimitives wrappers. ShapeIndex >= 0 draws that element only. Returns the convex
		// elements it skipped.
		int32 DrawBodyShapes(FDebugDrawQueue& Queue, const FDrawContext& Context, const PtParticleSnapshot& Particle, const FColor& Color, const int32 ShapeIndex = -1)
		{
			const FBodyInstance* Body = GetBody(Context, Particle);
			if (Body == nullptr)
			{
				return 0;
			}
			const FDebugDrawSettings& Settings = Context.Settings;
			const float Thickness = Settings.ShapeThicknesScale * Settings.LineThickness;
			int32 SkippedConvex = 0;
			ForEachShape(*Body, ToTransform(Particle.ActorTransform), [&](const int32 Index, const FWorldShape& Shape)
			{
				if (ShapeIndex >= 0 && Index != ShapeIndex)
				{
					return;
				}
				if (Shape.Type == FWorldShape::EType::Convex)
				{
					++SkippedConvex;
					return;
				}
				// The element's world bounds against the draw region (DrawShapesImpl, ChaosDebugDraw.cpp:909-913 [V]).
				if (!Queue.IsInRegionOfInterest(GetWorldBounds(Shape)))
				{
					Queue.NoteRegionRejection();
					return;
				}
				const FVector P = Shape.World.GetTranslation();
				switch (Shape.Type)
				{
				case FWorldShape::EType::Sphere:
					Queue.DrawDebugSphere(P, Shape.Radius, 8, Color, false, 0.0f, Settings.DrawPriority, Thickness);
					break;
				case FWorldShape::EType::Box:
					Queue.DrawDebugBox(P, Shape.HalfExtent, Shape.World.Rotation, Color, false, 0.0f, Settings.DrawPriority, Thickness);
					break;
				case FWorldShape::EType::Capsule:
					Queue.DrawDebugCapsule(P, Shape.HalfLength + Shape.Radius, Shape.Radius, Shape.World.Rotation, Color, false, 0.0f, Settings.DrawPriority, Thickness);
					break;
				default:
					break;
				}
			});
			return SkippedConvex;
		}

		// The particle views of the solver drawers: statics, kinematics, then dynamics (sleeping included), each in body order.
		template <typename TVisitor>
		void ForEachParticleByMobility(const FPhysicsDebugSnapshot& Snapshot, const bool bStatics, const bool bKinematics, const bool bDynamics, TVisitor&& Visitor)
		{
			const auto Pass = [&](const auto Predicate)
			{
				for (size_t Slot = 0; Slot < Snapshot.Particles.size(); ++Slot)
				{
					if (Predicate(Snapshot.Particles[Slot]))
					{
						Visitor(Slot, Snapshot.Particles[Slot]);
					}
				}
			};
			if (bStatics)
			{
				Pass([](const PtParticleSnapshot& P) { return GetState(P) == EObjectStateType::Static; });
			}
			if (bKinematics)
			{
				Pass([](const PtParticleSnapshot& P) { return GetState(P) == EObjectStateType::Kinematic; });
			}
			if (bDynamics)
			{
				Pass([](const PtParticleSnapshot& P) { return IsDynamicOrSleeping(P); });
			}
		}

		// Chaos::Utilities::BoxFromInertia without the COM-offset recovery (Utilities.h:560-600 [V]): the box whose inertia
		// about its centre is Inertia (principal moments, kg cm^2) for Mass. False when no such box exists.
		bool BoxFromInertia(const FVector& Inertia, const double Mass, FVector& OutSize)
		{
			if (Mass <= 0.0)
			{
				return false;
			}
			// A solid box of edges (a, b, c): I = m / 12 * (b^2 + c^2, c^2 + a^2, a^2 + b^2); solved for the squared edges.
			const FVector Scaled = Inertia * (12.0 / Mass);
			const double HalfSum = 0.5 * (Scaled.X + Scaled.Y + Scaled.Z);
			const FVector SquaredEdges(HalfSum - Scaled.X, HalfSum - Scaled.Y, HalfSum - Scaled.Z);
			if (SquaredEdges.X < 0.0 || SquaredEdges.Y < 0.0 || SquaredEdges.Z < 0.0)
			{
				return false;
			}
			OutSize = FVector(std::sqrt(SquaredEdges.X), std::sqrt(SquaredEdges.Y), std::sqrt(SquaredEdges.Z));
			return true;
		}

		// The colours of one contact point in DrawCollisionImpl (ChaosDebugDraw.cpp:1468-1500 [V]): red when active, yellow-green
		// inside the static friction cone, grey (and a 0.75 x normal) when inactive.
		struct FContactStyle
		{
			FColor Disc = FColor(250, 0, 0);
			FColor Normal = FColor(250, 0, 0);
			double NormalLengthScale = 1.0;
		};
		FContactStyle GetContactStyle(const uint32 Flags, const bool bIsActive)
		{
			FContactStyle Style;
			if (!bIsActive)
			{
				const FColor Grey(150, 150, 150);
				Style.Disc = Grey;
				Style.Normal = Grey;
				Style.NormalLengthScale = 0.75;
			}
			else if ((Flags & PT_CONTACT_STATIC_FRICTION) != 0)
			{
				Style.Disc = FColor(150, 200, 0);
			}
			return Style;
		}

		FLatentDrawCommand ToLatentCommand(const PtDebugDrawCmd& Native, const std::string& StringPool)
		{
			FLatentDrawCommand Command;
			Command.Color = FColor::FromPackedARGB(Native.ColorBGRA);
			Command.Thickness = Native.Thickness;
			Command.LifeTime = Native.Lifetime;
			Command.DepthPriority = Native.DepthPriority;
			const FVector A = ToVector(Native.A);
			const FVector B = ToVector(Native.B);
			const FQuat Rotation = ToQuat(Native.Rotation);
			switch (Native.Type)
			{
			case PT_DD_POINT:
				Command.Type = FLatentDrawCommand::EDrawType::Point;
				Command.LineStart = A;
				Command.Thickness = Native.Size;   // the point size travels in Thickness (DrawDebugPoint)
				break;
			case PT_DD_LINE:
				Command.Type = FLatentDrawCommand::EDrawType::Line;
				Command.LineStart = A;
				Command.LineEnd = B;
				break;
			case PT_DD_ARROW:
				Command.Type = FLatentDrawCommand::EDrawType::DirectionalArrow;
				Command.LineStart = A;
				Command.LineEnd = B;
				Command.ArrowSize = Native.Size;
				break;
			case PT_DD_CIRCLE:
			{
				Command.Type = FLatentDrawCommand::EDrawType::Circle;
				Command.Center = A;
				Command.Radius = Native.Size;
				Command.Segments = 8;
				Command.YAxis = Rotation.GetAxisY();
				Command.ZAxis = Rotation.GetAxisZ();
				break;
			}
			case PT_DD_SPHERE:
				Command.Type = FLatentDrawCommand::EDrawType::Sphere;
				Command.LineStart = A;
				Command.Center = A;
				Command.Radius = Native.Size;
				Command.Segments = 8;
				break;
			case PT_DD_CAPSULE:
				Command.Type = FLatentDrawCommand::EDrawType::Capsule;
				Command.Center = A;
				Command.Rotation = Rotation;
				Command.Radius = Native.Extent.X;
				Command.HalfHeight = Native.Extent.Y;   // includes the radius (PtDebugDrawCmd::Extent)
				break;
			case PT_DD_BOX:
				Command.Type = FLatentDrawCommand::EDrawType::Box;
				Command.Center = A;
				Command.Rotation = Rotation;
				Command.Extent = ToVector(Native.Extent);   // half extent
				break;
			case PT_DD_TRIANGLE:
				Command.Type = FLatentDrawCommand::EDrawType::Triangle;
				Command.LineStart = A;
				Command.LineEnd = B;
				Command.TriangleC = ToVector(Native.C);
				break;
			case PT_DD_STRING:
				Command.Type = FLatentDrawCommand::EDrawType::String;
				Command.TextLocation = A;
				if (static_cast<size_t>(Native.TextOffset) + Native.TextLength <= StringPool.size())
				{
					Command.Text.assign(StringPool, Native.TextOffset, Native.TextLength);
				}
				Command.bDrawShadow = Native.bDrawShadow != 0;
				Command.FontScale = Native.Size > 0.0f ? Native.Size : 1.0f;
				Command.Duration = Native.Lifetime;
				break;
			default:
				Command.Type = FLatentDrawCommand::EDrawType::Point;
				Command.LineStart = A;
				break;
			}
			return Command;
		}
	}

	FColor GetShapeColorByState(const EObjectStateType State)
	{
		switch (State)
		{
		case EObjectStateType::Dynamic: return FColor(255, 255, 0);
		case EObjectStateType::Sleeping: return FColor(128, 128, 128);
		case EObjectStateType::Kinematic: return FColor(0, 128, 255);
		case EObjectStateType::Static: return FColor(255, 0, 0);
		default: return FColor::Purple;
		}
	}

	FColor GetBoundsColorByState(const EObjectStateType State)
	{
		switch (State)
		{
		case EObjectStateType::Dynamic: return FColor(128, 128, 0);
		case EObjectStateType::Sleeping: return FColor(64, 64, 64);
		case EObjectStateType::Kinematic: return FColor(0, 64, 128);
		case EObjectStateType::Static: return FColor(128, 0, 0);
		default: return FColor::Purple;
		}
	}

	FColor GetIndexColor(const int32 Index)
	{
		static const FColor Colors[] = { FColor::Red, FColor::Orange, FColor::Yellow, FColor::Green, FColor::Emerald, FColor::Cyan, FColor::Turquoise,
			FColor::Blue, FColor::Magenta, FColor::Purple };
		constexpr int32 NumColors = static_cast<int32>(sizeof(Colors) / sizeof(Colors[0]));
		return Colors[((Index % NumColors) + NumColors) % NumColors];
	}

	FColor GetIslandColor(const int32 IslandIndex, const bool bIsAwake)
	{
		if (IslandIndex == INDEX_NONE)
		{
			return FColor::White;
		}
		if (!bIsAwake)
		{
			return FColor::Black;
		}
		return GetIndexColor(IslandIndex);
	}

	void BuildParticleLookup(const FPhysicsDebugSnapshot& Snapshot, std::vector<int32>& OutParticleOfBody)
	{
		// Sized from the snapshot's body-index bound, never from a reported index (M5): a particle outside it (PT_INDEX_NONE,
		// a garbage index) has no slot.
		const uint32 NumBodies = GetBodyIndexBound(Snapshot);
		OutParticleOfBody.assign(NumBodies, INDEX_NONE);
		for (size_t Slot = 0; Slot < Snapshot.Particles.size(); ++Slot)
		{
			const uint32 BodyIndex = Snapshot.Particles[Slot].BodyIndex;
			if (BodyIndex < NumBodies)
			{
				OutParticleOfBody[BodyIndex] = static_cast<int32>(Slot);
			}
		}
	}

	void DrawParticleShapes(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// The 5.8.3 path of p.Chaos.Solver.DebugDrawShapes (bChaosDebugDraw_UseNewQueue): FChaosDDParticle::DrawShapes for every
		// non-disabled particle (PBDRigidsSolver.cpp:3445-3453 [V]) - per shape element one command of cost 1, culled by the
		// particle's bounds (FChaosDDParticleShape::Draw, DebugDrawParticle.cpp:288-349 [V]). At render time UE picks the
		// colour by state (ShapesColorsPerState, the default table) and the line thickness LineThickness (no
		// ShapeThicknesScale), and FChaosDDParticleShape::ShouldRender hides Static / Kinematic / Dynamic particles by the
		// Show* CVars - Sleeping is never hidden there (DebugDrawParticle.cpp:146-161, 271-276 [V]). Statics, kinematics,
		// dynamics in body order.
		const FDebugDrawFlags& Flags = Context.Flags;
		const float Thickness = Context.Settings.LineThickness;
		ForEachParticleByMobility(Context.Snapshot, true, true, true, [&](size_t, const PtParticleSnapshot& Particle)
		{
			const FBodyInstance* Body = GetBody(Context, Particle);
			if (Body == nullptr)
			{
				return;
			}
			const EObjectStateType State = GetState(Particle);
			const bool bShouldRender = !((State == EObjectStateType::Static && !Flags.bShowStatics) || (State == EObjectStateType::Kinematic && !Flags.bShowKinematics)
				|| (State == EObjectStateType::Dynamic && !Flags.bShowDynamics));
			const FColor Color = GetShapeColorByState(State);
			const FBox ParticleBounds = ToBox(Particle.BoundsMin, Particle.BoundsMax);
			ForEachShape(*Body, ToTransform(Particle.ActorTransform), [&](int32, const FWorldShape& Shape)
			{
				const FVector P = Shape.World.GetTranslation();
				switch (Shape.Type)
				{
				case FWorldShape::EType::Sphere:
					Queue.DrawParticleShape(ParticleBounds, FLatentDrawCommand::EDrawType::Sphere, P, Shape.World.Rotation, FVector::ZeroVector, Shape.Radius, 0.0, Color,
						Thickness, bShouldRender);
					break;
				case FWorldShape::EType::Box:
					Queue.DrawParticleShape(ParticleBounds, FLatentDrawCommand::EDrawType::Box, P, Shape.World.Rotation, Shape.HalfExtent, 0.0, 0.0, Color, Thickness,
						bShouldRender);
					break;
				case FWorldShape::EType::Capsule:
					Queue.DrawParticleShape(ParticleBounds, FLatentDrawCommand::EDrawType::Capsule, P, Shape.World.Rotation, FVector::ZeroVector, Shape.Radius,
						Shape.HalfLength + Shape.Radius, Color, Thickness, bShouldRender);
					break;
				default:
					break;   // convex: not drawn in M4 (FDrawStats::NumSkippedConvexElements)
				}
			});
		});
	}

	void DrawCollisions(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawCollisionImpl (ChaosDebugDraw.cpp:1423-1622 [V]). The plane owner is Body1: the plane disc (ContactWidth) sits on
		// PointOn1, the point disc (half size) on PointOn0, the normal (ContactLen) starts at the plane location. Active points
		// are red, points in the static friction cone yellow-green, inactive ones grey with 0.75 x the normal length.
		const FDebugDrawSettings& Settings = Context.Settings;
		if (Settings.ContactWidth <= 0.0f && Settings.ContactLen <= 0.0f && Settings.ContactInfoWidth <= 0.0f && Settings.ImpulseScale <= 0.0f)
		{
			return;
		}
		const uint8 Priority = Settings.DrawPriority;
		for (const PtContactPoint& Contact : Context.Snapshot.Contacts)
		{
			const PtParticleSnapshot* Particle0 = FindParticle(Context, Contact.Body0);
			const PtParticleSnapshot* Particle1 = FindParticle(Context, Contact.Body1);
			if ((Particle0 != nullptr && !Queue.IsInRegionOfInterest(ToBox(Particle0->BoundsMin, Particle0->BoundsMax)))
				|| (Particle1 != nullptr && !Queue.IsInRegionOfInterest(ToBox(Particle1->BoundsMin, Particle1->BoundsMax))))
			{
				Queue.NoteRegionRejection();
				continue;
			}
			const bool bIsActive = (Contact.Flags & (PT_CONTACT_ACTIVE | PT_CONTACT_ACTIVE_APPROX)) != 0;
			if (!bIsActive && !Context.Flags.bShowInactiveContacts)
			{
				continue;
			}
			const FContactStyle Style = GetContactStyle(Contact.Flags, bIsActive);
			const FVector OnPlane = ToVector(Contact.PointOn1);   // Body1 owns the plane
			const FVector OnPoint = ToVector(Contact.PointOn0);
			const FVector N = ToVector(Contact.Normal);
			const FMatrix DiscFrame = FMatrix::MakeFromX(N);       // the discs lie in its Y-Z plane, normal to N
			const double Scale = Settings.DrawScale;
			if (Settings.ImpulseScale > 0.0f && !std::isnan(Contact.NormalImpulse) && Contact.NormalImpulse != 0.0)
			{
				// From the point's projection onto the contact plane, along the normal (blue).
				const FVector Foot = OnPoint - N * ((OnPoint - OnPlane) | N);
				Queue.DrawDebugLine(Foot, Foot + N * (Scale * Settings.ImpulseScale * Contact.NormalImpulse), FColor(0, 0, 250), false, 0.0f, Priority,
					Settings.LineThickness);
			}
			if (Settings.ContactWidth > 0.0f)
			{
				const double Radius = Scale * Settings.ContactWidth;
				Queue.DrawDebugCircle(OnPlane, Radius, 12, Style.Disc, false, 0.0f, Priority, Settings.LineThickness, DiscFrame.GetUnitAxis(EAxis::Y),
					DiscFrame.GetUnitAxis(EAxis::Z), false);
				Queue.DrawDebugCircle(OnPoint, 0.5 * Radius, 12, Style.Disc, false, 0.0f, Priority, Settings.LineThickness, DiscFrame.GetUnitAxis(EAxis::Y),
					DiscFrame.GetUnitAxis(EAxis::Z), false);
			}
			if (Settings.ContactLen > 0.0f)
			{
				Queue.DrawDebugLine(OnPlane, OnPlane + N * (Scale * Settings.ContactLen * Style.NormalLengthScale), Style.Normal, false, 0.0f, Priority,
					Settings.LineThickness);
			}
			// Sleeping contact: a flat black box on the plane, 1.1 x ContactInfoWidth (ChaosDebugDraw.cpp:1567-1576 [V]).
			if (Settings.ContactInfoWidth > 0.0f && (Contact.Flags & PT_CONTACT_SLEEPING) != 0)
			{
				const double Side = Scale * Settings.ContactInfoWidth * 1.1;
				Queue.DrawDebugBox(OnPlane, FVector(Side, Side, 0.01), FMatrix::MakeFromZ(N).ToQuat(), FColor::Black, false, 0.0f, Priority, 0.5f * Settings.LineThickness);
			}
		}
	}

	void DrawParticleMass(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawParticleMassImpl (ChaosDebugDraw.cpp:1744-1777 [V]): "<m> kg" at the COM (g below 1 kg, t above 1000 kg, one
		// decimal) of every rigid particle. Every non-static UE body is an FPBDRigidParticle (ChaosEngineInterface.cpp:2425-2432
		// [V]), so the solver draws the kinematic view first, then the dynamic one (PBDRigidsSolver.cpp:3478-3482 [V]); the
		// text is Yellow when IsDynamic() (Dynamic or Sleeping, ParticleHandle.h:1066 [V]), else Cyan.
		ForEachParticleByMobility(Context.Snapshot, false, true, true, [&](size_t, const PtParticleSnapshot& Particle)
		{
			if (ShouldIgnoreParticle(Context, Particle))
			{
				return;
			}
			const FColor Color = IsDynamicOrSleeping(Particle) ? FColor::Yellow : FColor::Cyan;
			const FBodyInstance* Body = GetBody(Context, Particle);
			const double Mass = Body != nullptr ? Body->MassProperties.Mass : 0.0;
			if (Mass <= 0.0)
			{
				return;
			}
			const char* Unit = "kg";
			double Multiplier = 1.0;
			if (Mass < 1.0)
			{
				Unit = "g";
				Multiplier = 1000.0;
			}
			else if (Mass > 1000.0)
			{
				Unit = "t";
				Multiplier = 0.001;
			}
			const double Rounded = 0.1 * std::round(Mass * Multiplier * 10.0);
			char Text[64];
			std::snprintf(Text, sizeof(Text), "%.1f %s", Rounded, Unit);
			Queue.DrawDebugString(ToVector(Particle.CenterOfMassWorld.Translation), Text, Color, GKindaSmallLifeTime, false, Context.Settings.FontScale);
		});
	}

	void DrawParticleBounds(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawParticleBoundsImpl (ChaosDebugDraw.cpp:1298-1336 [V]): the particle's world bounds in the darker state colour, then
		// each simple shape's world bounds. Statics, kinematics, dynamics.
		const FDebugDrawSettings& Settings = Context.Settings;
		ForEachParticleByMobility(Context.Snapshot, true, true, true, [&](size_t, const PtParticleSnapshot& Particle)
		{
			if (ShouldIgnoreParticle(Context, Particle))
			{
				return;
			}
			const FColor Color = GetBoundsColorByState(GetState(Particle));
			const FBox Bounds = ToBox(Particle.BoundsMin, Particle.BoundsMax);
			Queue.DrawDebugBox(Bounds.GetCenter(), Bounds.GetExtent(), FQuat::Identity, Color, false, GKindaSmallLifeTime, Settings.DrawPriority, Settings.LineThickness);
			if (const FBodyInstance* Body = GetBody(Context, Particle))
			{
				ForEachShape(*Body, ToTransform(Particle.ActorTransform), [&](int32, const FWorldShape& Shape)
				{
					const FBox ShapeBounds = GetWorldBounds(Shape);
					if (ShapeBounds.IsValid)
					{
						Queue.DrawDebugBox(ShapeBounds.GetCenter(), ShapeBounds.GetExtent(), FQuat::Identity, Color, false, GKindaSmallLifeTime, Settings.DrawPriority,
							Settings.LineThickness);
					}
				});
			}
		});
	}

	void DrawParticleTransforms(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawParticleTransformImpl (ChaosDebugDraw.cpp:1338-1421 [V]): red / green / blue COM axes (BodyAxisLen, ArrowSize), a
		// black point at the actor origin (PointSize), a grey COM-origin line; velocity lines when VelScale / AngVelScale > 0;
		// for dynamic particles the magenta box of equal inertia (InertiaScale). All particles, in body order.
		const FDebugDrawSettings& Settings = Context.Settings;
		const uint8 Priority = Settings.DrawPriority;
		for (const PtParticleSnapshot& Particle : Context.Snapshot.Particles)
		{
			if (ShouldIgnoreParticle(Context, Particle))
			{
				continue;
			}
			const FVector Com = ToVector(Particle.CenterOfMassWorld.Translation);
			const FQuat ComFrame = ToQuat(Particle.CenterOfMassWorld.Rotation);
			const double AxisLength = Settings.DrawScale * Settings.BodyAxisLen;
			const float HeadSize = Settings.DrawScale * Settings.ArrowSize;
			const FColor AxisColors[3] = { FColor::Red, FColor::Green, FColor::Blue };
			const FVector Axes[3] = { ComFrame.GetAxisX(), ComFrame.GetAxisY(), ComFrame.GetAxisZ() };
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Queue.DrawDebugDirectionalArrow(Com, Com + Axes[Axis] * AxisLength, HeadSize, AxisColors[Axis], false, GKindaSmallLifeTime, Priority, Settings.LineThickness);
			}
			const FVector Origin = ToVector(Particle.ActorTransform.Translation);
			Queue.DrawDebugPoint(Origin, FColor::Black, false, GKindaSmallLifeTime, Priority, Settings.DrawScale * Settings.PointSize);
			Queue.DrawDebugLine(Com, Origin, FColor(64, 64, 64), false, GKindaSmallLifeTime, Priority, Settings.LineThickness);
			const FVector Linear = ToVector(Particle.LinearVelocity);
			const FVector Angular = ToVector(Particle.AngularVelocity);
			if (Settings.VelScale > 0.0f && Linear.Size() > UE_KINDA_SMALL_NUMBER)
			{
				Queue.DrawDebugLine(Com, Com + Linear * Settings.VelScale, FColor::Red, false, GKindaSmallLifeTime, Priority, Settings.LineThickness);
			}
			if (Settings.AngVelScale > 0.0f && Angular.Size() > UE_KINDA_SMALL_NUMBER)
			{
				Queue.DrawDebugLine(Com, Com + Angular * Settings.AngVelScale, FColor::Green, false, GKindaSmallLifeTime, Priority, Settings.LineThickness);
			}
			if (Settings.InertiaScale > 0.0f && IsDynamicOrSleeping(Particle))
			{
				// The box of the harness inertia (D5: the inertia Chaos would use) in the harness principal frame: actor rotation x
				// FBodyInstance::MassProperties.RotationOfMass. Not the snapshot's COM rotation: an engine may order its principal
				// axes differently (Jolt sorts the moments, docs/Abi.md 5.5), which would permute the box's axes.
				const FBodyInstance* Body = GetBody(Context, Particle);
				FVector Size;
				if (Body != nullptr && BoxFromInertia(Body->MassProperties.InertiaTensor, Body->MassProperties.Mass, Size))
				{
					const FQuat PrincipalFrame = (ToQuat(Particle.ActorTransform.Rotation) * Body->MassProperties.RotationOfMass).GetNormalized();
					Queue.DrawDebugBox(Com, Size * (0.5 * Settings.InertiaScale), PrincipalFrame, FColor::Magenta, false, 0.0f, 0, Settings.LineThickness);
				}
			}
		}
	}

	void DrawConstraintGraph(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawConstraintGraphImpl, island boxes (ChaosDebugDraw.cpp:2017-2033 [V]): the bounds of each island's particles,
		// GetIslandColor(island index, awake) - Black when asleep - with 3 x LineThickness. The island index is the position
		// in the id-sorted list of FIslandBuilder islands.
		std::vector<FHarnessIsland> Gathered;
		if (Context.Islands == nullptr)
		{
			FIslandBuilder::GatherIslands(Context.Snapshot, Gathered);
		}
		const std::vector<FHarnessIsland>& Islands = Context.Islands != nullptr ? *Context.Islands : Gathered;
		const FDebugDrawSettings& Settings = Context.Settings;
		for (size_t IslandIndex = 0; IslandIndex < Islands.size(); ++IslandIndex)
		{
			const FHarnessIsland& Island = Islands[IslandIndex];
			if (!Island.Bounds.IsValid)
			{
				continue;
			}
			const FColor Color = GetIslandColor(static_cast<int32>(IslandIndex), Island.bAwake);
			Queue.DrawDebugBox(Island.Bounds.GetCenter(), Island.Bounds.GetExtent(), FQuat::Identity, Color, false, GKindaSmallLifeTime, Settings.DrawPriority,
				3.0f * Settings.LineThickness);
		}
	}

	void DrawSleepState(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// FPBDIslandManager::DebugDrawSleepState (IslandManager.cpp:4192-4258 [V]). Isolated awake dynamic particles: grey, or
		// red / green when they are above / below their sleep thresholds; isolated sleeping ones are not drawn. Particles of a
		// sleeping island: Black. Particles of an awake island: Black when asleep, else red / green against the thresholds,
		// grey without thresholds. The thresholds are the body material's (cm/s, rad/s) times the body's sleep family
		// multiplier; none when the profile turns sleeping off. The current velocity stands in for Chaos' smoothed velocity
		// (VSmooth / WSmooth).
		const FPhysicsDebugSnapshot& Snapshot = Context.Snapshot;
		const bool bHasIslands = Snapshot.HarnessIslandIndex.size() == Snapshot.Particles.size();
		std::vector<FHarnessIsland> Gathered;
		if (Context.Islands == nullptr)
		{
			FIslandBuilder::GatherIslands(Snapshot, Gathered);
		}
		const std::vector<FHarnessIsland>& Islands = Context.Islands != nullptr ? *Context.Islands : Gathered;
		const bool bSleepingAllowed = Context.Scene.GetTestSettings().bAllowSleeping;
		const auto GetThresholds = [&](const PtParticleSnapshot& Particle, double& OutLinear, double& OutAngular)
		{
			OutLinear = 0.0;
			OutAngular = 0.0;
			const FBodyInstance* Body = GetBody(Context, Particle);
			if (!bSleepingAllowed || Body == nullptr)
			{
				return;
			}
			const FPhysicalMaterial* Material = Body->GetSimplePhysicalMaterial();
			if (Material == nullptr)
			{
				return;
			}
			const double Multiplier = static_cast<double>(Body->GetSleepThresholdMultiplier());
			OutLinear = static_cast<double>(Material->SleepLinearVelocityThreshold) * Multiplier;
			OutAngular = static_cast<double>(Material->SleepAngularVelocityThreshold) * Multiplier;
		};
		const auto ThresholdColor = [&](const PtParticleSnapshot& Particle)
		{
			double Linear = 0.0;
			double Angular = 0.0;
			GetThresholds(Particle, Linear, Angular);
			if (Linear <= 0.0 && Angular <= 0.0)
			{
				return FColor(128, 128, 128);
			}
			const bool bPreventingSleep = ToVector(Particle.LinearVelocity).SizeSquared() > Linear * Linear
				|| ToVector(Particle.AngularVelocity).SizeSquared() > Angular * Angular;
			return bPreventingSleep ? FColor::Red : FColor::Green;
		};
		// Isolated particles first, then the islands (in id order).
		for (size_t Slot = 0; Slot < Snapshot.Particles.size(); ++Slot)
		{
			const PtParticleSnapshot& Particle = Snapshot.Particles[Slot];
			const bool bIsolated = !bHasIslands || Snapshot.HarnessIslandIndex[Slot] == INDEX_NONE;
			// UE's isolated loop walks GetActiveDynamicMovingKinematicParticlesView (IslandManager.cpp:4195 [V]) = the active
			// particles + moving kinematics (PBDRigidsSOAs.cpp:50-57 [V]); a particle put to sleep leaves the active array
			// (DeactivateParticle, PBDRigidsSOAs.h:722-749; isolated particles sleep through DeactivateParticles,
			// IslandManager.cpp:2712 [V]). So an isolated sleeping particle is not drawn at all; an awake one is grey / red / green.
			if (bIsolated && GetState(Particle) == EObjectStateType::Dynamic)
			{
				DrawBodyShapes(Queue, Context, Particle, ThresholdColor(Particle));
			}
		}
		if (!bHasIslands)
		{
			return;
		}
		// Island members in island-id order, each island's particles in body order (one sort instead of a scan per island).
		std::vector<size_t> Members;
		for (size_t Slot = 0; Slot < Snapshot.Particles.size(); ++Slot)
		{
			if (Snapshot.HarnessIslandIndex[Slot] != INDEX_NONE)
			{
				Members.push_back(Slot);
			}
		}
		std::stable_sort(Members.begin(), Members.end(), [&](const size_t A, const size_t B) { return Snapshot.HarnessIslandIndex[A] < Snapshot.HarnessIslandIndex[B]; });
		size_t IslandCursor = 0;
		for (const size_t Slot : Members)
		{
			const int32 Id = Snapshot.HarnessIslandIndex[Slot];
			while (IslandCursor < Islands.size() && Islands[IslandCursor].Id < Id)
			{
				++IslandCursor;
			}
			const bool bIslandAwake = IslandCursor < Islands.size() && Islands[IslandCursor].Id == Id && Islands[IslandCursor].bAwake;
			const PtParticleSnapshot& Particle = Snapshot.Particles[Slot];
			FColor Color = FColor::Black;
			if (bIslandAwake)
			{
				double Linear = 0.0;
				double Angular = 0.0;
				GetThresholds(Particle, Linear, Angular);
				if (Linear > 0.0 || Angular > 0.0)
				{
					Color = GetState(Particle) == EObjectStateType::Sleeping ? FColor::Black : ThresholdColor(Particle);
				}
				else
				{
					Color = FColor(128, 128, 128);
				}
			}
			DrawBodyShapes(Queue, Context, Particle, Color);
		}
	}

	void DrawCollidingShapes(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawCollidingShapesImpl (ChaosDebugDraw.cpp:1686-1742 [V]): every shape that has a contact, once, Yellow when the
		// particle IsDynamic() - Dynamic or Sleeping (FGenericParticleHandleImp::IsDynamic, ParticleHandle.h:1792 [V]) - and
		// Red otherwise (static, kinematic).
		std::unordered_set<uint64> Drawn;
		Drawn.reserve(Context.Snapshot.Contacts.size());
		const auto DrawOnce = [&](const uint32 BodyIndex, const uint16 ShapeIndex)
		{
			const uint64 Key = (static_cast<uint64>(BodyIndex) << 16) | ShapeIndex;
			if (!Drawn.insert(Key).second)
			{
				return;
			}
			if (const PtParticleSnapshot* Particle = FindParticle(Context, BodyIndex))
			{
				DrawBodyShapes(Queue, Context, *Particle, IsDynamicOrSleeping(*Particle) ? FColor::Yellow : FColor::Red, ShapeIndex);
			}
		};
		for (const PtContactPoint& Contact : Context.Snapshot.Contacts)
		{
			DrawOnce(Contact.Body0, Contact.Shape0);
			DrawOnce(Contact.Body1, Contact.Shape1);
		}
	}

	void DrawJointConstraints(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		// DrawJointConstraintImpl with the default features (actor connector + stretch, ChaosDebugDraw.h:179-185;
		// ChaosDebugDraw.cpp:1816-1922 [V]). Chaos' frame "a" belongs to particle[1] and "b" to particle[0]; the harness maps
		// FConstraintInstance::Body1 to particle[0] and Body2 to particle[1] (PtJointSnapshot is PROVISIONAL(M9)):
		//   white actor -> gap (JointComSize), red gap -> frame a, cyan gap -> frame b (1.5 x LineThickness), magenta a -> b (3 x).
		const FDebugDrawSettings& Settings = Context.Settings;
		const std::span<FConstraintInstance* const> Constraints = Context.Scene.GetConstraints();
		for (const PtJointSnapshot& Joint : Context.Snapshot.Joints)
		{
			const FConstraintInstance* Constraint = Joint.JointIndex < Constraints.size() ? Constraints[Joint.JointIndex] : nullptr;
			const auto ParticleOf = [&](const FBodyInstance* Body) -> const PtParticleSnapshot*
			{
				return Body != nullptr && Body->ActorHandle.IsValid() ? FindParticle(Context, Body->ActorHandle.Index) : nullptr;
			};
			const PtParticleSnapshot* Particle0 = Constraint != nullptr ? ParticleOf(Constraint->Body1) : nullptr;
			const PtParticleSnapshot* Particle1 = Constraint != nullptr ? ParticleOf(Constraint->Body2) : nullptr;
			const bool bAnyMoving = (Particle0 != nullptr && IsDynamicOrSleeping(*Particle0)) || (Particle1 != nullptr && IsDynamicOrSleeping(*Particle1));
			if (!bAnyMoving)
			{
				continue;
			}
			// End a: frame 2 and particle[1]; end b: frame 1 and particle[0]. A missing particle puts the actor on its frame.
			const FVector FrameA = ToVector(Joint.Frame2World.Translation);
			const FVector FrameB = ToVector(Joint.Frame1World.Translation);
			const FVector ActorA = Particle1 != nullptr ? ToVector(Particle1->ActorTransform.Translation) : FrameA;
			const FVector ActorB = Particle0 != nullptr ? ToVector(Particle0->ActorTransform.Translation) : FrameB;
			// Each connector leaves a gap of JointComSize at its actor: white actor -> gap, then coloured gap -> frame. UE scales
			// both gaps by the a-side length (ChaosDebugDraw.cpp:1859-1870 [V]); mirrored, so the b gap uses LengthA as well.
			const double Gap = Settings.DrawScale * Settings.JointComSize;
			const double LengthA = (FrameA - ActorA).Size();
			const double GapFraction = LengthA > 0.0 ? FMath::Clamp(Gap / LengthA, 0.0, 1.0) : 1.0;
			const FVector GapA = LengthA > UE_KINDA_SMALL_NUMBER ? ActorA + (FrameA - ActorA) * GapFraction : ActorA;
			const FVector GapB = (FrameB - ActorB).Size() > UE_KINDA_SMALL_NUMBER ? ActorB + (FrameB - ActorB) * GapFraction : ActorB;
			const float Connector = 1.5f * Settings.LineThickness;
			const uint8 Priority = Settings.DrawPriority;
			Queue.DrawDebugLine(ActorA, GapA, FColor::White, false, GKindaSmallLifeTime, Priority, Connector);
			Queue.DrawDebugLine(ActorB, GapB, FColor::White, false, GKindaSmallLifeTime, Priority, Connector);
			Queue.DrawDebugLine(GapA, FrameA, FColor::Red, false, GKindaSmallLifeTime, Priority, Connector);
			Queue.DrawDebugLine(GapB, FrameB, FColor::Cyan, false, GKindaSmallLifeTime, Priority, Connector);
			// Stretch: frame a -> frame b, 3 x LineThickness.
			Queue.DrawDebugLine(FrameA, FrameB, FColor::Magenta, false, GKindaSmallLifeTime, Priority, 3.0f * Settings.LineThickness);
		}
	}

	void DrawNativeCommands(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		for (const PtDebugDrawCmd& Native : Context.Snapshot.NativeCommands)
		{
			Queue.Enqueue(ToLatentCommand(Native, Context.Snapshot.StringPool));
		}
	}

	FDrawStats DrawAll(FDebugDrawQueue& Queue, const FDrawContext& Context)
	{
		FDrawStats Stats;
		if (!Context.Flags.bEnabled)
		{
			return Stats;
		}
		const FDrawContext& C = Context;
		const FDebugDrawFlags& F = Context.Flags;
		// FPBDRigidsSolver::PostTickDebugDraw order (PBDRigidsSolver.cpp:3417-3546 [V]).
		if (F.bDrawShapes)
		{
			DrawParticleShapes(Queue, C);
		}
		if (F.bDrawCollisions)
		{
			DrawCollisions(Queue, C);
		}
		if (F.bDrawMass)
		{
			DrawParticleMass(Queue, C);
		}
		if (F.bDrawBounds)
		{
			DrawParticleBounds(Queue, C);
		}
		if (F.bDrawTransforms)
		{
			DrawParticleTransforms(Queue, C);
		}
		if (F.bDrawIslands)
		{
			DrawConstraintGraph(Queue, C);
		}
		if (F.bDrawSleepState)
		{
			DrawSleepState(Queue, C);
		}
		if (F.bDrawCollidingShapes)
		{
			DrawCollidingShapes(Queue, C);
		}
		if (F.bDrawJoints)
		{
			DrawJointConstraints(Queue, C);
		}
		DrawNativeCommands(Queue, C);

		if (C.Islands != nullptr)
		{
			Stats.NumIslands = static_cast<int32>(C.Islands->size());
		}
		else if (F.bDrawIslands || F.bDrawSleepState)
		{
			std::vector<FHarnessIsland> Islands;
			FIslandBuilder::GatherIslands(C.Snapshot, Islands);
			Stats.NumIslands = static_cast<int32>(Islands.size());
		}
		if (F.bDrawShapes)
		{
			for (const PtParticleSnapshot& Particle : C.Snapshot.Particles)
			{
				if (const FBodyInstance* Body = GetBody(C, Particle); Body != nullptr && Body->BodySetup != nullptr)
				{
					Stats.NumSkippedConvexElements += static_cast<int32>(Body->BodySetup->AggGeom.ConvexElems.size());
				}
			}
		}
		return Stats;
	}

	uint32 GetRequiredSnapshotFlags(const FDebugDrawFlags& Flags)
	{
		uint32 SnapshotFlags = PT_SNAP_PARTICLES | PT_SNAP_NATIVE_DRAW;
		if (Flags.bDrawCollisions || Flags.bDrawIslands || Flags.bDrawSleepState || Flags.bDrawCollidingShapes)
		{
			SnapshotFlags |= PT_SNAP_CONTACTS;
		}
		if (Flags.bDrawJoints)
		{
			SnapshotFlags |= PT_SNAP_JOINTS;
		}
		return SnapshotFlags;
	}

	void CaptureSnapshot(const FPhysScene& Scene, const uint32 SnapshotFlags, FPhysicsDebugSnapshot& Out)
	{
		if (Scene.CaptureDebugSnapshot(Out, SnapshotFlags))
		{
			return;
		}
		// The backend has no snapshot: the host's view of every body with an actor (creation order = body index order).
		Out.Reset();
		Out.bFromHostState = true;
		Out.FrameIndex = Scene.GetFrameCount();
		Out.SimTime = Scene.GetSimTime();
		Out.Capabilities = Scene.GetBackendInfo().Capabilities;
		if ((SnapshotFlags & PT_SNAP_PARTICLES) == 0)
		{
			return;
		}
		for (const FBodyInstance* Body : Scene.GetBodies())
		{
			if (Body == nullptr || !Body->ActorHandle.IsValid())
			{
				continue;
			}
			const FTransform& BodyTM = Body->State.BodyTM;
			PtParticleSnapshot Particle = {};
			Particle.ActorTransform.Rotation = PtQuat{ BodyTM.Rotation.X, BodyTM.Rotation.Y, BodyTM.Rotation.Z, BodyTM.Rotation.W };
			Particle.ActorTransform.Translation = PtVec3{ BodyTM.Translation.X, BodyTM.Translation.Y, BodyTM.Translation.Z };
			const FVector ComWorld = BodyTM.TransformPosition(Body->MassProperties.CenterOfMass);
			const FQuat ComRotation = (BodyTM.Rotation * Body->MassProperties.RotationOfMass).GetNormalized();
			Particle.CenterOfMassWorld.Rotation = PtQuat{ ComRotation.X, ComRotation.Y, ComRotation.Z, ComRotation.W };
			Particle.CenterOfMassWorld.Translation = PtVec3{ ComWorld.X, ComWorld.Y, ComWorld.Z };
			FBox Bounds;
			ForEachShape(*Body, BodyTM, [&](int32, const FWorldShape& Shape)
			{
				const FBox ShapeBounds = GetWorldBounds(Shape);
				if (ShapeBounds.IsValid)
				{
					Bounds += ShapeBounds.Min;
					Bounds += ShapeBounds.Max;
				}
			});
			if (!Bounds.IsValid)
			{
				Bounds = FBox(BodyTM.Translation, BodyTM.Translation);
			}
			Particle.BoundsMin = PtVec3{ Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z };
			Particle.BoundsMax = PtVec3{ Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z };
			const FVector& V = Body->State.LinearVelocity;
			const FVector& W = Body->State.AngularVelocityInRadians;
			Particle.LinearVelocity = PtVec3{ V.X, V.Y, V.Z };
			Particle.AngularVelocity = PtVec3{ W.X, W.Y, W.Z };
			Particle.BodyIndex = Body->ActorHandle.Index;
			Particle.ObjectState = static_cast<uint8>(Body->State.ObjectState);
			Particle.NativeIslandId = -1;
			Out.Particles.push_back(Particle);
			Out.NumBodies = Particle.BodyIndex + 1 > Out.NumBodies ? Particle.BodyIndex + 1 : Out.NumBodies;   // host handles: valid
		}
	}
}
