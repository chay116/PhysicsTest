"""load - bench outputs -> tidy tables (02_Architecture 13 "load"; 03_WorkPlan M8 task 4).

Inputs are session directories (session.json + index.csv of ptbench run), -Gate=G8 directories (gate_g8.json with run0..N)
or plain run directories; a directory that is none of these is searched (three levels) for them. Every run's exit.json and
manifest.json are validated against Config/Schemas first (the result is kept per run: validate rejects a run whose files do
not match). Four tables, written as parquet by `python -I -m ptbench load` (<session>/analysis/*.parquet):
  runs       one row per run attempt: identity (cell, label, backend, variant, profile, scene, params, T, repeat), the verdict
             (exit.json present, exit code, status, outcome, raw process exit code), validity (schemas, headline flag and
             reasons), build (preset, flag profile, exe), hashes (scene, desc, physics, variant, h_native, h_state), host
             (power plan, contamination), and the window statistics of the manifest (win_<window>_<stat>)
  frames     one row per frame of timings.csv (run_key, frame, frame_ns, step_ns, push_ns, pull_ns, ...)
  effective  one row per effective setting of the manifest (run_key, key, native, canonical, unit, source)
  gates      one row per preflight gate of the manifest (run_key, id, result, checks, failures)
List- and dict-valued fields are stored as JSON text so that the parquet schema stays flat. In the parquet files the flag
columns are nullable booleans and the code / count columns nullable integers (pandas "boolean" / "Int64"), whatever runs
are missing: a died run (no exit.json, no manifest) is a missing value, never the text "nan" or "True".
"""
from __future__ import annotations

import dataclasses
import json
import pathlib
from typing import Any, Iterable

import pandas as pd

from .. import common
from . import comparability

WINDOW_STATS = ("mean", "median", "p95", "p99", "min", "max", "count")
KNOWN_WINDOWS = ("headline", "impact", "settled")
TIMING_COLUMNS = ["frame", "sim_time_s", "num_steps", "push_ns", "setup_ns", "start_ns", "wait_ns", "end_ns", "step_ns", "pull_ns", "frame_ns",
                  "frame_qpc_ticks", "solver_ns", "awake_bodies", "contact_pairs", "contact_points", "islands", "error_bits",
                  # M10: the TSC buckets' raw ticks, the whole Tick and the TSC / QPC at its end (M7-M9 files lack them)
                  "frame_tsc_ticks", "tick_ns", "tick_tsc_ticks", "tsc_end", "qpc_end"]
RUN_SCHEMA = "physicstest.run/2"


@dataclasses.dataclass
class Loaded:
    runs: pd.DataFrame
    frames: pd.DataFrame
    effective: pd.DataFrame
    gates: pd.DataFrame
    sessions: dict[str, dict[str, Any]]
    sources: list[str]

    @property
    def session(self) -> dict[str, Any] | None:
        return next(iter(self.sessions.values())) if len(self.sessions) == 1 else None


def default_label(manifest: dict[str, Any]) -> str:
    """The series label of a run the runner did not label (run and -Gate=G8 directories): backend.name lower case plus the
    variant. The Null backend exists twice - built in (module_file null) and as the PtBackend_Null.dll module (-Backend=
    NullDll) - with the same backend.name, so the module one is "null.Dll" (the label Config/Matrices/determinism.json uses)."""
    backend = (manifest.get("backend") or {})
    name = str(backend.get("name") or "?").lower()
    if name == "null" and backend.get("module_file"):
        name = "null.Dll"
    variant = backend.get("variant")
    return f"{name}.{variant}" if variant else name


def scene_label(scenario: str, params: dict[str, Any]) -> str:
    return f"{scenario}{common.format_params({k: str(v) for k, v in (params or {}).items()})}"


# The default seed of every scene: FRandomStream::DefaultSeed = 0x5EED, also the declared default of each Seed parameter
# (M3_BoxPile.cpp, P1_SmallScene.cpp; docs/Scenes.md).
DEFAULT_SEED = "24301"


