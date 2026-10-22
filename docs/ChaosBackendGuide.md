# Chaos 백엔드 구현 가이드 (초안)

> **작성일:** 2026-10-21 (M6. 수정 라운드 2 2026-10-22: §0, §10, §12 G3·G4, §15. M9 2026-10-22: §2, §10, §10.1 - ABI v1.1. M9 마감 2026-10-22: §10.1 M13 (b) 확인 조건)
> **상태:** 초안. 동결된 ABI **v1.1**(태그 `abi-v1.1`은 리드가 M9 커밋에 단다, [Abi.md](Abi.md))을 기준으로 쓴다: 핵심은 M6의 v1.0 그대로이고, M9까지 잠정이던 조인트·디버그 스냅샷·네이티브 드로가 M9에서 세 SDK 엔진 구현으로 확정되었다 (§10, §10.1, [Abi.md](Abi.md) §9.2·§9.4, `PT_ABI_PROVISIONAL_REVISION` 3에서 동결). 사용자가 M13에서 이 문서를 따라 Chaos 브리지를 구현한다.
> **관련 문서:** [Abi.md](Abi.md) · [02_Architecture.md](02_Architecture.md) §7, §9.6, §10, §15 · [03_WorkPlan.md](03_WorkPlan.md) M2.5, M6, M13 · [04_DecisionLog.md](04_DecisionLog.md) DL08, DL19, DL29, DL38~DL41 · [Scenes.md](Scenes.md) · `Config/Golden/tolerances.json`
> **근거 표기:** **[V]** UE 5.8.3 소스(`D:\UnrealEngine`)나 하네스 코드를 직접 읽어 확인 · **[R]** 추정 (M13에서 확인할 것). UE 소스는 읽기만 했고 이 문서의 어떤 코드도 UE에서 복사하지 않았다.

이 문서는 "Chaos 브리지 DLL이 하네스와 맞물리려면 무엇을 해야 하는가"를 의무사항 위주로 모은다. 경계 자체의 규칙(소유권, 스레드, phase, 오류, 버전)은 [Abi.md](Abi.md)가 정본이고, 여기서는 Chaos 쪽에서 그 규칙을 지키는 방법과 UE 쪽 함정을 적는다. 각 항목 끝의 file:line은 UE 5.8.3 기준이다.

---

## 0. 한 장 요약

| 항목 | 의무 | 근거 |
|---|---|---|
| ABI | `PT_GetBackendApi(1)`만 표를 돌려준다. 호스트는 major 1을 요청한다. M2.5 스파이크 DLL(major 0)은 동결 뒤 다시 빌드해야 로드된다. v1.0으로 빌드한 브리지는 뒤의 v1.x 호스트에서도 로드된다 (호스트는 v1.0 크기 216 B 이상의 표를 받고, 뒤에 더한 진입점은 없는 것으로 본다) | [Abi.md](Abi.md) §2, §9.1 결정 13 |
| 템플릿 | `Source/Backends/NullBackend`를 복사해 시작한다 (Kit 기반, `NATIVE_FRAME_MODEL`) | §13 |
| 빌드 | Target.cs `FPSemantics = Imprecise`, `bHasExports = false`, Build.cs `PrivateIncludePathModuleNames.Add("Launch")`, `PT_BACKEND_NS=ChaosBridge`, `PT_ABI_NO_EXCEPTIONS=1` | §1 |
| 스레드 T=1 | `SetThreadingMode_External(SingleThread)` + `p.Chaos.SingleWorkerPhysics=1` (선택 `p.Chaos.DisablePhysicsParallelFor=1`) + `-corelimit=1` | §3 |
| 동기 | `FChaosScene(nullptr, AsyncDt = -1)`. 0을 넘기면 async가 켜진다 | §4 |
| 생성 순서 | CreateActor → 지오메트리·필터·머티리얼 → `AddActorsToScene_AssumesLocked` 1회 → InitDynamicProperties → `!bStartAwake`이면 재우기 | §5 |
| 질량 | 받은 질량·관성·COM을 그대로 쓴다 (^0.75 재계산 금지) | §5 |
| 머티리얼 | 모든 셰이프에 머티리얼. 없으면 sleep 임계가 CVar 기본값으로 떨어진다 | §5 |
| 마찰 | F, SF를 그대로 `FChaosPhysicsMaterial`에. 쌍 규칙은 Chaos가 직접 계산한다 | §5 |
| 명령 | v1.0 계약: 속도·상태 쓰기는 잠든 바디를 깨운다 (Chaos `SetV`가 이미 그렇다. nearly zero 속도만 백엔드 정의이고 `command.nearly_zero_velocity_wakes` = 0으로 선언한다). G6가 Chaos에서 확인한다 | §7, §9 |
| 금지 | `RequestExit`, `SetStandaloneSolver(true)`, `FreeLibrary` | §2 |
| 게이트 | A0~A7, A9, A10, G1~G7, G9, G12 (M13 (a)). 조인트 계약 J1은 같은 물리 게이트 프로세스에서 돌고 조인트를 선언하기 전에는 등록된 Skip, M13 (b)에서 Pass다 (§10.1). `PT_MODULE_NO_REINIT`이면 A 게이트는 프로세스당 하나, 물리 게이트는 또 다른 프로세스 하나 (벤치가 섞인 `-Gates=` 목록을 거부한다). 거부된 `InitGamePhys`는 부작용이 없어야 한다 (§2 0단계). Chaos는 등록된 Skip이 없으므로 G3는 접촉을 캡처하는 DLL(`PT_CAP_CONTACT_POINTS`, 잠정 리비전 일치)로 돈다 | §2, §11 |

---

## 1. 빌드 (UBT Program DLL, D2)

골격은 M2.5 H 작업(`Unreal/ChaosBridgeHost`)으로 만든다. M6 시점에는 아직 저장소에 없다 (M2.5는 선택 항목이고 아직 하지 않았다, [Checkpoints.md](Checkpoints.md)). 이 절은 그 골격이 지켜야 할 설정이다.

**Target.cs (`PhysicsTestChaos.Target.cs`)**
- `Type = TargetType.Program`, `LinkType = TargetLinkType.Monolithic`, `bShouldCompileAsDLL = true`, `LaunchModuleName = "PhysicsTestChaos"`, `DefaultBuildSettings = BuildSettingsVersion.Latest`.
- **`FPSemantics = FPSemanticsMode.Imprecise`를 반드시 쓴다.** Program 타깃은 BuildSettings V7 이상에서 기본이 /fp:precise다 (VCToolChain.cs:1324-1330 [V]). 하네스와 SDK 백엔드는 /fp:fast이므로, 빠뜨리면 Chaos만 다른 부동소수점 의미로 돈다. G0(M7/M13)가 `.rsp`에서 확인한다.
- `bHasExports = false`를 명시한다. Monolithic에서는 이미 기본값이지만(`bHasExportsOverride ?? (LinkType == TargetLinkType.Modular)`, TargetRules.cs:1794 [V]) 명시해야 `*_API`가 비고 import library가 생기지 않는다. `PT_GetBackendApi`는 Kit 매크로의 자체 `__declspec(dllexport)`로 export된다.
- `bCompileAgainstEngine = false`, `bCompileAgainstCoreUObject = true`, `bUsesSlate = false`, `bCompileICU = false`, `GlobalDefinitions.Add("NOINITCRASHREPORTER=1")`.

**Build.cs (`PhysicsTestChaos.Build.cs`)**
- 의존 모듈: Core, CoreUObject, ApplicationCore, Projects, GeometryCore, PhysicsCore, Chaos, ChaosCore (+ `SetupModulePhysicsSupport`).
- **`PrivateIncludePathModuleNames.Add("Launch")`.** `RequiredProgramMainCPPInclude.h`가 `LaunchEngineLoop.h`와 `Runtime/Launch/Private/LaunchEngineLoop.cpp`를 include한다 (RequiredProgramMainCPPInclude.h:6, 11 [V]). HeadlessChaos도 Launch 경로를 더한다 (HeadlessChaos.Build.cs:20 [V]). 빠지면 첫 컴파일에서 실패한다 (SP1).
- include 경로: `<repo>/Source/Runtime/PhysicsTestAbi/Public` (ModuleDirectory에서 4단계 위). BackendCommon은 보지 않는다 (Chaos는 자기 규칙을 쓴다).
- 정의: `PT_ABI_NO_EXCEPTIONS=1`(UE는 `/EHs-c-`, Kit은 try 없이 컴파일된다, `PtAbiUeCompatCheck`가 매 빌드 이 조합을 시험한다), `PT_BACKEND_NS=ChaosBridge`(UE의 `::Chaos`와 겹치지 않는 이름. `Chaos`로 두면 `using namespace PhysicsTest::Abi;` 순간 모든 `Chaos::` 참조가 모호해진다, [Abi.md](Abi.md) §5.13), `PT_KIT_CURRENT_THREAD_ID()=FPlatformTLS::GetCurrentThreadId()`.
- `/we4668`(정의되지 않은 매크로를 `#if`에 쓰면 오류)는 UBT 기본값이다. Kit과 `PtBackendAbi.h`는 이것으로 이미 시험된다.

**출력:** `Binaries/Win64/PhysicsTestChaos.dll`(Development, 뷰어용)과 `PhysicsTestChaos-Win64-Test.dll`(Test, 벤치용). 두 DLL을 한 프로세스에 함께 두지 않는다. 호스트는 `-ChaosDll=<path>` 또는 `PT_CHAOS_DLL`로 찾는다.

**UBT 실행:** `Tools/Chaos/build_chaos.cmd <Development|Test>`(M2.5)가 깨끗한 환경에서 `Build.bat PhysicsTestChaos Win64 <Cfg> -Project=... -WaitMutex -NoHotReload`를 부른다. `Engine/Programs` 아래에는 아무것도 생기지 않아야 한다 (SP1).

---

## 2. 모듈 수명 (`InitGamePhys` / `TermGamePhys`)

