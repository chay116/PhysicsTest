# 부록 A. 헤더 스케치 (Header Sketches)

> **작성일:** 2026-10-21
> **상태:** 설계 초안 v1.0 (구현 전)
> **관련 문서:** [01_Feasibility.md](../01_Feasibility.md) · [02_Architecture.md](../02_Architecture.md) · [03_WorkPlan.md](../03_WorkPlan.md) · [04_DecisionLog.md](../04_DecisionLog.md)
> **근거 표기:** **[V]** 로컬 소스(`D:\UnrealEngine` 5.8.3, `D:\PhysicsRefs`)를 읽었거나 직접 컴파일·실행해 확인 · **[M]** UE 4.18 서드파티 미러에서 온 내용(미확인, 복사 금지, 사용자가 4.27로 확인할 대상) · **[R]** 기억에 의존해 미검증

---

## A.0 이 부록에 대해

### A.0.1 성격

- 이 부록은 [02_Architecture.md](../02_Architecture.md)가 정의한 타입과 경계를 **선언 수준**으로 적은 설계 초안이다. 함수 본문은 거의 없고, 아직 저장소에 컴파일되는 코드로 들어가지 않았다.
- 예외적으로 A.1~A.4(ABI 헤더, BackendKit, `PtAbiUeCompatCheck`, BackendCommon)는 2026-10-21에 MSVC 14.44.35207(cl 19.44.35229)로 실제 컴파일해 확인했다 **[V]**.
  - `PtBackendAbi.h`: C11 `/std:c11 /W4 /WX`로 컴파일되고, 구조체 크기 `static_assert` 12개가 모두 통과한다.
  - `PtBackendKit.h` + `PtAbiUeCompatCheck.cpp`: C++20 `/W4 /WX /GR- /EHs-c- /D_HAS_EXCEPTIONS=0 /permissive- /Zc:preprocessor`로 경고 없이 컴파일된다. 기본 가상 함수의 인자를 이름 없이 둔 덕분에 C4100이 나지 않는다.
  - BackendCommon: C++20 `/W4 /WX /GR- /EHs-c-`로 컴파일된다.
  - 하네스 측 서드파티 include 파일 쌍: `check()`/`ensure()` 매크로를 먼저 정의한 TU에서 멤버 함수 `check()`를 가진 헤더를 Start/End 파일 사이에 넣어 `/W4 /WX`로 컴파일되는 것을 확인했다. 확인할 때는 같은 내용을 `.inl` 이름으로 두었고, 이 판에서 파일 이름만 [02_Architecture.md](../02_Architecture.md) §8.1의 `Misc/PtThirdPartyIncludesStart.h` / `Misc/PtThirdPartyIncludesEnd.h`로 맞췄다.
  - 검토 판에서 고친 뒤 이 부록의 코드 블록을 다시 뽑아 같은 플래그로 다시 컴파일했다 (C11, C++20 ABI, compat, BackendCommon, 하네스 측 쌍 모두 통과).
  - 확인용 파일은 세션 스크래치(`scratchpad\hdrcheck`, `scratchpad\hdrcheck2`)에만 있고 저장소에는 넣지 않았다.
- 나머지 절(A.5~A.16)은 축약 스케치다. `/* ... */`로 줄인 부분이 있고, 구현할 때 정의와 private 멤버가 추가된다.
- 이 문서는 구현의 출발점이지 그대로 복사할 코드가 아니다. 이름, 시그니처, 의미 규약(단위, 순서, 호출 시점)을 고정하는 것이 목적이다.

### A.0.2 표기 규칙

- **단위:** cm, kg, s, rad, Z-up, double (D1). ABI 경계에서 단위가 바뀌지 않는다.
- **네임스페이스:**
  - 하네스 C++ = `PhysicsTest`
  - 백엔드 측 Kit과 BackendCommon = `PhysicsTest::Abi` 아래의 `inline namespace PT_BACKEND_NS`
  - C ABI = `Pt*` 접두어
- **코드 주석은 영어로 쓴다** (저장소 코드 관례). 설명 산문만 한국어다.
- **단계 태그:** 파일 머리 주석의 `[MVP Mx]`는 M0~M6 안에서 구현한다는 뜻이다. `[확장 Mx]`는 M7 이후의 뒤 마일스톤이다 (M7, M8도 확장이다). 마일스톤 ID는 [03_WorkPlan.md](../03_WorkPlan.md)를 따른다.
- **`PROVISIONAL(M9)`:** ABI 핵심(CORE)은 M6에서 v1.0으로 동결한다. 조인트, 디버그 스냅샷, 네이티브 드로 항목은 M9까지 바뀔 수 있다. [02_Architecture.md](../02_Architecture.md) §7.6대로 그 사이 변경은 minor 버전과 StructSize로 기록하고, 이 부록은 여기에 `PT_ABI_PROVISIONAL_REVISION`을 더한다. 바뀔 때마다 리비전을 올리고, 호스트는 리비전이 다른 모듈에서 이 항목의 capability를 없는 것으로 취급한다.
- 씬 ID는 항상 `ID.Name`(예: `M3.BoxPile`)으로 쓴다. 마일스톤 M0~M15와는 관계없다.

### A.0.3 파일 대응표

경로는 [02_Architecture.md](../02_Architecture.md)의 디렉터리 구조를 기준으로 한다. "구 번호"는 설계 과정의 스케치 번호 [1]~[13]이다.

| 절 | 저장소 경로 | 내용 | 구현 마일스톤 | 구 번호 |
|---|---|---|---|---|
| A.1 | `Source/Runtime/PhysicsTestAbi/Public/PhysicsTestAbi/PtBackendAbi.h` | 순수 C ABI. Chaos DLL과 공유하는 유일한 헤더 | M2 (v0.9) → M6 핵심 v1.0 동결 → M9 PROVISIONAL 확정 | [1] |
| A.2 | `Source/Runtime/PhysicsTestAbi/Public/PhysicsTestAbi/PtBackendKit.h` | 백엔드 측 header-only Kit | M2 | [2] |
| A.3 | `Source/Runtime/PhysicsTestAbi/Private/PtAbiUeCompatCheck.cpp` | UE와 같은 플래그로 Kit을 컴파일하는 OBJECT 타깃 | M2 | 신규 |
| A.4 | `Source/Runtime/BackendCommon/Public/*.h` (+ 하네스 측 `Core/Public/Misc/PtThirdPartyIncludesStart.h` / `End.h`) | 단위 변환, combine, 접촉 규약, COM 래핑 규칙, 빌드 자기보고, 서드파티 include 래퍼 | M0 (빌드 자기보고, 서드파티 래퍼), M2 (골격: 고정 폭 타입, 단위, combine, damping, 접촉), M3 (Jolt가 쓰는 부분), M5 (Bullet·PhysX, BackendCommon 테스트) | [3] |
| A.5 | `Source/Runtime/Core/Public/...` | 수학 타입, 소형 UE 관용구 | M0 (최소 구현, S5). `FConfigFile`·`IConsoleManager`는 M1, `FCycleCounter`는 확장 M10 | [4] |
| A.6 | `Source/Runtime/PhysicsCore/Public/PhysicsEngine/*.h`, `PhysicalMaterials/PhysicalMaterial.h`, `Physics/PhysicsInterfaceTypes.h` | 저작 데이터 모델 | M1 (조인트는 데이터 타입만). 조인트 사용은 확장 M9, convex hull·질량은 확장 M15 백로그 | [5] |
| A.7 | `Source/Runtime/PhysicsCore/Public/Physics/StepPlanner.h`, `PhysSceneStats.h` | 스텝 계획, 프레임 타이밍 | M1 (모든 모드). MVP 씬은 FixedStep·Sync만 사용 | [6] |
| A.8 | `Source/Runtime/PhysicsCore/Public/Physics/PhysicsBackend.h`, `PhysicsInterface.h` | 호스트 측 모듈·레지스트리·씬 프록시, `_AssumesLocked` 파사드 | M2 | [7] |
| A.9 | `Source/Runtime/PhysicsCore/Public/PhysScene.h` | 유일한 씬 `FPhysScene` | M1 골격, M2 ABI 연결, M3 해시 | [8] |
| A.10 | `Source/Runtime/PhysicsCore/Public/Physics/PhysicsProfile.h` | 프로필·변형·리졸버, effective 설정 | M1 최소, 완성은 확장 M9 | [9] |
| A.11 | `Physics/PhysicsDebugSnapshot.h`, `Source/Runtime/PhysicsDebugDraw/Public/*.h`, `Source/Runtime/D3D11DebugRenderer/Public/*.h` | 스냅샷, Chaos 방식 디버그 드로, 렌더러 | M4 (뷰어 1단계), 확장 M12 (뷰어 2단계) | [10] |
| A.12 | `Source/Runtime/PhysicsTestScenarios/`, `PhysicsTestBenchCore/`, `PhysicsTestConformance/`, `PhysicsTestBenchmark/` (각 `Public/`) | 시나리오, BenchCore, 게이트, 벤치 러너 | M3 최소 (BenchCore 클럭 버킷 포함), M6 게이트, 확장 M7 러너·호스트 제어, 확장 M10 | [11] |
| A.13 | `Source/Backends/NullBackend/Private/NullBackend.h` | SDK 없는 기준 백엔드, Chaos 템플릿 | M2. NATIVE_FRAME_MODEL 게이트는 M6 | 신규 |
| A.14 | `Source/Backends/PhysXBackend/Private/*.h` | UE4 방식 PhysX 5.11 (클린룸) | M5 | [12] |
| A.15 | `Source/Backends/JoltBackend/Private/PhysScene_Jolt.h`, `Source/Backends/BulletBackend/Private/PhysScene_Bullet.h` | Jolt, Bullet 백엔드 요점 | Jolt M3, Bullet M5 | [12] 말미 |
| A.16 | `Unreal/ChaosBridgeHost/Source/...` | Chaos 브리지 (UBT 전용) | 골격 M2.5 (H, 선택), 본체 확장 M13 (U), OOP 대안 확장 M14 | [13] |

[02_Architecture.md](../02_Architecture.md)의 디렉터리 트리 대비 이 부록에서 새로 이름을 붙인 파일은 다음과 같다. 나머지 파일 이름(`PtShapeWrapping.h`, `PtThirdPartyIncludes.h`, `Misc/PtThirdPartyIncludesStart.h` / `End.h`, `PhysicsTestBenchCore/`, `Physics/PhysicsInterfaceTypes.h` 등)은 02의 트리를 그대로 따른다.
- BackendCommon: `PtTypes.h` (02 §7.3이 말하는 고정 폭 typedef의 파일)
- PhysicsTestAbi: `Private/PtAbiUeCompatCheck.cpp` (02 §4.5의 `PtAbiUeCompatCheck` OBJECT 타깃의 소스)

**M1 구현과의 차이 (요약, 세부는 [Progress.md](../Progress.md) M1 §3).** A.5~A.10은 M1에서 컴파일되는 코드가 되었고, 스케치와 다른 곳은 다음과 같다.
- 새 파일: Core `Containers/StringConv.h`(UTF-16 ↔ UTF-8, `wmain` 인자), PhysicsCore `Physics/AnalyticReference.h`(`dt_engine`, 자유낙하 기준값), `Physics/PhysicsConsoleVariables.h`(하네스 물리 CVar 표: `p.ComNudgeAffectsInertia`, `p.SensitiveSleepThresholdMultiplier`. 리졸버가 이 표만 하네스 CVar로 본다), `Private/Physics/Matrix33.h`(Jacobi 대각화), `PhysicsEngine/ConstraintUtils.h`(A.6에서는 `ConstraintInstance.h` 안에 있던 프리셋).
- A.5의 `FConfigCacheIni`는 만들지 않았다 (쓰는 곳이 없다. 하네스는 ini 파일을 명시적으로 합성한다). `FConfigFile`의 섹션 이름은 UE처럼 대소문자를 무시한다.
- A.6: `PhysicsInterfaceTypes.h`의 채널 수는 C++ 상수 `NumCollisionChannels`다. C ABI의 매크로 `PT_NUM_COLLISION_CHANNELS`(A.1)와 이름이 같으면 ABI 헤더를 먼저 include한 TU에서 선언이 깨지기 때문이다. `FBodyCollisionFilter::IsValid()`를 더했다. `FBodySetup::GetVolume`은 UE 5.1+ 이름 `GetScaledVolume`이 되었고, `BuildScale3D`는 UE처럼 메타데이터라 곱하지 않는다.
- A.10의 `FConfigValue`는 Core ini의 UE 이름 `FConfigValue`와 겹쳐 `FResolvedConfigValue`로 바꿨다. `FProfileResolver::Resolve`는 입력을 `FProfileResolveInputs`(Base, 프로필, 변형, 씬 고정값, 커맨드라인 전체)로 받는다. `FResolvedPhysicsConfig::Sha256()`은 `PhysicsSha256()`이 되었다 (`FPhysicsProfile::Sha256()`·`FBackendVariant::Sha256()`과 구분). `ApplyConsoleVariables(Config, OutEngineVariables, OutError)`를 더했다. `ApplyForcedOverrides`는 M9에 둔다. effective 설정 API(`FEffectiveSetting`, `ParseEffectiveSettings`, `GetRequiredEffectiveKeys`)는 백엔드의 `DumpEffectiveSettings`가 생기는 M2(Null, A7)와 M3(`effective_settings.jsonl`)로 미뤘다.
- A.7의 `FStepPlan`에 `NumExternalSteps`(M2에서 `NumPushedSteps`로 이름을 바꿨다), `bAsyncInterpolated`, `Params`(UE `SetUpForFrame` 인자), `FrameDeltaTime`(UE5 `MDeltaTime`, PreTick이 받는 dt)을 더했고, `FStepPlanner`는 `MakeSetUpForFrameParams` / `GetAsyncDeltaTime` / `PlanFrame`으로 나눴다 (A.7의 편의 오버로드도 있다).
- A.9의 `Tick`은 UE5 순서다: `SetUpForFrame` → `StartFrame`(안에서 `OnPhysScenePreTick(FrameDeltaTime)`) → `WaitPhysScenes` → `EndFrame`. 생성·flush·준비는 phase Idle에서만 된다.
- A.6의 `FBodyInstance` 명령 함수(속도·힘·임펄스·깨우기)와 A.9의 `FPhysScene` 명령 버퍼·해시·디버그 스냅샷은 ABI가 생기는 M2~M4에 더한다. `FKConvexElem::BuildHull`은 M15다.
- `FPhysicsTestSettings`에 `bAllowSleeping`, `RestitutionThresholdVelocity`를 더했다 (02 §10 M1 구현 메모).

**M2 구현과의 차이 (요약, 세부는 [Progress.md](../Progress.md) M2 §3, 규칙은 [Abi.md](../Abi.md)).** A.1~A.4, A.8, A.9, A.13이 컴파일되는 코드가 되었다.
- A.1: 구조체 크기와 함수 표는 그대로다 (`static_assert` 12개가 C11과 C++20에서 통과). 로그 수준 `PT_LOG_*`, `PT_CAP_PROVISIONAL_MASK`를 더했고, 결과 코드·StructSize·`PT_READ_ACTIVE_ONLY`·명령 의미를 주석으로 적었다. 구조체 멤버 들여쓰기는 저장소 규칙대로 탭이다.
- A.2: `FStringRef::FromCString`, `FTextWriter::Appendf`/`AppendJsonString`/`GetNeeded`, `TStridedView::Make`/`IsValid`, `FFrameParams::bNewGravity`, `EModuleState`, `FValidationError`와 `Validate*` 함수들(오류 출력이 `FTextWriter*`가 아니라 `FValidationError&`), `IPhysicsBackendScene::ValidateBodyCommand`·`GetNumMaterials`, `IPhysicsBackend::GetMemoryStats`, 백엔드 클래스의 `static constexpr const char* ModuleName` 요구, `GetKitCurrentThreadId()`를 더했다. 썽크에 MXCSR 가드와 "메시지 없는 실패" 보정을 넣었다. 백엔드 객체는 정적 저장소에 만들고 파괴하지 않는다.
- A.3: 스케치 그대로이고, `/EHs-c-`와 RTTI 없음을 `#error`로 확인한다. ABI 헤더의 C11 컴파일은 별도 TU `Private/PtAbiC11Check.c`다.
- A.4: `PtUnitScale`에 `PtVec3` overload, `PtDamping`에 `UEDampingMultiplier`, `PtContactConvention`에 mobility 상수를 더했다. 모두 header-only inline이다. `PtShapeWrapping.h`는 M3이다.
- A.7: `FStepPlan::NumExternalSteps` → `NumPushedSteps` (UE `FPhysicsSolverBase::ExternalSteps`와 뜻이 달라서), `InternalStepDeltaTime`(double, 엔진이 실제로 스텝하는 dt)을 더했다.
- A.8: InitGamePhys 인자를 `FBackendInitParams`로 묶었다. `FindOrLoadModule`(초기화 없이 로드), 생성된 목록 `GetBackendRegistryEntries()`, `FMarshalledActorBatch` + `MarshalBodies`/`CreateActors(Bodies, Batch)`, `PullBodyStates`, `GetEntryPoint`, `ValidateApiTable`을 더했다. `FPhysicsBackendScene`은 `Create(...)` 팩토리로 만든다. `CaptureDebugSnapshot`은 M4다. `FPhysicsInterface`에 `SetBodyState_AssumesLocked`와 읽기 3개(`GetLinearVelocity_AssumesLocked`, `GetAngularVelocity_AssumesLocked`, `IsSleeping`)를 더했다.
- A.9: `FPhysScene`에 `ReleaseBody`, `GetFrameDriver`, `GetBackendScene`, `GetBackendInfo`, `ComputeNativeStateHash`, `DumpEffectiveSettings`를 더했다. `ComputeDescSha256`/`ComputeOptionsSha256`은 M3, 스냅샷과 드로 큐는 M4다.
- A.10: effective 설정 API는 `Physics/EffectiveSettings.h`(`ParseEffectiveSettings(Text, Out, Error)`, `GetGenericEffectiveKeys`, `GetRequiredEffectiveKeys`, `FindMissingEffectiveKeys`)에 있다.
- A.12: `IConformanceGate`는 G 게이트와 함께 M6에 둔다. M2 ABI 게이트는 `AbiGates::Run*` 함수이고, `FGateReport`는 `Check`/`Note`/`Summary`를 갖는다.
- A.13: `FNullFramePlanner::Plan(Params, AsyncDt, Acc)`는 `FNullStepPlan{NumSteps, NumPushedSteps, StepDeltaTime(double)}`을 돌려준다. 정적 진입점은 `NullBackend.cpp`, DLL 진입점은 `NullBackendDll.cpp`(스케치대로).

### A.0.4 UE 대응 차이 (헤더 수준)

UE 전문가가 읽을 때 걸릴 만한 차이를 모았다. 모두 의도한 차이다. D1에 따른 `float` → `double` 외에는 수치에 영향을 주는 항목이 없다 (R1). 전체 대응표는 [02_Architecture.md](../02_Architecture.md) §5.2이고, 이 표는 헤더에서 보이는 것만 다룬다.

| 항목 | UE 5.8.3 | 하네스 | 이유 |
|---|---|---|---|
| `FK*Elem` 치수·`RestOffset` | `float` (BoxElem.h:36, SphylElem.h:36-40 [V]) | `double` | D1: API는 double |
| `FKShapeElem::Name` | `FName` | `std::string` | UObject·이름 테이블 없음 |
| `FKBoxElem` / `FKSphylElem::Rotation` | `FRotator` (BoxElem.h:32, SphylElem.h:32 [V]) | `FRotator` 그대로. 호스트가 마샬링할 때 `GetTransform()`으로 바디당 한 번 FQuat로 바꾼다. `FK*Elem`에 FQuat 필드는 두지 않는다 | UE와 같다. 02 §5.2 1행의 "마샬링 시 FQuat 캐시"가 이 1회 변환이며, 비타이밍 `CreateActors`에서 일어난다 |
| `FKConvexElem` | VertexData, IndexData, ElemBox, Transform | VertexData, ElemBox, Transform, HullTriangles. `BuildHull`은 확장 M15 백로그 | Phase 1에서 convex는 데이터 모델과 ABI에만 둔다 |
| `FBox` 유효성 | `uint8 IsValid` | `uint8 IsValid` | 같다 (이전 스케치의 `bIsValid`를 고침) |
| 모빌리티 | 소유 컴포넌트의 Mobility | `FBodyInstance::bStatic`. kinematic = `!bStatic && !bSimulatePhysics` | Actor/Component 없음 |
| `FBodyInstance::InitBody` | `(UBodySetup*, FTransform, UPrimitiveComponent*, FPhysScene*[, SpawnParams])` | `(const FBodySetup*, FTransform, FPhysScene*)` | 컴포넌트 없음 |
| `AddForce` / `AddTorqueInRadians` / `AddImpulse` / `AddAngularImpulseInRadians` | 끝에 `FAsyncPhysicsTimestamp`, `APlayerController*` (BodyInstance.h:1001-1019 [V]) | 두 인자 없음 | 네트워크 물리 없음 |
| `FPhysicsInterface::*_AssumesLocked(..., bIsInternal)` | GT/PT 구분 | 인자는 남기고 `false`만 허용 (`checkf`) | 물리 스레드 측이 없음 |
| 임펄스와 속도 변화 | `AddImpulse_AssumesLocked(Handle, Impulse, bIsInternal)`와 `AddVelocity_AssumesLocked`가 별개 (ChaosEngineInterface.h:465-468 [V]) | 같다. ABI도 `PT_CMD_ADD_IMPULSE`와 `PT_CMD_ADD_VELOCITY`로 나눈다 | 이전 스케치의 `bVelChange` 인자를 없앴다 |
| `FRigidBodyState` | AngVel이 deg/s | `FBodyInstanceState`, rad/s | 단위 혼동을 막으려고 이름을 바꿈 |
| 충돌 채널 | `ECC_MAX` = 65, uint64 마스크 (EngineTypes.h:1101-1176, CollisionFilterData.cpp:263-272 [V]) | `uint8` 0..31, uint32 마스크 | Phase 1의 의도적 제한. UE4 PhysX word3의 5비트 채널, Bullet int 마스크에 맞춤. Chaos 브리지가 uint64로 넓힌다 |
| enum 형태 | `ECollisionEnabled::Type` 네임스페이스 enum, `ELinearConstraintMotion` 등 unscoped, `TEnumAsByte` | `enum class : uint8`, 값 이름은 같다 | UHT 없음 |
| `FConstraintProfileProperties` | `LinearLimit` / `ConeLimit` / `TwistLimit` + drive, soft limit 등 (ConstraintInstance.h:56-115 [V]) | 같은 중첩 구조의 Phase-1 부분집합 (motion, limit 각도, `bDisableCollision`) | ball/hinge만 |
| 조인트 프리셋 | `ConstraintUtils::ConfigureAsBallAndSocket` / `ConfigureAsHinge` (ConstraintUtils.h:10,19 [V]) | 같은 이름과 시그니처. 두 개만 둔다 | 이전 스케치의 `SetupAsBallJoint`를 고침 |
| `FDebugDrawQueue` | `Chaos::FDebugDrawQueue`, 프로세스 싱글턴 `GetInstance()`, `DrawDebugString(..., AActor* TestBaseActor, ...)`, `FString` (DebugDrawQueue.h:103-250 [V]) | 시그니처는 Chaos 쪽을 따른다. `FPhysScene`마다 하나, `AActor*` 없음, `std::string_view`. `DrawDebugTriangle`·`Enqueue`는 하네스 확장 | Engine `DrawDebugHelpers`가 아니라 Chaos 큐를 미러링한다. 뷰어 2단계(확장 M12)가 엔진별 씬을 동시에 가진다 |
| `FLatentDrawCommand::EDrawType` | 8종 (DebugDrawCommand.h:43-53 [V]) | 8종 + `Triangle` | `IChaosDDRenderer::RenderTriangle` 통과용 |
| `IChaosDDRenderer` | `FVector3d`, `FQuat4d`, `FSphere3d`, `FString`, `RenderLatentCommand` | `FVector`, `FQuat`, `FSphere`(모두 double), `std::string_view`. `RenderLatentCommand`는 UE 측에만 | UE 타입 없음 |
| 드로 함수 이름 | `DebugDraw::DrawConstraintGraph`, `FPBDIslandManager::DebugDrawSleepState`, `FPBDRigidsSolver::DebugDrawShapes` | `DebugDraw::DrawConstraintGraph`(같음), `DrawSleepState`, `DrawParticleShapes` | 후자 둘은 자유 함수로 모음 |
| 정지마찰 | 머티리얼은 원값을 갖고, 솔버가 쌍마다 결합 | 같다. 머티리얼 단위 해석 함수(이전 `GetEffectiveStaticFriction`)를 없앴다. Jolt·Bullet은 쌍당 마찰 계수가 하나라 동적 결합값만 쓴다 (`material.static_friction_rule=dynamic_only`) | 쌍 규칙은 A.1, A.4, 02 §5.1 참고 |
| `UPhysicsSettingsCore` / `UPhysicsSettings` | UObject CDO, 모든 키가 `[/Script/Engine.PhysicsSettings]` (BaseEngine.ini:3406 [V]) | 일반 구조체 두 개, ini 섹션은 같다 | UObject 없음 |
| `FCommandLine`, `FParse`, CVar, ini, `UE_LOG`, `check`/`ensure` | 엔진 구현 | 같은 이름의 소형 구현 | S5 |

