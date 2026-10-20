# 01. 타당성 검토 (Feasibility Review)

> **작성일:** 2026-10-20
> **대상 버전:** Unreal Engine 5.8.3 (`D:\UnrealEngine`) · PhysX SDK 5.11.0 (NVIDIA-Omniverse/PhysX `da950a35`) · Bullet 3.25 (`2c204c49`) · Jolt 5.6.0 (`e77f1755`)
> **검증 환경:** Windows 11 · VS 2022 BuildTools (MSVC 14.44.35207) · Windows SDK 10.0.26100 · CMake 4.1.2 · Ninja 1.13.2 · i5-13600KF (6P+8E, 20 logical) · RTX 3080
> **근거 표기:** **[V]** 소스를 읽었거나 직접 빌드·실행해 확인 · **[D]** 문서나 웹 페이지로 확인 · **[R]** 기억이나 추정이라 미검증

이 문서는 "Unreal의 Chaos가 Bullet, PhysX, Jolt보다 실제로 느린가, 그리고 결과가 어떻게 다른가"를 Windows용 CMake 프로젝트로 측정할 수 있는지 검토합니다. 검토 방법은 소스 판독과 빌드 실험입니다.
- 세 SDK는 이 PC에서 실제로 빌드하고 hello 테스트를 돌렸습니다.
- D3D11 렌더러 프로토타입을 만들어 Debug Draw 성능을 쟀습니다.
- Chaos는 UE 5.8.3 소스를 읽어 단독 실행 경로를 확인했습니다.

---

## 1. 결론 요약

**만들 수 있습니다.** 다만 "Chaos가 느린가"에 답하려면 측정 규약을 먼저 정해야 합니다. 같은 조건을 맞추지 않으면 숫자가 의미를 잃습니다.

| 항목 | 판정 | 한 줄 이유 |
|---|---|---|
| Bullet 3.25 (단일 스레드 world) | **GO** | `add_subdirectory` 빌드가 약 0.4분. hello 테스트 통과. 실행할 때마다 비트 단위로 같은 결과 |
| PhysX 5.11 (UE4 방식 설정) | **GO (조건부)** | CPU 전용 정적 빌드가 약 1분이고 packman이 필요 없음. UE4가 실제로 쓴 버전은 3.4이므로 "PhysX 5.11에 UE4 설정"이라고 표기 |
| Jolt 5.6.0 | **GO (조건부)** | FetchContent 빌드가 22초. Ninja에서는 AVX2가 조용히 꺼지는 함정 같은 빌드 함정을 모두 우회법과 함께 확인 |
| Chaos (나중에 직접 구현) | **위험 (스파이크 전까지)** | Engine 모듈 없이 도는 경로를 소스로 확인. 하지만 UBT DLL 빌드는 아직 한 번도 돌려 보지 않음 |
| CMake 기반 Windows 빌드 | **GO (조건부)** | 세 SDK + imgui/implot을 Ninja 트리 하나로 빌드하는 데 95초. MAX_PATH와 CRT 통일이 조건 |
| Chaos Debug Draw 스타일 뷰어 | **GO** | D3D11 프로토타입이 와이어 도형 3만 개(선분 208만 개)를 GPU 0.83 ms에 그림 |
| 공정한 속도 비교 | **위험** | 아직 측정 규약이 없음. 이 PC는 부하가 크고 전원 계획이 Balanced이며 P/E 하이브리드라, 실행마다 최대 2.5배 차이 |
| 결과(거동) 비교 | **GO (조건부)** | 가능하고 가치도 있음. 다만 차이의 상당 부분이 엔진 기본값 차이이므로 기본값을 통제해야 함 |
| Actor/Component 없는 UE식 단일 씬 구조 | **GO** | UE 5.8.3의 `FChaosScene` + `FChaosEngineInterface` 계층과 그대로 맞물림 |

**권장 진행 방식:**
1. Bullet, PhysX, Jolt 백엔드와 뷰어를 먼저 만듭니다.
2. 1~2일짜리 **"hello Chaos DLL" 스파이크**를 일찍 돌려 Chaos 연결 경로를 검증합니다.
3. 그 결과로 백엔드 경계(C ABI)를 확정한 뒤 Chaos 백엔드를 구현합니다.

구체적인 설계는 [02_Architecture.md](02_Architecture.md), 일정은 [03_WorkPlan.md](03_WorkPlan.md), 결정 사항과 남은 질문은 [04_DecisionLog.md](04_DecisionLog.md)에 있습니다.

---

## 2. 질문을 측정 가능한 형태로 바꾸기

### 2.1 "Chaos 속도"의 정의

Chaos는 몇 층으로 감싸져 있습니다. 어느 층을 재느냐에 따라 숫자가 달라집니다.

| 층 | 무엇을 재는가 | 쓰임 |
|---|---|---|
| L1: `FChaosScene` + `FChaosEngineInterface` | 게임이 매 프레임 거치는 경로: `SetUpForFrame` → `StartFrame` → `EndFrame`. GT↔PT 마샬링과 SQ 구조 복사까지 포함 | **헤드라인 수치.** "Unreal에서 Chaos를 쓰면 느린가"에 대한 답 |
| L0: `FPBDRigidsEvolutionGBF` 단독 | 솔버 루프만 (`AdvanceOneTimeStep` + `EndFrame`) | 진단용. L1과의 차이가 "통합 비용" |

