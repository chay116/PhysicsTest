# 체크포인트 기록 (Checkpoints)

> **관련 문서:** [03_WorkPlan.md](03_WorkPlan.md) §1.6 · [Progress.md](Progress.md) M4, M5 · [02_Architecture.md](02_Architecture.md) §8, §11 · [04_DecisionLog.md](04_DecisionLog.md) M5 구현 메모
> **근거 표기:** **[V]** 직접 빌드·실행해 확인 · **[R]** 추정
> **비공식:** 이 문서의 타이밍은 모두 비공식이다 (게이트 전, 전원 계획 Balanced 그대로, 측정 프로토콜 없음). 외부에 보고하지 않는다. 원시 수치는 `Saved/` 아래에만 있고 커밋하지 않는다.

---

## 체크포인트 A (M4 + M5, 2026-10-21)

**시점:** M4(뷰어 1단계)와 M5(Bullet + PhysX 백엔드)가 모두 끝난 직후. **목적:** 단위·규약 버그를 게이트 작업(M6) 전에 드러내고, 실제 타이밍 규모를 처음 본다.

**검증 환경:** Windows 11 Pro 10.0.26200, i5-13600KF 20 logical, RTX 3080, MSVC 14.44.35207 / cl 19.44.35229, CMake 4.1.2, Ninja 1.13.2, 전원 계획 Balanced (`381b4222-…`). 빌드: M5 마지막 상태, 세 프리셋 `--clean-first` 경고 0, CTest test 56/56, dev 63/63, dbg 63/63 ([Progress.md](Progress.md) M5).

### 결과 요약

| # | 통과 조건 (03 §1.6) | 결과 | 근거 |
|---|---|---|---|
| 1 | 뷰어 Single 레이아웃에서 Jolt, Bullet, PhysX `UE4Compat`/`-TGS`/`-Stab`/`SdkDefault`를 엔진 전환으로 하나씩 띄우고 `M1.SingleBoxDrop`·`M3.BoxPile` 1k에서 M4 시각 체크리스트를 통과. UE4Compat 계열 → SdkDefault(또는 반대)는 "재시작 필요" 후 동작, 계열 안 전환은 재시작 없이 | **통과 (헤드리스)**. 창 모드의 눈 확인은 사용자 몫으로 남긴다 | §1. 6 엔진·변형 모두 헤드리스 스크린숏 (M1 a, BoxPile 1k), 픽셀 수, `Viewer.PhysXVariantSwitchRules`, `Viewer.HeadlessRestart` |
| 2 | 세 엔진 모두 S00 \|z60 − 501.83328\| ≤ 2e-3 cm, M1 정지 접촉 normal·(+Z) > 0.999, 질량 readback 1e-5 상대오차 이내 | **통과** | §2. `SdkBackend.CheckpointAConventions` (엔진·변형 6행) |
| 3 | `M3.BoxPile` 1k/5k, T = 1, Normalized, 엔진마다 fresh process 5회의 `T_frame` 중앙값(QPC)을 CSV로 | **통과** | §3. `Saved/Bench/M5Checkpoint/summary.csv` |
| 4 | 발견한 규약 버그 목록과 처리 결과 | **기록** | §4 (11건 처리, 2건 열림, 거동 메모 3건) |

**판단 (03 §1.6):** 계획 공수(누적 약 31 pd)는 사람 기준이고 M0~M5는 에이전트가 하루에 했으므로 1.2배 규칙을 같은 척도로 잴 수 없다. 범위를 줄일 이유는 없었다: M5의 위험 레버 R4(`-TGS`/`-Stab`을 M9로 미룸)도 쓰지 않았다. **사용자 결정이 필요한 것:** M2.5(Chaos hello-DLL 스파이크)를 아직 하지 않았으므로 03 §1.6대로 이 시점에 사용자와 일정을 정한다. 그다음은 M6(물리 게이트 + ABI 핵심 v1.0 동결)이다.

