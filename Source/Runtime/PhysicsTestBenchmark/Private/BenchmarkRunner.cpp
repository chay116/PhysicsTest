// BenchmarkRunner.cpp - FBenchmarkRunner and FTimingRecorder (see BenchmarkRunner.h). The run order is 02_Architecture
// 12.2 without the M7 / M10 steps: parse -> config -> backend init -> engine-option preflight -> scene (BuildScene +
// flush, desc_sha256) -> effective settings + required-key preflight -> PrepareForSimulation -> frame 0 -> timed loop ->
// untimed reads -> outputs. A failed preflight skips everything after it but still writes manifest.json
// (status preflight_failed, exit code 2).
#include "BenchmarkRunner.h"

#include "BenchCore.h"
#include "BuildFlagsSha.h"
#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsBench, Log, All);

namespace PhysicsTest
{
	namespace
	{
		using FJson = nlohmann::ordered_json;

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

		const char* GetUnofficialLabel()
		{
			return "UNOFFICIAL (M3 minimal bench): no measurement protocol (M7: affinity, priority, timer resolution, pre-roll), the power "
				"plan is only read and recorded (not required to be High performance), no conformance stamp (M10). Not for reports.";
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

		// M7 / M10 manifest fields of 02 12.3 that the M3 minimal bench does not record (docs/Progress.md M3 §3).
		FJson MakeDeferredFieldsJson()
		{
			FJson Deferred;
			Deferred["order"] = "M7 (repeat_index, order_index, block_seed: the runner's order)";
			Deferred["platform.p_cores"] = "M7";
			Deferred["platform.priority_set"] = "M7 (the priority class is only read)";
			Deferred["platform.throttling"] = "M7 (power throttling opt-out)";
			Deferred["platform.timer_resolution"] = "M7 (timeBeginPeriod)";
			Deferred["platform.cpu_mhz"] = "M7";
			Deferred["platform.system_busy_other_pct"] = "M7";
			Deferred["platform.tsc_hz"] = "M10";
			Deferred["platform.timer_read_overhead_ns"] = "M10";
			Deferred["threads.census_init"] = "M10";
			Deferred["threads.census_window"] = "M10";
			Deferred["threads.foreign_cycles_fraction"] = "M10";
			Deferred["comparability_key"] = "M10";
			Deferred["conformance_stamp"] = "M10";
			Deferred["gates"] = "M6 / M7";
			Deferred["determinism.h_quantized"] = "M7";
			Deferred["summary.cpu_eff"] = "M10";
			return Deferred;
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

		FJson MakeBackendJson(const FBackendInfo& Info, const FBenchConfig& Files, const FResolvedPhysicsConfig& Config)
		{
			FJson Json;
			Json["name"] = Info.Name;
			Json["display_name"] = Info.DisplayName;
			Json["variant"] = Files.VariantName;
			Json["label"] = !Files.VariantLabel.empty() ? Files.VariantLabel : (Config.Engine.empty() ? Info.DisplayName : Config.Engine + "." + Config.VariantName);
			Json["sdk_version"] = Info.EngineVersion;
			Json["sdk_sha"] = Info.SourceCommit;
			Json["build_config"] = Info.BuildConfig;
			Json["abi_version"] = std::to_string(PT_ABI_VERSION_MAJOR) + "." + std::to_string(PT_ABI_VERSION_MINOR);
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

		bool WriteText(const std::filesystem::path& Path, const std::string& Text)
		{
			std::ofstream File(Path, std::ios::binary | std::ios::trunc);
			File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
			File.flush();
			return static_cast<bool>(File);
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
	}

	// ---- FTimingRecorder ---------------------------------------------------------------------------------------------
	bool FTimingRecorder::WriteCsv(const std::filesystem::path& Path) const
	{
		std::string Text = "frame,sim_time_s,num_steps,push_ns,setup_ns,start_ns,wait_ns,end_ns,step_ns,pull_ns,frame_ns,frame_qpc_ticks,solver_ns,awake_bodies,"
			"contact_pairs,contact_points,islands,error_bits\n";
		Text.reserve(Text.size() + Records.size() * 128);
		char Line[512];
		for (size_t Frame = 0; Frame < Records.size(); ++Frame)
		{
			const FFrameRecord& R = Records[Frame];
			const FFrameTiming& T = R.Timing;
			std::snprintf(Line, sizeof(Line), "%zu,%.17g,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
				",%" PRIu64 ",,%d,-1,-1,-1,%u\n", Frame, R.SimTime, T.NumSteps, T.PushNs, T.SetupNs, T.StartNs, T.WaitNs, T.EndNs, T.StepNs, T.PullNs,
				T.FrameNs, T.FrameQpcTicks, R.AwakeBodies, T.EngineErrorBits);
			Text += Line;
		}
		return WriteText(Path, Text);
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
		Json["backend"] = MakeBackendJson(Scene.GetBackendInfo(), Files, Config);
		Json["profile"] = Config.ProfileName;
		Json["scenario"] = Scenario != nullptr ? Scenario->GetName() : "";
		Json["required_effective_keys"] = GetRequiredEffectiveKeys(Scene.GetBackendInfo());
		Json["missing_effective_keys"] = Missing;
		Json["effective"] = ParseEffectiveToJson(Effective);
		Json["effective_module"] = ParseEffectiveToJson(DumpModuleEffectiveSettings(Scene.GetBackendScene().GetModule()));
		std::printf("%s\n", Json.dump(2).c_str());
		return Missing.empty() ? EBenchExit::Ok : EBenchExit::GateFailed;
	}

	EBenchExit FBenchmarkRunner::Run(const FBenchRunSpec& Spec)
	{
		const uint64 RunBegin = FClockBuckets::Begin();
		// ---- 1. the spec ------------------------------------------------------------------------------------------------
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find(Spec.Scenario);
		if (Scenario == nullptr)
		{
			UE_LOG(LogPhysicsBench, Error, "unknown scenario '%s' (see -List)", Spec.Scenario.c_str());
			return EBenchExit::GateFailed;
		}
		FScenarioParams Params;
		std::string Error;
		if (!Scenario->ResolveParams(Spec.Params, Params, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::GateFailed;
		}
		const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
		if (std::find(Available.begin(), Available.end(), Spec.Backend) == Available.end())
		{
			UE_LOG(LogPhysicsBench, Error, "backend '%s' is not available in this build (available: %zu backends, see -List)", Spec.Backend.c_str(), Available.size());
			return EBenchExit::BackendUnavailable;
		}

		// ---- 3. config --------------------------------------------------------------------------------------------------
		FResolvedPhysicsConfig Config;
		FBenchConfig Files;
		if (!LoadBenchConfig(Spec, Config, Files, Error))
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::GateFailed;
		}
		// The M3 runner hashes the final frame only and writes no per-frame counters: a profile asking for more would get
		// a manifest claiming work that never happened. Per-frame hashes and counters are the M7 recorder.
		if (Config.TestSettings.HashEvery != 0 || Config.TestSettings.CountersEvery != 0)
		{
			UE_LOG(LogPhysicsBench, Error, "HashEvery=%d / CountersEvery=%d: per-frame hashes and counters need the M7 recorder; the M3 bench hashes the "
				"final frame only and writes no per-frame counters (set both to 0)", Config.TestSettings.HashEvery, Config.TestSettings.CountersEvery);
			return EBenchExit::GateFailed;
		}
		// Headline eligibility (M4, M3 review): the bench drives FixedStep frames at the nominal dt; a headline run must also
		// be a bench-mode run without instrumentation (02 9.3, 9.4: no listener, no counters). Any other combination still
		// runs and writes its outputs, but the manifest marks it not headline-eligible with the reasons (M7's analysis drops
		// it); every M3 / M4 timing is unofficial anyway.
		std::vector<std::string> HeadlineIneligibleReasons;
		if (Config.TestSettings.TickMode != EPhysicsTickMode::FixedStep)
		{
			HeadlineIneligibleReasons.push_back(std::string("tick_mode ") + ToString(Config.TestSettings.TickMode) + ": headline runs are FixedStep");
		}
		if (Config.TestSettings.RunMode != EPhysicsRunMode::Bench)
		{
			HeadlineIneligibleReasons.push_back(std::string("run mode ") + ToString(Config.TestSettings.RunMode) + ": headline runs are RunMode=Bench");
		}
		if (Config.TestSettings.InstrumentFlags != 0)
		{
			HeadlineIneligibleReasons.push_back("instrumentation flags " + std::to_string(Config.TestSettings.InstrumentFlags) + ": headline runs have none");
		}
		for (const std::string& Reason : HeadlineIneligibleReasons)
		{
			UE_LOG(LogPhysicsBench, Warning, "not headline-eligible: %s", Reason.c_str());
		}
		const int32 Frames = Spec.Frames > 0 ? Spec.Frames : Scenario->GetDefaultFrames(Params);
		const int32 Seed = Scenario->GetSeed(Params);
		const std::string RunId = MakeRunId();
		std::filesystem::path OutDir = !Spec.OutDir.empty() ? Spec.OutDir : GetDefaultResultsDir() / StringConv::Utf8ToPath(Spec.Session) / RunId;
		OutDir.make_preferred();
		std::error_code DirError;
		std::filesystem::create_directories(OutDir, DirError);
		if (DirError)
		{
			UE_LOG(LogPhysicsBench, Error, "cannot create the run directory '%s': %s", StringConv::PathToUtf8(OutDir).c_str(), DirError.message().c_str());
			return EBenchExit::GateFailed;
		}
		const FHostFacts Host = ReadHostFacts();

		// ---- 4. backend init (timed separately; the scene below reuses the initialised module) --------------------------
		FBackendInitParams BackendInit;
		BackendInit.MaxThreads = static_cast<uint32>(Config.TestSettings.NumThreads > 0 ? Config.TestSettings.NumThreads : 1);
		BackendInit.InitOptions = Config.InitOptions;
		uint64 PhaseBegin = FClockBuckets::Begin();
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoad(Spec.Backend, BackendInit, Error);
		const uint64 BackendInitTicks = FClockBuckets::End(PhaseBegin);
		if (Module == nullptr)
		{
			UE_LOG(LogPhysicsBench, Error, "%s", Error.c_str());
			return EBenchExit::BackendUnavailable;
		}

		// The manifest fields every outcome writes (a preflight failure stops after these, 02 12.2).
		const auto MakeManifestHead = [&](const std::string& Status, const EBenchExit Exit, const FBackendInfo& Info)
		{
			FJson Manifest;
			Manifest["schema"] = "physicstest.run/1";
			Manifest["status"] = Status;
			Manifest["exit_code"] = static_cast<int32>(Exit);
			Manifest["timing_valid_for_headline"] = false;
			Manifest["unofficial"] = true;
			Manifest["timing_label"] = GetUnofficialLabel();
			Manifest["run_id"] = RunId;
			Manifest["session"] = Spec.Session;
			Manifest["utc"] = GetUtcTimestamp();
			Manifest["command_line"] = Spec.CommandLine;
			{
				FJson Harness;
				Harness["git_sha"] = PT_BUILD_GIT_SHA;
				Harness["dirty"] = PT_BUILD_GIT_DIRTY != 0;
				// Configure-time values: stale until the next CMake configure (M7 moves them to build time).
				Harness["git_sha_source"] = "cmake_configure";
				Harness["git_note"] = "recorded at CMake configure time: a commit or edit after the last configure is not reflected (M7: build time)";
				const std::filesystem::path Exe = GetExecutablePath();
				Harness["exe"] = StringConv::PathToUtf8(Exe.filename());
				Harness["exe_sha256"] = HashFileSha256(Exe);
				Harness["preset"] = PT_BUILD_PRESET;
				Manifest["harness"] = Harness;
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
				Manifest["build"] = Build;
			}
			Manifest["backend"] = MakeBackendJson(Info, Files, Config);
			{
				FJson Profile;
				Profile["physics"] = Config.ProfileName;
				Profile["profile_file"] = StringConv::PathToUtf8(Files.ProfilePath);
				Profile["variant_file"] = StringConv::PathToUtf8(Files.VariantPath);
				Profile["mass_rule"] = ToString(Config.TestSettings.MassRule);
				Profile["physics_sha256"] = Config.PhysicsSha256();
				Profile["profile_sha256"] = Files.ProfileSha256;
				Profile["variant_sha256"] = Files.VariantSha256;
				Profile["warnings"] = Config.Warnings;
				Profile["resolved"] = MakeResolvedJson(Config);
				Manifest["profile"] = Profile;
			}
			return Manifest;
		};

		// ---- 5. engine-option preflight -----------------------------------------------------------------------------------
		// The variant's [PhysicsTest.Backend.<Engine>] keys are validated by the backend's CreateScene. A probe scene with
		// exactly those keys (no bodies, untimed) turns a configuration error into exit 2 with a manifest; FPhysScene would
		// stop the process through Fatal (exit 3, no output). (M3 fix round; M7 adds the other preflight gates.)
		PhaseBegin = FClockBuckets::Begin();
		{
			std::vector<PtKeyValue> ProbeOptions;
			for (const auto& [Key, Value] : Config.SceneOptions)
			{
				ProbeOptions.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
			}
			PtSceneDesc Probe = {};
			Probe.StructSize = sizeof(PtSceneDesc);
			Probe.RunMode = static_cast<uint32>(Config.TestSettings.RunMode);
			Probe.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
			Probe.NominalDeltaTime = Config.TestSettings.FixedDeltaTime > 0.0f ? static_cast<double>(Config.TestSettings.FixedDeltaTime) : 1.0 / 60.0;
			Probe.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
			Probe.RestitutionThreshold = -1.0;
			Probe.NumThreads = 1;
			Probe.DebugName = "OptionPreflight";
			Probe.Options = ProbeOptions.empty() ? nullptr : ProbeOptions.data();
			Probe.NumOptions = static_cast<uint32>(ProbeOptions.size());
			std::string ProbeError;
			if (!FPhysicsBackendScene::Create(*Module, Probe, ProbeError))
			{
				UE_LOG(LogPhysicsBench, Error, "preflight: the backend rejects the variant's engine options: %s", ProbeError.c_str());
				FJson Manifest = MakeManifestHead("preflight_failed", EBenchExit::GateFailed, Module->GetInfo());
				Manifest["preflight_error"] = "engine options: " + ProbeError;
				Manifest["scene"] = { { "name", Scenario->GetName() }, { "version", Scenario->GetVersion() }, { "params", Params }, { "seed", Seed },
					{ "scene_sha256", Scenario->ComputeSceneSha256(Params) } };
				Manifest["outputs"] = { "manifest.json" };
				Manifest["wall_time_s"] = FClockBuckets::TicksToNs(FClockBuckets::End(RunBegin)) * 1.0e-9;
				WriteText(OutDir / "manifest.json", Manifest.dump(2) + "\n");
				Module->TermGamePhys();
				return EBenchExit::GateFailed;
			}
		}
		const uint64 OptionPreflightTicks = FClockBuckets::End(PhaseBegin);

		// ---- 6. scene: create, BuildScene + flush, hashes ---------------------------------------------------------------
		const FScenarioCapacity Capacity = Scenario->GetCapacity(Params);
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = Scenario->GetName();
		InitParams.BackendName = Spec.Backend;
		InitParams.Config = &Config;
		InitParams.MaxBodiesHint = Capacity.MaxBodies;
		InitParams.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitParams.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		PhaseBegin = FClockBuckets::Begin();
		std::unique_ptr<FPhysScene> Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Seed);
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		Scene->FlushDeferredActors();   // a scenario normally flushed already
		const uint64 SceneCreateTicks = FClockBuckets::End(PhaseBegin);
		// The bench drives FixedStep frames at the nominal dt (other tick modes plan from it the same way).
		const float FrameDeltaSeconds = Config.TestSettings.FixedDeltaTime;
		const std::vector<float> FrameDts(static_cast<size_t>(Frames), FrameDeltaSeconds);
		const std::string SceneSha256 = Scenario->ComputeSceneSha256(Params);
		const std::string DescSha256 = Scene->ComputeDescSha256(FrameDts);
		const std::string OptionsSha256 = Scene->ComputeOptionsSha256();

		// ---- 7. effective settings + the required-key preflight (A7 at run time) ----------------------------------------
		const std::string Effective = Scene->DumpEffectiveSettings();
		const std::string ModuleEffective = DumpModuleEffectiveSettings(*Module);
		WriteText(OutDir / "effective_settings.jsonl", Effective);
		std::vector<FEffectiveSetting> Settings;
		std::vector<std::string> MissingKeys;
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
		const bool bPreflightPassed = MissingKeys.empty();
		const FJson BodiesSampled = SampleMassProperties(*Scene);

		// ---- 8. prepare, 10. frame 0 and the timed loop (only after a passed preflight, 02 12.2) -------------------------
		uint64 PrepareTicks = 0;
		uint64 Frame0Ticks = 0;
		uint64 LoopTicks = 0;
		FTimingRecorder Recorder;
		if (bPreflightPassed)
		{
			PhaseBegin = FClockBuckets::Begin();
			Scene->PrepareForSimulation();
			PrepareTicks = FClockBuckets::End(PhaseBegin);
			Scene->OnPhysScenePreTick = [Scenario](FPhysScene* InScene, const float DeltaSeconds) { Scenario->OnPreTick(*InScene, DeltaSeconds); };
			Recorder.Reserve(Frames);
			PhaseBegin = FClockBuckets::Begin();
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				// Inside the loop: no allocation, no I/O, no log (02 9.3). Hashes and counters only after the last frame (DL22).
				Scene->Tick(FrameDeltaSeconds);
				Recorder.Record(Scene->GetLastFrameTiming(), Scene->GetSimTime(), Scene->GetStats().NumAwakeBodies);
				if (Frame == 0)
				{
					Frame0Ticks = FClockBuckets::End(PhaseBegin);
					PhaseBegin = FClockBuckets::Begin();
				}
			}
			LoopTicks = FClockBuckets::End(PhaseBegin);
		}
		else
		{
			UE_LOG(LogPhysicsBench, Error, "preflight: %zu required effective key(s) missing; Prepare and the frame loop are skipped", MissingKeys.size());
		}
		const int32 FramesRun = static_cast<int32>(Recorder.GetRecords().size());

		// ---- untimed reads after the last frame -------------------------------------------------------------------------
		const bool bNativeHash = bPreflightPassed && Scene->GetBackendInfo().Has(PT_CAP_NATIVE_HASH);
		const uint64 FinalHash = bNativeHash ? Scene->ComputeNativeStateHash() : 0;
		PtStepStats StepStats = {};
		Scene->GetBackendScene().GetStepStats(StepStats);
		uint32 ErrorBits = StepStats.EngineErrorBits;
		for (const FFrameRecord& Record : Recorder.GetRecords())
		{
			ErrorBits |= Record.Timing.EngineErrorBits;
		}
		if (bPreflightPassed)
		{
			ErrorBits |= Spec.InjectEngineErrorBits;   // test only (-InjectEngineErrorBits): the manifest records it
		}
		const uint32 CapacityBitsDeclared = GetCapacityErrorBits(Settings);
		const uint32 CapacityErrorBits = ErrorBits & CapacityBitsDeclared;
		const uint32 OtherErrorBits = ErrorBits & ~CapacityBitsDeclared;
		PtMemoryStats BackendMemory = {};
		const PtResult BackendMemoryResult = Scene->GetBackendScene().GetMemoryStats(BackendMemory);
		const FProcessMemory ProcessMemory = ReadProcessMemoryCounters();
		std::vector<std::string> Outputs = { "timings.csv", "manifest.json", "effective_settings.jsonl", "scene.json" };
		if (bPreflightPassed)
		{
			WriteText(OutDir / "effective_settings_after.jsonl", Scene->DumpEffectiveSettings());
			Outputs.push_back("effective_settings_after.jsonl");
		}

		// ---- exit code ------------------------------------------------------------------------------------------------
		EBenchExit Exit = EBenchExit::Ok;
		std::string Status = "ok";
		if (!bPreflightPassed)
		{
			Exit = EBenchExit::GateFailed;
			Status = "preflight_failed";
		}
		else if (OtherErrorBits != 0)
		{
			// An engine error wins over a capacity overflow in the same run: a capacity retry (M8) would not cure it. Both
			// bit groups are in the manifest (determinism.capacity_error_bits / other_error_bits).
			Exit = EBenchExit::EngineError;
			Status = "engine_error";
		}
		else if (CapacityErrorBits != 0)
		{
			Exit = EBenchExit::Capacity;
			Status = "capacity_exceeded";
		}

		// ---- 12. outputs -------------------------------------------------------------------------------------------------
		Recorder.WriteCsv(OutDir / "timings.csv");
		WriteText(OutDir / "scene.json", Scenario->MakeCanonicalJson(Params) + "\n");
		const FPhysicsTestSettings& Test = Config.TestSettings;
		const FStepPlan& LastPlan = Scene->GetLastPlan();
		FJson Manifest = MakeManifestHead(Status, Exit, Scene->GetBackendInfo());
		if (!bPreflightPassed)
		{
			Manifest["preflight_error"] = "missing required effective keys";
		}
		Manifest["effective"] = ParseEffectiveToJson(Effective);
		Manifest["effective_module"] = ParseEffectiveToJson(ModuleEffective);
		Manifest["missing_effective_keys"] = MissingKeys;
		Manifest["bodies_sampled"] = BodiesSampled;
		{
			FJson SceneJson;
			SceneJson["name"] = Scenario->GetName();
			SceneJson["version"] = Scenario->GetVersion();
			SceneJson["params"] = Params;
			SceneJson["seed"] = Seed;
			SceneJson["scene_sha256"] = SceneSha256;
			SceneJson["desc_sha256"] = DescSha256;
			// desc_sha256 covers harness-computed doubles (mass properties, FRotator -> FQuat) built with /fp:fast: it is
			// comparable within one optimisation level (dev = test), not between dbg (/Od) and the others (Scenes.md).
			SceneJson["desc_sha256_scope"] = std::string("engines and variants of one build configuration (") + PT_BUILD_CONFIGURATION + ")";
			SceneJson["options_sha256"] = OptionsSha256;
			SceneJson["num_bodies"] = Scene->GetStats().NumBodies;
			SceneJson["num_dynamic_bodies"] = Scene->GetStats().NumDynamicBodies;
			SceneJson["start_asleep_skipped"] = Scene->GetStats().NumStartAsleepSkipped;
			SceneJson["capacity_hints"] = { Capacity.MaxBodies, Capacity.MaxBodyPairs, Capacity.MaxContactConstraints };
			FJson Windows = FJson::array();
			for (const FStepWindow& Window : Scenario->GetWindows(Params))
			{
				Windows.push_back({ { "name", Window.Name }, { "first", Window.First }, { "last", Window.Last } });
			}
			SceneJson["windows"] = Windows;
			Manifest["scene"] = SceneJson;
		}
		{
			FJson Run;
			Run["mode"] = ToString(Test.RunMode);
			Run["headline_eligible"] = HeadlineIneligibleReasons.empty();
			Run["headline_ineligible_reasons"] = HeadlineIneligibleReasons;
			Run["instrumentation"] = Test.InstrumentFlags;
			Run["frames"] = Frames;
			Run["frames_run"] = FramesRun;
			Run["dt_s"] = static_cast<double>(FrameDeltaSeconds);
			Run["dt_engine_s"] = static_cast<double>(LastPlan.StepDeltaTime);
			Run["tick_mode"] = ToString(Test.TickMode);
			Run["clamp_rule"] = ToString(Test.SubstepClampRule);
			Run["frame_driver"] = ToString(Scene->GetFrameDriver());
			// How the host syncs with the engine (02 9.1, 9.3): commands pushed once per engine step (Harness driver) or
			// once per frame (Native driver), states pulled once per frame with PT_READ_ACTIVE_ONLY.
			Run["sync_policy"] = Scene->GetFrameDriver() == EFrameDriver::Native ? "push_per_frame+pull_active_only_per_frame" : "push_per_step+pull_active_only_per_frame";
			// Always 0 here (a non-zero value is rejected above): the final-frame hash and no per-frame counters.
			Run["hash_every"] = Test.HashEvery;
			Run["counters_every"] = Test.CountersEvery;
			if (Spec.InjectEngineErrorBits != 0)
			{
				Run["injected_engine_error_bits"] = Spec.InjectEngineErrorBits;   // test only: not an engine report
			}
			Run["async_interpolated"] = LastPlan.bAsyncInterpolated;
			Run["steps_total"] = Scene->GetStepCount();
			Run["sim_time_s"] = Scene->GetSimTime();
			Manifest["run"] = Run;
		}
		{
			FJson Threads;
			Threads["T"] = Test.NumThreads;
			// The engine's own report of its worker threads (the threads.workers READBACK key), next to the requested T.
			const auto Workers = std::find_if(Settings.begin(), Settings.end(), [](const FEffectiveSetting& S) { return S.Key == "threads.workers"; });
			if (Workers != Settings.end())
			{
				Threads["engine_native"] = { { "workers", Workers->bHasCanonical ? FJson(Workers->Canonical) : FJson(nullptr) }, { "native", Workers->Native },
					{ "source", Workers->Source } };
			}
			else
			{
				Threads["engine_native"] = nullptr;
			}
			Threads["affinity_mask"] = ToHex64(Host.ProcessAffinityMask);
			Threads["affinity_applied"] = false;
			Threads["priority_class"] = Host.PriorityClass;
			Manifest["threads"] = Threads;
		}
		{
			FJson Platform;
			Platform["cpu"] = Host.CpuBrand;
			Platform["logical_processors"] = Host.LogicalProcessors;
			Platform["os_build"] = Host.OsBuild;
			Platform["power_scheme"] = Host.PowerSchemeName;
			Platform["power_scheme_guid"] = Host.PowerSchemeGuid;
			Platform["qpc_hz"] = Host.QpcHz;
			Manifest["platform"] = Platform;
		}
		{
			FJson Phases;
			Phases["backend_init"] = FClockBuckets::TicksToNs(BackendInitTicks);
			Phases["option_preflight"] = FClockBuckets::TicksToNs(OptionPreflightTicks);
			Phases["scene_create"] = FClockBuckets::TicksToNs(SceneCreateTicks);
			Phases["prepare"] = FClockBuckets::TicksToNs(PrepareTicks);
			Phases["frame0"] = FClockBuckets::TicksToNs(Frame0Ticks);
			Phases["loop_after_frame0"] = FClockBuckets::TicksToNs(LoopTicks);
			Phases["teardown"] = nullptr;   // after the outputs are written (exit.json and the teardown record: M7)
			Phases["note"] = "wall time of each phase (QPC); frame0 and the loop include the untimed per-frame host work";
			Manifest["phases_ns"] = Phases;
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
			if (BackendMemoryResult == PT_OK)
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
			Manifest["memory"] = Memory;
		}
		{
			// Window statistics of the per-frame buckets; frame 0 is reported apart (02 9.5).
			const std::vector<FFrameRecord>& Records = Recorder.GetRecords();
			FJson Summary;
			Summary["unit"] = "ns";
			Summary["frame0_frame_ns"] = Records.empty() ? 0.0 : static_cast<double>(Records[0].Timing.FrameNs);
			FJson WindowsJson;
			for (const FStepWindow& Window : Scenario->GetWindows(Params))
			{
				std::vector<double> FrameNs;
				std::vector<double> StepNs;
				std::vector<double> PushNs;
				std::vector<double> PullNs;
				for (uint32 Frame = Window.First; Frame <= Window.Last && Frame < Records.size(); ++Frame)
				{
					if (Frame == 0)
					{
						continue;
					}
					FrameNs.push_back(static_cast<double>(Records[Frame].Timing.FrameNs));
					StepNs.push_back(static_cast<double>(Records[Frame].Timing.StepNs));
					PushNs.push_back(static_cast<double>(Records[Frame].Timing.PushNs));
					PullNs.push_back(static_cast<double>(Records[Frame].Timing.PullNs));
				}
				FJson Entry;
				Entry["first"] = Window.First;
				Entry["last"] = Window.Last;
				Entry["frame_ns"] = MakeStatsJson(ComputeDurationStats(FrameNs));
				Entry["step_ns"] = MakeStatsJson(ComputeDurationStats(StepNs));
				Entry["push_ns"] = MakeStatsJson(ComputeDurationStats(PushNs));
				Entry["pull_ns"] = MakeStatsJson(ComputeDurationStats(PullNs));
				WindowsJson[Window.Name] = Entry;
			}
			Summary["windows"] = WindowsJson;
			Summary["note"] = "T_frame = sum(T_push) + sum(T_step) + T_pull; T_step is not comparable with Chaos (02 9.3)";
			Manifest["summary"] = Summary;
		}
		{
			FJson Determinism;
			Determinism["h_native"] = bNativeHash ? ToHex64(FinalHash) : std::string();
			Determinism["h_native_frame"] = FramesRun;
			Determinism["engine_error_bits"] = ErrorBits;
			Determinism["capacity_error_bits_declared"] = CapacityBitsDeclared;   // the backend's errors.capacity_bits
			Determinism["capacity_error_bits"] = CapacityErrorBits;
			Determinism["other_error_bits"] = OtherErrorBits;
			Manifest["determinism"] = Determinism;
		}
		{
			FJson Stats;
			Stats["active_bodies"] = StepStats.NumActiveBodies;
			Stats["sleeping_bodies"] = StepStats.NumSleepingBodies;
			Stats["contact_pairs"] = StepStats.NumContactPairs;
			Stats["contact_points"] = StepStats.NumContactPoints;
			Stats["islands"] = StepStats.NumIslands;
			Manifest["step_stats_final"] = Stats;
		}
		Manifest["deferred_fields"] = MakeDeferredFieldsJson();
		Manifest["outputs"] = Outputs;
		Manifest["wall_time_s"] = FClockBuckets::TicksToNs(FClockBuckets::End(RunBegin)) * 1.0e-9;
		WriteText(OutDir / "manifest.json", Manifest.dump(2) + "\n");

		const FDurationStats Headline = ComputeDurationStats([&Recorder]
		{
			std::vector<double> Values;
			for (size_t Frame = 1; Frame < Recorder.GetRecords().size(); ++Frame)
			{
				Values.push_back(static_cast<double>(Recorder.GetRecords()[Frame].Timing.FrameNs));
			}
			return Values;
		}());
		UE_LOG(LogPhysicsBench, Display, "%s %s %s/%s: %d frames, T_frame mean %.3f ms median %.3f ms (frames 1..%d, UNOFFICIAL), h_native %s, status %s -> %s",
			Spec.Backend.c_str(), Scenario->GetName(), Config.ProfileName.c_str(), Files.VariantName.empty() ? "-" : Files.VariantName.c_str(), FramesRun,
			Headline.Mean * 1.0e-6, Headline.Median * 1.0e-6, FramesRun - 1, ToHex64(FinalHash).c_str(), Status.c_str(), StringConv::PathToUtf8(OutDir).c_str());

		// ---- 13. teardown (scene, then the module: 02 12.2) -------------------------------------------------------------
		Scene.reset();
		Module->TermGamePhys();
		return Exit;
	}
}
