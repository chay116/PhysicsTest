# 04. 설계 결정 기록 (Decision Log)

> **작성일:** 2026-10-21
> **상태:** 설계 초안 v1.0 (구현 전)
> **관련 문서:** [01_Feasibility.md](01_Feasibility.md) · [02_Architecture.md](02_Architecture.md) · [03_WorkPlan.md](03_WorkPlan.md) · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)
> **대상 버전:** UE 5.8.3 (`D:\UnrealEngine`) · PhysX 5.11.0 (`da950a35`) · Bullet 3.25 (`2c204c49`) · Jolt 5.6.0 (`e77f1755`)
> **근거 표기:** **[V]** 로컬 소스를 읽었거나 빌드·실행해 확인함 · **[M]** UE 4.18 제3자 미러에서 본 내용. 미확인이며 복사하지 않음. 사용자가 4.27로 확인할 대상 · **[R]** 기억이나 추정. 미검증

이 문서는 PhysicsTest 설계에서 내린 결정과 그 이유를 한곳에 모은다. 구성은 다음과 같다.

1. **고정 결정 D1~D7:** 바꾸지 않는 전제다. D1에는 개정 하나가 있다.
2. **설계 결정 DL01~DL54:** DL01~DL35는 설계 단계의 결정을 옮기고, 적대적 리뷰가 무효화한 부분을 고쳤다. DL36 이후는 리뷰와 단계화 정책에서 새로 생긴 결정이다.
3. **사용자에게 묻는 질문 Q1~Q19:** [01_Feasibility.md](01_Feasibility.md) §10의 Q1~Q10 번호를 그대로 쓰고, 새 질문은 Q11부터 붙인다.

구조의 세부는 [02_Architecture.md](02_Architecture.md), 일정은 [03_WorkPlan.md](03_WorkPlan.md), 헤더 초안은 [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)에 있다. 문서끼리 어긋나는 곳이 있으면 이 기록을 기준으로 맞춘다.

---

## 0. 표기

### 0.1 상태와 단계

| 표기 | 뜻 |
|---|---|
| 유지 | 원래 결정을 그대로 둔다. 보강 노트가 붙을 수 있다 |
| 정정 | 리뷰가 틀렸다고 확인한 부분을 고쳤다 |
| 단계화 | 결정은 그대로 두고, MVP에서 만드는 범위와 나중으로 미루는 범위를 나눴다 |
| 신규 | 리뷰나 단계화 정책에서 새로 생긴 결정이다 |
| [MVP] | M0~M6에서 만든다. 게이트와 ABI 동결 마일스톤(M6)까지다 |
| [확장 Mx] | 이름 붙은 이후 마일스톤 Mx에서 만든다. "선택"이라는 뜻이 아니다. 헤드라인에 필요한 M7, M8, M10, M11도 여기에 속한다 |

### 0.2 출처 표기

| 표기 | 뜻 |
|---|---|
| ue-faithful · bench-rigor · chaos-seam | 설계 단계에서 비교한 세 후보 아키텍처. 각각 UE 관용구 충실, 측정 엄밀성, Chaos 경계 설계에 무게를 둔 안이다 |
| 심사(UE) · 심사(벤치) | 후보안을 평가한 두 심사. UE 아키텍트 관점(승자 ue-faithful)과 벤치마크 과학자 관점(승자 bench-rigor)이다 |
| 종합 | 후보안과 심사 결과를 하나로 합친 단계 |
| 리뷰(사실) · 리뷰(빌드) | 종합안에 대한 적대적 리뷰 두 건. 하나는 로컬 소스와 대조했고, 하나는 구현 가능성과 일정을 봤다 |
| F1~F41 | 두 리뷰에서 뽑은 수정 항목 번호. §5에 색인이 있다 |
| S1~S7 | 리드의 단계화·단순화 결정. §3.2에 있다 |
| 연구 노트 | 조사 단계 보고서 (methodology, critique, chaos-standalone, ue4-physx, build-bullet/jolt/physx, renderer-tech, chaos-debugdraw, ue-layering) |

- 위 산출물은 저장소에 넣지 않는다. 그래서 결정의 근거가 된 소스 위치는 file:line으로 직접 적었다.
- 리뷰가 "개연성 있음(PLAUSIBLE)"으로만 판정한 지적은 그 표기를 남긴다.

### 0.3 마일스톤 ID

[03_WorkPlan.md](03_WorkPlan.md)의 번호를 쓴다.

| M | 이름 |
|---|---|
| M0 | 스캐폴드·툴체인·SDK 연결·환경 준비 |
| M1 | PhysicsCore 데이터 모델 (FBodyInstance 등, BodyUtils, FStepPlanner, 프로필 최소) |
| M2 | C ABI v0.9 + BackendKit + Null 백엔드 + 호스트 배관 (FPhysScene→ABI) |
| M3 | Jolt 수직 슬라이스 + 최소 벤치 exe (S00, `M1.SingleBoxDrop`, `M3.BoxPile`, timings.csv/manifest 기본) |
| M4 | 뷰어 1단계 (Chaos Debug Draw 스타일, 단일 씬, 엔진 전환, 일시정지/스텝, Stats) |
| M5 | Bullet + PhysX(UE4Compat/SdkDefault/-TGS/-Stab) 백엔드 + BackendCommon 테스트 |
| 체크포인트 A | 3엔진을 뷰어로 확인, `M3.BoxPile` 1k/5k T=1 비공식 수치 (누적 약 31 pd) |
| M6 | 물리 게이트 G1~G7·G9·G12 + 게이트 씬 + ABI 핵심 v1.0 동결 + ChaosBackendGuide 초안 |
| M2.5 | Chaos hello-DLL 스파이크 (선택, 3 달력일 타임박스, M5~M6과 병행) |
| M7 | 벤치 러너 + 측정 프로토콜 (호스트 제어, recorder, manifest, effective 덤프, 성능 씬, T=1..6, DET1, test-avx2 프리셋) |
| M8 | 오케스트레이션 + 분석 기본 (ptbench: 매트릭스, 셔플, fresh process, 반복, CV 보고, load/validate/perf/report) |
| M9 | 거동 씬 + ball/hinge 조인트(3엔진) + 프로필 완성 + 거동 분석 |
| M10 | 측정 엄밀성 확장 (TSC, ThreadCensus/cpu_eff, CV 게이트+sentinel, 스탬프, comparability key·주장 규칙·bootstrap CI, test-`<engine>`, prof) |
| M11 | Phase-1 캠페인 + 비공개 리포트 |
| M12 | 뷰어 2단계 (SideBySide, GhostOverlay, Replay, Profiler) |
| M13 | Chaos 백엔드 (사용자 구현, 동결 ABI 위) + 하네스 지원 |
| M14 | OOP 전송 (조건부: SP2/SP3 실패 또는 요청 시) |
| M15 | 확장 백로그 |

- [01_Feasibility.md](01_Feasibility.md)의 마일스톤 표기(스파이크 M2.5, 3엔진 통합 씬 M3/M5)도 이 표에 맞춰 갱신했다.
- 씬 ID(`M3.BoxPile` 등)는 마일스톤 번호와 관계없다 (DL34).

---

## 1. 고정 결정 D1~D7

이 결정들은 설계의 전제이며 바꾸지 않는다. 바꿔야 할 사실이 나오면 개정으로 기록한다.

| ID | 결정 | 근거·비고 |
|---|---|---|
| D1 | **단위와 축.** API는 cm, kg, s, Z-up, double이다. Chaos와 PhysX-UE4Compat는 네이티브 cm로 돈다 (`PxTolerancesScale(100, 1000)`). Jolt, Bullet, PhysX-SdkDefault는 백엔드 안에서 m로 변환한다. 중력은 항상 명시적으로 -980 cm/s²를 준다. 캡슐 축은 UE Z를 PhysX는 X, Jolt는 Y로 재매핑한다. **개정 (DL47):** Bullet은 `btCapsuleShapeZ`를 쓰므로 재매핑하지 않는다 | Jolt에 cm를 넣으면 속도 clamp(500 units/s)에 걸리고 질량이 1e9 kg가 된다 [V]. 엔진 기본 중력은 모두 다르다 [V]. `btCapsuleShapeZ`: btCapsuleShape.h:128-131 [V]. 01 §2.3, §3.3 |
| D2 | **Phase 1 백엔드.** Bullet(단일 스레드 world), PhysX 5.11(주 행 UE4Compat, SdkDefault 병기), Jolt. Chaos는 나중에 UBT Program DLL로 붙인다. 헤드라인은 `FChaosScene` + `FChaosEngineInterface` 경로이고, `FPBDRigidsEvolutionGBF` 단독 경로는 진단용이다. in-process가 막히면 같은 ABI를 쓰는 별도 프로세스(OOP)로 간다 | PhysX 결과는 항상 "UE4 settings on PhysX 5.11"로 표기한다. 01 §2.1, §2.2, §3.4 |
| D3 | **빌드.** Ninja 단일 config, MSVC 14.44, SDK를 추가하기 전에 /MD를 정한다. C++20은 타깃별로 건다. 빌드 루트는 짧은 `out/<preset>`이다. SDK는 FetchContent로 커밋을 고정해 빌드 트리에 clone하고, URL은 캐시 변수로 둔다 (`file:///D:/PhysicsRefs/...` 허용, 참조 체크아웃에서 직접 빌드 금지). 래퍼는 `PT::Jolt` / `PT::Bullet` / `PT::PhysX`이고, SDK는 백엔드 lib에 PRIVATE로 링크한다. Jolt는 AVX2를 강제하고 profiler·debug renderer·FP exception을 끈다. `PT_WITH_CHAOS`는 기본 OFF다 | 01 §4 규칙 표. Bullet 가져오기 방식은 DL54 |
| D4 | **뷰어.** Win32 + D3D11 + Dear ImGui(docking) + ImPlot + stb. Chaos 팔레트, 테셀레이션, 솔버 debug-draw 기본값을 따른다. 카메라는 UE 에디터 규약, 배경은 검정이 아닌 색, 바닥은 static box다. bench exe는 D3D 장치를 만들지 않는다 | 01 §5. 뷰어 위치와 성능 기준은 DL37 |
| D5 | **씬과 프로필.** 씬은 하나(`FPhysScene`)다. 질량·관성·COM은 하네스가 계산해 모든 엔진에 명시적으로 넘긴다. 물리 프로필은 UEParity / Normalized(+ 백엔드 변형), 플래그 프로필은 Shipped / MatchedAVX2이고, `/fp:fast`를 쓴다. 모든 설정과 결과를 기록한다 | 01 §2.3 "설계 반영", §6 |
| D6 | **측정 규약.** 헤드라인은 단일 스레드이고, P코어 스케일링 곡선을 따로 낸다. run마다 새 프로세스, 엔진 순서 교차, 5회 이상 반복해 중앙값/p95를 낸다. 스냅샷은 타이밍 구간 밖에서 받는다. bench 모드와 viz 모드를 나눈다 | 01 §6 |
| D7 | **라이선스.** UE 소스와 UE 파생 코드는 저장소에 넣지 않는다 (클린룸). Chaos glue는 로컬 UE에서만 컴파일한다. UE 산출물은 `.gitignore`로 막는다 | 01 §8 |

---

## 2. 설계 결정 DL01~DL35

설계 단계의 결정 목록을 옮기고 리뷰 결과를 반영했다. 상태 열에 무엇이 바뀌었는지 적었다.

