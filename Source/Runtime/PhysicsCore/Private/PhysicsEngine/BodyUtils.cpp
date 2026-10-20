// BodyUtils.cpp - harness mass properties (D5). Behaviour mirrors UE 5.8.3 BodyUtils.cpp:16-141 and the Chaos code it
// calls (ChaosInterfaceUtils.cpp:137-190 geometry creation, MassProperties.cpp:17-102 diagonalisation and
// :274-492 combination, Sphere.h / Box.h / Capsule.h volume and inertia, Utilities.h:327-337, :537-560) [V].
// The code is our own; the step order is documented in PhysicsEngine/BodyUtils.h.
#include "PhysicsEngine/BodyUtils.h"

#include "Logging/LogMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Physics/Matrix33.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"

#include <vector>

namespace PhysicsTest::BodyUtils
{
	namespace
	{
		using Private::FMatrix33;

		// Constants as Chaos evaluates them. UE_PI is a float constant, so the shapes see (double)(float)pi; the capsule's
		// FourThirds is 4.0f / 3.0f computed in float (Capsule.h:280 [V]); the sphere's is 4. / 3 (double, Sphere.h:213-217).
		// Written as exact literals so that /fp:fast cannot fold them differently (docs/Progress.md M0 4).
		constexpr double ChaosFloatPi = 3.1415927410125732421875;              // (double)(float)UE_PI
		constexpr double ChaosCapsuleFourThirds = 1.33333337306976318359375;   // (double)(4.0f / 3.0f)

		struct FElementMass
		{
			double Volume = 0.0;
			double Mass = 0.0;
			FMatrix33 Inertia;      // about the element's centre of mass, in the element frame RotationOfMass
			FVector CenterOfMass;
			FQuat RotationOfMass;
		};

		bool IsIdentityRotation(const FQuat& Q)
		{
			return Q.Equals(FQuat::Identity, UE_SMALL_NUMBER);   // TRotation::IsIdentity tolerance
		}

		// Chaos FRotation3::FromRotatedVector(Z, Axis): the shortest arc from +Z to Axis (Capsule.h:300-305 [V]).
		FQuat ShortestArcFromZ(const FVector& Axis)
		{
			const FVector From(0.0, 0.0, 1.0);
			const double CosAngle = From | Axis;
			if (CosAngle >= 1.0 - 1e-12)
			{
				return FQuat::Identity;
			}
			if (CosAngle <= -1.0 + 1e-12)
			{
				return FQuat(FVector(1.0, 0.0, 0.0), UE_DOUBLE_PI);
			}
			const FVector RotationAxis = (From ^ Axis).GetSafeNormal();
			return FQuat(RotationAxis, FMath::Acos(CosAngle));
		}

		FElementMass SphereMass(const FKSphereElem& Elem, const FVector& Scale3D, const double Density)
		{
			// ChaosInterfaceUtils.cpp:137-144: GetFinalScaled, radius >= KINDA_SMALL_NUMBER, TSphere(Center, Radius).
			const FKSphereElem Scaled = Elem.GetFinalScaled(Scale3D);
			const double Radius = FMath::Max<double>(Scaled.Radius, UE_KINDA_SMALL_NUMBER);
			FElementMass Out;
			Out.Volume = (4.0 / 3.0 * ChaosFloatPi) * Radius * Radius * Radius;   // Sphere.h:213-217
			Out.Mass = Out.Volume * Density;
			const double Diagonal = (2.0 / 5.0) * Out.Mass * Radius * Radius;     // Sphere.h:315-320 (solid)
			Out.Inertia = FMatrix33::Diagonal(FVector(Diagonal));
			Out.CenterOfMass = Scaled.Center;
			return Out;
		}

