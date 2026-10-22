# 씬 카탈로그 (Scenes)

> **작성일:** 2026-10-21 (M3. M6에서 게이트 씬 9개와 §1 `scene_hashes.json`을, M7에서 성능 씬 3개를, M9 준비(2026-10-22)에서 거동 씬 7개와 §1의 섭동·필요 capability·강제 오버라이드·거동 출력 규약을 더했다)
> **상태:** M3의 씬 3개(`S00.FreeFall`, `M1.SingleBoxDrop`, `M3.BoxPile`), M6의 게이트 씬 9개(`S00d`, `S01`~`S08`, §2.4~§2.12), M7의 성능 씬 3개(`P0.EmptyStep`, `P1.SmallScene`, `M2b.Pyramid1240`, §2.13~§2.15), M9의 거동 씬 7개(`M2a.Tower`, `M4.InclineFriction`, `M5.BouncingBalls`, `M6a.Pendulum`, `M6b.Chain`, `M6c.HeavyChain`, `M7.UEDefaults`, §2.16~§2.22)를 정의한다. 거동 씬의 고정값은 `PhysicsTestScenarios/Public/BehaviorScenes.h` 한 곳에 있다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §6, §9.5, §12, §14.3 · [03_WorkPlan.md](03_WorkPlan.md) M3, M6, M7, §5.2 · [04_DecisionLog.md](04_DecisionLog.md) DL23, DL34, DL42, DL43 · [Abi.md](Abi.md) · [TimingContract.md](TimingContract.md) · [Progress.md](Progress.md) M6, M7 · `Config/Golden/tolerances.json`, `Config/Golden/scene_hashes.json`
> **근거 표기:** **[V]** 직접 빌드·실행해 확인 · **[R]** 추정

이 문서는 방법론 카탈로그의 씬 ID를 하네스가 구현한 정의로 고정한다. 씬 ID는 항상 `ID.Name`으로 적는다 (DL34). 마일스톤 번호와는 관계가 없다 (`M3.BoxPile`은 카탈로그 ID다). 하네스에서만 쓰는 씬은 "카탈로그 확장"으로 표시한다.

---

## 1. 공통 규약

| 항목 | 규약 |
|---|---|
| 단위·축 | cm, kg, s, rad, Z-up (D1). 중력은 프로필 값 (0, 0, -980) cm/s² |
| 바닥 | 바닥이 있는 씬은 정적 박스 하나를 **바디 인덱스 0**으로 둔다. 200 m × 200 m × 1 m (half extent 10000, 10000, 50 cm), 윗면이 z = 0이다 (중심 z = -50 cm) |
| 저작 경로 | 씬은 `FPhysScene` API만으로 만든다 (`CreatePhysicalMaterial`, `CreateBodySetup`, `SpawnBody`, `FlushDeferredActors`). 그래서 모든 엔진이 같은 마샬링 바이트를 받고 `desc_sha256`이 엔진 간에 같다 (DL43) |
| 바디 기본값 | 바디 템플릿은 프로필의 `[BodyInstanceDefaults]`, 머티리얼은 `[PhysicalMaterialDefaults]`를 먼저 적용한 뒤 씬이 고정한 값을 덮어쓴다 (`FScenarioContext::MakeBodyTemplate`, 02 §10). 그래서 UEParity와 Normalized의 damping, 최대 각속도 등은 프로필이 정하고, 씬이 고정한 값(질량, 마찰 등)은 두 프로필에서 같다 |
| 질량 | 방법론 씬은 `bOverrideMass`로 질량을 고정한다 (02 §10, DL17). 그래서 엔진과 프로필이 같은 질량을 쓴다 |
| 난수 | `FRandomStream`(mt19937_64 + 하네스 [0,1) 매핑). 씬이 `Seed` 파라미터를 선언하면 그 값, 아니면 0x5EED(24301)로 시작한다. 그리기 순서는 씬마다 정의에 적는다 |
| 파라미터 | `-Param:Name=Value`. 씬이 선언하지 않은 이름이나 허용하지 않은 값은 오류다 (벤치 종료 코드 2). 이름은 대소문자를 구분하지 않는다. 파라미터에는 형이 있다 (`EScenarioParamType`): `text`(허용 값 목록이 있으면 그중 하나)와 `int32`(`-`와 10진 숫자만, 32-bit 범위). `int32` 값은 정규형으로 저장한다 (`024301` → `24301`). 그래서 `Seed=abc`는 오류이고(전에는 `atoi`로 0이 됐다), 같은 seed를 다르게 적어도 `scene_sha256`이 같다. seed는 `IPhysicsTestScenario::GetSeed`로만 읽는다 (M3 수정 라운드 0) |
| scene_sha256 | 정규 JSON `{"name","params"(모든 선언 파라미터, 정규형, 이름순),"seed","version"}`의 SHA-256 (02 §12.3). `seed`는 `GetSeed`의 값(정수)이라 `Seed` 파라미터가 없는 씬도 기본 seed가 바뀌면 해시가 바뀐다 (M4에서 더함, M3 검토 지적). `scene.json`으로도 쓴다 |
| desc_sha256의 범위 | 엔진 독립이지만 **빌드 구성에는 독립이 아니다.** 하네스가 계산해 넣는 double(질량·관성, `FRotator` → `FQuat`)이 `/fp:fast`에서 `/Od`(dbg)와 `/O2`(dev, test)의 마지막 1~2 ulp가 다르다 [V]: 정적 바닥의 관성(`0x1.52f506ea62ff3p+51` 대 `…ff4p+51`), `FRotator(20, 0, 30).Quaternion()`, `FTransform(FRotator(0,0,0)).Rotator()`의 Roll 부호(+0 대 −0). 그래서 바닥이 있거나 회전한 바디가 있는 씬(`M1.SingleBoxDrop`, `M3.BoxPile`)은 dbg와 dev/test의 값이 다르다. `S00.FreeFall`(구 하나, 바닥 없음)은 같다. dev와 test는 같다 (같은 `/O2`). 비교는 한 빌드 구성 안에서만 한다 (manifest `scene.desc_sha256_scope`). M7의 "test와 test-avx2의 `desc_sha256`이 같다"는 하네스 플래그가 두 프리셋에서 같고(하네스에 `/arch` 없음) 하네스가 어댑터의 AVX2 COMDAT을 쓰지 않을 때(`PhysicsTest.Probe.AdapterComdats`)만 성립하므로, M7은 두 조건을 함께 확인한다. **M7 [V]:** 두 조건이 test-avx2에서도 통과하고(`Probe.HarnessSdkIsolation`, `Probe.AdapterComdats`), 성능 씬 전부와 `M1.SingleBoxDrop` b(회전한 바디)의 `desc_sha256`이 test·test-avx2 × Null·Jolt·Bullet·PhysX 8 run에서 하나다 (씬·파라미터 8개 × 프로필 2, [Progress.md](Progress.md) M7 §2 기준 11). 정적 바디의 질량·관성을 해시에서 0으로 두는 안은 택하지 않았다: 회전한 바디의 쿼터니언도 다르므로 dbg 문제를 풀지 못하고, 엔진에 넘기는 바이트를 그대로 덮는다는 원칙만 깬다 |
| 윈도 | 씬이 프레임 윈도를 선언한다 (02 §9.5). frame 0은 통계에서 빼고 따로 보고한다 |
| 용량 힌트 | 씬이 바디·쌍·접촉 수를 선언하고, 그 값이 `FPhysSceneInitParams`의 힌트가 된다 (02 §9.4). Jolt는 이 값으로 `PhysicsSystem::Init`과 temp allocator 크기를 정한다 |
| 게이트 씬 상수 | 게이트 씬의 고정값(치수, 질량, 속도, 바디 인덱스)은 `PhysicsTestScenarios/Public/GateScenes.h` 한 곳에 있고, 씬과 게이트(`PhysicsTestConformance/Private/Gates/PhysicsGates.cpp`)가 같은 상수를 쓴다. 그래서 해석 기준값과 씬이 따로 바뀌지 않는다. 게이트 씬은 기준값에 필요한 값(damping 0, 질량, 머티리얼)만 고정하고 나머지(각속도 한계, 반발 임계, sleep 스위치)는 프로필에 맡긴다 (`ScenarioHelpers::MakeGateBodyTemplate`) |
| 명령 시점 | 게이트 씬의 임펄스는 `IPhysicsTestScenario::OnPreTick`(UE `OnPhysScenePreTick`, 02 §9.1)에서 frame 0에 한 번 낸다. 그래서 step 1부터 효과가 보인다 |
| sleep이 필요한 씬 | `RequiresSleeping()`이 참인 씬(`S08.StartAsleep`)은 sleep을 끈 프로필(Normalized)에서 거부한다: 벤치 종료 코드 2 (CTest `PhysicsTest.Bench.ExitSceneNeedsSleeping`) |
| `scene_hashes.json` | `Config/Golden/scene_hashes.json`(schema `physicstest.scene_hashes/1`)이 게이트 씬마다 `version`, `scene_sha256`(빌드 독립), `default_frames`와 빌드 구성(Release = dev·test, Debug = dbg) × 프로필별 `desc_sha256`을 고정한다. 다시 만들 때는 구성마다 `PhysicsTestBench -SceneHashes=Config/Golden/scene_hashes.json`을 돌린다 (그 구성의 항목만 바꾸고 나머지는 둔다). `PhysicsTest.Scenario.SceneHashesGolden`(CTest `PhysicsTest.Unit.Scenario`)이 매 빌드 대조한다. `S00`, `S00d`, `S01`, `S07`(바닥·회전 없음)은 두 구성의 값이 같고, 나머지는 위 desc_sha256의 범위 규칙대로 다르다 [V] |
| 등록 | `IMPLEMENT_PHYSICS_TEST_SCENARIO`로 정적 등록한다. 프로그램은 `PT::Scenarios`를 `/WHOLEARCHIVE`로 링크하고 시작할 때 레지스트리가 비어 있지 않은지 확인한다 (`pt_link_scenarios`, `FScenarioRegistry::CheckNotEmpty`) |
| 같은 엔진 섭동 변형 (M9) | 혼돈계 거동 씬(`M2a`, `M6b`, `M6c`)은 `int32` 파라미터 `Perturb`를 가진다. 0(기본) = 섭동 없음. k ≠ 0 = 모든 dynamic 바디의 시작 위치를 정확히 1e-4 cm(01 §7의 1e-6 m) 옮긴다: 방향은 바디마다 `FRandomStream(k)`(배치용 스트림과 따로)에서 뽑은 구면 균일 단위 벡터다. 그래서 k = 1, 2, …가 서로 다른 복제이고 배치는 그대로다. 다른 씬이므로 `scene_sha256`과 `desc_sha256`이 달라진다. float 엔진은 이 값을 한 ulp 근처로 반올림한다 (1000 cm에서 float cm 6.1e-5 cm, 10 m에서 float m 9.5e-7 m): 그 반올림도 잰 민감도의 일부다 (`PhysicsTest.Behavior.Perturbation`) |
| 필요 capability (M9) | 씬이 `GetRequiredCapabilities(파라미터)`로 선언한다 (조인트 씬: `PT_CAP_JOINT_BALL` / `_HINGE`, `-List`의 `required_capabilities`). 벤치는 그 capability가 없는 백엔드에서 **백엔드 초기화 전에** 거부한다: 종료 코드 2, status·category `unsupported`, `exit.json`만 (CTest `PhysicsTest.Bench.ExitSceneNeedsJoints`). M8 러너는 그런 run을 `unsupported`로, 모든 반복이 그런 셀을 `unsupported`로 적고 세션 실패로 세지 않는다. `FPhysScene`만으로 만든 씬(뷰어, 단위 시험)은 그 조인트를 호스트 전용으로 둔다 ([Abi.md](Abi.md) §9.4 11). **M9:** Jolt·Bullet·PhysX(모든 변형)가 두 조인트 capability를 선언하므로 조인트 씬을 거부하는 백엔드는 Null뿐이다 (`PhysicsTest.Bench.PreflightSweep`가 그 밖의 거부를 실패로 본다) |
| 강제 오버라이드 (M9) | 프로필의 `[/Script/PhysicsTest.ForcedOverrides]`(Normalized: 결합 모드 Average 강제, SF := F)는 씬이 저작한 **뒤** `FlushDeferredActors`에서 바디와 그 머티리얼에 적용되고 로그와 manifest `profile.forced_overrides`에 남는다 (02 §10, [Profiles.md](Profiles.md)). 게이트 씬은 적용하지 않는다 (`AllowsForcedOverrides()` false: `S05`·`S06`이 머티리얼별 결합 모드를 쓴다). 게이트 실행기도 적용하지 않는다. 그래서 `scene_hashes.json`의 게이트 씬 해시는 그대로다. 거동·성능 씬은 Normalized에서 적용되어 그 `desc_sha256`이 M8과 다르다 (SF := F가 마샬링 바이트를 바꾼다) |
| 거동 run 출력 (M9) | `-StatesEvery`로 상태를 기록하는 run은 프레임 0 전에(타이밍 밖) `scene_desc.json`(schema `physicstest.scene_desc/1`)도 쓴다: 백엔드가 받은 그대로의 머티리얼, 바디(질량·관성·COM·처음 자세·셰이프), 조인트(타입, Body1·Body2, 두 프레임, `simulated`). 분석(`behavior.py`)은 C++ 없이 이 파일과 `states.ptst`로 관통 깊이, 조인트 신장, 에너지를 계산한다 ([ResultsSchema.md](ResultsSchema.md)) |

