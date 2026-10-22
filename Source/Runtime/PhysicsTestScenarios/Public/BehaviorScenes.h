// BehaviorScenes.h - the M9 behaviour scenes' fixed values (docs/Scenes.md 2.16-2.24, 03_WorkPlan M9), shared by the scenarios
// (Private/Scenarios/Behavior) and whatever checks them (unit tests, the bench's -AnalyticReferences), so a reference and
// its scene cannot drift apart. [확장 M9]
// Body indices are creation order (= the ABI BodyIndex). Every scene pins what its reference needs (geometry, masses,
// materials, initial pose) on top of the profile's body template and leaves the rest to the profile (damping, angular limit,
// sleep, restitution threshold, combine modes; 02 10), so UEParity and Normalized differ only by what the profiles change.
// Chaotic scenes (M2a, M6b, M6c) take the same-engine perturbation parameter Perturb (docs/Scenes.md 1): 0 = none, k > 0 =
// every dynamic body's initial position moved by PerturbationCm along a unit direction drawn per body from FRandomStream(k)
// (a stream of its own: the layout draws are unchanged), so k = 1, 2, ... are independent replicas.
#pragma once

#include "CoreTypes.h"

namespace PhysicsTest::BehaviorScenes
{
	// 01 7 / 02 13: "1e-6 m" = 1e-4 cm. Engines in float metres (Jolt) or float cm (PhysX UE4Compat) resolve it to about one
	// ulp at these positions (10 m: 9.5e-7 m; 1000 cm: 6.1e-5 cm); the rounding is part of the measured sensitivity.
	inline constexpr double PerturbationCm = 1.0e-4;
	inline constexpr const char* PerturbParam = "Perturb";

	// M1.SingleBoxDrop (behaviour windows, M9): the frames before the first contact (the free fall in which damping alone
	// separates UEParity from Normalized). Discrete drop d_n = g dt^2 n (n + 1) / 2 (dt_engine, 60 Hz): case a's bottom
	// (100 cm) is crossed in step 27, case b's lowest corner (150 - 81.28 = 68.72 cm, FRotator(20, 0, 30)) in step 22; the
	// windows stop one frame earlier so a speculative contact cannot reach them.
	namespace M1
	{
		inline constexpr uint32 FallLastFrameA = 25;
		inline constexpr uint32 FallLastFrameB = 20;
	}

	// M2a.Tower(N): N axis-aligned 100 cm cubes of 100 kg stacked exactly (bottom face of the lowest on the ground, faces in
	// contact, no gap), on the profile's default material (UE: F 0.7, SF 0, R 0.3). Chaotic for N >= 10.
	namespace M2a
	{
		inline constexpr const char* Name = "M2a.Tower";
		inline constexpr double Edge = 100.0;         // cm
		inline constexpr float MassKg = 100.0f;
		inline constexpr int32 Frames = 600;          // 10 s: "sleeps within 5 s" (03 M9) is visible with margin
	}

	// M4.InclineFriction(Mu, BoxYaw): a static incline (3000 x 600 x 20 cm, pitched 30 deg, centre (0, 0, 1000) cm, no
	// ground) and one 50 cm cube of 10 kg resting on its top face, 200 cm below the upper end, released from rest. Incline and
	// cube share one material (F = Mu, SF 0, R 0), so every combine mode gives the pair Mu (Average of equal values) under both
	// profiles. tan 30 = 0.577: 0.30 / 0.50 / 0.55 slide with a = g (sin 30 - Mu cos 30), 0.60 / 0.70 hold. BoxYaw 45 turns
	// the cube about the incline normal (the isotropy variant: d(t) and the path must not depend on the contact patch's
	// orientation). The cube slides along the incline's -X (down-slope).
	namespace M4
	{
		inline constexpr const char* Name = "M4.InclineFriction";
		inline constexpr double AngleDegrees = 30.0;
		inline constexpr double InclineLength = 3000.0, InclineWidth = 600.0, InclineThickness = 20.0;   // cm, full extents
		inline constexpr double InclineCentreZ = 1000.0;   // cm
		inline constexpr double Edge = 50.0;               // cm
		inline constexpr float MassKg = 10.0f;
		inline constexpr double StartFromUpperEnd = 200.0;   // cm, along the incline, to the cube centre
		inline constexpr int32 Frames = 180;               // 3 s; the criterion reads d(2 s) = the state after step 120 (frame 119)
		inline constexpr uint32 InclineBody = 0;
		inline constexpr uint32 CubeBody = 1;
	}