### A.0.5 이 판에서 고친 것

설계 과정의 스케치에서 다음을 고쳤다. FIX ID별 대응은 A.17에 있다.
- Chaos 스레드 매핑, 바디 생성 순서, AsyncDt 규약, 충돌 채널 근거, 정지마찰 결합 규칙을 로컬 소스로 다시 확인해 주석에 반영했다.
- ABI 버전을 v0.9(major 0)로 되돌리고, 동결 시점을 M6으로 맞췄다. 조인트·스냅샷·네이티브 드로는 PROVISIONAL(M9)로 표시했다.
- Kit과 BackendCommon을 `inline namespace PT_BACKEND_NS`로 감싸 백엔드 간 ODR 충돌을 없앴다. 상태는 `TBackendApiProvider<T>`로 옮겼다.
- UE 이름을 5.8.3 원본에 맞췄다 (A.0.4).
- 파일 이름, 서드파티 include 래퍼, 필수 effective 키 목록, Jolt·Bullet의 정지마찰 처리, sleep off일 때의 putToSleep 생략, 단계 태그를 [02_Architecture.md](../02_Architecture.md)와 [04_DecisionLog.md](../04_DecisionLog.md)에 맞췄다.

---

## A.1 `PtBackendAbi.h` (순수 C11) — [MVP M2 → M6 동결]

- Chaos DLL과 공유하는 유일한 헤더다. C++ 기능을 쓰지 않으며 C11 컴파일러로도 확인했다.
- 구조체 크기는 MSVC 14.44에서 C와 C++ 양쪽으로 확인했다 **[V]**. 이 판에서 크기가 바뀐 구조체는 없다. `PtBackendInfo`는 필드가 늘었지만 `StructSize`로 버전을 맞추며 `static_assert` 대상이 아니다.
- 주요 변경: `PT_ABI_VERSION_MAJOR 0`, `PT_ASYNC_DT_SYNC`, 충돌 채널 근거, `PtPhysicalMaterial` 쌍 규칙, `CreateActors` 순서, 스레드 매핑 주석, `PT_CMD_ADD_VELOCITY` 분리, `PtBackendInfo.RequiredEffectiveKeys`.

```c
/* PtBackendAbi.h - pure C11. The ONLY header shared with the Chaos DLL.
   Units: cm, kg, s, rad, Z-up, double.
   CORE entries freeze as v1.0 at M6. Entries marked PROVISIONAL(M9) (joints, debug snapshot, native draw)
   may still change until M9; every such change bumps PT_ABI_PROVISIONAL_REVISION. */
#ifndef PT_BACKEND_ABI_H
#define PT_BACKEND_ABI_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
#  define PT_STATIC_ASSERT(c, m) static_assert(c, m)
extern "C" {
#else
#  define PT_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif

/* v0.9 (major 0) until the M6 freeze; the abi-v1.0 tag sets MAJOR 1 / MINOR 0. v1.x is append-only.
   The host always calls PT_GetBackendApi(PT_ABI_VERSION_MAJOR) with the major it was built with,
   so the M2.5 spike DLL (built against major 0) and the host agree. */
#define PT_ABI_VERSION_MAJOR 0u
#define PT_ABI_VERSION_MINOR 9u
#define PT_ABI_VERSION ((PT_ABI_VERSION_MAJOR << 16) | PT_ABI_VERSION_MINOR)
#define PT_ABI_PROVISIONAL_REVISION 1u                 /* PROVISIONAL(M9) layout revision; frozen at M9 */
#define PT_GET_BACKEND_API_SYMBOL "PT_GetBackendApi"   /* the only export of a DLL backend */
#define PT_CALL __cdecl
#define PT_INDEX_NONE 0xFFFFFFFFu
/* 32 channels with uint32 masks is a deliberate Phase-1 limit. UE 5.8.3 has ECC_MAX = 65 with uint64 masks
   (EngineTypes.h:1101-1176, CollisionFilterData.cpp:263-272,310-313 [V]). 32 is what UE4's legacy PhysX word3
   (5-bit channel field) and Bullet's int group/mask can hold. The Chaos bridge widens BlockMask to uint64. */
#define PT_NUM_COLLISION_CHANNELS 32u
/* PtSceneDesc.AsyncFixedTimeStepSize for sync stepping. Never 0: Chaos treats AsyncDt >= 0 as async
   (IsUsingAsyncResults, PhysicsSolverBase.h:712-715 [V]). */
#define PT_ASYNC_DT_SYNC (-1.0)

typedef int32_t PtResult;
#define PT_OK                 0
#define PT_W_TRUNCATED        1
#define PT_E_INVALID_ARG     -1
#define PT_E_INVALID_HANDLE  -2
#define PT_E_BAD_STATE       -3
#define PT_E_WRONG_THREAD    -4
#define PT_E_UNSUPPORTED     -5
#define PT_E_CAPACITY        -6   /* the required count is written back */
#define PT_E_VERSION         -7
#define PT_E_ENGINE          -8
#define PT_E_OUT_OF_MEMORY   -9
#define PT_E_TRANSPORT      -10
#define PT_E_INTERNAL       -11

typedef uint64_t PtSceneId;                                         /* 0 = invalid; (generation << 32) | slot */
typedef struct PtVec3 { double X, Y, Z; } PtVec3;                   /* 24 B */
typedef struct PtQuat { double X, Y, Z, W; } PtQuat;                /* 32 B */
typedef struct PtTransform { PtQuat Rotation; PtVec3 Translation; } PtTransform;  /* 56 B; scale is baked in by the host */
typedef struct PtTextBuffer { char* Data; uint32_t Capacity; uint32_t Length; } PtTextBuffer;  /* Length includes the NUL */
typedef struct PtKeyValue { const char* Key; const char* Value; } PtKeyValue;     /* "physx.Solver"="PGS", "p.Chaos.*"=".." */

/* ---- Motion type ---- */
#define PT_MOTION_STATIC     0u
#define PT_MOTION_KINEMATIC  1u
#define PT_MOTION_DYNAMIC    2u

/* ---- Object state: same values as Chaos::EObjectStateType (ObjectState.h:9-18 [V]) ---- */
#define PT_OBJECT_STATE_UNINITIALIZED 0u
#define PT_OBJECT_STATE_SLEEPING      1u
#define PT_OBJECT_STATE_KINEMATIC     2u
#define PT_OBJECT_STATE_STATIC        3u
#define PT_OBJECT_STATE_DYNAMIC       4u

/* ---- Shape type ---- */
#define PT_SHAPE_SPHERE   0u
#define PT_SHAPE_BOX      1u   /* half extents (the host halves FKBoxElem's full X/Y/Z) */
#define PT_SHAPE_CAPSULE  2u   /* Radius + HalfLength (segment only, = FKSphylElem::Length / 2) along local Z */
#define PT_SHAPE_CONVEX   3u   /* data model / ABI only in Phase 1 (no headline scene) */
#define PT_SHAPE_TRIMESH  4u   /* reserved */

/* ---- Material combine mode: same values as EFrictionCombineMode and PxCombineMode; the larger value wins ---- */
#define PT_COMBINE_AVERAGE   0u
#define PT_COMBINE_MIN       1u
#define PT_COMBINE_MULTIPLY  2u
#define PT_COMBINE_MAX       3u

/* ---- Joint type: PROVISIONAL(M9) ---- */
#define PT_JOINT_BALL   0u
#define PT_JOINT_HINGE  1u   /* twist about Frame X is free; swings are locked (ConstraintUtils::ConfigureAsHinge) */
#define PT_JOINT_FIXED  2u   /* reserved */

/* ---- Run mode ---- */
#define PT_RUN_BENCH     0u
#define PT_RUN_BEHAVIOR  1u
#define PT_RUN_VIZ       2u

/* ---- Instrumentation tiers: any bit set makes the run non-headline ---- */
#define PT_INSTR_COUNTERS           (1u << 0)
#define PT_INSTR_PHASES             (1u << 1)
#define PT_INSTR_ALLOC              (1u << 2)
#define PT_INSTR_CONTACTS           (1u << 3)
#define PT_INSTR_NATIVE_DEBUG_DRAW  (1u << 4)

/* ---- Scene simulation flags ---- */
#define PT_SIM_ALLOW_SLEEPING        (1u << 0)   /* off (Normalized): PhysX eDISABLE_SLEEPING, Chaos p.Chaos.Solver.Sleep.Enabled=0,
                                                    Jolt bAllowSleeping=false, Bullet DISABLE_DEACTIVATION */
#define PT_SIM_ENABLE_CCD            (1u << 1)
#define PT_SIM_ENHANCED_DETERMINISM  (1u << 2)

/* ---- Body flags. Creation-time (CreateActor, BodyInstance.cpp:1301-1340 [V]) unless noted ---- */
#define PT_BODY_ENABLE_GRAVITY         (1u << 0)
#define PT_BODY_START_AWAKE            (1u << 1)   /* applied after AddActorsToScene: wake counter 0 + putToSleep when clear;
                                                      skipped when PT_SIM_ALLOW_SLEEPING is off (PhysX rejects putToSleep in an
                                                      eDISABLE_SLEEPING scene, NpRigidDynamic.cpp:521-525 [V]) */
#define PT_BODY_ALLOW_SLEEP            (1u << 2)
#define PT_BODY_USE_CCD                (1u << 3)
#define PT_BODY_GYROSCOPIC             (1u << 4)
#define PT_BODY_INERTIA_CONDITIONING   (1u << 5)
#define PT_BODY_OVERRIDE_ITERATIONS    (1u << 6)

/* ---- Shape / joint / read flags ---- */
#define PT_SHAPEF_SIMULATION         (1u << 0)
#define PT_SHAPEF_QUERY              (1u << 1)
#define PT_JOINTF_DISABLE_COLLISION  (1u << 0)   /* PROVISIONAL(M9) */
#define PT_READ_ALL          0u
#define PT_READ_ACTIVE_ONLY  1u

/* ---- Capabilities ---- */
#define PT_CAP(n) ((uint64_t)1 << (n))
#define PT_CAP_COLLISION                 PT_CAP(0)
#define PT_CAP_SHAPE_CONVEX              PT_CAP(1)
#define PT_CAP_SHAPE_TRIMESH_STATIC      PT_CAP(2)
#define PT_CAP_KINEMATIC                 PT_CAP(3)
#define PT_CAP_JOINT_BALL                PT_CAP(4)   /* PROVISIONAL(M9) */
#define PT_CAP_JOINT_HINGE               PT_CAP(5)   /* PROVISIONAL(M9) */
#define PT_CAP_SLEEPING                  PT_CAP(6)
#define PT_CAP_CCD                       PT_CAP(7)
#define PT_CAP_GYROSCOPIC                PT_CAP(8)
#define PT_CAP_MULTITHREADED             PT_CAP(9)
#define PT_CAP_NATIVE_FRAME_MODEL        PT_CAP(10)  /* honours MaxSubsteps > 1, bSubstepping and AsyncDt; reports NumStepsTaken.
                                                        Chaos; also Null (FStepPlanner semantics) so the contract is gated at M6 (A10) */
#define PT_CAP_ACTIVE_LIST               PT_CAP(11)
#define PT_CAP_STATE_WRITE               PT_CAP(12)
#define PT_CAP_NATIVE_HASH               PT_CAP(13)
#define PT_CAP_CONTACT_POINTS            PT_CAP(16)  /* PROVISIONAL(M9) (debug snapshot) */
#define PT_CAP_CONTACT_NORMAL_IMPULSE    PT_CAP(17)  /* PROVISIONAL(M9) */
#define PT_CAP_CONTACT_FRICTION_IMPULSE  PT_CAP(18)  /* PROVISIONAL(M9) */
#define PT_CAP_CONTACT_ACTIVE_EXACT      PT_CAP(19)  /* PROVISIONAL(M9) */
#define PT_CAP_NATIVE_ISLANDS            PT_CAP(20)  /* PROVISIONAL(M9). Jolt does not declare it: GetIslandIndexInternal is an
                                                        internal, per-step API (MotionProperties.h:258 [V]); the viewer uses
                                                        the harness FIslandBuilder */
#define PT_CAP_SLEEP_THRESHOLD_INFO      PT_CAP(21)
#define PT_CAP_BOUNDS                    PT_CAP(22)  /* PROVISIONAL(M9) */
#define PT_CAP_JOINT_SNAPSHOT            PT_CAP(23)  /* PROVISIONAL(M9) */
#define PT_CAP_NATIVE_DEBUG_DRAW         PT_CAP(24)  /* PROVISIONAL(M9). Bullet: viz runs only - the drawer is attached only in viz
                                                        and DBG_NoDeactivation mirrors the profile (btDiscreteDynamicsWorld.cpp:418-421 [V]) */
#define PT_CAP_PHASE_TIMERS              PT_CAP(25)
#define PT_CAP_ALLOC_STATS               PT_CAP(26)

/* ---- Module flags ---- */
#define PT_MODULE_ONE_PER_PROCESS         0x01u
#define PT_MODULE_NO_REINIT               0x02u
#define PT_MODULE_UE_RUNTIME              0x04u  /* Chaos DLL. The bench writes and flushes every output plus exit.json, then
                                                    ends with TerminateProcess(GetCurrentProcess(), code); the runner trusts exit.json */
#define PT_MODULE_GLOBAL_SCENE_SETTINGS   0x08u  /* settings fixed process-wide at InitGamePhys. PhysX: one PxPhysics and one
                                                    PxTolerancesScale per process (PxPhysics.h:997, PxSceneDesc.h:541 [V]);
                                                    UE4Compat/-TGS/-Stab share (100,1000) and coexist as separate scenes,
                                                    SdkDefault (1,10) needs its own process. Bullet: global combine callbacks */
#define PT_MODULE_SPAWNS_THREADS_AT_INIT  0x10u
#define PT_MODULE_DEBUG_DRAW_COMPILED     0x20u

typedef void (PT_CALL *PtLogCallback)(void* UserData, uint32_t Level, const char* Category, const char* Message);  /* may be called from any thread */

typedef struct PtBackendInfo {
  uint32_t StructSize; uint32_t ModuleFlags; uint64_t Capabilities;
  const char* Name; const char* DisplayName; const char* EngineVersion; const char* SourceCommit;
  const char* BuildConfig;
  const char* BuildFlagsJson;      /* adapter-TU self-report only (__AVX2__, _M_FP_FAST, _MSC_FULL_VER, _DLL, SDK PUBLIC defines).
                                      SDK-internal flags come from compile_commands.json (CMake) or UBT .rsp files (Chaos) */
  uint32_t NumPhaseTimers;
  uint32_t ProvisionalRevision;    /* = PT_ABI_PROVISIONAL_REVISION the module was built with; on mismatch the host
                                      treats every PROVISIONAL(M9) capability as absent */
  const char* const* PhaseTimerNames;
  uint32_t NumRequiredEffectiveKeys; uint32_t Reserved;
  const char* const* RequiredEffectiveKeys;   /* backend-specific required keys; A7 checks the generic set plus these */
} PtBackendInfo;

typedef struct PtInitParams {
  uint32_t StructSize;
  uint32_t Flags;                  /* bench | viz */
  uint32_t MaxThreads;             /* process-wide budget T. Chaos: -corelimit=T on the PreInit command line */
  uint32_t Reserved;
  PtLogCallback LogCallback; void* LogUserData;
  const char* EngineCommandLine; const char* SavedDir;
  const PtKeyValue* Options; uint32_t NumOptions;   /* process-wide module options only, e.g. the PhysX tolerances scale */
} PtInitParams;

/* Hash split: desc_sha256 covers only engine-independent inputs - materials, shapes, actor create infos, convex
   vertices, constraints, Gravity and the dt sequence (NominalDeltaTime + host plan). Every other PtSceneDesc field
   (RunMode, InstrumentFlags, SimFlags, AsyncFixedTimeStepSize, RestitutionThreshold, MaxLinearVelocity, iterations,
   NumThreads, hints, Options) goes to options_sha256, which may differ per engine; profile files go to
   physics_sha256 / variant_sha256. */
typedef struct PtSceneDesc {
  uint32_t StructSize; uint32_t RunMode; uint32_t InstrumentFlags; uint32_t SimFlags;
  const char* DebugName;
  PtVec3 Gravity;                  /* explicit (0,0,-980); SDK backends set it only when it changes */
  double NominalDeltaTime;
  double AsyncFixedTimeStepSize;   /* PT_ASYNC_DT_SYNC (-1) = sync, > 0 = async fixed dt; anything else -> PT_E_INVALID_ARG.
                                      Chaos AsyncFixed: fixed-dt stepping needs p.UseAsyncInterpolation=1, so the GT state is
                                      interpolated with one-step latency (PhysicsSolverBase.h:717-720, .cpp:591-605 [V]) */
  double RestitutionThreshold;     /* cm/s; < 0 means engine native (recorded) */
  double MaxLinearVelocity;        /* <= 0 means unclamped */
  int32_t PositionIterations, VelocityIterations, ProjectionIterations;   /* 0 = variant default */
  uint32_t NumThreads;             /* T including the caller; 1 = inline.
                                      Chaos T=1:  SetThreadingMode_External(SingleThread) + p.Chaos.SingleWorkerPhysics=1
                                                  (opt. p.Chaos.DisablePhysicsParallelFor=1) + -corelimit=1
                                                  (Parallel.cpp:95,191; ConstraintGroupSolver.cpp:250 [V])
                                      Chaos T>=2: TaskGraph + -corelimit=T + p.Chaos.MaxNumWorkers=T (a cap only;
                                                  Parallel.cpp:16,33 default 100 [V])
                                      effective keys: threads.single_worker, threads.max_num_workers */
  uint32_t MaxBodiesHint, MaxBodyPairsHint, MaxContactConstraintsHint, Reserved0;
  const PtKeyValue* Options; uint32_t NumOptions; uint32_t Reserved1;   /* scene-level variant keys */
} PtSceneDesc;

typedef struct PtPhysicalMaterial {
  double Friction, StaticFriction, Restitution;   /* raw UPhysicalMaterial values, not pre-resolved. Per contact pair:
                                                      C = combine with mode max(M0, M1)
                                                      dynamic = C(F0, F1)
                                                      static  = max(C(max(F0,SF0), max(F1,SF1)), dynamic)
                                                      (PBDCollisionConstraints.cpp:472-476, PBDCollisionConstraint.h:486 [V]).
                                                      Per backend: PhysX SdkDefault createMaterial(max(F,SF), F, R) reproduces
                                                      it (PxPhysics.h:764 [V]); PhysX UE4Compat static = dynamic [M];
                                                      Jolt/Bullet have one coefficient per pair, so their callbacks return
                                                      dynamic only (effective key material.static_friction_rule=dynamic_only) */
  double SleepLinearVelocityThreshold, SleepAngularVelocityThreshold;  /* cm/s, rad/s */
  uint32_t SleepCounterThreshold;
  uint8_t FrictionCombineMode, RestitutionCombineMode;   /* per material, resolved by the host:
                                                            bOverride*CombineMode ? material mode : project setting */
  uint16_t Reserved;
} PtPhysicalMaterial;   /* 48 B */

typedef struct PtShapeElem {
  PtTransform LocalTransform;      /* relative to the actor origin */
  uint32_t Type, MaterialIndex;
  double Radius, HalfLength;       /* capsule debug draw: HalfHeight = HalfLength + Radius */
  PtVec3 BoxHalfExtent;
  uint32_t FirstVertex, NumVertices;
  double ContactOffset;            /* < 0 = profile rule (PhysX UE4 clamp) */
  double RestOffset;
  uint32_t Flags, Reserved;
} PtShapeElem;          /* 136 B */

/* Field groups follow UE. Creation-time (CreateActor) = Motion, Flags, iterations, MaxLinearVelocity (UE uses
   sqrt(FLT_MAX)); shape step = ObjectType/BlockMask filter + material; post-add (InitDynamicProperties, UE order)
   = mass/inertia/COM, damping, MaxAngularVelocity, MaxDepenetrationVelocity, initial velocity,
   sleep/stabilization multipliers, iterations, then putToSleep for !START_AWAKE. */
typedef struct PtActorCreateInfo {
  uint32_t BodyIndex, Motion, Flags, FirstShape, NumShapes;
  uint8_t ObjectType, PositionIterations, VelocityIterations, ProjectionIterations;
  uint32_t BlockMask, Reserved0;   /* 32-channel mask (PT_NUM_COLLISION_CHANNELS) */
  PtTransform ActorTransform;
  PtVec3 InitialLinearVelocity /* at COM, cm/s */, InitialAngularVelocity /* rad/s, world; harness extension */;
  double Mass;                     /* kg, computed by harness BodyUtils */
  PtVec3 InertiaDiagonal;          /* kg*cm^2, principal */
  PtTransform CenterOfMassLocal;   /* COM position + principal axes, in actor space */
  double LinearDamping, AngularDamping;   /* UE semantics: v *= max(0, 1 - c*dt) */
  double MaxAngularVelocity;       /* rad/s; <= 0 = unclamped */
  double MaxLinearVelocity;        /* cm/s; <= 0 = unclamped */
  double MaxDepenetrationVelocity; /* < 0 = solver default (PhysX: PX_MAX_F32) */
  double SleepThresholdMultiplier, StabilizationThresholdMultiplier;
  uint64_t UserTag;
} PtActorCreateInfo;    /* 288 B */

typedef struct PtActorBatch {
  uint32_t StructSize;
  uint32_t NumActors;          const PtActorCreateInfo* Actors; uint32_t ActorStride;
  uint32_t NumShapes;          const PtShapeElem* Shapes;       uint32_t ShapeStride;
  uint32_t NumConvexVertices;  const PtVec3* ConvexVertices;
} PtActorBatch;

typedef struct PtConstraintCreateInfo {   /* PROVISIONAL(M9) */
  uint32_t Type;
  uint32_t Body1;                  /* child */
  uint32_t Body2;                  /* parent, or PT_INDEX_NONE for the world */
  uint32_t Flags;
  PtTransform Frame1, Frame2;      /* in each body's actor space */
  double TwistLimitLow, TwistLimitHigh;   /* reserved; NaN */
} PtConstraintCreateInfo;   /* 144 B */

/* ---- Body commands (FChaosEngineInterface::*_AssumesLocked, ChaosEngineInterface.h:358-471 [V]) ---- */
#define PT_CMD_SET_GLOBAL_POSE          1u
#define PT_CMD_SET_KINEMATIC_TARGET     2u
#define PT_CMD_SET_LINEAR_VELOCITY      3u
#define PT_CMD_SET_ANGULAR_VELOCITY     4u
#define PT_CMD_ADD_IMPULSE              5u   /* AddImpulse_AssumesLocked: kg*cm/s */
#define PT_CMD_ADD_ANGULAR_IMPULSE      6u   /* AddAngularImpulseInRadians_AssumesLocked */
#define PT_CMD_ADD_FORCE                7u   /* AddForce_AssumesLocked (PT_CMDF_ACCEL_CHANGE, PT_CMDF_ALLOW_SUBSTEPPING) */
#define PT_CMD_ADD_TORQUE               8u   /* AddTorque_AssumesLocked */
#define PT_CMD_WAKE_UP                  9u
#define PT_CMD_PUT_TO_SLEEP            10u
#define PT_CMD_SET_BODY_STATE          11u   /* harness-only full state write (PT_CAP_STATE_WRITE) */
#define PT_CMD_ADD_VELOCITY            12u   /* AddVelocity_AssumesLocked: cm/s, mass-independent */
#define PT_CMD_ADD_ANGULAR_VELOCITY    13u   /* AddAngularVelocityInRadians_AssumesLocked */
#define PT_CMDF_ALLOW_SUBSTEPPING  (1u << 0)  /* forces/torques: informational only (UE5 ignores bAllowSubstepping;
                                                the host uses it for the Harness driver's UE4PhysSubstep rule) */
#define PT_CMDF_ACCEL_CHANGE       (1u << 1)
#define PT_CMDF_AUTO_WAKE          (1u << 2)
#define PT_CMDF_NO_WAKE            (1u << 3)  /* forces/torques: no wake, a sleeping body ignores them (M2 fix round 1) */

typedef struct PtBodyCommand {
  uint32_t Type, Body, Flags, Reserved;
  PtTransform Transform;
  PtVec3 Vector, Vector2;
} PtBodyCommand;        /* 120 B */

typedef struct PtBodyState {
  PtTransform ActorTransform;      /* body ORIGIN, not the COM */
  PtVec3 LinearVelocity;           /* at COM, cm/s */
  PtVec3 AngularVelocity;          /* rad/s */
  uint32_t ObjectState, Flags;
} PtBodyState;          /* 112 B */

typedef struct PtMassProperties { double Mass; PtVec3 InertiaDiagonal; PtTransform CenterOfMassLocal; } PtMassProperties;  /* 88 B */
typedef struct PtFrameResult { uint32_t NumStepsTaken; uint32_t EngineErrorBits; } PtFrameResult;
typedef struct PtStepStats {
  uint32_t StructSize;
  int32_t NumActiveBodies, NumSleepingBodies, NumContactPairs, NumContactPoints, NumIslands;   /* -1 = n/a */
  uint32_t EngineErrorBits, NumPhaseTimes;
  int64_t PhaseTimeNs[8];
} PtStepStats;          /* 96 B; reads that need no listeners are allowed in headline runs */
typedef struct PtMemoryStats { int64_t LiveBytes, PeakBytes, NumAllocs, NumFrees; uint32_t AllocatorKind, Reserved; } PtMemoryStats;

/* ---- Debug snapshot (Chaos conventions): PROVISIONAL(M9) ---- */
#define PT_SNAP_PARTICLES       (1u << 0)
#define PT_SNAP_BOUNDS          (1u << 1)
#define PT_SNAP_CONTACTS        (1u << 2)
#define PT_SNAP_JOINTS          (1u << 3)
#define PT_SNAP_NATIVE_ISLANDS  (1u << 4)
#define PT_SNAP_SLEEP_INFO      (1u << 5)
#define PT_SNAP_NATIVE_DRAW     (1u << 6)
#define PT_CONTACT_ACTIVE           (1u << 0)
#define PT_CONTACT_ACTIVE_APPROX    (1u << 1)   /* Jolt: phi <= 0 */
#define PT_CONTACT_SPECULATIVE      (1u << 2)
#define PT_CONTACT_STATIC_FRICTION  (1u << 3)
#define PT_CONTACT_SWAPPED          (1u << 4)

typedef struct PtParticleSnapshot {
  PtTransform ActorTransform, CenterOfMassWorld;
  PtVec3 BoundsMin, BoundsMax;     /* engine-defined bounds */
  PtVec3 LinearVelocity, AngularVelocity;
  uint32_t BodyIndex; uint8_t ObjectState, Flags; uint16_t Reserved;
  int32_t NativeIslandId; uint32_t Reserved2;
} PtParticleSnapshot;   /* 224 B */

typedef struct PtContactPoint {
  PtVec3 PointOn0, PointOn1;       /* PointOn1 is on the plane owner */
  PtVec3 Normal;                   /* lies on Body1, points toward Body0 (ContactPoint.h:49-53 [V]) */
  double Phi;                      /* < 0 = penetrating */
  double NormalImpulse;            /* NaN if n/a */
  PtVec3 FrictionImpulse;          /* NaN if n/a */
  uint32_t Body0, Body1;           /* Body0 = the more mobile body; ties -> lower index */
  uint16_t Shape0, Shape1;
  uint32_t Flags;
} PtContactPoint;       /* 128 B */

typedef struct PtJointSnapshot { PtTransform Frame1World, Frame2World; PtVec3 LinearImpulse, AngularImpulse; uint32_t JointIndex, Flags; } PtJointSnapshot;  /* 168 B */

/* Native debug draw command type: mirrors IChaosDDRenderer::Render* (ChaosDDRenderer.h:19-44 [V]) */
#define PT_DD_POINT     0u
#define PT_DD_LINE      1u
#define PT_DD_ARROW     2u
#define PT_DD_CIRCLE    3u
#define PT_DD_SPHERE    4u
#define PT_DD_CAPSULE   5u
#define PT_DD_BOX       6u
#define PT_DD_TRIANGLE  7u
#define PT_DD_STRING    8u

typedef struct PtDebugDrawCmd {
  uint32_t Type, ColorBGRA;
  float Thickness /* cm; 0 = 1 px */, Size /* point px / arrow size / radius */;
  PtVec3 A, B, C;
  PtQuat Rotation;
  PtVec3 Extent;                   /* box: half extent (Chaos callers pass 0.5*Extents as RenderBox Size,
                                      DebugDrawImplicitObject.cpp:73-76 [V]);
                                      capsule: (Radius, HalfHeight incl. radius = 0.5*Height + Radius, 0)
                                      (DebugDrawImplicitObject.cpp:62 [V]) */
  float Lifetime;
  uint8_t DepthPriority, bDrawShadow; uint16_t Reserved;
  uint32_t TextOffset, TextLength;
} PtDebugDrawCmd;       /* 160 B */

typedef struct PtDebugSnapshotRequest { uint32_t StructSize, Flags, MaxContactPoints, MaxNativeCommands, NativeDrawCategories, Reserved; } PtDebugSnapshotRequest;
typedef struct PtDebugSnapshotCounts { uint32_t NumParticles, NumContactPoints, NumJoints, NumNativeCommands, NumStringBytes, bTruncated; } PtDebugSnapshotCounts;
typedef struct PtDebugSnapshotBuffers {
  PtParticleSnapshot* Particles;     uint32_t ParticleCapacity, ParticleStride;
  PtContactPoint*     ContactPoints; uint32_t ContactCapacity,  ContactStride;
  PtJointSnapshot*    Joints;        uint32_t JointCapacity,    JointStride;
  PtDebugDrawCmd*     NativeCommands; uint32_t CommandCapacity, CommandStride;
  char* StringBytes; uint32_t StringCapacity, Reserved;
} PtDebugSnapshotBuffers;

PT_STATIC_ASSERT(sizeof(PtPhysicalMaterial) == 48, "abi");
PT_STATIC_ASSERT(sizeof(PtShapeElem) == 136, "abi");
PT_STATIC_ASSERT(sizeof(PtActorCreateInfo) == 288, "abi");
PT_STATIC_ASSERT(sizeof(PtConstraintCreateInfo) == 144, "abi");
PT_STATIC_ASSERT(sizeof(PtBodyCommand) == 120, "abi");
PT_STATIC_ASSERT(sizeof(PtBodyState) == 112, "abi");
PT_STATIC_ASSERT(sizeof(PtMassProperties) == 88, "abi");
PT_STATIC_ASSERT(sizeof(PtStepStats) == 96, "abi");
PT_STATIC_ASSERT(sizeof(PtContactPoint) == 128, "abi");
PT_STATIC_ASSERT(sizeof(PtParticleSnapshot) == 224, "abi");
PT_STATIC_ASSERT(sizeof(PtJointSnapshot) == 168, "abi");
PT_STATIC_ASSERT(sizeof(PtDebugDrawCmd) == 160, "abi");

typedef struct PtBackendApi {
  uint32_t StructSize; uint32_t AbiVersion; const char* ModuleName;
  PtResult (PT_CALL *GetInfo)(PtBackendInfo* Out);
  PtResult (PT_CALL *InitGamePhys)(const PtInitParams* Params);
  PtResult (PT_CALL *TermGamePhys)(void);
  PtResult (PT_CALL *CreateScene)(const PtSceneDesc* Desc, PtSceneId* OutScene);
  PtResult (PT_CALL *DestroyScene)(PtSceneId Scene);
  PtResult (PT_CALL *CreateMaterials)(PtSceneId Scene, const PtPhysicalMaterial* Materials, uint32_t Num, uint32_t Stride);
  /* CreateActors, UE order (FInitBodiesHelper): per body CreateActor (creation-time flags) -> geometry, filter, material
     -> ONE AddActorsToScene_AssumesLocked (PhysX addActors) -> per body InitDynamicProperties in UE order
     -> if !PT_BODY_START_AWAKE and not kinematic: wake counter 0 + putToSleep (the body must already be in a scene);
        skipped in every engine when PT_SIM_ALLOW_SLEEPING is off (Normalized), the skipped count is recorded
     (BodyInstance.cpp:1669,1695-1706,4723-4733; NpRigidDynamic.cpp:521-527 [V]) */
  PtResult (PT_CALL *CreateActors)(PtSceneId Scene, const PtActorBatch* Batch);
  PtResult (PT_CALL *ReleaseActors)(PtSceneId Scene, const uint32_t* BodyIndices, uint32_t Num);
  PtResult (PT_CALL *CreateConstraints)(PtSceneId Scene, const PtConstraintCreateInfo* Infos, uint32_t Num, uint32_t Stride);  /* PROVISIONAL(M9) */
  PtResult (PT_CALL *PrepareForSimulation)(PtSceneId Scene, PtTextBuffer* OutActions);
  PtResult (PT_CALL *SetUpForFrame)(PtSceneId Scene, const PtVec3* NewGrav, float DeltaSeconds, float MinDeltaTime,
                                    float MaxDeltaTime, float MaxSubstepDeltaTime, int32_t MaxSubsteps, uint32_t bSubstepping);
  PtResult (PT_CALL *StartFrame)(PtSceneId Scene);
  PtResult (PT_CALL *WaitPhysScenes)(PtSceneId Scene);
  PtResult (PT_CALL *EndFrame)(PtSceneId Scene, PtFrameResult* OutResult);
  PtResult (PT_CALL *ApplyBodyCommands)(PtSceneId Scene, const PtBodyCommand* Cmds, uint32_t Num, uint32_t Stride);
  PtResult (PT_CALL *ReadBodyStates)(PtSceneId Scene, uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* OutStates,
                                     uint32_t Capacity, uint32_t* OutCount);
  PtResult (PT_CALL *ReadMassProperties)(PtSceneId Scene, uint32_t First, uint32_t Num, PtMassProperties* Out);
  PtResult (PT_CALL *GetStepStats)(PtSceneId Scene, PtStepStats* Out);
  PtResult (PT_CALL *CaptureDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotRequest* Req, PtDebugSnapshotCounts* Out);  /* PROVISIONAL(M9) */
  PtResult (PT_CALL *ReadDebugSnapshot)(PtSceneId Scene, const PtDebugSnapshotBuffers* Buffers);                             /* PROVISIONAL(M9) */
  PtResult (PT_CALL *ComputeNativeStateHash)(PtSceneId Scene, uint64_t* OutHash);
  PtResult (PT_CALL *SetOption)(PtSceneId SceneOrZero, const char* Key, const char* Value);
  PtResult (PT_CALL *DumpEffectiveSettings)(PtSceneId SceneOrZero, PtTextBuffer* OutJsonLines);
  PtResult (PT_CALL *GetMemoryStats)(PtSceneId SceneOrZero, PtMemoryStats* Out);
  PtResult (PT_CALL *GetLastErrorMessage)(PtTextBuffer* Out);
  /* v1.x: append new entries here only */
} PtBackendApi;
typedef const PtBackendApi* (PT_CALL *PtGetBackendApiFn)(uint32_t RequestedAbiMajor);   /* NULL for an unsupported major */
/* static backends: PT_GetBackendApi_Null / _Jolt / _Bullet / _PhysX;  DLL backends: PT_GetBackendApi */
#ifdef __cplusplus
}
#endif
#endif
```

