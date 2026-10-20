// PhysicsProfile.h - physics profiles, backend variants and the resolver (02_Architecture 10, DL18, DL44).
// [MVP M1 minimal: layers, origins, variant key restriction, -ini: / -ExecCmds; 확장 M9: ForcedOverrides, preflight diff]
//
// Layers, lowest first (02 10):
//   built-in defaults   the code defaults of FPhysicsSettings / FPhysicsTestSettings / FBodyInstance / FPhysicalMaterial
//                       and the harness physics console variables (Physics/PhysicsConsoleVariables.h; Origin
//                       UEDefault, "built-in")
//   Base                Config/BasePhysicsTest.ini: every UE default under [/Script/Engine.PhysicsSettings] as a UE
//                       game stores it (BaseEngine.ini:3406 [V]); harness keys under [/Script/PhysicsTest.*]  (UEDefault)
//   Profile             Config/Profiles/<Name>.ini (UEParity, Normalized)                                    (PhysicsProfile)
//   Backend variant     Config/Profiles/Backend/<Engine>.<Variant>.ini: its own [PhysicsTest.Backend.<Engine>] keys
//                       and the keys the profile marks EngineDefault - anything else is a load error, so a variant
//                       cannot silently undo a normalisation                                                  (BackendVariant)
//   Scene-pinned        values a scenario fixes (M3)                                                          (ScenePinned)
//   Command line        -ini:PhysicsTest:[Section]:Key=Value,... (also -ini:Engine:, the UE spelling for physics
//                       settings; UE syntax, ConfigCacheIni.cpp:2413-2500) and -ExecCmds="cvar value, ..."  (CommandLine)
//
// Sections (names and keys compare case-insensitively, as in UE):
//   [/Script/Engine.PhysicsSettings]              UE keys. A UE key the harness does not use is kept (Origin tracked,
//                                                 bApplied = false, warning) so a project's DefaultEngine.ini can be
//                                                 pasted. [/Script/PhysicsCore.PhysicsSettingsCore] is a load error (DL44).
//   [/Script/PhysicsTest.PhysicsTestSettings]     FPhysicsTestSettings (unknown keys are errors)
//   [/Script/PhysicsTest.BodyInstanceDefaults]    applied to body templates BEFORE authoring (ApplyBodyInstanceDefaults)
//   [/Script/PhysicsTest.PhysicalMaterialDefaults] applied to material templates before authoring
//   [ConsoleVariables]                            harness physics CVars (PhysicsCVars::GetHarnessConsoleVariables():
//                                                 validated, pinned, hashed) and engine CVars passed through to the
//                                                 backend (Chaos SetOption, M13). Which other CVars a program happens to
//                                                 register never changes the resolved config.
//   [PhysicsTest.Backend.<Engine>] / [...<Engine>.Init]  engine keys -> SceneOptions / InitOptions (validated by the
//                                                 backend, M2+)
//   [PhysicsTest.Profile] / [PhysicsTest.Variant] metadata of the profile / variant file itself
//   [/Script/PhysicsTest.ForcedOverrides]          M9 (a load error until then)
// Canonical value keys are "<Prefix>.<Key>": PhysicsSettings., PhysicsTestSettings., BodyInstanceDefaults.,
// PhysicalMaterialDefaults., ConsoleVariables.<cvar>, Backend.<Engine>. and Backend.<Engine>.Init.
// Scalar keys accept plain Key=Value only; the array operators (+ . - !) are for the metadata arrays.
#pragma once