def _scene_params(manifest: dict[str, Any]) -> dict[str, str]:
    """The scene's parameters for its label. manifest scene.params holds every declared parameter, resolved (defaults
    included), so Seed is there for every scene that declares it; it is left out only when it is the default seed. A run
    with another seed keeps it ("M3.BoxPile[N=1000,Seed=1]"), so a matrix that varies the seed gets one table per seed
    instead of one table that validate refuses for two desc_sha256 values (Progress M8 D-14)."""
    params = {k: str(v) for k, v in (((manifest.get("scene") or {}).get("params")) or {}).items()}
    if params.get("Seed") == DEFAULT_SEED:
        params.pop("Seed")
    return params


def discover(inputs: Iterable[str | pathlib.Path]) -> list[tuple[str, pathlib.Path]]:
    """(kind, path) for every session, G8 gate directory and run directory under the inputs."""
    found: list[tuple[str, pathlib.Path]] = []

    def visit(path: pathlib.Path, depth: int) -> None:
        if (path / "session.json").is_file():
            found.append(("session", path))
        elif (path / "gate_g8.json").is_file():
            found.append(("gate_g8", path))
        elif any((path / name).is_file() for name in ("manifest.json", "exit.json", "child.log", "effective_settings.jsonl")):
            found.append(("run", path))
        elif depth < 3 and path.is_dir():
            for child in sorted(p for p in path.iterdir() if p.is_dir()):
                visit(child, depth + 1)

    for item in inputs:
        visit(pathlib.Path(item).resolve(), 0)
    return found


def _json_text(value: Any) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False)


