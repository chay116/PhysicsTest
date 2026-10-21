// BodyInstance.h - FBodyInstance and its small companions (UE: Engine/Classes/PhysicsEngine/BodyInstance.h,
// PhysicsCore/Public/BodyInstanceCore.h). [MVP M1: data model, InitBody, mass properties; commands M2]
//
// FBodyInstance folds UE's FBodyInstance + FBodyInstanceCore + FActorCreationParams + the owning component's
// BodyInstance member; the scene owns it (02_Architecture 5.1). Field names and defaults are UE's:
//   BodyInstance.cpp:357-418 [V]      8/2/1 iterations, damping 0.01/0, MassInKgOverride 100, bInertiaConditioning,
//                                     MaxAngularVelocity = project setting (3600 deg/s), SleepFamily Normal
//   BodyInstanceCore.cpp:13-24 [V]    bSimulatePhysics false, bOverrideMass false, bEnableGravity true,
//                                     bGyroscopicTorqueEnabled false, bStartAwake true
// Intentional differences (02 5.2): bStatic replaces the component's Mobility (kinematic = !bStatic && !bSimulatePhysics);
// the collision filter is an explicit 32-channel FBodyCollisionFilter instead of a collision profile; initial
// velocities are fields (angular: harness extension); FBodyInstanceState carries rad/s (UE FRigidBodyState: deg/s).
#pragma once

#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Physics/PhysicsInterfaceTypes.h"

#include <string>

namespace PhysicsTest
{
	class FBodySetup;
	class FPhysScene;
	struct FPhysicalMaterial;
	struct FPhysicsSettingsCore;

	// UE: ECC_WorldStatic = 0 (EngineTypes.h). The harness channels are plain 0..31 numbers.
	inline constexpr ECollisionChannel ECC_WorldStatic = 0;

	// Collision filtering of one body: its object channel, the channels it blocks, and whether it collides at all.
	// Mutual-block test as Chaos' FShapeFilterData::NarrowFilter (CollisionFilterData.cpp:263-272 [V]):
	//   block = (1 << A.ObjectType & B.BlockMask) && (1 << B.ObjectType & A.BlockMask)
	struct FBodyCollisionFilter
	{
		ECollisionChannel ObjectType = ECC_WorldStatic;
		uint32 BlockMask = 0xFFFFFFFFu;   // harness default: block every channel (UE takes it from the collision profile)
		ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

		// ObjectType is one of the NumCollisionChannels channels (FPhysScene::SpawnBody checks it; the ABI marshalling
		// rejects it from M2).
		[[nodiscard]] bool IsValid() const { return ObjectType < NumCollisionChannels; }
		// 0 for an invalid ObjectType: an out-of-range channel never aliases to ObjectType mod 32.
		[[nodiscard]] uint32 GetChannelMask() const { return IsValid() ? 1u << ObjectType : 0u; }
		// Both sides must have physics collision enabled and block each other's object channel.
		[[nodiscard]] bool CanCollideWith(const FBodyCollisionFilter& Other) const;
	};

	// UE: Chaos::FMassProperties after diagonalisation. InertiaTensor holds the principal moments (kg*cm^2) in the
	// frame RotationOfMass; CenterOfMass is in actor (body) space, cm.
	struct FMassProperties
	{
		double Volume = 0.0;   // cm^3
		double Mass = 0.0;     // kg
		FVector CenterOfMass;
		FQuat RotationOfMass;
		FVector InertiaTensor;

		// UE: FTransform(RotationOfMass, CenterOfMass) (BodyInstance.cpp:3507 [V], SetComLocalPose_AssumesLocked).
		[[nodiscard]] FTransform GetComLocalPose() const { return FTransform(RotationOfMass, CenterOfMass); }
	};

	// UE: FRigidBodyState / GetUnrealWorldTransform. BodyTM is the rigid pose of the body ORIGIN (not the COM) with
	// unit scale - the body scale lives in FBodyInstance::Scale3D, as in UE (BodyInstance.cpp:2732-2747 [V]:
	// GetUnrealWorldTransform returns the actor pose); velocities are of the COM in cm/s and rad/s.
	struct FBodyInstanceState
	{
		FTransform BodyTM;
		FVector LinearVelocity;
		FVector AngularVelocityInRadians;
		EObjectStateType ObjectState = EObjectStateType::Uninitialized;
	};