모든 씬은 `PhysicsTestBench.exe -List`로 볼 수 있다 (이름, 버전, 종류, 파라미터와 기본값, 기본 프레임 수).

---

## 2. 씬

### 2.1 `S00.FreeFall` (카탈로그 확장, 게이트 G1)

방법론 §5.1의 SI 단위 S00을 cm와 g = 980으로 다시 정의한 씬이다. M2 자유낙하 픽스처(`Fixtures::FFreeFall`)를 그대로 쓴다 (M2.5 SP3도 같은 픽스처를 쓴다).

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적 |
| 바디 | 구 하나: 반지름 50 cm, 1 kg (`bOverrideMass`), 시작 z = 1000 cm, damping 0, 중력 켬, 깨어 있음. **바닥 없음**: 60 프레임 동안 z = 50 cm까지 내려오지 않으므로 픽스처를 그대로 둔다 (바디 인덱스 0이 구다) |
| 프레임 | 60 (FixedStep 1/60) |
| 윈도 | headline 1-59 |
| 기준값 | dt_engine = (double)(float)(1/60) = 0.01666666753590107 s 기준 semi-implicit Euler: z60 = 501.8332813705 cm, v60 = -980.0000511 cm/s (DL23). 정확한 1/60 기준은 501.8333333 cm다 |
| 판정 | SDK 백엔드: \|z60 − 501.83328\| ≤ 2e-3 cm, \|v60 + 980.00005\| ≤ 1e-2 cm/s (03 M3, G1). Null: 기준값과 1e-9 이내 |
| 측정값 [V] | Jolt(float) z60 = 501.833296 cm (\|dz\| 1.6e-5), v60 = -979.99983 cm/s (\|dv\| 2.2e-4). UEParity와 Normalized가 같다 (damping을 씬이 0으로 고정). M6 G1: Jolt·Bullet·PhysX SdkDefault \|dz\| 1.58e-5 cm, PhysX UE4Compat·-TGS·-Stab 9.40e-5 cm (cm 단위 float 해상도), Null 1.1e-13 cm, 모든 엔진 \|dv\| ≤ 4.8e-4 cm/s |

### 2.2 `M1.SingleBoxDrop` (방법론 카탈로그 M1)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, 결정적 (바디별 비교 대상, 02 §13) |
| 파라미터 | `Case` = `a`(기본) 또는 `b` |
| 머티리얼 | 상자와 바닥이 같은 머티리얼: Friction 0.5, StaticFriction 0, Restitution 0 (결합 모드는 프로젝트 값 Average) |
| 바디 | 0: 바닥. 1: 한 변 100 cm 정육면체, 1000 kg (`bOverrideMass`), 중심 z = 150 cm (밑면이 바닥 위 100 cm) |
| 자세 | a: 축 정렬 (면으로 착지). b: `FRotator(Pitch 20, Yaw 0, Roll 30)`: roll 30° 다음 pitch 20° (UE 합성은 Roll을 먼저 적용, UnrealMath.cpp:483). 모서리로 착지해 넘어진다 |
| 프레임 | 300 |
| 윈도 | headline 1-299, impact 1-89, settled 150-299, **fall**(M9) a 1-25 / b 1-20: 첫 접촉 전의 자유 낙하 (가장 낮은 점이 a는 step 27, b는 step 22에 바닥을 지나므로 한 프레임 여유를 둔다). 03 M9의 UEParity 대 Normalized 표(damping만 다른 구간)에 쓴다 (`PhysicsTest.Behavior.InitialPoses`가 기하에서 다시 계산한다) |
| 용량 힌트 | 바디 2, 쌍 64, 접촉 64 |
| 판정 (03 M3) | a의 정지 접촉 스냅샷(viz run): 상자-바닥 접촉점마다 normal·(+Z) > 0.999, −2.5 cm ≤ Phi ≤ 0.5 cm. 하한은 Jolt 침투 허용치 `mPenetrationSlop` 0.02 m (PhysicsSettings.h:53 [V])에 0.5 cm를 더한 값이다. `shape.wrapping` = 0 |
| 측정값 [V] | Jolt Normalized 300 프레임: 접촉점 4개, normal.z = 1.0000, Phi = −2.00003 cm (4점 모두), 상자 z = 48.0 cm, Dynamic. UEParity는 정지 후 약 0.5 s 뒤 잠들고 잠든 바디는 접촉을 만들지 않으므로 50 프레임에 같은 검사를 한다 (−2.00004 cm). M4부터 Jolt 스냅샷은 잠든 섬의 접촉을 `PT_CONTACT_SLEEPING`으로 유지하고 Phi는 점마다 계산한다 (4점 −2.00003 ~ −2.00002 cm) |

