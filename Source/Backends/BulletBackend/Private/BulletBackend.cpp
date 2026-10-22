// BulletBackend.cpp - FBulletBackend (module: Bullet's process-wide settings, scene creation, options) and the static entry
// PT_GetBackendApi_Bullet that the generated backend registry references (02_Architecture 4.5, 7.5, 8.4, A.15). [MVP M5]
//
// PT_MODULE_GLOBAL_SCENE_SETTINGS (02 7.5): Bullet keeps the material combine rule, the deactivation time and the contact
// breaking threshold in process globals (gCalculateCombined*Callback, gDeactivationTime, gContactBreakingThreshold;
// btManifoldResult.cpp:24-29, btRigidBody.cpp:25, btPersistentManifold.cpp:26 [V]). They are module options
// ([PhysicsTest.Backend.Bullet.Init], InitGamePhys only), installed by InitGamePhys and restored by TermGamePhys, so the
// registry and the viewer apply their one-set-of-init-options-per-process rule (FPhysicsBackendRegistry::FindOrLoad,
// FViewerSession::ClassifyBackendSwitch: another rule needs its own process).
#include "PhysScene_Bullet.h"

#include "BulletCombine.h"
#include "BulletMath.h"
#include "PtBuildReport.h"

#include <atomic>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PT_BULLET_SOURCE_COMMIT
	#define PT_BULLET_SOURCE_COMMIT "unknown"
#endif

namespace PhysicsTest::BulletBackend
{
	namespace Abi = ::PhysicsTest::Abi;

	namespace
	{
		// Adapter-TU self-report (G0, DL49): the Bullet PUBLIC definitions this TU compiles with (PT::Bullet).
#if defined(BT_USE_DOUBLE_PRECISION)
	#define PT_BULLET_BR_DOUBLE "\"BT_USE_DOUBLE_PRECISION\":1,"
#else
	#define PT_BULLET_BR_DOUBLE "\"BT_USE_DOUBLE_PRECISION\":0,"
#endif
// PT::Bullet exports BT_THREADSAFE explicitly (cmake/ThirdParty/PtBullet.cmake), as in the Bullet smoke test.
#if !defined(BT_THREADSAFE)
	#error "PT::Bullet must export BT_THREADSAFE (cmake/ThirdParty/PtBullet.cmake _pt_bullet_header_defines)"
#elif BT_THREADSAFE
	#define PT_BULLET_BR_THREADSAFE "\"BT_THREADSAFE\":1,"
#else
	#define PT_BULLET_BR_THREADSAFE "\"BT_THREADSAFE\":0,"
#endif
#if defined(BT_USE_SSE)
	#define PT_BULLET_BR_SSE "\"BT_USE_SSE\":1,"
#else
	#define PT_BULLET_BR_SSE "\"BT_USE_SSE\":0,"
#endif
		PT_BUILD_REPORT_JSON(GBulletBuildReport, "\"backend\":\"Bullet\",\"bullet_version\":" PT_BR_STR(BT_BULLET_VERSION) "," PT_BULLET_BR_DOUBLE
			PT_BULLET_BR_THREADSAFE PT_BULLET_BR_SSE);

		// The backend's required effective keys (A7 checks them with the generic set).
		const char* const GRequiredEffectiveKeys[] = { "bullet.solver_mode", "bullet.angular_motion_threshold" };

		// Counting wrappers for Bullet's heap (M7 fix round 0, docs/TimingContract.md 4.1). Every btAlignedAlloc /
		// btAlignedFree (btAlignedObjectArray, BT_DECLARE_ALIGNED_ALLOCATOR's new / delete, the pools' fallbacks) goes to
		// btAlignedAllocDefault, which takes its block from the unaligned hook (sAllocFunc) because this MSVC build leaves
		// BT_HAS_ALIGNED_ALLOCATOR off (btScalar.h:92, btAlignedAllocator.cpp:63-92 [V]). btAlignedAllocSetCustom
		// (btAlignedAllocator.h:58 [V], a public hook) replaces that unaligned hook: the wrappers do what btAllocDefault /
		// btFreeDefault do (malloc + zero fill, free; btAlignedAllocator.cpp:25-35 [V]) plus a relaxed atomic count, so the
		// engine's results do not change. Nothing in Bullet is modified.
		std::atomic<int64> GBulletHeapAllocs{ 0 };
		std::atomic<int64> GBulletHeapFrees{ 0 };