	struct FBodyInstance
	{
		// ---- FBodyInstanceCore (BodyInstanceCore.cpp:13-24 [V]) ----
		bool bSimulatePhysics = false;
		bool bOverrideMass = false;
		bool bEnableGravity = true;
		bool bGyroscopicTorqueEnabled = false;
		bool bStartAwake = true;

		// ---- harness: replaces UPrimitiveComponent::Mobility ----
		bool bStatic = false;

		// ---- FBodyInstance (BodyInstance.cpp:357-418 [V]) ----
		float MassInKgOverride = 100.0f;      // kg, used when bOverrideMass
		float MassScale = 1.0f;
		FVector COMNudge;                     // cm, actor space (scaled by Scale3D, BodyInstance.cpp:3490 [V])
		FVector InertiaTensorScale = FVector(1.0);
		float LinearDamping = 0.01f;
		float AngularDamping = 0.0f;
		bool bOverrideMaxAngularVelocity = false;
		float MaxAngularVelocity = 3600.0f;   // deg/s; UE initialises it from UPhysicsSettings::MaxAngularVelocity
		bool bOverrideMaxDepenetrationVelocity = false;
		float MaxDepenetrationVelocity = 0.0f;   // cm/s
		bool bUseCCD = false;
		bool bInertiaConditioning = true;
		ESleepFamily SleepFamily = ESleepFamily::Normal;
		float CustomSleepThresholdMultiplier = 1.0f;
		float StabilizationThresholdMultiplier = 1.0f;
		bool bOverrideIterationCounts = false;
		uint8 PositionSolverIterationCount = 8;
		uint8 VelocitySolverIterationCount = 2;
		uint8 ProjectionSolverIterationCount = 1;
		FBodyCollisionFilter CollisionFilter;
		const FPhysicalMaterial* PhysMaterialOverride = nullptr;
		// Harness extension (02 5.2 #14): UE's InitDynamicProperties takes the linear velocity from the owning actor.
		FVector InitialLinearVelocity;              // cm/s
		FVector InitialAngularVelocityInRadians;    // rad/s
		std::string BodyName;

		// ---- runtime (set by InitBody / the scene) ----
		const FBodySetup* BodySetup = nullptr;
		FVector Scale3D = FVector(1.0);       // UE FBodyInstance::Scale3D: the scale of the InitBody transform
		FPhysicsActorHandle ActorHandle;
		FMassProperties MassProperties;
		FBodyInstanceState State;
		// UE: the body's index in an instanced mesh or physics asset. The harness has neither, so it stays INDEX_NONE;
		// the scene's creation order is ActorHandle.Index.
		int32 InstanceBodyIndex = INDEX_NONE;

		// UE signature minus the owning component: stores the setup, Scale3D = Transform's scale and the initial pose
		// (rotation + translation, no scale: UE's GetUnrealWorldTransform is the rigid actor pose), computes the mass
		// properties (harness BodyUtils, D5) and defers the actor to the scene (FPhysScene::DeferAddActor) when given.
		void InitBody(const FBodySetup* Setup, const FTransform& Transform, FPhysScene* InRBScene);
		// Releases the backend actor (FPhysScene::ReleaseBody, phase Idle) and clears the handle. The body stays in the
		// scene's storage and GetBodies() with ObjectState Uninitialized.
		void TermBody();

