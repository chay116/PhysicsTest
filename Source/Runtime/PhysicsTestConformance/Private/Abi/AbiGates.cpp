// AbiGates.cpp - A0-A7 over a backend's raw C table (see Abi/AbiGates.h). A10 is in AbiFrameModelGate.cpp.
#include "Abi/AbiGates.h"
#include "AbiGateHelpers.h"

#include "Physics/EffectiveSettings.h"
#include "Physics/PhysicsBackend.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

namespace PhysicsTest::AbiGates
{
	using namespace AbiGateHelpers;

	bool PrepareAbiGateTarget(const std::string_view BackendName, FAbiGateTarget& Out, std::string& OutError)
	{
		FPhysicsBackendModule* Module = FPhysicsBackendRegistry::Get().FindOrLoadModule(BackendName, OutError);
		if (Module == nullptr)
		{
			return false;
		}
		if (Module->GetNumLiveScenes() != 0)
		{
			OutError = "backend '" + std::string(BackendName) + "' still has live scene proxies";
			return false;
		}
		if (Module->IsInitialized())
		{
			Module->TermGamePhys();
		}
		Out.BackendName = std::string(BackendName);
		Out.Label = std::string(BackendName) + (Module->IsDll() ? " (DLL)" : " (static)");
		Out.Module = Module;
		Out.Api = &Module->GetApi();
		Out.Entry = Module->GetEntryPoint();
		return true;
	}

	// ---- A0 lifecycle ------------------------------------------------------------------------------------------------
	FGateReport RunA0Lifecycle(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A0", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);

		PtBackendInfo Info = {};
		Info.StructSize = sizeof(PtBackendInfo);
		R.Check(Api.GetInfo(&Info) == PT_OK, "GetInfo before InitGamePhys returns PT_OK");
		R.Check(Info.Name != nullptr && Info.ProvisionalRevision == PT_ABI_PROVISIONAL_REVISION, "GetInfo fills Name and ProvisionalRevision");
		PtSceneId Scene = 0;
		const PtSceneDesc Desc = MakeSceneDesc();
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_E_BAD_STATE, "CreateScene before InitGamePhys -> PT_E_BAD_STATE");
		R.Check(Api.TermGamePhys() == PT_E_BAD_STATE, "TermGamePhys before InitGamePhys -> PT_E_BAD_STATE");
		// Outside Ready only GetInfo, InitGamePhys and GetLastErrorMessage work; the module-level entries too are BAD_STATE.
		PtMemoryStats Memory = {};
		R.Check(Api.GetMemoryStats(0, &Memory) == PT_E_BAD_STATE, "GetMemoryStats(0) before InitGamePhys -> PT_E_BAD_STATE");
		R.Check(Api.SetOption(0, "a0.key", "1") == PT_E_BAD_STATE, "SetOption(0) before InitGamePhys -> PT_E_BAD_STATE");
		PtTextBuffer Query = {};
		R.Check(Api.DumpEffectiveSettings(0, &Query) == PT_E_BAD_STATE, "DumpEffectiveSettings(0) before InitGamePhys -> PT_E_BAD_STATE");
		R.Check(LastError(Api).find("not initialised") != std::string::npos, "GetLastErrorMessage works before InitGamePhys: " + LastError(Api));
		const PtInitParams Init = MakeInitParams();
		R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys -> PT_OK");
		R.Check(Api.InitGamePhys(&Init) == PT_E_BAD_STATE, "a second InitGamePhys -> PT_E_BAD_STATE");

		// create / destroy x 10, each with bodies and a frame; ids are unique and stale ids are rejected.
		std::vector<PtSceneId> Seen;
		for (int32 Round = 0; Round < 10; ++Round)
		{
			PtSceneId Id = 0;
			const bool bCreated = R.Check(Api.CreateScene(&Desc, &Id) == PT_OK && Id != 0, "CreateScene round " + std::to_string(Round));
			if (!bCreated)
			{
				continue;
			}
			for (const PtSceneId Old : Seen)
			{
				R.Check(Old != Id, "scene ids are not reused (round " + std::to_string(Round) + ")");
			}
			Seen.push_back(Id);
			const FTestScene Bodies = PopulateScene(Api, Id);
			R.Check(Bodies.Result == PT_OK, "materials + actors round " + std::to_string(Round) + ": " + LastError(Api));
			R.Check(StepFrame(Api, Id) == PT_OK, "one frame round " + std::to_string(Round));
			R.Check(Api.DestroyScene(Id) == PT_OK, "DestroyScene round " + std::to_string(Round));
			R.Check(Api.DestroyScene(Id) == PT_E_INVALID_HANDLE, "DestroyScene of a destroyed scene -> PT_E_INVALID_HANDLE");
			R.Check(StepFrame(Api, Id) == PT_E_INVALID_HANDLE, "SetUpForFrame on a destroyed scene -> PT_E_INVALID_HANDLE");
		}

