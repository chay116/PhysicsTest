# 타이밍 계약 (Timing Contract)

> **작성일:** 2026-10-22 (M7)
> **상태:** M7 측정 프로토콜의 정본이다. M8에서 러너(반복·셔플·쿨다운·재시도, §5.2)와 경미한 정정을 더했다. M10에서 TSC 버킷과 run 전체 QPC 보정·배치 측정·ABI 오버헤드 게이트(§2.1), 스레드 센서스·cpu_eff·G10b(§7.1), 헤드라인 조건 11~13(§6), 적합성 스탬프와 비공식 판정(§9), 러너의 스탬프 확인·CV 게이트·sentinel(§5.2)을 더했다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §9, §12, §13 · [03_WorkPlan.md](03_WorkPlan.md) M7, M10 · [ResultsSchema.md](ResultsSchema.md) · [Scenes.md](Scenes.md) · [Abi.md](Abi.md) · [Progress.md](Progress.md) M7, M10
> **근거 표기:** **[V]** 직접 빌드·실행해 확인 · **[R]** 추정

이 문서는 벤치 run 하나가 무엇을 재고, 어디서 재고, 어떤 조건에서 그 수치를 헤드라인 후보로 인정하는지 정한다. 엔진별 버킷 내용(§3)과 헤드라인 자격(§6)이 핵심이다. **M7의 모든 수치는 비공식이다** (§9).

---

## 1. 한 run = 한 프로세스

`PhysicsTestBench.exe` 프로세스 하나가 씬 하나를 백엔드 하나에서 한 번 돈다. 실행 순서는 02 §12.2를 따른다.

| 단계 | 내용 | 타이밍 |
|---|---|---|
| 1 | 인자 파싱, run 디렉터리, Fatal 훅(Fatal이면 `exit.json`에 3을 쓴다) | - |
| 2 | 호스트 제어 (§5): affinity, HIGH_PRIORITY_CLASS, power throttling opt-out, `timeBeginPeriod(1)`. SDK 초기화와 LoadLibrary보다 먼저 | - |
| 3 | ini 계층 리졸브 (Base → 프로필 → 변형 → 명령줄) | - |
| 4 | 백엔드 로드와 `InitGamePhys`. M9: 씬이 요구하는 capability(조인트)가 모듈에 없으면 초기화 **전에** 거부한다 (종료 코드 2, status `unsupported`, `exit.json`만) | `phases_ns.backend_init` |
| 5 | 스레드 센서스 #1 (M10): `InitGamePhys` 바로 뒤 이 프로세스의 스레드 목록(Toolhelp32)과 각 스레드의 cycle. 엔진이 초기화에서 만든 워커(Jolt job system, PhysX dispatcher, M13의 Chaos TaskGraph)가 여기서 보인다 (manifest `threads.census_init`, §7.1) | - |
| 4b | 엔진 옵션 preflight (바디 없는 시험 씬) | `phases_ns.option_preflight` |
| 6 | 씬 구성: BuildScene → Flush(M9: 강제 오버라이드, 조인트 생성) → `desc_sha256`. M9: 상태를 기록하는 run(`-StatesEvery`)은 여기서 `scene_desc.json`을 쓴다 (프레임 0 전, 타이밍 밖) | `phases_ns.scene_create` |
| 7 | effective 덤프와 preflight: 필수 키, G0, G9, G10a ([ResultsSchema.md](ResultsSchema.md) §2 gates). M9: `preflight_diff.json` (G9 규칙의 기대값 대 엔진 값, 키마다) | `phases_ns.preflight` |
| 8 | `PrepareForSimulation` (모든 엔진에 같은 비타이밍 기회: Jolt `OptimizeBroadPhase`·temp 블록 pre-fault, PhysX scratch pre-fault) | `phases_ns.prepare` |
| 9 | spin pre-roll: T개 CPU 전부에서 0.5 s (`-PreRoll=`) | `phases_ns.pre_roll` |
| 10 | 센서스 창의 시작(frame 0 바로 앞, pre-roll 뒤), frame 0, 이어서 측정 루프. M10: 60 프레임마다 프레임 사이에서 TSC/QPC 보정 쌍 하나 (§2.1) | 프레임마다 버킷 (§2) |
| 11 | 스레드 센서스 #2 (M10): 마지막 프레임 바로 뒤. 시작과 #2 사이가 센서스 창이다 → cpu_eff, 외부 스레드 cycle, G10b (§7.1). 마지막 보정 쌍과 run 보정(§2.1) | - |
| 12 | 결과를 모두 쓴다. 마지막에 `exit.json` | - |
| 13 | teardown: 씬 해제, `TermGamePhys` → `teardown.json`. UE_RUNTIME 모듈이면 `TerminateProcess` | `teardown.json.teardown_ns` |

