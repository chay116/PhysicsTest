# 03. 작업 계획 (Work Plan)

> **작성일:** 2026-10-21
> **상태:** 설계 초안 v1.0 (구현 전)
> **관련 문서:** [01_Feasibility.md](01_Feasibility.md) · [02_Architecture.md](02_Architecture.md) · [04_DecisionLog.md](04_DecisionLog.md) · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)
> **근거 표기:** **[V]** 로컬 소스를 읽었거나 직접 실행해 확인 · **[M]** UE 4.18 제3자 미러에서 온 내용 (미확인, 한 줄도 복사하지 않음, 사용자가 4.27로 확인할 대상) · **[R]** 기억이나 추정 (미검증)

이 문서는 빈 저장소에서 Phase-1 캠페인과 Chaos 연결까지 가는 마일스톤 계획이다. 구조와 규약은 [02_Architecture.md](02_Architecture.md), 결정 근거는 [04_DecisionLog.md](04_DecisionLog.md), 헤더 초안은 [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)를 따른다. 이 문서는 "무엇을 어떤 순서로 만들고, 언제 끝났다고 판정하는가"만 다룬다.

---

## 0. 읽는 법

**담당**
- **H:** 하네스 개발자. 지금 바로 진행할 수 있다.
- **U:** 사용자. Chaos 백엔드 구현 전부, 시스템 설정 변경(전원 계획), UE 4.27 소스 대조, 라이선스와 공개 결정을 맡는다.

**공수**
- pd(person-day)는 한 사람의 집중 작업일이다. 빌드와 측정에 드는 기계 시간은 따로 적는다.
- 마일스톤마다 작업별 pd의 합은 요약 표의 공수와 같다.

**완료 기준 규칙**
- 완료 기준은 그 마일스톤 자신과 선행 열에 있는 마일스톤(전이적으로 포함)의 산출물만 쓴다.
- 뒤 마일스톤에서 만들 씬, 게이트, 도구는 완료 기준에 쓰지 않는다. 이전 초안은 이 규칙을 어겨서 M2~M5를 닫을 수 없었다. §4에서 마일스톤마다 점검한다.
- 체크포인트 A는 마일스톤이 아니라 M4와 M5를 함께 보는 검토 지점이다. 두 마일스톤을 결합한 확인(뷰어로 3엔진 보기)은 여기서만 한다.
- 예외는 두 곳이며 둘 다 명시한다. M13은 완료 기준을 선행 범위 안의 (a)와, 뒤 마일스톤(M4, M9, M10, M11)에 의존하는 (b)(c)로 나누고 의존 대상을 적는다. 조건부 M14는 착수 시점에 이미 끝난 마일스톤에 따라 검사 범위가 달라지므로 조건문으로 적는다.

**ID 표기**
- 씬 ID는 항상 `ID.Name`으로 적는다 (예: `M3.BoxPile`). 마일스톤 번호 M0~M15와는 관계없다.
- 게이트 접두어는 A(ABI), G(물리·측정), SP(Chaos 스파이크), DET(결정성)다.

**단계 태그**
- **[MVP]:** M0~M6. SDK 3종 백엔드, 뷰어 1단계, 물리 게이트, ABI 핵심 v1.0 동결까지다. 이 시점에 사용자가 Chaos 백엔드를 시작할 수 있다. M2.5 스파이크는 이 기간에 병행하는 선택 항목이라 [MVP, 선택]으로 적는다.
- **[확장]:** M7~M15. 측정 프로토콜, 오케스트레이션, 측정 엄밀성, 캠페인, 뷰어 2단계, Chaos 백엔드(M13, U), OOP 전송(M14, 조건부), 백로그(M15)다. 항목마다 어느 마일스톤인지 적는다 (§5.3). "선택 사항"이라는 뜻이 아니다. 헤드라인에 필요한 M7, M8, M10, M11도 여기에 속한다.

**01_Feasibility.md와의 대응**
- 스파이크는 **M2.5**다 (01 §9도 이 번호로 갱신했다). 스파이크 DLL은 C ABI v0.9와 BackendKit 위에서 만들어야 결과를 ABI에 반영할 수 있으므로 M2 뒤로 옮겼다. 타임박스는 3 달력일이지만 사용자 공수는 약 2 pd로, 01의 "1~2일"과 같은 규모다.
- Z-up, cm 기준 씬을 세 백엔드에 통과시키는 일은 M3(Jolt)과 M5(Bullet, PhysX)에서 한다 (01 §9와 같다).
- 01 §1의 권장 진행 방식(SDK 3종과 뷰어 먼저, 스파이크는 일찍, 스파이크 결과로 C ABI를 확정한 뒤 Chaos)은 그대로다.

---

## 1. 요약

### 1.1 마일스톤 표

| M | 이름 | 담당 | 공수(pd) | 선행 |
|---|---|---|---|---|
| M0 | 스캐폴드·툴체인·SDK 연결·환경 준비 | H | 5 | - |
| M1 | PhysicsCore 데이터 모델 (FBodyInstance 등, BodyUtils, FStepPlanner, 프로필 최소) | H | 4 | M0 |
| M2 | C ABI v0.9 + BackendKit + Null 백엔드 + 호스트 배관 (FPhysScene→ABI) | H | 4 | M1 |
| M3 | Jolt 수직 슬라이스 + 최소 벤치 exe (S00, M1.SingleBoxDrop, M3.BoxPile, timings.csv/manifest 기본) | H | 4 | M2 |
| M4 | 뷰어 1단계 (Chaos Debug Draw 스타일, 단일 씬, 엔진 전환, 일시정지/스텝, Stats) | H | 5 | M3 |
| M5 | Bullet + PhysX(UE4Compat/SdkDefault/-TGS/-Stab) 백엔드 + BackendCommon 테스트 | H | 9 | M3 (1인 기준 M4 다음) |
| ★ | 체크포인트 A (누적 약 31 pd): 3엔진을 뷰어로 확인, M3.BoxPile 1k/5k T=1 비공식 수치 | | | |
| M6 | 물리 게이트 G1–G7·G9·G12 + 게이트 씬 + ABI 핵심 v1.0 동결 + ChaosBackendGuide 초안 | H | 4 | M5 |
| M2.5 | Chaos hello-DLL 스파이크 (선택, 3 달력일 타임박스) | H 1 + U 약 2 | 3 | M2 (M5–M6과 병행) |
| M7 | 벤치 러너 + 측정 프로토콜 (호스트 제어, recorder, manifest, effective 덤프, 성능 씬, T=1..6, DET1, test-avx2 프리셋) | H | 5 | M6 |
| M8 | 오케스트레이션 + 분석 기본 (ptbench: 매트릭스, 셔플, fresh process, 반복, CV 보고, load/validate/perf/report) | H | 4 | M7 |
| M9 | 거동 씬 + ball/hinge 조인트(3엔진) + 프로필 완성 + 거동 분석 | H | 6 | M6, M8 |
| M10 | 측정 엄밀성 확장 (TSC, ThreadCensus/cpu_eff, CV 게이트+sentinel, 스탬프, comparability key·주장 규칙·bootstrap CI, test-<engine>, prof) | H | 5 | M8 |
| M11 | Phase-1 캠페인 + 비공개 리포트 | H 3 + 기계 약 6h (U: 전원 계획 전환) | 3 | M9, M10 |
| M12 | 뷰어 2단계 (SideBySide, GhostOverlay, Replay, Profiler) | H | 3 | M4, M7 |
| M13 | Chaos 백엔드 (사용자 구현, 동결 ABI 위) + 하네스 지원 | U 6–10 + H 2 | - | M6, M7, M8 (재캠페인은 M11 이후) |
| M14 | OOP 전송 (조건부: SP2/SP3 실패 또는 요청 시) | H | 4 | M2.5 결과 |
| M15 | 확장 백로그 | H | 약 20 | M11 |

### 1.2 단계 구분

| 단계 | 마일스톤 | 끝났을 때의 상태 |
|---|---|---|
| [MVP] | M0~M6 | 세 SDK가 같은 cm·Z-up 씬을 C ABI로 돌리고, 뷰어로 보이며, 물리 게이트를 통과한다. ABI 핵심 v1.0이 동결되어 Chaos 작업을 시작할 수 있다 |
| [확장] | M7~M12 | 측정 규약을 지킨 헤드라인 수치, 거동 리포트, 뷰어 비교·리플레이 |
| 사용자 | M2.5 (선택), M13 | Chaos 연결 경로 실증, Chaos 백엔드 |
| 조건부 | M14 | in-process Chaos가 실패할 때만 만드는 OOP 전송 |
| 백로그 | M15 | M11 결과를 보고 우선순위를 정한다 |

### 1.3 합계와 일정

| 항목 | 값 |
|---|---|
| 하네스 M0~M12 (M2.5의 H 1 pd 포함) | 약 62 pd |
| M13 하네스 지원 | 2 pd (합계 약 64 pd) |
| 20% 버퍼 포함 | 약 77 pd, 1인 기준 약 15~16주 |
| 체크포인트 A | 누적 약 31 pd (버퍼 포함 약 7~8주차) |
| 첫 신뢰 가능 3엔진 헤드라인 (M11 성능 부분) | 직렬 약 52 pd (버퍼 포함 약 12~13주차) |
| 거동 리포트까지 | 직렬 약 58 pd (버퍼 포함 약 14주차) |
| 사용자 | M2.5 약 2 pd, M13 6~10 pd |
| 조건부 | M14 4 pd |
| 기계 시간 | M11 약 6 h (노이즈가 크면 최대 2배) |

- 산식: M0~M12의 H 공수는 5+4+4+4+5+9+4+5+4+6+5+3+3 = 61 pd이고, M2.5의 H 1 pd를 더해 62 pd다. 1주 = 5 pd로 환산했다.
- 이전 초안의 52.5 pd는 리뷰에서 과소 추정으로 지적되었다 (현실적 범위 75~90 pd). 이 계획은 범위를 [확장]과 백로그로 덜어 내고 버퍼를 명시해 77 pd로 맞췄다.

### 1.4 Critical path (1인 직렬 합)

critical path는 한 사람이 순서대로 할 때의 직렬 합으로 적는다. 병렬 가능성으로 줄이지 않는다.

```
첫 신뢰 가능 3엔진 헤드라인 (M11 성능 부분, 뷰어 포함):
  M0(5) → M1(4) → M2(4) → M3(4) → M4(5) → M5(9) → M6(4) → M7(5) → M8(4) → M10(5) → M11(3)  = 52 pd
거동 리포트까지:
  위 경로 + M9(6)                                                                       = 58 pd
Chaos 행까지:
  M6(ABI 핵심 동결) → M13(U 6~10 pd) → M11 재실행
```

- M4(뷰어)는 헤드라인의 논리적 선행이 아니지만, 사용자가 Chaos 전에 세 엔진을 눈으로 보기를 원하므로 1인 순서에서 M5 앞에 둔다. 그래서 직렬 합에 넣었다.
- M11의 성능 작업(§3 M11 작업 1~6)은 M10까지만 필요하다. 거동 작업(작업 7)만 M9가 필요하다. 표의 선행 "M9, M10"은 M11 전체 기준이다.
- M2.5(선택, U)와 M12는 경로 밖이다. M2.5의 H 몫 1 pd는 하네스 합계 62 pd에는 들어가지만 이 직렬 합에는 넣지 않았다. §1.5 권장대로 M5 전에 하면 1인 직렬 합은 1 pd 늘어난다.

### 1.5 권장 실행 순서

- **1인 기본 순서:** M0 → M1 → M2 → M3 → M4 → M5 → 체크포인트 A → M6 → M7 → M8 → M10 → M9 → M11 → M12.
  - 성능 헤드라인을 먼저 보고 싶으면 M10 직후 M11의 작업 1~6을 먼저 돌리고, M9 뒤에 작업 7~8을 한다.
- **M2.5:** U가 M5~M6 기간에 병행한다. H 몫(1 pd)은 M2 이후 아무 때나 할 수 있고, M5 착수 전에 끝내 두는 것을 권장한다.
- **M12:** M7 이후 아무 때나 가능하다. 1인이면 M11 뒤에 한다.
- **2인이면:** M5는 M3만 필요하므로 M4와 병행할 수 있다. 이 경우 체크포인트 A는 둘 다 끝난 시점이다.
- **M13:** U는 M6(ABI 핵심 동결)부터 시작할 수 있다. 조인트·스냅샷·네이티브 드로는 M9에서 잠정 표시가 풀린 뒤 구현한다.

### 1.6 체크포인트 A 정의

**시점:** M4와 M5가 모두 끝난 직후. 누적 약 31 pd.

**목적:** 단위·규약 버그를 게이트 작업 전에 드러내고, 실제 타이밍 규모를 처음으로 본다. 다음 일정을 다시 추정하는 지점이다.

**통과 조건 (모두 충족)**
1. 뷰어 Single 레이아웃에서 Jolt, Bullet, PhysX `UE4Compat`/`UE4Compat-TGS`/`UE4Compat-Stab`/`SdkDefault`를 엔진 전환으로 하나씩 띄운다. `M1.SingleBoxDrop`과 `M3.BoxPile` 1k에서 M4의 시각 체크리스트를 통과한다. `UE4Compat` 계열을 띄운 뒤 `SdkDefault`로 바꿀 때(또는 그 반대)는 "재시작 필요"가 표시되고 재시작 후 동작한다. `UE4Compat`, `-TGS`, `-Stab` 사이 전환은 재시작 없이 된다 (PxPhysics와 PxTolerancesScale은 프로세스당 하나, PxPhysics.h:997, PxSceneDesc.h:541 [V]).
2. 세 엔진 모두 `S00.FreeFall`에서 |z60 − 501.83328| ≤ 2e-3 cm, `M1.SingleBoxDrop` 정지 접촉의 normal·(+Z) > 0.999, 질량 readback이 하네스 값과 1e-5 상대오차 이내다.
3. `M3.BoxPile` 1k/5k, T=1, Normalized에서 엔진마다 fresh process 5회의 `T_frame` 중앙값(QPC)을 CSV로 남긴다. 표기는 "비공식 (게이트 전, 전원 계획 미확인)"이다. 외부에 보고하지 않는다.
4. 발견한 규약 버그 목록과 처리 결과를 `docs/Checkpoints.md`에 적는다. 원시 수치는 `Saved/` 아래에만 두고 커밋하지 않는다.

**판단:** 실적이 계획 대비 1.2배를 넘으면 §6의 일정 레버(범위 축소)를 적용하고 M6 이후를 다시 추정한다. M2.5를 아직 하지 않았으면 이 시점에 U와 일정을 확정한다.

---

## 2. 하네스(H)와 사용자(U) 작업 구분

| 구분 | 내용 | 시점 | 비고 |
|---|---|---|---|
| H, 지금 | M0~M12 전체 | 즉시 | 사용자 결정은 기본값으로 진행하고, 바뀌면 ini나 플래그만 바꾼다 |
| H, Chaos 준비물 | `Unreal/ChaosBridgeHost` 골격(uproject, Target.cs, Build.cs, TODO가 달린 `PtChaosModule.cpp`), `PtChaosUbt.cmake`, `Tools/Chaos/build_chaos.cmd`, `.rsp` 파서 (M2.5); DLL 로더와 Null 템플릿 (M2); `docs/ChaosBackendGuide.md` 초안 (M6); 적합성 스위트 (M6, M7, M10) | M2 이후 | U가 시작할 때 하네스 쪽에서 막히지 않게 한다 |
| U, 선택적 조기 | M2.5 스파이크 (약 2 pd): InitGamePhys 순서, 구 1개짜리 FChaosScene, Development·Test 빌드 | M5~M6 기간 | 전제: UE 5.8.3 소스 트리 (확인됨, §3 M0), D: 여유 10 GB 이상 (현재 348 GB [V]) |
| U, 나중 | M13 Chaos 백엔드 본체 (6~10 pd) | 핵심은 M6 이후, 조인트·스냅샷·네이티브 드로는 M9 이후 | 동결된 ABI 위에서 구현한다 |
| U, 시스템 설정 | 벤치 세션 동안 전원 계획을 High performance 또는 Ultimate로 전환 (01 Q9) | M8 smoke부터 권장, M11 필수 | 러너는 읽어서 기록만 하고 바꾸지 않는다 |
| U, 확인 | UE 4.27 소스로 [M] 키 대조 (01 Q10): filter word 배치, contact offset clamp 공식, 반복 8/1, UseDelta pre-clamp, D6 actor swap, 정지마찰 = 동적마찰 | M11 전 권장 | 모든 [M] 값은 증거 태그가 붙은 ini 키라서 코드 변경 없이 바꾼다. M10 이후에 바꾸면 해당 변형의 스탬프만 다시 만든다 |
| U, 결정 | 저장소·결과 공개 여부와 UE EULA 확인 (01 Q1) | 공개 전 | 기본값은 비공개. M11 리포트도 비공개다 |
| U, 결정 | 디버거 선택 (01 Q7) | M0 | VS Code + C/C++(cppvsdbg), VS 2022 IDE, 이미 설치된 CLion 중 하나 |
| U, 결정 | 질량 규칙 기본값 (Q4), UEParity restitution 임계 매핑 (Q11), 하네스 /fp:fast 유지 (Q12), MatchedAVX2 Chaos 빌드 (Q15), Jolt 임펄스 로컬 패치 (Q8) | M9·M11 전 | 기본값은 [04_DecisionLog.md](04_DecisionLog.md)의 미결 질문 기본값을 따른다 |
| H + U | Chaos 포함 재캠페인 | M13 이후 | M13의 H 2 pd에 포함 |

---

## 3. 마일스톤 상세

### M0. 스캐폴드·툴체인·SDK 연결·환경 준비 [MVP] (H, 5 pd, 선행 -)

**목표**
- 빈 저장소에서 프리셋 하나로 SDK 3종, imgui/implot/stb/doctest/nlohmann_json, 하네스 Core가 한 Ninja 트리로 빌드된다.
- 참조 체크아웃(`D:\PhysicsRefs`)은 읽기 전용으로 둔다.
- 빌드 플래그를 빌드 산출물로 자기보고한다.
- 디버거와 Python 환경을 갖춰 이후 마일스톤이 환경 때문에 멈추지 않게 한다.

**환경 전제 조건 (2026-10-21 확인)**