	// M5.BouncingBalls: four spheres (radius 25 cm, 10 kg) of restitution 0.3 / 0.5 / 0.7 / 0.9 (F 0.5, SF 0), each over a
	// static pad (200 x 200 x 100 cm, top at z = 0) of the same material, so the pair's restitution is the ball's under every
	// combine mode except Multiply / Min (the profiles use Average). Their bottoms start at the step-boundary height of G4
	// (K = 38: 201.7167 cm at 60 Hz, Analytic::StepBoundaryDropHeight) + TouchGap 0.01 cm, so the first impact is at 620.67
	// cm/s in step 39 for every engine; later impacts are wherever the bounces put them. e_k = -v_n(c) / v_n(c - 1) per impact.
	// Pads are bodies 0-3 (x = -300, -100, 100, 300), balls 4-7 in the same order.
	namespace M5
	{
		inline constexpr const char* Name = "M5.BouncingBalls";
		inline constexpr double Radius = 25.0;             // cm
		inline constexpr float MassKg = 10.0f;
		inline constexpr int32 TouchStep = 38;
		inline constexpr double TouchGap = 0.01;           // cm, as S05 v2
		inline constexpr int32 NumBalls = 4;
		inline constexpr float Restitutions[NumBalls] = { 0.3f, 0.5f, 0.7f, 0.9f };
		inline constexpr double FirstX = -300.0, SpacingX = 200.0;   // cm
		inline constexpr double PadHalfXY = 100.0, PadHalfZ = 50.0;  // cm
		inline constexpr int32 Frames = 600;
	}

	// M6a.Pendulum(Angle, Joint): a solid sphere bob (radius 5 cm, 1 kg) whose centre hangs Length = 100 cm below a world pivot
	// at (0, 0, 1000) cm, released from rest Angle degrees from the vertical in the XZ plane (the bob is rotated by the same
	// angle about Y, so its local +Z points at the pivot). Body1 = bob (Frame1 at local (0, 0, Length)), Body2 = world (Frame2
	// at the pivot); both frames have X = world Y, the hinge axis. Joint = Hinge (default, ConfigureAsHinge) or Ball
	// (ConfigureAsBallAndSocket). No ground. Reference (physical pendulum, I = m (L^2 + 2/5 r^2), amplitude series exact):
	// T(5) = 2.00905 s, T(30) = 2.04305 s, T(90) = 2.37023 s (g 980; 03 M9, Analytic::PendulumPeriod).
	namespace M6a
	{
		inline constexpr const char* Name = "M6a.Pendulum";
		inline constexpr double Length = 100.0;            // cm, pivot to bob centre
		inline constexpr double BobRadius = 5.0;           // cm
		inline constexpr float BobMassKg = 1.0f;
		inline constexpr double PivotZ = 1000.0;           // cm
		inline constexpr int32 Frames = 600;               // 10 s, about 4-5 periods
	}

	// M6b.Chain / M6c.HeavyChain: capsule links (radius 5 cm, segment 30 cm, so 40 cm tip to tip) laid along +X at z = PivotZ,
	// each rotated by Pitch 90 so its local +Z points at -X (toward the pivot). Ball joints (ConfigureAsBallAndSocket) at the
	// tips: link i's Frame1 at local (0, 0, +20) (Body1 = link i), Frame2 at the previous link's local (0, 0, -20) (Body2 =
	// link i - 1) or at the world pivot (0, 0, PivotZ) for the first link. bDisableCollision on every joint (adjacent links
	// touch at the tips; non-adjacent links collide). Released horizontal from rest: the chain falls and swings (chaotic).
	// M6c ends in a solid sphere (radius 20 cm, 100 kg = 100 x a link) whose centre is 20 cm past the last tip, jointed there.
	namespace Chain
	{
		inline constexpr double LinkRadius = 5.0;          // cm
		inline constexpr double LinkSegment = 30.0;        // cm, FKSphylElem::Length
		inline constexpr double LinkHalfPitch = 20.0;      // cm, centre to tip = LinkSegment / 2 + LinkRadius
		inline constexpr float LinkMassKg = 1.0f;
		inline constexpr double PivotZ = 1000.0;           // cm
		inline constexpr int32 Frames = 600;
	}
	namespace M6b
	{
		inline constexpr const char* Name = "M6b.Chain";
		inline constexpr int32 NumLinks = 20;
	}
	namespace M6c
	{
		inline constexpr const char* Name = "M6c.HeavyChain";
		inline constexpr int32 NumLinks = 10;
		inline constexpr double EndRadius = 20.0;          // cm
		inline constexpr float EndMassKg = 100.0f;         // 100 : 1
	}

	// M7.UEDefaults (catalog extension): bodies authored the way a UE level would - no bOverrideMass (the UE mass rule
	// m = MassScale x (rho V)^0.75 under MassRule UEBodyUtils, 02 5.3), the profile's default material, damping and sleep -
	// dropped from rest with their bottoms 300 cm above the ground, far enough apart never to touch: 50 / 100 / 200 cm cubes,
	// 25 / 50 cm spheres, a lying 25 x 100 cm capsule. Shows what the UE defaults themselves do (mass ^0.75, R 0.3 bounce,
	// damping), apart from the engines.
	namespace M7
	{
		inline constexpr const char* Name = "M7.UEDefaults";
		inline constexpr double DropHeight = 300.0;        // cm, bottom above the ground
		inline constexpr double SpacingX = 400.0;          // cm
		inline constexpr int32 Frames = 300;
	}
}