### 1. 뷰어에서 본 엔진 (조건 1)

모든 실행은 `PhysicsTestViewer.exe -Headless`(창 없음)다. M1 a: Normalized, 300 프레임 뒤 일시정지, 960×540, `-NoUI`, `DebugDrawCollisions/Transforms/Mass 1`, `InertiaScale 0`. BoxPile 1k: UEParity, 900 프레임 뒤 일시정지, Stats 패널, `DebugDrawIslands 1, DebugDrawBounds 1`, "Draw everything". 픽셀 수는 정확히 그 색인 픽셀이다 (위 20 px 메뉴 막대 제외, MSAA 가장자리는 세지 않음). 스크린숏은 `Saved/Checkpoints/A/`(커밋하지 않음), 문서 그림 4장은 `docs/images/m5_*.png`다.

| 엔진 / 변형 | M1 a: Dynamic 노랑 (255,255,0) | Static 빨강 (255,0,0) | 활성 접촉 (250,0,0) | 접촉 점 | BoxPile 1k (900 프레임): 하네스 섬 | 잠든 바디 | 모습 |
|---|---|---|---|---|---|---|---|
| Jolt 5.6.0 | 1647 | 535 | 233 | 4 | 99 | 300 | 기둥 100개가 선다 (M4 그림 2와 같다) |
| Bullet 3.25 | 1660 | 540 | 220 | 4 | 188 | 39 | **기둥이 무너져 흩어진다** (§4 N-1) |
| PhysX UE4Compat | 1657 | 541 | 226 | 4 | 100 | 430 | 기둥 100개가 선다 |
| PhysX UE4Compat-TGS | 1654 | 541 | 227 | 4 | 89 | 164 | 기둥 몇 개가 넘어진다 |
| PhysX UE4Compat-Stab | 1657 | 541 | 226 | 4 | 100 | 1000 | 모두 잠든다 (모든 섬 Black) |
| PhysX SdkDefault | 1658 | 541 | 228 | 4 | 73 | 300 | 기둥이 서고 일부가 서로 닿아 섬이 합쳐진다 |

- M4 시각 체크리스트의 색 항목(상태색, 정지 바닥, 활성 접촉 원 4개, COM 축, 질량 문자열)이 여섯 행 모두 같은 크기로 나온다: 엔진과 무관하게 같은 Chaos 규약 스냅샷이다. 섬 팔레트와 Black(잠든 섬)도 나온다 (BoxPile 그림).
- **엔진 전환:** `Viewer.PhysXVariantSwitchRules`(한 프로세스): PhysX 변형 없음 → UE4Compat → -TGS → -Stab → UE4Compat은 모두 Opened, SdkDefault는 RestartRequired ("the backend fixes its settings process-wide at InitGamePhys (GLOBAL_SCENE_SETTINGS) …"). 실행 중인 씬은 그대로 남는다. Null ↔ Jolt 전환은 M4(`Viewer.SessionPauseStepAndSwitch`).
- **재시작 후 동작:** `Viewer.HeadlessRestart`: `-ViewerCmds=open PhysX UE4Compat-TGS, open PhysX SdkDefault, restart`로 헤드리스 부모가 UE4Compat → -TGS(같은 프로세스) → SdkDefault(RestartRequired) → `restart`를 하고, 자식 viewer가 `-Backend=PhysX -BackendProfile=SdkDefault … -Headless -Frames=4`로 떠 SdkDefault 씬을 열고 스크린숏(manifest `variant: SdkDefault`, `frame: 4`)을 쓴 뒤 0으로 끝난다. 부모는 자식을 기다려 0으로 끝난다. 창 모드 버튼(Engines 패널 "Restart the viewer with this selection")은 같은 함수(`RequestRestart`)를 쓴다.
- **사용자 확인으로 남긴 것:** 창 모드에서 Engines 패널로 바꿔 보기와 "Restart required" 버튼 (자동 검사는 사용자 데스크톱을 쓰지 않도록 헤드리스로만 했다). 명령: `out\dev\bin\PhysicsTestViewer.exe -Backend=PhysX -BackendProfile=UE4Compat '-Scenario=M3.BoxPile'` (PowerShell에서는 점이 든 인자를 따옴표로 감싼다).