- `FChaosScene`은 `FPhysScene_Chaos`의 기반 클래스입니다. `UWorld`나 Engine 모듈 없이도 생성되고 스텝을 돕니다. HeadlessChaos 테스트(`Programs/HeadlessChaos/Private/HeadlessChaosTestPhysicsScene.cpp:154-252`)가 실제로 그렇게 씁니다 **[V]**.
- `FChaosScene` 생성자는 프로젝트 설정의 `SolverOptions`와 스레딩 모드를 적용합니다 (`PhysicsCore/Private/ChaosScene.cpp:56,73-77,90-94`) **[V]**.

### 2.2 "PhysX를 UE4처럼"의 정의

- UE4(4.14~4.27)가 실제로 탑재한 것은 **PhysX 3.4**입니다. UE 5.8.3에 남아 있는 `Engine/Binaries/ThirdParty/PhysX3/Win64/VS2015/PhysX3_x64.dll`의 FileVersion이 3.4.0.0입니다 **[V]**.
- 하지만 UE4의 PhysX 통합 소스는 로컬에 없습니다. `Engine/Source/ThirdParty/PhysX3`에는 `.ubtignore`만 있습니다 **[V]**.
- 그래서 1단계에서는 **로컬 PhysX 5.11에 UE4 설정을 적용한 프로필(`UE4Compat`)**을 씁니다. SDK 기본값 프로필(`SdkDefault`)도 함께 둡니다. 결과에는 반드시 "UE4 settings on PhysX 5.11"이라고 표기합니다.
- UE4 설정값의 상당수는 UE 5.8.3에 "흔적"으로 남아 있어 직접 확인했습니다 **[V]**:
  - `PhysicsCore/Private/PhysicsInitialization.cpp:18-28`: `p.ToleranceScale_Length` = 100, `p.ToleranceScale_Speed` = 1000 (ReadOnly CVar).
  - `PhysicsSettingsCore.cpp:27-49`: `bEnablePCM` true, `bEnableStabilization` false, `BounceThresholdVelocity` 200 cm/s, `ContactOffsetMultiplier`/`Min`/`Max` = 0.02 / 2 / 8 cm, `SimulateScratchMemorySize` 262144, `MaxAngularVelocity` 3600 deg/s, `DefaultGravityZ` -980.
  - `PhysicsSettings.cpp:21-27`: `MaxPhysicsDeltaTime` 1/30, `MaxSubstepDeltaTime` 1/60, `MaxSubsteps` 6, `PhysXTreeRebuildRate` 10.
  - 이 필드들은 5.8.3 런타임 어디에서도 읽히지 않습니다. PhysX 시절의 흔적입니다.
- `PxTolerancesScale(100, 1000)`이면 PhysX 5.11의 공식 자체에서 UE 기본값이 그대로 나옵니다 **[V]** (`PxSceneDesc.h:1082-1083`):
  - `bounceThreshold` = 0.2 × speed = **200 cm/s**
  - `contactOffset` = 0.02 × length = **2 cm**
- 반면 PhysX 5.11의 기본값은 UE4가 기대했던 3.4 기본값과 다릅니다 **[V]**:
  - broadphase 기본값이 `ePABP`입니다 (3.4는 `eSAP`).
  - 바디당 반복 횟수가 PGS 4/1입니다 (UE4는 8/1).
  - joint projection이 v5.2에서 제거됐습니다.
  - `PxD6Drive::eSWING`이 v5.7에서 제거됐습니다.
  - `PxCooking` 클래스가 사라지고 독립 함수(`PxCreate*Mesh`)로 바뀌었습니다.
  - 개선된 patch friction이 항상 켜져 있습니다.
- 따라서 `UE4Compat` 프로필은 `eSAP`, 8/1 같은 값을 **명시적으로** 설정해야 합니다.
- **주의:** UE4 내부 동작 중 일부(filter shader의 word 배치, 씬 플래그 조합, D6 joint 매핑)는 조사 과정에서 **공개 제3자 미러에 올라온 UE 4.18 소스**를 읽어 확인한 것입니다. 이 미러는 무단 재배포일 가능성이 높으므로:
  - 이 저장소에는 그 코드를 한 줄도 옮기지 않습니다. 동작만 보고 새로 구현합니다(clean-room).
  - 4.19~4.27의 변경분(MBP region, contact modify, RestOffset 등)은 읽지 못했습니다. Epic 계정과 연결된 GitHub의 UE 4.27 소스로 확인하기를 권장합니다.

### 2.3 기본값이 결과를 지배한다

같은 상자 하나(1 m 큐브)를 10 m 높이에서 떨어뜨리는 hello 테스트만 해도 엔진마다 결과가 다릅니다 **[V]** (Y-up, SI, dt = 1/60):

| 엔진 | 60스텝(1초) 후 높이 | 정지 높이 | 잠드는 시점 |
|---|---|---|---|
| PhysX 5.11 | 5.013251 m | 0.499996 m | 103스텝 |
| Bullet 3.25 | 5.013251 m | 0.500000 m | 206스텝 |
| Jolt 5.6.0 | **5.098089 m** | 0.499993 m | 180스텝 이내 |

