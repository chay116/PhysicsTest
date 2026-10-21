// PhysicsProfileTests.cpp - profile layers, Origin tracking, the variant key restriction, -ini: / -ExecCmds and the
// shipped Config/ drafts (03_WorkPlan M1 "프로필", 02_Architecture 10, DL18, DL44).
#include "CoreMinimal.h"

#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyUtils.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#ifndef PT_CONFIG_DIR
	#error "PT_CONFIG_DIR (the repository's Config/ directory) is defined by PhysicsTestAutomation's CMakeLists.txt"
#endif

using namespace PhysicsTest;

namespace
{
	std::string ConfigDir()
	{
		std::string Override;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", Override))
		{
			return Override;
		}
		return PT_CONFIG_DIR;
	}

	FConfigFile LoadBase()
	{
		FConfigFile Base;
		std::string Error;
		const bool bLoaded = FConfigFile::LoadFile(ConfigDir() + "/BasePhysicsTest.ini", Base, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		Base.Name = "Config/BasePhysicsTest.ini";
		return Base;
	}

	FPhysicsProfile LoadProfile(const char* Name)
	{
		FPhysicsProfile Profile;
		std::string Error;
		const bool bLoaded = FPhysicsProfile::LoadIni(ConfigDir() + "/Profiles/" + Name + ".ini", Profile, Error);
		INFO(Error);
		REQUIRE(bLoaded);
		Profile.Ini.Name = std::string("Config/Profiles/") + Name + ".ini";
		return Profile;
	}

	FBackendVariant MakeVariant(const char* Text, const char* DisplayName = "Variant.ini")
	{
		FBackendVariant Variant;
		std::string Error;
		const bool bParsed = FBackendVariant::FromText(Text, DisplayName, Variant, Error);
		INFO(Error);
		REQUIRE(bParsed);
		return Variant;
	}

	bool Resolve(const FConfigFile& Base, const FPhysicsProfile& Profile, const FBackendVariant* Variant, const std::string& CommandLine,
		FResolvedPhysicsConfig& Out, std::string& Error, const FConfigFile* ScenePinned = nullptr)
	{
		FProfileResolveInputs Inputs;
		Inputs.Base = &Base;
		Inputs.Profile = &Profile;
		Inputs.Variant = Variant;
		Inputs.ScenePinned = ScenePinned;
		Inputs.CommandLine = CommandLine;
		return FProfileResolver::Resolve(Inputs, Out, Error);
	}

	const char* PhysXVariantText =
		"[PhysicsTest.Variant]\n"
		"Engine=PhysX\n"
		"Name=UE4Compat\n"
		"Label=UE4 settings on PhysX 5.11\n"
		"+FidelityNotes=PhysX 5.11, not the PhysX 3.4 UE4 shipped\n"
		"[PhysicsTest.Backend.PhysX]\n"
		"; [V] PxSceneDesc.h\n"
		"SolverType=PGS\n"
		"[PhysicsTest.Backend.PhysX.Init]\n"
		"ToleranceScale=(Length=100,Speed=1000)\n"
		"[/Script/PhysicsTest.PhysicsTestSettings]\n"
		"; [M] UE4 substep formula\n"
		"SubstepClampRule=UE4PhysSubstep\n";

	const char* JoltVariantText =
		"[PhysicsTest.Variant]\n"
		"Engine=Jolt\n"
		"Name=Default\n"
		"[PhysicsTest.Backend.Jolt]\n"
		"NumVelocitySteps=10\n"
		"[ConsoleVariables]\n"
		"jolt.SomeEngineVariable=1\n";
}

TEST_CASE("PhysicsTest.Profile.ShippedFilesLoad")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile UEParity = LoadProfile("UEParity");
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	CHECK(UEParity.Name == "UEParity");
	CHECK(Normalized.Name == "Normalized");
	CHECK(UEParity.Version == 1);
	CHECK(UEParity.GetKeyPolicy("PhysicsTestSettings.SubstepClampRule") == EProfileKeyPolicy::EngineDefault);
	CHECK(UEParity.GetKeyPolicy("PhysicsTestSettings.RestitutionThresholdVelocity") == EProfileKeyPolicy::EngineDefault);
	CHECK(Normalized.GetKeyPolicy("PhysicsTestSettings.RestitutionThresholdVelocity") == EProfileKeyPolicy::Pinned);
	CHECK(Normalized.GetKeyPolicy("BodyInstanceDefaults.LinearDamping") == EProfileKeyPolicy::Pinned);
	CHECK(UEParity.Sha256() != Normalized.Sha256());

