// PhysXBackend.cpp - FPhysXBackend (module: InitGamePhys / TermGamePhys, scene creation, options) and the static entry
// PT_GetBackendApi_PhysX that the generated backend registry references (02_Architecture 4.5, 8.2, A.14), plus the
// plain C test hooks of PhysXBackendTestHooks.h.
#include "PhysScene_PhysX.h"

#include "PhysicsFiltering.h"
#include "PhysicsInitialization.h"
#include "PhysXBackend/PhysXBackendTestHooks.h"
#include "PtBuildReport.h"

#include <math.h>
#include <new>
#include <stdio.h>

#ifndef PT_PHYSX_SOURCE_COMMIT
	#define PT_PHYSX_SOURCE_COMMIT "unknown"
#endif

namespace PhysicsTest::PhysXBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// Adapter-TU self-report (G0, DL49): the PhysX PUBLIC definitions this TU compiles with (PT::PhysX).
		// PX_PHYSICS_VERSION is an expression, so the version is reported through its three literal parts (PhysXSmoke.cpp).
#if defined(PX_PHYSX_STATIC_LIB)
	#define PT_PHYSX_BR_STATIC "\"PX_PHYSX_STATIC_LIB\":1,"
#else
	#define PT_PHYSX_BR_STATIC "\"PX_PHYSX_STATIC_LIB\":0,"