- 해석해(연속 시간)는 5.095 m이고, 이산 semi-implicit Euler 해는 5.013249 m입니다.
- PhysX와 Bullet은 semi-implicit Euler 해와 일치합니다.
- Jolt만 8.5 cm 위에 있는 이유는 Jolt의 기본 linear damping 0.05 때문입니다 (`Body/BodyCreationSettings.h:107-113`).

즉 기본값을 통제하지 않으면, 엔진 차이처럼 보이는 것이 사실은 **설정 차이**입니다. 주요 기본값을 비교하면 다음과 같습니다 (모두 **[V]**).

| 항목 | Chaos / UE | PhysX 5.11 | Bullet 3.25 | Jolt 5.6 |
|---|---|---|---|---|
| 중력 기본값 | Chaos 원시값 -980.665 cm/s², UE 프로젝트 -980 | 0 | (0, -10, 0) | (0, -9.81, 0) |
| 솔버 반복 | 8 position / 2 velocity / 1 projection | PGS 4 / 1 (바디당) | 10 + split impulse | velocity 10 / position 2 |
| damping (선형 / 각) | UE 바디 0.01 / 0 | 0 / 0.05 | 0 / 0 | 0.05 / 0.05 |
| damping 공식 | v *= max(0, 1 − c·dt) | — | v *= (1 − c)^dt | v *= max(0, 1 − c·dt) |
| 최대 각속도 | 3600 deg/s (62.8 rad/s) | 100 rad/s | \|ω\|·dt ≤ π/2 | 47.1 rad/s |
| 마찰 결합 | Average | Average | 곱 | sqrt(곱) |
| 반발 결합 | Average | Average | 곱 | max |
| 반발 속도 임계 | 1000 cm/s² × dt (60 Hz에서 16.7 cm/s) | 0.2 × speed (UE 스케일 200 cm/s) | 0.2 m/s | 1.0 m/s |
| 박스 마진 | min(크기의 5%, 10 cm) | 정확한 박스 + contactOffset | 박스 안쪽 0.04 m | convex radius 0.05 m |
| 자이로 토크 | 파티클별 플래그 | opt-in 플래그 | 켜짐 | 꺼짐 |
| 정밀도 | 상태는 double (`FReal`), 제약 솔버는 float | float 전용 | float (double 옵션) | float (위치만 double 옵션) |
| 접촉 법선 방향 | body1 위, body0 쪽을 가리킴. phi < 0이면 침투 | Chaos와 같음 | Chaos와 같음 | **반대 방향**. 침투 깊이가 양수 |
| 캡슐 축 | Z | X | Y | Y |

> **설계 반영:** 이 차이 때문에 프로필을 두 가지로 둡니다. **UEParity**는 각 엔진을 UE 기본값에 맞춥니다. **Normalized**는 damping을 0으로, clamp를 해제하고, 결합 규칙을 통일하고, sleep을 끕니다. 질량·관성·COM은 하네스가 직접 계산해 모든 엔진에 명시적으로 넘깁니다.

---

## 3. 백엔드별 검토

### 3.1 Bullet 3.25 — GO

- **빌드 [V]:**
  - `add_subdirectory`로 LinearMath, BulletCollision, BulletDynamics만 빌드합니다. 약 0.4분입니다.
  - Bullet의 `CMakeLists.txt:1`은 `cmake_minimum_required(VERSION 2.4.3)`라서 CMake 4.x가 거부합니다. `CMAKE_POLICY_VERSION_MINIMUM=3.10`을 주면 경고 없이 통과합니다.
- **옵션 함정 [V]:** Bullet 안에서는 CMP0077이 OLD로 동작합니다. 그래서 옵션을 일반 변수로 넘기면 첫 configure에서 무시되고 데모와 테스트까지 다 빌드됩니다. 옵션은 반드시 `CACHE FORCE`로 넘겨야 합니다.
- **CRT [V]:** 부모 프로젝트가 `CMAKE_MSVC_RUNTIME_LIBRARY`를 설정하면 Bullet도 `/MD`를 따릅니다. Bullet의 `/fp:fast`와 `/GR-`는 Bullet 타깃 안에만 적용됩니다.
- **결정성 [V]:** 단일 스레드 world는 실행할 때마다 비트 단위로 같은 결과를 냅니다. 상자 3000개, 240스텝으로 3회 반복해 확인했습니다.
- **멀티스레드 world(`btDiscreteDynamicsWorldMt`) [V]:**
  - 빌드는 됩니다.
  - PPL 스케줄러와 함께 쓰면 segfault가 납니다. `btCollisionDispatcherMt`가 스레드별 배열을 `getNumThreads()` 크기로 만들고 `btGetCurrentThreadIndex()`로 인덱싱하기 때문입니다. 우회법은 확인했습니다.
  - 결과가 단일 스레드와 다르고, 실제로 여러 스레드를 쓰면 비결정적입니다.
  - 그래서 **멀티스레드 world는 별도 백엔드**로 취급하고 1단계에서는 제외합니다.
- **참고 자료:** `examples/Benchmarks/BenchmarkDemo.cpp`에 8개 씬이 있습니다(상자 3000개, 피라미드, 래그돌 136개, convex, 메시, 레이캐스트 등). 이식할 후보입니다.

