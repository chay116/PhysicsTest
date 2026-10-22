// PhysXDebugRender.cpp - PhysX debug visualization -> PtDebugDrawCmd (see PhysXDebugRender.h).
#include "PhysXDebugRender.h"

#include "PhysXSceneConfig.h"
#include "PhysXSupport.h"

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		PtDebugDrawCmd MakeCommand(const uint32 Type, const uint32 Color)
		{
			PtDebugDrawCmd Command = {};
			Command.Type = Type;
			Command.ColorBGRA = Color;   // PhysX packed ARGB = FColor's packed layout (see the header)
			Command.Thickness = 0.0f;    // 1 px
			Command.Rotation.W = 1.0;    // unused by points, lines and triangles; a valid rotation all the same
			Command.Lifetime = 0.0f;     // this frame only
			return Command;
		}
	}

	void ApplyVisualizationParameters(physx::PxScene& Scene, const uint32 Categories, const Abi::FUnitScale& Scale)
	{
		const float VizScale = static_cast<float>(Scale.Length(100.0));   // "1 m" in native units
		Scene.setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, VizScale);
		const auto Length = [&](const double Cm) { return static_cast<float>(Scale.Length(Cm) / static_cast<double>(VizScale)); };
		const auto Set = [&](const uint32 Bit, const physx::PxVisualizationParameter::Enum Parameter, const float Value)
		{
			if ((Categories & Bit) != 0)
			{
				Scene.setVisualizationParameter(Parameter, Value);
			}
		};
		Set(PhysXViz_CollisionShapes, physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		Set(PhysXViz_CollisionAABBs, physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
		Set(PhysXViz_ActorAxes, physx::PxVisualizationParameter::eACTOR_AXES, Length(GVizAxisLengthCm));
		Set(PhysXViz_BodyMassAxes, physx::PxVisualizationParameter::eBODY_MASS_AXES, 1.0f);   // the inertia box (NpDebugViz.cpp:872-883 [V])
		// Arrow = v x eSCALE x value (NpDebugViz.cpp:858-863 [V]): the distance covered in GVizVelocitySeconds.
		Set(PhysXViz_BodyLinearVelocity, physx::PxVisualizationParameter::eBODY_LIN_VELOCITY, static_cast<float>(GVizVelocitySeconds / static_cast<double>(VizScale)));
		Set(PhysXViz_JointLocalFrames, physx::PxVisualizationParameter::eJOINT_LOCAL_FRAMES, Length(GVizAxisLengthCm));
		Set(PhysXViz_JointLimits, physx::PxVisualizationParameter::eJOINT_LIMITS, Length(GVizAxisLengthCm));
		Set(PhysXViz_ContactPoints, physx::PxVisualizationParameter::eCONTACT_POINT, 1.0f);
		Set(PhysXViz_ContactNormals, physx::PxVisualizationParameter::eCONTACT_NORMAL, Length(GVizNormalLengthCm));
	}

	FPhysXRenderCounts CountRenderBuffer(const physx::PxRenderBuffer& Buffer)
	{
		FPhysXRenderCounts Counts;
		Counts.NumPoints = Buffer.getNbPoints();
		Counts.NumLines = Buffer.getNbLines();
		Counts.NumTriangles = Buffer.getNbTriangles();
		const physx::PxDebugLine* Lines = Buffer.getLines();
		for (uint32 Index = 0; Index < Counts.NumLines; ++Index)
		{
			Counts.NumMultiColour += Lines[Index].color0 != Lines[Index].color1 ? 1u : 0u;
		}
		const physx::PxDebugTriangle* Triangles = Buffer.getTriangles();
		for (uint32 Index = 0; Index < Counts.NumTriangles; ++Index)
		{
			Counts.NumMultiColour += Triangles[Index].color0 != Triangles[Index].color1 || Triangles[Index].color0 != Triangles[Index].color2 ? 1u : 0u;
		}
		return Counts;
	}

	FPhysXRenderCounts ConvertRenderBuffer(const physx::PxRenderBuffer& Buffer, const Abi::FUnitScale& Scale, const uint32 MaxCommands, physx::PxArray<PtDebugDrawCmd>& Out,
		bool& bTruncated)
	{
		const FPhysXRenderCounts Counts = CountRenderBuffer(Buffer);
		const uint32 Wanted = Counts.Total();
		const uint32 Room = MaxCommands;
		uint32 Written = 0;
		if (Wanted > Room)
		{
			bTruncated = true;
		}
		Out.reserve(Out.size() + (Wanted < Room ? Wanted : Room));
		const physx::PxDebugPoint* Points = Buffer.getPoints();
		for (uint32 Index = 0; Index < Counts.NumPoints && Written < Room; ++Index, ++Written)
		{
			PtDebugDrawCmd Command = MakeCommand(PT_DD_POINT, Points[Index].color);
			Command.A = P2UVector(Points[Index].pos, Scale);
			Command.Size = GVizPointSizePx;
			Out.pushBack(Command);
		}
		const physx::PxDebugLine* Lines = Buffer.getLines();
		for (uint32 Index = 0; Index < Counts.NumLines && Written < Room; ++Index, ++Written)
		{
			PtDebugDrawCmd Command = MakeCommand(PT_DD_LINE, Lines[Index].color0);
			Command.A = P2UVector(Lines[Index].pos0, Scale);
			Command.B = P2UVector(Lines[Index].pos1, Scale);
			Out.pushBack(Command);
		}
		const physx::PxDebugTriangle* Triangles = Buffer.getTriangles();
		for (uint32 Index = 0; Index < Counts.NumTriangles && Written < Room; ++Index, ++Written)
		{
			PtDebugDrawCmd Command = MakeCommand(PT_DD_TRIANGLE, Triangles[Index].color0);
			Command.A = P2UVector(Triangles[Index].pos0, Scale);
			Command.B = P2UVector(Triangles[Index].pos1, Scale);
			Command.C = P2UVector(Triangles[Index].pos2, Scale);
			Out.pushBack(Command);
		}
		return Counts;
	}
}
