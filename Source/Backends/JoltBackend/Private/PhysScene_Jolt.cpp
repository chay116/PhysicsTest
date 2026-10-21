// PhysScene_Jolt.cpp - FPhysScene_Jolt (see PhysScene_Jolt.h). UE behaviour mirrored here is cited inline; Jolt is
// used through its public API only (no local patch, Q8).
#include "PhysScene_Jolt.h"

#include "JoltCombine.h"
#include "PtContactConvention.h"
#include "PtUnitScale.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace PhysicsTest::JoltBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// The one unit conversion of this backend (D1, BackendCommon's unit-scale table): cm -> m.
		constexpr Abi::FUnitScale GScale = Abi::GetEngineUnitScale(Abi::EEngineUnits::Jolt);
		// "Unclamped" velocity limits: Jolt always clamps (MotionProperties::ClampLinearVelocity / ClampAngularVelocity square
		// the limit in float), so "no limit" is a value far beyond any reachable speed whose square stays finite in float.
		constexpr float GUnclampedLinearVelocity = 1.0e7f;    // m/s
		constexpr float GUnclampedAngularVelocity = 1.0e7f;   // rad/s
		// Capacity defaults when the host gives no hint (A gates, small tests).
		constexpr uint32 GDefaultMaxBodies = 1024;
		// Temp allocator sizing (02 9.4): Jolt allocates mMaxConstraints x cMaxConstraintSize (about 1 KiB with 4 points,
		// ContactConstraintManager.h:502, .cpp:762 [V]) per step plus per-body island / active arrays; the block holds that
		// with margin, and the peak is recorded (jolt.temp_allocator_peak_bytes).
		constexpr uint64 GTempBaseBytes = 16ull << 20;
		constexpr uint64 GTempBytesPerContactConstraint = 1024;
		constexpr uint64 GTempBytesPerBody = 1024;
		// Viz contact listener: Jolt reduces a manifold to at most 4 points, one contact constraint per manifold.
		constexpr uint32 GListenerPointsPerConstraint = 4;
		// COM identity on the Jolt side (DL42): the shape COM is computed in float metres, so the harness's 1e-6 cm (1e-8 m)
		// is below float resolution at body scale. A difference within this many float ulps of the body's size is the same
		// COM; DL42's 1e-6 cm stays the floor (PtShapeWrapping.h decides the native-primitive case in double).
		constexpr float GComToleranceUlps = 16.0f;

		JPH::Vec3 ToJoltLength(const PtVec3& Cm)
		{
			return JPH::Vec3(static_cast<float>(GScale.Length(Cm.X)), static_cast<float>(GScale.Length(Cm.Y)), static_cast<float>(GScale.Length(Cm.Z)));
		}
		JPH::Vec3 ToJoltInertialVector(const PtVec3& KgCm2)   // torque, angular impulse (x L^2)
		{
			return JPH::Vec3(static_cast<float>(GScale.Inertia(KgCm2.X)), static_cast<float>(GScale.Inertia(KgCm2.Y)), static_cast<float>(GScale.Inertia(KgCm2.Z)));
		}
		JPH::Vec3 ToJoltVector(const PtVec3& V)   // unitless or rad/s
		{
			return JPH::Vec3(static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
		}
		JPH::Quat ToJoltQuat(const PtQuat& Q)
		{
			// The ABI quaternion is unit within 1e-6 in double; renormalised in float so that Jolt's IsNormalized checks hold.
			return JPH::Quat(static_cast<float>(Q.X), static_cast<float>(Q.Y), static_cast<float>(Q.Z), static_cast<float>(Q.W)).Normalized();
		}
		PtVec3 FromJoltLength(const JPH::Vec3 V)
		{
			return PtVec3{ GScale.ToCm(static_cast<double>(V.GetX())), GScale.ToCm(static_cast<double>(V.GetY())), GScale.ToCm(static_cast<double>(V.GetZ())) };
		}
		PtVec3 FromJoltVector(const JPH::Vec3 V)
		{
			return PtVec3{ static_cast<double>(V.GetX()), static_cast<double>(V.GetY()), static_cast<double>(V.GetZ()) };
		}
		PtQuat FromJoltQuat(const JPH::Quat Q)
		{
			return PtQuat{ static_cast<double>(Q.GetX()), static_cast<double>(Q.GetY()), static_cast<double>(Q.GetZ()), static_cast<double>(Q.GetW()) };
		}

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

		bool ParseUInt(const Abi::FStringRef Text, uint64& Out)
		{
			char Buffer[32];
			if (Text.Length == 0 || Text.Length >= sizeof(Buffer) || Text.Data[0] == '-')
			{
				return false;
			}
			memcpy(Buffer, Text.Data, Text.Length);
			Buffer[Text.Length] = '\0';
			char* End = nullptr;
			const unsigned long long Value = strtoull(Buffer, &End, 10);
			if (End == Buffer || *End != '\0')
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

		// I_world * V for a dynamic body: R diag(I) R^T V, R = body rotation * principal-frame rotation.
		JPH::Vec3 MultiplyWorldInertia(const JPH::Body& Body, const JPH::Vec3 V)
		{
			const JPH::MotionProperties& Motion = *Body.GetMotionProperties();
			const JPH::Quat Rotation = Body.GetRotation() * Motion.GetInertiaRotation();
			const JPH::Vec3 InvDiagonal = Motion.GetInverseInertiaDiagonal();
			const JPH::Vec3 Local = Rotation.Conjugated() * V;
			const JPH::Vec3 Scaled(InvDiagonal.GetX() > 0.0f ? Local.GetX() / InvDiagonal.GetX() : 0.0f, InvDiagonal.GetY() > 0.0f ? Local.GetY() / InvDiagonal.GetY() : 0.0f,
				InvDiagonal.GetZ() > 0.0f ? Local.GetZ() / InvDiagonal.GetZ() : 0.0f);
			return Rotation * Scaled;
		}

		// Into += Value (the deferred accumulators of FJoltBody are Float3: no SIMD alignment inside JPH::Array<FJoltBody>).
		void AddPending(JPH::Float3& Into, const JPH::Vec3 Value)
		{
			(JPH::Vec3(Into) + Value).StoreFloat3(&Into);
		}

		// The convex radii of every BoxShape inside Shape (decorators and compounds are walked), in metres.
		void CollectBoxConvexRadii(const JPH::Shape& Shape, float& InOutMin, float& InOutMax, bool& bInOutAny)
		{
			switch (Shape.GetType())
			{
			case JPH::EShapeType::Convex:
				if (Shape.GetSubType() == JPH::EShapeSubType::Box)
				{
					const float Radius = static_cast<const JPH::BoxShape&>(Shape).GetConvexRadius();
					InOutMin = !bInOutAny || Radius < InOutMin ? Radius : InOutMin;
					InOutMax = !bInOutAny || Radius > InOutMax ? Radius : InOutMax;
					bInOutAny = true;
				}
				break;
			case JPH::EShapeType::Decorated:
				CollectBoxConvexRadii(*static_cast<const JPH::DecoratedShape&>(Shape).GetInnerShape(), InOutMin, InOutMax, bInOutAny);
				break;
			case JPH::EShapeType::Compound:
				for (const JPH::CompoundShape::SubShape& Sub : static_cast<const JPH::CompoundShape&>(Shape).GetSubShapes())
				{
					CollectBoxConvexRadii(*Sub.mShape, InOutMin, InOutMax, bInOutAny);
				}
				break;
			default:
				break;
			}
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

		float MaxAbsComponent(const JPH::Vec3 V)
		{
			return V.Abs().ReduceMax();
		}
	}

	PtResult ApplySceneOption(FJoltSceneOptions& Options, const Abi::FStringRef Key, const Abi::FStringRef Value, char (&Error)[256])
	{
		uint64 Unsigned = 0;
		double Real = 0.0;
		bool bFlag = false;
		if (EqualsNoCase("NumVelocitySteps", Key) || EqualsNoCase("NumPositionSteps", Key))
		{
			if (!ParseUInt(Value, Unsigned) || Unsigned < 1 || Unsigned > 255)
			{
				snprintf(Error, sizeof(Error), "Jolt: %.*s must be an integer in [1, 255]", static_cast<int>(Key.Length), Key.Data);
				return PT_E_INVALID_ARG;
			}
			(EqualsNoCase("NumVelocitySteps", Key) ? Options.NumVelocitySteps : Options.NumPositionSteps) = static_cast<uint32>(Unsigned);
			return PT_OK;
		}
		if (EqualsNoCase("ConvexRadius", Key) || EqualsNoCase("PenetrationSlop", Key) || EqualsNoCase("SpeculativeContactDistance", Key)
			|| EqualsNoCase("Baumgarte", Key))
		{
			if (!ParseDouble(Value, Real) || Real < 0.0)
			{
				snprintf(Error, sizeof(Error), "Jolt: %.*s must be a finite number >= 0", static_cast<int>(Key.Length), Key.Data);
				return PT_E_INVALID_ARG;
			}
			if (EqualsNoCase("ConvexRadius", Key))
			{
				Options.ConvexRadiusCm = Real;
			}
			else if (EqualsNoCase("PenetrationSlop", Key))
			{
				Options.PenetrationSlopCm = Real;
			}
			else if (EqualsNoCase("SpeculativeContactDistance", Key))
			{
				Options.SpeculativeContactDistanceCm = Real;
			}
			else
			{
				Options.Baumgarte = Real;
			}
			return PT_OK;
		}
		if (EqualsNoCase("UseLargeIslandSplitter", Key) || EqualsNoCase("DeterministicSimulation", Key))
		{
			if (!ParseBool(Value, bFlag))
			{
				snprintf(Error, sizeof(Error), "Jolt: %.*s must be true / false / 1 / 0", static_cast<int>(Key.Length), Key.Data);
				return PT_E_INVALID_ARG;
			}
			(EqualsNoCase("UseLargeIslandSplitter", Key) ? Options.bUseLargeIslandSplitter : Options.bDeterministicSimulation) = bFlag;
			return PT_OK;
		}
		if (EqualsNoCase("TempAllocatorBytes", Key))
		{
			if (!ParseUInt(Value, Unsigned) || Unsigned < (1ull << 20) || Unsigned > (4ull << 30))
			{
				snprintf(Error, sizeof(Error), "Jolt: TempAllocatorBytes must be an integer in [1 MiB, 4 GiB]");
				return PT_E_INVALID_ARG;
			}
			Options.TempAllocatorBytes = Unsigned;
			return PT_OK;
		}
		snprintf(Error, sizeof(Error), "Jolt: unknown scene option '%.*s' (NumVelocitySteps, NumPositionSteps, ConvexRadius, PenetrationSlop, "
			"SpeculativeContactDistance, Baumgarte, UseLargeIslandSplitter, DeterministicSimulation, TempAllocatorBytes)",
			static_cast<int>(Key.Length), Key.Data);
		return PT_E_INVALID_ARG;
	}

	// ---- construction ------------------------------------------------------------------------------------------------
	FPhysScene_Jolt::FPhysScene_Jolt(const PtSceneDesc& InDesc, const FJoltSceneOptions& InOptions)
		: Desc(InDesc)
		, Options(InOptions)
	{
		Desc.DebugName = nullptr;
		Desc.Options = nullptr;
		Desc.NumOptions = 0;
		AssertsSeen = GetNumJoltAsserts();   // asserts from here on (creation included) are this scene's (CheckJoltAsserts)
		Gravity = InDesc.Gravity;
		bSleepingAllowed = (InDesc.SimFlags & PT_SIM_ALLOW_SLEEPING) != 0;
		// Fixed capacities from the scenario's declared counts (02 9.4); overflow is the run's G12 failure.
		MaxBodies = InDesc.MaxBodiesHint > 0 ? InDesc.MaxBodiesHint : GDefaultMaxBodies;
		MaxBodyPairs = InDesc.MaxBodyPairsHint > 0 ? InDesc.MaxBodyPairsHint : 8u * MaxBodies + 1024u;
		MaxContactConstraints = InDesc.MaxContactConstraintsHint > 0 ? InDesc.MaxContactConstraintsHint : 4u * MaxBodies + 1024u;
		GroupFilter = new FJoltUEGroupFilter();
		System = std::make_unique<JPH::PhysicsSystem>();
		System->Init(MaxBodies, 0, MaxBodyPairs, MaxContactConstraints, BroadPhaseLayers, ObjectVsBroadPhaseFilter, ObjectLayerPairFilter);
		System->SetGravity(ToJoltLength(Gravity));   // explicit, never Jolt's default (D1)
		System->SetCombineFriction(&CombineFrictionUE);
		System->SetCombineRestitution(&CombineRestitutionUE);
		ApplyPhysicsSettings();
		// T includes the calling thread: T - 1 workers (02 9.6). T = 1 has none; the barrier wait runs the jobs inline.
		NumWorkerThreads = InDesc.NumThreads > 1 ? InDesc.NumThreads - 1 : 0;
		JobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(NumWorkerThreads));
		// No listener in bench / behaviour runs (02 9.4: "bench 모드에서 금지: ContactListener").
		if (InDesc.RunMode == PT_RUN_VIZ || (InDesc.InstrumentFlags & PT_INSTR_CONTACTS) != 0)
		{
			// Sized once from the contact capacity: the job threads never allocate (JoltContactListener.h).
			ContactListener = std::make_unique<FJoltContactListener>(GListenerPointsPerConstraint * MaxContactConstraints);
			System->SetContactListener(ContactListener.get());
			SleepingContacts.reserve(GListenerPointsPerConstraint * MaxContactConstraints);
			SleepingScratch.reserve(GListenerPointsPerConstraint * MaxContactConstraints);
			KeptContactBodies.reserve(MaxBodies);
		}
		Bodies.reserve(MaxBodies);
		MobilityRanks.reserve(MaxBodies);
		// Each body is listed at most once (bPendingImpulse / bPendingForce): T_push never allocates.
		PendingImpulseBodies.reserve(MaxBodies);
		PendingForceBodies.reserve(MaxBodies);
	}

	FPhysScene_Jolt::~FPhysScene_Jolt()
	{
		// Remove and destroy every live body before the system goes (Jolt's samples do the same).
		JPH::BodyInterface& BodyInterface = System->GetBodyInterfaceNoLock();
		JPH::BodyIDVector Live;
		Live.reserve(Bodies.size());
		for (const FJoltBody& Body : Bodies)
		{
			if (!Body.bReleased && !Body.Id.IsInvalid())
			{
				Live.push_back(Body.Id);
			}
		}
		if (!Live.empty())
		{
			BodyInterface.RemoveBodies(Live.data(), static_cast<int>(Live.size()));
			BodyInterface.DestroyBodies(Live.data(), static_cast<int>(Live.size()));
		}
		System->SetContactListener(nullptr);
		System.reset();
		JobSystem.reset();
		TempAllocator.reset();
	}

	void FPhysScene_Jolt::ApplyPhysicsSettings()
	{
		JPH::PhysicsSettings Settings = System->GetPhysicsSettings();
		Settings.mNumVelocitySteps = Options.NumVelocitySteps;
		Settings.mNumPositionSteps = Options.NumPositionSteps;
		Settings.mPenetrationSlop = static_cast<float>(GScale.Length(Options.PenetrationSlopCm));
		Settings.mSpeculativeContactDistance = static_cast<float>(GScale.Length(Options.SpeculativeContactDistanceCm));
		Settings.mBaumgarte = static_cast<float>(Options.Baumgarte);
		Settings.mUseLargeIslandSplitter = Options.bUseLargeIslandSplitter;
		Settings.mDeterministicSimulation = Options.bDeterministicSimulation;
		// Restitution threshold (DL35, Q11): the given cm/s, else the UEParity mapping for Jolt, Chaos' 1000 * dt cm/s.
		const double ThresholdCm = Desc.RestitutionThreshold > 0.0 ? Desc.RestitutionThreshold : 1000.0 * Desc.NominalDeltaTime;
		Settings.mMinVelocityForRestitution = static_cast<float>(GScale.Length(ThresholdCm));
		// Sleep is switched per body (mAllowSleeping, DL48); the global switch stays on.
		Settings.mAllowSleeping = true;
		System->SetPhysicsSettings(Settings);
	}

	void FPhysScene_Jolt::EnsureTempAllocator()
	{
		if (TempAllocator)
		{
			return;
		}
		const uint64 Bytes = Options.TempAllocatorBytes > 0 ? Options.TempAllocatorBytes
			: GTempBaseBytes + static_cast<uint64>(MaxContactConstraints) * GTempBytesPerContactConstraint + static_cast<uint64>(MaxBodies) * GTempBytesPerBody;
		TempAllocator = std::make_unique<FJoltTempAllocator>(static_cast<size_t>(Bytes));
	}

	JPH::Body* FPhysScene_Jolt::GetBody(const FJoltBody& Body) const
	{
		return System->GetBodyLockInterfaceNoLock().TryGetBody(Body.Id);
	}

	void FPhysScene_Jolt::MarkDirty(const uint32 BodyIndex)
	{
		FJoltBody& Body = Bodies[BodyIndex];
		if (!Body.bDirty)
		{
			Body.bDirty = true;
			DirtyBodies.push_back(BodyIndex);
		}
	}

	// ---- creation ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_Jolt::CreateMaterials(const Abi::TStridedView<PtPhysicalMaterial> InMaterials)
	{
		for (uint32 Index = 0; Index < InMaterials.Num(); ++Index)
		{
			Materials.push_back(InMaterials[Index]);
		}
		return PT_OK;
	}

	JPH::RefConst<JPH::Shape> FPhysScene_Jolt::MakeElementShape(const PtShapeElem& Element)
	{
		switch (Element.Type)
		{
		case PT_SHAPE_SPHERE:
			return new JPH::SphereShape(static_cast<float>(GScale.Length(Element.Radius)));
		case PT_SHAPE_BOX:
		{
			// The convex radius may not exceed the smallest half extent (BoxShape asserts it).
			const PtVec3& Half = Element.BoxHalfExtent;
			double MinHalf = Half.X < Half.Y ? Half.X : Half.Y;
			MinHalf = Half.Z < MinHalf ? Half.Z : MinHalf;
			const double RadiusCm = Options.ConvexRadiusCm < MinHalf ? Options.ConvexRadiusCm : MinHalf;
			return new JPH::BoxShape(ToJoltLength(Half), static_cast<float>(GScale.Length(RadiusCm)));   // shape.box_margin reads it back
		}
		case PT_SHAPE_CAPSULE:
			// Jolt CapsuleShape(half height of the cylinder, radius) lies along Y; the caller remaps it to UE's Z.
			return new JPH::CapsuleShape(static_cast<float>(GScale.Length(Element.HalfLength)), static_cast<float>(GScale.Length(Element.Radius)));
		default:
			return nullptr;   // convex: PT_CAP_SHAPE_CONVEX is not declared, the kit rejects it
		}
	}

	JPH::RefConst<JPH::Shape> FPhysScene_Jolt::MakeBodyShape(const PtActorCreateInfo& Actor, const Abi::TStridedView<PtShapeElem>& Shapes)
	{
		const PtShapeElem& First = Shapes[Actor.FirstShape];
		const JPH::Quat CapsuleRemap = ToJoltQuat(Abi::CapsuleAxisRemap(Abi::ECapsuleAxis::Y));
		if (Abi::CanUseNativePrimitive(Actor, &First))
		{
			if (First.Type != PT_SHAPE_CAPSULE)
			{
				return MakeElementShape(First);   // the common case (M3.BoxPile): no wrapper
			}
			++Wrapping.AxisRemap;
			return new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(), CapsuleRemap, MakeElementShape(First));
		}

		// One element placed away from the actor frame, or several elements.
		JPH::RefConst<JPH::Shape> Shape;
		if (Actor.NumShapes == 1)
		{
			const bool bCapsule = First.Type == PT_SHAPE_CAPSULE;
			const bool bPlaced = !Abi::IsNearlyIdentityTransform(First.LocalTransform);
			Shape = MakeElementShape(First);
			if (bPlaced || bCapsule)
			{
				const JPH::Quat Rotation = bCapsule ? ToJoltQuat(First.LocalTransform.Rotation) * CapsuleRemap : ToJoltQuat(First.LocalTransform.Rotation);
				Shape = new JPH::RotatedTranslatedShape(ToJoltLength(First.LocalTransform.Translation), Rotation, Shape);
				Wrapping.AxisRemap += bCapsule ? 1u : 0u;
				Wrapping.LocalTransform += bPlaced ? 1u : 0u;
			}
		}
		else
		{
			JPH::StaticCompoundShapeSettings Compound;
			for (uint32 Offset = 0; Offset < Actor.NumShapes; ++Offset)
			{
				const PtShapeElem& Element = Shapes[Actor.FirstShape + Offset];
				const bool bCapsule = Element.Type == PT_SHAPE_CAPSULE;
				const JPH::Quat Rotation = bCapsule ? ToJoltQuat(Element.LocalTransform.Rotation) * CapsuleRemap : ToJoltQuat(Element.LocalTransform.Rotation);
				Compound.AddShape(ToJoltLength(Element.LocalTransform.Translation), Rotation, MakeElementShape(Element));
				Wrapping.AxisRemap += bCapsule ? 1u : 0u;
			}
			const JPH::ShapeSettings::ShapeResult Result = Compound.Create();
			if (Result.HasError())
			{
				return nullptr;
			}
			Shape = Result.Get();
			++Wrapping.Compound;
		}
		// The body COM must be the harness COM (D5): Jolt takes it from the shape's geometric COM, so a difference
		// becomes an OffsetCenterOfMassShape. The mass and inertia themselves come from MassPropertiesOverride.
		if (Actor.Motion != PT_MOTION_STATIC)
		{
			const JPH::Vec3 Target = ToJoltLength(Actor.CenterOfMassLocal.Translation);
			const JPH::Vec3 ShapeCom = Shape->GetCenterOfMass();
			const JPH::Vec3 Offset = Target - ShapeCom;
			// Float-aware tolerance: a few ulps of the body's size (its COM-centred bounds and both COMs), never below DL42's
			// 1e-6 cm. A compound whose float COM equals the harness COM gets no decorator (Progress.md M3 §6).
			const JPH::AABox Bounds = Shape->GetLocalBounds();
			float Size = MaxAbsComponent(Bounds.mMin);
			Size = MaxAbsComponent(Bounds.mMax) > Size ? MaxAbsComponent(Bounds.mMax) : Size;
			Size = MaxAbsComponent(ShapeCom) > Size ? MaxAbsComponent(ShapeCom) : Size;
			Size = MaxAbsComponent(Target) > Size ? MaxAbsComponent(Target) : Size;
			const float Floor = static_cast<float>(GScale.Length(Abi::GComIdentityMaxOffsetCm));
			const float Tolerance = GComToleranceUlps * FLT_EPSILON * Size > Floor ? GComToleranceUlps * FLT_EPSILON * Size : Floor;
			if (Offset.LengthSq() > Tolerance * Tolerance)
			{
				Shape = new JPH::OffsetCenterOfMassShape(Shape, Offset);
				++Wrapping.OffsetCom;
			}
		}
		return Shape;
	}

	PtResult FPhysScene_Jolt::CreateActors(const Abi::FActorBatchView& Batch)
	{
		const uint32 First = static_cast<uint32>(Bodies.size());
		const uint32 Num = Batch.Actors.Num();
		if (static_cast<uint64>(First) + Num > MaxBodies)
		{
			return GetKit().SetLastError(PT_E_CAPACITY, "Jolt: %u more bodies exceed the scene's capacity of %u (PtSceneDesc::MaxBodiesHint; 02 9.4)", Num,
				MaxBodies);
		}
		JPH::BodyInterface& BodyInterface = System->GetBodyInterfaceNoLock();
		JPH::BodyIDVector Created;
		Created.reserve(Num);
		// The statistics MakeBodyShape / the overrides count while the batch is built, restored when it is rejected.
		const Abi::FShapeWrappingCounts WrappingBefore = Wrapping;
		const uint32 IterationOverridesBefore = NumIterationOverrides;
		const auto Abort = [&](const PtResult Code, const char* What, const uint32 Index) -> PtResult
		{
			// All or nothing: nothing was added to the system yet, so the created bodies are destroyed again and the
			// counters forget them (shape.wrapping and solver.iterations count only bodies that were created).
			if (!Created.empty())
			{
				BodyInterface.DestroyBodies(Created.data(), static_cast<int>(Created.size()));
			}
			Wrapping = WrappingBefore;
			NumIterationOverrides = IterationOverridesBefore;
			return GetKit().SetLastError(Code, "Jolt: actor %u: %s", Index, What);
		};

		// 1 + 2: per body CreateActor with the creation-time flags, then shape, filter and material (UE order, 02 6).
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			// Jolt keeps one friction / restitution per body: the first element's material. A body whose elements have
			// different materials gets that one for all of them (docs/Abi.md 7.1; per-sub-shape materials through the
			// combine callbacks' SubShapeIDs are M9 work).
			const PtPhysicalMaterial& Material = Materials[Batch.Shapes[Info.FirstShape].MaterialIndex];
			JPH::RefConst<JPH::Shape> Shape = MakeBodyShape(Info, Batch.Shapes);
			if (Shape == nullptr)
			{
				return Abort(PT_E_ENGINE, "its shape could not be built", Index);
			}
			JPH::BodyCreationSettings Settings;
			Settings.SetShape(Shape);
			Settings.mPosition = JPH::RVec3(ToJoltLength(Info.ActorTransform.Translation));
			Settings.mRotation = ToJoltQuat(Info.ActorTransform.Rotation);
			Settings.mMotionType = Info.Motion == PT_MOTION_STATIC ? JPH::EMotionType::Static
				: (Info.Motion == PT_MOTION_KINEMATIC ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic);
			Settings.mObjectLayer = Info.Motion == PT_MOTION_STATIC ? Layers::NonMoving : Layers::Moving;
			Settings.mCollisionGroup = JPH::CollisionGroup(GroupFilter, Info.BlockMask, Info.ObjectType);
			Settings.mAllowSleeping = bSleepingAllowed && (Info.Flags & PT_BODY_ALLOW_SLEEP) != 0;
			Settings.mMotionQuality = (Info.Flags & PT_BODY_USE_CCD) != 0 ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
			Settings.mApplyGyroscopicForce = (Info.Flags & PT_BODY_GYROSCOPIC) != 0;
			Settings.mGravityFactor = (Info.Flags & PT_BODY_ENABLE_GRAVITY) != 0 ? 1.0f : 0.0f;
			// UE sets no linear limit (CreateActor uses sqrt(FLT_MAX)); Jolt's 500 m/s default is lifted (02 8.3).
			const double MaxLinearCm = Info.MaxLinearVelocity > 0.0 ? Info.MaxLinearVelocity : Desc.MaxLinearVelocity;
			Settings.mMaxLinearVelocity = MaxLinearCm > 0.0 ? static_cast<float>(GScale.Length(MaxLinearCm)) : GUnclampedLinearVelocity;
			// Damping and the angular limit are InitDynamicProperties values (step 4); Jolt's 0.05 defaults never stay.
			Settings.mLinearDamping = 0.0f;
			Settings.mAngularDamping = 0.0f;
			Settings.mMaxAngularVelocity = GUnclampedAngularVelocity;
			if ((Info.Flags & PT_BODY_OVERRIDE_ITERATIONS) != 0)
			{
				// Literal mapping of UE's per-body counts (velocity -> velocity steps, position -> position steps); M9
				// decides the profile mapping (02 10).
				Settings.mNumVelocityStepsOverride = Info.VelocityIterations;
				Settings.mNumPositionStepsOverride = Info.PositionIterations;
				++NumIterationOverrides;
			}
			Settings.mFriction = static_cast<float>(Material.Friction);
			Settings.mRestitution = static_cast<float>(Material.Restitution);
			Settings.mUserData = MakeBodyUserData(Info.BodyIndex, Material.FrictionCombineMode, Material.RestitutionCombineMode);
			if (Info.Motion == PT_MOTION_DYNAMIC)
			{
				// Harness mass properties (D5): I_body = R diag(I) R^T about the COM, R = the principal frame, kg*m^2.
				const JPH::Mat44 Principal = JPH::Mat44::sRotation(ToJoltQuat(Info.CenterOfMassLocal.Rotation));
				const JPH::Mat44 Diagonal = JPH::Mat44::sScale(ToJoltInertialVector(Info.InertiaDiagonal));
				Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
				Settings.mMassPropertiesOverride.mMass = static_cast<float>(Info.Mass);
				Settings.mMassPropertiesOverride.mInertia = Principal * Diagonal * Principal.Transposed3x3();
			}
			JPH::Body* Body = BodyInterface.CreateBody(Settings);
			if (Body == nullptr)
			{
				return Abort(PT_E_CAPACITY, "Jolt ran out of bodies (PhysicsSystem::Init capacity)", Index);
			}
			Created.push_back(Body->GetID());
		}

		// 3: one AddActorsToScene for the whole batch (AddBodiesPrepare may reorder the array it gets: a copy).
		if (Num > 0)
		{
			JPH::BodyIDVector AddList = Created;
			const JPH::BodyInterface::AddState AddState = BodyInterface.AddBodiesPrepare(AddList.data(), static_cast<int>(AddList.size()));
			BodyInterface.AddBodiesFinalize(AddList.data(), static_cast<int>(AddList.size()), AddState, JPH::EActivation::Activate);
		}

		// 4 + 5: per body InitDynamicProperties in UE order, then put-to-sleep for !bStartAwake.
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			const PtActorCreateInfo& Info = Batch.Actors[Index];
			FJoltBody Record;
			Record.Id = Created[Index];
			Record.Motion = Info.Motion;
			Record.Flags = Info.Flags;
			Record.MaterialIndex = Batch.Shapes[Info.FirstShape].MaterialIndex;
			Record.Mass = Info.Mass;
			Record.InertiaDiagonal = Info.InertiaDiagonal;
			Record.CenterOfMassLocal = Info.CenterOfMassLocal;
			Record.KinematicTarget = Info.ActorTransform;
			JPH::Body& Body = *System->GetBodyLockInterfaceNoLock().TryGetBody(Record.Id);
			if (Info.Motion == PT_MOTION_DYNAMIC)
			{
				JPH::MotionProperties& Motion = *Body.GetMotionProperties();
				Motion.SetLinearDamping(static_cast<float>(Info.LinearDamping));
				Motion.SetAngularDamping(static_cast<float>(Info.AngularDamping));
				Motion.SetMaxAngularVelocity(Info.MaxAngularVelocity > 0.0 ? static_cast<float>(Info.MaxAngularVelocity) : GUnclampedAngularVelocity);
				// (max depenetration velocity: Jolt has no per-body initial-overlap rate; see the effective settings)
				Body.SetLinearVelocityClamped(ToJoltLength(Info.InitialLinearVelocity));
				Body.SetAngularVelocityClamped(ToJoltVector(Info.InitialAngularVelocity));
				// (sleep / stabilisation threshold multipliers: Jolt's sleep test is global, PhysicsSettings)
				if ((Info.Flags & PT_BODY_START_AWAKE) == 0)
				{
					if (bSleepingAllowed)
					{
						BodyInterface.DeactivateBody(Record.Id);   // the body is in the system already (BodyInstance.cpp:4723-4733 [V])
					}
					else
					{
						++NumStartAsleepSkipped;   // 02 6 step 5 is skipped when sleeping is off (Normalized)
					}
				}
			}
			Bodies.push_back(Record);
			MobilityRanks.push_back(Info.Motion == PT_MOTION_STATIC ? Abi::GMobilityStatic
				: (Info.Motion == PT_MOTION_KINEMATIC ? Abi::GMobilityKinematic : Abi::GMobilityDynamic));
			if (Info.Motion == PT_MOTION_KINEMATIC)
			{
				Kinematics.push_back(First + Index);
			}
			DirtyBodies.push_back(First + Index);   // reported by the next ACTIVE_ONLY read
		}
		ReadScratch.reserve(Bodies.size());
		ActiveScratch.reserve(Bodies.size());
		PrevActive.reserve(Bodies.size());
		DirtyBodies.reserve(Bodies.size());
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::ReleaseActors(const uint32_t* BodyIndices, const uint32_t Num)
	{
		// The kit has rejected out-of-range, released and repeated indices (Jolt asserts on a repeated BodyID).
		JPH::BodyIDVector Ids;
		Ids.reserve(Num);
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			Ids.push_back(Bodies[BodyIndices[Index]].Id);
		}
		JPH::BodyInterface& BodyInterface = System->GetBodyInterfaceNoLock();
		BodyInterface.RemoveBodies(Ids.data(), static_cast<int>(Ids.size()));
		BodyInterface.DestroyBodies(Ids.data(), static_cast<int>(Ids.size()));
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			FJoltBody& Body = Bodies[BodyIndices[Index]];
			Body.bReleased = true;
			Body.Id = JPH::BodyID();
		}
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::PrepareForSimulation(Abi::FTextWriter& OutActions)
	{
		// The same untimed opportunity for every engine (02 9.5): Jolt's broad phase tree is rebuilt once here instead of
		// lazily during the first frames.
		System->OptimizeBroadPhase();
		EnsureTempAllocator();
		// The temp block is allocated but untouched: its first use would take one page fault per 4 KiB inside the timed
		// T_step of the early (impact) frames as Jolt's usage grows. Touched here instead, outside the timing.
		const size_t Pages = TempAllocator->PreFault();
		OutActions.Appendf("jolt: OptimizeBroadPhase (%u bodies); temp allocator %llu bytes, pre-faulted (%llu pages of 4 KiB)", System->GetNumBodies(),
			static_cast<unsigned long long>(TempAllocator->GetSize()), static_cast<unsigned long long>(Pages));
		return PT_OK;
	}

	// ---- frame -------------------------------------------------------------------------------------------------------
	PtResult FPhysScene_Jolt::SetUpForFrame(const Abi::FFrameParams& Params)
	{
		// SDK backends set the gravity only when it changes (DL32).
		if (Params.bNewGravity && (Params.Gravity.X != Gravity.X || Params.Gravity.Y != Gravity.Y || Params.Gravity.Z != Gravity.Z))
		{
			Gravity = Params.Gravity;
			System->SetGravity(ToJoltLength(Gravity));
		}
		StepDeltaTime = Params.DeltaSeconds;   // one step per call (no NATIVE_FRAME_MODEL: MaxSubsteps 1, no substepping)
		// Asserts since the last frame belong to this frame: the ones a query between frames already counted (PendingErrorBits,
		// see CheckJoltAssertsBetweenFrames) and the ones nobody has looked at yet (creation, commands, reads).
		FrameErrorBits = PendingErrorBits;
		PendingErrorBits = 0;
		bStepped = false;
		CheckJoltAsserts();
		return PT_OK;
	}

	void FPhysScene_Jolt::CheckJoltAsserts()
	{
		const uint32 Now = GetNumJoltAsserts();
		if (Now != AssertsSeen)
		{
			FrameErrorBits |= GJoltErrorAssert;   // dbg builds (logged by JoltAssertFailed)
			TotalErrorBits |= GJoltErrorAssert;
			AssertsSeen = Now;
		}
	}

	void FPhysScene_Jolt::CheckJoltAssertsBetweenFrames()
	{
		// A query between frames (Idle) must not touch FrameErrorBits - that is the last frame's result, and the next
		// SetUpForFrame starts a new one. The assert is sticky for the run (TotalErrorBits) and becomes part of the next
		// frame's EngineErrorBits (docs/Abi.md 9).
		const uint32 Now = GetNumJoltAsserts();
		if (Now != AssertsSeen)
		{
			PendingErrorBits |= GJoltErrorAssert;
			TotalErrorBits |= GJoltErrorAssert;
			AssertsSeen = Now;
		}
	}

	bool FPhysScene_Jolt::IsSleepingPair(const FJoltRawContact& Contact) const
	{
		if (Contact.Body1 >= Bodies.size() || Contact.Body2 >= Bodies.size())
		{
			return false;
		}
		const auto IsAsleepOrNotDynamic = [this](const uint32 Index)
		{
			const FJoltBody& Record = Bodies[Index];
			return !Record.bReleased && (Record.Motion != PT_MOTION_DYNAMIC || !GetBody(Record)->IsActive());
		};
		const bool bAnyDynamic = Bodies[Contact.Body1].Motion == PT_MOTION_DYNAMIC || Bodies[Contact.Body2].Motion == PT_MOTION_DYNAMIC;
		return bAnyDynamic && IsAsleepOrNotDynamic(Contact.Body1) && IsAsleepOrNotDynamic(Contact.Body2);
	}

	void FPhysScene_Jolt::NoteKeptContactCommand(const uint32 BodyIndex, const bool bPutToSleep, const bool bPoseSet)
	{
		if (!ContactListener)
		{
			return;   // bench runs keep no contacts
		}
		FJoltBody& Record = Bodies[BodyIndex];
		Record.bPutToSleepByCommand = Record.bPutToSleepByCommand || bPutToSleep;
		Record.bPoseSetByCommand = Record.bPoseSetByCommand || bPoseSet;
		if (!Record.bKeptContactsListed)
		{
			Record.bKeptContactsListed = true;
			KeptContactBodies.push_back(BodyIndex);   // reserved to MaxBodies: each body is listed once
		}
	}

	void FPhysScene_Jolt::FlushKeptContactCommands()
	{
		if (KeptContactBodies.empty())
		{
			return;
		}
		// 1. Bodies put to sleep since the last step: Jolt will not report them, so their sleeping pairs come from the last
		// step's listener contents (still intact: the listener is cleared after this). A pair of such a body is not kept
		// yet - the body was awake in that step - so nothing is added twice.
		bool bAnyPutToSleep = false;
		for (const uint32 Index : KeptContactBodies)
		{
			bAnyPutToSleep = bAnyPutToSleep || Bodies[Index].bPutToSleepByCommand;
		}
		if (bAnyPutToSleep)
		{
			const FJoltRawContact* Stepped = ContactListener->GetContacts();
			const uint32 NumStepped = ContactListener->GetNumContacts();
			for (uint32 Index = 0; Index < NumStepped; ++Index)
			{
				const FJoltRawContact& Contact = Stepped[Index];
				const bool bTouchesPutToSleep = (Contact.Body1 < Bodies.size() && Bodies[Contact.Body1].bPutToSleepByCommand)
					|| (Contact.Body2 < Bodies.size() && Bodies[Contact.Body2].bPutToSleepByCommand);
				if (!bTouchesPutToSleep || !IsSleepingPair(Contact))
				{
					continue;
				}
				if (SleepingContacts.size() >= SleepingContacts.capacity())
				{
					bSleepingContactsDropped = true;   // never grow: the arrays are sized once
					break;
				}
				SleepingContacts.push_back(Contact);
			}
		}
		// 2. One pass over the kept pairs: drop those of a body whose pose a command set (stale points) and those that are no
		// longer sleeping pairs (a command woke a body).
		uint32 NumKept = 0;
		for (uint32 Index = 0; Index < static_cast<uint32>(SleepingContacts.size()); ++Index)
		{
			const FJoltRawContact& Contact = SleepingContacts[Index];
			if (!IsSleepingPair(Contact) || Bodies[Contact.Body1].bPoseSetByCommand || Bodies[Contact.Body2].bPoseSetByCommand)
			{
				continue;
			}
			SleepingContacts[NumKept++] = Contact;
		}
		SleepingContacts.resize(NumKept);
		for (const uint32 Index : KeptContactBodies)
		{
			FJoltBody& Record = Bodies[Index];
			Record.bKeptContactsListed = false;
			Record.bPutToSleepByCommand = false;
			Record.bPoseSetByCommand = false;
		}
		KeptContactBodies.clear();
	}

	void FPhysScene_Jolt::UpdateSleepingContacts()
	{
		// A pair is kept while it has a dynamic body and every dynamic body of it is asleep (IsSleepingPair). Jolt wakes a
		// sleeping body that an awake one touches, so a kept pair is never also in the listener's list.
		SleepingScratch.clear();
		for (const FJoltRawContact& Contact : SleepingContacts)
		{
			if (IsSleepingPair(Contact))
			{
				SleepingScratch.push_back(Contact);
			}
		}
		const FJoltRawContact* Stepped = ContactListener->GetContacts();
		const uint32 NumStepped = ContactListener->GetNumContacts();
		bSleepingContactsDropped = false;
		for (uint32 Index = 0; Index < NumStepped; ++Index)
		{
			if (!IsSleepingPair(Stepped[Index]))
			{
				continue;
			}
			if (SleepingScratch.size() >= SleepingScratch.capacity())
			{
				bSleepingContactsDropped = true;   // never grow: the arrays are sized once
				break;
			}
			SleepingScratch.push_back(Stepped[Index]);
		}
		std::swap(SleepingContacts, SleepingScratch);
	}

	PtResult FPhysScene_Jolt::StartFrame()
	{
		if (!(StepDeltaTime > 0.0f))
		{
			// Nothing to advance (the Harness driver never sends such a step). As in Null, the push consumed the forces;
			// impulses stay pending for the next step that runs.
			for (const uint32 Index : PendingForceBodies)
			{
				FJoltBody& Record = Bodies[Index];
				Record.PendingForce = JPH::Float3(0.0f, 0.0f, 0.0f);
				Record.PendingTorque = JPH::Float3(0.0f, 0.0f, 0.0f);
				Record.bPendingForce = false;
			}
			PendingForceBodies.clear();
			return PT_OK;
		}
		EnsureTempAllocator();
		// Kinematic bodies: a pending target becomes MoveKinematic for this step (the velocity that reaches it, actor pose).
		// A body that moved last step and got no target this step stops: Chaos' EKinematicTargetMode::Reset zeroes the
		// velocity (PBDRigidsEvolutionGBF.cpp:1191-1197 [V]); Jolt would keep moving it.
		if (!Kinematics.empty())
		{
			JPH::BodyInterface& BodyInterface = System->GetBodyInterfaceNoLock();
			for (const uint32 Index : Kinematics)
			{
				FJoltBody& Record = Bodies[Index];
				if (Record.bReleased)
				{
					continue;
				}
				if (Record.bHasKinematicTarget)
				{
					// The velocity Chaos reports: the origin's displacement over the step (ApplyKinematicTargets computes V from
					// the particle X, PBDRigidsEvolutionGBF.cpp:1180-1239 [V]); Jolt's own velocity is the COM's.
					const PtVec3 Origin = FromJoltLength(JPH::Vec3(GetBody(Record)->GetPosition()));
					const double InvDt = 1.0 / static_cast<double>(StepDeltaTime);
					Record.KinematicOriginVelocity = PtVec3{ (Record.KinematicTarget.Translation.X - Origin.X) * InvDt,
						(Record.KinematicTarget.Translation.Y - Origin.Y) * InvDt, (Record.KinematicTarget.Translation.Z - Origin.Z) * InvDt };
					BodyInterface.MoveKinematic(Record.Id, JPH::RVec3(ToJoltLength(Record.KinematicTarget.Translation)), ToJoltQuat(Record.KinematicTarget.Rotation),
						StepDeltaTime);
					Record.bHasKinematicTarget = false;
					Record.bKinematicMovedLastStep = true;
				}
				else if (Record.bKinematicMovedLastStep)
				{
					JPH::Body& Body = *GetBody(Record);
					Body.SetLinearVelocity(JPH::Vec3::sZero());
					Body.SetAngularVelocity(JPH::Vec3::sZero());
					Record.bKinematicMovedLastStep = false;
				}
			}
		}
		ApplyPendingDynamics();
		if (ContactListener)
		{
			FlushKeptContactCommands();   // before BeginStep clears the last step's contacts
			ContactListener->BeginStep();
		}
		const uint64 FallbacksBefore = TempAllocator->GetNumFallbacks();
		CheckJoltAsserts();   // the kinematic moves and pending dynamics above
		const JPH::EPhysicsUpdateError Error = System->Update(StepDeltaTime, 1, TempAllocator.get(), JobSystem.get());
		FrameErrorBits |= static_cast<uint32>(Error);
		if (TempAllocator->GetNumFallbacks() != FallbacksBefore)
		{
			FrameErrorBits |= GJoltErrorTempAllocatorFallback;
		}
		CheckJoltAsserts();   // the step itself
		if (ContactListener)
		{
			UpdateSleepingContacts();
		}
		TotalErrorBits |= FrameErrorBits;
		bStepped = true;
		++TotalSteps;
		return PT_OK;
	}

	void FPhysScene_Jolt::ApplyPendingDynamics()
	{
		// Velocity changes: one change at the step, added to the engine velocity before Update integrates gravity and the
		// forces, as Chaos adds LinearImpulseVelocity in Integrate (PBDRigidsEvolutionGBF.cpp:920-924 [V]) and Null in its
		// step. A body asleep now (PUT_TO_SLEEP after the impulse) keeps the change pending until a step finds it awake
		// (Null keeps it in LinearImpulseVelocity; UE drops it, docs/Abi.md 5.10 host rules).
		uint32 Kept = 0;
		for (uint32 Slot = 0; Slot < static_cast<uint32>(PendingImpulseBodies.size()); ++Slot)
		{
			const uint32 Index = PendingImpulseBodies[Slot];
			FJoltBody& Record = Bodies[Index];
			if (Record.bReleased)
			{
				Record.bPendingImpulse = false;
				continue;
			}
			JPH::Body& Body = *GetBody(Record);
			if (!Body.IsActive())
			{
				PendingImpulseBodies[Kept++] = Index;
				continue;
			}
			Body.SetLinearVelocityClamped(Body.GetLinearVelocity() + JPH::Vec3(Record.PendingLinearVelocity));
			Body.SetAngularVelocityClamped(Body.GetAngularVelocity() + JPH::Vec3(Record.PendingAngularVelocity));
			Record.PendingLinearVelocity = JPH::Float3(0.0f, 0.0f, 0.0f);
			Record.PendingAngularVelocity = JPH::Float3(0.0f, 0.0f, 0.0f);
			Record.bPendingImpulse = false;
		}
		PendingImpulseBodies.resize(Kept);

		// Forces and torques of this push act on this step only, and only on a body that is awake now (Null skips a
		// sleeping body in its step and clears the accumulators after the push). Update resets mForce / mTorque of the
		// active bodies after the step (PhysicsSystem.cpp:2541 [V]), so nothing is left on a body that sleeps later.
		for (const uint32 Index : PendingForceBodies)
		{
			FJoltBody& Record = Bodies[Index];
			if (!Record.bReleased)
			{
				JPH::Body& Body = *GetBody(Record);
				if (Body.IsActive())
				{
					Body.AddForce(JPH::Vec3(Record.PendingForce));
					Body.AddTorque(JPH::Vec3(Record.PendingTorque));
				}
			}
			Record.PendingForce = JPH::Float3(0.0f, 0.0f, 0.0f);
			Record.PendingTorque = JPH::Float3(0.0f, 0.0f, 0.0f);
			Record.bPendingForce = false;
		}
		PendingForceBodies.clear();
	}

	PtResult FPhysScene_Jolt::WaitPhysScenes()
	{
		return PT_OK;   // PhysicsSystem::Update is synchronous
	}

	PtResult FPhysScene_Jolt::EndFrame(PtFrameResult& Out)
	{
		CheckJoltAsserts();
		Out.NumStepsTaken = bStepped ? 1u : 0u;
		Out.EngineErrorBits = FrameErrorBits;
		return PT_OK;
	}

	// ---- commands ----------------------------------------------------------------------------------------------------
	PtResult FPhysScene_Jolt::ValidateBodyCommand(const PtBodyCommand& Cmd)
	{
		// Every check that can reject a command is here, so ApplyBodyCommand never fails (batch atomicity, docs/Abi.md 5.5).
		if (Cmd.Type == PT_CMD_SET_KINEMATIC_TARGET && Bodies[Cmd.Body].Motion != PT_MOTION_KINEMATIC)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Jolt: SET_KINEMATIC_TARGET on body %u, which is %s", Cmd.Body, GetMotionName(Bodies[Cmd.Body].Motion));
		}
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::ApplyBodyCommand(const PtBodyCommand& Cmd)
	{
		FJoltBody& Record = Bodies[Cmd.Body];
		JPH::BodyInterface& BodyInterface = System->GetBodyInterfaceNoLock();
		JPH::Body& Body = *GetBody(Record);
		const bool bDynamic = Record.Motion == PT_MOTION_DYNAMIC;
		const auto Wake = [&]()
		{
			if (bDynamic && !Body.IsActive())
			{
				BodyInterface.ActivateBody(Record.Id);
				MarkDirty(Cmd.Body);
				NoteKeptContactCommand(Cmd.Body, false, false);   // its kept pairs go at the next flush
			}
		};
		switch (Cmd.Type)
		{
		case PT_CMD_SET_GLOBAL_POSE:
			// UE5's teleport wakes a sleeping body whatever bAutoWake says (SetX, SingleParticlePhysicsProxy.h:240-251 [V]);
			// a kinematic body also gets the pose as its target with zero velocity (ChaosEngineInterface.cpp:2550-2557 [V]).
			BodyInterface.SetPositionAndRotation(Record.Id, JPH::RVec3(ToJoltLength(Cmd.Transform.Translation)), ToJoltQuat(Cmd.Transform.Rotation),
				Record.Motion == PT_MOTION_STATIC ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
			if (Record.Motion == PT_MOTION_KINEMATIC)
			{
				Body.SetLinearVelocity(JPH::Vec3::sZero());
				Body.SetAngularVelocity(JPH::Vec3::sZero());
				Record.KinematicTarget = Cmd.Transform;
				Record.bHasKinematicTarget = false;
				Record.bKinematicMovedLastStep = false;
			}
			MarkDirty(Cmd.Body);
			NoteKeptContactCommand(Cmd.Body, false, true);   // a static pose leaves the pairs on it asleep, with stale points
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
					Body.SetLinearVelocityClamped(ToJoltLength(Cmd.Vector));
				}
				else
				{
					Body.SetAngularVelocityClamped(ToJoltVector(Cmd.Vector));
				}
				MarkDirty(Cmd.Body);
				if ((Cmd.Flags & PT_CMDF_AUTO_WAKE) != 0)
				{
					Wake();
				}
			}
			break;
		case PT_CMD_SET_BODY_STATE:
			BodyInterface.SetPositionAndRotation(Record.Id, JPH::RVec3(ToJoltLength(Cmd.Transform.Translation)), ToJoltQuat(Cmd.Transform.Rotation),
				JPH::EActivation::DontActivate);
			if (bDynamic)
			{
				Body.SetLinearVelocityClamped(ToJoltLength(Cmd.Vector));
				Body.SetAngularVelocityClamped(ToJoltVector(Cmd.Vector2));
			}
			MarkDirty(Cmd.Body);
			NoteKeptContactCommand(Cmd.Body, false, true);   // DontActivate: a sleeping body stays asleep with stale kept points
			break;
		case PT_CMD_ADD_IMPULSE:
		case PT_CMD_ADD_VELOCITY:
			if (bDynamic)
			{
				Wake();   // [R] the Chaos game-thread impulse setter wakes the particle (as Null)
				// Deferred to the step (ApplyPendingDynamics), never Body::AddImpulse, which changes the velocity now.
				// FChaosStateOps::AddVelocity is AddImpulse(M * dv) (ChaosEngineInterface.cpp:861-864 [V]): dv as it is.
				const JPH::Vec3 DeltaV = Cmd.Type == PT_CMD_ADD_IMPULSE
					? ToJoltLength(Cmd.Vector) * Body.GetMotionProperties()->GetInverseMass()   // kg*cm/s -> kg*m/s -> m/s
					: ToJoltLength(Cmd.Vector);
				AddPending(Record.PendingLinearVelocity, DeltaV);
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
				const JPH::Vec3 DeltaW = Cmd.Type == PT_CMD_ADD_ANGULAR_IMPULSE
					? Body.GetMotionProperties()->MultiplyWorldSpaceInverseInertiaByVector(Body.GetRotation(), ToJoltInertialVector(Cmd.Vector))
					: ToJoltVector(Cmd.Vector);
				AddPending(Record.PendingAngularVelocity, DeltaW);
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
				if ((Cmd.Flags & PT_CMDF_NO_WAKE) != 0 && !Body.IsActive())
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
					const JPH::Vec3 Force = ToJoltLength(Cmd.Vector);
					AddPending(Record.PendingForce, bAccelChange ? Force * static_cast<float>(Record.Mass) : Force);
				}
				else
				{
					// bAccelChange: I_world * torque (ChaosEngineInterface.cpp:1029-1032 [V]) at push time; the vector is then an
					// angular acceleration (rad/s^2) and Jolt's world inertia is already in kg*m^2.
					AddPending(Record.PendingTorque, bAccelChange ? MultiplyWorldInertia(Body, ToJoltVector(Cmd.Vector)) : ToJoltInertialVector(Cmd.Vector));
				}
				// Handed to Body::AddForce / AddTorque at the step if the body is awake then (ApplyPendingDynamics).
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
			if (bDynamic && bSleepingAllowed && Body.IsActive())
			{
				// Jolt zeroes the velocities, as Null / Chaos do. The deferred impulse stays pending and the deferred force
				// is dropped at the step unless a later command of this push wakes the body again (as Null).
				BodyInterface.DeactivateBody(Record.Id);
				MarkDirty(Cmd.Body);
				NoteKeptContactCommand(Cmd.Body, true, false);   // keep its last-step contacts (FlushKeptContactCommands)
			}
			break;
		default:
			break;   // the kit validated the type
		}
		return PT_OK;
	}

	// ---- reads -------------------------------------------------------------------------------------------------------
	PtBodyState FPhysScene_Jolt::MakeState(const FJoltBody& Record) const
	{
		PtBodyState State = {};
		const JPH::Body& Body = *GetBody(Record);
		State.ActorTransform.Rotation = FromJoltQuat(Body.GetRotation());
		State.ActorTransform.Translation = FromJoltLength(JPH::Vec3(Body.GetPosition()));
		if (Record.Motion != PT_MOTION_STATIC)
		{
			const JPH::Vec3 Linear = Body.GetLinearVelocity();
			const JPH::Vec3 Angular = Body.GetAngularVelocity();
			if (Record.Motion == PT_MOTION_KINEMATIC && Record.bKinematicMovedLastStep)
			{
				// A target moved the body this step: the origin's displacement / dt, as Chaos (StartFrame).
				State.LinearVelocity = Record.KinematicOriginVelocity;
			}
			else if (Record.Motion == PT_MOTION_KINEMATIC)
			{
				// Otherwise Jolt's velocity converted to the origin (normally zero): v_origin = v_com - w x (R * c).
				const JPH::Vec3 ComOffset = Body.GetRotation() * Body.GetShape()->GetCenterOfMass();
				State.LinearVelocity = FromJoltLength(Linear - Angular.Cross(ComOffset));
			}
			else
			{
				State.LinearVelocity = FromJoltLength(Linear);
			}
			State.AngularVelocity = FromJoltVector(Angular);
		}
		State.ObjectState = Record.Motion == PT_MOTION_STATIC ? PT_OBJECT_STATE_STATIC
			: (Record.Motion == PT_MOTION_KINEMATIC ? PT_OBJECT_STATE_KINEMATIC : (Body.IsActive() ? PT_OBJECT_STATE_DYNAMIC : PT_OBJECT_STATE_SLEEPING));
		State.Flags = 0;
		return State;
	}

	PtResult FPhysScene_Jolt::ReadBodyStates(const uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, const uint32_t Capacity, uint32_t& OutCount)
	{
		const uint32 NumActive = System->GetNumActiveBodies(JPH::EBodyType::RigidBody);
		const JPH::BodyID* Active = System->GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
		const JPH::BodyLockInterfaceNoLock& Lock = System->GetBodyLockInterfaceNoLock();
		ActiveScratch.clear();
		for (uint32 Index = 0; Index < NumActive; ++Index)
		{
			ActiveScratch.push_back(GetBodyIndex(Lock.TryGetBody(Active[Index])->GetUserData()));
		}

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
				FJoltBody& Record = Bodies[Index];
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

		// ACTIVE_ONLY: Jolt's active list (awake dynamic and moving kinematic bodies), the bodies of the previous report
		// that left it (fell asleep: their ObjectState changed) and the bodies a command changed. One stamp per read
		// dedupes; the stamp advances even when the call fails, so a retry starts clean.
		const uint32 Stamp = ++ReadStamp;
		ReadScratch.clear();
		const auto Add = [this, Stamp](const uint32 Index)
		{
			FJoltBody& Record = Bodies[Index];
			if (!Record.bReleased && Record.LastReadStamp != Stamp)
			{
				Record.LastReadStamp = Stamp;
				ReadScratch.push_back(Index);
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
		OutCount = static_cast<uint32>(ReadScratch.size());
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

	PtResult FPhysScene_Jolt::ReadMassProperties(const uint32_t First, const uint32_t Num, PtMassProperties* Out)
	{
		for (uint32 Offset = 0; Offset < Num; ++Offset)
		{
			const FJoltBody& Record = Bodies[First + Offset];
			PtMassProperties& Mass = Out[Offset];
			if (Record.Motion != PT_MOTION_DYNAMIC)
			{
				// Jolt keeps no mass for static bodies and the kinematic mass is not used: the harness values.
				Mass.Mass = Record.Mass;
				Mass.InertiaDiagonal = Record.InertiaDiagonal;
				Mass.CenterOfMassLocal = Record.CenterOfMassLocal;
				continue;
			}
			// What the engine has (G2 readback): 1 / inverse mass, the diagonalised inertia and its frame, the shape COM. Jolt
			// re-diagonalises the provided tensor and sorts the principal moments largest first
			// (MassProperties::DecomposePrincipalMomentsOfInertia [V]), so for a non-cubic body InertiaDiagonal is a
			// permutation of the harness diagonal with a matching rotation: compare R diag(I) R^T (docs/Abi.md 5.7).
			const JPH::Body& Body = *GetBody(Record);
			const JPH::MotionProperties& Motion = *Body.GetMotionProperties();
			const JPH::Vec3 InvInertia = Motion.GetInverseInertiaDiagonal();
			Mass.Mass = 1.0 / static_cast<double>(Motion.GetInverseMass());
			Mass.InertiaDiagonal = PtVec3{ GScale.ToKgCm2(1.0 / static_cast<double>(InvInertia.GetX())), GScale.ToKgCm2(1.0 / static_cast<double>(InvInertia.GetY())),
				GScale.ToKgCm2(1.0 / static_cast<double>(InvInertia.GetZ())) };
			Mass.CenterOfMassLocal.Rotation = FromJoltQuat(Motion.GetInertiaRotation());
			Mass.CenterOfMassLocal.Translation = FromJoltLength(Body.GetShape()->GetCenterOfMass());
		}
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::GetStepStats(PtStepStats& Out)
	{
		CheckJoltAssertsBetweenFrames();   // reads and snapshots after the last frame (the bench reads the stats after its loop)
		// Values that need no listener (headline-safe): the active list. Contacts only with the viz listener.
		const uint32 NumActive = System->GetNumActiveBodies(JPH::EBodyType::RigidBody);
		const JPH::BodyID* Active = System->GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
		const JPH::BodyLockInterfaceNoLock& Lock = System->GetBodyLockInterfaceNoLock();
		int32 ActiveDynamic = 0;
		for (uint32 Index = 0; Index < NumActive; ++Index)
		{
			ActiveDynamic += Lock.TryGetBody(Active[Index])->IsDynamic() ? 1 : 0;
		}
		int32 Dynamic = 0;
		for (const FJoltBody& Record : Bodies)
		{
			Dynamic += !Record.bReleased && Record.Motion == PT_MOTION_DYNAMIC ? 1 : 0;
		}
		Out.NumActiveBodies = ActiveDynamic;
		Out.NumSleepingBodies = Dynamic - ActiveDynamic;
		if (ContactListener)
		{
			Out.NumContactPairs = static_cast<int32>(ContactListener->GetNumManifolds());
			Out.NumContactPoints = static_cast<int32>(ContactListener->GetNumContacts());
		}
		Out.NumIslands = -1;   // internal API only (GetIslandIndexInternal)
		Out.EngineErrorBits = TotalErrorBits;
		Out.NumPhaseTimes = 0;
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::CaptureDebugSnapshot(const PtDebugSnapshotRequest& Req, PtDebugSnapshotCounts& Out)
	{
		// Stage the snapshot now, so that ReadDebugSnapshot copies exactly what these counts describe.
		SnapshotParticles.clear();
		SnapshotContacts.clear();
		bool bTruncated = false;
		if ((Req.Flags & PT_SNAP_PARTICLES) != 0)
		{
			for (uint32 Index = 0; Index < Bodies.size(); ++Index)
			{
				const FJoltBody& Record = Bodies[Index];
				if (Record.bReleased)
				{
					continue;
				}
				const JPH::Body& Body = *GetBody(Record);
				const PtBodyState State = MakeState(Record);
				PtParticleSnapshot Particle = {};
				Particle.ActorTransform = State.ActorTransform;
				const JPH::Quat ComRotation = Record.Motion == PT_MOTION_DYNAMIC ? Body.GetRotation() * Body.GetMotionProperties()->GetInertiaRotation() : Body.GetRotation();
				Particle.CenterOfMassWorld.Rotation = FromJoltQuat(ComRotation.Normalized());
				Particle.CenterOfMassWorld.Translation = FromJoltLength(JPH::Vec3(Body.GetCenterOfMassPosition()));
				const JPH::AABox& Bounds = Body.GetWorldSpaceBounds();
				Particle.BoundsMin = FromJoltLength(Bounds.mMin);
				Particle.BoundsMax = FromJoltLength(Bounds.mMax);
				Particle.LinearVelocity = State.LinearVelocity;
				Particle.AngularVelocity = State.AngularVelocity;
				Particle.BodyIndex = Index;
				Particle.ObjectState = static_cast<uint8>(State.ObjectState);
				Particle.NativeIslandId = -1;
				SnapshotParticles.push_back(Particle);
			}
		}
		if ((Req.Flags & PT_SNAP_CONTACTS) != 0 && ContactListener)
		{
			bTruncated = bTruncated || ContactListener->HasDropped() || bSleepingContactsDropped;
			const double NaN = nan("");
			// The last step's contacts, then the kept contacts of sleeping islands (UpdateSleepingContacts).
			const FJoltRawContact* RawContacts = ContactListener->GetContacts();
			const uint32 NumStepped = ContactListener->GetNumContacts();
			const uint32 NumRaw = NumStepped + static_cast<uint32>(SleepingContacts.size());
			// Commands since the last step (FlushKeptContactCommands runs at the next StartFrame): a kept pair a command woke or
			// moved is already left out here, so a snapshot between steps shows no stale sleeping points.
			const bool bKeptCommandsPending = !KeptContactBodies.empty();
			for (uint32 RawIndex = 0; RawIndex < NumRaw; ++RawIndex)
			{
				const bool bSleeping = RawIndex >= NumStepped;
				const FJoltRawContact& Raw = bSleeping ? SleepingContacts[RawIndex - NumStepped] : RawContacts[RawIndex];
				if (bSleeping && bKeptCommandsPending
					&& (!IsSleepingPair(Raw) || Bodies[Raw.Body1].bPoseSetByCommand || Bodies[Raw.Body2].bPoseSetByCommand))
				{
					continue;
				}
				if (SnapshotContacts.size() >= Req.MaxContactPoints)
				{
					bTruncated = true;
					break;
				}
				if (Raw.Body1 >= Bodies.size() || Raw.Body2 >= Bodies.size() || Bodies[Raw.Body1].bReleased || Bodies[Raw.Body2].bReleased)
				{
					continue;
				}
				PtContactPoint Contact = {};
				Contact.PointOn0 = PtVec3{ GScale.ToCm(Raw.PointOn1[0]), GScale.ToCm(Raw.PointOn1[1]), GScale.ToCm(Raw.PointOn1[2]) };
				Contact.PointOn1 = PtVec3{ GScale.ToCm(Raw.PointOn2[0]), GScale.ToCm(Raw.PointOn2[1]), GScale.ToCm(Raw.PointOn2[2]) };
				Contact.Normal = PtVec3{ Raw.Normal[0], Raw.Normal[1], Raw.Normal[2] };
				Contact.Phi = GScale.ToCm(Raw.PenetrationDepth);   // this point's depth (p1 - p2) . n; negated below
				Contact.NormalImpulse = NaN;                     // Jolt exposes no per-point impulse (Q8)
				Contact.FrictionImpulse = PtVec3{ NaN, NaN, NaN };
				Contact.Body0 = Raw.Body1;
				Contact.Body1 = Raw.Body2;
				// The element indices of the manifold's sub shapes (M4, GetElementIndex in JoltContactListener.h).
				Contact.Shape0 = Raw.Shape1;
				Contact.Shape1 = Raw.Shape2;
				// Jolt: the normal moves body 2 out, i.e. points from body 1 (Body0) toward body 2 (Body1), and the depth is
				// positive when penetrating. Chaos: the normal lies on Body1 and points toward Body0, Phi < 0 penetrates
				// (ContactPoint.h:49-53 [V]); then Body0 = the more mobile body.
				Abi::CanonicalizeContact(Contact, MobilityRanks.data(), true, true);
				// A kept contact of a sleeping island carries the flags of its last stepped state plus PT_CONTACT_SLEEPING. Chaos
				// keeps a sleeping constraint's last solver results: they are rewritten only for constraints in the solver
				// (UpdateCollisionConstraintFromSolver, PBDCollisionContainerSolver.cpp:373-409 [V]), SetIsSleeping does not touch
				// them (PBDCollisionConstraint.cpp:538-545 [V]), and DrawCollisionImpl's bIsActive reads NetImpulse / NetPushOut
				// (ChaosDebugDraw.cpp:1453 [V]). So a resting sleeping contact stays active (red, or yellow-green in the static
				// friction cone) and gets the black sleeping box.
				Contact.Flags |= (Contact.Phi <= 0.0 ? PT_CONTACT_ACTIVE_APPROX : PT_CONTACT_SPECULATIVE) | (bSleeping ? PT_CONTACT_SLEEPING : 0u);
				SnapshotContacts.push_back(Contact);
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

	PtResult FPhysScene_Jolt::ReadDebugSnapshot(const PtDebugSnapshotBuffers& Buffers)
	{
		if (Buffers.ParticleCapacity < SnapshotParticles.size() || Buffers.ContactCapacity < SnapshotContacts.size())
		{
			return GetKit().SetLastError(PT_E_CAPACITY, "Jolt: ReadDebugSnapshot needs %u particles / %u contacts (capacity %u / %u)",
				static_cast<unsigned>(SnapshotParticles.size()), static_cast<unsigned>(SnapshotContacts.size()), Buffers.ParticleCapacity, Buffers.ContactCapacity);
		}
		if ((!SnapshotParticles.empty() && (Buffers.Particles == nullptr || Buffers.ParticleStride < sizeof(PtParticleSnapshot)))
			|| (!SnapshotContacts.empty() && (Buffers.ContactPoints == nullptr || Buffers.ContactStride < sizeof(PtContactPoint))))
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Jolt: ReadDebugSnapshot buffers are NULL or their strides are too small");
		}
		for (size_t Index = 0; Index < SnapshotParticles.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.Particles) + Index * Buffers.ParticleStride, &SnapshotParticles[Index], sizeof(PtParticleSnapshot));
		}
		for (size_t Index = 0; Index < SnapshotContacts.size(); ++Index)
		{
			memcpy(reinterpret_cast<uint8*>(Buffers.ContactPoints) + Index * Buffers.ContactStride, &SnapshotContacts[Index], sizeof(PtContactPoint));
		}
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::ComputeNativeStateHash(uint64_t& OutHash)
	{
		// FNV-1a over each live body's actor translation and rotation as Jolt has them (float, written as double in cm), in
		// body index order (02 7.2 #21). Engine-specific: never compared across engines.
		uint64 Hash = 0xcbf29ce484222325ull;
		for (const FJoltBody& Record : Bodies)
		{
			if (Record.bReleased)
			{
				continue;
			}
			const JPH::Body& Body = *GetBody(Record);
			const JPH::RVec3 Position = Body.GetPosition();
			const JPH::Quat Rotation = Body.GetRotation();
			const double Values[7] = { GScale.ToCm(static_cast<double>(Position.GetX())), GScale.ToCm(static_cast<double>(Position.GetY())),
				GScale.ToCm(static_cast<double>(Position.GetZ())), static_cast<double>(Rotation.GetX()), static_cast<double>(Rotation.GetY()),
				static_cast<double>(Rotation.GetZ()), static_cast<double>(Rotation.GetW()) };
			Hash = Abi::Fnv1a64(Values, sizeof(Values), Hash);
		}
		OutHash = Hash;
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::SetOption(const Abi::FStringRef Key, const Abi::FStringRef Value)
	{
		// Harness test hook (M4), runtime SetOption only: one assert outside Update, counted as the dbg hook counts one, so the
		// next frame reports GJoltErrorAssert. The "pt.test." prefix marks it as a test key; CreateScene options (profiles,
		// backend variants, the viewer's preflight) go through ApplySceneOption, which rejects it (SdkBackend tests).
		if (EqualsNoCase(GJoltTestRaiseAssertKey, Key))
		{
			RaiseTestAssert("SetOption");
			return PT_OK;
		}
		// The PhysicsSettings keys can change between frames; capacities, the convex radius and the temp block cannot.
		if (EqualsNoCase("ConvexRadius", Key) || EqualsNoCase("TempAllocatorBytes", Key))
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Jolt: scene option '%.*s' can only be given at CreateScene", static_cast<int>(Key.Length), Key.Data);
		}
		FJoltSceneOptions Updated = Options;
		char Error[256] = {};
		const PtResult Result = ApplySceneOption(Updated, Key, Value, Error);
		if (Result != PT_OK)
		{
			return GetKit().SetLastError(Result, "%s", Error);
		}
		Options = Updated;
		ApplyPhysicsSettings();
		return PT_OK;
	}

	bool FPhysScene_Jolt::ReadBoxConvexRadii(double& OutMinCm, double& OutMaxCm) const
	{
		float Min = 0.0f;
		float Max = 0.0f;
		bool bAny = false;
		for (const FJoltBody& Record : Bodies)
		{
			if (!Record.bReleased)
			{
				CollectBoxConvexRadii(*GetBody(Record)->GetShape(), Min, Max, bAny);
			}
		}
		OutMinCm = bAny ? GScale.ToCm(static_cast<double>(Min)) : 0.0;
		OutMaxCm = bAny ? GScale.ToCm(static_cast<double>(Max)) : 0.0;
		return bAny;
	}

	PtResult FPhysScene_Jolt::DumpEffectiveSettings(Abi::FTextWriter& Out)
	{
		const double NaN = nan("");
		const JPH::PhysicsSettings& Settings = System->GetPhysicsSettings();
		char Text[256];
		char VecText[128];
		const PtVec3 GravityCm = FromJoltLength(System->GetGravity());
		FormatVec(VecText, GravityCm);
		snprintf(Text, sizeof(Text), "%s cm/s^2 (PhysicsSystem::GetGravity)", VecText);
		Out.AppendEffective("world.gravity", Text, "cm/s^2", GravityCm.Z, "cm/s^2", "READBACK");
		snprintf(Text, sizeof(Text), "sequential impulse, %u velocity + %u position steps, large island splitter %s", Settings.mNumVelocitySteps,
			Settings.mNumPositionSteps, Settings.mUseLargeIslandSplitter ? "on" : "off");
		Out.AppendEffective("solver.type", Text, "", NaN, "", "READBACK");
		snprintf(Text, sizeof(Text), "velocity %u / position %u (PhysicsSettings); per-body overrides on %u bodies", Settings.mNumVelocitySteps,
			Settings.mNumPositionSteps, NumIterationOverrides);
		Out.AppendEffective("solver.iterations", Text, "steps", static_cast<double>(Settings.mNumVelocitySteps), "steps", "READBACK");
		Out.AppendEffective("broadphase.type", "BroadPhaseQuadTree, 2 layers (NON_MOVING, MOVING)", "", NaN, "", "COMPILE_TIME");
		const double ThresholdCm = GScale.ToCm(static_cast<double>(Settings.mMinVelocityForRestitution));
		snprintf(Text, sizeof(Text), "%.9g m/s (PhysicsSettings::mMinVelocityForRestitution)", static_cast<double>(Settings.mMinVelocityForRestitution));
		Out.AppendEffective("contact.restitution_threshold", Text, "m/s", ThresholdCm, "cm/s", "READBACK");
		Out.AppendEffective("contact.offset_rule", "speculative contacts within mSpeculativeContactDistance (no contact offset)", "", NaN, "", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "%.9g m", static_cast<double>(Settings.mSpeculativeContactDistance));
		Out.AppendEffective("contact.speculative_distance", Text, "m", GScale.ToCm(static_cast<double>(Settings.mSpeculativeContactDistance)), "cm", "READBACK");
		// The combine functions the PhysicsSystem holds (PhysicsSystem::GetCombineFriction / GetCombineRestitution): READBACK
		// (M5 round 0; they were COMPILE_TIME).
		const bool bUEFriction = System->GetCombineFriction() == &CombineFrictionUE;
		const bool bUERestitution = System->GetCombineRestitution() == &CombineRestitutionUE;
		Out.AppendEffective("material.friction_combine",
			bUEFriction ? "UE combine mode per pair (larger mode wins), PtCombine dynamic value (PhysicsSystem::GetCombineFriction)" : "other (not the UE combine function)",
			"", NaN, "", "READBACK");
		Out.AppendEffective("material.restitution_combine",
			bUERestitution ? "UE combine mode per pair (larger mode wins), PtCombine (PhysicsSystem::GetCombineRestitution)" : "other (not the UE combine function)", "",
			NaN, "", "READBACK");
		Out.AppendEffective("material.static_friction_rule", "dynamic_only", "", NaN, "", "COMPILE_TIME");
		Out.AppendEffective("body.damping_formula", "v *= max(0, 1 - c*dt) per step (Jolt MotionProperties)", "", NaN, "", "COMPILE_TIME");

		// Per-body values read back from the engine (dynamic bodies).
		FRange Linear;
		FRange Angular;
		FRange MaxAngular;
		FRange MaxLinear;
		uint32 NumDynamic = 0;
		uint32 NumMaySleep = 0;
		uint32 NumGyroscopic = 0;
		uint32 NumLinearCast = 0;
		for (const FJoltBody& Record : Bodies)
		{
			if (Record.bReleased || Record.Motion != PT_MOTION_DYNAMIC)
			{
				continue;
			}
			const JPH::Body& Body = *GetBody(Record);
			const JPH::MotionProperties& Motion = *Body.GetMotionProperties();
			++NumDynamic;
			Linear.Add(static_cast<double>(Motion.GetLinearDamping()));
			Angular.Add(static_cast<double>(Motion.GetAngularDamping()));
			MaxAngular.Add(static_cast<double>(Motion.GetMaxAngularVelocity()));
			MaxLinear.Add(GScale.ToCm(static_cast<double>(Motion.GetMaxLinearVelocity())));
			NumMaySleep += Body.GetAllowSleeping() ? 1u : 0u;
			NumGyroscopic += Body.GetApplyGyroscopicForce() ? 1u : 0u;
			NumLinearCast += Motion.GetMotionQuality() == JPH::EMotionQuality::LinearCast ? 1u : 0u;
		}
		snprintf(Text, sizeof(Text), "linear [%.9g, %.9g] angular [%.9g, %.9g] over %u dynamic bodies", Linear.Min, Linear.Max, Angular.Min, Angular.Max, NumDynamic);
		Out.AppendEffective("body.damping", Text, "1/s", Linear.bAny ? Linear.Max : NaN, "1/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] rad/s (%.9g = unclamped)", MaxAngular.Min, MaxAngular.Max, static_cast<double>(GUnclampedAngularVelocity));
		Out.AppendEffective("body.max_angular_velocity", Text, "rad/s", MaxAngular.bAny ? MaxAngular.Max : NaN, "rad/s", "READBACK");
		snprintf(Text, sizeof(Text), "[%.9g, %.9g] cm/s (Jolt default 500 m/s lifted; %.9g m/s = unclamped)", MaxLinear.Min, MaxLinear.Max,
			static_cast<double>(GUnclampedLinearVelocity));
		Out.AppendEffective("body.max_linear_velocity", Text, "cm/s", MaxLinear.bAny ? MaxLinear.Max : NaN, "cm/s", "READBACK");
		snprintf(Text, sizeof(Text), "%u of %u dynamic bodies apply the gyroscopic force", NumGyroscopic, NumDynamic);
		Out.AppendEffective("body.gyroscopic", Text, "bodies", NumGyroscopic > 0 ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "%s (%u of %u dynamic bodies may sleep, BodyCreationSettings::mAllowSleeping)", bSleepingAllowed ? "true" : "false",
			NumMaySleep, NumDynamic);
		Out.AppendEffective("sleep.enabled", Text, "bool", bSleepingAllowed && NumMaySleep > 0 ? 1.0 : 0.0, "bool", "READBACK");
		snprintf(Text, sizeof(Text), "every point of the body slower than %.9g m/s for %.9g s (global; UE per-material thresholds not mapped, M9)",
			static_cast<double>(Settings.mPointVelocitySleepThreshold), static_cast<double>(Settings.mTimeBeforeSleep));
		Out.AppendEffective("sleep.criteria", Text, "m/s", GScale.ToCm(static_cast<double>(Settings.mPointVelocitySleepThreshold)), "cm/s", "READBACK");
		snprintf(Text, sizeof(Text), "%u of %u dynamic bodies use EMotionQuality::LinearCast (speculative contacts always on)", NumLinearCast, NumDynamic);
		Out.AppendEffective("ccd.enabled", Text, "bodies", NumLinearCast > 0 ? 1.0 : 0.0, "bool", "READBACK");
		// The pool's own count: GetMaxConcurrency() = worker threads + the calling thread (JobSystemThreadPool.h:48 [V]).
		const int32 Workers = JobSystem->GetMaxConcurrency() - 1;
		snprintf(Text, sizeof(Text), "%d (JobSystemThreadPool::GetMaxConcurrency() - 1; the calling thread also runs jobs; requested %u)", Workers,
			NumWorkerThreads);
		Out.AppendEffective("threads.workers", Text, "threads", static_cast<double>(Workers), "threads", "READBACK");
		Out.AppendEffective("determinism.flag", Settings.mDeterministicSimulation ? "true (PhysicsSettings::mDeterministicSimulation)" : "false", "bool",
			Settings.mDeterministicSimulation ? 1.0 : 0.0, "bool", "READBACK");
		// Read from the bodies' shapes (BoxShape::GetConvexRadius through decorators and compounds), not from the option.
		double MinBoxRadiusCm = 0.0;
		double MaxBoxRadiusCm = 0.0;
		const bool bAnyBox = ReadBoxConvexRadii(MinBoxRadiusCm, MaxBoxRadiusCm);
		if (bAnyBox)
		{
			snprintf(Text, sizeof(Text), "convex radius [%.9g, %.9g] cm (BoxShape::GetConvexRadius over the live bodies; min(ConvexRadius, smallest half extent))",
				MinBoxRadiusCm, MaxBoxRadiusCm);
		}
		else
		{
			snprintf(Text, sizeof(Text), "no box shape");
		}
		Out.AppendEffective("shape.box_margin", Text, "cm", bAnyBox ? MaxBoxRadiusCm : NaN, "cm", "READBACK");
		// The option the adapter builds boxes with (an input, echoed): the shapes' actual radii are shape.box_margin.
		snprintf(Text, sizeof(Text), "%.9g cm (cDefaultConvexRadius 0.05 m unless the variant sets ConvexRadius)", Options.ConvexRadiusCm);
		Out.AppendEffective("shape.convex_radius", Text, "cm", Options.ConvexRadiusCm, "cm", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%u (compound %u, local_transform %u, offset_com %u, axis_remap %u)", Wrapping.Total(), Wrapping.Compound,
			Wrapping.LocalTransform, Wrapping.OffsetCom, Wrapping.AxisRemap);
		Out.AppendEffective("shape.wrapping", Text, "shapes", static_cast<double>(Wrapping.Total()), "shapes", "READBACK");
		Out.AppendEffective("listeners.installed", ContactListener ? "1 (contact listener: viz / PT_INSTR_CONTACTS)" : "0", "listeners", ContactListener ? 1.0 : 0.0,
			"listeners", "READBACK");

		// Jolt-specific (the first two are the backend's required keys).
		Out.AppendEffective("jolt.config_string", JPH::GetConfigurationString(), "", NaN, "", "COMPILE_TIME");
		const uint64 TempBytes = TempAllocator ? TempAllocator->GetSize()
			: (Options.TempAllocatorBytes > 0 ? Options.TempAllocatorBytes
				: GTempBaseBytes + static_cast<uint64>(MaxContactConstraints) * GTempBytesPerContactConstraint + static_cast<uint64>(MaxBodies) * GTempBytesPerBody);
		snprintf(Text, sizeof(Text), "%llu%s", static_cast<unsigned long long>(TempBytes), TempAllocator ? "" : " (allocated at PrepareForSimulation / first step)");
		// FJoltTempAllocator is the adapter's own allocator: its size, peak and fallbacks are adapter counts, SET_ONLY
		// (docs/Abi.md 5.14; M5 round 0).
		Out.AppendEffective("jolt.temp_allocator_bytes", Text, "bytes", static_cast<double>(TempBytes), "bytes", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%llu", static_cast<unsigned long long>(TempAllocator ? TempAllocator->GetPeakUsage() : 0));
		Out.AppendEffective("jolt.temp_allocator_peak_bytes", Text, "bytes", static_cast<double>(TempAllocator ? TempAllocator->GetPeakUsage() : 0), "bytes", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%llu (%llu bytes from the heap)", static_cast<unsigned long long>(TempAllocator ? TempAllocator->GetNumFallbacks() : 0),
			static_cast<unsigned long long>(TempAllocator ? TempAllocator->GetFallbackBytes() : 0));
		Out.AppendEffective("jolt.temp_allocator_fallbacks", Text, "allocations", static_cast<double>(TempAllocator ? TempAllocator->GetNumFallbacks() : 0),
			"allocations", "SET_ONLY");
		// The body capacity as the engine holds it (PhysicsSystem::GetMaxBodies, PhysicsSystem.h:225 [V]); Jolt has no getter for
		// the pair and contact constraint capacities, which are the values passed to PhysicsSystem::Init.
		const uint32 EngineMaxBodies = System->GetMaxBodies();
		snprintf(Text, sizeof(Text), "bodies %u (PhysicsSystem::GetMaxBodies), body pairs %u, contact constraints %u (passed to PhysicsSystem::Init)", EngineMaxBodies,
			MaxBodyPairs, MaxContactConstraints);
		Out.AppendEffective("jolt.capacity", Text, "bodies", static_cast<double>(EngineMaxBodies), "bodies", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g m", static_cast<double>(Settings.mPenetrationSlop));
		Out.AppendEffective("jolt.penetration_slop", Text, "m", GScale.ToCm(static_cast<double>(Settings.mPenetrationSlop)), "cm", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g", static_cast<double>(Settings.mBaumgarte));
		Out.AppendEffective("jolt.baumgarte", Text, "", static_cast<double>(Settings.mBaumgarte), "", "READBACK");
		Out.AppendEffective("jolt.max_depenetration_velocity", "n/a: no per-body initial-overlap rate (PtActorCreateInfo::MaxDepenetrationVelocity ignored)", "",
			NaN, "", "COMPILE_TIME");
		Out.AppendEffective("jolt.inertia_conditioning", "n/a: PT_BODY_INERTIA_CONDITIONING ignored", "", NaN, "", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "%u", NumStartAsleepSkipped);
		Out.AppendEffective("jolt.start_asleep_skipped", Text, "bodies", static_cast<double>(NumStartAsleepSkipped), "bodies", "SET_ONLY");
		snprintf(Text, sizeof(Text), "0x%x", TotalErrorBits);
		Out.AppendEffective("jolt.update_error_bits", Text, "bits", static_cast<double>(TotalErrorBits), "bits", "SET_ONLY");   // accumulated by the adapter
		// Which engine error bits mean "a capacity was exceeded" (the bench's exit code 5, docs/Abi.md 5.14); the others
		// (0x200 assert) are engine errors (exit code 3).
		snprintf(Text, sizeof(Text), "0x%x (ManifoldCacheFull 0x1 | BodyPairCacheFull 0x2 | ContactConstraintsFull 0x4 | temp allocator fallback 0x100)",
			GJoltCapacityErrorBits);
		Out.AppendEffective("errors.capacity_bits", Text, "bits", static_cast<double>(GJoltCapacityErrorBits), "bits", "COMPILE_TIME");
		return PT_OK;
	}

	PtResult FPhysScene_Jolt::GetMemoryStats(PtMemoryStats& Out)
	{
		// The per-scene temp block (allocator kind 2 = Jolt temp allocator): its size and its peak use. Jolt's heap
		// allocations are not counted (no allocator hook), so NumAllocs / NumFrees are -1 = unknown; the temp block's heap
		// fallbacks are the effective key jolt.temp_allocator_fallbacks and engine error bit 0x100, not an allocation count.
		Out.LiveBytes = TempAllocator ? static_cast<int64>(TempAllocator->GetSize()) : 0;
		Out.PeakBytes = TempAllocator ? static_cast<int64>(TempAllocator->GetPeakUsage()) : 0;
		Out.NumAllocs = -1;
		Out.NumFrees = -1;
		Out.AllocatorKind = 2;
		return PT_OK;
	}
}