### 3.2 PhysX 5.11 (UE4 방식) — GO (조건부)

- **빌드 [V]:** CPU 전용, 정적 라이브러리, `/MD`로 configuration 하나당 약 1분입니다(release 62초, 478 Ninja 스텝, 경고 0개).
  - `generate_projects.bat`는 쓰지 않습니다. 이 스크립트는 packman을 받고 `setx PM_PACKAGES_ROOT`로 **사용자 환경 변수를 영구 변경**합니다. CMake를 직접 호출하면 packman도 네트워크도 필요 없습니다.
- **반드시 피해야 할 함정 [V]:**
  1. configure 단계에서 `include/PxConfig.h`를 **소스 트리에 씁니다.** 그래서 `D:\PhysicsRefs`에서 바로 빌드하면 안 되고, 빌드 트리로 clone해서 빌드합니다.
  2. 내보낸 타깃에 `PX_PHYSX_STATIC_LIB`가 빠져 있어 `LNK2019 __imp_PxCreatePhysics`가 납니다. 래퍼 타깃에서 직접 정의해 줍니다.
  3. multi-config 생성기(VS)에서는 부모의 `CMAKE_CONFIGURATION_TYPES`를 debug;checked;profile;release로 FORCE 덮어씁니다. 이 블록은 `if(CMAKE_CONFIGURATION_TYPES)` 안에 있어서 Ninja 단일 config에서는 문제가 없습니다.
  4. 전역 `CMAKE_CXX_STANDARD=20`을 주면 PhysX의 `/std:c++14 /WX`가 깨집니다(C5055). C++ 표준은 타깃별로 지정합니다.
  5. 이 PC는 `LongPathsEnabled=0`입니다. 오브젝트 경로가 262자가 되자 C1083으로 실패했습니다. 빌드 루트는 짧게(`D:\PhysicsTest\out\<preset>`) 둡니다.
- **실행 [V]:**
  - 정적 exe는 CRT DLL 말고는 아무것도 의존하지 않습니다(PhysXGpu, PhysXDevice 불필요).
  - UE4Compat 프로브(cm 단위, 50 cm 큐브)에서 질량이 37.384 kg으로 나와 UE의 mass^0.75 규칙과 일치합니다.
- **관찰된 거동 차이 [V]:**
  - UE4 기본값(PGS 8/1, PCM 켬, stabilization 끔)으로 50 cm 큐브 10개를 쌓았습니다. 최대 약 25 cm까지 흔들리고 5초 동안 잠들지 않았습니다.
  - `eENABLE_STABILIZATION`을 켜면 약 1초, TGS로 바꾸면 약 2초 만에 안정됐습니다.
  - 즉 **엔진보다 설정이 결과를 더 크게 바꿉니다.**
- **스레드 [V]:** 작은 씬에서는 dispatcher의 wake 지연이 스텝 시간을 지배합니다. worker 2개일 때 스텝당 중앙값 47~114 µs, inline(worker 0개)일 때 4~7 µs였습니다. 0개와 4개의 결과는 비트 단위로 같았습니다.
- **PhysX 3.4 백엔드(정확한 UE4 재현):** 공개 저장소에 CMake가 없습니다(vc12~vc15 솔루션뿐). 최신 MSVC에서 경고가 에러로 처리된다는 보고도 있습니다 **[D]**. 1단계에서는 제외하고, 백엔드 경계만 열어 둡니다.

### 3.3 Jolt 5.6.0 — GO (조건부)

- **빌드 [V]:** FetchContent(`SOURCE_SUBDIR Build`)로 Jolt 라이브러리만 22초에 빌드됩니다.
- **조용히 성능을 바꾸는 함정 [V]:**
  - **Ninja에서는 SSE2 전용으로 빌드됩니다.** `Jolt.cmake:969-979`가 `CMAKE_VS_PLATFORM_NAME`(VS 생성기에서만 설정됨)을 보고 `/arch:AVX2`를 붙이기 때문입니다. Pyramid 1스레드 기준 SSE2 45.7 대 AVX2 83.8 steps/s로 약 1.8배 차이였습니다. 다른 작업이 돌던 상태에서 잰 값이라 참고치입니다.
  - Release 빌드에 profiler, debug renderer, FP exception이 기본으로 들어갑니다. Distribution 빌드보다 1.3~2.1배 느립니다(역시 참고치).
  - 기본 CRT가 정적(`/MT`)이라 `/MD` 코드와 섞으면 `LNK2038`이 납니다.
  - DX12/VK compute를 켜 두면 `.dxil`/`.spv`를 소스 트리에 씁니다.
  - `OBJECT_LAYER_BITS`가 `option()`으로 선언돼 있어 32비트로 바꾸려면 `-DOBJECT_LAYER_BITS=32`를 직접 줘야 합니다.
  - 모두 우회법이 검증됐습니다. 백엔드 정적 라이브러리에 Jolt를 PRIVATE로 링크하면 `/arch:AVX2`와 `JPH_*` 정의가 다른 타깃으로 새지 않는 것도 확인했습니다.
- **단위 [V]:** Jolt는 SI 미터를 전제로 튜닝돼 있습니다. cm로 넣은 실험에서는:
  - 속도 clamp(500 units/s)에 걸려 1초 후 높이가 624 cm가 나왔습니다. 정상값은 510 cm입니다.
  - 질량이 1e9 kg이 됐습니다.
  - 그래서 백엔드 경계에서 **반드시 m로 변환**합니다.
