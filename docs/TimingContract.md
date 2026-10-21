# 타이밍 계약 (Timing Contract)

> **작성일:** 2026-10-22 (M7)
> **상태:** M7 측정 프로토콜의 정본이다. M8에서 러너(반복·셔플·쿨다운·재시도, §5.2)와 경미한 정정을 더했다. TSC 버킷, 스레드 센서스(cpu_eff, G10b), 적합성 스탬프는 M10에 더한다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §9, §12 · [03_WorkPlan.md](03_WorkPlan.md) M7, M10 · [ResultsSchema.md](ResultsSchema.md) · [Scenes.md](Scenes.md) · [Abi.md](Abi.md) · [Progress.md](Progress.md) M7
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
| 4 | 백엔드 로드와 `InitGamePhys` | `phases_ns.backend_init` |
| 4b | 엔진 옵션 preflight (바디 없는 시험 씬) | `phases_ns.option_preflight` |
| 6 | 씬 구성: BuildScene → Flush → `desc_sha256` | `phases_ns.scene_create` |
| 7 | effective 덤프와 preflight: 필수 키, G0, G9, G10a ([ResultsSchema.md](ResultsSchema.md) §2 gates) | `phases_ns.preflight` |
| 8 | `PrepareForSimulation` (모든 엔진에 같은 비타이밍 기회: Jolt `OptimizeBroadPhase`·temp 블록 pre-fault, PhysX scratch pre-fault) | `phases_ns.prepare` |
| 9 | spin pre-roll: T개 CPU 전부에서 0.5 s (`-PreRoll=`) | `phases_ns.pre_roll` |
| 10 | frame 0, 이어서 측정 루프 | 프레임마다 버킷 (§2) |
| 12 | 결과를 모두 쓴다. 마지막에 `exit.json` | - |
| 13 | teardown: 씬 해제, `TermGamePhys` → `teardown.json`. UE_RUNTIME 모듈이면 `TerminateProcess` | `teardown.json.teardown_ns` |