| ID | 상태 | 결정 | 이유 | 출처 |
|---|---|---|---|---|
| DL01 | 유지 | 구조의 골격은 ue-faithful 안이다: UE 타입명과 기본값, 단일 `FPhysScene`, UE 프레임 모델, UE4 방식 PhysX. 측정 프로토콜은 bench-rigor에서, 경계와 Kit는 chaos-seam에서 이식한다 | 두 심사의 승자는 달랐지만 이식 목록은 같은 조합으로 모였다. 사용자가 가장 중시하는 것이 UE 관용구이고, UE 적합성 점수는 ue-faithful이 가장 높았다 (두 심사 모두 9점) | 심사(UE) 승자 ue-faithful, 심사(벤치) 승자 bench-rigor의 이식 목록 |
| DL02 | 유지 · [MVP] | 모든 엔진을 pure-C 경계 하나(`PtBackendApi`)로 구동한다. SDK 백엔드는 static lib이고, 생성된 레지스트리를 거쳐 `PT_GetBackendApi_<Engine>`을 노출한다. Chaos는 DLL에서 `PT_GetBackendApi`를 export한다. C++ `IPhysicsBackendScene` / `FPhysicsInterface`는 경계 양쪽의 파사드로만 남는다 | ue-faithful의 이중 경계(SDK용 C++ 인터페이스 + Chaos 전용 C ABI)를 없앤다. 모든 엔진이 같은 호출 경로, 스레드 규칙, 타이밍 지점을 지나고, 사용자가 Chaos를 쓰기 전에 실제 백엔드 3종이 경계를 검증한다. SDK를 SHARED DLL로 빌드하는 chaos-seam 기본안은 D3에 어긋나 기각한다 | chaos-seam + bench-rigor, 두 심사의 이식 목록, D3 |
| DL03 | 정정 (F28) · [MVP] | 백엔드 lib는 `PT::Abi`, header-only `PT::BackendCommon`, 자기 SDK만 링크한다. 하네스 lib는 링크하지 않으며 CTest가 이를 검사한다. 하네스 쪽 TU가 제3자 헤더(doctest, ImGui, ImPlot, nlohmann/json)를 include할 때는 `check` / `verify` / `ensure`를 `#pragma push_macro` + `#undef`로 잠시 치우고 뒤에서 `pop_macro`하는 Start/End **include 파일 쌍**(`Misc/PtThirdPartyIncludesStart.h` / `Misc/PtThirdPartyIncludesEnd.h`, 하네스 측 `PT_THIRD_PARTY_INCLUDES_START/END`에 해당)을 거친다. 백엔드 측 `PT_THIRD_PARTY_INCLUDES_START/END`는 warning push/pop만 한다. Core를 먼저 include하고 doctest를 include하는 CTest 컴파일 프로브를 둔다 | UBT로 빌드되는 Chaos 모듈과 같은 제약을 재현한다. 백엔드 TU는 하네스 Core를 보지 않으므로 SDK 헤더와 매크로가 부딪치지 않는다. 그러나 하네스 TU에서는 부딪친다: doctest는 `bool check(const String&)` 멤버를 선언한다 (v2.5.3 doctest.h:1647, 리뷰 인용. Jolt에 포함된 doctest.h:1671에서도 확인 [V]). 매크로는 `#undef`를 만들어 낼 수 없으므로 매크로 쌍이 아니라 include 파일 쌍으로 구현한다 | chaos-seam, 심사(벤치)의 ue-faithful THIRD_PARTY_INCLUDES 오류 지적, 리뷰(빌드) F28 |
| DL04 | 정정 (F41, ODR은 DL46) · [MVP] | header-only BackendKit를 둔다: 스레드 가드(`PT_E_WRONG_THREAD`), phase 상태기계(Idle/SetUp/Started/Waited), 입력 검증, 2회 호출 용량 프로토콜, 모듈 클래스별 템플릿 상태. STL, RTTI, 예외, UE와 부딪치는 식별자를 쓰지 않는다. `PtAbiUeCompatCheck`가 UE와 같은 플래그(/GR- /EHs-c- `_HAS_EXCEPTIONS=0` /W4 /WX)로 컴파일한다. 기본 virtual 구현은 매개변수 이름을 쓰지 않는다 (`/*Infos*/`). `PtAbiUeCompatCheck`는 아무 일도 하지 않는 백엔드 하나를 실제로 인스턴스화한다 | Chaos 연결이 서브클래스 두 개와 매크로 하나로 끝나고, 다른 엔진과 같은 강제를 받는다. 이름 붙은 미사용 매개변수는 /W4에서 C4100이고 /WX에서 오류다 (MSVC 14.44로 재현 [V]). Kit의 템플릿 코드(`TBackendApiProvider<T>`, C 썽크)는 인스턴스화해야만 컴파일되므로, 인스턴스 없이는 UE 플래그 검사가 빈 검사가 된다 | chaos-seam, 두 심사, 리뷰(사실) F41 |
| DL05 | 정정 (F2, F23, S3) · [MVP] | 씬은 relocatable blob 하나가 아니라 UE 동사를 차례로 호출해 경계를 넘긴다: `CreateScene`(설정만) → `CreateMaterials` → `CreateActors`(배치, `AddActorsToScene` 포함) → `CreateConstraints`. 각 호출의 인자는 POD 버퍼다. `CreateActors` 안의 순서는 DL38(UE 순서)을 따른다. `desc_sha256`은 DL43이 정한 엔진 독립 필드만 해시한다. 호출 직렬화 codec(`PtAbiCodec`)과 loopback은 미룬다 (DL28) | 사용자가 아는 `FInitBodiesHelper` → `AddActorsToScene_AssumesLocked` 구조를 그대로 유지하고, 실행 중 바디 추가도 허용한다. 인자가 POD이므로 codec을 나중에 붙여도 loopback과 OOP가 가능하다 | ue-faithful 동사안 대 chaos-seam blob, 종합, 리뷰(사실) F2, 리뷰(빌드) F23, 리드 S3 |
| DL06 | 유지 · [MVP] | ABI의 box는 half extent, capsule은 로컬 Z축 기준 `Radius` + `HalfLength`다. 하네스 `FKBoxElem`은 UE처럼 full extent를 유지한다. 변환은 호스트 한 곳에서만 하고 단위 테스트로 고정한다 | SDK 3종과 Chaos `TBox`가 모두 half extent를 쓰므로, 변환을 네 곳이 아니라 한 곳에서 하는 편이 오류가 적다. FKBoxElem X/Y/Z는 full extent (BoxElem.h:35-55 [V]), FKSphylElem Length는 로컬 Z 선분 길이다 (SphylElem.h:36-50 [V]). 디버그 드로 쪽 변환은 DL26 | chaos-seam 대 ue-faithful, 종합 |
| DL07 | 유지 (해시 규칙은 DL22) · [MVP] | 헤드라인 지표는 Chaos를 포함한 모든 엔진에서 `T_frame = ΣT_push + ΣT_step + T_pull`이다. `T_pull`은 프레임당 1회 `ReadBodyStates(ACTIVE_ONLY)`다. `T_step`은 "Chaos와 직접 비교 불가" 표기를 단 보조 열이다. solver 단독 시간과 Evolution 백엔드는 별도 진단 수준이다 | Chaos `EndFrame`에는 PT→GT pull, SQ 구조 복사, 이벤트 flip이 들어 있다 (ChaosScene.cpp:500-576 [V]). SDK의 `T_step`에는 동기화가 없다. `T_frame`은 모든 엔진에 "스텝 + 상태를 읽을 수 있게 만들기"를 똑같이 매긴다 | bench-rigor t_frame_eq, ue-faithful·chaos-seam에 대한 두 심사의 오류 지적 |
| DL08 | 유지 (보강 F26, F3, S6) | 프레임 driver: `NATIVE_FRAME_MODEL` 백엔드(Chaos)는 원래의 UE `SetUpForFrame` 인자를 프레임당 1회 받고, `NumStepsTaken`이 하네스 계획과 같아야 한다. SDK 백엔드는 스텝마다 호출을 받고 pull은 프레임당 1회다. Chaos에 스텝별 호출을 강제하는 것은 진단용이다. **보강:** (1) Null 백엔드가 `FStepPlanner` 의미로 `PT_CAP_NATIVE_FRAME_MODEL`을 선언해, 이 계약을 ABI 동결(M6) 전에 게이트 A10으로 검사한다 (DL29). (2) AsyncFixed에서 Chaos 상태는 보간된다 (DL40). (3) Chaos로 하는 실제 검증은 M13이다 | 서브스텝마다 `FChaosScene::EndFrame`을 부르면 substep/async 모드에서 GT 작업이 과다 계상된다. FixedStep과 Sync에서는 두 driver의 호출이 같다: MaxDt≤0이면 clamp가 없고 (ChaosScene.cpp:341-345 [V]), 솔버 루프도 MaxDeltaTime>0을 검사한다 (PBDRigidsSolver.cpp:569 [V]) | ue-faithful EFrameDriver, bench-rigor에 대한 심사 오류 지적, 리뷰(빌드) F26, 리드 S6 |
| DL09 | 유지 (보강 S6) · [MVP] | `FStepPlanner`는 두 substep clamp 규칙(UE5ChaosScene, UE4PhysSubstep)을 UE와 같은 float 산술로 구현한다. MinDeltaTime보다 작은 나머지는 이월하지 않고 버린다. 네 모드(FixedStep, Sync, Substepping, AsyncFixed)를 모두 M1에서 구현하고 단위 테스트로 고정하지만, MVP 씬은 FixedStep과 Sync만 쓴다. 규칙 선택은 하네스 프로필이 하며 백엔드 설정에 두지 않는다 (DL46) | dt 0.05에서 UE5는 3 스텝, UE4는 2 스텝이다. "나머지를 이월한다"는 주장은 틀렸다 (PBDRigidsSolver.cpp:554-568 [V]). UE4 공식은 5.8.3에 dead code로 남아 있다 (PhysSubstepTasks.cpp:211-227 [V]). 모드 구현은 작아서 미루는 이득이 없다 | ue-faithful, 심사 정정, 리드 S6 |
| DL10 | 단계화 (S2) · [MVP] + [확장 M10] | **MVP 클럭은 QPC다.** `std::chrono::steady_clock`은 MSVC에서 QPC를 감싼다 (MSVC 14.44 `__msvc_chrono.hpp:649` [V]). 프레임별 QPC 값과 `qpc_hz`를 기록한다. 분해능(보통 100 ns)보다 짧은 구간(호출당 ABI 오버헤드, 아주 작은 씬)은 여러 번 반복한 배치 시간을 나눠 잰다. rdtscp+lfence 버킷 클럭, run 전체 QPC 보정, 읽기 오버헤드 기록은 [확장 M10]이다 | QPC 100 ns 눈금으로는 작은 씬의 스텝이나 호출당 오버헤드를 직접 분해할 수 없다는 원래 이유는 그대로다. 하지만 헤드라인 씬은 스텝이 ms 단위다 (Bullet ST 3008 박스 중앙값 18.1 ms, Jolt Pyramid 1스레드 약 12~22 ms [V, 참고치]). 그래서 TSC 보정을 MVP 경로에서 빼도 헤드라인이 흔들리지 않는다 | bench-rigor, chaos-seam 오버헤드 목표에 대한 심사 오류 지적, 리드 S2 |
| DL11 | 단계화 (S3) · [확장 M7] + [확장 M10] | bench 프로세스는 SDK 초기화나 LoadLibrary보다 먼저 affinity를 정하고, 이어서 HIGH priority, power throttling opt-out, `timeBeginPeriod(1)`을 건다. 전원 계획은 읽어 기록만 하고 바꾸지 않는다. 이 호스트 제어와 bench 위생 게이트 G10a(listener 0, 드로어 분리, 계측 tier 0)는 M7에서 만든다. `FThreadCensus`와 cpu_eff 게이트 G10b로 단일 스레드 실행을 증명하는 것은 [확장 M10]이다. M3 최소 벤치 exe의 수치는 비공식이다 | UE DLL은 타이머 해상도를 프로세스 전역으로 바꾸므로 (WindowsPlatformMisc.cpp:1076 [V]) 모든 엔진에 똑같이 걸어야 공정하다. 센서스는 Chaos의 유휴 TaskGraph 워커가 일하지 않았음을 증명하는 장치인데, Chaos는 M13에 오므로 M10에 넣어도 늦지 않다. 시스템 설정 변경은 사용자 몫이다 (Q9) | bench-rigor, methodology 연구 노트, 리드 S3 |
| DL12 | 유지 | `/fp:fast`를 모든 타깃에 쓴다 (D5). `/arch`는 전역으로 걸지 않는다. Shipped는 Jolt만 AVX2이고, MatchedAVX2는 SDK와 어댑터 타깃에만 타깃별로 `/arch:AVX2`를 건다. 하네스 코어에는 `/arch`를 걸지 않으므로 `desc_sha256`이 플래그 프로필 간에 같다. MatchedAVX2용 `test-avx2` 프리셋은 M7에서 추가한다 (DL13) | bench-rigor의 하네스 `/fp:precise`는 D5에 어긋난다. 하네스 ISA를 고정하면 D5를 벗어나지 않고도 같은 입력 안정성을 얻는다. `/fp:precise`로 바꿀지는 Q12로 남긴다 | D5, bench-rigor에 대한 심사 오류 지적, ue-faithful의 전역 `add_compile_options` 대체 |
| DL13 | 정정·단계화 (S4) · [MVP] + [확장 M7, M10] | 프리셋은 UE 구성을 따른다. **MVP:** `dbg`(DebugGame 대응), `dev`(Development), `test`(Test, 헤드라인). `test-avx2`는 M7, `prof`와 `test-<engine>`은 M10에서 추가한다. RelWithDebInfo는 거부한다. Chaos DLL 구성은 프리셋을 따른다 (dev → Development, test → Test) | "보기는 Development, 측정은 Test" 원칙은 모든 엔진에 같다. 프리셋 8개를 M0에서 다 만들면 빌드 매트릭스만 커진다. test-avx2, test-<engine>, prof를 실제로 쓰는 곳은 M7과 M10부터다 | ue-faithful + bench-rigor, 리뷰(빌드) 범위 축소안, 리드 S4 |
| DL14 | 정정 (F15, F40) | PhysX의 실제 제약은 "프로세스당 `PxPhysics` 1개, `PxTolerancesScale` 1개"다. 그래서 같은 스케일 (100, 1000)을 쓰는 `UE4Compat` / `UE4Compat-TGS` / `UE4Compat-Stab`은 한 프로세스 안에서 별도 `PxScene`으로 공존할 수 있고, 뷰어가 이들을 나란히 돌려도 된다 (SideBySide는 M12). 스케일이 (1, 10)인 `SdkDefault`만 별도 프로세스가 필요하다. 스케일은 `InitGamePhys` 옵션으로 정한다. 모듈의 `GLOBAL_SCENE_SETTINGS` 검사는 프로필 이름이 아니라 초기화 옵션(스케일)을 키로 한다. Bullet은 전역 combine 콜백 때문에 프로세스당 combine 규칙이 하나다 | PxPhysics.h:997 [V] 프로세스당 하나. PxSceneDesc.h:541 [V] 씬은 PxPhysics와 같은 스케일이어야 한다. 세 변형은 `solverType`과 `eENABLE_STABILIZATION` 플래그만 다르다. 원래 DL14의 "뷰어에 PhysX 프로필 둘을 나란히 둘 수 없다"는 필요 이상으로 엄격했고, 가장 유용한 PhysX 시각 비교(10단 탑이 PGS에서 25 cm 흔들리는 것과 안정화된 변형 [V])를 막았다. chaos-seam의 "스케일마다 PxPhysics 하나"는 여전히 불가능하다 | chaos-seam에 대한 심사 오류 지적, 리뷰(사실) F15, 리뷰(빌드) F40 |
| DL15 | 정정 (F2, F34) · [MVP] | PhysX UE4Compat는 ue-faithful 레시피를 따른다: InitGamePhys, `FPhysXCPUDispatcherSingleThread`, filter word 배치 [M], `FPhysXSimEventCallback`, contact offset clamp [M], D6(actor 순서 뒤집음 [M]). **정정 1:** `InitDynamicProperties`는 `addActors` 다음에 실행한다 (DL38). **정정 2:** 단일 스레드 dispatcher의 `submitTask`는 재귀하지 않는다. 스레드 로컬 LIFO 스택을 두고, 재진입 호출은 push만 하고 반환하며, 가장 바깥 호출이 스택을 비운다. 결과는 `PxDefaultCpuDispatcherCreate(0)`과 해시로 교차검증한다. UE4에서 온 모든 키는 증거 태그가 붙은 ini 키다. kinematic 쌍은 PhysX 5의 kineKine/staticKine FilteringMode `eKEEP`과 셰이더 억제로 처리한다 | "PhysX를 UE4처럼" 요구와 D7을 함께 만족한다. [M] 값은 사용자가 4.27로 확인한 뒤 ini만 바꾸면 된다 (Q10). PhysX 5는 KK/KS 씬 플래그를 filtering mode로 바꿨다. continuation task는 `release()` 안에서 다시 submit되므로 run 후 release를 그대로 부르면 체인 길이만큼 재귀한다 (PLAUSIBLE). `putToSleep`은 씬에 들어간 actor에만 유효하다 (NpRigidDynamic.cpp:527 [V]) | ue-faithful, ue4-physx 연구 노트, 리뷰(사실) F2, 리뷰(빌드) F34 |
| DL16 | 유지 · [MVP] | Bullet 각속도 clamp: UEParity에서는 62.83 rad/s를 내부 pre-tick 콜백으로 에뮬레이트한다. 하드코딩된 한계 π/(2·dt) = 94.2 rad/s(60 Hz)는 COMPILE_TIME으로 기록하고, 여기에 걸린 run에는 플래그를 단다 | 이 한계는 풀 수 없다 (btRigidBody.cpp:380-385 [V]). 에뮬레이트하면 "문서화된 이탈" 대신 UEParity를 그대로 맞출 수 있다 | ue-faithful + bench-rigor, 심사 이식 목록 |
| DL17 | 정정 (F7) · [MVP] | 질량 규칙은 별도 축(`EMassRule`)이며, 두 물리 프로필 모두 기본값은 UEBodyUtils다. 방법론 씬은 `bOverrideMass`로 질량을 고정한다. Physical(지수 1)도 고를 수 있고 기록된다. 계산 순서는 UE와 같다: (1) 밀도: 물리 머티리얼이 없으면 1.0 kg/cm³, 있으면 max(9e-8, Density×0.001) → (2) 체적 적분 → (3) 대각화 → (4) 질량 규칙: `M' = max(MassScale × M^clamp(RaiseMassToPower, 1e-4, 1), 0.001)`, `bOverrideMass`이면 `max(MassInKgOverride, 0.001)` → (5) 관성을 M'/M로 스케일 → (6) COM += COMNudge → (7) `InertiaTensorScale` 적용 (`bInertiaScaleIncludeMass=false`) → (8) `p.ComNudgeAffectsInertia`이면 COMNudge 평행축 항 추가. 단위가 아닌 InertiaTensorScale과 0이 아닌 COMNudge를 함께 쓰는 단위 테스트를 둔다 | bench-rigor의 "Normalized면 Physical 질량"은 D5의 Normalized 정의에 없고, UEParity 대 Normalized 비교에 질량 차이를 섞는다. 이전 목록은 InertiaTensorScale을 마지막에 두었고, 머티리얼이 없을 때의 밀도와 override 분기를 빠뜨렸다. UE 주석은 평행축 항이 ScaleInertia 뒤에 와야 한다고 적는다. 근거: BodyUtils.cpp:37-45(밀도), 53-72(질량 규칙), 74-79(관성 비율 스케일·COMNudge), 90-96(InertiaTensorScale), 98-112(COMNudge 관성 항), 124(대각화) [V], BodyInstance.cpp:3490-3492 [V], CVar 이름 BodyUtils.cpp:17 [V]. 01 §10 Q4의 초기 권장과의 관계는 Q4 참조 | bench-rigor에 대한 심사 오류 지적, D5, 리뷰(사실) F7 |
| DL18 | 정정 (S5, 섹션은 DL44) · [MVP] 최소 + [확장 M9] 완성 | 프로필은 UE 스타일 ini 계층이다: Base → Profile → Backend 변형 → 씬 고정값 → `-ini:` / `-ExecCmds`. `FProfileResolver`가 값마다 Origin을 추적하고, 변형이 바꿀 수 있는 키를 제한하며(자기 엔진 키와 `EngineDefault` 키만), `ForcedOverrides`와 Chaos로 그대로 넘길 `[ConsoleVariables]`를 처리한다. CLI는 `FCommandLine` / `FParse`이고 CLI11은 쓰지 않는다. UE 관용구(FCommandLine/FParse, CVar 레지스트리, ini 리더, UE_LOG, check/ensure)는 이름과 사용감만 UE와 같은 **최소 구현**이다. M1에서 최소 리졸버를, M9에서 변형 ini 전체를 완성한다 | ue-faithful의 UE 관용구와 "변형이 정규화를 몰래 되돌릴 수 없다"는 bench-rigor 규칙을 합친다. FConfigCacheIni 계층이나 콘솔 명령 체계를 그대로 복제하면 M0 공수에 들어가지 않는다 (리뷰(빌드): 관용구 전체에 0.75 pd) | ue-faithful + bench-rigor, 리뷰(빌드) 공수 지적, 리드 S5 |
| DL19 | 정정 (F32, F13) · [MVP] | effective 설정 readback은 필수이며 JSON lines로 남긴다. 키마다 Source는 READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT 중 하나이고, 질량 속성은 샘플링해 읽는다. READBACK 일치는 게이트 G9(M6)가 검사하고, M7부터는 preflight가 불일치가 하나라도 있으면 run을 거부한다. **필수 키는 "공통 집합 + 백엔드가 `GetInfo`로 선언하는 백엔드별 집합"이고 A7이 둘 다 검사한다.** 박스 마진은 상수가 아니라 셰이프별로 readback한다: Bullet은 `setSafeMargin`이 min(0.04 m, 0.1×최소 half extent)를 고르므로 `getMargin()`으로 읽는다 (50 cm 큐브 → 0.025 m). Chaos는 min(2×minHalf×CollisionMarginFraction, CollisionMarginMax), 기본 5% / 10 cm다 | "설정함"은 "적용됨"이 아니다. hello 테스트에서 Jolt 기본 damping 0.05가 상자를 8.5 cm 움직였다 [V]. 필수 키를 전역 목록 하나로 두면 `physx.*` 키 때문에 Null과 PhysX가 아닌 백엔드가 A7을 통과할 수 없다. Bullet 근거: btBoxShape.cpp:25, btConvexInternalShape.h:66-81 [V]. Chaos 근거: ChaosInterfaceUtils.cpp:156 [V], 기본값 0.05 / 10 cm는 PBDRigidsEvolutionGBF.h:65-66 [V]. 이 코드는 Engine 모듈에 있으므로 Engine을 링크하지 않는 Chaos 브리지가 재구현해야 한다 | bench-rigor, critique 연구 노트, 리뷰(빌드) F32, 리뷰(사실) F13 |
| DL20 | 정정·단계화 (F24, S3) · [확장 M10] | 적합성 스탬프는 M10에서 만든다. 키는 {bench exe SHA, 백엔드 모듈 SHA(DLL 백엔드는 DLL 파일 해시), `variant_sha256`, `physics_sha256`, `tolerances_sha256`, 게이트 스위트 버전}이다. 스탬프는 bench exe 자신(`-Conformance`)이 만든다. 러너는 이 값들을 다시 계산해 비교하고, 맞지 않는 셀은 프로세스를 띄우기 전에 거부한다. 선택적으로 `/Brepro`로 링크해, 바뀌지 않은 코드를 다시 링크해도 exe SHA가 그대로이게 한다. M10 전의 결과(체크포인트 A 포함)는 "스탬프 없음, 비공식"으로 표시한다 | 원래 키(exe SHA × backend × 변형 이름 × 프로필 이름)는 사용자가 반복하는 Chaos DLL 재빌드와 4.27 확인 뒤의 ini 내용 변경을 놓친다. 반대로 하네스를 조금만 고쳐도 모든 스탬프를 무효로 만든다. 첫 신뢰 가능 헤드라인(M11)이 M10 뒤에 있으므로 스탬프도 M10이면 충분하다 | bench-rigor, 두 심사, 리뷰(빌드) F24, 리드 S3 |
| DL21 | 단계화 (S3) · [확장 M8] + [확장 M10] | 분석은 comparability key, 표 안 `desc_sha256` 동일성, 주장 규칙을 강제한다. 주장 규칙: block-bootstrap 비율 CI가 1을 포함하지 않고, \|ρ−1\| ≥ 3%이며, Shipped와 MatchedAVX2에서 방향이 같아야 한다. 아니면 "ISA-dependent"로 표기한다. 단계: 표 안 `desc_sha256` 동일성 검사와 기본 CV 보고는 M8, CV 게이트와 sentinel, comparability key·주장 규칙·bootstrap CI는 M10이다 | 서로 맞지 않는 run이 한 표에 섞이는 것을 막는다. Jolt SSE2 대 AVX2만으로 약 1.8배 차이가 난다 [V, 참고치] | bench-rigor, 리드 S3 |
| DL22 | 정정 (F18, F36) | run 모드(bench / behavior / viz)와 계측 tier(counters, phases, alloc, contacts, native_debug_draw)를 추적한다. tier가 하나라도 켜지면 `timing_valid_for_headline=false`다. **헤드라인 run은 해시를 마지막 프레임에서만 계산하고 `CountersEvery=0`이다.** listener가 필요 없는 `GetStepStats` 읽기는 허용한다. capability 정정: (1) Bullet `NATIVE_DEBUG_DRAW`는 viz 전용이다. 드로어는 viz run에서만 붙이고, `DBG_NoDeactivation`은 프로필의 sleep 설정과 맞춘다. (2) Jolt `NATIVE_ISLANDS`는 내부 API(`GetIslandIndexInternal`)에 기댄 근사이므로, 섬 표시는 하네스 `FIslandBuilder`에 맡긴다 | listener, report 플래그, 드로어, 할당자 훅은 타이밍을 바꾸고, Bullet에서는 거동까지 바꾼다: 드로어가 붙으면 `stepSimulation`이 `gDisableDeactivation`을 덮어쓴다 (btDiscreteDynamicsWorld.cpp:418-421 [V]). 10k 바디에서 N 프레임마다 해시나 카운터를 돌리면 1.1 MB 넘게 건드려 다음 타이밍 프레임의 캐시를 더럽힌다. Jolt의 섬 index는 내부용이고 스텝마다 바뀐다 (MotionProperties.h:258 [V]) | bench-rigor, 리뷰(사실) F18, 리뷰(빌드) F36 |
| DL23 | 정정 (F30, F33) · [MVP M6] | 해석 기준값은 g=980으로 계산한다: S00 z60 = 501.8333 cm (dt_engine 기준 501.83328 cm), 마찰 정지 거리 250.947 cm, 반발 게이트 낙하 높이 201.7167 cm(k = 38, 충돌 속도 620.67 cm/s), 진자 주기 2.00905 / 2.04305 / 2.37023 s (5° / 30° / 90°, L=100, r=5). **모든 해석 기준값은 `dt_engine = (double)(float)dt`로 계산하고, ±1e-9 허용오차는 이 기준에만 적용한다.** 반발 게이트는 이산 궤적이 스텝 경계에서 정확히 바닥에 닿도록 z0를 고르거나, 접촉 프레임 전후의 법선 속도로 e를 잰다. 엔진별 허용오차는 사유와 함께 `tolerances.json`에 미리 등록한다. **M0 [V]:** `/fp:fast`에서는 `(double)(float)dt`가 상수 접기로 반올림 없이 계산될 수 있다. 기준값은 런타임 float 값으로 만든다 (§4.1 아래 M0 구현 메모) | ue-faithful과 chaos-seam의 g=9.81 진자 값(2.00803 등)을 고친다. 수치는 설계 단계에서 다시 계산해 확인했다. ABI는 dt를 float로 넘기므로 float(1/60) = 0.0166666675이고, 정확한 dt 기준으로는 z60이 약 5.2e-5 cm 어긋난다. 그러면 완벽한 double 적분기도 ±1e-9를 통과하지 못한다. speculative contact가 없는 엔진은 60 Hz에서 첫 접촉이 늦고 깊게 생겨 깊이 회복이 반발에 더해지므로 ±0.02를 넘길 수 있다 (PLAUSIBLE) | 두 심사의 오류 지적, 종합 계산, 리뷰(빌드) F30·F33 |
| DL24 | 유지 (보강: 종료 DL52, CVar DL39) | Chaos 초기화 순서: `FPlatformMisc::SetOverrideProjectDir` → `UE::Geometry::ExactPredicates::GlobalInit` → `GEngineLoop.PreInit(-ABSLOG -unattended -corelimit=T)` → `ProcessNewlyLoadedUObjects` → `StartProcessingNewlyLoadedObjects` → `InitGamePhysCore` → CVar → 호스트 로그로 보내는 `FOutputDevice`. `RequestExit`, `SetStandaloneSolver(true)`, `FreeLibrary`는 절대 부르지 않는다 | ue-faithful은 HeadlessChaosApp.cpp:547 [V]이 PreInit 전에 부르는 `ExactPredicates::GlobalInit`을 빠뜨렸다. 금지 호출은 검증된 위험이다: `RequestExit`는 PostQuitMessage로 호스트 메시지 루프를 끝내고 (WindowsPlatformMisc.cpp:1517-1520 [V]), `SetStandaloneSolver(true)`는 솔버 태스크를 띄우지 않아 시뮬레이션을 멈춘다 (PhysicsSolverBase.cpp:649-653 [V]) | chaos-seam, ue-faithful에 대한 심사 오류 지적, chaos-standalone 연구 노트 |
| DL25 | 유지 · [확장 M13] | 같은 DLL에 `Chaos.Evolution` 진단 씬(raw `FPBDRigidsEvolutionGBF`)을 둔다. 선택적으로 Shipping+logging 민감도 빌드를 둔다 | D2가 Evolution 진단을 요구하는데 ue-faithful에는 없었다. Test 구성도 EndFrame의 `!SHIPPING` AABB 통계 블록을 컴파일한다 (ChaosScene.cpp:514-545 [V]) | 심사 이식 목록, D2 |
| DL26 | 정정 (F20, F11) · [MVP] | `IDebugDrawRenderer`는 `IChaosDDRenderer`를 그대로 미러링한다: `IsServer`, `GetDrawRegion`, Render* 9개(Point, Line, Arrow, Circle, Sphere, Capsule, Box, Triangle, String), 축 행렬을 받는 `RenderCircle`, `bDrawShadow`가 있는 `RenderString`. UE 브리지는 `RenderLatentCommand`를 `PtDebugDrawCmd`로 직렬화해 구현한다. **인자 순서는 이미 검증했다** (ChaosDDRenderer.h:19-44 [V]). "나중에 원본과 대조" 작업은 없앤다. 의미 규약: `RenderCapsule`의 HalfHeight는 반지름을 포함한다 (Chaos는 0.5×Height + Radius를 넘긴다, DebugDrawImplicitObject.cpp:62 [V]). `RenderBox`의 Size는 half extent로 넘어온다 (DebugDrawImplicitObject.cpp:73-76 [V]). ABI `HalfLength` → HalfHeight(= HalfLength + R) 변환을 단위 테스트한다. **드로 순서**는 `FPBDRigidsSolver::PostTickDebugDraw`와 같다: Shapes → Collisions → Mass → Bounds → Transforms → ConstraintGraph(섬) → SleepState → CollidingShapes → Joints (PBDRigidsSolver.cpp:3417-3546 [V]). 섬 드로어 이름은 `DrawConstraintGraph`다 | ue-faithful의 FQuat 축과 bench-rigor의 RenderConvex 대체를 고친다. 이전 문서는 Joints를 5번째, Mass를 8번째에 두었고 섬 드로어를 `DrawIslands`라 불렀다 | ue-faithful·bench-rigor에 대한 심사 오류 지적, 리뷰(사실) F11·F20, 리뷰(빌드) F26 |
| DL27 | 정정 (F4) · [MVP] | 충돌 필터링은 32 채널, uint32 마스크, UE 상호 차단 검사를 쓴다. 상수는 `PT_NUM_COLLISION_CHANNELS = 32`(C ABI 매크로. C++ 쪽 상수 이름은 매크로와 겹치지 않게 `NumCollisionChannels`, M1 수정 라운드)이고 **의도적인 Phase-1 단순화**다. Chaos 브리지는 `Chaos::Filter::FShapeFilterBuilder`로 `FShapeFilterData`를 만들 때 BlockMask를 uint64로 넓힌다 (레거시 4-word `FCollisionFilterData`는 이 uint64 마스크를 두 word로 나눠 담을 뿐이다, CollisionFilterData.cpp:308-315 [V]) | UE 5.8.3에는 기본 8 + 엔진 trace 6 + 게임 trace 50 = 64 채널이 있고, `ECC_OverlapAll_Deprecated = 64`, `ECC_MAX = 65`이며 마스크는 uint64다 (EngineTypes.h:1101-1176 [V], CollisionFilterData.cpp:263-272, 310-313 [V]). 32 채널 제한은 UE4 PhysX word3의 5비트 채널 필드(PhysicsFiltering.h `NumCollisionChannelBits`)와 Bullet의 int group/mask에서만 생긴다. 이전 DL27의 "ECC_MAX는 33"은 UE4 시절 값이며 [R] 5.8.3에서는 틀렸다. 모든 문서와 헤더 주석에서 지운다 | ue-faithful에 대한 심사 지적(그 지적 자체가 UE4 기준이었음), 리뷰(사실) F4 |
| DL28 | 단계화 (S3) · [MVP] + [확장 M14 또는 M15] | Null 백엔드(static + 시험용 DLL)를 둔다. MVP ABI 게이트는 A0~A7, A9, A10이다: lifecycle, 버전 핸드셰이크, 스레드, phase, 입력 검증, 용량 프로토콜, FP 환경(A6), 필수 effective 키(A7), DLL 위생(A9)은 M2, 프레임 모델(A10, DL29)은 M6이다. A4의 1만 개 fuzz는 표 기반 음성 사례 약 30개로 바꾸고, 무작위 fuzz는 백로그(M15)로 둔다. 전체 스위트 loopback(A8)과 `PtAbiCodec`은 M14(OOP와 함께) 또는 백로그 M15로 미룬다 | Chaos가 생기기 전에 DLL 경로, export, 핸드셰이크를 증명하고 사용자에게 최소 템플릿을 준다. codec은 진입점 25개, 중첩 strided 버퍼, 2회 호출 프로토콜을 비트 동일하게 직렬화해야 하는데, 실제 소비자는 조건부 OOP 전송 하나뿐이다. 원래 배정(0.5 pd)으로는 맞지 않는다 | chaos-seam, ue-faithful loopback 확장안, 리뷰(빌드) 범위 축소안, 리드 S3 |
| DL29 | 정정 (F26, F38) · [MVP] | **ABI 핵심 v1.0은 게이트 마일스톤 M6 끝에서 동결한다** (`abi-v1.0` 태그). 조건은 SDK 백엔드 3종과 Null이 그 경계를 통해 MVP A 게이트(A0~A7, A9, A10)와 M6 G 게이트(G1~G7, G9, G12)를 통과하는 것이다. 동결 전에 Null 백엔드가 `FStepPlanner` 의미로 `PT_CAP_NATIVE_FRAME_MODEL`을 선언하고, Null을 상대로 Native driver / `NumStepsTaken` 계약, Substepping, AsyncFixed 플래너 게이트를 돌린다 (A10). **조인트, 디버그 스냅샷, 네이티브 드로 진입점은 "M9까지 잠정(provisional)"으로 표시하고 M9에서 확정한다.** M2.5 스파이크 결과가 있으면 함께 반영한다. 동결 뒤 v1.x는 append-only다. 호스트는 자기가 빌드된 major(`PT_ABI_VERSION_MAJOR`)를 요청한다. 동결 전 스파이크 DLL은 major 0(v0.9)을 쓴다 | 실제 백엔드를 거친 뒤 동결하는 것이 가장 견고하고 (심사(벤치)), 조기 스파이크도 동결에 반영할 수 있다 (심사(UE)). 하지만 원래 안은 조인트, native frame model, 다중 스텝 tick mode, native draw를 아무 백엔드도 쓰기 전에 얼렸다. 핵심만 얼리고 아직 쓰이지 않은 진입점은 잠정으로 두면, 사용자가 M9 전에 Chaos(M13)를 시작해도 헤드라인 경로는 핵심 v1.0만으로 구현할 수 있다. `PT_GetBackendApi(1)`을 하드코딩하면 v0.9 스파이크 DLL이 거부된다 | bench-rigor + chaos-seam, 리뷰(빌드) F26·F38 |
| DL30 | 유지 (보강 F6) · M2.5 | 스파이크 게이트 SP1~SP5마다 대안을 문서로 둔다: `.def` export, 저장소 밖 프로젝트, OOP EXE, Shipping+logging. OOP 전송(M14)은 SP2나 SP3가 실패할 때만 만든다. SP1 체크리스트에 "RequiredProgramMainCPPInclude.h가 컴파일된다"를 넣는다 (DL51) | IPC에 4 pd를 쓰지 않으면서도 대안을 믿을 만하게 유지한다 | chaos-seam, 리뷰(사실) F6 |
| DL31 | 유지 (보강 S3) | Phase 1 조인트는 ball과 hinge다 (M9). Convex는 데이터 모델과 ABI에서만 지원하고 헤드라인 씬에서는 쓰지 않는다. 하네스 quickhull과 convex 질량 적분은 백로그(M15)다 | 조인트 파라미터는 엔진 간에 이식되지 않고, convex와 margin의 동등성은 아직 검토하지 않았다 (critique). convex를 쓰는 씬이 없으므로 quickhull과 사면체 적분을 M1에 넣을 이유가 없다 | ue-faithful + critique 연구 노트, 리뷰(빌드) 범위 축소안, 리드 S3 |
| DL32 | 유지 · [MVP] | SDK 백엔드는 중력이 바뀔 때만 설정한다. Chaos는 UE처럼 매 프레임 받는다 | Bullet `setGravity`는 바디 전체를 도는 O(N)이라 Bullet에만 스텝당 비용을 더한다 | chaos-seam + bench-rigor |
| DL33 | 유지 · 리플레이 [확장 M12] | bench와 viewer는 별도 exe다. bench는 렌더러를 링크하지 않는다. viewer는 Development Chaos DLL을 호스트하고, Test 구성 run은 `states.ptst` / `contacts.ptcn` 리플레이로 본다 | 벤치 위생과 "UE DLL은 프로세스당 하나" 규칙을 지키면서도 측정 run을 눈으로 확인할 수 있다 | ue-faithful + bench-rigor |
| DL34 | 정정 (F17, F2) | 씬 ID는 방법론 카탈로그 ID를 쓰고 항상 `ID.Name`으로 적는다. 게이트 접두어는 A(ABI), G(물리·측정), SP(스파이크), DET(결정성)이다. 카탈로그 이름대로 `M4.InclineFriction`이라 쓴다 (`M4.Incline` 아님). `S00.FreeFall`은 카탈로그 §5.1의 S00 보정 씬(FreeFall)을 cm·g=980으로 다시 정의한 것이므로, 하네스 정의 씬으로서 확장 목록과 함께 등록한다. `S00d`, `S01`~`S08`(`S08.StartAsleep`은 DL38의 `bStartAwake=false` 게이트 씬), `P0.EmptyStep`, `P1.SmallScene`, `M7.UEDefaults`는 카탈로그 확장이다. 모두 `docs/Scenes.md`에 등록한다 | 씬 M3, 마일스톤 M3, 결정 D1, 결정성 테스트가 서로 부딪치지 않게 한다. 방법론 카탈로그에는 M1~M6, S00(SI 단위), E1~E13만 있다 | 종합, 리뷰(사실) F17 |
| DL35 | 유지 (사용자 확인 Q11) | UEParity의 반발 임계: Chaos는 네이티브 1000·dt, PhysX-UE4Compat는 200 cm/s, Jolt와 Bullet은 Chaos 등가 1000·dt cm/s. Normalized는 모든 엔진 50 cm/s다 | UEParity는 UE 값이 없는 엔진에 UE(UE5에서는 Chaos) 기본값을 대입한다. 사용자 확인을 위해 열어 둔다 | bench-rigor (ue-faithful은 엔진 네이티브 값) |

