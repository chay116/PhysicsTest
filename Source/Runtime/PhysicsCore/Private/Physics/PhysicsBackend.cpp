// PhysicsBackend.cpp - FPhysicsBackendModule, FPhysicsBackendRegistry, FPhysicsBackendScene (see Physics/PhysicsBackend.h).
#include "Physics/PhysicsBackend.h"

#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Physics/PhysicsDebugSnapshot.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsBackend, Log, All);

namespace PhysicsTest
{
	using StringConv::PathToUtf8;
	using StringConv::Utf8ToPath;

	namespace
	{
		// Backend log messages (PtLogCallback; may arrive from any thread). UserData is the module name (static storage
		// owned by the module object, which is never destroyed while the process runs).
		void PT_CALL HostLogCallback(void* UserData, const uint32_t Level, const char* Category, const char* Message)
		{
			const char* Module = UserData != nullptr ? static_cast<const std::string*>(UserData)->c_str() : "?";
			switch (Level)
			{
			case PT_LOG_FATAL:   // the backend reports; the host decides (the call that failed returns an error code)
			case PT_LOG_ERROR:
				UE_LOG(LogPhysicsBackend, Error, "[%s] %s: %s", Module, Category, Message);
				break;
			case PT_LOG_WARNING:
				UE_LOG(LogPhysicsBackend, Warning, "[%s] %s: %s", Module, Category, Message);
				break;
			case PT_LOG_DISPLAY:
				UE_LOG(LogPhysicsBackend, Display, "[%s] %s: %s", Module, Category, Message);
				break;
			case PT_LOG_LOG:
				UE_LOG(LogPhysicsBackend, Log, "[%s] %s: %s", Module, Category, Message);
				break;
			default:
				UE_LOG(LogPhysicsBackend, Verbose, "[%s] %s: %s", Module, Category, Message);
				break;
			}
		}

		std::string ToString(const char* Text)
		{
			return Text != nullptr ? std::string(Text) : std::string();
		}

		PtVec3 ToPt(const FVector& V)
		{
			return PtVec3{ V.X, V.Y, V.Z };
		}

		PtTransform ToPt(const FTransform& T)
		{
			const FQuat& Q = T.GetRotation();
			return PtTransform{ PtQuat{ Q.X, Q.Y, Q.Z, Q.W }, ToPt(T.GetTranslation()) };
		}

		bool HasQuery(const ECollisionEnabled Value)
		{
			return Value == ECollisionEnabled::QueryOnly || Value == ECollisionEnabled::QueryAndPhysics;
		}

		uint32 MakeShapeFlags(const ECollisionEnabled Element, const ECollisionEnabled Body)
		{
			uint32 Flags = 0;
			if (CollisionEnabledHasPhysics(Element) && CollisionEnabledHasPhysics(Body))
			{
				Flags |= PT_SHAPEF_SIMULATION;
			}
			if (HasQuery(Element) && HasQuery(Body))
			{
				Flags |= PT_SHAPEF_QUERY;
			}
			return Flags;
		}

		PtPhysicalMaterial MarshalMaterial(const FPhysicalMaterial& Material, const FPhysicsSettingsCore& Project)
		{
			// Raw values: the pair rule (static friction, combine) is the backend's (DL41). The per-material combine mode is
			// resolved here: bOverride*CombineMode ? the material's mode : the project setting (A.1).
			PtPhysicalMaterial Out = {};
			Out.Friction = Material.Friction;
			Out.StaticFriction = Material.StaticFriction;
			Out.Restitution = Material.Restitution;
			Out.SleepLinearVelocityThreshold = Material.SleepLinearVelocityThreshold;
			Out.SleepAngularVelocityThreshold = Material.SleepAngularVelocityThreshold;
			Out.SleepCounterThreshold = Material.SleepCounterThreshold > 0 ? static_cast<uint32>(Material.SleepCounterThreshold) : 0u;
			Out.FrictionCombineMode = static_cast<uint8>(Material.GetEffectiveFrictionCombineMode(Project));
			Out.RestitutionCombineMode = static_cast<uint8>(Material.GetEffectiveRestitutionCombineMode(Project));
			return Out;
		}

		PtShapeElem MakeShape(const PtTransform& Local, const uint32 Type, const uint32 MaterialIndex, const FKShapeElem& Element,
			const ECollisionEnabled BodyCollision)
		{
			PtShapeElem Shape = {};
			Shape.LocalTransform = Local;
			Shape.Type = Type;
			Shape.MaterialIndex = MaterialIndex;
			Shape.ContactOffset = -1.0;   // profile rule (PhysX UE4Compat clamp, M5); UE elements carry no contact offset
			Shape.RestOffset = Element.RestOffset;
			Shape.Flags = MakeShapeFlags(Element.GetCollisionEnabled(), BodyCollision);
			return Shape;
		}

		// The process's current top-level exception filter (read without keeping a change).
		LPTOP_LEVEL_EXCEPTION_FILTER GetCurrentUnhandledExceptionFilter()
		{
			const LPTOP_LEVEL_EXCEPTION_FILTER Current = SetUnhandledExceptionFilter(nullptr);
			SetUnhandledExceptionFilter(Current);
			return Current;
		}
	}

	const char* GetPtResultName(const PtResult Result)
	{
		switch (Result)
		{
		case PT_OK: return "PT_OK";
		case PT_W_TRUNCATED: return "PT_W_TRUNCATED";
		case PT_E_INVALID_ARG: return "PT_E_INVALID_ARG";
		case PT_E_INVALID_HANDLE: return "PT_E_INVALID_HANDLE";
		case PT_E_BAD_STATE: return "PT_E_BAD_STATE";
		case PT_E_WRONG_THREAD: return "PT_E_WRONG_THREAD";
		case PT_E_UNSUPPORTED: return "PT_E_UNSUPPORTED";
		case PT_E_CAPACITY: return "PT_E_CAPACITY";
		case PT_E_VERSION: return "PT_E_VERSION";
		case PT_E_ENGINE: return "PT_E_ENGINE";
		case PT_E_OUT_OF_MEMORY: return "PT_E_OUT_OF_MEMORY";
		case PT_E_TRANSPORT: return "PT_E_TRANSPORT";
		case PT_E_INTERNAL: return "PT_E_INTERNAL";
		default: return "PT_?";
		}
	}

