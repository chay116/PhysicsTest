# ABI (C ABI v1.0, 핵심 동결판)

> **작성일:** 2026-10-21 (M2 초안 v0.9. M3: M2 검토 지적과 Jolt 백엔드. M5: Bullet·PhysX 백엔드, §5.13·§5.14·§6·§7.1·§8·§9. **M6: 핵심 v1.0 동결**, §2·§4·§5.8·§5.10·§5.14·§7.1·§8·§9. M6 수정 라운드 2(2026-10-22): §2 표 크기 규칙, §7, §7.1, §8, §9.1 결정 2·3·13)
> **상태:** **핵심(CORE) 동결 v1.0** (`PT_ABI_VERSION_MAJOR 1`, `PT_ABI_VERSION_MINOR 0`, 의도한 태그 `abi-v1.0`. 태그는 리드가 커밋과 함께 단다). `PROVISIONAL(M9)` 항목(조인트, 디버그 스냅샷, 네이티브 드로)은 M9 끝에서 확정한다 ([04_DecisionLog.md](04_DecisionLog.md) DL29). 동결 때 정한 계약은 §9.1에 모았다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §7 · [03_WorkPlan.md](03_WorkPlan.md) M2, M6 · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md) A.1, A.2, A.8, A.13 · [Progress.md](Progress.md) M2, M6 · [ChaosBackendGuide.md](ChaosBackendGuide.md)
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
| 현재 판 | **v1.0** (M6 동결): `PT_ABI_VERSION_MAJOR 1`, `PT_ABI_VERSION_MINOR 0`, `PT_ABI_VERSION = (major << 16) \| minor` = 0x10000. M6 전의 v0.9는 major 0이었다. `PtAbiC11Check.c`가 C11 컴파일러로 major 1, minor 0, `sizeof(PtFrameResult) == 16`을 매 빌드 확인한다 |
| 핸드셰이크 | 호스트는 **자신이 빌드된 major**로 부른다 (`PT_GetBackendApi(PT_ABI_VERSION_MAJOR)`). major 0으로 빌드한 모듈(M2.5 스파이크 DLL 등)은 major 1 요청에 NULL을 돌려주므로 로드되지 않는다: 이 헤더로 다시 빌드한다. major+1과 major 0(`Entry(0)`)은 NULL이다 (A1, M6에서 major 0 거부 검사를 더했다) |
| 표 검사 (호스트) | `StructSize >= PT_ABI_V1_0_API_SIZE`(v1.0 표 25개 = 216 B, 동결된 최소), `AbiVersion >> 16 == PT_ABI_VERSION_MAJOR`, v1.0의 25개 함수 포인터가 모두 NULL이 아님 (`FPhysicsBackendModule::ValidateApiTable`). **v1.x 호환 규칙 (M6 수정 라운드 2, §9.1 결정 13):** 표가 v1.0 크기 이상이면 받는다. 그래서 v1.x 호스트가 표 끝에 진입점을 더해도 v1.0으로 빌드한 모듈(사용자의 Chaos 브리지, M2.5 스파이크)이 계속 로드된다. 모듈의 `StructSize` 밖의 진입점은 없는 것(NULL)이고, 호스트는 더한 진입점을 `PT_API_HAS(Api, Entry)`로만 읽는다. NULL 검사는 v1.0의 25개에만 한다. 전에는 `StructSize < sizeof(PtBackendApi)`를 거부해 첫 append가 사실상 major 변경이었다 (A1이 v1.0 크기 표를 받는지, 그보다 작은 표를 거부하는지 확인한다) |
| 입력 구조체 | `StructSize`가 이 헤더의 `sizeof`보다 작으면 `PT_E_INVALID_ARG`. 더 큰 값(더 새 호스트)은 받고 모르는 꼬리는 무시한다 (`PtBackendInfo`, `PtInitParams`, `PtSceneDesc`, `PtActorBatch`, `PtStepStats`, `PtDebugSnapshotRequest`) |
| Reserved 필드 (M7, v1.0 규칙) | 입력 구조체의 `Reserved*` 필드(`PtInitParams.Reserved`, `PtSceneDesc.Reserved0`·`Reserved1`, `PtPhysicalMaterial.Reserved`, `PtShapeElem.Reserved`, `PtActorCreateInfo.Reserved0`, `PtBodyCommand.Reserved`)에 **호스트는 언제나 0을 쓰고 Kit은 0이 아니면 `PT_E_INVALID_ARG`로 거부한다** (§5.5, A4 사례 7개). 그래서 v1.x가 어떤 Reserved 필드에 뜻을 주더라도 그 필드를 모르는 모듈이 조용히 무시하는 일이 없다: v1.x 호스트는 그 뜻을 선언한 모듈(새 capability bit)에만 0이 아닌 값을 쓴다. 출력 구조체의 Reserved(`PtFrameResult.Reserved`, `PtMemoryStats.Reserved`, `PtBackendInfo.Reserved` 등)는 모듈이 0을 쓰고 호스트가 읽지 않는다. 이 검사는 M7의 Kit부터 있다: 그 전 Kit으로 빌드한 모듈(v1.0 태그의 Kit)은 0이 아닌 Reserved를 무시하므로, 위 capability 게이트 규칙이 두 경우를 함께 덮는다 (M6 검토 지적). **적합성(A4)은 다르다:** A4의 Reserved 사례 7개는 `PT_E_INVALID_ARG`를 기대하므로 v1.0 태그의 Kit으로 빌드한 모듈은 실행은 되지만 A4를 통과하지 못한다. 적합성 검사는 현재 Kit으로 다시 빌드한 모듈에 한다 (Chaos 브리지도 M13에서 현재 Kit으로 빌드한다, §8, M7 수정 라운드 0) |
| 출력 구조체 | `GetInfo`는 `StructSize`를 모듈이 아는 크기로 돌려 써서 호스트가 어느 필드까지 유효한지 알게 한다 |
| 배열 stride | `sizeof(T)` 이상이고 `alignof(T)`의 배수여야 한다. 더 긴 stride는 더 새 호스트의 확장 구조체로 보고 받는다 (A5) |
| 잠정 리비전 | `PT_ABI_PROVISIONAL_REVISION` (지금 1). 모듈이 다른 리비전으로 빌드되었으면 호스트는 `PT_CAP_PROVISIONAL_MASK`의 capability를 없는 것으로 본다 (`FBackendInfo::Capabilities`. 선언값은 `DeclaredCapabilities`) |
| 동결 | M6 끝(`abi-v1.0`)에서 major 1 / minor 0. 이제 v1.x는 append-only다: StructSize/stride 뒤 필드 추가, 표 끝 함수 추가(위 표 검사 규칙), 새 bit·enum만. StructSize도 stride도 없는 구조체(`PtFrameResult`, `PtBodyState`, `PtMassProperties`, `PtMemoryStats`)는 예약 필드만 쓸 수 있다. `PtBodyCommand`는 `ApplyBodyCommands(…, Stride)`로 넘기므로 `PtActorCreateInfo`처럼 stride 뒤 꼬리로 늘릴 수 있다 (M6 수정 라운드 2에서 목록을 고쳤다). 호환을 깨면 major를 올리고 Chaos를 다시 빌드한다. `PROVISIONAL(M9)` 항목의 변경은 `PT_ABI_PROVISIONAL_REVISION`을 올린다 (§9.2) |

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
| 14 | `EndFrame(Scene, OutResult)` | Ready | Waited → Idle | T_step | `PtFrameResult{NumStepsTaken, EngineErrorBits, NumPushedSteps, Reserved}` (v1.0, 16 B). Kit이 `NumPushedSteps = PT_INDEX_NONE`(보고 안 함), 나머지 0으로 미리 채운다. `OutResult`가 NULL이면 `PT_E_INVALID_ARG`이고 phase는 그대로다 | 핵심 |
| 15 | `ApplyBodyCommands(Scene, Cmds, Num, Stride)` | Ready | Idle | T_push | `*_AssumesLocked` 명령 배치(§5.10). 전체를 먼저 검증하고(공통 + 해제한 바디 + 백엔드별) 하나라도 틀리면 아무것도 적용하지 않는다 | 핵심 |
| 16 | `ReadBodyStates(Scene, ReadFlags, OutIndices, OutStates, Capacity, OutCount)` | Ready | Idle | T_pull | `PT_READ_ALL` 또는 `PT_READ_ACTIVE_ONLY`(§5.8). 두 번 호출 프로토콜. `PT_READ_ALL`은 빽빽하다: 만든 바디 전부를 인덱스 순서로, 해제한 바디는 ObjectState UNINITIALIZED에 0 자세(항등 회전)·0 속도로 낸다. 그래서 `OutIndices`는 NULL이어도 된다. NULL이 아니면 모듈이 쓴 상태마다 `OutIndices[i] = i`를 쓴다 (Kit이 성공한 `PT_READ_ALL` 뒤에 모든 Kit 백엔드에 대해 직접 쓴다, M3. 백엔드는 쓰지 않아도 된다). Kit은 개수 = 만든 바디 수를 확인하고, 아니면 `PT_E_INTERNAL`이다. `PT_READ_ACTIVE_ONLY`는 살아 있는 바디의 부분 집합이라 Capacity > 0이면 `OutIndices`가 필요하다 (NULL이면 `PT_E_INVALID_ARG`) | 핵심 |
| 17 | `ReadMassProperties(Scene, First, Num, Out)` | Ready | Idle | 비타이밍 | 엔진이 실제로 가진 질량·주관성·COM (G2 readback). 범위에 해제한 바디가 있으면 `PT_E_INVALID_ARG` | 핵심 |
| 18 | `GetStepStats(Scene, Out)` | Ready | Idle | 비타이밍 | 리스너가 필요 없는 카운터. 모르는 값은 -1 | 핵심 |
| 19 | `CaptureDebugSnapshot(Scene, Req, Out)` | Ready | Idle | 비타이밍 | 2단계 중 1단계(개수). Null은 `PT_E_UNSUPPORTED` | 잠정 |
| 20 | `ReadDebugSnapshot(Scene, Buffers)` | Ready | Idle | 비타이밍 | 2단계 중 2단계(복사) | 잠정 |
| 21 | `ComputeNativeStateHash(Scene, OutHash)` | Ready | Idle | 비타이밍 | 하네스 바디 순서의 FNV-1a. Null은 살아 있는 바디의 actor 이동·회전 double 7개 | 핵심 |
| 22 | `SetOption(SceneOrZero, Key, Value)` | Ready | Idle (씬) | - | 0이면 모듈 옵션. 모르는 키는 `PT_E_INVALID_ARG` | 핵심 |
| 23 | `DumpEffectiveSettings(SceneOrZero, OutJsonLines)` | Ready | Idle (씬) | 비타이밍 | JSON lines (§5.14). 두 번 측정해 결정적이어야 한다. 0이면 모듈 수준 키이고, 그 끝에 Kit이 `kit.mxcsr_restores`(§5.11)를 붙인다 | 핵심 |
| 24 | `GetMemoryStats(SceneOrZero, Out)` | Ready | Idle (씬) | 비타이밍 | 백엔드가 아는 만큼. 모르는 필드는 -1이다. Kit 썽크가 `LiveBytes`/`PeakBytes`/`NumAllocs`/`NumFrees`를 -1로 채워 넘기므로 백엔드는 아는 필드만 쓴다 (`GetStepStats`와 같다. 모듈 수준 Null·Jolt는 `AllocatorKind`만 쓰므로 나머지는 -1, M3 수정 라운드 1) (예: 할당 hook이 없는 백엔드의 `NumAllocs`/`NumFrees`. Jolt는 씬 temp 블록의 크기·peak만 내고, temp 블록의 heap fallback은 할당 횟수가 아니라 effective 키 `jolt.temp_allocator_fallbacks`와 오류 bit 0x100으로 낸다, M3 수정 라운드 0). `AllocatorKind` 0 = CRT malloc, 2 = 엔진 temp/arena. 모듈 수준(0)도 Ready에서만 된다 (Chaos는 InitGamePhys 전에 엔진이 없다). 벤치 manifest의 memory 그룹이 씬 값을 쓴다. **M7 수정 라운드 0:** 세 SDK 백엔드 모두 `NumAllocs`/`NumFrees`에 엔진 자기 할당자의 호출 수(프로세스 전체 누적)를 낸다: PhysX `PxAllocatorCallback`, Jolt는 어댑터가 `JPH::Allocate` 계열 함수 포인터에 다는 세는 래퍼, Bullet은 `btAlignedAllocSetCustom`으로 다는 세는 래퍼 (SDK의 공개 훅, SDK 수정 없음). 벤치는 루프 앞뒤의 차이를 `alloc.backend_allocs_*`에 쓴다 ([TimingContract.md](TimingContract.md) §4.1) | 핵심 |
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
| Reserved (M7) | 위 입력 구조체의 `Reserved*` 필드가 0이 아님 (§2 Reserved 필드 규칙. `PtInitParams`는 `InitGamePhys`, 나머지는 각 생성·명령 호출) |

