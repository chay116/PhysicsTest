// BulletDebugDrawer.cpp - FBulletDebugDrawer (see BulletDebugDrawer.h). [확장 M9]
#include "BulletDebugDrawer.h"

#include "BulletMath.h"
#include "PhysScene_Bullet.h"

#include <string.h>

namespace PhysicsTest::BulletBackend
{
	namespace
	{
		// PtDebugDrawCmd::Lifetime 0 = drawn for this capture only (docs/Abi.md 9.2, v1.1; PhysX writes the same).
		constexpr float GNativeLifetime = 0.0f;
		// The contact point marker: a point (px) and its normal as an arrow (cm long, arrow head cm).
		constexpr float GContactPointSize = 4.0f;
		constexpr double GContactNormalLengthCm = 5.0;
		constexpr float GContactArrowSize = 1.0f;

		uint32 ToPackedArgb(const btVector3& Color)
		{
			const auto Channel = [](const btScalar Value) -> uint32
			{
				const double Clamped = Value < btScalar(0) ? 0.0 : (Value > btScalar(1) ? 1.0 : static_cast<double>(Value));
				return static_cast<uint32>(Clamped * 255.0 + 0.5);
			};
			return (0xFFu << 24) | (Channel(Color.getX()) << 16) | (Channel(Color.getY()) << 8) | Channel(Color.getZ());
		}

		PtQuat GetRotation(const btTransform& Transform)
		{
			btQuaternion Rotation;
			Transform.getBasis().getRotation(Rotation);
			return QuatNormalize(FromBtQuat(Rotation));
		}

		// The rotation that takes UE's capsule axis (local Z, DrawDebugCapsule) to Bullet's up axis 0 / 1 / 2.
		PtQuat CapsuleAxisFromZ(const int UpAxis)
		{
			constexpr double Half = 0.70710678118654752440;
			switch (UpAxis)
			{
			case 0:
				return PtQuat{ 0.0, Half, 0.0, Half };    // Ry(+90 deg): Z -> X
			case 1:
				return PtQuat{ -Half, 0.0, 0.0, Half };   // Rx(-90 deg): Z -> Y
			default:
				return PtQuat{ 0.0, 0.0, 0.0, 1.0 };
			}
		}
	}

	FBulletDebugDrawer::FBulletDebugDrawer(FPhysScene_Bullet& InOwner, const bool bNoDeactivation)
		: Owner(InOwner)
		, Mode(DrawModes | (bNoDeactivation ? DBG_NoDeactivation : 0))
	{
	}

	void FBulletDebugDrawer::Begin(const uint32 InMaxCommands)
	{
		Commands.resizeNoInitialize(0);
		Strings.resizeNoInitialize(0);
		MaxCommands = InMaxCommands;
		bTruncated = false;
		bRecording = true;
	}

	void FBulletDebugDrawer::End()
	{
		bRecording = false;
	}

	PtDebugDrawCmd* FBulletDebugDrawer::AddCommand(const uint32 Type, const btVector3& Color)
	{
		if (!bRecording)
		{
			return nullptr;   // a draw call during a step (none in Bullet 3.25's paths for the harness shapes): not a capture
		}
		if (static_cast<uint32>(Commands.size()) >= MaxCommands)
		{
			bTruncated = true;
			return nullptr;
		}
		PtDebugDrawCmd& Command = Commands.expandNonInitializing();
		memset(&Command, 0, sizeof(Command));
		Command.Type = Type;
		Command.ColorBGRA = ToPackedArgb(Color);
		Command.Thickness = 0.0f;
		Command.Rotation = PtQuat{ 0.0, 0.0, 0.0, 1.0 };
		Command.Lifetime = GNativeLifetime;
		return &Command;
	}