- **데이터 노출 [V]:**
  - 접촉 충격량(impulse)은 public API로 꺼낼 수 없습니다(`CachedContactPoint`가 private).
  - island ID는 internal입니다.
  - 그래서 충격량 표시는 "근사"로 표기하고, island는 하네스가 union-find로 직접 계산합니다.
- **참고 자료:** `PerformanceTest`(고정 dt 1/60, `OptimizeBroadPhase` 후 측정, 스레드 수 스윕, steps/s + 해시 출력)가 우리 러너 설계의 좋은 본보기입니다. 다만 해시는 ISA나 빌드 구성이 바뀌면 달라지므로 **엔진 간 비교에는 쓸 수 없습니다** [V].

### 3.4 Chaos (나중에 직접 구현) — 위험 (스파이크 전까지)

**확인된 것 [V]:**
- **CMake로 Chaos 소스를 직접 컴파일하는 것은 불가능에 가깝습니다.** Windows용 `.cpp`가 약 1,429개, `.ispc`가 27개, UHT 반영 헤더가 약 118개입니다. 모듈마다 정의 헤더도 따로 생성됩니다.
- **UBT `Program` 타깃 + Monolithic + `bShouldCompileAsDLL = true`로 DLL을 만드는 방식은 Epic이 이미 씁니다.**
  - `DatasmithSDK`, `DatasmithMax2022`, TextureShare 등 10개 타깃이 이 방식입니다.
  - Datasmith는 DLL 안에서 `GEngineLoop.PreInit`을 호출합니다.
- **모듈 의존성에 Engine이 없습니다.** 필요한 것은 PhysicsCore, Chaos, ChaosCore, CoreUObject, NNE, GeometryCore와 ISPC입니다. ChaosSolverEngine은 Engine에 의존하므로 제외합니다.
- **`HeadlessChaosPerf`는 재사용할 것이 없습니다.** 카운터를 100만 번 올리는 테스트 하나만 들어 있는 빈 틀입니다.
- `HeadlessChaos`는 GoogleTest 단위 테스트 모음입니다. 그 안의 perf 테스트는 `DISABLED_`로 꺼져 있고 테스트 전용 설정을 씁니다.
- 둘 다 로컬에 빌드된 바이너리가 없습니다.

**함정 [V]:**
- **UBT의 `Program` 타깃은 `/fp:precise`가 기본입니다** (`VCToolChain.cs:1324-1330`, `BuildSettingsVersion` V7 이상). 게임 타깃은 `/fp:fast`입니다. 그래서 `FPSemantics = FPSemanticsMode.Imprecise`로 맞추지 않으면 Chaos만 불리해집니다.
- **`.uproject` 이름이 Program 타깃 이름과 같으면** intermediate와 receipt가 `D:\UnrealEngine\Engine` 아래로 갑니다 (`UEBuildTarget.cs:1926-1946`). 이름을 다르게 지어야 합니다(예: `ChaosBridgeHost.uproject` + `PhysicsTestChaos` 타깃).
- **`Test` 구성에서는 `CHAOS_DEBUG_DRAW`가 꺼집니다** (`Chaos/Declares.h:20`). 그래서 시각화는 `Development`에서, 시간 측정은 `Test`에서 합니다.
- **`SingleThread` 스레딩 모드만으로는 단일 스레드가 되지 않습니다.** 솔버 내부의 `PhysicsParallelFor`는 `p.Chaos.SingleWorkerPhysics=1`(또는 `p.Chaos.DisablePhysicsParallelFor=1`)일 때만 한 스레드로 돕니다 (`Chaos/Framework/Parallel.cpp:95`). `-corelimit=1`을 줘도 TaskGraph worker가 남으므로, T=1 측정에는 `SetThreadingMode_External(SingleThread)` + `p.Chaos.SingleWorkerPhysics=1` + `-corelimit=1`을 함께 씁니다.
- **프로세스 전체에 미치는 부작용이 있습니다:**
  - `PreInit`을 호출한 스레드가 GameThread가 되고, TaskGraph worker가 약 18개 생깁니다(logical 코어 수 − 2).
  - `timeBeginPeriod(1)`이 호출되고, 정적 초기화 단계에서 예외 필터가 설치됩니다.
  - `RequestExit`를 부르면 `PostQuitMessage`가 호스트의 메시지 루프를 종료시킵니다.
  - UE DLL은 프로세스당 하나만 올리고, `FreeLibrary`는 하지 않습니다.

**아직 확인하지 못한 것:**
- UBT Program DLL을 실제로 빌드하는 것.
- 남의 프로세스 안에서 `PreInit`을 호출하는 것.
- `Test` 구성 빌드와 export.

빌드 비용 추정은 클린 빌드 10~25분, 구성당 intermediate 3~5 GB입니다 **[R]**.