| 항목 | 현재 상태 | M0 조치 | 담당 |
|---|---|---|---|
| MSVC | VS 2022 BuildTools, MSVC 14.44.35207 [V]. VS IDE는 없다 [V] | 없음 | - |
| Windows SDK | 10.0.26100.0 [V] | 없음 | - |
| CMake / Ninja | 4.1.2 / 1.13.2, `%USERPROFILE%\.cmake-deps` 아래 [V] | `Tools/cmake.cmd`가 vcvars64와 PATH를 설정한다 | H |
| Python | 3.11.9 [V]. numpy·pandas 등은 없다 (`ModuleNotFoundError: No module named 'numpy'` [V]) | `.venv`에 `Tools/ptbench/requirements.lock`(numpy, pandas, pyarrow, scipy, matplotlib, jsonschema)의 고정 버전을 설치한다. 오프라인 재설치용 wheelhouse는 `Tools/ptbench/wheelhouse/`(git 무시)에 둔다 | H |
| 디버거 | VS IDE 없음, VS Code 없음 [V]. CLion 2026.1과 Rider 2026.1은 설치되어 있다 [V] | VS Code + C/C++ 확장(cppvsdbg) 설치, VS 2022 IDE 설치, 또는 CLion을 Ninja 프리셋과 MSVC 툴체인으로 사용. CLion의 MSVC 디버깅 품질은 [R] | U 결정, H 설정 |
| UE 5.8.3 소스 | `Engine/Build/Build.version` 5.8.3, `Build.bat`, UBT 바이너리(`UnrealBuildTool.dll`), `.uedependencies` 있음 [V]. Program 타깃 빌드 자체는 아직 돌려 보지 않았다 | M2.5 전에 소스 빌드 가능 상태를 점검한다: `Build.bat`로 UBT가 실행되는지(예: `-help`) 확인하고 결과를 README에 적는다. 엔진 트리는 수정하지 않는다 | U |
| 디스크 | D: 여유 348 GB [V] | M2.5는 10 GB 이상이면 충분하다 | - |
| 참조 저장소 | `D:\PhysicsRefs` (JoltPhysics e77f1755, PhysX da950a35, bullet3 2c204c49) [V] | `file:///` URL로 clone만 한다. 그 안에서 빌드하지 않는다 | H |
| LongPathsEnabled | 0 [V] | 시스템 설정은 바꾸지 않는다. 빌드 루트를 `out/<preset>`처럼 짧게 둔다 | - |
| 전원 계획 | Balanced (01 §6) [V] | M0에서는 그대로 둔다. 벤치 세션에서 U가 바꾼다 | U |
| 의존성 tarball | 스크래치에 imgui/implot/stb/doctest/json/tracy tarball이 있다. imgui·implot·stb의 SHA256은 프로토타입의 URL_HASH와 같다 [V] | 오프라인 빌드용 캐시로 저장소 밖에 복사할 수 있다 (§7) | H |

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | 저장소 기본 파일: `.gitignore`, `.gitattributes`, `README.md`, `THIRD_PARTY_NOTICES.md`. 문서 디렉터리는 소문자 `docs/` 하나만 쓴다 | 0.25 |
| 2 | 환경 준비: `Tools/cmake.cmd`, `.venv`와 wheelhouse, 디버거 설정, UE 소스 점검 결과를 README 환경 절에 기록 | 0.75 |
| 3 | 프리셋과 툴체인 모듈: `CMakePresets.json`(dbg, dev, test), `CMakeUserPresets.json.example`, `PTOptions`, `PTToolchain`, `PTFlagProfiles`, `PTModules` | 0.75 |
| 4 | ThirdParty 모듈: `Jolt.cmake`, `PtBullet.cmake`, `PhysX.cmake` + `PT::PhysX`, `PhysXBootstrap.cmake`(대안 경로), `ImGui.cmake`, `SmallDeps.cmake`, SHA 가드 | 1.75 |
| 5 | 빌드 자기보고: `DumpBuildFlags.cmake`, `PTBuildInfo.h.in`, `PtBuildReport.h` | 0.5 |
| 6 | Core 최소 관용구, 하네스 측 third-party include 래퍼, `PhysicsTestAutomation` 골격, SDK 스모크 3종, CTest 프로브 | 1.0 |

작업별 세부:
- **1 기본 파일**
  - `.gitignore`: `out/`, `Saved/`, `.venv/`, `Tools/ptbench/wheelhouse/`, `Unreal/**/{Binaries,Intermediate,Saved,DerivedDataCache}`, `*.generated.h`, `*.gen.cpp`, `*.dll`, `*.pdb`, `CMakeUserPresets.json`.
  - `THIRD_PARTY_NOTICES.md`: 받아 오는 의존성 전부와 라이선스를 적는다. PhysX `physx/` Apache-2.0 (저장소 루트 LICENSE의 BSD-3과 다르다, 재배포 시 NOTICE 의무), Jolt MIT, Bullet zlib, Dear ImGui MIT, ImPlot MIT, stb MIT 또는 public domain, nlohmann/json MIT, doctest MIT, Tracy BSD-3 (prof 프리셋 전용, M10).
- **3 프리셋과 툴체인**
  - 프리셋은 dbg / dev / test 세 개만 둔다. test-avx2는 M7, prof와 test-<engine>은 M10에서 추가한다.
  - `binaryDir`은 `${sourceDir}/out/${presetName}`이다.
  - `PTToolchain`: `CMAKE_MSVC_RUNTIME_LIBRARY`를 SDK보다 먼저 /MD로, `/fp:fast`, `RelWithDebInfo`는 FATAL, 전역 `CMAKE_CXX_STANDARD`는 쓰지 않는다 (PhysX /std:c++14 /WX가 C5055로 깨짐 [V]). `pt_target_defaults()`가 타깃마다 cxx_std_20, /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor를 건다.
  - `CMakeUserPresets.json.example`: `PT_JOLT_GIT`, `PT_BULLET_GIT`, `PT_PHYSX_GIT`을 `file:///D:/PhysicsRefs/<repo>`로, `PT_UE_ROOT`를 `D:/UnrealEngine`으로 둔다.
- **4 ThirdParty**
  - `Jolt.cmake`: 프로토타입을 가져온다. `block()` 안의 옵션(static CRT, IPO, ALL_WARNINGS, INSTALL, DX12/VK/CPU_COMPUTE, DEBUG_RENDERER, PROFILER, FP_EXCEPTIONS를 끔)은 검증된 그대로 둔다. Ninja에서는 `/arch:AVX2`가 조용히 빠지므로(Jolt.cmake:969-979 [V]) PUBLIC으로 건다. 백엔드 lib는 Jolt를 PRIVATE로 링크해 플래그가 새지 않게 한다.
  - `PtBullet.cmake`: `FetchContent_Declare(bullet3 GIT_REPOSITORY ${PT_BULLET_GIT} GIT_TAG 2c204c49 GIT_CONFIG core.longpaths=true SOURCE_SUBDIR _none_)` + `FetchContent_MakeAvailable` 뒤에 `add_subdirectory(${bullet3_SOURCE_DIR} ... EXCLUDE_FROM_ALL SYSTEM)`를 하고, 그 앞뒤로 `CMAKE_POLICY_VERSION_MINIMUM 3.10`을 둔다. 단일 인자 `FetchContent_Populate`는 CMP0169 때문에 쓰지 않는다. 옵션은 전부 CACHE FORCE로 준다 (Bullet 안에서 CMP0077이 OLD [V]). `BT_THREADSAFE`는 끈다 (프로토타입 기본값 ON을 바꾼다).
  - `PhysX.cmake`: FetchContent `SOURCE_SUBDIR physx`, da950a35 고정, Ninja 전용. `PxConfig.h`는 `out/<preset>/_deps`의 clone에만 쓰인다. `PT::PhysX` = `physx_lib` (physx/CMakeLists.txt:326 [V]) + `PX_PHYSX_STATIC_LIB` + `DISABLE_CUDA_PHYSX`. PvdSDK는 PhysX와 PhysXExtensions의 링크 의존성이라 Debug 전용으로 뺄 수 없다 (physx/source/compiler/cmake/PhysX.cmake:397-400 [V]. M0에서 "Debug에서만 링크"를 정정). release에서는 `PX_SUPPORT_PVD=0`으로 코드가 빠진다. 01의 통합 Ninja 빌드(95.4초)가 이 경로였다 [V].
    - 이 경로를 주 경로로 두는 근거: PhysX가 부모의 구성 목록과 `CMAKE_CXX_FLAGS_<CONFIG>`를 CACHE FORCE로 덮어쓰는 블록은 `if(CMAKE_CONFIGURATION_TYPES)` 안에 있어 multi-config(VS) 생성기에서만 돈다 (physx/buildtools/cmake/PhysXConfigurationTypes.cmake:12-36 [V]). 하위 프로젝트일 때는 부모가 정한 `CMAKE_MSVC_RUNTIME_LIBRARY`를 존중한다 (physx/CMakeLists.txt:131-134 [V]). build-physx 연구가 bootstrap을 권한 이유(설정 덮어쓰기)는 VS 생성기에서 관찰한 것이다. 완료 기준 4가 Ninja에서도 이를 실측으로 확인한다.
  - `PhysXBootstrap.cmake`: 대안 경로다. PhysX를 configure 시점에 별도 트리로 빌드하고 INTERFACE 타깃으로만 소비하므로 생성기와 무관하게 부모 설정을 건드리지 않는다. VS 생성기를 쓸 때와 완료 기준 4가 실패할 때(§6 R6)만 쓴다. 프로토타입에서 `RelWithDebInfo → profile` 매핑을 지우고 (RelWithDebInfo는 거부), debug 라이브러리가 없으면 release로 떨어지지 않고 실패하게 고친다.
  - `ImGui.cmake`, `SmallDeps.cmake`: imgui v1.92.9b-docking, implot v1.0, stb a7d4c217, nlohmann_json v3.12.0, doctest v2.5.3을 URL + URL_HASH SHA256으로 받는다. `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`는 정의하지 않는다 (implot이 깨짐 [V]).
  - git으로 받는 SDK 셋은 `GIT_TAG`에 커밋 SHA를 쓰고 (태그 이름이나 `GIT_SHALLOW`는 쓰지 않는다), configure 때 `git rev-parse HEAD`가 고정 SHA와 같은지 검사한다. 고정 SHA 전체 값 [V]: Jolt `e77f175595e64cb44218cc9d9d56fc365ad0e36a` (v5.6.0), PhysX `da950a3537927784951853c66618036f332ca0ce` (ovphysx-0.6.3), Bullet `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5` (3.25).
- **5 빌드 자기보고**
  - `DumpBuildFlags`는 configure 스크립트가 아니라 빌드 단계의 `add_custom_command(OUTPUT BuildFlags.json BuildFlagsSha.h DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json)`이다. `compile_commands.json`은 generate 단계에서 써지므로 configure 시점에는 읽을 수 없다.
  - SDK 내부 설정의 근거는 SDK 자신의 TU에 대한 `compile_commands.json` 항목과 선택된 PhysX config(debug/checked/profile/release)다. `PX_SUPPORT_PVD`는 PhysX 타깃의 PRIVATE 정의라서 어댑터 TU에서는 보이지 않는다 (physx/source/compiler/cmake/windows/CMakeLists.txt:109-112 [V]).
  - TU 자기보고(`PtBuildReport.h`)는 어댑터 TU에서 확인할 수 있는 사실만 담는다: `__AVX2__`, `_M_FP_FAST`, `_MSC_FULL_VER`, `_DLL`, Jolt PUBLIC 정의(`JPH_*`).
- **6 Core와 테스트 골격**
  - Core는 UE 관용구의 최소 구현이다. 전체 복제가 아니다: `FVector`/`FQuat`/`FRotator`/`FTransform`(double, UE 합성 순서), `UE_LOG`, `check`/`verify`/`ensure`, `FCommandLine`/`FParse`(필요한 만큼), `FPlatformTime`(QPC. MSVC의 `std::chrono::steady_clock`이 QPC를 감싼다 [V]), FNV-1a/SHA-256, `FRandomStream`. `FConfigFile`과 `IConsoleManager`는 M1에서 만든다.
  - 하네스 측 `PT_THIRD_PARTY_INCLUDES_START/END`: 경고 push/pop에 더해 `#pragma push_macro("check")` + `#undef check`를 하고(`verify`, `ensure`도 같게) 끝에서 pop한다. doctest v2.5.3은 `check()`라는 멤버 함수를 선언한다 (doctest.h:1647 [V]). doctest, ImGui, ImPlot, nlohmann은 반드시 이 래퍼를 거쳐 include한다.
  - SDK 스모크 3종: 네이티브 SI, Z-up, g = 9.80, damping 0, sleep 끔. 프로토타입 hello 소스를 옮긴다.
  - CTest 프로브: Core를 먼저 include한 뒤 래퍼를 거쳐 doctest, ImGui, ImPlot, nlohmann/json을 include하는 TU 컴파일, SDK 플래그 누수 프로브(프로토타입 seam), 참조 저장소 `git status`, 가장 긴 오브젝트 경로.

**완료 기준**
1. 빈 `out/`에서 file:// 미러로 `Tools\cmake.cmd --preset test` + `cmake --build --preset test`가 4분 안에 끝난다. 다시 빌드하면 `ninja: no work to do`가 나온다. dbg, dev 프리셋도 빌드된다.
2. `git -C D:\PhysicsRefs\{JoltPhysics,PhysX,bullet3} status --porcelain` 세 결과가 모두 비어 있다. 참조 트리에 `PxConfig.h`, `.dxil`, `.spv`가 새로 생기지 않는다.
3. 빌드 시점에 만들어진 `BuildFlags.json`에서:
   - Shipped: Jolt TU와 Jolt를 링크한 어댑터 TU만 `/arch:AVX2`, PhysX·Bullet·하네스는 `/arch` 없음.
   - 모든 TU가 `/fp:fast`와 `-MD`(dbg는 `-MDd`)이고 `-MT`는 하나도 없다. PhysX TU는 `/std:c++14`를 유지한다.
   - `compile_commands.json`을 바꾸면 `BuildFlags.json`이 다시 만들어진다.
4. PhysX를 켜고 끈 두 빌드에서 하네스·Jolt·Bullet TU의 플래그와 CRT가 같다. dbg, dev, test 세 프리셋 모두에서 비교한다. 다르면 `PhysXBootstrap` 경로로 바꾸고 이 항목을 다시 확인한다 (§6 R6).
5. 플래그 누수 프로브: Jolt를 PRIVATE로 링크한 정적 lib의 소비자 TU에서 `__AVX2__`와 `JPH_*`가 정의되지 않는다.
6. 스모크: 세 SDK 모두 z(60) = 5.0183328 m ±2e-5 m (z0 = 10 m, g = 9.80, dt = (float)(1/60). 허용치는 G1의 2e-3 cm와 같다).
7. Jolt TU 안에서 본 `JPH::GetConfigurationString()`에 AVX2가 있고 Profile / Debug Renderer / FP Exceptions가 없다.
8. 고정 SHA와 다른 태그를 주면 configure가 FATAL로 멈춘다. 가장 긴 오브젝트 경로가 240자 미만이다.
9. CTest 프로브 "Core → 래퍼 → doctest·ImGui·ImPlot·nlohmann/json" 컴파일이 통과한다.
10. `.venv`에서 `python -I -c "import numpy, pandas, pyarrow, scipy, matplotlib, jsonschema"`가 성공하고, `pip freeze`가 lock 파일과 같다. wheelhouse를 만들었으면 `pip install --no-index --find-links Tools/ptbench/wheelhouse`로 네트워크 없이 다시 설치된다.
11. 선택한 디버거로 dbg 프리셋 스모크 exe에 중단점을 걸고 멈출 수 있다.
12. `THIRD_PARTY_NOTICES.md`에 위의 9개 항목이 모두 있다.

**산출물:** 루트 `CMakeLists.txt`, `CMakePresets.json`, `CMakeUserPresets.json.example`, `cmake/*.cmake`, `cmake/ThirdParty/*.cmake`, `Tools/cmake.cmd`, `Tools/ptbench/requirements.lock`, `Source/Runtime/Core`(최소), `Source/Programs/PhysicsTestAutomation`(골격), SDK 스모크 테스트, `README.md`, `THIRD_PARTY_NOTICES.md`.

**선행:** 없음.

---

### M1. PhysicsCore 데이터 모델 [MVP] (H, 4 pd, 선행 M0)

**목표:** UE 타입·필드·기본값을 가진 데이터 모델, 하네스가 계산하는 질량 규칙, 모든 tick mode의 FStepPlanner, 최소 프로필 리졸버를 단위 테스트로 고정한다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | 타입: `FKShapeElem`, `FKSphereElem`, `FKBoxElem`, `FKSphylElem`, `FKConvexElem`(데이터만), `FKAggregateGeom`, `FBodySetup`, `FPhysicalMaterial`, `FBodyInstance`, `FConstraintInstance`(ball/hinge 데이터만), `FBodyInstanceState`, `EObjectStateType`, `ECollisionChannel` + `FBodyCollisionFilter`, 설정 계층 `FPhysicsSettingsCore`/`FPhysicsSettings`/`SolverOptions`/`FPhysicsTestSettings` | 1.25 |
| 2 | `BodyUtils::ComputeMassProperties`: 해석적 프리미티브(구, 박스, 캡슐), 평행축 합성, Jacobi 대각화, UE 질량 규칙과 Physical 규칙 | 0.75 |
| 3 | `FStepPlanner`: FixedStep, Sync, Substepping(UE5ChaosScene, UE4PhysSubstep), AsyncFixed. UE의 산술을 UE의 형 그대로 재현 (`FChaosScene::SetUpForFrame`은 float, 솔버의 스텝 수·누산기는 FReal = double, UE4 substep은 float. M1에서 확인), MinDeltaTime 미만 나머지 소실, `dt_engine = (double)(float)dt` | 0.75 |
| 4 | 최소 프로필: `FConfigFile`(ini 리더), `IConsoleManager`/`TAutoConsoleVariable`(최소), `FProfileResolver`(Base → Profile → Backend variant → 커맨드라인), Origin 추적, 변형 키 제한, `-ini:`/`-ExecCmds`. `BasePhysicsTest.ini`, `Profiles/UEParity.ini`, `Profiles/Normalized.ini` 초안 | 1.0 |
| 5 | `FPhysScene` 골격: 포인터 안정 저장소(std::deque), 지연 액터 목록, 델리게이트(`OnPhysScenePreTick`/`OnPhysSceneStep`/`OnPhysScenePostTick`), 프레임 상태기계. 백엔드는 스텁 | 0.25 |