**GetInfo**
- `ModuleFlags`: `PT_MODULE_UE_RUNTIME | PT_MODULE_ONE_PER_PROCESS | PT_MODULE_NO_REINIT | PT_MODULE_SPAWNS_THREADS_AT_INIT` (+ Development DLL이면 `PT_MODULE_DEBUG_DRAW_COMPILED`). `NO_REINIT`의 결과는 §11.
- `Capabilities`: `COLLISION | KINEMATIC | SLEEPING | CCD | GYROSCOPIC | MULTITHREADED | NATIVE_FRAME_MODEL | ACTIVE_LIST | NATIVE_HASH` (+ v1.1 항목: 조인트를 구현하면 `JOINT_BALL | JOINT_HINGE | JOINT_SNAPSHOT`, 접촉을 캡처하면 `CONTACT_*`, Development DLL의 네이티브 드로면 `NATIVE_DEBUG_DRAW`; §10). `STATE_WRITE`는 선택이다 (SET_BODY_STATE를 구현하면 선언한다).
- `RequiredEffectiveKeys`: `threads.single_worker`, `threads.max_num_workers`, `chaos.threading_mode`, `chaos.async_dt`, `chaos.use_async_interpolation` (02 §10, DL39). A7이 일반 집합 25개와 함께 확인한다.
- `ProvisionalRevision = PT_ABI_PROVISIONAL_REVISION` (v1.1에서 3으로 동결). 다르면 호스트가 `PT_CAP_PROVISIONAL_MASK`의 capability(조인트·접촉·스냅샷·드로)를 없는 것으로 본다. 그래서 브리지는 v1.1 헤더로 빌드한다.

**InitGamePhys 순서** (init 스레드가 GameThread가 된다, 02 §15.3)
0. **UE 호출 전에 인자를 모두 검사한다 (M6 수정 라운드 1).** `StructSize`, `MaxThreads ≥ 1`, `NumOptions`/`Options`, 키·값의 NULL은 Kit이 `Backend->InitGamePhys` 전에 거부한다 (PtBackendKit.h `InitGamePhys`). 브리지는 그다음 **모든 옵션 키와 값**(아는 키인지, 값이 파싱되는지)을 PreInit 전에 검사하고, 하나라도 틀리면 UE를 건드리지 않고 `PT_E_INVALID_ARG`를 낸다. 그래야 거부된 Init이 부작용이 없고 모듈이 Loaded로 남아 다음 유효한 Init을 받을 수 있다. A1(작은 `StructSize`로 거부된 Init 뒤 유효한 Init)과 A4(NULL, `MaxThreads` 0, 모르는 옵션, `Options` 없는 `NumOptions` 네 번 거부 뒤 유효한 Init)가 한 프로세스에서 이 순서를 요구한다 (AbiGates.cpp A1, A4 모듈 수준 사례). PreInit을 한 뒤에 옵션을 거부하면 `NO_REINIT` 브리지는 다음 Init을 받을 수 없다.
1. `FPlatformMisc::SetOverrideProjectDir(<SavedDir>)`
2. `UE::Geometry::ExactPredicates::GlobalInit()` (HeadlessChaosApp.cpp:547 [V])
3. `GEngineLoop.PreInit("PhysicsTestChaos -ABSLOG=<dir>/chaos.log -unattended -corelimit=<T>")`. T는 `PtInitParams::MaxThreads`다.
4. `ProcessNewlyLoadedUObjects()` → `FModuleManager::Get().StartProcessingNewlyLoadedObjects()` → `InitGamePhysCore()`
5. 모듈 CVar: T = 1이면 `p.Chaos.SingleWorkerPhysics=1` (선택 `p.Chaos.DisablePhysicsParallelFor=1`), T ≥ 2이면 `p.Chaos.MaxNumWorkers=T`. 그다음 `PtInitParams::Options`(프로필 `[ConsoleVariables]`의 Chaos CVar, 변형 키: 0단계에서 이미 검사한 값을 적용만 한다).
6. 호스트 로그 콜백으로 보내는 `FOutputDevice` 등록 (`PtLogCallback`은 어느 스레드에서 불러도 된다).
- 실패하면 모듈은 Loaded로 남는다. 이것은 0단계의 검사가 PreInit 전에 끝날 때만 지킬 수 있다. 모르는 옵션 키는 `PT_E_INVALID_ARG`다 (Null `InitGamePhys`의 거부 경로를 그대로 쓴다. Null은 옵션이 없어 첫 줄에서 거부한다). PreInit 뒤의 실패(엔진 초기화 실패)는 복구할 수 없으므로 브리지는 그 뒤로 모든 Init을 `PT_E_BAD_STATE`로 거부한다.

**금지**
- `RequestExit`: `PostQuitMessage`를 부른다 (WindowsPlatformMisc.cpp:1517-1520 [V]). 호스트 메시지 루프가 WM_QUIT를 받는다.
- `SetStandaloneSolver(true)`: 솔버가 태스크를 띄우지 않아 시뮬레이션이 진행되지 않는다 (PhysicsSolverBase.cpp:649-653 [V]).
- `FreeLibrary`: 호스트는 모듈을 내리지 않는다. 브리지도 자기 자신을 내리지 않는다.
- 프로세스 종료: `PT_MODULE_UE_RUNTIME` 모듈을 로드한 프로세스는 결과와 `exit.json`을 모두 쓰고 flush한 뒤 `TerminateProcess(GetCurrentProcess(), code)`로 끝내야 한다 ([Abi.md](Abi.md) §4). CRT detach 중 UE 정리를 피하기 위해서다. **M7에서 호스트가 구현했다:** run은 모든 출력과 `exit.json`(`terminate_process: true`)을 쓴 뒤 `TermGamePhys`를 부르고 `teardown.json`을 남긴 다음 `TerminateProcess`로 끝난다. `-Conformance`, `-Describe`, `-SceneHashes`, `-Gate=`도 UE_RUNTIME 모듈을 로드했으면 `wmain` 끝에서 같은 경로로 끝난다. Null DLL 시험 대역(`PT_NULL_DLL_UE_RUNTIME=1`)으로 확인했다: 그 DLL은 `DLL_PROCESS_DETACH`를 받지 않는다 (CTest `PhysicsTest.Bench.UeRuntimeTerminate`, 대조군 `UeRuntimeControl`, `UeRuntimeDescribe`). M13은 같은 경로로 Chaos를 확인한다. 브리지는 `TermGamePhys`에서 씬을 파괴하되 엔진 종료(`AppPreExit` 등)는 하지 않는다 [R]. Fatal도 같은 길로 끝난다 (M7 수정 라운드 0): UE_RUNTIME 모듈이 로드된 프로세스의 Fatal은 `exit.json`(3, `terminate_process: true`)을 쓴 뒤 `TerminateProcess`로 끝나고 (`_Exit`은 ExitProcess라 DLL detach를 부른다, CTest `PhysicsTest.Bench.UeRuntimeFatal`), teardown 중의 Fatal(브리지의 `TermGamePhys`에서 UE가 낸 Fatal 포함)은 `exit.json`을 바꾸지 않고 `teardown.json`에 남은 뒤 run의 코드로 `TerminateProcess`된다 (`ExitTeardownFatal`). **M8에서 run 밖의 모드로 넓혔다 (M7 검토):** `wmain`이 맨 처음 프로세스 전체의 Fatal 콜백을 건다 (`FBenchmarkRunner::InstallFatalHook`). `-Conformance`와 `-Gate=`는 run처럼 `FFatalExitScope`로 자기 `exit.json`(3, `fatal`, `terminate_process: true`)을 먼저 쓰고, `-Describe`·`-List`·`-SceneHashes`는 쓸 `exit.json`이 없으므로 바로 `TerminateProcess`한다. 어느 모드든 UE_RUNTIME 모듈이 로드된 프로세스의 Fatal은 `_Exit`(DLL detach)로 가지 않는다 (CTest `PhysicsTest.Bench.UeRuntimeConformanceFatal`: `-Conformance -InjectFatal`, `DLL_PROCESS_DETACH` 줄 없음).

**stdout (M7, M6 검토 지적):** `-List`, `-Describe`, `-SceneHashes`는 stdout에 JSON 하나만 내야 한다 (M8 도구가 파싱한다. 하네스 로그는 이 모드에서 모두 stderr로 간다). 브리지는 stdout에 아무것도 쓰지 않는다: PreInit 명령줄에 `-stdout`을 넣지 않고, 콘솔 출력 장치(`FOutputDeviceStdOutput` 등)를 붙이지 않는다. UE 로그는 `PtLogCallback`(호스트 로그, stderr)이나 `-ABSLOG` 파일로만 보낸다. M13에서 Chaos `-Describe`의 stdout 순수성을 `Bench.Conformance`에 더한다 (C-10).

**Reserved 필드 (M7):** 입력 구조체의 `Reserved*`는 호스트가 언제나 0으로 쓰고, M7 이후의 Kit은 0이 아니면 `PT_E_INVALID_ARG`로 거부한다 ([Abi.md](Abi.md) §2). 브리지는 Kit을 쓰므로 따로 할 일이 없다 (v1.1, M9 수정 라운드 1: 디버그 스냅샷의 `PtDebugSnapshotRequest.Reserved`·`PtDebugSnapshotBuffers.Reserved`와 `PT_SNAP_ALL` 밖의 요청 bit도 Kit이 capability 전에 거부한다. 브리지의 `CaptureDebugSnapshot`은 정의된 bit만 받는다). 출력 구조체의 Reserved에는 0을 쓴다.

**MXCSR (A6):** PreInit이 MXCSR(FTZ/DAZ)을 바꿀 수 있다. Kit은 호출마다 호출 전 값을 되돌리고 경고를 한 번 남기며 `kit.mxcsr_restores`를 센다. A6는 그 경고가 하나라도 나오면 실패한다. 경고가 나오면 PreInit 이후 브리지가 UE가 정한 MXCSR로 도는지(엔진 정책) 호스트 값으로 도는지 결정해야 한다 (M2.5 SP2 기록 항목).

---

## 3. 스레드 매핑 (DL39, 02 §9.6)

| T (`PtSceneDesc::NumThreads`) | 설정 | 근거 |
|---|---|---|
| 1 | `SceneSolver->SetThreadingMode_External(EThreadingModeTemp::SingleThread)` + `p.Chaos.SingleWorkerPhysics=1` (선택 `p.Chaos.DisablePhysicsParallelFor=1`) + `-corelimit=1` | SingleThread 모드만으로는 내부 ParallelFor가 꺼지지 않는다: `bSingleThreaded = bSingleWorkerPhysics \|\| bDisablePhysicsParallelFor \|\| bForceSingleThreaded` (Parallel.cpp:95 [V]). 제약 솔버의 워커 수도 `!bSingleWorkerPhysics`로 정해진다 (ConstraintGroupSolver.cpp:250 [V]) |
| ≥ 2 | TaskGraph(생성자 기본) + `-corelimit=T` + `p.Chaos.MaxNumWorkers=T` | `MaxNumWorkers`는 상한일 뿐 풀 크기를 정하지 않는다 (Parallel.cpp:16, 33, 기본 100 [V]). PreInit은 논리 코어 − 2개의 TaskGraph 워커를 만든다 [R, 02 §9.6] |