### 2. 규약 확인 (조건 2)

`PhysicsTest.SdkBackend.CheckpointAConventions` (dev, test, dbg 모두 통과). S00.FreeFall은 Normalized 60 프레임, M1.SingleBoxDrop a는 Normalized 300 프레임(viz run이라 접촉 리스너가 있다), 질량은 그 씬의 상자(1000 kg, 100 cm 큐브)를 엔진에서 읽은 값이다. 관성은 엔진이 주축을 바꿔 둘 수 있어 텐서 R·diag(I)·Rᵀ로 비교한다.

| 엔진 / 변형 | S00 \|z60 − 501.83328\| (cm) | M1 a 상자-바닥 접촉 | min normal·(+Z) | 상자 z (cm) | 질량 상대오차 | 관성 텐서 상대오차 |
|---|---|---|---|---|---|---|
| Jolt | 1.58e-5 | 4점 | 1 (≥ 0.9999995) | 48 (penetration slop 2 cm) | 4.7e-8 | 6.9e-8 |
| Bullet | 1.58e-5 | 4점 | 1 | 50 | 4.7e-8 | 3.1e-8 |
| PhysX UE4Compat | 9.40e-5 (cm 단위 float) | 4점 | 1 | 50 | 6.1e-8 | 2.5e-8 |
| PhysX UE4Compat-TGS | 9.40e-5 | 4점 | 1 | 50 | 6.1e-8 | 2.5e-8 |
| PhysX UE4Compat-Stab | 9.40e-5 | 4점 | 1 | 50 | 6.1e-8 | 2.5e-8 |
| PhysX SdkDefault | 1.58e-5 | 4점 | 1 | 49.9999 | 6.1e-8 | 3.1e-8 |

