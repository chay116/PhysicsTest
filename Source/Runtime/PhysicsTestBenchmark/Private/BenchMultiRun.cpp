// BenchMultiRun.cpp - the multi-process gates of M7 (02_Architecture 14.3, 03_WorkPlan M7) and the exit-4 test load:
//   -Gate=G8  (DET1)  the run in -Runs=N (default 3) fresh processes of this exe; every final h_native (and the harness-visible
//                     state hash h_state) must be equal
//   -Gate=G11         the run with contact capture off (-Instrument=none) and on (-Instrument=contacts -CaptureEvery=1), both
//                     with -HashEvery=1; every frame's h_native and h_state must be equal (a result-neutral knob)
//   -BusyHelper=<s>   spins the -Affinity CPUs at HIGH_PRIORITY_CLASS for <s> seconds (the contaminated-host CTest)
// Test only: -InjectGateFault=<f> hands -InjectStatePerturbation=<f> to one child (G8: the last run, G11: contacts on), so
// the comparison has to fail - the negative control that shows the gates can fail (M7 fix round 0).
// A Fatal in the gate process writes the gate's exit.json (3, fatal) through FFatalExitScope (M8, M7 review).
// The children are spawned with FProcessSpawner (job object, timeout, stdout / stderr to child.log) one after the other and
// read back through their exit.json and manifest.json, exactly as the M8 runner will (02 12.5). Children run with
// -HostCheck=record: another process's load changes timings, not states.
#include "BenchmarkRunner.h"

#include "BenchCore.h"
#include "BenchRecorders.h"
#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "Misc/Parse.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <fstream>
#include <sstream>
#include <system_error>
#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsBenchGate, Log, All);

namespace PhysicsTest
{
	namespace
	{
		using FJson = nlohmann::ordered_json;

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

		std::filesystem::path GetExePath()
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

		// The parent's switches minus the ones the gate sets per child (and minus anything that is not a switch).
		std::vector<std::string> KeepSwitches(const std::string& CommandLine, const std::vector<const char*>& Drop)
		{
			std::vector<std::string> Kept;
			const char* Cursor = CommandLine.c_str();
			std::string Token;
			while (FParse::Token(Cursor, Token))
			{
				if (Token.empty() || (Token[0] != '-' && Token[0] != '/'))
				{
					continue;
				}
				bool bDrop = false;
				for (const char* Prefix : Drop)
				{
					bDrop = bDrop || StartsWithNoCase(Token.substr(1), Prefix);
				}
				if (!bDrop)
				{
					Kept.push_back(Token);
				}
			}
			return Kept;
		}

		std::wstring MakeChildCommandLine(const std::vector<std::string>& Switches, const std::filesystem::path& OutDir, const std::vector<std::string>& Extra)
		{
			std::wstring Line = L"\"" + GetExePath().wstring() + L"\"";
			const auto Append = [&Line](const std::string& Token)
			{
				const bool bQuote = Token.find_first_of(" \t") != std::string::npos && Token.find('"') == std::string::npos;
				Line += L" " + (bQuote ? L"\"" + StringConv::Utf8ToWide(Token) + L"\"" : StringConv::Utf8ToWide(Token));
			};
			for (const std::string& Token : Switches)
			{
				Append(Token);
			}
			for (const std::string& Token : Extra)
			{
				Append(Token);
			}
			Line += L" \"-Out=" + OutDir.wstring() + L"\"";
			return Line;
		}

		FJson ReadJsonFile(const std::filesystem::path& Path)
		{
			std::ifstream File(Path, std::ios::binary);
			if (!File)
			{
				return FJson();
			}
			std::stringstream Buffer;
			Buffer << File.rdbuf();
			FJson Json = FJson::parse(Buffer.str(), nullptr, false);
			return Json.is_discarded() ? FJson() : Json;
		}

		// Json[Group][Key] when both exist with the right type, else Default (manifests of a refused run lack groups).
		template <typename T>
		T GetField(const FJson& Json, const char* Group, const char* Key, const T& Default)
		{
			if (!Json.is_object() || !Json.contains(Group) || !Json[Group].is_object() || !Json[Group].contains(Key))
			{
				return Default;
			}
			const FJson& Value = Json[Group][Key];
			if constexpr (std::is_same_v<T, std::string>)
			{
				return Value.is_string() ? Value.get<std::string>() : Default;
			}
			else
			{
				return Value.is_number() ? Value.get<T>() : Default;
			}
		}