	void ApplyBodyState(const PtBodyState& In, FBodyInstance& Body)
	{
		const PtQuat& Q = In.ActorTransform.Rotation;
		const PtVec3& T = In.ActorTransform.Translation;
		Body.State.BodyTM = FTransform(FQuat(Q.X, Q.Y, Q.Z, Q.W), FVector(T.X, T.Y, T.Z));
		Body.State.LinearVelocity = FVector(In.LinearVelocity.X, In.LinearVelocity.Y, In.LinearVelocity.Z);
		Body.State.AngularVelocityInRadians = FVector(In.AngularVelocity.X, In.AngularVelocity.Y, In.AngularVelocity.Z);
		Body.State.ObjectState = static_cast<EObjectStateType>(In.ObjectState);
	}

	// ---- FPhysicsBackendModule ---------------------------------------------------------------------------------------
	FPhysicsBackendModule::~FPhysicsBackendModule() = default;

	bool FPhysicsBackendModule::ValidateApiTable(const PtBackendApi* InApi, std::string& OutError)
	{
		if (InApi == nullptr)
		{
			OutError = "PT_GetBackendApi(" + std::to_string(PT_ABI_VERSION_MAJOR) + ") returned NULL (the module does not support this ABI major)";
			return false;
		}
		// The v1.0 table is the frozen minimum (docs/Abi.md 2): a module built against v1.0 keeps loading after a v1.x host
		// appends entries. Entries past the module's StructSize are absent; this host reads none (it uses the 25 v1.0 entries),
		// a later one reads an appended entry only through PT_API_HAS.
		if (InApi->StructSize < PT_ABI_V1_0_API_SIZE)
		{
			OutError = "PtBackendApi.StructSize " + std::to_string(InApi->StructSize) + " is below the v1.0 table size " + std::to_string(PT_ABI_V1_0_API_SIZE);
			return false;
		}
		if ((InApi->AbiVersion >> 16) != PT_ABI_VERSION_MAJOR)
		{
			OutError = "PtBackendApi.AbiVersion major " + std::to_string(InApi->AbiVersion >> 16) + " differs from the host's " + std::to_string(PT_ABI_VERSION_MAJOR);
			return false;
		}
		const std::pair<const char*, bool> Entries[] = {
			{ "GetInfo", InApi->GetInfo != nullptr },
			{ "InitGamePhys", InApi->InitGamePhys != nullptr },
			{ "TermGamePhys", InApi->TermGamePhys != nullptr },
			{ "CreateScene", InApi->CreateScene != nullptr },
			{ "DestroyScene", InApi->DestroyScene != nullptr },
			{ "CreateMaterials", InApi->CreateMaterials != nullptr },
			{ "CreateActors", InApi->CreateActors != nullptr },
			{ "ReleaseActors", InApi->ReleaseActors != nullptr },
			{ "CreateConstraints", InApi->CreateConstraints != nullptr },
			{ "PrepareForSimulation", InApi->PrepareForSimulation != nullptr },
			{ "SetUpForFrame", InApi->SetUpForFrame != nullptr },
			{ "StartFrame", InApi->StartFrame != nullptr },
			{ "WaitPhysScenes", InApi->WaitPhysScenes != nullptr },
			{ "EndFrame", InApi->EndFrame != nullptr },
			{ "ApplyBodyCommands", InApi->ApplyBodyCommands != nullptr },
			{ "ReadBodyStates", InApi->ReadBodyStates != nullptr },
			{ "ReadMassProperties", InApi->ReadMassProperties != nullptr },
			{ "GetStepStats", InApi->GetStepStats != nullptr },
			{ "CaptureDebugSnapshot", InApi->CaptureDebugSnapshot != nullptr },
			{ "ReadDebugSnapshot", InApi->ReadDebugSnapshot != nullptr },
			{ "ComputeNativeStateHash", InApi->ComputeNativeStateHash != nullptr },
			{ "SetOption", InApi->SetOption != nullptr },
			{ "DumpEffectiveSettings", InApi->DumpEffectiveSettings != nullptr },
			{ "GetMemoryStats", InApi->GetMemoryStats != nullptr },
			{ "GetLastErrorMessage", InApi->GetLastErrorMessage != nullptr },
		};
		for (const auto& [EntryName, bPresent] : Entries)
		{
			if (!bPresent)
			{
				OutError = std::string("PtBackendApi.") + EntryName + " is NULL";
				return false;
			}
		}
		return true;
	}

	bool FPhysicsBackendModule::Finish(std::string& OutError)
	{
		if (!ValidateApiTable(Api, OutError))
		{
			return false;
		}
		PtBackendInfo Raw = {};
		Raw.StructSize = sizeof(PtBackendInfo);
		const PtResult Result = Api->GetInfo(&Raw);
		if (Result != PT_OK)
		{
			OutError = std::string("GetInfo failed: ") + GetPtResultName(Result) + " " + GetLastErrorMessage();
			return false;
		}
		Info.Name = ToString(Raw.Name);
		Info.DisplayName = ToString(Raw.DisplayName);
		Info.EngineVersion = ToString(Raw.EngineVersion);
		Info.SourceCommit = ToString(Raw.SourceCommit);
		Info.BuildConfig = ToString(Raw.BuildConfig);
		Info.BuildFlagsJson = ToString(Raw.BuildFlagsJson);
		Info.DeclaredCapabilities = Raw.Capabilities;
		Info.ModuleFlags = Raw.ModuleFlags;
		Info.ProvisionalRevision = Raw.ProvisionalRevision;
		// A module built against another PROVISIONAL(M9) layout revision: its provisional capabilities read as absent (A.1).
		Info.Capabilities = Raw.ProvisionalRevision == PT_ABI_PROVISIONAL_REVISION ? Raw.Capabilities : (Raw.Capabilities & ~PT_CAP_PROVISIONAL_MASK);
		for (uint32 Index = 0; Index < Raw.NumPhaseTimers && Raw.PhaseTimerNames != nullptr; ++Index)
		{
			Info.PhaseTimerNames.push_back(ToString(Raw.PhaseTimerNames[Index]));
		}
		for (uint32 Index = 0; Index < Raw.NumRequiredEffectiveKeys && Raw.RequiredEffectiveKeys != nullptr; ++Index)
		{
			Info.RequiredEffectiveKeys.push_back(ToString(Raw.RequiredEffectiveKeys[Index]));
		}
		return true;
	}