#endif
#define PT_PHYSX_VERSION_TEXT PT_BR_STR(PX_PHYSICS_VERSION_MAJOR) "." PT_BR_STR(PX_PHYSICS_VERSION_MINOR) "." PT_BR_STR(PX_PHYSICS_VERSION_BUGFIX)
		PT_BUILD_REPORT_JSON(GPhysXBuildReport, "\"backend\":\"PhysX\",\"PX_PHYSICS_VERSION\":\"" PT_PHYSX_VERSION_TEXT "\"," PT_PHYSX_BR_STATIC);

		const char* const GRequiredEffectiveKeys[] = { "physx.tolerances_length", "physx.tolerances_speed", "physx.cpu_dispatcher" };

		const char* GetBuildConfig()
		{
#if defined(_DEBUG)
			return "Debug";
#else
			return "Release";
#endif
		}
	}

	void FPhysXBackend::GetInfo(PtBackendInfo& Out) const
	{
		Out.Name = ModuleName;
		Out.DisplayName = "PhysX 5.11 - UE4 settings on PhysX 5.11 (UE4-style integration, clean room)";
		Out.EngineVersion = "PhysX " PT_PHYSX_VERSION_TEXT;
		Out.SourceCommit = PT_PHYSX_SOURCE_COMMIT;
		Out.BuildConfig = GetBuildConfig();
		Out.BuildFlagsJson = GPhysXBuildReport;
		// 02 7.5: contact points (viz) with the solver's own "active" (the point's normal impulse) and bounds through the debug
		// snapshot; no native islands, no native frame model. The impulse values themselves stay NaN until M9 (Q8).
		Out.Capabilities = PT_CAP_COLLISION | PT_CAP_KINEMATIC | PT_CAP_SLEEPING | PT_CAP_CCD | PT_CAP_GYROSCOPIC | PT_CAP_MULTITHREADED | PT_CAP_ACTIVE_LIST
			| PT_CAP_STATE_WRITE | PT_CAP_NATIVE_HASH | PT_CAP_CONTACT_POINTS | PT_CAP_CONTACT_ACTIVE_EXACT | PT_CAP_BOUNDS;
		// One PxPhysics and one PxTolerancesScale per process (PxPhysics.h:997, PxSceneDesc.h:541 [V]): the init options
		// (ToleranceScale) are process-wide. UE4Compat / -TGS / -Stab share them; SdkDefault needs its own process.
		Out.ModuleFlags = PT_MODULE_GLOBAL_SCENE_SETTINGS;
		Out.NumPhaseTimers = 0;
		Out.PhaseTimerNames = nullptr;
		Out.NumRequiredEffectiveKeys = static_cast<uint32>(sizeof(GRequiredEffectiveKeys) / sizeof(GRequiredEffectiveKeys[0]));
		Out.RequiredEffectiveKeys = GRequiredEffectiveKeys;
	}

	PtResult FPhysXBackend::InitGamePhys(const PtInitParams& Params)
	{
		FPhysXInitOptions Options;
		for (uint32 Index = 0; Index < Params.NumOptions; ++Index)
		{
			char Error[256] = {};
			const PtResult Result = ApplyInitOption(Options, Abi::FStringRef::FromCString(Params.Options[Index].Key), Abi::FStringRef::FromCString(Params.Options[Index].Value),
				Error);
			if (Result != PT_OK)
			{
				return GetKit().SetLastError(Result, "%s", Error);
			}
		}
		char Error[256] = {};
		const PtResult Result = PhysXBackend::InitGamePhys(Options, GetKit(), Error);
		if (Result != PT_OK)
		{
			return GetKit().SetLastError(Result, "%s", Error);
		}
		MaxThreads = Params.MaxThreads;
		ToleranceLength = Options.ToleranceLength;
		ToleranceSpeed = Options.ToleranceSpeed;
		bToleranceScaleGiven = Options.bToleranceScaleGiven;
		return PT_OK;
	}

	PtResult FPhysXBackend::TermGamePhys()
	{
		// The kit destroyed every scene before this call.
		PhysXBackend::TermGamePhys();
		ReleaseThreadTaskStack();   // the owner thread's single-thread dispatcher stack
		return PT_OK;
	}

	PtResult FPhysXBackend::CreateScene(const PtSceneDesc& Desc, Abi::IPhysicsBackendScene*& OutScene)
	{
		FPhysXSceneConfig Config;
		for (uint32 Index = 0; Index < Desc.NumOptions; ++Index)
		{
			char Error[256] = {};
			const PtResult Result = ApplySceneOption(Config, Abi::FStringRef::FromCString(Desc.Options[Index].Key), Abi::FStringRef::FromCString(Desc.Options[Index].Value), Error);
			if (Result != PT_OK)
			{
				return GetKit().SetLastError(Result, "%s", Error);
			}
		}
		FPhysScene_PhysX* Scene = new (::std::nothrow) FPhysScene_PhysX(Desc, Config);
		if (Scene == nullptr)
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "PhysX: cannot allocate a scene");
		}
		char Error[256] = {};
		const PtResult Result = Scene->InitPhysScene(Error);
		if (Result != PT_OK)
		{
			delete Scene;
			return GetKit().SetLastError(Result, "%s", Error);
		}
		++NumLiveScenes;
		OutScene = Scene;
		return PT_OK;
	}

	void FPhysXBackend::DestroyScene(Abi::IPhysicsBackendScene* Scene)
	{
		delete Scene;
		--NumLiveScenes;
	}

	PtResult FPhysXBackend::SetModuleOption(const Abi::FStringRef Key, const Abi::FStringRef /*Value*/)
	{
		// The tolerances scale is fixed at InitGamePhys for the life of the PxPhysics.
		return GetKit().SetLastError(PT_E_INVALID_ARG, "PhysX: unknown runtime module option '%.*s' (ToleranceScale is an InitGamePhys option only)",
			static_cast<int>(Key.Length), Key.Data);
	}

	PtResult FPhysXBackend::DumpModuleSettings(Abi::FTextWriter& Out)
	{
		char Text[512];
		snprintf(Text, sizeof(Text), "%d.%d.%d (%.8s)", PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX, PT_PHYSX_SOURCE_COMMIT);
		Out.AppendEffective("physx.version", Text, "", static_cast<double>(PX_PHYSICS_VERSION_MAJOR * 10000 + PX_PHYSICS_VERSION_MINOR * 100 + PX_PHYSICS_VERSION_BUGFIX), "",
			"COMPILE_TIME");
		const physx::PxTolerancesScale& Tolerances = GetPhysXSDK().getTolerancesScale();
		snprintf(Text, sizeof(Text), "%.9g (PxPhysics::getTolerancesScale; %s)", static_cast<double>(Tolerances.length),
			bToleranceScaleGiven ? "init option ToleranceScale" : "no init option: the UE4Compat default (100, 1000)");
		Out.AppendEffective("physx.tolerances_length", Text, "native", static_cast<double>(Tolerances.length), "native", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g (PxPhysics::getTolerancesScale)", static_cast<double>(Tolerances.speed));
		Out.AppendEffective("physx.tolerances_speed", Text, "native/s", static_cast<double>(Tolerances.speed), "native/s", "READBACK");
		// The host's input and the adapter's own counters are SET_ONLY (docs/Abi.md 5.14); the live scenes are PhysX's count.
		snprintf(Text, sizeof(Text), "%u (PtInitParams::MaxThreads, the host's budget)", MaxThreads);
		Out.AppendEffective("physx.max_threads", Text, "threads", static_cast<double>(MaxThreads), "threads", "SET_ONLY");
		const physx::PxU32 NumScenes = GetPhysXSDK().getNbScenes();
		snprintf(Text, sizeof(Text), "%u (PxPhysics::getNbScenes; %u created by this module)", NumScenes, NumLiveScenes);
		Out.AppendEffective("physx.live_scenes", Text, "scenes", static_cast<double>(NumScenes), "scenes", "READBACK");
		snprintf(Text, sizeof(Text), "%u PxPhysics created in this process (one live at a time)", GetNumPhysXInitializations());
		Out.AppendEffective("physx.initializations", Text, "", static_cast<double>(GetNumPhysXInitializations()), "", "SET_ONLY");
		const FPhysXAllocator& Allocator = GetPhysXAllocator();
		snprintf(Text, sizeof(Text), "FPhysXAllocator (16-byte aligned): live %lld bytes, peak %lld bytes, %lld allocations, %lld frees",
			static_cast<long long>(Allocator.GetLiveBytes()), static_cast<long long>(Allocator.GetPeakBytes()), static_cast<long long>(Allocator.GetNumAllocs()),
			static_cast<long long>(Allocator.GetNumFrees()));
		Out.AppendEffective("physx.allocator", Text, "bytes", static_cast<double>(Allocator.GetLiveBytes()), "bytes", "SET_ONLY");   // the adapter's callback counts
		const FPhysXErrorCounts Counts = GetPhysXErrorCallback().GetCounts();
		snprintf(Text, sizeof(Text), "info %u, warning %u, invalid parameter %u, invalid operation %u, out of memory %u, internal %u, abort %u, perf warning %u",
			Counts.Counts[0], Counts.Counts[1], Counts.Counts[2], Counts.Counts[3], Counts.Counts[4], Counts.Counts[5], Counts.Counts[6], Counts.Counts[7]);
		Out.AppendEffective("physx.error_reports", Text, "reports", static_cast<double>(GetPhysXErrorCallback().GetTotal()), "reports", "SET_ONLY");
		Out.AppendEffective("physx.fidelity", GPhysXFidelityNote, "", nan(""), "", "COMPILE_TIME");
		return PT_OK;
	}

	PtResult FPhysXBackend::GetMemoryStats(PtMemoryStats& Out)
	{
		const FPhysXAllocator& Allocator = GetPhysXAllocator();
		Out.LiveBytes = Allocator.GetLiveBytes();
		Out.PeakBytes = Allocator.GetPeakBytes();
		Out.NumAllocs = Allocator.GetNumAllocs();
		Out.NumFrees = Allocator.GetNumFrees();
		Out.AllocatorKind = 0;   // FPhysXAllocator = _aligned_malloc
		return PT_OK;
	}

	// The material combine modes: CreateMaterials casts PT_COMBINE_* to PxCombineMode::Enum and the effective settings compare
	// the two, so the values must stay equal (PtBackendAbi.h, PxMaterial.h:104-107 [V]).
	static_assert(PT_COMBINE_AVERAGE == static_cast<uint32>(physx::PxCombineMode::eAVERAGE) && PT_COMBINE_MIN == static_cast<uint32>(physx::PxCombineMode::eMIN)
		&& PT_COMBINE_MULTIPLY == static_cast<uint32>(physx::PxCombineMode::eMULTIPLY) && PT_COMBINE_MAX == static_cast<uint32>(physx::PxCombineMode::eMAX),
		"PT_COMBINE_* must equal PxCombineMode");
	// The PhysX values the public test hooks mirror (PhysXBackendTestHooks.h has no PhysX header).
	static_assert(PT_PHYSX_TEST_FILTER_KILL == static_cast<uint32>(physx::PxFilterFlag::eKILL), "PxFilterFlag::eKILL");
	static_assert(PT_PHYSX_TEST_FILTER_SUPPRESS == static_cast<uint32>(physx::PxFilterFlag::eSUPPRESS), "PxFilterFlag::eSUPPRESS");
	static_assert(PT_PHYSX_TEST_FILTER_DEFAULT == static_cast<uint32>(physx::PxFilterFlag::eDEFAULT), "PxFilterFlag::eDEFAULT");
	static_assert(PT_PHYSX_TEST_PAIR_SOLVE_CONTACT == static_cast<uint32>(physx::PxPairFlag::eSOLVE_CONTACT), "PxPairFlag::eSOLVE_CONTACT");
	static_assert(PT_PHYSX_TEST_PAIR_MODIFY_CONTACTS == static_cast<uint32>(physx::PxPairFlag::eMODIFY_CONTACTS), "PxPairFlag::eMODIFY_CONTACTS");
	static_assert(PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_FOUND == static_cast<uint32>(physx::PxPairFlag::eNOTIFY_TOUCH_FOUND), "PxPairFlag::eNOTIFY_TOUCH_FOUND");
	static_assert(PT_PHYSX_TEST_PAIR_NOTIFY_TOUCH_PERSISTS == static_cast<uint32>(physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS), "PxPairFlag::eNOTIFY_TOUCH_PERSISTS");
	static_assert(PT_PHYSX_TEST_PAIR_NOTIFY_CONTACT_POINTS == static_cast<uint32>(physx::PxPairFlag::eNOTIFY_CONTACT_POINTS), "PxPairFlag::eNOTIFY_CONTACT_POINTS");
	static_assert(PT_PHYSX_TEST_PAIR_DETECT_DISCRETE_CONTACT == static_cast<uint32>(physx::PxPairFlag::eDETECT_DISCRETE_CONTACT), "PxPairFlag::eDETECT_DISCRETE_CONTACT");
	static_assert(PT_PHYSX_TEST_PAIR_DETECT_CCD_CONTACT == static_cast<uint32>(physx::PxPairFlag::eDETECT_CCD_CONTACT), "PxPairFlag::eDETECT_CCD_CONTACT");
	static_assert(PT_PHYSX_TEST_EPDF_CCD == EPDF_CCD && PT_PHYSX_TEST_EPDF_CONTACT_NOTIFY == EPDF_ContactNotify && PT_PHYSX_TEST_EPDF_STATIC_SHAPE == EPDF_StaticShape
		&& PT_PHYSX_TEST_EPDF_MODIFY_CONTACTS == EPDF_ModifyContacts && PT_PHYSX_TEST_EPDF_KINEMATIC_KINEMATIC_PAIRS == EPDF_KinematicKinematicPairs
		&& PT_PHYSX_TEST_EPDF_SIMPLE_COLLISION == EPDF_SimpleCollision && PT_PHYSX_TEST_EPDF_COMPLEX_COLLISION == EPDF_ComplexCollision, "EPhysXFilterDataFlags");
}

