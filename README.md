# PhysicsTest

Unreal Engine의 **Chaos**가 **Bullet**, **PhysX**, **Jolt**보다 실제로 느린지, 그리고 같은 입력에서 시뮬레이션 **결과가 어떻게 다른지**를 측정하는 Windows용 테스트 환경입니다.

- **빌드:** CMake + Ninja + MSVC (Windows 전용)
- **시각화:** Chaos Debug Draw 스타일의 단순한 와이어프레임 뷰어 (Win32 + Direct3D 11 + Dear ImGui)
- **구조:** Unreal 방식(UE 이름과 기본값, `FPhysScene` / `FBodyInstance` / `FPhysicalMaterial` …)을 따르되 Actor/Component는 없앴습니다. 렌더 씬과 물리 씬도 나누지 않고 하나로 둡니다.
- **백엔드:**

| 백엔드 | 버전 | 상태 |
|---|---|---|
| Bullet | 3.25 (`2c204c49`) | M5: 단일 스레드 `btDiscreteDynamicsWorld`, 벤치·뷰어에서 사용 가능 |
| PhysX | 5.11.0 (`da950a35`), UE4 방식 설정 (`UE4Compat`, `UE4Compat-TGS`, `UE4Compat-Stab`) + SDK 기본값 (`SdkDefault`) | M5: 변형 4종, 벤치·뷰어에서 사용 가능 |
| Jolt | 5.6.0 (`e77f1755`) | M3: 벤치·뷰어에서 사용 가능 |
| Chaos | UE 5.8.3, UBT Program DLL + C ABI | 나중에 직접 구현 |

> **현재 상태 (2026-10-22):** M10(측정 엄밀성 확장)까지 마쳤습니다. 타이밍 버킷은 TSC(rdtscp + lfence)이고 run 전체 QPC 보정으로 ns가 됩니다 (보정 잔차 최대 2.1e-6 < 1e-5, 읽기 오버헤드 TSC 13.4 ns). 스레드 센서스가 run마다 cpu_eff와 외부 스레드 cycle을 재고 T = 1의 단일 스레드 증명 G10b가 Jolt·Bullet·PhysX에서 통과합니다 (cpu_eff 1.0000, 외부 0%). `PhysicsTestBench -Conformance`가 적합성 스탬프(`Saved/Conformance/<키>.json`: exe·DLL·변형·물리 프로필·tolerances 해시, 게이트 스위트 버전, 오버라이드)를 쓰고, `ptbench run`은 perf 매트릭스에서 스탬프가 없거나 낡은 셀이 하나라도 있으면 프로세스를 띄우기 전에 거부합니다 (determinism·behavior 매트릭스는 스탬프 상태를 기록만 합니다; `PhysX.UE4Compat.ini` 키 하나 → PhysX 셀, Null DLL 다시 빌드 → Null DLL 셀, `tolerances.json` → 모든 셀). 러너에 CV 게이트(CV > 2%면 추가 블록 최대 2번, 그래도 넘으면 `noisy`)와 sentinel(처음·중간·끝, drift > 2%면 세션 플래그)이 생겼고, 분석은 comparability key마다 표 하나, bootstrap 95% CI(B = 10000), 주장 규칙(block-bootstrap 비율 CI, |ρ − 1| ≥ 3%, Shipped·MatchedAVX2 방향 일치)과 바이너리 구성 대조군 표를 냅니다. 새 프리셋 `test-jolt`·`test-physx`·`test-bullet`(단일 SDK)과 `prof`(Tracy v0.14.1 + Jolt external profile, 헤드라인 아님)가 생겼습니다. 이 PC는 Balanced라 **모든 타이밍은 여전히 비공식입니다** (manifest `unofficial`이 이제 판정이고 남은 사유는 전원 계획뿐입니다. High performance 세션은 M11). 완료 기준 표는 [docs/Progress.md](docs/Progress.md) M10, 비공식 리포트 예시는 [docs/reports/m10_smoke/](docs/reports/m10_smoke/)입니다. 다음 단계는 M11(Phase-1 캠페인)입니다. 그 전 단계: M9(거동 씬 + ball/hinge 조인트 3엔진 + 프로필 완성 + 거동 분석 + **ABI v1.1 확정**)까지 마쳤습니다. Jolt·Bullet·PhysX가 ball/hinge 조인트와 조인트 스냅샷을 내고(`PT_CAP_JOINT_*`), PhysX·Bullet이 viz run의 네이티브 디버그 드로를 냅니다. 조인트·디버그 스냅샷·네이티브 드로의 잠정 표시를 지우고 ABI를 v1.1로 확정했습니다 (레이아웃은 v1.0 그대로, v1.1이 최종이며 태그 `abi-v1.1`은 리드가 M9 커밋에 답니다, [docs/Abi.md](docs/Abi.md) §9.2·§9.4). UEParity에서 Jolt·Bullet이 UE 머티리얼 sleep 임계를 근사 매핑하고([docs/Profiles.md](docs/Profiles.md) §5.1), 거동 씬 7개를 `Config/Matrices/behavior.json`(552 run, 세션 `behavior-m9r1`)으로 돌려 `ptbench behavior`가 해석 기준·하네스 기하로 재고 엔진 간 차이마다 by-design / mismatch / algorithmic 태그를 붙입니다 (설명 없는 차이 0). 03 M9 완료 기준은 리드 결정(2026-10-22)대로 판정합니다: `M6a` 5° 주기는 카탈로그 씬(hinge)으로, PhysX PGS·TGS의 100:1 체인 에너지 증가는 받아들인 엔진 거동("fail — accepted engine behaviour"), Bullet의 체인은 별도 발견(조인트 분리), G11 `Bullet.StaticAabbOff`는 좁힌 범위, 실행 뒤 등록은 모두 승인(`pending_sign_offs` 0). 완료 기준 표와 엔진별 발견은 [docs/Progress.md](docs/Progress.md) M9, 비공식 거동 리포트 예시는 [docs/reports/m9_behavior/](docs/reports/m9_behavior/)입니다. 그 전 단계: M8(오케스트레이션 + 분석 기본)까지 마쳤습니다. `Tools/ptbench`(Python, `.venv`에 editable 설치, `python -I -m ptbench`)가 읽기 전용 환경 게이트(전원 계획이 High performance/Ultimate가 아니면 `--allow-balanced` 없이는 거부, 유휴, 다른 PhysicsTest 프로세스), 매트릭스(`Config/Matrices/{smoke,determinism}.json`)의 seed 고정 블록 셔플·fresh process·쿨다운·타임아웃·종료 코드 4 재시도, `exit.json` 판정(원시 종료 코드는 기록만), `session.json`·`index.csv`, 분석 load/validate/perf/report/determinism과 C++와 1e-9 안에서 같은 `analytic.py`를 갖췄습니다. smoke 매트릭스(4 엔진 변형 × P0, BoxPile 1k·5k × R = 5)가 무인으로 돌아 리포트(`report/<날짜>/index.md` + PNG)를 만듭니다. 이 PC의 전원 계획은 Balanced라 모든 리포트 머리에 그 사실이 표시되고, **모든 타이밍은 비공식입니다** (스탬프·CV 게이트는 M10, High performance 세션은 M11). 결과는 [docs/Progress.md](docs/Progress.md) M8, 비공식 리포트 예시는 [docs/reports/m8_smoke/](docs/reports/m8_smoke/)입니다. 다음 단계는 M9(거동 씬 + 조인트)·M10(측정 엄밀성)입니다. 그 전 단계: M7(벤치 러너 + 측정 프로토콜)까지 마쳤습니다. `PhysicsTestBench.exe`가 1 run = 1 process 실행 순서(호스트 제어 → preflight G0·G9·G10a → spin pre-roll → 측정 루프 → `exit.json`)를 따르고, 종료 코드 0/2/3/4/5/6을 모두 `exit.json`에 남기며, `manifest.json`(`physicstest.run/2`, `Config/Schemas/manifest.schema.json`), `hashes.csv`, `states.ptst`, `contacts.ptcn`을 씁니다. 결정성 1단계(G8, fresh process 3회)와 결과 중립 knob(G11, 접촉 캡처)이 모든 백엔드·변형에서 통과하고, 성능 씬 `P0.EmptyStep`, `P1.SmallScene`, `M2b.Pyramid1240`, 스레드 매핑 T = 1..6, `test-avx2` 프리셋이 생겼습니다. 측정 규약은 [docs/TimingContract.md](docs/TimingContract.md), 결과 형식은 [docs/ResultsSchema.md](docs/ResultsSchema.md)입니다. **타이밍은 여전히 비공식입니다** (적합성 스탬프와 cpu_eff는 M10). 오염 판정의 문턱(코어당 5% × run의 코어 수)은 M8에서 리드가 유지로 정했고, 러너가 종료 코드 4를 다시 돕니다. 그 전 단계 요약: M6(물리 게이트 + 게이트 씬 + **ABI 핵심 v1.0 동결** + ChaosBackendGuide 초안)까지 마쳤습니다. Jolt, Bullet, PhysX 변형 4종이 UEParity·Normalized에서 물리 게이트 G1~G7, G9, G12를 ABI로 통과하고 (`PhysicsTest.Gates.*`, `PhysicsTestBench -Conformance` → `conformance.json`), 엔진별 편차는 `Config/Golden/tolerances.json`에 사유와 함께 있습니다. C ABI는 v1.0(major 1)으로 동결했고 조인트·디버그 스냅샷·네이티브 드로만 M9까지 잠정입니다 ([docs/Abi.md](docs/Abi.md)). Chaos 브리지 작성 안내는 [docs/ChaosBackendGuide.md](docs/ChaosBackendGuide.md)입니다. 그 전에 M5(Bullet + PhysX 백엔드)와 **체크포인트 A**를 끝냈습니다 ([docs/Checkpoints.md](docs/Checkpoints.md)). UE 이름·기본값의 데이터 모델(M1)과 C ABI + Kit + Null 백엔드(M2) 위에 Jolt 5.6.0(M3), Bullet 3.25와 PhysX 5.11(UE4 방식 변형 3종 + SDK 기본값, M5)이 같은 ABI로 붙었습니다. 씬 세 개(`S00.FreeFall`, `M1.SingleBoxDrop`, `M3.BoxPile`)를 세 엔진이 같은 `desc_sha256`으로 돌리고, `PhysicsTestBench.exe`가 결과 파일을 씁니다. `PhysicsTestViewer.exe`(M4)는 Chaos Debug Draw와 같은 색·테셀레이션·그리기 순서로 세 엔진을 보여 줍니다 (엔진 전환, PhysX SdkDefault ↔ UE4Compat은 "재시작 필요"). 타이밍은 모두 **비공식**이고 뷰어의 시간은 "viz mode, not a benchmark"입니다 (측정 프로토콜은 M7). 결과와 완료 기준 표는 [docs/Progress.md](docs/Progress.md)에 있습니다.

![뷰어: M3.BoxPile 1k, Jolt UEParity, 섬 표시(잠든 섬은 Black)](docs/images/m4_viewer_boxpile1k_islands.png)

## 문서