작업별 세부:
- **타입의 UE 명명:**
  - `FKBoxElem::Rotation`, `FKSphylElem::Rotation`은 UE처럼 `FRotator`로 둔다 (BoxElem.h:30-33, SphylElem.h:30-33 [V]). 원소에 `FQuat` 필드는 두지 않고, 호스트가 마샬링할 때 `GetTransform()`으로 바디당 한 번 FQuat로 바꾼다 (02 §5.2의 "마샬링 시 FQuat 캐시", 부록 A.0.4).
  - FBox 유효성 필드는 `IsValid`다.
  - `FBodyInstance` 기본값은 BodyInstance.cpp:357-418 [V], `bStartAwake`와 `bGyroscopicTorqueEnabled` 같은 코어 플래그는 BodyInstanceCore.cpp:13-24 [V]를 주석으로 인용한다.
  - `FPhysicalMaterial`은 `StaticFriction`과 `Friction`을 따로 그대로 보관한다. 쌍 단위 결합(정지마찰 max 규칙)은 백엔드 결합 콜백이 한다 (M2 `PtCombine`).
  - UE와 다르게 둔 곳은 [02_Architecture.md](02_Architecture.md)의 "UE 대응 차이" 표에 적는다.
- **충돌 채널:** 하네스는 32채널(`uint32` 마스크)을 의도적으로 쓴다. UE 5.8.3은 `ECC_MAX = 65`이고 마스크가 `uint64`다 (EngineTypes.h:1101-1176, CollisionFilterData.cpp:263-272 [V]). 32채널로 제한하는 이유는 UE4 PhysX word3의 5비트 채널과 Bullet의 int group/mask다. Chaos 브리지는 `uint64`로 넓힌다 (M13).
- **질량 규칙 순서** (BodyUtils.cpp:37-45 밀도, 53-72 질량 규칙, 74-79 관성 비율·COMNudge, 90-96 InertiaTensorScale, 98-112 COMNudge 관성 항, 124 대각화; BodyInstance.cpp:3490-3492 [V]):
  1. 밀도: 머티리얼이 없으면 1.0 kg/cm³, 있으면 max(9e-8, Density × 0.001) kg/cm³.
  2. 부피 적분, 평행축 합성.
  3. 대각화.
  4. 질량 규칙: `bOverrideMass`가 아니면 M' = MassScale × M^clamp(RaiseMassToPower, 1e-4, 1), 맞으면 M' = max(MassInKgOverride, 0.001). 하한 0.001 kg.
  5. 관성을 M'/M으로 스케일한다.
  6. COM += COMNudge.
  7. InertiaTensorScale을 `bInertiaScaleIncludeMass = false`로 적용한다.
  8. `p.ComNudgeAffectsInertia`이면 COMNudge 평행축 항을 더한다 (원본 주석이 ScaleInertia 뒤여야 한다고 적고 있다).
  - convex 사면체 적분과 quickhull은 M15다. convex는 데이터 모델과 ABI에만 있다.
- **FStepPlanner:** 모든 모드를 구현하고 테스트하지만, MVP 씬은 FixedStep과 Sync만 쓴다. AsyncFixed 결과에는 "Chaos에서는 GT 상태가 한 스텝 늦게 보간됨" 표시를 붙인다. Chaos의 고정 dt 누산기는 `IsUsingFixedDt() = IsUsingAsyncResults() && UseAsyncInterpolation`일 때만 돈다 (PhysicsSolverBase.h:712-720, PhysicsSolverBase.cpp:262-263, 591-605 [V]).
- **ini:** UE 기본값 키는 UE처럼 모두 `[/Script/Engine.PhysicsSettings]` 아래에 둔다 (BaseEngine.ini:3406 [V]. `UPhysicsSettings`가 `UPhysicsSettingsCore`를 상속한다). `[/Script/PhysicsCore.PhysicsSettingsCore]` 섹션은 만들지 않는다. ForcedOverrides와 preflight diff는 M9다.

**완료 기준** (`PhysicsTestAutomation`, `PhysicsTest.<Area>.<Name>`)
- BodyUtils:
  - 100 cm 정육면체(기본 머티리얼): 177.83 kg, 296,380 kg·cm².
  - 50 cm 정육면체: 37.384 kg. 10 cm 정육면체: 1 kg.
  - 머티리얼 없음: 밀도 1.0 kg/cm³가 적용된다 (10 cm 정육면체 → 원질량 1000 kg → 177.83 kg).
  - `bOverrideMass`에 0.0001을 주면 0.001 kg가 된다.
  - 단위가 아닌 InertiaTensorScale과 0이 아닌 COMNudge를 함께 준 사례가 위 순서로 손계산한 값과 1e-9 상대오차 이내다.
- FStepPlanner 고정값:

  | 입력 | 결과 |
  |---|---|
  | dt 0.05, Sync | 1 × 1/30 |
  | dt 0.05, UE5ChaosScene | 3 × 1/60 (ceil 입력 2.99999989) |
  | dt 0.05, UE4PhysSubstep | 2 × 1/60 |
  | dt 0.2, UE5ChaosScene | 6 × 1/60, 0.1 s 소실 |
  | AsyncFixed 1/30, 프레임 1/60 | 스텝 수 0, 1 교대 |
  | MinDeltaTime 미만 나머지 | 이월 없이 소실 |

- 해석 기준값 함수가 `dt_engine`으로 계산된다: `S00` 설정의 z60 = 501.8332813705 cm (정확한 1/60로 계산하면 501.8333333이며 5.2e-5 cm 차이가 난다).
- 프로필: 변형 ini가 고정 키를 바꾸면 로드 오류가 난다. 모든 값에 Origin이 붙는다. `-ini:`와 `-ExecCmds`가 동작한다. `BasePhysicsTest.ini`에 PhysicsCore 섹션이 없다.
- 수학: quat/transform 합성 순서와 FRotator → FQuat 변환이 UE 순서와 같다. FKBoxElem X/Y/Z는 full extent, FKSphylElem Length는 로컬 Z 선분 길이로 해석된다.
- 충돌 필터: 32채널 상호 차단 진리표가 통과한다.

**산출물:** `Source/Runtime/PhysicsCore/Public/{PhysicsEngine,PhysicalMaterials,Physics}`의 타입, `BodyUtils`, `StepPlanner`, `PhysicsProfile`, Core의 `ConfigCacheIni`/`IConsoleManager`, `Config/BasePhysicsTest.ini`, `Config/Profiles/{UEParity,Normalized}.ini`, 단위 테스트.

**선행:** M0.

---

### M2. C ABI v0.9 + BackendKit + Null 백엔드 + 호스트 배관 [MVP] (H, 4 pd, 선행 M1)

**목표:** 모든 엔진이 지나갈 pure-C 경계를 만들고, SDK 없는 Null 백엔드로 static과 DLL 두 경로를 끝까지 통과시킨다. `FPhysScene::Tick`이 실제로 ABI를 구동한다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `PtBackendAbi.h` v0.9 (C, POD, static_assert 크기, C11 컴파일 TU) | 0.5 |
| 2 | `PtBackendKit.h` (header-only) | 1.0 |
| 3 | BackendCommon 골격: `PtUnitScale`, `PtCombine`, `PtDamping`, `PtContactConvention`, 고정 폭 typedef | 0.25 |
| 4 | 호스트 측: `FPhysicsBackendRegistry`, `FPhysicsBackendModule`, `FPhysicsBackendScene`, `FPhysicsInterface`, `FPhysScene::Tick`의 두 frame driver, `FlushDeferredActors` | 1.0 |
| 5 | Null 백엔드 static + `PtBackend_Null.dll` | 0.5 |
| 6 | ABI 게이트 A0~A7, A9, A10(Null 프레임 모델 사례), `PtAbiUeCompatCheck`, `Tools/check_modules.py`, `docs/Abi.md` 초안 | 0.75 |

작업별 세부:
- **ABI 헤더**
  - `PT_ABI_VERSION_MAJOR`는 동결 전까지 0 (v0.9)이다. 호스트는 자신이 빌드된 major를 요청한다.
  - 동기 모드는 `AsyncDt = -1`로 넘긴다. 0은 Chaos에서 async를 켜므로 (PhysicsSolverBase.h:712-714 [V]) Kit가 `PT_E_INVALID_ARG`로 거부한다.
  - `PT_NUM_COLLISION_CHANNELS 32`의 주석에는 M1의 근거를 쓴다: UE 5.8.3은 64채널(`ECC_MAX = 65`, `uint64` 마스크)이고, 하네스는 UE4 PhysX word3과 Bullet int 마스크에 맞춰 의도적으로 32채널을 쓴다.
  - `CreateActors`의 의미는 UE 순서다: 액터 생성(생성 시점 플래그) → 셰이프·필터·머티리얼 → 배치 전체를 씬에 1회 추가 → 바디별 InitDynamicProperties를 UE 순서로 → `!bStartAwake`이면 sleep (BodyInstance.cpp:1669, 1695-1706, 4723-4733 [V]).
  - `CreateConstraints`와 조인트 구조체, `CaptureDebugSnapshot`/`ReadDebugSnapshot`과 스냅샷 구조체, 네이티브 드로 명령(`PtDebugDrawCmd`)은 처음부터 "M9까지 잠정"으로 표시한다.
  - `PtDebugDrawCmd`: 캡슐 Extent = (R, 반지름을 포함한 HalfHeight, 0), 박스 Extent = half extent다 (DebugDrawImplicitObject.cpp:62, 73-76 [V]).
- **Kit**
  - 썽크, 스레드 가드(`PT_E_WRONG_THREAD`, 부작용 없음), phase 상태기계와 모듈 상태기계, 16-slot 핸들 표, 입력 검증, 2회 호출 용량 프로토콜, `FTextWriter`, stride 뷰.
  - last-error와 로그 콜백 상태는 `TBackendApiProvider<T>` 안에 둔다. 전역 inline 상태를 두지 않는다.
  - Kit와 BackendCommon은 `namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { ... } }`로 감싸고, `PT_BACKEND_NS`는 백엔드 타깃마다 다르게 정의한다. /arch가 다른 백엔드끼리 COMDAT를 공유하는 ODR 위반을 막기 위해서다.
  - 기본 가상 함수 구현은 매개변수 이름을 비워 둔다. /W4 /WX에서 C4100이 오류가 되기 때문이다 (MSVC 14.44로 재현 [V]). `PtAbiUeCompatCheck`는 Kit를 쓰는 아주 작은 백엔드를 실제로 인스턴스화해 그 본문까지 컴파일한다.
- **BackendCommon `PtCombine`:** 머티리얼별 결합 규칙(Average/Min/Multiply/Max, 큰 모드 우선)을 구현한다. 정지마찰은 Chaos처럼 쌍마다 `max(combine(max(F0,SF0), max(F1,SF1)), combine(F0,F1))`이다. 머티리얼마다 max(F, SF)를 먼저 한 뒤 결합한다 (PBDCollisionConstraints.cpp:474-476, PBDCollisionConstraint.h:486 [V]). 쌍당 계수가 하나인 Jolt·Bullet 콜백은 동적 결합값 `combine(F0,F1)`만 돌려주고 `material.static_friction_rule=dynamic_only`로 기록한다 ([04_DecisionLog.md](04_DecisionLog.md) DL41).
- **호스트 측**
  - `FPhysicsBackendModule`: `LoadLibraryExW(abs, LOAD_WITH_ALTERED_SEARCH_PATH)`, 버전과 StructSize 확인, 로드 직후 호스트 예외 필터 재설치, FreeLibrary 안 함.
  - `FPhysicsInterface`: `_AssumesLocked` 동사 이름을 쓴다. `AddImpulse_AssumesLocked(Handle, Impulse, bIsInternal)`과 `AddVelocity_AssumesLocked`를 따로 둔다 (ChaosEngineInterface.h:465-467 [V]).
  - `FPhysScene::Tick`: Harness driver(스텝마다 호출)와 Native driver(프레임당 UE 인자 1회, `NumStepsTaken` 확인)를 둘 다 구현한다.
- **Null 백엔드:** double 정밀도 semi-implicit Euler와 UE damping. `PT_CAP_NATIVE_FRAME_MODEL`을 선언하고 FStepPlanner 의미(UE5 `FChaosScene`/`PhysicsSolverBase` 규칙: Sync clamp, Substepping, AsyncFixed 누산기)로 다중 스텝을 처리한다. 그래서 Chaos 전에 Native driver와 `NumStepsTaken` 계약을 시험할 수 있다. Null은 하네스 lib를 링크하지 않으므로 이 계획 코드는 하네스 `FStepPlanner`와 따로 작성한다. 그래서 A10은 독립된 두 구현의 대조가 된다. 필수 effective 키는 공통 집합과 백엔드별 집합으로 나누고, 백엔드별 집합은 `GetInfo`로 선언한다.
- **ABI 게이트 (MVP 범위)**
  - A0 lifecycle, A1 버전 핸드셰이크, A2 다른 스레드 호출, A3 phase 상태기계, A4 입력 검증, A5 용량·stride, A6 MXCSR·x87 보존, A7 필수 effective 키(공통 + 백엔드 선언 집합), A9 DLL 위생.
  - A10(프레임 모델)을 Null로 만든다: Sync, Substepping(UE5ChaosScene 규칙), AsyncFixed 고정 입력에 대해 `NumStepsTaken == Plan.N`이고, FixedStep과 Sync에서 Native driver와 강제 Harness driver의 최종 상태가 비트 단위로 같다. 사례는 M2에서 통과시키고, 게이트로서는 M6 동결 조건으로 다시 돌린다.
  - A4는 1만 개 fuzz 대신 표로 정의한 음성 사례 약 30개다 (1만 개 fuzz는 M15).
  - A8(loopback)은 `PtAbiCodec`과 함께 M14로 미룬다. M14가 열리지 않으면 M15다.

**완료 기준**
- ABI 헤더가 C11과 C++20 모두에서 /W4 /WX로 경고 없이 컴파일된다. Kit는 /GR- /EHs-c- -D_HAS_EXCEPTIONS=0 -DPT_ABI_NO_EXCEPTIONS에서 경고 0이며, `PtAbiUeCompatCheck`가 인스턴스화한 시험 백엔드도 포함한다.
- `PtBackend_Null.dll`의 export는 `PT_GetBackendApi` 하나이고 의존성은 CRT DLL과 KERNEL32뿐이다 (`check_modules.py`).
- Null이 static과 DLL 두 경로에서 A0~A7, A9를 통과한다:
  - 다른 스레드 호출은 상태 변화 없이 `PT_E_WRONG_THREAD`다.
  - 음성 사례 약 30개가 크래시 없이 전부 거부된다. `AsyncDt = 0`은 `PT_E_INVALID_ARG`다.
  - 호스트가 major 0을 요청하면 성공, major 1을 요청하면 NULL이다.
  - A10 사례 전부에서 `NumStepsTaken == Plan.N`이다.
- A10: FixedStep과 Sync에서 Harness driver와 Native driver로 돌린 Null 상태가 비트 단위로 같다.
- 자유낙하 픽스처(구 r 50, 1 kg, z0 1000 cm, damping 0, 60 프레임, `FPhysScene` API로 직접 구성한 테스트): z60 = 501.8332813705 cm ±1e-9. 기준은 `dt_engine`이다. 이 픽스처는 M3의 `S00.FreeFall` 씬과 M2.5 SP3가 그대로 다시 쓴다.
- Null과 시험용 두 번째 static 백엔드를 한 exe에 링크해도 last-error 상태가 섞이지 않고, 링커 맵에서 Kit 심볼이 백엔드별 inline namespace로 나뉜다.
- Null, 바디 0개, 프레임당 4호출의 ABI 비용을 QPC 배치 평균으로 기록한다. 게이트가 아니다 (TSC 기반 100 ns 게이트는 M10).

**산출물:** `Source/Runtime/PhysicsTestAbi`, `Source/Runtime/BackendCommon`(골격), PhysicsCore의 호스트 프록시, `Source/Backends/NullBackend`(+ DLL), `Source/Runtime/PhysicsTestConformance/Abi`, `Tools/check_modules.py`, `docs/Abi.md` 초안.

**선행:** M1.

---

### M3. Jolt 수직 슬라이스 + 최소 벤치 exe [MVP] (H, 4 pd, 선행 M2)

**목표:** 첫 실제 엔진(Jolt)을 데이터 모델 → ABI → 엔진 → 결과 파일까지 한 번에 관통시킨다. 첫 씬 세 개와 최소 벤치 exe를 만든다. 수치는 아직 비공식이다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `FPhysScene_Jolt` (Jolt 백엔드 본체) | 2.0 |
| 2 | Jolt 디버그 스냅샷 (viz 전용) | 0.5 |
| 3 | 씬 레지스트리 + 씬 3개 + `docs/Scenes.md` | 0.5 |
| 4 | 최소 벤치 exe: `PT::BenchCore`, `PT::Benchmark`(최소), `PhysicsTestBench.exe` | 1.0 |