// The static entry the generated backend registry references (PTBackendRegistry.gen.cpp).
PT_IMPLEMENT_BACKEND_MODULE_STATIC(PhysicsTest::PhysXBackend::FPhysXBackend, PT_GetBackendApi_PhysX)

// ---- test hooks (PhysXBackendTestHooks.h) -------------------------------------------------------------------------------
extern "C" void PT_CALL PT_PhysXTest_MakeSimFilterData(const uint32_t BodyIndex, const uint32_t BlockMask, const uint32_t Channel, const uint32_t EpdfFlags, uint32_t OutWords[4])
{
	const physx::PxFilterData Data = PhysicsTest::PhysXBackend::MakeSimFilterData(BodyIndex, BlockMask, BodyIndex, Channel, EpdfFlags);
	OutWords[0] = Data.word0;
	OutWords[1] = Data.word1;
	OutWords[2] = Data.word2;
	OutWords[3] = Data.word3;
}

namespace PhysicsTest::PhysXBackend
{
	// The two hooks' body (in the backend namespace: Probe.AdapterComdatsPhysX allows adapter functions there).
	static void RunSimFilterShader(const PtPhysXTestFilterObject* A, const PtPhysXTestFilterObject* B, const void* ConstantBlock, const physx::PxU32 ConstantBlockSize,
		PtPhysXTestFilterResult* Out)
	{
		const auto MakeAttributes = [](const uint32_t Kind) -> physx::PxFilterObjectAttributes
		{
			switch (Kind)
			{
			case PT_PHYSX_TEST_OBJECT_STATIC: return static_cast<physx::PxFilterObjectAttributes>(physx::PxFilterObjectType::eRIGID_STATIC);
			case PT_PHYSX_TEST_OBJECT_KINEMATIC:
				return static_cast<physx::PxFilterObjectAttributes>(physx::PxFilterObjectType::eRIGID_DYNAMIC)
					| static_cast<physx::PxFilterObjectAttributes>(physx::PxFilterObjectFlag::eKINEMATIC);
			default: return static_cast<physx::PxFilterObjectAttributes>(physx::PxFilterObjectType::eRIGID_DYNAMIC);
			}
		};
		const physx::PxFilterData Data0(A->Words[0], A->Words[1], A->Words[2], A->Words[3]);
		const physx::PxFilterData Data1(B->Words[0], B->Words[1], B->Words[2], B->Words[3]);
		physx::PxPairFlags PairFlags;
		const physx::PxFilterFlags Flags = PhysXSimFilterShader(MakeAttributes(A->ObjectKind), Data0, MakeAttributes(B->ObjectKind), Data1,
			PairFlags, ConstantBlock, ConstantBlockSize);
		Out->FilterFlags = static_cast<uint32_t>(static_cast<physx::PxU16>(Flags));
		Out->PairFlags = static_cast<uint32_t>(static_cast<physx::PxU16>(PairFlags));
	}
}