- 생성자는 `FApp::ShouldUseThreadingForPerformance()`로 모드를 고른다 (ChaosScene.cpp:73-77 [V]). 그래서 생성 뒤 `SetThreadingMode_External`(PhysicsSolverBase.h:615 [V])로 명시적으로 덮어쓴다.
- effective 키: `threads.single_worker`(CVar 값을 읽어 READBACK), `threads.max_num_workers`(READBACK), `chaos.threading_mode`(솔버에서 읽어 READBACK), `threads.workers`(T − 1, 실제 워커 수를 알 수 있으면 READBACK).
- 호스트는 affinity를 정한 **뒤** `LoadLibraryExW`를 부른다 (UE 워커가 affinity를 상속한다). G10b(M10)가 T = 1의 단일 스레드를 cpu_eff로 증명한다.

---

## 4. 씬 생성 (`CreateScene`)

- `new FChaosScene(nullptr, AsyncDt)`. **sync이면 `AsyncDt = -1`**이다 (`PT_ASYNC_DT_SYNC`). 0 이상이면 async다 (`IsUsingAsyncResults`: `AsyncDt >= 0`, PhysicsSolverBase.h:712-715 [V]). `PtSceneDesc::AsyncFixedTimeStepSize > 0`이면 그 값을 넘긴다 (Native driver의 AsyncFixed).
- AsyncFixed에서 fixed-dt 누산은 `p.UseAsyncInterpolation=1`일 때만 켜지고, 그때 GT 상태가 한 스텝 늦게 보간된다 (`IsUsingFixedDt = IsUsingAsyncResults() && UseAsyncInterpolation`, PhysicsSolverBase.h:717-720 [V]). 그래서 AsyncFixed의 바디별 거동은 비교에서 뺀다 (02 §9.2). manifest에 `async_interpolated=true`를 남긴다. `chaos.async_dt`, `chaos.use_async_interpolation`이 READBACK 키다.
- **SolverOptions를 명시적으로 적용한다.** 생성자는 `UPhysicsSettingsCore` CDO의 `SolverOptions`와 `bEnableEnhancedDeterminism`을 적용한다 (ChaosScene.cpp:88-94 [V]). Engine 없이 도는 DLL에서 CDO는 `[/Script/PhysicsCore.PhysicsSettingsCore]`를 읽으므로 하네스 ini(`[/Script/Engine.PhysicsSettings]`)와 다를 수 있다. 생성 뒤 `SceneSolver->ApplyConfig(<리졸브된 SolverOptions>)`(PBDRigidsSolver.h:325 [V])와 `SceneSolver->SetIsDeterministic(<bEnableEnhancedDeterminism 또는 PT_SIM_ENHANCED_DETERMINISM>)`(PBDRigidsSolver.h:227 [V])을 다시 부른다.
- **sleep 끔 (Normalized, `PT_SIM_ALLOW_SLEEPING` 없음):** `p.Chaos.Solver.Sleep.Enabled=0` (IslandManager.cpp:53 [V]) 또는 파티클마다 `ESleepType::NeverSleep` (IslandManager.cpp:240 [V]). 그 씬의 `PUT_TO_SLEEP`은 무시하고, `!PT_BODY_START_AWAKE`의 재우기 단계는 건너뛰어 센다 (`chaos.start_asleep_skipped`, SET_ONLY).
- 중력은 `PtSceneDesc::Gravity`를 그대로, 바뀔 때만 다시 넣는다 (DL32). 반발 임계: `RestitutionThreshold > 0`이면 `p.CollisionRestitutionThreshold`에 그 값을 가속도로 바꿔(÷ dt) 넣는다 (PBDCollisionConstraints.cpp:69 [V, 02 §10]). `< 0`(UEParity)이면 Chaos 네이티브 값을 두고 READBACK한다.
- 모르는 씬 옵션 키는 `PT_E_INVALID_ARG`다. 씬 옵션은 변형 ini(`Config/Profiles/Backend/Chaos.<Variant>.ini`)의 `[PhysicsTest.Backend.Chaos]` 키다.
- 씬 슬롯은 Kit이 관리한다 (16개). 씬 하나는 `FChaosScene` 하나다. `Chaos.Evolution` 진단 씬(D2)은 같은 DLL 안의 두 번째 구현으로 나중에 더한다.

---

## 5. 바디 생성 (`CreateActors`, DL38)

배치 하나를 UE `FInitBodiesHelper` 순서로 만든다 (BodyInstance.cpp:1669, 1695-1706, 4648-4743 [V]).

1. **바디마다 `FChaosEngineInterface::CreateActor`** + 생성 시점 플래그: kinematic(`PT_MOTION_KINEMATIC`), gravity(`PT_BODY_ENABLE_GRAVITY`), CCD, `MaxLinearVelocity`, inertia conditioning, gyroscopic, 반복 횟수 (BodyInstance.cpp:1301-1340 [V]).
2. **지오메트리:** `TSphere` / `TBox` / `FCapsule` / `FConvex`.
   - 박스 half extent는 ABI가 이미 반으로 나눠 준다 (`PtShapeElem::BoxHalfExtent`).
   - **박스 margin = `min(2 × minHalf × CollisionMarginFraction, CollisionMarginMax)`** (ChaosInterfaceUtils.cpp:156 [V], 기본 0.05 / 10 cm). 이 코드는 Engine 모듈에 있어 링크할 수 없으므로 브리지가 다시 구현한다. `shape.box_margin`을 셰이프에서 읽어 READBACK으로 낸다.
   - 캡슐은 `Radius` + `HalfLength`(선분 절반)이고 축은 로컬 Z다. Chaos `FCapsule`의 두 끝점은 `±HalfLength × Z`다.
3. **충돌 필터:** `Chaos::Filter::FShapeFilterBuilder`. ABI의 `BlockMask`는 32 채널(uint32)이다. **UE 5.8.3 마스크는 uint64**(ECC_MAX = 65, EngineTypes.h:1176 [V])이므로 브리지가 uint64로 넓혀 넣는다 (상위 32 bit는 0).
4. **머티리얼: 모든 셰이프에 넣는다.** 머티리얼 없는 셰이프의 sleep 임계는 `p.Chaos.Solver.Sleep.Defaults.*` CVar 값(선 0.001, 각 0.0087, 카운터 20)으로 떨어진다 (IslandManager.cpp:121-131 [V]).
   - `PtPhysicalMaterial`의 F, SF, R을 그대로 `FChaosPhysicsMaterial`에 넣는다. **미리 결합하지 않는다.** 쌍 규칙 `dynamic = C(F0, F1)`, `static = max(C(max(F0, SF0), max(F1, SF1)), dynamic)`은 Chaos가 직접 계산한다 (PBDCollisionConstraints.cpp:472-476 [V], DL41).
   - 결합 모드는 머티리얼마다 호스트가 정해 준다 (`FrictionCombineMode`, `RestitutionCombineMode`). 쌍에서는 큰 값이 이긴다 (`ChooseCombineMode`).
5. **`AddActorsToScene_AssumesLocked`를 배치 전체에 한 번** 부른다.
6. **바디마다 InitDynamicProperties 순서:** 질량·관성·COM → damping → 최대 각속도 → 최대 침투 해소 속도 → 초기 속도 → sleep 임계 배수 → 안정화 임계 배수 → 반복 횟수.
   - **질량·관성·COM은 받은 값을 그대로 쓴다.** `PtActorCreateInfo::Mass`, `InertiaDiagonal`(주관성, kg·cm²), `CenterOfMassLocal`(COM 위치 + 주축 회전)은 하네스 `BodyUtils`가 계산한 값이다 (D5). Chaos에서 ^0.75 규칙(`RaiseMassToPower`)이나 셰이프 관성을 다시 계산하면 G2가 실패한다.
   - `MaxDepenetrationVelocity < 0`은 "설정 안 함"이다: 솔버의 `CollisionInitialOverlapDepenetrationVelocity`를 쓴다. 0은 "밀어내지 않음"이다 ([Abi.md](Abi.md) §5.7).
   - `MaxAngularVelocity`는 rad/s, `<= 0`은 제한 없음이다. Chaos는 `MaxAngularSpeedSq`로 언제나 clamp하므로 0을 그대로 넣으면 회전이 멈춘다 (PBDRigidsEvolutionGBF.cpp:950-954 [V, 02 §10]). 프로필 Normalized는 1e7 deg/s를 보낸다.
7. **`!PT_BODY_START_AWAKE`이고 kinematic이 아니면** `SetWakeCounter_AssumesLocked(0)` + `PutToSleep_AssumesLocked` (BodyInstance.cpp:4723-4733 [V]). sleep을 끈 씬에서는 건너뛰고 센다. G6 `S08.StartAsleep`가 frame 0의 `ReadBodyStates`에서 Sleeping을 확인한다.

`BodyIndex`는 생성 순서이고 재사용하지 않는다. 해제한 바디는 Kit이 기억한다 (`IsReleased`).

---

## 6. 프레임 (`NATIVE_FRAME_MODEL`)

- 호스트는 프레임당 한 번 UE `SetUpForFrame` 인자를 그대로 넘긴다 (Native driver, DL08). 브리지는 `FChaosScene::SetUpForFrame` → `StartFrame`(`AdvanceAndDispatch_External`) → `WaitPhysScenes` → `EndFrame`을 그대로 부른다.
- **`PtFrameResult::NumStepsTaken`** = 그 프레임에 솔버가 실제로 돈 스텝 수 (FPBDRigidsSolver의 AdvanceOneTimeStep 횟수). 하네스 `FStepPlan::NumSteps`와 다르면 호스트가 Fatal로 멈춘다. 솔버 스텝 콜백(예: `TSimCallbackObject`의 PreSimulate)을 세는 방법을 권한다 [R].
- **`PtFrameResult::NumPushedSteps`(v1.0 신규)** = 그 프레임에 솔버로 밀어 넣은 스텝 수 (`AdvanceAndDispatch_External`의 지역 `NumSteps` = `FStepPlan::NumPushedSteps`). `AsyncBlockMode`나 MinDeltaTime이 둘을 갈라 놓을 수 있다 (PhysicsSolverBase.cpp:623-646 [V]). 모르면 Kit이 미리 채운 `PT_INDEX_NONE`을 그대로 둔다. 채우면 호스트가 계획과 대조한다 (A10과 `FPhysScene`).
- 스텝 dt: Native 백엔드는 UE처럼 double `InternalDt`로 스텝한다. 해석 기준값은 Native는 `FStepPlan::InternalStepDeltaTime`, Harness는 `dt_engine = (double)(float)dt`로 계산한다 ([Abi.md](Abi.md) §5.8).
- 힘·임펄스·kinematic 목표의 프레임 규칙은 Chaos 자체 규칙이다 (Null이 그것을 흉내 낸 기준 구현이다, [Abi.md](Abi.md) §5.10). 브리지가 따로 할 일은 없다: 명령을 받는 즉시 GT API(`AddForce`, `SetV`, `SetKinematicTarget` 등)로 넘긴다.

