// PhysicsBackend.h - the host side of the C ABI (02_Architecture 7.4, A.8, docs/Abi.md). [MVP M2]
//   FPhysicsBackendModule    one loaded backend (static entry or DLL), never unloaded (~ a UE module)
//   FPhysicsBackendRegistry  the generated list of backends + process rules (~ FModuleManager)
//   FPhysicsBackendScene     the proxy over one PtSceneId: FBodyInstance -> Pt* marshalling, the frame verbs, the command
//                            buffer (T_push) and the state pull (T_pull). The timed path calls function pointers directly
//                            and does not allocate.
// The host always requests the ABI major it was built with (PT_ABI_VERSION_MAJOR: 0 until the M6 freeze).
#pragma once

#include "CoreTypes.h"
#include "Math/Vector.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The harness C++ channel count and the C ABI macro must agree (02 5.1; the names differ on purpose, see
// PhysicsInterfaceTypes.h).
static_assert(PhysicsTest::NumCollisionChannels == PT_NUM_COLLISION_CHANNELS, "harness and ABI collision channel counts differ");

namespace PhysicsTest
{
	struct FBodyInstance;
	struct FPhysicalMaterial;
	struct FPhysicsSettingsCore;

	// One generated registry entry (cmake/PTBackendRegistry.cpp.in): a static backend (StaticEntry) or a DLL file next to
	// the executable (DllFileName).
	struct FBackendRegistryEntry
	{
		const char* Name = nullptr;
		PtGetBackendApiFn StaticEntry = nullptr;
		const wchar_t* DllFileName = nullptr;
	};

	// Defined by the generated PT::BackendRegistry library (Source/Backends/CMakeLists.txt). Programs that create an
	// FPhysScene link PT::BackendRegistry.
	[[nodiscard]] std::span<const FBackendRegistryEntry> GetBackendRegistryEntries();

	struct FBackendInfo
	{
		std::string Name;
		std::string DisplayName;
		std::string EngineVersion;
		std::string SourceCommit;
		std::string BuildConfig;
		std::string BuildFlagsJson;
		uint64 Capabilities = 0;            // PROVISIONAL(M9) bits already cleared on a revision mismatch
		uint64 DeclaredCapabilities = 0;    // as the module reported them
		uint32 ModuleFlags = 0;
		uint32 ProvisionalRevision = 0;
		std::vector<std::string> PhaseTimerNames;
		std::vector<std::string> RequiredEffectiveKeys;   // backend set; A7 adds the generic set (Physics/EffectiveSettings.h)

		[[nodiscard]] bool Has(const uint64 Cap) const { return (Capabilities & Cap) == Cap; }
	};

	// InitGamePhys arguments (A.8 passes them as separate parameters).
	struct FBackendInitParams
	{
		uint32 MaxThreads = 1;
		uint32 Flags = 0;
		std::string SavedDir;
		std::string EngineCommandLine;
		std::vector<std::pair<std::string, std::string>> InitOptions;   // FResolvedPhysicsConfig::InitOptions

		[[nodiscard]] bool operator==(const FBackendInitParams&) const = default;
	};

	class FPhysicsBackendModule
	{
	public:
		~FPhysicsBackendModule();
		FPhysicsBackendModule(const FPhysicsBackendModule&) = delete;
		FPhysicsBackendModule& operator=(const FPhysicsBackendModule&) = delete;

		// A static backend: Entry(PT_ABI_VERSION_MAJOR), table and GetInfo checks.
		[[nodiscard]] static std::unique_ptr<FPhysicsBackendModule> CreateStatic(std::string_view Name, PtGetBackendApiFn Entry, std::string& OutError);
		// A DLL backend: LoadLibraryExW(absolute path, LOAD_WITH_ALTERED_SEARCH_PATH), GetProcAddress(PT_GetBackendApi), then
		// the host's unhandled-exception filter and error mode are re-installed (a module's CRT or engine init may replace
		// them). Never unloaded (no FreeLibrary).
		[[nodiscard]] static std::unique_ptr<FPhysicsBackendModule> LoadDll(std::string_view Name, const std::filesystem::path& DllPath, std::string& OutError);
		// The A1 host checks on a returned table: non-NULL, StructSize >= sizeof(PtBackendApi), AbiVersion major ==
		// PT_ABI_VERSION_MAJOR, every entry non-NULL.
		[[nodiscard]] static bool ValidateApiTable(const PtBackendApi* Api, std::string& OutError);