	void FBulletDebugDrawer::drawLine(const btVector3& From, const btVector3& To, const btVector3& Color)
	{
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_LINE, Color))
		{
			Command->A = FromBtLength(From);
			Command->B = FromBtLength(To);
		}
	}

	void FBulletDebugDrawer::drawLine(const btVector3& From, const btVector3& To, const btVector3& FromColor, const btVector3& /*ToColor*/)
	{
		drawLine(From, To, FromColor);   // one colour per command (btIDebugDraw's default does the same)
	}

	void FBulletDebugDrawer::drawSphere(const btScalar Radius, const btTransform& Transform, const btVector3& Color)
	{
		drawSphere(Transform.getOrigin(), Radius, Color);
	}

	void FBulletDebugDrawer::drawSphere(const btVector3& Position, const btScalar Radius, const btVector3& Color)
	{
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_SPHERE, Color))
		{
			Command->A = FromBtLength(Position);
			Command->Size = static_cast<float>(GScale.ToCm(double(Radius)));
		}
	}

	void FBulletDebugDrawer::drawTriangle(const btVector3& V0, const btVector3& V1, const btVector3& V2, const btVector3& /*N0*/, const btVector3& /*N1*/,
		const btVector3& /*N2*/, const btVector3& Color, const btScalar Alpha)
	{
		drawTriangle(V0, V1, V2, Color, Alpha);
	}

	void FBulletDebugDrawer::drawTriangle(const btVector3& V0, const btVector3& V1, const btVector3& V2, const btVector3& Color, const btScalar /*Alpha*/)
	{
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_TRIANGLE, Color))
		{
			Command->A = FromBtLength(V0);
			Command->B = FromBtLength(V1);
			Command->C = FromBtLength(V2);
		}
	}

	void FBulletDebugDrawer::drawContactPoint(const btVector3& PointOnB, const btVector3& NormalOnB, const btScalar /*Distance*/, const int /*LifeTime*/,
		const btVector3& Color)
	{
		// btCollisionWorld::debugDrawWorld passes the manifold point on B and its normal (pointing at A). A point and the normal
		// as a short arrow (the harness's contact draw shows Phi itself).
		const PtVec3 Point = FromBtLength(PointOnB);
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_POINT, Color))
		{
			Command->A = Point;
			Command->Size = GContactPointSize;
		}
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_ARROW, Color))
		{
			Command->A = Point;
			Command->B = Add(Point, Scale(FromBtVector(NormalOnB), GContactNormalLengthCm));
			Command->Size = GContactArrowSize;
		}
	}

	void FBulletDebugDrawer::reportErrorWarning(const char* WarningString)
	{
		// Bullet reports engine trouble here (e.g. an AABB overflow that takes a body out of the simulation,
		// btCollisionWorld.cpp:183-193 [V]): logged in every run that has the drawer, counted for bullet.debug_drawer.
		++NumWarnings;
		Owner.GetKit().LogToHost(PT_LOG_WARNING, "LogBulletBackend", "Bullet debug drawer warning: %s", WarningString != nullptr ? WarningString : "(null)");
	}

	void FBulletDebugDrawer::draw3dText(const btVector3& Location, const char* TextString)
	{
		const size_t Length = TextString != nullptr ? strlen(TextString) : 0u;
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_STRING, btVector3(1, 1, 1)))
		{
			Command->A = FromBtLength(Location);
			Command->TextOffset = static_cast<uint32>(Strings.size());
			Command->TextLength = static_cast<uint32>(Length);
			for (size_t Index = 0; Index < Length; ++Index)
			{
				Strings.push_back(TextString[Index]);   // the pool holds no terminators (offset + length)
			}
		}
	}

	void FBulletDebugDrawer::drawBox(const btVector3& BoundsMin, const btVector3& BoundsMax, const btVector3& Color)
	{
		btTransform Identity;
		Identity.setIdentity();
		drawBox(BoundsMin, BoundsMax, Identity, Color);
	}

	void FBulletDebugDrawer::drawBox(const btVector3& BoundsMin, const btVector3& BoundsMax, const btTransform& Transform, const btVector3& Color)
	{
		// The box between BoundsMin and BoundsMax in Transform's frame: its centre in the world, its half extent, its rotation.
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_BOX, Color))
		{
			Command->A = FromBtLength(Transform * ((BoundsMin + BoundsMax) * btScalar(0.5)));
			Command->Extent = FromBtLength((BoundsMax - BoundsMin) * btScalar(0.5));
			Command->Rotation = GetRotation(Transform);
		}
	}

	void FBulletDebugDrawer::drawCapsule(const btScalar Radius, const btScalar HalfHeight, const int UpAxis, const btTransform& Transform, const btVector3& Color)
	{
		// PtDebugDrawCmd capsule: centre, rotation of the local Z axis, Extent = (radius, half height including the radius).
		// Bullet's HalfHeight is the cylinder part's (btCapsuleShape::getHalfHeight).
		if (PtDebugDrawCmd* Command = AddCommand(PT_DD_CAPSULE, Color))
		{
			Command->A = FromBtLength(Transform.getOrigin());
			Command->Rotation = QuatNormalize(QuatMultiply(GetRotation(Transform), CapsuleAxisFromZ(UpAxis)));
			Command->Extent = PtVec3{ GScale.ToCm(double(Radius)), GScale.ToCm(double(HalfHeight) + double(Radius)), 0.0 };
		}
	}
}