- 비유한 값 검사는 비트로 한다. 하네스와 UE 게임 빌드는 `/fp:fast`라서 `isfinite`·NaN 비교가 접힐 수 있다 ([Progress.md](Progress.md) M0 §4).
- 해제 집합은 Kit이 들고 있다 (`IPhysicsBackendScene::IsReleased`, 바디당 1 bit, malloc). 그래서 해제한 바디에 대한 명령·질량 읽기, 중복·이중 해제는 모든 백엔드에서 같은 방식으로 거부된다 (M2 수정 라운드 1. 전에는 백엔드마다 다시 구현해야 했고 Jolt `RemoveBodies`는 중복 BodyID에서 assert한다). 그 밖의 백엔드별 검사(kinematic이 아닌 바디의 kinematic target, 모르는 옵션 키)는 백엔드가 한다. 명령 배치의 백엔드 검사(`IPhysicsBackendScene::ValidateBodyCommand`)도 적용 전에 배치 전체에 대해 돈다.
- 배치는 전부 아니면 전무다. A4는 거부된 호출 뒤 씬이 건드리지 않은 쌍둥이 씬과 다음 프레임까지 비트 단위로 같음을 확인한다.
- **원자성 규칙 (M3, M2 검토 지적):** Kit은 배치 전체를 검증(공통 → 해제한 바디 → `IPhysicsBackendScene::ValidateBodyCommand`)한 뒤 명령을 하나씩 적용한다. 그래서 명령을 거부할 수 있는 검사는 모두 `ValidateBodyCommand`에 두어야 하고, `ApplyBodyCommand`는 검증을 통과한 명령에 대해 실패하면 안 된다. 실패하면 Kit은 `PT_E_INTERNAL`과 "앞의 k개 명령은 이미 적용되었다"는 마지막 오류를 돌려준다 (그 배치는 더 이상 전부 아니면 전무가 아니며, 백엔드 버그다). Null과 Jolt의 `ApplyBodyCommand`는 언제나 `PT_OK`다.

### 5.6 핸들과 인덱스

- `PtSceneId = (generation << 32) | slot`, 0은 무효다. 슬롯은 `PT_KIT_MAX_SCENES`(16)개이고 파괴할 때 세대가 올라간다.
- 바디 인덱스는 생성 순서 = 삽입 순서 = 해시 순서다. 해제해도 재사용하지 않는다.
- 머티리얼 인덱스는 `CreateMaterials` 호출을 누적한 생성 순서다.

### 5.7 단위와 변환

