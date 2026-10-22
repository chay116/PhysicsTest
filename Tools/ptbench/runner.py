"""runner - unattended execution of a run matrix (03_WorkPlan M8 task 3; 02_Architecture 12.5; DL52).

    python -I -m ptbench run --matrix smoke [--preset test | --exe <PhysicsTestBench.exe>] [--out <dir>] [--report]
                             [--allow-balanced] [--allow-busy] [--allow-concurrent] [--idle-wait S] [--repeats N] [--dry-run]

Order of work:
  1. the read-only environment gate (envcheck): refused -> session.json with status "refused", exit code 3, nothing runs.
  2. cells = targets x profiles x scenes x T (matrix.expand_cells); blocks = one per repeat, each a seeded shuffle of all
     cells (matrix.plan_blocks). The bench gets -Repeat, -Order, -BlockSeed, -Session and its own -Out run directory.
  3. every run is a fresh process (1 run = 1 process), after a cooldown (2 s by default; the system load during it is
     recorded), with a timeout (the process is killed: timed_out, no exit.json). A run that has written exit.json but does
     not end within TEARDOWN_GRACE_S (a hang in TermGamePhys or a static teardown, the DL52 case) is killed then and keeps
     its exit.json verdict, with teardown_killed true and a note. The bench is held in a job object with
     JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE (like M7's FProcessSpawner for -Gate= children), so it dies with the runner (Ctrl+C,
     a CTest timeout, a crash); the wait is also wrapped in try/finally. The matrix's, the targets' and --bench-arg switches
     never repeat a key or set one the runner sets (matrix.check_switches: the bench reads the first occurrence only).
  4. the verdict comes from exit.json, never from the raw process exit code (02 12.2, DL52): a run without exit.json failed
     ("no_exit_json", or "timeout"), whatever the process returned. exit.json and manifest.json are validated against
     Config/Schemas on the spot.
  5. exit code 4 (contaminated host): the same run again in a fresh process after the cooldown, up to
     contaminated_retries (default 2) more times (M8 lead decision); every attempt is a row of index.csv and session.json; a
     repeat whose attempts all end with 4 is 'contaminated', and so is its cell.
  6. outputs: <session dir>/session.json (schema physicstest.session/1, rewritten after every run, so a killed runner
     still leaves a valid file), index.csv (one row per attempt), runs/<r>_<order>_<cell key>[_a<n>]/ (the bench's run
     directories, child.log = the bench's stdout and stderr) and, at the end, the per-cell CV report (reported only: the CV
     gate is M10). The cell values are the headline-valid ok runs only (the report's validate rule); ok runs that are not
     headline-valid (a dev / dbg build, -HostCheck=record with a recorded contamination, PreRoll < 0.5) are counted in
     n_not_headline_valid and give no value, so the console table never shows a CV the report would not.
Exit code: 0 every cell ok, 1 a cell failed or was contaminated, or session.json does not match its schema, 2 no matrix or
bench executable, or a bench switch that repeats a key / sets a runner switch, 3 refused by the environment gate. (A cell
whose runs are ok but not headline-valid is still ok: the runs ran; the console table and the report show that they give no
timing value.)
"""
from __future__ import annotations

import csv
import ctypes
import io
import json
import math
import pathlib
import statistics
import subprocess
import sys
import time
from ctypes import wintypes
from typing import Any

from . import __version__, common, envcheck
from .matrix import Matrix, MatrixError, PlannedRun, check_switches, load_matrix

SESSION_SCHEMA = "session.schema.json"
INDEX_COLUMNS = [
    "session", "run_name", "attempt", "cell_id", "cell_key", "label", "backend", "variant", "profile", "scenario", "params", "T", "frames",
    "repeat", "order", "block_seed", "out_dir", "started_utc", "wall_s", "cooldown_s", "cooldown_system_busy_pct", "timed_out", "teardown_killed",
    "process_exit_code", "exit_json", "exit_code", "exit_status", "exit_category", "outcome", "retried", "manifest_valid", "exit_json_valid",
    "timing_valid_for_headline", "window", "window_mean_ns", "window_count", "core_busy_other_pct", "contamination_limit_pct",
    "desc_sha256", "flag_profile", "h_native", "frames_run", "schema_errors",
]
RETRY_EXIT_CODE = 4
POLL_S = 1.0               # while a run is alive the runner looks for its exit.json once per second (one stat; no busy wait)
TEARDOWN_GRACE_S = 30.0    # a run still alive this long after its exit.json is killed (teardown hang); its verdict stands