- 5·11단계(스레드 센서스 #1, #2)는 M10에 채웠다. 센서스 창은 #1이 아니라 frame 0 바로 앞에서 시작한다: 씬 구성과 pre-roll의 cycle은 측정 루프가 아니다 (pre-roll은 도우미 스레드를 쓴다). #1은 엔진이 초기화에서 만든 스레드의 기록이고, 창의 각 스레드는 시작 때 있었는지(`new`), 끝까지 있었는지(`exited`)로 표시된다. 엔진 옵션 preflight는 02 §12.2의 4단계와 6단계 사이라 4b로 적는다 (M8에서 번호를 고쳤다. M7 판은 5로 적어 센서스 자리와 겹쳤다).
- preflight가 실패하면 8~10단계를 건너뛰고 종료 코드 2로 끝난다. 출력은 그대로 쓴다. 엔진 옵션 preflight(4b단계)가 거부하면 씬이 없으므로 씬에서 나오는 manifest 필드는 null이고 `scene.built`가 false다. 이때도 13단계(teardown)는 돈다.
- teardown의 실패는 종료 코드를 바꾸지 않는다. `exit.json`이 이미 쓰인 뒤이므로 `teardown.json`에만 남는다. **Fatal도 같다 (M7 수정 라운드 0):** Fatal 훅은 12단계의 `exit.json`을 쓰기 전까지만 `exit.json`(3, `fatal`)을 쓴다. 그 뒤의 Fatal(예: `TermGamePhys`의 `checkf`, UE 모듈이 넘긴 Fatal)은 `teardown.json`의 `fatal`에 기록하고, 프로세스는 run의 종료 코드로 `TerminateProcess`된다 (DL52, CTest `PhysicsTest.Bench.ExitTeardownFatal`). 훅은 run이 어느 경로로 끝나든 해제된다.
- UE_RUNTIME 모듈이 로드된 프로세스의 Fatal은 `exit.json`(`terminate_process: true`)을 쓴 뒤 바로 `TerminateProcess`로 끝난다. CRT 종료 경로(`_Exit` = ExitProcess)는 DLL의 `DLL_PROCESS_DETACH`를 부르므로 쓰지 않는다 (`PhysicsTest.Bench.UeRuntimeFatal`).
- `-Conformance`도 run 디렉터리를 먼저 만든다. 게이트 전에 멈춘 경우(백엔드 없음, 설정·허용치 오류)도 `exit.json`이 남는다. M10: `-Conformance`는 G10b를 자식 run 하나(`M3.BoxPile` N = 1000, 300 프레임, T = 1, pre-roll 0.5 s, `-HostCheck=record`)로 확인하고, 전체 스위트가 시험 주입 없이 끝나면 적합성 스탬프를 쓴다 (§9.1).

## 2. 클럭과 버킷

### 2.1 클럭

| 항목 | 값 (M10) |
|---|---|
| 버킷 소스 | **TSC** (M10): `FCycleCounter::Read` = `__rdtscp` 뒤 `_mm_lfence`. rdtscp는 앞의 명령이 끝난 뒤 읽고, lfence는 뒤의 명령이 읽기 전에 시작하지 않게 한다 (02 §9.3). 버킷 경계마다 한 번 읽는다. M7~M9의 버킷은 QPC(`FPlatformTime::Cycles64`)였다 |
| invariant TSC | CPUID 0x80000007 EDX bit 8. 아니면 TSC는 시계가 아니므로 헤드라인 자격이 없다 (§6 조건 12). 이 PC는 invariant, 3.494396 GHz [V] (`clock.tsc_invariant`, `clock.tsc_hz`) |
| run 전체 QPC 보정 | (TSC, QPC) 쌍을 호스트 제어 뒤, pre-roll 앞뒤, 측정 루프에서 60 프레임마다(프레임 사이, 버킷 밖), 루프 뒤에 읽는다. 쌍 하나 = QPC 한 번을 사이에 둔 TSC 두 번의 중간값(TSC, QPC)이다 (`FCycleCounter::ReadPair`). 두 TSC 사이(spread)가 가장 좁은 시도를 쓰고, 두 번째 시도부터는 spread가 지금까지 가장 좁은 값의 4배 안이면 그 자리에서 멈춘다 (인터럽트·문맥 전환이 끼지 않은 깨끗한 쌍. 최대 16번, M10 수정 라운드 0에서 문구를 코드에 맞췄다). `ns_per_tick` = (Q_n − Q_0) / (T_n − T_0) × 10⁹ / qpc_hz (첫 쌍과 마지막 쌍의 2점 보정). 잔차 = 모든 쌍의 \|(Q_i − Q_0) / (Q_n − Q_0) − (T_i − T_0) / (T_n − T_0)\| 최댓값이고 기준은 < 1e-5 (03 M10). 관측: 창 ≥ 0.15 s인 run 702개의 최대 5.4e-7 (G10b CTest의 300 프레임 run 3.6e-8~9.8e-8), CTest의 60 ms짜리 짧은 run까지 넣은 3781개(여덟 프리셋의 CTest와 증거 세션)의 최대 2.1e-6 (잔차의 바닥은 QPC 눈금 100 ns / 창 길이다) [V] (`clock.calibration`: valid, pairs, span_s, max_residual, rms_residual, max_pair_spread_ticks) |
| ns 변환 | 루프 동안은 프로세스의 첫 보정(QPC 20 ms 창, `clock.ns_per_tick_provisional`)으로 적고, 루프 뒤 run 보정(`clock.ns_per_tick`)으로 모든 프레임의 버킷을 다시 변환한다. `timings.csv`의 ns 열과 summary는 run 보정 값이다. 원시 TSC 틱(`frame_tsc_ticks`, `tick_tsc_ticks`, `tsc_end`)도 남는다 |
| QPC | 10 MHz, 눈금 100 ns [V] (`clock.qpc_hz`). 프레임마다 Tick 전체를 QPC로도 읽어 원시 틱(`frame_qpc_ticks`, `qpc_end`)을 남긴다: TSC와 독립인 두 번째 시계다 |
| 읽기 오버헤드 | 20000회 읽기 32묶음의 중앙값. TSC 약 13.4 ns, QPC 약 12.2 ns [V] (`clock.tsc_read_overhead_ns`, `clock.read_overhead_ns`). 호스트 제어 뒤에 잰다. 버킷 하나는 경계 읽기 둘이다 |

TSC 눈금은 약 0.29 ns라 M7의 QPC 눈금(100 ns) 문제는 없어졌다. 그래도 µs 아래의 작은 씬(`P0.EmptyStep`, `P1.SmallScene` 10)에서는 버킷 경계마다의 읽기(약 13 ns)가 상대적으로 크므로 **배치 측정**을 같이 적는다 (02 §9.3, M7 D-6): 윈도 첫 프레임의 Tick 시작부터 마지막 프레임의 Tick 끝까지 TSC 한 구간을 프레임 수로 나눈 값이다 (`summary.windows.<w>.batch_frame_ns`). 버킷마다의 경계 읽기와 반올림이 없고, 프레임 사이의 벤치 일(행 기록, 60 프레임마다 보정 쌍)이 들어간다. 프레임마다 Tick 전체(Tick 앞뒤 TSC 둘, `tick_ns`)도 적는다. `T_frame`과 `tick_ns`의 차이가 Tick 안 하네스 일, `batch_frame_ns`와의 차이가 프레임당 하네스 바닥이다. 헤드라인 값은 여전히 윈도의 `T_frame` 평균이다 (§3). **`batch_frame_ns`는 하네스 바닥의 지표이지 더 나은 P0 추정이 아니다** (M10 수정 라운드 0): 프레임마다 약 +190 ns(행 기록과 보정 쌍)가 들어가 Jolt P0에서 +18%, Bullet P0에서 약 +60%다. P0 헤드라인의 흔들림은 짧은 창(0.6~0.8 ms: 인터럽트 하나가 run 평균을 몇 % 움직이고 G10b는 언제나 Skip)에서 오므로, 답은 더 긴 P0 run(창 ≥ 50 ms, 수만 프레임)이다 (M11 전에 정한다).

**ABI 오버헤드 게이트 (M10):** `PhysicsTestBench -AbiOverhead [-Backend=Null]`가 Null 백엔드, 바디 0 씬에서 엔진 프레임 4호출(`SetUpForFrame` → `StartFrame` → `WaitPhysScenes` → `EndFrame`)을 20000 프레임씩 TSC 한 구간으로 재어(배치) 31묶음의 프레임당 중앙값을 낸다 (stdout JSON `physicstest.abi_overhead/1`). 기준은 중앙값 < 100 ns/frame이다 (03 M10). test 41.3 ns/frame(호출당 10.3 ns) [V]. Debug 빌드(dbg)는 값을 기록만 한다 (`gate: recorded`). CTest `PhysicsTest.Bench.AbiOverhead`(모든 프리셋), 단위 시험 `PhysicsTest.Abi.CallOverheadNull`(같은 함수, Release에서 < 100 ns 단언).

### 2.2 버킷 정의

`FPhysScene::Tick` 안에서만 잰다 (02 §9.1, §9.3).

| 버킷 | 덮는 것 | 비고 |
|---|---|---|
| `T_push` | `ApplyBodyCommands` 한 번. 명령이 없는 프레임은 호출하지 않고 0 | Harness driver는 스텝마다, Native driver는 프레임마다 |
| `T_step` | 엔진 프레임 4호출: `SetUpForFrame` → `StartFrame` → `WaitPhysScenes` → `EndFrame` | 하위 버킷 `setup`/`start`/`wait`/`end` |
| `T_pull` | `ReadBodyStates(PT_READ_ACTIVE_ONLY)` 한 번과 `FBodyInstance::State` 기록 | 프레임당 1회 |
| `T_frame` | ΣT_push + ΣT_step + T_pull | 엔진 비교는 반드시 이것으로 한다 |
| `frame_tsc_ticks` | 같은 구간의 TSC 원시 틱 합 (M10) | ns 변환 전 값 |
| `tick_ns`, `tick_tsc_ticks` | `FPhysScene::Tick` 전체 (앞뒤 TSC 둘, M10). 버킷과 Tick 안의 하네스 일(델리게이트, 프레임 계획) | 보조 |
| `frame_qpc_ticks` | M10: 같은 프레임의 Tick 전체를 QPC로 잰 원시 틱 (Tick 앞뒤 QPC 둘). M7~M9는 버킷 QPC 틱의 합이었다 | TSC와 독립인 대조 |

버킷 밖(타이밍에 들어가지 않음): PreTick·Step·PostTick 델리게이트와 씬의 `OnPreTick`, 프레임 계획(`FStepPlanner`), 깨어 있는 바디 수 세기, 그리고 §4의 기록 작업 전부.

### 2.3 엔진별 버킷 내용 [V]

| 엔진 | `SetUpForFrame` | `StartFrame` | `WaitPhysScenes` | `EndFrame` | `T_pull` |
|---|---|---|---|---|---|
| Jolt | dt 저장, 중력이 바뀌면 `SetGravity` | 쌓아 둔 임펄스·힘 적용 + `PhysicsSystem::Update(dt, 1, temp, jobs)` | 없음 | update error를 오류 bit로 | NoLock + `GetActiveBodiesUnsafe` + 직전 프레임 활성 목록 |
| PhysX | dt 저장, 중력이 바뀌면 `setGravity` | 쌓아 둔 명령 적용 + `simulate(dt, nullptr, scratch, 262144)` | `fetchResults(true)` | 이벤트(viz·contacts tier만), error callback bit | `getActiveActors` (SdkDefault는 `bEnableActiveActors=False`라 전체 순회) |
| Bullet | dt 저장, 중력이 바뀌면 `setGravity` | 쌓아 둔 명령·kinematic 목표 + `stepSimulation(dt, 0, dt)` (내부 pre-tick: UE 순서 감쇠, 속도 한계, 조인트가 있으면 조인트마다 스텝 시작 지레 캐시(M9 수정 라운드 0, O(조인트)); 내부 post-tick: 각속도 한계 집계, 풀 사용량, **UEParity(`SleepThresholds=UE`, sleep 켬)에서는 dynamic 바디마다 UE sleep 카운터 에뮬레이션(M9, O(N_dynamic))**) | 없음 | post-tick 집계, 풀 fallback bit | COM TM × inv(ComLocal), `isActive` |
| Null | (Native driver) UE5 프레임 계획 | 스텝 적분 | 없음 | 스텝 수·오류 bit | 전체 상태 복사 |
| Chaos (M13) | `FChaosScene::SetUpForFrame` (중력은 UE처럼 매 프레임) | `StartFrame` | `WaitPhysScenes` | `EndFrame` (PT→GT pull, SQ 갱신, 이벤트 flip 포함) | GT API X/R/V/W |

- Chaos의 `T_step`에는 GT 동기화가 들어가고 SDK 엔진의 `T_step`에는 없다. 그래서 `T_step`은 보조 열이며 "Chaos와 직접 비교 불가"로 표시한다 (02 §9.3). 엔진 간 비교는 `T_frame`만 한다.
- bench 모드에서 금지되는 것: Jolt ContactListener·ActivationListener·DebugRenderer·profiler, PhysX `eNOTIFY_*`·event callback·PVD, Bullet debug drawer. **preflight가 실제로 확인하는 것 (M8 정정, M7 검토):** G10a는 백엔드가 받은 `PtSceneDesc`(RunMode Bench, InstrumentFlags 0), effective 키 `listeners.installed`(Jolt는 어댑터가 설치한 접촉 리스너 — 엔진의 `PhysicsSystem::GetContactListener()`가 아니라 어댑터의 멤버에서 읽는다, PhysX는 시뮬레이션 이벤트 콜백, Bullet은 전역 접촉 콜백 수), `*.debug_drawer`(Bullet만 실제 드로어가 있다)를 본다. Jolt DebugRenderer·profiler는 빌드 사실이라 G0(Jolt 설정 문자열)가 막는다. Jolt ActivationListener와 PhysX PVD는 어댑터가 설치하지 않으며 어느 키로도 확인하지 않는다 (M9의 네이티브 드로·조인트 작업에서 READBACK 키를 더할 후보).

## 3. 윈도와 헤드라인 값

- 윈도는 씬이 선언한다 ([Scenes.md](Scenes.md)). 예: `M3.BoxPile` headline 1-599, impact 1-179, settled 300-599. `M2b.Pyramid1240` headline 1-499.
- frame 0은 모든 윈도에서 빼고 `summary.frame0_frame_ns`로 따로 보고한다 (02 §9.5).
- run 하나의 값은 윈도의 `T_frame` 평균이다 (`summary.windows.<name>.frame_ns.mean`). 중앙값, p95, p99, 최솟값, 최댓값도 같이 쓴다 (nearest-rank).
- 헤드라인 수치는 fresh process run들의 윈도 평균의 중앙값이다 (T = 1). 반복·셔플·쿨다운은 M8 러너가 한다. `-Repeat=`, `-Order=`, `-BlockSeed=`는 manifest `order`에 그대로 남는다.
- 워밍업 스텝은 없다 (02 §9.5). 대신 `PrepareForSimulation`과 spin pre-roll이 있다.

## 4. 측정 루프의 위생

### 4.1 루프 안에서 하지 않는 것

- `Tick` 안에서는 할당, I/O, 로그를 하지 않는다 (02 §9.3).
- **할당 카운팅 훅 (03 M7):** `PhysicsTestBench.exe`가 전역 `operator new`/`delete`를 모두 교체한다 (`AllocationHook.cpp`). 하네스, Kit, 정적 어댑터, SDK의 `operator new` 호출이 모두 세어진다. 벤치는 프레임마다 `Tick` 앞뒤에서 카운터를 읽어 `alloc.tick_new_calls`(타이밍 버킷 안 할당 수 합)를 기록한다. 0이 아니면 그 run은 헤드라인 자격이 없다 (§6).
- **훅의 범위:** 교체한 `operator new`는 exe 이미지 안에서만 묶인다. DLL 백엔드(Null DLL, Chaos)는 자기 `operator new`나 `FMemory`(GMalloc)로 할당하므로 훅에 보이지 않는다. 그런 run의 `tick_new_calls` 0은 그 백엔드에 대해 아무것도 말하지 않는다. manifest `alloc.hook_scope`가 이를 적는다. Chaos는 M13에서 GMalloc 래퍼로 `PtMemoryStats`에 할당 수를 낸다 ([ChaosBackendGuide.md](ChaosBackendGuide.md) §15 C-11).
- SDK가 자기 할당기로 하는 할당(Jolt `JPH::Allocate` 계열, PhysX `PxAllocatorCallback`, Bullet `btAlignedAlloc`)은 `operator new`를 지나지 않는다. 백엔드가 `PtMemoryStats.NumAllocs`로 그 호출 수를 낸다: PhysX는 할당자 콜백, Jolt와 Bullet은 M7 수정 라운드 0부터 어댑터가 SDK의 공개 훅(`JPH::Allocate`·`Reallocate`·`AlignedAllocate` 등의 함수 포인터, `btAlignedAllocSetCustom`)에 다는 세는 래퍼다. 래퍼는 SDK 기본 구현과 같은 CRT 함수를 같은 순서로 부르고 원자 카운터만 더하므로 결과를 바꾸지 않는다 (G8 해시가 그대로다). Null은 자기 배열의 할당을 센다. 벤치는 frame 0 직전, frame 0의 Tick 뒤, 마지막 Tick 뒤에만 읽어(프레임 사이, 타이밍 밖) `alloc.backend_allocs_in_loop`, `backend_allocs_frame0`, `backend_allocs_after_frame0`에 쓴다. 엔진 할당은 엔진 거동이므로 기록만 하고 헤드라인 조건에 넣지 않는다 (§6 조건 8은 하네스 훅 값이다).
- 관측값 [V] (dev, `M3.BoxPile` 1k 60 프레임): Jolt 119회(frame 0에 1), Bullet 128회(frame 0에 14), PhysX UE4Compat 2086회(frame 0에 161). 세 엔진 모두 프레임마다 조금씩 할당한다.
- **양성 대조 (M8, M7 검토):** 시험 전용 `-InjectTickAllocation=<f>`는 frame f의 `Tick` 안(씬의 PreTick 델리게이트, `FPhysScene::Tick` 안이라 훅이 세는 구간)에서 `operator new`를 한 번 한다. 훅이 그것을 센다: `tick_new_calls` 1, `tick_first_allocating_frame` f, 헤드라인 자격 없음 (CTest `PhysicsTest.Bench.AllocHookPositiveControl`). 그래서 다른 모든 run의 0은 "보지 못해서 0"이 아니다.

### 4.2 프레임 사이의 기록 작업

기록기는 프레임 0 전에 버퍼를 잡거나 파일을 연다. 기록은 `Tick`이 끝난 뒤, 버킷 밖에서 한다. 타이밍·해시 행은 메모리에 모았다가 run 끝에 쓰고, `states.ptst`와 `contacts.ptcn`은 4 MiB stdio 버퍼로 이어 쓴다.

| 기록 | 언제 | 헤드라인 run |
|---|---|---|
| `timings.csv` 행 | 매 프레임 (미리 잡은 배열) | 있음 |
| `hashes.csv` | `HashEvery` 프레임과 마지막 프레임 | 마지막 프레임 한 줄만 (`HashEvery = 0`) |
| `GetStepStats` 카운터 | `CountersEvery` 프레임 (counters tier면 기본 1) | 없음 (`CountersEvery = 0`) |
| `states.ptst` | `StatesEvery` 프레임과 마지막 프레임 | 없음 |
| `contacts.ptcn` | contacts tier에서 `CaptureEvery` 프레임 (기본 1) | 없음 |
| `native_draw.csv` (M9) | native_debug_draw tier에서 `CaptureEvery` 프레임 (기본 1) | 없음 |

"N 프레임마다"는 (f + 1) % N == 0인 프레임 f와 마지막 프레임이다 ([ResultsSchema.md](ResultsSchema.md) §1). 1만 바디의 해시나 카운터 패스는 1 MB 넘게 건드려 다음 프레임의 캐시를 식히므로 (02 §9.1), 마지막 프레임 해시 외의 기록이 하나라도 있으면 헤드라인 자격이 없다.

### 4.3 계측 tier

`-Instrument=counters,phases,alloc,contacts,native_debug_draw` (`PT_INSTR_*`). 하나라도 켜면 `timing_valid_for_headline = false`이고 사유에 tier가 적힌다. 거부하지는 않는다 (03 M7).

| tier | M7 동작 |
|---|---|
| counters | `GetStepStats` 카운터를 `timings.csv`의 contact_pairs, contact_points, islands 열에 쓴다 |
| phases | 백엔드가 `PT_CAP_PHASE_TIMERS`를 선언하면 매 프레임 `GetStepStats`를 읽어 phase timer의 합을 `solver_ns` 열에 쓴다 (counters 열은 `CountersEvery`를 따른다). SDK 백엔드는 선언하지 않는다. Null DLL 시험 대역(`PT_NULL_DLL_PHASE_TIMERS=1`, 타이머 `null.steps`)으로 경로를 확인한다 (CTest `PhysicsTest.Bench.InstrumentPhases`, manifest `recorders.phases`). Chaos 솔버 수준(02 §9.7)은 M13이다 |
| alloc | 백엔드 쪽 할당 계측을 켜는 자리다 (M7에서 백엔드 동작은 같다. 할당 훅은 모든 run에서 켜져 있다) |
| contacts | 백엔드가 접촉 listener를 설치하고 (Jolt, PhysX; Bullet은 캡처 때 매니폴드를 읽는다) 벤치가 `contacts.ptcn`을 쓴다 |
| native_debug_draw | 백엔드에 전달한다 (viz run에서 PhysX는 visualization 파라미터를 켜고 Bullet은 드로어를 붙인다). M9부터 벤치가 `CaptureEvery` 프레임마다 `PT_SNAP_NATIVE_DRAW`를 캡처해 `native_draw.csv`(타입별 수, `h_draw`)를 쓴다. 렌더링은 없다 ([ResultsSchema.md](ResultsSchema.md) §7.1). 캡처는 프레임 사이, 버킷 밖이다 |

## 5. 호스트 제어

프로세스 단위로만 바꾼다. 시스템 설정은 바꾸지 않는다 (03 §2, 01 Q9).

| 제어 | 방법 | manifest |
|---|---|---|
| affinity | `SetProcessAffinityMask`. 뒤에 만들어지는 엔진 워커가 물려받는다 | `threads.affinity_mask`, `affinity_applied`, `affinity_before` |
| 우선순위 | `HIGH_PRIORITY_CLASS` | `platform.priority_set`, `priority_before`, `priority_class` |
| power throttling | `SetProcessInformation(ProcessPowerThrottling)`: EXECUTION_SPEED와 IGNORE_TIMER_RESOLUTION을 제어하고 끈다 | `platform.throttling.opt_out_applied` |
| 타이머 해상도 | `timeBeginPeriod(1)`. UE DLL이 프로세스 전역으로 하므로 모든 엔진에 똑같이 (WindowsPlatformMisc.cpp:1076) | `platform.timer_resolution` (전후 `NtQueryTimerResolution`). `current_ms`는 시스템 전체의 현재 주기다. Windows 10 2004부터 `timeBeginPeriod`는 프로세스별이라 `after`가 이 프로세스의 1 ms 요청을 증명하지는 않는다. 이 프로세스의 요청은 `applied`(`timeBeginPeriod`의 반환값)다 (M8 정정, M7 검토) |
| 전원 계획 | 읽어서 기록만 한다 | `platform.power_scheme`, `power_scheme_guid`, `power_scheme_high_performance` |
| CPU MHz | `CallNtPowerInformation(ProcessorInformation)`으로 run의 CPU만 읽는다 | `platform.cpu_mhz` (하이브리드 Intel은 정격값을 내기도 한다) |

spin pre-roll은 run의 마스크에 든 CPU마다 스레드 하나를 0.5 s 돌린다 (호출 스레드는 가장 낮은 CPU에 잠시 고정, 나머지는 도우미 스레드). 코어를 idle 상태에서 꺼내기 위해서다 (02 §9.5).

### 5.1 오염된 호스트 (종료 코드 4)

- 측정 구간: spin pre-roll 시작부터 마지막 프레임까지.
- 측정 방법: TSC cycle 단위로 정확하게 잰다. CPU마다 바쁜 cycle = 경과 TSC − `QueryIdleProcessorCycleTime`이다. 이 프로세스의 cycle은 `QueryProcessCycleTime`으로 읽는다.
- **run의 코어** = 마스크가 닿는 물리 코어의 논리 CPU 전부, 곧 마스크 CPU와 그 SMT 형제다 (`host_load.core_mask`, `FProcessorTopology::CoresOf`). 이 PC의 T = 1이면 CPU 0과 1이다.
- `core_busy_other_pct` = (run 코어의 논리 CPU들의 바쁜 cycle 합 − 이 프로세스 cycle) / 경과 TSC × 100. 단위는 "코어 하나의 %"다. 이 프로세스는 affinity 때문에 마스크 CPU에서만 돌므로 그 cycle을 빼면 나머지는 모두 다른 일이다. 인터럽트와 DPC도 "다른 것"으로 센다.
- 함께 기록하는 값: 마스크 CPU만의 `mask_busy_other_pct`, SMT 형제만의 `smt_sibling_busy_pct`, 시스템 전체에서 이 프로세스를 뺀 `system_busy_other_pct`, run 코어의 CPU별 바쁨(`per_cpu`, `in_mask`).
- **판정:** `core_busy_other_pct` > `threshold_total_pct` = `-ContaminationPct`(코어당, 기본 5: 02 §12.2 "코어 1개의 5%") × run의 코어 수(`host_load.cores`)이면 오염이다. `-HostCheck=enforce`(기본)이면 종료 코드 4, `record`이면 기록만 하고 헤드라인 자격을 뺀다.
- 왜 시스템 전체가 아닌가: M8 러너의 유휴 기준(시스템 전체 3% 미만)은 이 PC에서 코어 0.6개에 해당한다. 시스템 전체를 5% of core로 판정하면 사용 중인 데스크톱에서는 거의 모든 run이 4가 된다. run을 실제로 방해하는 것은 자기 코어에 끼어든 일이다.
- 왜 SMT 형제를 넣는가 (M7 수정 라운드 0): 형제 논리 CPU의 부하는 같은 코어의 실행 자원을 나눠 써 run을 느리게 한다. 첫 판은 마스크 CPU만 판정해서, CPU 1(형제)을 100% 바쁘게 해도 `contaminated` false, `timing_valid_for_headline` true였다 (독립 검사). 지금은 그 run이 종료 코드 4다 (CTest `PhysicsTest.Bench.ExitContaminatedSibling`: 형제 99.999%, run 코어 100.19% > 5%).
- 왜 코어 수에 비례하는가 (M7 수정 라운드 0, Progress.md M7 §3 D-2의 기본값): 첫 판은 T개 CPU의 합을 코어 1개의 5%와 비교해, T가 클수록 같은 배경 부하에서도 4가 났다 (이 PC에서 PhysX BoxPile 10k T = 2~6이 끝까지 돈 뒤 4). 코어마다 같은 5%를 허용하면 T와 무관한 기준이 된다. **리드 결정 (M8):** 이 문턱을 그대로 둔다. 사용 중인 데스크톱에서 T ≥ 2 PhysX run의 약 10%가 끝까지 돈 뒤 4로 끝나는 것은(M7 독립 검사: 20 run 중 2, 문턱의 55~115%) 측정이 아니라 러너가 다룬다: 종료 코드 4인 run을 fresh process로 최대 2번 다시 돌리고, 모든 시도가 4이면 셀을 `contaminated`로 표시한다 (§5.2). M8 러너는 `threshold_total_pct`와 `core_busy_other_pct`를 그대로 읽는다.
- 관측값 [V] (test, 측정 프로토콜, Progress.md M7 §7): 사용 중인 이 데스크톱에서 혼자 돈 T = 1 run은 run 코어(CPU 0, 1)의 다른 부하가 1.4~2.8% of core다 (CPU 0 0.5~1.1%, 형제 0.8~1.7%). BoxPile 10k T = 2..6은 Jolt 5.2~10.1%, PhysX UE4Compat 8.4~17.6%로 문턱(10~30%) 아래다 (첫 판의 규칙이면 PhysX T = 2~6이 모두 4였다). 같은 CPU 0을 HIGH 우선순위로 도는 도우미 프로세스를 붙이면 약 50% of core로 종료 코드 4가 된다 (CTest `PhysicsTest.Bench.ExitContaminated`).
- CTest는 병렬 실행 때 서로의 CPU 0을 오염시키므로 `-HostCheck=record`로 돈다 (`ExitContaminated`만 enforce, RUN_SERIAL).

### 5.2 러너 (M8, `Tools/ptbench`)

- **환경 게이트 (읽기 전용):** `powercfg /getactivescheme`가 High performance나 Ultimate(GUID로 판정, 이름은 지역화된다)가 아니면 거부한다. `--allow-balanced`로만 우회하고, 우회한 세션은 리포트 머리, 모든 표 제목 밑의 조건 줄, 모든 그림 제목 위의 조건 줄로 표시된다 (M8 수정 라운드 1: 처음에는 머리에만 있어 따로 떼어 본 PNG에는 표시가 없었다). 유휴: 시스템 전체 바쁨 < 3% (2 s 창). 창을 하나씩 계속 보며 하나라도 통과하면 된다: 최소 5창, 그리고 `--idle-wait`(기본 120 s) 동안 (M8 수정 라운드 0: 이 데스크톱은 2.5~4.9%를 오가 처음의 "최대 5창"은 시작을 세 번 중 한두 번 거부했다). 기다린 시간과 모든 창이 `session.json` `environment.idle`에 남는다. 다른 PhysicsTest 프로세스가 없어야 한다. 실패한 검사는 `--allow-busy`, `--allow-concurrent`로 우회할 수 있다. 시험용 `--skip-idle`(유휴를 재지 않음)도 유휴 검사의 우회로 기록된다. 모든 우회는 `session.json` `bypasses`와 리포트 머리에 굵게 남는다. 리포트 머리는 세션 파일 하나가 아니라 모든 입력에서 만든다: 각 세션의 게이트와 각 run manifest의 `platform.power_scheme_high_performance` 중 하나라도 거짓이면 표시한다 (run 디렉터리만으로 만든 리포트, 여러 세션, 세션 도중의 전원 계획 변경). 시스템 설정은 바꾸지 않는다.
- **순서:** 반복 r마다 모든 셀을 한 번씩, seed 고정 셔플 순서로 돈다 (splitmix64 + Fisher-Yates, [ResultsSchema.md](ResultsSchema.md) §11). run마다 fresh process, 그 전에 쿨다운 2 s(그동안의 시스템 바쁨을 기록), 타임아웃이면 프로세스를 죽인다. 벤치에 `-Repeat`, `-Order`, `-BlockSeed`, `-Session`, `-Out`을 넘긴다.
- **판정은 `exit.json`이다:** 원시 프로세스 종료 코드는 기록만 한다. `exit.json`이 없으면 그 run은 끝까지 가지 못한 실패다(`no_exit_json`, 또는 `timeout`). 시험 전용 `-InjectSilentExit=<f>`(frame f 뒤 `TerminateProcess(0)`, 출력 없음)로 확인한다: 원시 코드 0인데 실패로 남는다 (CTest `PhysicsTest.Bench.InjectSilentExit`, `PhysicsTest.Py.Integration`).
- **종료 코드 4:** 같은 run을 쿨다운 뒤 fresh process로 최대 `contaminated_retries`(기본 2)번 더 돈다. 시도마다 `index.csv` 한 줄과 `session.json` runs[] 항목이 남는다. 모든 시도가 4이면 그 반복과 셀이 `contaminated`다.
- **CV:** 셀마다 ok이고 `timing_valid_for_headline`이 참인 run의 윈도 평균으로 CV(표본 표준편차 / 평균)를 보고한다 (리포트의 validate와 같은 run). ok지만 헤드라인 자격이 없는 run(dev·dbg, `-HostCheck=record`로 기록된 오염 등)은 값이 없고 `n_not_headline_valid`로 센다. 권위 있는 CV는 리포트의 것이다.
- **스탬프 확인 (M10, R9):** 러너는 run을 하나라도 띄우기 **전에** 매트릭스의 모든 (exe, 백엔드, 프로필, 변형, 오버라이드) 조합에 맞는 스탬프를 `Saved/Conformance/`에서 찾아 지금의 파일로 확인한다 (§9.1, `ptbench/stamps.py`). 하나라도 없거나(missing), 키 필드가 다르거나(stale), 무효(invalid)이면 거부한다: 종료 코드 3, `session.json` status `refused`, `refusal_reason`과 셀별 `stamps.cells`. 이 정책(`require`)은 perf 매트릭스의 기본이다. determinism·behavior 매트릭스는 기본이 `record`다 (M10 수정 라운드 0): 모든 셀을 돌리고 셀마다 스탬프 상태를 기록만 한다. R9는 보고 가능한 타이밍의 조건이고, 거동 매트릭스의 프로브 타깃(물리를 바꾸는 `-ini:` 오버라이드)은 게이트를 통과하지 못할 수 있어 유효 스탬프가 없을 수 있다. 매트릭스의 `"stamps"`가 정책을 정하고 `--require-stamps`가 `require`를 강제한다. 우회: `--skip-unstamped`(스탬프 없는 셀을 `unstamped`로 남기고 나머지만 돈다), `--allow-unstamped`(모두 돌되 run은 비공식). 둘 다 `bypasses`에 남는다. 스탬프는 `python -I -m ptbench stamp --matrix <m>`(조합마다 `-Conformance` 한 번)로 만든다.
- **CV 게이트 (M10):** R 블록 뒤 CV가 문턱(매트릭스 `cv_gate.threshold_pct`, 기본 2%)을 넘는 셀만 모아 추가 블록을 최대 `max_extra_blocks`(기본 2)번 더 돈다 (블록마다 새 seed의 셔플, 반복 번호 R, R+1). 그래도 넘으면 셀이 `noisy`다 (값과 CV는 그대로 보고하고 리포트가 ⚠로 표시한다). 추가 블록의 run은 `cv_gate: true`로 남는다. 매트릭스가 `cv_gate: false`이면 끈다.
- **sentinel (M10):** 매트릭스 `sentinel`(기본 smoke: `jolt.Default`, `M3.BoxPile` 1k, T = 1)을 세션 처음, 중간(블록 절반 뒤), 끝에 돈다 (`kind: sentinel`, 셀 값에는 들어가지 않는다. 중간 = 계획된 run의 절반 뒤). 처음 대비 중간·끝의 중앙값 drift > `drift_pct`(기본 2%)이거나 sentinel run이 하나라도 실패하면 세션 전체에 플래그(`sentinel.flagged`, `unofficial_reasons`)를 단다.
- **세션의 비공식 사유:** 전원 계획, 환경 게이트 우회, perf가 아닌 매트릭스, R < 5, 스탬프 정책(`record`, `--skip-unstamped`, `--allow-unstamped`), sentinel 없음이나 플래그가 `session.json` `unofficial_reasons`에 모이고 `unofficial`이 그 유무다. noisy 셀은 셀마다 표시한다 (`summary.noisy_cells`, 리포트 ⚠).

## 6. 헤드라인 자격 (`timing_valid_for_headline`)

다음을 모두 만족할 때만 참이다. 하나라도 어기면 거짓이고 `timing_invalid_reasons`에 사유가 하나씩 적힌다.

1. 종료 코드 0 (preflight 통과, 엔진 오류·용량 초과·오염 없음).
2. run mode가 Bench이고 계측 tier가 0 (G10a 통과).
3. 틱 모드가 FixedStep.
4. `HashEvery`, `StatesEvery`, `CountersEvery`, `CaptureEvery`가 모두 0 (마지막 프레임 해시만).
5. 호스트 제어 네 가지가 모두 적용됨.
6. spin pre-roll ≥ 0.5 s.
7. 호스트 부하가 측정됐고 오염 기준 아래 (`-HostCheck=record`로 기록만 한 오염도 자격을 뺀다).
8. 타이밍 버킷 안 할당 0 (`alloc.tick_new_calls`).
9. `PT_DO_CHECK = 0` 빌드 (test, test-avx2, M10의 test-jolt·test-physx·test-bullet 프리셋. prof도 check는 꺼져 있지만 조건 13에 걸린다). dev와 dbg는 check가 켜져 있어 자격이 없다.
10. 시험 전용 주입(`-InjectEngineErrorBits`, `-InjectPreflightFault`, `-InjectSceneInstrumentFlags`, `-InjectStatePerturbation`, M8의 `-InjectTickAllocation`, `-InjectSilentExit`, M10 수정 라운드 0의 `-InjectForeignWork`)이 없음. 사유 문장은 "a test-only injection was used"이고 주입은 `run.test_injections`에 남는다 (`BenchmarkRunner.cpp`의 헤드라인 사유 목록).
11. (M10) G10b가 Fail이 아님 (§7.1). T = 1이면 Pass이거나 own CPU 시간이 50 ms보다 짧은 창의 Skip(`short_window`: P0의 헤드라인 창, 그 백엔드는 스탬프의 G10b가 증명한다)이어야 한다. M10 수정 라운드 1부터 판정이 없는 T = 1 Skip(센서스 없음 `no_census`, 경합 창 `contended`)도 자격이 없다 (사유 "G10b gave no verdict (T = 1 single-thread proof): …"). T > 1은 Skip(`multi_thread`)이다. 종류는 `threads.census_window.g10b_skip`이다.
12. (M10) TSC가 invariant이고 run의 TSC/QPC 보정이 유효하며 그 잔차(`clock.calibration.max_residual`)가 1e-5 미만임 (§2.1, 02 §9.3). M10 수정 라운드 1부터 run마다 확인한다 (사유 "the run's TSC / QPC calibration residual … >= 1e-05"; 첫 판은 `valid`(두 쌍이 순서대로 있음)만 봤다).
13. (M10) 프로파일러 빌드가 아님 (prof 프리셋: Tracy 존, Jolt external profile). prof는 단계 분해 전용이다 (03 M10).

`Config/Schemas/manifest.schema.json`의 교차 규칙이 참일 때 이 조건을 모두 강제한다: 조건 5(호스트 제어 네 플래그 `threads.affinity_applied`, `platform.priority_set`, `platform.throttling.opt_out_applied`, `platform.timer_resolution.applied`)와 조건 9(`harness.do_check` false)는 M10에 더했다 (M8 검토). 조건 11~13은 `gates[]`의 G10b와 T = 1 `threads.census_window.g10b_skip`(`no_census`·`contended`가 아님, 수정 라운드 1), `clock.tsc_invariant`·`clock.calibration.valid`·`clock.calibration.max_residual` < 1e-5(수정 라운드 1), `harness.profiler`다.

### 6.1 preflight 게이트

| 게이트 | 확인 | 실패 시 |
|---|---|---|
| 필수 키 | 일반 키 25개 + 백엔드 선언 키 + `-RequireEffectiveKey` | 2 |
| G0 | 어댑터 TU 자기보고 = `BuildFlags.json`의 같은 어댑터 타깃 사실 = 플래그 프로필. SDK TU의 `/arch`·`/fp`·CRT·반복자 디버그 수준, PhysX 구성(release/debug), Jolt 설정 문자열에 Profile/Debug Renderer/FP Exceptions 없음, 트리 전체 위반 0, 벤치 exe TU에 `/arch` 없음 | 2 |
| G9 | 그 run 씬의 READBACK 키 전부가 `tolerances.json` 규칙의 리졸브 값과 같다. `listeners.installed`와 `*.debug_drawer`는 G10a 몫, `shape.wrapping` = 0은 성능 씬만 | 2 |
| G10a | bench 모드 tier 0 run: 백엔드가 받은 `PtSceneDesc`가 RunMode Bench·InstrumentFlags 0, `listeners.installed` 0, `*.debug_drawer` 0 | 2 |

## 7. 스레드 매핑 (02 §9.6)

- T = affinity 마스크의 논리 CPU 수다. 기본 마스크는 P코어마다 첫 논리 CPU 하나씩 앞에서 T개다: 이 PC(i5-13600KF)에서 0x1, 0x5, 0x15, 0x55, 0x155, 0x555 [V]. `-Affinity=`로 바꿀 수 있지만 CPU 수가 T와 같아야 한다.
- 엔진 매핑: Jolt 워커 T−1 (호출 스레드도 일한다), PhysX T = 1은 `FPhysXCPUDispatcherSingleThread`(inline), T ≥ 2는 `PxDefaultCpuDispatcherCreate(T)`(워커 T개, 호출 스레드는 `fetchResults`에서 기다린다고 본다 [R]), Bullet은 T = 1만 (T ≥ 2는 종료 코드 2).
- 엔진이 보고한 스레드 설정(`threads.workers` READBACK, `jolt.max_threads`, `physx.cpu_dispatcher` 등)을 manifest `threads.engine_native`, `threads.engine_thread_settings`에 T와 나란히 쓴다.
- M10부터 모든 run에 센서스 창의 cpu_eff와 외부 스레드 cycle이 붙는다 (§7.1). T = 1은 G10b가 판정하고, T > 1은 값을 S(T)·E(T) 옆에 적는다 (`threads.scaling_note`).
- 시리즈 플래그 (02 §9.6 "HT(12)와 E코어 포함(20) 시리즈는 별도로 플래그를 단다"): `threads.series`가 마스크의 모양이다. `p_cores_one_lp`(P코어마다 논리 CPU 하나, 기본 T 시리즈), `p_cores_smt`(어떤 코어의 SMT 쌍이 둘 다 마스크에 있음), `with_e_cores`(E코어가 마스크에 있음). `threads.cores`, `smt_pair_cores`, `e_cores`도 쓴다. 시리즈가 다른 run은 한 스케일링 곡선에 섞지 않는다 (M8). M10부터 시리즈는 comparability key에 들어가 표가 다르다 (02 §13).

### 7.1 스레드 센서스, cpu_eff, G10b (M10)

- **센서스:** `FThreadCensus::Capture`가 Toolhelp32로 이 프로세스의 스레드를 모두 찾고, 각 스레드를 `THREAD_QUERY_LIMITED_INFORMATION` 핸들로 열어 `QueryThreadCycleTime`(TSC 단위)을 읽는다. 측정하는 스레드(main)는 자기 cycle을 `GetCurrentThread`로 직접 읽는다. 창의 경계 하나의 카운터는 모두 함께, 열거(수십 ms) **바깥에서** 읽는다 (M10 수정 라운드 0): 센서스 #1은 열거 → idle → 브래킷 기준선 → 프로세스 총량(`QueryProcessCycleTime`) → 열거한 스레드들(잡은 핸들로 다시) → main → QPC → TSC(= 창의 시작), 센서스 #2는 TSC → QPC → main(= 창의 끝) → 센서스 #1의 스레드들(#1이 잡은 핸들로, 살아 있든 끝났든) → 프로세스 총량 → idle → 열거다. 그래서 프로세스 총량의 창이 #1 스레드들의 창을, 그 창이 main의 창을 µs 단위로 감싼다. 창 안에서 끝난 스레드는 #1의 핸들로 끝날 때까지의 cycle이 읽히고(`exited`), 창 안에서 생긴 스레드는 #2의 열거가 모든 cycle을 읽는다(`new`, 창 끝보다 열거만큼 늦게). M10 첫 판은 #1 스레드의 끝 값을 `DecideExit`에서(루프 뒤 다른 일까지 지난 뒤) 읽었고 프로세스 총량을 두 열거 바깥에서 읽어 `unattributed`가 열거 비용(창마다 약 38 ms)이었다.
- **값:** own = main + foreign (이 프로세스의 다른 스레드), `cpu_eff` = own / (wall × T − other_on_mask), `cpu_eff_raw` = own / (wall × T), `foreign_cycles_fraction` = foreign / own, `unattributed_cycles` = max(0, process − own): 어느 센서스에도 없는 스레드(창 안에서 생겨 끝남)의 cycle과 경계 읽기 자신이 남기는 몫. 후자는 측정했다 [V]: `QueryProcessCycleTime`은 한 번에 10~100 µs가 들고 그 일부가 어느 스레드의 cycle도 아닌 프로세스 cycle로 잡히며, `QueryIdleProcessorCycleTime`을 브래킷 안에서 읽으면 한 번에 약 0.3 M cycle이 더 붙는다 (그래서 idle은 브래킷 밖이다). 센서스 #1이 빈 브래킷(같은 읽기, 사이에 아무것도 없음)을 세 번 재어 중앙값을 `bracket_cycles`(약 0.05~0.4 M cycle)로 남기고, `unattributed_fraction` = max(0, unattributed − bracket) / own이다. 0으로 자르는 것은 창 안에서 생긴 스레드를 #2의 열거가 프로세스 총량보다 늦게 읽기 때문이다 (그래서 "구조로 ≥ 0"이 아니다). wall은 창의 TSC 구간, other_on_mask = 마스크 CPU의 바쁜 cycle(경과 − `QueryIdleProcessorCycleTime`) − own이다. 그래서 cpu_eff = own / (own + 마스크 CPU의 idle)이다: 1에서 내려가면 run 스레드가 잠들거나 막혔다는 뜻이고, 다른 프로세스가 run의 CPU를 뺏은 시간은 분모에서 빠진다 (그것은 §5.1의 오염 판정이 본다). `cpu_eff_raw`는 다른 부하까지 분모에 둔 값으로 함께 적는다. manifest `threads.census_window`(창 값과 스레드별 cycle), `threads.foreign_cycles_fraction`, `summary.cpu_eff`.
- **G10b (T = 1 단일 스레드 증명, 03 M10, 02 §14.3):** cpu_eff ∈ [0.98, 1.02]이고 (외부 스레드 cycle + 기준선을 뺀 unattributed) / own < 0.5%이면 Pass (M10 수정 라운드 0: 어느 센서스에도 없는 스레드도 외부 스레드다). 창의 **own CPU 시간**(`own_s` = own cycle을 창의 TSC 속도로 바꾼 값)이 50 ms보다 짧으면 판정할 수 없어 Skip `short_window`(값은 적는다): 기준선을 빼도 경계 읽기가 최대 약 0.35 M cycle(0.1 ms)을 남기고 [V], own 50 ms의 0.5%가 0.87 M cycle이다. M10 첫 판은 창 10 ms, 수정 라운드 0은 wall 50 ms였고, 수정 라운드 1에서 own CPU 시간으로 바꿨다: 0.5%는 own에 대한 비율이라, 코어를 다른 프로세스와 나눈 run(병렬 CTest의 벤치는 모두 CPU 0에 묶인다)은 wall이 50 ms를 넘어도 own이 몇 ms일 수 있다 (독립 검사가 dbg·prof CTest `-j6`에서 본 G10b Fail: own 2.8 ms / wall 98.7 ms, own 16 ms / wall 50 ms). **경합 창 (수정 라운드 1):** 다른 프로세스·인터럽트가 마스크 용량(wall × T)의 5%(`-ContaminationPct` 기본값, 종료 코드 4의 기준)를 넘게 쓴 창(`other_on_mask_fraction` > 0.05)에서 own 대비 0.5%를 넘었으면, 같은 cycle을 마스크의 바쁜 cycle(own + 다른 부하, `mask_busy_cycles`)에 대어서도 0.5%를 넘을 때만 Fail이고, 아니면 Skip `contended`(판정 없음)다. wall에 비례해 도는 스레드(prof 빌드의 Tracy 클라이언트)와 경계 읽기의 고정 잔여는 코어를 뺏긴 만큼 own 대비로 커지지만 실제 일은 그렇지 않다. 경합 창의 Pass는 그대로 Pass다 (작아진 own에 대어서도 경계를 지켰다). T > 1은 Skip `multi_thread`, 센서스가 없으면 Skip `no_census`와 그 사유다 (`threads.census_window.g10b_skip`). Fail이면, 그리고 T = 1의 `no_census`·`contended` Skip이면 헤드라인 자격이 없다 (§6 조건 11). `-Conformance`의 G10b 자식 run은 최대 세 번 돈다: own CPU 시간이 50 ms보다 짧으면(Null: 300 프레임 약 17 ms) 프레임을 own 약 0.15 s가 되게 늘리고(최대 30000 프레임), 경합 창이면 같은 프레임으로 1 s, 2 s 쉰 뒤 다시 돈다. 마지막 run도 Skip이면 그 G10b는 Fail이다 (수정 라운드 1: 유효한 스탬프는 언제나 G10b Pass를 가진다. ABI 게이트 프로세스가 Skip을 내도 Fail). 이 프로세스의 스레드만 보므로 사용 중인 데스크톱의 다른 부하가 판정을 만들지 않는다 (그 부하가 판정을 가릴 때는 경합 창의 Skip이다). CTest의 판정 시험(`Bench.G10b{Jolt,Bullet,PhysX}`, `Bench.G10bNegativeControl`)은 `RUN_SERIAL`이다. 엔진이 만든 워커(Jolt job system, PhysX dispatcher, M13의 Chaos TaskGraph 워커)가 T = 1에서 일하지 않았다는 증명이다. G10a(listener 0, 드로어 분리)는 M7 그대로다.
- **관측 [V] (test, 측정 프로토콜, `M3.BoxPile` 1k 300 프레임, T = 1):** Jolt·Bullet·PhysX UE4Compat 모두 Pass, cpu_eff 1.00000, cpu_eff_raw 0.996~0.997, 외부 0.000000%. 창의 스레드 4개 중 셋은 Windows 로더 스레드(cycle 0)다. M10 수정 라운드 0 뒤: unattributed 0.37~0.43 M cycle, 기준선 0.25~0.32 M, 남은 몫 0.008~0.014%. prof 빌드는 Tracy 클라이언트의 스레드 "Tracy Profiler" 하나가 서버를 기다리며(10 ms마다 poll) wall의 약 0.16~0.21%를 쓴다 (경합 없는 prof run 9개, M10 수정 라운드 1의 독립 검사. 수정 라운드 0이 적은 own의 0.084~0.088%는 `-PreRoll=0` 100 프레임 run 셋의 값이었다). 그 cycle은 wall에 비례하므로 코어를 뺏긴 창에서는 own 대비로 커진다 (수정 라운드 1의 경합 규칙 (`TRACY_NO_CALLSTACK`으로 시작 직후 30~55 ms를 쓰던 "Tracy Symbol Worker"가 없어졌다. `-PreRoll=0` 100 프레임 run도 Pass. 헤드라인 자격은 없다, 조건 13).
- **음성 대조 (M10 수정 라운드 0):** 시험용 `-InjectForeignWork=<ms>`는 센서스 #1 바로 뒤 도우미 스레드 하나를 띄워 자기 CPU 시간 <ms>를 돌리고 끝낸다 (센서스 #2 뒤에 join). CTest `PhysicsTest.Bench.G10bNegativeControl`(Release 20 ms, 창 약 0.3 s): 도우미는 창 안에서 끝나 어느 센서스에도 없고 unattributed로만 보인다 (약 7%) → G10b Fail, 헤드라인 사유 "G10b failed …", `unofficial` true. Debug(dbg)는 프레임이 훨씬 느려 30 프레임·200 ms로 돌고, 도우미가 센서스 #2에서 아직 돌아 새 스레드(외부)로 Fail한다.
- **한계 (M10 수정 라운드 0):** (1) cpu_eff = own / (own + 마스크 CPU의 idle)은 도는 timing 스레드에서는 구조로 약 1이다: timing 스레드가 잠들거나 막힌 경우만 잡는다 (워커가 일한 것은 잡지 못한다). 단일 스레드 증명은 외부 + unattributed 쪽이 진다. (2) 창 안에서 생겨 끝난 스레드는 이름도 스레드별 값도 없고 unattributed 합계로만 보인다. (3) own CPU 시간이 50 ms보다 짧은 창(P0의 0.6~0.8 ms 헤드라인 창)은 Skip이라 그 run은 증명이 없다: 그 백엔드의 증명은 스탬프의 G10b 자식 run이 맡는다 (수정 라운드 1부터 유효한 스탬프의 G10b는 언제나 Pass다). (5) 경합 창에서는 감도가 떨어진다 (수정 라운드 1): own이 wall의 c배이면 own 대비 0.5%/c보다 작은 외부 일은 Skip `contended`로 남는다. 그런 run은 증명이 없으므로 헤드라인 자격도 없다. (4) 지금의 Jolt·PhysX·(M13) Chaos 워커는 오래 사는 스레드라 #1·#2 둘 다에 있다.

## 8. 결정성과 결과 중립 knob

- **G8 (DET1):** 같은 exe, 같은 T에서 fresh process 3회의 마지막 프레임 `h_native`(와 하네스가 본 상태의 비트 해시 `h_state`)가 같다. `PhysicsTestBench -Gate=G8 -Runs=3 <run 인자>`가 자식 프로세스를 차례로 띄우고 각자의 `exit.json`과 manifest를 읽어 비교한다 (`gate_g8.json`).
- **G11:** 접촉 캡처를 끄고(`-Instrument=none`) 켠(`-Instrument=contacts -CaptureEvery=1`) 두 run의 모든 프레임 `h_native`와 `h_state`가 같다 (`-Gate=G11`, `gate_g11.json`). 두 run 모두 `-HashEvery=1`이라 해시 작업은 같다.
- 자식 프로세스는 `-HostCheck=record`로 돈다. 다른 프로세스의 부하는 시간을 바꾸지 상태를 바꾸지 않는다.
- **음성 대조 (M7 수정 라운드 0):** 시험용 `-InjectGateFault=<f>`를 주면 게이트가 자식 하나에만 `-InjectStatePerturbation=<f>`(frame f 뒤 마지막 동적 바디에 1e-3 cm/s 속도 변화, 프레임 사이)를 넘긴다 (G8: 마지막 run, G11: contacts on). 두 게이트 모두 차이를 찾아 종료 코드 2로 끝나야 한다 (CTest `PhysicsTest.Bench.G8NegativeControl`, `G11NegativeControl`: G11은 "hashes.csv differs at data row 6", 곧 frame 5부터 다르다).

## 9. 비공식 표시

- M10부터 manifest `unofficial`은 판정이다: 헤드라인 자격(§6), 그 run 키의 유효한 적합성 스탬프(§9.1), High performance/Ultimate 전원 계획 셋이 모두 참일 때만 false이고, 아니면 true이며 `unofficial_reasons`에 사유가 하나씩 적힌다. `timing_label`은 그 사유로 만든 문장이다 ("UNOFFICIAL: …. Not for reports." 또는 "reportable: …"). M7~M9의 manifest는 모두 `"unofficial": true`였다. 스키마가 "unofficial false ⇒ 헤드라인 자격, 스탬프 valid, High performance"를 강제한다.
- `timing_valid_for_headline = true`는 "이 run이 측정 프로토콜을 지켰다"는 뜻이지 보고할 수 있다는 뜻이 아니다.
- 보고 가능한 수치의 조건: run마다 `unofficial` false + 러너 세션의 반복·셔플(M8) + CV 게이트를 통과한 셀(noisy 아님)과 sentinel 플래그 없음(M10) + 주장 규칙(02 §13, M10). 세션의 사유는 `session.json` `unofficial_reasons`다 (§5.2).

### 9.1 적합성 스탬프 (M10, R9)

- **키:** {`bench_exe_sha256`, `backend_module_sha256`(DLL 백엔드는 DLL 파일 해시, 정적 백엔드는 ""), `variant_sha256`, `physics_sha256`, `tolerances_sha256`, `gate_suite_version`, `config_overrides`}. 마지막은 명령줄의 `-ini:`·`-ExecCmds=` 토큰을 정렬한 목록이다 (M10에 더함: 같은 설정 해시라도 명령줄 오버라이드가 다르면 다른 엔진 설정이다). 키 SHA-256 = 정렬된 키, 공백 없는 JSON(UTF-8)의 해시다. 파일은 `Saved/Conformance/<key sha256>.json` (`physicstest.stamp/1`, `Config/Schemas/stamp.schema.json`).
- **쓰기:** `-Conformance`가 게이트 스위트 전체(`-Gates=` 없이: 물리 게이트 전부와 G10b, DLL 백엔드는 ABI 게이트 A0~A7·A10도 게이트마다 새 프로세스로, M10 수정 라운드 0)를 시험 주입 없이 돌았을 때만 쓴다 (`-Gates=`로 일부만 돈 경우, 주입이 있는 경우는 쓰지 않고 이유를 결과 JSON `stamp`에 적는다). `valid` = 게이트 실패 0 그리고 `pending_sign_offs` 0 (수정 라운드 1: G10b 자식 run과 ABI 게이트 프로세스의 Skip은 Fail로 바뀌므로 유효한 스탬프의 G10b는 언제나 Pass다). 원자적으로 쓴다 (임시 파일 → 이름 바꾸기).
- **확인 (두 곳):** 벤치는 run마다 자기 키로 스탬프를 찾아 manifest `conformance_stamp`(found, valid, reason, path, key)에 적는다 (preflight 단계, 타이밍 밖). 러너는 프로세스를 띄우기 전에 exe·DLL 파일의 지금 해시, 스탬프가 기록한 설정 입력(Base·프로필·변형 ini, `tolerances.json`)의 지금 원시 해시, 셀의 오버라이드를 스탬프와 비교하고 스탬프 자신(키 해시 = 파일 이름, valid, `pending_sign_offs` 0)을 확인해 거부한다 (§5.2). 입력과 exe가 같으면 C++ 리졸버의 `variant_sha256`·`physics_sha256`도 같다. 바뀐 입력 하나(`PhysX.UE4Compat.ini`의 키 하나, Null DLL 다시 빌드, `tolerances.json`)가 그 입력을 쓰는 셀의 키를 바꾸므로 그 셀의 스탬프가 없어진다 (CTest `PhysicsTest.Py.Integration` `test_stamps_refuse_stale_cells_before_any_process`, `PhysicsTest.Bench.ConformanceStamp`).
- 스탬프는 적합성(게이트)의 증명이지 타이밍의 증명이 아니다. 같은 exe·설정이면 시간이 지나도 유효하다. exe를 다시 빌드하면(링커 타임스탬프가 바뀌므로) 스탬프를 다시 만든다. 선택 옵션 `PT_REPRO_LINK=ON`(`/Brepro`)은 같은 입력의 다시 컴파일·링크가 같은 바이트를 내게 한다. 다만 PDB를 새로 쓸 때만이다: 남은 PDB를 링커가 갱신하면 PDB age가 이미지에 들어가 SHA가 바뀐다 [V] (기본 OFF, [02_Architecture.md](02_Architecture.md) §4.1).

- **같은 씬, 다른 작업량 (M11 주의, M9 수정 라운드 1):** 헤드라인 씬 `M3.BoxPile` 1k에서 세 행은 600 프레임 윈도 안에서 서 있는 더미를 돌리지 않는다. Bullet.Default는 더미가 무너지고(Checkpoints N-1), PhysX UE4Compat-TGS는 상자를 더미 밖으로 던지며(frame 599에 86개(UEParity 77개)가 제자리에서 50 cm 넘게 벗어남, N-4), PhysX SdkDefault Normalized는 퍼지기 시작한다(frame 599에 97개, 900 프레임까지 무너짐, N-5). 이 행들의 `T_frame`은 다른 행과 같은 일을 잰 값이 아니므로 M11 표에 그 사실을 함께 적는다 (거동 리포트의 규칙 `bullet-stack-solver`, `physx-tgs-pile-ejection`, `physx-sdkdefault-pile`과 probe가 근거다). 씬 정의는 그대로 둔다 (엔진 거동이다).

## 10. 이후 확장

| 항목 | 마일스톤 |
|---|---|
| TSC 버킷(rdtscp + lfence), run 전체 QPC 보정, TSC 읽기 오버헤드, ABI 오버헤드 게이트 | M10 (구현, §2.1) |
| 스레드 센서스 #1/#2, cpu_eff, 외부 스레드 cycle, G10b | M10 (구현, §7.1) |
| comparability key, 적합성 스탬프, preflight의 스탬프 확인 | M10 (구현, §9.1, 02 §13) |
| 반복·셔플·쿨다운·CV 보고 | M8 (구현, §5.2) |
| CV 게이트(추가 블록, `noisy`), sentinel, bootstrap CI | M10 (구현, §5.2, 02 §13) |
| High performance 세션, 바이너리 구성 대조군의 판정(1%) | M11 |
| Chaos의 센서스(TaskGraph 워커)와 스탬프(Chaos DLL) | M13 |
