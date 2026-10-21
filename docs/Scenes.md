# 씬 카탈로그 (Scenes)

> **작성일:** 2026-10-21 (M3)
> **상태:** M3의 씬 3개(`S00.FreeFall`, `M1.SingleBoxDrop`, `M3.BoxPile`)를 정의한다. 게이트 씬(S01~S08)은 M6, 성능 씬(P0, P1, `M2b.Pyramid1240`)은 M7, 거동 씬은 M9에 이 문서에 더한다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §6, §9.5, §12 · [03_WorkPlan.md](03_WorkPlan.md) M3, §5.2 · [04_DecisionLog.md](04_DecisionLog.md) DL23, DL34, DL42, DL43 · [Abi.md](Abi.md)
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
| 측정값 [V] | Jolt(float) z60 = 501.833296 cm (\|dz\| 1.6e-5), v60 = -979.99983 cm/s (\|dv\| 2.2e-4). UEParity와 Normalized가 같다 (damping을 씬이 0으로 고정) |

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

---

## 3. 이후 마일스톤에서 더할 씬

| 씬 | 마일스톤 | 메모 |
|---|---|---|
| `S00d.FreeFallDamped`, `S01.UnitImpulse`, `S02.SpinInertia`, `S03.CapsuleRest`, `S04.ContactNormal`, `S05.RestitutionCombine`, `S06.FrictionSlide`, `S07.ClampProbe`, `S08.StartAsleep` | M6 | 게이트 씬 (카탈로그 확장). G1b~G7 |
| `P0.EmptyStep`, `P1.SmallScene`, `M2b.Pyramid1240` | M7 | 성능 씬 |
| `M4.InclineFriction`, `M5.BouncingBalls`, `M6a.Pendulum`, `M7.UEDefaults` 등 | M9 | 거동 씬 |
