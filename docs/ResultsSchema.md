# 결과 포맷 (Results Schema)

> **작성일:** 2026-10-22 (M7)
> **상태:** M7 벤치 출력의 정본이다. 스키마 파일은 `Config/Schemas/manifest.schema.json`(physicstest.run/2)과 `Config/Schemas/exit.schema.json`(physicstest.exit/1)이고, `Tools/validate_manifest.py`가 검증한다. session·matrix 스키마는 M8, stamp 스키마는 M10이다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §12.3 · [03_WorkPlan.md](03_WorkPlan.md) M7, M8 · [TimingContract.md](TimingContract.md) · [Scenes.md](Scenes.md) · [Abi.md](Abi.md) · [Progress.md](Progress.md) M7

---

## 1. 디렉터리와 공통 규칙

run 하나는 디렉터리 하나다: `-Out=<dir>`, 없으면 `Saved/Results/<Session>/<run_id>/` (`run_id`는 ULID, 시간순 정렬). `Saved/`는 커밋하지 않는다.

| 파일 | 언제 | 형식 |
|---|---|---|
| `manifest.json` | 백엔드가 초기화된(`InitGamePhys` 성공) 뒤의 모든 결과: ok, 엔진 오류, 용량 초과, 오염, preflight 실패(엔진 옵션 preflight의 거부 포함: 씬이 없어 `scene.built` false) | JSON, physicstest.run/2 (§2). 사용 오류와 백엔드 없음(종료 코드 6)은 `exit.json`만 쓴다 |
| `exit.json` | **언제나 마지막에** (명령줄 오류도 `-Out`이 있으면. `-Conformance`·`-Gate=`도 언제나) | JSON, physicstest.exit/1 (§3) |
| `timings.csv` | 씬이 만들어진 run (preflight에서 멈추면 머리 줄뿐) | CSV, 프레임당 한 줄 (§4) |
| `hashes.csv` | preflight를 통과한 run | CSV (§5) |
| `states.ptst`, `states.dtype.json` | `StatesEvery > 0` | 이진 + JSON (§6) |
| `contacts.ptcn` | contacts tier이고 백엔드가 `PT_CAP_CONTACT_POINTS`를 선언 | 이진 (§7) |
| `effective_settings.jsonl` | 씬 구성 직후 (preflight 전) | JSON lines (§8) |
| `effective_settings_after.jsonl` | 마지막 프레임 뒤 | JSON lines (§8) |
| `scene.json` | 씬이 만들어진 run | 정규 JSON (`scene_sha256`의 입력) |
| `teardown.json` | `exit.json` 다음, teardown 뒤 (teardown 중 Fatal이면 그때) | JSON, physicstest.teardown/1 (§9) |
| `gate_g8.json`, `gate_g11.json` | `-Gate=G8` / `-Gate=G11` | JSON, physicstest.gate/1 (§10) |
| `conformance.json` | `-Conformance` (M6) | JSON, physicstest.conformance/1 |

- **"N 프레임마다"**: (f + 1) % N == 0인 프레임 f(0부터)와 마지막 프레임이다. N = 0이면 마지막 프레임만이다. 그래서 `HashEvery = 0`(헤드라인)인 `hashes.csv`는 마지막 프레임 한 줄이고, `HashEvery = 1`은 모든 프레임이다.
- 모든 이진 파일은 little-endian이다. 단위는 cm, kg, s, rad, Z-up.
- `exit.json`은 임시 파일에 쓴 뒤 `MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)`로 바꿔 넣는다. 읽는 쪽은 반쯤 쓰인 파일을 보지 않는다. `exit.json`이 없으면 그 프로세스는 끝까지 가지 못한 것이다 (M8 러너는 원시 종료 코드로 성공을 판정하지 않는다).

## 2. `manifest.json` (physicstest.run/2)

