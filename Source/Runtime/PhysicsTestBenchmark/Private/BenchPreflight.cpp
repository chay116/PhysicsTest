// BenchPreflight.cpp - see BenchPreflight.h.
#include "BenchPreflight.h"

#include "BuildFlagsSha.h"
#include "Gates/PhysicsGates.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"
#include "PTBuildInfo.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace PhysicsTest::BenchPreflight
{
	namespace
	{
		using FJson = nlohmann::json;

		std::string Format(const char* Pattern, ...)
		{
			char Text[1024];
			va_list Args;
			va_start(Args, Pattern);
			std::vsnprintf(Text, sizeof(Text), Pattern, Args);
			va_end(Args);
			return Text;
		}

		bool EndsWith(const std::string& Text, const char* Suffix)
		{
			const size_t Length = std::char_traits<char>::length(Suffix);
			return Text.size() >= Length && Text.compare(Text.size() - Length, Length, Suffix) == 0;
		}

		bool IsDrawerKey(const std::string& Key)
		{
			return EndsWith(Key, ".debug_drawer");
		}

		const FEffectiveSetting* FindSetting(const std::vector<FEffectiveSetting>& Settings, const std::string& Key)
		{
			const auto Found = std::find_if(Settings.begin(), Settings.end(), [&Key](const FEffectiveSetting& S) { return S.Key == Key; });
			return Found != Settings.end() ? &*Found : nullptr;
		}

		// A JSON array of strings as one text ("[AVX2]"), for notes.
		std::string ListText(const FJson& Array)
		{
			std::string Text = "[";
			if (Array.is_array())
			{
				for (size_t Index = 0; Index < Array.size(); ++Index)
				{
					Text += (Index > 0 ? "," : "") + (Array[Index].is_string() ? Array[Index].get<std::string>() : Array[Index].dump());
				}
			}
			return Text + "]";
		}

		// True when Array is exactly [Expected].
		bool IsOnly(const FJson& Array, const std::string& Expected)
		{
			return Array.is_array() && Array.size() == 1 && Array[0].is_string() && Array[0].get<std::string>() == Expected;
		}

		// The registry name's adapter target and SDK group in BuildFlags.json.
		void GetBuildTargets(const std::string& BackendName, std::string& OutTarget, std::string& OutGroup)
		{
			OutGroup.clear();
			if (BackendName == "Null")
			{
				OutTarget = "PtBackendNull";
			}
			else if (BackendName == "NullDll")
			{
				OutTarget = "PtBackend_Null";
			}
			else if (BackendName == "Jolt")
			{
				OutTarget = "PtBackendJolt";
				OutGroup = "sdk.jolt";
			}
			else if (BackendName == "Bullet")
			{
				OutTarget = "PtBackendBullet";
				OutGroup = "sdk.bullet";
			}
			else if (BackendName == "PhysX")
			{
				OutTarget = "PtBackendPhysX";
				OutGroup = "sdk.physx";
			}
			else
			{
				OutTarget.clear();
			}
		}

		// The /fp, CRT and iterator debug level every TU of this configuration must have (PTToolchain.cmake, DumpBuildFlags).
		void CheckCommonFacts(FGateReport& Report, const FJson& Facts, const std::string& What, const bool bDebug)
		{
			Report.Check(IsOnly(Facts.value("fp", FJson::array()), "fast"), What + ": /fp " + ListText(Facts.value("fp", FJson::array())) + " (expected [fast])");
			const std::string Crt = bDebug ? "MDd" : "MD";
			Report.Check(IsOnly(Facts.value("crt", FJson::array()), Crt), What + ": CRT " + ListText(Facts.value("crt", FJson::array())) + " (expected [" + Crt + "])");
			const std::string Idl = bDebug ? "2" : "0";
			Report.Check(IsOnly(Facts.value("idl", FJson::array()), Idl),
				What + ": _ITERATOR_DEBUG_LEVEL " + ListText(Facts.value("idl", FJson::array())) + " (expected [" + Idl + "])");
		}
	}

	FGateReport RunG0(const FBackendInfo& Info, const std::string& BackendName, const std::vector<FEffectiveSetting>& Settings, const std::string& InjectFault)
	{
		FGateReport Report;
		Report.Gate = "G0";
		Report.Target = BackendName;
		FJson Facts = FJson::parse(PT_BUILD_FLAGS_G0_JSON, nullptr, false);   // non-const: operator[] on a missing group yields null
		if (!Report.Check(Facts.is_object(), "PT_BUILD_FLAGS_G0_JSON is not a JSON object (BuildFlagsSha.h)"))
		{
			return Report;
		}
		const std::string Profile = Facts.value("flag_profile", std::string());
		const std::string Configuration = Facts.value("configuration", std::string());
		const bool bDebug = Configuration == "Debug";
		Report.Check(Profile == PT_BUILD_FLAG_PROFILE, "BuildFlags.json flag profile '" + Profile + "' differs from this build's '" PT_BUILD_FLAG_PROFILE "'");
		Report.Check(Configuration == PT_BUILD_CONFIGURATION, "BuildFlags.json configuration '" + Configuration + "' differs from '" PT_BUILD_CONFIGURATION "'");
		Report.Check(Facts.value("violations", -1) == 0, Format("BuildFlags.json lists %d flag-profile violation(s) (CTest PhysicsTest.Probe.BuildFlags)", Facts.value("violations", -1)));
		Report.Note(Format("BuildFlags.json sha256 %s, %d TUs, profile %s, configuration %s", PT_BUILD_FLAGS_SHA256, PT_BUILD_FLAGS_TU_COUNT, Profile.c_str(),
			Configuration.c_str()));

		// The harness: the bench exe's TUs (BuildFlags.json) and this TU (the compiler) have no /arch (02 4.2, DL12).
#if defined(__AVX2__)
		Report.Check(false, "the bench TU was compiled with /arch:AVX2 (harness TUs never are)");
#endif
		if (const auto Bench = Facts["targets"].find("PhysicsTestBench"); Bench != Facts["targets"].end())
		{
			Report.Check(IsOnly(Bench->value("arch", FJson::array()), "none"), "PhysicsTestBench TUs: /arch " + ListText(Bench->value("arch", FJson::array())) + " (expected none)");
			CheckCommonFacts(Report, *Bench, "PhysicsTestBench TUs", bDebug);
		}
		else
		{
			Report.Check(false, "BuildFlags.json has no PhysicsTestBench TU");
		}

		std::string Target;
		std::string Group;
		GetBuildTargets(BackendName, Target, Group);
		if (Target.empty())
		{
			// A DLL built outside this CMake tree (the Chaos bridge: UBT .rsp files, M13): G0 has no compile_commands entry.
			Report.Result = EGateResult::Skip;
			Report.Note("backend '" + BackendName + "' is not built by this tree: G0 needs its build record (Chaos: the UBT .rsp parser, M13)");
			return Report;
		}
		const auto TargetFacts = Facts["targets"].find(Target);
		if (!Report.Check(TargetFacts != Facts["targets"].end(), "BuildFlags.json has no TU of the adapter target " + Target))
		{
			return Report;
		}
		const FJson& Adapter = *TargetFacts;
		const FJson ExpectArray = Adapter.value("expect_arch", FJson::array());
		const std::string ExpectArch = ExpectArray.is_array() && ExpectArray.size() == 1 && ExpectArray[0].is_string() ? ExpectArray[0].get<std::string>() : "?";
		Report.Check(ExpectArray.size() == 1, Target + ": the profile expects one /arch for every TU, got " + ListText(ExpectArray));
		Report.Check(IsOnly(Adapter.value("arch", FJson::array()), ExpectArch), Target + " TUs: /arch " + ListText(Adapter.value("arch", FJson::array())) + " (profile "
			+ Profile + " expects " + ExpectArch + ")");
		CheckCommonFacts(Report, Adapter, Target + " TUs", bDebug);
		Report.Note(Format("%s: %d TUs, /arch %s (expected %s)", Target.c_str(), Adapter.value("tu", 0), ListText(Adapter.value("arch", FJson::array())).c_str(),
			ExpectArch.c_str()));

		// The adapter TU's own report (PtBuildReport.h), read from the loaded module.
		FJson Self = FJson::parse(Info.BuildFlagsJson, nullptr, false);
		if (Report.Check(Self.is_object(), "the adapter self-report is not a JSON object: " + Info.BuildFlagsJson.substr(0, 200)))
		{
			if (InjectFault == "g0" || InjectFault == "G0")
			{
				Self["__AVX2__"] = Self.value("__AVX2__", 0) != 0 ? 0 : 1;   // test only (-InjectPreflightFault=g0)
				Report.Note("test-only fault injected: the self-report's __AVX2__ was flipped");
			}
			const int32 Avx2 = Self.value("__AVX2__", -1);
			Report.Check(Avx2 == (ExpectArch == "AVX2" ? 1 : 0), Format("adapter self-report __AVX2__ %d, but %s's TUs are /arch %s (BuildFlags.json)", Avx2, Target.c_str(),
				ExpectArch.c_str()));
			Report.Check(Self.value("fp", std::string()) == "fast", "adapter self-report fp '" + Self.value("fp", std::string()) + "' (expected fast)");
			Report.Check(Self.value("_DLL", -1) == 1, "adapter self-report _DLL must be 1 (/MD, /MDd)");
			Report.Check(Self.value("_DEBUG", -1) == (bDebug ? 1 : 0), Format("adapter self-report _DEBUG %d in a %s build", Self.value("_DEBUG", -1), Configuration.c_str()));
			Report.Check(Self.value("_MSC_FULL_VER", 0ll) == static_cast<long long>(_MSC_FULL_VER),
				Format("adapter self-report _MSC_FULL_VER %lld, the bench was compiled by %d", Self.value("_MSC_FULL_VER", 0ll), _MSC_FULL_VER));
			Report.Note("adapter self-report: " + Self.dump());
		}

		if (!Group.empty())
		{
			const auto GroupFacts = Facts["groups"].find(Group);
			if (Report.Check(GroupFacts != Facts["groups"].end(), "BuildFlags.json has no TU of " + Group))
			{
				const std::string GroupArch = GroupFacts->value("expect_arch", std::string());
				Report.Check(IsOnly(GroupFacts->value("arch", FJson::array()), GroupArch), Group + " TUs: /arch " + ListText(GroupFacts->value("arch", FJson::array())) + " (profile "
					+ Profile + " expects " + GroupArch + ")");
				Report.Check(GroupArch == ExpectArch, Group + " (" + GroupArch + ") and its adapter " + Target + " (" + ExpectArch + ") must share one /arch");
				CheckCommonFacts(Report, *GroupFacts, Group + " TUs", bDebug);
				Report.Note(Format("%s: %d TUs, /arch %s", Group.c_str(), GroupFacts->value("tu", 0), ListText(GroupFacts->value("arch", FJson::array())).c_str()));
			}
		}
		if (Group == "sdk.physx")
		{
			const std::string Expected = bDebug ? "debug" : "release";
			Report.Check(IsOnly(Facts.value("physx_config", FJson::array()), Expected),
				"PhysX TUs build configuration " + ListText(Facts.value("physx_config", FJson::array())) + " (expected [" + Expected + "])");
		}
		if (Group == "sdk.jolt")
		{
			// 02 14.3 G0: no Profile / Debug Renderer / FP Exceptions in Jolt (Jolt.cmake turns them off).
			const FEffectiveSetting* Config = FindSetting(Settings, "jolt.config_string");
			if (Report.Check(Config != nullptr, "jolt.config_string is missing from the effective settings"))
			{
				for (const char* Forbidden : { "(Profile)", "(Debug Renderer)", "(FP Exceptions)" })
				{
					Report.Check(Config->Native.find(Forbidden) == std::string::npos, std::string("Jolt configuration string has ") + Forbidden + ": " + Config->Native);
				}
				// M10: Jolt's external profile (its JPH_PROFILE scopes as Tracy zones) belongs to the prof preset and to no other build.
				const bool bExternalProfile = Config->Native.find("(External Profile)") != std::string::npos;
				Report.Check(bExternalProfile == (PT_BUILD_WITH_PROFILER != 0), std::string("Jolt configuration string ") + (bExternalProfile ? "has" : "lacks")
					+ " (External Profile) in a " + (PT_BUILD_WITH_PROFILER != 0 ? "profiler" : "non-profiler") + " build: " + Config->Native);
				Report.Note("jolt.config_string: " + Config->Native);
			}
			for (const FJson& Define : Facts.value("jolt_public_defines", FJson::array()))
			{
				const std::string Name = Define.is_string() ? Define.get<std::string>() : std::string();
				Report.Check(Name.rfind("JPH_PROFILE_ENABLED", 0) != 0 && Name.rfind("JPH_DEBUG_RENDERER", 0) != 0 && Name.rfind("JPH_FLOATING_POINT_EXCEPTIONS_ENABLED", 0) != 0,
					"Jolt public define " + Name + " (profile / debug renderer / FP exceptions are off, Jolt.cmake)");
			}
		}
		return Report;
	}

	FResult Run(const FInputs& Inputs)
	{
		FResult Result;
		const FBenchRunSpec& Spec = *Inputs.Spec;
		const FResolvedPhysicsConfig& Config = *Inputs.Config;
		const FPhysScene& Scene = *Inputs.Scene;
		const std::string Fault = Spec.InjectPreflightFault;

		// What the gates read: the run's effective settings, with a test-only fault where -InjectPreflightFault asks for one.
		std::vector<FEffectiveSetting> Settings = *Inputs.Settings;
		std::string InjectNote;
		if (Fault == "listener")
		{
			FEffectiveSetting* Listener = nullptr;
			for (FEffectiveSetting& Setting : Settings)
			{
				Listener = Setting.Key == "listeners.installed" ? &Setting : Listener;
			}
			if (Listener != nullptr)
			{
				Listener->Canonical = 1.0;
				Listener->bHasCanonical = true;
				Listener->Native = "1 (test-only injection: -InjectPreflightFault=listener)";
			}
			InjectNote = "test-only fault injected: listeners.installed reads 1";
		}
		else if (Fault == "drawer")
		{
			bool bFound = false;
			for (FEffectiveSetting& Setting : Settings)
			{
				if (IsDrawerKey(Setting.Key))
				{
					Setting.Canonical = 1.0;
					Setting.bHasCanonical = true;
					Setting.Native = "attached (test-only injection: -InjectPreflightFault=drawer)";
					bFound = true;
				}
			}
			if (!bFound)
			{
				FEffectiveSetting Drawer;
				Drawer.Key = "harness.debug_drawer";
				Drawer.Native = "attached (test-only injection: -InjectPreflightFault=drawer)";
				Drawer.NativeUnit = "bool";
				Drawer.Unit = "bool";
				Drawer.Source = "READBACK";
				Drawer.Canonical = 1.0;
				Drawer.bHasCanonical = true;
				Settings.push_back(Drawer);
			}
			InjectNote = "test-only fault injected: a debug drawer reads attached";
		}
		else if (Fault.rfind("g9:", 0) == 0 || Fault.rfind("G9:", 0) == 0)
		{
			const std::string Key = Fault.substr(3);
			bool bFound = false;
			for (FEffectiveSetting& Setting : Settings)
			{
				if (Setting.Key == Key)
				{
					Setting.Canonical = (Setting.bHasCanonical ? Setting.Canonical : 0.0) + 1.0;
					Setting.bHasCanonical = true;
					Setting.Native += " (test-only injection: canonical + 1)";
					bFound = true;
				}
			}
			InjectNote = bFound ? "test-only fault injected: " + Key + " canonical + 1" : "test-only fault: no effective key " + Key;
		}

		// ---- G0 ----
		{
			FGateReport G0 = RunG0(Scene.GetBackendInfo(), Spec.Backend, Settings, Fault);
			Result.Reports.push_back(G0);
		}

		// ---- G9 ----
		{
			FGateReport G9;
			G9.Gate = "G9";
			G9.Target = Spec.Backend;
			if (Inputs.Tolerances == nullptr)
			{
				G9.Check(false, "tolerances.json: " + Inputs.TolerancesError);
			}
			else
			{
				Gates::FGateTarget Target;
				Target.Backend = Spec.Backend;
				Target.Variant = Inputs.Files->VariantName;
				Target.Profile = Config.ProfileName;
				Target.Config = &Config;
				Target.Tolerances = Inputs.Tolerances;
				const bool bPerfScene = Inputs.Scenario->GetKind() == EScenarioKind::Perf;
				const Gates::FReadbackContext Context{ &Config, Inputs.Scenario, Inputs.Params, Inputs.DtEngine, bPerfScene, Scene.GetStats().NumDynamicBodies };
				std::vector<std::string> SkipKeys = { "listeners.installed" };
				for (const FEffectiveSetting& Setting : Settings)
				{
					if (IsDrawerKey(Setting.Key))
					{
						SkipKeys.push_back(Setting.Key);
					}
				}
				if (!bPerfScene)
				{
					SkipKeys.push_back("shape.wrapping");   // gate / behaviour scenes may need wrappers (S02 COMNudge, S03 capsules)
				}
				Gates::CheckEffectiveReadback(Target, Context, Settings, SkipKeys, bPerfScene, Inputs.Scenario->GetName(), G9, &Result.Diff);
				if (Fault.rfind("g9:", 0) == 0 || Fault.rfind("G9:", 0) == 0)
				{
					G9.Note(InjectNote);
					// The injection must be able to fail G9 (M7 review): the same check on the untouched settings must find
					// fewer failures. Otherwise the key does not exist or G9 does not compare it here (not READBACK, no rule,
					// a scene-bound rule, left to G10a) and the run is refused as a usage error instead of passing silently.
					FGateReport Clean;
					Clean.Gate = "G9";
					Gates::CheckEffectiveReadback(Target, Context, *Inputs.Settings, SkipKeys, bPerfScene, Inputs.Scenario->GetName(), Clean);
					if (G9.Failures.size() <= Clean.Failures.size())
					{
						const std::string Key = Fault.substr(3);
						const FEffectiveSetting* Setting = FindSetting(*Inputs.Settings, Key);
						Result.InjectionError = "-InjectPreflightFault=" + Fault + ": G9 does not compare '" + Key + "' on " + Inputs.Scenario->GetName() + " ("
							+ (Setting == nullptr ? std::string("no such effective key") : "source " + Setting->Source + ", no G9 comparison here")
							+ "): the test-only injection would not fail G9 (use a READBACK key a tolerances.json G9 rule compares, e.g. world.gravity)";
					}
				}
			}
			Result.Reports.push_back(G9);
		}

		// ---- G10a ----
		{
			FGateReport G10a;
			G10a.Gate = "G10a";
			G10a.Target = Spec.Backend;
			const FPhysicsTestSettings& Test = Config.TestSettings;
			const PtSceneDesc& Desc = Scene.GetSceneDesc();
			if (!InjectNote.empty() && (Fault == "listener" || Fault == "drawer"))
			{
				G10a.Note(InjectNote);
			}
			if (Test.RunMode != EPhysicsRunMode::Bench)
			{
				G10a.Note(std::string("run mode ") + ToString(Test.RunMode) + ": G10a applies to bench-mode runs; this run is not headline-valid");
				Result.TimingInvalidReasons.push_back(std::string("G10a: run mode ") + ToString(Test.RunMode));
			}
			else if (Test.InstrumentFlags != 0)
			{
				std::string Names;
				for (const std::string& Name : GetInstrumentNames(Test.InstrumentFlags))
				{
					Names += (Names.empty() ? "" : ",") + Name;
				}
				G10a.Note("instrumentation tier " + Names + ": not refused (03 M7), timing_valid_for_headline = false");
				Result.TimingInvalidReasons.push_back("G10a: instrumentation tier " + Names);
			}
			else
			{
				// Tier 0 bench run: what the backend actually received and installed.
				G10a.Check(Desc.RunMode == PT_RUN_BENCH, Format("the backend scene was created with RunMode %u (expected Bench)", Desc.RunMode));
				G10a.Check(Desc.InstrumentFlags == 0, Format("the backend scene was created with InstrumentFlags 0x%x in a tier-0 run", Desc.InstrumentFlags));
				const FEffectiveSetting* Listener = FindSetting(Settings, "listeners.installed");
				if (G10a.Check(Listener != nullptr && Listener->bHasCanonical, "listeners.installed is missing or has no canonical value"))
				{
					G10a.Check(Listener->Canonical == 0.0, "a listener is installed in a tier-0 bench run: " + Listener->Native);
				}
				int32 NumDrawerKeys = 0;
				for (const FEffectiveSetting& Setting : Settings)
				{
					if (IsDrawerKey(Setting.Key))
					{
						++NumDrawerKeys;
						G10a.Check(Setting.bHasCanonical && Setting.Canonical == 0.0, "a debug drawer is attached in a tier-0 bench run: " + Setting.Key + " = " + Setting.Native);
					}
				}
				G10a.Note(Format("tier 0 bench run: listeners %s, %d drawer key(s) detached, scene desc RunMode %u InstrumentFlags 0x%x",
					Listener != nullptr ? Listener->Native.c_str() : "?", NumDrawerKeys, Desc.RunMode, Desc.InstrumentFlags));
			}
			Result.Reports.push_back(G10a);
		}
		for (const FGateReport& Report : Result.Reports)
		{
			Result.bPassed = Result.bPassed && Report.Result != EGateResult::Fail;
		}
		return Result;
	}
}
