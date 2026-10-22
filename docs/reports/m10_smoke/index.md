# 벤치 리포트 — m10-smoke-test (UNOFFICIAL)

> **UNOFFICIAL 예시 (M10, 전원 계획 Balanced).** 세션 다섯 개(`m10-smoke-test`: test, `m10-smoke-avx2`: test-avx2, `m10-comp-{jolt,physx,bullet}`: 단일 SDK exe, `--target`)를 `python -I -m ptbench report <세션 5개>`로 합친 리포트를 복사했다. §2b는 주장 규칙(Shipped와 MatchedAVX2), §2c는 바이너리 구성 대조군(판정은 M11), §3은 CV 게이트(noisy)다. 그림은 BoxPile 5k Shipped 두 장(`table_04.png`, `frames_04.png`)만 함께 복사했고 나머지 그림 링크는 뺐다. 사용 중인 데스크톱, Balanced: 엔진 비교나 순위의 근거가 아니다. 생성된 리포트는 커밋하지 않는다 (Saved/). **M10 수정 라운드 0에서 같은 세션으로 다시 만들었다:** noisy 표시가 그 셀을 잰 세션의 것만 붙는다 (첫 판은 같은 셀 키의 다른 세션 표에도 붙어 17행이 noisy였다. 지금 8행 = test 넷 + test-avx2 넷, 각 `session.json`과 같다). §2c 마지막 열은 M11 기준의 참고값이다. **M10 수정 라운드 1에서 같은 세션으로 다시 만들었다:** noisy 셀이 있는 쌍은 주장하지 않는다 (TimingContract §9: CV 게이트를 통과한 셀만 보고할 수 있다). 그래서 §2b의 '빠르다/느리다'가 17쌍에서 6쌍(BoxPile 1k 다섯, BoxPile 5k bullet 대 jolt)으로 줄었고, 나머지 11쌍은 사유에 noisy 셀과 CI만으로의 판정을 적은 no claim이다. §2b 비율 표의 ⚠ noisy가 그 셀이다. §2c 첫 문단에 단일 SDK exe를 빌드 내용(manifest `harness.sdks`)으로 가린다는 문장이 더해졌다 (이 세션들의 manifest에는 그 필드가 없어 전처럼 preset 이름으로 가렸고 값은 같다). 다른 절의 수치와 그림 두 장은 같다.

> **전원 계획이 High performance가 아니다: 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e)** (세션 5개의 환경 게이트, run 191/191개의 manifest 기준). 환경 게이트를 **`--allow-balanced`**로 우회했다. Balanced에서는 run 간 노이즈가 크다 (01 Q9). 아래 모든 표가 이 조건의 값이다.
>
> 세션 5개를 합친 리포트다: `m10-smoke-test`, `m10-smoke-avx2`, `m10-comp-jolt`, `m10-comp-physx`, `m10-comp-bullet`.
>
> **비공식 (UNOFFICIAL)** — power plan is not High performance: 균형 조정 (Balanced); environment gate bypassed: --allow-balanced; no sentinel cell declared (02 12.5 step 4). 이 조건은 아래 모든 표의 제목 밑과 모든 그림의 제목 위에도 적는다.

## 1. 세션