run/1은 M3~M6의 최소 벤치 manifest다 (host·clock·order·alloc·gates 그룹이 없고 `timing_valid_for_headline`이 언제나 false). run/2에서 그룹이 늘었고, 기존 필드의 뜻은 그대로다. 예외 둘: M4의 `run.headline_eligible`·`run.headline_ineligible_reasons`는 없어졌고, 그 조건은 최상위 `timing_valid_for_headline`·`timing_invalid_reasons`(TimingContract.md §6)에 들어갔다. M3의 `run.injected_engine_error_bits`도 없어졌다 (`run.test_injections.engine_error_bits`, M7 수정 라운드 0). 그룹에는 필드가 더해질 수 있다 (스키마가 추가 속성을 허용한다).

**모양은 언제나 같다 (M7 수정 라운드 0):** 거부된 run도 모든 그룹을 쓴다. run이 닿지 못한 값은 null이다: 엔진 옵션 preflight가 거부하면 `scene.built` false이고 `scene.desc_sha256`·`options_sha256`·`num_bodies`·`num_dynamic_bodies`·`start_asleep_skipped`, `run.scene_desc_instrument_flags`·`dt_engine_last_plan_s`·`frame_driver`·`sync_policy`·`async_interpolated`·`steps_total`·`sim_time_s`, `step_stats_final`이 null이고 `effective`·`gates`는 빈 배열, `memory.backend_scene`은 `error`다. 스키마의 `allOf`가 `scene.built` true인 run에는 이 값들을 요구하고, false인 run은 `status` preflight_failed와 `preflight_error` "engine options: …"를 요구한다.