### 2.3 `M3.BoxPile(N)` (방법론 카탈로그 M3, 헤드라인 성능 씬)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Perf / 1, 혼돈계 (바디별 비교 대상이 아니다) |
| 파라미터 | `N` = `1000`(기본) / `5000` / `10000`, `Seed` = 24301 (0x5EED, `int32`: 10진수로 적는다) |
| 배치 | 층마다 10 × 10 / 25 × 20 / 40 × 25 (X 방향 열 × Y 방향 행), 10층. 중심 간격은 X·Y·Z 모두 150 cm. 격자는 원점 중심 (`X = −75·(열−1) + 150·i`, Y도 같음). 가장 아래 층의 중심 z = 100 cm (밑면이 바닥 위 50 cm), k층 중심 z = 100 + 150·k cm |
| 흔들림 | 상자마다 yaw U(−15°, 15°), x·y 이동 U(−5, 5) cm. 그리는 순서: 층 → 행 → 열, 상자마다 yaw, dx, dy (`FRandRange`) |
| 바디 | 0: 바닥. 1..N: 한 변 100 cm 정육면체, 1000 kg (`bOverrideMass`), COM이 중심 (래퍼가 필요 없는 네이티브 박스, DL42) |
| 머티리얼 | 씬 기본 머티리얼 = 프로필의 `[PhysicalMaterialDefaults]` (UE 기본값 F 0.7, SF 0, R 0.3) |
| 프레임 | 600 |
| 윈도 | headline 1-599, impact 1-179, settled 300-599 (02 §9.5) |
| 용량 힌트 | 바디 N + 1, 쌍 16·N + 1024, 접촉 8·N + 1024 |
| 판정 (03 M3) | `shape.wrapping` = 0 (G9의 전 단계). 1k/5k의 첫 타이밍은 manifest에 "비공식"으로 기록한다 |
| 측정값 [V] | Jolt, test 프리셋, T = 1, 전원 계획 Balanced, **비공식**: [Progress.md](Progress.md) M3 §2. 600 프레임 동안 엔진 오류 bit 0, temp allocator fallback 0. 같은 exe를 fresh process로 세 번 돌려 마지막 프레임 `h_native`가 같았다 |
| 배치 코드 (M7) | 배치와 흔들림은 `ScenarioHelpers::SpawnBoxPile(FBoxPileLayout{열, 행, 층})`에 있고 `P1.SmallScene`이 같은 함수를 쓴다. M7에서 옮겼고 `scene_sha256`·`desc_sha256`과 M3의 결정성 해시는 그대로다 |
| 측정값 (M7) [V] | 10k, test 프리셋, T = 1, 측정 프로토콜(호스트 제어, pre-roll 0.5 s, `-HostCheck=enforce`), **비공식**: `T_frame` 평균 Jolt 12.04 ms, Bullet 24.20 ms, PhysX UE4Compat 8.02 ms, SdkDefault 7.57 ms (헤드라인 1-599). `timing_valid_for_headline` 참, 타이밍 버킷 안 할당 0 |
| M9 거동 [V] (1k, 900 프레임, 100 프레임마다 기록, 비공식) | 서 있는 더미(평균 z ≈ 500 cm, 맨 위 ≈ 950 cm)는 Jolt(슬롭 2 cm만큼 낮다: 493.5 / 941.6 cm), PhysX UE4Compat·-Stab(498.5 / 947.7 cm)이다. **Bullet.Default는 무너지고**(254.6 cm, [Checkpoints.md](Checkpoints.md) N-1), **PhysX UE4Compat-TGS는 상자를 더미 밖으로 던지며**(frame 599까지 86개가 제자리에서 50 cm 넘게, 465.7 cm, N-4: 반발 0이면 선다), **PhysX SdkDefault Normalized는 퍼지다 frame 600 뒤 무너진다**(447.0 cm, N-5: 위치 반복 8이면 선다). 이 세 행은 성능 윈도 안에서도 다른 작업량이다 ([TimingContract.md](TimingContract.md) §9, M11 주의) |

### 2.4 `S00d.FreeFallDamped` (카탈로그 확장, 게이트 G1b)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` b39bd8dc…1efc5d |
| 바디 | 구 하나 (바디 인덱스 0, 바닥 없음): 반지름 50 cm, 1 kg (`bOverrideMass`), 시작 z = 1000 cm, **LinearDamping 0.5 1/s**, AngularDamping 0, 중력 켬 |
| 프레임 / 윈도 | 60 / headline 1-59 |
| 기준값 | 백엔드가 effective 키 `body.damping_formula`로 선언한 공식(Abi.md §5.14)으로 dt_engine 스텝마다 적분한다 (`Analytic::DampedFreeFall`): 공식 1(UE 순서 v = (v + a·dt)·max(0, 1 − c·dt), 모든 엔진) z60 = 578.026781101 cm. 공식 2(힘 전 감쇠)는 574.480787 cm다. damping 0.5는 두 순서를 3.5 cm로 가르려고 고른 값이다 (1e-3 cm 판정보다 훨씬 크다) |
| 판정 | \|z60 − 기준값\| ≤ 1e-3 cm (03 M6 G1b). 공식 id가 1·2가 아니면 실패 |
| 측정값 [V] | Null 0, Jolt 1.05e-4, Bullet 1.81e-4, PhysX UE4Compat·UE4Compat-TGS·UE4Compat-Stab 1.70e-4, PhysX SdkDefault 1.05e-4 cm (UEParity·Normalized 같음, M6 수정 라운드 1에서 엔진별 값을 바로잡았다). 모든 엔진이 공식 1이다 |

### 2.5 `S01.UnitImpulse` (카탈로그 확장, 게이트 G2)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` 8ee4ce82…1b7ecf |
| 바디 | 바닥 없음, 중력 끔, damping 0. 0: 한 변 50 cm 정육면체 10 kg, (0, 0, 500). 1: 같은 정육면체, (0, 300, 500). 머티리얼 F 0.5, R 0 |
| 명령 | frame 0의 PreTick: 바디 0에 `AddImpulse((1000, 0, 0) kg·cm/s)`(ADD_IMPULSE), 바디 1에 `AddImpulse((100, 0, 0), bVelChange)`(ADD_VELOCITY, BodyInstance.cpp:3875-3888 [V]) |
| 프레임 / 윈도 | 30 / headline 1-29 |
| 판정 (G2) | 두 바디 v.x = 100 cm/s (상대 1e-4). `ReadMassProperties`: 질량 상대 1e-5, 관성은 텐서 R·diag(I)·Rᵀ로 상대 1e-5 (Abi.md §5.7), COM 1e-5 cm (03 "COM readback 1e-5"를 cm 절대값으로 읽는다, M6 수정 라운드 0) |
| 측정값 [V] | 모든 엔진 v.x = 100 cm/s (상대 0). 질량 상대 ≤ 1.5e-8, 관성 텐서 상대 ≤ 4e-8, COM 0 |

### 2.6 `S02.SpinInertia` (카탈로그 확장, 게이트 G2)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` 7902235d…3c0801 |
| 바디 | 바닥 없음, 중력 끔, damping 0. 100 × 50 × 20 cm 박스(full extent) 10 kg 세 개: 0 (0, 0, 500), 1 (0, 300, 500), 2 (0, 600, 500) + `COMNudge (0, 0, 10)` cm (COM 래핑 경로: Jolt `OffsetCenterOfMassShape`, Bullet compound, PhysX COM frame. DL42). 주관성 Ixx = 2416.7, Izz = 10416.7 kg·cm² |
| 명령 | frame 0의 PreTick `AddAngularImpulseInRadians`: 바디 0은 Z축 L = Izz·2, 바디 1은 X축 L = Ixx·2, 바디 2는 Z축 L = Izz·2 |
| 프레임 / 윈도 | 60 / headline 1-59 |
| 판정 (G2) | ω = I⁻¹·L (하네스 관성. 바디 0·1은 2 rad/s, 바디 2는 평행축 항 m·d² = 1000 kg·cm²를 더한 관성으로 1.8248 rad/s)을 상대 1e-4. 질량·관성 텐서·COM readback 1e-5 / 1e-5 cm. 바디 2의 COM이 60 프레임 동안 1e-5 cm 안에 머물고(COM 둘레로 돈다) COM 속도가 1e-3 cm/s 이하다 |
| 측정값 [V] | \|ω\| 상대 오차 ≤ 2.2e-7 (Jolt), Bullet·PhysX 1.1e-8 이하. 바디 2 COM readback 오차 ≤ 1.5e-7 cm, COM drift ≤ 1.3e-6 cm |

### 2.7 `S03.CapsuleRest` (카탈로그 확장, 게이트 G2)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` 55d189ce…147fe5 |
| 바디 | 0: 바닥. 반지름 25 cm, 선분 길이 100 cm(`FKSphylElem::Length`, HalfLength 50) 캡슐 50 kg 두 개, 바닥 위에 정확히 놓는다: 1 서 있음 (축 Z, 중심 z = 75 cm), 2 누움 (`FRotator(0, 0, 90)`, 축 Y, 중심 (300, 0, 25)). 머티리얼 F 0.5, R 0, damping 0 |
| 프레임 / 윈도 | 120 / headline 1-119 |
| 판정 (G2) | 120 프레임 뒤 서 있는 캡슐 z = 75 cm, 누운 캡슐 z = 25 cm (±0.5 cm), 서 있는 캡슐의 축이 Z |
| 측정값 [V] | 모든 엔진·변형·프로필: 75.0000 / 25.0000 cm, axis.Z 1.000000 |

