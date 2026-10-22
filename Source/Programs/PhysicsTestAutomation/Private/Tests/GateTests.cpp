// GateTests.cpp - the M6 physics gates in PhysicsTestAutomation (03_WorkPlan M6, 02_Architecture 14.3): every gate runs on
// every backend x variant x physics profile through the C ABI - Null, Jolt.Default, Bullet.Default and the four PhysX
// variants (UE4Compat, UE4Compat-TGS, UE4Compat-Stab, SdkDefault) in UEParity and Normalized - with the tolerances of
// Config/Golden/tolerances.json. One doctest case per gate (PhysicsTest.Gates.<Gate>), one CTest entry per gate
// (PhysicsTest.Gates.<Gate>). A gate that a backend's capabilities make meaningless reports Skip, and passes only where
// tolerances.json registers the skip (Null has no collision and no contacts: G2's S03, G3-G5); S08.StartAsleep runs in
// UEParity only (G6 checks Normalized with a resting box). The target list is checked against the SDK backends this build
// configures (PT_GATE_SDK_BACKENDS, PhysicsTestAutomation/CMakeLists.txt) and the variants 03 M6 names, so a build that
// drops a backend or a variant file fails instead of gating fewer targets. A variant file outside that list (M9: e.g.
// Bullet.StaticAabbOff.ini for G11) is reported and not gated until it joins GOptInGateVariants (M6 round 1). The bench
// writes the same reports as conformance.json (PhysicsTestBench -Conformance, CTest PhysicsTest.Bench.Conformance).
#include "CoreMinimal.h"

#include "BenchmarkRunner.h"
#include "Containers/StringConv.h"
#include "Gates/PhysicsGates.h"
#include "Physics/PhysicsBackend.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsTestScenario.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef PT_GATE_SDK_BACKENDS
	#error "PT_GATE_SDK_BACKENDS (the SDK backends this build gates) must be defined by PhysicsTestAutomation/CMakeLists.txt"
#endif

using namespace PhysicsTest;