| 그룹 / 필드 | 내용 |
|---|---|
| `schema`, `status`, `exit_code` | `physicstest.run/2`; ok, preflight_failed, engine_error, capacity_exceeded, contaminated_host; 0, 2, 3, 4, 5 |
| `timing_valid_for_headline`, `timing_invalid_reasons` | 헤드라인 자격과 그 사유 목록 ([TimingContract.md](TimingContract.md) §6) |
| `unofficial`, `timing_label` | M10 전에는 언제나 true와 사유 문장 |
| `comparability_key`, `conformance_stamp` | M10 전에는 null |
| `run_id`, `session`, `utc`, `command_line`, `wall_time_s` | |
| `harness` | `git_sha`, `dirty`, `git_sha_source` = `build`(빌드 시점: `.git/HEAD`·`index`·`logs/HEAD`가 바뀌면 다시 만든다), `git_sha_configure`(비교용 configure 시점 값), `exe`, `exe_sha256`, `preset`, `do_check`. **한계:** `dirty`는 그 세 git 파일이 바뀔 때만 다시 계산한다. 소스만 고치고 git 명령 없이 빌드하면 직전 값이 남는다 (`cmake/GitInfo.cmake`). 바이너리의 정체는 `exe_sha256`(와 M10 스탬프)가 정본이다 |
| `build` | `flag_profile`(Shipped / MatchedAVX2), `configuration`, `msvc`, `cmake`, `crt`, `lto`, `flags_json_sha256`, `flags_json_tu_count`, `flags_json_violations` |
| `backend` | `name`, `display_name`, `variant`, `label`, `sdk_version`, `sdk_sha`, `build_config`, `module_file`·`module_sha256`(DLL 백엔드만, 아니면 null), `abi_version`, `capabilities`, `module_flags`, `required_effective_keys`, `fidelity_notes`, `tu_self_report`(어댑터 TU 자기보고, G0 입력) |
| `profile` | `physics`, `profile_file`, `variant_file`, `mass_rule`, `physics_sha256`, `profile_sha256`, `variant_sha256`, `warnings`, `resolved[]`(key, value, unit, origin, origin_name, applied). 명령줄의 run 옵션(`-Threads`, `-RunMode`, `-Instrument` 등)은 physics 키가 아니어서 여기 없고 `run`·`threads`에 쓴다 |
| `effective`, `effective_module`, `missing_effective_keys`, `bodies_sampled` | effective 설정(§8), 빠진 필수 키, 동적 바디 세 개의 질량·관성 텐서 비교 |
| `scene` | `name`, `version`, `kind`, `params`, `seed`, `scene_sha256`, `built`(씬을 만들었는지), `desc_sha256`, `desc_sha256_scope`, `options_sha256`, `num_bodies`, `num_dynamic_bodies`, `start_asleep_skipped`, `capacity_hints`, `windows[]`(씬이 선언한 윈도: name, first, last) |
| `run` | `mode`, `instrumentation[]`(tier 이름), `instrument_flags`, `scene_desc_instrument_flags`(백엔드가 실제로 받은 값), `frames`, `frames_run`, `dt_s`, `dt_engine_s` = (double)(float)dt, `dt_engine_last_plan_s`, `tick_mode`, `clamp_rule`, `frame_driver`, `sync_policy`, `hash_every`, `states_every`, `counters_every`, `capture_every`, `pre_roll_s`, `async_interpolated`, `steps_total`, `sim_time_s`, `test_injections{}`(시험 전용 주입: `engine_error_bits`, `preflight_fault`, `scene_instrument_flags`, `state_perturbation_frame`) |
| `threads` | `T`, `affinity_mask`(16진 문자열), `affinity_applied`, `affinity_before`, `logical_cpus[]`, `smt_sibling_mask`, `series`(`p_cores_one_lp` / `p_cores_smt` / `with_e_cores`, 02 §9.6), `cores`, `smt_pair_cores`, `e_cores`, `mapping`, `engine_native`(`threads.workers` READBACK: workers, native, source), `engine_thread_settings{}`, `scaling_note`("cpu_eff 미검증 (M10)" 등), `census_init`·`census_window`·`foreign_cycles_fraction`(M10, null) |
| `platform` | `cpu`, `logical_processors`, `p_cores[]`, `os_build`, `priority_class`, `priority_before`, `priority_set`, `throttling`, `timer_resolution`(requested_ms, applied, before, after), `host_control_errors`, `power_scheme`, `power_scheme_guid`, `power_scheme_high_performance`, `qpc_hz`, `cpu_mhz[]`, `system_busy_other_pct`, `host_load`(valid, window, wall_s, unit, own_process_pct, `core_mask`(마스크 CPU + SMT 형제), `cores`, `core_busy_other_pct`(종료 코드 4의 측정값), mask_busy_other_pct, smt_sibling_busy_pct, system_busy_other_pct, per_cpu[](cpu, in_mask, busy_pct), policy, `threshold_pct`(코어당), `threshold_total_pct`(코어당 × cores: core_busy_other_pct와 비교), contaminated; [TimingContract.md](TimingContract.md) §5.1) |
| `clock` | `source`, `qpc_hz`, `qpc_tick_ns`, `read_overhead_ns`, `read_overhead_method`, `tsc_hz`(M10, null) |
| `order` | `repeat_index`, `order_index`, `block_seed` (M8 러너가 넘긴다) |
| `phases_ns` | `backend_init`, `option_preflight`, `scene_create`, `preflight`, `prepare`, `pre_roll`, `frame0`, `loop_after_frame0`, `teardown`(null: `teardown.json`) |
| `memory` | `process`(peak·현재 commit, working set), `backend_scene`(`PtMemoryStats`: -1은 셀 수 없음) |
| `alloc` | `hook_installed`, `hook`, `hook_scope`(훅은 exe 이미지 안에서만 묶인다: DLL 백엔드의 할당은 보이지 않는다), `tick_new_calls`, `tick_delete_calls`, `tick_first_allocating_frame`, `frame0_new_calls`, `loop_new_calls`, `backend_allocs_in_loop`·`backend_allocs_frame0`·`backend_allocs_after_frame0`(엔진 자기 할당자의 호출 수: frame 0 직전 → frame 0 Tick 뒤 → 마지막 Tick 뒤. null = 백엔드가 세지 못함), `backend_note` ([TimingContract.md](TimingContract.md) §4.1) |
| `summary` | `unit` = ns, `frame0_frame_ns`(frame이 없으면 null), `windows.<name>.{first, last, declared_last, clipped, count, frame_ns, step_ns, push_ns, pull_ns}`, `cpu_eff`(M10, null). 통계는 각각 count, mean, median, p95, p99, min, max (nearest-rank). frame 0은 어느 윈도에도 넣지 않는다. **run이 윈도보다 짧으면 (M7 수정 라운드 0):** `last`는 이 run에서 윈도가 실제로 덮은 마지막 프레임 = min(`declared_last`, frames_run − 1)이고 `clipped`가 true다. 프레임이 하나도 없는 윈도는 `count` 0, `last`와 통계 넷이 null이다 (0 ns가 아니다. 스키마가 count 0의 통계 객체를 거부한다) |
| `determinism` | `h_native`(마지막 프레임, 백엔드 해시; 엔진 간·빌드 간 비교 금지), `h_native_frame`, `h_quantized`, `h_state`, `engine_error_bits`, `capacity_error_bits_declared`, `capacity_error_bits`, `other_error_bits` |
| `gates[]` | preflight 게이트 G0, G9, G10a: `id`, `result`(Pass/Fail/Skip), `checks`, `failures[]`, `notes[]` |
| `step_stats_final`, `recorders`, `deferred_fields`, `outputs[]` | 마지막 `GetStepStats`(씬이 없으면 null), 기록기별 행·프레임 수(`hashes`, `states`, `contacts`, `phases`: phase timer가 값을 낸 프레임 수와 타이머 이름), M10 필드 목록, 쓴 파일 |
| `preflight_error` | preflight가 실패했을 때만 |

