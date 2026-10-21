# ABI 초안 (C ABI v0.9)

> **작성일:** 2026-10-21 (M2)
> **상태:** 초안. 핵심(CORE)은 M6 끝에서 v1.0으로 동결하고, `PROVISIONAL(M9)` 항목은 M9 끝에서 확정한다 ([04_DecisionLog.md](04_DecisionLog.md) DL29).
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §7 · [03_WorkPlan.md](03_WorkPlan.md) M2 · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md) A.1, A.2, A.8, A.13 · [Progress.md](Progress.md) M2
> **근거 표기:** **[V]** 로컬 소스를 읽었거나 직접 빌드·실행해 확인 · **[R]** 추정

이 문서는 하네스와 백엔드(Null, Jolt, Bullet, PhysX, 그리고 사용자가 만들 Chaos 브리지) 사이의 C 경계를 정의한다. 코드의 정본은 두 헤더이고, 이 문서는 그 헤더가 말로 다 담지 못하는 규칙(소유권, 스레드, phase, 오류, 버전)을 적는다. 둘이 다르면 버그이며, 헤더 주석과 이 문서를 함께 고친다.

---

## 1. 파일과 진입점

| 파일 | 내용 | 누가 쓰나 |
|---|---|---|
| `Source/Runtime/PhysicsTestAbi/Public/PhysicsTestAbi/PtBackendAbi.h` | 순수 C11 헤더. POD 구조체, 상수, 함수 표 `PtBackendApi`. 구조체 크기를 `PT_STATIC_ASSERT`로 고정한다 | 호스트, 모든 백엔드, Chaos DLL (공유하는 유일한 헤더) |
| `Source/Runtime/PhysicsTestAbi/Public/PhysicsTestAbi/PtBackendKit.h` | 백엔드 측 header-only C++ Kit. `IPhysicsBackend` / `IPhysicsBackendScene`를 구현하면 `TBackendApiProvider<T>`가 C 썽크와 모든 공통 검사를 만든다 | 백엔드만 (`PT_BACKEND_NS` 필수) |
| `Source/Runtime/BackendCommon/Public/Pt*.h` | 단위 변환, 결합 규칙, damping 변환, 접촉 규약, FNV-1a (header-only, `PT_BACKEND_NS` 필수) | CMake 백엔드 (Chaos는 엔진 자체 규칙을 쓰므로 보지 않는다) |

**진입점**
- DLL 백엔드는 `PT_GetBackendApi` 하나만 export한다 (`PT_IMPLEMENT_BACKEND_MODULE(Class)`). export가 더 있거나 C 런타임과 KERNEL32(Chaos는 여기에 UE 런타임) 밖을 import하면 게이트 A9가 실패한다 (`Tools/check_modules.py`). 여기서 "C 런타임"은 VC 런타임(`vcruntime140*`), UCRT(`ucrtbase*`, `api-ms-win-crt-*`)이고, C++ 표준 라이브러리를 쓰는 SDK 백엔드에는 `msvcp140*`도 들어간다. Null DLL은 `--strict-crt`(msvcp140 금지)로 검사한다 (§7).
- static 백엔드는 이름 붙은 진입점을 하나씩 둔다: `PT_GetBackendApi_Null`, `_Jolt`, `_Bullet`, `_PhysX` (`PT_IMPLEMENT_BACKEND_MODULE_STATIC(Class, Name)`). 생성되는 레지스트리(`PT::BackendRegistry`)가 이 심볼을 직접 참조한다.
- 시그니처는 `const PtBackendApi* PT_CALL PT_GetBackendApi(uint32_t RequestedAbiMajor)`이고, 지원하지 않는 major에는 NULL을 돌려준다.

---

## 2. 버전

| 항목 | 값과 규칙 |
|---|---|
| 현재 판 | v0.9: `PT_ABI_VERSION_MAJOR 0`, `PT_ABI_VERSION_MINOR 9`, `PT_ABI_VERSION = (major << 16) \| minor` |
| 핸드셰이크 | 호스트는 **자신이 빌드된 major**로 부른다 (`PT_GetBackendApi(PT_ABI_VERSION_MAJOR)`). v0.9 스파이크 DLL(M2.5)도 major 0으로 빌드하므로 둘이 맞는다. major+1은 NULL이다 (A1) |
| 표 검사 (호스트) | `StructSize >= sizeof(PtBackendApi)`, `AbiVersion >> 16 == PT_ABI_VERSION_MAJOR`, 25개 함수 포인터가 모두 NULL이 아님 (`FPhysicsBackendModule::ValidateApiTable`) |
| 입력 구조체 | `StructSize`가 이 헤더의 `sizeof`보다 작으면 `PT_E_INVALID_ARG`. 더 큰 값(더 새 호스트)은 받고 모르는 꼬리는 무시한다 (`PtBackendInfo`, `PtInitParams`, `PtSceneDesc`, `PtActorBatch`, `PtStepStats`, `PtDebugSnapshotRequest`) |
| 출력 구조체 | `GetInfo`는 `StructSize`를 모듈이 아는 크기로 돌려 써서 호스트가 어느 필드까지 유효한지 알게 한다 |
| 배열 stride | `sizeof(T)` 이상이고 `alignof(T)`의 배수여야 한다. 더 긴 stride는 더 새 호스트의 확장 구조체로 보고 받는다 (A5) |
| 잠정 리비전 | `PT_ABI_PROVISIONAL_REVISION` (지금 1). 모듈이 다른 리비전으로 빌드되었으면 호스트는 `PT_CAP_PROVISIONAL_MASK`의 capability를 없는 것으로 본다 (`FBackendInfo::Capabilities`. 선언값은 `DeclaredCapabilities`) |
| 동결 | M6 끝 `abi-v1.0` 태그에서 major 1 / minor 0. 그 뒤 v1.x는 append-only다: StructSize/stride 뒤 필드 추가, 표 끝 함수 추가, 새 bit·enum만. 호환을 깨면 major를 올리고 Chaos를 다시 빌드한다 |

---

## 3. 결과 코드

모든 진입점은 `PtResult`(int32)를 돌려준다. 0 이상은 성공이다.

| 코드 | 값 | 뜻 | 마지막 오류 문자열 |
|---|---|---|---|
| `PT_OK` | 0 | 성공 | 바꾸지 않는다 |
| `PT_W_TRUNCATED` | 1 | 성공. 텍스트 출력이 용량에서 잘렸다 (`GetLastErrorMessage`, `PrepareForSimulation`만) | 바꾸지 않는다 |
| `PT_E_INVALID_ARG` | -1 | 인자 오류: NULL, 범위 밖 인덱스, 비유한 값, 단위가 아닌 쿼터니언, 질량 0, `AsyncDt = 0`, 알 수 없는 옵션 키 등 | 이유를 적는다 |
| `PT_E_INVALID_HANDLE` | -2 | 살아 있지 않은 `PtSceneId` (0, 파괴된 세대, 범위 밖 슬롯) | 적는다 |
| `PT_E_BAD_STATE` | -3 | 모듈 상태나 씬 phase가 맞지 않는다 | 적는다 |
| `PT_E_WRONG_THREAD` | -4 | 소유 스레드가 아닌 스레드에서 불렸다 | **바꾸지 않는다** (부작용 없음) |
| `PT_E_UNSUPPORTED` | -5 | 백엔드가 선언하지 않은 기능 (capability 없는 kinematic, convex, 상태 쓰기, Native frame model이 아닌 백엔드의 substepping·AsyncDt > 0, 스냅샷) | 적는다 |
| `PT_E_CAPACITY` | -6 | 버퍼가 작다. 필요한 개수(또는 NUL을 포함한 바이트 수)를 돌려 쓰고 그 밖에는 아무것도 쓰지 않는다. 씬 슬롯 16개가 다 찼을 때도 쓴다 | 적는다 |
| `PT_E_VERSION` | -7 | 예약 (버전 불일치) | |
| `PT_E_ENGINE` | -8 | 엔진 자체 오류 | 적는다 |
| `PT_E_OUT_OF_MEMORY` | -9 | 할당 실패 | 적는다 |
| `PT_E_TRANSPORT` | -10 | 예약 (OOP 전송, M14) | |
| `PT_E_INTERNAL` | -11 | 백엔드 내부 오류. CMake 빌드에서는 경계를 넘으려던 C++ 예외 | 적는다 |

