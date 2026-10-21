// DebugDrawSettings.h - FDebugDrawSettings / FDebugDrawFlags and their console variables (02_Architecture 11.1, A.11). [MVP M4]
// FDebugDrawSettings mirrors Chaos::DebugDraw::FChaosDebugDrawSettings with the solver's values
// (ChaosSolverDebugDebugDrawSettings, PBDRigidsSolver.cpp:219-247 [V]). FDebugDrawFlags holds the switches. Both are bound
// to console variables with the Chaos names, registered when this module is linked:
//   p.Chaos.DebugDraw.Enabled / .MaxLines / .Radius / .SeeThrough / .ShowInactiveContacts       (ChaosDebugDrawComponent.cpp:29-38,
//                                                                                                ChaosDebugDraw.cpp:58 [V])
//   p.Chaos.Solver.DebugDrawShapes / Collisions / Mass / Bounds / Transforms / Islands / SleepState / CollidingShapes /
//   Joints, p.Chaos.Solver.DebugDraw.ShowStatics / ShowKinematics / ShowDynamics                 (PBDRigidsSolver.cpp:171-186 [V])
//   p.Chaos.Solver.DebugDraw.<Setting> (ArrowSize, BodyAxisLen, ContactLen, ContactWidth, ...)   (PBDRigidsSolver.cpp:278-297 [V])
// Viewer defaults differ from UE's in one place, recorded in Progress.md M4: p.Chaos.DebugDraw.Enabled and
// p.Chaos.Solver.DebugDrawShapes start at 1 (UE: 0, enabled with the console), because the viewer exists to draw them.
// p.Chaos.Solver.DebugDrawJoints also starts at 1 as in the A.11 sketch (UE: 0).
// ContactOwnerWidth and PushOutScale are registered for name parity only: the harness draws no contact-owner lines and the
// snapshot has no push-out (Progress.md M4 3 #14).
#pragma once

#include "CoreTypes.h"
#include "Math/Color.h"

namespace PhysicsTest
{
	struct FDebugDrawSettings
	{
		float ArrowSize = 10.0f;
		float BodyAxisLen = 30.0f;
		float ContactLen = 30.0f;
		float ContactWidth = 6.0f;
		float ContactInfoWidth = 6.0f;
		float ContactOwnerWidth = 0.0f;
		float ConstraintAxisLen = 30.0f;
		float JointComSize = 2.0f;
		float LineThickness = 1.0f;          // cm, world space (0 = 1 px)
		float DrawScale = 1.0f;
		float FontHeight = 10.0f;
		float FontScale = 1.5f;
		float ShapeThicknesScale = 1.0f;     // UE spelling (FChaosDebugDrawSettings::ShapeThicknesScale)
		float PointSize = 5.0f;              // px
		float VelScale = 0.0f;
		float AngVelScale = 0.0f;
		float ImpulseScale = 0.0f;
		float PushOutScale = 0.0f;
		float InertiaScale = 1.0f;
		uint8 DrawPriority = 10;
	};

	struct FDebugDrawFlags
	{
		bool bEnabled = true;
		bool bDrawShapes = true;
		bool bDrawCollisions = false;
		bool bDrawMass = false;
		bool bDrawBounds = false;
		bool bDrawTransforms = false;
		bool bDrawIslands = false;
		bool bDrawSleepState = false;
		bool bDrawCollidingShapes = false;
		bool bDrawJoints = true;
		bool bShowStatics = true;
		bool bShowKinematics = true;
		bool bShowDynamics = true;
		bool bShowInactiveContacts = true;
		bool bSeeThrough = true;
		int32 MaxLines = 20000;              // capture budget and render budget (UE uses the one CVar for both), 0 = infinite
		float Radius = 3000.0f;              // cm around the camera, 0 = infinite (both stages)
		FColor EngineTint = FColor::White;   // M12 ghost overlay; unused in M4
	};

	namespace DebugDraw
	{
		// The current values of the console variables above (registered by this module's static initialisers).
		void ReadConsoleVariables(FDebugDrawSettings& OutSettings, FDebugDrawFlags& OutFlags);
		// Sets every debug draw console variable back to its default (unit tests).
		void ResetConsoleVariables();
	}
}
