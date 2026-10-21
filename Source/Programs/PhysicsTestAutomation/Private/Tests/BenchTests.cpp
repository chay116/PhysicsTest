// BenchTests.cpp - unit tests of the M7 bench pieces (03_WorkPlan M7, 02_Architecture 9.6, 12.3): the P-core thread masks, the
// -Instrument names, the recorders' file layouts (states.ptst, contacts.ptcn, hashes.csv), the state hashes, exit.json, the
// host load monitor and the tolerances loader's handling of malformed entries (M6 review). The process-level behaviour (host
// controls, preflight, exit codes, G8 / G11) is covered by the PhysicsTest.Bench.* CTests that run the exe.
#include "CoreMinimal.h"

#include "BenchCore.h"
#include "BenchmarkRunner.h"
#include "BenchRecorders.h"
#include "Gates/PhysicsGates.h"
#include "HAL/PlatformTime.h"
#include "Physics/PhysicsProfile.h"
#include "PhysScene.h"
#include "PhysicsTestAbi/PtBackendAbi.h"
#include "PhysicsTestScenario.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include "Misc/PtThirdPartyIncludesEnd.h"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace PhysicsTest;

namespace
{
	std::filesystem::path MakeTempDir(const char* Name)
	{
		wchar_t Buffer[MAX_PATH] = {};
		GetTempPathW(MAX_PATH, Buffer);
		std::filesystem::path Dir = std::filesystem::path(Buffer) / L"PhysicsTestAutomation" / Name;
		std::error_code Ignored;
		std::filesystem::remove_all(Dir, Ignored);
		std::filesystem::create_directories(Dir, Ignored);
		return Dir;
	}