	for (const FPhysicsProfile* Profile : { &UEParity, &Normalized })
	{
		FResolvedPhysicsConfig Config;
		std::string Error;
		CHECK(Resolve(Base, *Profile, nullptr, "", Config, Error));
		INFO(Error);
		CHECK(Config.Warnings.empty());
		CHECK(Config.ProfileName == Profile->Name);
		// CTest PhysicsTest.Probe.HarnessCVars compares these with a program that does not link BodyUtils.
		if (FParse::Param(FCommandLine::Get(), "PtPrintPhysicsSha256"))
		{
			std::printf("physics_sha256 %s=%s\n", Profile->Name.c_str(), Config.PhysicsSha256().c_str());
		}
	}
}

TEST_CASE("PhysicsTest.Profile.BaseHasNoPhysicsCoreSection")
{
	// DL44: every UE key lives in [/Script/Engine.PhysicsSettings]; BasePhysicsTest.ini has no PhysicsCore section ...
	const FConfigFile Base = LoadBase();
	CHECK(Base.FindSection(ConfigSections::PhysicsSettingsCore) == nullptr);
	for (const std::string& Section : Base.GetSectionNames())
	{
		CHECK_FALSE(Section.starts_with("/Script/PhysicsCore"));
	}
	REQUIRE(Base.FindSection(ConfigSections::PhysicsSettings) != nullptr);
	CHECK(Base.FindSection(ConfigSections::PhysicsSettings)->Contains("DefaultGravityZ"));
	CHECK(Base.FindSection(ConfigSections::PhysicsSettings)->Contains("SolverOptions"));   // a core field, Engine section

	// ... and the resolver refuses one anywhere, with the reason.
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	FConfigFile Pinned("Scene.ini");
	std::string Error;
	REQUIRE(Pinned.ParseText("[/Script/PhysicsCore.PhysicsSettingsCore]\nDefaultGravityZ=-490\n", Error));
	FResolvedPhysicsConfig Config;
	CHECK_FALSE(Resolve(Base, Normalized, nullptr, "", Config, Error, &Pinned));
	CHECK(Error.find("Scene.ini:1") != std::string::npos);
	CHECK(Error.find("DL44") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.BaseMatchesBuiltIn")
{
	// Every modelled key is in BasePhysicsTest.ini with exactly the code default (both cite the same UE source).
	const FConfigFile Base = LoadBase();
	const auto Defaults = FProfileResolver::GetBuiltInDefaults();
	CHECK(Defaults.size() > 60u);
	for (const auto& [Key, Default] : Defaults)
	{
		CAPTURE(Key);
		const size_t Dot = Key.find('.');
		const std::string Prefix = Key.substr(0, Dot);
		const std::string Name = Key.substr(Dot + 1);
		const char* Section = Prefix == "PhysicsSettings" ? ConfigSections::PhysicsSettings
			: Prefix == "PhysicsTestSettings" ? ConfigSections::PhysicsTestSettings
			: Prefix == "BodyInstanceDefaults" ? ConfigSections::BodyInstanceDefaults
			: ConfigSections::PhysicalMaterialDefaults;
		std::string Text;
		REQUIRE(Base.GetString(Section, Name, Text));
		std::string Normalised, Error;
		REQUIRE(FProfileResolver::NormalizeValue(Key, Text, Normalised, Error));
		CHECK(Normalised == Default);
	}
}

TEST_CASE("PhysicsTest.Profile.EveryValueHasAnOrigin")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	const FBackendVariant PhysX = MakeVariant(PhysXVariantText, "Config/Profiles/Backend/PhysX.UE4Compat.ini");
	FConfigFile Scene("Scene:S00.FreeFall");
	std::string Error;
	REQUIRE(Scene.ParseText("[/Script/PhysicsTest.PhysicsTestSettings]\nFixedDeltaTime=0.016666668\n", Error));
	FResolvedPhysicsConfig Config;
	const std::string CommandLine =
		"-Scenario=S00.FreeFall -ini:PhysicsTest:[/Script/Engine.PhysicsSettings]:MaxSubsteps=4 -ExecCmds=\"p.ComNudgeAffectsInertia 0\"";
	REQUIRE(Resolve(Base, Normalized, &PhysX, CommandLine, Config, Error, &Scene));
	INFO(Error);

	CHECK(Config.Values.size() > 70u);
	for (const auto& [Key, Value] : Config.Values)
	{
		CAPTURE(Key);
		CHECK_FALSE(Value.OriginName.empty());
		CHECK_FALSE(Value.Value.empty());
	}
	const auto Origin = [&Config](const char* Key) { return Config.Find(Key)->Origin; };
	const auto OriginName = [&Config](const char* Key) { return Config.Find(Key)->OriginName; };
	CHECK(Origin("PhysicsSettings.DefaultGravityZ") == EConfigOrigin::UEDefault);
	CHECK(OriginName("PhysicsSettings.DefaultGravityZ").starts_with("Config/BasePhysicsTest.ini:"));
	CHECK(Origin("BodyInstanceDefaults.LinearDamping") == EConfigOrigin::PhysicsProfile);
	CHECK(OriginName("BodyInstanceDefaults.LinearDamping").starts_with("Config/Profiles/Normalized.ini:"));
	CHECK(Origin("PhysicsTestSettings.SubstepClampRule") == EConfigOrigin::BackendVariant);
	CHECK(Origin("Backend.PhysX.SolverType") == EConfigOrigin::BackendVariant);
	CHECK(Origin("PhysicsTestSettings.FixedDeltaTime") == EConfigOrigin::ScenePinned);
	CHECK(Origin("PhysicsSettings.MaxSubsteps") == EConfigOrigin::CommandLine);
	CHECK(OriginName("PhysicsSettings.MaxSubsteps") == "-ini:1");
	CHECK(Origin("ConsoleVariables.p.ComNudgeAffectsInertia") == EConfigOrigin::CommandLine);
	CHECK(OriginName("ConsoleVariables.p.ComNudgeAffectsInertia") == "-ExecCmds:1");
	CHECK(Config.Find("PhysicsSettings.DefaultGravityZ")->Unit == "cm/s^2");

	// Applied values.
	CHECK(Config.Settings.MaxSubsteps == 4);
	CHECK(Config.TestSettings.SubstepClampRule == ESubstepClampRule::UE4PhysSubstep);
	CHECK_FALSE(Config.TestSettings.bAllowSleeping);
	CHECK(Config.Engine == "PhysX");
	CHECK(Config.SceneOptions == std::vector<std::pair<std::string, std::string>>{ { "SolverType", "PGS" } });
	CHECK(Config.InitOptions == std::vector<std::pair<std::string, std::string>>{ { "ToleranceScale", "(Length=100,Speed=1000)" } });
	CHECK(PhysX.Label == "UE4 settings on PhysX 5.11");
	CHECK(PhysX.FidelityNotes.size() == 1u);
}

TEST_CASE("PhysicsTest.Profile.VariantMayNotChangePinnedKeys")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	const auto Expect = [&](const char* VariantText, const char* ExpectedFragment)
	{
		const FBackendVariant Variant = MakeVariant(VariantText, "Bad.ini");
		FResolvedPhysicsConfig Config;
		std::string Error;
		CAPTURE(VariantText);
		CHECK_FALSE(Resolve(Base, Normalized, &Variant, "", Config, Error));
		CAPTURE(Error);
		CHECK(Error.find(ExpectedFragment) != std::string::npos);
		CHECK(Error.find("Bad.ini:") != std::string::npos);
	};
	// A variant silently undoing Normalized's damping 0 is a load error (02 10).
	Expect("[PhysicsTest.Variant]\nEngine=Jolt\nName=Bad\n[/Script/PhysicsTest.BodyInstanceDefaults]\nLinearDamping=0.05\n",
		"pinned key BodyInstanceDefaults.LinearDamping");
	Expect("[PhysicsTest.Variant]\nEngine=Jolt\nName=Bad\n[/Script/Engine.PhysicsSettings]\nDefaultGravityZ=-981\n",
		"pinned key PhysicsSettings.DefaultGravityZ");
	Expect("[PhysicsTest.Variant]\nEngine=Jolt\nName=Bad\n[/Script/PhysicsTest.PhysicsTestSettings]\nbAllowSleeping=True\n",
		"pinned key PhysicsTestSettings.bAllowSleeping");
	// Another engine's keys.
	Expect("[PhysicsTest.Variant]\nEngine=Jolt\nName=Bad\n[PhysicsTest.Backend.PhysX]\nSolverType=TGS\n", "may not set PhysX keys");
	// A harness console variable is pinned like any other harness key.
	Expect("[PhysicsTest.Variant]\nEngine=Chaos\nName=Bad\n[ConsoleVariables]\np.ComNudgeAffectsInertia=0\n",
		"pinned key ConsoleVariables.p.ComNudgeAffectsInertia");

	// Allowed: own engine keys, EngineDefault keys, engine console variables.
	const FBackendVariant Jolt = MakeVariant(JoltVariantText);
	FResolvedPhysicsConfig Config;
	std::string Error;
	CHECK(Resolve(Base, Normalized, &Jolt, "", Config, Error));
	INFO(Error);
	CHECK(Config.SceneOptions.size() == 1u);
	CHECK(Config.ConsoleVariables == std::vector<std::pair<std::string, std::string>>{ { "jolt.SomeEngineVariable", "1" } });
	CHECK(Config.Find("ConsoleVariables.jolt.SomeEngineVariable")->Origin == EConfigOrigin::BackendVariant);

	// A console variable the profile sets becomes profile-owned: a variant may not change it.
	FPhysicsProfile WithCVar;
	REQUIRE(FPhysicsProfile::FromText("[PhysicsTest.Profile]\nName=P\n[ConsoleVariables]\np.Chaos.Solver.Sleep.Enabled=0\n", "P.ini", WithCVar, Error));
	const FBackendVariant Chaos = MakeVariant("[PhysicsTest.Variant]\nEngine=Chaos\nName=Game\n[ConsoleVariables]\np.Chaos.Solver.Sleep.Enabled=1\n", "Chaos.ini");
	CHECK_FALSE(Resolve(Base, WithCVar, &Chaos, "", Config, Error));
	CHECK(Error.find("pinned key ConsoleVariables.p.Chaos.Solver.Sleep.Enabled") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.LoadErrors")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile UEParity = LoadProfile("UEParity");
	const auto ExpectPinned = [&](const char* SceneText, const char* Fragment)
	{
		FConfigFile Scene("Scene.ini");
		std::string Error;
		REQUIRE(Scene.ParseText(SceneText, Error));
		FResolvedPhysicsConfig Config;
		CAPTURE(SceneText);
		CHECK_FALSE(Resolve(Base, UEParity, nullptr, "", Config, Error, &Scene));
		CAPTURE(Error);
		CHECK(Error.find(Fragment) != std::string::npos);
	};
	ExpectPinned("[/Script/PhysicsTest.PhysicsTestSettings]\nTickMode=Fast\n", "unknown EPhysicsTickMode value 'Fast'");
	ExpectPinned("[/Script/PhysicsTest.PhysicsTestSettings]\nTickRate=60\n", "unknown key 'TickRate'");
	ExpectPinned("[/Script/PhysicsTest.BodyInstanceDefaults]\nLinearDamping=abc\n", "expected a finite number");
	ExpectPinned("[/Script/PhysicsTest.BodyInstanceDefaults]\nPositionSolverIterationCount=300\n", "in range");
	ExpectPinned("[/Script/PhysicsTest.BodyInstanceDefaults]\n+LinearDamping=0\n", "array operators");
	ExpectPinned("[/Script/Engine.PhysicsSettings]\nSolverOptions=(PositionIterations=x)\n", "PositionIterations");
	ExpectPinned("[/Script/PhysicsTest.ForcedOverrides]\nFriction=0.5\n", "M9");
	ExpectPinned("[Some.Other.Section]\nA=1\n", "unknown section");
	ExpectPinned("[PhysicsTest.Backend.PhysX]\nSolverType=TGS\n", "belong in a backend variant");
	ExpectPinned("[ConsoleVariables]\np.ComNudgeAffectsInertia=maybe\n", "does not parse");
	ExpectPinned("[/Script/Engine.PhysicsSettings]\nDefaultGravityZ=nan\n", "expected a finite number");
	ExpectPinned("[/Script/PhysicsTest.BodyInstanceDefaults]\nMaxAngularVelocity=inf\n", "expected a finite number");
	ExpectPinned("[/Script/PhysicsTest.PhysicsTestSettings]\nFixedDeltaTime=1e39\n", "expected a finite number");

	std::string Error;
	FPhysicsProfile Bad;
	CHECK_FALSE(FPhysicsProfile::FromText("[PhysicsTest.Profile]\nName=X\n+EngineDefaultKeys=PhysicsTestSettings.NoSuchKey\n", "X.ini", Bad, Error));
	CHECK(Error.find("NoSuchKey") != std::string::npos);
	CHECK_FALSE(FPhysicsProfile::FromText("[/Script/Engine.PhysicsSettings]\nDefaultGravityZ=-980\n", "NoMeta.ini", Bad, Error));
	FBackendVariant BadVariant;
	CHECK_FALSE(FBackendVariant::FromText("[PhysicsTest.Variant]\nEngine=Havok\nName=X\n", "V.ini", BadVariant, Error));
	CHECK(Error.find("Havok") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.CommandLineIniOverride")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile UEParity = LoadProfile("UEParity");
	FResolvedPhysicsConfig Config;
	std::string Error;
	// UE syntax (ConfigCacheIni.cpp:2413-2500): several pairs per switch, several switches, -ini:Engine: as well.
	const std::string CommandLine = "-ini:PhysicsTest:[/Script/Engine.PhysicsSettings]:DefaultGravityZ=-490,"
		"[/Script/PhysicsTest.PhysicsTestSettings]:TickMode=Sync "
		"-ini:Engine:[/Script/Engine.PhysicsSettings]:MaxPhysicsDeltaTime=0.05 "
		"-ini:Game:[/Script/Foo.Bar]:X=1 "
		"\"-ini:PhysicsTest:[/Script/PhysicsTest.PhysicsTestSettings]:RunMode=Viz\"";
	REQUIRE(Resolve(Base, UEParity, nullptr, CommandLine, Config, Error));
	INFO(Error);
	CHECK(Config.Settings.DefaultGravityZ == -490.0f);
	CHECK(Config.TestSettings.TickMode == EPhysicsTickMode::Sync);
	CHECK(Config.Settings.MaxPhysicsDeltaTime == 0.05f);
	CHECK(Config.TestSettings.RunMode == EPhysicsRunMode::Viz);
	CHECK(Config.Find("PhysicsSettings.DefaultGravityZ")->Origin == EConfigOrigin::CommandLine);
	CHECK(Config.Find("PhysicsSettings.DefaultGravityZ")->Value == "-490");
	CHECK(std::any_of(Config.Warnings.begin(), Config.Warnings.end(), [](const std::string& W) { return W.find("-ini:Game") != std::string::npos; }));

	CHECK_FALSE(Resolve(Base, UEParity, nullptr, "-ini:PhysicsTest:DefaultGravityZ=-490", Config, Error));
	CHECK(Error.find("malformed") != std::string::npos);
	CHECK_FALSE(Resolve(Base, UEParity, nullptr, "-ini:PhysicsTest:[/Script/PhysicsTest.PhysicsTestSettings]:TickMode=Fast", Config, Error));
	CHECK(Error.find("-ini:1") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.ExecCmdsSetConsoleVariables")
{
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile UEParity = LoadProfile("UEParity");
	FResolvedPhysicsConfig Config;
	std::string Error;
	REQUIRE(Resolve(Base, UEParity, nullptr, "-ExecCmds=\"p.ComNudgeAffectsInertia 0, p.Chaos.Solver.Sleep.Enabled=0\"", Config, Error));
	INFO(Error);
	CHECK(Config.ConsoleVariables == std::vector<std::pair<std::string, std::string>>{
		{ "p.ComNudgeAffectsInertia", "0" }, { "p.Chaos.Solver.Sleep.Enabled", "0" } });
	CHECK(Config.Find("ConsoleVariables.p.Chaos.Solver.Sleep.Enabled")->Origin == EConfigOrigin::CommandLine);

	// Applying sets the harness variable (with the console's priority) and hands the rest to the engine (Chaos).
	FBodyInstance Body;
	Body.bSimulatePhysics = true;
	Body.COMNudge = FVector(0.0, 0.0, 10.0);
	FBodySetup Setup;
	Setup.AggGeom.BoxElems.emplace_back(100.0);
	const FPhysicalMaterial Material;
	const double WithTerm = BodyUtils::ComputeMassProperties(Body, Setup, &Material, EMassRule::UEBodyUtils).InertiaTensor.Z;
	std::vector<std::pair<std::string, std::string>> EngineVariables;
	std::string ApplyError;
	CHECK(FProfileResolver::ApplyConsoleVariables(Config, EngineVariables, ApplyError));
	CHECK(ApplyError.empty());
	CHECK(EngineVariables == std::vector<std::pair<std::string, std::string>>{ { "p.Chaos.Solver.Sleep.Enabled", "0" } });
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable("p.ComNudgeAffectsInertia");
	REQUIRE(Variable != nullptr);
	CHECK_FALSE(Variable->GetBool());
	CHECK(Variable->GetSetBy() == ECVF_SetByConsole);
	const double WithoutTerm = BodyUtils::ComputeMassProperties(Body, Setup, &Material, EMassRule::UEBodyUtils).InertiaTensor.Z;
	CHECK(WithTerm - WithoutTerm == doctest::Approx(std::pow(1000.0, 0.75) * 100.0));

	// A later ini value has a lower priority than the console's: it is not applied, and Apply says so instead of
	// leaving Values claiming it (UE would log and ignore it).
	FPhysicsProfile IniCVar;
	REQUIRE(FPhysicsProfile::FromText("[PhysicsTest.Profile]\nName=IniCVar\n[ConsoleVariables]\np.ComNudgeAffectsInertia=1\n", "IniCVar.ini", IniCVar, Error));
	FResolvedPhysicsConfig Later;
	REQUIRE(Resolve(Base, IniCVar, nullptr, "", Later, Error));
	CHECK_FALSE(FProfileResolver::ApplyConsoleVariables(Later, EngineVariables, ApplyError));
	CAPTURE(ApplyError);
	CHECK(ApplyError.find("p.ComNudgeAffectsInertia = '1' was not applied") != std::string::npos);
	CHECK(ApplyError.find("already set by Console") != std::string::npos);
	CHECK_FALSE(Variable->GetBool());
	Variable->ResetToDefault();
	CHECK(PhysicsCVars::GetComNudgeAffectsInertia());
	CHECK(FProfileResolver::ApplyConsoleVariables(Later, EngineVariables, ApplyError));   // from the default: applied
	CHECK(Variable->GetSetBy() == ECVF_SetByConsoleVariablesIni);
	Variable->ResetToDefault();

	CHECK_FALSE(Resolve(Base, UEParity, nullptr, "-ExecCmds=\"p.ComNudgeAffectsInertia\"", Config, Error));
	CHECK_FALSE(Resolve(Base, UEParity, nullptr, "-ExecCmds=\"p.ComNudgeAffectsInertia sometimes\"", Config, Error));
	CHECK_FALSE(Resolve(Base, UEParity, nullptr, "-ExecCmds=\"p.SensitiveSleepThresholdMultiplier inf\"", Config, Error));
	CHECK(Error.find("does not parse") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.HarnessCVarsAreAFixedTable")
{
	// The resolver owns exactly PhysicsCVars::GetHarnessConsoleVariables(): another variable this process registers
	// (here a temporary one, in the Automation exe also pt.Test.*) changes neither Values, nor the variant policy, nor
	// physics_sha256 (CTest PhysicsTest.Probe.HarnessCVars checks the same across two programs).
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	FResolvedPhysicsConfig Before;
	std::string Error;
	REQUIRE(Resolve(Base, Normalized, nullptr, "", Before, Error));
	for (const IConsoleVariable* Variable : PhysicsCVars::GetHarnessConsoleVariables())
	{
		const std::string VariableName = Variable->GetName();   // a const char* CAPTURE logs the pointer as 1
		CAPTURE(VariableName);
		const FResolvedConfigValue* Value = Before.Find(std::string("ConsoleVariables.") + Variable->GetName());
		REQUIRE(Value != nullptr);
		CHECK(Value->Value == Variable->GetDefaultString());
		CHECK(Value->Origin == EConfigOrigin::UEDefault);
		CHECK(Value->bPhysicsHash);
	}
	CHECK(PhysicsCVars::FindHarnessConsoleVariable("P.COMNUDGEAFFECTSINERTIA") != nullptr);   // case-insensitive
	CHECK(PhysicsCVars::FindHarnessConsoleVariable("pt.Test.Int") == nullptr);
	for (const auto& [Key, Value] : Before.Values)
	{
		CHECK_FALSE(Key.starts_with("ConsoleVariables.pt.Test."));
	}

	FResolvedPhysicsConfig After;
	{
		TAutoConsoleVariable<int32> ProcessLocal("p.Test.ProcessLocalPhysicsLike", 1, "registered for this test only");
		REQUIRE(Resolve(Base, Normalized, nullptr, "", After, Error));
		CHECK(After.Find("ConsoleVariables.p.Test.ProcessLocalPhysicsLike") == nullptr);
		CHECK(After.PhysicsSha256() == Before.PhysicsSha256());

		// To the resolver it is an engine CVar: a variant may set it, it is not hashed, and an odd value is not
		// validated here. ApplyConsoleVariables still sets it, because this process registers it.
		const FBackendVariant Variant = MakeVariant("[PhysicsTest.Variant]\nEngine=Chaos\nName=Local\n[ConsoleVariables]\np.Test.ProcessLocalPhysicsLike=5\n");
		REQUIRE(Resolve(Base, Normalized, &Variant, "", After, Error));
		CHECK_FALSE(After.Find("ConsoleVariables.p.Test.ProcessLocalPhysicsLike")->bPhysicsHash);
		CHECK(After.PhysicsSha256() == Before.PhysicsSha256());
		std::vector<std::pair<std::string, std::string>> EngineVariables;
		std::string ApplyError;
		CHECK(FProfileResolver::ApplyConsoleVariables(After, EngineVariables, ApplyError));
		CHECK(EngineVariables.empty());
		CHECK(ProcessLocal.GetValueOnGameThread() == 5);
	}
}

TEST_CASE("PhysicsTest.Profile.SectionNamesAreCaseInsensitive")
{
	// UE's FConfigFile keys sections by FString (case-insensitive); so does the harness, and the canonical keys keep
	// the canonical spelling.
	const FConfigFile Base = LoadBase();
	FPhysicsProfile Lower;
	std::string Error;
	REQUIRE(FPhysicsProfile::FromText("[physicstest.profile]\nName=Lower\n[/script/engine.physicssettings]\nDefaultGravityZ=-490\n"
		"[consolevariables]\np.comnudgeaffectsinertia=0\n", "Lower.ini", Lower, Error));
	CHECK(Lower.Name == "Lower");
	FResolvedPhysicsConfig Config;
	REQUIRE(Resolve(Base, Lower, nullptr, "", Config, Error));
	INFO(Error);
	CHECK(Config.Settings.DefaultGravityZ == -490.0f);
	CHECK(Config.Find("PhysicsSettings.DefaultGravityZ")->Origin == EConfigOrigin::PhysicsProfile);
	CHECK(Config.Find("ConsoleVariables.p.ComNudgeAffectsInertia")->Origin == EConfigOrigin::PhysicsProfile);
	CHECK(Config.Find("ConsoleVariables.p.ComNudgeAffectsInertia")->Value == "0");

	const FBackendVariant Variant = MakeVariant("[physicstest.variant]\nEngine=PhysX\nName=Lower\n[physicstest.backend.physx]\nSolverType=TGS\n"
		"[PHYSICSTEST.BACKEND.PHYSX.INIT]\nToleranceScale=(Length=100,Speed=1000)\n");
	REQUIRE(Resolve(Base, Lower, &Variant, "", Config, Error));
	CHECK(Config.SceneOptions == std::vector<std::pair<std::string, std::string>>{ { "SolverType", "TGS" } });
	CHECK(Config.Find("Backend.PhysX.SolverType") != nullptr);
	CHECK(Config.Find("Backend.PhysX.Init.ToleranceScale") != nullptr);
	CHECK_FALSE(Resolve(Base, Lower, nullptr, "-ini:PhysicsTest:[/SCRIPT/PHYSICSCORE.PHYSICSSETTINGSCORE]:DefaultGravityZ=1", Config, Error));
	CHECK(Error.find("DL44") != std::string::npos);
}

TEST_CASE("PhysicsTest.Profile.ProfilesApplyToTemplates")
{
	const FConfigFile Base = LoadBase();
	FResolvedPhysicsConfig UEParity, Normalized;
	std::string Error;
	REQUIRE(Resolve(Base, LoadProfile("UEParity"), nullptr, "", UEParity, Error));
	REQUIRE(Resolve(Base, LoadProfile("Normalized"), nullptr, "", Normalized, Error));

	FBodyInstance Template;
	FProfileResolver::ApplyBodyInstanceDefaults(Normalized, Template);
	CHECK(Template.LinearDamping == 0.0f);
	CHECK(Template.AngularDamping == 0.0f);
	CHECK_FALSE(Template.bInertiaConditioning);
	CHECK(Template.bOverrideMaxAngularVelocity);
	// Released with a finite 1e7 deg/s, not 0 (0 would clamp Chaos to rest) and not 3.4e38 (inf once squared in rad/s).
	CHECK(Template.MaxAngularVelocity == 1.0e7f);
	CHECK(std::isfinite(static_cast<float>(FMath::Square(FMath::DegreesToRadians(static_cast<double>(Template.MaxAngularVelocity))))));
	CHECK(Template.MassInKgOverride == 100.0f);      // fields outside [BodyInstanceDefaults] are untouched
	FProfileResolver::ApplyBodyInstanceDefaults(UEParity, Template);
	CHECK(Template.LinearDamping == 0.01f);
	CHECK(Template.bInertiaConditioning);
	CHECK_FALSE(Template.bOverrideMaxAngularVelocity);

	CHECK(UEParity.TestSettings.bAllowSleeping);
	CHECK_FALSE(Normalized.TestSettings.bAllowSleeping);
	CHECK(UEParity.TestSettings.RestitutionThresholdVelocity < 0.0f);
	CHECK(Normalized.TestSettings.RestitutionThresholdVelocity == 50.0f);
	CHECK(Normalized.Settings.FrictionCombineMode == EFrictionCombineMode::Average);

	FPhysicalMaterial Material;
	Material.Friction = 0.1f;
	FProfileResolver::ApplyMaterialDefaults(Normalized, Material);
	CHECK(Material.Friction == 0.7f);
	CHECK(Material.Density == 1.0f);
}

TEST_CASE("PhysicsTest.Profile.PhysicsShaSeparatesProfilesNotEngines")
{
	// physics_sha256 must be equal for the engines and variants of one table and differ between profiles (02 12.3).
	const FConfigFile Base = LoadBase();
	const FPhysicsProfile Normalized = LoadProfile("Normalized");
	const FBackendVariant PhysX = MakeVariant(PhysXVariantText);   // also sets the EngineDefault SubstepClampRule
	const FBackendVariant Jolt = MakeVariant(JoltVariantText);
	FResolvedPhysicsConfig A, B, C, D, E;
	std::string Error;
	REQUIRE(Resolve(Base, Normalized, &PhysX, "", A, Error));
	REQUIRE(Resolve(Base, Normalized, &Jolt, "", B, Error));
	REQUIRE(Resolve(Base, LoadProfile("UEParity"), &Jolt, "", C, Error));
	REQUIRE(Resolve(Base, Normalized, &Jolt, "-ini:PhysicsTest:[/Script/PhysicsTest.BodyInstanceDefaults]:LinearDamping=0.5", D, Error));
	REQUIRE(Resolve(Base, Normalized, &Jolt, "-ini:PhysicsTest:[/Script/PhysicsTest.PhysicsTestSettings]:NumThreads=4", E, Error));
	CHECK(A.PhysicsSha256() == B.PhysicsSha256());
	CHECK(A.PhysicsSha256() != C.PhysicsSha256());
	CHECK(B.PhysicsSha256() != D.PhysicsSha256());   // a command-line change of a pinned physics key
	CHECK(B.PhysicsSha256() == E.PhysicsSha256());   // run options are not physics
	CHECK(PhysX.Sha256() != Jolt.Sha256());          // variant_sha256 labels the series
	CHECK(A.PhysicsSha256().size() == 64u);
}

TEST_CASE("PhysicsTest.Profile.UnmodelledUEKeysAndTickSwitches")
{
	// A UE key the harness does not model (pasted from a DefaultEngine.ini) is kept with its origin and reported.
	const FConfigFile Base = LoadBase();
	FPhysicsProfile Pasted;
	std::string Error;
	REQUIRE(FPhysicsProfile::FromText("[PhysicsTest.Profile]\nName=Pasted\n[/Script/Engine.PhysicsSettings]\nDefaultTerminalVelocity=4000.000000\n"
		"bSubstepping=True\nMaxAngularVelocity=3600.000000\n", "Pasted.ini", Pasted, Error));
	FResolvedPhysicsConfig Config;
	REQUIRE(Resolve(Base, Pasted, nullptr, "", Config, Error));
	const FResolvedConfigValue* Terminal = Config.Find("PhysicsSettings.DefaultTerminalVelocity");
	REQUIRE(Terminal != nullptr);
	CHECK_FALSE(Terminal->bApplied);
	CHECK(Terminal->Origin == EConfigOrigin::PhysicsProfile);
	CHECK(Config.Find("PhysicsSettings.MaxAngularVelocity")->Value == "3600");   // UE's 6-decimal text normalises
	// bSubstepping contradicts TickMode = FixedStep: TickMode decides, with a warning.
	CHECK(std::any_of(Config.Warnings.begin(), Config.Warnings.end(), [](const std::string& W) { return W.find("bSubstepping") != std::string::npos; }));
	CHECK(std::any_of(Config.Warnings.begin(), Config.Warnings.end(), [](const std::string& W) { return W.find("DefaultTerminalVelocity") != std::string::npos; }));
	// Unknown struct fields are ignored with a warning (UE ImportText behaviour).
	FConfigFile Scene("Scene.ini");
	REQUIRE(Scene.ParseText("[/Script/Engine.PhysicsSettings]\nSolverOptions=(PositionIterations=4,ClusterConnectionFactor=1.0)\n", Error));
	REQUIRE(Resolve(Base, Pasted, nullptr, "", Config, Error, &Scene));
	CHECK(Config.Settings.SolverOptions.PositionIterations == 4);
	CHECK(Config.Settings.SolverOptions.VelocityIterations == 2);   // unlisted fields: struct default (UE semantics)
	CHECK(std::any_of(Config.Warnings.begin(), Config.Warnings.end(), [](const std::string& W) { return W.find("ClusterConnectionFactor") != std::string::npos; }));
}
