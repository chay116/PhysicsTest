// AggregateGeom.cpp - simple collision elements: transforms, UE scaling rules, element volumes and bounds.
// Behaviour mirrors UE 5.8.3 (Engine/Private/PhysicsEngine/BodySetup.cpp, Engine/Classes/PhysicsEngine/*Elem.h,
// PhysicsCore/Public/PhysicsInterfaceTypesCore.h); the code is our own.
#include "PhysicsEngine/AggregateGeom.h"

#include "Math/UnrealMathUtility.h"

namespace PhysicsTest
{
	namespace
	{
		// UE writes these volumes with float literals (SphereElem.h:80, SphylElem.h:102 [V]): 1.3333f and UE_PI.
		constexpr double ElemVolumeFourThirds = 1.33329999446868896484375;    // (double)1.3333f
		constexpr double FloatPi = 3.1415927410125732421875;                  // (double)UE_PI (float)
	}

	void SetupNonUniformHelper(FVector InScale3D, double& OutMinScale, double& OutMinScaleAbs, FVector& OutScale3DAbs)
	{
		// PhysicsInterfaceTypesCore.h:109-130 [V].
		if (InScale3D.IsNearlyZero())
		{
			InScale3D = FVector(0.1);
		}
		OutScale3DAbs = FVector(FMath::Abs(InScale3D.X), FMath::Abs(InScale3D.Y), FMath::Abs(InScale3D.Z));
		OutMinScaleAbs = OutScale3DAbs.GetMin();
		// Negative only when all three components are negative.
		OutMinScale = FMath::Max(FMath::Max(InScale3D.X, InScale3D.Y), InScale3D.Z) < 0.0 ? -OutMinScaleAbs : OutMinScaleAbs;
		if (FMath::IsNearlyZero(OutMinScale))
		{
			OutMinScale = 0.1;
			OutMinScaleAbs = 0.1;
		}
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FKSphereElem
	// ----------------------------------------------------------------------------------------------------------------
	double FKSphereElem::GetScaledVolume(const FVector& Scale3D) const
	{
		const double ScaledRadius = Radius * Scale3D.GetAbsMin();
		return ElemVolumeFourThirds * FloatPi * ScaledRadius * ScaledRadius * ScaledRadius;
	}

	FKSphereElem FKSphereElem::GetFinalScaled(const FVector& Scale3D) const
	{
		double MinScale = 0.0, MinScaleAbs = 0.0;
		FVector Scale3DAbs;
		SetupNonUniformHelper(Scale3D, MinScale, MinScaleAbs, Scale3DAbs);
		FKSphereElem Scaled = *this;
		Scaled.Radius *= MinScaleAbs;
		Scaled.Center = Center * Scale3D;
		return Scaled;
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FKBoxElem
	// ----------------------------------------------------------------------------------------------------------------
	void FKBoxElem::SetTransform(const FTransform& InTransform)
	{
		Rotation = InTransform.Rotator();
		Center = InTransform.GetLocation();
	}

	double FKBoxElem::GetScaledVolume(const FVector& Scale3D) const
	{
		return FMath::Abs(Scale3D.X * Scale3D.Y * Scale3D.Z * X * Y * Z);
	}

	FKBoxElem FKBoxElem::GetFinalScaled(const FVector& Scale3D) const
	{
		double MinScale = 0.0, MinScaleAbs = 0.0;
		FVector Scale3DAbs;
		SetupNonUniformHelper(Scale3D, MinScale, MinScaleAbs, Scale3DAbs);
		FKBoxElem Scaled = *this;
		Scaled.X *= Scale3DAbs.X;
		Scaled.Y *= Scale3DAbs.Y;
		Scaled.Z *= Scale3DAbs.Z;
		// UE: SetTransform(GetTransform() * RelativeTM * FTransform(Identity, 0, Scale3D)) - the centre is scaled, the
		// rotation is kept (BodySetup.cpp:2060-2062 [V]).
		const FTransform ScaleTransform(FQuat::Identity, FVector::ZeroVector, Scale3D);
		Scaled.SetTransform(GetTransform() * ScaleTransform);
		return Scaled;
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FKSphylElem
	// ----------------------------------------------------------------------------------------------------------------
	void FKSphylElem::SetTransform(const FTransform& InTransform)
	{
		Rotation = InTransform.Rotator();
		Center = InTransform.GetLocation();
	}

	void FKSphylElem::GetSegment(FVector& OutStart, FVector& OutEnd) const
	{
		const FVector HalfAxis = Rotation.RotateVector(FVector(0.0, 0.0, Length * 0.5));
		OutStart = Center - HalfAxis;
		OutEnd = Center + HalfAxis;
	}

	double FKSphylElem::GetScaledHalfLength(const FVector& Scale3D) const
	{
		return FMath::Max((Length + Radius * 2.0) * FMath::Abs(Scale3D.Z) * 0.5, 0.1);
	}

	double FKSphylElem::GetScaledRadius(const FVector& Scale3D) const
	{
		const double RadiusScale = FMath::Max(FMath::Abs(Scale3D.X), FMath::Abs(Scale3D.Y));
		return FMath::Clamp(Radius * RadiusScale, 0.1, GetScaledHalfLength(Scale3D));
	}

	double FKSphylElem::GetScaledCylinderLength(const FVector& Scale3D) const
	{
		return FMath::Max(0.1, (GetScaledHalfLength(Scale3D) - GetScaledRadius(Scale3D)) * 2.0);
	}

	double FKSphylElem::GetScaledVolume(const FVector& Scale3D) const
	{
		const double ScaledRadius = GetScaledRadius(Scale3D);
		const double ScaledLength = GetScaledCylinderLength(Scale3D);
		return FloatPi * ScaledRadius * ScaledRadius * (ElemVolumeFourThirds * ScaledRadius + ScaledLength);
	}

	FKSphylElem FKSphylElem::GetFinalScaled(const FVector& Scale3D) const
	{
		double MinScale = 0.0, MinScaleAbs = 0.0;
		FVector Scale3DAbs;
		SetupNonUniformHelper(Scale3D, MinScale, MinScaleAbs, Scale3DAbs);
		FKSphylElem Scaled = *this;
		Scaled.Radius = GetScaledRadius(Scale3DAbs);
		Scaled.Length = GetScaledCylinderLength(Scale3DAbs);
		// UE (BodySetup.cpp:2156-2162 [V]): Rotation = (FTransform(Rotation, 0, S) * FTransform(Identity, 0, S)).Rotation,
		// i.e. unchanged for the positive scales the harness accepts; Center * S.
		Scaled.Center = Center * Scale3D;
		return Scaled;
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FKConvexElem
	// ----------------------------------------------------------------------------------------------------------------
	void FKConvexElem::UpdateElemBox()
	{
		ElemBox = FBox();
		for (const FVector& Vertex : VertexData)
		{
			ElemBox += Vertex;
		}
	}

	// ----------------------------------------------------------------------------------------------------------------
	// FKAggregateGeom
	// ----------------------------------------------------------------------------------------------------------------
	int32 FKAggregateGeom::GetElementCount() const
	{
		return static_cast<int32>(SphereElems.size() + BoxElems.size() + SphylElems.size() + ConvexElems.size());
	}

	double FKAggregateGeom::GetScaledVolume(const FVector& Scale3D) const
	{
		double Volume = 0.0;
		for (const FKSphereElem& Elem : SphereElems)
		{
			Volume += Elem.GetScaledVolume(Scale3D);
		}
		for (const FKBoxElem& Elem : BoxElems)
		{
			Volume += Elem.GetScaledVolume(Scale3D);
		}
		for (const FKSphylElem& Elem : SphylElems)
		{
			Volume += Elem.GetScaledVolume(Scale3D);
		}
		return Volume;
	}

	FBox FKAggregateGeom::CalcAABB(const FTransform& Transform) const
	{
		const FVector Scale3D = Transform.GetScale3D();
		const FTransform NoScale(Transform.GetRotation(), Transform.GetTranslation());
		FBox Box;
		for (const FKSphereElem& Elem : SphereElems)
		{
			const FKSphereElem Scaled = Elem.GetFinalScaled(Scale3D);
			const FVector C = NoScale.TransformPosition(Scaled.Center);
			Box += C - FVector(Scaled.Radius);
			Box += C + FVector(Scaled.Radius);
		}
		for (const FKBoxElem& Elem : BoxElems)
		{
			const FKBoxElem Scaled = Elem.GetFinalScaled(Scale3D);
			const FTransform ElemToWorld = Scaled.GetTransform() * NoScale;
			const FVector Half = Scaled.GetHalfExtents();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Local((Corner & 1) ? Half.X : -Half.X, (Corner & 2) ? Half.Y : -Half.Y, (Corner & 4) ? Half.Z : -Half.Z);
				Box += ElemToWorld.TransformPosition(Local);
			}
		}
		for (const FKSphylElem& Elem : SphylElems)
		{
			const FKSphylElem Scaled = Elem.GetFinalScaled(Scale3D);
			FVector A, B;
			Scaled.GetSegment(A, B);
			for (const FVector& End : { NoScale.TransformPosition(A), NoScale.TransformPosition(B) })
			{
				Box += End - FVector(Scaled.Radius);
				Box += End + FVector(Scaled.Radius);
			}
		}
		for (const FKConvexElem& Elem : ConvexElems)
		{
			const FTransform ElemToWorld = Elem.GetTransform() * Transform;
			for (const FVector& Vertex : Elem.VertexData)
			{
				Box += ElemToWorld.TransformPosition(Vertex);
			}
		}
		return Box;
	}
}
