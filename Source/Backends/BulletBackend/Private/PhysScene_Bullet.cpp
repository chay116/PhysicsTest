// PhysScene_Bullet.cpp - FPhysScene_Bullet (see PhysScene_Bullet.h). UE behaviour mirrored here is cited inline; Bullet is
// used through its public API only (no local patch, Q8).
#include "PhysScene_Bullet.h"

#include "BulletCombine.h"
#include "BulletMath.h"
#include "PtContactConvention.h"
#include "PtDamping.h"
#include "PtUnitScale.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace PhysicsTest::BulletBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// btDefaultCollisionConstructionInfo's pool sizes (btDefaultCollisionConfiguration.h:35-36 [V]): used when the host
		// gives no capacity hint (A gates, small tests).
		constexpr int32 GBulletDefaultPoolSize = 4096;
		constexpr int32 GMaxPoolSize = 1 << 24;
		constexpr uint32 GDefaultBodyReserve = 1024;

		bool EqualsNoCase(const char* A, const Abi::FStringRef B)
		{
			const size_t Length = strlen(A);
			if (Length != B.Length)
			{
				return false;
			}
			for (size_t Index = 0; Index < Length; ++Index)
			{
				const char CA = (A[Index] >= 'A' && A[Index] <= 'Z') ? static_cast<char>(A[Index] - 'A' + 'a') : A[Index];
				const char CB = (B.Data[Index] >= 'A' && B.Data[Index] <= 'Z') ? static_cast<char>(B.Data[Index] - 'A' + 'a') : B.Data[Index];
				if (CA != CB)
				{
					return false;
				}
			}
			return true;
		}

		bool ParseDouble(const Abi::FStringRef Text, double& Out)
		{
			char Buffer[64];
			if (Text.Length == 0 || Text.Length >= sizeof(Buffer))
			{
				return false;
			}
			memcpy(Buffer, Text.Data, Text.Length);
			Buffer[Text.Length] = '\0';
			char* End = nullptr;
			const double Value = strtod(Buffer, &End);
			if (End == Buffer || *End != '\0' || !Abi::IsFinite(Value))
			{
				return false;
			}
			Out = Value;
			return true;
		}

		// Decimal, or hexadecimal with 0x (SolverMode is a bit mask).
		bool ParseUInt(const Abi::FStringRef Text, uint64& Out)
		{
			char Buffer[32];
			if (Text.Length == 0 || Text.Length >= sizeof(Buffer) || Text.Data[0] == '-' || Text.Data[0] == '+')
			{
				return false;
			}
			memcpy(Buffer, Text.Data, Text.Length);
			Buffer[Text.Length] = '\0';
			const bool bHex = Text.Length > 2 && Buffer[0] == '0' && (Buffer[1] == 'x' || Buffer[1] == 'X');
			char* End = nullptr;
			const unsigned long long Value = strtoull(bHex ? Buffer + 2 : Buffer, &End, bHex ? 16 : 10);
			if (End == (bHex ? Buffer + 2 : Buffer) || *End != '\0')
			{
				return false;
			}
			Out = Value;
			return true;
		}

		bool ParseBool(const Abi::FStringRef Text, bool& Out)
		{
			if (EqualsNoCase("true", Text) || EqualsNoCase("1", Text))
			{
				Out = true;
				return true;
			}
			if (EqualsNoCase("false", Text) || EqualsNoCase("0", Text))
			{
				Out = false;
				return true;
			}
			return false;
		}

		void PreTickCallback(btDynamicsWorld* World, const btScalar TimeStep)
		{
			static_cast<FPhysScene_Bullet*>(World->getWorldUserInfo())->OnPreTick(TimeStep);
		}

		void PostTickCallback(btDynamicsWorld* World, const btScalar TimeStep)
		{
			static_cast<FPhysScene_Bullet*>(World->getWorldUserInfo())->OnPostTick(TimeStep);
		}

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

		int32 GetPoolSize(const uint32 Option, const uint32 Hint)
		{
			const uint32 Size = Option > 0 ? Option : (Hint > 0 ? Hint : static_cast<uint32>(GBulletDefaultPoolSize));
			return Size > static_cast<uint32>(GMaxPoolSize) ? GMaxPoolSize : static_cast<int32>(Size);
		}

		// I_world * V of a dynamic body: R diag(I) R^T V with R = the body (principal) basis, in Bullet units.
		btVector3 MultiplyWorldInertia(const btRigidBody& Body, const btVector3& V)
		{
			const btMatrix3x3& Basis = Body.getWorldTransform().getBasis();
			const btVector3 Local = V * Basis;   // R^T V
			return Basis * (Local * Body.getLocalInertia());
		}

		// The smallest half extent / radius of a body's elements (CCD sizes), cm.
		double GetSmallestExtent(const PtActorCreateInfo& Actor, const Abi::TStridedView<PtShapeElem>& Shapes)
		{
			double Smallest = DBL_MAX;
			for (uint32 Offset = 0; Offset < Actor.NumShapes; ++Offset)
			{
				const PtShapeElem& Element = Shapes[Actor.FirstShape + Offset];
				double Extent = Element.Radius;
				if (Element.Type == PT_SHAPE_BOX)
				{
					Extent = Element.BoxHalfExtent.X < Element.BoxHalfExtent.Y ? Element.BoxHalfExtent.X : Element.BoxHalfExtent.Y;
					Extent = Element.BoxHalfExtent.Z < Extent ? Element.BoxHalfExtent.Z : Extent;
				}
				Smallest = Extent < Smallest ? Extent : Smallest;
			}
			return Smallest;
		}

		constexpr PtTransform GIdentityTransform = { PtQuat{ 0.0, 0.0, 0.0, 1.0 }, PtVec3{ 0.0, 0.0, 0.0 } };
	}

	PtResult ApplySceneOption(FBulletSceneOptions& Options, const Abi::FStringRef Key, const Abi::FStringRef Value, char (&Error)[256])
	{
		uint64 Unsigned = 0;
		double Real = 0.0;
		bool bFlag = false;
		const auto Fail = [&](const char* Expectation) -> PtResult
		{
			snprintf(Error, sizeof(Error), "Bullet: %.*s must be %s", static_cast<int>(Key.Length), Key.Data, Expectation);
			return PT_E_INVALID_ARG;
		};
		if (EqualsNoCase("NumIterations", Key))
		{
			if (!ParseUInt(Value, Unsigned) || Unsigned < 1 || Unsigned > 1000)
			{
				return Fail("an integer in [1, 1000]");
			}
			Options.NumIterations = static_cast<int32>(Unsigned);
			return PT_OK;
		}
		if (EqualsNoCase("SolverMode", Key))
		{
			if (!ParseUInt(Value, Unsigned) || Unsigned > 0x1FFFu)
			{
				return Fail("a btSolverMode bit mask in [0, 0x1FFF] (decimal or 0x hex)");
			}
			Options.SolverMode = static_cast<int32>(Unsigned);
			return PT_OK;
		}
		if (EqualsNoCase("SplitImpulse", Key) || EqualsNoCase("ForceUpdateAllAabbs", Key))
		{
			if (!ParseBool(Value, bFlag))
			{
				return Fail("true / false / 1 / 0");
			}
			(EqualsNoCase("SplitImpulse", Key) ? Options.bSplitImpulse : Options.bForceUpdateAllAabbs) = bFlag;
			return PT_OK;
		}
		if (EqualsNoCase("SplitImpulsePenetrationThreshold", Key))
		{
			if (!ParseDouble(Value, Real))
			{
				return Fail("a finite number (cm; Bullet's default -4 cm)");
			}
			Options.SplitImpulsePenetrationThresholdCm = Real;
			return PT_OK;
		}
		if (EqualsNoCase("SplitImpulseTurnErp", Key) || EqualsNoCase("Erp2", Key) || EqualsNoCase("WarmstartingFactor", Key))
		{
			if (!ParseDouble(Value, Real) || Real < 0.0 || Real > 1.0)
			{
				return Fail("a number in [0, 1]");
			}
			if (EqualsNoCase("SplitImpulseTurnErp", Key))
			{
				Options.SplitImpulseTurnErp = Real;
			}
			else if (EqualsNoCase("Erp2", Key))
			{
				Options.Erp2 = Real;
			}
			else
			{
				Options.WarmstartingFactor = Real;
			}
			return PT_OK;
		}
		if (EqualsNoCase("LinearSlop", Key) || EqualsNoCase("LinearSleepingThreshold", Key) || EqualsNoCase("AngularSleepingThreshold", Key))
		{
			if (!ParseDouble(Value, Real) || Real < 0.0)
			{
				return Fail("a finite number >= 0");
			}
			if (EqualsNoCase("LinearSlop", Key))
			{
				Options.LinearSlopCm = Real;
			}
			else if (EqualsNoCase("LinearSleepingThreshold", Key))
			{
				Options.LinearSleepingThresholdCm = Real;
			}
			else
			{
				Options.AngularSleepingThreshold = Real;
			}
			return PT_OK;
		}
		if (EqualsNoCase("DampingModel", Key))
		{
			if (EqualsNoCase("UE", Value))
			{
				Options.DampingModel = EDampingModel::UEOrder;
				return PT_OK;
			}
			if (EqualsNoCase("Converted", Value))
			{
				Options.DampingModel = EDampingModel::Converted;
				return PT_OK;
			}
			return Fail("UE or Converted");
		}
		if (EqualsNoCase("ManifoldPoolSize", Key) || EqualsNoCase("AlgorithmPoolSize", Key))
		{
			if (!ParseUInt(Value, Unsigned) || Unsigned > static_cast<uint64>(GMaxPoolSize))
			{
				return Fail("an integer in [0, 16777216] (0 = from the capacity hints)");
			}
			(EqualsNoCase("ManifoldPoolSize", Key) ? Options.ManifoldPoolSize : Options.AlgorithmPoolSize) = static_cast<uint32>(Unsigned);
			return PT_OK;
		}
		snprintf(Error, sizeof(Error), "Bullet: unknown scene option '%.*s' (NumIterations, SolverMode, SplitImpulse, SplitImpulsePenetrationThreshold, "
			"SplitImpulseTurnErp, Erp2, LinearSlop, WarmstartingFactor, LinearSleepingThreshold, AngularSleepingThreshold, ForceUpdateAllAabbs, DampingModel, "
			"ManifoldPoolSize, AlgorithmPoolSize)", static_cast<int>(Key.Length), Key.Data);
		return PT_E_INVALID_ARG;
	}

	// ---- construction ------------------------------------------------------------------------------------------------
	FPhysScene_Bullet::FPhysScene_Bullet(const PtSceneDesc& InDesc, const FBulletSceneOptions& InOptions)
		: Desc(InDesc)
		, Options(InOptions)
	{
		Desc.DebugName = nullptr;
		Desc.Options = nullptr;
		Desc.NumOptions = 0;
		Gravity = InDesc.Gravity;
		bSleepingAllowed = (InDesc.SimFlags & PT_SIM_ALLOW_SLEEPING) != 0;
		// No contact callback in any run; the snapshot reads the manifolds only where Jolt installs its listener (02 9.4).
		bCaptureContacts = InDesc.RunMode == PT_RUN_VIZ || (InDesc.InstrumentFlags & PT_INSTR_CONTACTS) != 0;
		StepDeltaTime = static_cast<float>(InDesc.NominalDeltaTime);
		DampingDeltaTime = StepDeltaTime;

		// Pools from the capacity hints (02 9.4): one manifold per contact constraint, one collision algorithm per broad-phase
		// pair. Overflow falls back to the heap and is reported (FBulletCollisionDispatcher, capacity error bits).
		btDefaultCollisionConstructionInfo Construction;
		Construction.m_defaultMaxPersistentManifoldPoolSize = GetPoolSize(Options.ManifoldPoolSize, InDesc.MaxContactConstraintsHint);
		Construction.m_defaultMaxCollisionAlgorithmPoolSize = GetPoolSize(Options.AlgorithmPoolSize, InDesc.MaxBodyPairsHint);
		CollisionConfiguration = new btDefaultCollisionConfiguration(Construction);
		Dispatcher = new FBulletCollisionDispatcher(CollisionConfiguration);
		Broadphase = new btDbvtBroadphase();
		Solver = new btSequentialImpulseConstraintSolver();
		World = new btDiscreteDynamicsWorld(Dispatcher, Broadphase, Solver, CollisionConfiguration);
		World->setGravity(ToBtLength(Gravity));   // explicit, never Bullet's default (0, -10, 0) (D1)
		World->setForceUpdateAllAabbs(Options.bForceUpdateAllAabbs);
		World->getDispatchInfo().m_deterministicOverlappingPairs = (InDesc.SimFlags & PT_SIM_ENHANCED_DETERMINISM) != 0;
		// The emulations of the header comment. Both callbacks share the world user info (btDynamicsWorld.h:117-131 [V]).
		World->setInternalTickCallback(&PreTickCallback, this, true);
		World->setInternalTickCallback(&PostTickCallback, this, false);
		ApplySolverSettings();

		const int32 Reserve = static_cast<int32>(InDesc.MaxBodiesHint > 0 ? InDesc.MaxBodiesHint : GDefaultBodyReserve);
		Bodies.reserve(Reserve);
		MobilityRanks.reserve(Reserve);
	}

	FPhysScene_Bullet::~FPhysScene_Bullet()
	{
		for (int32 Index = 0; Index < Bodies.size(); ++Index)
		{
			if (!Bodies[Index].bReleased)
			{
				DestroyBody(Bodies[Index]);
			}
		}
		delete World;
		delete Solver;
		delete Broadphase;
		delete Dispatcher;
		delete CollisionConfiguration;
	}

	void FPhysScene_Bullet::ApplySolverSettings()
	{
		btContactSolverInfo& Info = World->getSolverInfo();
		Info.m_numIterations = Options.NumIterations;
		Info.m_solverMode = Options.SolverMode;
		Info.m_splitImpulse = Options.bSplitImpulse ? 1 : 0;
		Info.m_splitImpulsePenetrationThreshold = btScalar(GScale.Length(Options.SplitImpulsePenetrationThresholdCm));
		Info.m_splitImpulseTurnErp = btScalar(Options.SplitImpulseTurnErp);
		Info.m_erp2 = btScalar(Options.Erp2);
		Info.m_linearSlop = btScalar(GScale.Length(Options.LinearSlopCm));
		Info.m_warmstartingFactor = btScalar(Options.WarmstartingFactor);
		// Restitution threshold (DL35, Q11): the given cm/s, else the UEParity mapping for Bullet, Chaos' 1000 * dt cm/s.
		// Below it Bullet uses no restitution (restitutionCurve, btSequentialImpulseConstraintSolver.cpp:494-502 [V]).
		const double ThresholdCm = Desc.RestitutionThreshold > 0.0 ? Desc.RestitutionThreshold : 1000.0 * Desc.NominalDeltaTime;
		Info.m_restitutionVelocityThreshold = btScalar(GScale.Length(ThresholdCm));
		World->setForceUpdateAllAabbs(Options.bForceUpdateAllAabbs);
	}

	void FPhysScene_Bullet::MarkDirty(const uint32 BodyIndex)
	{
		FBulletBody& Record = Bodies[static_cast<int32>(BodyIndex)];
		Record.bDirty = true;
		if (!Record.bDirtyListed)
		{
			Record.bDirtyListed = true;
			DirtyBodies.push_back(BodyIndex);   // reserved to the body count: each body is listed once
		}
	}

	// ---- creation ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_Bullet::CreateMaterials(const Abi::TStridedView<PtPhysicalMaterial> InMaterials)
	{
		for (uint32 Index = 0; Index < InMaterials.Num(); ++Index)
		{
			Materials.push_back(InMaterials[Index]);
		}
		return PT_OK;
	}

	btCollisionShape* FPhysScene_Bullet::MakeElementShape(const PtShapeElem& Element) const
	{
		switch (Element.Type)
		{
		case PT_SHAPE_SPHERE:
			return new btSphereShape(btScalar(GScale.Length(Element.Radius)));
		case PT_SHAPE_BOX:
			// btBoxShape picks its own margin (setSafeMargin: min(0.04 m, 0.1 x smallest half extent), inside the box);
			// shape.box_margin reads it back.
			return new btBoxShape(ToBtLength(Element.BoxHalfExtent));
		case PT_SHAPE_CAPSULE:
			// btCapsuleShapeZ(radius, height of the cylinder part) along local Z = UE's FKSphylElem axis: no remap (DL47).
			return new btCapsuleShapeZ(btScalar(GScale.Length(Element.Radius)), btScalar(GScale.Length(2.0 * Element.HalfLength)));
		default:
			return nullptr;   // convex: PT_CAP_SHAPE_CONVEX is not declared, the kit rejects it
		}
	}

	btCollisionShape* FPhysScene_Bullet::MakeBodyShape(const PtActorCreateInfo& Actor, const Abi::TStridedView<PtShapeElem>& Shapes, Abi::EShapeWrapKind& OutKind) const
	{
		const PtShapeElem& First = Shapes[Actor.FirstShape];
		// Only a dynamic body has its COM frame as the body frame; a static or kinematic one keeps the actor frame (its mass
		// never enters the solver, and a kinematic body's reported velocity is the actor origin's anyway, PtBodyState).
		PtActorCreateInfo Effective = Actor;
		if (Actor.Motion != PT_MOTION_DYNAMIC)
		{
			Effective.CenterOfMassLocal = GIdentityTransform;
		}
		OutKind = Abi::ClassifyCompoundWrapping(Effective, &First);
		if (OutKind == Abi::EShapeWrapKind::None)
		{
			return MakeElementShape(First);   // the common case (M3.BoxPile): the native primitive, no wrapper
		}
		// Children at inverse(ComLocal) * Elem: the compound's origin is the body (COM) frame (02 8.4).
		btCompoundShape* Compound = new btCompoundShape(true, static_cast<int>(Actor.NumShapes));
		const PtTransform InverseCom = TransformInverse(Effective.CenterOfMassLocal);
		for (uint32 Offset = 0; Offset < Actor.NumShapes; ++Offset)
		{
			const PtShapeElem& Element = Shapes[Actor.FirstShape + Offset];
			btCollisionShape* Child = MakeElementShape(Element);
			if (Child == nullptr)
			{
				DestroyShape(Compound);
				return nullptr;
			}
			// Child index = element index (CollidingShapes and PtContactPoint::Shape0 / Shape1 rely on it).
			Compound->addChildShape(ToBtTransform(TransformMultiply(InverseCom, Element.LocalTransform)), Child);
		}
		return Compound;
	}

	void FPhysScene_Bullet::DestroyShape(btCollisionShape* Shape)
	{
		if (Shape == nullptr)
		{
			return;
		}
		if (Shape->isCompound())
		{
			btCompoundShape* Compound = static_cast<btCompoundShape*>(Shape);
			for (int Child = Compound->getNumChildShapes() - 1; Child >= 0; --Child)
			{
				delete Compound->getChildShape(Child);
			}
		}
		delete Shape;
	}

	void FPhysScene_Bullet::DestroyBody(FBulletBody& Record)
	{
		if (Record.Body == nullptr)
		{
			return;
		}
		if (Record.Body->isInWorld())
		{
			World->removeRigidBody(Record.Body);   // also drops its broad-phase pairs, algorithms and manifolds
		}
		btCollisionShape* Shape = Record.Body->getCollisionShape();
		delete Record.Body;
		DestroyShape(Shape);
		Record.Body = nullptr;
	}

	PtResult FPhysScene_Bullet::CreateActors(const Abi::FActorBatchView& Batch)
	{
		const uint32 First = static_cast<uint32>(Bodies.size());
		const uint32 Num = Batch.Actors.Num();
		const int32 Total = static_cast<int32>(First + Num);
		// Every per-body list is reserved to the body count here (T_create): T_push and T_pull never allocate.
		Bodies.reserve(Total);
		MobilityRanks.reserve(Total);
		NonStaticBodies.reserve(Total);
		DynamicBodies.reserve(Total);
		Kinematics.reserve(Total);
		PreTickBodies.reserve(Total);
		DirtyBodies.reserve(Total);
		PendingImpulseBodies.reserve(Total);
		PendingForceBodies.reserve(Total);
		ReadScratch.reserve(Total);

		btAlignedObjectArray<btRigidBody*> Created;
		Created.reserve(static_cast<int>(Num));
		const auto Abort = [&](const PtResult Code, const char* What, const uint32 Index) -> PtResult
		{
			// All or nothing: nothing was added to the world yet.
			for (int32 Slot = 0; Slot < Created.size(); ++Slot)
			{
				btCollisionShape* Shape = Created[Slot]->getCollisionShape();
				delete Created[Slot];
				DestroyShape(Shape);
			}
			return GetKit().SetLastError(Code, "Bullet: actor %u: %s", Index, What);
		};

		// 1 + 2: per body CreateActor with the creation-time flags, then shape, filter and material (UE order, 02 6).
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			// Bullet keeps one friction / restitution per collision object: the first element's material (docs/Abi.md 7.1,
			// the Jolt limit; per-child materials through gContactAddedCallback are M9 work).
			const PtPhysicalMaterial& Material = Materials[static_cast<int32>(Batch.Shapes[Info.FirstShape].MaterialIndex)];
			Abi::EShapeWrapKind Kind = Abi::EShapeWrapKind::None;
			btCollisionShape* Shape = MakeBodyShape(Info, Batch.Shapes, Kind);
			if (Shape == nullptr)
			{
				return Abort(PT_E_ENGINE, "its shape could not be built", Index);
			}
			const bool bDynamic = Info.Motion == PT_MOTION_DYNAMIC;
			const bool bComIdentity = !bDynamic || Abi::IsComLocalIdentity(Info.CenterOfMassLocal);
			const PtTransform BodyPose = bComIdentity ? Info.ActorTransform : TransformMultiply(Info.ActorTransform, Info.CenterOfMassLocal);
			// Harness mass properties (D5): the principal moments in the principal (body) frame, kg*m^2. Static and kinematic
			// bodies get mass 0 (Bullet's infinite mass).
			btRigidBody::btRigidBodyConstructionInfo Construction(bDynamic ? btScalar(Info.Mass) : btScalar(0.0), nullptr, Shape,
				bDynamic ? ToBtInertial(Info.InertiaDiagonal) : btVector3(0, 0, 0));
			Construction.m_startWorldTransform = ToBtTransform(BodyPose);
			Construction.m_friction = btScalar(Material.Friction);
			Construction.m_restitution = btScalar(Material.Restitution);
			Construction.m_rollingFriction = 0;
			Construction.m_spinningFriction = 0;
			// Damping is an InitDynamicProperties value (step 4).
			Construction.m_linearDamping = 0;
			Construction.m_angularDamping = 0;
			// Bullet's own per-body sleep thresholds (the variant's; UE's material thresholds are mapped in M9).
			Construction.m_linearSleepingThreshold = btScalar(GScale.Length(Options.LinearSleepingThresholdCm));
			Construction.m_angularSleepingThreshold = btScalar(Options.AngularSleepingThreshold);
			btRigidBody* Body = new btRigidBody(Construction);
			if (Info.Motion == PT_MOTION_KINEMATIC)
			{
				// A kinematic body is mass 0 plus CF_KINEMATIC_OBJECT; setMassProps(0) also set CF_STATIC_OBJECT, which would
				// keep it out of the world's non-static list (btDiscreteDynamicsWorld::addRigidBody [V]).
				Body->setCollisionFlags((Body->getCollisionFlags() & ~btCollisionObject::CF_STATIC_OBJECT) | btCollisionObject::CF_KINEMATIC_OBJECT);
			}
			// Creation-time flags (CreateActor, BodyInstance.cpp:1301-1340 [V]). Bullet's default rigid-body flag is
			// BT_ENABLE_GYROSCOPIC_FORCE_IMPLICIT_BODY (btRigidBody.cpp:91 [V]); UE's bGyroscopicTorqueEnabled decides.
			int RigidBodyFlags = 0;
			if ((Info.Flags & PT_BODY_ENABLE_GRAVITY) == 0)
			{
				RigidBodyFlags |= BT_DISABLE_WORLD_GRAVITY;
			}
			if ((Info.Flags & PT_BODY_GYROSCOPIC) != 0)
			{
				RigidBodyFlags |= BT_ENABLE_GYROSCOPIC_FORCE_IMPLICIT_BODY;
			}
			Body->setFlags(RigidBodyFlags);
			if (bDynamic && (Info.Flags & PT_BODY_USE_CCD) != 0)
			{
				// [R] Bullet's continuous collision: a swept sphere once the body moves more than its smallest half extent in a
				// step (btDiscreteDynamicsWorld.cpp:951-1045 [V]); the sizes are the adapter's choice, recorded (ccd.enabled).
				const double Smallest = GScale.Length(GetSmallestExtent(Info, Batch.Shapes));
				Body->setCcdMotionThreshold(btScalar(Smallest));
				Body->setCcdSweptSphereRadius(btScalar(0.8 * Smallest));
			}
			Body->setUserIndex(static_cast<int>(Info.BodyIndex));
			Body->setUserIndex2(static_cast<int>(Abi::PackCombineModes(Material.FrictionCombineMode, Material.RestitutionCombineMode)));
			NumIterationOverrides += (Info.Flags & PT_BODY_OVERRIDE_ITERATIONS) != 0 ? 1u : 0u;
			Created.push_back(Body);
		}

		// 3: AddActorsToScene once for the whole batch (Bullet has no batch add: one pass after every body exists).
		// Filter: group = 1 << ObjectType, mask = BlockMask; Bullet's broad-phase test is UE's mutual block check.
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			World->addRigidBody(Created[static_cast<int32>(Index)], static_cast<int>(1u << Info.ObjectType), static_cast<int>(Info.BlockMask));
		}

		// 4 + 5: per body InitDynamicProperties in UE order, then put-to-sleep for !bStartAwake.
		const double NominalDt = Desc.NominalDeltaTime;
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			btRigidBody* Body = Created[static_cast<int32>(Index)];
			const bool bDynamic = Info.Motion == PT_MOTION_DYNAMIC;
			FBulletBody Record;
			Record.Body = Body;
			Record.Motion = Info.Motion;
			Record.Flags = Info.Flags;
			Record.MaterialIndex = Batch.Shapes[Info.FirstShape].MaterialIndex;
			Record.Mass = Info.Mass;
			Record.InertiaDiagonal = Info.InertiaDiagonal;
			Record.HarnessComLocal = Info.CenterOfMassLocal;
			Record.bComIdentity = !bDynamic || Abi::IsComLocalIdentity(Info.CenterOfMassLocal);
			Record.ComLocal = Record.bComIdentity ? GIdentityTransform : Info.CenterOfMassLocal;
			Record.ComLocalInverse = TransformInverse(Record.ComLocal);
			Record.KinematicTarget = Info.ActorTransform;
			Record.KinematicPose = Info.ActorTransform;
			if (bDynamic)
			{
				// Damping: UE's coefficients, applied in UE's order by the pre-tick (DampingModel=UE), or converted for
				// Bullet's own damping (DampingModel=Converted, 02 8.1).
				Record.LinearDamping = Info.LinearDamping;
				Record.AngularDamping = Info.AngularDamping;
				if (Options.DampingModel == EDampingModel::Converted)
				{
					Body->setDamping(btScalar(Abi::UEDampingToBullet(Info.LinearDamping, NominalDt)), btScalar(Abi::UEDampingToBullet(Info.AngularDamping, NominalDt)));
				}
				// Max angular velocity (and the scene / body linear limit): the pre-tick clamps them.
				Record.MaxAngularVelocity = Info.MaxAngularVelocity;
				Record.MaxLinearVelocity = Info.MaxLinearVelocity > 0.0 ? Info.MaxLinearVelocity : Desc.MaxLinearVelocity;
				// (max depenetration velocity: Bullet has no per-body initial-overlap rate; see the effective settings)
				NumDepenetrationOverrides += Info.MaxDepenetrationVelocity >= 0.0 ? 1u : 0u;
				Body->setLinearVelocity(ToBtLength(Info.InitialLinearVelocity));
				Body->setAngularVelocity(ToBtVector(Info.InitialAngularVelocity));
				// (sleep / stabilisation threshold multipliers: Bullet's thresholds are the variant's, M9)
				const bool bUEDamping = Options.DampingModel == EDampingModel::UEOrder && (Info.LinearDamping > 0.0 || Info.AngularDamping > 0.0);
				if (bUEDamping || Record.MaxAngularVelocity > 0.0 || Record.MaxLinearVelocity > 0.0)
				{
					PreTickBodies.push_back(First + Index);
				}
				if (!bSleepingAllowed)
				{
					Body->forceActivationState(DISABLE_DEACTIVATION);   // DL48 (btCollisionObject.h:25 [V])
				}
				else if ((Info.Flags & PT_BODY_START_AWAKE) == 0)
				{
					// The body is in the world already (BodyInstance.cpp:4723-4733 [V]); as PhysX putToSleep, zero velocity.
					Body->setActivationState(ISLAND_SLEEPING);
					Body->setLinearVelocity(btVector3(0, 0, 0));
					Body->setAngularVelocity(btVector3(0, 0, 0));
				}
				if (!bSleepingAllowed && (Info.Flags & PT_BODY_START_AWAKE) == 0)
				{
					++NumStartAsleepSkipped;   // 02 6 step 5 is skipped when sleeping is off (Normalized)
				}
				DynamicBodies.push_back(First + Index);
				NonStaticBodies.push_back(First + Index);
			}
			else if (Info.Motion == PT_MOTION_KINEMATIC)
			{
				if (!bSleepingAllowed)
				{
					Body->forceActivationState(DISABLE_DEACTIVATION);
				}
				Kinematics.push_back(First + Index);
				NonStaticBodies.push_back(First + Index);
			}
			Bodies.push_back(Record);
			MobilityRanks.push_back(Abi::GetMobilityRank(Info.Motion));
			MarkDirty(First + Index);   // reported by the next ACTIVE_ONLY read
		}
		if (Options.DampingModel == EDampingModel::Converted)
		{
			DampingDeltaTime = static_cast<float>(NominalDt);
		}
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::ReleaseActors(const uint32_t* BodyIndices, const uint32_t Num)
	{
		// The kit has rejected out-of-range, released and repeated indices.
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			FBulletBody& Record = Bodies[static_cast<int32>(BodyIndices[Index])];
			DestroyBody(Record);
			Record.bReleased = true;
		}
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::PrepareForSimulation(Abi::FTextWriter& OutActions)
	{
		// The same untimed opportunity for every engine (02 9.5): the broad-phase trees are rebuilt top-down once here and
		// the AABBs computed. The pools were written through at construction (btPoolAllocator's free list), so their pages
		// are already committed.
		Broadphase->optimize();
		World->updateAabbs();
		const btPoolAllocator& Manifolds = Dispatcher->GetManifoldPool();
		const btPoolAllocator& Algorithms = Dispatcher->GetAlgorithmPool();
		OutActions.Appendf("bullet: btDbvtBroadphase::optimize + updateAabbs (%d collision objects); pools: %d manifolds x %d B, %d algorithms x %d B",
			World->getNumCollisionObjects(), Manifolds.getMaxCount(), Manifolds.getElementSize(), Algorithms.getMaxCount(), Algorithms.getElementSize());
		return PT_OK;
	}

	// ---- frame -------------------------------------------------------------------------------------------------------
	uint32 FPhysScene_Bullet::TakePoolErrorBits()
	{
		uint32 Bits = 0;
		if (Dispatcher->NumManifoldHeapAllocations != ManifoldHeapSeen)
		{
			Bits |= GBulletErrorManifoldPoolFallback;
			ManifoldHeapSeen = Dispatcher->NumManifoldHeapAllocations;
		}
		if (Dispatcher->NumAlgorithmHeapAllocations != AlgorithmHeapSeen)
		{
			Bits |= GBulletErrorAlgorithmPoolFallback;
			AlgorithmHeapSeen = Dispatcher->NumAlgorithmHeapAllocations;
		}
		return Bits;
	}

	PtResult FPhysScene_Bullet::SetUpForFrame(const Abi::FFrameParams& Params)
	{
		// SDK backends set the gravity only when it changes (DL32). btDiscreteDynamicsWorld::setGravity updates the AWAKE bodies
		// only (btDiscreteDynamicsWorld.cpp:499-510 [V]); a sleeping body would keep the old value after its wake, so the
		// sleeping ones get it here too.
		if (Params.bNewGravity && (Params.Gravity.X != Gravity.X || Params.Gravity.Y != Gravity.Y || Params.Gravity.Z != Gravity.Z))
		{
			Gravity = Params.Gravity;
			const btVector3 NewGravity = ToBtLength(Gravity);
			World->setGravity(NewGravity);
			for (int32 Slot = 0; Slot < DynamicBodies.size(); ++Slot)
			{
				FBulletBody& Record = Bodies[static_cast<int32>(DynamicBodies[Slot])];
				if (!Record.bReleased && (Record.Body->getFlags() & BT_DISABLE_WORLD_GRAVITY) == 0 && !Record.Body->isActive())
				{
					Record.Body->setGravity(NewGravity);
				}
			}
		}
		StepDeltaTime = Params.DeltaSeconds;   // one step per call (no NATIVE_FRAME_MODEL: MaxSubsteps 1, no substepping)
		bStepped = false;
		// Pool overflows outside a step (none are expected: Bullet allocates manifolds and algorithms in the narrow phase)
		// belong to this frame.
		FrameErrorBits = TakePoolErrorBits();
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::StartFrame()
	{
		// stepSimulation does nothing for btFuzzyZero(dt) (btDiscreteDynamicsWorld.cpp:405-409 [V]); the Harness driver never
		// sends such a step. As in Null, the push consumed the forces; impulses stay pending for the next step that runs.
		if (!(StepDeltaTime > 0.0f) || btFuzzyZero(btScalar(StepDeltaTime)))
		{
			for (int32 Slot = 0; Slot < PendingForceBodies.size(); ++Slot)
			{
				FBulletBody& Record = Bodies[static_cast<int32>(PendingForceBodies[Slot])];
				Record.PendingForce = PtVec3{};
				Record.PendingTorque = PtVec3{};
				Record.bPendingForce = false;
			}
			PendingForceBodies.resizeNoInitialize(0);
			return PT_OK;
		}
		const double Dt = static_cast<double>(StepDeltaTime);
		// Kinematic bodies: a pending target moves the body there for this step (its world transform: the collision
		// detection of this step sees it there, as Chaos' ApplyKinematicTargets moves the particle first); the pre-tick
		// gives it the velocity that reaches it (PhysScene_Bullet.h). A body without a target stops (Chaos mode Reset).
		for (int32 Slot = 0; Slot < Kinematics.size(); ++Slot)
		{
			FBulletBody& Record = Bodies[static_cast<int32>(Kinematics[Slot])];
			if (Record.bReleased)
			{
				continue;
			}
			if (Record.bHasKinematicTarget)
			{
				// The origin's displacement over the step (Chaos computes V from the particle X, PBDRigidsEvolutionGBF.cpp:
				// 1180-1239 [V]); a kinematic body's frame is its actor frame, so this is also what the solver needs.
				const PtTransform& From = Record.KinematicPose;
				const PtTransform& To = Record.KinematicTarget;
				Record.KinematicOriginVelocity = Scale(Sub(To.Translation, From.Translation), 1.0 / Dt);
				Record.KinematicAngularVelocity = AngularVelocityBetween(From.Rotation, To.Rotation, Dt);
				Record.KinematicPose = To;
				Record.Body->setWorldTransform(ToBtTransform(To));
				Record.Body->activate(true);   // a sleeping kinematic body would be skipped (and wakes what it touches)
				Record.bHasKinematicTarget = false;
				Record.bKinematicMovedLastStep = true;
			}
			else if (Record.bKinematicMovedLastStep)
			{
				Record.KinematicOriginVelocity = PtVec3{};
				Record.KinematicAngularVelocity = PtVec3{};
				Record.bKinematicMovedLastStep = false;
			}
		}
		ApplyPendingDynamics();
		if (Options.DampingModel == EDampingModel::Converted && StepDeltaTime != DampingDeltaTime)
		{
			ApplyConvertedDamping(StepDeltaTime);
		}
		FrameErrorBits |= TakePoolErrorBits();   // the commands above (none allocate today)
		// One variable step of exactly dt (maxSubSteps 0): the harness owns the frame plan (02 9.1).
		World->stepSimulation(btScalar(StepDeltaTime), 0, btScalar(StepDeltaTime));
		FrameErrorBits |= TakePoolErrorBits();
		TotalErrorBits |= FrameErrorBits;
		bStepped = true;
		++TotalSteps;
		return PT_OK;
	}

	void FPhysScene_Bullet::ApplyConvertedDamping(const float StepDt)
	{
		for (int32 Slot = 0; Slot < DynamicBodies.size(); ++Slot)
		{
			FBulletBody& Record = Bodies[static_cast<int32>(DynamicBodies[Slot])];
			if (!Record.bReleased)
			{
				Record.Body->setDamping(btScalar(Abi::UEDampingToBullet(Record.LinearDamping, StepDt)), btScalar(Abi::UEDampingToBullet(Record.AngularDamping, StepDt)));
			}
		}
		DampingDeltaTime = StepDt;
	}

	void FPhysScene_Bullet::ApplyPendingDynamics()
	{
		// Velocity changes: one change at the step, added to the engine velocity before stepSimulation integrates gravity
		// and the forces, as Chaos adds LinearImpulseVelocity in Integrate (PBDRigidsEvolutionGBF.cpp:920-924 [V]). A body
		// asleep now (PUT_TO_SLEEP after the impulse) keeps the change pending until a step finds it awake (as Null, Jolt).
		int32 Kept = 0;
		for (int32 Slot = 0; Slot < PendingImpulseBodies.size(); ++Slot)
		{
			const uint32 Index = PendingImpulseBodies[Slot];
			FBulletBody& Record = Bodies[static_cast<int32>(Index)];
			if (Record.bReleased)
			{
				Record.bPendingImpulse = false;
				continue;
			}
			btRigidBody& Body = *Record.Body;
			if (!Body.isActive())
			{
				PendingImpulseBodies[Kept++] = Index;
				continue;
			}
			const PtVec3& Dv = Record.PendingLinearVelocity;
			const PtVec3& Dw = Record.PendingAngularVelocity;
			Body.setLinearVelocity(Body.getLinearVelocity() + btVector3(btScalar(Dv.X), btScalar(Dv.Y), btScalar(Dv.Z)));
			Body.setAngularVelocity(Body.getAngularVelocity() + btVector3(btScalar(Dw.X), btScalar(Dw.Y), btScalar(Dw.Z)));
			Record.PendingLinearVelocity = PtVec3{};
			Record.PendingAngularVelocity = PtVec3{};
			Record.bPendingImpulse = false;
		}
		PendingImpulseBodies.resizeNoInitialize(Kept);

		// Forces and torques of this push act on this step only, and only on a body awake now; stepSimulation's clearForces
		// clears the accumulators of every non-static body afterwards (btDiscreteDynamicsWorld.cpp:309-319, 443 [V]).
		for (int32 Slot = 0; Slot < PendingForceBodies.size(); ++Slot)
		{
			FBulletBody& Record = Bodies[static_cast<int32>(PendingForceBodies[Slot])];
			if (!Record.bReleased && Record.Body->isActive())
			{
				const PtVec3& F = Record.PendingForce;
				const PtVec3& T = Record.PendingTorque;
				Record.Body->applyCentralForce(btVector3(btScalar(F.X), btScalar(F.Y), btScalar(F.Z)));
				Record.Body->applyTorque(btVector3(btScalar(T.X), btScalar(T.Y), btScalar(T.Z)));
			}
			Record.PendingForce = PtVec3{};
			Record.PendingTorque = PtVec3{};
			Record.bPendingForce = false;
		}
		PendingForceBodies.resizeNoInitialize(0);
	}

	void FPhysScene_Bullet::OnPreTick(const btScalar TimeStep)
	{
		const double Dt = static_cast<double>(TimeStep);
		// Kinematic velocities (computed in double at StartFrame): the target's, or zero without one (Reset). This replaces
		// what saveKinematicState differentiated a moment ago.
		for (int32 Slot = 0; Slot < Kinematics.size(); ++Slot)
		{
			const FBulletBody& Record = Bodies[static_cast<int32>(Kinematics[Slot])];
			if (Record.bReleased || !Record.Body->isActive())
			{
				continue;
			}
			Record.Body->setLinearVelocity(ToBtLength(Record.KinematicOriginVelocity));
			Record.Body->setAngularVelocity(ToBtVector(Record.KinematicAngularVelocity));
		}
		// Dynamic bodies: UE's damping order and speed limits on the velocity the solver will end at (header comment). The
		// solver adds a dt = F / m dt and alpha dt = I_world^-1 tau dt (btSequentialImpulseConstraintSolver.cpp:477-478 [V]);
		// gravity is in the force already (applyGravity ran before this callback).
		const bool bUEDamping = Options.DampingModel == EDampingModel::UEOrder;
		for (int32 Slot = 0; Slot < PreTickBodies.size(); ++Slot)
		{
			const FBulletBody& Record = Bodies[static_cast<int32>(PreTickBodies[Slot])];
			if (Record.bReleased || !Record.Body->isActive())
			{
				continue;
			}
			btRigidBody& Body = *Record.Body;
			const double LinearMultiplier = bUEDamping ? Abi::UEDampingMultiplier(Record.LinearDamping, Dt) : 1.0;
			const double MaxLinear = Record.MaxLinearVelocity > 0.0 ? GScale.Length(Record.MaxLinearVelocity) : 0.0;
			if (LinearMultiplier != 1.0 || MaxLinear > 0.0)
			{
				const btVector3 V = Body.getLinearVelocity();
				const btVector3 ForceDv = Body.getTotalForce() * (Body.getInvMass() * TimeStep);
				double Pre[3];
				double End[3];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					Pre[Axis] = Abi::UEOrderPreSolveVelocity(double(V[Axis]), double(ForceDv[Axis]), LinearMultiplier);
					End[Axis] = Pre[Axis] + double(ForceDv[Axis]);
				}
				const double Speed2 = End[0] * End[0] + End[1] * End[1] + End[2] * End[2];
				const bool bClamp = MaxLinear > 0.0 && Speed2 > MaxLinear * MaxLinear;
				if (bClamp)
				{
					const double Factor = MaxLinear / sqrt(Speed2);
					for (int Axis = 0; Axis < 3; ++Axis)
					{
						Pre[Axis] = End[Axis] * Factor - double(ForceDv[Axis]);
					}
				}
				if (LinearMultiplier != 1.0 || bClamp)
				{
					Body.setLinearVelocity(btVector3(btScalar(Pre[0]), btScalar(Pre[1]), btScalar(Pre[2])));
				}
			}
			const double AngularMultiplier = bUEDamping ? Abi::UEDampingMultiplier(Record.AngularDamping, Dt) : 1.0;
			const double MaxAngular = Record.MaxAngularVelocity;
			if (AngularMultiplier != 1.0 || MaxAngular > 0.0)
			{
				const btVector3 W = Body.getAngularVelocity();
				const btVector3& Torque = Body.getTotalTorque();
				const btVector3 TorqueDw = Torque.isZero() ? btVector3(0, 0, 0) : Body.getInvInertiaTensorWorld() * Torque * TimeStep;
				double Pre[3];
				double End[3];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					Pre[Axis] = Abi::UEOrderPreSolveVelocity(double(W[Axis]), double(TorqueDw[Axis]), AngularMultiplier);
					End[Axis] = Pre[Axis] + double(TorqueDw[Axis]);
				}
				const double Speed2 = End[0] * End[0] + End[1] * End[1] + End[2] * End[2];
				const bool bClamp = MaxAngular > 0.0 && Speed2 > MaxAngular * MaxAngular;
				if (bClamp)
				{
					const double Factor = MaxAngular / sqrt(Speed2);
					for (int Axis = 0; Axis < 3; ++Axis)
					{
						Pre[Axis] = End[Axis] * Factor - double(TorqueDw[Axis]);
					}
				}
				if (AngularMultiplier != 1.0 || bClamp)
				{
					Body.setAngularVelocity(btVector3(btScalar(Pre[0]), btScalar(Pre[1]), btScalar(Pre[2])));
				}
			}
		}
	}

	void FPhysScene_Bullet::OnPostTick(const btScalar TimeStep)
	{
		// Bullet's own limit: above pi/4 per step integrateTransform under-rotates the body (btTransformUtil.h:44-72 [V]);
		// count the awake bodies whose angular velocity was above it this step (their rotation was cut short).
		const double Limit = GBulletAngularMotionThreshold / static_cast<double>(TimeStep);
		const btScalar Limit2 = btScalar(Limit * Limit);
		for (int32 Slot = 0; Slot < DynamicBodies.size(); ++Slot)
		{
			const FBulletBody& Record = Bodies[static_cast<int32>(DynamicBodies[Slot])];
			if (!Record.bReleased && Record.Body->isActive() && Record.Body->getAngularVelocity().length2() > Limit2)
			{
				++AngularMotionLimitHits;
			}
		}
		const int32 Manifolds = Dispatcher->GetManifoldPool().getUsedCount();
		const int32 Algorithms = Dispatcher->GetAlgorithmPool().getUsedCount();
		PeakManifoldsUsed = Manifolds > PeakManifoldsUsed ? Manifolds : PeakManifoldsUsed;
		PeakAlgorithmsUsed = Algorithms > PeakAlgorithmsUsed ? Algorithms : PeakAlgorithmsUsed;
	}

	PtResult FPhysScene_Bullet::WaitPhysScenes()
	{
		return PT_OK;   // stepSimulation is synchronous
	}

	PtResult FPhysScene_Bullet::EndFrame(PtFrameResult& Out)
	{
		Out.NumStepsTaken = bStepped ? 1u : 0u;
		Out.EngineErrorBits = FrameErrorBits;
		return PT_OK;
	}

	// ---- commands ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_Bullet::ValidateBodyCommand(const PtBodyCommand& Cmd)
	{
		// Every check that can reject a command is here, so ApplyBodyCommand never fails (batch atomicity, docs/Abi.md 5.5).
		if (Cmd.Type == PT_CMD_SET_KINEMATIC_TARGET && Bodies[static_cast<int32>(Cmd.Body)].Motion != PT_MOTION_KINEMATIC)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Bullet: SET_KINEMATIC_TARGET on body %u, which is %s", Cmd.Body,
				GetMotionName(Bodies[static_cast<int32>(Cmd.Body)].Motion));
		}
		return PT_OK;
	}

	void FPhysScene_Bullet::DropContacts(const FBulletBody& Record)
	{
		// The body's manifolds go with its pairs' algorithms; the pairs stay, so the next narrow phase rebuilds what is
		// awake (btHashedOverlappingPairCache::cleanProxyFromPairs [V]). A pair asleep on both sides keeps none.
		if (Record.Body->getBroadphaseHandle() != nullptr)
		{
			World->getPairCache()->cleanProxyFromPairs(Record.Body->getBroadphaseHandle(), Dispatcher);
		}
	}

	void FPhysScene_Bullet::SetBodyPose(FBulletBody& Record, const PtTransform& ActorPose)
	{
		const btTransform Pose = ToBtTransform(Record.bComIdentity ? ActorPose : TransformMultiply(ActorPose, Record.ComLocal));
		if (Record.Motion == PT_MOTION_DYNAMIC)
		{
			Record.Body->setCenterOfMassTransform(Pose);   // also the interpolation transform and the world inertia
		}
		else
		{
			Record.Body->setWorldTransform(Pose);
			Record.Body->setInterpolationWorldTransform(Pose);
		}
		if (Record.Motion == PT_MOTION_KINEMATIC)
		{
			Record.KinematicPose = ActorPose;
		}
		World->updateSingleAabb(Record.Body);
		DropContacts(Record);   // the kept points would be stale
	}

	PtResult FPhysScene_Bullet::ApplyBodyCommand(const PtBodyCommand& Cmd)
	{
		FBulletBody& Record = Bodies[static_cast<int32>(Cmd.Body)];
		btRigidBody& Body = *Record.Body;
		const bool bDynamic = Record.Motion == PT_MOTION_DYNAMIC;
		const auto Wake = [&]()
		{
			if (bDynamic && !Body.isActive())
			{
				Body.activate();   // ACTIVE_TAG, deactivation time 0 (a DISABLE_DEACTIVATION body is always active)
				MarkDirty(Cmd.Body);
			}
		};
		switch (Cmd.Type)
		{
		case PT_CMD_SET_GLOBAL_POSE:
			// UE5's teleport wakes a sleeping body whatever bAutoWake says (SetX, SingleParticlePhysicsProxy.h:240-251 [V]);
			// a kinematic body also gets the pose as its target with zero velocity (ChaosEngineInterface.cpp:2550-2557 [V]).
			SetBodyPose(Record, Cmd.Transform);
			if (bDynamic)
			{
				Wake();
			}
			else if (Record.Motion == PT_MOTION_KINEMATIC)
			{
				Body.setLinearVelocity(btVector3(0, 0, 0));
				Body.setAngularVelocity(btVector3(0, 0, 0));
				Record.KinematicTarget = Cmd.Transform;
				Record.KinematicOriginVelocity = PtVec3{};
				Record.KinematicAngularVelocity = PtVec3{};
				Record.bHasKinematicTarget = false;
				Record.bKinematicMovedLastStep = false;
			}
			MarkDirty(Cmd.Body);
			break;
		case PT_CMD_SET_KINEMATIC_TARGET:
			Record.KinematicTarget = Cmd.Transform;   // the last target before a step wins
			Record.bHasKinematicTarget = true;
			break;
		case PT_CMD_SET_LINEAR_VELOCITY:
		case PT_CMD_SET_ANGULAR_VELOCITY:
			if (bDynamic)
			{
				if (Cmd.Type == PT_CMD_SET_LINEAR_VELOCITY)
				{
					Body.setLinearVelocity(ToBtLength(Cmd.Vector));
				}
				else
				{
					Body.setAngularVelocity(ToBtVector(Cmd.Vector));
				}
				MarkDirty(Cmd.Body);
				if ((Cmd.Flags & PT_CMDF_AUTO_WAKE) != 0)
				{
					Wake();
				}
			}
			break;
		case PT_CMD_SET_BODY_STATE:
			SetBodyPose(Record, Cmd.Transform);   // no wake (a sleeping body stays asleep, its pairs without points)
			if (bDynamic)
			{
				Body.setLinearVelocity(ToBtLength(Cmd.Vector));
				Body.setAngularVelocity(ToBtVector(Cmd.Vector2));
			}
			MarkDirty(Cmd.Body);
			break;
		case PT_CMD_ADD_IMPULSE:
		case PT_CMD_ADD_VELOCITY:
			if (bDynamic)
			{
				Wake();   // [R] the Chaos game-thread impulse setter wakes the particle (as Null, Jolt)
				// Deferred to the step (ApplyPendingDynamics), never applyCentralImpulse, which changes the velocity now.
				// FChaosStateOps::AddVelocity is AddImpulse(M * dv) (ChaosEngineInterface.cpp:861-864 [V]): dv as it is.
				const double Factor = Cmd.Type == PT_CMD_ADD_IMPULSE ? double(Body.getInvMass()) : 1.0;   // kg*m/s -> m/s
				const PtVec3 Dv = GScale.Length(Cmd.Vector);
				Record.PendingLinearVelocity = Add(Record.PendingLinearVelocity, PtVec3{ Dv.X * Factor, Dv.Y * Factor, Dv.Z * Factor });
				if (!Record.bPendingImpulse)
				{
					Record.bPendingImpulse = true;
					PendingImpulseBodies.push_back(Cmd.Body);
				}
			}
			break;
		case PT_CMD_ADD_ANGULAR_IMPULSE:
		case PT_CMD_ADD_ANGULAR_VELOCITY:
			if (bDynamic)
			{
				Wake();
				// The angular impulse through the world inverse inertia at push time, as Null (kg*cm^2/s -> kg*m^2/s -> rad/s).
				// AddAngularVelocityInRadians is an angular impulse of I_world * dw (ChaosEngineInterface.cpp:866-870 [V]): dw.
				const btVector3 Dw = Cmd.Type == PT_CMD_ADD_ANGULAR_IMPULSE ? Body.getInvInertiaTensorWorld() * ToBtInertial(Cmd.Vector) : ToBtVector(Cmd.Vector);
				Record.PendingAngularVelocity = Add(Record.PendingAngularVelocity, FromBtVector(Dw));
				if (!Record.bPendingImpulse)
				{
					Record.bPendingImpulse = true;
					PendingImpulseBodies.push_back(Cmd.Body);
				}
			}
			break;
		case PT_CMD_ADD_FORCE:
		case PT_CMD_ADD_TORQUE:
			if (bDynamic)
			{
				if ((Cmd.Flags & PT_CMDF_NO_WAKE) != 0 && !Body.isActive())
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
					const PtVec3 Force = GScale.Length(Cmd.Vector);
					const double Factor = bAccelChange ? Record.Mass : 1.0;
					Record.PendingForce = Add(Record.PendingForce, PtVec3{ Force.X * Factor, Force.Y * Factor, Force.Z * Factor });
				}
				else
				{
					// bAccelChange: I_world * torque (ChaosEngineInterface.cpp:1029-1032 [V]) at push time; the vector is then an
					// angular acceleration (rad/s^2) and Bullet's world inertia is already in kg*m^2.
					const btVector3 Torque = bAccelChange ? MultiplyWorldInertia(Body, ToBtVector(Cmd.Vector)) : ToBtInertial(Cmd.Vector);
					Record.PendingTorque = Add(Record.PendingTorque, FromBtVector(Torque));
				}
				// Handed to applyCentralForce / applyTorque at the step if the body is awake then (ApplyPendingDynamics).
				if (!Record.bPendingForce)
				{
					Record.bPendingForce = true;
					PendingForceBodies.push_back(Cmd.Body);
				}
			}
			break;
		case PT_CMD_WAKE_UP:
			Wake();
			break;
		case PT_CMD_PUT_TO_SLEEP:
			// Ignored when the scene disables sleeping (as PhysX rejects putToSleep in an eDISABLE_SLEEPING scene).
			if (bDynamic && bSleepingAllowed && Body.isActive())
			{
				// Zero velocity, as Null / Chaos / Jolt. The deferred impulse stays pending and the deferred force is dropped
				// at the step unless a later command of this push wakes the body again (as Null). Its manifolds stay: Bullet
				// keeps a sleeping pair's points (the M4 kept-contact rule needs no bookkeeping here).
				Body.setActivationState(ISLAND_SLEEPING);
				Body.setLinearVelocity(btVector3(0, 0, 0));
				Body.setAngularVelocity(btVector3(0, 0, 0));
				MarkDirty(Cmd.Body);
			}
			break;
		default:
			break;   // the kit validated the type
		}
		return PT_OK;
	}

	// ---- reads -------------------------------------------------------------------------------------------------------
	bool FPhysScene_Bullet::IsActiveForRead(const FBulletBody& Record) const
	{
		if (Record.Motion == PT_MOTION_DYNAMIC)
		{
			return Record.Body->isActive();
		}
		return Record.Motion == PT_MOTION_KINEMATIC && Record.bKinematicMovedLastStep;
	}

	PtBodyState FPhysScene_Bullet::MakeState(const FBulletBody& Record) const
	{
		PtBodyState State = {};
		const PtTransform BodyPose = FromBtTransform(Record.Body->getWorldTransform());
		State.ActorTransform = Record.bComIdentity ? BodyPose : TransformMultiply(BodyPose, Record.ComLocalInverse);
		switch (Record.Motion)
		{
		case PT_MOTION_DYNAMIC:
			State.LinearVelocity = FromBtLength(Record.Body->getLinearVelocity());   // of the COM
			State.AngularVelocity = FromBtVector(Record.Body->getAngularVelocity());
			State.ObjectState = Record.Body->isActive() ? PT_OBJECT_STATE_DYNAMIC : PT_OBJECT_STATE_SLEEPING;
			break;
		case PT_MOTION_KINEMATIC:
			// The actor origin's velocity of the last move (exact, double), zero without one (PtBodyState).
			State.LinearVelocity = Record.bKinematicMovedLastStep ? Record.KinematicOriginVelocity : PtVec3{};
			State.AngularVelocity = Record.bKinematicMovedLastStep ? Record.KinematicAngularVelocity : PtVec3{};
			State.ObjectState = PT_OBJECT_STATE_KINEMATIC;
			break;
		default:
			State.ObjectState = PT_OBJECT_STATE_STATIC;
			break;
		}
		State.Flags = 0;
		return State;
	}

	PtResult FPhysScene_Bullet::ReadBodyStates(const uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, const uint32_t Capacity, uint32_t& OutCount)
	{
		if (ReadFlags == PT_READ_ALL)
		{
			// Dense: every created body in index order, a released one as UNINITIALIZED (the kit writes OutIndices).
			OutCount = static_cast<uint32>(Bodies.size());
			if (Capacity < OutCount)
			{
				return PT_E_CAPACITY;
			}
			for (uint32 Index = 0; Index < OutCount; ++Index)
			{
				FBulletBody& Record = Bodies[static_cast<int32>(Index)];
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
				Record.bDirtyListed = false;
				Record.bReportedActive = IsActiveForRead(Record);
			}
			DirtyBodies.resizeNoInitialize(0);
			return PT_OK;
		}

		// ACTIVE_ONLY: Bullet keeps no active list, so the non-static bodies are walked (as its synchronizeMotionStates
		// does): the awake ones, the ones the previous read saw awake (they fell asleep: their ObjectState changed) and the
		// ones a command changed (static ones included). One stamp per read dedupes; nothing is consumed on PT_E_CAPACITY.
		const uint32 Stamp = ++ReadStamp;
		ReadScratch.resizeNoInitialize(0);
		const auto AddToReport = [this, Stamp](const uint32 Index)
		{
			FBulletBody& Record = Bodies[static_cast<int32>(Index)];
			if (!Record.bReleased && Record.LastReadStamp != Stamp)
			{
				Record.LastReadStamp = Stamp;
				ReadScratch.push_back(Index);   // reserved to the body count
			}
		};
		for (int32 Slot = 0; Slot < NonStaticBodies.size(); ++Slot)
		{
			const uint32 Index = NonStaticBodies[Slot];
			const FBulletBody& Record = Bodies[static_cast<int32>(Index)];
			if (!Record.bReleased && (Record.bReportedActive || Record.bDirty || IsActiveForRead(Record)))
			{
				AddToReport(Index);
			}
		}
		for (int32 Slot = 0; Slot < DirtyBodies.size(); ++Slot)
		{
			AddToReport(DirtyBodies[Slot]);
		}
		OutCount = static_cast<uint32>(ReadScratch.size());
		if (Capacity < OutCount)
		{
			return PT_E_CAPACITY;   // nothing reported, nothing consumed
		}
		for (uint32 Slot = 0; Slot < OutCount; ++Slot)
		{
			const uint32 Index = ReadScratch[static_cast<int32>(Slot)];
			FBulletBody& Record = Bodies[static_cast<int32>(Index)];
			OutIndices[Slot] = Index;
			Out[Slot] = MakeState(Record);
			Record.bDirty = false;
			Record.bReportedActive = IsActiveForRead(Record);
		}
		for (int32 Slot = 0; Slot < DirtyBodies.size(); ++Slot)
		{
			Bodies[static_cast<int32>(DirtyBodies[Slot])].bDirtyListed = false;
		}
		DirtyBodies.resizeNoInitialize(0);
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::ReadMassProperties(const uint32_t First, const uint32_t Num, PtMassProperties* Out)
	{
		for (uint32 Offset = 0; Offset < Num; ++Offset)
		{
			const FBulletBody& Record = Bodies[static_cast<int32>(First + Offset)];
			PtMassProperties& Mass = Out[Offset];
			if (Record.Motion != PT_MOTION_DYNAMIC)
			{
				// Bullet keeps mass 0 for static and kinematic bodies: the harness values.
				Mass.Mass = Record.Mass;
				Mass.InertiaDiagonal = Record.InertiaDiagonal;
				Mass.CenterOfMassLocal = Record.HarnessComLocal;
				continue;
			}
			// What the engine has (G2 readback): 1 / inverse mass and the local (principal) inertia of the body frame. The body
			// frame IS the COM principal frame by construction (BulletMath.h), so its pose in actor space is the COM frame the
			// adapter placed the shape around (the record): Bullet itself stores no COM offset.
			const btRigidBody& Body = *Record.Body;
			const btVector3 Inertia = Body.getLocalInertia();
			Mass.Mass = 1.0 / double(Body.getInvMass());
			Mass.InertiaDiagonal = PtVec3{ GScale.ToKgCm2(double(Inertia.getX())), GScale.ToKgCm2(double(Inertia.getY())), GScale.ToKgCm2(double(Inertia.getZ())) };
			Mass.CenterOfMassLocal = Record.bComIdentity ? Record.HarnessComLocal : Record.ComLocal;
		}
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::GetStepStats(PtStepStats& Out)
	{
		// Every value here needs no listener (headline-safe): activation states, the dispatcher's manifolds, island tags.
		int32 Dynamic = 0;
		int32 Active = 0;
		IslandScratch.resize(World->getNumCollisionObjects(), 0);
		for (int32 Slot = 0; Slot < IslandScratch.size(); ++Slot)
		{
			IslandScratch[Slot] = 0;
		}
		int32 Islands = 0;
		for (int32 Slot = 0; Slot < DynamicBodies.size(); ++Slot)
		{
			const FBulletBody& Record = Bodies[static_cast<int32>(DynamicBodies[Slot])];
			if (Record.bReleased)
			{
				continue;
			}
			++Dynamic;
			Active += Record.Body->isActive() ? 1 : 0;
			const int Tag = Record.Body->getIslandTag();
			if (Tag >= 0 && Tag < IslandScratch.size() && IslandScratch[Tag] == 0)
			{
				IslandScratch[Tag] = 1;
				++Islands;
			}
		}
		int32 Pairs = 0;
		int32 Points = 0;
		for (int Manifold = 0; Manifold < Dispatcher->getNumManifolds(); ++Manifold)
		{
			const int Count = Dispatcher->getManifoldByIndexInternal(Manifold)->getNumContacts();
			Pairs += Count > 0 ? 1 : 0;
			Points += Count;
		}
		Out.NumActiveBodies = Active;
		Out.NumSleepingBodies = Dynamic - Active;
		Out.NumContactPairs = Pairs;
		Out.NumContactPoints = Points;
		Out.NumIslands = TotalSteps > 0 ? Islands : -1;   // tags exist once a step built the islands
		// A pool overflow seen since the last frame is part of the run's bits; the next frame reports it too.
		const uint32 Pending = (Dispatcher->NumManifoldHeapAllocations != ManifoldHeapSeen ? GBulletErrorManifoldPoolFallback : 0u)
			| (Dispatcher->NumAlgorithmHeapAllocations != AlgorithmHeapSeen ? GBulletErrorAlgorithmPoolFallback : 0u);
		Out.EngineErrorBits = TotalErrorBits | Pending;
		Out.NumPhaseTimes = 0;
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::CaptureDebugSnapshot(const PtDebugSnapshotRequest& Req, PtDebugSnapshotCounts& Out)
	{
		// Stage the snapshot now, so that ReadDebugSnapshot copies exactly what these counts describe.
		SnapshotParticles.resizeNoInitialize(0);
		SnapshotContacts.resizeNoInitialize(0);
		bool bTruncated = false;
		if ((Req.Flags & PT_SNAP_PARTICLES) != 0)
		{
			for (int32 Index = 0; Index < Bodies.size(); ++Index)
			{
				const FBulletBody& Record = Bodies[Index];
				if (Record.bReleased)
				{
					continue;
				}
				const btRigidBody& Body = *Record.Body;
				const PtBodyState State = MakeState(Record);
				PtParticleSnapshot Particle = {};
				Particle.ActorTransform = State.ActorTransform;
				// The COM frame: a dynamic body's world transform; for static / kinematic bodies the harness COM on the actor.
				Particle.CenterOfMassWorld = Record.Motion == PT_MOTION_DYNAMIC ? FromBtTransform(Body.getWorldTransform())
					: TransformMultiply(State.ActorTransform, Record.HarnessComLocal);
				// The broad-phase AABB Bullet uses (shape AABB plus gContactBreakingThreshold, btCollisionWorld.cpp:153-160 [V]).
				const btBroadphaseProxy* Proxy = Body.getBroadphaseHandle();
				btVector3 BoundsMin;
				btVector3 BoundsMax;
				if (Proxy != nullptr)
				{
					BoundsMin = Proxy->m_aabbMin;
					BoundsMax = Proxy->m_aabbMax;
				}
				else
				{
					Body.getCollisionShape()->getAabb(Body.getWorldTransform(), BoundsMin, BoundsMax);
				}
				Particle.BoundsMin = FromBtLength(BoundsMin);
				Particle.BoundsMax = FromBtLength(BoundsMax);
				Particle.LinearVelocity = State.LinearVelocity;
				Particle.AngularVelocity = State.AngularVelocity;
				Particle.BodyIndex = static_cast<uint32>(Index);
				Particle.ObjectState = static_cast<uint8>(State.ObjectState);
				// Bullet's island tag (the union-find root of the last step's islands; -1 for static and kinematic bodies).
				Particle.NativeIslandId = Record.Motion == PT_MOTION_DYNAMIC ? Body.getIslandTag() : -1;
				SnapshotParticles.push_back(Particle);
			}
		}
		if ((Req.Flags & PT_SNAP_CONTACTS) != 0 && bCaptureContacts)
		{
			const double NaN = nan("");
			const bool bTwoFrictionDirections = (World->getSolverInfo().m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS) != 0;
			const int32 BodyCount = Bodies.size();
			for (int ManifoldIndex = 0; ManifoldIndex < Dispatcher->getNumManifolds() && !bTruncated; ++ManifoldIndex)
			{
				const btPersistentManifold& Manifold = *Dispatcher->getManifoldByIndexInternal(ManifoldIndex);
				const int Index0 = Manifold.getBody0()->getUserIndex();
				const int Index1 = Manifold.getBody1()->getUserIndex();
				if (Manifold.getNumContacts() == 0 || Index0 < 0 || Index1 < 0 || Index0 >= BodyCount || Index1 >= BodyCount)
				{
					continue;
				}
				const FBulletBody& Record0 = Bodies[Index0];
				const FBulletBody& Record1 = Bodies[Index1];
				const bool bDynamic0 = Record0.Motion == PT_MOTION_DYNAMIC;
				const bool bDynamic1 = Record1.Motion == PT_MOTION_DYNAMIC;
				if (Record0.bReleased || Record1.bReleased || (!bDynamic0 && !bDynamic1))
				{
					continue;   // no constraint between two non-dynamic bodies (Bullet runs the narrow phase, without response)
				}
				// A sleeping pair: every dynamic body of it asleep (no narrow phase, no solve: the last step's points and impulses).
				const bool bSleeping = (!bDynamic0 || !Record0.Body->isActive()) && (!bDynamic1 || !Record1.Body->isActive());
				for (int PointIndex = 0; PointIndex < Manifold.getNumContacts(); ++PointIndex)
				{
					if (static_cast<uint32>(SnapshotContacts.size()) >= Req.MaxContactPoints)
					{
						bTruncated = true;
						break;
					}
					const btManifoldPoint& Point = Manifold.getContactPoint(PointIndex);
					PtContactPoint Contact = {};
					// Bullet: m_normalWorldOnB lies on B and points toward A, distance < 0 penetrates - Chaos' signs with
					// A = Body0, B = Body1 (PtContactConvention.h).
					Contact.PointOn0 = FromBtLength(Point.getPositionWorldOnA());
					Contact.PointOn1 = FromBtLength(Point.getPositionWorldOnB());
					Contact.Normal = FromBtVector(Point.m_normalWorldOnB);
					Contact.Phi = GScale.ToCm(double(Point.getDistance()));
					// The last solve's impulses (written back with SOLVER_USE_WARMSTARTING, btSequentialImpulseConstraintSolver.cpp:
					// 1765-1783, 1840-1843 [V]): the normal one, and the friction one on A along the solver's friction directions.
					if ((World->getSolverInfo().m_solverMode & SOLVER_USE_WARMSTARTING) != 0)
					{
						Contact.NormalImpulse = GScale.ToCm(double(Point.getAppliedImpulse()));
						const btVector3 Friction = Point.m_lateralFrictionDir1 * Point.m_appliedImpulseLateral1
							+ (bTwoFrictionDirections ? Point.m_lateralFrictionDir2 * Point.m_appliedImpulseLateral2 : btVector3(0, 0, 0));
						Contact.FrictionImpulse = FromBtLength(Friction);
					}
					else
					{
						Contact.NormalImpulse = NaN;
						Contact.FrictionImpulse = PtVec3{ NaN, NaN, NaN };
					}
					Contact.Body0 = static_cast<uint32>(Index0);
					Contact.Body1 = static_cast<uint32>(Index1);
					// The compound child (= element) index of each side; -1 = the object's own (only) shape.
					Contact.Shape0 = static_cast<uint16>(Point.m_index0 >= 0 ? Point.m_index0 : 0);
					Contact.Shape1 = static_cast<uint16>(Point.m_index1 >= 0 ? Point.m_index1 : 0);
					Abi::CanonicalizeContact(Contact, &MobilityRanks[0], false, false);
					// Active is exact: the last solve applied a normal impulse. A point beyond the surface (Phi > 0) within the
					// breaking threshold is speculative (Bullet solves it as such). A sleeping pair's points keep their last
					// flags plus PT_CONTACT_SLEEPING (the M4 rule; Chaos keeps a sleeping constraint's last results).
					const bool bActive = Contact.NormalImpulse > 0.0;
					Contact.Flags |= (bActive ? PT_CONTACT_ACTIVE : 0u) | (Contact.Phi > 0.0 ? PT_CONTACT_SPECULATIVE : 0u) | (bSleeping ? PT_CONTACT_SLEEPING : 0u);
					SnapshotContacts.push_back(Contact);
				}
			}
		}
		Out.NumParticles = static_cast<uint32>(SnapshotParticles.size());
		Out.NumContactPoints = static_cast<uint32>(SnapshotContacts.size());
		Out.NumJoints = 0;
		Out.NumNativeCommands = 0;
		Out.NumStringBytes = 0;
		Out.bTruncated = bTruncated ? 1u : 0u;
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::ReadDebugSnapshot(const PtDebugSnapshotBuffers& Buffers)
	{
		const uint32 NumParticles = static_cast<uint32>(SnapshotParticles.size());
		const uint32 NumContacts = static_cast<uint32>(SnapshotContacts.size());
		if (Buffers.ParticleCapacity < NumParticles || Buffers.ContactCapacity < NumContacts)
		{
			return GetKit().SetLastError(PT_E_CAPACITY, "Bullet: ReadDebugSnapshot needs %u particles / %u contacts (capacity %u / %u)", NumParticles, NumContacts,
				Buffers.ParticleCapacity, Buffers.ContactCapacity);
		}
		if ((NumParticles > 0 && (Buffers.Particles == nullptr || Buffers.ParticleStride < sizeof(PtParticleSnapshot)))
			|| (NumContacts > 0 && (Buffers.ContactPoints == nullptr || Buffers.ContactStride < sizeof(PtContactPoint))))
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Bullet: ReadDebugSnapshot buffers are NULL or their strides are too small");
		}
		for (uint32 Index = 0; Index < NumParticles; ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.Particles) + static_cast<size_t>(Index) * Buffers.ParticleStride, &SnapshotParticles[static_cast<int32>(Index)],
				sizeof(PtParticleSnapshot));
		}
		for (uint32 Index = 0; Index < NumContacts; ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.ContactPoints) + static_cast<size_t>(Index) * Buffers.ContactStride, &SnapshotContacts[static_cast<int32>(Index)],
				sizeof(PtContactPoint));
		}
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::ComputeNativeStateHash(uint64_t& OutHash)
	{
		// FNV-1a over each live body's world transform as Bullet has it (the COM frame for dynamic bodies; float, written as
		// double in cm), origin then rotation, in body index order (02 7.2 #21). Engine-specific: never compared across engines.
		uint64 Hash = 0xcbf29ce484222325ull;
		for (int32 Index = 0; Index < Bodies.size(); ++Index)
		{
			const FBulletBody& Record = Bodies[Index];
			if (Record.bReleased)
			{
				continue;
			}
			const btTransform& Pose = Record.Body->getWorldTransform();
			btQuaternion Rotation;
			Pose.getBasis().getRotation(Rotation);
			const double Values[7] = { GScale.ToCm(double(Pose.getOrigin().getX())), GScale.ToCm(double(Pose.getOrigin().getY())),
				GScale.ToCm(double(Pose.getOrigin().getZ())), double(Rotation.getX()), double(Rotation.getY()), double(Rotation.getZ()), double(Rotation.getW()) };
			Hash = Abi::Fnv1a64(Values, sizeof(Values), Hash);
		}
		OutHash = Hash;
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::SetOption(const Abi::FStringRef Key, const Abi::FStringRef Value)
	{
		// The solver keys can change between frames; the pools, the damping model and the per-body sleep thresholds are
		// creation-time values.
		if (EqualsNoCase("ManifoldPoolSize", Key) || EqualsNoCase("AlgorithmPoolSize", Key) || EqualsNoCase("DampingModel", Key)
			|| EqualsNoCase("LinearSleepingThreshold", Key) || EqualsNoCase("AngularSleepingThreshold", Key))
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Bullet: scene option '%.*s' can only be given at CreateScene", static_cast<int>(Key.Length), Key.Data);
		}
		FBulletSceneOptions Updated = Options;
		char Error[256] = {};
		const PtResult Result = ApplySceneOption(Updated, Key, Value, Error);
		if (Result != PT_OK)
		{
			return GetKit().SetLastError(Result, "%s", Error);
		}
		Options = Updated;
		ApplySolverSettings();
		return PT_OK;
	}

	Abi::FShapeWrappingCounts FPhysScene_Bullet::ReadWrapping() const
	{
		// From the bodies' shapes: a compound with several children is a Compound; a one-child compound wraps one element
		// for its COM frame (OffsetCom) or for its placement (LocalTransform). Bullet capsules are btCapsuleShapeZ: never an
		// axis remap (DL47).
		Abi::FShapeWrappingCounts Counts;
		for (int32 Index = 0; Index < Bodies.size(); ++Index)
		{
			const FBulletBody& Record = Bodies[Index];
			if (Record.bReleased || !Record.Body->getCollisionShape()->isCompound())
			{
				continue;
			}
			const btCompoundShape& Compound = *static_cast<const btCompoundShape*>(Record.Body->getCollisionShape());
			if (Compound.getNumChildShapes() > 1)
			{
				++Counts.Compound;
			}
			else if (!Record.bComIdentity)
			{
				++Counts.OffsetCom;
			}
			else
			{
				++Counts.LocalTransform;
			}
		}
		return Counts;
	}

	PtResult FPhysScene_Bullet::DumpEffectiveSettings(Abi::FTextWriter& Out)
	{
		const double NaN = nan("");
		const btContactSolverInfo& Info = World->getSolverInfo();
		char Text[256];
		char VecText[128];
		const PtVec3 GravityCm = FromBtLength(World->getGravity());
		FormatVec(VecText, GravityCm);
		snprintf(Text, sizeof(Text), "%s cm/s^2 (btDiscreteDynamicsWorld::getGravity)", VecText);
		Out.AppendEffective("world.gravity", Text, "cm/s^2", GravityCm.Z, "cm/s^2", "READBACK");
		snprintf(Text, sizeof(Text), "btSequentialImpulseConstraintSolver (PGS), %d iterations, solver mode 0x%x, split impulse %s", Info.m_numIterations,
			static_cast<unsigned>(Info.m_solverMode), Info.m_splitImpulse != 0 ? "on" : "off");
		Out.AppendEffective("solver.type", Text, "", NaN, "", "READBACK");
		snprintf(Text, sizeof(Text), "%d (btContactSolverInfo::m_numIterations); per-body overrides requested on %u bodies, n/a in Bullet", Info.m_numIterations,
			NumIterationOverrides);
		Out.AppendEffective("solver.iterations", Text, "iterations", static_cast<double>(Info.m_numIterations), "iterations", "READBACK");
		Out.AppendEffective("broadphase.type", "btDbvtBroadphase (dynamic AABB trees, btHashedOverlappingPairCache)", "", NaN, "", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "%.9g m/s (btContactSolverInfo::m_restitutionVelocityThreshold)", double(Info.m_restitutionVelocityThreshold));
		Out.AppendEffective("contact.restitution_threshold", Text, "m/s", GScale.ToCm(double(Info.m_restitutionVelocityThreshold)), "cm/s", "READBACK");
		Out.AppendEffective("contact.offset_rule",
			"no contact offset: manifold points are kept up to the contact breaking threshold (gContactBreakingThreshold x shape angular motion disc, "
			"CD_USE_RELATIVE_CONTACT_BREAKING_THRESHOLD) and the solver treats distance > 0 as speculative",
			"", NaN, "", "COMPILE_TIME");
		// The distance up to which Bullet keeps a manifold point, as the engine holds it (M5 round 0: the module value was
		// reported, which is a factor in the dispatcher's default relative mode). Per manifold
		// btPersistentManifold::getContactBreakingThreshold = min over its two shapes of gContactBreakingThreshold x angular
		// motion disc (btCollisionDispatcher.cpp:74-75, btCollisionShape.cpp:42-45 [V]; about 0.87 cm for a 50 cm cube). From the
		// live manifolds when there are any, else from the dynamic bodies' shapes (each one an upper bound of its pairs).
		{
			const bool bRelative = (Dispatcher->getDispatcherFlags() & btCollisionDispatcher::CD_USE_RELATIVE_CONTACT_BREAKING_THRESHOLD) != 0;
			FRange Breaking;
			for (int Manifold = 0; Manifold < Dispatcher->getNumManifolds(); ++Manifold)
			{
				Breaking.Add(double(Dispatcher->getManifoldByIndexInternal(Manifold)->getContactBreakingThreshold()));
			}
			const bool bFromManifolds = Breaking.bAny;
			for (int32 Index = 0; Index < Bodies.size() && !bFromManifolds; ++Index)
			{
				const FBulletBody& Record = Bodies[Index];
				if (!Record.bReleased && Record.Motion == PT_MOTION_DYNAMIC)
				{
					const btCollisionShape& Shape = *Record.Body->getCollisionShape();
					Breaking.Add(bRelative ? double(Shape.getContactBreakingThreshold(gContactBreakingThreshold)) : double(gContactBreakingThreshold));
				}
			}
			if (Breaking.bAny)
			{
				snprintf(Text, sizeof(Text), "[%.9g, %.9g] m (%s; %s: gContactBreakingThreshold %.9g x angular motion disc)", Breaking.Min, Breaking.Max,
					bFromManifolds ? "btPersistentManifold::getContactBreakingThreshold over the live manifolds"
						: "btCollisionShape::getContactBreakingThreshold of the dynamic bodies, an upper bound of their pairs",
					bRelative ? "relative" : "absolute", double(gContactBreakingThreshold));
			}
			else
			{
				snprintf(Text, sizeof(Text), "no manifold and no dynamic body (gContactBreakingThreshold %.9g, %s)", double(gContactBreakingThreshold),
					bRelative ? "relative" : "absolute");
			}
			Out.AppendEffective("contact.speculative_distance", Text, "m", Breaking.bAny ? GScale.ToCm(Breaking.Max) : NaN, "cm", "READBACK");
		}
		const bool bUECombine = gCalculateCombinedFrictionCallback == &CombineFrictionUE;
		Out.AppendEffective("material.friction_combine",
			bUECombine ? "UE combine mode per pair (larger mode wins), PtCombine dynamic value (gCalculateCombinedFrictionCallback)"
				: "Bullet native: F0 * F1 clamped to [-10, 10] (btManifoldResult::calculateCombinedFriction)",
			"", NaN, "", "READBACK");
		Out.AppendEffective("material.restitution_combine",
			gCalculateCombinedRestitutionCallback == &CombineRestitutionUE ? "UE combine mode per pair (larger mode wins), PtCombine (gCalculateCombinedRestitutionCallback)"
				: "Bullet native: R0 * R1 (btManifoldResult::calculateCombinedRestitution)",
			"", NaN, "", "READBACK");
		Out.AppendEffective("material.static_friction_rule", "dynamic_only", "", NaN, "", "COMPILE_TIME");
		const bool bUEDamping = Options.DampingModel == EDampingModel::UEOrder;
		Out.AppendEffective("body.damping_formula",
			bUEDamping ? "UE: v = (v + a dt) * max(0, 1 - c dt) before the solve (internal pre-tick; btRigidBody damping 0, PtDamping.h)"
				: "Bullet: v *= (1 - c_b)^dt before the force integration, c_b = 1 - (1 - c dt)^(1/dt) (DampingModel=Converted)",
			"", NaN, "", "COMPILE_TIME");

		// Per-body values (dynamic bodies): read from the engine where Bullet holds them.
		FRange Linear;
		FRange Angular;
		FRange MaxAngular;
		FRange MaxLinear;
		FRange LinearSleep;
		FRange AngularSleep;
		FRange BoxMargin;
		uint32 NumDynamic = 0;
		uint32 NumMaySleep = 0;
		uint32 NumGyroscopic = 0;
		uint32 NumCcd = 0;
		for (int32 Index = 0; Index < Bodies.size(); ++Index)
		{
			const FBulletBody& Record = Bodies[Index];
			if (Record.bReleased)
			{
				continue;
			}
			// Box margins of every live body's boxes (compound children included): btConvexInternalShape::getMargin.
			const btCollisionShape* Shape = Record.Body->getCollisionShape();
			if (Shape->isCompound())
			{
				const btCompoundShape& Compound = *static_cast<const btCompoundShape*>(Shape);
				for (int Child = 0; Child < Compound.getNumChildShapes(); ++Child)
				{
					if (Compound.getChildShape(Child)->getShapeType() == BOX_SHAPE_PROXYTYPE)
					{
						BoxMargin.Add(double(Compound.getChildShape(Child)->getMargin()));
					}
				}
			}
			else if (Shape->getShapeType() == BOX_SHAPE_PROXYTYPE)
			{
				BoxMargin.Add(double(Shape->getMargin()));
			}
			if (Record.Motion != PT_MOTION_DYNAMIC)
			{
				continue;
			}
			const btRigidBody& Body = *Record.Body;
			++NumDynamic;
			if (bUEDamping)
			{
				Linear.Add(Record.LinearDamping);
				Angular.Add(Record.AngularDamping);
			}
			else
			{
				Linear.Add(Abi::BulletDampingToUE(double(Body.getLinearDamping()), static_cast<double>(DampingDeltaTime)));
				Angular.Add(Abi::BulletDampingToUE(double(Body.getAngularDamping()), static_cast<double>(DampingDeltaTime)));
			}
			MaxAngular.Add(Record.MaxAngularVelocity);
			MaxLinear.Add(Record.MaxLinearVelocity);
			LinearSleep.Add(GScale.ToCm(double(Body.getLinearSleepingThreshold())));
			AngularSleep.Add(double(Body.getAngularSleepingThreshold()));
			NumMaySleep += Body.getActivationState() != DISABLE_DEACTIVATION ? 1u : 0u;
			NumGyroscopic += (Body.getFlags() & (BT_ENABLE_GYROSCOPIC_FORCE_EXPLICIT | BT_ENABLE_GYROSCOPIC_FORCE_IMPLICIT_WORLD | BT_ENABLE_GYROSCOPIC_FORCE_IMPLICIT_BODY)) != 0 ? 1u : 0u;
			NumCcd += Body.getCcdMotionThreshold() > 0 ? 1u : 0u;
		}
		if (NumDynamic == 0)
		{
			Out.AppendEffective("body.damping", "no dynamic body", "1/s", NaN, "1/s", bUEDamping ? "SET_ONLY" : "READBACK");
		}
		else if (bUEDamping)
		{
			snprintf(Text, sizeof(Text), "UE linear [%.9g, %.9g] angular [%.9g, %.9g] 1/s over %u dynamic bodies, applied by the pre-tick (btRigidBody damping 0)",
				Linear.Min, Linear.Max, Angular.Min, Angular.Max, NumDynamic);
			Out.AppendEffective("body.damping", Text, "1/s", Linear.Max, "1/s", "SET_ONLY");
		}
		else
		{
			snprintf(Text, sizeof(Text), "btRigidBody damping as UE linear [%.9g, %.9g] angular [%.9g, %.9g] 1/s at dt %.9g over %u dynamic bodies", Linear.Min, Linear.Max,
				Angular.Min, Angular.Max, static_cast<double>(DampingDeltaTime), NumDynamic);
			Out.AppendEffective("body.damping", Text, "1/s", Linear.bAny ? Linear.Max : NaN, "1/s", "READBACK");
		}
		if (MaxAngular.bAny)
		{
			snprintf(Text, sizeof(Text), "[%.9g, %.9g] rad/s, clamped by the pre-tick (<= 0: none); above pi/4 per step Bullet under-rotates "
				"(bullet.angular_motion_threshold)", MaxAngular.Min, MaxAngular.Max);
		}
		else
		{
			snprintf(Text, sizeof(Text), "no dynamic body (the pre-tick clamps each body's limit; above pi/4 per step Bullet under-rotates)");
		}
		Out.AppendEffective("body.max_angular_velocity", Text, "rad/s", MaxAngular.bAny && MaxAngular.Max > 0.0 ? MaxAngular.Max : NaN, "rad/s", "SET_ONLY");
		if (MaxLinear.bAny && MaxLinear.Max > 0.0)
		{
			snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm/s, clamped by the pre-tick (<= 0: none)", MaxLinear.Min, MaxLinear.Max);
			Out.AppendEffective("body.max_linear_velocity", Text, "cm/s", MaxLinear.Max, "cm/s", "SET_ONLY");
		}
		else
		{
			Out.AppendEffective("body.max_linear_velocity", "unclamped (Bullet has no linear limit; BT_CLAMP_VELOCITY_TO is 0)", "cm/s", NaN, "cm/s", "COMPILE_TIME");
		}
		snprintf(Text, sizeof(Text), "%u of %u dynamic bodies have a BT_ENABLE_GYROSCOPIC_FORCE_* flag", NumGyroscopic, NumDynamic);
		Out.AppendEffective("body.gyroscopic", Text, "bodies", NumGyroscopic > 0 ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "%s (%u of %u dynamic bodies may deactivate; the others are DISABLE_DEACTIVATION; gDisableDeactivation %s)",
			bSleepingAllowed ? "true" : "false", NumMaySleep, NumDynamic, gDisableDeactivation ? "true" : "false");
		const bool bMaySleep = bSleepingAllowed && !gDisableDeactivation && (NumDynamic == 0 || NumMaySleep > 0);
		Out.AppendEffective("sleep.enabled", Text, "bool", bMaySleep ? 1.0 : 0.0, "bool", "READBACK");
		if (LinearSleep.bAny)
		{
			snprintf(Text, sizeof(Text), "per body: linear < [%.9g, %.9g] cm/s and angular < [%.9g, %.9g] rad/s for gDeactivationTime %.9g s, island-wide "
				"(Bullet's own; UE material thresholds not mapped, M9)", LinearSleep.Min, LinearSleep.Max, AngularSleep.Min, AngularSleep.Max, double(gDeactivationTime));
		}
		else
		{
			snprintf(Text, sizeof(Text), "no dynamic body (variant thresholds %.9g cm/s, %.9g rad/s; gDeactivationTime %.9g s)", Options.LinearSleepingThresholdCm,
				Options.AngularSleepingThreshold, double(gDeactivationTime));
		}
		Out.AppendEffective("sleep.criteria", Text, "cm/s", LinearSleep.bAny ? LinearSleep.Max : NaN, "cm/s", "READBACK");
		snprintf(Text, sizeof(Text), "%u of %u dynamic bodies have a CCD motion threshold (swept sphere); predictive contacts %s", NumCcd, NumDynamic,
			World->getDispatchInfo().m_useContinuous ? "on" : "off");
		Out.AppendEffective("ccd.enabled", Text, "bodies", NumCcd > 0 ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "0 (BT_THREADSAFE=0: btDiscreteDynamicsWorld runs on the calling thread; requested %u)", Desc.NumThreads);
		Out.AppendEffective("threads.workers", Text, "threads", 0.0, "threads", "COMPILE_TIME");
		const bool bDeterministicPairs = World->getDispatchInfo().m_deterministicOverlappingPairs;
		Out.AppendEffective("determinism.flag", bDeterministicPairs ? "true (btDispatcherInfo::m_deterministicOverlappingPairs)" : "false (btDispatcherInfo::m_deterministicOverlappingPairs)",
			"bool", bDeterministicPairs ? 1.0 : 0.0, "bool", "READBACK");
		if (BoxMargin.bAny)
		{
			snprintf(Text, sizeof(Text), "[%.9g, %.9g] m (btBoxShape::getMargin over the live bodies' boxes; setSafeMargin min(0.04 m, 0.1 x smallest half extent))",
				BoxMargin.Min, BoxMargin.Max);
		}
		else
		{
			snprintf(Text, sizeof(Text), "no box shape");
		}
		Out.AppendEffective("shape.box_margin", Text, "m", BoxMargin.bAny ? GScale.ToCm(BoxMargin.Max) : NaN, "cm", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g m (CONVEX_DISTANCE_MARGIN of other convex shapes; spheres and capsules are all margin; boxes: shape.box_margin)",
			double(CONVEX_DISTANCE_MARGIN));
		Out.AppendEffective("shape.convex_radius", Text, "m", GScale.ToCm(double(CONVEX_DISTANCE_MARGIN)), "cm", "COMPILE_TIME");
		const Abi::FShapeWrappingCounts Wrapping = ReadWrapping();
		snprintf(Text, sizeof(Text), "%u (compound %u, local_transform %u, offset_com %u, axis_remap %u)", Wrapping.Total(), Wrapping.Compound,
			Wrapping.LocalTransform, Wrapping.OffsetCom, Wrapping.AxisRemap);
		Out.AppendEffective("shape.wrapping", Text, "shapes", static_cast<double>(Wrapping.Total()), "shapes", "READBACK");
		// Bullet's contact callbacks are process globals (btManifoldResult.h:32, btPersistentManifold.h:39-42 [V]); none is
		// installed (viz / PT_INSTR_CONTACTS snapshots read the dispatcher's manifolds at capture).
		const uint32 NumContactCallbacks = (gContactAddedCallback != nullptr ? 1u : 0u) + (gContactProcessedCallback != nullptr ? 1u : 0u)
			+ (gContactStartedCallback != nullptr ? 1u : 0u) + (gContactEndedCallback != nullptr ? 1u : 0u) + (gContactDestroyedCallback != nullptr ? 1u : 0u);
		snprintf(Text, sizeof(Text), "%u (gContact*Callback set%s)", NumContactCallbacks,
			bCaptureContacts ? "; viz / PT_INSTR_CONTACTS snapshots read the dispatcher's manifolds at capture" : "");
		Out.AppendEffective("listeners.installed", Text, "listeners", static_cast<double>(NumContactCallbacks), "listeners", "READBACK");

		// Bullet-specific (the first two are the backend's required keys).
		snprintf(Text, sizeof(Text), "0x%x (warmstarting %s, SIMD %s, 2 friction directions %s, randomize order %s), split impulse %s (threshold %.9g m, turn erp %.9g), "
			"erp2 %.9g, linear slop %.9g m, warmstarting factor %.9g", static_cast<unsigned>(Info.m_solverMode),
			(Info.m_solverMode & SOLVER_USE_WARMSTARTING) != 0 ? "on" : "off", (Info.m_solverMode & SOLVER_SIMD) != 0 ? "on" : "off",
			(Info.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS) != 0 ? "on" : "off", (Info.m_solverMode & SOLVER_RANDMIZE_ORDER) != 0 ? "on" : "off",
			Info.m_splitImpulse != 0 ? "on" : "off", double(Info.m_splitImpulsePenetrationThreshold), double(Info.m_splitImpulseTurnErp), double(Info.m_erp2),
			double(Info.m_linearSlop), double(Info.m_warmstartingFactor));
		Out.AppendEffective("bullet.solver_mode", Text, "bits", static_cast<double>(Info.m_solverMode), "bits", "READBACK");
		const double AngularLimit = GBulletAngularMotionThreshold / Desc.NominalDeltaTime;
		snprintf(Text, sizeof(Text), "%.9g rad/s at dt %.9g s: above pi/4 per step (ANGULAR_MOTION_THRESHOLD) integrateTransform turns a body by "
			"2 atan((|w| dt / (pi/4)) tan(pi/8)) < |w| dt; the velocity is not clamped (btTransformUtil.h:44-72)", AngularLimit, Desc.NominalDeltaTime);
		Out.AppendEffective("bullet.angular_motion_threshold", Text, "rad/s", AngularLimit, "rad/s", "COMPILE_TIME");
		// Adapter counters are SET_ONLY (docs/Abi.md 5.14; M5 round 0): the post-tick counts the body-steps above the limit.
		snprintf(Text, sizeof(Text), "%llu body-steps (counted by the post-tick)", static_cast<unsigned long long>(AngularMotionLimitHits));
		Out.AppendEffective("bullet.angular_motion_limit_hits", Text, "body-steps", static_cast<double>(AngularMotionLimitHits), "body-steps", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%.9g (btContactSolverInfo::m_erp2, contacts)", double(Info.m_erp2));
		Out.AppendEffective("bullet.erp2", Text, "", double(Info.m_erp2), "", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g m", double(Info.m_splitImpulsePenetrationThreshold));
		Out.AppendEffective("bullet.split_impulse_penetration_threshold", Text, "m", GScale.ToCm(double(Info.m_splitImpulsePenetrationThreshold)), "cm", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g m", double(Info.m_linearSlop));
		Out.AppendEffective("bullet.linear_slop", Text, "m", GScale.ToCm(double(Info.m_linearSlop)), "cm", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g", double(Info.m_warmstartingFactor));
		Out.AppendEffective("bullet.warmstarting_factor", Text, "", double(Info.m_warmstartingFactor), "", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g s (gDeactivationTime)", double(gDeactivationTime));
		Out.AppendEffective("bullet.deactivation_time", Text, "s", double(gDeactivationTime), "s", "READBACK");
		Out.AppendEffective("bullet.damping_model", bUEDamping ? "UE" : "Converted", "", bUEDamping ? 0.0 : 1.0, "", "SET_ONLY");
		Out.AppendEffective("bullet.force_update_all_aabbs", World->getForceUpdateAllAabbs() ? "true" : "false", "bool", World->getForceUpdateAllAabbs() ? 1.0 : 0.0,
			"bool", "READBACK");
		const btPoolAllocator& ManifoldPool = Dispatcher->GetManifoldPool();
		const btPoolAllocator& AlgorithmPool = Dispatcher->GetAlgorithmPool();
		snprintf(Text, sizeof(Text), "manifolds %d x %d B (peak used %d), algorithms %d x %d B (peak used %d)", ManifoldPool.getMaxCount(), ManifoldPool.getElementSize(),
			PeakManifoldsUsed, AlgorithmPool.getMaxCount(), AlgorithmPool.getElementSize(), PeakAlgorithmsUsed);
		Out.AppendEffective("bullet.pools", Text, "manifolds", static_cast<double>(ManifoldPool.getMaxCount()), "manifolds", "READBACK");
		snprintf(Text, sizeof(Text), "manifolds %llu, algorithms %llu (heap allocations after a pool was exhausted)",
			static_cast<unsigned long long>(Dispatcher->NumManifoldHeapAllocations), static_cast<unsigned long long>(Dispatcher->NumAlgorithmHeapAllocations));
		Out.AppendEffective("bullet.pool_heap_fallbacks", Text, "allocations",
			static_cast<double>(Dispatcher->NumManifoldHeapAllocations + Dispatcher->NumAlgorithmHeapAllocations), "allocations", "SET_ONLY");   // the adapter's dispatcher counts
		snprintf(Text, sizeof(Text), "n/a: no per-body initial-overlap rate (set on %u bodies, ignored)", NumDepenetrationOverrides);
		Out.AppendEffective("bullet.max_depenetration_velocity", Text, "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("bullet.inertia_conditioning", "n/a: PT_BODY_INERTIA_CONDITIONING ignored", "", NaN, "", "COMPILE_TIME");
		const bool bDebugDrawer = World->getDebugDrawer() != nullptr;
		Out.AppendEffective("bullet.debug_drawer",
			bDebugDrawer ? "attached (btCollisionWorld::getDebugDrawer)"
				: "none (btCollisionWorld::getDebugDrawer; never attached in bench / behaviour runs: it would override gDisableDeactivation; viz drawer M9)",
			"bool", bDebugDrawer ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "%u", NumStartAsleepSkipped);
		Out.AppendEffective("bullet.start_asleep_skipped", Text, "bodies", static_cast<double>(NumStartAsleepSkipped), "bodies", "SET_ONLY");
		snprintf(Text, sizeof(Text), "0x%x", TotalErrorBits);
		Out.AppendEffective("bullet.engine_error_bits", Text, "bits", static_cast<double>(TotalErrorBits), "bits", "SET_ONLY");
		// Which engine error bits mean "a capacity was exceeded" (the bench's exit code 5, docs/Abi.md 5.14).
		snprintf(Text, sizeof(Text), "0x%x (manifold pool heap fallback 0x1 | collision algorithm pool heap fallback 0x2)", GBulletCapacityErrorBits);
		Out.AppendEffective("errors.capacity_bits", Text, "bits", static_cast<double>(GBulletCapacityErrorBits), "bits", "COMPILE_TIME");
		return PT_OK;
	}

	PtResult FPhysScene_Bullet::GetMemoryStats(PtMemoryStats& Out)
	{
		// The two pools of the collision configuration (allocator kind 2 = an engine pool allocator): their size and the
		// peak bytes in use (sampled after every step). Bullet's other heap use is not counted (no allocator hook), so
		// NumAllocs / NumFrees stay -1; the pools' heap fallbacks are bullet.pool_heap_fallbacks and capacity error bits.
		const btPoolAllocator& Manifolds = Dispatcher->GetManifoldPool();
		const btPoolAllocator& Algorithms = Dispatcher->GetAlgorithmPool();
		Out.LiveBytes = static_cast<int64>(Manifolds.getMaxCount()) * Manifolds.getElementSize() + static_cast<int64>(Algorithms.getMaxCount()) * Algorithms.getElementSize();
		Out.PeakBytes = static_cast<int64>(PeakManifoldsUsed) * Manifolds.getElementSize() + static_cast<int64>(PeakAlgorithmsUsed) * Algorithms.getElementSize();
		Out.AllocatorKind = 2;
		return PT_OK;
	}
}