- 실패한 호출은 모두 마지막 오류 문자열을 남긴다. 백엔드가 메시지 없이 실패 코드를 돌려주면 Kit이 "`<Entry>` failed with `<code>`"를 대신 적는다. 성공한 호출은 문자열을 지우지 않는다 (Win32 `GetLastError`와 같다).
- 엔진 수준의 오류(Jolt update error, PhysX error callback)는 반환 코드가 아니라 `PtFrameResult::EngineErrorBits`, `PtStepStats::EngineErrorBits`로 낸다. 반환 코드는 계약 위반에 쓴다.

---

## 4. 함수 표

"상태"는 모듈 상태, "phase"는 씬 phase다. "버킷"은 [02_Architecture.md](02_Architecture.md) §9.3의 타이밍 버킷이다. "동결"의 핵심은 M6, 잠정은 M9에 확정한다.

| # | Entry | 상태 | phase | 버킷 | 의미 (Kit 검사 포함) | 동결 |
|---|---|---|---|---|---|---|
| 1 | `GetInfo(Out)` | 언제나 | - | - | 이름, capability, module flag, `BuildFlagsJson`(어댑터 TU 자기보고), 필수 effective 키(백엔드 집합), `ProvisionalRevision`. 문자열은 모듈의 정적 문자열이다 | 핵심 |
| 2 | `InitGamePhys(Params)` | Loaded (또는 NO_REINIT가 없는 Terminated) | - | 기록만 | 호출한 스레드가 소유 스레드가 된다. 로그 콜백, `MaxThreads`(≥ 1), 모듈 옵션(모르는 키는 `PT_E_INVALID_ARG`). 실패하면 모듈은 그대로 Loaded다 | 핵심 |
| 3 | `TermGamePhys()` | Ready | - | - | 살아 있는 씬을 슬롯 순서대로 모두 파괴한 뒤(어느 phase든) 엔진을 종료한다. 소유 스레드와 로그 콜백을 지운다 | 핵심 |
| 4 | `CreateScene(Desc, OutScene)` | Ready | - | T_create | `PtSceneDesc` 검증(§5.5), 빈 슬롯이 없으면 `PT_E_CAPACITY`. 실패하면 `*OutScene = 0` | 핵심 |
| 5 | `DestroyScene(Scene)` | Ready | Idle | - | 세대가 올라가 같은 id는 이후 `PT_E_INVALID_HANDLE` | 핵심 |
| 6 | `CreateMaterials(Scene, Materials, Num, Stride)` | Ready | Idle | T_create | 원래의 F, SF, R과 호스트가 정한 머티리얼별 결합 모드. 인덱스는 생성 순서(0부터 누적) | 핵심 |
| 7 | `CreateActors(Scene, Batch)` | Ready | Idle | T_create | 배치 하나. `BodyIndex`는 지금까지 만든 바디 수 + i여야 한다. 백엔드 내부는 UE 순서(§5.9) | 핵심 |
| 8 | `ReleaseActors(Scene, Indices, Num)` | Ready | Idle | - | 인덱스는 그대로 남고(재사용 없음) 이후 그 바디에 대한 명령·질량 읽기는 거부된다. 범위 밖, 이미 해제한 인덱스, 한 호출 안의 중복은 Kit이 `PT_E_INVALID_ARG`로 거부한다 (전부 아니면 전무) | 핵심 |
| 9 | `CreateConstraints(Scene, Infos, Num, Stride)` | Ready | Idle | T_create | ball/hinge. Null은 `PT_E_UNSUPPORTED` | 잠정 |
| 10 | `PrepareForSimulation(Scene, OutActions)` | Ready | Idle | T_prepare | 비타이밍 준비(Jolt `OptimizeBroadPhase` 등)와 그 설명 텍스트. `OutActions`는 NULL이어도 된다. 한 번만 하므로 텍스트가 넘치면 잘라서 `PT_W_TRUNCATED` | 핵심 |
| 11 | `SetUpForFrame(Scene, NewGrav, Dt, MinDt, MaxDt, MaxSubDt, MaxSubsteps, bSubstepping)` | Ready | Idle → SetUp | T_step | UE `FChaosScene::SetUpForFrame` 인자 그대로. 시간은 유한, Dt·MinDt ≥ 0, MaxSubsteps ≥ 1, bSubstepping ∈ {0, 1}. `NATIVE_FRAME_MODEL`이 없으면 MaxSubsteps = 1이고 substepping이 아니어야 한다 | 핵심 |
| 12 | `StartFrame(Scene)` | Ready | SetUp → Started | T_step | 엔진을 진행시킨다 (Chaos: `AdvanceAndDispatch`) | 핵심 |
| 13 | `WaitPhysScenes(Scene)` | Ready | Started → Waited | T_step | 끝날 때까지 기다린다 | 핵심 |
| 14 | `EndFrame(Scene, OutResult)` | Ready | Waited → Idle | T_step | `PtFrameResult{NumStepsTaken, EngineErrorBits}`. `OutResult`가 NULL이면 `PT_E_INVALID_ARG`이고 phase는 그대로다 | 핵심 |
| 15 | `ApplyBodyCommands(Scene, Cmds, Num, Stride)` | Ready | Idle | T_push | `*_AssumesLocked` 명령 배치(§5.10). 전체를 먼저 검증하고(공통 + 해제한 바디 + 백엔드별) 하나라도 틀리면 아무것도 적용하지 않는다 | 핵심 |
| 16 | `ReadBodyStates(Scene, ReadFlags, OutIndices, OutStates, Capacity, OutCount)` | Ready | Idle | T_pull | `PT_READ_ALL` 또는 `PT_READ_ACTIVE_ONLY`(§5.8). 두 번 호출 프로토콜. `PT_READ_ALL`은 빽빽하다: 만든 바디 전부를 인덱스 순서로, 해제한 바디는 ObjectState UNINITIALIZED에 0 자세(항등 회전)·0 속도로 낸다. 그래서 `OutIndices`는 NULL이어도 된다 (Kit이 개수 = 만든 바디 수를 확인하고, 아니면 `PT_E_INTERNAL`). `PT_READ_ACTIVE_ONLY`는 살아 있는 바디의 부분 집합이라 Capacity > 0이면 `OutIndices`가 필요하다 (NULL이면 `PT_E_INVALID_ARG`) | 핵심 |
| 17 | `ReadMassProperties(Scene, First, Num, Out)` | Ready | Idle | 비타이밍 | 엔진이 실제로 가진 질량·주관성·COM (G2 readback). 범위에 해제한 바디가 있으면 `PT_E_INVALID_ARG` | 핵심 |
| 18 | `GetStepStats(Scene, Out)` | Ready | Idle | 비타이밍 | 리스너가 필요 없는 카운터. 모르는 값은 -1 | 핵심 |
| 19 | `CaptureDebugSnapshot(Scene, Req, Out)` | Ready | Idle | 비타이밍 | 2단계 중 1단계(개수). Null은 `PT_E_UNSUPPORTED` | 잠정 |
| 20 | `ReadDebugSnapshot(Scene, Buffers)` | Ready | Idle | 비타이밍 | 2단계 중 2단계(복사) | 잠정 |
| 21 | `ComputeNativeStateHash(Scene, OutHash)` | Ready | Idle | 비타이밍 | 하네스 바디 순서의 FNV-1a. Null은 살아 있는 바디의 actor 이동·회전 double 7개 | 핵심 |
| 22 | `SetOption(SceneOrZero, Key, Value)` | Ready | Idle (씬) | - | 0이면 모듈 옵션. 모르는 키는 `PT_E_INVALID_ARG` | 핵심 |
| 23 | `DumpEffectiveSettings(SceneOrZero, OutJsonLines)` | Ready | Idle (씬) | 비타이밍 | JSON lines (§5.14). 두 번 측정해 결정적이어야 한다. 0이면 모듈 수준 키이고, 그 끝에 Kit이 `kit.mxcsr_restores`(§5.11)를 붙인다 | 핵심 |
| 24 | `GetMemoryStats(SceneOrZero, Out)` | Ready | Idle (씬) | 비타이밍 | 백엔드가 아는 만큼. 모듈 수준(0)도 Ready에서만 된다 (Chaos는 InitGamePhys 전에 엔진이 없다) | 핵심 |
| 25 | `GetLastErrorMessage(Out)` | 언제나 | - | - | 마지막 오류 문자열. 자기 자신은 마지막 오류를 바꾸지 않는다. (NULL, 0)이면 `PT_E_CAPACITY` + 길이, 작으면 잘라서 `PT_W_TRUNCATED` | 핵심 |