		// The 16-slot handle table: the 17th scene is refused, every slot is reusable afterwards.
		std::vector<PtSceneId> Live;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			PtSceneId Id = 0;
			if (R.Check(Api.CreateScene(&Desc, &Id) == PT_OK, "CreateScene slot " + std::to_string(Index)))
			{
				Live.push_back(Id);
			}
		}
		PtSceneId Extra = 0;
		R.Check(Api.CreateScene(&Desc, &Extra) == PT_E_CAPACITY && Extra == 0, "the 17th live scene -> PT_E_CAPACITY, OutScene 0");
		for (const PtSceneId Id : Live)
		{
			R.Check(Api.DestroyScene(Id) == PT_OK, "DestroyScene of a slot");
		}
		// TermGamePhys with a live scene destroys it.
		PtSceneId Leftover = 0;
		R.Check(Api.CreateScene(&Desc, &Leftover) == PT_OK, "CreateScene before TermGamePhys");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys -> PT_OK (destroys the live scene)");
		R.Check(Api.TermGamePhys() == PT_E_BAD_STATE, "a second TermGamePhys -> PT_E_BAD_STATE");
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_E_BAD_STATE, "CreateScene after TermGamePhys -> PT_E_BAD_STATE");
		R.Check(Api.DestroyScene(Leftover) == PT_E_BAD_STATE, "DestroyScene after TermGamePhys -> PT_E_BAD_STATE");
		R.Check(Api.GetInfo(&Info) == PT_OK, "GetInfo after TermGamePhys -> PT_OK");
		// Re-initialisation follows PT_MODULE_NO_REINIT.
		const bool bNoReinit = (Info.ModuleFlags & PT_MODULE_NO_REINIT) != 0;
		R.Note(std::string("PT_MODULE_NO_REINIT ") + (bNoReinit ? "set: a second InitGamePhys after the Term must be refused" : "not set: the module re-initialises"));
		const PtResult Again = Api.InitGamePhys(&Init);
		R.Check(bNoReinit ? Again == PT_E_BAD_STATE : Again == PT_OK, std::string("InitGamePhys after TermGamePhys follows PT_MODULE_NO_REINIT (")
			+ (bNoReinit ? "set" : "not set") + ")");
		if (Again == PT_OK)
		{
			R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys after re-initialisation");
		}
		return R;
	}

	// ---- A1 version handshake ----------------------------------------------------------------------------------------
	FGateReport RunA1VersionHandshake(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A1", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		if (!R.Check(Target.Entry != nullptr, "the entry point is known"))
		{
			return R;
		}
		R.Check(Target.Entry(PT_ABI_VERSION_MAJOR) == &Api, "PT_GetBackendApi(host major " + std::to_string(PT_ABI_VERSION_MAJOR) + ") returns the table");
		R.Check(Target.Entry(PT_ABI_VERSION_MAJOR + 1) == nullptr, "PT_GetBackendApi(major + 1) returns NULL");
		R.Check(Target.Entry(0xFFFFu) == nullptr, "PT_GetBackendApi(0xFFFF) returns NULL");
		// A module built against this header writes sizeof(PtBackendApi); one built against a later v1.x header more. The
		// frozen minimum is the v1.0 table (docs/Abi.md 2).
		R.Check(Api.StructSize >= PT_ABI_V1_0_API_SIZE && Api.StructSize % sizeof(void*) == 0, "PtBackendApi.StructSize "
			+ std::to_string(Api.StructSize) + " >= the v1.0 table size " + std::to_string(PT_ABI_V1_0_API_SIZE) + " (whole entries)");
		R.Check(Api.AbiVersion == PT_ABI_VERSION, "PtBackendApi.AbiVersion == PT_ABI_VERSION (1.0)");
		R.Check(Target.Entry(0u) == nullptr, "PT_GetBackendApi(0) returns NULL (a v0.9 host or spike DLL is refused after the freeze)");
		R.Check(Api.ModuleName != nullptr && Api.ModuleName[0] != '\0', "PtBackendApi.ModuleName is set");
		std::string Error;
		R.Check(FPhysicsBackendModule::ValidateApiTable(&Api, Error), "the host accepts the table: " + Error);
		// The host refuses tables it cannot use, and accepts any v1.x table of at least the v1.0 size (v1.x appends entries:
		// a v1.0 module - the Chaos bridge - must keep loading in a later v1.x host, M6 round 2).
		PtBackendApi Copy = Api;
		Copy.StructSize = PT_ABI_V1_0_API_SIZE;
		R.Check(FPhysicsBackendModule::ValidateApiTable(&Copy, Error), "the host accepts a v1.0-sized table (StructSize " + std::to_string(PT_ABI_V1_0_API_SIZE)
			+ "): " + Error);
		R.Check(PT_API_HAS(&Copy, GetLastErrorMessage) && PT_API_HAS(&Copy, GetInfo), "PT_API_HAS sees the v1.0 entries of a v1.0-sized table");
		Copy.StructSize = PT_ABI_V1_0_API_SIZE + 2u * static_cast<uint32_t>(sizeof(void*));
		R.Check(FPhysicsBackendModule::ValidateApiTable(&Copy, Error), "the host accepts a larger table (a newer v1.x module): " + Error);
		Copy.StructSize = PT_ABI_V1_0_API_SIZE - static_cast<uint32_t>(sizeof(void*));
		R.Check(!FPhysicsBackendModule::ValidateApiTable(&Copy, Error), "the host refuses a table smaller than v1.0");
		R.Check(!PT_API_HAS(&Copy, GetLastErrorMessage) && PT_API_HAS(&Copy, GetMemoryStats), "PT_API_HAS treats an entry past StructSize as absent");
		Copy = Api;
		Copy.AbiVersion = ((PT_ABI_VERSION_MAJOR + 1u) << 16) | 0u;
		R.Check(!FPhysicsBackendModule::ValidateApiTable(&Copy, Error), "the host refuses another major");
		Copy = Api;
		Copy.GetLastErrorMessage = nullptr;
		R.Check(!FPhysicsBackendModule::ValidateApiTable(&Copy, Error), "the host refuses a NULL entry");
		R.Check(!FPhysicsBackendModule::ValidateApiTable(nullptr, Error), "the host refuses NULL (unsupported major)");
		// StructSize protocol of the info / init structures.
		PtBackendInfo Info = {};
		R.Check(Api.GetInfo(&Info) == PT_E_INVALID_ARG, "GetInfo with StructSize 0 -> PT_E_INVALID_ARG");
		struct FNewerInfo
		{
			PtBackendInfo Info;
			uint64_t AppendedByANewerHost[4];
		} Newer = {};
		Newer.Info.StructSize = sizeof(FNewerInfo);
		R.Check(Api.GetInfo(&Newer.Info) == PT_OK && Newer.Info.StructSize == sizeof(PtBackendInfo),
			"GetInfo with a larger StructSize -> PT_OK, StructSize written back as the module's size");
		PtInitParams Init = MakeInitParams();
		Init.StructSize = 8;
		R.Check(Api.InitGamePhys(&Init) == PT_E_INVALID_ARG, "InitGamePhys with a small StructSize -> PT_E_INVALID_ARG");
		Init = MakeInitParams();
		R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys");
		PtSceneDesc Desc = MakeSceneDesc();
		Desc.StructSize = sizeof(PtSceneDesc) - 8;
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_E_INVALID_ARG, "CreateScene with a small StructSize -> PT_E_INVALID_ARG");
		struct FNewerDesc
		{
			PtSceneDesc Desc;
			uint64_t AppendedByANewerHost[2];
		} NewerDesc = {};
		NewerDesc.Desc = MakeSceneDesc();
		NewerDesc.Desc.StructSize = sizeof(FNewerDesc);
		R.Check(Api.CreateScene(&NewerDesc.Desc, &Scene) == PT_OK, "CreateScene with a larger StructSize -> PT_OK");
		R.Check(Api.DestroyScene(Scene) == PT_OK, "DestroyScene");
		return R;
	}

	// ---- A2 other thread ---------------------------------------------------------------------------------------------
	FGateReport RunA2OtherThread(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A2", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		const PtInitParams Init = MakeInitParams();
		if (!R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys on the owner thread"))
		{
			return R;
		}
		const PtSceneDesc Desc = MakeSceneDesc();
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_OK, "CreateScene");
		const FTestScene Bodies = PopulateScene(Api, Scene);
		R.Check(Bodies.Result == PT_OK, "bodies: " + LastError(Api));
		for (int32 Frame = 0; Frame < 3; ++Frame)
		{
			R.Check(StepFrame(Api, Scene) == PT_OK, "frame on the owner thread");
		}
		// The reference state, then a known last error (the capacity query inside ReadAllStates sets one too).
		const std::vector<PtBodyState> StatesBefore = ReadAllStates(Api, Scene);
		const uint64 HashBefore = ReadHash(Api, Scene);
		R.Check(Api.SetOption(Scene, "a2.unknown.option", "1") == PT_E_INVALID_ARG, "an unknown option sets a last error");
		const std::string ErrorBefore = LastError(Api);

		// Every entry from another thread: PT_E_WRONG_THREAD, and no output is written (sentinels stay).
		struct FCall
		{
			const char* Name;
			std::function<PtResult()> Call;
		};
		PtBackendInfo Info = {};
		Info.StructSize = sizeof(PtBackendInfo);
		Info.Name = "sentinel";
		PtSceneId NewScene = 0xABCDu;
		const PtPhysicalMaterial Material = MakeMaterial();
		const FTestScene MoreBodies = MakeTestBodies(static_cast<uint32>(StatesBefore.size()), 0);
		const PtActorBatch Batch = MoreBodies.MakeBatch();
		const uint32 ReleaseIndex = 1;
		char Text[256];
		memset(Text, 0x5A, sizeof(Text));
		PtTextBuffer TextBuffer = { Text, sizeof(Text), 0xDEADu };
		PtFrameResult Frame = { 0xDEADu, 0xDEADu, 0xDEADu, 0xDEADu };
		PtBodyCommand Command = MakeCommand(PT_CMD_ADD_IMPULSE, 1);
		Command.Vector = PtVec3{ 0.0, 0.0, 1000.0 };
		std::vector<uint32> Indices(16, 0xDEADu);
		std::vector<PtBodyState> States(16);
		memset(States.data(), 0x5A, States.size() * sizeof(PtBodyState));
		uint32 Count = 0xDEADu;
		PtMassProperties Mass[1];
		memset(Mass, 0x5A, sizeof(Mass));
		PtStepStats Stats = {};
		Stats.StructSize = sizeof(PtStepStats);
		Stats.NumActiveBodies = 0x5A5A;
		PtDebugSnapshotRequest Request = {};
		Request.StructSize = sizeof(PtDebugSnapshotRequest);
		PtDebugSnapshotCounts Counts = {};
		memset(&Counts, 0x5A, sizeof(Counts));
		PtDebugSnapshotBuffers Buffers = {};
		uint64 Hash = 0xDEADu;
		PtMemoryStats Memory = {};
		Memory.LiveBytes = 0x5A5A;
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		// Byte copies of every output sentinel: a rejected call must leave each output byte as it was (M2 review: States and
		// Mass were filled but never compared).
		const auto Bytes = [](const void* Data, const size_t Size)
		{
			const unsigned char* Begin = static_cast<const unsigned char*>(Data);
			return std::vector<unsigned char>(Begin, Begin + Size);
		};
		const auto Same = [&Bytes](const void* Data, const size_t Size, const std::vector<unsigned char>& Sentinel) { return Bytes(Data, Size) == Sentinel; };
		const std::vector<unsigned char> InfoSentinel = Bytes(&Info, sizeof(Info));
		const std::vector<unsigned char> TextSentinel = Bytes(Text, sizeof(Text));
		const std::vector<unsigned char> TextBufferSentinel = Bytes(&TextBuffer, sizeof(TextBuffer));
		const std::vector<unsigned char> FrameSentinel = Bytes(&Frame, sizeof(Frame));
		const std::vector<uint32> IndicesSentinel = Indices;
		const std::vector<unsigned char> StatesSentinel = Bytes(States.data(), States.size() * sizeof(PtBodyState));
		const std::vector<unsigned char> MassSentinel = Bytes(Mass, sizeof(Mass));
		const std::vector<unsigned char> StatsSentinel = Bytes(&Stats, sizeof(Stats));
		const std::vector<unsigned char> CountsSentinel = Bytes(&Counts, sizeof(Counts));
		const std::vector<unsigned char> MemorySentinel = Bytes(&Memory, sizeof(Memory));
		const std::vector<FCall> Calls = {
			{ "GetInfo", [&] { return Api.GetInfo(&Info); } },
			{ "InitGamePhys", [&] { return Api.InitGamePhys(&Init); } },
			{ "TermGamePhys", [&] { return Api.TermGamePhys(); } },
			{ "CreateScene", [&] { return Api.CreateScene(&Desc, &NewScene); } },
			{ "DestroyScene", [&] { return Api.DestroyScene(Scene); } },
			{ "CreateMaterials", [&] { return Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial)); } },
			{ "CreateActors", [&] { return Api.CreateActors(Scene, &Batch); } },
			{ "ReleaseActors", [&] { return Api.ReleaseActors(Scene, &ReleaseIndex, 1); } },
			{ "CreateConstraints", [&] { return Api.CreateConstraints(Scene, nullptr, 0, sizeof(PtConstraintCreateInfo)); } },
			{ "PrepareForSimulation", [&] { return Api.PrepareForSimulation(Scene, &TextBuffer); } },
			{ "SetUpForFrame", [&] { return Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u); } },
			{ "StartFrame", [&] { return Api.StartFrame(Scene); } },
			{ "WaitPhysScenes", [&] { return Api.WaitPhysScenes(Scene); } },
			{ "EndFrame", [&] { return Api.EndFrame(Scene, &Frame); } },
			{ "ApplyBodyCommands", [&] { return Api.ApplyBodyCommands(Scene, &Command, 1, sizeof(PtBodyCommand)); } },
			{ "ReadBodyStates", [&] { return Api.ReadBodyStates(Scene, PT_READ_ALL, Indices.data(), States.data(), 16, &Count); } },
			{ "ReadMassProperties", [&] { return Api.ReadMassProperties(Scene, 0, 1, Mass); } },
			{ "GetStepStats", [&] { return Api.GetStepStats(Scene, &Stats); } },
			{ "CaptureDebugSnapshot", [&] { return Api.CaptureDebugSnapshot(Scene, &Request, &Counts); } },
			{ "ReadDebugSnapshot", [&] { return Api.ReadDebugSnapshot(Scene, &Buffers); } },
			{ "ComputeNativeStateHash", [&] { return Api.ComputeNativeStateHash(Scene, &Hash); } },
			{ "SetOption", [&] { return Api.SetOption(Scene, "a2.other.option", "1"); } },
			{ "DumpEffectiveSettings", [&] { return Api.DumpEffectiveSettings(Scene, &TextBuffer); } },
			{ "GetMemoryStats", [&] { return Api.GetMemoryStats(Scene, &Memory); } },
			{ "GetLastErrorMessage", [&] { return Api.GetLastErrorMessage(&TextBuffer); } },
		};
		R.Check(Calls.size() == 25, "all 25 entries are exercised");
		std::vector<PtResult> Results(Calls.size(), PT_OK);
		std::thread Other([&Calls, &Results]
		{
			for (size_t Index = 0; Index < Calls.size(); ++Index)
			{
				Results[Index] = Calls[Index].Call();
			}
		});
		Other.join();
		for (size_t Index = 0; Index < Calls.size(); ++Index)
		{
			R.Check(Results[Index] == PT_E_WRONG_THREAD, std::string(Calls[Index].Name) + " from another thread -> PT_E_WRONG_THREAD (got "
				+ GetPtResultName(Results[Index]) + ")");
		}
		R.Check(std::strcmp(Info.Name, "sentinel") == 0 && NewScene == 0xABCDu && Frame.NumStepsTaken == 0xDEADu && Count == 0xDEADu && Hash == 0xDEADu
				&& TextBuffer.Length == 0xDEADu && static_cast<unsigned char>(Text[0]) == 0x5A && Stats.NumActiveBodies == 0x5A5A && Memory.LiveBytes == 0x5A5A
				&& Indices[0] == 0xDEADu,
			"no output parameter was written by a rejected call");
		// Every byte of every output buffer (M3: the A2 review minor).
		R.Check(Same(&Info, sizeof(Info), InfoSentinel), "PtBackendInfo output unchanged (every byte)");
		R.Check(Same(Text, sizeof(Text), TextSentinel) && Same(&TextBuffer, sizeof(TextBuffer), TextBufferSentinel), "text bytes and PtTextBuffer unchanged");
		R.Check(Same(&Frame, sizeof(Frame), FrameSentinel), "PtFrameResult unchanged");
		R.Check(Indices == IndicesSentinel, "ReadBodyStates OutIndices unchanged (all 16)");
		R.Check(Same(States.data(), States.size() * sizeof(PtBodyState), StatesSentinel), "ReadBodyStates OutStates unchanged (all 16, every byte)");
		R.Check(Same(Mass, sizeof(Mass), MassSentinel), "ReadMassProperties output unchanged");
		R.Check(Same(&Stats, sizeof(Stats), StatsSentinel), "PtStepStats unchanged");
		R.Check(Same(&Counts, sizeof(Counts), CountsSentinel), "PtDebugSnapshotCounts unchanged");
		R.Check(Same(&Memory, sizeof(Memory), MemorySentinel), "PtMemoryStats unchanged");
		// The owner thread sees no change.
		R.Check(LastError(Api) == ErrorBefore, "the last error is unchanged");
		R.Check(SameStates(ReadAllStates(Api, Scene), StatesBefore), "every body state is unchanged (count, pose, velocity, object state)");
		R.Check(ReadHash(Api, Scene) == HashBefore, "the native state hash is unchanged");
		R.Check(Api.StartFrame(Scene) == PT_E_BAD_STATE, "the phase is still Idle (the other thread's SetUpForFrame did nothing)");
		R.Check(StepFrame(Api, Scene) == PT_OK, "the scene still steps on the owner thread");
		R.Check(Api.DestroyScene(Scene) == PT_OK, "DestroyScene on the owner thread");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys on the owner thread");
		return R;
	}

	// ---- A3 phase state machine --------------------------------------------------------------------------------------
	FGateReport RunA3PhaseStateMachine(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A3", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		const PtInitParams Init = MakeInitParams();
		R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys");
		const PtSceneDesc Desc = MakeSceneDesc();
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_OK, "CreateScene");
		const FTestScene Bodies = PopulateScene(Api, Scene);
		R.Check(Bodies.Result == PT_OK, "bodies: " + LastError(Api));
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		PtFrameResult Frame = {};

		// Every Idle-only call, for the phases that forbid it.
		const auto CheckIdleOnlyRejected = [&](const char* Phase)
		{
			const PtPhysicalMaterial Material = MakeMaterial();
			const FTestScene More = MakeTestBodies(Bodies.NumBodies, 0);
			const PtActorBatch Batch = More.MakeBatch();
			PtBodyCommand Command = MakeCommand(PT_CMD_WAKE_UP, 1);
			std::vector<PtBodyState> States(16);
			uint32 Count = 0;
			PtMassProperties Mass = {};
			PtStepStats Stats = {};
			Stats.StructSize = sizeof(PtStepStats);
			uint64 Hash = 0;
			char Text[64];
			PtTextBuffer Buffer = { Text, sizeof(Text), 0 };
			const uint32 Release = 1;
			const std::string P(Phase);
			R.Check(Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial)) == PT_E_BAD_STATE, "CreateMaterials in " + P);
			R.Check(Api.CreateActors(Scene, &Batch) == PT_E_BAD_STATE, "CreateActors in " + P);
			R.Check(Api.ReleaseActors(Scene, &Release, 1) == PT_E_BAD_STATE, "ReleaseActors in " + P);
			R.Check(Api.PrepareForSimulation(Scene, nullptr) == PT_E_BAD_STATE, "PrepareForSimulation in " + P);
			R.Check(Api.ApplyBodyCommands(Scene, &Command, 1, sizeof(PtBodyCommand)) == PT_E_BAD_STATE, "ApplyBodyCommands in " + P);
			R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States.data(), 16, &Count) == PT_E_BAD_STATE, "ReadBodyStates in " + P);
			R.Check(Api.ReadMassProperties(Scene, 0, 1, &Mass) == PT_E_BAD_STATE, "ReadMassProperties in " + P);
			R.Check(Api.GetStepStats(Scene, &Stats) == PT_E_BAD_STATE, "GetStepStats in " + P);
			R.Check(Api.ComputeNativeStateHash(Scene, &Hash) == PT_E_BAD_STATE, "ComputeNativeStateHash in " + P);
			R.Check(Api.DumpEffectiveSettings(Scene, &Buffer) == PT_E_BAD_STATE, "DumpEffectiveSettings in " + P);
			R.Check(Api.SetOption(Scene, "a3.option", "1") == PT_E_BAD_STATE, "SetOption in " + P);
			R.Check(Api.DestroyScene(Scene) == PT_E_BAD_STATE, "DestroyScene in " + P);
		};

		// Idle: only SetUpForFrame advances.
		R.Check(Api.StartFrame(Scene) == PT_E_BAD_STATE, "StartFrame in Idle");
		R.Check(Api.WaitPhysScenes(Scene) == PT_E_BAD_STATE, "WaitPhysScenes in Idle");
		R.Check(Api.EndFrame(Scene, &Frame) == PT_E_BAD_STATE, "EndFrame in Idle");
		// SetUp
		R.Check(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_OK, "SetUpForFrame in Idle -> SetUp");
		R.Check(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_E_BAD_STATE, "SetUpForFrame in SetUp");
		R.Check(Api.WaitPhysScenes(Scene) == PT_E_BAD_STATE, "WaitPhysScenes in SetUp");
		R.Check(Api.EndFrame(Scene, &Frame) == PT_E_BAD_STATE, "EndFrame in SetUp");
		CheckIdleOnlyRejected("SetUp");
		// Started
		R.Check(Api.StartFrame(Scene) == PT_OK, "StartFrame in SetUp -> Started");
		R.Check(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_E_BAD_STATE, "SetUpForFrame in Started");
		R.Check(Api.StartFrame(Scene) == PT_E_BAD_STATE, "StartFrame in Started");
		R.Check(Api.EndFrame(Scene, &Frame) == PT_E_BAD_STATE, "EndFrame in Started");
		CheckIdleOnlyRejected("Started");
		// Waited
		R.Check(Api.WaitPhysScenes(Scene) == PT_OK, "WaitPhysScenes in Started -> Waited");
		R.Check(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_E_BAD_STATE, "SetUpForFrame in Waited");
		R.Check(Api.StartFrame(Scene) == PT_E_BAD_STATE, "StartFrame in Waited");
		R.Check(Api.WaitPhysScenes(Scene) == PT_E_BAD_STATE, "WaitPhysScenes in Waited");
		R.Check(Api.EndFrame(Scene, nullptr) == PT_E_INVALID_ARG, "EndFrame(NULL) in Waited -> PT_E_INVALID_ARG, phase unchanged");
		CheckIdleOnlyRejected("Waited");
		// Back to Idle
		Frame = {};
		R.Check(Api.EndFrame(Scene, &Frame) == PT_OK && Frame.NumStepsTaken == 1, "EndFrame in Waited -> Idle, one step taken");
		std::vector<PtBodyState> States = ReadAllStates(Api, Scene);
		R.Check(States.size() == Bodies.NumBodies, "reads work again in Idle");
		R.Check(StepFrame(Api, Scene) == PT_OK, "a full frame after the rejected calls");
		// A rejected call does not change the body count or the phase: no extra body, no release happened (PT_READ_ALL is
		// dense, a released body would read as UNINITIALIZED).
		States = ReadAllStates(Api, Scene);
		R.Check(States.size() == Bodies.NumBodies && States[1].ObjectState != PT_OBJECT_STATE_UNINITIALIZED,
			"the rejected CreateActors / ReleaseActors changed nothing");
		// TermGamePhys mid-frame destroys the scene.
		R.Check(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u) == PT_OK && Api.StartFrame(Scene) == PT_OK, "a frame left in Started");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys with a scene in Started -> PT_OK");
		R.Check(Api.StartFrame(Scene) == PT_E_BAD_STATE, "after TermGamePhys every scene call -> PT_E_BAD_STATE");
		return R;
	}

	// ---- A4 negative cases -------------------------------------------------------------------------------------------
	FGateReport RunA4NegativeCases(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A4", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		const double NaN = std::numeric_limits<double>::quiet_NaN();
		const double Inf = std::numeric_limits<double>::infinity();

		struct FCase
		{
			const char* Name;
			PtResult Expected;
			std::function<PtResult()> Call;
		};
		int32 NumCases = 0;
		const auto RunCases = [&R, &NumCases](const std::vector<FCase>& Cases, const std::function<void(const char*)>& Verify)
		{
			for (const FCase& Case : Cases)
			{
				++NumCases;
				const PtResult Got = Case.Call();
				R.Check(Got == Case.Expected, std::string(Case.Name) + " -> " + GetPtResultName(Case.Expected) + " (got " + GetPtResultName(Got) + ")");
				Verify(Case.Name);
			}
		};

		// Module level, before InitGamePhys (the module must stay Loaded).
		PtInitParams BadInit = MakeInitParams();
		const PtKeyValue UnknownModuleOption = { "a4.unknown.module.option", "1" };
		RunCases({
				{ "InitGamePhys(NULL)", PT_E_INVALID_ARG, [&] { return Api.InitGamePhys(nullptr); } },
				{ "InitGamePhys MaxThreads 0", PT_E_INVALID_ARG, [&] { BadInit = MakeInitParams(); BadInit.MaxThreads = 0; return Api.InitGamePhys(&BadInit); } },
				{ "InitGamePhys unknown module option", PT_E_INVALID_ARG, [&] { BadInit = MakeInitParams(); BadInit.Options = &UnknownModuleOption; BadInit.NumOptions = 1; return Api.InitGamePhys(&BadInit); } },
				{ "InitGamePhys NumOptions 2 with Options NULL", PT_E_INVALID_ARG, [&] { BadInit = MakeInitParams(); BadInit.NumOptions = 2; return Api.InitGamePhys(&BadInit); } },
			},
			[&](const char* Name)
			{
				PtSceneId Probe = 0;
				const PtSceneDesc Desc = MakeSceneDesc();
				R.Check(Api.CreateScene(&Desc, &Probe) == PT_E_BAD_STATE, std::string("the module is still not initialised after ") + Name);
			});

		const PtInitParams Init = MakeInitParams();
		if (!R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys"))
		{
			return R;
		}
		const PtSceneDesc GoodDesc = MakeSceneDesc();
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&GoodDesc, &Scene) == PT_OK, "CreateScene");
		const FTestScene Bodies = PopulateScene(Api, Scene);
		R.Check(Bodies.Result == PT_OK, "bodies: " + LastError(Api));
		R.Check(StepFrame(Api, Scene) == PT_OK, "one frame");
		// An untouched twin: any part of a rejected call that leaked into the scene (an impulse that only acts at the next
		// step, a velocity) shows up as a difference after the final common frame.
		PtSceneId Twin = 0;
		R.Check(Api.CreateScene(&GoodDesc, &Twin) == PT_OK && PopulateScene(Api, Twin).Result == PT_OK && StepFrame(Api, Twin) == PT_OK, "twin scene");
		PtSceneId Stale = 0;
		R.Check(Api.CreateScene(&GoodDesc, &Stale) == PT_OK && Api.DestroyScene(Stale) == PT_OK, "a destroyed scene id for the stale-handle cases");
		// One released body in both scenes (the last one, index NumBodies - 1) for the released-body cases.
		const uint32 Released = Bodies.NumBodies - 1;
		R.Check(Api.ReleaseActors(Scene, &Released, 1) == PT_OK && Api.ReleaseActors(Twin, &Released, 1) == PT_OK, "release body " + std::to_string(Released)
			+ " in both scenes");
		const std::vector<PtBodyState> Reference = ReadAllStates(Api, Scene);
		const uint64 ReferenceHash = ReadHash(Api, Scene);
		const auto VerifyUnchanged = [&](const char* Name)
		{
			R.Check(ReadHash(Api, Scene) == ReferenceHash && ReadAllStates(Api, Scene).size() == Reference.size(),
				std::string("the scene is unchanged after ") + Name);
		};

		PtSceneDesc Desc = GoodDesc;
		PtSceneId OutScene = 0;
		const PtKeyValue UnknownSceneOption = { "a4.unknown.scene.option", "1" };
		const auto WithDesc = [&](const std::function<void(PtSceneDesc&)>& Edit)
		{
			Desc = GoodDesc;
			Edit(Desc);
			return Api.CreateScene(&Desc, &OutScene);
		};
		PtPhysicalMaterial Material = MakeMaterial();
		const auto WithMaterial = [&](const std::function<void(PtPhysicalMaterial&)>& Edit)
		{
			Material = MakeMaterial();
			Edit(Material);
			return Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial));
		};
		FTestScene Extra = MakeTestBodies(Bodies.NumBodies, 0);
		const auto WithActors = [&](const std::function<void(FTestScene&, PtActorBatch&)>& Edit)
		{
			Extra = MakeTestBodies(Bodies.NumBodies, 0);
			PtActorBatch Batch = Extra.MakeBatch();
			Edit(Extra, Batch);
			return Api.CreateActors(Scene, &Batch);
		};
		PtBodyCommand Commands[2] = {};
		const auto WithCommand = [&](const std::function<void(PtBodyCommand&)>& Edit)
		{
			Commands[0] = MakeCommand(PT_CMD_ADD_IMPULSE, Bodies.FirstDynamic);
			Commands[0].Vector = PtVec3{ 0.0, 0.0, 100.0 };
			Edit(Commands[0]);
			return Api.ApplyBodyCommands(Scene, Commands, 1, sizeof(PtBodyCommand));
		};
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		const PtVec3 BadGravity = { 0.0, 0.0, Inf };
		std::vector<PtBodyState> States(64);
		uint32 Count = 0;
		PtMassProperties Mass[8] = {};
		PtStepStats Stats = {};
		PtTextBuffer BadText = { nullptr, 10, 0 };
		const uint32 BadRelease = 99;
		const uint32 RepeatedRelease[2] = { Bodies.FirstDynamic, Bodies.FirstDynamic };
		PtConstraintCreateInfo Joint = {};

		RunCases({
				// scenes
				{ "CreateScene(NULL desc)", PT_E_INVALID_ARG, [&] { return Api.CreateScene(nullptr, &OutScene); } },
				{ "CreateScene(NULL OutScene)", PT_E_INVALID_ARG, [&] { return Api.CreateScene(&GoodDesc, nullptr); } },
				{ "CreateScene AsyncFixedTimeStepSize 0", PT_E_INVALID_ARG, [&] { return WithDesc([](PtSceneDesc& D) { D.AsyncFixedTimeStepSize = 0.0; }); } },
				{ "CreateScene AsyncFixedTimeStepSize -0.5", PT_E_INVALID_ARG, [&] { return WithDesc([](PtSceneDesc& D) { D.AsyncFixedTimeStepSize = -0.5; }); } },
				{ "CreateScene AsyncFixedTimeStepSize NaN", PT_E_INVALID_ARG, [&] { return WithDesc([NaN](PtSceneDesc& D) { D.AsyncFixedTimeStepSize = NaN; }); } },
				{ "CreateScene gravity NaN", PT_E_INVALID_ARG, [&] { return WithDesc([NaN](PtSceneDesc& D) { D.Gravity.Z = NaN; }); } },
				{ "CreateScene NominalDeltaTime 0", PT_E_INVALID_ARG, [&] { return WithDesc([](PtSceneDesc& D) { D.NominalDeltaTime = 0.0; }); } },
				{ "CreateScene NominalDeltaTime inf", PT_E_INVALID_ARG, [&] { return WithDesc([Inf](PtSceneDesc& D) { D.NominalDeltaTime = Inf; }); } },
				{ "CreateScene NumThreads 0", PT_E_INVALID_ARG, [&] { return WithDesc([](PtSceneDesc& D) { D.NumThreads = 0; }); } },
				{ "CreateScene RunMode 7", PT_E_INVALID_ARG, [&] { return WithDesc([](PtSceneDesc& D) { D.RunMode = 7; }); } },
				{ "CreateScene unknown scene option", PT_E_INVALID_ARG, [&] { return WithDesc([&UnknownSceneOption](PtSceneDesc& D) { D.Options = &UnknownSceneOption; D.NumOptions = 1; }); } },
				{ "DestroyScene(0)", PT_E_INVALID_HANDLE, [&] { return Api.DestroyScene(0); } },
				{ "DestroyScene(stale id)", PT_E_INVALID_HANDLE, [&] { return Api.DestroyScene(Stale); } },
				{ "DestroyScene(slot 99)", PT_E_INVALID_HANDLE, [&] { return Api.DestroyScene((1ull << 32) | 99u); } },
				// materials
				{ "CreateMaterials stride 8", PT_E_INVALID_ARG, [&] { Material = MakeMaterial(); return Api.CreateMaterials(Scene, &Material, 1, 8); } },
				{ "CreateMaterials friction NaN", PT_E_INVALID_ARG, [&] { return WithMaterial([NaN](PtPhysicalMaterial& M) { M.Friction = NaN; }); } },
				{ "CreateMaterials friction -1", PT_E_INVALID_ARG, [&] { return WithMaterial([](PtPhysicalMaterial& M) { M.Friction = -1.0; }); } },
				{ "CreateMaterials combine mode 7", PT_E_INVALID_ARG, [&] { return WithMaterial([](PtPhysicalMaterial& M) { M.FrictionCombineMode = 7; }); } },
				// actors
				{ "CreateActors(NULL batch)", PT_E_INVALID_ARG, [&] { return Api.CreateActors(Scene, nullptr); } },
				{ "CreateActors MaterialIndex 99", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Shapes[0].MaterialIndex = 99; }); } },
				{ "CreateActors shape range out of bounds", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[0].FirstShape = 1000; }); } },
				{ "CreateActors non-unit quaternion", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].ActorTransform.Rotation = PtQuat{ 0.0, 0.0, 0.0, 2.0 }; }); } },
				{ "CreateActors dynamic mass 0", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].Mass = 0.0; }); } },
				{ "CreateActors dynamic mass NaN", PT_E_INVALID_ARG, [&] { return WithActors([NaN](FTestScene& S, PtActorBatch&) { S.Actors[1].Mass = NaN; }); } },
				{ "CreateActors negative inertia", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].InertiaDiagonal.Y = -1.0; }); } },
				{ "CreateActors ObjectType 32", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].ObjectType = 32; }); } },
				{ "CreateActors BodyIndex out of creation order", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].BodyIndex += 5; }); } },
				{ "CreateActors Motion 7", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Actors[1].Motion = 7; }); } },
				{ "CreateActors shape type 9", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Shapes[1].Type = 9; }); } },
				{ "CreateActors sphere radius -1", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene& S, PtActorBatch&) { S.Shapes[1].Radius = -1.0; }); } },
				{ "CreateActors initial velocity inf", PT_E_INVALID_ARG, [&] { return WithActors([Inf](FTestScene& S, PtActorBatch&) { S.Actors[1].InitialLinearVelocity.X = Inf; }); } },
				{ "CreateActors ActorStride 4", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene&, PtActorBatch& B) { B.ActorStride = 4; }); } },
				{ "CreateActors misaligned ActorStride", PT_E_INVALID_ARG, [&] { return WithActors([](FTestScene&, PtActorBatch& B) { B.ActorStride = sizeof(PtActorCreateInfo) + 4; }); } },
				// commands (the batch is validated as a whole before anything is applied)
				{ "ApplyBodyCommands body 99", PT_E_INVALID_ARG, [&] { return WithCommand([](PtBodyCommand& C) { C.Body = 99; }); } },
				{ "ApplyBodyCommands type 99", PT_E_INVALID_ARG, [&] { return WithCommand([](PtBodyCommand& C) { C.Type = 99; }); } },
				{ "ApplyBodyCommands NaN vector", PT_E_INVALID_ARG, [&] { return WithCommand([NaN](PtBodyCommand& C) { C.Vector.Y = NaN; }); } },
				{ "ApplyBodyCommands SET_GLOBAL_POSE non-unit quaternion", PT_E_INVALID_ARG, [&] { return WithCommand([](PtBodyCommand& C) { C.Type = PT_CMD_SET_GLOBAL_POSE; C.Transform.Rotation = PtQuat{ 0.5, 0.0, 0.0, 0.5 }; }); } },
				{ "ApplyBodyCommands unknown flags", PT_E_INVALID_ARG, [&] { return WithCommand([](PtBodyCommand& C) { C.Flags = 0x80u; }); } },
				{ "ApplyBodyCommands [valid, invalid] (atomic)", PT_E_INVALID_ARG, [&]
					{
						Commands[0] = MakeCommand(PT_CMD_ADD_IMPULSE, Bodies.FirstDynamic);
						Commands[0].Vector = PtVec3{ 0.0, 0.0, 1.0e5 };
						Commands[1] = MakeCommand(PT_CMD_ADD_IMPULSE, 99);
						return Api.ApplyBodyCommands(Scene, Commands, 2, sizeof(PtBodyCommand));
					} },
				{ "ApplyBodyCommands on a stale scene", PT_E_INVALID_HANDLE, [&] { Commands[0] = MakeCommand(PT_CMD_WAKE_UP, 0); return Api.ApplyBodyCommands(Stale, Commands, 1, sizeof(PtBodyCommand)); } },
				{ "ApplyBodyCommands on a released body", PT_E_INVALID_ARG, [&] { return WithCommand([Released](PtBodyCommand& C) { C.Body = Released; }); } },
				// reads
				{ "ReadBodyStates ReadFlags 7", PT_E_INVALID_ARG, [&] { return Api.ReadBodyStates(Scene, 7u, nullptr, States.data(), 64, &Count); } },
				{ "ReadBodyStates OutCount NULL", PT_E_INVALID_ARG, [&] { return Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States.data(), 64, nullptr); } },
				{ "ReadBodyStates capacity 4 with OutStates NULL", PT_E_INVALID_ARG, [&] { return Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, nullptr, 4, &Count); } },
				{ "ReadBodyStates ACTIVE_ONLY capacity 64 with OutIndices NULL", PT_E_INVALID_ARG, [&] { return Api.ReadBodyStates(Scene, PT_READ_ACTIVE_ONLY, nullptr, States.data(), 64, &Count); } },
				{ "ReadMassProperties range past the end", PT_E_INVALID_ARG, [&] { return Api.ReadMassProperties(Scene, Bodies.NumBodies - 1, 2, Mass); } },
				{ "ReadMassProperties First + Num overflow", PT_E_INVALID_ARG, [&] { return Api.ReadMassProperties(Scene, 0xFFFFFFFFu, 2, Mass); } },
				{ "ReleaseActors index 99", PT_E_INVALID_ARG, [&] { return Api.ReleaseActors(Scene, &BadRelease, 1); } },
				{ "ReleaseActors [i, i] (a live body listed twice)", PT_E_INVALID_ARG, [&] { return Api.ReleaseActors(Scene, RepeatedRelease, 2); } },
				{ "ReleaseActors of a released body", PT_E_INVALID_ARG, [&] { return Api.ReleaseActors(Scene, &Released, 1); } },
				{ "ReadMassProperties of a released body", PT_E_INVALID_ARG, [&] { return Api.ReadMassProperties(Scene, Released, 1, Mass); } },
				{ "CreateConstraints body out of range", PT_E_INVALID_ARG, [&] { Joint = MakeJoint(99, PT_INDEX_NONE); return Api.CreateConstraints(Scene, &Joint, 1, sizeof(Joint)); } },
				// frame
				{ "SetUpForFrame dt NaN", PT_E_INVALID_ARG, [&] { return Api.SetUpForFrame(Scene, &Gravity, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, 1, 0u); } },
				{ "SetUpForFrame dt -1", PT_E_INVALID_ARG, [&] { return Api.SetUpForFrame(Scene, &Gravity, -1.0f, 0.0f, 0.0f, 0.0f, 1, 0u); } },
				{ "SetUpForFrame MaxSubsteps 0", PT_E_INVALID_ARG, [&] { return Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 0, 0u); } },
				{ "SetUpForFrame bSubstepping 2", PT_E_INVALID_ARG, [&] { return Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 2u); } },
				{ "SetUpForFrame NewGrav inf", PT_E_INVALID_ARG, [&] { return Api.SetUpForFrame(Scene, &BadGravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u); } },
				// options and outputs
				{ "SetOption(scene, unknown key)", PT_E_INVALID_ARG, [&] { return Api.SetOption(Scene, "a4.unknown", "1"); } },
				{ "SetOption(module, NULL key)", PT_E_INVALID_ARG, [&] { return Api.SetOption(0, nullptr, "1"); } },
				{ "GetStepStats(NULL)", PT_E_INVALID_ARG, [&] { return Api.GetStepStats(Scene, nullptr); } },
				{ "GetStepStats StructSize 4", PT_E_INVALID_ARG, [&] { Stats = {}; Stats.StructSize = 4; return Api.GetStepStats(Scene, &Stats); } },
				{ "ComputeNativeStateHash(NULL)", PT_E_INVALID_ARG, [&] { return Api.ComputeNativeStateHash(Scene, nullptr); } },
				{ "DumpEffectiveSettings(NULL)", PT_E_INVALID_ARG, [&] { return Api.DumpEffectiveSettings(Scene, nullptr); } },
				{ "DumpEffectiveSettings Data NULL with Capacity 10", PT_E_INVALID_ARG, [&] { return Api.DumpEffectiveSettings(Scene, &BadText); } },
				{ "GetMemoryStats(NULL)", PT_E_INVALID_ARG, [&] { return Api.GetMemoryStats(Scene, nullptr); } },
				{ "GetLastErrorMessage Data NULL with Capacity 10", PT_E_INVALID_ARG, [&] { return Api.GetLastErrorMessage(&BadText); } },
			},
			VerifyUnchanged);
		R.Note(std::to_string(NumCases) + " negative cases");
		R.Check(NumCases >= 30, "about 30 negative cases or more");
		R.Check(StepFrame(Api, Scene) == PT_OK && StepFrame(Api, Twin) == PT_OK, "the scene still steps after every rejected call");
		R.Check(SameStates(ReadAllStates(Api, Scene), ReadAllStates(Api, Twin)) && ReadHash(Api, Scene) == ReadHash(Api, Twin),
			"after one more frame the scene equals its untouched twin (no rejected call had any effect)");
		R.Check(Api.DestroyScene(Twin) == PT_OK, "DestroyScene (twin)");
		R.Check(Api.DestroyScene(Scene) == PT_OK, "DestroyScene");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys");
		return R;
	}

	// ---- A5 capacity / stride ----------------------------------------------------------------------------------------
	FGateReport RunA5CapacityAndStride(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A5", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		const PtInitParams Init = MakeInitParams();
		R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys");
		const PtSceneDesc Desc = MakeSceneDesc();
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_OK, "CreateScene");
		const FTestScene Bodies = PopulateScene(Api, Scene);
		R.Check(Bodies.Result == PT_OK, "bodies: " + LastError(Api));
		const uint32 N = Bodies.NumBodies;

		// ReadBodyStates: query, too small, exact, larger.
		uint32 Count = 0;
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, nullptr, 0, &Count) == PT_E_CAPACITY && Count == N, "ReadBodyStates(capacity 0) -> PT_E_CAPACITY + the count");
		std::vector<PtBodyState> States(N + 4);
		std::vector<uint32> Indices(N + 4, 0xA5A5A5A5u);
		memset(States.data(), 0xA5, States.size() * sizeof(PtBodyState));
		const std::vector<PtBodyState> Sentinel = States;
		Count = 0;
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, Indices.data(), States.data(), N - 1, &Count) == PT_E_CAPACITY && Count == N,
			"ReadBodyStates(capacity N - 1) -> PT_E_CAPACITY + N");
		R.Check(std::memcmp(States.data(), Sentinel.data(), States.size() * sizeof(PtBodyState)) == 0 && Indices[0] == 0xA5A5A5A5u,
			"nothing is written on PT_E_CAPACITY");
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, Indices.data(), States.data(), N, &Count) == PT_OK && Count == N, "ReadBodyStates(capacity N) -> PT_OK");
		bool bIndicesInOrder = true;
		for (uint32 Index = 0; Index < N; ++Index)
		{
			bIndicesInOrder = bIndicesInOrder && Indices[Index] == Index;
		}
		R.Check(bIndicesInOrder, "PT_READ_ALL returns bodies in index order");
		memset(States.data(), 0xA5, States.size() * sizeof(PtBodyState));
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States.data(), N + 4, &Count) == PT_OK && Count == N
				&& std::memcmp(&States[N], &Sentinel[N], 4 * sizeof(PtBodyState)) == 0,
			"ReadBodyStates(capacity N + 4) -> PT_OK, N written, the tail untouched, OutIndices may be NULL");
		// ACTIVE_ONLY after a frame: at least every awake dynamic body.
		R.Check(StepFrame(Api, Scene) == PT_OK, "one frame");
		PtStepStats Stats = {};
		Stats.StructSize = sizeof(PtStepStats);
		R.Check(Api.GetStepStats(Scene, &Stats) == PT_OK, "GetStepStats");
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ACTIVE_ONLY, nullptr, nullptr, 0, &Count) == PT_E_CAPACITY && static_cast<int32>(Count) >= Stats.NumActiveBodies,
			"ReadBodyStates(ACTIVE_ONLY, capacity 0) counts at least the awake bodies");

		// After a release PT_READ_ALL stays dense (index order, the released body as UNINITIALIZED with a zero state), so
		// OutIndices may stay NULL; PT_READ_ACTIVE_ONLY never reports the released body.
		const uint32 Released = Bodies.FirstDynamic;
		R.Check(Api.ReleaseActors(Scene, &Released, 1) == PT_OK, "ReleaseActors (one body)");
		std::fill(Indices.begin(), Indices.end(), 0xA5A5A5A5u);
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ALL, Indices.data(), States.data(), N, &Count) == PT_OK && Count == N, "PT_READ_ALL after a release -> all N bodies");
		bool bDense = true;
		for (uint32 Index = 0; Index < N; ++Index)
		{
			bDense = bDense && Indices[Index] == Index;
		}
		const PtBodyState& Gone = States[Released];
		R.Check(bDense && Gone.ObjectState == PT_OBJECT_STATE_UNINITIALIZED && Gone.ActorTransform.Rotation.W == 1.0 && Gone.ActorTransform.Translation.Z == 0.0
				&& Gone.LinearVelocity.Z == 0.0,
			"PT_READ_ALL is dense: the released body reads as UNINITIALIZED with a zero pose and velocity");
		R.Check(StepFrame(Api, Scene) == PT_OK, "a frame after the release");
		R.Check(Api.ReadBodyStates(Scene, PT_READ_ACTIVE_ONLY, Indices.data(), States.data(), N, &Count) == PT_OK && Count >= 1, "PT_READ_ACTIVE_ONLY with indices");
		bool bSkipsReleased = true;
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			bSkipsReleased = bSkipsReleased && Indices[Index] != Released;
		}
		R.Check(bSkipsReleased, "PT_READ_ACTIVE_ONLY does not report the released body");

		// Text: DumpEffectiveSettings measure / too small / exact.
		PtTextBuffer Query = {};
		R.Check(Api.DumpEffectiveSettings(Scene, &Query) == PT_E_CAPACITY && Query.Length > 1, "DumpEffectiveSettings(NULL, 0) -> PT_E_CAPACITY + bytes needed");
		std::vector<char> Text(Query.Length + 8, '\x5A');
		PtTextBuffer Small = { Text.data(), Query.Length - 1, 0 };
		R.Check(Api.DumpEffectiveSettings(Scene, &Small) == PT_E_CAPACITY && Small.Length == Query.Length, "DumpEffectiveSettings(needed - 1) -> PT_E_CAPACITY");
		R.Check(Text[0] == '\x5A' && Text[Query.Length - 2] == '\x5A', "nothing is written on PT_E_CAPACITY (text)");
		PtTextBuffer Exact = { Text.data(), Query.Length, 0 };
		R.Check(Api.DumpEffectiveSettings(Scene, &Exact) == PT_OK && Exact.Length == Query.Length && std::strlen(Text.data()) + 1 == Query.Length,
			"DumpEffectiveSettings(needed) -> PT_OK, Length = strlen + 1");
		// GetLastErrorMessage: query, truncated, full.
		R.Check(Api.SetOption(Scene, "a5.unknown.option.with.a.long.name", "1") == PT_E_INVALID_ARG, "an unknown option sets a last error");
		PtTextBuffer ErrorQuery = {};
		R.Check(Api.GetLastErrorMessage(&ErrorQuery) == PT_E_CAPACITY && ErrorQuery.Length > 5, "GetLastErrorMessage(NULL, 0) -> PT_E_CAPACITY + length");
		char Tiny[5];
		PtTextBuffer Truncated = { Tiny, sizeof(Tiny), 0 };
		R.Check(Api.GetLastErrorMessage(&Truncated) == PT_W_TRUNCATED && Truncated.Length == ErrorQuery.Length && std::strlen(Tiny) == 4,
			"GetLastErrorMessage(5 bytes) -> PT_W_TRUNCATED, 4 characters + NUL");
		std::vector<char> Full(ErrorQuery.Length);
		PtTextBuffer FullBuffer = { Full.data(), ErrorQuery.Length, 0 };
		R.Check(Api.GetLastErrorMessage(&FullBuffer) == PT_OK && std::strstr(Full.data(), "a5.unknown.option") != nullptr, "GetLastErrorMessage(full) -> PT_OK");
		// PrepareForSimulation truncates (one-shot).
		char Little[4];
		PtTextBuffer LittleBuffer = { Little, sizeof(Little), 0 };
		const PtResult Prepared = Api.PrepareForSimulation(Scene, &LittleBuffer);
		R.Check(Prepared == PT_W_TRUNCATED || (Prepared == PT_OK && LittleBuffer.Length <= sizeof(Little)), "PrepareForSimulation(4 bytes) -> PT_W_TRUNCATED or a fitting text");
		R.Check(Api.PrepareForSimulation(Scene, nullptr) >= 0, "PrepareForSimulation(NULL) succeeds");
		R.Check(Api.DestroyScene(Scene) == PT_OK, "DestroyScene");

		// Strides: the same scene through padded arrays (a newer host's larger structs) gives the same result.
		PtSceneId Tight = 0;
		PtSceneId Padded = 0;
		R.Check(Api.CreateScene(&Desc, &Tight) == PT_OK && Api.CreateScene(&Desc, &Padded) == PT_OK, "two scenes");
		const FTestScene TightBodies = PopulateScene(Api, Tight);
		const FTestScene PaddedBodies = PopulateScenePadded(Api, Padded);
		R.Check(TightBodies.Result == PT_OK && PaddedBodies.Result == PT_OK, "tight and padded creation: " + LastError(Api));
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			PtBodyCommand Command = MakeCommand(PT_CMD_ADD_IMPULSE, TightBodies.FirstDynamic);
			Command.Vector = PtVec3{ 10.0 * Frame, 0.0, 50.0 };
			struct FPaddedCommand
			{
				PtBodyCommand Command;
				uint64 Tail;
			} PaddedCommands[2] = { { Command, 0x1111 }, { Command, 0x2222 } };
			PaddedCommands[1].Command.Type = PT_CMD_ADD_ANGULAR_VELOCITY;
			PaddedCommands[1].Command.Vector = PtVec3{ 0.0, 0.1, 0.0 };
			const PtBodyCommand TightCommands[2] = { PaddedCommands[0].Command, PaddedCommands[1].Command };
			R.Check(Api.ApplyBodyCommands(Tight, TightCommands, 2, sizeof(PtBodyCommand)) == PT_OK, "tight commands");
			R.Check(Api.ApplyBodyCommands(Padded, &PaddedCommands[0].Command, 2, sizeof(FPaddedCommand)) == PT_OK, "padded commands (stride sizeof + 8)");
			R.Check(StepFrame(Api, Tight) == PT_OK && StepFrame(Api, Padded) == PT_OK, "frames");
		}
		R.Check(SameStates(ReadAllStates(Api, Tight), ReadAllStates(Api, Padded)) && ReadHash(Api, Tight) == ReadHash(Api, Padded),
			"padded strides give bit-identical states and hashes");
		R.Check(Api.DestroyScene(Tight) == PT_OK && Api.DestroyScene(Padded) == PT_OK, "DestroyScene x2");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys");
		return R;
	}

	// ---- A6 floating-point environment -------------------------------------------------------------------------------
	namespace
	{
		// Warnings the backend (or its Kit) sends through the log callback during A6.
		struct FA6Log
		{
			std::vector<std::string> Warnings;
		};

		void PT_CALL CaptureA6Log(void* UserData, const uint32_t Level, const char* Category, const char* Message)
		{
			if (Level <= PT_LOG_WARNING)
			{
				static_cast<FA6Log*>(UserData)->Warnings.push_back(std::string(Category != nullptr ? Category : "?") + ": " + (Message != nullptr ? Message : ""));
			}
		}

		// The Kit's module-level kit.mxcsr_restores (-1 when the backend does not report it, e.g. a non-Kit module).
		double ReadMxcsrRestores(const PtBackendApi& Api)
		{
			std::vector<FEffectiveSetting> Settings;
			std::string Error;
			if (!ParseEffectiveSettings(DumpText(Api, 0), Settings, Error))
			{
				return -1.0;
			}
			for (const FEffectiveSetting& Setting : Settings)
			{
				if (Setting.Key == "kit.mxcsr_restores")
				{
					return Setting.Canonical;
				}
			}
			return -1.0;
		}
	}

	FGateReport RunA6FloatingPointState(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A6", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		// The Kit restores MXCSR after every call, so a before / after comparison alone tests the Kit's safety net. A
		// backend that changes MXCSR is caught by the Kit's warning (log callback) and its restore counter.
		const FFpState Original = ReadFpState();
		// A distinctive host state, as UE's PreInit could leave it: FTZ + DAZ on, x87 at 64-bit precision.
		FFpState Host = Original;
		Host.MxcsrControl = (Original.MxcsrControl | 0x8040u) & 0xFFC0u;
		Host.X87Control = static_cast<uint16>((Original.X87Control & ~0x0300u) | 0x0300u);
		WriteFpState(Host);
		const FFpState Applied = ReadFpState();
		R.Check(Applied.MxcsrControl == Host.MxcsrControl && Applied.X87Control == Host.X87Control, "the test can set MXCSR and the x87 control word");
		int32 NumCalls = 0;
		const auto Expect = [&](const PtResult Got, const PtResult Wanted, const char* Name)
		{
			++NumCalls;
			const FFpState After = ReadFpState();
			R.Check(Got == Wanted, std::string(Name) + " -> " + GetPtResultName(Wanted) + " (got " + GetPtResultName(Got) + ")");
			R.Check(After.MxcsrControl == Host.MxcsrControl && After.X87Control == Host.X87Control, std::string(Name) + " preserves MXCSR and the x87 control word");
		};
		PtBackendInfo Info = {};
		Info.StructSize = sizeof(PtBackendInfo);
		Expect(Api.GetInfo(&Info), PT_OK, "GetInfo");
		FA6Log Log;
		PtInitParams Init = MakeInitParams();
		Init.LogCallback = &CaptureA6Log;
		Init.LogUserData = &Log;
		Expect(Api.InitGamePhys(&Init), PT_OK, "InitGamePhys");
		const double RestoresBefore = ReadMxcsrRestores(Api);
		const PtSceneDesc Desc = MakeSceneDesc();
		PtSceneId Scene = 0;
		Expect(Api.CreateScene(&Desc, &Scene), PT_OK, "CreateScene");
		const PtPhysicalMaterial Material = MakeMaterial();
		Expect(Api.CreateMaterials(Scene, &Material, 1, sizeof(PtPhysicalMaterial)), PT_OK, "CreateMaterials");
		const FTestScene Bodies = MakeTestBodies(0, 0);
		const PtActorBatch Batch = Bodies.MakeBatch();
		Expect(Api.CreateActors(Scene, &Batch), PT_OK, "CreateActors");
		Expect(Api.PrepareForSimulation(Scene, nullptr), PT_OK, "PrepareForSimulation");
		const PtVec3 Gravity = { 0.0, 0.0, -980.0 };
		for (int32 FrameIndex = 0; FrameIndex < 10; ++FrameIndex)
		{
			PtBodyCommand Command = MakeCommand(PT_CMD_ADD_TORQUE, Bodies.FirstDynamic);
			Command.Vector = PtVec3{ 1.0e3, 0.0, 0.0 };
			Expect(Api.ApplyBodyCommands(Scene, &Command, 1, sizeof(PtBodyCommand)), PT_OK, "ApplyBodyCommands");
			Expect(Api.SetUpForFrame(Scene, &Gravity, 1.0f / 60.0f, 0.0f, 0.0f, 0.0f, 1, 0u), PT_OK, "SetUpForFrame");
			Expect(Api.StartFrame(Scene), PT_OK, "StartFrame");
			Expect(Api.WaitPhysScenes(Scene), PT_OK, "WaitPhysScenes");
			PtFrameResult Frame = {};
			Expect(Api.EndFrame(Scene, &Frame), PT_OK, "EndFrame");
		}
		std::vector<PtBodyState> States(Bodies.NumBodies);
		uint32 Count = 0;
		Expect(Api.ReadBodyStates(Scene, PT_READ_ALL, nullptr, States.data(), Bodies.NumBodies, &Count), PT_OK, "ReadBodyStates");
		PtMassProperties Mass = {};
		Expect(Api.ReadMassProperties(Scene, 0, 1, &Mass), PT_OK, "ReadMassProperties");
		PtStepStats Stats = {};
		Stats.StructSize = sizeof(PtStepStats);
		Expect(Api.GetStepStats(Scene, &Stats), PT_OK, "GetStepStats");
		uint64 Hash = 0;
		Expect(Api.ComputeNativeStateHash(Scene, &Hash), (Info.Capabilities & PT_CAP_NATIVE_HASH) != 0 ? PT_OK : PT_E_UNSUPPORTED, "ComputeNativeStateHash");
		PtTextBuffer Query = {};
		Expect(Api.DumpEffectiveSettings(Scene, &Query), PT_E_CAPACITY, "DumpEffectiveSettings (measure)");
		PtMemoryStats Memory = {};
		Expect(Api.GetMemoryStats(Scene, &Memory), PT_OK, "GetMemoryStats");
		Expect(Api.SetOption(Scene, "a6.unknown", "1"), PT_E_INVALID_ARG, "SetOption (rejected)");
		PtTextBuffer ErrorQuery = {};
		Expect(Api.GetLastErrorMessage(&ErrorQuery), PT_E_CAPACITY, "GetLastErrorMessage (measure)");
		Expect(Api.DestroyScene(Scene), PT_OK, "DestroyScene");
		const double RestoresAfter = ReadMxcsrRestores(Api);
		R.Check(RestoresBefore >= 0.0 && RestoresAfter == RestoresBefore, "the Kit restored MXCSR for no call (kit.mxcsr_restores "
			+ std::to_string(RestoresBefore) + " -> " + std::to_string(RestoresAfter) + ")");
		Expect(Api.TermGamePhys(), PT_OK, "TermGamePhys");
		std::string MxcsrWarnings;
		for (const std::string& Warning : Log.Warnings)
		{
			MxcsrWarnings += Warning.find("MXCSR") != std::string::npos ? (MxcsrWarnings.empty() ? "" : " | ") + Warning : std::string();
		}
		R.Check(MxcsrWarnings.empty(), "no 'changed MXCSR' warning through the log callback: " + MxcsrWarnings);
		WriteFpState(Original);
		const FFpState Restored = ReadFpState();
		R.Check(Restored.MxcsrControl == Original.MxcsrControl && Restored.X87Control == Original.X87Control, "the test restored the original FP state");
		R.Note(std::to_string(NumCalls) + " calls under MXCSR control 0x" + ToHex(Host.MxcsrControl) + ", x87 control 0x" + ToHex(Host.X87Control));
		return R;
	}

	// ---- A7 effective keys -------------------------------------------------------------------------------------------
	FGateReport RunA7EffectiveKeys(const FAbiGateTarget& Target)
	{
		FGateReport R = MakeReport("A7", Target);
		const PtBackendApi& Api = *Target.Api;
		const FTerminateOnExit Terminate(Api);
		const PtInitParams Init = MakeInitParams();
		R.Check(Api.InitGamePhys(&Init) == PT_OK, "InitGamePhys");
		const PtSceneDesc Desc = MakeSceneDesc();
		PtSceneId Scene = 0;
		R.Check(Api.CreateScene(&Desc, &Scene) == PT_OK, "CreateScene");
		const FTestScene Bodies = PopulateScene(Api, Scene);
		R.Check(Bodies.Result == PT_OK, "bodies");
		const std::string Lines = DumpText(Api, Scene);
		std::vector<FEffectiveSetting> Settings;
		std::string Error;
		R.Check(ParseEffectiveSettings(Lines, Settings, Error), "the scene's effective settings parse: " + Error);
		const std::vector<std::string> Missing = FindMissingEffectiveKeys(Settings, Target.Module->GetInfo());
		std::string MissingText;
		for (const std::string& Key : Missing)
		{
			MissingText += (MissingText.empty() ? "" : ", ") + Key;
		}
		R.Check(Missing.empty(), "every required key is present (generic set + " + std::to_string(Target.Module->GetInfo().RequiredEffectiveKeys.size())
			+ " backend keys); missing: " + MissingText);
		const std::string ModuleLines = DumpText(Api, 0);
		std::vector<FEffectiveSetting> ModuleSettings;
		R.Check(ParseEffectiveSettings(ModuleLines, ModuleSettings, Error), "the module-level effective settings parse: " + Error);
		R.Note(std::to_string(Settings.size()) + " scene keys, " + std::to_string(ModuleSettings.size()) + " module keys");
		R.Check(Api.DestroyScene(Scene) == PT_OK, "DestroyScene");
		R.Check(Api.TermGamePhys() == PT_OK, "TermGamePhys");
		return R;
	}
}
