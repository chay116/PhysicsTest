// BenchmarkRunner.cpp - FBenchmarkRunner (see BenchmarkRunner.h). One run is FBenchRun, stage by stage (02_Architecture
// 12.2 with the M7 steps): run directory + Fatal hook -> scenario -> host controls -> config -> backend init -> engine-option
// preflight -> scene (BuildScene + flush, desc_sha256) -> effective settings + preflight (required keys, G0, G9, G10a) ->
// PrepareForSimulation -> spin pre-roll -> frame 0 -> timed loop (recorders between frames) -> untimed reads -> outputs ->
// exit.json -> teardown. A failed preflight skips Prepare and the loop but still writes manifest.json (status
// preflight_failed, exit code 2); the manifest always has every group (null where the run stopped earlier).
#include "BenchmarkRunner.h"

#include "BenchChildRuns.h"
#include "BenchCore.h"
#include "BenchPreflight.h"
#include "BenchRecorders.h"
#include "BuildFlagsSha.h"
#include "ConformanceStamp.h"
#include "Containers/StringConv.h"
#include "Abi/AbiGates.h"
#include "BehaviorScenes.h"
#include "GateScenes.h"
#include "Gates/PhysicsGates.h"
#include "Gates/ThreadGate.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Hash/Sha256.h"
#include "Logging/LogMacros.h"
#include "Physics/AnalyticReference.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"
#include "PTGitInfo.h"
#include "RunManifest.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <system_error>
#include <thread>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsBench, Log, All);

namespace PhysicsTest
{
	namespace
	{
		using FJson = nlohmann::ordered_json;
		using BenchRecorders::WriteText;

		// Engine error bits that mean "a capacity was exceeded" (exit code 5; 02 9.4, G12 in M6), as the backend declares
		// them in the effective key errors.capacity_bits (docs/Abi.md 5.14; Jolt: 0x107 = ManifoldCacheFull | BodyPairCacheFull
		// | ContactConstraintsFull | temp allocator fallback). A backend without the key has none: every error bit it reports
		// is an engine error (exit code 3).
		uint32 GetCapacityErrorBits(const std::vector<FEffectiveSetting>& Settings)
		{
			const auto Found = std::find_if(Settings.begin(), Settings.end(), [](const FEffectiveSetting& S) { return S.Key == "errors.capacity_bits"; });
			if (Found == Settings.end() || !Found->bHasCanonical || !(Found->Canonical >= 0.0) || Found->Canonical > 4294967295.0)
			{
				return 0;
			}
			return static_cast<uint32>(Found->Canonical);
		}

		// M10 (R9, docs/TimingContract.md 9): a run's timing is reportable only when the run kept the measurement protocol
		// (timing_valid_for_headline), its conformance stamp is valid and the power plan is High performance / Ultimate. Every
		// condition that fails is a reason; manifest unofficial = !reasons.empty(), timing_label quotes them.
		std::string MakeTimingLabel(const std::vector<std::string>& Reasons)
		{
			if (Reasons.empty())
			{
				return "reportable: headline-valid run, valid conformance stamp, High performance / Ultimate power plan (M10). A headline number is still the "
					"median of fresh-process runs that pass the runner's CV gate (Tools/ptbench, 02 12.5).";
			}
			std::string Text = "UNOFFICIAL: ";
			for (size_t Index = 0; Index < Reasons.size(); ++Index)
			{
				Text += (Index == 0 ? "" : "; ") + Reasons[Index];
			}
			return Text + ". Not for reports.";
		}

		// The CRT this TU (and so every harness TU, one flag profile) was compiled against: /MD, /MDd, /MT, /MTd.
		const char* GetCrtName()
		{
#if defined(_DLL) && defined(_DEBUG)
			return "MDd";
#elif defined(_DLL)
			return "MD";
#elif defined(_DEBUG)
			return "MTd";
#else
			return "MT";
#endif
		}

		// M10: a TSC / QPC pair every this many frames (between frames): the residual of the run's calibration (02 9.3) is read
		// over the whole loop without a QPC read per bucket.
		constexpr int32 CalibrationPairEvery = 60;

		// 02 12.3 manifest fields this milestone does not record yet: none since M10 (an empty object; kept for later fields).
		FJson MakeDeferredFieldsJson()
		{
			return FJson::object();
		}

		std::string ToHexBits(const double Value)
		{
			uint64 Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(Bits));
			char Text[24];
			std::snprintf(Text, sizeof(Text), "%016" PRIx64, Bits);
			return Text;
		}

		// The census #1 thread list (manifest threads.census_init).
		FJson MakeCensusThreadsJson(const FThreadCensus& Census)
		{
			FJson Json;
			Json["valid"] = Census.IsValid();
			Json["error"] = Census.GetError();
			Json["threads"] = Census.GetThreads().size();
			FJson List = FJson::array();
			for (const FCensusThread& Thread : Census.GetThreads())
			{
				List.push_back({ { "tid", Thread.ThreadId }, { "name", Thread.Name }, { "main", Thread.bMain }, { "cycles", Thread.Cycles } });
			}
			Json["list"] = List;
			return Json;
		}

		FJson MakeCensusWindowJson(const FCensusWindow& Window, const Gates::EG10bSkip G10bSkip)
		{
			FJson Json;
			Json["valid"] = Window.bValid;
			Json["error"] = Window.Error;
			Json["window"] = "census #1 right before frame 0 (after the pre-roll), census #2 right after the last frame";
			Json["T"] = Window.Threads;
			Json["wall_s"] = Window.WallSeconds;
			Json["wall_tsc"] = Window.WallTsc;
			Json["own_s"] = Window.GetOwnSeconds();   // M10 fix round 1: own cycles at the window's TSC rate (G10b's minimum window)
			Json["own_cycles"] = Window.OwnCycles;
			Json["main_cycles"] = Window.MainCycles;
			Json["foreign_cycles"] = Window.ForeignCycles;
			Json["process_cycles"] = Window.ProcessCycles;
			Json["unattributed_cycles"] = Window.UnattributedCycles;
			Json["mask_busy_cycles"] = Window.MaskBusyCycles;
			Json["other_on_mask_cycles"] = Window.OtherOnMaskCycles;
			Json["other_on_mask_fraction"] = Window.GetOtherOnMaskFraction();   // M10 fix round 1: G10b's contended rule
			Json["cpu_eff"] = Window.CpuEff;
			Json["cpu_eff_raw"] = Window.CpuEffRaw;
			Json["foreign_cycles_fraction"] = Window.ForeignFraction;
			Json["bracket_cycles"] = Window.BracketCycles;   // M10 fix round 0: census #1's null-bracket baseline
			Json["unattributed_fraction"] = Window.UnattributedFraction;   // max(0, unattributed - bracket) / own: G10b judges foreign + this
			Json["threads_begin"] = Window.ThreadsBegin;
			Json["threads_end"] = Window.ThreadsEnd;
			// M10 fix round 1: why the run's G10b is Skip ("" = a verdict): multi_thread, no_census, short_window, contended.
			Json["g10b_skip"] = Gates::ToString(G10bSkip);
			FJson Threads = FJson::array();
			for (const FCensusThreadDelta& Thread : Window.PerThread)
			{
				Threads.push_back({ { "tid", Thread.ThreadId }, { "name", Thread.Name }, { "main", Thread.bMain }, { "cycles", Thread.Cycles }, { "new", Thread.bNew },
					{ "exited", Thread.bExited } });
			}
			Json["per_thread"] = Threads;
			Json["note"] = "cycles = QueryThreadCycleTime (TSC units) over the window; own = main + foreign (the other threads of this process); "
				"cpu_eff = own / (wall x T - other_on_mask), cpu_eff_raw = own / (wall x T); unattributed = max(0, process - own): a thread born and gone "
				"inside the window, plus what the reads leave (bracket_cycles, measured with nothing inside); unattributed_fraction = max(0, unattributed - "
				"bracket) / own; G10b at T = 1 (own CPU time own_s >= 50 ms): cpu_eff in [0.98, 1.02], foreign_cycles_fraction + unattributed_fraction < 0.5 %; "
				"when other_on_mask_fraction > 5 % (contended) a failure counts only if the same cycles are also >= 0.5 % of mask_busy_cycles, else Skip "
				"(g10b_skip contended: no single-thread proof, not headline-valid)";
			return Json;
		}

		// R diag(I) R^T for a principal frame: the inertia tensor about the COM in body space.
		void MakeInertiaTensor(const FQuat& Rotation, const FVector& Diagonal, double (&Out)[3][3])
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

		// |T_engine - T_harness|_F / |T_harness|_F: independent of how each side orders its principal moments (Jolt sorts
		// them largest first, MassProperties::DecomposePrincipalMomentsOfInertia [V]).
		double InertiaTensorRelError(const FQuat& HarnessRotation, const FVector& HarnessDiagonal, const FQuat& EngineRotation, const FVector& EngineDiagonal)
		{
			double Harness[3][3];
			double Engine[3][3];
			MakeInertiaTensor(HarnessRotation, HarnessDiagonal, Harness);
			MakeInertiaTensor(EngineRotation, EngineDiagonal, Engine);
			double Difference = 0.0;
			double Norm = 0.0;
			for (int Row = 0; Row < 3; ++Row)
			{
				for (int Column = 0; Column < 3; ++Column)
				{
					Difference += (Engine[Row][Column] - Harness[Row][Column]) * (Engine[Row][Column] - Harness[Row][Column]);
					Norm += Harness[Row][Column] * Harness[Row][Column];
				}
			}
			return Norm > 0.0 ? std::sqrt(Difference / Norm) : 0.0;
		}

		std::string ToHex64(const uint64 Value)
		{
			char Text[32];
			std::snprintf(Text, sizeof(Text), "0x%016" PRIx64, Value);
			return Text;
		}

		std::string ToHexMask(const uint64 Value)
		{
			char Text[32];
			std::snprintf(Text, sizeof(Text), "0x%" PRIx64, Value);
			return Text;
		}

		std::filesystem::path GetExecutablePath()
		{
			std::wstring Path(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD Length = GetModuleFileNameW(nullptr, Path.data(), static_cast<DWORD>(Path.size()));
				if (Length < Path.size())
				{
					Path.resize(Length);
					break;
				}
				Path.resize(Path.size() * 2);
			}
			return std::filesystem::path(Path);
		}

		FJson MakeStatsJson(const FDurationStats& Stats)
		{
			FJson Json;
			Json["count"] = Stats.Count;
			Json["mean"] = Stats.Mean;
			Json["median"] = Stats.Median;
			Json["p95"] = Stats.P95;
			Json["p99"] = Stats.P99;
			Json["min"] = Stats.Min;
			Json["max"] = Stats.Max;
			return Json;
		}

		FJson MakeCapabilitiesJson(const uint64 Capabilities)
		{
			static const std::pair<uint64, const char*> Names[] = {
				{ PT_CAP_COLLISION, "COLLISION" }, { PT_CAP_SHAPE_CONVEX, "SHAPE_CONVEX" }, { PT_CAP_SHAPE_TRIMESH_STATIC, "SHAPE_TRIMESH_STATIC" },
				{ PT_CAP_KINEMATIC, "KINEMATIC" }, { PT_CAP_JOINT_BALL, "JOINT_BALL" }, { PT_CAP_JOINT_HINGE, "JOINT_HINGE" }, { PT_CAP_SLEEPING, "SLEEPING" },
				{ PT_CAP_CCD, "CCD" }, { PT_CAP_GYROSCOPIC, "GYROSCOPIC" }, { PT_CAP_MULTITHREADED, "MULTITHREADED" },
				{ PT_CAP_NATIVE_FRAME_MODEL, "NATIVE_FRAME_MODEL" }, { PT_CAP_ACTIVE_LIST, "ACTIVE_LIST" }, { PT_CAP_STATE_WRITE, "STATE_WRITE" },
				{ PT_CAP_NATIVE_HASH, "NATIVE_HASH" }, { PT_CAP_CONTACT_POINTS, "CONTACT_POINTS" }, { PT_CAP_CONTACT_NORMAL_IMPULSE, "CONTACT_NORMAL_IMPULSE" },
				{ PT_CAP_CONTACT_FRICTION_IMPULSE, "CONTACT_FRICTION_IMPULSE" }, { PT_CAP_CONTACT_ACTIVE_EXACT, "CONTACT_ACTIVE_EXACT" },
				{ PT_CAP_NATIVE_ISLANDS, "NATIVE_ISLANDS" }, { PT_CAP_SLEEP_THRESHOLD_INFO, "SLEEP_THRESHOLD_INFO" }, { PT_CAP_BOUNDS, "BOUNDS" },
				{ PT_CAP_JOINT_SNAPSHOT, "JOINT_SNAPSHOT" }, { PT_CAP_NATIVE_DEBUG_DRAW, "NATIVE_DEBUG_DRAW" }, { PT_CAP_PHASE_TIMERS, "PHASE_TIMERS" },
				{ PT_CAP_ALLOC_STATS, "ALLOC_STATS" },
			};
			FJson List = FJson::array();
			for (const auto& [Bit, Name] : Names)
			{
				if ((Capabilities & Bit) != 0)
				{
					List.push_back(Name);
				}
			}
			return List;
		}

		FJson MakeBackendJson(const FBackendInfo& Info, const FBenchConfig& Files, const FResolvedPhysicsConfig& Config, const FPhysicsBackendModule* Module)
		{
			FJson Json;
			Json["name"] = Info.Name;
			Json["display_name"] = Info.DisplayName;
			Json["variant"] = Files.VariantName;
			Json["label"] = !Files.VariantLabel.empty() ? Files.VariantLabel : (Config.Engine.empty() ? Info.DisplayName : Config.Engine + "." + Config.VariantName);
			Json["sdk_version"] = Info.EngineVersion;
			Json["sdk_sha"] = Info.SourceCommit;
			Json["build_config"] = Info.BuildConfig;
			// DLL backends (Null DLL, Chaos): the module file's hash (the M10 stamp key's backend part, 02 12.4); a static
			// backend is part of the exe (harness.exe_sha256).
			if (Module != nullptr && Module->IsDll())
			{
				Json["module_file"] = StringConv::PathToUtf8(Module->GetDllPath().filename());
				Json["module_sha256"] = HashFileSha256(Module->GetDllPath());
			}
			else
			{
				Json["module_file"] = nullptr;
				Json["module_sha256"] = nullptr;
			}
			Json["abi_version"] = std::to_string(PT_ABI_VERSION_MAJOR) + "." + std::to_string(PT_ABI_VERSION_MINOR);
			// M9 fix round 2 (append): the module's own ABI - PtBackendApi.AbiVersion and PtBackendInfo.ProvisionalRevision as the module
			// reported them. abi_version above is the host header's; a v1.0-built DLL under a v1.1 host shows "1.0" / 1 here.
			if (Module != nullptr)
			{
				const uint32 ModuleAbi = Module->GetApi().AbiVersion;
				Json["module_abi_version"] = std::to_string(ModuleAbi >> 16) + "." + std::to_string(ModuleAbi & 0xFFFFu);
				Json["module_provisional_revision"] = Info.ProvisionalRevision;
			}
			else
			{
				Json["module_abi_version"] = nullptr;
				Json["module_provisional_revision"] = nullptr;
			}
			Json["capabilities"] = MakeCapabilitiesJson(Info.Capabilities);
			Json["module_flags"] = Info.ModuleFlags;
			Json["required_effective_keys"] = Info.RequiredEffectiveKeys;
			Json["fidelity_notes"] = Files.FidelityNotes;
			FJson SelfReport = FJson::parse(Info.BuildFlagsJson, nullptr, false);
			Json["tu_self_report"] = SelfReport.is_discarded() ? FJson(Info.BuildFlagsJson) : SelfReport;
			return Json;
		}

		FJson MakeResolvedJson(const FResolvedPhysicsConfig& Config)
		{
			FJson List = FJson::array();
			for (const auto& [Key, Value] : Config.Values)
			{
				FJson Entry;
				Entry["key"] = Key;
				Entry["value"] = Value.Value;
				if (!Value.Unit.empty())
				{
					Entry["unit"] = Value.Unit;
				}
				Entry["origin"] = ToString(Value.Origin);
				Entry["origin_name"] = Value.OriginName;
				Entry["applied"] = Value.bApplied;
				List.push_back(Entry);
			}
			return List;
		}

		// ---- M9 outputs ---------------------------------------------------------------------------------------------------
		FJson ToJson(const PtTransform& T)
		{
			return FJson{ { "rotation", { T.Rotation.X, T.Rotation.Y, T.Rotation.Z, T.Rotation.W } }, { "translation", { T.Translation.X, T.Translation.Y, T.Translation.Z } } };
		}

		FJson ToJsonOrNull(const bool bHas, const double Value)
		{
			return bHas && std::isfinite(Value) ? FJson(Value) : FJson(nullptr);
		}

		const char* CombineModeName(const uint32 Mode)
		{
			switch (Mode)
			{
			case PT_COMBINE_AVERAGE:  return "Average";
			case PT_COMBINE_MIN:      return "Min";
			case PT_COMBINE_MULTIPLY: return "Multiply";
			case PT_COMBINE_MAX:      return "Max";
			default:                  return "?";
			}
		}

		// scene_desc.json (physicstest.scene_desc/1, M9): the scene as the backend received it - materials, bodies with their
		// marshalled shapes, mass properties and initial pose, joints - so the behaviour analysis (Tools/ptbench, no C++) can
		// compute geometry penetration, joint stretch and energies from states.ptst. Written before frame 0 (untimed) when
		// the run records states. Units cm, kg, s, rad; poses are actor origins, quaternions (x, y, z, w).
		FJson MakeSceneDescJson(const FPhysScene& Scene, const IPhysicsTestScenario& Scenario, const FScenarioParams& Params, const std::string& DescSha256,
			std::string& OutError)
		{
			FPhysicsBackendScene& Backend = Scene.GetBackendScene();
			std::vector<FBodyInstance*> Bodies(Backend.GetNumBodies(), nullptr);
			for (uint32 Index = 0; Index < Backend.GetNumBodies(); ++Index)
			{
				Bodies[Index] = Backend.GetBody(Index);
				if (Bodies[Index] == nullptr)
				{
					OutError = "body " + std::to_string(Index) + " is released: scene_desc.json describes the scene before frame 0";
					return FJson();
				}
			}
			FMarshalledActorBatch Batch;
			if (!Backend.MarshalBodies(Bodies, 0, Scene.GetSettings(), Batch, OutError))
			{
				return FJson();
			}
			FJson Json;
			Json["schema"] = "physicstest.scene_desc/1";
			Json["scenario"] = Scenario.GetName();
			Json["version"] = Scenario.GetVersion();
			Json["params"] = Params;
			Json["desc_sha256"] = DescSha256;
			Json["units"] = "cm, kg, s, rad; poses = actor origin; quaternion (x, y, z, w); box half extents; capsule radius + half segment along local Z";
			Json["gravity"] = { Scene.GetGravity().X, Scene.GetGravity().Y, Scene.GetGravity().Z };
			FJson Materials = FJson::array();
			const std::span<const FPhysicalMaterial* const> Sources = Backend.GetMaterialSources();
			for (size_t Index = 0; Index < Sources.size(); ++Index)
			{
				const PtPhysicalMaterial M = MarshalPhysicalMaterial(*Sources[Index], Scene.GetSettings());
				Materials.push_back({ { "index", Index }, { "name", Sources[Index]->Name }, { "friction", M.Friction }, { "static_friction", M.StaticFriction },
					{ "restitution", M.Restitution }, { "friction_combine", CombineModeName(M.FrictionCombineMode) },
					{ "restitution_combine", CombineModeName(M.RestitutionCombineMode) } });
			}
			Json["materials"] = Materials;
			FJson BodiesJson = FJson::array();
			for (size_t Index = 0; Index < Batch.Actors.size(); ++Index)
			{
				const PtActorCreateInfo& A = Batch.Actors[Index];
				FJson Shapes = FJson::array();
				for (uint32 Shape = A.FirstShape; Shape < A.FirstShape + A.NumShapes; ++Shape)
				{
					const PtShapeElem& E = Batch.Shapes[Shape];
					FJson S{ { "type", E.Type == PT_SHAPE_SPHERE ? "sphere" : E.Type == PT_SHAPE_BOX ? "box" : E.Type == PT_SHAPE_CAPSULE ? "capsule" : "convex" },
						{ "local", ToJson(E.LocalTransform) }, { "material", E.MaterialIndex } };
					if (E.Type == PT_SHAPE_SPHERE || E.Type == PT_SHAPE_CAPSULE)
					{
						S["radius"] = E.Radius;
					}
					if (E.Type == PT_SHAPE_CAPSULE)
					{
						S["half_length"] = E.HalfLength;
					}
					if (E.Type == PT_SHAPE_BOX)
					{
						S["half_extent"] = { E.BoxHalfExtent.X, E.BoxHalfExtent.Y, E.BoxHalfExtent.Z };
					}
					Shapes.push_back(S);
				}
				BodiesJson.push_back({ { "index", A.BodyIndex }, { "name", Bodies[Index]->BodyName },
					{ "motion", A.Motion == PT_MOTION_STATIC ? "static" : (A.Motion == PT_MOTION_KINEMATIC ? "kinematic" : "dynamic") }, { "mass", A.Mass },
					{ "inertia_diagonal", { A.InertiaDiagonal.X, A.InertiaDiagonal.Y, A.InertiaDiagonal.Z } }, { "com_local", ToJson(A.CenterOfMassLocal) },
					{ "initial_pose", ToJson(A.ActorTransform) }, { "linear_damping", A.LinearDamping }, { "angular_damping", A.AngularDamping },
					{ "shapes", Shapes } });
			}
			Json["bodies"] = BodiesJson;
			FJson Joints = FJson::array();
			const std::span<FConstraintInstance* const> Constraints = Scene.GetConstraints();
			const std::span<const PtConstraintCreateInfo> Marshalled = Scene.GetMarshalledConstraints();
			for (size_t Index = 0; Index < Marshalled.size() && Index < Constraints.size(); ++Index)
			{
				const PtConstraintCreateInfo& C = Marshalled[Index];
				Joints.push_back({ { "index", Index }, { "name", Constraints[Index]->JointName }, { "type", C.Type == PT_JOINT_BALL ? "ball" : "hinge" },
					{ "body1", C.Body1 }, { "body2", C.Body2 == PT_INDEX_NONE ? FJson(-1) : FJson(C.Body2) }, { "frame1", ToJson(C.Frame1) },
					{ "frame2", ToJson(C.Frame2) }, { "disable_collision", (C.Flags & PT_JOINTF_DISABLE_COLLISION) != 0 },
					{ "simulated", Constraints[Index]->IsSimulated() } });
			}
			Json["joints"] = Joints;
			Json["joints_note"] = "body2 -1 = the world (frame2 is then a world pose); frames in each body's actor space; hinge axis = frame X (docs/Abi.md 9.4)";
			return Json;
		}

