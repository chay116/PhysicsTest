# 프로필 (Profiles)

> **작성일:** 2026-10-22 (M9 준비, 하네스 쪽. M9 통합(2026-10-22): 세 엔진의 변형 키, UEParity sleep 임계 매핑, 조인트·네이티브 드로 매핑, `Bullet.StaticAabbOff` 판정)
> **상태:** 03 M9 산출물 `docs/Profiles.md` (M9 확정판). 프로필 축, ini 계층, `[/Script/PhysicsTest.ForcedOverrides]`, preflight diff, 백엔드 변형 목록과 엔진별 매핑표를 한곳에 모은다. 정본은 `Config/` 아래 ini 파일과 리졸버(`Physics/PhysicsProfile.h`)이고, 이 문서와 다르면 버그다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §10 · [03_WorkPlan.md](03_WorkPlan.md) M9 · [04_DecisionLog.md](04_DecisionLog.md) DL17, DL18, DL35, DL41, DL44, DL48 · [Scenes.md](Scenes.md) §1 · [ResultsSchema.md](ResultsSchema.md) · [Abi.md](Abi.md) §5.14
> **근거 표기:** **[V]** 로컬 소스를 읽었거나 직접 실행해 확인 · **[M]** UE4 설계 문서 기준 · **[R]** 추정

---

## 1. 축

모든 run의 manifest에 남는다. 표는 한 축만 바꾸어 비교한다 (02 §10, 02 §13).

| 축 | 값 | 어디서 정하나 |
|---|---|---|
| 물리 프로필 | `UEParity`(각 엔진을 UE 기본값에), `Normalized`(엔진 고유 감쇠·한계·결합·sleep 제거) | `-Profile=`, `Config/Profiles/<Name>.ini` |
| 백엔드 변형 | 엔진마다 하나 이상 (아래 §4) | `-BackendProfile=`, `Config/Profiles/Backend/<Engine>.<Variant>.ini` |
| 질량 규칙 | `UEBodyUtils`(기본, (ρV)^0.75) / `Physical` | `PhysicsTestSettings.MassRule`. 물리 프로필에 묶지 않는다 (DL17). 방법론 씬은 `bOverrideMass`로 질량을 고정한다 |
| 플래그 프로필 | `Shipped` / `MatchedAVX2` | 빌드 속성 (프리셋 test / test-avx2) |
| tick mode | FixedStep(기본) / Sync / Substepping / AsyncFixed | `PhysicsTestSettings.TickMode` |
| 오버라이드 | 명령줄 `-ini:`, `-ExecCmds`, 강제 오버라이드 | §2, §3 |

## 2. ini 계층

`BasePhysicsTest.ini` → `Profiles/<Profile>.ini` → `Profiles/Backend/<Engine>.<Variant>.ini` → 씬 고정값 → 명령줄 `-ini:` / `-ExecCmds` → (씬 저작) → **강제 오버라이드**.

| 섹션 | 뜻 | 누가 쓸 수 있나 |
|---|---|---|
| `[/Script/Engine.PhysicsSettings]` | UE 키 (UE 게임처럼 한 섹션, DL44). 모르는 UE 키는 기록만 한다 | Base, 프로필, `-ini:`. 변형은 프로필이 `EngineDefaultKeys`로 연 키만 |
| `[/Script/PhysicsTest.PhysicsTestSettings]` | 하네스 설정 (tick mode, sleep 축, 반발 임계, 질량 규칙, run 옵션) | 같다 |
| `[/Script/PhysicsTest.BodyInstanceDefaults]`, `[...PhysicalMaterialDefaults]` | 씬이 저작하기 **전에** 템플릿에 적용 | 같다 |
| `[/Script/PhysicsTest.ForcedOverrides]` | 씬이 저작한 **뒤에** 바디와 머티리얼에 적용 (§3, M9) | Base, 프로필, `-ini:`. 변형은 로드 오류 |
| `[ConsoleVariables]` | 하네스 물리 CVar(고정·해시)와 엔진 CVar(엔진 키) | Base, 프로필. 변형은 엔진 CVar만 |
| `[PhysicsTest.Backend.<Engine>]`, `[...<Engine>.Init]` | 엔진 키 (씬 옵션 / 모듈 init 옵션). 백엔드가 모르는 키는 거부 | 변형, `-ini:` |
| `[PhysicsTest.Profile]`, `[PhysicsTest.Variant]` | 메타데이터 (`EngineDefaultKeys`, `Label`, `FidelityNotes`, `DefaultForEngine`) | 각 파일 자신 |