---

## 7. 명령 (`ApplyBodyCommands`)과 v1.0 계약

명령은 `FChaosEngineInterface::*_AssumesLocked` 동사에 그대로 대응한다 (PtBackendAbi.h 명령 표). 배치는 전부 아니면 전무다: 거부할 수 있는 검사는 모두 `ValidateBodyCommand`에 두고 `ApplyBodyCommand`는 실패하면 안 된다.

| 명령 | Chaos | v1.0 계약 메모 |
|---|---|---|
| `SET_GLOBAL_POSE` | `SetGlobalPose_AssumesLocked` | 잠든 dynamic 바디는 언제나 깬다 (`SetX`, SingleParticlePhysicsProxy.h:240-251 [V]). kinematic 바디는 그 자세가 목표가 되고 속도가 0이 된다 (ChaosEngineInterface.cpp:2550-2557 [V]) |
| `SET_LINEAR_VELOCITY` / `SET_ANGULAR_VELOCITY` | `SetLinearVelocity_AssumesLocked(…, true)` / `SetAngularVelocity_AssumesLocked(…, true)` | **v1.0: 잠든 바디를 깨운다, `PT_CMDF_AUTO_WAKE`와 무관하다.** Chaos가 이미 그렇다: 0이 아닌 벡터는 `SetVBase`/`SetWBase`가, 정확한 0 벡터는 `SetX`/`SetR`가 깨운다 (SingleParticlePhysicsProxy.h:322-340, :362-380, :1217-1244 [V]). 엔진 인터페이스는 `bAutoWake = true`만 지원한다 (`ensure(bAutoWake)`, ChaosEngineInterface.cpp:626-658 [V]). 그래서 `bAutoWake`에는 언제나 true를 넘기고 따로 `WakeUp`을 부르지 않는다. 0이 아니지만 nearly zero인 벡터(모든 성분 ≤ 1e-4)는 `SetVBase`/`SetWBase`가 깨우지 않는다 (:331 `!InV.IsNearlyZero()` [V]). v1.0 계약이 이 경우를 백엔드 정의로 둔 것은 이 때문이다 ([Abi.md](Abi.md) §5.10, §9.1 결정 1): 브리지는 UE 동작을 그대로 두고 effective 키 `command.nearly_zero_velocity_wakes`를 canonical **0**으로 낸다 (§9). **G6(UEParity)가 확인한다:** `S08`의 60 프레임 잠든 바디에 `PT_CMDF_AUTO_WAKE` 없이 (100, 0, 0) cm/s, 0 벡터, (0, 0, 2) rad/s를 쓰면 다음 스텝에 Dynamic이고 그 속도(0 벡터는 한 스텝의 중력)로 움직인다. nearly zero 쓰기 (5e-5, 0, 0) cm/s와 (0, 0, 5e-5) rad/s는 선언 0이면 Sleeping 그대로이고 움직이지 않아야 한다 (M6 수정 라운드 1) |
| `SET_BODY_STATE` (선택, `STATE_WRITE`) | `SetX` + `SetR` + `SetV` + `SetW` | v1.0: 잠든 dynamic 바디를 깨운다 (텔레포트 + 속도). `STATE_WRITE`를 선언하면 G6가 이것도 확인한다 |
| `ADD_IMPULSE` / `ADD_VELOCITY` | `AddImpulse_AssumesLocked` / `AddVelocity_AssumesLocked` | 다음 스텝 적분에 한 번 들어간다 (PBDRigidsEvolutionGBF.cpp:920-924 [V]) |
| `ADD_ANGULAR_IMPULSE` / `ADD_ANGULAR_VELOCITY` | `AddAngularImpulseInRadians_AssumesLocked` / `AddAngularVelocityInRadians_AssumesLocked` | |
| `ADD_FORCE` / `ADD_TORQUE` | `AddForce_AssumesLocked` / `AddTorque_AssumesLocked` | `PT_CMDF_NO_WAKE`이면 깨우지 않고, 그때 잠든 바디는 이 명령을 무시한다 (호스트가 UE의 GT 깨우기를 따로 `WAKE_UP`으로 보냈다). flag가 없으면 UE처럼 잠든 바디를 깬다. `PT_CMDF_ACCEL_CHANGE`는 `bAccelChange` |
| `SET_KINEMATIC_TARGET` | `SetKinematicTarget_AssumesLocked` | kinematic 바디만 (아니면 `ValidateBodyCommand`가 `PT_E_INVALID_ARG`) |
| `WAKE_UP` / `PUT_TO_SLEEP` | `WakeUp_AssumesLocked` / `PutToSleep_AssumesLocked` | sleep을 끈 씬의 `PUT_TO_SLEEP`은 무시한다 |

- `PT_CMDF_ALLOW_SUBSTEPPING`은 정보용이다 (UE5가 쓰지 않는다, ChaosEngineInterface.cpp:932-950 [V]). 무시한다.
- static·kinematic 바디에 대한 힘·임펄스·속도 명령은 효과가 없다.

---

## 8. 읽기

- **`ReadBodyStates`:** `PT_READ_ALL`은 만든 바디 전부를 인덱스 순서로 (해제한 바디는 UNINITIALIZED, 0 자세·속도). `PT_READ_ACTIVE_ONLY`는 깨어 있는 dynamic, 움직인 kinematic, ObjectState가 바뀐 바디(잠들거나 깬 바디 포함). `ActorTransform`은 바디 원점의 자세다 (COM 아님). 선속도: dynamic은 COM의 속도, **kinematic은 actor 원점의 속도**다 (Chaos `ApplyKinematicTargets`가 파티클 X의 변위로 V를 정한다, PBDRigidsEvolutionGBF.cpp:1180-1239 [V]). GT 파티클(`V()`)을 그대로 읽으면 된다.
- **`ReadMassProperties`:** 엔진이 실제로 가진 값 (파티클의 `M()`, `I()`, `CenterOfMass()`, `RotationOfMass()`). G2가 하네스 값과 1e-5로 대조한다. 하네스가 넘긴 값을 기록해 두었다가 돌려주면 안 된다 (v1.0 effective 정직성 규칙과 같은 원칙, [Abi.md](Abi.md) §5.14).
- **`GetStepStats`:** 리스너가 필요 없는 카운터만 (활성·잠든 바디 수, 섬 수). 모르는 값은 -1. `PhaseTimeNs`는 `PT_INSTR_PHASES`에서 `TSimCallbackObject`(PreSimulate, PostSolve)로 [R].
- **`ComputeNativeStateHash`:** 하네스 바디 순서로 FNV-1a (`PtContactConvention.h`의 `Fnv1a64`와 같은 알고리즘을 브리지가 다시 구현한다. BackendCommon은 보지 않는다). 무엇을 해시하는지(X, R, V, W) effective 키로 적는다.

---

## 9. effective 설정 (`DumpEffectiveSettings`)

- 한 줄에 JSON 객체 하나 (`key`, `native`, `native_unit`, `canonical`, `unit`, `source`). source는 READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT.
- **READBACK은 엔진에서 읽은 값만이다.** 어댑터가 넘긴 입력이나 어댑터가 센 값은 SET_ONLY다 ([Abi.md](Abi.md) §5.14, M5 수정 라운드 0). G9가 READBACK 키를 리졸브된 값과 대조하므로, 입력을 READBACK으로 표시하면 그 검사가 저절로 통과해 버린다.
- 일반 집합 25개는 Null의 `DumpEffectiveSettings`를 출발점으로 쓴다. Chaos 고유 값:
  - `body.damping_formula`: **canonical = 1**(UE 순서: v ← (v + a·dt)·max(0, 1 − c·dt), Chaos `Integrate`). G1b가 이 id로 기준값을 계산한다.
  - `command.nearly_zero_velocity_wakes`: **canonical = 0** (unit `bool`, COMPILE_TIME): nearly zero 속도 쓰기는 잠든 바디를 깨우지 않는다 (`SetVBase`/`SetWBase`, SingleParticlePhysicsProxy.h:331 [V]). G6가 선언대로인지 본다. 브리지가 이때도 깨우기로 하면(따로 `WakeUp`) 1을 낸다.
  - `contact.restitution_threshold`: Chaos 네이티브는 가속도 단위다 (`p.CollisionRestitutionThreshold`). cm/s로 바꿔(× dt) canonical에 넣는다.
  - `shape.box_margin`: 셰이프에서 읽은 margin (§5).
  - `sleep.criteria`: 머티리얼 임계 (cm/s, rad/s, 카운터).
  - 필수 키 5개 (§2).
  - `contact.point_pose` (선택 키, M7): **canonical = 1**, COMPILE_TIME. 스냅샷 접촉점이 `Integrate` 뒤 예측 자세 P, Q 기준이라는 선언이다 (§10). G3는 키가 있으면 점에서 추론한 자세와 같은지 확인한다 ([Abi.md](Abi.md) §5.14).
  - **벤치 preflight의 G9 (M7):** 벤치는 run마다 그 씬의 effective 덤프로 G9 비교를 한다. 상자 기하에서 나온 상수 규칙에는 `"scene"`(그 기하의 씬 이름 하나나 목록)을 달고, 씬마다 값이 다르면 `"by_scene"`으로 그 씬의 값을 준다 (M7 수정 라운드 0, `tolerances.json`의 Bullet `contact.speculative_distance`가 예). 그렇지 않으면 다른 기하의 씬 run이 종료 코드 2로 거부된다. 바디 값을 정하는 게이트 씬에서는 바디 템플릿 규칙(`profile:linear_damping` 등)을 건너뛴다.
