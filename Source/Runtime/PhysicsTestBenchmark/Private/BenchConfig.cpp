// BenchConfig.cpp - FBenchRunSpec::FromCommandLine, LoadBenchConfig and the default directories (see BenchmarkRunner.h).
#include "BenchmarkRunner.h"

#include "Containers/StringConv.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Physics/PhysicsProfile.h"

#include <system_error>

#ifndef PT_SOURCE_DIR
	#error "PT_SOURCE_DIR must name the repository root (Source/Runtime/PhysicsTestBenchmark/CMakeLists.txt)"
#endif

namespace PhysicsTest
{
	namespace
	{
		bool StartsWithNoCase(const std::string& Text, const char* Prefix)
		{
			size_t Index = 0;
			for (; Prefix[Index] != '\0'; ++Index)
			{
				if (Index >= Text.size())
				{
					return false;
				}
				const char A = (Text[Index] >= 'A' && Text[Index] <= 'Z') ? static_cast<char>(Text[Index] - 'A' + 'a') : Text[Index];
				const char B = (Prefix[Index] >= 'A' && Prefix[Index] <= 'Z') ? static_cast<char>(Prefix[Index] - 'A' + 'a') : Prefix[Index];
				if (A != B)
				{
					return false;
				}
			}
			return true;
		}

		std::string StripQuotes(std::string Text)
		{
			if (Text.size() >= 2 && Text.front() == '"' && Text.back() == '"')
			{
				Text = Text.substr(1, Text.size() - 2);
			}
			return Text;
		}
	}

	std::filesystem::path GetDefaultConfigDir()
	{
		return StringConv::Utf8ToPath(PT_SOURCE_DIR).make_preferred() / "Config";
	}

	std::filesystem::path GetDefaultResultsDir()
	{
		return StringConv::Utf8ToPath(PT_SOURCE_DIR).make_preferred() / "Saved" / "Results";
	}

	bool FBenchRunSpec::ParseScenarioParams(const char* CommandLine, FScenarioParams& OutParams, std::string& OutError)
	{
		// -Param:Name=Value tokens (repeatable; the value may be quoted).
		const char* Cursor = CommandLine != nullptr ? CommandLine : "";
		std::string Token;
		while (FParse::Token(Cursor, Token))
		{
			if (!StartsWithNoCase(Token, "-Param:") && !StartsWithNoCase(Token, "/Param:"))
			{
				continue;
			}
			const std::string Assignment = Token.substr(7);
			const size_t Equals = Assignment.find('=');
			if (Equals == std::string::npos || Equals == 0)
			{
				OutError = "malformed " + Token + " (expected -Param:Name=Value)";
				return false;
			}
			OutParams[Assignment.substr(0, Equals)] = StripQuotes(Assignment.substr(Equals + 1));
		}
		return true;
	}