| 항목 | 값 |
|---|---|
| [1] 세션 | `m10-smoke-test` (complete, 2026-10-22T09:22:37Z → 2026-10-22T09:27:13Z, 4.6 분) |
| [1] 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `ef9461134c95`) |
| [1] 벤치 exe | `D:\PhysicsTest\out\test\bin\PhysicsTestBench.exe` (preset test, flag profile Shipped, sha256 `d7e7cf79a9f2`) |
| [1] git | `6f44886c0d2b` (dirty) |
| [1] 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| [1] 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| [1] 환경 게이트: idle | 통과 — system busy 2.64% over 2 s (limit 3%; 1 window(s) in 2.0 s, wait limit 120 s: 2.64) |
| [1] 환경 게이트: processes | 통과 — no other PhysicsTest process |
| [1] 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| [1] run | 계획 68, 시도 71 (재시도 0), 결과 {"ok": 71}, 셀 상태 {"ok": 12}, exit.json 없음 0, 타임아웃 0 |
| [1] 적합성 스탬프 (M10) | 정책 `require`, 스탬프 디렉터리 `D:\PhysicsTest\Saved\Conformance`, 대상 4개, 스탬프 없는 셀 0개 |
| [1] CV 게이트 (M10) | CV > 2.0 %이면 추가 블록 최대 2회: 추가 블록 2개, noisy 셀 4개 |
| [1] sentinel (M10) | `jolt.Default|Normalized|M3.BoxPile[N=1000]|T1`: start 990.59 µs, middle 979.22 µs, end 978.29 µs; drift 1.24 % (문턱 2.0 %) |
| [2] 세션 | `m10-smoke-avx2` (complete, 2026-10-22T09:31:20Z → 2026-10-22T09:35:59Z, 4.7 분) |
| [2] 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `ef9461134c95`) |
| [2] 벤치 exe | `D:\PhysicsTest\out\test-avx2\bin\PhysicsTestBench.exe` (preset test-avx2, flag profile MatchedAVX2, sha256 `a613b5a9e264`) |
| [2] git | `6f44886c0d2b` (dirty) |
| [2] 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| [2] 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| [2] 환경 게이트: idle | 통과 — system busy 2.81% over 2 s (limit 3%; 2 window(s) in 4.0 s, wait limit 120 s: 3.35, 2.81) |
| [2] 환경 게이트: processes | 통과 — no other PhysicsTest process |
| [2] 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| [2] run | 계획 68, 시도 71 (재시도 0), 결과 {"ok": 71}, 셀 상태 {"ok": 12}, exit.json 없음 0, 타임아웃 0 |
| [2] 적합성 스탬프 (M10) | 정책 `require`, 스탬프 디렉터리 `D:\PhysicsTest\Saved\Conformance`, 대상 4개, 스탬프 없는 셀 0개 |
| [2] CV 게이트 (M10) | CV > 2.0 %이면 추가 블록 최대 2회: 추가 블록 2개, noisy 셀 4개 |
| [2] sentinel (M10) | `jolt.Default|Normalized|M3.BoxPile[N=1000]|T1`: start 982.36 µs, middle 990.29 µs, end 990.79 µs; drift 0.86 % (문턱 2.0 %) |
| [3] 세션 | `m10-comp-jolt` (complete, 2026-10-22T09:27:33Z → 2026-10-22T09:28:45Z, 1.2 분) |
| [3] 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `ef9461134c95`) |
| [3] 벤치 exe | `D:\PhysicsTest\out\test-jolt\bin\PhysicsTestBench.exe` (preset test-jolt, flag profile Shipped, sha256 `c1784623b3b6`) |
| [3] git | `6f44886c0d2b` (dirty) |
| [3] 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| [3] 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| [3] 환경 게이트: idle | 통과 — system busy 2.85% over 2 s (limit 3%; 9 window(s) in 18.0 s, wait limit 120 s: 3.99, 3.54, 4.47, 4.44, 3.78, 5.13, 3.42, 3.23, 2.85) |
| [3] 환경 게이트: processes | 통과 — no other PhysicsTest process |
| [3] 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| [3] run | 계획 15, 시도 19 (재시도 1), 결과 {"contaminated": 1, "ok": 18}, 셀 상태 {"ok": 3}, exit.json 없음 0, 타임아웃 0 |
| [3] 적합성 스탬프 (M10) | 정책 `require`, 스탬프 디렉터리 `D:\PhysicsTest\Saved\Conformance`, 대상 1개, 스탬프 없는 셀 0개 |
| [3] CV 게이트 (M10) | CV > 2.0 %이면 추가 블록 최대 2회: 추가 블록 0개, noisy 셀 0개 |
| [3] sentinel (M10) | `jolt.Default|Normalized|M3.BoxPile[N=1000]|T1`: start 985.14 µs, middle 983.07 µs, end 993.44 µs; drift 0.84 % (문턱 2.0 %) |
| [4] 세션 | `m10-comp-physx` (complete, 2026-10-22T09:29:05Z → 2026-10-22T09:29:57Z, 0.9 분) |
| [4] 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `ef9461134c95`) |
| [4] 벤치 exe | `D:\PhysicsTest\out\test-physx\bin\PhysicsTestBench.exe` (preset test-physx, flag profile Shipped, sha256 `9a3be6efa9a4`) |
| [4] git | `6f44886c0d2b` (dirty) |
| [4] 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| [4] 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| [4] 환경 게이트: idle | 통과 — system busy 2.77% over 2 s (limit 3%; 10 window(s) in 20.0 s, wait limit 120 s: 3.27, 3.59, 4.44, 4.11, 4.11, 3.26, 3.06, 4.39, 3.13, 2.77) |
| [4] 환경 게이트: processes | 통과 — no other PhysicsTest process |
| [4] 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| [4] run | 계획 15, 시도 15 (재시도 0), 결과 {"ok": 15}, 셀 상태 {"ok": 3}, exit.json 없음 0, 타임아웃 0 |
| [4] 적합성 스탬프 (M10) | 정책 `require`, 스탬프 디렉터리 `D:\PhysicsTest\Saved\Conformance`, 대상 1개, 스탬프 없는 셀 0개 |
| [4] CV 게이트 (M10) | CV > 2.0 %이면 추가 블록 최대 2회: 추가 블록 0개, noisy 셀 0개 |
| [4] sentinel (M10) | 선언 없음 |
| [5] 세션 | `m10-comp-bullet` (complete, 2026-10-22T09:29:59Z → 2026-10-22T09:31:16Z, 1.3 분) |
| [5] 매트릭스 | `smoke` (perf, R = 5, block seed 20261022, sha256 `ef9461134c95`) |
| [5] 벤치 exe | `D:\PhysicsTest\out\test-bullet\bin\PhysicsTestBench.exe` (preset test-bullet, flag profile Shipped, sha256 `045d76ca658e`) |
| [5] git | `6f44886c0d2b` (dirty) |
| [5] 호스트 | 13th Gen Intel(R) Core(TM) i5-13600KF, 논리 CPU 20, Windows-10-10.0.26200-SP0 |
| [5] 환경 게이트: power_plan | **우회** --allow-balanced — 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) |
| [5] 환경 게이트: idle | 통과 — system busy 2.57% over 2 s (limit 3%; 1 window(s) in 2.0 s, wait limit 120 s: 2.57) |
| [5] 환경 게이트: processes | 통과 — no other PhysicsTest process |
| [5] 러너 | 쿨다운 2.0 s, 타임아웃 600.0 s, 종료 코드 4 재시도 2회, 윈도 `headline`, 판정 = `exit.json` (원시 종료 코드는 기록만) |
| [5] run | 계획 15, 시도 15 (재시도 0), 결과 {"ok": 15}, 셀 상태 {"ok": 3}, exit.json 없음 0, 타임아웃 0 |
| [5] 적합성 스탬프 (M10) | 정책 `require`, 스탬프 디렉터리 `D:\PhysicsTest\Saved\Conformance`, 대상 1개, 스탬프 없는 셀 0개 |
| [5] CV 게이트 (M10) | CV > 2.0 %이면 추가 블록 최대 2회: 추가 블록 0개, noisy 셀 0개 |
| [5] sentinel (M10) | 선언 없음 |
| run의 exe sha256 | `045d76ca658e`, `9a3be6efa9a4`, `a613b5a9e264`, `c1784623b3b6`, `d7e7cf79a9f2` (여러 개) |
| run의 preset | `test`, `test-avx2`, `test-bullet`, `test-jolt`, `test-physx` (여러 개) |
| 분석 | run 191개 중 181개 채택, 10개 거부 (§5) |

