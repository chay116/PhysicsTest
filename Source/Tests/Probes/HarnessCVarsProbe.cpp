// HarnessCVarsProbe.cpp - CTest PhysicsTest.Probe.HarnessCVars (M1 fix round).
// A program that links PT::PhysicsCore for the profile resolver only - it never references BodyUtils, FBodyInstance's
// functions or FPhysScene, so the linker leaves those object files out of the static library (CheckHarnessCVars.cmake
// verifies that with the link map). The harness physics console variables must still exist and keep their policy:
//   1 p.ComNudgeAffectsInertia and p.SensitiveSleepThresholdMultiplier are registered
//   2 a backend variant may not set them (pinned key)
//   3 a malformed -ExecCmds value for them is a resolve error
//   4 they are part of physics_sha256; the hashes printed here must equal PhysicsTestAutomation's (the script compares)
// Exit code 0 = every check passed.
#include "CoreMinimal.h"

#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsConsoleVariables.h"
#include "Physics/PhysicsProfile.h"

#include <cstdio>
#include <string>

#ifndef PT_CONFIG_DIR
	#error "PT_CONFIG_DIR (the repository's Config/ directory) is defined by Source/Tests/Probes/CMakeLists.txt"
#endif

namespace
{
	using namespace PhysicsTest;

	int32 GFailures = 0;

	void Check(const bool bCondition, const char* What, const std::string& Detail = std::string())
	{
		std::printf("[%s] %s%s%s\n", bCondition ? "PASS" : "FAIL", What, Detail.empty() ? "" : ": ", Detail.c_str());
		GFailures += bCondition ? 0 : 1;
	}

	bool Resolve(const FConfigFile& Base, const FPhysicsProfile& Profile, const FBackendVariant* Variant, const char* CommandLine,
		FResolvedPhysicsConfig& Out, std::string& Error)
	{
		FProfileResolveInputs Inputs;
		Inputs.Base = &Base;
		Inputs.Profile = &Profile;
		Inputs.Variant = Variant;
		Inputs.CommandLine = CommandLine;
		return FProfileResolver::Resolve(Inputs, Out, Error);
	}
}

int main()
{
	FPlatformMisc::SuppressErrorDialogs();
	const std::string ConfigDir = PT_CONFIG_DIR;
	std::string Error;
	FConfigFile Base;
	FPhysicsProfile UEParity;
	FPhysicsProfile Normalized;
	if (!FConfigFile::LoadFile(ConfigDir + "/BasePhysicsTest.ini", Base, Error) || !FPhysicsProfile::LoadIni(ConfigDir + "/Profiles/UEParity.ini", UEParity, Error)
		|| !FPhysicsProfile::LoadIni(ConfigDir + "/Profiles/Normalized.ini", Normalized, Error))
	{
		std::printf("[FAIL] loading Config/: %s\n", Error.c_str());
		return 1;
	}

	// 1
	for (const char* Name : { "p.ComNudgeAffectsInertia", "p.SensitiveSleepThresholdMultiplier" })
	{
		Check(IConsoleManager::Get().FindConsoleVariable(Name) != nullptr && PhysicsCVars::FindHarnessConsoleVariable(Name) != nullptr,
			"registered without BodyUtils", Name);
	}

	// 2
	FBackendVariant Variant;
	if (!FBackendVariant::FromText("[PhysicsTest.Variant]\nEngine=Chaos\nName=Probe\n[ConsoleVariables]\np.ComNudgeAffectsInertia=0\n", "Probe.ini", Variant, Error))
	{
		std::printf("[FAIL] variant text: %s\n", Error.c_str());
		return 1;
	}
	FResolvedPhysicsConfig Config;
	bool bOk = Resolve(Base, Normalized, &Variant, "", Config, Error);
	Check(!bOk && Error.find("pinned key ConsoleVariables.p.ComNudgeAffectsInertia") != std::string::npos, "variant may not set a harness CVar", Error);

	// 3
	bOk = Resolve(Base, Normalized, nullptr, "-ExecCmds=\"p.ComNudgeAffectsInertia 7x\"", Config, Error);
	Check(!bOk && Error.find("does not parse") != std::string::npos, "malformed -ExecCmds value is rejected", Error);
	bOk = Resolve(Base, Normalized, nullptr, "-ExecCmds=\"p.SensitiveSleepThresholdMultiplier nan\"", Config, Error);
	Check(!bOk && Error.find("does not parse") != std::string::npos, "non-finite -ExecCmds value is rejected", Error);

	// 4
	for (const FPhysicsProfile* Profile : { &UEParity, &Normalized })
	{
		bOk = Resolve(Base, *Profile, nullptr, "", Config, Error);
		Check(bOk, "resolve", Profile->Name + (bOk ? std::string() : ": " + Error));
		if (!bOk)
		{
			continue;
		}
		const FResolvedConfigValue* Nudge = Config.Find("ConsoleVariables.p.ComNudgeAffectsInertia");
		const FResolvedConfigValue* Sensitive = Config.Find("ConsoleVariables.p.SensitiveSleepThresholdMultiplier");
		Check(Nudge != nullptr && Nudge->bPhysicsHash && Nudge->Value == "1", "p.ComNudgeAffectsInertia is a hashed physics key (1)", Profile->Name);
		Check(Sensitive != nullptr && Sensitive->bPhysicsHash && Sensitive->Value == "0.05", "p.SensitiveSleepThresholdMultiplier is a hashed physics key (0.05)",
			Profile->Name);
		std::printf("physics_sha256 %s=%s\n", Profile->Name.c_str(), Config.PhysicsSha256().c_str());
	}

	std::printf("HarnessCVarsProbe: %s (%d failure(s))\n", GFailures == 0 ? "PASS" : "FAIL", GFailures);
	return GFailures == 0 ? 0 : 1;
}