def run_row(run_dir: pathlib.Path, info: dict[str, Any] | None = None) -> tuple[dict[str, Any], dict[str, Any] | None]:
    """The runs-table row of one run directory (+ the manifest, for the other tables). info: what the runner recorded."""
    info = dict(info or {})
    exit_doc, exit_error = common.try_read_json(run_dir / "exit.json")
    manifest, manifest_error = common.try_read_json(run_dir / "manifest.json")
    row: dict[str, Any] = {
        "run_key": str(run_dir),
        "run_dir": str(run_dir),
        "source": info.get("source", ""),
        "source_kind": info.get("source_kind", "run"),
        # M10 fix round 0: the session directory a session run came from (the runner's per-session cell state, e.g. its CV gate's
        # noisy mark, belongs to that session only; cell keys repeat across sessions of one matrix)
        "session_dir": info.get("session_dir"),
        "run_name": info.get("run_name", run_dir.name),
        "attempt": info.get("attempt", 0),
        "repeat": info.get("repeat"),
        "order": info.get("order"),
        "process_exit_code": info.get("process_exit_code"),
        "timed_out": bool(info.get("timed_out", False)),
        "exit_json": exit_doc is not None,
        "exit_json_error": "" if exit_doc is not None else exit_error,
        "manifest": manifest is not None,
        "manifest_error": "" if manifest is not None else manifest_error,
    }
    schema_errors = []
    if exit_doc is not None:
        errors = common.schema_errors(exit_doc, "exit.schema.json")
        schema_errors += [f"exit.json {e}" for e in errors]
        row.update(exit_json_valid=not errors, exit_code=exit_doc.get("exit_code"), exit_status=exit_doc.get("status"), exit_category=exit_doc.get("category"),
                   exit_reason=exit_doc.get("reason", ""), terminate_process=exit_doc.get("terminate_process"))
    else:
        row.update(exit_json_valid=None, exit_code=None, exit_status=None, exit_category=None, exit_reason="", terminate_process=None)
    if manifest is not None and manifest.get("schema") != RUN_SCHEMA:
        # M10 (M8 review): a manifest of another kind - the viewer's physicstest.viewer/1 (top-level "mode": "viz", 02 11.2) -
        # is never a run; validate rejects it by its schema.
        row.update(manifest_valid=False, manifest_schema=manifest.get("schema"), mode=str(manifest.get("mode") or manifest.get("schema") or "?"))
        schema_errors.append(f"manifest.json schema {manifest.get('schema')!r} is not {RUN_SCHEMA}")
        manifest = None
    elif manifest is not None:
        errors = common.schema_errors(manifest, "manifest.schema.json")
        schema_errors += [f"manifest.json {e}" for e in errors]
        scene = manifest.get("scene") or {}
        run = manifest.get("run") or {}
        build = manifest.get("build") or {}
        harness = manifest.get("harness") or {}
        backend = manifest.get("backend") or {}
        profile = manifest.get("profile") or {}
        threads = manifest.get("threads") or {}
        platform = manifest.get("platform") or {}
        host = platform.get("host_load") or {}
        determinism = manifest.get("determinism") or {}
        summary = manifest.get("summary") or {}
        params = _scene_params(manifest)
        census = threads.get("census_window") if isinstance(threads.get("census_window"), dict) else {}
        clock = manifest.get("clock") or {}
        calibration = clock.get("calibration") if isinstance(clock.get("calibration"), dict) else {}
        stamp = manifest.get("conformance_stamp") if isinstance(manifest.get("conformance_stamp"), dict) else {}
        g10b = next((g for g in manifest.get("gates") or [] if isinstance(g, dict) and g.get("id") == "G10b"), {})
        row.update(
            manifest_valid=not errors,
            manifest_schema=manifest.get("schema"),
            # M10: the comparability key as written and as recomputed from the manifest's own fields (validate compares them)
            comparability_key=manifest.get("comparability_key"),
            comparability_key_recomputed=comparability.key_from_manifest(manifest),
            stamp_valid=stamp.get("valid") if stamp else None,
            stamp_reason=stamp.get("reason") if stamp else "no conformance_stamp (an M7-M9 manifest)",
            stamp_key=stamp.get("key_sha256") if stamp else None,
            cpu_eff=summary.get("cpu_eff") if isinstance(summary.get("cpu_eff"), (int, float)) else None,
            cpu_eff_raw=census.get("cpu_eff_raw"),
            foreign_fraction=threads.get("foreign_cycles_fraction") if isinstance(threads.get("foreign_cycles_fraction"), (int, float)) else None,
            g10b=g10b.get("result"),
            g10b_skip=census.get("g10b_skip"),   # M10 fix round 1: why G10b is Skip ("" = a verdict)
            tsc_hz=clock.get("tsc_hz") if isinstance(clock.get("tsc_hz"), (int, float)) else None,
            tsc_residual=calibration.get("max_residual"),
            unofficial_reasons=_json_text(manifest.get("unofficial_reasons") or []),
            run_id=manifest.get("run_id"),
            session=manifest.get("session"),
            status=manifest.get("status"),
            backend=backend.get("name"),
            variant=backend.get("variant") or "",
            backend_label=backend.get("label"),
            module_sha256=backend.get("module_sha256"),
            profile=profile.get("physics"),
            physics_sha256=profile.get("physics_sha256"),
            variant_sha256=profile.get("variant_sha256"),
            scenario=scene.get("name"),
            scene_version=scene.get("version"),
            params=_json_text(params),
            scene=scene_label(scene.get("name") or "?", params),
            scene_sha256=scene.get("scene_sha256"),
            desc_sha256=scene.get("desc_sha256"),
            options_sha256=scene.get("options_sha256"),
            num_bodies=scene.get("num_bodies"),
            N=scene.get("num_dynamic_bodies"),
            mode=run.get("mode"),
            instrumentation=_json_text(run.get("instrumentation") or []),
            frames=run.get("frames"),
            frames_run=run.get("frames_run"),
            dt_engine_s=run.get("dt_engine_s"),
            tick_mode=run.get("tick_mode"),
            hash_every=run.get("hash_every"),
            test_injections=_json_text(run.get("test_injections") or {}),
            T=threads.get("T"),
            affinity_mask=threads.get("affinity_mask"),
            thread_series=threads.get("series"),
            preset=harness.get("preset"),
            # M10 fix round 1: the SDKs compiled into the exe ("Jolt" = a single-SDK exe, the composition control; None before)
            build_sdks=",".join(map(str, harness["sdks"])) if isinstance(harness.get("sdks"), list) else None,
            do_check=harness.get("do_check"),
            exe_sha256=harness.get("exe_sha256"),
            git_sha=harness.get("git_sha"),
            flag_profile=build.get("flag_profile"),
            configuration=build.get("configuration"),
            timing_valid_for_headline=manifest.get("timing_valid_for_headline"),
            timing_invalid_reasons=_json_text(manifest.get("timing_invalid_reasons") or []),
            unofficial=manifest.get("unofficial"),
            power_scheme=platform.get("power_scheme"),
            power_scheme_guid=platform.get("power_scheme_guid"),
            power_scheme_high_performance=platform.get("power_scheme_high_performance"),
            cpu=platform.get("cpu"),
            contaminated=host.get("contaminated"),
            core_busy_other_pct=host.get("core_busy_other_pct"),
            contamination_limit_pct=host.get("threshold_total_pct"),
            tick_new_calls=(manifest.get("alloc") or {}).get("tick_new_calls"),
            h_native=determinism.get("h_native"),
            h_state=determinism.get("h_state"),
            h_native_frame=determinism.get("h_native_frame"),
            engine_error_bits=determinism.get("engine_error_bits"),
            gates_failed=_json_text([g.get("id") for g in manifest.get("gates") or [] if g.get("result") == "Fail"]),
            frame0_ns=summary.get("frame0_frame_ns"),
            windows=_json_text(scene.get("windows") or []),
        )
        windows = summary.get("windows") or {}
        for name in set(KNOWN_WINDOWS) | set(windows):
            entry = windows.get(name) or {}
            stats = entry.get("frame_ns") if isinstance(entry.get("frame_ns"), dict) else {}
            for stat in WINDOW_STATS:
                row[f"win_{name}_{stat}"] = stats.get(stat) if stat != "count" else entry.get("count")
            row[f"win_{name}_first"] = entry.get("first")
            row[f"win_{name}_last"] = entry.get("last")
            # M10: the batch measurement (one TSC interval over the window) and the whole Tick's mean
            row[f"win_{name}_batch"] = entry.get("batch_frame_ns") if isinstance(entry.get("batch_frame_ns"), (int, float)) else None
            tick = entry.get("tick_ns") if isinstance(entry.get("tick_ns"), dict) else {}
            row[f"win_{name}_tick_mean"] = tick.get("mean")
    elif "manifest_schema" not in row:
        row["manifest_valid"] = None
    row["schema_errors"] = _json_text(schema_errors)
    row["kind"] = info.get("kind") or "cell"
    row["cell_key"] = info.get("cell_key")
    # M10: the block of the run for the block bootstrap - the runner's repeat, else the manifest's order.repeat_index
    row["block"] = info.get("repeat") if info.get("repeat") is not None else (((manifest or {}).get("order") or {}).get("repeat_index") if manifest else None)
    label = info.get("label") or (default_label(manifest) if manifest is not None else (info.get("backend") or "?"))
    row["label"] = label
    for key in ("backend", "variant", "profile", "scenario", "T"):
        if row.get(key) is None and info.get(key) is not None:
            row[key] = info[key]
    if "params" not in row:
        params = {k: str(v) for k, v in (info.get("params") or {}).items()}
        row["params"] = _json_text(params)
        row["scene"] = scene_label(info.get("scenario") or "?", params)
    row["cell_id"] = info.get("cell_id") or f"{label}|{row.get('profile')}|{row.get('scene')}|T{row.get('T')}"
    row["outcome"] = info.get("outcome") or derive_outcome(row)
    return row, manifest