		void* CountingBulletAlloc(const size_t Size)
		{
			GBulletHeapAllocs.fetch_add(1, std::memory_order_relaxed);
			char* Data = static_cast<char*>(malloc(Size));
			if (Data != nullptr)
			{
				memset(Data, 0, Size);   // as btAllocDefault ("keep msan happy")
			}
			return Data;
		}

		void CountingBulletFree(void* Block)
		{
			if (Block != nullptr)
			{
				GBulletHeapFrees.fetch_add(1, std::memory_order_relaxed);
			}
			free(Block);
		}

		const char* GetBuildConfig()
		{
#if defined(_DEBUG)
			return "Debug";
#else
			return "Release";
#endif
		}

		// "Bullet 3.25" from BT_BULLET_VERSION (325) of the headers this TU compiled against, never a literal.
		const char* GetEngineVersion()
		{
			static char Text[32] = {};
			if (Text[0] == '\0')
			{
				snprintf(Text, sizeof(Text), "Bullet %d.%02d", BT_BULLET_VERSION / 100, BT_BULLET_VERSION % 100);
			}
			return Text;
		}

		bool EqualsNoCase(const char* A, const char* B)
		{
			for (;; ++A, ++B)
			{
				const char CA = (*A >= 'A' && *A <= 'Z') ? static_cast<char>(*A - 'A' + 'a') : *A;
				const char CB = (*B >= 'A' && *B <= 'Z') ? static_cast<char>(*B - 'A' + 'a') : *B;
				if (CA != CB)
				{
					return false;
				}
				if (CA == '\0')
				{
					return true;
				}
			}
		}

		bool ParseFinite(const char* Text, double& Out)
		{
			char* End = nullptr;
			const double Value = strtod(Text, &End);
			if (End == Text || *End != '\0' || !Abi::IsFinite(Value))
			{
				return false;
			}
			Out = Value;
			return true;
		}

		// One [PhysicsTest.Backend.Bullet.Init] key. PT_OK, or PT_E_INVALID_ARG with a message in Error.
		PtResult ApplyModuleOption(FBulletModuleOptions& Options, const char* Key, const char* Value, char (&Error)[256])
		{
			double Real = 0.0;
			if (EqualsNoCase(Key, "CombineRule"))
			{
				if (EqualsNoCase(Value, "UE"))
				{
					Options.CombineRule = ECombineRule::UE;
					return PT_OK;
				}
				if (EqualsNoCase(Value, "Bullet"))
				{
					Options.CombineRule = ECombineRule::Bullet;
					return PT_OK;
				}
				snprintf(Error, sizeof(Error), "Bullet: module option CombineRule must be UE or Bullet");
				return PT_E_INVALID_ARG;
			}
			if (EqualsNoCase(Key, "DeactivationTime"))
			{
				// 0 is refused: with gDeactivationTime == 0 btRigidBody::wantsSleeping is always false (btRigidBody.h:522 [V]) and
				// updateActivationState sets every body ACTIVE_TAG each step (btDiscreteDynamicsWorld.cpp:622-626 [V]) - sleep
				// silently off while sleep.enabled would still say true (M5 round 0). A profile turns sleep off instead.
				if (!ParseFinite(Value, Real) || !(Real > 0.0))
				{
					snprintf(Error, sizeof(Error), "Bullet: module option DeactivationTime must be a finite number of seconds > 0 (0 disables sleep; use the profile's "
						"bAllowSleeping)");
					return PT_E_INVALID_ARG;
				}
				Options.DeactivationTime = Real;
				return PT_OK;
			}
			if (EqualsNoCase(Key, "ContactBreakingThreshold"))
			{
				// gContactBreakingThreshold as Bullet takes it: a unitless factor of the shapes' angular motion disc (relative mode)
				// and the AABB inflation in m (FBulletModuleOptions). M5 round 0: it was read as cm and converted, which matched the
				// default only by coincidence.
				if (!ParseFinite(Value, Real) || !(Real > 0.0))
				{
					snprintf(Error, sizeof(Error), "Bullet: module option ContactBreakingThreshold must be a finite number > 0 (gContactBreakingThreshold: a factor of "
						"the shape's angular motion disc, and the AABB inflation in m; default 0.02)");
					return PT_E_INVALID_ARG;
				}
				Options.ContactBreakingThreshold = Real;
				return PT_OK;
			}
			snprintf(Error, sizeof(Error), "Bullet: unknown module option '%s' (CombineRule, DeactivationTime, ContactBreakingThreshold)", Key);
			return PT_E_INVALID_ARG;
		}
	}