namespace
{
	std::filesystem::path GateConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return StringConv::Utf8ToPath(Override);
		}
		return StringConv::Utf8ToPath(PT_CONFIG_DIR);
	}

	const Gates::FTolerances& GetTolerances()
	{
		static Gates::FTolerances Tolerances;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			std::string Error;
			const bool bOk = Tolerances.LoadFile(Gates::GetTolerancesPath(GateConfigDir()), Error);
			INFO(Error);
			REQUIRE(bOk);
			bLoaded = true;
		}
		return Tolerances;
	}

	// One target: a resolved config with its names.
	struct FTargetConfig
	{
		std::string Backend;
		std::string Variant;
		std::string Profile;
		FResolvedPhysicsConfig Config;
	};

	using FVariantMap = std::map<std::string, std::vector<std::string>>;

	// The variants 03 M6 gates for each SDK backend: jolt.Default, bullet.Default, physx.{UE4Compat, UE4Compat-TGS,
	// UE4Compat-Stab, SdkDefault}.
	const FVariantMap& GetVariants03()
	{
		static const FVariantMap Variants = { { "Jolt", { "Default" } }, { "Bullet", { "Default" } },
			{ "PhysX", { "UE4Compat", "UE4Compat-TGS", "UE4Compat-Stab", "SdkDefault" } } };
		return Variants;
	}

	// Variant files gated beyond the 03 list. A new variant file (M9) joins here once its expected deviations are
	// registered in tolerances.json; until then it is reported as not gated instead of failing every gate case.
	const FVariantMap& GetOptInGateVariants()
	{
		static const FVariantMap Variants = {};
		return Variants;
	}

	bool IsListed(const FVariantMap& Map, const std::string& Backend, const std::string& Variant)
	{
		const auto Found = Map.find(Backend);
		return Found != Map.end() && std::find(Found->second.begin(), Found->second.end(), Variant) != Found->second.end();
	}

	// Null, then every SDK backend with each of its gated variant files (Config/Profiles/Backend/<Backend>.<Variant>.ini: the
	// 03 list and the opt-ins), in both profiles. PhysX's UE4Compat family comes before SdkDefault (one tolerances scale per
	// process: the gate runner re-initialises the idle module between them).
	std::vector<FTargetConfig> MakeGateTargets()
	{
		std::vector<std::pair<std::string, std::string>> Engines = { { "Null", "" } };
		std::vector<std::string> NotGated;
		for (const std::string& Backend : FPhysicsBackendRegistry::Get().GetAvailableNames())
		{
			if (Backend == "Null" || Backend == "NullDll" || Backend == "Chaos")
			{
				continue;
			}
			std::vector<std::string> Variants;
			const std::string Prefix = Backend + ".";
			std::error_code Error;
			for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GateConfigDir() / "Profiles" / "Backend", Error))
			{
				const std::string Stem = StringConv::PathToUtf8(Entry.path().stem());
				if (Entry.path().extension() == ".ini" && Stem.size() > Prefix.size() && Stem.compare(0, Prefix.size(), Prefix) == 0)
				{
					Variants.push_back(Stem.substr(Prefix.size()));
				}
			}
			// SdkDefault last (its own tolerances scale).
			std::sort(Variants.begin(), Variants.end(), [](const std::string& A, const std::string& B)
			{
				const bool bA = A == "SdkDefault";
				const bool bB = B == "SdkDefault";
				return bA != bB ? bB : A < B;
			});
			for (const std::string& Variant : Variants)
			{
				if (IsListed(GetVariants03(), Backend, Variant) || IsListed(GetOptInGateVariants(), Backend, Variant))
				{
					Engines.emplace_back(Backend, Variant);
				}
				else
				{
					NotGated.push_back(Backend + "." + Variant);
				}
			}
		}
		for (const std::string& Name : NotGated)
		{
			MESSAGE("not gated: variant file " << Name << ".ini is neither in the 03 M6 list nor in GetOptInGateVariants (GateTests.cpp)");
		}
		std::vector<FTargetConfig> Targets;
		for (const auto& [Backend, Variant] : Engines)
		{
			for (const char* Profile : { "UEParity", "Normalized" })
			{
				FBenchRunSpec Spec;
				Spec.Backend = Backend;
				Spec.BackendProfile = Variant;
				Spec.PhysicsProfile = Profile;
				Spec.ConfigDir = GateConfigDir();
				FTargetConfig Target;
				FBenchConfig Files;
				std::string Error;
				const bool bLoaded = LoadBenchConfig(Spec, Target.Config, Files, Error);
				INFO(Error);
				REQUIRE(bLoaded);
				Target.Backend = Backend;
				Target.Variant = Files.VariantName;
				Target.Profile = Target.Config.ProfileName;
				Targets.push_back(std::move(Target));
			}
		}
		return Targets;
	}

	// The SDK backends this build configures (PT_GATE_SDK_BACKENDS, e.g. "Jolt,Bullet,PhysX") with the variants 03 M6 gates
	// for each (GetVariants03).
	FVariantMap RequiredEngines()
	{
		const FVariantMap& Variants03 = GetVariants03();
		FVariantMap Required;
		std::stringstream List(PT_GATE_SDK_BACKENDS);
		std::string Backend;
		while (std::getline(List, Backend, ','))
		{
			if (Backend.empty())
			{
				continue;
			}
			const auto Found = Variants03.find(Backend);
			REQUIRE_MESSAGE(Found != Variants03.end(), "PT_GATE_SDK_BACKENDS names " << Backend << ", for which 03 M6 lists no variant");
			Required[Backend] = Found->second;
		}
		return Required;
	}

	// Every configured SDK backend is in the registry with each of its 03 variants in both profiles, and nothing outside
	// the 03 list and the opt-ins is gated: 2 x (Null + the variants + the opt-ins present) targets (14 with Jolt, Bullet
	// and PhysX in M6).
	void RequireExpectedTargets(const std::vector<FTargetConfig>& Targets)
	{
		const FVariantMap Required = RequiredEngines();
		const std::vector<std::string> Available = FPhysicsBackendRegistry::Get().GetAvailableNames();
		size_t Expected = 2u;   // Null in UEParity and Normalized
		for (const auto& [Backend, Variants] : Required)
		{
			CAPTURE(Backend);
			REQUIRE(std::find(Available.begin(), Available.end(), Backend) != Available.end());
			for (const std::string& Variant : Variants)
			{
				for (const char* Profile : { "UEParity", "Normalized" })
				{
					const std::string ProfileName(Profile);
					const bool bFound = std::any_of(Targets.begin(), Targets.end(), [&](const FTargetConfig& Target)
					{
						return Target.Backend == Backend && Target.Variant == Variant && Target.Profile == ProfileName;
					});
					CAPTURE(Variant);
					CAPTURE(ProfileName);
					REQUIRE(bFound);
				}
				Expected += 2u;
			}
		}
		for (const FTargetConfig& Target : Targets)
		{
			if (Target.Backend != "Null" && !IsListed(Required, Target.Backend, Target.Variant))
			{
				CAPTURE(Target.Backend);
				CAPTURE(Target.Variant);
				REQUIRE(IsListed(GetOptInGateVariants(), Target.Backend, Target.Variant));
				++Expected;
			}
		}
		REQUIRE(Targets.size() == Expected);
	}

	// Runs one gate on every target; every report must pass or carry a registered skip (RunPhysicsGates fails any other).
	// G12 reads the engine error bits of every frame the gates before it ran: RunPhysicsGates runs them along and returns
	// the G12 report only.
	void RunGateOnAllTargets(const char* GateId)
	{
		const Gates::FTolerances& Tolerances = GetTolerances();
		const std::vector<FTargetConfig> Targets = MakeGateTargets();
		RequireExpectedTargets(Targets);
		int32 Passed = 0;
		int32 Skipped = 0;
		for (const FTargetConfig& Item : Targets)
		{
			Gates::FGateTarget Target;
			Target.Backend = Item.Backend;
			Target.Variant = Item.Variant;
			Target.Profile = Item.Profile;
			Target.Config = &Item.Config;
			Target.Tolerances = &Tolerances;
			std::vector<FGateReport> Reports;
			std::string Error;
			const bool bRan = Gates::RunPhysicsGates(Target, std::vector<std::string>{ GateId }, Reports, Error);
			const std::string Label = Target.GetLabel();
			CAPTURE(Label);
			INFO(Error);
			REQUIRE(bRan);
			REQUIRE(Reports.size() == 1u);
			const FGateReport& Report = Reports.back();
			REQUIRE(Report.Gate == GateId);
			MESSAGE(Report.Summary());
			CHECK(Report.Result != EGateResult::Fail);
			Passed += Report.Result == EGateResult::Pass ? 1 : 0;
			Skipped += Report.Result == EGateResult::Skip ? 1 : 0;
		}
		MESSAGE(std::string(GateId) << ": " << Passed << " targets pass, " << Skipped << " skip (registered) of " << Targets.size());
		CHECK(Passed > 0);
		// Leave no module initialised with another target's process-wide settings (PhysX SdkDefault) for the next test.
		FPhysicsBackendRegistry::Get().TermIdleModules();
	}
}