def derive_outcome(row: dict[str, Any]) -> str:
    """The runner's outcome rule for a directory the runner did not record (exit.json decides; the raw code never does)."""
    if not row["exit_json"]:
        return "timeout" if row.get("timed_out") else "no_exit_json"
    if row.get("exit_code") == 0:
        return "ok" if row.get("exit_json_valid") and row.get("manifest_valid") else "invalid_manifest"
    if row.get("exit_code") == 4:
        return "contaminated"
    return f"exit_{row.get('exit_code')}_{row.get('exit_status')}"


def read_timings(run_dir: pathlib.Path, run_key: str) -> pd.DataFrame | None:
    path = run_dir / "timings.csv"
    if not path.is_file():
        return None
    frame = pd.read_csv(path)
    if frame.empty:
        return None
    frame.insert(0, "run_key", run_key)
    return frame


def load(inputs: Iterable[str | pathlib.Path], with_frames: bool = True) -> Loaded:
    rows: list[dict[str, Any]] = []
    frames: list[pd.DataFrame] = []
    effective: list[dict[str, Any]] = []
    gates: list[dict[str, Any]] = []
    sessions: dict[str, dict[str, Any]] = {}
    sources = []
    for kind, path in discover(inputs):
        sources.append(f"{kind}:{path}")
        if kind == "session":
            document = common.read_json(path / "session.json")
            sessions[str(path)] = document
            cells = {c["key"]: c for c in document.get("cells", [])}
            for run in document.get("runs", []):
                cell = cells.get(run.get("cell_key"), {})
                info = dict(run)
                info.update(source=document.get("session_id"), source_kind="session", session_dir=str(path), label=run.get("label") or cell.get("label"),
                            params=run.get("params"))
                entries = [(path / run["out_dir"], info)]
                _collect(entries, rows, frames, effective, gates, with_frames)
        elif kind == "gate_g8":
            gate = common.read_json(path / "gate_g8.json")
            entries = []
            for index, child in enumerate(gate.get("runs", [])):
                child_dir = path / child.get("label", f"run{index}")
                entries.append((child_dir, {"source": str(path), "source_kind": "gate_g8", "run_name": child.get("label"), "repeat": index,
                                            "process_exit_code": child.get("process_exit_code"), "timed_out": child.get("timed_out", False)}))
            _collect(entries, rows, frames, effective, gates, with_frames)
        else:
            _collect([(path, {"source": str(path.parent), "source_kind": "run"})], rows, frames, effective, gates, with_frames)
    runs = pd.DataFrame(rows)
    return Loaded(
        runs=runs,
        frames=pd.concat(frames, ignore_index=True) if frames else pd.DataFrame(columns=["run_key"] + TIMING_COLUMNS),
        effective=pd.DataFrame(effective, columns=["run_key", "key", "native", "native_unit", "canonical", "unit", "source"]),
        gates=pd.DataFrame(gates, columns=["run_key", "id", "result", "checks", "n_failures", "failures"]),
        sessions=sessions,
        sources=sources,
    )


