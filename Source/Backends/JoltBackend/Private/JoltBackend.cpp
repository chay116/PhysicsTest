// JoltBackend.cpp - FJoltBackend (module: Jolt registration, scene creation, options) and the static entry
// PT_GetBackendApi_Jolt that the generated backend registry references (02_Architecture 4.5, 8.3, A.15).
#include "PhysScene_Jolt.h"

#include "PtBuildReport.h"

#include <atomic>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>

#ifndef PT_JOLT_SOURCE_COMMIT
	#define PT_JOLT_SOURCE_COMMIT "unknown"
#endif

namespace PhysicsTest::JoltBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// Adapter-TU self-report (G0, DL49): the Jolt PUBLIC definitions this TU compiles with.
#if defined(JPH_PROFILE_ENABLED)
	#define PT_JOLT_BR_PROFILE "\"JPH_PROFILE_ENABLED\":1,"
#else
	#define PT_JOLT_BR_PROFILE "\"JPH_PROFILE_ENABLED\":0,"
#endif
#if defined(JPH_DEBUG_RENDERER)
	#define PT_JOLT_BR_RENDERER "\"JPH_DEBUG_RENDERER\":1,"
#else
	#define PT_JOLT_BR_RENDERER "\"JPH_DEBUG_RENDERER\":0,"
#endif
#if defined(JPH_FLOATING_POINT_EXCEPTIONS_ENABLED)
	#define PT_JOLT_BR_FPE "\"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED\":1,"
#else
	#define PT_JOLT_BR_FPE "\"JPH_FLOATING_POINT_EXCEPTIONS_ENABLED\":0,"
#endif
#if defined(JPH_USE_AVX2)
	#define PT_JOLT_BR_AVX2 "\"JPH_USE_AVX2\":1,"
#else
	#define PT_JOLT_BR_AVX2 "\"JPH_USE_AVX2\":0,"
#endif
#if defined(JPH_ENABLE_ASSERTS)
	#define PT_JOLT_BR_ASSERTS "\"JPH_ENABLE_ASSERTS\":1,"
#else
	#define PT_JOLT_BR_ASSERTS "\"JPH_ENABLE_ASSERTS\":0,"
#endif
		PT_BUILD_REPORT_JSON(GJoltBuildReport, "\"backend\":\"Jolt\",\"jolt_version\":\"" PT_BR_STR(JPH_VERSION_MAJOR) "." PT_BR_STR(JPH_VERSION_MINOR) "."
			PT_BR_STR(JPH_VERSION_PATCH) "\"," PT_JOLT_BR_PROFILE PT_JOLT_BR_RENDERER PT_JOLT_BR_FPE PT_JOLT_BR_AVX2 PT_JOLT_BR_ASSERTS);

		const char* const GRequiredEffectiveKeys[] = { "jolt.config_string", "jolt.temp_allocator_bytes" };

		// Jolt's global hooks have no user data: the module's kit services (log callback) while the module is initialised.
		Abi::FKitServices* GKitForHooks = nullptr;
		std::atomic<uint32> GNumJoltAsserts{ 0 };

		void JoltTrace(const char* Format, ...)
		{
			char Buffer[1024];
			va_list Args;
			va_start(Args, Format);
			vsnprintf(Buffer, sizeof(Buffer), Format, Args);
			va_end(Args);
			if (Abi::FKitServices* Kit = GKitForHooks)
			{
				Kit->LogToHost(PT_LOG_LOG, "Jolt", "%s", Buffer);
			}
		}

#ifdef JPH_ENABLE_ASSERTS
		// dbg builds: a Jolt assert is logged as an error and counted (jolt.asserts); the run continues so that the failing
		// test or gate reports it instead of a debugger break.
		bool JoltAssertFailed(const char* Expression, const char* Message, const char* File, const JPH::uint Line)
		{
			GNumJoltAsserts.fetch_add(1, std::memory_order_relaxed);
			if (Abi::FKitServices* Kit = GKitForHooks)
			{
				Kit->LogToHost(PT_LOG_ERROR, "Jolt", "assert %s:%u: (%s) %s", File, Line, Expression, Message != nullptr ? Message : "");
			}
			return false;
		}