| 문서 | 내용 |
|---|---|
| [01_Feasibility.md](docs/01_Feasibility.md) | 타당성 검토: SDK 빌드 실험, hello 테스트, 렌더러 프로토타입, Chaos 단독 실행 경로, 공정성 리스크 |
| [02_Architecture.md](docs/02_Architecture.md) | 아키텍처: 디렉터리, CMake 타깃, 핵심 타입(UE 대응표), 단일 씬, C ABI 경계, 백엔드 내부(UE4 방식 PhysX), 스텝/타이밍 계약, 프로필, 뷰어, 벤치 러너 |
| [03_WorkPlan.md](docs/03_WorkPlan.md) | 마일스톤 M0~M15, 완료 기준, 공수, 크리티컬 패스 |
| [04_DecisionLog.md](docs/04_DecisionLog.md) | 고정 결정, 설계 결정 로그, 사용자 확인이 필요한 질문 |
| [appendix/A_HeaderSketches.md](docs/appendix/A_HeaderSketches.md) | 핵심 헤더 선언 초안 (C ABI, 씬 타입, 백엔드 Kit) |
| [Progress.md](docs/Progress.md) | 마일스톤별 진행 기록: 만든 것, 완료 기준 결과, 설계와 다르게 한 점, 후속 작업 |
| [Checkpoints.md](docs/Checkpoints.md) | 체크포인트 A (M4 + M5): 엔진별 시각 확인, S00·정지 접촉·질량 readback, 비공식 BoxPile 타이밍, 발견한 규약 버그와 처리 |
| [Abi.md](docs/Abi.md) | C ABI v1.0 (핵심 동결판): 규칙과 함수 표, Kit, 게이트 A0~A10·G 게이트, 동결 때 정한 계약 |
| [Scenes.md](docs/Scenes.md) | 씬 카탈로그: 씬 ID별 정의(바디, 배치, 프레임, 윈도, 판정, 측정값), M6 게이트 씬 9개, M7 성능 씬 3개 |
| [TimingContract.md](docs/TimingContract.md) | (M7) 측정 규약: 실행 순서, 클럭과 버킷, 엔진별 버킷 내용, 루프 위생과 계측 tier, 호스트 제어와 오염 판정(종료 코드 4), 헤드라인 자격, 스레드 매핑 |
| [ResultsSchema.md](docs/ResultsSchema.md) | (M7) 결과 포맷: `manifest.json`(run/2), `exit.json`, `timings.csv`, `hashes.csv`, `states.ptst`, `contacts.ptcn`, `teardown.json`, 게이트 결과 |
| [ChaosBackendGuide.md](docs/ChaosBackendGuide.md) | (초안, M6) Chaos 브리지 작성 안내: 빌드, 초기화 순서, 스레드, 바디 생성 순서, 질량·머티리얼·마진, 동기 모드, 종료 규칙, Null 백엔드를 템플릿으로 |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | 빌드 때 받아 오는 의존성 9개와 라이선스·고지 의무 |

## 빌드 환경

### 요구 사항 (2026-10-21 이 PC에서 확인)

| 항목 | 버전 | 비고 |
|---|---|---|
| OS | Windows 10/11 x64 | 확인: Windows 11 Pro 10.0.26200 |
| 컴파일러 | VS 2022 BuildTools, MSVC 14.44.35207 (cl 19.44.35229) | VS IDE는 필요 없습니다 |
| Windows SDK | 10.0.26100 | |
| CMake / Ninja | 4.1.2 / 1.13.2 | `%USERPROFILE%\.cmake-deps` 아래. PATH에 없어도 `Tools\cmake.cmd`가 찾아 줍니다 (BuildTools에 딸린 CMake/Ninja보다 앞에 둡니다) |
| Git | 2.53 | SDK clone용 |
| Python | 3.11.9 | 분석 도구(M8 이후)용. 패키지는 저장소 루트의 `.venv`에만 설치합니다 |
| 디스크 | D: 여유 약 373 GB | 빌드 트리 크기: test 1.13 GB, dev 1.22 GB, dbg 1.42 GB |

`LongPathsEnabled=0`인 PC를 기준으로 하므로 빌드 루트는 `out/<preset>`처럼 짧게 둡니다 (가장 긴 오브젝트 경로 170자, 한도 240자).

### 빠른 시작

```bat
rem (선택) 로컬 읽기 전용 미러에서 SDK를 clone합니다. 없으면 GitHub에서 받습니다.
set PT_SDK_MIRROR=D:/PhysicsRefs

Tools\cmake.cmd --preset dev
Tools\cmake.cmd --build --preset dev
Tools\ctest.cmd --preset dev
```

- `Tools\cmake.cmd`, `Tools\ctest.cmd`는 `Tools\vcenv.cmd`로 vcvars64(vswhere로 찾음, 기본은 VS 2022 BuildTools 경로)와 CMake/Ninja 경로를 설정한 뒤 `cmake`/`ctest`에 인자를 그대로 넘깁니다. "x64 Native Tools Command Prompt"에서는 `cmake`, `ctest`를 바로 써도 됩니다.
- 경로를 바꾸려면 `PT_VCVARS`(vcvars64.bat 전체 경로), `PT_CMAKE_BIN`, `PT_NINJA_BIN`을 설정합니다.

### 프리셋

| 프리셋 | 구성 | UE 대응 | check | 뷰어 라이브러리 | 용도 |
|---|---|---|---|---|---|
| `dbg` | Debug, `/MDd` | DebugGame | 켬 | 빌드 | 디버깅 |
| `dev` | Release, `/MD` | Development | 켬 | 빌드 | 뷰어, 일상 확인 (헤드라인 아님) |
| `test` | Release, `/MD` | Test | 끔 | 빌드 안 함 | 헤드라인 측정 (M7부터) |
| `test-avx2` | Release, `/MD` | Test | 끔 | 빌드 안 함 | (M7) 플래그 프로필 MatchedAVX2: PhysX·Bullet SDK와 어댑터에도 `/arch:AVX2`, 하네스에는 없음. ISA 민감도 측정 |
| `test-jolt`, `test-physx`, `test-bullet` | Release, `/MD` | Test | 끔 | 빌드 안 함 | (M10) test에서 다른 두 SDK를 뺀 단일 SDK exe (Null은 있음). 바이너리 구성 대조군: 같은 셀을 test와 비교 (판정은 M11) |
| `prof` | Release, `/MD` | - | 끔 | 빌드 안 함 | (M10) 단계 분해 전용: Tracy v0.14.1 존(하네스)과 Jolt external profile → Tracy. run은 헤드라인 자격이 없습니다 ("profiler build") |

- 모든 프리셋은 Ninja 단일 구성이고 빌드 트리는 `out/<preset>`입니다. `RelWithDebInfo`/`MinSizeRel`은 configure에서 거부합니다.
- 공통 플래그: 전 타깃 `/fp:fast`, `/MD`(dbg는 `/MDd`), `/Zi`, LTO 없음. `/arch:AVX2`는 Jolt와 Jolt를 쓰는 TU에만 붙습니다 (Shipped 프로필). 하네스 타깃은 C++20, `/W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor`입니다 (하네스 TU의 경고는 빌드 실패). 전역 `CMAKE_CXX_STANDARD`는 쓰지 않습니다 (PhysX `/std:c++14 /WX`가 깨짐).
- `test-avx2`의 `desc_sha256`은 `test`와 같습니다 (하네스 플래그가 같다, M7에서 확인). 단일 SDK 프리셋도 같습니다 (하네스가 같다). 선택 옵션 `-DPT_REPRO_LINK=ON`은 컴파일·링크에 `/Brepro`를 겁니다 (M10, 기본 OFF).

### SDK 소스

| SDK | 고정 커밋 | 기본 URL (`PT_SDK_MIRROR` 미설정) |
|---|---|---|
| Jolt 5.6.0 | `e77f175595e64cb44218cc9d9d56fc365ad0e36a` | https://github.com/jrouwe/JoltPhysics.git |
| PhysX 5.11.0 | `da950a3537927784951853c66618036f332ca0ce` | https://github.com/NVIDIA-Omniverse/PhysX.git |
| Bullet 3.25 | `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5` | https://github.com/bulletphysics/bullet3.git |

- SDK는 항상 빌드 트리(`out/<preset>/_deps/<name>-src`)로 clone하고, 그 안에서만 빌드합니다. `PT_SDK_MIRROR`를 설정하면 URL 기본값이 `file:///<mirror>/<repo>`가 됩니다. 캐시 변수 `PT_JOLT_GIT`, `PT_PHYSX_GIT`, `PT_BULLET_GIT`으로 직접 바꿀 수도 있습니다. 같은 일을 하는 사용자 프리셋 예시는 [CMakeUserPresets.json.example](CMakeUserPresets.json.example)입니다 (`dbg-local` 등, 복사해서 `CMakeUserPresets.json`으로 씁니다).
- 고정 SHA와 다른 `PT_<SDK>_GIT_TAG`, 빌드 트리의 `_deps` 밖(미러, 다른 체크아웃)을 가리키는 `FETCHCONTENT_SOURCE_DIR_<NAME>`(경로는 대소문자 무시로 비교), 고정 SHA가 아닌 HEAD는 모두 configure를 FATAL로 멈춥니다 (CTest `PhysicsTest.Probe.PinGuard`). SDK를 고쳐 보려면 `out/<preset>/_deps/<name>-src`의 clone을 직접 고칩니다.
- PhysX 경로는 기본이 FetchContent + `add_subdirectory`입니다. VS 생성기에서는 자동으로, 또는 `-DPT_PHYSX_USE_BOOTSTRAP=ON`으로 configure 시점 별도 빌드(`PhysXBootstrap.cmake`)를 씁니다.

작은 의존성(Dear ImGui v1.92.9b-docking, ImPlot v1.0, stb a7d4c217, nlohmann/json v3.12.0, doctest v2.5.3)은 URL + SHA-256으로 받습니다. 네트워크 없이 빌드하려면 같은 아카이브를 한 디렉터리에 두고 `PT_DEPS_CACHE`로 가리킵니다 (파일 이름은 `cmake/ThirdParty/ImGui.cmake`, `SmallDeps.cmake`의 `CACHE_FILE`).

### 테스트 (CTest)

