// PtBackendKit.h - header-only, backend side (02_Architecture 7.3, A.2, docs/Abi.md). Compiled into each backend (CMake
// static lib / DLL, or a UBT module). A backend implements IPhysicsBackend + IPhysicsBackendScene and writes
//   PT_IMPLEMENT_BACKEND_MODULE(FMyBackend)                         // DLL: exports PT_GetBackendApi
//   PT_IMPLEMENT_BACKEND_MODULE_STATIC(FMyBackend, PT_GetBackendApi_My)   // static: one named entry per backend
// TBackendApiProvider<T> provides everything else: the C thunks, the thread guard (PT_E_WRONG_THREAD without side
// effects), the module and scene phase state machines, the PT_KIT_MAX_SCENES handle table, input validation (bounds,
// finite values, unit quaternions within 1e-6, index ranges, dynamic mass > 0, AsyncFixedTimeStepSize == -1 or > 0),
// the released-body set (repeated / double releases and commands or mass reads on released bodies are rejected the same
// way for every backend), the two-call capacity protocol, the MXCSR guard, the last-error text and the host log callback.
//
// Rules (DL29, A.2):
//   - No STL in the interface, no RTTI or exceptions required, no check / verify / ensure / min / max identifiers.
//   - Everything lives in namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { ... } }: PT_BACKEND_NS is a
//     PRIVATE definition per backend target (Null, Jolt, Bullet, PhysX; UBT: ChaosBridge), so non-template inline Kit
//     code compiled with different /arch never folds into one COMDAT, and no Kit state is shared between backends.
//     Never name it after a namespace the backend also uses (not "Chaos": with `using namespace PhysicsTest::Abi;`
//     every `Chaos::` in a bridge TU would be ambiguous with UE's ::Chaos).
//   - All state (last error, log callback, owner thread, scene slots, module state) is a static member of
//     TBackendApiProvider<TBackend>: one set per backend class, never an inline global.
//   - Default virtual implementations leave unused parameters unnamed (C4100 is an error under /W4 /WX).
//   - CMake builds catch C++ exceptions in every thunk and return PT_E_INTERNAL; with PT_ABI_NO_EXCEPTIONS (UE) or
//     without exception support (/EHs-c-) the thunks contain no try. SEH is never caught (crash dumps stay intact).
//   - MXCSR: each thunk restores the control bits (DAZ, exception masks, rounding, FTZ; bits 6-15) if the backend
//     changed them, logs a warning on the first restore and counts every restore (module-level effective setting
//     kit.mxcsr_restores; gate A6 fails on a restore). The x87 control word is not touched by x64 MSVC code and is not
//     saved per call (that needs FXSAVE); gate A6 checks that backends preserve it.
#pragma once

#include "PhysicsTestAbi/PtBackendAbi.h"

#ifndef PT_BACKEND_NS   // CMake: PT_BACKEND_NS=Jolt etc.; UBT: PrivateDefinitions "PT_BACKEND_NS=ChaosBridge"
	#error "PT_BACKEND_NS must be defined per backend target"
#endif
#include <intrin.h>   // __readgsdword, __iso_volatile_load32 / __iso_volatile_store32
#ifndef PT_KIT_CURRENT_THREAD_ID   // UE build: FPlatformTLS::GetCurrentThreadId()
	// TEB->ClientId.UniqueThread (x64 TEB offset 0x48): the same value as GetCurrentThreadId(), without windows.h.
	#define PT_KIT_CURRENT_THREAD_ID() ((uint32_t)__readgsdword(0x48))
#endif
#ifndef PT_KIT_MAX_SCENES
	#define PT_KIT_MAX_SCENES 16
#endif
#if !defined(PT_ABI_NO_EXCEPTIONS) && defined(_CPPUNWIND)
	#define PT_KIT_CATCH_EXCEPTIONS 1
#else
	#define PT_KIT_CATCH_EXCEPTIONS 0
#endif

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>   // the released-body set (malloc / realloc / free: no C++ library, A9)
#include <string.h>
#include <xmmintrin.h>