	void FBulletBackend::GetInfo(PtBackendInfo& Out) const
	{
		Out.Name = ModuleName;
		Out.DisplayName = "Bullet Physics 3.25";
		Out.EngineVersion = GetEngineVersion();
		Out.SourceCommit = PT_BULLET_SOURCE_COMMIT;
		Out.BuildConfig = GetBuildConfig();
		Out.BuildFlagsJson = GBulletBuildReport;
		// 02 7.5: contact points with the solver's impulses and an exact active flag, Bullet's island tags, bounds. M9: ball and
		// hinge joints (btPoint2PointConstraint / btHingeConstraint) with their snapshot (btJointFeedback impulses), the native
		// debug draw of viz runs (FBulletDebugDrawer). No native frame model, single threaded (BT_THREADSAFE=0).
		Out.Capabilities = PT_CAP_COLLISION | PT_CAP_KINEMATIC | PT_CAP_SLEEPING | PT_CAP_CCD | PT_CAP_GYROSCOPIC | PT_CAP_ACTIVE_LIST | PT_CAP_STATE_WRITE
			| PT_CAP_NATIVE_HASH | PT_CAP_CONTACT_POINTS | PT_CAP_CONTACT_NORMAL_IMPULSE | PT_CAP_CONTACT_FRICTION_IMPULSE | PT_CAP_CONTACT_ACTIVE_EXACT
			| PT_CAP_NATIVE_ISLANDS | PT_CAP_BOUNDS | PT_CAP_JOINT_BALL | PT_CAP_JOINT_HINGE | PT_CAP_JOINT_SNAPSHOT | PT_CAP_NATIVE_DEBUG_DRAW;
		Out.ModuleFlags = PT_MODULE_GLOBAL_SCENE_SETTINGS;   // Bullet's combine callbacks and sleep / contact globals
		Out.NumPhaseTimers = 0;
		Out.PhaseTimerNames = nullptr;
		Out.NumRequiredEffectiveKeys = static_cast<uint32>(sizeof(GRequiredEffectiveKeys) / sizeof(GRequiredEffectiveKeys[0]));
		Out.RequiredEffectiveKeys = GRequiredEffectiveKeys;
	}