---

## 3. 신규 결정 DL36~DL54

### 3.1 결정 표

| ID | 상태 | 결정 | 이유 | 출처 |
|---|---|---|---|---|
| DL36 | 신규 (S1~S7) | **단계화 정책.** 모든 주요 기능에 [MVP] (M0~M6) 또는 [확장 Mx]를 붙인다. MVP는 "3엔진을 같은 경계로 돌리고, 게이트로 검증하고, ABI 핵심을 동결한 상태"까지다. 측정 엄밀성 장치(TSC, 센서스, 스탬프, 주장 규칙)는 첫 신뢰 가능 헤드라인(M11) 직전인 M10에 모은다. 공수는 하네스 M0~M12 약 62 pd + M13 하네스 지원 2 pd로 약 64 pd이고, 버퍼 20%를 더하면 약 77 pd(1인 15~16주)다. 체크포인트 A는 누적 약 31 pd, 첫 신뢰 가능 3엔진 헤드라인(M11 성능 부분)까지의 직렬 경로 M0→M1→M2→M3→M4→M5→M6→M7→M8→M10→M11은 약 52 pd, 거동 리포트(M9 포함)까지는 약 58 pd다. 세부는 §3.2와 [03_WorkPlan.md](03_WorkPlan.md) | 리뷰(빌드)는 단일 엔지니어에게 범위가 너무 크고 (현실적으로 75~90 pd), 첫 엔진 비교 수치가 약 2개월 뒤에야 나온다고 지적했다. 동결과 게이트까지를 MVP로 묶으면 "믿을 수 있는 비교의 토대"와 "측정 엄밀성 확장"이 분리되고, 중간에 체크포인트 A가 생긴다 | 리드 S1~S7, 리뷰(빌드) 범위·공수 지적 |
| DL37 | 신규 (S7, F29) | **뷰어를 첫 엔진 슬라이스 바로 뒤에 둔다.** M3(Jolt 수직 슬라이스) 다음 M4에서 뷰어 1단계를 만든다: Chaos Debug Draw 스타일, 단일 씬, 엔진 전환, 일시정지/한 스텝, Stats. Bullet과 PhysX(M5)는 뷰어 위에서 디버깅한다. **성능 기준:** 시뮬레이션을 일시정지했거나 리플레이 중일 때 ≥60 fps, 씬당 스냅샷+드로 CPU ≤4 ms. 라이브 시뮬레이션 fps는 보고만 하고 게이트로 쓰지 않는다. SideBySide, GhostOverlay, Replay, Profiler, WARP 골든 PNG는 뷰어 2단계(M12)다 | 사용자는 Chaos를 쓰기 전에 Bullet, PhysX, Jolt가 도는 것을 보고 싶어 한다. 단위·규약 버그는 숫자보다 그림에서 먼저 드러난다. 01 §1의 권장 진행 방식(백엔드와 뷰어 먼저)과 같다. 물리는 메시지 루프 스레드에서 돌고 Bullet ST는 3008 박스에 스텝당 18.1 ms [V]이므로, "10k 박스 3엔진 라이브 60 fps"는 달성할 수 없는 목표였다 | 리드 S7, 리뷰(빌드) F29와 수직 슬라이스 제안, 01 §1 |
| DL38 | 신규 (F2) · [MVP] | **바디 생성 순서는 UE 순서다.** 모든 백엔드와 Chaos 가이드가 같은 순서를 쓴다: (1) CreateActor(생성 시점 플래그: kinematic, gravity, CCD, 최대 선속도, inertia conditioning, gyroscopic, 반복 횟수) → (2) 지오메트리·필터·머티리얼 → (3) `AddActorsToScene_AssumesLocked` 1회 (PhysX는 `addActors`) → (4) 바디마다 UE 순서의 `InitDynamicProperties` (질량·관성·COM → damping → 최대 각속도 → depenetration → 초기 속도 → sleep/stabilization 임계 → 반복 횟수) → (5) `!bStartAwake`이면 wake counter 0 + `putToSleep`. ABI `CreateActors` 행, PhysX 레시피, Chaos 매핑을 이 순서로 맞춘다. **게이트:** `bStartAwake=false` dynamic 바디가 frame 0 `ReadBodyStates`에서 `Sleeping`인지 검사하는 게이트 씬 `S08.StartAsleep`을 M6에 추가하고 G6(sleep 계약)가 검사한다 (DL34). Normalized는 sleep을 끄므로 이 검사는 UEParity에서만 돈다 | UE 5.8.3은 `AddActorsToScene_AssumesLocked`(BodyInstance.cpp:1669) 다음에 `InitDynamicProperties_AssumesLocked`를 돈다 (:1695-1706) [V]. (4)의 순서는 그 함수 본문 그대로다 (:4648-4743 [V]). putToSleep은 `IsInScene`이고 kinematic이 아닐 때만 부른다 (:4723-4733) [V]. PhysX는 씬에 없는 actor의 putToSleep을 거부하고 (NpRigidDynamic.cpp:527 [V]), `eDISABLE_SLEEPING`이면 무시한다 (:523 [V]). 이전 설계 순서로는 `bStartAwake=false`가 Chaos에서는 조용히 사라지고 PhysX에서는 실패한다. `bStartAwake` 기본값 true는 FBodyInstanceCore에 있다 (BodyInstanceCore.cpp:13-24 [V]) | 리뷰(사실) F2, F22 |
| DL39 | 신규 (F1) · [확장 M13] | **Chaos 스레드 매핑.** T=1 = `SetThreadingMode_External(SingleThread)` + `p.Chaos.SingleWorkerPhysics=1` (선택: `p.Chaos.DisablePhysicsParallelFor=1`) + `-corelimit=1`. T≥2 = TaskGraph + `-corelimit=T` + `p.Chaos.MaxNumWorkers=T`. MaxNumWorkers는 상한일 뿐 스레드 풀 크기를 정하지 않는다. effective 키 `threads.single_worker`, `threads.max_num_workers`로 기록하고 Chaos의 백엔드별 필수 키로 선언한다 (DL19). G10b(M10)가 실제 증거다. ChaosBackendGuide 초안(M6)에 적는다 | SingleThread 모드는 솔버 tick을 호출 스레드에서 돌릴 뿐이다 (ChaosScene.cpp:73-77 [V]). 내부 ParallelFor는 `bSingleWorkerPhysics \|\| bDisablePhysicsParallelFor \|\| bForceSingleThreaded`일 때만 단일 스레드로 떨어진다 (Parallel.cpp:95 [V]). 제약 솔버의 워커 수도 `!bSingleWorkerPhysics`로 정해진다 (ConstraintGroupSolver.cpp:250 [V]). `p.Chaos.MaxNumWorkers`는 존재하며 (Parallel.cpp:33 [V], 기본 100) 워커 사용 상한과 MinBatchSize만 바꾼다 (Parallel.cpp:97, 123 [V]). PreInit은 TaskGraph 워커를 논리 코어 − 2개 만든다 [V, chaos-standalone]. affinity 0x1에서 이 워커들이 한 코어를 나눠 쓰면 시간이 부풀거나 G10b가 실패한다 | 리뷰(사실) F1, 리뷰(빌드) F1 |
| DL40 | 신규 (F3) · [확장 M13] | **Chaos AsyncFixed는 보간 상태다.** Chaos의 AsyncFixed는 "GT 상태가 보간되고 한 스텝 늦다(UE 동작)"로 표기하고, 바디별 거동 비교에서 뺀다. 스텝 수(`NumStepsTaken`)와 성능은 비교한다. step-aligned 상태가 필요하면 PT 측 sim callback에서 읽는 진단 경로를 둘 수 있다 (Q16). ABI 매핑: sync는 `AsyncDt=-1`을 넘기고 0은 절대 넘기지 않는다. Kit는 `AsyncFixedTimeStepSize=0`을 `PT_E_INVALID_ARG`로 거부하고 A4 음성 사례에 넣는다. `p.UseAsyncInterpolation=0`은 fixed-dt accumulator까지 끈다는 점을 ChaosBackendGuide에 적는다. 하네스 `FStepPlanner`의 AsyncFixed 행 "보간 없음"은 SDK 백엔드에만 해당한다 | `IsUsingAsyncResults()`는 `AsyncDt >= 0`이고 `IsUsingFixedDt()`는 `IsUsingAsyncResults() && UseAsyncInterpolation`이다 (PhysicsSolverBase.h:712-720 [V]). `p.UseAsyncInterpolation` 기본값은 1이고 (PhysicsSolverBase.cpp:262-263 [V]), accumulator는 `IsUsingFixedDt()`일 때만 돈다 (PhysicsSolverBase.cpp:591-605 [V]). 그러므로 "보간 없는 AsyncFixed"는 Chaos가 도달할 수 없는 상태다. 0을 넘기면 async가 켜진다 | 리뷰(사실) F3 |
| DL41 | 신규 (F10, 식 정정) · [MVP] | **정지마찰은 Chaos 식으로 결합한다.** ABI는 머티리얼의 원래 `Friction`과 `StaticFriction`을 그대로 넘긴다 (호스트가 미리 합치지 않는다). Chaos 5.8.3의 쌍 정지마찰은 `max(combine(max(F0,SF0), max(F1,SF1)), combine(F0,F1))`이다. 즉 머티리얼마다 max(F, SF)를 먼저 하고, friction combine 모드로 합친 뒤, 쌍 수준에서 동적 결합값과 다시 max를 한다. 단조인 combine 모드(Average, Min, Multiply, Max)에서는 바깥 max가 효과가 없다. Jolt와 Bullet은 마찰 계수가 하나뿐이므로 combine 콜백이 동적 결합값 `combine(F0, F1)`을 돌려준다. SF ≤ F이면 이 값이 Chaos의 쌍 정지마찰과 같다. PhysX SdkDefault는 `createMaterial(max(F, SF), F, R)`로 같은 규칙을 낸다 (인자 순서는 staticFriction, dynamicFriction, restitution, PxPhysics.h:764 [V]. PhysX가 정지마찰도 같은 combine 모드로 결합한다). PhysX UE4Compat는 정지 = 동적 [M]을 유지한다. Normalized는 Average 강제 + 정지 = 동적(SF := F)이다. 세부는 [02_Architecture.md](02_Architecture.md) §5.1, §8 | 근거: PBDCollisionConstraints.cpp:469-476 [V] (머티리얼별 max는 :474-475), PBDCollisionConstraint.h:486 [V]. 리뷰(사실)가 낸 식 `max(combine(SF0,SF1), combine(F0,F1))`은 :474-475의 머티리얼별 max를 빠뜨렸다. 예: 머티리얼 A가 F=0.5, SF=0이고 B가 F=0.5, SF=1.0, Average이면 리뷰 식은 0.5, 실제 Chaos는 0.75다. 원래 설계의 "머티리얼별 max(F, SF)"는 맞았다. 원래 값을 넘기는 이유는 Chaos 브리지가 UE 머티리얼 값을 그대로 `FChaosPhysicsMaterial`에 넣게 하기 위해서다. **한계:** Jolt와 Bullet은 계수가 하나라 정지와 동적을 나눌 수 없다. 동적 결합값을 고른 이유는 미끄러짐 거동이 Chaos와 같고, UE4Compat와 Normalized의 "정지 = 동적" 규약과도 맞기 때문이다. 대신 SF > F인 머티리얼이 낀 쌍에서는 Jolt·Bullet의 정지 마찰이 Chaos보다 작아, Chaos에서 멈춰 있을 바디가 미끄러질 수 있다. 이런 run은 `material.static_friction_rule=dynamic_only`로 기록하고 by-design으로 태그한다. 방법론 씬은 SF ≤ F로 둔다 (UE 기본 SF = 0, PhysicalMaterial.cpp:42-60 [V]) | 리뷰(사실) F10, 로컬 소스 재확인 |
| DL42 | 신규 (F25) · [MVP] | **COM 래핑은 필요할 때만 한다.** 요소가 하나이고 ComLocal이 identity에 가까우면(\|t\| < 1e-6 cm, \|q·I\| > 1 − 1e-9) 네이티브 프리미티브(`btBoxShape`, Jolt `BoxShape` 등)를 그대로 쓴다. Bullet `btCompoundShape`와 Jolt `OffsetCenterOfMassShape`는 그 밖의 경우에만 쓴다. effective 설정에 `shape.wrapping` 개수를 기록하고, `M3.BoxPile`이 래퍼를 0개 만드는지 G9에서 검사한다. PhysX는 `setCMassLocalPose`로 COM 자세를 직접 받는다 (PxRigidBody.h:222 [V]) | 이전 설계는 COM이 원점이어도 모든 바디를 compound나 decorator로 감쌌다. 헤드라인 씬(`M3.BoxPile`, `M2b.Pyramid1240`)은 COM이 중심인 단일 큐브다. Bullet에서 compound는 쌍마다 `btCompoundCollisionAlgorithm` 디스패치, 자식 변환 합성, compound별 AABB 트리를 더하므로, 단일 스레드 전용인 Bullet만 불리해진다 | 리뷰(빌드) F25 |
| DL43 | 신규 (F23) · [MVP] | **`desc_sha256`은 엔진 독립 필드만 해시한다:** 머티리얼, 셰이프, actor create info, convex 꼭짓점, 조인트, 중력, dt 시퀀스. `RunMode`, `InstrumentFlags`, `NumThreads`, `RestitutionThreshold`, `Options`는 `options_sha256`(엔진마다 달라도 됨)이나 `physics_sha256`으로 옮긴다. 단위 테스트: 같은 시나리오를 백엔드 둘, 변형 둘로 만들면 `desc_sha256`이 같다 | 이전 정의는 엔진에 따라 달라지는 필드(UEParity에서 엔진마다 다른 restitution threshold, NumThreads 등)를 포함하면서 표 안에서 엔진 간 동일성을 요구했다. 그대로였다면 분석 검증기가 모든 UEParity 표와 스레드 스케일링 시리즈를 거부했을 것이다. T는 comparability key의 별도 필드다 | 리뷰(빌드) F23 |
| DL44 | 신규 (F16) · [MVP] | **UE 기본값 키는 모두 `[/Script/Engine.PhysicsSettings]`에 둔다.** UE 게임처럼 core 필드(DefaultGravityZ, SolverOptions 등)도 이 섹션에 둔다. `[/Script/PhysicsCore.PhysicsSettingsCore]` 섹션은 쓰지 않는다. Chaos 브리지는 `FChaosScene`을 만든 직후 리졸브된 SolverOptions로 `ApplyConfig`와 `SetIsDeterministic`을 명시적으로 호출하고, CDO가 ini를 읽는 것에 기대지 않는다 | UE 게임에서는 config 객체가 `UPhysicsSettingsCore`의 서브클래스인 `UPhysicsSettings`라서 모든 물리 설정이 `[/Script/Engine.PhysicsSettings]`에 있다 (BaseEngine.ini:3406 [V], PhysicsSettingsCore.h:18-19, PhysicsSettings.h:260-261 [V]). Engine 없이 도는 Chaos DLL의 `UPhysicsSettingsCore` CDO는 `[/Script/PhysicsCore.PhysicsSettingsCore]`를 읽는다. 그래서 사용자가 프로젝트의 DefaultEngine.ini 값을 붙여 넣어도 FChaosScene 생성자의 `ApplyConfig` (ChaosScene.cpp:90-94 [V])가 그 값을 쓰지 않는다 | 리뷰(사실) F16 |
| DL45 | 신규 (F37) · [MVP] | **문서 디렉터리 이름은 모든 곳에서 소문자 `docs/`다.** 이전 트리의 `Docs/`는 쓰지 않는다. 문서 사이 링크는 상대 경로로 건다 | 저장소에 이미 `docs/01_Feasibility.md`가 있다. Git은 대소문자를 구분하지만 NTFS는 구분하지 않으므로, `Docs/`와 `docs/`가 섞이면 GitHub에 디렉터리가 두 개 생긴다 | 리뷰(빌드) F37 |
| DL46 | 신규 (F27) · [MVP] | **Kit와 BackendCommon의 ODR을 막는다.** header-only Kit와 BackendCommon을 `namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { … } }`로 감싸고, `PT_BACKEND_NS`를 백엔드 타깃마다 다르게 정의한다 (또는 템플릿이 아닌 정의를 모두 internal linkage로 둔다). last-error와 로그 콜백 상태는 `TBackendApiProvider<T>` 안에 둔다. BackendCommon에 고정 폭 typedef를 두고 하네스의 `uint8`/`int32`를 쓰지 않는다. PhysX 백엔드 설정에서 `ClampRule`을 빼고, 규칙은 하네스 `FStepPlanner`가 소유한다 | `SetLastError`, `LogToHost`, `FTextWriter`, `UEDampingToBullet`(pow), `CanonicalizeContact` 같은 비템플릿 inline 함수가 /arch가 다른 백엔드들(Jolt만 AVX2)에 컴파일되면, 링커가 COMDAT 하나를 임의로 고른다. 그러면 Bullet 백엔드가 Jolt TU에서 컴파일된 AVX2/FMA 축약 코드를 실행할 수 있어 `test`와 `test-bullet` exe의 결과가 달라질 수 있다. 뷰어 안의 static 백엔드들이 inline 에러·로그 상태를 공유하는 문제도 생긴다. 백엔드는 하네스 타입을 include할 수 없다 (DL03) | 리뷰(빌드) F27 |
| DL47 | 신규 (F14, D1 개정) · [MVP] | **Bullet 캡슐은 `btCapsuleShapeZ`다.** 캡슐 축을 재매핑하지 않는다. D1의 캡슐 규칙을 "UE Z → PhysX X, Jolt Y, Bullet 재매핑 없음"으로 개정한다. `S03.CapsuleRest` 게이트(서면 75 cm, 누우면 25 cm)는 그대로 둔다 | `btCapsuleShapeZ`는 Bullet 3.25에 있다 (btCapsuleShape.h:128-131 [V]). Bullet에는 회전 decorator가 없어서 `btCapsuleShape`(Y축)에 Y→Z 회전을 주려면 compound로 감싸야 하는데, 이는 DL42의 "필요할 때만 래핑"에 어긋난다. 물리적으로는 같다. 원래 D1 문구("Jolt/Bullet Y")와 다르므로 개정으로 기록한다 | 리뷰(사실) F14, 리뷰(빌드) |
| DL48 | 신규 (F8) · [MVP] | **Normalized에서 sleep을 끄는 방법:** PhysX `PxSceneFlag::eDISABLE_SLEEPING`, Chaos `p.Chaos.Solver.Sleep.Enabled=0`(또는 파티클별 `ESleepType::NeverSleep`), Jolt `BodyCreationSettings::mAllowSleeping=false`, Bullet `DISABLE_DEACTIVATION`. 임계값을 0으로 두는 우회는 쓰지 않는다. G6가 검증한다 | PhysX 5.11에는 sleep을 아예 끄는 씬 플래그가 있다 (PxSceneDesc.h:359 [V]). 임계값 0은 sleep을 확실히 막지 못할 수 있고 sleep 장부 비용도 남는다. Chaos에는 전역 CVar (IslandManager.cpp:53 [V])와 파티클별 NeverSleep (IslandManager.cpp:240 [V])이 둘 다 있다. Jolt: BodyCreationSettings.h:106 [V], Bullet: btCollisionObject.h:25 [V]. 이전 문서의 [R] 두 개를 [V]로 바꾼다 | 리뷰(사실) F8 |
| DL49 | 신규 (F5) · [MVP M0] + [확장 M7, M13] | **G0 빌드 자기보고는 출처를 역할로 나눈다.** TU 자기보고는 어댑터 TU에서 알 수 있는 사실만 담는다: `__AVX2__`, `_M_FP_FAST`, `_MSC_FULL_VER`, `_DLL`, Jolt PUBLIC define. SDK 내부 설정은 `compile_commands.json` 안의 SDK 자체 TU 항목과 선택한 PhysX CMake 구성(debug/checked/profile/release)에서 가져온다. `PX_SUPPORT_PVD`와 `PX_ENABLE_SIM_STATS`는 TU 보고에서 뺀다. `BuildFlags.json`은 configure 시점이 아니라 `compile_commands.json`에 의존하는 빌드 시점 `add_custom_command`로 만든다. Chaos는 UBT `.rsp` 파일을 파싱한다 (`check_modules.py`). 단계: 두 출처의 생성(`BuildFlags.json`, TU 자기보고)은 M0, 두 출처를 대조하는 게이트 G0는 M7, Chaos `.rsp` 출처는 M13이다 ([02_Architecture.md](02_Architecture.md) §4.6) | `PX_SUPPORT_PVD`는 PhysX SDK 타깃의 구성별 PRIVATE 정의라 (windows/CMakeLists.txt:109-112 [V]) 공개 헤더에 없고, 어댑터 TU에서는 항상 정의되지 않는다. `PX_ENABLE_SIM_STATS`는 공개 헤더에 1로 하드코딩되어 있어 (PxPhysXCommonConfig.h:91 [V]) 비교해도 의미가 없다. Jolt `GetConfigurationString()`은 inline이라 호출한 TU의 설정을 보고한다. `compile_commands.json`은 generate 시점에 쓰이므로 configure 스크립트가 읽을 수 없다 | 리뷰(사실) F5, 리뷰(빌드) G0 지적 |
| DL50 | 신규 (F12) · [MVP] | **UE와 같은 이름을 쓰면 UE 5.8.3 시그니처를 따른다.** `FKBoxElem` / `FKSphylElem::Rotation`은 `FRotator`다. 원소에 `FQuat` 필드는 두지 않고, 호스트가 마샬링할 때 바디당 한 번 FQuat로 바꾼다 ([02_Architecture.md](02_Architecture.md) §5.2의 "마샬링 시 FQuat 캐시", 부록 A.0.4). `FBox`의 유효 필드는 `IsValid`다. 조인트 프리셋은 `ConstraintUtils::ConfigureAsBallAndSocket` / `ConfigureAsHinge`다. `AddImpulse_AssumesLocked(Handle, Impulse, bIsInternal)`과 별도의 `AddVelocity_AssumesLocked`를 둔다. `FDebugDrawQueue`는 Engine DrawDebugHelpers가 아니라 `Chaos::FDebugDrawQueue`를 미러링한다: `DrawDebugCircle(Center, Radius, Segments, Color, …, YAxis, ZAxis, bDrawAxis)`, Size가 없는 `DrawDebugPoint`, `FRotator`를 받는 `DrawDebugCoordinateSystem`. 그대로 따르지 못하는 것(예: 32 채널 충돌 필터, DL27)은 [02_Architecture.md](02_Architecture.md) §5.2 "UE 대응 차이" 표에 의도적 차이로 기록하고, 일치하는 항목도 같은 표에 "일치"로 남긴다 | 사용자는 UE 소스를 깊이 읽는 사람이라, 이름은 같은데 시그니처가 다르면 바로 알아챈다. 근거: BoxElem.h:30-33, SphylElem.h:30-33, ConstraintUtils.h:10,19, ChaosEngineInterface.h:465-467, DebugDrawQueue.h:114,150,209 [V] | 리뷰(사실) F12 |
| DL51 | 신규 (F6, F9) · M2.5 | **Chaos Program 타깃 규칙.** `PhysicsTestChaos.Build.cs`에 `PrivateIncludePathModuleNames.Add("Launch")`를 넣는다 (HeadlessChaos는 Public 변형을 쓴다). Target.cs의 `bHasExports=false`는 명시적으로 둔다. Monolithic 타깃의 기본값이기도 하지만, `*_API`가 비어 있고 import library가 생기지 않는다는 점을 분명히 하기 위해서다. `PT_GetBackendApi`는 자기 `__declspec(dllexport)`로 export된다 | `PtChaosModule.cpp`가 include하는 RequiredProgramMainCPPInclude.h는 `LaunchEngineLoop.h`와 `Runtime/Launch/Private/LaunchEngineLoop.cpp`를 include한다 (:6, :11 [V]). Launch 경로가 없으면 SP1이 첫 컴파일에서 실패한다. `bHasExports` 기본값은 `LinkType == Modular`일 때만 true다 (TargetRules.cs:1792-1795 [V]). 이전 문서의 "지정하지 않으면 모든 `*_API`가 export된다"는 틀린 설명이다. `bHasExports=false`는 /IMPLIB만 뺀다 (VCToolChain.cs:3480, 리뷰 확인) | 리뷰(사실) F6·F9, 리뷰(빌드) |
| DL52 | 신규 (F35) · [확장 M7] + M13 | **UE-in-DLL 프로세스는 결과를 먼저 쓰고 강제로 끝낸다.** bench는 모든 출력과 의도한 종료 코드를 담은 `exit.json`을 쓰고 flush한다. UE_RUNTIME 모듈이 올라간 프로세스는 마지막에 `TerminateProcess(GetCurrentProcess(), code)`로 끝낸다. 러너는 프로세스 종료 코드보다 `exit.json`을 믿는다 | `RequestExit`는 금지이고 모듈은 언로드하지 않으므로, UE 싱글턴(TaskGraph 스레드, GMalloc, 정적 소멸자)은 CRT process detach 때에야 해체된다. 결과를 다 쓴 뒤 이 해체가 크래시나 hang을 일으키면 러너가 종료 코드(0~6)를 잘못 읽는다 (PLAUSIBLE) | 리뷰(빌드) F35 |
| DL53 | 신규 (F31) · [MVP] + [확장 M10] | **타깃 그래프에 `PT::BenchCore`를 둔다.** 클럭 버킷, `FHostEnvironment`, 프로세스 spawner를 담고, `FThreadCensus`는 M10에 여기에 더한다. 의존 방향은 Conformance → BenchCore, Benchmark → Conformance다 | 이전 그래프는 순환했다: G10(Conformance, 지금의 G10a/G10b)은 FThreadCensus·cpu_eff·FHostEnvironment(Benchmark)가 필요하고, FPreflight(Benchmark)는 G0·G9·G10을 돌리며, G8은 bench exe가 자기 자신을 다시 띄워야 한다 | 리뷰(빌드) F31 |
| DL54 | 신규 (F19, F21, F22) · [MVP M0] | **SDK 가져오기와 제3자 고지.** Bullet은 `FetchContent_Declare(bullet3 GIT_REPOSITORY ${PT_BULLET_GIT} GIT_TAG 2c204c49 GIT_CONFIG core.longpaths=true SOURCE_SUBDIR _none_)` + `FetchContent_MakeAvailable`로 소스만 받고, `CMAKE_POLICY_VERSION_MINIMUM 3.10`을 둘러 `add_subdirectory(${bullet3_SOURCE_DIR} … EXCLUDE_FROM_ALL SYSTEM)`로 추가한다. PhysX 래퍼는 `PT::PhysX = physx_lib + PX_PHYSX_STATIC_LIB + DISABLE_CUDA_PHYSX`다. `physx_lib` 타깃 이름은 확인됐으므로 (physx/CMakeLists.txt:326 [V]) "M0에서 이름 확인" 작업은 없앤다. `THIRD_PARTY_NOTICES.md`는 받아 오는 모든 의존성과 라이선스를 적는다: PhysX `physx/` Apache-2.0, Jolt MIT, Bullet zlib, imgui MIT, implot MIT, stb MIT/퍼블릭 도메인, nlohmann/json MIT, doctest MIT, Tracy BSD-3 | CMake 3.30 이상에서 단일 인자 `FetchContent_Populate(<name>)`은 CMP0169로 금지된다. 검증한 프로토타입은 로컬 소스를 add_subdirectory했을 뿐 populate는 쓰지 않았다. `SOURCE_SUBDIR`가 CMakeLists.txt가 없는 경로(없는 디렉터리 포함)를 가리키면 MakeAvailable은 받기만 하고 add하지 않는다 (CMake 4.1.2 Modules/FetchContent.cmake:345-349 문서와 :2407 `if(EXISTS ${__cmake_srcdir}/CMakeLists.txt)` [V]). Bullet `CMakeLists.txt:1`의 `cmake_minimum_required(2.4.3)`는 CMake 4.x가 거부한다 [V]. 이전 고지 목록에는 stb, json, doctest, Tracy가 빠져 있었다 | 리뷰(사실) F19·F21·F22, build-bullet 연구 노트, 01 §3.1, §8 |