- 변형 ini가 바꿀 수 있는 것은 자기 엔진 키와 프로필이 `EngineDefaultKeys`로 연 키뿐이다. 그 밖은 로드 오류다 (변형이 정규화를 조용히 되돌리지 못하게).
- 모든 값에 Origin이 붙는다: UEDefault / Profile / Variant / ScenePinned / CommandLine / Forced. manifest `profile.resolved[]`에 남는다.
- `physics_sha256`은 적용된 물리 키 중 프로필이 고정한 것(강제 오버라이드 포함)만 넣는다. EngineDefault 키, 엔진 키, run 옵션은 뺀다.

## 3. 강제 오버라이드 `[/Script/PhysicsTest.ForcedOverrides]` (M9)

```ini
[/Script/PhysicsTest.ForcedOverrides]
PhysicalMaterial.FrictionCombineMode=Average
PhysicalMaterial.bOverrideFrictionCombineMode=True
PhysicalMaterial.RestitutionCombineMode=Average
PhysicalMaterial.bOverrideRestitutionCombineMode=True
PhysicalMaterial.StaticFriction=Friction
```

- **키:** `PhysicalMaterial.<필드>`(필드는 `[PhysicalMaterialDefaults]`의 것) 또는 `BodyInstance.<필드>`(`[BodyInstanceDefaults]`의 것). 대소문자를 무시하고 정규 철자로 바꾼다 (`ForcedOverrides.PhysicalMaterial.FrictionCombineMode`).
- **금지 필드:** 하네스 질량 계산(BodyUtils, 02 §5.3)이 먼저 읽는 `Density`, `RaiseMassToPower`, `MassScale`은 로드 오류다. 질량이 이미 계산된 뒤에 적용되기 때문이다. 그런 값은 `[...Defaults]`나 씬에서 정한다.
- **관계 토큰:** `PhysicalMaterial.StaticFriction=Friction`은 그 머티리얼의 Friction을 정지마찰로 쓴다 (02 §10 Normalized의 SF := F). 평범한 값을 파일 순서대로 먼저 적용하고 관계 토큰을 마지막에 적용한다 (강제된 Friction을 읽는다).
- **적용 시점과 대상:** `FPhysScene::FlushDeferredActors`가 마샬링 바로 전에, 그 flush의 바디(제자리)와 그 바디의 머티리얼(씬이 소유한 것만: `CreatePhysicalMaterial`, 기본 머티리얼)에 적용한다. 씬이 소유하지 않은 머티리얼은 바꾸지 않고 Error 로그와 `foreign_materials`로 센다. 바뀐 값마다 Display 로그를 남긴다 (머티리얼은 하나씩, 바디는 같은 변경을 묶어 개수와 함께).
- **적용하지 않는 곳:** 게이트 씬(`IPhysicsTestScenario::AllowsForcedOverrides()` = false: `S05`·`S06`은 머티리얼별 결합 모드가 기준값의 일부다)과 게이트 실행기(`PhysicsGates`). 그래서 게이트 결과와 `Config/Golden/scene_hashes.json`은 그대로다.
- **기록:** manifest `profile.forced_overrides`(`config[]`, `applied`, 머티리얼·바디 수, `changes[]`), `profile.resolved[]`(origin Forced), 벤치 로그.
- **Normalized (Version 2):** 위 다섯 줄이다. SF ≤ F이면(UE 기본 SF = 0) 어느 엔진에서도 쌍의 정지마찰이 같다 (Chaos·PhysX SdkDefault는 max(F, SF), UE4Compat는 정지 = 동적, Jolt·Bullet은 동적만, DL41). 그러나 마샬링 바이트가 바뀌므로 Normalized의 `desc_sha256`은 M8 세션과 다르다.

## 4. 백엔드 변형