> **결론:** 설계는 소스로 뒷받침되지만 실증은 0입니다. Chaos 구현 전에 **1~2일짜리 스파이크**를 권장합니다. 내용은 DLL 하나를 만들어 `FChaosScene`에 구 하나를 떨어뜨리고 Z를 출력하는 것입니다. 스파이크가 성공하면 C ABI를 확정합니다. 실패하면 **같은 ABI를 쓰는 별도 프로세스 EXE**로 돌아갑니다. 공유 메모리 IPC가 300~500줄 정도 늘어납니다.

---

## 4. 빌드 환경 (CMake, Windows) — GO (조건부)

- **[V]** imgui, implot, Jolt, Bullet, PhysX를 Ninja 트리 하나로 빌드하면 95.4초입니다(소스는 이미 받아 둔 상태, AVX2 프로필). 각 SDK도 MSVC 14.44 + CMake 4.1.2 + Ninja로 따로 빌드해 확인했습니다.
- **반드시 지킬 규칙:**

| 규칙 | 이유 |
|---|---|
| SDK를 추가하기 **전에** `CMAKE_MSVC_RUNTIME_LIBRARY`를 `/MD`로 설정 | Jolt와 Bullet은 기본이 `/MT`라 `LNK2038`이 남 [V] |
| 전역 `CMAKE_CXX_STANDARD` 금지. 타깃별로 `cxx_std_20` | PhysX가 C5055로 깨짐 [V]. Jolt는 소비자에게 C++ 표준을 전파하지 않음 [V] |
| 빌드 루트는 짧게(`D:\PhysicsTest\out\<preset>`), `%TEMP%` 아래에서 빌드 금지 | `LongPathsEnabled=0`이라 MAX_PATH에 걸림 [V] |
| SDK는 빌드 트리로 clone (URL은 캐시 변수, 기본값은 `file:///D:/PhysicsRefs/...`) | PhysX가 소스 트리에 파일을 씀 [V]. 참조 체크아웃을 보호하기 위함 |
| Ninja 단일 config preset을 기본으로 사용 | PhysX가 multi-config 설정을 덮어씀 [V]. 이 PC에는 VS IDE 없이 BuildTools만 있음 |
| Jolt는 `/arch:AVX2`를 명시하고 profiler/debug renderer/FP exception을 끔 | 끄지 않으면 성능이 조용히 왜곡됨 [V] |
| 각 SDK는 백엔드 정적 라이브러리에 PRIVATE로 링크. 백엔드 공개 헤더에는 SDK include 금지 | 컴파일 플래그와 정의가 새는 것을 막음 [V] |
| Chaos는 `PT_WITH_CHAOS=OFF`가 기본. 켜면 `add_custom_target`으로 UBT `Build.bat` 호출 | UE 없이도 저장소가 빌드되어야 함 |

- **디버깅 도구:** 현재 VS IDE가 없습니다. 둘 중 하나를 고르면 됩니다: VS 2022 Community를 설치하고 "폴더 열기"로 CMakePresets를 쓰거나, VS Code + CMake Tools + cppvsdbg를 씁니다.

---

## 5. Chaos Debug Draw 스타일 뷰어 — GO

- **기술 선택 [V]:** Win32 + Direct3D 11 + Dear ImGui (docking) + ImPlot + stb. 외부 의존성이 셋뿐이고 모두 FetchContent로 받습니다. 셰이더는 System32의 `d3dcompiler_47.dll`로 런타임에 컴파일합니다.
- **프로토타입 성능 [V]** (RTX 3080, 1600×900, 4× MSAA):

| 도형 수 | 1 px 선 | 두꺼운 선(1.5 px) |
|---|---|---|
| 와이어 도형 3만 개 (선분 208만 개) | GPU 0.83 ms | GPU 2.17 ms |
| 1만 개 | GPU 0.30 ms | GPU 0.82 ms |

  - 출력 예: [images/proto_render.png](images/proto_render.png). 도형 배치와 그래프 값은 렌더링 확인용 가짜 데이터입니다.
  - 창 없이 오프스크린으로 그려 PNG를 저장하는 것도 동작합니다. 단, PNG 인코딩이 동기 방식으로 154 ms 걸리므로 영상 캡처는 비동기로 해야 합니다.
- **Chaos와 똑같이 보이게 하는 사양 [V]:**
  - 상태별 색 (`ChaosDebugDraw.cpp:384-393`):
    - Dynamic (255,255,0)
    - Sleeping (128,128,128)
    - Kinematic (0,128,255)
    - Static (255,0,0)
    - Debris (0,255,0)
  - 바운드는 위 색의 어두운 버전입니다.
  - 실제로 적용되는 기본값은 솔버 설정입니다 (`PBDRigidsSolver.cpp:219-247`): LineThickness **1.0 cm(월드 공간)**, PointSize 5, ContactLen 30, ContactWidth 6, BodyAxisLen 30, ArrowSize 10. `ChaosDebugDraw.cpp`에 있는 0.15 / 2는 대체값일 뿐입니다.
  - 테셀레이션은 UE line batcher와 같습니다: 박스 12선, 구 8×8(128선), 캡슐 16면(68선).
  - 접촉은 원판 + 법선으로 그립니다. 색은 활성 (250,0,0), 비활성 (150,150,150)입니다.
  - Chaos는 평면을 그리지 않습니다. 그래서 바닥은 **정적 박스**로 만듭니다.