### 3.2 단계화 정책 S1~S7 (DL36 세부)

| S | 정책 | 반영한 결정 |
|---|---|---|
| S1 | 아키텍처의 모든 주요 기능과 절에 [MVP] (M0~M6, 게이트·동결 마일스톤까지) 또는 [확장] (이름 붙은 이후 마일스톤)을 붙인다 | 이 문서의 상태 열, [02_Architecture.md](02_Architecture.md) 전체 |
| S2 | 클럭: MVP는 QPC(`std::chrono::steady_clock`이 MSVC에서 QPC를 감쌈 [V])다. rdtscp+QPC 보정은 M10이다 | DL10 |
| S3 | [확장] 항목: TSC 클럭(M10), FThreadCensus와 cpu_eff 게이트(M10), CV 게이트와 sentinel(M8은 기본 CV 보고만, 게이트와 sentinel은 M10), bootstrap CI·주장 규칙·comparability key(M10), 적합성 스탬프(M10, 키는 DL20), test-<engine> 바이너리 구성 대조군(M10), prof 프리셋(M10), PtAbiCodec loopback과 A8(M14 또는 백로그), A4 1만 개 fuzz는 MVP에서 표 기반 음성 사례 약 30개로 대체, OOP 전송(M14 조건부), GhostOverlay·Replay·WARP 골든 PNG(M12), Mode C iso-quality sweep(백로그), convex quickhull과 convex 질량(백로그, convex는 데이터 모델과 ABI에만 남음), BulletMt·PhysX 3.4·double 빌드(백로그) | DL11, DL20, DL21, DL28, DL31, DL37 |
| S4 | 프리셋: MVP는 dbg, dev, test. test-avx2는 M7, prof와 test-<engine>은 M10에서 추가한다 | DL12, DL13 |
| S5 | UE 관용구(FCommandLine/FParse, CVar 레지스트리, ini 리더, UE_LOG, check/ensure)는 UE 전체 복제가 아니라 작은 최소 구현이다 | DL18 |
| S6 | FStepPlanner의 모든 모드는 작으므로 M1에서 구현하고 단위 테스트한다. MVP 씬은 FixedStep과 Sync만 쓴다. Chaos로 하는 Native driver 검증은 M13이다 | DL08, DL09 |
| S7 | 사용자는 Chaos를 쓰기 전에 Bullet, PhysX, Jolt가 도는 것을 보고 싶어 한다. 그래서 뷰어는 첫 엔진(Jolt) 슬라이스 바로 뒤에 온다 | DL37 |

