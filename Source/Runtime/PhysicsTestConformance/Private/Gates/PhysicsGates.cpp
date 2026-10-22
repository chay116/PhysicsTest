// PhysicsGates.cpp - the M6 physics gates (see Gates/PhysicsGates.h). Each gate builds its gate scenes through the
// scenario registry and FPhysScene, so the backend receives exactly what the bench and the viewer send (desc_sha256 equal
// across engines, DL43), ticks them with the profile's FixedStep dt, and compares with references computed with
// dt_engine = (double)(float)dt (DL23). Measured values go into the report notes, so a pass is never silent.
#include "Gates/PhysicsGates.h"

#include "Fixtures/FreeFallFixture.h"
#include "GateScenes.h"
#include "Logging/LogMacros.h"
#include "Physics/AnalyticReference.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsEngine/ConstraintUtils.h"
#include "PhysicsTestScenario.h"
#include "PhysScene.h"
#include "Math/UnrealMathUtility.h"
#include "Physics/PhysicsInterface.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsGates, Log, All);

namespace PhysicsTest::Gates
{
	namespace
	{
		// ---- formatting ---------------------------------------------------------------------------------------------
		std::string Format(const char* Pattern, ...)
		{
			char Text[1024];
			va_list Args;
			va_start(Args, Pattern);
			const int Written = std::vsnprintf(Text, sizeof(Text), Pattern, Args);
			va_end(Args);
			std::string Out(Text);
			if (Written >= static_cast<int>(sizeof(Text)))
			{
				// Truncated (a long tolerances.json reason in a note): drop a UTF-8 sequence cut at the end, so the note stays valid
				// UTF-8 - conformance.json's writer (nlohmann) throws on invalid UTF-8 and the bench would end without its outputs.
				size_t Lead = Out.size();
				while (Lead > 0 && (static_cast<unsigned char>(Out[Lead - 1]) & 0xC0u) == 0x80u)
				{
					--Lead;
				}
				if (Lead > 0)
				{
					const unsigned char First = static_cast<unsigned char>(Out[Lead - 1]);
					const size_t Need = First >= 0xF0u ? 4u : First >= 0xE0u ? 3u : First >= 0xC0u ? 2u : 1u;
					if (Out.size() - (Lead - 1) < Need)
					{
						Out.resize(Lead - 1);
					}
				}
			}
			return Out;
		}

		std::string ToLowerAscii(std::string_view Text)
		{
			std::string Out(Text);
			for (char& C : Out)
			{
				C = (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
			}
			return Out;
		}

		// ---- the scene of one gate run ------------------------------------------------------------------------------
		// The config copy outlives the scene (declared first, destroyed last).
		struct FGateScene
		{
			std::unique_ptr<FResolvedPhysicsConfig> Config;
			IPhysicsTestScenario* Scenario = nullptr;
			FScenarioParams Params;
			std::unique_ptr<FPhysScene> Scene;
			std::string Label;
			float Dt = 0.0f;
			double DtEngine = 0.0;
			int32 Frames = 0;
			uint32 InjectedEngineErrorBits = 0;   // FGateTarget::InjectedEngineErrorBits (test only)

			[[nodiscard]] FBodyInstance& Body(const uint32 Index) const { return *Scene->GetBodies()[Index]; }
		};

		// Builds ScenarioName with Overrides on the target. RunMode / Instrument override the profile's when set (G3: viz).
		// bSleepOff (J1): the scene runs with sleeping off whatever the profile says (a joint that falls asleep checks nothing; G6
		// owns sleep); every other setting of the profile and the variant stays.
		std::unique_ptr<FGateScene> MakeGateScene(const FGateTarget& Target, const char* ScenarioName, const FScenarioParams& Overrides,
			const EPhysicsRunMode* RunMode = nullptr, const bool bSleepOff = false)
		{
			auto Run = std::make_unique<FGateScene>();
			Run->Config = std::make_unique<FResolvedPhysicsConfig>(*Target.Config);
			if (RunMode != nullptr)
			{
				Run->Config->TestSettings.RunMode = *RunMode;
			}
			if (bSleepOff)
			{
				Run->Config->TestSettings.bAllowSleeping = false;
			}
			Run->Scenario = FScenarioRegistry::Get().Find(ScenarioName);
			if (Run->Scenario == nullptr)
			{
				UE_LOG(LogPhysicsGates, Fatal, "gate scene %s is not registered (PT::Scenarios linked without WHOLE_ARCHIVE?)", ScenarioName);
			}
			std::string Error;
			if (!Run->Scenario->ResolveParams(Overrides, Run->Params, Error))
			{
				UE_LOG(LogPhysicsGates, Fatal, "gate scene %s: %s", ScenarioName, Error.c_str());
			}
			const FScenarioCapacity Capacity = Run->Scenario->GetCapacity(Run->Params);
			FPhysSceneInitParams InitParams;
			InitParams.DebugName = std::string("Gate:") + ScenarioName;
			InitParams.BackendName = Target.Backend;
			InitParams.Config = Run->Config.get();
			InitParams.MaxBodiesHint = Capacity.MaxBodies;
			InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
			InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
			// M9: the gates never apply [/Script/PhysicsTest.ForcedOverrides] (docs/Scenes.md 2.9, 2.10: S05 / S06 need their own
			// combine modes; G9 / G12 judge the profile and the engine, not the forced values).
			InitParams.bApplyForcedOverrides = false;
			Run->Scene = std::make_unique<FPhysScene>(InitParams);
			IPhysicsTestScenario* Scenario = Run->Scenario;
			Run->Scene->OnPhysScenePreTick = [Scenario](FPhysScene* InScene, const float DeltaSeconds) { Scenario->OnPreTick(*InScene, DeltaSeconds); };
			FRandomStream Random(Run->Scenario->GetSeed(Run->Params));
			const FScenarioContext Context{ Run->Params, Random, Run->Config.get() };
			Run->Scenario->BuildScene(*Run->Scene, Context);
			Run->Scene->PrepareForSimulation();
			Run->Dt = Run->Config->TestSettings.FixedDeltaTime;
			Run->DtEngine = Analytic::EngineDeltaTime(Run->Dt);
			Run->InjectedEngineErrorBits = Target.InjectedEngineErrorBits;
			Run->Label = ScenarioName;
			for (const auto& [Name, Value] : Run->Params)
			{
				Run->Label += " " + Name + "=" + Value;
			}
			return Run;
		}

		// One frame; the engine error bits feed G12.
		void TickGateScene(FGateScene& Run, FGateRunState& State)
		{
			Run.Scene->Tick(Run.Dt);
			++Run.Frames;
			++State.NumFramesRun;
			const uint32 Bits = Run.Scene->GetLastFrameTiming().EngineErrorBits | Run.InjectedEngineErrorBits;
			if (Bits != 0)
			{
				State.AccumulatedEngineErrorBits |= Bits;
				if (State.ErrorFrames.size() < 8)
				{
					State.ErrorFrames.push_back(Format("%s frame %d: 0x%x", Run.Label.c_str(), Run.Frames, Bits));
				}
			}
		}

		void FinishGateScene(const FGateScene& Run, FGateRunState& State)
		{
			State.ScenesRun.push_back(Format("%s (%d frames)", Run.Label.c_str(), Run.Frames));
		}

		std::vector<FEffectiveSetting> ReadEffective(const FPhysScene& Scene, FGateReport& Report)
		{
			std::vector<FEffectiveSetting> Settings;
			std::string Error;
			Report.Check(ParseEffectiveSettings(Scene.DumpEffectiveSettings(), Settings, Error), "effective settings parse: " + Error);
			return Settings;
		}

		const FEffectiveSetting* FindSetting(const std::vector<FEffectiveSetting>& Settings, const std::string_view Key)
		{
			const auto Found = std::find_if(Settings.begin(), Settings.end(), [Key](const FEffectiveSetting& S) { return S.Key == Key; });
			return Found != Settings.end() ? &*Found : nullptr;
		}

		// A post-run registration (tolerances.json "registered": "post-run") the gate's verdict uses goes into the report once,
		// with its sign-off: conformance.json lists them (M6 round 2), so a pass that rests on an unsigned deviation is visible
		// without parsing notes.
		void RecordPostRun(const FGateTarget& Target, FGateReport& Report, const char* Gate, const char* Check)
		{
			const FToleranceOverride* Override = Target.Tolerances->FindOverride(Gate, Check, Target.Backend, Target.Variant, Target.Profile);
			if (Override == nullptr || !Override->bPostRun)
			{
				return;
			}
			const std::string Key = std::string(Gate) + "." + Check;
			for (const FGatePostRunRegistration& Used : Report.PostRunRegistrations)
			{
				if (Used.Key == Key)
				{
					return;
				}
			}
			Report.PostRunRegistrations.push_back(FGatePostRunRegistration{ Key, Override->Engine, Override->Variant, Override->Profile, Override->Value,
				Override->SignOff });
		}

		double Tolerance(const FGateTarget& Target, FGateReport& Report, const char* Gate, const char* Check)
		{
			RecordPostRun(Target, Report, Gate, Check);
			return Target.Tolerances->Get(Gate, Check, Target.Backend, Target.Variant, Target.Profile);
		}

		// A registered value that differs from its default is an expected deviation: the report says which and why.
		double ToleranceNoted(const FGateTarget& Target, FGateReport& Report, const char* Gate, const char* Check)
		{
			std::string Reason;
			bool bPostRun = false;
			std::string SignOff;
			RecordPostRun(Target, Report, Gate, Check);
			const double Value = Target.Tolerances->Get(Gate, Check, Target.Backend, Target.Variant, Target.Profile, &Reason, &bPostRun, &SignOff);
			std::string DefaultReason;
			const double Default = Target.Tolerances->Get(Gate, Check, "", "", "", &DefaultReason);
			if (Value != Default)
			{
				const std::string PostRun = bPostRun ? ", registered post-run after the mechanism was verified, lead sign-off: " + SignOff : std::string();
				Report.Note(Format("registered deviation %s.%s = %.9g (default %.9g%s): %s", Gate, Check, Value, Default, PostRun.c_str(), Reason.c_str()));
			}
			return Value;
		}

		// A part of a gate the target cannot run (G2's S03 without collision): a note when tolerances.json allows the skip
		// for the target, a failed check otherwise. True when allowed.
		bool PartSkipAllowed(const FGateTarget& Target, FGateReport& Report, const char* Gate, const std::string& What)
		{
			const FAllowedSkip* Allowed = Target.Tolerances->FindAllowedSkip(Gate, Target.Backend, Target.Variant, Target.Profile);
			if (Allowed != nullptr)
			{
				Report.Note(What + " (registered skip: " + Allowed->Reason + ")");
				return true;
			}
			Report.Check(false, What + ": an unregistered skip (tolerances.json gates." + Gate + ".allowed_skip has no entry for " + Target.GetEngineLabel() + ")");
			return false;
		}

		// The contact points of one body pair in a snapshot, in either order; OutSleeping counts PT_CONTACT_SLEEPING points.
		int32 CountPairPoints(const FPhysicsDebugSnapshot& Snapshot, const uint32 BodyA, const uint32 BodyB, int32& OutSleeping, int32& OutMultiFlag)
		{
			int32 Points = 0;
			OutSleeping = 0;
			OutMultiFlag = 0;
			for (const PtContactPoint& Contact : Snapshot.Contacts)
			{
				if ((Contact.Body0 == BodyA && Contact.Body1 == BodyB) || (Contact.Body0 == BodyB && Contact.Body1 == BodyA))
				{
					++Points;
					OutSleeping += (Contact.Flags & PT_CONTACT_SLEEPING) != 0u ? 1 : 0;
					const uint32 Exclusive = Contact.Flags & (PT_CONTACT_ACTIVE | PT_CONTACT_ACTIVE_APPROX | PT_CONTACT_SPECULATIVE);
					OutMultiFlag += (Exclusive & (Exclusive - 1u)) != 0u ? 1 : 0;
				}
			}
			return Points;
		}

		bool Declares(const FPhysScene& Scene, const uint64 Capability)
		{
			return Scene.GetBackendInfo().Has(Capability);
		}

		// R diag(I) R^T (kg cm^2) and its relative Frobenius error against the harness body (G2 readback).
		void InertiaTensor(const FQuat& Rotation, const FVector& Diagonal, double (&Out)[3][3])
		{
			const FVector Axes[3] = { Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ() };
			const double Moments[3] = { Diagonal.X, Diagonal.Y, Diagonal.Z };
			for (int Row = 0; Row < 3; ++Row)
			{
				for (int Column = 0; Column < 3; ++Column)
				{
					double Sum = 0.0;
					for (int Axis = 0; Axis < 3; ++Axis)
					{
						const double A[3] = { Axes[Axis].X, Axes[Axis].Y, Axes[Axis].Z };
						Sum += Moments[Axis] * A[Row] * A[Column];
					}
					Out[Row][Column] = Sum;
				}
			}
		}

		double InertiaRelError(const FBodyInstance& Body, const PtMassProperties& Engine)
		{
			double Harness[3][3];
			double Read[3][3];
			InertiaTensor(Body.MassProperties.RotationOfMass, Body.MassProperties.InertiaTensor, Harness);
			InertiaTensor(FQuat(Engine.CenterOfMassLocal.Rotation.X, Engine.CenterOfMassLocal.Rotation.Y, Engine.CenterOfMassLocal.Rotation.Z,
				Engine.CenterOfMassLocal.Rotation.W), FVector(Engine.InertiaDiagonal.X, Engine.InertiaDiagonal.Y, Engine.InertiaDiagonal.Z), Read);
			double Difference = 0.0;
			double Norm = 0.0;
			for (int Row = 0; Row < 3; ++Row)
			{
				for (int Column = 0; Column < 3; ++Column)
				{
					Difference += (Read[Row][Column] - Harness[Row][Column]) * (Read[Row][Column] - Harness[Row][Column]);
					Norm += Harness[Row][Column] * Harness[Row][Column];
				}
			}
			return Norm > 0.0 ? std::sqrt(Difference / Norm) : 0.0;
		}

		// The engine's mass, inertia tensor and COM (ReadMassProperties) against the harness values (G2, docs/Abi.md 5.7).
		void CheckMassReadback(const FGateTarget& Target, FGateReport& Report, const FGateScene& Run, const uint32 Index, const char* What)
		{
			const FBodyInstance& Body = Run.Body(Index);
			PtMassProperties Engine = {};
			const PtResult Result = Run.Scene->GetBackendScene().ReadMassProperties(Index, std::span<PtMassProperties>(&Engine, 1));
			if (!Report.Check(Result == PT_OK, Format("%s: ReadMassProperties(%u) = %s", What, Index, GetPtResultName(Result))))
			{
				return;
			}
			const double MassRel = std::abs(Engine.Mass - Body.MassProperties.Mass) / Body.MassProperties.Mass;
			const double InertiaRel = InertiaRelError(Body, Engine);
			const FVector EngineCom(Engine.CenterOfMassLocal.Translation.X, Engine.CenterOfMassLocal.Translation.Y, Engine.CenterOfMassLocal.Translation.Z);
			const double ComError = (EngineCom - Body.MassProperties.CenterOfMass).Size();
			const double MassTol = Tolerance(Target, Report, "G2", "mass_rel");
			const double InertiaTol = Tolerance(Target, Report, "G2", "inertia_tensor_rel");
			const double ComTol = Tolerance(Target, Report, "G2", "com_abs_cm");
			Report.Check(MassRel <= MassTol, Format("%s: mass %.9g kg vs harness %.9g (rel %.3g > %.3g)", What, Engine.Mass, Body.MassProperties.Mass, MassRel, MassTol));
			Report.Check(InertiaRel <= InertiaTol, Format("%s: inertia tensor rel error %.3g > %.3g", What, InertiaRel, InertiaTol));
			Report.Check(ComError <= ComTol, Format("%s: COM (%.6g, %.6g, %.6g) vs harness (%.6g, %.6g, %.6g), |d| %.3g cm > %.3g", What, EngineCom.X, EngineCom.Y,
				EngineCom.Z, Body.MassProperties.CenterOfMass.X, Body.MassProperties.CenterOfMass.Y, Body.MassProperties.CenterOfMass.Z, ComError, ComTol));
			Report.Note(Format("%s: mass rel %.2g, inertia tensor rel %.2g, COM |d| %.2g cm (COM z %.6g)", What, MassRel, InertiaRel, ComError, EngineCom.Z));
		}

		// Signed distance of a world point to a body's single shape at a given body pose (G3 contact points). The element's
		// transform is applied first, then the body pose (UE order: Elem * Body).
		double BodySignedDistance(const FBodyInstance& Body, const FTransform& BodyPose, const FVector& WorldPoint)
		{
			const FKAggregateGeom& Geom = Body.BodySetup->AggGeom;
			if (!Geom.BoxElems.empty())
			{
				const FKBoxElem& Box = Geom.BoxElems[0];
				const FVector Local = (Box.GetTransform() * BodyPose).InverseTransformPositionNoScale(WorldPoint);
				const FVector Half(0.5 * Box.X, 0.5 * Box.Y, 0.5 * Box.Z);
				const FVector Q(std::abs(Local.X) - Half.X, std::abs(Local.Y) - Half.Y, std::abs(Local.Z) - Half.Z);
				const FVector Outside(std::max(Q.X, 0.0), std::max(Q.Y, 0.0), std::max(Q.Z, 0.0));
				return Outside.Size() + std::min(std::max(Q.X, std::max(Q.Y, Q.Z)), 0.0);
			}
			if (!Geom.SphereElems.empty())
			{
				const FKSphereElem& Sphere = Geom.SphereElems[0];
				return (WorldPoint - BodyPose.TransformPositionNoScale(Sphere.Center)).Size() - Sphere.Radius;
			}
			if (!Geom.SphylElems.empty())
			{
				const FKSphylElem& Capsule = Geom.SphylElems[0];
				const FVector Local = (Capsule.GetTransform() * BodyPose).InverseTransformPositionNoScale(WorldPoint);
				const double Half = 0.5 * Capsule.Length;
				const double Z = std::clamp(Local.Z, -Half, Half);
				return (Local - FVector(0.0, 0.0, Z)).Size() - Capsule.Radius;
			}
			return 0.0;
		}

		// The rotation angle between two orientations, in [0, pi].
		double AngleBetween(const FQuat& A, const FQuat& B)
		{
			const double Dot = std::min(1.0, std::abs(A | B));
			return 2.0 * std::acos(Dot);
		}

		// ---- G1 -----------------------------------------------------------------------------------------------------
		class FGateG1 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G1"; }
			const char* GetDescription() const override { return "S00.FreeFall: z60 and v60 against the dt_engine reference"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, "S00.FreeFall", {});
				const int32 Frames = Run->Scenario->GetDefaultFrames(Run->Params);
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				const FBodyInstance& Sphere = Run->Body(0);
				const double Z = Sphere.State.BodyTM.GetTranslation().Z;
				const double V = Sphere.State.LinearVelocity.Z;
				const Analytic::FFreeFallState Reference = Analytic::SemiImplicitEulerFreeFall(Fixtures::FFreeFall::Z0, 0.0, Run->Scene->GetGravity().Z, Run->DtEngine, Frames);
				const double ZTol = ToleranceNoted(Target, Report, "G1", "z60_abs_cm");
				const double VTol = ToleranceNoted(Target, Report, "G1", "v60_abs_cms");
				// Every engine against the dt_engine reference of its own profile (DL23). The 03 M6 numbers 501.83328 / -980.00005
				// are that reference rounded for dt 1/60, g -980 and 60 frames: a sanity check of the reference, not the reference.
				const double ZRef = Reference.Z;
				const double VRef = Reference.Vz;
				if (Run->Dt == 1.0f / 60.0f && Run->Scene->GetGravity().Z == -980.0 && Frames == 60)
				{
					Report.Check(std::abs(ZRef - 501.83328) < 5e-6 && std::abs(VRef + 980.00005) < 5e-6, Format("the dt_engine reference (%.10f, %.8f) is not 03 M6's "
						"501.83328 / -980.00005 for dt 1/60", ZRef, VRef));
				}
				Report.Check(Run->Scene->GetStepCount() == static_cast<uint64>(Frames), Format("%llu steps, expected %d", static_cast<unsigned long long>(Run->Scene->GetStepCount()), Frames));
				Report.Check(std::abs(Z - ZRef) <= ZTol, Format("z60 %.10f cm, |z60 - %.10f| = %.3g > %.3g", Z, ZRef, std::abs(Z - ZRef), ZTol));
				Report.Check(std::abs(V - VRef) <= VTol, Format("v60 %.8f cm/s, |v60 - (%.8f)| = %.3g > %.3g", V, VRef, std::abs(V - VRef), VTol));
				Report.Note(Format("z60 %.9f cm (|dz| %.2e), v60 %.7f cm/s (|dv| %.2e) against the dt_engine reference z %.10f v %.7f", Z, std::abs(Z - ZRef), V,
					std::abs(V - VRef), ZRef, VRef));
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G1b ----------------------------------------------------------------------------------------------------
		class FGateG1b final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G1b"; }
			const char* GetDescription() const override { return "S00d.FreeFallDamped: z60 against the declared damping formula's reference"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				using namespace GateScenes::S00d;
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*Run->Scene, Report);
				const FEffectiveSetting* Formula = FindSetting(Settings, "body.damping_formula");
				if (!Report.Check(Formula != nullptr && Formula->bHasCanonical && (Formula->Canonical == 1.0 || Formula->Canonical == 2.0),
					"body.damping_formula must declare its formula as canonical 1 (UE order) or 2 (before forces), docs/Abi.md 5.14"))
				{
					return Report;
				}
				const auto Kind = static_cast<Analytic::EDampingFormula>(static_cast<int32>(Formula->Canonical));
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				const double Z = Run->Body(0).State.BodyTM.GetTranslation().Z;
				const double GravityZ = Run->Scene->GetGravity().Z;
				const Analytic::FFreeFallState Reference = Analytic::DampedFreeFall(Z0, GravityZ, LinearDamping, Run->DtEngine, Frames, Kind);
				const Analytic::FFreeFallState Other = Analytic::DampedFreeFall(Z0, GravityZ, LinearDamping, Run->DtEngine, Frames,
					Kind == Analytic::EDampingFormula::UEOrder ? Analytic::EDampingFormula::BeforeForces : Analytic::EDampingFormula::UEOrder);
				const double Tol = ToleranceNoted(Target, Report, "G1b", "z60_abs_cm");
				Report.Check(std::abs(Z - Reference.Z) <= Tol, Format("z60 %.9f cm, declared formula %d reference %.9f, |dz| %.3g > %.3g", Z,
					static_cast<int32>(Kind), Reference.Z, std::abs(Z - Reference.Z), Tol));
				Report.Note(Format("declared '%s' (formula %d): z60 %.9f cm, reference %.9f (|dz| %.2e); the other order would give %.6f", Formula->Native.c_str(),
					static_cast<int32>(Kind), Z, Reference.Z, std::abs(Z - Reference.Z), Other.Z));
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G2 -----------------------------------------------------------------------------------------------------
		class FGateG2 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G2"; }
			const char* GetDescription() const override { return "S01 / S02 / S03: impulse, spin inertia, mass readback, capsule rest heights"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				const double RelTol = ToleranceNoted(Target, Report, "G2", "velocity_rel");
				// S01.UnitImpulse
				{
					using namespace GateScenes::S01;
					std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
					CheckMassReadback(Target, Report, *Run, ImpulseBody, "S01 body 0");
					for (int32 Frame = 0; Frame < Frames; ++Frame)
					{
						TickGateScene(*Run, State);
						if (Frame == 0 || Frame == Frames - 1)
						{
							for (const uint32 Index : { ImpulseBody, VelocityChangeBody })
							{
								const FVector V = Run->Body(Index).State.LinearVelocity;
								const double Rel = (V - FVector(ExpectedSpeed, 0.0, 0.0)).Size() / ExpectedSpeed;
								Report.Check(Rel <= RelTol, Format("S01 body %u frame %d: v (%.9g, %.3g, %.3g) cm/s, rel error %.3g > %.3g", Index, Frame + 1, V.X, V.Y, V.Z, Rel, RelTol));
								if (Frame == Frames - 1)
								{
									Report.Note(Format("S01 body %u (%s): v.x %.9g cm/s (rel %.2g)", Index, Index == ImpulseBody ? "impulse 1000 kg cm/s" : "velocity change 100 cm/s",
										V.X, Rel));
								}
							}
						}
					}
					FinishGateScene(*Run, State);
				}
				// S02.SpinInertia
				{
					using namespace GateScenes::S02;
					std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
					CheckMassReadback(Target, Report, *Run, SpinZBody, "S02 body 0");
					CheckMassReadback(Target, Report, *Run, NudgedBody, "S02 body 2 (COMNudge 0,0,10)");
					// omega = I_world^-1 L with the harness tensor and the frame-0 pose (the pre-tick applies L at frame 0).
					const FVector Impulses[3] = { FVector(0.0, 0.0, Izz * TargetOmega), FVector(Ixx * TargetOmega, 0.0, 0.0), FVector(0.0, 0.0, Izz * TargetOmega) };
					FVector Expected[3];
					FVector ComWorld0;
					for (uint32 Index = 0; Index < 3; ++Index)
					{
						const FBodyInstance& Body = Run->Body(Index);
						const FQuat Principal = Body.State.BodyTM.GetRotation() * Body.MassProperties.RotationOfMass;
						const FVector LocalL = Principal.UnrotateVector(Impulses[Index]);
						const FVector& I = Body.MassProperties.InertiaTensor;
						Expected[Index] = Principal.RotateVector(FVector(LocalL.X / I.X, LocalL.Y / I.Y, LocalL.Z / I.Z));
					}
					ComWorld0 = Run->Body(NudgedBody).State.BodyTM.TransformPositionNoScale(Run->Body(NudgedBody).MassProperties.CenterOfMass);
					double MaxComDrift = 0.0;
					const double ComSpeedTol = ToleranceNoted(Target, Report, "G2", "com_speed_cms");
					for (int32 Frame = 0; Frame < Frames; ++Frame)
					{
						TickGateScene(*Run, State);
						const FBodyInstance& Nudged = Run->Body(NudgedBody);
						MaxComDrift = std::max(MaxComDrift, (Nudged.State.BodyTM.TransformPositionNoScale(Nudged.MassProperties.CenterOfMass) - ComWorld0).Size());
						if (Frame == 0 || Frame == Frames - 1)
						{
							for (uint32 Index = 0; Index < 3; ++Index)
							{
								const FVector W = Run->Body(Index).State.AngularVelocityInRadians;
								const double Rel = (W - Expected[Index]).Size() / Expected[Index].Size();
								Report.Check(Rel <= RelTol, Format("S02 body %u frame %d: w (%.9g, %.9g, %.9g) rad/s vs (%.9g, %.9g, %.9g), rel %.3g > %.3g", Index,
									Frame + 1, W.X, W.Y, W.Z, Expected[Index].X, Expected[Index].Y, Expected[Index].Z, Rel, RelTol));
								if (Frame == Frames - 1)
								{
									Report.Note(Format("S02 body %u: |w| %.9g rad/s, expected %.9g (rel %.2g)", Index, W.Size(), Expected[Index].Size(), Rel));
								}
							}
							// No linear motion from a pure angular impulse (the COM velocity of a dynamic body).
							const double Linear = Run->Body(NudgedBody).State.LinearVelocity.Size();
							Report.Check(Linear <= ComSpeedTol, Format("S02 body 2 frame %d: COM velocity %.3g cm/s from an angular impulse (> %.3g)", Frame + 1, Linear,
								ComSpeedTol));
						}
					}
					const double DriftTol = ToleranceNoted(Target, Report, "G2", "com_drift_cm");
					Report.Check(MaxComDrift <= DriftTol, Format("S02 body 2: the COM moved %.3g cm while spinning (> %.3g): the shape is not placed around the COM", MaxComDrift, DriftTol));
					Report.Note(Format("S02 body 2: COM drift %.2g cm over %d frames (spinning about the nudged COM)", MaxComDrift, Frames));
					FinishGateScene(*Run, State);
				}
				// S03.CapsuleRest
				{
					using namespace GateScenes::S03;
					std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
					if (!Declares(*Run->Scene, PT_CAP_COLLISION))
					{
						PartSkipAllowed(Target, Report, "G2", "S03 skipped: the backend declares no collision (capsule rest heights need contacts)");
						return Report;
					}
					for (int32 Frame = 0; Frame < Frames; ++Frame)
					{
						TickGateScene(*Run, State);
					}
					const double HeightTol = ToleranceNoted(Target, Report, "G2", "capsule_height_abs_cm");
					const double StandingAxisMin = ToleranceNoted(Target, Report, "G2", "capsule_axis_standing_min");
					const double LyingAxisMax = ToleranceNoted(Target, Report, "G2", "capsule_axis_lying_max");
					const double Standing = Run->Body(StandingBody).State.BodyTM.GetTranslation().Z;
					const double Lying = Run->Body(LyingBody).State.BodyTM.GetTranslation().Z;
					const double StandingAxisZ = Run->Body(StandingBody).State.BodyTM.GetRotation().GetAxisZ().Z;
					const double LyingAxisZ = std::abs(Run->Body(LyingBody).State.BodyTM.GetRotation().GetAxisZ().Z);
					Report.Check(std::abs(Standing - StandingZ) <= HeightTol, Format("S03 standing capsule centre z %.4f cm, expected %.1f +- %.2g", Standing, StandingZ, HeightTol));
					Report.Check(std::abs(Lying - LyingZ) <= HeightTol, Format("S03 lying capsule centre z %.4f cm, expected %.1f +- %.2g", Lying, LyingZ, HeightTol));
					Report.Check(StandingAxisZ > StandingAxisMin, Format("S03 standing capsule tipped over (axis . Z = %.6f <= %.6g)", StandingAxisZ, StandingAxisMin));
					Report.Check(LyingAxisZ < LyingAxisMax, Format("S03 lying capsule tilted (|axis . Z| = %.6f >= %.3g)", LyingAxisZ, LyingAxisMax));
					Report.Note(Format("S03 after %d frames: standing z %.4f cm (axis.Z %.6f), lying z %.4f cm", Frames, Standing, StandingAxisZ, Lying));
					FinishGateScene(*Run, State);
				}
				return Report;
			}
		};