- **G9 등록:** Chaos의 READBACK 키마다 `Config/Golden/tolerances.json`의 `gates.G9.readback`에 규칙을 더한다 (범위 `Chaos`, 변형별이면 `Chaos.<Variant>`). 규칙은 리졸브된 값과의 대조(`profile:`, `option:`, `init:`, `capacity:`, `const:`/`min:` + 이유, 텍스트 규칙)이거나, 대응하는 프로필 값이 없다는 이유를 단 `unchecked`다. 등록되지 않은 READBACK 키가 있으면 G9가 실패한다.

---

## 10. 디버그 스냅샷과 네이티브 드로 (v1.1 확정)

이 절의 구조체와 진입점(`CaptureDebugSnapshot`, `ReadDebugSnapshot`, `PtParticleSnapshot`, `PtContactPoint`, `PtJointSnapshot`, `PtDebugDrawCmd`)은 M9에서 확정되었다 (v1.1, [Abi.md](Abi.md) §9.2). 레이아웃은 v1.0 그대로다. 접촉 규칙은 v1.0에 이미 적었고, 드로 규칙은 v1.1이 더했다:
- **접촉 규약:** normal은 Body1 위에서 Body0을 향한다, Phi < 0은 침투, Body0은 더 움직이는 쪽 (ContactPoint.h:49-53 [V]). **PointOn0은 Body0 표면, PointOn1은 Body1 표면**이고 (PointOn0 − PointOn1)·n = Phi다 (v1.0, M6 계약 (c)). 점은 그 스텝의 narrow phase 자세, 곧 Chaos에서는 `Integrate` 뒤의 예측 자세 P, Q 기준이다 (§12 G3).
- **정규화 (canonicalisation, M6 수정 라운드 2):** Chaos는 쌍의 두 파티클 순서를 **이동성이 아니라 셰이프 쌍 타입으로** 정한다: `CalculateShapePairType(…, bSwap)`이 셰이프 타입에 따라 파티클을 바꾸고 제약은 바뀐 순서로 만들어진다 (ParticlePairMidPhase.cpp:1169-1174, :1672-1687 [V]). 그래서 제약의 Particle0이 static 바닥이고 Particle1이 dynamic 상자일 수 있다. 브리지는 BackendCommon의 `CanonicalizeContact`를 볼 수 없으므로 같은 일을 다시 구현한다: Body0 = 더 움직이는 바디(dynamic > kinematic > static, 같으면 낮은 인덱스)가 되게 바꿔야 하면 PointOn0·PointOn1과 Shape0·Shape1을 바꾸고, normal과 마찰 임펄스의 부호를 뒤집고(법선 임펄스는 크기라 그대로, Phi도 그대로), `PT_CONTACT_SWAPPED`를 토글한다. G3는 dynamic 바디가 Body0인지 점마다 확인한다 (`Body0 must be the more mobile body`).
- **플래그 규칙 (v1.0):** ACTIVE / ACTIVE_APPROX / SPECULATIVE 중 많아야 하나. ACTIVE = 솔버가 마지막 스텝에 그 점에 작용한 점 - 법선 임펄스 또는 push-out (Chaos `bIsActive`: `NetPushOut`이나 `NetImpulse`가 nearly zero가 아님, ChaosDebugDraw.cpp:1453 [V]). 브리지는 `PT_CAP_CONTACT_ACTIVE_EXACT`를 선언하고 ACTIVE만 쓴다 (ACTIVE_APPROX는 정확한 결과가 없는 Jolt용이다). **`NormalImpulse`를 채우면** (`PT_CAP_CONTACT_NORMAL_IMPULSE`) push-out을 포함한 값을 낸다: (`NetImpulse` + `NetPushOut` / Dt)·n (kg·cm/s). QPBD velocity solve는 push-out한 점의 `NetImpulse`를 −`NetPushOut` / Dt까지 음수로 둘 수 있으므로(`MinImpulseNormal`, PBDCollisionSolver.h:1178-1196 [V]) `NetImpulse`만 내면 ACTIVE 점의 임펄스가 음수가 된다. 합은 그 clamp 때문에 0 이상이다. G3는 둘을 한 방향으로만 묶는다: 임펄스 > 0.05 kg·cm/s이면 ACTIVE, ACTIVE이면 임펄스 ≥ −0.05 (`G3.normal_impulse_eps`). ACTIVE인데 임펄스 0(마찰만, push-out 속도를 지운 점)은 통과한다. 임펄스를 채우지 않으면(NaN, capability 없음) 이 검사는 없다 (M6 수정 라운드 2. 전의 G3는 ACTIVE == (임펄스 > 0)을 요구해 `bIsActive`를 그대로 쓰는 브리지가 실패했을 것이다). SPECULATIVE = Phi > 0이고 ACTIVE가 아님. 잠든 섬의 접촉은 마지막 솔버 결과 그대로 + `PT_CONTACT_SLEEPING` (PBDCollisionContainerSolver.cpp:373-409 [V, Abi.md §7.1]). 명령으로 깬 바디의 잠든 쌍은 다음 스텝까지 스냅샷에서 뺀다. **G3가 Chaos에서 확인한다:** step 1 점마다 셋 중 많아야 하나, SPECULATIVE는 Phi > 0만, `ACTIVE_EXACT`면 쌍마다 ACTIVE 점이 있고 ACTIVE_APPROX가 없음. UEParity에서는 `S04`의 상자를 40 프레임 뒤 `PutInstanceToSleep`으로 재워 유지 점(모두 SLEEPING)을 확인하고, `WakeInstance` 뒤 스텝 전 스냅샷에서 그 점이 0개, 스텝 뒤 다시 나오고 SLEEPING이 없는지 본다.
- **드로 규약:** 캡슐 `HalfHeight`는 반지름을 포함한다: `0.5 × Height + Radius` (DebugDrawImplicitObject.cpp:62 [V]). 박스 `Size`는 half extent다: `0.5 × Extents` (DebugDrawImplicitObject.cpp:73-76 [V]). `PtDebugDrawCmd::Extent`가 이 두 규약을 따른다.
- 네이티브 드로는 `FChaosDDScene` + `Solver->SetDebugDrawScene` + `FChaosDDContext::SetIsDebugDrawEnabled(true)` + `IChaosDDRenderer` 구현(`RenderLatentCommand` 포함)이다. **Development DLL에서만** 가능하다 (`CHAOS_DEBUG_DRAW`). Test DLL은 스냅샷 요청에 `PT_E_UNSUPPORTED`를 돌려준다 (SP4). 그래서 Test DLL은 `PT_CAP_JOINT_SNAPSHOT`을 선언할 수 없고, 그 DLL의 J1 Pass는 조인트 스냅샷을 확인하지 않는다 (§10.1 M13 (b)). 접촉 캡처는 안전을 위해 SingleThread + SingleWorkerPhysics에서 한다.
- **네이티브 드로 v1.1 규칙 (M9, PhysX·Bullet 구현으로 확정, [Abi.md](Abi.md) §9.2):** viz run(`PT_RUN_VIZ`)에서만 그린다. bench·behaviour run의 캡처는 명령 0개다. 그리기가 결과를 바꾸면 안 된다 (G11 `-G11Knob=viz`가 bench child와 viz child의 해시를 프레임마다 비교하고, viz child는 `-Instrument=native_debug_draw -CaptureEvery=1`로 매 프레임 캡처한다). `ColorBGRA`는 FColor 메모리 배치를 uint32 하나로 읽은 값(packed 0xAARRGGBB)이므로 브리지는 `FColor::ToPackedARGB()`를 그대로 쓴다. 정점마다 색이 다르면 첫 정점 색. 자세는 Chaos가 그린 시점의 것이다 (어느 시점인지 브리지 주석에 적는다). 문자열은 `TextOffset`/`TextLength`로 문자열 풀을 가리킨다. 잘림은 앞에서부터 `MaxNativeCommands`개 + `bTruncated`. `NativeDrawCategories`는 백엔드 정의(0 = 기본 집합): 브리지가 Chaos 드로 CVar 범주를 고르는 데 쓸 수 있고, 쓰면 뜻을 이 문서와 `FidelityNotes`에 적는다. 벤치는 `-RunMode=Viz -Instrument=native_debug_draw`로 뷰어 없이 `native_draw.csv`(프레임별 타입 수, `h_draw`)를 남긴다.
- **`IChaosDDRenderer` → `PtDebugDrawCmd` (타입별 필드, M9 수정 라운드 2, [Abi.md](Abi.md) §9.2 표와 같다):** 브리지의 렌더러 구현은 `Render*` 호출 하나를 명령 하나로 옮긴다. 쓰지 않는 필드는 0, 그때 `Rotation`은 (0, 0, 0, 1)이다.

  | `IChaosDDRenderer` 호출 | `PtDebugDrawCmd` |
  |---|---|
  | `RenderPoint(Position, Color, PointSize, Lifetime)` | `PT_DD_POINT`, `A` = Position, `Size` = PointSize(px) |
  | `RenderLine(A, B, Color, LineThickness, Lifetime)` | `PT_DD_LINE`, `A`, `B`, `Thickness` |
  | `RenderArrow(A, B, ArrowSize, …)` | `PT_DD_ARROW`, `A` → `B`(끝), `Size` = ArrowSize(cm) |
  | `RenderCircle(Center, Axes, Radius, …)` | `PT_DD_CIRCLE`, `A` = Center, `Size` = Radius, **`Rotation` = 정규직교 `Axes`의 `FQuat`** (원은 `Rotation`의 Y·Z가 펼치는 평면, UE `DrawDebugCircle`의 행렬판처럼 `Axes`의 Y·Z가 원을 펼친다; `Axes`에 스케일이 있으면 단위 축으로 바꾼 뒤 만든다) |
  | `RenderSphere(Center, Radius, …)` | `PT_DD_SPHERE`, `A` = Center, `Size` = Radius |
  | `RenderCapsule(Center, Rotation, HalfHeight, Radius, …)` | `PT_DD_CAPSULE`, `A`, `Rotation`(축 = Z), `Extent` = (Radius, HalfHeight, 0) - HalfHeight는 반지름을 포함한다 |
  | `RenderBox(Position, Rotation, Size, …)` | `PT_DD_BOX`, `A`, `Rotation`, `Extent` = Size(half extent) |
  | `RenderTriangle(A, B, C, …)` | `PT_DD_TRIANGLE`, `A`, `B`, `C` |
  | `RenderString(TextLocation, Text, Color, FontScale, bDrawShadow, Lifetime)` | `PT_DD_STRING`, `A`, 문자열 풀의 `TextOffset`/`TextLength`, `Size` = FontScale, `bDrawShadow` |
  | 공통 | `ColorBGRA` = `Color.ToPackedARGB()`, `DepthPriority` = SDPG, `Reserved` = 0, **`Lifetime` = 0**(이 캡처에서만; Chaos가 넘기는 수명은 버린다 - 호스트는 마지막 캡처를 매 프레임 다시 그리고, 저장소의 PhysX·Bullet도 0을 쓴다) |

  `RenderLatentCommand`(옛 `FLatentDrawCommand`)는 그 타입에 맞는 줄로 옮긴다.