### 2.8 `S04.ContactNormal` (카탈로그 확장, 게이트 G3)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` 4ba0ec32…adcd1f |
| 바디 | 0: 바닥. 1: 한 변 100 cm 정육면체 (−300, 0, 49), 2: 반지름 50 cm 구 (0, 0, 49), 3: 반지름 25 · 길이 100 cm 누운 캡슐 (300, 0, 24). 모두 100 kg이고 시작할 때 바닥에 **1 cm 잠겨 있다**. 머티리얼 F 0.5, R 0 |
| 실행 | viz run (게이트가 `RunMode = Viz`로 만들어 접촉 리스너를 붙인다), step 1 뒤의 스냅샷 |
| 프레임 / 윈도 | 60 / headline 1-59 |
| 판정 (G3) | step 1의 바디별 접촉: 백엔드가 Chaos 규약으로 바꾼 뒤 normal·(+Z) > 0.999 (Jolt 부호 반전 포함), Phi < 0, PointOn1이 바닥 표면 위, PointOn0이 narrow phase 자세의 바디 표면 위 (0.05 cm, Abi.md §9.1 결정 3. 스텝 시작 자세(Jolt·Bullet·PhysX)와 자유 운동 예측 자세 X0 + (v0 + g·dt)·dt(Chaos, 0.272 cm 아래) 중 씬 전체에 맞는 하나, M6 수정 라운드 2), flag 규칙(결정 2: ACTIVE / ACTIVE_APPROX / SPECULATIVE 중 많아야 하나, SPECULATIVE는 Phi > 0만, `PT_CAP_CONTACT_ACTIVE_EXACT` 백엔드는 ACTIVE만 쓰고 쌍마다 ACTIVE 점이 있으며, 유한한 임펄스가 있으면 임펄스 > 0.05 kg·cm/s ⇒ ACTIVE, ACTIVE ⇒ 임펄스 ≥ −0.05 (한 방향, 수정 라운드 2), 아니면 침투 점마다 ACTIVE_APPROX). UEParity: frame 40에 상자를 `PutInstanceToSleep`, 두 스텝 뒤 상자의 유지 점이 모두 SLEEPING, `WakeInstance` + flush 뒤 스텝 전 스냅샷에 0개, 그 스텝 뒤 다시 나오고 SLEEPING 없음 (결정 2의 깬 쌍 제외) |
| 측정값 [V] (M6 수정 라운드 0) | step 1 flag: Jolt 모든 점 ACTIVE_APPROX(+SWAPPED), Bullet·PhysX 모든 점 ACTIVE. 깬 상자: 네 SDK 엔진 모두 유지 4점 → 0 → 4 (SLEEPING 0) |
| 측정값 [V] | 박스 4점, 구 1점, 캡슐 2점(Bullet은 step 1의 매니폴드에 1점), normal.z 1.0000000, Phi −1.00000 cm (±1e-5), 표면 오차 ≤ 3.7e-4 cm (Jolt 박스 PointOn0), PhysX 표면 투영 fallback 0 |

### 2.9 `S05.RestitutionCombine` (카탈로그 확장, 게이트 G4)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / **2**, 결정적. `scene_sha256` 719ef5ad…4fb6119 (v1 e349e0bc…2b6375. v2 = 0.01 cm 틈, M6 수정 라운드 2) |
| 머티리얼 | 바닥: F 0.5, R 0 (프로젝트 결합 모드 Average). 구: F 0.5, SF 0, **R 0.8**, 반발 결합 모드를 구마다 덮어쓴다 (`bOverrideRestitutionCombineMode`): Average / Multiply / Max. 쌍의 모드는 둘 중 큰 것이다 (DL41) |
| 바디 | 0: 바닥. 1·2·3: 반지름 25 cm 구 10 kg, x = −200 / 0 / 200 cm, 밑면 높이 h0 (아래) |
| 낙하 높이 | 이산 궤적이 step K = 38 끝에 바닥에 닿는 높이(DL23, `Analytic::StepBoundaryDropHeight`) h0 = g·dt_engine²·K(K+1)/2 = 201.7167 cm에 **틈 0.01 cm**(`GateScenes::S05::TouchGap`, v2)를 더한다: 201.7267 cm, 충돌 속도 620.667 cm/s. 씬이 자기 tick 모드의 dt와 프로필 중력으로 계산한다. 그래서 어느 엔진도 이르거나 깊게 닿지 않는다 (PhysX contact offset 2 cm에서도 39번째 스텝에 접촉이 시작된다). 틈의 이유: v1은 step 38 끝이 정확한 접점이라, narrow phase를 예측 자세(스텝 끝)에서 돌리는 Chaos는 Phi = 0 ± 반올림을 보고 반쯤의 확률로 step 38에 push-out과 반발을 낼 것이었다 (독립 검사). 0.01 cm는 모든 SDK 엔진의 투기적 거리 안이라 접촉 스텝은 그대로 39다 |
| 프레임 / 윈도 | 120 / headline 1-119, impact 36-42 |
| 판정 (G4) | 접촉 프레임 c = 39 (여유 0). e = −v_z[c] / v_z[c−1] (접촉 프레임과 직전 프레임의 법선 속도 비, 03 M6 문장 그대로)가 엔진의 등록된 반발 속도 모델(`G4.restitution_velocity_model`: 0 = e·\|v[c−1]\|, 1 = e·(\|v[c−1]\| + g·dt))로 예측한 값(기대 e Average 0.4, Multiply 0, Max 0.8)의 ±0.02 (03 기본값, 모든 엔진) 이내이고, 다른 모델의 예측보다 가깝다. 보조: apex 비가 접촉 스텝 뒤 높이 h_c에서 시작한 이산 비행(모델의 v_after) ±0.02 |
| 측정값 [V] (v2, M6 수정 라운드 2) | 접촉 프레임 39 (모든 엔진). Jolt (모델 0): e 0.80000 / 0.40000 / 0, apex 비 0.6443 / 0.1663 (= 이산 기준, h_c 8.29 cm). Bullet (모델 0): e 0.79903 / 0.39903, apex 비 0.6427 / 0.1655 (기준 0.6442 / 0.1663): Bullet의 투기적 접촉은 반발 목표에서 틈 / dt = 0.6 cm/s를 뺀다 (btSequentialImpulseConstraintSolver.cpp:949-953 [V], e에서 −0.00097). PhysX (모델 1, 예측 0.82105 / 0.41053): PGS e 0.82105 / 0.41053, apex 비 0.6780 / 0.1749 (= 기준, h_c 8.50 cm); TGS도 e 0.82105 / 0.41053, apex 비 0.6359 / 0.1539 (= 기준, h_c 0.008 cm: TGS는 접촉 스텝 안에서 접촉을 푼다). v1(정확한 접점)에서는 TGS가 0.81949 / 0.41028, apex 0.6334로 모델과 0.0016 어긋났다: 정확한 접점에서 TGS의 접촉 시작이 반올림에 걸려 있었던 것으로 본다 [R] |
| 주의 | M9의 `ForcedOverrides`(결합 모드 강제)가 이 씬의 구별 결합 모드를 덮으면 G4가 무의미해진다. 게이트 실행은 ForcedOverrides를 쓰지 않는다 |

### 2.10 `S06.FrictionSlide` (카탈로그 확장, 게이트 G5)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` a445eac1…a92207b |
| 바디 | 0: 바닥 (F 0.5, R 0). 1: 한 변 50 cm 정육면체 10 kg, 바닥 위 (0, 0, 25), 초기 속도 (500, 0, 0) cm/s (바닥과 같은 머티리얼, Average). 2: static 경사 600 × 300 × 20 cm, 중심 (0, 1000, 400), `FRotator(Pitch 30)`, F 0 R 0. 3·4: 한 변 50 cm 정육면체 10 kg, 경사 윗면에 맞춰 놓음 (3은 경사 중심선에서 Y −75 cm, 4는 +75 cm). 머티리얼 F 0.6 (3) / 0.55 (4), SF 0, **마찰 결합 Max**로 경사(F 0)를 이긴다 |
| 프레임 / 윈도 | 120 / headline 1-119 |
| 기준값 | 정지 거리: dt_engine 이산해 250.947 cm (`Analytic::FrictionSlideStopDistance`, DL23. 연속해 255.10은 오답). 경사: tan 30° = 0.577이라 μ 0.6은 정지, μ 0.55는 a = g(sin 30° − 0.55·cos 30°) = 23.212 cm/s²로 미끄러진다 |
| 판정 (G5) | 정지 거리 ±1%, 미끄러진 상자의 120 프레임 속도 ≤ 1 cm/s. frame 20(어느 엔진의 sleep도 느린 미끄러짐을 멈추기 전)에: μ 0.6 상자의 경사 아래 방향 가속도(frame 10→20) ≤ 2 cm/s²이고 변위 ≤ 0.1 cm(등록 편차: PhysX PGS 1.0 cm), μ 0.55 상자의 변위가 동마찰 기준(1.3541 cm)의 0.5~1.5배. μ 0.6 상자의 creep 속도(위치로 잰 경사면 위 변위 / 10 dt, frame 10-20과 110-120) ≤ 0.75 cm/s (등록 편차: PhysX PGS 3 cm/s) |
| 측정값 [V] | 정지 거리: Jolt 250.9452 (−0.001%), Bullet 250.7576 (−0.076%), PhysX PGS 250.9427 (−0.002%), TGS 250.9662 (+0.008%) cm. μ 0.6 frame 20: Jolt 0.0002, Bullet −0.0002, PhysX TGS 0.0303, PhysX PGS(UE4Compat·-Stab·SdkDefault) 0.73 cm (가속도 0.29 cm/s²: 스텝 1에 얻은 약 2 cm/s를 잃지 않는 PGS creep). μ 0.55: 1.354~1.668 cm, a 22.9~23.2 cm/s². μ 0.6 creep 속도 (frame 10-20 / 110-120): Jolt 0 / 0, Bullet 0.0010 / 0.0011, TGS 0.065 / 0.067 (UEParity는 frame 40부터 0: sleep), PGS 2.50 / 2.48 cm/s (+Y 0.8 cm/s 성분 포함; -Stab UEParity는 110-120에 0). TGS의 보고 속도(약 0.5 cm/s)는 위치 변화와 맞지 않아 creep은 위치로 잰다 (M6 수정 라운드 0) |
| 주의 | M8까지 Jolt UEParity는 μ 0.55 상자를 약 3.0 cm에서 재웠다 (Jolt 자체 임계: 세 점을 감싸는 구의 반지름이 `mPointVelocitySleepThreshold × mTimeBeforeSleep` = 1.5 cm를 넘지 않은 채 0.5 s, PhysicsSystem.cpp:2527, Body.cpp:160-183 [V]). **M9의 UE 임계 매핑**(`SleepThresholdMapping=UE`: 0.5 cm/s × 0.0832 s = 반지름 0.042 cm, [Profiles.md](Profiles.md) §5.1) 뒤에는 이 상자가 잠들지 않고 frame 119까지 46.8 cm 미끄러진다 (UE 규칙으로 계산해도 Chaos가 재우지 않는 상자다 [R]). 판정 프레임 20은 그대로 둔다: 엔진의 sleep이 느린 미끄러짐을 멈추기 전이라는 이유는 다른 엔진·변형(PhysX UE4 규칙, Bullet UE 매핑의 바디별 카운터)과 `SleepThresholdMapping=Native`에 여전히 해당한다. M9의 `ForcedOverrides`가 머티리얼 결합 모드를 덮으면 경사 판정이 무의미해진다 |