		// preflight_diff.json (physicstest.preflight_diff/1, M9, 02 10): every effective key of the run next to what the
		// profile, variant and scene resolved for it (the G9 rule's expectation), with the comparison's status.
		FJson MakePreflightDiffJson(const std::vector<Gates::FReadbackDiffRow>& Rows, const FResolvedPhysicsConfig& Config, const std::string& Backend,
			const std::string& Variant)
		{
			FJson Json;
			Json["schema"] = "physicstest.preflight_diff/1";
			Json["backend"] = Backend;
			Json["variant"] = Variant;
			Json["profile"] = Config.ProfileName;
			std::map<std::string, int32> Counts;
			FJson List = FJson::array();
			for (const Gates::FReadbackDiffRow& Row : Rows)
			{
				++Counts[Row.Status];
				List.push_back({ { "key", Row.Key }, { "source", Row.Source }, { "effective", ToJsonOrNull(Row.bHasEffective, Row.Effective) },
					{ "native", Row.Native }, { "rule", Row.Rule }, { "resolved", ToJsonOrNull(Row.bHasResolved, Row.Resolved) }, { "status", Row.Status },
					{ "note", Row.Note } });
			}
			Json["counts"] = Counts;
			Json["keys"] = List;
			Json["status_note"] = "match / mismatch: compared with the resolved value (G9 rule); unchecked: registered as not profile-derived; skipped: not "
				"compared on this scene; no_rule: READBACK without a rule (G9 fails); not_readback: SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT, recorded only";
			return Json;
		}

		FJson MakeForcedOverridesJson(const FResolvedPhysicsConfig& Config, const FPhysScene* Scene)
		{
			FJson Json;
			FJson Entries = FJson::array();
			for (const FForcedOverride& Override : Config.ForcedOverrides)
			{
				Entries.push_back({ { "key", Override.GetKey() }, { "value", Override.Value }, { "origin", Override.OriginName } });
			}
			Json["config"] = Entries;
			if (Scene == nullptr)
			{
				Json["applied"] = nullptr;
				return Json;
			}
			const FForcedOverrideReport& Report = Scene->GetForcedOverrideReport();
			Json["applied"] = Report.bEnabled;
			Json["materials"] = Report.NumMaterials;
			Json["materials_changed"] = Report.NumMaterialsChanged;
			Json["foreign_materials"] = Report.NumForeignMaterials;
			Json["bodies"] = Report.NumBodies;
			Json["bodies_changed"] = Report.NumBodiesChanged;
			Json["changes"] = Report.Changes;
			return Json;
		}

		FJson ParseEffectiveToJson(const std::string& Lines)
		{
			FJson List = FJson::array();
			size_t Start = 0;
			while (Start < Lines.size())
			{
				const size_t End = Lines.find('\n', Start);
				const std::string Line = Lines.substr(Start, End == std::string::npos ? std::string::npos : End - Start);
				if (!Line.empty())
				{
					FJson Parsed = FJson::parse(Line, nullptr, false);
					List.push_back(Parsed.is_discarded() ? FJson(Line) : Parsed);
				}
				if (End == std::string::npos)
				{
					break;
				}
				Start = End + 1;
			}
			return List;
		}

		std::string DumpModuleEffectiveSettings(const FPhysicsBackendModule& Module)
		{
			const PtBackendApi& Api = Module.GetApi();
			PtTextBuffer Query = {};
			Api.DumpEffectiveSettings(0, &Query);
			std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
			PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
			if (Api.DumpEffectiveSettings(0, &Buffer) != PT_OK)
			{
				return std::string();
			}
			Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
			return Text;
		}

		FJson MakeGateReportJson(const FGateReport& Report)
		{
			FJson Entry;
			Entry["id"] = Report.Gate;
			Entry["result"] = ToString(Report.Result);
			Entry["checks"] = Report.NumChecks;
			Entry["failures"] = Report.Failures;
			Entry["notes"] = Report.Notes;
			return Entry;
		}

		// The first, middle and last dynamic body: the engine's mass properties against the harness values (02 10).
		FJson SampleMassProperties(FPhysScene& Scene)
		{
			std::vector<FBodyInstance*> Dynamic;
			for (FBodyInstance* Body : Scene.GetBodies())
			{
				if (Body->IsDynamic() && Body->ActorHandle.IsValid())
				{
					Dynamic.push_back(Body);
				}
			}
			FJson List = FJson::array();
			if (Dynamic.empty())
			{
				return List;
			}
			const size_t Picks[3] = { 0, Dynamic.size() / 2, Dynamic.size() - 1 };
			for (size_t Pick = 0; Pick < 3; ++Pick)
			{
				if (Pick > 0 && Picks[Pick] == Picks[Pick - 1])
				{
					continue;
				}
				const FBodyInstance& Body = *Dynamic[Picks[Pick]];
				PtMassProperties Engine = {};
				const PtResult Result = Scene.GetBackendScene().ReadMassProperties(Body.ActorHandle.Index, std::span<PtMassProperties>(&Engine, 1));
				FJson Entry;
				Entry["body_index"] = Body.ActorHandle.Index;
				Entry["harness_mass_kg"] = Body.MassProperties.Mass;
				Entry["harness_inertia_kgcm2"] = { Body.MassProperties.InertiaTensor.X, Body.MassProperties.InertiaTensor.Y, Body.MassProperties.InertiaTensor.Z };
				if (Result == PT_OK)
				{
					// The engine's principal diagonal may be a permutation of the harness one (its own principal frame): the
					// tensor error is the comparable number (G2, M6).
					const FQuat EngineRotation(Engine.CenterOfMassLocal.Rotation.X, Engine.CenterOfMassLocal.Rotation.Y, Engine.CenterOfMassLocal.Rotation.Z,
						Engine.CenterOfMassLocal.Rotation.W);
					const FVector EngineDiagonal(Engine.InertiaDiagonal.X, Engine.InertiaDiagonal.Y, Engine.InertiaDiagonal.Z);
					Entry["engine_mass_kg"] = Engine.Mass;
					Entry["engine_inertia_kgcm2"] = { Engine.InertiaDiagonal.X, Engine.InertiaDiagonal.Y, Engine.InertiaDiagonal.Z };
					Entry["mass_rel_error"] = Body.MassProperties.Mass > 0.0 ? std::abs(Engine.Mass - Body.MassProperties.Mass) / Body.MassProperties.Mass : 0.0;
					Entry["inertia_tensor_rel_error"] = InertiaTensorRelError(Body.MassProperties.RotationOfMass, Body.MassProperties.InertiaTensor, EngineRotation,
						EngineDiagonal);
				}
				else
				{
					Entry["engine_error"] = GetPtResultName(Result);
				}
				List.push_back(Entry);
			}
			return List;
		}

		// "Every N" frames: the frames f with (f + 1) % N == 0, plus the final frame (docs/ResultsSchema.md). N = 0: the final
		// frame only.
		bool IsRecordedFrame(const int32 Every, const int32 Frame, const int32 Frames)
		{
			return Frame == Frames - 1 || (Every > 0 && (Frame + 1) % Every == 0);
		}