- **G3와 DLL 구성:** G3는 `PT_CAP_CONTACT_POINTS`가 있어야 돈다. capability가 없어 Skip하면 `tolerances.json`에 Chaos의 `allowed_skip`이 없으므로 **실패**다 (Skip은 Null만 등록되어 있다, [Abi.md](Abi.md) §8). 그래서 G3는 Development DLL(뷰어용)로 돌리거나, Test DLL이 `CHAOS_DEBUG_DRAW` 없이 접촉을 캡처해 `PT_CAP_CONTACT_POINTS`를 선언해야 한다 (`FPBDCollisionConstraint`의 매니폴드 점과 `FManifoldPointResult`는 디버그 드로와 무관하게 읽을 수 있다 [R]). 브리지의 `ProvisionalRevision`이 호스트의 `PT_ABI_PROVISIONAL_REVISION`과 다르면 호스트가 `PT_CAP_CONTACT_*`를 없는 것으로 보므로 같은 실패가 난다 (M9에서 리비전이 오르면 브리지를 다시 빌드한다).

### 10.1 조인트 (v1.1 확정, [Abi.md](Abi.md) §9.4)

- **capability:** `PT_CAP_JOINT_BALL`, `PT_CAP_JOINT_HINGE`, 조인트 스냅샷을 내면 `PT_CAP_JOINT_SNAPSHOT`. 브리지는 `ProvisionalRevision` = `PT_ABI_PROVISIONAL_REVISION` 3(`abi-v1.1`에서 동결)으로 빌드한다.
- **만드는 법:** `PtConstraintCreateInfo`를 `FConstraintInstance`로 되돌려 UE가 하는 그대로 만든다: Type BALL → `ConstraintUtils::ConfigureAsBallAndSocket`, HINGE → `ConfigureAsHinge` (twist = 프레임 X), 프레임은 `Frame1`/`Frame2`(actor 공간, 스케일 1), `bDisableCollision` = `PT_JOINTF_DISABLE_COLLISION`. 나머지 `FConstraintProfileProperties`는 UE 기본값 그대로 둔다 (bEnableProjection true 등, ConstraintInstance.cpp:179-200 [V]): 조인트 의미는 UE 동등성이고, SDK 엔진에는 projection이 없으므로 그 차이는 by-design이다. `FChaosEngineInterface::CreateConstraint(Body1, Body2, Frame1, Frame2)`가 `SetPhysicsBodies({ Body1, Body2 })`로 Body1을 particle[0]에 둔다 (ChaosEngineInterface.cpp:1389-1392 [V]). Body2가 `PT_INDEX_NONE`(월드)이면 UE처럼 kinematic end point가 생기고 Frame2는 월드 자세다 (:1395-1461 [V]).
- **Kit이 하는 것:** 인자 검사(Body1 바디, Body1 ≠ Body2, 해제한 바디, flag, Reserved 0, 단위 프레임)와 capability 검사, 조인트 인덱스(`GetNumJoints`). 브리지는 `IPhysicsBackendScene::CreateConstraints`에서 검증된 배치를 만들기만 하고, 실패하면 아무것도 남기지 않는다 (전부 아니면 전무).
- **해제:** `ReleaseActors`가 그 바디의 조인트를 함께 해제한다 (UE의 `TermConstraint`). 인덱스는 재사용하지 않는다.
- **스냅샷:** `PtJointSnapshot` = 스텝 끝 자세의 world 프레임 둘, Body1(child)에 준 linear 충격량(kg·cm/s)과 **조인트 앵커(`Frame1World` 원점) 둘레의 angular 충격량**(kg·cm²/s, v1.1). 선형 충격량이 앵커에 작용하므로 각 충격량은 각 구속 행의 couple이다. **어디서 읽는가 (M9 수정 라운드 1, [V]):** UE 기본 `FConstraintInstance`는 `bUseLinearJointSolver` true(ConstraintInstance.cpp:195, `FPBDJointConstraints` 기본 `bUseLinearSolver` true, PBDJointConstraints.cpp:361)라 조인트는 `FPBDJointCachedSolver`로 풀린다. 그 `GetNetLinearImpulse`/`GetNetAngularImpulse`는 위치 충격량(충격량 × dt, 주석 "NOTE: This is a positional impulse", PBDJointCachedSolverGaussSeidel.h:329-330, :377-378)이고 각 쪽은 회전 구속 lambda만 더하므로 couple이다. 브리지는 이것을 바로 읽지 말고 **솔버 결과로 저장된 값**을 읽는다: `ScatterOutputImpl`이 `LinearImpulse = −GetNetLinearImpulse() / Dt`, `AngularImpulse = −GetNetAngularImpulse() / Dt`로 바꿔 넘긴다("Particle order was reversed in the solver", PBDJointContainerSolver.cpp:271-274, `SetSolverResults` :283). 이미 /Dt와 부호 반전이 된 값이다. Net*Impulse를 바로 내면 60 Hz에서 60배 작고 부호가 반대라 J1의 충격량 규약(m g dt, −50 m g dt)이 실패한다. [R: 저장된 값이 어느 particle 쪽(Body1 = particle[0])인지는 M13에서 확인.] (전에는 기본 경로가 아닌 `FPBDJointSolver`, PBDJointSolverGaussSeidel.cpp:1342, :1370, :1458, :1523을 인용했다.) Body1의 COM 둘레 값을 내면 안 된다: 게이트 J1의 충격량 규약(월드 hinge 끝에 수평으로 붙든 1 kg·100 cm 막대: m g dt 위, couple −50 m g dt)이 실패한다. COM 둘레 값을 앵커로 옮긴다면 **솔버가 쓴 지레**(스텝 시작 자세의 pivot − COM)로 옮긴다: 스텝 끝 자세로 옮기면 움직이는 조인트가 가짜 couple을 내고 J1의 움직이는 ball 검사(흔들림 바닥에서 couple ≤ 1e-3 × \|L\| × 앵커-COM 거리)가 실패한다 (M9 수정 라운드 0, Bullet). 프레임은 actor 공간이다: COM이 actor 원점이 아닌 바디(COMNudge·compound)에서 COM 공간 프레임을 쓰면 J1의 COM 넛지 막대가 실패한다. 깨어 있는 dynamic 바디가 없는 조인트는 0 + `PT_JOINTSNAP_SLEEPING`, 둘 다 모르면 NaN + `PT_JOINTSNAP_IMPULSE_NA`, 각 충격량만 모르면 그것만 NaN + `PT_JOINTSNAP_ANGULAR_IMPULSE_NA`(v1.1). 힘을 내는 API면 스텝 dt를 곱한다. 디버그 드로에서 Chaos의 a = particle[1] = Body2, b = particle[0] = Body1이다.
- **확인 (M13 (b)):** **물리 게이트 J1**을 다른 물리 게이트와 같은 프로세스에서 돌린다: `PhysicsTestBench -Conformance -Backend=Chaos -ChaosDll=<dll> -Profile=UEParity` / `Normalized` (각각 새 프로세스, `conformance.json`의 `J1`이 Pass여야 한다; `-Gates=J1`로 따로 돌릴 수도 있다). J1은 선언한 조인트 타입마다 M6a 5° hinge·ball(stretch ≤ 0.5 cm, hinge 축 오차, 반대편으로 넘어감, 스냅샷 프레임 = 호스트 값, 움직이는 조인트의 couple 0, 해제), COM이 actor 원점이 아닌 막대(actor 공간 프레임), **충돌 스위치**(M9 수정 라운드 2: 6 cm 겹친 두 정육면체를 이은 조인트 - UE 기본 `bDisableCollision` false면 쌍이 충돌해 벌어지고, `PT_JOINTF_DISABLE_COLLISION`이면 겹친 채다. 브리지가 스위치를 뒤집으면 여기서 실패한다; 전에는 M6b/M6c의 혼돈계 차이로만 드러났을 것이다), hinge 외팔보의 충격량 규약을 보고, sleep을 켠 프로필(UEParity)에서는 **잠든 조인트**(두 고리를 `PutInstanceToSleep`하면 두 레코드가 `PT_JOINTSNAP_SLEEPING`과 정확히 0인 충격량)도 본다 ([Abi.md](Abi.md) §9.4 13). M9의 `PhysicsTest.Joint.BackendContract`·`SnapshotImpulseConvention`은 automation exe 안에서 레지스트리 SDK 엔진만 돌리므로(NO_REINIT DLL은 빠진다) Chaos의 확인이 될 수 없었다 - 수정 라운드 0에서 J1로 옮겼고 doctest는 SDK 엔진의 빠른 경로로 J1을 부른다. 그 밖에 `PhysicsTest.Bench.PreflightSweep`(조인트 씬 preflight), M6a/M6b/M6c 거동 씬(03 M9: 5° 주기 오차 < 0.5%, 체인 신장 보고, 에너지 비발산), A4의 조인트 음성 사례.
- **조인트를 구현하기 전 (M13 (a)):** 조인트 capability를 하나도 선언하지 않는다. 그러면 조인트는 호스트 전용이고, 벤치가 조인트 씬을 `unsupported`로 거부하며 러너가 실패로 세지 않는다. J1은 호스트 전용 경로를 확인하고 Skip하며 `tolerances.json` `J1.allowed_skip`의 Chaos 항목으로 통과한다 (M13 (a)는 조인트가 필요 없다). `PhysicsTest.Bench.PreflightSweep`도 `-Describe`가 선언한 capability로 판정하므로 Chaos를 sweep 대상에 넣어도 조인트 씬 거부는 허용·목록화된다. **한 타입이라도 선언하면** J1은 그 타입의 사례를 돌리고 Skip할 수 없다 (선언하지 않은 타입은 호스트 전용인지 확인만 한다). **M13 (b)의 "조인트 씬이 돌고 조인트 스냅샷이 나온다"는 J1 Pass 하나로는 확인되지 않는다 (M9 마감, 검토 지적).** J1은 `PT_CAP_JOINT_SNAPSHOT`이 없어도 통과한다: 그때 스냅샷 프레임 검사, 움직이는 조인트의 couple, 충격량 규약, 잠든 조인트 사례는 노트로만 남는다 ("no PT_CAP_JOINT_SNAPSHOT - snapshot checks not run", "no PT_CAP_JOINT_SNAPSHOT: the impulse convention is not checked", "sleeping joint: no PT_CAP_JOINT_SNAPSHOT - PT_JOINTSNAP_SLEEPING is not checked"). §10대로 Test DLL은 스냅샷 요청에 `PT_E_UNSUPPORTED`를 내므로, Test DLL 브리지의 J1 Pass는 스냅샷 없이 M13 (b)를 "확인"할 것이다. 그래서 M13 (b)의 확인은 다음 넷을 **모두** 보인다: (1) 브리지가 `PT_CAP_JOINT_BALL`·`PT_CAP_JOINT_HINGE`·`PT_CAP_JOINT_SNAPSHOT`을 선언한다 (`-Describe`의 capability). 곧 J1을 Development DLL로 돌리거나, `CHAOS_DEBUG_DRAW` 없이 조인트 솔버 결과(`SetSolverResults`가 저장한 값, 위 "어디서 읽는가")를 읽어 스냅샷을 내는 Test DLL로 돌린다 [R: Test 구성에서 그 값을 읽을 수 있는지는 M13에서 확인]. (2) UEParity와 Normalized 두 `-Conformance` 프로세스 모두 `conformance.json`의 `J1`이 Pass다 (잠든 조인트 사례는 sleep을 켠 UEParity에서만 돈다). (3) 두 J1 보고의 노트에 위 "no PT_CAP_JOINT_SNAPSHOT" 줄이 하나도 없다 (스냅샷 사례가 모두 돌았다). (4) 거동 매트릭스의 M6a/M6b/M6c run이 `unsupported`가 아니고 `scene_desc.json`의 조인트가 `simulated`다.
- **SDK 엔진과 다를 것 (by-design, M9 거동 표에 태그):** Chaos 브리지는 UE 기본값(`bEnableProjection` true 등)으로 만든다. 세 SDK 엔진에는 projection이 없다 (`joint.projection` 0). M9에서 SDK 엔진은 M6c(100:1)에서 신장이 크고(Jolt 35 cm, PhysX PGS 발산, Bullet 수 m) Chaos PBD + projection과 비교가 이 씬의 목적이다 ([Progress.md](Progress.md) M9 발견).