	PtResult FBulletBackend::InitGamePhys(const PtInitParams& Params)
	{
		FBulletModuleOptions Options;
		for (uint32 Index = 0; Index < Params.NumOptions; ++Index)
		{
			char Error[256] = {};
			const PtResult Result = ApplyModuleOption(Options, Params.Options[Index].Key, Params.Options[Index].Value, Error);
			if (Result != PT_OK)
			{
				return GetKit().SetLastError(Result, "%s", Error);
			}
		}
		ModuleOptions = Options;
		MaxThreads = Params.MaxThreads;
		// The process-wide settings, owned by this module between InitGamePhys and TermGamePhys.
		SavedFrictionCallback = gCalculateCombinedFrictionCallback;
		SavedRestitutionCallback = gCalculateCombinedRestitutionCallback;
		SavedDeactivationTime = gDeactivationTime;
		SavedContactBreakingThreshold = gContactBreakingThreshold;
		if (ModuleOptions.CombineRule == ECombineRule::UE)
		{
			gCalculateCombinedFrictionCallback = &CombineFrictionUE;
			gCalculateCombinedRestitutionCallback = &CombineRestitutionUE;
		}
		else
		{
			gCalculateCombinedFrictionCallback = &btManifoldResult::calculateCombinedFriction;
			gCalculateCombinedRestitutionCallback = &btManifoldResult::calculateCombinedRestitution;
		}
		gDeactivationTime = btScalar(ModuleOptions.DeactivationTime);
		gContactBreakingThreshold = btScalar(ModuleOptions.ContactBreakingThreshold);
		btAlignedAllocSetCustom(&CountingBulletAlloc, &CountingBulletFree);   // the heap count (both sides are malloc / free)
		bGlobalsInstalled = true;
		GetKit().LogToHost(PT_LOG_LOG, "LogBulletBackend", "Bullet %d.%02d initialised (combine rule %s, deactivation time %g s, contact breaking threshold %g), "
			"MaxThreads %u (Bullet runs single threaded)", BT_BULLET_VERSION / 100, BT_BULLET_VERSION % 100, ModuleOptions.CombineRule == ECombineRule::UE ? "UE" : "Bullet",
			ModuleOptions.DeactivationTime, ModuleOptions.ContactBreakingThreshold, MaxThreads);
		return PT_OK;
	}

	PtResult FBulletBackend::TermGamePhys()
	{
		// The kit destroyed every scene before this call.
		if (bGlobalsInstalled)
		{
			gCalculateCombinedFrictionCallback = SavedFrictionCallback;
			gCalculateCombinedRestitutionCallback = SavedRestitutionCallback;
			gDeactivationTime = SavedDeactivationTime;
			gContactBreakingThreshold = SavedContactBreakingThreshold;
			btAlignedAllocSetCustom(nullptr, nullptr);   // Bullet's defaults again (malloc / free, as the wrappers)
			bGlobalsInstalled = false;
		}
		return PT_OK;
	}

	PtResult FBulletBackend::CreateScene(const PtSceneDesc& Desc, Abi::IPhysicsBackendScene*& OutScene)
	{
		FBulletSceneOptions Options;
		for (uint32 Index = 0; Index < Desc.NumOptions; ++Index)
		{
			char Error[256] = {};
			const PtResult Result = ApplySceneOption(Options, Abi::FStringRef::FromCString(Desc.Options[Index].Key), Abi::FStringRef::FromCString(Desc.Options[Index].Value), Error);
			if (Result != PT_OK)
			{
				return GetKit().SetLastError(Result, "%s", Error);
			}
		}
		FPhysScene_Bullet* Scene = new (::std::nothrow) FPhysScene_Bullet(Desc, Options);
		if (Scene == nullptr)
		{
			return GetKit().SetLastError(PT_E_OUT_OF_MEMORY, "Bullet: cannot allocate a scene");
		}
		++NumLiveScenes;
		OutScene = Scene;
		return PT_OK;
	}

	void FBulletBackend::DestroyScene(Abi::IPhysicsBackendScene* Scene)
	{
		delete Scene;
		--NumLiveScenes;
	}

	PtResult FBulletBackend::SetModuleOption(const Abi::FStringRef Key, const Abi::FStringRef /*Value*/)
	{
		// GLOBAL_SCENE_SETTINGS: the module options are fixed at InitGamePhys (another value needs its own process).
		return GetKit().SetLastError(PT_E_INVALID_ARG, "Bullet: module option '%.*s' can only be given to InitGamePhys (CombineRule, DeactivationTime, "
			"ContactBreakingThreshold; PT_MODULE_GLOBAL_SCENE_SETTINGS)", static_cast<int>(Key.Length), Key.Data);
	}

