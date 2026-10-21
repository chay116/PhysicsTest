// ViewerSession.cpp - FViewerSession (see the header).
#include "ViewerSession.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Physics/PhysicsBackend.h"
#include "PhysScene.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>

DEFINE_LOG_CATEGORY_STATIC(LogViewerSession, Log, All);

namespace PhysicsTest
{
	const char* ToString(const EViewerOpenResult Result)
	{
		switch (Result)
		{
		case EViewerOpenResult::Opened: return "Opened";
		case EViewerOpenResult::RestartRequired: return "RestartRequired";
		default: return "Failed";
		}
	}

	EViewerOpenResult ClassifyBackendSwitch(const FBackendSwitchState& State, std::string& OutReason)
	{
		if (State.bLoadRefusedOnePerProcess)
		{
			OutReason = "a UE_RUNTIME backend (one per process) is already loaded";
			return EViewerOpenResult::RestartRequired;
		}
		if (State.bInitialized && (State.ModuleFlags & PT_MODULE_GLOBAL_SCENE_SETTINGS) != 0 && State.CurrentInitOptions != State.WantedInitOptions)
		{
			OutReason = "the backend fixes its settings process-wide at InitGamePhys (GLOBAL_SCENE_SETTINGS) and was initialised with other init options";
			return EViewerOpenResult::RestartRequired;
		}
		if (State.bInitGamePhysRefused && (State.ModuleFlags & (PT_MODULE_ONE_PER_PROCESS | PT_MODULE_NO_REINIT | PT_MODULE_GLOBAL_SCENE_SETTINGS)) != 0)
		{
			OutReason = "InitGamePhys refused in this process (ONE_PER_PROCESS / NO_REINIT / GLOBAL_SCENE_SETTINGS module)";
			return EViewerOpenResult::RestartRequired;
		}
		if (State.bInitGamePhysRefused)
		{
			OutReason = "InitGamePhys failed";
			return EViewerOpenResult::Failed;
		}
		if (State.bInitialized && (State.ModuleFlags & (PT_MODULE_ONE_PER_PROCESS | PT_MODULE_NO_REINIT)) != 0 && State.WantedMaxThreads != 0
			&& State.CurrentMaxThreads != State.WantedMaxThreads)
		{
			OutReason = "the backend fixes its thread budget at InitGamePhys (ONE_PER_PROCESS / NO_REINIT) and was initialised with MaxThreads "
				+ std::to_string(State.CurrentMaxThreads) + ", not " + std::to_string(State.WantedMaxThreads);
			return EViewerOpenResult::RestartRequired;
		}
		OutReason.clear();
		return EViewerOpenResult::Opened;
	}

	FViewerSession::FViewerSession() = default;

	FViewerSession::~FViewerSession()
	{
		Close();
	}

	void FViewerSession::Close()
	{
		Scene.reset();
		Scenario = nullptr;
	}

	uint64 FViewerSession::GetFrameCount() const
	{
		return Scene ? Scene->GetFrameCount() : 0;
	}