#include "Misc/ConfigCacheIni.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PhysicsTest
{
	enum class EConfigOrigin : uint8
	{
		UEDefault,        // built-in default or BasePhysicsTest.ini
		PhysicsProfile,
		BackendVariant,
		ScenePinned,
		CommandLine,
		ForcedOverride,   // M9
	};

	[[nodiscard]] const char* ToString(EConfigOrigin Origin);

	// Whether a backend variant may set a key (only the physics profile decides; unlisted keys are Pinned).
	enum class EProfileKeyPolicy : uint8
	{
		Pinned,
		EngineDefault,
	};

	namespace ConfigSections
	{
		inline constexpr const char* PhysicsSettings = "/Script/Engine.PhysicsSettings";
		inline constexpr const char* PhysicsTestSettings = "/Script/PhysicsTest.PhysicsTestSettings";
		inline constexpr const char* BodyInstanceDefaults = "/Script/PhysicsTest.BodyInstanceDefaults";
		inline constexpr const char* PhysicalMaterialDefaults = "/Script/PhysicsTest.PhysicalMaterialDefaults";
		inline constexpr const char* ConsoleVariables = "ConsoleVariables";
		inline constexpr const char* ForcedOverrides = "/Script/PhysicsTest.ForcedOverrides";
		inline constexpr const char* PhysicsSettingsCore = "/Script/PhysicsCore.PhysicsSettingsCore";   // never valid (DL44)
		inline constexpr const char* ProfileMeta = "PhysicsTest.Profile";
		inline constexpr const char* VariantMeta = "PhysicsTest.Variant";
		inline constexpr const char* BackendPrefix = "PhysicsTest.Backend.";
	}

	// One resolved value with its provenance (A.10 calls it FConfigValue; renamed because Core's ini already has UE's
	// FConfigValue).
	struct FResolvedConfigValue
	{
		std::string Value;        // canonical text (re-exported after parsing, so formatting does not matter)
		std::string Unit;
		EConfigOrigin Origin = EConfigOrigin::UEDefault;
		std::string OriginName;   // "Config/Profiles/Normalized.ini:23", "-ini:", "-ExecCmds", "built-in"
		bool bApplied = true;     // false: a UE key the harness does not model, or an engine key of another engine
		bool bPhysicsHash = false;   // part of physics_sha256 (see FResolvedPhysicsConfig::PhysicsSha256)
	};

	// Config/Profiles/<Name>.ini. Metadata: [PhysicsTest.Profile] Name=, Version=, +EngineDefaultKeys=<canonical key>.
	struct FPhysicsProfile
	{
		std::string Name;
		int32 Version = 1;
		FConfigFile Ini;
		std::map<std::string, EProfileKeyPolicy, std::less<>> KeyPolicy;   // canonical key -> EngineDefault (others Pinned)

		static bool LoadIni(const std::string& Utf8Path, FPhysicsProfile& Out, std::string& OutError);
		static bool FromText(std::string_view Text, const std::string& DisplayName, FPhysicsProfile& Out, std::string& OutError);
		[[nodiscard]] EProfileKeyPolicy GetKeyPolicy(std::string_view CanonicalKey) const;
		[[nodiscard]] std::string Sha256() const;   // of the canonical ini text
	};

	// Config/Profiles/Backend/<Engine>.<Variant>.ini. Metadata: [PhysicsTest.Variant] Engine=, Name=, Label=,
	// +FidelityNotes=. Keys carry [V]/[M]/[R] evidence tags in comments.
	struct FBackendVariant
	{
		std::string Engine;   // "PhysX", "Jolt", "Bullet", "Chaos", "Null"
		std::string Name;     // "UE4Compat", ...
		std::string Label;    // e.g. "UE4 settings on PhysX 5.11"
		FConfigFile Ini;
		std::vector<std::string> FidelityNotes;

		static bool LoadIni(const std::string& Utf8Path, FBackendVariant& Out, std::string& OutError);
		static bool FromText(std::string_view Text, const std::string& DisplayName, FBackendVariant& Out, std::string& OutError);
		[[nodiscard]] std::string Sha256() const;   // variant_sha256
	};

	struct FProfileResolveInputs
	{
		const FConfigFile* Base = nullptr;            // required
		const FPhysicsProfile* Profile = nullptr;     // required
		const FBackendVariant* Variant = nullptr;     // optional
		const FConfigFile* ScenePinned = nullptr;     // optional (M3 scenarios)
		std::string CommandLine;                      // scanned for -ini:PhysicsTest: / -ini:Engine: and -ExecCmds=
	};

	struct FResolvedPhysicsConfig
	{
		std::string ProfileName;
		std::string Engine;          // the variant's engine ("" without a variant)
		std::string VariantName;
		std::map<std::string, FResolvedConfigValue, std::less<>> Values;   // canonical key -> value
		FPhysicsSettings Settings;
		FPhysicsTestSettings TestSettings;
		FBodyInstance BodyInstanceDefaults;     // only the [BodyInstanceDefaults] fields are meaningful
		FPhysicalMaterial MaterialDefaults;     // only the [PhysicalMaterialDefaults] fields are meaningful
		// Process-wide (InitGamePhys) and per-scene engine keys of Engine, and every [ConsoleVariables] entry in order.
		std::vector<std::pair<std::string, std::string>> InitOptions;
		std::vector<std::pair<std::string, std::string>> SceneOptions;
		std::vector<std::pair<std::string, std::string>> ConsoleVariables;
		std::vector<std::string> Warnings;

		[[nodiscard]] const FResolvedConfigValue* Find(std::string_view CanonicalKey) const;
		// physics_sha256 (02 12.3): every applied physics key the profile pins - EngineDefault keys, engine keys,
		// run options (threads, run mode, hashing cadence) and unused UE keys are excluded, so it is equal across the
		// engines and variants of one table.
		[[nodiscard]] std::string PhysicsSha256() const;
	};

	class FProfileResolver
	{
	public:
		static bool Resolve(const FProfileResolveInputs& Inputs, FResolvedPhysicsConfig& Out, std::string& OutError);

		// Copies the resolved [BodyInstanceDefaults] / [PhysicalMaterialDefaults] fields onto a template (before authoring).
		static void ApplyBodyInstanceDefaults(const FResolvedPhysicsConfig& Config, FBodyInstance& InOutTemplate);
		static void ApplyMaterialDefaults(const FResolvedPhysicsConfig& Config, FPhysicalMaterial& InOutTemplate);
		// Sets every Config.ConsoleVariables entry that names a console variable registered in this process (the harness
		// physics table plus e.g. viewer variables) - ini values with ECVF_SetByConsoleVariablesIni, -ExecCmds with
		// ECVF_SetByConsole, as UE executes them as console commands. OutEngineVariables gets the other entries (they
		// are passed to the engine). Returns false, after trying every entry, when a variable rejected its value
		// (unparsable, read-only, or already set with a higher priority, e.g. by -ExecCmds of an earlier resolve);
		// OutError names each one with its current value and SetBy.
		static bool ApplyConsoleVariables(const FResolvedPhysicsConfig& Config, std::vector<std::pair<std::string, std::string>>& OutEngineVariables,
			std::string& OutError);

		// "<Prefix>.<Key>" for a section/key pair ("" when the section has no canonical prefix).
		[[nodiscard]] static std::string MakeCanonicalKey(std::string_view Section, std::string_view Key);
		// Canonical keys of every modelled setting with the exported code default (BasePhysicsTest.ini must agree).
		[[nodiscard]] static std::vector<std::pair<std::string, std::string>> GetBuiltInDefaults();
		// Parses Text as the value of a modelled key and returns its canonical text.
		static bool NormalizeValue(std::string_view CanonicalKey, std::string_view Text, std::string& OutCanonical, std::string& OutError);
	};
}