## 2. 표 (comparability key마다 하나, 윈도 `headline`)

값은 run마다 윈도의 `T_frame` 평균이다 (M10: TSC 버킷, run 전체 QPC 보정). median = run 값의 중앙값과 그 percentile bootstrap 95 % CI (B = 10000), CV = run 값의 표본 표준편차 / 평균, p95·p99 = 셀의 모든 run의 윈도 프레임을 합친 nearest-rank, cpu_eff = 센서스 창의 이 프로세스 cycle / T개 코어의 cycle. ⚠는 CV > 2 %, **noisy**는 CV 게이트의 추가 블록 뒤에도 넘은 셀. 표 하나 = comparability key 하나 (02 §13): 엔진과 변형은 시리즈다. 엔진 간 비교는 `T_frame`만 한다 (TimingContract §2).

### M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `87f51f7c124752b7` (표 안에서 하나), run 20개.

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| jolt.Default | 5 | 983.62 µs | 979.35 µs – 1.025 ms | 1.266 ms | 1.653 ms | 1.90% | 979.35 µs – 1.025 ms | 1.0000 | 293.42 µs |
| bullet.Default | 5 | 1.562 ms | 1.556 ms – 1.572 ms | 1.829 ms | 1.979 ms | 0.45% | 1.556 ms – 1.572 ms | 1.0000 | 379.22 µs |
| physx.UE4Compat | 5 | 580.79 µs | 575.30 µs – 584.93 µs | 673.88 µs | 754.63 µs | 0.65% | 575.30 µs – 584.93 µs | 1.0000 | 604.94 µs |
| physx.SdkDefault | 5 | 576.72 µs | 570.04 µs – 583.76 µs | 655.52 µs | 774.10 µs | 0.99% | 570.04 µs – 583.76 µs | 1.0000 | 437.69 µs |



### M3.BoxPile[N=1000] · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `87f51f7c124752b7` (표 안에서 하나), run 35개.

> **주의:** 4 exe values in one table (045d76ca658e, 9a3be6efa9a4, c1784623b3b6, d7e7cf79a9f2): the runs come from different builds of one harness commit

> **주의:** 4 preset values in one table (test, test-bullet, test-jolt, test-physx): the runs come from different builds of one harness commit

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| physx.SdkDefault | 5 | 588.99 µs | 583.12 µs – 601.19 µs | 670.29 µs | 754.10 µs | 1.18% | 583.12 µs – 601.19 µs | 1.0000 | 425.53 µs |
| bullet.Default@test | 5 | 1.580 ms | 1.568 ms – 1.594 ms | 1.861 ms | 2.035 ms | 0.61% | 1.568 ms – 1.594 ms | 1.0000 | 390.26 µs |
| bullet.Default@test-bullet | 5 | 1.572 ms | 1.562 ms – 1.585 ms | 1.851 ms | 1.991 ms | 0.60% | 1.562 ms – 1.585 ms | 1.0000 | 381.07 µs |
| jolt.Default@test | 5 | 980.62 µs | 979.85 µs – 994.63 µs | 1.245 ms | 1.632 ms | 0.72% | 979.85 µs – 994.63 µs | 1.0000 | 279.97 µs |
| jolt.Default@test-jolt | 5 | 983.25 µs | 980.87 µs – 988.30 µs | 1.214 ms | 1.629 ms | 0.32% | 980.87 µs – 988.30 µs | 1.0000 | 285.06 µs |
| physx.UE4Compat@test | 5 | 594.30 µs | 593.20 µs – 602.84 µs | 693.24 µs | 784.14 µs | 0.77% | 593.20 µs – 602.84 µs | 1.0000 | 599.85 µs |
| physx.UE4Compat@test-physx | 5 | 602.80 µs | 592.03 µs – 607.72 µs | 695.63 µs | 831.33 µs | 1.19% | 592.03 µs – 607.72 µs | 1.0000 | 638.40 µs |



### M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `e5bf86dde70a8275` (표 안에서 하나), run 24개.

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| jolt.Default | 5 | 5.438 ms | 5.372 ms – 5.461 ms | 6.546 ms | 8.545 ms | 0.75% | 5.372 ms – 5.461 ms | 1.0000 | 1.448 ms |
| bullet.Default | 5 | 10.652 ms | 10.543 ms – 10.708 ms | 14.060 ms | 14.700 ms | 0.57% | 10.543 ms – 10.708 ms | 1.0000 | 1.823 ms |
| physx.UE4Compat | 7 | 3.359 ms | 3.338 ms – 3.551 ms | 4.012 ms | 4.795 ms | 3.06% ⚠ **noisy** | 3.317 ms – 3.564 ms | 1.0000 | 2.879 ms |
| physx.SdkDefault | 7 | 3.200 ms | 3.175 ms – 3.357 ms | 3.807 ms | 4.043 ms | 3.20% ⚠ **noisy** | 3.166 ms – 3.431 ms | 1.0000 | 1.650 ms |



### M3.BoxPile[N=5000] · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `e5bf86dde70a8275` (표 안에서 하나), run 37개.

> **주의:** 4 exe values in one table (045d76ca658e, 9a3be6efa9a4, c1784623b3b6, d7e7cf79a9f2): the runs come from different builds of one harness commit