- 기준 ≤ 2e-3 cm, > 0.999, ≤ 1e-5를 모두 큰 여유로 만족한다. S00의 v60은 모두 −980 cm/s다 (`SdkBackend.FreeFallS00`). PhysX 50 cm 큐브 질량은 37.3837 kg로 01 §3.2의 37.384 kg와 같다 (`PhysXBackend.UE4CompatReadback`).
- 정지 접촉의 Phi: Jolt −2.0 cm 안팎(slop), Bullet −5.7e-5 ~ +1.2e-5 cm(split impulse), PhysX ±0(float). 그래서 하네스 섬 간선에 솔버 임펄스(ACTIVE)를 더했다 (§4 #8).

### 3. 비공식 타이밍 (조건 3)

**비공식 (게이트 전, 전원 계획 Balanced, 측정 프로토콜 없음).** test 프리셋 `PhysicsTestBench.exe -Threads=1`, FixedStep 1/60, 600 프레임, headline 윈도 1..599, 설정마다 fresh process 5회(반복을 엔진·프로필·크기 사이에 섞음). 값은 run별 `T_frame` 중앙값(QPC)의 중앙값 [최소..최대], ms. CSV: `Saved/Bench/M5Checkpoint/summary.csv`(run 디렉터리 138개 포함).

| 엔진 / 변형 | Normalized 1k | Normalized 5k | 참고: Normalized 10k (3회) | 참고: UEParity 1k | 참고: UEParity 5k |
|---|---|---|---|---|---|
| Jolt 5.6.0 | **1.004** [1.004..1.057] | **5.759** [5.729..6.338] | 12.738 | 0.931 | 5.132 |
| Bullet 3.25 | **1.692** [1.684..1.747] | **11.272** [11.250..11.444] | 26.257 | 1.747 | 10.979 |
| PhysX UE4Compat | **0.620** [0.616..0.626] | **3.822** [3.791..3.841] | 8.514 | 0.566 | 3.410 |
| PhysX UE4Compat-TGS | **0.907** [0.899..0.910] | **5.493** [5.466..5.520] | 12.476 | 0.897 | 5.447 |
| PhysX UE4Compat-Stab | **0.619** [0.619..0.626] | **3.816** [3.777..3.828] | 8.602 | 0.051 | 0.343 |
| PhysX SdkDefault | **0.625** [0.625..0.626] | **3.672** [3.644..3.726] | 8.316 | 0.601 | 3.497 |

- 5회의 마지막 `h_native`는 설정마다 같다 (결정성). Normalized의 UE4Compat-Stab은 UE4Compat과 비트 단위로 같은 시뮬레이션이다 (안정화는 sleep을 끈 씬에서 효과가 없다, §4 N-3).
- 이 수치로 보는 규모 (비공식): T = 1, Normalized 5k에서 PhysX(UE4Compat 3.8 ms, SdkDefault 3.7 ms)가 가장 빠르고, Jolt는 UE4Compat의 1.5배, Bullet은 3배, TGS는 1.4배다. Bullet의 프레임에는 무너지는 더미의 추가 접촉이 들어 있다 (§4 N-1). UEParity의 -Stab이 작은 것은 상자가 일찍 잠들기 때문이다 (평균 0.18 / 1.18 ms). Chaos는 M13 이후에 이 표에 들어간다.
- 측정 품질의 한계: 전원 계획 Balanced, affinity·우선순위·타이머 해상도 고정 없음(M7), 적합성 스탬프 없음(M10), 같은 기계에서 사용자 앱이 열려 있었다. 범위 [최소..최대]는 대부분 중앙값의 5 % 안이지만 Jolt 5k(두 프로필)와 Bullet UEParity 5k에 한 번씩 10~18 % 튄 run이 있다.

### 4. 발견한 규약 버그와 처리 (조건 4)

**처리한 것** (M5 구현과 통합에서 찾아 고쳤다. 상세는 [Progress.md](Progress.md) M5 §3)

| # | 찾은 것 | 영향 | 처리 | 확인 |
|---|---|---|---|---|
| 1 | Bullet은 힘 적분 **전에** 감쇠한다 (02 §8.1의 계수 변환은 배수만 맞춘다) | UE 기본 감쇠 0.01에서 30 프레임에 0.08 cm/s 차이, `CommandsMatchNull` 실패 | pre-tick이 UE 순서를 에뮬레이트 (`DampingModel=UE`, 변환은 `Converted`로 남김) | `BulletBackend.DampingOrder`, `SdkBackend.CommandsMatchNull` |
| 2 | Bullet 각속도 한계의 근거가 틀렸다: 94.2 rad/s 클램프는 `btDiscreteDynamicsWorld`가 부르지 않는 함수의 값 | Normalized에서 47.1 rad/s 위의 회전각이 줄어드는 것을 놓쳤다 (속도는 그대로) | `bullet.angular_motion_threshold` π/4 per step(COMPILE_TIME)과 넘은 수(READBACK). 02·03·04(DL16)·ini 주석 정정 | `BulletBackend.AngularVelocityLimits` (57.8°·82.6° per step이 식과 같다) |
| 3 | Bullet `calculateVelocity`(float acos)가 회전하지 않은 kinematic 바디에 약 0.04 rad/s를 만든다 | kinematic 원점 속도 오차 | 목표 속도를 double로 계산해 pre-tick이 넣는다 | `SdkBackend.KinematicOriginVelocity` (ω.z = 31.4159) |
| 4 | Bullet `setGravity`는 깨어 있는 바디만 바꾼다 | 잠든 채 중력이 바뀐 바디가 깨면 옛 중력으로 떨어진다 | 잠든 dynamic 바디도 직접 바꾼다 | `BulletBackend.GravityChangeReachesSleepingBodies` (음성 대조 포함) |
| 5 | PhysX: SdkDefault 씬(m)이 UE4Compat PxPhysics(cm, (100, 1000))에서 돌 수 있었다 | sleep 임계·contact offset이 100배 어긋난 결과가 조용히 나온다 | CreateScene이 `tolerances.length = 100 × LengthScale`을 요구하고, 레지스트리·Kit도 다른 init 옵션을 거부 | `PhysXBackend.UE4CompatFamilySharesOneProcess` |
| 6 | PhysX는 섬이 잠드는 스텝에 그 섬의 접촉을 보고하지 않는다 | 잠든 섬이 접촉 없이(고립된 것처럼) 그려진다 | viz에서 직전 스텝 보고를 유지해 `PT_CONTACT_SLEEPING`으로 낸다 (Jolt M4 규칙과 같은 의미) | `PhysXBackend.SleepingContactsAreKept` |
| 7 | PhysX 속도 한계 "무제한" 값이 PhysX 범위 밖(1e16 초과 거부, 각속도 1e7에서 잘림)이고 MaxDepenetration 0은 거부된다 | 생성 오류 또는 조용한 클램프 | 1e16 native/s, 1e7 rad/s, `MaxDepenetrationRule`로 UE4 프로젝트 값 0 → PX_MAX_F32 | `PhysXBackend.OptionsAreValidated`, effective 키 |
| 8 | 정지 PhysX 접촉은 Phi ≈ +0(float)이라 하네스 섬 간선 규칙(Phi ≤ 0)이 놓친다 | Normalized M1에서 하네스 섬 0 | 간선 = Phi ≤ 0 또는 솔버 임펄스(ACTIVE) | `DebugDraw.IslandBuilder` (Phi +1e-5 + ACTIVE는 간선, SPECULATIVE는 아님) |
| 9 | 스냅샷 소비자가 백엔드가 보낸 `BodyIndex`로 배열 크기를 정했다 (M4 검토) | PT_INDEX_NONE이면 범위 밖 쓰기, 쓰레기 값이면 거대 할당 | 호스트 캡처가 버리고 로그, 소비자는 `GetBodyIndexBound` | `DebugDraw.SnapshotBodyIndexBounds` |
| 10 | 뷰어: PhysX "(Default / none)"과 UE4Compat 계열이 같은 scale인데 init 옵션 문자열이 달라 서로 RestartRequired | 불필요한 재시작 | UE4Compat 계열 ini는 기본값과 같은 `ToleranceScale`을 적지 않는다 (Bullet.Default와 같은 규칙) | `Viewer.PhysXVariantSwitchRules` |
| 11 | 공유 시험의 Jolt 전용 기대값 (캡슐은 언제나 axis_remap, 임펄스는 언제나 NaN) | Bullet 등록 시 실패 | capability와 엔진 캡슐 축으로 판정 (Bullet `btCapsuleShapeZ`는 axis_remap 0) | `SdkBackend.ShapeWrapping`, `SingleBoxDropRestingContact` |
| 12 | (M5 수정 라운드 0) PhysX: `PUT_TO_SLEEP`으로 재운 바디의 마지막 스텝 점이 유지 목록과 직전 스텝 목록 양쪽에 들어가 스냅샷에 두 번 나왔다 | 잠든 동안 접촉 점 수가 두 배 (M1 a: 4점 → 8점), Stats·Inspector 수와 유지 배열 용량을 잡아먹음 | 직전 스텝의 각 점을 둘 중 한 곳에만 넣는다 (`FlushKeptContactCommands`) | `SdkBackend.PutToSleepKeepsEachContactOnce` (세 엔진 모두 4 → 4. 옛 코드로 되돌린 음성 대조에서 PhysX 8점으로 실패) |
| 13 | (M5 수정 라운드 0) Bullet: 모듈 키 `ContactBreakingThreshold`를 cm로 읽어 0.01을 곱했고 `contact.speculative_distance`에 그 값(2 cm)을 냈다. 디스패처의 기본 상대 모드에서 이 전역은 셰이프 angular motion disc에 곱하는 계수다 | 보고 값이 엔진이 쓰는 값이 아님 (50 cm 큐브 실제 약 0.87 cm, 100 cm 큐브 1.73 cm). 기본값에서만 우연히 맞음 | 키는 Bullet 값 그대로(0.02), effective는 매니폴드·셰이프의 `getContactBreakingThreshold`를 READBACK | `BulletBackend.EffectiveSettings` (1.732 cm), `BulletBackend.GlobalCombineRule` (0.03 readback) |

**열린 것**

| # | 찾은 것 | 처리 계획 |
|---|---|---|
| O-1 | 잠든 바디에 쓴 속도(`PT_CMDF_AUTO_WAKE` 없는 `SET_*_VELOCITY`, `SET_BODY_STATE`)가 엔진마다 다르다 (M5 수정 라운드 0에서 정리): Null·Jolt는 재운 채 유지(깨운 뒤 그 속도로 움직임), Bullet은 잃음(`updateActivationState`가 `ISLAND_SLEEPING` 바디의 속도를 스텝마다 0으로, btDiscreteDynamicsWorld.cpp:605-620 [V]), PhysX는 0이 아닌 속도로 바로 깨움(NpRigidDynamic.cpp:253, :286 [V], `SET_BODY_STATE` 포함) | `SdkBackend.VelocityOfASleepingBody`가 세 동작을 고정한다. M6 ABI 동결 전에 하나로 맞출지 계약으로 둘지 정한다 ([Abi.md](Abi.md) §9) |
| O-2 | PhysX `NormalImpulse`/`FrictionImpulse` 값은 아직 NaN (점별 impulse는 ACTIVE 판정에만 쓴다) | M9 (`PT_CAP_CONTACT_NORMAL_IMPULSE`) |

**거동 메모** (규약 버그가 아니다)

| # | 관찰 | 해석 |
|---|---|---|
| N-1 | Bullet `M3.BoxPile` 1k: 120 프레임에 기둥 100개가 서 있고(하네스 섬 72) 300 프레임에 기울고 900 프레임에 흩어진다 (UEParity 섬 188, Normalized 169). Jolt·PhysX UE4Compat은 선다 | 반발 0만(반복 10): 무너짐(176). 반복 50만(반발 0.3): 무너짐(141). **반발 0 + 반복 50: 선다(100).** 단위·규약 오류가 아니라 Bullet SI 솔버(반복 10)가 반발 0.3인 ±15° 회전 10단 스택을 버티지 못하는 거동이다. 성능 씬 정의는 그대로 두고, M9 거동 씬에서 다시 본다 |
| N-2 | Bullet UEParity BoxPile은 거의 잠들지 않는다 (900 프레임 39개) | Bullet 섬은 broad phase 쌍으로 합쳐져(네이티브 섬 3~6개) 섬 전체가 sleep 조건을 만족해야 잠들고, sleep 기준도 Bullet 자신의 값이다 (UE 임계 매핑은 M9) |
| N-3 | PhysX Normalized에서 UE4Compat-Stab = UE4Compat (비트 동일) | `eENABLE_STABILIZATION`은 sleep 검사 안에 있어 `eDISABLE_SLEEPING`에서는 효과가 없다 (DySleep.cpp:21-30, DyDynamics.cpp:1184-1187 [V]) |

### 5. 원시 결과 위치 (커밋하지 않음)

- `Saved/Bench/M5Checkpoint/` — 비공식 타이밍 run 138개(`<엔진>_<프로필>_N<수>_r<회>/`의 `timings.csv`, `manifest.json`, `effective_settings*.jsonl`)와 `summary.csv`, `runs.txt`.
- `Saved/Checkpoints/A/` — 엔진·변형별 M1 a, BoxPile 1k 헤드리스 스크린숏과 `mode = viz` manifest.