---

## 4. 사용자에게 묻는 질문

[01_Feasibility.md](01_Feasibility.md) §10의 질문과 설계 단계의 열린 질문을 합치고 중복을 없앴다. 같은 질문은 01의 번호(Q1~Q10)를 그대로 쓰고, 새 질문은 Q11부터 붙였다. 답이 없어도 대부분은 권장 기본값으로 진행한다.

마일스톤 열의 표기는 다음과 같다.
- **차단:** 답이 없으면 그 마일스톤을 끝낼 수 없다.
- **기본값 진행:** 기본값으로 시작하고, 답이 오면 ini나 설정만 바꾼다.

### 4.1 질문 표

| Q | 질문 | 권장 기본값 | 영향 | 마일스톤 | 출처 |
|---|---|---|---|---|---|
| Q1 | GitHub 저장소(github.com/chay116/PhysicsTest)를 공개로 둘지 비공개로 둘지. Chaos 비교 결과를 공개할지 | 비공개로 시작한다. UE EULA(조사 중 HTTP 403으로 원문 미확인)와 각 SDK 라이선스의 벤치마크 조항을 사용자가 확인한 뒤 공개를 정한다. 우리 글루 코드만 커밋하고 `PT_WITH_CHAOS`는 기본 OFF다 | Chaos 글루와 UE 유래 상수를 저장소에서 얼마나 엄격하게 떼어 놓을지, 리포트를 누구와 공유할지 | 기본값 진행. 첫 push(M0) 전에 저장소 공개 여부를 확인하기를 권장한다. 결과 공개는 **M11 리포트 공유 전 차단** | 01 Q1 + 설계 질문 |
| Q2 | 씬 단위와 축 | **결정됨 (D1).** UE 기준 cm, kg, s, Z-up, double. Jolt, Bullet, PhysX-SdkDefault만 m로 변환한다. 캡슐 재매핑은 D1 개정(DL47)을 따른다 | - | 해당 없음 | 01 Q2 |
| Q3 | 헤드라인 숫자에 쓸 빌드 플래그 프로필 | `Shipped`(각 SDK가 의도한 플래그, Jolt만 AVX2 명시)를 헤드라인으로, `MatchedAVX2`는 민감도 확인용으로 쓴다. M10의 주장 규칙은 두 프로필에서 방향이 같을 것을 요구한다 (DL21) | ISA 차이(Jolt만 약 1.8배)를 어떻게 해석할지 | 기본값 진행. M7(test-avx2 프리셋), M10 | 01 Q3 |
| Q4 | 질량 규칙 기본값: UE BodyUtils 규칙(mass^0.75)인가 Physical(지수 1)인가 | UEBodyUtils를 두 물리 프로필의 기본으로 둔다. 방법론 씬은 `bOverrideMass`로 질량을 고정하고, `M7.UEDefaults`가 ^0.75 효과를 보여 준다. Physical은 질량 축의 값으로 언제든 고를 수 있다. **01의 초기 권장(Normalized = 물리적 밀도)에서 바꿨다.** 질량 규칙을 물리 프로필에 묶으면 UEParity 대 Normalized 비교에 질량 차이가 섞이기 때문이다 (DL17). 01의 조합이 필요하면 Normalized run에 Physical 축을 지정하면 된다 | 질량을 고정하지 않은 바디의 질량, 거동 비교 | 기본값 진행. M9(거동 씬·프로필 완성) 전에 확인하기를 권장한다 | 01 Q4 + 설계 질문 |
| Q5 | PhysX 재현 수준 | 5.11 + UE4Compat로 충분하다. PhysX 3.4 백엔드는 백로그(M15)다 | 주 PhysX 행의 의미 ("UE4 settings on PhysX 5.11") | 기본값 진행 (M5) | 01 Q5 |
| Q6 | Chaos 헤드라인 층과 구성 | `FChaosScene` 경로의 `T_frame`이 헤드라인이고, solver 시간과 Evolution은 진단이다 (DL07, DL25). 구성은 Test다. T=1은 SingleThread + `p.Chaos.SingleWorkerPhysics=1` + `-corelimit=1`이고 (DL39), 스케일링은 TaskGraph다. Shipping+logging 민감도 빌드는 선택이다 | 리포트에서 "Chaos 속도"가 무엇을 뜻하는지 | 기본값 진행. M6 ChaosBackendGuide 초안에 적고, M13 시작 전에 확인한다 | 01 Q6 + 설계 질문 |
| Q7 | 디버거: VS Code + cppvsdbg로 충분한가, VS 2022 IDE를 설치하는가, 이미 설치된 CLion을 쓰는가 | VS Code + C/C++ 확장(cppvsdbg)을 dbg / dev Ninja 프리셋과 함께 쓴다. VS multi-config 프리셋은 두지 않는다. 지금은 VS IDE와 VS Code가 없고 VS BuildTools만 있으므로 이 둘은 M0에서 설치가 필요하다. CLion 2026.1은 이미 설치되어 있어 Ninja 프리셋 + MSVC 툴체인으로 바로 쓸 수 있지만 MSVC 디버깅 품질은 [R]이다 ([03_WorkPlan.md](03_WorkPlan.md) M0 환경 표) | VS 생성기 경로(`PhysXBootstrap`)를 유지할지 | M0 환경 준비 (빌드는 막지 않음) | 01 Q7 + 설계 질문, 리뷰(빌드) F39 |
| Q8 | Jolt 접촉 임펄스를 얻으려고 로컬 패치를 허용할지 | 허용하지 않는다. Jolt 접촉은 근사로 표시하고 (phi ≤ 0, impulse NaN) 패치는 백로그(M15)에 둔다 | Jolt 임펄스 디버그 드로, 정확한 활성 접촉 수 | 기본값 진행 | 01 Q8 + 설계 질문 |
| Q9 | 벤치 세션 동안 Windows 전원 계획을 High performance나 Ultimate로 직접 바꿔 줄 수 있는가 | 예. 러너는 Balanced이면 거부하고 (`--allow-balanced`로 우회), 시스템 설정은 절대 바꾸지 않는다 | Balanced에서 실행 간 최대 2.5배 노이즈가 관찰됐다 [V] | M7·M8의 측정은 Balanced이면 플래그가 붙는다. **M11 캠페인 차단** | 01 Q9 + 설계 질문 |
| Q10 | UE 4.27 소스(Epic 계정에 연결된 GitHub)로 [M] 키를 확인할 수 있는가: filter word 배치, contact offset clamp 공식, 8/1 반복, UseDelta 사전 clamp, D6 actor swap, 정지마찰 = 동적마찰, UE4 substep의 MinPhysicsDeltaTime 처리 (M1은 UE5 솔버 루프와 같게 적용 [R]) | 4.18에서 온 값을 증거 태그가 붙은 ini 키로 두고 진행한다. 확인한 뒤에는 키만 바꾸면 되고 코드 변경은 없다 | 주 PhysX 행의 충실도 | 기본값 진행. M11 캠페인 전에 확인하기를 권장한다 | 01 Q10 + 설계 질문 |
| Q11 | UEParity에서 Jolt와 Bullet에 Chaos 등가 반발 임계(1000·dt cm/s)와 UE sleep 임계 매핑을 쓸지, 엔진 네이티브 값을 쓸지 | Chaos 등가 매핑을 쓴다. PhysX-UE4Compat는 200 cm/s를 유지하고, Normalized는 모든 엔진 50 cm/s다 (DL35) | UEParity 표의 바운스·sleep 거동과 by-design 태그 | 기본값 진행. M9 전에 확인한다 | 설계 질문 |
| Q12 | 하네스 코어 FP 모델: /fp:fast를 유지하고(D5) 하네스 ISA를 고정하는가, /fp:precise로 컴파일하는가 | /fp:fast를 유지하고 하네스 타깃에는 /arch를 걸지 않는다. 프로필 간 `desc_sha256` 동일성 테스트가 입력 안정성을 지킨다 (DL12) | 하네스가 계산한 입력(질량, 기준값)의 빌드 간 비트 안정성. 바꾸면 고정 결정 D5가 바뀐다 | 기본값 진행 (M0 툴체인). 나중에 바꾸면 골든 해시를 다시 잡아야 한다 | 설계 질문 |
| Q13 | M2.5 hello-Chaos-DLL 스파이크를 일찍(M5~M6과 병행) 진행할지. `D:\UnrealEngine` 5.8.3 소스 빌드(UBT와 `Build.bat`이 동작하는 상태)와 10 GB 이상의 여유 디스크가 필요하다 | 예. 3 달력일 타임박스로 하고, 결과와 관계없이 Phase 1은 계속한다. M0에서 UE 소스 빌드 상태를 점검한다 | 스파이크 결과가 v1.0 동결(M6) 전에 반영되면 나중의 major 버전 증가와 Chaos 재빌드를 피한다 | **M2.5 차단** (UE 빌드, 디스크, 사용자 약 2 pd) | 설계 질문, 01 §1 권장 진행 방식, 리뷰(빌드) F39 |
| Q14 | in-process 스파이크가 통과해도 OOP Chaos 전송(M14, 4 pd)을 미리 만들지 | 아니요. SP2나 SP3가 실패할 때만 만든다 | UE의 프로세스 전역 부작용을 격리하는 이득 대 4 pd | 기본값 진행. M14는 M2.5 결과로 정한다 | 설계 질문 |
| Q15 | MatchedAVX2 Chaos 빌드(두 번째 UBT 타깃, `MinCpuArchX64=AVX2`, intermediate 3~5 GB 추가 [R])를 원하는가 | 예. 다만 M13 이후 재캠페인에서 만든다. Chaos의 주장도 두 플래그 프로필 주장 규칙을 만족하게 된다 | 없으면 Chaos 비율은 Shipped 프로필 기준으로만 표기할 수 있다 | 기본값 진행. M13 / 재캠페인 | 설계 질문 |
| Q16 | Chaos AsyncFixed에서 step-aligned 상태가 필요한가 | 아니요. GT 보간 상태에 "보간, 한 스텝 지연" 라벨을 붙이고 바디별 비교에서 뺀다. 필요하면 PT 측 sim callback으로 읽는 진단 경로를 M13에 더한다 (DL40) | AsyncFixed 모드의 거동 비교 범위 | 기본값 진행. M13 | 리뷰(사실) F3 |
| Q17 | ptbench용 Python 패키지(numpy, pandas, pyarrow, scipy, matplotlib, jsonschema)를 PyPI에서 받아 git이 무시하는 `.venv`에 설치해도 되는가, 아니면 오프라인 wheelhouse가 필요한가 | PyPI에서 버전을 고정(lock 파일)해 `.venv`에 설치한다. 현재 Python 3.11.9는 있지만 numpy조차 없다 (`ModuleNotFoundError` [V]) | 분석 파이프라인을 돌릴 수 있는지 | M0 환경 준비. **M8 차단** | 리뷰(빌드) F39 |
| Q18 | 뷰어 1단계 범위: 단일 씬 + 엔진 전환으로 충분한가, 같은 화면 비교(SideBySide)를 더 일찍 원하는가 | 단일 씬 + 엔진 전환(M4)으로 시작한다. SideBySide, GhostOverlay, Replay, Profiler는 M12다. PhysX UE4Compat / -TGS / -Stab 나란히 보기도 M12에서 한다 (DL14) | SideBySide를 M4로 당기면 체크포인트 A 전에 약 1 pd가 늘어난다 [R] | 기본값 진행 (M4) | 리드 S7, 리뷰(빌드) F29·F40 |
| Q19 | PhysX의 dev 구성: dev(Release) 프리셋은 지금 PhysX `release` 라이브러리(`PX_SUPPORT_PVD=0`, PhysX 체크 없음)를 링크한다. UE4 Development는 PhysX PROFILE 라이브러리를, Test/Shipping은 SHIPPING 라이브러리를 링크했던 것으로 기억한다 [R] (로컬에 UE4 PhysX 빌드 규칙이 없다). 4.27의 `PhysX.Build.cs`로 확인할 수 있는가 | 헤드라인인 test 프리셋은 `release`를 유지한다 (UE4 Test/Shipping에 해당한다고 본다 [R]). dev도 우선 `release`로 두고, 4.27 확인 결과가 PROFILE이면 M5에서 dev의 PhysX 구성만 바꾸는 옵션을 더한다. 같은 결정에서 어댑터 TU에 `PX_DEBUG`/`PX_CHECKED`/`PX_PROFILE`/`PX_SUPPORT_PVD`를 `$<CONFIG>`별로 넘길지도 정한다 (지금은 `PX_PHYSX_STATIC_LIB`, `DISABLE_CUDA_PHYSX`만 넘긴다) | dev 프리셋의 PhysX 측정값과 UE4Compat의 "UE4 설정" 주장. 어댑터 TU와 라이브러리의 `PX_*` 매크로 일치(인라인·assert ODR) | 기본값 진행 (M5) | M0 독립 검사 |

