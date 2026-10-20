# PhysicsTest

Unreal Engine의 **Chaos**가 **Bullet**, **PhysX**, **Jolt**보다 실제로 느린지, 그리고 같은 입력에서 시뮬레이션 **결과가 어떻게 다른지**를 측정하는 Windows용 테스트 환경입니다.

- **빌드:** CMake + Ninja + MSVC (Windows 전용)
- **시각화:** Chaos Debug Draw 스타일의 단순한 와이어프레임 뷰어 (Win32 + Direct3D 11 + Dear ImGui)
- **구조:** Unreal 방식(UE 이름과 기본값, `FPhysScene` / `FBodyInstance` / `FPhysicalMaterial` …)을 따르되 Actor/Component는 없앴습니다. 렌더 씬과 물리 씬도 나누지 않고 하나로 둡니다.
- **백엔드:**

| 백엔드 | 버전 | 상태 |
|---|---|---|
| Bullet | 3.25 (`2c204c49`) | 1단계 |
| PhysX | 5.11.0 (`da950a35`), UE4 방식 설정 (`UE4Compat`) + SDK 기본값 (`SdkDefault`) | 1단계 |
| Jolt | 5.6.0 (`e77f1755`) | 1단계 |
| Chaos | UE 5.8.3, UBT Program DLL + C ABI | 나중에 직접 구현 |

> **현재 상태 (2026-10-21):** M0(스캐폴드·툴체인·SDK 연결·환경 준비)를 마쳤습니다. 세 SDK, 작은 의존성, 하네스 Core가 Ninja 트리 하나로 빌드되고 SDK 스모크 3종과 CTest 프로브가 통과합니다. 결과와 완료 기준 표는 [docs/Progress.md](docs/Progress.md)에 있습니다. 다음 단계는 [작업 계획](docs/03_WorkPlan.md)의 M1(PhysicsCore 데이터 모델)입니다.

## 문서

| 문서 | 내용 |
|---|---|
| [01_Feasibility.md](docs/01_Feasibility.md) | 타당성 검토: SDK 빌드 실험, hello 테스트, 렌더러 프로토타입, Chaos 단독 실행 경로, 공정성 리스크 |
| [02_Architecture.md](docs/02_Architecture.md) | 아키텍처: 디렉터리, CMake 타깃, 핵심 타입(UE 대응표), 단일 씬, C ABI 경계, 백엔드 내부(UE4 방식 PhysX), 스텝/타이밍 계약, 프로필, 뷰어, 벤치 러너 |
| [03_WorkPlan.md](docs/03_WorkPlan.md) | 마일스톤 M0~M15, 완료 기준, 공수, 크리티컬 패스 |
| [04_DecisionLog.md](docs/04_DecisionLog.md) | 고정 결정, 설계 결정 로그, 사용자 확인이 필요한 질문 |
| [appendix/A_HeaderSketches.md](docs/appendix/A_HeaderSketches.md) | 핵심 헤더 선언 초안 (C ABI, 씬 타입, 백엔드 Kit) |
| [Progress.md](docs/Progress.md) | 마일스톤별 진행 기록: 만든 것, 완료 기준 결과, 설계와 다르게 한 점, 후속 작업 |
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

