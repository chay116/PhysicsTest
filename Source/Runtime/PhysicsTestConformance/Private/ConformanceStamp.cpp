// ConformanceStamp.cpp - the conformance stamp writer and checker (see ConformanceStamp.h).
#include "ConformanceStamp.h"

#include "BenchCore.h"
#include "Containers/StringConv.h"
#include "Hash/Sha256.h"
#include "Misc/Parse.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace PhysicsTest
{
	namespace
	{
		using FJson = nlohmann::json;              // std::map objects: keys sorted (the canonical form)
		using FOrderedJson = nlohmann::ordered_json;

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

		bool WriteFileAtomic(const std::filesystem::path& Path, const std::string& Text)
		{
			std::filesystem::path Temporary = Path;
			Temporary += L".tmp";
			{
				std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
				File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
				File.flush();
				if (!File)
				{
					return false;
				}
			}
			return MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
		}

		const char* GateResultName(const EGateResult Result)
		{
			switch (Result)
			{
			case EGateResult::Pass: return "pass";
			case EGateResult::Fail: return "fail";
			default: return "skip";
			}
		}
	}

	std::string FConformanceStampKey::ToCanonicalJson() const
	{
		FJson Key = FJson::object();
		Key["bench_exe_sha256"] = BenchExeSha256;
		Key["backend_module_sha256"] = BackendModuleSha256;
		Key["variant_sha256"] = VariantSha256;
		Key["physics_sha256"] = PhysicsSha256;
		Key["tolerances_sha256"] = TolerancesSha256;
		Key["gate_suite_version"] = GateSuiteVersion;
		Key["config_overrides"] = ConfigOverrides;
		return Key.dump();   // compact, keys sorted, UTF-8 as is
	}

	std::string FConformanceStampKey::ComputeSha256() const
	{
		return FSHA256::HashString(ToCanonicalJson());
	}

	std::filesystem::path FConformanceStampKey::ToPath(const std::filesystem::path& StampDir) const
	{
		return StampDir / (ComputeSha256() + ".json");
	}

	std::vector<std::string> ExtractConfigOverrides(const std::string& CommandLine)
	{
		std::vector<std::string> Out;
		const char* Cursor = CommandLine.c_str();
		std::string Token;
		while (FParse::Token(Cursor, Token))
		{
			if (Token.size() < 2 || (Token[0] != '-' && Token[0] != '/'))
			{
				continue;
			}
			const std::string Body = Token.substr(1);
			if (!StartsWithNoCase(Body, "ini:") && !StartsWithNoCase(Body, "execcmds="))
			{
				continue;
			}
			std::string Clean;
			Clean.reserve(Token.size());
			Clean += '-';
			for (const char C : Body)
			{
				if (C != '"')
				{
					Clean += C;
				}
			}
			Out.push_back(Clean);
		}
		std::sort(Out.begin(), Out.end());
		return Out;
	}

	bool WriteConformanceStamp(const FStampDocument& Document, const std::filesystem::path& StampDir, std::filesystem::path& OutPath, std::string& OutError)
	{
		std::error_code DirError;
		std::filesystem::create_directories(StampDir, DirError);
		if (DirError)
		{
			OutError = "cannot create the stamp directory '" + StringConv::PathToUtf8(StampDir) + "': " + DirError.message();
			return false;
		}
		const std::string KeySha = Document.Key.ComputeSha256();
		int32 Failed = 0;
		int32 Skipped = 0;
		FOrderedJson Gates = FOrderedJson::array();
		for (const FStampGate& Gate : Document.Gates)
		{
			Failed += Gate.Result == EGateResult::Fail ? 1 : 0;
			Skipped += Gate.Result == EGateResult::Skip ? 1 : 0;
			Gates.push_back({ { "id", Gate.Id }, { "result", GateResultName(Gate.Result) }, { "reason", Gate.Reason }, { "failures", Gate.Failures } });
		}
		const bool bPassed = Failed == 0 && !Document.Gates.empty();
		const bool bValid = bPassed && Document.PendingSignOffs == 0;
		FOrderedJson Json;
		Json["schema"] = "physicstest.stamp/1";
		Json["stamp_key_sha256"] = KeySha;
		Json["valid"] = bValid;
		Json["result"] = bPassed ? "pass" : "fail";
		Json["invalid_reason"] = bValid ? std::string() : (!bPassed ? std::to_string(Failed) + " gate(s) failed" : std::to_string(Document.PendingSignOffs)
			+ " verdict(s) rest on a post-run registration the lead has not signed (pending_sign_offs)");
		Json["pending_sign_offs"] = Document.PendingSignOffs;
		Json["summary"] = { { "gates", Document.Gates.size() }, { "failed", Failed }, { "skipped", Skipped } };
		// The key exactly as hashed (FConformanceStampKey::ToCanonicalJson), then its components for reading.
		Json["key_canonical"] = Document.Key.ToCanonicalJson();
		Json["key"] = FOrderedJson::parse(Document.Key.ToCanonicalJson());
		Json["target"] = { { "backend", Document.Backend }, { "variant_requested", Document.VariantRequested }, { "variant", Document.Variant },
			{ "profile", Document.Profile } };
		Json["build"] = { { "exe", StringConv::PathToUtf8(Document.ExePath.filename()) }, { "exe_path", StringConv::PathToUtf8(Document.ExePath) },
			{ "preset", Document.Preset }, { "flag_profile", Document.FlagProfile }, { "configuration", Document.Configuration }, { "git_sha", Document.GitSha },
			{ "git_dirty", Document.bGitDirty } };
		Json["backend_module"] = { { "kind", Document.ModuleKind }, { "file", Document.ModulePath.empty() ? FOrderedJson(nullptr)
			: FOrderedJson(StringConv::PathToUtf8(Document.ModulePath.filename())) }, { "path", Document.ModulePath.empty() ? FOrderedJson(nullptr)
			: FOrderedJson(StringConv::PathToUtf8(Document.ModulePath)) } };
		Json["config_dir"] = StringConv::PathToUtf8(Document.ConfigDir);
		FOrderedJson Inputs = FOrderedJson::array();
		for (const FStampInput& Input : Document.Inputs)
		{
			Inputs.push_back({ { "role", Input.Role }, { "path", StringConv::PathToUtf8(Input.Path) }, { "sha256", HashFileSha256(Input.Path) } });
		}
		Json["inputs"] = Inputs;
		Json["inputs_note"] = "raw-byte SHA-256 of every configuration file the stamped run read: the runner re-hashes them before it starts a process "
			"(variant_sha256 and physics_sha256 are the harness's canonical hashes of the same content)";
		Json["gates"] = Gates;
		Json["conformance_dir"] = StringConv::PathToUtf8(Document.ConformanceDir);
		Json["command_line"] = Document.CommandLine;
		Json["utc"] = GetUtcTimestamp();
		OutPath = StampDir / (KeySha + ".json");
		if (!WriteFileAtomic(OutPath, Json.dump(2) + "\n"))
		{
			OutError = "cannot write " + StringConv::PathToUtf8(OutPath);
			return false;
		}
		return true;
	}

	FStampCheck CheckConformanceStamp(const FConformanceStampKey& Key, const std::filesystem::path& StampDir)
	{
		FStampCheck Out;
		Out.KeySha256 = Key.ComputeSha256();
		Out.Path = StampDir / (Out.KeySha256 + ".json");
		std::ifstream File(Out.Path, std::ios::binary);
		if (!File)
		{
			Out.Reason = "no stamp for this key (PhysicsTestBench -Conformance with the same exe, backend module, configuration and overrides; "
				"python -I -m ptbench stamp)";
			return Out;
		}
		Out.bFound = true;
		std::stringstream Buffer;
		Buffer << File.rdbuf();
		const FJson Stamp = FJson::parse(Buffer.str(), nullptr, false);
		if (Stamp.is_discarded() || !Stamp.is_object() || Stamp.value("schema", std::string()) != "physicstest.stamp/1")
		{
			Out.Reason = "the stamp file is not a physicstest.stamp/1 document";
			return Out;
		}
		Out.StampUtc = Stamp.value("utc", std::string());
		if (Stamp.value("stamp_key_sha256", std::string()) != Out.KeySha256 || Stamp.value("key_canonical", std::string()) != Key.ToCanonicalJson())
		{
			Out.Reason = "the stamp's key does not match this run's key (renamed or edited file)";
			return Out;
		}
		if (!Stamp.value("valid", false))
		{
			Out.Reason = "the stamp says invalid: " + Stamp.value("invalid_reason", std::string("?"));
			return Out;
		}
		Out.bValid = true;
		return Out;
	}
}