> **주의:** 4 preset values in one table (test, test-bullet, test-jolt, test-physx): the runs come from different builds of one harness commit

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| physx.SdkDefault | 5 | 3.232 ms | 3.205 ms – 3.285 ms | 3.762 ms | 4.058 ms | 0.93% | 3.205 ms – 3.285 ms | 1.0000 | 1.596 ms |
| bullet.Default@test | 5 | 10.488 ms | 10.414 ms – 10.522 ms | 13.734 ms | 14.398 ms | 0.42% | 10.414 ms – 10.522 ms | 1.0000 | 1.881 ms |
| bullet.Default@test-bullet | 5 | 10.370 ms | 10.283 ms – 10.475 ms | 13.602 ms | 14.200 ms | 0.71% | 10.283 ms – 10.475 ms | 1.0000 | 1.816 ms |
| jolt.Default@test | 5 | 5.432 ms | 5.381 ms – 5.450 ms | 6.672 ms | 8.551 ms | 0.49% | 5.381 ms – 5.450 ms | 1.0000 | 1.456 ms |
| jolt.Default@test-jolt | 5 | 5.403 ms | 5.389 ms – 5.442 ms | 6.539 ms | 8.628 ms | 0.43% | 5.389 ms – 5.442 ms | 1.0000 | 1.424 ms |
| physx.UE4Compat@test | 7 | 3.448 ms | 3.419 ms – 3.645 ms | 4.134 ms | 4.771 ms | 3.49% ⚠ **noisy** | 3.393 ms – 3.685 ms | 1.0000 | 2.800 ms |
| physx.UE4Compat@test-physx | 5 | 3.427 ms | 3.399 ms – 3.431 ms | 3.996 ms | 4.641 ms | 0.46% | 3.399 ms – 3.431 ms | 1.0000 | 2.854 ms |

![M3.BoxPile[N=5000] · Normalized · Shipped · T1](table_04.png)

![M3.BoxPile[N=5000] · Normalized · Shipped · T1 per frame](frames_04.png)

### P0.EmptyStep · Normalized · MatchedAVX2 · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `f11b244039de3090` (표 안에서 하나), run 24개.

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| jolt.Default | 7 | 1.03 µs | 1.02 µs – 1.06 µs | 1.28 µs | 1.33 µs | 4.90% ⚠ **noisy** | 1.01 µs – 1.16 µs | 1.0000 | 13.11 µs |
| bullet.Default | 5 | 325 ns | 321 ns – 330 ns | 339 ns | 356 ns | 1.20% | 321 ns – 330 ns | 1.0000 | 24.21 µs |
| physx.UE4Compat | 7 | 2.32 µs | 2.29 µs – 2.38 µs | 2.36 µs | 3.25 µs | 12.01% ⚠ **noisy** | 2.29 µs – 3.09 µs | 1.0000 | 94.96 µs |
| physx.SdkDefault | 5 | 2.16 µs | 2.15 µs – 2.20 µs | 2.24 µs | 2.32 µs | 0.88% | 2.15 µs – 2.20 µs | 1.0000 | 85.26 µs |


### P0.EmptyStep · Normalized · Shipped · T1

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

desc_sha256 `f11b244039de3090` (표 안에서 하나), run 41개.

> **주의:** 4 exe values in one table (045d76ca658e, 9a3be6efa9a4, c1784623b3b6, d7e7cf79a9f2): the runs come from different builds of one harness commit

> **주의:** 4 preset values in one table (test, test-bullet, test-jolt, test-physx): the runs come from different builds of one harness commit

| series | n | median | 95 % CI | p95 | p99 | CV | min – max | cpu_eff | frame 0 (median) |
|---|---|---|---|---|---|---|---|---|---|
| physx.SdkDefault | 7 | 2.04 µs | 2.04 µs – 2.09 µs | 2.10 µs | 2.15 µs | 2.47% ⚠ **noisy** | 2.03 µs – 2.17 µs | 1.0000 | 81.81 µs |
| bullet.Default@test | 7 | 319 ns | 317 ns – 337 ns | 401 ns | 420 ns | 3.55% ⚠ **noisy** | 316 ns – 344 ns | 1.0000 | 24.88 µs |
| bullet.Default@test-bullet | 5 | 333 ns | 325 ns – 337 ns | 410 ns | 436 ns | 1.51% | 325 ns – 337 ns | 1.0000 | 24.13 µs |
| jolt.Default@test | 7 | 1.01 µs | 1.01 µs – 1.03 µs | 1.06 µs | 1.35 µs | 2.40% ⚠ **noisy** | 990 ns – 1.07 µs | 1.0000 | 13.88 µs |
| jolt.Default@test-jolt | 5 | 1.04 µs | 1.03 µs – 1.06 µs | 1.27 µs | 1.32 µs | 1.18% | 1.03 µs – 1.06 µs | 1.0000 | 12.11 µs |
| physx.UE4Compat@test | 5 | 2.19 µs | 2.15 µs – 2.22 µs | 2.24 µs | 2.46 µs | 1.28% | 2.15 µs – 2.22 µs | 1.0000 | 93.10 µs |
| physx.UE4Compat@test-physx | 5 | 2.19 µs | 2.16 µs – 2.22 µs | 2.23 µs | 2.75 µs | 1.13% | 2.16 µs – 2.22 µs | 1.0000 | 94.61 µs |


## 2b. 비교와 주장 규칙 (M10)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