- **뷰포트 규약 [V]:**
  - Z-up, 왼손 좌표계, cm 단위.
  - 수평 FOV 90°, near 10 cm.
  - WASD 이동, E/Q로 위아래, 휠로 카메라 속도 조절.
- **배경색:** 순수 검정은 피합니다. 잠든 island 같은 검정 요소가 보이지 않기 때문입니다. 예: (40,42,46).

---

## 6. 공정한 속도 비교 — 위험 (규약으로 해결)

현재까지 잰 시간은 모두 **벤치마크 수준이 아닙니다.** 측정할 때 배경 CPU 부하가 11~100%였고, 전원 계획은 Balanced였으며, P/E 하이브리드 CPU라 같은 측정이 최대 2.5배까지 흔들렸습니다 **[V]**.

**확인된 편향 요인과 대응:**

| 편향 요인 | 크기 / 근거 | 대응 |
|---|---|---|
| ISA 차이 | Jolt SSE2 대 AVX2 약 1.8배 [V] | 빌드 플래그 프로필 두 개(`Shipped`, `MatchedAVX2`)를 두고 모든 결과에 기록 |
| 디버그 기능 포함 여부 | Jolt Release 대 Distribution 1.3~2.1배 [V]. Chaos `Development`는 `check()`와 STATS가 켜져 있음 | Jolt는 해당 옵션 OFF. Chaos는 `Test` 구성으로 측정 |
| FP 모드 | UBT Program 타깃은 `/fp:precise` [V] | `/fp:fast`로 통일. Chaos 타깃은 `FPSemantics=Imprecise` |
| 스레드 풀 의미 차이와 wake 지연 | PhysX worker 2개 47~114 µs 대 inline 4~7 µs [V] | 헤드라인은 단일 스레드. 별도로 P-core에 고정한 T=1..6 스케일링 곡선 |
| 반복 횟수 숫자가 같아도 일의 양은 다름 | 엔진마다 iteration의 의미가 다름 [V] | 같은 반복 횟수가 아니라 "같은 품질 목표"에서 비교(iso-quality sweep) |
| sleep | 일찍 잠드는 엔진이 빨라 보임 | 성능 씬은 sleep을 끄거나, 잠드는 시간을 별도 지표로 보고 |
| 계측 자체의 비용 | contact listener, report flag | bench 모드와 viz 모드를 분리. 스냅샷은 타이밍 구간 밖에서 수집 |
| 머신 노이즈 | 최대 2.5배 [V] | 실행마다 새 프로세스, 엔진 순서 교차, 5회 이상 반복해 중앙값/p95, CV 게이트, 고성능 전원 계획(사용자가 직접 전환) |

---

## 7. 결과(거동) 비교 — GO (조건부)

- 비교 가능하고 가치가 큽니다. 다만 위 2.3의 기본값 차이가 결과를 지배합니다.
- **해석해 게이트:** 해석해는 연속 시간 해가 아니라 **이산 semi-implicit Euler 해**를 기준으로 합니다. 1초 자유낙하의 경우 dt = 1/60에서 4.98675 m로, 연속 해 4.905 m와 1.7% 차이가 납니다 [V].
- **결정적인 씬** (낙하, 바운스, 경사면 마찰, 진자): 바디별 궤적을 직접 비교합니다.
- **카오스적인 씬** (더미, 탑 붕괴): 바디별 궤적 비교는 의미가 없습니다. 대신 COM, 퍼짐 정도, 최종 높이 분포 같은 **통계 관측량**으로 비교합니다. 이때 같은 엔진에 1e-6 m 섭동을 줬을 때의 민감도를 함께 보고합니다.
- **해시:** 엔진 간에는 절대 비교하지 않습니다. 같은 바이너리의 회귀 검사에만 씁니다.
- **접촉 데이터:** 법선 방향과 phi 부호를 Chaos 규약으로 정규화합니다. 엔진마다 단위 테스트를 둡니다(박스 위 박스에서 법선이 +Z이고 phi ≈ 0인지).

---

## 8. 라이선스

| 대상 | 라이선스 | 이 저장소에서의 정책 |
|---|---|---|
| PhysX 5.11 SDK (`physx/`) | Apache-2.0 [V]. 저장소 루트 LICENSE는 BSD-3이지만 SDK 디렉터리는 5.11.0부터 Apache-2.0 | 소스를 넣지 않고 빌드 시 clone. 재배포할 경우 NOTICE 의무 확인 |
| Bullet 3.25 | zlib | 빌드 시 clone |
| Jolt 5.6.0 | MIT | 빌드 시 clone |
| Unreal Engine | UE EULA. 조사 중 웹 페이지가 403을 반환해 원문 미확인 | 엔진 소스, UE 소스를 옮긴 코드, `*.generated.h`, Chaos가 들어간 DLL/PDB는 **절대 커밋하지 않음**. UE 헤더를 include하는 우리 글루 코드와 `*.Target.cs`/`*.Build.cs`만 커밋. 공개 플러그인에서 흔한 관행이며, 예로 UnrealRoboticsLab이 Apache-2.0 공개 저장소에서 Chaos 헤더를 include함 [V] |

- **확인 필요:** 이 GitHub 저장소를 공개로 둘지, Chaos 비교 결과를 공개할지. PEEL은 Havok 결과를 라이선스 때문에 공개하지 못했습니다 [D]. 결과를 공개하기 전에 EULA를 직접 확인하기를 권장합니다.