| 테스트 | 확인하는 것 |
|---|---|
| `PhysicsTest.Automation` | `PhysicsTestAutomation.exe`(doctest)의 모든 단위 테스트를 한 프로세스에서 실행 |
| `PhysicsTest.Unit.<Area>` | 같은 exe를 `-tc=PhysicsTest.<Area>.*`로 영역별 실행: `Core`(UE 회전·합성 순서, SHA-256/FNV, FParse, `FRandomStream` 골든 값, UTF-8), `Config`(ini 문법, CVar 우선순위), `Types`(UE 필드·기본값, 32채널 충돌 필터, 조인트 프리셋), `BodyUtils`(질량 규칙), `StepPlanner`(tick mode 고정값), `Analytic`(`dt_engine`, S00 기준값), `Profile`(ini 계층, Origin, 변형 키 제한, `-ini:`/`-ExecCmds`), `PhysScene`(프레임 상태기계, 두 frame driver, 자유낙하 픽스처, 명령, 타이밍 버킷, 하네스 CVar 가드), `Abi`(ABI 게이트 A0~A7·A10을 static Null과 `PtBackend_Null.dll` 두 경로로 (v1.0: major 0 요청 거부, `NumPushedSteps`), A0~A7을 SDK 백엔드(Jolt, Bullet, PhysX)로, Kit 상태 분리·가드, ABI 호출 비용 기록), `BackendCommon`(단위 계수표, 캡슐 축(UE4 `U2PSphylBasis` 포함), Bullet damping 변환과 UE 감쇠 순서, 결합 규칙과 packed 모드, 접촉 규약과 가동성 순위, compound 래핑 분류, FNV-1a, ODR), `Backend`(레지스트리, 마샬링, effective 설정 파서), `Scenario`(씬 레지스트리, 파라미터, `scene_sha256`, BoxPile 배치, `desc_sha256`이 엔진·엔진 키와 무관함, M6 게이트 씬의 `Config/Golden/scene_hashes.json` 대조), `SdkBackend`(레지스트리의 모든 SDK 엔진: S00 자유낙하, 네이티브 프리미티브, 정지 접촉 스냅샷, kinematic 원점 속도, 용량 힌트, Null과 같은 명령 결과, 질량 텐서, 체크포인트 A 표(엔진·변형마다 S00, 정지 접촉 normal, 질량 readback), v1.0 계약(잠든 바디에 쓴 속도는 깨움, 접촉 점이 각 셰이프 표면 위·flag 배타, 깬 바디의 유지 접촉 버림)), `BulletBackend`(M5: 마진 readback, effective 키, 래핑, 정지 접촉 임펄스, 잠든 섬 접촉, 감쇠 순서, 각속도 한계, kinematic, 중력 변경, 풀 넘침, 섬, 전역 결합 규칙), `PhysXBackend`(M5: 변형 4종의 씬, 한 프로세스의 UE4Compat 계열과 SdkDefault 거부, dispatcher 해시 동일성, UE4Compat readback, 필터 셰이더 진리표, 오류 bit, 옵션 검증, 잠든 섬 접촉, 변형 간 `desc_sha256`). 걸러진 테스트가 0개이면 실패로 칩니다 |
| `PhysicsTest.Gates.{G1,G1b,G2,G3,G4,G5,G6,G7,G9,J1,G12}` | (M6, J1은 M9 수정 라운드 0, 세 프리셋) 물리 게이트를 Null과 레지스트리의 모든 SDK 엔진 × 변형(Jolt.Default, Bullet.Default, PhysX UE4Compat/-TGS/-Stab/SdkDefault) × UEParity·Normalized로 ABI를 통해 돈다. 허용치와 엔진별 등록 편차는 `Config/Golden/tolerances.json`. 씬 정의와 측정값은 [docs/Scenes.md](docs/Scenes.md) §2.4~§2.12 |
| `PhysicsTest.Gates.ToleranceFileIsComplete` | (M6) 게이트가 읽는 모든 허용치 키가 `tolerances.json`에 있고, 03 M6 기본값과 등록 편차(예: PhysX G4 반발 속도 모델 1, 실행 뒤 등록 표시)가 그 값이며, 사유 없는 값·override·허용 Skip·규칙은 로드되지 않음, 가장 구체적인 override가 이김, Skip은 Null만 허용 |
| `PhysicsTest.Gates.SelectionRules` | (M6 수정 라운드 0) `RunPhysicsGates`의 규칙: 등록되지 않은 Skip은 Fail, `-Gates=G12`만 골라도 앞의 게이트를 돌리고 G12 보고만 냄 |
| `PhysicsTest.Unit.DebugDraw` | (M4, 세 프리셋) 디버그 드로 파이프라인: 단위 와이어 메시 선분 수(box 12, sphere 128, capsule 68, circle 8), Chaos 상태·bounds·섬 색, 9단계 그리기 순서(`PostTickDebugDraw`), 캡슐 HalfHeight = HalfLength + R와 박스 half extent, 접촉 원 6/3·normal 30·비활성 ×0.75, COM 축 30·화살표 10, `FIslandBuilder`, 큐 예산·영역, Null 호스트 스냅샷, 합성 스냅샷의 조인트, Jolt의 점별 Phi·잠든 섬 접촉·요소 인덱스 |
| `PhysicsTest.Unit.Viewer` | (M4, dbg·dev) `PhysicsTestViewerTests.exe`: 카메라(비행, 가속·감쇠, E/Q, 시점, 휠, FOV, F), `FSceneView`(hFOV 90 major 축, reversed-Z), 세션(일시정지, 한 스텝 = FrameCount +1, Null ↔ Jolt 전환), 재시작 규칙과 재실행 명령줄, 콘솔 `open`/`restart`(M5), PhysX 변형 전환 규칙(UE4Compat 계열은 같은 프로세스, SdkDefault는 RestartRequired, M5), Console의 읽기 전용 하네스 CVar |
| `PhysicsTest.Unit.ViewerRenderer` | (M4, dbg·dev) 오프스크린 D3D11 픽셀 색 (하드웨어 D3D11 장치가 없으면 건너뜀) |
| `PhysicsTest.Viewer.Headless{Null,Jolt,Bullet,PhysX}` | (M4, M5 dbg·dev) `PhysicsTestViewer.exe -Headless`가 씬을 그려 PNG와 `mode = viz` manifest를 쓰고 0으로 끝남 (PhysX는 변형 UE4Compat) |
| `PhysicsTest.Viewer.HeadlessRestart` | (M5, dbg·dev) "재시작 필요" 경로를 끝까지: `-ViewerCmds`로 PhysX UE4Compat → -TGS(같은 프로세스) → SdkDefault(RestartRequired) → `restart`. 헤드리스 부모가 자식을 헤드리스로 띄워 기다리고, 자식이 SdkDefault로 스크린숏과 `-PerfReport`를 쓰고 0으로 끝남 (부모는 자식의 리포트를 덮어쓰지 않는다, M5 수정 라운드 0) |
| `PhysicsTest.Unit.CommandLineUtf8` | 한글 인자(`-PtUtf8Probe=한글 경로`)가 `wmain`을 거쳐 UTF-8로 `FCommandLine`에 들어감 |
| `PhysicsTest.Bench.List` | `PhysicsTestBench.exe -List`가 성공하고 `M3.BoxPile`을 보여 줌 (시나리오 레지스트리가 `/WHOLEARCHIVE`로 들어옴) |
| `PhysicsTest.Bench.M3Matrix` | Null, Jolt, Bullet, PhysX `UE4Compat`/`-TGS`/`-Stab`/`SdkDefault` × UEParity/Normalized × `M1.SingleBoxDrop` a/b, `M3.BoxPile` 1k/5k (30 프레임, 56 run, 1 run = 1 process): 결과 파일 세 개, 행 수, `status ok`, 비공식 표시, `shape.wrapping` = 0, 모든 엔진·변형의 `desc_sha256`이 Null과 같음 (M5) |
| `PhysicsTest.Bench.Conformance` | (M6) `PhysicsTestBench -Conformance`를 Null, NullDll, Jolt, Bullet, PhysX 변형 4종 × UEParity·Normalized(16 fresh process)로 돌려 `conformance.json`(schema `physicstest.conformance/1`, ABI 1.0, tolerances SHA-256, 게이트 10개 통과)을 확인하고, ABI 게이트 한 개씩(`-Gates=A0`/`A7`/`A10` Null, `A3` NullDll)도 돈다. NO_REINIT 시험 대역(`PT_NULL_DLL_NO_REINIT=1`인 NullDll)으로 A0~A7·A10 각각과 물리 게이트를 프로세스 하나씩 돌고, 섞인 `-Gates=` 목록이 거부되는지 본다 (M6 수정 라운드 1). `-Describe`/`-List`의 stdout이 순수 JSON인지도 본다 (40 run·검사) |
| `PhysicsTest.Bench.ConformanceG12Injected` | (M6 수정 라운드 1) 시험 전용 `-InjectEngineErrorBits=0x1`을 `-Conformance -Gates=G12`에 주면 G12가 Fail, 종료 코드 2, `conformance.json`에 `injected_engine_error_bits` |
| `PhysicsTest.Bench.ExitSceneNeedsSleeping` | (M6) sleep을 끈 프로필(Normalized)에서 `S08.StartAsleep`은 종료 코드 2 |
| `PhysicsTest.Bench.ExitCapacityBullet` | (M5) Bullet manifold 풀을 64로 줄인 BoxPile 1k: heap fallback이 용량 bit 0x1(`errors.capacity_bits` 0x3)이 되어 종료 코드 5 |
| `PhysicsTest.Bench.Exit{BackendUnavailable,BadCommandLine,UnknownScenario,UnknownParam,BadSeed,BadEngineOption,PreflightMissingKey,EngineError,Fatal,Capacity,EngineErrorOverCapacity,BulletThreads}` | 벤치 종료 코드 6, 2, 2, 2, 2, 2, 2, 3, 3, 5, 3, 2. M7부터 모든 경로에서 `exit.json`의 코드와 `status`도 확인한다. BadEngineOption(변형에 없는 Jolt 키)과 PreflightMissingKey(`-RequireEffectiveKey=no.such.key`)는 `manifest.json`의 `status = preflight_failed`도 확인한다 (PreflightMissingKey는 `frames_run` 0). EngineError: 시험용 `-InjectEngineErrorBits=0x200`(Null). Fatal(M7): 시험용 `-InjectFatal`의 Fatal 뒤에도 `exit.json`이 3 (`status fatal`). Capacity: temp allocator를 1 MiB로 줄인 Jolt BoxPile 1k (manifest의 용량 bit 묶음 확인). EngineErrorOverCapacity: 둘 다 있으면 엔진 오류(3)가 이기고 manifest에 두 묶음이 남음. BulletThreads(M7): Bullet은 T = 1만. M3의 ExitHashEvery는 M7 recorder가 생겨 지웠다 |
| `PhysicsTest.Bench.ExitContaminated{,Sibling}` | (M7, 직렬) 같은 CPU 0(Sibling: CPU 0의 SMT 형제)에 `-BusyHelper` 부하 프로세스(10 s: 벤치의 측정 구간 전체를 덮는다, M8 수정 라운드 1)를 띄운 측정 프로토콜 run(`-HostCheck=enforce`)이 종료 코드 4, `exit.json` `contaminated_host`. 오염은 run의 코어(마스크 CPU + SMT 형제)에서 잰다 |
| `PhysicsTest.Bench.{ExitTeardownFatal,UeRuntimeFatal,ConformanceBackendUnavailable}` | (M7 수정 라운드 0) teardown 중 Fatal은 `exit.json`을 바꾸지 않고 `teardown.json`에 남음; UE_RUNTIME Null DLL의 Fatal도 `TerminateProcess`로 끝남(`DLL_PROCESS_DETACH` 없음); `-Conformance`도 게이트 전에 멈추면 `exit.json`을 남김 |
| `PhysicsTest.Bench.InstrumentPhases` | (M7 수정 라운드 0) phases tier: phase timer를 선언하는 Null DLL 시험 대역(`PT_NULL_DLL_PHASE_TIMERS=1`)의 `solver_ns`가 매 프레임 채워짐 |
| `PhysicsTest.Bench.{G8,G11}NegativeControl` | (M7 수정 라운드 0) 시험용 `-InjectGateFault=4`가 자식 하나만 흔들면 G8·G11이 차이를 찾아 종료 코드 2 |
| `PhysicsTest.Bench.{InstrumentCounters,HeadlineIneligibleViz}` | (M7) 계측 tier(`-Instrument=counters`)나 viz run은 거부되지 않고 `timing_valid_for_headline = false`와 사유 |
| `PhysicsTest.Bench.Recorders` | (M7) `-HashEvery=1`의 `hashes.csv`(`frame,h_native,h_quantized,h_state`, 프레임마다), `-StatesEvery=10`의 `states.ptst` + `states.dtype.json` |
| `PhysicsTest.Bench.Preflight{G9Injected,G10aListenerInjected,G10aDrawerInjected,G0Injected,G10aRealListener,G10aBulletDrawerInjected}` | (M7) 시험용 `-InjectPreflightFault=`(G9 값, listener, drawer, G0 사실)와 백엔드에 몰래 준 `PT_INSTR_CONTACTS`(Jolt가 실제로 listener를 설치)를 preflight가 종료 코드 2로 거부 |
| `PhysicsTest.Bench.PreflightSweep` | (M7) 등록된 모든 씬 × Null과 모든 SDK 엔진·변형 × UEParity·Normalized(2 프레임)의 preflight가 통과 (`S08`/Normalized는 설계대로 거부) |
| `PhysicsTest.Bench.UeRuntime{Terminate,Control,Describe}` | (M7) `PT_NULL_DLL_UE_RUNTIME=1`인 Null DLL: run과 `-Describe`가 출력·`exit.json` 뒤 `TerminateProcess`로 끝나 DLL이 `DLL_PROCESS_DETACH`를 받지 않음. 대조군은 정상 종료 |
| `PhysicsTest.Bench.{G8Det1,G11ContactCapture}` | (M7) `-Gate=G8`(fresh process 3회의 마지막 `h_native`가 같음)와 `-Gate=G11`(접촉 캡처 끔/켬의 프레임별 `h_native`·`h_state`가 같음)을 Null, NullDll, Jolt, Bullet, PhysX 변형 4종으로 (BoxPile 1k, 60 프레임. 600 프레임은 증거 run) |
| `PhysicsTest.Bench.ThreadMatrix` | (M7, 직렬) Jolt·PhysX T = 1..6, Bullet T = 1: 마스크 0x1~0x555, 엔진 워커 수. M10: 센서스 창의 cpu_eff·외부 스레드 cycle과 T, T > 1의 표기 |
| `PhysicsTest.Bench.{AbiOverhead,G10bJolt,G10bBullet,G10bPhysX,G10bNegativeControl,ConformanceStamp}` | (M10) `-AbiOverhead`: Null, 바디 0, 4호출의 TSC 배치 중앙값 < 100 ns/frame (Debug는 기록만). 측정 프로토콜의 `M3.BoxPile` 1k 300 프레임 T = 1 run에서 G10b Pass(센서스 창). 음성 대조(수정 라운드 0): 시험 전용 `-InjectForeignWork=<ms>`의 도우미 스레드가 창 안에서 일하면 G10b Fail, 헤드라인 자격 없음. G10b 판정 시험 넷은 `RUN_SERIAL`(수정 라운드 1: 병렬 CTest가 CPU 0을 나눠 쓰면 창이 판정 없는 Skip이 될 수 있다). `-Conformance -StampDir=`가 스탬프를 쓰고, 같은 run은 그 스탬프를 찾고, `-ini:` 오버라이드를 더한 run은 찾지 못함 |
| `PhysicsTest.Bench.ManifestSchema` | (M7) BoxPile 5k run과 거부된 run 둘(엔진 옵션 preflight, 빠진 필수 키)의 `manifest.json`·`exit.json`이 `Config/Schemas/*.schema.json`을 통과하고 (`Tools/validate_manifest.py`, `.venv` Python `-I`, jsonschema), 일부러 망가뜨린 사본 여섯(mode, 전원 계획 이름, 전원 계획 GUID, 0 ns 빈 윈도, 그리고 M8의 헤드라인 일관성 둘: 사유가 남은 채 자격 참, 사유 없이 자격 거짓)은 실패 |
| `PhysicsTest.Bench.{AllocHookPositiveControl,HeadlinePositiveControl}` | (M8, M7 검토) 시험용 `-InjectTickAllocation=3`이 `Tick` 안에서 한 `operator new`를 훅이 셈 (`tick_new_calls` 1, 첫 프레임 3, 자격 없음). 측정 프로토콜 그대로의 run(pre-roll 0.5 s, tier 0)은 헤드라인 자격이 있음 (test 프리셋: 사유 0. 기록만 한 오염과 dev/dbg의 `PT_DO_CHECK` 사유만 허용) |
| `PhysicsTest.Bench.{PreflightG9InjectedNotCompared,PreflightG9InjectedUnknownKey}` | (M8, M7 검토) `-InjectPreflightFault=g9:<key>`의 key를 G9가 비교하지 않으면(READBACK이 아님, 없는 키) 조용히 통과하지 않고 종료 코드 2 `usage_error` |
| `PhysicsTest.Bench.UeRuntimeConformanceFatal` | (M8, M7 검토) UE_RUNTIME Null DLL을 로드한 `-Conformance`의 Fatal도 `exit.json`(3, `terminate_process`)을 쓴 뒤 `TerminateProcess`로 끝나 `DLL_PROCESS_DETACH`가 없음 |
| `PhysicsTest.Bench.{InjectSilentExit,AnalyticReferences}` | (M8) 시험용 `-InjectSilentExit=2`는 원시 종료 코드 0으로 끝나지만 `exit.json`을 남기지 않음 (러너가 실패로 판정해야 하는 경우). `-AnalyticReferences`는 C++ 해석 기준값 JSON을 냄 |
| `PhysicsTest.Unit.{Joint,JoltJoint}`, `PhysicsTest.Unit.{BulletBackend,PhysXBackend}`의 조인트 시험 | (M9) 조인트 계약은 물리 게이트 J1입니다 (수정 라운드 0: M6a 5°의 hinge·ball, COM 넛지 막대, 움직이는 조인트의 couple, hinge 외팔보의 v1.1 각 충격량 규약; `-Conformance`로 DLL 백엔드도 자기 프로세스에서 돕니다). `Joint.BackendContract`는 레지스트리 SDK 엔진에 J1을 돌리고 승인 대기 등록이 없는지 봅니다 (Bullet ball 3 cm는 리드가 2026-10-22에 승인했습니다). 그 밖에 엔진별 조인트·스냅샷·sleep 매핑·네이티브 드로 시험 |
| `PhysicsTest.Bench.{G11BulletStaticAabbOff,G11BulletStaticAabbOffPile,G11BulletVizDrawer,G11PhysXViz,G11VizNoDrawing,NativeDrawBullet,NativeDrawPhysX}` | (M9) `Bullet.StaticAabbOff` = `Bullet.Default`(좁힌 범위: 한 바디, 결정성 켠 Normalized 1k 더미. 리드 결정(2026-10-22)으로 이 범위가 03 기준이고, 기본 프로필 더미에서는 결과를 바꾸는 by-design knob입니다), viz 드로어·PhysX 시각화를 붙인 run과 안 붙인 run의 해시 동일(UEParity, sleep 켬; viz child가 매 프레임 그렸는지도 확인, 그리지 않으면 실패: `G11VizNoDrawing`), 뷰어 없이 `native_draw.csv` 기록 |
| `PhysicsTest.Py.Unit` | (M8, `.venv`의 ptbench) 셔플(splitmix64 기준값), 매트릭스 전개(smoke 12셀 × 5블록), 겹치는 스위치 키와 러너 스위치의 거부, `analytic.py`의 문서 값(읽을 수 없는 C++ 기준값은 한 줄 오류와 종료 코드 2), 통계(nearest-rank, CV, α, 오버헤드 모델), validate 규칙(viz 거부, `exit.json` 없음 = 실패, 플래그 프로필·`desc_sha256` 섞임 = 오류, Fatal run은 manifest 없이도 됨), 거부된 표는 통계 없음(리포트 §3 포함), 리포트 머리의 전원 계획 표시(run 디렉터리만, 여러 세션), 모든 표 밑·그림 위의 조건 줄, 값이 없는 윈도, parquet 열 형식, 과반 기준 run의 첫 발산 프레임과 incomplete 판정, 러너의 teardown 멈춤 처리·타임아웃·중단 시 자식 종료·job object, 절대 경로 실행(git, powercfg), 전원 계획 파싱과 Balanced 거부(계획을 주입해 어느 기계에서나), 유휴 대기, `--skip-idle` 우회 기록, UTF-8 콘솔 출력, 스키마. M10: 주장 규칙 세 경우(CI가 1을 포함, |ρ − 1| < 3%, 방향 불일치), bootstrap CI, CV 게이트·sentinel drift(합성 결과, 가짜 벤치로 러너 끝까지), 스탬프 확인(missing / stale / invalid), comparability key, 뷰어 manifest, S(T)/E(T), 없는 CLI 경로, 구성 대조 표, 타깃 선택 |
| `PhysicsTest.Py.AnalyticMatchesCpp` | (M8) `analytic.py`가 `PhysicsTestBench -AnalyticReferences`의 모든 C++ 값과 1e-9 이내 (z60 = 501.8332813705 cm 포함) |
| `PhysicsTest.Py.Integration` | (M8) 이 빌드의 벤치로: 환경 게이트가 거부하면 run 없이 종료 코드 3, 도중에 죽은 run(원시 코드 0, `exit.json` 없음)과 타임아웃은 실패, 종료 코드 4는 2번 재시도 뒤 `contaminated`(건너뛰지 않음), 매트릭스·타깃·`--bench-arg` 스위치가 벤치에 닿음(manifest로 확인)과 겹치는 키 거부, viz manifest 거부, Shipped + MatchedAVX2와 다른 `desc_sha256`은 표 오류, `-Gate=G8` run의 DET1 행렬과 해시를 일부러 바꾼 사본의 첫 발산 프레임, 짧은 세션의 끝까지(스키마, parquet 열 형식, 리포트, run 디렉터리만으로 만든 리포트의 전원 계획 표시, 출력을 파이프로 받은 `ptbench perf`). M10: Config·exe·DLL 사본에서 스탬프 무효화 셋(`PhysX.UE4Compat.ini` 키, Null DLL, `tolerances.json`)을 러너가 프로세스 전에 거부, 측정 프로토콜 run의 G10b와 TSC 보정, 단일 SDK 빌드는 있는 엔진만 |
| `PhysicsTest.Bench.ManifestsValidate` | (M7 수정 라운드 0) 위 벤치 테스트들이 쓴 모든 `manifest.json`·`exit.json`(거부·실패·오염·계측 run, G8·G11 자식 포함)이 스키마를 통과. 다른 벤치 테스트 뒤에 돈다 (`DEPENDS`) |
| `PhysicsTest.Smoke.{Jolt,Bullet,PhysX}` | SDK 스모크: SI, Z-up, g = 9.80, damping 0, sleep 끔. z(60) = 5.0183328 m ± 2e-5. Jolt는 설정 문자열(AVX2, Profile/Debug Renderer/FP Exceptions 없음)도 검사. `RunSmoke.cmake`가 `PT_SMOKE_JSON` 줄이 유효한 JSON인지도 확인 |
| `PhysicsTest.Probe.ThirdPartyIncludes` | Core를 먼저 include한 TU가 래퍼 쌍을 거쳐 doctest·ImGui·ImPlot·nlohmann/json을 include하고 컴파일됨 |
| `PhysicsTest.Probe.NegativeScaleFatal` | 음수 스케일 `FTransform` 합성이 모든 프리셋(`PT_DO_CHECK=0`인 test 포함)에서 Fatal로 끝남 (종료 코드 3) |
| `PhysicsTest.Probe.HarnessCVars` | 프로필 리졸버만 쓰는 프로그램(링크 맵에 `BodyUtils`·`BodyInstance`·`PhysScene` 오브젝트 없음)에도 하네스 물리 CVar가 등록되고 변형 금지·값 검증이 그대로이며, `physics_sha256`이 `PhysicsTestAutomation`과 같음 |
| `PhysicsTest.Probe.FlagLeak` | Jolt를 PRIVATE로 링크한 정적 lib의 소비자 TU에 `__AVX2__`와 `JPH_*`가 없음 |
| `PhysicsTest.Probe.BuildFlags` | `BuildFlags.json`에 플래그 프로필 위반이 0개 (모든 TU의 `_ITERATOR_DEBUG_LEVEL`이 같은지, Bullet TU와 Bullet을 쓰는 하네스 TU의 `BT_*` 정의가 같은지 포함) |
| `PhysicsTest.Probe.RefsClean` | `D:\PhysicsRefs` 세 저장소의 `git status --porcelain`(`--ignored` 포함)이 비어 있고 PxConfig.h/.dxil/.spv가 없음. 프로브 자신도 `--no-optional-locks`로 읽기만 함 |
| `PhysicsTest.Probe.LongestObjectPath` | 가장 긴 컴파일러 오브젝트 경로 < 240자 |
| `PhysicsTest.Probe.PinGuard` | 잘못된 SDK 태그, 미러(대소문자 바꾼 경로 포함)·빌드 트리 밖 소스 디렉터리, 다른 HEAD(임시 저장소)에서 configure가 FATAL |
| `PhysicsTest.Probe.CheckModules` | 게이트 A9: `PtBackend_Null.dll`의 export가 `PT_GetBackendApi` 하나이고 import가 VC 런타임, UCRT, KERNEL32뿐 (`Tools/check_modules.py --strict-crt`: C++ 라이브러리 DLL `msvcp140*` 금지. MSVC `dumpbin`, `.venv` Python `-I`) |
| `PhysicsTest.Probe.KitNamespaces` | `PhysicsTestAutomation.map`에서 Kit 심볼이 백엔드별 inline namespace(`Null`, `AbiTwin`, `Jolt`, `Bullet`, `PhysX`, `AutomationTest`)로 나뉘고 `PhysicsTest::Abi` 바로 아래에는 없음 (Jolt `/arch:AVX2`와 Bullet·PhysX가 한 exe에 있어도 COMDAT이 섞이지 않음) |
| `PhysicsTest.Probe.BackendIsolation` | 백엔드 TU의 include 경로에 하네스 Core·PhysicsCore가 없고 `PT_BACKEND_NS`가 정의됨 |
| `PhysicsTest.Probe.KitNeedsBackendNs` | `PT_BACKEND_NS` 없이 Kit를 include한 TU가 `#error`로 컴파일되지 않음 (`compile_commands.json`의 그 TU 컴파일 줄을 스크립트가 직접 실행한다. 빌드 트리를 다시 부르지 않는다) |
| `PhysicsTest.Probe.HarnessSdkIsolation` | 하네스 TU(백엔드, SDK 스모크, JoltSeam 밖의 모든 `Source/` TU와 생성된 레지스트리)에 SDK include 경로와 `JPH_*`/`BT_*`/`PX_*` 정의가 없고, 모든 플래그 프로필(M7부터 MatchedAVX2 포함)에서 `/arch`가 없음 |
| `PhysicsTest.Probe.BenchNoRenderer` | `PhysicsTestBench.exe`가 d3d11.dll과 dxgi.dll을 import하지 않음 (`dumpbin /dependents`) |
| `PhysicsTest.Probe.AutomationNoRenderer` | (M4) 디버그 드로를 링크한 `PhysicsTestAutomation.exe`도 d3d11·dxgi·d3dcompiler를 import하지 않음 |
| `PhysicsTest.Probe.AdapterComdats` | Automation·Bench 링커 맵(`PT_WITH_VIEWER`: + Viewer, ViewerTests)에서 `/arch:AVX2` 어댑터(`PtBackendJolt`) 오브젝트가 내놓은 함수가 모두 백엔드 소유이고, Jolt lib 자신의 namespace 밖 함수는 알려진 목록뿐 (하네스가 어댑터의 AVX2 COMDAT을 실행하지 않음, Abi.md §5.13). Debug 프리셋은 보고만 한다 |
| `PhysicsTest.Probe.AdapterComdats{Bullet,PhysX}` | (M5) 같은 맵에서 Bullet·PhysX 어댑터 오브젝트가 내놓은 함수도 모두 백엔드 소유 (Bullet은 `bt` 이름 포함) |
| `PhysicsTest.Probe.AdapterComdatsNegative` | 같은 스크립트가 어댑터 `vector<uint32>::push_back` 줄을 넣은 고정 맵(`Source/Tests/Probes/Fixtures/AdapterComdatsNegative.map`)에서 실패함 |