---

## 5. 규칙

### 5.1 스레드

- 모든 호출은 `InitGamePhys`를 부른 스레드(소유 스레드)에서 한다. 다른 스레드의 호출은 **부작용 없이** `PT_E_WRONG_THREAD`다: 출력 인자를 쓰지 않고 마지막 오류도 바꾸지 않는다 (A2가 25개 진입점 전부와 출력 sentinel로 확인한다).
- 소유 스레드가 없을 때(Init 전, Term 뒤)는 어느 스레드든 부를 수 있지만, 그때 동작하는 진입점은 `GetInfo`, `InitGamePhys`, `GetLastErrorMessage` 셋뿐이다. 나머지는 모듈 수준 호출(`SetOption(0)`, `DumpEffectiveSettings(0)`, `GetMemoryStats(0)`)까지 모두 `PT_E_BAD_STATE`다. 단 NULL 인자 같은 인자 오류는 그보다 먼저 `PT_E_INVALID_ARG`로 보고될 수 있다 (A0가 확인한다).
- 소유 스레드 id는 소유 스레드가 Init·Term에서 쓰고 모든 썽크가 어느 스레드에서든 읽는다. Kit은 이 값을 relaxed atomic 32-bit 접근(`__iso_volatile_load32` / `__iso_volatile_store32`, MSVC `std::atomic`의 relaxed load·store와 같은 연산)으로 다룬다. 인터페이스에 `<atomic>`을 넣지 않기 위해서다.
- 로그 콜백(`PtLogCallback`)은 예외다. 백엔드는 어느 스레드에서든 콜백을 부를 수 있고, 호스트 콜백은 스레드 안전하다.
- Kit의 스레드 id는 TEB(`__readgsdword(0x48)`)에서 읽는다. `GetCurrentThreadId()`와 같음을 단위 테스트가 확인한다. UE 빌드는 `PT_KIT_CURRENT_THREAD_ID`를 `FPlatformTLS::GetCurrentThreadId()`로 정의한다.
- 엔진 작업 스레드는 엔진 안에만 있다. 스레드 예산은 `PtInitParams::MaxThreads`와 `PtSceneDesc::NumThreads`(호출자 포함 T)다.

### 5.2 모듈 상태기계

```
Loaded --InitGamePhys--> Ready --TermGamePhys--> Terminated --InitGamePhys--> Ready   (PT_MODULE_NO_REINIT이면 마지막 화살표 없음)
```
- 두 번째 Init과 Init 전의 Term은 `PT_E_BAD_STATE`다 (A0).
- Term은 남은 씬을 모두 파괴한다. 호스트(`FPhysicsBackendModule::TermGamePhys`)는 씬 프록시가 살아 있으면 `checkf`로 멈춘다.
- 모듈은 한 번 로드하면 해제하지 않는다 (FreeLibrary 없음). UE_RUNTIME 모듈(Chaos)은 프로세스당 하나다.
- static 백엔드와 같은 소스의 DLL은 서로 다른 모듈이다. Kit 상태(모듈 상태, 슬롯, 마지막 오류, 소유 스레드)가 따로 있다.

### 5.3 씬 phase 상태기계

```
Idle --SetUpForFrame--> SetUp --StartFrame--> Started --WaitPhysScenes--> Waited --EndFrame--> Idle
```
- 생성(`CreateMaterials`, `CreateActors`, `ReleaseActors`, `CreateConstraints`), 준비, 쓰기(`ApplyBodyCommands`, `SetOption`), 읽기(`ReadBodyStates`, `ReadMassProperties`, `GetStepStats`, 스냅샷, 해시, `DumpEffectiveSettings`)와 `DestroyScene`은 Idle에서만 된다.
- 순서를 어긴 호출은 phase를 바꾸지 않고 `PT_E_BAD_STATE`다 (A3). 백엔드가 실패를 돌려주면 phase는 나아가지 않는다.
- Native driver의 한 프레임은 4개 호출(T_step) 한 벌이고, Harness driver는 계획한 스텝마다 한 벌이다 (§5.8).

### 5.4 소유권과 버퍼

- 버퍼는 언제나 호출자 소유다. 모듈은 힙 소유권을 넘기지 않으며, 모듈이 내보내는 문자열(`GetInfo`, `ModuleName`)은 정적 문자열이다.
- **두 번 호출 프로토콜:** 용량이 부족하면 `PT_E_CAPACITY`와 필요한 개수(텍스트는 NUL을 포함한 바이트 수 `PtTextBuffer::Length`)를 돌려 쓰고 **그 밖에는 아무것도 쓰지 않는다** (A5가 sentinel로 확인한다). Kit은 텍스트 출력을 한 번 측정한 뒤 들어갈 때만 다시 써서 이 성질을 보장한다. 그래서 `DumpEffectiveSettings` 구현은 결정적이어야 한다.
- 예외 두 가지: `GetLastErrorMessage`(진단용이라 잘린 앞부분이 쓸모 있다)와 `PrepareForSimulation`(한 번만 하는 동작이라 두 번 부를 수 없다)은 잘라서 쓰고 `PT_W_TRUNCATED`를 돌려준다.
- 입력 배열(`Materials`, `Actors`, `Shapes`, `Cmds`, `Infos`)은 호출 동안만 읽는다. 백엔드가 계속 필요하면 복사한다.

