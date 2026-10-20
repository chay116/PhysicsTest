# 진행 기록 (Progress)

> **관련 문서:** [01_Feasibility.md](01_Feasibility.md) · [02_Architecture.md](02_Architecture.md) · [03_WorkPlan.md](03_WorkPlan.md) · [04_DecisionLog.md](04_DecisionLog.md) · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)
> **근거 표기:** **[V]** 이 마일스톤에서 직접 빌드·실행해 확인 · **[R]** 추정

마일스톤마다 만든 것, [03_WorkPlan.md](03_WorkPlan.md)의 완료 기준 결과, 설계와 다르게 한 점과 그 이유, 후속 작업을 적는다. 커밋과 태그는 리드가 한다.

---

## M0. 스캐폴드·툴체인·SDK 연결·환경 준비 (2026-10-21)

**상태:** 완료. 완료 기준 12개 중 11개 통과, 1개(11, 디버거 중단점)는 사용자 확인 대기다. 환경 표의 "UE 5.8.3 소스 빌드 가능 점검(`Build.bat -help`)"도 사용자 확인 대기다. 독립 검사가 지적한 21건(주요 3, 경미 18. 같은 지적 2쌍 포함)을 수정 라운드 0에서 반영하고 영향받는 기준을 다시 확인했다 (§6).