- cm, kg, s, rad, Z-up, double. ABI 경계에서 단위가 바뀌지 않는다. 엔진 단위 변환은 백엔드 안 `PtUnitScale` 한 곳에서만 한다 (D1).
- 박스는 half extent다. 호스트가 `FKBoxElem`의 full extent를 반으로 나누는 곳은 `FPhysicsBackendScene::MarshalBodies` 하나다.
- 캡슐은 `Radius` + `HalfLength`(선분 절반 = `FKSphylElem::Length / 2`)이고 축은 로컬 Z다. 디버그 드로의 HalfHeight는 `HalfLength + Radius`다.
- 셰이프 치수와 로컬 변환에는 바디 스케일(`FBodyInstance::Scale3D`)이 UE `GetFinalScaled` 규칙으로 이미 들어가 있다. `FBodySetup::BuildScale3D`는 메타데이터라 곱하지 않는다.
- `ActorTransform`은 바디 원점의 강체 자세(스케일 없음), `CenterOfMassLocal`은 actor 공간의 COM 위치와 주축 회전이다. 선속도는 cm/s로, dynamic 바디는 COM의 속도, kinematic 바디는 actor 원점의 속도다 (아래 "선속도의 기준점", 02 §5.2 #48. M4에서 이 문장을 정정했다). 각속도는 rad/s다.
- 질량·관성·COM은 하네스가 계산해 넘긴다 (BodyUtils, D5). 엔진은 그 값을 쓴다.
- **`ReadMassProperties`의 주축 (M3 수정 라운드 0):** 엔진이 받은 관성 텐서를 다시 대각화하고 주축 순서를 자기 방식으로 정할 수 있다. Jolt는 주관성 모멘트를 큰 것부터 정렬한다 (MassProperties.cpp `DecomposePrincipalMomentsOfInertia` [V]). 그러면 정육면체가 아닌 바디에서 `InertiaDiagonal`은 하네스 대각의 순열이고 `CenterOfMassLocal.Rotation`도 그에 맞게 다르다. 비교할 값은 텐서 R·diag(I)·Rᵀ다 (`PhysicsTest.SdkBackend.MassReadbackIsATensor`: 200×50×20 cm 판의 하네스 대각 (12852, 179049, 188356), Jolt 대각 (188356, 179049, 12852), 텐서 상대 오차 1.0e-7). G2(M6)는 텐서로 비교하고, 벤치 `bodies_sampled`는 `inertia_tensor_rel_error`를 낸다.
- **선속도의 기준점 (M3, M2 검토 지적):** dynamic 바디의 `LinearVelocity`(상태 읽기, 초기 속도, 속도 명령)는 COM의 속도다. kinematic 바디가 읽기에서 내는 선속도는 **actor 원점**의 속도다. Chaos의 `ApplyKinematicTargets`가 목표로 옮길 때 파티클 X(actor 원점)의 변위로 V를 정하기 때문이다 (PBDRigidsEvolutionGBF.cpp:1180-1239 [V]). COM 오프셋이 있는 kinematic 바디가 회전하면 둘은 w × (R·c)만큼 다르다. Null은 원점 변위 / dt로, Jolt는 `MoveKinematic` 때의 원점 변위 / dt로 낸다 (Jolt 자신의 속도는 COM 기준이다). Chaos의 `GetWorldVelocityAtPoint`는 V를 COM 속도로 다루는데 (ChaosEngineInterface.cpp:722-735 [V]) kinematic 바디에서는 이것과 어긋난다. 하네스는 상태 읽기를 Chaos 파티클 V와 같게 맞춘다.
- **`MaxDepenetrationVelocity` (M3에서 UE 5.8.3 의미로 정정, M2 검토 지적):** 음수 = 설정 안 함(솔버 설정을 쓴다), 0 이상 = 그 바디의 초기 겹침 해소 속도(cm/s)다. 호스트는 UE5 `FBodyInstance::UpdateMaxDepenetrationVelocity`처럼 `bOverrideMaxDepenetrationVelocity`이면 바디 값, 아니면 **-1**을 보낸다 (BodyInstance.cpp:3713-3722 [V]). 프로젝트의 `MaxDepenetrationVelocity`(UPhysicsSettingsCore)는 PhysX 시절 키라 Chaos 경로에서 읽히지 않는다. Chaos에서 -1은 솔버의 `CollisionInitialOverlapDepenetrationVelocity`(기본 -1 = 사실상 무제한 1e10 cm/s)로 바뀌고 (PBDRigidsSolver.cpp:3668-3672, :3700, PBDCollisionConstraints.h:250-264 [V]), **0은 처음 겹친 셰이프를 밀어내지 않는다는 뜻이다** (PBDCollisionContainerSolver.cpp:180-219 [V]). 쌍의 값은 두 바디 중 큰 값이다 (PBDCollisionConstraint.cpp:341-344 [V]). M2 초안의 "0 = 제한 없음"은 UE4 PhysX의 의미였고 틀렸다. SDK 백엔드: 설정 안 함은 엔진 자체 동작이고 effective 키로 기록한다. 바디별 값이 없는 엔진은 0 이상의 값도 무시하고 effective 키로 그 사실을 기록한다 (Jolt: `jolt.max_depenetration_velocity` = n/a). PhysX UE4Compat는 설정 안 함을 UE4 프로젝트 규칙(0 → PX_MAX_F32)으로 바꾼다 (M5. 프로젝트 값을 넘길 통로는 M5에서 정한다).

### 5.8 프레임 모델

- **Harness driver (SDK 백엔드):** 호스트가 `FStepPlanner`로 스텝을 계획하고 스텝마다 `SetUpForFrame(&G, StepDt, 0, 0, 0, 1, false)` → `StartFrame` → `WaitPhysScenes` → `EndFrame`을 부른다. 씬은 `AsyncFixedTimeStepSize = -1`로 만든다 (async 누산기도 호스트가 돌린다). `NumStepsTaken`은 1이어야 한다.
- **Native driver (`PT_CAP_NATIVE_FRAME_MODEL`: Chaos, Null):** 호스트가 UE `SetUpForFrame` 인자를 프레임당 한 번 넘기고 백엔드가 UE5 규칙(`FChaosScene::SetUpForFrame`의 float clamp, `FPhysicsSolverBase::AdvanceAndDispatch_External`의 FReal 계획, 솔버 루프의 MinDeltaTime 처리)으로 스텝한다. AsyncFixed는 씬을 `AsyncFixedTimeStepSize > 0`으로 만들어 백엔드가 누산기를 돌린다.
- **`NumStepsTaken`의 정의:** 그 프레임에 엔진이 실제로 진행한 스텝 수다 (UE `FPBDRigidsSolver`의 AdvanceOneTimeStep 횟수). 하네스의 `FStepPlan::NumSteps`와 같아야 하며, 밀어 넣은 스텝 수 `FStepPlan::NumPushedSteps`가 아니다. 둘은 MinDeltaTime 이하의 스텝에서 갈라진다. 하네스는 다르면 Fatal로 멈춘다. Chaos 브리지는 솔버 스텝 콜백 횟수 같은 실제 진행 횟수를 세어야 한다 [R] (M13).
- **`NumPushedSteps` (v1.0):** `NATIVE_FRAME_MODEL` 백엔드가 그 프레임에 솔버로 밀어 넣은 스텝 수다 (UE `AdvanceAndDispatch_External`의 NumSteps). 호스트는 Native driver에서 `FStepPlan::NumPushedSteps`, Harness driver에서 1과 비교하고, 다르면 Fatal로 멈춘다 (`FPhysScene::RunBackendFrame`). 호스트는 계획이 스텝을 push하는 프레임에만 힘을 보내므로, Chaos `AsyncBlockMode`나 MinDeltaTime이 스텝을 버려 둘이 갈라지면(PhysicsSolverBase.cpp:628-640 [V]) 힘의 시점이 조용히 어긋난다. 이 검사가 그것을 잡는다. `PT_INDEX_NONE`(Kit 기본값)은 "보고하지 않음"이고 검사를 건너뛴다. SDK 백엔드는 보고하지 않아도 된다 (프레임당 한 스텝). Null은 자기 플래너의 값을 낸다 (A10이 `FStepPlan::NumPushedSteps`와 대조한다).
- **`EndFrame`은 `PtFrameResult`의 네 필드를 모두 쓴다** (`Reserved` = 0, 보고하지 않는 `NumPushedSteps` = `PT_INDEX_NONE`). 호스트(`FPhysScene::RunBackendFrame`)도 Kit처럼 호출 전에 `NumPushedSteps = PT_INDEX_NONE`을 채운다. 그래서 Kit을 쓰지 않고 M5의 두 필드만 쓰는 백엔드(M14 OOP 전송, 손으로 쓴 브리지)가 위 검사에서 0 push로 Fatal이 되지 않는다 (M6 수정 라운드 0).
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
| `SET_GLOBAL_POSE` | `Transform` = actor 자세 | 텔레포트. 잠든 dynamic 바디는 언제나 깨운다 (UE5 `SetX`, SingleParticlePhysicsProxy.h:240-251 [V]; `PT_CMDF_AUTO_WAKE`는 정보용, 아래). kinematic 바디는 그 자세가 목표가 되고 선·각속도가 바로 0이 된다 (ChaosEngineInterface.cpp:2550-2557 [V]) |
| `SET_KINEMATIC_TARGET` | `Transform` | kinematic 바디의 목표. 아래 프레임 규칙대로 스텝이 돌 때까지 남아 있고, 그 프레임의 마지막 목표가 이긴다 |
| `SET_LINEAR_VELOCITY` / `SET_ANGULAR_VELOCITY` | cm/s, rad/s | 즉시 설정. **v1.0: 잠든 dynamic 바디를 깨운다** (`PT_CMDF_AUTO_WAKE`와 무관, 정확한 0 벡터 포함). UE 5.8.3 `SetV`/`SetW`는 GT 프록시가 Sleeping이면 Dynamic으로 바꾸고(SingleParticlePhysicsProxy.h:322-340, :362-380 [V]. 정확한 0 벡터는 `SetX`/`SetR`가 깨운다, :1217-1244 [V]) 엔진 인터페이스는 `bAutoWake = true`만 지원한다 (`ensure(bAutoWake)`, ChaosEngineInterface.cpp:626-658 [V]). **예외 하나는 백엔드 정의이고, 백엔드가 effective 키로 선언한다 (M6 수정 라운드 1):** 0이 아니지만 nearly zero인 벡터(모든 성분 ≤ 1e-4)는 UE가 잠든 채 둔다 (`SetVBase`/`SetWBase`의 `!IsNearlyZero()`, :331 [V]). `command.nearly_zero_velocity_wakes`(§5.14) canonical 1 = 이때도 깨운다 (Null, Jolt, Bullet, PhysX), 0 = 잠든 채 둔다 (Chaos 브리지는 UE를 따라 0). G6가 (5e-5, 0, 0) cm/s와 (0, 0, 5e-5) rad/s 쓰기로 선언대로인지 확인한다. 그 밖은 모든 백엔드와 Null이 같다 (§9.1 결정 1) |
| `ADD_IMPULSE` | kg·cm/s | 누적했다가 다음 스텝 적분에 속도 변화로 들어간다 (PBDRigidsEvolutionGBF.cpp:920-924 [V]). push 때 속도를 바로 바꾸지 않는다: 같은 배치의 뒤 `SET_LINEAR_VELOCITY`·`SET_BODY_STATE`는 임펄스를 지우지 않고(스텝 결과 = 설정한 V + J/m), 스텝 전의 읽기는 임펄스 전 속도를 본다. 스텝 때 잠든 바디(임펄스 뒤 `PUT_TO_SLEEP`)는 깨어 있는 다음 스텝까지 임펄스를 든다 (Null. SDK 백엔드 규칙은 §7.1) |
| `ADD_VELOCITY` | cm/s | Chaos처럼 M·dv 임펄스다 (ChaosEngineInterface.cpp:861-864 [V]) |
| `ADD_ANGULAR_IMPULSE` / `ADD_ANGULAR_VELOCITY` | kg·cm²/s, rad/s | 각속도 변화 = I_world⁻¹·L. 각속도 명령은 I_world·dw 임펄스다 (:866-870 [V]) |
| `ADD_FORCE` / `ADD_TORQUE` | kg·cm/s², kg·cm²/s² | 아래 프레임 규칙대로 작용한다. `PT_CMDF_ACCEL_CHANGE`는 질량 무관(힘은 F·M, 토크는 I_world·τ, :940-944, :1029-1032 [V]). `PT_CMDF_ALLOW_SUBSTEPPING`은 정보일 뿐 Native 백엔드는 무시한다 (UE5가 `bAllowSubstepping`을 쓰지 않는다, :932-950, :1024-1037 [V]). flag가 없으면 UE의 `AddForce`/`AddTorque`다: 잠든 dynamic 바디는 벡터가 0이어도 깬다 (GT 프록시가 Sleeping이면 매번 Dynamic으로 바꾼다, SingleParticlePhysicsProxy.h:706-720, :757-771 [V]; `FChaosStateOps::AddForce`의 0 검사는 그 앞의 중복 검사다). **`PT_CMDF_NO_WAKE`**이면 깨우지 않고, 그때 잠든 바디는 이 명령을 무시한다 (아래 호스트 규칙). 호스트가 보내는 힘은 모두 이 flag를 단다 |
| `WAKE_UP` / `PUT_TO_SLEEP` | - | sleep을 끈 씬의 PUT_TO_SLEEP은 무시한다 (PhysX가 `eDISABLE_SLEEPING` 씬의 putToSleep을 거부하는 것과 같다). PUT_TO_SLEEP은 선·각속도를 0으로 둔다. 같은 push에서 그 앞에 온 힘은 스텝 때 바디가 잠들어 있으면 작용하지 않고 버려진다 (뒤에 깨우면 작용한다). 나중의 깨우기로 넘어가지 않는다 |
| `SET_BODY_STATE` | 자세 + 선·각속도 | 하네스 전용 전체 상태 쓰기 (`PT_CAP_STATE_WRITE`). **v1.0: 잠든 dynamic 바디를 깨운다** (텔레포트 + 속도 쓰기, UE `SetX` + `SetV`/`SetW`와 같다) |
| `PT_CMDF_AUTO_WAKE` (flag) | - | v1.0에서 **정보용**이다 (UE의 `bAutoWake` 인자를 기록할 뿐 동작을 바꾸지 않는다). 자세·속도 쓰기는 위처럼 언제나 깨운다. bit는 지우지 않는다 (append-only) |

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

- CMake 빌드(`/EHsc`)에서 Kit 썽크는 C++ 예외를 잡아 `PT_E_INTERNAL`로 바꾼다. UE 빌드는 `PT_ABI_NO_EXCEPTIONS`(또는 `/EHs-c-`)로 try 없이 컴파일된다. `PtAbiUeCompatCheck`가 매 빌드 그 플래그로 Kit을 인스턴스화한다. M3부터 UBT 기본값(BuildSettingsVersion V6 / Latest)인 `/we4668`(정의되지 않은 매크로를 `#if`에 쓰면 오류)도 붙인다. `PtAbiC11Check`도 같다.
- SEH는 삼키지 않는다 (크래시 덤프를 남긴다). 호스트는 DLL을 로드한 직후 자신의 unhandled-exception filter와 error mode를 다시 설치한다.

### 5.13 ODR과 `PT_BACKEND_NS`

- Kit과 BackendCommon의 모든 정의는 `namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { ... } }` 안에 있다. 백엔드 타깃마다 `PT_BACKEND_NS`를 PRIVATE으로 다르게 정의한다 (Null, Jolt, Bullet, PhysX, UBT 브리지는 `ChaosBridge`). 없으면 `#error`다.
- 백엔드가 쓰는 다른 namespace와 같은 이름은 쓰지 않는다. `PT_BACKEND_NS=Chaos`이면 `PhysicsTest::Abi::Chaos`가 생기고, 브리지 TU가 `using namespace PhysicsTest::Abi;`를 쓰는 순간 모든 `Chaos::` 참조가 UE의 `::Chaos`와 모호해진다.
- 이유: /arch가 다른 백엔드(Jolt AVX2, 나머지 SSE2)의 같은 inline 함수가 한 COMDAT으로 합쳐지면 다른 백엔드가 AVX2 코드를 실행할 수 있다. 이름이 백엔드마다 달라지므로 링커가 합치지 않는다. CTest `PhysicsTest.Probe.KitNamespaces`가 링커 맵으로 확인한다.
- **어댑터 코드 전체로 넓힌 규칙 (M3 수정 라운드 0):** namespace 밖의 템플릿과 CRT inline 함수도 같은 문제를 만든다. `/arch`가 다른 어댑터 TU가 하네스와 같은 `std::vector<uint32>`를 인스턴스화하면 링커가 그 AVX2 사본을 골라 하네스가 실행한다 (M3 검사: `vector<uint32>::push_back`, `vector<uint8>`, `vector<PtContactPoint>`, `vector<PtParticleSnapshot>`, `vector<PtPhysicalMaterial>`이 `PhysScene_Jolt.cpp.obj`에서 왔다). 그래서:
  - 어댑터의 컨테이너는 백엔드에만 있는 타입이다. Jolt는 `JPH::Array<T>`(`JPH::STLAllocator`), M5의 Bullet·PhysX는 그 SDK의 배열(`btAlignedObjectArray`, `PxArray`)이나 백엔드 고유 할당자를 쓴 `std::vector<T, F…Allocator<T>>`를 쓴다. 기본 할당자의 `std::` 컨테이너, `std::string`, `std::function`을 어댑터 상태에 두지 않는다.
  - `/arch`가 다른 어댑터 타깃은 `_NO_CRT_STDIO_INLINE`으로 컴파일하고 `legacy_stdio_definitions`를 링크한다 (`snprintf`·`vsnprintf`의 inline 정의를 어댑터 오브젝트에 만들지 않는다).
  - 잡 스레드 콜백(접촉 리스너)은 할당하지 않는다: 한 번 크기를 잡은 배열에 atomic 슬롯 예약으로 쓴다.
  - CTest `PhysicsTest.Probe.AdapterComdats`가 Automation·Bench 링커 맵에서 어댑터 오브젝트가 내놓은 함수를 모두 검사한다: 백엔드 namespace(`JoltBackend`, Kit의 `Jolt`), SDK namespace(`JPH`), lambda, 정적 진입점, 컴파일러가 만드는 sized-delete 도우미(`__global_delete` 등)만 허용한다. SDK 자신의 TU가 내놓는 namespace 밖 함수는 이름 목록으로만 허용한다 (Jolt: `steady_clock::now`, `this_thread::sleep_for`, `std::terminate`, `::abs(float)`. 부동소수점 연산이 없거나 정확한 연산이다. SDK는 패치하지 않는다, Q8). 음성 대조 `PhysicsTest.Probe.AdapterComdatsNegative`는 검사 결과의 `vector<uint32>::push_back` 줄을 넣은 맵에서 실패를 확인한다. dbg(`/Od`)는 SDK 헤더의 std 도우미(atomic, `shared_mutex`, `std::min<float>`, `fabsf` 등, 모두 정확한 연산)가 모두 밖으로 나오므로 보고만 한다 (어댑터 쪽 약 36개/맵). dbg는 측정 빌드가 아니다.
  - **M5:** /arch가 없는 Bullet·PhysX 어댑터에도 같은 규칙을 둔다 (그 SDK의 헤더와 정의를 보는 TU는 어댑터뿐이다). `PhysicsTest.Probe.AdapterComdatsBullet`(소유 = `BulletBackend`, Kit의 `Bullet`, Bullet 이름 `bt[A-Z]`. Bullet에는 namespace가 없다)과 `AdapterComdatsPhysX`(소유 = `physx`, `PhysXBackend`, Kit의 `PhysX`, 시험 훅 `PT_PhysXTest_*`). SDK 라이브러리 자신은 검사 목록에 넣지 않는다: PhysX TU는 전역 namespace 함수를 수백 개 내놓고(`CompanionPrunerAABBTree` 등), 두 SDK 모두 /arch가 하네스와 같아 COMDAT을 공유해도 다른 코드가 돌지 않는다. 어댑터 컨테이너는 `btAlignedObjectArray`, `physx::PxArray`다.
  - **M5 수정 라운드 0:** 두 어댑터 모두 `_NO_CRT_STDIO_INLINE` + `legacy_stdio_definitions`로 컴파일·링크한다 (전에는 Bullet만. /arch가 다른 프리셋 test-avx2, test-bullet/test-physx(M7/M10)가 오면 Jolt와 같은 공유 COMDAT 문제가 돌아온다). PhysX 어댑터 상태의 `std::atomic<int64>`/`std::atomic<uint32>`(`FPhysXAllocator`, `FPhysXErrorCallback`, PhysXSupport.h)는 백엔드 namespace 밖의 템플릿 COMDAT이다: 어댑터의 /arch가 하네스와 다른 프리셋에서는 `Probe.AdapterComdatsPhysX`로 다시 확인해야 한다 (지금 프리셋에서는 /arch가 같아 무해하다).
- Kit 상태는 `TBackendApiProvider<TBackend>`의 static 멤버다. 백엔드 클래스마다 따로 있고 전역 inline 상태는 없다. 백엔드 객체는 정적 저장소에 만들고 파괴하지 않는다 (UE 모듈의 CRT detach 중 소멸자를 피한다).

### 5.14 effective 설정 (`DumpEffectiveSettings`)

- 한 줄에 JSON 객체 하나: `{"key", "native", "native_unit", "canonical"(숫자 또는 null), "unit", "source"}`. source는 READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT 중 하나다 (Kit `FTextWriter::AppendEffective`).
- 필수 키 = 일반 집합 25개(M6 수정 라운드 1에서 `command.nearly_zero_velocity_wakes`를 더했다) + 백엔드가 `GetInfo`의 `RequiredEffectiveKeys`로 선언한 집합 (A7). 일반 집합은 `Physics/EffectiveSettings.h`와 [02_Architecture.md](02_Architecture.md) §10에 있다. 개념이 없는 백엔드는 `native="n/a"`, COMPILE_TIME으로 낸다.
- **READBACK은 엔진에서 읽은 값만이다.** 어댑터가 넘긴 입력(옵션 값)이나 어댑터 자신이 센 값을 그대로 내면 SET_ONLY다. G9(M6)가 READBACK 키를 리졸브한 값과 비교하므로, 입력을 READBACK으로 표시하면 그 검사가 저절로 통과한다 (M3 수정 라운드 1: Jolt `shape.convex_radius`는 SET_ONLY, `shape.box_margin`은 바디 셰이프의 `BoxShape::GetConvexRadius`, `threads.workers`는 `JobSystemThreadPool::GetMaxConcurrency() - 1`).
- **M5 수정 라운드 0 (세 SDK 백엔드와 Kit):** 읽을 수 있는 키는 READBACK으로 바꿨다: 머티리얼 결합 규칙(PhysX `PxMaterial::getFrictionCombineMode`/`getRestitutionCombineMode`를 호스트 값과 대조, Jolt `PhysicsSystem::GetCombineFriction`/`GetCombineRestitution`, Bullet 전역 콜백), PhysX `shape.wrapping`(actor의 셰이프·local pose·COM frame), `physx.live_scenes`(`PxPhysics::getNbScenes`), `physx.filter_shader_rules`(`PxScene::getFilterShaderData`), `jolt.capacity`(`PhysicsSystem::GetMaxBodies`), `bullet.debug_drawer`(`getDebugDrawer`), Bullet `listeners.installed`(`gContact*Callback`), Bullet `contact.speculative_distance`(매니폴드·셰이프의 `getContactBreakingThreshold`). 호스트 입력과 어댑터·Kit 카운터는 SET_ONLY로 바꿨다: `*.max_threads`, Jolt·Bullet `*.live_scenes`, `physx.initializations`, `jolt.asserts`, `kit.mxcsr_restores`, `*.start_asleep_skipped`, `physx.error_bits`/`bullet.engine_error_bits`/`jolt.update_error_bits`, `physx.allocator`, `physx.error_reports`, `physx.sim_event_callback`, `physx.dispatcher_max_reentrancy`, `jolt.temp_allocator_*`, `bullet.angular_motion_limit_hits`, `bullet.pool_heap_fallbacks`. `SdkBackend.ReadbackIsReadFromTheEngine`이 세 엔진에서 확인한다. A6는 `kit.mxcsr_restores`의 canonical 값만 읽으므로 그대로다.
- **M7 (벤치 preflight의 G9):** 벤치는 run마다 그 run 씬의 effective 덤프로 G9 비교를 한다 (`Gates::CheckEffectiveReadback`, [TimingContract.md](TimingContract.md) §6.1). M6의 규칙 가운데 상자 기하에서 나온 상수(Bullet·PhysX `contact.speculative_distance`, Bullet `shape.box_margin`, `physx.contact_offset_min/max`, Jolt `shape.box_margin`, Null `null.start_asleep_skipped`)에는 `"scene"`을 달아 그 기하의 씬에만 걸고, 다른 씬에서는 노트를 남기고 건너뛴다. 수정 라운드 0: `"scene"`은 목록이 될 수 있어 같은 상자의 성능 씬 넷(`M3.BoxPile`, `P1.SmallScene`, `P0.EmptyStep`, `M2b.Pyramid1240`)에 모두 걸리고, 씬마다 값이 다르면 `"by_scene"`으로 그 씬의 기대값을 준다 (Bullet `contact.speculative_distance`: M3·P1 1.732 cm, M2b 3.464 cm. P0에는 동적 바디가 없어 값이 없다). 바디 템플릿 값(`profile:linear_damping`, `profile:max_angular_velocity`, `profile:gyroscopic`, `profile:ccd`)과 비교하는 규칙은 씬이 바디 값을 스스로 정하는 경우(게이트·거동 씬)에 건너뛴다. 동적 바디가 없는 씬(`P0.EmptyStep`)에서 값이 없는 바디 집계 키도 건너뛴다. 규칙 쪽 변경: PhysX `threads.workers`와 `physx.cpu_dispatcher`는 `profile:dispatcher_workers`(T = 1이면 0, T ≥ 2면 `PxDefaultCpuDispatcherCreate(T)`의 T), Bullet `bullet.pools`는 변형의 `ManifoldPoolSize`가 있으면 그 값, 없으면 씬 용량이다. Jolt `sleep.enabled`는 동적 바디가 없으면 canonical null이다 (전에는 0).
- **M6 (G9, 동결 전 READBACK 정직성):** G9가 `M3.BoxPile` 1k의 READBACK 키를 하나도 빼지 않고 `Config/Golden/tolerances.json`의 규칙(리졸브된 프로필 값, 변형 옵션, init 옵션, 씬 용량, 사유를 단 상수, 사유를 단 `unchecked`)과 비교한다. 규칙이 없는 READBACK 키는 G9 실패다. 그래서 새 READBACK 키를 더하는 백엔드는 규칙도 함께 등록한다. 이번에 엔진에서 읽게 바꾼 값: PhysX `contact.speculative_distance`(씬 셰이프의 `getContactOffset` 최댓값 × 2. 두 셰이프의 contact offset 합 안에서 접촉을 만든다. 전에는 SET_ONLY 상수였다), PhysX `body.gyroscopic`(actor의 `eENABLE_GYROSCOPIC_FORCES`), Bullet `ReadMassProperties`의 COM 프레임(compound 자식 0의 transform에서 거꾸로 계산. 전에는 하네스 값). Bullet `sleep.enabled`는 dynamic 바디가 있을 때만 READBACK이다 (바디의 activation state에서 읽는다. 바디가 없으면 SET_ONLY).
- **규칙 키의 canonical (v1.0, M6 수정 라운드 0):** 백엔드가 엔진 객체마다 호스트 규칙과 대조하는 READBACK 키 - PhysX `contact.offset_rule`·`material.static_friction_rule`·`material.friction_combine`·`material.restitution_combine`(셰이프·PxMaterial마다), Jolt·Bullet `material.friction_combine`·`material.restitution_combine`(엔진이 든 결합 함수) - 의 canonical은 **규칙을 지키지 않는 엔진 객체 수**다 (unit `count`, 0 = 모두 지킴). G9가 `const:0`으로 비교한다. native 텍스트("1000 of 1000 shapes hold it")는 사람이 읽는 용도이고 게이트가 파싱하지 않는다. 전의 G9 `all_of_n` 텍스트 규칙은 문구가 바뀌면 깨지고 "0 of 0"을 통과시켰으며, Jolt·Bullet의 결합 키는 `unchecked`라 UE가 아닌 콜백도 통과했다. 새 규칙 키도 같은 방식으로 낸다.
- **`body.damping_formula` (v1.0):** canonical 값이 감쇠 공식 id다. 1 = UE 순서 v = (v + a·dt)·max(0, 1 − c·dt) (Chaos·Null·Jolt·PhysX, Bullet `DampingModel=UE`), 2 = 힘 적분 전에 감쇠 v = v·m 뒤 v += a·dt (Bullet `DampingModel=Converted`). G1b가 이 값으로 기준값을 고른다 (`Analytic::EDampingFormula`, `DampedFreeFall`). 다른 값은 G1b 실패다. 새 공식은 새 id로 더한다.
- **`command.nearly_zero_velocity_wakes` (v1.0, M6 수정 라운드 1):** §9.1 결정 1의 유일한 백엔드 정의 경우를 데이터로 선언한다. canonical 1 = nearly zero 속도 쓰기(모든 성분 ≤ 1e-4, UE `FVector::IsNearlyZero`)도 잠든 바디를 깨운다 (Null, Jolt, Bullet, PhysX), 0 = 잠든 채 둔다 (UE `SetVBase`/`SetWBase` SingleParticlePhysicsProxy.h:331 [V], Chaos 브리지). unit `bool`, COMPILE_TIME. G6가 이 값을 읽고 두 쓰기의 결과(깸: 다음 스텝에 중력으로 낙하 / 유지: Sleeping, 이동 ≤ `G6.asleep_dz_cm`)를 확인한다. 0·1 밖의 값이나 키가 없으면 G6 실패다 (A7도 필수 키로 본다). 동결 전에 더했다: 동결 뒤 새 필수 키는 v1.0 백엔드를 A7에서 깨뜨린다.
- **선택 키 `contact.point_pose` (M7, M6 검토 지적):** 디버그 스냅샷 접촉점(`PtContactPoint::PointOn0/1`)이 어느 자세 기준인지 선언한다. canonical 0 = 그 스텝을 시작한 자세(Jolt, Bullet, PhysX), 1 = 적분 뒤 예측 자세(Chaos: `Integrate` 뒤 narrow phase, §9.1 결정 3). COMPILE_TIME. G3는 점에서 자세를 추론하고, 키가 있으면 선언과 추론이 같아야 한다 (다르면 G3 실패). `contacts.ptcn`을 읽는 쪽(뷰어, M9 분석)은 이 키로 규약을 안다. **필수 키가 아니라 선택 키다:** 동결 뒤 일반 필수 집합에 키를 더하면 v1.0 모듈이 A7에서 깨진다. 선언하지 않은 백엔드는 G3 노트에 "not declared"로 남고 추론만 한다. Chaos 브리지는 1을 선언한다 ([ChaosBackendGuide.md](ChaosBackendGuide.md) §10).
- **선택 키 `errors.capacity_bits`:** 엔진 오류 bit(`PtFrameResult`/`PtStepStats::EngineErrorBits`) 가운데 "용량 초과"를 뜻하는 bit의 합 (canonical = 정수, COMPILE_TIME). 벤치는 이 bit만 있으면 종료 코드 5, 다른 bit가 있으면 3이다. 키가 없는 백엔드는 용량 bit가 없다 (모든 오류 bit가 3). Jolt는 0x107 (ManifoldCacheFull 1, BodyPairCacheFull 2, ContactConstraintsFull 4, temp allocator fallback 0x100). Bullet은 0x3 (manifold 풀 heap fallback 1, collision algorithm 풀 fallback 2, M5), PhysX는 0x10 (eOUT_OF_MEMORY. PhysX는 풀을 스스로 늘리므로 할당 실패만 용량 오류다, M5). 전에는 벤치가 백엔드 이름 "Jolt"로 0x107을 알았다 (M3 수정 라운드 1). **v1.0 결정:** 엔진 오류 bit의 뜻은 백엔드가 정하고(ABI 상수로 두지 않는다), 용량 bit는 이 키로 알린다. G12는 bit가 0인지만 본다.

---

## 6. 호스트 측 (`Physics/PhysicsBackend.h`, `Physics/PhysicsInterface.h`)

| 타입 | 하는 일 |
|---|---|
| `FPhysicsBackendModule` | static 진입점 또는 `LoadLibraryExW(절대 경로, LOAD_WITH_ALTERED_SEARCH_PATH)` + `GetProcAddress(PT_GetBackendApi)`. 자기 major로 표를 받고 검사한다. 로드 직후 호스트의 unhandled-exception filter와 error mode를 되돌린다. 해제하지 않는다. `InitGamePhys`는 로그 콜백을 `LogPhysicsBackend`로 잇는다 |
| `FPhysicsBackendRegistry` | 생성된 목록(`GetBackendRegistryEntries`: `Null` static, `Jolt` static(M3, `PT_WITH_JOLT`), `Bullet`·`PhysX` static(M5, `PT_WITH_BULLET`/`PT_WITH_PHYSX` + 타깃 속성 `PT_IN_BACKEND_REGISTRY`), `NullDll` = exe 옆 `PtBackend_Null.dll`)과 Chaos(`-ChaosDll=` 또는 `PT_CHAOS_DLL`, M2.5/M13). 프로세스 규칙: UE_RUNTIME 모듈은 하나, GLOBAL_SCENE_SETTINGS 모듈은 init 옵션 한 벌. init 옵션은 키·값 문자열로 비교하므로 변형 ini는 백엔드 기본값과 같은 init 값을 적지 않는다 (M5: PhysX UE4Compat 계열, `Bullet.Default`). 이미 초기화된 모듈을 다른 `MaxThreads`로 요청하면 첫 초기화를 쓰고 경고한다 (M3). `TermIdleModules`(M4)는 살아 있는 씬이 없는 초기화된 모듈을 모두 내린다 |
| `FPhysicsBackendScene` | `PtSceneId` 하나의 프록시. `FBodyInstance` → `PtActorCreateInfo`/`PtShapeElem` 마샬링(채널 32 이상은 로드 오류), 프레임 4동사, 명령 큐(T_push), 미리 크기를 잡은 버퍼로 상태 pull(T_pull, 할당 없음). 디버그 스냅샷(M5): `BodyIndex`가 이 씬의 바디 수 이상(PT_INDEX_NONE 포함)인 파티클은 백엔드 버그로 보고 버리며 씬당 한 번 Error 로그를 남긴다 (`FPhysicsDebugSnapshot::NumBodies`, `NumRejectedParticles`). 소비자는 `GetBodyIndexBound`로 배열 크기를 정한다 |
| `FPhysicsInterface` | UE `FChaosEngineInterface`의 `_AssumesLocked` 동사. 쓰기는 큐에 쌓고, 읽기는 큐를 비우고 전체 상태를 당긴 뒤 답한다. `bIsInternal`은 false만 받는다 |
| `FPhysScene` | 생성 시 레지스트리 → 모듈 → CreateScene, `FlushDeferredActors`가 한 배치로 생성, `Tick`이 두 driver 중 하나로 ABI를 구동한다 ([02_Architecture.md](02_Architecture.md) §9.1). `FPhysicsInterface` 전용 내부 훅(`TeleportKinematic_AssumesLocked`, `MarkDynamicsClean/Dirty_AssumesLocked`)은 M3부터 private이고 `friend class FPhysicsInterface`만 부른다. 아직 flush하지 않은 바디를 `TermBody`하면 대기 목록에서 빠지고(`RemoveBodyInstanceFromPendingLists_AssumesLocked`), 같은 바디를 두 번 `InitBody`해도 actor는 하나다 (M3). 실행 해시 `ComputeDescSha256` / `ComputeOptionsSha256`과 디버그 스냅샷 읽기 `CaptureDebugSnapshot`(M3) |

---

## 7. Null 백엔드 (Chaos 작성자의 템플릿)

- `Source/Backends/NullBackend`: `NullBackend.cpp`(구현 + `PT_GetBackendApi_Null`), `NullBackendDll.cpp`(`PT_GetBackendApi` export → `PtBackend_Null.dll`). 하네스 lib를 링크하지 않는다 (PT::BackendCommon → PT::Abi만).
- double semi-implicit Euler를 Chaos `Integrate` 순서로 한다: v += a·dt → 임펄스 → v *= max(0, 1 − c·dt) → 속도 clamp → x += v·dt → q = normalize(q + dt/2·(w,0)·q). 충돌, 자동 sleep, gyroscopic 항은 없다.
- capability: KINEMATIC, NATIVE_FRAME_MODEL, ACTIVE_LIST, STATE_WRITE, NATIVE_HASH. module flag 없음 (단 DLL 빌드 `PtBackend_Null.dll`은 환경 변수 `PT_NULL_DLL_NO_REINIT=1`이면 `PT_MODULE_NO_REINIT`를 선언하는 시험 대역이 된다, §8·§9.1 결정 7). 필수 effective 키는 일반 집합만.
- `FNullFramePlanner`는 UE 소스를 보고 하네스 `FStepPlanner`와 따로 작성했다. A10이 두 구현을 대조한다.
- DLL은 C++ 라이브러리 코드를 쓰지 않는다 (배열은 malloc 기반). import는 VCRUNTIME140, VCRUNTIME140_1(예외 처리기), UCRT 전달자 5개, KERNEL32뿐이다 [V]. CTest `PhysicsTest.Probe.CheckModules`는 이 성질을 `--strict-crt`로 지킨다 (`msvcp140*`가 붙으면 실패). SDK 백엔드 DLL(M14 OOP 등)은 `--strict-crt` 없이 검사해 `msvcp140*`를 허용한다.
- 프레임 규칙(§5.10)과 명령 의미는 UE5 Chaos를 따른다: 힘은 push 프레임의 모든 스텝(평균 가중치 포함), 임펄스는 첫 스텝, kinematic 목표는 `PseudoFraction`, `AddForce`/`AddTorque`는 언제나 깨운다 (`PT_CMDF_NO_WAKE`이면 깨우지 않고 잠든 바디는 무시), 텔레포트는 언제나 깨우고 kinematic 바디의 목표·속도를 그 자리로 맞춘다. v1.0부터 속도 쓰기와 `SET_BODY_STATE`도 언제나 깨운다 (M5까지 Null은 잠든 채 속도를 유지했다). `EndFrame`은 `NumPushedSteps`에 자기 플래너의 push 수를 낸다. `PT_READ_ALL`은 해제한 바디도 UNINITIALIZED로 낸다. 해제 검사는 Kit이 한다.

Chaos 브리지가 Null에서 가져갈 것: 진입점 매크로, `ModuleName`, `GetInfo` 채우기, Init에서 옵션 거부, `CreateScene`에서 씬 옵션 거부, UE 순서의 `CreateActors`, `NumStepsTaken` 세기, `DumpEffectiveSettings`의 일반 키 25개.

### 7.1 SDK 백엔드 (Jolt M3, Bullet·PhysX M5) - Harness driver 쪽 계약

SDK 백엔드는 `NATIVE_FRAME_MODEL`이 없으므로 호스트가 스텝마다 백엔드 프레임 하나를 돌린다 (§5.8). Jolt(`Source/Backends/JoltBackend`)가 처음으로 이 경로를 지났고, M5의 Bullet(`Source/Backends/BulletBackend`)·PhysX(`Source/Backends/PhysXBackend`)가 같은 규칙을 따른다. 아래 표는 Jolt 구현을 적고, M5 두 백엔드가 다른 곳은 표 아래 "M5"에 적는다.

| 항목 | 규칙 (Jolt 구현) |
|---|---|
| 프레임 | `SetUpForFrame`은 dt를 저장하고 중력이 바뀔 때만 엔진에 넣는다 (DL32). `StartFrame`이 대기 중인 kinematic 목표를 적용한 뒤 엔진을 한 스텝 진행한다. `EndFrame`은 `NumStepsTaken = 1`과 엔진 오류 bit를 낸다 |
| kinematic 목표 | `SET_KINEMATIC_TARGET`은 다음 스텝에 적용한다 (Jolt `MoveKinematic(target, dt)`). 목표를 받지 못한 스텝에는 직전 스텝에 움직인 kinematic 바디의 속도를 0으로 둔다 (Chaos `EKinematicTargetMode::Reset`, PBDRigidsEvolutionGBF.cpp:1191-1197 [V]). 텔레포트(`SET_GLOBAL_POSE`)는 kinematic 바디의 속도를 바로 0으로 둔다 |
| `PT_CMDF_NO_WAKE` | 잠든 바디면 그 힘·토크를 무시하고, 아니면 깨우지 않고 더한다 (Jolt `BodyInterface::AddForce`는 깨우므로 `Body::AddForce`를 직접 쓴다) |
| sleep | 씬이 sleep을 끄면 바디마다 엔진의 sleep 스위치를 끈다 (Jolt `mAllowSleeping=false`, DL48). 그 씬의 `PUT_TO_SLEEP`은 무시하고, `!PT_BODY_START_AWAKE`의 5단계는 건너뛰어 센다 |
| `PT_READ_ACTIVE_ONLY` | 엔진의 활성 목록 + 이전 읽기에서 활성이었다가 빠진 바디(잠든 바디) + 명령으로 상태가 바뀐 바디를 중복 없이 낸다. 읽을 때 할당하지 않는다 (미리 잡은 배열) |
| 임펄스·속도 변화 | `ADD_IMPULSE`/`ADD_VELOCITY`/`ADD_ANGULAR_IMPULSE`/`ADD_ANGULAR_VELOCITY`는 **스텝으로 미루고 push 때 엔진에 넣지 않는다.** 백엔드가 바디별 속도 변화(각임펄스는 push 시점의 I_world⁻¹로 바꾼 값)를 쌓아 두고 `StartFrame`에서 kinematic 목표 뒤, 엔진 스텝 바로 전에 엔진 속도에 더한다. 그때 잠든 바디는 깨어 있는 다음 스텝까지 든다 (Null과 같다). 엔진의 즉시 임펄스 API(Jolt `Body::AddImpulse`, Body.inl:133-154 [V]; Bullet `btRigidBody::applyCentralImpulse`, btRigidBody.h:319-321 [V]. 둘 다 속도를 바로 바꾼다)를 push에서 부르면 같은 배치의 뒤 `SET_LINEAR_VELOCITY`가 임펄스를 지우고, 스텝 전 읽기가 임펄스를 보고, 뒤의 `PUT_TO_SLEEP`이 임펄스를 지운다 (M3 수정 라운드 1, `SdkBackend.CommandsMatchNull` 바디 6~8). 쌓는 목록은 최대 바디 수로 미리 잡아 T_push에서 할당하지 않는다 |
| 힘·토크 | `ADD_FORCE`/`ADD_TORQUE`도 바디별로 쌓아 두고 `StartFrame`에서 그때 깨어 있는 바디에만 엔진에 넘긴다 (Jolt `Body::AddForce`/`AddTorque`). 목록은 스텝마다 비운다. 그래서 같은 push의 `PUT_TO_SLEEP` 앞에 온 힘이 엔진 누산기에 남아 나중에 깨운 뒤의 첫 스텝에 작용하는 일이 없다 (Jolt는 활성 바디의 `mForce`만 스텝 뒤에 지운다, PhysicsSystem.cpp:2541 [V]; `SdkBackend.CommandsMatchNull` 바디 9·10) |
| 용량 | `PtSceneDesc`의 용량 힌트로 엔진 풀을 잡는다 (Jolt `PhysicsSystem::Init`, temp allocator). 넘치면 엔진 오류 bit를 낸다 (Jolt `EPhysicsUpdateError` 1/2/4, temp allocator heap fallback 0x100). 그 bit의 합을 effective 키 `errors.capacity_bits`로 알린다 (§5.14). 바디 수를 넘는 `CreateActors`는 `PT_E_CAPACITY`다 |
| Prepare | `PrepareForSimulation`은 비타이밍 기회다 (02 §9.5): Jolt는 broad phase를 최적화하고 temp 블록을 4 KiB 페이지마다 한 번 써서 미리 page fault를 낸다 (M3 수정 라운드 1. 전에는 블록의 첫 사용이 충돌 초기 프레임의 T_step 안에서 page fault를 냈다) |
| 리스너 | 접촉 리스너는 viz run 또는 `PT_INSTR_CONTACTS`에서만 붙인다. bench run에는 없다 (`listeners.installed` = 0) |
| 스냅샷 | `CaptureDebugSnapshot`이 스냅샷을 준비하고 개수를 내며, `ReadDebugSnapshot`이 그 내용을 복사한다. 접촉은 Chaos 규약으로 바꾼다 (`CanonicalizeContact`, Jolt는 normal과 depth 부호를 뒤집는다). 임펄스를 얻을 수 없으면 NaN이다. **Jolt (M4에서 고침):** `Phi`는 점마다 `−(p1 − p2)·n`이다 (Jolt 솔버가 점마다 쓰는 분리 거리 `(p2 − p1)·n`, ContactConstraintManager.cpp:1906 [V]. M3는 모든 점에 매니폴드의 `mPenetrationDepth`, 곧 가장 깊은 점의 값을 줬다). `PtContactPoint::Shape0/Shape1`은 매니폴드 SubShapeID의 하네스 요소 인덱스다 (데코레이터는 ID를 그대로 넘기고, compound는 `CompoundShape::GetSubShapeIndexFromID`. `MakeBodyShape`가 자식을 요소 순서로 넣고 `StaticCompoundShape`가 순서를 유지한다, StaticCompoundShape.cpp:193-199 [V]). Jolt는 잠든 바디의 충돌 검사를 하지 않아 리스너가 아무것도 받지 못한다. 그래서 씬이 스텝마다 잠든 쌍의 접촉을 잠든 스텝의 것으로 유지해, 그 스텝의 플래그(Phi ≤ 0이면 `ACTIVE_APPROX`, 아니면 `SPECULATIVE`)에 `PT_CONTACT_SLEEPING`(bit 5, M4)을 더해 낸다 (깨면 버리고 리스너가 새로 받는다). 명령과의 관계(M4 수정 라운드 2, `FlushKeptContactCommands`): 스텝 사이의 `PUT_TO_SLEEP`으로 잠든 바디는 마지막 스텝의 리스너 접촉을 유지한다 (그 스텝에 잠든 것과 같다). `SET_GLOBAL_POSE`(static 바디는 DontActivate)나 `SET_BODY_STATE`가 바디의 자세를 정하면 그 바디의 유지 쌍은 점 위치가 맞지 않으므로 버리고, Jolt가 쌍을 다시 알릴 때까지(다음 스텝) 그 쌍은 없다 (M4~M5에서는 `SET_BODY_STATE`가 DontActivate라 잠든 채 텔레포트된 상자가 섬 없이 그려졌다. v1.0에서는 dynamic 바디를 깨운다, §5.10). 명령으로 깬 바디의 유지 쌍도 버린다. 정리는 다음 `StartFrame`(리스너를 비우기 전)에 하고, 그 사이의 스냅샷은 깼거나 자세가 바뀐 바디의 유지 쌍을 빼고 낸다. Chaos도 잠든 제약의 마지막 솔버 결과를 그대로 두므로(PBDCollisionContainerSolver.cpp:373-409, PBDCollisionConstraint.cpp:538-545 [V]) 쉬는 잠든 접촉은 활성으로 그려진다 (ChaosDebugDraw.cpp:1453 [V]). 리스너 배열과 유지 배열은 4 × 접촉 제약 용량으로 한 번 잡고, 넘치는 점은 버리고 스냅샷을 truncated로 표시한다. 스냅샷이 없는 백엔드(Null)는 뷰어가 호스트 상태로 파티클을 만든다 (`DebugDraw::CaptureSnapshot`, `bFromHostState`) |
| 머티리얼 | 엔진이 바디당 머티리얼 하나만 가지면 첫 요소의 머티리얼을 쓴다. **Jolt의 한계:** `BodyCreationSettings::mFriction/mRestitution`이 바디당 하나라, 요소마다 머티리얼이 다른 바디는 모든 요소가 첫 요소의 마찰·반발을 쓴다. 결합 콜백은 SubShapeID를 받으므로 M9에서 요소별 머티리얼을 읽게 바꾼다. M3 씬은 바디마다 요소가 하나다 |
| COM 래핑 (Jolt) | 하네스 COM과 엔진 셰이프 COM의 차이를 float 해상도로 판정한다: 허용치 = max(1e-6 cm, 16 ulp × 바디 크기) (`GComToleranceUlps`). 1e-6 cm를 m float로 비교하면 float 해상도 아래라, compound는 COM이 같아도 언제나 `OffsetCenterOfMassShape`를 받았다 (M3 수정 라운드 0). 네이티브 프리미티브 판정(`CanUseNativePrimitive`)은 그대로 double 1e-6 cm다 (DL42) |

**M5: Bullet·PhysX가 위 표와 다른 곳** ([02_Architecture.md](02_Architecture.md) §8.2·§8.4 M5 메모, [Progress.md](Progress.md) M5 §3)

| 항목 | Bullet | PhysX |
|---|---|---|
| 프레임 | `StartFrame`이 쌓은 명령과 kinematic 목표(world transform)를 적용한 뒤 `stepSimulation(dt, 0, dt)`. 엔진 안의 pre-tick이 UE 순서 감쇠, 속도 한계, kinematic 속도(double)를 넣는다 | `StartFrame` = 명령 적용 + `simulate(dt, nullptr, scratch, 262144)`, `WaitPhysScenes` = `fetchResults(true)` |
| 중력 | 바뀔 때 잠든 dynamic 바디까지 직접 바꾼다 (`setGravity`는 깨어 있는 바디만 바꾼다) | `setGravity` |
| 잠든 바디의 속도 (`SET_LINEAR_VELOCITY`/`SET_ANGULAR_VELOCITY`, `SET_BODY_STATE`) | **v1.0: 깨운다** (§5.10, §9.1 결정 1). 속도를 쓴 뒤 바디를 깨운다 (어댑터의 `Wake`). 잠든 채 두면 `btDiscreteDynamicsWorld::updateActivationState`가 `ISLAND_SLEEPING` 바디의 속도를 스텝마다 0으로 둔다 (btDiscreteDynamicsWorld.cpp:605-620 [V]). M5에서는 이 속도를 잃었다 | **v1.0: 깨운다.** `setLinearVelocity(v, false)` 뒤 잠들어 있으면 `wakeUp()`. 0이 아닌 속도는 autowake = false여도 이미 깨웠고 (NpRigidDynamic.cpp:253, :286, :605-622 [V]), 0 벡터는 깨우지 않았다 (M5) |
| (위 칸의 확인) | `SdkBackend.VelocityOfASleepingBody`: Null과 세 SDK 백엔드의 기본 변형(Jolt.Default, Bullet.Default, PhysX.UE4Compat)이 두 명령과 0 벡터에서 깨우고, 다음 스텝에 그 속도로 움직인다 (M6). 모든 엔진·변형(PhysX 4 변형)과 DLL 백엔드는 G6(UEParity)가 확인한다: `SET_LINEAR_VELOCITY` (100, 0, 0)·0 벡터, `SET_ANGULAR_VELOCITY`, `SET_BODY_STATE`(STATE_WRITE), 모두 `PT_CMDF_AUTO_WAKE` 없이 (M6 수정 라운드 0). M5의 세 동작(Null·Jolt 유지, Bullet 잃음, PhysX 0이 아니면 깨움)은 없어졌다 | 같은 시험과 G6 |
| 자세 명령과 접촉 | `SET_GLOBAL_POSE`/`SET_BODY_STATE`는 그 바디의 매니폴드를 버린다 (`cleanProxyFromPairs`). v1.0: 명령으로 깬 바디(`bWokenByCommand`)가 든 잠든 쌍은 다음 스텝까지 스냅샷에서 뺀다 | Jolt와 같은 규칙이다 (`FlushKeptContactCommands`: `PUT_TO_SLEEP`으로 잠든 바디는 마지막 스텝 점을 유지하고, 자세를 정한 바디와 명령으로 깬 바디의 유지 쌍은 버린다) |
| 접촉 flag (v1.0, §9.1 결정 2) | ACTIVE(솔버 임펄스 > 0, 정확) / Phi > 0이면 SPECULATIVE / 그 밖은 셋 다 없음. M5에서는 ACTIVE와 SPECULATIVE가 함께 붙을 수 있었다. **한계 (M6 수정 라운드 2):** ACTIVE와 `NormalImpulse`는 `getAppliedImpulse()`다. split impulse의 push(침투가 `SplitImpulsePenetrationThreshold` −4 cm보다 깊을 때)는 매니폴드 점에 다시 쓰이지 않으므로(`writeBackContacts`는 `m_appliedImpulse`만 쓰고 push는 `m_rhsPenetration`/`m_appliedPushImpulse`에 남는다, btSequentialImpulseConstraintSolver.cpp:963-974, :1765-1773 [V]) push로만 바로잡힌 깊은 점은 ACTIVE도 임펄스도 없다. "법선 임펄스 또는 push-out"이라는 v1.0 정의와 다른 점이고, S04(1 cm 잠김)는 이 경우를 만들지 않는다. M9에서 다시 본다 | ACTIVE(점별 normal impulse > 0) / Phi > 0이면 SPECULATIVE / 그 밖은 셋 다 없음 |
| sleep | Normalized `DISABLE_DEACTIVATION`(kinematic 포함). UEParity는 Bullet 기준(바디별 80 cm/s·1 rad/s, 2 s)이고 UE 임계 매핑은 M9 | Normalized `eDISABLE_SLEEPING`. UEParity는 UE4 규칙(PhysX 기본 임계 × SleepFamily) |
| 오류 bit | 0x1 manifold 풀 fallback, 0x2 algorithm 풀 fallback (`errors.capacity_bits` 0x3) | 새 PhysX 보고의 PxErrorCode 값(0x2·0x4·0x8·0x10·0x20·0x40) + 0x100(simulate/fetchResults 거부). 카운터는 모듈 전역 (`errors.capacity_bits` 0x10) |
| 리스너 | 없음. viz / `PT_INSTR_CONTACTS`에서 캡처할 때 디스패처의 매니폴드를 읽는다 (`listeners.installed` 0) | `FPhysXSimEventCallback`(viz / `PT_INSTR_CONTACTS`, `listeners.installed` 1) |
| 스냅샷 접촉 | 매니폴드 점이 이미 Chaos 부호다 (PointOn0 = `m_positionWorldOnA`, PointOn1 = `m_positionWorldOnB`, Phi = `getDistance()`. 두 점 모두 그 셰이프 표면 위다). `NormalImpulse`·`FrictionImpulse`는 실제 값(`PT_CAP_CONTACT_NORMAL_IMPULSE`/`FRICTION_IMPULSE`), ACTIVE는 정확(임펄스 > 0). 잠든 쌍의 매니폴드는 그대로 남으므로 마지막 점 + `PT_CONTACT_SLEEPING` (장부 없음). `NativeIslandId` = `getIslandTag()`(`PT_CAP_NATIVE_ISLANDS`) | `PxContactPairPoint`의 normal을 그대로 쓴다: 두 번째 셰이프에서 첫 번째 셰이프를 향하므로(PxSimulationEventCallback.h:448 [V]) Body1 → Body0, 곧 Chaos 규약이다. Phi = separation. **v1.0 (§9.1 결정 3):** PointOn1은 Body1 셰이프 표면 위의 점이다: 어댑터가 스텝 전 자세(`StartFrame`이 저장한 `PreStepPoses`)의 Body1 셰이프에 `PxGeometryQuery::pointDistance`로 `position + n·(abs(sep) + 2·contactOffset + 0.01)`의 가장 가까운 점을 구한다. PointOn0 = PointOn1 + Phi·n. 보고 순서(`PairHeader.actors[0/1]`)와 무관하다. M5의 "PointOn1 = position"은 틀렸다: PhysX `position`은 두 셰이프 사이의 한 점이고(캡슐-박스에서 1 cm 벗어났다 [V]) 어느 표면 위도 아니다. 표면을 찾지 못하면 position을 쓰고 세어 둔다 (`NumSurfaceFallbacks`, G3에서 0) (M5 수정 라운드 0 문서 정정: normal은 반전하지 않는다). ACTIVE는 점별 normal impulse > 0 (`PT_CAP_CONTACT_ACTIVE_EXACT`), 임펄스 값은 M9까지 NaN. 섬이 잠드는 스텝에 보고가 없어 리스너가 있을 때(viz 또는 `PT_INSTR_CONTACTS`) 직전 스텝 점을 유지해 `PT_CONTACT_SLEEPING`으로 낸다. 직전 스텝의 각 점은 유지 목록(`PUT_TO_SLEEP`으로 잠든 바디의 쌍)과 직전 스텝 목록 중 한 곳에만 간다 (M5 수정 라운드 0: 전에는 명령으로 재운 바디의 점이 두 번 나왔다, `SdkBackend.PutToSleepKeepsEachContactOnce`) |
| Prepare | 없음 | scratch 블록 pre-fault |
| 용량 | 풀 크기 = 접촉·쌍 힌트 | `PxSceneLimits` 힌트 (넘으면 PhysX가 늘린다) |

---

## 8. 게이트 대응

| 게이트 | 확인 | 어디서 |
|---|---|---|
| A0 | GetInfo, Init/Term 상태기계, 이중 Init, Init 전 모듈 수준 진입점 → BAD_STATE와 `GetLastErrorMessage`, create/destroy ×10, 16 슬롯 | `PhysicsTestConformance/Private/Abi/AbiGates.cpp`, `PhysicsTest.Abi.A0Lifecycle` |
| A1 | 자기 major(v1.0: 1) → 표, major + 1 → NULL, major 0(v0.9 요청) → NULL (M6), StructSize(v1.0 크기 이상)·AbiVersion(1.0), 호스트 표 검사 (major + 1로 고친 표 거부, v1.0 크기 표·더 큰 표는 받고 더 작은 표는 거부, `PT_API_HAS`, M6 수정 라운드 2) | `PhysicsTest.Abi.A1VersionHandshake` |
| A2 | 25개 진입점을 다른 스레드에서 → `PT_E_WRONG_THREAD`, 출력·상태·마지막 오류 불변 | `PhysicsTest.Abi.A2OtherThread` |
| A3 | phase 상태기계, 거부된 호출이 phase를 안 바꿈, Term이 진행 중 프레임의 씬을 파괴 | `PhysicsTest.Abi.A3PhaseStateMachine` |
| A4 | 표로 정의한 음성 사례 70개 (모듈 4 + 씬 66: 해제한 바디에 대한 명령·질량 읽기, 이중 해제, `[i, i]` 해제, `OutIndices` 없는 ACTIVE_ONLY 포함), 크래시 없음, 쌍둥이 씬과 비트 동일. M7에 입력 Reserved 필드 사례 7개를 더했다: 이 사례들은 M7 이후의 Kit으로 빌드한 모듈에서만 통과한다 (§2 Reserved 행) | `PhysicsTest.Abi.A4NegativeCases` |
| A5 | 두 번 호출 프로토콜(상태, 텍스트, 오류), 해제 뒤 빽빽한 `PT_READ_ALL`과 해제한 바디를 빼는 ACTIVE_ONLY, 패딩 stride | `PhysicsTest.Abi.A5CapacityAndStride` |
| A6 | MXCSR 제어 bit, x87 제어 워드 보존 (FTZ+DAZ, 64-bit precision 아래 66 호출), Kit이 되돌린 일이 없음 (로그 경고 없음, `kit.mxcsr_restores` 불변) | `PhysicsTest.Abi.A6FloatingPointState` |
| A7 | 필수 effective 키, 줄 형식 | `PhysicsTest.Abi.A7EffectiveKeys` |
| A8 | loopback | M14 (열리지 않으면 M15) |
| A9 | export 하나, import는 CRT + KERNEL32 (Null은 `--strict-crt`), `PtAbiUeCompatCheck` 빌드 | CTest `PhysicsTest.Probe.CheckModules` + 빌드 |
| A10 | (a) Native `NumStepsTaken == FStepPlan::NumSteps`, (b) FixedStep·Sync의 두 driver 비트 동일: 고정 스크립트를 FixedStep, Sync, 스텝이 없는 프레임(1e-9, 0)이 든 Sync dt 목록으로, 그리고 무작위 명령 스크립트(힘·토크·임펄스·속도 설정과 `bAddToCurrent`·sleep/wake·텔레포트·kinematic 목표의 모든 진입점, 0과 1e-9가 든 무작위 dt) 6 seed, (c) FPhysScene Native driver의 스텝 총수, (d) Substepping·AsyncFixed에서 두 driver가 같은 UE5 프레임 규칙(힘, 임펄스, sleep/wake, kinematic)을 따라 허용 오차 안에서 같음 (고정 스크립트, 토크 없는 무작위 스크립트는 좁은 허용 오차, 토크가 든 무작위 스크립트는 §5.10의 토크 시점 차이만큼 넓은 허용 오차) | `AbiFrameModelGate.cpp`, `PhysicsTest.Abi.A10FrameModel` (M6에서 동결 조건으로 다시 돈다). M6 수정 라운드 1부터 모듈을 **한 번** 초기화한다 (레지스트리 모듈로, FPhysScene 기본 init 인자). (a)~(d)가 모두 그 초기화 위에서 돌아 `PT_MODULE_NO_REINIT` 모듈도 한 프로세스에서 A10을 통과한다 (전에는 (a)가 원시 표로 Init·Term한 뒤 (b)~(d)의 FPhysScene이 다시 Init했다). 프레임 규칙의 단위 시험은 `PhysicsTest.Abi.NullNativeFrameRules`, `PhysicsTest.PhysScene.{ForceRulesPerDriver, KinematicTargetRulesPerDriver, KinematicTeleportThenTarget, ZeroStepFramesReachTheBackend, ForceAndSleepFollowIssueOrder}` |

**물리 게이트 (M6, [02_Architecture.md](02_Architecture.md) §14.3, [Progress.md](Progress.md) M6)**

| 게이트 | 확인 (ABI로) | 어디서 |
|---|---|---|
| G1 | `S00.FreeFall` z60·v60 (dt_engine 기준) | `PhysicsTestConformance/Private/Gates/PhysicsGates.cpp`, `PhysicsTest.Gates.G1` |
| G1b | `S00d.FreeFallDamped` z60이 `body.damping_formula`(§5.14)가 선언한 공식의 기준값과 1e-3 cm | `PhysicsTest.Gates.G1b` |
| G2 | `S01`·`S02` 속도 변화, `ReadMassProperties`(질량·관성 텐서·COM 1e-5 cm), `S03` 캡슐 높이 | `PhysicsTest.Gates.G2` |
| G3 | `S04` step 1 스냅샷 접촉: normal·(+Z) > 0.999, Phi < 0, PointOn0/1이 각 셰이프 표면 위(PointOn0은 narrow phase 자세 기준: 스텝 시작 자세 또는 자유 운동 예측 자세 X0 + (v0 + g·dt)·dt 중 씬 전체에 맞는 하나, M6 수정 라운드 2), flag 규칙(셋 중 많아야 하나, SPECULATIVE는 Phi > 0만, `ACTIVE_EXACT` 백엔드는 ACTIVE만 쓰고 쌍마다 ACTIVE 점이 있음, 유한한 임펄스가 있으면 임펄스 > eps ⇒ ACTIVE, ACTIVE ⇒ 임펄스 ≥ −eps, 아니면 침투 점마다 ACTIVE_APPROX). UEParity: 명령으로 재운 상자의 유지(SLEEPING) 점이 `WAKE_UP` 뒤 다음 스텝 전까지 빠지고 스텝 뒤 새로 나온다 (§9.1 결정 2·3) | `PhysicsTest.Gates.G3` |
| G4 | `S05` 반발 e(법선 속도 비)를 엔진의 등록된 반발 속도 모델(`G4.restitution_velocity_model`: 0 = 접촉 스텝 전 속도, 1 = 그 스텝의 중력을 더한 솔버 전 속도)의 예측과 ±0.02로, 다른 모델보다 가까워야 한다. apex 비는 접촉 스텝 뒤 높이에서 시작한 이산 비행과 비교. `S05` v2는 step 38 끝이 바닥 위 0.01 cm다 (예측 자세로 충돌하는 Chaos가 반올림으로 한 스텝 일찍 튀지 않게, M6 수정 라운드 2) | `PhysicsTest.Gates.G4` |
| G5 | `S06` 정지 거리, 30° 경사 유지(가속도, 변위, 위치로 잰 creep 속도 frame 10-20·110-120)·미끄러짐 | `PhysicsTest.Gates.G5` |
| G6 | Normalized 600 프레임 sleep 0, UEParity `S08.StartAsleep`, 그리고 60 프레임 잠든 바디에 `PT_CMDF_AUTO_WAKE` 없는 속도 쓰기 (100, 0, 0)·0 벡터·각속도·`SET_BODY_STATE`가 깨우고 다음 스텝이 그 속도로 돈다. nearly zero 쓰기 (5e-5, 0, 0) cm/s·(0, 0, 5e-5) rad/s는 `command.nearly_zero_velocity_wakes`(§5.14)가 선언한 대로 깨우거나 잠든 채 둔다 (§9.1 결정 1) | `PhysicsTest.Gates.G6` |
| G7 | `S07` 각속도 한계와 스텝 회전각 | `PhysicsTest.Gates.G7` |
| G9 | `M3.BoxPile` 1k READBACK 키 전부 = 리졸브된 값 (§5.14), `shape.wrapping` 0 | `PhysicsTest.Gates.G9` |
| G12 | 모든 게이트 씬과 BoxPile 1k 120 프레임의 엔진 오류 bit 0. 혼자 고르면(`-Gates=G12`) 앞의 게이트를 모두 함께 돌리고 G12 보고만 낸다. 시험 전용 `-InjectEngineErrorBits`(벤치 `-Conformance`)는 모든 게이트 프레임의 오류 bit에 OR되어 G12가 실패하는지 보인다 (M6 수정 라운드 1) | `PhysicsTest.Gates.G12`, `PhysicsTest.Bench.ConformanceG12Injected` |

- 대상: Null, Jolt.Default, Bullet.Default, PhysX.{UE4Compat, UE4Compat-TGS, UE4Compat-Stab, SdkDefault} × UEParity·Normalized. `GateTests.cpp`가 대상 목록을 빌드가 구성한 SDK 백엔드(`PT_GATE_SDK_BACKENDS`)와 03의 변형 목록에 맞춰 확인한다 (세 SDK면 14 대상. 백엔드나 03 변형 파일이 빠지면 실패. 03 목록 밖의 변형 파일(M9의 새 변형)은 `GetOptInGateVariants`에 넣기 전까지 "not gated"로 보고만 하고 게이트하지 않는다, M6 수정 라운드 1). 허용치와 엔진별 등록 편차는 `Config/Golden/tolerances.json`(사유 필수, `PhysicsTest.Gates.ToleranceFileIsComplete`)이다. 실행에서 찾아 메커니즘을 확인한 뒤 등록한 값은 `"registered": "post-run"`이고 `"sign_off"`(리드가 받아들이기 전에는 `"pending"`, 그 뒤 누가 언제)가 있어야 로드된다. 게이트 노트에 둘 다 나온다 (M6 수정 라운드 1).
- **Skip 규칙 (M6 수정 라운드 0):** capability가 없어 게이트(또는 G2의 S03)를 Skip하는 것은 `tolerances.json`의 `gates.<Gate>.allowed_skip`에 그 엔진이 사유와 함께 있을 때만 통과다 (지금은 Null·NullDll의 G2 S03, G3, G4, G5). 등록되지 않은 Skip은 `RunPhysicsGates`가 Fail로 바꾼다 (`PhysicsTest.Gates.SelectionRules`). 그래서 `PT_CAP_CONTACT_POINTS`를 선언하지 않는 Chaos DLL(접촉 캡처가 없는 Test DLL)이나 잠정 리비전이 달라 호스트가 `PT_CAP_CONTACT_*`를 없는 것으로 보는 DLL(§2)은 G3에서 실패한다.
- `PhysicsTestBench -Conformance -Backend=X [-BackendProfile=V] -Profile=P [-Gates=...]`는 같은 게이트를 그 프로세스의 실제 백엔드 코드로 돌려 `conformance.json`(schema `physicstest.conformance/1`: 대상, exe SHA-256, ABI 버전, tolerances·프로필·변형 SHA-256, 게이트 씬 `scene_sha256`, 게이트별 결과와 노트)을 쓴다. `-Gates=A0`처럼 ABI 게이트 하나(A0~A7, A10)도 돌릴 수 있다: `PT_MODULE_NO_REINIT` 백엔드(Chaos)는 프로세스당 ABI 게이트 하나, 또는 물리 게이트 전부로 이 경로를 쓴다 (§9.1 결정 7). 그런 모듈에 ABI 게이트 둘 이상이나 ABI·물리 게이트를 섞은 목록은 벤치가 게이트를 돌리기 전에 거부한다 (종료 코드 2). CTest `PhysicsTest.Bench.Conformance`가 위 조합 16개와 ABI 게이트 4개(Null A0·A7·A10, NullDll A3), **NO_REINIT 시험 대역**(`PT_NULL_DLL_NO_REINIT=1`인 `PtBackend_Null.dll`: `PT_MODULE_NO_REINIT`를 선언하고 Term 뒤 Init을 Kit이 거부한다)의 A0~A7·A10 각각과 물리 게이트 한 번, 그 대역이 섞인 목록 두 개를 거부하는지를 fresh process로 돌고, `-List`와 Null·모든 SDK 항목의 `-Describe` stdout이 JSON 하나인지 확인한다 (세 SDK면 40 run·검사, M6 수정 라운드 1).

모든 A 게이트는 static(`Null`)과 DLL(`NullDll`) 두 경로에서 돈다. M3부터 레지스트리의 SDK 백엔드도 static 경로로 A0~A7을 돈다 (`PhysicsTest.Abi.SdkBackendsA0toA7`: M3 Jolt, M5 Bullet·PhysX. 세 엔진 모두 A0 156, A1 24(M6: `PT_GetBackendApi(0)` 검사로 20, 수정 라운드 2: v1.0 크기 표 규칙과 `PT_API_HAS`로 24), A2 50, A3 62, A4 153, A5 124, A6 136, A7 8 checks). A2는 M3부터 거부된 호출의 모든 출력 버퍼를 바이트 단위로 비교한다 (상태 16개, 질량, 텍스트, 통계, 스냅샷 개수, 메모리, 인덱스 16개).

---

## 9. 동결 결정과 잠정 항목

### 9.1 v1.0 동결 때 정한 계약 (M6)

M5까지 "동결 전에 다시 볼 것"이던 항목을 이렇게 정했다. 모든 SDK 백엔드와 Null이 같은 계약을 따르고, 공유 시험과 G 게이트가 지킨다.

| # | 항목 | v1.0 계약 | 근거와 확인 [V] |
|---|---|---|---|
| 1 | 잠든 바디에 쓴 속도 (M5 O-1) | `SET_LINEAR_VELOCITY`/`SET_ANGULAR_VELOCITY`/`SET_BODY_STATE`는 잠든 dynamic 바디를 **깨운다** (정확한 0 벡터 포함). `PT_CMDF_AUTO_WAKE`는 정보용이다. `SET_GLOBAL_POSE`도 깨운다 (M2부터). **백엔드 정의 예외 하나:** 0이 아니지만 nearly zero인 속도(모든 성분 ≤ 1e-4)는 UE처럼 잠든 채 둬도 된다 (Chaos 브리지). Null과 SDK 백엔드는 깨운다. 어느 쪽인지 effective 키 `command.nearly_zero_velocity_wakes`(§5.14, 1 = 깸, 0 = 유지)로 선언하고 G6가 확인한다 (M6 수정 라운드 1) | UE 5.8.3 GT `SetV`/`SetW`/`SetX`가 Sleeping 프록시를 Dynamic으로 바꾸고 (SingleParticlePhysicsProxy.h:240-251, :322-340, :362-380, :1217-1244), `SetVBase`/`SetWBase`는 nearly zero 벡터에서 깨우지 않는다 (:331 `!InV.IsNearlyZero()`), 엔진 인터페이스가 `bAutoWake = false`를 지원하지 않는다 (ChaosEngineInterface.cpp:626-658 `ensure(bAutoWake)`). M5의 세 동작 중 "유지"(Null·Jolt)는 Chaos와 다르고, "잃음"(Bullet)은 결과가 틀린다. `SdkBackend.VelocityOfASleepingBody`(Null + 세 SDK 백엔드의 기본 변형, 두 명령, 0 벡터), **G6**(UEParity, 모든 엔진·변형과 DLL 백엔드: Chaos도 이 게이트로 확인된다. nearly zero 쓰기 두 개는 선언한 키대로) |
| 2 | 스냅샷 접촉 flag | ACTIVE / ACTIVE_APPROX / SPECULATIVE는 **많아야 하나**. ACTIVE = 지난 스텝에 솔버가 그 점에 작용한 점 - 법선 임펄스 또는 push-out (Chaos `bIsActive`: NetPushOut이나 NetImpulse가 nearly zero가 아님, ChaosDebugDraw.cpp:1453 [V]) - 이고 `PT_CAP_CONTACT_ACTIVE_EXACT` 백엔드(Bullet, PhysX, Chaos)만 쓴다. ACTIVE_APPROX = 정확한 솔버 결과가 없는 백엔드의 Phi ≤ 0 (Jolt), SPECULATIVE = Phi > 0이고 앞의 둘이 아님. 정확한 백엔드의 Phi ≤ 0, 작용 0 점은 셋 다 없다. 명령으로 깬 바디가 든 잠든 쌍의 유지(SLEEPING) 점은 다음 스텝까지 뺀다. **ACTIVE와 `NormalImpulse`는 한 방향으로만 묶인다 (M6 수정 라운드 2):** `NormalImpulse`(`PT_CAP_CONTACT_NORMAL_IMPULSE`, PROVISIONAL(M9))는 그 점에 솔버가 마지막 스텝에 준 법선 임펄스(kg·cm/s)이고 위치 보정도 임펄스로 넣는다 (Chaos: (NetImpulse + NetPushOut / Dt)·n. 솔버의 clamp `MinImpulseNormal = −NetPushOut / Dt` 때문에 0 이상이다). 임펄스 > eps이면 ACTIVE, ACTIVE이면 임펄스 ≥ −eps (eps = `G3.normal_impulse_eps` 0.05 kg·cm/s). ACTIVE인데 임펄스가 0인 점은 허용한다: Chaos `bIsActive`는 마찰만 있거나 push-out의 속도를 velocity solve가 지운 점도 센다 (PBDCollisionSolver.h:1178-1196 [V]). Bullet은 split impulse push를 점에 쓰지 않는 한계가 있다 (§7.1) | M5 Bullet은 ACTIVE와 SPECULATIVE를 함께 붙였다. 헤더 주석, `SdkBackend.ContactPointsOnSurfaces`(flag 배타성), `SdkBackend.WakeDropsKeptContacts`, **G3**(step 1 점마다 위 규칙, `ACTIVE_EXACT`면 쌍마다 ACTIVE 점, UEParity에서 깬 상자의 유지 점 제외. M6 수정 라운드 0에서 게이트에 넣었다: 전에는 문서가 G3를 근거로 들었지만 G3가 flag를 보지 않았다). 수정 라운드 2: 전의 G3는 정확한 임펄스가 있으면 ACTIVE == (임펄스 > 0)을 요구해 위 정의대로 ACTIVE = `bIsActive`를 내는 Chaos 브리지가 실패했을 것이다 (독립 검사). 이제 한 방향 검사다 (변이 시험: ACTIVE 점의 임펄스 0 → 통과, −1 → 실패, ACTIVE 없는 임펄스 5 → 실패) |
| 3 | `PointOn0`/`PointOn1` | PointOn0은 Body0 셰이프 표면, PointOn1은 Body1(평면 주인) 셰이프 표면 위의 점이고 그 스텝의 narrow phase 자세 기준이다. (PointOn0 − PointOn1)·n = Phi. 보고 순서와 무관하다. **narrow phase 자세는 엔진마다 다르다 (M6 수정 라운드 2):** Jolt·Bullet·PhysX는 스텝 시작 자세, Chaos는 `Integrate` 뒤의 예측 자세 P = X + V·dt, Q다 (PBDRigidsEvolutionGBF.cpp:581 `Integrate`가 :650 `RunNarrowPhase`보다 먼저, ShapeInstance.cpp:460-462·:548-556 `GetLeafWorldTransform` = `GetTransformPQ` [V]). G3는 둘 다 받는다: 스텝 시작 자세와 자유 운동 예측 자세 X0 + (v0 + g·dt)·dt 중 씬의 모든 바디에 맞는 하나를 골라 그 자세로 표면 검사를 한다 | PhysX `PxContactPairPoint::position`은 표면 위 점이 아니다 (M5 "PointOn1 = position"은 캡슐-박스에서 1 cm 어긋났다). 어댑터가 Body1 셰이프에 투영한다 (§7.1). Jolt·Bullet은 매니폴드의 두 점 그대로. `SdkBackend.ContactPointsOnSurfaces`(먼저 만든 static 박스 위 구, 300 cm 박스 위 누운 캡슐, static 구 위 박스: SDF ≤ 0.05 cm), G3 (점 오차 ≤ 3.7e-4 cm). 수정 라운드 2: 전의 G3는 스텝 시작 자세만 받아 Chaos의 점(S04 step 1에서 g·dt² = 0.272 cm 아래)을 언제나 실패시켰을 것이다 (독립 검사). 변이 시험: 점을 예측 자세로 옮기면 "predicted pose"로 통과, 두 자세 사이(0.136 cm)로 옮기면 실패 |
| 4 | `PtFrameResult` | 16 B `{NumStepsTaken, EngineErrorBits, NumPushedSteps, Reserved}` (§5.8). StructSize가 없으므로 v1.x가 쓸 수 있는 자리는 `Reserved` 하나뿐이다. `ReadBodyStates`·`ReadMassProperties`도 stride가 없어 `PtBodyState`(Flags bit만)·`PtMassProperties`(여분 없음)는 v1.x에서 늘릴 수 없다 (새 읽기 진입점은 stride와 함께 append할 수 있다). **태그 전 리드 확인 항목:** 이대로 받아들일지, `PtFrameResult`를 24 B(`Reserved[3]`: AsyncBlockMode의 버린 스텝 수, Chaos substep·solver 수 등의 자리)로 넓힐지 ([Progress.md](Progress.md) M6 §5) | M2 검토 지적 (Native 백엔드의 push 스텝 수 확인). `Abi.A10FrameModel`(Null static·DLL: `NumPushedSteps == FStepPlan::NumPushedSteps`), `PtAbiC11Check` |
| 5 | 엔진 오류 bit | 뜻은 백엔드가 정한다. 용량 bit는 effective 키 `errors.capacity_bits`로 알린다 (§5.14). 0 = 오류 없음 | G12가 bit 0을 요구한다. ABI 상수로 두면 Chaos 오류 분류를 미리 정해야 한다 |
| 6 | `PT_CMDF_ALLOW_SUBSTEPPING` | 정보용 bit로 **남긴다** (지우지 않는다). Native 백엔드는 무시하고, 호스트가 UE4 규칙의 Harness driver에서만 쓴다 | UE5가 `bAllowSubstepping`을 쓰지 않는다 (ChaosEngineInterface.cpp:932-950, :1024-1037). 지우면 bit 재사용이 생겨 append-only와 맞지 않는다 |
| 7 | `PT_MODULE_NO_REINIT` 백엔드의 A 게이트 | 게이트당 프로세스 하나: `PhysicsTestBench -Conformance -Backend=X -Gates=A0`(A0~A7, A10 각각). 각 A 게이트는 모듈을 한 번 Init하고 한 번 Term한다 (A10도: M6 수정 라운드 1에서 고쳤다). 물리 게이트는 다른 프로세스 하나에서 한 번의 Init으로 돈다. 그런 모듈에 ABI 게이트 둘 이상이나 ABI·물리 게이트를 섞은 `-Gates=` 목록은 벤치가 거부한다 (종료 코드 2). 거부된 InitGamePhys(잘못된 인자)는 부작용이 없어야 한다 (A1, A4가 한 프로세스에서 거부된 Init 뒤에 유효한 Init을 한다) | M6 첫 판은 A10이 원시 표로 Init·Term한 뒤 FPhysScene으로 다시 Init해 NO_REINIT 모듈에서 돌 수 없었고, 이 경로를 NO_REINIT 모듈로 돈 적이 없었다 (독립 검사). 수정 라운드 1: `PhysicsTest.Bench.Conformance`가 NO_REINIT 시험 대역(`PT_NULL_DLL_NO_REINIT=1`인 NullDll)으로 A0~A7·A10 각각, 물리 게이트, 거부되는 목록 둘을 돈다 (A0 "PT_MODULE_NO_REINIT set", A10 "one InitGamePhys for (a)-(d)" 노트 확인). Chaos 절차는 [ChaosBackendGuide.md](ChaosBackendGuide.md) §2·§11 |
| 8 | `PT_E_VERSION`, `PT_E_TRANSPORT` | 예약 그대로 둔다 (쓰는 곳 없음). `PT_E_TRANSPORT`는 M14 OOP가 쓴다 | 값을 지우면 재사용이 생긴다 |
| 9 | 용량 힌트·반복 횟수 필드 | `PtSceneDesc`의 `MaxBodiesHint`/`MaxBodyPairsHint`/`MaxContactConstraintsHint`와 반복 횟수 필드를 그대로 동결한다. 힌트는 힌트다 (넘치면 엔진 오류 bit, G12) | Jolt·Bullet·PhysX가 M3·M5에서 썼다 (`jolt.capacity`, `bullet.pools`, `physx.scene_limits`가 G9 `capacity:` 규칙) |
| 10 | `body.damping_formula` canonical | 1 = UE 순서, 2 = 힘 전 감쇠 (§5.14) | G1b |
| 11 | READBACK 정직성 | 엔진에서 읽은 값만 READBACK이고, G9 규칙표에 없는 READBACK 키는 실패다. 엔진 객체마다 규칙을 대조하는 키의 canonical은 규칙을 지키지 않는 객체 수다 (§5.14) | G9 (Null 9, Jolt 22, Bullet 24, PhysX 38 키. `unchecked`는 Jolt·Bullet `solver.type` 설명 텍스트 둘뿐) |
| 12 | 게이트 Skip | capability가 없어 생기는 Skip은 `tolerances.json` `allowed_skip`에 사유와 함께 등록된 엔진만 통과다. Chaos는 등록된 Skip이 없다 | §8 Skip 규칙, `PhysicsTest.Gates.SelectionRules` |
| 13 | 함수 표의 최소 크기 (M6 수정 라운드 2) | 호스트는 `StructSize >= PT_ABI_V1_0_API_SIZE`(216 B = v1.0의 25개 진입점)인 표를 받는다. 모듈 `StructSize` 밖의 진입점은 없는 것이고 호스트는 `PT_API_HAS(Api, Entry)`로만 읽는다. NULL 검사는 v1.0의 25개에만 한다. 레이아웃은 그대로다 | 전의 검사(`StructSize < sizeof(PtBackendApi)` 거부)로는 v1.x의 첫 append가 모든 v1.0 DLL(Chaos 브리지)을 거부해 append가 사실상 major 변경이었다. 결정 4의 "stride와 함께 append하는 새 읽기 진입점"이 탈출구가 되려면 이 규칙이 태그 전에 있어야 했다 (독립 검사). A1: v1.0 크기 표와 더 큰 표를 받고 더 작은 표를 거부, `PT_API_HAS`가 `StructSize` 밖 진입점을 없는 것으로 봄. `PtAbiC11Check`: `PT_ABI_V1_0_API_SIZE == 216`, C에서 `PT_API_HAS` 컴파일 |

M2.5 스파이크(Chaos hello-DLL)는 하지 않았으므로 반영할 변경 목록이 없다. 스파이크 DLL은 major 1(이 헤더)로 빌드해야 로드된다.

### 9.2 PROVISIONAL(M9)

세 엔진의 조인트·스냅샷이 M9에 처음 쓰므로 그때 확정한다. 그 사이 변경은 `PT_ABI_PROVISIONAL_REVISION`을 올리고, 리비전이 다른 모듈의 잠정 capability를 호스트가 없는 것으로 본다 (§2).
- `CreateConstraints`, `PtConstraintCreateInfo`, 조인트 타입, `PT_JOINTF_*`
- `CaptureDebugSnapshot` / `ReadDebugSnapshot`, `PtParticleSnapshot`, `PtContactPoint`, `PtJointSnapshot`, `PT_SNAP_*`, `PT_CONTACT_*` (M4에 `PT_CONTACT_SLEEPING` bit 5를 더했다. 위 결정 2·3은 잠정 구조체의 **의미**를 정한 것이고 배치는 그대로다)
- 네이티브 드로 `PtDebugDrawCmd`, `PT_DD_*`
- capability: JOINT_BALL/HINGE, CONTACT_*, NATIVE_ISLANDS, SLEEP_THRESHOLD_INFO, BOUNDS, JOINT_SNAPSHOT, NATIVE_DEBUG_DRAW (`PT_CAP_PROVISIONAL_MASK`). `PT_CAP_SLEEP_THRESHOLD_INFO`(bit 21)는 만든 쪽도 쓰는 쪽도 없이 핵심으로 동결될 뻔했다: 짝인 `PT_SNAP_SLEEP_INFO`가 잠정 스냅샷이므로 M6 수정 라운드 2에서 뜻(스냅샷이 바디별 sleep 임계·카운터를 낸다)을 적고 잠정 마스크에 넣었다

### 9.3 동결 뒤에도 열린 것 (계약이 아니라 구현)

- Chaos 브리지의 `NumStepsTaken`·`NumPushedSteps` 세기 방법 (§5.8) [R]. M13에서 정한다.
- PhysX `NormalImpulse`/`FrictionImpulse` 값 (지금 NaN, M9).
- SDK 백엔드 엔진 오류 bit의 백엔드별 표 (M5 §7.1 표): Bullet 0x1/0x2 풀 fallback, PhysX PxErrorCode 값 + 0x100, Jolt `EPhysicsUpdateError` + 0x100 temp fallback + 0x200 assert (assert 카운터는 프로세스 전역이라 동시에 살아 있는 Jolt 씬 사이에서 어느 씬의 assert인지 구분하지 못한다. 하네스 시험 키 `pt.test.raise_assert`가 모든 빌드에서 이 경로를 시험한다).