"A가 씬 S에서 B보다 빠르다"는 블록 bootstrap 비율 CI가 1을 포함하지 않고, |ρ − 1| ≥ 3 %이고, Shipped와 MatchedAVX2에서 방향이 같을 때만 쓴다. 방향이 다르면 ISA-dependent다 (02 §13). ρ = T_frame(A) / T_frame(B)이며 1보다 작으면 A가 빠르다. 블록이 5개뿐이면 percentile bootstrap CI가 거칠어 점추정이 CI 끝에 놓이기도 한다: 보고용 주장은 R = 10 세션(03 M11 헤드라인 세션)으로 낸다. CV 게이트 뒤에도 noisy인 셀(§3)이 A나 B에 하나라도 있으면(어느 플래그 프로필이든) 주장하지 않는다: 보고할 수 있는 셀이 아니다 (TimingContract §9). 그때 사유에 noisy 셀과 CI만으로의 판정을 적는다.

| 표 | A | B | ρ | 95 % CI | 블록 |
|---|---|---|---|---|---|
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | bullet.Default | jolt.Default | 1.5885 | [1.5181, 1.5942] | 5 |
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.SdkDefault | 2.7093 | [2.6661, 2.7410] | 5 |
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.UE4Compat | 2.6903 | [2.6703, 2.7160] | 5 |
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | jolt.Default | physx.SdkDefault | 1.7055 | [1.7055, 1.7562] | 5 |
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | jolt.Default | physx.UE4Compat | 1.6936 | [1.6862, 1.7590] | 5 |
| M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | physx.SdkDefault | physx.UE4Compat | 0.9930 | [0.9861, 1.0016] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | bullet.Default | jolt.Default | 1.6111 | [1.5881, 1.6128] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | bullet.Default | physx.SdkDefault | 2.6824 | [2.6180, 2.7094] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | bullet.Default | physx.UE4Compat | 2.6584 | [2.6188, 2.6816] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | jolt.Default | physx.SdkDefault | 1.6649 | [1.6485, 1.6864] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | jolt.Default | physx.UE4Compat | 1.6501 | [1.6255, 1.6736] | 5 |
| M3.BoxPile[N=1000] · Normalized · Shipped · T1 | physx.SdkDefault | physx.UE4Compat | 0.9911 | [0.9673, 1.0003] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | bullet.Default | jolt.Default | 1.9589 | [1.9521, 1.9829] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.SdkDefault ⚠ noisy | 3.2975 | [3.1044, 3.3675] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.UE4Compat ⚠ noisy | 3.1713 | [2.9897, 3.2062] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | jolt.Default | physx.SdkDefault ⚠ noisy | 1.6834 | [1.5656, 1.7175] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | jolt.Default | physx.UE4Compat ⚠ noisy | 1.6189 | [1.5127, 1.6352] | 5 |
| M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | physx.SdkDefault ⚠ noisy | physx.UE4Compat ⚠ noisy | 0.9527 | [0.9333, 0.9676] | 7 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | bullet.Default | jolt.Default | 1.9307 | [1.9229, 1.9554] | 5 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | bullet.Default | physx.SdkDefault | 3.2454 | [3.1996, 3.2836] | 5 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | bullet.Default | physx.UE4Compat ⚠ noisy | 3.0453 | [2.8762, 3.0816] | 5 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | jolt.Default | physx.SdkDefault | 1.6810 | [1.6549, 1.6840] | 5 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | jolt.Default | physx.UE4Compat ⚠ noisy | 1.5773 | [1.4957, 1.6012] | 5 |
| M3.BoxPile[N=5000] · Normalized · Shipped · T1 | physx.SdkDefault | physx.UE4Compat ⚠ noisy | 0.9383 | [0.8926, 0.9538] | 5 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | bullet.Default | jolt.Default ⚠ noisy | 0.3078 | [0.2769, 0.3185] | 5 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.SdkDefault | 0.1499 | [0.1486, 0.1526] | 5 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | bullet.Default | physx.UE4Compat ⚠ noisy | 0.1388 | [0.1052, 0.1411] | 5 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | jolt.Default ⚠ noisy | physx.SdkDefault | 0.4871 | [0.4685, 0.5367] | 5 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | jolt.Default ⚠ noisy | physx.UE4Compat ⚠ noisy | 0.4464 | [0.4326, 0.4609] | 7 |
| P0.EmptyStep · Normalized · MatchedAVX2 · T1 | physx.SdkDefault | physx.UE4Compat ⚠ noisy | 0.9254 | [0.7023, 0.9406] | 5 |
| P0.EmptyStep · Normalized · Shipped · T1 | bullet.Default ⚠ noisy | jolt.Default ⚠ noisy | 0.3148 | [0.3064, 0.3285] | 7 |
| P0.EmptyStep · Normalized · Shipped · T1 | bullet.Default ⚠ noisy | physx.SdkDefault ⚠ noisy | 0.1564 | [0.1527, 0.1653] | 7 |
| P0.EmptyStep · Normalized · Shipped · T1 | bullet.Default ⚠ noisy | physx.UE4Compat | 0.1458 | [0.1437, 0.1570] | 5 |
| P0.EmptyStep · Normalized · Shipped · T1 | jolt.Default ⚠ noisy | physx.SdkDefault ⚠ noisy | 0.4967 | [0.4851, 0.5031] | 7 |
| P0.EmptyStep · Normalized · Shipped · T1 | jolt.Default ⚠ noisy | physx.UE4Compat | 0.4693 | [0.4564, 0.4862] | 5 |
| P0.EmptyStep · Normalized · Shipped · T1 | physx.SdkDefault ⚠ noisy | physx.UE4Compat | 0.9479 | [0.9328, 0.9792] | 5 |