#endif

		const char* GetBuildConfig()
		{
#if defined(_DEBUG)
			return "Debug";
#else
			return "Release";
#endif
		}
	}

	uint32 GetNumJoltAsserts()
	{
		return GNumJoltAsserts.load(std::memory_order_relaxed);
	}

	void RaiseTestAssert(const char* Where)
	{
		GNumJoltAsserts.fetch_add(1, std::memory_order_relaxed);
		if (Abi::FKitServices* Kit = GKitForHooks)
		{
			Kit->LogToHost(PT_LOG_WARNING, "Jolt", "test assert raised (%s)", Where);
		}
	}

	void FJoltBackend::GetInfo(PtBackendInfo& Out) const
	{
		Out.Name = ModuleName;
		Out.DisplayName = "Jolt Physics 5.6.0";
		Out.EngineVersion = "Jolt " PT_BR_STR(JPH_VERSION_MAJOR) "." PT_BR_STR(JPH_VERSION_MINOR) "." PT_BR_STR(JPH_VERSION_PATCH);
		Out.SourceCommit = PT_JOLT_SOURCE_COMMIT;
		Out.BuildConfig = GetBuildConfig();
		Out.BuildFlagsJson = GJoltBuildReport;
		// 02 7.5: contact points (viz) and bounds through the debug snapshot; no native islands, no native frame model.
		Out.Capabilities = PT_CAP_COLLISION | PT_CAP_KINEMATIC | PT_CAP_SLEEPING | PT_CAP_CCD | PT_CAP_GYROSCOPIC | PT_CAP_MULTITHREADED | PT_CAP_ACTIVE_LIST
			| PT_CAP_STATE_WRITE | PT_CAP_NATIVE_HASH | PT_CAP_CONTACT_POINTS | PT_CAP_BOUNDS;
		Out.ModuleFlags = 0;   // Jolt keeps no process-wide scene settings (the combine callbacks are per PhysicsSystem)
		Out.NumPhaseTimers = 0;
		Out.PhaseTimerNames = nullptr;
		Out.NumRequiredEffectiveKeys = static_cast<uint32>(sizeof(GRequiredEffectiveKeys) / sizeof(GRequiredEffectiveKeys[0]));
		Out.RequiredEffectiveKeys = GRequiredEffectiveKeys;
	}

	PtResult FJoltBackend::InitGamePhys(const PtInitParams& Params)
	{
		if (Params.NumOptions > 0)
		{
			return GetKit().SetLastError(PT_E_INVALID_ARG, "Jolt: unknown module option '%s' (Jolt has no module options)", Params.Options[0].Key);
		}
		MaxThreads = Params.MaxThreads;
		GKitForHooks = &GetKit();
		JPH::Trace = &JoltTrace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &JoltAssertFailed;)
		// Jolt's type registry is process-global: the module owns it between InitGamePhys and TermGamePhys.
		if (JPH::Factory::sInstance == nullptr)
		{
			JPH::RegisterDefaultAllocator();
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
			bJoltRegistered = true;
		}
		GetKit().LogToHost(PT_LOG_LOG, "LogJoltBackend", "Jolt %d.%d.%d initialised (%s), MaxThreads %u", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH,
			JPH::GetConfigurationString(), MaxThreads);
		return PT_OK;
	}

	PtResult FJoltBackend::TermGamePhys()
	{
		// The kit destroyed every scene before this call.
		if (bJoltRegistered)
		{
			JPH::UnregisterTypes();
			delete JPH::Factory::sInstance;
			JPH::Factory::sInstance = nullptr;
			bJoltRegistered = false;
		}
		GKitForHooks = nullptr;
		return PT_OK;
	}

	PtResult FJoltBackend::CreateScene(const PtSceneDesc& Desc, Abi::IPhysicsBackendScene*& OutScene)
	{
		FJoltSceneOptions Options;
		for (uint32 Index = 0; Index < Desc.NumOptions; ++Index)
		{
			char Error[256] = {};
			const PtResult Result = ApplySceneOption(Options, Abi::FStringRef::FromCString(Desc.Options[Index].Key), Abi::FStringRef::FromCString(Desc.Options[Index].Value), Error);
			if (Result != PT_OK)
			{
				return GetKit().SetLastError(Result, "%s", Error);
			}
		}
		FPhysScene_Jolt* Scene = new (::std::nothrow) FPhysScene_Jolt(Desc, Options);
		if (Scene == nullptr)
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "Jolt: cannot allocate a scene");
		}
		++NumLiveScenes;
		OutScene = Scene;
		return PT_OK;
	}

	void FJoltBackend::DestroyScene(Abi::IPhysicsBackendScene* Scene)
	{
		delete Scene;
		--NumLiveScenes;
	}

	PtResult FJoltBackend::SetModuleOption(const Abi::FStringRef Key, const Abi::FStringRef /*Value*/)
	{
		return GetKit().SetLastError(PT_E_INVALID_ARG, "Jolt: unknown module option '%.*s' (Jolt has no module options)", static_cast<int>(Key.Length), Key.Data);
	}

	PtResult FJoltBackend::DumpModuleSettings(Abi::FTextWriter& Out)
	{
		char Text[64];
		// From the headers this TU compiled against and the pinned source commit (CMake PT_JOLT_PIN_SHA), never a literal.
		snprintf(Text, sizeof(Text), "%d.%d.%d (%.8s)", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, PT_JOLT_SOURCE_COMMIT);
		Out.AppendEffective("jolt.version", Text, "", static_cast<double>(JPH_VERSION_MAJOR * 10000 + JPH_VERSION_MINOR * 100 + JPH_VERSION_PATCH), "",
			"COMPILE_TIME");
		Out.AppendEffective("jolt.module_config_string", JPH::GetConfigurationString(), "", nan(""), "", "COMPILE_TIME");
		snprintf(Text, sizeof(Text), "%u", MaxThreads);
		Out.AppendEffective("jolt.max_threads", Text, "threads", static_cast<double>(MaxThreads), "threads", "READBACK");
		snprintf(Text, sizeof(Text), "%u", NumLiveScenes);
		Out.AppendEffective("jolt.live_scenes", Text, "scenes", static_cast<double>(NumLiveScenes), "scenes", "READBACK");
		const uint32 Asserts = GNumJoltAsserts.load(std::memory_order_relaxed);
		snprintf(Text, sizeof(Text), "%u", Asserts);
		Out.AppendEffective("jolt.asserts", Text, "asserts", static_cast<double>(Asserts), "asserts", "READBACK");
		return PT_OK;
	}

	PtResult FJoltBackend::GetMemoryStats(PtMemoryStats& Out)
	{
		// Module level: Jolt's heap use is not counted (no allocator hook), so the byte and allocation fields keep the Kit's
		// -1 (unknown); the per-scene numbers come from GetMemoryStats(Scene) (the Jolt temp block).
		Out.AllocatorKind = 2;
		return PT_OK;
	}
}

// The static entry the generated backend registry references (PTBackendRegistry.gen.cpp).
PT_IMPLEMENT_BACKEND_MODULE_STATIC(PhysicsTest::JoltBackend::FJoltBackend, PT_GetBackendApi_Jolt)
