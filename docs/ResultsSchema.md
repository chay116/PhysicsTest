# 결과 포맷 (Results Schema)

> **작성일:** 2026-10-22 (M7. M9: `scene_desc.json`, `preflight_diff.json`, `native_draw.csv`, 매트릭스 `purpose` behavior·`scenes[].bench_args`, 거동 리포트. M10: TSC·센서스·스탬프·comparability 필드, `timings.csv` 열 다섯, 스탬프 파일, 세션의 스탬프·CV 게이트·sentinel)
> **상태:** M7 벤치 출력과 M8 세션 출력의 정본이다. 스키마 파일은 `Config/Schemas/manifest.schema.json`(physicstest.run/2), `Config/Schemas/exit.schema.json`(physicstest.exit/1), M8의 `session.schema.json`(physicstest.session/1)과 `matrix.schema.json`(physicstest.matrix/1), M10의 `stamp.schema.json`(physicstest.stamp/1)이고, `Tools/validate_manifest.py`와 `Tools/ptbench`가 검증한다.
> **관련 문서:** [02_Architecture.md](02_Architecture.md) §12.3, §12.4, §13 · [03_WorkPlan.md](03_WorkPlan.md) M7, M8, M10 · [TimingContract.md](TimingContract.md) · [Scenes.md](Scenes.md) · [Abi.md](Abi.md) · [Progress.md](Progress.md) M7, M10

---

## 1. 디렉터리와 공통 규칙

run 하나는 디렉터리 하나다: `-Out=<dir>`, 없으면 `Saved/Results/<Session>/<run_id>/` (`run_id`는 ULID, 시간순 정렬). `Saved/`는 커밋하지 않는다.

| 파일 | 언제 | 형식 |
|---|---|---|
| `manifest.json` | 백엔드가 초기화된(`InitGamePhys` 성공) 뒤의 모든 결과: ok, 엔진 오류, 용량 초과, 오염, preflight 실패(엔진 옵션 preflight의 거부 포함: 씬이 없어 `scene.built` false) | JSON, physicstest.run/2 (§2). 씬을 만든 뒤의 사용 오류(M8 `usage_error`: 시험 전용 주입이 효과를 낼 수 없는 run, 종료 코드 2)도 manifest를 쓴다. 명령줄의 사용 오류, 백엔드 없음(종료 코드 6), 지원하지 않는 씬(M9 `unsupported`, §3), Fatal은 `exit.json`만 쓴다 |
| `exit.json` | **언제나 마지막에** (명령줄 오류도 `-Out`이 있으면. `-Conformance`·`-Gate=`도 언제나) | JSON, physicstest.exit/1 (§3) |
| `timings.csv` | 씬이 만들어진 run (preflight에서 멈추면 머리 줄뿐) | CSV, 프레임당 한 줄 (§4) |
| `hashes.csv` | preflight를 통과한 run | CSV (§5) |
| `states.ptst`, `states.dtype.json` | `StatesEvery > 0` | 이진 + JSON (§6) |
| `contacts.ptcn` | contacts tier이고 백엔드가 `PT_CAP_CONTACT_POINTS`를 선언 | 이진 (§7) |
| `native_draw.csv` (M9) | native_debug_draw tier이고 백엔드가 `PT_CAP_NATIVE_DEBUG_DRAW`를 선언 (viz run이 아니면 모든 행이 0) | CSV (§7.1) |
| `effective_settings.jsonl` | 씬 구성 직후 (preflight 전) | JSON lines (§8) |
| `effective_settings_after.jsonl` | 마지막 프레임 뒤 | JSON lines (§8) |
| `scene.json` | 씬이 만들어진 run | 정규 JSON (`scene_sha256`의 입력) |
| `scene_desc.json` (M9) | `StatesEvery > 0`인 run, 씬을 만든 직후 (프레임 0 전, 타이밍 밖) | JSON, physicstest.scene_desc/1: `scenario`, `version`, `params`, `desc_sha256`, `units`, `gravity`, `materials[]`(index, name, friction, static_friction, restitution, friction_combine, restitution_combine: 백엔드가 받은 값, 강제 오버라이드 뒤), `bodies[]`(index, name, motion, mass, inertia_diagonal, com_local, initial_pose, linear_damping, angular_damping, `shapes[]`(type sphere/box/capsule/convex, local, material, radius, half_length, half_extent)), `joints[]`(index, name, type ball/hinge, body1, body2(−1 = 월드), frame1, frame2, disable_collision, simulated). 자세는 actor 원점, 쿼터니언 (x, y, z, w). 거동 분석이 `states.ptst`와 함께 쓴다 ([Scenes.md](Scenes.md) §1) |
| `preflight_diff.json` (M9) | preflight를 돈 run (G9이 effective 덤프를 비교한 run) | JSON, physicstest.preflight_diff/1: `backend`, `variant`, `profile`, `counts{status: n}`, `keys[]`(key, source, effective(canonical), native, rule(tolerances.json G9 기대식), resolved(그 run에서 리졸브한 기대값), status, note). status = match / mismatch / unchecked / skipped / no_rule / not_readback ([02_Architecture.md](02_Architecture.md) §10 "preflight diff") |
| `teardown.json` | `exit.json` 다음, teardown 뒤 (teardown 중 Fatal이면 그때) | JSON, physicstest.teardown/1 (§9) |
| `gate_g8.json`, `gate_g11.json` | `-Gate=G8` / `-Gate=G11` | JSON, physicstest.gate/1 (§10) |
| `conformance.json` | `-Conformance` (M6) | JSON, physicstest.conformance/1. M10: `stamp`(written, reason, key_sha256, key, path, valid)와 G10b 자식 run(`g10b_run/`, 다시 돌면 `g10b_run_2/`, `g10b_run_3/`: M10 수정 라운드 1, [TimingContract.md](TimingContract.md) §7.1) |
| `Saved/Conformance/<key sha256>.json` (M10) | `-Conformance`가 게이트 스위트 전체를 주입 없이 돈 뒤 (`-StampDir=`로 바꿀 수 있다) | JSON, physicstest.stamp/1 (§2.1) |