		FElementMass BoxMass(const FKBoxElem& Elem, const FVector& Scale3D, const double Density)
		{
			// ChaosInterfaceUtils.cpp:146-171: half extents >= KINDA_SMALL_NUMBER; a rotated box is a transformed TBox.
			const FKBoxElem Scaled = Elem.GetFinalScaled(Scale3D);
			const FVector Dim(
				2.0 * FMath::Max<double>(Scaled.X * 0.5, UE_KINDA_SMALL_NUMBER),
				2.0 * FMath::Max<double>(Scaled.Y * 0.5, UE_KINDA_SMALL_NUMBER),
				2.0 * FMath::Max<double>(Scaled.Z * 0.5, UE_KINDA_SMALL_NUMBER));
			const FTransform BoxTransform = Scaled.GetTransform();
			FElementMass Out;
			Out.Volume = Dim.X * Dim.Y * Dim.Z;
			Out.Mass = Out.Volume * Density;
			const double M = Out.Mass / 12.0;   // Box.h:430-437
			Out.Inertia = FMatrix33::Diagonal(FVector(M * (Dim.Y * Dim.Y + Dim.Z * Dim.Z), M * (Dim.X * Dim.X + Dim.Z * Dim.Z), M * (Dim.X * Dim.X + Dim.Y * Dim.Y)));
			Out.CenterOfMass = BoxTransform.GetTranslation();
			Out.RotationOfMass = IsIdentityRotation(BoxTransform.GetRotation()) ? FQuat::Identity : BoxTransform.GetRotation();
			return Out;
		}

		FElementMass SphylMass(const FKSphylElem& Elem, const FVector& Scale3D, const double Density)
		{
			// ChaosInterfaceUtils.cpp:174-190: segment Center -/+ Rotation * (0, 0, HalfHeight), HalfHeight and Radius
			// >= KINDA_SMALL_NUMBER; FCapsule volume / inertia (Capsule.h:279-306).
			const FKSphylElem Scaled = Elem.GetFinalScaled(Scale3D);
			const double HalfHeight = FMath::Max<double>(Scaled.Length * 0.5, UE_KINDA_SMALL_NUMBER);
			const double R = FMath::Max<double>(Scaled.Radius, UE_KINDA_SMALL_NUMBER);
			const double H = 2.0 * HalfHeight;
			FElementMass Out;
			Out.Volume = ChaosFloatPi * R * R * (H + ChaosCapsuleFourThirds * R);
			Out.Mass = Out.Volume * Density;
			const double RR = R * R;
			const double HH = H * H;
			const double Diag12 = Out.Mass * (5.0 * HH * H + 20.0 * HH * R + 45.0 * H * RR + 32.0 * RR * R) / (60.0 * H + 80.0 * R);
			const double Diag3 = Out.Mass * (RR * (15.0 * H + 16.0 * R)) / (30.0 * H + 40.0 * R);
			Out.Inertia = FMatrix33::Diagonal(FVector(Diag12, Diag12, Diag3));
			Out.CenterOfMass = Scaled.Center;
			Out.RotationOfMass = ShortestArcFromZ(Scaled.Rotation.RotateVector(FVector(0.0, 0.0, 1.0)).GetSafeNormal());
			return Out;
		}

		// Chaos::TransformToLocalSpace (MassProperties.cpp:17-106): keeps an (almost) diagonal tensor as it is and
		// diagonalises otherwise; returns the principal moments and composes the rotation of mass.
		void TransformToLocalSpace(const FMatrix33& Inertia, FQuat& InOutRotationOfMass, FVector& OutPrincipal)
		{
			const double OffDiagSizeSq = FMath::Square(Inertia.M[1][0]) + FMath::Square(Inertia.M[2][0]) + FMath::Square(Inertia.M[2][1]);
			const double Trace = (Inertia.M[0][0] + Inertia.M[1][1] + Inertia.M[2][2]) / 3.0;
			if (Trace <= UE_SMALL_NUMBER || (OffDiagSizeSq / FMath::Square(Trace)) < UE_SMALL_NUMBER)
			{
				// Identity rotation; the diagonal is kept and the (tiny) off-diagonal terms are dropped (UE reads the
				// diagonal only, BodyInstance.cpp:3500-3503).
				OutPrincipal = Inertia.GetDiagonal();
				return;
			}
			const FMatrix33 Eigenvectors = Private::DiagonalizeSymmetric(Inertia, OutPrincipal);
			InOutRotationOfMass = InOutRotationOfMass * Private::QuatFromRotationMatrix(Eigenvectors);
		}