	PtResult FBulletBackend::DumpModuleSettings(Abi::FTextWriter& Out)
	{
		char Text[256];
		// From the headers this TU compiled against and the pinned source commit (CMake PT_BULLET_PIN_SHA), never a literal.
		snprintf(Text, sizeof(Text), "%d.%02d (%.8s)", BT_BULLET_VERSION / 100, BT_BULLET_VERSION % 100, PT_BULLET_SOURCE_COMMIT);
		Out.AppendEffective("bullet.version", Text, "", static_cast<double>(BT_BULLET_VERSION), "", "COMPILE_TIME");
		// The host's input and the adapter's own count are SET_ONLY (docs/Abi.md 5.14; M5 round 0).
		snprintf(Text, sizeof(Text), "%u (PtInitParams::MaxThreads; Bullet runs single threaded, BT_THREADSAFE=0)", MaxThreads);
		Out.AppendEffective("bullet.max_threads", Text, "threads", static_cast<double>(MaxThreads), "threads", "SET_ONLY");
		snprintf(Text, sizeof(Text), "%u", NumLiveScenes);
		Out.AppendEffective("bullet.live_scenes", Text, "scenes", static_cast<double>(NumLiveScenes), "scenes", "SET_ONLY");
		const bool bUECombine = gCalculateCombinedFrictionCallback == &CombineFrictionUE && gCalculateCombinedRestitutionCallback == &CombineRestitutionUE;
		const bool bBulletCombine = gCalculateCombinedFrictionCallback == &btManifoldResult::calculateCombinedFriction
			&& gCalculateCombinedRestitutionCallback == &btManifoldResult::calculateCombinedRestitution;
		Out.AppendEffective("bullet.combine_rule", bUECombine ? "UE (PtCombine through gCalculateCombined*Callback)" : (bBulletCombine ? "Bullet (native callbacks)" : "other"),
			"", bUECombine ? 0.0 : (bBulletCombine ? 1.0 : nan("")), "", "READBACK");
		snprintf(Text, sizeof(Text), "%.9g s (gDeactivationTime)", double(gDeactivationTime));
		Out.AppendEffective("bullet.deactivation_time_global", Text, "s", double(gDeactivationTime), "s", "READBACK");
		// Two roles (FBulletModuleOptions): the relative factor of the manifold breaking threshold and the AABB inflation in m.
		snprintf(Text, sizeof(Text), "%.9g (gContactBreakingThreshold: x each shape's angular motion disc per manifold, CD_USE_RELATIVE_CONTACT_BREAKING_THRESHOLD; "
			"AABB inflation %.9g m)", double(gContactBreakingThreshold), double(gContactBreakingThreshold));
		Out.AppendEffective("bullet.contact_breaking_threshold", Text, "factor", double(gContactBreakingThreshold), "factor", "READBACK");
		Out.AppendEffective("bullet.disable_deactivation_global", gDisableDeactivation ? "true (gDisableDeactivation)" : "false (gDisableDeactivation)", "bool",
			gDisableDeactivation ? 1.0 : 0.0, "bool", "READBACK");
		return PT_OK;
	}

	int64 GetBulletHeapAllocCount()
	{
		return GBulletHeapAllocs.load(std::memory_order_relaxed);
	}

	int64 GetBulletHeapFreeCount()
	{
		return GBulletHeapFrees.load(std::memory_order_relaxed);
	}

	PtResult FBulletBackend::GetMemoryStats(PtMemoryStats& Out)
	{
		// Module level: Bullet's heap calls through the counting wrappers (process-wide); the byte fields keep the Kit's -1
		// (the wrappers count calls, not sizes). The per-scene numbers come from GetMemoryStats(Scene) (the pools).
		Out.NumAllocs = GetBulletHeapAllocCount();
		Out.NumFrees = GetBulletHeapFreeCount();
		Out.AllocatorKind = 0;
		return PT_OK;
	}
}

// The static entry the generated backend registry references (PTBackendRegistry.gen.cpp).
PT_IMPLEMENT_BACKEND_MODULE_STATIC(PhysicsTest::BulletBackend::FBulletBackend, PT_GetBackendApi_Bullet)