	std::unique_ptr<FPhysicsBackendModule> FPhysicsBackendModule::CreateStatic(const std::string_view InName, const PtGetBackendApiFn Entry, std::string& OutError)
	{
		std::unique_ptr<FPhysicsBackendModule> Module(new FPhysicsBackendModule());
		Module->Name = std::string(InName);
		if (Entry == nullptr)
		{
			OutError = "backend '" + Module->Name + "': no static entry";
			return nullptr;
		}
		// The host requests the major it was built with (DL29): 0 until the M6 freeze.
		Module->Entry = Entry;
		Module->Api = Entry(PT_ABI_VERSION_MAJOR);
		if (!Module->Finish(OutError))
		{
			OutError = "backend '" + Module->Name + "': " + OutError;
			return nullptr;
		}
		return Module;
	}

	std::unique_ptr<FPhysicsBackendModule> FPhysicsBackendModule::LoadDll(const std::string_view InName, const std::filesystem::path& InDllPath, std::string& OutError)
	{
		std::unique_ptr<FPhysicsBackendModule> Module(new FPhysicsBackendModule());
		Module->Name = std::string(InName);
		std::error_code Error;
		const std::filesystem::path Absolute = std::filesystem::absolute(InDllPath, Error);
		if (Error || !std::filesystem::exists(Absolute, Error))
		{
			OutError = "backend '" + Module->Name + "': DLL '" + PathToUtf8(InDllPath) + "' not found";
			return nullptr;
		}
		// A module's CRT or engine initialisation may install its own unhandled-exception filter or error mode; the host's
		// are put back right after the load (02 7.4). Affinity is set before any load (M7, FHostEnvironment).
		const LPTOP_LEVEL_EXCEPTION_FILTER HostFilter = GetCurrentUnhandledExceptionFilter();
		const UINT HostErrorMode = GetErrorMode();
		const HMODULE Handle = LoadLibraryExW(Absolute.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		const DWORD LoadError = Handle == nullptr ? GetLastError() : ERROR_SUCCESS;   // before any other Win32 call
		const LPTOP_LEVEL_EXCEPTION_FILTER ModuleFilter = SetUnhandledExceptionFilter(HostFilter);
		SetErrorMode(HostErrorMode);
		if (Handle == nullptr)
		{
			OutError = "backend '" + Module->Name + "': LoadLibraryExW('" + PathToUtf8(Absolute) + "') failed with Win32 error " + std::to_string(LoadError);
			return nullptr;
		}
		if (ModuleFilter != HostFilter)
		{
			UE_LOG(LogPhysicsBackend, Warning, "backend '%s': loading the DLL replaced the unhandled-exception filter; the host's was re-installed",
				Module->Name.c_str());
		}
		// Never FreeLibrary (02 7.1): the module stays for the life of the process.
		const auto Entry = reinterpret_cast<PtGetBackendApiFn>(reinterpret_cast<void*>(GetProcAddress(Handle, PT_GET_BACKEND_API_SYMBOL)));
		if (Entry == nullptr)
		{
			OutError = "backend '" + Module->Name + "': '" + PathToUtf8(Absolute) + "' does not export " PT_GET_BACKEND_API_SYMBOL;
			return nullptr;
		}
		Module->DllPath = Absolute;
		Module->Entry = Entry;
		Module->Api = Entry(PT_ABI_VERSION_MAJOR);
		if (!Module->Finish(OutError))
		{
			OutError = "backend '" + Module->Name + "': " + OutError;
			return nullptr;
		}
		return Module;
	}

	PtResult FPhysicsBackendModule::InitGamePhys(const FBackendInitParams& Params)
	{
		InitParams = Params;
		InitOptionViews.clear();
		for (const auto& [Key, Value] : InitParams.InitOptions)
		{
			InitOptionViews.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
		}
		PtInitParams Raw = {};
		Raw.StructSize = sizeof(PtInitParams);
		Raw.Flags = InitParams.Flags;
		Raw.MaxThreads = InitParams.MaxThreads;
		Raw.LogCallback = &HostLogCallback;
		Raw.LogUserData = &Name;
		Raw.EngineCommandLine = InitParams.EngineCommandLine.c_str();
		Raw.SavedDir = InitParams.SavedDir.c_str();
		Raw.Options = InitOptionViews.empty() ? nullptr : InitOptionViews.data();
		Raw.NumOptions = static_cast<uint32>(InitOptionViews.size());
		const PtResult Result = Api->InitGamePhys(&Raw);
		bInitialized = Result >= 0;
		return Result;
	}

	PtResult FPhysicsBackendModule::TermGamePhys()
	{
		checkf(NumLiveScenes == 0, "FPhysicsBackendModule '%s': TermGamePhys with %d live scene proxies", Name.c_str(), NumLiveScenes);
		const PtResult Result = Api->TermGamePhys();
		bInitialized = false;
		return Result;
	}

	std::string FPhysicsBackendModule::GetLastErrorMessage() const
	{
		PtTextBuffer Query = {};
		Api->GetLastErrorMessage(&Query);   // PT_E_CAPACITY + the needed length
		std::string Text(Query.Length > 0 ? Query.Length : 1u, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		Api->GetLastErrorMessage(&Buffer);
		Text.resize(std::strlen(Text.c_str()));
		return Text;
	}

	// ---- FPhysicsBackendRegistry -------------------------------------------------------------------------------------
	FPhysicsBackendRegistry& FPhysicsBackendRegistry::Get()
	{
		static FPhysicsBackendRegistry Registry;
		return Registry;
	}

	std::filesystem::path FPhysicsBackendRegistry::GetExecutableDirectory()
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
		return std::filesystem::path(Path).parent_path();
	}

	bool FPhysicsBackendRegistry::IsChaosAvailable(std::string& OutReason)
	{
		std::string DllPath;
		if (!FParse::Value(FCommandLine::Get(), "ChaosDll=", DllPath))
		{
			char* Env = nullptr;
			size_t Size = 0;
			if (_dupenv_s(&Env, &Size, "PT_CHAOS_DLL") == 0 && Env != nullptr)
			{
				DllPath = Env;
			}
			std::free(Env);
		}
		if (DllPath.empty())
		{
			OutReason = "no Chaos DLL configured (-ChaosDll=<path> or PT_CHAOS_DLL; the bridge is M2.5 / M13)";
			return false;
		}
		std::error_code Error;
		if (!std::filesystem::exists(Utf8ToPath(DllPath), Error))
		{
			OutReason = "Chaos DLL '" + DllPath + "' does not exist";
			return false;
		}
		OutReason.clear();
		return true;
	}

	std::vector<std::string> FPhysicsBackendRegistry::GetAvailableNames() const
	{
		std::vector<std::string> Names;
		const std::filesystem::path ExeDir = GetExecutableDirectory();
		for (const FBackendRegistryEntry& Entry : GetBackendRegistryEntries())
		{
			std::error_code Error;
			if (Entry.StaticEntry != nullptr || (Entry.DllFileName != nullptr && std::filesystem::exists(ExeDir / Entry.DllFileName, Error)))
			{
				Names.emplace_back(Entry.Name);
			}
		}
		std::string Reason;
		if (IsChaosAvailable(Reason))
		{
			Names.emplace_back("Chaos");
		}
		return Names;
	}

	FPhysicsBackendModule* FPhysicsBackendRegistry::FindOrLoadModule(const std::string_view InName, std::string& OutError, EBackendLoadFailure* OutFailure)
	{
		const auto Fail = [OutFailure](const EBackendLoadFailure Failure) -> FPhysicsBackendModule*
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = Failure;
			}
			return nullptr;
		};
		if (OutFailure != nullptr)
		{
			*OutFailure = EBackendLoadFailure::None;
		}
		for (const std::unique_ptr<FPhysicsBackendModule>& Module : Modules)
		{
			if (Module->GetName() == InName)
			{
				return Module.get();
			}
		}
		std::unique_ptr<FPhysicsBackendModule> Loaded;
		if (InName == "Chaos")
		{
			std::string Reason;
			if (!IsChaosAvailable(Reason))
			{
				OutError = Reason;
				return Fail(EBackendLoadFailure::Unavailable);
			}
			std::string DllPath;
			if (!FParse::Value(FCommandLine::Get(), "ChaosDll=", DllPath))
			{
				char* Env = nullptr;
				size_t Size = 0;
				if (_dupenv_s(&Env, &Size, "PT_CHAOS_DLL") == 0 && Env != nullptr)
				{
					DllPath = Env;
				}
				std::free(Env);
			}
			Loaded = FPhysicsBackendModule::LoadDll(InName, Utf8ToPath(DllPath), OutError);
		}
		else
		{
			const FBackendRegistryEntry* Found = nullptr;
			for (const FBackendRegistryEntry& Entry : GetBackendRegistryEntries())
			{
				if (InName == Entry.Name)
				{
					Found = &Entry;
					break;
				}
			}
			if (Found == nullptr)
			{
				OutError = "unknown backend '" + std::string(InName) + "'";
				return Fail(EBackendLoadFailure::UnknownBackend);
			}
			Loaded = Found->StaticEntry != nullptr ? FPhysicsBackendModule::CreateStatic(InName, Found->StaticEntry, OutError)
				: FPhysicsBackendModule::LoadDll(InName, GetExecutableDirectory() / Found->DllFileName, OutError);
		}
		if (!Loaded)
		{
			return Fail(EBackendLoadFailure::LoadFailed);
		}
		if ((Loaded->GetInfo().ModuleFlags & PT_MODULE_UE_RUNTIME) != 0)
		{
			for (const std::unique_ptr<FPhysicsBackendModule>& Module : Modules)
			{
				if ((Module->GetInfo().ModuleFlags & PT_MODULE_UE_RUNTIME) != 0)
				{
					OutError = "backend '" + std::string(InName) + "' is a UE_RUNTIME module and '" + Module->GetName() + "' is already loaded (one per process)";
					return Fail(EBackendLoadFailure::OnePerProcess);
				}
			}
		}
		Modules.push_back(std::move(Loaded));
		UE_LOG(LogPhysicsBackend, Log, "loaded backend '%s' (%s, %s%s)", Modules.back()->GetName().c_str(), Modules.back()->GetInfo().DisplayName.c_str(),
			Modules.back()->IsDll() ? "DLL " : "static", Modules.back()->IsDll() ? PathToUtf8(Modules.back()->GetDllPath()).c_str() : "");
		return Modules.back().get();
	}

	FPhysicsBackendModule* FPhysicsBackendRegistry::FindOrLoad(const std::string_view InName, const FBackendInitParams& Params, std::string& OutError)
	{
		FPhysicsBackendModule* Module = FindOrLoadModule(InName, OutError);
		if (Module == nullptr)
		{
			return nullptr;
		}
		if (Module->IsInitialized())
		{
			if ((Module->GetInfo().ModuleFlags & PT_MODULE_GLOBAL_SCENE_SETTINGS) != 0 && Module->GetInitParams().InitOptions != Params.InitOptions)
			{
				OutError = "backend '" + Module->GetName() + "' fixes its settings process-wide at InitGamePhys (GLOBAL_SCENE_SETTINGS) and was "
					"initialised with other init options; this combination needs its own process";
				return nullptr;
			}
			if (Module->GetInitParams().MaxThreads != Params.MaxThreads)
			{
				// The thread budget T is process-wide (Chaos: -corelimit=T at PreInit); the module keeps its first one. A scene
				// still passes its own PtSceneDesc::NumThreads.
				UE_LOG(LogPhysicsBackend, Warning, "backend '%s' was initialised with MaxThreads %u; a request for %u reuses that initialisation",
					Module->GetName().c_str(), Module->GetInitParams().MaxThreads, Params.MaxThreads);
			}
			return Module;
		}
		const PtResult Result = Module->InitGamePhys(Params);
		if (Result < 0)
		{
			OutError = "backend '" + Module->GetName() + "': InitGamePhys failed with " + GetPtResultName(Result) + ": " + Module->GetLastErrorMessage();
			return nullptr;
		}
		return Module;
	}

	int32 FPhysicsBackendRegistry::TermIdleModules()
	{
		int32 NumTerminated = 0;
		for (const std::unique_ptr<FPhysicsBackendModule>& Module : Modules)
		{
			if (Module->IsInitialized() && Module->GetNumLiveScenes() == 0)
			{
				const PtResult Result = Module->TermGamePhys();
				if (Result < 0)
				{
					UE_LOG(LogPhysicsBackend, Warning, "backend '%s': TermGamePhys failed with %s: %s", Module->GetName().c_str(), GetPtResultName(Result),
						Module->GetLastErrorMessage().c_str());
					continue;
				}
				++NumTerminated;
			}
		}
		return NumTerminated;
	}

	bool FPhysicsBackendRegistry::AnyLoadedModuleHas(const uint32 ModuleFlags) const
	{
		for (const std::unique_ptr<FPhysicsBackendModule>& Module : Modules)
		{
			if ((Module->GetInfo().ModuleFlags & ModuleFlags) != 0)
			{
				return true;
			}
		}
		return false;
	}

	// ---- FMarshalledActorBatch / FPhysicsBackendScene ----------------------------------------------------------------
	PtActorBatch FMarshalledActorBatch::MakeBatch() const
	{
		PtActorBatch Batch = {};
		Batch.StructSize = sizeof(PtActorBatch);
		Batch.NumActors = static_cast<uint32>(Actors.size());
		Batch.Actors = Actors.empty() ? nullptr : Actors.data();
		Batch.ActorStride = sizeof(PtActorCreateInfo);
		Batch.NumShapes = static_cast<uint32>(Shapes.size());
		Batch.Shapes = Shapes.empty() ? nullptr : Shapes.data();
		Batch.ShapeStride = sizeof(PtShapeElem);
		Batch.NumConvexVertices = static_cast<uint32>(ConvexVertices.size());
		Batch.ConvexVertices = ConvexVertices.empty() ? nullptr : ConvexVertices.data();
		return Batch;
	}

	std::unique_ptr<FPhysicsBackendScene> FPhysicsBackendScene::Create(FPhysicsBackendModule& InModule, const PtSceneDesc& Desc, std::string& OutError)
	{
		std::unique_ptr<FPhysicsBackendScene> Scene(new FPhysicsBackendScene());
		Scene->Module = &InModule;
		Scene->Api = &InModule.GetApi();
		const PtResult Result = Scene->Api->CreateScene(&Desc, &Scene->SceneId);
		if (Result < 0)
		{
			OutError = "backend '" + InModule.GetName() + "': CreateScene failed with " + GetPtResultName(Result) + ": " + InModule.GetLastErrorMessage();
			Scene->Api = nullptr;
			return nullptr;
		}
		++InModule.NumLiveScenes;
		Scene->PendingCommands.reserve(256);
		return Scene;
	}

	FPhysicsBackendScene::~FPhysicsBackendScene()
	{
		if (Api == nullptr)
		{
			return;
		}
		const PtResult Result = Api->DestroyScene(SceneId);
		if (Result < 0)
		{
			UE_LOG(LogPhysicsBackend, Error, "backend '%s': DestroyScene failed with %s: %s", Module->GetName().c_str(), GetPtResultName(Result),
				Module->GetLastErrorMessage().c_str());
		}
		--Module->NumLiveScenes;
	}

	bool FPhysicsBackendScene::MarshalBodies(const std::span<FBodyInstance* const> Bodies, const uint32 FirstBodyIndex, const FPhysicsSettingsCore& Project,
		FMarshalledActorBatch& Out, std::string& OutError) const
	{
		Out = FMarshalledActorBatch();
		Out.Actors.reserve(Bodies.size());
		const auto FindMaterialIndex = [this, &Out, &Project](const FPhysicalMaterial* Material) -> uint32
		{
			for (uint32 Index = 0; Index < MaterialSources.size(); ++Index)
			{
				if (MaterialSources[Index] == Material)
				{
					return Index;
				}
			}
			for (uint32 Index = 0; Index < Out.NewMaterialSources.size(); ++Index)
			{
				if (Out.NewMaterialSources[Index] == Material)
				{
					return static_cast<uint32>(MaterialSources.size()) + Index;
				}
			}
			Out.NewMaterialSources.push_back(Material);
			Out.NewMaterials.push_back(MarshalMaterial(*Material, Project));
			return static_cast<uint32>(MaterialSources.size() + Out.NewMaterialSources.size() - 1);
		};

		for (size_t BodyOffset = 0; BodyOffset < Bodies.size(); ++BodyOffset)
		{
			const FBodyInstance& Body = *Bodies[BodyOffset];
			const char* BodyName = Body.BodyName.empty() ? "<unnamed>" : Body.BodyName.c_str();
			if (!Body.CollisionFilter.IsValid())
			{
				// 02 5.1: an invalid channel is a load error, never folded to ObjectType mod 32 (SpawnBody's checkf is
				// compiled out in the test preset; this check is not).
				OutError = "body " + std::to_string(FirstBodyIndex + BodyOffset) + " ('" + BodyName + "'): collision channel "
					+ std::to_string(Body.CollisionFilter.ObjectType) + " is not below PT_NUM_COLLISION_CHANNELS (" + std::to_string(PT_NUM_COLLISION_CHANNELS) + ")";
				return false;
			}
			if (Body.BodySetup == nullptr)
			{
				OutError = "body " + std::to_string(FirstBodyIndex + BodyOffset) + " ('" + BodyName + "') has no body setup";
				return false;
			}
			const FPhysicalMaterial* Material = Body.GetSimplePhysicalMaterial();
			if (Material == nullptr)
			{
				OutError = "body " + std::to_string(FirstBodyIndex + BodyOffset) + " ('" + BodyName + "') has no physical material";
				return false;
			}
			const uint32 MaterialIndex = FindMaterialIndex(Material);
			const FKAggregateGeom& Geom = Body.BodySetup->AggGeom;
			const FVector& Scale = Body.Scale3D;   // BuildScale3D is metadata (02 5.2 #25): never applied
			const ECollisionEnabled BodyCollision = Body.CollisionFilter.CollisionEnabled;
			const uint32 FirstShape = static_cast<uint32>(Out.Shapes.size());
			for (const FKSphereElem& Elem : Geom.SphereElems)
			{
				const FKSphereElem Scaled = Elem.GetFinalScaled(Scale);
				PtShapeElem Shape = MakeShape(ToPt(FTransform(Scaled.Center)), PT_SHAPE_SPHERE, MaterialIndex, Elem, BodyCollision);
				Shape.Radius = Scaled.Radius;
				Out.Shapes.push_back(Shape);
			}
			for (const FKBoxElem& Elem : Geom.BoxElems)
			{
				const FKBoxElem Scaled = Elem.GetFinalScaled(Scale);
				PtShapeElem Shape = MakeShape(ToPt(Scaled.GetTransform()), PT_SHAPE_BOX, MaterialIndex, Elem, BodyCollision);
				// The one place where FKBoxElem's full extents become the ABI's half extents (02 7.1, DL06).
				Shape.BoxHalfExtent = ToPt(Scaled.GetHalfExtents());
				Out.Shapes.push_back(Shape);
			}
			for (const FKSphylElem& Elem : Geom.SphylElems)
			{
				const FKSphylElem Scaled = Elem.GetFinalScaled(Scale);
				PtShapeElem Shape = MakeShape(ToPt(Scaled.GetTransform()), PT_SHAPE_CAPSULE, MaterialIndex, Elem, BodyCollision);
				Shape.Radius = Scaled.Radius;
				Shape.HalfLength = Scaled.Length * 0.5;   // segment half length along local Z (FKSphylElem::Length)
				Out.Shapes.push_back(Shape);
			}
			for (const FKConvexElem& Elem : Geom.ConvexElems)
			{
				// Data model / ABI only in Phase 1 (DL31): vertices in element space scaled by the body scale.
				const FTransform& Local = Elem.GetTransform();
				PtShapeElem Shape = MakeShape(ToPt(FTransform(Local.GetRotation(), Local.GetTranslation() * Scale)), PT_SHAPE_CONVEX, MaterialIndex, Elem, BodyCollision);
				Shape.FirstVertex = static_cast<uint32>(Out.ConvexVertices.size());
				Shape.NumVertices = static_cast<uint32>(Elem.VertexData.size());
				for (const FVector& Vertex : Elem.VertexData)
				{
					Out.ConvexVertices.push_back(ToPt(Vertex * Scale));
				}
				Out.Shapes.push_back(Shape);
			}
			const uint32 NumShapes = static_cast<uint32>(Out.Shapes.size()) - FirstShape;
			if (NumShapes == 0)
			{
				OutError = "body " + std::to_string(FirstBodyIndex + BodyOffset) + " ('" + BodyName + "'): its body setup has no collision element";
				return false;
			}

			PtActorCreateInfo Actor = {};
			Actor.BodyIndex = FirstBodyIndex + static_cast<uint32>(BodyOffset);
			Actor.Motion = Body.IsStatic() ? PT_MOTION_STATIC : (Body.IsKinematic() ? PT_MOTION_KINEMATIC : PT_MOTION_DYNAMIC);
			// Creation-time flags (BodyInstance.cpp:1301-1340 [V]).
			Actor.Flags = (Body.bEnableGravity ? PT_BODY_ENABLE_GRAVITY : 0u) | (Body.bStartAwake ? PT_BODY_START_AWAKE : 0u) | PT_BODY_ALLOW_SLEEP
				| (Body.bUseCCD ? PT_BODY_USE_CCD : 0u) | (Body.bGyroscopicTorqueEnabled ? PT_BODY_GYROSCOPIC : 0u)
				| (Body.bInertiaConditioning ? PT_BODY_INERTIA_CONDITIONING : 0u) | (Body.bOverrideIterationCounts ? PT_BODY_OVERRIDE_ITERATIONS : 0u);
			Actor.FirstShape = FirstShape;
			Actor.NumShapes = NumShapes;
			Actor.ObjectType = Body.CollisionFilter.ObjectType;
			Actor.PositionIterations = Body.PositionSolverIterationCount;
			Actor.VelocityIterations = Body.VelocitySolverIterationCount;
			Actor.ProjectionIterations = Body.ProjectionSolverIterationCount;
			Actor.BlockMask = Body.CollisionFilter.BlockMask;
			Actor.ActorTransform = ToPt(Body.State.BodyTM);
			Actor.InitialLinearVelocity = ToPt(Body.InitialLinearVelocity);
			Actor.InitialAngularVelocity = ToPt(Body.InitialAngularVelocityInRadians);
			// Mass properties computed by the harness (BodyUtils, D5); the principal frame is RotationOfMass.
			Actor.Mass = Body.MassProperties.Mass;
			Actor.InertiaDiagonal = ToPt(Body.MassProperties.InertiaTensor);
			Actor.CenterOfMassLocal = ToPt(Body.MassProperties.GetComLocalPose());
			if (Actor.Motion == PT_MOTION_DYNAMIC && !(Actor.Mass > 0.0))
			{
				OutError = "body " + std::to_string(Actor.BodyIndex) + " ('" + BodyName + "') is dynamic without mass";
				return false;
			}
			Actor.LinearDamping = Body.LinearDamping;
			Actor.AngularDamping = Body.AngularDamping;
			Actor.MaxAngularVelocity = Body.GetMaxAngularVelocityInRadians(Project);
			Actor.MaxLinearVelocity = 0.0;   // UE has no linear velocity limit (CreateActor uses sqrt(FLT_MAX))
			// UE 5.8.3 FBodyInstance::UpdateMaxDepenetrationVelocity sends the body's value only when it overrides, else -1 =
			// "the solver setting" (BodyInstance.cpp:3713-3722 [V]); the project MaxDepenetrationVelocity is a PhysX-era key the
			// Chaos path never reads (PtBackendAbi.h, docs/Abi.md 5.7; M2 review, fixed in M3).
			Actor.MaxDepenetrationVelocity = Body.bOverrideMaxDepenetrationVelocity ? Body.MaxDepenetrationVelocity : -1.0;
			Actor.SleepThresholdMultiplier = Body.GetSleepThresholdMultiplier();
			Actor.StabilizationThresholdMultiplier = Body.StabilizationThresholdMultiplier;
			Actor.UserTag = 0;
			Out.Actors.push_back(Actor);
		}
		return true;
	}

	PtResult FPhysicsBackendScene::CreateActors(const std::span<FBodyInstance* const> Bodies, const FMarshalledActorBatch& Batch)
	{
		if (!Batch.NewMaterials.empty())
		{
			const PtResult Result = Api->CreateMaterials(SceneId, Batch.NewMaterials.data(), static_cast<uint32>(Batch.NewMaterials.size()), sizeof(PtPhysicalMaterial));
			if (Result < 0)
			{
				return Result;
			}
			MaterialSources.insert(MaterialSources.end(), Batch.NewMaterialSources.begin(), Batch.NewMaterialSources.end());
		}
		const PtActorBatch Raw = Batch.MakeBatch();
		const PtResult Result = Api->CreateActors(SceneId, &Raw);
		if (Result < 0)
		{
			return Result;
		}
		for (size_t Offset = 0; Offset < Bodies.size(); ++Offset)
		{
			FBodyInstance* Body = Bodies[Offset];
			Body->ActorHandle.Scene = this;
			Body->ActorHandle.Index = Batch.Actors[Offset].BodyIndex;
			Body->ActorHandle.Serial = NextSerial++;
			BodiesByIndex.push_back(Body);
		}
		// Pull buffers sized for every body: the timed T_pull never allocates.
		PullIndices.resize(BodiesByIndex.size());
		PullStates.resize(BodiesByIndex.size());
		return Result;
	}

	PtResult FPhysicsBackendScene::ReleaseActor(FBodyInstance& Body)
	{
		const uint32 Index = Body.ActorHandle.Index;
		const PtResult Result = Api->ReleaseActors(SceneId, &Index, 1);
		if (Result >= 0)
		{
			BodiesByIndex[Index] = nullptr;
			// Queued commands for the released body are dropped (UE discards them with the proxy); the backend would reject
			// them, and with them the rest of the atomic batch.
			std::erase_if(PendingCommands, [Index](const PtBodyCommand& Command) { return Command.Body == Index; });
		}
		return Result;
	}

	PtResult FPhysicsBackendScene::PrepareForSimulation(std::string& OutActions)
	{
		char Buffer[4096] = {};
		PtTextBuffer Text = { Buffer, sizeof(Buffer), 0 };
		const PtResult Result = Api->PrepareForSimulation(SceneId, &Text);
		OutActions = Result >= 0 ? std::string(Buffer) : std::string();
		return Result;
	}

	PtResult FPhysicsBackendScene::SetUpForFrame(const FVector* NewGrav, const float DeltaSeconds, const float MinDeltaTime, const float MaxDeltaTime,
		const float MaxSubstepDeltaTime, const int32 MaxSubsteps, const bool bSubstepping)
	{
		PtVec3 Gravity = {};
		if (NewGrav != nullptr)
		{
			Gravity = ToPt(*NewGrav);
		}
		return Api->SetUpForFrame(SceneId, NewGrav != nullptr ? &Gravity : nullptr, DeltaSeconds, MinDeltaTime, MaxDeltaTime, MaxSubstepDeltaTime, MaxSubsteps,
			bSubstepping ? 1u : 0u);
	}

	PtResult FPhysicsBackendScene::FlushCommands()
	{
		if (PendingCommands.empty())
		{
			return PT_OK;
		}
		const PtResult Result = Api->ApplyBodyCommands(SceneId, PendingCommands.data(), static_cast<uint32>(PendingCommands.size()), sizeof(PtBodyCommand));
		PendingCommands.clear();
		return Result;
	}

	PtResult FPhysicsBackendScene::PullBodyStates(const uint32 ReadFlags, uint32& OutCount)
	{
		OutCount = 0;
		const PtResult Result = Api->ReadBodyStates(SceneId, ReadFlags, PullIndices.data(), PullStates.data(), static_cast<uint32>(PullStates.size()), &OutCount);
		if (Result < 0)
		{
			return Result;
		}
		for (uint32 Index = 0; Index < OutCount; ++Index)
		{
			if (FBodyInstance* Body = BodiesByIndex[PullIndices[Index]])
			{
				ApplyBodyState(PullStates[Index], *Body);
			}
		}
		return Result;
	}

	PtResult FPhysicsBackendScene::ReadMassProperties(const uint32 First, const std::span<PtMassProperties> Out)
	{
		return Api->ReadMassProperties(SceneId, First, static_cast<uint32>(Out.size()), Out.data());
	}

	PtResult FPhysicsBackendScene::GetStepStats(PtStepStats& Out)
	{
		Out = PtStepStats();
		Out.StructSize = sizeof(PtStepStats);
		return Api->GetStepStats(SceneId, &Out);
	}

	PtResult FPhysicsBackendScene::GetMemoryStats(PtMemoryStats& Out)
	{
		Out = PtMemoryStats();
		return Api->GetMemoryStats(SceneId, &Out);
	}

	PtResult FPhysicsBackendScene::ComputeNativeStateHash(uint64& OutHash)
	{
		return Api->ComputeNativeStateHash(SceneId, &OutHash);
	}

	PtResult FPhysicsBackendScene::CaptureDebugSnapshot(const uint32 SnapshotFlags, FPhysicsDebugSnapshot& Out)
	{
		// The arrays keep their sizes between captures (M4: a viewer captures every frame): resize below only
		// value-initialises elements it adds, and ReadDebugSnapshot overwrites every element the counts describe.
		Out.FrameIndex = 0;
		Out.SimTime = 0.0;
		Out.bTruncated = false;
		Out.bFromHostState = false;
		Out.HarnessIslandIndex.clear();
		Out.Capabilities = Module->GetInfo().Capabilities;
		PtDebugSnapshotRequest Request = {};
		Request.StructSize = sizeof(PtDebugSnapshotRequest);
		Request.Flags = SnapshotFlags;
		Request.MaxContactPoints = 1u << 22;
		Request.MaxNativeCommands = 1u << 20;
		PtDebugSnapshotCounts Counts = {};
		PtResult Result = Api->CaptureDebugSnapshot(SceneId, &Request, &Counts);
		if (Result < 0)
		{
			Out.Reset();
			return Result;
		}
		Out.Particles.resize(Counts.NumParticles);
		Out.Contacts.resize(Counts.NumContactPoints);
		Out.Joints.resize(Counts.NumJoints);
		Out.NativeCommands.resize(Counts.NumNativeCommands);
		Out.StringPool.assign(Counts.NumStringBytes, '\0');
		PtDebugSnapshotBuffers Buffers = {};
		Buffers.Particles = Out.Particles.empty() ? nullptr : Out.Particles.data();
		Buffers.ParticleCapacity = Counts.NumParticles;
		Buffers.ParticleStride = sizeof(PtParticleSnapshot);
		Buffers.ContactPoints = Out.Contacts.empty() ? nullptr : Out.Contacts.data();
		Buffers.ContactCapacity = Counts.NumContactPoints;
		Buffers.ContactStride = sizeof(PtContactPoint);
		Buffers.Joints = Out.Joints.empty() ? nullptr : Out.Joints.data();
		Buffers.JointCapacity = Counts.NumJoints;
		Buffers.JointStride = sizeof(PtJointSnapshot);
		Buffers.NativeCommands = Out.NativeCommands.empty() ? nullptr : Out.NativeCommands.data();
		Buffers.CommandCapacity = Counts.NumNativeCommands;
		Buffers.CommandStride = sizeof(PtDebugDrawCmd);
		Buffers.StringBytes = Out.StringPool.empty() ? nullptr : Out.StringPool.data();
		Buffers.StringCapacity = Counts.NumStringBytes;
		Result = Api->ReadDebugSnapshot(SceneId, &Buffers);
		Out.bTruncated = Counts.bTruncated != 0;
		if (Result < 0)
		{
			Out.Reset();
			return Result;
		}
		// M5 (M4 carry-over (2)): a particle must name a body of this scene. PT_INDEX_NONE or an index beyond the bodies
		// created here is a backend bug; the particle is dropped (the consumers index arrays by body) and logged once.
		const uint32 NumBodies = GetNumBodies();
		Out.NumBodies = NumBodies;
		const auto Invalid = std::remove_if(Out.Particles.begin(), Out.Particles.end(),
			[NumBodies](const PtParticleSnapshot& Particle) { return Particle.BodyIndex >= NumBodies; });
		Out.NumRejectedParticles = static_cast<uint32>(Out.Particles.end() - Invalid);
		if (Out.NumRejectedParticles > 0)
		{
			if (NumSnapshotIndexErrorLogs == 0)
			{
				UE_LOG(LogPhysicsBackend, Error, "scene %llu (%s): CaptureDebugSnapshot returned %u particle(s) with a BodyIndex outside [0, %u) (PT_INDEX_NONE "
					"or not a body of this scene); dropped (logged once per scene)", static_cast<unsigned long long>(SceneId), Module->GetName().c_str(), Out.NumRejectedParticles, NumBodies);
				++NumSnapshotIndexErrorLogs;
			}
			Out.Particles.erase(Invalid, Out.Particles.end());
		}
		return Result;
	}

	PtResult FPhysicsBackendScene::DumpEffectiveSettings(std::string& OutJsonLines)
	{
		OutJsonLines.clear();
		PtTextBuffer Query = {};
		PtResult Result = Api->DumpEffectiveSettings(SceneId, &Query);
		if (Result != PT_E_CAPACITY && Result < 0)
		{
			return Result;
		}
		std::string Text(Query.Length, '\0');
		PtTextBuffer Buffer = { Text.data(), static_cast<uint32_t>(Text.size()), 0 };
		Result = Api->DumpEffectiveSettings(SceneId, &Buffer);
		if (Result < 0)
		{
			return Result;
		}
		Text.resize(Buffer.Length > 0 ? Buffer.Length - 1 : 0);
		OutJsonLines = std::move(Text);
		return Result;
	}

	PtResult FPhysicsBackendScene::SetOption(const std::string_view Key, const std::string_view Value)
	{
		const std::string KeyText(Key);
		const std::string ValueText(Value);
		return Api->SetOption(SceneId, KeyText.c_str(), ValueText.c_str());
	}
}