	bool FBenchRunSpec::FromCommandLine(const char* CommandLine, FBenchRunSpec& Out, std::string& OutError)
	{
		Out = FBenchRunSpec();
		Out.CommandLine = CommandLine != nullptr ? CommandLine : "";
		const char* Cmd = Out.CommandLine.c_str();
		Out.bList = FParse::Param(Cmd, "List");
		Out.bDescribe = FParse::Param(Cmd, "Describe");
		FParse::Value(Cmd, "Backend=", Out.Backend);
		FParse::Value(Cmd, "BackendProfile=", Out.BackendProfile);
		FParse::Value(Cmd, "Profile=", Out.PhysicsProfile);
		FParse::Value(Cmd, "Scenario=", Out.Scenario);
		FParse::Value(Cmd, "Session=", Out.Session);
		std::string Text;
		if (FParse::Value(Cmd, "Out=", Text))
		{
			Out.OutDir = StringConv::Utf8ToPath(Text);
		}
		if (FParse::Value(Cmd, "ConfigDir=", Text))
		{
			Out.ConfigDir = StringConv::Utf8ToPath(Text);
		}
		// -RequireEffectiveKey=a.b,c.d: keys the run's preflight requires on top of the backend's (a comma list).
		if (FParse::Value(Cmd, "RequireEffectiveKey=", Text, false))
		{
			size_t Start = 0;
			while (Start <= Text.size())
			{
				const size_t Comma = Text.find(',', Start);
				const std::string Key = Text.substr(Start, Comma == std::string::npos ? std::string::npos : Comma - Start);
				if (Key.empty())
				{
					OutError = "-RequireEffectiveKey=" + Text + " has an empty key";
					return false;
				}
				Out.ExtraRequiredKeys.push_back(Key);
				if (Comma == std::string::npos)
				{
					break;
				}
				Start = Comma + 1;
			}
		}
		// -InjectEngineErrorBits=0x...: a test-only switch (decimal or 0x hex), recorded in the manifest.
		{
			std::string Raw;
			if (FParse::Value(Cmd, "InjectEngineErrorBits=", Raw))
			{
				uint32 Bits = 0;
				if (!FParse::Value(Cmd, "InjectEngineErrorBits=", Bits) || Bits == 0)
				{
					OutError = "-InjectEngineErrorBits=" + Raw + " must be a non-zero 32-bit unsigned integer (decimal or 0x hex)";
					return false;
				}
				Out.InjectEngineErrorBits = Bits;
			}
		}
		// Numeric switches: present but malformed is an error (FParse rejects "4x", "1.5", signs on unsigned values).
		const auto ReadPositive = [Cmd, &OutError](const char* Key, int32& Value, const int32 Max) -> bool
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;   // absent
			}
			int32 Parsed = 0;
			if (!FParse::Value(Cmd, Key, Parsed) || Parsed < 1 || Parsed > Max)
			{
				OutError = std::string("-") + Key + Raw + " must be an integer in [1, " + std::to_string(Max) + "]";
				return false;
			}
			Value = Parsed;
			return true;
		};
		if (!ReadPositive("Frames=", Out.Frames, 10000000) || !ReadPositive("Threads=", Out.Threads, 64))
		{
			return false;
		}
		if (!ParseScenarioParams(Cmd, Out.Params, OutError))
		{
			return false;
		}
		if (!Out.bList && Out.Backend.empty())
		{
			OutError = "-Backend=<name> is required (see -List)";
			return false;
		}
		if (!Out.bList && !Out.bDescribe && Out.Scenario.empty())
		{
			OutError = "-Scenario=<ID.Name> is required (see -List)";
			return false;
		}
		return true;
	}

	bool LoadBenchConfig(const FBenchRunSpec& Spec, FResolvedPhysicsConfig& OutConfig, FBenchConfig& OutFiles, std::string& OutError)
	{
		const std::filesystem::path ConfigDir = Spec.ConfigDir.empty() ? GetDefaultConfigDir() : std::filesystem::path(Spec.ConfigDir).make_preferred();
		FConfigFile Base;
		std::string Error;
		if (!FConfigFile::LoadFile(StringConv::PathToUtf8(ConfigDir / "BasePhysicsTest.ini"), Base, Error))
		{
			OutError = "BasePhysicsTest.ini: " + Error;
			return false;
		}
		OutFiles.ProfilePath = ConfigDir / "Profiles" / (Spec.PhysicsProfile + ".ini");
		FPhysicsProfile Profile;
		if (!FPhysicsProfile::LoadIni(StringConv::PathToUtf8(OutFiles.ProfilePath), Profile, Error))
		{
			OutError = "physics profile '" + Spec.PhysicsProfile + "': " + Error;
			return false;
		}
		OutFiles.ProfileSha256 = Profile.Sha256();
		// The backend variant: the given name, else <Backend>.Default when that file exists, else the one <Backend>.*.ini whose
		// [PhysicsTest.Variant] says DefaultForEngine=True (M5 round 0: PhysX.UE4Compat, so that every PhysX run carries a
		// variant label and fidelity notes, 02 8.2), else none.
		FBackendVariant Variant;
		bool bHasVariant = false;
		std::string VariantName = Spec.BackendProfile;
		std::error_code Ignored;
		if (VariantName.empty() && std::filesystem::exists(ConfigDir / "Profiles" / "Backend" / (Spec.Backend + ".Default.ini"), Ignored))
		{
			VariantName = "Default";
		}
		if (VariantName.empty() && !Spec.Backend.empty())
		{
			const std::string Prefix = Spec.Backend + ".";
			for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(ConfigDir / "Profiles" / "Backend", Ignored))
			{
				const std::string Stem = StringConv::PathToUtf8(Entry.path().stem());
				if (Entry.path().extension() != ".ini" || Stem.size() <= Prefix.size() || Stem.compare(0, Prefix.size(), Prefix) != 0)
				{
					continue;
				}
				FBackendVariant Candidate;
				if (!FBackendVariant::LoadIni(StringConv::PathToUtf8(Entry.path()), Candidate, Error))
				{
					OutError = "backend variant '" + Stem + "': " + Error;
					return false;
				}
				if (!Candidate.bDefaultForEngine || Candidate.Engine != Spec.Backend)
				{
					continue;
				}
				if (!VariantName.empty())
				{
					OutError = "backend variants '" + Spec.Backend + "." + VariantName + "' and '" + Stem + "' both say DefaultForEngine=True";
					return false;
				}
				VariantName = Stem.substr(Prefix.size());
			}
		}
		if (!VariantName.empty())
		{
			OutFiles.VariantPath = ConfigDir / "Profiles" / "Backend" / (Spec.Backend + "." + VariantName + ".ini");
			if (!FBackendVariant::LoadIni(StringConv::PathToUtf8(OutFiles.VariantPath), Variant, Error))
			{
				OutError = "backend variant '" + Spec.Backend + "." + VariantName + "': " + Error;
				return false;
			}
			bHasVariant = true;
			OutFiles.VariantName = VariantName;
			OutFiles.VariantLabel = Variant.Label;
			OutFiles.FidelityNotes = Variant.FidelityNotes;
			OutFiles.VariantSha256 = Variant.Sha256();
		}
		FProfileResolveInputs Inputs;
		Inputs.Base = &Base;
		Inputs.Profile = &Profile;
		Inputs.Variant = bHasVariant ? &Variant : nullptr;
		Inputs.CommandLine = Spec.CommandLine;
		if (!FProfileResolver::Resolve(Inputs, OutConfig, Error))
		{
			OutError = "profile resolution: " + Error;
			return false;
		}
		if (bHasVariant && OutConfig.Engine != Spec.Backend)
		{
			OutError = "backend variant '" + StringConv::PathToUtf8(OutFiles.VariantPath) + "' is for engine '" + OutConfig.Engine + "', not '" + Spec.Backend + "'";
			return false;
		}
		// Run options from the command line (not physics keys: excluded from physics_sha256, 02 10).
		OutConfig.TestSettings.NumThreads = Spec.Threads;
		return true;
	}
}