| 변형 | 파일 | 내용 | 게이트 |
|---|---|---|---|
| `Jolt.Default` | `Jolt.Default.ini` | Jolt 5.6.0 기본값을 명시 (속도 10 / 위치 2, convex radius 5 cm, penetration slop 2 cm, 결정성 켬, large island splitter 켬). M9: `SleepThresholdMapping=UE` (§5.1) | 03 목록 |
| `Jolt.SharpBoxes` (M9) | `Jolt.SharpBoxes.ini` | `Jolt.Default` + `ConvexRadius=0` (날카로운 박스). Jolt `PyramidScene`의 원래 설정이고, 박스 마진 정책 차이를 by-design으로 분리해 본다. M9 확인: `shape.box_margin`·`shape.convex_radius` 0, 떨어뜨린 상자가 `Jolt.Default`처럼 z 25 cm에 쉰다 (`PhysicsTest.JoltJoint.SharpBoxesVariant`), `M1.SingleBoxDrop` b의 첫 접촉이 한 프레임 이르다 (22 대 23) | not gated (리드 결정 전까지) |
| `Bullet.Default` | `Bullet.Default.ini` | Bullet 3.25 기본값 + `DampingModel=UE`(하네스 규칙). M9: `SleepThresholds=UE`(§5.1), `BallJoint=Point2Point` | 03 목록 |
| `Bullet.StaticAabbOff` (M9) | `Bullet.StaticAabbOff.ini` | `Bullet.Default` + `ForceUpdateAllAabbs=false` (비활성 객체의 AABB를 스텝마다 다시 계산하지 않는다, btCollisionWorld.cpp:197-212 [V]). 속도 손잡이. 더미에서는 결과를 바꾸는 by-design knob이다 (`Bullet.Default`와 같은 것으로 비교하지 않는다) | not gated. G11 `-G11Knob=variants:Default,StaticAabbOff` (좁힌 범위, 아래 판정) |
| `PhysX.UE4Compat` | `PhysX.UE4Compat.ini` | UE4 설정의 PhysX 5.11 (PGS, SAP, cm, UE4 substep 규칙). PhysX 기본 변형. M9: `JointRecipe=D6`, `JointActorOrder=ParentFirst` [M], `DebugVisualization` 기본 범주 | 03 목록 |
| `PhysX.UE4Compat-TGS`, `-Stab` | | UE4Compat + TGS / + stabilization | 03 목록 |
| `PhysX.SdkDefault` | `PhysX.SdkDefault.ini` | PhysX SDK 기본값 (m, tolerances scale (1, 10): 자기 프로세스). M9: `JointRecipe=Native`(spherical / revolute) [D] | 03 목록 |

- **`Bullet.StaticAabbOff` 판정 (M9, 준비 발견 F-1):** G11 `variants:Default,StaticAabbOff`(매 프레임 h_native·h_state)는 한 바디·조인트·탑 씬(`M1` b, `M6a`, `M6b`, `M6c`, `M2a` N=10, 두 프로필)과 **`M3.BoxPile` 1k Normalized + `bEnableEnhancedDeterminism`**(600 프레임)에서 성립한다. 기본 프로필의 더미에서는 frame 51(UEParity)/54(Normalized)부터 갈린다: Bullet은 `DBVT_BP_PREVENTFALSEUPDATE` 0이라 강제 AABB 갱신이 static 바닥 proxy를 매 스텝 fixed 트리에서 dynamic 트리로 옮기고(btDbvtBroadphase.cpp:319-329 [V]) 쌍 순서가 바뀐다. 결정성을 켜면 쌍과 섬 매니폴드를 바디 id로 정렬해(btOverlappingPairCache.cpp:374, btSimulationIslandManager.cpp:376-378 [V]) 그 차이가 솔버 순서로 새지 않는다. UEParity + 결정성은 frame 408에 갈린다 (잠든 바디의 AABB를 갱신하지 않는 차이 [R]). **리드 결정 (2026-10-22): 좁힌 범위를 받아들였다.** 03 M9의 G11 항목은 위 범위(한 바디·조인트·탑 씬과 결정성 켠 `M3.BoxPile` 1k Normalized)에서 판정하고, CTest `G11BulletStaticAabbOff`·`G11BulletStaticAabbOffPile`이 그 범위를 고정한다. 더미에서 이 변형은 **결과 중립이 아니다** (쌍 순서가 바뀌어 frame 약 51부터 갈린다): 결과를 바꾸는 by-design knob으로 문서화하고, 비교에서 `Bullet.Default`와 같은 것으로 다루지 않는다 (같은 엔진의 다른 series다). M9 거동 세션의 탑에서도 무너지는 `M2a.Tower` N=30(frame 254~293)과 N=10의 섭동 복제 하나(Perturb 3, frame 2)는 갈린다: 탑 씬의 중립은 N=10 기준 run의 것이다 ([Progress.md](Progress.md) M9 리드 결정).
- **주의 (Jolt, M9 J-2):** 반복 횟수를 바꾸는 변형을 만들 때 Jolt의 velocity step만 올리지 않는다. `M6c.HeavyChain`(100:1)에서 velocity 20·40·80 / position 2는 1~2 s 무렵 체인이 끊어진다 (기본 10 / 2는 버틴다: M9 거동 세션 `behavior-m9b` Normalized의 기준 run과 섭동 복제 3개에서 최대 신장 35.0~36.4 cm. 40 / 10은 8.4 cm. Jolt 구현자의 첫 측정 약 28 cm는 다른 빌드·기준 run 하나의 값이다). 더 수렴한 velocity 해가 가벼운 링크를 더 빨리 휘두르고 position step 2번(Baumgarte 0.2)이 회전 선형화 오차를 따라잡지 못한다 [R].
- 새 변형은 `GateTests.cpp`의 `GetOptInGateVariants`에 넣기 전까지 G 게이트가 "not gated"로 보고만 한다. preflight(G9)는 `PhysicsTest.Bench.PreflightSweep`이 모든 씬 × 두 프로필로 돈다 (M9 준비부터 두 새 변형 포함, M9부터 조인트 씬도 세 엔진 모든 변형에서).