### 2.11 `S07.ClampProbe` (카탈로그 확장, 게이트 G7)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적. `scene_sha256` d8a75202…e6ceaa2 |
| 바디 | 바닥 없음, 중력 끔, damping 0. 한 변 100 cm 정육면체(등방 관성) 100 kg 세 개, x = 0 / 300 / 600 cm, z 500, 초기 각속도 Z축 50 / 80 / 120 rad/s. 각속도 한계는 프로필 값이다 (UEParity 3600 °/s = 62.83 rad/s, Normalized 해제 1e7 °/s) |
| 프레임 / 윈도 | 10 / headline 1-9 |
| 판정 (G7) | UEParity \|ω\| = min(ω0, 62.83) ±0.01 rad/s, Normalized ω0 ±0.01. 마지막 스텝 회전각이 엔진의 회전 모델(`tolerances.json` `step_angle_model`)의 값 ±0.001 rad: 0 = \|ω\|·dt (Jolt, PhysX), 1 = 2·atan(\|ω\|·dt/2) (Null = Chaos 1차 쿼터니언, Rotation.cpp:158-162 [V]), 2 = Bullet π/4 한계 (btTransformUtil.h:44-72 [V], DL16 등록 예외) |
| 측정값 [V] | UEParity 62.83185 rad/s (모든 SDK 엔진), Normalized 50 / 80 / 120 그대로. 80 rad/s의 UEParity 스텝 회전각: Jolt·PhysX 1.047198, Bullet 1.009191 (모델 2), Null 0.964696 (모델 1) |

### 2.12 `S08.StartAsleep` (카탈로그 확장, 게이트 G6, UEParity 전용)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Gate / 1, 결정적, `RequiresSleeping` (Normalized에서는 거부). `scene_sha256` 799d246f…0e5f0530 |
| 바디 | 0: 바닥. 한 변 100 cm 정육면체 100 kg 두 개, z0 = 500 cm: 1 (−200, 0, 500) `bStartAwake = false` (02 §6 5단계: AddActorsToScene 뒤 wake counter 0 + putToSleep), 2 (200, 0, 500) 깨어 있음 |
| 프레임 / 윈도 | 60 / headline 1-59 |
| 판정 (G6) | 바디 1이 생성 직후와 60 프레임 뒤 모두 Sleeping이고 z가 변하지 않는다 (1e-6 cm). 바디 2는 100 cm 넘게 떨어진다. 그다음 ABI v1.0 결정 1: 60 프레임 잠든 바디 1에 `PT_CMDF_AUTO_WAKE` 없이 `SET_LINEAR_VELOCITY` (100, 0, 0), 0 벡터, `SET_ANGULAR_VELOCITY` (0, 0, 2) rad/s, `SET_BODY_STATE`(STATE_WRITE를 선언하면)를 각각 새 실행에서 쓰고, 다음 스텝에 Dynamic이며 그 속도(0 벡터는 한 스텝의 중력 −g·dt)로 움직인다 (속도 상대 1e-4, 변위 v·dt의 1%). G6의 Normalized 쪽은 `M1.SingleBoxDrop` a를 600 프레임 돌려 잠든 바디-프레임 0을 확인한다 |
| 측정값 [V] | 모든 엔진: 바디 1 z 500.0000 cm (이동 0), Sleeping. 바디 2는 바닥에 닿는다. Normalized 600 프레임 잠든 바디 0. 속도 쓰기 (M6 수정 라운드 0): 모든 엔진·변형이 깨우고 v.x 100.000000, 0 벡터 v.z −16.333334 cm/s, w.z 2.000000 rad/s; 한 스텝 변위 1.6666 cm (PhysX TGS 1.666626), −0.27222 cm, 회전 0.033333 rad (Null 0.033330 = Chaos 1차 쿼터니언 모델) |

### 2.13 `P0.EmptyStep` (카탈로그 확장, 성능)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Perf / 1, 결정적 |
| 바디 | 0: 바닥 (§1)뿐. 동적 바디 없음 |
| 프레임 / 윈도 | 600 / headline 1-599 |
| 용량 힌트 | `MakePileCapacity(0)`: 바디 1, 쌍 1024, 접촉 1024 |
| 용도 | 할 일이 없는 프레임의 값: ABI 프레임 호출 4개, 엔진의 스텝당 고정 작업, 빈 상태 pull. M8이 P0, P1, `M3.BoxPile`로 맞추는 오버헤드 모델 t = a + b·N의 절편 a다 (02 §13) |
| preflight | 동적 바디가 없어 값이 없는 바디 집계 effective 키는 G9가 건너뛴다 (Jolt `sleep.enabled`는 canonical null). [Abi.md](Abi.md) §5.14 M7 메모 |
| 측정값 [V] | test, T = 1, 측정 프로토콜, **비공식**, `T_frame` 평균 / 중앙값: Null 0.2 / 0.1 µs, Jolt 1.1 / 1.0 µs, Bullet 0.4 / 0.3 µs, PhysX UE4Compat 2.2 / 2.2 µs, SdkDefault 2.1 / 2.0 µs. QPC 눈금이 100 ns라 프레임 값 하나는 눈금 몇 개다. 윈도 평균만 쓴다 (02 §9.3의 배치 측정은 M10 TSC와 함께, [Progress.md](Progress.md) M7 §3) |

### 2.14 `P1.SmallScene(N)` (카탈로그 확장, 성능)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Perf / 1, 혼돈계 |
| 파라미터 | `N` = `10`(기본) / `100`, `Seed` = 24301 (`int32`) |
| 배치 | `M3.BoxPile`의 규칙을 작은 N에 쓴다 (`ScenarioHelpers::SpawnBoxPile`): N = 10은 층마다 1 × 1(한 기둥), N = 100은 5 × 2, 10층. 정육면체·질량·간격·흔들림·seed가 `M3.BoxPile`과 같아 상자 하나가 하는 일이 같다 |
| 바디 | 0: 바닥. 1..N: 한 변 100 cm 정육면체 1000 kg |
| 프레임 / 윈도 | 600 / headline 1-599, impact 1-179, settled 300-599 |
| 용량 힌트 | 바디 N + 1, 쌍 16·N + 1024, 접촉 8·N + 1024 |
| 측정값 [V] | test, T = 1, 측정 프로토콜, **비공식**, `T_frame` 평균: N = 10 Null 0.6, Jolt 11.2, Bullet 14.0, PhysX UE4Compat 10.7, SdkDefault 9.5 µs. N = 100 Null 5.4, Jolt 91.1, Bullet 138.1, PhysX UE4Compat 58.9, SdkDefault 54.6 µs. Normalized에서는 600 프레임 뒤에도 모든 상자가 깨어 있다 (sleep 끔). 마지막 프레임 모양 (Normalized, `-StatesEvery`로 마지막 프레임만 기록): 모두 서 있으면 맨 위 상자 z = 950 cm, 평균 z = 500 cm다. N = 10은 모든 엔진에서 기둥이 선다 (맨 위 z 938.9~948.5 cm, 평균 493.0~499.1 cm. Jolt가 가장 낮은 것은 정지 접촉의 겹침을 허용하기 때문이다: 접촉마다 약 1 cm, 허용치 `mPenetrationSlop` 2 cm, §2.2). N = 100(기둥 10개)은 엔진마다 다르게 무너진다: 평균 z Jolt 493.9, PhysX UE4Compat 498.5, SdkDefault 458.1, Bullet 256.3 cm (혼돈계라 바디별 비교 대상이 아니다) |