		std::string ReadTextFile(const std::filesystem::path& Path)
		{
			std::ifstream File(Path, std::ios::binary);
			std::stringstream Buffer;
			Buffer << File.rdbuf();
			return Buffer.str();
		}

		struct FChildRun
		{
			std::string Label;
			std::filesystem::path OutDir;
			FChildResult Process;
			int32 ExitJsonCode = -1;         // -1 = no exit.json (the runner treats it as a failed run, M8)
			std::string Status;
			std::string HNative;
			std::string HState;
			std::string DescSha256;
			int32 Threads = 0;
			int32 FramesRun = 0;
			FJson Manifest;
		};

		FChildRun RunChild(const std::string& Label, const std::vector<std::string>& Switches, const std::filesystem::path& OutDir, const std::vector<std::string>& Extra)
		{
			FChildRun Child;
			Child.Label = Label;
			Child.OutDir = OutDir;
			std::error_code Ignored;
			std::filesystem::remove_all(OutDir, Ignored);
			std::filesystem::create_directories(OutDir, Ignored);
			const std::wstring CommandLine = MakeChildCommandLine(Switches, OutDir, Extra);
			UE_LOG(LogPhysicsBenchGate, Display, "%s: %s", Label.c_str(), StringConv::WideToUtf8(CommandLine).c_str());
			Child.Process = FProcessSpawner::RunChild(CommandLine, 1800.0, OutDir / "child.log");
			const FJson Exit = ReadJsonFile(OutDir / "exit.json");
			if (Exit.is_object())
			{
				Child.ExitJsonCode = Exit.value("exit_code", -1);
				Child.Status = Exit.value("status", std::string());
			}
			Child.Manifest = ReadJsonFile(OutDir / "manifest.json");
			Child.HNative = GetField(Child.Manifest, "determinism", "h_native", std::string());
			Child.HState = GetField(Child.Manifest, "determinism", "h_state", std::string());
			Child.DescSha256 = GetField(Child.Manifest, "scene", "desc_sha256", std::string());
			Child.Threads = GetField(Child.Manifest, "threads", "T", 0);
			Child.FramesRun = GetField(Child.Manifest, "run", "frames_run", 0);
			UE_LOG(LogPhysicsBenchGate, Display, "%s: process exit %d, exit.json %d (%s), h_native %s, h_state %s, %.1f s", Label.c_str(), Child.Process.ExitCode,
				Child.ExitJsonCode, Child.Status.c_str(), Child.HNative.c_str(), Child.HState.c_str(), Child.Process.WallSeconds);
			return Child;
		}

		FJson ChildJson(const FChildRun& Child)
		{
			return FJson{ { "label", Child.Label }, { "out_dir", StringConv::PathToUtf8(Child.OutDir) }, { "started", Child.Process.bStarted },
				{ "timed_out", Child.Process.bTimedOut }, { "process_exit_code", Child.Process.ExitCode }, { "exit_json_code", Child.ExitJsonCode },
				{ "status", Child.Status }, { "h_native", Child.HNative }, { "h_state", Child.HState }, { "desc_sha256", Child.DescSha256 },
				{ "T", Child.Threads }, { "frames_run", Child.FramesRun }, { "wall_s", Child.Process.WallSeconds }, { "spawn_error", Child.Process.Error } };
		}
	}

	EBenchExit FBenchmarkRunner::RunBusyHelper(const FBenchRunSpec& Spec)
	{
		const uint64 Mask = Spec.AffinityMask != 0 ? Spec.AffinityMask : 0x1ull;
		UE_LOG(LogPhysicsBenchGate, Display, "busy helper: spinning CPUs 0x%llx at HIGH_PRIORITY_CLASS for %.2f s (the exit-4 test load)",
			static_cast<unsigned long long>(Mask), Spec.BusyHelperSeconds);
		FHostEnvironment::RunBusyLoad(Spec.BusyHelperSeconds, Mask);
		return EBenchExit::Ok;
	}