namespace PhysicsTest::Abi
{
inline namespace PT_BACKEND_NS
{
	// ---- value checks (bit tests: /fp:fast may fold isfinite and NaN comparisons, docs/Progress.md M0 4) -------------
	inline bool IsFinite(const double Value)
	{
		uint64_t Bits = 0;
		memcpy(&Bits, &Value, sizeof(Bits));
		return ((Bits >> 52) & 0x7FFu) != 0x7FFu;
	}
	inline bool IsFinite(const float Value)
	{
		uint32_t Bits = 0;
		memcpy(&Bits, &Value, sizeof(Bits));
		return ((Bits >> 23) & 0xFFu) != 0xFFu;
	}
	inline bool IsFinite(const PtVec3& V)
	{
		return IsFinite(V.X) && IsFinite(V.Y) && IsFinite(V.Z);
	}
	// Unit quaternion within 1e-6 (|q|^2 within 2e-6 of 1).
	inline bool IsUnitQuat(const PtQuat& Q)
	{
		if (!IsFinite(Q.X) || !IsFinite(Q.Y) || !IsFinite(Q.Z) || !IsFinite(Q.W))
		{
			return false;
		}
		const double SizeSquared = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		const double Error = SizeSquared - 1.0;
		return Error <= 2e-6 && Error >= -2e-6;
	}
	inline bool IsValidTransform(const PtTransform& T)
	{
		return IsUnitQuat(T.Rotation) && IsFinite(T.Translation);
	}

	// ---- strings and text output -------------------------------------------------------------------------------
	struct FStringRef
	{
		const char* Data = nullptr;
		uint32_t Length = 0;

		static FStringRef FromCString(const char* Text)
		{
			FStringRef Ref;
			Ref.Data = Text != nullptr ? Text : "";
			Ref.Length = static_cast<uint32_t>(strlen(Ref.Data));
			return Ref;
		}
		bool Equals(const char* Z) const
		{
			const size_t ZLength = Z != nullptr ? strlen(Z) : 0;
			return ZLength == Length && (Length == 0 || memcmp(Data, Z, Length) == 0);
		}
	};

	// Writes into a caller-owned PtTextBuffer. A writer without a buffer (or with Data == NULL) only measures. Once a
	// piece does not fit, nothing more is written (the written prefix stays a valid C string, empty when the first piece
	// already overflowed) but the needed size keeps counting, so Finish() can report it (two-call protocol).
	class FTextWriter
	{
	public:
		explicit FTextWriter(PtTextBuffer* InTarget)
			: Target(InTarget)
		{
			if (Target != nullptr && Target->Data != nullptr)
			{
				Data = Target->Data;
				Capacity = Target->Capacity;
				if (Capacity > 0)
				{
					Data[0] = '\0';   // a valid (empty) C string even if nothing fits
				}
			}
		}

		void Append(const char* Text, const uint32_t Length)
		{
			if (!bOverflow && Data != nullptr && Written + Length < Capacity)
			{
				memcpy(Data + Written, Text, Length);
				Written += Length;
				Data[Written] = '\0';
			}
			else
			{
				bOverflow = true;
			}
			Needed += Length;
		}
		void Append(const char* Text)
		{
			Append(Text, static_cast<uint32_t>(strlen(Text)));
		}
		// printf-style; one call writes at most 1023 bytes.
		void Appendf(const char* Format, ...)
		{
			char Local[1024];
			va_list Args;
			va_start(Args, Format);
			const int Count = vsnprintf(Local, sizeof(Local), Format, Args);
			va_end(Args);
			if (Count > 0)
			{
				Append(Local, Count < static_cast<int>(sizeof(Local)) ? static_cast<uint32_t>(Count) : static_cast<uint32_t>(sizeof(Local) - 1));
			}
		}
		// A JSON string literal (quotes included). Control characters, quotes and backslashes are escaped.
		void AppendJsonString(const char* Text)
		{
			Append("\"", 1);
			for (const char* Cursor = Text != nullptr ? Text : ""; *Cursor != '\0'; ++Cursor)
			{
				const unsigned char C = static_cast<unsigned char>(*Cursor);
				if (C == '"' || C == '\\')
				{
					const char Escaped[2] = { '\\', static_cast<char>(C) };
					Append(Escaped, 2);
				}
				else if (C < 0x20)
				{
					Appendf("\\u%04x", static_cast<unsigned>(C));
				}
				else
				{
					Append(Cursor, 1);
				}
			}
			Append("\"", 1);
		}
		// One effective-settings JSON line (02_Architecture 10, DL19):
		//   {"key":..,"native":..,"native_unit":..,"canonical":<number|null>,"unit":..,"source":..}\n
		// Source is READBACK, SET_ONLY, COMPILE_TIME or ENGINE_DEFAULT; a non-finite Canonical is written as null.
		void AppendEffective(const char* Key, const char* Native, const char* NativeUnit, const double Canonical, const char* Unit, const char* Source)
		{
			Append("{\"key\":");
			AppendJsonString(Key);
			Append(",\"native\":");
			AppendJsonString(Native);
			Append(",\"native_unit\":");
			AppendJsonString(NativeUnit);
			Append(",\"canonical\":");
			if (IsFinite(Canonical))
			{
				Appendf("%.17g", Canonical);
			}
			else
			{
				Append("null");
			}
			Append(",\"unit\":");
			AppendJsonString(Unit);
			Append(",\"source\":");
			AppendJsonString(Source);
			Append("}\n");
		}

		// Bytes needed, including the NUL.
		uint32_t GetNeeded() const { return Needed + 1; }
		bool HasOverflowed() const { return bOverflow || Data == nullptr; }
		// Target->Length = bytes needed (incl. NUL). PT_OK when everything fit, else PT_E_CAPACITY.
		PtResult Finish()
		{
			if (Target != nullptr)
			{
				Target->Length = GetNeeded();
			}
			if (Data == nullptr || bOverflow || Needed + 1 > Capacity)
			{
				return PT_E_CAPACITY;
			}
			Data[Needed] = '\0';
			return PT_OK;
		}

	private:
		PtTextBuffer* Target = nullptr;
		char* Data = nullptr;
		uint32_t Capacity = 0;
		uint32_t Written = 0;
		uint32_t Needed = 0;
		bool bOverflow = false;
	};

	// ---- strided input views -------------------------------------------------------------------------------------
	template <class T>
	struct TStridedView
	{
		const uint8_t* Base = nullptr;
		uint32_t Count = 0;
		uint32_t Stride = 0;

		const T& operator[](const uint32_t Index) const { return *reinterpret_cast<const T*>(Base + static_cast<size_t>(Index) * Stride); }
		uint32_t Num() const { return Count; }

		static TStridedView Make(const void* InBase, const uint32_t InCount, const uint32_t InStride)
		{
			TStridedView View;
			View.Base = static_cast<const uint8_t*>(InBase);
			View.Count = InCount;
			View.Stride = InStride;
			return View;
		}
		// Stride >= sizeof(T) (a newer host may append fields), aligned for T, Base aligned and non-NULL when Count > 0.
		static bool IsValid(const void* InBase, const uint32_t InCount, const uint32_t InStride)
		{
			if (InCount == 0)
			{
				return true;
			}
			return InBase != nullptr && InStride >= sizeof(T) && InStride % alignof(T) == 0
				&& reinterpret_cast<uintptr_t>(InBase) % alignof(T) == 0;
		}
	};

	struct FActorBatchView
	{
		TStridedView<PtActorCreateInfo> Actors;
		TStridedView<PtShapeElem> Shapes;
		const PtVec3* ConvexVertices = nullptr;
		uint32_t NumConvexVertices = 0;
	};

	// The UE arguments of FChaosScene::SetUpForFrame (ChaosScene.cpp:331-361 [V]) as the host passed them.
	struct FFrameParams
	{
		PtVec3 Gravity = {};
		bool bNewGravity = false;   // NewGrav was not NULL
		float DeltaSeconds = 0.0f;
		float MinDeltaTime = 0.0f;
		float MaxDeltaTime = 0.0f;
		float MaxSubstepDeltaTime = 0.0f;
		int32_t MaxSubsteps = 1;
		bool bSubstepping = false;
	};

	enum class EScenePhase : uint8_t
	{
		Idle,
		SetUp,
		Started,
		Waited,
	};

	enum class EModuleState : uint8_t
	{
		Loaded,
		Ready,
		Terminated,
	};

	inline const char* ToString(const EScenePhase Phase)
	{
		switch (Phase)
		{
		case EScenePhase::Idle: return "Idle";
		case EScenePhase::SetUp: return "SetUp";
		case EScenePhase::Started: return "Started";
		case EScenePhase::Waited: return "Waited";
		}
		return "?";
	}

	// ---- services the provider gives each backend (one instance per backend class) ----------------------------------
	class FKitServices
	{
	public:
		// Records the message for GetLastErrorMessage and returns Code (use: return GetKit().SetLastError(PT_E_..., ...)).
		PtResult SetLastError(const PtResult Code, const char* Format, ...)
		{
			va_list Args;
			va_start(Args, Format);
			const int Count = vsnprintf(LastError, sizeof(LastError), Format, Args);
			va_end(Args);
			LastErrorLength = Count < 0 ? 0u : (Count < static_cast<int>(sizeof(LastError)) ? static_cast<uint32_t>(Count) : static_cast<uint32_t>(sizeof(LastError) - 1));
			LastError[LastErrorLength] = '\0';
			LastErrorCode = Code;
			++LastErrorSerial;
			return Code;
		}
		// Formats a message and hands it to the host's log callback (if any). May be called from any thread.
		void LogToHost(const uint32_t Level, const char* Category, const char* Format, ...) const
		{
			const PtLogCallback Callback = LogCallback;
			if (Callback == nullptr)
			{
				return;
			}
			char Message[1024];
			va_list Args;
			va_start(Args, Format);
			vsnprintf(Message, sizeof(Message), Format, Args);
			va_end(Args);
			Callback(LogUserData, Level, Category != nullptr ? Category : "PtBackend", Message);
		}
		uint32_t GetOwnerThreadId() const { return LoadOwnerThreadId(); }
		const char* GetLastErrorText() const { return LastError; }
		PtResult GetLastErrorCode() const { return LastErrorCode; }
		uint32_t GetNumFpStateRestores() const { return NumFpStateRestores; }

	private:
		// The owner thread id is written on the owner thread (InitGamePhys, TermGamePhys) and read by the thread guard of
		// every thunk on any thread: relaxed atomic 32-bit accesses, the operations MSVC's std::atomic<uint32_t> uses for
		// a relaxed load / store (no <atomic> in the interface, no barrier on x64).
		uint32_t LoadOwnerThreadId() const { return static_cast<uint32_t>(__iso_volatile_load32(reinterpret_cast<const volatile int*>(&OwnerThreadId))); }
		void StoreOwnerThreadId(const uint32_t Id) { __iso_volatile_store32(reinterpret_cast<volatile int*>(&OwnerThreadId), static_cast<int>(Id)); }

		PtLogCallback LogCallback = nullptr;
		void* LogUserData = nullptr;
		uint32_t OwnerThreadId = 0;   // only through LoadOwnerThreadId / StoreOwnerThreadId
		uint32_t LastErrorLength = 0;
		uint32_t LastErrorSerial = 0;
		uint32_t NumFpStateRestores = 0;
		PtResult LastErrorCode = PT_OK;
		char LastError[1024] = {};
		template <class> friend struct TBackendApiProvider;
	};

	// ~ FChaosScene + FChaosEngineInterface. Every call arrives validated, on the owner thread, in the right phase.
	class IPhysicsBackendScene
	{
	public:
		IPhysicsBackendScene() = default;
		IPhysicsBackendScene(const IPhysicsBackendScene&) = delete;
		IPhysicsBackendScene& operator=(const IPhysicsBackendScene&) = delete;
		virtual ~IPhysicsBackendScene() { free(ReleasedWords); }
		virtual PtResult CreateMaterials(TStridedView<PtPhysicalMaterial> Materials) = 0;
		// Already validated by the kit. UE order: per body CreateActor -> geometry/filter/material -> ONE AddActorsToScene
		// -> per body InitDynamicProperties (UE order) -> putToSleep for !PT_BODY_START_AWAKE (skipped when sleeping is off).
		virtual PtResult CreateActors(const FActorBatchView& Batch) = 0;
		// Already validated by the kit: every index is below GetNumBodies(), live and listed once. IsReleased() reports
		// them released only after this returns success (a failure leaves the set unchanged).
		virtual PtResult ReleaseActors(const uint32_t* BodyIndices, uint32_t Num) = 0;
		// v1.1 (M9; PtBackendAbi.h, docs/Abi.md 9.4). Already validated by the kit: known type
		// whose capability the backend declares, Body1 a live body, Body2 a live other body or PT_INDEX_NONE, known flags,
		// Reserved 0, unit frames. Joint i of the call gets index GetNumJoints() + i (the kit adds Num on success). All or
		// nothing: a failure must leave no joint behind. A backend without joints keeps this default.
		virtual PtResult CreateConstraints(TStridedView<PtConstraintCreateInfo> /*Infos*/) { return PT_E_UNSUPPORTED; }
		virtual PtResult PrepareForSimulation(FTextWriter& /*OutActions*/) { return PT_OK; }
		virtual PtResult SetUpForFrame(const FFrameParams& Params) = 0;  // without NATIVE_FRAME_MODEL: MaxSubsteps == 1 && !bSubstepping
		virtual PtResult StartFrame() = 0;
		virtual PtResult WaitPhysScenes() = 0;
		virtual PtResult EndFrame(PtFrameResult& Out) = 0;
		// Backend-specific checks of one command (motion type, ...); runs for the whole batch before any command is
		// applied, so a rejected batch changes nothing. The kit has already rejected commands to released bodies.
		virtual PtResult ValidateBodyCommand(const PtBodyCommand& /*Cmd*/) { return PT_OK; }
		// Batch atomicity rule (docs/Abi.md 5.5): every check that can reject a command belongs in ValidateBodyCommand.
		// ApplyBodyCommand must not fail for a command that passed validation; if it does, the kit returns PT_E_INTERNAL
		// and reports how many commands of the batch were already applied (the batch is then NOT all-or-nothing).
		virtual PtResult ApplyBodyCommand(const PtBodyCommand& Cmd) = 0;
		// Two-call protocol: Capacity < count -> OutCount = count, return PT_E_CAPACITY, write nothing.
		// PT_READ_ALL is dense: GetNumBodies() states in index order, a released body as ObjectState UNINITIALIZED with a
		// zero pose (identity rotation) and zero velocities (the kit fails the call with PT_E_INTERNAL on another count).
		// After a successful PT_READ_ALL the kit writes OutIndices[i] = i itself when OutIndices is non-NULL (the ABI
		// contract, M3), so a backend may ignore OutIndices for PT_READ_ALL.
		// PT_READ_ACTIVE_ONLY: live bodies only, OutIndices is non-NULL whenever Capacity > 0 (checked by the kit); the
		// backend writes the index of every state.
		virtual PtResult ReadBodyStates(uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, uint32_t Capacity, uint32_t& OutCount) = 0;
		// The range holds live bodies only (checked by the kit).
		virtual PtResult ReadMassProperties(uint32_t First, uint32_t Num, PtMassProperties* Out) = 0;
		virtual PtResult GetStepStats(PtStepStats& Out) = 0;
		virtual PtResult CaptureDebugSnapshot(const PtDebugSnapshotRequest& /*Req*/, PtDebugSnapshotCounts& /*Out*/) { return PT_E_UNSUPPORTED; }   // v1.1 (docs/Abi.md 9.1, 9.2)
		virtual PtResult ReadDebugSnapshot(const PtDebugSnapshotBuffers& /*Buffers*/) { return PT_E_UNSUPPORTED; }                              // v1.1
		virtual PtResult ComputeNativeStateHash(uint64_t& /*OutHash*/) { return PT_E_UNSUPPORTED; }
		virtual PtResult SetOption(FStringRef /*Key*/, FStringRef /*Value*/) { return PT_E_UNSUPPORTED; }
		// Called twice per DumpEffectiveSettings (measure, then write): the output must be deterministic.
		virtual PtResult DumpEffectiveSettings(FTextWriter& Out) = 0;
		// Out arrives with LiveBytes / PeakBytes / NumAllocs / NumFrees = -1 (unknown): set only what the backend knows.
		virtual PtResult GetMemoryStats(PtMemoryStats& /*Out*/) { return PT_E_UNSUPPORTED; }

		EScenePhase GetPhase() const { return Phase; }              // maintained by the kit
		uint32_t GetNumBodies() const { return NumBodies; }         // bodies created so far (indices 0 .. N-1)
		uint32_t GetNumMaterials() const { return NumMaterials; }
		// Released by a successful ReleaseActors (maintained by the kit; indices are never reused).
		bool IsReleased(const uint32_t BodyIndex) const
		{
			return BodyIndex < NumBodies && ((ReleasedWords[BodyIndex >> 5u] >> (BodyIndex & 31u)) & 1u) != 0u;
		}
		uint32_t GetNumReleased() const { return NumReleased; }
		uint32_t GetNumJoints() const { return NumJoints; }         // joints created so far (indices 0 .. N-1, M9)
		FKitServices& GetKit() const { return *Kit; }              // bound by the provider after CreateScene

	private:
		// One bit per created body. Grown before CreateActors runs, so a body below NumBodies always has its word.
		bool ReserveReleasedBits(const uint32_t NewNumBodies)
		{
			const uint32_t Words = (NewNumBodies + 31u) >> 5u;
			if (Words <= NumReleasedWords)
			{
				return true;
			}
			uint32_t* Grown = static_cast<uint32_t*>(realloc(ReleasedWords, static_cast<size_t>(Words) * sizeof(uint32_t)));
			if (Grown == nullptr)
			{
				return false;
			}
			memset(Grown + NumReleasedWords, 0, static_cast<size_t>(Words - NumReleasedWords) * sizeof(uint32_t));
			ReleasedWords = Grown;
			NumReleasedWords = Words;
			return true;
		}
		void SetReleased(const uint32_t BodyIndex, const bool bReleased)
		{
			const uint32_t Mask = 1u << (BodyIndex & 31u);
			ReleasedWords[BodyIndex >> 5u] = bReleased ? (ReleasedWords[BodyIndex >> 5u] | Mask) : (ReleasedWords[BodyIndex >> 5u] & ~Mask);
		}

		FKitServices* Kit = nullptr;
		EScenePhase Phase = EScenePhase::Idle;
		uint32_t NumBodies = 0;
		uint32_t NumMaterials = 0;
		uint32_t* ReleasedWords = nullptr;
		uint32_t NumReleasedWords = 0;
		uint32_t NumReleased = 0;
		uint32_t NumJoints = 0;
		template <class> friend struct TBackendApiProvider;
	};

	// ~ FChaosSolversModule + InitGamePhys / TermGamePhys.
	class IPhysicsBackend
	{
	public:
		virtual ~IPhysicsBackend() = default;   // never runs: the provider constructs the backend in static storage
		virtual void GetInfo(PtBackendInfo& Out) const = 0;
		virtual PtResult InitGamePhys(const PtInitParams& Params) = 0;   // validates Params.Options (unknown -> PT_E_INVALID_ARG)
		virtual PtResult TermGamePhys() = 0;
		virtual PtResult CreateScene(const PtSceneDesc& Desc, IPhysicsBackendScene*& OutScene) = 0;   // validates Desc.Options
		virtual void DestroyScene(IPhysicsBackendScene* Scene) = 0;
		virtual PtResult SetModuleOption(FStringRef /*Key*/, FStringRef /*Value*/) { return PT_E_UNSUPPORTED; }
		virtual PtResult DumpModuleSettings(FTextWriter& /*Out*/) { return PT_OK; }
		// As the scene's: the -1 (unknown) fields stay -1 unless the backend sets them.
		virtual PtResult GetMemoryStats(PtMemoryStats& /*Out*/) { return PT_OK; }
		FKitServices& GetKit() const { return *Kit; }

	private:
		FKitServices* Kit = nullptr;
		template <class> friend struct TBackendApiProvider;
	};

	// ---- generic validation (shared by the provider and by backend unit tests) -------------------------------------
	// Each returns PT_OK or an error code and writes a reason into Error (a small fixed buffer).
	struct FValidationError
	{
		char Text[256] = {};
		PtResult Set(const PtResult Code, const char* Format, ...)
		{
			va_list Args;
			va_start(Args, Format);
			vsnprintf(Text, sizeof(Text), Format, Args);
			va_end(Args);
			return Code;
		}
	};

	inline PtResult ValidateSceneDesc(const PtSceneDesc& Desc, const uint64_t Capabilities, FValidationError& Error)
	{
		if (Desc.StructSize < sizeof(PtSceneDesc))
		{
			return Error.Set(PT_E_INVALID_ARG, "PtSceneDesc.StructSize %u is below %u", Desc.StructSize, static_cast<unsigned>(sizeof(PtSceneDesc)));
		}
		if (Desc.RunMode > PT_RUN_VIZ)
		{
			return Error.Set(PT_E_INVALID_ARG, "RunMode %u is unknown", Desc.RunMode);
		}
		if ((Desc.InstrumentFlags & ~PT_INSTR_ALL) != 0 || (Desc.SimFlags & ~PT_SIM_ALL) != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "unknown InstrumentFlags 0x%x / SimFlags 0x%x bits", Desc.InstrumentFlags, Desc.SimFlags);
		}
		if (!IsFinite(Desc.Gravity))
		{
			return Error.Set(PT_E_INVALID_ARG, "Gravity is not finite");
		}
		if (!IsFinite(Desc.NominalDeltaTime) || !(Desc.NominalDeltaTime > 0.0))
		{
			return Error.Set(PT_E_INVALID_ARG, "NominalDeltaTime must be finite and > 0");
		}
		// -1 = sync. 0 would switch Chaos to async results (AsyncDt >= 0, PhysicsSolverBase.h:712-715 [V]), so only an
		// exact -1 or a finite positive step is accepted (DL40).
		const double AsyncDt = Desc.AsyncFixedTimeStepSize;
		if (!IsFinite(AsyncDt) || !(AsyncDt == PT_ASYNC_DT_SYNC || AsyncDt > 0.0))
		{
			return Error.Set(PT_E_INVALID_ARG, "AsyncFixedTimeStepSize must be -1 (sync) or > 0; 0 enables async in Chaos");
		}
		if (AsyncDt > 0.0 && (Capabilities & PT_CAP_NATIVE_FRAME_MODEL) == 0)
		{
			return Error.Set(PT_E_UNSUPPORTED, "AsyncFixedTimeStepSize > 0 needs PT_CAP_NATIVE_FRAME_MODEL (the Harness driver plans async steps)");
		}
		if (!IsFinite(Desc.RestitutionThreshold) || !IsFinite(Desc.MaxLinearVelocity))
		{
			return Error.Set(PT_E_INVALID_ARG, "RestitutionThreshold / MaxLinearVelocity must be finite");
		}
		if (Desc.PositionIterations < 0 || Desc.VelocityIterations < 0 || Desc.ProjectionIterations < 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "negative iteration count");
		}
		if (Desc.NumThreads < 1)
		{
			return Error.Set(PT_E_INVALID_ARG, "NumThreads must be >= 1 (the caller counts)");
		}
		if (Desc.NumOptions > 0 && Desc.Options == nullptr)
		{
			return Error.Set(PT_E_INVALID_ARG, "NumOptions %u with Options == NULL", Desc.NumOptions);
		}
		// Reserved-field rule (v1.0, M7; docs/Abi.md 2): the host writes 0 into every Reserved field of an input struct, and
		// the Kit refuses anything else, so a later v1.x meaning of such a field can never be ignored silently - a host may
		// set it only for a module that declares the capability that gives it that meaning.
		if (Desc.Reserved0 != 0 || Desc.Reserved1 != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "PtSceneDesc.Reserved0 / Reserved1 must be 0 (0x%x / 0x%x)", Desc.Reserved0, Desc.Reserved1);
		}
		for (uint32_t Index = 0; Index < Desc.NumOptions; ++Index)
		{
			if (Desc.Options[Index].Key == nullptr || Desc.Options[Index].Value == nullptr)
			{
				return Error.Set(PT_E_INVALID_ARG, "Options[%u] has a NULL key or value", Index);
			}
		}
		return PT_OK;
	}

	inline PtResult ValidateMaterial(const PtPhysicalMaterial& M, const uint32_t Index, FValidationError& Error)
	{
		if (!IsFinite(M.Friction) || !IsFinite(M.StaticFriction) || !IsFinite(M.Restitution) || M.Friction < 0.0 || M.StaticFriction < 0.0
			|| M.Restitution < 0.0)
		{
			return Error.Set(PT_E_INVALID_ARG, "material %u: friction / static friction / restitution must be finite and >= 0", Index);
		}
		if (!IsFinite(M.SleepLinearVelocityThreshold) || !IsFinite(M.SleepAngularVelocityThreshold) || M.SleepLinearVelocityThreshold < 0.0
			|| M.SleepAngularVelocityThreshold < 0.0)
		{
			return Error.Set(PT_E_INVALID_ARG, "material %u: sleep thresholds must be finite and >= 0", Index);
		}
		if (M.FrictionCombineMode > PT_COMBINE_MAX || M.RestitutionCombineMode > PT_COMBINE_MAX)
		{
			return Error.Set(PT_E_INVALID_ARG, "material %u: combine mode %u / %u is unknown", Index, M.FrictionCombineMode, M.RestitutionCombineMode);
		}
		if (M.Reserved != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "material %u: Reserved must be 0 (0x%x)", Index, M.Reserved);
		}
		return PT_OK;
	}

	inline PtResult ValidateShape(const PtShapeElem& S, const uint32_t Index, const uint32_t NumMaterials, const uint32_t NumConvexVertices,
		const uint64_t Capabilities, FValidationError& Error)
	{
		if (!IsValidTransform(S.LocalTransform))
		{
			return Error.Set(PT_E_INVALID_ARG, "shape %u: LocalTransform is not finite or its rotation is not a unit quaternion", Index);
		}
		if (S.MaterialIndex >= NumMaterials)
		{
			return Error.Set(PT_E_INVALID_ARG, "shape %u: MaterialIndex %u is not below the %u materials created", Index, S.MaterialIndex, NumMaterials);
		}
		if (!IsFinite(S.ContactOffset) || !IsFinite(S.RestOffset) || (S.Flags & ~PT_SHAPEF_ALL) != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "shape %u: offsets must be finite, flags 0x%x unknown", Index, S.Flags);
		}
		if (S.Reserved != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "shape %u: Reserved must be 0 (0x%x)", Index, S.Reserved);
		}
		switch (S.Type)
		{
		case PT_SHAPE_SPHERE:
			if (!IsFinite(S.Radius) || !(S.Radius > 0.0))
			{
				return Error.Set(PT_E_INVALID_ARG, "shape %u: sphere radius must be finite and > 0", Index);
			}
			return PT_OK;
		case PT_SHAPE_BOX:
			if (!IsFinite(S.BoxHalfExtent) || !(S.BoxHalfExtent.X > 0.0) || !(S.BoxHalfExtent.Y > 0.0) || !(S.BoxHalfExtent.Z > 0.0))
			{
				return Error.Set(PT_E_INVALID_ARG, "shape %u: box half extents must be finite and > 0", Index);
			}
			return PT_OK;
		case PT_SHAPE_CAPSULE:
			if (!IsFinite(S.Radius) || !IsFinite(S.HalfLength) || !(S.Radius > 0.0) || S.HalfLength < 0.0)
			{
				return Error.Set(PT_E_INVALID_ARG, "shape %u: capsule radius must be > 0 and half length >= 0", Index);
			}
			return PT_OK;
		case PT_SHAPE_CONVEX:
			if ((Capabilities & PT_CAP_SHAPE_CONVEX) == 0)
			{
				return Error.Set(PT_E_UNSUPPORTED, "shape %u: convex shapes need PT_CAP_SHAPE_CONVEX", Index);
			}
			if (S.NumVertices < 4 || S.FirstVertex > NumConvexVertices || S.NumVertices > NumConvexVertices - S.FirstVertex)
			{
				return Error.Set(PT_E_INVALID_ARG, "shape %u: convex vertex range [%u, +%u) is outside the %u vertices", Index, S.FirstVertex, S.NumVertices,
					NumConvexVertices);
			}
			return PT_OK;
		default:
			return Error.Set(PT_E_INVALID_ARG, "shape %u: type %u is unknown or reserved", Index, S.Type);
		}
	}

	inline PtResult ValidateActor(const PtActorCreateInfo& A, const uint32_t Index, const uint32_t ExpectedBodyIndex, const uint32_t NumShapes,
		const uint64_t Capabilities, FValidationError& Error)
	{
		if (A.BodyIndex != ExpectedBodyIndex)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: BodyIndex %u, expected %u (creation order)", Index, A.BodyIndex, ExpectedBodyIndex);
		}
		if (A.Motion > PT_MOTION_DYNAMIC)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: Motion %u is unknown", Index, A.Motion);
		}
		if (A.Motion == PT_MOTION_KINEMATIC && (Capabilities & PT_CAP_KINEMATIC) == 0)
		{
			return Error.Set(PT_E_UNSUPPORTED, "actor %u: kinematic bodies need PT_CAP_KINEMATIC", Index);
		}
		if ((A.Flags & ~PT_BODY_FLAGS_ALL) != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: unknown flags 0x%x", Index, A.Flags);
		}
		if (A.NumShapes == 0 || A.FirstShape > NumShapes || A.NumShapes > NumShapes - A.FirstShape)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: shape range [%u, +%u) is empty or outside the %u shapes", Index, A.FirstShape, A.NumShapes, NumShapes);
		}
		if (A.ObjectType >= PT_NUM_COLLISION_CHANNELS)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: ObjectType %u is not below PT_NUM_COLLISION_CHANNELS", Index, A.ObjectType);
		}
		if (A.Reserved0 != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: Reserved0 must be 0 (0x%x)", Index, A.Reserved0);
		}
		if (!IsValidTransform(A.ActorTransform) || !IsValidTransform(A.CenterOfMassLocal))
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: ActorTransform / CenterOfMassLocal not finite or not a unit rotation", Index);
		}
		if (!IsFinite(A.InitialLinearVelocity) || !IsFinite(A.InitialAngularVelocity))
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: initial velocity is not finite", Index);
		}
		const bool bDynamic = A.Motion == PT_MOTION_DYNAMIC;
		if (!IsFinite(A.Mass) || !IsFinite(A.InertiaDiagonal) || A.Mass < 0.0 || A.InertiaDiagonal.X < 0.0 || A.InertiaDiagonal.Y < 0.0
			|| A.InertiaDiagonal.Z < 0.0)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: mass / inertia must be finite and >= 0", Index);
		}
		if (bDynamic && (!(A.Mass > 0.0) || !(A.InertiaDiagonal.X > 0.0) || !(A.InertiaDiagonal.Y > 0.0) || !(A.InertiaDiagonal.Z > 0.0)))
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: a dynamic body needs mass > 0 and inertia > 0 (got %g kg)", Index, A.Mass);
		}
		if (!IsFinite(A.LinearDamping) || !IsFinite(A.AngularDamping) || A.LinearDamping < 0.0 || A.AngularDamping < 0.0)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: damping must be finite and >= 0", Index);
		}
		if (!IsFinite(A.MaxAngularVelocity) || !IsFinite(A.MaxLinearVelocity) || !IsFinite(A.MaxDepenetrationVelocity)
			|| !IsFinite(A.SleepThresholdMultiplier) || !IsFinite(A.StabilizationThresholdMultiplier) || A.SleepThresholdMultiplier < 0.0
			|| A.StabilizationThresholdMultiplier < 0.0)
		{
			return Error.Set(PT_E_INVALID_ARG, "actor %u: velocity limits / threshold multipliers must be finite (multipliers >= 0)", Index);
		}
		return PT_OK;
	}

	inline PtResult ValidateBodyCommandGeneric(const PtBodyCommand& Cmd, const uint32_t Index, const uint32_t NumBodies, const uint64_t Capabilities,
		FValidationError& Error)
	{
		if (Cmd.Body >= NumBodies)
		{
			return Error.Set(PT_E_INVALID_ARG, "command %u: body %u is not below the %u bodies created", Index, Cmd.Body, NumBodies);
		}
		if ((Cmd.Flags & ~PT_CMDF_ALL) != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "command %u: unknown flags 0x%x", Index, Cmd.Flags);
		}
		if (Cmd.Reserved != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "command %u: Reserved must be 0 (0x%x)", Index, Cmd.Reserved);
		}
		switch (Cmd.Type)
		{
		case PT_CMD_SET_GLOBAL_POSE:
		case PT_CMD_SET_KINEMATIC_TARGET:
			if (!IsValidTransform(Cmd.Transform))
			{
				return Error.Set(PT_E_INVALID_ARG, "command %u: transform not finite or not a unit rotation", Index);
			}
			if (Cmd.Type == PT_CMD_SET_KINEMATIC_TARGET && (Capabilities & PT_CAP_KINEMATIC) == 0)
			{
				return Error.Set(PT_E_UNSUPPORTED, "command %u: kinematic targets need PT_CAP_KINEMATIC", Index);
			}
			return PT_OK;
		case PT_CMD_SET_BODY_STATE:
			if ((Capabilities & PT_CAP_STATE_WRITE) == 0)
			{
				return Error.Set(PT_E_UNSUPPORTED, "command %u: SET_BODY_STATE needs PT_CAP_STATE_WRITE", Index);
			}
			if (!IsValidTransform(Cmd.Transform) || !IsFinite(Cmd.Vector) || !IsFinite(Cmd.Vector2))
			{
				return Error.Set(PT_E_INVALID_ARG, "command %u: state not finite or rotation not unit", Index);
			}
			return PT_OK;
		case PT_CMD_SET_LINEAR_VELOCITY:
		case PT_CMD_SET_ANGULAR_VELOCITY:
		case PT_CMD_ADD_IMPULSE:
		case PT_CMD_ADD_ANGULAR_IMPULSE:
		case PT_CMD_ADD_FORCE:
		case PT_CMD_ADD_TORQUE:
		case PT_CMD_ADD_VELOCITY:
		case PT_CMD_ADD_ANGULAR_VELOCITY:
			if (!IsFinite(Cmd.Vector))
			{
				return Error.Set(PT_E_INVALID_ARG, "command %u: vector is not finite", Index);
			}
			return PT_OK;
		case PT_CMD_WAKE_UP:
		case PT_CMD_PUT_TO_SLEEP:
			return PT_OK;
		default:
			return Error.Set(PT_E_INVALID_ARG, "command %u: type %u is unknown", Index, Cmd.Type);
		}
	}

	// One joint of CreateConstraints (M9 semantics, docs/Abi.md 9.4): every argument check first (PT_E_INVALID_ARG), then the
	// joint type's capability (PT_E_UNSUPPORTED). Scene supplies the created bodies and the released set.
	inline PtResult ValidateConstraint(const PtConstraintCreateInfo& Info, const uint32_t Index, const IPhysicsBackendScene& Scene, const uint64_t Capabilities,
		FValidationError& Error)
	{
		const uint32_t NumBodies = Scene.GetNumBodies();
		if (Info.Type != PT_JOINT_BALL && Info.Type != PT_JOINT_HINGE)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: type %u is unknown or reserved (BALL %u, HINGE %u)", Index, Info.Type, PT_JOINT_BALL, PT_JOINT_HINGE);
		}
		if (Info.Body1 >= NumBodies)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: Body1 %u is not below the %u bodies created (the child is always a body)", Index, Info.Body1, NumBodies);
		}
		if (Info.Body2 != PT_INDEX_NONE && Info.Body2 >= NumBodies)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: Body2 %u is neither PT_INDEX_NONE (the world) nor below the %u bodies created", Index, Info.Body2, NumBodies);
		}
		if (Info.Body1 == Info.Body2)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: Body1 and Body2 are the same body %u", Index, Info.Body1);
		}
		if (Scene.IsReleased(Info.Body1) || (Info.Body2 != PT_INDEX_NONE && Scene.IsReleased(Info.Body2)))
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: body %u is released", Index, Scene.IsReleased(Info.Body1) ? Info.Body1 : Info.Body2);
		}
		if ((Info.Flags & ~PT_JOINTF_ALL) != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: unknown flags 0x%x", Index, Info.Flags);
		}
		uint64_t Reserved[2] = {};
		memcpy(&Reserved[0], &Info.Reserved0, sizeof(uint64_t));
		memcpy(&Reserved[1], &Info.Reserved1, sizeof(uint64_t));
		if (Reserved[0] != 0 || Reserved[1] != 0)
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: Reserved0 / Reserved1 must be 0 (bits 0x%llx / 0x%llx)", Index,
				static_cast<unsigned long long>(Reserved[0]), static_cast<unsigned long long>(Reserved[1]));
		}
		if (!IsValidTransform(Info.Frame1) || !IsValidTransform(Info.Frame2))
		{
			return Error.Set(PT_E_INVALID_ARG, "constraint %u: a frame is not finite or its rotation is not unit", Index);
		}
		const uint64_t Needed = Info.Type == PT_JOINT_BALL ? PT_CAP_JOINT_BALL : PT_CAP_JOINT_HINGE;
		if ((Capabilities & Needed) == 0)
		{
			return Error.Set(PT_E_UNSUPPORTED, "constraint %u: %s joints need %s", Index, Info.Type == PT_JOINT_BALL ? "ball" : "hinge",
				Info.Type == PT_JOINT_BALL ? "PT_CAP_JOINT_BALL" : "PT_CAP_JOINT_HINGE");
		}
		return PT_OK;
	}

	inline PtResult ValidateFrameParams(const FFrameParams& P, const uint64_t Capabilities, FValidationError& Error)
	{
		if (P.bNewGravity && !IsFinite(P.Gravity))
		{
			return Error.Set(PT_E_INVALID_ARG, "SetUpForFrame: NewGrav is not finite");
		}
		if (!IsFinite(P.DeltaSeconds) || !IsFinite(P.MinDeltaTime) || !IsFinite(P.MaxDeltaTime) || !IsFinite(P.MaxSubstepDeltaTime)
			|| P.DeltaSeconds < 0.0f || P.MinDeltaTime < 0.0f)
		{
			return Error.Set(PT_E_INVALID_ARG, "SetUpForFrame: times must be finite and DeltaSeconds / MinDeltaTime >= 0");
		}
		if (P.MaxSubsteps < 1)
		{
			return Error.Set(PT_E_INVALID_ARG, "SetUpForFrame: MaxSubsteps %d must be >= 1", P.MaxSubsteps);
		}
		if ((Capabilities & PT_CAP_NATIVE_FRAME_MODEL) == 0 && (P.MaxSubsteps != 1 || P.bSubstepping))
		{
			return Error.Set(PT_E_UNSUPPORTED, "SetUpForFrame: substepping needs PT_CAP_NATIVE_FRAME_MODEL (Harness driver: one step per call)");
		}
		return PT_OK;
	}

	// ---- the provider: C thunks and all Kit state for one backend class --------------------------------------------
	// TBackend: a default-constructible IPhysicsBackend with `static constexpr const char* ModuleName`.
	template <class TBackend>
	struct TBackendApiProvider
	{
		// NULL unless RequestedMajor == PT_ABI_VERSION_MAJOR.
		static const PtBackendApi* Get(const uint32_t RequestedMajor)
		{
			if (RequestedMajor != PT_ABI_VERSION_MAJOR)
			{
				return nullptr;
			}
			EnsureBackend();
			return &Api;
		}

	private:
		static inline FKitServices Kit{};
		static inline IPhysicsBackendScene* Slots[PT_KIT_MAX_SCENES] = {};
		static inline uint32_t Generations[PT_KIT_MAX_SCENES] = {};
		static inline EModuleState ModuleState = EModuleState::Loaded;
		static inline uint64_t Capabilities = 0;
		static inline uint32_t ModuleFlags = 0;
		static inline TBackend* Backend = nullptr;
		alignas(TBackend) static inline unsigned char BackendStorage[sizeof(TBackend)] = {};

		static void EnsureBackend()
		{
			if (Backend == nullptr)
			{
				// Static storage, never destroyed: no backend destructor runs during CRT / DLL detach (UE modules).
				TBackend* Instance = ::new (static_cast<void*>(BackendStorage)) TBackend();
				static_cast<IPhysicsBackend*>(Instance)->Kit = &Kit;
				PtBackendInfo Info = {};
				Info.StructSize = sizeof(PtBackendInfo);
				Instance->GetInfo(Info);
				Capabilities = Info.Capabilities;
				ModuleFlags = Info.ModuleFlags;
				Backend = Instance;
			}
		}

		// Thread guard, exception barrier, MXCSR guard and "every failure has a message". bKeepLastError: the call must
		// not overwrite the last error (GetLastErrorMessage itself).
		template <class FBody>
		static PtResult Guarded(const char* Entry, FBody&& Body, const bool bKeepLastError = false)
		{
			const uint32_t Owner = Kit.LoadOwnerThreadId();
			if (Owner != 0 && Owner != PT_KIT_CURRENT_THREAD_ID())
			{
				return PT_E_WRONG_THREAD;   // no side effect: not even the last error changes
			}
			const unsigned int Csr = _mm_getcsr();
			const uint32_t Serial = Kit.LastErrorSerial;
			PtResult Result = PT_E_INTERNAL;
#if PT_KIT_CATCH_EXCEPTIONS
			try
			{
				Result = Body();
			}
			catch (...)
			{
				Result = Kit.SetLastError(PT_E_INTERNAL, "%s: a C++ exception escaped the backend", Entry);
			}
#else
			Result = Body();
#endif
			const unsigned int ControlMask = 0xFFC0u;   // DAZ, exception masks, rounding control, FTZ (status bits 0-5 are not state)
			const unsigned int CsrAfter = _mm_getcsr();
			if ((CsrAfter & ControlMask) != (Csr & ControlMask))
			{
				_mm_setcsr((Csr & ControlMask) | (CsrAfter & ~ControlMask));
				if (Kit.NumFpStateRestores++ == 0)
				{
					Kit.LogToHost(PT_LOG_WARNING, "PtKit", "%s: the backend changed MXCSR 0x%04x -> 0x%04x; the Kit restored it", Entry, Csr & ControlMask,
						CsrAfter & ControlMask);
				}
			}
			if (Result < 0 && !bKeepLastError && Kit.LastErrorSerial == Serial)
			{
				Kit.SetLastError(Result, "%s failed with %d (the backend gave no message)", Entry, Result);
			}
			return Result;
		}

		static PtResult RequireReady(const char* Entry)
		{
			if (ModuleState != EModuleState::Ready)
			{
				return Kit.SetLastError(PT_E_BAD_STATE, "%s: the module is not initialised (InitGamePhys first)", Entry);
			}
			return PT_OK;
		}

		static PtSceneId MakeSceneId(const uint32_t Slot)
		{
			return (static_cast<uint64_t>(Generations[Slot] + 1u) << 32) | Slot;
		}

		static PtResult FindScene(const PtSceneId Id, const char* Entry, IPhysicsBackendScene*& OutScene, uint32_t* OutSlot = nullptr)
		{
			OutScene = nullptr;
			const uint32_t Slot = static_cast<uint32_t>(Id & 0xFFFFFFFFu);
			const uint32_t Generation = static_cast<uint32_t>(Id >> 32);
			if (Id == 0 || Slot >= PT_KIT_MAX_SCENES || Slots[Slot] == nullptr || Generation != Generations[Slot] + 1u)
			{
				return Kit.SetLastError(PT_E_INVALID_HANDLE, "%s: scene id 0x%llx is not a live scene", Entry, static_cast<unsigned long long>(Id));
			}
			OutScene = Slots[Slot];
			if (OutSlot != nullptr)
			{
				*OutSlot = Slot;
			}
			return PT_OK;
		}

		// Ready module + live scene in the expected phase.
		static PtResult EnterScene(const PtSceneId Id, const char* Entry, const EScenePhase Expected, IPhysicsBackendScene*& OutScene)
		{
			PtResult Result = RequireReady(Entry);
			if (Result != PT_OK)
			{
				return Result;
			}
			Result = FindScene(Id, Entry, OutScene);
			if (Result != PT_OK)
			{
				return Result;
			}
			if (OutScene->Phase != Expected)
			{
				return Kit.SetLastError(PT_E_BAD_STATE, "%s: the scene is in phase %s, expected %s", Entry, ToString(OutScene->Phase), ToString(Expected));
			}
			return PT_OK;
		}

		// Module-level effective settings: the backend's lines, then the Kit's own. The restore count changes only after a
		// thunk returns, so both passes of one DumpEffectiveSettings write the same text.
		static PtResult DumpModuleLevel(FTextWriter& Out)
		{
			const PtResult Result = Backend->DumpModuleSettings(Out);
			if (Result < 0)
			{
				return Result;
			}
			char Text[32];
			snprintf(Text, sizeof(Text), "%u", Kit.NumFpStateRestores);
			// The Kit's own count: SET_ONLY, as every adapter counter (docs/Abi.md 5.14).
			Out.AppendEffective("kit.mxcsr_restores", Text, "calls", static_cast<double>(Kit.NumFpStateRestores), "calls", "SET_ONLY");
			return Result;
		}

		static PtResult Fail(const PtResult Code, const char* Entry, const FValidationError& Error)
		{
			return Kit.SetLastError(Code, "%s: %s", Entry, Error.Text);
		}

		static PtResult ValidateTextBuffer(const PtTextBuffer* Buffer, const char* Entry, const bool bRequired)
		{
			if (Buffer == nullptr)
			{
				return bRequired ? Kit.SetLastError(PT_E_INVALID_ARG, "%s: the text buffer is NULL", Entry) : PT_OK;
			}
			if (Buffer->Data == nullptr && Buffer->Capacity != 0)
			{
				return Kit.SetLastError(PT_E_INVALID_ARG, "%s: Data is NULL but Capacity is %u", Entry, Buffer->Capacity);
			}
			return PT_OK;
		}

		// ---- thunks ----
		static PtResult PT_CALL GetInfo(PtBackendInfo* Out)
		{
			return Guarded("GetInfo", [&]() -> PtResult
			{
				if (Out == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "GetInfo: Out is NULL");
				}
				if (Out->StructSize < sizeof(PtBackendInfo))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "GetInfo: StructSize %u is below %u", Out->StructSize, static_cast<unsigned>(sizeof(PtBackendInfo)));
				}
				memset(Out, 0, sizeof(PtBackendInfo));
				Out->StructSize = sizeof(PtBackendInfo);
				Backend->GetInfo(*Out);
				Out->StructSize = sizeof(PtBackendInfo);
				Out->Capabilities = Capabilities;
				Out->ModuleFlags = ModuleFlags;
				Out->ProvisionalRevision = PT_ABI_PROVISIONAL_REVISION;
				return PT_OK;
			});
		}

		static PtResult PT_CALL InitGamePhys(const PtInitParams* Params)
		{
			return Guarded("InitGamePhys", [&]() -> PtResult
			{
				if (ModuleState == EModuleState::Ready)
				{
					return Kit.SetLastError(PT_E_BAD_STATE, "InitGamePhys: already initialised");
				}
				if (ModuleState == EModuleState::Terminated && (ModuleFlags & PT_MODULE_NO_REINIT) != 0)
				{
					return Kit.SetLastError(PT_E_BAD_STATE, "InitGamePhys: the module was terminated and declares PT_MODULE_NO_REINIT");
				}
				if (Params == nullptr || Params->StructSize < sizeof(PtInitParams))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "InitGamePhys: Params is NULL or its StructSize is below %u", static_cast<unsigned>(sizeof(PtInitParams)));
				}
				if (Params->MaxThreads < 1)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "InitGamePhys: MaxThreads must be >= 1");
				}
				if (Params->NumOptions > 0 && Params->Options == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "InitGamePhys: NumOptions %u with Options == NULL", Params->NumOptions);
				}
				if (Params->Reserved != 0)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "InitGamePhys: PtInitParams.Reserved must be 0 (0x%x)", Params->Reserved);
				}
				for (uint32_t Index = 0; Index < Params->NumOptions; ++Index)
				{
					if (Params->Options[Index].Key == nullptr || Params->Options[Index].Value == nullptr)
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "InitGamePhys: Options[%u] has a NULL key or value", Index);
					}
				}
				Kit.StoreOwnerThreadId(PT_KIT_CURRENT_THREAD_ID());
				Kit.LogCallback = Params->LogCallback;
				Kit.LogUserData = Params->LogUserData;
				const PtResult Result = Backend->InitGamePhys(*Params);
				if (Result < 0)
				{
					Kit.StoreOwnerThreadId(0);
					Kit.LogCallback = nullptr;
					Kit.LogUserData = nullptr;
					return Result;
				}
				ModuleState = EModuleState::Ready;
				return Result;
			});
		}

		static PtResult PT_CALL TermGamePhys()
		{
			return Guarded("TermGamePhys", [&]() -> PtResult
			{
				const PtResult Ready = RequireReady("TermGamePhys");
				if (Ready != PT_OK)
				{
					return Ready;
				}
				// Scenes still alive are destroyed first (in slot order), whatever their phase.
				for (uint32_t Slot = 0; Slot < PT_KIT_MAX_SCENES; ++Slot)
				{
					if (Slots[Slot] != nullptr)
					{
						Backend->DestroyScene(Slots[Slot]);
						Slots[Slot] = nullptr;
						++Generations[Slot];
					}
				}
				const PtResult Result = Backend->TermGamePhys();
				ModuleState = EModuleState::Terminated;
				Kit.StoreOwnerThreadId(0);
				Kit.LogCallback = nullptr;
				Kit.LogUserData = nullptr;
				return Result;
			});
		}

		static PtResult PT_CALL CreateScene(const PtSceneDesc* Desc, PtSceneId* OutScene)
		{
			return Guarded("CreateScene", [&]() -> PtResult
			{
				const PtResult Ready = RequireReady("CreateScene");
				if (Ready != PT_OK)
				{
					return Ready;
				}
				if (Desc == nullptr || OutScene == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CreateScene: Desc or OutScene is NULL");
				}
				*OutScene = 0;
				FValidationError Error;
				const PtResult Valid = ValidateSceneDesc(*Desc, Capabilities, Error);
				if (Valid != PT_OK)
				{
					return Fail(Valid, "CreateScene", Error);
				}
				uint32_t FreeSlot = PT_KIT_MAX_SCENES;
				for (uint32_t Slot = 0; Slot < PT_KIT_MAX_SCENES; ++Slot)
				{
					if (Slots[Slot] == nullptr)
					{
						FreeSlot = Slot;
						break;
					}
				}
				if (FreeSlot == PT_KIT_MAX_SCENES)
				{
					return Kit.SetLastError(PT_E_CAPACITY, "CreateScene: all %d scene slots are in use", PT_KIT_MAX_SCENES);
				}
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Result = Backend->CreateScene(*Desc, Scene);
				if (Result < 0)
				{
					return Result;
				}
				if (Scene == nullptr)
				{
					return Kit.SetLastError(PT_E_INTERNAL, "CreateScene: the backend returned no scene");
				}
				Scene->Kit = &Kit;
				Scene->Phase = EScenePhase::Idle;
				Scene->NumBodies = 0;
				Scene->NumMaterials = 0;
				Slots[FreeSlot] = Scene;
				*OutScene = MakeSceneId(FreeSlot);
				return Result;
			});
		}

		static PtResult PT_CALL DestroyScene(const PtSceneId Id)
		{
			return Guarded("DestroyScene", [&]() -> PtResult
			{
				const PtResult Ready = RequireReady("DestroyScene");
				if (Ready != PT_OK)
				{
					return Ready;
				}
				IPhysicsBackendScene* Scene = nullptr;
				uint32_t Slot = 0;
				const PtResult Found = FindScene(Id, "DestroyScene", Scene, &Slot);
				if (Found != PT_OK)
				{
					return Found;
				}
				if (Scene->Phase != EScenePhase::Idle)
				{
					return Kit.SetLastError(PT_E_BAD_STATE, "DestroyScene: the scene is in phase %s", ToString(Scene->Phase));
				}
				Backend->DestroyScene(Scene);
				Slots[Slot] = nullptr;
				++Generations[Slot];
				return PT_OK;
			});
		}

		static PtResult PT_CALL CreateMaterials(const PtSceneId Id, const PtPhysicalMaterial* Materials, const uint32_t Num, const uint32_t Stride)
		{
			return Guarded("CreateMaterials", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "CreateMaterials", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (!TStridedView<PtPhysicalMaterial>::IsValid(Materials, Num, Stride))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CreateMaterials: invalid array (pointer %p, %u entries, stride %u; stride must be >= %u and a multiple of %u)",
						static_cast<const void*>(Materials), Num, Stride, static_cast<unsigned>(sizeof(PtPhysicalMaterial)), static_cast<unsigned>(alignof(PtPhysicalMaterial)));
				}
				const TStridedView<PtPhysicalMaterial> View = TStridedView<PtPhysicalMaterial>::Make(Materials, Num, Stride);
				FValidationError Error;
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					const PtResult Valid = ValidateMaterial(View[Index], Index, Error);
					if (Valid != PT_OK)
					{
						return Fail(Valid, "CreateMaterials", Error);
					}
				}
				const PtResult Result = Scene->CreateMaterials(View);
				if (Result >= 0)
				{
					Scene->NumMaterials += Num;
				}
				return Result;
			});
		}

		static PtResult PT_CALL CreateActors(const PtSceneId Id, const PtActorBatch* Batch)
		{
			return Guarded("CreateActors", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "CreateActors", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (Batch == nullptr || Batch->StructSize < sizeof(PtActorBatch))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CreateActors: Batch is NULL or its StructSize is below %u", static_cast<unsigned>(sizeof(PtActorBatch)));
				}
				if (!TStridedView<PtActorCreateInfo>::IsValid(Batch->Actors, Batch->NumActors, Batch->ActorStride)
					|| !TStridedView<PtShapeElem>::IsValid(Batch->Shapes, Batch->NumShapes, Batch->ShapeStride)
					|| (Batch->NumConvexVertices > 0 && Batch->ConvexVertices == nullptr))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CreateActors: invalid array pointer or stride (actors stride %u, shapes stride %u)", Batch->ActorStride,
						Batch->ShapeStride);
				}
				FActorBatchView View;
				View.Actors = TStridedView<PtActorCreateInfo>::Make(Batch->Actors, Batch->NumActors, Batch->ActorStride);
				View.Shapes = TStridedView<PtShapeElem>::Make(Batch->Shapes, Batch->NumShapes, Batch->ShapeStride);
				View.ConvexVertices = Batch->ConvexVertices;
				View.NumConvexVertices = Batch->NumConvexVertices;
				FValidationError Error;
				for (uint32_t Index = 0; Index < View.Shapes.Num(); ++Index)
				{
					const PtResult Valid = ValidateShape(View.Shapes[Index], Index, Scene->NumMaterials, View.NumConvexVertices, Capabilities, Error);
					if (Valid != PT_OK)
					{
						return Fail(Valid, "CreateActors", Error);
					}
				}
				for (uint32_t Index = 0; Index < View.NumConvexVertices; ++Index)
				{
					if (!IsFinite(View.ConvexVertices[Index]))
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "CreateActors: convex vertex %u is not finite", Index);
					}
				}
				for (uint32_t Index = 0; Index < View.Actors.Num(); ++Index)
				{
					const PtResult Valid = ValidateActor(View.Actors[Index], Index, Scene->NumBodies + Index, View.Shapes.Num(), Capabilities, Error);
					if (Valid != PT_OK)
					{
						return Fail(Valid, "CreateActors", Error);
					}
				}
				if (Batch->NumActors > 0xFFFFFFFFu - Scene->NumBodies || !Scene->ReserveReleasedBits(Scene->NumBodies + Batch->NumActors))
				{
					return Kit.SetLastError(PT_E_OUT_OF_MEMORY, "CreateActors: cannot track %u more bodies", Batch->NumActors);
				}
				const PtResult Result = Scene->CreateActors(View);
				if (Result >= 0)
				{
					Scene->NumBodies += Batch->NumActors;
				}
				return Result;
			});
		}

		static PtResult PT_CALL ReleaseActors(const PtSceneId Id, const uint32_t* BodyIndices, const uint32_t Num)
		{
			return Guarded("ReleaseActors", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ReleaseActors", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (Num > 0 && BodyIndices == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReleaseActors: BodyIndices is NULL");
				}
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					if (BodyIndices[Index] >= Scene->NumBodies)
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "ReleaseActors: body %u is not below the %u bodies created", BodyIndices[Index], Scene->NumBodies);
					}
					if (Scene->IsReleased(BodyIndices[Index]))
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "ReleaseActors: body %u is already released", BodyIndices[Index]);
					}
				}
				// Duplicates within the call: mark as we go, undo the marks on the first repeat (all or nothing).
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					if (Scene->IsReleased(BodyIndices[Index]))
					{
						for (uint32_t Undo = 0; Undo < Index; ++Undo)
						{
							Scene->SetReleased(BodyIndices[Undo], false);
						}
						return Kit.SetLastError(PT_E_INVALID_ARG, "ReleaseActors: body %u is listed twice", BodyIndices[Index]);
					}
					Scene->SetReleased(BodyIndices[Index], true);
				}
				// The backend sees the indices as live while it releases them.
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					Scene->SetReleased(BodyIndices[Index], false);
				}
				const PtResult Result = Scene->ReleaseActors(BodyIndices, Num);
				if (Result >= 0)
				{
					for (uint32_t Index = 0; Index < Num; ++Index)
					{
						Scene->SetReleased(BodyIndices[Index], true);
					}
					Scene->NumReleased += Num;
				}
				return Result;
			});
		}

		static PtResult PT_CALL CreateConstraints(const PtSceneId Id, const PtConstraintCreateInfo* Infos, const uint32_t Num, const uint32_t Stride)
		{
			return Guarded("CreateConstraints", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "CreateConstraints", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (!TStridedView<PtConstraintCreateInfo>::IsValid(Infos, Num, Stride))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CreateConstraints: invalid array pointer or stride %u", Stride);
				}
				const TStridedView<PtConstraintCreateInfo> View = TStridedView<PtConstraintCreateInfo>::Make(Infos, Num, Stride);
				// M9 (docs/Abi.md 9.4): argument errors for every joint first, then capabilities, so a module without joints
				// still reports a malformed call as PT_E_INVALID_ARG (A4).
				FValidationError Error;
				PtResult Unsupported = PT_OK;
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					const PtResult Valid = ValidateConstraint(View[Index], Index, *Scene, Capabilities, Error);
					if (Valid == PT_E_UNSUPPORTED)
					{
						Unsupported = Unsupported == PT_OK ? Valid : Unsupported;
						continue;
					}
					if (Valid != PT_OK)
					{
						return Fail(Valid, "CreateConstraints", Error);
					}
				}
				if (Unsupported == PT_OK && (Capabilities & (PT_CAP_JOINT_BALL | PT_CAP_JOINT_HINGE)) == 0)
				{
					return Kit.SetLastError(PT_E_UNSUPPORTED, "CreateConstraints: the backend declares no joint capability (PT_CAP_JOINT_BALL / PT_CAP_JOINT_HINGE)");
				}
				if (Unsupported != PT_OK)
				{
					// The first unsupported joint's reason (argument errors of later joints were checked above).
					for (uint32_t Index = 0; Index < Num; ++Index)
					{
						if (ValidateConstraint(View[Index], Index, *Scene, Capabilities, Error) == PT_E_UNSUPPORTED)
						{
							return Fail(PT_E_UNSUPPORTED, "CreateConstraints", Error);
						}
					}
				}
				const PtResult Result = Scene->CreateConstraints(View);
				if (Result >= 0)
				{
					Scene->NumJoints += Num;
				}
				return Result;
			});
		}

		static PtResult PT_CALL PrepareForSimulation(const PtSceneId Id, PtTextBuffer* OutActions)
		{
			return Guarded("PrepareForSimulation", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "PrepareForSimulation", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				const PtResult BufferOk = ValidateTextBuffer(OutActions, "PrepareForSimulation", false);
				if (BufferOk != PT_OK)
				{
					return BufferOk;
				}
				// One pass (preparation is not repeatable): text that does not fit is truncated.
				FTextWriter Writer(OutActions);
				const PtResult Result = Scene->PrepareForSimulation(Writer);
				if (Result < 0)
				{
					return Result;
				}
				if (OutActions != nullptr && Writer.Finish() != PT_OK)
				{
					return PT_W_TRUNCATED;
				}
				return Result;
			});
		}

		static PtResult PT_CALL SetUpForFrame(const PtSceneId Id, const PtVec3* NewGrav, const float DeltaSeconds, const float MinDeltaTime,
			const float MaxDeltaTime, const float MaxSubstepDeltaTime, const int32_t MaxSubsteps, const uint32_t bSubstepping)
		{
			return Guarded("SetUpForFrame", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "SetUpForFrame", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (bSubstepping > 1u)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "SetUpForFrame: bSubstepping must be 0 or 1");
				}
				FFrameParams Params;
				Params.bNewGravity = NewGrav != nullptr;
				if (NewGrav != nullptr)
				{
					Params.Gravity = *NewGrav;
				}
				Params.DeltaSeconds = DeltaSeconds;
				Params.MinDeltaTime = MinDeltaTime;
				Params.MaxDeltaTime = MaxDeltaTime;
				Params.MaxSubstepDeltaTime = MaxSubstepDeltaTime;
				Params.MaxSubsteps = MaxSubsteps;
				Params.bSubstepping = bSubstepping != 0u;
				FValidationError Error;
				const PtResult Valid = ValidateFrameParams(Params, Capabilities, Error);
				if (Valid != PT_OK)
				{
					return Fail(Valid, "SetUpForFrame", Error);
				}
				const PtResult Result = Scene->SetUpForFrame(Params);
				if (Result >= 0)
				{
					Scene->Phase = EScenePhase::SetUp;
				}
				return Result;
			});
		}

		static PtResult PT_CALL StartFrame(const PtSceneId Id)
		{
			return Guarded("StartFrame", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "StartFrame", EScenePhase::SetUp, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				const PtResult Result = Scene->StartFrame();
				if (Result >= 0)
				{
					Scene->Phase = EScenePhase::Started;
				}
				return Result;
			});
		}

		static PtResult PT_CALL WaitPhysScenes(const PtSceneId Id)
		{
			return Guarded("WaitPhysScenes", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "WaitPhysScenes", EScenePhase::Started, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				const PtResult Result = Scene->WaitPhysScenes();
				if (Result >= 0)
				{
					Scene->Phase = EScenePhase::Waited;
				}
				return Result;
			});
		}

		static PtResult PT_CALL EndFrame(const PtSceneId Id, PtFrameResult* OutResult)
		{
			return Guarded("EndFrame", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "EndFrame", EScenePhase::Waited, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (OutResult == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "EndFrame: OutResult is NULL");
				}
				PtFrameResult Frame = {};
				Frame.NumPushedSteps = PT_INDEX_NONE;   // v1.0: not reported unless the backend fills it (a NATIVE_FRAME_MODEL backend)
				const PtResult Result = Scene->EndFrame(Frame);
				if (Result >= 0)
				{
					*OutResult = Frame;
					Scene->Phase = EScenePhase::Idle;
				}
				return Result;
			});
		}

		static PtResult PT_CALL ApplyBodyCommands(const PtSceneId Id, const PtBodyCommand* Cmds, const uint32_t Num, const uint32_t Stride)
		{
			return Guarded("ApplyBodyCommands", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ApplyBodyCommands", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (!TStridedView<PtBodyCommand>::IsValid(Cmds, Num, Stride))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ApplyBodyCommands: invalid array pointer or stride %u", Stride);
				}
				const TStridedView<PtBodyCommand> View = TStridedView<PtBodyCommand>::Make(Cmds, Num, Stride);
				FValidationError Error;
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					const PtResult Valid = ValidateBodyCommandGeneric(View[Index], Index, Scene->NumBodies, Capabilities, Error);
					if (Valid != PT_OK)
					{
						return Fail(Valid, "ApplyBodyCommands", Error);
					}
					if (Scene->IsReleased(View[Index].Body))
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "ApplyBodyCommands: command %u (type %u) targets released body %u", Index, View[Index].Type,
							View[Index].Body);
					}
					const PtResult BackendValid = Scene->ValidateBodyCommand(View[Index]);
					if (BackendValid < 0)
					{
						return BackendValid;
					}
				}
				for (uint32_t Index = 0; Index < Num; ++Index)
				{
					const PtResult Result = Scene->ApplyBodyCommand(View[Index]);
					if (Result < 0)
					{
						// The backend broke the batch atomicity rule (IPhysicsBackendScene::ApplyBodyCommand): a validated command
						// failed. Commands [0, Index) are already applied.
						return Kit.SetLastError(PT_E_INTERNAL, "ApplyBodyCommands: command %u (type %u, body %u) failed with %d after validation; the "
							"first %u commands of the batch were applied (backend bug: checks belong in ValidateBodyCommand)", Index, View[Index].Type,
							View[Index].Body, Result, Index);
					}
				}
				return PT_OK;
			});
		}

		static PtResult PT_CALL ReadBodyStates(const PtSceneId Id, const uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* OutStates,
			const uint32_t Capacity, uint32_t* OutCount)
		{
			return Guarded("ReadBodyStates", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ReadBodyStates", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (ReadFlags != PT_READ_ALL && ReadFlags != PT_READ_ACTIVE_ONLY)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadBodyStates: ReadFlags %u is unknown", ReadFlags);
				}
				if (OutCount == nullptr || (Capacity > 0 && OutStates == nullptr))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadBodyStates: OutCount is NULL, or Capacity %u with OutStates == NULL", Capacity);
				}
				if (ReadFlags == PT_READ_ACTIVE_ONLY && Capacity > 0 && OutIndices == nullptr)
				{
					// The states of a partial read cannot be matched to bodies without their indices.
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadBodyStates: PT_READ_ACTIVE_ONLY with Capacity %u needs OutIndices", Capacity);
				}
				uint32_t Count = 0;
				const PtResult Result = Scene->ReadBodyStates(ReadFlags, OutIndices, OutStates, Capacity, Count);
				if ((Result >= 0 || Result == PT_E_CAPACITY) && ReadFlags == PT_READ_ALL && Count != Scene->NumBodies)
				{
					return Kit.SetLastError(PT_E_INTERNAL, "ReadBodyStates: PT_READ_ALL must report all %u created bodies (released ones as UNINITIALIZED), "
						"the backend reported %u", Scene->NumBodies, Count);
				}
				*OutCount = Count;
				if (Result == PT_E_CAPACITY)
				{
					return Kit.SetLastError(PT_E_CAPACITY, "ReadBodyStates: %u states needed, capacity %u", Count, Capacity);
				}
				if (Result >= 0 && ReadFlags == PT_READ_ALL && OutIndices != nullptr)
				{
					// The dense read's indices are 0 .. N-1 by definition; the kit writes them for every backend (M3).
					for (uint32_t Index = 0; Index < Count; ++Index)
					{
						OutIndices[Index] = Index;
					}
				}
				return Result;
			});
		}

		static PtResult PT_CALL ReadMassProperties(const PtSceneId Id, const uint32_t First, const uint32_t Num, PtMassProperties* Out)
		{
			return Guarded("ReadMassProperties", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ReadMassProperties", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (First > Scene->NumBodies || Num > Scene->NumBodies - First || (Num > 0 && Out == nullptr))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadMassProperties: range [%u, +%u) is outside the %u bodies, or Out is NULL", First, Num, Scene->NumBodies);
				}
				for (uint32_t Index = First; Scene->NumReleased > 0 && Index < First + Num; ++Index)
				{
					if (Scene->IsReleased(Index))
					{
						return Kit.SetLastError(PT_E_INVALID_ARG, "ReadMassProperties: body %u in [%u, +%u) is released", Index, First, Num);
					}
				}
				return Scene->ReadMassProperties(First, Num, Out);
			});
		}

		static PtResult PT_CALL GetStepStats(const PtSceneId Id, PtStepStats* Out)
		{
			return Guarded("GetStepStats", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "GetStepStats", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (Out == nullptr || Out->StructSize < sizeof(PtStepStats))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "GetStepStats: Out is NULL or its StructSize is below %u", static_cast<unsigned>(sizeof(PtStepStats)));
				}
				PtStepStats Stats = {};
				Stats.StructSize = sizeof(PtStepStats);
				Stats.NumActiveBodies = -1;
				Stats.NumSleepingBodies = -1;
				Stats.NumContactPairs = -1;
				Stats.NumContactPoints = -1;
				Stats.NumIslands = -1;
				const PtResult Result = Scene->GetStepStats(Stats);
				if (Result >= 0)
				{
					Stats.StructSize = sizeof(PtStepStats);
					*Out = Stats;
				}
				return Result;
			});
		}

		static PtResult PT_CALL CaptureDebugSnapshot(const PtSceneId Id, const PtDebugSnapshotRequest* Req, PtDebugSnapshotCounts* Out)
		{
			return Guarded("CaptureDebugSnapshot", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "CaptureDebugSnapshot", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (Req == nullptr || Out == nullptr || Req->StructSize < sizeof(PtDebugSnapshotRequest))
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CaptureDebugSnapshot: Req / Out is NULL or Req->StructSize is too small");
				}
				// v1.1 (M9 fix round 1): the Reserved-field rule (docs/Abi.md 2) and the defined request bits, before the
				// backend's capability: an argument error is PT_E_INVALID_ARG on every Kit-based module.
				if (Req->Reserved != 0)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CaptureDebugSnapshot: PtDebugSnapshotRequest.Reserved must be 0 (0x%x)", Req->Reserved);
				}
				if ((Req->Flags & ~PT_SNAP_ALL) != 0)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "CaptureDebugSnapshot: unknown PT_SNAP_* bits 0x%x (defined: PT_SNAP_ALL 0x%x)",
						Req->Flags & ~PT_SNAP_ALL, PT_SNAP_ALL);
				}
				return Scene->CaptureDebugSnapshot(*Req, *Out);
			});
		}

		static PtResult PT_CALL ReadDebugSnapshot(const PtSceneId Id, const PtDebugSnapshotBuffers* Buffers)
		{
			return Guarded("ReadDebugSnapshot", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ReadDebugSnapshot", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (Buffers == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadDebugSnapshot: Buffers is NULL");
				}
				if (Buffers->Reserved != 0)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ReadDebugSnapshot: PtDebugSnapshotBuffers.Reserved must be 0 (0x%x)", Buffers->Reserved);
				}
				return Scene->ReadDebugSnapshot(*Buffers);
			});
		}

		static PtResult PT_CALL ComputeNativeStateHash(const PtSceneId Id, uint64_t* OutHash)
		{
			return Guarded("ComputeNativeStateHash", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(Id, "ComputeNativeStateHash", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				if (OutHash == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "ComputeNativeStateHash: OutHash is NULL");
				}
				return Scene->ComputeNativeStateHash(*OutHash);
			});
		}

		static PtResult PT_CALL SetOption(const PtSceneId SceneOrZero, const char* Key, const char* Value)
		{
			return Guarded("SetOption", [&]() -> PtResult
			{
				if (Key == nullptr || Value == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "SetOption: Key or Value is NULL");
				}
				if (SceneOrZero == 0)
				{
					const PtResult Ready = RequireReady("SetOption");
					return Ready != PT_OK ? Ready : Backend->SetModuleOption(FStringRef::FromCString(Key), FStringRef::FromCString(Value));
				}
				IPhysicsBackendScene* Scene = nullptr;
				const PtResult Entered = EnterScene(SceneOrZero, "SetOption", EScenePhase::Idle, Scene);
				if (Entered != PT_OK)
				{
					return Entered;
				}
				return Scene->SetOption(FStringRef::FromCString(Key), FStringRef::FromCString(Value));
			});
		}

		static PtResult PT_CALL DumpEffectiveSettings(const PtSceneId SceneOrZero, PtTextBuffer* OutJsonLines)
		{
			return Guarded("DumpEffectiveSettings", [&]() -> PtResult
			{
				IPhysicsBackendScene* Scene = nullptr;
				if (SceneOrZero == 0)
				{
					const PtResult Ready = RequireReady("DumpEffectiveSettings");
					if (Ready != PT_OK)
					{
						return Ready;
					}
				}
				else
				{
					const PtResult Entered = EnterScene(SceneOrZero, "DumpEffectiveSettings", EScenePhase::Idle, Scene);
					if (Entered != PT_OK)
					{
						return Entered;
					}
				}
				const PtResult BufferOk = ValidateTextBuffer(OutJsonLines, "DumpEffectiveSettings", true);
				if (BufferOk != PT_OK)
				{
					return BufferOk;
				}
				// Two passes: measure, then write only if everything fits (on PT_E_CAPACITY nothing is written).
				FTextWriter Measure(nullptr);
				PtResult Result = Scene != nullptr ? Scene->DumpEffectiveSettings(Measure) : DumpModuleLevel(Measure);
				if (Result < 0)
				{
					return Result;
				}
				const uint32_t Needed = Measure.GetNeeded();
				if (OutJsonLines->Data == nullptr || OutJsonLines->Capacity < Needed)
				{
					OutJsonLines->Length = Needed;
					return Kit.SetLastError(PT_E_CAPACITY, "DumpEffectiveSettings: %u bytes needed, capacity %u", Needed, OutJsonLines->Capacity);
				}
				FTextWriter Writer(OutJsonLines);
				Result = Scene != nullptr ? Scene->DumpEffectiveSettings(Writer) : DumpModuleLevel(Writer);
				if (Result < 0)
				{
					return Result;
				}
				const PtResult Finished = Writer.Finish();
				if (Finished != PT_OK)
				{
					return Kit.SetLastError(PT_E_INTERNAL, "DumpEffectiveSettings: the second pass wrote %u bytes, the first measured %u (output must be deterministic)",
						Writer.GetNeeded(), Needed);
				}
				return Result;
			});
		}

		static PtResult PT_CALL GetMemoryStats(const PtSceneId SceneOrZero, PtMemoryStats* Out)
		{
			return Guarded("GetMemoryStats", [&]() -> PtResult
			{
				if (Out == nullptr)
				{
					return Kit.SetLastError(PT_E_INVALID_ARG, "GetMemoryStats: Out is NULL");
				}
				// -1 = "the backend cannot tell" (PtMemoryStats): a field the backend leaves alone reads as unknown, never as a
				// counted 0 (as GetStepStats pre-fills its counters).
				PtMemoryStats Stats = {};
				Stats.LiveBytes = -1;
				Stats.PeakBytes = -1;
				Stats.NumAllocs = -1;
				Stats.NumFrees = -1;
				PtResult Result = PT_OK;
				if (SceneOrZero == 0)
				{
					// Module level needs a Ready module too: a Chaos backend has no engine before InitGamePhys (PreInit).
					const PtResult Ready = RequireReady("GetMemoryStats");
					if (Ready != PT_OK)
					{
						return Ready;
					}
					Result = Backend->GetMemoryStats(Stats);
				}
				else
				{
					IPhysicsBackendScene* Scene = nullptr;
					const PtResult Entered = EnterScene(SceneOrZero, "GetMemoryStats", EScenePhase::Idle, Scene);
					if (Entered != PT_OK)
					{
						return Entered;
					}
					Result = Scene->GetMemoryStats(Stats);
				}
				if (Result >= 0)
				{
					*Out = Stats;
				}
				return Result;
			});
		}

		static PtResult PT_CALL GetLastErrorMessage(PtTextBuffer* Out)
		{
			return Guarded("GetLastErrorMessage", [&]() -> PtResult
			{
				if (Out == nullptr || (Out->Data == nullptr && Out->Capacity != 0))
				{
					return PT_E_INVALID_ARG;
				}
				const uint32_t Needed = Kit.LastErrorLength + 1;
				Out->Length = Needed;
				if (Out->Data == nullptr || Out->Capacity == 0)
				{
					return PT_E_CAPACITY;
				}
				const uint32_t Copy = Out->Capacity < Needed ? Out->Capacity - 1 : Kit.LastErrorLength;
				memcpy(Out->Data, Kit.LastError, Copy);
				Out->Data[Copy] = '\0';
				return Out->Capacity < Needed ? PT_W_TRUNCATED : PT_OK;
			}, true);
		}

		static inline const PtBackendApi Api = {
			sizeof(PtBackendApi), PT_ABI_VERSION, TBackend::ModuleName,
			&GetInfo, &InitGamePhys, &TermGamePhys, &CreateScene, &DestroyScene, &CreateMaterials, &CreateActors, &ReleaseActors,
			&CreateConstraints, &PrepareForSimulation, &SetUpForFrame, &StartFrame, &WaitPhysScenes, &EndFrame, &ApplyBodyCommands,
			&ReadBodyStates, &ReadMassProperties, &GetStepStats, &CaptureDebugSnapshot, &ReadDebugSnapshot, &ComputeNativeStateHash,
			&SetOption, &DumpEffectiveSettings, &GetMemoryStats, &GetLastErrorMessage,
		};
	};

	// Kit services of a backend class, for its own unit tests (e.g. the last error without going through the C table).
	template <class TBackend>
	inline const PtBackendApi* GetBackendApi(const uint32_t RequestedMajor)
	{
		return TBackendApiProvider<TBackend>::Get(RequestedMajor);
	}

	// The Kit's thread id source, exposed so a host-side test can compare it with GetCurrentThreadId() (02 14.1).
	inline uint32_t GetKitCurrentThreadId()
	{
		return PT_KIT_CURRENT_THREAD_ID();
	}
}   // inline namespace PT_BACKEND_NS
}   // namespace PhysicsTest::Abi

#define PT_IMPLEMENT_BACKEND_MODULE(BackendClass) \
	extern "C" __declspec(dllexport) const PtBackendApi* PT_CALL PT_GetBackendApi(uint32_t RequestedAbiMajor) \
	{ \
		return ::PhysicsTest::Abi::TBackendApiProvider<BackendClass>::Get(RequestedAbiMajor); \
	}
#define PT_IMPLEMENT_BACKEND_MODULE_STATIC(BackendClass, EntryName) \
	extern "C" const PtBackendApi* PT_CALL EntryName(uint32_t RequestedAbiMajor) \
	{ \
		return ::PhysicsTest::Abi::TBackendApiProvider<BackendClass>::Get(RequestedAbiMajor); \
	}
