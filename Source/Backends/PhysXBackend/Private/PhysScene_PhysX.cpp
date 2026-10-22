// PhysScene_PhysX.cpp - FPhysScene_PhysX (see PhysScene_PhysX.h). UE4 behaviour re-implemented here is cited from the
// design text [M] (02_Architecture 8.2); PhysX facts are cited from the pinned SDK [V]; PhysX is used through its public
// API only (no local patch, Q8).
#include "PhysScene_PhysX.h"

#include "PhysicsFiltering.h"
#include "PhysicsInitialization.h"
#include "PhysXParse.h"
#include "PtContactConvention.h"

#include <float.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// "Unclamped" limits: PhysX squares the limit in float and rejects values above 1e16 (NpRigidBodyTemplate.h:952,
		// :975 [V]); its integrator clamps angular speed at 1e7 rad/s anyway (DyBodyCoreIntegrator.h:123 [V]).
		constexpr float GUnclampedLinearVelocity = 1.0e16f;   // native units / s
		constexpr float GUnclampedAngularVelocity = 1.0e7f;   // rad/s
		// Viz contact listener capacity: PCM keeps a handful of points per pair; 16 per pair is the [M] bound.
		constexpr uint32 GListenerPointsPerPairHint = 8;
		constexpr uint32 GDefaultListenerCapacity = 4096;
		// COM identity on the PhysX side (shape.wrapping offset_com, DL42): the geometric COM is computed in float native
		// units; a difference within this many float ulps of the body's size is the same COM (the Jolt rule, M3).
		constexpr float GComToleranceUlps = 16.0f;

		const char* GetMotionName(const uint32 Motion)
		{
			return Motion == PT_MOTION_STATIC ? "static" : (Motion == PT_MOTION_KINEMATIC ? "kinematic" : "dynamic");
		}

		void FormatVec(char (&Out)[128], const PtVec3& V)
		{
			snprintf(Out, sizeof(Out), "(%.9g, %.9g, %.9g)", V.X, V.Y, V.Z);
		}

		struct FRange
		{
			double Min = 0.0;
			double Max = 0.0;
			bool bAny = false;
			void Add(const double Value)
			{
				if (!bAny)
				{
					Min = Max = Value;
					bAny = true;
					return;
				}
				Min = Value < Min ? Value : Min;
				Max = Value > Max ? Value : Max;
			}
		};

		// The PhysX geometry of one element in native units (capsules along PhysX X).
		bool MakeGeometry(const PtShapeElem& Element, const Abi::FUnitScale& Scale, physx::PxGeometryHolder& Out)
		{
			switch (Element.Type)
			{
			case PT_SHAPE_SPHERE:
				Out.storeAny(physx::PxSphereGeometry(static_cast<float>(Scale.Length(Element.Radius))));
				return true;
			case PT_SHAPE_BOX:
				Out.storeAny(physx::PxBoxGeometry(static_cast<float>(Scale.Length(Element.BoxHalfExtent.X)), static_cast<float>(Scale.Length(Element.BoxHalfExtent.Y)),
					static_cast<float>(Scale.Length(Element.BoxHalfExtent.Z))));
				return true;
			case PT_SHAPE_CAPSULE:
				Out.storeAny(physx::PxCapsuleGeometry(static_cast<float>(Scale.Length(Element.Radius)), static_cast<float>(Scale.Length(Element.HalfLength))));
				return true;
			default:
				return false;   // convex: PT_CAP_SHAPE_CONVEX is not declared, the kit rejects it
			}
		}

		// The element's local pose: element transform, and for a capsule the UE Z -> PhysX X basis.
		physx::PxTransform MakeShapeLocalPose(const PtShapeElem& Element, const Abi::FUnitScale& Scale)
		{
			physx::PxTransform Pose = U2PTransform(Element.LocalTransform, Scale);
			if (Element.Type == PT_SHAPE_CAPSULE)
			{
				Pose.q = (Pose.q * U2PSphylBasis()).getNormalized();
			}
			return Pose;
		}

		// The rotation that carries dq = To * conj(From) as an angular velocity over Dt (shortest arc), rad/s.
		PtVec3 AngularVelocityBetween(const PtQuat& From, const PtQuat& To, const double Dt)
		{
			// dq = To * conj(From)
			const double FX = -From.X, FY = -From.Y, FZ = -From.Z, FW = From.W;
			double X = To.W * FX + To.X * FW + To.Y * FZ - To.Z * FY;
			double Y = To.W * FY - To.X * FZ + To.Y * FW + To.Z * FX;
			double Z = To.W * FZ + To.X * FY - To.Y * FX + To.Z * FW;
			double W = To.W * FW - To.X * FX - To.Y * FY - To.Z * FZ;
			if (W < 0.0)
			{
				X = -X;
				Y = -Y;
				Z = -Z;
				W = -W;
			}
			const double SinHalf = sqrt(X * X + Y * Y + Z * Z);
			if (!(SinHalf > 1e-12) || !(Dt > 0.0))
			{
				return PtVec3{ 0.0, 0.0, 0.0 };
			}
			const double Angle = 2.0 * atan2(SinHalf, W);
			const double Scale = Angle / (SinHalf * Dt);
			return PtVec3{ X * Scale, Y * Scale, Z * Scale };
		}
	}

	// ---- construction ------------------------------------------------------------------------------------------------
	FPhysScene_PhysX::FPhysScene_PhysX(const PtSceneDesc& InDesc, const FPhysXSceneConfig& InConfig)
		: Desc(InDesc)
		, Config(InConfig)
		, Scale{ InConfig.LengthScale }
	{
		Desc.DebugName = nullptr;
		Desc.Options = nullptr;
		Desc.NumOptions = 0;
		Gravity = InDesc.Gravity;
		bSleepingAllowed = (InDesc.SimFlags & PT_SIM_ALLOW_SLEEPING) != 0;
		ErrorsSeen = GetPhysXErrorCallback().GetCounts();   // reports from here on (creation included) are this scene's
	}

	FPhysScene_PhysX::~FPhysScene_PhysX()
	{
		// Actors are owned by the PxPhysics, not the scene: release the joints (M9; PhysX does not release a joint with its
		// actors), then every live actor (which removes it from the scene), then the scene, the dispatcher it used, the
		// materials and the scratch block.
		ReleaseAllJoints();
		for (FPhysXBody& Body : Bodies)
		{
			if (!Body.bReleased && Body.Actor != nullptr)
			{
				Body.Actor->release();
				Body.Actor = nullptr;
				Body.Dynamic = nullptr;
			}
		}
		if (Scene != nullptr)
		{
			Scene->release();
			Scene = nullptr;
		}
		SingleThreadDispatcher.reset();
		InlineDispatcher.reset();
		if (PoolDispatcher != nullptr)
		{
			PoolDispatcher->release();
			PoolDispatcher = nullptr;
		}
		EventCallback.reset();
		for (physx::PxMaterial* Material : Materials)
		{
			Material->release();
		}
		Materials.clear();
		if (ScratchBlock != nullptr)
		{
			_aligned_free(ScratchBlock);
			ScratchBlock = nullptr;
		}
	}

	PtResult FPhysScene_PhysX::InitPhysScene(char (&Error)[256])
	{
		physx::PxPhysics& Physics = GetPhysXSDK();
		const physx::PxTolerancesScale& Tolerances = Physics.getTolerancesScale();
		// One PxTolerancesScale per process (PxPhysics.h:997 [V]); its length is "1 m" in the scene's units (UE4: 100 cm,
		// SDK: 1 m). A scene whose LengthScale contradicts it (SdkDefault under a UE4Compat PxPhysics) is refused here.
		const double ExpectedLength = 100.0 * Config.LengthScale;
		if (fabs(static_cast<double>(Tolerances.length) - ExpectedLength) > 1e-4 * ExpectedLength)
		{
			snprintf(Error, sizeof(Error), "PhysX: LengthScale %g needs PxTolerancesScale length %g, but this process's PxPhysics has (%g, %g) - one "
				"tolerances scale per process (PT_MODULE_GLOBAL_SCENE_SETTINGS): this variant needs its own process", Config.LengthScale, ExpectedLength,
				static_cast<double>(Tolerances.length), static_cast<double>(Tolerances.speed));
			return PT_E_INVALID_ARG;
		}

		// The CPU dispatcher of the thread budget T (02 9.6).
		const uint32 NumThreads = Desc.NumThreads > 0 ? Desc.NumThreads : 1u;
		const bool bSingle = Config.Dispatcher == EPhysXDispatcherKind::SingleThread || (Config.Dispatcher == EPhysXDispatcherKind::Auto && NumThreads <= 1);
		if (bSingle)
		{
			SingleThreadDispatcher = std::make_unique<FPhysXCPUDispatcherSingleThread>();
			Dispatcher = SingleThreadDispatcher.get();
		}
		else if (NumThreads <= 1)
		{
			InlineDispatcher = std::make_unique<FPhysXCPUDispatcherInline>();
			if (!InlineDispatcher->IsValid())
			{
				snprintf(Error, sizeof(Error), "PhysX: PxDefaultCpuDispatcherCreate(0) failed");
				return PT_E_ENGINE;
			}
			Dispatcher = InlineDispatcher.get();
		}
		else
		{
			PoolDispatcher = physx::PxDefaultCpuDispatcherCreate(NumThreads);
			if (PoolDispatcher == nullptr)
			{
				snprintf(Error, sizeof(Error), "PhysX: PxDefaultCpuDispatcherCreate(%u) failed", NumThreads);
				return PT_E_ENGINE;
			}
			Dispatcher = PoolDispatcher;
		}

		physx::PxSceneDesc SceneDesc(Tolerances);
		SceneDesc.gravity = U2PVector(Gravity, Scale);
		SceneDesc.filterShader = &PhysXSimFilterShader;
		// The two [M] filter rules of the variant (PhysicsFiltering.h); PhysX copies the block (PxSceneDesc.h:609-624 [V]).
		FilterRules.NonBlocking = static_cast<uint32>(Config.NonBlockingPairFilter);
		FilterRules.KinematicStatic = static_cast<uint32>(Config.KinematicStaticPairFilter);
		SceneDesc.filterShaderData = &FilterRules;
		SceneDesc.filterShaderDataSize = sizeof(FilterRules);
		SceneDesc.kineKineFilteringMode = Config.KineKineFiltering;
		SceneDesc.staticKineFilteringMode = Config.StaticKineFiltering;
		SceneDesc.broadPhaseType = Config.BroadPhase;
		SceneDesc.solverType = Config.Solver;
		// Restitution threshold (DL35): the profile's value when given (Normalized 50 cm/s), else the variant's (UE4 200 cm/s).
		const double BounceCm = Desc.RestitutionThreshold > 0.0 ? Desc.RestitutionThreshold : Config.BounceThresholdVelocityCm;
		SceneDesc.bounceThresholdVelocity = static_cast<float>(Scale.Length(BounceCm));
		physx::PxSceneFlags Flags;
		if (Config.bEnablePCM)
		{
			Flags |= physx::PxSceneFlag::eENABLE_PCM;
		}
		if (Config.bEnableStabilization)
		{
			Flags |= physx::PxSceneFlag::eENABLE_STABILIZATION;
		}
		if (Config.bEnableCCD || (Desc.SimFlags & PT_SIM_ENABLE_CCD) != 0)
		{
			Flags |= physx::PxSceneFlag::eENABLE_CCD;
		}
		if (Config.bEnableEnhancedDeterminism || (Desc.SimFlags & PT_SIM_ENHANCED_DETERMINISM) != 0)
		{
			Flags |= physx::PxSceneFlag::eENABLE_ENHANCED_DETERMINISM;
		}
		if (Config.bEnableActiveActors)
		{
			Flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
			Flags |= physx::PxSceneFlag::eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS;
		}
		if (!bSleepingAllowed)
		{
			Flags |= physx::PxSceneFlag::eDISABLE_SLEEPING;   // Normalized (DL48): never a zero-threshold workaround
		}
		SceneDesc.flags = Flags;
		SceneDesc.dynamicTreeRebuildRateHint = Config.DynamicTreeRebuildRateHint;
		// Capacity hints (02 9.4) as PhysX's preallocation hints; PhysX grows past them.
		if (Desc.MaxBodiesHint > 0)
		{
			SceneDesc.limits.maxNbActors = Desc.MaxBodiesHint;
			SceneDesc.limits.maxNbBodies = Desc.MaxBodiesHint;
			SceneDesc.limits.maxNbStaticShapes = Desc.MaxBodiesHint;
			SceneDesc.limits.maxNbDynamicShapes = Desc.MaxBodiesHint;
		}
		if (Desc.MaxBodyPairsHint > 0)
		{
			SceneDesc.limits.maxNbBroadPhaseOverlaps = Desc.MaxBodyPairsHint;
		}
		SceneDesc.cpuDispatcher = Dispatcher;
		// No listener in bench / behaviour runs (02 9.4: no event callback, no eNOTIFY_* flag).
		if (Desc.RunMode == PT_RUN_VIZ || (Desc.InstrumentFlags & PT_INSTR_CONTACTS) != 0)
		{
			const uint32 Hint = Desc.MaxContactConstraintsHint > 0 ? Desc.MaxContactConstraintsHint * GListenerPointsPerPairHint : 0u;
			ListenerCapacity = Hint > GDefaultListenerCapacity ? Hint : GDefaultListenerCapacity;
			EventCallback = std::make_unique<FPhysXSimEventCallback>(ListenerCapacity);
			SceneDesc.simulationEventCallback = EventCallback.get();
			SleepingContacts.reserve(ListenerCapacity);
			SleepingScratch.reserve(ListenerCapacity);
			PrevStepContacts.reserve(ListenerCapacity);
		}
		if (!SceneDesc.isValid())
		{
			snprintf(Error, sizeof(Error), "PhysX: the PxSceneDesc built from the variant is not valid (PxSceneDesc::isValid)");
			return PT_E_INVALID_ARG;
		}
		Scene = Physics.createScene(SceneDesc);
		if (Scene == nullptr)
		{
			snprintf(Error, sizeof(Error), "PhysX: PxPhysics::createScene failed");
			return PT_E_ENGINE;
		}
		// M9 native debug draw: PhysX's visualization in viz runs only (PhysXDebugRender.h). Every other run keeps eSCALE 0, so
		// PhysX draws nothing and spends nothing on it.
		if (Desc.RunMode == PT_RUN_VIZ && Config.Visualization != 0)
		{
			ApplyVisualizationParameters(*Scene, Config.Visualization, Scale);
			bVisualization = true;
		}
		// UE4's SimulateScratchMemorySize block: 16-byte aligned, a multiple of 16 KiB (PxScene::simulate).
		if (Config.ScratchMemoryBytes > 0)
		{
			ScratchBlock = _aligned_malloc(Config.ScratchMemoryBytes, 16384);
			if (ScratchBlock == nullptr)
			{
				snprintf(Error, sizeof(Error), "PhysX: cannot allocate the %u-byte scratch block", Config.ScratchMemoryBytes);
				return PT_E_OUT_OF_MEMORY;
			}
		}
		return PT_OK;
	}

	void FPhysScene_PhysX::MarkDirty(const uint32 BodyIndex)
	{
		FPhysXBody& Body = Bodies[BodyIndex];
		if (!Body.bDirty)
		{
			Body.bDirty = true;
			DirtyBodies.pushBack(BodyIndex);
		}
	}

	bool FPhysScene_PhysX::IsAsleep(const FPhysXBody& Body) const
	{
		// isSleeping() is false in an eDISABLE_SLEEPING scene (NpRigidDynamic.cpp:425 [V]).
		return Body.IsSimulatedDynamic() && Body.Dynamic != nullptr && Body.Dynamic->isSleeping();
	}

	void FPhysScene_PhysX::CheckPhysXErrors(const bool bBetweenFrames)
	{
		const FPhysXErrorCounts Now = GetPhysXErrorCallback().GetCounts();
		const uint32 Bits = GetNewErrorBits(ErrorsSeen, Now);
		ErrorsSeen = Now;
		if (Bits == 0)
		{
			return;
		}
		// A report between frames belongs to the next frame (the last frame's result stays as it was, as in the Jolt backend).
		(bBetweenFrames ? PendingErrorBits : FrameErrorBits) |= Bits;
		TotalErrorBits |= Bits;
	}

	physx::PxVec3 FPhysScene_PhysX::MultiplyWorldInverseInertia(const physx::PxRigidDynamic& Actor, const physx::PxVec3& V) const
	{
		const physx::PxQuat Rotation = Actor.getGlobalPose().q * Actor.getCMassLocalPose().q;
		const physx::PxVec3 Local = Rotation.rotateInv(V);
		const physx::PxVec3 InvInertia = Actor.getMassSpaceInvInertiaTensor();
		return Rotation.rotate(physx::PxVec3(Local.x * InvInertia.x, Local.y * InvInertia.y, Local.z * InvInertia.z));
	}

	physx::PxVec3 FPhysScene_PhysX::MultiplyWorldInertia(const physx::PxRigidDynamic& Actor, const physx::PxVec3& V) const
	{
		const physx::PxQuat Rotation = Actor.getGlobalPose().q * Actor.getCMassLocalPose().q;
		const physx::PxVec3 Local = Rotation.rotateInv(V);
		const physx::PxVec3 Inertia = Actor.getMassSpaceInertiaTensor();
		return Rotation.rotate(physx::PxVec3(Local.x * Inertia.x, Local.y * Inertia.y, Local.z * Inertia.z));
	}

	// ---- creation ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_PhysX::CreateMaterials(const Abi::TStridedView<PtPhysicalMaterial> InMaterials)
	{
		physx::PxPhysics& Physics = GetPhysXSDK();
		const uint32 First = Materials.size();
		for (uint32 Index = 0; Index < InMaterials.Num(); ++Index)
		{
			const PtPhysicalMaterial& Info = InMaterials[Index];
			// createMaterial(staticFriction, dynamicFriction, restitution) (PxPhysics.h:764 [V]). UE4Compat: static = dynamic
			// [M]; SdkDefault: max(F, SF), which PhysX's pair combine (static = max(C(SF0, SF1), C(F0, F1)),
			// PxsMaterialCombiner.h [V]) turns into Chaos' pair rule (PBDCollisionConstraints.cpp:472-476 [V]).
			const double Static = Config.StaticFrictionRule == EPhysXStaticFrictionRule::EqualsDynamic ? Info.Friction
				: (Info.StaticFriction > Info.Friction ? Info.StaticFriction : Info.Friction);
			physx::PxMaterial* Material = Physics.createMaterial(static_cast<float>(Static), static_cast<float>(Info.Friction), static_cast<float>(Info.Restitution));
			if (Material == nullptr)
			{
				// All or nothing: release what this call created.
				while (Materials.size() > First)
				{
					Materials.back()->release();
					Materials.popBack();
					MaterialInfos.popBack();
				}
				return GetKit().SetLastError(PT_E_ENGINE, "PhysX: createMaterial failed for material %u", Index);
			}
			// The PT_COMBINE_* values are PxCombineMode's (PtBackendAbi.h; the larger mode wins in both).
			Material->setFrictionCombineMode(static_cast<physx::PxCombineMode::Enum>(Info.FrictionCombineMode));
			Material->setRestitutionCombineMode(static_cast<physx::PxCombineMode::Enum>(Info.RestitutionCombineMode));
			Materials.pushBack(Material);
			MaterialInfos.pushBack(Info);
		}
		return PT_OK;
	}

	double FPhysScene_PhysX::ComputeContactOffsetCm(const PtShapeElem& Element) const
	{
		// MinExtent: the radius of a sphere or capsule, the smallest half extent of a box [M].
		double MinExtent = Element.Radius;
		if (Element.Type == PT_SHAPE_BOX)
		{
			const PtVec3& Half = Element.BoxHalfExtent;
			MinExtent = Half.X < Half.Y ? Half.X : Half.Y;
			MinExtent = Half.Z < MinExtent ? Half.Z : MinExtent;
		}
		const double Offset = Config.ContactOffsetMultiplier * MinExtent;
		return Offset < Config.MinContactOffsetCm ? Config.MinContactOffsetCm : (Offset > Config.MaxContactOffsetCm ? Config.MaxContactOffsetCm : Offset);
	}

	physx::PxRigidActor* FPhysScene_PhysX::CreateActor_AssumesLocked(const PtActorCreateInfo& Info)
	{
		physx::PxPhysics& Physics = GetPhysXSDK();
		const physx::PxTransform Pose = U2PTransform(Info.ActorTransform, Scale);
		physx::PxRigidActor* Actor = nullptr;
		if (Info.Motion == PT_MOTION_STATIC)
		{
			Actor = Physics.createRigidStatic(Pose);
		}
		else
		{
			physx::PxRigidDynamic* Dynamic = Physics.createRigidDynamic(Pose);
			if (Dynamic == nullptr)
			{
				return nullptr;
			}
			// Creation-time flags (BodyInstance.cpp:1301-1340 [V], CreateActor): kinematic, CCD, gyroscopic, the linear limit.
			if (Info.Motion == PT_MOTION_KINEMATIC)
			{
				Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
			}
			else
			{
				if ((Info.Flags & PT_BODY_USE_CCD) != 0)
				{
					Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);   // with the scene flag and EPDF_CCD
				}
				if ((Info.Flags & PT_BODY_GYROSCOPIC) != 0)
				{
					// UE4 on PhysX 3.4 had no gyroscopic term: the UE4Compat family ignores the request (GyroscopicForces=Ignore,
					// counted in body.gyroscopic); eENABLE_GYROSCOPIC_FORCES exists since PhysX 5.1 (PxRigidBody.h [V]).
					if (Config.GyroscopicRule == EPhysXGyroscopicRule::FromBody)
					{
						Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES, true);
					}
					else
					{
						++NumGyroscopicIgnored;
					}
				}
			}
			const double MaxLinearCm = Info.MaxLinearVelocity > 0.0 ? Info.MaxLinearVelocity : Desc.MaxLinearVelocity;
			const double MaxLinear = MaxLinearCm > 0.0 ? Scale.Length(MaxLinearCm) : static_cast<double>(GUnclampedLinearVelocity);
			Dynamic->setMaxLinearVelocity(static_cast<float>(MaxLinear < static_cast<double>(GUnclampedLinearVelocity) ? MaxLinear : static_cast<double>(GUnclampedLinearVelocity)));
			if (EventCallback)
			{
				Dynamic->setActorFlag(physx::PxActorFlag::eSEND_SLEEP_NOTIFIES, true);   // FPhysXSimEventCallback counters (viz)
			}
			Actor = Dynamic;
		}
		if (Actor != nullptr)
		{
			if ((Info.Flags & PT_BODY_ENABLE_GRAVITY) == 0)
			{
				Actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, true);
			}
			Actor->userData = FPhysxUserData::Encode(EPhysxUserDataType::Body, Info.BodyIndex);
		}
		return Actor;
	}

	bool FPhysScene_PhysX::CreateShapesFromAggGeom(const PtActorCreateInfo& Info, const Abi::TStridedView<PtShapeElem>& Shapes, physx::PxRigidActor& Actor)
	{
		physx::PxPhysics& Physics = GetPhysXSDK();
		// EPDF flags of every shape of this body (UE4 CreateShapeFilterData [M]).
		uint32 Epdf = 0;
		Epdf |= Info.Motion == PT_MOTION_STATIC ? EPDF_StaticShape : 0u;
		Epdf |= (Info.Flags & PT_BODY_USE_CCD) != 0 && Info.Motion == PT_MOTION_DYNAMIC ? EPDF_CCD : 0u;
		Epdf |= EventCallback ? EPDF_ContactNotify : 0u;   // the listener only (viz or PT_INSTR_CONTACTS, 02 9.4)
		for (uint32 Offset = 0; Offset < Info.NumShapes; ++Offset)
		{
			const PtShapeElem& Element = Shapes[Info.FirstShape + Offset];
			physx::PxGeometryHolder Geometry;
			if (!MakeGeometry(Element, Scale, Geometry))
			{
				return false;
			}
			physx::PxShapeFlags ShapeFlags = physx::PxShapeFlag::eVISUALIZATION;
			if ((Element.Flags & PT_SHAPEF_SIMULATION) != 0)
			{
				ShapeFlags |= physx::PxShapeFlag::eSIMULATION_SHAPE;
			}
			if ((Element.Flags & PT_SHAPEF_QUERY) != 0)
			{
				ShapeFlags |= physx::PxShapeFlag::eSCENE_QUERY_SHAPE;
			}
			// One exclusive shape per element (UE4 bEnableShapeSharing = false, PhysicsSettingsCore default [V]).
			physx::PxShape* Shape = Physics.createShape(Geometry.any(), *Materials[Element.MaterialIndex], true, ShapeFlags);
			if (Shape == nullptr)
			{
				return false;
			}
			Shape->setLocalPose(MakeShapeLocalPose(Element, Scale));
			// Contact offset: the element's own value when the host gives one, else the variant's rule (UE4 clamp or PhysX's
			// default 0.02 x tolerances length, left untouched). Rest offset from the element (UE: 0).
			const double RestCm = Element.RestOffset;
			double ContactCm = -1.0;
			if (Element.ContactOffset >= 0.0)
			{
				ContactCm = Element.ContactOffset;
			}
			else if (Config.ContactOffsetRule == EPhysXContactOffsetRule::UE4Clamp)
			{
				ContactCm = ComputeContactOffsetCm(Element);
			}
			if (ContactCm >= 0.0)
			{
				const double Contact = ContactCm > RestCm ? ContactCm : RestCm + Config.MinContactOffsetCm;   // PhysX needs contact > rest
				Shape->setContactOffset(static_cast<float>(Scale.Length(Contact)));
			}
			if (RestCm != 0.0)
			{
				Shape->setRestOffset(static_cast<float>(Scale.Length(RestCm)));
			}
			const uint32 ShapeEpdf = Epdf | ((Element.Flags & PT_SHAPEF_SIMULATION) != 0 ? EPDF_SimpleCollision : 0u);
			const physx::PxFilterData Filter = MakeSimFilterData(Info.BodyIndex, Info.BlockMask, Info.BodyIndex, Info.ObjectType, ShapeEpdf);
			Shape->setSimulationFilterData(Filter);
			Shape->setQueryFilterData(Filter);
			Shape->userData = FPhysxUserData::Encode(EPhysxUserDataType::Shape, Offset);
			const bool bAttached = Actor.attachShape(*Shape);
			Shape->release();   // the actor holds the exclusive shape now
			if (!bAttached)
			{
				return false;
			}
		}
		return true;
	}

	Abi::FShapeWrappingCounts FPhysScene_PhysX::ReadWrapping() const
	{
		// Read from the engine's actors (M5 round 0; before, the counts came from PtActorCreateInfo at creation). PhysX holds
		// several shapes per actor, a local pose per shape and a COM frame per body natively, so nothing is wrapped; the counts
		// mark the conditions the Jolt and Bullet wrappers mark (G9: a lone primitive at the actor frame counts 0):
		//   compound         several PxShapes on the actor
		//   local_transform  one PxShape whose local pose, without U2PSphylBasis for a capsule, is not the actor frame (DL42
		//                    tolerances; the rotation test is on the half-angle sine, far above float noise)
		//   offset_com       the COM frame (getCMassLocalPose) is not the attached shapes' uniform-density COM
		//                    (PxMassProperties of each geometry at its local pose, PxMassProperties.h), float 16 ulp of the size
		//   axis_remap       a PxCapsuleGeometry (UE's Z capsule lies along PhysX's X)
		Abi::FShapeWrappingCounts Counts;
		constexpr physx::PxU32 MaxShapes = 16;
		const physx::PxQuat SphylBasisInverse = U2PSphylBasis().getConjugate();
		const double MaxSinHalfAngle = sqrt(1.0 - Abi::GComIdentityMinQuatDot * Abi::GComIdentityMinQuatDot);   // |q . identity| > 1 - 1e-9
		const float Floor = static_cast<float>(Scale.Length(Abi::GComIdentityMaxOffsetCm));
		for (const FPhysXBody& Record : Bodies)
		{
			if (Record.bReleased)
			{
				continue;
			}
			const physx::PxRigidActor& Actor = *Record.Actor;
			const physx::PxU32 NumShapes = Actor.getNbShapes();
			physx::PxShape* Shapes[MaxShapes];
			for (physx::PxU32 Start = 0; Start < NumShapes; Start += MaxShapes)
			{
				const physx::PxU32 Got = Actor.getShapes(Shapes, MaxShapes, Start);
				for (physx::PxU32 Index = 0; Index < Got; ++Index)
				{
					Counts.AxisRemap += Shapes[Index]->getGeometry().getType() == physx::PxGeometryType::eCAPSULE ? 1u : 0u;
				}
			}
			const physx::PxU32 NumRead = Actor.getShapes(Shapes, MaxShapes, 0);
			if (NumShapes > 1)
			{
				++Counts.Compound;
			}
			else if (NumRead == 1)
			{
				const physx::PxTransform Local = Shapes[0]->getLocalPose();
				const physx::PxQuat Element = Shapes[0]->getGeometry().getType() == physx::PxGeometryType::eCAPSULE ? Local.q * SphylBasisInverse : Local.q;
				const double SinHalfAngle = sqrt(static_cast<double>(Element.x) * Element.x + static_cast<double>(Element.y) * Element.y
					+ static_cast<double>(Element.z) * Element.z);
				const double OffsetCm = Scale.ToCm(sqrt(static_cast<double>(Local.p.magnitudeSquared())));
				if (SinHalfAngle > MaxSinHalfAngle || !(OffsetCm < Abi::GComIdentityMaxOffsetCm))
				{
					++Counts.LocalTransform;
				}
			}
			if (Record.Dynamic == nullptr || NumRead == 0)
			{
				continue;   // a static body has no COM frame
			}
			if (NumShapes > MaxShapes)
			{
				++Counts.OffsetCom;   // not checked: counted as wrapped (no M5 body has more than 16 elements)
				continue;
			}
			physx::PxMassProperties Parts[MaxShapes];
			physx::PxTransform Poses[MaxShapes];
			float Size = 0.0f;
			for (physx::PxU32 Index = 0; Index < NumRead; ++Index)
			{
				Parts[Index] = physx::PxMassProperties(Shapes[Index]->getGeometry());
				Poses[Index] = Shapes[Index]->getLocalPose();
				Size = Poses[Index].p.abs().maxElement() > Size ? Poses[Index].p.abs().maxElement() : Size;
			}
			const physx::PxVec3 Geometric = physx::PxMassProperties::sum(Parts, Poses, NumRead).centerOfMass;
			const physx::PxVec3 Target = Record.Dynamic->getCMassLocalPose().p;
			Size = Target.abs().maxElement() > Size ? Target.abs().maxElement() : Size;
			Size = Geometric.abs().maxElement() > Size ? Geometric.abs().maxElement() : Size;
			const float Tolerance = GComToleranceUlps * FLT_EPSILON * Size > Floor ? GComToleranceUlps * FLT_EPSILON * Size : Floor;
			if ((Target - Geometric).magnitudeSquared() > Tolerance * Tolerance)
			{
				++Counts.OffsetCom;
			}
		}
		return Counts;
	}

	void FPhysScene_PhysX::InitDynamicProperties_AssumesLocked(physx::PxRigidDynamic& Actor, const PtActorCreateInfo& Info)
	{
		const bool bKinematic = Info.Motion == PT_MOTION_KINEMATIC;
		// 1. mass, inertia (principal, x L^2), COM frame - the harness values (D5).
		if (Info.Mass > 0.0)
		{
			Actor.setMass(static_cast<float>(Info.Mass));
		}
		if (Info.InertiaDiagonal.X > 0.0 && Info.InertiaDiagonal.Y > 0.0 && Info.InertiaDiagonal.Z > 0.0)
		{
			Actor.setMassSpaceInertiaTensor(U2PInertialVector(Info.InertiaDiagonal, Scale));
		}
		Actor.setCMassLocalPose(U2PTransform(Info.CenterOfMassLocal, Scale));
		// 2. damping (PhysX applies v *= max(0, 1 - c dt) like UE, DyBodyCoreIntegrator.h:26-42 [V]).
		Actor.setLinearDamping(static_cast<float>(Info.LinearDamping));
		Actor.setAngularDamping(static_cast<float>(Info.AngularDamping));
		// 3. max angular velocity (rad/s).
		const double MaxAngular = Info.MaxAngularVelocity > 0.0 ? Info.MaxAngularVelocity : static_cast<double>(GUnclampedAngularVelocity);
		Actor.setMaxAngularVelocity(static_cast<float>(MaxAngular < static_cast<double>(GUnclampedAngularVelocity) ? MaxAngular : static_cast<double>(GUnclampedAngularVelocity)));
		// 4. max depenetration velocity. ABI: < 0 unset, >= 0 the body's rate (UE5). PhysX 5.11 needs > 0
		//    (NpRigidBodyTemplate.h:834 [V]).
		if (Config.DepenetrationRule == EPhysXDepenetrationRule::UE4Project)
		{
			// UE4 [M]: unset -> the project's UPhysicsSettingsCore::MaxDepenetrationVelocity; 0 -> unlimited (PX_MAX_F32).
			double Rate = Info.MaxDepenetrationVelocity;
			if (Rate < 0.0)
			{
				Rate = Config.ProjectMaxDepenetrationVelocityCm;
				++NumDepenetrationFromProject;
			}
			if (Rate <= 0.0)
			{
				Actor.setMaxDepenetrationVelocity(PX_MAX_F32);
				++NumDepenetrationUnlimited;
			}
			else
			{
				Actor.setMaxDepenetrationVelocity(static_cast<float>(Scale.Length(Rate)));
			}
		}
		else if (Info.MaxDepenetrationVelocity > 0.0)
		{
			Actor.setMaxDepenetrationVelocity(static_cast<float>(Scale.Length(Info.MaxDepenetrationVelocity)));
		}
		else if (Info.MaxDepenetrationVelocity == 0.0)
		{
			++NumDepenetrationUnrepresentable;   // UE5's "do not push apart" has no PhysX value (> 0 required): SDK default kept
		}
		// 5. initial velocity (COM, cm/s; rad/s). Kinematic bodies take no velocity (they follow targets).
		if (!bKinematic)
		{
			Actor.setLinearVelocity(U2PVector(Info.InitialLinearVelocity, Scale));
			Actor.setAngularVelocity(U2PUnitless(Info.InitialAngularVelocity));
		}
		// 6-7. sleep and stabilisation thresholds: PhysX's defaults from the tolerances (5e-5 / 2.5e-5 x speed^2, PxvDynamics.h
		//      init [V]: 50 / 25 at (100, 1000)) times the body's multipliers (UE4 [M]; SleepFamily via the host).
		if (Config.ThresholdRule == EPhysXThresholdRule::UE4Multiplier)
		{
			Actor.setSleepThreshold(Actor.getSleepThreshold() * static_cast<float>(Info.SleepThresholdMultiplier));
			Actor.setStabilizationThreshold(Actor.getStabilizationThreshold() * static_cast<float>(Info.StabilizationThresholdMultiplier));
		}
		// 8. solver iterations: the body's own when it overrides, else the scene's (PtSceneDesc) or the variant's (UE4 8/1).
		uint32 Position = Desc.PositionIterations > 0 ? static_cast<uint32>(Desc.PositionIterations) : Config.PositionIterations;
		uint32 Velocity = Desc.VelocityIterations > 0 ? static_cast<uint32>(Desc.VelocityIterations) : Config.VelocityIterations;
		if ((Info.Flags & PT_BODY_OVERRIDE_ITERATIONS) != 0)
		{
			Position = Info.PositionIterations;
			Velocity = Info.VelocityIterations;
			++NumIterationOverrides;
		}
		Position = Position < 1u ? 1u : (Position > 255u ? 255u : Position);
		Velocity = Velocity > 255u ? 255u : Velocity;
		Actor.setSolverIterationCounts(Position, Velocity);
	}

	PtResult FPhysScene_PhysX::CreateActors(const Abi::FActorBatchView& Batch)
	{
		const uint32 First = Bodies.size();
		const uint32 Num = Batch.Actors.Num();
		physx::PxArray<physx::PxActor*> Created;
		Created.reserve(Num);
		const auto Abort = [&](const PtResult Code, const char* What, const uint32 Index) -> PtResult
		{
			// All or nothing: nothing was added to the scene yet, so the created actors are released again.
			for (physx::PxActor* Actor : Created)
			{
				Actor->release();
			}
			return GetKit().SetLastError(Code, "PhysX: actor %u: %s", Index, What);
		};

		// 1 + 2: per body CreateActor with the creation-time flags, then its shapes (UE order, 02 6).
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			physx::PxRigidActor* Actor = CreateActor_AssumesLocked(Info);
			if (Actor == nullptr)
			{
				return Abort(PT_E_ENGINE, "createRigidStatic / createRigidDynamic failed", Index);
			}
			Created.pushBack(Actor);
			if (!CreateShapesFromAggGeom(Info, Batch.Shapes, *Actor))
			{
				return Abort(PT_E_ENGINE, "a shape could not be created or attached", Index);
			}
		}

		// 3: one AddActorsToScene for the whole batch.
		if (Num > 0 && !Scene->addActors(Created.begin(), Num))
		{
			return Abort(PT_E_ENGINE, "PxScene::addActors failed", 0);
		}

		// 4 + 5: per body InitDynamicProperties in UE order, then put-to-sleep for !bStartAwake.
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			FPhysXBody Record;
			Record.Actor = static_cast<physx::PxRigidActor*>(Created[Index]);
			Record.Dynamic = Info.Motion != PT_MOTION_STATIC ? static_cast<physx::PxRigidDynamic*>(Record.Actor) : nullptr;
			Record.Motion = Info.Motion;
			Record.Flags = Info.Flags;
			Record.Mass = Info.Mass;
			Record.InertiaDiagonal = Info.InertiaDiagonal;
			Record.CenterOfMassLocal = Info.CenterOfMassLocal;
			Record.KinematicTarget = Info.ActorTransform;
			if (Record.Dynamic != nullptr)
			{
				InitDynamicProperties_AssumesLocked(*Record.Dynamic, Info);
				if (Info.Motion == PT_MOTION_DYNAMIC && (Info.Flags & PT_BODY_START_AWAKE) == 0)
				{
					if (bSleepingAllowed)
					{
						// The body is in the scene already (putToSleep needs it, NpRigidDynamic.cpp:527 [V]).
						Record.Dynamic->setWakeCounter(0.0f);
						Record.Dynamic->putToSleep();
					}
					else
					{
						++NumStartAsleepSkipped;   // 02 6 step 5 is skipped when sleeping is off (Normalized)
					}
				}
			}
			Bodies.pushBack(Record);
			MobilityRanks.pushBack(Info.Motion == PT_MOTION_STATIC ? Abi::GMobilityStatic : (Info.Motion == PT_MOTION_KINEMATIC ? Abi::GMobilityKinematic : Abi::GMobilityDynamic));
			if (Info.Motion == PT_MOTION_KINEMATIC)
			{
				Kinematics.pushBack(First + Index);
			}
			DirtyBodies.pushBack(First + Index);   // reported by the next ACTIVE_ONLY read
		}
		// Reserved to the body count: T_push and T_pull never allocate.
		const uint32 Total = Bodies.size();
		PendingImpulseBodies.reserve(Total);
		PendingForceBodies.reserve(Total);
		ReadScratch.reserve(Total);
		ActiveScratch.reserve(Total);
		PrevActive.reserve(Total);
		DirtyBodies.reserve(Total);
		if (EventCallback)
		{
			KeptContactBodies.reserve(Total);
			ReportStamps.resize(Total, 0u);
		}
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::ReleaseActors(const uint32_t* BodyIndices, const uint32_t Num)
	{
		// The kit has rejected out-of-range, released and repeated indices. M9: the body's joints go first (docs/Abi.md 9.4
		// item 6), the indices stay taken.
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			ReleaseJointsOf(BodyIndices[Index]);
			FPhysXBody& Body = Bodies[BodyIndices[Index]];
			Body.Actor->release();   // removes it from the scene
			Body.Actor = nullptr;
			Body.Dynamic = nullptr;
			Body.bReleased = true;
		}
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::PrepareForSimulation(Abi::FTextWriter& OutActions)
	{
		// The same untimed opportunity for every engine (02 9.5): the scratch block's pages are touched here so that its
		// first use is not a page fault inside the timed first frames (02 9.4 lists no other PhysX preparation).
		uint32 Pages = 0;
		if (ScratchBlock != nullptr)
		{
			volatile unsigned char* Bytes = static_cast<unsigned char*>(ScratchBlock);
			for (uint32 Offset = 0; Offset < Config.ScratchMemoryBytes; Offset += 4096u)
			{
				Bytes[Offset] = 0;
				++Pages;
			}
		}
		OutActions.Appendf("physx: %u actors in the scene; scratch block %u bytes, pre-faulted (%u pages of 4 KiB)", Scene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC
			| physx::PxActorTypeFlag::eRIGID_DYNAMIC), Config.ScratchMemoryBytes, Pages);
		return PT_OK;
	}

	// ---- frame -------------------------------------------------------------------------------------------------------
	PtResult FPhysScene_PhysX::SetUpForFrame(const Abi::FFrameParams& Params)
	{
		// SDK backends set the gravity only when it changes (DL32).
		if (Params.bNewGravity && (Params.Gravity.X != Gravity.X || Params.Gravity.Y != Gravity.Y || Params.Gravity.Z != Gravity.Z))
		{
			Gravity = Params.Gravity;
			Scene->setGravity(U2PVector(Gravity, Scale));
		}
		StepDeltaTime = Params.DeltaSeconds;   // one step per call (no NATIVE_FRAME_MODEL)
		// Reports since the last frame belong to this frame: those a query between frames already saw (PendingErrorBits)
		// and those nobody has looked at yet (creation, commands, reads).
		FrameErrorBits = PendingErrorBits;
		PendingErrorBits = 0;
		bStepped = false;
		bSimulating = false;
		CheckPhysXErrors(false);
		return PT_OK;
	}

	void FPhysScene_PhysX::ApplyPendingDynamics()
	{
		// Velocity changes: added to the PhysX velocity right before simulate, as Chaos adds LinearImpulseVelocity in
		// Integrate (PBDRigidsEvolutionGBF.cpp:920-924 [V]). A body asleep now keeps its change pending (as Null and Jolt).
		uint32 Kept = 0;
		for (uint32 Slot = 0; Slot < PendingImpulseBodies.size(); ++Slot)
		{
			const uint32 Index = PendingImpulseBodies[Slot];
			FPhysXBody& Record = Bodies[Index];
			if (Record.bReleased)
			{
				Record.bPendingImpulse = false;
				continue;
			}
			if (IsAsleep(Record))
			{
				PendingImpulseBodies[Kept++] = Index;
				continue;
			}
			Record.Dynamic->setLinearVelocity(Record.Dynamic->getLinearVelocity() + Record.PendingLinearVelocity, false);
			Record.Dynamic->setAngularVelocity(Record.Dynamic->getAngularVelocity() + Record.PendingAngularVelocity, false);
			Record.PendingLinearVelocity = physx::PxVec3(0.0f);
			Record.PendingAngularVelocity = physx::PxVec3(0.0f);
			Record.bPendingImpulse = false;
		}
		PendingImpulseBodies.forceSize_Unsafe(Kept);

		// Forces and torques of this push act on this step only, and only on a body that is awake now: PhysX clears the
		// accumulators after each simulate, so nothing is left for a body that sleeps later.
		for (const uint32 Index : PendingForceBodies)
		{
			FPhysXBody& Record = Bodies[Index];
			if (!Record.bReleased && !IsAsleep(Record))
			{
				Record.Dynamic->addForce(Record.PendingForce, physx::PxForceMode::eFORCE, false);
				Record.Dynamic->addTorque(Record.PendingTorque, physx::PxForceMode::eFORCE, false);
			}
			Record.PendingForce = physx::PxVec3(0.0f);
			Record.PendingTorque = physx::PxVec3(0.0f);
			Record.bPendingForce = false;
		}
		PendingForceBodies.clear();
	}

	PtResult FPhysScene_PhysX::StartFrame()
	{
		if (!(StepDeltaTime > 0.0f))
		{
			// Nothing to advance (the Harness driver never sends such a step): the push consumed the forces, impulses stay.
			for (const uint32 Index : PendingForceBodies)
			{
				FPhysXBody& Record = Bodies[Index];
				Record.PendingForce = physx::PxVec3(0.0f);
				Record.PendingTorque = physx::PxVec3(0.0f);
				Record.bPendingForce = false;
			}
			PendingForceBodies.clear();
			return PT_OK;
		}
		// Kinematic bodies: a pending target becomes setKinematicTarget for this step; the velocity reported is the actor
		// origin's displacement over the step (Chaos ApplyKinematicTargets, PBDRigidsEvolutionGBF.cpp:1180-1239 [V]). A body
		// that moved last step and got no target stops (Chaos EKinematicTargetMode::Reset, :1191-1197 [V]).
		for (const uint32 Index : Kinematics)
		{
			FPhysXBody& Record = Bodies[Index];
			if (Record.bReleased)
			{
				continue;
			}
			if (Record.bHasKinematicTarget)
			{
				const PtTransform Current = P2UTransform(Record.Actor->getGlobalPose(), Scale);
				const double InvDt = 1.0 / static_cast<double>(StepDeltaTime);
				Record.KinematicOriginVelocity = PtVec3{ (Record.KinematicTarget.Translation.X - Current.Translation.X) * InvDt,
					(Record.KinematicTarget.Translation.Y - Current.Translation.Y) * InvDt, (Record.KinematicTarget.Translation.Z - Current.Translation.Z) * InvDt };
				Record.KinematicAngularVelocity = AngularVelocityBetween(Current.Rotation, Record.KinematicTarget.Rotation, static_cast<double>(StepDeltaTime));
				Record.Dynamic->setKinematicTarget(U2PTransform(Record.KinematicTarget, Scale));
				Record.bHasKinematicTarget = false;
				Record.bKinematicMovedLastStep = true;
			}
			else if (Record.bKinematicMovedLastStep)
			{
				Record.KinematicOriginVelocity = PtVec3{ 0.0, 0.0, 0.0 };
				Record.KinematicAngularVelocity = PtVec3{ 0.0, 0.0, 0.0 };
				Record.bKinematicMovedLastStep = false;
			}
		}
		ApplyPendingDynamics();
		if (EventCallback)
		{
			// Before the listener forgets the last step's contacts: the commands' kept pairs, and the previous step's reports.
			FlushKeptContactCommands();
			EventCallback->BeginStep();
			// The narrow phase of this step sees the poses of now: the callback places PointOn1 on the second shape at them.
			PreStepPoses.resizeUninitialized(Bodies.size());
			for (uint32 Index = 0; Index < Bodies.size(); ++Index)
			{
				PreStepPoses[Index] = Bodies[Index].Actor != nullptr && !Bodies[Index].bReleased ? Bodies[Index].Actor->getGlobalPose() : physx::PxTransform(physx::PxIdentity);
			}
			EventCallback->SetPreStepPoses(PreStepPoses.begin(), PreStepPoses.size());
		}
		CheckPhysXErrors(false);   // the kinematic targets and pending dynamics above
		// UE4 TG_StartPhysics: simulate with the scene's scratch block. With FPhysXCPUDispatcherSingleThread the whole step
		// runs inside this call.
		if (!Scene->simulate(StepDeltaTime, nullptr, ScratchBlock, ScratchBlock != nullptr ? Config.ScratchMemoryBytes : 0u))
		{
			FrameErrorBits |= GPhysXErrorSimulateRefused;
		}
		else
		{
			bSimulating = true;
			LastSimulatedDeltaTime = StepDeltaTime;   // PxConstraint::getForce divides the joint impulse by this dt (joint snapshot)
		}
		CheckPhysXErrors(false);
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::WaitPhysScenes()
	{
		// UE4 TG_EndPhysics: fetchResults(true). The simulation event callback runs inside it, on this thread.
		if (bSimulating)
		{
			physx::PxU32 ErrorState = 0;
			if (!Scene->fetchResults(true, &ErrorState) || ErrorState != 0)
			{
				FrameErrorBits |= GPhysXErrorSimulateRefused;
			}
			bSimulating = false;
			bStepped = true;
			++TotalSteps;
		}
		CheckPhysXErrors(false);
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::EndFrame(PtFrameResult& Out)
	{
		// UE4 dispatches the buffered simulation events at EndFrame: here the kept contacts of sleeping islands are updated.
		if (EventCallback && bStepped)
		{
			UpdateSleepingContacts();
		}
		CheckPhysXErrors(false);
		TotalErrorBits |= FrameErrorBits;
		Out.NumStepsTaken = bStepped ? 1u : 0u;
		Out.EngineErrorBits = FrameErrorBits;
		return PT_OK;
	}

	// ---- kept sleeping contacts (the Jolt backend's rules, Progress M4) ----------------------------------------------
	bool FPhysScene_PhysX::IsSleepingPair(const FPhysXRawContact& Contact) const
	{
		if (Contact.Body0 >= Bodies.size() || Contact.Body1 >= Bodies.size())
		{
			return false;
		}
		const auto IsAsleepOrNotDynamic = [this](const uint32 Index)
		{
			const FPhysXBody& Record = Bodies[Index];
			return !Record.bReleased && (!Record.IsSimulatedDynamic() || IsAsleep(Record));
		};
		const bool bAnyDynamic = Bodies[Contact.Body0].IsSimulatedDynamic() || Bodies[Contact.Body1].IsSimulatedDynamic();
		return bAnyDynamic && IsAsleepOrNotDynamic(Contact.Body0) && IsAsleepOrNotDynamic(Contact.Body1);
	}

	void FPhysScene_PhysX::NoteKeptContactCommand(const uint32 BodyIndex, const bool bPutToSleep, const bool bPoseSet)
	{
		if (!EventCallback)
		{
			return;   // bench runs keep no contacts
		}
		FPhysXBody& Record = Bodies[BodyIndex];
		Record.bPutToSleepByCommand = Record.bPutToSleepByCommand || bPutToSleep;
		Record.bPoseSetByCommand = Record.bPoseSetByCommand || bPoseSet;
		if (!Record.bKeptContactsListed)
		{
			Record.bKeptContactsListed = true;
			KeptContactBodies.pushBack(BodyIndex);   // reserved to the body count: each body is listed once
		}
	}

	void FPhysScene_PhysX::FlushKeptContactCommands()
	{
		// 1. The last step's reports, each to exactly one place. A pair of a body put to sleep since that step is kept now (as
		//    if the body had fallen asleep in it); every other report becomes the previous step's, from which
		//    UpdateSleepingContacts takes a pair that falls asleep in the coming step. (Before M5 round 0 every report also went
		//    to PrevStepContacts, so UpdateSleepingContacts kept a commanded pair a second time: each point of a body put to
		//    sleep by PT_CMD_PUT_TO_SLEEP was listed twice for as long as it slept. Sc::BodySim::putToSleep deactivates the
		//    body at once, so the pair is already a sleeping pair here.) Both arrays are sized once: never grown.
		bool bAnyPutToSleep = false;
		for (const uint32 Index : KeptContactBodies)
		{
			bAnyPutToSleep = bAnyPutToSleep || Bodies[Index].bPutToSleepByCommand;
		}
		PrevStepContacts.clear();
		const FPhysXRawContact* Stepped = EventCallback->GetContacts();
		const uint32 NumStepped = EventCallback->GetNumContacts();
		for (uint32 Index = 0; Index < NumStepped; ++Index)
		{
			const FPhysXRawContact& Contact = Stepped[Index];
			const bool bTouchesPutToSleep = bAnyPutToSleep && ((Contact.Body0 < Bodies.size() && Bodies[Contact.Body0].bPutToSleepByCommand)
				|| (Contact.Body1 < Bodies.size() && Bodies[Contact.Body1].bPutToSleepByCommand));
			if (bTouchesPutToSleep && IsSleepingPair(Contact))
			{
				if (SleepingContacts.size() < SleepingContacts.capacity())
				{
					SleepingContacts.pushBack(Contact);
				}
				else
				{
					bSleepingContactsDropped = true;
				}
				continue;
			}
			if (PrevStepContacts.size() < PrevStepContacts.capacity())
			{
				PrevStepContacts.pushBack(Contact);
			}
		}
		if (KeptContactBodies.empty())
		{
			return;
		}
		// 2. Drop the kept pairs of a body whose pose a command set (stale points) and those no longer sleeping.
		uint32 NumKept = 0;
		for (uint32 Index = 0; Index < SleepingContacts.size(); ++Index)
		{
			const FPhysXRawContact& Contact = SleepingContacts[Index];
			if (!IsSleepingPair(Contact) || Bodies[Contact.Body0].bPoseSetByCommand || Bodies[Contact.Body1].bPoseSetByCommand)
			{
				continue;
			}
			SleepingContacts[NumKept++] = Contact;
		}
		SleepingContacts.forceSize_Unsafe(NumKept);
		for (const uint32 Index : KeptContactBodies)
		{
			FPhysXBody& Record = Bodies[Index];
			Record.bKeptContactsListed = false;
			Record.bPutToSleepByCommand = false;
			Record.bPoseSetByCommand = false;
		}
		KeptContactBodies.clear();
	}

	void FPhysScene_PhysX::UpdateSleepingContacts()
	{
		// A pair is kept while it has a dynamic body and every dynamic body of it is asleep. PhysX wakes a sleeping body that
		// an awake one touches, so a kept pair is never also in the step's reports. Unlike Jolt, PhysX already reports nothing
		// for an island in the step it falls asleep in (M5 [V]: M1.SingleBoxDrop a reports 4 points every step until the box
		// sleeps, then none), so a pair that became a sleeping pair in this step is taken from the previous step's reports -
		// unless a dynamic body of it was reported in this step, which then has the newer points. A pair kept by a command is
		// not among the previous step's reports (FlushKeptContactCommands), so no pair is listed twice.
		SleepingScratch.clear();
		for (const FPhysXRawContact& Contact : SleepingContacts)
		{
			if (IsSleepingPair(Contact))
			{
				SleepingScratch.pushBack(Contact);
			}
		}
		const FPhysXRawContact* Stepped = EventCallback->GetContacts();
		const uint32 NumStepped = EventCallback->GetNumContacts();
		const uint32 Stamp = ++ReportStamp;
		const auto StampBody = [this, Stamp](const uint32 Index)
		{
			if (Index < ReportStamps.size())
			{
				ReportStamps[Index] = Stamp;
			}
		};
		const auto ReportedNow = [this, Stamp](const uint32 Index)
		{
			return Index < ReportStamps.size() && Bodies[Index].IsSimulatedDynamic() && ReportStamps[Index] == Stamp;
		};
		bSleepingContactsDropped = false;
		for (uint32 Index = 0; Index < NumStepped; ++Index)
		{
			StampBody(Stepped[Index].Body0);
			StampBody(Stepped[Index].Body1);
		}
		const auto Keep = [this](const FPhysXRawContact& Contact) -> bool
		{
			if (SleepingScratch.size() >= SleepingScratch.capacity())
			{
				bSleepingContactsDropped = true;   // never grow: the arrays are sized once
				return false;
			}
			SleepingScratch.pushBack(Contact);
			return true;
		};
		for (uint32 Index = 0; Index < NumStepped; ++Index)
		{
			if (IsSleepingPair(Stepped[Index]) && !Keep(Stepped[Index]))
			{
				break;
			}
		}
		for (const FPhysXRawContact& Contact : PrevStepContacts)
		{
			if (IsSleepingPair(Contact) && !ReportedNow(Contact.Body0) && !ReportedNow(Contact.Body1) && !Keep(Contact))
			{
				break;
			}
		}
		SleepingContacts.swap(SleepingScratch);
		// Taken once: a pair kept now is in SleepingContacts from here on.
		PrevStepContacts.clear();
	}

	// ---- commands ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_PhysX::ValidateBodyCommand(const PtBodyCommand& Cmd)
	{
		// Every check that can reject a command is here, so ApplyBodyCommand never fails (batch atomicity, docs/Abi.md 5.5).
		if (Cmd.Type == PT_CMD_SET_KINEMATIC_TARGET && Bodies[Cmd.Body].Motion != PT_MOTION_KINEMATIC)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "PhysX: SET_KINEMATIC_TARGET on body %u, which is %s", Cmd.Body, GetMotionName(Bodies[Cmd.Body].Motion));
		}
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::ApplyBodyCommand(const PtBodyCommand& Cmd)
	{
		FPhysXBody& Record = Bodies[Cmd.Body];
		const bool bDynamic = Record.IsSimulatedDynamic();
		physx::PxRigidDynamic* Dynamic = Record.Dynamic;
		const auto Wake = [&]()
		{
			if (bDynamic && Dynamic->isSleeping())
			{
				Dynamic->wakeUp();
				MarkDirty(Cmd.Body);
				NoteKeptContactCommand(Cmd.Body, false, false);   // its kept pairs go at the next flush
			}
		};
		switch (Cmd.Type)
		{
		case PT_CMD_SET_GLOBAL_POSE:
		{
			// UE5's teleport wakes a sleeping body whatever bAutoWake says (SetX, SingleParticlePhysicsProxy.h:240-251 [V]);
			// a kinematic body also gets the pose as its target with zero velocity (ChaosEngineInterface.cpp:2550-2557 [V]).
			Record.Actor->setGlobalPose(U2PTransform(Cmd.Transform, Scale), bDynamic);
			if (Record.Motion == PT_MOTION_KINEMATIC)
			{
				Record.KinematicTarget = Cmd.Transform;
				Record.KinematicOriginVelocity = PtVec3{ 0.0, 0.0, 0.0 };
				Record.KinematicAngularVelocity = PtVec3{ 0.0, 0.0, 0.0 };
				Record.bHasKinematicTarget = false;
				Record.bKinematicMovedLastStep = false;
			}
			MarkDirty(Cmd.Body);
			NoteKeptContactCommand(Cmd.Body, false, true);
			break;
		}
		case PT_CMD_SET_KINEMATIC_TARGET:
			Record.KinematicTarget = Cmd.Transform;   // the last target before a step wins
			Record.bHasKinematicTarget = true;
			break;
		case PT_CMD_SET_LINEAR_VELOCITY:
		case PT_CMD_SET_ANGULAR_VELOCITY:
			if (bDynamic)
			{
				// ABI v1.0 contract (docs/Abi.md 5.10): a velocity write wakes a sleeping body whatever PT_CMDF_AUTO_WAKE says, as
				// UE 5.8.3's game-thread SetV / SetW do (SingleParticlePhysicsProxy.h:322-340, :362-380, :1217-1244 [V]). PhysX
				// itself would leave a body asleep for a zero vector without autowake (NpRigidDynamic.cpp:253, :286, :605-622 [V]).
				const bool bWasAsleep = Dynamic->isSleeping();
				if (Cmd.Type == PT_CMD_SET_LINEAR_VELOCITY)
				{
					Dynamic->setLinearVelocity(U2PVector(Cmd.Vector, Scale), false);
				}
				else
				{
					Dynamic->setAngularVelocity(U2PUnitless(Cmd.Vector), false);
				}
				MarkDirty(Cmd.Body);
				if (Dynamic->isSleeping())
				{
					Wake();
				}
				else if (bWasAsleep)
				{
					NoteKeptContactCommand(Cmd.Body, false, false);   // a non-zero vector already woke it: its kept pairs go too
				}
			}
			break;
		case PT_CMD_SET_BODY_STATE:
			Record.Actor->setGlobalPose(U2PTransform(Cmd.Transform, Scale), false);
			if (bDynamic)
			{
				Dynamic->setLinearVelocity(U2PVector(Cmd.Vector, Scale), false);
				Dynamic->setAngularVelocity(U2PUnitless(Cmd.Vector2), false);
				Wake();   // ABI v1.0: a state write wakes a sleeping body (UE's SetX / SetV, docs/Abi.md 5.10)
			}
			MarkDirty(Cmd.Body);
			NoteKeptContactCommand(Cmd.Body, false, true);
			break;
		case PT_CMD_ADD_IMPULSE:
		case PT_CMD_ADD_VELOCITY:
			if (bDynamic)
			{
				Wake();   // [R] the Chaos game-thread impulse setter wakes the particle (as Null)
				// Deferred to the step (ApplyPendingDynamics). FChaosStateOps::AddVelocity is AddImpulse(M * dv)
				// (ChaosEngineInterface.cpp:861-864 [V]): dv as it is.
				const physx::PxVec3 DeltaV = Cmd.Type == PT_CMD_ADD_IMPULSE ? U2PVector(Cmd.Vector, Scale) * Dynamic->getInvMass() : U2PVector(Cmd.Vector, Scale);
				Record.PendingLinearVelocity += DeltaV;
				if (!Record.bPendingImpulse)
				{
					Record.bPendingImpulse = true;
					PendingImpulseBodies.pushBack(Cmd.Body);
				}
			}
			break;
		case PT_CMD_ADD_ANGULAR_IMPULSE:
		case PT_CMD_ADD_ANGULAR_VELOCITY:
			if (bDynamic)
			{
				Wake();
				// The angular impulse through the world inverse inertia at push time, as Null; AddAngularVelocityInRadians is an
				// angular impulse of I_world * dw (ChaosEngineInterface.cpp:866-870 [V]): dw as it is.
				const physx::PxVec3 DeltaW = Cmd.Type == PT_CMD_ADD_ANGULAR_IMPULSE ? MultiplyWorldInverseInertia(*Dynamic, U2PInertialVector(Cmd.Vector, Scale))
					: U2PUnitless(Cmd.Vector);
				Record.PendingAngularVelocity += DeltaW;
				if (!Record.bPendingImpulse)
				{
					Record.bPendingImpulse = true;
					PendingImpulseBodies.pushBack(Cmd.Body);
				}
			}
			break;
		case PT_CMD_ADD_FORCE:
		case PT_CMD_ADD_TORQUE:
			if (bDynamic)
			{
				if ((Cmd.Flags & PT_CMDF_NO_WAKE) != 0 && Dynamic->isSleeping())
				{
					break;   // asleep when the force arrives: the host sent UE's game-thread wake where it was issued (docs/Abi.md 5.10)
				}
				if ((Cmd.Flags & PT_CMDF_NO_WAKE) == 0)
				{
					Wake();   // UE's AddForce / AddTorque wake on every call (SingleParticlePhysicsProxy.h:706-720, :757-771 [V])
				}
				const bool bAccelChange = (Cmd.Flags & PT_CMDF_ACCEL_CHANGE) != 0;
				if (Cmd.Type == PT_CMD_ADD_FORCE)
				{
					// bAccelChange: Force * M (ChaosEngineInterface.cpp:940-944 [V]).
					const physx::PxVec3 Force = U2PVector(Cmd.Vector, Scale);
					Record.PendingForce += bAccelChange ? Force * Dynamic->getMass() : Force;
				}
				else
				{
					// bAccelChange: I_world * torque at push time (ChaosEngineInterface.cpp:1029-1032 [V]).
					Record.PendingTorque += bAccelChange ? MultiplyWorldInertia(*Dynamic, U2PUnitless(Cmd.Vector)) : U2PInertialVector(Cmd.Vector, Scale);
				}
				if (!Record.bPendingForce)
				{
					Record.bPendingForce = true;
					PendingForceBodies.pushBack(Cmd.Body);
				}
			}
			break;
		case PT_CMD_WAKE_UP:
			Wake();
			break;
		case PT_CMD_PUT_TO_SLEEP:
			// Ignored when the scene disables sleeping (PhysX rejects putToSleep there, NpRigidDynamic.cpp:521-525 [V]).
			if (bDynamic && bSleepingAllowed && !Dynamic->isSleeping())
			{
				// PhysX zeroes the velocities and clears the force accumulators (as Null / Chaos). The deferred impulse stays
				// pending, the deferred force is dropped at the step unless a later command of this push wakes the body again.
				Dynamic->putToSleep();
				MarkDirty(Cmd.Body);
				NoteKeptContactCommand(Cmd.Body, true, false);
			}
			break;
		default:
			break;   // the kit validated the type
		}
		return PT_OK;
	}

	// ---- reads -------------------------------------------------------------------------------------------------------
	PtBodyState FPhysScene_PhysX::MakeState(const FPhysXBody& Record) const
	{
		PtBodyState State = {};
		State.ActorTransform = P2UTransform(Record.Actor->getGlobalPose(), Scale);
		if (Record.Motion == PT_MOTION_KINEMATIC)
		{
			// The origin's displacement / dt of the last target step, zero otherwise (as Chaos reports it).
			State.LinearVelocity = Record.bKinematicMovedLastStep ? Record.KinematicOriginVelocity : PtVec3{ 0.0, 0.0, 0.0 };
			State.AngularVelocity = Record.bKinematicMovedLastStep ? Record.KinematicAngularVelocity : PtVec3{ 0.0, 0.0, 0.0 };
		}
		else if (Record.Motion == PT_MOTION_DYNAMIC)
		{
			State.LinearVelocity = P2UVector(Record.Dynamic->getLinearVelocity(), Scale);   // the COM's velocity
			State.AngularVelocity = P2UUnitless(Record.Dynamic->getAngularVelocity());
		}
		State.ObjectState = Record.Motion == PT_MOTION_STATIC ? PT_OBJECT_STATE_STATIC
			: (Record.Motion == PT_MOTION_KINEMATIC ? PT_OBJECT_STATE_KINEMATIC : (IsAsleep(Record) ? PT_OBJECT_STATE_SLEEPING : PT_OBJECT_STATE_DYNAMIC));
		State.Flags = 0;
		return State;
	}

	PtResult FPhysScene_PhysX::ReadBodyStates(const uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, const uint32_t Capacity, uint32_t& OutCount)
	{
		// The bodies that moved in the last step: PhysX's active actors (awake dynamic bodies; kinematic ones are excluded,
		// eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS) plus the kinematic bodies a target moved. Without active actors
		// (SdkDefault: the flag is off by default, PxSceneDesc.h:1084 [V]) every awake dynamic body.
		ActiveScratch.clear();
		if (Config.bEnableActiveActors)
		{
			physx::PxU32 NumActive = 0;
			physx::PxActor** Active = Scene->getActiveActors(NumActive);
			for (physx::PxU32 Index = 0; Index < NumActive; ++Index)
			{
				const FPhysxUserData Data = FPhysxUserData::Decode(Active[Index]->userData);
				if (Data.Type == EPhysxUserDataType::Body && Data.Index < Bodies.size())
				{
					ActiveScratch.pushBack(Data.Index);
				}
			}
		}
		else
		{
			for (uint32 Index = 0; Index < Bodies.size(); ++Index)
			{
				const FPhysXBody& Record = Bodies[Index];
				if (!Record.bReleased && Record.IsSimulatedDynamic() && !IsAsleep(Record))
				{
					ActiveScratch.pushBack(Index);
				}
			}
		}
		for (const uint32 Index : Kinematics)
		{
			if (!Bodies[Index].bReleased && Bodies[Index].bKinematicMovedLastStep)
			{
				ActiveScratch.pushBack(Index);
			}
		}

		if (ReadFlags == PT_READ_ALL)
		{
			// Dense: every created body in index order, a released one as UNINITIALIZED (the kit writes OutIndices).
			OutCount = Bodies.size();
			if (Capacity < OutCount)
			{
				return PT_E_CAPACITY;
			}
			for (uint32 Index = 0; Index < OutCount; ++Index)
			{
				FPhysXBody& Record = Bodies[Index];
				if (Record.bReleased)
				{
					PtBodyState Released = {};
					Released.ActorTransform.Rotation.W = 1.0;
					Released.ObjectState = PT_OBJECT_STATE_UNINITIALIZED;
					Out[Index] = Released;
					continue;
				}
				Out[Index] = MakeState(Record);
				Record.bDirty = false;
			}
			PrevActive.swap(ActiveScratch);
			DirtyBodies.clear();
			return PT_OK;
		}

		// ACTIVE_ONLY: the active bodies, the bodies of the previous report that left the list (fell asleep or stopped) and
		// the bodies a command changed, deduplicated by a stamp (which advances even when the call fails).
		const uint32 Stamp = ++ReadStamp;
		ReadScratch.clear();
		const auto Add = [this, Stamp](const uint32 Index)
		{
			FPhysXBody& Record = Bodies[Index];
			if (!Record.bReleased && Record.LastReadStamp != Stamp)
			{
				Record.LastReadStamp = Stamp;
				ReadScratch.pushBack(Index);
			}
		};
		for (const uint32 Index : ActiveScratch)
		{
			Add(Index);
		}
		for (const uint32 Index : PrevActive)
		{
			Add(Index);
		}
		for (const uint32 Index : DirtyBodies)
		{
			Add(Index);
		}
		OutCount = ReadScratch.size();
		if (Capacity < OutCount)
		{
			return PT_E_CAPACITY;   // nothing reported, nothing consumed
		}
		for (uint32 Slot = 0; Slot < OutCount; ++Slot)
		{
			const uint32 Index = ReadScratch[Slot];
			OutIndices[Slot] = Index;
			Out[Slot] = MakeState(Bodies[Index]);
			Bodies[Index].bDirty = false;
		}
		PrevActive.swap(ActiveScratch);
		DirtyBodies.clear();
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::ReadMassProperties(const uint32_t First, const uint32_t Num, PtMassProperties* Out)
	{
		for (uint32 Offset = 0; Offset < Num; ++Offset)
		{
			const FPhysXBody& Record = Bodies[First + Offset];
			PtMassProperties& Mass = Out[Offset];
			if (Record.Dynamic == nullptr)
			{
				// Static bodies have no mass in PhysX: the harness values.
				Mass.Mass = Record.Mass;
				Mass.InertiaDiagonal = Record.InertiaDiagonal;
				Mass.CenterOfMassLocal = Record.CenterOfMassLocal;
				continue;
			}
			// What the engine has (G2 readback): PhysX keeps the given principal moments and mass frame as they are.
			const physx::PxVec3 Inertia = Record.Dynamic->getMassSpaceInertiaTensor();
			Mass.Mass = static_cast<double>(Record.Dynamic->getMass());
			Mass.InertiaDiagonal = PtVec3{ Scale.ToKgCm2(static_cast<double>(Inertia.x)), Scale.ToKgCm2(static_cast<double>(Inertia.y)), Scale.ToKgCm2(static_cast<double>(Inertia.z)) };
			Mass.CenterOfMassLocal = P2UTransform(Record.Dynamic->getCMassLocalPose(), Scale);
		}
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::GetStepStats(PtStepStats& Out)
	{
		CheckPhysXErrors(true);   // reads and snapshots after the last frame (the bench reads the stats after its loop)
		int32 Awake = 0;
		int32 Asleep = 0;
		for (const FPhysXBody& Record : Bodies)
		{
			if (Record.bReleased || !Record.IsSimulatedDynamic())
			{
				continue;
			}
			(IsAsleep(Record) ? Asleep : Awake) += 1;
		}
		Out.NumActiveBodies = Awake;
		Out.NumSleepingBodies = Asleep;
		if (EventCallback)
		{
			Out.NumContactPairs = static_cast<int32>(EventCallback->GetNumPairs());
			Out.NumContactPoints = static_cast<int32>(EventCallback->GetNumContacts());
		}
		Out.NumIslands = -1;   // PhysX exposes no island ids
		Out.EngineErrorBits = TotalErrorBits | PendingErrorBits;
		Out.NumPhaseTimes = 0;
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::CaptureDebugSnapshot(const PtDebugSnapshotRequest& Req, PtDebugSnapshotCounts& Out)
	{
		// Stage the snapshot now, so that ReadDebugSnapshot copies exactly what these counts describe.
		SnapshotParticles.clear();
		SnapshotContacts.clear();
		SnapshotJoints.clear();
		SnapshotCommands.clear();
		bool bTruncated = false;
		if ((Req.Flags & PT_SNAP_PARTICLES) != 0)
		{
			for (uint32 Index = 0; Index < Bodies.size(); ++Index)
			{
				const FPhysXBody& Record = Bodies[Index];
				if (Record.bReleased)
				{
					continue;
				}
				const PtBodyState State = MakeState(Record);
				PtParticleSnapshot Particle = {};
				Particle.ActorTransform = State.ActorTransform;
				const physx::PxTransform Pose = Record.Actor->getGlobalPose();
				const physx::PxTransform Com = Record.Dynamic != nullptr ? Pose * Record.Dynamic->getCMassLocalPose() : Pose;
				Particle.CenterOfMassWorld = P2UTransform(physx::PxTransform(Com.p, Com.q.getNormalized()), Scale);
				const physx::PxBounds3 Bounds = Record.Actor->getWorldBounds(1.0f);
				Particle.BoundsMin = P2UVector(Bounds.minimum, Scale);
				Particle.BoundsMax = P2UVector(Bounds.maximum, Scale);
				Particle.LinearVelocity = State.LinearVelocity;
				Particle.AngularVelocity = State.AngularVelocity;
				Particle.BodyIndex = Index;
				Particle.ObjectState = static_cast<uint8>(State.ObjectState);
				Particle.NativeIslandId = -1;
				SnapshotParticles.pushBack(Particle);
			}
		}
		if ((Req.Flags & PT_SNAP_CONTACTS) != 0 && EventCallback)
		{
			bTruncated = bTruncated || EventCallback->HasDropped() || bSleepingContactsDropped;
			const double NaN = nan("");
			// The last step's contacts, then the kept contacts of sleeping islands.
			const FPhysXRawContact* RawContacts = EventCallback->GetContacts();
			const uint32 NumStepped = EventCallback->GetNumContacts();
			const uint32 NumRaw = NumStepped + SleepingContacts.size();
			// Commands since the last step (FlushKeptContactCommands runs at the next StartFrame): a kept pair a command woke
			// or moved is left out here already.
			const bool bKeptCommandsPending = !KeptContactBodies.empty();
			for (uint32 RawIndex = 0; RawIndex < NumRaw; ++RawIndex)
			{
				const bool bSleeping = RawIndex >= NumStepped;
				const FPhysXRawContact& Raw = bSleeping ? SleepingContacts[RawIndex - NumStepped] : RawContacts[RawIndex];
				if (Raw.Body0 >= Bodies.size() || Raw.Body1 >= Bodies.size() || Bodies[Raw.Body0].bReleased || Bodies[Raw.Body1].bReleased)
				{
					continue;
				}
				if (bSleeping && bKeptCommandsPending && (!IsSleepingPair(Raw) || Bodies[Raw.Body0].bPoseSetByCommand || Bodies[Raw.Body1].bPoseSetByCommand))
				{
					continue;
				}
				if (SnapshotContacts.size() >= Req.MaxContactPoints)
				{
					bTruncated = true;
					break;
				}
				PtContactPoint Contact = {};
				const PtVec3 Normal{ Raw.Normal[0], Raw.Normal[1], Raw.Normal[2] };
				// PointOn1 on the report's second shape (found by the callback, M6 contract (c)), PointOn0 = PointOn1 + Phi n.
				Contact.PointOn1 = PtVec3{ Scale.ToCm(Raw.PointOn1[0]), Scale.ToCm(Raw.PointOn1[1]), Scale.ToCm(Raw.PointOn1[2]) };
				const double PhiCm = Scale.ToCm(Raw.Separation);
				Contact.PointOn0 = PtVec3{ Contact.PointOn1.X + PhiCm * Normal.X, Contact.PointOn1.Y + PhiCm * Normal.Y, Contact.PointOn1.Z + PhiCm * Normal.Z };
				Contact.Normal = Normal;
				Contact.Phi = PhiCm;
				Contact.NormalImpulse = NaN;                 // Q8 (PxContactPairPoint::impulse: M9)
				Contact.FrictionImpulse = PtVec3{ NaN, NaN, NaN };
				Contact.Body0 = Raw.Body0;
				Contact.Body1 = Raw.Body1;
				Contact.Shape0 = Raw.Shape0;
				Contact.Shape1 = Raw.Shape1;
				// PhysX's normal points from the second shape toward the first, i.e. lies on Body1 and points toward Body0, and
				// separation < 0 penetrates: already the Chaos convention (ContactPoint.h:49-53 [V]); then Body0 = the more
				// mobile body.
				Abi::CanonicalizeContact(Contact, MobilityRanks.begin(), false, false);
				// "Active" is the solver's own answer, as Chaos' bIsActive (NetImpulse / NetPushOut, ChaosDebugDraw.cpp:1453 [V]):
				// the point carried a normal impulse in its last solved step (PxContactPairPoint::impulse; PhysX's velocity
				// solver includes the penetration bias). A resting PhysX contact sits at Phi ~ 0 (+/- float noise), so Phi <= 0
				// would be a coin toss there. A kept sleeping contact keeps the flags of its last step.
				Contact.Flags |= Raw.NormalImpulse > 0.0 ? PT_CONTACT_ACTIVE : (Contact.Phi > 0.0 ? PT_CONTACT_SPECULATIVE : 0u);
				Contact.Flags |= bSleeping ? PT_CONTACT_SLEEPING : 0u;
				SnapshotContacts.pushBack(Contact);
			}
		}
		// M9: every live joint with the engine's world frames and the impulse on Body1 (PT_CAP_JOINT_SNAPSHOT, PhysXConstraint.cpp).
		if ((Req.Flags & PT_SNAP_JOINTS) != 0)
		{
			StageJointSnapshots();
		}
		// M9 native debug draw: PhysX's render buffer of the last step (viz runs; PhysXDebugRender.h). Elsewhere eSCALE is 0 and
		// the buffer is empty. Req.NativeDrawCategories is not used: the categories are the variant's DebugVisualization, fixed
		// when the scene is created (PhysX draws during simulate, so a capture cannot select what the last step drew).
		if ((Req.Flags & PT_SNAP_NATIVE_DRAW) != 0 && bVisualization)
		{
			LastCaptureCommands = ConvertRenderBuffer(Scene->getRenderBuffer(), Scale, Req.MaxNativeCommands, SnapshotCommands, bTruncated);
			++NumNativeCaptures;
		}
		Out.NumParticles = SnapshotParticles.size();
		Out.NumContactPoints = SnapshotContacts.size();
		Out.NumJoints = SnapshotJoints.size();
		Out.NumNativeCommands = SnapshotCommands.size();
		Out.NumStringBytes = 0;   // PhysX 5.11's render buffer holds no text
		Out.bTruncated = bTruncated ? 1u : 0u;
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::ReadDebugSnapshot(const PtDebugSnapshotBuffers& Buffers)
	{
		if (Buffers.ParticleCapacity < SnapshotParticles.size() || Buffers.ContactCapacity < SnapshotContacts.size() || Buffers.JointCapacity < SnapshotJoints.size()
			|| Buffers.CommandCapacity < SnapshotCommands.size())
		{
			return GetKit().SetLastError(PT_E_CAPACITY, "PhysX: ReadDebugSnapshot needs %u particles / %u contacts / %u joints / %u native commands (capacity %u / %u / %u / %u)",
				SnapshotParticles.size(), SnapshotContacts.size(), SnapshotJoints.size(), SnapshotCommands.size(), Buffers.ParticleCapacity, Buffers.ContactCapacity,
				Buffers.JointCapacity, Buffers.CommandCapacity);
		}
		if ((!SnapshotParticles.empty() && (Buffers.Particles == nullptr || Buffers.ParticleStride < sizeof(PtParticleSnapshot)))
			|| (!SnapshotContacts.empty() && (Buffers.ContactPoints == nullptr || Buffers.ContactStride < sizeof(PtContactPoint)))
			|| (!SnapshotJoints.empty() && (Buffers.Joints == nullptr || Buffers.JointStride < sizeof(PtJointSnapshot)))
			|| (!SnapshotCommands.empty() && (Buffers.NativeCommands == nullptr || Buffers.CommandStride < sizeof(PtDebugDrawCmd))))
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "PhysX: ReadDebugSnapshot buffers are NULL or their strides are too small");
		}
		for (uint32 Index = 0; Index < SnapshotParticles.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.Particles) + static_cast<size_t>(Index) * Buffers.ParticleStride, &SnapshotParticles[Index], sizeof(PtParticleSnapshot));
		}
		for (uint32 Index = 0; Index < SnapshotContacts.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.ContactPoints) + static_cast<size_t>(Index) * Buffers.ContactStride, &SnapshotContacts[Index], sizeof(PtContactPoint));
		}
		for (uint32 Index = 0; Index < SnapshotJoints.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.Joints) + static_cast<size_t>(Index) * Buffers.JointStride, &SnapshotJoints[Index], sizeof(PtJointSnapshot));
		}
		for (uint32 Index = 0; Index < SnapshotCommands.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.NativeCommands) + static_cast<size_t>(Index) * Buffers.CommandStride, &SnapshotCommands[Index], sizeof(PtDebugDrawCmd));
		}
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::ComputeNativeStateHash(uint64_t& OutHash)
	{
		// FNV-1a over each live body's actor pose as PhysX has it (float, written as double in cm), in body index order
		// (02 7.2 #21). Engine-specific: never compared across engines.
		uint64 Hash = 0xcbf29ce484222325ull;
		for (const FPhysXBody& Record : Bodies)
		{
			if (Record.bReleased)
			{
				continue;
			}
			const physx::PxTransform Pose = Record.Actor->getGlobalPose();
			const double Values[7] = { Scale.ToCm(static_cast<double>(Pose.p.x)), Scale.ToCm(static_cast<double>(Pose.p.y)), Scale.ToCm(static_cast<double>(Pose.p.z)),
				static_cast<double>(Pose.q.x), static_cast<double>(Pose.q.y), static_cast<double>(Pose.q.z), static_cast<double>(Pose.q.w) };
			Hash = Abi::Fnv1a64(Values, sizeof(Values), Hash);
		}
		OutHash = Hash;
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::SetOption(const Abi::FStringRef Key, const Abi::FStringRef Value)
	{
		// Harness test hook, runtime SetOption only (CreateScene rejects the key): one PhysX error report through the
		// foundation, counted by FPhysXErrorCallback, so the next frame reports eINVALID_OPERATION (0x8).
		if (EqualsNoCase(GPhysXTestReportErrorKey, Key))
		{
			GetPhysXFoundation().error(physx::PxErrorCode::eINVALID_OPERATION, __FILE__, __LINE__, "harness test report (%s)", GPhysXTestReportErrorKey);
			return PT_OK;
		}
		if (!IsRuntimeSceneOption(Key))
		{
			FPhysXSceneConfig Probe = Config;
			char Error[256] = {};
			if (ApplySceneOption(Probe, Key, Value, Error) != PT_OK)
			{
				return GetKit().SetLastError(PT_E_INVALID_ARG, "%s", Error);
			}
			return GetKit().SetLastError(PT_E_INVALID_ARG, "PhysX: scene option '%.*s' can only be given at CreateScene (runtime keys: BounceThresholdVelocity, "
				"DynamicTreeRebuildRateHint)", static_cast<int>(Key.Length), Key.Data);
		}
		FPhysXSceneConfig Updated = Config;
		char Error[256] = {};
		if (ApplySceneOption(Updated, Key, Value, Error) != PT_OK)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "%s", Error);
		}
		Config = Updated;
		if (Desc.RestitutionThreshold <= 0.0)
		{
			Scene->setBounceThresholdVelocity(static_cast<float>(Scale.Length(Config.BounceThresholdVelocityCm)));
		}
		Scene->setDynamicTreeRebuildRateHint(Config.DynamicTreeRebuildRateHint);
		return PT_OK;
	}

	PtResult FPhysScene_PhysX::DumpEffectiveSettings(Abi::FTextWriter& Out)
	{
		const double NaN = nan("");
		const double L2 = Config.LengthScale * Config.LengthScale;
		const physx::PxSceneFlags Flags = Scene->getFlags();
		const physx::PxTolerancesScale& Tolerances = GetPhysXSDK().getTolerancesScale();
		char Text[512];
		char VecText[128];

		const PtVec3 GravityCm = P2UVector(Scene->getGravity(), Scale);
		FormatVec(VecText, GravityCm);
		snprintf(Text, sizeof(Text), "%s cm/s^2 (PxScene::getGravity)", VecText);
		Out.AppendEffective("world.gravity", Text, "cm/s^2", GravityCm.Z, "cm/s^2", "READBACK");
		snprintf(Text, sizeof(Text), "%s, stabilization %s, PCM %s (PxScene::getSolverType / getFlags)", ToString(Scene->getSolverType()),
			(Flags & physx::PxSceneFlag::eENABLE_STABILIZATION) ? "on" : "off", (Flags & physx::PxSceneFlag::eENABLE_PCM) ? "on" : "off");
		Out.AppendEffective("solver.type", Text, "", NaN, "", "READBACK");

		// Per-body values read back from the engine (dynamic bodies).
		FRange PositionIters;
		FRange VelocityIters;
		FRange Linear;
		FRange Angular;
		FRange MaxAngular;
		FRange MaxLinear;
		FRange SleepThreshold;
		FRange StabThreshold;
		FRange Depenetration;
		uint32 NumDynamic = 0;
		uint32 NumGyroscopic = 0;
		uint32 NumCcd = 0;
		for (const FPhysXBody& Record : Bodies)
		{
			if (Record.bReleased || !Record.IsSimulatedDynamic())
			{
				continue;
			}
			const physx::PxRigidDynamic& Dynamic = *Record.Dynamic;
			++NumDynamic;
			physx::PxU32 Position = 0;
			physx::PxU32 Velocity = 0;
			Dynamic.getSolverIterationCounts(Position, Velocity);
			PositionIters.Add(static_cast<double>(Position));
			VelocityIters.Add(static_cast<double>(Velocity));
			Linear.Add(static_cast<double>(Dynamic.getLinearDamping()));
			Angular.Add(static_cast<double>(Dynamic.getAngularDamping()));
			MaxAngular.Add(static_cast<double>(Dynamic.getMaxAngularVelocity()));
			MaxLinear.Add(Scale.ToCm(static_cast<double>(Dynamic.getMaxLinearVelocity())));
			SleepThreshold.Add(static_cast<double>(Dynamic.getSleepThreshold()) / L2);   // mass-normalised kinetic energy, cm^2/s^2
			StabThreshold.Add(static_cast<double>(Dynamic.getStabilizationThreshold()) / L2);
			Depenetration.Add(Scale.ToCm(static_cast<double>(Dynamic.getMaxDepenetrationVelocity())));
			NumGyroscopic += (Dynamic.getRigidBodyFlags() & physx::PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES) ? 1u : 0u;
			NumCcd += (Dynamic.getRigidBodyFlags() & physx::PxRigidBodyFlag::eENABLE_CCD) ? 1u : 0u;
		}
		snprintf(Text, sizeof(Text), "position [%.9g, %.9g] / velocity [%.9g, %.9g] per body (PxRigidDynamic::getSolverIterationCounts, %u dynamic bodies; "
			"%u overrides; variant default %u/%u)", PositionIters.Min, PositionIters.Max, VelocityIters.Min, VelocityIters.Max, NumDynamic, NumIterationOverrides,
			Config.PositionIterations, Config.VelocityIterations);
		Out.AppendEffective("solver.iterations", Text, "iterations", PositionIters.bAny ? PositionIters.Max : NaN, "iterations", "READBACK");
		snprintf(Text, sizeof(Text), "%s (PxScene::getBroadPhaseType)", ToString(Scene->getBroadPhaseType()));
		Out.AppendEffective("broadphase.type", Text, "", NaN, "", "READBACK");
		const double BounceCm = Scale.ToCm(static_cast<double>(Scene->getBounceThresholdVelocity()));
		snprintf(Text, sizeof(Text), "%.9g native units/s (PxScene::getBounceThresholdVelocity)", static_cast<double>(Scene->getBounceThresholdVelocity()));
		Out.AppendEffective("contact.restitution_threshold", Text, "native/s", BounceCm, "cm/s", "READBACK");
		// Contact and rest offsets as the shapes hold them, and how many follow the variant's rule (checked against each
		// shape's own geometry: MinExtent = radius or smallest half extent).
		FRange ContactOffset;
		FRange RestOffset;
		uint32 NumShapes = 0;
		uint32 NumShapesFollowingRule = 0;
		for (const FPhysXBody& Record : Bodies)
		{
			if (Record.bReleased)
			{
				continue;
			}
			physx::PxShape* Shapes[8];
			const physx::PxU32 Count = Record.Actor->getNbShapes();
			for (physx::PxU32 Start = 0; Start < Count; Start += 8)
			{
				const physx::PxU32 Got = Record.Actor->getShapes(Shapes, 8, Start);
				for (physx::PxU32 Index = 0; Index < Got; ++Index)
				{
					const physx::PxShape& Shape = *Shapes[Index];
					const double ContactCm = Scale.ToCm(static_cast<double>(Shape.getContactOffset()));
					ContactOffset.Add(ContactCm);
					RestOffset.Add(Scale.ToCm(static_cast<double>(Shape.getRestOffset())));
					++NumShapes;
					double ExpectedCm = Scale.ToCm(0.02 * static_cast<double>(Tolerances.length));
					if (Config.ContactOffsetRule == EPhysXContactOffsetRule::UE4Clamp)
					{
						PtShapeElem Element = {};
						const physx::PxGeometry& Geometry = Shape.getGeometry();
						if (Geometry.getType() == physx::PxGeometryType::eBOX)
						{
							const physx::PxVec3 Half = static_cast<const physx::PxBoxGeometry&>(Geometry).halfExtents;
							Element.Type = PT_SHAPE_BOX;
							Element.BoxHalfExtent = P2UVector(Half, Scale);
						}
						else if (Geometry.getType() == physx::PxGeometryType::eSPHERE)
						{
							Element.Type = PT_SHAPE_SPHERE;
							Element.Radius = Scale.ToCm(static_cast<double>(static_cast<const physx::PxSphereGeometry&>(Geometry).radius));
						}
						else if (Geometry.getType() == physx::PxGeometryType::eCAPSULE)
						{
							Element.Type = PT_SHAPE_CAPSULE;
							Element.Radius = Scale.ToCm(static_cast<double>(static_cast<const physx::PxCapsuleGeometry&>(Geometry).radius));
						}
						ExpectedCm = ComputeContactOffsetCm(Element);
					}
					NumShapesFollowingRule += fabs(ContactCm - ExpectedCm) <= 1e-5 * (ExpectedCm > 1.0 ? ExpectedCm : 1.0) ? 1u : 0u;
				}
			}
		}
		if (Config.ContactOffsetRule == EPhysXContactOffsetRule::UE4Clamp)
		{
			snprintf(Text, sizeof(Text), "UE4: clamp(%.9g x MinExtent, %.9g, %.9g) cm per element; %u of %u shapes hold it (PxShape::getContactOffset against the "
				"shape's geometry); rest offset from the element", Config.ContactOffsetMultiplier, Config.MinContactOffsetCm, Config.MaxContactOffsetCm,
				NumShapesFollowingRule, NumShapes);
		}
		else
		{
			snprintf(Text, sizeof(Text), "SDK: PxShape default 0.02 x PxTolerancesScale::length; %u of %u shapes hold it (PxShape::getContactOffset)", NumShapesFollowingRule,
				NumShapes);
		}
		// READBACK when the engine's shapes were checked against the rule; a scene without shapes can only echo it. canonical =
		// the shapes that do not hold the rule (docs/Abi.md 5.14 rule keys: 0 = every engine object holds the host's rule).
		Out.AppendEffective("contact.offset_rule", Text, "", NumShapes > 0 ? static_cast<double>(NumShapes - NumShapesFollowingRule) : NaN, "count",
			NumShapes > 0 ? "READBACK" : "SET_ONLY");
		// M6 (M5 review): PhysX creates a contact once two shapes are closer than the sum of their contact offsets, so the
		// pair range is read back from the shapes (PxShape::getContactOffset): [2 x min, 2 x max] over the scene's shapes,
		// canonical = the largest pair sum (Bullet's key is the manifold breaking threshold, Jolt's mSpeculativeContactDistance).
		if (ContactOffset.bAny)
		{
			snprintf(Text, sizeof(Text), "pairs [%.9g, %.9g] cm: the sum of the two shapes' contact offsets (PxShape::getContactOffset, %u shapes)", 2.0 * ContactOffset.Min,
				2.0 * ContactOffset.Max, NumShapes);
			Out.AppendEffective("contact.speculative_distance", Text, "cm", 2.0 * ContactOffset.Max, "cm", "READBACK");
		}
		else
		{
			Out.AppendEffective("contact.speculative_distance", "no shape: PhysX generates contacts within the sum of the two shapes' contact offsets", "", NaN, "",
				"COMPILE_TIME");
		}
		// Read back from the materials: each PxMaterial's combine modes (PxMaterial::getFrictionCombineMode /
		// getRestitutionCombineMode) and static / dynamic friction against the host material it was made from.
		uint32 NumFrictionCombineHeld = 0;
		uint32 NumRestitutionCombineHeld = 0;
		uint32 NumMaterialsFollowingRule = 0;
		uint32 NumStaticAboveDynamic = 0;
		for (uint32 Index = 0; Index < Materials.size(); ++Index)
		{
			const physx::PxMaterial& Material = *Materials[Index];
			const PtPhysicalMaterial& Info = MaterialInfos[Index];
			NumFrictionCombineHeld += static_cast<uint32>(Material.getFrictionCombineMode()) == Info.FrictionCombineMode ? 1u : 0u;
			NumRestitutionCombineHeld += static_cast<uint32>(Material.getRestitutionCombineMode()) == Info.RestitutionCombineMode ? 1u : 0u;
			const double Expected = Config.StaticFrictionRule == EPhysXStaticFrictionRule::EqualsDynamic ? Info.Friction
				: (Info.StaticFriction > Info.Friction ? Info.StaticFriction : Info.Friction);
			const bool bStaticOk = fabs(static_cast<double>(Material.getStaticFriction()) - Expected) <= 1e-6 * (Expected > 1.0 ? Expected : 1.0);
			const bool bDynamicOk = fabs(static_cast<double>(Material.getDynamicFriction()) - Info.Friction) <= 1e-6 * (Info.Friction > 1.0 ? Info.Friction : 1.0);
			NumMaterialsFollowingRule += bStaticOk && bDynamicOk ? 1u : 0u;
			NumStaticAboveDynamic += Material.getStaticFriction() > Material.getDynamicFriction() ? 1u : 0u;
		}
		snprintf(Text, sizeof(Text), "%s (%s; %u of %u PxMaterials hold it, %u with static > dynamic friction)",
			Config.StaticFrictionRule == EPhysXStaticFrictionRule::EqualsDynamic ? "equals_dynamic" : "chaos_pair",
			Config.StaticFrictionRule == EPhysXStaticFrictionRule::EqualsDynamic ? "createMaterial(F, F, R), UE4" : "createMaterial(max(F, SF), F, R)", NumMaterialsFollowingRule,
			Materials.size(), NumStaticAboveDynamic);
		// canonical = the PxMaterials that do not follow the rule (0 = all do).
		const double NumMaterialObjects = static_cast<double>(Materials.size());
		Out.AppendEffective("material.static_friction_rule", Text, "", Materials.empty() ? NaN : NumMaterialObjects - NumMaterialsFollowingRule, "count",
			Materials.empty() ? "SET_ONLY" : "READBACK");
		// The host resolves each material's UE combine modes (PT_COMBINE_*, the values of PxCombineMode, static_asserted in
		// PhysXBackend.cpp); PhysX combines a pair with the larger mode (PxsMaterialCombiner.h [V]). READBACK when the
		// materials were checked, as above.
		snprintf(Text, sizeof(Text), "PxMaterial friction combine mode per material, the larger PxCombineMode wins (PxsMaterialCombiner.h); %u of %u PxMaterials hold "
			"the host's mode (PxMaterial::getFrictionCombineMode)", NumFrictionCombineHeld, Materials.size());
		Out.AppendEffective("material.friction_combine", Text, "", Materials.empty() ? NaN : NumMaterialObjects - NumFrictionCombineHeld, "count",
			Materials.empty() ? "SET_ONLY" : "READBACK");
		snprintf(Text, sizeof(Text), "PxMaterial restitution combine mode per material, the larger PxCombineMode wins (PxsMaterialCombiner.h); %u of %u PxMaterials hold "
			"the host's mode (PxMaterial::getRestitutionCombineMode)", NumRestitutionCombineHeld, Materials.size());
		Out.AppendEffective("material.restitution_combine", Text, "", Materials.empty() ? NaN : NumMaterialObjects - NumRestitutionCombineHeld, "count",
			Materials.empty() ? "SET_ONLY" : "READBACK");
		// canonical = the formula id of docs/Abi.md 5.14: 1 = UE order (gravity into the velocity, then v *= max(0, 1 - c dt),
		// DyBodyCoreIntegrator.h:26-42 [V]); G1b checks it.
		Out.AppendEffective("body.damping_formula", "UE order: v = (v + a dt) * max(0, 1 - c dt) per step (DyBodyCoreIntegrator.h:26-42)", "formula_id", 1.0,
			"formula_id", "COMPILE_TIME");
		// docs/Abi.md 9.1 decision 1: every velocity / state write wakes a sleeping body, a nearly zero one too (canonical 1);
		// G6 checks the declared behaviour.
		Out.AppendEffective("command.nearly_zero_velocity_wakes", "1: a nearly zero velocity write wakes a sleeping body (PxRigidDynamic::wakeUp on every write)",
			"bool", 1.0, "bool", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "linear [%.9g, %.9g] angular [%.9g, %.9g] over %u dynamic bodies", Linear.Min, Linear.Max, Angular.Min, Angular.Max, NumDynamic);
		Out.AppendEffective("body.damping", Text, "1/s", Linear.bAny ? Linear.Max : NaN, "1/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] rad/s (%.9g = unclamped)", MaxAngular.Min, MaxAngular.Max, static_cast<double>(GUnclampedAngularVelocity));
		Out.AppendEffective("body.max_angular_velocity", Text, "rad/s", MaxAngular.bAny ? MaxAngular.Max : NaN, "rad/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm/s (PhysX ceiling %.9g native/s = unclamped)", MaxLinear.Min, MaxLinear.Max, static_cast<double>(GUnclampedLinearVelocity));
		Out.AppendEffective("body.max_linear_velocity", Text, "cm/s", MaxLinear.bAny ? MaxLinear.Max : NaN, "cm/s", "READBACK");
		if (Config.GyroscopicRule == EPhysXGyroscopicRule::Ignore)
		{
			snprintf(Text, sizeof(Text), "%u of %u dynamic bodies have eENABLE_GYROSCOPIC_FORCES (GyroscopicForces=Ignore; %u requests ignored: UE4 on PhysX 3.4 "
				"had no gyroscopic term)", NumGyroscopic, NumDynamic, NumGyroscopicIgnored);
		}
		else
		{
			snprintf(Text, sizeof(Text), "%u of %u dynamic bodies have eENABLE_GYROSCOPIC_FORCES (GyroscopicForces=FromBody: each body's bGyroscopicTorqueEnabled)",
				NumGyroscopic, NumDynamic);
		}
		Out.AppendEffective("body.gyroscopic", Text, "bodies", NumGyroscopic > 0 ? 1.0 : 0.0, "bool", "READBACK");
		const bool bSleepEnabled = !(Flags & physx::PxSceneFlag::eDISABLE_SLEEPING);
		snprintf(Text, sizeof(Text), "%s (PxSceneFlag::eDISABLE_SLEEPING %s)", bSleepEnabled ? "true" : "false", bSleepEnabled ? "off" : "on");
		Out.AppendEffective("sleep.enabled", Text, "bool", bSleepEnabled ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "mass-normalised kinetic energy below the sleep threshold [%.9g, %.9g] cm^2/s^2 (stabilization [%.9g, %.9g]) for the wake counter "
			"(%.9g s, PxScene::getWakeCounterResetValue); %s", SleepThreshold.Min, SleepThreshold.Max, StabThreshold.Min, StabThreshold.Max,
			static_cast<double>(Scene->getWakeCounterResetValue()), Config.ThresholdRule == EPhysXThresholdRule::UE4Multiplier ? "UE4: PhysX default x body multiplier"
			: "the PhysX defaults");
		Out.AppendEffective("sleep.criteria", Text, "cm^2/s^2", SleepThreshold.bAny ? SleepThreshold.Max : NaN, "cm^2/s^2", "READBACK");
		const bool bSceneCcd = (Flags & physx::PxSceneFlag::eENABLE_CCD) ? true : false;
		snprintf(Text, sizeof(Text), "scene eENABLE_CCD %s; %u of %u dynamic bodies have eENABLE_CCD", bSceneCcd ? "on" : "off", NumCcd, NumDynamic);
		Out.AppendEffective("ccd.enabled", Text, "bodies", bSceneCcd && NumCcd > 0 ? 1.0 : 0.0, "bool", "READBACK");
		// The dispatcher the scene holds (PxScene::getCpuDispatcher), not the adapter's own pointer.
		const uint32 Workers = Scene->getCpuDispatcher()->getWorkerCount();
		snprintf(Text, sizeof(Text), "%u (PxCpuDispatcher::getWorkerCount; %s)", Workers,
			SingleThreadDispatcher ? "FPhysXCPUDispatcherSingleThread runs every task on the calling thread"
			: (InlineDispatcher ? "PxDefaultCpuDispatcherCreate(0) runs every task inline" : "the caller waits in fetchResults"));
		Out.AppendEffective("threads.workers", Text, "threads", static_cast<double>(Workers), "threads", "READBACK");
		const bool bDeterminism = (Flags & physx::PxSceneFlag::eENABLE_ENHANCED_DETERMINISM) ? true : false;
		Out.AppendEffective("determinism.flag", bDeterminism ? "true (PxSceneFlag::eENABLE_ENHANCED_DETERMINISM)" : "false (eENABLE_ENHANCED_DETERMINISM off)", "bool",
			bDeterminism ? 1.0 : 0.0, "bool", "READBACK");
		Out.AppendEffective("shape.box_margin", "0: PxBoxGeometry is an exact box (contacts start at the contact offset instead)", "cm", 0.0, "cm", "COMPILE_TIME");
		Out.AppendEffective("shape.convex_radius", "0: exact shapes (no convex radius)", "cm", 0.0, "cm", "COMPILE_TIME");
		const Abi::FShapeWrappingCounts Wrapping = ReadWrapping();
		snprintf(Text, sizeof(Text), "%u (compound %u, local_transform %u, offset_com %u, axis_remap %u; read from the actors: PhysX holds multi-shape actors, shape local "
			"poses and the COM frame natively, the counts mark the same conditions as the Jolt / Bullet wrappers)", Wrapping.Total(), Wrapping.Compound,
			Wrapping.LocalTransform, Wrapping.OffsetCom, Wrapping.AxisRemap);
		Out.AppendEffective("shape.wrapping", Text, "shapes", static_cast<double>(Wrapping.Total()), "shapes", "READBACK");
		const bool bListener = Scene->getSimulationEventCallback() != nullptr;
		// M7 (M6 review): the narrow-phase pose of the snapshot contact points, declared (optional key; G3 checks it against
		// the points): 0 = the start-of-step pose (docs/Abi.md 9.1 decision 3).
		Out.AppendEffective("contact.point_pose", "0 (start of step: the narrow phase collides the poses the step began with)", "", 0.0, "", "COMPILE_TIME");
		Out.AppendEffective("listeners.installed", bListener ? "1 (FPhysXSimEventCallback: viz / PT_INSTR_CONTACTS)" : "0", "listeners", bListener ? 1.0 : 0.0,
			"listeners", "READBACK");
		// M9: the joint recipe and the native debug draw (optional keys; docs/Abi.md 9.4 item 4).
		AppendJointSettings(Out);
		AppendVisualizationSettings(Out);

		// PhysX-specific (physx.tolerances_* and physx.cpu_dispatcher are the backend's required keys).
		snprintf(Text, sizeof(Text), "%.9g (PxPhysics::getTolerancesScale().length, native units)", static_cast<double>(Tolerances.length));
		Out.AppendEffective("physx.tolerances_length", Text, "native", static_cast<double>(Tolerances.length), "native", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g (PxPhysics::getTolerancesScale().speed, native units/s)", static_cast<double>(Tolerances.speed));
		Out.AppendEffective("physx.tolerances_speed", Text, "native/s", static_cast<double>(Tolerances.speed), "native/s", "READBACK");
		// The D1 table (BackendCommon GetEngineUnitScale): the UE4Compat family runs in cm, SdkDefault in m.
		const double CompatScale = Abi::GetEngineUnitScale(Abi::EEngineUnits::PhysXUE4Compat).L;
		const double SdkScale = Abi::GetEngineUnitScale(Abi::EEngineUnits::PhysXSdkDefault).L;
		snprintf(Text, sizeof(Text), "%.9g native units per cm (%s)", Config.LengthScale,
			Config.LengthScale == CompatScale ? "cm, the UE4Compat family's D1 scale" : (Config.LengthScale == SdkScale ? "m, SdkDefault's D1 scale" : "custom, outside the D1 table"));
		Out.AppendEffective("physx.length_scale", Text, "", Config.LengthScale, "", "SET_ONLY");
		const FPhysXDispatcherStats& Stats = SingleThreadDispatcher ? SingleThreadDispatcher->GetStats() : (InlineDispatcher ? InlineDispatcher->GetStats() : FPhysXDispatcherStats());
		if (SingleThreadDispatcher)
		{
			snprintf(Text, sizeof(Text), "FPhysXCPUDispatcherSingleThread (thread-local LIFO, 0 workers): %llu tasks, max re-entrancy depth %u, max pending %u, %u stack growths, "
				"%u inline fallbacks", static_cast<unsigned long long>(Stats.NumTasks), Stats.MaxReentrancyDepth, Stats.MaxPendingTasks, Stats.NumGrowths,
				Stats.NumInlineFallbacks);
		}
		else if (InlineDispatcher)
		{
			snprintf(Text, sizeof(Text), "PxDefaultCpuDispatcherCreate(0) (inline, recursive): %llu tasks, max re-entrancy depth %u",
				static_cast<unsigned long long>(Stats.NumTasks), Stats.MaxReentrancyDepth);
		}
		else
		{
			snprintf(Text, sizeof(Text), "PxDefaultCpuDispatcherCreate(%u) (worker pool)", Workers);
		}
		Out.AppendEffective("physx.cpu_dispatcher", Text, "workers", static_cast<double>(Workers), "workers", "READBACK");
		// Counted by the adapter's dispatcher (FPhysXCPUDispatcherSingleThread / the inline proxy): SET_ONLY (docs/Abi.md 5.14).
		Out.AppendEffective("physx.dispatcher_max_reentrancy", PoolDispatcher != nullptr ? "n/a (worker pool)" : (Stats.MaxReentrancyDepth <= 1 ? "<= 1" : "> 1"),
			"depth", PoolDispatcher != nullptr ? NaN : static_cast<double>(Stats.MaxReentrancyDepth), "depth", PoolDispatcher != nullptr ? "COMPILE_TIME" : "SET_ONLY");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm over %u shapes (PxShape::getContactOffset)", ContactOffset.Min, ContactOffset.Max, NumShapes);
		Out.AppendEffective("physx.contact_offset_min", Text, "cm", ContactOffset.bAny ? ContactOffset.Min : NaN, "cm", "READBACK");
		Out.AppendEffective("physx.contact_offset_max", Text, "cm", ContactOffset.bAny ? ContactOffset.Max : NaN, "cm", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm over %u shapes (PxShape::getRestOffset)", RestOffset.Min, RestOffset.Max, NumShapes);
		Out.AppendEffective("physx.rest_offset", Text, "cm", RestOffset.bAny ? RestOffset.Max : NaN, "cm", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm^2/s^2 over %u dynamic bodies (PxRigidDynamic::getSleepThreshold / L^2)", SleepThreshold.Min, SleepThreshold.Max, NumDynamic);
		Out.AppendEffective("physx.sleep_threshold_min", Text, "cm^2/s^2", SleepThreshold.bAny ? SleepThreshold.Min : NaN, "cm^2/s^2", "READBACK");
		Out.AppendEffective("physx.sleep_threshold_max", Text, "cm^2/s^2", SleepThreshold.bAny ? SleepThreshold.Max : NaN, "cm^2/s^2", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm^2/s^2 over %u dynamic bodies (PxRigidDynamic::getStabilizationThreshold / L^2)", StabThreshold.Min, StabThreshold.Max,
			NumDynamic);
		Out.AppendEffective("physx.stabilization_threshold_min", Text, "cm^2/s^2", StabThreshold.bAny ? StabThreshold.Min : NaN, "cm^2/s^2", "READBACK");
		Out.AppendEffective("physx.stabilization_threshold_max", Text, "cm^2/s^2", StabThreshold.bAny ? StabThreshold.Max : NaN, "cm^2/s^2", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm/s (PxRigidBody::getMaxDepenetrationVelocity; %s: %u unset -> project %.9g cm/s, %u -> unlimited, %u zero not "
			"representable)", Depenetration.Min, Depenetration.Max, ToString(Config.DepenetrationRule), NumDepenetrationFromProject, Config.ProjectMaxDepenetrationVelocityCm,
			NumDepenetrationUnlimited, NumDepenetrationUnrepresentable);
		Out.AppendEffective("physx.max_depenetration_velocity", Text, "cm/s", Depenetration.bAny ? Depenetration.Max : NaN, "cm/s", "READBACK");
		snprintf(Text, sizeof(Text), "0x%x (PCM %d, stabilization %d, CCD %d, enhanced determinism %d, active actors %d, exclude kinematics %d, disable sleeping %d)",
			static_cast<uint32>(Flags), (Flags & physx::PxSceneFlag::eENABLE_PCM) ? 1 : 0, (Flags & physx::PxSceneFlag::eENABLE_STABILIZATION) ? 1 : 0,
			bSceneCcd ? 1 : 0, bDeterminism ? 1 : 0, (Flags & physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS) ? 1 : 0,
			(Flags & physx::PxSceneFlag::eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS) ? 1 : 0, bSleepEnabled ? 0 : 1);
		Out.AppendEffective("physx.scene_flags", Text, "flags", static_cast<double>(static_cast<uint32>(Flags)), "flags", "READBACK");
		snprintf(Text, sizeof(Text), "kinematic-kinematic %s, static-kinematic %s (the UE4 filter shader suppresses those pairs)",
			ToString(Scene->getKinematicKinematicFilteringMode()), ToString(Scene->getStaticKinematicFilteringMode()));
		Out.AppendEffective("physx.kinematic_pair_filtering", Text, "", NaN, "", "READBACK");
		// The [M] filter rules as the scene holds them (PxScene::getFilterShaderData, PhysicsFiltering.h). Canonical: 2 x
		// NonBlocking + KinematicStatic (0 = the defaults Kill / UnlessKinematicPairs).
		const FPhysXFilterShaderRules HeldRules = ReadFilterShaderRules(Scene->getFilterShaderData(), Scene->getFilterShaderDataSize());
		snprintf(Text, sizeof(Text), "non-blocking pairs %s, kinematic-static pairs %s (PxScene::getFilterShaderData, %u bytes; [M] variant keys NonBlockingPairFilter, "
			"KinematicStaticPairFilter)", ToString(static_cast<EPhysXNonBlockingPairFilter>(HeldRules.NonBlocking)),
			ToString(static_cast<EPhysXKinematicStaticPairFilter>(HeldRules.KinematicStatic)), Scene->getFilterShaderDataSize());
		Out.AppendEffective("physx.filter_shader_rules", Text, "", static_cast<double>(HeldRules.NonBlocking * 2u + HeldRules.KinematicStatic), "", "READBACK");
		snprintf(Text, sizeof(Text), "%u (PxScene::getDynamicTreeRebuildRateHint)", Scene->getDynamicTreeRebuildRateHint());
		Out.AppendEffective("physx.dynamic_tree_rebuild_rate", Text, "frames", static_cast<double>(Scene->getDynamicTreeRebuildRateHint()), "frames", "READBACK");
		snprintf(Text, sizeof(Text), "%u bytes passed to PxScene::simulate (16 KiB multiple; 0 = none)", ScratchBlock != nullptr ? Config.ScratchMemoryBytes : 0u);
		Out.AppendEffective("physx.scratch_bytes", Text, "bytes", static_cast<double>(ScratchBlock != nullptr ? Config.ScratchMemoryBytes : 0u), "bytes", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%.9g cm (PxScene::getFrictionOffsetThreshold)", Scale.ToCm(static_cast<double>(Scene->getFrictionOffsetThreshold())));
		Out.AppendEffective("physx.friction_offset_threshold", Text, "cm", Scale.ToCm(static_cast<double>(Scene->getFrictionOffsetThreshold())), "cm", "READBACK");
		const physx::PxSceneLimits Limits = Scene->getLimits();
		snprintf(Text, sizeof(Text), "actors %u, bodies %u, static shapes %u, dynamic shapes %u, broad-phase overlaps %u (PxScene::getLimits, from the capacity hints)",
			Limits.maxNbActors, Limits.maxNbBodies, Limits.maxNbStaticShapes, Limits.maxNbDynamicShapes, Limits.maxNbBroadPhaseOverlaps);
		Out.AppendEffective("physx.scene_limits", Text, "actors", static_cast<double>(Limits.maxNbActors), "actors", "READBACK");
		// Adapter counters (docs/Abi.md 5.14: what the adapter counted itself is SET_ONLY, never READBACK).
		snprintf(Text, sizeof(Text), "%u", NumStartAsleepSkipped);
		Out.AppendEffective("physx.start_asleep_skipped", Text, "bodies", static_cast<double>(NumStartAsleepSkipped), "bodies", "SET_ONLY");
		snprintf(Text, sizeof(Text), "0x%x (PxErrorCode bits of the reports seen by this scene; 0x100 simulate / fetchResults refused)", TotalErrorBits | PendingErrorBits);
		Out.AppendEffective("physx.error_bits", Text, "bits", static_cast<double>(TotalErrorBits | PendingErrorBits), "bits", "SET_ONLY");
		if (EventCallback)
		{
			snprintf(Text, sizeof(Text), "%u points capacity, %u kept sleeping points, onWake %llu / onSleep %llu events, %llu points beyond %u per pair", ListenerCapacity,
				SleepingContacts.size(), static_cast<unsigned long long>(EventCallback->GetNumWakeEvents()),
				static_cast<unsigned long long>(EventCallback->GetNumSleepEvents()), static_cast<unsigned long long>(EventCallback->GetNumOverPairLimit()),
				GPhysXMaxPointsPerPair);
			Out.AppendEffective("physx.sim_event_callback", Text, "points", static_cast<double>(ListenerCapacity), "points", "SET_ONLY");
		}
		Out.AppendEffective("physx.fidelity", GPhysXFidelityNote, "", NaN, "", "COMPILE_TIME");
		// Which engine error bits mean "a capacity was exceeded" (the bench's exit code 5, docs/Abi.md 5.14).
		snprintf(Text, sizeof(Text), "0x%x (eOUT_OF_MEMORY; PhysX grows its pools itself)", GPhysXCapacityErrorBits);
		Out.AppendEffective("errors.capacity_bits", Text, "bits", static_cast<double>(GPhysXCapacityErrorBits), "bits", "COMPILE_TIME");
		return PT_OK;
	}

	void FPhysScene_PhysX::AppendVisualizationSettings(Abi::FTextWriter& Out) const
	{
		// What the scene holds (PxScene::getVisualizationParameter) and what its render buffer holds now (the last step's
		// drawing). SET_ONLY: the categories are the adapter's input and no G9 rule covers the key (docs/Abi.md 9.1 decision 11).
		char Text[1024];
		if (!bVisualization)
		{
			snprintf(Text, sizeof(Text), "off: %s (PxVisualizationParameter::eSCALE %.9g; the parameters are set in viz runs only, PT_SNAP_NATIVE_DRAW reports no command)",
				Desc.RunMode == PT_RUN_VIZ ? "DebugVisualization=None" : "not a viz run", static_cast<double>(Scene->getVisualizationParameter(physx::PxVisualizationParameter::eSCALE)));
			Out.AppendEffective("physx.debug_visualization", Text, "categories", 0.0, "categories", "SET_ONLY");
			return;
		}
		char List[512] = {};
		uint32 Length = 0;
		uint32 NumCategories = 0;
		for (uint32 Bit = 1; Bit <= PhysXViz_All; Bit <<= 1u)
		{
			if ((Config.Visualization & Bit) == 0)
			{
				continue;
			}
			const int Written = snprintf(List + Length, sizeof(List) - Length, "%s%s", NumCategories > 0 ? "," : "", GetVisualizationCategoryName(Bit));
			Length += Written > 0 && static_cast<uint32>(Written) < sizeof(List) - Length ? static_cast<uint32>(Written) : 0u;
			++NumCategories;
		}
		const FPhysXRenderCounts Now = CountRenderBuffer(Scene->getRenderBuffer());
		snprintf(Text, sizeof(Text), "on (viz run): DebugVisualization=%s, eSCALE %.9g native units (1 m), axes %.9g cm, contact normals %.9g cm; render buffer of the last "
			"step: %u points, %u lines, %u triangles (%u multi-colour; PxScene::getRenderBuffer); %u PT_SNAP_NATIVE_DRAW captures, the last of %u primitives", List,
			static_cast<double>(Scene->getVisualizationParameter(physx::PxVisualizationParameter::eSCALE)), GVizAxisLengthCm, GVizNormalLengthCm, Now.NumPoints, Now.NumLines,
			Now.NumTriangles, Now.NumMultiColour, NumNativeCaptures, LastCaptureCommands.Total());
		Out.AppendEffective("physx.debug_visualization", Text, "categories", static_cast<double>(NumCategories), "categories", "SET_ONLY");
	}

	PtResult FPhysScene_PhysX::GetMemoryStats(PtMemoryStats& Out)
	{
		// The FPhysXAllocator counters (module-wide: PhysX has one allocator per PxFoundation, so with one scene per process -
		// a bench run - they are this scene's).
		const FPhysXAllocator& Allocator = GetPhysXAllocator();
		Out.LiveBytes = Allocator.GetLiveBytes();
		Out.PeakBytes = Allocator.GetPeakBytes();
		Out.NumAllocs = Allocator.GetNumAllocs();
		Out.NumFrees = Allocator.GetNumFrees();
		Out.AllocatorKind = 0;
		return PT_OK;
	}
}