		int32 CountRecordedFrames(const int32 Every, const int32 Frames)
		{
			int32 Count = 0;
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Count += IsRecordedFrame(Every, Frame, Frames) ? 1 : 0;
			}
			return Count;
		}

		// ---- the Fatal hook (02 12.2: the runner trusts exit.json) ---------------------------------------------------------
		// Run phase, until the run's final exit.json: a Fatal writes exit.json with code 3 (status fatal). Teardown phase, after
		// it: the verdict is final (DL52, docs/TimingContract.md 1) - the Fatal goes to teardown.json and the process ends with
		// the run's own code through TerminateProcess. With a PT_MODULE_UE_RUNTIME module loaded both phases end with
		// TerminateProcess, so a UE DLL never runs DLL_PROCESS_DETACH after a Fatal either (02 12.2 step 13).
		enum class EFatalHookPhase : uint8
		{
			Off,
			Run,
			Teardown,
		};

		struct FFatalHookState
		{
			std::filesystem::path OutDir;            // set before Phase leaves Off
			std::string RunId;
			std::atomic<EFatalHookPhase> Phase{ EFatalHookPhase::Off };
			std::atomic<int32> RunExitCode{ 0 };     // teardown phase: the code the final exit.json holds
		};
		FFatalHookState GFatalHook;

		std::string TrimLineEnd(const char* Message)
		{
			std::string Text = Message != nullptr ? Message : "";
			while (!Text.empty() && (Text.back() == '\n' || Text.back() == '\r'))
			{
				Text.pop_back();
			}
			return Text;
		}

		void OnFatalExit(const int32 ExitCode, const char* Message)
		{
			const EFatalHookPhase Phase = GFatalHook.Phase.load();
			const bool bUeRuntime = FBenchmarkRunner::HasUeRuntimeModule();
			if (Phase == EFatalHookPhase::Off)
			{
				// Outside a run, -Conformance or -Gate= (-Describe, -List, -SceneHashes): no exit.json to write, but a UE DLL must
				// not see DLL_PROCESS_DETACH after a Fatal either (M7 review, ChaosBackendGuide 2).
				if (bUeRuntime)
				{
					FBenchmarkRunner::TerminateProcessNow(ExitCode, "a Fatal outside a run with a PT_MODULE_UE_RUNTIME module loaded");
				}
				return;   // RequestExitFatal ends the process with ExitCode
			}
			if (Phase == EFatalHookPhase::Run)
			{
				FExitRecord Record;
				Record.Code = static_cast<EBenchExit>(ExitCode);
				Record.Status = "fatal";
				Record.Category = "engine";
				Record.Reason = TrimLineEnd(Message);
				Record.RunId = GFatalHook.RunId;
				Record.bTerminateProcess = bUeRuntime;
				Record.WriteAndFlush(GFatalHook.OutDir);
				if (bUeRuntime)
				{
					FBenchmarkRunner::TerminateProcessNow(ExitCode, "a Fatal with a PT_MODULE_UE_RUNTIME module loaded, after exit.json");
				}
				return;   // RequestExitFatal ends the process with ExitCode
			}
			FJson Teardown;
			Teardown["schema"] = "physicstest.teardown/1";
			Teardown["teardown_ns"] = nullptr;
			Teardown["term_game_phys"] = nullptr;
			Teardown["fatal"] = TrimLineEnd(Message);
			Teardown["terminate_process"] = true;
			Teardown["note"] = "a Fatal during the teardown, after exit.json: exit.json keeps the run's verdict and the process ends with its code through "
				"TerminateProcess (DL52)";
			WriteText(GFatalHook.OutDir / "teardown.json", Teardown.dump(2) + "\n");
			FBenchmarkRunner::TerminateProcessNow(GFatalHook.RunExitCode.load(), "a Fatal during the teardown, after exit.json (the run's code stands)");
		}


		// The per-process host controls of one run, applied before the backend is loaded (02 12.2 step 2).
		struct FHostControls
		{
			uint64 Mask = 0;
			uint64 CoreMask = 0;                     // every logical CPU of the mask's cores (the SMT siblings included)
			uint64 SiblingMask = 0;
			FProcessorTopology::FMaskShape Shape;
			bool bAffinity = false;
			bool bPriority = false;
			bool bThrottling = false;
			bool bTimer = false;
			std::vector<std::string> Errors;
			FTimerResolution TimerBefore;
			FTimerResolution TimerAfter;
			std::string PriorityBefore;
			uint64 AffinityBefore = 0;
			[[nodiscard]] bool AllApplied() const { return bAffinity && bPriority && bThrottling && bTimer; }
		};

		FJson MakeTimerJson(const FTimerResolution& Timer)
		{
			if (!Timer.bValid)
			{
				return nullptr;
			}
			return FJson{ { "current_ms", Timer.CurrentMs }, { "finest_ms", Timer.FinestMs }, { "coarsest_ms", Timer.CoarsestMs } };
		}

		bool IsHighPerformanceScheme(const FHostFacts& Host)
		{
			// The Windows "High performance" and "Ultimate Performance" schemes (powercfg /list GUIDs).
			std::string Guid = Host.PowerSchemeGuid;
			for (char& C : Guid)
			{
				C = (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
			}
			return Guid.find("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c") != std::string::npos || Guid.find("e9a42b02-d5df-448d-aa00-03f14749eb61") != std::string::npos;
		}

		// One window of summary.windows (02 9.5, docs/ResultsSchema.md 2). Frame 0 is never in a window. A run shorter than the
		// window clips it: "last" is the last frame the window covered in this run and "clipped" says so; a window without a
		// frame has null statistics (never a 0 ns time).
		FJson MakeWindowJson(const FStepWindow& Window, const std::vector<FFrameRecord>& Records, const double NsPerTick)
		{
			std::vector<double> FrameNs;
			std::vector<double> StepNs;
			std::vector<double> PushNs;
			std::vector<double> PullNs;
			std::vector<double> TickNs;
			const int64 LastRun = static_cast<int64>(Records.size()) - 1;
			const int64 Last = std::min<int64>(static_cast<int64>(Window.Last), LastRun);
			const int64 First = std::max<int64>(static_cast<int64>(Window.First), 1);
			for (int64 Frame = First; Frame <= Last; ++Frame)
			{
				const FFrameRecord& Record = Records[static_cast<size_t>(Frame)];
				const FFrameTiming& Timing = Record.Timing;
				FrameNs.push_back(static_cast<double>(Timing.FrameNs));
				StepNs.push_back(static_cast<double>(Timing.StepNs));
				PushNs.push_back(static_cast<double>(Timing.PushNs));
				PullNs.push_back(static_cast<double>(Timing.PullNs));
				TickNs.push_back(static_cast<double>(Record.TickTsc) * NsPerTick);
			}
			const auto Stats = [](const std::vector<double>& Values) { return Values.empty() ? FJson(nullptr) : MakeStatsJson(ComputeDurationStats(Values)); };
			FJson Entry;
			Entry["first"] = Window.First;
			Entry["last"] = FrameNs.empty() ? FJson(nullptr) : FJson(Last);
			Entry["declared_last"] = Window.Last;
			Entry["clipped"] = LastRun < static_cast<int64>(Window.Last);
			Entry["count"] = FrameNs.size();
			Entry["frame_ns"] = Stats(FrameNs);
			Entry["step_ns"] = Stats(StepNs);
			Entry["push_ns"] = Stats(PushNs);
			Entry["pull_ns"] = Stats(PullNs);
			// M10: the whole Tick per frame (two TSC reads around FPhysScene::Tick: the buckets plus the harness's own work inside
			// Tick) and the batch measurement of the window (02 9.3, M7 D-6): one TSC interval from the start of the first frame's
			// Tick to the end of the last one, divided by the frames - no per-bucket rounding, and the bench's per-frame row work
			// between the Ticks included. For P0 / P1 the difference to T_frame is the per-frame overhead floor.
			Entry["tick_ns"] = Stats(TickNs);
			if (!FrameNs.empty() && NsPerTick > 0.0)
			{
				const FFrameRecord& FirstRecord = Records[static_cast<size_t>(First)];
				const FFrameRecord& LastRecord = Records[static_cast<size_t>(Last)];
				const uint64 BatchBegin = FirstRecord.TscEnd - FirstRecord.TickTsc;
				Entry["batch_frame_ns"] = LastRecord.TscEnd > BatchBegin ? FJson(static_cast<double>(LastRecord.TscEnd - BatchBegin) * NsPerTick
					/ static_cast<double>(FrameNs.size())) : FJson(nullptr);
			}
			else
			{
				Entry["batch_frame_ns"] = nullptr;
			}
			return Entry;
		}
	}

	// ---- FRunManifest / FExitRecord / EBenchExit ---------------------------------------------------------------------
	bool FRunManifest::Write(const std::filesystem::path& Path) const
	{
		return WriteText(Path, Json.dump(2) + "\n");
	}

	std::string FRunManifest::ComputeComparabilityKey() const
	{
		const auto Field = [this](const char* Group, const char* Key) -> nlohmann::json
		{
			if (!Json.contains(Group) || !Json[Group].is_object() || !Json[Group].contains(Key))
			{
				return nullptr;
			}
			return nlohmann::json::parse(Json[Group][Key].dump());
		};
		nlohmann::json Key = nlohmann::json::object();   // std::map: keys sorted (the canonical form)
		Key["schema"] = "physicstest.comparability/1";
		Key["scene_sha256"] = Field("scene", "scene_sha256");
		Key["desc_sha256"] = Field("scene", "desc_sha256");
		Key["physics_sha256"] = Field("profile", "physics_sha256");
		Key["mass_rule"] = Field("profile", "mass_rule");
		Key["flag_profile"] = Field("build", "flag_profile");
		Key["tick_mode"] = Field("run", "tick_mode");
		const nlohmann::json Dt = Field("run", "dt_s");
		Key["dt_bits"] = Dt.is_number() ? nlohmann::json(ToHexBits(Dt.get<double>())) : nlohmann::json(nullptr);
		Key["T"] = Field("threads", "T");
		Key["thread_series"] = Field("threads", "series");
		Key["sync_policy"] = Field("run", "sync_policy");
		Key["mode"] = Field("run", "mode");
		nlohmann::json Instrumentation = Field("run", "instrumentation");
		if (Instrumentation.is_array())
		{
			std::vector<std::string> Names;
			for (const nlohmann::json& Name : Instrumentation)
			{
				Names.push_back(Name.is_string() ? Name.get<std::string>() : Name.dump());
			}
			std::sort(Names.begin(), Names.end());
			Instrumentation = Names;
		}
		Key["instrumentation"] = Instrumentation;
		const nlohmann::json Sha = Field("harness", "git_sha");
		const nlohmann::json Dirty = Field("harness", "dirty");
		Key["harness_commit"] = (Sha.is_string() ? Sha.get<std::string>() : std::string()) + (Dirty.is_boolean() && Dirty.get<bool>() ? "+dirty" : "");
		return FSHA256::HashString(Key.dump());
	}

	const char* ToString(const EBenchExit Exit)
	{
		switch (Exit)
		{
		case EBenchExit::Ok: return "ok";
		case EBenchExit::GateFailed: return "gate_or_preflight_or_usage";
		case EBenchExit::EngineError: return "engine_error";
		case EBenchExit::Contaminated: return "contaminated_host";
		case EBenchExit::Capacity: return "capacity_exceeded";
		case EBenchExit::BackendUnavailable: return "backend_unavailable";
		default: return "?";
		}
	}

	bool FExitRecord::WriteAndFlush(const std::filesystem::path& OutDir) const
	{
		FJson Json;
		Json["schema"] = "physicstest.exit/1";
		Json["exit_code"] = static_cast<int32>(Code);
		Json["meaning"] = ToString(Code);
		Json["status"] = Status;
		Json["category"] = Category;
		Json["reason"] = Reason;
		Json["run_id"] = RunId;
		Json["utc"] = GetUtcTimestamp();
		Json["pid"] = static_cast<uint32>(GetCurrentProcessId());
		Json["terminate_process"] = bTerminateProcess;
		Json["outputs"] = Outputs;
		Json["note"] = "written last, after every other output (02_Architecture 12.2): the runner trusts this file over the process exit code";
		return BenchRecorders::WriteTextAtomic(OutDir / "exit.json", Json.dump(2) + "\n");
	}

	// ---- FTimingRecorder ---------------------------------------------------------------------------------------------
	void FTimingRecorder::Recalibrate(const double InNsPerTick)
	{
		NsPerTick = InNsPerTick;
		if (!(NsPerTick > 0.0))
		{
			return;
		}
		const auto ToNs = [this](const uint64 Ticks) { return static_cast<uint64>(static_cast<double>(Ticks) * NsPerTick + 0.5); };
		for (FFrameRecord& Record : Records)
		{
			FFrameTiming& T = Record.Timing;
			T.PushNs = ToNs(T.PushTsc);
			T.SetupNs = ToNs(T.SetupTsc);
			T.StartNs = ToNs(T.StartTsc);
			T.WaitNs = ToNs(T.WaitTsc);
			T.EndNs = ToNs(T.EndTsc);
			T.StepNs = ToNs(T.StepTsc);
			T.PullNs = ToNs(T.PullTsc);
			T.FrameNs = T.PushNs + T.StepNs + T.PullNs;   // T_frame = sum(T_push) + sum(T_step) + T_pull (02 9.3)
		}
	}

	bool FTimingRecorder::WriteCsv(const std::filesystem::path& Path) const
	{
		std::string Text = "frame,sim_time_s,num_steps,push_ns,setup_ns,start_ns,wait_ns,end_ns,step_ns,pull_ns,frame_ns,frame_qpc_ticks,solver_ns,awake_bodies,"
			"contact_pairs,contact_points,islands,error_bits,frame_tsc_ticks,tick_ns,tick_tsc_ticks,tsc_end,qpc_end\n";
		Text.reserve(Text.size() + Records.size() * 192);
		char Line[640];
		for (size_t Frame = 0; Frame < Records.size(); ++Frame)
		{
			const FFrameRecord& R = Records[Frame];
			const FFrameTiming& T = R.Timing;
			char Solver[32] = "";
			if (R.SolverNs >= 0)
			{
				std::snprintf(Solver, sizeof(Solver), "%" PRId64, R.SolverNs);
			}
			char TickNs[32] = "";
			if (NsPerTick > 0.0 && R.TickTsc > 0)
			{
				std::snprintf(TickNs, sizeof(TickNs), "%" PRIu64, static_cast<uint64>(static_cast<double>(R.TickTsc) * NsPerTick + 0.5));
			}
			std::snprintf(Line, sizeof(Line), "%zu,%.17g,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
				",%" PRIu64 ",%s,%d,%d,%d,%d,%u,%" PRIu64 ",%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", Frame, R.SimTime, T.NumSteps, T.PushNs, T.SetupNs, T.StartNs,
				T.WaitNs, T.EndNs, T.StepNs, T.PullNs, T.FrameNs, T.FrameQpcTicks, Solver, R.AwakeBodies, R.ContactPairs, R.ContactPoints, R.Islands,
				T.EngineErrorBits, T.FrameTsc, TickNs, R.TickTsc, R.TscEnd, R.QpcEnd);
			Text += Line;
		}
		return WriteText(Path, Text);
	}

	// ---- UE_RUNTIME process end ----------------------------------------------------------------------------------------
	bool FBenchmarkRunner::HasUeRuntimeModule()
	{
		return FPhysicsBackendRegistry::Get().AnyLoadedModuleHas(PT_MODULE_UE_RUNTIME);
	}

	void FBenchmarkRunner::TerminateProcessNow(const int32 Code, const char* Why)
	{
		UE_LOG(LogPhysicsBench, Display, "TerminateProcess(GetCurrentProcess(), %d): %s (02 12.2 step 13)", Code,
			Why != nullptr ? Why : "PT_MODULE_UE_RUNTIME module loaded, after the outputs");
		std::fflush(stdout);
		std::fflush(stderr);
		TerminateProcess(GetCurrentProcess(), static_cast<UINT>(Code));
		std::_Exit(Code);   // not reached
	}

	// ---- the Fatal hook (run, -Conformance, -Gate=; process-wide fallback) ----------------------------------------------
	FFatalExitScope::FFatalExitScope(const std::filesystem::path& OutDir, const std::string& RunId)
	{
		GFatalHook.OutDir = OutDir;
		GFatalHook.RunId = RunId;
		GFatalHook.RunExitCode.store(0);
		GFatalHook.Phase.store(EFatalHookPhase::Run);
		FPlatformMisc::SetFatalExitCallback(&OnFatalExit);
	}

	FFatalExitScope::~FFatalExitScope()
	{
		// Disarmed; the callback stays (InstallFatalHook's fallback: a UE_RUNTIME process still ends with TerminateProcess).
		GFatalHook.Phase.store(EFatalHookPhase::Off);
	}

	void FFatalExitScope::EnterTeardown(const EBenchExit RunExit)
	{
		GFatalHook.RunExitCode.store(static_cast<int32>(RunExit));
		GFatalHook.Phase.store(EFatalHookPhase::Teardown);
	}

	void FBenchmarkRunner::InstallFatalHook()
	{
		FPlatformMisc::SetFatalExitCallback(&OnFatalExit);
	}

	// ---- -AnalyticReferences (M8) ----------------------------------------------------------------------------------------
	// The canonical inputs are the gate scenes' (GateScenes.h): S00 / S00d free fall from z0 1000 cm under -980 cm/s^2 for 60
	// steps (S00d: linear damping 0.5 1/s, both declared formulas), S06 friction slide 500 cm/s with mu 0.5, S05 touch step
	// K = 38. Tools/ptbench/analytic.py has the same case table and recomputes every value from these inputs (CTest
	// PhysicsTest.Py.AnalyticMatchesCpp: |difference| <= 1e-9, DL23).
	EBenchExit FBenchmarkRunner::AnalyticReferences()
	{
		const double DtEngine = Analytic::EngineDeltaTime(1.0f / 60.0f);
		FJson Cases = FJson::array();
		const auto AddDt = [&Cases](const char* Id, const double Dt)
		{
			Cases.push_back({ { "id", Id }, { "function", "EngineDeltaTime" }, { "inputs", { { "dt", Dt } } }, { "values", { { "dt_engine", Analytic::EngineDeltaTime(Dt) } } } });
		};
		AddDt("dt_engine.1_60", 1.0 / 60.0);
		AddDt("dt_engine.1_30", 1.0 / 30.0);
		AddDt("dt_engine.1_120", 1.0 / 120.0);
		AddDt("dt_engine.1_50", 1.0 / 50.0);
		const auto AddFreeFall = [&Cases](const char* Id, const double Z0, const double V0, const double GravityZ, const double Dt, const int32 Steps)
		{
			const Analytic::FFreeFallState State = Analytic::SemiImplicitEulerFreeFall(Z0, V0, GravityZ, Dt, Steps);
			Cases.push_back({ { "id", Id }, { "function", "SemiImplicitEulerFreeFall" },
				{ "inputs", { { "z0", Z0 }, { "v0", V0 }, { "gravity_z", GravityZ }, { "dt", Dt }, { "steps", Steps } } },
				{ "values", { { "z", State.Z }, { "vz", State.Vz } } } });
		};
		AddFreeFall("S00.free_fall_dt_engine", 1000.0, 0.0, -980.0, DtEngine, 60);
		AddFreeFall("S00.free_fall_exact_1_60", 1000.0, 0.0, -980.0, 1.0 / 60.0, 60);
		AddFreeFall("free_fall_v0_30_steps", 500.0, 250.0, -980.0, DtEngine, 30);
		const auto AddDamped = [&Cases](const char* Id, const double LinearDamping, const double Dt, const int32 Steps, const Analytic::EDampingFormula Formula)
		{
			const Analytic::FFreeFallState State = Analytic::DampedFreeFall(1000.0, -980.0, LinearDamping, Dt, Steps, Formula);
			Cases.push_back({ { "id", Id }, { "function", "DampedFreeFall" },
				{ "inputs", { { "z0", 1000.0 }, { "gravity_z", -980.0 }, { "linear_damping", LinearDamping }, { "dt", Dt }, { "steps", Steps },
					{ "formula", static_cast<int32>(Formula) } } },
				{ "values", { { "z", State.Z }, { "vz", State.Vz } } } });
		};
		AddDamped("S00d.damped_ue_order", GateScenes::S00d::LinearDamping, DtEngine, GateScenes::S00d::Frames, Analytic::EDampingFormula::UEOrder);
		AddDamped("S00d.damped_before_forces", GateScenes::S00d::LinearDamping, DtEngine, GateScenes::S00d::Frames, Analytic::EDampingFormula::BeforeForces);
		AddDamped("damped_heavy_c80", 80.0, DtEngine, 60, Analytic::EDampingFormula::UEOrder);   // c dt >= 1: the multiplier clamps to 0
		const auto AddFriction = [&Cases](const char* Id, const double V0, const double Mu, const double G, const double Dt)
		{
			Cases.push_back({ { "id", Id }, { "function", "FrictionSlideStopDistance" }, { "inputs", { { "v0", V0 }, { "mu", Mu }, { "g", G }, { "dt", Dt } } },
				{ "values", { { "distance", Analytic::FrictionSlideStopDistance(V0, Mu, G, Dt) } } } });
		};
		AddFriction("S06.friction_stop", GateScenes::S06::V0, GateScenes::S06::Friction, 980.0, DtEngine);
		AddFriction("friction_stop_mu_0_3", 300.0, 0.3, 980.0, DtEngine);
		const auto AddDrop = [&Cases](const char* Id, const double G, const double Dt, const int32 K)
		{
			Cases.push_back({ { "id", Id }, { "function", "StepBoundaryDropHeight" }, { "inputs", { { "g", G }, { "dt", Dt }, { "k", K } } },
				{ "values", { { "height", Analytic::StepBoundaryDropHeight(G, Dt, K) }, { "touch_speed", G * Dt * static_cast<double>(K) } } } });
		};
		AddDrop("S05.drop_height_k38", 980.0, DtEngine, GateScenes::S05::TouchStep);
		AddDrop("drop_height_k10", 980.0, DtEngine, 10);
		// M9 behaviour references (BehaviorScenes.h, docs/Scenes.md 2.17, 2.19): the M6a pendulum periods (03 M9: 2.00905 /
		// 2.04305 / 2.37023 s) and the M4 discrete slide d(2 s) = d after 120 steps.
		const auto AddPendulum = [&Cases](const char* Id, const double AngleDegrees)
		{
			const double Amplitude = AngleDegrees * 3.14159265358979323846 / 180.0;
			Cases.push_back({ { "id", Id }, { "function", "PendulumPeriod" },
				{ "inputs", { { "length", BehaviorScenes::M6a::Length }, { "bob_radius", BehaviorScenes::M6a::BobRadius }, { "g", 980.0 }, { "amplitude_deg", AngleDegrees } } },
				{ "values", { { "period", Analytic::PendulumPeriod(BehaviorScenes::M6a::Length, BehaviorScenes::M6a::BobRadius, 980.0, Amplitude) } } } });
		};
		AddPendulum("M6a.period_5deg", 5.0);
		AddPendulum("M6a.period_30deg", 30.0);
		AddPendulum("M6a.period_90deg", 90.0);
		const auto AddIncline = [&Cases, DtEngine](const char* Id, const double Mu, const int32 Steps)
		{
			const double Angle = BehaviorScenes::M4::AngleDegrees * 3.14159265358979323846 / 180.0;
			Cases.push_back({ { "id", Id }, { "function", "InclineSlideDistance" },
				{ "inputs", { { "mu", Mu }, { "angle_deg", BehaviorScenes::M4::AngleDegrees }, { "g", 980.0 }, { "dt", DtEngine }, { "steps", Steps } } },
				{ "values", { { "distance", Analytic::InclineSlideDistance(Mu, Angle, 980.0, DtEngine, Steps) } } } });
		};
		AddIncline("M4.d2s_mu_0_30", 0.30, 120);
		AddIncline("M4.d2s_mu_0_50", 0.50, 120);
		AddIncline("M4.d2s_mu_0_55", 0.55, 120);
		AddIncline("M4.d2s_mu_0_60", 0.60, 120);
		FJson Json;
		Json["schema"] = "physicstest.analytic/1";
		Json["note"] = "C++ Analytic:: references (Source/Runtime/PhysicsCore/Public/Physics/AnalyticReference.h) on the gate scenes' canonical inputs; "
			"dt_engine = (double)(float)dt (DL23). Tools/ptbench/analytic.py must reproduce every value within 1e-9.";
		Json["preset"] = PT_BUILD_PRESET;
		Json["cases"] = Cases;
		std::printf("%s\n", Json.dump(2).c_str());
		return EBenchExit::Ok;
	}

	// ---- -AbiOverhead (M10) -------------------------------------------------------------------------------------------
	EBenchExit FBenchmarkRunner::AbiOverhead(const FBenchRunSpec& Spec)
	{
		AbiGates::FAbiGateTarget Target;
		std::string Error;
		if (!AbiGates::PrepareAbiGateTarget(Spec.Backend, Target, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "-AbiOverhead: %s", Error.c_str());
			return EBenchExit::BackendUnavailable;
		}
		const AbiGates::FAbiCallOverhead Overhead = AbiGates::MeasureCallOverhead(Target);
		const bool bDebug = std::string(PT_BUILD_CONFIGURATION) == "Debug";
		const bool bPass = Overhead.bValid && Overhead.MedianNsPerFrame < AbiGates::CallOverheadLimitNsPerFrame;
		FJson Json;
		Json["schema"] = "physicstest.abi_overhead/1";
		Json["backend"] = Target.Label;
		Json["preset"] = PT_BUILD_PRESET;
		Json["configuration"] = PT_BUILD_CONFIGURATION;
		Json["valid"] = Overhead.bValid;
		Json["error"] = Overhead.Error;
		Json["calls_per_frame"] = 4;
		Json["bodies"] = 0;
		Json["frames_per_batch"] = Overhead.FramesPerBatch;
		Json["batches"] = Overhead.Batches;
		Json["median_ns_per_frame"] = Overhead.MedianNsPerFrame;
		Json["median_ns_per_call"] = Overhead.MedianNsPerFrame / 4.0;
		Json["min_ns_per_frame"] = Overhead.MinNsPerFrame;
		Json["max_ns_per_frame"] = Overhead.MaxNsPerFrame;
		Json["ns_per_tick"] = Overhead.NsPerTick;
		Json["limit_ns_per_frame"] = AbiGates::CallOverheadLimitNsPerFrame;
		Json["gate"] = bPass ? "pass" : (bDebug ? "recorded (Debug build: not judged)" : "fail");
		Json["method"] = "SetUpForFrame, StartFrame, WaitPhysScenes, EndFrame per frame through the raw C table, one TSC interval (rdtscp + lfence) per "
			"batch, the median over batches after a warm-up batch (03 M10)";
		std::printf("%s\n", Json.dump(2).c_str());
		return bPass || (bDebug && Overhead.bValid) ? EBenchExit::Ok : EBenchExit::GateFailed;
	}

	// ---- FBenchmarkRunner --------------------------------------------------------------------------------------------
	EBenchExit FBenchmarkRunner::List()
	{
		FJson Json;
		FJson Scenarios = FJson::array();
		for (const IPhysicsTestScenario* Scenario : FScenarioRegistry::Get().GetAll())
		{
			FJson Entry;
			Entry["name"] = Scenario->GetName();
			Entry["version"] = Scenario->GetVersion();
			Entry["kind"] = ToString(Scenario->GetKind());
			Entry["chaotic"] = Scenario->IsChaotic();
			Entry["description"] = Scenario->GetDescription();
			FScenarioParams Defaults;
			std::string Ignored;
			Scenario->ResolveParams(FScenarioParams(), Defaults, Ignored);
			Entry["default_frames"] = Scenario->GetDefaultFrames(Defaults);
			// M9: the capabilities the scene needs with its default parameters (a joint scene's Joint parameter may change them).
			Entry["required_capabilities"] = MakeCapabilitiesJson(Scenario->GetRequiredCapabilities(Defaults));
			Entry["forced_overrides"] = Scenario->AllowsForcedOverrides();
			FJson Params = FJson::array();
			for (const FScenarioParamDesc& Desc : Scenario->GetParamDescs())
			{
				FJson Param;
				Param["name"] = Desc.Name;
				Param["default"] = Desc.Default;
				Param["type"] = ToString(Desc.Type);
				Param["allowed"] = Desc.AllowedValues;
				Param["description"] = Desc.Description;
				Params.push_back(Param);
			}
			Entry["params"] = Params;
			FJson Windows = FJson::array();
			for (const FStepWindow& Window : Scenario->GetWindows(Defaults))
			{
				Windows.push_back({ { "name", Window.Name }, { "first", Window.First }, { "last", Window.Last } });
			}
			Entry["windows"] = Windows;
			Scenarios.push_back(Entry);
		}
		Json["scenarios"] = Scenarios;
		Json["backends"] = FPhysicsBackendRegistry::Get().GetAvailableNames();
		std::printf("%s\n", Json.dump(2).c_str());
		return EBenchExit::Ok;
	}

	EBenchExit FBenchmarkRunner::Describe(const FBenchRunSpec& Spec)
	{
		const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
		if (std::find(Available.begin(), Available.end(), Spec.Backend) == Available.end())
		{
			UE_LOG(LogPhysicsBench, Error, "backend '%s' is not available in this build", Spec.Backend.c_str());
			return EBenchExit::BackendUnavailable;
		}
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		if (!LoadBenchConfig(Spec, Config, Files, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::GateFailed;
		}
		IPhysicsTestScenario* Scenario = Spec.Scenario.empty() ? nullptr : FScenarioRegistry::Get().Find(Spec.Scenario);
		if (!Spec.Scenario.empty() && Scenario == nullptr)
		{
			UE_LOG(LogPhysicsBench, Error, "unknown scenario '%s' (see -List)", Spec.Scenario.c_str());
			return EBenchExit::GateFailed;
		}
		FScenarioParams Params;
		if (Scenario != nullptr && !Scenario->ResolveParams(Spec.Params, Params, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::GateFailed;
		}
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = Scenario != nullptr ? Scenario->GetName() : "Describe";
		InitParams.BackendName = Spec.Backend;
		InitParams.Config = &Config;
		if (Scenario != nullptr)
		{
			const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
			InitParams.MaxBodiesHint = Capacity.MaxBodies;
			InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
			InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
			InitParams.bApplyForcedOverrides = Scenario->AllowsForcedOverrides();
		}
		FPhysScene Scene(InitParams);
		if (Scenario != nullptr)
		{
			FRandomStream Random(Scenario->GetSeed(Params));
			const FScenarioContext Context{ Params, Random, &Config };
			Scenario->BuildScene(Scene, Context);
		}
		const std::string Effective = Scene.DumpEffectiveSettings();
		std::vector<FEffectiveSetting> Settings;
		const bool bParsed = ParseEffectiveSettings(Effective, Settings, Error);
		const std::vector<std::string> Missing = bParsed ? FindMissingEffectiveKeys(Settings, Scene.GetBackendInfo()) : std::vector<std::string>{ "<unparsable>" };
		FJson Json;
		Json["backend"] = MakeBackendJson(Scene.GetBackendInfo(), Files, Config, &Scene.GetBackendScene().GetModule());
		Json["profile"] = Config.ProfileName;
		Json["scenario"] = Scenario != nullptr ? Scenario->GetName() : "";
		Json["required_effective_keys"] = GetRequiredEffectiveKeys(Scene.GetBackendInfo());
		Json["missing_effective_keys"] = Missing;
		Json["effective"] = ParseEffectiveToJson(Effective);
		Json["effective_module"] = ParseEffectiveToJson(DumpModuleEffectiveSettings(Scene.GetBackendScene().GetModule()));
		std::printf("%s\n", Json.dump(2).c_str());
		return Missing.empty() ? EBenchExit::Ok : EBenchExit::GateFailed;
	}

	// ---- one run (02 12.2) ---------------------------------------------------------------------------------------------
	namespace
	{
		// The run of one scenario on one backend in this process, stage by stage (02 12.2, docs/TimingContract.md 1). A stage
		// that ends the run early writes exit.json through Finish and returns false. Every stage leaves what it learned in the
		// members, and MakeManifest writes every manifest group from whatever the run reached (null where it stopped earlier),
		// so a refused run's manifest has the same shape as a finished one (physicstest.run/2).
		class FBenchRun
		{
		public:
			explicit FBenchRun(const FBenchRunSpec& InSpec)
				: Spec(InSpec)
			{
			}

			EBenchExit Execute();

		private:
			bool ResolveScenario();
			bool ApplyHostControls();
			bool LoadConfig();
			bool LoadBackend();
			bool RunOptionPreflight();
			void BuildScene();
			void RunPreflight();
			void RunLoop();
			void DecideExit();
			void WriteOutputs();
			void Teardown();
			// exit.json, written last (02 12.2 step 12); terminate_process when a PT_MODULE_UE_RUNTIME module is loaded. From
			// here on the Fatal hook no longer touches exit.json (it is in its teardown phase).
			EBenchExit Finish(EBenchExit Code, const std::string& Status, const std::string& Category, const std::string& Reason);
			[[nodiscard]] FRunManifest MakeManifest() const;
			[[nodiscard]] int64 ReadBackendAllocs() const;

			const FBenchRunSpec& Spec;
			FFatalExitScope* Hook = nullptr;
			uint64 RunBegin = 0;
			std::string RunId;
			std::filesystem::path OutDir;
			std::vector<std::string> Outputs;
			std::string Error;

			// scenario and config
			IPhysicsTestScenario* Scenario = nullptr;
			FScenarioParams Params;
			int32 Frames = 0;
			int32 Seed = 0;
			FResolvedPhysicsConfig Config;
			FBenchConfig Files;
			bool bCountersTier = false;
			bool bContactsTier = false;
			bool bNativeDrawTier = false;      // M9: -Instrument=native_debug_draw (native_draw.csv)
			bool bPhasesTier = false;
			int32 StatesEvery = 0;
			float FrameDeltaSeconds = 0.0f;
			double DtEngine = 0.0;

			// host
			FProcessorTopology Topology;
			FHostControls Controls;
			double ClockReadOverheadNs = 0.0;
			FHostFacts Host;
			std::vector<FCpuFrequency> CpuFrequencies;

			// backend and scene
			FPhysicsBackendModule* Module = nullptr;
			std::string OptionPreflightError;
			FScenarioCapacity Capacity;
			std::unique_ptr<FPhysScene> Scene;
			std::string DescSha256;
			std::string OptionsSha256;
			uint64 BackendInitTicks = 0;
			uint64 OptionPreflightTicks = 0;
			uint64 SceneCreateTicks = 0;
			uint64 PreflightTicks = 0;
			uint64 PrepareTicks = 0;
			uint64 PreRollTicks = 0;
			uint64 Frame0Ticks = 0;
			uint64 LoopTicks = 0;

			// preflight
			std::string Effective;
			std::string ModuleEffective;
			std::vector<FEffectiveSetting> Settings;
			std::vector<std::string> MissingKeys;
			BenchPreflight::FResult Preflight;
			bool bPreflightPassed = false;
			std::string PreflightError;
			FJson BodiesSampled = FJson::array();

			// the loop
			FTimingRecorder Recorder;
			BenchRecorders::FHashRecorder Hashes;
			BenchRecorders::FStateRecorder States;
			BenchRecorders::FContactRecorder Contacts;
			BenchRecorders::FNativeDrawRecorder NativeDraw;
			bool bStates = false;
			bool bContacts = false;
			bool bNativeDraw = false;          // M9: the native draw stream is recorded (tier on, PT_CAP_NATIVE_DEBUG_DRAW)
			std::string StatesError;
			std::string ContactsNote;
			std::string NativeDrawNote;
			bool bSceneDesc = false;           // scene_desc.json written (M9: runs that record states)
			uint64 TickAllocations = 0;        // operator new calls inside Scene->Tick (the timed buckets) over every frame
			uint64 TickFrees = 0;
			uint64 Frame0Allocations = 0;
			uint64 LoopAllocations = 0;        // over the whole loop, recorder work between frames included
			int32 FirstAllocatingFrame = -1;
			int64 BackendAllocsBeforeLoop = -1;     // PtMemoryStats.NumAllocs right before frame 0 (-1 = not counted)
			int64 BackendAllocsAfterFrame0 = -1;    // after frame 0's Tick
			int64 BackendAllocsAfterLastTick = -1;  // after the last frame's Tick
			uint32 PhaseFrames = 0;            // frames with a solver_ns value (phases tier)
			int32 TickFrame = -1;              // the frame whose Tick runs (the -InjectTickAllocation delegate reads it)
			std::unique_ptr<int64> InjectedTickAllocation;   // test only: the block -InjectTickAllocation allocates inside a Tick
			FHostLoad Load;

			// M10: the TSC clock (02 9.3), the thread census (02 12.2 steps 5 / 11, G10b) and the conformance stamp (02 12.4)
			std::vector<FCycleCounter::FPair> CalibrationPairs;   // host controls, pre-roll, every CalibrationPairEvery frames, the end
			FTscCalibration Calibration;
			double ProvisionalNsPerTick = 0.0;  // the process-wide calibration the scene's buckets used before the run's own
			double TscReadOverheadNs = 0.0;
			bool bTscInvariant = false;
			FThreadCensus CensusInit;           // census #1 (after InitGamePhys: the threads the engine made)
			FThreadCensus CensusBegin;          // the window: right before frame 0 ...
			FThreadCensus CensusEnd;            // ... and right after the last frame
			FCensusWindow CensusWindow;
			FGateReport G10bReport;
			Gates::EG10bSkip G10bSkip = Gates::EG10bSkip::None;   // why G10bReport is Skip (M10 fix round 1: the headline rule)
			bool bG10b = false;                 // the window was measured (the loop ran)
			FConformanceStampKey StampKey;
			FStampCheck Stamp;
			bool bStampKey = false;
			std::string StampKeyError;
			std::filesystem::path StampDir;
			std::string ExeSha256;

			// results
			BenchRecorders::FHashRow FinalHash;
			PtStepStats StepStats = {};
			uint32 ErrorBits = 0;
			uint32 CapacityBitsDeclared = 0;
			uint32 CapacityErrorBits = 0;
			uint32 OtherErrorBits = 0;
			PtMemoryStats BackendMemory = {};
			PtResult BackendMemoryResult = PT_E_BAD_STATE;
			FProcessMemory ProcessMemory;
			double ContaminationLimitPct = 0.0;   // -ContaminationPct (per core) x the run's cores
			bool bContaminated = false;
			EBenchExit Exit = EBenchExit::Ok;
			std::string Status = "ok";
			std::string Category;
			std::string Reason;
			std::vector<std::string> InvalidReasons;
			bool bTimingValid = false;
		};

		EBenchExit FBenchRun::Execute()
		{
			RunBegin = FClockBuckets::Begin();
			RunId = MakeRunId();
			ExeSha256 = HashFileSha256(GetExecutablePath());   // the manifest's harness.exe_sha256 and the stamp key (M10)
			// ---- 1. the run directory, then the Fatal hook (exit.json 3) ------------------------------------------------------
			OutDir = !Spec.OutDir.empty() ? Spec.OutDir : GetDefaultResultsDir() / StringConv::Utf8ToPath(Spec.Session) / RunId;
			OutDir.make_preferred();
			{
				std::error_code DirError;
				std::filesystem::create_directories(OutDir, DirError);
				if (DirError)
				{
					UE_LOG(LogPhysicsBench, Error, "cannot create the run directory '%s': %s", StringConv::PathToUtf8(OutDir).c_str(), DirError.message().c_str());
					return EBenchExit::GateFailed;   // no directory: no exit.json possible
				}
			}
			FFatalExitScope HookScope(OutDir, RunId);
			Hook = &HookScope;
			if (!ResolveScenario() || !ApplyHostControls() || !LoadConfig() || !LoadBackend())
			{
				return Exit;   // exit.json written (usage error or backend unavailable; no manifest before the backend is initialised)
			}
			if (RunOptionPreflight())
			{
				BuildScene();
				RunPreflight();
				if (bPreflightPassed)
				{
					RunLoop();
				}
				else
				{
					UE_LOG(LogPhysicsBench, Error, "preflight failed (%s): Prepare and the frame loop are skipped", PreflightError.c_str());
				}
			}
			DecideExit();
			WriteOutputs();
			Finish(Exit, Status, Category, Reason);
			Teardown();   // ends the process (TerminateProcess) when a PT_MODULE_UE_RUNTIME module is loaded
			return Exit;
		}

		EBenchExit FBenchRun::Finish(const EBenchExit Code, const std::string& InStatus, const std::string& InCategory, const std::string& InReason)
		{
			Exit = Code;
			Status = InStatus;
			Category = InCategory;
			Reason = InReason;
			if (Code != EBenchExit::Ok)
			{
				UE_LOG(LogPhysicsBench, Error, "%s (exit %d, %s)", InReason.c_str(), static_cast<int32>(Code), InStatus.c_str());
			}
			FExitRecord Record;
			Record.Code = Code;
			Record.Status = InStatus;
			Record.Category = InCategory;
			Record.Reason = InReason;
			Record.RunId = RunId;
			Record.bTerminateProcess = FBenchmarkRunner::HasUeRuntimeModule();
			Record.Outputs = Outputs;
			if (!Record.WriteAndFlush(OutDir))
			{
				UE_LOG(LogPhysicsBench, Error, "cannot write %s", StringConv::PathToUtf8(OutDir / "exit.json").c_str());
			}
			Hook->EnterTeardown(Code);
			return Code;
		}

		bool FBenchRun::ResolveScenario()
		{
			Scenario = FScenarioRegistry::Get().Find(Spec.Scenario);
			if (Scenario == nullptr)
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", "unknown scenario '" + Spec.Scenario + "' (see -List)");
				return false;
			}
			if (!Scenario->ResolveParams(Spec.Params, Params, Error))
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", Error);
				return false;
			}
			const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
			if (std::find(Available.begin(), Available.end(), Spec.Backend) == Available.end())
			{
				Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", "backend '" + Spec.Backend + "' is not available in this build (see -List)");
				return false;
			}
			return true;
		}

		// ---- 2. host controls, before any SDK init or LoadLibrary (02 12.2; per process only, nothing system-wide) -------
		bool FBenchRun::ApplyHostControls()
		{
			Topology = ReadProcessorTopology();
			const uint32 T = static_cast<uint32>(Spec.Threads);
			Controls.Mask = Spec.AffinityMask != 0 ? Spec.AffinityMask : Topology.MakeThreadMask(T);
			if (Controls.Mask == 0)
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", "T=" + std::to_string(T) + " exceeds the " + std::to_string(Topology.PCoreFirstLogicals.size())
					+ " P-cores of this CPU (one logical CPU per P-core, 02 9.6); give -Affinity= explicitly");
				return false;
			}
			if ((Controls.Mask & ~Topology.SystemMask) != 0)
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", "-Affinity=" + ToHexMask(Controls.Mask) + " names CPUs this machine does not have ("
					+ ToHexMask(Topology.SystemMask) + ")");
				return false;
			}
			Controls.CoreMask = Topology.CoresOf(Controls.Mask);
			Controls.SiblingMask = Controls.CoreMask & ~Controls.Mask;
			Controls.Shape = Topology.DescribeMask(Controls.Mask);
			const FHostFacts Before = ReadHostFacts();
			Controls.PriorityBefore = Before.PriorityClass;
			Controls.AffinityBefore = Before.ProcessAffinityMask;
			Controls.TimerBefore = FHostEnvironment::QueryTimerResolution();
			std::string ControlError;
			Controls.bAffinity = FHostEnvironment::ApplyAffinity(Controls.Mask, ControlError);
			if (!Controls.bAffinity)
			{
				Controls.Errors.push_back(ControlError);
			}
			Controls.bPriority = FHostEnvironment::SetHighPriority(ControlError);
			if (!Controls.bPriority)
			{
				Controls.Errors.push_back(ControlError);
			}
			Controls.bThrottling = FHostEnvironment::DisablePowerThrottling(ControlError);
			if (!Controls.bThrottling)
			{
				Controls.Errors.push_back(ControlError);
			}
			Controls.bTimer = FHostEnvironment::SetTimerResolution1ms(ControlError);
			if (!Controls.bTimer)
			{
				Controls.Errors.push_back(ControlError);
			}
			Controls.TimerAfter = FHostEnvironment::QueryTimerResolution();
			for (const std::string& Message : Controls.Errors)
			{
				UE_LOG(LogPhysicsBench, Warning, "host control: %s", Message.c_str());
			}
			ClockReadOverheadNs = MeasureClockReadOverheadNs();
			Host = ReadHostFacts();
			CpuFrequencies = FHostEnvironment::ReadCpuFrequencies(Controls.Mask);
			// M10 (02 9.3): the TSC buckets. The process-wide calibration converts the scene's buckets until the run's own
			// calibration (every pair from here to the end of the loop) converts them again; the read overhead is recorded.
			FCycleCounter::EnsureCalibrated();
			ProvisionalNsPerTick = FCycleCounter::GetNsPerTick();
			bTscInvariant = FCycleCounter::IsInvariant();
			TscReadOverheadNs = FCycleCounter::MeasureReadOverheadNs();
			CalibrationPairs.reserve(64);
			CalibrationPairs.push_back(FCycleCounter::ReadPair());
			return true;
		}

		// ---- 3. config --------------------------------------------------------------------------------------------------
		bool FBenchRun::LoadConfig()
		{
			if (!LoadBenchConfig(Spec, Config, Files, Error))
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", Error);
				return false;
			}
			// M6: a scene that only means something with sleeping allowed (S08.StartAsleep) is refused under a profile without it.
			if (Scenario->RequiresSleeping() && !Config.TestSettings.bAllowSleeping)
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", std::string("scenario ") + Scenario->GetName() + " needs a profile with sleeping allowed "
					"(bAllowSleeping = True, e.g. UEParity); profile " + Config.ProfileName + " turns it off and skips the start-asleep step (02 6)");
				return false;
			}
			FPhysicsTestSettings& Test = Config.TestSettings;
			Test.AffinityMask = Controls.Mask;
			bCountersTier = (Test.InstrumentFlags & PT_INSTR_COUNTERS) != 0;
			bContactsTier = (Test.InstrumentFlags & PT_INSTR_CONTACTS) != 0;
			bNativeDrawTier = (Test.InstrumentFlags & PT_INSTR_NATIVE_DEBUG_DRAW) != 0;
			bPhasesTier = (Test.InstrumentFlags & PT_INSTR_PHASES) != 0;
			if (bCountersTier && Test.CountersEvery == 0)
			{
				Test.CountersEvery = 1;   // -Instrument=counters without a period: every frame
			}
			if ((bContactsTier || bNativeDrawTier) && Test.CaptureEvery == 0)
			{
				Test.CaptureEvery = 1;    // -Instrument=contacts / native_debug_draw without a period: every frame
			}
			StatesEvery = Spec.StatesEvery.value_or(0);
			Frames = Spec.Frames > 0 ? Spec.Frames : Scenario->GetDefaultFrames(Params);
			Seed = Scenario->GetSeed(Params);
			// The bench drives FixedStep frames at the nominal dt (other tick modes plan from it the same way).
			FrameDeltaSeconds = Test.FixedDeltaTime;
			DtEngine = Analytic::EngineDeltaTime(FrameDeltaSeconds);
			return true;
		}

		// ---- 4. backend load and init (the module's capabilities decide whether T > 1 is possible) ------------------------
		bool FBenchRun::LoadBackend()
		{
			const FPhysicsTestSettings& Test = Config.TestSettings;
			FPhysicsBackendModule* Loaded = FPhysicsBackendRegistry::Get().FindOrLoadModule(Spec.Backend, Error);
			if (Loaded == nullptr)
			{
				Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Error);
				return false;
			}
			// M9: a scene that needs a capability the backend lacks (the joint scenes on a backend without joints) is refused
			// before the backend is initialised: exit 2, status / category "unsupported", exit.json only (docs/ResultsSchema.md).
			// The runner reports such a cell as unsupported, not failed.
			const uint64 Required = Scenario->GetRequiredCapabilities(Params);
			const uint64 Missing = Required & ~Loaded->GetInfo().Capabilities;
			if (Missing != 0)
			{
				std::string Names;
				for (const FJson& Name : MakeCapabilitiesJson(Missing))
				{
					Names += (Names.empty() ? "PT_CAP_" : ", PT_CAP_") + Name.get<std::string>();
				}
				// M9 fix round 1: a capability the module declares but the host masks (built against another
				// PT_ABI_PROVISIONAL_REVISION, i.e. before the v1.1 freeze) is a misbuilt module, not a backend without joints: exit 6
				// backend_unavailable, which the runner counts as failed, so such a DLL cannot skip every joint cell of a session
				// silently. Only a capability the module does not declare at all is "unsupported" (exit 2, not a failure).
				const bool bRevision = (Missing & Loaded->GetInfo().DeclaredCapabilities) != 0;
				if (bRevision)
				{
					Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", "backend '" + Spec.Backend + "' declares " + Names
						+ " for scenario " + Scenario->GetName() + " but was built with PT_ABI_PROVISIONAL_REVISION "
						+ std::to_string(Loaded->GetInfo().ProvisionalRevision) + " (host " + std::to_string(PT_ABI_PROVISIONAL_REVISION)
						+ "): the host reads those capabilities as absent - rebuild the module against the current PtBackendAbi.h (docs/Abi.md 2)");
					return false;
				}
				Finish(EBenchExit::GateFailed, "unsupported", "unsupported", "backend '" + Spec.Backend + "' does not support scenario " + Scenario->GetName()
					+ ": it needs " + Names + " (03 M9: joint scenes run only on backends with joints)");
				return false;
			}
			if (Test.NumThreads > 1 && !Loaded->GetInfo().Has(PT_CAP_MULTITHREADED))
			{
				Finish(EBenchExit::GateFailed, "usage_error", "usage", "backend '" + Spec.Backend + "' runs single-threaded only (no PT_CAP_MULTITHREADED): T="
					+ std::to_string(Test.NumThreads) + " is refused (02 9.6: Bullet T = 1; BulletMt is the M15 backlog)");
				return false;
			}
			FBackendInitParams BackendInit;
			BackendInit.MaxThreads = static_cast<uint32>(Test.NumThreads > 0 ? Test.NumThreads : 1);
			BackendInit.InitOptions = Config.InitOptions;
			const uint64 PhaseBegin = FClockBuckets::Begin();
			Module = FPhysicsBackendRegistry::Get().FindOrLoad(Spec.Backend, BackendInit, Error);
			BackendInitTicks = FClockBuckets::End(PhaseBegin);
			if (Module == nullptr)
			{
				Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Error);
				return false;
			}
			// M10 (02 12.2 step 5): thread census #1 - the threads the engine has after InitGamePhys (workers, dispatchers, a UE
			// TaskGraph). Untimed; the window census around the loop says whether any of them worked.
			CensusInit.Capture();
			return true;
		}

		// ---- 5. engine-option preflight -----------------------------------------------------------------------------------
		// The variant's [PhysicsTest.Backend.<Engine>] keys are validated by the backend's CreateScene. A probe scene with
		// exactly those keys (no bodies, untimed) turns a configuration error into exit 2 with a manifest; FPhysScene would
		// stop the process through Fatal (exit 3, no output). (M3 fix round.) False: refused (status preflight_failed); the
		// run still writes its manifest (no scene: the scene-derived fields are null) and tears the module down.
		bool FBenchRun::RunOptionPreflight()
		{
			const FPhysicsTestSettings& Test = Config.TestSettings;
			const uint64 PhaseBegin = FClockBuckets::Begin();
			std::vector<PtKeyValue> ProbeOptions;
			for (const auto& [Key, Value] : Config.SceneOptions)
			{
				ProbeOptions.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
			}
			PtSceneDesc Probe = {};
			Probe.StructSize = sizeof(PtSceneDesc);
			Probe.RunMode = static_cast<uint32>(Test.RunMode);
			Probe.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
			Probe.NominalDeltaTime = Test.FixedDeltaTime > 0.0f ? static_cast<double>(Test.FixedDeltaTime) : 1.0 / 60.0;
			Probe.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
			Probe.RestitutionThreshold = -1.0;
			Probe.NumThreads = 1;
			Probe.DebugName = "OptionPreflight";
			Probe.Options = ProbeOptions.empty() ? nullptr : ProbeOptions.data();
			Probe.NumOptions = static_cast<uint32>(ProbeOptions.size());
			std::string ProbeError;
			const bool bAccepted = FPhysicsBackendScene::Create(*Module, Probe, ProbeError) != nullptr;
			OptionPreflightTicks = FClockBuckets::End(PhaseBegin);
			if (!bAccepted)
			{
				OptionPreflightError = ProbeError;
				PreflightError = "engine options: " + ProbeError;
				ModuleEffective = DumpModuleEffectiveSettings(*Module);
				UE_LOG(LogPhysicsBench, Error, "preflight: the backend rejects the variant's engine options: %s", ProbeError.c_str());
			}
			return bAccepted;
		}

		// ---- 6. scene: create, BuildScene + flush, hashes ---------------------------------------------------------------
		void FBenchRun::BuildScene()
		{
			Capacity = Scenario->GetCapacity(Params);
			FPhysSceneInitParams InitParams;
			InitParams.DebugName = Scenario->GetName();
			InitParams.BackendName = Spec.Backend;
			InitParams.Config = &Config;
			InitParams.MaxBodiesHint = Capacity.MaxBodies;
			InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
			InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
			InitParams.InjectedSceneInstrumentFlags = Spec.InjectSceneInstrumentFlags;   // test only
			InitParams.bApplyForcedOverrides = Scenario->AllowsForcedOverrides();      // M9 (02 10): gate scenes keep their values
			const uint64 PhaseBegin = FClockBuckets::Begin();
			Scene = std::make_unique<FPhysScene>(InitParams);
			FRandomStream Random(Seed);
			const FScenarioContext Context{ Params, Random, &Config };
			Scenario->BuildScene(*Scene, Context);
			Scene->FlushDeferredActors();   // a scenario normally flushed already
			SceneCreateTicks = FClockBuckets::End(PhaseBegin);
			const std::vector<float> FrameDts(static_cast<size_t>(Frames), FrameDeltaSeconds);
			DescSha256 = Scene->ComputeDescSha256(FrameDts);
			OptionsSha256 = Scene->ComputeOptionsSha256();
			// M9: the scene description for the behaviour analysis, with the states it describes (untimed, before frame 0).
			if (StatesEvery > 0)
			{
				std::string DescError;
				const FJson Desc = MakeSceneDescJson(*Scene, *Scenario, Params, DescSha256, DescError);
				bSceneDesc = !Desc.is_null() && WriteText(OutDir / "scene_desc.json", Desc.dump(1) + "\n");
				if (!bSceneDesc)
				{
					UE_LOG(LogPhysicsBench, Warning, "scene_desc.json not written: %s", DescError.empty() ? "cannot write the file" : DescError.c_str());
				}
			}
		}

		// ---- 7. effective settings + the preflight: required keys (A7 at run time), G0, G9, G10a -------------------------
		void FBenchRun::RunPreflight()
		{
			const uint64 PhaseBegin = FClockBuckets::Begin();
			Effective = Scene->DumpEffectiveSettings();
			ModuleEffective = DumpModuleEffectiveSettings(*Module);
			WriteText(OutDir / "effective_settings.jsonl", Effective);
			Outputs.push_back("effective_settings.jsonl");
			if (!ParseEffectiveSettings(Effective, Settings, Error))
			{
				MissingKeys.push_back("<unparsable: " + Error + ">");
			}
			else
			{
				MissingKeys = FindMissingEffectiveKeys(Settings, Scene->GetBackendInfo());
				for (const std::string& Key : Spec.ExtraRequiredKeys)
				{
					if (std::none_of(Settings.begin(), Settings.end(), [&Key](const FEffectiveSetting& S) { return S.Key == Key; }))
					{
						MissingKeys.push_back(Key);
					}
				}
			}
			const std::filesystem::path ConfigDir = Spec.ConfigDir.empty() ? GetDefaultConfigDir() : std::filesystem::path(Spec.ConfigDir).make_preferred();
			Gates::FTolerances Tolerances;
			std::string TolerancesError;
			const bool bTolerances = Tolerances.LoadFile(Gates::GetTolerancesPath(ConfigDir), TolerancesError);
			BenchPreflight::FInputs Inputs;
			Inputs.Spec = &Spec;
			Inputs.Config = &Config;
			Inputs.Files = &Files;
			Inputs.Scenario = Scenario;
			Inputs.Params = &Params;
			Inputs.Scene = Scene.get();
			Inputs.Settings = &Settings;
			Inputs.Tolerances = bTolerances ? &Tolerances : nullptr;
			Inputs.TolerancesError = TolerancesError;
			Inputs.DtEngine = DtEngine;
			Preflight = BenchPreflight::Run(Inputs);
			// M10 (02 12.2 step 7, 12.4): the stamp of this run's key - recorded in the manifest (conformance_stamp), never a refusal
			// here (the runner refuses unstamped cells before it starts a process; validate rejects runs without a valid stamp).
			StampDir = Spec.StampDir.empty() ? GetDefaultStampDir() : std::filesystem::path(Spec.StampDir).make_preferred();
			if (bTolerances)
			{
				StampKey.BenchExeSha256 = ExeSha256;
				StampKey.BackendModuleSha256 = Module->IsDll() ? HashFileSha256(Module->GetDllPath()) : std::string();
				StampKey.VariantSha256 = Files.VariantSha256;
				StampKey.PhysicsSha256 = Config.PhysicsSha256();
				StampKey.TolerancesSha256 = Tolerances.GetSha256();
				StampKey.GateSuiteVersion = Gates::GateSuiteVersion;
				StampKey.ConfigOverrides = ExtractConfigOverrides(Spec.CommandLine);
				bStampKey = true;
				Stamp = CheckConformanceStamp(StampKey, StampDir);
				UE_LOG(LogPhysicsBench, Display, "conformance stamp %s: %s%s", Stamp.KeySha256.substr(0, 16).c_str(), Stamp.bValid ? "valid" : "not valid",
					Stamp.bValid ? "" : (" (" + Stamp.Reason + ")").c_str());
			}
			else
			{
				StampKeyError = "tolerances: " + TolerancesError;
			}
			PreflightTicks = FClockBuckets::End(PhaseBegin);
			// M9 (02 10): the preflight diff - every effective key next to its resolved value - and a summary in the log.
			if (!Preflight.Diff.empty())
			{
				WriteText(OutDir / "preflight_diff.json", MakePreflightDiffJson(Preflight.Diff, Config, Spec.Backend, Files.VariantName).dump(1) + "\n");
				Outputs.push_back("preflight_diff.json");
				std::map<std::string, int32> Counts;
				for (const Gates::FReadbackDiffRow& Row : Preflight.Diff)
				{
					++Counts[Row.Status];
					if (Row.Status == "mismatch" || Row.Status == "no_rule")
					{
						UE_LOG(LogPhysicsBench, Warning, "preflight diff: %s %s: effective %s, resolved %s (%s)", Row.Key.c_str(), Row.Status.c_str(), Row.Native.c_str(),
							Row.bHasResolved ? std::to_string(Row.Resolved).c_str() : "-", Row.Rule.c_str());
					}
				}
				std::string Summary;
				for (const auto& [RowStatus, Count] : Counts)
				{
					Summary += (Summary.empty() ? "" : ", ") + std::to_string(Count) + " " + RowStatus;
				}
				UE_LOG(LogPhysicsBench, Display, "preflight diff (resolved vs effective, preflight_diff.json): %zu keys: %s", Preflight.Diff.size(), Summary.c_str());
			}
			bPreflightPassed = MissingKeys.empty() && Preflight.bPassed && Preflight.InjectionError.empty();
			if (!MissingKeys.empty())
			{
				PreflightError = std::to_string(MissingKeys.size()) + " required effective key(s) missing";
			}
			if (!Preflight.InjectionError.empty())
			{
				PreflightError += (PreflightError.empty() ? "" : "; ") + Preflight.InjectionError;
			}
			for (const FGateReport& Report : Preflight.Reports)
			{
				UE_LOG(LogPhysicsBench, Display, "preflight %s", Report.Summary().c_str());
				if (Report.Result == EGateResult::Fail)
				{
					PreflightError += (PreflightError.empty() ? "" : "; ") + Report.Gate + " failed" + (Report.Failures.empty() ? std::string() : ": " + Report.Failures.front());
				}
			}
			BodiesSampled = SampleMassProperties(*Scene);
			if (Spec.bInjectFatal)
			{
				UE_LOG(LogPhysicsBench, Fatal, "test-only -InjectFatal: a Fatal after the preflight (exit.json must still say 3)");
			}
		}

		int64 FBenchRun::ReadBackendAllocs() const
		{
			PtMemoryStats Stats = {};
			return Scene->GetBackendScene().GetMemoryStats(Stats) == PT_OK && Stats.NumAllocs >= 0 ? Stats.NumAllocs : -1;
		}

		// ---- 8. prepare, 9. pre-roll, 10. frame 0 and the timed loop (only after a passed preflight, 02 12.2) ------------
		void FBenchRun::RunLoop()
		{
			const FPhysicsTestSettings& Test = Config.TestSettings;
			uint64 PhaseBegin = FClockBuckets::Begin();
			Scene->PrepareForSimulation();
			PrepareTicks = FClockBuckets::End(PhaseBegin);
			IPhysicsTestScenario* const RunScenario = Scenario;
			if (Spec.InjectTickAllocationFrame < 0)
			{
				Scene->OnPhysScenePreTick = [RunScenario](FPhysScene* InScene, const float DeltaSeconds) { RunScenario->OnPreTick(*InScene, DeltaSeconds); };
			}
			else
			{
				// Test only (-InjectTickAllocation=<f>, M8: the positive control of the allocation hook): one operator new inside
				// frame f's Tick, from the PreTick delegate (FPhysScene::Tick, the span the hook counts). The block outlives the
				// loop, so the allocation cannot be elided; it is freed after the loop.
				Scene->OnPhysScenePreTick = [this, RunScenario](FPhysScene* InScene, const float DeltaSeconds)
				{
					if (TickFrame == Spec.InjectTickAllocationFrame && !InjectedTickAllocation)
					{
						InjectedTickAllocation = std::make_unique<int64>(TickFrame);
					}
					RunScenario->OnPreTick(*InScene, DeltaSeconds);
				};
			}
			// Everything the loop records is sized or opened here (02 9.3: no allocation, I/O or log inside the timing loop).
			Recorder.Reserve(Frames);
			Hashes.Reserve(static_cast<size_t>(CountRecordedFrames(Test.HashEvery, Frames)));
			if (StatesEvery > 0)
			{
				bStates = States.Open(OutDir / "states.ptst", static_cast<uint32>(Scene->GetBodies().size()), DtEngine, Scenario->ComputeSceneSha256(Params), DescSha256);
				StatesError = bStates ? "" : "cannot open states.ptst";
			}
			FPhysicsDebugSnapshot Snapshot;
			if (bContactsTier)
			{
				// The header records the layout revision (PT_ABI_PROVISIONAL_REVISION, 3 since v1.1) of the PtContactPoint records and the backend's declared
				// narrow-phase pose (contact.point_pose; M7 review), so the M9 analysis can reject a file of another layout.
				const auto PointPose = std::find_if(Settings.begin(), Settings.end(), [](const FEffectiveSetting& S) { return S.Key == "contact.point_pose"; });
				const uint32 PointPoseValue = PointPose != Settings.end() && PointPose->bHasCanonical && PointPose->Canonical >= 0.0
					? static_cast<uint32>(PointPose->Canonical) : BenchRecorders::FContactRecorder::UnknownPointPose;
				bContacts = Scene->GetBackendInfo().Has(PT_CAP_CONTACT_POINTS) && Contacts.Open(OutDir / "contacts.ptcn", PointPoseValue);
				ContactsNote = bContacts ? "" : "the backend declares no contact points (PT_CAP_CONTACT_POINTS): no contacts.ptcn";
				Snapshot.Contacts.reserve(static_cast<size_t>(std::max<uint32>(Capacity.MaxContactConstraints, 64u)) * 4);
				Snapshot.Particles.reserve(Scene->GetBodies().size());
			}
			// M9 (03 M9: the native draw verified without a viewer, "the command stream only"): -Instrument=native_debug_draw
			// captures PT_SNAP_NATIVE_DRAW every CaptureEvery frames into native_draw.csv (counts per type and h_draw). The
			// backend draws only in viz runs (-RunMode=Viz; PhysX sets its visualization parameters, Bullet attaches its drawer).
			FPhysicsDebugSnapshot DrawSnapshot;
			if (bNativeDrawTier)
			{
				bNativeDraw = Scene->GetBackendInfo().Has(PT_CAP_NATIVE_DEBUG_DRAW);
				NativeDrawNote = !bNativeDraw ? "the backend declares no native debug draw (PT_CAP_NATIVE_DEBUG_DRAW): no native_draw.csv"
					: (Test.RunMode != EPhysicsRunMode::Viz ? "not a viz run (-RunMode=Viz): the backend draws nothing, every row is empty" : "");
				if (bNativeDraw)
				{
					NativeDraw.Reserve(static_cast<size_t>(CountRecordedFrames(Test.CaptureEvery, Frames)));
				}
			}
			// Test only (-InjectStatePerturbation=<f>, the G8 / G11 negative control): a velocity change of 1e-3 cm/s on the
			// last dynamic body after frame f, between frames.
			FBodyInstance* PerturbedBody = nullptr;
			if (Spec.InjectStatePerturbationFrame >= 0)
			{
				for (FBodyInstance* Body : Scene->GetBodies())
				{
					PerturbedBody = Body->IsDynamic() && Body->ActorHandle.IsValid() ? Body : PerturbedBody;
				}
			}
			const bool bNativeHash = Scene->GetBackendInfo().Has(PT_CAP_NATIVE_HASH);
			CalibrationPairs.reserve(CalibrationPairs.size() + static_cast<size_t>(Frames / CalibrationPairEvery) + 8);
			FHostLoadMonitor LoadMonitor;
			LoadMonitor.Begin();
			CalibrationPairs.push_back(FCycleCounter::ReadPair());
			PhaseBegin = FClockBuckets::Begin();
			FHostEnvironment::SpinPreRoll(Spec.PreRollSeconds, Controls.Mask);
			PreRollTicks = FClockBuckets::End(PhaseBegin);
			CalibrationPairs.push_back(FCycleCounter::ReadPair());
			BackendAllocsBeforeLoop = ReadBackendAllocs();
			// M10 (02 12.2 step 11): the census window begins here; its counters are read last, right before frame 0.
			CensusBegin.Capture(FThreadCensus::ETiming::CountersLast);
			// Test only (-InjectForeignWork=<ms>, the G10b negative control, M10 fix round 0): a helper thread that spins <ms> of its
			// own CPU time inside the window. A short spin ends before census #2, so the helper is in neither census and only the
			// process total shows it (unattributed); G10b must fail on it either way.
			std::thread ForeignWork;
			if (Spec.InjectForeignWorkMs > 0)
			{
				const uint64 Cycles = static_cast<uint64>(Spec.InjectForeignWorkMs * 1.0e6 / FCycleCounter::GetNsPerTick());
				ForeignWork = std::thread([Cycles]
				{
					ULONG64 Begin = 0;
					QueryThreadCycleTime(GetCurrentThread(), &Begin);
					ULONG64 Now = Begin;
					volatile uint64 Sink = 0;
					while (Now - Begin < Cycles)
					{
						for (int32 Index = 0; Index < 1000; ++Index)
						{
							Sink = Sink + 1;
						}
						QueryThreadCycleTime(GetCurrentThread(), &Now);
					}
				});
			}
			const uint64 LoopNewBegin = FAllocationCounters::NumNew.load(std::memory_order_relaxed);
			PhaseBegin = FClockBuckets::Begin();
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				// Inside Tick: no allocation, no I/O, no log (02 9.3). The counters around it are two relaxed loads; the TSC and QPC
				// reads around it (M10) are outside the buckets.
				const uint64 NewBefore = FAllocationCounters::NumNew.load(std::memory_order_relaxed);
				const uint64 DeleteBefore = FAllocationCounters::NumDelete.load(std::memory_order_relaxed);
				TickFrame = Frame;
				const uint64 QpcBefore = FPlatformTime::Cycles64();
				const uint64 TscBefore = FCycleCounter::Read();
				Scene->Tick(FrameDeltaSeconds);
				const uint64 TscAfter = FCycleCounter::Read();
				const uint64 QpcAfter = FPlatformTime::Cycles64();
				const uint64 FrameNew = FAllocationCounters::NumNew.load(std::memory_order_relaxed) - NewBefore;
				TickAllocations += FrameNew;
				TickFrees += FAllocationCounters::NumDelete.load(std::memory_order_relaxed) - DeleteBefore;
				if (FrameNew > 0 && FirstAllocatingFrame < 0)
				{
					FirstAllocatingFrame = Frame;
				}
				// The engine's own allocator count (PtMemoryStats.NumAllocs) after frame 0's and the last frame's Tick only:
				// an untimed ABI read between frames, never one per frame.
				if (Frame == 0)
				{
					Frame0Allocations = FrameNew;
					BackendAllocsAfterFrame0 = ReadBackendAllocs();
				}
				if (Frame == Frames - 1)
				{
					BackendAllocsAfterLastTick = ReadBackendAllocs();
				}
				// Untimed recorder work between frames (only in the runs that ask for it; a headline run records the row only).
				FFrameRecord Row;
				Row.Timing = Scene->GetLastFrameTiming();
				Row.Timing.FrameQpcTicks = QpcAfter - QpcBefore;   // M10: the QPC ticks of the whole Tick (the buckets read the TSC)
				Row.TickTsc = TscAfter - TscBefore;
				Row.TscEnd = TscAfter;
				Row.QpcEnd = QpcAfter;
				Row.SimTime = Scene->GetSimTime();
				Row.AwakeBodies = Scene->GetStats().NumAwakeBodies;
				const bool bCountersFrame = Test.CountersEvery > 0 && IsRecordedFrame(Test.CountersEvery, Frame, Frames);
				if (bCountersFrame || bPhasesTier)
				{
					// The phases tier reads the backend's phase timers every frame (solver_ns, 02 9.7); the counter columns
					// follow CountersEvery.
					PtStepStats Stats = {};
					if (Scene->GetBackendScene().GetStepStats(Stats) == PT_OK)
					{
						if (bCountersFrame)
						{
							Row.ContactPairs = Stats.NumContactPairs;
							Row.ContactPoints = Stats.NumContactPoints;
							Row.Islands = Stats.NumIslands;
						}
						if (bPhasesTier && Stats.NumPhaseTimes > 0)
						{
							int64 Sum = 0;
							for (uint32 Phase = 0; Phase < Stats.NumPhaseTimes && Phase < 8; ++Phase)
							{
								Sum += Stats.PhaseTimeNs[Phase];
							}
							Row.SolverNs = Sum;
							++PhaseFrames;
						}
					}
				}
				Recorder.Record(Row);
				if (IsRecordedFrame(Test.HashEvery, Frame, Frames))
				{
					BenchRecorders::FHashRow Hash;
					Hash.Frame = static_cast<uint32>(Frame);
					Hash.bHasNative = bNativeHash;
					Hash.Native = bNativeHash ? Scene->ComputeNativeStateHash() : 0;
					Hash.Quantized = BenchRecorders::ComputeQuantizedStateHash(*Scene);
					Hash.State = BenchRecorders::ComputeExactStateHash(*Scene);
					Hashes.Record(Hash);
				}
				if (bStates && IsRecordedFrame(StatesEvery, Frame, Frames))
				{
					States.Append(static_cast<uint32>(Frame), static_cast<uint32>(Scene->GetStepCount()), Scene->GetSimTime(), *Scene);
				}
				if (bContacts && IsRecordedFrame(Test.CaptureEvery, Frame, Frames))
				{
					if (Scene->CaptureDebugSnapshot(Snapshot, PT_SNAP_CONTACTS))
					{
						Contacts.Append(static_cast<uint32>(Frame), Snapshot.Contacts);
					}
				}
				if (bNativeDraw && IsRecordedFrame(Test.CaptureEvery, Frame, Frames))
				{
					if (Scene->CaptureDebugSnapshot(DrawSnapshot, PT_SNAP_NATIVE_DRAW))
					{
						NativeDraw.Record(static_cast<uint32>(Frame), DrawSnapshot.NativeCommands, DrawSnapshot.StringPool, DrawSnapshot.bTruncated);
					}
				}
				if (PerturbedBody != nullptr && Frame == Spec.InjectStatePerturbationFrame)
				{
					PerturbedBody->AddImpulse(FVector(0.0, 0.0, 1.0e-3), true);
				}
				if (Frame == Spec.InjectSilentExitFrame)
				{
					// Test only (-InjectSilentExit=<f>, M8): the process dies half-way with a raw exit code of 0 - no output, no
					// exit.json. The runner must report the run as failed (it never trusts the raw code, 02 12.2).
					UE_LOG(LogPhysicsBench, Warning, "test-only -InjectSilentExit: TerminateProcess(GetCurrentProcess(), 0) after frame %d, before any output or exit.json", Frame);
					std::fflush(stdout);
					std::fflush(stderr);
					TerminateProcess(GetCurrentProcess(), 0);
				}
				if ((Frame + 1) % CalibrationPairEvery == 0)
				{
					CalibrationPairs.push_back(FCycleCounter::ReadPair());   // between frames, untimed (02 9.3 calibration)
				}
				if (Frame == 0)
				{
					Frame0Ticks = FClockBuckets::End(PhaseBegin);
					PhaseBegin = FClockBuckets::Begin();
				}
			}
			// M10 fix round 1: the loop phase ends with its last frame (read before census #2, whose enumeration takes tens of ms,
			// and before the -InjectForeignWork join), as in M7-M9.
			LoopTicks = FClockBuckets::End(PhaseBegin);
			// M10: the census window ends here, counters first (census #1's threads are read with them, through its handles).
			CensusEnd.Capture(FThreadCensus::ETiming::CountersFirst, &CensusBegin);
			if (ForeignWork.joinable())
			{
				ForeignWork.join();   // test only (-InjectForeignWork): after census #2, so a helper still spinning there is a new thread
			}
			bG10b = true;
			LoopAllocations = FAllocationCounters::NumNew.load(std::memory_order_relaxed) - LoopNewBegin;
			InjectedTickAllocation.reset();   // test only (-InjectTickAllocation): freed outside the loop
			Load = LoadMonitor.End(Controls.Mask, Controls.CoreMask);
			if (bStates && !States.Close())
			{
				StatesError = "writing states.ptst failed";
			}
			if (bContacts && !Contacts.Close())
			{
				ContactsNote = "writing contacts.ptcn failed";
			}
		}

		// ---- untimed reads after the last frame, the exit code and headline validity (03 M7) ----------------------------
		void FBenchRun::DecideExit()
		{
			const FPhysicsTestSettings& Test = Config.TestSettings;
			// M10 (02 9.3): the run's TSC calibration over every pair from the host controls to here, and every recorded bucket
			// converted again with it; then the census window and G10b.
			CalibrationPairs.push_back(FCycleCounter::ReadPair());
			Calibration = CalibrateTsc(CalibrationPairs);
			Recorder.Recalibrate(Calibration.bValid ? Calibration.NsPerTick : ProvisionalNsPerTick);
			if (bG10b)
			{
				CensusWindow = MeasureCensusWindow(CensusBegin, CensusEnd, Controls.Mask, static_cast<uint32>(std::max(Test.NumThreads, 1)));
				G10bReport = Gates::EvaluateG10b(CensusWindow, Spec.Backend + (Files.VariantName.empty() ? "" : "." + Files.VariantName) + " / " + Config.ProfileName,
					&G10bSkip);
				UE_LOG(LogPhysicsBench, Display, "%s", G10bReport.Summary().c_str());
			}
			FinalHash = Hashes.GetRows().empty() ? BenchRecorders::FHashRow() : Hashes.GetRows().back();
			if (Scene)
			{
				Scene->GetBackendScene().GetStepStats(StepStats);
				BackendMemoryResult = Scene->GetBackendScene().GetMemoryStats(BackendMemory);
			}
			ErrorBits = StepStats.EngineErrorBits;
			for (const FFrameRecord& Record : Recorder.GetRecords())
			{
				ErrorBits |= Record.Timing.EngineErrorBits;
			}
			if (bPreflightPassed)
			{
				ErrorBits |= Spec.InjectEngineErrorBits;   // test only (-InjectEngineErrorBits): the manifest records it
			}
			CapacityBitsDeclared = GetCapacityErrorBits(Settings);
			CapacityErrorBits = ErrorBits & CapacityBitsDeclared;
			OtherErrorBits = ErrorBits & ~CapacityBitsDeclared;
			ProcessMemory = ReadProcessMemoryCounters();
			if (bPreflightPassed)
			{
				WriteText(OutDir / "effective_settings_after.jsonl", Scene->DumpEffectiveSettings());
				Outputs.push_back("effective_settings_after.jsonl");
			}
			// Exit code 4 (docs/TimingContract.md 5.1): other processes' load on the run's cores (the mask's CPUs and their SMT
			// siblings) against -ContaminationPct per core x the number of cores.
			ContaminationLimitPct = Spec.ContaminationPct * static_cast<double>(std::max<uint32>(Controls.Shape.Cores, 1u));
			bContaminated = Load.bValid && Load.CoreBusyOtherCorePct > ContaminationLimitPct;

			if (!bPreflightPassed && !Preflight.InjectionError.empty())
			{
				// A test-only injection that cannot take effect is a usage error, never a "passed" or an ordinary G9 refusal.
				Exit = EBenchExit::GateFailed;
				Status = "usage_error";
				Category = "usage";
				Reason = Preflight.InjectionError;
			}
			else if (!bPreflightPassed)
			{
				Exit = EBenchExit::GateFailed;
				Status = "preflight_failed";
				Category = "preflight";
				Reason = OptionPreflightError.empty() ? "preflight: " + PreflightError : "preflight: the backend rejects the variant's engine options: " + OptionPreflightError;
			}
			else if (OtherErrorBits != 0)
			{
				// An engine error wins over a capacity overflow in the same run: a capacity retry (M8) would not cure it. Both
				// bit groups are in the manifest (determinism.capacity_error_bits / other_error_bits).
				Exit = EBenchExit::EngineError;
				Status = "engine_error";
				Category = "engine";
				Reason = "engine error bits " + ToHexMask(OtherErrorBits) + " outside the backend's capacity bits";
			}
			else if (CapacityErrorBits != 0)
			{
				Exit = EBenchExit::Capacity;
				Status = "capacity_exceeded";
				Category = "capacity";
				Reason = "capacity error bits " + ToHexMask(CapacityErrorBits) + " (errors.capacity_bits " + ToHexMask(CapacityBitsDeclared) + ")";
			}
			else if (bContaminated && Spec.HostCheck == EHostCheckPolicy::Enforce)
			{
				Exit = EBenchExit::Contaminated;
				Status = "contaminated_host";
				Category = "host";
				char Text[384];
				std::snprintf(Text, sizeof(Text), "other processes used %.2f%% of one core on the run's %u core(s) (CPUs %s with their SMT siblings %s; limit %.2f%% = "
					"%.2f%% per core, -ContaminationPct)", Load.CoreBusyOtherCorePct, Controls.Shape.Cores, ToHexMask(Controls.Mask).c_str(),
					ToHexMask(Controls.SiblingMask).c_str(), ContaminationLimitPct, Spec.ContaminationPct);
				Reason = Text;
			}

			// ---- headline validity (03 M7: tier, G10a, protocol; docs/TimingContract.md 6) ----
			InvalidReasons = Preflight.TimingInvalidReasons;   // G10a: run mode or instrumentation tier
			if (Exit != EBenchExit::Ok)
			{
				InvalidReasons.push_back(std::string("exit code ") + std::to_string(static_cast<int32>(Exit)) + " (" + Status + ")");
			}
			if (Test.TickMode != EPhysicsTickMode::FixedStep)
			{
				InvalidReasons.push_back(std::string("tick_mode ") + ToString(Test.TickMode) + ": headline runs are FixedStep");
			}
			if (Test.HashEvery != 0 || StatesEvery != 0 || Test.CountersEvery != 0 || Test.CaptureEvery != 0)
			{
				InvalidReasons.push_back("per-frame recorder work between frames (HashEvery " + std::to_string(Test.HashEvery) + ", StatesEvery " + std::to_string(StatesEvery)
					+ ", CountersEvery " + std::to_string(Test.CountersEvery) + ", CaptureEvery " + std::to_string(Test.CaptureEvery) + "): headline runs hash the final frame only (02 9.1)");
			}
			if (!Controls.AllApplied())
			{
				InvalidReasons.push_back("a host control failed (affinity, priority, power throttling or timer resolution)");
			}
			if (Spec.PreRollSeconds < 0.5)
			{
				InvalidReasons.push_back("spin pre-roll shorter than 0.5 s (02 9.5)");
			}
			if (!Load.bValid)
			{
				InvalidReasons.push_back("the host load was not measured");
			}
			else if (bContaminated)
			{
				InvalidReasons.push_back(Spec.HostCheck == EHostCheckPolicy::Enforce ? "contaminated host" : "contaminated host (recorded only: -HostCheck=record)");
			}
			if (TickAllocations != 0)
			{
				InvalidReasons.push_back(std::to_string(TickAllocations) + " allocation(s) inside the timed buckets (first at frame " + std::to_string(FirstAllocatingFrame) + ")");
			}
#if PT_DO_CHECK
			InvalidReasons.push_back(std::string("checks compiled in (PT_DO_CHECK=1, preset ") + PT_BUILD_PRESET + "): headline runs use the test presets");
#endif
			if (Spec.InjectEngineErrorBits != 0 || !Spec.InjectPreflightFault.empty() || Spec.InjectSceneInstrumentFlags != 0 || Spec.InjectStatePerturbationFrame >= 0
				|| Spec.InjectTickAllocationFrame >= 0 || Spec.InjectSilentExitFrame >= 0 || Spec.InjectForeignWorkMs > 0)
			{
				InvalidReasons.push_back("a test-only injection was used");
			}
			// M10 (docs/TimingContract.md 6, conditions 11-13): the single-thread proof, the TSC clock, no profiler build.
			if (bG10b && G10bReport.Result == EGateResult::Fail)
			{
				InvalidReasons.push_back("G10b failed (T = 1 single-thread proof): " + (G10bReport.Failures.empty() ? std::string("?") : G10bReport.Failures.front()));
			}
			else if (bG10b && CensusWindow.Threads == 1 && (G10bSkip == Gates::EG10bSkip::NoCensus || G10bSkip == Gates::EG10bSkip::Contended))
			{
				// M10 fix round 1: a T = 1 run long enough for a verdict that has none carries no single-thread proof. Only the
				// short-window Skip (P0's sub-ms windows) rests on the stamp's G10b run; T > 1 is the scaling series.
				InvalidReasons.push_back(std::string("G10b gave no verdict (T = 1 single-thread proof): ") + Gates::ToString(G10bSkip) + " - "
					+ (G10bReport.Notes.empty() ? std::string("?") : G10bReport.Notes.back()));
			}
			if (!bTscInvariant)
			{
				InvalidReasons.push_back("the CPU's TSC is not invariant (CPUID 0x80000007 EDX bit 8): the TSC buckets are not a clock");
			}
			if (bPreflightPassed && !Calibration.bValid)
			{
				InvalidReasons.push_back("the run's TSC / QPC calibration failed");
			}
			else if (bPreflightPassed && !(Calibration.MaxResidual < TscCalibrationResidualMax))
			{
				// M10 fix round 1 (02 9.3, 03 M10 criterion 1, docs/TimingContract.md 6 #12): the bound is checked on every run.
				char Text[160];
				std::snprintf(Text, sizeof(Text), "the run's TSC / QPC calibration residual %.3g >= %.0e (02 9.3): the TSC is not a straight QPC clock here",
					Calibration.MaxResidual, TscCalibrationResidualMax);
				InvalidReasons.push_back(Text);
			}
#if PT_BUILD_WITH_PROFILER
			InvalidReasons.push_back(std::string("profiler build (preset ") + PT_BUILD_PRESET + ": Tracy zones, Jolt's external profile): phase breakdown only, never a headline (03 M10)");
#endif
			bTimingValid = InvalidReasons.empty();
		}

		// ---- 12. outputs (exit.json comes after them, Finish) -------------------------------------------------------------
		void FBenchRun::WriteOutputs()
		{
			if (Scene)
			{
				Recorder.WriteCsv(OutDir / "timings.csv");
				Outputs.push_back("timings.csv");
			}
			if (bPreflightPassed)
			{
				Hashes.WriteCsv(OutDir / "hashes.csv");
				Outputs.push_back("hashes.csv");
			}
			if (bStates)
			{
				BenchRecorders::FStateRecorder::WriteDtypeJson(OutDir / "states.dtype.json", static_cast<uint32>(Scene->GetBodies().size()), States.GetFrameCount(), DtEngine);
				Outputs.push_back("states.ptst");
				Outputs.push_back("states.dtype.json");
			}
			if (bContacts)
			{
				Outputs.push_back("contacts.ptcn");
			}
			if (bNativeDraw)
			{
				if (NativeDraw.WriteCsv(OutDir / "native_draw.csv"))
				{
					Outputs.push_back("native_draw.csv");
				}
				else
				{
					NativeDrawNote = "writing native_draw.csv failed";
				}
			}
			if (Scene)
			{
				WriteText(OutDir / "scene.json", Scenario->MakeCanonicalJson(Params) + "\n");
				Outputs.push_back("scene.json");
			}
			if (bSceneDesc)
			{
				Outputs.push_back("scene_desc.json");   // written before frame 0 (BuildScene, M9)
			}
			Outputs.push_back("manifest.json");
			MakeManifest().Write(OutDir / "manifest.json");

			const int32 FramesRun = static_cast<int32>(Recorder.GetRecords().size());
			std::vector<double> FrameNs;
			for (size_t Frame = 1; Frame < Recorder.GetRecords().size(); ++Frame)
			{
				FrameNs.push_back(static_cast<double>(Recorder.GetRecords()[Frame].Timing.FrameNs));
			}
			const FDurationStats Headline = ComputeDurationStats(FrameNs);
			UE_LOG(LogPhysicsBench, Display, "%s %s %s/%s T=%d mask %s: %d frames, T_frame mean %.3f ms median %.3f ms (frames 1..%d, UNOFFICIAL), h_native %s, "
				"headline-valid %s, host other %.2f%% (limit %.2f%%), status %s -> %s", Spec.Backend.c_str(), Scenario->GetName(), Config.ProfileName.c_str(),
				Files.VariantName.empty() ? "-" : Files.VariantName.c_str(), Config.TestSettings.NumThreads, ToHexMask(Controls.Mask).c_str(), FramesRun,
				Headline.Mean * 1.0e-6, Headline.Median * 1.0e-6, FramesRun - 1, FinalHash.bHasNative ? ToHex64(FinalHash.Native).c_str() : "-", bTimingValid ? "yes" : "no",
				Load.bValid ? Load.CoreBusyOtherCorePct : -1.0, ContaminationLimitPct, Status.c_str(), StringConv::PathToUtf8(OutDir).c_str());
			for (const std::string& Why : InvalidReasons)
			{
				UE_LOG(LogPhysicsBench, Log, "not headline-valid: %s", Why.c_str());
			}
		}

		// ---- 13. teardown (scene, then the module: 02 12.2), after exit.json: recorded apart from the exit code ----------
		void FBenchRun::Teardown()
		{
			const uint64 PhaseBegin = FClockBuckets::Begin();
			Scene.reset();
			if (Spec.bInjectTeardownFatal)
			{
				UE_LOG(LogPhysicsBench, Fatal, "test-only -InjectTeardownFatal: a Fatal during the teardown (exit.json must keep the run's code)");
			}
			const PtResult TermResult = Module->TermGamePhys();
			const double TeardownNs = FClockBuckets::TicksToNs(FClockBuckets::End(PhaseBegin));
			const bool bUeRuntime = FBenchmarkRunner::HasUeRuntimeModule();
			FJson Json;
			Json["schema"] = "physicstest.teardown/1";
			Json["teardown_ns"] = TeardownNs;
			Json["term_game_phys"] = GetPtResultName(TermResult);
			Json["fatal"] = nullptr;
			Json["terminate_process"] = bUeRuntime;
			Json["note"] = "written after exit.json: a teardown failure (also a Fatal) never changes the run's exit code";
			WriteText(OutDir / "teardown.json", Json.dump(2) + "\n");
			if (bUeRuntime)
			{
				FBenchmarkRunner::TerminateProcessNow(static_cast<int32>(Exit));
			}
		}

		// The manifest of the run (docs/ResultsSchema.md 2): every group, from what the run reached. Without a scene (the
		// engine-option preflight refused the variant) the scene-derived fields are null and scene.built is false.
		FRunManifest FBenchRun::MakeManifest() const
		{
			const FPhysicsTestSettings& Test = Config.TestSettings;
			const std::vector<FFrameRecord>& Records = Recorder.GetRecords();
			const int32 FramesRun = static_cast<int32>(Records.size());
			const FBackendInfo& Info = Scene ? Scene->GetBackendInfo() : Module->GetInfo();
			FRunManifest Manifest;
			FJson& J = Manifest.Json;
			J["schema"] = FRunManifest::SchemaId;
			J["status"] = Status;
			J["exit_code"] = static_cast<int32>(Exit);
			J["timing_valid_for_headline"] = bTimingValid;
			J["timing_invalid_reasons"] = InvalidReasons;
			J["unofficial"] = true;              // M10: decided at the end of the manifest (the reasons need every group)
			J["timing_label"] = "";
			J["comparability_key"] = nullptr;    // M10: FRunManifest::ComputeComparabilityKey, at the end
			{
				// M10 (02 12.2 step 7, 12.4): the stamp of this run's key, as the bench found it.
				FJson StampJson;
				StampJson["key_sha256"] = bStampKey ? FJson(Stamp.KeySha256) : FJson(nullptr);
				StampJson["found"] = Stamp.bFound;
				StampJson["valid"] = bStampKey && Stamp.bValid;
				StampJson["reason"] = bStampKey ? Stamp.Reason : (StampKeyError.empty() ? std::string("no stamp key: the run stopped before its preflight") : StampKeyError);
				StampJson["path"] = bStampKey ? FJson(StringConv::PathToUtf8(Stamp.Path)) : FJson(nullptr);
				StampJson["stamp_utc"] = Stamp.StampUtc;
				StampJson["stamp_dir"] = StringConv::PathToUtf8(StampDir);
				StampJson["key"] = bStampKey ? FJson::parse(StampKey.ToCanonicalJson()) : FJson(nullptr);
				J["conformance_stamp"] = StampJson;
			}
			J["run_id"] = RunId;
			J["session"] = Spec.Session;
			J["utc"] = GetUtcTimestamp();
			J["command_line"] = Spec.CommandLine;
			{
				FJson Harness;
				Harness["git_sha"] = PT_GIT_SHA;
				Harness["dirty"] = PT_GIT_DIRTY != 0;
				Harness["git_sha_source"] = PT_GIT_SHA_SOURCE;
				Harness["git_note"] = PT_GIT_NOTE;
				Harness["git_sha_configure"] = PT_BUILD_GIT_SHA;   // the configure-time value (PTBuildInfo.h), for comparison
				const std::filesystem::path Exe = GetExecutablePath();
				Harness["exe"] = StringConv::PathToUtf8(Exe.filename());
				Harness["exe_sha256"] = ExeSha256;
				Harness["preset"] = PT_BUILD_PRESET;
				Harness["do_check"] = PT_DO_CHECK != 0;
				Harness["profiler"] = PT_BUILD_WITH_PROFILER != 0;   // M10: the prof preset (never a headline)
				// M10 fix round 1: the SDK backends compiled into this exe (PT_WITH_<SDK>): a build with one is a single-SDK exe, the
				// binary composition control (02 14.3), whatever its preset is called (Tools/ptbench perf.py).
				FJson Sdks = FJson::array();
#if PT_BUILD_WITH_JOLT
				Sdks.push_back("Jolt");
#endif
#if PT_BUILD_WITH_BULLET
				Sdks.push_back("Bullet");
#endif
#if PT_BUILD_WITH_PHYSX
				Sdks.push_back("PhysX");
#endif
#if PT_BUILD_WITH_CHAOS
				Sdks.push_back("Chaos");
#endif
				Harness["sdks"] = Sdks;
				J["harness"] = Harness;
			}
			{
				FJson Build;
				Build["flag_profile"] = PT_BUILD_FLAG_PROFILE;
				Build["configuration"] = PT_BUILD_CONFIGURATION;
				Build["msvc"] = PT_BUILD_CXX_COMPILER_VERSION;
				Build["cmake"] = PT_BUILD_CMAKE_VERSION;
				Build["crt"] = GetCrtName();
				Build["lto"] = PT_BUILD_LTO != 0;
				Build["flags_json_sha256"] = PT_BUILD_FLAGS_SHA256;
				Build["flags_json_tu_count"] = PT_BUILD_FLAGS_TU_COUNT;
				Build["flags_json_violations"] = PT_BUILD_FLAGS_VIOLATIONS;
				J["build"] = Build;
			}
			J["backend"] = MakeBackendJson(Info, Files, Config, Module);
			{
				FJson Profile;
				Profile["physics"] = Config.ProfileName;
				Profile["profile_file"] = StringConv::PathToUtf8(Files.ProfilePath);
				Profile["variant_file"] = StringConv::PathToUtf8(Files.VariantPath);
				Profile["mass_rule"] = ToString(Test.MassRule);
				Profile["physics_sha256"] = Config.PhysicsSha256();
				Profile["profile_sha256"] = Files.ProfileSha256;
				Profile["variant_sha256"] = Files.VariantSha256;
				Profile["warnings"] = Config.Warnings;
				Profile["resolved"] = MakeResolvedJson(Config);
				Profile["forced_overrides"] = MakeForcedOverridesJson(Config, Scene.get());   // M9 (02 10)
				J["profile"] = Profile;
			}
			if (!bPreflightPassed)
			{
				J["preflight_error"] = PreflightError;
			}
			J["effective"] = ParseEffectiveToJson(Effective);
			J["effective_module"] = ParseEffectiveToJson(ModuleEffective);
			J["missing_effective_keys"] = MissingKeys;
			J["bodies_sampled"] = BodiesSampled;
			{
				FJson SceneJson{ { "name", Scenario->GetName() }, { "version", Scenario->GetVersion() }, { "kind", ToString(Scenario->GetKind()) }, { "params", Params },
					{ "seed", Seed }, { "scene_sha256", Scenario->ComputeSceneSha256(Params) } };
				SceneJson["built"] = Scene != nullptr;
				SceneJson["desc_sha256"] = Scene ? FJson(DescSha256) : FJson(nullptr);
				// desc_sha256 covers harness-computed doubles (mass properties, FRotator -> FQuat) built with /fp:fast: it is
				// comparable within one optimisation level (dev = test = test-avx2), not between dbg (/Od) and the others (Scenes.md).
				SceneJson["desc_sha256_scope"] = std::string("engines, variants and flag profiles of one build configuration (") + PT_BUILD_CONFIGURATION + ")";
				SceneJson["options_sha256"] = Scene ? FJson(OptionsSha256) : FJson(nullptr);
				SceneJson["num_bodies"] = Scene ? FJson(Scene->GetStats().NumBodies) : FJson(nullptr);
				SceneJson["num_dynamic_bodies"] = Scene ? FJson(Scene->GetStats().NumDynamicBodies) : FJson(nullptr);
				SceneJson["start_asleep_skipped"] = Scene ? FJson(Scene->GetStats().NumStartAsleepSkipped) : FJson(nullptr);
				// M9: joints (spawned; kept host-only because the backend lacks the type - a refused run never gets here).
				SceneJson["num_constraints"] = Scene ? FJson(Scene->GetStats().NumConstraints) : FJson(nullptr);
				SceneJson["num_unsupported_constraints"] = Scene ? FJson(Scene->GetStats().NumUnsupportedConstraints) : FJson(nullptr);
				SceneJson["required_capabilities"] = MakeCapabilitiesJson(Scenario->GetRequiredCapabilities(Params));
				SceneJson["chaotic"] = Scenario->IsChaotic();
				const FScenarioCapacity Hints = Scenario->GetCapacity(Params);
				SceneJson["capacity_hints"] = { Hints.MaxBodies, Hints.MaxBodyPairs, Hints.MaxContactConstraints };
				FJson Windows = FJson::array();
				for (const FStepWindow& Window : Scenario->GetWindows(Params))
				{
					Windows.push_back({ { "name", Window.Name }, { "first", Window.First }, { "last", Window.Last } });
				}
				SceneJson["windows"] = Windows;
				J["scene"] = SceneJson;
			}
			{
				FJson Run;
				Run["mode"] = ToString(Test.RunMode);
				Run["instrumentation"] = GetInstrumentNames(Test.InstrumentFlags);
				Run["instrument_flags"] = Test.InstrumentFlags;
				Run["scene_desc_instrument_flags"] = Scene ? FJson(Scene->GetSceneDesc().InstrumentFlags) : FJson(nullptr);   // what the backend received
				Run["frames"] = Frames;
				Run["frames_run"] = FramesRun;
				Run["dt_s"] = static_cast<double>(FrameDeltaSeconds);
				Run["dt_engine_s"] = DtEngine;
				Run["dt_engine_last_plan_s"] = Scene ? FJson(static_cast<double>(Scene->GetLastPlan().StepDeltaTime)) : FJson(nullptr);
				Run["tick_mode"] = ToString(Test.TickMode);
				Run["clamp_rule"] = ToString(Test.SubstepClampRule);
				Run["frame_driver"] = Scene ? FJson(ToString(Scene->GetFrameDriver())) : FJson(nullptr);
				// How the host syncs with the engine (02 9.1, 9.3): commands pushed once per engine step (Harness driver) or
				// once per frame (Native driver), states pulled once per frame with PT_READ_ACTIVE_ONLY.
				Run["sync_policy"] = !Scene ? FJson(nullptr) : FJson(Scene->GetFrameDriver() == EFrameDriver::Native ? "push_per_frame+pull_active_only_per_frame"
					: "push_per_step+pull_active_only_per_frame");
				Run["hash_every"] = Test.HashEvery;
				Run["states_every"] = StatesEvery;
				Run["counters_every"] = Test.CountersEvery;
				Run["capture_every"] = Test.CaptureEvery;
				Run["pre_roll_s"] = Spec.PreRollSeconds;
				Run["async_interpolated"] = Scene ? FJson(Scene->GetLastPlan().bAsyncInterpolated) : FJson(nullptr);
				Run["steps_total"] = Scene ? FJson(Scene->GetStepCount()) : FJson(nullptr);
				Run["sim_time_s"] = Scene ? FJson(Scene->GetSimTime()) : FJson(nullptr);
				FJson Injections = FJson::object();
				if (Spec.InjectEngineErrorBits != 0)
				{
					Injections["engine_error_bits"] = Spec.InjectEngineErrorBits;   // test only: not an engine report
				}
				if (!Spec.InjectPreflightFault.empty())
				{
					Injections["preflight_fault"] = Spec.InjectPreflightFault;
				}
				if (Spec.InjectSceneInstrumentFlags != 0)
				{
					Injections["scene_instrument_flags"] = Spec.InjectSceneInstrumentFlags;
				}
				if (Spec.InjectStatePerturbationFrame >= 0)
				{
					Injections["state_perturbation_frame"] = Spec.InjectStatePerturbationFrame;
				}
				if (Spec.InjectTickAllocationFrame >= 0)
				{
					Injections["tick_allocation_frame"] = Spec.InjectTickAllocationFrame;
				}
				if (Spec.InjectSilentExitFrame >= 0)
				{
					Injections["silent_exit_frame"] = Spec.InjectSilentExitFrame;   // only when the run ended before that frame
				}
				if (Spec.InjectForeignWorkMs > 0)
				{
					Injections["foreign_work_ms"] = Spec.InjectForeignWorkMs;   // test only: G10b's negative control
				}
				Run["test_injections"] = Injections;
				J["run"] = Run;
			}
			{
				FJson Threads;
				Threads["T"] = Test.NumThreads;
				Threads["affinity_mask"] = ToHexMask(Controls.Mask);
				Threads["affinity_applied"] = Controls.bAffinity;
				Threads["affinity_before"] = ToHexMask(Controls.AffinityBefore);
				Threads["logical_cpus"] = MaskToCpus(Controls.Mask);
				Threads["smt_sibling_mask"] = ToHexMask(Controls.SiblingMask);
				// 02 9.6: the HT and E-core series are flagged apart from the default one-logical-CPU-per-P-core series.
				Threads["series"] = Controls.Shape.Series;
				Threads["cores"] = Controls.Shape.Cores;
				Threads["smt_pair_cores"] = Controls.Shape.SmtPairCores;
				Threads["e_cores"] = Controls.Shape.ECores;
				Threads["mapping"] = "02 9.6: one logical CPU per P-core (the first T of " + std::to_string(Topology.PCoreFirstLogicals.size()) + "); Jolt workers T-1, "
					"PhysX T = 1 inline / T >= 2 PxDefaultCpuDispatcherCreate(T), Bullet T = 1 only";
				// The engine's own report of its worker threads (the threads.workers READBACK key) and every engine thread key.
				const auto Workers = std::find_if(Settings.begin(), Settings.end(), [](const FEffectiveSetting& S) { return S.Key == "threads.workers"; });
				if (Workers != Settings.end())
				{
					const bool bWhole = Workers->bHasCanonical && Workers->Canonical == std::floor(Workers->Canonical) && std::abs(Workers->Canonical) < 1.0e9;
					Threads["engine_native"] = { { "workers", bWhole ? FJson(static_cast<int64>(Workers->Canonical)) : (Workers->bHasCanonical ? FJson(Workers->Canonical) : FJson(nullptr)) },
						{ "native", Workers->Native }, { "source", Workers->Source } };
				}
				else
				{
					Threads["engine_native"] = nullptr;
				}
				FJson EngineThreadKeys = FJson::object();
				for (const FEffectiveSetting& Setting : Settings)
				{
					if (Setting.Key.rfind("threads.", 0) == 0 || Setting.Key.find("max_threads") != std::string::npos || Setting.Key.find("dispatcher") != std::string::npos)
					{
						EngineThreadKeys[Setting.Key] = Setting.Native;
					}
				}
				for (const FJson& Line : ParseEffectiveToJson(ModuleEffective))
				{
					if (Line.is_object() && Line.contains("key") && Line["key"].is_string() && Line["key"].get<std::string>().find("max_threads") != std::string::npos)
					{
						EngineThreadKeys[Line["key"].get<std::string>()] = Line.value("native", std::string());
					}
				}
				Threads["engine_thread_settings"] = EngineThreadKeys;
				Threads["scaling_note"] = Test.NumThreads > 1 ? "T > 1: cpu_eff and the foreign thread cycles of the census window come with the run (M10); "
					"S(T) / E(T) are read next to them (G10b judges T = 1 only)" : "T = 1: G10b (cpu_eff, foreign thread cycles) is in gates[]";
				// M10 (02 12.2 steps 5 / 11): census #1 after InitGamePhys, the window around the loop.
				Threads["census_init"] = CensusInit.IsValid() || !CensusInit.GetError().empty() ? MakeCensusThreadsJson(CensusInit) : FJson(nullptr);
				Threads["census_window"] = bG10b ? MakeCensusWindowJson(CensusWindow, G10bSkip) : FJson(nullptr);
				Threads["foreign_cycles_fraction"] = bG10b && CensusWindow.bValid ? FJson(CensusWindow.ForeignFraction) : FJson(nullptr);
				J["threads"] = Threads;
			}
			{
				FJson Platform;
				Platform["cpu"] = Host.CpuBrand;
				Platform["logical_processors"] = Host.LogicalProcessors;
				Platform["p_cores"] = Topology.PCoreFirstLogicals;
				Platform["os_build"] = Host.OsBuild;
				Platform["priority_class"] = Host.PriorityClass;
				Platform["priority_before"] = Controls.PriorityBefore;
				Platform["priority_set"] = Controls.bPriority;
				Platform["throttling"] = { { "opt_out_applied", Controls.bThrottling }, { "control", "PROCESS_POWER_THROTTLING_EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, state 0" } };
				Platform["timer_resolution"] = { { "requested_ms", 1.0 }, { "applied", Controls.bTimer }, { "before", MakeTimerJson(Controls.TimerBefore) },
					{ "after", MakeTimerJson(Controls.TimerAfter) } };
				Platform["host_control_errors"] = Controls.Errors;
				Platform["power_scheme"] = Host.PowerSchemeName;
				Platform["power_scheme_guid"] = Host.PowerSchemeGuid;
				Platform["power_scheme_high_performance"] = IsHighPerformanceScheme(Host);
				Platform["qpc_hz"] = Host.QpcHz;
				FJson Mhz = FJson::array();
				for (const FCpuFrequency& Cpu : CpuFrequencies)
				{
					Mhz.push_back({ { "cpu", Cpu.Cpu }, { "current_mhz", Cpu.CurrentMhz }, { "max_mhz", Cpu.MaxMhz }, { "mhz_limit", Cpu.MhzLimit } });
				}
				Platform["cpu_mhz"] = Mhz;
				Platform["cpu_mhz_note"] = "CallNtPowerInformation(ProcessorInformation) on the run's CPUs before the run, as read (hybrid Intel parts may report the nominal value)";
				Platform["system_busy_other_pct"] = Load.bValid ? FJson(Load.SystemBusyOtherCorePct) : FJson(nullptr);
				FJson HostLoad;
				HostLoad["valid"] = Load.bValid;
				HostLoad["window"] = "spin pre-roll start .. last frame";
				HostLoad["wall_s"] = Load.WallSeconds;
				HostLoad["unit"] = "% of one core (TSC cycles; QueryIdleProcessorCycleTime, QueryProcessCycleTime)";
				HostLoad["own_process_pct"] = Load.OwnProcessCorePct;
				HostLoad["core_mask"] = ToHexMask(Controls.CoreMask);
				HostLoad["cores"] = Controls.Shape.Cores;
				HostLoad["core_busy_other_pct"] = Load.CoreBusyOtherCorePct;   // the exit-4 measure
				HostLoad["mask_busy_other_pct"] = Load.MaskBusyOtherCorePct;
				HostLoad["smt_sibling_busy_pct"] = Load.SiblingBusyCorePct;
				HostLoad["system_busy_other_pct"] = Load.SystemBusyOtherCorePct;
				FJson PerCpu = FJson::array();
				for (const uint32 Cpu : MaskToCpus(Controls.CoreMask))
				{
					if (Cpu < Load.PerCpuBusyPct.size())
					{
						PerCpu.push_back({ { "cpu", Cpu }, { "in_mask", ((Controls.Mask >> Cpu) & 1ull) != 0 }, { "busy_pct", Load.PerCpuBusyPct[Cpu] } });
					}
				}
				HostLoad["per_cpu"] = PerCpu;
				HostLoad["policy"] = Spec.HostCheck == EHostCheckPolicy::Enforce ? "enforce" : "record";
				HostLoad["threshold_pct"] = Spec.ContaminationPct;             // per core (-ContaminationPct)
				HostLoad["threshold_total_pct"] = ContaminationLimitPct;       // per core x cores: what core_busy_other_pct is compared with
				HostLoad["contaminated"] = bContaminated;
				Platform["host_load"] = HostLoad;
				J["platform"] = Platform;
			}
			{
				FJson Clock;
				Clock["source"] = "TSC (FCycleCounter: rdtscp + lfence per bucket), calibrated against QPC over the whole run (M10, 02 9.3)";
				Clock["qpc_hz"] = Host.QpcHz;
				Clock["qpc_tick_ns"] = Host.QpcHz > 0 ? 1.0e9 / static_cast<double>(Host.QpcHz) : 0.0;
				Clock["read_overhead_ns"] = ClockReadOverheadNs;
				Clock["read_overhead_method"] = "QPC: median of 32 batches of 20000 FClockBuckets::Begin() calls, after the host controls";
				Clock["tsc_hz"] = Calibration.bValid ? FJson(Calibration.TscHz) : FJson(nullptr);
				Clock["tsc_invariant"] = bTscInvariant;
				Clock["tsc_read_overhead_ns"] = TscReadOverheadNs;
				Clock["tsc_read_overhead_method"] = "median of 32 batches of 20000 FCycleCounter::Read() (rdtscp + lfence) calls, after the host controls";
				Clock["ns_per_tick"] = Calibration.bValid ? FJson(Calibration.NsPerTick) : FJson(nullptr);
				Clock["ns_per_tick_provisional"] = ProvisionalNsPerTick;
				Clock["calibration"] = { { "valid", Calibration.bValid }, { "pairs", Calibration.Pairs }, { "span_s", Calibration.SpanSeconds },
					{ "max_residual", Calibration.MaxResidual }, { "rms_residual", Calibration.RmsResidual }, { "max_pair_spread_ticks", Calibration.MaxPairSpreadTicks },
					{ "pair_every_frames", CalibrationPairEvery },
					{ "method", "two-point TSC / QPC calibration between the first pair (host controls) and the last (after the loop); residual = the largest "
						"|(Q_i - Q_0) / (Q_n - Q_0) - (T_i - T_0) / (T_n - T_0)| over every pair (before / after the pre-roll and every pair_every_frames frames); "
						"every bucket of timings.csv is converted with ns_per_tick (03 M10: residual < 1e-5)" } };
				J["clock"] = Clock;
			}
			J["order"] = { { "repeat_index", Spec.RepeatIndex }, { "order_index", Spec.OrderIndex }, { "block_seed", Spec.BlockSeed } };
			{
				FJson Phases;
				Phases["backend_init"] = FClockBuckets::TicksToNs(BackendInitTicks);
				Phases["option_preflight"] = FClockBuckets::TicksToNs(OptionPreflightTicks);
				Phases["scene_create"] = FClockBuckets::TicksToNs(SceneCreateTicks);
				Phases["preflight"] = FClockBuckets::TicksToNs(PreflightTicks);
				Phases["prepare"] = FClockBuckets::TicksToNs(PrepareTicks);
				Phases["pre_roll"] = FClockBuckets::TicksToNs(PreRollTicks);
				Phases["frame0"] = FClockBuckets::TicksToNs(Frame0Ticks);
				Phases["loop_after_frame0"] = FClockBuckets::TicksToNs(LoopTicks);
				Phases["teardown"] = nullptr;   // after the outputs: teardown.json
				Phases["note"] = "wall time of each phase (QPC); frame0 and the loop include the untimed per-frame host work";
				J["phases_ns"] = Phases;
			}
			{
				// Process counters after the last frame (peaks cover the whole run) and the backend's own numbers (02 12.3).
				FJson Memory;
				FJson Process;
				Process["valid"] = ProcessMemory.bValid;
				Process["peak_commit_bytes"] = ProcessMemory.PeakCommitBytes;
				Process["commit_bytes"] = ProcessMemory.CommitBytes;
				Process["peak_working_set_bytes"] = ProcessMemory.PeakWorkingSetBytes;
				Process["working_set_bytes"] = ProcessMemory.WorkingSetBytes;
				Memory["process"] = Process;
				FJson Backend;
				if (!Scene)
				{
					Backend["error"] = "no scene (the engine-option preflight refused the variant)";
				}
				else if (BackendMemoryResult == PT_OK)
				{
					Backend["live_bytes"] = BackendMemory.LiveBytes;
					Backend["peak_bytes"] = BackendMemory.PeakBytes;
					Backend["num_allocs"] = BackendMemory.NumAllocs;   // -1 = the backend cannot count
					Backend["num_frees"] = BackendMemory.NumFrees;
					Backend["allocator_kind"] = BackendMemory.AllocatorKind;
				}
				else
				{
					Backend["error"] = GetPtResultName(BackendMemoryResult);
				}
				Memory["backend_scene"] = Backend;
				J["memory"] = Memory;
			}
			{
				// 03 M7: "test 빌드에서 타이밍 루프 안 할당이 0이다 (카운팅 훅)" - operator new calls inside Tick (the timed buckets),
				// in frame 0 and over the whole loop; and the engine's own allocator over the loop (docs/TimingContract.md 4.1).
				FJson Alloc;
				Alloc["hook_installed"] = FAllocationCounters::bHookInstalled.load();
				Alloc["hook"] = "PhysicsTestBench.exe replaces the global operator new / delete (AllocationHook.cpp): harness, Kit, static adapters, SDK operator new";
				// The replacement binds inside the exe image only: a DLL backend (Null DLL, Chaos) allocates through its own
				// operator new / FMemory, which the hook never sees (its tick_new_calls 0 says nothing about it).
				Alloc["hook_scope"] = Module != nullptr && Module->IsDll() ? "exe image only: this DLL backend's own allocations are not seen (backend_allocs_* are)"
					: "exe image (the static backend and its SDK included)";
				Alloc["tick_new_calls"] = TickAllocations;
				Alloc["tick_delete_calls"] = TickFrees;
				Alloc["tick_first_allocating_frame"] = FirstAllocatingFrame;
				Alloc["frame0_new_calls"] = Frame0Allocations;
				Alloc["loop_new_calls"] = LoopAllocations;
				const bool bBackendLoop = BackendAllocsBeforeLoop >= 0 && BackendAllocsAfterLastTick >= 0;
				const bool bBackendFrame0 = BackendAllocsBeforeLoop >= 0 && BackendAllocsAfterFrame0 >= 0;
				Alloc["backend_allocs_in_loop"] = bBackendLoop ? FJson(BackendAllocsAfterLastTick - BackendAllocsBeforeLoop) : FJson(nullptr);
				Alloc["backend_allocs_frame0"] = bBackendFrame0 ? FJson(BackendAllocsAfterFrame0 - BackendAllocsBeforeLoop) : FJson(nullptr);
				Alloc["backend_allocs_after_frame0"] = bBackendLoop && bBackendFrame0 ? FJson(BackendAllocsAfterLastTick - BackendAllocsAfterFrame0) : FJson(nullptr);
				Alloc["backend_note"] = "PtMemoryStats.NumAllocs (the engine's own allocator: PhysX PxAllocatorCallback, Jolt JPH::Allocate family, Bullet btAlignedAlloc, "
					"Null CRT) right before frame 0, after frame 0's Tick and after the last Tick; null = the backend cannot count. Engine behaviour: "
					"recorded, not a headline condition";
				J["alloc"] = Alloc;
			}
			{
				// Window statistics of the per-frame buckets; frame 0 is reported apart (02 9.5).
				FJson Summary;
				Summary["unit"] = "ns";
				Summary["frame0_frame_ns"] = Records.empty() ? FJson(nullptr) : FJson(static_cast<double>(Records[0].Timing.FrameNs));
				FJson WindowsJson = FJson::object();
				for (const FStepWindow& Window : Scenario->GetWindows(Params))
				{
					WindowsJson[Window.Name] = MakeWindowJson(Window, Records, Recorder.GetNsPerTick());
				}
				Summary["windows"] = WindowsJson;
				Summary["cpu_eff"] = bG10b && CensusWindow.bValid ? FJson(CensusWindow.CpuEff) : FJson(nullptr);   // M10: the census window (G10b)
				Summary["note"] = "T_frame = sum(T_push) + sum(T_step) + T_pull; T_step is not comparable with Chaos (02 9.3)";
				J["summary"] = Summary;
			}
			{
				FJson Determinism;
				Determinism["h_native"] = FinalHash.bHasNative ? ToHex64(FinalHash.Native) : std::string();
				Determinism["h_native_frame"] = FramesRun > 0 ? FramesRun - 1 : -1;
				Determinism["h_quantized"] = FramesRun > 0 ? ToHex64(FinalHash.Quantized) : std::string();
				Determinism["h_state"] = FramesRun > 0 ? ToHex64(FinalHash.State) : std::string();
				Determinism["engine_error_bits"] = ErrorBits;
				Determinism["capacity_error_bits_declared"] = CapacityBitsDeclared;   // the backend's errors.capacity_bits
				Determinism["capacity_error_bits"] = CapacityErrorBits;
				Determinism["other_error_bits"] = OtherErrorBits;
				J["determinism"] = Determinism;
			}
			{
				FJson GatesJson = FJson::array();
				for (const FGateReport& Report : Preflight.Reports)
				{
					GatesJson.push_back(MakeGateReportJson(Report));
				}
				if (bG10b)
				{
					GatesJson.push_back(MakeGateReportJson(G10bReport));   // M10: after the loop (census window)
				}
				J["gates"] = GatesJson;
			}
			if (Scene)
			{
				FJson Stats;
				Stats["active_bodies"] = StepStats.NumActiveBodies;
				Stats["sleeping_bodies"] = StepStats.NumSleepingBodies;
				Stats["contact_pairs"] = StepStats.NumContactPairs;
				Stats["contact_points"] = StepStats.NumContactPoints;
				Stats["islands"] = StepStats.NumIslands;
				J["step_stats_final"] = Stats;
			}
			else
			{
				J["step_stats_final"] = nullptr;
			}
			{
				FJson Recorders;
				Recorders["hashes"] = { { "rows", Hashes.GetRows().size() }, { "every", Test.HashEvery } };
				Recorders["states"] = { { "frames", bStates ? States.GetFrameCount() : 0u }, { "every", StatesEvery }, { "error", StatesError } };
				Recorders["contacts"] = { { "frames", bContacts ? Contacts.GetFrameCount() : 0u }, { "points", bContacts ? Contacts.GetPointCount() : 0ull },
					{ "every", Test.CaptureEvery }, { "note", ContactsNote } };
				Recorders["native_draw"] = { { "frames", bNativeDraw ? NativeDraw.GetRows().size() : size_t(0) }, { "commands", bNativeDraw ? NativeDraw.GetCommandCount() : 0ull },
					{ "truncated_frames", bNativeDraw ? NativeDraw.GetTruncatedFrames() : 0u }, { "every", bNativeDrawTier ? Test.CaptureEvery : 0 },
					{ "note", bNativeDrawTier ? NativeDrawNote : std::string("native_debug_draw tier off") } };
				FJson PhaseNames = FJson::array();
				for (const std::string& Name : Info.PhaseTimerNames)
				{
					PhaseNames.push_back(Name);
				}
				Recorders["phases"] = { { "frames", PhaseFrames }, { "timers", PhaseNames },
					{ "note", !bPhasesTier ? "phases tier off" : (Info.Has(PT_CAP_PHASE_TIMERS) ? "solver_ns = the sum of the backend's phase timers, every frame"
						: "the backend declares no phase timers (PT_CAP_PHASE_TIMERS): solver_ns stays empty") } };
				J["recorders"] = Recorders;
			}
			J["deferred_fields"] = MakeDeferredFieldsJson();
			J["outputs"] = Outputs;
			J["wall_time_s"] = FClockBuckets::TicksToNs(FClockBuckets::End(RunBegin)) * 1.0e-9;
			// M10: the comparability key (02 13) from the groups above, and whether the timing is reportable (R9).
			J["comparability_key"] = Scene ? FJson(Manifest.ComputeComparabilityKey()) : FJson(nullptr);
			std::vector<std::string> Unofficial;
			if (!bTimingValid)
			{
				Unofficial.push_back("not headline-valid (timing_invalid_reasons)");
			}
			if (!(bStampKey && Stamp.bValid))
			{
				Unofficial.push_back("no valid conformance stamp for this run's key (" + (bStampKey ? Stamp.Reason : std::string("no key")) + ")");
			}
			if (!IsHighPerformanceScheme(Host))
			{
				Unofficial.push_back("the power plan is not High performance / Ultimate (" + Host.PowerSchemeName + ")");
			}
			J["unofficial"] = !Unofficial.empty();
			J["unofficial_reasons"] = Unofficial;
			J["timing_label"] = MakeTimingLabel(Unofficial);
			return Manifest;
		}
	}

	EBenchExit FBenchmarkRunner::Run(const FBenchRunSpec& Spec)
	{
		FBenchRun Run(Spec);
		return Run.Execute();
	}

	// ---- -Conformance (M6) -------------------------------------------------------------------------------------------
	EBenchExit FBenchmarkRunner::Conformance(const FBenchRunSpec& Spec)
	{
		const uint64 RunBegin = FClockBuckets::Begin();
		// The run directory first: every outcome, a refusal before any gate ran included, leaves exit.json there (02 12.2: the
		// runner trusts exit.json and treats a missing one as a crashed run).
		const std::string RunId = MakeRunId();
		std::filesystem::path OutDir = !Spec.OutDir.empty() ? Spec.OutDir : GetDefaultResultsDir() / StringConv::Utf8ToPath(Spec.Session) / ("conformance_" + RunId);
		OutDir.make_preferred();
		std::error_code DirError;
		std::filesystem::create_directories(OutDir, DirError);
		if (DirError)
		{
			UE_LOG(LogPhysicsBench, Error, "cannot create '%s': %s", StringConv::PathToUtf8(OutDir).c_str(), DirError.message().c_str());
			return EBenchExit::GateFailed;   // no directory: no exit.json possible
		}
		// A Fatal from here on (a gate's check, a UE module's Fatal) writes exit.json (3, fatal) first; with a UE_RUNTIME module
		// the process then ends with TerminateProcess (M7 review: M13 runs every Chaos gate through -Conformance).
		FFatalExitScope FatalScope(OutDir, RunId);
		const auto Finish = [&](const EBenchExit Code, const char* Status, const char* Category, const std::string& Reason, const std::vector<std::string>& Outputs)
		{
			FExitRecord Record;
			Record.Code = Code;
			Record.Status = Status;
			Record.Category = Category;
			Record.Reason = Reason;
			Record.RunId = RunId;
			Record.bTerminateProcess = HasUeRuntimeModule();
			Record.Outputs = Outputs;
			Record.WriteAndFlush(OutDir);
			return Code;
		};
		const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
		if (std::find(Available.begin(), Available.end(), Spec.Backend) == Available.end())
		{
			const std::string Message = "backend '" + Spec.Backend + "' is not available in this build";
			UE_LOG(LogPhysicsBench, Error, "%s", Message.c_str());
			return Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Message, {});
		}
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		std::string Error;
		if (!LoadBenchConfig(Spec, Config, Files, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return Finish(EBenchExit::GateFailed, "usage_error", "usage", Error, {});
		}
		const std::filesystem::path ConfigDir = Spec.ConfigDir.empty() ? GetDefaultConfigDir() : std::filesystem::path(Spec.ConfigDir).make_preferred();
		Gates::FTolerances Tolerances;
		if (!Tolerances.LoadFile(Gates::GetTolerancesPath(ConfigDir), Error))
		{
			UE_LOG(LogPhysicsBench, Error, "tolerances: %s", Error.c_str());
			return Finish(EBenchExit::GateFailed, "usage_error", "usage", "tolerances: " + Error, {});
		}
		if (Spec.bInjectFatal)
		{
			// Test only (CTest PhysicsTest.Bench.UeRuntimeConformanceFatal): a Fatal with the backend module loaded.
			if (FPhysicsBackendRegistry::Get().FindOrLoadModule(Spec.Backend, Error) == nullptr)
			{
				return Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Error, {});
			}
			UE_LOG(LogPhysicsBench, Fatal, "test-only -InjectFatal in -Conformance (exit.json must say 3; a UE_RUNTIME module ends with TerminateProcess)");
		}

		Gates::FGateTarget Target;
		Target.Backend = Spec.Backend;
		Target.Variant = Files.VariantName;
		Target.Profile = Config.ProfileName;
		Target.Config = &Config;
		Target.Tolerances = &Tolerances;
		// -Gates= may name ABI gates (A0-A7, A10) as well as physics gates. The ABI gates run first, each on an uninitialised
		// module (each Inits and Terms it once). A PT_MODULE_NO_REINIT backend (the Chaos bridge) can be initialised once per
		// process, so one process runs ONE ABI gate or the physics gates (which share one Init); the caller starts a process
		// for each (docs/ChaosBackendGuide.md 11). Any other list is refused here (exit 2) instead of failing half-way.
		// -InjectEngineErrorBits (test only) is OR-ed into every physics-gate frame's engine error bits: G12 must fail.
		std::vector<std::string> AbiGateIds;
		std::vector<std::string> PhysicsGateIds;
		// M10: G10b is not an in-process gate - it runs a fresh T = 1 bench run (its census window). It runs with the whole suite
		// (no -Gates=) or when named.
		bool bRunG10b = Spec.Gates.empty();
		for (const std::string& Id : Spec.Gates)
		{
			if (Id == "G10b" || Id == "g10b" || Id == "G10B")
			{
				bRunG10b = true;
				continue;
			}
			const bool bAbi = Id.size() >= 2 && (Id[0] == 'A' || Id[0] == 'a') && Id[1] >= '0' && Id[1] <= '9';
			(bAbi ? AbiGateIds : PhysicsGateIds).push_back(Id);
		}
		const bool bOnlyG10b = !Spec.Gates.empty() && AbiGateIds.empty() && PhysicsGateIds.empty();
		if (!AbiGateIds.empty())
		{
			const FPhysicsBackendModule* Loaded = FPhysicsBackendRegistry::Get().FindOrLoadModule(Spec.Backend, Error);
			if (Loaded == nullptr)
			{
				UE_LOG(LogPhysicsBench, Error, "conformance: %s", Error.c_str());
				return Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Error, {});
			}
			if ((Loaded->GetInfo().ModuleFlags & PT_MODULE_NO_REINIT) != 0 && (AbiGateIds.size() > 1 || !PhysicsGateIds.empty()))
			{
				std::string List;
				for (const std::string& Id : Spec.Gates)
				{
					List += (List.empty() ? "" : ",") + Id;
				}
				const std::string Message = "backend '" + Spec.Backend + "' declares PT_MODULE_NO_REINIT: one -Conformance process runs one ABI gate (-Gates=A0) or the "
					"physics gates, not -Gates=" + List + " (an ABI gate terminates the module and it cannot be initialised again; run one process per ABI gate)";
				UE_LOG(LogPhysicsBench, Error, "%s", Message.c_str());
				return Finish(EBenchExit::GateFailed, "usage_error", "usage", Message, {});
			}
		}
		if (Spec.InjectEngineErrorBits != 0 && PhysicsGateIds.empty() && !AbiGateIds.empty())
		{
			UE_LOG(LogPhysicsBench, Error, "-InjectEngineErrorBits applies to the physics gates' frames; -Gates names ABI gates only");
			return Finish(EBenchExit::GateFailed, "usage_error", "usage", "-InjectEngineErrorBits with ABI gates only", {});
		}
		Target.InjectedEngineErrorBits = Spec.InjectEngineErrorBits;
		std::vector<FGateReport> Reports;
		for (const std::string& Id : AbiGateIds)
		{
			std::string Upper = Id;
			Upper[0] = 'A';
			if (Upper == "A10")
			{
				Reports.push_back(AbiGates::RunA10FrameModel(Spec.Backend));
				continue;
			}
			AbiGates::FAbiGateTarget AbiTarget;
			if (!AbiGates::PrepareAbiGateTarget(Spec.Backend, AbiTarget, Error))
			{
				UE_LOG(LogPhysicsBench, Error, "conformance %s: %s", Upper.c_str(), Error.c_str());
				return Finish(EBenchExit::GateFailed, "gate_failed", "gate", Error, {});
			}
			using FAbiGate = FGateReport (*)(const AbiGates::FAbiGateTarget&);
			const std::pair<const char*, FAbiGate> AbiGateTable[] = { { "A0", &AbiGates::RunA0Lifecycle }, { "A1", &AbiGates::RunA1VersionHandshake },
				{ "A2", &AbiGates::RunA2OtherThread }, { "A3", &AbiGates::RunA3PhaseStateMachine }, { "A4", &AbiGates::RunA4NegativeCases },
				{ "A5", &AbiGates::RunA5CapacityAndStride }, { "A6", &AbiGates::RunA6FloatingPointState }, { "A7", &AbiGates::RunA7EffectiveKeys } };
			const auto Found = std::find_if(std::begin(AbiGateTable), std::end(AbiGateTable), [&Upper](const auto& Entry) { return Upper == Entry.first; });
			if (Found == std::end(AbiGateTable))
			{
				UE_LOG(LogPhysicsBench, Error, "unknown ABI gate '%s' (A0-A7, A10; A8 is M14, A9 is Tools/check_modules.py)", Id.c_str());
				return Finish(EBenchExit::GateFailed, "usage_error", "usage", "unknown ABI gate " + Id, {});
			}
			Reports.push_back(Found->second(AbiTarget));
		}
		// M10 fix round 0 (02 14.2: the ABI gates apply to every backend; 12.4): the whole suite of a DLL backend runs the ABI gates
		// too, each in a fresh process, so its stamp - keyed by the DLL's SHA-256 - certifies them on that binary (a rebuilt Chaos
		// bridge in M13 is stamped only after A0-A7 and A10 passed on it). A static backend is part of the exe, whose ABI gates
		// CTest runs (PhysicsTest.Abi.*).
		if (Spec.Gates.empty())
		{
			const FPhysicsBackendModule* Loaded = FPhysicsBackendRegistry::Get().FindOrLoadModule(Spec.Backend, Error);
			if (Loaded == nullptr)
			{
				UE_LOG(LogPhysicsBench, Error, "conformance: %s", Error.c_str());
				return Finish(EBenchExit::BackendUnavailable, "backend_unavailable", "backend", Error, {});
			}
			if (Loaded->IsDll())
			{
				for (const char* Id : ConformanceAbiGateIds)
				{
					std::string Lower = Id;
					Lower[0] = 'a';
					Reports.push_back(RunConformanceAbiGate(Spec, OutDir / ("abi_" + Lower), Id, Target.GetLabel()));
				}
			}
		}
		if (!bOnlyG10b && (!PhysicsGateIds.empty() || AbiGateIds.empty()))
		{
			std::vector<FGateReport> PhysicsReports;
			if (!Gates::RunPhysicsGates(Target, PhysicsGateIds, PhysicsReports, Error))
			{
				UE_LOG(LogPhysicsBench, Error, "conformance %s: %s", Target.GetLabel().c_str(), Error.c_str());
				return Finish(EBenchExit::GateFailed, "gate_failed", "gate", Error, {});
			}
			Reports.insert(Reports.end(), PhysicsReports.begin(), PhysicsReports.end());
		}
		if (bRunG10b)
		{
			// The module of this process stays idle meanwhile (the child is another process with its own module).
			Reports.push_back(RunConformanceG10b(Spec, OutDir / "g10b_run", Target.GetLabel()));
		}

		int32 Passed = 0;
		int32 Failed = 0;
		int32 Skipped = 0;
		FJson GateList = FJson::array();
		// The post-run registrations (tolerances.json "registered": "post-run") the verdicts used, once each, with their sign-off:
		// M8 / M10 tooling flags or refuses a pass that rests on a deviation the lead has not signed (M6 round 2).
		FJson PostRunList = FJson::array();
		std::vector<std::string> PostRunSeen;
		int32 PendingSignOffs = 0;
		const auto PostRunJson = [](const FGatePostRunRegistration& Used)
		{
			return FJson{ { "key", Used.Key }, { "engine", Used.Engine }, { "variant", Used.Variant }, { "profile", Used.Profile }, { "value", Used.Value },
				{ "sign_off", Used.SignOff } };
		};
		for (const FGateReport& Report : Reports)
		{
			Passed += Report.Result == EGateResult::Pass ? 1 : 0;
			Failed += Report.Result == EGateResult::Fail ? 1 : 0;
			Skipped += Report.Result == EGateResult::Skip ? 1 : 0;
			const Gates::IConformanceGate* Gate = Gates::FindPhysicsGate(Report.Gate);
			FJson Entry;
			Entry["id"] = Report.Gate;
			Entry["description"] = Gate != nullptr ? Gate->GetDescription() : "ABI gate (docs/Abi.md 8)";
			Entry["result"] = ToString(Report.Result);
			Entry["checks"] = Report.NumChecks;
			Entry["failures"] = Report.Failures;
			Entry["notes"] = Report.Notes;
			FJson Used = FJson::array();
			for (const FGatePostRunRegistration& Registration : Report.PostRunRegistrations)
			{
				Used.push_back(PostRunJson(Registration));
				const std::string Id = Registration.Key + "|" + Registration.Engine + "|" + Registration.Variant + "|" + Registration.Profile;
				if (std::find(PostRunSeen.begin(), PostRunSeen.end(), Id) == PostRunSeen.end())
				{
					PostRunSeen.push_back(Id);
					PostRunList.push_back(PostRunJson(Registration));
					PendingSignOffs += Registration.SignOff == "pending" ? 1 : 0;
				}
			}
			Entry["post_run_registrations"] = Used;
			GateList.push_back(Entry);
			UE_LOG(LogPhysicsBench, Display, "%s", Report.Summary().c_str());
		}
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule(Spec.Backend, Error);

		FJson Json;
		Json["schema"] = "physicstest.conformance/1";
		Json["gate_suite_version"] = Gates::GateSuiteVersion;
		Json["result"] = Failed == 0 ? "pass" : "fail";
		Json["summary"] = { { "passed", Passed }, { "failed", Failed }, { "skipped", Skipped } };
		Json["post_run_registrations"] = PostRunList;   // what the verdicts rest on beyond the pre-run registrations
		Json["pending_sign_offs"] = PendingSignOffs;     // of those, the ones the lead has not signed (M10 stamp key)
		Json["utc"] = GetUtcTimestamp();
		Json["command_line"] = Spec.CommandLine;
		Json["target"] = { { "backend", Spec.Backend }, { "variant", Files.VariantName }, { "profile", Config.ProfileName }, { "label", Target.GetLabel() } };
		if (Spec.InjectEngineErrorBits != 0)
		{
			Json["injected_engine_error_bits"] = Spec.InjectEngineErrorBits;   // test only (-InjectEngineErrorBits): not an engine report
		}
		if (Module != nullptr)
		{
			Json["backend"] = MakeBackendJson(Module->GetInfo(), Files, Config, Module);
		}
		{
			FJson Build;
			const std::filesystem::path Exe = GetExecutablePath();
			Build["exe"] = StringConv::PathToUtf8(Exe.filename());
			Build["exe_sha256"] = HashFileSha256(Exe);
			Build["preset"] = PT_BUILD_PRESET;
			Build["configuration"] = PT_BUILD_CONFIGURATION;
			Build["flag_profile"] = PT_BUILD_FLAG_PROFILE;
			Build["git_sha"] = PT_GIT_SHA;
			Build["git_dirty"] = PT_GIT_DIRTY != 0;
			Build["git_sha_source"] = PT_GIT_SHA_SOURCE;
			Build["abi_version"] = std::to_string(PT_ABI_VERSION_MAJOR) + "." + std::to_string(PT_ABI_VERSION_MINOR);
			Json["build"] = Build;
		}
		{
			FJson Inputs;
			Inputs["tolerances_file"] = Tolerances.GetSourceName();
			Inputs["tolerances_sha256"] = Tolerances.GetSha256();
			Inputs["tolerances_version"] = Tolerances.GetVersion();
			Inputs["physics_sha256"] = Config.PhysicsSha256();
			Inputs["profile_sha256"] = Files.ProfileSha256;
			Inputs["variant_sha256"] = Files.VariantSha256;
			Json["inputs"] = Inputs;
		}
		{
			// The gate scenes' identities (scene_sha256: name, version, parameters, seed; build-independent).
			FJson Scenes = FJson::object();
			for (const IPhysicsTestScenario* Scenario : FScenarioRegistry::Get().GetAll())
			{
				if (Scenario->GetKind() == EScenarioKind::Gate)
				{
					FScenarioParams Params;
					std::string Ignored;
					Scenario->ResolveParams(FScenarioParams(), Params, Ignored);
					Scenes[Scenario->GetName()] = Scenario->ComputeSceneSha256(Params);
				}
			}
			Json["scene_sha256"] = Scenes;
		}
		Json["gates"] = GateList;
		// M10 (02 12.4, 03 M10 task 4): the stamp - the whole suite only (no -Gates= selection) and no test-only injection.
		{
			FJson StampJson;
			const bool bFullSuite = Spec.Gates.empty();
			const bool bInjected = Spec.InjectEngineErrorBits != 0 || Spec.bInjectFatal || Spec.InjectForeignWorkMs > 0;
			std::string StampError;
			if (!bFullSuite || bInjected)
			{
				StampJson["written"] = false;
				StampJson["reason"] = !bFullSuite ? "a -Gates= selection is not the suite: no stamp" : "a test-only injection was used: no stamp";
			}
			else if (Module == nullptr)
			{
				StampJson["written"] = false;
				StampJson["reason"] = "the backend module is not loaded: " + Error;
			}
			else
			{
				FStampDocument Document;
				Document.Key.BenchExeSha256 = HashFileSha256(GetExecutablePath());
				Document.Key.BackendModuleSha256 = Module->IsDll() ? HashFileSha256(Module->GetDllPath()) : std::string();
				Document.Key.VariantSha256 = Files.VariantSha256;
				Document.Key.PhysicsSha256 = Config.PhysicsSha256();
				Document.Key.TolerancesSha256 = Tolerances.GetSha256();
				Document.Key.GateSuiteVersion = Gates::GateSuiteVersion;
				Document.Key.ConfigOverrides = ExtractConfigOverrides(Spec.CommandLine);
				Document.Backend = Spec.Backend;
				Document.VariantRequested = Spec.BackendProfile;
				Document.Variant = Files.VariantName;
				Document.Profile = Config.ProfileName;
				Document.Preset = PT_BUILD_PRESET;
				Document.FlagProfile = PT_BUILD_FLAG_PROFILE;
				Document.Configuration = PT_BUILD_CONFIGURATION;
				Document.GitSha = PT_GIT_SHA;
				Document.bGitDirty = PT_GIT_DIRTY != 0;
				Document.ExePath = GetExecutablePath();
				Document.ModuleKind = Module->IsDll() ? "dll" : "static";
				Document.ModulePath = Module->IsDll() ? Module->GetDllPath() : std::filesystem::path();
				Document.ConfigDir = ConfigDir;
				Document.Inputs.push_back({ "base_ini", ConfigDir / "BasePhysicsTest.ini" });
				Document.Inputs.push_back({ "profile_ini", Files.ProfilePath });
				if (!Files.VariantPath.empty())
				{
					Document.Inputs.push_back({ "variant_ini", Files.VariantPath });
				}
				Document.Inputs.push_back({ "tolerances", Gates::GetTolerancesPath(ConfigDir) });
				for (const FGateReport& Report : Reports)
				{
					FStampGate Gate;
					Gate.Id = Report.Gate;
					Gate.Result = Report.Result;
					Gate.Failures = Report.Failures;
					Gate.Reason = Report.Result == EGateResult::Fail ? (Report.Failures.empty() ? std::string("failed") : Report.Failures.front())
						: (Report.Result == EGateResult::Skip ? (Report.Notes.empty() ? std::string("skipped") : Report.Notes.back()) : std::string());
					Document.Gates.push_back(Gate);
				}
				Document.PendingSignOffs = PendingSignOffs;
				Document.ConformanceDir = OutDir;
				Document.CommandLine = Spec.CommandLine;
				const std::filesystem::path StampDir = Spec.StampDir.empty() ? GetDefaultStampDir() : std::filesystem::path(Spec.StampDir).make_preferred();
				std::filesystem::path StampPath;
				const bool bWritten = WriteConformanceStamp(Document, StampDir, StampPath, StampError);
				StampJson["written"] = bWritten;
				StampJson["reason"] = bWritten ? std::string() : StampError;
				StampJson["key_sha256"] = Document.Key.ComputeSha256();
				StampJson["key"] = FJson::parse(Document.Key.ToCanonicalJson());
				StampJson["path"] = bWritten ? FJson(StringConv::PathToUtf8(StampPath)) : FJson(nullptr);
				StampJson["valid"] = bWritten && Failed == 0 && PendingSignOffs == 0;
				UE_LOG(LogPhysicsBench, Display, "conformance stamp %s: %s -> %s", Document.Key.ComputeSha256().substr(0, 16).c_str(),
					Failed == 0 && PendingSignOffs == 0 ? "valid" : "not valid", bWritten ? StringConv::PathToUtf8(StampPath).c_str() : StampError.c_str());
			}
			Json["stamp"] = StampJson;
		}
		Json["wall_time_s"] = FClockBuckets::TicksToNs(FClockBuckets::End(RunBegin)) * 1.0e-9;
		const std::filesystem::path Path = OutDir / "conformance.json";
		if (!WriteText(Path, Json.dump(2) + "\n"))
		{
			UE_LOG(LogPhysicsBench, Error, "cannot write %s", StringConv::PathToUtf8(Path).c_str());
			return Finish(EBenchExit::GateFailed, "gate_failed", "gate", "cannot write conformance.json", {});
		}
		UE_LOG(LogPhysicsBench, Display, "conformance %s: %d pass, %d fail, %d skip -> %s", Target.GetLabel().c_str(), Passed, Failed, Skipped,
			StringConv::PathToUtf8(Path).c_str());
		FPhysicsBackendRegistry::Get().TermIdleModules();
		return Failed == 0 ? Finish(EBenchExit::Ok, "ok", "", "", { "conformance.json" })
			: Finish(EBenchExit::GateFailed, "gate_failed", "gate", std::to_string(Failed) + " gate(s) failed", { "conformance.json" });
	}

	// ---- -SceneHashes (M6) -------------------------------------------------------------------------------------------
	EBenchExit FBenchmarkRunner::WriteSceneHashes(const FBenchRunSpec& Spec)
	{
		// The golden hashes (02 14.1 "scene hash golden"): scene_sha256 of every gate scene, and desc_sha256 per physics profile
		// for this build configuration (it depends on /O2 vs /Od, docs/Scenes.md 1), with the scenario's default frames. Built
		// on Null: desc_sha256 is engine independent (DL43). Entries of the other configuration in an existing file are kept.
		FJson Root = FJson::object();
		{
			std::ifstream File(Spec.SceneHashesPath, std::ios::binary);
			if (File)
			{
				std::stringstream Buffer;
				Buffer << File.rdbuf();
				FJson Existing = FJson::parse(Buffer.str(), nullptr, false);
				if (!Existing.is_discarded() && Existing.is_object())
				{
					Root = std::move(Existing);
				}
			}
		}
		Root["schema"] = "physicstest.scene_hashes/1";
		Root["description"] = "Golden scene hashes of the M6 gate scenes (docs/Scenes.md). scene_sha256 is build-independent; desc_sha256 is per build "
			"configuration (Release = dev / test, Debug = dbg) and physics profile, with the scenario's default frames. Regenerate with "
			"PhysicsTestBench -SceneHashes=Config/Golden/scene_hashes.json in each configuration; PhysicsTest.Scenario.SceneHashesGolden checks it.";
		FJson& Scenes = Root["scenes"];
		const std::filesystem::path ConfigDir = Spec.ConfigDir.empty() ? GetDefaultConfigDir() : std::filesystem::path(Spec.ConfigDir).make_preferred();
		std::vector<FSceneHashEntry> Entries;
		std::string Error;
		if (!ComputeGateSceneHashes(ConfigDir, Entries, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::GateFailed;
		}
		for (const FSceneHashEntry& Hashes : Entries)
		{
			FJson& Entry = Scenes[Hashes.Scene];
			Entry["version"] = Hashes.Version;
			Entry["scene_sha256"] = Hashes.SceneSha256;
			Entry["default_frames"] = Hashes.DefaultFrames;
			for (const auto& [Profile, Sha] : Hashes.DescSha256ByProfile)
			{
				Entry["desc_sha256"][PT_BUILD_CONFIGURATION][Profile] = Sha;
			}
		}
		std::error_code DirError;
		std::filesystem::create_directories(std::filesystem::path(Spec.SceneHashesPath).parent_path(), DirError);
		if (!WriteText(Spec.SceneHashesPath, Root.dump(2) + "\n"))
		{
			UE_LOG(LogPhysicsBench, Error, "cannot write %s", StringConv::PathToUtf8(Spec.SceneHashesPath).c_str());
			return EBenchExit::GateFailed;
		}
		std::printf("%s\n", Root.dump(2).c_str());
		FPhysicsBackendRegistry::Get().TermIdleModules();
		return EBenchExit::Ok;
	}

	bool ComputeGateSceneHashes(const std::filesystem::path& ConfigDir, std::vector<FSceneHashEntry>& Out, std::string& OutError)
	{
		Out.clear();
		for (IPhysicsTestScenario* Scenario : FScenarioRegistry::Get().GetAll())
		{
			if (Scenario->GetKind() != EScenarioKind::Gate)
			{
				continue;
			}
			FScenarioParams Params;
			std::string Ignored;
			Scenario->ResolveParams(FScenarioParams(), Params, Ignored);
			FSceneHashEntry Entry;
			Entry.Scene = Scenario->GetName();
			Entry.Version = Scenario->GetVersion();
			Entry.SceneSha256 = Scenario->ComputeSceneSha256(Params);
			Entry.DefaultFrames = Scenario->GetDefaultFrames(Params);
			for (const char* Profile : { "UEParity", "Normalized" })
			{
				FBenchRunSpec ProfileSpec;
				ProfileSpec.Backend = "Null";
				ProfileSpec.PhysicsProfile = Profile;
				ProfileSpec.ConfigDir = ConfigDir;
				FResolvedPhysicsConfig Config;
				FBenchConfig Files;
				if (!LoadBenchConfig(ProfileSpec, Config, Files, OutError))
				{
					return false;
				}
				if (Scenario->RequiresSleeping() && !Config.TestSettings.bAllowSleeping)
				{
					continue;
				}
				const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
				FPhysSceneInitParams InitParams;
				InitParams.DebugName = Scenario->GetName();
				InitParams.BackendName = "Null";
				InitParams.Config = &Config;
				InitParams.MaxBodiesHint = Capacity.MaxBodies;
				InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
				InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
				InitParams.bApplyForcedOverrides = Scenario->AllowsForcedOverrides();   // gate scenes: false (M9)
				FPhysScene Scene(InitParams);
				FRandomStream Random(Scenario->GetSeed(Params));
				const FScenarioContext Context{ Params, Random, &Config };
				Scenario->BuildScene(Scene, Context);
				const std::vector<float> FrameDts(static_cast<size_t>(Entry.DefaultFrames), Config.TestSettings.FixedDeltaTime);
				Entry.DescSha256ByProfile.emplace_back(Profile, Scene.ComputeDescSha256(FrameDts));
			}
			Out.push_back(std::move(Entry));
		}
		return true;
	}
}