def git_facts(repo: pathlib.Path) -> dict[str, Any]:
    """HEAD and whether the work tree is dirty (read-only git commands; git.exe by its full path from PATH, never from the
    current directory)."""
    facts: dict[str, Any] = {"sha": None, "dirty": None}
    git = common.find_executable("git")
    if git is None:
        return facts
    try:
        sha = subprocess.run([git, "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True, timeout=30, check=False)
        if sha.returncode == 0:
            facts["sha"] = sha.stdout.strip()
        status = subprocess.run([git, "-C", str(repo), "status", "--porcelain"], capture_output=True, text=True, timeout=60, check=False)
        if status.returncode == 0:
            facts["dirty"] = bool(status.stdout.strip())
    except (OSError, subprocess.TimeoutExpired):
        pass
    return facts


# ---- child containment: a job object that kills the bench when the runner's handle closes ---------------------------------

class _BasicLimit(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64), ("LimitFlags", wintypes.DWORD),
                ("MinimumWorkingSetSize", ctypes.c_size_t), ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class _IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount", "ReadTransferCount",
                                                     "WriteTransferCount", "OtherTransferCount")]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _BasicLimit), ("IoInfo", _IoCounters), ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000


class KillOnCloseJob:
    """One job object for the runner's lifetime with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: the handle is never inherited and
    closes when the runner process ends, however it ends, and Windows then terminates every bench still in the job. No
    other limit is set (the bench keeps its own affinity and HIGH_PRIORITY_CLASS). Best effort: when the job cannot be
    created or a process not assigned, `error` says why and the runner still kills the child in its finally block."""

    def __init__(self) -> None:
        self.handle = None
        self.error = ""
        try:
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
            kernel32.CreateJobObjectW.restype = wintypes.HANDLE
            kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
            kernel32.SetInformationJobObject.restype = wintypes.BOOL
            kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
            kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
            self._kernel32 = kernel32
            handle = kernel32.CreateJobObjectW(None, None)
            if not handle:
                raise OSError(ctypes.get_last_error(), "CreateJobObjectW failed")
            info = _ExtendedLimit()
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if not kernel32.SetInformationJobObject(handle, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(info), ctypes.sizeof(info)):
                raise OSError(ctypes.get_last_error(), "SetInformationJobObject failed")
            self.handle = handle
        except (OSError, AttributeError) as error:
            self.error = f"no job object: {error}"

    def assign(self, process: subprocess.Popen) -> bool:
        if self.handle is None:
            return False
        if not self._kernel32.AssignProcessToJobObject(self.handle, int(process._handle)):   # Popen._handle: the process handle (Windows)
            self.error = f"AssignProcessToJobObject failed: {ctypes.get_last_error()}"
            return False
        return True


_JOB: KillOnCloseJob | None = None


def _job() -> KillOnCloseJob:
    global _JOB
    if _JOB is None:
        _JOB = KillOnCloseJob()
    return _JOB


def window_value(manifest: dict[str, Any] | None, window: str) -> tuple[float | None, int | None]:
    """The run's value: the window's T_frame mean (docs/TimingContract.md 3) and its frame count."""
    if not isinstance(manifest, dict):
        return None, None
    entry = (((manifest.get("summary") or {}).get("windows") or {}).get(window)) or {}
    stats = entry.get("frame_ns")
    if not isinstance(stats, dict):
        return None, entry.get("count")
    return stats.get("mean"), entry.get("count")


