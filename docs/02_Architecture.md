# 02. 아키텍처 (Architecture)

> **작성일:** 2026-10-21
> **상태:** 설계 초안 v1.0 (구현 전)
> **관련 문서:** [01_Feasibility.md](01_Feasibility.md) (타당성 검토) · [03_WorkPlan.md](03_WorkPlan.md) (작업 계획) · [04_DecisionLog.md](04_DecisionLog.md) (결정 로그) · [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md) (헤더 스케치)
> **대상:** `D:\PhysicsTest` (github.com/chay116/PhysicsTest). Windows x64, MSVC 14.44, CMake 4.1.2 + Ninja
> **기준 소스:** UE 5.8.3 (`D:\UnrealEngine`) · PhysX 5.11.0 (`da950a35`) · Bullet 3.25 (`2c204c49`) · Jolt 5.6.0 (`e77f1755`) (`D:\PhysicsRefs`)

이 문서는 PhysicsTest 하네스의 최종 아키텍처를 정한다.
- **골격:** UE 충실 안을 바탕으로 한다. UE 명명, 단일 `FPhysScene`, UE4 방식 PhysX 백엔드, UE 프레임 모델이 여기에 해당한다.
- **이식한 부분:** 측정 프로토콜(bench-rigor 안)과 경계·Kit 설계(chaos-seam 안).
- **수정:** 적대적 검토 두 건(사실 검증, 빌드 가능성)이 지적한 오류와 누락을 모두 반영했다. 로컬 소스로 다시 확인한 항목은 [V]로 표시했다.
- 결정의 근거와 대안은 [04_DecisionLog.md](04_DecisionLog.md), 일정과 공수는 [03_WorkPlan.md](03_WorkPlan.md), 선언 수준의 코드는 [appendix/A_HeaderSketches.md](appendix/A_HeaderSketches.md)에 있다.

**증거 태그**

| 태그 | 의미 |
|---|---|
| [V] | 로컬 소스를 읽었거나 직접 실행해 검증함 |
| [M] | UE 4.18 서드파티 미러에서 온 내용. 미확인이며 절대 복사하지 않는다. 사용자가 UE 4.27로 확인할 대상 |
| [R] | 기억이나 추정에 의존함. 게이트나 사용자 확인으로 검증할 대상 |

**단계 태그**

| 태그 | 의미 |
|---|---|
| [MVP] | M0~M6에서 만든다. 게이트와 ABI 핵심 v1.0 동결(M6)까지가 범위다 |
| [확장 Mx] | M7 이후 마일스톤 Mx에서 만든다. "선택 사항"이라는 뜻이 아니다. 헤드라인 측정에 필요한 M7, M8, M10, M11도 여기에 속한다. 순수한 선택 항목은 따로 "선택"이라고 적는다 |

**표기 규칙**
- 씬 ID는 항상 `ID.Name` 형태로 적는다 (예: `M3.BoxPile`).
  - methodology 카탈로그의 ID(`M1`~`M6`, `E1`~`E13`)를 그대로 쓴다.
  - 하네스가 추가한 ID(`S00.FreeFall`, `S00d`, `S01`~`S08`, `P0.EmptyStep`, `P1.SmallScene`, `M7.UEDefaults`)는 카탈로그 확장이며 `docs/Scenes.md`에 등록한다.
  - 씬 ID는 마일스톤 M0~M15, 고정 결정 D1~D7과 관계없다.
- 게이트 접두어는 네 종류다: A(ABI), G(물리·측정), SP(Chaos 스파이크), DET(결정성).
- D1~D7은 고정 결정이다. 이 문서는 D1의 Bullet 캡슐 축 한 가지만 보정한다 (§8.1, 결정 로그 참고).

---

## 0. 한눈에 보기

- **질문:** 같은 씬, 같은 dt, 같은 스레드 수에서 Chaos가 Bullet·PhysX·Jolt보다 느린가? 그리고 같은 입력에서 결과가 어떻게 다른가? Chaos는 사용자가 나중에 구현한다.
- **구조:** UE 이름과 기본값을 그대로 쓰는 단일 `FPhysScene`이 `FBodyInstance`, `FBodySetup`, `FPhysicalMaterial`, `FConstraintInstance`를 직접 소유한다. Actor/Component와 렌더/물리 씬 분리는 없다.
- **경계:** 모든 엔진을 pure-C 함수 테이블 `PtBackendApi` 하나로 구동한다. SDK 백엔드는 static lib이고, Chaos는 UBT Program DLL이다. 헤더 전용 Kit가 스레드 규칙, phase 순서, 입력 검증을 모든 엔진에 똑같이 강제한다.
- **입력 소유:** 질량·관성·COM, dt 시퀀스, 중력, 프로필 값은 하네스가 계산해 넘긴다. 백엔드는 매핑만 하고, 실제 적용값은 readback으로 검증한다.
- **헤드라인:** `T_frame = ΣT_push + ΣT_step + T_pull`을 T=1, fresh process, 5회 이상 반복의 중앙값으로 보고한다. Chaos의 `EndFrame`에 들어 있는 동기화 비용을 다른 엔진도 `T_pull`로 똑같이 치른다.
- **프로필:** UEParity(각 엔진을 UE 기본값에 맞춤)와 Normalized(damping 0, clamp 해제, combine 통일, sleep 끔) 두 가지를 둔다. 빌드 플래그 프로필은 Shipped와 MatchedAVX2다.
- **뷰어:** Chaos Debug Draw와 같은 색·테셀레이션·그리는 순서를 쓰는 Win32 + D3D11 + ImGui 뷰어다. 첫 엔진(Jolt) 슬라이스 바로 다음인 M4에 나온다.
- **단계:** MVP(M0~M6)는 3엔진, 뷰어 1단계, 물리 게이트, ABI 핵심 v1.0 동결이다. 체크포인트 A(누적 약 31 pd)에서 3엔진을 뷰어로 보고 `M3.BoxPile` 1k/5k T=1 비공식 수치를 얻는다.
- **Chaos:** 사용자가 M13에서 동결된 ABI 위에 구현한다. 하네스는 UBT 골격, Null 템플릿, 적합성 게이트, ChaosBackendGuide, 선택 스파이크(M2.5)를 제공한다.

**MVP와 확장의 경계**

| 영역 | [MVP] (M0~M6) | [확장] (마일스톤) |
|---|---|---|
| 빌드 | 프리셋 dbg / dev / test, Shipped 플래그, `BuildFlags.json`, SDK 핀 고정, 환경 준비 (M0) | test-avx2와 MatchedAVX2 (M7). prof, test-`<engine>` (M10). Chaos `.rsp` 기반 G0 (M13) |
| 데이터 모델 | `FBodyInstance` 등 UE 타입, `BodyUtils` (프리미티브), `FStepPlanner` 전 모드, 최소 프로필 (M1) | 프로필 완성: 나머지 변형, ForcedOverrides, preflight diff (M9). convex quickhull과 convex 질량 (M15) |
| ABI | v0.9 (M2) → 핵심 v1.0 동결 (M6). Kit, Null (static + DLL), A0~A7, A9, A10 | 조인트·스냅샷·네이티브 드로 항목 확정 (M9). `PtAbiCodec` loopback, A8, OOP 전송 (M14) |
| 백엔드 | Jolt (M3), Bullet과 PhysX 4변형 (M5) | ball/hinge 조인트 3엔진 (M9). BulletMt, PhysX 3.4, double 빌드 (M15) |
| 뷰어 | 1단계: 단일 씬, 엔진 전환, 일시정지/스텝, Stats (M4) | 2단계: SideBySide, GhostOverlay, Replay, Profiler, WARP 골든 PNG (M12) |
| 게이트 | G1~G7, G9, G12, 게이트 씬, A10 프레임 모델 게이트 (M6) | G0 교차검증, G8(DET1), G10a, G11 (M7). G10b cpu_eff (M10) |
| 벤치 | 최소 벤치 exe, `timings.csv`, 기본 manifest (M3), QPC 클럭 | 호스트 제어, recorder, preflight, 성능 씬, T=1..6 (M7). TSC 클럭, FThreadCensus, 적합성 스탬프 (M10) |
| 분석 | 없음 | ptbench 오케스트레이션, CV 보고 (M8). 거동 분석 (M9). CV 게이트·sentinel, bootstrap CI, 주장 규칙, comparability key (M10). 캠페인 (M11) |
| Chaos | UBT 골격과 스파이크 (M2.5, 선택), ChaosBackendGuide 초안 (M6) | Chaos 백엔드 (M13, 사용자). OOP 대안 (M14, 조건부) |

**[01_Feasibility.md](01_Feasibility.md)와의 대응**
- 01 문서의 마일스톤 표기(스파이크 M2.5, 3엔진 통합 씬 M3/M5)와 Q4(질량 규칙을 별도 축으로 분리)는 이 설계에 맞춰 갱신했다.
- 01 문서 §2.3 표의 값(Bullet 박스 마진 0.04 m, Bullet 캡슐 축 Y)은 엔진 기본값이다. 하네스가 실제로 쓰는 값은 §8.1과 §8.4에 있다 (Bullet 마진은 0.04 m가 상한이고 셰이프마다 다르며, 캡슐은 `btCapsuleShapeZ`).

---

## 1. 목표와 범위

**답할 질문은 두 가지다.**
1. **속도:** 같은 씬, 같은 dt, 같은 스레드 수에서 Chaos가 Bullet·PhysX·Jolt보다 느린가?
   - 비교 대상은 게임 경로, 즉 `FChaosScene` + `FChaosEngineInterface`다 (D2).
   - 느리다면 그 원인이 고정 통합 비용인지, 바디당 비용인지, 솔버인지까지 분리한다 (§9.7).
2. **결과:** 같은 입력에서 시뮬레이션 결과가 어떻게 다른가?
   - 각 차이에 by-design / mismatch / algorithmic 중 하나의 태그를 붙인다.

**Phase 1 (지금 수행)**
- 엔진은 세 가지다.
  - Bullet 3.25: 단일 스레드 월드 [MVP M5]
  - PhysX 5.11: CPU, static 링크 [MVP M5]. 주 행은 `UE4Compat`이며 결과에 "UE4 settings on PhysX 5.11"로 표기한다. 변형으로 `SdkDefault`, `UE4Compat-TGS`, `UE4Compat-Stab`을 둔다.
  - Jolt 5.6.0 [MVP M3]
- 셰이프: Sphere, Box, Sphyl [MVP]. Convex는 데이터 모델과 ABI에만 둔다. 하네스 quickhull과 convex 질량 적분은 [확장 M15]이며, 그 전에는 convex 바디를 만드는 경로를 시험하지 않는다.
- 조인트: ball과 hinge [확장 M9]. ABI 항목은 M6에 넣되 M9까지 잠정이다 (§7.6).
- 모빌리티: static, kinematic, dynamic [MVP].

**Phase 2 (사용자 작업) [확장 M13]**
- 사용자가 Chaos 백엔드를 구현한다.
- 하네스가 제공하는 것:
  - 동결된 C ABI 핵심 v1.0 (M6)과 M9에 확정되는 잠정 항목
  - UBT 골격 (M2.5)
  - 로더
  - 적합성 스위트
  - `docs/ChaosBackendGuide.md` (초안 M6)

**범위 밖 (M15 백로그)**
- trimesh/heightfield
- scene query 벤치
- soft limit과 drive
- CCD 연구 (`E1`, `E9`, `E11`)
- BulletMt 백엔드
- PhysX 3.4 백엔드
- double 빌드
- Mode C iso-quality sweep (01 문서 §6의 "같은 품질 목표" 비교)
- convex·trimesh 패리티 정책 (sharp vs rounded)

**산출물**

| 산출물 | 내용 | 단계 |
|---|---|---|
| `PhysicsTestBench.exe` | 헤드리스. 1 run = 1 process. 렌더러를 링크하지 않고 D3D 장치를 만들지 않는다 | 최소판 [MVP M3], 전체 [확장 M7] |
| `PhysicsTestViewer.exe` | Chaos Debug Draw 스타일 뷰어. 1단계는 단일 씬과 엔진 전환, 2단계는 lockstep 비교와 리플레이 | 1단계 [MVP M4], 2단계 [확장 M12] |
| `PhysicsTestAutomation.exe` | doctest 기반 단위 테스트와 ABI 게이트 | 골격 [MVP M0], 단위 테스트 [MVP M1부터] |
| `Tools/ptbench` | 실행 오케스트레이션과 분석 (Python) | [확장 M8] |
| `Unreal/ChaosBridgeHost` | Chaos 브리지 골격 (UBT 전용) | 골격 [MVP M2.5, 선택], 본체 [확장 M13] |

## 2. 설계 원칙

| # | 원칙 | 강제 지점 | 단계 |
|---|---|---|---|
| R1 | 수치에 영향을 주는 모든 것은 UE 타입·필드·기본값을 그대로 쓴다. UObject, Actor/Component, GT/PT 분리, 렌더/물리 씬 분리 때문에만 존재하는 요소는 제거한다. 의도적 차이는 §5.2 표에 모두 적는다 | §5 | MVP |
| R2 | UE 단위(cm, kg, s, rad, Z-up, double)를 하네스와 ABI 전체에서 쓴다. 단위 변환은 백엔드 내부 `PtUnitScale` 한 곳에서만 한다 (D1) | ABI, BackendCommon | MVP |
| R3 | 경계는 하나다. 모든 엔진을 pure-C `PtBackendApi`로만 구동한다. SDK 백엔드는 static lib에서 `PT_GetBackendApi_<Engine>`을, Chaos DLL은 `PT_GetBackendApi`를 export한다 | §7 | MVP |
| R4 | 백엔드는 `PT::Abi`, `PT::BackendCommon`, 자기 SDK만 본다. 하네스 lib는 링크하지 않는다 (UBT Chaos 모듈과 같은 제약). SDK 헤더는 백엔드 Private 밖으로 나가지 않는다 | CMake, CTest | MVP |
| R5 | 단일 호출 스레드: 모든 물리 호출은 `InitGamePhys`를 부른 스레드에서만 한다. Kit가 `PT_E_WRONG_THREAD`로 강제하며 락은 쓰지 않는다 | Kit | MVP |
| R6 | 입력은 하네스가 소유한다: 질량·관성·COM, dt 시퀀스, 프로필 값, 중력. 백엔드는 매핑만 한다. 실제 적용값은 readback으로 검증한다 | §10, G9 | MVP |
| R7 | 측정과 관찰을 분리한다. 타이밍 구간은 문서로 정의한다. 스냅샷, 드로, 해시, I/O는 그 구간 밖에서 한다. 계측 tier를 하나라도 켜면 헤드라인 자격을 잃는다 | §9, G10 | MVP (구간 정의), 확장 M7 (tier 검사) |
| R8 | 모든 결과에 profile, variant, flags, SHA, effective settings, `scene_sha256`/`desc_sha256`을 기록한다. comparability key는 M10에 더한다 | §12 | 기본 MVP M3, 전체 확장 M7·M10 |
| R9 | 적합성 스탬프가 없는 셀은 러너가 실행하지 않는다. 스탬프 키는 §12.4를 따른다. M10 이전의 수치는 모두 비공식이다 | §12, §14 | 확장 M10 |
| R10 | 클린룸 원칙 (D7) | §16 | MVP |

## 3. 디렉터리 구조

최종 목표 구조다. M0에는 뼈대만 만들고, 뒤 마일스톤에 생기는 항목은 괄호에 마일스톤을 적었다.

```
D:\PhysicsTest\
  CMakeLists.txt  CMakePresets.json  CMakeUserPresets.json.example
  .gitignore  .gitattributes  README.md  THIRD_PARTY_NOTICES.md
  cmake/
    PTOptions.cmake   PTToolchain.cmake   PTFlagProfiles.cmake   PTModules.cmake
    PTBuildInfo.h.in  PTBackendRegistry.cpp.in  DumpBuildFlags.cmake  GitInfo.cmake (build-step git SHA -> PTGitInfo.h, M7)
    PTPython.cmake (PT_PYTHON = .venv, jsonschema / ptbench probes, skipped Python tests without them, M8)
    ThirdParty/  Jolt.cmake  PtBullet.cmake  PhysX.cmake  PhysXBootstrap.cmake (VS only)  ImGui.cmake  SmallDeps.cmake
                 PtFetch.cmake (pinned FetchContent helpers + SHA guard, M0)
    Chaos/       PtChaosUbt.cmake
  Config/
    BasePhysicsTest.ini                       (M1: every modelled key with its UE / harness default)
    Profiles/UEParity.ini  Profiles/Normalized.ini   (M1 drafts, complete M9)
    Profiles/Backend/  PhysX.UE4Compat  PhysX.SdkDefault  PhysX.UE4Compat-TGS  PhysX.UE4Compat-Stab
                       Jolt.Default (M3)  Jolt.SharpBoxes  Bullet.Default  Bullet.StaticAabbOff  Chaos.Game  Chaos.Evolution  (.ini)
    Matrices/  smoke determinism (M8)  headline sensitivity-avx2 scaling overhead behavior (later) (.json, schema physicstest.matrix/1)
    Golden/    tolerances.json  scene_hashes.json  native_hashes.json
    Schemas/   manifest.schema.json  exit.schema.json (M7)  session / matrix (M8)  stamp (M10) (.schema.json)
  Source/
    Runtime/
      Core/                Math, LogMacros, AssertionMacros, CommandLine/Parse, ConfigCacheIni, IConsoleManager,
                           PlatformTime (QPC), PlatformAffinity, Fnv/Sha256, FRandomStream,
                           Containers/StringConv (UTF-16 <-> UTF-8, wmain argv, M1),
                           Misc/PtThirdPartyIncludesStart.h / PtThirdPartyIncludesEnd.h (harness side),
                           Math/Color Matrix Sphere (FColor, FMatrix, FSphere for the debug draw, M4)
      PhysicsTestAbi/Public/PhysicsTestAbi/  PtBackendAbi.h (C)  PtBackendKit.h  PtAbiCodec.h (M14)
      PhysicsTestAbi/Private/CompatCheck/    PtAbiUeCompatCheck.cpp (Kit with UE flags)  PtAbiC11Check.c (ABI header as C11)  (M2; own CMakeLists)
      BackendCommon/Public/                  PtTypes.h  PtUnitScale.h  PtContactConvention.h  PtCombine.h  PtDamping.h  (M2)
                                             PtShapeWrapping.h (M3)  PtBuildReport.h  PtThirdPartyIncludes.h  (header-only)
      PhysicsCore/
        Public/PhysScene.h
        Public/PhysicsEngine/    ShapeElem SphereElem BoxElem SphylElem ConvexElem AggregateGeom BodySetup
                                 BodyInstance ConstraintInstance ConstraintUtils PhysicsSettings BodyUtils
        Public/PhysicalMaterials/PhysicalMaterial.h
        Public/Physics/          PhysicsInterfaceTypes PhysicsInterface PhysicsBackend StepPlanner PhysSceneStats
                                 PhysicsProfile PhysicsConsoleVariables (M1) PhysicsDebugSnapshot AnalyticReference (dt_engine, M1)
                                 EffectiveSettings (DumpEffectiveSettings parser + required keys, M2)
        Private/Physics/Matrix33 (3x3 helpers: Jacobi, quat <-> matrix, M1)
      PhysicsDebugDraw/    DebugDrawQueue DebugDrawSettings PhysicsDebugDraw DebugDrawRenderer IslandBuilder WireMeshes (no D3D, M4)
      D3D11DebugRenderer/  D3D11RHI D3D11DebugDrawRenderer SceneView; Private/D3D11RHIPrivate.h Shaders.hlsl.inl
                           StbImageWrite.cpp (M4; PNG capture is FD3D11RHI::CaptureOutputPng)
      PhysicsTestScenarios/    Public/PhysicsTestScenario.h (registry, IMPLEMENT_PHYSICS_TEST_SCENARIO, M3)
                               Public/Fixtures/FreeFallFixture.h (M2 fixture, moved from Conformance in M3)
                               Private/Scenarios/ScenarioHelpers.h  Gates/S00_FreeFall  Behavior/M1_SingleBoxDrop  Perf/M3_BoxPile (M3)
                               Private/Scenarios/Gates/S01..S08 (M6)  Perf/P0_EmptyStep P1_SmallScene M2b_Pyramid1240 (M7)
                               Private/Scenarios/{Overhead,Extended} (later)
      PhysicsTestBenchCore/    Public/BenchCore.h (M3: clock buckets, window statistics, ULID run id, file SHA-256, read-only host
                               facts; M7: processor topology, FHostEnvironment, FHostLoadMonitor, FProcessSpawner, FAllocationCounters)
                               Private/BenchCore.cpp (M3)  HostEnvironment.cpp (M7)  ThreadCensus (M10)
      PhysicsTestConformance/  Public/ConformanceGate.h  Public/Abi/AbiGates.h (A0-A7, A10; M2)
                               Private/Abi/AbiGates.cpp  AbiFrameModelGate.cpp  Gates/G0..G12 (M6)  FConformanceSuite (M6)
      PhysicsTestBenchmark/    Public/BenchmarkRunner.h  Private/BenchConfig.cpp BenchmarkRunner.cpp (M3 minimal, M7 full: FBenchRun)
                               Public/BenchRecorders.h  Private/BenchRecorders.cpp BenchPreflight.* BenchMultiRun.cpp RunManifest.h (M7)
    Backends/
      CMakeLists.txt (backends + the generated PT::BackendRegistry, M2)
      NullBackend/   Private/ NullBackend.h NullBackend.cpp (static entry) NullBackendDll.cpp (-> PtBackend_Null.dll, DLL path)
                     Public/NullBackend/NullBackendEntry.h (M2)
      JoltBackend/   Private/ JoltIncludes.h PhysScene_Jolt.* JoltLayers.h JoltCombine.h JoltContactListener.h
                              JoltTempAllocator.h JoltBackend.cpp (static entry)
                     Public/JoltBackend/JoltBackendEntry.h (M3)
      BulletBackend/ Private/ BulletIncludes.h PhysScene_Bullet.* BulletCombine.* BulletKinematics.*
      PhysXBackend/  Private/ PhysXIncludes.h PhysXSupport.* PhysicsInitialization.cpp PhysXCPUDispatcher.*
                              PhysicsFiltering.* PhysXSimEventCallback.* PhysScene_PhysX.* PhysXBodySetup.cpp
                              PhysXBodyInstance.cpp PhysXConstraint.cpp PhysXSceneConfig.* PhysXCompat.h
    Editor/PhysicsTestEditor/  PhysicsViewportClient ViewportCameraController ViewerSession PhysicsTestEditor (M4)  ReplaySource (M12)
                               Private/Panels/{Engines,Inspector,Stats,Console}Panel.cpp (M4)  (+ Profiler, OutputLog: M12)
    Programs/  PhysicsTestBench/ (M3: Private/PhysicsTestBenchMain.cpp, RunBenchMatrix.cmake; M6: RunConformanceMatrix.cmake;
                                  M7: Private/AllocationHook.cpp, Run{Contaminated,GateMatrix,ManifestSchema,PreflightSweep,ThreadMatrix}.cmake)
               PhysicsTestViewer/ (M4: Private/PhysicsTestViewerApp.*, PhysicsTestViewerMain.cpp; Tests/ -> PhysicsTestViewerTests.exe)
               PhysicsTestAutomation/  PtBackendServer/ (M14, conditional)
    Tests/     SdkSmoke/ (PtSmoke{Jolt,Bullet,PhysX}.exe, M0)  Probes/ (CTest probes: includes, flag leak, BuildFlags,
               refs clean, longest path, pin guard, negative-scale fatal, M0; harness CVars, M1; check modules, Kit
               namespaces, backend isolation, Kit needs PT_BACKEND_NS, M2; harness SDK isolation, bench without renderer, M3;
               automation without renderer, M4)
               AbiTwin/ (second static test backend, PT_BACKEND_NS=AbiTwin: Kit state isolation and guards, M2)
               PtBench/ (CTest PhysicsTest.Py.*: Tools/ptbench unit, analytic vs C++, integration with the bench exe, M8)
  Unreal/ChaosBridgeHost/          UBT only, implemented by the user
    ChaosBridgeHost.uproject       (name differs from the target, UEBuildTarget.cs:1926-1946 [V])
    .gitignore                     Binaries/ Intermediate/ Saved/ DerivedDataCache/ *.generated.h *.gen.cpp *.dll *.pdb
    Source/PhysicsTestChaos.Target.cs  PhysicsTestChaosAvx2.Target.cs  PhysicsTestChaosServer.Target.cs (fallback)
    Source/PhysicsTestChaos/PhysicsTestChaos.Build.cs
    Source/PhysicsTestChaos/Private/  PtChaosModule.cpp PtChaosBackend.* PtChaosScene.* PtChaosBodyFactory.cpp
                                      PtChaosSnapshot.cpp PtChaosDebugDraw.cpp PtChaosEvolutionScene.cpp PtChaosDefaults.h
  Tools/  cmake.cmd ctest.cmd vcenv.cmd (vcvars64)  check_physx_isolation.cmd + CompareBuildFlags.cmake (M0)
          check_modules.py  validate_manifest.py (M7)  Chaos/build_chaos.cmd
          ptbench/ (runner, envcheck, analysis/{load,validate,perf,behavior,determinism,report}, analytic)
          ptbench/ M8: pyproject.toml (package dir = this folder, editable install into .venv)  __main__ (python -I -m ptbench)
                   common matrix runner envcheck analytic  analysis/{load,validate,perf,report,determinism}  tests/{test_unit,test_integration}
          ptbench/requirements.lock  requirements-build.txt (M8)  wheelhouse/ (offline only, ignored)
  .venv/  (ignored, repository root; Tools/ptbench analysis packages)
  docs/   01_Feasibility.md  02_Architecture.md  03_WorkPlan.md  04_DecisionLog.md  appendix/A_HeaderSketches.md
          Abi.md (M2 draft)  Scenes.md (M3)  ChaosBackendGuide.md (M6)  TimingContract.md  ResultsSchema.md (M7)  Progress.md
          (planned) Profiles.md  ChaosSpike.md  Checkpoints.md
          reports/m8_smoke/ (M8: one UNOFFICIAL smoke report copied from Saved/, Balanced power plan)
  out/<preset>/   (ignored, short root)
  Saved/          (ignored) Logs/ Results/<session>/<run_id>/ Conformance/ Screenshots/
```

- 문서 디렉터리는 소문자 `docs/` 하나만 쓴다. Git은 대소문자를 구분하고 NTFS는 구분하지 않으므로, 두 표기가 섞이면 GitHub에 디렉터리가 두 개 생긴다.

## 4. CMake 타깃 그래프와 빌드 정책

### 4.1 툴체인 [MVP M0]

`PTToolchain.cmake`는 ThirdParty 모듈보다 먼저 include한다.

- **가드:** MSVC x64만 허용한다. `RelWithDebInfo`는 FATAL로 막는다 (/Ob1이 붙고 PhysX config와 맞지 않음).
- **CRT:** `CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` CACHE FORCE. SDK를 추가하기 전에 설정하며 모든 타깃이 /MD다 (D3).
- **FP:** `add_compile_options(/fp:fast)` (D5).
- **디버그 정보:** /Zi, Release에서 /DEBUG:FULL /OPT:REF,ICF.
- **LTO:** OFF (UE의 bAllowLTCG=false와 동일).
- **compile_commands:** `CMAKE_EXPORT_COMPILE_COMMANDS` ON. G0의 입력이다 (§4.6).
- **C++ 표준:** 전역 `CMAKE_CXX_STANDARD`는 쓰지 않는다 (PhysX의 /std:c++14 /WX가 C5055로 깨짐 [V]). `pt_target_defaults()`가 하네스 타깃마다 다음을 건다:
  - cxx_std_20
  - /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor (`/WX`는 M1 수정 라운드에서 더했다. 하네스 TU의 경고는 모든 프리셋에서 빌드 실패다)
  - NOMINMAX WIN32_LEAN_AND_MEAN UNICODE
  - PT_DO_CHECK
- **/arch:** 전역으로 걸지 않는다. `pt_apply_arch(<targets>)`가 타깃 목록에만 붙인다.
- **재현 가능 링크:** `/Brepro`는 선택이다. 바뀌지 않은 코드를 다시 링크해도 exe SHA가 유지되므로 스탬프 무효화가 줄어든다 [확장 M10, 선택].

### 4.2 플래그 프로필

| 코드 | Shipped [MVP] | MatchedAVX2 [확장 M7] |
|---|---|---|
| Jolt SDK + `PT::Backend::Jolt` | /arch:AVX2 강제 (Ninja에서는 기본이 SSE2, Jolt.cmake:969-979 [V]). PUBLIC으로 걸리므로 백엔드 lib에서 PRIVATE으로 막는다 | 동일 |
| PhysX, Bullet SDK와 각 어댑터 | 벤더 기본 (SSE2) | `pt_apply_arch`로 /arch:AVX2 (SDK 디렉터리 타깃을 재귀 처리) |
| 하네스 (Core, PhysicsCore, Scenarios 등) | /arch 없음 | /arch 없음. 하네스 입력이 프로필 간 비트 동일해야 하므로 `desc_sha256` 동일성으로 테스트한다 |
| Chaos (UBT) | UBT 기본 (no /arch, SSE4.2 intrinsics, ISPC) + `FPSemantics=Imprecise` | 별도 타깃 `PhysicsTestChaosAvx2` (`MinCpuArchX64=AVX2`), M13 이후 선택 |

- 헤더 전용 Kit와 BackendCommon은 /arch가 다른 백엔드 TU에 함께 컴파일된다. ODR 위반을 막기 위해 백엔드마다 다른 inline namespace를 쓴다 (§7.3).

### 4.3 SDK [MVP M0]

| SDK | 가져오기 | 핵심 설정 | 래퍼 |
|---|---|---|---|
| Jolt v5.6.0 (`e77f1755`) | FetchContent, GIT_TAG는 커밋 SHA, SOURCE_SUBDIR `Build`, configure 후 `git rev-parse` 검사 | `block()` 안에서 static CRT / IPO / ALL_WARNINGS / INSTALL OFF, DX12 / VK / CPU_COMPUTE OFF (소스 트리에 .dxil/.spv 생성 방지), DEBUG_RENDERER와 PROFILER_IN_DEBUG_AND_RELEASE OFF (prof 프리셋에서만 ON, M10), FP_EXCEPTIONS OFF, OBJECT_LAYER_BITS 16 유지 | `PT::Jolt` |
| Bullet 3.25 (`2c204c49`) | `FetchContent_Declare(bullet3 GIT_REPOSITORY ${PT_BULLET_GIT} GIT_TAG <2c204c49 full SHA> GIT_CONFIG core.longpaths=true SOURCE_SUBDIR _none_)` + `FetchContent_MakeAvailable(bullet3)`. `_none_`에는 CMakeLists.txt가 없으므로 받기만 한다. 그다음 `block()` 안에서 `CMAKE_POLICY_VERSION_MINIMUM` 3.10을 설정하고 `add_subdirectory(${bullet3_SOURCE_DIR} <bin> EXCLUDE_FROM_ALL SYSTEM)` | 단일 인자 `FetchContent_Populate`는 CMP0169(3.30)로 금지되므로 쓰지 않는다. 옵션은 전부 CACHE FORCE (Bullet 안에서 CMP0077 OLD), LinearMath / BulletCollision / BulletDynamics만, BT_THREADSAFE=0, /wd4819 | `PT::Bullet` (BT_* define 재수출) |
| PhysX 5.11.0 (`da950a35`, ovphysx-0.6.3) | FetchContent, SOURCE_SUBDIR `physx` (Ninja 전용). PxConfig.h는 `out/<preset>/_deps`에만 쓰인다 | GPU OFF, STATIC ON, NV_USE_STATIC_WINCRT OFF, SNIPPETS / PVDRUNTIME OFF, PRECISE_MATH OFF | `PT::PhysX` INTERFACE = `physx_lib` (physx/CMakeLists.txt:326 [V]) + `PX_PHYSX_STATIC_LIB` + `DISABLE_CUDA_PHYSX`. `physx_lib`는 `PX_PHYSX_STATIC_LIB`를 전달하지 않으므로 래퍼가 직접 정의한다. PvdSDK는 PhysX·PhysXExtensions의 링크 의존성이라 모든 구성에서 링크되며, release에서는 `PX_SUPPORT_PVD=0`으로 코드가 빠진다 (physx/source/compiler/cmake/PhysX.cmake:397-400 [V], M0에서 정정) |
| imgui v1.92.9b-docking, implot v1.0, stb `a7d4c217`, nlohmann/json v3.12.0, doctest v2.5.3, Tracy v0.14.1 (prof만, M10) | URL + URL_HASH SHA256 | `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`는 정의하지 않는다 (implot이 깨짐) | `PT::ImGui` 등 |

- 저장소 URL은 캐시 변수 `PT_<SDK>_GIT`로 둔다. User preset에서 `file:///D:/PhysicsRefs/<repo>`로 바꾸면 오프라인으로 빌드된다.
- `D:\PhysicsRefs` 안에서는 절대 빌드하지 않는다. 항상 빌드 트리로 clone한다.
- 빌드 루트는 `out/<preset>`처럼 짧게 둔다. 이 머신은 LongPathsEnabled=0이라 MAX_PATH에 걸린다 [V].

### 4.4 프리셋

| 프리셋 | BUILD_TYPE | UE 대응 | 플래그 | 용도 | Chaos DLL | 단계 |
|---|---|---|---|---|---|---|
| dbg | Debug | DebugGame | Shipped, /MDd, checks | VS Code cppvsdbg 디버깅 | - | MVP M0 |
| dev | Release | Development | Shipped, checks on | 뷰어, 일상 확인 (헤드라인 아님) | Development | MVP M0 |
| test | Release | Test | Shipped, checks off, RENDER OFF | **헤드라인** | `PhysicsTestChaos-Win64-Test.dll` | MVP M0 |
| test-avx2 | Release | Test | MatchedAVX2 | 민감도 측정 | `PhysicsTestChaosAvx2` Test | 확장 M7 |
| prof | Release | - | Shipped + Jolt profiler + Tracy | 단계 분해 (헤드라인 절대 아님) | Development | 확장 M10 |
| test-jolt, test-physx, test-bullet | Release | Test | 단일 SDK | 바이너리 구성 대조군 | - | 확장 M10 |

"보기는 Development, 측정은 Test" 원칙은 모든 엔진에 똑같이 적용한다. VS 생성기는 벤치 프리셋에서 지원하지 않는다.

### 4.5 타깃 그래프 [MVP]

```
PT::Abi                INTERFACE  PtBackendAbi.h / PtBackendKit.h       (+ PtAbiCodec.h, M14)
PT::BackendCommon      INTERFACE  -> PT::Abi
PT::Core               STATIC
PT::PhysicsCore        STATIC     -> PT::Core, PT::Abi                   (FPhysScene, host proxies)
PT::BenchCore          STATIC     -> PT::Core                            (M3: clock buckets; M7: FHostEnvironment, process spawner;
                                                                          M10: FThreadCensus, TSC)
PT::Backend::Null      STATIC     -> PT::BackendCommon                   (+ PtBackend_Null.dll SHARED, tests)
PT::Backend::Jolt      STATIC     -> PT::BackendCommon; PRIVATE PT::Jolt
PT::Backend::Bullet    STATIC     -> PT::BackendCommon; PRIVATE PT::Bullet
PT::Backend::PhysX     STATIC     -> PT::BackendCommon; PRIVATE PT::PhysX
                                  (each backend: PRIVATE PT_BACKEND_NS=<Engine>)
PT::BackendRegistry    STATIC     generated PTBackendRegistry.gen.cpp {name -> PT_GetBackendApi_X}
                                  + Chaos LoadLibraryExW loader (no UE dependency)
PT::Scenarios          STATIC     -> PT::PhysicsCore                     (IMPLEMENT_PHYSICS_TEST_SCENARIO)
PT::Conformance        STATIC     -> PT::Scenarios, PT::BenchCore; PRIVATE PT::Json   (M6: tolerances.json)
PT::Benchmark          STATIC     -> PT::Conformance, nlohmann_json
PT::PhysicsDebugDraw   STATIC     -> PT::PhysicsCore
PT::D3D11DebugRenderer STATIC     -> PT::PhysicsDebugDraw, imgui, implot, stb, d3d11, dxgi, d3dcompiler
PT::Editor             STATIC     -> PT::D3D11DebugRenderer, PT::Benchmark (readers)
PhysicsTestBench.exe       -> PT::Benchmark, PT::BackendRegistry, WHOLE_ARCHIVE(PT::Scenarios)   (no renderer)
PhysicsTestViewer.exe      -> PT::Editor,    PT::BackendRegistry, WHOLE_ARCHIVE(PT::Scenarios)
PhysicsTestAutomation.exe  -> doctest, PT::Conformance, PT::PhysicsDebugDraw, PT::BackendRegistry, WHOLE_ARCHIVE(...)
PtAbiUeCompatCheck         OBJECT  Kit + trivial backend with /GR- /EHs-c- _HAS_EXCEPTIONS=0 PT_ABI_NO_EXCEPTIONS /W4 /WX
PtAbiC11Check              OBJECT  ABI header as C11 /W4 /WX (M2: a separate C target)
PhysicsTestChaos_UBT_{Development,Test}   custom target (PT_WITH_CHAOS=ON, not in ALL) -> Tools/Chaos/build_chaos.cmd
```

**의존 방향**
- `PT::BenchCore`를 Conformance 아래에 둔다. G8(DET1, fresh process 재실행)과 G10이 쓰는 프로세스 실행기, 호스트 환경, 클럭이 여기에 있다. 그래서 Conformance → BenchCore, Benchmark → Conformance로 순환이 없다.

**백엔드 등록 방식**
- 백엔드는 생성된 레지스트리가 직접 참조한다. 그래서 WHOLE_ARCHIVE가 필요 없다.
- 시나리오는 static registrar로 등록하므로 WHOLE_ARCHIVE로 링크한다. 프로그램 시작 시 레지스트리가 비어 있지 않은지 assert한다.

**CTest로 확인하는 것**
- `PhysicsTestBench.exe`가 d3d11/dxgi를 import하지 않는다.
- 백엔드 타깃의 include 경로에 `Source/Runtime/Core`가 없다.
- 빌드 후 `D:\PhysicsRefs`의 `git status --porcelain`이 비어 있다. 소스 트리에 .dxil, .spv, PxConfig.h가 없다.
- 컴파일 프로브: Core를 먼저 include한 TU가 하네스 측 third-party 헤더 쌍(§8.1)을 거쳐 doctest, ImGui, ImPlot, nlohmann/json을 include해도 컴파일된다. doctest는 `check()`라는 멤버 함수를 선언하기 때문이다.
- `PT_BACKEND_NS`가 정의되지 않은 백엔드 TU는 `#error`로 실패한다.

**M2 구현 메모 [V]**
- 지금 있는 타깃: `PT::Abi`(INTERFACE), `PtAbiC11Check`·`PtAbiUeCompatCheck`(OBJECT, ALL에 포함), `PT::BackendCommon`(→ PT::Abi), `PT::PhysicsCore`(→ PT::Core, PT::Abi; PRIVATE PT::Json), `PT::Backend::Null`(STATIC) + `PtBackend_Null`(SHARED, 같은 소스), `PT::BackendRegistry`(생성, → PT::PhysicsCore + 각 백엔드), `PT::Conformance`(→ PT::PhysicsCore), 시험용 `PtBackendAbiTwin`(STATIC, `PT_BACKEND_NS=AbiTwin`).
- `PT::Conformance → PT::Scenarios, PT::BenchCore`는 두 타깃이 생기는 M3부터다. M2의 Conformance는 PhysicsCore만 본다.
- `FPhysicsBackendRegistry`의 클래스와 로더는 PhysicsCore에 있고, 정적 목록(`GetBackendRegistryEntries`)만 생성된 `PT::BackendRegistry`가 정의한다. FPhysScene을 만드는 프로그램은 PT::BackendRegistry를 링크한다. 프로필만 쓰는 프로그램(`PtProbeHarnessCVars`)은 링커가 PhysScene 오브젝트를 끌어오지 않으므로 그대로 링크된다.
- 하네스 C TU(`PtAbiC11Check.c`)는 `BuildFlags.json`에서 `/std:c11`을 기대한다 (`DumpBuildFlags.cmake`의 `harness_c`).
- `PtAbiUeCompatCheck`가 있는 디렉터리는 `CMAKE_CXX_FLAGS`에서 CMake 기본 `/EHsc`를 뺀다. `/EHs-c-`를 뒤에 붙이기만 하면 D9025 명령줄 경고가 난다 (MSVC 14.44로 재현 [V]). 이 설정은 디렉터리 범위라서 두 검사 타깃만 있는 `PhysicsTestAbi/Private/CompatCheck/`(자체 CMakeLists)에 둔다. 같은 모듈에 나중에 들어올 C++ 타깃(M14 `PtAbiCodec`)이 `/EHsc` 없이 컴파일되지 않게 하기 위해서다 (M2 수정 라운드 1).
- CTest 프로브 4개를 더했다: `PhysicsTest.Probe.CheckModules`(A9), `KitNamespaces`(링커 맵), `BackendIsolation`(백엔드 TU의 include 경로에 Core·PhysicsCore 없음, `PT_BACKEND_NS` 정의), `KitNeedsBackendNs`(위의 `#error`. M2 수정 라운드부터 `compile_commands.json`의 그 TU 컴파일 줄을 스크립트가 직접 실행한다. 빌드 트리를 다시 부르지 않는다).

**M3 구현 메모 [V]**
- 더한 타깃: `PT::Scenarios`(STATIC → PT::PhysicsCore), `PT::BenchCore`(STATIC → PT::Core; PRIVATE PowrProf, Ole32), `PT::Benchmark`(STATIC → PT::Conformance, PT::Scenarios, PT::BenchCore; PRIVATE PT::Json, PT::BuildInfo), `PT::Backend::Jolt`(`PtBackendJolt` STATIC → PRIVATE PT::BackendCommon, PT::Jolt; `PT_BACKEND_NS=Jolt`), `PhysicsTestBench.exe`(→ PT::Core, PT::Benchmark, PT::BackendRegistry, PT::BuildInfo + 시나리오 전체).
- `PT::Conformance`는 이제 PT::PhysicsCore, PT::Abi, PT::Scenarios, PT::BenchCore를 본다. M2의 자유낙하 픽스처(`Fixtures::FFreeFall`)는 `S00.FreeFall`이 쓰므로 Scenarios로 옮겼다. 그래야 Conformance → Scenarios가 순환하지 않는다.
- **WHOLE_ARCHIVE:** CMake의 `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`는 한 라이브러리가 두 feature로 링크되면 거부한다 (Conformance가 Scenarios를 보통으로 링크한다). 그래서 `pt_link_scenarios(<target>)`가 보통 링크에 MSVC `/WHOLEARCHIVE:<lib 파일 이름>`을 더한다. 벤치 exe와 Automation이 이것을 쓰고, 시작할 때 `FScenarioRegistry::CheckNotEmpty()`가 빈 레지스트리를 Fatal로 막는다.
- Jolt의 PUBLIC `/arch:AVX2`와 `JPH_*` 정의는 `PtBackendJolt` TU에만 간다 (PRIVATE 링크). 하네스 TU에 SDK include 경로·정의가 없고 Shipped에서 `/arch`가 없다는 것은 `PhysicsTest.Probe.HarnessSdkIsolation`이, 벤치 exe가 d3d11/dxgi를 import하지 않는다는 것은 `PhysicsTest.Probe.BenchNoRenderer`(dumpbin)가 확인한다. 하네스가 어댑터 오브젝트의 AVX2 COMDAT을 실행하지 않는다는 것(바이너리 수준)은 `PhysicsTest.Probe.AdapterComdats`(Automation·Bench 링커 맵, M3 수정 라운드 0, §7.3)가 확인한다. 벤치 exe도 `/MAP`을 낸다. `KitNamespaces`는 Jolt의 `PT_BACKEND_NS`도 본다.
- `PTBuildInfo.h`에 `PT_BUILD_GIT_SHA`와 `PT_BUILD_GIT_DIRTY`를 더했다 (configure 시점 `git --no-optional-locks`). manifest의 harness 필드가 쓴다.

**M4 구현 메모 [V]**
- 더한 타깃: `PT::PhysicsDebugDraw`(`PtPhysicsDebugDraw` STATIC → PT::PhysicsCore. D3D가 없어 Automation이 모든 프리셋에서 링크한다), `PT::D3D11DebugRenderer`(→ PUBLIC PT::PhysicsDebugDraw, PRIVATE PT::Stb, d3d11, dxgi, d3dcompiler), `PT::Editor`(`PtEditor` → PUBLIC PT::D3D11DebugRenderer, PT::Benchmark, PT::Scenarios, PRIVATE PT::ImGui), `PhysicsTestViewer.exe`(→ PT::Core, PT::Editor, PT::BackendRegistry, PT::BuildInfo, PT::ImGui, PT::Json + 시나리오 전체), `PhysicsTestViewerTests.exe`(doctest, `PhysicsTest.Viewer.*`). 렌더러·Editor·두 exe는 `PT_WITH_VIEWER`(dbg, dev)에서만 만든다. test 프리셋에는 D3D 타깃이 없다.
- 위 그래프의 `PT::D3D11DebugRenderer → imgui, implot`은 링크하지 않았다. 렌더러는 ImGui를 쓰지 않고 `DrawDebugString` 텍스트를 호출자에게 돌려준다. ImGui·ImPlot은 Editor와 viewer exe가 링크한다. 렌더러의 단위 와이어 메시는 렌더러와 무관한 `PhysicsDebugDraw/WireMeshes`로 옮겨 선분 수 시험이 모든 프리셋에서 돈다 ([Progress.md](Progress.md) M4 §3).
- CTest: `PhysicsTest.Unit.DebugDraw`(Automation, 세 프리셋), `PhysicsTest.Unit.Viewer`, `PhysicsTest.Unit.ViewerRenderer`와 `PhysicsTest.Viewer.Headless{Null,Jolt}`(dbg·dev, 창 없이 오프스크린 D3D11. 하드웨어 D3D11 장치가 없으면 렌더러 시험은 실패가 아니라 건너뜀), `PhysicsTest.Probe.AutomationNoRenderer`(Automation이 d3d11·dxgi·d3dcompiler를 import하지 않는다), `PhysicsTest.Bench.Headline{IneligibleViz,EligibleDefault}`. doctest 요약을 보는 `PASS_REGULAR_EXPRESSION`의 `|`는 따옴표 인자 안에서 역슬래시 두 개로 쓴다 (하나면 CMake가 지워 정규식이 alternation이 되고 실패한 요약도 통과한다. M4 수정 라운드 2).
- ODR / 플래그 격리 (DL46): `PhysicsTestViewer.exe`와 `PhysicsTestViewerTests.exe`도 `/arch:AVX2` Jolt 어댑터 객체를 에디터·렌더러·ImGui TU와 함께 링크하므로 `/MAP`을 내고, `PT_WITH_VIEWER` 프리셋(dbg·dev)에서 `PhysicsTest.Probe.AdapterComdats`가 그 map도 본다 (dev는 strict, dbg는 보고만. M4 수정 라운드 2).

### 4.6 빌드 자기보고 (G0) [MVP M0 생성, 교차검증 확장 M7]

두 출처를 쓰되, 각 출처가 볼 수 있는 사실만 맡긴다.

1. **`BuildFlags.json` (SDK 내부를 포함한 전 TU):**
   - 빌드 시점에 `add_custom_command(OUTPUT BuildFlags.json BuildFlagsSha.h DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json)`로 `DumpBuildFlags.cmake`를 실행한다. compile_commands.json은 generate 단계에 생기므로 configure 시점 스크립트로는 만들 수 없다.
   - 하네스·어댑터 TU뿐 아니라 **SDK 자신의 TU** 항목에서 실제 플래그와 정의를 읽는다. 선택된 PhysX CMake config(debug / checked / profile / release)도 기록한다.
   - PhysX의 구성별 PRIVATE 정의(`PX_SUPPORT_PVD` 등)는 어댑터 TU에서 보이지 않는다 (physx/source/compiler/cmake/windows/CMakeLists.txt:109-112, PhysX.cmake:371 `PRIVATE ${PHYSX_COMPILE_DEFS}` [V]). 그래서 TU 자기보고에 넣지 않고 이 출처에서만 읽는다.
   - exe에는 `BuildFlagsSha.h`로 그 SHA를 임베드한다.
2. **TU 자기보고 (`PT_BUILD_REPORT_JSON` → `PtBackendInfo.BuildFlagsJson`):** 어댑터 TU가 직접 관찰할 수 있는 사실만 담는다.
   - 컴파일러·플래그: `__AVX2__`, `_M_FP_FAST`/`_M_FP_PRECISE`, `_MSC_FULL_VER`, `_DLL`
   - Jolt PUBLIC 정의: `JPH_PROFILE_ENABLED`, `JPH_DEBUG_RENDERER`, `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED`, `JPH_USE_AVX2` 등과 `JPH::GetConfigurationString()`. 이 함수는 inline이라 호출한 TU의 설정을 보고한다.
   - PhysX: 공개 헤더의 `PX_PHYSICS_VERSION`과 래퍼 정의 `PX_PHYSX_STATIC_LIB`
   - Bullet: `BT_BULLET_VERSION`, `BT_USE_SSE`, 재수출된 `BT_THREADSAFE`
3. **판정:** 두 출처가 겹치는 사실(ISA, FP 모드, CRT, 컴파일러 버전)이 서로 다르거나 플래그 프로필 기대값과 다르면 분석 단계가 그 run을 거부한다.
4. **Chaos [확장 M13]:** UBT가 만든 `Unreal/ChaosBridgeHost/Intermediate` 아래의 `.rsp` 파일을 `check_modules.py`가 파싱해 BuildFlags 출처로 쓴다.

**M7 구현 메모 [V]**
- **test-avx2 프리셋:** `test`를 상속하고 `PT_FLAG_PROFILE=MatchedAVX2`만 바꾼다 (`CMakePresets.json`, 사용자 예시 `test-avx2-local`). MatchedAVX2는 `PT_ARCH_JOLT`, `PT_ARCH_PHYSX`, `PT_ARCH_BULLET`을 AVX2로 둔다 (`cmake/PTFlagProfiles.cmake`). PhysX는 `physx` 소스 디렉터리의 모든 타깃에, Bullet은 세 라이브러리 타깃에 `/arch:AVX2`를 **PUBLIC**으로 건다. 그래서 그 SDK를 링크하는 어댑터 TU도 같은 ISA로 컴파일된다. 하네스 TU에는 어느 프로필에서도 `/arch`가 없다 (`PhysicsTest.Probe.HarnessSdkIsolation`이 모든 프로필에서 확인한다. 전에는 Shipped만).
- **G0 판정 (preflight):** `DumpBuildFlags.cmake`가 대상별 G0 사실(어댑터 TU의 `/arch`, FP 모드, CRT, 컴파일러 버전)을 `BuildFlagsSha.h`의 `PT_BUILD_FLAGS_G0_JSON`으로 exe에 넣는다. 벤치 preflight가 이 값, 어댑터 자기보고(`PtBackendInfo.BuildFlagsJson`), 플래그 프로필 기대값(Jolt는 두 프로필 모두 AVX2, PhysX·Bullet은 MatchedAVX2에서만 AVX2) 셋을 대조하고, 하나라도 다르면 종료 코드 2다 (`BenchPreflight::RunG0`, CTest `PhysicsTest.Bench.PreflightG0Injected`).
- **git SHA:** `PTGitInfo.h`를 빌드 단계의 custom command(`cmake/GitInfo.cmake`)가 만든다. `.git/HEAD`, `.git/index`, `.git/logs/HEAD`에 의존하고 내용이 바뀔 때만 다시 쓴다. 그래서 manifest `harness.git_sha_source`는 `build`이고, 아무것도 바뀌지 않은 두 번째 빌드는 "no work to do"다. 커밋 뒤 첫 빌드는 이 헤더를 쓰는 TU만 다시 컴파일한다.

### 4.7 환경 준비 [MVP M0]

- **Python:** 3.11.9는 있지만 numpy, pandas 등은 설치되어 있지 않다 (`ModuleNotFoundError: numpy` [V]). M0에 `Tools/ptbench`용 `.venv`(git 무시)를 만들고 버전을 고정한 `requirements.lock`으로 설치한다. 오프라인 설치가 필요하면 같은 lock으로 wheelhouse를 만든다 (사용자 확인 Q17, [04_DecisionLog.md](04_DecisionLog.md) §4). 실행은 `python -I`로 한다.
- **디버거:** 이 PC에는 VS IDE와 VS Code가 없고 VS BuildTools만 있다. CLion 2026.1은 설치되어 있다 [V]. VS Code + C/C++ 확장(cppvsdbg) 설치, VS 2022 IDE 설치, CLion을 Ninja 프리셋 + MSVC 툴체인으로 사용하는 것 중 하나를 고른다 (01 문서 Q7, [04_DecisionLog.md](04_DecisionLog.md) Q7).
- **UE:** M2.5 전에 `D:\UnrealEngine` 5.8.3 소스 빌드가 가능한지(Build.bat, Build.version 5.8.x) 확인하고 디스크 여유 10 GB 이상을 확보한다.

## 5. 핵심 타입 [MVP M1]

### 5.1 UE 5.8.3 대응표

| UE 타입 | 하네스 타입 | 접는 방식 / 비고 |
|---|---|---|
| UWorld 물리 부분 + FPhysScene_Chaos + FChaosScene(호스트 측) + FScene의 디버그 드로 부분 | `FPhysScene` | 유일한 씬. 프레임 메서드는 FChaosScene 시그니처를 따른다. 델리게이트는 UE4 이름 `OnPhysScenePreTick` / `OnPhysSceneStep` / `OnPhysScenePostTick` |
| FBodyInstance + FBodyInstanceCore + FActorCreationParams + UPrimitiveComponent::BodyInstance 소유권 | `FBodyInstance` | 필드와 기본값이 같다. damping 0.01/0, MassInKgOverride 100, 8/2/1 + bOverrideIterationCounts, bInertiaConditioning true, SleepFamily (BodyInstance.cpp:357-418 [V]). 코어 플래그 bSimulatePhysics=false, bOverrideMass=false, bEnableGravity=true, bGyroscopicTorqueEnabled=false, bStartAwake=true는 BodyInstanceCore.cpp:13-24 [V]. 씬이 소유한다. Mobility 대신 `bStatic`을 쓰고, kinematic = !bStatic && !bSimulatePhysics |
| UBodySetup + FKAggregateGeom + FKSphereElem/FKBoxElem/FKSphylElem/FKConvexElem | `FBodySetup`, `FKAggregateGeom`, `FK*Elem` | FKBoxElem X/Y/Z는 full extent, FKSphylElem Length는 로컬 Z 선분 길이 (BoxElem.h:35-55, SphylElem.h:36-50 [V]). `Rotation`은 UE처럼 `FRotator`다 (BoxElem.h:32, SphylElem.h:32 [V]). 호스트가 마샬링할 때 FQuat로 한 번 변환해 캐시한다 |
| UPhysicalMaterial | `FPhysicalMaterial` | 0.7/0/0.3, Density 1 g/cm³, RaiseMassToPower 0.75, sleep 1 cm/s, 0.05 rad/s, 4틱 (PhysicalMaterial.cpp:42-60 [V]). `EFrictionCombineMode {Average 0, Min 1, Multiply 2, Max 3}`은 PxCombineMode와 값이 같고 큰 값이 우선한다. 정지마찰 규칙은 아래 "정지마찰" 참고 |
| FConstraintInstance + FConstraintProfileProperties | 같은 이름 (Phase 1 부분집합) | ball/hinge만 [확장 M9]. Body1 = child (ConstraintBone1), Body2 = parent. 프리셋은 UE와 같은 free function `ConstraintUtils::ConfigureAsBallAndSocket` / `ConfigureAsHinge(Instance, bOverwriteLimits)` (ConstraintUtils.h:19, :10 [V]) |
| UPhysicsSettingsCore / UPhysicsSettings / FChaosSolverConfiguration / FChaosPhysicsSettings | `FPhysicsSettingsCore` / `FPhysicsSettings` / `SolverOptions` / `FChaosPhysicsSettings` | UE 게임처럼 코어 필드를 포함한 모든 UE 기본 키를 `[/Script/Engine.PhysicsSettings]`에 둔다 (BaseEngine.ini:3406 [V]; UPhysicsSettings가 UPhysicsSettingsCore를 상속). PhysX 시대 필드(Bounce 200, ContactOffset 0.02/2/8, PCM, scratch 262144, TreeRebuildRate 10)는 UE 위치에 그대로 둔다. 5.8.3은 이 값들을 읽지 않지만 UE4Compat은 읽는다. 하네스 전용 설정은 `[/Script/PhysicsTest.PhysicsTestSettings]`의 `FPhysicsTestSettings` |
| FPhysicsInterface (= FPhysInterface_Chaos) | `FPhysicsInterface` (static facade) | `_AssumesLocked` 동사명을 그대로 쓴다. 임펄스는 UE처럼 `AddImpulse_AssumesLocked(Handle, Impulse, bIsInternal)`과 `AddVelocity_AssumesLocked(Handle, Velocity, bIsInternal)`로 나뉜다 (ChaosEngineInterface.h:465-467 [V]). 명령은 핸들의 씬 프록시에 버퍼링한다. ExecuteRead/Write는 락 없는 pass-through |
| FChaosScene 프레임 동사 + FChaosEngineInterface static 동사 | 백엔드 측 `PhysicsTest::Abi::IPhysicsBackendScene` (Kit), 호스트 측 `FPhysicsBackendScene` (C 테이블 프록시) | CreateActors(+AddActorsToScene_AssumesLocked), ReleaseActors, CreateConstraints, SetUpForFrame / StartFrame / WaitPhysScenes / EndFrame |
| FChaosSolversModule + InitGamePhys/TermGamePhys + FModuleManager | `Abi::IPhysicsBackend`, `FPhysicsBackendModule`, `FPhysicsBackendRegistry` | 모듈은 언로드하지 않는다 |
| FPhysicsActorHandle (= FSingleParticlePhysicsProxy*) | 같은 이름 `{FPhysicsBackendScene* Scene; uint32 Index; uint32 Serial}` | Index = 생성 순서 = 삽입 순서 = 해시 순서 |
| Chaos::FMassProperties + BodyUtils::ComputeMassProperties | `FMassProperties`, `BodyUtils::` | 하네스가 계산해 명시적으로 전달한다 (D5). 규칙은 §5.3 |
| FRigidBodyState / GetUnrealWorldTransform | `FBodyInstanceState` {BodyTM = 바디 원점의 강체 자세(스케일 1), cm/s, rad/s, ObjectState} | UE의 AngVel은 deg/s라서 이름을 `AngularVelocityInRadians`로 다르게 했다. 스케일은 UE처럼 `FBodyInstance::Scale3D`에만 있다 (§5.2 #26) |
| Chaos::EObjectStateType | `EObjectStateType` {Uninitialized 0, Sleeping 1, Kinematic 2, Static 3, Dynamic 4} | 값이 같다 (Chaos/Particle/ObjectState.h:9-18 [V]) |
| ECollisionChannel / `Chaos::Filter::FShapeFilterData` (레거시 `FCollisionFilterData`) | `ECollisionChannel` (uint8, 0..31) + `FBodyCollisionFilter` {ObjectType, BlockMask uint32, CollisionEnabled} | 상호 차단 검사는 UE와 같다 (CollisionFilterData.cpp:263-272 [V]). 채널 수는 아래 "충돌 채널" 참고 |
| Chaos::FDebugDrawQueue / IChaosDDRenderer / FChaosDebugDrawSettings / Chaos::DebugDraw::Draw* | `FDebugDrawQueue` / `IDebugDrawRenderer` / `FDebugDrawSettings` / `DebugDraw::Draw*` | `FDebugDrawQueue`는 Engine의 DrawDebugHelpers가 아니라 **Chaos::FDebugDrawQueue**(DebugDrawQueue.h)를 미러링한다. CVar 이름이 같다 |
| IConsoleManager, TAutoConsoleVariable, FCommandLine, FParse, FConfigFile, UE_LOG, check/ensure, FPlatformTime | 같은 이름의 소형 구현 | UE 복제판이 아니라 필요한 부분만 구현한다. CLI11은 쓰지 않는다 |
| FEditorViewportClient / FEditorCameraController / `stat ChaosCounters` | `FPhysicsTestViewportClient` / `FViewportCameraController` / Stats 패널 | |
| UE4 PhysX 계층 | `PhysicsTest::PhysXBackend`에 같은 이름으로 클린룸 재구현 | §8.2 |

**충돌 채널**
- UE 5.8.3은 기본 채널 8개, 엔진 trace 채널 6개, 게임 trace 채널 50개로 64채널을 직렬화한다. `ECC_OverlapAll_Deprecated` = 64, `ECC_MAX` = 65이고 마스크는 uint64다 (EngineTypes.h:1101-1176; CollisionFilterData.cpp:263-272, 310-313 [V]).
- 하네스는 의도적으로 32채널(C++ 상수 `NumCollisionChannels = 32`, C ABI 매크로 `PT_NUM_COLLISION_CHANNELS`, uint32 마스크)을 쓴다. C++ 상수에 매크로와 같은 이름을 쓰면 `PtBackendAbi.h`를 먼저 include한 TU에서 선언이 깨지므로 이름을 나눴다. M2에서 두 이름이 함께 보이는 곳에 `static_assert(NumCollisionChannels == PT_NUM_COLLISION_CHANNELS)`를 둔다. `ObjectType`이 32~255이면 무효다: `FBodyCollisionFilter::IsValid()`가 거짓이고 채널 마스크는 0이며(32로 나눈 나머지 채널로 접히지 않는다) `FPhysScene::SpawnBody`가 `checkf`로 거부한다. M2 마샬링도 거부한다. UE4 PhysX의 word3 레이아웃은 채널을 5비트로 담고(PhysicsFiltering.h:34 NumCollisionChannelBits=5, EngineTypes.h:1063 NumExtraFilterBits=6 [V]), Bullet의 group/mask는 int라서 둘 다 32채널까지만 담기 때문이다.
- Chaos 브리지는 `Chaos::Filter::FShapeFilterBuilder`로 `FShapeFilterData`를 만들 때 BlockMask를 uint64로 넓힌다 (Chaos 모듈 CollisionFilterData.h:116-171 [V]). 레거시 4-word `FCollisionFilterData`는 이 uint64 마스크를 두 word로 나눠 담을 뿐이다 (CollisionFilterData.cpp:308-315 [V]).

**정지마찰 (Chaos 규칙) [V]**
- Chaos는 머티리얼마다 `max(F, SF)`를 먼저 구하고, 그것을 마찰 combine 모드로 결합한다 (PBDCollisionConstraints.cpp:473-476). 접촉에서는 다시 `max(결합된 정지, 결합된 동적)`을 쓴다 (PBDCollisionConstraint.h:486).
- 정리하면 쌍의 정지마찰 = `max(combine(max(F0,SF0), max(F1,SF1)), combine(F0,F1))`이다. 네 combine 모드가 모두 단조이므로 바깥 max는 결과를 바꾸지 않는다.
- ABI는 원래 값 `Friction`과 `StaticFriction`을 그대로 넘긴다. 호스트가 미리 합치지 않는다.
- 백엔드별 처리 (§8):
  - PhysX SdkDefault는 머티리얼 정지마찰에 `max(F, SF)`를 넣어 이 규칙을 그대로 재현한다.
  - PhysX UE4Compat는 정지 = 동적 [M]이다.
  - Jolt와 Bullet은 쌍마다 마찰 계수가 하나뿐이라 (`mCombinedFriction`, `m_combinedFriction`) 동적 결합값 `combine(F0, F1)`만 쓴다. 두 머티리얼 모두 SF ≤ F이면 Chaos와 같다. SF > F인 머티리얼이 낀 쌍은 정지마찰이 Chaos보다 작아질 수 있으므로, 이 백엔드들은 effective 키 `material.static_friction_rule=dynamic_only`를 내고 그런 차이에 by-design 태그를 단다.

**제거한 것**
- AActor, UActorComponent, UPrimitiveComponent
- FPhysicsCommand 락
- 리플리케이션
- 컴포넌트 동기화 (OnSyncBodies → MoveComponent)
- FCollisionShape와 scene query
- Tapered capsule, LevelSet
- SDK 백엔드의 async interpolation (Chaos AsyncFixed는 UE 동작 그대로 보간한다, §9.2)
- TG_* tick group (델리게이트 훅만 남김)
- render proxy, FScene

### 5.2 UE 대응 차이 (의도적 차이와 확인된 일치)

UE 소스를 읽는 사람이 바로 눈치챌 항목을 모두 적는다. "일치"는 이름과 시그니처를 UE에 맞췄다는 뜻이다.

| # | 항목 | UE 5.8.3 | 하네스 | 판정 |
|---|---|---|---|---|
| 1 | `FKBoxElem::Rotation`, `FKSphylElem::Rotation` | `FRotator` (BoxElem.h:32, SphylElem.h:32 [V]) | `FRotator` + 마샬링 시 FQuat 캐시 | 일치 |
| 2 | FBox 유효 플래그 | `uint8 IsValid` (Box.h:45 [V]) | `IsValid` | 일치 |
| 3 | 조인트 프리셋 | `ConstraintUtils::ConfigureAsBallAndSocket` / `ConfigureAsHinge(FConstraintInstance&, bool bOverwriteLimits = true)` (ConstraintUtils.h:19, :10 [V]) | 같은 이름의 free function | 일치 |
| 4 | 임펄스와 속도 변화 | `AddImpulse_AssumesLocked(Handle, Impulse, bIsInternal)`, 별도의 `AddVelocity_AssumesLocked` (ChaosEngineInterface.h:465-467 [V]). `FBodyInstance::AddImpulse(Impulse, bVelChange, FAsyncPhysicsTimestamp, APlayerController*)`가 bVelChange로 둘 중 하나를 고른다 (BodyInstance.cpp:3875-3888 [V]) | 같은 두 동사. ABI 명령도 `PT_CMD_ADD_IMPULSE`와 `PT_CMD_ADD_VELOCITY`로 나눈다. `FBodyInstance::AddImpulse(Impulse, bVelChange)`는 UE처럼 둘 중 하나로 분기한다. 비동기 타임스탬프와 PlayerController 인자는 없다 | 일치 (네트워크·비동기 인자만 제거) |
| 5 | `FDebugDrawQueue` 시그니처 | Chaos::FDebugDrawQueue: `DrawDebugPoint(Position, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)` (Size 인자 없음), `DrawDebugCoordinateSystem(Position, const FRotator& AxisRot, Scale, ...)`, `DrawDebugCircle(Center, Radius, Segments, Color, bPersistentLines, LifeTime, DepthPriority, Thickness, YAxis, ZAxis, bDrawAxis)` (DebugDrawQueue.h:114,150,209 [V]) | 같은 인자 순서 | 일치 |
| 6 | `DrawDebugString` | `(TextLocation, const FString&, AActor* TestBaseActor, Color, Duration, bDrawShadow, FontScale)` (DebugDrawQueue.h:196 [V]) | `std::string_view`, `TestBaseActor` 없음 | 의도적 차이 (Actor 없음) |
| 7 | `DrawDebugTriangle` | Chaos::FDebugDrawQueue에는 없음 | 하네스 추가. `IChaosDDRenderer::RenderTriangle`에 대응 | 하네스 확장 |
| 8 | `IDebugDrawRenderer::RenderLatentCommand` | IChaosDDRenderer에 있음 | 하네스 인터페이스에는 없다. UE 브리지가 구현해 `PtDebugDrawCmd`로 직렬화한다 | 의도적 차이 |
| 9 | 충돌 채널 | 64채널, `ECC_MAX` = 65, uint64 마스크 | 32채널, uint32 마스크 | 의도적 단순화 (§5.1) |
| 10 | 모빌리티 | `UPrimitiveComponent::Mobility` | `FBodyInstance::bStatic` | Component 제거 |
| 11 | 상태 각속도 | `FRigidBodyState::AngVel` (deg/s) | `AngularVelocityInRadians` (rad/s) | 이름으로 단위 구분 |
| 12 | 액터 핸들 | `FSingleParticlePhysicsProxy*` | `{Scene, Index, Serial}` | ABI 경계 때문 |
| 13 | 물리 락 | FPhysicsCommand 락, ExecuteRead/Write | 락 없는 pass-through + 명령 버퍼 | 단일 호출 스레드 (R5) |
| 14 | 초기 속도 | `InitDynamicProperties`가 선속도만 설정하고, 값은 소유 Actor에서 온다 | `FBodyInstance::InitialLinearVelocity`와 `InitialAngularVelocityInRadians` 필드 (각속도는 하네스 확장, 기본 0) | 하네스 확장 (S02, S07이 사용) |
| 15 | 물리 설정 ini 섹션 | 모든 물리 설정이 `[/Script/Engine.PhysicsSettings]` | 같음 | 일치 |
| 16 | `FRandomStream` | UE의 LCG | mt19937_64 + 하네스 고유의 [0,1) 매핑 (seed 0x5EED) | 의도적 차이 (재현성만 목적) |
| 17 | 셰이프 이름 | `FName` | `std::string` | UObject 없음 |
| 18 | Core 관용구 | UE 전체 구현 | 필요한 부분만의 소형 구현 (`FParse`, CVar 레지스트리, ini 리더, `UE_LOG`, `check`/`ensure`) | 의도적 축소 |
| 19 | 수학 타입 기본 생성 | `FVector`, `FQuat`는 초기화하지 않는다 (`ForceInit`가 필요). `FTransform()`은 항등 | `FVector`는 0, `FQuat`는 항등으로 초기화. `FTransform()`은 항등 (UE와 같음) | 의도적 차이 (미초기화 값이 수치를 오염시키지 않게) |
| 20 | `FTransform::Inverse()`, 스케일이 모두 0 | Win64는 벡터화 구현을 컴파일한다 (`ENABLE_VECTORIZED_TRANSFORM`, ScalarRegister.h:15-16 [V]). 어떤 \|Scale\| 성분도 `SMALL_NUMBER`보다 크지 않으면 Identity (TransformVectorized.h:363-374 [V]). 스칼라 구현(TransformNonVectorized.h:304-312)은 역회전 + 0 스케일 | Win64 동작 그대로 Identity. 0에 가까운 성분만 있으면 그 성분의 역수를 0으로 둔다 | 일치 (M1에서 고침) |
| 21 | 음수 스케일 `FTransform` 합성 | 행렬 경로 `MultiplyUsingMatrixWithScale` (TransformVectorized.cpp:320-324 [V]) | 구현하지 않고 모든 빌드에서 Fatal (종료 코드 3, CTest `PhysicsTest.Probe.NegativeScaleFatal`) | 의도적 차이 (지원 범위 밖을 조용히 다르게 계산하지 않음) |
| 22 | 질량 규칙의 정밀도, 대각화, 기여 요소가 없는 바디 | 1단계 밀도를 float로 계산한다 (`0.001f × Density`, 1 g/cm³이면 0.0010000000475 kg/cm³, BodyUtils.cpp:30-45 [V]). `ApplyMassPropertiesModifiers`가 `OldMass`/`NewMass`/`MassRatio`를 float로 계산 (BodyUtils.cpp:50-75 [V]). 대각화는 Chaos의 닫힌 형식 고유값 풀이 (MassProperties.cpp:17-96 [V]). 질량에 기여하는 셰이프가 하나도 없으면 `FBodyInstance`가 그 셰이프들을 빼고 질량 계산을 건너뛰므로, 관성 trace의 `CHAOS_ENSURE`가 실패하고 질량을 설정하지 않는다 (BodyInstance.cpp:3405-3417, 3484-3507 [V]) | 1·4·5단계를 double로 계산 (밀도 0.001, 상대 차이 4.7e-8). 대각화는 순환 Jacobi이고, Chaos와 같은 "이미 대각" 조기 종료(OffDiag²/Trace² < 1e-8)를 쓴다. 텐서는 같고, 대각이 아닌 텐서의 주축 순서만 다를 수 있다. 셰이프 체적은 Chaos처럼 float `UE_PI`와 float `4/3`(캡슐)을 쓴다. 기여 요소가 없으면 Chaos `CalculateMassPropertiesFromShapeCollection`의 대체값(523 kg, 5.24e5 kg·cm², MassProperties.cpp:481-497 [V])에 질량 규칙을 적용하고 Error 로그를 남긴다. UE 바디는 이 경로에 오지 않으므로 UE 동작이 아니라 결정적인 하네스 값이다 | 의도적 차이 (상대 1e-7 수준, 모든 엔진이 같은 하네스 값을 받는다) |
| 23 | `FParse` 숫자 | 키가 있으면 true, `Atoi`/`Atof`로 변환 (Parse.cpp:709-718) | 숫자를 실제로 읽고 형에 들어갈 때만 true. unsigned는 `-`를 거부 (`-Threads=-1`이 4294967295가 되지 않음), 범위를 넘으면 거부. M3부터 숫자 뒤에는 끝, 공백, `,`, `)`, 닫는 `"`만 올 수 있고 (`-Threads=4x`, `-Threads=1.5` 거부), unsigned는 `0x`/`0X` 접두를 16진수로 읽는다 (`-Affinity=0x55` = 85) | 의도적 차이 (CLI 오타를 조용히 받지 않음, Progress M0 §3 #13, M1 §3, M3 §3 #18) |
| 24 | 프로그램 진입점과 명령줄 | `TCHAR`(UTF-16) 명령줄 | `wmain` + `FUtf8ArgV`로 UTF-16을 UTF-8로 바꿔 `FCommandLine`에 넣는다. `main(int, char**)`는 ANSI 코드 페이지(cp949)를 받으므로 쓰지 않는다 | 일치 (문자열 표현만 UTF-8) |
| 25 | `FBodySetup::BuildScale3D` | `RescaleSimpleCollision`이 빌드 스케일을 AggGeom에 미리 굽고 BuildScale3D에는 그 값을 기록만 한다. `UBodySetup::GetScaledVolume`과 ChaosInterfaceUtils는 읽지 않는다 (BodySetup.cpp:756-812, 2417-2425 [V]) | 같다. 메타데이터로만 두고 질량·체적·마샬링에서 곱하지 않는다. 지오메트리를 키우는 것은 `FBodyInstance::Scale3D`뿐이다 | 일치 (M1 수정 라운드에서 고침. 전에는 Scale3D에 곱해 UE 에셋 값을 두 번 스케일했다) |
| 26 | 바디 자세 | `GetUnrealWorldTransform`은 강체 액터 자세다 (`GetTransform_AssumesLocked`, 스케일 없음). 스케일은 `FBodyInstance::Scale3D` (BodyInstance.cpp:2732-2747 [V]) | `FBodyInstanceState::BodyTM` = 회전 + 이동, 스케일 1. 스케일은 `Scale3D`에만 둔다 | 일치 (M1 수정 라운드에서 고침. 0번째 프레임과 M2 readback이 같은 형태다) |
| 27 | 물리 CVar | `p.ComNudgeAffectsInertia` (BodyUtils.cpp:16-17), `p.SensitiveSleepThresholdMultiplier` (기본 1/20, BodyInstance.cpp:106-110) [V] | 같은 이름·기본값. 둘 다 `Physics/PhysicsConsoleVariables.cpp` 한 TU에 있고 프로필 리졸버가 그 TU를 참조하므로, 어떤 프로그램이 정적 라이브러리에서 무엇을 링크하든 등록된다 | 일치 (M1 수정 라운드에서 Sensitive 배수를 상수에서 CVar로 바꿨다) |
| 28 | ini 섹션 이름 | `FConfigFile`이 `TMap<FString, FConfigSection>`이라 대소문자를 무시한다 (ConfigCacheIni.h:502, :574 [V]) | 대소문자 무시. 처음 쓴 철자를 유지하고, 정규 키는 정규 철자를 쓴다 | 일치 (M1 수정 라운드에서 고침) |
| 29 | CVar 우선순위 충돌 | 더 낮은 우선순위의 Set은 로그만 남기고 무시한다 | `IConsoleVariable::Set`은 false를 돌려주고 (M1 §3 #15), `FProfileResolver::ApplyConsoleVariables`는 거부된 CVar마다 이름·현재 값·현재 SetBy를 오류로 돌려준다 | 의도적 차이 (적용되지 않은 값이 적용된 것처럼 기록되지 않게) |
| 30 | 숫자 텍스트의 inf/nan | `FCString::Atof`와 ImportText가 `inf`/`nan`을 받는다 | `FParse` float/double, float CVar, ini `GetFloat`/`GetDouble`, 프로필 값은 유한수만 받는다 | 의도적 차이 (프로필 오타가 엔진 상태에 inf/nan으로 들어가지 않게) |
| 31 | 프레임 델리게이트 | UE5: `SetUpForFrame` 다음 `FChaosScene::StartFrame` → `FPhysScene_Chaos::OnStartFrame(MDeltaTime)`이 `OnPhysScenePreTick`과 `OnPhysSceneStep`을 clamp된 프레임 dt로 한 번씩 broadcast한다 (ChaosScene.cpp:331-374, PhysScene_Chaos.cpp:2272-2316 [V]). `OnPhysScenePostTick`은 dt 없이 EndFrame에서 (ChaosScene.cpp:576 [V]) | PreTick은 `StartFrame` 안(phase SetUp, 스텝 전)에서 `FStepPlan::FrameDeltaTime`(= MDeltaTime)으로 한 번. `OnPhysSceneStep`도 두 driver 모두 PreTick 바로 뒤에 프레임 dt로 한 번 (스텝이 없는 프레임 포함). Harness driver의 `UE4PhysSubstep` 규칙(PhysX UE4Compat)만 서브스텝마다 서브스텝 dt로 부른다 (UE4 PhysX substep 방식 [M]). PostTick은 같은 프레임 dt를 넘긴다 | 일치 (PreTick은 M1 수정 라운드, Step은 M2 수정 라운드 1에서 고침. 전에는 Harness driver가 UE5 규칙에서도 스텝마다 불러, 거기서 더한 힘이 1+2+…+N 스텝에 누적됐다) |
| 32 | `-ExecCmds` 안의 `name=value` | UE 콘솔은 공백에서만 나눈다 (`GetTextSection`). `p.X=0`은 콘솔 객체 이름으로 찾지 못해 조용히 무시된다 | `name value`와 `name=value`를 둘 다 받는다 (`IConsoleManager::ProcessUserConsoleInput`, 리졸버의 `-ExecCmds`). README 예시는 UE 형식으로 쓴다 | 의도적 차이 (M2에서 기록. 하네스 줄을 UE에 그대로 옮기면 `name=value` 항목은 효과가 없다) |
| 33 | Native driver의 `OnPhysSceneStep` | UE5 `OnStartFrame`이 PreTick 다음에 Step을 프레임 dt(`MDeltaTime`)로 한 번 broadcast한다 (PhysScene_Chaos.cpp:2272-2316 [V]) | 두 driver 모두 같다: 프레임당 한 번, `FStepPlan::FrameDeltaTime`. `UE4PhysSubstep` 규칙만 #31대로 서브스텝마다 | 일치 (M2) |
| 34 | `FBodyInstance::SetLinearVelocity(V, bAddToCurrent)` | `bAddToCurrent`이면 `GetLinearVelocity_AssumesLocked`(GT 파티클 속도, 같은 프레임에 앞서 쓴 값 포함)에 더한 값을 설정한다 (BodyInstance.cpp:3640-3643 [V]) | UE처럼 `FPhysicsInterface::GetLinearVelocity_AssumesLocked`에 더한다. 이 읽기는 명령 큐를 flush하고 전체 상태를 당기므로 같은 프레임에 앞서 쓴 속도와 스텝이 없는 프레임에 쓴 속도를 본다. 각속도도 같다 | 일치 (M2 수정 라운드 1에서 고침. 전에는 마지막으로 당겨 온 `FBodyInstanceState`에 더해 앞서 쓴 값을 보지 못했다) |
| 35 | `-ini:` 형식 오류 | 잘못된 쌍을 조용히 건너뛴다 (ConfigCacheIni.cpp:2475-2478 [V]) | 로드 오류로 거부한다 (M1 `PhysicsTest.Profile.CommandLineIniOverride`) | 의도적 차이 (M1에서 테스트로 고정, M2에서 기록) |
| 36 | `-ini:`로 준 CVar의 우선순위 | UE는 `-ini:` 덮어쓰기를 ini 데이터로 적용한다 | `ECVF_SetByConsoleVariablesIni`. `-ExecCmds`만 `ECVF_SetByConsole` (`FProfileResolver::GetConsoleVariableSetBy`) | 일치 (M2에서 고침. M1은 Origin이 CommandLine이면 모두 Console로 적용했다) |
| 37 | 하네스 물리 CVar와 씬 | CVar는 프로세스 전역이고 씬은 그것을 읽기만 한다 | 같은 전역 CVar를 쓰되, `FPhysScene`이 생성 시 자기 설정의 값(없으면 기본값)으로 맞추고 더 높은 우선순위의 낡은 값은 경고와 함께 덮는다. `SpawnBody`·`FlushDeferredActors`에서 바뀌었으면 Fatal. 그래서 동시에 살아 있는 씬은 이 CVar 값이 같아야 한다 (M12 나란히 비교 전에 씬 로컬로 옮긴다, Progress M2 §5) | 하네스 추가 (M2. 설정에 기록·해시된 값과 실제 질량·sleep 임계가 어긋나지 않게) |
| 38 | 프레임 안의 힘 (`bAllowSubstepping`) | UE5는 `bAllowSubstepping`을 쓰지 않는다 (ChaosEngineInterface.cpp:932-950, :1024-1037). push한 프레임의 모든 스텝이 첫 스텝의 가속도를 받고 (`CopySubstepData`, ChaosMarshallingManager.cpp:103-111, :325-342), push가 없는 프레임의 힘은 다음 push에서 1/ExternalSteps로 평균된다 (PhysicsSolverBase.cpp:623-646, PBDRigidsSolver.cpp:1582) [V] | Native 백엔드(Null)와 Harness driver의 UE5 규칙이 같다. 두 driver 모두 힘을 스텝을 push하는 프레임까지 호스트가 들고 있다가 그 push에서 보낸다 (Native는 한 번, Harness는 스텝마다). `PT_CMDF_ALLOW_SUBSTEPPING`은 정보용이다. Harness driver의 `UE4PhysSubstep` 규칙(PhysX UE4Compat)만 UE4처럼 `bAllowSubstepping` 힘을 낸 서브스텝부터 모든 서브스텝, 나머지를 낸 서브스텝 하나에 준다 [M]. Harness driver는 평균 가중치를 가속도가 아니라 힘에 곱한다 | 일치 (M2 수정 라운드에서 고침. 전에는 Native에서도 UE4 규칙을 썼다). 가중치 위치는 의도적 차이 (AsyncFixed에서 마지막 비트) |
| 39 | kinematic 목표의 중간 스텝 | solver substep이면 k번째 스텝이 현재 자세에서 1/(N−k)만큼 `FVec3::Lerp` + float `FRotation3f::Slerp`, 아니면 첫 스텝에 목표 (`PseudoFraction`, PhysicsSolverBase.cpp:250, PBDRigidsEvolutionGBF.cpp:1203-1220 [V]) | Null과 Harness driver(UE5 규칙)가 같은 규칙. 회전 Slerp는 double이고, Shoemake 식을 하네스가 직접 쓴 것이다 (동작 기준은 UE `FQuat::Slerp`, UnrealMath.cpp:1226-1255 [V]: 짧은 호, \|cos\| ≥ 1 − 1e-4에서 선형 가중치). Harness driver의 UE4 규칙은 `bAllowSubstepping` 목표만 프레임 시작 자세에서 (k+1)/N로 Lerp/Slerp하고, 나머지는 첫 서브스텝에 닿는다 [M]. Harness driver는 같은 프레임의 텔레포트 자세를 보간 시작점으로 쓴다 (#43) | 일치 (M2 수정 라운드에서 고침. 전에는 절대 alpha (k+1)/N의 NLerp). float과 double Slerp의 차이는 의도적 차이 (중간 스텝 회전만 다르고 마지막 자세는 같다) |
| 40 | `AddForce`/`AddTorque`와 sleep | GT 프록시의 `AddForce`/`AddTorque`가 Sleeping 바디를 언제나 Dynamic으로 바꾼다 (0 벡터 포함, SingleParticlePhysicsProxy.h:706-720, :757-771 [V]) | 호스트가 그 깨우기를 힘을 낸 자리에 `PT_CMD_WAKE_UP`으로 큐에 넣고, 힘은 push 때 `PT_CMDF_NO_WAKE`로 보낸다 (두 driver 같음). 그 사이에 재운 바디는 깨지 않고 힘도 받지 않는다. 플래그 없는 `PT_CMD_ADD_FORCE`는 UE의 `AddForce`대로 Null이 언제나 깨운다 | 일치 (M2 수정 라운드에서 0 벡터, 수정 라운드 1에서 호출 순서를 고침. 전에는 push 끝의 힘이 그 앞의 PutToSleep 뒤에 다시 깨웠고, 스텝이 없는 프레임에서는 깨우지 않고 힘을 버렸다) |
| 41 | 해제한 바디에 걸린 명령 | 프록시와 함께 버려진다 | `FPhysScene::ReleaseBody`가 그 바디의 힘·kinematic 목표를, `FPhysicsBackendScene::ReleaseActor`가 큐의 명령을 버린다 | 일치 (M2 수정 라운드에서 고침. 전에는 다음 push가 Fatal로 끝났다) |
| 42 | 씬 수준 동사와 진입점 | `FPhysScene_Chaos::AddForce_AssumesLocked`/`AddTorque_AssumesLocked(FBodyInstance*, ..., bAllowSubstepping, bAccelChange)`, `SetKinematicTarget_AssumesLocked(FBodyInstance*, TM, bAllowSubstepping)` (PhysScene_Chaos.h:232-238 [V])이 `FPhysicsInterface`로 넘긴다 (PhysScene_Chaos.cpp:1670-1713 [V]). `FBodyInstance::SetBodyTransform`은 kinematic + `ETeleportType::None`이면 씬 동사를 부르고, 텔레포트면 `FPhysicsInterface::SetKinematicTarget_AssumesLocked` 뒤 `SetGlobalPose_AssumesLocked` (BodyInstance.cpp:2804-2827 [V]) | 같은 이름의 씬 동사. 방향만 반대다: `FPhysicsInterface`의 힘·토크·kinematic 목표가 소유 씬으로 가서 씬의 프레임 규칙을 탄다. `SetBodyTransform`은 UE5와 같은 호출을 한다. 인터페이스 경로의 kinematic 목표는 `bAllowSubstepping` false (UE4에서 서브스테퍼를 거치지 않던 경로 [M], UE4 규칙에서만 읽는다) | 일치 (M2 수정 라운드 1. 전에는 `EnqueueForce`/`EnqueueKinematicTarget`이었고 `SetBodyTransform`·인터페이스의 목표가 씬 규칙을 건너뛰어 Harness substepping에서 첫 서브스텝에 목표에 닿았다) |
| 43 | kinematic 바디의 텔레포트 | `SetGlobalPose_AssumesLocked`가 kinematic이면 자세를 목표로도 두고 V·W를 0으로 한다 (ChaosEngineInterface.cpp:2550-2557 [V]). `SetX`는 잠든 바디를 bAutoWake와 무관하게 깨운다 (SingleParticlePhysicsProxy.h:240-251 [V]) | Null `PT_CMD_SET_GLOBAL_POSE`가 같다 (kinematic: 목표 = 자세, V·W = 0. dynamic: 언제나 깨움, `PT_CMDF_AUTO_WAKE`는 정보용). 호스트는 같은 프레임의 대기 목표를 텔레포트 자세로 바꾸고, Harness driver는 그 뒤의 목표를 그 자세에서 보간한다 (`FPhysScene::TeleportKinematic_AssumesLocked`) | 일치 (M2 수정 라운드 1) |
| 44 | PutToSleep 뒤의 힘 | GT `SetObjectState(Sleeping)`이 dynamics dirty 표시를 지워 그 앞의 힘을 push하지 않는다. 뒤에 힘이나 임펄스가 오면 다시 dirty가 되어 앞의 힘도 함께 간다 (ParticleHandle.h:3755-3766 [V]) | 호스트가 같은 규칙을 씬의 힘 목록에 적용한다 (`MarkDynamicsClean_AssumesLocked`/`MarkDynamicsDirty_AssumesLocked`). sleep을 끈 씬은 PUT_TO_SLEEP을 무시하므로 지우지 않는다. 임펄스 쪽(PutToSleep 앞의 임펄스가 버려지는 것)은 Null이 재현하지 않는다 | 힘은 일치, 임펄스는 의도적 차이 (M2 수정 라운드 1, Progress M2 §7) |
| 45 | 스텝이 없는 프레임의 명령 | GT 쓰기는 호출하는 순간 GT 파티클에 들어가고, 다음 push에서 PT로 간다 | 두 driver 모두 매 프레임 push한다. Harness driver는 스텝이 없는 프레임(dt ≤ MinPhysicsDeltaTime, push가 없는 AsyncFixed 프레임)에도 큐를 flush한다 (힘·kinematic 목표는 규칙대로 씬이 들고 있다) | 일치 (M2 수정 라운드 1. 전에는 Harness driver가 그 프레임의 큐를 다음 스텝까지 들고 있어 읽기와 bAddToCurrent가 보지 못했다) |
| 46 | 토크의 월드 관성 시점 | GT `AddTorque`가 호출 시점의 GT 회전으로 각가속도로 바꾸고, push의 모든 스텝이 그 각가속도를 쓴다 (`CopySubstepData`) | Native(Null)는 push 때 한 번 바꾼다. Harness driver는 스텝마다 토크를 보내므로 SDK 엔진이 스텝마다 그 스텝의 회전으로 바꾼다 | 의도적 차이: 같은 프레임에 토크 뒤 같은 바디를 텔레포트하면 Native가 UE와 다르다. Substepping에서 빠르게 도는 바디는 두 driver가 각속도 1e-2 rad/s 수준으로 갈린다 (A10 (d), Progress M2 §7) |
| 47 | `MaxDepenetrationVelocity` | `bOverrideMaxDepenetrationVelocity`가 아니면 -1(솔버 설정을 따름)을 보낸다 (BodyInstance.cpp:3713-3722 [V]). -1이면 솔버 설정 `CollisionInitialOverlapDepenetrationVelocity`(기본 -1 = 1e10 cm/s, 사실상 제한 없음. PBDRigidsSolver.cpp:3668-3672, PBDCollisionConstraints.h:250-265 [V]), 0이면 초기 침투를 밀어내지 않는다. 쌍은 두 바디 값 중 큰 값 (PBDCollisionContainerSolver.cpp:180-219, PBDCollisionConstraint.cpp:341-344 [V]) | 마샬링이 같은 규칙으로 -1 또는 바디 값을 보낸다 (`PtActorCreateInfo::MaxDepenetrationVelocity`, 0 = 밀어내지 않음). Null은 쓰지 않고, Jolt는 대응 설정이 없어 `jolt.max_depenetration_velocity` = n/a로 기록한다 | 일치 (M3에서 고침. M2는 0을 "제한 없음"으로 적었다) |
| 48 | kinematic 바디의 속도 기준점 | kinematic 속도는 파티클 X(actor 원점)의 차분이다 (PBDRigidsEvolutionGBF.cpp:1180-1239 [V]). `GetWorldVelocityAtPoint`는 COM 기준으로 계산한다 (ChaosEngineInterface.cpp:722-735 [V]) | `PtBodyState::LinearVelocity`는 kinematic이면 원점 속도다 (Null: 원점 차분, Jolt: `MoveKinematic`에 준 원점 속도를 저장해 돌려준다). dynamic은 COM 속도 그대로 | 일치 (Chaos 기준점). `GetWorldVelocityAtPoint`의 불일치는 UE 쪽 문제로 기록만 한다 |
| 49 | `FBodyInstance::InitBody` 두 번, flush 전 `TermBody` | UE5 `TermBody`가 `RemoveBodyInstanceFromPendingLists_AssumesLocked`로 대기 목록에서 뺀다 (PhysScene_Chaos.h:230 [V]). UE의 `InitBody`는 `TermBody`를 부르지 않는다 (BodyInstance.cpp:1759-1789 [V]). 다시 만드는 것은 호출자(`UPrimitiveComponent`의 physics state 재생성: 먼저 term, 그다음 init)의 몫이다 | flush 전 대기 바디는 `TermBody`가 뺀다 (UE와 같다). `InitBody`는 바디가 이미 씬에 있으면 먼저 `TermBody`를 부른다 (actor 두 개가 생기지 않는다). 같은 바디를 두 번 대기시키면 Fatal | 대기 목록은 일치. `InitBody`가 `TermBody`를 부르는 것은 **의도적 차이**다: 하네스에는 컴포넌트 계층이 없어 재생성 순서를 대신 지켜야 하고, UE에서 같은 바디를 두 번 init하는 것은 호출자 버그다 (M3 수정 라운드 0에서 "일치"를 고침) |
| 50 | `SetLinearVelocity`의 `bAutoWake`, `IsInstanceAwake` | UE5 `SetLinearVelocity(V, bAddToCurrent, bAutoWake)`는 bAutoWake를 쓰지 않는다 (BodyInstance.cpp:3631-3666 [V]). `IsInstanceAwake`는 GT 상태 (:3580-3593) | 같은 시그니처, bAutoWake는 무시한다. `IsInstanceAwake()`는 dynamic이고 잠들지 않았는지를 호스트 프록시의 마지막 상태(마지막 pull)로 답한다 | 일치 |

### 5.3 질량 규칙 (`BodyUtils::ComputeMassProperties`, BodyUtils.cpp [V])

UE가 하는 순서를 그대로 따른다.

1. **밀도:** 물리 머티리얼이 없으면 1.0 kg/cm³다 (BodyUtils.cpp:37-45). 있으면 `max(9e-8, Density × 0.001)` kg/cm³다. 9e-8은 수소 밀도 0.09 kg/m³다.
2. **체적 적분:** 해석적 프리미티브(Phase 1은 Sphere, Box, Sphyl)로 질량·관성·COM을 구하고 평행축 정리로 합친다.
3. **대각화:** 관성을 주축으로 대각화한다 (BodyUtils.cpp:124).
4. **질량 규칙** (:53-71):
   - `!bOverrideMass`이면 `M' = max(MassScale × M^clamp(RaiseMassToPower, 1e-4, 1), 0.001)`. 머티리얼이 없으면 지수는 0.75다.
   - `bOverrideMass`이면 `M' = max(MassInKgOverride, 0.001)`.
5. **관성 비율 스케일:** 관성에 `M'/M`을 곱한다 (:74-76).
6. **COMNudge:** `COM += COMNudge` (actor 공간, :78-79).
7. **InertiaTensorScale:** `(InertiaTensorScale - 1)`의 어느 성분이든 1e-3보다 클 때만 적용한다 (`IsNearlyZero(1e-3f)`가 거짓일 때). 관성을 actor 공간 행렬로 바꾼 뒤 `ScaleInertia(..., bInertiaScaleIncludeMass = false)`를 부르고 다시 대각화한다 (:90-95; BodyInstance.cpp:3490-3492).
8. **COMNudge 관성 항:** `p.ComNudgeAffectsInertia`(기본 true, :16-17)이면 관성을 다시 actor 공간 행렬로 바꾸고, 대각 성분마다 `I_ii += M' × nudge_i²`를 더한 뒤 다시 대각화한다 (:99-111). 원본 주석이 "ScaleInertia 다음에 와야 한다"고 적고 있다. 일반적인 평행축 공식(`I_xx += M'(y² + z²)` 등)이 아니라 UE 구현 그대로다.

질량 규칙 축(`EMassRule`)은 §10에서 정한다. `Physical`은 4단계의 지수를 1로 둔다.

**M1 구현 메모 [V]**
- 2단계는 UE가 부르는 Chaos 셰이프 적분을 그대로 따른다 (ChaosInterfaceUtils.cpp:137-190, MassProperties.cpp:281-492). 요소마다 `GetFinalScaled`(BodySetup.cpp:2006-2182)로 스케일을 반영하고, half extent와 반지름의 하한 `UE_KINDA_SMALL_NUMBER`도 같다. 체적에는 Chaos처럼 float `UE_PI`(구, 캡슐)와 float `4.0f / 3.0f`(캡슐, Capsule.h:280)가 들어간다. 그래서 반지름 50 cm 구의 체적은 정확한 π로 계산한 값과 상대 2.8e-8 다르다.
- 요소가 하나이고 회전이 항등이면 합성 없이 그대로 쓴다 (CombineWorldSpace). 아무 요소도 질량에 기여하지 않으면 Chaos의 대체값(523 kg, 5.24e5 kg·cm²)을 쓰고 Error 로그를 남긴다. UE는 이 경우 질량 계산 자체를 건너뛰므로 (§5.2 #22) 이 값은 UE 동작이 아니라 결정적인 하네스 대체값이다. 기여 요소의 질량 합이 `UE_SMALL_NUMBER` 이하이면 CombineWorldSpace의 100 cm 물 정육면체를 쓴다. Chaos는 텐서만 바꾸고 0에 가까운 질량을 유지하지만, 하네스는 5단계의 질량 비율이 터지지 않게 정육면체의 1000 kg도 함께 쓴다.
- 지오메트리 스케일은 `FBodyInstance::Scale3D`뿐이다. `FBodySetup::BuildScale3D`는 UE처럼 AggGeom에 이미 구운 값의 기록이라 곱하지 않는다 (§5.2 #25).
- 3·7·8단계의 대각화는 순환 Jacobi다. Chaos `TransformToLocalSpace`처럼 OffDiag²/Trace² < 1e-8이면 대각으로 보고 회전을 바꾸지 않는다 (MassProperties.cpp:17-38). 대각이 아닌 텐서에서는 주축 순서가 Chaos의 닫힌 형식 풀이와 다를 수 있지만 텐서 자체는 같다 (§5.2 #22).
- 1·4·5단계는 double로 계산한다. UE는 float로 계산한다 (§5.2 #22). 1단계의 차이는 1 g/cm³에서 상대 4.7e-8이다.
- 8단계의 `p.ComNudgeAffectsInertia`와 Sensitive sleep 배수 `p.SensitiveSleepThresholdMultiplier`는 하네스 물리 CVar 표(`Physics/PhysicsConsoleVariables.h`)에 있다 (§5.2 #27, §10 M1 메모).

**검증 예 (단위 테스트로 고정)**
- 100 cm 정육면체: 177.83 kg, 296,380 kg·cm²
- 50 cm 정육면체: 37.384 kg
- 10 cm 정육면체: 1 kg
- 단위가 아닌 InertiaTensorScale과 0이 아닌 COMNudge를 함께 준 경우: 6~8단계 순서를 고정하는 테스트. 기대값은 테스트가 문서화된 공식으로 독립 계산한다 (C++ 단위 테스트, M1. M8의 `Tools/ptbench/analytic.py`는 dt_engine 기준값만 다룬다).

## 6. 단일 씬 모델 [MVP M1~M2]

**`FPhysScene`이 소유하는 것**
- 포인터가 안정적인 저장소 (std::deque): `FPhysicalMaterial`, `FBodySetup`, `FBodyInstance`, `FConstraintInstance`
- 백엔드 씬 프록시 `FPhysicsBackendScene` 하나
- 지연 목록: 추가할 액터, 프레임 명령 (힘, kinematic target)
- 통계: `FPhysSceneStats`, `FFrameTiming`
- 디버그: `FPhysicsDebugSnapshot`, `FDebugDrawQueue`
- 카운터: SimTime, FrameCount, StepCount, AccumulatedTime
- 델리게이트

렌더 씬은 따로 없다. 뷰어는 `FBodySetup` 지오메트리, `FBodyInstance::State`, 스냅샷을 직접 읽는다.

**수명 흐름 (UE 5.8.3과 같은 순서)**

```
Scenario::BuildScene(FPhysScene&)
  CreatePhysicalMaterial / CreateBodySetup / SpawnBody(Template, Setup, TM)
    -> FBodyInstance::InitBody -> UpdateMassProperties (harness BodyUtils) -> DeferAddActor
  SpawnConstraint
FlushDeferredActors()                                                   [T_create, recorded]
  CreateMaterials
  CreateActors (one batch, creation order). Inside every backend, UE order:
    1. per body: CreateActor with creation-time flags
                 (kinematic, gravity, CCD, max linear velocity, inertia conditioning, gyroscopic, iteration counts)
    2. per body: geometry -> collision filter -> material
    3. AddActorsToScene_AssumesLocked once        (PhysX: PxScene::addActors with the whole batch)
    4. per body: InitDynamicProperties in UE order
                 mass / inertia / COM (harness values) -> damping -> max angular velocity -> max depenetration
                 -> initial velocity -> sleep threshold -> stabilization threshold -> iteration counts
    5. per body: if !bStartAwake and not kinematic: wake counter 0 + put to sleep
  CreateConstraints
PrepareForSimulation()   the same untimed opportunity for every engine  [T_prepare, recorded]
Tick(DeltaSeconds) ...   (§9)
```

- 근거: UE는 `AddActorsToScene_AssumesLocked`(BodyInstance.cpp:1669) 다음에 바디마다 `InitDynamicProperties_AssumesLocked`를 부른다 (:1695-1706 [V]). 생성 시 플래그는 CreateActor 단계에서 정한다 (:1301-1340 [V]). 4단계의 순서는 그 함수 본문 그대로다 (:4648-4743 [V]).
- 5단계는 UE에서 `InitDynamicProperties_AssumesLocked`의 마지막 부분이다. putToSleep은 `IsInScene && !IsRigidBodyKinematic`일 때만 한다 (BodyInstance.cpp:4723-4733 [V]). PhysX는 씬에 없는 액터의 putToSleep을 오류로 거부한다 (NpRigidDynamic.cpp:527 [V]). 이 순서를 어기면 `bStartAwake=false`가 Chaos에서는 조용히 사라지고 PhysX에서는 실패한다. 게이트 씬 `S08.StartAsleep`이 이를 검증한다 (G6, UEParity에서만).
- **sleep을 끈 프로필(Normalized)에서는 5단계를 모든 엔진에서 건너뛴다.** PhysX는 `eDISABLE_SLEEPING` 씬에서 putToSleep을 `eINVALID_OPERATION` 오류로 무시하므로 (NpRigidDynamic.cpp:521-525 [V]) 그대로 부르면 G12가 실패한다. Jolt에서는 비활성으로 추가된 바디가 sleep을 꺼도 접촉이나 명시적 활성화 전까지 깨어나지 않으므로 [R] 엔진 간 시작 상태가 달라진다. 건너뛴 바디 수는 manifest에 기록한다.
- 질량은 호스트가 미리 계산한다. Chaos와 PhysX는 그 값을 UE와 같이 4단계에서 적용한다. Jolt(`BodyCreationSettings`의 `MassPropertiesOverride`)처럼 생성 시점에 받는 것이 자연스러운 엔진은 1단계에서 넘긴다. 질량 적용 시점은 결과에 영향을 주지 않고, 순서가 결과를 바꾸는 것은 5단계뿐이다.

**여러 씬**
- 뷰어 1단계는 씬 하나를 돌리고, 엔진을 바꾸면 씬을 다시 만든다 [MVP M4]. 2단계는 엔진마다 `FPhysScene`을 하나씩 만들어 lockstep으로 돌린다 [확장 M12].
- 벤치는 프로세스당 씬 하나, 백엔드 하나다.
- 프로세스 제약:
  - **PhysX:** 프로세스당 `PxPhysics` 하나와 `PxTolerancesScale` 하나만 가능하다 (PxPhysics.h:997, PxSceneDesc.h:541 [V]). `UE4Compat`, `UE4Compat-TGS`, `UE4Compat-Stab`은 모두 (100, 1000)을 쓰고 PxSceneDesc만 다르므로 한 프로세스에서 별도 PxScene으로 함께 돌 수 있다. 다른 스케일 (1, 10)을 쓰는 `SdkDefault`만 따로 프로세스가 필요하다.
  - **Bullet:** combine 콜백이 전역이다. 한 프로세스의 Bullet 씬들은 combine 설정이 같아야 한다.
  - **Chaos:** 프로세스당 UE DLL 하나다. Dev와 Test DLL은 함께 두지 않는다.
- 레지스트리는 두 가지 조합만 거부한다: InitGamePhys 옵션이 충돌하는 조합(PhysX tolerance scale)과, 전역 설정이 다른 Bullet 씬 조합(combine 콜백). 나머지 조합은 한 프로세스에 함께 둘 수 있다.

**스레드 규칙**
- 모든 `FPhysScene`, `FBodyInstance`, `FPhysicsInterface` 호출은 GameThread 하나에서만 한다.
- 작업 스레드는 엔진 내부에만 있다.
- 생성(`CreatePhysicalMaterial`, `CreateBodySetup`, `SpawnBody`, `SpawnConstraint`, `DeferAddActor`), `FlushDeferredActors`, `PrepareForSimulation`은 프레임 phase가 Idle일 때만 할 수 있다 (`checkf`). M1 수정 라운드에서 `IsSimulating()` 검사를 phase 검사로 바꿔 `SetUpForFrame`과 `StartFrame` 사이도 막았다. `OnPhysScenePreTick`은 phase SetUp에서 불리므로 그 안에서 바디를 만들 수 없다.

## 7. 백엔드 경계 (C ABI) [MVP M2, 핵심 동결 M6]

### 7.1 규칙

- **형식:** `extern "C"`, POD만, 모든 함수가 `PtResult`를 반환한다. 예외가 경계를 넘지 않는다.
  - CMake 빌드에서는 Kit가 C++ 예외를 잡아 `PT_E_INTERNAL`로 바꾼다.
  - UE 빌드에서는 `PT_ABI_NO_EXCEPTIONS`를 쓴다.
  - SEH는 삼키지 않는다 (덤프를 남긴다).
- **메모리:**
  - 버퍼는 항상 호출자가 소유한다. 공간이 모자라면 `PT_E_CAPACITY`와 필요 개수를 돌려준다 (2회 호출 프로토콜).
  - 힙 소유권은 넘기지 않는다. 모듈이 내보내는 문자열은 정적 문자열뿐이다.
- **핸들:** 씬 핸들은 포인터가 아니라 `PtSceneId`이고, 값은 (generation<<32)|slot이다.
- **단위:** cm, kg, s, rad, Z-up, double.
- **변환 규약:**
  - Box는 half extent로 넘긴다. 호스트가 FKBoxElem의 full extent를 반으로 나누며, 이 지점 하나만 테스트한다.
  - Capsule은 `Radius` + `HalfLength`이고 축은 로컬 Z다.
  - BuildScale3D는 UE처럼 AggGeom에 이미 반영된 값의 기록이다. 호스트는 다시 곱하지 않고 `FBodyInstance::Scale3D`만 적용한다 (§5.2 #25).
  - 엔진별 차이(변형 반복 횟수, contact offset 규칙 등)는 `PtActorCreateInfo`에 쓰지 않는다. 바디 정보에는 UE `FBodyInstance` 값을 쓰고, 엔진별 해석은 `PtSceneDesc.Options`(변형 키)로만 전달한다. 그래야 `desc_sha256`이 엔진 간에 같다 (§12.3).
- **비동기 dt:** `PtSceneDesc.AsyncFixedTimeStepSize`는 sync이면 반드시 -1이다. Chaos는 `AsyncDt >= 0`을 async로 취급하므로 (PhysicsSolverBase.h:712-715 [V]) Kit는 0을 `PT_E_INVALID_ARG`로 거부한다.
- **스레드 규칙:** 모든 호출은 `InitGamePhys`를 부른 스레드에서 한다. 다른 스레드에서 부르면 부작용 없이 `PT_E_WRONG_THREAD`를 돌려준다. 예외는 로그 콜백 하나뿐이다.
- **씬 phase 상태기계:**
  - `Idle -> SetUpForFrame -> SetUp -> StartFrame -> Started -> WaitPhysScenes -> Waited -> EndFrame -> Idle`
  - 생성, 쓰기, 읽기는 `Idle`에서만 할 수 있다. 어기면 `PT_E_BAD_STATE`.
- **모듈 상태기계:** `Loaded -> Ready -> Terminated`. NO_REINIT 플래그가 있으면 재초기화를 거부한다.
- **수명:**
  - 한 번 로드하면 FreeLibrary하지 않는다.
  - Init 뒤에 Term. `PT_MODULE_NO_REINIT`이 없으면 Term 뒤에 다시 Init할 수 있다 (A0가 Null을 다시 초기화한다). 있으면 두 번째 Init은 `PT_E_BAD_STATE`다.
  - UE_RUNTIME 모듈은 프로세스당 최대 1개.

### 7.2 함수 테이블 (`PtBackendApi`)

"동결" 열의 "핵심"은 M6에 v1.0으로 동결한다는 뜻이고, "잠정"은 M9 종료 때 확정한다는 뜻이다 (§7.6).

| # | Entry | 상태 | 버킷 | 의미 | Chaos 매핑 (사용자 구현) | 동결 |
|---|---|---|---|---|---|---|
| 1 | GetInfo | 항상 | - | capability, module flags, `BuildFlagsJson`(어댑터 TU 자기보고), `RequiredEffectiveKeys`(백엔드 전용 필수 키) | 정적 문자열. Dev와 Test의 capability가 다르다 | 핵심 |
| 2 | InitGamePhys | Loaded | 기록만 | 스레드 예산, 로그 콜백, cmdline, SavedDir, 모듈 옵션 (예: `physx.ToleranceScale`) | §15.3 순서 | 핵심 |
| 3 | TermGamePhys | 종료 시 | - | | RequestEngineExit → AppPreExit → UnloadModulesAtShutdown → AppExit. 벤치는 그 뒤 프로세스를 직접 끝낸다 (§12.2) | 핵심 |
| 4 | CreateScene | Ready | T_create | 설정만 담은 월드 | `new FChaosScene(nullptr, AsyncDt)` (sync는 -1) → `SetThreadingMode_External` (§9.6) → `ApplyConfig(SolverOptions)` + `SetIsDeterministic` (§15.4) | 핵심 |
| 5 | DestroyScene | Idle | - | | ReleaseActor ×N | 핵심 |
| 6 | CreateMaterials | Idle | T_create | 원래 F, SF, R과 combine 모드 | FPhysicalMaterialManager Create + UpdateMaterial | 핵심 |
| 7 | CreateActors | Idle | T_create | 배치 하나, 하네스 순서. 백엔드 내부는 §6의 UE 순서 | 바디별 `CreateActor`(생성 시 플래그) → `SetGeometry` → filter → material, `AddActorsToScene_AssumesLocked` 1회, 바디별 InitDynamicProperties 순서, `!bStartAwake`이면 `SetWakeCounter(0)` + `PutToSleep` (sleep을 끈 프로필은 제외, §6) | 핵심 |
| 8 | ReleaseActors | Idle | - | | ReleaseActor | 핵심 |
| 9 | CreateConstraints | Idle | T_create | ball/hinge | CreateConstraint + 조인트 설정 | 잠정 (M9) |
| 10 | PrepareForSimulation | Idle | T_prepare | 비타이밍, 수행 내용을 기록 | 사용자 정의 (기록) | 핵심 |
| 11 | SetUpForFrame | Idle→SetUp | T_step | UE 시그니처 그대로 | `FChaosScene::SetUpForFrame` 1:1 | 핵심 |
| 12 | StartFrame | →Started | T_step | | StartFrame | 핵심 |
| 13 | WaitPhysScenes | →Waited | T_step | | WaitPhysScenes | 핵심 |
| 14 | EndFrame | →Idle | T_step | `PtFrameResult{NumStepsTaken, EngineErrorBits, NumPushedSteps, Reserved}` (v1.0, 16 B) | EndFrame (pull, SQ copy, event flip). `NumPushedSteps` = `AdvanceAndDispatch_External`이 push한 스텝 수 | 핵심 |
| 15 | ApplyBodyCommands | Idle | T_push | `*_AssumesLocked` 배치 | SetGlobalPose_ / SetKinematicTarget_ / AddForce_ / AddImpulse_ / AddVelocity_ … | 핵심 |
| 16 | ReadBodyStates | Idle | T_pull | ALL 또는 ACTIVE_ONLY | GT API X/R/V/W/ObjectState | 핵심 |
| 17 | ReadMassProperties | Idle | 비타이밍 | G2 readback | M / I / CenterOfMass / RotationOfMass | 핵심 |
| 18 | GetStepStats | Idle | 비타이밍 | 카운터, PhaseTimeNs. 리스너가 필요 없는 값만 헤드라인 run에서 읽을 수 있다 | sim-callback solver time | 핵심 |
| 19 | CaptureDebugSnapshot | Idle | 비타이밍 | 2단계 중 1단계: 개수 | particles, collision constraints, FChaosDDScene (Dev) | 잠정 (M9) |
| 20 | ReadDebugSnapshot | 19 이후 | 비타이밍 | 2단계 중 2단계: 복사. 조인트 스냅샷과 네이티브 드로(`PtDebugDrawCmd`)를 포함 | | 잠정 (M9) |
| 21 | ComputeNativeStateHash | Idle | 비타이밍 | FNV-1a, 하네스 순서 | X/R double | 핵심 |
| 22 | SetOption | Ready/Idle | - | key/value (scene 0 = 모듈) | IConsoleVariable::Set | 핵심 |
| 23 | DumpEffectiveSettings | Ready/Idle | 비타이밍 | JSON lines {key, native, native_unit, canonical, unit, source} | CVars + SolverOptions + PtChaosDefaults | 핵심 |
| 24 | GetMemoryStats | Ready (씬은 Idle) | 비타이밍 | 모듈 수준(0)도 Ready에서만 | 프로세스 수준만 (FMalloc) | 핵심 |
| 25 | GetLastErrorMessage | 항상 | - | | | 핵심 |

### 7.3 Kit (`PtBackendKit.h`, header-only)

**제공하는 것**
- 백엔드는 `PhysicsTest::Abi::IPhysicsBackend` / `IPhysicsBackendScene`을 구현한 뒤 `PT_IMPLEMENT_BACKEND_MODULE(Class)`를 쓴다. DLL은 export하고, static은 `PT_IMPLEMENT_BACKEND_MODULE_STATIC(Class, PT_GetBackendApi_X)`를 쓴다.
- 나머지는 `TBackendApiProvider<T>`가 만든다:
  - C 썽크
  - 스레드 가드
  - phase 상태기계
  - 16-slot 핸들 테이블
  - 입력 검증: 경계, 유한값, 단위 쿼터니언 1e-6, 인덱스 범위, dynamic 질량 > 0, `AsyncFixedTimeStepSize`가 -1 또는 양수, 알 수 없는 옵션 키 거부
  - 마지막 오류 텍스트와 로그 콜백 상태. 템플릿의 static 멤버이므로 백엔드 클래스마다 따로 있다
  - `FTextWriter`, stride 뷰

**ODR과 링크 규칙**
- Kit와 BackendCommon의 모든 정의는 `namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS { ... } }` 안에 둔다. `PT_BACKEND_NS`는 백엔드 타깃마다 PRIVATE 정의로 준다 (Null, Jolt, Bullet, PhysX; UBT 브리지는 `ChaosBridge`).
  - 백엔드가 쓰는 다른 namespace 이름은 쓰지 않는다. `Chaos`로 하면 `PhysicsTest::Abi::Chaos`가 생겨, 브리지 TU에서 `using namespace PhysicsTest::Abi;` 뒤의 모든 `Chaos::` 참조가 UE `::Chaos`와 모호해진다 (M2 수정 라운드).
  - 이유: 헤더 전용 비템플릿 inline 함수가 /arch가 다른 TU(Jolt는 AVX2, 나머지는 SSE2)에 함께 컴파일되면 링커가 COMDAT 하나만 남긴다. 그러면 Bullet 백엔드가 Jolt TU에서 AVX2로 컴파일된 코드를 실행할 수 있고, `test`와 `test-bullet`의 결과가 달라진다.
- **어댑터의 STL과 CRT inline 함수 (M3 수정 라운드 0):** 같은 문제가 namespace 밖에서도 생긴다. `/arch`가 다른 어댑터 TU가 `std::vector<uint32>` 같은 하네스와 같은 인스턴스를 만들면 링커가 어댑터의 AVX2 사본을 골라 하네스(Shipped, `/arch` 없음)가 그것을 실행할 수 있다 (M3 검사에서 `vector<uint32>::push_back`, `vector<PtContactPoint>` 등이 `PhysScene_Jolt.cpp.obj`에서 왔다). 규칙:
  - 어댑터의 컨테이너는 백엔드에만 있는 타입으로 쓴다. Jolt는 `JPH::Array<T>`(할당자 `JPH::STLAllocator`), 그 밖의 SDK는 그 SDK의 컨테이너나 백엔드 고유 할당자를 쓴 `std::vector<T, FBackendAllocator<T>>`다. `std::` 컨테이너를 기본 할당자로 쓰지 않는다.
  - `/arch`가 다른 어댑터 타깃에는 `_NO_CRT_STDIO_INLINE`(UCRT의 inline `snprintf` 등을 어댑터 오브젝트에 만들지 않는다)과 `legacy_stdio_definitions` 링크를 준다.
  - CTest `PhysicsTest.Probe.AdapterComdats`가 Automation·Bench 링커 맵에서 어댑터 오브젝트가 내놓은 함수가 모두 백엔드 소유(백엔드 namespace, SDK namespace, lambda, 진입점, 컴파일러가 만드는 sized-delete 도우미)인지 확인한다. SDK 자신의 TU가 내놓는 std 함수(Jolt: `steady_clock::now`, `this_thread::sleep_for`, `std::terminate`, `::abs(float)`)는 정확한 이름 목록으로만 허용한다 (SDK 패치 없음, Q8). Debug 프리셋은 /Od가 SDK 헤더의 std 도우미를 모두 밖으로 꺼내므로 보고만 한다.
- 백엔드가 하네스 Core 타입을 쓰지 않도록 BackendCommon이 고정 폭 typedef(`int32`, `uint8` 등)를 제공한다.
- 기본 virtual 구현은 쓰지 않는 매개변수에 이름을 붙이지 않는다 (예: `CreateConstraints(TStridedView<PtConstraintCreateInfo> /*Infos*/)`). /W4 /WX에서 C4100이 오류가 되기 때문이다 (MSVC 14.44로 재현 [V]).

**제약**
- 인터페이스에 STL을 쓰지 않는다.
- RTTI와 예외에 의존하지 않는다.
- `check/verify/ensure/min/max` 같은 식별자를 쓰지 않는다.
- 스레드 ID 기본값은 `__readgsdword(0x48)` (TEB). UE에서는 `FPlatformTLS::GetCurrentThreadId()`로 바꾼다.
- `PtAbiUeCompatCheck` 타깃이 UE와 같은 플래그(/GR- /EHs-c- _HAS_EXCEPTIONS=0 /W4 /WX)로 매 빌드 컴파일된다. 이 타깃은 아무 동작도 하지 않는 백엔드 하나를 인스턴스화해 기본 virtual 구현까지 실제로 컴파일되게 한다.

### 7.4 호스트 측

- **`FPhysicsBackendRegistry`** (≈FModuleManager)
  - 생성된 static 목록과 Chaos DLL 경로를 가진다. DLL 경로는 `PT_CHAOS_DLL_DIR` 또는 `-ChaosDll=`로 정한다.
  - UE_RUNTIME 모듈은 하나만 허용한다.
- **`FPhysicsBackendModule`**
  - `LoadLibraryExW(abs, LOAD_WITH_ALTERED_SEARCH_PATH)`로 로드한다.
  - 호스트는 자신이 빌드된 메이저 버전 `PT_ABI_VERSION_MAJOR`로 `PT_GetBackendApi`를 호출한다. 그다음 StructSize를 확인하고, 로드 후 호스트 예외 필터를 재설치한다.
  - 해제하지 않는다.
- **`FPhysicsBackendScene`**
  - `FBodyInstance`를 Pt 구조체로 마샬링한다.
  - 타이밍 경로에서는 함수 포인터 직접 호출만 하고 할당이 없다.
- **`FPhysicsInterface`**
  - 쓰기는 명령 버퍼에 쌓았다가 T_push에서 한 번에 플러시한다.
  - 읽기(`GetGlobalPose_AssumesLocked` 등)를 하기 전에 버퍼를 먼저 플러시한다.

### 7.5 capability와 module flag

| 항목 | Null | Jolt | PhysX | Bullet | Chaos (예상) |
|---|---|---|---|---|---|
| CONTACT_POINTS | - | viz | viz | viz (캡처 때 매니폴드를 읽음) | O |
| CONTACT_NORMAL_IMPULSE / FRICTION_IMPULSE | - | -/- | -/- (M5: 점별 impulse는 ACTIVE 판정에만 쓰고 값은 NaN. 값은 M9) | O/O | O/? |
| CONTACT_ACTIVE_EXACT | - | - (근사) | O | O | O |
| NATIVE_ISLANDS | - | - (내부 API `MotionProperties::GetIslandIndexInternal()`뿐이고 스텝 단위, MotionProperties.h:258 [V]. 하네스 `FIslandBuilder`를 쓴다) | - | O (`getIslandTag`) | O |
| NATIVE_FRAME_MODEL | **O** (FStepPlanner 의미, §7.7) | - | - | - | O |
| NATIVE_DEBUG_DRAW | - | - | M9 (M5는 선언하지 않음) | M9: viz run에만 드로어를 붙이고, 드로어의 `DBG_NoDeactivation`을 프로필의 sleep 설정과 맞춘다 (btDiscreteDynamicsWorld.cpp:418-421 [V]). M5는 드로어를 붙이지 않는다 | Dev만 |
| PHASE_TIMERS | - | - | - | - | O |
| MULTITHREADED | - | O | O | - | O |

**module flag**
- Chaos: ONE_PER_PROCESS, NO_REINIT, UE_RUNTIME, SPAWNS_THREADS_AT_INIT. Dev 빌드는 DEBUG_DRAW_COMPILED도 켠다.
- Bullet: GLOBAL_SCENE_SETTINGS (전역 combine 콜백). M5: 전역값은 모듈 init 옵션 `[PhysicsTest.Backend.Bullet.Init]` `CombineRule`(UE/Bullet), `DeactivationTime`, `ContactBreakingThreshold`이고, `Bullet.Default.ini`는 `.Init` 섹션을 두지 않는다 (변형 없는 Bullet과 init 옵션이 같아야 한 프로세스를 쓴다).
- PhysX: GLOBAL_SCENE_SETTINGS (프로세스당 PxPhysics 하나와 tolerance scale 하나). 같은 scale의 변형들은 한 프로세스에 함께 둘 수 있다 (§6). M5: 레지스트리가 init 옵션을 문자열로 비교하므로 UE4Compat 계열은 init 키를 적지 않고(백엔드 기본값이 (100, 1000)), SdkDefault만 `ToleranceScale=(Length=1,Speed=10)`을 적는다. 그래서 변형 없는 PhysX와 UE4Compat·-TGS·-Stab이 한 프로세스를 쓰고 SdkDefault는 RestartRequired다.

### 7.6 버전과 동결

- `PT_GetBackendApi(RequestedMajor)`는 지원하지 않는 메이저 버전이면 NULL을 돌려준다. 호스트는 항상 자신의 `PT_ABI_VERSION_MAJOR`를 요청한다.
- 버전 단계:
  - **M6 이전:** v0.9, 메이저 0. M2.5 스파이크 DLL도 메이저 0으로 빌드한다.
  - **M6 종료:** 핵심 v1.0을 동결하고 `abi-v1.0` 태그를 단다. 조건은 다음과 같다.
    - SDK 3종과 Null이 MVP 게이트(A0~A7, A9, A10, G1~G7, G9, G12)를 통과한다.
    - Null이 `NATIVE_FRAME_MODEL`로 Native driver와 `NumStepsTaken` 계약을 미리 통과한다 (A10). 실제 엔진으로 처음 이 계약을 쓰는 것은 Chaos(M13)이기 때문이다.
    - M2.5 스파이크 결과가 있으면 함께 반영한다.
  - **잠정 항목 (M9까지):** 조인트(`CreateConstraints`, `PtConstraintCreateInfo`), 디버그 스냅샷(`CaptureDebugSnapshot`/`ReadDebugSnapshot`, 조인트 스냅샷), 네이티브 드로(`PT_SNAP_NATIVE_DRAW`, `PtDebugDrawCmd`). 세 엔진의 조인트가 M9에 처음 이 경로를 쓰므로 그때 확정한다. 그 사이 변경은 minor 버전과 StructSize로 기록한다.
  - **동결 이후 v1.x:** append-only다. StructSize/Stride 뒤에 필드 추가, 테이블 끝에 함수 추가, 새 bit와 enum만 허용한다.
- 호환을 깨는 변경은 메이저 버전을 올리고 Chaos를 재빌드해야 한다.
- **M6 결과 [V]:** 핵심을 v1.0으로 동결했다 (`PT_ABI_VERSION_MAJOR 1`, `MINOR 0`. `abi-v1.0` 태그는 리드가 단다). 동결 때 정한 계약은 [Abi.md](Abi.md) §9.1이다: 속도·상태 쓰기는 잠든 바디를 깨운다(`PT_CMDF_AUTO_WAKE`는 정보용, 0이 아니지만 nearly zero인 속도만 백엔드 정의, G6가 모든 백엔드에서 확인), 스냅샷 접촉 flag ACTIVE / ACTIVE_APPROX / SPECULATIVE는 배타적(G3), PointOn0/1은 각 셰이프 표면 위, `PtFrameResult`는 16 B(`NumPushedSteps` 추가), 엔진 오류 bit의 뜻은 백엔드가 정하고 용량 bit는 `errors.capacity_bits`, `PT_CMDF_ALLOW_SUBSTEPPING`과 `PT_E_VERSION`/`PT_E_TRANSPORT`는 남김, NO_REINIT 백엔드의 A 게이트는 `PhysicsTestBench -Conformance -Gates=Ax`로 게이트당 프로세스 하나, capability 부족의 게이트 Skip은 `tolerances.json`에 등록된 엔진(Null)만 통과. M2.5 스파이크는 하지 않아 반영할 목록이 없다. 동결 조건(SDK 3종과 Null이 A0~A7, A9, A10, G1~G7, G9, G12 통과)은 [Progress.md](Progress.md) M6 §2에 있다.

### 7.7 Null 백엔드 [MVP M2], loopback과 OOP [확장 M14]

- **Null 백엔드:**
  - SDK 없이 semi-implicit Euler와 UE damping만으로 동작한다. static lib과 `PtBackend_Null.dll` 두 형태로 빌드한다.
  - `PT_CAP_NATIVE_FRAME_MODEL`을 선언한다. `SetUpForFrame`이 UE 인자 전체를 받고, UE5 `FChaosScene` / `PhysicsSolverBase` 규칙(Sync clamp, Substepping, AsyncFixed 누산기)으로 스텝을 직접 계획한 뒤 `NumStepsTaken`을 돌려준다. 프레임 안의 명령도 UE5 규칙이다: 힘은 push 프레임의 모든 스텝(push가 없는 프레임은 넘겨서 평균), 임펄스는 첫 스텝, kinematic 목표는 `PseudoFraction` (§5.2 #38~#40, [Abi.md](Abi.md) §5.10).
  - DLL은 C++ 라이브러리 DLL(`msvcp140*`)을 import하지 않는다. A9의 "CRT"는 SDK 백엔드에 한해 `msvcp140*`를 포함하고, Null은 `check_modules.py --strict-crt`로 이를 막는다.
    - Null은 하네스 lib를 링크할 수 없으므로(R4) 이 계획 코드는 하네스 `FStepPlanner`와 따로 작성한다. 그래서 A10은 독립된 두 구현을 서로 대조하는 검사가 된다.
  - 쓰임:
    - Chaos 작성자의 템플릿
    - ABI 오버헤드 측정 (MVP는 QPC 배치 측정, M10에 TSC)
    - DLL 경로 시험
    - Native driver 계약의 사전 검증
- **loopback [확장 M14]:** `PtAbiCodec`이 모든 호출을 arena로 인코드한 뒤 같은 스레드에서 디스패치한다. 전체 게이트 스위트를 loopback으로 돌려 직접 호출과 비트 동일한지 확인한다 (A8). 실제 소비자가 OOP뿐이므로 OOP와 함께 만든다.
- **OOP [확장 M14, 조건부]:**
  - 구성: 같은 codec + shared memory mailbox (`Local\PT.<pid>.<nonce>`) + frame ring.
  - 서버 `RunBackendServer`는 메인 스레드에서 호출을 실행한다.
  - 타이밍은 서버 안에서 같은 측정 루프로 잰다. IPC는 타이밍 구간에 넣지 않는다.
  - SP2 또는 SP3가 실패하거나 사용자가 요청할 때만 만든다.

### 7.8 M2 구현 메모 [V]

규칙의 정본은 [Abi.md](Abi.md)다. 설계(§7.1~7.7) 위에 M2에서 정한 것:
- **텍스트 버퍼:** `PtTextBuffer::Length`는 NUL을 포함한 필요 바이트 수다. Kit은 `DumpEffectiveSettings`를 한 번 측정한 뒤 들어갈 때만 다시 써서 `PT_E_CAPACITY`이면 아무것도 쓰지 않는다 (구현은 결정적이어야 한다). `GetLastErrorMessage`와 한 번만 하는 `PrepareForSimulation`은 잘라서 쓰고 `PT_W_TRUNCATED`다.
- **마지막 오류:** 실패한 호출은 모두 메시지를 남긴다 (백엔드가 안 남기면 Kit이 남긴다). `PT_E_WRONG_THREAD`와 `GetLastErrorMessage` 자신은 바꾸지 않는다.
- **스레드:** 소유 스레드는 `InitGamePhys`를 부른 스레드이고 `TermGamePhys`가 지운다. 그 전후에는 어느 스레드든 부를 수 있지만 동작하는 것은 `GetInfo`, `InitGamePhys`, `GetLastErrorMessage`뿐이고 나머지(모듈 수준 `SetOption(0)`, `DumpEffectiveSettings(0)`, `GetMemoryStats(0)` 포함)는 `PT_E_BAD_STATE`다. 소유 스레드 id는 relaxed atomic 32-bit 접근으로 읽고 쓴다.
- **TermGamePhys:** 살아 있는 씬을 phase와 무관하게 모두 파괴한 뒤 종료한다.
- **`NumStepsTaken`:** 엔진이 실제로 진행한 스텝 수 = `FStepPlan::NumSteps` (밀어 넣은 스텝 수 `NumPushedSteps`가 아니다). §9.2.
- **Harness driver와 AsyncDt:** Harness driver로 도는 씬은 항상 `AsyncFixedTimeStepSize = -1`로 만든다. async 누산기는 하네스 계획이 돌린다. `AsyncDt > 0`은 `NATIVE_FRAME_MODEL` 백엔드에만 허용된다 (그 밖은 `PT_E_UNSUPPORTED`).
- **`PT_READ_ACTIVE_ONLY`:** 이전 읽기 뒤 상태가 바뀌었을 수 있는 살아 있는 바디 전부(깨어 있는 dynamic, 움직인 kinematic, ObjectState가 바뀐 바디). 잠든 바디가 빠지면 호스트 상태가 낡는다. 부분 읽기라서 `OutIndices`가 필요하다 (Capacity > 0이면 NULL 거부). `PT_READ_ALL`은 만든 바디 전부를 인덱스 순서로 빽빽하게 돌려주고, 해제한 바디는 ObjectState UNINITIALIZED와 0 상태로 낸다 (Kit이 개수를 확인한다, M2 수정 라운드 1).
- **해제한 바디:** Kit이 해제 집합(바디당 1 bit, malloc)을 들고 `ReleaseActors`의 중복·이미 해제한 인덱스, 해제한 바디에 대한 명령과 `ReadMassProperties`를 백엔드와 무관하게 거부한다 (배치는 전부 아니면 전무). 백엔드는 `IsReleased()`로 볼 수 있다 (M2 수정 라운드 1).
- **MXCSR:** Kit 썽크가 제어 bit(6~15)를 보존하고 바뀌면 되돌린다(첫 번만 경고, 횟수는 모듈 수준 effective 키 `kit.mxcsr_restores`). A6는 그 경고나 횟수 증가를 실패로 본다. x87 제어 워드는 A6가 확인만 한다.
- **입력 구조체 StructSize:** 이 헤더의 sizeof보다 작으면 거부, 크면 받는다. 배열 stride는 sizeof 이상이고 정렬되어야 한다.
- **레지스트리 이름:** `Null`(static), `NullDll`(exe 옆 `PtBackend_Null.dll`). Chaos는 `-ChaosDll=<파일>` 또는 환경 변수 `PT_CHAOS_DLL`(§7.4의 `PT_CHAOS_DLL_DIR` 대신 파일 경로, 파일이 있을 때만 목록에 나온다). DLL을 로드한 직후 호스트의 unhandled-exception filter와 error mode를 되돌리고, 바뀌었으면 경고한다.
- **UE4 substep 규칙:** `ESubstepClampRule::UE4PhysSubstep`은 하네스 플래너에만 있다. `FrameDriver=Auto`이면 이 규칙의 Substepping은 Harness driver로 돌고, Native를 강제하면 Fatal이다. 프레임 안의 힘과 kinematic 목표도 이 규칙일 때만 UE4 방식이다 (§9.1, `FStepPlan::ClampRule`).
- **힘과 kinematic 목표의 진입점:** `FBodyInstance::AddForce`는 UE5처럼 `FPhysicsInterface::AddForce_AssumesLocked`를 부르고, 이것이 소유 씬의 `FPhysScene::AddForce_AssumesLocked`로 간다. kinematic 목표도 `SetBodyTransform`·`FPhysicsInterface::SetKinematicTarget_AssumesLocked`가 `FPhysScene::SetKinematicTarget_AssumesLocked`로 간다. 모든 진입점이 같은 프레임 규칙을 탄다 (§5.2 #42).
- **ABI 호출 비용 (Null, 바디 0, 프레임당 4호출, QPC 배치 평균):** dev·test 10.0 ns/호출, dbg 약 50 ns (static과 DLL 같음). 게이트가 아니다 (TSC 기반 100 ns 게이트는 M10).

M3에서 더한 것 (정본은 [Abi.md](Abi.md) §4, §5.5, §5.7, §7.1):
- **`PT_READ_ALL`과 `OutIndices`:** NULL이 아니면 Kit이 0..N−1을 써 준다. 호스트는 두 모드를 같은 코드로 읽는다.
- **배치의 원자성:** 검증은 배치 전체를 먼저 한다 (하나라도 틀리면 아무것도 적용하지 않음). 검증을 통과한 뒤 백엔드 적용이 실패하면 Kit이 `PT_E_INTERNAL`과 "앞의 k개는 적용됨"을 남긴다. 그 뒤 씬 상태는 정의되지 않으므로 호스트는 Fatal로 끝낸다.
- **SDK 백엔드의 계약:** Harness driver로만 돈다 (`NATIVE_FRAME_MODEL` 없음). `StartFrame` 하나가 엔진 스텝 하나이고 `NumStepsTaken`은 0 또는 1이다. kinematic 목표는 그 스텝의 `MoveKinematic`, 힘은 그 스텝 동안 상수다 (Abi.md §7.1).

## 8. 백엔드별 내부 구조

### 8.1 공통 (BackendCommon) [MVP M2~M5]

- **`PtUnitScale`:** L은 cm당 네이티브 길이다. Chaos와 PhysX-UE4Compat은 1, Jolt·Bullet·PhysX-SdkDefault는 0.01 (D1). M5: 이 표는 `EEngineUnits` + constexpr `GetEngineUnitScale`이고, Jolt·Bullet은 이 값으로만 변환한다. PhysX는 변형 키 `LengthScale`을 쓰고 effective 키가 표의 어느 칸인지 적는다 (CreateScene이 PxTolerancesScale과의 일치를 검사한다, §8.2 M5 메모).
  - ×L: 길이, 속도, 가속도, 힘, 선형 임펄스
  - ×L²: 관성, 토크, 각임펄스
  - ×1: 질량, 각도, damping
- **캡슐 축 재매핑:**
  - UE Z → PhysX X (`U2PSphylBasis` = BackendCommon `CapsuleAxisRemapUE4SphylBasis()`, 180° about (1,0,1)/√2), Jolt Y (`RotatedTranslatedShape`).
  - Bullet은 `btCapsuleShapeZ`(btCapsuleShape.h:128-131 [V])를 써서 재매핑하지 않는다. 이것은 **D1 보정**이다. D1 원안은 "Bullet Y"(기본 `btCapsuleShape`)였다. 물리적으로 같고 변환 지점이 하나 줄어든다. 결정 로그에 기록하며 `S03.CapsuleRest` 게이트로 확인한다.
- **COM 래핑은 필요할 때만 한다:**
  - 요소가 하나이고 `ComLocal`이 항등에 가까우면(|t| < 1e-6 cm, |q·identity| > 1 − 1e-9) 네이티브 프리미티브를 그대로 쓴다 (`btBoxShape`, Jolt `BoxShape` 등).
  - 그 밖의 경우에만 compound(Bullet)나 `OffsetCenterOfMassShape`(Jolt)를 쓴다.
  - 이유: 헤드라인 씬(`M3.BoxPile`, `M2b.Pyramid1240`)은 COM이 중심인 균일 큐브다. 모든 바디를 감싸면 compound 디스패치와 자식 변환 비용이 Bullet·Jolt에만 붙는다.
  - effective 키 `shape.wrapping`에 종류별 개수(compound, local_transform, offset_com, axis_remap)를 기록한다. `M3.BoxPile`은 0이어야 한다 (G9). M5: compound만 있는 엔진(Bullet)은 `ClassifyCompoundWrapping`이 종류를 정한다. 래퍼 객체가 없는 PhysX는 같은 조건을 엔진의 actor에서 읽는다 (§8.2 M5 메모).
- **접촉 정규화 (Chaos 규약):**
  - normal은 Body1 위에 있고 Body0을 향한다. Phi < 0이면 관통 (ContactPoint.h:49-53 [V]).
  - Body0은 더 가동성이 큰 쪽이다 (Dynamic > Kinematic > Static, 동률이면 작은 index).
  - Jolt는 normal과 depth 부호를 반전한다. Bullet 매니폴드 점은 이미 Chaos 부호다 (`m_normalWorldOnB`는 B 위에서 A를 향하고 `getDistance() < 0`이 관통, btManifoldPoint.h [V]. A → Body0, B → Body1). PhysX도 반전하지 않는다: `PxContactPairPoint`의 normal은 두 번째 셰이프에서 첫 번째 셰이프를 향하므로(PxSimulationEventCallback.h:448 [V]) shape 1 → Body1, shape 0 → Body0으로 그대로 Chaos 규약이고, Phi = separation이다 (M5 수정 라운드 0에서 문서 정정. 코드는 처음부터 반전하지 않았다, `SdkBackend.CheckpointAConventions` min normal.z = 1).
  - **PointOn0/PointOn1 (ABI v1.0 계약, [D]):** PointOn0은 Body0 셰이프 표면, PointOn1은 Body1 셰이프 표면 위의 점이고 (PointOn0 − PointOn1)·n = Phi다 ([Abi.md](Abi.md) §9.1 결정 3). 자세는 그 스텝의 narrow phase 자세다: Jolt·Bullet·PhysX는 스텝 시작 자세, Chaos는 `Integrate` 뒤의 예측 자세이고 G3가 둘 다 받는다 (M6 수정 라운드 2). Jolt와 Bullet은 매니폴드의 두 점을 그대로 쓴다. PhysX의 `position`은 어느 표면 위의 점도 아니다 (PCM 캡슐·구 경로는 narrow phase 셰이프 A 위, 비 PCM 구-구는 중점, 보고 순서가 narrow phase 순서와 다르면 normal만 뒤집힌다. M5 검토 지적). M5의 "PointOn1 = position, PointOn0 = position + separation·n"은 같은 형의 박스·convex PCM 쌍에서만 맞았다. M6부터 어댑터가 스텝 전 자세의 Body1 셰이프에 `PxGeometryQuery::pointDistance`로 투영해 PointOn1을 구하고 PointOn0 = PointOn1 + Phi·n으로 둔다 (§8.2 M6 메모). `SdkBackend.ContactPointsOnSurfaces`(캡슐-박스, 박스-static 구, 구-static 박스)와 G3가 확인한다.
  - impulse를 얻을 수 없으면 NaN으로 둔다. 가동성 순위는 `GetMobilityRank(PT_MOTION_*)`.
- **damping:** Chaos, Jolt, PhysX는 `v *= max(0, 1 - c·dt)`를 힘 적분 **뒤에** 한다 (PhysX DyBodyCoreIntegrator.h:26-42 [V]). Bullet의 `(1 - c_b)^dt` 배수는 `c_b = 1 - (1 - c·dt)^(1/dt)`로 맞출 수 있지만 순서가 다르다: `predictUnconstraintMotion`에서 감쇠한 뒤 솔버가 힘 임펄스를 더한다 (btDiscreteDynamicsWorld.cpp:1091-1105, btSequentialImpulseConstraintSolver.cpp:477-478, 1820-1826 [V]). 그래서 Bullet 백엔드는 기본으로 Bullet 감쇠를 0으로 두고 내부 pre-tick이 `UEOrderPreSolveVelocity`(v·m − a·dt·(1 − m))를 넣는다 (`DampingModel=UE`). 변환만 쓰는 `DampingModel=Converted`도 남겼다 (M5 [V]: 차이는 N 스텝 뒤 g·dt·(1 − m^N)).
- **SDK 헤더 (백엔드 측):** 백엔드 Private TU에서 `PT_THIRD_PARTY_INCLUDES_START/END`(warning push/pop만 함) 안에서만 include한다. 백엔드는 하네스 Core를 include하지 않으므로 check/ensure 매크로와 충돌하지 않는다.
- **third-party 헤더 (하네스 측):** 하네스 TU는 Core의 `check`/`verify`/`ensure`/`checkf` 매크로를 갖고 있고, doctest는 `check()`라는 멤버 함수를 선언한다. 매크로로는 `#undef`를 만들 수 없으므로 include 파일 쌍으로 처리한다.
  - `Misc/PtThirdPartyIncludesStart.h`: warning push → `#pragma push_macro("check")` 등 → `#undef`
  - `Misc/PtThirdPartyIncludesEnd.h`: `#pragma pop_macro(...)` → warning pop
  - doctest, ImGui, ImPlot, nlohmann/json은 반드시 이 쌍을 거쳐 include한다. CTest 컴파일 프로브가 확인한다 (§4.5).
- **중력:** SDK에서는 값이 바뀔 때만 설정한다. Bullet `setGravity`가 O(N)이기 때문이다. Chaos는 UE처럼 매 프레임 받는다. M5 [V]: `btDiscreteDynamicsWorld::setGravity`는 깨어 있는 바디만 바꾸므로(btDiscreteDynamicsWorld.cpp:499-510) Bullet 백엔드는 잠든 dynamic 바디의 중력도 직접 바꾼다.
- **박스 마진 기록:** 엔진마다 규칙이 다르므로 셰이프별로 readback해 `shape.box_margin`에 요약(min, max)한다.

| 엔진 | 박스 마진 규칙 | 50 cm 큐브 | 1 m 큐브 |
|---|---|---|---|
| Chaos | `min(2 × minHalf × CollisionMarginFraction(0.05), CollisionMarginMax(10 cm))` (ChaosInterfaceUtils.cpp:156 [V], Engine 모듈) | 2.5 cm | 5 cm |
| Bullet | `setSafeMargin(halfExtents)` → `min(0.04 m, 0.1 × minHalfExtent)`, 셰이프 안쪽 (btBoxShape.cpp:25, btConvexInternalShape.h:66-81 [V]). `getMargin()`으로 읽는다 | 2.5 cm | 4 cm |
| Jolt | convex radius 기본 0.05 m (PhysicsSettings.h:16 `cDefaultConvexRadius` [V]), `Jolt.SharpBoxes`는 0 | 5 cm | 5 cm |
| PhysX | 정확한 박스 + contactOffset (§8.2) | - | - |

### 8.2 PhysX 5.11 — UE4 방식 (클린룸) [MVP M5]

**구성**

| UE4 조각 | 하네스 파일·타입 | 5.11 구현 |
|---|---|---|
| PhysXIncludes.h | `PhysXIncludes.h` | `PxPhysicsAPI.h`를 유일하게 include하는 파일 |
| PhysXSupport | `U2PVector/P2UVector/U2PQuat/U2PTransform/P2UTransform` (LengthScale 포함), `U2PSphylBasis` (180°, 축 (1,0,1)/√2), `FPhysXAllocator` (16B 정렬, live/peak 집계), `FPhysXErrorCallback` (error bits), `FPhysxUserData {Type, Index}` | |
| InitGamePhys / TermGamePhys | `PhysicsInitialization.cpp`: GPhysXFoundation, GPhysXSDK | PxCreateFoundation → `PxCreatePhysics(PxTolerancesScale(Length, Speed))` → PxInitExtensions. 살아 있는 PxPhysics는 프로세스에 하나다 (M5: `TermGamePhys`가 해제한 뒤에는 다른 scale로 다시 만들 수 있다. 초기화된 동안 다른 scale은 레지스트리·Kit·CreateScene이 거부한다). tolerance scale은 InitGamePhys 옵션 `ToleranceScale=(Length=..,Speed=..)`(UE struct 문법)로 받는다 (PxPhysics.h:997, PxSceneDesc.h:541 [V]). 같은 scale을 쓰는 변형은 각자 PxScene을 만든다 |
| FPhysScene_PhysX::InitPhysScene | 같은 이름 | `FPhysXSceneConfig`(변형 ini)로 PxSceneDesc 구성. substep clamp 규칙은 하네스 `FStepPlanner` 소관이므로 이 config에 없다 |
| FPhysXCPUDispatcherSingleThread / PhysX 워커 풀 | 같은 이름 / `PxDefaultCpuDispatcherCreate(T)` [R] | 아래 "단일 스레드 dispatcher" 참고 |
| PhysXSimFilterShader | `PhysicsFiltering.*` | 아래 "필터 셰이더" 참고 |
| FPhysXSimEventCallback | 같은 이름 (viz / `PT_INSTR_CONTACTS`) | onContact → extractContacts, 쌍당 최대 16점 [M]. PointOn1 = Body1 셰이프 위로 투영한 점, PointOn0 = PointOn1 + separation·n (v1.0 [D], §8.1. M5는 PointOn1 = position이었다). onWake/onSleep 카운터. 배열은 한 번 잡는다. 점별 normal impulse > 0이면 `PT_CONTACT_ACTIVE`(정확), impulse 값은 M9까지 NaN |
| FInitBodiesHelper / InitBody | `PhysScene_PhysX.cpp` (`FPhysScene_PhysX::CreateActors`) | §6의 UE 순서: createRigidStatic/Dynamic + 생성 시 플래그(eKINEMATIC, eDISABLE_GRAVITY, eENABLE_CCD, 최대 선속도) → 셰이프 → **배치 전체를 `addActors` 1회** → 바디별 InitDynamicProperties → `!bStartAwake`이면 putToSleep |
| FBodySetupShapeIterator | `PhysScene_PhysX.cpp` | 요소마다 exclusive PxShape를 만든다 (bEnableShapeSharing=false). contactOffset = clamp(0.02·MinExtent, 2, 8) cm [M 공식, V 상수]. MinExtent는 반지름 또는 최소 half-extent. restOffset 0. 50 cm 큐브 → 2 cm, 10 m 박스 → 8 cm |
| InitDynamicProperties_AssumesLocked | 같은 순서 (BodyInstance.cpp:4648-4743 [V]) | mass/inertia/COM → damping → maxAngularVelocity → maxDepenetration (`PtActorCreateInfo::MaxDepenetrationVelocity`는 M3부터 UE5 의미다: -1 = 설정 안 함, 0 이상 = 바디 값. UE4Compat는 설정 안 함을 UE4 규칙대로 프로젝트 값 `UPhysicsSettingsCore::MaxDepenetrationVelocity`로 바꾸고, 0은 PX_MAX_F32로 바꾼다. PhysX 5.11은 ≤0을 거부한다. 프로젝트 값은 M5에서 변형 키 `MaxDepenetrationRule=UE4Project` + `ProjectMaxDepenetrationVelocity`(UE 기본 0 [V])로 넘긴다. UE5에서 바디 값 0은 "밀어내지 않음"이므로 이 변환은 UE4Compat만의 차이다) → 초기 속도 → sleepThreshold × 배수 → stabilizationThreshold × 배수 → iterations → !bStartAwake이면 putToSleep (Normalized의 `eDISABLE_SLEEPING` 씬에서는 건너뜀, §6). 이 함수는 `addActors` **다음에** 돈다 |
| FConstraintInstance → PxD6Joint | `PhysXConstraint.cpp` [확장 M9] | `PxD6JointCreate(SDK, actor2, frame2, actor1, frame1)` (actor 순서를 뒤집음 [M]). projection 없음 (5.2에서 제거) |
| 머티리얼 | | 인자 순서는 `createMaterial(staticFriction, dynamicFriction, restitution)`이다 (PxPhysics.h:764 [V]). UE4Compat: `createMaterial(F, F, R)`, 즉 정지마찰 = 동적마찰 [M]. SdkDefault: `createMaterial(max(F, SF), F, R)`. PhysX가 정지마찰을 같은 combine 모드로 결합하므로 §5.1의 Chaos 규칙이 그대로 나온다. combine 모드는 static_cast |
| 스텝 | | StartFrame = `simulate(dt, nullptr, scratch, 262144)`, WaitPhysScenes = `fetchResults(true)` (UE4의 TG_StartPhysics/EndPhysics 분할) |
| 동기화 | | `getActiveActors` (eENABLE_ACTIVE_ACTORS + eEXCLUDE_KINEMATICS_FROM_ACTIVE_ACTORS, PxSceneDesc.h:92,212 [V]) |

**단일 스레드 dispatcher (`FPhysXCPUDispatcherSingleThread`)**
- 워커는 0개이고 `submitTask`는 호출 스레드에서 실행한다.
- PhysX는 `release()`/`removeReference` 안에서 후속 태스크를 다시 제출한다. 그래서 단순히 run 후 release하면 태스크 체인 길이만큼 재귀가 깊어진다. 1만 바디 씬에서는 체인이 길다.
- 구현: thread-local LIFO 스택을 둔다. 재진입한 `submitTask`는 스택에 넣고 바로 돌아온다. 가장 바깥 호출만 스택이 빌 때까지 꺼내 실행한다. 재귀는 없다.
- 결과는 `PxDefaultCpuDispatcherCreate(0)`(inline 실행, ExtDefaultCpuDispatcher.cpp:109 [V])과 해시로 교차검증한다.

**필터 셰이더**
- filter word 배치 [M]:
  - word0 = body index
  - word1 = block mask
  - word2 = component id (하네스에서는 body index)
  - word3 = 상위 6비트 mask, 이어서 5비트 channel, 하위 21비트 EPDF 플래그 (PhysicsFiltering.h:18-35 [V])
- 셰이더 동작:
  - KK 쌍은 억제한다 (EPDF_KinematicKinematicPairs가 없을 때) [M].
  - K-static 쌍은 변형 키 `KinematicStaticPairFilter`를 따른다 [M]: `UnlessKinematicPairs`(기본, KK와 같이 플래그가 없을 때 억제) 또는 `Always`(언제나 억제).
  - 상호 차단 검사를 한다. 차단하지 않는 쌍은 변형 키 `NonBlockingPairFilter`를 따른다 [M]: `Kill`(기본, eKILL) 또는 `Suppress`(eSUPPRESS).
  - eCONTACT_DEFAULT를 쓴다.
  - notify 플래그는 리스너가 있을 때(viz 또는 `PT_INSTR_CONTACTS`) 켠다.
  - M5 수정 라운드 0: 이 설계 문단은 K-static과 차단하지 않는 쌍의 결과를 정하지 않으므로 두 결과를 [M] 변형 키로 두고 `PxSceneDesc::filterShaderData`로 셰이더에 넘긴다 (effective 키 `physx.filter_shader_rules`, `PxScene::getFilterShaderData` READBACK). 기본값은 이 문단을 읽은 대로이고, 검토자는 UE4가 둘 다 eSUPPRESS/언제나 억제였다고 기억한다 (미확인). 사용자 4.27 확인 목록에 넣었다 (03 §2). M5 씬은 모든 바디가 모든 채널을 차단하고 kinematic 쌍 플래그가 없어 어느 쪽도 지나지 않는다.

**프로필 (`Config/Profiles/Backend/PhysX.*.ini`)**

모든 키에 증거 태그를 붙여, 사용자가 4.27로 확인한 뒤 코드 변경 없이 바꿀 수 있게 한다.

| 키 | UE4Compat (주) | SdkDefault | 근거 |
|---|---|---|---|
| 단위 / LengthScale | cm / 1 | m / 0.01 | D1 |
| PxTolerancesScale | (100, 1000) | (1, 10) | [V] UE `PhysicsCore/Private/PhysicsInitialization.cpp:18-28` (`p.ToleranceScale_Length`/`_Speed`) |
| broadPhaseType | eSAP | ePABP | [M] / [V] PxSceneDesc.h:1076 |
| solverType | ePGS | ePGS | [V] |
| PCM / stabilization / enhanced determinism | on / off / off | on / off / off | [V] 설정값 |
| kineKine / staticKine FilteringMode | eKEEP / eKEEP (셰이더가 억제) | 기본 eSUPPRESS | [M] UE4가 scene flag를 항상 설정 / [V] PxFiltering.h |
| scene CCD flag | on | off | [M] |
| bounceThresholdVelocity | 200 cm/s | 2 m/s | [V] |
| dynamicTreeRebuildRateHint | 10 | 100 | [V] |
| scratch | 262144 B | 없음 | [V] |
| per-body iterations | 8/1 (UE5 FBodyInstance는 8/2) | 4/1 | [M] / [V] PxvDynamics.h:116. M5: 변형 키 `PositionIterations`/`VelocityIterations`가 override 없는 바디에 쓰인다 |
| damping / maxAngVel | 0.01/0, 62.83 rad/s | (0/0.05, 100은 SDK 기본값) | [V]. M5: 바디 값은 모든 엔진처럼 프로필 값이고 변형이 바꾸지 않는다 (§10). SdkDefault 칸은 ini 주석으로만 남는다 |
| sleep / stab threshold | 50 / 25 (× SleepFamily) | 공식 그대로 | [V] PxvDynamics.h:134-136 |
| contact offset | UE4 clamp | 0.02·length | [M] |
| 정지마찰 | = 동적 | `max(F, SF)` | [M] / [V] Chaos 규칙 |
| joint | D6 (actors swapped) | Spherical / Revolute | [M] |
| substep clamp rule (하네스 키) | UE4PhysSubstep | UE5ChaosScene | [M]/[V]. 하네스 `[/Script/PhysicsTest.PhysicsTestSettings] SubstepClampRule`이고 EngineDefault 정책이라 변형이 지정할 수 있다 |

**변형**
- `UE4Compat-TGS`: eTGS.
- `UE4Compat-Stab`: eENABLE_STABILIZATION. M5 [V]: 안정화는 sleep 검사 안에 있어(DySleep.cpp:21-30, DyDynamics.cpp:1184-1187) `eDISABLE_SLEEPING`(Normalized)에서는 효과가 없다. Normalized의 -Stab은 UE4Compat과 비트 단위로 같다.
- 프로브 결과: 10단 스택이 PGS에서는 25 cm 흔들리고, 두 변형에서는 안정된다 [V]. 세 변형은 tolerance scale이 같으므로 뷰어 2단계에서 한 프로세스에 나란히 띄울 수 있다 [확장 M12]. 이것이 가장 유익한 PhysX 시각 비교다.

**충실도 노트 (모든 PhysX manifest에 기록)**
- UE4가 출하한 것은 PhysX 3.4.0.0 DLL이다 [V].
- 5.11과의 차이:
  - improved patch friction이 항상 켜져 있다.
  - joint projection과 eSWING drive가 없다.
  - PxCooking이 없다 (PxCreateConvexMesh 사용).
  - maxDepenetration 값은 0보다 커야 한다.
  - 기본 broadphase가 PABP다.
- 결과에는 항상 "UE4 settings on PhysX 5.11"로 표기한다.

**M5 구현 메모 [V]** ([Progress.md](Progress.md) M5 §3 P-1~P-18, I-1)
- **파일:** `Source/Backends/PhysXBackend/Private/` — `PhysXIncludes.h`(SDK 헤더는 여기서만), `PhysXSupport.*`, `PhysicsInitialization.*`, `PhysXCPUDispatcher.*`, `PhysicsFiltering.*`, `PhysXSimEventCallback.*`, `PhysXSceneConfig.*`/`PhysXParse.h`(`[PhysicsTest.Backend.PhysX]` 키, 대소문자 무시, 잘못된 키·값은 이유와 함께 `PT_E_INVALID_ARG`), `PhysScene_PhysX.*`, `PhysXBackend.cpp`. 시험 훅은 PhysX 헤더 없는 C 함수(`Public/PhysXBackend/PhysXBackendTestHooks.h`). 어댑터 컨테이너는 `physx::PxArray`다.
- **한 프로세스의 tolerance scale:** CreateScene이 `tolerances.length = 100 × LengthScale`(상대 1e-4)을 요구한다. 그렇지 않으면 SdkDefault 씬(m)이 UE4Compat PxPhysics(cm)에서 sleep 임계·contact offset이 100배 어긋난 채 조용히 돈다.
- **userData:** `FPhysxUserData {Type, Index}`를 포인터 값 자체에 넣는다 (bit 32-39 Type, 0-31 Index). 할당이 없고 커지는 배열의 포인터가 무효해지지 않는다.
- **속도 한계:** "제한 없음"은 선속도 1e16 native/s, 각속도 1e7 rad/s다 (PhysX가 1e16 초과를 거부하고 적분기가 1e7에서 자른다, NpRigidBodyTemplate.h:952, :975, DyBodyCoreIntegrator.h:123).
- **`shape.wrapping`:** PhysX에는 래퍼 객체가 없어 같은 조건을 actor에서 읽는다 (M5 수정 라운드 0: 전에는 생성 때 `PtActorCreateInfo`로 세어 READBACK이 아니었다): `getNbShapes` ≥ 2 = compound, 단일 셰이프의 local pose(캡슐은 `U2PSphylBasis`를 뺀 값)가 actor frame이 아님 = local_transform, `getCMassLocalPose` ≠ 붙은 셰이프의 균일 밀도 기하 COM(`PxMassProperties::sum`, float 16 ulp) = offset_com, `PxCapsuleGeometry` = axis_remap.
- **오류 bit:** 새 PhysX 보고의 PxErrorCode 값(0x2, 0x4, 0x8, 0x10, 0x20, 0x40. info·perf warning은 로그만)과 0x100(simulate/fetchResults 거부). `errors.capacity_bits` = 0x10(eOUT_OF_MEMORY). 카운터는 모듈 전역이다.
- **dispatcher:** 씬 옵션 `CpuDispatcher=Auto`(T = 1 단일 스레드, T ≥ 2 `PxDefaultCpuDispatcherCreate(T)`) / `SingleThread` / `PxDefault`. SdkDefault는 `PxDefault`. 단일 스레드와 inline(`PxDefaultCpuDispatcherCreate(0)`)은 BoxPile 5k 600 프레임에서 `h_native`와 태스크 수(173883)가 같고 재진입 깊이는 1 (inline은 12). T = 2도 T = 1과 비트 동일했다.
- **용량:** 힌트를 `PxSceneLimits`로 넘긴다. PhysX는 넘으면 늘리므로 용량 오류는 메모리 부족뿐이다.
- **Prepare:** scratch 블록(256 KiB)을 4 KiB마다 한 번 써서 page fault를 미리 낸다 (§9.5).
- **접촉:** 섬이 잠드는 스텝에 PhysX는 그 섬의 접촉을 보고하지 않는다 (M1 a: 49 프레임 4점, 50 프레임 잠듦 0점). 그래서 리스너가 있을 때(viz 또는 `PT_INSTR_CONTACTS`) 직전 스텝 보고를 복사해 두고 새로 잠든 쌍을 가져와 `PT_CONTACT_SLEEPING`으로 낸다 (Jolt M4 규칙과 같은 의미). 직전 스텝 보고의 각 점은 다음 `StartFrame`에서 한 곳에만 간다: `PUT_TO_SLEEP`으로 잠든 바디의 쌍이면 유지 목록, 아니면 직전 스텝 목록 (M5 수정 라운드 0: 전에는 두 곳 모두에 들어가 명령으로 재운 바디의 점이 두 번 나왔다, `SdkBackend.PutToSleepKeepsEachContactOnce`). 정지 PhysX 점은 Phi ≈ ±0(float)이라 하네스 섬 간선은 솔버 임펄스(ACTIVE)로도 잇는다 (§11 M5 메모).
- **명령:** 0이 아닌 `setLinearVelocity`/`setAngularVelocity`는 autowake = false여도 잠든 바디를 깨운다 (NpRigidDynamic.cpp:253, :286, :605-622). M5에서는 그래서 PhysX만 `SET_*_VELOCITY`와 0이 아닌 속도의 `SET_BODY_STATE`로 잠든 바디를 깨웠다 (Null·Jolt는 잠든 채 속도를 유지, Bullet은 잠든 동안 속도를 잃음). **M6 (ABI v1.0):** 모든 백엔드가 속도·상태 쓰기로 잠든 바디를 깨운다 (0 벡터 포함, [Abi.md](Abi.md) §9.1 결정 1, `SdkBackend.VelocityOfASleepingBody`, G6). PhysX 어댑터는 `setLinearVelocity(v, false)` 뒤 잠들어 있으면 `wakeUp()`을 부른다. **UE4와 다른 점 [M]:** UE4 `FPhysicsInterface_PhysX::SetLinearVelocity_AssumesLocked`(와 각속도)는 `bAutoWake`를 `PxRigidBody::setLinearVelocity(v, autowake)`에 그대로 넘겼다. 그래서 UE4에서 `bAutoWake = false`로 0 벡터를 쓰면 잠든 바디가 잠든 채 남았다. v1.0은 `PT_CMDF_AUTO_WAKE`를 정보용으로 두므로 UE4Compat 계열도 이때 깨운다. UE5 Chaos와 같은 계약을 택한 의도적 차이다 (0이 아닌 벡터는 PhysX가 autowake와 무관하게 깨우므로 UE4와 같다, NpRigidDynamic.cpp:253, :286 [V]).
- **effective 정직성 (M5 수정 라운드 0):** `material.friction_combine`/`restitution_combine`은 PxMaterial마다 `getFrictionCombineMode`/`getRestitutionCombineMode`를 호스트 값과 대조한 READBACK("N of N PxMaterials hold the host's mode"), `physx.live_scenes`는 `PxPhysics::getNbScenes` READBACK. 호스트 입력과 어댑터 카운터(`physx.max_threads`, `physx.initializations`, `physx.allocator`, `physx.error_reports`, `physx.start_asleep_skipped`, `physx.error_bits`, `physx.sim_event_callback`, `physx.dispatcher_max_reentrancy`)는 SET_ONLY다 ([Abi.md](Abi.md) §5.14).
- **dispatcher 검증 범위:** 단일 스레드 LIFO와 `PxDefaultCpuDispatcherCreate(0)`(깊이 우선 재귀)은 후속 태스크를 다른 순서로 돌린다. 해시 동일은 `M3.BoxPile`(CCD·조인트·articulation 없음)에서만 보였다. D6 조인트와 CCD가 태스크 그래프 가지를 더하므로 M9에서 D6 씬과 CCD 씬으로 단일 스레드 대 inline 해시·깊이 교차검증을 다시 돈다 (Progress M5 §5).
- **PX_DEBUG/PX_CHECKED/PX_SUPPORT_PVD는 어댑터에 전달하지 않는다** (공개 헤더에서 레이아웃을 바꾸지 않는다, `cmake/ThirdParty/PhysX.cmake` 주석). checked 라이브러리의 PX_ASSERT는 핸들러 없이 `__debugbreak`다 (FdAssert.cpp:31-33). dbg 스위트·벤치에서 assert는 나지 않았다. M6 정정: 레이아웃은 같지만 어댑터가 dbg 라이브러리와 같은 inline·템플릿(`PxArray`, `PxVec3` 등, PX_ASSERT 포함)을 assert 없이 인스턴스화하므로 그 COMDAT은 ODR상 다르고 링커가 한 벌만 남긴다. dbg는 측정 빌드가 아니라 받아들인다 (`PhysXIncludes.h`, `PhysX.cmake` 주석).

**M6 구현 메모 [V]** ([Progress.md](Progress.md) M6 §3)
- **접촉 점 (v1.0 계약 3):** `StartFrame`이 리스너가 있을 때 바디의 스텝 전 자세(`PreStepPoses`)를 저장한다. `onContact`는 점마다 `position + n·(|separation| + 2·contactOffset + 0.01)`에서 Body1 셰이프(그 자세)까지 `PxGeometryQuery::pointDistance`의 가장 가까운 점을 PointOn1로 쓴다 (면에서는 법선 아래 점, 곡면과 모서리에서는 법선 위의 점. 처음 쓴 raycast는 박스 모서리에 걸친 캡슐 끝에서 빗나갔다). 원점이 셰이프 안이면 position을 쓰고 `NumSurfaceFallbacks`로 센다 (G3 0). 보고 순서는 `PairHeader.actors[1]`의 `PxRigidActor`로 Body1을 정해 상관없다.
- **gyroscopic (M5 검토 지적):** `PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES`는 PhysX 5.1.0에 생겼다 (CHANGELOG.md:1965, :2056 [V]). UE4는 PhysX 3.4라 이 항이 없었다. 변형 키 `GyroscopicForces=Ignore|FromBody`(UE4Compat 계열 Ignore [M], SdkDefault FromBody)를 두고, `body.gyroscopic`(READBACK, actor flag)과 무시한 바디 수를 기록한다. 두 프로필 모두 `bGyroscopicTorqueEnabled=False`라 지금 결과는 같다.
- **effective:** `contact.speculative_distance`는 씬 셰이프 contact offset 최댓값 × 2의 READBACK이다 (PhysX는 두 셰이프의 contact offset 합 안에서 접촉을 만든다). `body.damping_formula` canonical 1.
- **변형 ini 증거 태그:** `ProjectMaxDepenetrationVelocity`의 근거를 PhysicsSettingsCore.h:93-95(0 = 제한 없음, 기본 0은 UObject 0 초기화)로 고쳤다. scene CCD, active actors, KK/KS 쌍 유지는 UE 5.8.3에 남은 PhysX 시절 스위치 `bDisableCCD`, `bDisableActiveActors`, `bDisableKinematicStaticPairs`, `bDisableKinematicKinematicPairs`(PhysicsSettings.h:292-305, 모두 기본 false)의 기본값이라 [V]이고 스위치 → 씬 flag 대응만 [M]이다.

### 8.3 Jolt 5.6.0 (`FPhysScene_Jolt`) [MVP M3]

- **단위:** m. 질량은 `MassPropertiesOverride` + `EOverrideMassProperties::MassAndInertiaProvided`로 넘긴다 (R·diag·Rᵀ, ×1e-4).
- **셰이프와 COM:**
  - COM이 항등인 단일 요소는 `BoxShape`/`SphereShape`를 그대로 쓴다 (§8.1).
  - COM이 원점에서 벗어나면 `OffsetCenterOfMassShape`로 옮겨 바디 원점을 UE actor 원점으로 유지한다.
  - capsule은 `RotatedTranslatedShape`로 Y→Z 변환한다. 이것은 축 재매핑이며 `shape.wrapping.axis_remap`으로 따로 센다.
  - 여러 요소는 `StaticCompoundShape`.
  - convex는 `ConvexHullShape` [확장 M15]. 반지름 정책은 기록하며, `Jolt.SharpBoxes` 변형은 반지름 0.
- **필터:** BP 레이어 2개 (NON_MOVING, MOVING). UE 마스크는 `FJoltUEGroupFilter`로 처리한다.
- **combine:** `SetCombineFriction` / `SetCombineRestitution` 람다로 UE 규칙을 적용한다.
  - Jolt는 마찰 계수가 하나뿐이다 (`mCombinedFriction`). 람다는 동적 마찰 `combine(F0, F1)`을 돌려준다.
  - UE 기본값(SF=0 ≤ F)에서는 Chaos 규칙과 결과가 같다. SF > F인 머티리얼 쌍은 표현할 수 없으므로 `material.static_friction_rule=dynamic_only`로 기록하고 by-design 태그를 단다.
- **스텝:** `PhysicsSystem::Update(dt, 1, FJoltTempAllocator, JobSystemThreadPool(T-1))`. 할당기는 Jolt `TempAllocatorImpl`이 아니라 하네스 `FJoltTempAllocator`다 (M3 메모: `TempAllocatorImpl`은 블록이 모자라면 `std::abort`하므로 넘침을 오류 bit로 내는 할당기, [Progress.md](Progress.md) M3 §3 #16).
  - TempAllocator 크기는 용량 힌트로 정한다 (M3 메모: 128 MB 고정 대신 힌트에서 계산).
  - EPhysicsUpdateError를 매 스텝 검사한다.
- **동기화:** `GetBodyInterfaceNoLock` + `GetActiveBodiesUnsafe` (M3 메모).
- **Prepare:** `OptimizeBroadPhase`.
- **접촉:** 리스너는 viz 또는 `PT_INSTR_CONTACTS`일 때만 있다 (잡 스레드에서 잠금·할당 없이 미리 잡은 배열에 atomic 슬롯을 예약한다. M3 메모 "스냅샷", [Progress.md](Progress.md) M3 §3 #13). `Normal = -mWorldSpaceNormal`, 점마다 `Phi = -(p1 - p2)·n` (M4에서 정정. 매니폴드의 `mPenetrationDepth`는 가장 깊은 점의 값이다). impulse는 NaN이고 active 여부는 근사 플래그다. 잠든 섬의 접촉은 유지해 마지막 스텝의 플래그(Phi ≤ 0이면 `ACTIVE_APPROX`)에 `PT_CONTACT_SLEEPING`을 더해 낸다 (M4 메모. Chaos도 잠든 제약의 마지막 솔버 결과를 지우지 않아 쉬는 잠든 접촉을 활성으로 그린다).
- **섬:** 네이티브 섬은 내부 API뿐이므로 하네스 `FIslandBuilder`를 쓴다 (§7.5).
- **sleep 끄기 (Normalized):** 바디마다 `BodyCreationSettings::mAllowSleeping=false` (BodyCreationSettings.h:106 [V]).
- **속도 클램프:** UEParity에서 `mMaxAngularVelocity`는 62.83으로 둔다. `mMaxLinearVelocity`(500 m/s)는 해제한다.

**M3 구현 메모 [V]**
- **생성 순서 (§6):** 바디마다 `BodyCreationSettings`(모션 타입, 레이어, `CollisionGroup`, `mAllowSleeping`, CCD, 자이로, 중력 계수, 선속도 한계, damping 0, 질량·관성 `mMassPropertiesOverride` + `EOverrideMassProperties::MassAndInertiaProvided`) → `AddBodiesPrepare`/`AddBodiesFinalize` 1회 → 동적 속성(damping, 각속도 한계를 `MotionProperties`에, 초기 속도) → `!bStartAwake`이고 kinematic이 아니면 `DeactivateBody`. sleep을 끈 씬(Normalized)에서는 이 마지막 단계를 건너뛰고 `jolt.start_asleep_skipped`로 센다. 질량·관성은 UE가 `InitDynamicProperties`에서 정하지만, Jolt는 `CreateBody`가 `MotionProperties`를 계산하므로 1단계의 생성 설정에 넣는다. 그 사이에 스텝이 없어 결과는 같다 (M3 검토 지적, M4에서 이 문장을 정정했다).
- **속도 한계:** Jolt는 언제나 clamp한다 (`ClampLinearVelocity`가 float로 제곱한다). "제한 없음"은 1e7 m/s, 1e7 rad/s로 둔다 (제곱이 float에서 유한하다). 프로필 각속도 한계(UEParity `MaxAngularVelocity`)가 있으면 그 값을 쓴다.
- **필터:** `FJoltUEGroupFilter`. `CollisionGroup`의 GroupID에 BlockMask, SubGroupID에 ObjectType을 넣어 `(Mask0 & Bit(Type1)) && (Mask1 & Bit(Type0))`을 판정한다. BP 레이어는 정적/움직임 2개.
- **combine:** 바디 user data의 bit 0-31에 바디 인덱스, 32-33에 마찰 결합 모드, 34-35에 반발 결합 모드를 넣고 `SetCombineFriction`/`SetCombineRestitution` 함수가 `PtCombine`의 UE 규칙(우선순위 Max > Multiply > Min > Average)으로 계산한다.
- **restitution threshold:** `PtSceneDesc::RestitutionThreshold`가 있으면 그 값, 없으면(UEParity) Chaos와 같은 1000·dt cm/s를 `mMinVelocityForRestitution`에 넣는다 (DL35). 값은 options_sha256에 들어간다.
- **용량:** `PhysicsSystem::Init(바디 = 힌트 또는 1024, 쌍 = 힌트 또는 8N+1024, 접촉 = 힌트 또는 4N+1024)`. temp allocator는 16 MiB + 1 KiB × 접촉 제약 + 1 KiB × 바디 (`TempAllocatorBytes`로 덮어쓸 수 있음)를 `PrepareForSimulation`이나 첫 스텝에서 만든다. `M3.BoxPile` 10k에서 110 MB를 잡고 peak 39.9 MB였다. 할당기는 `TempAllocatorImpl`(블록이 모자라면 `std::abort`) 대신 하네스 `FJoltTempAllocator`다. 블록이 모자라면 힙으로 넘어가 계속 돌되 오류 bit 0x100(벤치 종료 코드 5)으로 알린다. 128 MB 고정은 1k 씬에도 128 MB를 commit하므로 쓰지 않았다.
- **kinematic:** 목표가 있는 스텝에 `MoveKinematic(목표, dt)`. 텔레포트(`SetGlobalPose`)는 `SetPositionAndRotation` + 속도 0. 상태의 선속도는 `MoveKinematic`에 준 원점 속도를 저장해 돌려준다 (§5.2 #48).
- **임펄스·힘 (M3 수정 라운드 1):** `ADD_IMPULSE`/`ADD_VELOCITY`/`ADD_ANGULAR_*`는 `Body::AddImpulse`처럼 push 때 속도를 바꾸지 않는다. 바디 기록에 속도 변화(m/s, rad/s. 각임펄스는 push 시점 `MultiplyWorldSpaceInverseInertiaByVector`)를 쌓고 `StartFrame`이 kinematic 목표 뒤, `Update` 바로 전에 `SetLinearVelocityClamped`/`SetAngularVelocityClamped`로 더한다. 그때 잠든 바디는 들고 있는다. `ADD_FORCE`/`ADD_TORQUE`도 쌓았다가 그때 깨어 있는 바디에만 `Body::AddForce`/`AddTorque`로 넘기고 목록을 비운다. 두 목록은 최대 바디 수로 미리 잡는다. 규칙은 [Abi.md](Abi.md) §7.1.
- **Prepare (M3 수정 라운드 1):** `OptimizeBroadPhase`와 temp 블록 pre-fault(4 KiB 페이지마다 한 번 쓰기). 블록의 첫 사용이 충돌 초기 프레임의 T_step 안에서 page fault를 내지 않게 한다 (§9.5).
- **읽기:** `PT_READ_ACTIVE_ONLY`는 `GetActiveBodiesUnsafe` + 직전 프레임에 활성이던 바디 + 명령으로 바뀐 바디(스탬프로 중복 제거)다. 잠든 바디의 마지막 상태도 한 번 나간다.
- **스냅샷:** 접촉 listener는 viz 실행이나 `PT_INSTR_CONTACTS`일 때만 설치한다 (벤치는 비용을 내지 않는다, effective `listeners.installed`). `OnContactAdded`/`OnContactPersisted`의 매니폴드를 한 번 크기를 잡은 `JPH::Array`(4 × 접촉 제약 용량)에 atomic 슬롯 예약으로 모으고 (잡 스레드에서 할당·잠금 없음, 넘치면 truncated. M3 수정 라운드 0 전에는 mutex + `std::vector`), 캡처할 때 `CanonicalizeContact`로 Chaos 규약(Body0이 더 움직이는 쪽, normal은 Body1 → Body0, Phi < 0 침투)에 맞춘다. Phi ≤ 0이면 `ACTIVE_APPROX`, 아니면 `SPECULATIVE` 플래그를 단다. 임펄스는 NaN, 섬 번호는 내지 않는다.
- **해시:** `h_native`는 바디 인덱스 순서로 Jolt가 가진 actor 위치(cm로 바꾼 double)와 회전을 이어 붙인 값의 FNV-1a 64다. 엔진 간에는 비교하지 않는다.
- **훅:** Jolt `Trace`와 `AssertFailed`를 Kit 로그로 보낸다. assert는 dbg 빌드에서만 켜지고, 난 스텝은 오류 bit 0x200(벤치 종료 코드 3), 누적 횟수는 모듈 effective 키 `jolt.asserts`로 남는다. M4부터 `Update` 밖의 assert(생성, 명령, 읽기, 스냅샷)도 다음 프레임의 0x200과 `GetStepStats`의 누적 bit가 된다 (씬이 카운터를 `SetUpForFrame`, `StartFrame` 앞뒤, `EndFrame`, `GetStepStats`에서 확인한다. 프레임 사이의 `GetStepStats`가 먼저 본 assert는 대기 bit로 남아 다음 프레임에 들어간다. M3 검토 지적, M4 수정 라운드 0). `Factory`와 `RegisterTypes`는 `InitGamePhys`에서, 해제는 `TermGamePhys`에서 한다.
- **예외와 RTTI:** 어댑터 TU는 CMake 기본 `/GR /EHsc`를 유지한다 (Jolt 자신은 `/GR-`, 예외 없음). Kit의 예외 장벽이 하네스와 같은 조건에서 동작한다. 어댑터 컨테이너는 `JPH::Array`라 예외를 던지지 않는다 (§7.3 어댑터 규칙, M3 수정 라운드 0). `JPH::GetConfigurationString()`은 헤더 inline이라 어댑터 TU 기준("(C++ RTTI) (C++ Exceptions)")을 보고하므로, Jolt lib의 플래그 증거는 `BuildFlags.json`이다.
- **변형:** `Config/Profiles/Backend/Jolt.Default.ini`가 Jolt 기본값을 명시한다 (NumVelocitySteps 10, NumPositionSteps 2, ConvexRadius 5 cm, PenetrationSlop 2 cm, SpeculativeContactDistance 2 cm, Baumgarte 0.2). 옵션 키는 이 6개와 `UseLargeIslandSplitter`, `DeterministicSimulation`, `TempAllocatorBytes`다.
- **effective 키:** 공통 키 24개(M6 수정 라운드 1부터 25개) + `jolt.config_string`, `jolt.temp_allocator_bytes`, `jolt.temp_allocator_peak_bytes`, `jolt.temp_allocator_fallbacks`, `jolt.capacity`, `jolt.penetration_slop`, `jolt.baumgarte`, `jolt.max_depenetration_velocity`(n/a), `jolt.inertia_conditioning`(n/a), `jolt.start_asleep_skipped`, `jolt.update_error_bits` + 선택 키 `errors.capacity_bits`(0x107, [Abi.md](Abi.md) §5.14). 모듈 키는 `jolt.version`(`JPH_VERSION_*`와 고정 SHA로 만든다), `jolt.module_config_string`, `jolt.max_threads`, `jolt.live_scenes`, `jolt.asserts`. READBACK은 엔진에서 읽은 값만이다 (M3 수정 라운드 1): `shape.box_margin`은 바디 셰이프의 `BoxShape::GetConvexRadius`(데코레이터·compound를 따라간다), `threads.workers`는 `JobSystemThreadPool::GetMaxConcurrency() - 1`, `shape.convex_radius`는 옵션 값이라 SET_ONLY다.

### 8.4 Bullet 3.25 (`FPhysScene_Bullet`, ST) [MVP M5]

- **월드 구성:** `btDefaultCollisionConfiguration` (풀 크기는 힌트로 정함), `btDbvtBroadphase`, `btSequentialImpulseConstraintSolver`, `btDiscreteDynamicsWorld`.
- **스텝:** `stepSimulation(dt, 0, dt)`.
- **셰이프:**
  - `btSphereShape`
  - `btBoxShape`: 마진은 셰이프마다 `min(0.04 m, 0.1 × minHalfExtent)`이며 `getMargin()`으로 읽어 기록한다 (§8.1)
  - `btCapsuleShapeZ` (재매핑 없음, D1 보정)
  - `btConvexHullShape` [확장 M15]
- **COM:** body frame이 COM 주축이다 (M5: dynamic 바디만. static·kinematic은 actor frame이고 `ReadMassProperties`는 하네스 값이다).
  - COM이 항등인 단일 요소는 셰이프를 그대로 쓴다.
  - 그 밖의 경우에만 `btCompoundShape` 자식을 inv(ComLocal)·Elem 위치에 놓고, 읽을 때 ActorTM을 복원한다.
  - 질량은 `setMassProps(m, I·1e-4)`.
- **필터:** group = 1<<ObjectType, mask = BlockMask. 기본 broadphase 필터가 이미 UE의 상호 차단 검사와 같다 [V].
- **combine:** 전역 `gCalculateCombinedFriction/RestitutionCallback`으로 UE 규칙을 적용한다 (GLOBAL_SCENE_SETTINGS). Bullet도 마찰 계수가 하나뿐이므로 (`m_combinedFriction`) 정지마찰은 Jolt와 같이 처리한다.
- **각속도 클램프:**
  - UEParity: 62.83 rad/s를 내부 pre-tick 콜백으로 에뮬레이트한다.
  - **M5 정정 [V]:** 설계가 인용한 π/(2·dt) = 94.2 rad/s 클램프(btRigidBody.cpp:380-385)는 `btRigidBody::integrateVelocities`에 있고, 이 함수는 `btSimpleDynamicsWorld`만 부른다(btSimpleDynamicsWorld.cpp:221). `btDiscreteDynamicsWorld`에 걸리는 한계는 `btTransformUtil::integrateTransform`(btTransformUtil.h:19, 44-72)이다: 스텝 회전이 `ANGULAR_MOTION_THRESHOLD` = π/4(60 Hz에서 47.1 rad/s)를 넘으면 지수 사상의 각도만 줄여 한 스텝에 2·atan((\|w\|·dt/(π/4))·tan(π/8))만 돈다 (62.83 rad/s면 60° 대신 57.9°). 속도는 그대로다. 이 문턱을 `bullet.angular_motion_threshold`(COMPILE_TIME)로, 넘은 body-step 수를 `bullet.angular_motion_limit_hits`(어댑터 post-tick이 세므로 SET_ONLY, M5 수정 라운드 0)로 기록한다. UEParity의 62.83 rad/s는 Bullet 문턱 위다.
- **kinematic:** CF_KINEMATIC_OBJECT. M5: StartFrame이 목표로 world transform을 옮기고, 속도는 double로 계산해 pre-tick이 넣는다 (목표가 없으면 0, Chaos Reset). `btTransformUtil::calculateVelocity`는 float 쿼터니언의 acos(w)라 회전하지 않은 바디에도 약 0.04 rad/s를 만든다 [V].
- **sleep 끄기 (Normalized):** 바디마다 `DISABLE_DEACTIVATION` (M5: kinematic에도). UEParity의 sleep 기준은 M5에서 Bullet 자신의 값(바디별 80 cm/s·1 rad/s, `gDeactivationTime` 2 s)이고 UE 임계값 매핑은 M9다.
- **디버그 드로어:** bench run에서는 붙이지 않는다. 드로어가 붙어 있으면 stepSimulation이 드로어의 `DBG_NoDeactivation` 비트로 `gDisableDeactivation`을 덮어쓴다 (btDiscreteDynamicsWorld.cpp:418-421 [V]). viz run에서만 붙이고, 그 비트를 프로필의 sleep 설정과 맞춘다 (M9. M5는 어느 run에도 붙이지 않는다).

**M5 구현 메모 [V]** ([Progress.md](Progress.md) M5 §3 B-1~B-14)
- **파일:** `Source/Backends/BulletBackend/Private/` — `BulletIncludes.h`(SDK 헤더는 여기서만), `BulletMath.h`(단위 변환 한 곳 `GScale = GetEngineUnitScale(Bullet)`, actor ↔ COM 대수 double), `BulletCombine.h`, `PhysScene_Bullet.*`, `BulletBackend.cpp`. 어댑터 컨테이너는 `btAlignedObjectArray`다.
- **내부 pre-tick** (btDiscreteDynamicsWorld.cpp:456-459, `saveKinematicState`·`applyGravity` 뒤, `predictUnconstraintMotion` 앞): UE 순서 감쇠(§8.1), 프로필의 선·각속도 한계(UEParity 62.83 rad/s), kinematic 속도. **post-tick:** 각속도 문턱 초과 수, 풀 peak.
- **접촉 스냅샷:** 리스너가 없다. viz run이나 `PT_INSTR_CONTACTS`에서 캡처할 때 디스패처의 persistent manifold를 읽는다 (`listeners.installed` 0). 법선·마찰 임펄스는 실제 값(kg·cm/s), `PT_CONTACT_ACTIVE`는 정확(임펄스 > 0), Phi > 0이면 SPECULATIVE, `NativeIslandId` = `getIslandTag()`. 잠든 쌍은 narrow phase를 돌지 않아 매니폴드가 그대로 남으므로 M4의 잠든 접촉 규칙(`PT_CONTACT_SLEEPING`)이 장부 없이 성립한다. 자세를 정하는 명령(`SET_GLOBAL_POSE`, `SET_BODY_STATE`)은 그 바디의 매니폴드를 버린다 (`cleanProxyFromPairs`, Jolt `FlushKeptContactCommands`와 같은 규칙).
- **섬:** Bullet 섬은 broad phase 쌍으로 합친다 (`btSimulationIslandManager::findUnions`). 그래서 네이티브 섬이 하네스 섬보다 크고, 섬 전체가 sleep 조건을 만족해야 잠든다.
- **풀:** manifold 풀 = `MaxContactConstraintsHint`, collision algorithm 풀 = `MaxBodyPairsHint`(없으면 4096). heap으로 넘치면 오류 bit 0x1(manifold)/0x2(algorithm), `errors.capacity_bits` = 0x3, 벤치 종료 코드 5.
- **contact breaking threshold (M5 수정 라운드 0):** 디스패처는 기본으로 `CD_USE_RELATIVE_CONTACT_BREAKING_THRESHOLD`다 (btCollisionDispatcher.cpp:32 [V]). 그래서 `gContactBreakingThreshold`(0.02)는 두 역할이다: 매니폴드의 breaking threshold에서는 셰이프 angular motion disc에 곱하는 무차원 계수(쌍의 두 셰이프 중 작은 값, btCollisionDispatcher.cpp:74-75, btCollisionShape.cpp:42-45 [V]. 50 cm 큐브 약 0.87 cm, 100 cm 큐브 1.73 cm)이고, AABB 팽창에서는 m 단위 길이다 (btCollisionWorld.cpp:153-160 [V]). 모듈 키 `ContactBreakingThreshold`는 Bullet이 받는 값 그대로다 (전에는 cm로 읽어 0.01을 곱했고 기본값에서만 우연히 맞았다). `contact.speculative_distance`는 엔진이 쓰는 값을 READBACK한다: 살아 있는 매니폴드의 `getContactBreakingThreshold` 범위, 없으면 dynamic 바디 셰이프의 `getContactBreakingThreshold(gContactBreakingThreshold)` 범위(그 바디 쌍의 상한). `DeactivationTime`은 0보다 커야 한다 (0이면 `wantsSleeping`이 언제나 거짓이라 sleep이 조용히 꺼진다, btRigidBody.h:522 [V]).
- **effective 정직성 (M5 수정 라운드 0):** `bullet.debug_drawer`는 `getDebugDrawer()` READBACK, `listeners.installed`는 `gContact*Callback` 전역 5개를 읽는다. 어댑터 카운터(`bullet.angular_motion_limit_hits`, `bullet.pool_heap_fallbacks`, `bullet.start_asleep_skipped`, `bullet.engine_error_bits`)와 호스트 입력·어댑터 수(`bullet.max_threads`, `bullet.live_scenes`)는 SET_ONLY다.
- **변형 키** (`[PhysicsTest.Backend.Bullet]`, `Bullet.Default.ini`가 Bullet 3.25 기본값을 증거 태그와 함께 명시): NumIterations 10, SolverMode 0x104, SplitImpulse, 임계 −4 cm, TurnErp 0.1, Erp2 0.2, LinearSlop 0, WarmstartingFactor 0.85, sleep 80 cm/s·1 rad/s, ForceUpdateAllAabbs, DampingModel=UE, 풀 크기 등 14개. 모듈 키는 §7.5.
- **거동 메모:** `M3.BoxPile` 1k는 기본값(반복 10, 반발 0.3)에서 기둥이 120~300 프레임 사이에 무너진다. 반발 0 + 반복 50이면 선다. 규약 오류가 아니라 솔버 거동이다 ([Checkpoints.md](Checkpoints.md)).

## 9. 프레임/스텝 모델과 타이밍 계약

### 9.1 `FPhysScene::Tick` (UE 공식은 한 곳에만 구현) [MVP M2]

```
Tick(float DeltaSeconds)                               ~ UWorld::Tick physics part (LevelTick.cpp:1740-1778)
  SetUpForFrame(&Gravity, Dt, MinDt, MaxDt, MaxSubDt, MaxSubsteps, bSubstepping)  -> FStepPlanner -> FStepPlan{N, StepDt, FrameDt, Lost}
  StartFrame()
    OnPhysScenePreTick(FrameDt)   phase SetUp; FrameDt = UE5 MDeltaTime (FChaosScene::StartFrame -> OnStartFrame)
    bIsSimulating = true
    if Driver == Native (backend has NATIVE_FRAME_MODEL: Chaos, Null):
      OnPhysSceneStep(FrameDt) once                           (UE5 OnStartFrame; not per substep)
      [T_push] flush commands once, every frame: the queue in issue order (incl. each force's WAKE_UP), the kinematic
               targets, and at a frame that pushes steps the held forces with PT_CMDF_NO_WAKE (the backend applies UE5's
               frame rules: forces on every step of a pushing frame, averaged over the frames since the last push,
               impulses on the first step, kinematic targets by PseudoFraction; bAllowSubstepping is informational)
      [T_step] Backend SetUpForFrame(original UE args) -> StartFrame -> WaitPhysScenes -> EndFrame
               Result.NumStepsTaken == Plan.NumSteps, else Fatal
      SimTime += NumSteps * Plan.InternalStepDeltaTime       (the double dt the engine steps with)
    else (Harness driver, SDK backends):
      UE5ChaosScene: OnPhysSceneStep(FrameDt) once            (as Native)
      if N == 0: [T_push] flush the queue only                (the frame's writes reach the backend in that frame)
      for k in 0..N-1:
        UE4PhysSubstep: OnPhysSceneStep(StepDt)               (untimed, before the step's push)
        [T_push] the queue (issued since the last flush). Then by Plan.ClampRule, forces with PT_CMDF_NO_WAKE:
                 UE5ChaosScene  every force on every step x ForceWeight (1 / frames since the last push; frames that push
                                nothing keep the forces); kinematic target Lerp/Slerp by 1/(N-k) from the last sent pose
                                (a teleport this frame: from its pose) if Plan.bSolverSubstepped, else the target on step 0
                 UE4PhysSubstep bAllowSubstepping forces on every substep from the one they were issued in, the others on
                                that one; kinematic target with bAllowSubstepping Lerp/Slerp alpha=(k+1)/N from the frame's
                                start pose, the others on k = 0 [M]
        [T_step] Backend SetUpForFrame(&G, StepDt, 0, 0, 0, 1, false) -> StartFrame -> WaitPhysScenes -> EndFrame
                 (NumStepsTaken must be 1; the scene was created with AsyncDt -1)
        SimTime += dt_engine(StepDt)
  WaitPhysScenes()  no-op (kept for API parity)
  EndFrame()    bIsSimulating = false
    [T_pull] ReadBodyStates(ACTIVE_ONLY), once per frame -> FBodyInstance::State
    [untimed] bench run:     GetStepStats values that need no listener; native hash only after the final frame
              behavior/viz:  hash every H, counters every C, capture frames: snapshot -> FIslandBuilder -> DebugDraw::Draw*
    OnPhysScenePostTick(FrameDt)
```

- **델리게이트 순서 (M1 수정 라운드에서 고침, §5.2 #31):** UE5는 `SetUpForFrame` 다음 `StartFrame` 안에서 PreTick과 Step을 clamp된 프레임 dt(`MDeltaTime`)로 한 번씩 broadcast한다 (ChaosScene.cpp:331-374, PhysScene_Chaos.cpp:2272-2316 [V]). 하네스 PreTick과 Step은 두 driver 모두 이와 같다 (M2 수정 라운드 1). Harness driver의 `UE4PhysSubstep` 규칙만 `OnPhysSceneStep`을 서브스텝마다 서브스텝 dt로 부른다 (UE4 PhysX substep 방식). PostTick은 UE5에서 dt 없이 EndFrame에서 broadcast되고 (ChaosScene.cpp:576 [V]), 하네스는 같은 FrameDt를 넘긴다.
- **명령의 시점 (M2 수정 라운드 1, §5.2 #40·#44·#45):** 두 driver 모두 매 프레임 push한다. 스텝이 없는 프레임의 쓰기도 그 프레임에 백엔드에 닿으므로, 읽기와 `bAddToCurrent`가 UE의 GT 파티클처럼 본다. `AddForce`/`AddTorque`의 GT 깨우기는 호출한 자리의 `WAKE_UP`이고, 힘은 push 때 `PT_CMDF_NO_WAKE`로 간다. 힘은 스텝을 push하는 프레임까지 씬이 들고 있고, 그 사이 `PutToSleep`이 오면 UE처럼 버린다 (뒤에 힘·임펄스가 오면 되살린다).

- **FixedStep과 Sync에서는** 두 driver가 백엔드에 같은 명령을 같은 순서로 보내고 호출도 같다. 그래서 스텝이 없는 프레임(dt 0, 1e-9)을 포함해 결과가 비트 단위로 같다 (A10 (b): 고정 스크립트 3가지 dt 목록과 무작위 명령 스크립트 6 seed).
  - Chaos에서 MaxDt≤0은 "clamp 없음"이다 (ChaosScene.cpp:341-345). 솔버 루프도 MaxDeltaTime>0을 검사한다 (PBDRigidsSolver.cpp:569) [V].
- **다중 스텝 모드에서는:**
  - Chaos가 UE처럼 EndFrame을 프레임당 1회만 수행한다. 서브스텝마다 EndFrame을 부르면 GT 작업이 과다 계상되기 때문이다.
  - `EFrameDriver::Harness`를 Chaos에 강제하는 것은 진단용으로만 허용한다. Null에서는 두 driver를 모두 돌려 A10을 검사한다.
- **헤드라인 run의 비타이밍 작업:** 1만 바디의 해시나 카운터 패스는 1.1 MB 이상을 건드려 다음 프레임의 캐시를 식힌다. 그래서 bench 모드는 `HashEvery=0`(마지막 프레임 뒤에만 해시)과 `CountersEvery=0`을 기본으로 한다. 리스너가 필요 없는 `GetStepStats` 값은 매 프레임 읽어도 된다.

### 9.2 `FStepPlanner` (UE의 산술을 UE의 형 그대로 재현하고 테스트로 고정) [MVP M1]

전 모드를 M1에 구현하고 단위 테스트로 고정한다. MVP 씬은 FixedStep과 Sync만 쓴다. Chaos로 Native driver를 실제 검증하는 것은 M13이고, 그 전에는 Null로 검증한다 (A10).

| 모드 | 이번 프레임 스텝 | 근거 |
|---|---|---|
| FixedStep (벤치 기본) | N=1, dt = FixedDeltaTime (1/60). 프레임 dt는 무시 | 하네스 |
| Sync | N=1, dt = min(dt, MaxPhysicsDeltaTime) | ChaosScene.cpp:331-361 [V] |
| Substepping, UE5ChaosScene | D = min(dt, MaxSubsteps·MaxSubDt), N = ceil(D/MaxSubDt). N > Max이면 N=Max, 스텝 = MaxSubDt (남는 시간 소실). 아니면 스텝 = D/N | PhysicsSolverBase.cpp:581-621 [V] |
| Substepping, UE4PhysSubstep | UseDelta = min(dt, MaxPhysicsDeltaTime) [M] 후 D = min(UseDelta, Max·MaxSubDt), N = clamp(ceil(D·(1/MaxSubDt)), 1, Max), 스텝 = D/N | PhysSubstepTasks.cpp:211-227 [V] (5.8.3에서는 dead code) |
| AsyncFixed | acc += min(dt, MaxPhysicsDeltaTime), N = floor(acc/AsyncDt) (0일 수 있음), acc −= N·AsyncDt | PhysicsSolverBase.cpp:591-605 [V] |

**AsyncFixed와 Chaos 보간 [V]**
- Chaos의 고정 dt 누산기는 `IsUsingFixedDt()`일 때만 돈다. `IsUsingFixedDt() = IsUsingAsyncResults() && UseAsyncInterpolation`이다 (PhysicsSolverBase.h:712-720; `p.UseAsyncInterpolation` 기본 1, PhysicsSolverBase.cpp:262-263).
- 따라서 Chaos에서 AsyncFixed를 쓰면 EndFrame이 가져오는 GT 상태는 결과 시간에서 이전·다음 결과 사이로 보간된다 (PhysicsSolverBaseImpl.h:51-67 [V]). 결과 시간은 `ExternalTime - AsyncDt × AsyncMultiplier`라서 최신 스텝보다 한 스텝 늦다 (PhysicsSolverBase.h:769-776 [V]). 이것이 UE의 실제 동작이다.
- `p.UseAsyncInterpolation=0`으로 보간을 끄면 고정 dt 누산기도 함께 꺼진다. 즉 "보간을 끈 AsyncFixed"는 Chaos에 존재하지 않는 상태다.
- SDK 백엔드는 마지막 스텝의 상태를 그대로 낸다. 그래서 AsyncFixed run에는 `async_interpolated` 표시를 붙이고, Chaos의 이 모드는 바디별 거동 비교에서 제외한다. 스텝 수(`NumStepsTaken`)와 성능은 비교할 수 있다.

**MinPhysicsDeltaTime**
- MinDeltaTime보다 작은 나머지는 이월하지 않고 버린다 (PBDRigidsSolver.cpp:554-568).
- 하한은 `UE_SMALL_NUMBER`다 (PhysicsSolverBase.h:727-730).
- 이 동작은 그대로 재현한다.

**고정 테스트 값**

| 입력 | 결과 |
|---|---|
| dt 0.05, Sync | 1 × 1/30 |
| dt 0.05, UE5 | 3 × 1/60 (ceil 입력 2.99999989) |
| dt 0.05, UE4 | 2 × 1/60 |
| dt 0.2, UE5 | 6 × 1/60, 0.1 s 소실 |
| Async 1/30, 프레임 1/60 | 스텝 수 0, 1 교대 |

PhysX UE4Compat는 UE4 규칙을 쓰고, 나머지는 모두 UE5 규칙을 쓴다.

**M1 구현 메모 [V]**
- 계획은 UE의 형(type)으로 계산한다. `FChaosScene::SetUpForFrame`의 clamp는 float, `AdvanceAndDispatch_External`의 스텝 수·누산기는 FReal(double)이다 (PhysicsSolverBase.h:1013-1017). 각 스텝은 솔버 루프(PBDRigidsSolver.cpp:560-575)를 그대로 지나므로 `MinDeltaTime` 이하인 스텝은 시뮬레이션되지 않고 그 시간은 이월되지 않는다. UE4 규칙의 `MinDeltaTime` 처리는 같은 방식으로 적용했다 [R].
- `FStepPlan`은 엔진 스텝 수(`NumSteps`)와 함께 UE가 밀어 넣은 스텝 수(`NumPushedSteps`: UE `AdvanceAndDispatch_External`의 지역 변수 `NumSteps`. `FPhysicsSolverBase::ExternalSteps`는 dt > 0인 호출 수를 세는 다른 값이다, PhysicsSolverBase.cpp:623-626 [V]), Native driver가 받는 UE 인자(`FSetUpForFrameParams`), AsyncFixed 표시(`bAsyncInterpolated`)를 갖는다. M1의 이름 `NumExternalSteps`는 UE의 `ExternalSteps`와 뜻이 달라 M2에서 바꿨다.
- **M2:** A10은 `NumStepsTaken`을 `NumSteps`와 대조한다 (엔진이 실제로 진행한 스텝 수). `FStepPlan::InternalStepDeltaTime`(double)을 더했다. Chaos가 실제로 스텝하는 FReal dt다: UE5 substepping은 D/N을 double로, AsyncFixed는 AsyncDt, FixedStep·Sync는 `(double)MDeltaTime`(= `(double)StepDeltaTime`). `StepDeltaTime`은 그 float 값이고 Harness driver가 ABI로 보낸다. 둘은 UE5 substepping에서만 다르다 (dt 0.05: 0.016666666915 대 0.016666667536, 상대 3.7e-8). Native driver 결과의 해석 기준값은 `InternalStepDeltaTime`, Harness driver는 `dt_engine = (double)StepDeltaTime`으로 계산한다. FixedStep·Sync에서는 같으므로 A10의 비트 동일 비교가 성립한다.
- dt 0.2에서 float `6 × (1/60)`은 정확히 반올림 경계(0.1000000052154064)라서 짝수 쪽 0.100000009가 되고, ceil 입력이 6.0000002라 7 > 6이므로 6 × 1/60으로 떨어진다. 반대로 반올림되어도 6 × 1/60이다. 고정값은 두 경우 모두에서 같다.

### 9.3 타이밍 버킷과 헤드라인

**버킷 정의 [MVP M2]**
- `T_push`: ApplyBodyCommands. 명령이 없으면 호출하지 않고 0으로 기록한다. 스텝이 없는 프레임의 flush(Harness driver)도 여기에 든다. 힘 하나는 명령 둘이다 (호출 자리의 `WAKE_UP` + push 때의 힘).
- `T_step`: 4개 호출. 하위 버킷 setup/start/wait/end로 다시 나눈다.
- `T_pull`: ReadBodyStates(ACTIVE_ONLY), 프레임당 1회.
- `T_frame = ΣT_push + ΣT_step + T_pull`.

**헤드라인**
- 정의: 헤드라인 윈도에서 run별 평균 `T_frame`을 구하고, fresh process run들에 걸친 중앙값을 낸다. T=1, frame 0 제외.
- 엔진 비교는 반드시 `T_frame`으로 한다. 이유:
  - Chaos의 `T_step`에는 EndFrame 안의 PT→GT pull, SQ 구조 복사, 이벤트 flip, query material sync가 포함된다 (ChaosScene.cpp:500-576 [V]).
  - SDK의 `T_step`에는 동기화가 없다.
- `T_step`은 보조 열로만 보고하고 "Chaos와 직접 비교 불가" 표기를 붙인다.

**클럭**
- **[MVP] QPC:** `FPlatformTime::Cycles64`는 QPC다. MSVC의 `std::chrono::steady_clock::now()`도 `_Query_perf_frequency`/`_Query_perf_counter`, 즉 QPC를 감싼다 (MSVC 14.44 `__msvc_chrono.hpp:656-658` [V]). 버킷마다 QPC를 읽고, `qpc_hz`를 manifest에 기록한다.
  - QPC 눈금은 이 PC에서 100 ns다. 프레임 단위 버킷에는 충분하다.
  - 작은 씬(`P0`, `P1`)과 호출당 ABI 오버헤드는 여러 프레임 또는 여러 호출을 묶어 배치로 잰다.
- **[확장 M10] TSC:** 버킷마다 `FCycleCounter`(rdtscp + lfence)를 쓴다.
  - run 전체에 걸쳐 QPC로 보정한다 (잔차 < 1e-5).
  - 프레임별 QPC raw 값도 같이 저장한다.
  - 읽기 오버헤드를 측정해 기록한다 (< 30 ns 예상).

**루프 제약:** 타이밍 루프 안에서는 할당, I/O, 로그를 하지 않는다. `test` 빌드의 카운팅 훅으로 검증한다 [확장 M7].
- **M7 구현 [V]:** 벤치 exe가 전역 `operator new`/`delete`(모든 형)를 바꿔 원자 카운터에 센다 (`PhysicsTestBench/Private/AllocationHook.cpp`). manifest `alloc.tick_new_calls`는 `Tick`(타이밍 버킷) 안에서 일어난 `operator new` 호출 수다. 하네스, Kit, 정적 어댑터, SDK의 `operator new`가 모두 이 훅을 지난다. SDK가 자기 할당자로 하는 할당(Jolt `JPH::Allocate`, Bullet `btAlignedAlloc`, PhysX `PxAllocatorCallback`은 `malloc` 계열)은 이 훅에 보이지 않으므로, 백엔드가 셀 수 있으면 `PtMemoryStats.NumAllocs`의 루프 전후 차이를 `alloc.backend_allocs_in_loop`로 따로 적는다 (첫 판은 PhysX·Null만, M7 수정 라운드 0부터 Jolt는 `JPH::Allocate` 계열 함수 포인터의 세는 래퍼, Bullet은 `btAlignedAllocSetCustom`으로 세 SDK 모두. M8에서 고친 문장). `tick_new_calls` > 0이면 헤드라인 자격이 없다. `backend_allocs_in_loop`는 엔진 거동이라 자격에 넣지 않고 기록만 한다. 클럭 읽기 오버헤드는 시작할 때 QPC를 20000번 읽어 `clock.read_overhead_ns`로 기록한다 ([TimingContract.md](TimingContract.md) §2.1).

### 9.4 엔진별 타이밍 구간 [MVP M3·M5, Chaos 행은 확장 M13]

| | SetUpForFrame | StartFrame | WaitPhysScenes | EndFrame | ReadBodyStates (T_pull) | Prepare (비타이밍) | bench 모드에서 금지 |
|---|---|---|---|---|---|---|---|
| Jolt | dt 저장, 변경 시 SetGravity | 쌓아 둔 임펄스·힘 적용 + Update(dt, 1, Temp, Jobs) | - | update error 반환 | NoLock + GetActiveBodiesUnsafe | OptimizeBroadPhase, temp 블록 pre-fault | ContactListener, ActivationListener, DebugRenderer, profiler |
| PhysX | dt 저장, 변경 시 setGravity | 쌓아 둔 명령 적용 + simulate(dt, nullptr, scratch, 262144) | fetchResults(true) | 이벤트 (viz만) / error bits | getActiveActors (SdkDefault는 `bEnableActiveActors=False`라 전체 순회) | scratch 블록 pre-fault (M5) | eNOTIFY_* 플래그, event callback, PVD |
| Bullet | dt 저장, 변경 시 setGravity (잠든 바디 포함) | 쌓아 둔 명령·kinematic 목표 적용 + stepSimulation(dt, 0, dt) (내부 pre-tick: UE 순서 감쇠, 속도 한계, kinematic 속도) | - | post-tick 집계 / 풀 fallback bit | COM TM × inv(ComLocal) (dynamic 바디), isActive | 없음 | debug drawer (pre-/post-tick은 UE 모델의 일부라 bench에서도 돈다, M5) |
| Chaos | FChaosScene::SetUpForFrame (중력은 UE처럼 매 프레임) | StartFrame | WaitPhysScenes | EndFrame | GT API X/R/V/W | 사용자 정의 (기록) | sim callback (PHASES 제외), CVD, debug draw (Test에서는 컴파일되지 않음) |

**공통 규칙**
- 바디와 머티리얼은 한 배치로, 하네스 순서대로 생성한다.
- 용량은 힌트로 넉넉하게 잡는다. 넘치면 run을 실패로 처리한다 (G12). 힌트는 시나리오가 선언한 개수로 `FPhysSceneInitParams::MaxBodiesHint`/`MaxBodyPairsHint`/`MaxContactConstraintsHint`를 채우고 `PtSceneDesc`로 넘어간다 (M2 수정 라운드 1에서 배관. Jolt `PhysicsSystem::Init`이 M3에서 처음 쓴다).

### 9.5 워밍업과 윈도

- 워밍업 스텝은 두지 않는다. 시뮬레이션 후 리셋하면 warm-start 상태가 엔진마다 다르게 오염되기 때문이다.
- 순서:
  1. `PrepareForSimulation`: 모든 엔진에 같은 비타이밍 기회를 준다 [MVP].
  2. T개 코어 전부에서 0.5 s spin pre-roll [확장 M7].
  3. frame 0: 따로 측정해 `T_frame0`으로 보고한다 [MVP].
- 윈도는 씬이 선언한다. 예: `M3.BoxPile` headline 1-599, impact 1-179, settled 300-599.
- 초기화 단계(`backend_init`, `scene_create`, `prepare`, `frame0`, `teardown`)는 각각 별도로 기록한다.

### 9.6 스레드 매핑

- T = affinity mask에 든 논리 CPU 수다. P코어당 1개만 쓴다.
- 마스크: 0x1, 0x5, 0x15, 0x55, 0x155, 0x555 (i5-13600KF 논리 0, 2, …, 10 [V]).
- 헤드라인은 T=1이다. MVP에서는 비공식 수치만 낸다 (체크포인트 A). 측정 프로토콜은 [확장 M7]에서 갖추고, 공식 수치는 적합성 스탬프가 생기는 [확장 M10] 이후에만 낸다 (R9). T=1..6 스케일링은 [확장 M7].

| 엔진 | T=1 | T≥2 |
|---|---|---|
| Jolt | 워커 0 | 워커 T-1 (호출 스레드도 참여) |
| PhysX | `FPhysXCPUDispatcherSingleThread` (inline, LIFO) | `PxDefaultCpuDispatcherCreate(T)`: 워커 스레드 T개를 만든다 (ExtDefaultCpuDispatcher.cpp:51-73 [V]). 호출 스레드는 `fetchResults(true)`에서 기다리기만 한다고 본다 [R]. 워커 수를 T와 T−1 중 무엇으로 할지는 cpu_eff로 정한다 (M10) |
| Bullet | ST | 해당 없음 (BulletMt는 M15 백로그) |
| Chaos | `SetThreadingMode_External(SingleThread)` + `p.Chaos.SingleWorkerPhysics=1` (선택: `p.Chaos.DisablePhysicsParallelFor=1`) + `-corelimit=1` | TaskGraph + `-corelimit=T` + `p.Chaos.MaxNumWorkers=T` |

**Chaos 스레드 근거 [V]**
- SingleThread 스레딩 모드는 솔버 tick을 호출 스레드에서 돌릴 뿐이다. 내부 `PhysicsParallelFor`가 한 스레드로 떨어지는 조건은 `bSingleWorkerPhysics || bDisablePhysicsParallelFor || bForceSingleThreaded`다 (Parallel.cpp:95; CVar 정의 :37). 그 밖에는 PreInit이 만든 TaskGraph 워커로 나간다.
- 제약 솔버와 섬 그룹도 `FApp::ShouldUseThreadingForPerformance() && !bSingleWorkerPhysics`로 워커 수를 정한다 (ConstraintGroupSolver.cpp:250, SolverPartitionManager.cpp:210, IslandGroupManager.cpp:67, PBDRigidsEvolutionGBF.cpp:250).
- `p.Chaos.MaxNumWorkers`(Parallel.cpp:16,33, 기본 100)는 워커 사용 수와 MinBatchSize의 상한일 뿐이다. 스레드 풀 크기는 PreInit과 `-corelimit`가 정한다.
  - `-corelimit=T`는 물리·논리 코어 수 보고값을 T로 제한한다 (GenericPlatformMisc.cpp:1930-1946 [V]). Windows가 만드는 워커 수의 기준값은 `max(T − 1, 2)`다 (`NumberOfWorkerThreadsToSpawn`, WindowsPlatformMisc.cpp:2719-2739 [V]. 우선순위 세트별로 몇 배가 되는지는 [R]).
  - 그래서 `-corelimit=1`이어도 워커 2개가 남는다. T=1에 `p.Chaos.SingleWorkerPhysics=1`이 반드시 필요한 이유다.
- 이 값들을 effective 키 `threads.single_worker`, `threads.max_num_workers`로 기록하며, Chaos 필수 키다 (§10). T=1이 실제로 한 코어에서 돌았는지는 G10b가 증명한다 (M10).

- 엔진 고유의 스레드 설정값을 T 옆에 함께 기록한다.
- HT(12)와 E코어 포함(20) 시리즈는 별도로 플래그를 단다.

**M7 구현 메모 [V]**
- 마스크는 고정 상수가 아니라 `GetLogicalProcessorInformationEx`로 읽은 토폴로지에서 만든다 (`FProcessorTopology::MakeThreadMask`: 효율 등급이 가장 높은 코어마다 첫 논리 CPU를 T개). 이 PC에서는 위 표의 마스크와 같다 (CTest `PhysicsTest.Unit.Bench`의 `ThreadMasksFromTopology`). `-Affinity=`를 주면 그 마스크의 비트 수가 T와 같아야 한다 (다르면 종료 코드 2).
- PhysX T ≥ 2는 `PxDefaultCpuDispatcherCreate(T)` 그대로다 (워커 T개). effective `threads.workers`와 `physx.cpu_dispatcher`는 T = 1이면 0, T ≥ 2면 T이고, G9 규칙 `profile:dispatcher_workers`가 비교한다. Bullet에 T ≥ 2를 주면 종료 코드 2다 (`PhysicsTest.Bench.ExitBulletThreads`).
- manifest `threads`에 `T`, `affinity_mask`, `logical_cpus`, `engine_native`(백엔드의 스레드 키), `scaling_note` "cpu_eff 미검증 (M10)"을 적는다. T = 1..6 스케일링 수치는 모두 비공식이다.
- 시리즈 플래그 (M7 수정 라운드 0): `threads.series`가 마스크의 모양을 토폴로지에서 적는다. `p_cores_one_lp`(P코어마다 논리 CPU 하나, 기본 T 시리즈), `p_cores_smt`(한 코어의 SMT 쌍이 마스크에 있음: HT 시리즈), `with_e_cores`(E코어가 마스크에 있음). `threads.cores`, `smt_pair_cores`, `e_cores`도 함께 쓴다. `-Affinity=`로 HT·E코어 시리즈를 돌려도 M8이 마스크를 다시 풀지 않는다.

### 9.7 Chaos 보고 수준 (수준 간 비교는 하지 않음) [확장 M13]

| 수준 | Chaos 측정값 | 비교 대상 | run 종류 |
|---|---|---|---|
| Frame (헤드라인) | `Chaos.Game`의 T_frame | 다른 엔진의 T_frame | bench, instr 없음 |
| Solver | Presimulate→PostSolve sim callback 구간 | 다른 엔진의 T_step | bench + PHASES (진단) |
| 통합 비용 | frame − solver, 그리고 P0/P1/M3에서 맞춘 t = a + b·N | - | 파생값 |
| Evolution (L0) | `Chaos.Evolution` (FPBDRigidsEvolutionGBF) | 다른 엔진의 T_step | 진단 |

## 10. 프로필 시스템

**축** (모든 run에 기록)
- 물리 프로필: UEParity / Normalized [MVP M1]
- 백엔드 변형 [MVP M5, 완성 확장 M9]
- 플래그 프로필 (빌드 속성)
- 질량 규칙: `EMassRule` UEBodyUtils(기본) / Physical. 별도 축으로 둔다.
  - 01 문서 Q4는 "Normalized는 물리적 밀도"를 권장했다. 이 설계는 질량 규칙을 물리 프로필에 묶지 않는다. 묶으면 UEParity와 Normalized를 비교할 때 질량까지 바뀌어 원인을 분리할 수 없기 때문이다.
  - 방법론 씬은 `bOverrideMass`로 질량을 고정하므로, 엔진 간·프로필 간 질량은 항상 같다. `M7.UEDefaults` 씬이 ^0.75 규칙의 효과를 따로 보여준다.
- tick mode
- 오버라이드

**ini 계층**

`BasePhysicsTest.ini` → `Profiles/<Profile>.ini` → `Profiles/Backend/<Engine>.<Variant>.ini` → 씬 고정값 → 커맨드라인 `-ini:` / `-ExecCmds`.

- `BasePhysicsTest.ini`는 UE 기본값을 담는다. UE 게임처럼 모든 UE 기본 키(코어 필드 DefaultGravityZ, SolverOptions 등 포함)를 `[/Script/Engine.PhysicsSettings]` 한 섹션에 둔다 (BaseEngine.ini:3406 [V]). 사용자가 자기 프로젝트의 DefaultEngine.ini 값을 그대로 붙여 넣을 수 있다.
- Chaos 브리지는 이 값을 CDO에 의존하지 않고 명시적으로 적용한다 (§15.4). Engine 없이 도는 DLL에서는 UPhysicsSettingsCore CDO가 `[/Script/PhysicsCore.PhysicsSettingsCore]`를 읽기 때문이다.

**섹션**
- `[/Script/Engine.PhysicsSettings]`: UE 기본값.
- `[/Script/PhysicsTest.PhysicsTestSettings]`: 하네스 전용 설정 (tick mode, FixedDeltaTime, SubstepClampRule, RunMode, HashEvery 등).
- `[/Script/PhysicsTest.BodyInstanceDefaults]`, `[...PhysicalMaterialDefaults]`: 시나리오 작성 **전에** 템플릿에 적용한다.
- `[/Script/PhysicsTest.ForcedOverrides]`: 작성 **후에** 적용하고 로그를 남긴다 [확장 M9].
- `[ConsoleVariables]`: 하네스 CVar로 쓰고, Chaos에는 SetOption으로 그대로 전달한다.
- `[PhysicsTest.Backend.<Engine>]`: 엔진 전용 키.

**리졸버 (`FProfileResolver`) [MVP M1]**
- 모든 값에 Origin을 붙인다: UEDefault / Profile / Variant / ScenePinned / CommandLine / Forced.
- 변형 ini가 바꿀 수 있는 것은 두 가지뿐이다:
  - 자기 엔진 키
  - 물리 프로필이 `EngineDefault`로 표시한 키
- 그 밖의 키를 바꾸면 로드 오류다. 변형이 정규화를 조용히 되돌리지 못하게 하기 위해서다.
- 백엔드가 모르는 키는 거부한다.
- **M1 구현 메모 [V]:**
  - 값의 정규 키는 `<접두어>.<키>`다: `PhysicsSettings.`, `PhysicsTestSettings.`, `BodyInstanceDefaults.`, `PhysicalMaterialDefaults.`, `ConsoleVariables.<cvar>`, `Backend.<Engine>.`(`.Init`). 프로필은 `[PhysicsTest.Profile]`의 `+EngineDefaultKeys=<정규 키>`로 변형에 연다.
  - 모델링한 키는 모두 구조체 속성 표를 거쳐 파싱·재출력하므로 형식이 달라도(`-980.000000`과 `-980.0`) 같은 값이다. UE 섹션의 모르는 키는 Origin과 함께 기록만 하고(`bApplied=false`, 경고) 하네스 섹션의 모르는 키는 오류다. 스칼라 키에 배열 연산자(`+ . - !`)를 쓰면 오류다.
  - `[ConsoleVariables]`: 하네스 물리 CVar와 Base·프로필이 설정한 CVar는 고정 키다. 그 밖의 CVar(예: Chaos CVar)는 엔진 키로 보고 변형이 쓸 수 있다.
  - 하네스 물리 CVar는 `PhysicsCVars::GetHarnessConsoleVariables()`의 고정 표다 (`p.ComNudgeAffectsInertia`, `p.SensitiveSleepThresholdMultiplier`, `Physics/PhysicsConsoleVariables.cpp` 한 TU). 리졸버는 이 표만 하네스 CVar로 본다: 값 검증·정규화, 변형 금지, `physics_sha256` 포함, 내장 기본값 층. 프로그램이 따로 등록한 CVar(뷰어 CVar, 테스트의 `pt.Test.*`)는 리졸버에게는 엔진 CVar다. `ApplyConsoleVariables`만 그 프로세스에 등록된 CVar이면 설정한다. 그래서 같은 프로필은 어느 프로그램에서나 같은 값·같은 해시가 된다 (M1 수정 라운드. 전에는 프로세스에 등록된 모든 CVar로 시드해 링크 구성에 따라 해시가 달라졌다. CTest `PhysicsTest.Probe.HarnessCVars`가 BodyUtils를 링크하지 않는 프로그램과 `PhysicsTestAutomation`의 해시를 대조한다).
  - `ApplyConsoleVariables`는 Set이 거부되면(못 읽는 값, 읽기 전용, 이미 더 높은 우선순위로 설정됨) 거부된 CVar마다 현재 값과 SetBy를 담아 false를 돌려준다. 섹션 이름은 UE처럼 대소문자를 무시한다. 숫자 값은 유한수만 받는다 (`inf`/`nan`은 로드 오류).
  - `physics_sha256`: 적용된 물리 키 중 프로필이 고정한 것만 넣는다. EngineDefault 키, 엔진 키, 실행 옵션(스레드 수, run mode, 해시 주기), 모르는 UE 키는 뺀다.
  - 커맨드라인은 UE 문법이다: `-ini:PhysicsTest:[Section]:Key=Value,...`(UE 철자인 `-ini:Engine:`도 받는다, ConfigCacheIni.cpp:2413-2500)와 `-ExecCmds="cvar value, ..."`. CVar 적용 우선순위는 ini가 `ECVF_SetByConsoleVariablesIni`, `-ExecCmds`가 `ECVF_SetByConsole`이다. **M2:** `-ini:`로 준 CVar도 ini 우선순위다 (M1 구현은 Origin이 CommandLine이면 모두 Console로 적용했다. `FProfileResolver::GetConsoleVariableSetBy`가 OriginName으로 고른다). `-ExecCmds` 안의 `name=value`는 하네스만 받는 형식이다 (§5.2 #32).
  - **M2:** 하네스 물리 CVar는 프로세스 전역이지만 `FPhysScene`이 생성될 때 자기 설정의 값(없으면 기본값)으로 맞추고, `SpawnBody`·`FlushDeferredActors`에서 그 뒤로 바뀌지 않았는지 확인한다 (§5.2 #37). 동시에 살아 있는 씬들은 이 값이 같아야 한다 (뷰어 2단계 M12의 나란히 보기는 한 프로필이므로 같다).
  - Normalized의 "최대 각속도 해제"는 0이 아니라 `bOverrideMaxAngularVelocity=True`, `MaxAngularVelocity=1e7`(deg/s, 약 1.75e5 rad/s)이다. Chaos는 `MaxAngularSpeedSq`로 무조건 clamp하므로 0이면 회전이 멈춘다 (PBDRigidsEvolutionGBF.cpp:950-954 [V]). `UE_BIG_NUMBER`(3.4e+38)도 쓰지 않는다: rad/s로 바꾼 5.9e36을 엔진이 float로 제곱하면 +inf가 되어 (Chaos `MMaxAngularSpeedSq`는 `FRealSingle`, ParticleDirtyFlags.h:579, :646. Jolt `Square(mMaxAngularVelocity)`, MotionProperties.inl:39. PhysX `PxReal maxAngularVelocitySq`, PxvDynamics.h:74 [V]) readback, effective 설정 대조, 차이 계산에 inf가 들어간다. 1e7 deg/s는 제곱해도 float로 유한하고 도달할 수 없는 속도다 (M1 수정 라운드에서 고침).
  - 위 표의 행을 키로 옮기면서 `FPhysicsTestSettings`에 `bAllowSleeping`(sleep 축)과 `RestitutionThresholdVelocity`(cm/s, 0 이하이면 엔진별 UEParity 매핑)를 더했다. "Average 강제, SF := F"를 머티리얼마다 강제하는 `ForcedOverrides`는 M9이고, M1 초안은 프로젝트 combine 모드만 Average로 둔다.

| 항목 | UEParity | Normalized |
|---|---|---|
| 중력 | (0,0,-980) | 동일 |
| damping | 0.01 / 0 (Bullet은 UE 순서를 pre-tick으로 에뮬레이트, §8.1) | 0 / 0 |
| 최대 각속도 | 62.83 rad/s (Bullet은 에뮬레이트) | 해제 (Bullet은 속도를 clamp하지 않지만 스텝 회전이 π/4(60 Hz 47.1 rad/s)를 넘으면 회전각이 줄어든다: `bullet.angular_motion_threshold` COMPILE_TIME, 넘은 수 `bullet.angular_motion_limit_hits`. M5 정정, §8.4) |
| 최대 선속도 | 해제 (UE에 제한 없음, Jolt 500 m/s 해제) | 해제 |
| combine | 머티리얼 모드, 기본 Average (Jolt·Bullet은 콜백). 정지마찰은 Chaos 규칙 (§5.1). Jolt·Bullet은 동적 마찰만 | Average 강제, 정지마찰 = 동적마찰 (SF := F) |
| restitution threshold | Chaos 네이티브 1000·dt / PhysX-UE4 200 cm/s / Jolt·Bullet은 Chaos 등가 1000·dt cm/s (사용자 확인 Q11, [04_DecisionLog.md](04_DecisionLog.md) §4) | 모든 엔진 50 cm/s (Chaos는 `p.CollisionRestitutionThreshold`를 50/dt로, 단위가 가속도라서, PBDCollisionConstraints.cpp:69 [V]) |
| sleep | on, UE 머티리얼 임계값 매핑 (근사는 플래그) | off: PhysX `PxSceneFlag::eDISABLE_SLEEPING` (PxSceneDesc.h:359, NpRigidDynamic.cpp:521 [V]; sleep 장부 비용도 사라짐), Chaos `p.Chaos.Solver.Sleep.Enabled=0` (IslandManager.cpp:53 [V]) 또는 파티클별 `ESleepType::NeverSleep` (IslandManager.cpp:240 [V]), Jolt `BodyCreationSettings::mAllowSleeping=false`, Bullet `DISABLE_DEACTIVATION`. G6로 검증 |
| 질량 | 기본 UEBodyUtils. 방법론 씬은 bOverrideMass로 고정 | 동일 (혼동 방지) |
| gyroscopic | off (Bullet 기본 on을 끔). PhysX UE4Compat 계열은 켜도 무시한다 (`GyroscopicForces=Ignore`: UE4의 PhysX 3.4에 항이 없다, §8.2 M6 메모) | off |
| CCD | off (Jolt·Chaos speculative는 by-design) | off |
| 반복 횟수 | 엔진 UE 매핑 (PhysX-UE4 8/1, Chaos 8/2/1) 또는 네이티브 (Jolt 10/2, Bullet 10) | 동일. Mode C sweep은 M15 백로그 |
| inertia conditioning | on | off |

**Effective readback**
- `CreateScene` 이후 `DumpEffectiveSettings`를 호출한다. 각 키의 Source는 READBACK / SET_ONLY / COMPILE_TIME / ENGINE_DEFAULT 중 하나다 [MVP: Null M2 (A7), Jolt M3, Bullet·PhysX M5]. run 출력 `effective_settings.jsonl`은 M3 최소 벤치부터 쓰고, preflight 통합은 [확장 M7]이다.
- 바디 질량, 관성, COM은 첫/중간/마지막 dynamic 바디를 `ReadMassProperties`로 샘플한다.
- READBACK 값이 하나라도 어긋나면 G9가 실패한다 [MVP M6]. Preflight는 그런 run을 시작 전에 거부한다 (exit 2) [확장 M7]. SET_ONLY 키는 manifest에 "미검증"으로 기록한다.
- **필수 키는 두 집합이다.** A7이 둘 다 검사한다.
  - **일반 집합 (모든 백엔드):** world.gravity, solver.type, solver.iterations, broadphase.type, contact.restitution_threshold, contact.offset_rule, contact.speculative_distance, material.friction_combine, material.restitution_combine, material.static_friction_rule, body.damping_formula, command.nearly_zero_velocity_wakes (M6 수정 라운드 1: §9.1 결정 1의 nearly zero 속도 쓰기가 깨우면 1, UE·Chaos처럼 잠든 채 두면 0), body.damping, body.max_angular_velocity, body.max_linear_velocity, body.gyroscopic, sleep.enabled, sleep.criteria, ccd.enabled, threads.workers, determinism.flag, shape.box_margin, shape.convex_radius, shape.wrapping, listeners.installed
    - 해당 개념이 없는 백엔드는 `native="n/a"`, Source=COMPILE_TIME으로 낸다 (예: Null의 broadphase.type).
  - **백엔드 집합 (`PtBackendInfo.RequiredEffectiveKeys`로 선언):**
    - PhysX: physx.pcm, physx.stabilization, physx.scratch, physx.tolerances, physx.active_actors, physx.filter_shader, physx.dispatcher
    - Jolt: jolt.config_string, jolt.temp_allocator_bytes
    - Bullet: bullet.max_angvel_compile_time, bullet.debug_drawer_attached
    - Chaos: threads.single_worker, threads.max_num_workers, chaos.threading_mode, chaos.async_dt, chaos.use_async_interpolation

## 11. 디버그 드로 파이프라인과 뷰어

### 11.1 드로 파이프라인 [MVP M4]

```
after EndFrame, only on viz/capture frames, untimed
  CaptureDebugSnapshot -> ReadDebugSnapshot -> FPhysicsDebugSnapshot (cm, Chaos conventions)
  FIslandBuilder   union-find over contacts with Phi<=0 or a solver impulse (ACTIVE, M5) + joints, statics excluded,
                   id = lowest body index, deterministic
  DebugDraw::DrawAll, in the order of FPBDRigidsSolver::PostTickDebugDraw (PBDRigidsSolver.cpp:3417-3546 [V]):
    DrawParticleShapes -> DrawCollisions -> DrawParticleMass -> DrawParticleBounds -> DrawParticleTransforms
    -> DrawConstraintGraph (islands) -> DrawSleepState (UE: FPBDIslandManager::DebugDrawSleepState)
    -> DrawCollidingShapes -> DrawJointConstraints
  -> FDebugDrawQueue (Chaos::FDebugDrawQueue signatures; capture stage = FChaosDDFrame: budget p.Chaos.DebugDraw.MaxLines
     20000, region radius 3000 cm)
  -> VisitCommands -> FDebugDrawRenderStage (render stage = FChaosDDRenderer: same budget and region again)
  -> IDebugDrawRenderer -> FD3D11DebugDrawRenderer
```

- 섬 드로어 이름은 UE와 같은 `DrawConstraintGraph`다. CVar는 UE처럼 `p.Chaos.Solver.DebugDrawIslands`다.
- UE 함수에는 Density, BVH, cluster 드로가 사이사이에 있지만 Phase 1에서는 그리지 않는다. 순서는 위 목록으로 고정한다.

**기본값과 색** [V]

솔버 기본값 (PBDRigidsSolver.cpp:219-247):

| 항목 | 값 | 항목 | 값 |
|---|---|---|---|
| LineThickness | 1 cm (world space, 0이면 1 px) | PointSize | 5 px |
| ArrowSize | 10 | BodyAxisLen | 30 |
| ContactLen | 30 | ContactWidth | 6 |
| ConstraintAxisLen | 30 | DrawPriority | 10 |

색 규칙:
- **상태색:** Dynamic (255,255,0), Sleeping (128,128,128), Kinematic (0,128,255), Static (255,0,0). Bounds는 더 어두운 표를 쓴다.
- **접촉:** plane 원 r=6 (Body1), point 원 r=3, normal 30. active (250,0,0), inactive (150,150,150)은 길이 ×0.75.
- **섬:** 10색 팔레트, 두께 3배. 잠든 섬은 Black.
- **조인트:** White 연결선, Red는 frame a, Cyan은 frame b, Magenta는 stretch. Chaos에서 a는 particle[1]이다.

**테셀레이션 (UE line batcher)**

| 프리미티브 | 선분 수 |
|---|---|
| box | 12 |
| sphere | 8×8 = 128 |
| capsule | 16변, 68 |
| circle | 8 |
| arrow 머리 | √ArrowSize |

**패스 순서**
1. 클리어: sRGB (40,42,46). 검정이 아닌 색이어야 Black 요소가 보인다.
2. grid: 100 cm 셀, major 10, ±5000 cm, depth test.
3. 선: foreground pass에서 `CF_Always`로 그린 다음 `CF_DepthNearOrEqual`로 다시 그린다 (SeeThrough=true).
4. 점.
5. ImGui 텍스트.

바닥은 static box로 만든다. Chaos는 plane을 그리지 않기 때문이다.

**`IDebugDrawRenderer`는 `IChaosDDRenderer`(ChaosDDRenderer.h:19-44)를 미러링한다**
- IsServer, GetDrawRegion
- Render 함수 9개: Point, Line, Arrow, Circle, Sphere, Capsule, Box, Triangle, String
- RenderCircle은 축 행렬을 받는다.
- RenderString에는 bDrawShadow가 있다.
- 메서드 목록과 인자 순서는 원본 헤더와 일치함을 이미 확인했다 [V]. 남은 확인 작업은 없다.
- 두 가지 의미를 고정한다:
  - **RenderCapsule의 HalfHeight는 반지름을 포함한다.** Chaos는 `Scale.Z × (0.5 × Height + Radius)`를 넘긴다 (DebugDrawImplicitObject.cpp:62 [V]). ABI 캡슐은 `HalfLength`(선분 절반)이므로 `HalfHeight = HalfLength + Radius`다. 단위 테스트로 고정한다.
  - **RenderBox의 Size는 half extent다.** Chaos 호출부가 `0.5 × Scale × Extents`를 넘긴다 (DebugDrawImplicitObject.cpp:73-76 [V]).
  - 그래서 `PtDebugDrawCmd.Extent`는 box = half extent, capsule = (R, 반지름을 포함한 HalfHeight, 0)이다.
- `RenderLatentCommand`는 Chaos 브리지(UE 측)가 구현해 `PtDebugDrawCmd`로 직렬화한다 [확장 M13].

CVar 이름은 Chaos와 같다: `p.Chaos.DebugDraw.Enabled`, `p.Chaos.Solver.DebugDrawShapes/Collisions/Bounds/Transforms/Islands/SleepState/Joints/Mass/CollidingShapes`, `p.Chaos.Solver.DebugDraw.<Setting>`, `ShowInactiveContacts`, `SeeThrough`, `Radius`, `MaxLines`.

### 11.2 뷰어 (`FPhysicsTestViewerApp`)

**1단계 [MVP M4]**: 첫 엔진(Jolt) 슬라이스 바로 다음에 만든다. Chaos를 작성하기 전에 Bullet·PhysX·Jolt가 도는 모습을 보기 위해서다. M5에서 Bullet과 PhysX가 레지스트리에 들어오면 코드 변경 없이 선택할 수 있다.
- **구조:** FEngineLoop 단계를 따른다 (PreInit / Init / Tick / Exit). 물리는 메시지 루프 스레드에서 돈다. 문제가 생기면 sim 전용 스레드로 옮겨도 ABI는 바뀌지 않는다.
- **씬:** 한 번에 하나. Engines 패널에서 백엔드와 변형을 바꾸면 씬을 다시 만든다.
- **조작:** 일시정지, 한 스텝 진행, 리셋, 스크린샷 (동기 PNG 저장).
- **카메라 (UE 에디터 규약):**
  - hFOV 90°를 major 축에 고정, near 10 cm, reversed-Z + 무한 far
  - 뷰 기저 교환
  - RMB+WASD로 이동, E/Q는 월드 상하, C/Z는 FOV
  - 비행 중 휠은 속도 ±10%, 가속 20000 cm/s², 감쇠 10/s
  - F는 선택 프레이밍, Alt+LMB 궤도 [R]
- **패널:** Engines, Inspector (Outliner와 Details를 합친 패널), Stats (`stat ChaosCounters` 이름 사용), Console (CVar/stat).
- **타이밍 표기:** 뷰어가 보여주는 모든 시간에는 "viz mode, not a benchmark"를 붙인다. 뷰어에서 나온 manifest는 mode=viz이며 분석 단계가 거부한다.
- **성능 기준:**
  - 1600×900, RTX 3080에서 시뮬레이션을 멈춘 상태로 ≥ 60 fps.
  - 스냅샷 + 드로의 CPU 시간은 씬당 ≤ 4 ms (`M3.BoxPile` 10k). 연속 부하(`-Headless` 또는 `-NoVSync`)에서 재고 Stats에 표시한다. 창 모드 vsync의 Stats 값은 판정에 쓰지 않는다 (M4 [V]: vsync를 기다리며 쉬는 스레드는 같은 일을 2~3배 느리게 한다).
  - 라이브 시뮬레이션의 fps는 보고만 하고 게이트로 쓰지 않는다. 물리가 메시지 루프 스레드에서 돌고, Bullet ST만 해도 3008 박스에서 스텝당 중앙값 18.1 ms가 걸리기 때문이다 [V build-bullet].

**M4 구현 메모 [V]** ([Progress.md](Progress.md) M4)
- **파이프라인 함수:** `DebugDraw::CaptureSnapshot`(백엔드 스냅샷. 스냅샷이 없는 백엔드(Null)는 호스트가 당겨 둔 바디 상태로 파티클을 만들고 `bFromHostState`를 켠다) → `FIslandBuilder::Build`/`GatherIslands`(접촉을 캡처한 프레임만) → `DebugDraw::DrawAll`(`FDrawContext`: 씬, 스냅샷, 설정, 플래그, 바디 → 파티클 표, 섬 목록) → `FDebugDrawQueue::ExtractAllElements` → `VisitCommands`(→ `FDebugDrawRenderStage`) → `FD3D11DebugDrawRenderer`. 뷰어에서는 `FPhysicsViewportClient::BuildFrame` 한 함수가 이 순서로 부르고 단계별 CPU 시간을 잰다.
- **영역과 예산 (수정 라운드 0에서 5.8.3 모델로 고침):** 5.8.3은 새 큐가 기본이고(`bChaosDebugDraw_UseNewQueue = true`, DebugDrawQueue.cpp:10 [V]) 판정이 두 단계다. **캡처 단계**(`FDebugDrawQueue`, UE `FChaosDDFrame`): 솔버 드로어는 ChaosDebugDraw.cpp:151-245의 파일 지역 래퍼를 거쳐 `FChaosDDPrimitives`로 가므로 비용은 점·선·화살표·원 1, 구 64, 캡슐 16, 박스 12, 삼각형 3, 문자열 10이고, 명령마다 **자기 bounds**로 영역을 본다 (DebugDrawPrimitives.cpp:11-120 [V]. 박스 bounds는 넘긴 half extent의 절반 크기, 캡슐 bounds는 축 선분뿐이라는 UE 특성까지 따른다). 셰이프는 `FChaosDDParticle::DrawShapes` 경로라 셰이프 하나에 비용 1, 파티클 bounds로만 거른다 (PBDRigidsSolver.cpp:3445-3453, DebugDrawParticle.cpp:283-349 [V]). 영역은 반경 0이면 어디서나, 아니면 bounds와 중심의 제곱 거리 < W², 예산 0은 무한이고 거부된 명령의 비용도 누적한다 (ChaosDDFrame.h:106-149 [V]). 레거시 API로만 있는 `DrawDebugCoordinateSystem`은 위치 판정·비용 3의 latent 명령이다. **렌더 단계**(`FDebugDrawRenderStage`, UE `FChaosDDRenderer`, ChaosDebugDrawSubsystem.cpp:50-246 [V]): 같은 `MaxLines`를 렌더 예산으로 다시 쓰고 비용은 점·선 1, 화살표 3, 원 8, 구 16, 캡슐 16, 박스 12, 삼각형 3, 문자열 10이다. 영역은 제곱 거리 <= W²이고, `TryAddToCost`가 수락한 명령의 비용을 두 번 더하는 UE 동작도 그대로 따른다 (박스 하나가 예산 24를 쓴다). latent 명령은 먼저, 예산 밖에서 그린다. 하네스가 다른 점은 하나다: UE 렌더 단계의 영역 판정에는 W > 0 조건이 없어 `Radius 0`("0 means infinite")이면 카메라를 bounds 안에 품은 명령만 그려지는데, 하네스는 두 단계 모두 W = 0을 어디서나로 둔다. 영역 중심은 카메라 위치다 (ChaosDebugDrawSubsystem.cpp:411-417 [V]). SleepState·CollidingShapes는 `DrawShapesImpl` 경로라 셰이프마다 자기 bounds로 먼저 거르고, 접촉은 두 파티클 bounds로 거른다 (ChaosDebugDraw.cpp:910, 1437-1445 [V]). 기본값(MaxLines 20000, Radius 3000 cm)이면 `M3.BoxPile` 1k도 렌더 예산 때문에 박스 833개만 그려지고(1001 × 24 > 20000) 그 뒤의 접촉·bounds는 그려지지 않는다. 그래서 Stats가 두 단계의 비용·거부 수와 UE의 "Capture / Render Budget Exceeded" 문구를 보여 주고, 하나도 안 그려지면 뷰포트 가운데에 안내를 띄우고, Show 메뉴에 "Draw everything (MaxLines 0, Radius 0)"을 뒀다.
- **원과 구:** 원 명령은 `DrawCollisionImpl`처럼 12분할을 기록하지만 비용은 분할 수와 무관하다 (캡처 1, 렌더 8). `IDebugDrawRenderer::RenderCircle`에는 분할 인자가 없고, UE의 ChaosDD 렌더러도 원과 구를 `SphereSegments(8)`로 그린다 (ChaosDebugDrawSubsystem.cpp:58, :133-154 [V]). 그래서 원은 8선분, 구는 128선분이다.
- **CVar 기본값:** A.11 스케치대로 `p.Chaos.DebugDraw.Enabled`, `p.Chaos.Solver.DebugDrawShapes`, `p.Chaos.Solver.DebugDrawJoints`가 1이다 (UE는 0이고 콘솔로 켠다). 나머지는 UE 값이다.
- **섬:** 간선은 Phi ≤ 0 접촉과 씬의 조인트다. static뿐 아니라 kinematic도 섬을 잇지 않는다 (Chaos의 섬 노드는 dynamic 파티클이다). 간선이 하나도 없는 dynamic 바디는 섬이 없다 (Chaos의 isolated particle, IslandManager.cpp:4194-4224 [V]). 섬 색 번호는 id 정렬 목록의 위치다 (UE `GetIslandColor`의 인자가 섬 배열 위치인 것과 같다). Jolt 스냅샷은 잠든 섬의 접촉을 `PT_CONTACT_SLEEPING`으로 유지하므로 UEParity에서 잠든 섬이 Black으로 남는다 (§8.3).
- **조인트:** Chaos의 a = particle[1], b = particle[0]을 `FConstraintInstance::Body2`, `Body1`로 둔다 (PROVISIONAL(M9)). 기본 기능(actor connector + stretch, ChaosDebugDraw.h:179-185 [V])만 그린다. b 쪽 간격도 a의 길이(`Lena`)로 나누는 UE 코드(ChaosDebugDraw.cpp:1869 [V])까지 따랐다.
- **SleepState:** 스냅샷에 매끄럽게 한 속도(VSmooth)가 없어 현재 속도를 쓴다. 임계값은 바디 머티리얼의 sleep 임계값 × sleep family 배수이고, 프로필이 sleep을 끄면 임계값이 없다 (회색). 섬이 없는(고립된) 잠든 바디는 그리지 않는다: UE의 고립 루프는 활성 파티클 보기(`GetActiveDynamicMovingKinematicParticlesView`)를 도는데 잠든 파티클은 활성 배열에서 빠진다 (PBDRigidsSOAs.cpp:50-57, PBDRigidsSOAs.h:722-749, IslandManager.cpp:2712 [V]. 수정 라운드 1).
- **그리지 않는 것:** convex 요소(Phase 1 씬에 없다. Stats가 개수를 낸다), 접촉의 InitialPhi 원과 restored·replaced 정보 상자(스냅샷에 데이터가 없다), contact graph 선(UE 기본 꺼짐).
- **렌더러:** 위치는 카메라 상대 float로 올린다. 선 폭은 끝점마다 `max(LineThickness × 픽셀/cm, 1 px)`를 VS에서 정한다. 박스·구·캡슐·원은 단위 메시 인스턴스(80 B), 선과 점은 구조화 버퍼다. 화살표는 UE line batcher처럼 축 하나와 머리선 둘이다: 끝에서 축을 따라 −√ArrowSize, 옆으로 ±√ArrowSize (ArrowSize 10이면 한 줄 약 4.47 cm, LineBatchComponent.cpp:361-383 [V]). 옆 방향은 Core의 `FVector::GetPerpendicularUnit`(축 × 월드 Z, 축의 Z 성분이 가장 크면 축 × 월드 X)이라 머리가 UE와 같은 평면에 놓인다. 원 명령의 틀은 `MakeCircleFrame`이 만들고(UE `FRotationMatrix::MakeFromYZ`의 결과), 축이 0이거나 평행해도 직교 틀을 낸다 (M4 수정 라운드 2. 두 함수는 UE 코드가 아니라 모양 설명에서 짰다). 캡슐 양 끝 반구의 중심은 중심에서 `max(HalfHeight − Radius, 1)`이다 (LineBatchComponent.cpp:588 [V]). GPU 시간은 timestamp query 4개 링이다.
- **앱:** 콘솔 서브시스템 exe다 (로그가 콘솔로 간다). `-Headless`는 창 없이 같은 경로로 오프스크린에 그린다 (자동 검사와 문서 그림). ImGui 레이아웃은 창 모드에서 `Saved/Config/PhysicsTestViewer.imgui.ini`에 남는다. 물리는 렌더 프레임마다 한 프레임이다 (일시정지면 0, "한 스텝"이면 정확히 1). 화면이 60 Hz보다 느리면 시뮬레이션도 실시간보다 느리다 (viz). 잘못된 `-Threads`, `-Frames`, `-WarmupFrames`, `-Param:` 값은 모든 모드에서 종료 코드 2다 (벤치와 같다. 처음 구현은 `-Param:`을 조용히 버리고 기본 파라미터로 씬을 열었다). 종료 때 레지스트리가 초기화된 모듈 중 살아 있는 씬이 없는 것을 모두 `TermGamePhys`한다 (`FPhysicsBackendRegistry::TermIdleModules`: 열기에 실패한 선택이 초기화한 모듈 포함). 전원 스로틀링(QoS)은 Windows 기본값이다. 뷰어 전용 opt-out과 `-AllowPowerThrottling`은 효과가 재어지지 않아 수정 라운드 2에서 지웠고, 벤치와 뷰어의 QoS 정책은 M7 호스트 제어(DL11)가 함께 정한다.
- **재시작:** `FViewerSession::Open`이 씬을 만들기 전에 레지스트리 규칙을 본다. 두 번째 UE_RUNTIME 모듈, 다른 init 옵션으로 초기화된 GLOBAL_SCENE_SETTINGS 모듈, ONE_PER_PROCESS·NO_REINIT·GLOBAL_SCENE_SETTINGS 모듈의 InitGamePhys 거부는 RestartRequired다 (`ClassifyBackendSwitch`). Engines 패널의 버튼이 같은 exe를 새 선택의 명령줄로 띄우고 (`CreateProcessW`) 자신은 끝난다. 모듈은 프로세스가 끝날 때까지 내리지 않는다 (뷰어는 TermGamePhys를 부르지 않는다. PhysX 같은 GLOBAL_SCENE_SETTINGS 모듈은 프로세스당 tolerances scale 하나이고, 바꾸려면 idle 모듈을 Term해야 하므로 뷰어는 RestartRequired로 처리한다. 게이트 실행기는 idle PhysX 모듈을 Term하고 다시 초기화한다, M6 수정 라운드 2에서 문구 정정). 변형의 엔진 키는 씬 전에 빈 시험 씬으로 검사해 Fatal 대신 오류 문장을 낸다 (벤치의 preflight와 같다).
- **카메라 [R]:** 최대 비행 속도 1000 cm/s × 속도 배율, 마우스 시점 0.25°/px, 피치 ±89°, C/Z FOV 60°/s, 비행이 아닐 때 휠 1칸 = 100 cm × 배율 전진, 왼쪽 드래그는 지면 이동 + yaw, 가운데 드래그는 팬. 설계가 정한 값(hFOV 90 major 축, near 10 cm, RMB + WASD, E/Q 월드 상하, 휠 ±10 %, 가속 20000 cm/s², 감쇠 10/s)은 단위 테스트로 고정했다.
- **manifest:** 스크린숏마다 `<png>.json`(`schema = physicstest.viewer/1`, `mode = viz`, `timing_label`, `timing_valid_for_headline = false`, 씬·desc·options 해시, 카메라, 디버그 드로 플래그와 수, 단계별 시간)을 쓰고, `-PerfReport`는 `physicstest.viewer.perf/1`(mode viz)을 쓴다. 뷰어가 여는 씬의 `RunMode`는 언제나 Viz다.

**M5 구현 메모 [V]** ([Progress.md](Progress.md) M5 §3 I-1~I-4)
- **엔진 목록:** Bullet과 PhysX(변형 4종)가 뷰어 코드 변경 없이 레지스트리와 `Config/Profiles/Backend`에서 나타난다 (`Viewer.Headless{Bullet,PhysX}`).
- **섬 간선:** Phi ≤ 0 **또는** `PT_CONTACT_ACTIVE`/`ACTIVE_APPROX`인 접촉이다. 정지 PhysX 점은 Phi ≈ +0(float)이고 솔버 임펄스가 있어 Phi만으로는 Normalized M1에서 섬이 생기지 않았다. Jolt는 ACTIVE_APPROX ⟺ Phi ≤ 0이라 그래프가 같다.
- **스냅샷 인덱스:** 호스트 `FPhysicsBackendScene::CaptureDebugSnapshot`이 `BodyIndex ≥ 바디 수`(PT_INDEX_NONE 포함)인 파티클을 버리고 씬당 한 번 Error 로그를 남긴다 (`NumBodies`, `NumRejectedParticles`). `BuildParticleLookup`, `FIslandBuilder::Build`, `GatherIslands`는 `GetBodyIndexBound`로 배열 크기를 정하고 범위 밖 인덱스를 건너뛴다. 백엔드가 보낸 인덱스로 배열 크기를 정하지 않는다 (M13 Chaos 브리지도 같은 경로).
- **재시작 (M4 이월):** PhysX와 Bullet이 GLOBAL_SCENE_SETTINGS라 이제 실제로 일어난다. UE4Compat 계열 ini는 init 키를 적지 않으므로(§7.5) "(Default / none)" ↔ UE4Compat ↔ -TGS ↔ -Stab는 같은 프로세스, SdkDefault ↔ 나머지는 RestartRequired다 (`Viewer.PhysXVariantSwitchRules`). 콘솔 명령 `open <Backend> [<Variant>|-]`(Apply와 같은 경로)과 `restart`(Engines 패널 버튼과 같은 `RequestRestart`), 그리고 그 명령을 프레임마다 하나씩 실행하는 `-ViewerCmds=`를 더했다. `-Headless` 프로세스의 재실행은 자동 실행의 일부라 `-Headless`와 실행 제어(`-Frames`, `-WarmupFrames`, `-Paused`, `-Exit`, `-Screenshot`, `-PerfReport`, `-Camera`, `-WaitGpu`)를 넘기고, 자식을 `CREATE_NO_WINDOW` + 표준 핸들 상속으로 띄워 끝날 때까지 기다린 뒤 그 종료 코드로 끝난다. 창 모드의 재실행은 그대로 실행 제어를 넘기지 않고 새 창을 띄운 뒤 끝난다. `-ViewerCmds`는 넘기지 않는다. CTest `Viewer.HeadlessRestart`가 PhysX UE4Compat → -TGS(같은 프로세스) → SdkDefault(RestartRequired) → `restart` → 자식이 SdkDefault로 스크린숏을 쓰고 0으로 끝나는 것을 확인한다.
- **`-PerfReport`:** 헤드리스에서는 `vsync`를 false로 쓴다 (M4 경미 지적).

**2단계 [확장 M12]**
- **레이아웃:** SideBySide / GhostOverlay (엔진별 tint + COM 궤적).
  - PhysX `UE4Compat`, `-TGS`, `-Stab`은 tolerance scale이 같아 한 프로세스에서 나란히 돌릴 수 있다. `SdkDefault`처럼 scale이 다른 조합, combine 설정이 다른 Bullet 두 씬만 거부한다 (§6).
- **리플레이:** `FReplaySource`가 `states.ptst`와 `contacts.ptcn`을 읽는다. 다른 프로세스에서 돈 Test 구성 Chaos run도 이 방식으로 본다. 성능 기준(≥ 60 fps)은 리플레이에도 적용한다.
- **패널 추가:** Profiler (ImPlot, "viz mode, not a benchmark" 표기), Output Log.
- **캡처:** headless PNG (스테이징 텍스처, 비동기 stb 인코딩), WARP 골든 PNG 테스트.
- **네이티브 오버레이 슬롯:** `DrawNativeCommands` (PhysX render buffer, Chaos Dev ChaosDD).

**벤치 exe는 D3D 장치를 만들지 않는다.**

## 12. 벤치마크 러너와 결과 포맷

### 12.1 CLI (UE 스타일)

```
PhysicsTestBench.exe -Backend=PhysX -BackendProfile=UE4Compat -Profile=Normalized -Scenario=M3.BoxPile -Param:N=5000
                     -Frames=600 -Threads=1 -Affinity=0x1 -RunMode=Bench [-Instrument=counters,phases]
                     -Repeat=3 -Order=17 -HashEvery=0 -StatesEvery=0 [-ExecCmds="p.Chaos..."] [-ini:...]
                     -Out=Saved\Results\<session>\<run_id>
PhysicsTestBench.exe -List | -DescribeBuild | -Describe -Backend=.. | -Conformance -Backend=.. -BackendProfile=.. -Profile=..
```

- **[MVP M3] 최소판:** `-Backend`, `-BackendProfile`, `-Profile`, `-Scenario`, `-Param:`, `-Frames`, `-Threads`, `-Out`, `-List`, `-Describe`. 출력은 `timings.csv`와 기본 manifest다.
- **[MVP M6]:** `-Conformance`가 게이트 스위트를 돌려 `conformance.json`을 쓴다. 스탬프 키 검증은 M10에 더한다.
- **[확장 M7] 전체판:** 나머지 인자, 호스트 제어, recorder, preflight, 종료 코드.
- `-HashEvery=0`은 "마지막 프레임 뒤에만"이라는 뜻이며 bench 모드의 기본이다 (§9.1).
- **M7에서 더한 인자 [V]:** `-Affinity`, `-RunMode=Bench|Viz`, `-Instrument=`(counters, phases, alloc, contacts, native_debug_draw), `-HashEvery`, `-StatesEvery`, `-CountersEvery`, `-CaptureEvery`, `-Repeat`, `-Order`, `-BlockSeed`, `-PreRoll=`(초, 기본 0.5), `-HostCheck=enforce|record`, `-ContaminationPct=`(기본 5), `-Gate=G8|G11`(여러 프로세스 게이트, `-Runs=`), 시험 전용 `-InjectPreflightFault=`, `-InjectSceneInstrumentFlags=`, `-InjectFatal`, `-BusyHelper=<초>`. 인자 표와 기본값은 [TimingContract.md](TimingContract.md)와 [ResultsSchema.md](ResultsSchema.md)에 있다. M3의 `-Repeat`은 run 수가 아니라 manifest `order.repeat_index`다 (fresh process 반복은 러너 M8과 `-Gate=G8`의 몫).
- **M8에서 더한 인자 [V]:** `-AnalyticReferences`(C++ 해석 기준값과 입력을 JSON으로 stdout에 낸다, `analytic.py --check-cpp`), 시험 전용 `-InjectTickAllocation=<f>`(frame f의 `Tick` 안에서 `operator new` 한 번: 할당 훅의 양성 대조), `-InjectSilentExit=<f>`(frame f 뒤 `TerminateProcess(0)`, 출력·`exit.json` 없음: "도중에 죽은 run"). 두 시험 스위치는 `run.test_injections`와 헤드라인 사유 "test-only injection"에 남는다 (TimingContract §6 #10). M3부터 있던 `-Session=`(manifest `session`)은 M8 러너가 세션 id를 넘기는 데 쓴다.

### 12.2 실행 순서 (1 run = 1 process)

1. 인자를 파싱한다.
2. 호스트를 제어한다 [확장 M7]. SDK 초기화나 LoadLibrary보다 **먼저** 다음을 수행한다:
   - `SetProcessAffinityMask`, HIGH_PRIORITY_CLASS
   - power throttling opt-out
   - `timeBeginPeriod(1)`: UE DLL이 프로세스 전역으로 이 값을 바꾸므로 (WindowsPlatformMisc.cpp:1076) 모든 엔진에 똑같이 적용한다.
   - 전원 계획을 읽어 기록한다. 바꾸지는 않는다.
   - CPU MHz와 배경 부하를 기록한다.
3. ini를 로드하고 리졸브한다.
4. 백엔드를 로드하고 `InitGamePhys`를 호출한다.
5. 스레드 센서스 #1 [확장 M10].
6. 씬을 구성한다: BuildScene → Flush → `desc_sha256`.
7. `DumpEffectiveSettings`를 받고 preflight를 돈다: G0, G9, G10a [확장 M7], 스탬프 확인 [확장 M10].
8. Prepare를 실행한다.
9. spin pre-roll [확장 M7].
10. frame 0, 이어서 측정 루프.
11. 스레드 센서스 #2로 외부 스레드 cycle을 구한다 [확장 M10].
12. 결과를 모두 쓰고 flush한다. 마지막으로 의도한 종료 코드를 담은 `exit.json`을 쓴다.
13. teardown: `TermGamePhys`. UE_RUNTIME 모듈이면 그 직후 `TerminateProcess(GetCurrentProcess(), code)`로 끝낸다 [확장 M7]. 실행 모드만이 아니라 UE_RUNTIME 모듈을 로드한 모든 모드(`-Conformance`, `-Describe`, `-SceneHashes`, `-Gate=`)가 이 경로를 쓴다. **M7에서 구현했다 [V]:** run은 `exit.json` 다음에 `TermGamePhys`와 `teardown.json`을 쓰고 끝나며, 다른 모드는 `wmain` 끝에서 `FBenchmarkRunner::HasUeRuntimeModule()`이면 같은 경로로 끝난다. `PT_NULL_DLL_UE_RUNTIME=1`인 Null DLL이 `DLL_PROCESS_DETACH`를 받지 않는 것으로 확인한다 (CTest `PhysicsTest.Bench.UeRuntimeTerminate`, 대조군 `UeRuntimeControl`, `UeRuntimeDescribe`).
    - 이유: UE DLL은 언로드하지 않으므로 UE 싱글턴(정적 소멸자, TaskGraph 스레드, GMalloc)은 CRT process detach 때 정리된다. 결과를 다 쓴 뒤 이 단계에서 크래시나 hang이 나면 종료 코드를 잘못 읽게 된다.

**종료 코드** (러너는 프로세스 종료 코드보다 `exit.json`을 신뢰한다. `exit.json`이 없을 때만 프로세스 종료 코드와 타임아웃을 쓴다)

| 코드 | 의미 |
|---|---|
| 0 | ok |
| 2 | gate/preflight 실패 (M3 최소 벤치에서는 명령줄·프로필·씬·파라미터·엔진 키 같은 설정 오류도 2다. §12.3 M3 메모) |
| 3 | 엔진 오류 |
| 4 | 오염된 호스트 (다른 프로세스 바쁨 > 코어 1개의 5%). M7은 시스템 전체가 아니라 run의 코어에서 잰다: 마스크 CPU와 그 SMT 형제의 바쁜 cycle에서 이 프로세스의 cycle을 뺀 값(`platform.host_load.core_busy_other_pct`)을 "코어당 5% × run의 코어 수"와 비교한다 (M7 수정 라운드 0, [TimingContract.md](TimingContract.md) §5.1) |
| 5 | 용량 초과 |
| 6 | 백엔드 없음 (Chaos 미빌드) |

**M7 구현 메모 [V]**
- 여러 원인이 겹치면 2 > 3 > 5 > 4 순서로 하나를 고른다 (설정·preflight 오류가 먼저이고, 엔진 오류는 용량 초과를 덮으며, 오염은 run이 끝까지 돈 경우에만 남는다).
- `exit.json`(schema `physicstest.exit/1`: `exit_code`, `status`, `category`, `reason`, `run_id`, `terminate_process`, `outputs`)은 언제나 마지막 출력이고 임시 파일 + `MoveFileExW`로 원자적으로 쓴다. 명령줄 해석 실패도 `-Out`을 읽을 수 있었으면 `exit.json`을 남긴다. Fatal(`UE_LOG(..., Fatal)`, `check`)은 Core의 Fatal 콜백(`FPlatformMisc::SetFatalExitCallback`)으로 그 run의 `exit.json`(코드 3, `status: fatal`, `category: engine`, 사유는 Fatal 메시지)을 쓴 뒤 `_Exit`한다.
- **M7 수정 라운드 0:** Fatal 훅은 run의 마지막 `exit.json`을 쓰기 전까지만 `exit.json`을 쓴다. 그 뒤(teardown)의 Fatal은 `teardown.json`의 `fatal`에 남고, 프로세스는 run의 종료 코드로 `TerminateProcess`된다 (DL52: teardown은 판정을 바꾸지 않는다, CTest `PhysicsTest.Bench.ExitTeardownFatal`). UE_RUNTIME 모듈이 로드된 프로세스의 Fatal도 `exit.json`(`terminate_process: true`)을 쓴 뒤 `TerminateProcess`로 끝난다 (`PhysicsTest.Bench.UeRuntimeFatal`). 백엔드를 로드한 뒤 끝나는 모든 경로의 `exit.json`이 `terminate_process`를 바르게 적는다.
- `-Conformance`도 `exit.json`을 쓴다. run 디렉터리를 먼저 만들므로 게이트 전에 멈춘 경우(백엔드 없음, 설정·허용치 오류)도 `exit.json`이 남는다 (`PhysicsTest.Bench.ConformanceBackendUnavailable`). CTest `PhysicsTest.Bench.Exit*`가 모든 코드의 `exit.json`을 확인한다 ([Progress.md](Progress.md) M7 §2, §7).

### 12.3 출력 `Saved/Results/<session>/<run_id>/` (ULID)

**해시 정의** [MVP M3. 스탬프 키로 쓰는 것은 확장 M10]

| 해시 | 덮는 것 | 엔진 간 |
|---|---|---|
| `scene_sha256` | 씬 이름, 버전, 파라미터, seed의 정규 JSON (`scene.json`) | 같아야 함 |
| `desc_sha256` | 엔진 독립 입력만: 머티리얼, 셰이프, actor create info, convex 정점, 제약, 중력, dt 시퀀스 | 같아야 함. 같은 씬을 두 백엔드 × 두 변형으로 만들어 같은 값이 나오는지 단위 테스트한다 |
| `options_sha256` | `PtSceneDesc`의 RunMode, InstrumentFlags, NumThreads, RestitutionThreshold, Options(엔진 키) | 달라도 됨 |
| `physics_sha256` | 리졸브된 물리 프로필 (UEParity/Normalized + 오버라이드, 변형 키 제외) | 한 표 안에서 같아야 함 |
| `variant_sha256` | 백엔드 변형 ini의 내용 | 시리즈 레이블 |
| `tolerances_sha256` | `Config/Golden/tolerances.json` | 스탬프 키 |

- UEParity에서 엔진마다 다른 restitution threshold 매핑은 `options_sha256`에 들어간다. 그래서 UEParity 표도 `desc_sha256` 동일성 검사를 통과한다.

**`manifest.json`** (schema `physicstest.run/1`, M7부터 `physicstest.run/2`: [ResultsSchema.md](ResultsSchema.md) §2, `Config/Schemas/manifest.schema.json`). 기본 필드는 [MVP M3], 나머지는 표시한 마일스톤에 더한다.

| 필드 그룹 | 내용 |
|---|---|
| 상태 | status, timing_valid_for_headline (M7), comparability_key (M10) |
| harness | git_sha, dirty, exe_sha256, preset |
| build | flag_profile, msvc, cmake, crt, lto, tu_self_report, flags_json_sha256 |
| backend | name, variant, label, sdk_version, sdk_sha, module_sha256 (DLL 백엔드), abi_version, capabilities, module_flags, fidelity_notes |
| profile | physics, mass_rule, resolved[] (key, value, unit, origin), physics_sha256, variant_sha256 |
| 실측값 | effective[], bodies_sampled[] |
| scene | name, version, params, scene_sha256, desc_sha256, options_sha256, num_bodies, windows |
| run | mode, instrumentation[], frames, dt_s, dt_engine_s=(double)(float)dt, tick_mode, clamp_rule, frame_driver, sync_policy, hash_every, async_interpolated |
| threads | T, affinity_mask, engine_native, census_init/window (M10), foreign_cycles_fraction (M10) |
| platform | cpu, P코어 목록, priority, throttling, timer_resolution, power_scheme, os_build, qpc_hz, tsc_hz (M10), timer_read_overhead_ns (M10), cpu_mhz, system_busy_other_pct |
| order | repeat_index, order_index, block_seed |
| 단계 | phases_ns |
| memory | process peak commit/WS, 백엔드 alloc (Chaos는 프로세스 수준만) |
| 결과 | summary (윈도별 mean/median/p95/p99/max, T_step, T_pull, cpu_eff (M10)), determinism (final h_native, h_quantized), gates, conformance_stamp (M10), outputs |

**파일**
- **`timings.csv`** [MVP M3] (프레임당 한 줄): `frame, sim_time_s, num_steps, push_ns, setup_ns, start_ns, wait_ns, end_ns, step_ns, pull_ns, frame_ns, frame_qpc_ticks, solver_ns, awake_bodies, contact_pairs, contact_points, islands, error_bits`. 값이 없으면 비우고, 카운터를 얻을 수 없으면 -1.
- **`states.ptst`** [확장 M7] (little-endian):
  - 64 B 헤더: magic `PTST`, version, flags, body_count, frame_count, record_stride 112, units 1=cm, dt, scene_sha256 prefix, desc_sha256 prefix.
  - 프레임 레코드: {u32 frame, u32 step, f64 sim_time}.
  - 바디 레코드: 13×f64 (actor 원점 위치, quat, 선속도 cm/s(dynamic은 COM, kinematic은 actor 원점: `PtBodyState`, §5.2 #48. M4에서 정정), 각속도 rad/s) + u8 state + pad.
  - numpy 로딩용 `states.dtype.json`을 함께 둔다.
- **`hashes.csv`** [확장 M7]: `frame, h_native, h_quantized`. h_quantized는 위치 1e-4 cm, quat는 w≥0으로 정규화 후 1e-6. 엔진 간이나 빌드 간에는 절대 비교하지 않는다. M7은 `h_state`(`states.ptst` 바디 레코드 앞 105 B의 FNV-1a 64, 비트 단위 상태 해시) 열을 더했다. G11은 이 값을 프레임마다 비교한다.
- **`contacts.ptcn`** [확장 M7]: `PTCN` 헤더(32 B) + 프레임별 {u32 frame, u32 count} + `PtContactPoint[count]`.
- **그 외:** `effective_settings.jsonl` [MVP M3], `scene.json` (정규 JSON, %.17g), `build_flags.json`, `backend.log`, `exit.json` [확장 M7].
  - **M7·M8 실제 [V]:** `build_flags.json`은 run 디렉터리에 쓰지 않는다. 빌드 사실은 manifest의 `build.flags_json_sha256`·`flags_json_tu_count`와 `backend.tu_self_report`(G0 입력)에 들어간다. `backend.log`도 따로 없다: 백엔드 로그는 벤치의 stdout·stderr로 나가고, M8 러너가 run 디렉터리의 `child.log`에 받는다 (`-Gate=`의 자식도 같다). 그 밖에 `effective_settings_after.jsonl`, `hashes.csv`, `teardown.json`(M7)이 있다 ([ResultsSchema.md](ResultsSchema.md) §1, Progress.md M8 §3).
- M3 최소 벤치는 마지막 프레임의 `h_native`를 manifest의 determinism 필드에만 남긴다.

**M3 최소 벤치 구현 메모 [V]** (`PT::Benchmark`, `PhysicsTestBench.exe`)
- **CLI:** `-Backend`, `-BackendProfile`(생략하면 `Config/Profiles/Backend/<Backend>.Default.ini`가 있을 때 그것), `-Profile`(기본 Normalized), `-Scenario`, `-Param:Name=Value`(반복), `-Frames`(0 = 씬 기본), `-Threads`(기본 1), `-Out`(생략하면 `Saved/Results/<Session>/<ULID>`), 그리고 M3에서 더한 `-Session`(기본 `adhoc`), `-ConfigDir`. `-ini:`과 `-ExecCmds`는 프로필 리졸버로 간다. `-List`는 씬(이름, 버전, 종류, 파라미터와 기본값, 기본 프레임 수)과 등록된 백엔드를, `-Describe`는 백엔드 정보·capability·필수 키와 리졸브한 프로필로 만든 씬의 effective 설정을 JSON으로 낸다.
- **순서:** §12.2의 1, 3, 4, 6, 7(필수 effective 키만 확인), 8, 10, 12, 13을 한다. 호스트 제어(2)와 pre-roll(9)은 M7이다. 전원 계획은 PowrProf로 읽어 manifest `platform.power_scheme`에 기록만 한다. 결과 파일을 모두 쓴 뒤 씬과 모듈(`TermGamePhys`)을 내린다.
- **종료 코드:** 0, 2(명령줄 형식 오류, 없는 프로필·씬·파라미터, 잘못된 파라미터 값(예: `Seed=abc`), `HashEvery`/`CountersEvery` > 0(프레임별 해시·카운터는 M7 recorder라 M3 러너는 거부한다. 전에는 받아서 manifest에 적기만 했다, M3 수정 라운드 1), 백엔드가 거부한 변형의 엔진 키, 필수 effective 키 누락. 뒤의 둘은 `manifest.json`을 `status = preflight_failed`로 쓰고 Prepare와 프레임 루프를 건너뛴다), 3(용량 bit 밖의 엔진 오류 bit. preflight 뒤의 ABI 실패는 Fatal로 3), 5(용량 bit만: 백엔드가 effective 키 `errors.capacity_bits`로 알린 bit. Jolt는 0x107 = ManifoldCacheFull | BodyPairCacheFull | ContactConstraintsFull | temp fallback. 키가 없는 백엔드는 용량 bit가 없다), 6(레지스트리에 없거나 쓸 수 없는 백엔드). 4는 M7이다. 용량 bit와 다른 오류 bit가 한 run에 함께 있으면 3이다 (용량을 늘려 다시 돌려도 엔진 오류는 남는다). manifest `determinism`에 `capacity_error_bits_declared`, `capacity_error_bits`, `other_error_bits`를 따로 적어 어느 쪽도 숨기지 않는다 (M3 수정 라운드 1). 엔진 키 검사는 씬 생성 전에 그 키만 담은 빈 시험 씬을 `CreateScene`으로 만들어 본다 (M3 수정 라운드 0. 전에는 `FPhysScene`의 Fatal로 3이었고 출력이 없었다). CTest `PhysicsTest.Bench.Exit*`가 2(명령줄, 씬, 파라미터, seed, `HashEvery`, 엔진 키, 필수 키), 3(시험 전용 `-InjectEngineErrorBits`로 넣은 0x200, 용량 bit와 함께일 때 포함), 5, 6을 확인한다.
- **출력:** `timings.csv`, `manifest.json`, `effective_settings.jsonl`(Prepare 전, §12.2의 7), `effective_settings_after.jsonl`(마지막 프레임 뒤. temp allocator peak 같은 READBACK 값), `scene.json`. 카운터는 `CountersEvery = 0`이라 -1, `solver_ns`는 비운다. 명령이 없는 프레임의 `push_ns`는 0이다 (호출이 없다, §9.3).
- **manifest:** `schema = physicstest.run/1`, `status`, `exit_code`, `unofficial = true`, `timing_label`(비공식 사유 문장), `timing_valid_for_headline = false`, harness(git SHA, dirty, `git_sha_source = cmake_configure`: 마지막 configure 시점 값이라 그 뒤의 커밋·수정은 반영되지 않는다, exe SHA-256, preset), build(`crt`는 하네스 TU의 `_DLL`/`_DEBUG`로 정한다: dbg는 `MDd`. `lto`는 `CMAKE_INTERPROCEDURAL_OPTIMIZATION`), backend, profile(`physics_sha256`, `variant_sha256`, fidelity notes), `effective[]`, `effective_module[]`, `missing_effective_keys`, `bodies_sampled`(질량 상대 오차와 관성 텐서 R·diag·Rᵀ의 상대 오차. 엔진의 주축 순서는 하네스와 다를 수 있다), scene(`scene_sha256`, `desc_sha256`, `desc_sha256_scope`, `options_sha256`, windows), run(`frames`, `frames_run`, `sync_policy` = `push_per_step+pull_active_only_per_frame`(Harness driver) 또는 `push_per_frame+pull_active_only_per_frame`(Native driver)), threads(`T`, `engine_native` = 백엔드의 `threads.workers` READBACK), platform, memory(프로세스 peak commit·WS와 현재 값, 백엔드 씬 `GetMemoryStats`), `phases_ns`(`option_preflight` 포함), summary(윈도별 mean/median/p95/p99/min/max, frame 0 별도), determinism(`h_native`, `engine_error_bits`, `capacity_error_bits_declared`·`capacity_error_bits`·`other_error_bits`), 시험 전용 `run.injected_engine_error_bits`(`-InjectEngineErrorBits`를 줬을 때만), `deferred_fields`(M7/M10이 채울 §12.3 필드 목록), outputs. preflight 실패는 앞부분(status, harness, build, backend, profile)과 `preflight_error`만 쓴다 (M3 수정 라운드 0). **M7에서 바뀌었다:** run/2는 preflight 실패(엔진 옵션 거부 포함)에도 모든 그룹을 쓰고(닿지 못한 값은 null), 시험 주입은 `run.test_injections`에 있다 ([ResultsSchema.md](ResultsSchema.md) §2, M7 수정 라운드 0).
- **비공식:** M3의 모든 타이밍은 비공식이다. 측정 프로토콜(M7), 전원 계획 확인(M8 러너), 적합성 스탬프(M10)가 없다.

**M7 구현 메모 [V]** (형식의 정본은 [ResultsSchema.md](ResultsSchema.md), 타이밍 규약은 [TimingContract.md](TimingContract.md))
- `FRunManifest`(`physicstest.run/2`)가 위 표의 그룹을 모두 쓴다. M3과 다른 점: `timing_valid_for_headline`이 10개 조건으로 계산되고 거짓이면 `timing_invalid_reasons`가 붙는다. `unofficial`은 M10 전까지 언제나 참이다. `harness.git_sha_source`는 `build`. `platform`에 우선순위, throttling, 타이머 해상도, 전원 계획 GUID와 고성능 여부, CPU MHz, `host_load`. `clock`(`qpc_hz`, `read_overhead_ns`). `order`, `alloc`, `recorders`, `gates`(G0, G9, G10a의 preflight 결과), `determinism.h_state`. `deferred_fields`에는 M10 필드만 남는다.
- recorder 작업은 프레임 사이(타이밍 버킷 밖)에서 한다: 타이밍·해시 행은 미리 잡은 메모리에 모았다가 run 끝에 쓰고, `states.ptst`와 `contacts.ptcn`은 4 MiB stdio 버퍼로 이어 쓴다 ([TimingContract.md](TimingContract.md) §4.2). "N마다"는 (f + 1) % N == 0인 프레임과 마지막 프레임이다.
- `teardown.json`(`TermGamePhys` 시간과 결과)은 `exit.json` 뒤에 쓰는 유일한 파일이다. 러너는 이 파일이 없어도 run을 받는다.
- 여러 프로세스 게이트(`-Gate=G8`, `-Gate=G11`)는 자식 run을 job object(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)로 띄우고 `gate_g8.json`/`gate_g11.json`(`physicstest.gate/1`)을 쓴다 (`FProcessSpawner`).

**M4 메모 [V]** (M3 검토 경미 지적)
- `scene_sha256`의 정규 JSON에 seed(정수, `GetSeed`)를 언제나 넣는다: `{"name","params","seed","version"}`. 전에는 `Seed` 파라미터를 선언한 씬(`M3.BoxPile`)만 seed가 들어갔다. 값이 바뀌었으므로 M3 세션의 `scene_sha256`과는 비교하지 않는다.
- (M7에서 최상위 `timing_valid_for_headline`·`timing_invalid_reasons`로 바뀌었다, [ResultsSchema.md](ResultsSchema.md) §2) manifest `run.headline_eligible`, `run.headline_ineligible_reasons`: 틱 모드가 FixedStep이 아니거나, `RunMode`가 Bench가 아니거나, `InstrumentFlags`가 0이 아니면 거짓과 사유(경고 로그도 낸다). run은 그대로 돌고 종료 코드도 바뀌지 않는다 (M7 분석이 거른다). 벤치 루프는 언제나 명목 dt의 FixedStep 프레임을 넣는다. 비공식 표시(`timing_valid_for_headline = false`)는 그대로다.

### 12.4 적합성 스탬프 [확장 M10]

- **키:** {bench exe SHA, backend module SHA, `variant_sha256`, `physics_sha256`, `tolerances_sha256`, gate-suite version}.
  - backend module SHA는 DLL 백엔드(Chaos, Null DLL)이면 DLL 파일 해시다. static 백엔드는 exe SHA에 포함된다.
  - 변형 이름이 아니라 ini 내용을 키로 쓴다. 그래야 사용자가 4.27 확인 후 `PhysX.UE4Compat.ini`를 고치거나 Chaos DLL을 다시 빌드했을 때 스탬프가 무효가 된다.
- **위치:** `Saved/Conformance/<stamp_key_sha256>.json`. 내용은 {키 구성요소 전체, gates: pass/fail/skip(사유), utc}.
- **생성:** 벤치 exe의 `-Conformance`가 실제 컴파일된 백엔드 코드로 게이트를 돌려 만든다.
- **확인:** 러너가 프로세스를 띄우기 **전에** 모든 키 구성요소를 다시 계산해 비교한다.
- 링크에 `/Brepro`를 쓰면 무관한 재링크로 exe SHA가 바뀌지 않는다 (선택).

### 12.5 `Tools/ptbench` 러너 (`python -I -m ptbench run --matrix ...`) [확장 M8]

1. **환경 게이트 (읽기 전용):**
   - `powercfg /getactivescheme`: High performance나 Ultimate가 아니면 거부한다 (`--allow-balanced`로 우회 가능). 전원 계획 전환은 사용자가 한다.
   - 유휴 확인: 2 s 동안 < 3%.
   - 다른 PhysicsTest 프로세스가 없어야 한다.
2. **스탬프 확인 [확장 M10]:** 스탬프가 없거나 키가 맞지 않는 셀은 프로세스를 띄우기 **전에** 거부한다.
3. **셀 전개:** backend-variant × profile × scene × params × T.
4. **블록 실행:**
   - 반복 r마다 seed 고정 셔플을 한다 (R: 헤드라인 10, 그 외 ≥5).
   - run마다 fresh process, 타임아웃, 2 s 쿨다운.
   - sentinel 셀을 처음, 중간, 끝에 돌린다 [확장 M10].
5. **CV:** M8은 셀별 CV를 보고만 한다. [확장 M10] CV 게이트: CV > 2%이면 추가 블록을 최대 2회 돌린 뒤 `noisy` 표시. sentinel drift > 2%이면 세션 전체에 플래그.
6. **출력:** `session.json` (환경, 순서, seed, exit code), `index.csv`.

헤드라인 매트릭스의 기계 시간은 약 1~1.5시간이다.

**M8 구현 메모 [V]** ([Progress.md](Progress.md) M8, [TimingContract.md](TimingContract.md) §5.2, [ResultsSchema.md](ResultsSchema.md) §11)
- **패키지:** `Tools/ptbench`가 곧 패키지 디렉터리다 (`pyproject.toml`의 `package-dir`). 저장소 `.venv`에 `pip install --no-build-isolation --no-deps -e Tools/ptbench`로 설치하고 `python -I -m ptbench <command>`로 돌린다. 명령: `envcheck`, `run`, `load`, `validate`, `perf`, `report`, `determinism`, `analytic`.
- **환경 게이트:** 전원 계획은 `powercfg /getactivescheme`의 GUID로 판정한다 (이름은 지역화된다: 이 PC는 "균형 조정"). 유휴는 `GetSystemTimes`로 2 s 창의 시스템 전체 바쁨 < 3%를 본다: 한 창이라도 통과할 때까지 최소 5창, `--idle-wait`(기본 120 s) 동안 (이 사용 중인 데스크톱은 2.5~4.9%를 오가 5창 고정으로는 무인 시작이 자주 거부됐다, M8 수정 라운드 0). 다른 PhysicsTest 프로세스는 Toolhelp32 스냅숏으로 찾는다. 실패한 검사의 우회 플래그(`--allow-balanced`, `--allow-busy`, `--allow-concurrent`)와 시험용 `--skip-idle`(유휴를 재지 않음, 우회로 기록)은 `session.json` `bypasses`와 리포트 머리에 남는다. 거부되면 종료 코드 3이고 아무 run도 돌지 않는다.
- **블록 셔플:** 반복 r의 seed = splitmix64(`block_seed` + r)의 출력, 순서 = 그 seed의 splitmix64 Fisher-Yates. Python의 `random.shuffle`은 알고리즘이 계약이 아니어서 쓰지 않았다. 벤치에 `-Repeat`, `-Order`, `-BlockSeed`를 넘겨 manifest `order`에 남긴다.
- **판정:** `exit.json`만 믿는다. 없으면 `no_exit_json`(또는 `timeout`) 실패이고, 원시 종료 코드는 `process_exit_code`로 기록만 한다. 시험 전용 `-InjectSilentExit=<f>`(원시 코드 0, 출력 없음)로 확인했다. run마다 `exit.json`·`manifest.json`을 스키마로 검증한다 (M7 후속 "러너가 run마다 검증").
- **종료 코드 4 (리드 결정, M8):** 오염 문턱(코어당 5% × 코어 수)은 그대로 두고, 4로 끝난 run을 쿨다운 뒤 fresh process로 최대 2번 다시 돈다. 모든 시도가 4이면 그 반복과 셀이 `contaminated`다. 시도마다 `index.csv` 한 줄이 남는다.
- **추가 스위치 (M8 수정 라운드 1):** 매트릭스의 `bench_args`, 타깃의 `bench_args`, `--bench-arg`에서 한 셀에 같은 스위치 키(대소문자 무시, `=` 앞, `-ini:`는 `[Section]:Key`까지)가 두 번 나오면 러너가 거부한다 (`MatrixError`, 종료 코드 2, run 없음). 벤치는 `FParse::Value`로 스위치의 **첫** 출현만 읽으므로(UE Parse.cpp:265의 `FCString::Strifind`와 같은 규칙) 뒤의 중복은 말없이 무시되기 때문이다. 러너가 셀마다 넘기는 스위치(`-Out`, `-Backend`, `-BackendProfile`, `-Profile`, `-Scenario`, `-Param:<k>`, `-Threads`, `-Frames`, `-Repeat`, `-Order`, `-BlockSeed`, `-Session`)와 다른 모드를 여는 스위치(`-Gate`, `-List`, `-Describe`, `-Conformance`, `-AnalyticReferences`, `-SceneHashes`, `-BusyHelper`)도 거부한다. 키가 모두 다르므로 명령줄의 순서는 아무것도 정하지 않는다.
- **자식 프로세스 (M8 수정 라운드 1):** 벤치는 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job object에 들어간다 (M7 `FProcessSpawner`의 `-Gate=` 자식과 같다). 러너가 어떻게 끝나든(Ctrl+C, CTest 타임아웃, 충돌) 벤치도 끝난다. 대기는 try/finally라 예외에도 자식을 죽이고, KeyboardInterrupt 밖의 예외도 `session.json`을 `aborted`(사유 포함)로 남긴 뒤 다시 던진다. `exit.json`을 쓴 뒤 30 s(`teardown_grace_s`) 안에 끝나지 않는 run(TermGamePhys나 정적 teardown의 멈춤, DL52)은 그때 죽이고 `teardown_killed` true와 메모를 남긴다. 판정은 그 `exit.json`이다 (러너는 1 s마다 `exit.json`이 생겼는지만 본다).
- **smoke 매트릭스 (03 M8 완료 기준):** 4 엔진 변형 × {P0, BoxPile 1k, 5k} × T = 1 × R = 5 = 60 run이 이 PC(Balanced, 사용 중)에서 무인으로 약 4분에 끝난다.

## 13. 분석 파이프라인 (`Tools/ptbench/analysis`)

사용 패키지: numpy, pandas, pyarrow, scipy, matplotlib, jsonschema. 버전을 고정한 venv/wheelhouse로 설치한다 (§4.7).

| 모듈 | 역할 | 단계 |
|---|---|---|
| load | 스키마 검증 후 tidy parquet로 저장 (runs, frames, effective, gates) | 확장 M8 |
| validate | 거부 대상: 헤드라인 자격 없는 run, 게이트 실패, 오염 호스트, 플래그 프로필이 섞인 표, 한 표 안의 `desc_sha256` 불일치, mode=viz manifest. 스탬프 누락·불일치는 M10에 더한다 | 확장 M8 (M10 보강) |
| perf | run별 윈도 평균, run 간 중앙값, p95/p99, 프레임별 중앙값/IQR 시계열, CV, N-scaling α, S(T)/E(T), 오버헤드 모델 t = a + b·N (P0/P1/M3). percentile bootstrap 95% CI (B=10000)와 cpu_eff는 M10 | 확장 M8 (M10 보강) |
| behavior | 반발 계수, 마찰 d(t) (이산·연속 기준), 진자 주기 (영점 교차), 체인 신장, 하네스 기하 관통 (sphere-plane, box corner, SAT), resting jitter, time-to-sleep, 자기 민감도 대비 발산, 최종 z의 Wasserstein | 확장 M9 |
| determinism | DET1 행렬과 첫 발산 프레임. DET2~DET5는 M15 | 확장 M8 |
| report | `report/<date>/index.md` + PNG. comparability key마다 표 하나 (M10 전에는 scene × profile × flag × T마다) | 확장 M8 |
| analytic | 모든 해석 기준값을 `dt_engine = (double)(float)dt`로 계산한다. ±1e-9 허용오차는 이 기준에만 쓴다 | 확장 M8 (단위 테스트 쪽은 MVP M1) |

**M8 구현 메모 [V]** ([Progress.md](Progress.md) M8)
- **load:** 입력은 세션 디렉터리, `-Gate=G8` 디렉터리, run 디렉터리다 (아니면 세 단계까지 찾아 내려간다). 모든 run의 `exit.json`·`manifest.json`을 스키마로 다시 검증해 parquet 네 개(runs, frames, effective, gates)를 `<세션>/analysis/`에 쓴다. 목록·객체 필드는 JSON 문자열로 둔다. 플래그 열은 nullable boolean, 코드·개수 열은 nullable Int64다 (도중에 죽은 run의 빈 값이 문자열 "nan"·"True"가 되지 않는다). 씬 이름에는 기본값(24301)이 아닌 `Seed`만 남긴다.
- **validate:** run 거부 사유는 하나씩 모두 적는다 (`exit.json` 없음 = "원시 종료 코드는 믿지 않음", 종료 코드 ≠ 0, 스키마 불일치, viz·Behavior 모드, 계측 tier, preflight 게이트 실패, 오염, 헤드라인 자격 없음과 그 사유). 표는 scene × profile × flag profile × T이고, 한 표 안에 플래그 프로필이 둘이거나 `desc_sha256`이 둘이면 `TableError`로 표 자체를 거부한다 (리포트에는 "표 거부"로 남고 통계를 내지 않는다: perf가 그 표의 셀에 값을 만들지 않고 셀별 CV 목록에도 "표 거부"로만 적는다). 한 표의 run이 여러 빌드(exe_sha256, git_sha, preset)에서 오면 표는 만들되 주의를 단다 (M10 comparability key 전까지).
- **perf:** run 값 = 윈도 `T_frame` 평균(manifest), 셀 = run 값의 중앙값·평균·최소·최대·CV(표본 표준편차 / 평균), p95·p99 = 셀의 모든 run의 윈도 프레임을 합친 nearest-rank, 프레임별 중앙값/IQR, N-스케일링 α(로그-로그 최소제곱), S(T)/E(T)(T가 여럿일 때), 오버헤드 모델 t = a + b·N(P0/P1/M3).
- **report:** `<세션>/report/<날짜>/index.md` + PNG(표마다 막대 = 중앙값과 run 점, BoxPile 표마다 프레임별 중앙값/IQR, N-스케일링). 머리에 비공식 표시와, 전원 계획이 High performance가 아니면 그 사실과 우회 플래그를 굵게 적는다. 머리는 모든 입력에서 만든다: 세션마다의 게이트와 run마다의 manifest `platform.power_scheme_high_performance` (run 디렉터리·G8 디렉터리만의 리포트, 여러 세션, 세션 도중의 전원 계획 변경도 표시된다). `ptbench report`는 입력을 여럿 받는다. 같은 조건(비공식, High performance가 아닌 전원 계획, 게이트 우회)을 모든 표 제목 밑의 한 줄과 모든 그림 제목 위의 한 줄로도 적는다: 표나 PNG를 따로 떼어 봐도 조건이 보인다 (M8 수정 라운드 1). 고른 윈도에 값이 없는 series는 표에 "값 없음"으로 적고 그림에서 뺀다.
- **determinism:** DET1 그룹(G8 디렉터리, 세션의 셀, 같은 exe·T·모듈의 run) × run의 마지막 `h_native`·`h_state`와, `hashes.csv`에서 기준 run(과반과 같은 run, 과반이 없으면 run 0)과 처음 다른 프레임(`h_native` 먼저, 그다음 `h_state`). 해시가 모든 프레임에 있으면 "per-frame"(정확한 첫 발산), 헤드라인 run처럼 마지막 프레임뿐이면 "sparse"(첫 발산은 그 프레임이거나 그 전)다. 기준 run은 해시 열이 과반과 같은 run이다 (run이 셋 이상일 때: run 0이 홀로 다르면 run 0을 다른 run으로 적는다. 과반이 없으면 run 0). 판정은 equal, DIFFERENT, incomplete다. incomplete는 run 하나라도 실패했거나(`exit.json` 없음, 0·4 밖의 종료 코드) 끝까지 간 run이 둘 미만인데 끝까지 간 run들은 같은 경우다: DET1은 N개가 **모두** 같아야 하므로 통과가 아니다 (M7 `-Gate=G8`도 자식이 실패하면 실패다). 종료 코드 4 run은 타이밍만 오염됐으므로 끝까지 간 run으로 센다. CLI 종료 코드는 0 모두 equal, 1 DIFFERENT, 2 run 없음, 3 incomplete (M8 수정 라운드 1).
- **analytic:** `Analytic::*`(C++)와 같은 식을 같은 순서로 쓴다. 벤치의 `-AnalyticReferences`가 C++ 값을 입력과 함께 JSON으로 내고, `analytic.py --check-cpp`가 그 입력으로 다시 계산해 비교한다: 14 사례 22 값의 최대 차이 1.1e-13 (z60 = 501.83328137049938 cm는 두 쪽이 비트까지 같다).

**Comparability key [확장 M10]**
- `sha256(scene_sha256, desc_sha256, physics_sha256, mass_rule, flag_profile, tick_mode, dt, T, sync_policy, mode, instrumentation, harness_commit)`
- 표 안에서 엔진과 변형은 시리즈 레이블이다. `options_sha256`과 `variant_sha256`은 엔진마다 달라도 된다.
- 한 표에 섞인 run들의 `desc_sha256`이 모두 같은지 assert한다.

**주장 규칙 [확장 M10]:** "A가 씬 S에서 B보다 빠르다"고 쓰려면 다음을 모두 만족해야 한다.
- block-bootstrap 비율 CI가 1을 포함하지 않는다.
- |ρ-1| ≥ 3%.
- Shipped와 MatchedAVX2에서 방향이 같다. 다르면 "ISA-dependent"로 표기한다.

씬별 결과를 우선한다. M10 이전의 비율은 "비공식"으로 표기한다.

**거동 차이 태그 [확장 M9]**
- 모든 거동 차이에 by-design / mismatch / algorithmic 중 하나를 붙인다.
- 혼돈계 씬(더미, 탑)은 집계 관측량과 같은 엔진의 1e-4 cm 섭동 민감도로만 보고한다.
- 바디별 비교는 결정적 씬 `M1.SingleBoxDrop`, `M4.InclineFriction`, `M5.BouncingBalls`, `M6a.Pendulum`에서만 한다. Chaos AsyncFixed run은 보간 때문에 제외한다 (§9.2).

## 14. 검증 게이트와 테스트

### 14.1 단위 테스트 (doctest, `PhysicsTest.<Area>.<Name>`) [MVP M1~M6]

- quat/transform 합성 순서 (UE 순서), FRotator → FQuat 변환
- FParse, ini 계층, CVar
- BodyUtils 예제와 질량 규칙 순서 (§5.3), 단위가 아닌 InertiaTensorScale + 0이 아닌 COMNudge 조합
- FStepPlanner 고정값 (§9.2)
- PtUnitScale 계수표, 캡슐 재매핑, Bullet damping 변환
- ABI 캡슐 `HalfLength` → DrawDebugCapsule `HalfHeight` (= HalfLength + Radius) 변환
- 접촉 정규화 (Jolt flip)
- 정지마찰 규칙 (§5.1): SF < F 머티리얼과 SF > F 머티리얼의 조합
- COM 래핑 판정 (§8.1) 경계값
- FRandomStream (mt19937_64 + 하네스 고유의 [0,1) 매핑, seed 0x5EED)
- FNV, SHA-256
- `desc_sha256` 불변성: 같은 시나리오를 두 백엔드 × 두 변형으로 만들어도 같다
- 해석 기준값이 `dt_engine`으로 계산되는지
- 씬 해시 골든
- manifest 스키마 왕복
- 테셀레이션 개수, 색 표, 드로 순서
- union-find 결정성
- Kit TEB 스레드 ID가 GetCurrentThreadId와 같은지
- PhysX: `FPhysXCPUDispatcherSingleThread`와 `PxDefaultCpuDispatcherCreate(0)`의 해시 일치, 재진입 깊이 1 이하 (M5)

### 14.2 ABI 게이트 (Null, 그리고 모든 백엔드)

| ID | 검사 | 단계 |
|---|---|---|
| A0 | lifecycle: GetInfo, 이중 Init 시 BAD_STATE, create/destroy ×10 | MVP M2 |
| A1 | 버전 핸드셰이크: Major+1이면 NULL, StructSize 검사, 호스트가 자기 `PT_ABI_VERSION_MAJOR`를 요청. v1.0(M6)부터 major 0 요청도 NULL | MVP M2 |
| A2 | 다른 스레드에서 호출 시 WRONG_THREAD, 상태 변화 없음 | MVP M2 |
| A3 | phase 상태기계 | MVP M2 |
| A4 | 입력 검증: 표로 정의한 부정 사례 약 30개 이상 (범위 밖 index, NaN, 비단위 쿼터니언, 질량 0, AsyncDt 0, 알 수 없는 키 등. M2 구현은 모듈 4 + 씬 66 = 70개. 해제한 바디·이중 해제·`OutIndices` 없는 ACTIVE_ONLY 포함). 크래시 0, 전부 거부, 건드리지 않은 쌍둥이 씬과 다음 프레임까지 비트 동일. 무작위 1만 건 fuzz는 M15 | MVP M2 |
| A5 | capacity / stride 2회 호출 프로토콜 | MVP M2 |
| A6 | 호출마다 MXCSR와 x87 상태 보존 (UE PreInit 이후 중요) | MVP M2 |
| A7 | 필수 effective 키 존재: 일반 집합 + 백엔드가 선언한 집합 | MVP M2 |
| A8 | 전체 G 스위트를 loopback으로 돌려 직접 호출과 비트 동일 | 확장 M14 |
| A9 | DLL 위생: export가 PT_GetBackendApi 하나, 의존성은 CRT와 시스템뿐 (`check_modules.py`; Null은 `--strict-crt`로 msvcp140도 금지). `PtAbiUeCompatCheck` 빌드 성공 | MVP M2 |
| A10 | 프레임 모델: Null(`NATIVE_FRAME_MODEL`)에서 Sync / Substepping(UE5) / AsyncFixed의 `NumStepsTaken`이 FStepPlanner 고정값과 같다. FixedStep과 Sync에서 Native driver와 강제 Harness driver의 최종 상태가 비트 동일하다 (스텝이 없는 프레임과 무작위 명령 스크립트 포함). Substepping·AsyncFixed는 같은 프레임 규칙을 허용 오차 안에서 따른다 ([Abi.md](Abi.md) §8) | MVP M6 |

### 14.3 물리·측정 게이트

모든 backend × variant × profile 조합에서 ABI를 통해 돌린다. 씬이 프로필을 제한하는 경우만 예외다 (예: `S08.StartAsleep`은 UEParity만). M6에서는 `PhysicsTestBench -Conformance`가 결과를 `conformance.json`으로 남기고, M10부터 이것이 스탬프가 된다.

| ID | 씬 | 통과 기준 | 단계 |
|---|---|---|---|
| G0 | 빌드 | TU 자기보고와 `BuildFlags.json`의 겹치는 사실이 서로, 그리고 플래그 프로필과 일치. Jolt 설정 문자열에 Profile / Debug Renderer / FP Exceptions가 없어야 함. Chaos는 `.rsp` 기준 (M13) | 확장 M7 |
| G1 | `S00.FreeFall` (r 50, 1 kg, z0 1000, damping 0, 60 스텝) | \|z60 − 501.83328\| ≤ 2e-3 cm. 기준은 dt_engine으로 계산한 501.8332814이며, 정확한 1/60 기준 501.8333333과 5.2e-5 cm 다르다 (연속해 510은 오답). \|v60 + 980.00005\| ≤ 1e-2 cm/s (dt_engine 기준 v60 = −980.0000511). Null은 같은 기준에 ±1e-9 | MVP M6 (S00 자체는 M3) |
| G1b | `S00d.FreeFallDamped` | 엔진이 선언한 damping 공식과 1e-3 cm 이내 | MVP M6 |
| G2 | `S01.UnitImpulse` (10 kg, J=1000 → Δv=100 cm/s), `S02.SpinInertia` (100×50×20 cm, 10 kg, ω=L/I; COMNudge (0,0,10) 변형은 래핑 경로를 시험), `S03.CapsuleRest` (R25 L100: 서면 75 cm, 누우면 25 cm) | Δv와 ω는 1e-4 상대오차. 질량·관성·COM readback 1e-5. 캡슐 높이 ±0.5 cm | MVP M6 |
| G3 | `S04.ContactNormal` (viz) | normal·(+Z) > 0.999, step 1에서 Phi < 0. M6: PointOn0/1이 각 표면 위(narrow phase 자세: 스텝 시작 또는 예측 자세), v1.0 flag 규칙(정확한 임펄스와는 한 방향: 임펄스 > eps ⇒ ACTIVE, ACTIVE ⇒ 임펄스 ≥ −eps), UEParity에서 깬 상자의 유지 점 제외 ([Abi.md](Abi.md) §9.1 결정 2·3) | MVP M6 |
| G4 | `S05.RestitutionCombine` (e 0.8, 임계 Normalized 50 cm/s / UEParity 엔진 매핑, 모두 첫 충돌 620.67 cm/s 아래) | 아래 "반발 게이트" 참고. 바닥 e=0일 때 Average 0.4, Multiply 0, Max 0.8 (e 기준, apex 비로는 0.16 / 0 / 0.64) | MVP M6 |
| G5 | `S06.FrictionSlide` (v0 500, μ 0.5) | 정지 거리 250.947 cm ±1% (dt_engine 기준 이산해, 연속해 255.10). 30° 경사에서 μ≥0.6은 정지, ≤0.55는 미끄러짐 | MVP M6 |
| G6 | sleep 계약 | Normalized: 정지 600 프레임 동안 sleep 0. UEParity: `S08.StartAsleep`의 `bStartAwake=false` dynamic 바디가 frame 0 ReadBodyStates에서 Sleeping (§6 순서 검증, PhysX putToSleep 포함). `S08`은 UEParity에서만 돈다 (Normalized는 5단계를 건너뜀, §6) | MVP M6 |
| G7 | `S07.ClampProbe` (ω0 50/80/120) | UEParity에서 62.83 ±0.01. Normalized는 해제 (Bullet: 속도는 해제되지만 47.1 rad/s 위에서 스텝 회전각이 줄어드는 것이 문서화된 예외, §8.4 M5 정정) | MVP M6 |
| G8 | DET1 | fresh process 3회에서 h_native 동일 | 확장 M7 |
| G9 | effective readback | READBACK 키 전부 일치. `M3.BoxPile` 1k에서 `shape.wrapping` 합계 0 | MVP M6 |
| G10a | bench 위생 | listeners 0, 드로어 분리, 계측 tier 0 | 확장 M7 |
| G10b | 단일 스레드 증명 | T=1에서 cpu_eff ∈ [0.98, 1.02], 외부 스레드 cycle < 0.5% | 확장 M10 |
| G11 | 결과 중립 knob | 접촉 캡처 on/off 시 `M3.BoxPile` 1k 600 프레임 비트 동일 (M7). `Bullet.StaticAabbOff` 해시가 `Bullet.Default`와 동일 (M9, 변형이 생길 때) | 확장 M7 / M9 |
| G12 | 용량·오류 | Jolt update error 0, PhysX error callback 0, Bullet 풀 heap fallback 0 | MVP M6 |

**반발 게이트 (G4, 그리고 `M5.BouncingBalls`)**
- 60 Hz에서 h0 = 200 cm면 충돌 속도는 626 cm/s, 스텝당 10.4 cm를 움직인다. speculative contact가 없는 엔진은 접촉이 늦고 깊게 시작한다. PhysX UE4Compat의 contactOffset은 r=25에서 2 cm이고 maxDepenetration은 PX_MAX_F32다. 그러면 침투 해소가 반발에 더해져 apex 비가 ±0.02를 벗어날 수 있다.
- 그래서 둘 중 하나를 쓴다:
  - `Analytic::StepBoundaryDropHeight`(C++, M6. Python 쪽은 M8의 `analytic.py` `step_boundary_drop_height`)가 z0를 골라, 이산 궤적이 정확히 스텝 경계에서 바닥에 닿게 한다 (`S05` v2는 그 위 0.01 cm TouchGap). 낙하 거리 = g·dt_e²·k(k+1)/2이며, 예를 들어 k = 38이면 201.72 cm다.
  - 또는 접촉 구간 직전·직후 프레임의 COM 법선 속도로 e = −v_after / v_before를 잰다. apex 비는 보조 지표로 남긴다.
- 엔진별 허용오차는 사유와 함께 `tolerances.json`에 **미리** 등록한다.

**M6 구현 메모 [V]** ([Progress.md](Progress.md) M6, [Scenes.md](Scenes.md) §2.4~§2.12)
- **위치와 실행:** 게이트는 `Source/Runtime/PhysicsTestConformance/{Public,Private}/Gates/`(`IConformanceGate::Run(FGateTarget, FGateRunState)`)에 있다. `PhysicsTestAutomation`의 `PhysicsTest.Gates.G1`~`G12`(CTest 게이트마다 하나)는 Null과 레지스트리의 모든 SDK 엔진 × 변형 파일 × UEParity·Normalized(14 대상)를 돈다. 대상 목록은 빌드가 구성한 SDK 백엔드(`PT_GATE_SDK_BACKENDS`)와 03의 변형 목록에 맞아야 한다. `PhysicsTestBench -Conformance`는 같은 게이트를 한 대상에 돌려 `conformance.json`을 쓴다 (`PhysicsTest.Bench.Conformance`). 허용치는 `Config/Golden/tolerances.json`(schema `physicstest.tolerances/1`, 모든 기본값·override·허용 Skip에 사유가 있어야 로드된다. 실행 뒤 등록한 값은 `"registered": "post-run"`)이다. **Skip은 `allowed_skip`에 등록된 엔진(Null·NullDll의 G2 S03, G3, G4, G5)만 통과하고 그 밖은 Fail이다** (M6 수정 라운드 0, `PhysicsTest.Gates.SelectionRules`). `-Gates=G12`만 골라도 앞의 게이트를 모두 돌린다 (G12는 그 프레임의 오류 bit를 본다).
- **기준값:** 모두 `Analytic::EngineDeltaTime`(dt_engine)으로 계산한다. G1b는 `body.damping_formula`의 공식 id(1 = UE 순서, 2 = 힘 전 감쇠)로 `Analytic::DampedFreeFall`을 고른다. G4의 낙하 높이는 `Analytic::StepBoundaryDropHeight`(K = 38, 201.7167 cm), G5의 정지 거리는 `Analytic::FrictionSlideStopDistance`(250.947 cm)다.
- **G4의 e:** 03 M6 문장대로 접촉 프레임과 직전 프레임의 법선 속도 비 e = −v(c)/v(c−1)이다. 엔진이 −e를 거는 속도를 모델로 등록한다 (`G4.restitution_velocity_model`): 0 = 접촉 스텝 전 속도 |v(c−1)| (Jolt·Bullet, 0.80000), 1 = 그 스텝의 중력을 더한 솔버 전 속도 |v(c−1)| + g·dt (PhysX: DyDynamics.cpp:1979, DyContactPrepShared.h:305-323 [V], e(1 + g·dt/|v|) = 0.82105; PGS 0.82105, TGS 0.81949. M6 수정 라운드 2의 `S05` v2(step 38 끝 0.01 cm 틈)에서는 PGS·TGS 모두 0.82105이고 Bullet은 투기적 접촉의 틈 / dt 때문에 0.79903). 게이트는 모델의 예측과 ±0.02(03 기본값, 모든 엔진)이고 다른 모델보다 가까운지를 본다. apex 비는 접촉 스텝 뒤 높이(PGS·Jolt·Bullet은 v_after·dt, TGS는 약 0)에서 시작한 이산 비행과 ±0.02로 비교한다. M6 첫 판은 PhysX에 e ±0.025·apex ±0.04를 실행 뒤 등록했었다 (M6 수정 라운드 0에서 모델로 바꿨다). 중력 보정 v_in으로 바꾸면 Jolt·Bullet이 0.7795가 되어 e 정의는 그대로 두었다.
- **G5의 정지 판정:** μ 0.6 상자는 frame 10→20의 경사 방향 가속도(≤ 2 cm/s²), 변위(frame 20 ≤ 0.1 cm), 위치로 잰 경사면 위 creep 속도(frame 10-20과 110-120, ≤ 0.75 cm/s)로 본다. 보고 속도가 아니라 위치를 쓴다: PhysX TGS는 쉬는 상자에 움직임과 맞지 않는 약 0.5 cm/s 속도를 보고한다. PhysX PGS(UE4Compat·-Stab·SdkDefault)는 스텝 1에 얻은 속도를 마찰이 지우지 못해 경사면 위로 2.5 cm/s 등속 creep을 하지만(경사 아래 성분과 함께 +Y로 0.8 cm/s, PGS 마찰은 마지막 3 위치 반복과 축별 처리, PxSceneDesc.h:30-42, :245-259 [V]. 속도 반복 4회면 frame 20에 0.32 cm, TGS 0.03 cm) 가속하지 않는다: 등록 편차 변위 1.0 cm, creep 속도 3 cm/s (실행 뒤 등록, 리드 확인 대상).
- **G6:** UEParity는 `S08.StartAsleep`, Normalized는 `M1.SingleBoxDrop` a 600 프레임의 잠든 바디-프레임 0이다. `S08`은 Normalized에서 거부된다 (`RequiresSleeping`). UEParity에서는 60 프레임 잠든 바디에 `PT_CMDF_AUTO_WAKE` 없이 `SET_LINEAR_VELOCITY` (100, 0, 0)·0 벡터, `SET_ANGULAR_VELOCITY` (0, 0, 2), `SET_BODY_STATE`(STATE_WRITE)를 각각 새 실행에서 쓰고, 다음 스텝에 Dynamic이고 그 속도로 움직이는지 본다 (ABI v1.0 결정 1을 Chaos DLL에서도 지키게, M6 수정 라운드 0).
- **G7의 스텝 회전각:** 속도 한계와 함께 마지막 스텝의 회전각을 엔진 모델로 본다: 0 = |ω|·dt (Jolt, PhysX), 1 = 2·atan(|ω|·dt/2) (Null = Chaos 1차 쿼터니언 갱신, Rotation.cpp:158-162 [V]), 2 = Bullet π/4 한계 (btTransformUtil.h:44-72 [V], DL16).
- **G9:** `M3.BoxPile` 1k(UEParity·Normalized)의 READBACK 키마다 `tolerances.json`의 규칙(`profile:`, `option:`, `init:`, `capacity:`, 사유를 단 `const:`/`min:`, 텍스트 규칙, 사유를 단 `unchecked`)이 있어야 하고 그 값과 같아야 한다. 규칙 찾기는 `<Engine>.<Variant>` → `<Engine>` → `*`다. 엔진 객체마다 규칙을 대조하는 키(결합 모드, contact offset 규칙, 정지마찰 규칙)는 canonical = 규칙을 지키지 않는 객체 수이고 `const:0`과 비교한다 ([Abi.md](Abi.md) §5.14, M6 수정 라운드 0. 전의 `all_of_n` 텍스트 규칙과 Jolt·Bullet 결합 키의 `unchecked`를 대신한다). 키 수: Null 9, Jolt 22 (unchecked 1), Bullet 24 (unchecked 1), PhysX 38 (unchecked 0).
- **G12:** 모든 게이트 씬과 BoxPile 1k 120 프레임에서 모은 엔진 오류 bit가 0이다.

**결정성 (DET)**

| ID | 조건 | 단계 |
|---|---|---|
| DET1 | 같은 exe, 같은 T | 확장 M7 |
| DET2 | T = 1..6 | 확장 M15 |
| DET3 | 생성 순서 셔플 | 확장 M15 |
| DET4 | 플래그 프로필 간 (차이 예상, 문서화) | 확장 M15 |
| DET5 | 엔진 결정성 스위치 on | 확장 M15 |

**골든 네이티브 해시 [확장 M7]:** 회귀 경고로만 쓴다. SDK나 플래그가 바뀌면 다시 기준선을 잡는다.

**바이너리 구성 대조군 [확장 M10]:** `test-<engine>`과 `test`의 차이가 1% 이내여야 한다. 넘으면 헤드라인을 단일 SDK exe 기준으로 잡는다.

## 15. Chaos 백엔드 연결 계획 (나중, 사용자 구현) [확장 M13, 스파이크 M2.5]

### 15.1 빌드 (D2)

`Unreal/ChaosBridgeHost/ChaosBridgeHost.uproject`에 타깃 `PhysicsTestChaos`를 둔다.

**Target.cs 설정**
- `Type=Program`, `LinkType=Monolithic`, `bShouldCompileAsDLL=true`, `LaunchModuleName=PhysicsTestChaos`
- `DefaultBuildSettings=Latest`
- `FPSemantics=Imprecise`: Program 타깃은 V7 기본값이 /fp:precise이므로 반드시 지정한다 (VCToolChain.cs:1324-1330 [V]).
- `bCompileAgainstEngine=false`, `bCompileAgainstCoreUObject=true`
- `bHasExports=false`: Monolithic 타깃에서는 이미 기본값이다 (`bHasExportsOverride ?? (LinkType == Modular)`, TargetRules.cs:1794 [V]). 명시적으로 지정해 `*_API`가 비어 있고 import library가 생기지 않게 한다. `PT_GetBackendApi`는 자체 `__declspec(dllexport)`로 이 설정과 무관하게 export된다.
- `bUsesSlate=false`, `bCompileICU=false`
- `NOINITCRASHREPORTER=1`
- 선택: `bUseLoggingInShipping` (Shipping 민감도 빌드용)

**Build.cs**
- 의존 모듈: Core, CoreUObject, ApplicationCore, Projects, GeometryCore, PhysicsCore, Chaos, ChaosCore, 그리고 `SetupModulePhysicsSupport`.
- `PrivateIncludePathModuleNames.Add("Launch")`: `PtChaosModule.cpp`가 include하는 `RequiredProgramMainCPPInclude.h`는 `LaunchEngineLoop.h`와 `Runtime/Launch/Private/LaunchEngineLoop.cpp`를 include한다 (RequiredProgramMainCPPInclude.h:6,11 [V]). HeadlessChaos도 Launch 경로를 추가한다 (HeadlessChaos.Build.cs:20 [V]). 빠지면 SP1이 첫 컴파일에서 실패한다.
- include 경로: `<repo>/Source/Runtime/PhysicsTestAbi/Public` (ModuleDirectory에서 4단계 위).
- 정의: `PT_ABI_NO_EXCEPTIONS=1`, `PT_BACKEND_NS=ChaosBridge`(UE `::Chaos`와 겹치지 않는 이름, §7.3), `PT_KIT_CURRENT_THREAD_ID()` → FPlatformTLS.

**출력 위치:** `Binaries/Win64/PhysicsTestChaos.dll`, `PhysicsTestChaos-Win64-Test.dll`. 상대 엔진 경로가 박히므로 그 자리에서 로드한다.

**CMake 연결:** `PtChaosUbt.cmake`가 다음을 맡는다.
- `PT_UE_ROOT` 검증: Build.bat이 있고 Build.version이 5.8.x인지.
- `build_chaos.cmd <Development|Test>` 호출: 깨끗한 환경에서 `Build.bat PhysicsTestChaos Win64 <Cfg> -Project=... -WaitMutex -NoHotReload`.

### 15.2 호스트 규칙

- affinity를 설정한 **뒤** `LoadLibraryExW`를 호출한다. 그래야 UE 워커가 affinity를 상속한다.
- 로드 직후 호스트 예외 필터를 재설치한다.
- `PT_GetBackendApi(PT_ABI_VERSION_MAJOR)`를 호출한다. 호스트가 빌드된 메이저 버전을 요청하므로, M6 동결 뒤의 호스트(메이저 1)는 메이저 0으로 빌드한 스파이크 DLL을 거부한다 (NULL). 브리지는 v1.0 헤더로 빌드한다.
- 레지스트리는 UE 모듈을 하나만 허용한다.
- FreeLibrary는 하지 않는다.
- Dev와 Test DLL은 같은 프로세스에 함께 두지 않는다. 뷰어는 Dev, 벤치는 Test를 쓴다.
- 벤치는 결과와 `exit.json`을 모두 쓴 뒤 `TerminateProcess`로 끝낸다 (§12.2. 호스트 쪽은 M7 의무이고 `-Conformance`·`-Describe`·`-SceneHashes`도 포함한다. M6까지는 구현되지 않았다).

### 15.3 `InitGamePhys` 순서 (init 스레드가 GameThread가 됨)

1. `FPlatformMisc::SetOverrideProjectDir(SavedDir)`
2. `UE::Geometry::ExactPredicates::GlobalInit()` (HeadlessChaosApp.cpp:547)
3. `GEngineLoop.PreInit("PhysicsTestChaos -ABSLOG=<dir>/chaos.log -unattended -corelimit=<T>")`
4. `ProcessNewlyLoadedUObjects()`
5. `FModuleManager::Get().StartProcessingNewlyLoadedObjects()`
6. `InitGamePhysCore()`
7. 모듈 CVar 설정: T=1이면 `p.Chaos.SingleWorkerPhysics=1` (선택: `p.Chaos.DisablePhysicsParallelFor=1`), T≥2이면 `p.Chaos.MaxNumWorkers=T`. 그리고 `[ConsoleVariables]`의 값.
8. 호스트 로그 콜백으로 보내는 `FOutputDevice` 등록

**금지:**
- `RequestExit`: PostQuitMessage를 부른다 (WindowsPlatformMisc.cpp:1517-1520).
- `SetStandaloneSolver(true)`: 시뮬레이션이 진행되지 않는다 (PhysicsSolverBase.cpp:649-653).
- `FreeLibrary`.

### 15.4 씬 매핑 (요약)

**CreateScene**
- `FChaosScene(nullptr, AsyncDt)`. sync이면 AsyncDt는 -1이다. 0을 넘기면 async가 켜진다 (`IsUsingAsyncResults`: `AsyncDt >= 0`).
- `SceneSolver->SetThreadingMode_External(T==1 ? SingleThread : TaskGraph)` (PhysicsSolverBase.h:615 [V]). 생성자는 `FApp::ShouldUseThreadingForPerformance()`로 모드를 고르므로 (ChaosScene.cpp:73-77 [V]) 생성 후 명시적으로 덮어쓴다. 내부 병렬은 §15.3의 CVar가 정한다 (§9.6).
- 생성자가 UPhysicsSettingsCore CDO의 SolverOptions를 적용한 **뒤에**(ChaosScene.cpp:88-93 [V]) `SceneSolver->ApplyConfig(<리졸브된 SolverOptions>)`와 `SceneSolver->SetIsDeterministic(<bEnableEnhancedDeterminism>)`를 명시적으로 다시 호출한다 (§10).
- 나머지 CVar를 매핑한다.

**CreateActors** (§6의 UE 순서)
1. 바디마다 `FChaosEngineInterface::CreateActor` + 생성 시 플래그 (kinematic, gravity, CCD, MaxLinearVelocity, inertia conditioning, gyroscopic, iteration counts; BodyInstance.cpp:1301-1340)
2. `SetGeometry`: TSphere / TBox / FCapsule / FConvex
   - 박스 margin = `min(2·minHalf·CollisionMarginFraction, CollisionMarginMax)` (ChaosInterfaceUtils.cpp:156 [V]). 이 함수는 Engine 모듈에 있어 링크할 수 없으므로 브리지가 재구현한다.
3. `Chaos::Filter::FShapeFilterBuilder` (Chaos 모듈. BlockMask를 uint64로 넓힘, §5.1)
4. 머티리얼: 모든 셰이프에 필요하다. 없으면 sleep이 `p.Chaos.Solver.Sleep.Defaults.*` CVar 임계값으로 떨어진다 (IslandManager.cpp:121-131 [V]).
5. `AddActorsToScene_AssumesLocked`를 배치 전체에 한 번 호출한다.
6. 바디마다 InitDynamicProperties 순서로 설정한다. 전달받은 mass, inertia, COM을 그대로 쓰고 ^0.75로 재계산하지 않는다 (G2가 검증).
7. `!bStartAwake`이고 kinematic이 아니면 `SetWakeCounter_AssumesLocked(0)` + `PutToSleep_AssumesLocked` (G6 `S08.StartAsleep`). sleep을 끈 프로필에서는 건너뛴다 (§6).

**프레임:** 모든 tick mode에서 Native driver를 쓰고, NumStepsTaken을 돌려준다. AsyncFixed에서는 GT 상태가 보간된다는 점을 manifest에 `async_interpolated=true`로 남긴다 (§9.2).

**viz 기능**
- 접촉: collision event 또는 constraint 순회. 안전을 위해 SingleThread + SingleWorkerPhysics로 캡처한다.
- 네이티브 드로: `FChaosDDScene` + `Solver->SetDebugDrawScene` + `FChaosDDContext::SetIsDebugDrawEnabled(true)` + `IChaosDDRenderer` 구현 (`RenderLatentCommand` 포함) → `PtDebugDrawCmd`. Development에서만 가능하다 (CHAOS_DEBUG_DRAW, Declares.h:20).

### 15.5 구성과 진단

| 구성·백엔드 | 용도 |
|---|---|
| Test | 헤드라인 |
| Development | 시각 확인 |
| Shipping + logging (선택) | 민감도. Test에도 !SHIPPING AABB 통계 블록이 남아 있기 때문 (ChaosScene.cpp:514-545) |
| `PhysicsTestChaosAvx2` (선택) | MatchedAVX2 |
| `Chaos.Evolution` | 같은 DLL 안의 두 번째 씬 구현 (FPBDRigidsEvolutionGBF, D2 진단) |
| `PT_INSTR_PHASES` | `TSimCallbackObject` (Presimulate, PostSolve)로 solver 시간 측정 |

### 15.6 스파이크 게이트 (M2.5, 선택, 3 달력일 타임박스)

스파이크 DLL은 ABI v0.9(메이저 0)로 빌드한다. M6 동결(v1.0, 메이저 1) 뒤에 스파이크를 하면 v1.0 헤더로 빌드한다 (메이저 0 DLL은 지금 호스트가 로드하지 않는다).

| 게이트 | 기준 | 실패 시 대안 |
|---|---|---|
| SP1 | `RequiredProgramMainCPPInclude.h`가 컴파일된다 (Launch include 경로). DLL과 intermediate가 프로젝트 안에 생성됨. export는 PT_GetBackendApi 하나, 의존성은 CRT와 시스템뿐. .rsp에 /fp:fast | .def 파일 (ModuleDefinitionFile) / 저장소 밖 프로젝트 |
| SP2 | PreInit가 0 반환, ABSLOG 경로 확인, Engine/Programs에 아무것도 생기지 않음, WM_QUIT 없음, 예외 필터 복구됨, MXCSR·스레드 수·타이머·DPI·작업 디렉터리 기록 | OOP EXE (M14) |
| SP3 | M2 자유낙하 픽스처(`S00.FreeFall`과 같은 설정. 스파이크는 M2에만 의존하므로 M3의 씬 대신 픽스처를 쓴다) z60 = 501.833 ±0.01 cm (T=1 SingleThread + SingleWorkerPhysics + `-corelimit=1`과 TaskGraph 모두), create/destroy 10회, 메모리 증가 없음, 교착 없음, 종료 시 `TerminateProcess` 경로 확인 | OOP EXE (M14) |
| SP4 | Test DLL이 두 번째 프로세스에서 동작하고 debug draw는 UNSUPPORTED를 돌려줌 | Shipping + logging |
| SP5 | A0~A6 통과, M2 자유낙하 픽스처에 G1 허용치(\|z60 − 501.83328\| ≤ 2e-3 cm)를 적용해 통과, `docs/ChaosSpike.md` 작성, ABI 변경 목록 정리 (1 pd 이내). M6 동결에 반영 | - |

### 15.7 OOP 대안 [확장 M14, 조건부]

`PhysicsTestChaosServer` (같은 모듈, `PT_CHAOS_SERVER=1`, EXE)가 같은 ABI를 shared memory로 제공한다. 호스트 쪽은 transport만 바꾸면 된다.

### 15.8 사용자 구현 범위

- 규모: `FPtChaosBackend`, `FPtChaosScene`, `FPtChaosEvolutionScene`, `PtChaosDefaults.h` (모든 기본값에 file:line 근거). 약 1.3~1.6k LOC.
- 템플릿: Null 백엔드.
- 지침: `docs/ChaosBackendGuide.md`. 초안은 M6에 쓰고, 다음을 의무사항으로 담는다.
  - T=1 스레드 설정 (§9.6, Parallel.cpp:95 인용)
  - §6의 생성 순서와 putToSleep 조건
  - sync에서 AsyncDt = -1
  - SolverOptions 명시 적용 (§15.4)
  - 모든 셰이프에 머티리얼
  - Build.cs의 Launch 경로
  - Dev와 Test의 capability 차이
  - `threads.single_worker`, `threads.max_num_workers` effective 키
- 완료 기준: 선언한 capability 범위에서 A0~A7, A9, A10과 G0~G12를 통과한다 (G0는 `.rsp` 기준). G10b와 스탬프는 M10 이후, 조인트·스냅샷·네이티브 드로는 M9 이후에 확인한다 ([03_WorkPlan.md](03_WorkPlan.md) M13 완료 기준 (a)~(c)).

## 16. 라이선스 정책 (D7) [MVP M0]

**코드**
- UE 소스와 UE 파생 코드를 넣지 않는다. UE 파일·함수명은 주석에서 인용만 한다.
- 4.18 미러에서는 아무것도 복사하지 않는다. UE4에서 유래한 동작은 증거 태그가 붙은 ini 키로 재정의한다.
- Chaos glue는 엔진 헤더를 include하며 로컬 UE에서 UBT로만 컴파일한다. `PT_WITH_CHAOS`는 기본 OFF다.

**저장소에 넣지 않는 것 (.gitignore)**
- `out/`, `Saved/`, `CMakeUserPresets.json`, `.venv/`(저장소 루트, M0), `Tools/ptbench/wheelhouse/`
- UE Binaries, Intermediate, Saved, DerivedDataCache
- `*.generated.h`, `*.gen.cpp`
- DLL, PDB

Chaos DLL은 커밋하거나 배포하지 않는다.

**고지와 SDK**
- `THIRD_PARTY_NOTICES.md`에 빌드 시 받아오는 모든 의존성과 라이선스를 적는다:

| 의존성 | 라이선스 |
|---|---|
| PhysX 5.11 `physx/` | Apache-2.0 (재배포 시 NOTICE 의무. 저장소 루트 LICENSE의 BSD-3과 다름) |
| Jolt 5.6.0 | MIT |
| Bullet 3.25 | zlib |
| Dear ImGui | MIT |
| ImPlot | MIT |
| stb | MIT 또는 Public Domain (택일) |
| nlohmann/json | MIT |
| doctest | MIT |
| Tracy (prof 프리셋만) | BSD-3-Clause |

- SDK는 빌드 시점에 받아오며 저장소에 포함하지 않는다.

**결과 공개**
- UE EULA는 HTTP 403으로 확인하지 못했다. 사용자가 EULA와 각 SDK 라이선스의 벤치마크 조항을 확인하기 전까지 결과는 비공개로 둔다.