		// ---- commands (UE FBodyInstance names; M2). They go through FPhysicsInterface into the scene proxy's command
		// buffer and reach the backend at the next T_push. A body without a backend actor ignores them.
		// UE5 (BodyInstance.cpp:2804-2827 [V]): a kinematic body with ETeleportType::None gets a kinematic target from the
		// owning scene (FPhysScene::SetKinematicTarget_AssumesLocked, bAllowSubstepping true); otherwise a kinematic body
		// gets FPhysicsInterface::SetKinematicTarget_AssumesLocked and then the teleport, any other body the teleport.
		void SetBodyTransform(const FTransform& NewTransform, ETeleportType Teleport);
		// bAddToCurrent adds FPhysicsInterface::Get*Velocity_AssumesLocked, as UE does (BodyInstance.cpp:3630-3646 [V]): the
		// game-thread velocity including the writes issued since the last pull (the read flushes the command buffer).
		void SetLinearVelocity(const FVector& NewVel, bool bAddToCurrent);
		void SetAngularVelocityInRadians(const FVector& NewAngVel, bool bAddToCurrent);
		// UE5: FPhysicsInterface::AddForce_AssumesLocked / AddTorque_AssumesLocked, which the harness hands to the owning
		// FPhysScene (FPhysScene::AddForce_AssumesLocked; its frame rule decides the steps a force acts on, PhysScene.h).
		void AddForce(const FVector& Force, bool bAllowSubstepping = true, bool bAccelChange = false);
		void AddTorqueInRadians(const FVector& Torque, bool bAllowSubstepping = true, bool bAccelChange = false);
		// UE: bVelChange picks AddVelocity_AssumesLocked, else AddImpulse_AssumesLocked (BodyInstance.cpp:3875-3888 [V]).
		void AddImpulse(const FVector& Impulse, bool bVelChange);
		void AddAngularImpulseInRadians(const FVector& Impulse, bool bVelChange);
		void WakeInstance();
		void PutInstanceToSleep();
		// Recomputes MassProperties with BodyUtils (static bodies too; engines ignore it for them).
		void UpdateMassProperties();

		// Override material, else the setup's material, else the scene default (UE: GEngine->DefaultPhysMaterial;
		// FBodyInstance::GetSimplePhysicalMaterial, BodyInstance.cpp:3178-3240 [V]).
		[[nodiscard]] const FPhysicalMaterial* GetSimplePhysicalMaterial() const;
		[[nodiscard]] bool IsStatic() const { return bStatic; }
		[[nodiscard]] bool IsKinematic() const { return !bStatic && !bSimulatePhysics; }
		[[nodiscard]] bool IsDynamic() const { return !bStatic && bSimulatePhysics; }
		// UE: IsNonKinematic (simulating, i.e. dynamic).
		[[nodiscard]] bool IsNonKinematic() const { return IsDynamic(); }
		// Object state a body starts with before any sleep request: Static / Kinematic / Dynamic.
		[[nodiscard]] EObjectStateType GetInitialObjectState() const;
		// UE: Sensitive = p.SensitiveSleepThresholdMultiplier (default 1/20, BodyInstance.cpp:106-110, :3617-3629 [V]),
		// Custom = CustomSleepThresholdMultiplier, else 1.
		[[nodiscard]] float GetSleepThresholdMultiplier() const;
		// UE: bOverrideMaxAngularVelocity ? MaxAngularVelocity : project MaxAngularVelocity, converted to rad/s.
		[[nodiscard]] double GetMaxAngularVelocityInRadians(const FPhysicsSettingsCore& Project) const;

		[[nodiscard]] FTransform GetUnrealWorldTransform() const { return State.BodyTM; }
		[[nodiscard]] FVector GetUnrealWorldVelocity() const { return State.LinearVelocity; }
		[[nodiscard]] FVector GetUnrealWorldAngularVelocityInRadians() const { return State.AngularVelocityInRadians; }
		[[nodiscard]] double GetBodyMass() const { return MassProperties.Mass; }
		[[nodiscard]] FVector GetBodyInertiaTensor() const { return MassProperties.InertiaTensor; }
		[[nodiscard]] FPhysScene* GetPhysicsScene() const { return Scene; }

	private:
		FPhysScene* Scene = nullptr;
		// Set by FPhysScene::SpawnBody: only scene-owned bodies (deque storage) may be deferred, released or used by
		// constraints (a stack FBodyInstance would leave a dangling pointer in the batch).
		const FPhysScene* SpawnedBy = nullptr;
		friend class FPhysScene;
	};
}