작업별 세부:
- **Jolt 백엔드**
  - 단위는 m다 (cm 입력은 속도 clamp와 질량 폭주로 틀린 결과를 낸다 [V]). 질량은 `MassPropertiesOverride` + `MassAndInertiaProvided`로 넘긴다.
  - COM 래핑은 필요할 때만 한다. 요소가 하나이고 ComLocal이 사실상 항등이면 (|t| < 1e-6 cm, |q·I| > 1 − 1e-9) `BoxShape` 같은 네이티브 프리미티브를 그대로 쓴다. 그 밖에만 `OffsetCenterOfMassShape`나 `StaticCompoundShape`를 쓴다. 래핑 개수를 effective 키 `shape.wrapping`으로 기록한다.
  - 캡슐은 `RotatedTranslatedShape`로 Y → Z.
  - BP 레이어 2개 + UE 상호 차단 `GroupFilter`. 결합 람다는 `PtCombine`의 동적 결합값을 쓴다 (Jolt는 쌍당 마찰 계수가 하나라 정지마찰 규칙을 재현하지 못한다, DL41).
  - 바디 생성은 M2의 UE 순서를 따른다: 바디 생성(생성 시점 플래그) → 셰이프·필터·머티리얼 → `AddBodiesPrepare`/`AddBodiesFinalize`로 배치 추가 1회 → 동적 속성 → `!bStartAwake`이고 kinematic이 아니면 추가된 뒤 `BodyInterface::DeactivateBody`로 재운다.
  - Normalized에서는 sleep을 끄고 (바디마다 `BodyCreationSettings::mAllowSleeping = false`, BodyCreationSettings.h:106 [V], [04_DecisionLog.md](04_DecisionLog.md) DL48), 기본 damping 0.05 (BodyCreationSettings.h:109 [V])를 덮어쓴다. 선속도 clamp(500 m/s, BodyCreationSettings.h:111 [V])는 해제한다.
  - 스텝은 `PhysicsSystem::Update(dt, 1, FJoltTempAllocator, JobSystemThreadPool(T−1))`, 매 스텝 `EPhysicsUpdateError` 검사, `PrepareForSimulation`에서 `OptimizeBroadPhase`, T_pull은 `GetActiveBodiesUnsafe`(+ 직전 활성 바디와 명령으로 바뀐 바디). 구현은 `TempAllocatorImpl`(블록이 모자라면 abort) 대신 넘침을 오류 bit로 내는 하네스 할당기를, `GetActiveBodies`(벡터 복사 = T_pull 안의 할당) 대신 `GetActiveBodiesUnsafe`를 쓴다 ([Progress.md](Progress.md) M3 §3 #12, #16).
  - 임펄스·속도 변화·힘은 push 때 엔진에 넣지 않고 스텝 직전에 넣는다 (Chaos·Null과 같은 "다음 스텝 적분" 의미, [Abi.md](Abi.md) §7.1, Progress.md M3 §7).
  - 중력은 값이 바뀔 때만 설정한다.
  - `DumpEffectiveSettings`는 공통 키와 Jolt 키를 내보내고, 읽을 수 있는 값은 READBACK으로 표시한다. `GetInfo`의 BuildFlagsJson은 어댑터 TU 자기보고다.
- **Jolt 스냅샷:** viz 전용 contact listener(구현은 잡 스레드에서 잠금·할당 없는 atomic 슬롯 배열, Progress.md M3 §3 #13). `Normal = −mWorldSpaceNormal`, 점마다 `Phi = −(p1 − p2)·n`으로 Chaos 규약에 맞춘다 (M4에서 정정: 처음 문장의 `−mPenetrationDepth`는 매니폴드에서 가장 깊은 점의 값이라 점마다 다른 Chaos의 Phi와 맞지 않았다). 임펄스는 NaN, active 여부는 근사 플래그다. Jolt island ID는 internal API(`GetIslandIndexInternal`)뿐이라 쓰지 않고, 하네스 `FIslandBuilder`(M4)에 맡긴다.
- **씬:** `IMPLEMENT_PHYSICS_TEST_SCENARIO` 정적 등록, `WHOLE_ARCHIVE(PT::Scenarios)`, 시작 시 레지스트리가 비어 있지 않은지 assert.
  - `S00.FreeFall`: M2 자유낙하 픽스처와 같은 설정.
  - `M1.SingleBoxDrop` a/b: 100 cm 정육면체, 1000 kg 고정(bOverrideMass), 중심 z = 150 cm, b는 roll 30° 후 pitch 20°, μ 0.5, e 0, 300 프레임.
  - `M3.BoxPile(N)`: N = 1000 / 5000 / 10000, 100 cm 정육면체, 격자 10×10 / 25×20 / 40×25 × 10층, 간격 150 cm, yaw U(±15°), xy 흔들림 U(±5 cm), 바닥은 정적 박스, 600 프레임.
  - `docs/Scenes.md`를 만들고 `S00.FreeFall`을 카탈로그 확장으로 등록한다 (방법론 §5.1의 SI 단위 S00을 cm·g = 980으로 다시 정의).
- **최소 벤치 exe**
  - `PT::BenchCore`를 만든다: 클럭 버킷(QPC), 윈도 통계. M7에서 `FHostEnvironment`와 프로세스 스포너, M10에서 `FThreadCensus`와 TSC가 들어온다.
  - 타깃 그래프는 `PT::Conformance → PT::BenchCore`, `PT::Benchmark → PT::Conformance`다. 순환이 없다.
  - `FBenchRunSpec` 최소판: `-Backend`, `-BackendProfile`, `-Profile`, `-Scenario`, `-Param:`, `-Frames`, `-Threads`, `-Out`, `-List`, `-Describe`.
  - FixedStep 루프, `T_push`/`T_step`/`T_pull`/`T_frame` 버킷, frame 0 별도 기록.
  - 출력: `timings.csv`, `manifest.json`(기본 필드. 마지막 프레임 `h_native`는 determinism 필드에 둔다), `effective_settings.jsonl`. `hashes.csv`, `states.ptst`, `contacts.ptcn`은 M7 recorder다.
  - 기본값은 해시를 마지막 프레임에만 계산하고 `CountersEvery = 0`이다.
  - `desc_sha256`은 엔진과 무관한 필드만 덮는다: 머티리얼, 셰이프, 액터 생성 정보, convex 정점, 조인트, 중력, dt 시퀀스. `RunMode`, `InstrumentFlags`, `NumThreads`, `RestitutionThreshold`, 엔진 Options는 `options_sha256`이나 `physics_sha256`으로 간다.
  - 종료 코드 0 / 2 / 3 / 5 / 6.

**완료 기준**
- Jolt가 ABI를 통해 `S00.FreeFall`(Normalized)에서 |z60 − 501.83328| ≤ 2e-3 cm, |v60 + 980.00005| ≤ 1e-2 cm/s다.
- `PhysicsTestBench.exe`로 `M1.SingleBoxDrop` a/b와 `M3.BoxPile` 1k/5k를 UEParity와 Normalized에서 돌리면 `timings.csv`, `manifest.json`, `effective_settings.jsonl`이 나온다.
- `M3.BoxPile`과 `M1.SingleBoxDrop`에서 `shape.wrapping` = 0이다.
- 같은 씬·프로필에서 Null과 Jolt의 `desc_sha256`이 같다. 엔진 키만 다른 시험용 변형 두 개로 같은 씬을 만들어도 `desc_sha256`이 같다는 단위 테스트가 통과한다. `options_sha256`은 달라도 된다.
- Jolt가 static 경로에서 A0~A7을 통과한다.
- `M1.SingleBoxDrop` a의 정지 접촉 스냅샷: normal·(+Z) > 0.999, −2.5 cm ≤ Phi ≤ 0.5 cm. 하한은 Jolt의 침투 허용치 `mPenetrationSlop` 0.02 m (PhysicsSettings.h:53 [V])에 0.5 cm를 더한 값이다. Jolt 위치 솔버는 slop을 넘는 침투만 보정하므로 (ContactConstraintManager.cpp:1904-1906 [V]) 정지 접촉이 약 2 cm 침투한 채로 남을 수 있다.
- CTest: 하네스 TU에 SDK include 경로나 SDK 정의가 없고, Shipped에서 `__AVX2__`도 없다. `PhysicsTestBench.exe`가 d3d11.dll과 dxgi.dll을 import하지 않는다.
- `M3.BoxPile` 1k/5k의 첫 타이밍은 manifest에 "비공식"으로 기록한다.

**산출물:** `Source/Backends/JoltBackend`, `Source/Runtime/PhysicsTestScenarios`(S00, M1, M3), `Source/Runtime/PhysicsTestBenchmark`(최소)와 BenchCore, `Source/Programs/PhysicsTestBench`, `docs/Scenes.md`.

**선행:** M2.

---

### M4. 뷰어 1단계 [MVP] (H, 5 pd, 선행 M3)

**목표:** Chaos Debug Draw와 같은 색·테셀레이션·그리기 순서로 단일 씬을 보는 뷰어. 엔진 전환, 일시정지/한 스텝, Stats를 갖춘다. 사용자가 Chaos 전에 엔진을 눈으로 확인할 수 있게 첫 엔진(Jolt) 바로 다음에 둔다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `PT::PhysicsDebugDraw`: `FDebugDrawQueue`, `FDebugDrawSettings`(Chaos CVar 이름), `DebugDraw::Draw*`, `FIslandBuilder`, `VisitCommands`, `IDebugDrawRenderer` | 1.5 |
| 2 | `PT::D3D11DebugRenderer`: `proto_render.cpp` 이식 | 1.75 |
| 3 | 카메라와 앱: `FViewportCameraController`, `FPhysicsTestViewerApp`, 패널, 엔진·씬·프로필 전환, 일시정지/스텝/리셋, 스크린샷 | 1.75 |

작업별 세부:
- **디버그 드로**
  - `FDebugDrawQueue`는 `Chaos::FDebugDrawQueue`의 시그니처를 미러링한다. `DrawDebugCircle(Center, Radius, Segments, Color, ..., YAxis, ZAxis, bDrawAxis)`, 크기 인자가 없는 `DrawDebugPoint`, `FRotator`를 받는 `DrawDebugCoordinateSystem` (DebugDrawQueue.h:114, 150, 209 [V]). 예산은 `p.Chaos.DebugDraw.MaxLines` 20000, 영역 반경 3000 cm.
  - 그리는 순서는 `FPBDRigidsSolver::PostTickDebugDraw`와 같다: Shapes → Collisions → Mass → Bounds → Transforms → ConstraintGraph(섬) → SleepState → CollidingShapes → Joints (PBDRigidsSolver.cpp:3417-3546 [V]). 섬 드로어 이름은 `DrawConstraintGraph`다.
  - 상태색: Dynamic (255,255,0), Sleeping (128,128,128), Kinematic (0,128,255), Static (255,0,0). Bounds는 어두운 표를 쓴다 (ChaosDebugDraw.cpp:384-393 [V]).
  - 솔버 기본값: LineThickness 1 cm(월드 공간), PointSize 5, ContactLen 30, ContactWidth 6, BodyAxisLen 30, ArrowSize 10 (PBDRigidsSolver.cpp:219-247 [V]).
  - `IDebugDrawRenderer`는 `IChaosDDRenderer`의 메서드 순서를 그대로 따른다. 이 순서는 이미 확인되었다 (ChaosDDRenderer.h:19-44 [V]). 다시 대조하는 작업은 두지 않는다.
  - `FIslandBuilder`: Phi ≤ 0 접촉과 조인트에 대한 union-find, static 제외, 섬 ID는 가장 작은 바디 index, 결정적.
- **렌더러:** 단위 와이어 box 12 / sphere 128 / capsule 68 선분, circle 8, 월드 폭 선(0이면 1 px), SeeThrough 2패스, 화면 공간 점, reversed-Z 무한 far, UE 뷰 기저, MSAA 4x, 클리어 sRGB (40,42,46), grid 100 cm. 스크린샷 PNG는 동기 저장이면 충분하다 (비동기 인코딩과 WARP 골든은 M12).
- **앱과 패널**
  - `FPhysicsTestViewerApp`는 FEngineLoop 단계(PreInit / Init / Tick / Exit)를 따른다. 물리는 메시지 루프 스레드에서 돈다.
  - 카메라는 UE 에디터 규약이다: hFOV 90°, near 10 cm, RMB + WASD, E/Q 월드 상하, 휠로 속도 조절.
  - 패널: Engines, Inspector(Outliner와 Details를 합친 하나), Stats(`stat ChaosCounters` 이름), Console(CVar).
  - 레이아웃은 Single만 있다. SideBySide는 M12다.
  - 엔진 목록은 백엔드 레지스트리에서 읽는다. M5의 백엔드는 등록만 하면 나타난다.
  - 모듈 플래그(`GLOBAL_SCENE_SETTINGS`, `ONE_PER_PROCESS`)나 `InitGamePhys` 거부 때문에 같은 프로세스에서 바꿀 수 없는 조합이면 "재시작 필요"를 띄우고 해당 인자로 뷰어를 다시 실행한다.
  - 뷰어에서 나온 manifest는 항상 `mode = viz`다.

**완료 기준** (Jolt와 Null로 판정)
- 시각 체크리스트를 `M1.SingleBoxDrop`과 `M3.BoxPile` 1k에서 통과한다:
  - 상태색과 bounds 색이 위 표와 같다.
  - 선분 수 12 / 128 / 68 (단위 테스트).
  - 접촉 원 반경 6 / 3, normal 길이 30, 비활성 접촉은 ×0.75.
  - COM 축 30, 화살표 10.
  - 섬 팔레트가 보이고 잠든 섬은 Black이다. 클리어 색이 검정이 아니라서 보인다.
  - 바닥이 박스로 그려진다.
- 명령 스트림 단위 테스트: Draw 호출 순서가 위 9단계와 같다. 조인트 드로는 합성 스냅샷으로 검증한다 (실제 조인트는 M9).
- 변환 단위 테스트: ABI HalfLength → DrawDebugCapsule HalfHeight = HalfLength + R. 박스는 half extent 그대로 넘긴다.
- 성능 (RTX 3080, 1600×900, MSAA 4x):
  - `M3.BoxPile` 10k Jolt를 일시정지한 상태에서 60 fps 이상이다.
  - 같은 `M3.BoxPile` 10k에서 씬 하나의 스냅샷 + 드로 CPU 시간이 4 ms 이하다. 연속 부하(`-Headless` 또는 창 모드 `-NoVSync`)에서 재고, 값은 Stats에 표시한다. 기본 창 모드(vsync)에서 Stats가 보여 주는 값은 프레임마다 vsync를 기다리며 쉬는 스레드의 값이라 판정에 쓰지 않는다 (M4 측정 [V]: 같은 일이 2~3배 느리다. Stats가 그때 그 사실을 한 줄로 알린다).
  - 시뮬레이션을 돌리는 동안의 fps는 표시만 하고 기준으로 삼지 않는다. 물리가 같은 스레드에서 돌기 때문이다 (Bullet ST 3008 박스가 스텝당 중앙값 18.1 ms [V]).
- Null ↔ Jolt 전환이 재시작 없이 된다. 일시정지 중 "한 스텝"은 FrameCount를 정확히 1 올린다.
- 뷰어가 보여 주는 모든 시간에 "viz mode, not a benchmark" 표기가 붙는다.

**산출물:** `Source/Runtime/PhysicsDebugDraw`, `Source/Runtime/D3D11DebugRenderer`, `Source/Editor/PhysicsTestEditor`(1단계 패널), `Source/Programs/PhysicsTestViewer`.

**선행:** M3.

---

### M5. Bullet + PhysX 백엔드 + BackendCommon 테스트 [MVP] (H, 9 pd, 선행 M3, 1인 기준 M4 다음)

**목표:** 나머지 두 SDK를 Jolt와 같은 경로에 올린다. PhysX는 UE4 방식 레시피를 클린룸으로 재현하고 변형 4종을 둔다. BackendCommon을 단위 테스트로 고정한다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | BackendCommon 완성과 단위 테스트 | 1.0 |
| 2 | `FPhysScene_Bullet` (ST 월드) | 3.0 |
| 3 | PhysX 5.11 UE4 방식 (`UE4Compat`, `SdkDefault`, `UE4Compat-TGS`, `UE4Compat-Stab`) | 4.5 |
| 4 | 등록과 통합: 레지스트리, `-Describe`, 엔진 간 `desc_sha256` 동일성, smoke run | 0.5 |

작업별 세부:
- **BackendCommon:** 단위 계수표(Chaos·PhysX-UE4Compat는 1, Jolt·Bullet·PhysX-SdkDefault는 0.01), 캡슐 재매핑(PhysX X는 `U2PSphylBasis`, Jolt Y, Bullet은 `btCapsuleShapeZ`라서 재매핑 없음. 고정 결정 D1의 보정으로 [04_DecisionLog.md](04_DecisionLog.md)에 기록), Bullet damping 변환, UE 결합 규칙(정지마찰 max 규칙 포함), 접촉 정규화(Jolt 부호 반전, Body0 = 가동성이 더 큰 쪽), ODR 분리 확인.
- **Bullet**
  - `btDefaultCollisionConfiguration`(풀 크기는 힌트), `btDbvtBroadphase`, `btSequentialImpulseConstraintSolver`, `btDiscreteDynamicsWorld`. 스텝은 `stepSimulation(dt, 0, dt)`.
  - 셰이프: `btSphereShape`, `btBoxShape`, `btCapsuleShapeZ`. 박스 마진은 상수가 아니라 셰이프마다 `setSafeMargin`이 정한 min(0.04 m, 0.1 × 최소 half extent)이다 (btBoxShape.cpp:25, btConvexInternalShape.h:66-81 [V]). `getMargin()`으로 읽어 `shape.box_margin`에 셰이프별로 기록한다.
  - COM: 요소가 하나이고 ComLocal이 사실상 항등이면 `btBoxShape`를 그대로 쓴다. 그 밖에만 `btCompoundShape`(자식을 inv(ComLocal)·Elem에 배치, 읽을 때 ActorTM 복원).
  - 필터 group = 1 << ObjectType, mask = BlockMask. 전역 결합 콜백(`GLOBAL_SCENE_SETTINGS`).
  - UEParity 각속도 62.83 rad/s는 내부 pre-tick 콜백으로 흉내낸다. 하드코딩 한계 π/(2·dt) = 94.2 rad/s (btRigidBody.cpp:380-385 [V])는 COMPILE_TIME으로 기록한다.
  - Normalized sleep 끔은 `DISABLE_DEACTIVATION`.
  - kinematic은 `CF_KINEMATIC_OBJECT` + 보간 transform + 속도. 매니폴드 → Chaos 규약 스냅샷, `getIslandTag`.
  - 디버그 드로어는 bench와 behavior 모드에서는 붙이지 않는다. 드로어가 붙으면 `stepSimulation`이 `gDisableDeactivation`을 덮어쓰기 때문이다 (btDiscreteDynamicsWorld.cpp:418-421 [V]). viz 모드 드로어는 M9에서 다룬다.
  - 바디 생성 순서는 M2의 UE 순서를 따른다.
- **PhysX UE4 방식**
  - `PhysXSupport`: U2P/P2U 변환(LengthScale 포함), `U2PSphylBasis`, `FPhysXAllocator`(16 B 정렬, live/peak), `FPhysXErrorCallback`(error bits), `FPhysxUserData`.
  - `InitGamePhys`는 프로세스당 1회다. PxPhysics와 PxTolerancesScale은 프로세스당 하나다 (PxPhysics.h:997, PxSceneDesc.h:541 [V]). `UE4Compat`, `-TGS`, `-Stab`은 (100, 1000)을 공유하므로 한 프로세스 안에서 별도 PxScene으로 함께 둘 수 있다. `SdkDefault`(1, 10)만 자기 프로세스가 필요하다.
  - `FPhysXCPUDispatcherSingleThread`: `submitTask`는 thread-local LIFO 스택을 쓴다. 재진입 호출은 push만 하고 돌아가고, 가장 바깥 호출이 스택을 비운다. 재귀하지 않는다. T ≥ 2는 `PxDefaultCpuDispatcherCreate(T)`.
  - `PhysicsFiltering` + 필터 셰이더: word 배치는 [M], word3의 EPDF 비트 배치는 UE5 PhysicsFiltering.h:18-35 [V].
  - 머티리얼: `createMaterial(staticFriction, dynamicFriction, restitution)` 순서다 (PxPhysics.h:764 [V]). `UE4Compat` 계열은 UE4처럼 `createMaterial(F, F, R)`, 즉 정지마찰 = 동적마찰이다 [M]. `SdkDefault`는 `createMaterial(max(F, SF), F, R)`이다. 어느 규칙을 썼는지 effective 키로 기록한다.
  - `FPhysXSimEventCallback`(viz 전용), `FPhysScene_PhysX::InitPhysScene`이 변형 ini로 PxSceneDesc를 구성한다. 모든 UE4 유래 키에 증거 태그가 붙는다.
  - 바디 생성 순서: `createRigidStatic/Dynamic`(생성 플래그) → `FBodySetupShapeIterator`로 셰이프(요소마다 exclusive PxShape, contact offset = clamp(0.02 × MinExtent, 2, 8) cm [M 공식, V 상수]) → `addActors` 1회 → InitDynamicProperties를 UE 순서로 (BodyInstance.cpp:4648-4743 [V]) → `!bStartAwake`이면 `putToSleep`. `putToSleep`은 씬에 들어간 바디에서만 허용된다 (NpRigidDynamic.cpp:527 [V]).
  - 스텝은 StartFrame = `simulate(dt, nullptr, scratch, 262144)`, WaitPhysScenes = `fetchResults(true)`. 동기화는 `getActiveActors`.
  - Normalized sleep 끔은 `PxSceneFlag::eDISABLE_SLEEPING`이다 (임계값 0 같은 우회를 쓰지 않는다).
  - substep clamp 규칙은 하네스 FStepPlanner가 갖는다. `FPhysXSceneConfig`에는 ClampRule 같은 하네스 타입을 두지 않는다.
  - 모든 PhysX manifest에 충실도 노트(UE4가 출하한 것은 PhysX 3.4.0.0, 5.11과의 차이)를 적고, 결과에는 "UE4 settings on PhysX 5.11"로 표기한다.
  - 조인트(D6)는 M9에서 세 엔진을 한꺼번에 한다.

**완료 기준** (뷰어 없이 bench exe와 automation으로 판정)
- Jolt, Bullet, PhysX 변형 4종이 모두 `S00.FreeFall`, `M1.SingleBoxDrop`, `M3.BoxPile` 1k/5k를 UEParity와 Normalized에서 ABI로 생성하고 스텝한다.
- 전 백엔드 `S00.FreeFall`: |z60 − 501.83328| ≤ 2e-3 cm.
- `-Describe`가 공통 필수 키와 백엔드가 선언한 키를 하나도 빠짐없이 출력하고, 읽을 수 있는 키는 전부 READBACK이다. Bullet `shape.box_margin`: 50 cm 큐브 0.025 m, 100 cm 큐브 0.04 m.
- `desc_sha256`이 jolt, bullet, physx 변형 4종에서 씬·프로필마다 같다.
- 각 백엔드가 static 경로에서 A0~A7을 통과한다.
- `M3.BoxPile`에서 Bullet과 Jolt의 `shape.wrapping` = 0이다.
- PhysX:
  - `UE4Compat`, `-TGS`, `-Stab` 세 PxScene을 한 프로세스에서 만들고 스텝한다. `UE4Compat` 뒤에 `SdkDefault`를 초기화하려 하면 크래시 없이 오류로 거부된다.
  - `FPhysXCPUDispatcherSingleThread`로 돌린 `M3.BoxPile` 5k 600 프레임의 최종 `h_native`가 `PxDefaultCpuDispatcherCreate(0)` 결과와 같다. 디스패처 재진입 깊이가 1을 넘지 않는다.
  - `UE4Compat` readback: contact offset 2 cm (50 cm 큐브) / 8 cm (10 m 박스), tolerances (100, 1000), bounce 200 cm/s, sleep 50 / stab 25 (× SleepFamily).
  - 필터 셰이더 진리표 단위 테스트가 통과한다.
- ODR: Jolt(/arch:AVX2)와 Bullet·PhysX(/arch 없음) 백엔드를 한 exe에 링크했을 때 링커 맵에서 BackendCommon과 Kit의 비템플릿 함수가 백엔드별 namespace로 분리되어 있다.
- 하네스 TU에 SDK include나 정의가 없다 (CTest).

**산출물:** `Source/Backends/BulletBackend`, `Source/Backends/PhysXBackend`, `Config/Profiles/Backend/{PhysX.UE4Compat,PhysX.SdkDefault,PhysX.UE4Compat-TGS,PhysX.UE4Compat-Stab,Jolt.Default,Bullet.Default}.ini`, BackendCommon 단위 테스트.

**선행:** M3. 1인이면 M4 다음에 한다.

---

### ★ 체크포인트 A

§1.6의 정의를 따른다. M4와 M5의 결합 확인은 여기서만 한다.

---

### M6. 물리 게이트 + 게이트 씬 + ABI 핵심 v1.0 동결 + ChaosBackendGuide 초안 [MVP] (H, 4 pd, 선행 M5)

**목표:** 세 SDK가 해석해 게이트를 통과한다는 것을 보인 뒤 ABI 핵심을 v1.0으로 동결하고, 사용자가 Chaos를 시작할 수 있게 가이드 초안을 낸다. 이 게이트를 통과하기 전에는 어떤 엔진 비교 수치도 보고하지 않는다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | 게이트 씬: `S00d.FreeFallDamped`, `S01.UnitImpulse`, `S02.SpinInertia`, `S03.CapsuleRest`, `S04.ContactNormal`, `S05.RestitutionCombine`, `S06.FrictionSlide`, `S07.ClampProbe`, `S08.StartAsleep`. `docs/Scenes.md`에 카탈로그 확장으로 등록 | 1.25 |
| 2 | 게이트 G1, G1b, G2~G7, G9, G12와 `Config/Golden/tolerances.json`, `PhysicsTestBench -Conformance`(게이트 스위트 실행 → `conformance.json`) | 1.25 |
| 3 | ABI 핵심 v1.0 동결 | 0.75 |
| 4 | `docs/ChaosBackendGuide.md` 초안 | 0.75 |

작업별 세부:
- **게이트 씬**
  - `S05.RestitutionCombine`: e 0.8, 임계 50 cm/s. 해석 기준은 `dt_engine`으로 만든다. 낙하 높이는 이산 궤적이 스텝 경계에서 정확히 바닥에 닿도록 고른다: h0 = 980 × dt_engine² × k(k+1)/2, k = 38이면 h0 = 201.7167 cm, 충돌 속도 620.67 cm/s. 반발 계수는 접촉 프레임 직전과 직후의 법선 속도 비로 잰다. apex 비는 보조 지표다.
  - `S08.StartAsleep`: 공중(z0 = 500 cm)에 `bStartAwake = false` 박스 하나와 `bStartAwake = true` 박스 하나를 둔다. UEParity(sleep 켬)에서만 돈다.
  - 나머지 씬의 설정은 [02_Architecture.md](02_Architecture.md)의 게이트 표를 따른다.
- **게이트와 허용치**
  - 모든 해석 기준값은 `dt_engine`으로 계산한다. ±1e-9 같은 허용치는 그 기준값에 대해서만 쓴다 (Null). float 엔진은 G1의 2e-3 cm를 쓴다.
  - 엔진별 허용치는 실행 전에 `tolerances.json`에 사유와 함께 미리 등록한다. 예: PhysX UE4Compat contact offset 2 cm에 따른 이른 접촉, Bullet 마진, Bullet 94.2 rad/s.
  - G9는 리졸브된 프로필 값과 `DumpEffectiveSettings` READBACK 값의 직접 비교다 (preflight 통합은 M7).
  - `PhysicsTestBench -Conformance`는 실제 컴파일된 백엔드 코드로 게이트 스위트를 돌려 `conformance.json`을 쓴다. 이것을 스탬프 키로 묶어 실행 전 거부에 쓰는 일은 M10이다.
  - G0(빌드), G8(DET1), G10a(벤치 위생), G10b(단일 스레드 증명), G11(결과 중립 knob)은 여기서 하지 않는다. G0·G8·G10a·G11은 M7, G10b는 M10이다.
- **ABI 핵심 v1.0 동결**
  - `PT_ABI_VERSION_MAJOR = 1`, 태그 `abi-v1.0`.
  - 동결 대상은 핵심 항목이다. 조인트(`CreateConstraints`와 구조체), 디버그 스냅샷(`CaptureDebugSnapshot`/`ReadDebugSnapshot`과 구조체), 네이티브 드로(`PtDebugDrawCmd`)는 "M9까지 잠정"으로 남긴다.
  - 동결 전제: SDK 3종과 Null이 A0~A7, A9, A10과 이 마일스톤의 G 게이트를 통과한다. A10(Null의 Sync, Substepping UE5, AsyncFixed 사례)은 M2 결과를 재사용하지 않고 다시 돌린다. Null의 독립 플래너는 UE5 `FChaosScene`/`PhysicsSolverBase` 규칙만 구현하므로 UE4PhysSubstep 규칙은 M1 FStepPlanner 단위 테스트로만 고정한다.
  - M2.5 변경 목록이 있으면 반영한다. 스파이크 DLL(major 0)은 동결 뒤 major 1로 다시 빌드해야 로드된다.
  - 동결 뒤 v1.x는 append-only다 (StructSize/Stride 뒤 필드 추가, 테이블 끝 함수 추가, 새 bit와 enum).
- **ChaosBackendGuide 초안에 넣을 의무사항**
  - 스레드: T=1은 `SetThreadingMode_External(SingleThread)` + `p.Chaos.SingleWorkerPhysics=1`(선택으로 `p.Chaos.DisablePhysicsParallelFor=1`) + `-corelimit=1`이다. SingleThread 모드만으로는 내부 ParallelFor가 꺼지지 않는다 (Parallel.cpp:95-97, ConstraintGroupSolver.cpp:250 [V]). T ≥ 2는 TaskGraph + `-corelimit=T` + `p.Chaos.MaxNumWorkers=T`이고, MaxNumWorkers는 상한일 뿐 풀 크기를 정하지 않는다 (Parallel.cpp:33, 기본 100 [V]). effective 키 `threads.single_worker`, `threads.max_num_workers`를 Chaos 필수 키로 선언한다.
  - 바디 생성 순서: CreateActor → SetGeometry → 필터 → 모든 셰이프에 머티리얼 → `AddActorsToScene_AssumesLocked` 1회 → InitDynamicProperties → `!bStartAwake`이면 sleep.
  - 질량은 받은 값을 그대로 쓰고 ^0.75로 다시 계산하지 않는다 (G2가 검증). 모든 셰이프에 머티리얼이 있어야 sleep 임계가 CVar 값으로 떨어지지 않는다 (IslandManager.cpp:121-131 [V]).
  - 박스 마진은 min(2 × minHalf × 0.05, 10 cm)이다 (ChaosInterfaceUtils.cpp:156 [V]). Engine 모듈에 있으므로 브리지가 다시 구현한다.
  - 동기 모드는 `AsyncDt = -1`. AsyncFixed의 GT 상태는 한 스텝 늦게 보간되므로 바디별 거동 비교에서 뺀다. `p.UseAsyncInterpolation=0`으로 보간을 끄면 fixed-dt 누산기도 함께 꺼진다 (PhysicsSolverBase.h:712-720 [V]).
  - 충돌 마스크는 `uint64`로 넓힌다 (UE 5.8.3 ECC_MAX = 65).
  - 정지마찰은 UE 머티리얼 값(F, SF)을 `FChaosPhysicsMaterial`에 그대로 넣는다. 쌍 규칙 `max(combine(max(F0,SF0), max(F1,SF1)), combine(F0,F1))`은 Chaos가 직접 적용한다 (DL41).
  - Normalized sleep 끔: `p.Chaos.Solver.Sleep.Enabled=0` 또는 파티클별 `ESleepType::NeverSleep` (IslandManager.cpp:53, 240 [V]).
  - SolverOptions는 `FChaosScene` 생성 뒤 `ApplyConfig` + `SetIsDeterministic`으로 명시 적용한다. CDO ini에 기대지 않는다.
  - Build.cs에 `PrivateIncludePathModuleNames.Add("Launch")`, Target.cs에 `bHasExports = false`(이미 Monolithic 기본값이지만 명시), `FPSemantics = Imprecise`.
  - 디버그 드로: 캡슐 HalfHeight는 반지름 포함, 박스 Size는 half extent.
  - 금지: `RequestExit`, `SetStandaloneSolver(true)`, `FreeLibrary`. 종료는 결과와 `exit.json`을 쓴 뒤 `TerminateProcess(GetCurrentProcess(), code)`.

**완료 기준**
- jolt.Default, bullet.Default, physx.{UE4Compat, SdkDefault, UE4Compat-TGS, UE4Compat-Stab}가 UEParity와 Normalized 모두에서 G1~G7, G9, G12를 통과한다 (`PhysicsTestAutomation`의 `PhysicsTest.Gates.*`). 예상된 편차는 `tolerances.json`에 사유와 함께 있다.
- 개별 수치:
  - G1: |z60 − 501.83328| ≤ 2e-3 cm, |v60 + 980.00005| ≤ 1e-2 cm/s.
  - G1b: 엔진이 선언한 damping 공식의 기준값과 1e-3 cm 이내.
  - G2: Δv(100 cm/s)와 ω가 1e-4 상대오차, 질량·관성·COM readback이 1e-5, 캡슐이 서면 75 cm, 누우면 25 cm (±0.5 cm).
  - G3: Jolt 부호 반전 후에도 normal·(+Z) > 0.999, step 1에서 Phi < 0.
  - G4: 법선 속도 비로 잰 e가 엔진별 등록 허용치(기본 ±0.02) 이내. 보조로 apex 비 0.64, 바닥 e = 0일 때 Average 0.16 / Multiply 0 / Max 0.64.
  - G5: 정지 거리 250.947 cm ±1% (`dt_engine` 기준 이산해. 연속해 255.10은 오답). 30° 경사에서 μ ≥ 0.6은 정지, μ ≤ 0.55는 미끄러짐.
  - G6: Normalized에서 정지 600 프레임 동안 잠든 바디 0. UEParity `S08.StartAsleep`: `bStartAwake = false` 박스가 생성 직후와 60 프레임 뒤 모두 Sleeping이고 z가 변하지 않는다. 다른 박스는 떨어진다.
  - G7: UEParity 62.83 ±0.01 rad/s. Normalized는 해제 (Bullet 94.2는 등록된 예외).
  - G9: READBACK 키 전부가 리졸브된 값과 같다. `M3.BoxPile` 1k에서 `shape.wrapping` 합계가 0이다.
  - G12: Jolt update error 0, PhysX error callback 0, Bullet 풀 heap fallback 0.
- `PhysicsTestBench -Conformance`가 위 조합마다 `conformance.json`을 남긴다.
- ABI: `abi-v1.0` 태그. Null이 A10을 통과한다. 모든 SDK 백엔드가 A0~A7을, Null DLL이 A9를 통과한다.
- `docs/ChaosBackendGuide.md` 초안이 위 의무사항을 file:line 근거와 함께 담는다.

**산출물:** 게이트 씬, `Source/Runtime/PhysicsTestConformance/Gates`, `PhysicsTestBench -Conformance`, `Config/Golden/tolerances.json`, `scene_hashes.json`, `abi-v1.0` 태그, `docs/Abi.md`(동결판), `docs/ChaosBackendGuide.md`(초안), `docs/Scenes.md` 갱신.

**선행:** M5.

---

### M2.5. Chaos hello-DLL 스파이크 [MVP, 선택] (H 1 pd + U 약 2 pd, 3 달력일 타임박스, 선행 M2, M5–M6과 병행)

**목표:** UBT Program DLL로 Chaos를 남의 프로세스 안에서 돌릴 수 있는지 실증하고, ABI 동결 전에 필요한 변경을 찾는다. 실패해도 Phase 1은 그대로 진행한다.

**전제:** `D:\UnrealEngine` 5.8.3 소스 트리 (Build.version, Build.bat, UBT 바이너리, `.uedependencies` 확인됨 [V]), D: 여유 10 GB 이상.

**작업**

| # | 작업 | 담당 | pd |
|---|---|---|---|
| 1 | `Unreal/ChaosBridgeHost` 골격: 타깃명과 다른 이름의 `ChaosBridgeHost.uproject` (UEBuildTarget.cs:1926-1946 [V]), `PhysicsTestChaos.Target.cs`, `PhysicsTestChaos.Build.cs`, Kit 기반 `PtChaosModule.cpp`(GetInfo / Init / Term에 TODO), `.gitignore` | H | 0.5 |
| 2 | `cmake/Chaos/PtChaosUbt.cmake`, `Tools/Chaos/build_chaos.cmd`, `check_modules.py`의 UBT `.rsp` 파서 | H | 0.25 |
| 3 | `docs/ChaosSpike.md` 양식과 게이트 체크리스트 | H | 0.25 |
| 4 | InitGamePhys: SetOverrideProjectDir → `UE::Geometry::ExactPredicates::GlobalInit()` → `GEngineLoop.PreInit(-ABSLOG -unattended -corelimit=T)` → ProcessNewlyLoadedUObjects → StartProcessingNewlyLoadedObjects → InitGamePhysCore | U | 약 1 |
| 5 | 구 1개짜리 FChaosScene: CreateActor, SetGeometry, AddActorsToScene, 프레임 4호출, ReadBodyStates. Development와 Test 빌드 | U | 약 1 |

골격의 세부:
- Target.cs: `Type = Program`, `LinkType = Monolithic`, `bShouldCompileAsDLL = true`, `FPSemantics = Imprecise`(Program 타깃의 V7 기본값은 /fp:precise, VCToolChain.cs:1324-1330 [V]), `bCompileAgainstEngine = false`, `NOINITCRASHREPORTER=1`, `bHasExports = false`. 마지막 값은 이미 Monolithic의 기본값이다 (TargetRules.cs:1792-1795 [V]). `*_API`가 비어 있게 명시하고, `PT_GetBackendApi`는 자기 `__declspec(dllexport)`로 export된다.
- Build.cs: Core, CoreUObject, ApplicationCore, Projects, GeometryCore, PhysicsCore, Chaos, ChaosCore + `PrivateIncludePathModuleNames.Add("Launch")`. `RequiredProgramMainCPPInclude.h`가 `LaunchEngineLoop.h`와 `LaunchEngineLoop.cpp`를 include하기 때문이다 (RequiredProgramMainCPPInclude.h:6, 11 [V]).
- 스파이크 DLL은 ABI major 0 (v0.9)을 쓴다. 호스트는 자신이 빌드된 major를 요청한다.

**게이트**

| 게이트 | 기준 | 실패 시 대안 |
|---|---|---|
| SP1 | DLL과 intermediate가 `Unreal/ChaosBridgeHost` 안에 생기고 `Engine/Programs` 아래에는 아무것도 생기지 않는다. `RequiredProgramMainCPPInclude.h`가 컴파일된다. export는 `PT_GetBackendApi` 하나, 의존성은 CRT와 시스템 DLL뿐. `.rsp`에 /fp:fast | .def 파일 (ModuleDefinitionFile) / 저장소 밖 프로젝트 |
| SP2 | PreInit이 0을 반환하고 ABSLOG 경로가 맞다. WM_QUIT 없음, 호스트 예외 필터 복구됨. MXCSR, 스레드 수, 타이머 해상도, DPI, 작업 디렉터리를 기록한다 | OOP EXE (M14) |
| SP3 | M2 자유낙하 픽스처: z60 = 501.833 ±0.01 cm. T=1 (SingleThread + `p.Chaos.SingleWorkerPhysics=1` + `-corelimit=1`)과 TaskGraph 둘 다. create/destroy 10회에 메모리 증가와 교착 없음 | OOP EXE (M14) |
| SP4 | Test DLL이 두 번째 프로세스에서 동작하고 debug draw 요청에 `PT_E_UNSUPPORTED`를 돌려준다 | Shipping + logging |
| SP5 | Kit 경유로 A0~A6 통과. M2 자유낙하 픽스처에 G1 허용치(\|z60 − 501.83328\| ≤ 2e-3 cm)를 적용해 통과. `docs/ChaosSpike.md` 작성, ABI 변경 목록 정리 (반영은 M6, 1 pd 이내) | - |

- 프로세스 종료 시 크래시나 행이 있는지도 기록한다. 결과가 M7의 `exit.json` + `TerminateProcess` 설계의 근거가 된다.
- 실패하면 게이트별로 어느 대안을 고를지와 그 비용을 `docs/ChaosSpike.md`에 적는다.

**완료 기준:** SP1~SP5 각각에 통과 또는 실패와 선택한 대안이 기록되어 있다. 통과한 경우 ABI 변경 목록이 M6 동결 전에 H에게 전달된다.

**산출물:** `Unreal/ChaosBridgeHost` 골격, `PtChaosUbt.cmake`, `build_chaos.cmd`, `.rsp` 파서, `docs/ChaosSpike.md`.

**선행:** M2. M5~M6과 병행한다.

---

### M7. 벤치 러너 + 측정 프로토콜 [확장] (H, 5 pd, 선행 M6)

**목표:** 1 run = 1 process 규약을 지키는 완전한 벤치 실행 순서, 결과 포맷, preflight, 성능 씬, 스레드 매핑을 만든다. 결정성 1단계(DET1, G8), 벤치 위생(G10a), 결과 중립 knob(G11)을 게이트로 넣는다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `FBenchRunSpec` 완성, `FBenchmarkRunner` 실행 순서, 종료 코드, `exit.json` | 1.0 |
| 2 | `FHostEnvironment`와 프로세스 스포너 (`PT::BenchCore`) | 0.75 |
| 3 | Recorders: `timings.csv`, `states.ptst` + `states.dtype.json`, `hashes.csv`, `contacts.ptcn`, `effective_settings.jsonl`, `scene.json` | 1.0 |
| 4 | `FRunManifest`, 스키마, `FPreflight`(G0, G9, G10a), 계측 tier와 `timing_valid_for_headline` | 0.75 |
| 5 | 성능 씬, 스레드 매핑 T=1..6, DET1(G8), G11, test-avx2 프리셋 | 1.5 |

작업별 세부:
- **실행 순서:** 인자 파싱 → 호스트 제어 → ini 로드 → 백엔드 로드와 `InitGamePhys` → 씬 구성과 `desc_sha256` → effective 덤프와 preflight → Prepare → spin pre-roll 0.5 s → frame 0 → 측정 루프 → teardown → 기록. 스레드 센서스 단계는 M10에서 끼워 넣는다.
- **`exit.json`:** 모든 출력을 쓰고 flush한 뒤 의도한 종료 코드를 `exit.json`에 적는다. 모듈 플래그에 `UE_RUNTIME`이 있으면 `TerminateProcess(GetCurrentProcess(), code)`로 끝낸다. UE를 품은 DLL은 정적 소멸자 단계에서 크래시하거나 멈출 수 있기 때문이다. 러너(M8)는 원시 종료 코드보다 `exit.json`을 믿는다.
- **호스트 제어:** SDK 초기화나 LoadLibrary보다 먼저 affinity, HIGH_PRIORITY_CLASS, power throttling opt-out, `timeBeginPeriod(1)`(UE DLL이 프로세스 전역으로 바꾸므로 모든 엔진에 똑같이, WindowsPlatformMisc.cpp:1076 [V]). 전원 계획, CPU MHz, 배경 부하는 읽어서 기록만 한다.
- **Recorders:** 타이밍 루프 안에서는 할당, I/O, 로그를 하지 않는다. 헤드라인 run은 해시를 마지막 프레임에만 계산하고 `CountersEvery = 0`이다. 리스너가 필요 없는 `GetStepStats` 읽기는 헤드라인 run에서도 허용한다.
- **계측 tier:** counters, phases, alloc, contacts, native_debug_draw 중 하나라도 켜면 `timing_valid_for_headline = false`다.
- **G0:** TU 자기보고(어댑터 TU 사실) = `BuildFlags.json` = 플래그 프로필. SDK 내부는 SDK TU의 `compile_commands.json` 항목과 PhysX config로 판정한다.
- **G10a (벤치 위생):** 헤드라인 자격 run의 조건(listener 0, 드로어 분리, 계측 tier 0)을 preflight가 확인한다. 단일 스레드 증명(cpu_eff, 외부 스레드 cycle)은 G10b로 M10에 둔다.
- **성능 씬:** `P0.EmptyStep`, `P1.SmallScene`(10/100), `M3.BoxPile` 10k, `M2b.Pyramid1240`. P0, P1은 `docs/Scenes.md`에 카탈로그 확장으로 등록한다. 윈도는 씬이 선언한다 (예: `M3.BoxPile` headline 1-599, impact 1-179, settled 300-599).
- **스레드 매핑:** affinity 마스크 0x1, 0x5, 0x15, 0x55, 0x155, 0x555 (P코어당 논리 CPU 1개). Jolt 워커 T−1, PhysX T=1 inline / T ≥ 2 `PxDefaultCpuDispatcherCreate(T)`, Bullet T=1만. 엔진 고유 스레드 설정값을 T 옆에 기록한다.
- **test-avx2 프리셋:** MatchedAVX2 플래그 프로필. SDK와 어댑터에만 `/arch:AVX2`, 하네스에는 없음.

**완료 기준**
- `M3.BoxPile` 5k run의 `manifest.json`이 `Config/Schemas/manifest.schema.json`을 통과하고, mode, instrumentation, `timing_valid_for_headline`, `dt_engine_s`, threads, platform(전원 계획 포함) 필드가 모두 채워진다.
- 클럭: QPC 주파수와 읽기 오버헤드가 기록된다 (TSC는 M10).
- G8(DET1): 모든 백엔드·변형에서 T=1 fresh process 3회의 최종 `h_native`가 같다.
- G0: Jolt, Bullet, PhysX가 test와 test-avx2 두 빌드에서 통과한다.
- G11: 접촉 캡처를 켜고 끈 `M3.BoxPile` 1k 600 프레임의 상태가 모든 백엔드에서 비트 단위로 같다.
- G9 불일치가 하나라도 있으면 preflight가 exit 2로 거부한다 (주입 테스트).
- G10a: 계측 tier 0인 bench 모드 run에 listener나 드로어가 붙어 있으면 preflight가 exit 2로 거부한다 (주입 테스트). 계측 tier를 켠 run은 거부하지 않고 `timing_valid_for_headline = false`로 표시한다. 모든 백엔드의 정상 bench run은 통과한다.
- `-Instrument=counters` run에 `timing_valid_for_headline = false`가 붙는다. 헤드라인 모드 run은 `hashes.csv`에 마지막 프레임 한 줄만 있다.
- `test` 빌드에서 타이밍 루프 안 할당이 0이다 (카운팅 훅).
- 종료 코드 0, 2, 3, 4, 5, 6 경로 모두에서 `exit.json`이 의도한 코드와 함께 남는다 (4는 다른 프로세스 부하를 주입해 오염된 호스트 판정을 재현한다). `UE_RUNTIME` 플래그를 켠 Null DLL 시험 빌드로 `TerminateProcess` 경로를 확인한다.
- 같은 씬·프로필에서 test와 test-avx2의 `desc_sha256`이 같다.
- Jolt와 PhysX의 T=1..6 run과 Bullet T=1 run이 끝까지 돌고 affinity 마스크가 기록된다. 스케일링 수치에는 "cpu_eff 미검증 (M10)" 표기가 붙는다.
- `PhysicsTestBench.exe`가 여전히 d3d11.dll을 import하지 않는다.

**산출물:** `Source/Runtime/PhysicsTestBenchmark`(완성), BenchCore 확장, `Config/Schemas/manifest.schema.json`, 성능 씬, test-avx2 프리셋, `docs/Scenes.md` 갱신, `docs/TimingContract.md`(엔진별 타이밍 구간·버킷 정의), `docs/ResultsSchema.md`(manifest·timings·states·hashes 포맷).

**선행:** M6.

---

### M8. 오케스트레이션 + 분석 기본 [확장] (H, 4 pd, 선행 M7)

**목표:** 매트릭스를 무인으로 돌리고 원시 결과에서 표와 그림을 만드는 Python 도구. 이 단계의 CV는 보고만 하고 게이트로 쓰지 않는다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `Tools/ptbench` 패키지 (pyproject, M0 lock 파일의 고정 버전, `python -I -m ptbench`) | 0.5 |
| 2 | 환경 게이트 (읽기 전용) | 0.25 |
| 3 | 러너: 매트릭스 전개, seed 고정 블록 셔플, fresh process, 타임아웃, 2 s 쿨다운, 반복 R ≥ 5, `exit.json` 신뢰, `session.json`, `index.csv`, 셀별 CV 보고 | 1.25 |
| 4 | 분석 기본: load, validate, perf, report, determinism(DET1 행렬), `analytic.py` | 2.0 |

작업별 세부:
- **환경 게이트:** `powercfg /getactivescheme`가 High performance나 Ultimate가 아니면 거부하고 `--allow-balanced`로만 우회한다. 우회한 세션은 모든 표에 표시된다. 유휴 확인(2 s 동안 3% 미만), 다른 PhysicsTest 프로세스 없음. 시스템 설정은 바꾸지 않는다.
- **분석:**
  - load: 스키마 검증 후 tidy parquet (runs, frames, effective, gates).
  - validate: 헤드라인 자격 없는 run, 게이트 실패, 오염 호스트, viz 모드, 한 표에 섞인 플래그 프로필, 한 표 안의 `desc_sha256` 불일치를 거부한다.
  - perf: run별 윈도 평균, run 간 중앙값, p95/p99, 프레임별 중앙값/IQR 시계열, N-스케일링 α, S(T)/E(T)(원시), 오버헤드 모델 t = a + b·N (P0/P1/M3).
  - report: `report/<date>/index.md` + PNG.
  - determinism: DET1 행렬(M7 G8 run들)과 첫 발산 프레임. DET2~DET5는 M15다.
  - `analytic.py`: C++ 단위 테스트와 같은 기준값을 `dt_engine`으로 계산한다.
  - bootstrap CI, 주장 규칙, comparability key, CV 게이트, sentinel은 M10이다.

**완료 기준**
- smoke 매트릭스 (jolt.Default, bullet.Default, physx.UE4Compat, physx.SdkDefault × {`P0.EmptyStep`, `M3.BoxPile` 1k, 5k} × T=1 × R=5)가 무인으로 30분 안에 끝나고, 셀마다 중앙값, p95, CV가 있는 리포트를 만든다.
- 테스트로 확인한다:
  - Shipped와 MatchedAVX2를 한 표에 섞으면 오류.
  - 한 표 안에서 `desc_sha256`이 다르면 오류.
  - viz 모드 manifest는 거부.
  - 도중에 죽은 run (`exit.json` 없음)은 실패로 표시되고 원시 종료 코드로 오판되지 않는다.
- `analytic.py`와 C++ 기준값이 1e-9 이내로 같다 (예: z60 = 501.8332813705 cm).
- determinism이 M7의 DET1 run들에서 행렬을 만들고, 일부러 한 run의 해시를 바꾼 입력에서 첫 발산 프레임을 보고한다.
- `M3.BoxPile` 5k의 CV가 셀마다 보고된다. 전원 계획이 High performance가 아니면 그 사실이 리포트 머리에 표시된다. CV 2% 미만은 목표로만 적는다.

**산출물:** `Tools/ptbench/{runner,envcheck,analysis/{load,validate,perf,report,determinism},analytic}`, `Config/Matrices/{smoke,determinism}.json`, `Config/Schemas/{session,matrix}.schema.json`.

**선행:** M7.

---

### M9. 거동 씬 + ball/hinge 조인트(3엔진) + 프로필 완성 + 거동 분석 [확장] (H, 6 pd, 선행 M6, M8)

**목표:** 결정적 씬에서 바디별 궤적을, 혼돈계 씬에서 통계 관측량을 비교한다. 세 엔진의 ball/hinge 조인트를 만들고, M6에서 잠정으로 둔 ABI 항목을 확정한다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | 거동 씬과 섭동 변형 | 1.75 |
| 2 | 세 백엔드의 ball/hinge 조인트와 조인트 스냅샷 | 1.5 |
| 3 | ABI 잠정 항목 확정 (조인트, 디버그 스냅샷, 네이티브 드로) | 0.5 |
| 4 | 프로필 완성 | 0.75 |
| 5 | 거동 분석: `behavior.py`, `analytic.py` 확장 | 1.5 |

작업별 세부:
- **씬:** `M1.SingleBoxDrop` a/b(거동 윈도), `M2a.Tower`(5/10/20/30), `M4.InclineFriction`(μ 0.30/0.50/0.55/0.60/0.70, 30°) + 등방성 변형, `M5.BouncingBalls`, `M6a.Pendulum`(5/30/90°), `M6b.Chain`(캡슐 20개), `M6c.HeavyChain`(100:1), `M7.UEDefaults`, 같은 엔진 1e-4 cm 섭동 변형. `M7.UEDefaults`는 `docs/Scenes.md`에 카탈로그 확장으로 등록한다.
  - `M5.BouncingBalls`도 G4와 같은 방식이다: 스텝 경계에서 닿는 h0(k = 38이면 201.7167 cm)를 쓰고 e는 법선 속도 비로 잰다.
- **조인트:** 하네스 프리셋 이름은 UE의 `ConstraintUtils::ConfigureAsBallAndSocket` / `ConfigureAsHinge`를 따른다 (ConstraintUtils.h:10, 19 [V]). PhysX UE4Compat는 `PxD6JointCreate`에 actor를 뒤집어 넘긴다 [M]. SdkDefault는 Spherical / Revolute. Jolt는 `PointConstraint` / `HingeConstraint`, Bullet은 `btPoint2PointConstraint` / `btHingeConstraint`. 조인트 스냅샷(`PT_CAP_JOINT_SNAPSHOT`).
- **ABI 잠정 항목 확정:** 조인트와 스냅샷 구조체를 실제 세 엔진으로 검증한다. 네이티브 드로는 PhysX `PxRenderBuffer` → `PtDebugDrawCmd` 왕복과 Bullet viz 드로어(viz run에서만 붙이고 `DBG_NoDeactivation`은 프로필의 sleep 설정을 그대로 반영)로 검증한다. 뷰어 없이 bench exe를 `-RunMode=Viz -Instrument=native_debug_draw`로 돌려 검증한다 (렌더링 없이 명령 스트림만 기록). 바뀐 것이 있으면 append-only로 반영하고 태그 `abi-v1.1`을 단다. 이후 이 항목들도 동결이다.
- **프로필 완성:** 변형 ini `Jolt.SharpBoxes`, `Bullet.StaticAabbOff`, `[/Script/PhysicsTest.ForcedOverrides]`, preflight diff 출력. UEParity의 Jolt·Bullet sleep 임계 매핑(근사는 플래그).
- **분석:** `behavior.py`(반발 apex, 마찰 d(t)를 이산·연속 기준과 비교, 진자 주기(영점 교차), 체인 신장, 하네스 기하 관통(sphere-plane, box corner, SAT), resting jitter, time-to-sleep, 자기 민감도 대비 발산, 최종 z의 Wasserstein). 결정성 분석(DET1)은 M8에 있고 DET2~DET5는 M15다.

**완료 기준** (Normalized, 3엔진)
- `M4.InclineFriction`: 미끄러지는 μ에서 d(2 s)가 이산 기준과 1% 이내. μ ≥ 0.6은 정지 (drift < 0.1 cm).
- `M5.BouncingBalls`: 임계 50 cm/s를 넘는 모든 충돌에서 e_k가 엔진별 등록 허용치(기본 ±0.02) 이내.
- `M6a.Pendulum`: 5°에서 주기 오차 < 0.5%. 기준값 2.00905 s (30°는 2.04305, 90°는 2.37023; g = 980, L = 100, r = 5).
- `M6b`/`M6c`: 체인 신장을 보고하고 에너지가 발산하지 않는다.
- 모든 엔진 간 차이에 by-design / mismatch / algorithmic 태그가 붙는다.
- `M1.SingleBoxDrop` UEParity 대 Normalized 표가 기본값이 만든 차이(예: Jolt damping 0.05)를 보여준다.
- `M2a.Tower` N=10 (UEParity, sleep 켬)에서 PhysX UE4Compat / -Stab / -TGS의 안정화 순서가 01의 프로브와 같은 방향이다 (UE4Compat가 가장 늦게 안정되거나 5 s 안에 잠들지 않음).
- G11 추가 항목: `Bullet.StaticAabbOff`의 해시가 `Bullet.Default`와 같다.
- Bullet viz 드로어를 붙인 run과 붙이지 않은 run의 최종 `h_native`가 같다 (UEParity, sleep 켬).
- `abi-v1.1` 태그. 조인트, 스냅샷, 네이티브 드로 항목에서 "잠정" 표시가 사라진다.

**산출물:** 거동 씬, 조인트 구현 3종, `Config/Profiles/Backend/{Jolt.SharpBoxes,Bullet.StaticAabbOff}.ini`, `Tools/ptbench/analysis/behavior.py`, `Config/Matrices/behavior.json`, `abi-v1.1` 태그, `docs/Scenes.md` 갱신, `docs/Profiles.md`(프로필 축·ini 계층·엔진별 매핑표).

**선행:** M6, M8.

---

### M10. 측정 엄밀성 확장 [확장] (H, 5 pd, 선행 M8)

**목표:** 헤드라인 수치를 주장할 수 있게 하는 장치를 더한다. 정밀 클럭, 단일 스레드 증명, 반복 안정성 게이트, 바이너리 적합성 스탬프, 통계적 주장 규칙, 바이너리 구성 대조군이다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | TSC 클럭: rdtscp + lfence 버킷, run 전체 QPC 보정, 프레임별 QPC 원시값 유지, 읽기 오버헤드 기록, ABI 오버헤드 게이트 | 0.75 |
| 2 | `FThreadCensus`(QueryThreadCycleTime, 센서스 #1/#2), cpu_eff, 외부 스레드 cycle, G10b | 1.0 |
| 3 | ptbench CV 게이트와 sentinel | 0.5 |
| 4 | 적합성 스탬프: M6 `-Conformance` 결과를 스탬프 키로 묶어 `Saved/Conformance/`에 쓰기, 러너의 실행 전 거부, 선택 `/Brepro` | 1.0 |
| 5 | comparability key, block-bootstrap 비율 CI (B = 10000), 주장 규칙 | 1.0 |
| 6 | test-jolt / test-physx / test-bullet 프리셋, prof 프리셋 (Jolt profiler + Tracy) | 0.75 |

작업별 세부:
- **G10b (단일 스레드 증명):** T=1에서 cpu_eff ∈ [0.98, 1.02], 외부 스레드 cycle < 0.5%. listener 0과 드로어 분리는 M7의 G10a가 이미 확인한다. Chaos가 PreInit에서 만드는 TaskGraph 워커가 일하지 않았다는 증명이기도 하다.
- **CV 게이트:** CV > 2%이면 추가 블록을 최대 2회 돌린 뒤 `noisy`로 표시한다. sentinel 셀을 세션 처음·중간·끝에 돌리고 drift > 2%이면 세션 전체에 플래그를 단다.
- **스탬프 키:** {벤치 exe SHA, 백엔드 모듈 SHA (DLL 백엔드는 DLL 파일 해시), `variant_sha256`, `physics_sha256`, `tolerances_sha256`, 게이트 스위트 버전}. 러너는 프로세스를 띄우기 전에 이 값을 다시 계산해 비교한다. 경로는 `Saved/Conformance/` 아래다.
- **comparability key:** sha256(scene_sha256, desc_sha256, physics_sha256, mass_rule, flag_profile, tick_mode, dt, T, sync_policy, mode, instrumentation, harness_commit). 엔진과 변형은 표 안의 시리즈 레이블이다.
- **주장 규칙:** "A가 씬 S에서 B보다 빠르다"는 bootstrap 비율 CI가 1을 포함하지 않고, |ρ − 1| ≥ 3%이고, Shipped와 MatchedAVX2에서 방향이 같을 때만 쓴다. 방향이 다르면 "ISA-dependent"로 표기한다.
- **prof:** 단계 분해 전용이며 헤드라인에는 절대 쓰지 않는다.

**완료 기준**
- TSC–QPC 보정 잔차 < 1e-5. 읽기 오버헤드가 manifest에 있다. Null, 바디 0, 4호출의 ABI 오버헤드 중앙값이 프레임당 100 ns 미만이다 (TSC 배치 측정).
- G10b가 Jolt, Bullet, PhysX의 T=1에서 통과한다.
- 스탬프:
  - `PhysX.UE4Compat.ini`의 키 하나를 바꾸면 해당 셀의 스탬프가 무효가 되고 러너가 프로세스를 띄우기 전에 거부한다.
  - Null DLL을 다시 빌드하면 Null DLL 셀이 거부된다.
  - `tolerances.json`을 바꾸면 모든 셀이 거부된다.
- 주장 규칙을 합성 데이터 세 경우(CI가 1을 포함, |ρ − 1| < 3%, 방향 불일치)로 검증한다.
- CV 재실행 논리와 sentinel drift 플래그를 합성 결과로 검증한다.
- test-<engine> 세 exe와 prof exe가 빌드된다. test 대비 차이를 측정해 기록한다 (판정은 M11).

**산출물:** BenchCore의 TSC·FThreadCensus, Conformance의 G10b와 스탬프 작성기, `Config/Schemas/stamp.schema.json`, ptbench의 CV 게이트·sentinel·comparability·claim 모듈, 프리셋 4개 추가.

**선행:** M8.

---

### M11. Phase-1 캠페인 + 비공개 리포트 [확장] (H 3 pd + 기계 약 6 h, U: 전원 계획 전환, 선행 M9, M10)

**목표:** 세 엔진의 헤드라인, 민감도, 스케일링, 거동 결과를 측정 규약대로 만들어 비공개 리포트로 묶는다.

**작업**

| # | 작업 | 필요한 선행 | pd |
|---|---|---|---|
| 1 | test, test-avx2, test-<engine> exe의 스탬프 갱신 | M10 | 0.25 |
| 2 | 헤드라인: Normalized × test × {`P0.EmptyStep`, `P1.SmallScene` 10/100, `M3.BoxPile` 1k/5k/10k, `M2b.Pyramid1240`} × T=1 × R=10 | M10 | 0.5 |
| 3 | 민감도: 같은 매트릭스를 test-avx2로 | M10 | 0.25 |
| 4 | 맥락: UEParity + sleep 켬, time-to-sleep, active body-step당 비용 | M10 | 0.25 |
| 5 | 스케일링: `M3.BoxPile` 10k와 `M2b.Pyramid1240`에서 T=1..6 | M10 | 0.25 |
| 6 | 바이너리 구성 대조군 (test-<engine> 대 test) | M10 | 0.25 |
| 7 | 거동 리포트 | M9 | 0.5 |
| 8 | 비공개 리포트 조립 (`ptbench report`) | 위 전부 | 0.75 |

- 작업 1~6은 M9 없이 할 수 있다. 이것이 critical path 52 pd의 근거다.
- U는 세션 전에 전원 계획을 High performance 또는 Ultimate로 바꾼다. 기계 시간의 대부분(약 6 h)은 사람이 지켜볼 필요가 없다.

**완료 기준**
- 모든 manifest의 전원 계획이 High performance 또는 Ultimate다.
- 모든 헤드라인 셀이 CV 게이트를 통과하거나 눈에 띄게 `noisy` 표시가 있다. 세션 sentinel drift < 2%.
- 모든 비교 문장이 주장 규칙을 만족하거나 "ISA-dependent"로 표기되어 있다.
- test-<engine>과 test의 차이가 1% 이내다. 넘으면 해당 엔진의 헤드라인을 단일 SDK exe 기준으로 다시 잡는다.
- 엔진마다 effective 설정 표와 오버헤드 모델 (a, b)이 리포트에 있다. PhysX 행에는 "UE4 settings on PhysX 5.11"이 붙는다.
- 명령 하나로 원시 결과에서 리포트를 다시 만들 수 있다.
- 리포트는 비공개다. 공개 여부는 U의 EULA 확인 뒤에 정한다.

**산출물:** `Saved/Results/<session>/`(커밋 안 함), `report/<date>/index.md`와 그림, 비공개 리포트.

**선행:** M9, M10 (성능 부분은 M10만).

---

### M12. 뷰어 2단계 [확장] (H, 3 pd, 선행 M4, M7)

**목표:** 엔진과 변형을 나란히 보고, 측정된 run을 리플레이하고, 프로파일을 본다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | SideBySide와 GhostOverlay(엔진별 tint + COM 궤적). lockstep 씬들. PhysX `UE4Compat`/`-TGS`/`-Stab`은 한 프로세스에서 나란히 돌린다 | 1.0 |
| 2 | `FReplaySource`: `states.ptst`, `contacts.ptcn`을 읽어 그린다. 다른 프로세스에서 돈 Test 구성 run도 이 방식으로 본다 | 1.0 |
| 3 | Profiler 패널(ImPlot, "viz mode, not a benchmark" 표기), Output Log, 네이티브 드로 오버레이 슬롯과 Null의 시험용 네이티브 드로 출력, 비동기 PNG 인코딩, WARP 골든 | 1.0 |

- 같은 프로세스에 둘 수 없는 조합은 거부한다: PhysX `SdkDefault`와 (100, 1000) 변형(PxTolerancesScale이 다름), Bullet 두 프로필(전역 결합 콜백).
- 네이티브 드로 오버레이 슬롯은 Null이 내는 합성 `PtDebugDrawCmd`로 검증한다. 실제 엔진의 네이티브 드로는 M9가 끝났으면 자동으로 보인다 (완료 기준에 넣지 않는다).

**완료 기준**
- dev 프리셋으로 돌린 behavior 모드 `M3.BoxPile` 1k run을 리플레이하면 frame 300의 포즈가 같은 빌드의 라이브 뷰어 run과 비트 단위로 같다. test 프리셋 run 리플레이에는 구성 이름이 표시된다.
- SideBySide 3레인 × `M3.BoxPile` 5k가 일시정지 또는 리플레이 상태에서 60 fps 이상이다. 라이브 fps는 표시만 한다.
- PhysX `UE4Compat`/`-TGS`/`-Stab` 세 레인이 한 프로세스에서 lockstep으로 돈다. 거부해야 할 조합은 메시지와 함께 거부된다.
- `S00.FreeFall` frame 30의 WARP 렌더가 골든 PNG와 허용 픽셀 차 이내다.
- 뷰어에서 나온 manifest는 `mode = viz`이고 M8 validate에서 거부된다.
- Null 합성 네이티브 명령이 오버레이 슬롯에 그려진다.

**산출물:** `Source/Editor/PhysicsTestEditor`의 2단계 패널과 `ReplaySource`, WARP 골든 이미지.

**선행:** M4, M7.

---

### M13. Chaos 백엔드 [확장] (U 6~10 pd + H 2 pd, 선행 M6, M7, M8, 재캠페인은 M11 이후)

**목표:** 사용자가 동결된 ABI 위에 Chaos 백엔드를 구현하고, 하네스가 측정과 리포트에 Chaos 행을 붙인다. 헤드라인 층은 `FChaosScene` + `FChaosEngineInterface` 경로이고, `FPBDRigidsEvolutionGBF` 단독 경로는 진단이다.

**U 작업 (6~10 pd, 세부 배분은 U가 정한다)**
- `FPtChaosBackend`: InitGamePhys 순서(M2.5와 같음), 금지 호출 준수.
- `FPtChaosScene`:
  - CreateScene: `FChaosScene(nullptr, AsyncDt 또는 -1)`. 동기 모드에 0을 넘기지 않는다. T=1과 T ≥ 2의 스레드 설정은 ChaosBackendGuide를 따른다. 생성 뒤 SolverOptions를 `ApplyConfig` + `SetIsDeterministic`으로 명시 적용한다.
  - CreateActors: UE 순서, TBox 마진 규칙, `uint64` 필터, 모든 셰이프에 머티리얼, 받은 질량을 그대로 사용.
  - 프레임: Native driver, `NumStepsTaken` 반환.
  - 읽기, 명령, 해시, 스냅샷(Chaos 규약), `FChaosDDScene` → `PtDebugDrawCmd`(Development만), SetOption(CVar), `DumpEffectiveSettings`(`threads.single_worker`, `threads.max_num_workers` 포함), `PtChaosDefaults.h`(모든 기본값에 file:line 근거), PHASES용 sim callback.
- `FPtChaosEvolutionScene`(진단).
- 조인트, 스냅샷, 네이티브 드로 구현은 `abi-v1.1`(M9) 이후에 한다.

**H 작업 (2 pd)**

| # | 작업 | pd |
|---|---|---|
| 1 | run mode에 따른 Development/Test DLL 선택, `-corelimit`, `p.Chaos.SingleWorkerPhysics`, `p.Chaos.MaxNumWorkers` 연결 | 0.5 |
| 2 | Chaos manifest 필드(UE 5.8.3, UBT 구성, FPSemantics, MinCpuArchX64, ISPC, threading, 할당자), `.rsp` 기반 G0 | 0.5 |
| 3 | 리포트 수준 분리 (Frame / Solver / Evolution / 통합 비용) | 0.5 |
| 4 | `UE_RUNTIME` 종료 경로 점검 (`exit.json` + `TerminateProcess`), 선택: `PhysicsTestChaosAvx2` 타깃과 Shipping 민감도 빌드 연결 | 0.5 |

**완료 기준 (a) 백엔드 적합성** (M6, M7, M8 산출물만 사용)
- Test와 Development DLL이 선언한 capability 범위에서 A0~A7, A9, A10을 통과한다.
- G0(`.rsp` 기준), G1~G7, G8(DET1), G9, G10a, G11(접촉 캡처), G12를 통과한다.
  - G1: |z60 − 501.83328| ≤ 2e-3 cm.
  - G2: 질량 readback이 하네스 값과 같다 (^0.75로 다시 계산하지 않았다는 증거).
  - G6: `S08.StartAsleep`의 `bStartAwake = false` 바디가 frame 0 `ReadBodyStates`에서 Sleeping이다.
- A10: Native driver의 `NumStepsTaken`이 Sync, Substepping(UE5 규칙), AsyncFixed 계획과 같다. AsyncFixed 결과에는 "보간된 GT 상태, 한 스텝 지연" 표시가 붙고 바디별 거동 비교에서 빠진다.
- T=1 run의 effective에 `threads.single_worker = 1`이 READBACK으로 있다.
- 벤치 프로세스가 `exit.json`을 남기고 `TerminateProcess`로 끝나며, 러너가 결과를 정상으로 읽는다.

**완료 기준 (b) 잠정 항목** (M9 이후. 오버레이 확인은 M4 뷰어를 쓰므로 M4에도 의존한다)
- 조인트 씬(`M6a`/`M6b`/`M6c`)이 돌고 조인트 스냅샷이 나온다.
- Development 뷰어(M4)에서 네이티브 ChaosDD 오버레이가 하네스 드로와 색과 형상이 일치한다.

**완료 기준 (c) 캠페인 통합** (M10, M11 이후)
- G10b: T=1 SingleThread + SingleWorkerPhysics에서 cpu_eff ∈ [0.98, 1.02], 외부 스레드 cycle < 0.5%.
- 스탬프 키에 Chaos DLL 파일 해시가 들어가고, DLL을 다시 빌드하면 스탬프가 무효가 된다.
- 헤드라인과 스케일링 매트릭스를 Chaos 포함으로 다시 돌리고, Frame 수준과 Solver 수준을 한 표에 섞지 않는다.

**산출물 (U):** `Unreal/ChaosBridgeHost/Source/PhysicsTestChaos/Private/*` (UE 헤더를 include하는 글루만 커밋, 바이너리와 생성 파일은 커밋하지 않음). **(H):** Chaos manifest 필드, 리포트 수준 분리, `docs/ChaosBackendGuide.md` 최종판.

**선행:** M6, M7, M8. 재캠페인은 M11 이후.

---

### M14. OOP 전송 [확장, 조건부] (H 4 pd, 선행 M2.5 결과)

**트리거:** SP2 또는 SP3 실패, 또는 사용자 요청. 열리지 않으면 `PtAbiCodec`과 A8은 M15 백로그로 간다.

**작업**

| # | 작업 | pd |
|---|---|---|
| 1 | `PtAbiCodec` + loopback (모든 호출을 arena로 인코드 후 같은 스레드에서 디스패치), A8 | 1.0 |
| 2 | shared memory mailbox (`Local\PT.<pid>.<nonce>`)와 frame ring | 1.0 |
| 3 | 원격 API 클라이언트: 서버 실행, 핸드셰이크, 사망 감지 | 0.75 |
| 4 | `PtBackendServer.exe`와 `RunBackendServer` (UBT `PhysicsTestChaosServer` 타깃과 공유) | 0.5 |
| 5 | OOP 적합성 검사와 오버헤드 측정 | 0.75 |

**완료 기준**
- Null이 loopback과 OOP 두 경로에서 A0~A7과 A8(직접 호출과 비트 동일)을 통과한다. 착수 시점에 끝난 SDK 백엔드(M3 이후면 Jolt, M5 이후면 Bullet·PhysX)도 같은 검사를 통과한다. M6 이후에 착수하면 G1~G7도 OOP로 통과한다.
- 서버 안에서 잰 OOP 타이밍이 in-process와 2% 이내다. IPC는 타이밍 구간에 들어가지 않는다.
- 서버를 죽이면 1 s 안에 `PT_E_TRANSPORT`가 돌아온다.

**선행:** M2.5 결과.

---

### M15. 확장 백로그 [확장] (H, 약 20 pd, 선행 M11)

M11 결과를 보고 우선순위를 정한다. 후보 합계는 약 24 pd이며, 그중 약 20 pd 범위에서 고른다.

| 항목 | 공수 (pd) |
|---|---|
| Mode C iso-quality sweep + Pareto | 4 |
| convex quickhull, convex 질량 적분, convex·trimesh 패리티 정책 (sharp vs rounded) | 4 |
| CCD `E1`, `E9.OverlapRecovery`, `E11.SeaOfStatics` | 2 |
| BulletMt 백엔드 (BT_THREADSAFE, ODR을 피하려고 별도 DLL) | 2 |
| soft limit과 drive (UE 강성 의미) | 3 |
| scene query 벤치 | 3 |
| Jolt 접촉 임펄스 로컬 패치 (U 승인 시) | 1 |
| PhysX 3.4 DLL 백엔드 (타임박스) | 2 |
| DET2~DET5 (T 스윕, 생성 순서 셔플, 플래그 프로필 간, 엔진 결정성 스위치), double 빌드 | 2 |
| `PtAbiCodec` + A8, A4 1만 개 fuzz (M14가 열리지 않은 경우) | 1 |

---

## 4. 완료 기준 의존성 점검

각 마일스톤의 완료 기준이 쓰는 이전 산출물이 선행 열(전이 포함) 안에 있는지 점검한 표다. 이전 초안에서 앞당기거나 뒤로 미룬 항목도 함께 적는다.

| M | 선행 (전이 포함) | 완료 기준이 쓰는 이전 산출물 | 이전 초안 대비 조정 |
|---|---|---|---|
| M0 | - | 없음 | 프리셋을 dbg/dev/test로 줄임 (test-avx2 → M7, prof·test-<engine> → M10) |
| M1 | M0 | Core, Automation 골격 | quickhull·convex 질량 → M15 |
| M2 | M0, M1 | M1 타입, FStepPlanner | `S00` 씬 대신 자체 자유낙하 픽스처. loopback·A8 → M14. 1만 개 fuzz → 표 기반 약 30개. TSC 오버헤드 게이트 → M10 |
| M3 | M0~M2 | ABI, Kit, A 게이트, Null, 픽스처 | `S00`, `M1.SingleBoxDrop`, `M3.BoxPile`을 여기서 만든다 (이전에는 M4·M6·M8) |
| M4 | M0~M3 | Jolt 백엔드와 스냅샷, 씬 3개 | SideBySide·GhostOverlay·Replay·WARP 골든 → M12. 성능 기준을 일시정지 상태로 바꿈. 렌더러 인자 순서 재확인 작업 삭제 (이미 [V]) |
| M5 | M0~M3 | ABI, BackendCommon 골격, 씬, 최소 벤치 | 뷰어 확인은 체크포인트 A로. PhysX D6 → M9 |
| M6 | M0~M3, M5 | 세 SDK 백엔드와 스냅샷, Null 프레임 모델(A10) | G0·G8·G10a·G11 → M7, G10b → M10, 스탬프 → M10 (`-Conformance`의 `conformance.json`은 M6). 조인트·스냅샷·네이티브 드로는 잠정 |
| M2.5 | M0~M2 | Kit, A 게이트, 자유낙하 픽스처 | SP5의 G1은 M6 게이트 스위트가 아니라 M2 픽스처에 G1 허용치를 적용한다 |
| M7 | M0~M3, M5, M6 | 게이트 스위트, 씬, 백엔드 | ThreadCensus·TSC → M10 |
| M8 | M7까지 | manifest, recorder, `exit.json`, test-avx2 | 전원 계획은 보고만 (이전에는 M7 완료 기준이 U의 전원 계획 전환에 의존) |
| M9 | M6, M8까지 | 게이트, ptbench, recorder | 뷰어를 쓰지 않는다 (네이티브 드로는 bench exe viz 모드로 검증). 결정성 분석은 M8, DET2~DET5는 M15 |
| M10 | M8까지 | ptbench, 러너, 게이트 | - |
| M11 | M9, M10까지 | 뷰어를 뺀 전부 | 성능 부분은 M9 없이 가능함을 명시 |
| M12 | M4, M7까지 (M5 포함) | 뷰어 1단계, recorder, PhysX 변형 | 네이티브 오버레이는 Null 합성 명령으로 검증 |
| M13 | M6, M7, M8까지 | (a)는 동결 ABI 핵심, 게이트, 러너, ptbench만 쓴다 | 완료 기준을 (a) 적합성, (b) 잠정 항목 (M9와 M4 뷰어에 의존함을 명시), (c) 캠페인 (M10, M11에 의존함을 명시)으로 나눔 |
| M14 | M2.5 결과 | A 게이트, Null (SDK 백엔드와 G 스위트는 착수 시점에 끝난 것만) | 조건문으로 적은 예외 (§0) |
| M15 | M11 | - | - |

---

## 5. 게이트·씬·확장 항목의 배치

### 5.1 게이트

| ID | 내용 | 마일스톤 |
|---|---|---|
| A0~A7, A9 | ABI 수명, 버전, 스레드, phase, 입력 검증, 용량, FP 환경, effective 키, DLL 위생 | A0~A7: M2 (Null), M3 (Jolt), M5 (Bullet, PhysX), M13 (Chaos). A9는 DLL 경로만: M2 (Null DLL), M13 (Chaos DLL) |
| A10 | 프레임 모델: Null Native driver의 `NumStepsTaken == Plan.N` (Sync, Substepping UE5, AsyncFixed), FixedStep·Sync에서 두 driver의 최종 상태 비트 동일 | M2에서 사례 작성, 게이트 단계는 M6 (동결 조건), M13 (Chaos) |
| A4 | 표 기반 음성 사례 약 30개 (1만 개 fuzz는 M15) | M2 |
| A8 | loopback 비트 동일 | M14 (열리지 않으면 M15) |
| G0 | 빌드 자기보고 | M7 (Chaos는 `.rsp`, M13) |
| G1, G1b, G2~G7, G9, G12 | 해석해·규약·readback·용량 | M6 |
| G6의 `S08.StartAsleep` | `bStartAwake = false` → frame 0 Sleeping | M6 |
| G8 (DET1) | fresh process 3회 동일 해시 | M7 |
| G10a | 벤치 위생 (listener 0, 드로어 분리, 계측 tier 0) | M7 |
| G10b | 단일 스레드 증명 (cpu_eff, 외부 스레드 cycle) | M10 |
| G11 | 접촉 캡처 on/off 비트 동일 / `Bullet.StaticAabbOff` 해시 동일 | M7 / M9 |
| SP1~SP5 | Chaos 스파이크 | M2.5 |
| DET2~DET5 | T 스윕, 생성 순서 셔플, 플래그 프로필 간, 엔진 결정성 스위치 | M15 |

### 5.2 씬

| 씬 | 마일스톤 | 카탈로그 |
|---|---|---|
| `S00.FreeFall` | M3 | 방법론 §5.1의 S00 보정 씬(SI)을 cm·g = 980으로 다시 정의한 하네스 씬. 카탈로그 확장으로 `docs/Scenes.md`에 등록 |
| `M1.SingleBoxDrop`, `M3.BoxPile` | M3 | 방법론 카탈로그 |
| `S00d`, `S01`~`S08` | M6 | 카탈로그 확장 (`docs/Scenes.md`) |
| `P0.EmptyStep`, `P1.SmallScene` | M7 | 카탈로그 확장 |
| `M2b.Pyramid1240` | M7 | 방법론 카탈로그 |
| `M2a.Tower`, `M4.InclineFriction`, `M5.BouncingBalls`, `M6a`/`M6b`/`M6c` | M9 | 방법론 카탈로그 (`M4`의 이름은 `InclineFriction`이다) |
| `M7.UEDefaults` | M9 | 카탈로그 확장 |
| `E1`, `E9`, `E11` 등 | M15 | 방법론 확장 세트 |

### 5.3 [확장]으로 미룬 항목

| 항목 | MVP에서의 처리 | 확장 위치 |
|---|---|---|
| 클럭 | QPC (`std::chrono::steady_clock`) | rdtscp + QPC 보정: M10 |
| 스레드 위생 | 엔진 스레드 설정 기록 (M7의 G10a: listener 0, 드로어 분리) | FThreadCensus, cpu_eff, G10b: M10 |
| 반복 안정성 | (M8) 셀별 CV 보고 | CV 게이트, sentinel: M10 |
| 통계와 주장 | 중앙값, p95 | bootstrap CI, 주장 규칙, comparability key: M10 |
| 적합성 스탬프 | 게이트를 automation과 `-Conformance`(`conformance.json`)로 실행 | 스탬프와 스탬프 키: M10 |
| 바이너리 구성 대조군 | - | test-<engine>: M10 |
| 프리셋 | dbg, dev, test | test-avx2: M7, prof·test-<engine>: M10 |
| loopback과 A8 | - | M14 (조건부) 또는 M15 |
| A4 fuzz | 표 기반 음성 사례 약 30개 | 1만 개 fuzz: M15 |
| OOP 전송 | - | M14 (조건부) |
| GhostOverlay, Replay, WARP 골든 | - | M12 |
| Mode C iso-quality sweep | - | M15 |
| convex quickhull과 convex 질량 | 데이터 모델과 ABI에만 존재 | M15 |
| BulletMt, PhysX 3.4, double 빌드 | - | M15 |

---

## 6. 리스크 (일정 관점)

| # | 리스크 | 영향 | 징후 (트리거) | 대응 | 일정 영향 |
|---|---|---|---|---|---|
| R1 | Chaos UBT Program DLL 경로가 실증되지 않음 | M13 | SP1~SP3 실패 | M2.5를 일찍 한다. 게이트별 대안(.def, 저장소 밖 프로젝트, OOP, Shipping + logging) | Phase 1 영향 없음. 실패 시 M14 +4 pd |
| R2 | 단위·규약 버그가 엔진 차이처럼 보임 | M3~M6 | S00 오차, 법선 부호, 질량 readback 불일치 | 체크포인트 A에서 1차 확인, M6 게이트 전에는 어떤 비교 수치도 보고하지 않음 | 버퍼 안 |
| R3 | 측정 노이즈 (P/E 하이브리드, Balanced, 배경 부하로 최대 2.5배 [V]) | M8, M10, M11 | CV > 2%, sentinel drift | U의 전원 계획 전환, M8 CV 보고, M10 CV 게이트와 sentinel | M11 기계 시간 최대 2배 (약 12 h) |
| R4 | M5 공수 초과 (PhysX UE4 세부, 변형 4종) | M5 | 7 pd 시점에 PhysX UE4Compat가 S00과 `M3.BoxPile`을 통과하지 못함 | 프로토타입 재사용, [M] 값은 ini 키로. 레버: `-TGS`/`-Stab` 변형을 M9로 미룸 (약 1 pd 절약) | +1~2 pd |
| R5 | M4 렌더러·뷰어 공수 초과 | M4 | 3.5 pd 시점에 렌더러 미완 | `proto_render.cpp` 이식. 레버: Console 패널과 Inspector를 M12로 (약 0.75 pd 절약) | +1 pd |
| R6 | PhysX FetchContent가 부모 플래그를 바꿈 | M0 | PhysX on/off에 따라 다른 TU의 `BuildFlags.json`이 달라짐 | `PhysXBootstrap` 경로로 전환 (프로토타입 검증됨) | +0.5 pd |
| R7 | ABI 동결 뒤 변경 | M6~M13 | 조인트·스냅샷·네이티브 드로 계약 변경 | 핵심만 v1.0 동결, 세 항목은 M9까지 잠정, v1.x append-only | Chaos 재빌드 1회 |
| R8 | UE 4.27 대조로 PhysX 레시피가 바뀜 | M5, M11 | [M] 키가 4.27과 다름 | 모든 [M] 값이 ini 키. 코드 변경 없음, 해당 변형 스탬프 재생성 | 게이트 재실행 기계 시간 |
| R9 | UE-in-DLL 종료 시 크래시나 행 | M2.5, M13 | 결과 기록 뒤 비정상 종료 코드 | `exit.json` + `TerminateProcess`, 러너는 `exit.json`을 믿음 | 없음 |
| R10 | 환경 미비 (Python 패키지, 디버거 없음) | M0, M8 | `ModuleNotFoundError`, 디버깅 불가 | M0에서 lock 고정 `.venv`(필요하면 wheelhouse)와 디버거를 준비 | M0에 포함 |
| R11 | 뷰어 라이브 시뮬레이션 fps가 낮음 | M4, M12 | Bullet 10k 스텝 약 60 ms 이상 [R, 3008 박스 18.1 ms에서 외삽] | 성능 기준은 일시정지·리플레이. 라이브 fps는 표시만 | 없음 |
| R12 | 범위 확장 | 전체 | 새 씬·엔진·기능 요청 | M15 백로그로 격리, [MVP]/[확장] 태그 | - |
| R13 | 1인 가용성과 추정 오차 | 전체 | 누적 실적이 계획의 1.2배 초과 | 20% 버퍼, 체크포인트 A에서 재추정, 버퍼가 바닥나면 M12를 M15로 | 최대 약 3 pd 회수 |

---

## 7. 바로 다음 할 일 (M0 착수 체크리스트)

1. **스크래치 프로토타입을 저장소로 복사한다 (가장 먼저).** 스크래치 디렉터리는 세션 임시 폴더라 언제든 지워질 수 있다. 모두 우리가 쓴 코드이며 UE 코드는 없다.

   ```powershell
   $SP = "C:\Users\chay1\AppData\Local\Temp\claude\D--PhysicsTest\ecbad32c-0f99-4bf8-a174-58c2620a1f27\scratchpad"
   $R  = "D:\PhysicsTest"
   New-Item -ItemType Directory -Force "$R\cmake\ThirdParty", "$R\Tools\Prototypes" | Out-Null
   Copy-Item "$SP\build-jolt\hello\cmake\Jolt.cmake"                 "$R\cmake\ThirdParty\Jolt.cmake"
   Copy-Item "$SP\build-bullet\hello\cmake\PtBullet.cmake"           "$R\cmake\ThirdParty\PtBullet.cmake"
   Copy-Item "$SP\build-physx\superbuild\cmake\PhysXBootstrap.cmake" "$R\cmake\ThirdParty\PhysXBootstrap.cmake"
   Copy-Item "$SP\render-deps\proto\src\proto_render.cpp"            "$R\Tools\Prototypes\proto_render.cpp"
   Copy-Item "$SP\render-deps\proto\CMakeLists.txt"                  "$R\Tools\Prototypes\proto_unified_CMakeLists.txt"
   Copy-Item "$SP\build-jolt\hello\seam"                             "$R\Tools\Prototypes\jolt_seam" -Recurse
   Copy-Item "$SP\build-jolt\hello\main.cpp"                         "$R\Tools\Prototypes\hello_jolt.cpp"
   Copy-Item "$SP\build-bullet\hello\src\hello_bullet.cpp"           "$R\Tools\Prototypes\hello_bullet.cpp"
   Copy-Item "$SP\build-physx\superbuild\main.cpp"                   "$R\Tools\Prototypes\hello_physx.cpp"
   ```

   - 앞의 네 파일이 핵심이다. 나머지는 통합 빌드의 URL_HASH 값, 플래그 누수 프로브, 스모크 테스트의 출발점이다.
   - 오프라인 빌드를 원하면 `$SP\render-deps\tarballs`의 tarball을 저장소 밖 캐시(예: `D:\PhysicsCache\tarballs`)로 옮기고 URL 캐시 변수로 가리킨다. imgui·implot·stb는 URL_HASH와 같다 [V]. 나머지 셋은 헤더의 버전 매크로로 확인했다 [V]: nlohmann/json 3.12.0 (`NLOHMANN_JSON_VERSION_*`), doctest 2.5.3 (`DOCTEST_VERSION_*`, `check()` 멤버는 doctest.h:1647), Tracy 0.14.1 (`TracyVersion.hpp`). 이 셋의 tarball이 GitHub archive URL과 바이트 단위로 같은지는 확인하지 않았으므로, URL_HASH는 처음 받을 때 계산해 고정한다.
   - `Tools/Prototypes`는 M4가 끝나면 지운다.
2. **첫 커밋을 만든다.** `docs/` 전체(01~04, appendix, images)와 `.gitignore`, `.gitattributes`만 담는다. 첫 push 전에 저장소 공개 여부(01 Q1)를 U가 정한다. 이후 작업은 `m0/scaffold` 브랜치에서 한다.
3. **`Jolt.cmake`를 고친다.** 캐시 변수를 `PT_JOLT_GIT`으로 바꾸고, `GIT_TAG v5.6.0` + `GIT_SHALLOW`를 커밋 SHA `e77f175595e64cb44218cc9d9d56fc365ad0e36a`로 바꾼 뒤 `git rev-parse HEAD` 가드를 단다. `block()` 안의 옵션과 PUBLIC `/arch:AVX2`는 그대로 둔다.
4. **`PtBullet.cmake`를 고친다.** `PT_BULLET_ROOT` + `add_subdirectory`를 M0 작업 4의 FetchContent 형식(`SOURCE_SUBDIR _none_` + `add_subdirectory(... EXCLUDE_FROM_ALL SYSTEM)`)으로 바꾼다. `PT_BULLET_MULTITHREADING` 기본값을 OFF로 바꾸고 PPL 옵션을 뺀다. CACHE FORCE 매크로, `/wd4819`, postfix 선점은 그대로 둔다.
5. **`PhysX.cmake`를 새로 쓴다.** `proto_unified_CMakeLists.txt`의 PhysX 블록을 출발점으로 삼고, `GIT_TAG`을 da950a35로 고정하고 `PT::PhysX`(`physx_lib` + `PX_PHYSX_STATIC_LIB` + `DISABLE_CUDA_PHYSX`)를 만든다. `PhysXBootstrap.cmake`에서는 `RelWithDebInfo → profile` 매핑을 지우고 debug 라이브러리가 없으면 실패하게 한다.
6. **프리셋과 툴체인을 쓴다.** `CMakePresets.json`(dbg/dev/test, `out/${presetName}`), `CMakeUserPresets.json.example`(file:///D:/PhysicsRefs URL 세 개, `PT_UE_ROOT`), `Tools/cmake.cmd`(vcvars64 + `%USERPROFILE%\.cmake-deps`의 CMake/Ninja), `PTToolchain.cmake`(/MD를 SDK보다 먼저, /fp:fast, RelWithDebInfo FATAL, `pt_target_defaults()`).
7. **작은 의존성과 빌드 자기보고를 붙인다.** `ImGui.cmake`, `SmallDeps.cmake`(URL + URL_HASH), `DumpBuildFlags.cmake`(빌드 단계 `add_custom_command`, `compile_commands.json`에 DEPENDS).
8. **Core 최소판과 테스트 골격을 만든다.** UE 수학 타입, `UE_LOG`, `check`/`verify`/`ensure`, `FCommandLine`/`FParse`, `FPlatformTime`, 하네스 측 `PT_THIRD_PARTY_INCLUDES_START/END`(push_macro), `PhysicsTestAutomation` 골격. `hello_*.cpp`를 Z-up, g = 9.80 스모크 테스트 3종으로 옮긴다.
9. **CTest 프로브를 단다.** Core → doctest 컴파일, `jolt_seam` 기반 플래그 누수 프로브, `D:\PhysicsRefs` 세 저장소의 `git status --porcelain`, 가장 긴 오브젝트 경로.
10. **Python 환경을 만든다.** `py -3.11 -m venv .venv`, `pip install -r Tools/ptbench/requirements.lock`(PyPI, 고정 버전). 오프라인 재설치가 필요하면 `pip download -d Tools/ptbench/wheelhouse -r Tools/ptbench/requirements.lock`로 받아 두고 `pip install --no-index --find-links Tools/ptbench/wheelhouse -r Tools/ptbench/requirements.lock`로 설치한다. wheelhouse는 git이 무시한다.
11. **디버거를 정한다 (U, 01 Q7).** VS Code + C/C++ 확장, VS 2022 IDE, 이미 설치된 CLion 중 하나를 고르고, dbg 프리셋 스모크 exe에서 중단점을 확인한다.
12. **M0 완료 기준 12개를 차례로 확인한다.** 빌드 시간과 `BuildFlags.json` 요약을 README 환경 절에 적고, 커밋하고 `m0` 태그를 단다.