| 씬 | A | B | ρ Shipped | ρ MatchedAVX2 | 판정 | 사유 |
|---|---|---|---|---|---|---|
| M3.BoxPile[N=1000] (Normalized) | bullet.Default | jolt.Default | 1.6111 | 1.5885 | **A가 느리다** | - |
| M3.BoxPile[N=1000] (Normalized) | bullet.Default | physx.SdkDefault | 2.6824 | 2.7093 | **A가 느리다** | - |
| M3.BoxPile[N=1000] (Normalized) | bullet.Default | physx.UE4Compat | 2.6584 | 2.6903 | **A가 느리다** | - |
| M3.BoxPile[N=1000] (Normalized) | jolt.Default | physx.SdkDefault | 1.6649 | 1.7055 | **A가 느리다** | - |
| M3.BoxPile[N=1000] (Normalized) | jolt.Default | physx.UE4Compat | 1.6501 | 1.6936 | **A가 느리다** | - |
| M3.BoxPile[N=1000] (Normalized) | physx.SdkDefault | physx.UE4Compat | 0.9911 | 0.9930 | no claim | Shipped: the ratio CI [0.9673, 1.0003] includes 1 |
| M3.BoxPile[N=5000] (Normalized) | bullet.Default | jolt.Default | 1.9307 | 1.9589 | **A가 느리다** | - |
| M3.BoxPile[N=5000] (Normalized) | bullet.Default | physx.SdkDefault | 3.2454 | 3.2975 | no claim | noisy cell(s) after the CV gate (physx.SdkDefault MatchedAVX2): not reportable; the intervals alone give slower |
| M3.BoxPile[N=5000] (Normalized) | bullet.Default | physx.UE4Compat | 3.0453 | 3.1713 | no claim | noisy cell(s) after the CV gate (physx.UE4Compat Shipped, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give slower |
| M3.BoxPile[N=5000] (Normalized) | jolt.Default | physx.SdkDefault | 1.6810 | 1.6834 | no claim | noisy cell(s) after the CV gate (physx.SdkDefault MatchedAVX2): not reportable; the intervals alone give slower |
| M3.BoxPile[N=5000] (Normalized) | jolt.Default | physx.UE4Compat | 1.5773 | 1.6189 | no claim | noisy cell(s) after the CV gate (physx.UE4Compat Shipped, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give slower |
| M3.BoxPile[N=5000] (Normalized) | physx.SdkDefault | physx.UE4Compat | 0.9383 | 0.9527 | no claim | noisy cell(s) after the CV gate (physx.UE4Compat Shipped, physx.SdkDefault MatchedAVX2, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | bullet.Default | jolt.Default | 0.3148 | 0.3078 | no claim | noisy cell(s) after the CV gate (bullet.Default Shipped, jolt.Default Shipped, jolt.Default MatchedAVX2): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | bullet.Default | physx.SdkDefault | 0.1564 | 0.1499 | no claim | noisy cell(s) after the CV gate (bullet.Default Shipped, physx.SdkDefault Shipped): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | bullet.Default | physx.UE4Compat | 0.1458 | 0.1388 | no claim | noisy cell(s) after the CV gate (bullet.Default Shipped, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | jolt.Default | physx.SdkDefault | 0.4967 | 0.4871 | no claim | noisy cell(s) after the CV gate (jolt.Default Shipped, physx.SdkDefault Shipped, jolt.Default MatchedAVX2): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | jolt.Default | physx.UE4Compat | 0.4693 | 0.4464 | no claim | noisy cell(s) after the CV gate (jolt.Default Shipped, jolt.Default MatchedAVX2, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give faster |
| P0.EmptyStep (Normalized) | physx.SdkDefault | physx.UE4Compat | 0.9479 | 0.9254 | no claim | noisy cell(s) after the CV gate (physx.SdkDefault Shipped, physx.UE4Compat MatchedAVX2): not reportable; the intervals alone give faster |

판정이 '빠르다 / 느리다'인 쌍만 문장으로 쓸 수 있다 (noisy 셀이 있는 쌍은 no claim). 세션이 비공식이면 주장도 비공식이다.

## 2c. 바이너리 구성 대조군 (test-<engine> 대 test; M10 측정, 판정은 M11)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

diff = median(test-<engine>) / median(test) − 1. 단일 SDK exe는 manifest `harness.sdks`(빌드에 든 SDK)가 하나인 exe이고, 기준은 같은 레이블의 SDK가 가장 많은 exe다 (M10 수정 라운드 1: preset 이름이 아니라 빌드 내용으로 고른다. `harness.sdks`가 없는 예전 manifest만 preset 이름 test-<engine>으로). 두 값은 다른 세션의 run이라 블록을 공유하지 않으므로 CI는 독립 표본 percentile bootstrap이다 (B = 10000). M11 기준은 |diff| ≤ 1 %이고, 넘으면 그 엔진의 헤드라인을 단일 SDK exe로 다시 잡는다 (02 §14.3). 마지막 열은 그 기준을 미리 대 본 참고값이다: 판정은 M11 (High performance, A-B-A)이 한다.

| 씬 | 엔진 | preset | n / n(test) | median | median (test) | diff | 95 % CI | M11 기준 ≤ 1 % (참고) |
|---|---|---|---|---|---|---|---|---|
| M3.BoxPile[N=1000] (Normalized) | bullet.Default | test-bullet | 5 / 5 | 1.572 ms | 1.580 ms | -0.49 % | [-1.35 %, +0.65 %] | 예 |
| M3.BoxPile[N=1000] (Normalized) | jolt.Default | test-jolt | 5 / 5 | 983.25 µs | 980.62 µs | +0.27 % | [-1.14 %, +0.78 %] | 예 |
| M3.BoxPile[N=1000] (Normalized) | physx.UE4Compat | test-physx | 5 / 5 | 602.80 µs | 594.30 µs | +1.43 % | [-1.49 %, +2.26 %] | 아니오 |
| M3.BoxPile[N=5000] (Normalized) | bullet.Default | test-bullet | 5 / 5 | 10.370 ms | 10.488 ms | -1.13 % | [-1.96 %, -0.02 %] | 아니오 |
| M3.BoxPile[N=5000] (Normalized) | jolt.Default | test-jolt | 5 / 5 | 5.403 ms | 5.432 ms | -0.54 % | [-0.87 %, +0.48 %] | 예 |
| M3.BoxPile[N=5000] (Normalized) | physx.UE4Compat | test-physx | 5 / 7 | 3.427 ms | 3.448 ms | -0.60 % | [-6.66 %, +0.33 %] | 예 |
| P0.EmptyStep (Normalized) | bullet.Default | test-bullet | 5 / 7 | 333 ns | 319 ns | +4.35 % | [-2.58 %, +6.03 %] | 아니오 |
| P0.EmptyStep (Normalized) | jolt.Default | test-jolt | 5 / 7 | 1.04 µs | 1.01 µs | +2.38 % | [-0.13 %, +4.70 %] | 아니오 |
| P0.EmptyStep (Normalized) | physx.UE4Compat | test-physx | 5 / 5 | 2.19 µs | 2.19 µs | +0.20 % | [-1.39 %, +2.42 %] | 예 |

## 3. 셀별 CV와 CV 게이트 (M3.BoxPile 5k 포함 모든 셀)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

M10 CV 게이트 (러너): CV > 2 %인 셀은 추가 블록을 최대 2회 더 돌리고, 그래도 넘으면 **noisy**로 표시한다 (02 §12.5).

| 셀 | n | median | CV | CV 게이트 |
|---|---|---|---|---|
| bullet.Default · M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | 5 | 1.562 ms | 0.45% | 통과 |
| jolt.Default · M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | 5 | 983.62 µs | 1.90% | 통과 |
| physx.SdkDefault · M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | 5 | 576.72 µs | 0.99% | 통과 |
| physx.UE4Compat · M3.BoxPile[N=1000] · Normalized · MatchedAVX2 · T1 | 5 | 580.79 µs | 0.65% | 통과 |
| bullet.Default@test · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 1.580 ms | 0.61% | 통과 |
| bullet.Default@test-bullet · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 1.572 ms | 0.60% | 통과 |
| jolt.Default@test · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 980.62 µs | 0.72% | 통과 |
| jolt.Default@test-jolt · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 983.25 µs | 0.32% | 통과 |
| physx.SdkDefault · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 588.99 µs | 1.18% | 통과 |
| physx.UE4Compat@test · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 594.30 µs | 0.77% | 통과 |
| physx.UE4Compat@test-physx · M3.BoxPile[N=1000] · Normalized · Shipped · T1 | 5 | 602.80 µs | 1.19% | 통과 |
| bullet.Default · M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | 5 | 10.652 ms | 0.57% | 통과 |
| jolt.Default · M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | 5 | 5.438 ms | 0.75% | 통과 |
| physx.SdkDefault · M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | 7 | 3.200 ms | 3.20% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| physx.UE4Compat · M3.BoxPile[N=5000] · Normalized · MatchedAVX2 · T1 | 7 | 3.359 ms | 3.06% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| bullet.Default@test · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 10.488 ms | 0.42% | 통과 |
| bullet.Default@test-bullet · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 10.370 ms | 0.71% | 통과 |
| jolt.Default@test · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 5.432 ms | 0.49% | 통과 |
| jolt.Default@test-jolt · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 5.403 ms | 0.43% | 통과 |
| physx.SdkDefault · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 3.232 ms | 0.93% | 통과 |
| physx.UE4Compat@test · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 7 | 3.448 ms | 3.49% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| physx.UE4Compat@test-physx · M3.BoxPile[N=5000] · Normalized · Shipped · T1 | 5 | 3.427 ms | 0.46% | 통과 |
| bullet.Default · P0.EmptyStep · Normalized · MatchedAVX2 · T1 | 5 | 325 ns | 1.20% | 통과 |
| jolt.Default · P0.EmptyStep · Normalized · MatchedAVX2 · T1 | 7 | 1.03 µs | 4.90% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| physx.SdkDefault · P0.EmptyStep · Normalized · MatchedAVX2 · T1 | 5 | 2.16 µs | 0.88% | 통과 |
| physx.UE4Compat · P0.EmptyStep · Normalized · MatchedAVX2 · T1 | 7 | 2.32 µs | 12.01% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| bullet.Default@test · P0.EmptyStep · Normalized · Shipped · T1 | 7 | 319 ns | 3.55% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| bullet.Default@test-bullet · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 333 ns | 1.51% | 통과 |
| jolt.Default@test · P0.EmptyStep · Normalized · Shipped · T1 | 7 | 1.01 µs | 2.40% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| jolt.Default@test-jolt · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 1.04 µs | 1.18% | 통과 |
| physx.SdkDefault · P0.EmptyStep · Normalized · Shipped · T1 | 7 | 2.04 µs | 2.47% ⚠ | **noisy** (추가 블록 뒤에도 > 2 %) |
| physx.UE4Compat@test · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 2.19 µs | 1.28% | 통과 |
| physx.UE4Compat@test-physx · P0.EmptyStep · Normalized · Shipped · T1 | 5 | 2.19 µs | 1.13% | 통과 |

## 4. 스케일링과 오버헤드 모델 (원시, 비공식)

> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e) — High performance 아님** · 환경 게이트 우회 **`--allow-balanced`**.

| series | scene | N | median | α (t = c·N^α) |
|---|---|---|---|---|
| bullet.Default · Normalized · T1 | M3.BoxPile | 1000, 5000 | 1.562 ms, 10.652 ms | 1.193 |
| bullet.Default@test · Normalized · T1 | M3.BoxPile | 1000, 5000 | 1.580 ms, 10.488 ms | 1.176 |
| bullet.Default@test-bullet · Normalized · T1 | M3.BoxPile | 1000, 5000 | 1.572 ms, 10.370 ms | 1.172 |
| jolt.Default · Normalized · T1 | M3.BoxPile | 1000, 5000 | 983.62 µs, 5.438 ms | 1.062 |
| jolt.Default@test · Normalized · T1 | M3.BoxPile | 1000, 5000 | 980.62 µs, 5.432 ms | 1.064 |
| jolt.Default@test-jolt · Normalized · T1 | M3.BoxPile | 1000, 5000 | 983.25 µs, 5.403 ms | 1.059 |
| physx.SdkDefault · Normalized · T1 | M3.BoxPile | 1000, 5000 | 576.72 µs, 3.200 ms | 1.065 |
| physx.SdkDefault · Normalized · T1 | M3.BoxPile | 1000, 5000 | 588.99 µs, 3.232 ms | 1.058 |
| physx.UE4Compat · Normalized · T1 | M3.BoxPile | 1000, 5000 | 580.79 µs, 3.359 ms | 1.090 |
| physx.UE4Compat@test · Normalized · T1 | M3.BoxPile | 1000, 5000 | 594.30 µs, 3.448 ms | 1.092 |
| physx.UE4Compat@test-physx · Normalized · T1 | M3.BoxPile | 1000, 5000 | 602.80 µs, 3.427 ms | 1.080 |


오버헤드 모델 t = a + b·N (P0/P1/M3 셀의 median, 최소제곱):

| series | a (프레임당 고정) | b (바디당) | R² | 점 |
|---|---|---|---|---|
| bullet.Default · Normalized · T1 | -270.20 µs | 2170.8 ns | 0.9971 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| bullet.Default@test · Normalized · T1 | -246.34 µs | 2134.6 ns | 0.9975 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| bullet.Default@test-bullet · Normalized · T1 | -238.76 µs | 2109.8 ns | 0.9976 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| jolt.Default · Normalized · T1 | -48.84 µs | 1094.8 ns | 0.9996 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| jolt.Default@test · Normalized · T1 | -49.78 µs | 1093.9 ns | 0.9996 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| jolt.Default@test-jolt · Normalized · T1 | -45.70 µs | 1087.4 ns | 0.9997 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.SdkDefault · Normalized · T1 | -28.78 µs | 644.2 ns | 0.9996 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.SdkDefault · Normalized · T1 | -26.05 µs | 650.1 ns | 0.9997 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.UE4Compat · Normalized · T1 | -41.88 µs | 677.9 ns | 0.9992 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.UE4Compat@test · Normalized · T1 | -44.04 µs | 696.1 ns | 0.9992 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |
| physx.UE4Compat@test-physx · Normalized · T1 | -38.02 µs | 691.1 ns | 0.9994 | M3.BoxPile[N=1000], M3.BoxPile[N=5000], P0.EmptyStep |

a < 0은 비용이 N에 대해 선형보다 빨리 자란다는 뜻이다 (α > 1, 접촉 쌍이 바디 수보다 빨리 는다). 선형 모델은 M8 형식일 뿐 물리적 고정 비용의 추정이 아니다. P1 셀(N 10, 100)이 있으면 a가 작은 N 쪽에 맞는다.

## 5. 거부된 run과 재시도

| run | 셀 | 결과 | 사유 |
|---|---|---|---|
| `s_start_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_middle_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_end_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_start_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_middle_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_end_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_start_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | contaminated | sentinel run: the session's drift check (02 12.5), never a cell value; exit.json 4 (contaminated_host); contaminated host; not headline-valid |
| `s_start_0_b6cbdd8d_a1` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_middle_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |
| `s_end_0_b6cbdd8d` | jolt.Default\|Normalized\|M3.BoxPile[N=1000]\|T1 | ok | sentinel run: the session's drift check (02 12.5), never a cell value |

## 6. 방법

- 러너: 셀 = target × profile × scene × T, 반복 r마다 seed 고정 블록 셔플(splitmix64 + Fisher-Yates), run마다 fresh process, 쿨다운, 타임아웃. 판정은 `exit.json`이다: 없으면 프로세스가 끝까지 가지 못한 실패이고 원시 종료 코드로 성공을 판정하지 않는다 (02 §12.2, DL52). 종료 코드 4는 같은 run을 fresh process로 최대 2번 다시 돌리고, 모든 시도가 4이면 셀이 `contaminated`다.
- 채택 조건 (validate): `exit.json` 0, 두 스키마 통과, Bench 모드, 계측 tier 0, 게이트(G0, G9, G10a, G10b) 통과, 오염 없음, `timing_valid_for_headline` 참, comparability key가 manifest 필드와 일치, 유효한 적합성 스탬프 (R9; `--allow-unstamped`면 받되 모든 표에 표시). sentinel run은 셀 값이 아니다. 표는 comparability key마다 하나이고, 그 안에서 플래그 프로필이나 `desc_sha256`이 섞이면 표 자체를 거부하고 통계를 내지 않는다 (§3에도 '표 거부'로만 적는다). 같은 시리즈가 여러 preset(test, test-<engine>)에서 오면 `label@preset`으로 나눈다 (바이너리 구성 대조군).
- M10: median의 95 % CI는 percentile bootstrap (B = 10000), 비율 ρ의 CI는 블록(러너의 반복) bootstrap이다. 비교 문장은 §2b의 주장 규칙을 따른다. CV 게이트와 sentinel은 러너가 세션 안에서 적용했다 (§1).

