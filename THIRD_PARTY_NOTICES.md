# 제3자 고지 (Third-Party Notices)

> **기준일:** 2026-10-21 · **관련 문서:** [docs/02_Architecture.md](docs/02_Architecture.md) §16 (라이선스 정책, D7) · [docs/04_DecisionLog.md](docs/04_DecisionLog.md) DL54

이 저장소는 아래 의존성의 소스를 **포함하지 않는다.** 모두 빌드할 때 `out/<preset>/_deps`로 받아 오며, 버전은 커밋 SHA(SDK 3종) 또는 아카이브 SHA-256(나머지)으로 고정한다 (`cmake/PTOptions.cmake`, `cmake/ThirdParty/*.cmake`). 빌드 결과물(exe, lib, pdb)을 배포하게 되면 각 라이선스의 고지 의무가 생긴다. 지금은 결과물을 배포하지 않으며, 벤치마크 결과도 UE EULA와 각 SDK 라이선스의 벤치마크 조항을 사용자가 확인할 때까지 비공개다 (01 Q1).

## 1. 목록

| # | 의존성 | 고정 버전 | 받는 곳 | 라이선스 (SPDX) | 저작권 | 쓰는 곳 |
|---|---|---|---|---|---|---|
| 1 | NVIDIA PhysX SDK 5.11.0 (`physx/` 디렉터리) | `da950a3537927784951853c66618036f332ca0ce` (tag ovphysx-0.6.3) | github.com/NVIDIA-Omniverse/PhysX 또는 `PT_SDK_MIRROR` | **Apache-2.0** (`physx/LICENSE.md`) | NVIDIA Corporation | `PT::PhysX`, PhysX 백엔드(M5), 스모크 |
| 2 | Jolt Physics 5.6.0 | `e77f175595e64cb44218cc9d9d56fc365ad0e36a` (v5.6.0) | github.com/jrouwe/JoltPhysics 또는 미러 | MIT | Copyright 2021 Jorrit Rouwe | `PT::Jolt`, Jolt 백엔드(M3), 스모크, 플래그 누수 프로브 |
| 3 | Bullet Physics 3.25 | `2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5` (3.25) | github.com/bulletphysics/bullet3 또는 미러 | Zlib (`LICENSE.txt`, `Extras/`와 `examples/ThirdPartyLibs` 제외 - 둘 다 빌드하지 않음) | Copyright (c) 2003-2006 Erwin Coumans 외 (소스 파일 머리말) | `PT::Bullet` (LinearMath, BulletCollision, BulletDynamics만), Bullet 백엔드(M5), 스모크 |
| 4 | Dear ImGui | v1.92.9b-docking, SHA-256 `90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc` | github.com/ocornut/imgui (archive) | MIT | Copyright (c) 2014-2026 Omar Cornut | `PT::ImGui`, 뷰어(M4) |
| 5 | ImPlot | v1.0, SHA-256 `e4a9db64eef7bcc604e2a5ea380af124eb97aa3e8a6a96330079c88add0f7e93` | github.com/epezent/implot (archive) | MIT | Copyright (c) 2020 Evan Pezent | `PT::ImGui`, 뷰어(M4) |
| 6 | stb | `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d`, SHA-256 `ae498e10fadeba0c2e7aa86c71cc9989c4d895201cd26b8b72061c76cfde4edf` | github.com/nothings/stb (archive) | MIT **또는** Unlicense(퍼블릭 도메인) 택일. 하네스는 MIT로 받는다 | Copyright (c) 2017 Sean Barrett | `PT::Stb`, 뷰어 캡처(M4) |
| 7 | nlohmann/json | v3.12.0, SHA-256 `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` (release asset `json.tar.xz`) | github.com/nlohmann/json | MIT | Copyright (c) 2013-2025 Niels Lohmann | `PT::Json`, manifest·결과 포맷(M3+) |
| 8 | doctest | v2.5.3, SHA-256 `174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331` | github.com/doctest/doctest (archive) | MIT | Copyright (c) 2016-2023 Viktor Kirilov | `PT::Doctest`, `PhysicsTestAutomation` |
| 9 | Tracy Profiler | v0.14.1, SHA-256 `bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1` | github.com/wolfpld/tracy (archive) | BSD-3-Clause | Copyright (c) 2017-2026 Bartosz Taudul | **prof 프리셋 전용 (M10).** M0에서는 고정값만 기록했고, M10부터 prof 프리셋이 `cmake/ThirdParty/Tracy.cmake`로 받아 클라이언트(`public/TracyClient.cpp`)만 빌드한다. 받은 archive의 SHA-256이 고정값과 같음을 2026-10-22에 다시 확인했다. 다른 프리셋은 받지 않는다 |

- SHA-256은 2026-10-21에 GitHub 아카이브를 다시 받아 계산했고, 타당성 검토 때 받아 둔 tarball과 같았다 [V].
- Python 분석 도구(`Tools/ptbench`, M8+)의 패키지는 저장소에 넣지 않고 `.venv`에만 설치한다. 버전은 `Tools/ptbench/requirements.lock`에 있다 (numpy, pandas, pyarrow, scipy, matplotlib, jsonschema와 그 의존성. 각 패키지의 라이선스는 설치된 패키지 메타데이터를 따른다).

## 2. 라이선스별 의무 (배포할 때)

| 라이선스 | 해당 | 배포 시 할 일 |
|---|---|---|
| Apache-2.0 | PhysX `physx/` | 라이선스 사본을 함께 준다. 수정한 파일에는 수정 사실을 표시한다. 원본의 저작권·특허·상표·귀속 고지를 유지한다. 저작물에 `NOTICE` 파일이 있으면 그 귀속 고지를 함께 배포해야 한다 - **da950a35 기준 `physx/` 아래에는 `NOTICE` 파일이 없다** [V]. 그래도 재배포할 때마다 그 시점의 SDK에 `NOTICE`가 생겼는지 다시 확인한다 |
| MIT | Jolt, Dear ImGui, ImPlot, stb(MIT 선택), nlohmann/json, doctest | 저작권 고지와 허가 문구를 소프트웨어의 모든 사본 또는 상당 부분에 포함한다 |
| Zlib | Bullet | 출처를 사칭하지 않는다. 수정한 소스는 수정본임을 표시한다. 소스 배포에서 고지를 지우지 않는다 (바이너리 배포에서의 고지는 권장 사항) |
| BSD-3-Clause | Tracy (M10) | 소스·바이너리 배포에 저작권 고지, 조건, 면책 문구를 포함한다. 저작권자 이름으로 파생물을 홍보하지 않는다 |

## 3. 주의

- PhysX 저장소 **루트**의 `LICENSE.md`는 BSD-3-Clause이지만, 하네스가 빌드하는 SDK는 `physx/` 디렉터리이고 그 라이선스는 **Apache-2.0**이다 (5.11.0부터) [V]. 이 둘을 혼동하지 않는다.
- Unreal Engine 소스와 UE 파생 코드는 이 저장소에 없다 (D7, 클린룸). Chaos 연결 코드(M13)는 로컬 UE 5.8.3 설치본으로만 컴파일하며, 그 결과물(DLL, PDB, Intermediate)은 커밋하거나 배포하지 않는다.
- `prototypes/local/`(git 무시)의 UE4 방식 PhysX 프로브는 공개 UE 4.18 미러를 읽고 작성한 것이라 저장소에 넣지 않는다.
