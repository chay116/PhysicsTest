// FreeFallFixture.cpp - see Fixtures/FreeFallFixture.h.
#include "Fixtures/FreeFallFixture.h"

#include "Physics/AnalyticReference.h"
#include "PhysScene.h"

namespace PhysicsTest::Fixtures
{
	FBodyInstance* FFreeFall::Build(FPhysScene& Scene, const FBodyInstance* BaseTemplate)
	{
		FBodySetup Setup;
		Setup.BodySetupName = "FreeFallSphere";
		Setup.AggGeom.SphereElems.emplace_back(Radius);
		const FBodySetup* SphereSetup = Scene.CreateBodySetup(std::move(Setup));

		FBodyInstance Template = BaseTemplate != nullptr ? *BaseTemplate : FBodyInstance();
		Template.BodyName = "FreeFallSphere";
		Template.bSimulatePhysics = true;
		Template.bStatic = false;
		Template.bOverrideMass = true;
		Template.MassInKgOverride = MassKg;
		Template.LinearDamping = 0.0f;
		Template.AngularDamping = 0.0f;
		Template.bEnableGravity = true;
		Template.bStartAwake = true;
		FBodyInstance* Body = Scene.SpawnBody(Template, SphereSetup, FTransform(FVector(0.0, 0.0, Z0)));
		Scene.FlushDeferredActors();
		return Body;
	}

	double FFreeFall::ExpectedZ(const int32 Steps, const float StepDeltaTime, const double GravityZ)
	{
		return Analytic::SemiImplicitEulerFreeFall(Z0, 0.0, GravityZ, Analytic::EngineDeltaTime(StepDeltaTime), Steps).Z;
	}

	double FFreeFall::ExpectedVz(const int32 Steps, const float StepDeltaTime, const double GravityZ)
	{
		return Analytic::SemiImplicitEulerFreeFall(Z0, 0.0, GravityZ, Analytic::EngineDeltaTime(StepDeltaTime), Steps).Vz;
	}
}
