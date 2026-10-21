# 벤치 리포트 — smoke-20261021T202335Z (UNOFFICIAL)

> **UNOFFICIAL 예시 (M8 smoke, 전원 계획 Balanced).** `python -I -m ptbench run --matrix smoke --allow-balanced --report`의 세션 `Saved/Results/smoke-20261021T202335Z`를 수정 라운드 1의 리포트 코드로 다시 만들어 복사했다. 그림은 BoxPile 5k 두 장(`table_02.png`, `frames_02.png`)만 함께 복사했고 나머지 그림 링크(`table_01.png`, `frames_01.png`, `table_03.png`, `scaling.png`)는 뺐다. 모든 표 제목 밑과 두 그림의 제목 위에 조건(UNOFFICIAL, Balanced, `--allow-balanced`)이 적혀 있다. 사용 중인 데스크톱, Balanced, 스탬프 없음: 엔진 비교나 순위의 근거가 아니다. 생성된 리포트는 커밋하지 않는다 (Saved/).

> **전원 계획이 High performance가 아니다: 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e)** (세션의 환경 게이트, run 60/60개의 manifest 기준). 환경 게이트를 **`--allow-balanced`**로 우회했다. Balanced에서는 run 간 노이즈가 크다 (01 Q9). 아래 모든 표가 이 조건의 값이다.
>
> **비공식 (UNOFFICIAL)** — no conformance stamp (M10); no CV gate / claim rules (M10); power plan is not High performance: 균형 조정 (Balanced). CV < 2 %는 목표로만 적는다 (게이트는 M10). 이 조건은 아래 모든 표의 제목 밑과 모든 그림의 제목 위에도 적는다.

## 1. 세션

| 항목 | 값 |
|---|---|
| 세션 | `smoke-20261021T202335Z` (complete, 2026-10-21T20:23:35Z → 2026-10-21T20:27:29Z, 3.9 분) |
| 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `2f9fe78bd7c3`) |
| 벤치 exe | `D:\PhysicsTest\out\test\bin\PhysicsTestBench.exe` (preset test, flag profile Shipped, sha256 `d675d1e9f9c0`) |
| git | `aaf81c60c62d` (dirty) |
| 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| 환경 게이트: idle | 통과 — system busy 2.68% over 2 s (limit 3%, windows tried: 3.01, 4.38, 2.68) |
| 환경 게이트: processes | 통과 — no other PhysicsTest process |
| 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| run | 계획 60, 시도 60 (재시도 0), 결과 {"ok": 60}, 셀 상태 {"ok": 12}, exit.json 없음 0, 타임아웃 0 |
| 분석 | run 60개 중 60개 채택, 0개 거부 (§5) |

## 2. 표 (scene × profile × flag profile × T, 윈도 `headline`)

값은 run마다 윈도의 `T_frame` 평균이다. median = run 값의 중앙값, CV = run 값의 표본 표준편차 / 평균, p95·p99 = 셀의 모든 run의 윈도 프레임을 합친 nearest-rank. ⚠는 CV ≥ 2 % (목표 미달, M8은 보고만). 엔진 간 비교는 `T_frame`만 한다 (TimingContract §2).

### M3.BoxPile[N=1000] · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `3b12fe32bba80d46` (표 안에서 하나), run 20개.

| series | n | median | p95 | p99 | CV | min – max | frame 0 (median) |
|---|---|---|---|---|---|---|---|
| jolt.Default | 5 | 980.91 µs | 1.232 ms | 1.649 ms | 0.60% | 978.12 µs – 993.01 µs | 314.40 µs |
| bullet.Default | 5 | 1.577 ms | 1.847 ms | 1.949 ms | 0.43% | 1.565 ms – 1.581 ms | 395.50 µs |
| physx.UE4Compat | 5 | 596.97 µs | 682.60 µs | 775.20 µs | 0.52% | 592.74 µs – 601.23 µs | 632.20 µs |
| physx.SdkDefault | 5 | 589.71 µs | 672.60 µs | 742.30 µs | 1.69% | 583.66 µs – 609.25 µs | 450.00 µs |

### M3.BoxPile[N=5000] · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `d67bd724a06f94af` (표 안에서 하나), run 20개.

| series | n | median | p95 | p99 | CV | min – max | frame 0 (median) |
|---|---|---|---|---|---|---|---|
| jolt.Default | 5 | 5.402 ms | 6.532 ms | 8.586 ms | 0.54% | 5.375 ms – 5.445 ms | 1.427 ms |
| bullet.Default | 5 | 10.343 ms | 13.571 ms | 14.250 ms | 0.31% | 10.318 ms – 10.398 ms | 1.809 ms |
| physx.UE4Compat | 5 | 3.447 ms | 4.154 ms | 4.862 ms | 0.87% | 3.441 ms – 3.513 ms | 2.844 ms |
| physx.SdkDefault | 5 | 3.222 ms | 3.736 ms | 4.019 ms | 0.12% | 3.217 ms – 3.226 ms | 1.599 ms |

![M3.BoxPile[N=5000] · Normalized · Shipped · T1](table_02.png)

![M3.BoxPile[N=5000] · Normalized · Shipped · T1 per frame](frames_02.png)