extern "C" void PT_CALL PT_PhysXTest_RunSimFilterShader(const PtPhysXTestFilterObject* A, const PtPhysXTestFilterObject* B, PtPhysXTestFilterResult* Out)
{
	PhysicsTest::PhysXBackend::RunSimFilterShader(A, B, nullptr, 0, Out);
}

extern "C" void PT_CALL PT_PhysXTest_RunSimFilterShaderWithRules(const PtPhysXTestFilterObject* A, const PtPhysXTestFilterObject* B, const uint32_t NonBlockingSuppress,
	const uint32_t KinematicStaticAlways, PtPhysXTestFilterResult* Out)
{
	PhysicsTest::PhysXBackend::FPhysXFilterShaderRules Rules;
	Rules.NonBlocking = NonBlockingSuppress != 0 ? static_cast<uint32_t>(PhysicsTest::PhysXBackend::EPhysXNonBlockingPairFilter::Suppress)
		: static_cast<uint32_t>(PhysicsTest::PhysXBackend::EPhysXNonBlockingPairFilter::Kill);
	Rules.KinematicStatic = KinematicStaticAlways != 0 ? static_cast<uint32_t>(PhysicsTest::PhysXBackend::EPhysXKinematicStaticPairFilter::Always)
		: static_cast<uint32_t>(PhysicsTest::PhysXBackend::EPhysXKinematicStaticPairFilter::UnlessKinematicPairs);
	PhysicsTest::PhysXBackend::RunSimFilterShader(A, B, &Rules, sizeof(Rules), Out);
}