		// ---- G3 -----------------------------------------------------------------------------------------------------
		class FGateG3 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G3"; }
			const char* GetDescription() const override
			{
				return "S04.ContactNormal (viz): step-1 contact normals, Phi < 0, points on both surfaces, the v1.0 flag rule; UEParity: a wake drops kept points";
			}
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				using namespace GateScenes::S04;
				FGateReport Report;
				const EPhysicsRunMode Viz = EPhysicsRunMode::Viz;   // the contact listener exists in viz runs (or with PT_INSTR_CONTACTS, 02 9.4)
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {}, &Viz);
				if (!Declares(*Run->Scene, PT_CAP_CONTACT_POINTS))
				{
					// Allowed only where tolerances.json registers it (Null); RunPhysicsGates fails any other Skip.
					Report.Result = EGateResult::Skip;
					Report.Note("skipped: the backend declares no contact points (PT_CAP_CONTACT_POINTS)");
					return Report;
				}
				// docs/Abi.md 9.1 decision 2: ACTIVE needs exact solver results (PT_CAP_CONTACT_ACTIVE_EXACT), ACTIVE_APPROX is the
				// Phi <= 0 stand-in of a backend without them.
				const bool bExact = Declares(*Run->Scene, PT_CAP_CONTACT_ACTIVE_EXACT);
				const bool bHasNormalImpulse = Declares(*Run->Scene, PT_CAP_CONTACT_NORMAL_IMPULSE);
				// The two poses step 1's narrow phase can use (docs/Abi.md 9.1 decision 3: PointOn0 / PointOn1 are at the pose the
				// step's narrow phase used). Jolt, Bullet and PhysX collide the start-of-step pose. Chaos integrates first and
				// collides the predicted pose P = X + V dt, Q (PBDRigidsEvolutionGBF.cpp:581 Integrate before :650 RunNarrowPhase;
				// ShapeInstance.cpp:460-462, :548-556 GetLeafWorldTransform = GetTransformPQ [V]): 0.27 cm (g dt^2) lower here.
				// The S04 bodies start at rest with damping 0, so the predicted pose is X0 + (v0 + g dt) dt with the start rotation.
				constexpr uint32 NumBodies = 4;
				FTransform StartPoses[NumBodies];
				FTransform PredictedPoses[NumBodies];
				const FVector Gravity = Run->Scene->GetGravity();
				for (uint32 Index = 0; Index < NumBodies; ++Index)
				{
					const FBodyInstance& Body = Run->Body(Index);
					StartPoses[Index] = Body.State.BodyTM;
					PredictedPoses[Index] = Body.State.BodyTM;
					if (Index != 0)
					{
						Report.Check(Body.State.AngularVelocityInRadians.IsNearlyZero(1e-12), "S04 bodies start without spin (the predicted pose keeps the start rotation)");
						const FVector V1 = Body.State.LinearVelocity + Gravity * Run->DtEngine;
						PredictedPoses[Index].SetTranslation(Body.State.BodyTM.GetTranslation() + V1 * Run->DtEngine);
					}
				}
				TickGateScene(*Run, State);
				FPhysicsDebugSnapshot Snapshot;
				if (!Report.Check(Run->Scene->CaptureDebugSnapshot(Snapshot), "CaptureDebugSnapshot failed"))
				{
					return Report;
				}
				const double NormalMin = Tolerance(Target, Report, "G3", "normal_z_min");
				const double SurfaceTol = ToleranceNoted(Target, Report, "G3", "point_on_surface_cm");
				const double ImpulseEps = Tolerance(Target, Report, "G3", "normal_impulse_eps");
				const char* const Names[NumBodies] = { "ground", "box", "sphere", "capsule" };
				struct FPairStats
				{
					int32 Points = 0;
					double MinNormalZ = 2.0;
					double MaxPhi = -1e30;
					double MinPhi = 1e30;
					double WorstOn1 = 0.0;
					double WorstOn0Start = 0.0;       // PointOn0 off the body at the start-of-step pose
					double WorstOn0Predicted = 0.0;   // ... at the predicted pose
					double WorstPhiConsistency = 0.0;
					// The v1.0 flag rule, counted over the pair's points.
					int32 NumMultiple = 0;               // more than one of ACTIVE / ACTIVE_APPROX / SPECULATIVE
					int32 NumSpeculativeDeep = 0;        // SPECULATIVE with Phi <= 0
					int32 NumWrongKind = 0;              // ACTIVE without exact results, or ACTIVE_APPROX with them
					int32 NumMissingApprox = 0;          // no exact results: Phi <= 0 without ACTIVE_APPROX
					int32 NumImpulseNotActive = 0;       // exact results: a normal impulse > eps without ACTIVE
					int32 NumActiveNegativeImpulse = 0;  // exact results: ACTIVE with a normal impulse < -eps
					int32 NumActive = 0;
					uint32 FlagsSeen = 0;
				};
				FPairStats Pairs[NumBodies];
				for (const uint32 Body : { BoxBody, SphereBody, CapsuleBody })
				{
					FPairStats& Pair = Pairs[Body];
					for (const PtContactPoint& Contact : Snapshot.Contacts)
					{
						const bool bPair = (Contact.Body0 == Body && Contact.Body1 == 0u) || (Contact.Body0 == 0u && Contact.Body1 == Body);
						if (!bPair)
						{
							continue;
						}
						++Pair.Points;
						Report.Check(Contact.Body0 == Body && Contact.Body1 == 0u, Format("%s: Body0 must be the more mobile body (Body0 %u, Body1 %u)", Names[Body],
							Contact.Body0, Contact.Body1));
						const FVector N(Contact.Normal.X, Contact.Normal.Y, Contact.Normal.Z);
						const FVector P0(Contact.PointOn0.X, Contact.PointOn0.Y, Contact.PointOn0.Z);
						const FVector P1(Contact.PointOn1.X, Contact.PointOn1.Y, Contact.PointOn1.Z);
						Pair.MinNormalZ = std::min(Pair.MinNormalZ, N.Z);
						Pair.MaxPhi = std::max(Pair.MaxPhi, Contact.Phi);
						Pair.MinPhi = std::min(Pair.MinPhi, Contact.Phi);
						// PointOn1 on the ground (Body1, static: its top face is z = 0) and PointOn0 on the body at the narrow-phase
						// pose; Phi = (PointOn0 - PointOn1) . n (docs/Abi.md 7.1, contract (c)).
						Pair.WorstOn1 = std::max(Pair.WorstOn1, std::abs(BodySignedDistance(Run->Body(0), StartPoses[0], P1)));
						Pair.WorstOn0Start = std::max(Pair.WorstOn0Start, std::abs(BodySignedDistance(Run->Body(Body), StartPoses[Body], P0)));
						Pair.WorstOn0Predicted = std::max(Pair.WorstOn0Predicted, std::abs(BodySignedDistance(Run->Body(Body), PredictedPoses[Body], P0)));
						Pair.WorstPhiConsistency = std::max(Pair.WorstPhiConsistency, std::abs(FVector::DotProduct(P0 - P1, N) - Contact.Phi));
						const uint32 Exclusive = Contact.Flags & (PT_CONTACT_ACTIVE | PT_CONTACT_ACTIVE_APPROX | PT_CONTACT_SPECULATIVE);
						Pair.FlagsSeen |= Contact.Flags;
						Pair.NumMultiple += (Exclusive & (Exclusive - 1u)) != 0u ? 1 : 0;
						Pair.NumSpeculativeDeep += (Contact.Flags & PT_CONTACT_SPECULATIVE) != 0u && Contact.Phi <= 0.0 ? 1 : 0;
						const bool bActive = (Contact.Flags & PT_CONTACT_ACTIVE) != 0u;
						Pair.NumActive += bActive ? 1 : 0;
						if (bExact)
						{
							Pair.NumWrongKind += (Contact.Flags & PT_CONTACT_ACTIVE_APPROX) != 0u ? 1 : 0;
							if (bHasNormalImpulse && std::isfinite(Contact.NormalImpulse))
							{
								// One way only (M6 round 2): a point the solver pushed on is ACTIVE, and an ACTIVE point's impulse is not
								// negative. ACTIVE with a zero impulse is allowed: Chaos bIsActive counts friction and a push-out whose
								// velocity the solve cancelled (PBDCollisionSolver.h:1178-1196 [V]).
								Pair.NumImpulseNotActive += !bActive && Contact.NormalImpulse > ImpulseEps ? 1 : 0;
								Pair.NumActiveNegativeImpulse += bActive && Contact.NormalImpulse < -ImpulseEps ? 1 : 0;
							}
						}
						else
						{
							Pair.NumWrongKind += bActive ? 1 : 0;
							Pair.NumMissingApprox += Contact.Phi <= 0.0 && (Contact.Flags & PT_CONTACT_ACTIVE_APPROX) == 0u ? 1 : 0;
						}
					}
				}
				// One narrow-phase pose for the whole scene: the one under which the worst PointOn0 of the three bodies is nearer
				// (an engine does not mix them). The two are 0.27 cm apart, five times the surface tolerance.
				double WorstStart = 0.0;
				double WorstPredicted = 0.0;
				for (const uint32 Body : { BoxBody, SphereBody, CapsuleBody })
				{
					WorstStart = std::max(WorstStart, Pairs[Body].WorstOn0Start);
					WorstPredicted = std::max(WorstPredicted, Pairs[Body].WorstOn0Predicted);
				}
				const bool bPredictedPose = WorstPredicted < WorstStart;
				Report.Note(Format("narrow-phase pose of step 1: %s (worst PointOn0 off the body %.2g cm at the start-of-step pose, %.2g cm at the predicted pose "
					"X0 + (v0 + g dt) dt)", bPredictedPose ? "the predicted pose (Chaos convention)" : "the start-of-step pose", WorstStart, WorstPredicted));
				// M7 (M6 review): the pose is declared, not only inferred. The optional effective key contact.point_pose (0 = the
				// start-of-step pose, 1 = the predicted pose; COMPILE_TIME) tells contacts.ptcn readers, the viewer and the M9
				// analysis which convention the points follow; when a backend declares it, the inferred pose must agree. Optional
				// because a generic required key added after the v1.0 freeze would fail A7 for a v1.0 module (docs/Abi.md 5.14).
				{
					const std::vector<FEffectiveSetting> Settings = ReadEffective(*Run->Scene, Report);
					const FEffectiveSetting* Declared = FindSetting(Settings, "contact.point_pose");
					if (Declared == nullptr)
					{
						Report.Note("contact.point_pose: not declared (optional key; the inferred pose above is the only record)");
					}
					else if (Report.Check(Declared->bHasCanonical && (Declared->Canonical == 0.0 || Declared->Canonical == 1.0),
						"contact.point_pose must be 0 (start-of-step) or 1 (predicted): '" + Declared->Native + "'"))
					{
						const bool bDeclaredPredicted = Declared->Canonical == 1.0;
						Report.Check(bDeclaredPredicted == bPredictedPose, Format("contact.point_pose declares the %s pose, the points follow the %s pose",
							bDeclaredPredicted ? "predicted" : "start-of-step", bPredictedPose ? "predicted" : "start-of-step"));
						Report.Note(Format("contact.point_pose declared %d (%s), matches the points", bDeclaredPredicted ? 1 : 0, bDeclaredPredicted ? "predicted" : "start-of-step"));
					}
				}
				for (const uint32 Body : { BoxBody, SphereBody, CapsuleBody })
				{
					const FPairStats& Pair = Pairs[Body];
					if (!Report.Check(Pair.Points > 0, Format("%s: no contact with the ground at step 1", Names[Body])))
					{
						continue;
					}
					const double WorstOn0 = bPredictedPose ? Pair.WorstOn0Predicted : Pair.WorstOn0Start;
					Report.Check(Pair.MinNormalZ > NormalMin, Format("%s: normal . (+Z) min %.7f <= %.4f", Names[Body], Pair.MinNormalZ, NormalMin));
					Report.Check(Pair.MaxPhi < 0.0, Format("%s: Phi max %.5f cm at step 1 (expected < 0: the body starts %.1f cm deep)", Names[Body], Pair.MaxPhi, Penetration));
					Report.Check(Pair.WorstOn1 <= SurfaceTol, Format("%s: PointOn1 is %.4f cm off the ground surface (> %.3g)", Names[Body], Pair.WorstOn1, SurfaceTol));
					Report.Check(WorstOn0 <= SurfaceTol, Format("%s: PointOn0 is %.4f cm off the body surface at the %s pose (> %.3g)", Names[Body], WorstOn0,
						bPredictedPose ? "predicted" : "start-of-step", SurfaceTol));
					Report.Check(Pair.WorstPhiConsistency <= SurfaceTol, Format("%s: (PointOn0 - PointOn1) . n differs from Phi by %.4f cm (> %.3g)", Names[Body],
						Pair.WorstPhiConsistency, SurfaceTol));
					Report.Check(Pair.NumMultiple == 0, Format("%s: %d of %d points carry more than one of ACTIVE / ACTIVE_APPROX / SPECULATIVE (docs/Abi.md 9.1 decision 2)",
						Names[Body], Pair.NumMultiple, Pair.Points));
					Report.Check(Pair.NumSpeculativeDeep == 0, Format("%s: %d points are SPECULATIVE with Phi <= 0 (SPECULATIVE = Phi > 0 and not active)", Names[Body],
						Pair.NumSpeculativeDeep));
					Report.Check(Pair.NumWrongKind == 0, Format("%s: %d points use %s (the backend %s PT_CAP_CONTACT_ACTIVE_EXACT)", Names[Body], Pair.NumWrongKind,
						bExact ? "ACTIVE_APPROX" : "ACTIVE", bExact ? "declares" : "does not declare"));
					if (bExact)
					{
						// The body starts 1 cm deep under gravity: the solver must push on at least one of its points in step 1.
						Report.Check(Pair.NumActive > 0, Format("%s: no ACTIVE point at step 1 although the body starts %.1f cm deep", Names[Body], Penetration));
						Report.Check(Pair.NumImpulseNotActive == 0, Format("%s: %d points with a normal impulse > %.3g kg cm/s are not ACTIVE (the solver pushed on them)",
							Names[Body], Pair.NumImpulseNotActive, ImpulseEps));
						Report.Check(Pair.NumActiveNegativeImpulse == 0, Format("%s: %d ACTIVE points have a normal impulse < -%.3g kg cm/s (NormalImpulse includes "
							"push-out, docs/Abi.md 7.1)", Names[Body], Pair.NumActiveNegativeImpulse, ImpulseEps));
					}
					else
					{
						Report.Check(Pair.NumMissingApprox == 0, Format("%s: %d penetrating points without ACTIVE_APPROX", Names[Body], Pair.NumMissingApprox));
					}
					Report.Note(Format("%s: %d points, normal.z >= %.7f, Phi [%.5f, %.5f] cm, PointOn1 off ground %.2g cm, PointOn0 off body %.2g cm (%s pose), flags "
						"seen 0x%x (%d ACTIVE, %s)", Names[Body], Pair.Points, Pair.MinNormalZ, Pair.MinPhi, Pair.MaxPhi, Pair.WorstOn1, WorstOn0,
						bPredictedPose ? "predicted" : "start-of-step", Pair.FlagsSeen, Pair.NumActive, bExact ? "exact" : "approximate"));
				}
				CheckWakeDropsKeptPoints(Target, Report, *Run, State);
				FinishGateScene(*Run, State);
				return Report;
			}

		private:
			// docs/Abi.md 9.1 decision 2: a command that wakes a body of a sleeping pair drops that pair's kept (SLEEPING) points
			// until the next step, which reports the pair afresh (SdkBackend.WakeDropsKeptContacts on the gate scene, so that
			// a DLL backend - Chaos - is held to it too). Needs sleeping: UEParity only.
			static void CheckWakeDropsKeptPoints(const FGateTarget& Target, FGateReport& Report, FGateScene& Run, FGateRunState& State)
			{
				using namespace GateScenes::S04;
				if (!Run.Config->TestSettings.bAllowSleeping)
				{
					Report.Note("wake-drop check: needs sleeping (UEParity); the profile disables it");
					return;
				}
				const int32 RestFrames = static_cast<int32>(ToleranceNoted(Target, Report, "G3", "wake_rest_frames"));
				while (Run.Frames < RestFrames)
				{
					TickGateScene(Run, State);
				}
				FBodyInstance& Box = Run.Body(BoxBody);
				Box.PutInstanceToSleep();
				TickGateScene(Run, State);
				TickGateScene(Run, State);
				const auto Count = [&Run, &Report](int32& OutSleeping, int32& OutMultiFlag)
				{
					FPhysicsDebugSnapshot Snapshot;
					Report.Check(Run.Scene->CaptureDebugSnapshot(Snapshot), "CaptureDebugSnapshot failed");
					return CountPairPoints(Snapshot, BoxBody, 0u, OutSleeping, OutMultiFlag);
				};
				int32 Sleeping = 0;
				int32 MultiFlag = 0;
				const int32 Kept = Count(Sleeping, MultiFlag);
				Report.Check(Box.State.ObjectState == EObjectStateType::Sleeping, Format("wake-drop: the box is in state %d after PUT_TO_SLEEP and two steps",
					static_cast<int32>(Box.State.ObjectState)));
				Report.Check(Kept > 0, "wake-drop: the box put to sleep keeps no contact points (a sleeping island's contacts are kept with PT_CONTACT_SLEEPING, "
					"docs/Abi.md 7.1)");
				Report.Check(Sleeping == Kept, Format("wake-drop: %d of the box's %d kept points carry PT_CONTACT_SLEEPING", Sleeping, Kept));
				Report.Check(MultiFlag == 0, Format("wake-drop: %d kept points carry more than one of ACTIVE / ACTIVE_APPROX / SPECULATIVE", MultiFlag));
				Box.WakeInstance();
				Report.Check(Run.Scene->GetBackendScene().FlushCommands() == PT_OK, "wake-drop: the WAKE_UP command was rejected");   // no step yet
				const int32 AfterWake = Count(Sleeping, MultiFlag);
				Report.Check(AfterWake == 0, Format("wake-drop: %d kept points of the woken box remain before its next step (decision 2 drops them)", AfterWake));
				TickGateScene(Run, State);
				const int32 Fresh = Count(Sleeping, MultiFlag);
				Report.Check(Fresh > 0, "wake-drop: the woken box's pair is not reported after its next step");
				Report.Check(Sleeping == 0, Format("wake-drop: %d of the %d points after the step still carry PT_CONTACT_SLEEPING", Sleeping, Fresh));
				Report.Note(Format("wake-drop (frame %d): box kept %d sleeping points; after WAKE_UP before a step %d; after the step %d (%d sleeping)", RestFrames, Kept,
					AfterWake, Fresh, Sleeping));
			}
		};

		// ---- G4 -----------------------------------------------------------------------------------------------------
		class FGateG4 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G4"; }
			const char* GetDescription() const override
			{
				return "S05.RestitutionCombine: e from the normal velocity around the contact step, under the engine's registered restitution velocity model";
			}
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				using namespace GateScenes::S05;
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
				if (!Declares(*Run->Scene, PT_CAP_COLLISION))
				{
					Report.Result = EGateResult::Skip;   // allowed only where tolerances.json registers it (RunPhysicsGates)
					Report.Note("skipped: the backend declares no collision");
					return Report;
				}
				const double G = -Run->Scene->GetGravity().Z;
				const double H0 = Analytic::StepBoundaryDropHeight(G, Run->DtEngine, TouchStep) + TouchGap;   // the scene's drop height (version 2)
				const uint32 BodiesToCheck[3] = { AverageBody, MultiplyBody, MaxBody };
				const double ExpectedE[3] = { 0.5 * Restitution, 0.0, Restitution };
				const char* const Names[3] = { "Average", "Multiply", "Max" };
				std::vector<double> Vz[3];
				std::vector<double> Z[3];
				for (uint32 Slot = 0; Slot < 3; ++Slot)
				{
					Vz[Slot].push_back(Run->Body(BodiesToCheck[Slot]).State.LinearVelocity.Z);
					Z[Slot].push_back(Run->Body(BodiesToCheck[Slot]).State.BodyTM.GetTranslation().Z);
				}
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(*Run, State);
					for (uint32 Slot = 0; Slot < 3; ++Slot)
					{
						Vz[Slot].push_back(Run->Body(BodiesToCheck[Slot]).State.LinearVelocity.Z);
						Z[Slot].push_back(Run->Body(BodiesToCheck[Slot]).State.BodyTM.GetTranslation().Z);
					}
				}
				const double ETol = ToleranceNoted(Target, Report, "G4", "e_abs");
				const double ApexTol = ToleranceNoted(Target, Report, "G4", "apex_ratio_abs");
				const double FrameSlack = ToleranceNoted(Target, Report, "G4", "contact_frame_slack");
				const double DetectAbs = ToleranceNoted(Target, Report, "G4", "contact_detect_cms");
				const double DetectRel = ToleranceNoted(Target, Report, "G4", "contact_detect_rel");
				// Which normal velocity the engine applies -e to (tolerances.json G4.restitution_velocity_model): 0 = the velocity
				// before the contact step, |v_before| (Jolt); 1 = the contact step's pre-solve velocity with that step's gravity in
				// it, |v_before| + g dt (PhysX: DyDynamics.cpp:1979, DyContactPrepShared.h:305-323 [V]); 2 = the contact state at
				// the contact step's start (Bullet, M9, btSequentialImpulseConstraintSolver.cpp:944-972 [V]): a gap g > 0 there
				// takes g / dt off the rebound, e |v_before| - g / dt (S05's TouchGap 0.01 cm: 0.79903 for e 0.8). The model
				// predicts the measured e = -v_after / v_before exactly; the other model (1 for 0, 0 for 1 and 2) is 0.021 away
				// for e 0.8 (model 1) or 0.00097 (model 2, the gap term).
				const int32 VelocityModel = static_cast<int32>(ToleranceNoted(Target, Report, "G4", "restitution_velocity_model"));
				if (!Report.Check(VelocityModel >= 0 && VelocityModel <= 2, Format("G4.restitution_velocity_model %d is not 0, 1 or 2", VelocityModel)))
				{
					return Report;
				}
				const int32 OtherModel = VelocityModel == 0 ? 1 : 0;
				const double GDt = G * Run->DtEngine;
				for (uint32 Slot = 0; Slot < 3; ++Slot)
				{
					// The contact frame: the first frame whose velocity left free fall (by more than contact_detect_cms or _rel).
					int32 Contact = -1;
					for (int32 Frame = 1; Frame <= Frames; ++Frame)
					{
						const double FreeFall = Vz[Slot][Frame - 1] - GDt;
						if (std::abs(Vz[Slot][Frame] - FreeFall) > std::max(DetectAbs, DetectRel * std::abs(FreeFall)))
						{
							Contact = Frame;
							break;
						}
					}
					if (!Report.Check(Contact > 0, Format("%s: the ball never touched the ground in %d frames", Names[Slot], Frames)))
					{
						continue;
					}
					// 03 M6 / 02 14.3: e = -v_after / v_before with the normal velocities of the frames just before and just after the
					// contact (v_before = the last free-fall frame, 620.67 cm/s at the step boundary).
					const double VIn = Vz[Slot][Contact - 1];
					const double VOut = Vz[Slot][Contact];
					const double E = -VOut / VIn;
					const double Speed = std::abs(VIn);
					// The gap at the contact step's start: the lowest point of the ball in the last free-fall frame (model 2).
					const double Gap = Z[Slot][Contact - 1] - Radius;
					if (VelocityModel == 2 && !Report.Check(Gap > 0.0, Format("%s: restitution model 2 needs the contact step to start with a gap (S05's "
						"TouchGap); the ball is %.4g cm above the ground in frame %d", Names[Slot], Gap, Contact - 1)))
					{
						continue;
					}
					const auto ModelVOut = [&](int32 Model)
					{
						return Model == 1 ? ExpectedE[Slot] * (Speed + GDt) : Model == 2 ? ExpectedE[Slot] * Speed - Gap / Run->DtEngine : ExpectedE[Slot] * Speed;
					};
					const double ExpectedVOut = ModelVOut(VelocityModel);
					const double ExpectedMeasuredE = ExpectedVOut / Speed;
					const double OtherMeasuredE = ModelVOut(OtherModel) / Speed;
					Report.Check(std::abs(E - ExpectedMeasuredE) <= ETol, Format("%s: e = -v_after / v_before = -(%.4f) / (%.4f) = %.5f, model %d expects %.5f (e %.3f) "
						"+- %.3g", Names[Slot], VOut, VIn, E, VelocityModel, ExpectedMeasuredE, ExpectedE[Slot], ETol));
					if (std::abs(ExpectedMeasuredE - OtherMeasuredE) > 1e-9)
					{
						// The band alone could hold both models (Average: 0.4000 vs 0.4105); the registered one must be the nearer.
						Report.Check(std::abs(E - ExpectedMeasuredE) < std::abs(E - OtherMeasuredE), Format("%s: e %.5f is closer to the other restitution velocity model "
							"(%.5f) than to the registered model %d (%.5f)", Names[Slot], E, OtherMeasuredE, VelocityModel, ExpectedMeasuredE));
					}
					Report.Check(std::abs(Contact - (TouchStep + 1)) <= FrameSlack, Format("%s: contact at frame %d, the discrete trajectory ends step %d %.2g cm "
						"above the ground (contact step %d +- %.0f)", Names[Slot], Contact, TouchStep, TouchGap, TouchStep + 1, FrameSlack));
					// Secondary: the apex after the bounce against the discrete flight from the height the contact step left the ball
					// at (h_c: v_after dt for an engine that moves the ball by its new velocity in that step - Jolt, Bullet, PhysX PGS -
					// and about 0 for one that resolves the contact inside the step - PhysX TGS) with the model's v_after: step n after
					// the contact step is at h_c + v_after n dt - g dt^2 n (n + 1) / 2.
					double Apex = 0.0;
					for (int32 Frame = Contact; Frame <= Frames; ++Frame)
					{
						Apex = std::max(Apex, Z[Slot][Frame] - Radius);
					}
					const double ContactStepHeight = Z[Slot][Contact] - Radius;
					double ApexRef = ContactStepHeight;
					for (int32 N = 1; N < 400; ++N)
					{
						ApexRef = std::max(ApexRef, ContactStepHeight + ExpectedVOut * N * Run->DtEngine - G * Run->DtEngine * Run->DtEngine * N * (N + 1) * 0.5);
					}
					const double Ratio = Apex / H0;
					const double RatioRef = ApexRef / H0;
					Report.Check(std::abs(Ratio - RatioRef) <= ApexTol, Format("%s: apex ratio %.4f, discrete reference %.4f (v_after %.3f cm/s, h_c %.4f cm) +- %.3g",
						Names[Slot], Ratio, RatioRef, ExpectedVOut, ContactStepHeight, ApexTol));
					Report.Note(Format("%s: contact frame %d, v_before %.3f, v_after %.3f cm/s, e %.5f (model %d: %.5f, other model %.5f); apex ratio %.4f (discrete %.4f "
						"from h_c %.4f cm, continuous e^2 %.2f)", Names[Slot], Contact, VIn, VOut, E, VelocityModel, ExpectedMeasuredE, OtherMeasuredE, Ratio, RatioRef,
						ContactStepHeight, ExpectedE[Slot] * ExpectedE[Slot]));
				}
				Report.Note(Format("drop height %.4f cm (K %d + %.2g cm gap), impact speed %.3f cm/s at the end of step %d", H0, TouchStep, TouchGap, GDt * TouchStep,
					TouchStep));
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G5 -----------------------------------------------------------------------------------------------------
		class FGateG5 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G5"; }
			const char* GetDescription() const override { return "S06.FrictionSlide: discrete stop distance, 30 deg incline hold / slide"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				using namespace GateScenes::S06;
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
				if (!Declares(*Run->Scene, PT_CAP_COLLISION))
				{
					Report.Result = EGateResult::Skip;   // allowed only where tolerances.json registers it (RunPhysicsGates)
					Report.Note("skipped: the backend declares no collision");
					return Report;
				}
				const double G = -Run->Scene->GetGravity().Z;
				const FVector Start = Run->Body(SlideBody).State.BodyTM.GetTranslation();
				const FQuat Pitch(FRotator(SlopeDegrees, 0.0, 0.0));
				const FVector DownSlope = -Pitch.GetAxisX();
				const FVector SlopeNormal = Pitch.GetAxisZ();
				// Length along the slope surface (a creep moves the box in the surface plane; the normal part is the contact's push).
				const auto AlongSurface = [&SlopeNormal](const FVector& V) { return (V - SlopeNormal * FVector::DotProduct(V, SlopeNormal)).Size(); };
				const FVector HoldStart = Run->Body(HoldBody).State.BodyTM.GetTranslation();
				constexpr int32 CreepWindow = 10;   // frames: the creep speed is the hold box's surface displacement over this window
				static_assert(SlopeCheckFrame >= CreepWindow && Frames >= CreepWindow, "the creep windows end at frames 20 and 120");
				std::vector<FVector> HoldPositions = { HoldStart };
				const FVector SlideStart = Run->Body(SlopeSlideBody).State.BodyTM.GetTranslation();
				const double SlopeRad = FMath::DegreesToRadians(SlopeDegrees);
				const double SlideAccel = G * (std::sin(SlopeRad) - SlideFriction * std::cos(SlopeRad));
				const double HoldTol = ToleranceNoted(Target, Report, "G5", "hold_displacement_cm");
				const double HoldAccelTol = ToleranceNoted(Target, Report, "G5", "hold_acceleration_cms2");
				const double HoldSpeedTol = ToleranceNoted(Target, Report, "G5", "hold_speed_cms");
				double HoldSpeedHalf = 0.0;
				double SlideSpeedHalf = 0.0;
				const double SlideLow = ToleranceNoted(Target, Report, "G5", "slide_fraction_min");
				const double SlideHigh = ToleranceNoted(Target, Report, "G5", "slide_fraction_max");
				std::string HoldTrace = "incline mu 0.60 down-slope displacement (cm) at frames";
				std::string SlideTrace = "incline mu 0.55 down-slope displacement (cm) at frames";
				std::string HoldSpeedTrace = "incline mu 0.60 reported speed along the slope (cm/s) at frames";
				for (int32 Frame = 1; Frame <= Frames; ++Frame)
				{
					TickGateScene(*Run, State);
					HoldPositions.push_back(Run->Body(HoldBody).State.BodyTM.GetTranslation());
					if (Frame == 1 || Frame == 5 || Frame == 10 || Frame == 20 || Frame == 40 || Frame == 60 || Frame == 120)
					{
						HoldTrace += Format(" %d:%.4f", Frame, FVector::DotProduct(Run->Body(HoldBody).State.BodyTM.GetTranslation() - HoldStart, DownSlope));
						SlideTrace += Format(" %d:%.4f", Frame, FVector::DotProduct(Run->Body(SlopeSlideBody).State.BodyTM.GetTranslation() - SlideStart, DownSlope));
						HoldSpeedTrace += Format(" %d:%.4f", Frame, AlongSurface(Run->Body(HoldBody).State.LinearVelocity));
					}
					if (Frame == SlopeCheckFrame || Frame == Frames)
					{
						// Holding is also no motion: a box that keeps creeping at a constant speed does not accelerate, yet does not hold
						// (PhysX PGS, tolerances.json G5.hold_speed_cms), and a creep that grows must fail. The speed comes from the
						// positions (the surface displacement over the last CreepWindow frames), not from the reported velocity: PhysX
						// TGS reports a resting box's velocity with a 0.5 cm/s residual that does not move it.
						const double HoldSpeed = AlongSurface(HoldPositions[Frame] - HoldPositions[Frame - CreepWindow]) / (CreepWindow * Run->DtEngine);
						const FVector HoldMoved = HoldPositions[Frame] - HoldStart;
						const FVector& V = Run->Body(HoldBody).State.LinearVelocity;
						Report.Check(HoldSpeed <= HoldSpeedTol, Format("incline mu %.2f creeps at %.4f cm/s along the slope over frames %d-%d (> %.3g): it does not hold",
							HoldFriction, HoldSpeed, Frame - CreepWindow, Frame, HoldSpeedTol));
						Report.Note(Format("incline mu %.2f frames %d-%d: creep %.4f cm/s along the slope; at frame %d moved (%.4f, %.4f, %.4f) cm, reported v (%.4f, %.4f, "
							"%.4f) cm/s", HoldFriction, Frame - CreepWindow, Frame, HoldSpeed, Frame, HoldMoved.X, HoldMoved.Y, HoldMoved.Z, V.X, V.Y, V.Z));
					}
					if (Frame == SlopeCheckFrame / 2)
					{
						HoldSpeedHalf = FVector::DotProduct(Run->Body(HoldBody).State.LinearVelocity, DownSlope);
						SlideSpeedHalf = FVector::DotProduct(Run->Body(SlopeSlideBody).State.LinearVelocity, DownSlope);
					}
					if (Frame == SlopeCheckFrame)
					{
						// Holding is static friction cancelling gravity along the slope: no acceleration. Sliding is kinetic friction:
						// a = g (sin 30 - mu cos 30). Both from the down-slope velocity between frames 10 and 20, plus the displacement.
						const double Span = (SlopeCheckFrame - SlopeCheckFrame / 2) * Run->DtEngine;
						const double HoldAccel = (FVector::DotProduct(Run->Body(HoldBody).State.LinearVelocity, DownSlope) - HoldSpeedHalf) / Span;
						const double SlideAccelMeasured = (FVector::DotProduct(Run->Body(SlopeSlideBody).State.LinearVelocity, DownSlope) - SlideSpeedHalf) / Span;
						const double Hold = FVector::DotProduct(Run->Body(HoldBody).State.BodyTM.GetTranslation() - HoldStart, DownSlope);
						const double Slide = FVector::DotProduct(Run->Body(SlopeSlideBody).State.BodyTM.GetTranslation() - SlideStart, DownSlope);
						const double SlideRef = SlideAccel * Run->DtEngine * Run->DtEngine * Frame * (Frame + 1) * 0.5;
						Report.Check(std::abs(HoldAccel) <= HoldAccelTol, Format("incline mu %.2f (>= tan 30 = 0.577) accelerates at %.3f cm/s^2 down the slope "
							"(frames %d-%d, > %.3g): static friction does not hold it", HoldFriction, HoldAccel, SlopeCheckFrame / 2, Frame, HoldAccelTol));
						Report.Check(std::abs(Hold) <= HoldTol, Format("incline mu %.2f moved %.4f cm down the slope by frame %d (> %.3g)", HoldFriction, Hold, Frame, HoldTol));
						Report.Check(SlideAccelMeasured >= SlideLow * SlideAccel && SlideAccelMeasured <= SlideHigh * SlideAccel, Format("incline mu %.2f accelerates at "
							"%.3f cm/s^2, kinetic reference %.3f, accepted [%.2f, %.2f] x reference", SlideFriction, SlideAccelMeasured, SlideAccel, SlideLow, SlideHigh));
						Report.Check(Slide >= SlideLow * SlideRef && Slide <= SlideHigh * SlideRef, Format("incline mu %.2f moved %.4f cm by frame %d; kinetic reference "
							"%.4f cm, accepted [%.2f, %.2f] x reference", SlideFriction, Slide, Frame, SlideRef, SlideLow, SlideHigh));
						Report.Note(Format("incline at frame %d: mu %.2f moved %.4f cm, a %.3f cm/s^2 (holds); mu %.2f moved %.4f cm, a %.3f cm/s^2 (kinetic reference "
							"%.4f cm, %.3f cm/s^2)", Frame, HoldFriction, Hold, HoldAccel, SlideFriction, Slide, SlideAccelMeasured, SlideRef, SlideAccel));
					}
				}
				Report.Note(HoldTrace);
				Report.Note(HoldSpeedTrace);
				Report.Note(SlideTrace);
				const double Distance = Run->Body(SlideBody).State.BodyTM.GetTranslation().X - Start.X;
				const double Speed = Run->Body(SlideBody).State.LinearVelocity.Size();
				const double Reference = Analytic::FrictionSlideStopDistance(V0, Friction, G, Run->DtEngine);
				const double RelTol = ToleranceNoted(Target, Report, "G5", "stop_distance_rel");
				Report.Check(std::abs(Distance - Reference) <= RelTol * Reference, Format("stop distance %.4f cm, discrete reference %.4f cm +- %.1f%% (continuous "
					"v0^2/(2 mu g) %.4f is not the reference)", Distance, Reference, 100.0 * RelTol, V0 * V0 / (2.0 * Friction * G)));
				const double FinalSpeedTol = ToleranceNoted(Target, Report, "G5", "final_speed_cms");
				Report.Check(Speed <= FinalSpeedTol, Format("the slider still moves at %.4f cm/s after %d frames (> %.3g)", Speed, Frames, FinalSpeedTol));
				Report.Note(Format("stop distance %.4f cm (reference %.4f, %+.3f%%), final speed %.3g cm/s", Distance, Reference, 100.0 * (Distance - Reference) / Reference, Speed));
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G6 -----------------------------------------------------------------------------------------------------
		class FGateG6 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G6"; }
			const char* GetDescription() const override
			{
				return "sleep contract: none asleep with sleep off; bStartAwake = false asleep from creation; a velocity write wakes a sleeping body";
			}
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				if (!Target.Config->TestSettings.bAllowSleeping)
				{
					// Sleep off: a resting box for 600 frames never sleeps (PhysX eDISABLE_SLEEPING, Jolt mAllowSleeping false, Bullet
					// DISABLE_DEACTIVATION, Chaos p.Chaos.Solver.Sleep.Enabled=0; DL48).
					constexpr int32 RestFrames = 600;
					std::unique_ptr<FGateScene> Run = MakeGateScene(Target, "M1.SingleBoxDrop", { { "Case", "a" } });
					int32 Asleep = 0;
					int32 FirstAsleep = -1;
					for (int32 Frame = 1; Frame <= RestFrames; ++Frame)
					{
						TickGateScene(*Run, State);
						for (const FBodyInstance* Body : Run->Scene->GetBodies())
						{
							if (Body->State.ObjectState == EObjectStateType::Sleeping)
							{
								++Asleep;
								FirstAsleep = FirstAsleep < 0 ? Frame : FirstAsleep;
							}
						}
					}
					Report.Check(Asleep == 0, Format("sleep off: %d body-frames asleep in %d frames (first at frame %d)", Asleep, RestFrames, FirstAsleep));
					Report.Note(Format("M1.SingleBoxDrop a, %d frames with sleeping off: 0 body-frames asleep required, %d seen; box z %.3f cm", RestFrames, Asleep,
						Run->Body(1).State.BodyTM.GetTranslation().Z));
					FinishGateScene(*Run, State);
					return Report;
				}
				using namespace GateScenes::S08;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
				const EObjectStateType Created = Run->Body(AsleepBody).State.ObjectState;
				const EObjectStateType OtherCreated = Run->Body(AwakeBody).State.ObjectState;
				Report.Check(Created == EObjectStateType::Sleeping, Format("bStartAwake = false body: state %d right after creation, expected Sleeping (%d)",
					static_cast<int32>(Created), static_cast<int32>(EObjectStateType::Sleeping)));
				Report.Check(OtherCreated == EObjectStateType::Dynamic, Format("bStartAwake = true body: state %d right after creation", static_cast<int32>(OtherCreated)));
				const double Z0Asleep = Run->Body(AsleepBody).State.BodyTM.GetTranslation().Z;
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				const FBodyInstance& Asleep = Run->Body(AsleepBody);
				const FBodyInstance& Awake = Run->Body(AwakeBody);
				const double Dz = Asleep.State.BodyTM.GetTranslation().Z - Z0Asleep;
				const double DzTol = ToleranceNoted(Target, Report, "G6", "asleep_dz_cm");
				const double FallMin = ToleranceNoted(Target, Report, "G6", "awake_fall_min_cm");
				Report.Check(Asleep.State.ObjectState == EObjectStateType::Sleeping, Format("bStartAwake = false body: state %d after %d frames, expected Sleeping",
					static_cast<int32>(Asleep.State.ObjectState), Frames));
				Report.Check(std::abs(Dz) <= DzTol, Format("bStartAwake = false body moved %.3g cm in %d frames (> %.3g)", Dz, Frames, DzTol));
				Report.Check(Awake.State.BodyTM.GetTranslation().Z < Z0 - FallMin, Format("the awake body is at z %.3f cm after %d frames: it did not fall %.0f cm",
					Awake.State.BodyTM.GetTranslation().Z, Frames, FallMin));
				Report.Note(Format("S08: asleep body Sleeping at creation and after %d frames, z %.4f cm (moved %.2g); awake body z %.3f cm", Frames,
					Asleep.State.BodyTM.GetTranslation().Z, Dz, Awake.State.BodyTM.GetTranslation().Z));
				CheckVelocityWritesWake(Target, Report, std::move(Run), State);
				return Report;
			}

		private:
			enum class EWakeWrite : uint8
			{
				LinearVelocity,       // SET_LINEAR_VELOCITY (100, 0, 0) cm/s
				ZeroLinearVelocity,   // SET_LINEAR_VELOCITY (0, 0, 0): UE wakes through SetX (SingleParticlePhysicsProxy.h:1217-1229 [V])
				AngularVelocity,      // SET_ANGULAR_VELOCITY (0, 0, 2) rad/s
				BodyState,            // SET_BODY_STATE (pose, (100, 0, 0), 0), PT_CAP_STATE_WRITE backends
				// M6 round 1: the one backend-defined case of decision 1, a nearly zero vector (every component <= 1e-4, UE's
				// FVector::IsNearlyZero), follows the backend's declared command.nearly_zero_velocity_wakes: 1 wakes (Null, the
				// SDK backends), 0 keeps the body asleep (UE / Chaos: SingleParticlePhysicsProxy.h:331 !InV.IsNearlyZero() [V]).
				NearlyZeroLinearVelocity,    // SET_LINEAR_VELOCITY (5e-5, 0, 0) cm/s
				NearlyZeroAngularVelocity,   // SET_ANGULAR_VELOCITY (0, 0, 5e-5) rad/s
			};

			// docs/Abi.md 9.1 decision 1 on the engine (the Chaos DLL runs no SdkBackend.* test): a velocity or state write to a
			// sleeping dynamic body wakes it whatever PT_CMDF_AUTO_WAKE says (the writes here send bAutoWake = false), and the next
			// step runs at the written velocity. Each write goes to the S08 body that has slept since creation, after the 60
			// frames, in its own run (the first reuses the run of the checks above).
			static void CheckVelocityWritesWake(const FGateTarget& Target, FGateReport& Report, std::unique_ptr<FGateScene> FirstRun, FGateRunState& State)
			{
				using namespace GateScenes::S08;
				const double RelTol = ToleranceNoted(Target, Report, "G6", "wake_velocity_rel");
				const double MoveTol = ToleranceNoted(Target, Report, "G6", "wake_displacement_rel");
				std::vector<EWakeWrite> Writes = { EWakeWrite::LinearVelocity, EWakeWrite::ZeroLinearVelocity, EWakeWrite::AngularVelocity };
				if (Declares(*FirstRun->Scene, PT_CAP_STATE_WRITE))
				{
					Writes.push_back(EWakeWrite::BodyState);
				}
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*FirstRun->Scene, Report);
				const FEffectiveSetting* NearlyZero = FindSetting(Settings, "command.nearly_zero_velocity_wakes");
				const bool bNearlyZeroDeclared = Report.Check(NearlyZero != nullptr && NearlyZero->bHasCanonical
						&& (NearlyZero->Canonical == 0.0 || NearlyZero->Canonical == 1.0),
					"command.nearly_zero_velocity_wakes must declare canonical 0 (stays asleep) or 1 (wakes), docs/Abi.md 9.1 decision 1");
				const bool bNearlyZeroWakes = bNearlyZeroDeclared && NearlyZero->Canonical == 1.0;
				if (bNearlyZeroDeclared)
				{
					Writes.push_back(EWakeWrite::NearlyZeroLinearVelocity);
					Writes.push_back(EWakeWrite::NearlyZeroAngularVelocity);
				}
				const double AsleepDzTol = Tolerance(Target, Report, "G6", "asleep_dz_cm");
				std::string Summary = "velocity writes to the sleeping body (no PT_CMDF_AUTO_WAKE):";
				for (const EWakeWrite Write : Writes)
				{
					std::unique_ptr<FGateScene> Run = Write == EWakeWrite::LinearVelocity ? std::move(FirstRun) : MakeGateScene(Target, Name, {});
					while (Run->Frames < Frames)
					{
						TickGateScene(*Run, State);
					}
					FBodyInstance& Body = Run->Body(AsleepBody);
					const char* const What = Write == EWakeWrite::LinearVelocity ? "SET_LINEAR_VELOCITY (100, 0, 0)"
						: Write == EWakeWrite::ZeroLinearVelocity ? "SET_LINEAR_VELOCITY (0, 0, 0)"
						: Write == EWakeWrite::AngularVelocity ? "SET_ANGULAR_VELOCITY (0, 0, 2)"
						: Write == EWakeWrite::BodyState ? "SET_BODY_STATE (v (100, 0, 0))"
						: Write == EWakeWrite::NearlyZeroLinearVelocity ? "SET_LINEAR_VELOCITY (5e-5, 0, 0)" : "SET_ANGULAR_VELOCITY (0, 0, 5e-5)";
					const bool bNearlyZeroWrite = Write == EWakeWrite::NearlyZeroLinearVelocity || Write == EWakeWrite::NearlyZeroAngularVelocity;
					if (!Report.Check(Body.State.ObjectState == EObjectStateType::Sleeping, Format("%s: the body is in state %d before the write, expected Sleeping", What,
						static_cast<int32>(Body.State.ObjectState))))
					{
						continue;
					}
					const FTransform Before = Body.State.BodyTM;
					const FVector Linear(100.0, 0.0, 0.0);
					const FVector Angular(0.0, 0.0, 2.0);
					const double NearlyZeroValue = 5e-5;   // below UE's KINDA_SMALL_NUMBER 1e-4 per component (FVector::IsNearlyZero)
					switch (Write)
					{
					case EWakeWrite::LinearVelocity: FPhysicsInterface::SetLinearVelocity_AssumesLocked(Body.ActorHandle, Linear, false); break;
					case EWakeWrite::ZeroLinearVelocity: FPhysicsInterface::SetLinearVelocity_AssumesLocked(Body.ActorHandle, FVector::ZeroVector, false); break;
					case EWakeWrite::AngularVelocity: FPhysicsInterface::SetAngularVelocity_AssumesLocked(Body.ActorHandle, Angular, false); break;
					case EWakeWrite::BodyState: FPhysicsInterface::SetBodyState_AssumesLocked(Body.ActorHandle, Before, Linear, FVector::ZeroVector); break;
					case EWakeWrite::NearlyZeroLinearVelocity:
						FPhysicsInterface::SetLinearVelocity_AssumesLocked(Body.ActorHandle, FVector(NearlyZeroValue, 0.0, 0.0), false);
						break;
					case EWakeWrite::NearlyZeroAngularVelocity:
						FPhysicsInterface::SetAngularVelocity_AssumesLocked(Body.ActorHandle, FVector(0.0, 0.0, NearlyZeroValue), false);
						break;
					}
					TickGateScene(*Run, State);
					const double Dt = Run->DtEngine;
					const double G = -Run->Scene->GetGravity().Z;
					const FVector V = Body.State.LinearVelocity;
					const FVector W = Body.State.AngularVelocityInRadians;
					const FVector Moved = Body.State.BodyTM.GetTranslation() - Before.GetTranslation();
					if (bNearlyZeroWrite && !bNearlyZeroWakes)
					{
						// Declared 0 (UE's rule): the body stays asleep and is not integrated.
						Report.Check(Body.State.ObjectState == EObjectStateType::Sleeping, Format("%s: the body is in state %d after the write and a step, expected "
							"Sleeping (command.nearly_zero_velocity_wakes = 0)", What, static_cast<int32>(Body.State.ObjectState)));
						Report.Check(Moved.Size() <= AsleepDzTol, Format("%s: the sleeping body moved %.3g cm (> %.3g)", What, Moved.Size(), AsleepDzTol));
						Summary += Format(" %s -> stays asleep (declared 0), moved %.2g;", What, Moved.Size());
						FinishGateScene(*Run, State);
						continue;
					}
					Report.Check(Body.State.ObjectState == EObjectStateType::Dynamic, Format("%s: the body is in state %d after the write and a step, expected Dynamic "
						"(v1.0: a velocity write wakes a sleeping body%s)", What, static_cast<int32>(Body.State.ObjectState),
						bNearlyZeroWrite ? "; command.nearly_zero_velocity_wakes = 1" : ""));
					if (Write == EWakeWrite::AngularVelocity)
					{
						const double Turned = AngleBetween(Before.GetRotation(), Body.State.BodyTM.GetRotation());
						Report.Check(std::abs(W.Z - Angular.Z) <= RelTol * Angular.Z, Format("%s: w.z %.7f rad/s after the step (expected %.1f +- %.2g relative)", What,
							W.Z, Angular.Z, RelTol));
						Report.Check(std::abs(Turned - Angular.Z * Dt) <= MoveTol * Angular.Z * Dt, Format("%s: the step turned the body %.7f rad, |w| dt = %.7f (+- %.2g "
							"relative)", What, Turned, Angular.Z * Dt, MoveTol));
						Summary += Format(" %s -> w.z %.6f, turned %.6f rad;", What, W.Z, Turned);
					}
					else if (Write == EWakeWrite::ZeroLinearVelocity || bNearlyZeroWrite)
					{
						// Woken with no (or a nearly zero) velocity, the body falls: one step of gravity (semi-implicit Euler, damping 0).
						Report.Check(std::abs(V.Z + G * Dt) <= RelTol * G * Dt && std::abs(V.X) <= RelTol * G * Dt, Format("%s: v (%.6f, %.3g, %.6f) cm/s after the "
							"step, expected (0, 0, %.6f) +- %.2g relative: the woken body falls", What, V.X, V.Y, V.Z, -G * Dt, RelTol));
						Report.Check(std::abs(Moved.Z + G * Dt * Dt) <= MoveTol * G * Dt * Dt, Format("%s: the step moved the body %.6f cm in z, expected %.6f (+- %.2g "
							"relative)", What, Moved.Z, -G * Dt * Dt, MoveTol));
						Summary += Format(" %s -> v.z %.6f, dz %.6f;", What, V.Z, Moved.Z);
					}
					else
					{
						Report.Check(std::abs(V.X - Linear.X) <= RelTol * Linear.X, Format("%s: v.x %.6f cm/s after the step (expected %.1f +- %.2g relative)", What, V.X,
							Linear.X, RelTol));
						Report.Check(std::abs(Moved.X - Linear.X * Dt) <= MoveTol * Linear.X * Dt, Format("%s: the step moved the body %.6f cm in x, v dt = %.6f (+- %.2g "
							"relative)", What, Moved.X, Linear.X * Dt, MoveTol));
						Summary += Format(" %s -> v.x %.6f, dx %.6f;", What, V.X, Moved.X);
					}
					FinishGateScene(*Run, State);
				}
				Report.Note(Summary);
			}
		};

		// ---- G7 -----------------------------------------------------------------------------------------------------
		class FGateG7 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G7"; }
			const char* GetDescription() const override { return "S07.ClampProbe: the profile's angular velocity limit (UEParity 62.83 rad/s, Normalized released)"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				using namespace GateScenes::S07;
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, Name, {});
				// The limit each body was created with: the profile's (UE: bOverrideMaxAngularVelocity ? body : project).
				const double Limit = Run->Body(0).GetMaxAngularVelocityInRadians(Run->Config->Settings);
				FQuat Previous[3];
				for (int32 Frame = 1; Frame <= Frames; ++Frame)
				{
					if (Frame == Frames)
					{
						for (uint32 Index = 0; Index < 3; ++Index)
						{
							Previous[Index] = Run->Body(Index).State.BodyTM.GetRotation();
						}
					}
					TickGateScene(*Run, State);
				}
				const double OmegaTol = ToleranceNoted(Target, Report, "G7", "omega_abs_rads");
				const double AngleTol = ToleranceNoted(Target, Report, "G7", "step_angle_abs_rad");
				// How the engine turns a body by w over one step (tolerances.json G7.step_angle_model): 0 the exact rotation |w| dt
				// (Jolt, PhysX); 1 the first-order quaternion update q + dt/2 (w, 0) q, normalised, which turns 2 atan(|w| dt / 2)
				// (Chaos IntegrateRotationWithAngularVelocity, ChaosCore Rotation.cpp:158-162 [V]; Null mirrors it); 2 Bullet's
				// integrateTransform, |w| dt up to pi/4 and 2 atan((|w| dt / (pi/4)) tan(pi/8)) above (btTransformUtil.h:44-72 [V], DL16).
				const int32 AngleModel = static_cast<int32>(ToleranceNoted(Target, Report, "G7", "step_angle_model"));
				for (uint32 Index = 0; Index < 3; ++Index)
				{
					const FBodyInstance& Body = Run->Body(Index);
					const double Expected = std::min(Omega0[Index], Limit);
					const double Omega = Body.State.AngularVelocityInRadians.Z;
					Report.Check(std::abs(Omega - Expected) <= OmegaTol, Format("w0 %.0f rad/s: w %.6f rad/s after %d frames, expected %.6f +- %.3g (limit %.6f)",
						Omega0[Index], Omega, Frames, Expected, OmegaTol, Limit));
					const double StepAngle = AngleBetween(Previous[Index], Body.State.BodyTM.GetRotation());
					const double Theta = std::abs(Omega) * Run->DtEngine;
					const double QuarterPi = 0.25 * 3.14159265358979323846;
					const double ExpectedAngle = AngleModel == 1 ? 2.0 * std::atan(0.5 * Theta)
						: (AngleModel == 2 && Theta > QuarterPi ? 2.0 * std::atan((Theta / QuarterPi) * std::tan(0.5 * QuarterPi)) : Theta);
					Report.Check(std::abs(StepAngle - ExpectedAngle) <= AngleTol, Format("w0 %.0f rad/s: the last step turned %.6f rad, model %d expects %.6f (+- %.3g; "
						"|w| dt = %.6f)", Omega0[Index], StepAngle, AngleModel, ExpectedAngle, AngleTol, Theta));
					Report.Note(Format("w0 %.0f: w %.6f rad/s (expected %.6f), last step %.6f rad (model %d: %.6f, |w| dt %.6f)", Omega0[Index], Omega, Expected, StepAngle,
						AngleModel, ExpectedAngle, Theta));
				}
				Report.Note(Format("angular limit %.6f rad/s (%s)", Limit, Limit > 1e4 ? "released" : "UE project MaxAngularVelocity"));
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G9 -----------------------------------------------------------------------------------------------------
		double ParseNumber(const std::string& Text, bool& bOk)
		{
			const char* Begin = Text.c_str();
			char* End = nullptr;
			const bool bHex = Text.size() > 2 && Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X');
			const double Value = bHex ? static_cast<double>(std::strtoull(Begin, &End, 16)) : std::strtod(Begin, &End);
			bOk = End != Begin && *End == '\0';
			return Value;
		}

		const std::string* FindOption(const std::vector<std::pair<std::string, std::string>>& Options, const std::string& Key)
		{
			for (const auto& [Name, Value] : Options)
			{
				if (ToLowerAscii(Name) == ToLowerAscii(Key))
				{
					return &Value;
				}
			}
			return nullptr;
		}

		double OptionNumber(const std::string& Value, bool& bOk)
		{
			const std::string Lower = ToLowerAscii(Value);
			if (Lower == "true")
			{
				bOk = true;
				return 1.0;
			}
			if (Lower == "false")
			{
				bOk = true;
				return 0.0;
			}
			return ParseNumber(Value, bOk);
		}

		// The resolved value an expectation names. False + OutWhy when it cannot be computed.
		bool ResolveExpectation(const std::string& Expect, const FGateTarget& Target, const FReadbackContext& Run, double& OutValue, std::string& OutWhy)
		{
			const FResolvedPhysicsConfig& Config = *Run.Config;
			bool bOk = false;
			if (Expect.rfind("const:", 0) == 0 || Expect.rfind("min:", 0) == 0)
			{
				OutValue = ParseNumber(Expect.substr(Expect.find(':') + 1), bOk);
				OutWhy = bOk ? "" : "unparsable number in '" + Expect + "'";
				return bOk;
			}
			if (Expect.rfind("option:", 0) == 0)
			{
				const std::string Key = Expect.substr(7);
				const std::string* Value = FindOption(Config.SceneOptions, Key);
				if (Value == nullptr)
				{
					OutWhy = "the variant sets no scene option " + Key;
					return false;
				}
				OutValue = OptionNumber(*Value, bOk);
				OutWhy = bOk ? "" : "option " + Key + " = '" + *Value + "' is not a number";
				return bOk;
			}
			if (Expect.rfind("init:", 0) == 0)
			{
				// init:<Key>:<default> - a module option, or its documented default when the variant states none.
				const size_t Colon = Expect.find(':', 5);
				const std::string Key = Expect.substr(5, Colon == std::string::npos ? std::string::npos : Colon - 5);
				const std::string* Value = FindOption(Config.InitOptions, Key);
				const std::string Text = Value != nullptr ? *Value : (Colon == std::string::npos ? std::string() : Expect.substr(Colon + 1));
				OutValue = OptionNumber(Text, bOk);
				OutWhy = bOk ? "" : "init option " + Key + " has no numeric value";
				return bOk;
			}
			if (Expect.rfind("capacity:", 0) == 0)
			{
				const FScenarioCapacity Capacity = Run.Scenario->GetCapacity(*Run.Params);
				const std::string What = Expect.substr(9);
				OutValue = What == "bodies" ? Capacity.MaxBodies : (What == "pairs" ? Capacity.MaxBodyPairs : Capacity.MaxContactConstraints);
				return true;
			}
			if (Expect == "1000dt")
			{
				OutValue = 1000.0 * Run.DtEngine;   // Chaos' native restitution threshold mapping (DL35, Q11)
				return true;
			}
			if (Expect.rfind("profile:", 0) == 0)
			{
				const std::string Name = Expect.substr(8);
				const FBodyInstance& Body = Config.BodyInstanceDefaults;
				if (Name == "gravity_z") { OutValue = Config.Settings.DefaultGravityZ; return true; }
				if (Name == "linear_damping") { OutValue = Body.LinearDamping; return true; }
				if (Name == "max_angular_velocity") { OutValue = Body.GetMaxAngularVelocityInRadians(Config.Settings); return true; }
				if (Name == "gyroscopic") { OutValue = Body.bGyroscopicTorqueEnabled ? 1.0 : 0.0; return true; }
				if (Name == "ccd") { OutValue = Body.bUseCCD ? 1.0 : 0.0; return true; }
				if (Name == "sleep_enabled") { OutValue = Config.TestSettings.bAllowSleeping ? 1.0 : 0.0; return true; }
				if (Name == "sleep_disabled") { OutValue = Config.TestSettings.bAllowSleeping ? 0.0 : 1.0; return true; }
				if (Name == "workers") { OutValue = static_cast<double>(std::max(Config.TestSettings.NumThreads, 1) - 1); return true; }
				// M7: the PhysX dispatcher for T >= 2 is PxDefaultCpuDispatcherCreate(T) - T worker threads, the caller waits in
				// fetchResults (02 9.6; ExtDefaultCpuDispatcher.cpp:51-73 [V]); T = 1 is the inline / single-thread dispatcher.
				if (Name == "dispatcher_workers") { OutValue = Config.TestSettings.NumThreads > 1 ? static_cast<double>(Config.TestSettings.NumThreads) : 0.0; return true; }
				if (Name == "enhanced_determinism") { OutValue = Config.Settings.bEnableEnhancedDeterminism ? 1.0 : 0.0; return true; }
				if (Name == "restitution_threshold")
				{
					if (Config.TestSettings.RestitutionThresholdVelocity > 0.0f)
					{
						OutValue = Config.TestSettings.RestitutionThresholdVelocity;
						return true;
					}
					OutWhy = "the profile leaves the restitution threshold to the engine and the rule has no fallback";
					return false;
				}
				OutWhy = "unknown profile value " + Name;
				return false;
			}
			OutWhy = "unknown expectation '" + Expect + "'";
			(void)Target;
			return false;
		}

		void CheckReadbackRule(const FGateTarget& Target, const FReadbackContext& Run, const FEffectiveSetting& Setting, const FReadbackRule& Rule, FGateReport& Report,
			int32& OutCompared, int32& OutUnchecked, FReadbackDiffRow* OutRow = nullptr)
		{
			const std::string Where = "G9 " + Setting.Key + " (native '" + Setting.Native.substr(0, 160) + "')";
			if (!Rule.Unchecked.empty())
			{
				++OutUnchecked;
				return;
			}
			++OutCompared;
			const FResolvedPhysicsConfig& Config = *Run.Config;
			if (Rule.Expect == "text_contains_options")
			{
				const std::string Native = ToLowerAscii(Setting.Native);
				for (const std::string& Key : Rule.Options)
				{
					const std::string* Value = FindOption(Config.SceneOptions, Key);
					Report.Check(Value != nullptr && Native.find(ToLowerAscii(*Value)) != std::string::npos,
						Where + ": does not name option " + Key + " = '" + (Value != nullptr ? *Value : std::string("<unset>")) + "'");
				}
				return;
			}
			if (Rule.Expect == "text_flags")
			{
				for (const auto& [Flag, Source] : Rule.Flags)
				{
					double Value = 0.0;
					std::string Why;
					if (!Report.Check(ResolveExpectation(Source, Target, Run, Value, Why), Where + ": flag " + Flag + ": " + Why))
					{
						continue;
					}
					const std::string Expected = Flag + " " + (Value != 0.0 ? "1" : "0");
					Report.Check(Setting.Native.find(Expected) != std::string::npos, Where + ": expected '" + Expected + "' (" + Source + ")");
				}
				return;
			}
			double Expected = 0.0;
			std::string Why;
			bool bResolved = ResolveExpectation(Rule.Expect, Target, Run, Expected, Why);
			if (!bResolved && !Rule.Fallback.empty())
			{
				bResolved = ResolveExpectation(Rule.Fallback, Target, Run, Expected, Why);
			}
			if (OutRow != nullptr && bResolved)
			{
				OutRow->bHasResolved = true;
				OutRow->Resolved = Expected;
			}
			if (!Report.Check(bResolved, Where + ": " + Why))
			{
				return;
			}
			if (!Report.Check(Setting.bHasCanonical, Where + ": no canonical value to compare with " + Rule.Expect))
			{
				return;
			}
			if (Rule.Expect.rfind("min:", 0) == 0)
			{
				Report.Check(Setting.Canonical >= Expected, Where + Format(": %.9g < %.9g (%s)", Setting.Canonical, Expected, Rule.Reason.c_str()));
				return;
			}
			const double Allowed = std::max(Rule.Abs, Rule.Rel * std::abs(Expected));
			Report.Check(std::abs(Setting.Canonical - Expected) <= Allowed, Where + Format(": readback %.9g, resolved %.9g (%s), |d| %.3g > %.3g", Setting.Canonical,
				Expected, Rule.Expect.c_str(), std::abs(Setting.Canonical - Expected), Allowed));
		}

		class FGateG9 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G9"; }
			const char* GetDescription() const override { return "effective readback: every READBACK key equals its resolved value; shape.wrapping 0 on M3.BoxPile 1k"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, "M3.BoxPile", { { "N", "1000" } });
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*Run->Scene, Report);
				const FReadbackContext Context{ Run->Config.get(), Run->Scenario, &Run->Params, Run->DtEngine };
				CheckEffectiveReadback(Target, Context, Settings, {}, true, "M3.BoxPile 1k", Report);
				FinishGateScene(*Run, State);
				return Report;
			}
		};

		// ---- G12 ----------------------------------------------------------------------------------------------------
		class FGateG12 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "G12"; }
			const char* GetDescription() const override { return "capacity / errors: no engine error bit on any frame; M3.BoxPile 1k on its hints"; }
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				std::unique_ptr<FGateScene> Run = MakeGateScene(Target, "M3.BoxPile", { { "N", "1000" } });
				constexpr int32 PileFrames = 120;
				for (int32 Frame = 0; Frame < PileFrames; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				FinishGateScene(*Run, State);
				Report.Check(State.AccumulatedEngineErrorBits == 0, Format("engine error bits 0x%x over %lld frames of the gate scenes", State.AccumulatedEngineErrorBits,
					static_cast<long long>(State.NumFramesRun)));
				for (const std::string& Frame : State.ErrorFrames)
				{
					Report.Note("error: " + Frame);
				}
				// The backends' own counters (Jolt update errors and temp-allocator fallbacks, PhysX error reports, Bullet pool heap
				// fallbacks): present ones must be 0.
				const std::vector<FEffectiveSetting> Settings = ReadEffective(*Run->Scene, Report);
				int32 Counters = 0;
				for (const char* Key : { "jolt.update_error_bits", "jolt.temp_allocator_fallbacks", "jolt.asserts", "physx.error_bits", "physx.error_reports",
						 "bullet.engine_error_bits", "bullet.pool_heap_fallbacks" })
				{
					if (const FEffectiveSetting* Setting = FindSetting(Settings, Key))
					{
						++Counters;
						Report.Check(Setting->bHasCanonical && Setting->Canonical == 0.0, std::string(Key) + " = " + Setting->Native);
						Report.Note(std::string(Key) + " = " + Setting->Native);
					}
				}
				Report.Note(Format("%lld frames over %zu scene runs; %d engine counters read", static_cast<long long>(State.NumFramesRun), State.ScenesRun.size(), Counters));
				return Report;
			}
		};

		// ---- J1 (M9 fix round 0) ------------------------------------------------------------------------------------
		// The joint contract of docs/Abi.md 9.4 (item 13) as a physics gate, so that a DLL backend - the Chaos bridge, M13 (b) - runs
		// it in its own -Conformance process like every other gate (the doctests of the automation exe run the registry's SDK
		// engines only: PhysicsTest.Joint.BackendContract and PhysicsTest.Gates.J1 call this gate in process). Per declared type:
		//   frames     M6a.Pendulum 5 deg, 120 frames: the two world frames stay together (J1.hinge_stretch_cm / ball_stretch_cm),
		//              the hinge X axes stay aligned (hinge_axis_rad), the bob swings through to x < swing_through_x_cm
		//   snapshot   (PT_CAP_JOINT_SNAPSHOT) one record, JointIndex 0, JointHostIndex 0, not SLEEPING, Frame1World / Frame2World
		//              = the host's FConstraintInstance::GetWorldFrame (snapshot_frame_cm / snapshot_frame_rad); at the bottom of the
		//              swing (frame 29, the bob at its fastest) the joint transmits no couple about its anchor: |AngularImpulse| <=
		//              moving_couple_rel x |LinearImpulse| x the anchor-to-COM distance (a COM lever taken at the end of the step
		//              instead of the solver's shows here: M9 fix round 0, Bullet)
		//   release    releasing the bob releases the joint: not simulated, the snapshot drops it
		//   COM offset a 10 x 10 x 100 cm rod whose COMNudge puts its COM 5 cm off the vertical and 10 cm down, hung from the world
		//              at its top: the frames are in actor space, not COM space (a mapping that mixes them is off by the nudge
		//              from frame 0): stretch <= com_offset_stretch_cm over 60 frames, snapshot frames = the host's
		//   collision  (M9 fix round 2, item 5) two 20 cm cubes overlapping by 6 cm, gravity off, joined 40 cm above their centres (a
		//              hinge's axis is world Y, so the pair can open like a V): with UE's default (bDisableCollision false) the pair
		//              collides and the contact pushes the centres apart by >= collision_push_cm in 60 frames; with
		//              PT_JOINTF_DISABLE_COLLISION nothing acts and they keep their overlap (<= disable_collision_drift_cm) - measured
		//              on the host's poses, so a backend without contact capture runs it too
		//   sleeping   (M9 fix round 2, item 9; a profile with sleep on - UEParity - and PT_CAP_JOINT_SNAPSHOT) a two-link chain hung at
		//              rest from the world: awake, both joints carry the weight; after PUT_TO_SLEEP of both links and two steps the links
		//              stay asleep and both records are PT_JOINTSNAP_SLEEPING with zero impulses
		// and once (hinge + snapshot), the impulse convention (abi-v1.1, item 9): a level 100 cm rod of 1 kg held at its end by a
		// world hinge about X gets LinearImpulse = m g dt up (impulse_rel) and AngularImpulse = the couple -50 m g dt about Y
		// (couple_rel) - or NaN with PT_JOINTSNAP_ANGULAR_IMPULSE_NA (PhysX TGS).
		// The J1 scenes run with sleeping off (a joint that falls asleep checks nothing; G6 owns sleep) except the sleeping case, which
		// needs the profile's sleep; damping, iterations and the rest of the profile and the variant stay. A backend that declares neither joint type keeps its joints host-only (not
		// simulated, NumUnsupportedConstraints; checked) and reports Skip, allowed only where tolerances.json registers it (Null;
		// the Chaos bridge until it implements joints, docs/ChaosBackendGuide.md 10.1).
		struct FJ1Type
		{
			const char* Name;            // the M6a Joint parameter
			uint64 Capability;
			const char* StretchCheck;    // tolerances.json J1.<check>
			bool bHinge;
		};
		const FJ1Type GJ1Types[] = {
			{ "Hinge", PT_CAP_JOINT_HINGE, "hinge_stretch_cm", true },
			{ "Ball", PT_CAP_JOINT_BALL, "ball_stretch_cm", false },
		};

		// An empty scene on the target for the J1 cases with bodies of their own: no scenario, small hints, sleeping off unless
		// bKeepProfileSleep (the J1 sleeping case keeps the profile's sleep).
		std::unique_ptr<FGateScene> MakeEmptyGateScene(const FGateTarget& Target, const char* Label, const bool bKeepProfileSleep = false)
		{
			auto Run = std::make_unique<FGateScene>();
			Run->Config = std::make_unique<FResolvedPhysicsConfig>(*Target.Config);
			if (!bKeepProfileSleep)
			{
				Run->Config->TestSettings.bAllowSleeping = false;
			}
			FPhysSceneInitParams InitParams;
			InitParams.DebugName = std::string("Gate:") + Label;
			InitParams.BackendName = Target.Backend;
			InitParams.Config = Run->Config.get();
			InitParams.MaxBodiesHint = 8;
			InitParams.MaxBodyPairsHint = 16;
			InitParams.MaxContactConstraintsHint = 16;
			InitParams.bApplyForcedOverrides = false;
			Run->Scene = std::make_unique<FPhysScene>(InitParams);
			Run->Dt = Run->Config->TestSettings.FixedDeltaTime;
			Run->DtEngine = Analytic::EngineDeltaTime(Run->Dt);
			Run->InjectedEngineErrorBits = Target.InjectedEngineErrorBits;
			Run->Label = Label;
			return Run;
		}

		// The world frames of a joint at the host's pulled poses: the distance of their origins (cm) and the angle between their X
		// axes (rad, the hinge axis).
		void MeasureJointFrames(const FConstraintInstance& Joint, double& OutStretch, double& OutAxis)
		{
			const FTransform A = Joint.GetWorldFrame(EConstraintFrame::Frame1);
			const FTransform B = Joint.GetWorldFrame(EConstraintFrame::Frame2);
			OutStretch = (A.GetTranslation() - B.GetTranslation()).Size();
			OutAxis = std::acos(std::clamp(A.GetRotation().GetAxisX() | B.GetRotation().GetAxisX(), -1.0, 1.0));
		}

		FVector ToFVector(const PtVec3& V)
		{
			return FVector(V.X, V.Y, V.Z);
		}

		// One captured joint record against the host's FConstraintInstance at the same pulled poses (docs/Abi.md 9.4 item 9).
		void CheckSnapshotFrames(const FGateTarget& Target, FGateReport& Report, const PtJointSnapshot& Engine, const FConstraintInstance& Joint, const std::string& What)
		{
			const double FrameTol = Tolerance(Target, Report, "J1", "snapshot_frame_cm");
			const double AngleTol = Tolerance(Target, Report, "J1", "snapshot_frame_rad");
			double WorstDistance = 0.0;
			double WorstAngle = 0.0;
			for (const EConstraintFrame Frame : { EConstraintFrame::Frame1, EConstraintFrame::Frame2 })
			{
				const PtTransform& Reported = Frame == EConstraintFrame::Frame1 ? Engine.Frame1World : Engine.Frame2World;
				const FTransform Host = Joint.GetWorldFrame(Frame);
				WorstDistance = std::max(WorstDistance, (Host.GetTranslation() - ToFVector(Reported.Translation)).Size());
				WorstAngle = std::max(WorstAngle, AngleBetween(Host.GetRotation(), FQuat(Reported.Rotation.X, Reported.Rotation.Y, Reported.Rotation.Z, Reported.Rotation.W)));
			}
			Report.Check(WorstDistance <= FrameTol, Format("%s: snapshot frame origins %.3g cm from the host's GetWorldFrame (> %.3g)", What.c_str(), WorstDistance, FrameTol));
			Report.Check(WorstAngle <= AngleTol, Format("%s: snapshot frame rotations %.3g rad from the host's GetWorldFrame (> %.3g)", What.c_str(), WorstAngle, AngleTol));
		}

		class FGateJ1 final : public IConformanceGate
		{
		public:
			const char* GetId() const override { return "J1"; }
			const char* GetDescription() const override
			{
				return "joint contract (docs/Abi.md 9.4): M6a ball / hinge frames, swing, snapshot and release; a COM-offset body; the collision switch "
					"(bDisableCollision); a sleeping joint (sleep on); the snapshot impulse convention";
			}
			FGateReport Run(const FGateTarget& Target, FGateRunState& State) override
			{
				FGateReport Report;
				std::vector<const FJ1Type*> Declared;
				bool bSnapshot = false;
				for (const FJ1Type& Type : GJ1Types)
				{
					std::unique_ptr<FGateScene> Run = MakeGateScene(Target, "M6a.Pendulum", { { "Angle", "5" }, { "Joint", Type.Name } }, nullptr, true);
					const std::span<FConstraintInstance* const> Constraints = Run->Scene->GetConstraints();
					if (!Report.Check(Constraints.size() == 1u, Format("M6a %s: %zu constraints in the scene, expected 1", Type.Name, Constraints.size())))
					{
						continue;
					}
					bSnapshot = Declares(*Run->Scene, PT_CAP_JOINT_SNAPSHOT);
					if (!Declares(*Run->Scene, Type.Capability))
					{
						// docs/Abi.md 9.4 (P-4): a joint the backend does not declare stays on the host - not sent, not simulated, counted.
						const int32 Unsupported = Run->Scene->GetStats().NumUnsupportedConstraints;
						Report.Check(!Constraints[0]->IsSimulated() && Unsupported == 1, Format("M6a %s without its capability: the joint must stay host-only "
							"(simulated %d, NumUnsupportedConstraints %d)", Type.Name, Constraints[0]->IsSimulated() ? 1 : 0, Unsupported));
						Report.Note(Format("%s: capability not declared - the joint is host-only (not simulated); its cases do not run", Type.Name));
						continue;
					}
					Declared.push_back(&Type);
					CheckPendulum(Target, Report, *Run, Type, State);
					FinishGateScene(*Run, State);
				}
				if (Declared.empty())
				{
					if (Report.Failures.empty())
					{
						Report.Result = EGateResult::Skip;   // allowed only where tolerances.json registers it (RunPhysicsGates)
					}
					Report.Note("the backend declares neither PT_CAP_JOINT_BALL nor PT_CAP_JOINT_HINGE: joints stay host-only (docs/Abi.md 9.4)");
					return Report;
				}
				for (const FJ1Type* Type : Declared)
				{
					CheckComOffset(Target, Report, *Type, State);
					CheckCollisionSwitch(Target, Report, *Type, State);
				}
				if (!Target.Config->TestSettings.bAllowSleeping)
				{
					Report.Note("sleeping joint: the profile turns sleep off - the case runs under a profile with sleep on (UEParity)");
				}
				else if (!bSnapshot)
				{
					Report.Note("sleeping joint: no PT_CAP_JOINT_SNAPSHOT - PT_JOINTSNAP_SLEEPING is not checked");
				}
				else
				{
					CheckSleepingJoint(Target, Report, *Declared.front(), State);
				}
				const bool bHinge = std::any_of(Declared.begin(), Declared.end(), [](const FJ1Type* Type) { return Type->bHinge; });
				if (bHinge && bSnapshot)
				{
					CheckImpulseConvention(Target, Report, State);
				}
				else
				{
					Report.Note(bHinge ? "no PT_CAP_JOINT_SNAPSHOT: the impulse convention is not checked" : "no hinge: the impulse convention (a hinged cantilever) is not checked");
				}
				return Report;
			}

		private:
			static void CheckPendulum(const FGateTarget& Target, FGateReport& Report, FGateScene& Run, const FJ1Type& Type, FGateRunState& State)
			{
				FPhysScene& Scene = *Run.Scene;
				FConstraintInstance& Joint = *Scene.GetConstraints()[0];
				FBodyInstance& Bob = Run.Body(0);
				const std::string What = std::string("M6a 5 deg ") + Type.Name;
				if (!Report.Check(Joint.IsSimulated(), What + ": the backend declares the type, but the joint is not simulated"))
				{
					return;
				}
				Report.Check(Joint.ConstraintHandle.Index == 0u, Format("%s: joint index %u, expected 0 (creation order, docs/Abi.md 9.4 item 7)", What.c_str(),
					Joint.ConstraintHandle.Index));
				const bool bSnapshot = Declares(Scene, PT_CAP_JOINT_SNAPSHOT);
				const double StretchTol = ToleranceNoted(Target, Report, "J1", Type.StretchCheck);
				const double AxisTol = Tolerance(Target, Report, "J1", "hinge_axis_rad");
				const double SwingX = Tolerance(Target, Report, "J1", "swing_through_x_cm");
				const double CoupleRel = Tolerance(Target, Report, "J1", "moving_couple_rel");
				constexpr int32 Frames = 120;
				constexpr int32 BottomFrame = 29;   // the state after 30 steps (0.5 s, a quarter of the 2.009 s period): the bob passes the bottom
				double MaxStretch = 0.0;
				double MaxAxis = 0.0;
				double MinX = 1e30;
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(Run, State);
					double Stretch = 0.0;
					double Axis = 0.0;
					MeasureJointFrames(Joint, Stretch, Axis);
					MaxStretch = std::max(MaxStretch, Stretch);
					MaxAxis = Type.bHinge ? std::max(MaxAxis, Axis) : MaxAxis;
					MinX = std::min(MinX, Bob.State.BodyTM.GetTranslation().X);
					if (bSnapshot && Frame == BottomFrame)
					{
						CheckMovingCouple(Report, Scene, Bob, What, CoupleRel);
					}
				}
				Report.Check(MaxStretch <= StretchTol, Format("%s: max stretch %.4g cm over %d frames (> %.3g)", What.c_str(), MaxStretch, Frames, StretchTol));
				if (Type.bHinge)
				{
					Report.Check(MaxAxis <= AxisTol, Format("%s: max hinge axis error %.3g rad (> %.3g)", What.c_str(), MaxAxis, AxisTol));
				}
				Report.Check(MinX < SwingX, Format("%s: released at x = +8.7 cm, the bob never swung through to x < %.3g cm in %d frames (min %.3f)", What.c_str(),
					SwingX, Frames, MinX));
				Report.Note(Format("%s: max stretch %.3g cm, max hinge axis error %.2g rad, min bob x %.2f cm over %d frames", What.c_str(), MaxStretch, MaxAxis, MinX, Frames));
				FPhysicsDebugSnapshot Snapshot;
				if (bSnapshot)
				{
					const bool bCaptured = Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS);
					if (Report.Check(bCaptured && Snapshot.Joints.size() == 1u && Snapshot.JointHostIndex.size() == 1u, Format("%s: snapshot with %zu joint records "
						"(%zu host indices), expected 1", What.c_str(), Snapshot.Joints.size(), Snapshot.JointHostIndex.size())))
					{
						const PtJointSnapshot& Record = Snapshot.Joints[0];
						Report.Check(Record.JointIndex == 0u, Format("%s: snapshot JointIndex %u, expected 0", What.c_str(), Record.JointIndex));
						Report.Check(Snapshot.JointHostIndex[0] == 0, Format("%s: JointHostIndex %d, expected 0", What.c_str(), Snapshot.JointHostIndex[0]));
						Report.Check((Record.Flags & ~PT_JOINTSNAP_ALL) == 0u, Format("%s: undefined PT_JOINTSNAP_* bits 0x%x", What.c_str(), Record.Flags));
						Report.Check((Record.Flags & PT_JOINTSNAP_SLEEPING) == 0u, What + ": a swinging joint (sleeping off) reported PT_JOINTSNAP_SLEEPING");
						CheckSnapshotFrames(Target, Report, Record, Joint, What);
					}
				}
				Scene.ReleaseBody(&Bob);
				Report.Check(!Joint.IsSimulated(), What + ": releasing the bob must release its joint (docs/Abi.md 9.4 item 8)");
				if (bSnapshot)
				{
					const bool bCaptured = Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS);
					Report.Check(bCaptured && Snapshot.Joints.empty(), Format("%s: %zu joint records after the release, expected none", What.c_str(), Snapshot.Joints.size()));
				}
				else
				{
					Report.Note(What + ": no PT_CAP_JOINT_SNAPSHOT - snapshot checks not run");
				}
			}

			// A ball or a hinge swinging in its plane transmits no couple: about the anchor (abi-v1.1) the angular impulse is 0.
			static void CheckMovingCouple(FGateReport& Report, const FPhysScene& Scene, const FBodyInstance& Bob, const std::string& What, const double CoupleRel)
			{
				FPhysicsDebugSnapshot Snapshot;
				if (!Report.Check(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS) && Snapshot.Joints.size() == 1u,
					What + ": no joint record at the bottom of the swing"))
				{
					return;
				}
				const PtJointSnapshot& Moving = Snapshot.Joints[0];
				const FVector Linear = ToFVector(Moving.LinearImpulse);
				const FVector Angular = ToFVector(Moving.AngularImpulse);
				const FVector Com = Bob.State.BodyTM.TransformPosition(Bob.MassProperties.CenterOfMass);
				const double Lever = (ToFVector(Moving.Frame1World.Translation) - Com).Size();
				const double Speed = Bob.State.LinearVelocity.Size();
				if ((Moving.Flags & PT_JOINTSNAP_IMPULSE_NA) != 0u)
				{
					Report.Note(What + ": PT_JOINTSNAP_IMPULSE_NA at the bottom of the swing - the couple is not checked");
					return;
				}
				const double Weight = Bob.MassProperties.Mass * std::abs(Scene.GetGravity().Z) * Analytic::EngineDeltaTime(Scene.GetTestSettings().FixedDeltaTime);
				Report.Check(Linear.Size() > 0.5 * Weight, Format("%s: |LinearImpulse| %.4g kg cm/s at the bottom of the swing, below half of m g dt %.4g", What.c_str(),
					Linear.Size(), Weight));
				if ((Moving.Flags & PT_JOINTSNAP_ANGULAR_IMPULSE_NA) != 0u)
				{
					Report.Note(What + ": PT_JOINTSNAP_ANGULAR_IMPULSE_NA - the moving joint's couple is not checked");
					return;
				}
				const double Bound = CoupleRel * Linear.Size() * Lever;
				Report.Check(Angular.Size() <= Bound, Format("%s: the moving joint reports a couple |AngularImpulse| %.4g kg cm^2/s about its anchor at the bottom of "
					"the swing (bob speed %.3g cm/s; > %.3g = moving_couple_rel x |LinearImpulse| %.4g x anchor-COM %.4g cm): a ball or a hinge in its plane "
					"transmits none (docs/Abi.md 9.4 item 9)", What.c_str(), Angular.Size(), Speed, Bound, Linear.Size(), Lever));
				Report.Note(Format("%s at the bottom of the swing (bob %.3g cm/s): LinearImpulse |%.4g| kg cm/s, AngularImpulse |%.3g| kg cm^2/s about the anchor "
					"(%.2g of |L| x %.4g cm)", What.c_str(), Speed, Linear.Size(), Angular.Size(), Linear.Size() * Lever > 0.0 ? Angular.Size() / (Linear.Size() * Lever) : 0.0,
					Lever));
			}

			static void CheckComOffset(const FGateTarget& Target, FGateReport& Report, const FJ1Type& Type, FGateRunState& State)
			{
				const std::string What = std::string("COM-offset rod ") + Type.Name;
				std::unique_ptr<FGateScene> Run = MakeEmptyGateScene(Target, Type.bHinge ? "J1.ComOffsetHinge" : "J1.ComOffsetBall");
				FPhysScene& Scene = *Run->Scene;
				FBodySetup Rod;
				Rod.AggGeom.BoxElems.emplace_back(10.0, 10.0, 100.0);
				Rod.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
				const FBodySetup* RodSetup = Scene.CreateBodySetup(std::move(Rod));
				FBodyInstance Template;
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = 1.0f;
				Template.LinearDamping = 0.0f;
				Template.AngularDamping = 0.0f;
				const FVector Nudge(5.0, 0.0, -10.0);
				Template.COMNudge = Nudge;
				FBodyInstance* Body = Scene.SpawnBody(Template, RodSetup, FTransform(FVector(0.0, 0.0, 450.0)));
				// Hung at its top (actor-local (0, 0, 50), the world point (0, 0, 500)); the hinge axis (frame X) is world Y, the plane of
				// the COM's offset is XZ, so the rod swings about the hinge axis (about 4.8 deg from the vertical).
				FConstraintInstance Hang;
				Hang.JointName = "J1ComOffset";
				Hang.Pos1 = FVector(0.0, 0.0, 50.0);
				Hang.PriAxis1 = FVector(0.0, 1.0, 0.0);
				Hang.SecAxis1 = FVector(0.0, 0.0, 1.0);
				Hang.Pos2 = FVector(0.0, 0.0, 500.0);
				Hang.PriAxis2 = FVector(0.0, 1.0, 0.0);
				Hang.SecAxis2 = FVector(0.0, 0.0, 1.0);
				if (Type.bHinge)
				{
					ConstraintUtils::ConfigureAsHinge(Hang);
				}
				else
				{
					ConstraintUtils::ConfigureAsBallAndSocket(Hang);
				}
				FConstraintInstance* Joint = Scene.SpawnConstraint(Hang, Body, nullptr);
				Scene.FlushDeferredActors();
				Scene.PrepareForSimulation();
				Report.Check((Body->MassProperties.CenterOfMass - Nudge).Size() < 1e-6, Format("%s: the host COM (%.6g, %.6g, %.6g) is not the nudge", What.c_str(),
					Body->MassProperties.CenterOfMass.X, Body->MassProperties.CenterOfMass.Y, Body->MassProperties.CenterOfMass.Z));
				if (!Report.Check(Joint != nullptr && Joint->IsSimulated(), What + ": the joint is not simulated"))
				{
					return;
				}
				const double StretchTol = Tolerance(Target, Report, "J1", "com_offset_stretch_cm");
				const double AxisTol = Tolerance(Target, Report, "J1", "hinge_axis_rad");
				constexpr int32 Frames = 60;
				double MaxStretch = 0.0;
				double MaxAxis = 0.0;
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					TickGateScene(*Run, State);
					double Stretch = 0.0;
					double Axis = 0.0;
					MeasureJointFrames(*Joint, Stretch, Axis);
					MaxStretch = std::max(MaxStretch, Stretch);
					MaxAxis = Type.bHinge ? std::max(MaxAxis, Axis) : MaxAxis;
				}
				const double SwingX = Body->State.BodyTM.TransformPosition(Body->MassProperties.CenterOfMass).X;
				Report.Check(MaxStretch <= StretchTol, Format("%s: max stretch %.4g cm over %d frames (> %.3g): frames taken in COM space instead of actor space "
					"are off by the nudge (%.3g cm)", What.c_str(), MaxStretch, Frames, StretchTol, Nudge.Size()));
				if (Type.bHinge)
				{
					Report.Check(MaxAxis <= AxisTol, Format("%s: max hinge axis error %.3g rad (> %.3g)", What.c_str(), MaxAxis, AxisTol));
				}
				Report.Note(Format("%s (COM nudge (5, 0, -10) cm): max stretch %.3g cm, hinge axis error %.2g rad over %d frames, COM x %.2f cm (swinging from +5)",
					What.c_str(), MaxStretch, MaxAxis, Frames, SwingX));
				if (Declares(Scene, PT_CAP_JOINT_SNAPSHOT))
				{
					FPhysicsDebugSnapshot Snapshot;
					if (Report.Check(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS) && Snapshot.Joints.size() == 1u,
						Format("%s: %zu joint records in the snapshot, expected 1", What.c_str(), Snapshot.Joints.size())))
					{
						CheckSnapshotFrames(Target, Report, Snapshot.Joints[0], *Joint, What);
					}
				}
				FinishGateScene(*Run, State);
			}

			// M9 fix round 2 (docs/Abi.md 9.4 item 5): PT_JOINTF_DISABLE_COLLISION is UE's bDisableCollision. UE's default (false) lets the
			// jointed pair collide; with the flag no shape of Body1 collides with one of Body2. Two 20 cm cubes overlapping by 6 cm along X,
			// gravity off, joined at a point 40 cm above their centres - outside both, so the pair can open like a V about it (the frames'
			// X axis, a hinge's twist axis, is world Y). Colliding, the contact pushes the cubes apart: their centre distance grows by at
			// least collision_push_cm within 60 frames (an engine that keeps a penetration slop - Jolt 2 cm - does not remove the whole
			// overlap; full separation opens the V to about 22 cm). With the flag nothing acts on the pair and the overlap stays
			// (disable_collision_drift_cm). Measured on the host's poses: a backend without contact capture runs it too. An inverted
			// switch fails both runs.
			static void CheckCollisionSwitch(const FGateTarget& Target, FGateReport& Report, const FJ1Type& Type, FGateRunState& State)
			{
				const double PushMin = Tolerance(Target, Report, "J1", "collision_push_cm");
				const double DriftMax = Tolerance(Target, Report, "J1", "disable_collision_drift_cm");
				constexpr double CubeSize = 20.0;
				constexpr double CentreDistance = 14.0;   // 6 cm overlap
				constexpr double AnchorAbove = 40.0;
				constexpr double Height = 500.0;
				constexpr int32 Frames = 60;
				for (const bool bDisable : { false, true })
				{
					const std::string What = std::string("collision switch ") + Type.Name + (bDisable ? " with PT_JOINTF_DISABLE_COLLISION" : " (UE default: the pair collides)");
					std::unique_ptr<FGateScene> Run = MakeEmptyGateScene(Target, bDisable ? "J1.DisableCollision" : "J1.CollidingPair");
					FPhysScene& Scene = *Run->Scene;
					FBodySetup Cube;
					Cube.AggGeom.BoxElems.emplace_back(CubeSize, CubeSize, CubeSize);
					Cube.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
					const FBodySetup* CubeSetup = Scene.CreateBodySetup(std::move(Cube));
					FBodyInstance Template;
					Template.bSimulatePhysics = true;
					Template.bOverrideMass = true;
					Template.MassInKgOverride = 1.0f;
					Template.LinearDamping = 0.0f;
					Template.AngularDamping = 0.0f;
					Template.bEnableGravity = false;
					FBodyInstance* Left = Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(-0.5 * CentreDistance, 0.0, Height)));
					FBodyInstance* Right = Scene.SpawnBody(Template, CubeSetup, FTransform(FVector(0.5 * CentreDistance, 0.0, Height)));
					// Both frames sit at the world point (0, 0, Height + AnchorAbove) at rest, X = world Y.
					FConstraintInstance Pair;
					Pair.JointName = "J1CollisionSwitch";
					Pair.Pos1 = FVector(0.5 * CentreDistance, 0.0, AnchorAbove);
					Pair.PriAxis1 = FVector(0.0, 1.0, 0.0);
					Pair.SecAxis1 = FVector(0.0, 0.0, 1.0);
					Pair.Pos2 = FVector(-0.5 * CentreDistance, 0.0, AnchorAbove);
					Pair.PriAxis2 = FVector(0.0, 1.0, 0.0);
					Pair.SecAxis2 = FVector(0.0, 0.0, 1.0);
					if (Type.bHinge)
					{
						ConstraintUtils::ConfigureAsHinge(Pair);
					}
					else
					{
						ConstraintUtils::ConfigureAsBallAndSocket(Pair);
					}
					Pair.ProfileInstance.bDisableCollision = bDisable;
					FConstraintInstance* Joint = Scene.SpawnConstraint(Pair, Left, Right);
					Scene.FlushDeferredActors();
					Scene.PrepareForSimulation();
					if (!Report.Check(Joint != nullptr && Joint->IsSimulated(), What + ": the joint is not simulated"))
					{
						continue;
					}
					const auto CentreGap = [&]() { return (Right->State.BodyTM.GetTranslation() - Left->State.BodyTM.GetTranslation()).Size(); };
					const double Start = CentreGap();
					double MaxStretch = 0.0;
					for (int32 Frame = 0; Frame < Frames; ++Frame)
					{
						TickGateScene(*Run, State);
						double Stretch = 0.0;
						double Axis = 0.0;
						MeasureJointFrames(*Joint, Stretch, Axis);
						MaxStretch = std::max(MaxStretch, Stretch);
					}
					const double Moved = CentreGap() - Start;
					if (bDisable)
					{
						Report.Check(std::abs(Moved) <= DriftMax, Format("%s: the cubes' centre distance changed by %.4g cm in %d frames (> %.3g): something acts on a "
							"pair that must not collide (docs/Abi.md 9.4 item 5)", What.c_str(), Moved, Frames, DriftMax));
					}
					else
					{
						Report.Check(Moved >= PushMin, Format("%s: the overlapping cubes' centre distance grew by %.4g cm in %d frames (< %.3g): the pair does not "
							"collide although bDisableCollision is false (UE's default, docs/Abi.md 9.4 item 5)", What.c_str(), Moved, Frames, PushMin));
					}
					Report.Note(Format("%s: centre distance %.3f -> %.3f cm over %d frames (6 cm overlap at the start), max joint stretch %.3g cm", What.c_str(), Start,
						Start + Moved, Frames, MaxStretch));
					FinishGateScene(*Run, State);
				}
			}

			// M9 fix round 2 (docs/Abi.md 9.4 item 9): a joint with no awake dynamic body did not run - zero impulses and
			// PT_JOINTSNAP_SLEEPING. A two-link chain (10 x 10 x 40 cm, 1 kg each) hung at rest from the world (joint 0: world - upper
			// link, joint 1: upper - lower link with bDisableCollision, the links touch at their tips). Awake, both joints carry the weight
			// (LinearImpulse up, not SLEEPING); after PUT_TO_SLEEP of both links and two steps, both links stay asleep and both records are
			// SLEEPING alone with exactly zero impulses. Needs a profile with sleep on (UEParity) and PT_CAP_JOINT_SNAPSHOT.
			static void CheckSleepingJoint(const FGateTarget& Target, FGateReport& Report, const FJ1Type& Type, FGateRunState& State)
			{
				const std::string What = std::string("sleeping joint ") + Type.Name;
				std::unique_ptr<FGateScene> Run = MakeEmptyGateScene(Target, "J1.SleepingJoint", true);
				FPhysScene& Scene = *Run->Scene;
				FBodySetup Link;
				Link.AggGeom.BoxElems.emplace_back(10.0, 10.0, 40.0);
				Link.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
				const FBodySetup* LinkSetup = Scene.CreateBodySetup(std::move(Link));
				FBodyInstance Template;
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = 1.0f;
				FBodyInstance* Upper = Scene.SpawnBody(Template, LinkSetup, FTransform(FVector(0.0, 0.0, 580.0)));
				FBodyInstance* Lower = Scene.SpawnBody(Template, LinkSetup, FTransform(FVector(0.0, 0.0, 540.0)));
				const auto MakeJoint = [&](const char* Name, const FVector& Pos1, const FVector& Pos2, const bool bDisableCollision)
				{
					FConstraintInstance Joint;
					Joint.JointName = Name;
					Joint.Pos1 = Pos1;
					Joint.PriAxis1 = FVector(0.0, 1.0, 0.0);
					Joint.SecAxis1 = FVector(0.0, 0.0, 1.0);
					Joint.Pos2 = Pos2;
					Joint.PriAxis2 = FVector(0.0, 1.0, 0.0);
					Joint.SecAxis2 = FVector(0.0, 0.0, 1.0);
					if (Type.bHinge)
					{
						ConstraintUtils::ConfigureAsHinge(Joint);
					}
					else
					{
						ConstraintUtils::ConfigureAsBallAndSocket(Joint);
					}
					Joint.ProfileInstance.bDisableCollision = bDisableCollision;
					return Joint;
				};
				FConstraintInstance* Top = Scene.SpawnConstraint(MakeJoint("J1SleepTop", FVector(0.0, 0.0, 20.0), FVector(0.0, 0.0, 600.0), false), Upper, nullptr);
				FConstraintInstance* Middle = Scene.SpawnConstraint(MakeJoint("J1SleepMiddle", FVector(0.0, 0.0, 20.0), FVector(0.0, 0.0, -20.0), true), Lower, Upper);
				Scene.FlushDeferredActors();
				Scene.PrepareForSimulation();
				if (!Report.Check(Top != nullptr && Middle != nullptr && Top->IsSimulated() && Middle->IsSimulated(), What + ": the joints are not simulated"))
				{
					return;
				}
				for (int32 Frame = 0; Frame < 3; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				FPhysicsDebugSnapshot Snapshot;
				if (!Report.Check(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS) && Snapshot.Joints.size() == 2u,
					Format("%s: %zu joint records while awake, expected 2", What.c_str(), Snapshot.Joints.size())))
				{
					return;
				}
				for (const PtJointSnapshot& Awake : Snapshot.Joints)
				{
					Report.Check((Awake.Flags & PT_JOINTSNAP_SLEEPING) == 0u, Format("%s: joint %u reports PT_JOINTSNAP_SLEEPING while its links are awake", What.c_str(),
						Awake.JointIndex));
					Report.Check((Awake.Flags & PT_JOINTSNAP_IMPULSE_NA) != 0u || Awake.LinearImpulse.Z > 0.0, Format("%s: joint %u carries LinearImpulse.Z %.4g "
						"while awake, expected the weight below it (> 0)", What.c_str(), Awake.JointIndex, Awake.LinearImpulse.Z));
				}
				Upper->PutInstanceToSleep();
				Lower->PutInstanceToSleep();
				for (int32 Frame = 0; Frame < 2; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				FinishGateScene(*Run, State);
				const bool bAsleep = Upper->State.ObjectState == EObjectStateType::Sleeping && Lower->State.ObjectState == EObjectStateType::Sleeping;
				if (!Report.Check(bAsleep, Format("%s: the links are in states %d / %d two steps after PUT_TO_SLEEP, expected Sleeping (nothing wakes them)", What.c_str(),
					static_cast<int32>(Upper->State.ObjectState), static_cast<int32>(Lower->State.ObjectState))))
				{
					return;
				}
				if (!Report.Check(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS) && Snapshot.Joints.size() == 2u,
					Format("%s: %zu joint records while asleep, expected 2", What.c_str(), Snapshot.Joints.size())))
				{
					return;
				}
				for (const PtJointSnapshot& Asleep : Snapshot.Joints)
				{
					const bool bZero = Asleep.LinearImpulse.X == 0.0 && Asleep.LinearImpulse.Y == 0.0 && Asleep.LinearImpulse.Z == 0.0 && Asleep.AngularImpulse.X == 0.0
						&& Asleep.AngularImpulse.Y == 0.0 && Asleep.AngularImpulse.Z == 0.0;
					Report.Check(Asleep.Flags == PT_JOINTSNAP_SLEEPING && bZero, Format("%s: joint %u with both links asleep reports flags 0x%x, LinearImpulse (%.4g, %.4g, "
						"%.4g), AngularImpulse (%.4g, %.4g, %.4g); expected PT_JOINTSNAP_SLEEPING alone and zero impulses (docs/Abi.md 9.4 item 9)", What.c_str(),
						Asleep.JointIndex, Asleep.Flags, Asleep.LinearImpulse.X, Asleep.LinearImpulse.Y, Asleep.LinearImpulse.Z, Asleep.AngularImpulse.X,
						Asleep.AngularImpulse.Y, Asleep.AngularImpulse.Z));
				}
				Report.Note(Format("%s: two links hung at rest, put to sleep after 3 awake frames: both joints PT_JOINTSNAP_SLEEPING with zero impulses", What.c_str()));
			}

			// abi-v1.1 (docs/Abi.md 9.4 item 9): LinearImpulse on Body1, AngularImpulse about the joint anchor (Frame1World's origin) - the
			// couple of the angular rows, since the linear impulse acts at the anchor. About the rod's COM the joint's net angular
			// impulse would be 0.
			static void CheckImpulseConvention(const FGateTarget& Target, FGateReport& Report, FGateRunState& State)
			{
				std::unique_ptr<FGateScene> Run = MakeEmptyGateScene(Target, "J1.Cantilever");
				FPhysScene& Scene = *Run->Scene;
				FBodySetup Rod;
				Rod.AggGeom.BoxElems.emplace_back(100.0, 4.0, 4.0);
				Rod.PhysMaterial = Scene.GetDefaultPhysicalMaterial();
				const FBodySetup* RodSetup = Scene.CreateBodySetup(std::move(Rod));
				FBodyInstance Template;
				Template.bSimulatePhysics = true;
				Template.bOverrideMass = true;
				Template.MassInKgOverride = 1.0f;
				Template.LinearDamping = 0.0f;
				Template.AngularDamping = 0.0f;
				FBodyInstance* Body = Scene.SpawnBody(Template, RodSetup, FTransform(FVector(0.0, 0.0, 500.0)));
				FConstraintInstance Arm;
				Arm.JointName = "J1Cantilever";
				Arm.Pos1 = FVector(-50.0, 0.0, 0.0);
				Arm.PriAxis1 = FVector(1.0, 0.0, 0.0);
				Arm.SecAxis1 = FVector(0.0, 1.0, 0.0);
				Arm.Pos2 = FVector(-50.0, 0.0, 500.0);
				Arm.PriAxis2 = FVector(1.0, 0.0, 0.0);
				Arm.SecAxis2 = FVector(0.0, 1.0, 0.0);
				ConstraintUtils::ConfigureAsHinge(Arm);
				FConstraintInstance* Joint = Scene.SpawnConstraint(Arm, Body, nullptr);
				Scene.FlushDeferredActors();
				Scene.PrepareForSimulation();
				if (!Report.Check(Joint != nullptr && Joint->IsSimulated(), "cantilever: the hinge is not simulated"))
				{
					return;
				}
				for (int32 Frame = 0; Frame < 5; ++Frame)
				{
					TickGateScene(*Run, State);
				}
				FinishGateScene(*Run, State);
				FPhysicsDebugSnapshot Snapshot;
				if (!Report.Check(Scene.CaptureDebugSnapshot(Snapshot, PT_SNAP_PARTICLES | PT_SNAP_JOINTS) && Snapshot.Joints.size() == 1u,
					Format("cantilever: %zu joint records in the snapshot, expected 1", Snapshot.Joints.size())))
				{
					return;
				}
				const PtJointSnapshot& Held = Snapshot.Joints[0];
				const double Weight = std::abs(Scene.GetGravity().Z) * Run->DtEngine;   // m g dt of 1 kg
				const double ImpulseRel = Tolerance(Target, Report, "J1", "impulse_rel");
				const double CoupleRel = Tolerance(Target, Report, "J1", "couple_rel");
				Report.Note(Format("cantilever (1 kg, 100 cm, world hinge about X at its end): LinearImpulse (%.4g, %.4g, %.4g) kg cm/s, AngularImpulse (%.4g, %.4g, %.4g) "
					"kg cm^2/s, flags 0x%x; m g dt = %.4g, -50 m g dt = %.4g", Held.LinearImpulse.X, Held.LinearImpulse.Y, Held.LinearImpulse.Z, Held.AngularImpulse.X,
					Held.AngularImpulse.Y, Held.AngularImpulse.Z, Held.Flags, Weight, -50.0 * Weight));
				Report.Check((Held.Flags & (PT_JOINTSNAP_SLEEPING | PT_JOINTSNAP_IMPULSE_NA)) == 0u, Format("cantilever: flags 0x%x - a snapshot backend reports the "
					"joint's impulses (sleeping off)", Held.Flags));
				Report.Check(std::abs(Held.LinearImpulse.Z - Weight) <= ImpulseRel * Weight && std::abs(Held.LinearImpulse.X) <= ImpulseRel * Weight
					&& std::abs(Held.LinearImpulse.Y) <= ImpulseRel * Weight, Format("cantilever: LinearImpulse (%.4g, %.4g, %.4g), expected (0, 0, m g dt %.4g) within "
					"%.3g relative (docs/Abi.md 9.4 item 9: the impulse on Body1)", Held.LinearImpulse.X, Held.LinearImpulse.Y, Held.LinearImpulse.Z, Weight, ImpulseRel));
				if ((Held.Flags & PT_JOINTSNAP_ANGULAR_IMPULSE_NA) != 0u)
				{
					Report.Check(std::isnan(Held.AngularImpulse.X) && std::isnan(Held.AngularImpulse.Y) && std::isnan(Held.AngularImpulse.Z),
						"cantilever: PT_JOINTSNAP_ANGULAR_IMPULSE_NA with a non-NaN AngularImpulse");
					Report.Note("cantilever: PT_JOINTSNAP_ANGULAR_IMPULSE_NA - the couple is not checked (LinearImpulse is)");
					return;
				}
				const double Couple = -50.0 * Weight;
				Report.Check(std::abs(Held.AngularImpulse.Y - Couple) <= CoupleRel * std::abs(Couple) && std::abs(Held.AngularImpulse.X) <= CoupleRel * std::abs(Couple)
					&& std::abs(Held.AngularImpulse.Z) <= CoupleRel * std::abs(Couple), Format("cantilever: AngularImpulse (%.4g, %.4g, %.4g), expected the couple "
					"(0, -50 m g dt = %.4g, 0) about the anchor within %.3g relative (abi-v1.1, docs/Abi.md 9.4 item 9; about the COM it would be 0)",
					Held.AngularImpulse.X, Held.AngularImpulse.Y, Held.AngularImpulse.Z, Couple, CoupleRel));
			}
		};

		// ---- the module rule ------------------------------------------------------------------------------------------
		bool PrepareModule(const FGateTarget& Target, std::string& OutError)
		{
			FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule(Target.Backend, OutError);
			if (Module == nullptr)
			{
				return false;
			}
			if (Module->IsInitialized() && (Module->GetInfo().ModuleFlags & PT_MODULE_GLOBAL_SCENE_SETTINGS) != 0
				&& Module->GetInitParams().InitOptions != Target.Config->InitOptions)
			{
				if (Module->GetNumLiveScenes() != 0)
				{
					OutError = "backend " + Target.Backend + " is initialised with other process-wide settings and has live scenes";
					return false;
				}
				const PtResult Result = Module->TermGamePhys();
				if (Result != PT_OK)
				{
					OutError = "TermGamePhys of " + Target.Backend + " failed: " + Module->GetLastErrorMessage();
					return false;
				}
			}
			return true;
		}
	}

	std::string FGateTarget::GetEngineLabel() const
	{
		return Variant.empty() ? Backend : Backend + "." + Variant;
	}

	std::string FGateTarget::GetLabel() const
	{
		return GetEngineLabel() + " / " + Profile;
	}

	void CheckEffectiveReadback(const FGateTarget& Target, const FReadbackContext& Context, const std::vector<FEffectiveSetting>& Settings,
		const std::vector<std::string>& SkipKeys, const bool bRequireNoWrapping, const std::string& SceneLabel, FGateReport& Report,
		std::vector<FReadbackDiffRow>* OutDiff)
	{
		int32 NumReadback = 0;
		int32 Compared = 0;
		int32 Unchecked = 0;
		int32 Skipped = 0;
		int32 SceneBound = 0;
		int32 ScenePinned = 0;
		int32 NoBodyKeys = 0;
		std::vector<std::string> UncheckedKeys;
		std::vector<std::string> SceneBoundKeys;
		std::vector<std::string> ScenePinnedKeys;
		for (const FEffectiveSetting& Setting : Settings)
		{
			// M9: the diff row of this key; the branches below fill its status.
			FReadbackDiffRow Scratch;
			FReadbackDiffRow& Row = OutDiff != nullptr ? OutDiff->emplace_back() : Scratch;
			Row.Key = Setting.Key;
			Row.Source = Setting.Source;
			Row.Native = Setting.Native;
			Row.bHasEffective = Setting.bHasCanonical;
			Row.Effective = Setting.Canonical;
			if (Setting.Source != "READBACK")
			{
				Row.Status = "not_readback";
				continue;
			}
			if (std::find(SkipKeys.begin(), SkipKeys.end(), Setting.Key) != SkipKeys.end())
			{
				++Skipped;
				Row.Status = "skipped";
				Row.Note = "left to another gate (G10a)";
				continue;
			}
			++NumReadback;
			const std::string SceneName = Context.Scenario != nullptr ? Context.Scenario->GetName() : "";
			const FReadbackRule* Rule = Target.Tolerances->FindReadbackRule(Target.Backend, Target.Variant, Setting.Key, SceneName);
			if (!Report.Check(Rule != nullptr, "G9 " + Setting.Key + ": READBACK key without a registered expectation (tolerances.json gates.G9.readback)"))
			{
				Row.Status = "no_rule";
				continue;
			}
			Row.Rule = !Rule->Unchecked.empty() ? std::string("unchecked") : Rule->Expect;
			if (!Rule->Scenes.empty() && Context.Scenario != nullptr && std::find(Rule->Scenes.begin(), Rule->Scenes.end(), SceneName) == Rule->Scenes.end())
			{
				++SceneBound;
				SceneBoundKeys.push_back(Setting.Key);
				Row.Status = "skipped";
				Row.Note = "geometry-bound rule (tolerances.json scene list) of other scenes";
				continue;
			}
			const bool bBodyTemplateRule = Rule->Expect == "profile:linear_damping" || Rule->Expect == "profile:max_angular_velocity"
				|| Rule->Expect == "profile:gyroscopic" || Rule->Expect == "profile:ccd";
			if (bBodyTemplateRule && !Context.bSceneUsesProfileBodyDefaults)
			{
				++ScenePinned;
				ScenePinnedKeys.push_back(Setting.Key);
				Row.Status = "skipped";
				Row.Note = "the scene pins its bodies' values (body-template rule)";
				continue;
			}
			if (Context.NumDynamicBodies == 0 && !Setting.bHasCanonical && Rule->Unchecked.empty())
			{
				++NoBodyKeys;
				Row.Status = "skipped";
				Row.Note = "aggregates over dynamic bodies; the scene has none";
				continue;
			}
			const int32 Before = Unchecked;
			const size_t FailuresBefore = Report.Failures.size();
			CheckReadbackRule(Target, Context, Setting, *Rule, Report, Compared, Unchecked, &Row);
			if (Unchecked != Before)
			{
				UncheckedKeys.push_back(Setting.Key + " (" + Rule->Unchecked + ")");
				Row.Status = "unchecked";
				Row.Note = Rule->Unchecked;
			}
			else
			{
				Row.Status = Report.Failures.size() == FailuresBefore ? "match" : "mismatch";
				if (Row.Status == "mismatch")
				{
					Row.Note = Report.Failures.back();
				}
			}
		}
		if (bRequireNoWrapping)
		{
			const FEffectiveSetting* Wrapping = FindSetting(Settings, "shape.wrapping");
			Report.Check(Wrapping != nullptr && Wrapping->bHasCanonical && Wrapping->Canonical == 0.0,
				SceneLabel + ": shape.wrapping must be 0 (" + (Wrapping != nullptr ? Wrapping->Native : std::string("missing")) + ")");
		}
		Report.Note(Format("%s: %d READBACK keys, %d compared with the resolved value, %d registered as not profile-derived%s", SceneLabel.c_str(), NumReadback,
			Compared, Unchecked, Skipped > 0 ? Format(", %d left to another gate", Skipped).c_str() : ""));
		for (const std::string& Key : UncheckedKeys)
		{
			Report.Note("not profile-derived: " + Key);
		}
		if (SceneBound > 0)
		{
			std::string Keys;
			for (const std::string& Key : SceneBoundKeys)
			{
				Keys += (Keys.empty() ? "" : ", ") + Key;
			}
			Report.Note(Format("%d READBACK key(s) with a geometry-bound rule (tolerances.json \"scene\") not checked on %s: %s", SceneBound, SceneLabel.c_str(), Keys.c_str()));
		}
		if (ScenePinned > 0)
		{
			std::string Keys;
			for (const std::string& Key : ScenePinnedKeys)
			{
				Keys += (Keys.empty() ? "" : ", ") + Key;
			}
			Report.Note(Format("%d READBACK key(s) with a body-template rule not checked: %s pins its bodies' values itself (%s)", ScenePinned, SceneLabel.c_str(), Keys.c_str()));
		}
		if (NoBodyKeys > 0)
		{
			Report.Note(Format("%d READBACK key(s) aggregate over dynamic bodies and read back no value: %s has none", NoBodyKeys, SceneLabel.c_str()));
		}
	}

	const std::vector<IConformanceGate*>& GetPhysicsGates()
	{
		static FGateG1 G1;
		static FGateG1b G1b;
		static FGateG2 G2;
		static FGateG3 G3;
		static FGateG4 G4;
		static FGateG5 G5;
		static FGateG6 G6;
		static FGateG7 G7;
		static FGateG9 G9;
		static FGateJ1 J1;
		static FGateG12 G12;
		static const std::vector<IConformanceGate*> Gates = { &G1, &G1b, &G2, &G3, &G4, &G5, &G6, &G7, &G9, &J1, &G12 };
		return Gates;
	}

	IConformanceGate* FindPhysicsGate(const std::string_view Id)
	{
		const std::string Lower = ToLowerAscii(Id);
		for (IConformanceGate* Gate : GetPhysicsGates())
		{
			if (ToLowerAscii(Gate->GetId()) == Lower)
			{
				return Gate;
			}
		}
		return nullptr;
	}

	bool RunPhysicsGates(const FGateTarget& Target, const std::vector<std::string>& GateIds, std::vector<FGateReport>& OutReports, std::string& OutError)
	{
		OutReports.clear();
		if (Target.Config == nullptr || Target.Tolerances == nullptr)
		{
			OutError = "a gate target needs the resolved config and the tolerances";
			return false;
		}
		const std::vector<IConformanceGate*>& Suite = GetPhysicsGates();
		std::vector<const IConformanceGate*> Selected;
		for (const std::string& Id : GateIds)
		{
			IConformanceGate* Gate = FindPhysicsGate(Id);
			if (Gate == nullptr)
			{
				OutError = "unknown physics gate '" + Id + "' (G1, G1b, G2, G3, G4, G5, G6, G7, G9, J1, G12)";
				return false;
			}
			Selected.push_back(Gate);
		}
		const auto IsSelected = [&Selected](const IConformanceGate* Gate)
		{
			return Selected.empty() || std::find(Selected.begin(), Selected.end(), Gate) != Selected.end();
		};
		// G12 judges the engine error bits of every frame the gates before it ran: selected, it brings the whole suite up to
		// it along (their reports are not returned, so -Gates=G12 still reports one gate).
		const auto G12 = std::find_if(Suite.begin(), Suite.end(), [](const IConformanceGate* Gate) { return std::string_view(Gate->GetId()) == "G12"; });
		const bool bWholeSuiteForG12 = !Selected.empty() && G12 != Suite.end() && IsSelected(*G12);
		if (!PrepareModule(Target, OutError))
		{
			return false;
		}
		FGateRunState State;
		int32 NumRunUnreported = 0;
		for (IConformanceGate* Gate : Suite)
		{
			const bool bReported = IsSelected(Gate);
			if (!bReported && !bWholeSuiteForG12)
			{
				continue;
			}
			FGateReport Report = Gate->Run(Target, State);
			Report.Gate = Gate->GetId();
			Report.Target = Target.GetLabel();
			if (Report.Result == EGateResult::Skip)
			{
				// A Skip passes only where tolerances.json registers it with a reason (Null: no collision, no contacts). Any other
				// - a DLL that declares no contact points, a provisional-revision mismatch hiding PT_CAP_CONTACT_* - fails.
				if (const FAllowedSkip* Allowed = Target.Tolerances->FindAllowedSkip(Report.Gate, Target.Backend, Target.Variant, Target.Profile))
				{
					Report.Note("registered skip (tolerances.json gates." + Report.Gate + ".allowed_skip): " + Allowed->Reason);
				}
				else
				{
					Report.Check(false, "unregistered skip: tolerances.json gates." + Report.Gate + ".allowed_skip has no entry for " + Target.GetEngineLabel() +
						" (a gate is skipped only where its reason is registered)");
				}
			}
			if (!bReported)
			{
				++NumRunUnreported;
				continue;
			}
			if (G12 != Suite.end() && Gate == *G12 && NumRunUnreported > 0)
			{
				Report.Note(Format("selected alone: the %d gates before G12 ran too (their frames are what it judges); only the selected reports are returned",
					NumRunUnreported));
			}
			OutReports.push_back(std::move(Report));
		}
		return true;
	}
}