		[[nodiscard]] const PtBackendApi& GetApi() const { return *Api; }
		// The static entry or the DLL's PT_GetBackendApi (gate A1 calls it with other majors).
		[[nodiscard]] PtGetBackendApiFn GetEntryPoint() const { return Entry; }
		[[nodiscard]] const FBackendInfo& GetInfo() const { return Info; }
		[[nodiscard]] const std::string& GetName() const { return Name; }
		[[nodiscard]] bool IsDll() const { return !DllPath.empty(); }
		[[nodiscard]] const std::filesystem::path& GetDllPath() const { return DllPath; }
		[[nodiscard]] bool IsInitialized() const { return bInitialized; }
		[[nodiscard]] const FBackendInitParams& GetInitParams() const { return InitParams; }
		[[nodiscard]] int32 GetNumLiveScenes() const { return NumLiveScenes; }

		// InitGamePhys with the host log callback (backend messages go to LogPhysicsBackend). PtResult of the call.
		PtResult InitGamePhys(const FBackendInitParams& Params);
		// TermGamePhys; requires that no FPhysicsBackendScene of this module is alive.
		PtResult TermGamePhys();
		// The module's last error text (GetLastErrorMessage, two-call protocol).
		[[nodiscard]] std::string GetLastErrorMessage() const;

	private:
		FPhysicsBackendModule() = default;
		bool Finish(std::string& OutError);

		std::string Name;
		std::filesystem::path DllPath;
		PtGetBackendApiFn Entry = nullptr;
		const PtBackendApi* Api = nullptr;
		FBackendInfo Info;
		FBackendInitParams InitParams;
		std::vector<PtKeyValue> InitOptionViews;
		bool bInitialized = false;
		int32 NumLiveScenes = 0;
		friend class FPhysicsBackendScene;
	};

	class FPhysicsBackendRegistry
	{
	public:
		[[nodiscard]] static FPhysicsBackendRegistry& Get();

		// Generated static list + DLL backends whose file exists next to the executable + Chaos when available.
		[[nodiscard]] std::vector<std::string> GetAvailableNames() const;
		// Loads (once) without initialising. nullptr + OutError when unknown or the load fails.
		FPhysicsBackendModule* FindOrLoadModule(std::string_view Name, std::string& OutError);
		// Loads and initialises once (InitGamePhys with Params). Process rules (02 6, A.8): at most one UE_RUNTIME module;
		// a GLOBAL_SCENE_SETTINGS module accepts one set of init options per process (PhysX tolerances scale) - a request
		// with other options is refused. Other modules reuse their first initialisation.
		FPhysicsBackendModule* FindOrLoad(std::string_view Name, const FBackendInitParams& Params, std::string& OutError);
		// Chaos: -ChaosDll=<path> on the command line or PT_CHAOS_DLL in the environment, and the file exists (M2.5 / M13).
		[[nodiscard]] static bool IsChaosAvailable(std::string& OutReason);
		[[nodiscard]] static std::filesystem::path GetExecutableDirectory();

	private:
		FPhysicsBackendRegistry() = default;
		std::vector<std::unique_ptr<FPhysicsBackendModule>> Modules;
	};

	// The bodies of one FlushDeferredActors, marshalled for CreateMaterials + CreateActors.
	struct FMarshalledActorBatch
	{
		std::vector<PtPhysicalMaterial> NewMaterials;          // materials first used by this batch (CreateMaterials)
		std::vector<const FPhysicalMaterial*> NewMaterialSources;
		std::vector<PtActorCreateInfo> Actors;
		std::vector<PtShapeElem> Shapes;
		std::vector<PtVec3> ConvexVertices;

		[[nodiscard]] PtActorBatch MakeBatch() const;
	};

	class FPhysicsBackendScene
	{
	public:
		// Creates the backend scene (CreateScene). nullptr + OutError on failure.
		[[nodiscard]] static std::unique_ptr<FPhysicsBackendScene> Create(FPhysicsBackendModule& Module, const PtSceneDesc& Desc, std::string& OutError);
		~FPhysicsBackendScene();   // DestroyScene
		FPhysicsBackendScene(const FPhysicsBackendScene&) = delete;
		FPhysicsBackendScene& operator=(const FPhysicsBackendScene&) = delete;