TEST_CASE("PhysicsTest.Gates.ToleranceFileIsComplete")
{
	// Every value the gates read is registered, and every override names its reason (LoadText rejects a missing one).
	const Gates::FTolerances& Tolerances = GetTolerances();
	const std::pair<const char*, const char*> Checks[] = {
		{ "G1", "z60_abs_cm" }, { "G1", "v60_abs_cms" }, { "G1b", "z60_abs_cm" }, { "G2", "velocity_rel" }, { "G2", "mass_rel" },
		{ "G2", "inertia_tensor_rel" }, { "G2", "com_abs_cm" }, { "G2", "com_drift_cm" }, { "G2", "com_speed_cms" }, { "G2", "capsule_height_abs_cm" },
		{ "G2", "capsule_axis_standing_min" }, { "G2", "capsule_axis_lying_max" }, { "G3", "normal_z_min" }, { "G3", "point_on_surface_cm" },
		{ "G3", "wake_rest_frames" }, { "G3", "normal_impulse_eps" }, { "G4", "restitution_velocity_model" }, { "G4", "e_abs" }, { "G4", "apex_ratio_abs" }, { "G4", "contact_frame_slack" },
		{ "G4", "contact_detect_cms" }, { "G4", "contact_detect_rel" }, { "G5", "stop_distance_rel" }, { "G5", "final_speed_cms" }, { "G5", "hold_displacement_cm" },
		{ "G5", "hold_speed_cms" }, { "G5", "hold_acceleration_cms2" }, { "G5", "slide_fraction_min" }, { "G5", "slide_fraction_max" }, { "G6", "asleep_dz_cm" },
		{ "G6", "awake_fall_min_cm" }, { "G6", "wake_velocity_rel" }, { "G6", "wake_displacement_rel" }, { "G7", "omega_abs_rads" }, { "G7", "step_angle_abs_rad" },
		{ "G7", "step_angle_model" }, { "J1", "hinge_stretch_cm" }, { "J1", "ball_stretch_cm" }, { "J1", "hinge_axis_rad" }, { "J1", "swing_through_x_cm" },
		{ "J1", "snapshot_frame_cm" }, { "J1", "snapshot_frame_rad" }, { "J1", "moving_couple_rel" }, { "J1", "com_offset_stretch_cm" }, { "J1", "impulse_rel" },
		{ "J1", "couple_rel" }, { "J1", "collision_push_cm" }, { "J1", "disable_collision_drift_cm" },
	};
	for (const auto& [Gate, Check] : Checks)
	{
		CAPTURE(Gate);
		CAPTURE(Check);
		CHECK(Tolerances.Has(Gate, Check));
	}
	CHECK(Tolerances.GetSha256().size() == 64u);
	// The 03 M6 defaults themselves.
	CHECK(Tolerances.Get("G1", "z60_abs_cm", "Jolt", "Default", "UEParity") == 2e-3);
	CHECK(Tolerances.Get("G1", "z60_abs_cm", "Null", "", "UEParity") == 1e-9);
	CHECK(Tolerances.Get("G4", "e_abs", "Jolt", "Default", "UEParity") == 0.02);
	CHECK(Tolerances.Get("G4", "e_abs", "PhysX", "UE4Compat-TGS", "Normalized") == 0.02);   // the 03 default for every engine (M6 round 0)
	// PhysX's restitution velocity is a registered model (any PhysX variant), found on a run: post-run.
	bool bPostRun = false;
	CHECK(Tolerances.Get("G4", "restitution_velocity_model", "PhysX", "UE4Compat-TGS", "Normalized", nullptr, &bPostRun) == 1.0);
	CHECK(bPostRun);
	CHECK(Tolerances.Get("G4", "restitution_velocity_model", "Jolt", "Default", "UEParity", nullptr, &bPostRun) == 0.0);
	CHECK_FALSE(bPostRun);
	// Bullet's contact-state model (M9 S-3), registered after the M9 runs and signed off by the lead with the M9 commit.
	CHECK(Tolerances.Get("G4", "restitution_velocity_model", "Bullet", "StaticAabbOff", "Normalized", nullptr, &bPostRun) == 2.0);
	CHECK(bPostRun);
	CHECK(Tolerances.Get("G5", "stop_distance_rel", "Bullet", "Default", "Normalized") == 0.01);
	CHECK(Tolerances.Get("G2", "com_abs_cm", "PhysX", "UE4Compat", "UEParity") == 1e-5);
	// Skips: Null may skip what needs collision or contacts; no SDK engine and no Chaos may skip anything.
	for (const char* Gate : { "G2", "G3", "G4", "G5" })
	{
		CAPTURE(Gate);
		CHECK(Tolerances.FindAllowedSkip(Gate, "Null", "", "UEParity") != nullptr);
		CHECK(Tolerances.FindAllowedSkip(Gate, "NullDll", "", "Normalized") != nullptr);
		CHECK(Tolerances.FindAllowedSkip(Gate, "Jolt", "Default", "UEParity") == nullptr);
		CHECK(Tolerances.FindAllowedSkip(Gate, "PhysX", "UE4Compat", "UEParity") == nullptr);
		CHECK(Tolerances.FindAllowedSkip(Gate, "Chaos", "", "UEParity") == nullptr);
	}
	for (const char* Gate : { "G1", "G1b", "G6", "G7", "G9", "G12" })
	{
		CAPTURE(Gate);
		CHECK(Tolerances.FindAllowedSkip(Gate, "Null", "", "UEParity") == nullptr);
	}
	// J1 (M9 fix round 0): a backend without joints skips - Null, and the Chaos bridge until it declares PT_CAP_JOINT_* (once it
	// declares one the gate runs and cannot skip); no SDK engine.
	CHECK(Tolerances.FindAllowedSkip("J1", "Null", "", "UEParity") != nullptr);
	CHECK(Tolerances.FindAllowedSkip("J1", "NullDll", "", "Normalized") != nullptr);
	CHECK(Tolerances.FindAllowedSkip("J1", "Chaos", "", "UEParity") != nullptr);
	CHECK(Tolerances.FindAllowedSkip("J1", "Jolt", "Default", "UEParity") == nullptr);
	CHECK(Tolerances.FindAllowedSkip("J1", "Bullet", "Default", "Normalized") == nullptr);
	CHECK(Tolerances.FindAllowedSkip("J1", "PhysX", "UE4Compat", "Normalized") == nullptr);
	// A rule needs a reason: a "const:" expectation or an override without one does not load.
	Gates::FTolerances Bad;
	std::string Error;
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G1":{"z60_abs_cm":{"default":1}}}})", "bad", Error));
	CHECK(Error.find("reason") != std::string::npos);
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G9":{"readback":{"*":{"k":{"expect":"const:1"}}}}}})", "bad", Error));
	// An allowed skip needs its reason too, and "registered" is pre-run or post-run.
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G3":{"allowed_skip":[{"engine":"Null"}]}}})", "bad", Error));
	CHECK(Error.find("reason") != std::string::npos);
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G":{"c":{"default":1,"reason":"d","overrides":[
		{"engine":"E","value":2,"reason":"e","registered":"later"}]}}}})", "bad", Error));
	// A post-run registration names its sign-off ("pending" until the lead accepts it); a pre-run one has none (M6 round 1).
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G":{"c":{"default":1,"reason":"d","overrides":[
		{"engine":"E","value":2,"reason":"e","registered":"post-run"}]}}}})", "bad", Error));
	CHECK(Error.find("sign_off") != std::string::npos);
	CHECK_FALSE(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G":{"c":{"default":1,"reason":"d","overrides":[
		{"engine":"E","value":2,"reason":"e","sign_off":"pending"}]}}}})", "bad", Error));
	CHECK(Error.find("sign_off") != std::string::npos);
	// Every post-run registration of the real file carries its sign-off; the list is what the lead signs. M9 commit: the lead
	// signed every one (2026-10-22) - a new post-run registration waits as "pending" and fails here until the lead signs it, so
	// no commit carries an unsigned one (docs/Progress.md M9 리드 결정).
	const std::vector<std::string> PostRun = Tolerances.ListPostRunRegistrations();
	CHECK(PostRun.size() == 11u);   // G4 PhysX (D-3), G4 Bullet model 2 (M9 S-3), G5 PhysX PGS x 6 (D-4), G7 Null / NullDll (D-6), J1 Bullet ball (M9 J-1)
	for (const std::string& Line : PostRun)
	{
		MESSAGE("post-run registration: " << Line);
		CHECK(Line.find("(sign-off: ") != std::string::npos);
		CHECK_MESSAGE(Line.find("(sign-off: pending)") == std::string::npos, "unsigned post-run registration: " << Line);
	}
	std::string SignOff;
	CHECK(Tolerances.Get("G5", "hold_speed_cms", "PhysX", "SdkDefault", "Normalized", nullptr, &bPostRun, &SignOff) == 3.0);
	CHECK(bPostRun);
	CHECK_FALSE(SignOff.empty());
	// FindOverride names what conformance.json lists as the verdict's post-run registrations (M6 round 2).
	const Gates::FToleranceOverride* Used = Tolerances.FindOverride("G5", "hold_speed_cms", "PhysX", "SdkDefault", "Normalized");
	CHECK((Used != nullptr && Used->bPostRun && Used->Variant == "SdkDefault" && Used->SignOff == SignOff));
	CHECK(Tolerances.FindOverride("G5", "hold_speed_cms", "PhysX", "UE4Compat-TGS", "Normalized") == nullptr);   // TGS holds: the default
	CHECK(Tolerances.FindOverride("G1", "z60_abs_cm", "Jolt", "Default", "UEParity") == nullptr);
	// Specificity: engine + variant beats engine.
	CHECK(Bad.LoadText(R"({"schema":"physicstest.tolerances/1","version":1,"gates":{"G":{"c":{"default":1,"reason":"d","overrides":[
		{"engine":"E","value":2,"reason":"e"},{"engine":"E","variant":"V","value":3,"reason":"ev"},{"engine":"E","profile":"P","value":4,"reason":"ep"}]}}}})", "ok", Error));
	CHECK(Bad.Get("G", "c", "E", "V", "P") == 3.0);
	CHECK(Bad.Get("G", "c", "E", "W", "P") == 4.0);
	CHECK(Bad.Get("G", "c", "E", "W", "Q") == 2.0);
	CHECK(Bad.Get("G", "c", "F", "V", "P") == 1.0);
}

