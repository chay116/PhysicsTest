// GateScenes.h - the M6 gate scenes' fixed values, shared by the scenarios (Private/Scenarios/Gates) and the physics gates
// (PT::Conformance, Gates/PhysicsGates.h), so that a gate's analytic reference and its scene cannot drift apart
// (docs/Scenes.md 2.4-2.12, 02_Architecture 14.3, 03_WorkPlan M6). [MVP M6]
// Body indices are creation order (= the ABI BodyIndex); a scene with a ground has it at index 0 (docs/Scenes.md 1).
// Every gate scene pins what its reference needs (damping, materials, masses) on top of the profile's body template, and
// leaves the rest to the profile (02 10): S07.ClampProbe keeps the profile's angular limit, S05 the profile's restitution
// threshold, S06 / S08 the profile's sleep switch.
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest::GateScenes
{
	// S00d.FreeFallDamped (G1b): S00.FreeFall with linear damping. A visible damping (0.5 1/s) so that the order of
	// damping and force integration (Abi.md 5.14 body.damping_formula) changes z60 by centimetres, not by 1e-3 cm.
	namespace S00d
	{
		inline constexpr const char* Name = "S00d.FreeFallDamped";
		inline constexpr double Radius = 50.0;         // cm
		inline constexpr float MassKg = 1.0f;
		inline constexpr double Z0 = 1000.0;           // cm
		inline constexpr float LinearDamping = 0.5f;   // 1/s
		inline constexpr int32 Frames = 60;
	}

	// S01.UnitImpulse (G2): two free 50 cm cubes of 10 kg, gravity off, damping 0. Frame 0's pre-tick gives body 0 the
	// impulse J = (1000, 0, 0) kg cm/s (AddImpulse) and body 1 the velocity change (100, 0, 0) cm/s (AddImpulse with
	// bVelChange = ADD_VELOCITY): both end at 100 cm/s.
	namespace S01
	{
		inline constexpr const char* Name = "S01.UnitImpulse";
		inline constexpr double Edge = 50.0;            // cm
		inline constexpr float MassKg = 10.0f;
		inline constexpr double Impulse = 1000.0;       // kg cm/s
		inline constexpr double VelocityChange = 100.0; // cm/s
		inline constexpr double ExpectedSpeed = 100.0;  // cm/s = Impulse / MassKg
		inline constexpr int32 Frames = 30;
		inline constexpr uint32 ImpulseBody = 0;
		inline constexpr uint32 VelocityChangeBody = 1;
	}

	// S02.SpinInertia (G2): 100 x 50 x 20 cm boxes of 10 kg, gravity off, damping 0. Frame 0's pre-tick gives each an
	// angular impulse L: body 0 about Z (the largest moment), body 1 about X (the smallest), body 2 = body 0 with
	// COMNudge (0, 0, 10) cm (the COM-wrapping path: compound / offset-COM decorator / PhysX COM frame). omega = I^-1 L with
	// the harness inertia (2 rad/s about each axis), well below UEParity's 62.83 rad/s limit.
	namespace S02
	{
		inline constexpr const char* Name = "S02.SpinInertia";
		inline constexpr double SizeX = 100.0, SizeY = 50.0, SizeZ = 20.0;   // cm, full extents
		inline constexpr float MassKg = 10.0f;
		inline constexpr double TargetOmega = 2.0;                          // rad/s
		inline constexpr double ComNudgeZ = 10.0;                           // cm
		inline constexpr int32 Frames = 60;
		inline constexpr uint32 SpinZBody = 0;
		inline constexpr uint32 SpinXBody = 1;
		inline constexpr uint32 NudgedBody = 2;
		// Principal moments of the box (kg cm^2): m (b^2 + c^2) / 12 per axis.
		inline constexpr double Ixx = MassKg * (SizeY * SizeY + SizeZ * SizeZ) / 12.0;
		inline constexpr double Izz = MassKg * (SizeX * SizeX + SizeY * SizeY) / 12.0;
	}

	// S03.CapsuleRest (G2): radius 25, length 100 (segment; HalfLength 50) capsules placed exactly on the ground: body 1
	// standing (axis Z, centre 75 cm), body 2 lying (axis Y, centre 25 cm). Friction 0.5, restitution 0, damping 0.
	namespace S03
	{
		inline constexpr const char* Name = "S03.CapsuleRest";
		inline constexpr double Radius = 25.0;          // cm
		inline constexpr double Length = 100.0;         // cm, FKSphylElem::Length (the segment)
		inline constexpr double StandingZ = 75.0;       // cm = Length / 2 + Radius
		inline constexpr double LyingZ = 25.0;          // cm = Radius
		inline constexpr float MassKg = 50.0f;
		inline constexpr int32 Frames = 120;
		inline constexpr uint32 StandingBody = 1;
		inline constexpr uint32 LyingBody = 2;
	}

	// S04.ContactNormal (G3, a viz run): a 100 cm cube, a radius 50 sphere and a lying radius 25 / length 100 capsule, each
	// 1 cm into the ground at the start, so that the contacts of step 1 penetrate (Phi < 0) and point along +Z.
	namespace S04
	{
		inline constexpr const char* Name = "S04.ContactNormal";
		inline constexpr double Penetration = 1.0;      // cm
		inline constexpr double BoxEdge = 100.0;        // cm
		inline constexpr double SphereRadius = 50.0;    // cm
		inline constexpr double CapsuleRadius = 25.0;   // cm
		inline constexpr double CapsuleLength = 100.0;  // cm
		inline constexpr float MassKg = 100.0f;
		inline constexpr int32 Frames = 60;
		inline constexpr uint32 BoxBody = 1;
		inline constexpr uint32 SphereBody = 2;
		inline constexpr uint32 CapsuleBody = 3;
	}

	// S05.RestitutionCombine (G4): radius 25 spheres with restitution 0.8 dropped on a ground of restitution 0 from the
	// height at which the discrete trajectory touches at the end of step K = 38 (Analytic::StepBoundaryDropHeight, DL23)
	// plus TouchGap: no early or deep contact on any engine. The pair mode is the larger of the two materials' (the ground
	// is Average): body 1 Average (e 0.4), body 2 Multiply (0), body 3 Max (0.8).
	// TouchGap (version 2, M6 round 2): without it the end of step 38 is an exact touch, Phi = 0 +- rounding. An engine
	// whose narrow phase runs at the predicted end-of-step pose (Chaos: P = X + V dt after Integrate) would then see a
	// rounding-sized penetration in step 38 about half the time, push it out and bounce a step early. 0.01 cm keeps step 38
	// separated for every engine (2000x the 4e-6 cm float resolution of a 35 cm sphere-ground offset) and lies inside every SDK
	// engine's speculative distance, so they still start the contact in step 39; Bullet's speculative bias takes gap / dt
	// = 0.6 cm/s off the bounce (btSequentialImpulseConstraintSolver.cpp:949-953 [V]), 1e-3 of e.
	namespace S05
	{
		inline constexpr const char* Name = "S05.RestitutionCombine";
		inline constexpr double Radius = 25.0;          // cm
		inline constexpr float MassKg = 10.0f;
		inline constexpr float Restitution = 0.8f;
		inline constexpr int32 TouchStep = 38;
		inline constexpr double TouchGap = 0.01;        // cm above the step-38 touch
		inline constexpr int32 Frames = 120;
		inline constexpr uint32 AverageBody = 1;
		inline constexpr uint32 MultiplyBody = 2;
		inline constexpr uint32 MaxBody = 3;
	}

	// S06.FrictionSlide (G5): body 1, a 50 cm cube on the ground (friction 0.5 on both, Average), starts at 500 cm/s along
	// +X; body 2 is a static incline (600 x 300 x 20 cm, pitched 30 deg, friction 0) and bodies 3 / 4 are 50 cm cubes at rest
	// on it whose material wins the pair with Max: friction 0.6 (holds: tan 30 deg = 0.577) and 0.55 (slides).
	namespace S06
	{
		inline constexpr const char* Name = "S06.FrictionSlide";
		inline constexpr double Edge = 50.0;            // cm
		inline constexpr float MassKg = 10.0f;
		inline constexpr double V0 = 500.0;             // cm/s
		inline constexpr float Friction = 0.5f;
		inline constexpr double SlopeDegrees = 30.0;
		inline constexpr float HoldFriction = 0.6f;
		inline constexpr float SlideFriction = 0.55f;
		inline constexpr double InclineSizeX = 600.0, InclineSizeY = 300.0, InclineSizeZ = 20.0;   // cm
		inline constexpr double InclineCentreY = 1000.0, InclineCentreZ = 400.0;                   // cm
		inline constexpr double SlopeBoxOffsetY = 75.0;  // cm, either side of the incline centre line
		inline constexpr int32 Frames = 120;
		inline constexpr int32 SlopeCheckFrame = 20;     // before any engine's sleep timer can stop a slow slider
		inline constexpr uint32 SlideBody = 1;
		inline constexpr uint32 InclineBody = 2;
		inline constexpr uint32 HoldBody = 3;
		inline constexpr uint32 SlopeSlideBody = 4;
	}

	// S07.ClampProbe (G7): free 100 cm cubes (isotropic inertia), gravity off, damping 0, initial angular velocity about Z
	// of 50 / 80 / 120 rad/s; the profile's angular limit applies (UEParity 3600 deg/s = 62.83 rad/s, Normalized released).
	namespace S07
	{
		inline constexpr const char* Name = "S07.ClampProbe";
		inline constexpr double Edge = 100.0;           // cm
		inline constexpr float MassKg = 100.0f;
		inline constexpr double Omega0[3] = { 50.0, 80.0, 120.0 };   // rad/s
		inline constexpr int32 Frames = 10;
	}

	// S08.StartAsleep (G6, UEParity only): two 100 cm cubes at z 500 cm over the ground; body 1 has bStartAwake = false
	// (02 6 step 5: wake counter 0 + put to sleep after AddActorsToScene), body 2 starts awake and falls.
	namespace S08
	{
		inline constexpr const char* Name = "S08.StartAsleep";
		inline constexpr double Edge = 100.0;           // cm
		inline constexpr float MassKg = 100.0f;
		inline constexpr double Z0 = 500.0;             // cm
		inline constexpr int32 Frames = 60;
		inline constexpr uint32 AsleepBody = 1;
		inline constexpr uint32 AwakeBody = 2;
	}
}