	EViewerOpenResult FViewerSession::Open(const FViewerSelection& InSelection, std::string& OutMessage)
	{
		OutMessage.clear();
		// ---- the scenario and its parameters ----
		IPhysicsTestScenario* NewScenario = FScenarioRegistry::Get().Find(InSelection.Scenario);
		if (NewScenario == nullptr)
		{
			OutMessage = "unknown scenario '" + InSelection.Scenario + "'";
			return EViewerOpenResult::Failed;
		}
		FScenarioParams NewParams;
		if (!NewScenario->ResolveParams(InSelection.Params, NewParams, OutMessage))
		{
			return EViewerOpenResult::Failed;
		}
		FPhysicsBackendRegistry& Registry = FPhysicsBackendRegistry::Get();
		const std::vector<std::string> Available = Registry.GetAvailableNames();
		if (std::find(Available.begin(), Available.end(), InSelection.Backend) == Available.end())
		{
			OutMessage = "backend '" + InSelection.Backend + "' is not available in this build";
			return EViewerOpenResult::Failed;
		}

		// ---- the configuration, as a bench run resolves it, then viz mode ----
		FBenchRunSpec Spec;
		Spec.Backend = InSelection.Backend;
		Spec.BackendProfile = InSelection.BackendProfile;
		Spec.PhysicsProfile = InSelection.PhysicsProfile;
		Spec.Scenario = InSelection.Scenario;
		Spec.Params = InSelection.Params;
		Spec.Threads = InSelection.Threads;
		Spec.CommandLine = FCommandLine::Get();   // -ini: and -ExecCmds reach the resolver
		std::string ConfigDir;
		if (FParse::Value(FCommandLine::Get(), "ConfigDir=", ConfigDir))
		{
			Spec.ConfigDir = StringConv::Utf8ToPath(ConfigDir);
		}
		auto NewConfig = std::make_unique<FResolvedPhysicsConfig>();
		FBenchConfig NewFiles;
		if (!LoadBenchConfig(Spec, *NewConfig, NewFiles, OutMessage))
		{
			return EViewerOpenResult::Failed;
		}
		NewConfig->TestSettings.RunMode = EPhysicsRunMode::Viz;   // the viewer's manifests are always mode viz (03 M4)
		NewConfig->TestSettings.NumThreads = InSelection.Threads > 0 ? InSelection.Threads : 1;

		// ---- process rules: restart required? ----
		FBackendInitParams InitParams;
		InitParams.MaxThreads = static_cast<uint32>(NewConfig->TestSettings.NumThreads);
		InitParams.InitOptions = NewConfig->InitOptions;
		FBackendSwitchState State;
		State.WantedInitOptions = InitParams.InitOptions;
		State.WantedMaxThreads = InitParams.MaxThreads;
		std::string Error;
		EBackendLoadFailure LoadFailure = EBackendLoadFailure::None;
		FPhysicsBackendModule* Module = Registry.FindOrLoadModule(InSelection.Backend, Error, &LoadFailure);
		if (Module == nullptr)
		{
			State.bLoadRefusedOnePerProcess = LoadFailure == EBackendLoadFailure::OnePerProcess;
			if (!State.bLoadRefusedOnePerProcess)
			{
				OutMessage = Error;
				return EViewerOpenResult::Failed;
			}
		}
		else
		{
			State.ModuleFlags = Module->GetInfo().ModuleFlags;
			State.bInitialized = Module->IsInitialized();
			State.CurrentInitOptions = Module->GetInitParams().InitOptions;
			State.CurrentMaxThreads = Module->GetInitParams().MaxThreads;
		}
		std::string Reason;
		EViewerOpenResult Classified = ClassifyBackendSwitch(State, Reason);
		if (Classified != EViewerOpenResult::Opened)
		{
			OutMessage = "'" + InSelection.Backend + "': " + Reason;
			return Classified;
		}
		if (Registry.FindOrLoad(InSelection.Backend, InitParams, Error) == nullptr)
		{
			State.bInitGamePhysRefused = true;
			Classified = ClassifyBackendSwitch(State, Reason);
			OutMessage = "'" + InSelection.Backend + "': " + Reason + ": " + Error;
			return Classified;
		}

		// ---- engine-option preflight (as the bench's step 5): a rejected variant key is an error message, not FPhysScene's
		// Fatal ----
		{
			std::vector<PtKeyValue> ProbeOptions;
			for (const auto& [Key, Value] : NewConfig->SceneOptions)
			{
				ProbeOptions.push_back(PtKeyValue{ Key.c_str(), Value.c_str() });
			}
			PtSceneDesc Probe = {};
			Probe.StructSize = sizeof(PtSceneDesc);
			Probe.RunMode = PT_RUN_VIZ;
			Probe.Gravity = PtVec3{ 0.0, 0.0, -980.0 };
			Probe.NominalDeltaTime = NewConfig->TestSettings.FixedDeltaTime > 0.0f ? static_cast<double>(NewConfig->TestSettings.FixedDeltaTime) : 1.0 / 60.0;
			Probe.AsyncFixedTimeStepSize = PT_ASYNC_DT_SYNC;
			Probe.RestitutionThreshold = -1.0;
			Probe.NumThreads = 1;
			Probe.DebugName = "ViewerOptionPreflight";
			Probe.Options = ProbeOptions.empty() ? nullptr : ProbeOptions.data();
			Probe.NumOptions = static_cast<uint32>(ProbeOptions.size());
			FPhysicsBackendModule* Initialised = Registry.FindOrLoadModule(InSelection.Backend, Error);
			if (Initialised == nullptr || !FPhysicsBackendScene::Create(*Initialised, Probe, Error))
			{
				OutMessage = "the backend rejects the variant's engine options: " + Error;
				return EViewerOpenResult::Failed;
			}
		}

		// ---- the scene: the old one goes first (one scene at a time; the harness CVars must agree) ----
		Scene.reset();
		const FScenarioCapacity Capacity = NewScenario->GetCapacity(NewParams);
		FPhysSceneInitParams InitScene;
		InitScene.DebugName = std::string("Viewer.") + NewScenario->GetName();
		InitScene.BackendName = InSelection.Backend;
		InitScene.Config = NewConfig.get();
		InitScene.MaxBodiesHint = Capacity.MaxBodies;
		InitScene.MaxBodyPairsHint = Capacity.MaxBodyPairs;
		InitScene.MaxContactConstraintsHint = Capacity.MaxContactConstraints;
		Config = std::move(NewConfig);
		ConfigFiles = NewFiles;
		Scene = std::make_unique<FPhysScene>(InitScene);
		FRandomStream Random(NewScenario->GetSeed(NewParams));
		const FScenarioContext Context{ NewParams, Random, Config.get() };
		NewScenario->BuildScene(*Scene, Context);
		Scene->FlushDeferredActors();
		Scene->PrepareForSimulation();
		Scene->OnPhysScenePreTick = [NewScenario](FPhysScene* InScene, const float DeltaSeconds) { NewScenario->OnPreTick(*InScene, DeltaSeconds); };
		const std::vector<float> FrameDts(static_cast<size_t>(NewScenario->GetDefaultFrames(NewParams)), Config->TestSettings.FixedDeltaTime);
		DescSha256 = Scene->ComputeDescSha256(FrameDts);
		Scenario = NewScenario;
		Selection = InSelection;
		ResolvedParams = NewParams;
		bSingleStepRequested = false;
		LastTickMilliseconds = 0.0;
		UE_LOG(LogViewerSession, Display, "opened %s on %s (%s / %s), %zu bodies, desc_sha256 %s", NewScenario->GetName(), InSelection.Backend.c_str(),
			Config->ProfileName.c_str(), ConfigFiles.VariantName.empty() ? "-" : ConfigFiles.VariantName.c_str(), Scene->GetBodies().size(), DescSha256.c_str());
		return EViewerOpenResult::Opened;
	}