		// Chaos::Utilities::ScaleInertia (matrix overload, Utilities.h:537-560 [V]) with bScaleMass = false.
		FMatrix33 ScaleInertia(const FMatrix33& Inertia, const FVector& Scale)
		{
			const FVector AbsScale(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
			const FVector D = Inertia.GetDiagonal();
			const FVector XYZSq = (FVector(0.5 * (D.X + D.Y + D.Z)) - D) * AbsScale * AbsScale;
			FMatrix33 Result;
			Result.M[0][0] = XYZSq.Y + XYZSq.Z;
			Result.M[1][1] = XYZSq.X + XYZSq.Z;
			Result.M[2][2] = XYZSq.X + XYZSq.Y;
			Result.M[0][1] = Result.M[1][0] = Inertia.M[0][1] * AbsScale.X * AbsScale.Y;
			Result.M[0][2] = Result.M[2][0] = Inertia.M[0][2] * AbsScale.X * AbsScale.Z;
			Result.M[1][2] = Result.M[2][1] = Inertia.M[1][2] * AbsScale.Y * AbsScale.Z;
			return Result;
		}
	}

	double GetBodyInstanceDensity(const FPhysicalMaterial* Material)
	{
		// BodyUtils.cpp:21-46 [V]: nothing weighs less than hydrogen (0.09 kg/m^3 = 9e-8 kg/cm^3). UE computes this in
		// float (0.001f * Density: 0.0010000000475 kg/cm^3 for 1 g/cm^3); the harness uses double (02 5.2 #22).
		double DensityKgPerCm3 = 1.0;
		if (Material != nullptr)
		{
			DensityKgPerCm3 = FMath::Max(0.09 / (100.0 * 100.0 * 100.0), static_cast<double>(Material->Density) / 1000.0);
		}
		return DensityKgPerCm3;
	}