### 5.5 입력 검증 (Kit 공통)

| 대상 | 거부 조건 (`PT_E_INVALID_ARG`, 기능이 없으면 `PT_E_UNSUPPORTED`) |
|---|---|
| `PtSceneDesc` | StructSize 부족, RunMode > 2, 모르는 InstrumentFlags·SimFlags bit, 비유한 중력, NominalDeltaTime ≤ 0 또는 비유한, `AsyncFixedTimeStepSize`가 정확히 -1도 양수도 아님(0, 음수, NaN), 비유한 RestitutionThreshold·MaxLinearVelocity, 음수 반복 횟수, NumThreads 0, NULL 옵션 키·값. `AsyncFixedTimeStepSize > 0`인데 `NATIVE_FRAME_MODEL`이 없으면 `PT_E_UNSUPPORTED` |
| `PtPhysicalMaterial` | 비유한·음수 마찰·정지마찰·반발·sleep 임계, 결합 모드 > 3 |
| `PtShapeElem` | 비유한 변환 또는 단위가 아닌 회전(1e-6), MaterialIndex ≥ 머티리얼 수, 비유한 offset, 모르는 flag, 모르는·예약된 타입(TRIMESH 포함), 구 반지름 ≤ 0, 박스 half extent ≤ 0, 캡슐 반지름 ≤ 0 또는 HalfLength < 0, convex 정점 범위 밖(capability가 없으면 `PT_E_UNSUPPORTED`) |
| `PtActorCreateInfo` | BodyIndex가 생성 순서가 아님, Motion > 2, 모르는 flag, 셰이프가 없거나 범위 밖, ObjectType ≥ 32, 비유한·단위가 아닌 자세와 COM 프레임, 비유한 초기 속도, 비유한·음수 질량·관성, dynamic인데 질량이나 관성이 0, 비유한·음수 damping, 비유한 속도 한계·음수 임계 배수. kinematic인데 capability가 없으면 `PT_E_UNSUPPORTED` |
| `PtBodyCommand` | 바디 인덱스 ≥ 바디 수, 해제한 바디, 모르는 flag, 모르는 타입, 비유한 벡터, 자세 명령의 단위가 아닌 회전. capability 없는 kinematic target / 상태 쓰기는 `PT_E_UNSUPPORTED` |
| `ReleaseActors` | 바디 인덱스 ≥ 바디 수, 이미 해제한 바디, 한 호출 안의 중복 |
| `ReadBodyStates` / `ReadMassProperties` | 모르는 ReadFlags, `PT_READ_ACTIVE_ONLY`인데 Capacity > 0이고 `OutIndices` NULL / 범위 밖이거나 해제한 바디가 든 범위 |
| `SetUpForFrame` | §4 #11 |
| 배열 | NULL(개수 > 0), stride < sizeof 또는 정렬되지 않음 |

- 비유한 값 검사는 비트로 한다. 하네스와 UE 게임 빌드는 `/fp:fast`라서 `isfinite`·NaN 비교가 접힐 수 있다 ([Progress.md](Progress.md) M0 §4).
- 해제 집합은 Kit이 들고 있다 (`IPhysicsBackendScene::IsReleased`, 바디당 1 bit, malloc). 그래서 해제한 바디에 대한 명령·질량 읽기, 중복·이중 해제는 모든 백엔드에서 같은 방식으로 거부된다 (M2 수정 라운드 1. 전에는 백엔드마다 다시 구현해야 했고 Jolt `RemoveBodies`는 중복 BodyID에서 assert한다). 그 밖의 백엔드별 검사(kinematic이 아닌 바디의 kinematic target, 모르는 옵션 키)는 백엔드가 한다. 명령 배치의 백엔드 검사(`IPhysicsBackendScene::ValidateBodyCommand`)도 적용 전에 배치 전체에 대해 돈다.
- 배치는 전부 아니면 전무다. A4는 거부된 호출 뒤 씬이 건드리지 않은 쌍둥이 씬과 다음 프레임까지 비트 단위로 같음을 확인한다.

### 5.6 핸들과 인덱스

- `PtSceneId = (generation << 32) | slot`, 0은 무효다. 슬롯은 `PT_KIT_MAX_SCENES`(16)개이고 파괴할 때 세대가 올라간다.
- 바디 인덱스는 생성 순서 = 삽입 순서 = 해시 순서다. 해제해도 재사용하지 않는다.
- 머티리얼 인덱스는 `CreateMaterials` 호출을 누적한 생성 순서다.

### 5.7 단위와 변환

- cm, kg, s, rad, Z-up, double. ABI 경계에서 단위가 바뀌지 않는다. 엔진 단위 변환은 백엔드 안 `PtUnitScale` 한 곳에서만 한다 (D1).
- 박스는 half extent다. 호스트가 `FKBoxElem`의 full extent를 반으로 나누는 곳은 `FPhysicsBackendScene::MarshalBodies` 하나다.
- 캡슐은 `Radius` + `HalfLength`(선분 절반 = `FKSphylElem::Length / 2`)이고 축은 로컬 Z다. 디버그 드로의 HalfHeight는 `HalfLength + Radius`다.
- 셰이프 치수와 로컬 변환에는 바디 스케일(`FBodyInstance::Scale3D`)이 UE `GetFinalScaled` 규칙으로 이미 들어가 있다. `FBodySetup::BuildScale3D`는 메타데이터라 곱하지 않는다.
- `ActorTransform`은 바디 원점의 강체 자세(스케일 없음), `CenterOfMassLocal`은 actor 공간의 COM 위치와 주축 회전이다. 속도는 COM의 cm/s와 rad/s다.
- 질량·관성·COM은 하네스가 계산해 넘긴다 (BodyUtils, D5). 엔진은 그 값을 쓴다.

### 5.8 프레임 모델