**해시 정의 (02 §12.3):**

| 해시 | 덮는 것 | 비교 범위 |
|---|---|---|
| `scene_sha256` | `scene.json`: 씬 이름, 버전, 파라미터, seed | 빌드·엔진 무관 |
| `desc_sha256` | 엔진 독립 입력: 머티리얼, 셰이프, actor create info, 중력, dt 시퀀스 | 한 빌드 구성 안의 엔진·변형·**플래그 프로필**(test = test-avx2). dbg와 dev/test는 다르다 ([Scenes.md](Scenes.md) §1) |
| `options_sha256` | `PtSceneDesc`의 나머지 필드와 엔진 옵션, frame driver | 엔진마다 다를 수 있다 |
| `physics_sha256` | 리졸브된 물리 프로필 (변형 키 제외) | 한 표 안에서 같아야 함 |
| `variant_sha256` | 백엔드 변형 ini 내용 | 시리즈 레이블 |
| `h_native` | 백엔드의 상태 해시 (`ComputeNativeStateHash`) | 같은 exe·같은 T·같은 엔진 (DET1) |
| `h_quantized` | FNV-1a 64: 바디마다 actor 위치를 1e-4 cm로, 회전을 w ≥ 0 정규화 후 1e-6으로 반올림한 int64 7개 | 같은 엔진, 마지막 자리 잡음 흡수용. 엔진·빌드 간 비교 금지 |
| `h_state` | FNV-1a 64: 바디마다 `states.ptst` 레코드의 앞 105 바이트(double 13개 + object state) | 하네스가 받은 상태의 비트 동일성 (G8, G11) |

## 3. `exit.json` (physicstest.exit/1)

| 필드 | 내용 |
|---|---|
| `exit_code`, `meaning` | 0, 2, 3, 4, 5, 6과 이름 |
| `status` | ok, usage_error, preflight_failed, gate_failed, engine_error, fatal, contaminated_host, capacity_exceeded, backend_unavailable |
| `category` | "", usage, preflight, gate, engine, host, capacity, backend. 종료 코드 2는 사용 오류(usage)와 preflight·게이트 실패를 함께 쓰므로 이 필드로 나눈다 |
| `reason` | 사람이 읽는 사유 (Fatal이면 Fatal 로그 줄) |
| `run_id`, `utc`, `pid`, `terminate_process`, `outputs[]` | `terminate_process`가 참이면 UE_RUNTIME 모듈이 로드되어 프로세스가 `TerminateProcess`로 끝났다 (teardown 뒤, 또는 Fatal이면 이 파일을 쓴 직후). 백엔드를 로드한 뒤 끝나는 모든 경로가 이 값을 바르게 적는다 |