	FMassProperties ComputeUniformMassProperties(const FBodySetup& Setup, const FVector& Scale3D, const double DensityKgPerCm3)
	{
		// Setup.BuildScale3D is not applied: as in UE it only records the scale already baked into AggGeom
		// (UBodySetup::RescaleSimpleCollision, BodySetup.cpp:756-812 [V]; ChaosInterfaceUtils never reads it).
		const FVector& Scale = Scale3D;
		std::vector<FElementMass> Elements;
		for (const FKSphereElem& Elem : Setup.AggGeom.SphereElems)
		{
			if (Elem.GetContributeToMass())
			{
				Elements.push_back(SphereMass(Elem, Scale, DensityKgPerCm3));
			}
		}
		for (const FKBoxElem& Elem : Setup.AggGeom.BoxElems)
		{
			if (Elem.GetContributeToMass())
			{
				Elements.push_back(BoxMass(Elem, Scale, DensityKgPerCm3));
			}
		}
		for (const FKSphylElem& Elem : Setup.AggGeom.SphylElems)
		{
			if (Elem.GetContributeToMass())
			{
				Elements.push_back(SphylMass(Elem, Scale, DensityKgPerCm3));
			}
		}
		for (const FKConvexElem& Elem : Setup.AggGeom.ConvexElems)
		{
			if (Elem.GetContributeToMass())
			{
				UE_LOG(LogPhysicsTest, Error, "BodyUtils: convex element '%s' of '%s' is skipped - convex mass integration is backlog (M15)",
					Elem.Name.c_str(), Setup.BodySetupName.c_str());
			}
		}

		FMassProperties Out;
		FMatrix33 Tensor;
		// CalculateMassPropertiesFromShapeCollection (MassProperties.cpp:440-492) + CombineWorldSpace (:281-330).
		double TotalMass = 0.0;
		double TotalVolume = 0.0;
		for (const FElementMass& Element : Elements)
		{
			TotalMass += Element.Mass;
			TotalVolume += Element.Volume;
		}
		if (TotalMass > 0.0 && !Elements.empty())
		{
			if (Elements.size() == 1 && IsIdentityRotation(Elements[0].RotationOfMass))
			{
				Tensor = Elements[0].Inertia;
				Out.CenterOfMass = Elements[0].CenterOfMass;
			}
			else
			{
				FVector CenterOfMass;
				double Mass = 0.0;
				for (const FElementMass& Element : Elements)
				{
					const FMatrix33 R = FMatrix33::FromQuat(Element.RotationOfMass);
					const FMatrix33 World = R * Element.Inertia * R.GetTransposed();
					for (int32 Row = 0; Row < 3; ++Row)
					{
						for (int32 Column = 0; Column < 3; ++Column)
						{
							Tensor.M[Row][Column] += World.M[Row][Column];
						}
					}
					CenterOfMass += Element.CenterOfMass * Element.Mass;
					Mass += Element.Mass;
				}
				if (!(Mass > UE_SMALL_NUMBER))
				{
					// CombineWorldSpace's ensure (MassProperties.cpp:298-307 [V]) replaces only the tensor with a 100 cm
					// water cube's and keeps the (near-zero) total mass. The harness also returns the cube's 1000 kg:
					// the mass rule divides by the old mass (step 5), which would blow up for a near-zero mass.
					// Reached only when contributing elements sum to <= UE_SMALL_NUMBER kg.
					UE_LOG(LogPhysicsTest, Error, "BodyUtils: zero total mass for '%s'; using a 100 cm water cube (Chaos CombineWorldSpace)",
						Setup.BodySetupName.c_str());
					constexpr double Dim = 100.0;
					Out.Volume = Dim * Dim * Dim;
					Out.Mass = Out.Volume * 0.001;
					Out.InertiaTensor = FVector(Out.Mass * Dim * Dim / 6.0);
					return Out;
				}
				CenterOfMass /= Mass;
				if (Elements.size() > 1)
				{
					for (const FElementMass& Element : Elements)
					{
						const double M = Element.Mass;
						const FVector P = Element.CenterOfMass - CenterOfMass;
						Tensor.M[0][0] += M * (P.Y * P.Y + P.Z * P.Z);
						Tensor.M[1][1] += M * (P.Z * P.Z + P.X * P.X);
						Tensor.M[2][2] += M * (P.Y * P.Y + P.X * P.X);
						Tensor.M[0][1] -= M * P.Y * P.X;
						Tensor.M[1][0] -= M * P.Y * P.X;
						Tensor.M[0][2] -= M * P.Z * P.X;
						Tensor.M[2][0] -= M * P.Z * P.X;
						Tensor.M[1][2] -= M * P.Z * P.Y;
						Tensor.M[2][1] -= M * P.Z * P.Y;
					}
				}
				Out.CenterOfMass = CenterOfMass;
			}
			Out.Mass = TotalMass;
			Out.Volume = TotalVolume;
		}
		else
		{
			// No element contributes to the mass: Chaos' fallback, a 50 cm sphere of 1 g/cc
			// (CalculateMassPropertiesFromShapeCollection, MassProperties.cpp:481-497 [V]). UE itself never gets here:
			// FBodyInstance drops non-contributing shapes and then skips the mass update, so the CHAOS_ENSURE on the
			// inertia trace fails and the particle keeps its previous mass (BodyInstance.cpp:3405-3417, 3484-3507 [V]).
			// The harness keeps the deterministic Chaos value and reports it (02 5.2 #22).
			UE_LOG(LogPhysicsTest, Error, "BodyUtils: no element of '%s' contributes to the mass; using Chaos' 523 kg fallback "
				"(UE would skip the mass update)", Setup.BodySetupName.c_str());
			Tensor = FMatrix33::Diagonal(FVector(5.24e5));
			Out.Mass = 523.0;
			Out.Volume = 523000.0;
		}

		// BodyUtils.cpp:124: diagonalise (the rotation of mass starts at identity).
		Out.RotationOfMass = FQuat::Identity;
		TransformToLocalSpace(Tensor, Out.RotationOfMass, Out.InertiaTensor);
		return Out;
	}

