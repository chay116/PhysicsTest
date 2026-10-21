# 씬 카탈로그 (Scenes)

> **작성일:** 2026-10-21 (M3. M6에서 게이트 씬 9개와 §1 `scene_hashes.json`을 더했다)
> **상태:** M3의 씬 3개(`S00.FreeFall`, `M1.SingleBoxDrop`, `M3.BoxPile`)와 M6의 게이트 씬 9개(`S00d`, `S01`~`S08`, §2.4~§2.12)를 정의한다. 성능 씬(P0, P1, `M2b.Pyramid1240`)은 M7, 거동 씬은 M9에 이 문서에 더한다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §6, §9.5, §12, §14.3 · [03_WorkPlan.md](03_WorkPlan.md) M3, M6, §5.2 · [04_DecisionLog.md](04_DecisionLog.md) DL23, DL34, DL42, DL43 · [Abi.md](Abi.md) · [Progress.md](Progress.md) M6 · `Config/Golden/tolerances.json`, `Config/Golden/scene_hashes.json`
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
| desc_sha256의 범위 | 엔진 독립이지만 **빌드 구성에는 독립이 아니다.** 하네스가 계산해 넣는 double(질량·관성, `FRotator` → `FQuat`)이 `/fp:fast`에서 `/Od`(dbg)와 `/O2`(dev, test)의 마지막 1~2 ulp가 다르다 [V]: 정적 바닥의 관성(`0x1.52f506ea62ff3p+51` 대 `…ff4p+51`), `FRotator(20, 0, 30).Quaternion()`, `FTransform(FRotator(0,0,0)).Rotator()`의 Roll 부호(+0 대 −0). 그래서 바닥이 있거나 회전한 바디가 있는 씬(`M1.SingleBoxDrop`, `M3.BoxPile`)은 dbg와 dev/test의 값이 다르다. `S00.FreeFall`(구 하나, 바닥 없음)은 같다. dev와 test는 같다 (같은 `/O2`). 비교는 한 빌드 구성 안에서만 한다 (manifest `scene.desc_sha256_scope`). M7의 "test와 test-avx2의 `desc_sha256`이 같다"는 하네스 플래그가 두 프리셋에서 같고(하네스에 `/arch` 없음) 하네스가 어댑터의 AVX2 COMDAT을 쓰지 않을 때(`PhysicsTest.Probe.AdapterComdats`)만 성립하므로, M7은 두 조건을 함께 확인한다. 정적 바디의 질량·관성을 해시에서 0으로 두는 안은 택하지 않았다: 회전한 바디의 쿼터니언도 다르므로 dbg 문제를 풀지 못하고, 엔진에 넘기는 바이트를 그대로 덮는다는 원칙만 깬다 |
| 윈도 | 씬이 프레임 윈도를 선언한다 (02 §9.5). frame 0은 통계에서 빼고 따로 보고한다 |
| 용량 힌트 | 씬이 바디·쌍·접촉 수를 선언하고, 그 값이 `FPhysSceneInitParams`의 힌트가 된다 (02 §9.4). Jolt는 이 값으로 `PhysicsSystem::Init`과 temp allocator 크기를 정한다 |
| 게이트 씬 상수 | 게이트 씬의 고정값(치수, 질량, 속도, 바디 인덱스)은 `PhysicsTestScenarios/Public/GateScenes.h` 한 곳에 있고, 씬과 게이트(`PhysicsTestConformance/Private/Gates/PhysicsGates.cpp`)가 같은 상수를 쓴다. 그래서 해석 기준값과 씬이 따로 바뀌지 않는다. 게이트 씬은 기준값에 필요한 값(damping 0, 질량, 머티리얼)만 고정하고 나머지(각속도 한계, 반발 임계, sleep 스위치)는 프로필에 맡긴다 (`ScenarioHelpers::MakeGateBodyTemplate`) |
| 명령 시점 | 게이트 씬의 임펄스는 `IPhysicsTestScenario::OnPreTick`(UE `OnPhysScenePreTick`, 02 §9.1)에서 frame 0에 한 번 낸다. 그래서 step 1부터 효과가 보인다 |
| sleep이 필요한 씬 | `RequiresSleeping()`이 참인 씬(`S08.StartAsleep`)은 sleep을 끈 프로필(Normalized)에서 거부한다: 벤치 종료 코드 2 (CTest `PhysicsTest.Bench.ExitSceneNeedsSleeping`) |
| `scene_hashes.json` | `Config/Golden/scene_hashes.json`(schema `physicstest.scene_hashes/1`)이 게이트 씬마다 `version`, `scene_sha256`(빌드 독립), `default_frames`와 빌드 구성(Release = dev·test, Debug = dbg) × 프로필별 `desc_sha256`을 고정한다. 다시 만들 때는 구성마다 `PhysicsTestBench -SceneHashes=Config/Golden/scene_hashes.json`을 돌린다 (그 구성의 항목만 바꾸고 나머지는 둔다). `PhysicsTest.Scenario.SceneHashesGolden`(CTest `PhysicsTest.Unit.Scenario`)이 매 빌드 대조한다. `S00`, `S00d`, `S01`, `S07`(바닥·회전 없음)은 두 구성의 값이 같고, 나머지는 위 desc_sha256의 범위 규칙대로 다르다 [V] |
| 등록 | `IMPLEMENT_PHYSICS_TEST_SCENARIO`로 정적 등록한다. 프로그램은 `PT::Scenarios`를 `/WHOLEARCHIVE`로 링크하고 시작할 때 레지스트리가 비어 있지 않은지 확인한다 (`pt_link_scenarios`, `FScenarioRegistry::CheckNotEmpty`) |

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
| 윈도 | headline 1-299, impact 1-89, settled 150-299 |
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
| 주의 | Jolt UEParity는 μ 0.55 상자를 약 3.0 cm에서 재운다 (frame 40 뒤 2.9986 cm 그대로). Jolt sleep 검사는 속도가 아니라 세 점을 감싸는 구의 반지름이 `mPointVelocitySleepThreshold × mTimeBeforeSleep` = 1.5 cm를 넘지 않은 채 0.5 s가 지나는지를 본다 (PhysicsSystem.cpp:2527, Body.cpp:160-183 [V]). 천천히 가속하는 상자가 0.5 s 동안 지름 3 cm 안쪽만 움직이면 잠든다. 그래서 판정 프레임은 20이다. M9의 `ForcedOverrides`가 머티리얼 결합 모드를 덮으면 경사 판정이 무의미해진다 |

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

---

## 3. 이후 마일스톤에서 더할 씬

| 씬 | 마일스톤 | 메모 |
|---|---|---|
| `P0.EmptyStep`, `P1.SmallScene`, `M2b.Pyramid1240` | M7 | 성능 씬 |
| `M4.InclineFriction`, `M5.BouncingBalls`, `M6a.Pendulum`, `M7.UEDefaults` 등 | M9 | 거동 씬 |