**M0 구현 메모 (Q12, DL23) [V]:** Q12는 기본값(`/fp:fast`)으로 진행했다. M0에서 MSVC 14.44가 `/fp:fast`일 때 `static_cast<double>(constexpr float)`를 float로 반올림하지 않고 접는 것을 확인했다. `(double)(1.0f/60.0f)`가 `/fp:fast`에서는 0.016666666666666666, `/fp:precise`에서는 0.01666666753590107이다 ([Progress.md](Progress.md) M0 §4). 그래서 DL23의 해석 기준값은 엔진이 실제로 받는 float 값(런타임 값, `volatile` 경유 등)으로 계산한다. 반대로 `FStepPlanner`처럼 엔진 입력을 만드는 하네스 코드는 UE 게임 빌드와 같은 `/fp:fast`를 유지한다. UE도 Game 타깃의 기본 FP 모드는 `/fp:fast`이고, Editor·Program 타깃만 BuildSettingsVersion V7부터 `/fp:precise`다 (UnrealBuildTool `Platform/Windows/VCToolChain.cs:1324-1335` [V]). 하네스 전체를 precise로 강제하면 UE와 달라진다. 또 `/fp:fast`에서는 NaN 비교 결과가 정해지지 않으므로 (M0에서 `FMath::Clamp(NaN, -1, 1)`이 -1로 접혔다) 어떤 코드도 NaN 결과에 기대지 않는다.