	std::vector<uint8> ReadBytes(const std::filesystem::path& Path)
	{
		std::ifstream File(Path, std::ios::binary);
		return std::vector<uint8>((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	}

	template <typename T>
	T ReadAt(const std::vector<uint8>& Bytes, const size_t Offset)
	{
		T Value{};
		std::memcpy(&Value, Bytes.data() + Offset, sizeof(T));
		return Value;
	}

	// A Null scene of M3.BoxPile N=1000 with the Normalized profile (1001 bodies).
	std::unique_ptr<FPhysScene> MakeBoxPile(FResolvedPhysicsConfig& Config)
	{
		FBenchRunSpec Spec;
		Spec.Backend = "Null";
		Spec.ConfigDir = PT_CONFIG_DIR;
		FBenchConfig Files;
		std::string Error;
		REQUIRE_MESSAGE(LoadBenchConfig(Spec, Config, Files, Error), Error);
		IPhysicsTestScenario* Scenario = FScenarioRegistry::Get().Find("M3.BoxPile");
		REQUIRE(Scenario != nullptr);
		FScenarioParams Params;
		REQUIRE(Scenario->ResolveParams(FScenarioParams(), Params, Error));
		FPhysSceneInitParams InitParams;
		InitParams.DebugName = "BenchTests";
		InitParams.BackendName = "Null";
		InitParams.Config = &Config;
		auto Scene = std::make_unique<FPhysScene>(InitParams);
		FRandomStream Random(Scenario->GetSeed(Params));
		const FScenarioContext Context{ Params, Random, &Config };
		Scenario->BuildScene(*Scene, Context);
		return Scene;
	}
}

TEST_CASE("PhysicsTest.Bench.ThreadMasksFromTopology")
{
	// The i5-13600KF layout (02 9.6): six P-cores with two logical CPUs each (0-11), eight E-cores (12-19).
	FProcessorTopology Topology;
	for (uint32 Core = 0; Core < 6; ++Core)
	{
		Topology.Cores.push_back(FProcessorCore{ 0x3ull << (2 * Core), 2 * Core, 1 });
	}
	for (uint32 Cpu = 12; Cpu < 20; ++Cpu)
	{
		Topology.Cores.push_back(FProcessorCore{ 1ull << Cpu, Cpu, 0 });
	}
	Topology.MaxEfficiencyClass = 1;
	for (const FProcessorCore& Core : Topology.Cores)
	{
		if (Core.EfficiencyClass == 1)
		{
			Topology.PCoreFirstLogicals.push_back(Core.FirstLogical);
		}
	}
	const uint64 Expected[6] = { 0x1, 0x5, 0x15, 0x55, 0x155, 0x555 };
	for (uint32 T = 1; T <= 6; ++T)
	{
		CAPTURE(T);
		CHECK(Topology.MakeThreadMask(T) == Expected[T - 1]);
		CHECK(CountBits(Topology.MakeThreadMask(T)) == T);
	}
	CHECK(Topology.MakeThreadMask(7) == 0);   // more threads than P-cores: no mask (the bench refuses the run)
	CHECK(Topology.MakeThreadMask(0) == 0);
	CHECK(Topology.CoresOf(0x5) == 0xF);       // the SMT siblings 1 and 3 belong to the cores of 0 and 2
	CHECK(Topology.CoresOf(1ull << 13) == (1ull << 13));
	CHECK(MaskToCpus(0x155) == std::vector<uint32>{ 0, 2, 4, 6, 8 });
	// The series flag of a mask (02 9.6: the HT and E-core series are flagged apart).
	const FProcessorTopology::FMaskShape OneLp = Topology.DescribeMask(0x15);
	CHECK(OneLp.Cores == 3u);
	CHECK(OneLp.SmtPairCores == 0u);
	CHECK(OneLp.ECores == 0u);
	CHECK(std::string(OneLp.Series) == "p_cores_one_lp");
	const FProcessorTopology::FMaskShape Smt = Topology.DescribeMask(0x7);   // CPUs 0, 1 (one core) and 2
	CHECK(Smt.Cores == 2u);
	CHECK(Smt.SmtPairCores == 1u);
	CHECK(std::string(Smt.Series) == "p_cores_smt");
	const FProcessorTopology::FMaskShape WithE = Topology.DescribeMask(0x1 | (1ull << 12) | 0x3);
	CHECK(WithE.Cores == 2u);
	CHECK(WithE.ECores == 1u);
	CHECK(std::string(WithE.Series) == "with_e_cores");

	// This machine: at least one core, a non-empty P-core list, and T = 1 is one CPU.
	const FProcessorTopology Host = ReadProcessorTopology();
	REQUIRE(!Host.Cores.empty());
	REQUIRE(!Host.PCoreFirstLogicals.empty());
	CHECK(CountBits(Host.MakeThreadMask(1)) == 1);
	CHECK((Host.MakeThreadMask(1) & Host.SystemMask) == Host.MakeThreadMask(1));
	std::string Firsts;
	for (const uint32 Cpu : Host.PCoreFirstLogicals)
	{
		Firsts += std::to_string(Cpu) + " ";
	}
	MESSAGE("this machine: " << Host.Cores.size() << " cores, P-core first logicals " << Firsts);
}

TEST_CASE("PhysicsTest.Bench.InstrumentNames")
{
	uint32 Flags = 0;
	std::string Error;
	CHECK(ParseInstrumentFlags("counters,contacts", Flags, Error));
	CHECK(Flags == (PT_INSTR_COUNTERS | PT_INSTR_CONTACTS));
	CHECK(ParseInstrumentFlags("none", Flags, Error));
	CHECK(Flags == 0u);
	CHECK(ParseInstrumentFlags("Counters,Phases,Alloc,Contacts,Native_Debug_Draw", Flags, Error));
	CHECK(Flags == PT_INSTR_ALL);
	CHECK(GetInstrumentNames(PT_INSTR_ALL).size() == 5);
	CHECK_FALSE(ParseInstrumentFlags("counters,,contacts", Flags, Error));
	CHECK_FALSE(ParseInstrumentFlags("bogus", Flags, Error));
	CHECK(Error.find("unknown tier 'bogus'") != std::string::npos);
}

TEST_CASE("PhysicsTest.Bench.CommandLineRunOptions")
{
	FBenchRunSpec Spec;
	std::string Error;
	REQUIRE_MESSAGE(FBenchRunSpec::FromCommandLine("-Backend=Jolt -Scenario=M3.BoxPile -Threads=3 -Affinity=0x15 -RunMode=Viz -Instrument=counters "
		"-HashEvery=5 -StatesEvery=0 -Repeat=2 -Order=7 -BlockSeed=0xABC -PreRoll=0.25 -HostCheck=record -ContaminationPct=2.5", Spec, Error), Error);
	CHECK(Spec.Threads == 3);
	CHECK(Spec.AffinityMask == 0x15ull);
	REQUIRE(Spec.RunMode.has_value());
	CHECK(*Spec.RunMode == EPhysicsRunMode::Viz);
	REQUIRE(Spec.InstrumentFlags.has_value());
	CHECK(*Spec.InstrumentFlags == PT_INSTR_COUNTERS);
	CHECK(Spec.HashEvery.value_or(-1) == 5);
	CHECK(Spec.StatesEvery.value_or(-1) == 0);
	CHECK(Spec.RepeatIndex == 2);
	CHECK(Spec.OrderIndex == 7);
	CHECK(Spec.BlockSeed == 0xABCull);
	CHECK(Spec.PreRollSeconds == doctest::Approx(0.25));
	CHECK(Spec.HostCheck == EHostCheckPolicy::Record);
	CHECK(Spec.ContaminationPct == doctest::Approx(2.5));
	// The mask must name T CPUs; malformed values are errors, and -Out is known even when a later switch fails.
	CHECK_FALSE(FBenchRunSpec::FromCommandLine("-Backend=Jolt -Scenario=M3.BoxPile -Threads=2 -Affinity=0x1", Spec, Error));
	CHECK(Error.find("-Affinity names 1 CPUs but -Threads=2") != std::string::npos);
	CHECK_FALSE(FBenchRunSpec::FromCommandLine("-Out=C:\\x -Backend=Jolt -Scenario=M3.BoxPile -RunMode=Fast", Spec, Error));
	CHECK(!Spec.OutDir.empty());
	CHECK_FALSE(FBenchRunSpec::FromCommandLine("-Backend=Jolt -Scenario=M3.BoxPile -HostCheck=maybe", Spec, Error));
	CHECK_FALSE(FBenchRunSpec::FromCommandLine("-Backend=Jolt -Scenario=M3.BoxPile -Gate=G3", Spec, Error));
	CHECK(FBenchRunSpec::FromCommandLine("-Backend=Jolt -Scenario=M3.BoxPile -Gate=g8 -Runs=4", Spec, Error));
	CHECK(Spec.MultiRunGate == "G8");
	CHECK(Spec.GateRuns == 4);
	CHECK(FBenchRunSpec::FromCommandLine("-BusyHelper=2 -Affinity=0x1", Spec, Error));
	CHECK(Spec.BusyHelperSeconds == doctest::Approx(2.0));
}

TEST_CASE("PhysicsTest.Bench.StateHashes")
{
	FResolvedPhysicsConfig Config;
	std::unique_ptr<FPhysScene> Scene = MakeBoxPile(Config);
	const uint64 Quantized = BenchRecorders::ComputeQuantizedStateHash(*Scene);
	const uint64 Exact = BenchRecorders::ComputeExactStateHash(*Scene);
	CHECK(BenchRecorders::ComputeQuantizedStateHash(*Scene) == Quantized);   // pure functions of the pulled state
	CHECK(BenchRecorders::ComputeExactStateHash(*Scene) == Exact);
	FBodyInstance& Body = *Scene->GetBodies()[5];
	const FVector Saved = Body.State.BodyTM.GetTranslation();
	// A last-bit change: h_state sees it, h_quantized (1e-4 cm) does not.
	Body.State.BodyTM.SetTranslation(FVector(std::nextafter(Saved.X, 1.0e30), Saved.Y, Saved.Z));
	CHECK(BenchRecorders::ComputeExactStateHash(*Scene) != Exact);
	CHECK(BenchRecorders::ComputeQuantizedStateHash(*Scene) == Quantized);
	// A 1e-3 cm change: both see it.
	Body.State.BodyTM.SetTranslation(Saved + FVector(1.0e-3, 0.0, 0.0));
	CHECK(BenchRecorders::ComputeQuantizedStateHash(*Scene) != Quantized);
	// q and -q are one rotation: h_quantized normalises w >= 0.
	Body.State.BodyTM.SetTranslation(Saved);
	const FQuat Q = Body.State.BodyTM.GetRotation();
	Body.State.BodyTM.SetRotation(FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W));
	CHECK(BenchRecorders::ComputeQuantizedStateHash(*Scene) == Quantized);
	CHECK(BenchRecorders::ComputeExactStateHash(*Scene) != Exact);
}

TEST_CASE("PhysicsTest.Bench.StatesFileLayout")
{
	FResolvedPhysicsConfig Config;
	std::unique_ptr<FPhysScene> Scene = MakeBoxPile(Config);
	const std::filesystem::path Dir = MakeTempDir("StatesFileLayout");
	const uint32 Bodies = static_cast<uint32>(Scene->GetBodies().size());
	REQUIRE(Bodies == 1001);
	const std::string SceneSha(64, 'a');
	const std::string DescSha = "0123456789abcdef" + std::string(48, '0');
	BenchRecorders::FStateRecorder Recorder;
	REQUIRE(Recorder.Open(Dir / "states.ptst", Bodies, 1.0 / 60.0, SceneSha, DescSha));
	Recorder.Append(0, 1, 0.0166, *Scene);
	Scene->Tick(1.0f / 60.0f);
	Recorder.Append(1, 2, 0.0333, *Scene);
	REQUIRE(Recorder.Close());
	REQUIRE(BenchRecorders::FStateRecorder::WriteDtypeJson(Dir / "states.dtype.json", Bodies, Recorder.GetFrameCount(), 1.0 / 60.0));
	const std::vector<uint8> Bytes = ReadBytes(Dir / "states.ptst");
	REQUIRE(Bytes.size() == 64u + 2u * (16u + Bodies * 112u));
	CHECK(std::memcmp(Bytes.data(), "PTST", 4) == 0);
	CHECK(ReadAt<uint32>(Bytes, 4) == 1u);          // version
	CHECK(ReadAt<uint32>(Bytes, 8) == 1u);          // flags: frame_count final
	CHECK(ReadAt<uint32>(Bytes, 12) == Bodies);
	CHECK(ReadAt<uint32>(Bytes, 16) == 2u);         // frame_count
	CHECK(ReadAt<uint32>(Bytes, 20) == 112u);       // record_stride
	CHECK(ReadAt<uint32>(Bytes, 24) == 1u);         // units: cm
	CHECK(ReadAt<uint32>(Bytes, 28) == 16u);        // frame_header_size
	CHECK(ReadAt<double>(Bytes, 32) == 1.0 / 60.0);
	CHECK(ReadAt<uint8>(Bytes, 40) == 0xAA);        // scene_sha256 prefix
	CHECK(ReadAt<uint8>(Bytes, 48) == 0x01);        // desc_sha256 prefix
	CHECK(ReadAt<uint8>(Bytes, 55) == 0xEF);
	// Frame block 1: its header, then body 5's record equals the pulled state bit for bit.
	const size_t Frame1 = 64u + 16u + Bodies * 112u;
	CHECK(ReadAt<uint32>(Bytes, Frame1) == 1u);
	CHECK(ReadAt<uint32>(Bytes, Frame1 + 4) == 2u);
	CHECK(ReadAt<double>(Bytes, Frame1 + 8) == 0.0333);
	const FBodyInstance& Body = *Scene->GetBodies()[5];
	const size_t Record = Frame1 + 16u + 5u * 112u;
	CHECK(ReadAt<double>(Bytes, Record + 16) == Body.State.BodyTM.GetTranslation().Z);
	CHECK(ReadAt<double>(Bytes, Record + 48) == Body.State.BodyTM.GetRotation().W);
	CHECK(ReadAt<double>(Bytes, Record + 72) == Body.State.LinearVelocity.Z);
	CHECK(ReadAt<uint8>(Bytes, Record + 104) == static_cast<uint8>(Body.State.ObjectState));
	CHECK(ReadAt<uint8>(Bytes, Record + 111) == 0u);
	std::ifstream DtypeFile(Dir / "states.dtype.json");
	const nlohmann::json Dtype = nlohmann::json::parse(DtypeFile, nullptr, false);
	REQUIRE(Dtype.is_object());
	CHECK(Dtype["frame_count"] == 2);
	CHECK(Dtype["frame_bytes"] == 16 + Bodies * 112);
}

TEST_CASE("PhysicsTest.Bench.ContactsFileLayout")
{
	const std::filesystem::path Dir = MakeTempDir("ContactsFileLayout");
	BenchRecorders::FContactRecorder Recorder;
	REQUIRE(Recorder.Open(Dir / "contacts.ptcn", 1u));
	PtContactPoint Points[3] = {};
	for (uint32 Index = 0; Index < 3; ++Index)
	{
		Points[Index].Body0 = Index + 1;
		Points[Index].Phi = -0.5 * Index;
	}
	Recorder.Append(4, std::span<const PtContactPoint>(Points, 3));
	Recorder.Append(5, std::span<const PtContactPoint>());
	REQUIRE(Recorder.Close());
	const std::vector<uint8> Bytes = ReadBytes(Dir / "contacts.ptcn");
	REQUIRE(Bytes.size() == 32u + 8u + 3u * sizeof(PtContactPoint) + 8u);
	CHECK(std::memcmp(Bytes.data(), "PTCN", 4) == 0);
	CHECK(ReadAt<uint32>(Bytes, 8) == 128u);
	CHECK(ReadAt<uint32>(Bytes, 12) == 2u);
	CHECK(ReadAt<uint32>(Bytes, 20) == PT_ABI_PROVISIONAL_REVISION);   // M8: the PtContactPoint layout revision (M7 review)
	CHECK(ReadAt<uint32>(Bytes, 24) == 1u);                            // the declared contact.point_pose
	CHECK(ReadAt<uint32>(Bytes, 28) == 0u);                            // reserved
	CHECK(ReadAt<uint32>(Bytes, 32) == 4u);
	CHECK(ReadAt<uint32>(Bytes, 36) == 3u);
	CHECK(ReadAt<PtContactPoint>(Bytes, 40 + 2 * sizeof(PtContactPoint)).Body0 == 3u);
	CHECK(ReadAt<uint32>(Bytes, 40 + 3 * sizeof(PtContactPoint)) == 5u);
	CHECK(ReadAt<uint32>(Bytes, 44 + 3 * sizeof(PtContactPoint)) == 0u);
}

TEST_CASE("PhysicsTest.Bench.ExitRecord")
{
	const std::filesystem::path Dir = MakeTempDir("ExitRecord");
	FExitRecord Record;
	Record.Code = EBenchExit::Contaminated;
	Record.Status = "contaminated_host";
	Record.Category = "host";
	Record.Reason = "test";
	Record.Outputs = { "manifest.json" };
	REQUIRE(Record.WriteAndFlush(Dir));
	CHECK_FALSE(std::filesystem::exists(Dir / "exit.json.tmp"));   // written through a temporary file and renamed
	std::ifstream File(Dir / "exit.json");
	const nlohmann::json Json = nlohmann::json::parse(File, nullptr, false);
	REQUIRE(Json.is_object());
	CHECK(Json["schema"] == "physicstest.exit/1");
	CHECK(Json["exit_code"] == 4);
	CHECK(Json["status"] == "contaminated_host");
	CHECK(Json["outputs"][0] == "manifest.json");
}

TEST_CASE("PhysicsTest.Bench.HostLoadMonitor")
{
	// 0.2 s of this process spinning one thread: its own share is about one core; the monitor is cycle-based (TSC).
	FHostLoadMonitor Monitor;
	Monitor.Begin();
	const uint64 Deadline = FPlatformTime::Cycles64() + FPlatformTime::GetCyclesPerSecond64() / 5;
	volatile uint64 Sink = 0;
	while (FPlatformTime::Cycles64() < Deadline)
	{
		Sink = Sink + 1;
	}
	const FHostLoad Load = Monitor.End(0x1, 0x3);
	REQUIRE(Load.bValid);
	CHECK(Load.WallSeconds == doctest::Approx(0.2).epsilon(0.2));
	CHECK(Load.OwnProcessCorePct > 50.0);
	CHECK(Load.OwnProcessCorePct < 400.0);   // other doctest threads may run too, but not 4 cores' worth
	CHECK(Load.MaskBusyOtherCorePct >= 0.0);
	// The run's cores (mask + SMT sibling) hold the mask's CPUs: their other-process share is never below the mask's.
	CHECK(Load.CoreBusyOtherCorePct >= Load.MaskBusyOtherCorePct - 1.0e-9);
	CHECK(Load.PerCpuBusyPct.size() >= 2u);
	MESSAGE("own " << Load.OwnProcessCorePct << " %, CPU 0 other " << Load.MaskBusyOtherCorePct << " %, system other " << Load.SystemBusyOtherCorePct << " %");
	CHECK(MeasureClockReadOverheadNs(2000) > 0.0);
}

TEST_CASE("PhysicsTest.Bench.TolerancesMalformedEntries")
{
	// M7 (M6 review): a malformed tolerances.json is a load error, never a json::type_error that ends the program.
	const auto Load = [](const std::string& Text, std::string& Error)
	{
		Gates::FTolerances Tolerances;
		return Tolerances.LoadText(Text, "test.json", Error);
	};
	std::string Error;
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":{"a.b":{"expect":"text_contains_options","options":[3]}}}}}})", Error));
	CHECK(Error.find("options/flags entries must be strings") != std::string::npos);
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":{"a.b":{"expect":"text_flags","flags":{"x":1}}}}}}})", Error));
	CHECK(Error.find("options/flags entries must be strings") != std::string::npos);
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":"not an object"}}}})", Error));
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G1":{"z":{"default":1,"reason":"r","overrides":[{"engine":7,"value":1,"reason":"r"}]}}}})", Error));
	CHECK(Error.find("malformed entry") != std::string::npos);
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G1":{"z":{"default":1,"reason":"r","overrides":5}}}})", Error));
	// M7 fix round 0: "scene" is a name or a non-empty list of names; "by_scene" gives one more scene its own rule.
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":{"a.b":{"expect":"const:1","reason":"r","scene":[]}}}}}})", Error));
	CHECK(Error.find("non-empty list") != std::string::npos);
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":{"a.b":{"expect":"const:1","reason":"r","scene":["S.A",2]}}}}}})", Error));
	CHECK_FALSE(Load(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"*":{"a.b":{"expect":"const:1","reason":"r","by_scene":{"S.B":{"expect":"const:2","reason":"r","by_scene":{}}}}}}}}})", Error));
	CHECK(Error.find("by_scene") != std::string::npos);
	{
		Gates::FTolerances Scoped;
		REQUIRE_MESSAGE(Scoped.LoadText(R"({"schema":"physicstest.tolerances/1","gates":{"G9":{"readback":{"Jolt":{"a.b":{"expect":"const:1","reason":"r","scene":["S.A","S.C"],"by_scene":{"S.B":{"expect":"const:2","reason":"r2"}}}}}}}})",
			"scoped.json", Error), Error);
		const Gates::FReadbackRule* Plain = Scoped.FindReadbackRule("Jolt", "", "a.b", "S.A");
		const Gates::FReadbackRule* ForB = Scoped.FindReadbackRule("Jolt", "", "a.b", "S.B");
		REQUIRE(Plain != nullptr);
		REQUIRE(ForB != nullptr);
		CHECK(Plain->Expect == "const:1");
		CHECK(Plain->Scenes == std::vector<std::string>{ "S.A", "S.C" });
		CHECK(ForB->Expect == "const:2");
		CHECK(ForB->Scenes == std::vector<std::string>{ "S.B" });
		CHECK(Scoped.FindReadbackRule("Jolt", "", "a.b") == Plain);   // no scene: the plain rule
	}
	// The real file still loads.
	Gates::FTolerances Real;
	CHECK_MESSAGE(Real.LoadFile(Gates::GetTolerancesPath(PT_CONFIG_DIR), Error), Error);
}