### 2.15 `M2b.Pyramid1240` (방법론 카탈로그 M2b, 성능)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Perf / 1, 혼돈계 |
| 출처 | Jolt PerformanceTest의 `PyramidScene`(JoltPhysics/PerformanceTest/PyramidScene.h:24-43 [V], MIT)을 cm, Z-up, 하네스 데이터 모델로 다시 썼다. 섬 하나가 아주 큰 씬이다 |
| 배치 | 15층. i층(0 = 맨 아래)에 (15 − i)²개, 모두 1240개. 한 변 200 cm 정육면체, 수평 간격 200 cm(이웃이 면으로 닿는다), 홀수 층은 x·y로 100 cm 어긋난다. i층 중심 z = 100 + 250·i cm (층 사이 50 cm 틈, 맨 아래 층은 바닥 위). Jolt 루프의 (i, j, k) 상자는 x = 100·(−15 + 2j + (i 홀수 ? 1 : 0)), y = 100·(−15 + 2k + (i 홀수 ? 1 : 0)), z = 100·(1 + 2.5i) cm, j·k ∈ [i/2, 15 − (i+1)/2) |
| 바디 | 0: 바닥. 1..1240: 8000 kg (`bOverrideMass`. Jolt 기본 밀도 1000 kg/m³ × 8 m³), 층 → j → k 순서 |
| Jolt 씬과 다른 점 (의도) | 머티리얼은 프로필 기본값(`M3.BoxPile`과 같은 규칙), sleep은 프로필을 따른다 (Normalized는 끔 = Jolt `mAllowSleeping = false`와 같다), 박스 convex radius는 변형의 값이다 (Jolt 씬의 0은 M9 변형 `Jolt.SharpBoxes`) |
| 프레임 / 윈도 | 500 (방법론의 500 스텝) / headline 1-499, impact 1-119, settled 300-499 |
| 용량 힌트 | 바디 1241, 쌍 16·1240 + 1024, 접촉 8·1240 + 1024 |
| preflight | 상자 기하의 G9 상수 규칙은 이 씬에도 걸린다 (M7 수정 라운드 0: `tolerances.json`의 `"scene"` 목록). 값이 같은 것: Bullet `shape.box_margin` 4 cm(min(4, 0.1 × 100)), Jolt `shape.box_margin`(ConvexRadius 5 cm < 100), PhysX `contact.speculative_distance` 4 cm와 `physx.contact_offset_min/max` 2 cm(UE4 clamp(0.02 × 100, 2, 8) = 2). 값이 다른 것: Bullet `contact.speculative_distance`는 200 cm 상자의 0.02 × √3 × 100 = 3.464 cm다 (`"by_scene"`). M7 첫 판은 `M3.BoxPile` 상수로 Bullet run을 거부했고, 다음 판은 이 규칙들을 건너뛰기만 했다 |
| 측정값 [V] | test, T = 1, 측정 프로토콜, **비공식**, `T_frame` 평균: Null 65 µs(충돌 없음), Jolt 6.04 ms, Bullet 7.81 ms, PhysX UE4Compat 3.98 ms, SdkDefault 3.94 ms. 스레드 매핑 (120 프레임, 평균): Jolt T = 1..6 5.46 / 3.82 / 2.97 / 2.48 / 2.18 / 2.03 ms, PhysX 3.69 / 2.41 / 1.86 / 1.57 / 1.30 / 1.21 ms, Bullet T = 1 6.79 ms ("cpu_eff 미검증 (M10)"). 마지막 프레임 모양 (Normalized, 500 프레임): 층 사이 틈 50 cm × 14가 닫히면 맨 위 상자 z = 2900 cm다. 모든 엔진에서 피라미드가 선다: 맨 위 상자 z Jolt 2893.9, Bullet 2899.2, PhysX UE4Compat 2898.5, SdkDefault 2897.0 cm (시작 3600), 맨 아래 층 평균 z 99.90~100.00 cm |

### 2.16 `M2a.Tower(N)` (방법론 카탈로그 M2a, 거동)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, **혼돈계** (집계 관측량과 같은 엔진 섭동 민감도로만 비교, 02 §13) |
| 파라미터 | `N` = `5` / `10`(기본) / `20` / `30`, `Perturb` = `0`(기본, `int32`, §1) |
| 바디 | 0: 바닥. 1..N: 한 변 100 cm 정육면체 100 kg (`bOverrideMass`), 축 정렬, 정확히 쌓는다 (k번째 중심 z = 50 + 100·k cm, 면끼리 닿고 틈 없음) |
| 머티리얼 | 씬 기본 머티리얼 = 프로필 `[PhysicalMaterialDefaults]` (UE F 0.7, SF 0, R 0.3) |
| 프레임 / 윈도 | 600 / headline 1-599, early 1-60, sleep5s 1-300, late 300-599 |
| 용량 힌트 | `MakePileCapacity(N)` |
| 판정 (03 M9) | N = 10, UEParity(sleep 켬): PhysX UE4Compat / -Stab / -TGS의 안정화 순서가 01의 프로브와 같은 방향 (UE4Compat가 가장 늦게 안정되거나 5 s 안에 잠들지 않음) |
| M9 준비 관측 [V] (dev, 비공식) | N = 10 UEParity, 모든 상자가 잠든 첫 프레임: UE4Compat 390 (6.5 s, 맨 위 상자 수평 흔들림 최대 19.4 cm), -Stab 38, -TGS 95, Jolt 100, Bullet 121. 01 프로브(PGS 8/1 흔들림 25 cm·안 잠듦, stabilization 약 1 s, TGS 약 2 s)와 같은 방향이다. N = 30 Normalized Jolt는 무너지고(맨 위 상자 최종 z 98 cm) `Perturb=1`이면 다르게 무너진다 (517 cm) |
| M9 관측 [V] (test, 거동 세션, 비공식) | N = 10 UEParity 모든 상자가 잠드는 시각 (기준 run과 섭동 복제 3개): UE4Compat 6.52 s / 3.32~3.33 s, -Stab 0.65 s, -TGS 1.60 s - 03 M9 기준(UE4Compat가 가장 늦다) 통과. M9의 UE sleep 매핑 뒤 Jolt와 Bullet의 탑은 10 s 안에 잠들지 않는다 (Jolt 맨 위 흔들림 10.6 cm; Bullet은 가장 빠른 상자가 2 → 8.7 cm/s로 계속 기어 UE 임계 1 cm/s 아래로 내려가지 않는다, [Progress.md](Progress.md) M9 Bullet S-2. 전의 Bullet 자체 규칙 80 cm/s는 frame 121에 재워 이 크리프를 감췄다). 정확히 맞닿은 탑은 첫 스텝에 1e-4 cm 섭동을 Bullet 약 2700배, Jolt 약 200배 키우고 PhysX PGS는 키우지 않는다 |

### 2.17 `M4.InclineFriction(Mu, BoxYaw)` (방법론 카탈로그 M4, 거동)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, 결정적 (바디별 비교 대상, 02 §13) |
| 파라미터 | `Mu` = `0.30` / `0.50`(기본) / `0.55` / `0.60` / `0.70`, `BoxYaw` = `0`(기본) / `45` (등방성 변형) |
| 바디 | 0: static 경사 3000 × 600 × 20 cm(full extent), 중심 (0, 0, 1000) cm, `FRotator(Pitch 30)` (로컬 +X가 경사 위쪽, 로컬 +Z가 면 법선), 바닥 없음. 1: 한 변 50 cm 정육면체 10 kg, 경사 윗면에 정확히 놓는다 (경사 로컬 (1300, 0, 35) cm = 위 끝에서 200 cm 아래), 회전 = 경사 회전 × 면 법선 둘레 `BoxYaw` |
| 머티리얼 | 경사와 상자가 **한 머티리얼**을 쓴다: F = `Mu`, SF 0, R 0. 같은 값끼리의 결합이라 어떤 결합 모드(Average·Min·Max)에서도 쌍 마찰이 `Mu`다 (Normalized의 강제 SF := F도 쌍 값을 바꾸지 않는다) |
| 기준값 | tan 30° = 0.577: 0.30 / 0.50 / 0.55는 a = g(sin 30° − μ cos 30°)로 미끄러지고 0.60 / 0.70은 정지. 이산 기준 d_n = a·dt²·n(n+1)/2 (`Analytic::InclineSlideDistance`): d(2 s) = d_120 = μ 0.30 474.700, 0.50 132.389, 0.55 46.811, 0.60·0.70 0 cm (벤치 `-AnalyticReferences`의 `M4.d2s_*`). 프레임 f의 상태는 스텝 f + 1 뒤라 d(2 s)는 frame 119의 상태다 |
| 프레임 / 윈도 | 180 / headline 1-179, 2s 1-119 |
| 판정 (03 M9, Normalized) | 미끄러지는 μ에서 d(2 s)가 이산 기준과 1% 이내. μ ≥ 0.6은 정지 (drift < 0.1 cm). 분석은 `BoxYaw` 0과 등방성 변형 `BoxYaw` 45를 같은 한계로 **따로** 판정한다 (M9 수정 라운드 1: 처음 판은 `BoxYaw` 0만 보고 그것을 적지 않았다). `BoxYaw` 45 Normalized [V]: Jolt 통과, Bullet μ 0.50 −7.57%·μ 0.55 −6.85%, PhysX UE4Compat μ 0.55 +6.27%, 정지 μ 0.6 0.294 cm·μ 0.7 0.115 cm (creep 0: 초반에 미끄러지고 멈춘다). Bullet·PhysX는 실행 뒤 등록 허용치(Bullet 8%, PGS 6.5% / 6 cm, TGS 0.3 cm) 안이다. **리드 결정 (2026-10-22):** 이 등록을 엔진 거동 / 회귀 상한으로 승인했다 (메커니즘은 [Progress.md](Progress.md) M9). 그래서 세 엔진 모두 등록 허용치로 통과한다 (`fail (pending override)`는 승인 전의 표시였다) |
| M9 관측 [V] (test, 거동 세션, 비공식) | 03 M9 판정 (Normalized, d(2 s) = frame 119): Jolt 통과 (최대 0.005%, μ ≥ 0.6 drift 0.0006 cm). Bullet μ 0.50 −1.89%, μ 0.55 −1.26% (미끄러지는 상자가 4점 매니폴드 위에서 흔들린다, \|ω\| 0.46~0.84 rad/s [R]), μ ≥ 0.6은 정지. PhysX UE4Compat μ 0.55 +2.89% (PGS 마찰 축 분리, frame 30 +13.9%에서 줄어드는 초반 과속), μ 0.6 drift 4.97 cm·μ 0.7 0.86 cm (M6 D-4 PGS creep), TGS 0.156 cm. 세 편차는 algorithmic 태그이고, 실행 뒤 등록 허용치(리드 승인 2026-10-22)로 판정한다 ([Progress.md](Progress.md) M9 리드 결정). UEParity: Jolt μ 0.55 +0.004% (M9 UE sleep 매핑 전에는 0.5 s에 잠들어 −93.6%), Bullet −3.59% |
| M9 준비 관측 [V] (dev, Normalized, 비공식) | frame 120(스텝 121 뒤)의 이산 기준 대비 오차: μ 0.30 Jolt −0.014%, Bullet −0.54%(옆 방향 0.27 cm), PhysX UE4Compat −0.03%(옆 방향 5.2 cm), TGS −0.014%. μ 0.55 Jolt −0.01%, Bullet −1.3%, UE4Compat +2.8%, TGS +0.37%. μ 0.60 frame 120 이동: Jolt 0, Bullet 0, UE4Compat 4.65 cm (M6 D-4의 PGS creep), TGS 0.15 cm (creep 0.065 cm/s, M6 G5와 같은 값). Jolt의 일정한 −0.065 cm는 한 스텝(a·dt²)의 출발 지연이다. `BoxYaw` 45: Jolt 같음, Bullet −0.78%, UE4Compat 옆 방향 2.8 cm (PGS 마찰 축 이방성) |

