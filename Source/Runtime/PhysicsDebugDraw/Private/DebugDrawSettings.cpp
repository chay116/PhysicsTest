// DebugDrawSettings.cpp - the debug draw console variables (see DebugDrawSettings.h). The names are Chaos' (a profile or
// -ExecCmds line written for UE works unchanged); the help texts are the harness's own.
#include "DebugDrawSettings.h"

#include "HAL/IConsoleManager.h"

namespace PhysicsTest
{
	namespace
	{
		// Storage the console variables refer to. Defaults: FDebugDrawSettings / FDebugDrawFlags.
		FDebugDrawSettings GSettings;
		FDebugDrawFlags GFlags;

		// p.Chaos.DebugDraw.* (ChaosDebugDrawComponent.cpp:29-38, ChaosDebugDraw.cpp:58 [V])
		FAutoConsoleVariableRef CVarEnabled("p.Chaos.DebugDraw.Enabled", GFlags.bEnabled, "Draws the solver debug view (1) or nothing (0)");
		FAutoConsoleVariableRef CVarMaxLines("p.Chaos.DebugDraw.MaxLines", GFlags.MaxLines,
			"Debug draw budget per frame, in Chaos command cost units, applied at capture and again at render (0 = no limit)");
		FAutoConsoleVariableRef CVarRadius("p.Chaos.DebugDraw.Radius", GFlags.Radius, "Debug draw region: radius around the camera in cm (0 = everywhere)");
		FAutoConsoleVariableRef CVarSeeThrough("p.Chaos.DebugDraw.SeeThrough", GFlags.bSeeThrough,
			"1 = debug lines show through the scene (an always-pass pass under the depth-tested one)");
		FAutoConsoleVariableRef CVarShowInactiveContacts("p.Chaos.DebugDraw.ShowInactiveContacts", GFlags.bShowInactiveContacts,
			"1 = also draw contact points that took no impulse (grey, shorter normal)");

		// p.Chaos.Solver.DebugDraw* switches (PBDRigidsSolver.cpp:171-186 [V])
		FAutoConsoleVariableRef CVarShapes("p.Chaos.Solver.DebugDrawShapes", GFlags.bDrawShapes, "1 = draw collision shapes after each frame");
		FAutoConsoleVariableRef CVarCollisions("p.Chaos.Solver.DebugDrawCollisions", GFlags.bDrawCollisions,
			"1 = draw contact points, discs and normals after each frame");
		FAutoConsoleVariableRef CVarMass("p.Chaos.Solver.DebugDrawMass", GFlags.bDrawMass, "1 = draw each rigid body's mass as text at its centre of mass");
		FAutoConsoleVariableRef CVarBounds("p.Chaos.Solver.DebugDrawBounds", GFlags.bDrawBounds, "1 = draw particle and shape bounds");
		FAutoConsoleVariableRef CVarTransforms("p.Chaos.Solver.DebugDrawTransforms", GFlags.bDrawTransforms,
			"1 = draw centre-of-mass axes, the actor origin and the inertia box");
		FAutoConsoleVariableRef CVarIslands("p.Chaos.Solver.DebugDrawIslands", GFlags.bDrawIslands,
			"1 = draw one box per island (harness islands), Black when asleep");
		FAutoConsoleVariableRef CVarSleepState("p.Chaos.Solver.DebugDrawSleepState", GFlags.bDrawSleepState,
			"1 = colour shapes by island sleep state against the sleep thresholds");
		FAutoConsoleVariableRef CVarCollidingShapes("p.Chaos.Solver.DebugDrawCollidingShapes", GFlags.bDrawCollidingShapes,
			"1 = draw only the shapes that have a contact");
		FAutoConsoleVariableRef CVarJoints("p.Chaos.Solver.DebugDrawJoints", GFlags.bDrawJoints, "1 = draw joint connectors and frames");
		FAutoConsoleVariableRef CVarShowStatics("p.Chaos.Solver.DebugDraw.ShowStatics", GFlags.bShowStatics, "0 hides static bodies in the shape drawers");
		FAutoConsoleVariableRef CVarShowKinematics("p.Chaos.Solver.DebugDraw.ShowKinematics", GFlags.bShowKinematics,
			"0 hides kinematic bodies in the shape drawers");
		FAutoConsoleVariableRef CVarShowDynamics("p.Chaos.Solver.DebugDraw.ShowDynamics", GFlags.bShowDynamics, "0 hides dynamic bodies in the shape drawers");