### P0.EmptyStep · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `0d1a1e4639195961` (표 안에서 하나), run 20개.

| series | n | median | p95 | p99 | CV | min – max | frame 0 (median) |
|---|---|---|---|---|---|---|---|
| jolt.Default | 5 | 1.09 µs | 1.40 µs | 1.70 µs | 10.32% ⚠ | 1.01 µs – 1.31 µs | 24.70 µs |
| bullet.Default | 5 | 357 ns | 500 ns | 500 ns | 5.32% ⚠ | 327 ns – 366 ns | 27.60 µs |
| physx.UE4Compat | 5 | 2.24 µs | 2.40 µs | 3.00 µs | 1.93% | 2.20 µs – 2.32 µs | 105.20 µs |
| physx.SdkDefault | 5 | 2.09 µs | 3.00 µs | 3.10 µs | 16.30% ⚠ | 2.05 µs – 2.89 µs | 95.00 µs |

## 3. 셀별 CV (M3.BoxPile 5k 포함 모든 셀)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

| 셀 | n | median | CV | 목표 (< 2 %) |
|---|---|---|---|---|
| bullet.Default · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 1.577 ms | 0.43% | 충족 |
| jolt.Default · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 980.91 µs | 0.60% | 충족 |
| physx.SdkDefault · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 589.71 µs | 1.69% | 충족 |
| physx.UE4Compat · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 596.97 µs | 0.52% | 충족 |
| bullet.Default · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 10.343 ms | 0.31% | 충족 |
| jolt.Default · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 5.402 ms | 0.54% | 충족 |
| physx.SdkDefault · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 3.222 ms | 0.12% | 충족 |
| physx.UE4Compat · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 3.447 ms | 0.87% | 충족 |
| bullet.Default · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 357 ns | 5.32% ⚠ | 미달 (보고만) |
| jolt.Default · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 1.09 µs | 10.32% ⚠ | 미달 (보고만) |
| physx.SdkDefault · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 2.09 µs | 16.30% ⚠ | 미달 (보고만) |
| physx.UE4Compat · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 2.24 µs | 1.93% | 충족 |

## 4. 스케일링과 오버헤드 모델 (원시, 비공식)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

| series | scene | N | median | α (t = c·N^α) |
|---|---|---|---|---|
| bullet.Default · Normalized · T1 | M3.BoxPile | 1000, 5000 | 1.577 ms, 10.343 ms | 1.169 |
| jolt.Default · Normalized · T1 | M3.BoxPile | 1000, 5000 | 980.91 µs, 5.402 ms | 1.060 |
| physx.SdkDefault · Normalized · T1 | M3.BoxPile | 1000, 5000 | 589.71 µs, 3.222 ms | 1.055 |
| physx.UE4Compat · Normalized · T1 | M3.BoxPile | 1000, 5000 | 596.97 µs, 3.447 ms | 1.089 |

오버헤드 모델 t = a + b·N (P0/P1/M3 셀의 median, 최소제곱):

| series | a (프레임당 고정) | b (바디당) | R² | 점 |
|---|---|---|---|---|
| bullet.Default · Normalized · T1 | -233.84 µs | 2103.7 ns | 0.9977 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| jolt.Default · Normalized · T1 | -46.69 µs | 1087.3 ns | 0.9996 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.SdkDefault · Normalized · T1 | -24.79 µs | 648.1 ns | 0.9997 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.UE4Compat · Normalized · T1 | -42.65 µs | 695.7 ns | 0.9992 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |

a < 0은 비용이 N에 대해 선형보다 빨리 자란다는 뜻이다 (α > 1, 접촉 쌍이 바디 수보다 빨리 는다). 선형 모델은 M8 형식일 뿐 물리적 고정 비용의 추정이 아니다. P1 셀(N 10, 100)이 있으면 a가 작은 N 쪽에 맞는다.

## 5. 거부된 run과 재시도

거부된 run 없음.

## 6. 방법

- 러너: 셀 = target × profile × scene × T, 반복 r마다 seed 고정 블록 셔플(splitmix64 + Fisher-Yates), run마다 fresh process, 쿨다운, 타임아웃. 판정은 `exit.json`이다: 없으면 프로세스가 끝까지 가지 못한 실패이고 원시 종료 코드로 성공을 판정하지 않는다 (02 §12.2, DL52). 종료 코드 4는 같은 run을 fresh process로 최대 2번 다시 돌리고, 모든 시도가 4이면 셀이 `contaminated`다.
- 채택 조건 (validate): `exit.json` 0, 두 스키마 통과, Bench 모드, 계측 tier 0, preflight 게이트 통과, 오염 없음, `timing_valid_for_headline` 참. 한 표에 플래그 프로필이 섞이거나 `desc_sha256`이 다르면 표 자체를 거부하고 통계를 내지 않는다 (§3에도 '표 거부'로만 적는다). 한 표의 run이 여러 빌드(exe, git, preset)에서 왔으면 표 아래에 주의를 적는다 (comparability key는 M10).
- bootstrap CI, comparability key, CV 게이트, sentinel, 주장 규칙은 M10이다. 이 리포트의 비율과 순위는 주장이 아니다.