**M9 변형 키 (엔진 키, `[PhysicsTest.Backend.<Engine>]`)**

| 엔진 | 키 | 값 (기본) | 뜻 | 증거 |
|---|---|---|---|---|
| Jolt | `SleepThresholdMapping` | `UE` \| `Native` (UE) | UEParity의 UE 머티리얼 sleep 임계 매핑(§5.1) / Jolt 자체 0.03 m/s·0.5 s. Normalized는 sleep 끔이라 영향 없음 | [D] |
| Bullet | `SleepThresholds` | `UE` \| `Bullet` (UE) | UE 머티리얼 임계 + 카운터 에뮬레이션(§5.1) / Bullet 자체 80 cm/s·1 rad/s·2 s | [D] |
| Bullet | `BallJoint` | `Point2Point` \| `Generic6DofSpring2` (Point2Point) | ball 조인트의 Bullet 클래스. 6DOF2는 비교용이다 (M6a ball 신장 0.005 cm 대 2.35 cm, 그러나 가벼운 구가 큐브를 매단 이중 진자에서는 45 cm 대 4.5 cm: 만능이 아니다) | [D] |
| PhysX | `JointRecipe` | `D6` \| `Native` (UE4Compat 계열 D6 [M], SdkDefault Native [D]) | D6(UE4 경로) / spherical·revolute. M6a·M6c에서 궤적이 비트 단위로 같다 | [M] / [D] |
| PhysX | `JointActorOrder` | `ParentFirst` \| `ChildFirst` (ParentFirst [M]) | actor0 = parent(UE4의 actor 뒤집기) / child. **거동 스위치다:** PhysX는 선형 행을 actor1 프레임 원점에 건다. 사용자가 UE 4.27로 확인하면 키만 바꾼다 (Q10) | [M] |
| PhysX | `DebugVisualization` | `None` \| 범주 목록 (CollisionShapes, JointLocalFrames, JointLimits, ContactNormals) | viz run의 PhysX 네이티브 드로 범주. bench·behaviour run은 eSCALE 0이라 무시된다. ContactPoints는 더미에서 명령이 세 배라 기본에서 뺐다 | [D] |

네 PhysX 변형 ini에 키를 더해 PhysX run의 `options_sha256`·`variant_sha256`가 M8과 다르다 (`desc_sha256`은 그대로). Jolt·Bullet은 UEParity의 sleep 결과(해시 포함)가 M8과 다르다 (Normalized는 sleep 끔이라 그대로).

## 5. 엔진별 매핑표

