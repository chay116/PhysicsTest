// BodyUtils.h - mass properties computed by the harness (D5): clean-room re-implementation of UE 5.8.3
// Engine/Private/PhysicsEngine/BodyUtils.cpp:16-141 [V] on top of the Chaos per-shape integration it calls
// (ChaosInterfaceUtils.cpp:137-190, Chaos MassProperties.cpp:281-492 [V]). [MVP M1: analytic primitives]
//
// Order (02_Architecture 5.3, DL17):
//  1 density      no material -> 1.0 kg/cm^3; else max(9e-8, Density[g/cm^3] * 0.001) kg/cm^3 (BodyUtils.cpp:37-45)
//  2 volume integration per element (Chaos TSphere / TBox / FCapsule volume and inertia), parallel-axis combination
//    (CombineWorldSpace)
//  3 diagonalisation (BodyUtils.cpp:124; harness: cyclic Jacobi with Chaos' "already diagonal" early-out)
//  4 mass rule    !bOverrideMass: M' = max(MassScale * M^clamp(RaiseMassToPower, 1e-4, 1), 0.001)
//                  bOverrideMass: M' = max(MassInKgOverride, 0.001)          (BodyUtils.cpp:53-72)
//                  EMassRule::Physical uses exponent 1
//  5 inertia *= M'/M                                                          (:74-76)
//  6 COM += COMNudge * Scale3D (actor space)                                  (:77-79, BodyInstance.cpp:3490-3492)
//  7 InertiaTensorScale via ScaleInertia(bInertiaScaleIncludeMass = false) when any |S - 1| > 1e-3, re-diagonalised
//                                                                             (:90-96)
//  8 p.ComNudgeAffectsInertia (default true, :16-17): I_xx += M' n_x^2, I_yy += M' n_y^2, I_zz += M' n_z^2 on the
//    actor-space tensor, exactly as UE writes it (after step 7, per the source comment), then re-diagonalised (:98-112)
//
// Intentional differences (02 5.2 #22): steps 1, 4 and 5 run in double (UE: float density 0.001f * Density and float
// OldMass / NewMass / MassRatio, relative differences of order 1e-8); the diagonaliser is Jacobi instead of Chaos'
// closed-form eigen solver - same tensor, the principal-axis order of a non-diagonal tensor may differ; a setup with
// no contributing element gets Chaos' 523 kg fallback with an Error log, where UE skips the mass update altogether.
// Convex elements are data only (M15): a contributing convex element is skipped with an Error log.
// FBodySetup::BuildScale3D is not applied (as in UE, it records the scale already baked into AggGeom).
#pragma once

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace PhysicsTest
{
	class FBodySetup;
	struct FPhysicalMaterial;
}

namespace PhysicsTest::BodyUtils
{
	// Step 1 (kg/cm^3).
	[[nodiscard]] double GetBodyInstanceDensity(const FPhysicalMaterial* Material);

	// Steps 1-3: uniform-density mass properties of Setup's contributing elements at Scale3D, diagonalised. With no
	// contributing element: Chaos' fallback (523 kg, 5.24e5 kg*cm^2, MassProperties.cpp:481-497) and an Error log.
	[[nodiscard]] FMassProperties ComputeUniformMassProperties(const FBodySetup& Setup, const FVector& Scale3D, double DensityKgPerCm3);

	// Steps 4-8 on diagonalised mass properties.
	void ApplyMassPropertiesModifiers(FMassProperties& InOut, const FBodyInstance& Body, const FPhysicalMaterial* Material, EMassRule Rule);

	// Steps 1-8 for Body (Body.Scale3D is the body scale). Step 8 reads p.ComNudgeAffectsInertia
	// (PhysicsCVars::GetComNudgeAffectsInertia, Physics/PhysicsConsoleVariables.h).
	[[nodiscard]] FMassProperties ComputeMassProperties(const FBodyInstance& Body, const FBodySetup& Setup, const FPhysicalMaterial* Material, EMassRule Rule);
}