	void ApplyMassPropertiesModifiers(FMassProperties& InOut, const FBodyInstance& Body, const FPhysicalMaterial* Material, const EMassRule Rule)
	{
		// Steps 4-5 (BodyUtils.cpp:50-76). The harness evaluates in double (UE: float OldMass / NewMass / MassRatio).
		const double OldMass = InOut.Mass;
		double NewMass = 0.0;
		if (!Body.bOverrideMass)
		{
			double RaiseMassToPower = 0.75;
			if (Material != nullptr)
			{
				RaiseMassToPower = Material->RaiseMassToPower;
			}
			const double UsePow = Rule == EMassRule::Physical ? 1.0 : FMath::Clamp<double>(RaiseMassToPower, UE_KINDA_SMALL_NUMBER, 1.0);
			NewMass = std::pow(OldMass, UsePow);
			NewMass = FMath::Max<double>(Body.MassScale * NewMass, 0.001);   // min weight of 1 g
		}
		else
		{
			NewMass = FMath::Max<double>(Body.MassInKgOverride, 0.001);
		}
		const double MassRatio = NewMass / OldMass;
		InOut.Mass *= MassRatio;
		InOut.InertiaTensor *= MassRatio;

		// Step 6 (:77-79): COMNudge in actor space = MassModifierTransform.TransformVector(COMNudge) with
		// MassModifierTransform = (Identity, 0, Scale3D) (BodyInstance.cpp:3490-3492).
		const FVector NudgeInActorSpace = Body.COMNudge * Body.Scale3D;
		InOut.CenterOfMass += NudgeInActorSpace;

		// Step 7 (:90-96): InertiaTensorScale with bInertiaScaleIncludeMass = false.
		if (!(Body.InertiaTensorScale - FVector(1.0)).IsNearlyZero(1e-3))
		{
			const FMatrix33 World = Private::ComputeWorldSpaceInertia(InOut.RotationOfMass, InOut.InertiaTensor);
			InOut.RotationOfMass = FQuat::Identity;
			TransformToLocalSpace(ScaleInertia(World, Body.InertiaTensorScale), InOut.RotationOfMass, InOut.InertiaTensor);
		}

		// Step 8 (:98-112): parallel-axis term of the nudge, diagonal only, exactly as UE writes it.
		if (PhysicsCVars::GetComNudgeAffectsInertia())
		{
			FMatrix33 World = Private::ComputeWorldSpaceInertia(InOut.RotationOfMass, InOut.InertiaTensor);
			InOut.RotationOfMass = FQuat::Identity;
			World.M[0][0] += InOut.Mass * NudgeInActorSpace.X * NudgeInActorSpace.X;
			World.M[1][1] += InOut.Mass * NudgeInActorSpace.Y * NudgeInActorSpace.Y;
			World.M[2][2] += InOut.Mass * NudgeInActorSpace.Z * NudgeInActorSpace.Z;
			TransformToLocalSpace(World, InOut.RotationOfMass, InOut.InertiaTensor);
		}
	}

	FMassProperties ComputeMassProperties(const FBodyInstance& Body, const FBodySetup& Setup, const FPhysicalMaterial* Material, const EMassRule Rule)
	{
		FMassProperties MassProperties = ComputeUniformMassProperties(Setup, Body.Scale3D, GetBodyInstanceDensity(Material));
		ApplyMassPropertiesModifiers(MassProperties, Body, Material, Rule);
		return MassProperties;
	}
}