**읽을 때 짚을 점**
- `PT_CMDF_ALLOW_SUBSTEPPING`이 이전 `PT_CMDF_VEL_CHANGE` 자리(bit 0)를 쓴다. v0.9라서 아직 호환 의무가 없다. **정보용 bit다** (M2 수정 라운드에서 고침). UE5 `FChaosEngineInterface::AddForce`/`AddTorque`는 `bAllowSubstepping`을 쓰지 않고 (ChaosEngineInterface.cpp:932-950, :1024-1037 [V]), push한 프레임의 모든 스텝이 첫 스텝의 가속도를 받는다 (`CopySubstepData`, ChaosMarshallingManager.cpp:103-111, :325-342 [V]). 그래서 Native 백엔드는 이 bit를 무시한다. Chaos 브리지는 받은 플래그를 그대로 `AddForce_AssumesLocked(..., bAllowSubstepping, ...)`에 넘겨도 되고 넘기지 않아도 결과가 같다. 호스트는 Harness driver의 UE4PhysSubstep 규칙(PhysX UE4Compat)에서만 이 값을 쓴다. 실제 헤더의 명령 블록 주석이 프레임 규칙 전체를 적는다 ([Abi.md](../Abi.md) §5.10).
- `PT_CMDF_NO_WAKE`(bit 3, M2 수정 라운드 1): UE의 `AddForce`/`AddTorque`는 호출하는 순간 GT에서 바디를 깨운다. 호스트는 그 깨우기를 힘을 낸 자리의 `PT_CMD_WAKE_UP`으로 보내고, 힘은 push 때 이 flag를 붙여 보낸다. 그래서 그 사이에 재운 바디가 push 끝의 힘으로 다시 깨지 않는다. flag가 없는 `PT_CMD_ADD_FORCE`는 UE `AddForce` 그대로 깨운다. `PT_READ_ALL`은 해제한 바디까지 빽빽하게 돌려주고(UNINITIALIZED), `PT_READ_ACTIVE_ONLY`는 `OutIndices`가 필요하다 (실제 헤더 주석, [Abi.md](../Abi.md) §4 #16).
- 정지마찰 쌍 규칙은 5.8.3 소스를 다시 읽고 고쳤다. Chaos는 결합 **전에** 머티리얼마다 `max(F, SF)`를 먼저 적용한다 (PBDCollisionConstraints.cpp:474-475 [V]). 그래서 규칙은 `max(C(SF0,SF1), C(F0,F1))`가 아니라 `max(C(max(F0,SF0), max(F1,SF1)), C(F0,F1))`이다. Average/Min/Multiply/Max가 모두 단조이므로 바깥 max는 사실상 항상 앞 항을 고른다. 두 식은 한 머티리얼이 SF < F이고 다른 머티리얼이 SF > F일 때 결과가 다르다. 이 식은 [02_Architecture.md](../02_Architecture.md) §5.1, [04_DecisionLog.md](../04_DecisionLog.md) DL41과 같다. Jolt·Bullet은 쌍당 계수가 하나라 동적 결합값만 돌려준다 (DL41의 한계).
- `PtStepStats` 주석의 "listener가 필요 없는 읽기"는 헤드라인 run에서도 허용한다. 해시는 헤드라인 run에서 마지막 프레임에만 계산한다 (A.7의 `HashEvery = 0`).

---

## A.2 `PtBackendKit.h` — [MVP M2]

- 백엔드마다 컴파일 정의 `PT_BACKEND_NS`를 다르게 준다 (CMake: `PT_BACKEND_NS=Jolt` 등, UBT: `PrivateDefinitions.Add("PT_BACKEND_NS=ChaosBridge")`). 백엔드가 쓰는 다른 namespace 이름(`Chaos`)은 피한다: `PhysicsTest::Abi::Chaos`가 생기면 `using namespace PhysicsTest::Abi;` 뒤의 `Chaos::`가 UE `::Chaos`와 모호해진다 (M2 수정 라운드).
  - `inline namespace` 덕분에 소스에서는 `PhysicsTest::Abi::FTextWriter`처럼 쓰지만 맹글링된 이름은 백엔드마다 다르다.
  - 그래서 `/arch:AVX2`로 컴파일한 Jolt의 Kit 코드와 SSE2로 컴파일한 Bullet의 Kit 코드가 링커에서 한 COMDAT으로 합쳐지지 않는다.
- 마지막 오류 문자열, 로그 콜백, 소유 스레드, 씬 슬롯은 모두 `TBackendApiProvider<T>`의 static 멤버다. inline 전역 상태는 없다.
- 기본 가상 함수의 인자는 이름을 주석으로만 남긴다. `/W4 /WX`에서 C4100(C2220)을 피하려는 것이다.

```cpp
// PtBackendKit.h - header-only, backend side. Compiled into each backend (CMake static lib or UBT module).
// No STL in the interface; no RTTI or exceptions required; no check/verify/ensure/min/max identifiers.
#pragma once
#include "PhysicsTestAbi/PtBackendAbi.h"
#ifndef PT_BACKEND_NS                                  // CMake: PT_BACKEND_NS=Jolt etc.; UBT: PrivateDefinitions "PT_BACKEND_NS=ChaosBridge"
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
#ifndef PT_KIT_CURRENT_THREAD_ID                       // UE build: FPlatformTLS::GetCurrentThreadId()
#  include <intrin.h>
#  define PT_KIT_CURRENT_THREAD_ID() ((uint32_t)__readgsdword(0x48))
#endif
#ifndef PT_KIT_MAX_SCENES
#  define PT_KIT_MAX_SCENES 16
#endif

namespace PhysicsTest::Abi {
// Every non-template definition gets a per-backend mangled name, so backends built with different /arch
// never share (COMDAT-fold) Kit code, and no Kit state is shared between static backends.
inline namespace PT_BACKEND_NS {

struct FStringRef { const char* Data; uint32_t Length; bool Equals(const char* Z) const; };

class FTextWriter {
public:
  explicit FTextWriter(PtTextBuffer* Target);
  void Append(const char* Text);
  void AppendEffective(const char* Key, const char* Native, const char* NativeUnit, double Canonical, const char* Unit, const char* Source);
  PtResult Finish();
private:
  PtTextBuffer* Buffer; uint32_t Needed;
};

template <class T> struct TStridedView { const uint8_t* Base; uint32_t Count, Stride; const T& operator[](uint32_t I) const; };
struct FActorBatchView { TStridedView<PtActorCreateInfo> Actors; TStridedView<PtShapeElem> Shapes; const PtVec3* ConvexVertices; uint32_t NumConvexVertices; };
struct FFrameParams { PtVec3 Gravity; float DeltaSeconds, MinDeltaTime, MaxDeltaTime, MaxSubstepDeltaTime; int32_t MaxSubsteps; bool bSubstepping; };
enum class EScenePhase : uint8_t { Idle, SetUp, Started, Waited };

class FKitServices {                                  // one instance per backend class, owned by TBackendApiProvider<T>
public:
  PtResult SetLastError(PtResult Code, const char* Fmt, ...);
  void LogToHost(uint32_t Level, const char* Category, const char* Fmt, ...) const;
  uint32_t GetOwnerThreadId() const;
private:
  PtLogCallback LogCallback = nullptr; void* LogUserData = nullptr;
  uint32_t OwnerThreadId = 0; uint32_t LastErrorLength = 0; char LastError[1024] = {};
  template <class> friend struct TBackendApiProvider;
};

class IPhysicsBackendScene {                          // ~ FChaosScene + FChaosEngineInterface
public:
  virtual ~IPhysicsBackendScene() = default;
  virtual PtResult CreateMaterials(TStridedView<PtPhysicalMaterial> Materials) = 0;
  // Already validated by the kit. UE order: per body CreateActor -> geometry/filter/material -> ONE AddActorsToScene
  // -> per body InitDynamicProperties (UE order) -> putToSleep for !PT_BODY_START_AWAKE (skipped when sleeping is off).
  virtual PtResult CreateActors(const FActorBatchView& Batch) = 0;
  virtual PtResult ReleaseActors(const uint32_t* BodyIndices, uint32_t Num) = 0;
  virtual PtResult CreateConstraints(TStridedView<PtConstraintCreateInfo> /*Infos*/) { return PT_E_UNSUPPORTED; }   // PROVISIONAL(M9)
  virtual PtResult PrepareForSimulation(FTextWriter& /*OutActions*/) { return PT_OK; }
  virtual PtResult SetUpForFrame(const FFrameParams& Params) = 0;  // without NATIVE_FRAME_MODEL: MaxSubsteps == 1 && !bSubstepping
  virtual PtResult StartFrame() = 0;
  virtual PtResult WaitPhysScenes() = 0;
  virtual PtResult EndFrame(PtFrameResult& Out) = 0;
  virtual PtResult ApplyBodyCommand(const PtBodyCommand& Cmd) = 0;
  virtual PtResult ReadBodyStates(uint32_t ReadFlags, uint32_t* OutIndices, PtBodyState* Out, uint32_t Capacity, uint32_t& OutCount) = 0;
  virtual PtResult ReadMassProperties(uint32_t First, uint32_t Num, PtMassProperties* Out) = 0;
  virtual PtResult GetStepStats(PtStepStats& Out) = 0;
  virtual PtResult CaptureDebugSnapshot(const PtDebugSnapshotRequest& /*Req*/, PtDebugSnapshotCounts& /*Out*/) { return PT_E_UNSUPPORTED; }   // PROVISIONAL(M9)
  virtual PtResult ReadDebugSnapshot(const PtDebugSnapshotBuffers& /*Buffers*/) { return PT_E_UNSUPPORTED; }                              // PROVISIONAL(M9)
  virtual PtResult ComputeNativeStateHash(uint64_t& /*OutHash*/) { return PT_E_UNSUPPORTED; }
  virtual PtResult SetOption(FStringRef /*Key*/, FStringRef /*Value*/) { return PT_E_UNSUPPORTED; }
  virtual PtResult DumpEffectiveSettings(FTextWriter& Out) = 0;
  virtual PtResult GetMemoryStats(PtMemoryStats& /*Out*/) { return PT_E_UNSUPPORTED; }
  EScenePhase GetPhase() const;                     // maintained by the kit
  uint32_t GetNumBodies() const;
  FKitServices& GetKit() const;                      // bound by the provider after CreateScene
private:
  FKitServices* Kit = nullptr; EScenePhase Phase = EScenePhase::Idle; uint32_t NumBodies = 0;
  template <class> friend struct TBackendApiProvider;
};

class IPhysicsBackend {                               // ~ FChaosSolversModule + InitGamePhys / TermGamePhys
public:
  virtual ~IPhysicsBackend() = default;              // never runs for UE modules
  virtual void GetInfo(PtBackendInfo& Out) const = 0;
  virtual PtResult InitGamePhys(const PtInitParams& Params) = 0;
  virtual PtResult TermGamePhys() = 0;
  virtual PtResult CreateScene(const PtSceneDesc& Desc, IPhysicsBackendScene*& OutScene) = 0;
  virtual void DestroyScene(IPhysicsBackendScene* Scene) = 0;
  virtual PtResult SetModuleOption(FStringRef /*Key*/, FStringRef /*Value*/) { return PT_E_UNSUPPORTED; }
  virtual PtResult DumpModuleSettings(FTextWriter& /*Out*/) { return PT_OK; }
  FKitServices& GetKit() const;
private:
  FKitServices* Kit = nullptr;
  template <class> friend struct TBackendApiProvider;
};

// Bounds, finite values, unit quats (1e-6), indices, dynamic mass > 0, AsyncFixedTimeStepSize == -1 or > 0.
PtResult ValidateActorBatch(const PtActorBatch& Batch, uint32_t NumMaterials, FTextWriter* Error);
PtResult ValidateSceneDesc(const PtSceneDesc& Desc, FTextWriter* Error);

// C thunks, thread guard, phase machine, PT_KIT_MAX_SCENES handle slots, last-error and log state.
// All state is per TBackend instantiation (static members), never an inline global.
template <class TBackend> struct TBackendApiProvider {
  static const PtBackendApi* Get(uint32_t RequestedMajor);   // NULL unless RequestedMajor == PT_ABI_VERSION_MAJOR
private:
  static inline FKitServices Kit{};
  static inline IPhysicsBackendScene* Slots[PT_KIT_MAX_SCENES] = {};
  static inline uint32_t Generations[PT_KIT_MAX_SCENES] = {};
};

}   // inline namespace PT_BACKEND_NS
}   // namespace PhysicsTest::Abi

#define PT_IMPLEMENT_BACKEND_MODULE(BackendClass) \
  extern "C" __declspec(dllexport) const PtBackendApi* PT_CALL PT_GetBackendApi(uint32_t RequestedAbiMajor) \
  { return ::PhysicsTest::Abi::TBackendApiProvider<BackendClass>::Get(RequestedAbiMajor); }
#define PT_IMPLEMENT_BACKEND_MODULE_STATIC(BackendClass, EntryName) \
  extern "C" const PtBackendApi* PT_CALL EntryName(uint32_t RequestedAbiMajor) \
  { return ::PhysicsTest::Abi::TBackendApiProvider<BackendClass>::Get(RequestedAbiMajor); }
```

---

## A.3 `PtAbiUeCompatCheck.cpp` — [MVP M2]

- `PtAbiUeCompatCheck` OBJECT 타깃의 유일한 소스다. UE와 같은 플래그(`/W4 /WX /GR- /EHs-c- _HAS_EXCEPTIONS=0`)로 Kit을 컴파일한다.
- 사소한 백엔드를 실제로 인스턴스화하므로 Kit의 기본 함수 본문과 `TBackendApiProvider<T>`가 매 빌드 컴파일된다.
- 백엔드 클래스는 이름 있는 네임스페이스에 둔다. 익명 네임스페이스에 두면 정의가 다른 TU에 있는 템플릿 멤버와 엮여 C5046이 난다 (이 스케치를 컴파일하며 확인 **[V]**).

```cpp
// PtAbiUeCompatCheck.cpp - OBJECT target compiled with /W4 /WX /GR- /EHs-c- _HAS_EXCEPTIONS=0 (UE-like flags),
// PT_BACKEND_NS=UeCompatCheck. Instantiates a trivial backend so every Kit default body and the provider template are compiled.
#include "PhysicsTestAbi/PtBackendKit.h"

namespace PtUeCompatCheck {
using namespace ::PhysicsTest::Abi;

class FTrivialScene final : public IPhysicsBackendScene {
public:
  PtResult CreateMaterials(TStridedView<PtPhysicalMaterial>) override { return PT_OK; }
  PtResult CreateActors(const FActorBatchView&) override { return PT_OK; }
  PtResult ReleaseActors(const uint32_t*, uint32_t) override { return PT_OK; }
  PtResult SetUpForFrame(const FFrameParams&) override { return PT_OK; }
  PtResult StartFrame() override { return PT_OK; }
  PtResult WaitPhysScenes() override { return PT_OK; }
  PtResult EndFrame(PtFrameResult& Out) override { Out.NumStepsTaken = 1; Out.EngineErrorBits = 0; return PT_OK; }
  PtResult ApplyBodyCommand(const PtBodyCommand&) override { return PT_OK; }
  PtResult ReadBodyStates(uint32_t, uint32_t*, PtBodyState*, uint32_t, uint32_t& OutCount) override { OutCount = 0; return PT_OK; }
  PtResult ReadMassProperties(uint32_t, uint32_t, PtMassProperties*) override { return PT_OK; }
  PtResult GetStepStats(PtStepStats&) override { return PT_OK; }
  PtResult DumpEffectiveSettings(FTextWriter&) override { return PT_OK; }
};

class FTrivialBackend final : public IPhysicsBackend {
public:
  void GetInfo(PtBackendInfo&) const override {}
  PtResult InitGamePhys(const PtInitParams&) override { return PT_OK; }
  PtResult TermGamePhys() override { return PT_OK; }
  PtResult CreateScene(const PtSceneDesc&, IPhysicsBackendScene*& OutScene) override { OutScene = nullptr; return PT_OK; }
  void DestroyScene(IPhysicsBackendScene*) override {}
};
}   // namespace PtUeCompatCheck

PT_IMPLEMENT_BACKEND_MODULE_STATIC(PtUeCompatCheck::FTrivialBackend, PT_GetBackendApi_UeCompatCheck)

// keep FTrivialScene referenced so its vtable (and the Kit default bodies) are emitted
::PhysicsTest::Abi::IPhysicsBackendScene* PtUeCompatCheck_MakeScene() { static PtUeCompatCheck::FTrivialScene Scene; return &Scene; }
```

---

## A.4 BackendCommon (header-only) — [MVP M0 / M2 / M3 / M5]

- 백엔드는 `PT::Abi`, `PT::BackendCommon`, 자기 SDK만 본다 (R4). 하네스 Core를 include하지 않는다.
- `PtThirdPartyIncludes.h`(백엔드 측, warning push/pop 매크로만)와 `PtBuildReport.h`는 정의만 담으므로 `PT_BACKEND_NS`가 필요 없다. 하네스 측 서드파티 래퍼는 Core의 include 파일 쌍이다 (이 절 끝).
- 나머지 BackendCommon 헤더는 `PT_BACKEND_NS`가 없으면 `#error`다. Automation의 BackendCommon 단위 테스트 TU는 `PT_BACKEND_NS=AutomationTest`로 컴파일한다.
- Chaos UBT 모듈은 `PhysicsTestAbi/Public`만 include 경로에 넣으므로 BackendCommon을 보지 않는다. Chaos는 combine, 단위, 접촉 규약을 엔진 자체로 처리한다.

### `PtTypes.h`

```cpp
// PtTypes.h - fixed-width aliases so backend code (UE4-named PhysX clean room) never needs harness Core.
#pragma once
#include <cstdint>
#include <cstddef>
namespace PhysicsTest::Abi::FixedWidth {
using int8  = std::int8_t;  using int16  = std::int16_t;  using int32  = std::int32_t;  using int64  = std::int64_t;
using uint8 = std::uint8_t; using uint16 = std::uint16_t; using uint32 = std::uint32_t; using uint64 = std::uint64_t;
}
namespace PhysicsTest::Abi { using namespace FixedWidth; }
```

### `PtUnitScale.h`

```cpp
// PtUnitScale.h - the only place where units are converted (D1).
#pragma once
#include "PtTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"
#ifndef PT_BACKEND_NS
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS {
struct FUnitScale {                                   // L = native length units per cm
  double L = 1.0;
  constexpr double Length(double Cm) const { return Cm * L; }             // also velocity, acceleration, force, linear impulse
  constexpr double Inertia(double KgCm2) const { return KgCm2 * L * L; }  // also torque and angular impulse (x L^2)
  constexpr double ToCm(double Native) const { return Native / L; }
  constexpr double ToKgCm2(double Native) const { return Native / (L * L); }
};
inline constexpr FUnitScale GCentimetres{1.0};       // Chaos, PhysX UE4Compat / -TGS / -Stab
inline constexpr FUnitScale GMetres{0.01};           // Jolt, Bullet, PhysX SdkDefault
enum class ECapsuleAxis : uint8 { X, Y, Z };
// UE capsule axis Z -> PhysX X (U2PSphylBasis), Jolt Y (RotatedTranslatedShape).
// Bullet uses btCapsuleShapeZ: no remap (D1 amendment).
PtQuat CapsuleAxisRemap(ECapsuleAxis NativeAxis);
}}
```

### `PtCombine.h`

```cpp
// PtCombine.h - UE/Chaos material combine rules, used by the Jolt and Bullet combine callbacks.
// Jolt and Bullet keep ONE friction coefficient per pair (mCombinedFriction, m_combinedFriction): their callbacks
// return FPairFriction::Dynamic and report material.static_friction_rule=dynamic_only (DL41). Static is the
// reference value for tests and for the effective-settings readback.
#pragma once
#include "PtTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"
#ifndef PT_BACKEND_NS
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS {
uint8 ResolveCombineMode(uint8 ModeA, uint8 ModeB);      // max(A, B) (Chaos/Defines.h:160-164 [V])
double CombineValues(double A, double B, uint8 Mode);    // Average / Min / Multiply / Max (Defines.h:146-158 [V])
struct FPairFriction { double Dynamic, Static; };
// Dynamic = C(F0, F1); Static = max(C(max(F0,SF0), max(F1,SF1)), Dynamic), C = CombineValues with the resolved
// friction mode (PBDCollisionConstraints.cpp:472-476, PBDCollisionConstraint.h:486 [V]).
FPairFriction CombinePairFriction(const PtPhysicalMaterial& M0, const PtPhysicalMaterial& M1);
double CombinePairRestitution(const PtPhysicalMaterial& M0, const PtPhysicalMaterial& M1);
}}
```

### `PtDamping.h`

```cpp
// PtDamping.h
#pragma once
#ifndef PT_BACKEND_NS
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS {
double UEDampingToBullet(double C, double Dt);   // c_b = 1 - (1 - c*dt)^(1/dt); UE, Chaos, Jolt: v *= max(0, 1 - c*dt)
}}
```

### `PtContactConvention.h`

```cpp
// PtContactConvention.h - Chaos contact convention (normal on Body1 toward Body0, Phi < 0 = penetrating).
#pragma once
#include "PtTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"
#ifndef PT_BACKEND_NS
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS {
// Body0 = more mobile (Dynamic > Kinematic > Static), ties -> lower index; sets PT_CONTACT_SWAPPED on a swap.
void CanonicalizeContact(PtContactPoint& InOut, const uint8* MobilityRank, bool bNativeNormalTowardBody1, bool bNativeDepthPositive);
uint64 Fnv1a64(const void* Data, size_t Bytes, uint64 Seed = 0xcbf29ce484222325ull);
}}
```

### `PtShapeWrapping.h`

```cpp
// PtShapeWrapping.h - shared shape decisions; every backend applies the same COM-wrapping rule.
#pragma once
#include "PtTypes.h"
#include "PhysicsTestAbi/PtBackendAbi.h"
#ifndef PT_BACKEND_NS
#  error "PT_BACKEND_NS must be defined per backend target"
#endif
namespace PhysicsTest::Abi { inline namespace PT_BACKEND_NS {
inline constexpr double GComIdentityMaxOffsetCm = 1e-6;          // |t| < 1e-6 cm
inline constexpr double GComIdentityMinQuatDot  = 1.0 - 1e-9;    // |q . identity| > 1 - 1e-9
bool IsComLocalIdentity(const PtTransform& CenterOfMassLocal);
// true : one element and COM ~ identity -> native primitive (btBoxShape, Jolt BoxShape, ...)
// false: btCompoundShape / Jolt OffsetCenterOfMassShape (or StaticCompoundShape).
// Counts per kind (compound, offset_com, axis_remap) go to the effective key shape.wrapping;
// M3.BoxPile must create 0 wrappers (checked by G9).
bool CanUseNativePrimitive(const PtActorCreateInfo& Actor, const PtShapeElem* FirstShape);
}}
```

### `PtBuildReport.h`

```cpp
// PtBuildReport.h - adapter-TU self-report for G0. Only facts visible to THIS TU: compiler/flags and the SDK's
// PUBLIC defines (Jolt JPH_*, PT::Bullet BT_*, PT::PhysX PX_PHYSX_STATIC_LIB / DISABLE_CUDA_PHYSX, PX_PHYSICS_VERSION).
// SDK-internal defines (e.g. PhysX PX_SUPPORT_PVD, a PRIVATE per-config define) come from compile_commands.json
// entries of the SDK's own TUs plus the selected PhysX config. BuildFlags.json is a build-time
// add_custom_command(OUTPUT BuildFlags.json DEPENDS compile_commands.json). Chaos: UBT .rsp files.
#pragma once
#define PT_BR_STR2(x) #x
#define PT_BR_STR(x) PT_BR_STR2(x)
#if defined(__AVX2__)
#  define PT_BR_AVX2 "\"__AVX2__\":1,"
#else
#  define PT_BR_AVX2 "\"__AVX2__\":0,"
#endif
#if defined(_M_FP_FAST)
#  define PT_BR_FP "\"fp\":\"fast\","
#elif defined(_M_FP_PRECISE)
#  define PT_BR_FP "\"fp\":\"precise\","
#else
#  define PT_BR_FP "\"fp\":\"other\","
#endif
#if defined(_DLL)
#  define PT_BR_CRT "\"_DLL\":1,"
#else
#  define PT_BR_CRT "\"_DLL\":0,"
#endif
// SdkFields: string literal of extra "key":value pairs written by the backend TU, e.g. "\"JPH_PROFILE_ENABLED\":0"
#define PT_BUILD_REPORT_JSON(Name, SdkFields) \
  static const char Name[] = "{" PT_BR_AVX2 PT_BR_FP PT_BR_CRT "\"_MSC_FULL_VER\":" PT_BR_STR(_MSC_FULL_VER) "," SdkFields "}"
```

### 서드파티 include 래퍼: `PtThirdPartyIncludes.h` (백엔드 측), `Misc/PtThirdPartyIncludesStart.h` / `End.h` (하네스 측)

[02_Architecture.md](../02_Architecture.md) §8.1과 [04_DecisionLog.md](../04_DecisionLog.md) DL03을 따라 두 장치로 나눈다.
- **백엔드 측 (BackendCommon `PtThirdPartyIncludes.h`):** 백엔드 Private TU가 SDK 헤더를 include할 때 쓴다. 백엔드 TU는 하네스 Core를 보지 않아 `check` 등이 없으므로 `PT_THIRD_PARTY_INCLUDES_START/END` 매크로는 warning push/pop만 한다.
- **하네스 측 (Core `Misc/PtThirdPartyIncludesStart.h` / `Misc/PtThirdPartyIncludesEnd.h`):** 매크로는 `#undef`를 만들어 낼 수 없으므로 include 파일 쌍으로 둔다. doctest, ImGui, ImPlot, nlohmann/json은 반드시 이 쌍을 거친다. [03_WorkPlan.md](../03_WorkPlan.md) M0의 "하네스 측 `PT_THIRD_PARTY_INCLUDES_START/END`"가 이 쌍을 가리킨다.

```cpp
// BackendCommon/Public/PtThirdPartyIncludes.h - BACKEND side only (SDK headers in backend Private TUs).
// Backend TUs never include harness Core, so check/verify/ensure are not defined there: warnings only.
//   PT_THIRD_PARTY_INCLUDES_START
//   #include <Jolt/Jolt.h>
//   PT_THIRD_PARTY_INCLUDES_END
#pragma once
#define PT_THIRD_PARTY_INCLUDES_START __pragma(warning(push, 0))
#define PT_THIRD_PARTY_INCLUDES_END   __pragma(warning(pop))
```

```cpp
// Core/Public/Misc/PtThirdPartyIncludesStart.h - HARNESS side; no include guard, always paired with ...End.h.
// A macro cannot emit #undef, hence an include-file pair:
//   #include "Misc/PtThirdPartyIncludesStart.h"
//   #include <doctest/doctest.h>     // also ImGui, ImPlot, nlohmann/json
//   #include "Misc/PtThirdPartyIncludesEnd.h"
// doctest v2.5.3 declares a member check() (doctest.h:1647). A CTest compile probe includes harness Core first.
#pragma warning(push, 0)
#pragma push_macro("check")
#pragma push_macro("checkf")
#pragma push_macro("verify")
#pragma push_macro("ensure")
#undef check
#undef checkf
#undef verify
#undef ensure
```

```cpp
// Core/Public/Misc/PtThirdPartyIncludesEnd.h - HARNESS side; no include guard.
#pragma pop_macro("ensure")
#pragma pop_macro("verify")
#pragma pop_macro("checkf")
#pragma pop_macro("check")
#pragma warning(pop)
```

---

## A.5 Core (`namespace PhysicsTest`) — [MVP M0, 최소 구현]

- UE 관용구는 이름과 사용법만 맞춘 소형 구현이다 (S5). UE 전체를 복제하지 않는다.
- MVP의 시계는 QPC다. MSVC의 `std::chrono::steady_clock`이 QPC를 감싼다 **[V]**. rdtscp + QPC 보정 시계(`FCycleCounter`)는 확장 M10에서 추가한다 (S2).

```cpp
// File: Source/Runtime/Core/Public/{Math/*.h, HAL/PlatformTime.h, Misc/*.h, Logging/LogMacros.h, HAL/IConsoleManager.h}
//   [MVP M0, minimal implementations (S5); ini and CVar registry M1]   FCycleCounter: [확장 M10]
#pragma once
#include <cstdint>
namespace PhysicsTest {
using int8  = std::int8_t;  using int16  = std::int16_t;  using int32  = std::int32_t;  using int64  = std::int64_t;
using uint8 = std::uint8_t; using uint16 = std::uint16_t; using uint32 = std::uint32_t; using uint64 = std::uint64_t;
inline constexpr int32 INDEX_NONE = -1;
inline constexpr float UE_SMALL_NUMBER = 1.e-8f;
inline constexpr float UE_KINDA_SMALL_NUMBER = 1.e-4f;

struct FQuat;
struct FRotator;
struct FVector    { double X = 0, Y = 0, Z = 0;
                    static const FVector ZeroVector, OneVector, UpVector;
                    double Size() const; double SizeSquared() const; FVector GetSafeNormal(double Tolerance = 1e-8) const;
                    bool IsNearlyZero(double Tolerance = 1e-4) const;
                    /* + - * /, dot (|), cross (^) */ };
struct FVector3f  { float X = 0, Y = 0, Z = 0; };
struct FQuat      { double X = 0, Y = 0, Z = 0, W = 1; static const FQuat Identity;
                    FVector RotateVector(const FVector&) const; FVector UnrotateVector(const FVector&) const;
                    FQuat Inverse() const; FQuat GetNormalized() const; FQuat operator*(const FQuat&) const;
                    FRotator Rotator() const; static FQuat MakeFromEuler(const FVector& RollPitchYawDeg);
                    FVector GetAxisX() const; FVector GetAxisY() const; FVector GetAxisZ() const; };
struct FRotator   { double Pitch = 0, Yaw = 0, Roll = 0;            // degrees, UE field order
                    static const FRotator ZeroRotator; FQuat Quaternion() const; };
struct FMatrix    { double M[4][4]; };                               // IDebugDrawRenderer::RenderCircle axes
struct FMatrix44f { float M[4][4]; };                                // render matrices
struct FTransform { FQuat Rotation; FVector Translation; FVector Scale3D{1, 1, 1}; static const FTransform Identity;
                    FTransform() = default;
                    FTransform(const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector{1, 1, 1});
                    FTransform(const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector{1, 1, 1});
                    FVector TransformPosition(const FVector&) const; FVector TransformVector(const FVector&) const;
                    FVector InverseTransformPosition(const FVector&) const; FTransform Inverse() const;
                    FTransform operator*(const FTransform& Other) const; };   // UE order: A*B applies A, then B
struct FBox       { FVector Min, Max; uint8 IsValid = 0;              // UE TBox field name
                    FVector GetCenter() const; FVector GetExtent() const; FBox ExpandBy(double W) const; };
struct FSphere    { FVector Center; double W = 0; };
struct FColor     { uint8 B = 0, G = 0, R = 0, A = 255;              // UE memory order (BGRA)
                    constexpr FColor() = default; constexpr FColor(uint8 InR, uint8 InG, uint8 InB, uint8 InA = 255);
                    static const FColor White, Black, Red, Green, Blue, Yellow, Cyan, Magenta, Orange, Purple, Emerald, Turquoise, Silver; };
struct FLinearColor { float R, G, B, A; explicit FLinearColor(const FColor& SRGB); };

struct FPlatformTime { static uint64 Cycles64(); static double GetSecondsPerCycle64(); };   // [MVP] QPC (steady_clock on MSVC [V])
struct FCycleCounter {                                                                      // [확장 M10] rdtscp + lfence
  static uint64 Read();
  static double CalibrateNsPerTick(uint64 Tsc0, uint64 Qpc0, uint64 Tsc1, uint64 Qpc1);    // QPC calibration over the run
  static double MeasureReadOverheadNs(int32 Samples = 100000);
};
// Also provided (minimal, S5): UE_LOG / DECLARE_LOG_CATEGORY_EXTERN, check / checkf / verify / ensure (PT_DO_CHECK),
//   FCommandLine, FParse::Value / Param, FRandomStream (mt19937_64 + harness [0,1) mapping, seed 0x5EED), FSHA256 [M0];
//   FConfigFile / FConfigCacheIni, IConsoleManager, TAutoConsoleVariable<T>, FAutoConsoleVariableRef [M1]
//   (M1 implemented FConfigFile without FConfigCacheIni, see A.0.3).
// Third-party headers (doctest, ImGui, ImPlot, nlohmann/json) are included only between
// #include "Misc/PtThirdPartyIncludesStart.h" and #include "Misc/PtThirdPartyIncludesEnd.h" (A.4).
}
```

---

## A.6 PhysicsCore 저작 데이터 모델 — [MVP M1]

- UE 필드 이름과 기본값을 그대로 쓴다. 값의 근거는 각 블록 주석에 있다.
- 질량 규칙(`BodyUtils`)의 단계 순서를 5.8.3 소스대로 고쳤다. 밀도 → 부피 적분 → 대각화 → 질량 규칙 → 관성 비율 스케일 → COMNudge → InertiaTensorScale → 평행축 항이다.

### `Physics/PhysicsInterfaceTypes.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysicsInterfaceTypes.h   [MVP M1]
namespace PhysicsTest {
enum class ECollisionEnabled : uint8 { NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics };   // UE: ECollisionEnabled::Type
enum class EFrictionCombineMode : uint8 { Average = 0, Min = 1, Multiply = 2, Max = 3 };       // = PT_COMBINE_*, PxCombineMode
enum class EObjectStateType : uint8 { Uninitialized = 0, Sleeping = 1, Kinematic = 2, Static = 3, Dynamic = 4 };
enum class ETeleportType : uint8 { None, TeleportPhysics, ResetPhysics };
enum class ESleepFamily : uint8 { Normal, Sensitive, Custom };
enum class EPhysicsType : uint8 { PhysType_Default, PhysType_Kinematic, PhysType_Simulated };
// 0..31 (PT_NUM_COLLISION_CHANNELS). UE 5.8.3 has ECC_MAX = 65 with uint64 masks (EngineTypes.h:1101-1176 [V]);
// 32 is a deliberate Phase-1 limit (UE4 PhysX word3, Bullet int masks). The Chaos bridge widens masks to uint64.
using ECollisionChannel = uint8;

class FPhysicsBackendScene;
struct FPhysicsActorHandle      { FPhysicsBackendScene* Scene = nullptr; uint32 Index = ~0u; uint32 Serial = 0; bool IsValid() const; };
struct FPhysicsConstraintHandle { FPhysicsBackendScene* Scene = nullptr; uint32 Index = ~0u; uint32 Serial = 0; bool IsValid() const; };
}
```

### `PhysicsEngine/ShapeElem.h` … `AggregateGeom.h`

```cpp
// ---- File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/ShapeElem.h   [MVP M1]
namespace PhysicsTest {
struct FKShapeElem  { std::string Name; double RestOffset = 0; bool bContributeToMass = true;
                      ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics; };
}
// ---- File: PhysicsEngine/SphereElem.h
namespace PhysicsTest {
struct FKSphereElem : FKShapeElem { FVector Center; double Radius = 1; };
}
// ---- File: PhysicsEngine/BoxElem.h
namespace PhysicsTest {
struct FKBoxElem : FKShapeElem {
  FVector Center; FRotator Rotation; double X = 1, Y = 1, Z = 1;  // FULL extents; FRotator as in UE (BoxElem.h:28-36 [V])
  FTransform GetTransform() const;                                // FRotator -> FQuat on demand (marshalling, untimed)
};
}
// ---- File: PhysicsEngine/SphylElem.h
namespace PhysicsTest {
struct FKSphylElem : FKShapeElem {
  FVector Center; FRotator Rotation; double Radius = 1, Length = 1;   // Length = segment along local Z (SphylElem.h:28-40 [V])
  FTransform GetTransform() const;                                    // ABI HalfLength = Length / 2
};
}
// ---- File: PhysicsEngine/ConvexElem.h   [MVP: data model + ABI only; hull build and convex mass: 확장 M15 backlog]
namespace PhysicsTest {
struct FKConvexElem : FKShapeElem {
  std::vector<FVector> VertexData; FTransform Transform; FBox ElemBox;
  std::vector<std::array<int32, 3>> HullTriangles;                    // filled by BuildHull() [확장 M15]
  void BuildHull();
};
}
// ---- File: PhysicsEngine/AggregateGeom.h
namespace PhysicsTest {
struct FKAggregateGeom { std::vector<FKSphereElem> SphereElems; std::vector<FKBoxElem> BoxElems;
                         std::vector<FKSphylElem> SphylElems;   std::vector<FKConvexElem> ConvexElems;
                         int32 GetElementCount() const; FBox CalcAABB(const FTransform&) const; };
}
```

### `PhysicalMaterials/PhysicalMaterial.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicalMaterials/PhysicalMaterial.h   [MVP M1]
namespace PhysicsTest {
struct FPhysicsSettingsCore;
struct FPhysicalMaterial {                          // ~ UPhysicalMaterial (PhysicalMaterial.cpp:42-60 [V])
  std::string Name;
  float Friction = 0.7f, StaticFriction = 0.f, Restitution = 0.3f;   // raw values, passed through the ABI unchanged
  EFrictionCombineMode FrictionCombineMode = EFrictionCombineMode::Average;     bool bOverrideFrictionCombineMode = false;
  EFrictionCombineMode RestitutionCombineMode = EFrictionCombineMode::Average;  bool bOverrideRestitutionCombineMode = false;
  float Density = 1.f, RaiseMassToPower = 0.75f;                     // g/cm^3
  float SleepLinearVelocityThreshold = 1.f, SleepAngularVelocityThreshold = 0.05f; int32 SleepCounterThreshold = 4;
  // override ? own mode : project FrictionCombineMode / RestitutionCombineMode (written into PtPhysicalMaterial)
  EFrictionCombineMode GetEffectiveFrictionCombineMode(const FPhysicsSettingsCore& Project) const;
  EFrictionCombineMode GetEffectiveRestitutionCombineMode(const FPhysicsSettingsCore& Project) const;
  static EFrictionCombineMode ResolveCombineMode(EFrictionCombineMode A, EFrictionCombineMode B);   // the larger value wins
  // Reference for tests only (backends use BackendCommon CombinePairFriction):
  // dynamic = C(F0,F1); static = max(C(max(F0,SF0), max(F1,SF1)), dynamic)
  static void CombinePairFriction(const FPhysicalMaterial& A, const FPhysicalMaterial& B, const FPhysicsSettingsCore& Project,
                                  double& OutDynamic, double& OutStatic);
};
}
```

### `PhysicsEngine/BodySetup.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/BodySetup.h   [MVP M1]
namespace PhysicsTest {
class FBodySetup {                                  // ~ UBodySetup (no cooking, no UObject)
public:
  std::string BodySetupName; FKAggregateGeom AggGeom; const FPhysicalMaterial* PhysMaterial = nullptr;
  EPhysicsType PhysicsType = EPhysicsType::PhysType_Default; FVector BuildScale3D{1, 1, 1};
  double GetVolume(const FVector& Scale3D) const;
};
}
```

### `PhysicsEngine/BodyInstance.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/BodyInstance.h   [MVP M1]
namespace PhysicsTest {
struct FBodyCollisionFilter { ECollisionChannel ObjectType = 0; uint32 BlockMask = 0xFFFFFFFFu;
                              ECollisionEnabled CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
                              bool CanCollideWith(const FBodyCollisionFilter& Other) const; };   // UE mutual-block test (CollisionFilterData.cpp:263-272 [V])
struct FMassProperties { double Volume = 0, Mass = 0; FVector CenterOfMass; FQuat RotationOfMass; FVector InertiaTensor;
                         FTransform GetComLocalPose() const; };
struct FBodyInstanceState { FTransform BodyTM; FVector LinearVelocity; FVector AngularVelocityInRadians;    // BodyTM = body origin
                            EObjectStateType ObjectState = EObjectStateType::Uninitialized; };
class FPhysScene;

// FBodyInstance + FBodyInstanceCore + FActorCreationParams.
// Defaults: BodyInstance.cpp:357-418 [V] (8/2/1, damping 0.01/0, MassInKgOverride 100, bInertiaConditioning, MaxAngularVelocity)
//           BodyInstanceCore.cpp:13-24 [V] (bSimulatePhysics, bOverrideMass, bEnableGravity, bGyroscopicTorqueEnabled, bStartAwake)
struct FBodyInstance {
  bool bSimulatePhysics = false, bOverrideMass = false, bEnableGravity = true, bStartAwake = true, bGyroscopicTorqueEnabled = false;
  bool bStatic = false;                             // replaces Mobility; kinematic = !bStatic && !bSimulatePhysics
  float MassInKgOverride = 100.f, MassScale = 1.f; FVector COMNudge; FVector InertiaTensorScale{1, 1, 1};
  float LinearDamping = 0.01f, AngularDamping = 0.f;
  bool bOverrideMaxAngularVelocity = false;       float MaxAngularVelocity = 3600.f;   // deg/s (project setting)
  bool bOverrideMaxDepenetrationVelocity = false; float MaxDepenetrationVelocity = 0.f;
  bool bUseCCD = false, bInertiaConditioning = true;
  ESleepFamily SleepFamily = ESleepFamily::Normal; float CustomSleepThresholdMultiplier = 1.f, StabilizationThresholdMultiplier = 1.f;
  bool bOverrideIterationCounts = false; uint8 PositionSolverIterationCount = 8, VelocitySolverIterationCount = 2, ProjectionSolverIterationCount = 1;
  FBodyCollisionFilter CollisionFilter; const FPhysicalMaterial* PhysMaterialOverride = nullptr;
  FVector InitialLinearVelocity, InitialAngularVelocityInRadians;   // angular: harness extension (UE sets linear only)
  std::string BodyName;
  // runtime
  const FBodySetup* BodySetup = nullptr; FPhysicsActorHandle ActorHandle; FMassProperties MassProperties;
  FBodyInstanceState State; int32 InstanceBodyIndex = INDEX_NONE;

  void InitBody(const FBodySetup* Setup, const FTransform& Transform, FPhysScene* InRBScene);   // UE signature minus PrimComp
  void TermBody(); void UpdateMassProperties();
  bool IsDynamic() const; bool IsNonKinematic() const; float GetSleepThresholdMultiplier() const;   // Sensitive = 1/20
  FTransform GetUnrealWorldTransform() const; FVector GetUnrealWorldVelocity() const; FVector GetUnrealWorldAngularVelocityInRadians() const;
  float GetBodyMass() const; FVector GetBodyInertiaTensor() const;
  void SetBodyTransform(const FTransform& NewTransform, ETeleportType Teleport, bool bAutoWake = true);   // kinematic + None -> target
  void SetLinearVelocity(const FVector& NewVel, bool bAddToCurrent, bool bAutoWake = true);
  void SetAngularVelocityInRadians(const FVector& NewAngVel, bool bAddToCurrent, bool bAutoWake = true);
  void AddForce(const FVector& Force, bool bAllowSubstepping = true, bool bAccelChange = false);       // deferred per frame
  void AddTorqueInRadians(const FVector& Torque, bool bAllowSubstepping = true, bool bAccelChange = false);
  void AddImpulse(const FVector& Impulse, bool bVelChange);                   // bVelChange -> AddVelocity_AssumesLocked
  void AddAngularImpulseInRadians(const FVector& Impulse, bool bVelChange);  // bVelChange -> AddAngularVelocityInRadians_AssumesLocked
  void WakeInstance(); void PutInstanceToSleep(); bool IsInstanceAwake() const;
  FPhysScene* GetPhysicsScene() const;
};
}
```

### `PhysicsEngine/ConstraintInstance.h` — [MVP M1 타입만, 사용은 확장 M9]

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/ConstraintInstance.h   [MVP M1 types; joints exercised in 확장 M9]
namespace PhysicsTest {
enum class ELinearConstraintMotion : uint8 { LCM_Free, LCM_Limited, LCM_Locked };
enum class EAngularConstraintMotion : uint8 { ACM_Free, ACM_Limited, ACM_Locked };
// UE nesting kept (ConstraintInstance.h:56-115 [V]); Phase-1 subset. Defaults: ConstraintTypes.cpp:43-70 [V].
struct FLinearConstraint { float Limit = 0.f; ELinearConstraintMotion XMotion = ELinearConstraintMotion::LCM_Locked,
                           YMotion = ELinearConstraintMotion::LCM_Locked, ZMotion = ELinearConstraintMotion::LCM_Locked; };
struct FConeConstraint   { float Swing1LimitDegrees = 45.f, Swing2LimitDegrees = 45.f;
                           EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::ACM_Free, Swing2Motion = EAngularConstraintMotion::ACM_Free; };
struct FTwistConstraint  { float TwistLimitDegrees = 45.f; EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::ACM_Free; };
struct FConstraintProfileProperties { FLinearConstraint LinearLimit; FConeConstraint ConeLimit; FTwistConstraint TwistLimit;
                                      bool bDisableCollision = false; };
struct FConstraintInstance {
  std::string JointName;
  FBodyInstance* Body1 = nullptr;                   // child (ConstraintBone1)
  FBodyInstance* Body2 = nullptr;                   // parent; nullptr = world
  FVector Pos1, PriAxis1{1, 0, 0}, SecAxis1{0, 1, 0};
  FVector Pos2, PriAxis2{1, 0, 0}, SecAxis2{0, 1, 0};
  FConstraintProfileProperties ProfileInstance; FPhysicsConstraintHandle ConstraintHandle;
  FTransform GetRefFrame(int32 Frame) const;
};
namespace ConstraintUtils {                         // ConstraintUtils.h:10,19 [V]; ConstraintUtils.cpp:8-27,70-88 [V]
  void ConfigureAsBallAndSocket(FConstraintInstance& ConstraintInstance, bool bOverwriteLimits = true);   // linear locked, angular free
  void ConfigureAsHinge(FConstraintInstance& ConstraintInstance, bool bOverwriteLimits = true);           // linear locked, swings locked, twist free
  bool IsBallAndSocket(const FConstraintInstance& ConstraintInstance);
  bool IsHinge(const FConstraintInstance& ConstraintInstance);
}
}
```

### `PhysicsEngine/PhysicsSettings.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/PhysicsSettings.h   [MVP M1]
// Every UE key, core fields included, lives in [/Script/Engine.PhysicsSettings], as in UE (BaseEngine.ini:3406 [V];
// UPhysicsSettings derives from UPhysicsSettingsCore). The two structs below only mirror UE's class split.
namespace PhysicsTest {
struct FChaosSolverConfiguration { int32 PositionIterations = 8, VelocityIterations = 2, ProjectionIterations = 1;
  float CollisionMarginFraction = 0.05f, CollisionMarginMax = 10.f, CollisionCullDistance = 3.f,
        CollisionMaxPushOutVelocity = 1000.f, CollisionInitialOverlapDepenetrationVelocity = -1.f; };
enum class EChaosThreadingMode : uint8 { DedicatedThread, TaskGraph, SingleThread };   // PhysicsCoreTypes.h:20-27 [V]
struct FChaosPhysicsSettings { EChaosThreadingMode DefaultThreadingModel = EChaosThreadingMode::TaskGraph; };   // PhysicsSettings.cpp:198 [V]; the bench maps T instead (A.16)
struct FPhysicsSettingsCore {
  float DefaultGravityZ = -980.f; int32 SimulateScratchMemorySize = 262144;
  bool bEnableShapeSharing = false, bEnablePCM = true, bEnableStabilization = false, bEnableEnhancedDeterminism = false;
  float BounceThresholdVelocity = 200.f, MaxAngularVelocity = 3600.f, MaxDepenetrationVelocity = 0.f;
  EFrictionCombineMode FrictionCombineMode = EFrictionCombineMode::Average, RestitutionCombineMode = EFrictionCombineMode::Average;
  float ContactOffsetMultiplier = 0.02f, MinContactOffset = 2.f, MaxContactOffset = 8.f;
  FChaosSolverConfiguration SolverOptions; };
struct FPhysicsSettings : FPhysicsSettingsCore {
  float MinPhysicsDeltaTime = UE_SMALL_NUMBER, MaxPhysicsDeltaTime = 1.f / 30.f;
  bool bSubstepping = false; float MaxSubstepDeltaTime = 1.f / 60.f; int32 MaxSubsteps = 6;
  bool bTickPhysicsAsync = false; float AsyncFixedTimeStepSize = 1.f / 30.f; int32 PhysXTreeRebuildRate = 10;
  FChaosPhysicsSettings ChaosSettings; };

enum class EPhysicsTickMode : uint8 { FixedStep, Sync, Substepping, AsyncFixed };
enum class ESubstepClampRule : uint8 { UE5ChaosScene, UE4PhysSubstep };   // owned by FStepPlanner; never reaches a backend
enum class EFrameDriver : uint8 { Auto, Harness, Native };              // Auto: Native if PT_CAP_NATIVE_FRAME_MODEL
enum class EPhysicsRunMode : uint8 { Bench, Behavior, Viz };
enum class EMassRule : uint8 { UEBodyUtils, Physical };
struct FPhysicsTestSettings {                       // [/Script/PhysicsTest.PhysicsTestSettings]
  EPhysicsTickMode TickMode = EPhysicsTickMode::FixedStep; float FixedDeltaTime = 1.f / 60.f;
  ESubstepClampRule SubstepClampRule = ESubstepClampRule::UE5ChaosScene; EFrameDriver FrameDriver = EFrameDriver::Auto;
  EPhysicsRunMode RunMode = EPhysicsRunMode::Bench; uint32 InstrumentFlags = 0; EMassRule MassRule = EMassRule::UEBodyUtils;
  int32 NumThreads = 1; uint64 AffinityMask = 0x1;
  int32 HashEvery = 0;       // 0 = final frame only (headline); > 0 only in non-headline runs
  int32 CountersEvery = 0;   // any PT_INSTR_COUNTERS run is non-headline
  int32 CaptureEvery = 0; };
}
```

### `PhysicsEngine/BodyUtils.h`

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysicsEngine/BodyUtils.h   [MVP M1]
namespace PhysicsTest::BodyUtils {                  // clean-room re-implementation of BodyUtils.cpp:16-128 [V]
// No material -> 1.0 kg/cm^3; otherwise max(9e-8, Density[g/cm^3] * 0.001) (BodyUtils.cpp:37-45 [V]).
double GetBodyInstanceDensity(const FPhysicalMaterial* Material);
// Order (BodyUtils.cpp:53-112 modifiers, :124 diagonalize, BodyInstance.cpp:3490-3492 [V]):
//  1 density  2 volume integration (analytic primitives; convex: 확장 M15)  3 diagonalize (Jacobi)
//  4 mass rule   !bOverrideMass: M' = max(MassScale * M^clamp(RaiseMassToPower, 1e-4, 1), 0.001)
//                 bOverrideMass: M' = max(MassInKgOverride, 0.001)            (EMassRule::Physical: exponent 1)
//  5 inertia *= M'/M      6 COM += COMNudge (actor space)
//  7 InertiaTensorScale via ScaleInertia with bInertiaScaleIncludeMass = false; skipped when ~(1,1,1) within 1e-3
//  8 if p.ComNudgeAffectsInertia (BodyUtils.cpp:17): Ixx += M'*n.x^2, Iyy += M'*n.y^2, Izz += M'*n.z^2 exactly as UE
//    writes it (after step 7, per the source comment), then back to principal axes
FMassProperties ComputeMassProperties(const FBodyInstance& Body, const FBodySetup& Setup, const FPhysicalMaterial* Material, EMassRule Rule);
void ApplyMassPropertiesModifiers(FMassProperties& InOut, const FBodyInstance& Body, const FPhysicalMaterial* Material, EMassRule Rule);
// Unit tests: 100 cm cube 177.83 kg / 296,380 kg*cm^2; 50 cm 37.384 kg; 10 cm 1 kg;
//   non-unit InertiaTensorScale combined with a nonzero COMNudge (order of steps 6-8).
}
```

---

## A.7 `StepPlanner.h`, `PhysSceneStats.h` — [MVP M1]

- 모든 tick mode를 M1에서 구현하고 단위 테스트로 고정한다 (S6). MVP 씬은 FixedStep과 Sync만 쓴다.
- Native driver의 실제 검증은 Chaos가 들어오는 확장 M13이다. 그 전에 Null 백엔드가 같은 의미로 NATIVE_FRAME_MODEL을 선언해 M6의 프레임 모델 게이트 A10에서 계약을 확인한다 (A.13).

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/StepPlanner.h   [MVP M1: all modes; MVP scenes use FixedStep and Sync]
namespace PhysicsTest {
// StepDeltaTime is the float the engines receive. Analytic references use dt_engine = (double)StepDeltaTime and
// the +-1e-9 tolerances apply only against that value.
struct FStepPlan { int32 NumSteps = 0; float StepDeltaTime = 0.f; float ConsumedTime = 0.f; float LostTime = 0.f; };
class FStepPlanner {                                // UE float arithmetic replicated; pinned by unit tests
public:
  // FixedStep  : N = 1, FixedDeltaTime; the frame dt is ignored.
  // Sync       : N = 1, min(dt, MaxPhysicsDeltaTime) (ChaosScene.cpp:331-361 [V]).
  // Substepping: UE5ChaosScene (PhysicsSolverBase.cpp:581-621 [V]) or UE4PhysSubstep (PhysSubstepTasks.cpp:211-227 [V],
  //              dead code in 5.8.3; UseDelta pre-clamp [M]). dt 0.05 -> UE5 3 x 1/60, UE4 2 x 1/60; dt 0.2 UE5 -> 6 x 1/60, 0.1 s lost.
  // AsyncFixed : acc += min(dt, MaxPhysicsDeltaTime); N = floor(acc / AsyncDt), may be 0. SDK backends step exactly N.
  //              Chaos also interpolates GT state with one-step latency: fixed-dt stepping requires
  //              p.UseAsyncInterpolation=1, and setting it to 0 also disables the accumulator
  //              (PhysicsSolverBase.h:717-720, .cpp:262-263,591-605 [V]). Chaos AsyncFixed runs are labelled and
  //              excluded from per-body behaviour comparison.
  // Remainders below MinDeltaTime are dropped, not carried (PBDRigidsSolver.cpp:554-568 [V]); Min >= UE_SMALL_NUMBER.
  static FStepPlan PlanFrame(float FrameDeltaTime, const FPhysicsSettings& Settings, const FPhysicsTestSettings& TestSettings,
                             double& InOutAccumulator);
};
}
```

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysSceneStats.h   [MVP M1]
namespace PhysicsTest {
struct FFrameTiming {                               // [MVP] FPlatformTime (QPC) ticks -> ns; [확장 M10] FCycleCounter (TSC) per bucket
  uint64 PushNs = 0, SetupNs = 0, StartNs = 0, WaitNs = 0, EndNs = 0, StepNs = 0, PullNs = 0, FrameNs = 0, FrameQpcTicks = 0;
  uint32 NumSteps = 0, EngineErrorBits = 0; };      // T_frame = sum(T_push) + sum(T_step) + T_pull
struct FPhysSceneStats { int32 NumBodies = 0, NumDynamicBodies = 0, NumKinematicBodies = 0, NumStaticBodies = 0, NumAwakeBodies = 0;
                         int32 NumIslands = -1, NumManifoldPoints = -1, NumActiveManifoldPoints = -1, NumConstraints = 0;  // stat ChaosCounters names
                         FFrameTiming Last; double FrameMsMin = 0, FrameMsAvg = 0, FrameMsP95 = 0, FrameMsMax = 0; };
}
```

---

## A.8 호스트 측 백엔드 연결 — [MVP M2]

- 호스트는 자기가 빌드된 ABI major를 요청한다. v0.9 동안은 0이다.
- PhysX 공존 규칙: tolerance scale이 같은 변형(UE4Compat, -TGS, -Stab)은 한 프로세스에서 별도 PxScene으로 함께 돌 수 있다. 뷰어 side-by-side(뷰어 2단계, 확장 M12)가 이 경우다. SdkDefault만 별도 프로세스가 필요하다.
- 레지스트리가 거부하는 조합은 두 가지뿐이다: InitGamePhys 옵션이 충돌하는 조합(PhysX tolerance scale)과 전역 combine 설정이 다른 Bullet 씬 조합 ([02_Architecture.md](../02_Architecture.md) §6).
- `FPhysicsInterface`는 5.8.3의 `FChaosEngineInterface` 동사 이름과 인자를 따른다. 임펄스와 속도 변화를 나눴다.

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysicsBackend.h   [MVP M2]
namespace PhysicsTest {
struct FBackendInfo { std::string Name, DisplayName, EngineVersion, SourceCommit, BuildConfig, BuildFlagsJson;
                      uint64 Capabilities = 0; uint32 ModuleFlags = 0; uint32 ProvisionalRevision = 0;
                      std::vector<std::string> RequiredEffectiveKeys;   // backend set; A7 adds the generic set (A.10)
                      bool Has(uint64 Cap) const; };                    // PROVISIONAL(M9) caps read as absent on a revision mismatch
class FPhysicsBackendModule {                       // one loaded module; never unloaded
public:
  // Calls PT_GetBackendApi(PT_ABI_VERSION_MAJOR): the host requests the major it was built with (0 until the M6 freeze).
  // DLL: LoadLibraryExW(abs, LOAD_WITH_ALTERED_SEARCH_PATH) after affinity is set; re-installs the host exception filter.
  const PtBackendApi& GetApi() const; const FBackendInfo& GetInfo() const; bool IsDll() const;
  PtResult InitGamePhys(uint32 MaxThreads, const std::filesystem::path& SavedDir, std::span<const PtKeyValue> InitOptions, uint32 Flags);
  void TermGamePhysAtExit(); std::string GetLastErrorMessage() const;
};
class FPhysicsBackendRegistry {                     // ~ FModuleManager; implemented in PT::BackendRegistry (generated list)
public:
  static FPhysicsBackendRegistry& Get();
  std::vector<std::string> GetAvailableNames() const;       // generated static list + Chaos if the DLL exists
  // Process rules: at most one UE_RUNTIME module. A GLOBAL_SCENE_SETTINGS module accepts one set of process-wide init
  // options: PhysX UE4Compat, -TGS and -Stab share PxTolerancesScale(100,1000) and may run as several scenes in one
  // process (viewer side by side, M12); SdkDefault (1,10) next to them is refused with OutError. Bullet scenes with
  // different global combine settings are refused as well.
  FPhysicsBackendModule* FindOrLoad(std::string_view Name, std::span<const PtKeyValue> InitOptions, std::string& OutError);
  static bool IsChaosAvailable(std::string& OutReason);
};
class FPhysicsBackendScene {                        // host proxy over one PtSceneId; inline hot path, no allocation
public:
  FPhysicsBackendScene(FPhysicsBackendModule& Module, const PtSceneDesc& Desc);
  ~FPhysicsBackendScene();
  void CreateMaterials(std::span<FPhysicalMaterial* const> Materials);
  void CreateActors(std::span<FBodyInstance* const> Bodies, std::span<FPhysicsActorHandle> OutHandles);   // FK*Elem -> PtShapeElem (box halves here)
  void CreateConstraints(std::span<FConstraintInstance* const> Constraints);                            // PROVISIONAL(M9)
  std::string PrepareForSimulation();
  PtResult SetUpForFrame(const FVector* NewGrav, float Dt, float MinDt, float MaxDt, float MaxSubDt, int32 MaxSubsteps, bool bSubstepping);
  PtResult StartFrame(); PtResult WaitPhysScenes(); PtResult EndFrame(PtFrameResult& Out);
  void EnqueueCommand(const PtBodyCommand& Cmd); PtResult FlushCommands();     // T_push
  PtResult ReadBodyStates(uint32 ReadFlags, std::span<uint32> OutIdx, std::span<PtBodyState> Out, uint32& OutCount);   // T_pull
  PtResult ReadMassProperties(uint32 First, std::span<PtMassProperties> Out);
  PtResult GetStepStats(PtStepStats& Out); uint64 ComputeNativeStateHash();
  std::string DumpEffectiveSettings(); PtResult SetOption(std::string_view Key, std::string_view Value);
  PtResult CaptureDebugSnapshot(uint32 SnapFlags, uint32 MaxPoints, struct FPhysicsDebugSnapshot& Out);   // PROVISIONAL(M9)
  const FBackendInfo& GetInfo() const; PtSceneId GetSceneId() const;
};
}
```

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysicsInterface.h   [MVP M2]
namespace PhysicsTest {
// ~ FPhysInterface_Chaos static facade; verbs and parameter names as FChaosEngineInterface (ChaosEngineInterface.h:358-471 [V]).
// Writes are buffered in the handle's scene proxy and flushed at T_push; reads flush first.
// bIsInternal is kept for signature parity; the harness has no physics-thread side, so only false is accepted (checkf).
class FPhysicsInterface {
public:
  static void SetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewPose, bool bAutoWake = true);
  static void SetKinematicTarget_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FTransform& InNewTarget);
  static void SetLinearVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);
  static void SetAngularVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InNewVelocity, bool bAutoWake = true);
  static void AddForce_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Force, bool bAllowSubstepping, bool bAccelChange, bool bIsInternal = false);
  static void AddTorque_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange, bool bIsInternal = false);
  static void AddImpulse_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce, bool bIsInternal = false);
  static void AddAngularImpulseInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque, bool bIsInternal = false);
  static void AddVelocity_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InForce, bool bIsInternal = false);       // UE names it InForce
  static void AddAngularVelocityInRadians_AssumesLocked(const FPhysicsActorHandle& InActorReference, const FVector& InTorque, bool bIsInternal = false);
  static void WakeUp_AssumesLocked(const FPhysicsActorHandle& InActorReference);
  static void PutToSleep_AssumesLocked(const FPhysicsActorHandle& InActorReference);
  static FTransform GetGlobalPose_AssumesLocked(const FPhysicsActorHandle& InActorReference);
  template <class F> static bool ExecuteRead(const FPhysicsActorHandle& InHandle, F&& InFunc);    // lock-free pass-through
  template <class F> static bool ExecuteWrite(const FPhysicsActorHandle& InHandle, F&& InFunc);
};
}
```

---

## A.9 `PhysScene.h` (유일한 씬) — [MVP M1 골격, M2 ABI 연결]

- `FlushDeferredActors`가 백엔드에 한 배치를 넘긴다. 백엔드 안의 순서는 A.1의 `CreateActors` 주석(UE 순서)을 따른다.
- 게이트 씬 `S08.StartAsleep`이 `bStartAwake = false`인 바디를 만들고 frame 0에서 `ObjectState == Sleeping`인지 확인한다 (G6, M6). sleep을 끈 프로필(Normalized)에서는 모든 엔진이 putToSleep 단계를 건너뛰므로 이 씬은 UEParity에서만 돈다.
- `desc_sha256`과 `options_sha256`을 나눴다. 한 시나리오를 두 백엔드 × 두 변형으로 만들었을 때 `desc_sha256`이 같아야 한다 (단위 테스트).

```cpp
// File: Source/Runtime/PhysicsCore/Public/PhysScene.h   [MVP M1 skeleton, M2 ABI wiring, M3 hashes]
namespace PhysicsTest {
class FDebugDrawQueue; struct FPhysicsDebugSnapshot; struct FResolvedPhysicsConfig;
struct FPhysSceneInitParams { std::string DebugName, BackendName; const FResolvedPhysicsConfig* Config = nullptr;
                              uint32 MaxBodiesHint = 0, MaxBodyPairsHint = 0, MaxContactConstraintsHint = 0; };   // -> PtSceneDesc (M2 fix round 1)
using FOnPhysScenePreTick  = std::function<void(FPhysScene*, float)>;
using FOnPhysSceneStep     = std::function<void(FPhysScene*, float)>;   // once per frame (UE5 OnStartFrame); UE4PhysSubstep: per substep
using FOnPhysScenePostTick = std::function<void(FPhysScene*, float)>;
class FPhysScene {                                  // UWorld physics part + FPhysScene_Chaos + host-side FChaosScene, folded
public:
  explicit FPhysScene(const FPhysSceneInitParams& Params);   // registry -> module -> CreateScene
  ~FPhysScene(); FPhysScene(const FPhysScene&) = delete; FPhysScene& operator=(const FPhysScene&) = delete;
  // assets (stable addresses)
  FPhysicalMaterial* CreatePhysicalMaterial(const FPhysicalMaterial& Template);
  FBodySetup* CreateBodySetup(FBodySetup&& Setup);
  // bodies and constraints (~ component owning FBodyInstance + FInitBodiesHelper)
  FBodyInstance* SpawnBody(const FBodyInstance& Template, const FBodySetup* Setup, const FTransform& Transform);
  FConstraintInstance* SpawnConstraint(const FConstraintInstance& Template, FBodyInstance* Body1, FBodyInstance* Body2);   // M9
  void DeferAddActor(FBodyInstance* Body);
  // CreateMaterials -> CreateActors (one batch in creation order; inside the backend: CreateActor ... ONE AddActorsToScene
  // ... InitDynamicProperties ... putToSleep for !bStartAwake unless sleeping is off, UE order) -> CreateConstraints.
  // [T_create, recorded]
  void FlushDeferredActors();
  void PrepareForSimulation();                      // the same untimed opportunity for every engine [T_prepare, recorded]
  // frame (~ UWorld physics tick + FChaosScene)
  void Tick(float DeltaSeconds);
  void SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds, float InMinPhysicsDeltaTime, float InMaxPhysicsDeltaTime,
                     float InMaxSubstepDeltaTime, int32 InMaxSubsteps, bool bSubstepping);
  void StartFrame(); void WaitPhysScenes(); void EndFrame(); bool IsSimulating() const;
  // per-frame commands: the UE5 FPhysScene_Chaos verbs (PhysScene_Chaos.h:232-238); FPhysicsInterface's forces, torques
  // and kinematic targets end here, and each driver applies the frame rule (02 9.1). (M2 drafts: EnqueueForce /
  // EnqueueKinematicTarget, renamed in M2 fix round 1, docs/Progress.md M2 section 7.)
  void AddForce_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, bool bAllowSubstepping, bool bAccelChange);
  void AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange);
  void SetKinematicTarget_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& TargetTM, bool bAllowSubstepping);
  void TeleportKinematic_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& Pose);   // harness: SetGlobalPose of a kinematic
  void MarkDynamicsClean_AssumesLocked(const FBodyInstance* BodyInstance);   // harness: PutToSleep drops the held forces
  void MarkDynamicsDirty_AssumesLocked(const FBodyInstance* BodyInstance);   // harness: a later force / impulse restores them
  FOnPhysScenePreTick OnPhysScenePreTick; FOnPhysSceneStep OnPhysSceneStep; FOnPhysScenePostTick OnPhysScenePostTick;
  // queries
  std::span<FBodyInstance* const> GetBodies() const; std::span<FConstraintInstance* const> GetConstraints() const;
  const FPhysicsSettings& GetSettings() const; const FPhysicsTestSettings& GetTestSettings() const;
  FVector GetGravity() const; double GetSimTime() const; uint64 GetFrameCount() const; uint64 GetStepCount() const;
  const FPhysSceneStats& GetStats() const; const FFrameTiming& GetLastFrameTiming() const; const FStepPlan& GetLastPlan() const;
  void SetCaptureDebugSnapshot(bool bCapture); const FPhysicsDebugSnapshot* GetDebugSnapshot() const; FDebugDrawQueue& GetDebugDrawQueue();
  FPhysicsBackendScene& GetBackendScene(); const FBackendInfo& GetBackendInfo() const;
  // desc_sha256: engine-independent inputs only - materials, shapes, actor create infos, convex vertices, constraints,
  // gravity, dt sequence. Equal for one scenario across backends and variants (unit test: 2 backends x 2 variants).
  std::string ComputeDescSha256() const;
  // options_sha256: RunMode, InstrumentFlags, SimFlags, NumThreads, AsyncFixedTimeStepSize, RestitutionThreshold,
  // MaxLinearVelocity, iterations, hints, scene Options (may differ per engine).
  std::string ComputeOptionsSha256() const;
private:
  struct FImpl; std::unique_ptr<FImpl> Impl;
};
}
```

---

## A.10 `PhysicsProfile.h` — [MVP M1 최소, 완성은 확장 M9]

- `BasePhysicsTest.ini`는 UE 기본값을 모두 `[/Script/Engine.PhysicsSettings]`에 둔다. UE가 그렇게 저장하기 때문이다 (BaseEngine.ini:3406 [V]).
- 필수 effective 키를 "일반 집합 + 백엔드 집합"으로 나눴다. 키 목록은 [02_Architecture.md](../02_Architecture.md) §10과 같다. 일반 집합은 모든 백엔드가 내며, 개념이 없는 백엔드는 `native="n/a"`, Source=COMPILE_TIME으로 낸다 (Null도 그렇게 채운다). 백엔드 집합은 `GetInfo`의 `RequiredEffectiveKeys`로 선언한다.

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysicsProfile.h   [MVP M1 minimal (layers, origins); 확장 M9 complete]
namespace PhysicsTest {
enum class EConfigOrigin : uint8 { UEDefault, PhysicsProfile, BackendVariant, ScenePinned, CommandLine, ForcedOverride };
enum class EProfileKeyPolicy : uint8 { Pinned, EngineDefault };   // whether a variant may set the key
struct FConfigValue { std::string Value, Unit; EConfigOrigin Origin = EConfigOrigin::UEDefault; std::string OriginName; };
struct FPhysicsProfile {                            // Config/Profiles/<Name>.ini (UEParity, Normalized)
  std::string Name; int32 Version = 1; FConfigFile Ini; std::map<std::string, EProfileKeyPolicy> KeyPolicy;
  static FPhysicsProfile LoadIni(const std::filesystem::path& Path); std::string Sha256() const; };   // -> physics_sha256
struct FBackendVariant {                            // Config/Profiles/Backend/<Engine>.<Variant>.ini
  std::string Engine, Name, Label;                  // e.g. "UE4 settings on PhysX 5.11"
  FConfigFile Ini;                                  // [PhysicsTest.Backend.<Engine>] (+ [ConsoleVariables]); keys carry [V]/[M]/[R] tags
  std::vector<std::string> FidelityNotes;
  static FBackendVariant LoadIni(const std::filesystem::path& Path); std::string Sha256() const; };   // -> variant_sha256
struct FResolvedPhysicsConfig {
  std::map<std::string, FConfigValue> Values;       // canonical dotted keys, e.g. "BodyInstanceDefaults.LinearDamping"
  FPhysicsSettings Settings; FPhysicsTestSettings TestSettings;
  // InitOptions: process-wide (PhysX tolerances scale); SceneOptions: per scene (solver, stabilization, ...); -> PtKeyValue
  std::vector<std::pair<std::string, std::string>> InitOptions, SceneOptions, ConsoleVariables;
  std::string Sha256() const; };
class FProfileResolver {
public:
  // Order: Base < physics profile < backend variant (own engine keys + EngineDefault keys only) < scene-pinned < CLI.
  // Base = Config/BasePhysicsTest.ini: every UE default under [/Script/Engine.PhysicsSettings] (BaseEngine.ini:3406 [V]);
  // harness keys under [/Script/PhysicsTest.*].
  static bool Resolve(const FConfigFile& Base, const FPhysicsProfile& Profile, const FBackendVariant& Variant,
                      std::string_view CommandLineIniOverrides, std::string_view ExecCmds,
                      FResolvedPhysicsConfig& Out, std::string& OutError);
  static void ApplyBodyInstanceDefaults(const FResolvedPhysicsConfig& Config, FBodyInstance& InOutTemplate);       // before authoring
  static void ApplyMaterialDefaults(const FResolvedPhysicsConfig& Config, FPhysicalMaterial& InOutTemplate);
  static void ApplyForcedOverrides(const FResolvedPhysicsConfig& Config, FPhysScene& Scene, std::vector<std::string>& OutLog);   // after authoring [확장 M9]
};
struct FEffectiveSetting { std::string Key, Native, NativeUnit, Unit, Source; double Canonical = 0; std::optional<double> Expected; bool bOk = true; };
std::vector<FEffectiveSetting> ParseEffectiveSettings(std::string_view JsonLines);
// Required effective keys = generic set + FBackendInfo::RequiredEffectiveKeys; A7 validates both (02 §10).
// Generic (every backend; a concept the backend lacks is native="n/a", Source=COMPILE_TIME, e.g. Null broadphase.type):
//   world.gravity, solver.type, solver.iterations, broadphase.type, contact.restitution_threshold, contact.offset_rule,
//   contact.speculative_distance, material.friction_combine, material.restitution_combine, material.static_friction_rule,
//   body.damping_formula, body.damping, body.max_angular_velocity, body.max_linear_velocity, body.gyroscopic,
//   sleep.enabled, sleep.criteria, ccd.enabled, threads.workers, determinism.flag, shape.box_margin (per shape, e.g.
//   Bullet getMargin()), shape.convex_radius, shape.wrapping, listeners.installed.
// Backend sets: PhysX physx.pcm / stabilization / scratch / tolerances / active_actors / filter_shader / dispatcher;
//   Jolt jolt.config_string / temp_allocator_bytes; Bullet bullet.max_angvel_compile_time / debug_drawer_attached;
//   Chaos threads.single_worker / threads.max_num_workers / chaos.threading_mode / chaos.async_dt /
//   chaos.use_async_interpolation.
std::vector<std::string> GetRequiredEffectiveKeys(const FBackendInfo& Info);
}
```

---

## A.11 디버그 스냅샷·드로·렌더러 — [MVP M4, 확장 M12]

- `FDebugDrawQueue`는 Engine `DrawDebugHelpers`가 아니라 **`Chaos::FDebugDrawQueue`**를 미러링한다. 인자 순서와 기본값을 5.8.3 원본(DebugDrawQueue.h:103-250 [V])에 맞췄다.
- 드로 순서는 `FPBDRigidsSolver::PostTickDebugDraw`(PBDRigidsSolver.cpp:3417-3546 [V])를 따른다. 섬 드로어 이름은 UE와 같은 `DrawConstraintGraph`다.
- `IDebugDrawRenderer`의 메서드 순서와 인자 순서는 ChaosDDRenderer.h:19-44와 이미 대조했다 **[V]**. 나중에 다시 확인할 작업은 없다.
- 뷰어 성능 기준은 시뮬레이션을 멈추거나 리플레이할 때 60 fps 이상, 씬당 스냅샷+드로 CPU 4 ms 이하다. 시뮬레이션이 도는 동안의 fps는 보고만 하고 게이트로 쓰지 않는다. 마일스톤별 완료 기준은 [03_WorkPlan.md](../03_WorkPlan.md)를 따른다.
- SideBySide, GhostOverlay, Replay, Profiler는 뷰어 2단계(확장 M12)다. 뷰어 1단계(M4)는 씬 하나를 돌리고 엔진을 바꾸면 씬을 다시 만든다. PhysX UE4Compat / -TGS / -Stab은 tolerance scale이 같으므로 한 프로세스에서 나란히 띄울 수 있다 (A.8).

```cpp
// File: Source/Runtime/PhysicsCore/Public/Physics/PhysicsDebugSnapshot.h   [MVP M4; the ABI structs it holds are PROVISIONAL(M9)]
namespace PhysicsTest {
struct FPhysicsDebugSnapshot {                      // ABI structs held as-is (cm, Chaos conventions); captured untimed
  uint64 FrameIndex = 0; double SimTime = 0; uint64 Capabilities = 0; bool bTruncated = false;
  std::vector<PtParticleSnapshot> Particles; std::vector<PtContactPoint> Contacts; std::vector<PtJointSnapshot> Joints;
  std::vector<PtDebugDrawCmd> NativeCommands; std::string StringPool;
  std::vector<int32> HarnessIslandIndex;            // FIslandBuilder output, per body
  void Reset(); };
}
```

```cpp
// File: Source/Runtime/PhysicsDebugDraw/Public/IslandBuilder.h   [MVP M4]
namespace PhysicsTest {
// Union-find over Phi <= 0 contacts + joints, statics excluded, id = lowest body index, deterministic.
// Used for every engine (Jolt has no public island API).
class FIslandBuilder { public: static void Build(FPhysicsDebugSnapshot& InOut, std::span<FConstraintInstance* const> Joints); };
}
```

```cpp
// File: Source/Runtime/PhysicsDebugDraw/Public/DebugDrawSettings.h   [MVP M4]
namespace PhysicsTest {
struct FDebugDrawSettings {                          // solver defaults (PBDRigidsSolver.cpp:219-247 [V])
  float ArrowSize = 10, BodyAxisLen = 30, ContactLen = 30, ContactWidth = 6, ContactInfoWidth = 6, ConstraintAxisLen = 30, JointComSize = 2;
  float LineThickness = 1.f, PointSize = 5.f, DrawScale = 1, FontHeight = 10, FontScale = 1.5f;
  float VelScale = 0, AngVelScale = 0, ImpulseScale = 0, PushOutScale = 0, InertiaScale = 1; uint8 DrawPriority = 10; };
// Bound to p.Chaos.DebugDraw.Enabled / .Radius / .MaxLines / .SeeThrough / .ShowInactiveContacts and
// p.Chaos.Solver.DebugDrawShapes / Collisions / Mass / Bounds / Transforms / Islands / SleepState / CollidingShapes / Joints.
struct FDebugDrawFlags {
  bool bEnabled = true, bDrawShapes = true, bDrawCollisions = false, bDrawMass = false, bDrawBounds = false, bDrawTransforms = false;
  bool bDrawIslands = false, bDrawSleepState = false, bDrawCollidingShapes = false, bDrawJoints = true;
  bool bShowInactiveContacts = true, bSeeThrough = true;
  int32 MaxLines = 20000; float Radius = 3000.f; FColor EngineTint = FColor::White; };
}
```

```cpp
// File: Source/Runtime/PhysicsDebugDraw/Public/DebugDrawQueue.h   [MVP M4]
// Mirrors Chaos::FDebugDrawQueue / FLatentDrawCommand (Chaos/DebugDrawQueue.h:103-250, Chaos/DebugDrawCommand.h:16-53 [V]),
// not Engine DrawDebugHelpers. One queue per FPhysScene (Chaos: a process singleton, GetInstance()).
namespace PhysicsTest {
struct FLatentDrawCommand {
  FVector LineStart, LineEnd; FColor Color; int32 Segments = 0; bool bPersistentLines = false; float ArrowSize = 0, LifeTime = 0;
  uint8 DepthPriority = 0; float Thickness = 0; double Radius = 0, HalfHeight = 0; FVector Center, Extent; FQuat Rotation;
  FVector TextLocation; std::string Text; bool bDrawShadow = false; float FontScale = 1, Duration = 0;
  FMatrix TransformMatrix; bool bDrawAxis = false; FVector YAxis, ZAxis;
  FVector TriangleC;                                 // harness extension (RenderTriangle passthrough)
  enum class EDrawType : uint8 { Point, Line, DirectionalArrow, Sphere, Box, String, Circle, Capsule,
                                 Triangle /* harness extension */ } Type = EDrawType::Point;
};
class FDebugDrawQueue {
public:
  void ExtractAllElements(std::vector<FLatentDrawCommand>& OutDrawCommands, bool bPaused = false);
  void DrawDebugPoint(const FVector& Position, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness);   // no Size: Thickness is the point size
  void DrawDebugLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f);
  void DrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f);
  void DrawDebugCoordinateSystem(const FVector& Position, const FRotator& AxisRot, double Scale, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f);
  void DrawDebugSphere(const FVector& Center, double Radius, int32 Segments, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f);
  void DrawDebugBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness);   // Extent = half extent
  void DrawDebugString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float Duration, bool bDrawShadow, float FontScale);   // UE also takes AActor* TestBaseActor
  void DrawDebugCircle(const FVector& Center, double Radius, int32 Segments, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness,
                       const FVector& YAxis, const FVector& ZAxis, bool bDrawAxis);
  void DrawDebugCapsule(const FVector& Center, double HalfHeight, double Radius, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness);   // HalfHeight includes Radius
  // harness extensions
  void DrawDebugTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LifeTime, float Thickness);
  void Enqueue(const FLatentDrawCommand& Command);   // native PtDebugDrawCmd passthrough
  void SetEnabled(bool bInEnabled); void SetMaxCost(int32 InMaxCost); void SetRegionOfInterest(const FVector& Pos, float InRadius);
  FVector GetCenterOfInterest() const; double GetRadiusOfInterest() const; bool IsInRegionOfInterest(const FVector& Pos, double Radius) const;
  void Reset();
};
}
```

```cpp
// File: Source/Runtime/PhysicsDebugDraw/Public/PhysicsDebugDraw.h   [MVP M4]   ~ Chaos::DebugDraw colour tables and drawers
namespace PhysicsTest::DebugDraw {
FColor GetShapeColorByState(EObjectStateType State); FColor GetBoundsColorByState(EObjectStateType State);
FColor GetIndexColor(int32 Index); FColor GetIslandColor(int32 IslandIndex, bool bIsAwake);
struct FDrawContext { const FPhysScene& Scene; const FPhysicsDebugSnapshot& Snapshot; const FDebugDrawSettings& Settings; const FDebugDrawFlags& Flags; };
void DrawParticleShapes(FDebugDrawQueue& Queue, const FDrawContext& Context);       // ~ FPBDRigidsSolver::DebugDrawShapes
void DrawCollisions(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawParticleMass(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawParticleBounds(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawParticleTransforms(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawConstraintGraph(FDebugDrawQueue& Queue, const FDrawContext& Context);      // islands (p.Chaos.Solver.DebugDrawIslands)
void DrawSleepState(FDebugDrawQueue& Queue, const FDrawContext& Context);           // ~ FPBDIslandManager::DebugDrawSleepState
void DrawCollidingShapes(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawJointConstraints(FDebugDrawQueue& Queue, const FDrawContext& Context);
void DrawNativeCommands(FDebugDrawQueue& Queue, const FDrawContext& Context);       // harness: PtDebugDrawCmd passthrough
// Shapes -> Collisions -> Mass -> Bounds -> Transforms -> ConstraintGraph -> SleepState -> CollidingShapes -> Joints
// (FPBDRigidsSolver::PostTickDebugDraw, PBDRigidsSolver.cpp:3417-3546 [V]), then native commands.
void DrawAll(FDebugDrawQueue& Queue, const FDrawContext& Context);
}
```

```cpp
// File: Source/Runtime/PhysicsDebugDraw/Public/DebugDrawRenderer.h   [MVP M4]
namespace PhysicsTest {
// Mirrors ChaosDD::Private::IChaosDDRenderer (ChaosDDRenderer.h:19-44 [V]; method and parameter order verified).
class IDebugDrawRenderer {
public:
  virtual ~IDebugDrawRenderer() = default;
  virtual bool IsServer() const = 0;
  virtual FSphere GetDrawRegion() const = 0;
  virtual void RenderPoint(const FVector& Position, const FColor& Color, float PointSize, float Lifetime) = 0;
  virtual void RenderLine(const FVector& A, const FVector& B, const FColor& Color, float LineThickness, float Lifetime) = 0;
  virtual void RenderArrow(const FVector& A, const FVector& B, float ArrowSize, const FColor& Color, float LineThickness, float Lifetime) = 0;
  virtual void RenderCircle(const FVector& Center, const FMatrix& Axes, float Radius, const FColor& Color, float LineThickness, float Lifetime) = 0;
  virtual void RenderSphere(const FVector& Center, float Radius, const FColor& Color, float LineThickness, float Lifetime) = 0;
  // HalfHeight includes the radius: Chaos passes 0.5*Height + Radius (DebugDrawImplicitObject.cpp:62 [V]); ABI HalfLength + Radius.
  virtual void RenderCapsule(const FVector& Center, const FQuat& Rotation, float HalfHeight, float Radius, const FColor& Color, float LineThickness, float Lifetime) = 0;
  // Size is a half extent: Chaos passes 0.5 * Extents (DebugDrawImplicitObject.cpp:73-76 [V]).
  virtual void RenderBox(const FVector& Position, const FQuat& Rotation, const FVector& Size, const FColor& Color, float LineThickness, float Lifetime) = 0;
  virtual void RenderTriangle(const FVector& A, const FVector& B, const FVector& C, const FColor& Color, float LineThickness, float Lifetime) = 0;
  virtual void RenderString(const FVector& TextLocation, std::string_view Text, const FColor& Color, float FontScale, bool bDrawShadow, float Lifetime) = 0;
};   // RenderLatentCommand exists only on the UE side (the Chaos bridge serializes it into PtDebugDrawCmd)
void VisitCommands(std::span<const FLatentDrawCommand> Commands, IDebugDrawRenderer& Renderer);   // ~ FChaosDDScene::RenderLatestFrames
}
```

```cpp
// File: Source/Runtime/D3D11DebugRenderer/Public/D3D11DebugDrawRenderer.h   [MVP M4]
namespace PhysicsTest {
// Unit wires 12 / 128 / 68 lines (box / sphere 8x8 / capsule 16 sides), VS-expanded world-width quads, SeeThrough pass
// (CF_Always then CF_DepthNearOrEqual), non-black sRGB clear (40,42,46). The bench exe never creates a D3D device.
class FD3D11DebugDrawRenderer final : public IDebugDrawRenderer {
public:
  explicit FD3D11DebugDrawRenderer(class FD3D11RHI& RHI);
  void BeginView(const struct FSceneView& View);
  void DrawGrid(double Cell = 100, int32 MajorEvery = 10, double HalfExtent = 5000);
  void Flush(bool bSeeThrough);
  /* + IDebugDrawRenderer overrides */
};
}
```

---

## A.12 시나리오, BenchCore, 게이트, 벤치 러너 — [MVP M3 / M6, 확장 M7 / M10]

- `PT::BenchCore`를 새로 둔다. 타깃 의존은 `PT::Conformance → PT::BenchCore`, `PT::Benchmark → PT::Conformance`이며 순환이 없다.
- BenchCore는 단계적으로 채운다 ([03_WorkPlan.md](../03_WorkPlan.md) M3, M7, M10): M3에 QPC 클럭 버킷, 확장 M7에 `FHostEnvironment`와 프로세스 스포너, 확장 M10에 `FThreadCensus`와 TSC.
- 헤드라인 run은 해시를 마지막 프레임에만 계산하고 카운터를 끈다 (`HashEvery = 0`, `CountersEvery = 0`).
- UE_RUNTIME 모듈(Chaos)을 쓴 run은 모든 출력과 `exit.json`을 쓰고 flush한 뒤 `TerminateProcess`로 끝낸다. 러너는 프로세스 종료 코드보다 `exit.json`을 믿는다 (확장 M7, 러너 쪽은 확장 M8).
- 게이트 단계: A0~A7, A9는 M2, A10과 G1~G7, G9, G12는 M6 [MVP]. G0 교차검증, G8(DET1), G10a, G11은 확장 M7, G10b는 확장 M10이다.
- 적합성 스탬프 키와 comparability key는 확장 M10이다.

```cpp
// File: Source/Runtime/PhysicsTestScenarios/Public/PhysicsTestScenario.h   [MVP M3 (S00, M1.SingleBoxDrop, M3.BoxPile); gate scenes M6]
namespace PhysicsTest {
enum class EScenarioKind : uint8 { Gate, Overhead, Behavior, Perf, Extended };
struct FStepWindow { std::string Name; uint32 First, Last; };
class IPhysicsTestScenario {
public:
  virtual ~IPhysicsTestScenario() = default;
  // Catalog ID.Name, e.g. "M3.BoxPile", "M4.InclineFriction". P0.EmptyStep, P1.SmallScene, M7.UEDefaults and
  // S00/S00d/S01..S08 (S08.StartAsleep: G6, UEParity only) extend the methodology catalog; documented in docs/Scenes.md.
  virtual const char* GetName() const = 0;
  virtual int32 GetVersion() const = 0;
  virtual EScenarioKind GetKind() const = 0;
  virtual bool IsChaotic() const = 0;
  virtual std::vector<FStepWindow> GetWindows(const std::map<std::string, std::string>& Params) const = 0;
  virtual void BuildScene(FPhysScene& Scene, const std::map<std::string, std::string>& Params, FRandomStream& Random) = 0;   // ground = static box, index 0
  virtual void OnPreTick(FPhysScene& /*Scene*/, float /*DeltaSeconds*/) {}
  virtual uint64 GetRequiredCapabilities() const { return 0; }
};
#define IMPLEMENT_PHYSICS_TEST_SCENARIO(Class, PrettyName) /* static registrar; the scenario lib links WHOLE_ARCHIVE */
}
```

```cpp
// File: Source/Runtime/PhysicsTestBenchCore/Public/BenchCore.h   (PT::BenchCore)
//   [MVP M3 FClockBuckets; 확장 M7 FHostEnvironment, FProcessSpawner; 확장 M10 FThreadCensus, TSC]
namespace PhysicsTest {
struct FClockBuckets {                              // [MVP M3] QPC per bucket; [확장 M10] FCycleCounter (TSC) + QPC calibration
  uint64 Begin(); uint64 End(uint64 Begin) const; double TicksToNs(uint64 Ticks) const; };
class FHostEnvironment {                            // [확장 M7] runs before any SDK init or LoadLibrary
public:
  static bool ApplyAffinity(uint64 Mask); static bool SetHighPriority(); static bool DisablePowerThrottling();
  static bool SetTimerResolution1ms();              // UE sets it process-wide (WindowsPlatformMisc.cpp:1076 [V]); applied to every engine
  static std::string GetActivePowerSchemeName();    // read and recorded, never changed
  static double SampleSystemBusyOther(double Seconds);
  static void SpinPreRoll(double Seconds, uint32 Threads); static std::vector<uint32> PCoreFirstLogicals(); };
class FProcessSpawner {                             // [확장 M7] fresh process per run; G8 (DET1) re-spawns the bench exe
public:
  static int32 RunChild(const std::wstring& CommandLine, double TimeoutSeconds, const std::filesystem::path& ExitJsonPath); };
class FThreadCensus {                               // [확장 M10] QueryThreadCycleTime per thread; G10b cpu_eff gate
public:
  void Capture(); static double ForeignCycleFraction(const FThreadCensus& Before, const FThreadCensus& After, uint32 MainTid); };
}
```

```cpp
// File: Source/Runtime/PhysicsTestConformance/Public/ConformanceGate.h   (PT::Conformance -> PT::BenchCore)   [MVP M6]
namespace PhysicsTest {
enum class EGateResult : uint8 { Pass, Fail, Skip };
struct FGateReport { std::string Gate; EGateResult Result; std::string Detail; };
class IConformanceGate {
public:
  virtual ~IConformanceGate() = default;
  virtual const char* GetId() const = 0;            // "A0".."A10" (no A8 before M14), "G0".."G12" (G8 = DET1; G10a, G10b)
  virtual uint64 RequiredCapabilities() const { return 0; }
  virtual FGateReport Run(FPhysicsBackendModule& Module, const FResolvedPhysicsConfig& Config) = 0;
};
}
```

```cpp
// File: Source/Runtime/PhysicsTestBenchmark/Public/BenchmarkRunner.h   (PT::Benchmark -> PT::Conformance)
//   [MVP M3 minimal; 확장 M7 full; 확장 M10]
namespace PhysicsTest {
struct FBenchRunSpec { std::string Backend, BackendProfile, PhysicsProfile, Scenario; std::map<std::string, std::string> Params;
                       int32 Frames = 600, Threads = 1; uint64 AffinityMask = 0x1; EPhysicsRunMode RunMode = EPhysicsRunMode::Bench;
                       uint32 InstrumentFlags = 0; int32 RepeatIndex = 0, OrderIndex = 0;
                       int32 HashEvery = 0, StatesEvery = 0, CountersEvery = 0;   // headline: hash at the final frame only
                       std::string ExecCmds, IniOverrides; std::filesystem::path OutDir;
                       static FBenchRunSpec FromCommandLine(const char* CommandLine); };
class FTimingRecorder { public: void Reserve(int32 Frames); void Record(int32 Frame, const FFrameTiming& Timing, const PtStepStats* Stats);
                        bool WriteCsv(const std::filesystem::path& Path) const; };            // timings.csv [MVP M3]
class FStateRecorder  { public: bool Open(const std::filesystem::path& Path, uint32 Bodies, double Dt, uint64 SceneSha, uint64 DescSha);
                        void Append(uint32 Frame, uint32 Step, double SimTime, std::span<const PtBodyState> States); bool Close(); };   // states.ptst PTST v1 [확장 M7]
struct FRunManifest { nlohmann::json Json;          // physicstest.run/1 [MVP M3 basic, 확장 M7 full]
                      bool Write(const std::filesystem::path& Path) const;
                      std::string ComputeComparabilityKey() const; };   // [확장 M10]
struct FConformanceStampKey {                       // [확장 M10]
  std::string BenchExeSha256, BackendModuleSha256 /* DLL file hash for DLL backends */, VariantSha256, PhysicsSha256, TolerancesSha256;
  int32 GateSuiteVersion = 1;
  std::string ToPath() const; };                    // optional /Brepro keeps the exe SHA stable across relinks
class FPreflight { public: static std::vector<FGateReport> Run(FPhysScene& Scene, const FBenchRunSpec& Spec); };
                                                    // [확장 M7] G0, G9, G10a before the timed run; stamp check [확장 M10]
enum class EBenchExit : int32 { Ok = 0, GateFailed = 2, EngineError = 3, Contaminated = 4, Capacity = 5, BackendUnavailable = 6 };
struct FExitRecord { EBenchExit Code = EBenchExit::Ok; std::string Reason;   // [확장 M7]
                     bool WriteAndFlush(const std::filesystem::path& OutDir) const; };   // exit.json; the runner trusts it over the exit code
class FBenchmarkRunner {
public:
  // Writes and flushes every output and exit.json. With a UE_RUNTIME module it then ends the process with
  // TerminateProcess(GetCurrentProcess(), code) instead of returning (no UE teardown during CRT detach).
  EBenchExit Run(const FBenchRunSpec& Spec);
};
}
```

---

## A.13 Null 백엔드 — [MVP M2, NATIVE_FRAME_MODEL 게이트 M6]

- SDK 없이 semi-implicit Euler와 UE damping만으로 도는 기준 백엔드이며 Chaos 작성자의 템플릿이다.
- ABI 동결(M6) 전에 Native driver와 `NumStepsTaken` 계약을 검증하려고 PT_CAP_NATIVE_FRAME_MODEL을 선언한다.
- 백엔드는 하네스 코드를 링크할 수 없으므로(R4) 플래너를 독립적으로 다시 구현한다. M6의 프레임 모델 게이트 A10이 Sync, Substepping(UE5), AsyncFixed 표에서 이 값을 하네스 `FStepPlanner`와 대조하고, FixedStep과 Sync에서 Native driver와 강제 Harness driver의 최종 상태가 비트 동일한지 본다. 두 구현이 따로 있으므로 교차검증이 된다.

```cpp
// File: Source/Backends/NullBackend/Private/NullBackend.h   [MVP M2; planner gate A10 at M6]   PT_BACKEND_NS=Null
#include "PhysicsTestAbi/PtBackendKit.h"
#include "PtTypes.h"
namespace PhysicsTest::NullBackend {
using namespace ::PhysicsTest::Abi::FixedWidth;
// Independent re-implementation of FStepPlanner semantics (Sync, UE5ChaosScene substepping, AsyncFixed accumulator).
class FNullFramePlanner {
public:
  uint32 Plan(const ::PhysicsTest::Abi::FFrameParams& Params, double AsyncDt, double& InOutAccumulator, float& OutStepDt) const;
};
class FNullScene final : public ::PhysicsTest::Abi::IPhysicsBackendScene {
  /* arrays of PtBodyState + mass; SetUpForFrame stores the UE arguments; EndFrame reports NumStepsTaken from FNullFramePlanner;
     ReadBodyStates supports PT_READ_ACTIVE_ONLY; no collision */
};
class FNullBackend final : public ::PhysicsTest::Abi::IPhysicsBackend {
  /* GetInfo: Capabilities = KINEMATIC | NATIVE_FRAME_MODEL | ACTIVE_LIST | STATE_WRITE | NATIVE_HASH;
     RequiredEffectiveKeys = {} (the generic set only) */
};
}
// NullBackend.cpp:    PT_IMPLEMENT_BACKEND_MODULE_STATIC(PhysicsTest::NullBackend::FNullBackend, PT_GetBackendApi_Null)
// NullBackendDll.cpp: PT_IMPLEMENT_BACKEND_MODULE(PhysicsTest::NullBackend::FNullBackend)   -> PtBackend_Null.dll (DLL path tests, A9)
```

---

## A.14 PhysX 백엔드 내부 (UE4 이름, 클린룸) — [MVP M5]

- `FPhysXSceneConfig`에서 substep clamp rule을 뺐다. 하네스 `FStepPlanner`가 소유한다. 대신 Normalized의 sleep off용 `bDisableSleeping`(PxSceneFlag::eDISABLE_SLEEPING [V])을 넣었다.
- 정수 타입은 BackendCommon `PtTypes.h`에서 가져온다. 하네스 Core를 보지 않는다.
- 바디 생성은 UE의 `FInitBodiesHelper` 순서다. PhysX의 `putToSleep`은 씬 밖의 액터에 대해 오류를 낸다 (NpRigidDynamic.cpp:527 [V]). `eDISABLE_SLEEPING` 씬에서는 `eINVALID_OPERATION`으로 무시하므로 (NpRigidDynamic.cpp:521-525 [V]) Normalized에서는 이 단계를 건너뛴다.
- 머티리얼은 `createMaterial(staticFriction, dynamicFriction, restitution)` 순서다 (PxPhysics.h:764 [V]). UE4Compat 계열은 `(F, F, R)` [M], SdkDefault는 `(max(F, SF), F, R)`로 Chaos 쌍 규칙을 재현한다.

```cpp
// File: Source/Backends/PhysXBackend/Private/*.h   [MVP M5]   PT_BACKEND_NS=PhysX   (UE4 names, clean room; condensed)
namespace PhysicsTest::PhysXBackend {
using namespace ::PhysicsTest::Abi::FixedWidth;      // BackendCommon PtTypes.h, never harness Core

// ---- PhysXSupport.h
physx::PxVec3 U2PVector(const PtVec3& V, double LengthScale); PtVec3 P2UVector(const physx::PxVec3& V, double LengthScale);
physx::PxTransform U2PTransform(const PtTransform& T, double LengthScale); PtTransform P2UTransform(const physx::PxTransform& T, double LengthScale);
extern const physx::PxQuat U2PSphylBasis;            // 180 deg about (1,0,1)/sqrt2: UE Z capsule -> PhysX X
struct FPhysxUserData { uint8 Type; int32 Index; };
class FPhysXAllocator final : public physx::PxAllocatorCallback { /* 16 B aligned; live/peak/allocs */ };
class FPhysXErrorCallback final : public physx::PxErrorCallback { /* -> log + error bits */ };

// ---- PhysicsInitialization.cpp: once per process. One PxPhysics and one PxTolerancesScale (PxPhysics.h:997,
//      PxSceneDesc.h:541 [V]): (100,1000) for UE4Compat / -TGS / -Stab (separate PxScenes may coexist, viewer side by side M12),
//      (1,10) for SdkDefault (its own process). The scale arrives as a process-wide InitGamePhys option.
bool InitGamePhys(const physx::PxTolerancesScale& Scale); void TermGamePhys();

// ---- PhysXCPUDispatcher.h
class FPhysXCPUDispatcherSingleThread final : public physx::PxCpuDispatcher {
public:
  // No recursion: a thread-local LIFO stack. A re-entrant submitTask (a continuation released inside run()) pushes
  // and returns; the outermost call drains the stack (run(), then release()). Cross-checked by state hash against
  // PxDefaultCpuDispatcherCreate(0).
  void submitTask(physx::PxBaseTask& Task) override;
  uint32_t getWorkerCount() const override;          // 0
};

// ---- PhysicsFiltering.h   (word0 body index, word1 block mask, word2 component id, word3 mask|channel|EPDF [M]; PhysicsFiltering.h:18-35 [V])
enum EPhysXFilterDataFlags : uint32 { EPDF_SimpleCollision = 0x1, EPDF_ComplexCollision = 0x2, EPDF_CCD = 0x4, EPDF_ContactNotify = 0x8,
                                      EPDF_StaticShape = 0x10, EPDF_ModifyContacts = 0x20, EPDF_KinematicKinematicPairs = 0x40 };
physx::PxFilterFlags PhysXSimFilterShader(physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0,
                                          physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1,
                                          physx::PxPairFlags& PairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize);

// ---- PhysXSimEventCallback.h
class FPhysXSimEventCallback final : public physx::PxSimulationEventCallback { /* viz only; <= 16 points per pair [M]; onWake/onSleep counters */ };

// ---- PhysXSceneConfig.h   (from the variant ini; no substep clamp rule here - the harness FStepPlanner owns it)
struct FPhysXSceneConfig {
  double LengthScale; physx::PxBroadPhaseType::Enum BroadPhase; physx::PxSolverType::Enum Solver;
  bool bPCM, bStabilization, bCCD, bEnhancedDeterminism, bDisableSleeping /* eDISABLE_SLEEPING (Normalized) */, bKeepKinematicPairs, bActiveActors;
  float BounceThreshold; uint32 TreeRebuildRate, ScratchBytes; bool bUE4ContactOffset; uint8 PosIters, VelIters;
  bool bStaticFrictionEqualsDynamic /* UE4Compat: createMaterial(F, F, R) [M]; else (max(F, SF), F, R) */, bD6ForAllJoints; };

// ---- PhysScene_PhysX.h
class FPhysScene_PhysX final : public PhysicsTest::Abi::IPhysicsBackendScene {
  void InitPhysScene();                              // PxSceneDesc from FPhysXSceneConfig
  // CreateActors = FInitBodiesHelper order:
  //   1 per body: CreateActor_AssumesLocked   createRigidStatic/Dynamic, eKINEMATIC, eDISABLE_GRAVITY, CCD, iterations
  //   2 per body: CreateShapesFromAggGeom     exclusive PxShape per element, filter words, material, contact offset
  //   3 once:     PxScene::addActors(all)     ~ AddActorsToScene_AssumesLocked
  //   4 per body: InitDynamicProperties_AssumesLocked in BodyInstance.cpp:4648-4743 [V] order: mass/inertia/COM -> damping
  //               -> maxAngularVelocity -> maxDepenetration (0 -> PX_MAX_F32; 5.11 rejects <= 0) -> linear velocity
  //               -> sleep threshold x multiplier -> stabilization x multiplier -> iterations
  //               -> if !START_AWAKE and not kinematic: setWakeCounter(0) + putToSleep (skipped when bDisableSleeping)
  void CreateActor_AssumesLocked(const PtActorCreateInfo& Info);                                 // ~ TInitBodiesHelperBase::CreateActor_AssumesLocked
  void CreateShapesFromAggGeom(const PtActorCreateInfo& Info, physx::PxRigidActor& Actor);     // ~ FBodySetupShapeIterator
  void InitDynamicProperties_AssumesLocked(physx::PxRigidDynamic& Actor, const PtActorCreateInfo& Info);
  float ComputeContactOffset(double MinExtentCm) const;   // clamp(0.02 * MinExtent, 2, 8) cm [M formula, V constants]
  /* StartFrame: simulate(dt, nullptr, Scratch, 262144); WaitPhysScenes: fetchResults(true); ReadBodyStates: getActiveActors */
};
}   // PT_IMPLEMENT_BACKEND_MODULE_STATIC(FPhysXBackend, PT_GetBackendApi_PhysX)
```

---

## A.15 Jolt, Bullet 백엔드 요점 — [Jolt MVP M3, Bullet MVP M5]

- 두 백엔드 모두 BackendCommon의 COM 래핑 규칙을 쓴다. 단일 요소이고 COM이 원점과 같으면 네이티브 프리미티브를 바로 쓴다. 그래서 `M3.BoxPile`은 래퍼를 0개 만든다.
- 마찰·반발 결합은 `CombinePairFriction` / `CombinePairRestitution`으로 UE combine 모드 규칙을 재현한다. 두 엔진은 쌍당 마찰 계수가 하나라 (`mCombinedFriction`, `m_combinedFriction`) 콜백이 동적 결합값 `C(F0, F1)`만 돌려준다. SF ≤ F인 머티리얼(UE 기본 SF = 0)에서는 Chaos 쌍 정지마찰과 같고, SF > F인 머티리얼이 낀 쌍은 `material.static_friction_rule=dynamic_only`로 기록해 by-design으로 태그한다 ([04_DecisionLog.md](../04_DecisionLog.md) DL41).

```cpp
// File: Source/Backends/JoltBackend/Private/PhysScene_Jolt.h   [MVP M3]   PT_BACKEND_NS=Jolt
namespace PhysicsTest::JoltBackend {
class FPhysScene_Jolt final : public PhysicsTest::Abi::IPhysicsBackendScene {
  // Units: m (GMetres). Mass: MassPropertiesOverride + EOverrideMassProperties::MassAndInertiaProvided (R*diag*R^T, x1e-4).
  // Shapes: BoxShape / SphereShape directly when CanUseNativePrimitive(); otherwise OffsetCenterOfMassShape / StaticCompoundShape.
  // Capsule: RotatedTranslatedShape (UE Z -> Jolt Y).
  // Combine: PhysicsSystem::SetCombineFriction / SetCombineRestitution lambdas -> CombinePairFriction(...).Dynamic /
  //   CombinePairRestitution; one coefficient per pair -> material.static_friction_rule=dynamic_only.
  // Islands: PT_CAP_NATIVE_ISLANDS not declared (GetIslandIndexInternal is internal); the viewer uses FIslandBuilder.
  // Sleep off (Normalized): BodyCreationSettings::mAllowSleeping = false.
  // Step: PhysicsSystem::Update(dt, 1, TempAllocatorImpl, JobSystemThreadPool(T-1)); EPhysicsUpdateError checked every step.
  // Sync: GetBodyInterfaceNoLock + GetActiveBodies. Prepare: OptimizeBroadPhase.
};
}
```

```cpp
// File: Source/Backends/BulletBackend/Private/PhysScene_Bullet.h   [MVP M5]   PT_BACKEND_NS=Bullet
namespace PhysicsTest::BulletBackend {
class FPhysScene_Bullet final : public PhysicsTest::Abi::IPhysicsBackendScene {
  // Units: m (GMetres). World: btDefaultCollisionConfiguration, btDbvtBroadphase, btSequentialImpulseConstraintSolver,
  //   btDiscreteDynamicsWorld; step stepSimulation(dt, 0, dt).
  // Shapes: btBoxShape / btSphereShape directly when CanUseNativePrimitive(); btCompoundShape (COM frame) only otherwise.
  // Box margin: setSafeMargin -> min(0.04 m, 0.1 * minHalfExtent) per shape (btBoxShape.cpp:25, btConvexInternalShape.h:66-81 [V]);
  //   read back with getMargin() into shape.box_margin (a 50 cm cube gets 0.025 m).
  // Capsule: btCapsuleShapeZ, no remap (D1 amendment).
  // Combine: global gCalculateCombinedFriction / RestitutionCallback -> CombinePairFriction(...).Dynamic (GLOBAL_SCENE_SETTINGS);
  //   one coefficient per pair (m_combinedFriction) -> material.static_friction_rule=dynamic_only.
  // Damping: UEDampingToBullet. Angular clamp: UEParity emulates 62.83 rad/s; pi/(2*dt) is COMPILE_TIME (btRigidBody.cpp:380-385 [V]).
  // Debug drawer: attached in viz runs only, DBG_NoDeactivation mirrored from the profile, because stepSimulation
  //   overwrites gDisableDeactivation from it (btDiscreteDynamicsWorld.cpp:418-421 [V]).
  // Sleep off (Normalized): DISABLE_DEACTIVATION.
};
}
```

---

## A.16 Chaos 브리지 (UBT 전용, 사용자 구현) — [골격 MVP M2.5 (H, 선택), 본체 확장 M13 (U)]

- 이 절의 파일은 로컬 UE 5.8.3 소스 빌드에 대해서만 UBT로 컴파일한다 (D7, `PT_WITH_CHAOS` 기본 OFF). UE 소스나 UE 파생 코드는 저장소에 넣지 않는다.
- 하네스 개발자가 M2.5 스파이크(선택, 3 달력일 타임박스)에서 TODO가 달린 골격을 만든다. 본체는 사용자가 M13에서 M6에 동결된 ABI 위에 구현한다.
- 스파이크 DLL은 `PT_ABI_VERSION_MAJOR` 0으로 빌드한다. 호스트도 같은 major를 요청한다.

```csharp
// File: Unreal/ChaosBridgeHost/Source/PhysicsTestChaos.Target.cs   [H: M2.5 skeleton]
using UnrealBuildTool;

public class PhysicsTestChaosTarget : TargetRules
{
    public PhysicsTestChaosTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        LinkType = TargetLinkType.Monolithic;
        bShouldCompileAsDLL = true;
        LaunchModuleName = "PhysicsTestChaos";
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        FPSemantics = FPSemanticsMode.Imprecise;    // Program targets default to /fp:precise (VCToolChain.cs:1324-1330 [V])
        bCompileAgainstEngine = false;
        bCompileAgainstCoreUObject = true;
        // Already the Monolithic default (TargetRules.cs:1792-1795 [V]); set explicitly so every *_API stays empty and no
        // import library is produced. PT_GetBackendApi is exported by its own __declspec(dllexport).
        bHasExports = false;
        bUsesSlate = false;
        bCompileICU = false;
        bBuildDeveloperTools = false;
        bLegalToDistributeBinary = false;
        GlobalDefinitions.Add("NOINITCRASHREPORTER=1");
    }
}
// PhysicsTestChaosAvx2.Target.cs : same, plus MinCpuArchX64 = MinimumCpuArchitectureX64.AVX2   (MatchedAVX2, optional)
// PhysicsTestChaosServer.Target.cs: bShouldCompileAsDLL = false, GlobalDefinitions "PT_CHAOS_SERVER=1"   (OOP fallback, 확장 M14, conditional)
```

```csharp
// File: Unreal/ChaosBridgeHost/Source/PhysicsTestChaos/PhysicsTestChaos.Build.cs   [H: M2.5 skeleton]
using System.IO;
using UnrealBuildTool;

public class PhysicsTestChaos : ModuleRules
{
    public PhysicsTestChaos(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "ApplicationCore", "Projects", "GeometryCore", "PhysicsCore", "Chaos", "ChaosCore" });
        // RequiredProgramMainCPPInclude.h includes LaunchEngineLoop.h and Runtime/Launch/Private/LaunchEngineLoop.cpp
        // (RequiredProgramMainCPPInclude.h:6,11 [V]); HeadlessChaos.Build.cs:20 adds the same path (as Public).
        PrivateIncludePathModuleNames.Add("Launch");
        // Also pulls NNE and Chaos Visual Debugger support in 5.8.3 (ModuleRules.cs:1793-1813 [V]).
        SetupModulePhysicsSupport(Target);
        // <repo>/Source/Runtime/PhysicsTestAbi/Public, four levels above ModuleDirectory. BackendCommon is not visible here.
        PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../Source/Runtime/PhysicsTestAbi/Public")));
        PrivateDefinitions.Add("PT_ABI_NO_EXCEPTIONS=1");
        PrivateDefinitions.Add("PT_BACKEND_NS=ChaosBridge");   // not "Chaos": it would clash with UE's ::Chaos (A.2)
    }
}
```

```cpp
// File: Unreal/ChaosBridgeHost/Source/PhysicsTestChaos/Private/PtChaosBackend.h   [U: 확장 M13; H delivers the skeleton in M2.5]
#pragma once
#define PT_KIT_CURRENT_THREAD_ID() ((uint32_t)FPlatformTLS::GetCurrentThreadId())
#include "PhysicsTestAbi/PtBackendKit.h"
#include "Chaos/ChaosScene.h"
#include "Chaos/ChaosEngineInterface.h"

class FPtChaosScene final : public PhysicsTest::Abi::IPhysicsBackendScene
{
    // CreateScene
    //   Scene = MakeUnique<FChaosScene>(nullptr, Desc.AsyncFixedTimeStepSize);   // PT_ASYNC_DT_SYNC (-1) for sync, never 0
    //   Solver->ApplyConfig(resolved SolverOptions); Solver->SetIsDeterministic(...);   // explicit, after construction: without
    //     Engine the CDO reads [/Script/PhysicsCore.PhysicsSettingsCore], not the user's [/Script/Engine.PhysicsSettings]
    //   T=1 : Solver->SetThreadingMode_External(Chaos::EThreadingModeTemp::SingleThread) (PhysicsSolverBase.h:260-265,615 [V];
    //         the FChaosScene ctor picks Chaos::EThreadingMode for CreateSolver, ChaosScene.cpp:75 [V])
    //         + p.Chaos.SingleWorkerPhysics=1 (opt. p.Chaos.DisablePhysicsParallelFor=1); -corelimit=1 was given to PreInit
    //   T>=2: TaskGraph + -corelimit=T + p.Chaos.MaxNumWorkers=T (a cap only)
    //   DumpEffectiveSettings: threads.single_worker, threads.max_num_workers, chaos.threading_mode, chaos.async_dt,
    //     chaos.use_async_interpolation as READBACK keys (the Chaos RequiredEffectiveKeys, A.10)
    //   Normalized sleep off: p.Chaos.Solver.Sleep.Enabled=0 (IslandManager.cpp:53 [V]) or ESleepType::NeverSleep per particle
    //     (IslandManager.cpp:240 [V])
    // CreateActors, UE order (BodyInstance.cpp:1301-1340, 1669, 1695-1706, 4648-4743 [V]):
    //   1 per body: FChaosEngineInterface::CreateActor(FActorCreationParams) + creation-time setters
    //               (CCD, kinematic, SetMaxLinearVelocity, inertia conditioning, gyroscopic, iteration counts)
    //   2 per body: SetGeometry - TSphere / TBox (margin = min(2*minHalf*0.05, 10 cm), ChaosInterfaceUtils.cpp:156 [V]) /
    //               FCapsule / FConvex; FShapeFilterBuilder with BlockMask widened to uint64; a material on every shape
    //   3 once:     AddActorsToScene_AssumesLocked
    //   4 per body: mass/inertia/COM as given (no ^0.75 recompute; G2 checks), damping, max angular velocity, depenetration,
    //               linear velocity, sleep thresholds, iterations, then, if !START_AWAKE and not kinematic,
    //               SetWakeCounter_AssumesLocked(0) + PutToSleep_AssumesLocked (requires IsInScene,
    //               BodyInstance.cpp:4723-4733 [V]); skipped when sleeping is off (Normalized)
    // Frame (native frame model)
    //   SetUpForFrame -> Scene->SetUpForFrame(&Grav, Dt, MinDt, MaxDt, MaxSubDt, MaxSubsteps, bSubstepping);
    //   StartFrame / WaitPhysScenes / EndFrame -> FChaosScene 1:1; NumStepsTaken reported.
    //   AsyncFixed: GT state is interpolated with one-step latency (p.UseAsyncInterpolation=1 is required for fixed-dt
    //   stepping); such runs are labelled and excluded from per-body behaviour comparison.
    TUniquePtr<FChaosScene> Scene; TArray<FPhysicsActorHandle> Actors; TArray<Chaos::FMaterialHandle> Materials;
};

class FPtChaosEvolutionScene final : public PhysicsTest::Abi::IPhysicsBackendScene { /* diagnostic: raw FPBDRigidsEvolutionGBF */ };

class FPtChaosBackend final : public PhysicsTest::Abi::IPhysicsBackend
{
    // ctor trivial (constructed before PreInit)
    // InitGamePhys: SetOverrideProjectDir -> UE::Geometry::ExactPredicates::GlobalInit() -> GEngineLoop.PreInit("... -corelimit=T")
    //               -> ProcessNewlyLoadedUObjects -> StartProcessingNewlyLoadedObjects -> InitGamePhysCore -> CVars -> FOutputDevice
    // TermGamePhys: RequestEngineExit -> AppPreExit -> UnloadModulesAtShutdown -> AppExit
    //               (never RequestExit, never SetStandaloneSolver(true), never FreeLibrary)
    // GetInfo: ModuleFlags = ONE_PER_PROCESS | NO_REINIT | UE_RUNTIME | SPAWNS_THREADS_AT_INIT (+ DEBUG_DRAW_COMPILED in Development);
    //          UE_RUNTIME makes the bench write exit.json and end with TerminateProcess.
};
```

```cpp
// File: Unreal/ChaosBridgeHost/Source/PhysicsTestChaos/Private/PtChaosModule.cpp   [H: M2.5 skeleton]
#include "RequiredProgramMainCPPInclude.h"      // needs PrivateIncludePathModuleNames "Launch" (Build.cs above)
#include "PtChaosBackend.h"
IMPLEMENT_APPLICATION(PhysicsTestChaos, "PhysicsTestChaos");
PT_IMPLEMENT_BACKEND_MODULE(FPtChaosBackend)    // exports PT_GetBackendApi; the M2.5 spike is built against PT_ABI_VERSION_MAJOR 0
```

---

## A.17 반영한 수정 (FIX 대응표)

적대적 검토에서 나온 수정 목록 중 헤더에 해당하는 항목과 반영 위치다.

| FIX | 내용 | 반영 위치 |
|---|---|---|
| F1 | Chaos T=1/T≥2 스레드 매핑, effective 키 | A.1 `PtSceneDesc.NumThreads`, `PtInitParams.MaxThreads`; A.16 `FPtChaosScene` |
| F2 | UE 순서의 바디 생성, `bStartAwake=false` 게이트 (`S08.StartAsleep`, G6), sleep off에서 putToSleep 생략 | A.1 `CreateActors`·`PtActorCreateInfo`·`PT_BODY_START_AWAKE`; A.2; A.9; A.14; A.16 |
| F3 | AsyncFixed 보간과 지연, `AsyncDt=-1` | A.1 `PT_ASYNC_DT_SYNC`, `AsyncFixedTimeStepSize`; A.2 `ValidateSceneDesc`; A.7; A.16 |
| F4 | ECC_MAX 65, uint64 마스크, 하네스 32채널 | A.1 `PT_NUM_COLLISION_CHANNELS`; A.6 `ECollisionChannel`; A.0.4 |
| F5 | TU 자기보고는 어댑터 TU 사실만 | A.1 `BuildFlagsJson`; A.4 `PtBuildReport.h` |
| F6 | Build.cs에 `Launch` include 경로 | A.16 Build.cs, PtChaosModule.cpp |
| F7 | 질량 규칙 단계 순서 | A.6 `BodyUtils.h` |
| F8 | Normalized sleep off의 엔진별 수단 | A.1 `PT_SIM_ALLOW_SLEEPING`; A.14 `bDisableSleeping`; A.15; A.16 |
| F9 | `bHasExports` 근거 | A.16 Target.cs |
| F10 | 정지마찰 원값 전달, 쌍 결합, 엔진별 처리 (DL41) | A.1 `PtPhysicalMaterial`; A.4 `PtCombine.h`; A.6 `FPhysicalMaterial`; A.14 머티리얼; A.15 (규칙 자체는 아래 "판단" 참고) |
| F11 | 드로 순서, `DrawConstraintGraph` | A.11 `DebugDraw::DrawAll` |
| F12 | UE 이름 충실도 | A.6 (FRotator, ConstraintUtils, 중첩 구조), A.5 (`FBox::IsValid`), A.8 (`AddImpulse_`/`AddVelocity_`), A.11 (`FDebugDrawQueue`), A.0.4 표 |
| F13 | Bullet·Chaos 박스 margin | A.15 Bullet; A.16 `TBox` |
| F14 | Bullet `btCapsuleShapeZ` (D1 보완) | A.4 `PtUnitScale.h`; A.15 |
| F15·F40 | PxPhysics·tolerance scale은 프로세스당 하나, 같은 scale 변형은 공존 | A.1 `PT_MODULE_GLOBAL_SCENE_SETTINGS`; A.8 레지스트리; A.10 `InitOptions`; A.14 |
| F16 | ini 섹션, SolverOptions 명시 적용 | A.6 `PhysicsSettings.h`; A.10; A.16 |
| F17 | 씬 ID (S08은 02·03이 F2 게이트용으로 더한 확장) | A.12 `GetName` 주석 |
| F18 | Bullet 드로어 viz 전용, Jolt 섬 | A.1 capability 주석; A.11 `FIslandBuilder`; A.15 |
| F20 | IChaosDDRenderer 확인 완료, 캡슐·박스 의미 | A.1 `PtDebugDrawCmd.Extent`, `PtShapeElem`; A.11 `IDebugDrawRenderer` |
| F22 | `FBodyInstanceCore` 인용 | A.6 `BodyInstance.h` |
| F23 | `desc_sha256` 범위 | A.1 해시 분리 주석; A.9 `ComputeDescSha256` / `ComputeOptionsSha256` |
| F24 | 스탬프 키 | A.12 `FConformanceStampKey` [확장 M10] |
| F25 | 필요할 때만 COM 래핑 | A.4 `PtShapeWrapping.h`; A.15 |
| F26 | 핵심 v1.0 동결 M6, PROVISIONAL(M9), Null의 NATIVE_FRAME_MODEL (A10) | A.1 전체; A.2; A.7; A.13 |
| F27 | `inline namespace PT_BACKEND_NS`, 상태는 provider, 고정 폭 타입, ClampRule 제거 | A.2; A.4 전체; A.14 |
| F28 | doctest `check()` 충돌, START/END | A.4 하네스 측 `Misc/PtThirdPartyIncludesStart.h` / `End.h`, 백엔드 측 `PtThirdPartyIncludes.h` (컴파일 확인); A.5 |
| F29 | 뷰어 성능 기준 | A.11 머리말 (산문) |
| F30 | dt_engine 기준 해석 해 | A.7 `FStepPlan` 주석 |
| F31 | `PT::BenchCore` (M3 클럭 버킷, 확장 M7 호스트·스포너, 확장 M10 ThreadCensus) | A.12 |
| F32 | 필수 effective 키 = 일반 + 백엔드 (02 §10 목록) | A.1 `PtBackendInfo.RequiredEffectiveKeys`; A.8; A.10; A.13; A.16 |
| F34 | 단일 스레드 디스패처의 LIFO 스택 | A.14 |
| F35 | `exit.json`, `TerminateProcess` | A.1 `PT_MODULE_UE_RUNTIME`; A.12; A.16 |
| F36 | 헤드라인은 마지막 프레임 해시, 카운터 off | A.1 `PtStepStats` 주석; A.6 `FPhysicsTestSettings`; A.12 `FBenchRunSpec` |
| F38 | ABI major 0 (v0.9), 호스트는 빌드 major 요청 | A.1 버전 매크로; A.8; A.16 |
| F41 | 기본 가상 함수 인자 이름 없음, 사소한 백엔드 인스턴스화 | A.2; A.3 (컴파일 확인) |

**헤더 밖이라 이 부록에 반영하지 않은 항목:** F19(Bullet FetchContent), F21(`physx_lib` 타깃명), F33(반발 게이트 허용치), F37(`docs/` 소문자. 이 부록의 경로와 링크는 모두 소문자 `docs/`를 쓴다), F39(환경 준비). 이 항목들은 [02_Architecture.md](../02_Architecture.md), [03_WorkPlan.md](../03_WorkPlan.md)에서 다룬다. F22 중 THIRD_PARTY_NOTICES 부분도 같다.

**판단이 들어간 항목**
- **F10의 쌍 규칙:** 수정 목록의 식은 `max(combine(SF0,SF1), combine(F0,F1))`이다. 5.8.3 소스를 다시 읽으면 Chaos는 결합 전에 머티리얼마다 `max(F, SF)`를 먼저 적용한다 (PBDCollisionConstraints.cpp:474-476 [V]). 그래서 헤더에는 소스와 같은 `max(C(max(F0,SF0), max(F1,SF1)), C(F0,F1))`를 적었다. [02_Architecture.md](../02_Architecture.md) §5.1과 [04_DecisionLog.md](../04_DecisionLog.md) DL41도 같은 식이다. "원값을 ABI로 넘긴다"는 F10의 요지는 그대로다. 다만 쌍당 계수가 하나인 Jolt·Bullet은 콜백이 이 식을 재현하지 못하고 동적 결합값만 돌려준다 (DL41의 한계, `material.static_friction_rule=dynamic_only`). 정지마찰 규칙을 그대로 재현하는 SDK 백엔드는 PhysX SdkDefault(`createMaterial(max(F, SF), F, R)`)뿐이다.
- **F28의 형태:** 매크로는 `#undef`를 만들 수 없으므로, 하네스 측은 include 파일 쌍(Core `Misc/PtThirdPartyIncludesStart.h` / `End.h`)으로, 백엔드 측은 warning push/pop만 하는 `PT_THIRD_PARTY_INCLUDES_START/END` 매크로(BackendCommon `PtThirdPartyIncludes.h`)로 나눴다. 02 §8.1, DL03과 같다.
- **F12의 FRotator:** "FRotator + 캐시된 FQuat" 대신 UE처럼 `FK*Elem`에는 `FRotator`만 두고, 호스트가 마샬링할 때 `GetTransform()`으로 바디당 한 번 변환한다. 02 §5.2의 "마샬링 시 FQuat 캐시"는 이 1회 변환을 말한다. 변환은 비타이밍 생성 단계에서 일어나고, `desc_sha256`은 변환 후의 쿼터니언을 해시하므로 원소에 캐시 필드가 필요 없다.
- **ABI 명령 플래그:** `PT_CMD_ADD_VELOCITY` / `PT_CMD_ADD_ANGULAR_VELOCITY`를 추가하고, 비게 된 bit 0을 `PT_CMDF_ALLOW_SUBSTEPPING`에 썼다. Native driver가 `bAllowSubstepping`을 Chaos에 넘길 수단이 이전 스케치에는 없었다.