02 §10 표를 엔진별로 펼친 것이다. M9에서 세 엔진 구현자가 sleep·조인트·드로 칸을 채웠다.

| 항목 | 프로필 값 | Chaos (M13) | PhysX UE4Compat | Jolt | Bullet |
|---|---|---|---|---|---|
| 중력 | 둘 다 (0, 0, −980) | 프로젝트 값 | `setGravity` | `SetGravity` (m) | 모든 바디에 직접 (M5) |
| 선형·각 damping | UEParity 0.01 / 0, Normalized 0 / 0 | UE 순서 | UE 순서 (PhysX 식) | UE 순서 | 하네스 pre-tick으로 UE 순서 (`DampingModel=UE`) |
| 최대 각속도 | UEParity 62.83 rad/s, Normalized 1e7 °/s | `MaxAngularSpeedSq` | `maxAngularVelocity` | `mMaxAngularVelocity` | pre-tick 에뮬레이트 + π/4 스텝 한계 (`bullet.angular_motion_limit_hits`) |
| 결합 | UEParity 머티리얼 모드(기본 Average), Normalized Average 강제 + SF := F (§3) | UE 규칙 | `PxCombineMode`, 정지 = 동적 [M] | 콜백, 동적만 | 전역 콜백, 동적만 |
| 반발 임계 | UEParity EngineDefault, Normalized 50 cm/s | 1000·dt (가속도) | 200 cm/s (`bounceThresholdVelocity`) | 1000·dt cm/s | 1000·dt cm/s |
| sleep | UEParity 켬, Normalized 끔 | UE 머티리얼 임계 | UE4 규칙 (PhysX 임계 × SleepFamily) | 끔은 `mAllowSleeping=false`. UEParity: **UE 임계 매핑(근사, 전역)** `SleepThresholdMapping=UE` (§5.1, `jolt.sleep_mapping`) | 끔은 `DISABLE_DEACTIVATION`. UEParity: **UE 임계 + 카운터(근사, 바디별)** `SleepThresholds=UE` (§5.1, `bullet.sleep_rule`) |
| gyroscopic / CCD | 둘 다 끔 | 파티클 플래그 | 무시 (UE4 PhysX 3.4에 항 없음) | 끔 | 끔 (Bullet 기본 켬을 끔) |
| 반복 횟수 | EngineDefault | 8 / 2 / 1 | 8 / 1 (UE4) | 10 / 2 | 10 |
| inertia conditioning | UEParity 켬, Normalized 끔 | 파티클 플래그 | 없음 (기록) | 없음 (기록) | 없음 (기록) |
| 조인트 (M9) | 두 프로필 같음: hard ball / hinge ([Abi.md](Abi.md) §9.4) | UE `FConstraintInstance` 기본값 그대로 (projection 켬) | D6(`JointRecipe`), actor0 = parent(`JointActorOrder`), projection 없음 (PhysX 5.11, `joint.projection` 0). SdkDefault는 spherical / revolute | `PointConstraint` / `HingeConstraint` (COM 공간 프레임, `sFixedToWorld`), projection 없음 | `btPoint2PointConstraint` / `btHingeConstraint` (COM 프레임, hinge Ry(90°), 씬 월드 앵커), projection 없음. 비교용 `BallJoint=Generic6DofSpring2` |
| 조인트 반복 | 엔진 반복 그대로 | UE 기본 | 8 / 1 (PGS), TGS 같은 수 | 10 / 2 (velocity / position step) | 10 (Baumgarte ERP 0.2, 위치 반복 없음) |
| 네이티브 드로 (M9) | viz run만 | Chaos DD (Development DLL, M13) | `PxVisualizationParameter` → `PtDebugDrawCmd` (`DebugVisualization`) | 없음 | viz run에만 `btIDebugDraw` 드로어 (`DBG_NoDeactivation` = 프로필 sleep 끔, 전역 bit 복원) |

### 5.1 UEParity sleep 임계 매핑 (M9)

UE(Chaos)는 섬마다 "모든 파티클의 평활 속도가 머티리얼 임계 × `SleepThresholdMultiplier` 아래"인 스텝을 세고 머티리얼 `SleepCounterThreshold`를 넘으면 섬을 재운다 (IslandManager.cpp:239-256, :2773-2783 [V]). SDK 엔진에는 같은 규칙이 없으므로 근사하고, 근사는 effective 키로 기록한다 (02 §10 "근사는 플래그").