### 2.18 `M5.BouncingBalls` (방법론 카탈로그 M5, 거동)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, 결정적 |
| 바디 | 0-3: static 패드 200 × 200 × 100 cm(윗면 z = 0), x = −300 / −100 / 100 / 300 cm. 4-7: 반지름 25 cm 구 10 kg (`MakeGateBodyTemplate`: damping 0), 같은 x |
| 머티리얼 | 공과 그 아래 패드가 한 머티리얼: F 0.5, SF 0, R = 0.3 / 0.5 / 0.7 / 0.9 (공 4·5·6·7). 쌍 반발이 공의 값이다 |
| 낙하 높이 | G4(`S05` v2)와 같은 방식: 밑면 = `Analytic::StepBoundaryDropHeight`(K = 38, 201.7167 cm) + 0.01 cm. 첫 충돌은 모든 엔진에서 step 39, 620.67 cm/s. 그 뒤의 충돌은 튄 궤적이 정한다 |
| 프레임 / 윈도 | 600 / headline 1-599, impact1 36-42 |
| 판정 (03 M9) | 임계 50 cm/s를 넘는 모든 충돌에서 e_k = −v_n(c) / v_n(c−1)이 엔진별 등록 허용치(기본 ±0.02) 이내 |
| M9 관측 [V] (test, 거동 세션, 비공식) | 03 M9 판정 (Normalized, 임계를 넘는 모든 충돌): Jolt 통과 (등록 모델 0, 차이 < 2e-7), PhysX UE4Compat 통과 (등록 모델 1 e(1 + g·dt/\|v\|), 차이 < 3e-7). Bullet은 모델 0으로는 실패(최대 0.80)이고, 스텝 시작의 접촉 상태 모델(틈 > 0이면 e − 틈/(dt·\|v\|), split 임계 −4 cm보다 얕은 관통이면 e + erp2·깊이/(dt·\|v\|), 더 깊으면 e; btSequentialImpulseConstraintSolver.cpp:944-972 [V])이 모든 판정 충돌을 5.8e-6 안에서 맞춘다 (거동 세션 `behavior-m9b`, 리포트 §7.1). **리드 결정 (2026-10-22):** `tolerances.json` G4에 Bullet 모델 2로 등록했다 (post-run, 승인). 그래서 Bullet도 등록 모델로 통과한다 (최대 5.8e-6, 접촉 스텝의 e로 판정). G4(`S05` v2)의 첫 충돌도 틈 0.01 cm 때문에 0.8 − 0.01/(dt·620.67) = 0.79903이고 측정값과 같다 (G4 게이트가 모델 2로 확인한다). 충돌 깊이 = 스텝 이동 − 접촉 거리 (0.9 공 최대 Bullet 7.7, Jolt 5.7, PhysX 2.9, TGS 1.1 cm) |
| M9 준비 관측 [V] (dev, Normalized, 비공식) | 처음 여섯 충돌의 e_k: Jolt 모두 공칭값(0.3 / 0.5 / 0.7 / 0.9). Bullet 첫 충돌 0.299 / 0.499 / 0.699 / 0.899, 뒤 충돌에서 흩어짐 (0.9 공 0.981까지, 0.7 공 0.565~0.819). PhysX UE4Compat·SdkDefault는 속도가 줄수록 커진다: 0.9 공 0.9237 → 0.9339, 0.3 공 0.3079 → 0.3696 (M6 D-3의 반발 속도 모델 1, e(1 + g·dt/\|v\|)) |

### 2.19 `M6a.Pendulum(Angle, Joint)` (방법론 카탈로그 M6a, 거동, 조인트)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, 결정적. **필요 capability**: `Joint=Hinge` → `PT_CAP_JOINT_HINGE`, `Joint=Ball` → `PT_CAP_JOINT_BALL` |
| 파라미터 | `Angle` = `5`(기본) / `30` / `90` (도), `Joint` = `Hinge`(기본) / `Ball` |
| 바디 | 0: 반지름 5 cm 구(솔리드) 1 kg, 중심 = 피벗 (0, 0, 1000) + 100·(sin a, 0, −cos a) cm, 회전 `FRotator(Pitch a)` (로컬 +Z가 피벗을 향한다). 바닥 없음 |
| 조인트 | Body1 = 추, Body2 = 월드. Frame1 = 추 로컬 (0, 0, 100), X = 로컬 Y (= 월드 Y, 힌지 축), Y = 로컬 Z. Frame2 = 피벗, X = 월드 Y, Y = 막대 방향. 정지 상태에서 두 프레임이 일치한다 ([Abi.md](Abi.md) §9.4 3) |
| 기준값 | 물리 진자 T = 2π·sqrt((L² + 2/5 r²)/(g L)) × 2K(sin(a/2))/π (`Analytic::PendulumPeriod`): 5° 2.00905 s, 30° 2.04305 s, 90° 2.37023 s (g 980, L 100, r 5. 03 M9의 세 값과 같다) |
| 프레임 / 윈도 | 600 (약 4~5주기) / headline 1-599 |
| 판정 (03 M9, Normalized) | 5°에서 주기 오차 < 0.5% (영점 교차). **리드 결정 (2026-10-22):** 기준은 카탈로그 씬(기본 `Joint=Hinge`)에서 판정한다 (`M6a.period`). `Joint=Ball` 변형은 측정값을 보고만 하고 판정하지 않는다 (`M6a.period.ball`, `reported`). Bullet ball의 +8.7%는 엔진 거동 발견이다 (`btPoint2PointConstraint`, algorithmic) |
| M9 관측 [V] (test, 거동 세션, Normalized, 비공식) | 주기 오차 (%) / 진폭 감소 (%/주기) / 최대 신장 (cm), hinge = ball(아래 예외만): **5°** Jolt −0.012 / 0.047 / 0.0018, PhysX PGS(UE4Compat·-Stab·SdkDefault) −0.011 / 0.047 / 0.0011, TGS −0.011 / −0.005 / 0.0003, Bullet hinge −0.009 / 0.007 / 0.005: **03 M9 기준(< 0.5%) 통과**. Bullet ball +8.71 / 9.2 / 2.35: 0.5% 밖이지만 판정하지 않는 변형이다 (보고만, 엔진 거동 발견: `btPoint2PointConstraint`의 세 스칼라 행, 반복 10, [Progress.md](Progress.md) M9 Bullet J-1). **30°** Jolt −0.12 / 1.58, PhysX PGS −0.14 / 1.55, TGS +0.018 / −0.29, Bullet hinge +0.056 / 0.27 (신장 0.17), Bullet ball +37.9 / 22.4 (신장 36 cm). **90°** Jolt −6.4 / 9.2 (신장 0.45), PhysX PGS −6.5 / 9.1 (0.26), TGS +3.5 / −4.6 (진폭이 자란다, 신장 0.94), Bullet hinge −1.5 / 2.6 (1.22), Bullet ball +23 / 22 (73 cm). Jolt·PhysX PGS의 진폭 감소는 velocity 단계 조인트 해법의 접선 투영 손실 모형 (3/4)π²a₀²·dt/T(5° 0.047, 30° 1.66 %/주기)과 맞는다. UEParity는 UE 감쇠 0.01이 주기당 1.0%를 더한다. 태그는 [Progress.md](Progress.md) M9 §4 |