def execute(exe: pathlib.Path, args: list[str], out_dir: pathlib.Path, timeout_s: float, teardown_grace_s: float = TEARDOWN_GRACE_S,
            poll_s: float = POLL_S) -> dict[str, Any]:
    """One fresh bench process; stdout + stderr go to <out_dir>/child.log. Never interprets the exit code.
    The process is killed (TerminateProcess) when it outlives timeout_s (timed_out: no exit.json), when it is still alive
    teardown_grace_s after its exit.json appeared (teardown_killed: the exit.json verdict stands; a deadline reached after
    exit.json counts as this too), and in the finally block whenever the wait ends by an exception (Ctrl+C, an error)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    command = [str(exe)] + args + [f"-Out={out_dir}"]
    started = time.perf_counter()
    result: dict[str, Any] = {"command": command, "timed_out": False, "teardown_killed": False, "process_exit_code": None, "spawn_error": "",
                              "exit_json_after_s": None}
    exit_json = out_dir / "exit.json"
    with open(out_dir / "child.log", "wb") as log:
        try:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=str(out_dir))
        except OSError as error:
            result["spawn_error"] = str(error)
            result["wall_s"] = time.perf_counter() - started
            return result
        _job().assign(process)
        try:
            deadline = started + timeout_s
            exit_seen = None
            while True:
                try:
                    result["process_exit_code"] = process.wait(timeout=max(0.01, min(poll_s, deadline - time.perf_counter())))
                    break
                except subprocess.TimeoutExpired:
                    pass
                now = time.perf_counter()
                if exit_seen is None and exit_json.is_file():   # written atomically (temporary file + rename), last of the run's outputs
                    exit_seen = now
                    result["exit_json_after_s"] = round(now - started, 3)
                if exit_seen is not None and now - exit_seen >= teardown_grace_s:
                    result["teardown_killed"] = True
                    break
                if now >= deadline:
                    result["teardown_killed" if exit_seen is not None else "timed_out"] = True
                    break
        finally:
            if process.poll() is None:
                process.kill()   # TerminateProcess: a timed-out run never leaves exit.json; a teardown hang keeps the one it wrote
                result["process_exit_code"] = process.wait()
    result["wall_s"] = time.perf_counter() - started
    return result


NO_MANIFEST_STATUSES = ("fatal", "usage_error", "unsupported")
# M9: the bench refuses a scene whose required capabilities (joints) the backend lacks before it initialises the backend:
# exit.json 2, status "unsupported". Not a failed run: the cell is "unsupported" and does not fail the session. M9 fix round 1:
# a module that declares the capability but was built against another PT_ABI_PROVISIONAL_REVISION (the host masks it) is not
# "unsupported": the bench exits 6 backend_unavailable and the cell fails (docs/ResultsSchema.md 2, status).
UNSUPPORTED_STATUS = "unsupported"


def manifest_expected(exit_code: Any, exit_status: Any, has_exit_json: bool) -> bool:
    """Whether a run with this exit.json must have written manifest.json (ResultsSchema 1): exit codes 0, 3, 4 and 5 do,
    except a Fatal run (exit 3, status fatal: the Fatal hook writes exit.json only) and a usage error (exit 2); exit 6 and a
    run without exit.json never do."""
    return has_exit_json and exit_code in (0, 3, 4, 5) and exit_status not in NO_MANIFEST_STATUSES


def judge(out_dir: pathlib.Path, process: dict[str, Any], window: str) -> dict[str, Any]:
    """The run's verdict from exit.json (+ the manifest's facts); the raw process exit code is only recorded."""
    exit_doc, exit_error = common.try_read_json(out_dir / "exit.json")
    manifest, manifest_error = common.try_read_json(out_dir / "manifest.json")
    row: dict[str, Any] = {"exit_json": exit_doc is not None, "schema_errors": []}
    if exit_doc is None:
        row.update(exit_code=None, exit_status=None, exit_category=None, exit_json_valid=None)
        if process.get("timed_out"):
            row["outcome"] = "timeout"
        elif process.get("spawn_error"):
            row["outcome"] = "spawn_error"
        else:
            row["outcome"] = "no_exit_json"   # died half-way: whatever the raw code says, the run failed
        row["reason"] = f"exit.json {exit_error} (raw process exit code {process.get('process_exit_code')}, not trusted)"
    else:
        errors = common.schema_errors(exit_doc, "exit.schema.json")
        row["exit_json_valid"] = not errors
        row["schema_errors"] += [f"exit.json {e}" for e in errors]
        row["exit_code"] = exit_doc.get("exit_code")
        row["exit_status"] = exit_doc.get("status")
        row["exit_category"] = exit_doc.get("category")
        row["reason"] = exit_doc.get("reason", "")
        if row["exit_code"] == 0 and not errors:
            row["outcome"] = "ok"
        elif row["exit_code"] == 2 and row["exit_status"] == UNSUPPORTED_STATUS and not errors:
            row["outcome"] = "unsupported"
        elif row["exit_code"] == RETRY_EXIT_CODE:
            row["outcome"] = "contaminated"
        elif errors:
            row["outcome"] = "invalid_exit_json"
        else:
            row["outcome"] = f"exit_{row['exit_code']}_{row['exit_status']}"
        if process.get("process_exit_code") is not None and process["process_exit_code"] != row["exit_code"]:
            row["note"] = f"raw process exit code {process['process_exit_code']} differs from exit.json {row['exit_code']} (exit.json is trusted)"
    if manifest is None:
        row["manifest_valid"] = None
        if manifest_expected(row.get("exit_code"), row.get("exit_status"), exit_doc is not None):
            row["schema_errors"].append(f"manifest.json {manifest_error}")
            if row["outcome"] == "ok":
                row["outcome"] = "invalid_manifest"
    else:
        errors = common.schema_errors(manifest, "manifest.schema.json")
        row["manifest_valid"] = not errors
        row["schema_errors"] += [f"manifest.json {e}" for e in errors]
        if errors and row["outcome"] == "ok":
            row["outcome"] = "invalid_manifest"
        mean, count = window_value(manifest, window)
        host = (manifest.get("platform") or {}).get("host_load") or {}
        row.update(
            timing_valid_for_headline=manifest.get("timing_valid_for_headline"),
            timing_invalid_reasons=manifest.get("timing_invalid_reasons"),
            window_mean_ns=mean,
            window_count=count,
            core_busy_other_pct=host.get("core_busy_other_pct"),
            contamination_limit_pct=host.get("threshold_total_pct"),
            desc_sha256=(manifest.get("scene") or {}).get("desc_sha256"),
            flag_profile=(manifest.get("build") or {}).get("flag_profile"),
            h_native=(manifest.get("determinism") or {}).get("h_native"),
            frames_run=(manifest.get("run") or {}).get("frames_run"),
            exe_sha256=(manifest.get("harness") or {}).get("exe_sha256"),
            power_scheme_high_performance=(manifest.get("platform") or {}).get("power_scheme_high_performance"),
        )
    return row


def summarize_cells(cells: list[dict[str, Any]], runs: list[dict[str, Any]], repeats: int) -> None:
    """Per cell: the state of every repeat (its last attempt), the runs that count, and the reported CV (M8: not a gate).
    Only an ok run with timing_valid_for_headline true gives a value (the report's validate rule); an ok run that is not
    headline-valid is counted in n_not_headline_valid."""
    by_key: dict[str, list[dict[str, Any]]] = {}
    for run in runs:
        by_key.setdefault(run["cell_key"], []).append(run)
    for cell in cells:
        attempts = by_key.get(cell["key"], [])
        repeat_states = []
        values = []
        not_headline_valid = 0
        for repeat in range(repeats):
            tries = sorted((a for a in attempts if a["repeat"] == repeat), key=lambda a: a["attempt"])
            if not tries:
                repeat_states.append("not_run")
                continue
            last = tries[-1]
            if last["outcome"] == "ok":
                repeat_states.append("ok")
                if last.get("timing_valid_for_headline") is not True:
                    not_headline_valid += 1
                elif last.get("window_mean_ns") is not None:
                    values.append(float(last["window_mean_ns"]))
            elif all(t["outcome"] == "contaminated" for t in tries):
                repeat_states.append("contaminated")
            elif last["outcome"] == "unsupported":
                repeat_states.append("unsupported")
            else:
                repeat_states.append("failed")
        states = set(repeat_states)
        if states == {"ok"}:
            state = "ok"
        elif states == {"unsupported"}:
            state = "unsupported"
        elif "failed" in states:
            state = "failed"
        elif "contaminated" in states:
            state = "contaminated"
        else:
            state = "incomplete"
        cell.update(
            state=state,
            repeat_states=repeat_states,
            n_attempts=len(attempts),
            n_ok=repeat_states.count("ok"),
            n_not_headline_valid=not_headline_valid,
            n_contaminated_attempts=sum(1 for a in attempts if a["outcome"] == "contaminated"),
            values_ns=values,
            median_ns=statistics.median(values) if values else None,
            mean_ns=(sum(values) / len(values)) if values else None,
            cv_pct=common.cv_percent(values) if len(values) >= 2 else None,
        )


def write_index(path: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=INDEX_COLUMNS, extrasaction="ignore", lineterminator="\n")
    writer.writeheader()
    for row in rows:
        flat = dict(row)
        flat["params"] = json.dumps(row.get("params", {}), sort_keys=True)
        flat["schema_errors"] = " | ".join(row.get("schema_errors", []))
        writer.writerow({k: ("" if flat.get(k) is None else flat.get(k)) for k in INDEX_COLUMNS})
    common.write_text_atomic(path, buffer.getvalue())


def format_ms(value_ns: float | None) -> str:
    if value_ns is None or (isinstance(value_ns, float) and math.isnan(value_ns)):
        return "-"
    return f"{value_ns / 1e6:.3f} ms" if value_ns >= 1e5 else f"{value_ns / 1e3:.2f} us"


class Session:
    """session.json + index.csv of one matrix execution (rewritten after every run)."""

    def __init__(self, directory: pathlib.Path, document: dict[str, Any]) -> None:
        self.directory = directory
        self.document = document

    def save(self) -> None:
        self.document["summary"] = self.summary()
        common.write_json_atomic(self.directory / "session.json", self.document)
        write_index(self.directory / "index.csv", self.document["runs"])

    def summary(self) -> dict[str, Any]:
        runs = self.document["runs"]
        cells = self.document["cells"]
        outcomes: dict[str, int] = {}
        for run in runs:
            outcomes[run["outcome"]] = outcomes.get(run["outcome"], 0) + 1
        return {
            "planned_runs": len(self.document["plan"]),
            "attempts": len(runs),
            "outcomes": outcomes,
            "retries": sum(1 for run in runs if run["attempt"] > 0),
            "cells": len(cells),
            "cells_by_state": {state: sum(1 for c in cells if c.get("state") == state) for state in sorted({c.get("state", "pending") for c in cells})},
            "no_exit_json": sum(1 for run in runs if not run.get("exit_json")),
            "timed_out": sum(1 for run in runs if run.get("timed_out")),
            "teardown_killed": sum(1 for run in runs if run.get("teardown_killed")),
            "schema_errors": sum(1 for run in runs if run.get("schema_errors")),
        }


def run_matrix(matrix: Matrix, exe: pathlib.Path, preset: str | None, out_root: pathlib.Path, gate: dict[str, Any], repeats: int | None = None,
               cooldown_s: float | None = None, timeout_s: float | None = None, extra_args: list[str] | None = None, session_name: str | None = None,
               dry_run: bool = False, log=None) -> tuple[pathlib.Path | None, int]:
    """Runs the matrix; returns (session dir, exit code: 0 every cell ok, 1 a cell failed or contaminated, 3 refused)."""
    log = log or (lambda *parts: print(*parts, flush=True))   # flushed: the progress is readable while a session runs
    repeats = matrix.repeats if repeats is None else repeats
    cooldown_s = float(matrix.get("cooldown_s", 2.0) if cooldown_s is None else cooldown_s)
    timeout_s = float(matrix.get("timeout_s", 600.0) if timeout_s is None else timeout_s)
    retries = int(matrix.get("contaminated_retries", 2))
    window = matrix.get("window", "headline")
    check_switches(matrix.document, extra_args)   # MatrixError: a repeated switch key or a switch the runner sets (FParse: first wins)
    bench_args = list(matrix.get("bench_args", [])) + list(extra_args or [])
    cells = matrix.cells()
    plan = matrix.plan(repeats)
    session_id = session_name or f"{matrix.name}-{common.utc_stamp()}"
    if dry_run:
        log(f"dry run: {len(cells)} cells x {repeats} repeats = {len(plan)} runs, exe {exe}")
        for planned in plan:
            log(f"  r{planned.repeat} #{planned.order:03d} seed 0x{planned.block_seed:016x} {planned.cell.cell_id}")
        return None, 0
    session_dir = out_root / session_id
    session_dir.mkdir(parents=True, exist_ok=False)
    unofficial = ["no conformance stamp (M10)", "no CV gate / claim rules (M10)"]
    if not gate["power_plan_high_performance"]:
        unofficial.append(f"power plan is not High performance: {gate['power_scheme'].get('name')} ({gate['power_scheme'].get('kind')})")
    if matrix.purpose == "perf" and repeats < 5:
        unofficial.append(f"R = {repeats} < 5 (02 12.5)")
    exe_sha = common.sha256_file(exe) if exe.exists() else None
    document: dict[str, Any] = {
        "schema": "physicstest.session/1",
        "session_id": session_id,
        "status": "refused" if gate["refused"] else "running",
        "started_utc": common.utc_now(),
        "finished_utc": None,
        "wall_s": None,
        "matrix": {"name": matrix.name, "path": str(matrix.path), "sha256": matrix.sha256, "purpose": matrix.purpose, "repeats": repeats,
                   "repeats_in_file": matrix.repeats, "block_seed": matrix.seed, "document": matrix.document},
        "runner": {"ptbench": __version__, "python": sys.version.split()[0], "command_line": sys.argv, "cooldown_s": cooldown_s, "timeout_s": timeout_s,
                   "teardown_grace_s": TEARDOWN_GRACE_S, "contaminated_retries": retries, "window": window, "bench_args": bench_args,
                   "verdict_source": "exit.json (the raw process exit code is recorded only)"},
        "bench": {"exe": str(exe), "exe_sha256": exe_sha, "preset": preset},
        "git": git_facts(common.repo_root()),
        "environment": gate,
        "bypasses": gate["bypasses"],
        "unofficial": True,
        "unofficial_reasons": unofficial,
        "blocks": [],
        "plan": [{"run_name": p.run_name, "repeat": p.repeat, "order": p.order, "block_seed": p.block_seed, "cell_key": p.cell.key} for p in plan],
        "cells": [c.as_json() for c in cells],
        "runs": [],
    }
    for repeat in range(repeats):
        block = [p for p in plan if p.repeat == repeat]
        document["blocks"].append({"repeat": repeat, "block_seed": block[0].block_seed if block else None, "order": [p.cell.key for p in block]})
    session = Session(session_dir, document)
    if gate["refused"]:
        session.save()
        log(f"refused by the environment gate (bypass flags: --allow-balanced, --allow-busy, --allow-concurrent; --idle-wait): {session_dir / 'session.json'}")
        return session_dir, 3
    if not exe.exists():
        document["status"] = "aborted"
        document["abort_reason"] = f"no bench executable at {exe}"
        session.save()
        log(document["abort_reason"])
        return session_dir, 2
    job = _job()
    document["runner"]["child_containment"] = "job object, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE" if job.handle is not None else job.error
    session.save()
    log(f"session {session_id}: {len(cells)} cells x {repeats} repeats = {len(plan)} runs -> {session_dir}")
    started = time.perf_counter()
    sampler = envcheck.SystemBusySampler()
    try:
        for index, planned in enumerate(plan):
            for attempt in range(retries + 1):
                row = run_once(planned, attempt, exe, bench_args, session_id, session_dir, cooldown_s, timeout_s, window, sampler)
                row["retried"] = row["outcome"] == "contaminated" and attempt < retries
                document["runs"].append(row)
                summarize_cells(document["cells"], document["runs"], repeats)
                session.save()
                log(f"[{index + 1}/{len(plan)}] {planned.run_name}{'' if attempt == 0 else f' attempt {attempt}'} {planned.cell.cell_id}: "
                    f"{row['outcome']} (exit.json {row.get('exit_code')}, process {row.get('process_exit_code')}) "
                    f"{format_ms(row.get('window_mean_ns'))}{'' if row.get('timing_valid_for_headline') else ' [not headline-valid]'} {row['wall_s']:.1f} s"
                    f"{' [killed ' + str(TEARDOWN_GRACE_S) + ' s after exit.json: teardown hang]' if row.get('teardown_killed') else ''}")
                if row["outcome"] != "contaminated":
                    break
        document["status"] = "complete"
    except KeyboardInterrupt:
        document["status"] = "aborted"
        document["abort_reason"] = "interrupted"
    except Exception as error:
        # Anything else (a disk error, a bug): session.json says aborted with the reason, then the error propagates. The
        # running bench was already killed by execute's finally block.
        document["status"] = "aborted"
        document["abort_reason"] = f"{type(error).__name__}: {error}"[:2000]
        document["finished_utc"] = common.utc_now()
        document["wall_s"] = time.perf_counter() - started
        try:
            summarize_cells(document["cells"], document["runs"], repeats)
            session.save()
        except Exception:   # noqa: BLE001 - the original error matters more than a failed save
            pass
        raise
    document["finished_utc"] = common.utc_now()
    document["wall_s"] = time.perf_counter() - started
    summarize_cells(document["cells"], document["runs"], repeats)
    session.save()
    errors = common.schema_errors(document, SESSION_SCHEMA)
    log(cv_table(document))
    if errors:
        log("ERROR: session.json does not match Config/Schemas/session.schema.json (exit code 1): " + "; ".join(errors))
    bad = [c for c in document["cells"] if c.get("state") not in ("ok", "unsupported")]   # M9: unsupported is reported, not failed
    return session_dir, (0 if not bad and not errors and document["status"] == "complete" else 1)


def run_once(planned: PlannedRun, attempt: int, exe: pathlib.Path, bench_args: list[str], session_id: str, session_dir: pathlib.Path, cooldown_s: float,
             timeout_s: float, window: str, sampler: envcheck.SystemBusySampler) -> dict[str, Any]:
    cell = planned.cell
    name = planned.run_name + (f"_a{attempt}" if attempt else "")
    out_dir = session_dir / "runs" / name
    busy = None
    if cooldown_s > 0:
        sampler.start()
        time.sleep(cooldown_s)
        busy = round(sampler.stop(), 3)
    # The cell's switches, the runner's bookkeeping, the matrix's / --bench-arg switches, then the target's. check_switches
    # guarantees that no key appears twice (the bench reads the first occurrence only), so this order decides nothing.
    args = (cell.bench_args(include_target_args=False) + [f"-Repeat={planned.repeat}", f"-Order={planned.order}", f"-BlockSeed={planned.block_seed}",
                                                          f"-Session={session_id}"] + bench_args + list(cell.target_args))
    started_utc = common.utc_now()
    process = execute(exe, args, out_dir, timeout_s)
    verdict = judge(out_dir, process, window)
    row = {
        "session": session_id, "run_name": name, "attempt": attempt, "cell_id": cell.cell_id, "cell_key": cell.key, "label": cell.label, "backend": cell.backend,
        "variant": cell.variant, "profile": cell.profile, "scenario": cell.scenario, "params": cell.params_dict, "T": cell.threads, "frames": cell.frames,
        "repeat": planned.repeat, "order": planned.order, "block_seed": planned.block_seed, "out_dir": str(out_dir.relative_to(session_dir)),
        "started_utc": started_utc, "wall_s": round(process["wall_s"], 4), "cooldown_s": cooldown_s, "cooldown_system_busy_pct": busy,
        "timed_out": process["timed_out"], "teardown_killed": process["teardown_killed"], "exit_json_after_s": process["exit_json_after_s"],
        "process_exit_code": process["process_exit_code"], "spawn_error": process["spawn_error"], "window": window, "command": process["command"],
    }
    row.update(verdict)
    if process["teardown_killed"]:
        # Not a failure: exit.json holds the run's verdict (DL52); the process hung in its teardown and was killed.
        note = f"teardown_timeout: still running {TEARDOWN_GRACE_S:g} s after exit.json, killed (exit.json is trusted)"
        row["note"] = note + ("; " + row["note"] if row.get("note") else "")
    return row


def cv_table(document: dict[str, Any]) -> str:
    """The per-cell CV report printed at the end (M8: reported only; the CV < 2 % target is the M10 gate)."""
    lines = [f"per-cell result ({document['runner']['window']} window mean of each ok run; CV = sample stdev / mean; target CV < 2 % is M10's gate, reported only):",
             f"  {'cell':<58} {'state':<12} {'ok':>5} {'values':>6} {'median':>12} {'CV':>8}"]
    for cell in document["cells"]:
        cv = cell.get("cv_pct")
        values = len(cell.get("values_ns") or [])
        lines.append(f"  {cell['cell_id']:<58} {cell.get('state', '-'):<12} {cell.get('n_ok', 0):>2}/{len(cell.get('repeat_states', [])):<2} {values:>6} "
                     f"{format_ms(cell.get('median_ns')):>12} {('-' if cv is None else f'{cv:.2f}%'):>8}")
    hidden = sum(cell.get("n_not_headline_valid") or 0 for cell in document["cells"])
    if hidden:
        lines.append(f"  {hidden} ok run(s) are not headline-valid and give no value (see manifest timing_invalid_reasons; the report rejects them too)")
    if document.get("unofficial_reasons"):
        lines.append("UNOFFICIAL: " + "; ".join(document["unofficial_reasons"]))
    return "\n".join(lines)


def main(args) -> int:
    extra = list(args.bench_arg or [])
    try:
        matrix = load_matrix(args.matrix)
        check_switches(matrix.document, extra)   # before the environment gate: a usage error costs no idle wait
    except (MatrixError, OSError, ValueError) as error:
        print(f"ptbench run: {error}", file=sys.stderr)
        return 2
    preset = args.preset or matrix.get("preset") or "test"
    exe = pathlib.Path(args.exe).resolve() if args.exe else common.default_bench_exe(preset)
    out_root = pathlib.Path(args.out).resolve() if args.out else common.default_results_dir()
    if args.dry_run:
        gate = {"refused": False, "bypasses": [], "power_plan_high_performance": True, "power_scheme": {}}
        run_matrix(matrix, exe, preset, out_root, gate, repeats=args.repeats, extra_args=extra, dry_run=True)
        return 0
    gate = envcheck.run_envcheck(allow_balanced=args.allow_balanced, allow_busy=args.allow_busy, allow_concurrent=args.allow_concurrent,
                                 skip_idle=args.skip_idle, idle_wait_s=args.idle_wait)
    print(envcheck.describe(gate))
    session_dir, code = run_matrix(matrix, exe, preset if not args.exe else None, out_root, gate, repeats=args.repeats, cooldown_s=args.cooldown,
                                   timeout_s=args.timeout, extra_args=extra, session_name=args.session)
    if session_dir is not None:
        print(f"session: {session_dir}")
    if args.report and session_dir is not None and code in (0, 1):
        from .analysis import report

        path = report.build_report(session_dir)
        print(f"report: {path}")
    return code
