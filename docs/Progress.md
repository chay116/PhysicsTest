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