- **Harness driver (SDK 백엔드):** 호스트가 `FStepPlanner`로 스텝을 계획하고 스텝마다 `SetUpForFrame(&G, StepDt, 0, 0, 0, 1, false)` → `StartFrame` → `WaitPhysScenes` → `EndFrame`을 부른다. 씬은 `AsyncFixedTimeStepSize = -1`로 만든다 (async 누산기도 호스트가 돌린다). `NumStepsTaken`은 1이어야 한다.
- **Native driver (`PT_CAP_NATIVE_FRAME_MODEL`: Chaos, Null):** 호스트가 UE `SetUpForFrame` 인자를 프레임당 한 번 넘기고 백엔드가 UE5 규칙(`FChaosScene::SetUpForFrame`의 float clamp, `FPhysicsSolverBase::AdvanceAndDispatch_External`의 FReal 계획, 솔버 루프의 MinDeltaTime 처리)으로 스텝한다. AsyncFixed는 씬을 `AsyncFixedTimeStepSize > 0`으로 만들어 백엔드가 누산기를 돌린다.
- **`NumStepsTaken`의 정의:** 그 프레임에 엔진이 실제로 진행한 스텝 수다 (UE `FPBDRigidsSolver`의 AdvanceOneTimeStep 횟수). 하네스의 `FStepPlan::NumSteps`와 같아야 하며, 밀어 넣은 스텝 수 `FStepPlan::NumPushedSteps`가 아니다. 둘은 MinDeltaTime 이하의 스텝에서 갈라진다. 하네스는 다르면 Fatal로 멈춘다. Chaos 브리지는 솔버 스텝 콜백 횟수 같은 실제 진행 횟수를 세어야 한다 [R] (M13).
- **스텝 dt:** Native 백엔드는 UE처럼 double `InternalDt`(UE5 substepping에서는 D/N)로 스텝한다. Harness driver는 ABI의 float로 넘긴다. 둘은 UE5 substepping에서만 다르다 (dt 0.05: 0.016666666915 대 0.016666667536, 상대 3.7e-8). 하네스는 `FStepPlan::InternalStepDeltaTime`(double)과 `StepDeltaTime`(float)을 둘 다 가진다. Native driver 결과의 해석 기준값은 앞의 것, Harness driver는 뒤의 것(`dt_engine`)으로 계산한다.
- **두 driver 모두 매 프레임 push한다.** 스텝이 없는 프레임(dt ≤ MinDeltaTime, push가 없는 AsyncFixed 프레임)에도 그 프레임에 쓴 명령이 백엔드에 닿는다 (Harness driver는 큐만 flush하고, 힘·kinematic 목표는 아래 규칙대로 호스트가 들고 있다). UE5의 GT 쓰기가 호출하는 순간 GT 파티클에 들어가는 것과 같아서, 읽기와 `bAddToCurrent`가 앞서 쓴 값을 본다 (M2 수정 라운드 1).
- FixedStep과 Sync에서는 두 driver가 백엔드에 같은 명령을 같은 순서로 보내므로 결과가 비트 단위로 같아야 한다. 스텝이 없는 프레임(dt 0, 1e-9)을 포함한다 (A10 (b)).
- `PT_READ_ACTIVE_ONLY`는 이전 읽기 뒤로 상태가 바뀌었을 수 있는 살아 있는 바디를 모두 돌려준다: 깨어 있는 dynamic 바디, 움직인 kinematic 바디, ObjectState가 바뀐 바디(잠들거나 깬 바디 포함). 잠든 바디가 빠지면 호스트의 ObjectState가 낡기 때문이다. 부분 집합이므로 `OutIndices`가 필요하다. `PT_READ_ALL`은 해제한 바디까지 인덱스 순서로 빽빽하다 (§4 #16).

### 5.9 바디 생성 순서 (`CreateActors`)

UE `FInitBodiesHelper` 순서를 따른다 (BodyInstance.cpp:1669, 1695-1706, 4648-4743 [V]):
1. 바디마다 CreateActor (생성 시점 flag: motion, gravity, CCD, 반복 횟수, inertia conditioning, gyroscopic, 최대 선속도)
2. 셰이프 → 충돌 필터 → 머티리얼
3. AddActorsToScene 한 번 (PhysX `addActors`)
4. 바디마다 InitDynamicProperties: 질량·관성·COM → damping → 최대 각속도 → 최대 침투 해소 속도 → 초기 속도 → sleep 임계 → 안정화 임계 → 반복 횟수
5. `!PT_BODY_START_AWAKE`인 dynamic 바디는 wake counter 0 + putToSleep. 씬이 sleep을 끄면(`PT_SIM_ALLOW_SLEEPING` 없음, Normalized) 모든 엔진이 건너뛰고 호스트가 센다

### 5.10 명령

| 명령 | 단위·필드 | 의미 (Null 기준, Chaos와 같게 둔 것은 인용) |
|---|---|---|
| `SET_GLOBAL_POSE` | `Transform` = actor 자세 | 텔레포트. 잠든 dynamic 바디는 언제나 깨운다 (UE5 `SetX`, SingleParticlePhysicsProxy.h:240-251 [V]; `PT_CMDF_AUTO_WAKE`는 Native 백엔드에 정보용). kinematic 바디는 그 자세가 목표가 되고 선·각속도가 바로 0이 된다 (ChaosEngineInterface.cpp:2550-2557 [V]) |
| `SET_KINEMATIC_TARGET` | `Transform` | kinematic 바디의 목표. 아래 프레임 규칙대로 스텝이 돌 때까지 남아 있고, 그 프레임의 마지막 목표가 이긴다 |
| `SET_LINEAR_VELOCITY` / `SET_ANGULAR_VELOCITY` | cm/s, rad/s | 즉시 설정 |
| `ADD_IMPULSE` | kg·cm/s | 누적했다가 다음 스텝 적분에 속도 변화로 들어간다 (PBDRigidsEvolutionGBF.cpp:920-924 [V]) |
| `ADD_VELOCITY` | cm/s | Chaos처럼 M·dv 임펄스다 (ChaosEngineInterface.cpp:861-864 [V]) |
| `ADD_ANGULAR_IMPULSE` / `ADD_ANGULAR_VELOCITY` | kg·cm²/s, rad/s | 각속도 변화 = I_world⁻¹·L. 각속도 명령은 I_world·dw 임펄스다 (:866-870 [V]) |
| `ADD_FORCE` / `ADD_TORQUE` | kg·cm/s², kg·cm²/s² | 아래 프레임 규칙대로 작용한다. `PT_CMDF_ACCEL_CHANGE`는 질량 무관(힘은 F·M, 토크는 I_world·τ, :940-944, :1029-1032 [V]). `PT_CMDF_ALLOW_SUBSTEPPING`은 정보일 뿐 Native 백엔드는 무시한다 (UE5가 `bAllowSubstepping`을 쓰지 않는다, :932-950, :1024-1037 [V]). flag가 없으면 UE의 `AddForce`/`AddTorque`다: 잠든 dynamic 바디는 벡터가 0이어도 깬다 (GT 프록시가 Sleeping이면 매번 Dynamic으로 바꾼다, SingleParticlePhysicsProxy.h:706-720, :757-771 [V]; `FChaosStateOps::AddForce`의 0 검사는 그 앞의 중복 검사다). **`PT_CMDF_NO_WAKE`**이면 깨우지 않고, 그때 잠든 바디는 이 명령을 무시한다 (아래 호스트 규칙). 호스트가 보내는 힘은 모두 이 flag를 단다 |
| `WAKE_UP` / `PUT_TO_SLEEP` | - | sleep을 끈 씬의 PUT_TO_SLEEP은 무시한다 (PhysX가 `eDISABLE_SLEEPING` 씬의 putToSleep을 거부하는 것과 같다) |
| `SET_BODY_STATE` | 자세 + 선·각속도 | 하네스 전용 전체 상태 쓰기 (`PT_CAP_STATE_WRITE`) |

- static·kinematic 바디에 대한 힘·임펄스·속도 명령은 효과가 없다 (dynamic만 적분된다).
- 명령은 배열 순서대로 적용한다. 호스트는 T_push에서 큐의 명령을 한 번의 `ApplyBodyCommands`로 보낸다. 명령이 없으면 부르지 않는다 (T_push = 0).

**호스트 규칙 (두 driver 공통, M2 수정 라운드 1)**
- **힘의 깨우기:** UE의 `AddForce`/`AddTorque`는 호출하는 순간 GT에서 바디를 깨운다. 호스트(`FPhysScene::AddForce_AssumesLocked`)는 그 깨우기를 힘을 낸 자리에 `WAKE_UP`으로 큐에 넣고, 힘 자체는 push 때 `PT_CMDF_NO_WAKE`로 보낸다. 그래서 힘 뒤에 `PutToSleep`한 바디가 push 끝의 힘으로 다시 깨지 않는다 (UE: 잠든 파티클은 적분되지 않는다).
- **힘을 드는 기간:** 힘은 스텝을 push하는 프레임까지 호스트가 든다. Native는 그 프레임 push 끝에 한 번(백엔드가 가중치와 스텝 규칙을 적용), Harness는 그 프레임의 스텝마다 보낸다.
- **PutToSleep 뒤의 힘:** UE의 `SetObjectState(Sleeping)`은 GT dynamics의 dirty 표시를 지워 그 앞의 힘을 push하지 않는다. 뒤에 힘이나 임펄스가 오면 다시 dirty가 되어 앞의 힘도 함께 간다 (ParticleHandle.h:3755-3766 [V]). 호스트가 같은 규칙을 든 힘에 적용한다 (`MarkDynamicsClean_AssumesLocked`/`MarkDynamicsDirty_AssumesLocked`). sleep을 끈 씬은 PUT_TO_SLEEP을 무시하므로 지우지 않는다. 임펄스 쪽(PutToSleep 앞의 임펄스도 UE에서는 push되지 않는다)은 큐로 가므로 Null이 재현하지 않는다 (의도적 차이, [Progress.md](Progress.md) M2 §7).
- **kinematic 목표의 진입점:** 씬 동사 `FPhysScene::SetKinematicTarget_AssumesLocked`, `FBodyInstance::SetBodyTransform(ETeleportType::None)`, `FPhysicsInterface::SetKinematicTarget_AssumesLocked` 모두 씬의 목표 목록으로 간다 (UE5는 씬 동사가 인터페이스로 넘기는 한 경로다, BodyInstance.cpp:2804-2827, PhysScene_Chaos.cpp:1708-1713 [V]). kinematic 바디의 텔레포트(`SetGlobalPose_AssumesLocked`)는 큐로 가고, 씬의 대기 목표를 그 자세로 바꾼다. Harness driver는 그 뒤의 목표를 텔레포트 자세에서 보간한다.
- **토크의 월드 관성:** Native(Null)는 push 때 토크를 한 번 각가속도로 바꾸고 모든 스텝이 그 값을 쓴다 (Chaos `CopySubstepData`). Harness driver는 스텝마다 토크를 보내므로 SDK 엔진이 그 스텝의 회전으로 바꾼다. UE GT는 호출 시점의 회전으로 바꾼다. 차이는 한 프레임에 여러 스텝이 돌 때와, 같은 프레임에 토크 뒤 같은 바디를 텔레포트할 때만 난다 (A10 (d)의 허용 오차).
- **`OnPhysSceneStep`:** 두 driver 모두 프레임당 한 번, PreTick 바로 뒤에 프레임 dt로 (UE5 `OnStartFrame`). 거기서 낸 힘은 PreTick에서 낸 힘과 같다. Harness driver의 `UE4PhysSubstep` 규칙만 서브스텝마다 부르고, 서브스텝 k에서 낸 힘은 k부터 적용한다 (`bAllowSubstepping`이 없으면 k에만).

**프레임 규칙 (`NATIVE_FRAME_MODEL` 백엔드 = UE5 Chaos, Null이 기준 구현) [V]**
- **임펄스·속도 변화**는 누적했다가 다음에 실제로 도는 스텝의 적분에 한 번 들어간다. 같은 프레임의 뒤 스텝은 반복하지 않는다 (`CopySubstepData`가 임펄스만 지운다, ChaosMarshallingManager.cpp:325-342).
- **힘·토크**는 스텝을 밀어 넣는(push) 다음 프레임까지 누적되고, 그 프레임의 **모든** 스텝에 작용한다. `Step_External`이 둘째 이후 push 스텝마다 첫 스텝의 가속도를 복사한다 (ChaosMarshallingManager.cpp:105-111). push가 없는 프레임(AsyncFixed 누산 중)은 힘을 넘겨주고, push 때 가속도에 1 / (지난 push 뒤 dt > 0인 프레임 수)를 곱한다 (`ExternalSteps`, PhysicsSolverBase.cpp:623-646, `DynamicsWeight`, PBDRigidsSolver.cpp:1582). push는 했지만 도는 스텝이 없는 프레임(dt ≤ MinDeltaTime)은 힘을 소비한다 (`ClearAccumulatedData`, SingleParticlePhysicsProxy.cpp:258-267).
- **kinematic 목표**는 솔버가 substep하는 프레임(MaxSubsteps > 1)이면 k번째 스텝이 현재 자세에서 목표 쪽으로 1 / (N − k)만큼 Lerp(위치)·Slerp(회전)하고 마지막 스텝이 목표에 닿는다. 그 밖(한 스텝, AsyncFixed의 여러 스텝)에서는 첫 스텝에 닿고, 다음 스텝이 kinematic 속도를 0으로 만든다 (`PseudoFraction`, PhysicsSolverBase.cpp:250; `EKinematicTargetMode::Position`/`Reset`, PBDRigidsEvolutionGBF.cpp:1203-1220). Chaos는 회전을 float `FRotation3f::Slerp`로, Null은 double로 보간한다 (의도한 차이).
- **Harness driver**(SDK 백엔드)는 스텝마다 백엔드 프레임 하나를 돌리므로 호스트가 같은 규칙을 대신 적용한다: UE5 규칙(`ESubstepClampRule::UE5ChaosScene`)이면 모든 힘을 모든 스텝에 보내고(가중치를 곱한 힘), kinematic 목표를 위 규칙대로 스텝마다 보낸다. UE4 규칙(`UE4PhysSubstep`, PhysX UE4Compat)이면 `bAllowSubstepping` 힘만 모든 서브스텝, 나머지는 낸 서브스텝 하나이고, `bAllowSubstepping` kinematic 목표는 프레임 시작 자세에서 (k + 1) / N로 Lerp/Slerp, 나머지는 첫 서브스텝에 닿는다 (UE4 `FPhysSubstepTask` [M]). Harness driver에서는 Chaos가 가속도에 거는 가중치를 힘에 걸기 때문에 AsyncFixed 평균이 마지막 비트에서 다를 수 있다.
- `FBodyInstance::AddForce`와 `FPhysicsInterface::AddForce_AssumesLocked`는 같은 규칙을 탄다. UE5에서 앞의 것이 뒤의 것을 부르기 때문이다 (BodyInstance.cpp:3769-3779). 둘 다 소유 씬의 `FPhysScene::AddForce_AssumesLocked`(UE5 `FPhysScene_Chaos`의 동사 이름)로 간다.
- 해제한 바디에 걸려 있던 힘·kinematic 목표·큐의 명령은 버린다 (UE가 프록시와 함께 버린다). 남아 있으면 백엔드가 그 명령을 거부하고, 배치가 원자적이라 다른 바디의 명령까지 버려진다.

### 5.11 부동소수점 환경

- 호출 전후로 MXCSR의 제어 bit(6~15: DAZ, 예외 마스크, 반올림, FTZ)와 x87 제어 워드가 같아야 한다 (A6). UE PreInit 뒤 Chaos DLL에서 특히 중요하다.
- Kit의 썽크는 호출 전 MXCSR을 기억했다가, 백엔드가 제어 bit를 바꿨으면 되돌리고 처음 한 번 경고를 로그로 남기며, 되돌린 횟수를 모두 센다 (모듈 수준 effective 키 `kit.mxcsr_restores`). 상태 bit(0~5)는 계약이 아니다.
- Kit이 되돌리므로 호출 전후 비교만으로는 백엔드가 아니라 Kit의 안전망을 시험하게 된다. 그래서 A6는 로그 콜백을 걸어 "changed MXCSR" 경고가 하나라도 오거나 `kit.mxcsr_restores`가 늘면 실패한다. Chaos 브리지에서 이 경고가 나오면 엔진이 UE가 정한 상태가 아니라 호스트의 MXCSR로 돈다는 뜻이므로 원인을 찾아야 한다.
- x87 제어 워드는 x64 MSVC 코드가 건드리지 않으므로 Kit이 호출마다 저장하지 않는다 (FXSAVE가 필요해 비싸다). A6가 백엔드의 보존을 확인한다.

### 5.12 예외와 SEH

- CMake 빌드(`/EHsc`)에서 Kit 썽크는 C++ 예외를 잡아 `PT_E_INTERNAL`로 바꾼다. UE 빌드는 `PT_ABI_NO_EXCEPTIONS`(또는 `/EHs-c-`)로 try 없이 컴파일된다. `PtAbiUeCompatCheck`가 매 빌드 그 플래그로 Kit을 인스턴스화한다.
- SEH는 삼키지 않는다 (크래시 덤프를 남긴다). 호스트는 DLL을 로드한 직후 자신의 unhandled-exception filter와 error mode를 다시 설치한다.

### 5.13 ODR과 `PT_BACKEND_NS`

- Kit과 BackendCommon의 모든 정의는 `namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { ... } }` 안에 있다. 백엔드 타깃마다 `PT_BACKEND_NS`를 PRIVATE으로 다르게 정의한다 (Null, Jolt, Bullet, PhysX, UBT 브리지는 `ChaosBridge`). 없으면 `#error`다.
- 백엔드가 쓰는 다른 namespace와 같은 이름은 쓰지 않는다. `PT_BACKEND_NS=Chaos`이면 `PhysicsTest::Abi::Chaos`가 생기고, 브리지 TU가 `using namespace PhysicsTest::Abi;`를 쓰는 순간 모든 `Chaos::` 참조가 UE의 `::Chaos`와 모호해진다.
- 이유: /arch가 다른 백엔드(Jolt AVX2, 나머지 SSE2)의 같은 inline 함수가 한 COMDAT으로 합쳐지면 다른 백엔드가 AVX2 코드를 실행할 수 있다. 이름이 백엔드마다 달라지므로 링커가 합치지 않는다. CTest `PhysicsTest.Probe.KitNamespaces`가 링커 맵으로 확인한다.
- Kit 상태는 `TBackendApiProvider<TBackend>`의 static 멤버다. 백엔드 클래스마다 따로 있고 전역 inline 상태는 없다. 백엔드 객체는 정적 저장소에 만들고 파괴하지 않는다 (UE 모듈의 CRT detach 중 소멸자를 피한다).

### 5.14 effective 설정 (`DumpEffectiveSettings`)

- 한 줄에 JSON 객체 하나: `{"key", "native", "native_unit", "canonical"(숫자 또는 null), "unit", "source"}`. source는 READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT 중 하나다 (Kit `FTextWriter::AppendEffective`).
- 필수 키 = 일반 집합 24개 + 백엔드가 `GetInfo`의 `RequiredEffectiveKeys`로 선언한 집합 (A7). 일반 집합은 `Physics/EffectiveSettings.h`와 [02_Architecture.md](02_Architecture.md) §10에 있다. 개념이 없는 백엔드는 `native="n/a"`, COMPILE_TIME으로 낸다.

---

## 6. 호스트 측 (`Physics/PhysicsBackend.h`, `Physics/PhysicsInterface.h`)

| 타입 | 하는 일 |
|---|---|
| `FPhysicsBackendModule` | static 진입점 또는 `LoadLibraryExW(절대 경로, LOAD_WITH_ALTERED_SEARCH_PATH)` + `GetProcAddress(PT_GetBackendApi)`. 자기 major로 표를 받고 검사한다. 로드 직후 호스트의 unhandled-exception filter와 error mode를 되돌린다. 해제하지 않는다. `InitGamePhys`는 로그 콜백을 `LogPhysicsBackend`로 잇는다 |
| `FPhysicsBackendRegistry` | 생성된 목록(`GetBackendRegistryEntries`: `Null` static, `NullDll` = exe 옆 `PtBackend_Null.dll`)과 Chaos(`-ChaosDll=` 또는 `PT_CHAOS_DLL`, M2.5/M13). 프로세스 규칙: UE_RUNTIME 모듈은 하나, GLOBAL_SCENE_SETTINGS 모듈은 init 옵션 한 벌 |
| `FPhysicsBackendScene` | `PtSceneId` 하나의 프록시. `FBodyInstance` → `PtActorCreateInfo`/`PtShapeElem` 마샬링(채널 32 이상은 로드 오류), 프레임 4동사, 명령 큐(T_push), 미리 크기를 잡은 버퍼로 상태 pull(T_pull, 할당 없음) |
| `FPhysicsInterface` | UE `FChaosEngineInterface`의 `_AssumesLocked` 동사. 쓰기는 큐에 쌓고, 읽기는 큐를 비우고 전체 상태를 당긴 뒤 답한다. `bIsInternal`은 false만 받는다 |
| `FPhysScene` | 생성 시 레지스트리 → 모듈 → CreateScene, `FlushDeferredActors`가 한 배치로 생성, `Tick`이 두 driver 중 하나로 ABI를 구동한다 ([02_Architecture.md](02_Architecture.md) §9.1) |

---

## 7. Null 백엔드 (Chaos 작성자의 템플릿)

- `Source/Backends/NullBackend`: `NullBackend.cpp`(구현 + `PT_GetBackendApi_Null`), `NullBackendDll.cpp`(`PT_GetBackendApi` export → `PtBackend_Null.dll`). 하네스 lib를 링크하지 않는다 (PT::BackendCommon → PT::Abi만).
- double semi-implicit Euler를 Chaos `Integrate` 순서로 한다: v += a·dt → 임펄스 → v *= max(0, 1 − c·dt) → 속도 clamp → x += v·dt → q = normalize(q + dt/2·(w,0)·q). 충돌, 자동 sleep, gyroscopic 항은 없다.
- capability: KINEMATIC, NATIVE_FRAME_MODEL, ACTIVE_LIST, STATE_WRITE, NATIVE_HASH. module flag 없음. 필수 effective 키는 일반 집합만.
- `FNullFramePlanner`는 UE 소스를 보고 하네스 `FStepPlanner`와 따로 작성했다. A10이 두 구현을 대조한다.
- DLL은 C++ 라이브러리 코드를 쓰지 않는다 (배열은 malloc 기반). import는 VCRUNTIME140, VCRUNTIME140_1(예외 처리기), UCRT 전달자 5개, KERNEL32뿐이다 [V]. CTest `PhysicsTest.Probe.CheckModules`는 이 성질을 `--strict-crt`로 지킨다 (`msvcp140*`가 붙으면 실패). SDK 백엔드 DLL(M14 OOP 등)은 `--strict-crt` 없이 검사해 `msvcp140*`를 허용한다.
- 프레임 규칙(§5.10)과 명령 의미는 UE5 Chaos를 따른다: 힘은 push 프레임의 모든 스텝(평균 가중치 포함), 임펄스는 첫 스텝, kinematic 목표는 `PseudoFraction`, `AddForce`/`AddTorque`는 언제나 깨운다 (`PT_CMDF_NO_WAKE`이면 깨우지 않고 잠든 바디는 무시), 텔레포트는 언제나 깨우고 kinematic 바디의 목표·속도를 그 자리로 맞춘다. `PT_READ_ALL`은 해제한 바디도 UNINITIALIZED로 낸다. 해제 검사는 Kit이 한다.

Chaos 브리지가 Null에서 가져갈 것: 진입점 매크로, `ModuleName`, `GetInfo` 채우기, Init에서 옵션 거부, `CreateScene`에서 씬 옵션 거부, UE 순서의 `CreateActors`, `NumStepsTaken` 세기, `DumpEffectiveSettings`의 일반 키 24개.

---

## 8. 게이트 대응

| 게이트 | 확인 | 어디서 |
|---|---|---|
| A0 | GetInfo, Init/Term 상태기계, 이중 Init, Init 전 모듈 수준 진입점 → BAD_STATE와 `GetLastErrorMessage`, create/destroy ×10, 16 슬롯 | `PhysicsTestConformance/Private/Abi/AbiGates.cpp`, `PhysicsTest.Abi.A0Lifecycle` |
| A1 | major 0 → 표, major 1 → NULL, StructSize·AbiVersion, 호스트 표 검사 | `PhysicsTest.Abi.A1VersionHandshake` |
| A2 | 25개 진입점을 다른 스레드에서 → `PT_E_WRONG_THREAD`, 출력·상태·마지막 오류 불변 | `PhysicsTest.Abi.A2OtherThread` |
| A3 | phase 상태기계, 거부된 호출이 phase를 안 바꿈, Term이 진행 중 프레임의 씬을 파괴 | `PhysicsTest.Abi.A3PhaseStateMachine` |
| A4 | 표로 정의한 음성 사례 70개 (모듈 4 + 씬 66: 해제한 바디에 대한 명령·질량 읽기, 이중 해제, `[i, i]` 해제, `OutIndices` 없는 ACTIVE_ONLY 포함), 크래시 없음, 쌍둥이 씬과 비트 동일 | `PhysicsTest.Abi.A4NegativeCases` |
| A5 | 두 번 호출 프로토콜(상태, 텍스트, 오류), 해제 뒤 빽빽한 `PT_READ_ALL`과 해제한 바디를 빼는 ACTIVE_ONLY, 패딩 stride | `PhysicsTest.Abi.A5CapacityAndStride` |
| A6 | MXCSR 제어 bit, x87 제어 워드 보존 (FTZ+DAZ, 64-bit precision 아래 66 호출), Kit이 되돌린 일이 없음 (로그 경고 없음, `kit.mxcsr_restores` 불변) | `PhysicsTest.Abi.A6FloatingPointState` |
| A7 | 필수 effective 키, 줄 형식 | `PhysicsTest.Abi.A7EffectiveKeys` |
| A8 | loopback | M14 (열리지 않으면 M15) |
| A9 | export 하나, import는 CRT + KERNEL32 (Null은 `--strict-crt`), `PtAbiUeCompatCheck` 빌드 | CTest `PhysicsTest.Probe.CheckModules` + 빌드 |
| A10 | (a) Native `NumStepsTaken == FStepPlan::NumSteps`, (b) FixedStep·Sync의 두 driver 비트 동일: 고정 스크립트를 FixedStep, Sync, 스텝이 없는 프레임(1e-9, 0)이 든 Sync dt 목록으로, 그리고 무작위 명령 스크립트(힘·토크·임펄스·속도 설정과 `bAddToCurrent`·sleep/wake·텔레포트·kinematic 목표의 모든 진입점, 0과 1e-9가 든 무작위 dt) 6 seed, (c) FPhysScene Native driver의 스텝 총수, (d) Substepping·AsyncFixed에서 두 driver가 같은 UE5 프레임 규칙(힘, 임펄스, sleep/wake, kinematic)을 따라 허용 오차 안에서 같음 (고정 스크립트, 토크 없는 무작위 스크립트는 좁은 허용 오차, 토크가 든 무작위 스크립트는 §5.10의 토크 시점 차이만큼 넓은 허용 오차) | `AbiFrameModelGate.cpp`, `PhysicsTest.Abi.A10FrameModel` (M6에서 동결 조건으로 다시 돈다). 프레임 규칙의 단위 시험은 `PhysicsTest.Abi.NullNativeFrameRules`, `PhysicsTest.PhysScene.{ForceRulesPerDriver, KinematicTargetRulesPerDriver, KinematicTeleportThenTarget, ZeroStepFramesReachTheBackend, ForceAndSleepFollowIssueOrder}` |

모든 A 게이트는 static(`Null`)과 DLL(`NullDll`) 두 경로에서 돈다.

---

## 9. 잠정 항목과 동결 전에 바뀔 수 있는 것

**PROVISIONAL(M9)** (세 엔진의 조인트·스냅샷이 M9에 처음 쓴다)
- `CreateConstraints`, `PtConstraintCreateInfo`, 조인트 타입, `PT_JOINTF_*`
- `CaptureDebugSnapshot` / `ReadDebugSnapshot`, `PtParticleSnapshot`, `PtContactPoint`, `PtJointSnapshot`, `PT_SNAP_*`, `PT_CONTACT_*`
- 네이티브 드로 `PtDebugDrawCmd`, `PT_DD_*`
- capability: JOINT_BALL/HINGE, CONTACT_*, NATIVE_ISLANDS, BOUNDS, JOINT_SNAPSHOT, NATIVE_DEBUG_DRAW (`PT_CAP_PROVISIONAL_MASK`)

**M6 동결 전에 다시 볼 것 (핵심이지만 아직 v0.9)**
- Jolt(M3)·Bullet·PhysX(M5)가 이 경계를 처음 지나면서 필요한 필드·옵션 키·오류 코드. M2.5 스파이크 결과.
- `PT_E_VERSION`, `PT_E_TRANSPORT`는 아직 쓰는 곳이 없다.
- `PtSceneDesc`의 반복 횟수 필드의 쓰임 (M5 변형 키와 함께). 용량 힌트(`MaxBodiesHint`, `MaxBodyPairsHint`, `MaxContactConstraintsHint`)는 M2 수정 라운드 1에서 `FPhysSceneInitParams`로 배관했다 (Null은 바디 배열을 예약하고 `null.max_bodies_hint`로 낸다). Jolt `PhysicsSystem::Init`이 M3에서 처음 쓴다 (넘치면 G12 실패).
- `PT_MODULE_NO_REINIT` 백엔드(Chaos 브리지)의 A 게이트 실행 방식: 지금 A0~A7·A10은 게이트마다 모듈을 Init/Term하므로 그런 백엔드는 프로세스당 첫 게이트만 통과할 수 있다. 게이트당 프로세스 하나(벤치 `-Conformance` 생성)나 한 Init 주기를 공유하는 구조로 바꾼다 (M6/M13, [Progress.md](Progress.md) M2 §5).
- Chaos 브리지의 `NumStepsTaken` 세기 방법 (§5.8) [R].
- `PT_CMDF_ALLOW_SUBSTEPPING`은 정보용 bit로 남긴다. Native 백엔드는 무시하고, 호스트도 UE4 규칙의 Harness driver에서만 쓴다. M6 동결 때 지울지(bit 0을 예약으로) 정한다.