def _collect(entries, rows, frames, effective, gates, with_frames) -> None:
    for run_dir, info in entries:
        row, manifest = run_row(run_dir, info)
        rows.append(row)
        if manifest is None:
            continue
        if with_frames:
            timings = read_timings(run_dir, row["run_key"])
            if timings is not None:
                frames.append(timings)
        for entry in manifest.get("effective") or []:
            effective.append({"run_key": row["run_key"], "key": entry.get("key"), "native": str(entry.get("native")), "native_unit": entry.get("native_unit"),
                              "canonical": entry.get("canonical"), "unit": entry.get("unit"), "source": entry.get("source")})
        for gate in manifest.get("gates") or []:
            gates.append({"run_key": row["run_key"], "id": gate.get("id"), "result": gate.get("result"), "checks": gate.get("checks"),
                          "n_failures": len(gate.get("failures") or []), "failures": _json_text(gate.get("failures") or [])})


# Typed columns of the parquet files (nullable: a run without exit.json or manifest has missing values, not text).
BOOL_COLUMNS = ("timed_out", "exit_json", "manifest", "exit_json_valid", "manifest_valid", "terminate_process", "built", "do_check",
                "timing_valid_for_headline", "unofficial", "power_scheme_high_performance", "contaminated", "accepted", "stamp_valid")