- **"N 프레임마다"**: (f + 1) % N == 0인 프레임 f(0부터)와 마지막 프레임이다. N = 0이면 마지막 프레임만이다. 그래서 `HashEvery = 0`(헤드라인)인 `hashes.csv`는 마지막 프레임 한 줄이고, `HashEvery = 1`은 모든 프레임이다.
- 모든 이진 파일은 little-endian이다. 단위는 cm, kg, s, rad, Z-up.
- `exit.json`은 임시 파일에 쓴 뒤 `MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH)`로 바꿔 넣는다. 읽는 쪽은 반쯤 쓰인 파일을 보지 않는다. `exit.json`이 없으면 그 프로세스는 끝까지 가지 못한 것이다 (M8 러너는 원시 종료 코드로 성공을 판정하지 않는다).

## 2. `manifest.json` (physicstest.run/2)

run/1은 M3~M6의 최소 벤치 manifest다 (host·clock·order·alloc·gates 그룹이 없고 `timing_valid_for_headline`이 언제나 false). run/2에서 그룹이 늘었고, 기존 필드의 뜻은 그대로다. 예외 둘: M4의 `run.headline_eligible`·`run.headline_ineligible_reasons`는 없어졌고, 그 조건은 최상위 `timing_valid_for_headline`·`timing_invalid_reasons`(TimingContract.md §6)에 들어갔다. M3의 `run.injected_engine_error_bits`도 없어졌다 (`run.test_injections.engine_error_bits`, M7 수정 라운드 0). 그룹에는 필드가 더해질 수 있다 (스키마가 추가 속성을 허용한다).

**모양은 언제나 같다 (M7 수정 라운드 0):** 거부된 run도 모든 그룹을 쓴다. run이 닿지 못한 값은 null이다: 엔진 옵션 preflight가 거부하면 `scene.built` false이고 `scene.desc_sha256`·`options_sha256`·`num_bodies`·`num_dynamic_bodies`·`start_asleep_skipped`, `run.scene_desc_instrument_flags`·`dt_engine_last_plan_s`·`frame_driver`·`sync_policy`·`async_interpolated`·`steps_total`·`sim_time_s`, `step_stats_final`이 null이고 `effective`·`gates`는 빈 배열, `memory.backend_scene`은 `error`다. 스키마의 `allOf`가 `scene.built` true인 run에는 이 값들을 요구하고, false인 run은 `status` preflight_failed와 `preflight_error` "engine options: …"를 요구한다.