- 5·11단계(스레드 센서스 #1, #2)는 M10이다. 엔진 옵션 preflight는 02 §12.2의 4단계와 6단계 사이라 4b로 적는다 (M8에서 번호를 고쳤다. M7 판은 5로 적어 센서스 자리와 겹쳤다).
- preflight가 실패하면 8~10단계를 건너뛰고 종료 코드 2로 끝난다. 출력은 그대로 쓴다. 엔진 옵션 preflight(4b단계)가 거부하면 씬이 없으므로 씬에서 나오는 manifest 필드는 null이고 `scene.built`가 false다. 이때도 13단계(teardown)는 돈다.
- teardown의 실패는 종료 코드를 바꾸지 않는다. `exit.json`이 이미 쓰인 뒤이므로 `teardown.json`에만 남는다. **Fatal도 같다 (M7 수정 라운드 0):** Fatal 훅은 12단계의 `exit.json`을 쓰기 전까지만 `exit.json`(3, `fatal`)을 쓴다. 그 뒤의 Fatal(예: `TermGamePhys`의 `checkf`, UE 모듈이 넘긴 Fatal)은 `teardown.json`의 `fatal`에 기록하고, 프로세스는 run의 종료 코드로 `TerminateProcess`된다 (DL52, CTest `PhysicsTest.Bench.ExitTeardownFatal`). 훅은 run이 어느 경로로 끝나든 해제된다.
- UE_RUNTIME 모듈이 로드된 프로세스의 Fatal은 `exit.json`(`terminate_process: true`)을 쓴 뒤 바로 `TerminateProcess`로 끝난다. CRT 종료 경로(`_Exit` = ExitProcess)는 DLL의 `DLL_PROCESS_DETACH`를 부르므로 쓰지 않는다 (`PhysicsTest.Bench.UeRuntimeFatal`).
- `-Conformance`도 run 디렉터리를 먼저 만든다. 게이트 전에 멈춘 경우(백엔드 없음, 설정·허용치 오류)도 `exit.json`이 남는다.

## 2. 클럭과 버킷

### 2.1 클럭

| 항목 | M7 값 |
|---|---|
| 소스 | QPC (`FPlatformTime::Cycles64`). MSVC `steady_clock`도 QPC다 (02 §9.3) |
| 주파수 | 10 MHz, 눈금 100 ns [V] (manifest `clock.qpc_hz`, `clock.qpc_tick_ns`) |
| 읽기 오버헤드 | 20000회 읽기 32묶음의 중앙값. 이 PC에서 약 12 ns [V] (manifest `clock.read_overhead_ns`). 호스트 제어 뒤에 잰다 |
| TSC | M10 (`clock.tsc_hz`는 null) |

QPC 눈금이 100 ns이므로 버킷 하나가 수 µs 아래인 작은 씬(`P0.EmptyStep`, `P1.SmallScene` 10)은 프레임 하나의 값보다 윈도 평균을 본다. 호출당 ABI 오버헤드처럼 더 짧은 구간은 M10의 TSC 배치 측정으로 잰다.

### 2.2 버킷 정의

`FPhysScene::Tick` 안에서만 잰다 (02 §9.1, §9.3).

| 버킷 | 덮는 것 | 비고 |
|---|---|---|
| `T_push` | `ApplyBodyCommands` 한 번. 명령이 없는 프레임은 호출하지 않고 0 | Harness driver는 스텝마다, Native driver는 프레임마다 |
| `T_step` | 엔진 프레임 4호출: `SetUpForFrame` → `StartFrame` → `WaitPhysScenes` → `EndFrame` | 하위 버킷 `setup`/`start`/`wait`/`end` |
| `T_pull` | `ReadBodyStates(PT_READ_ACTIVE_ONLY)` 한 번과 `FBodyInstance::State` 기록 | 프레임당 1회 |
| `T_frame` | ΣT_push + ΣT_step + T_pull | 엔진 비교는 반드시 이것으로 한다 |
| `frame_qpc_ticks` | 같은 구간의 QPC 원시 틱 합 | ns 변환 전 값 |

버킷 밖(타이밍에 들어가지 않음): PreTick·Step·PostTick 델리게이트와 씬의 `OnPreTick`, 프레임 계획(`FStepPlanner`), 깨어 있는 바디 수 세기, 그리고 §4의 기록 작업 전부.

### 2.3 엔진별 버킷 내용 [V]

| 엔진 | `SetUpForFrame` | `StartFrame` | `WaitPhysScenes` | `EndFrame` | `T_pull` |
|---|---|---|---|---|---|
| Jolt | dt 저장, 중력이 바뀌면 `SetGravity` | 쌓아 둔 임펄스·힘 적용 + `PhysicsSystem::Update(dt, 1, temp, jobs)` | 없음 | update error를 오류 bit로 | NoLock + `GetActiveBodiesUnsafe` + 직전 프레임 활성 목록 |
| PhysX | dt 저장, 중력이 바뀌면 `setGravity` | 쌓아 둔 명령 적용 + `simulate(dt, nullptr, scratch, 262144)` | `fetchResults(true)` | 이벤트(viz·contacts tier만), error callback bit | `getActiveActors` (SdkDefault는 `bEnableActiveActors=False`라 전체 순회) |
| Bullet | dt 저장, 중력이 바뀌면 `setGravity` | 쌓아 둔 명령·kinematic 목표 + `stepSimulation(dt, 0, dt)` (내부 pre-tick: UE 순서 감쇠, 속도 한계) | 없음 | post-tick 집계, 풀 fallback bit | COM TM × inv(ComLocal), `isActive` |
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

"N 프레임마다"는 (f + 1) % N == 0인 프레임 f와 마지막 프레임이다 ([ResultsSchema.md](ResultsSchema.md) §1). 1만 바디의 해시나 카운터 패스는 1 MB 넘게 건드려 다음 프레임의 캐시를 식히므로 (02 §9.1), 마지막 프레임 해시 외의 기록이 하나라도 있으면 헤드라인 자격이 없다.

### 4.3 계측 tier

`-Instrument=counters,phases,alloc,contacts,native_debug_draw` (`PT_INSTR_*`). 하나라도 켜면 `timing_valid_for_headline = false`이고 사유에 tier가 적힌다. 거부하지는 않는다 (03 M7).

| tier | M7 동작 |
|---|---|
| counters | `GetStepStats` 카운터를 `timings.csv`의 contact_pairs, contact_points, islands 열에 쓴다 |
| phases | 백엔드가 `PT_CAP_PHASE_TIMERS`를 선언하면 매 프레임 `GetStepStats`를 읽어 phase timer의 합을 `solver_ns` 열에 쓴다 (counters 열은 `CountersEvery`를 따른다). SDK 백엔드는 선언하지 않는다. Null DLL 시험 대역(`PT_NULL_DLL_PHASE_TIMERS=1`, 타이머 `null.steps`)으로 경로를 확인한다 (CTest `PhysicsTest.Bench.InstrumentPhases`, manifest `recorders.phases`). Chaos 솔버 수준(02 §9.7)은 M13이다 |
| alloc | 백엔드 쪽 할당 계측을 켜는 자리다 (M7에서 백엔드 동작은 같다. 할당 훅은 모든 run에서 켜져 있다) |
| contacts | 백엔드가 접촉 listener를 설치하고 (Jolt, PhysX; Bullet은 캡처 때 매니폴드를 읽는다) 벤치가 `contacts.ptcn`을 쓴다 |
| native_debug_draw | 백엔드에 전달만 한다. 명령 스트림 기록은 M9 |

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

- **환경 게이트 (읽기 전용):** `powercfg /getactivescheme`가 High performance나 Ultimate(GUID로 판정, 이름은 지역화된다)가 아니면 거부한다. `--allow-balanced`로만 우회하고, 우회한 세션은 리포트 머리, 모든 표 제목 밑의 조건 줄, 모든 그림 제목 위의 조건 줄로 표시된다 (M8 수정 라운드 1: 처음에는 머리에만 있어 따로 떼어 본 PNG에는 표시가 없었다). 유휴: 시스템 전체 바쁨 < 3% (2 s 창). 창을 하나씩 계속 보며 하나라도 통과하면 된다: 최소 5창, 그리고 `--idle-wait`(기본 120 s) 동안 (M8 수정 라운드 0: 이 데스크톱은 2.5~4.9%를 오가 5창으로는 무인 시작이 절반쯤 거부됐다). 기다린 시간과 모든 창이 `session.json` `environment.idle`에 남는다. 다른 PhysicsTest 프로세스가 없어야 한다. 실패한 검사는 `--allow-busy`, `--allow-concurrent`로 우회할 수 있다. 시험용 `--skip-idle`(유휴를 재지 않음)도 유휴 검사의 우회로 기록된다. 모든 우회는 `session.json` `bypasses`와 리포트 머리에 굵게 남는다. 리포트 머리는 세션 파일 하나가 아니라 모든 입력에서 만든다: 각 세션의 게이트와 각 run manifest의 `platform.power_scheme_high_performance` 중 하나라도 거짓이면 표시한다 (run 디렉터리만으로 만든 리포트, 여러 세션, 세션 도중의 전원 계획 변경). 시스템 설정은 바꾸지 않는다.
- **순서:** 반복 r마다 모든 셀을 한 번씩, seed 고정 셔플 순서로 돈다 (splitmix64 + Fisher-Yates, [ResultsSchema.md](ResultsSchema.md) §11). run마다 fresh process, 그 전에 쿨다운 2 s(그동안의 시스템 바쁨을 기록), 타임아웃이면 프로세스를 죽인다. 벤치에 `-Repeat`, `-Order`, `-BlockSeed`, `-Session`, `-Out`을 넘긴다.
- **판정은 `exit.json`이다:** 원시 프로세스 종료 코드는 기록만 한다. `exit.json`이 없으면 그 run은 끝까지 가지 못한 실패다(`no_exit_json`, 또는 `timeout`). 시험 전용 `-InjectSilentExit=<f>`(frame f 뒤 `TerminateProcess(0)`, 출력 없음)로 확인한다: 원시 코드 0인데 실패로 남는다 (CTest `PhysicsTest.Bench.InjectSilentExit`, `PhysicsTest.Py.Integration`).
- **종료 코드 4:** 같은 run을 쿨다운 뒤 fresh process로 최대 `contaminated_retries`(기본 2)번 더 돈다. 시도마다 `index.csv` 한 줄과 `session.json` runs[] 항목이 남는다. 모든 시도가 4이면 그 반복과 셀이 `contaminated`다.
- **CV:** 셀마다 ok이고 `timing_valid_for_headline`이 참인 run의 윈도 평균으로 CV(표본 표준편차 / 평균)를 보고한다 (리포트의 validate와 같은 run). ok지만 헤드라인 자격이 없는 run(dev·dbg, `-HostCheck=record`로 기록된 오염 등)은 값이 없고 `n_not_headline_valid`로 센다. 2% 미만은 목표로만 적는다 (게이트, 추가 블록, sentinel은 M10). 권위 있는 CV는 리포트의 것이다.

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
9. `PT_DO_CHECK = 0` 빌드 (test, test-avx2 프리셋). dev와 dbg는 check가 켜져 있어 자격이 없다.
10. 시험 전용 주입(`-InjectEngineErrorBits`, `-InjectPreflightFault`, `-InjectSceneInstrumentFlags`, `-InjectStatePerturbation`, M8의 `-InjectTickAllocation`, `-InjectSilentExit`)이 없음. 사유는 "test-only injection"이고 주입은 `run.test_injections`에 남는다 (`BenchmarkRunner.cpp`의 헤드라인 사유 목록).

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
- T > 1 수치에는 `threads.scaling_note` = "cpu_eff 미검증 (M10)"이 붙는다. 실제로 T개 코어만 일했는지(외부 스레드 cycle)는 M10의 스레드 센서스가 증명한다. T = 1의 단일 스레드 증명(G10b)도 M10이다.
- 시리즈 플래그 (02 §9.6 "HT(12)와 E코어 포함(20) 시리즈는 별도로 플래그를 단다"): `threads.series`가 마스크의 모양이다. `p_cores_one_lp`(P코어마다 논리 CPU 하나, 기본 T 시리즈), `p_cores_smt`(어떤 코어의 SMT 쌍이 둘 다 마스크에 있음), `with_e_cores`(E코어가 마스크에 있음). `threads.cores`, `smt_pair_cores`, `e_cores`도 쓴다. 시리즈가 다른 run은 한 스케일링 곡선에 섞지 않는다 (M8).

## 8. 결정성과 결과 중립 knob

- **G8 (DET1):** 같은 exe, 같은 T에서 fresh process 3회의 마지막 프레임 `h_native`(와 하네스가 본 상태의 비트 해시 `h_state`)가 같다. `PhysicsTestBench -Gate=G8 -Runs=3 <run 인자>`가 자식 프로세스를 차례로 띄우고 각자의 `exit.json`과 manifest를 읽어 비교한다 (`gate_g8.json`).
- **G11:** 접촉 캡처를 끄고(`-Instrument=none`) 켠(`-Instrument=contacts -CaptureEvery=1`) 두 run의 모든 프레임 `h_native`와 `h_state`가 같다 (`-Gate=G11`, `gate_g11.json`). 두 run 모두 `-HashEvery=1`이라 해시 작업은 같다.
- 자식 프로세스는 `-HostCheck=record`로 돈다. 다른 프로세스의 부하는 시간을 바꾸지 상태를 바꾸지 않는다.
- **음성 대조 (M7 수정 라운드 0):** 시험용 `-InjectGateFault=<f>`를 주면 게이트가 자식 하나에만 `-InjectStatePerturbation=<f>`(frame f 뒤 마지막 동적 바디에 1e-3 cm/s 속도 변화, 프레임 사이)를 넘긴다 (G8: 마지막 run, G11: contacts on). 두 게이트 모두 차이를 찾아 종료 코드 2로 끝나야 한다 (CTest `PhysicsTest.Bench.G8NegativeControl`, `G11NegativeControl`: G11은 "hashes.csv differs at data row 6", 곧 frame 5부터 다르다).

## 9. 비공식 표시

- M7의 manifest는 모두 `"unofficial": true`이고 `timing_label`이 사유를 적는다: 적합성 스탬프가 없고(M10), High performance 전원 계획 세션(M8 환경 게이트, M11)이 아니다.
- `timing_valid_for_headline = true`는 "이 run이 M7 측정 프로토콜을 지켰다"는 뜻이지 보고할 수 있다는 뜻이 아니다.
- 보고 가능한 수치의 조건: M10 스탬프 + 전원 계획 High performance/Ultimate + M8 러너의 반복·셔플 + M10 CV 게이트와 주장 규칙.

## 10. 이후 확장

| 항목 | 마일스톤 |
|---|---|
| TSC 버킷(rdtscp + lfence), run 전체 QPC 보정, TSC 읽기 오버헤드, ABI 오버헤드 게이트 | M10 |
| 스레드 센서스 #1/#2, cpu_eff, 외부 스레드 cycle, G10b | M10 |
| comparability key, 적합성 스탬프, preflight의 스탬프 확인 | M10 |
| 반복·셔플·쿨다운·CV 보고 | M8 (구현, §5.2) |
| CV 게이트(추가 블록, `noisy`), sentinel, bootstrap CI | M10 |