INT_COLUMNS = ("attempt", "repeat", "order", "process_exit_code", "exit_code", "scene_version", "num_bodies", "N", "frames", "frames_run",
               "hash_every", "T", "tick_new_calls", "h_native_frame", "engine_error_bits", "frame", "num_steps", "checks", "n_failures")


def _is_missing(value: Any) -> bool:
    return value is None or value is pd.NA or (isinstance(value, float) and value != value)


def parquet_ready(table: pd.DataFrame) -> pd.DataFrame:
    """A copy with stable column types: known flags -> "boolean", known codes and counts -> "Int64", win_*_first/last/count ->
    "Int64", other object columns: lists and dicts as JSON text, all-bool / all-int / all-number columns typed, the rest text
    with missing values kept missing."""
    table = table.copy()
    for column in table.columns:
        series = table[column]
        integral = column in INT_COLUMNS or (column.startswith("win_") and column.endswith(("_first", "_last", "_count")))
        if column in BOOL_COLUMNS:
            table[column] = series.map(lambda v: pd.NA if _is_missing(v) else bool(v)).astype("boolean")
            continue
        if integral:
            table[column] = series.map(lambda v: pd.NA if _is_missing(v) else int(v)).astype("Int64")
            continue
        if series.dtype != object:
            continue
        present = [v for v in series if not _is_missing(v)]
        if present and all(isinstance(v, bool) for v in present):
            table[column] = series.map(lambda v: pd.NA if _is_missing(v) else v).astype("boolean")
        elif present and all(isinstance(v, int) and not isinstance(v, bool) for v in present):
            table[column] = series.map(lambda v: pd.NA if _is_missing(v) else v).astype("Int64")
        elif present and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in present):
            table[column] = series.map(lambda v: pd.NA if _is_missing(v) else float(v)).astype("Float64")
        else:
            table[column] = series.map(lambda v: None if _is_missing(v) else (v if isinstance(v, str) else
                                                                               (json.dumps(v, sort_keys=True, ensure_ascii=False) if isinstance(v, (list, dict)) else str(v))))
    return table


def write_parquet(loaded: Loaded, out_dir: pathlib.Path) -> list[pathlib.Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name in ("runs", "frames", "effective", "gates"):
        table = parquet_ready(getattr(loaded, name))
        path = out_dir / f"{name}.parquet"
        table.to_parquet(path, index=False)
        written.append(path)
    return written


def main(args) -> int:
    missing = [p for p in args.inputs if not pathlib.Path(p).exists()]
    if missing:
        print(f"ptbench load: no such path: {', '.join(missing)}")
        return 2
    loaded = load(args.inputs)
    if loaded.runs.empty:
        print("ptbench load: no run found under " + ", ".join(args.inputs))
        return 1
    out = pathlib.Path(args.out) if args.out else (pathlib.Path(next(iter(loaded.sessions))) / "analysis" if len(loaded.sessions) == 1 else pathlib.Path(args.inputs[0]) / "analysis")
    written = write_parquet(loaded, out)
    invalid = loaded.runs[(loaded.runs["manifest_valid"] == False) | (loaded.runs["exit_json_valid"] == False)]  # noqa: E712
    print(f"{len(loaded.runs)} runs ({len(invalid)} with schema errors), {len(loaded.frames)} frames, {len(loaded.effective)} effective rows, "
          f"{len(loaded.gates)} gate rows from {len(loaded.sources)} source(s)")
    for path in written:
        print(f"  {path}")
    return 0