**검증 환경:** Windows 11 Pro 10.0.26200, i5-13600KF (20 logical), MSVC 14.44.35207 (cl 19.44.35229), Windows SDK 10.0.26100, CMake 4.1.2, Ninja 1.13.2, Git 2.53, Python 3.11.9. 전원 계획은 Balanced 그대로 두었다. SDK는 모두 `PT_SDK_MIRROR=D:/PhysicsRefs`(file:// 미러)에서 clone했고, 작은 의존성은 GitHub에서 받았다.

### 1. 만든 것

| 영역 | 파일 | 내용 |
|---|---|---|
| 저장소 기본 | `.gitignore`(병합), `.gitattributes`(유지), `.editorconfig`(신규), `README.md`(환경·빌드 절 갱신), `THIRD_PARTY_NOTICES.md` | 9개 의존성과 라이선스·고지 의무, UE 소스 점검 결과, 디버거 안내 |
| 루트 CMake | `CMakeLists.txt`, `CMakePresets.json`(dbg/dev/test), `CMakeUserPresets.json.example` | 프리셋은 Ninja, `out/${presetName}`. 옵션 `PT_WITH_JOLT/BULLET/PHYSX`(ON), `PT_WITH_CHAOS`(OFF, 아무것도 하지 않음), `PT_WITH_VIEWER`(ON, test는 OFF) |
| 툴체인 | `cmake/PTOptions.cmake`, `PTToolchain.cmake`, `PTFlagProfiles.cmake`, `PTModules.cmake` | SDK 핀과 URL(`PT_SDK_MIRROR` 규약), 핀 가드, MSVC x64 가드, RelWithDebInfo/MinSizeRel 거부, `/MD` CRT를 SDK보다 먼저, `/fp:fast` 전역, `/Zi`, Release `/DEBUG:FULL /OPT:REF,ICF`, LTO 끔, `pt_target_defaults()`, `pt_apply_arch()`, 모듈·프로그램 헬퍼 |
| ThirdParty | `cmake/ThirdParty/Jolt.cmake`, `PtBullet.cmake`, `PhysX.cmake`, `PhysXBootstrap.cmake`, `ImGui.cmake`, `SmallDeps.cmake`, `PtFetch.cmake` | SDK 3종은 고정 SHA로 clone → 미러 내 빌드 거부 → `git rev-parse HEAD` 검사 → `add_subdirectory(... EXCLUDE_FROM_ALL SYSTEM)`. 래퍼 `PT::Jolt`, `PT::Bullet`, `PT::PhysX`, `PT::ImGui`/`PT::ImGuiHeaders`, `PT::Stb`, `PT::Json`, `PT::Doctest` |
| 빌드 자기보고 | `cmake/DumpBuildFlags.cmake`, `cmake/PTBuildInfo.h.in`, `Source/Runtime/BackendCommon/Public/PtBuildReport.h` | 빌드 단계 `add_custom_command(OUTPUT BuildFlags.json Generated/BuildFlagsSha.h DEPENDS compile_commands.json)`(내용이 같으면 다시 쓰지 않음), TU마다 실효 `_ITERATOR_DEBUG_LEVEL`, configure 시점 `PTBuildInfo.h`, 어댑터 TU 자기보고 매크로 |
| Core | `Source/Runtime/Core` (`PT::Core`) | `FVector`/`FRotator`/`FQuat`/`FTransform`/`FBox`(double, UE 합성 순서), `FMath`, `UE_LOG`과 로그 카테고리, `check`/`checkf`/`verify`/`ensure`/`ensureMsgf`/`checkNoEntry`(`PT_DO_CHECK`), `FCommandLine`, `FParse`(UE 의미), `FPlatformTime`(QPC), `FPlatformMisc`, `FFnv`, `FSHA256`, `FRandomStream`(mt19937_64, seed 0x5EED), 하네스 측 `Misc/PtThirdPartyIncludesStart.h`/`End.h` |
| BackendCommon | `Source/Runtime/BackendCommon` (`PT::BackendCommon`, INTERFACE) | `PtBuildReport.h`, 백엔드 측 `PtThirdPartyIncludes.h`(경고만 push/pop) |
| Automation 골격 | `Source/Programs/PhysicsTestAutomation` | doctest 러너, `-BuildInfo`, Core 테스트 17개(`PhysicsTest.Core.*`) |
| SDK 스모크 | `Source/Tests/SdkSmoke` | `PtSmokeJolt/Bullet/PhysX.exe`: SI, Z-up, g = 9.80, damping 0, sleep 끔, dt = (float)(1/60), 60 스텝. CTest는 `RunSmoke.cmake`로 돌려 `PT_SMOKE_JSON` 줄을 `string(JSON)`으로 검증 |
| CTest 프로브 | `Source/Tests/Probes` | 제3자 include 컴파일, 음수 스케일 합성 Fatal, 플래그 누수(jolt seam), BuildFlags 위반, 참조 저장소 깨끗함, 가장 긴 오브젝트 경로, 핀 가드 |
| 도구 | `Tools/cmake.cmd`, `Tools/ctest.cmd`, `Tools/vcenv.cmd`, `Tools/check_physx_isolation.cmd`, `Tools/CompareBuildFlags.cmake` | vcvars64(vswhere) + `%USERPROFILE%\.cmake-deps`의 CMake/Ninja, PhysX on/off 플래그 비교(`--clean`이면 비교 트리 삭제) |
| Python | `.venv`(git 무시), `Tools/ptbench/requirements.lock`, `requirements.in`, `Tools/ptbench/wheelhouse/`(git 무시, 103 MB) | numpy 2.4.6, pandas 3.0.6, pyarrow 26.0.0, scipy 1.17.1, matplotlib 3.11.2, jsonschema 4.26.0 + 의존성 15개 |
| 디버거 준비 | `.vscode/launch.json.example`, `.vscode/settings.json.example`, README의 CLion 안내 | cppvsdbg 설정 5개, CMake Tools 설정. 프리셋에 `architecture`/`toolset`(strategy external) 힌트 |
| 문서 | `docs/Progress.md`(신규), `docs/02_Architecture.md` §3·§4.3, `docs/03_WorkPlan.md` M0 작업 4 | 디렉터리 트리에 M0 경로 추가, PvdSDK 문장 정정 (아래 §3) |

CTest는 프리셋마다 11개다: `PhysicsTest.Automation`, `PhysicsTest.Smoke.{Jolt,Bullet,PhysX}`, `PhysicsTest.Probe.{ThirdPartyIncludes,NegativeScaleFatal,FlagLeak,BuildFlags,RefsClean,LongestObjectPath,PinGuard}`. 세 프리셋 모두 11/11 통과했다 [V] (`NegativeScaleFatal`은 수정 라운드 0에서 더했다).

### 2. 완료 기준 결과

| # | 기준 (03 M0) | 결과 | 증거 [V] |
|---|---|---|---|
| 1 | 빈 `out/`에서 file:// 미러로 `test` configure + build가 4분 안. 재빌드는 `ninja: no work to do`. dbg, dev도 빌드 | **통과** | `out/` 전체 삭제 후 `Tools\cmake.cmd --preset test` 13.1 s + `Tools\cmake.cmd --build --preset test` 42.7 s = **55.7 s**. 다시 빌드하면 `ninja: no work to do.` 빈 트리에서 dev 71.3 s, dbg 32.8 s, 각각 재빌드 `ninja: no work to do.` 세 프리셋 모두 컴파일러 경고 0 (775개 오브젝트를 컴파일한 test 로그 기준). **수정 라운드 0 뒤 다시 잼:** 빈 `out/test`에서 14.4 s + 41.7 s = 56.1 s (813 TU, 경고 0), 다시 빌드 `no work to do`. 변경 없이 다시 configure하면 `Generating BuildFlags.json` 한 단계만 돌고 재컴파일·재링크가 없다 (전에는 `PhysicsTestAutomationMain.cpp` 재컴파일과 재링크가 따라왔다) |
| 2 | `D:\PhysicsRefs` 세 저장소의 `git status --porcelain`이 비어 있음. PxConfig.h, .dxil, .spv가 새로 생기지 않음 | **통과** | 세 프리셋 빌드와 PhysXBootstrap 빌드 뒤 `PhysicsTest.Probe.RefsClean`: JoltPhysics/PhysX/bullet3 모두 `git status --porcelain` → empty, `--ignored` → empty, 미러와 저장소 소스 트리에서 PxConfig.h/.dxil/.spv 0개. PxConfig.h는 `out/<preset>/_deps/physx-src`에만 생겼다. **수정 라운드 0:** 프로브는 이제 `git --no-optional-locks`(와 `GIT_OPTIONAL_LOCKS=0`)로 읽기만 한다. CTest 전후로 미러 세 저장소의 `.git`과 `.git/index` mtime이 같다 (전 프로브는 `index.lock`을 만들고 지웠다) |
| 3 | `BuildFlags.json`: Shipped에서 Jolt TU와 Jolt를 링크한 TU만 `/arch:AVX2`, PhysX·Bullet·하네스는 `/arch` 없음. 전 TU `/fp:fast` + `-MD`(dbg `-MDd`), `-MT` 없음, PhysX `/std:c++14`. `compile_commands.json`이 바뀌면 다시 만들어짐 | **통과** | test: 812 TU — sdk.jolt 139 (AVX2 139), 하네스 17 (AVX2 2 = `PtSmokeJolt`, `PtProbeJoltSeam`, 나머지 15 none), sdk.physx 466 (none, c++14), sdk.bullet 180 (none), ImGui 10 (none). 전부 fp fast, CRT MD (dbg는 MDd), 위반 0 (`PhysicsTest.Probe.BuildFlags`). PhysX 구성은 PhysX TU의 정의에서 dbg `debug`, dev/test `release`. 재생성: `compile_commands.json`을 touch하고 빌드 → `Generating BuildFlags.json` 실행 후 다음 빌드는 `no work to do`. `-DPT_DO_CHECK=ON`으로 다시 configure → 재생성되고 `PT_DO_CHECK=1` TU가 0 → 17개, 되돌리면 다시 0. **수정 라운드 0:** 813 TU (하네스 18 = AVX2 2 + none 16). `/D "X"` 형태 정의(PhysX의 `_DEBUG`, `NDEBUG`, `_ITERATOR_DEBUG_LEVEL=0`, `_HAS_ITERATOR_DEBUGGING=0`)도 읽고, TU마다 실효 `_ITERATOR_DEBUG_LEVEL`을 기록해 기대값(dbg 2, dev/test 0)과 다르면 위반으로 센다. 세 프리셋 모두 전 TU가 기대값이고 위반 0. 가짜 `compile_commands.json`으로 불일치 TU 2개가 위반 2건으로 잡히는 것을 확인했다 |
| 4 | PhysX를 켜고 끈 두 빌드에서 하네스·Jolt·Bullet TU의 플래그와 CRT가 같음 (dbg, dev, test) | **통과** | `Tools\check_physx_isolation.cmd <preset>`: dbg, dev, test 모두 PhysX를 끈 트리의 345 TU(하네스, Jolt, Bullet, ImGui)가 켠 트리와 **정규화한 cl 명령 전체가 같다** (345/345). 켠 트리에만 있는 467 TU는 PhysX 타깃과 `PtSmokePhysX`뿐이다. `PhysXBootstrap`으로 바꿀 필요가 없었다 (R6 미발생). **수정 라운드 0:** 세 프리셋을 다시 비교해 346/346 (새 프로브 TU 포함), 켠 트리에만 467 TU. `--clean`으로 비교 트리를 지웠다 |
| 5 | Jolt를 PRIVATE로 링크한 정적 lib의 소비자 TU에 `__AVX2__`와 `JPH_*` 없음 | **통과** | `PhysicsTest.Probe.FlagLeak`: `consumer TU: __AVX2__ not defined, JPH_* not defined`, 라이브러리 TU 자기보고 `"__AVX2__":1`, Jolt 설정 문자열에 AVX2. `BuildFlags.json`에서도 `SeamConsumer.cpp`는 `arch ""`, JPH 정의 없음 |
| 6 | 세 SDK 모두 z(60) = 5.0183328 m ± 2e-5 m | **통과** | 세 프리셋 × 세 SDK 모두 z(60) = 5.018332958 m, 기준 5.018332814 m, 차이 1.445e-7 m. vz(60) = -9.7999983 m/s. **수정 라운드 0:** CTest가 `RunSmoke.cmake`로 세 SDK의 `PT_SMOKE_JSON` 줄을 `string(JSON)`으로 파싱한다. PhysX의 `PX_PHYSICS_VERSION`은 이제 `"5.11.0"`(전에는 식 텍스트라 JSON이 아니었다). Jolt 바닥도 100 x 100 x 2 m로 맞췄다 |
| 7 | Jolt TU 안에서 본 `JPH::GetConfigurationString()`에 AVX2, Profile/Debug Renderer/FP Exceptions 없음 | **통과** | test/dev: `Single precision x86 64-bit with instructions: SSE2 SSE4.1 SSE4.2 AVX AVX2 F16C LZCNT TZCNT FMADD (16-bit ObjectLayer) (ObjectStream) (C++ RTTI) (C++ Exceptions)`. dbg는 `(Assertions)`, `(Debug)`가 더해진다. 스모크가 문자열을 검사한다. `BuildFlags.json`의 Jolt PUBLIC 정의는 `JPH_OBJECT_STREAM` 하나 |
| 8 | 고정 SHA와 다른 태그면 configure FATAL. 가장 긴 오브젝트 경로 < 240자 | **통과** | `PhysicsTest.Probe.PinGuard`(configure 4회): `PT_JOLT_GIT_TAG=v5.5.0` → FATAL, 다른 40자리 SHA → FATAL, `FETCHCONTENT_SOURCE_DIR_JOLT=<미러>` → "inside the read-only mirror" FATAL, 다른 HEAD의 checkout → "expected the pinned SHA" FATAL. 가장 긴 컴파일러 오브젝트 경로 170자(test), 169자(dev, dbg): `out\test\_deps\physx-build\sdk_source_bin\CMakeFiles\PhysXExtensions.dir\__\__\physxmetadata\extensions\src\PxExtensionAutoGeneratedMetaDataObjects.cpp.obj`. **수정 라운드 0:** 6회로 늘렸다. 대문자로 바꾼 미러 경로 → "inside the read-only mirror" FATAL, `PT_SDK_MIRROR` 없이 빌드 트리 밖 디렉터리 → "outside this build tree's FETCHCONTENT_BASE_DIR" FATAL. 다른 HEAD는 프로브 트리 `_deps` 안의 임시 저장소(빈 커밋 1개)로 검사하므로 이 저장소에 `.git`이 없어도 된다. 끝나면 `_probe` 디렉터리를 지운다 |
| 9 | CTest 프로브 "Core → 래퍼 → doctest·ImGui·ImPlot·nlohmann/json" 컴파일 | **통과** | `PhysicsTest.Probe.ThirdPartyIncludes`: `CoreMinimal.h` 다음 래퍼 안에서 `DOCTEST_CONFIG_IMPLEMENT` + doctest(멤버 `check()`를 쓰는 구현부 포함), imgui.h, implot.h, nlohmann/json.hpp. 뒤에서 `check`/`checkf`/`verify`/`ensure`/`ensureMsgf`가 복원되는지 `#error`로 검사하고 실행해 확인 |
| 10 | `.venv`에서 `python -I -c "import numpy, pandas, pyarrow, scipy, matplotlib, jsonschema"` 성공, `pip freeze` = lock. wheelhouse가 있으면 오프라인 재설치 | **통과** | `.venv` import 성공. `pip freeze`와 `requirements.lock`이 같다. 스크래치에 새 venv를 만들어 `pip install --no-index --find-links Tools/ptbench/wheelhouse -r Tools/ptbench/requirements.lock` → 성공, 그 venv의 `pip freeze`도 lock과 같고 import 성공 |
| 11 | 선택한 디버거로 dbg 프리셋 스모크 exe에 중단점 | **사용자 확인 대기** | 디버거 선택(Q7)과 설치는 사용자 몫이다. 준비: `.vscode/*.json.example`(cppvsdbg), README의 CLion 안내. 사전 검증: CLion에 딸린 LLDB(`C:\Program Files\JetBrains\CLion 2026.1\bin\lldb\win\x64\bin\lldb.exe`)를 배치 모드로 실행해 `out\dbg\bin\PtSmokeBullet.exe`의 `BulletSmoke.cpp:81` 중단점에서 멈추고(`stop reason = breakpoint 1.1`), 지역 변수(`Box.m_worldTransform.m_origin.m_floats[2] = 5.01833296`)와 호출 스택을 읽었다. IDE 안에서 확인하면 이 항목을 닫는다. **수정 라운드 0:** `.vscode/settings.json.example`의 PATH 값(역슬래시 이스케이프 오류로 JSON이 아니었다)을 슬래시로 고쳤다. `.vscode/*.json.example`, `CMakePresets.json`, `CMakeUserPresets.json.example`을 주석을 뗀 뒤 `json.loads`로 확인했다 |
| 12 | `THIRD_PARTY_NOTICES.md`에 9개 항목 | **통과** | PhysX `physx/` Apache-2.0 (NOTICE 의무, 루트 BSD-3과 구분, da950a35의 `physx/`에는 NOTICE 파일 없음), Jolt MIT, Bullet zlib, Dear ImGui MIT, ImPlot MIT, stb MIT/퍼블릭 도메인, nlohmann/json MIT, doctest MIT, Tracy BSD-3 (prof 전용, M10). 각 고정 버전과 SHA-256 포함 |

**환경 표 항목**
- UE 5.8.3 소스 빌드 가능 점검: **사용자 확인 대기.** 읽기 전용 사실만 README에 적었다 (Build.version 5.8.3, `UnrealBuildTool.dll` 있음, `Build.bat`·`.uedependencies`·번들 .NET 10.0 있음). `Build.bat -help`는 UBT가 엔진 트리에 쓰므로 실행하지 않았다.
- 의존성 tarball: GitHub 아카이브를 다시 받아 SHA-256이 스크래치 tarball과 같음을 확인했다 (doctest, nlohmann/json, Tracy 포함 [V]). URL_HASH를 고정했고 오프라인용으로 `PT_DEPS_CACHE`를 두었다. tarball을 저장소 밖 캐시로 옮기지는 않았다 (네트워크가 되므로 필요 없었다).

### 3. 설계와 다르게 한 점

| # | 설계 | 실제 | 이유 |
|---|---|---|---|
| 1 | 02 §4.3: Jolt `SOURCE_SUBDIR Build`, PhysX `SOURCE_SUBDIR physx`로 `FetchContent_MakeAvailable`이 추가 | 세 SDK 모두 `SOURCE_SUBDIR`를 CMakeLists.txt가 없는 경로로 두고 받기만 한 뒤(`PtFetch.cmake`), 미러 내 빌드 거부와 `git rev-parse HEAD` 검사를 하고 `add_subdirectory(... EXCLUDE_FROM_ALL SYSTEM)` | SHA 가드와 "참조 체크아웃에서 빌드 금지" 가드가 SDK의 CMakeLists보다 **먼저** 돌아야 한다. Bullet(DL54)과 같은 방식으로 셋을 맞췄다. 결과는 같다 (criterion 4) |
| 2 | 02 §4.3, 03 M0 작업 4: "PvdSDK는 Debug에서만 링크" | 모든 구성에서 링크된다. 02·03의 문장을 정정했다 | PhysXPvdSDK는 PhysX(PRIVATE)와 PhysXExtensions(PUBLIC)의 링크 의존성이다 (physx/source/compiler/cmake/PhysX.cmake:397-400, PhysXExtensions.cmake:319-324 [V]). release에서는 `PX_SUPPORT_PVD=0`으로 코드가 빠진다 |
| 3 | 02 §3 트리에 없음 | `Source/Tests/{SdkSmoke,Probes}`, `cmake/ThirdParty/PtFetch.cmake`, `Tools/{ctest,vcenv,check_physx_isolation}.cmd`, `Tools/CompareBuildFlags.cmake`, `.editorconfig` 추가. 02 §3 트리에 반영 | 스모크와 프로브는 하네스 모듈도 프로그램도 아니어서 둘 곳이 없었다. 기준 4를 반복 확인할 도구가 필요했다 |
| 4 | 02 §16: `Tools/ptbench/.venv/` | `.venv`는 저장소 루트 (03 §7 10번, 리드 지시). `.gitignore`는 두 경로를 다 막는다. 02 §16 문장도 고쳤다 (수정 라운드 0) | 리드 지시 |
| 5 | 02 §4.4: test는 "RENDER OFF" | test 프리셋은 `PT_WITH_VIEWER=OFF`. ImGui 정적 lib은 만들지 않고, 헤더만 쓰는 INTERFACE `PT::ImGuiHeaders`를 따로 둬서 include 프로브는 세 프리셋 모두에서 돈다 | 기준 9가 프리셋과 무관하게 ImGui/ImPlot 헤더를 요구한다 |
| 6 | A.4 `PT_BUILD_REPORT_JSON`: 고정 필드 뒤에 SdkFields | SdkFields를 앞에 두고 `_MSC_FULL_VER`를 마지막에 둔다. `_DEBUG`, `_M_FP_STRICT`를 더했다 | SdkFields가 비어도 유효한 JSON이 되게 했다 |
| 7 | A.4 하네스 측 쌍: check/checkf/verify/ensure | `checkNoEntry`, `verifyf`, `ensureMsgf`도 park/restore | Core가 이 매크로들도 정의하므로 |
| 8 | 02 §4.6 `BuildFlags.json` | rc.exe로 컴파일되는 PhysX `*.rc` 4개는 제외(`skipped_non_cxx`), `*-build` 아래의 SDK TU(Jolt PCH `cmake_pch.cxx`)는 그 SDK 그룹으로 분류, 타임스탬프를 넣지 않음(SHA-256이 플래그 집합 자체를 식별), 하네스 C++20과 `/GL` 없음도 검사, TU마다 정규화한 명령 SHA-256 기록 | 첫 빌드에서 .rc와 PCH가 거짓 위반 13개로 잡혔다. 내용이 같으면 SHA가 같아야 exe에 넣는 값으로 쓸 수 있다. 명령 SHA는 기준 4 비교에 쓴다 |
| 9 | 03 M0 작업 3: `PTToolchain` | `CMAKE_CXX_SCAN_FOR_MODULES OFF` 추가 | C++20 타깃에 CMP0155가 TU마다 모듈 스캔 단계를 넣는다. 하네스는 모듈을 쓰지 않는다 (UE도 같음) |
| 10 | 기준 3 "Jolt를 링크한 어댑터 TU" | M0에는 어댑터가 없으므로 Jolt include 경로로 컴파일되는 하네스 TU(`PtSmokeJolt`, `PtProbeJoltSeam`)를 "Jolt 소비 TU"로 판정 | 같은 의미를 TU 수준에서 판정할 수 있는 방법 |
| 11 | 기준 8 "가장 긴 오브젝트 경로" | `CMakeFiles` 아래의 컴파일러 오브젝트만 센다 | Bullet 소스 트리에 Wavefront `*.obj` 메시 파일이 있다 |
| 12 | 03 §7 1번: 스크래치 프로토타입을 `Tools/Prototypes`로 복사 | 하지 않음 | 이미 `prototypes/`에 커밋되어 있다 (기록용으로 그대로 둠) |
| 13 | A.5 Core 스케치 | `UE_LOG` 포맷은 UTF-8 `char` 문자열(`TEXT()` 없음), Fatal과 `check` 실패는 로그 후 종료 코드 3. `FParse`의 의도한 차이 두 가지: 숫자 overload는 실제로 숫자를 읽었을 때만 true (UE는 키가 있으면 항상 true이고 `Atoi`/`Atof`로 바꾸므로 `-Threads=abc`가 0, Parse.cpp:709-718), `Bool`은 모르는 값이면 false를 돌려주고 `OnOff`를 그대로 둔다 (UE는 true를 돌려주고 `FCString::ToBool` 결과 false를 넣는다). 키 검색 규칙(앞 글자가 영숫자가 아닐 때만 일치, 따옴표 안 건너뜀)과 `Bool`의 정수 처리(0이 아니면 true)는 UE와 같다. `FTransform`의 음수 스케일 합성(UE의 행렬 경로)은 구현하지 않고 모든 빌드에서 Fatal로 멈춘다 (처음에는 `checkf`였는데 `PT_DO_CHECK=0`인 test에서 빠졌다, 수정 라운드 0). `FRotator::Quaternion()`의 비유한 결과 → Identity 대체는 UE처럼 `PT_DO_CHECK=1`일 때만 한다 (UE: `ENABLE_NAN_DIAGNOSTIC` 또는 `DO_CHECK`, UnrealMath.cpp:538-545). `FMath::Min`/`Max`/`Clamp`는 UE와 같은 비교식이고 `Asin`/`Acos`도 UE처럼 [-1, 1]로 자른다. 다만 `/fp:fast`에서는 NaN 결과가 정해지지 않으므로 (UE 게임 빌드도 같다) NaN 동작은 보장하지 않는다. A.5의 `FVector3f`, `FMatrix`, `FMatrix44f`, `FSphere`, `FColor`, `FLinearColor`는 아직 없다 (렌더러와 디버그 드로가 쓰는 M4에 만든다). `FCycleCounter`는 설계대로 M10 | Windows `TEXT` 매크로와 충돌을 피한다. 숫자·Bool 차이는 벤치 CLI의 오타를 조용히 0이나 false로 바꾸지 않으려는 것이다. 최소 구현 원칙(S5) |
| 14 | 03 M0 환경 표: wheelhouse는 선택 | 만들었다 (103 MB, git 무시) | 받기가 빨랐고 기준 10의 오프라인 절을 실제로 검증할 수 있다 |
| 15 | 02 §4.3 SDK 소스 위치 | `pt_fetch_git_pinned()`는 SDK 소스 디렉터리가 이 빌드 트리의 `FETCHCONTENT_BASE_DIR`(`out/<preset>/_deps`) 안에 있어야 한다. 밖을 가리키는 `FETCHCONTENT_SOURCE_DIR_<SDK>`는 `PT_SDK_MIRROR` 설정 여부와 상관없이 FATAL이다. 경로는 `file(REAL_PATH)` 후 소문자로 비교한다 (수정 라운드 0) | 전에는 `PT_SDK_MIRROR`가 있을 때만, 대소문자를 구분해 미러 안인지 봤다. 미러를 가리키는 override가 대소문자만 달라도, 또는 `PT_SDK_MIRROR` 없이 들어오면 PhysX가 configure 중 미러에 `PxConfig.h`를 쓸 수 있었다 |
| 16 | 03 M0 작업 6·기준 6: 스모크의 판정 방법은 정하지 않음 (M0 첫 구현은 exe 종료 코드) | CTest는 `RunSmoke.cmake`로 exe를 돌려 종료 코드 0과 함께 `PT_SMOKE_JSON` 줄 하나가 유효한 JSON이고 `pass`가 true인지 본다 (수정 라운드 0) | 이 줄은 G0(M7)이 기계로 읽는다. PhysX 줄이 JSON이 아닌 채로 통과했었다 |
| 17 | 03 M0 프로브 목록 | `PhysicsTest.Probe.NegativeScaleFatal` 추가 (CTest 11개) | 음수 스케일 합성이 test(`PT_DO_CHECK=0`)에서도 멈추는지는 프로세스 종료로만 확인할 수 있다 |
| 18 | A.5 `CoreTypes.h` | `PT_DO_CHECK`와 `/Zc:preprocessor`를 `PT::Core`의 PUBLIC 사용 요구로 옮겼다. `CoreTypes.h`는 `PT_DO_CHECK`가 없거나 전통 전처리기면 `#error`로 멈춘다 (전에는 조용히 `PT_DO_CHECK=1`) (수정 라운드 0) | UE의 `DO_CHECK`처럼 빌드 전역이어야 한다. `pt_target_defaults()` 없이 만든 소비자가 다른 값으로 인라인 매크로를 펼치면 ODR 위반이다 |

### 4. 새로 확인한 사실 [V]

- **`/fp:fast`에서 float 상수의 double 변환이 반올림되지 않는다.** MSVC 14.44에서 `inline constexpr float Dt = 1.0f / 60.0f;`의 `static_cast<double>(Dt)`가 `/fp:fast`이면 0.016666666666666666(정확한 1/60), `/fp:precise`이면 0.01666666753590107이다. 그래서 스모크 기준값이 처음에 5.0183333(정확한 1/60 기준)으로 나왔다. `volatile float`에 한 번 저장해 반올림을 강제했다 (기준 5.0183328). DL23의 "`dt_engine = (double)(float)dt`"는 하네스 기본 `/fp:fast`(D5, Q12)에서 상수 접기로 깨질 수 있다 → M1 후속 작업.
- PhysX의 `compile_commands.json`에는 rc.exe 항목 4개(`PhysX*.rc`)가 있다.
- vcvars64.bat(VsDevCmd 확장)은 `vswhere.exe`가 PATH에 없으면 "'vswhere.exe' is not recognized"를 stderr에 찍는다. `Tools\vcenv.cmd`는 Installer 디렉터리를 PATH에 먼저 넣는다. `for /f`로 vswhere를 부를 때는 경로의 `(x86)` 때문에 `call "..."` 형태가 필요하다.
- doctest.h를 include만 해도 전역 테스트 스위트 등록 때문에 구현부 심볼이 필요하다. include 프로브는 `DOCTEST_CONFIG_IMPLEMENT`까지 래퍼 안에서 컴파일한다 (멤버 `check()`를 호출하는 구현부까지 검사하게 되어 더 강하다).
- `JPH::GetConfigurationString()`의 `(C++ RTTI) (C++ Exceptions)`는 호출한 TU의 설정이다. Jolt 자신은 `/GR-`, 예외 없음이고 하네스 TU는 CMake 기본 `/GR /EHsc`다. MSVC에서는 섞어도 링크와 실행에 문제가 없었다 (M3에서 Jolt 어댑터 TU 설정을 정할 때 참고).
- 세 SDK의 60 스텝 자유낙하 결과가 float까지 같다 (z = 5.018332958, vz = -9.799998283).
- CLion 2026.1에 딸린 JetBrains LLDB가 MSVC PDB로 줄 단위 중단점과 지역 변수를 읽는다.
- PhysXBootstrap 대안 경로(dev 프리셋, `-DPT_PHYSX_USE_BOOTSTRAP=ON`)도 configure 시점 release 빌드 후 `PtSmokePhysX`가 통과했다. 다만 이 경로에서는 PhysX TU가 부모의 `compile_commands.json`에 없으므로 `BuildFlags.json`의 `physx_config`가 비어 있다.
- 빌드 트리 크기: test 1.13 GB, dev 1.22 GB, dbg 1.42 GB.
- (수정 라운드 0) **UE 5.8.3 `FQuat4d::Rotator`는 특이점(|SingularityTest| > 0.4999995)에서 Roll = 0이고 Yaw = NormalizeAxis(∓2·atan2(X, W))다** (UnrealMath.cpp:691-717). UE4 시절처럼 Yaw를 두고 나머지를 Roll에 넣는 방식이 아니다. 하네스 `FQuat::Rotator()`를 이에 맞췄다. 같은 회전이지만 오일러 표현이 다르므로, 회전자를 비교하는 테스트는 극점 근처에서 이 규칙을 따라야 한다.
- (수정 라운드 0) **UE의 `FCString::Strifind`는 앞 글자가 영숫자가 아닐 때만 일치한다** (CString.h:722-760). `FParse::Value("-WarmupSteps=10 -Steps=600", "Steps=")`는 UE에서 600이다. 단순 부분 문자열 검색이던 첫 구현은 10을 돌려줬다. `_`와 `-`는 영숫자가 아니므로 `-Warmup_Steps=5`에서는 `Steps=`가 일치한다.
- (수정 라운드 0) **`/fp:fast`에서는 NaN 비교 결과가 정해지지 않는다.** MSVC 14.44 test 프리셋에서 `FMath::Clamp(NaN, -1, 1)`(UE와 같은 `Max(Min(X, Hi), Lo)` 식)이 -1이 됐다 (IEEE대로면 1). UE 게임 빌드도 `/fp:fast`이므로 NaN 동작은 UE와도 보장되지 않는다. 테스트는 NaN 결과를 검사하지 않는다.
- (수정 라운드 0) 그냥 `git status`는 optional lock을 잡는다: `.git/index.lock`을 만들고 지우며, stat 정보가 낡았으면 `.git/index`를 다시 쓴다. 읽기 전용 저장소에는 `git --no-optional-locks`(또는 `GIT_OPTIONAL_LOCKS=0`)를 쓴다. M0 첫 구현의 RefsClean 프로브가 미러 `.git`의 mtime을 바꾼 원인이다 (작업 트리와 추적 내용은 그대로였다).
- (수정 라운드 0) CMake 4.1은 generate할 때마다 `compile_commands.json`을 다시 쓴다. Ninja의 커스텀 명령은 `restat = 1`이므로, 출력 내용이 같을 때 파일을 다시 쓰지 않으면(`file(COPY_FILE ... ONLY_IF_DIFFERENT)`) 뒤따르는 컴파일·링크가 없다.
- (수정 라운드 0) PhysX는 반복자 디버그 관련 정의를 `/D "X"` 형태로 넘긴다 (NvidiaBuildOptions.cmake:58). release는 `_ITERATOR_DEBUG_LEVEL=0`, `_HAS_ITERATOR_DEBUGGING=0`(`/MD`의 MSVC 기본과 같음), debug는 `_DEBUG`만 넘겨 기본값 2가 된다. 그래서 지금은 모든 TU의 실효 값이 같다 (dbg 2, dev/test 0).
- (수정 라운드 0) 스모크 3종이 같은 바닥(100 x 100 x 2 m)을 쓰게 Jolt 바닥을 줄였다. 60 스텝 동안 상자가 바닥에 닿지 않으므로 z(60)은 그대로다.

### 5. 후속 작업

| 대상 | 내용 | 시점 |
|---|---|---|
| H (M1) | 두 가지를 나눈다. (1) 해석 기준값(DL23)은 엔진이 실제로 받는 float로 반올림한 dt로 계산한다: 런타임 float 값(`volatile`이나 `std::bit_cast` 경유)에서 double로 바꾸고, `/fp:fast`에서도 0.01666666753590107이 나오는지 단위 테스트로 고정한다. `PtUnitScale` 등 BackendCommon 상수의 float→double 변환에도 같은 주의. (2) `FStepPlanner`처럼 엔진 입력을 만드는 하네스 코드는 UE 게임 빌드처럼 `/fp:fast`로 둔다. UE도 Game 타깃은 `/fp:fast`다 (VCToolChain.cs:1324-1335, Editor·Program 타깃만 V7부터 precise). 하네스 전체나 `FStepPlanner`에 precise 의미(`#pragma float_control(precise, on)` 등)를 강제하면 UE와 달라진다 (04 §4.1 아래 M0 메모) | M1 |
| H (M1) | `FConfigFile`/`FConfigCacheIni`, `IConsoleManager`(설계상 M1) | M1 |
| H (M3) | Jolt 어댑터 TU의 RTTI/예외 설정을 Jolt와 맞출지 결정 (`/GR-`, `_HAS_EXCEPTIONS=0`는 UE 플래그와도 같다) | M3 |
| H (M7) | G0: TU 자기보고(`PtBuildReport.h`)와 `BuildFlags.json`의 겹치는 사실 대조. PhysXBootstrap 경로를 쓰게 되면 그 트리의 `compile_commands.json`도 읽게 확장 | M7 |
| H (M10) | Tracy v0.14.1 핀(SHA-256 기록됨)으로 prof 프리셋 | M10 |
| H (M5) | PhysX 어댑터 TU에 `PX_DEBUG`/`PX_CHECKED`/`PX_PROFILE`/`PX_SUPPORT_PVD`를 `$<CONFIG>`별로 넘길지, 안 넘기는 이유를 적을지 정한다. 지금 dbg 라이브러리는 `PX_CHECKED=1`인데 어댑터 TU는 `PxPreprocessor.h` 기본값 0을 본다 (인라인·assert ODR 차이). dev의 PhysX 구성(release인가 profile인가)은 Q19 답을 따른다 | M5 |
| H (툴체인) | PhysX TU 466개는 PhysX 자신의 `/WX`로 컴파일된다 (physx/source/compiler/cmake/windows/CMakeLists.txt:23). MSVC를 올렸을 때 SDK 코드 경고로 빌드가 깨지면 `cmake/ThirdParty/PhysX.cmake`에서 PhysX 타깃에 `/WX-`를 더한다 (개별 경고 억제는 하지 않는다). 하네스는 `/W4`, `/WX` 없음 | 툴체인 변경 시 |
| H (M7) | G0가 `PT_SMOKE_JSON`/어댑터 자기보고와 `BuildFlags.json`의 `idl`(실효 `_ITERATOR_DEBUG_LEVEL`)을 함께 읽는다. 값은 이미 JSON으로 검증된다 (`RunSmoke.cmake`, `CheckBuildFlags.cmake`) | M7 |
| U | 디버거 선택(Q7)과 IDE에서 dbg 스모크 exe 중단점 확인 → 기준 11 종료 | M1 전 권장 |
| U | `D:\UnrealEngine\Engine\Build\BatchFiles\Build.bat -help` 실행 결과를 README에 기록 (Q13) | M2.5 전 |
| U | Q19: UE 4.27의 `PhysX.Build.cs`로 Development/Test 구성이 링크하던 PhysX 라이브러리(PROFILE/SHIPPING) 확인 | M5 전 |
| U | 저장소 공개 여부(Q1) 확인 후 첫 push | 첫 push 전 |
| 리드 | 커밋, `m0` 태그 (03 §7 12번) | 지금 |

### 6. 수정 라운드 0 (독립 검사 반영, 2026-10-21)

실패한 완료 기준은 없었다. 지적 21건을 모두 처리했다 (한 건은 M5로 결정을 미루고 질문 Q19로 남겼다). 고친 뒤 세 프리셋을 다시 빌드하고(경고 0) CTest 11/11, 기준 1·2·3·4·6·8을 다시 확인했다 [V].

| 지적 (심각도) | 고친 것 | 확인 [V] |
|---|---|---|
| `FParse` 키 검색이 단순 부분 문자열 검색 (주요) | `Parse.cpp`를 다시 썼다. 키는 따옴표 밖이고 앞 글자가 `[A-Za-z0-9]`가 아닐 때만 일치한다 (UE `Strifind`, CString.h:722-760). 빈 키는 일치하지 않는다. `Param`은 UE 코드를 옮기지 않고 따옴표 상태를 추적하는 공백 토큰 순회로 새로 짰다 (동작은 Parse.cpp:325-350과 같음). `Bool`은 UE `ToBool`처럼 정수를 받는다 (0이 아니면 true). 헤더 주석의 "UE int32 overload도 같다"는 틀린 말이라 고쳤고 의도한 차이는 §3 #13에 적었다 | doctest `ParseKeyNeedsNonAlnumLeadIn`: `-WarmupSteps=10 -Steps=600` → 600, `-PhysXThreads=4 -Threads=1` → "1", `-BenchScene=Other -Scene=M3.BoxPile` → M3.BoxPile, `-Warmup_Steps=5` → 5, 따옴표 안의 `-Steps=3` 무시. `ParseParam`에 따옴표 안 스위치, 접두어 위치, 부분 토큰 사례 추가 |
| `FQuat::Rotator()` 특이점 분기가 UE4 방식 (주요) | UE 5.8.3 `FQuat4d::Rotator`(UnrealMath.cpp:691-717)대로 Pitch = ±90, Yaw = NormalizeAxis(∓2·atan2(X, W)), Roll = 0. 주석과 테스트 설명을 고쳤다 | `QuatRotatorRoundTrip`: (-90, -30, 15) → Yaw -15·Roll 0, (90, 30, 40) → Yaw -10·Roll 0 (극점에서는 Yaw+Roll, Yaw−Roll만 관측된다는 독립 기대값), (-89.99, 120, -170) → Roll 0, Yaw ≈ -50.0087 |
| `.vscode/settings.json.example` PATH가 JSON이 아님 (주요, 경미 중복) | `${env:USERPROFILE}/.cmake-deps/ninja/win/x64;${env:PATH}` (슬래시) | 주석을 뗀 뒤 `json.loads` 통과, PATH 값 확인. `launch.json.example`, `CMakePresets.json`, `CMakeUserPresets.json.example`도 통과 |
| `PT_SMOKE_JSON`(PhysX)이 JSON이 아님 | `PX_PHYSICS_VERSION`을 `"5.11.0"`(MAJOR/MINOR/BUGFIX 리터럴)로 내고 `static_assert`로 식과 대조. CTest는 `RunSmoke.cmake`로 줄을 `string(JSON)` 파싱 (§3 #16) | 세 프리셋 × 세 SDK 통과, PhysX 줄 `"PX_PHYSICS_VERSION":"5.11.0"` |
| 다시 configure하면 재컴파일·재링크 | `DumpBuildFlags.cmake`가 두 출력을 임시 파일에 쓴 뒤 `file(COPY_FILE ... ONLY_IF_DIFFERENT)` (Ninja `restat = 1`) | test: configure 후 빌드는 `Generating BuildFlags.json` 한 단계, 그다음 `no work to do` |
| RefsClean 프로브가 미러 `.git`에 씀 | `git --no-optional-locks` + `GIT_OPTIONAL_LOCKS=0` | CTest 전후 미러 세 저장소의 `.git`, `.git/index` mtime 같음 |
| PhysX `/WX` 위험 | 그대로 두고 위험과 대처(`/WX-`)를 `PhysX.cmake` 주석과 §5에 적었다 | 세 프리셋 경고 0 |
| PinGuard `wrong_head`가 `.git`에 의존 | 프로브 트리 `_deps` 안에 임시 저장소(빈 커밋 1개, 명령줄로 신원 지정)를 만들어 쓴다. 대소문자 바꾼 미러 경로, 빌드 트리 밖 디렉터리 사례 추가, 끝나면 `PT_PROBE_DIR` 삭제 | 6회 모두 기대한 FATAL, `out/<preset>/_probe` 없음 |
| `.editorconfig`의 `[*.{lock,in}]`이 `*.in` 탭 규칙을 덮음 (중복 1쌍) | `[Tools/ptbench/requirements.{in,lock}]`로 좁혔다 | 파일 검토 |
| `out/`에 남은 비교 트리 (약 1.6 GB)와 빈 `_probe` | `check_physx_isolation.cmd <preset> --clean` 추가, PinGuard가 `_probe` 삭제 | 세 프리셋 `--clean` 실행 뒤 `out/`에는 dbg, dev, test만 있다 |
| `+Roll` 방향 주석 오류 | `Rotator.h`, `Math.cpp` 주석을 "+Roll은 +Y를 -Z 쪽으로"로 고쳤다 | `RotatorAxisConventions` (0,0,-1) 그대로 통과 |
| `FParse` 주석 오류와 `Param`의 UE 코드 근접 | 위 첫 줄 참고 | 위 첫 줄 참고 |
| Core 공개 헤더의 사용 요구가 PRIVATE뿐 | `PT::Core`에 PUBLIC `/Zc:preprocessor`, `PT_DO_CHECK=0\|1`. `CoreTypes.h`는 없으면 `#error` (§3 #18) | cl로 직접 컴파일: 정의 없음 → C1189 `PT_DO_CHECK is not defined`, 전통 전처리기 → C1189 `needs the conforming preprocessor`, 둘 다 있으면 성공. `compile_commands.json`에서 하네스 TU마다 각 플래그가 한 번씩(중복 제거) |
| 음수 스케일 합성이 test에서 조용히 지나감 | `checkf` 대신 항상 `UE_LOG(LogCore, Fatal, ...)` (종료 코드 3) | 새 CTest `PhysicsTest.Probe.NegativeScaleFatal`이 세 프리셋에서 종료 코드 3과 Fatal 줄 확인 (test는 `PT_DO_CHECK=0`) |
| 문서 불일치 | 02 §16의 `.venv` 경로 정정, §3 #13 보강(`FVector3f`/`FMatrix`/`FMatrix44f`/`FSphere`/색 타입 미룸, Asin/Acos clamp, NaN → Identity, NaN 미보장), 04 DL23과 §4.1 아래 M0 메모, §5 M1 후속 작업을 "해석 기준값은 float 반올림, `FStepPlanner`는 UE처럼 `/fp:fast`"로 나눔. 덧붙여 `FMath::Min`/`Max`/`Clamp`를 UE 비교식으로 바꾸고 `FRotator::Quaternion()`에 `PT_DO_CHECK=1`일 때 비유한 결과 → Identity를 넣었다 | 문서 검토. doctest `MathClampAndNonFiniteRotator` (dbg/dev는 Identity, test는 비유한 값 그대로) |
| SDK 소스 위치 가드가 미러 설정·대소문자에 의존 | 빌드 트리 `FETCHCONTENT_BASE_DIR` 안만 허용, `file(REAL_PATH)` + 소문자 비교, 메시지에 대문자 이름 (§3 #15) | PinGuard `in_place`, `in_place_case`, `external_dir`. 빈 `out/test`에서 세 SDK clone이 가드를 통과(`(verified)`) |
| `BuildFlags.json`이 `/D "X"` 정의와 반복자 디버그 수준을 놓침, 생성 TU 분류, `;` 분할 | 세 정의 형태를 순서대로 읽는다. TU마다 실효 `_ITERATOR_DEBUG_LEVEL`(MSVC yvals.h 규칙)을 기록하고 기대값(dbg 2, 그 밖 0)과 다르면 위반. `<BIN>/Generated/` TU는 하네스로 분류, 경로는 `<BIN>`으로 정규화. `compile_commands.json`은 `file(READ)` 후 `;`, `[`, `]`를 자리표시자로 바꿔 줄을 나눈다 | 세 프리셋 위반 0, `cmd_sha256`은 전 덤프와 812 TU 모두 같음. 가짜 입력(불일치 2 TU, `;`가 든 정의, `[`가 든 경로, `/D NAME`, `Generated/` TU)에서 위반 2건과 분류 확인 |
| `PT::PhysX`가 구성별 `PX_*` 매크로를 넘기지 않음, dev의 PhysX 구성 | M5에서 정한다. `PhysX.cmake` 주석, §5 후속 작업, 04 Q19(사용자 질문, 4.27 확인)로 남겼다 | 해당 없음 (결정 보류) |
| 스모크 바닥 크기 서술 불일치 | Jolt 바닥을 `BoxShape(50, 50, 1)`로 맞춤 | z(60) 그대로 (세 프리셋) |

---

## M1. PhysicsCore 데이터 모델 (2026-10-21)

**상태:** 완료. [03_WorkPlan.md](03_WorkPlan.md) M1 완료 기준 6개 묶음(BodyUtils 5, FStepPlanner 고정값 6, 해석 기준값 1, 프로필 4, 수학 1, 충돌 필터 1)을 모두 통과했다 [V]. 리드가 M1에 붙인 요구(세 프리셋 녹색·경고 0, `dt_engine`의 `/fp:fast` 접기 방지와 그 테스트, M0 경미 지적 (a)~(e))도 모두 처리했다. 사용자 확인이 필요한 기준은 없다. 독립 검사의 지적 15건은 수정 라운드 0(§6)에서 처리했고, 아래 숫자는 그 뒤의 빈 `out/` 빌드에서 다시 쟀다.

**검증 환경:** M0와 같다 (Windows 11 Pro 10.0.26200, i5-13600KF, MSVC 14.44.35207, CMake 4.1.2, Ninja 1.13.2, `PT_SDK_MIRROR=D:/PhysicsRefs`). 전원 계획은 Balanced 그대로다. UE 소스(`D:\UnrealEngine` 5.8.3)는 읽기만 했고 UBT는 실행하지 않았다.

### 1. 만든 것

| 영역 | 파일 | 내용 |
|---|---|---|
| Core (M1 추가) | `Public/Misc/ConfigCacheIni.h` + `Private/Misc/ConfigCacheIni.cpp` | `FConfigFile`/`FConfigSection`/`FConfigValue` 최소판 (`FConfigCacheIni`는 쓰는 곳이 없어 수정 라운드에서 뺐다). UE 문법 부분집합(`Key=`, `+`, `.`, `-`, `!`, 따옴표, 줄 주석, BOM). 섹션 이름과 키는 UE처럼 대소문자를 무시한다. 파일의 원래 줄 목록을 남겨 계층 합성(`Combine`)과 값마다 파일·줄 출처를 기록한다 |
| | `Public/HAL/IConsoleManager.h` + `Private/HAL/ConsoleManager.cpp` | `IConsoleManager`, `IConsoleVariable`, `TAutoConsoleVariable<T>`, `FAutoConsoleVariableRef`. bool/int32/float/string, 대소문자 무시, UE `ECVF_SetBy*` 우선순위(낮은 우선순위의 Set은 무시), `ProcessUserConsoleInput`("name value"/"name=value"), 하네스 추가 `GetDefaultString`/`ResetToDefault` |
| | `Public/Containers/StringConv.h` + `.cpp`, `Misc/CommandLine.h`의 `FUtf8ArgV` | UTF-16 ↔ UTF-8(Win32 CP_UTF8), UTF-8 경로 ↔ `std::filesystem::path`, `wmain` 인자를 UTF-8 argv로 (M0 지적 e) |
| | `Misc/Parse.h/.cpp` | unsigned 숫자의 `-` 거부와 모든 정수 overload의 범위 검사 (M0 지적 b), UE `FParse::Token`(따옴표·구분자 규칙) |
| | `Math/Transform.h`, `Math.cpp` | `Inverse()`가 Win64 UE처럼 스케일이 모두 0이면 Identity, 인용을 TransformVectorized로 정정, 헤더의 낡은 "check()로 거부" 문장 정정 (M0 지적 c) |
| PhysicsCore (`PT::PhysicsCore`, 신규) | `Public/Physics/PhysicsInterfaceTypes.h` | `ECollisionEnabled`, `EFrictionCombineMode`, `EObjectStateType`(값 0~4), `ETeleportType`, `ESleepFamily`, `EPhysicsType`, `ECollisionChannel`(0..31), `FPhysicsActorHandle`/`FPhysicsConstraintHandle` |
| | `Public/PhysicsEngine/{ShapeElem,SphereElem,BoxElem,SphylElem,ConvexElem,AggregateGeom,BodySetup}.h` + `Private/PhysicsEngine/AggregateGeom.cpp` | UE 필드와 기본값. `FKBoxElem` X/Y/Z full extent, `FKSphylElem` Length = 로컬 Z 선분, `Rotation`은 `FRotator`, UE `GetFinalScaled`/`GetScaledVolume`/`SetupNonUniformHelper` 규칙(클램프 포함), `CalcAABB`. convex는 데이터만 |
| | `Public/PhysicalMaterials/PhysicalMaterial.h` + `.cpp` | `FPhysicalMaterial`(PhysicalMaterial.cpp:42-60 기본값), combine 모드 해석(큰 값 우선), Chaos 정지마찰 규칙의 기준 구현 `CombinePairFriction` (DL41) |
| | `Public/PhysicsEngine/BodyInstance.h` + `Private/PhysicsEngine/BodyInstance.cpp` | `FBodyInstance`(BodyInstance.cpp:357-418, BodyInstanceCore.cpp:13-24 기본값), `FBodyCollisionFilter`(32채널 상호 차단), `FMassProperties`, `FBodyInstanceState`. `InitBody` → `UpdateMassProperties` → `DeferAddActor`, 머티리얼 대체 순서(바디 → BodySetup → 씬 기본 머티리얼) |
| | `Public/PhysicsEngine/{ConstraintInstance,ConstraintUtils}.h` + `Private/PhysicsEngine/ConstraintInstance.cpp` | ball/hinge 데이터와 UE 이름의 프리셋 `ConfigureAsBallAndSocket`/`ConfigureAsHinge`, `IsBallAndSocket`/`IsHinge`, `GetRefFrame` |
| | `Public/PhysicsEngine/PhysicsSettings.h` + `.cpp` | `FChaosSolverConfiguration`, `FChaosPhysicsSettings`, `FPhysicsSettingsCore`, `FPhysicsSettings`(UE 기본값), `FPhysicsTestSettings`(tick mode, clamp rule, 질량 규칙, sleep 축, 반발 임계, 실행 옵션), enum 이름 |
| | `Public/PhysicsEngine/BodyUtils.h` + `Private/PhysicsEngine/BodyUtils.cpp`, `Private/Physics/Matrix33.*` | 질량 규칙 8단계(02 §5.3). Chaos 셰이프 체적·관성(float `UE_PI`, 캡슐 float 4/3), CombineWorldSpace 평행축 합성, Jacobi 대각화. `BuildScale3D`는 UE처럼 곱하지 않는다 |
| | `Public/Physics/PhysicsConsoleVariables.h` + `Private/Physics/PhysicsConsoleVariables.cpp` | 하네스 물리 CVar 표: `p.ComNudgeAffectsInertia`(BodyUtils 8단계), `p.SensitiveSleepThresholdMultiplier`(Sensitive sleep 배수). 한 TU에 모았고 리졸버가 참조하므로 정적 라이브러리에서 무엇을 링크하든 등록된다. 리졸버는 이 표만 하네스 CVar로 본다 (수정 라운드) |
| | `Public/Physics/StepPlanner.h` + `.cpp` | `FSetUpForFrameParams`, `FStepPlan`, `FStepPlanner`: FixedStep / Sync / Substepping(UE5, UE4) / AsyncFixed, 솔버 루프의 MinDeltaTime 처리 |
| | `Public/Physics/AnalyticReference.h` + `.cpp` | `EngineDeltaTime`(`dt_engine`, `/fp:fast` 접기 방지), 반암시적 오일러 자유낙하 닫힌 식 |
| | `Public/Physics/PhysicsProfile.h` + `.cpp` | `FPhysicsProfile`, `FBackendVariant`, `FProfileResolver`, `FResolvedPhysicsConfig`: 내장 기본값 → Base → 프로필 → 변형 → 씬 고정 → 커맨드라인, 값마다 Origin(파일:줄), 변형 키 제한, `-ini:`/`-ExecCmds`, `physics_sha256`, 템플릿 적용 |
| | `Public/PhysScene.h` + `Private/PhysScene.cpp`, `Public/Physics/PhysSceneStats.h` | `FPhysScene` 골격: `std::deque` 저장소, 지연 액터 배치(생성 순서 = Index), 시작 sleep 단계(sleep을 끈 프로필이면 건너뛰고 센다), 델리게이트 3종, 프레임 상태기계(Idle → SetUp → Started → Waited → Idle), 스텁 백엔드 |
| Config (신규) | `Config/BasePhysicsTest.ini`, `Config/Profiles/UEParity.ini`, `Config/Profiles/Normalized.ini` | Base는 모델링한 키 69개 전부를 UE/하네스 기본값과 [V] 근거와 함께 적는다. 두 프로필은 02 §10 표의 행을 키로 옮긴 초안이다 (M9 완성) |
| 테스트 | `Source/Programs/PhysicsTestAutomation/Private/Tests/{CoreConfig,PhysicsTypes,BodyUtils,StepPlanner,PhysicsProfile,PhysScene}Tests.cpp`, `CoreMiscTests.cpp`/`CoreMathTests.cpp` 보강, `Source/Tests/Probes/{HarnessCVarsProbe.cpp,CheckHarnessCVars.cmake}` | doctest 73 케이스. `wmain` 진입점. CTest에 영역별 `PhysicsTest.Unit.<Area>` 8개, `PhysicsTest.Unit.CommandLineUtf8`, `PhysicsTest.Probe.HarnessCVars`(수정 라운드) 추가 |
| 빌드 (M0 지적 d) | `cmake/ThirdParty/PtBullet.cmake`, `cmake/DumpBuildFlags.cmake`, `Source/Tests/Probes/CheckBuildFlags.cmake`, `Source/Tests/SdkSmoke/BulletSmoke.cpp` | `CLAMP_VELOCITIES=0`, `INTERNAL_UPDATE_SERIALIZATION_STRUCTURES=OFF` 강제. 헤더에 보이는 `BT_THREADSAFE=0`을 Bullet의 모든 타깃과 `PT::Bullet` 소비자에 같이 정의한다. `BuildFlags.json`에 `bullet_header_defines`를 기록하고, Bullet TU끼리 또는 Bullet TU와 소비자 TU 사이의 `BT_*` 차이를 위반으로 센다. 스모크는 `BT_THREADSAFE` 없음과 `BT_CLAMP_VELOCITY_TO` 있음을 `#error`로 막는다 |
| 문서 | `README.md`, `docs/02_Architecture.md` §3·§5.2·§5.3·§9.2·§10, `docs/03_WorkPlan.md` M1 작업 3, `docs/04_DecisionLog.md` §4.1, `docs/appendix/A_HeaderSketches.md` A.0.3 | 아래 §3 |

CTest는 프리셋마다 21개다 (M0 11개 + `PhysicsTest.Unit.{Core,Config,Types,BodyUtils,StepPlanner,Analytic,Profile,PhysScene,CommandLineUtf8}` + `PhysicsTest.Probe.HarnessCVars`). 영역별 항목은 `-tc=PhysicsTest.<Area>.*`로 돌린다. doctest는 걸러진 케이스가 0개여도 종료 코드 0을 내므로 "test cases: N | N passed | 0 failed" 줄(N ≥ 1)을 `PASS_REGULAR_EXPRESSION`으로 요구한다.

### 2. 완료 기준 결과

| # | 기준 (03 M1) | 결과 | 증거 [V] |
|---|---|---|---|
| 1 | BodyUtils: 100 cm 정육면체(기본 머티리얼) 177.83 kg, 296,380 kg·cm² | **통과** | `PhysicsTest.BodyUtils.CubeMassesFromTheWorkPlan`: M = 1000^0.75 = 177.8279410 kg(공식 대비 상대 1e-12, \|M − 177.83\| < 0.005), I = 296,379.90 kg·cm²(\|I − 296380\| < 0.5), COM 0, 회전 항등 |
| 2 | 50 cm 정육면체 37.384 kg, 10 cm 정육면체 1 kg | **통과** | 같은 케이스: 125^0.75 = 37.3837195 kg(\|M − 37.384\| < 0.0005), 1.0 kg |
| 3 | 머티리얼 없음 → 1.0 kg/cm³ (10 cm → 원질량 1000 kg → 177.83 kg) | **통과** | `PhysicsTest.BodyUtils.NoMaterialMeansOneKgPerCm3`: `GetBodyInstanceDensity(nullptr) == 1.0`, 177.8279 kg. 수소 하한 9e-8 kg/cm³도 확인 |
| 4 | `bOverrideMass` 0.0001 → 0.001 kg | **통과** | `PhysicsTest.BodyUtils.OverrideMassFloor`: 0.001 kg, 관성도 M'/M 비율로 바뀜. `!bOverrideMass`의 1 g 하한도 확인 |
| 5 | 단위가 아닌 InertiaTensorScale + 0이 아닌 COMNudge가 순서대로 손계산한 값과 1e-9 상대오차 이내 | **통과** | `PhysicsTest.BodyUtils.InertiaScaleThenComNudgeOrder`: 100×50×20 cm, S = (2, 1, 0.5), n = (4, −3, 10). 기대값(7357.5660, 105957.3835, 115159.6115 kg·cm²)을 테스트 안에서 공식으로 따로 계산했고 상대 1e-9 이내다. 순서를 바꾼 값(넛지 → 스케일)과는 1e-3 넘게 다르고, 세 성분 모두 스케일·넛지 전 값과 다르다 (처음 고른 n_x = 5는 0.75·c²/12 = 25 = n_x²라서 X 성분이 우연히 같아 바꿨다). 1e-3 이내 스케일은 건너뜀, CVar로 8단계를 끄면 넛지 항이 사라짐, 바디 스케일이 넛지에 곱해짐도 확인 |
| 6 | FStepPlanner: dt 0.05 Sync → 1 × 1/30 | **통과** | `PhysicsTest.StepPlanner.SyncClampsToMaxPhysicsDeltaTime`: N = 1, StepDt == float(1/30), Lost ≈ 0.01667 |
| 7 | dt 0.05 UE5ChaosScene → 3 × 1/60 (ceil 입력 2.99999989) | **통과** | `UE5SubstepCeil`: (double)0.05f / (double)float(1/60) = 2.99999989 < 3, N = 3, StepDt == float(1/60) |
| 8 | dt 0.05 UE4PhysSubstep → 2 × 1/60 | **통과** | `UE4SubstepPreClamp`: UseDelta = min(dt, 1/30) 뒤 N = 2, StepDt == float(1/60) |
| 9 | dt 0.2 UE5ChaosScene → 6 × 1/60, 0.1 s 소실 | **통과** | `UE5LosesTimeBeyondMaxSubsteps`: N = 6, Consumed ≈ 0.1, Lost ≈ 0.1 (상대 1e-5) |
| 10 | AsyncFixed 1/30, 프레임 1/60 → 스텝 0, 1 교대 | **통과** | `AsyncFixedAlternates`: 10 프레임 동안 0, 1, 0, 1, …, 누산기 1/60 ↔ 0, `bAsyncInterpolated`, dt = 0 flush 사례, sync는 AsyncDt = −1 |
| 11 | MinDeltaTime 미만 나머지는 이월 없이 소실 | **통과** | `MinDeltaTimeRemainderIsDropped`: MinDt 8 ms에서 5 ms 프레임 3번 모두 0 스텝(이월되었다면 둘째 프레임이 10 ms로 스텝했을 것), 9 ms는 1 스텝. substep 16.7 ms < MinDt 20 ms → 0 스텝, 0.05 s 소실. MinDt가 0이어도 하한 `UE_SMALL_NUMBER` |
| 12 | 해석 기준값 함수가 `dt_engine`으로 계산됨: S00 z60 = 501.8332813705 cm (정확한 1/60이면 501.8333333, 5.2e-5 cm 차이) | **통과** | `PhysicsTest.Analytic.FreeFallReferenceUsesEngineDt`: \|z60 − 501.8332813705\| < 1e-9, v60 = −980.0000511110, 정확한 1/60 기준 501.8333333333, 차이 5.196e-5 cm, 명시적 루프와 일치. `PhysicsTest.Analytic.EngineDeltaTimeSurvivesFastMath`: `EngineDeltaTime(constexpr 1.0f/60.0f)`의 비트가 0x1.111112p-6(0.01666666753590107)이고 1/60과 다르다. 같은 TU의 `static_cast<double>(constexpr float)`는 세 프리셋(dbg 포함) 모두 0.016666666666666666으로 접혔다(테스트 MESSAGE). 헬퍼가 접히면 이 테스트가 실패한다 |
| 13 | 프로필: 변형 ini가 고정 키를 바꾸면 로드 오류 | **통과** | `PhysicsTest.Profile.VariantMayNotChangePinnedKeys`: Normalized 위의 변형이 `BodyInstanceDefaults.LinearDamping`, `PhysicsSettings.DefaultGravityZ`, `PhysicsTestSettings.bAllowSleeping`, 하네스 CVar, 프로필이 정한 CVar, 다른 엔진 섹션을 쓰면 모두 "Bad.ini:<줄>: … pinned key …" 오류. EngineDefault 키(`SubstepClampRule`), 자기 엔진 키, 엔진 CVar는 허용 |
| 14 | 모든 값에 Origin이 붙는다 | **통과** | `PhysicsTest.Profile.EveryValueHasAnOrigin`: Base + Normalized + PhysX 변형 + 씬 고정 + `-ini:` + `-ExecCmds`를 겹친 결과의 값 76개 모두 Origin 이름이 있다. `DefaultGravityZ` → UEDefault `Config/BasePhysicsTest.ini:<줄>`, `LinearDamping` → Profile `Normalized.ini:<줄>`, `SubstepClampRule`·`Backend.PhysX.SolverType` → Variant, `FixedDeltaTime` → ScenePinned, `MaxSubsteps` → CommandLine `-ini:1`, CVar → CommandLine `-ExecCmds:1`. 단위(`cm/s^2`)도 붙는다 |
| 15 | `-ini:`와 `-ExecCmds`가 동작한다 | **통과** | `PhysicsTest.Profile.CommandLineIniOverride`: 한 스위치에 쌍 여러 개, `-ini:Engine:` 별칭, 따옴표로 감싼 토큰, `-ini:Game:` 무시 경고, 형식 오류와 값 오류(`-ini:1`) 거부. `PhysicsTest.Profile.ExecCmdsSetConsoleVariables`: `-ExecCmds="p.ComNudgeAffectsInertia 0, p.Chaos.Solver.Sleep.Enabled=0"` → 하네스 CVar는 `ECVF_SetByConsole`로 설정되어 `BodyUtils` 결과가 M'·10² 만큼 바뀌고, Chaos CVar는 엔진 전달 목록으로 돌아온다 |
| 16 | `BasePhysicsTest.ini`에 PhysicsCore 섹션이 없다 | **통과** | `PhysicsTest.Profile.BaseHasNoPhysicsCoreSection`: `/Script/PhysicsCore`로 시작하는 섹션이 없고, `SolverOptions`(코어 필드)가 `[/Script/Engine.PhysicsSettings]`에 있다. 그런 섹션이 어느 층에 있어도 "… DL44" 오류다. 덧붙여 `PhysicsTest.Profile.BaseMatchesBuiltIn`이 Base에 69개 키가 모두 있고 코드 기본값과 같음을 확인한다 |
| 17 | 수학: quat/transform 합성 순서와 FRotator → FQuat 변환이 UE 순서. FKBoxElem X/Y/Z full extent, FKSphylElem Length = 로컬 Z 선분 | **통과** | M0의 `PhysicsTest.Core.{QuatCompositionOrder,TransformCompositionOrder,RotatorToQuatMatchesMatrix,QuatRotatorRoundTrip}`(독립 회전행렬 대조) + M1의 `PhysicsTest.Types.BoxElemFullExtent`(100×50×20 → 반폭 50/25/10, AABB, yaw 90에서 X·Y 교환, `GetTransform` = `FRotator::Quaternion`), `PhysicsTest.Types.SphylElemLengthIsLocalZSegment`(R25 L100 선분 ±50 cm, 서면 높이 150 cm·누우면 50 cm, 회전 후 선분이 세계 X, UE 스케일 클램프), `PhysicsTest.Core.TransformInverseZeroScale` |
| 18 | 충돌 필터: 32채널 상호 차단 진리표 | **통과** | `PhysicsTest.Types.CollisionFilterTruthTable32`: 32 × 32 채널 쌍 전부를 무작위 마스크(`FRandomStream(1234)`, 채널 0은 전부 차단, 1은 아무것도 차단 안 함)로 독립 공식 `(maskB >> i & 1) && (maskA >> j & 1)`과 대조, 대칭, 채널 31 경계, QueryOnly/NoCollision은 충돌 없음 |

**리드 추가 요구**

| 요구 | 결과 | 증거 [V] |
|---|---|---|
| 세 프리셋 녹색·경고 0 (CTest 11개 이상) | **통과** | 수정 라운드 뒤 빈 `out/<preset>`에서 configure + 빌드: test 14.5 s + 44.6 s(826 단계), dev 13.0 s + 55.0 s(837), dbg 14.0 s + 21.6 s(837). 하네스 TU는 `/W4 /WX`로 컴파일되고(수정 라운드), 세 configure·빌드 로그 모두 `warning` 0줄이며, 다시 빌드하면 `ninja: no work to do.` CTest는 세 프리셋 21/21. doctest 73 케이스(dbg/dev 18,518 단언, test 18,519). `BuildFlags.json` 835 TU(하네스 40), 위반 0, `bullet_header_defines` = [`BT_THREADSAFE=0`]. `PhysicsTest.Probe.RefsClean` 통과(미러 세 저장소 깨끗함) |
| `dt_engine`을 견고하게, 접히면 실패하는 테스트로 고정 | **통과** | 위 #12 |
| (a) `FRandomStream` 골든 값 | **통과** | `PhysicsTest.Core.RandomStreamGoldenValues`: seed 0x5EED와 −1(int32 → uint32 → uint64 경로)의 `GetFraction` 3개(16진 double), `FRand` 3개, `GetUnsignedInt` 3개, `RandRange(−3, 3)` 8개. 기대값은 스크래치의 독립 MT19937-64 구현(표준의 10000번째 출력 9981545732273789042로 검증)으로 계산했고, 같은 표준 값을 `std::mt19937_64`로도 확인한다 |
| (b) `FParse` unsigned의 `-`와 overflow | **통과** | `PhysicsTest.Core.ParseNumericRangeAndSign`: `-Threads=-1`/`-Threads= -1`/`-Affinity=-5` 거부(값 그대로), 4294967296·18446744073709551616·±2^31 경계 밖 거부, 경계값 수용, `+4` 허용 |
| (c) `FTransform` 주석과 0 스케일 `Inverse` | **통과** | `Inverse()`를 Win64 UE(TransformVectorized.h:363-374)에 맞춰 고치고 `PhysicsTest.Core.TransformInverseZeroScale`로 고정. 기본 초기화 차이는 `PhysicsTest.Core.DefaultInitialisation`. 02 §5.2에 #19(기본 초기화), #20(0 스케일 Inverse, 일치), #21(음수 스케일 Fatal) 행을 더했다 |
| (d) `PT::Bullet`의 헤더 가시 옵션 | **통과** | 위 §1 빌드 행. 가짜 `compile_commands.json` 두 개로 확인했다: `BulletSmoke.cpp`에만 `-DBT_CLAMP_VELOCITY_TO=500` → 위반 1건("… differ from Bullet's 'BT_THREADSAFE=0' (inline-code ODR)"), `btRigidBody.cpp`에만 `-DBT_USE_DOUBLE_PRECISION` → 위반 1건("Bullet TUs disagree …") |
| (e) 넓은 문자 명령줄 | **통과** | `PhysicsTestAutomation`이 `wmain` + `FUtf8ArgV`로 들어온다. CTest `PhysicsTest.Unit.CommandLineUtf8`이 `-PtUtf8Probe=한글 경로`를 넘겨 UTF-8 바이트 일치를 확인한다. 대조: 스크래치의 `main(int, char**)` 프로그램은 같은 "한글"을 cp949 바이트 `C7 D1 B1 DB`로 받았다(UTF-8은 `ED 95 9C EA B8 80`). `PhysicsTest.Core.StringConvUtf8`이 서로게이트 쌍과 경로 왕복도 확인한다 |

### 3. 설계와 다르게 한 점

| # | 설계 | 실제 | 이유 |
|---|---|---|---|
| 1 | A.10 `FConfigValue {Value, Unit, Origin, OriginName}` | `FResolvedConfigValue` (+ `bApplied`, `bPhysicsHash`) | Core ini가 UE 이름 `FConfigValue`(값 + 출처 줄)를 이미 쓴다. 같은 네임스페이스에서 이름이 겹친다 |
| 2 | A.10 `Resolve(Base, Profile, Variant, CommandLineIniOverrides, ExecCmds, Out, Err)` | `Resolve(FProfileResolveInputs, Out, Err)`. 입력에 씬 고정층(선택)을 더했고, 커맨드라인은 문자열 전체를 받아 `-ini:`와 `-ExecCmds=`를 직접 찾는다. `ApplyForcedOverrides`는 M9에 둔다. 덧붙여 `ApplyConsoleVariables`, `GetBuiltInDefaults`, `NormalizeValue`, `MakeCanonicalKey` | 02 §10의 계층에 씬 고정값이 있다 (M3 시나리오가 쓴다). 커맨드라인을 나눠 받으면 호출자가 UE 토큰 규칙을 다시 구현해야 한다 |
| 3 | A.7 `FStepPlan {NumSteps, StepDeltaTime, ConsumedTime, LostTime}`, `PlanFrame(dt, Settings, TestSettings, Acc)` | `NumExternalSteps`, `bAsyncInterpolated`, `Params`(UE `SetUpForFrame` 인자)를 더했고, `MakeSetUpForFrameParams` / `GetAsyncDeltaTime` / `PlanFrame(Params, AsyncDt, Rule, Acc)`로 나눴다 (A.7 형태의 오버로드도 둔다) | Native driver(Chaos, Null)는 UE 인자를 그대로 받아야 하고, AsyncDt는 UE에서 씬 생성 인자다 (FChaosScene(nullptr, AsyncDt)). UE가 밀어 넣은 스텝 수와 실제 엔진 스텝 수는 MinDeltaTime에서 갈라진다 |
| 4 | 03 M1 작업 3 "UE의 float 산술 재현" | `FChaosScene::SetUpForFrame`은 float, 솔버의 스텝 수·누산기는 FReal(double), UE4 substep은 float로 계산한다. 03과 02 §9.2 문구를 고쳤다 | UE 소스 확인 결과 (PhysicsSolverBase.h:1013-1017, PhysicsSolverBase.cpp:581-621) |
| 5 | A.6 `FBodyInstance`의 명령 함수(`SetBodyTransform`, `SetLinearVelocity`, `AddForce`, `AddImpulse`, `WakeInstance` 등), A.9 `FPhysScene`의 `EnqueueForce`/`EnqueueKinematicTarget`/해시/스냅샷/`GetBackendScene` | M1에는 넣지 않았다. 대신 UE의 `Scale3D` 필드와 `IsStatic`/`IsKinematic`/`GetInitialObjectState`/`GetMaxAngularVelocityInRadians`, 씬의 `GetFramePhase`/`GetNumDeferredActors`/`GetDefaultPhysicalMaterial`을 더했다 | 명령은 `FPhysicsInterface`와 ABI 명령 버퍼(M2)가 있어야 의미가 있다. 선언만 두면 링크 오류의 함정이 된다. 해시는 M3, 스냅샷·드로 큐는 M4다 |
| 6 | A.6 `FKConvexElem::BuildHull()` | 없다. `UpdateElemBox()`만 있다. 질량에 기여하는 convex 요소는 Error 로그 후 건너뛴다 | quickhull과 convex 질량은 M15 (DL31) |
| 7 | A.6 `ConstraintUtils`를 `ConstraintInstance.h` 안에 | `PhysicsEngine/ConstraintUtils.h`로 분리 | UE와 같은 위치 (Engine/Classes/PhysicsEngine/ConstraintUtils.h). 02 §3 트리도 `ConstraintUtils`를 따로 적는다 |
| 8 | 02 §3 트리 | Core `Containers/StringConv`, PhysicsCore `Physics/AnalyticReference`(dt_engine, 자유낙하), `Private/Physics/Matrix33`을 더했다. 트리를 고쳤다 | `dt_engine`을 견고하게 만들 곳과 Jacobi 대각화가 필요했다. 해석 기준값의 Python 짝(`analytic.py`)은 M8이다 |
| 9 | 03 M1 작업 2 "Jacobi 대각화" / UE의 질량 규칙 | Jacobi는 그대로다. UE `TransformToLocalSpace`의 "이미 대각" 조기 종료를 같이 쓰고, Chaos 셰이프의 float `UE_PI`·float 4/3을 그대로 쓴다. 1·4·5단계는 double(UE는 float, 1단계 밀도 차이는 상대 4.7e-8). 질량 기여 요소가 없으면 Chaos 대체값 523 kg + Error 로그(UE는 질량 계산을 건너뛴다). 02 §5.2 #22, §5.3 메모 | UE와 같은 텐서를 내되, 테스트의 1e-9 허용오차를 유지하려면 double이 필요하다. 엔진은 모두 같은 하네스 값을 받으므로 비교에 영향이 없다. 기여 요소 없는 바디의 질량은 UE에 대응값이 없어 결정적인 값을 고르고 알린다 |
| 10 | 02 §10 표 (UEParity/Normalized 항목) | 행을 키로 옮기며 `FPhysicsTestSettings`에 `bAllowSleeping`과 `RestitutionThresholdVelocity`(0 이하 = 엔진별 UEParity 매핑)를 더했다. Normalized의 최대 각속도 해제는 `bOverrideMaxAngularVelocity=True`, `MaxAngularVelocity=1e7`(deg/s)다 (처음의 3.4e+38은 수정 라운드에서 바꿨다: rad/s로 제곱하면 float +inf). "Average 강제, SF := F"는 프로젝트 combine 모드만 Average로 두고 머티리얼 강제는 M9 `ForcedOverrides`로 남겼다. 반복 횟수와 clamp rule은 두 프로필 모두 EngineDefault, 반발 임계는 UEParity만 EngineDefault다 | 표의 각 행이 어디에 사는지 정해야 했다. 0은 Chaos에서 "회전 정지"다 (§4) |
| 11 | 03 M1 기준 "머티리얼 없음" | `BodyUtils`는 `nullptr` 머티리얼에 1.0 kg/cm³를 쓴다 (기준 그대로). 그러나 `FBodyInstance::GetSimplePhysicalMaterial()`은 UE처럼 바디 → BodySetup → 씬 기본 머티리얼(UE `GEngine->DefaultPhysMaterial`, 1 g/cm³) 순서로 찾으므로, 씬 안의 바디가 "머티리얼 없음"이 되는 일은 없다 | UE와 같은 대체 순서 (BodyInstance.cpp:3178-3240). UE도 기본 머티리얼이 없을 때만 1.0 kg/cm³가 된다 |
| 12 | 02 §9.2 UE4 규칙 | UE4 규칙의 MinDeltaTime 처리는 UE5 솔버 루프와 같은 방식으로 적용했다 [R] | UE4의 해당 코드(4.27)는 로컬에 없다. 사용자가 4.27로 확인할 대상이다 (§5) |
| 13 | ini 문법 | 같은 파일 안에서도 평범한 `Key=Value`는 그 키의 값 전체를 바꾼다. 스칼라 키에 배열 연산자(`+ . - !`)는 오류다. UE 섹션의 모르는 키는 기록만 하고(경고) 하네스 섹션의 모르는 키는 오류다. 구조체 값은 나열하지 않은 필드가 구조체 기본값이 되고(UE ImportText와 같음) 모르는 필드는 경고다 | 하네스는 계층을 명시적으로 합성하므로 한 줄의 의미가 위치와 무관해야 한다. 사용자가 프로젝트의 DefaultEngine.ini를 붙여 넣을 수 있게 하면서 하네스 키의 오타는 잡는다 |
| 14 | 커맨드라인 | `-ini:PhysicsTest:`와 UE 철자 `-ini:Engine:`을 받는다. 다른 ini 이름은 경고 후 무시한다. `-ExecCmds`는 `ECVF_SetByConsole`, ini의 CVar는 `ECVF_SetByConsoleVariablesIni`로 적용한다 | UE는 물리 설정을 Engine ini에 둔다. UE는 ExecCmds를 콘솔 명령으로 실행한다 |
| 15 | `IConsoleVariable::Set` (UE: 반환값 없음, 못 읽으면 0) | `bool Set(...)`: 못 읽는 값, 읽기 전용, 낮은 우선순위는 false로 거부한다. `GetDefaultString`/`ResetToDefault` 추가 | 프로필 오타를 조용히 0으로 바꾸지 않는다. 리졸버가 CVar 기본값에 Origin을 붙이고, 테스트가 전역 상태를 되돌린다 |
| 16 | M0 이후 CTest 11개 | 21개 (영역별 `PhysicsTest.Unit.<Area>` 8개 + `CommandLineUtf8` + 수정 라운드의 `PhysicsTest.Probe.HarnessCVars`) | 실패한 영역이 이름으로 보인다. doctest는 걸러진 케이스가 0개여도 0을 반환하므로 요약 줄을 정규식으로 요구한다 |
| 17 | 설정 파일 위치 | `PhysicsTestAutomation`은 `PT_CONFIG_DIR`(소스 트리의 `Config/`, 컴파일 정의)을 읽고 `-ConfigDir=`로 바꿀 수 있다 | 벤치 exe의 설정 탐색 규칙은 M3에서 정한다 |
| 18 | `BuildFlags.json` 스키마 (M0) | 최상위에 `bullet_header_defines`를 더했다. 스키마 이름은 그대로 `PhysicsTest.BuildFlags/1`이다 | 필드 추가만이고 기존 소비자(`CheckBuildFlags.cmake`, `CompareBuildFlags.cmake`)가 그대로 동작한다 |
| 19 | A.10 effective 설정 API와 해시 이름 | `FEffectiveSetting`, `ParseEffectiveSettings`, `GetRequiredEffectiveKeys`는 만들지 않았다. M2(Null `DumpEffectiveSettings`, A7 게이트)와 M3(`effective_settings.jsonl`)로 미룬다. `FResolvedPhysicsConfig::Sha256()`은 `PhysicsSha256()`이다. `ApplyConsoleVariables(Config, OutEngineVariables, OutError)`는 거부된 CVar를 오류로 돌려준다 | effective 설정은 백엔드가 내는 값이라 ABI 전에는 소비자도 생산자도 없다. `FPhysicsProfile::Sha256()`(프로필 파일 해시)과 `physics_sha256`(리졸브된 물리 값)을 이름으로 구분한다 |
| 20 | A.5 `FConfigFile` / `FConfigCacheIni` | `FConfigCacheIni`는 없다. `FConfigFile`의 섹션 이름은 UE처럼 대소문자를 무시하고 처음 쓴 철자를 유지한다 (처음 구현은 대소문자를 구분했다) | 하네스는 파일을 명시적으로 합성하므로 이름 캐시를 쓰는 곳이 없다. UE `FConfigFile`은 `TMap<FString, FConfigSection>`이다 (ConfigCacheIni.h:502, :574 [V]) |
| 21 | A.6 `PhysicsInterfaceTypes.h`, `BodySetup.h` | 채널 수 C++ 상수는 `NumCollisionChannels`(설계 스케치 주석의 `PT_NUM_COLLISION_CHANNELS`는 C ABI 매크로로만 쓴다). `FBodyCollisionFilter::IsValid()`를 더했다. `FBodySetup::GetVolume`은 UE 5.1+ 이름 `GetScaledVolume`이고, `BuildScale3D`는 곱하지 않는 메타데이터다 | 같은 이름이면 `PtBackendAbi.h`를 먼저 include한 TU에서 `inline constexpr uint32 32u = 32;`가 되어 include 순서에 따라 컴파일이 깨진다. UE는 BuildScale3D를 AggGeom에 미리 굽는다 (BodySetup.cpp:756-812 [V]) |
| 22 | 02 §9.1 `Tick` 순서 (`OnPhysScenePreTick` → `SetUpForFrame`) | `SetUpForFrame` → `StartFrame` 안에서 `OnPhysScenePreTick(FStepPlan::FrameDeltaTime)`. `FStepPlan`에 `FrameDeltaTime`(UE5 `MDeltaTime`, UE4 규칙은 UseDelta [M])을 더했다. 02 §9.1, §5.2 #31을 고쳤다 | UE5는 SetUpForFrame(PhysLevel.cpp:210) 다음 `FChaosScene::StartFrame` → `OnStartFrame(MDeltaTime)`에서 PreTick을 clamp된 dt로 broadcast한다 (ChaosScene.cpp:363-374, PhysScene_Chaos.cpp:2272-2316 [V]). Harness driver의 스텝마다 `OnPhysSceneStep`은 그대로다 (UE4 방식) |
| 23 | 프로필 리졸버의 하네스 CVar | 프로세스에 등록된 모든 CVar가 아니라 고정 표 `PhysicsCVars::GetHarnessConsoleVariables()`만 하네스 CVar다 (새 파일 `Physics/PhysicsConsoleVariables.h`). Sensitive sleep 배수도 상수에서 CVar `p.SensitiveSleepThresholdMultiplier`로 바꿨다 | 정적 라이브러리의 CVar 정적 객체는 그 TU를 참조하지 않는 프로그램에는 링크되지 않는다. 그러면 하네스 CVar가 엔진 CVar로 취급되어 변형 금지와 값 검증이 빠지고 `physics_sha256`이 프로그램마다 달라진다 |
| 24 | 02 §4.1 하네스 플래그 `/W4` | `/W4 /WX` | 경고 0인 지금 고정해 두면 하네스 경고가 로그에만 남고 지나가는 일이 없다 (M0 경미 지적의 후속) |

### 4. 새로 확인한 사실 [V]

- **`/fp:fast` 상수 접기는 dbg에서도 일어난다.** `static_cast<double>(constexpr float 1.0f / 60.0f)`가 dbg(/Od), dev, test 모두 0.016666666666666666이다. 최적화가 아니라 프런트엔드의 상수 평가에서 생긴다. `volatile float`를 거친 `EngineDeltaTime`은 세 프리셋 모두 0.01666666753590107이다.
- **Chaos는 최대 각속도 0을 "제한 없음"으로 보지 않는다.** `AngularSpeedSq > MaxAngularSpeedSq()`이면 `W *= sqrt(Max / AngularSpeedSq)`라서, 0이면 각속도가 0이 된다 (PBDRigidsEvolutionGBF.cpp:950-954). 해제는 큰 값으로 한다.
- **Chaos 셰이프 체적은 float π를 쓴다.** `TSphere::GetVolume`은 `4. / 3 * UE_PI`(UE_PI는 float, Sphere.h:213-217), `FCapsule::GetVolume`은 `UE_PI`와 `4.0f / 3.0f`(Capsule.h:280)다. 반지름 50 cm 구는 정확한 π 기준과 상대 2.8e-8 다르다. UE 요소의 `GetScaledVolume`은 또 다른 상수 `1.3333f`를 쓴다 (SphereElem.h:80, SphylElem.h:102). 이 값은 질량에 쓰이지 않는다.
- **UE의 대각화는 OffDiag²/Trace² < 1e-8이면 대각으로 본다** (MassProperties.cpp:31-38). 즉 대각 원소 평균의 1e-4배까지의 비대각 성분은 버려진다.
- **UE `ApplyMassPropertiesModifiers`는 질량 비율을 float로 계산한다** (BodyUtils.cpp:50-75). `MassScale` 하한 0.001 kg도 float다.
- **UE5 substep: dt 0.2에서 float `6 × float(1/60)`이 정확한 반올림 경계다** (0.1000000052154064178466796875). 짝수 쪽 0.100000009가 되어 ceil 입력이 6.0000002이고, 7 > 6이라 MaxSubSteps 6 × MaxDt로 떨어진다. 0.1 s는 소실된다.
- **UE4 규칙은 dt 0.2에서도 2 스텝이다.** 먼저 `UseDelta = min(dt, MaxPhysicsDeltaTime) = 1/30` [M]이기 때문이다.
- **AsyncFixed 교대는 정확하다.** (double)float(1/60)의 두 배가 (double)float(1/30)과 비트가 같다 (2배는 지수만 바뀐다).
- **`main(int, char**)`는 한글 인자를 cp949로 받는다** ("한글" → `C7 D1 B1 DB`). `wmain` + `WideCharToMultiByte(CP_UTF8)`이면 `ED 95 9C EA B8 80`이다.
- **Bullet 헤더는 `BT_THREADSAFE`를 `#if`로만 검사한다** (btThreads.h:72-95, `#ifdef` 없음). 그래서 `BT_THREADSAFE=0`을 내보내도 Bullet 자신의 의미는 그대로다.
- **doctest v2.5.3은 `-tc=` 필터에 맞는 케이스가 없어도 종료 코드 0이다.** 그래서 영역별 CTest는 요약 줄을 요구한다.
- **UE `-ini:` 문법** (ConfigCacheIni.cpp:2413-2500): `FParse::Token`으로 토큰을 나누고, `-ini:<이름>:` 뒤를 `,`로 나눈 뒤 각 쌍을 첫 `=`에서 가르고 마지막 `]:`에서 섹션과 키를 가른다. 값이 따옴표로 둘러싸였으면 벗긴다. `FParse::Token`은 따옴표로 시작한 토큰의 따옴표를 벗기고, 중간의 따옴표 부분은 그대로 둔다 (Parse.cpp:846-930).
- **UE BaseEngine.ini의 `[/Script/Engine.PhysicsSettings]`에는 `DefaultGravityZ`와 `bEnable2DPhysics` 두 키뿐이다** (BaseEngine.ini:3406-3408). 나머지는 모두 코드 기본값이다. 그래서 `BasePhysicsTest.ini`는 코드 기본값을 명시적으로 적고, `BaseMatchesBuiltIn` 테스트가 둘을 맞춘다.
- **정적 라이브러리 TU의 CVar 정적 객체는 참조하는 프로그램에만 생긴다** (수정 라운드). 프로필만 쓰는 `PtProbeHarnessCVars`의 링크 맵에는 `PtPhysicsCore`의 오브젝트가 `PhysicsConsoleVariables`, `PhysicsProfile`, `PhysicsSettings` 셋뿐이다. 그래서 하네스 물리 CVar를 리졸버가 참조하는 한 TU에 모았다.
- **UE5의 PreTick은 SetUpForFrame 다음, clamp된 dt로 온다.** `FChaosScene::SetUpForFrame`이 `MDeltaTime = min(dt, MaxPhysicsDeltaTime)`(substep이면 `min(dt, MaxSubsteps × MaxSubstepDeltaTime)`)을 정하고, `StartFrame` → `FPhysScene_Chaos::OnStartFrame(MDeltaTime)`이 PreTick과 Step을 한 번씩 broadcast한다 (ChaosScene.cpp:331-374, PhysScene_Chaos.cpp:2272-2316). PostTick은 dt 없이 EndFrame에서 온다 (ChaosScene.cpp:576).
- **UE `BuildScale3D`는 기록일 뿐이다.** `RescaleSimpleCollision`이 요소 치수에 스케일을 곱해 넣고 `BuildScale3D = BuildScale`로 적는다 (BodySetup.cpp:756-812). `GetScaledVolume`은 인자 스케일만 쓴다 (:2417-2425).
- **UE 밀도는 float다.** `GetBodyInstanceDensity`가 `0.001f × Density`를 float로 돌려준다 (BodyUtils.cpp:30-45). 1 g/cm³이면 0.0010000000475 kg/cm³다.
- **Chaos `MMaxAngularSpeedSq`는 `FRealSingle`이다** (ParticleDirtyFlags.h:579, :646). 3.4e+38 deg/s를 rad/s로 바꿔 제곱하면 float +inf다.
- **UE `FConfigFile`은 `TMap<FString, FConfigSection>`이다** (ConfigCacheIni.h:502, :574). 섹션 이름은 대소문자를 무시한다.

### 5. 후속 작업

| 대상 | 내용 | 시점 |
|---|---|---|
| H (M2) | 스텁을 `FPhysicsBackendScene`(C ABI)으로 바꾼다. `FBodyInstance` 명령 함수와 `FPhysicsInterface`, Native driver(프레임마다 `FSetUpForFrameParams` 한 번), FK*Elem → Pt 마샬링(박스 반폭은 한 곳). A10에서 `NumStepsTaken`을 `NumSteps`와 `NumExternalSteps` 중 무엇과 대조할지 정한다 (Chaos가 MinDeltaTime 이하 스텝을 어떻게 세는지에 따라) | M2 |
| H (M2, 수정 라운드에서 넘김) | ABI 헤더가 보이는 곳에 `static_assert(NumCollisionChannels == PT_NUM_COLLISION_CHANNELS)`. 마샬링이 `ObjectType >= NumCollisionChannels`인 필터를 로드 오류로 거부한다 (`SpawnBody`의 `checkf`는 test 프리셋에서 빠진다. 그때도 마스크는 0이라 다른 채널로 접히지 않는다). 마샬링은 `BuildScale3D`를 곱하지 않는다. Native driver에서 `OnPhysSceneStep`을 UE5처럼 프레임당 한 번 부를지 정한다. PreTick 안에서 낸 명령이 그 프레임 스텝에 들어가는지 테스트한다. effective 설정 API(§3 #19) | M2 |
| H (M3) | 시나리오가 씬 고정층을 쓰고, `desc_sha256`/`options_sha256`(A.9)을 만든다. 벤치 exe도 `wmain` + `FUtf8ArgV`로 들어오고 설정 디렉터리 탐색 규칙을 정한다. 콘솔 출력의 UTF-8(`SetConsoleOutputCP`) 여부도 정한다 | M3 |
| H (M5) | `Config/Profiles/Backend/*.ini` 변형 파일을 이 리졸버로 읽고, 백엔드가 `SceneOptions`/`InitOptions`의 모르는 키를 거부한다 (02 §10) | M5 |
| H (M8) | `Tools/ptbench/analysis/analytic.py`가 `Analytic::EngineDeltaTime`과 같은 float 반올림을 쓴다 (numpy float32) | M8 |
| H (M9) | `ForcedOverrides`(Normalized의 머티리얼 combine Average 강제, SF := F), preflight diff, 프로필 완성 | M9 |
| H (M15) | convex quickhull과 질량 적분 | M15 |
| U | Q10에 덧붙임: UE 4.27 `FPhysScene_PhysX`의 MinPhysicsDeltaTime 처리(서브스텝 dt가 MinDeltaTime 이하일 때 건너뛰는지)를 확인한다. 지금은 UE5 솔버 루프와 같게 적용했다 [R] | M11 전 |
| H (남은 M0 경미 지적) | 이번에 처리한 (a)~(e)와 하네스 경고 회귀(`/WX`, §3 #24) 외에는 그대로다: 핀 가드의 캐시 사용성(`PT_<SDK>_GIT_TAG` 빈 기본값, README의 `--fresh` 안내), VS 생성기 경로(FATAL 또는 경고), `BuildFlags.json` 범위 문구("compile database N TU 중 M개 빌드"), `PT::PhysX` 범위 축소(M5), 고지의 공동 저작권자, 이름 정리(`RequestExitFatal`, `UE_LOG` Fatal의 noreturn, 스모크의 `median_step_us`, `pt_add_sdk_smoke`의 접미사 인자), RefsClean 프로브가 `file://` `PT_*_GIT`도 검사, `PT_DEPS_CACHE` 예시, PhysX의 "x86" 출력 문구 메모 | 해당 마일스톤 |
| 리드 | 커밋, `m1` 태그 | 지금 |

### 6. 수정 라운드 0 (독립 검사 반영, 2026-10-21)

실패한 완료 기준은 없었다. 지적 15건(주요 1, 경미 14)을 모두 처리했다. 세 건(채널 범위의 ABI 검증, Native driver의 Step 횟수, effective 설정 API)은 M2 몫을 §5에 넘겼다. 고친 뒤 세 프리셋을 빈 `out/`에서 다시 빌드했고(경고 0, `/WX`) CTest 21/21, doctest 73 케이스가 통과한다 [V].

| 지적 (심각도) | 고친 것 | 확인 [V] |
|---|---|---|
| C++ 상수 `PT_NUM_COLLISION_CHANNELS`가 C ABI 매크로와 이름이 같다 (주요) | `NumCollisionChannels`로 바꿨다. 주석에 이유와 M2의 `static_assert`를 적었다. 02 §5.1, 04 DL27, A.0.3 | 세 프리셋 빌드. `PhysicsTest.Types.EnumValuesCrossTheAbi`, `CollisionFilterTruthTable32` |
| 하네스 CVar 등록이 링커에 달렸다 / `physics_sha256`이 등록된 CVar에 달렸다 (경미 2건) | 하네스 물리 CVar를 `Physics/PhysicsConsoleVariables.cpp` 한 TU의 고정 표로 모았다. 리졸버는 내장 기본값 층, 값 검증·정규화, 변형 금지, 해시를 이 표로만 정한다. 다른 등록 CVar(`pt.Test.*`, 뷰어)는 리졸버에게 엔진 CVar이고 `ApplyConsoleVariables`만 등록돼 있으면 설정한다 (02 §10 M1 메모) | 새 CTest `PhysicsTest.Probe.HarnessCVars`: BodyUtils를 참조하지 않는 `PtProbeHarnessCVars`의 링크 맵에 `BodyUtils`/`BodyInstance`/`PhysScene` 오브젝트가 없고(있으면 실패), 두 CVar가 등록되어 있고, 변형의 `p.ComNudgeAffectsInertia=0` → "pinned key", `-ExecCmds="p.ComNudgeAffectsInertia 7x"`·`"p.SensitiveSleepThresholdMultiplier nan"` → "does not parse", UEParity·Normalized의 `physics_sha256`이 `PhysicsTestAutomation`(테스트 CVar 4개를 등록한 프로그램)과 같다 (세 프리셋 같음: UEParity `a890604523f0028b…`, Normalized `47d96f0deed67e0d…`). doctest `PhysicsTest.Profile.HarnessCVarsAreAFixedTable`: 임시 CVar를 등록해도 값·해시가 같고, 변형이 그 CVar를 쓸 수 있고 해시에 안 들어가며, Apply는 그것을 설정한다 |
| `docs/Progress.md` 숫자 (경미) | §2 리드 요구 행을 수정 라운드 뒤 빈 `out/` 빌드의 숫자로 바꿨다 (configure와 빌드 시간을 나눠 적음) | 위 §2 |
| `FBodyCollisionFilter` 채널 32~255가 `ObjectType mod 32`로 접힌다 (경미, M2 제안) | `IsValid()`를 더했고 무효 채널의 마스크는 0이다. `SpawnBody`가 `checkf`로 거부한다. ABI 마샬링 검증은 M2 (§5) | `CollisionFilterTruthTable32`: 32, 33, 63, 255는 무효, 마스크 0, 전부 차단하는 채널 0과도 충돌 없음 |
| 하네스 TU에 `/WX`가 없다 (경미) | `pt_target_defaults()`에 `/WX`. 02 §4.1, README | 세 프리셋 빈 트리 빌드가 `/WX`로 경고 0 |
| `BuildScale3D`를 곱한다 (경미) | UE처럼 메타데이터로 둔다: BodyUtils와 `GetScaledVolume`에서 뺐다. `BodySetup.h` 주석, 02 §5.2 #25, §5.3, §7.1 | `PhysicsTest.BodyUtils.ComNudgeCVarAndScale`: BuildScale3D (1, 1, 3)이어도 1000^0.75 kg, `GetScaledVolume`은 인자 스케일만 반영 |
| PreTick이 SetUpForFrame 전에 원래 dt로 온다 (경미, M2 제안) | 지금 고쳤다: `StartFrame` 안(phase SetUp)에서 `FStepPlan::FrameDeltaTime`(UE5 `MDeltaTime`)으로 broadcast한다. PostTick도 같은 dt. Harness driver의 스텝별 Step은 그대로. 02 §9.1, §5.2 #31, §3 #22 | `PhysicsTest.PhysScene.FrameStateMachineAndDelegates`: FixedStep에서 `Pre 0.016667 idle`(phase SetUp) → `Step` → `Post 0.016667`, Sync dt 0.05 → PreTick 1/30, 0.01 → 0.01. `PhysicsTest.StepPlanner.*`가 모드별 `FrameDeltaTime`을 고정 |
| `State.BodyTM`에 스케일이 들어간다 (경미) | 회전 + 이동만 넣는다. 스케일은 `Scale3D`. 02 §5.1, §5.2 #26 | `PhysicsTest.PhysScene.MassUsesSceneDefaults`: 스케일 3 스폰에서 BodyTM 스케일 1, 이동·회전 그대로, `Scale3D` = 3 |
| Normalized `MaxAngularVelocity=3.4e+38`의 제곱이 float inf (경미) | `1e7` deg/s(약 1.75e5 rad/s). ini 주석과 02 §10에 이유 | `PhysicsTest.Profile.ProfilesApplyToTemplates`: 1e7, rad/s 제곱이 float로 유한 |
| BodyUtils 문서 공백 3건 (경미) | (1) 기여 요소 없음: Chaos 대체값을 유지하고 Error 로그, 02 §5.2 #22·§5.3에 UE는 질량 계산을 건너뛴다고 적었다. (2) UE 밀도는 float: 같은 곳에 의도한 차이로 적었다 (4·5단계가 이미 double이라 1단계만 float로 맞춰도 비트 일치는 안 된다). (3) 물 정육면체 경로에 Chaos와의 차이를 주석으로 | 문서 검토. `NoContributingShape`가 Error 로그와 523 kg |
| `ApplyConsoleVariables`가 Set 결과를 버린다 / 프로필 float가 `inf`·`nan`을 받는다 (경미) | `ApplyConsoleVariables(Config, OutEngineVariables, OutError)`가 거부된 CVar마다 이유(읽기 전용, 더 높은 SetBy, 못 읽음)와 현재 값을 돌려주고 false. `ImportText<float/double>`, `ConsoleVariablePrivate::ParseFloat`, `FParse` float/double, `FConfigFile::GetDouble`이 비유한 값을 거부한다. 02 §5.2 #29·#30 | `ExecCmdsSetConsoleVariables`: `-ExecCmds` 뒤 ini 값 적용은 "already set by Console" 오류, 기본값에서는 성공. `LoadErrors`: `DefaultGravityZ=nan`, `MaxAngularVelocity=inf`, `FixedDeltaTime=1e39` → "expected a finite number". `ParseNumericRangeAndSign`, `IniSyntaxAndArrayOps`, `ConsoleVariableTypesAndPriority`의 inf/nan 사례 |
| 설계 차이가 기록되지 않았다 (경미) | §3 #19~#24, A.0.3 요약에 effective 설정 API(M2/M3), `PhysicsSha256`, `GetScaledVolume`, `NumCollisionChannels`, `FConfigCacheIni` 생략, 섹션 대소문자, `FrameDeltaTime`, PreTick 순서를 적었다. 섹션 이름은 UE처럼 대소문자를 무시하게 고쳤다 | `PhysicsTest.Profile.SectionNamesAreCaseInsensitive`: 소문자 섹션의 프로필·변형(`[physicstest.backend.physx]`, `.INIT`)이 정규 키로 풀리고, 대문자 PhysicsCore 섹션도 DL44 오류 |
| 생성 검사가 `IsSimulating()`이라 SetUp phase를 놓친다 (경미) | 생성, `DeferAddActor`, flush, prepare가 phase Idle을 `checkf`로 요구한다. 02 §6 | 세 프리셋 단위 테스트 (PreTick이 phase SetUp에서 불림을 확인). `checkf` 실패 경로는 프로세스를 끝내므로 단위 테스트로는 돌리지 않았다 |
| 작은 항목 5건 (경미) | (1) `FConfigCacheIni` 삭제. (2) `InstanceBodyIndex`는 UE 의미대로 `INDEX_NONE`, 생성 순서는 `ActorHandle.Index`. (3) `p.SensitiveSleepThresholdMultiplier` CVar. (4) 인용을 `Chaos/Particle/ObjectState.h`로. (5) `NumAwakeBodies`는 Dynamic 상태만 센다 (kinematic 제외) | `StablePointersAndDeferredBatch`(`INDEX_NONE`), `PhysicsTest.Types.BodyInstanceDefaultsMatchUE`(CVar로 배수 0.25 → 복원), `InitialStatesAndStartAsleep`(깨어 있는 수 1 또는 2) |