### 2.20 `M6b.Chain` (방법론 카탈로그 M6b, 거동, 조인트)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, **혼돈계**. 필요 capability `PT_CAP_JOINT_BALL` |
| 파라미터 | `Perturb` = `0`(기본, §1) |
| 바디 | 0..19: 캡슐 고리 (반지름 5 cm, 선분 30 cm, 끝에서 끝 40 cm, 1 kg), 중심 (20 + 40·i, 0, 1000) cm, 회전 `FRotator(Pitch 90)` (로컬 +Z = 월드 −X, 부모 쪽). 바닥 없음. 수평으로 놓고 정지에서 놓는다 |
| 조인트 | ball (`ConfigureAsBallAndSocket`), 끝과 끝: 고리 i의 Frame1 = 로컬 (0, 0, +20), Body2 = 고리 i−1의 Frame2 = 로컬 (0, 0, −20). 첫 고리는 월드 피벗 (0, 0, 1000) cm (프레임 축은 첫 고리의 월드 축). 모든 조인트 `bDisableCollision` (이웃 고리는 끝에서 닿는다. 이웃이 아닌 고리끼리는 충돌한다) |
| 프레임 / 윈도 | 600 / headline 1-599, swing1 1-60, late 300-599 |
| 판정 (03 M9) | 체인 신장(조인트 프레임 원점 사이 거리)을 보고하고 에너지가 발산하지 않는다. 분석은 `states.ptst`와 `scene_desc.json`의 조인트 프레임으로 계산한다 (§1 "거동 run 출력") |
| M9 관측 [V] (test, 거동 세션, 비공식) | 최대 신장 (Normalized / UEParity): Jolt 10.8 / 9.5 cm, Bullet 20.3 / 19.3, PhysX UE4Compat 11.6 / 23.1, -Stab 11.6 / 14.4, SdkDefault(4 / 1) 26.6 / 31.6, TGS 2.9 / 2.3 cm. 에너지는 모든 엔진·복제에서 방출 에너지 위로 오르지 않는다 (−0.018%, 03 기준 통과). 10 s 뒤 남은 에너지는 엔진마다 다르다 (late 기울기 −0.7 ~ −3.5 %/s) |

### 2.21 `M6c.HeavyChain` (방법론 카탈로그 M6c, 거동, 조인트)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, **혼돈계**. 필요 capability `PT_CAP_JOINT_BALL` |
| 파라미터 | `Perturb` = `0`(기본) |
| 바디 | `M6b.Chain`과 같은 고리 10개 + 끝 구 (반지름 20 cm, **100 kg** = 고리의 100배), 구 중심은 마지막 고리 끝에서 20 cm 너머 (x = 420 cm) |
| 조인트 | 고리 사이는 `M6b`와 같다. 구의 Frame1 = 로컬 (−20, 0, 0) (축은 고리의 월드 축), Body2 = 마지막 고리 (0, 0, −20). 모두 `bDisableCollision` |
| 프레임 / 윈도 | 600 / headline 1-599, swing1 1-60, late 300-599 |
| 판정 (03 M9) | `M6b`와 같다 (질량비 100:1에서의 신장과 에너지) |
| M9 관측 [V] (test, 거동 세션, 비공식) | Normalized, 기준 run과 섭동 복제 3개: **Jolt** 최대 신장 35.0~36.4 cm, 에너지 비발산 (통과; 40 velocity / 10 position step이면 8.4 cm). **Bullet** 11~15 m (가벼운 고리가 π/4/스텝 회전 한계를 넘는다, 반복 횟수 무관), 에너지는 오르지 않는다 (조인트가 떨어진 체인의 자명한 에너지 검사라 통과로 세지 않는다: 별도 발견 - 조인트 분리, 아래 리드 결정). **PhysX PGS**(UE4Compat·-Stab·SdkDefault) 약 0.75 s에 찢어진다: 10~19 m, 에너지가 낙하 규모의 44~139배 (**실패**, 모든 복제. 리드 결정 2026-10-22: 받아들인 엔진 거동 - `fail (accepted engine behaviour)`, 조정하지 않는다). **TGS** 기준 run은 신장 25.5 cm로 버티지만 에너지는 네 run 모두 방출 에너지 위로 오른다 (낙하 규모 대비 +21 / +1077 / +58 / +1313 %, 복제 1·3은 205·235 cm로 찢어진다. UEParity +16 / +1354 / +144 / +93 %): 03의 "에너지 비발산"(1%)은 TGS에서도 성립하지 않는다 (M9 수정 라운드 0 정정: 전에는 "복제 둘 발산"으로 적었다. PGS와 함께 받아들인 엔진 거동이다: PhysX 5.x에는 UE4가 기댄 joint projection이 없다, algorithmic). UEParity: Jolt도 모든 복제가 찢어진다 (25~34 m): UE 선형 감쇠 0.01만 0으로 강제한 probe(`jolt.Damp0`)가 Normalized 결과를 되돌린다: run 4개 중 3개(Perturb 0~2)는 비트 단위로 같고, Perturb 3은 frame 89부터 갈리지만 체인은 버틴다 (신장 36.2 cm, Normalized 36.4 cm). 감쇠 특유의 메커니즘은 아니다 (M9 수정 라운드 2 probe, Normalized): 감쇠 없이 중력만 −979 / −981 cm/s²(0.1%)로 바꿔도, 각 감쇠 0.01만 줘도, 선형 + 각 0.01(조인트 속도 구속을 지키는 균일 배율)이어도 frame 86~115에 찢어지고, 중력 0.01% 변화(−979.99 / −980.1)는 버틴다 (36.7~37.2 cm). Jolt 기본 10 / 2 step의 이 체인은 경계에 있다: Normalized의 통과는 경계 위의 통과다. Bullet은 기준 run(Perturb 0)에서 에너지가 m g span의 +323% 오른다 (복제 셋은 오르지 않는다. 처음에 적은 "2.3배"는 z = 0 기준 위치에너지로 잰 E_max / E_0라 10 m 높이의 피벗에 매인 값이었다, M9 수정 라운드 1). Normalized의 Bullet "에너지 통과"는 조인트가 떨어져(신장 11~15 m, 고리 40 cm) 자유 낙하하는 체인이라 자명하다: 리드 결정(2026-10-22)대로 통과가 아니라 별도 발견(조인트 분리)으로 보고한다 (분석 `finding: joints separated`). 발견과 태그는 [Progress.md](Progress.md) M9 |

### 2.22 `M7.UEDefaults` (카탈로그 확장, 거동)

| 항목 | 값 |
|---|---|
| 종류 / 버전 | Behavior / 1, 결정적 |
| 목적 | UE 레벨이 만들 듯 만든 바디: `bOverrideMass` 없음 (질량 규칙 `UEBodyUtils`의 (ρV)^0.75, 02 §5.3), 프로필 기본 머티리얼(UE F 0.7, SF 0, R 0.3, 밀도 1 g/cm³)·damping·sleep. UE 기본값이 스스로 만드는 효과(질량 ^0.75, R 0.3 튐, damping, sleep)를 엔진과 떼어 보여준다 (02 §10) |
| 바디 | 0: 바닥. 1..6: 한 변 50 / 100 / 200 cm 정육면체, 반지름 25 / 50 cm 구, 누운 캡슐(반지름 25, 선분 100 cm, `FRotator(Roll 90)`). 밑면이 바닥 위 300 cm, x = −1000 / −600 / −200 / 200 / 600 / 1000 cm (서로 닿지 않는다) |
| 질량 [V] | UE 규칙: 37.384 / 177.828 / 845.897 kg, 23.011 / 109.458 kg, 캡슐 65.084 kg (질량 규칙 `Physical`이면 ρV: 125 / 1000 / 8000 kg, 65.4 / 523.6 kg, 261.8 kg) |
| 프레임 / 윈도 | 300 / headline 1-299, settled 200-299 |
| M9 관측 [V] (test, 거동 세션, 비공식) | UEParity 모든 바디가 잠드는 시각: Jolt 1.63 s, Bullet 1.65 s, PhysX UE4Compat 1.73 s, TGS 1.97 s (M9 UE 매핑 전 Bullet은 frame 206). 질량은 UE 규칙 표와 3.7e-4 kg 안. 튐 수(프로필 임계 위)는 엔진 반발 모델과 임계에 따라 1~4 (by-design). Bullet viz 드로어를 붙인 run(G11 `-G11Knob=viz`, 150 프레임, 매 프레임 `debugDrawWorld`)과 안 붙인 run의 해시가 매 프레임 같다 (CTest `PhysicsTest.Bench.G11BulletVizDrawer`) |

---

## 3. 이후 마일스톤에서 더할 씬

| 씬 | 마일스톤 | 메모 |
|---|---|---|
| ~~`P0.EmptyStep`, `P1.SmallScene`, `M2b.Pyramid1240`~~ | M7 | 성능 씬. M7에서 더했다 (§2.13~§2.15) |
| ~~`M2a.Tower`, `M4.InclineFriction`, `M5.BouncingBalls`, `M6a.Pendulum`, `M6b.Chain`, `M6c.HeavyChain`, `M7.UEDefaults`~~ | M9 | 거동 씬. M9 준비에서 더했고 M9에서 세 엔진으로 돌렸다 (§2.16~§2.22, 거동 매트릭스 `Config/Matrices/behavior.json`) |