| 엔진 | 매핑 | 매핑하지 않은 것 (기록) | 확인 [V] |
|---|---|---|---|
| Jolt (`SleepThresholdMapping=UE`) | Jolt의 sleep 검사는 전역 하나다 (COM과 바운딩 박스 두 점이 반지름 `mPointVelocitySleepThreshold × mTimeBeforeSleep`의 구 안에 `mTimeBeforeSleep` 동안 머문다, PhysicsSystem.cpp:2527, Body.cpp:160-183). `mPointVelocitySleepThreshold` = 잘 수 있는 dynamic 바디의 min(선 임계 × 배수) / 2 (일정한 drift 2배가 구를 채운다), `mTimeBeforeSleep` = (max 카운터 + 1 − 0.01) × dt. Base 머티리얼(1 cm/s, 카운터 4)은 0.5 cm/s·0.0832 s | 바디·섬별 임계, 각 임계(회전은 박스 점의 이동으로 센다), Chaos 평활 속도 (`jolt.sleep_mapping` 텍스트). G9 `sleep.criteria`는 규칙 키 `const:0` (M9 수정 라운드 0: 엔진의 두 설정값이 살아 있는 바디에서 다시 계산한 규칙과 다른 수. 값 자체는 `jolt.sleep_threshold`. 처음의 `const:0.5`는 Base 머티리얼 값이라 다른 머티리얼 임계·배수를 쓰는 씬에서 실패했다) | 바닥 위 정지 상자 5 프레임 뒤 잠듦 (자체 값 30 프레임). μ 0.55 경사 상자는 더는 3 cm에서 잠들지 않고 46.8 cm 미끄러진다 (UE 규칙으로 계산해도 Chaos가 재우지 않는 상자, [R]) |
| Bullet (`SleepThresholds=UE`) | dynamic 바디마다 Bullet 임계 = 첫 요소 머티리얼 `SleepLinear/AngularVelocityThreshold` × 배수 (`setSleepingThresholds`). post-tick이 임계 아래 연속 스텝 수를 세고 `m_deactivationTime`을 미리 넣어 바디가 N번째 스텝에 `WANTS_DEACTIVATION`, 섬이 다음 스텝에 잠들어 Chaos 섬 카운터 > N과 같은 스텝에 Sleeping으로 읽힌다 | Bullet은 스텝 속도, Chaos는 평활 속도(lerp 0.3). Bullet은 바디마다 세고 섬의 모든 바디가 원하면 섬이 잠든다 (Chaos는 섬 카운터 하나). 접촉으로 깬 바디는 임계 아래인 동안 WANTS로 남는다. 각 크기 보정 없음, 고립 파티클 배수 1 (`bullet.sleep_rule` 텍스트). G9 `sleep.criteria` 규칙 키 `const:0` | 카운터 4·30·Sensitive(1 cm/s / 20)에서 "잠들기 전 임계 아래 프레임 수 == SleepCounterThreshold" (`PhysicsTest.BulletBackend.UESleepMapping`). `M2a.Tower` N=10의 Bullet 탑은 2 → 8.7 cm/s로 계속 기어 UE 임계에서 잠들지 않는다 (S-2, 자체 규칙은 frame 121에 재워 이것을 감췄다) |
| PhysX | UE4 규칙 그대로 (PhysX 임계 × SleepFamily, wake counter 0.4 s) | - | M5·M6 |

## 6. preflight diff (M9)

벤치는 preflight에서 run의 effective 덤프를 G9 규칙(`Config/Golden/tolerances.json`)과 비교한다 ([TimingContract.md](TimingContract.md) §6.1). M9부터 그 비교를 키마다 `preflight_diff.json`(physicstest.preflight_diff/1)으로 남긴다: key, source, 엔진 값(effective canonical·native), 규칙(기대식), 리졸브한 기대값(resolved), 상태(match / mismatch / unchecked / skipped / no_rule / not_readback)와 노트. 로그에는 상태별 개수 한 줄과, `mismatch`·`no_rule`이면 키마다 Warning이 남는다. 판정은 바뀌지 않는다 (G9가 그대로 판정한다).