	EViewerOpenResult FViewerSession::Reset(std::string& OutMessage)
	{
		const FViewerSelection Current = Selection;
		return Open(Current, OutMessage);
	}

	int32 FViewerSession::TickPhysics()
	{
		if (!Scene || (bPaused && !bSingleStepRequested))
		{
			return 0;
		}
		bSingleStepRequested = false;
		const uint64 Begin = FPlatformTime::Cycles64();
		Scene->Tick(Config->TestSettings.FixedDeltaTime);
		LastTickMilliseconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Begin) * 1000.0;
		return 1;
	}

	std::string FViewerSession::MakeCommandLine(const FViewerSelection& InSelection, const std::string& Extra)
	{
		const auto Quote = [](std::string Value)
		{
			// FParse toggles on every double quote and has no escape, so a quote inside a value cannot be passed on.
			Value.erase(std::remove(Value.begin(), Value.end(), '"'), Value.end());
			return Value.find_first_of(" \t,") != std::string::npos ? "\"" + Value + "\"" : Value;
		};
		std::string Line = "-Backend=" + Quote(InSelection.Backend);
		if (!InSelection.BackendProfile.empty())
		{
			Line += " -BackendProfile=" + Quote(InSelection.BackendProfile);
		}
		Line += " -Profile=" + Quote(InSelection.PhysicsProfile) + " -Scenario=" + Quote(InSelection.Scenario);
		for (const auto& [Name, Value] : InSelection.Params)
		{
			Line += " -Param:" + Name + "=" + Quote(Value);
		}
		if (InSelection.Threads > 1)
		{
			Line += " -Threads=" + std::to_string(InSelection.Threads);
		}
		if (!Extra.empty())
		{
			Line += " " + Extra;
		}
		return Line;
	}

	std::string FViewerSession::GetForwardedSwitches(const char* CommandLine)
	{
		if (CommandLine == nullptr)
		{
			return {};
		}
		// Tokens split at whitespace outside double quotes (FCommandLine::BuildFromArgV quotes -Key="value with spaces").
		std::vector<std::string> Tokens;
		std::string Current;
		bool bInQuotes = false;
		for (const char* Cursor = CommandLine; *Cursor != '\0'; ++Cursor)
		{
			const char C = *Cursor;
			if (C == '"')
			{
				bInQuotes = !bInQuotes;
			}
			if (!bInQuotes && (C == ' ' || C == '\t'))
			{
				if (!Current.empty())
				{
					Tokens.push_back(Current);
					Current.clear();
				}
				continue;
			}
			Current.push_back(C);
		}
		if (!Current.empty())
		{
			Tokens.push_back(Current);
		}
		const auto StartsWithNoCase = [](const std::string& Text, const std::string_view Prefix)
		{
			if (Text.size() < Prefix.size())
			{
				return false;
			}
			for (size_t Index = 0; Index < Prefix.size(); ++Index)
			{
				const char A = Text[Index] >= 'A' && Text[Index] <= 'Z' ? static_cast<char>(Text[Index] - 'A' + 'a') : Text[Index];
				const char B = Prefix[Index] >= 'A' && Prefix[Index] <= 'Z' ? static_cast<char>(Prefix[Index] - 'A' + 'a') : Prefix[Index];
				if (A != B)
				{
					return false;
				}
			}
			return true;
		};
		// Value switches (prefix match) and flags (whole token).
		static constexpr std::string_view ValuePrefixes[] = { "-ini:", "-ExecCmds=", "-ConfigDir=", "-ResX=", "-ResY=", "-ChaosDll=", "-UI=" };
		static constexpr std::string_view Flags[] = { "-NoVSync", "-D3DDebug", "-NoUI" };
		std::string Forwarded;
		for (const std::string& Token : Tokens)
		{
			bool bKeep = false;
			for (const std::string_view Prefix : ValuePrefixes)
			{
				bKeep = bKeep || StartsWithNoCase(Token, Prefix);
			}
			for (const std::string_view Flag : Flags)
			{
				bKeep = bKeep || (Token.size() == Flag.size() && StartsWithNoCase(Token, Flag));
			}
			if (bKeep)
			{
				Forwarded += Forwarded.empty() ? Token : " " + Token;
			}
		}
		return Forwarded;
	}
	namespace
	{
		std::vector<std::string> ListIniStems(const std::filesystem::path& Directory, const std::string& Prefix)
		{
			std::vector<std::string> Names;
			std::error_code Error;
			for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Directory, Error))
			{
				if (!Entry.is_regular_file(Error) || Entry.path().extension() != ".ini")
				{
					continue;
				}
				const std::string Stem = StringConv::PathToUtf8(Entry.path().stem());
				if (Stem.size() > Prefix.size() && Stem.compare(0, Prefix.size(), Prefix) == 0)
				{
					Names.push_back(Stem.substr(Prefix.size()));
				}
				else if (Prefix.empty())
				{
					Names.push_back(Stem);
				}
			}
			std::sort(Names.begin(), Names.end());
			return Names;
		}

		std::filesystem::path GetViewerConfigDir()
		{
			std::string ConfigDir;
			if (FParse::Value(FCommandLine::Get(), "ConfigDir=", ConfigDir))
			{
				return StringConv::Utf8ToPath(ConfigDir);
			}
			return GetDefaultConfigDir();
		}
	}

	std::vector<std::string> FViewerSession::ListBackendVariants(const std::string& Backend)
	{
		return ListIniStems(GetViewerConfigDir() / "Profiles" / "Backend", Backend + ".");
	}

	std::vector<std::string> FViewerSession::ListPhysicsProfiles()
	{
		return ListIniStems(GetViewerConfigDir() / "Profiles", "");
	}
}