종료 코드가 여럿 해당하면 2(루프가 돌지 않음) > 3 > 5 > 4 순서다. Fatal(3)은 `FPlatformMisc::SetFatalExitCallback` 훅이 쓴다. 훅은 run의 마지막 `exit.json`을 쓰기 전까지만 이 파일을 쓴다. teardown 중의 Fatal은 이 파일을 바꾸지 않고 `teardown.json`에 남는다 (§9).

## 4. `timings.csv`

프레임당 한 줄. 열: `frame, sim_time_s, num_steps, push_ns, setup_ns, start_ns, wait_ns, end_ns, step_ns, pull_ns, frame_ns, frame_qpc_ticks, solver_ns, awake_bodies, contact_pairs, contact_points, islands, error_bits`.

- 버킷의 뜻은 [TimingContract.md](TimingContract.md) §2다. `frame_ns` = push + step + pull.
- `solver_ns`: phases tier이고 백엔드가 phase timer를 선언할 때만 값이 있다. 아니면 비어 있다.
- `awake_bodies`: 호스트가 받은 상태로 센 깨어 있는 동적 바디 수 (언제나 있음).
- `contact_pairs`, `contact_points`, `islands`: counters 프레임의 `GetStepStats` 값, 아니면 -1. 백엔드가 모르면 -1이다.
- `error_bits`: 그 프레임의 엔진 오류 bit.

## 5. `hashes.csv`

열: `frame, h_native, h_quantized, h_state` (16진수, `0x` + 16자리). `h_native`는 `PT_CAP_NATIVE_HASH`가 없으면 비어 있다. 행은 §1의 "N 프레임마다" 규칙을 따른다 (`HashEvery`).

## 6. `states.ptst` (PTST v1)과 `states.dtype.json`

| 위치 | 크기 | 필드 |
|---|---|---|
| 0 | 4 | magic `PTST` |
| 4 | u32 | version 1 |
| 8 | u32 | flags (bit 0 = frame_count 확정. 닫을 때 쓴다) |
| 12 | u32 | body_count |
| 16 | u32 | frame_count |
| 20 | u32 | record_stride = 112 |
| 24 | u32 | units = 1 (cm, kg, s, rad) |
| 28 | u32 | frame_header_size = 16 |
| 32 | f64 | dt (dt_engine) |
| 40 | 8 | `scene_sha256`의 앞 8바이트 |
| 48 | 8 | `desc_sha256`의 앞 8바이트 |
| 56 | u64 | reserved 0 |

그 뒤 기록한 프레임마다 프레임 헤더 `{u32 frame, u32 step, f64 sim_time}`(16 B)와 바디 레코드 body_count개(생성 순서 = 바디 인덱스)가 이어진다. 바디 레코드(112 B): `f64 position[3]`(actor 원점, cm), `f64 rotation[4]`(x, y, z, w), `f64 linear_velocity[3]`(cm/s; 동적 바디는 COM, kinematic은 actor 원점: `PtBodyState`), `f64 angular_velocity[3]`(rad/s), `u8 object_state`(1 Sleeping, 2 Kinematic, 3 Static, 4 Dynamic), `u8 pad[7]`(0).

`states.dtype.json`(physicstest.states_dtype/1)은 같은 배치를 numpy dtype 목록으로 적는다 (header, frame_header, body, frame_bytes). 프레임 블록 i는 `header_size + i × frame_bytes`에서 시작한다.

## 7. `contacts.ptcn` (PTCN v1)