---

## 11. 통과해야 할 게이트 (M13 완료 기준 (a))

| 게이트 | 내용 | 실행 방법 |
|---|---|---|
| A0~A7 | lifecycle, 버전, 스레드, phase, 입력 검증, 용량, FP, effective 키 | `PhysicsTestAutomation -tc=PhysicsTest.Abi.SdkBackendsA0toA7`은 게이트마다 Init/Term하므로 **`PT_MODULE_NO_REINIT` 모듈은 프로세스당 첫 게이트만 통과한다.** 대신 게이트마다 새 프로세스로 `PhysicsTestBench -Conformance -Backend=Chaos -ChaosDll=<dll> -Gates=A0` (A1 … A7)를 돌린다. `NO_REINIT` 모듈에 ABI 게이트 둘 이상이나 ABI·물리 게이트를 섞은 목록은 벤치가 거부한다 (종료 코드 2). `PhysicsTest.Bench.Conformance`가 NO_REINIT 시험 대역(`PT_NULL_DLL_NO_REINIT=1`인 NullDll)으로 이 경로 전체(A0~A7, A10, 물리 게이트, 거부되는 목록)를 시험한다 (M6 수정 라운드 1) |
| A9 | export는 `PT_GetBackendApi` 하나, import는 CRT + 시스템 + UE 런타임 | `Tools/check_modules.py` (UE 런타임 허용 목록은 M2.5/M13에서 더한다) |
| A10 | Native 프레임 모델: `NumStepsTaken`(과 보고하면 `NumPushedSteps`)이 `FStepPlanner`와 같다 | `-Gates=A10` (프로세스 하나). A10은 모듈을 한 번만 Init하고 (a) 원시 ABI 부분과 (b)~(d) FPhysScene 부분이 그 초기화를 함께 쓴다 (M6 수정 라운드 1에서 고쳤다. 첫 판은 (a)가 Init·Term한 뒤 FPhysScene이 다시 Init해 `NO_REINIT` 브리지에서 실패했을 것이다) |
| G1~G7, G9, J1, G12 | 물리 게이트 (§12의 기대값). J1(조인트 계약)은 조인트 capability가 없으면 등록된 Skip이다 (§10.1, M13 (b)에서 Pass). v1.0 계약 중 `SdkBackend.*` 단위 시험이 지키던 것(속도 쓰기의 깨우기, 깬 쌍의 유지 점 제외, flag 규칙)은 그 시험이 Chaos를 돌지 않으므로(`SdkBackendNames`가 Chaos를 뺀다) G6·G3가 지킨다 | `-Conformance -Backend=Chaos -Profile=UEParity` / `Normalized`, 각각 새 프로세스. `conformance.json`이 남는다. **G3는 접촉을 캡처하는 DLL로**(§10). `-Gates=G12`만 골라도 앞의 게이트를 모두 돌린 뒤 G12만 낸다 |
| G0 | 빌드 자기보고, `.rsp` 기준 | M13 (`check_modules.py`의 `.rsp` 파서) |

- 하네스 쪽 준비는 끝나 있다: 게이트 스위트(`Source/Runtime/PhysicsTestConformance/Private/Gates`), 허용치 파일, 벤치 `-Conformance`, Null 기준 결과.
- G 게이트는 A 게이트와 다른 프로세스에서 돌린다 (A 게이트가 Term한 `NO_REINIT` 모듈은 다시 Init할 수 없다). 물리 게이트는 한 프로세스에서 한 번의 Init으로 돈다 (`RunPhysicsGates`는 init 옵션이 같으면 모듈을 다시 열지 않는다).
- 브리지 전에 경로만 보려면 `PT_NULL_DLL_NO_REINIT=1`로 `-Backend=NullDll`을 돌린다 (§13).
- **Skip은 등록된 엔진만 통과한다.** Skip이 결과로 나오면 `RunPhysicsGates`가 `tolerances.json`의 `gates.<Gate>.allowed_skip`을 보고, Chaos 항목이 없으면 Fail로 바꾼다 (`PhysicsTest.Gates.SelectionRules`). Chaos는 충돌·접촉·sleep을 모두 가지므로 Skip을 등록할 이유가 없다.

---

## 12. 게이트별로 Chaos에서 예상되는 값

[Scenes.md](Scenes.md)의 게이트 씬 정의와 M6 측정값([Progress.md](Progress.md) M6 §2)을 기준으로 한다. 엔진 동작이 정한 값(G4 반발 속도 모델, G7 회전 모델)은 Chaos 소스로 먼저 확인해 **첫 실행 전에** `tolerances.json`에 `"engine": "Chaos"` 재정의와 이유를 등록한다. 실행에서 처음 드러난 편차는 원인을 엔진 소스에서 찾은 뒤에만 등록하고 `"registered": "post-run"`을 단다 (M6의 PhysX G4·G5, Null G7이 그랬다. 리드 확인 대상). 허용치를 결과에 맞춰 넓히지 않는다. 가능하면 허용치 대신 모델(식)을 등록한다 (G4·G7).