---

## 9. 남은 리스크

1. **Chaos 경로가 실증되지 않았습니다.** 스파이크로 해결합니다([작업 계획](03_WorkPlan.md)의 M2.5).
2. **통합 하네스가 아직 없습니다.** hello 테스트는 모두 Y-up, SI, 엔진별 관례로 돌렸습니다. Z-up, cm 기준 씬을 세 백엔드에 통과시키는 일은 M3(Jolt)와 M5(Bullet, PhysX)에서 합니다.
3. **도형 동등성:** convex hull 생성 방식, 삼각형 메시의 internal edge 처리, 박스 마진 정책(날카로운 박스로 할지 둥근 박스로 할지)이 엔진마다 다릅니다. 1단계는 기본 도형과 convex로 한정합니다.
4. **CCD와 speculative contact:** Chaos와 Jolt는 speculative contact가 기본이고, PhysX와 Bullet의 CCD는 opt-in입니다. 빠른 물체가 나오는 씬은 별도로 다룹니다.
5. **Joint 동등성:** UE의 soft limit 스케일(Chaos가 ×100000), PhysX 5에서 projection이 제거된 점 등이 걸립니다. 1단계는 ball/hinge 진자 정도로 제한합니다.
6. **할당자 차이:** Chaos DLL 안의 FMalloc, CRT malloc, Jolt/PhysX의 allocator callback, Bullet의 `btAlignedAlloc`. 메모리는 별도 지표로 다룹니다.
7. **UE4 PhysX 근거가 약합니다.** 4.18 미러만 확인했습니다. 사용자가 UE 4.27 소스로 확인하기를 권장합니다.

---

## 10. 사용자 결정이 필요한 사항

| # | 질문 | 권장 기본값 |
|---|---|---|
| Q1 | GitHub 저장소를 공개로 둘지 비공개로 둘지. Chaos 비교 결과를 공개할지 | 비공개로 시작. 공개는 EULA 확인 후 |
| Q2 | 씬 단위와 축 | UE 기준(cm, kg, s, Z-up, double). Jolt/Bullet/PhysX-SdkDefault만 m로 변환 |
| Q3 | 헤드라인 숫자에 쓸 빌드 플래그 프로필 | `Shipped`(각 SDK가 의도한 플래그, 단 Jolt는 AVX2 명시)를 헤드라인으로, `MatchedAVX2`는 민감도 확인용 |
| Q4 | 질량 규칙 | 두 프로필 모두 UE 규칙(mass^0.75)을 기본으로 하고, 비교용 씬은 질량을 고정합니다. 물리적 밀도는 별도 축으로 고를 수 있습니다 (설계 단계에서 갱신, [04_DecisionLog.md](04_DecisionLog.md) Q4·DL17) |
| Q5 | PhysX 재현 수준 | 5.11 + UE4Compat로 충분. 3.4 백엔드는 필요해지면 나중에 |
| Q6 | Chaos 헤드라인 층 | `FChaosScene` 경로의 frame 시간을 헤드라인으로 하고 solver 시간을 함께 보고 |
| Q7 | 디버거 | VS Code + C/C++ 확장(cppvsdbg) 설치. 이미 설치된 CLion이나 VS 2022 Community도 선택지 |
| Q8 | Jolt 충격량을 위해 로컬 패치를 허용할지 | 허용하지 않음. Jolt는 기하 정보만 표시하고 충격량은 "근사"로 표기 |
| Q9 | 벤치마크할 때 전원 계획 | 측정 세션마다 사용자가 "고성능"으로 직접 전환 |
| Q10 | UE 4.27 소스 확인이 가능한지 | 가능하면 UE4 PhysX 레시피를 대조 |

---

## 부록 A. 검증 로그 요약

| 실험 | 결과 |
|---|---|
| PhysX 5.11 CPU 정적 빌드 (Ninja, release/debug/checked/profile) | 62 / 49 / 63 / 57초, 경고 0 |
| PhysX hello (dispatcher worker 2개) | y = 5.013251 (semi-implicit Euler 5.013249), 103스텝에 sleep, 중앙값 47.5 µs/step |
| Jolt hello (worker 0개) | y = 5.098089 (damping 반영 기준값 5.098087), 180스텝에 sleep, 중앙값 5.0 µs/step |
| Jolt cm 입력 실험 | 1초 후 624.4 cm (정상 509.8 cm), 질량 1e9 → cm 입력 금지 |
| Bullet hello (단일 스레드) | y = 5.013251, 정지 높이 0.500000, 206스텝에 sleep, 중앙값 0.7 µs/step |
| Bullet 상자 3000개, 240스텝 | 단일 스레드 world는 3회 모두 같은 결과. MT world는 결과가 다르고 실행마다 달라짐 |
| PhysX UE4Compat 10단 스택 | PGS 8/1: 25 cm까지 흔들림, sleep 안 함. stabilization: 약 1초에 안정. TGS: 약 2초에 안정 |
| D3D11 뷰어 프로토타입 | 3만 도형 0.83 ms (1 px) / 2.17 ms (두꺼운 선), 오프스크린 PNG 동작 |
| 통합 Ninja 빌드 (imgui + implot + 세 SDK) | 95.4초 wall |