		// ---- creation (untimed, T_create) ----
		// FBodyInstance -> PtActorCreateInfo / PtShapeElem (02 7.1): FK*Elem::GetFinalScaled(Scale3D) (BuildScale3D is never
		// applied), box half extents halved here and only here, capsule Radius + HalfLength along local Z, the body's
		// simple material (new ones are added to NewMaterials with the host-resolved combine modes), BodyIndex =
		// FirstBodyIndex + i. Rejects (false + OutError) an ObjectType >= PT_NUM_COLLISION_CHANNELS, a body without a
		// setup or shapes, or a dynamic body without mass.
		bool MarshalBodies(std::span<FBodyInstance* const> Bodies, uint32 FirstBodyIndex, const FPhysicsSettingsCore& Project,
			FMarshalledActorBatch& Out, std::string& OutError) const;
		// CreateMaterials(NewMaterials) + CreateActors, then binds every body's ActorHandle to this proxy.
		PtResult CreateActors(std::span<FBodyInstance* const> Bodies, const FMarshalledActorBatch& Batch);
		PtResult ReleaseActor(FBodyInstance& Body);
		// [T_prepare] Returns the backend's description of what it did.
		PtResult PrepareForSimulation(std::string& OutActions);

		// ---- frame (T_step) ----
		PtResult SetUpForFrame(const FVector* NewGrav, float DeltaSeconds, float MinDeltaTime, float MaxDeltaTime, float MaxSubstepDeltaTime,
			int32 MaxSubsteps, bool bSubstepping);
		PtResult StartFrame() { return Api->StartFrame(SceneId); }
		PtResult WaitPhysScenes() { return Api->WaitPhysScenes(SceneId); }
		PtResult EndFrame(PtFrameResult& Out) { return Api->EndFrame(SceneId, &Out); }

		// ---- commands (T_push) ----
		void EnqueueCommand(const PtBodyCommand& Command) { PendingCommands.push_back(Command); }
		[[nodiscard]] bool HasPendingCommands() const { return !PendingCommands.empty(); }
		// ApplyBodyCommands with every queued command (one call), then clears the queue.
		PtResult FlushCommands();

		// ---- reads (T_pull) ----
		// ReadBodyStates into the proxy's buffers (sized at CreateActors, no allocation) and writes every returned state
		// into the owning FBodyInstance::State.
		PtResult PullBodyStates(uint32 ReadFlags, uint32& OutCount);
		PtResult ReadMassProperties(uint32 First, std::span<PtMassProperties> Out);
		PtResult GetStepStats(PtStepStats& Out);
		PtResult ComputeNativeStateHash(uint64& OutHash);
		// JSON lines (two-call protocol). Empty string + error code on failure.
		PtResult DumpEffectiveSettings(std::string& OutJsonLines);
		PtResult SetOption(std::string_view Key, std::string_view Value);

		[[nodiscard]] PtSceneId GetSceneId() const { return SceneId; }
		[[nodiscard]] FPhysicsBackendModule& GetModule() const { return *Module; }
		[[nodiscard]] const FBackendInfo& GetInfo() const { return Module->GetInfo(); }
		[[nodiscard]] const PtBackendApi& GetApi() const { return *Api; }
		[[nodiscard]] uint32 GetNumBodies() const { return static_cast<uint32>(BodiesByIndex.size()); }
		[[nodiscard]] FBodyInstance* GetBody(const uint32 Index) const { return Index < BodiesByIndex.size() ? BodiesByIndex[Index] : nullptr; }
		[[nodiscard]] std::string GetLastErrorMessage() const { return Module->GetLastErrorMessage(); }

	private:
		FPhysicsBackendScene() = default;

		FPhysicsBackendModule* Module = nullptr;
		const PtBackendApi* Api = nullptr;
		PtSceneId SceneId = 0;
		uint32 NextSerial = 1;
		std::vector<const FPhysicalMaterial*> MaterialSources;   // backend material index -> host material
		std::vector<FBodyInstance*> BodiesByIndex;                // creation order (= BodyIndex)
		std::vector<PtBodyCommand> PendingCommands;
		std::vector<uint32> PullIndices;
		std::vector<PtBodyState> PullStates;
	};

	// PtBodyState -> FBodyInstanceState (cm, cm/s, rad/s; the object state values are shared).
	void ApplyBodyState(const PtBodyState& In, FBodyInstance& Body);
	[[nodiscard]] const char* GetPtResultName(PtResult Result);
}