**M1 구현 메모 (DL23, DL17, DL18) [V]:**
- DL23의 `dt_engine = (double)(float)dt`는 `PhysicsTest::Analytic::EngineDeltaTime`(PhysicsCore `Physics/AnalyticReference.h`)이 만든다. 값을 `volatile float`에 한 번 저장한 뒤 double로 바꾸고, 별도 번역 단위에 둔다. MSVC 14.44는 `static_cast<double>(constexpr float 1.0f / 60.0f)`를 dbg(/Od)를 포함한 세 프리셋 모두에서 정확한 1/60(0.016666666666666666)으로 접는다. 단위 테스트 `PhysicsTest.Analytic.EngineDeltaTimeSurvivesFastMath`가 결과 비트(0x1.111112p-6)를 고정하므로, 이 헬퍼가 접히면 테스트가 실패한다. `FStepPlanner`는 UE 게임 타깃처럼 `/fp:fast` 그대로 런타임 값으로 계산한다.
- DL17의 질량 규칙 순서는 UE 5.8.3 BodyUtils.cpp를 그대로 따른다. 4·5단계는 double로 계산하고(UE는 float), 대각화는 Jacobi다 ([02_Architecture.md](02_Architecture.md) §5.2 #22).
- DL18의 최소 리졸버는 [02_Architecture.md](02_Architecture.md) §10의 M1 구현 메모대로 만들었다. Normalized의 최대 각속도 해제는 0이 아니라 큰 값이다. Chaos는 `MaxAngularSpeedSq`로 무조건 clamp하므로 0이면 회전이 멈춘다 (PBDRigidsEvolutionGBF.cpp:950-954).

**M2 구현 메모 (DL08, DL19, DL28, DL29, DL40) [V]:**
- DL29: ABI는 v0.9(major 0) 그대로다. 호스트는 자기 major로 `PT_GetBackendApi`를 부르고, major 1은 NULL이다 (A1). 규칙의 정본은 [Abi.md](Abi.md)다.
- DL08: Native driver의 계약 `NumStepsTaken == Plan.N`에서 N은 엔진이 실제로 진행한 스텝 수 `FStepPlan::NumSteps`다 (밀어 넣은 스텝 수 `NumPushedSteps`가 아니다. 둘은 MinDeltaTime에서 갈라진다). Native driver는 UE5처럼 `OnPhysSceneStep`을 프레임당 한 번 프레임 dt로 부른다. Harness driver로 도는 씬은 `AsyncDt = -1`로 만들고 async 누산기는 하네스가 돌린다. UE5 substepping에서 Chaos가 실제로 스텝하는 double dt(D/N)를 `FStepPlan::InternalStepDeltaTime`으로 따로 둔다. FixedStep·Sync에서는 float 값과 같아서 두 driver가 비트 단위로 같다 (A10, Null로 확인).
- DL28: A0~A7, A9, A10을 Null의 static과 DLL 두 경로로 통과시켰다. A4는 표 기반 음성 사례 70개(모듈 4 + 씬 66, "약 30개 이상". 수정 라운드 1에서 해제한 바디 사례 5개를 더했다)이고 1만 건 fuzz는 그대로 M15다. A9는 `Tools/check_modules.py`(dumpbin)와 `PtAbiUeCompatCheck` 빌드다.
- DL40: `AsyncFixedTimeStepSize = 0`은 Kit이 `PT_E_INVALID_ARG`로 거부한다 (A4). 0이 아닌 양수는 `NATIVE_FRAME_MODEL` 백엔드만 받는다 (그 밖은 `PT_E_UNSUPPORTED`).
- DL19: A7은 일반 키 24개 + `GetInfo`의 백엔드 키를 확인한다. Null은 일반 집합만 선언하고, 개념이 없는 키는 `native="n/a"`, COMPILE_TIME으로 낸다.
- DL08 (M2 수정 라운드): 프레임 안의 명령 규칙은 UE5 Chaos를 따른다. 힘은 push한 프레임의 모든 스텝에 작용하고(`CopySubstepData`), push가 없는 프레임의 힘은 다음 push에서 1/ExternalSteps로 평균되며, `bAllowSubstepping`은 무시한다. kinematic 목표는 `PseudoFraction`(solver substep이면 1/(N−k), 아니면 첫 스텝)이다. Native 백엔드(Null)와 Harness driver의 UE5 규칙이 같고, Harness driver의 `UE4PhysSubstep` 규칙(PhysX UE4Compat)만 UE4 `FPhysSubstepTask` 방식이다. 이 규칙은 M6에 핵심 ABI와 함께 동결된다 ([Abi.md](Abi.md) §5.10).
- DL29 (M2 수정 라운드): UBT 브리지의 `PT_BACKEND_NS`는 `ChaosBridge`다. `Chaos`이면 `PhysicsTest::Abi::Chaos`가 UE `::Chaos`와 모호해진다.
- DL08 (M2 수정 라운드 1): 두 driver가 같은 명령 흐름을 보낸다. 매 프레임 push하므로 스텝이 없는 프레임의 쓰기도 그 프레임에 백엔드에 닿고, `AddForce`/`AddTorque`의 GT 깨우기는 호출한 자리의 `WAKE_UP`, 힘은 스텝을 push하는 프레임까지 씬이 들었다가 `PT_CMDF_NO_WAKE`로 보낸다 (PutToSleep이 그 앞의 힘을 버리는 UE 규칙 포함). `OnPhysSceneStep`은 두 driver 모두 프레임당 한 번이다 (UE4 규칙만 서브스텝마다). kinematic 목표는 모든 진입점(`SetBodyTransform`, `FPhysicsInterface`)이 씬 동사 `SetKinematicTarget_AssumesLocked`로 간다. 그래서 FixedStep·Sync의 비트 동일이 스텝이 없는 프레임과 무작위 명령에서도 성립한다 (A10 (b)). 남는 차이는 다중 스텝 프레임의 토크 월드 관성 시점이다 ([Abi.md](Abi.md) §5.10).
- DL29 (M2 수정 라운드 1): Kit이 해제 집합을 들고 해제 관련 검사를 모든 백엔드에 같게 한다. `PT_READ_ALL`은 해제한 바디까지 빽빽하고(UNINITIALIZED), `PT_READ_ACTIVE_ONLY`는 `OutIndices`가 필요하다. 명령 flag `PT_CMDF_NO_WAKE`(bit 3)를 더했다. 모두 v0.9 안의 변경이다 (major 0 유지).

**M3 구현 메모 (DL35, DL42, DL43, DL46, DL48) [V]** (M4에서 기록. M3가 이 결정들을 구체화했지만 이 문서에 메모를 남기지 않았다는 M3 검토 지적)
- DL35: UEParity의 Jolt 반발 임계는 Chaos 등가 1000·dt cm/s를 `PhysicsSettings::mMinVelocityForRestitution`에 넣는다. `PtSceneDesc::RestitutionThreshold`가 있으면(Normalized 50 cm/s) 그 값이다. 값은 `options_sha256`에 들어간다 ([02_Architecture.md](02_Architecture.md) §8.3 M3 메모). Bullet도 M5에서 같은 매핑을 쓴다.
- DL42: 하네스 쪽 COM 항등 판정(`CanUseNativePrimitive`, double)은 1e-6 cm 그대로다. Jolt 쪽에서 엔진 셰이프 COM과 하네스 COM을 비교할 때는 max(1e-6 cm, 16 ulp × 바디 크기)로 판정한다. Jolt가 COM을 m 단위 float로 계산하므로 1e-6 cm(1e-8 m)는 float 해상도 아래이고, 그대로면 COM이 같은 compound도 언제나 `OffsetCenterOfMassShape`를 받았다 ([Progress.md](Progress.md) M3 §3 #22).
- DL43: 바디별 반복 횟수 override(`PT_BODY_OVERRIDE_ITERATIONS`)는 `desc_sha256`에서 빼고 `options_sha256`에 넣었다. 엔진마다 뜻이 다르고 변형이 바꿀 수 있기 때문이다. `options_sha256`에는 SimFlags, AsyncFixedTimeStepSize, 씬 MaxLinearVelocity·반복 횟수, 용량 힌트, frame driver도 들어간다. 또 `desc_sha256`은 **한 빌드 구성 안에서만** 엔진 간 같다. 하네스가 `/fp:fast`라 dbg(`/Od`)와 dev/test(`/O2`)가 하네스 계산값(정적 바닥의 관성, `FRotator` → `FQuat`)의 마지막 1~2 ulp에서 다르다. manifest의 `scene.desc_sha256_scope`가 범위를 적는다 ([Progress.md](Progress.md) M3 §3 #6, #24, [Scenes.md](Scenes.md) §1).
- DL46: ODR 규칙을 Kit·BackendCommon에서 어댑터 코드 전체로 넓혔다. 어댑터 컨테이너는 SDK 고유 타입(Jolt는 `JPH::Array`)이고 `std::` 컨테이너를 쓰지 않는다. `/arch`가 하네스와 다른 어댑터 타깃은 `_NO_CRT_STDIO_INLINE` + `legacy_stdio_definitions`를 쓴다. 잡 스레드 콜백은 할당하지 않는다. CTest `PhysicsTest.Probe.AdapterComdats`가 링커 맵으로 지킨다 (Debug는 보고만) ([Progress.md](Progress.md) M3 §3 #21, [Abi.md](Abi.md) §5.13).
- DL48: Jolt는 Normalized에서 바디마다 `BodyCreationSettings::mAllowSleeping = false`로 sleep을 끈다. 그 씬에서는 `!bStartAwake` 바디를 재우는 생성 마지막 단계를 건너뛰고 `jolt.start_asleep_skipped`로 센다 (ABI 규칙: sleep을 끈 씬은 모든 엔진이 이 단계를 건너뛴다).

**M4 구현 메모 (DL33, DL37, DL46, DL50) [V]**
- DL37 (M4 수정 라운드 2): 성능 기준 "씬당 스냅샷+드로 CPU ≤ 4 ms"는 연속 부하(`-Headless` 또는 창 모드 `-NoVSync`)에서 재고, 값은 Stats에 표시한다. 기본 창 모드(vsync)의 Stats 값은 프레임마다 vsync를 기다리며 약 12 ms 쉬는 스레드의 값이라 판정에 쓰지 않는다 (같은 P코어에서 같은 일이 2~3배 느렸다 [V]. Stats가 그때 한 줄로 알린다). 리드의 M4 지시("헤드리스에서 GPU timestamp query와 CPU 타이머로 잰다")를 기준 문장으로 옮긴 것이다 ([03_WorkPlan.md](03_WorkPlan.md) M4, [02_Architecture.md](02_Architecture.md) §11.2, [Progress.md](Progress.md) M4 §8). ≥ 60 fps도 같은 연속 부하 측정이다.
- DL46 (M4 수정 라운드 2): `PhysicsTest.Probe.AdapterComdats`는 어댑터를 링크하는 모든 exe의 map을 본다. `PT_WITH_VIEWER` 프리셋에서는 `PhysicsTestViewer.exe`와 `PhysicsTestViewerTests.exe`가 더해진다 (dev strict 통과).
- 뷰어 QoS (M4 수정 라운드 2, DL11): 뷰어는 전원 스로틀링 opt-out을 하지 않는다. 수정 라운드 0에 넣은 opt-out은 창 모드 vsync 값도 헤드리스 값도 바꾸지 않았다 (헤드리스 10k 전부 그림 스냅샷+드로 중앙값 opt-out 1.68·1.73 ms, Windows 기본 1.77·1.80 ms). 벤치와 뷰어의 QoS 정책은 M7 호스트 제어(affinity, 우선순위, power throttling)가 함께 정한다.
- DL33: 뷰어는 별도 exe(`PhysicsTestViewer.exe`)이고 벤치는 렌더러를 링크하지 않는다 (`PhysicsTest.Probe.BenchNoRenderer`). M4는 렌더러와 무관한 디버그 드로 파이프라인(`PT::PhysicsDebugDraw`)을 Automation에 넣었고, Automation도 d3d11·dxgi·d3dcompiler를 import하지 않는다 (`PhysicsTest.Probe.AutomationNoRenderer`). 뷰어가 여는 씬은 언제나 `RunMode = Viz`이고, 뷰어가 쓰는 manifest(스크린숏 `<png>.json`, `-PerfReport`)는 `mode = viz`다.
- DL50: `FDebugDrawQueue`는 `Chaos::FDebugDrawQueue`의 이름, 인자 순서, 기본값을 따른다 (크기 인자가 없는 `DrawDebugPoint`, `FRotator`를 받는 `DrawDebugCoordinateSystem`, `YAxis`/`ZAxis`를 받는 `DrawDebugCircle`). 비용과 영역 판정은 레거시 큐가 아니라 5.8.3이 기본(`bChaosDebugDraw_UseNewQueue = true`)으로 쓰는 경로를 따른다 (수정 라운드 0에서 고침. 처음 구현은 레거시 큐 비용에 명령 기준점 하나로 영역을 봐서 UE와 달랐다): 캡처 단계는 `FChaosDDPrimitives` 비용(점·선·화살표·원 1, 구 64, 캡슐 16, 박스 12, 삼각형 3, 문자열 10)과 명령 bounds 판정, 셰이프는 `FChaosDDParticle` 경로(셰이프당 1, 파티클 bounds)이고, 렌더 단계 `FDebugDrawRenderStage`가 `FChaosDDRenderer`의 두 번째 예산(박스 12, 원 8, 구 16, 화살표 3, 문자열 10. 수락하면 비용을 두 번 더하는 UE 동작 포함)과 영역 판정(<= W²)을 한다. 하네스와 UE가 다른 곳은 렌더 단계의 `Radius 0`뿐이다 (UE는 W > 0 조건이 없어 거의 아무것도 그리지 않고, 하네스는 어디서나로 둔다). 하네스 확장은 `DrawDebugTriangle`, `DrawParticleShape`, `Enqueue`(네이티브 명령), `NoteRegionRejection`(Stats 계수), `GetCommandBounds`다. `IDebugDrawRenderer`는 `IChaosDDRenderer`의 메서드와 인자 순서 그대로이고 `RenderLatentCommand`도 있다 ([02_Architecture.md](02_Architecture.md) §11 M4 구현 메모, [Progress.md](Progress.md) M4 §6).

### 4.2 마일스톤별 요약

| 마일스톤 | 차단하는 질문 | 확인을 권장하는 질문 |
|---|---|---|
| M0 | 없음 | Q1(첫 push 전 공개 여부), Q7(디버거 선택·설치), Q12(FP 모델), Q13·Q17(환경 점검) |
| M2.5 | **Q13** (UE 5.8.3 소스 빌드, 10 GB, 사용자 약 2 pd) | - |
| M5 | 없음 | Q5, Q10, Q19 |
| M6 | 없음 | Q6 (ChaosBackendGuide 초안에 기본값을 적음) |
| M7 | 없음 | Q3, Q9 (Balanced이면 측정에 플래그가 붙음) |
| M8 | **Q17** (분석 패키지) | Q9 |
| M9 | 없음 | Q4, Q11 |
| M11 | **Q9** (전원 계획). 결과 공개는 **Q1** | Q10 |
| M12 | 없음 | Q18 |
| M13 | 없음 | Q6, Q15, Q16 |
| M14 | 없음 | Q14 (M2.5 결과와 함께 정함) |

---

## 5. 리뷰 수정 항목 색인 (F1~F41)

두 리뷰에서 뽑은 수정 항목과 이 문서에서 반영한 위치다. 구조·일정의 세부 반영은 [02_Architecture.md](02_Architecture.md)와 [03_WorkPlan.md](03_WorkPlan.md)에 있다.

| F | 요지 | 반영 |
|---|---|---|
| F1 | Chaos T=1은 SingleThread + `p.Chaos.SingleWorkerPhysics=1` + `-corelimit=1`. T≥2의 MaxNumWorkers는 상한 | DL39, Q6 |
| F2 | 바디 생성 순서를 UE 순서로, `bStartAwake=false` 게이트 (`S08.StartAsleep`, G6) | DL38, DL05, DL15, DL34 |
| F3 | Chaos AsyncFixed는 보간 상태, sync는 `AsyncDt=-1` | DL40, DL08, Q16 |
| F4 | UE 5.8.3 ECC_MAX = 65, uint64 마스크. 32 채널은 의도적 단순화 | DL27 |
| F5 | G0 출처 분리: TU 보고는 어댑터 사실만, SDK는 compile_commands, Chaos는 .rsp | DL49 |
| F6 | Chaos Build.cs에 Launch include 경로 | DL51, DL30 |
| F7 | 질량 규칙 단계 순서와 override·무머티리얼 분기 | DL17 |
| F8 | Normalized sleep off의 엔진별 방법 | DL48 |
| F9 | `bHasExports=false` 설명 정정 | DL51 |
| F10 | 정지마찰 결합. 로컬 소스로 식을 다시 확인해 머티리얼별 max를 더함 | DL41 |
| F11 | 디버그 드로 순서와 `DrawConstraintGraph` 이름 | DL26 |
| F12 | UE 명명 충실도와 "UE 대응 차이" 표 | DL50 |
| F13 | 박스 마진은 셰이프별 readback (Bullet, Chaos) | DL19 |
| F14 | Bullet `btCapsuleShapeZ`, D1 개정 | D1, DL47 |
| F15 | PhysX 제약은 스케일 단위. UE4Compat 계열은 한 프로세스 공존 | DL14 |
| F16 | ini 섹션 `[/Script/Engine.PhysicsSettings]`, SolverOptions 명시 적용 | DL44 |
| F17 | `M4.InclineFriction`, 카탈로그 확장 씬 등록 | DL34 |
| F18 | capability 표: Bullet 드로어 viz 전용, Jolt 섬 근사 | DL22 |
| F19 | Bullet FetchContent 방식 (CMP0169) | DL54 |
| F20 | IChaosDDRenderer 순서 검증 완료, HalfHeight·Size 의미 | DL26 |
| F21 | `physx_lib` 이름 확인 완료 | DL54 |
| F22 | 제3자 고지 전체 목록, BodyInstanceCore 인용 | DL54, DL38 |
| F23 | `desc_sha256` 범위를 엔진 독립 필드로 | DL43, DL05 |
| F24 | 적합성 스탬프 키 | DL20 |
| F25 | COM 래핑은 필요할 때만, `shape.wrapping` 기록 | DL42 |
| F26 | 핵심 v1.0을 M6에 동결, 잠정 진입점, Null의 native frame model 게이트 (A10) | DL29, DL08, DL28 |
| F27 | Kit·BackendCommon ODR (inline namespace, 상태 위치, ClampRule 제거) | DL46 |
| F28 | doctest `check()` 충돌, 하네스 측 Start/End | DL03 |
| F29 | 뷰어 성능 기준 (일시정지·리플레이 60 fps, 4 ms) | DL37 |
| F30 | 해석 기준값은 `dt_engine = (double)(float)dt` | DL23 |
| F31 | `PT::BenchCore`로 순환 제거 | DL53 |
| F32 | 필수 effective 키 = 공통 + 백엔드별 | DL19 |
| F33 | 반발 게이트 측정법과 사전 등록 허용오차 | DL23 |
| F34 | 단일 스레드 dispatcher의 LIFO 스택 | DL15 |
| F35 | UE-in-DLL 종료: `exit.json` + TerminateProcess | DL52 |
| F36 | 헤드라인 run의 해시는 마지막 프레임만, `CountersEvery=0` | DL22 |
| F37 | 문서 디렉터리는 소문자 `docs/` | DL45 |
| F38 | 호스트는 빌드된 ABI major를 요청, 스파이크는 major 0 | DL29 |
| F39 | M0 환경 준비 (Python venv, 디버거, UE 소스 빌드 점검) | Q7, Q13, Q17, [02_Architecture.md](02_Architecture.md) §4.7, [03_WorkPlan.md](03_WorkPlan.md) M0 |
| F40 | 뷰어에서 PhysX UE4Compat 계열 나란히 보기 | DL14, Q18 |
| F41 | Kit 기본 virtual 구현의 매개변수 이름 제거, 사소한 백엔드 인스턴스화 | DL04 |