헤더 32 B: magic `PTCN`, u32 version 1, u32 record_size 128(`sizeof(PtContactPoint)`), u32 frame_count(닫을 때), u32 flags(bit 0 = frame_count 확정), u32 reserved[3]. 그 뒤 기록한 프레임마다 `{u32 frame, u32 count}`와 `PtContactPoint` count개를 백엔드가 쓴 그대로 둔다. 규약은 v1.0 ABI다 ([Abi.md](Abi.md) §7.1, §9.1 결정 2·3): Normal은 Body1 위에서 Body0 쪽, Phi < 0은 침투, flag는 ACTIVE / ACTIVE_APPROX / SPECULATIVE 중 하나 이하, PointOn은 그 스텝의 narrow phase 자세 기준. 어느 자세인지는 effective 키 `contact.point_pose`가 선언한다 (0 = 스텝 시작 자세: Jolt, Bullet, PhysX; 1 = 예측 자세: Chaos).

## 8. `effective_settings.jsonl`

한 줄에 키 하나: `{"key", "native", "native_unit", "canonical", "unit", "source"}`. source는 READBACK(엔진에서 읽은 값), SET_ONLY(어댑터가 설정한 값), COMPILE_TIME, ENGINE_DEFAULT다 ([Abi.md](Abi.md) §5.14). `canonical`은 하네스 단위의 숫자 또는 null이다. `effective_settings.jsonl`은 preflight가 읽은 값이고, `effective_settings_after.jsonl`은 마지막 프레임 뒤의 값(temp allocator peak, 누적 오류 bit 같은 run 결과)이다.

## 9. `teardown.json` (physicstest.teardown/1)

`teardown_ns`, `term_game_phys`(`TermGamePhys`의 결과 이름), `fatal`(teardown 중 Fatal의 로그 줄, 없으면 null), `terminate_process`. `exit.json` 다음에 쓰므로 teardown이 실패해도 run의 종료 코드는 그대로다. teardown 중 Fatal이면 Fatal 훅이 이 파일을 `fatal`과 함께 쓰고(`teardown_ns`, `term_game_phys`는 null) 프로세스를 run의 종료 코드로 `TerminateProcess`한다 (M7 수정 라운드 0, DL52).

## 10. `gate_g8.json`, `gate_g11.json` (physicstest.gate/1)

`gate`, `result`(pass / fail), `command_line`, `utc`, `runs[]`(label, out_dir, started, timed_out, process_exit_code, exit_json_code, status, h_native, h_state, desc_sha256, T, frames_run, wall_s, spawn_error), `failures[]`, `notes[]`, `injected_gate_fault_frame`(시험용 `-InjectGateFault`를 썼을 때만: 음성 대조, [TimingContract.md](TimingContract.md) §8). 자식 run 디렉터리(`run0`..`runN-1`, 또는 `off`·`on`)에 자식의 결과 전부와 `child.log`(표준 출력·오류)가 있다. 게이트 프로세스 자신의 `exit.json`도 같은 디렉터리에 있다.

## 11. 검증

```
.venv\Scripts\python.exe -I Tools\validate_manifest.py --require-fields <run dir>...
```

- `manifest.json`을 `Config/Schemas/manifest.schema.json`, `exit.json`을 `Config/Schemas/exit.schema.json`으로 검증한다 (jsonschema, Draft 2020-12).
- `--require-fields`는 03 M7 완료 기준 1의 필드(mode, instrumentation, timing_valid_for_headline, dt_engine_s, threads, platform의 전원 계획, clock)가 채워졌는지도 본다.
- CTest `PhysicsTest.Bench.ManifestSchema`가 `M3.BoxPile` 5k run(윈도보다 짧아 headline이 clipped, settled가 비어 통계 null)과 거부된 run 둘(엔진 옵션 preflight: `scene.built` false, 빠진 필수 키)의 manifest를 검증한다. 일부러 망가뜨린 사본 넷이 실패하는지도 확인한다: `run.mode` = "Fast", `platform.power_scheme`(전원 계획 이름) 삭제, `platform.power_scheme_guid` = "x", 빈 윈도에 0 ns 통계.