### 물리 프로필 (M1)

설정은 UE처럼 ini 계층으로 정합니다: 코드 기본값 → `Config/BasePhysicsTest.ini`(UE 기본값, 모든 UE 키는 `[/Script/Engine.PhysicsSettings]`) → `Config/Profiles/<UEParity|Normalized>.ini` → 백엔드 변형 ini(M5) → 씬 고정값(M3) → 커맨드라인. 값마다 출처(Origin: 파일과 줄)가 남습니다. 변형 ini는 자기 엔진 키와 프로필이 `EngineDefaultKeys`로 연 키만 바꿀 수 있고, 그 밖의 키를 건드리면 로드 오류입니다. 하네스 물리 CVar는 `p.ComNudgeAffectsInertia`와 `p.SensitiveSleepThresholdMultiplier` 두 개이고 (`Physics/PhysicsConsoleVariables.h`), 그 밖의 CVar 이름은 엔진(Chaos 등)으로 넘어갑니다. 섹션 이름은 UE처럼 대소문자를 무시하고, 숫자 값은 유한수만 받습니다. 커맨드라인 문법은 UE와 같습니다. 예외 하나: `-ExecCmds` 안의 `name=value`도 받습니다. UE 콘솔은 공백으로만 나누므로(`name value`) 같은 줄을 UE에 넣으면 `name=value` 항목은 조용히 무시됩니다. UE로 옮길 줄은 아래처럼 공백 형식으로 씁니다 (의도한 차이, docs/02 §5.2 #32).

```
-ini:PhysicsTest:[/Script/Engine.PhysicsSettings]:DefaultGravityZ=-490,[/Script/PhysicsTest.PhysicsTestSettings]:TickMode=Sync
-ini:Engine:[/Script/Engine.PhysicsSettings]:MaxSubsteps=4
-ExecCmds="p.ComNudgeAffectsInertia 0, p.Chaos.Solver.Sleep.Enabled 0"
```

하네스 물리 CVar는 프로세스 전역이지만 `FPhysScene`은 생성될 때 자기 설정의 값(설정이 없으면 기본값)으로 맞추고, 바디를 만들 때 바뀌지 않았는지 확인합니다 (M2). 같은 시간에 살아 있는 씬들은 이 값이 같아야 합니다.

### 백엔드 경계 (M2)

모든 엔진은 순수 C ABI `Source/Runtime/PhysicsTestAbi/Public/PhysicsTestAbi/PtBackendAbi.h`(**v1.0, major 1, M6에 핵심 동결**. 그 뒤로는 append-only)를 거칩니다. 백엔드는 header-only Kit(`PtBackendKit.h`)로 썽크·스레드 가드·phase 상태기계·입력 검증을 얻고, 백엔드 타깃마다 `PT_BACKEND_NS`를 다르게 정의합니다. 규칙과 함수 표는 [docs/Abi.md](docs/Abi.md)에 있습니다.

- `Null`: SDK 없는 기준 백엔드(semi-implicit Euler, UE damping, 충돌 없음, `PT_CAP_NATIVE_FRAME_MODEL`). static으로 링크되고, 같은 소스가 `out/<preset>/bin/PtBackend_Null.dll`(export `PT_GetBackendApi` 하나)로도 빌드됩니다. 레지스트리 이름은 `Null`(static)과 `NullDll`(DLL)입니다.
- 백엔드 목록은 configure 때 생성되는 `PT::BackendRegistry`(`out/<preset>/Generated/PTBackendRegistry.gen.cpp`)가 정적 진입점을 직접 참조합니다. `Jolt`(M3), `Bullet`·`PhysX`(M5)가 들어 있습니다.
- `Jolt`: Jolt 5.6.0을 m 단위로 감싼 static 백엔드입니다 (`Source/Backends/JoltBackend`, Jolt의 `/arch:AVX2`와 `JPH_*` 정의는 이 lib의 TU에만). 변형 ini는 `Config/Profiles/Backend/Jolt.Default.ini`(Jolt 기본 솔버 설정)이고, Normalized에서는 바디마다 sleep을 끄고 Jolt 기본 damping 0.05와 선속도 한계 500 m/s를 쓰지 않습니다.
- `Bullet` (M5): Bullet 3.25 단일 스레드 `btDiscreteDynamicsWorld`, m 단위, `stepSimulation(dt, 0, dt)` (`Source/Backends/BulletBackend`). UE 감쇠 순서와 UEParity 각속도 62.83 rad/s는 내부 pre-tick이 에뮬레이트하고, UE 결합 규칙은 전역 콜백(모듈 init 옵션, GLOBAL_SCENE_SETTINGS)입니다. 변형 ini는 `Bullet.Default.ini`(Bullet 기본값 명시).
- `PhysX` (M5): PhysX 5.11에 UE4의 PhysX 통합(PhysXSupport, InitGamePhys, `FPhysScene_PhysX::InitPhysScene`, 필터 셰이더, 생성 순서)을 클린룸으로 다시 구현한 static 백엔드입니다 (`Source/Backends/PhysXBackend`). 변형 `UE4Compat`(cm, (100, 1000), SAP, PGS 8/1, UE4 contact offset·머티리얼 규칙), `UE4Compat-TGS`, `UE4Compat-Stab`, `SdkDefault`(m, (1, 10), SDK 기본값). PxPhysics와 tolerance scale은 프로세스당 하나라 앞의 세 변형은 한 프로세스에 함께 둘 수 있고 `SdkDefault`는 자기 프로세스가 필요합니다. 결과에는 "UE4 settings on PhysX 5.11"로 표기합니다 (UE4가 출하한 것은 PhysX 3.4).
- `FPhysScene::Tick`은 `NATIVE_FRAME_MODEL` 백엔드에 UE의 `SetUpForFrame` 인자를 프레임당 한 번 넘기고(Native driver, `NumStepsTaken`이 하네스 계획과 같아야 함), 그 밖의 백엔드에는 계획한 스텝마다 한 번씩 넘깁니다(Harness driver).

`[/Script/PhysicsCore.PhysicsSettingsCore]` 섹션은 쓰지 않습니다 (UE 게임처럼 모든 물리 키가 Engine 섹션에 있습니다, docs/04 DL44). 하네스 프로그램은 `wmain`으로 들어오므로 한글 경로 같은 비 ASCII 인자도 UTF-8로 그대로 전달됩니다.

### 벤치 exe (M3 최소판, M7 완성)

`out/<preset>/bin/PhysicsTestBench.exe`는 씬 하나를 백엔드 하나로 돌리고 결과 파일을 씁니다 (렌더러 없음, 1 run = 1 process). M7에서 측정 프로토콜을 갖췄습니다:

```
PhysicsTestBench.exe -Backend=PhysX -BackendProfile=UE4Compat -Scenario=M2b.Pyramid1240 -Threads=4 [-Affinity=0x55]
                     [-RunMode=Bench|Viz] [-Instrument=counters,contacts,...] [-HashEvery=N] [-StatesEvery=N] [-CountersEvery=N]
                     [-CaptureEvery=N] [-PreRoll=0.5] [-HostCheck=enforce|record] [-ContaminationPct=5] [-Repeat=i -Order=j -BlockSeed=s]
PhysicsTestBench.exe -Gate=G8 -Runs=3 -Backend=Jolt -Scenario=M3.BoxPile -Param:N=1000
PhysicsTestBench.exe -Gate=G11 -Backend=Bullet -Scenario=M3.BoxPile -Param:N=1000
```

- **실행 순서와 규약 (M7):** 호스트 제어(affinity, HIGH_PRIORITY_CLASS, power throttling opt-out, `timeBeginPeriod(1)`. 전원 계획은 읽기만) → 설정 → 백엔드 → 씬과 `desc_sha256` → effective 덤프와 preflight(필수 키, G0, G9, G10a) → Prepare → 0.5 s spin pre-roll → frame 0 → 측정 루프 → 출력 → `exit.json`(마지막) → teardown. 정의는 [docs/TimingContract.md](docs/TimingContract.md), 파일 형식은 [docs/ResultsSchema.md](docs/ResultsSchema.md)에 있습니다. 종료 코드: 0 ok, 2 명령줄·preflight·게이트 실패, 3 엔진 오류·Fatal, 4 오염된 호스트(run의 코어 = 마스크 CPU와 그 SMT 형제의 다른 프로세스 바쁨 > 코어당 5% × 코어 수, M7 수정 라운드 0. M8 러너가 4를 fresh process로 최대 2번 다시 돈다), 5 용량 초과, 6 백엔드 없음. 러너는 프로세스 종료 코드보다 `exit.json`을 믿습니다.
- **헤드라인 자격:** `manifest.json`의 `timing_valid_for_headline`은 bench 모드·계측 tier 0·FixedStep·프레임별 recorder 없음·호스트 제어 적용·pre-roll 0.5 s 이상·오염 없음·타이밍 버킷 안 할당 0·`PT_DO_CHECK=0`(test 프리셋)·시험 주입 없음, M10의 G10b Fail 없음(수정 라운드 1: T = 1에서 센서스 없음·경합 창의 Skip도 자격 없음)·invariant TSC와 유효한 TSC/QPC 보정(잔차 < 1e-5, 수정 라운드 1부터 run마다)·프로파일러 빌드 아님이 모두 참일 때만 참입니다. `"unofficial"`은 M10부터 판정입니다: 헤드라인 자격, 그 run 키의 유효한 적합성 스탬프, High performance/Ultimate 전원 계획이 모두 참일 때만 false입니다 (사유는 `unofficial_reasons`).
- **M10 측정:** 버킷은 TSC(rdtscp + lfence)이고 run 전체 QPC 보정으로 ns가 됩니다 (`clock.calibration`, 잔차 < 1e-5). 스레드 센서스가 cpu_eff와 외부 스레드 cycle을 재고 T = 1에서 G10b를 판정합니다 (`threads.census_window`; 최소 창은 own CPU 시간 50 ms, 다른 부하가 5%를 넘는 경합 창의 판정 없는 Skip은 헤드라인 자격 없음). 배치 측정 `summary.windows.<w>.batch_frame_ns`는 프레임 사이의 행 기록과 보정 쌍까지 든 하네스 바닥 지표입니다 (P0 헤드라인 값이나 그 후보가 아닙니다, M10 수정 라운드 0). `PhysicsTestBench -AbiOverhead [-Backend=NullDll]`가 ABI 호출 비용(프레임당 4호출)을 잽니다 ([docs/TimingContract.md](docs/TimingContract.md) §2.1, §7.1).
- 아래는 M3~M6 설명입니다.

```
PhysicsTestBench.exe -List
PhysicsTestBench.exe -Backend=Jolt -Profile=Normalized -Scenario=M3.BoxPile -Param:N=5000 [-Frames=600] [-Threads=1]
                     [-BackendProfile=Default] [-Session=<이름>] [-Out=<run 디렉터리>]
PhysicsTestBench.exe -Describe -Backend=Jolt -Scenario=M1.SingleBoxDrop -Param:Case=b
PhysicsTestBench.exe -Conformance -Backend=PhysX -BackendProfile=UE4Compat -Profile=UEParity [-Gates=G1,G4] [-Out=<디렉터리>]
PhysicsTestBench.exe -Conformance -Backend=Null -Gates=A0
PhysicsTestBench.exe -Conformance -Backend=Jolt -BackendProfile=Default -Profile=Normalized [-StampDir=<디렉터리>]   (M10: 스위트 전체면 스탬프)
PhysicsTestBench.exe -SceneHashes=Config/Golden/scene_hashes.json
```

- **적합성 (M6):** `-Conformance`는 물리 게이트(기본 G1~G7, G9, J1, G12, `-Gates=`로 고름. J1은 M9 수정 라운드 0의 조인트 계약)나 ABI 게이트 하나(A0~A7, A10)를 그 프로세스의 백엔드로 돌려 `conformance.json`을 씁니다 (대상, exe SHA-256, ABI 버전, tolerances·프로필·변형 SHA-256, 게이트 씬 `scene_sha256`, 게이트별 결과와 노트). 실패한 게이트가 있으면 종료 코드 2입니다 (`conformance.json`은 남습니다). M10: 스위트에 G10b(자식 run)가 더해졌고, `-Gates=` 없이 시험 주입 없이 돌면 적합성 스탬프 `Saved/Conformance/<키 SHA-256>.json`을 씁니다 (키: exe·DLL 백엔드 파일·변형·물리 프로필·tolerances 해시, 게이트 스위트 버전, 명령줄 오버라이드, [docs/TimingContract.md](docs/TimingContract.md) §9.1). `PT_MODULE_NO_REINIT` 백엔드(Chaos)는 프로세스마다 ABI 게이트 하나 또는 물리 게이트를 돌고, 둘 이상의 ABI 게이트나 섞은 목록은 종료 코드 2로 거부됩니다. `-SceneHashes=`는 그 빌드 구성의 게이트 씬 해시를 `scene_hashes.json`에 씁니다. `-List`, `-Describe`, `-SceneHashes`의 로그는 stderr로 가고 stdout은 순수 JSON입니다.

- **출력** (`Saved/Results/<session>/<ULID>/`, `-Out`으로 바꿀 수 있음): `timings.csv`(프레임마다 T_push/T_step/T_pull/T_frame와 QPC tick, M10: TSC tick·Tick 전체·원시 TSC/QPC), `manifest.json`(빌드·백엔드·프로필·씬 해시·플랫폼·윈도별 통계·마지막 프레임 `h_native`), `effective_settings.jsonl`(Prepare 전), `effective_settings_after.jsonl`(마지막 프레임 뒤), `scene.json`.
- **종료 코드 (M3):** 0 ok, 2 잘못된 명령줄·프로필·씬·파라미터, 백엔드가 거부한 엔진 키 또는 필수 effective 키 누락, 3 엔진 오류, 5 용량 초과(백엔드가 effective 키 `errors.capacity_bits`로 알린 bit만 있을 때. 다른 오류 bit가 함께 있으면 3), 6 백엔드 없음. M7에서 4와 `exit.json`이 더해졌고 `HashEvery`/`CountersEvery` > 0은 더는 거부하지 않습니다 (위).
- **비공식:** 모든 타이밍은 비공식입니다. 측정 프로토콜은 M7, 전원 계획 확인은 M8 러너, 적합성 스탬프는 M10에 생겼습니다. 이 PC는 Balanced라 manifest의 `"unofficial"`이 참으로 남습니다 (전원 계획은 읽어서 기록만 합니다).
- 씬 정의는 [docs/Scenes.md](docs/Scenes.md), 결과 형식은 [docs/02_Architecture.md](docs/02_Architecture.md) §12에 있습니다.

### 뷰어 (M4, 1단계)

`out/dev/bin/PhysicsTestViewer.exe`(dbg, dev 프리셋)는 씬 하나를 Chaos Debug Draw 방식으로 그립니다 (Win32 + D3D11 + Dear ImGui docking). 물리는 메시지 루프 스레드에서 렌더 프레임마다 한 프레임 돕니다.

```
PhysicsTestViewer.exe [-Backend=Jolt] [-BackendProfile=..] [-Profile=Normalized] [-Scenario=M1.SingleBoxDrop] [-Param:N=1000]
                      [-ExecCmds="p.Chaos.Solver.DebugDrawCollisions 1, p.Chaos.DebugDraw.Radius 0"] [-Paused] [-WarmupFrames=N]
                      [-ResX=1600 -ResY=900] [-NoVSync] [-Frames=N -Exit] [-Headless] [-Screenshot=<png>] [-PerfReport=<json>]
                      [-NoUI | -UI=Stats,Engines,Inspector,Console] [-Camera=X,Y,Z,Pitch,Yaw]
```

- **패널:** Engines(레지스트리의 백엔드, 변형, 프로필, 씬과 파라미터, 스레드, Apply, 재생/일시정지/한 스텝/리셋/스크린숏, "재시작 필요" 안내), Inspector(바디 목록 + 선택한 바디의 상세), Stats(`stat ChaosCounters` 이름, 단계별 시간: 모두 "viz mode, not a benchmark"), Console(CVar와 명령: `help`, `stat ChaosCounters`, `pause`, `step`, `reset`, `cvars <filter>`, `open <Backend> [<Variant>|-]`, `restart`). Show 메뉴가 `p.Chaos.Solver.DebugDraw*` 스위치를 켜고 끕니다.
- **엔진 전환 (M5):** Bullet과 PhysX 변형 4종이 Engines 목록에 나옵니다. PhysX "(Default / none)"(= `UE4Compat`, 변형 파일의 `DefaultForEngine`)·`UE4Compat`·`-TGS`·`-Stab` 사이는 같은 프로세스에서 바뀌고, `SdkDefault`로(또는 그 반대로) 바꾸면 "Restart required"와 재실행 버튼이 나옵니다 (PxPhysics/PxTolerancesScale은 프로세스당 하나). 콘솔 `restart`도 같습니다. `-ViewerCmds="open PhysX SdkDefault, restart"`는 콘솔 명령을 프레임마다 하나씩 실행합니다 (자동 검사용). `-Headless` 프로세스의 재실행은 창 없이 같은 실행 제어로 자식을 띄우고 그 종료 코드로 끝납니다.
- **조작 (UE 에디터 규약):** RMB + 마우스 = 시점, RMB + WASD = 이동, E/Q = 월드 위/아래, C/Z = FOV, RMB 중 휠 = 속도 ±10 %, 휠 = 전진/후진, 왼쪽 드래그 = 지면 이동, 가운데 드래그 = 팬, Alt + 왼쪽 드래그 = 궤도, F = 선택(없으면 씬) 맞추기, 클릭 = 선택, P = 일시정지, `.` = 한 스텝, Ctrl+R = 리셋, F9 = 스크린숏(`Saved/Screenshots/`, 옆에 `mode = viz` `.json`).
- **Chaos 예산:** 기본값은 Chaos와 같습니다 (`p.Chaos.DebugDraw.MaxLines 20000`, `p.Chaos.DebugDraw.Radius 3000` cm, 영역 중심은 카메라). UE 5.8.3처럼 캡처와 렌더 두 단계에서 예산과 영역을 보므로(렌더에서 박스 하나가 24), `M3.BoxPile` 1k도 기본값이면 박스 833개만 그려지고 그 뒤의 접촉·bounds는 빠집니다. Stats가 두 단계의 비용과 거부 수를 보여 줍니다. 다 보려면 Show > "Draw everything" 또는 `-ExecCmds="p.Chaos.DebugDraw.MaxLines 0, p.Chaos.DebugDraw.Radius 0"`을 씁니다.
- **헤드리스:** `-Headless`는 창 없이 같은 경로로 오프스크린에 그리고 `-Frames`(기본 1) 뒤에 끝납니다 (자동 검사, 문서 그림). `-PerfReport`는 프레임별 CPU·GPU 시간 요약을 씁니다.
- **시간 읽기:** 창 모드 vsync에서는 스레드가 프레임마다 vsync를 기다리며 쉬어 Stats의 "Snapshot + draw"가 연속 부하 값의 2~3배로 보입니다. 성능을 볼 때는 `-NoVSync`(지원되면 tearing으로 갱신 주기에 묶이지 않음)나 `-Headless`를 씁니다. 기준 4b(스냅샷 + 드로 ≤ 4 ms)는 이 연속 부하 값으로 판정합니다.
- 백엔드에 스냅샷이 없으면(Null) 호스트가 당겨 둔 바디 상태로 그립니다 (접촉·섬 없음).
- **PowerShell:** Windows PowerShell 5.1은 따옴표 없는 `-Scenario=M3.BoxPile`을 점에서 잘라 넘깁니다 (뷰어가 "unknown scenario 'M3'"를 냅니다). PowerShell에서는 `'-Scenario=M3.BoxPile'`처럼 점이 든 인자를 따옴표로 감싸거나 cmd에서 실행합니다.
- **하네스 물리 CVar**(`p.ComNudgeAffectsInertia`, `p.SensitiveSleepThresholdMultiplier`)는 Console에서 값을 보기만 하고 바꾸지 않습니다. 씬의 설정이 정하는 값이라 살아 있는 씬 아래에서 바꾸면 다음 바디 생성에서 Fatal이 되기 때문입니다. 프로필이나 커맨드라인(`-ExecCmds=`, `-ini:`)으로 정하면 Apply / Reset이 다음 씬에 적용합니다.

![뷰어: M1.SingleBoxDrop a, Jolt, 정지 접촉(접촉 원, COM 축, 질량). InertiaScale 0으로 찍었습니다 (기본값 1이면 마젠타 관성 상자가 정육면체를 덮습니다)](docs/images/m4_viewer_m1_jolt.png)

### 빌드 자기보고

- `out/<preset>/BuildFlags.json`: 빌드 단계에서 `compile_commands.json`으로부터 만듭니다 (SDK 자신의 TU 포함, TU마다 `/arch`, `/fp`, CRT, `/std`, 최적화, 실효 `_ITERATOR_DEBUG_LEVEL`, 주요 정의(`/D "X"` 형태 포함), 정규화한 명령의 SHA-256). PhysX 구성(debug/release)은 PhysX TU의 정의에서 읽습니다. 그 SHA-256은 `Generated/BuildFlagsSha.h`로 exe에 들어갑니다 (`PhysicsTestAutomation.exe -BuildInfo`). 내용이 같으면 두 파일을 다시 쓰지 않으므로, 변경 없이 다시 configure해도 재컴파일·재링크가 없습니다.
- `out/<preset>/Generated/PTBuildInfo.h`: configure 시점 사실(프리셋, 구성, 프로필, SDK SHA).
- `Tools\check_physx_isolation.cmd <preset> [--clean]`: PhysX를 끈 트리(`out/<preset>-nophysx`)를 만들어 하네스·Jolt·Bullet TU의 플래그가 PhysX 유무와 무관한지 비교합니다 (03 M0 완료 기준 4). `--clean`은 끝난 뒤 그 트리(프리셋당 약 0.5 GB)를 지웁니다.

### M0 측정값 (2026-10-21, i5-13600KF, 전원 계획 Balanced)

| 항목 | 값 |
|---|---|
| 빈 `out/`에서 `test` configure + build (file:// 미러) | 13.1 s + 42.7 s = **55.7 s**, 다시 빌드하면 `ninja: no work to do.` (수정 라운드 후 다시 잼: 14.4 s + 41.7 s = 56.1 s, 813 TU) |
| 빈 `out/`에서 `dev` / `dbg` configure + build | 71.3 s / 32.8 s |
| 컴파일 TU / 경고 | 813 TU(.rc 4개 제외), 컴파일러 경고 0 (세 프리셋) |
| `BuildFlags.json` (test) | Jolt 139 TU + Jolt 소비 하네스 TU 2개만 `/arch:AVX2`, PhysX 466·Bullet 180·하네스 16·ImGui 10 TU는 `/arch` 없음. 전부 `/fp:fast` + `-MD`(dbg `-MDd`), `_ITERATOR_DEBUG_LEVEL` 0(dbg 2), PhysX `/std:c++14`, 위반 0 |
| PhysX on/off 비교 (dbg, dev, test) | PhysX 외 346 TU의 정규화 명령이 모두 같음 |
| 스모크 z(60) (세 SDK, 세 프리셋) | 5.0183330 m (기준 5.0183328 m, 차이 1.4e-7 m) |

### Python 환경

```bat
py -3.11 -m venv .venv
.venv\Scripts\python -m pip install -r Tools\ptbench\requirements.lock
.venv\Scripts\python -I -c "import numpy, pandas, pyarrow, scipy, matplotlib, jsonschema"
```

- `Tools/ptbench/requirements.lock`은 `pip freeze` 결과 그대로입니다 (numpy 2.4.6, pandas 3.0.6, pyarrow 26.0.0, scipy 1.17.1, matplotlib 3.11.2, jsonschema 4.26.0과 그 의존성). 직접 의존성 목록은 `requirements.in`입니다.
- 오프라인 재설치: `pip download --only-binary=:all: -d Tools\ptbench\wheelhouse -r Tools\ptbench\requirements.lock`로 받아 두고 `pip install --no-index --find-links Tools\ptbench\wheelhouse -r Tools\ptbench\requirements.lock`. wheelhouse(약 103 MB)와 `.venv`는 git이 무시합니다.
- M8: ptbench 패키지를 editable로 설치합니다 (파일은 복사되지 않고 경로만 등록됩니다). `.venv`의 setuptools 65.5.0은 wheel 패키지가 있어야 해서 `requirements-build.txt`에 고정했습니다 (setuptools 65.5.0은 Python 3.11.9 venv에 들어 있고, wheel 0.45.1은 한 번 PyPI에서 받습니다). 오프라인 재설치까지 하려면 wheelhouse를 만들 때 `-r Tools\ptbench\requirements-build.txt`도 함께 받아 둡니다: `pip download -d Tools\ptbench\wheelhouse wheel==0.45.1`. 이 PC의 지금 wheelhouse(M0)에는 wheel이 없습니다. pip 버전은 고정하지 않습니다 (PEP 660 editable 설치는 pip 21.3 이상이면 됩니다. 이 `.venv`는 26.2.2). 설치한 뒤 CMake를 다시 configure해야 Python CTest가 Skipped가 아니라 실행됩니다.

```bat
.venv\Scripts\python -m pip install -r Tools\ptbench\requirements-build.txt
.venv\Scripts\python -m pip install --no-build-isolation --no-deps -e Tools\ptbench
.venv\Scripts\python -I -m ptbench --help
```

### 오케스트레이션과 분석 (M8, `Tools/ptbench`)

```bat
rem 환경 게이트만 (읽기 전용: 전원 계획, 2 s 유휴 < 3%를 최대 --idle-wait 120 s 동안, 다른 PhysicsTest 프로세스). 거부되면 종료 코드 3
.venv\Scripts\python -I -m ptbench envcheck
rem smoke 매트릭스를 무인으로 돌리고 리포트까지 (Balanced인 이 PC에서는 --allow-balanced가 필요하고 리포트 머리에 표시됩니다)
.venv\Scripts\python -I -m ptbench run --matrix smoke --allow-balanced --report
rem 이미 있는 세션의 분석 (report는 run 디렉터리나 여러 세션도 받고, 머리의 전원 계획 표시는 모든 run의 manifest에서 읽습니다)
.venv\Scripts\python -I -m ptbench report Saved\Results\smoke-<UTC>
rem parquet: runs, frames, effective, gates
.venv\Scripts\python -I -m ptbench load Saved\Results\smoke-<UTC>
rem DET1 행렬과 첫 발산 프레임. 종료 코드 0 equal, 1 DIFFERENT, 3 incomplete
.venv\Scripts\python -I -m ptbench determinism Saved\Results\determinism-<UTC>
.venv\Scripts\python -I -m ptbench analytic --check-cpp out\test\bin\PhysicsTestBench.exe
rem M10: 적합성 스탬프. perf 매트릭스의 run은 스탬프 없는 셀이 하나라도 있으면 프로세스를 띄우기 전에 거부합니다 (종료 코드 3)
rem determinism·behavior 매트릭스는 스탬프 상태를 기록만 하고 돕니다 (매트릭스 "stamps": "record"가 기본, --require-stamps로 강제)
rem exe나 DLL을 다시 빌드하면 그 exe의 스탬프가 모두 낡습니다 (/Brepro 없이는 다시 링크할 때마다 SHA가 바뀜): 세션 전에 다시 stamp
.venv\Scripts\python -I -m ptbench stamp --matrix smoke
.venv\Scripts\python -I -m ptbench stamp --matrix smoke --check
rem 지워진·다시 빌드된 exe·DLL의 스탬프와 그 conformance run을 지웁니다 (--check와 함께면 목록만)
.venv\Scripts\python -I -m ptbench stamp --prune --check
rem M10: 단일 SDK exe로 같은 셀 (바이너리 구성 대조군. 리포트 §2c가 test 세션과 함께 비교합니다)
.venv\Scripts\python -I -m ptbench stamp --matrix smoke --preset test-jolt --target jolt.Default
.venv\Scripts\python -I -m ptbench run --matrix smoke --preset test-jolt --target jolt.Default --allow-balanced
rem M9: 거동 매트릭스(behaviour run: states.ptst + scene_desc.json, 타이밍 주장 없음)와 거동 분석 (리포트 §7, behavior.json, differences.csv, PNG)
.venv\Scripts\python -I -m ptbench run --matrix behavior --allow-balanced --report
rem 종료 코드 0 분석함, 1 완료 기준 fail, 2 거동 run 없음
.venv\Scripts\python -I -m ptbench behavior Saved\Results\behavior-<UTC> --out <dir> --json <file>
```

- 세션은 `Saved/Results/<matrix>-<UTC 시각>/`에 `session.json`, `index.csv`, `runs/`(벤치 run 디렉터리, `child.log`), `analysis/`, `report/<날짜>/`를 남깁니다 ([docs/ResultsSchema.md](docs/ResultsSchema.md) §11). `Saved/`는 커밋하지 않습니다.
- 판정은 `exit.json`입니다. 원시 종료 코드는 기록만 하고, `exit.json`이 없는 run은 실패입니다. 종료 코드 4는 fresh process로 최대 2번 다시 돌고 모두 4이면 셀이 `contaminated`입니다 ([docs/TimingContract.md](docs/TimingContract.md) §5.2).
- **M10 러너:** 스탬프 확인(프로세스 전. perf 매트릭스는 요구(`require`), determinism·behavior 매트릭스는 기록(`record`), `--skip-unstamped`·`--allow-unstamped`는 우회로 기록), CV 게이트(CV > 2%인 셀만 추가 블록 최대 2번, 그래도 넘으면 `noisy`), sentinel(처음·중간·끝, drift > 2%면 세션 플래그), 세션의 `unofficial` 판정. 리포트는 comparability key마다 표 하나, 중앙값의 bootstrap 95% CI, cpu_eff, 주장 표(§2b: Shipped·MatchedAVX2 두 세션이 있을 때), 바이너리 구성 대조 표(§2c)를 냅니다. 스탬프 없는 run은 표에 들어가지 않습니다 (M8·M9 세션은 `--allow-unstamped`로 분석하고 표마다 표시됩니다).
- 분석 명령은 없는 입력 경로를 종료 코드 2로 거부하고, `--session`의 디렉터리가 이미 있으면 run도 종료 코드 2입니다.
- 추가 벤치 스위치(매트릭스·타깃의 `bench_args`, `--bench-arg`)는 한 셀에서 같은 키를 두 번 쓸 수 없고 러너가 넘기는 스위치(`-Frames`, `-Out` 등)도 쓸 수 없습니다. 벤치는 스위치의 첫 출현만 읽으므로(`FParse::Value`) 중복은 말없이 무시되기 때문입니다 (종료 코드 2, [docs/ResultsSchema.md](docs/ResultsSchema.md) §11).
- 리포트의 모든 표 제목 밑과 모든 그림 제목 위에 조건(UNOFFICIAL, High performance가 아닌 전원 계획, 게이트 우회)이 적힙니다.
- 콘솔 출력은 UTF-8입니다 (`python -I`는 `PYTHONIOENCODING`을 무시하므로 ptbench가 stdout을 직접 UTF-8로 바꿉니다). 파일로 돌려받은 출력(`> out.txt`)도 UTF-8로 읽습니다.
- 유휴 검사는 사용 중인 데스크톱에서 2 s 창을 최대 120 s 동안 다시 봅니다. 그래도 거부되면 `--idle-wait`을 늘리거나 `--allow-busy`로 우회합니다 (우회는 리포트 머리에 남습니다).

### 디버거

dbg 프리셋 exe(`out/dbg/bin/*.exe`, PDB 포함)를 디버깅합니다. 선택은 사용자 몫입니다 (04 Q7).

- **VS Code + C/C++ 확장 (cppvsdbg, 권장 기본값):** [.vscode/launch.json.example](.vscode/launch.json.example)과 [.vscode/settings.json.example](.vscode/settings.json.example)을 `.vscode/`에 이름을 바꿔 복사합니다. CMake Tools 확장이 `CMakePresets.json`을 읽고, 프리셋의 `architecture`/`toolset`(strategy external) 힌트로 MSVC 환경을 불러옵니다.
- **CLion 2026.1 (설치되어 있음):** 폴더를 열면 `CMakePresets.json`의 dbg/dev/test가 보입니다. Settings > Build, Execution, Deployment > Toolchains에서 "Visual Studio" 툴체인(경로 `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`, 아키텍처 amd64)을 고르고, CMake는 `%USERPROFILE%\.cmake-deps\cmake\win\x64\bin\cmake.exe`, 빌드 도구는 `%USERPROFILE%\.cmake-deps\ninja\win\x64\ninja.exe`로 지정합니다. 환경 변수 `PT_SDK_MIRROR=D:/PhysicsRefs`는 CMake 프로필의 Environment에 넣습니다. 디버거는 기본값 "Bundled LLDB"(MSVC PDB 지원)를 씁니다.
  - M0에서 CLion에 딸린 LLDB(`bin\lldb\win\x64\bin\lldb.exe`)를 배치 모드로 실행해 `out/dbg/bin/PtSmokeBullet.exe`의 `BulletSmoke.cpp:81`에 중단점을 걸고 멈춤, 지역 변수 읽기(`Box.m_worldTransform.m_origin.m_floats[2] = 5.01833296`), 호출 스택을 확인했습니다. IDE 안에서의 확인은 사용자가 합니다.
- VS 2022 IDE를 설치해 "폴더 열기"로 써도 됩니다 (VS 생성기 프리셋은 두지 않습니다).

### UE 5.8.3 소스 점검 (읽기 전용 확인만)

| 항목 | 결과 |
|---|---|
| `D:\UnrealEngine\Engine\Build\Build.version` | 5.8.3, Changelist 0, CompatibleChangelist 55116800, BranchName UE5 |
| `Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll` | 있음 (3,203,584 bytes, 2026-09-26) |
| `Engine\Build\BatchFiles\Build.bat`, `.uedependencies` | 있음 |
| 번들 .NET SDK | `Engine\Binaries\ThirdParty\DotNet\10.0` 있음 |
| `Build.bat -help`로 UBT 실행 확인 | **사용자 확인 대기.** UBT는 엔진 트리에 파일을 쓰므로 하네스 작업에서는 실행하지 않았습니다. M2.5 전에 사용자가 실행하고 결과를 여기에 적습니다 |

## 디렉터리

```
CMakeLists.txt  CMakePresets.json  CMakeUserPresets.json.example  THIRD_PARTY_NOTICES.md
cmake/            PTOptions, PTToolchain, PTFlagProfiles, PTModules, DumpBuildFlags, PTBuildInfo.h.in
cmake/ThirdParty/ Jolt, PtBullet, PhysX, PhysXBootstrap, ImGui, SmallDeps, PtFetch (.cmake)
Config/           BasePhysicsTest.ini, Profiles/UEParity.ini, Profiles/Normalized.ini (프로필 ini 계층, M1 초안),
                  Profiles/Backend/Jolt.Default.ini (M3), Bullet.Default.ini, PhysX.{UE4Compat,UE4Compat-TGS,
                  UE4Compat-Stab,SdkDefault}.ini (백엔드 변형, M5), Golden/tolerances.json, Golden/scene_hashes.json (M6)
Source/Runtime/   Core (UE 관용구 최소 구현, ini, CVar), PhysicsTestAbi (C ABI, Kit), BackendCommon (단위·결합·접촉 규약,
                  셰이프 래핑 규칙, 빌드 자기보고), PhysicsCore (UE 데이터 모델, BodyUtils, FStepPlanner, 프로필 리졸버,
                  FPhysScene), PhysicsTestConformance (ABI 게이트, 물리 게이트 M6), PhysicsTestScenarios (씬과 레지스트리, 게이트 씬 M6),
                  PhysicsTestBenchCore (클럭 버킷, 통계, 호스트 사실), PhysicsTestBenchmark (벤치 실행, 결과 파일),
                  PhysicsDebugDraw (Chaos Debug Draw 파이프라인, M4), D3D11DebugRenderer (뷰어 렌더러, M4)
Source/Backends/  NullBackend (static + DLL), JoltBackend (M3), BulletBackend, PhysXBackend (M5)
Source/Programs/  PhysicsTestAutomation (doctest 단위 테스트), PhysicsTestBench (벤치 exe, M3), PhysicsTestViewer (뷰어, M4)
Source/Editor/    PhysicsTestEditor (뷰어 세션, 패널, 카메라 컨트롤러, M4)
Source/Tests/     SdkSmoke (스모크 3종), Probes (CTest 프로브), AbiTwin (Kit 분리 시험 백엔드)
Tools/            cmake.cmd, ctest.cmd, vcenv.cmd, check_physx_isolation.cmd, CompareBuildFlags.cmake, validate_manifest.py, ptbench/ (M8: python -I -m ptbench)
docs/             설계 문서와 Progress.md
prototypes/       타당성 검토 때의 일회성 코드 (기록용, 빌드하지 않음)
```

## 라이선스 메모

- 이 저장소에는 Unreal Engine 소스와 UE 소스를 옮긴 코드를 넣지 않습니다. Chaos 연결 코드는 로컬 UE 설치본으로만 컴파일하며, 빌드 결과물(DLL/PDB, Intermediate 등)은 커밋하지 않습니다. Core의 UE 관용구는 이름과 동작만 맞춘 자체 구현이며, 동작을 따른 곳에는 UE 파일:줄을 주석으로 적었습니다.
- 서드파티 라이선스는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 정리했습니다. PhysX SDK(`physx/`)는 Apache-2.0, Jolt는 MIT, Bullet은 zlib, Dear ImGui·ImPlot·nlohmann/json·doctest는 MIT, stb는 MIT 또는 퍼블릭 도메인, Tracy(M10 prof 전용)는 BSD-3입니다.
- 벤치마크 결과는 UE EULA와 각 SDK 라이선스를 확인할 때까지 공개하지 않습니다.