	EBenchExit FBenchmarkRunner::RunMultiProcessGate(const FBenchRunSpec& Spec)
	{
		const std::string Gate = Spec.MultiRunGate;
		const std::string RunId = MakeRunId();
		std::string Lower = Gate;
		for (char& C : Lower)
		{
			C = (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
		}
		std::filesystem::path OutDir = !Spec.OutDir.empty() ? Spec.OutDir : GetDefaultResultsDir() / StringConv::Utf8ToPath(Spec.Session) / ("gate_" + Lower + "_" + RunId);
		OutDir.make_preferred();
		std::error_code DirError;
		std::filesystem::create_directories(OutDir, DirError);
		if (DirError)
		{
			UE_LOG(LogPhysicsBenchGate, Error, "cannot create '%s': %s", StringConv::PathToUtf8(OutDir).c_str(), DirError.message().c_str());
			return EBenchExit::GateFailed;
		}
		const FFatalExitScope FatalScope(OutDir, RunId);   // a Fatal in the gate process still leaves exit.json (3, fatal)
		std::vector<FChildRun> Children;
		std::vector<std::string> Failures;
		std::vector<std::string> Notes;
		if (Gate == "G8")
		{
			// DET1 (02 14.3 G8, DET1: same exe, same T): N fresh processes of the identical run.
			const std::vector<std::string> Switches = KeepSwitches(Spec.CommandLine, { "Gate=", "Runs=", "Out=", "Session=", "HostCheck=", "Repeat=", "InjectGateFault=" });
			for (int32 Index = 0; Index < Spec.GateRuns; ++Index)
			{
				std::vector<std::string> Extra = { "-HostCheck=record", "-Repeat=" + std::to_string(Index) };
				if (Spec.InjectGateFaultFrame >= 0 && Index == Spec.GateRuns - 1)
				{
					Extra.push_back("-InjectStatePerturbation=" + std::to_string(Spec.InjectGateFaultFrame));   // test only: the negative control
				}
				Children.push_back(RunChild("run" + std::to_string(Index), Switches, OutDir / ("run" + std::to_string(Index)), Extra));
			}
			for (const FChildRun& Child : Children)
			{
				if (Child.ExitJsonCode != 0)
				{
					Failures.push_back(Child.Label + ": exit.json " + std::to_string(Child.ExitJsonCode) + " (" + Child.Status + "), process " + std::to_string(Child.Process.ExitCode));
				}
				if (Child.HState.empty())
				{
					Failures.push_back(Child.Label + ": no final state hash in manifest.json");
				}
			}
			for (size_t Index = 1; Index < Children.size(); ++Index)
			{
				if (Children[Index].HNative != Children[0].HNative)
				{
					Failures.push_back("h_native differs: " + Children[0].Label + " " + Children[0].HNative + ", " + Children[Index].Label + " " + Children[Index].HNative);
				}
				if (Children[Index].HState != Children[0].HState)
				{
					Failures.push_back("h_state differs: " + Children[0].Label + " " + Children[0].HState + ", " + Children[Index].Label + " " + Children[Index].HState);
				}
			}
			Notes.push_back("DET1: " + std::to_string(Children.size()) + " fresh processes of one exe; final h_native " + (Children.empty() ? "" : Children[0].HNative)
				+ (Children.empty() || Children[0].HNative.empty() ? " (backend without native hash: h_state only)" : ""));
		}
		else
		{
			// G11 (02 14.3, 03 M7): contact capture is a result-neutral knob - every frame's state, with it off and on.
			const std::vector<std::string> Switches = KeepSwitches(Spec.CommandLine, { "Gate=", "Runs=", "Out=", "Session=", "HostCheck=", "Instrument=",
				"HashEvery=", "CaptureEvery=", "InjectGateFault=" });
			std::vector<std::string> OnExtra = { "-HostCheck=record", "-Instrument=contacts", "-HashEvery=1", "-CaptureEvery=1" };
			if (Spec.InjectGateFaultFrame >= 0)
			{
				OnExtra.push_back("-InjectStatePerturbation=" + std::to_string(Spec.InjectGateFaultFrame));   // test only: the negative control
			}
			Children.push_back(RunChild("contacts_off", Switches, OutDir / "off", { "-HostCheck=record", "-Instrument=none", "-HashEvery=1" }));
			Children.push_back(RunChild("contacts_on", Switches, OutDir / "on", OnExtra));
			for (const FChildRun& Child : Children)
			{
				if (Child.ExitJsonCode != 0)
				{
					Failures.push_back(Child.Label + ": exit.json " + std::to_string(Child.ExitJsonCode) + " (" + Child.Status + "), process " + std::to_string(Child.Process.ExitCode));
				}
			}
			const std::string Off = ReadTextFile(OutDir / "off" / "hashes.csv");
			const std::string On = ReadTextFile(OutDir / "on" / "hashes.csv");
			std::istringstream OffLines(Off);
			std::istringstream OnLines(On);
			std::string A;
			std::string B;
			int32 Rows = -1;   // the header line
			int32 FirstDifference = -1;
			while (true)
			{
				const bool bA = static_cast<bool>(std::getline(OffLines, A));
				const bool bB = static_cast<bool>(std::getline(OnLines, B));
				if (!bA && !bB)
				{
					break;
				}
				if (bA != bB || A != B)
				{
					FirstDifference = Rows + 1;
					Failures.push_back("hashes.csv differs at data row " + std::to_string(Rows + 1) + ": off '" + (bA ? A : std::string("<end>")) + "', on '" + (bB ? B : std::string("<end>")) + "'");
					break;
				}
				++Rows;
			}
			if (Rows <= 0 && FirstDifference < 0)
			{
				Failures.push_back("hashes.csv has no frame rows (off " + std::to_string(Off.size()) + " B, on " + std::to_string(On.size()) + " B)");
			}
			if (Children.size() == 2 && Children[0].FramesRun != Rows && FirstDifference < 0)
			{
				Failures.push_back("hashes.csv has " + std::to_string(Rows) + " rows for " + std::to_string(Children[0].FramesRun) + " frames (HashEvery=1)");
			}
			const FJson Recorders = Children.size() == 2 && Children[1].Manifest.is_object() && Children[1].Manifest.contains("recorders")
				? Children[1].Manifest["recorders"] : FJson::object();
			const uint64 ContactFrames = GetField(Recorders, "contacts", "frames", uint64(0));
			const uint64 ContactPoints = GetField(Recorders, "contacts", "points", uint64(0));
			const std::string ContactsNote = GetField(Recorders, "contacts", "note", std::string());
			Notes.push_back(std::to_string(Rows) + " frames compared (h_native and h_state per frame); contacts on: " + std::to_string(ContactFrames)
				+ " captured frames, " + std::to_string(ContactPoints) + " points" + (ContactsNote.empty() ? "" : " (" + ContactsNote + ")"));
		}
		const bool bPassed = Failures.empty();
		FJson Json;
		Json["schema"] = "physicstest.gate/1";
		Json["gate"] = Gate;
		Json["result"] = bPassed ? "pass" : "fail";
		Json["command_line"] = Spec.CommandLine;
		Json["utc"] = GetUtcTimestamp();
		FJson Runs = FJson::array();
		for (const FChildRun& Child : Children)
		{
			Runs.push_back(ChildJson(Child));
		}
		Json["runs"] = Runs;
		Json["failures"] = Failures;
		Json["notes"] = Notes;
		if (Spec.InjectGateFaultFrame >= 0)
		{
			Json["injected_gate_fault_frame"] = Spec.InjectGateFaultFrame;   // test only: one child was perturbed on purpose
		}
		const std::string FileName = "gate_" + Lower + ".json";
		BenchRecorders::WriteText(OutDir / FileName, Json.dump(2) + "\n");
		for (const std::string& Note : Notes)
		{
			UE_LOG(LogPhysicsBenchGate, Display, "%s %s", Gate.c_str(), Note.c_str());
		}
		for (const std::string& Failure : Failures)
		{
			UE_LOG(LogPhysicsBenchGate, Error, "%s FAIL %s", Gate.c_str(), Failure.c_str());
		}
		UE_LOG(LogPhysicsBenchGate, Display, "%s %s: %s -> %s", Gate.c_str(), Spec.Backend.c_str(), bPassed ? "Pass" : "Fail", StringConv::PathToUtf8(OutDir / FileName).c_str());
		FExitRecord Record;
		Record.Code = bPassed ? EBenchExit::Ok : EBenchExit::GateFailed;
		Record.Status = bPassed ? "ok" : "gate_failed";
		Record.Category = bPassed ? "" : "gate";
		Record.Reason = bPassed ? "" : Failures.front();
		Record.RunId = RunId;
		Record.Outputs = { FileName };
		Record.WriteAndFlush(OutDir);
		return Record.Code;
	}
}