		// p.Chaos.Solver.DebugDraw.<Setting> (PBDRigidsSolver.cpp:278-297 [V])
		FAutoConsoleVariableRef CVarArrowSize("p.Chaos.Solver.DebugDraw.ArrowSize", GSettings.ArrowSize, "Arrow head size of the axis arrows, cm");
		FAutoConsoleVariableRef CVarBodyAxisLen("p.Chaos.Solver.DebugDraw.BodyAxisLen", GSettings.BodyAxisLen, "Length of the centre-of-mass axis arrows, cm");
		FAutoConsoleVariableRef CVarContactLen("p.Chaos.Solver.DebugDraw.ContactLen", GSettings.ContactLen, "Length of the contact normal line, cm");
		FAutoConsoleVariableRef CVarContactWidth("p.Chaos.Solver.DebugDraw.ContactWidth", GSettings.ContactWidth,
			"Radius of the contact plane disc, cm (the point disc is half of it)");
		FAutoConsoleVariableRef CVarContactInfoWidth("p.Chaos.Solver.DebugDraw.ContactInfoWidth", GSettings.ContactInfoWidth,
			"Size of the contact info boxes (sleeping contact), cm");
		FAutoConsoleVariableRef CVarContactOwnerWidth("p.Chaos.Solver.DebugDraw.ContactOwnerWidth", GSettings.ContactOwnerWidth,
			"Registered for name parity; the harness does not draw contact owner lines (Progress.md M4)");
		FAutoConsoleVariableRef CVarConstraintAxisLen("p.Chaos.Solver.DebugDraw.ConstraintAxisLen", GSettings.ConstraintAxisLen,
			"Length of the joint frame axes, cm");
		FAutoConsoleVariableRef CVarLineThickness("p.Chaos.Solver.DebugDraw.LineThickness", GSettings.LineThickness,
			"Debug line width, cm in world space (0 = one pixel)");
		FAutoConsoleVariableRef CVarShapeLineThicknessScale("p.Chaos.Solver.DebugDraw.ShapeLineThicknessScale", GSettings.ShapeThicknesScale,
			"Multiplier on LineThickness for shapes drawn by the sleep-state and colliding-shape drawers");
		FAutoConsoleVariableRef CVarPointSize("p.Chaos.Solver.DebugDraw.PointSize", GSettings.PointSize, "Size of debug points, pixels");
		FAutoConsoleVariableRef CVarVelScale("p.Chaos.Solver.DebugDraw.VelScale", GSettings.VelScale,
			"Above 0: velocity line on the transforms view, length = velocity x this");
		FAutoConsoleVariableRef CVarAngVelScale("p.Chaos.Solver.DebugDraw.AngVelScale", GSettings.AngVelScale,
			"Above 0: angular velocity line on the transforms view, length = rad/s x this");
		FAutoConsoleVariableRef CVarImpulseScale("p.Chaos.Solver.DebugDraw.ImpulseScale", GSettings.ImpulseScale,
			"Above 0: normal impulse line per contact point, length = impulse x this");
		FAutoConsoleVariableRef CVarPushOutScale("p.Chaos.Solver.DebugDraw.PushOutScale", GSettings.PushOutScale,
			"Registered for name parity; the snapshot carries no push-out, so nothing is drawn (Progress.md M4)");
		FAutoConsoleVariableRef CVarInertiaScale("p.Chaos.Solver.DebugDraw.InertiaScale", GSettings.InertiaScale,
			"Above 0: the box with the body's inertia on the transforms view, scaled by this");
		FAutoConsoleVariableRef CVarScale("p.Chaos.Solver.DebugDraw.Scale", GSettings.DrawScale, "Global multiplier on debug draw lengths and sizes");
		// DrawPriority is a uint8 in the harness struct; its console variable keeps its own int32 (UE: int32 in FChaosDebugDrawSettings).
		int32 GDrawPriority = 10;
		FAutoConsoleVariableRef CVarDrawPriority("p.Chaos.Solver.DebugDraw.DrawPriority", GDrawPriority,
			"Depth priority passed with the debug commands (recorded; the viewer draws all at their depth)");

		FAutoConsoleVariableRef* const GAllVariables[] = { &CVarEnabled, &CVarMaxLines, &CVarRadius, &CVarSeeThrough, &CVarShowInactiveContacts, &CVarShapes,
			&CVarCollisions, &CVarMass, &CVarBounds, &CVarTransforms, &CVarIslands, &CVarSleepState, &CVarCollidingShapes, &CVarJoints, &CVarShowStatics,
			&CVarShowKinematics, &CVarShowDynamics, &CVarArrowSize, &CVarBodyAxisLen, &CVarContactLen, &CVarContactWidth, &CVarContactInfoWidth,
			&CVarContactOwnerWidth, &CVarConstraintAxisLen, &CVarLineThickness, &CVarShapeLineThicknessScale, &CVarPointSize, &CVarVelScale, &CVarAngVelScale,
			&CVarImpulseScale, &CVarPushOutScale, &CVarInertiaScale, &CVarScale, &CVarDrawPriority };
	}

	namespace DebugDraw
	{
		void ReadConsoleVariables(FDebugDrawSettings& OutSettings, FDebugDrawFlags& OutFlags)
		{
			OutSettings = GSettings;
			OutSettings.DrawPriority = static_cast<uint8>(GDrawPriority < 0 ? 0 : (GDrawPriority > 255 ? 255 : GDrawPriority));
			OutFlags = GFlags;
		}

		void ResetConsoleVariables()
		{
			for (FAutoConsoleVariableRef* Variable : GAllVariables)
			{
				(*Variable)->ResetToDefault();
			}
		}
	}
}