| 게이트 | 기준 | Chaos 메모 |
|---|---|---|
| G1 | z60 501.83328 ± 2e-3 cm, v60 −980.00005 ± 1e-2 | Native driver는 double `InternalDt`로 스텝한다. FixedStep 1/60에서는 `dt_engine`과 같다 |
| G1b | 선언한 공식(canonical 1)의 기준과 1e-3 cm | Chaos `Integrate`는 v += a·dt 다음 damping이다 (PBDRigidsEvolutionGBF.cpp:928-934 [V]) |
| G2 | Δv, ω 1e-4, 질량·관성 1e-5 (상대), COM 1e-5 cm, 캡슐 75/25 ± 0.5 cm | 질량을 다시 계산하지 않는지가 핵심이다 (§5) |
| G3 | step 1 접촉: normal·Z > 0.999, Phi < 0, 점이 각 표면 위(narrow phase 자세 기준), flag 규칙. UEParity: 깬 상자의 유지 점 제외 | **`ShapeContactPoints`는 셰이프 local 점이다** (ContactPoint.h:47, 법선 `ShapeContactNormal` :50, Phi :53 [V]). 그대로 복사하면 G3가 실패한다. 계약 3(PointOn은 그 스텝의 narrow phase 자세 기준)대로: PointOn0/1 = `Constraint.GetShapeWorldTransform0/1().TransformPosition(ManifoldPoint.ContactPoint.ShapeContactPoints[0/1])`, Normal = `GetShapeWorldTransform1().TransformVectorNoScale(ShapeContactNormal)`, Phi = `ManifoldPoint.ContactPoint.Phi` (PBDCollisionConstraint.h:689-690 [V]). **이 변환은 예측 자세 P, Q다:** Chaos는 `Integrate` 뒤에 narrow phase를 돌리고(PBDRigidsEvolutionGBF.cpp:581, :650 [V]) 셰이프 변환을 `GetLeafWorldTransform` = `GetTransformPQ`로 정한다 (ShapeInstance.cpp:460-462, :548-556, ParticlePairMidPhase.cpp:505, :741 [V]). 그래서 S04 step 1의 PointOn0은 스텝 시작 자세의 표면보다 g·dt² = 0.272 cm 아래다. G3는 스텝 시작 자세와 자유 운동 예측 자세 X0 + (v0 + g·dt)·dt를 둘 다 받고 씬 전체에 맞는 쪽을 골라 노트에 적는다 ("narrow-phase pose of step 1: the predicted pose (Chaos convention)", M6 수정 라운드 2. 전의 G3는 스텝 시작 자세만 받아 이 식이 0.05 cm 표면 검사에서 언제나 실패했을 것이고, 이 절의 첫 판은 이 식이 통과한다고 잘못 적었다). 두 점을 스텝 시작 자세로 옮기면 Phi = (P0 − P1)·n 검사가 깨지므로 옮기지 않는다. Chaos 자신의 `DrawCollisions`는 스텝 뒤 actor 변환(`GetActorWorldTransform`, ChaosDebugDraw.cpp:1434-1435, :1462-1466 [V])을 쓰므로 그 식도 따르지 않는다. 파티클 순서는 §10의 정규화로 맞춘다 (Body0 = dynamic). ACTIVE = `bIsActive`, `NormalImpulse`는 push-out 포함 (§10). 접촉을 캡처하는 DLL이어야 한다 (Skip은 실패). 이 식은 M13에서 확인할 때까지 [R]이다 (C-8) |
| G4 | e = −v_after / v_before가 등록한 반발 속도 모델의 예측 ± 0.02이고 다른 모델보다 가깝다. apex 비는 접촉 스텝 뒤 높이에서 시작한 이산 비행 ± 0.02 | `G4.restitution_velocity_model`: 0 = 접촉 스텝 전 속도에 −e (Jolt·Bullet: 0.80000), 1 = 그 스텝의 중력을 더한 솔버 전 속도에 −e (PhysX: 0.82105 = 0.8 × (1 + g·dt/\|v\|)). Chaos가 어느 쪽인지(반발 목표를 정하는 속도가 `Integrate` 뒤인지) M13에서 소스로 확인하고 **첫 실행 전에** 등록한다 [R] `S05` v2(M6 수정 라운드 2)는 step 38 끝이 바닥 위 0.01 cm다: 예측 자세로 충돌하는 Chaos에서 정확한 접점(Phi = 0 ± 반올림)이면 반쯤의 확률로 step 38에 push-out과 반발이 나서 `contact_frame_slack` 0을 어길 것이었다. 0.01 cm면 step 38의 예측 자세는 떨어져 있고 push-out이 없어 velocity solve가 돌지 않으므로(PBDCollisionSolver.h:1178-1181 [V]) 접촉은 step 39에 시작해야 한다. 그래도 첫 실행에서 접촉 프레임이 38이면 원인(투기적 접촉 처리)을 소스로 확인한 뒤 `contact_frame_slack`을 Chaos 재정의로 등록한다 (C-9) |
| G5 | 정지 거리 250.947 cm ± 1%, 30° 경사 μ 0.6 정지 / 0.55 미끄러짐 | 정지는 "가속 없음"(±2 cm/s²), 변위 0.1 cm (frame 20), 위치로 잰 creep 속도 0.75 cm/s (frame 10-20, 110-120)로 판정한다. PhysX PGS는 등속 creep(2.5 cm/s, 속도 반복 수가 아니라 PGS 마찰 성질) 때문에 변위·속도 허용치가 post-run으로 등록되어 있고 리드 확인 대기다 (Progress M6 D-4). Chaos는 기본 허용치로 통과해야 한다 |
| G6 | Normalized 600 프레임 동안 잠든 바디 0, UEParity `S08` 재운 바디가 생성 직후부터 Sleeping, 그다음 속도 쓰기가 깨움 | §4 sleep 끔, §5 7단계, §7. nearly zero 쓰기 둘은 `command.nearly_zero_velocity_wakes` = 0이면 잠든 채 (§9) |
| G7 | UEParity 62.83 ± 0.01 rad/s, Normalized 해제, 스텝 회전각 모델 | **Chaos의 회전 적분은 1차 쿼터니언 갱신이다:** q' = normalize(q + dt/2·(w,0)·q) (ChaosCore Rotation.cpp:158-162 [V]). 한 스텝 회전각은 2·atan(\|w\|·dt/2)이다. `G7.step_angle_model`을 1로 등록한다 (Null과 같다) |
| G9 | READBACK 키 = 리졸브된 값, 모든 READBACK 키 등록 | §9 |
| G12 | 모든 프레임의 엔진 오류 bit 0 | Chaos 오류(ensure, 용량)를 어떤 bit로 낼지 정하고 `errors.capacity_bits`로 용량 bit를 알린다 |

---

## 13. Null 백엔드를 템플릿으로 쓰는 법

`Source/Backends/NullBackend`는 SDK 없는 기준 백엔드이고 Chaos 브리지의 출발점이다 ([Abi.md](Abi.md) §7).

가져올 것:
- 진입점 매크로 `PT_IMPLEMENT_BACKEND_MODULE(FChaosBridgeBackend)`와 `ModuleName`, `GetInfo` 채우기 (capability, module flag, 필수 키, `BuildFlagsJson`).
- `InitGamePhys`의 옵션 거부, `CreateScene`의 씬 옵션 거부, `CreateMaterials`.
- UE 순서의 `CreateActors` 뼈대 (§5의 7단계가 Null에서는 몇 줄로 줄어 있다).
- `SetUpForFrame`/`StartFrame`/`WaitPhysScenes`/`EndFrame`의 phase와 `NumStepsTaken`/`NumPushedSteps` 세기 (Null은 `FNullFramePlanner`로 UE5 규칙을 흉내 낸다. Chaos는 실제 솔버 콜백으로 센다).
- `ApplyBodyCommand`의 명령 분기와 v1.0 깨우기 계약 (§7).
- `DumpEffectiveSettings`의 일반 키 25개와 `body.damping_formula`·`command.nearly_zero_velocity_wakes` canonical (Chaos는 후자를 0으로 바꾼다).
- `NO_REINIT` 경로 시험: Null DLL은 `PT_NULL_DLL_NO_REINIT=1`이면 `PT_MODULE_NO_REINIT`를 선언하는 시험 대역이 된다 (NullBackendDll.cpp, Kit이 Term 뒤 Init을 거부). 브리지를 만들기 전에 `PhysicsTestBench -Conformance -Backend=NullDll -Gates=A10`처럼 같은 명령줄을 돌려 볼 수 있다.

버릴 것: Null의 적분기, `FNullFramePlanner`, 해제 바디 배열 (Chaos 파티클로 대신한다).

---

## 14. 하지 말 것 / 자주 틀리는 것

- `PT_GetBackendApi(1)` 말고 다른 major에도 표를 돌려주기 (A1 실패).
- 질량·관성 재계산, 셰이프 COM 사용 (G2).
- 머티리얼 없는 셰이프 (sleep 임계가 CVar 기본값으로, G6·거동 비교).
- `AsyncDt = 0` (async가 켜진다).
- SingleThread 모드만 켜고 `p.Chaos.SingleWorkerPhysics`를 빠뜨리기 (G10b, M10).
- CDO SolverOptions에 기대기 (§4).
- 하네스 입력을 READBACK으로 표시하기 (G9).
- 디버그 드로 HalfHeight에 반지름을 빼먹기, 박스 Size에 full extent 넣기.
- `RequestExit`, `SetStandaloneSolver(true)`, `FreeLibrary`, 정상 종료 경로에서 UE 정리 (§2).
- 엔진 스레드에서 호스트 API 부르기 (로그 콜백만 허용, [Abi.md](Abi.md) §5.1).

---

## 15. 열린 항목 (M13에서 정한다)

| # | 항목 | 메모 |
|---|---|---|
| C-1 | `NumStepsTaken`을 세는 정확한 지점 [R] | 솔버 스텝 콜백 횟수 권장 |
| C-2 | Chaos 반발 속도 모델 (G4 `restitution_velocity_model` 0/1) | 접촉 스텝 중력 전/후. 소스로 확인해 첫 실행 전에 등록한다 |
| C-7 | Test DLL의 접촉 캡처 (G3) | `CHAOS_DEBUG_DRAW` 없이 `PT_CAP_CONTACT_POINTS`를 낼지, G3만 Development DLL로 돌릴지 [R] |
| C-3 | MXCSR: PreInit 뒤 FTZ/DAZ를 UE 값으로 둘지 호스트 값으로 둘지 (A6 경고) | M2.5 SP2 |
| C-4 | `check_modules.py`의 UE 런타임 허용 목록과 `.rsp` 파서 (A9, G0) | M2.5 H 작업 |
| C-5 | Chaos 엔진 오류의 bit 배치와 `errors.capacity_bits` | G12 |
| C-6 | 스냅샷·네이티브 드로 구조체 | M9에서 확정 (v1.1, [Abi.md](Abi.md) §9.2): 브리지는 규칙대로 채운다 (§10) |
| C-8 | G3 narrow phase 자세와 접촉 점 식 (M6 수정 라운드 2) | 브리지의 PointOn0/1이 예측 자세 P, Q의 셰이프 표면 위인지, G3 노트가 "the predicted pose (Chaos convention)"를 고르는지 첫 G3 실행에서 확인한다. 정규화(§10)와 `NormalImpulse` = (`NetImpulse` + `NetPushOut` / Dt)·n의 부호도 함께 본다 [R] |
| C-9 | `S05` 0.01 cm 틈에서의 Chaos 투기적 접촉 (G4) | step 38 끝 Phi = +0.01 cm에서 Chaos가 반발을 일찍 내지 않는지. 내면 원인을 소스로 확인하고 `G4.contact_frame_slack` Chaos 재정의를 첫 실행 전에 등록한다 [R] |
| C-10 | Chaos `-Describe`의 stdout 순수성 (M7 추가) | `Bench.Conformance`의 `-Describe` 검사에 Chaos 항목을 더한다 (§2 stdout) |
| C-11 | Chaos의 할당 수 (M7 수정 라운드 0) | 벤치 exe의 `operator new` 훅은 exe 이미지 안에서만 묶인다: DLL인 브리지는 자기 `operator new`와 `FMemory`(GMalloc)로 할당하므로 manifest `alloc.tick_new_calls` 0은 Chaos에 대해 아무것도 말하지 않는다 (`alloc.hook_scope`가 그렇게 적는다). 브리지는 GMalloc 래퍼(호출 수만 세는 `FMalloc` 위임자)를 InitGamePhys에서 두고 `GetMemoryStats`의 `NumAllocs`/`NumFrees`로 낸다. 그러면 `alloc.backend_allocs_in_loop`가 SDK 엔진과 같은 뜻이 된다 ([TimingContract.md](TimingContract.md) §4.1). 래퍼가 엔진 동작을 바꾸지 않는지(같은 할당자에 위임) M13에서 확인한다 [R] |