| 그룹 / 필드 | 내용 |
|---|---|
| `schema`, `status`, `exit_code` | `physicstest.run/2`; ok, preflight_failed, engine_error, capacity_exceeded, contaminated_host, usage_error(M8: 씬을 만든 뒤 시험 전용 주입이 효과를 낼 수 없다고 판정한 run, 종료 코드 2. `-InjectPreflightFault=g9:<key>`의 key를 G9가 비교하지 않을 때); 0, 2, 3, 4, 5 |
| `timing_valid_for_headline`, `timing_invalid_reasons` | 헤드라인 자격과 그 사유 목록 ([TimingContract.md](TimingContract.md) §6). M8부터 스키마가 둘의 일관성을 강제한다: 참이면 사유 0개, status ok, 종료 코드 0, Bench 모드, 계측 tier 없음(`instrumentation` 빈 배열, `instrument_flags`·`scene_desc_instrument_flags` 0), FixedStep, `hash_every`·`states_every`·`counters_every`·`capture_every` 0, `pre_roll_s` ≥ 0.5, `test_injections` 빈 객체, `alloc.tick_new_calls` 0, `host_load.valid` 참·`contaminated` 거짓. M10(M8 검토)에서 TimingContract §6의 나머지 조건을 더했다: 호스트 제어 네 플래그(`threads.affinity_applied`, `platform.priority_set`, `platform.throttling.opt_out_applied`, `platform.timer_resolution.applied`) 참, `harness.do_check` 거짓, G10b Fail 없음, `clock.tsc_invariant`·`clock.calibration.valid` 참, `harness.profiler` 거짓. M10 수정 라운드 1: `clock.calibration.max_residual` < 1e-5, T = 1이면 `threads.census_window.g10b_skip`이 `no_census`·`contended`가 아님. 거짓이면 사유가 하나 이상이다 (M7 검토 지적) |
| `unofficial`, `unofficial_reasons`, `timing_label` | M10: `unofficial`은 판정이다. 헤드라인 자격, 이 run 키의 유효한 스탬프, High performance/Ultimate 전원 계획이 모두 참일 때만 false (스키마가 "false ⇒ 셋 모두"와 "true ⇒ 사유 ≥ 1"을 강제한다). `timing_label`은 사유로 만든 문장이다. M7~M9는 언제나 true와 사유 문장이었다 ([TimingContract.md](TimingContract.md) §9) |
| `comparability_key` | M10: 이 run의 comparability key (sha256 hex, [02_Architecture.md](02_Architecture.md) §13). 씬을 만들지 못한 run은 null. M7~M9는 null |
| `conformance_stamp` | M10: 벤치가 run마다 찾은 스탬프: `key_sha256`, `found`, `valid`, `reason`, `path`, `stamp_utc`, `stamp_dir`, `key`(키 필드 전부, §2.1). preflight 전에 멈춘 run은 key null과 그 사유. M7~M9는 null |
| `run_id`, `session`, `utc`, `command_line`, `wall_time_s` | |
| `harness` | `git_sha`, `dirty`, `git_sha_source` = `build`(빌드 시점: `.git/HEAD`·`index`·`logs/HEAD`가 바뀌면 다시 만든다), `git_sha_configure`(비교용 configure 시점 값), `exe`, `exe_sha256`, `preset`, `do_check`, `profiler`(M10: prof 프리셋, Tracy·Jolt external profile), `sdks`(M10 수정 라운드 1: 이 exe에 든 SDK 백엔드 `PT_WITH_<SDK>`, 예 ["Jolt", "Bullet", "PhysX"]. 하나뿐이면 단일 SDK exe = 바이너리 구성 대조군이다: 분석은 preset 이름이 아니라 이것으로 가른다). **한계:** `dirty`는 그 세 git 파일이 바뀔 때만 다시 계산한다. 소스만 고치고 git 명령 없이 빌드하면 직전 값이 남는다 (`cmake/GitInfo.cmake`). 바이너리의 정체는 `exe_sha256`(와 M10 스탬프)가 정본이다 |
| `build` | `flag_profile`(Shipped / MatchedAVX2), `configuration`, `msvc`, `cmake`, `crt`, `lto`, `flags_json_sha256`, `flags_json_tu_count`, `flags_json_violations` |
| `backend` | `name`, `display_name`, `variant`, `label`, `sdk_version`, `sdk_sha`, `build_config`, `module_file`·`module_sha256`(DLL 백엔드만, 아니면 null), `abi_version`(호스트 헤더의 ABI), `module_abi_version`·`module_provisional_revision`(M9 수정 라운드 2, 덧붙임: 모듈이 보고한 `PtBackendApi.AbiVersion`과 `PtBackendInfo.ProvisionalRevision` - v1.1 호스트에서 돈 v1.0 DLL은 "1.0" / 1. 이전 manifest에는 없다), `capabilities`, `module_flags`, `required_effective_keys`, `fidelity_notes`, `tu_self_report`(어댑터 TU 자기보고, G0 입력) |
| `profile` | `physics`, `profile_file`, `variant_file`, `mass_rule`, `physics_sha256`, `profile_sha256`, `variant_sha256`, `warnings`, `resolved[]`(key, value, unit, origin, origin_name, applied). 명령줄의 run 옵션(`-Threads`, `-RunMode`, `-Instrument` 등)은 physics 키가 아니어서 여기 없고 `run`·`threads`에 쓴다. **M9:** `forced_overrides`(`config[]`(key, value, origin), `applied`(이 씬이 적용했는지: 게이트 씬은 false), `materials`, `materials_changed`, `foreign_materials`, `bodies`, `bodies_changed`, `changes[]`(바뀐 값마다 한 줄)). 강제 오버라이드 키는 `resolved[]`에도 `ForcedOverrides.<Target>.<Field>`, origin Forced로 나온다 |
| `effective`, `effective_module`, `missing_effective_keys`, `bodies_sampled` | effective 설정(§8), 빠진 필수 키, 동적 바디 세 개의 질량·관성 텐서 비교 |
| `scene` | `name`, `version`, `kind`, `params`, `seed`, `scene_sha256`, `built`(씬을 만들었는지), `desc_sha256`, `desc_sha256_scope`, `options_sha256`, `num_bodies`, `num_dynamic_bodies`, `start_asleep_skipped`, `capacity_hints`, `windows[]`(씬이 선언한 윈도: name, first, last). **M9:** `num_constraints`, `num_unsupported_constraints`(백엔드에 그 타입 capability가 없어 호스트 전용인 조인트: 벤치는 그런 run을 시작 전에 거부하므로 언제나 0), `required_capabilities[]`, `chaotic` |
| `run` | `mode`, `instrumentation[]`(tier 이름), `instrument_flags`, `scene_desc_instrument_flags`(백엔드가 실제로 받은 값), `frames`, `frames_run`, `dt_s`, `dt_engine_s` = (double)(float)dt, `dt_engine_last_plan_s`, `tick_mode`, `clamp_rule`, `frame_driver`, `sync_policy`, `hash_every`, `states_every`, `counters_every`, `capture_every`, `pre_roll_s`, `async_interpolated`, `steps_total`, `sim_time_s`, `test_injections{}`(시험 전용 주입: `engine_error_bits`, `preflight_fault`, `scene_instrument_flags`, `state_perturbation_frame`, M8의 `tick_allocation_frame`·`silent_exit_frame`, M10 수정 라운드 0의 `foreign_work_ms`: G10b 음성 대조) |
| `threads` | `T`, `affinity_mask`(16진 문자열), `affinity_applied`, `affinity_before`, `logical_cpus[]`, `smt_sibling_mask`, `series`(`p_cores_one_lp` / `p_cores_smt` / `with_e_cores`, 02 §9.6), `cores`, `smt_pair_cores`, `e_cores`, `mapping`, `engine_native`(`threads.workers` READBACK: workers, native, source), `engine_thread_settings{}`, `scaling_note`, M10: `census_init`(센서스 #1: valid, error, threads, `list[]`(tid, name, main, cycles)), `census_window`(valid, error, window, T, wall_s, wall_tsc, own/main/foreign/process/unattributed/mask_busy/other_on_mask cycles, `cpu_eff`, `cpu_eff_raw`, `foreign_cycles_fraction`, M10 수정 라운드 0의 `bracket_cycles`(센서스 #1이 잰 경계 읽기의 기준선)·`unattributed_fraction`(= max(0, unattributed − bracket) / own, G10b가 외부와 함께 본다), M10 수정 라운드 1의 `own_s`(own CPU 시간: G10b의 최소 창 50 ms), `other_on_mask_fraction`(다른 부하 / (wall × T): 0.05를 넘으면 경합 창), `g10b_skip`(G10b가 Skip인 까닭: "" 판정, `multi_thread`, `no_census`, `short_window`, `contended`. T = 1의 `no_census`·`contended`는 헤드라인 자격이 없다), threads_begin/end, `per_thread[]`(tid, name, main, cycles, new, exited), note), `foreign_cycles_fraction` ([TimingContract.md](TimingContract.md) §7.1). M7~M9는 null |
| `platform` | `cpu`, `logical_processors`, `p_cores[]`, `os_build`, `priority_class`, `priority_before`, `priority_set`, `throttling`, `timer_resolution`(requested_ms, applied, before, after), `host_control_errors`, `power_scheme`, `power_scheme_guid`, `power_scheme_high_performance`, `qpc_hz`, `cpu_mhz[]`, `system_busy_other_pct`, `host_load`(valid, window, wall_s, unit, own_process_pct, `core_mask`(마스크 CPU + SMT 형제), `cores`, `core_busy_other_pct`(종료 코드 4의 측정값), mask_busy_other_pct, smt_sibling_busy_pct, system_busy_other_pct, per_cpu[](cpu, in_mask, busy_pct), policy, `threshold_pct`(코어당), `threshold_total_pct`(코어당 × cores: core_busy_other_pct와 비교), contaminated; [TimingContract.md](TimingContract.md) §5.1) |
| `clock` | `source`, `qpc_hz`, `qpc_tick_ns`, `read_overhead_ns`, `read_overhead_method`. M10: `tsc_hz`(run 보정의 1e9 / ns_per_tick), `tsc_invariant`, `tsc_read_overhead_ns`, `tsc_read_overhead_method`, `ns_per_tick`, `ns_per_tick_provisional`, `calibration`(valid, pairs, span_s, max_residual, rms_residual, max_pair_spread_ticks, pair_every_frames, method) ([TimingContract.md](TimingContract.md) §2.1) |
| `order` | `repeat_index`, `order_index`, `block_seed` (M8 러너가 넘긴다) |
| `phases_ns` | `backend_init`, `option_preflight`, `scene_create`, `preflight`, `prepare`, `pre_roll`, `frame0`, `loop_after_frame0`(frame 1부터 마지막 프레임까지. M10 수정 라운드 1에서 M7~M9처럼 마지막 프레임 바로 뒤에 읽게 되돌렸다: M10 첫 판은 센서스 #2(수십 ms)와 `-InjectForeignWork` join 뒤에 읽었다), `teardown`(null: `teardown.json`) |
| `memory` | `process`(peak·현재 commit, working set), `backend_scene`(`PtMemoryStats`: -1은 셀 수 없음) |
| `alloc` | `hook_installed`, `hook`, `hook_scope`(훅은 exe 이미지 안에서만 묶인다: DLL 백엔드의 할당은 보이지 않는다), `tick_new_calls`, `tick_delete_calls`, `tick_first_allocating_frame`, `frame0_new_calls`, `loop_new_calls`, `backend_allocs_in_loop`·`backend_allocs_frame0`·`backend_allocs_after_frame0`(엔진 자기 할당자의 호출 수: frame 0 직전 → frame 0 Tick 뒤 → 마지막 Tick 뒤. null = 백엔드가 세지 못함), `backend_note` ([TimingContract.md](TimingContract.md) §4.1) |
| `summary` | `unit` = ns, `frame0_frame_ns`(frame이 없으면 null), `windows.<name>.{first, last, declared_last, clipped, count, frame_ns, step_ns, push_ns, pull_ns}`, M10: `windows.<name>.tick_ns`(Tick 전체, 통계)와 `batch_frame_ns`(배치 측정: 윈도의 TSC 한 구간 / 프레임 수, 숫자), `cpu_eff`(센서스 창). 통계는 각각 count, mean, median, p95, p99, min, max (nearest-rank). frame 0은 어느 윈도에도 넣지 않는다. **run이 윈도보다 짧으면 (M7 수정 라운드 0):** `last`는 이 run에서 윈도가 실제로 덮은 마지막 프레임 = min(`declared_last`, frames_run − 1)이고 `clipped`가 true다. 프레임이 하나도 없는 윈도는 `count` 0, `last`와 통계 넷이 null이다 (0 ns가 아니다. 스키마가 count 0의 통계 객체를 거부한다) |
| `determinism` | `h_native`(마지막 프레임, 백엔드 해시; 엔진 간·빌드 간 비교 금지), `h_native_frame`, `h_quantized`, `h_state`, `engine_error_bits`, `capacity_error_bits_declared`, `capacity_error_bits`, `other_error_bits` |
| `gates[]` | preflight 게이트 G0, G9, G10a와 M10의 G10b(루프 뒤, 센서스 창): `id`, `result`(Pass/Fail/Skip), `checks`, `failures[]`, `notes[]` |
| `step_stats_final`, `recorders`, `deferred_fields`, `outputs[]` | 마지막 `GetStepStats`(씬이 없으면 null), 기록기별 행·프레임 수(`hashes`, `states`, `contacts`, `phases`: phase timer가 값을 낸 프레임 수와 타이머 이름), 아직 채우지 않은 필드 목록(M10부터 빈 객체), 쓴 파일 |
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

### 2.1 적합성 스탬프 (physicstest.stamp/1, M10)

`Saved/Conformance/<stamp_key_sha256>.json`. 키와 쓰는 조건은 [TimingContract.md](TimingContract.md) §9.1이다.

| 필드 | 내용 |
|---|---|
| `schema`, `stamp_key_sha256`, `key_canonical`, `key` | 키 SHA-256 = `key_canonical`(정렬된 키, 공백 없는 JSON)의 해시. `key`: `bench_exe_sha256`, `backend_module_sha256`(정적 백엔드는 ""), `variant_sha256`, `physics_sha256`, `tolerances_sha256`, `gate_suite_version`(지금 11: M10 수정 라운드 1, 10은 수정 라운드 0), `config_overrides`(명령줄 `-ini:`·`-ExecCmds=` 토큰, 정렬) |
| `valid`, `result`, `invalid_reason`, `pending_sign_offs`, `summary` | `valid` = 게이트 실패 0 그리고 `pending_sign_offs` 0. 무효 스탬프도 쓴다 (러너가 "invalid"로 거부하고 사유를 보인다) |
| `target`, `build`, `backend_module` | backend, variant_requested, variant, profile / exe, exe_path, preset, flag_profile, configuration, git_sha, git_dirty / kind(static, dll), file, path |
| `config_dir`, `inputs[]` | 설정 디렉터리와 키를 만든 입력 파일(role: base_ini, profile_ini, variant_ini, tolerances; path; sha256). 러너는 이 파일들의 지금 원시 해시가 기록과 같은지 본다. 입력과 exe가 같으면 C++ 리졸버의 `variant_sha256`·`physics_sha256`도 같으므로 이것이 러너의 재계산이다 (`ptbench/stamps.py`) |
| `gates[]` | 게이트마다 id, result, reason, failures. DLL 백엔드는 ABI 게이트 A0~A7·A10(게이트마다 새 프로세스)이 앞에 온다 (M10 수정 라운드 0). G10b와 ABI 게이트에는 skip이 없다: 판정 없는 G10b 자식 run(최대 세 번)과 ABI 게이트 프로세스의 Skip은 fail로 적힌다 (M10 수정 라운드 1, 유효한 스탬프의 G10b는 언제나 pass). skip은 `tolerances.json`이 등록한 물리 게이트 부분만이다 |
| `conformance_dir`, `command_line`, `utc` | 스탬프를 만든 `-Conformance` 실행 |

## 3. `exit.json` (physicstest.exit/1)

| 필드 | 내용 |
|---|---|
| `exit_code`, `meaning` | 0, 2, 3, 4, 5, 6과 이름 |
| `status` | ok, usage_error, preflight_failed, gate_failed, engine_error, fatal, contaminated_host, capacity_exceeded, backend_unavailable, **unsupported**(M9: 씬이 요구하는 capability(조인트)가 백엔드에 없어 백엔드를 초기화하기 전에 거부, 종료 코드 2, manifest 없음. 러너는 실패로 세지 않는다. M9 수정 라운드 1: 모듈이 그 capability를 **선언했는데** 다른 `PT_ABI_PROVISIONAL_REVISION`으로 빌드되어(v1.1 전) 호스트가 없는 것으로 보면 unsupported가 아니라 `backend_unavailable`(종료 코드 6, category backend)이다 - 잘못 빌드한 DLL이 조인트 셀을 모두 조용히 건너뛰고 세션이 0으로 끝나지 않게) |
| `category` | "", usage, preflight, gate, engine, host, capacity, backend, unsupported(M9). 종료 코드 2는 사용 오류(usage), preflight·게이트 실패, 지원하지 않는 씬(unsupported)을 함께 쓰므로 이 필드로 나눈다. M8 러너는 `unsupported`인 run의 outcome을 `unsupported`, 모든 반복이 그런 셀의 state를 `unsupported`로 적고 러너 종료 코드에서 실패로 세지 않는다 (`session.schema.json`) |
| `reason` | 사람이 읽는 사유 (Fatal이면 Fatal 로그 줄) |
| `run_id`, `utc`, `pid`, `terminate_process`, `outputs[]` | `terminate_process`가 참이면 UE_RUNTIME 모듈이 로드되어 프로세스가 `TerminateProcess`로 끝났다 (teardown 뒤, 또는 Fatal이면 이 파일을 쓴 직후). 백엔드를 로드한 뒤 끝나는 모든 경로가 이 값을 바르게 적는다 |

종료 코드가 여럿 해당하면 2(루프가 돌지 않음) > 3 > 5 > 4 순서다. Fatal(3)은 `FPlatformMisc::SetFatalExitCallback` 훅이 쓴다. 훅은 run의 마지막 `exit.json`을 쓰기 전까지만 이 파일을 쓴다. teardown 중의 Fatal은 이 파일을 바꾸지 않고 `teardown.json`에 남는다 (§9).

## 4. `timings.csv`

프레임당 한 줄. 열: `frame, sim_time_s, num_steps, push_ns, setup_ns, start_ns, wait_ns, end_ns, step_ns, pull_ns, frame_ns, frame_qpc_ticks, solver_ns, awake_bodies, contact_pairs, contact_points, islands, error_bits` 그리고 M10의 `frame_tsc_ticks, tick_ns, tick_tsc_ticks, tsc_end, qpc_end`(뒤에 덧붙임).

- 버킷의 뜻은 [TimingContract.md](TimingContract.md) §2다. `frame_ns` = push + step + pull.
- `solver_ns`: phases tier이고 백엔드가 phase timer를 선언할 때만 값이 있다. 아니면 비어 있다.
- `awake_bodies`: 호스트가 받은 상태로 센 깨어 있는 동적 바디 수 (언제나 있음).
- `contact_pairs`, `contact_points`, `islands`: counters 프레임의 `GetStepStats` 값, 아니면 -1. 백엔드가 모르면 -1이다.
- `error_bits`: 그 프레임의 엔진 오류 bit.
- M10: ns 열은 TSC 틱을 run 보정 `clock.ns_per_tick`로 바꾼 값이다. `frame_tsc_ticks`는 버킷 TSC 틱의 합(`frame_ns`의 원시값), `tick_tsc_ticks`·`tick_ns`는 `FPhysScene::Tick` 전체, `tsc_end`·`qpc_end`는 그 Tick이 끝난 순간의 원시 TSC·QPC다. **`frame_qpc_ticks`의 뜻이 바뀌었다:** M7~M9는 버킷 QPC 틱의 합, M10부터는 Tick 전체를 QPC 두 번으로 잰 틱이다 (TSC와 독립인 대조, [TimingContract.md](TimingContract.md) §2.2). **거친 대조다 (M10 수정 라운드 1, 독립 검사 [V]):** 프레임마다 `frame_qpc_ticks`가 `tick_tsc_ticks` × ns_per_tick보다 길게 나오는 양의 치우침이 있다 (BoxPile run 24개의 프레임별 (QPC − TSC) 중앙값 +120 ns ~ +910 ns, 합으로 1e-4 ~ 3.5e-4, 긴 Tick일수록 크다: 긴 Tick 직후의 차가운 QPC 읽기로 본다). run 전체의 TSC·QPC 프레임 끝 쌍(`tsc_end`, `qpc_end`)은 5e-7 안에서 맞는다. 정확한 값은 TSC 쪽이고 헤드라인(TSC 버킷)에는 영향이 없다. 두 번째 시계로 프레임 단위 대조를 할 때는 이 치우침을 감안한다.

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

헤더 32 B: magic `PTCN`, u32 version 1, u32 record_size 128(`sizeof(PtContactPoint)`), u32 frame_count(닫을 때), u32 flags(bit 0 = frame_count 확정), u32 abi_provisional_revision(오프셋 20), u32 point_pose(오프셋 24), u32 reserved. M8에서 M7의 reserved 세 칸 중 앞 두 칸을 채웠다 (레이아웃과 version 1은 그대로): `abi_provisional_revision`은 기록한 빌드의 `PT_ABI_PROVISIONAL_REVISION`이다. `PtContactPoint`는 M9까지 PROVISIONAL(M9) 구조체였고 v1.1에서 리비전 3으로 확정되었다 (레이아웃 그대로). 읽는 쪽은 record_size가 같아도 revision이 다른 파일을 거부할 수 있다 (M8 파일은 1(M9 준비 빌드는 2), M9 v1.1 파일은 3. M6~M8 헤더의 `PT_ABI_PROVISIONAL_REVISION`은 1이다, [Abi.md](Abi.md) §2). `point_pose`는 백엔드의 effective 키 `contact.point_pose`(0 스텝 시작 자세, 1 예측 자세, 0xFFFFFFFF 선언 없음)다. M7이 쓴 파일은 두 칸이 0이다 (revision 0 = M8 이전). 그 뒤 기록한 프레임마다 `{u32 frame, u32 count}`와 `PtContactPoint` count개를 백엔드가 쓴 그대로 둔다. 규약은 v1.0 ABI다 ([Abi.md](Abi.md) §7.1, §9.1 결정 2·3): Normal은 Body1 위에서 Body0 쪽, Phi < 0은 침투, flag는 ACTIVE / ACTIVE_APPROX / SPECULATIVE 중 하나 이하, PointOn은 그 스텝의 narrow phase 자세 기준. 어느 자세인지는 effective 키 `contact.point_pose`가 선언한다 (0 = 스텝 시작 자세: Jolt, Bullet, PhysX; 1 = 예측 자세: Chaos).

### 7.1 `native_draw.csv` (M9)

`-Instrument=native_debug_draw`(보통 `-RunMode=Viz`와 함께)이면 벤치가 `CaptureEvery`(없으면 1) 프레임마다 `PT_SNAP_NATIVE_DRAW`를 캡처해 한 줄을 남긴다: `frame, commands, points, lines, arrows, circles, spheres, capsules, boxes, triangles, strings, other_type, string_bytes, truncated, h_draw`. 타입 열은 `PT_DD_*`별 수, `other_type`은 정의 밖 타입(백엔드 버그, 세기만 한다), `truncated`는 백엔드의 `bTruncated`(요청 한도 2^20), `h_draw`는 160 B `PtDebugDrawCmd` 레코드(백엔드가 쓴 그대로)와 문자열 풀의 FNV-1a 64(hex)다. 명령 자체는 저장하지 않는다: 같은 빌드의 두 run은 `h_draw`로 프레임마다 스트림을 비교한다. manifest `recorders.native_draw` = {frames, commands, truncated_frames, every, note}. tier가 꺼졌거나 백엔드에 capability가 없으면 frames 0과 그 사유, viz run이 아니면 "not a viz run" 노트 ([Abi.md](Abi.md) §9.2: 네이티브 드로는 viz run 전용). CTest `PhysicsTest.Bench.NativeDrawBullet`·`NativeDrawPhysX`(30 프레임, 명령 > 0, 잘림 0).

## 8. `effective_settings.jsonl`

한 줄에 키 하나: `{"key", "native", "native_unit", "canonical", "unit", "source"}`. source는 READBACK(엔진에서 읽은 값), SET_ONLY(어댑터가 설정한 값), COMPILE_TIME, ENGINE_DEFAULT다 ([Abi.md](Abi.md) §5.14). `canonical`은 하네스 단위의 숫자 또는 null이다. `effective_settings.jsonl`은 preflight가 읽은 값이고, `effective_settings_after.jsonl`은 마지막 프레임 뒤의 값(temp allocator peak, 누적 오류 bit 같은 run 결과)이다.

## 9. `teardown.json` (physicstest.teardown/1)

`teardown_ns`, `term_game_phys`(`TermGamePhys`의 결과 이름), `fatal`(teardown 중 Fatal의 로그 줄, 없으면 null), `terminate_process`. `exit.json` 다음에 쓰므로 teardown이 실패해도 run의 종료 코드는 그대로다. teardown 중 Fatal이면 Fatal 훅이 이 파일을 `fatal`과 함께 쓰고(`teardown_ns`, `term_game_phys`는 null) 프로세스를 run의 종료 코드로 `TerminateProcess`한다 (M7 수정 라운드 0, DL52).

## 10. `gate_g8.json`, `gate_g11.json` (physicstest.gate/1)

`gate`, `result`(pass / fail), `command_line`, `utc`, `runs[]`(label, out_dir, started, timed_out, process_exit_code, exit_json_code, status, h_native, h_state, desc_sha256, T, frames_run, wall_s, spawn_error), `failures[]`, `notes[]`, `injected_gate_fault_frame`(시험용 `-InjectGateFault`를 썼을 때만: 음성 대조, [TimingContract.md](TimingContract.md) §8). 자식 run 디렉터리(`run0`..`runN-1`, 또는 `off`·`on`)에 자식의 결과 전부와 `child.log`(표준 출력·오류)가 있다. 게이트 프로세스 자신의 `exit.json`도 같은 디렉터리에 있다. **M9:** `-G11Knob=`이 G11의 손잡이를 고른다: `contacts`(기본, M7: 접촉 캡처 off/on), `variants:<A>,<B>`(백엔드 변형 A·B, 예: Bullet `Default`·`StaticAabbOff`; 두 run의 `desc_sha256`도 같아야 한다), `viz`(Bench 모드 계측 없음 / Viz 모드 `-Instrument=native_debug_draw -CaptureEvery=1`: Bullet viz 드로어가 결과를 바꾸지 않는지, 03 M9). `off`·`on` 디렉터리 이름은 그대로이고 runs[].label이 손잡이를 말한다 (`variant_<A>`, `bench`, `viz_native_draw`), notes 첫 줄이 `knob: ...`다

## 11. 세션과 매트릭스 (M8, `Tools/ptbench`)

`python -I -m ptbench run --matrix <name>`이 세션 하나를 `Saved/Results/<matrix>-<UTC 시각>/`에 만든다 (`--out`으로 바꿀 수 있다).

| 파일 | 형식 |
|---|---|
| `session.json` | physicstest.session/1 (`Config/Schemas/session.schema.json`). run이 끝날 때마다 원자적으로 다시 쓴다. 그래서 중단된 세션도 스키마를 통과한다 (`status` running / aborted) |
| `index.csv` | 시도마다 한 줄 (재시도 포함). 열은 아래 runs 항목과 같다 |
| `runs/r<반복>_<순서 3자리>_<셀 키>[_a<시도>]/` | 벤치의 run 디렉터리 (`-Out`). `child.log`에 벤치의 stdout·stderr. 셀 키는 셀 id의 SHA-1 앞 8자리다 (경로를 짧게: LongPathsEnabled=0) |
| `analysis/{runs,frames,effective,gates}.parquet` | `ptbench load`의 tidy 표 (스키마 검증 결과 포함). 플래그 열(`exit_json_valid`, `manifest_valid`, `timing_valid_for_headline`, `contaminated`, `power_scheme_high_performance` 등)은 nullable boolean, 코드·개수 열(`exit_code`, `process_exit_code`, `T`, `attempt`, `repeat`, `win_*_first/last/count` 등)은 nullable Int64, 목록·객체는 JSON 문자열이다. 값이 없는 run(도중에 죽은 run)은 빈 값이지 문자열 "nan"이 아니다 |
| `report/<날짜>/index.md`, `*.png` | `ptbench report` |

**`session.json` 필드:** `session_id`, `status`(running, complete, aborted, refused), `started_utc`·`finished_utc`·`wall_s`, `matrix`(name, path, sha256, purpose, repeats, block_seed, 문서 전체), `runner`(ptbench 버전, Python, 명령줄, `cooldown_s`, `timeout_s`, `teardown_grace_s`(30), `child_containment`(job object 여부), `contaminated_retries`, `window`, `bench_args`, `verdict_source` = exit.json), `bench`(exe, exe_sha256, preset), `git`(sha, dirty), `environment`(환경 게이트 결과: 전원 계획 GUID·이름·종류, 유휴 `idle`(측정창 `samples_pct`, `windows`, 기다린 시간 `waited_s`, `wait_limit_s`, `skipped`), 다른 PhysicsTest 프로세스, 검사별 pass·bypassed·bypass_flag, refused, 호스트), `bypasses`(`--allow-balanced`, `--allow-busy`, `--allow-concurrent`, `--skip-idle`: 재지 않은 유휴도 우회다), `unofficial`·`unofficial_reasons`(M10: 판정, [TimingContract.md](TimingContract.md) §5.2. M8·M9 세션은 언제나 참), `blocks[]`(반복마다 block_seed와 셀 순서, M10: CV 게이트의 추가 블록은 `cv_gate: true`), `plan[]`, `cells[]`, `runs[]`, `summary`(M10: `noisy_cells`, sentinel run 수). M10: `refusal_reason`(거부된 세션: 환경 게이트, 또는 스탬프 없는 셀), `stamps`(policy require / record(M10 수정 라운드 0: determinism·behavior 매트릭스의 기본, 스탬프 상태를 기록만 한다) / skip / allow, stamp_dir, checked, `unstamped_cells`, `targets[]`(state valid / missing / stale / invalid, reason, target)), `cv_gate`(enabled, threshold_pct, max_extra_blocks, `blocks[]`(index, cells), `noisy`; 꺼져 있으면 `reason`: 매트릭스의 `cv_gate` false 또는 perf가 아닌 매트릭스), `sentinel`(declared, `points[]`(point start / middle / end, runs, value_ns), drift_pct, flagged, reason). `bypasses`에 `--allow-unstamped`, `--skip-unstamped`가 더해졌다.

- **runs[] (시도 하나):** `run_name`, `kind`(M10: cell 또는 sentinel), `attempt`(0부터, 종료 코드 4 재시도), 셀 정보(`cell_id`, `cell_key`, label, backend, variant, profile, scenario, params, T, frames), `repeat`·`order`·`block_seed`(벤치에 `-Repeat`·`-Order`·`-BlockSeed`로 넘겨 manifest `order`에도 남는다), `out_dir`, `started_utc`, `wall_s`, `cooldown_s`·`cooldown_system_busy_pct`(쿨다운 동안 시스템 전체 바쁨), `timed_out`, `teardown_killed`·`exit_json_after_s`·`note`(`exit.json`을 쓴 뒤 `teardown_grace_s` 안에 끝나지 않아 러너가 죽인 run: 실패가 아니고 판정은 그 `exit.json`이다), `process_exit_code`(**기록만**), `exit_json`(있는지), `exit_code`·`exit_status`·`exit_category`(exit.json), `exit_json_valid`·`manifest_valid`·`schema_errors`, `outcome`, `retried`, manifest에서 읽은 `timing_valid_for_headline`·사유, 윈도 평균 `window_mean_ns`·`window_count`, `core_busy_other_pct`·`contamination_limit_pct`, `desc_sha256`, `flag_profile`, `h_native`, `frames_run`, `exe_sha256`, `command`.
- **outcome:** `ok`(exit.json 0이고 두 파일이 스키마를 통과), `contaminated`(exit.json 4), `exit_<코드>_<status>`, `no_exit_json`(프로세스가 exit.json 없이 끝났다: 원시 종료 코드가 0이어도 실패), `timeout`(러너가 죽였다), `spawn_error`, `invalid_exit_json`, `invalid_manifest`. 스키마가 "exit.json 없음 ⇒ exit_code null, outcome no_exit_json/timeout/spawn_error"와 "ok ⇒ exit.json 0"을 강제한다.
- **cells[]:** 셀 정보(`bench_args`: 타깃의 추가 스위치)와 `state`(ok: 모든 반복이 ok, contaminated: 어떤 반복의 시도가 모두 4, failed: 어떤 반복이 다른 이유로 실패, incomplete), `repeat_states`, `n_attempts`, `n_ok`, `n_contaminated_attempts`, ok이고 `timing_valid_for_headline`이 참인 run의 윈도 평균 `values_ns`와 `median_ns`·`mean_ns`·`cv_pct`, 헤드라인 자격이 없어 값이 없는 ok run의 수 `n_not_headline_valid`. M10: `noisy`, `cv_gate`(initial_cv_pct, extra_blocks, final_cv_pct, noisy), `stamp`(state, reason), state `unstamped`(`--skip-unstamped`로 돌지 않은 셀). 리포트의 validate와 같은 run만 세지만 권위 있는 CV는 리포트의 것이다.
- **manifest가 없어도 되는 run:** 종료 코드 0, 3, 4, 5는 manifest를 쓴다. 단 Fatal run(종료 코드 3, status `fatal`: Fatal 훅은 `exit.json`만 쓴다)과 usage error(2), 종료 코드 6은 `exit.json`만 쓴다. 러너와 validate는 이들에게 "manifest.json missing"을 붙이지 않는다.
- **러너 종료 코드:** 0 모든 셀 ok, 1 셀이 failed·contaminated이거나 `session.json`이 스키마를 통과하지 못함, 2 매트릭스나 벤치 exe 없음, 추가 스위치의 키가 겹치거나 러너의 스위치를 쓴다 (아래), 또는 `--session`의 디렉터리가 이미 있음(M10), 3 환경 게이트 거부 또는 스탬프 확인 거부(M10: 프로세스를 하나도 띄우지 않는다).

**매트릭스 (`Config/Matrices/<name>.json`, physicstest.matrix/1):** `name`, `description`, `purpose`(perf / determinism), `preset`, `repeats`, `block_seed`, `cooldown_s`(기본 2), `timeout_s`, `contaminated_retries`(기본 2), `window`(기본 headline), `bench_args`, `targets[]`(label, backend, variant, 선택 `bench_args`: 이 타깃에만 붙는 벤치 스위치. 표는 label로 나뉘므로 label이 달라야 한다), `profiles[]`, `scenes[]`(scenario, params, frames), `threads[]`, `exclude[]`(이름 붙은 필드가 모두 맞는 셀을 뺀다. reason 필수), M10: `cv_gate`(false, 또는 threshold_pct·max_extra_blocks), `sentinel`(label, profile, scenario, params, T, runs, drift_pct). `preset`의 enum에 test-jolt, test-physx, test-bullet, prof가 더해졌다. 셀 = targets × profiles × scenes × threads − exclude. 반복 r의 block seed = splitmix64(block_seed + r)의 출력, 블록 순서 = 그 seed의 splitmix64 Fisher-Yates 셔플 (Python 버전과 무관하게 재현된다). M8 매트릭스는 `smoke`(03 M8 완료 기준)와 `determinism`(DET1, `-HashEvery=1`)이다.

**추가 스위치 규칙 (M8 수정 라운드 1):** 한 셀의 매트릭스 `bench_args`, 타깃 `bench_args`, `--bench-arg`에서 같은 스위치 키(대소문자 무시, `=` 앞까지, `-ini:`는 `[Section]:Key`까지, `-Param:`은 이름까지)가 두 번 나오면 러너가 매트릭스를 거부한다 (종료 코드 2, run 없음). 벤치는 `FParse::Value`로 첫 출현만 읽으므로(UE Parse.cpp:265) 중복은 말없이 무시되기 때문이다. 러너가 셀마다 넘기는 스위치(`-Out`, `-Backend`, `-BackendProfile`, `-Profile`, `-Scenario`, `-Param:`, `-Threads`, `-Frames`, `-Repeat`, `-Order`, `-BlockSeed`, `-Session`, M10의 `-StampDir`)와 다른 모드를 여는 스위치(`-Gate`, `-List`, `-Describe`, `-Conformance`, `-AnalyticReferences`, `-SceneHashes`, `-BusyHelper`)도 거부한다. 그래서 명령줄의 순서는 아무것도 정하지 않는다 ("뒤의 것이 이긴다"는 M8 수정 라운드 0의 설명은 틀렸다).
**M9 (거동, `purpose` behavior):** 매트릭스 `purpose`에 `behavior`가 더해졌고(`session.json`의 `matrix.purpose`도), `scenes[]`에 선택 `bench_args`(그 씬 항목에만 붙는 벤치 스위치, 예: 거동 씬 `-StatesEvery=1`, 1000 바디 더미 `-StatesEvery=100`)가 생겼다. 씬 `bench_args`도 위 추가 스위치 규칙(중복 키·러너 스위치 거부)에 들어가고 셀 id에는 들어가지 않는다. 버전은 physicstest.matrix/1 그대로다 (추가만). `ptbench behavior <세션|run 디렉터리…> [--out DIR] [--json FILE]`(종료 코드 0 분석함, 1 완료 기준 fail, 2 거동 run 없음. 리드 결정으로 붙는 판정 - `fail (accepted engine behaviour)`, `reported`, `finding: joints separated` - 과 승인 대기 `fail (pending override)`는 1이 아니다, M9 마감)과 `ptbench report`의 §7(입력에 `run.mode` Behavior run이나 `unsupported` run이 있을 때)이 거동 분석을 낸다: `index.md` §7(완료 기준, 차이와 태그, M1 프로필 표, 결정적·혼돈계·조인트 씬 표, 변형 knob, 허용치와 규칙), PNG 4장(`behavior_m1_profiles.png`, `behavior_m4.png`, `behavior_m5.png`, `behavior_tower.png`), `differences.csv`(차이마다 label, profile, scene, metric, value, reference, deviation, tolerance·출처, 태그, 규칙, 확신, 사유), `behavior.json`(physicstest.behavior/1: runs, observations, differences, ue_vs_normalized, sensitivity, wasserstein, variant_knobs, criteria, unsupported, skipped, errors, tolerances, rules). 거동 run은 타이밍 표에 들어가지 않는다 (§5에 한 줄 요약).

## 12. 검증

```
.venv\Scripts\python.exe -I Tools\validate_manifest.py --require-fields <run dir>...
```

- `manifest.json`을 `Config/Schemas/manifest.schema.json`, `exit.json`을 `Config/Schemas/exit.schema.json`으로 검증한다 (jsonschema, Draft 2020-12).
- `--require-fields`는 03 M7 완료 기준 1의 필드(mode, instrumentation, timing_valid_for_headline, dt_engine_s, threads, platform의 전원 계획, clock)가 채워졌는지도 본다.
- CTest `PhysicsTest.Bench.ManifestSchema`가 `M3.BoxPile` 5k run(윈도보다 짧아 headline이 clipped, settled가 비어 통계 null)과 거부된 run 둘(엔진 옵션 preflight: `scene.built` false, 빠진 필수 키)의 manifest를 검증한다. 일부러 망가뜨린 사본 여섯이 실패하는지도 확인한다: `run.mode` = "Fast", `platform.power_scheme`(전원 계획 이름) 삭제, `platform.power_scheme_guid` = "x", 빈 윈도에 0 ns 통계, 그리고 M8의 헤드라인 일관성 규칙 둘(사유가 남은 채 `timing_valid_for_headline` 참, 사유 없이 거짓).
- jsonschema를 import할 수 없는 인터프리터에서는 스키마 CTest가 설치 안내와 함께 **Skipped**로 나온다 (`cmake/PTPython.cmake`, M8. 전에는 시스템 Python으로 떨어져 ImportError로 실패했다).
- M8: `Tools/ptbench`의 러너는 run마다 `exit.json`과 `manifest.json`을 스키마로 검증하고(`session.json` runs[].schema_errors), 세션 끝에 `session.json` 자신도 `session.schema.json`으로 검증한다. `ptbench load`는 모든 run을 다시 검증해 parquet의 `exit_json_valid`·`manifest_valid`에 남긴다.
- M10: `ptbench load`는 manifest의 comparability key를 같은 필드로 다시 계산해(`comparability_key_recomputed`) validate가 다르면 거부한다 (복사·편집된 manifest). 뷰어의 `physicstest.viewer/1`(최상위 mode viz)은 run manifest가 아니라는 사유로 거부한다. 스탬프 파일은 `stamp.schema.json`으로 검증된다 (CTest `PhysicsTest.Bench.ConformanceStamp`, `PhysicsTest.Py.Integration`).
