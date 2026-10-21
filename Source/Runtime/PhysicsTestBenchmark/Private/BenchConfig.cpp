// BenchConfig.cpp - FBenchRunSpec::FromCommandLine, the -Instrument names, LoadBenchConfig and the default directories (see
// BenchmarkRunner.h).
#include "BenchmarkRunner.h"

#include "Containers/StringConv.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Physics/PhysicsProfile.h"
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <bit>
#include <system_error>

#ifndef PT_SOURCE_DIR
	#error "PT_SOURCE_DIR must name the repository root (Source/Runtime/PhysicsTestBenchmark/CMakeLists.txt)"
#endif

namespace PhysicsTest
{
	namespace
	{
		char ToLowerAscii(const char C)
		{
			return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}

		bool EqualsNoCase(const std::string& A, const char* B)
		{
			size_t Index = 0;
			for (; B[Index] != '\0'; ++Index)
			{
				if (Index >= A.size() || ToLowerAscii(A[Index]) != ToLowerAscii(B[Index]))
				{
					return false;
				}
			}
			return Index == A.size();
		}

		bool StartsWithNoCase(const std::string& Text, const char* Prefix)
		{
			size_t Index = 0;
			for (; Prefix[Index] != '\0'; ++Index)
			{
				if (Index >= Text.size() || ToLowerAscii(Text[Index]) != ToLowerAscii(Prefix[Index]))
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

		// A comma list ("a,b,c"); false when an item is empty.
		bool SplitList(const std::string& Text, std::vector<std::string>& Out)
		{
			size_t Start = 0;
			while (Start <= Text.size())
			{
				const size_t Comma = Text.find(',', Start);
				const std::string Item = Text.substr(Start, Comma == std::string::npos ? std::string::npos : Comma - Start);
				if (Item.empty())
				{
					return false;
				}
				Out.push_back(Item);
				if (Comma == std::string::npos)
				{
					break;
				}
				Start = Comma + 1;
			}
			return true;
		}

		struct FInstrumentName
		{
			const char* Name;
			uint32 Flag;
		};
		constexpr FInstrumentName GInstrumentNames[] = {
			{ "counters", PT_INSTR_COUNTERS },
			{ "phases", PT_INSTR_PHASES },
			{ "alloc", PT_INSTR_ALLOC },
			{ "contacts", PT_INSTR_CONTACTS },
			{ "native_debug_draw", PT_INSTR_NATIVE_DEBUG_DRAW },
		};
	}

	bool ParseInstrumentFlags(const std::string& Text, uint32& OutFlags, std::string& OutError)
	{
		OutFlags = 0;
		if (EqualsNoCase(Text, "none") || EqualsNoCase(Text, "0"))
		{
			return true;
		}
		std::vector<std::string> Names;
		if (!SplitList(Text, Names))
		{
			OutError = "-Instrument=" + Text + " has an empty tier name";
			return false;
		}
		for (const std::string& Name : Names)
		{
			bool bKnown = false;
			for (const FInstrumentName& Entry : GInstrumentNames)
			{
				if (EqualsNoCase(Name, Entry.Name))
				{
					OutFlags |= Entry.Flag;
					bKnown = true;
				}
			}
			if (!bKnown)
			{
				OutError = "-Instrument: unknown tier '" + Name + "' (none, counters, phases, alloc, contacts, native_debug_draw)";
				return false;
			}
		}
		return true;
	}

	std::vector<std::string> GetInstrumentNames(const uint32 Flags)
	{
		std::vector<std::string> Names;
		for (const FInstrumentName& Entry : GInstrumentNames)
		{
			if ((Flags & Entry.Flag) != 0)
			{
				Names.push_back(Entry.Name);
			}
		}
		return Names;
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
		std::string Text;
		// The run directory first: every later usage error is still written to <Out>/exit.json (M7).
		if (FParse::Value(Cmd, "Out=", Text))
		{
			Out.OutDir = StringConv::Utf8ToPath(Text);
		}
		Out.bList = FParse::Param(Cmd, "List");
		Out.bDescribe = FParse::Param(Cmd, "Describe");
		Out.bConformance = FParse::Param(Cmd, "Conformance");
		Out.bInjectFatal = FParse::Param(Cmd, "InjectFatal");
		Out.bInjectTeardownFatal = FParse::Param(Cmd, "InjectTeardownFatal");
		if (FParse::Value(Cmd, "SceneHashes=", Text))
		{
			Out.SceneHashesPath = StringConv::Utf8ToPath(Text);
		}
		// -Gates=G1,G9 (a comma list of physics gate ids; -Conformance only).
		if (FParse::Value(Cmd, "Gates=", Text, false) && !SplitList(Text, Out.Gates))
		{
			OutError = "-Gates=" + Text + " has an empty gate id";
			return false;
		}
		FParse::Value(Cmd, "Backend=", Out.Backend);
		FParse::Value(Cmd, "BackendProfile=", Out.BackendProfile);
		FParse::Value(Cmd, "Profile=", Out.PhysicsProfile);
		FParse::Value(Cmd, "Scenario=", Out.Scenario);
		FParse::Value(Cmd, "Session=", Out.Session);
		if (FParse::Value(Cmd, "ConfigDir=", Text))
		{
			Out.ConfigDir = StringConv::Utf8ToPath(Text);
		}
		// -RequireEffectiveKey=a.b,c.d: keys the run's preflight requires on top of the backend's (a comma list).
		if (FParse::Value(Cmd, "RequireEffectiveKey=", Text, false) && !SplitList(Text, Out.ExtraRequiredKeys))
		{
			OutError = "-RequireEffectiveKey=" + Text + " has an empty key";
			return false;
		}
		// Unsigned switches (decimal or 0x hex): present but malformed is an error.
		const auto ReadUnsigned32 = [Cmd, &OutError](const char* Key, uint32& Value, const bool bNonZero) -> bool
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;
			}
			uint32 Parsed = 0;
			if (!FParse::Value(Cmd, Key, Parsed) || (bNonZero && Parsed == 0))
			{
				OutError = std::string("-") + Key + Raw + " must be a" + (bNonZero ? " non-zero" : "") + " 32-bit unsigned integer (decimal or 0x hex)";
				return false;
			}
			Value = Parsed;
			return true;
		};
		if (!ReadUnsigned32("InjectEngineErrorBits=", Out.InjectEngineErrorBits, true) || !ReadUnsigned32("InjectSceneInstrumentFlags=", Out.InjectSceneInstrumentFlags, true))
		{
			return false;
		}
		if (Out.InjectSceneInstrumentFlags != 0 && (Out.InjectSceneInstrumentFlags & ~PT_INSTR_ALL) != 0)
		{
			OutError = "-InjectSceneInstrumentFlags: only PT_INSTR_* bits (0x1F)";
			return false;
		}
		FParse::Value(Cmd, "InjectPreflightFault=", Out.InjectPreflightFault);
		if (!Out.InjectPreflightFault.empty() && !EqualsNoCase(Out.InjectPreflightFault, "g0") && !StartsWithNoCase(Out.InjectPreflightFault, "g9:")
			&& !EqualsNoCase(Out.InjectPreflightFault, "listener") && !EqualsNoCase(Out.InjectPreflightFault, "drawer"))
		{
			OutError = "-InjectPreflightFault=" + Out.InjectPreflightFault + ": g0, g9:<READBACK key>, listener or drawer";
			return false;
		}
		{
			std::string Raw;
			if (FParse::Value(Cmd, "Affinity=", Raw))
			{
				uint64 Mask = 0;
				if (!FParse::Value(Cmd, "Affinity=", Mask) || Mask == 0)
				{
					OutError = "-Affinity=" + Raw + " must be a non-zero CPU mask (e.g. 0x15)";
					return false;
				}
				Out.AffinityMask = Mask;
			}
			if (FParse::Value(Cmd, "BlockSeed=", Raw) && !FParse::Value(Cmd, "BlockSeed=", Out.BlockSeed))
			{
				OutError = "-BlockSeed=" + Raw + " must be a 64-bit unsigned integer";
				return false;
			}
		}
		// Integer switches in a range: present but malformed or out of range is an error (FParse rejects "4x", "1.5").
		const auto ReadInt = [Cmd, &OutError](const char* Key, int32& Value, const int32 Min, const int32 Max) -> bool
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;   // absent
			}
			int32 Parsed = 0;
			if (!FParse::Value(Cmd, Key, Parsed) || Parsed < Min || Parsed > Max)
			{
				OutError = std::string("-") + Key + Raw + " must be an integer in [" + std::to_string(Min) + ", " + std::to_string(Max) + "]";
				return false;
			}
			Value = Parsed;
			return true;
		};
		const auto ReadOptionalInt = [Cmd, &ReadInt](const char* Key, std::optional<int32>& Value, const int32 Min, const int32 Max) -> bool
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;
			}
			int32 Parsed = 0;
			if (!ReadInt(Key, Parsed, Min, Max))
			{
				return false;
			}
			Value = Parsed;
			return true;
		};
		if (!ReadInt("Frames=", Out.Frames, 1, 10000000) || !ReadInt("Threads=", Out.Threads, 1, 64) || !ReadInt("Repeat=", Out.RepeatIndex, 0, 1000000)
			|| !ReadInt("Order=", Out.OrderIndex, 0, 1000000) || !ReadInt("Runs=", Out.GateRuns, 2, 100)
			|| !ReadOptionalInt("HashEvery=", Out.HashEvery, 0, 10000000) || !ReadOptionalInt("StatesEvery=", Out.StatesEvery, 0, 10000000)
			|| !ReadOptionalInt("CountersEvery=", Out.CountersEvery, 0, 10000000) || !ReadOptionalInt("CaptureEvery=", Out.CaptureEvery, 0, 10000000)
			|| !ReadInt("InjectStatePerturbation=", Out.InjectStatePerturbationFrame, 0, 10000000)
			|| !ReadInt("InjectGateFault=", Out.InjectGateFaultFrame, 0, 10000000))
		{
			return false;
		}
		const auto ReadSeconds = [Cmd, &OutError](const char* Key, double& Value, const double Max) -> bool
		{
			std::string Raw;
			if (!FParse::Value(Cmd, Key, Raw))
			{
				return true;
			}
			double Parsed = 0.0;
			if (!FParse::Value(Cmd, Key, Parsed) || Parsed < 0.0 || Parsed > Max)
			{
				OutError = std::string("-") + Key + Raw + " must be a number in [0, " + std::to_string(Max) + "]";
				return false;
			}
			Value = Parsed;
			return true;
		};
		if (!ReadSeconds("PreRoll=", Out.PreRollSeconds, 60.0) || !ReadSeconds("ContaminationPct=", Out.ContaminationPct, 10000.0)
			|| !ReadSeconds("BusyHelper=", Out.BusyHelperSeconds, 600.0))
		{
			return false;
		}
		if (FParse::Value(Cmd, "RunMode=", Text))
		{
			if (EqualsNoCase(Text, "Bench"))
			{
				Out.RunMode = EPhysicsRunMode::Bench;
			}
			else if (EqualsNoCase(Text, "Behavior") || EqualsNoCase(Text, "Behaviour"))
			{
				Out.RunMode = EPhysicsRunMode::Behavior;
			}
			else if (EqualsNoCase(Text, "Viz"))
			{
				Out.RunMode = EPhysicsRunMode::Viz;
			}
			else
			{
				OutError = "-RunMode=" + Text + ": Bench, Behavior or Viz";
				return false;
			}
		}
		if (FParse::Value(Cmd, "Instrument=", Text, false))
		{
			uint32 Flags = 0;
			if (!ParseInstrumentFlags(Text, Flags, OutError))
			{
				return false;
			}
			Out.InstrumentFlags = Flags;
		}
		if (FParse::Value(Cmd, "HostCheck=", Text))
		{
			if (EqualsNoCase(Text, "enforce"))
			{
				Out.HostCheck = EHostCheckPolicy::Enforce;
			}
			else if (EqualsNoCase(Text, "record"))
			{
				Out.HostCheck = EHostCheckPolicy::Record;
			}
			else
			{
				OutError = "-HostCheck=" + Text + ": enforce or record";
				return false;
			}
		}
		if (FParse::Value(Cmd, "Gate=", Text))
		{
			if (!EqualsNoCase(Text, "G8") && !EqualsNoCase(Text, "G11"))
			{
				OutError = "-Gate=" + Text + ": G8 (DET1) or G11 (multi-process gates; the M6 physics gates run with -Conformance -Gates=)";
				return false;
			}
			Out.MultiRunGate = EqualsNoCase(Text, "G8") ? "G8" : "G11";
		}
		if (!ParseScenarioParams(Cmd, Out.Params, OutError))
		{
			return false;
		}
		const bool bSceneHashes = !Out.SceneHashesPath.empty();
		const bool bBusyHelper = Out.BusyHelperSeconds > 0.0;
		if (!Out.bList && !bSceneHashes && !bBusyHelper && Out.Backend.empty())
		{
			OutError = "-Backend=<name> is required (see -List)";
			return false;
		}
		if (!Out.Gates.empty() && !Out.bConformance)
		{
			OutError = "-Gates= selects physics gates for -Conformance only";
			return false;
		}
		if (!Out.bList && !Out.bDescribe && !Out.bConformance && !bSceneHashes && !bBusyHelper && Out.Scenario.empty())
		{
			OutError = "-Scenario=<ID.Name> is required (see -List)";
			return false;
		}
		if (Out.AffinityMask != 0 && !bBusyHelper && static_cast<int32>(std::popcount(Out.AffinityMask)) != Out.Threads)
		{
			OutError = "-Affinity names " + std::to_string(std::popcount(Out.AffinityMask)) + " CPUs but -Threads=" + std::to_string(Out.Threads)
				+ " (T is the number of logical CPUs in the mask, 02 9.6)";
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
		// Run options from the command line (not physics keys: excluded from physics_sha256, 02 10). The manifest's run group
		// records the values the run used; profile.resolved keeps what the ini files said.
		FPhysicsTestSettings& Test = OutConfig.TestSettings;
		Test.NumThreads = Spec.Threads;
		if (Spec.AffinityMask != 0)
		{
			Test.AffinityMask = Spec.AffinityMask;
		}
		if (Spec.RunMode)
		{
			Test.RunMode = *Spec.RunMode;
		}
		if (Spec.InstrumentFlags)
		{
			Test.InstrumentFlags = *Spec.InstrumentFlags;
		}
		if (Spec.HashEvery)
		{
			Test.HashEvery = *Spec.HashEvery;
		}
		if (Spec.CountersEvery)
		{
			Test.CountersEvery = *Spec.CountersEvery;
		}
		if (Spec.CaptureEvery)
		{
			Test.CaptureEvery = *Spec.CaptureEvery;
		}
		return true;
	}
}
