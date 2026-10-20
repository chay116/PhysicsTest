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

> **현재 상태 (2026-10-21):** 타당성 검토와 설계까지 끝났고, 구현은 아직 시작하지 않았습니다. 다음 단계는 [작업 계획](docs/03_WorkPlan.md)의 M0(스캐폴드)입니다.

## 문서

| 문서 | 내용 |
|---|---|
| [01_Feasibility.md](docs/01_Feasibility.md) | 타당성 검토: SDK 빌드 실험, hello 테스트, 렌더러 프로토타입, Chaos 단독 실행 경로, 공정성 리스크 |
| [02_Architecture.md](docs/02_Architecture.md) | 아키텍처: 디렉터리, CMake 타깃, 핵심 타입(UE 대응표), 단일 씬, C ABI 경계, 백엔드 내부(UE4 방식 PhysX), 스텝/타이밍 계약, 프로필, 뷰어, 벤치 러너 |
| [03_WorkPlan.md](docs/03_WorkPlan.md) | 마일스톤 M0~M15, 완료 기준, 공수, 크리티컬 패스 |
| [04_DecisionLog.md](docs/04_DecisionLog.md) | 고정 결정, 설계 결정 로그, 사용자 확인이 필요한 질문 |
| [appendix/A_HeaderSketches.md](docs/appendix/A_HeaderSketches.md) | 핵심 헤더 선언 초안 (C ABI, 씬 타입, 백엔드 Kit) |

## 빌드 환경 (예정)

- Windows 10/11 x64
- Visual Studio 2022 BuildTools 이상 (MSVC 14.44에서 검증), Windows SDK 10.0.26100
- CMake 4.1 이상, Ninja
- Python 3.11 (분석 도구용, 가상환경 사용)
- Chaos 백엔드를 쓸 경우: 로컬 UE 5.8.3 소스 빌드 (`PT_WITH_CHAOS=ON`, 기본값은 OFF)

물리 SDK는 빌드할 때 지정한 커밋으로 clone합니다. 로컬 체크아웃(`file:///D:/PhysicsRefs/...`)을 가리키게 할 수도 있습니다. 어느 경우든 저장소에는 SDK 소스를 넣지 않습니다.

## 라이선스 메모

- 이 저장소에는 Unreal Engine 소스와 UE 소스를 옮긴 코드를 넣지 않습니다. Chaos 연결 코드는 로컬 UE 설치본으로만 컴파일하며, 빌드 결과물(DLL/PDB, Intermediate 등)은 커밋하지 않습니다.
- 서드파티 라이선스: PhysX SDK(`physx/`)는 Apache-2.0, Jolt는 MIT, Bullet은 zlib, Dear ImGui와 ImPlot은 MIT입니다. 자세한 내용은 M0에서 `THIRD_PARTY_NOTICES.md`로 정리합니다.
- 벤치마크 결과는 UE EULA와 각 SDK 라이선스를 확인할 때까지 공개하지 않습니다.