TEST_CASE("PhysicsTest.Gates.G1") { RunGateOnAllTargets("G1"); }
TEST_CASE("PhysicsTest.Gates.G1b") { RunGateOnAllTargets("G1b"); }
TEST_CASE("PhysicsTest.Gates.G2") { RunGateOnAllTargets("G2"); }
TEST_CASE("PhysicsTest.Gates.G3") { RunGateOnAllTargets("G3"); }
TEST_CASE("PhysicsTest.Gates.G4") { RunGateOnAllTargets("G4"); }
TEST_CASE("PhysicsTest.Gates.G5") { RunGateOnAllTargets("G5"); }
TEST_CASE("PhysicsTest.Gates.G6") { RunGateOnAllTargets("G6"); }
TEST_CASE("PhysicsTest.Gates.G7") { RunGateOnAllTargets("G7"); }
TEST_CASE("PhysicsTest.Gates.G9") { RunGateOnAllTargets("G9"); }
TEST_CASE("PhysicsTest.Gates.J1") { RunGateOnAllTargets("J1"); }
TEST_CASE("PhysicsTest.Gates.G12") { RunGateOnAllTargets("G12"); }

// The selection rules of RunPhysicsGates, on Null (cheap): a Skip that tolerances.json does not register is a Fail, and G12
// selected alone runs the gates before it (whose frames it judges) but reports itself only.
TEST_CASE("PhysicsTest.Gates.SelectionRules")
{
	FResolvedPhysicsConfig Config;
	{
		FBenchRunSpec Spec;
		Spec.Backend = "Null";
		Spec.PhysicsProfile = "UEParity";
		Spec.ConfigDir = GateConfigDir();
		FBenchConfig Files;
		std::string Error;
		const bool bLoaded = LoadBenchConfig(Spec, Config, Files, Error);
		INFO(Error);
		REQUIRE(bLoaded);
	}
	Gates::FGateTarget Target;
	Target.Backend = "Null";
	Target.Profile = Config.ProfileName;
	Target.Config = &Config;
	Target.Tolerances = &GetTolerances();
	std::vector<FGateReport> Reports;
	std::string Error;
	REQUIRE(Gates::RunPhysicsGates(Target, { "G3" }, Reports, Error));
	REQUIRE(Reports.size() == 1u);
	CHECK(Reports[0].Result == EGateResult::Skip);   // registered: Null declares no contact points
	// The same file without G3's allowed_skip: Null's Skip becomes a Fail.
	std::ifstream File(Gates::GetTolerancesPath(GateConfigDir()), std::ios::binary);
	std::stringstream Text;
	Text << File.rdbuf();
	std::string Json = Text.str();
	const size_t Begin = Json.find("\"allowed_skip\"", Json.find("\"G3\""));
	REQUIRE(Begin != std::string::npos);
	const size_t End = Json.find("],", Begin);
	REQUIRE(End != std::string::npos);
	Json.erase(Begin, End + 2 - Begin);
	Gates::FTolerances WithoutSkip;
	const bool bLoaded = WithoutSkip.LoadText(Json, "tolerances.json without G3.allowed_skip", Error);
	INFO(Error);
	REQUIRE(bLoaded);
	CHECK(WithoutSkip.FindAllowedSkip("G3", "Null", "", "UEParity") == nullptr);
	Target.Tolerances = &WithoutSkip;
	REQUIRE(Gates::RunPhysicsGates(Target, { "G3" }, Reports, Error));
	REQUIRE(Reports.size() == 1u);
	CHECK(Reports[0].Result == EGateResult::Fail);
	REQUIRE(!Reports[0].Failures.empty());
	CHECK(Reports[0].Failures[0].find("unregistered skip") != std::string::npos);
	// G12 alone: one report, after the whole suite ran (it judges the frames of the gates before it).
	Target.Tolerances = &GetTolerances();
	REQUIRE(Gates::RunPhysicsGates(Target, { "G12" }, Reports, Error));
	REQUIRE(Reports.size() == 1u);
	CHECK(Reports[0].Gate == "G12");
	CHECK(Reports[0].Result == EGateResult::Pass);
	const bool bRanSuite = std::any_of(Reports[0].Notes.begin(), Reports[0].Notes.end(),
		[](const std::string& Note) { return Note.find("the 10 gates before G12 ran too") != std::string::npos; });
	CHECK(bRanSuite);
	MESSAGE(Reports[0].Summary());
	FPhysicsBackendRegistry::Get().TermIdleModules();
}