- 모든 프리셋은 Ninja 단일 구성이고 빌드 트리는 `out/<preset>`입니다. `RelWithDebInfo`/`MinSizeRel`은 configure에서 거부합니다.
- 공통 플래그: 전 타깃 `/fp:fast`, `/MD`(dbg는 `/MDd`), `/Zi`, LTO 없음. `/arch:AVX2`는 Jolt와 Jolt를 쓰는 TU에만 붙습니다 (Shipped 프로필). 하네스 타깃은 C++20, `/W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor`입니다. 전역 `CMAKE_CXX_STANDARD`는 쓰지 않습니다 (PhysX `/std:c++14 /WX`가 깨짐).
- `test-avx2`는 M7, `prof`·`test-<engine>`은 M10에서 추가합니다.

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
| `PhysicsTest.Automation` | `PhysicsTestAutomation.exe`(doctest): Core 관용구 단위 테스트 (UE 회전·합성 순서, SHA-256/FNV, FParse 등) |
| `PhysicsTest.Smoke.{Jolt,Bullet,PhysX}` | SDK 스모크: SI, Z-up, g = 9.80, damping 0, sleep 끔. z(60) = 5.0183328 m ± 2e-5. Jolt는 설정 문자열(AVX2, Profile/Debug Renderer/FP Exceptions 없음)도 검사. `RunSmoke.cmake`가 `PT_SMOKE_JSON` 줄이 유효한 JSON인지도 확인 |
| `PhysicsTest.Probe.ThirdPartyIncludes` | Core를 먼저 include한 TU가 래퍼 쌍을 거쳐 doctest·ImGui·ImPlot·nlohmann/json을 include하고 컴파일됨 |
| `PhysicsTest.Probe.NegativeScaleFatal` | 음수 스케일 `FTransform` 합성이 모든 프리셋(`PT_DO_CHECK=0`인 test 포함)에서 Fatal로 끝남 (종료 코드 3) |
| `PhysicsTest.Probe.FlagLeak` | Jolt를 PRIVATE로 링크한 정적 lib의 소비자 TU에 `__AVX2__`와 `JPH_*`가 없음 |
| `PhysicsTest.Probe.BuildFlags` | `BuildFlags.json`에 플래그 프로필 위반이 0개 (모든 TU의 `_ITERATOR_DEBUG_LEVEL`이 같은지 포함) |
| `PhysicsTest.Probe.RefsClean` | `D:\PhysicsRefs` 세 저장소의 `git status --porcelain`(`--ignored` 포함)이 비어 있고 PxConfig.h/.dxil/.spv가 없음. 프로브 자신도 `--no-optional-locks`로 읽기만 함 |
| `PhysicsTest.Probe.LongestObjectPath` | 가장 긴 컴파일러 오브젝트 경로 < 240자 |
| `PhysicsTest.Probe.PinGuard` | 잘못된 SDK 태그, 미러(대소문자 바꾼 경로 포함)·빌드 트리 밖 소스 디렉터리, 다른 HEAD(임시 저장소)에서 configure가 FATAL |

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
Source/Runtime/   Core (UE 관용구 최소 구현), BackendCommon (빌드 자기보고, SDK include 매크로)
Source/Programs/  PhysicsTestAutomation (doctest 골격)
Source/Tests/     SdkSmoke (스모크 3종), Probes (CTest 프로브)
Tools/            cmake.cmd, ctest.cmd, vcenv.cmd, check_physx_isolation.cmd, CompareBuildFlags.cmake, ptbench/
docs/             설계 문서와 Progress.md
prototypes/       타당성 검토 때의 일회성 코드 (기록용, 빌드하지 않음)
```

## 라이선스 메모

- 이 저장소에는 Unreal Engine 소스와 UE 소스를 옮긴 코드를 넣지 않습니다. Chaos 연결 코드는 로컬 UE 설치본으로만 컴파일하며, 빌드 결과물(DLL/PDB, Intermediate 등)은 커밋하지 않습니다. Core의 UE 관용구는 이름과 동작만 맞춘 자체 구현이며, 동작을 따른 곳에는 UE 파일:줄을 주석으로 적었습니다.
- 서드파티 라이선스는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 정리했습니다. PhysX SDK(`physx/`)는 Apache-2.0, Jolt는 MIT, Bullet은 zlib, Dear ImGui·ImPlot·nlohmann/json·doctest는 MIT, stb는 MIT 또는 퍼블릭 도메인, Tracy(M10 prof 전용)는 BSD-3입니다.
- 벤치마크 결과는 UE EULA와 각 SDK 라이선스를 확인할 때까지 공개하지 않습니다.
