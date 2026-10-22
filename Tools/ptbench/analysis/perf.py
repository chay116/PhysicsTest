"""perf - timing statistics of accepted runs (02_Architecture 13 "perf"; 03_WorkPlan M8 task 4, M10 task 5;
docs/TimingContract.md 3).

  run value      the scene window's T_frame mean of the run (manifest summary.windows.<window>.frame_ns.mean)
  cell           one series of one table (validate: one comparability key): median over runs of the run values (the
                 headline statistic) with its percentile-bootstrap 95 % CI (M10, B = 10000), mean, min / max, CV = sample
                 stdev / mean of the run values (the runner's M10 CV gate marks a cell above 2 % after its extra blocks as
                 noisy), p95 / p99 = nearest rank of every window frame of every run of the cell pooled (frames table),
                 cpu_eff (M10: the median of the runs' census window) and the batch measurement of the window (M10)
  frame series   per frame of the window: median and IQR (q25, q75) of frame_ns across the runs
  N-scaling      alpha of t = c N^alpha over the cells of one series that differ only in the body count N (least squares in
                 log-log; scene.num_dynamic_bodies)
  S(T), E(T)     t(T = 1) / t(T) and S / T, with the cpu_eff of the T cell next to them (M10); series of other thread shapes
                 are other tables (thread_series is in the comparability key)
  overhead       t = a + b N over the performance scenes of one series (P0 / P1 / M3: per-frame fixed cost a, per-body cost b)
  ratios/claims  M10: for every pair of series of a table, rho = median(A) / median(B) with its block-bootstrap 95 % CI, and the
                 claim rule over the Shipped and MatchedAVX2 tables of the same scene, profile, T and thread series (claims.py);
                 M10 fix round 1: a pair with a noisy cell (CV gate) carries no claim (docs/TimingContract.md 9)
  composition    M10: a single-SDK exe's series (the binary composition control, 02 14.3) against the same label from a
                 multi-SDK exe; fix round 1: told apart by the manifest's harness.sdks (the build's content), not the preset name
A table that validate refuses (mixed flag profiles or desc_sha256) gets one row per series with its table_error and n_runs
but no statistic (every value NaN, values_ns empty): it is shown as refused, never computed.
"""
from __future__ import annotations

import math
import pathlib
from typing import Any

import numpy as np
import pandas as pd

from .. import common
from . import bootstrap, claims, validate

SERIES_KEYS = ("label", "profile", "flag_profile", "T", "thread_series")


def run_values(accepted: pd.DataFrame, window: str) -> pd.Series:
    column = f"win_{window}_mean"
    if column not in accepted.columns:
        return pd.Series([math.nan] * len(accepted), index=accepted.index)
    return pd.to_numeric(accepted[column], errors="coerce")


def frames_by_run(frames: pd.DataFrame) -> dict[str, pd.DataFrame]:
    """The frames table split by run_key once (pooled_frames would otherwise filter the whole table once per run)."""
    if frames.empty:
        return {}
    return {key: group for key, group in frames.groupby("run_key", sort=False)}


def _missing(value: Any) -> bool:
    return value is None or value is pd.NA or (isinstance(value, float) and math.isnan(value))


def pooled_frames(frames: pd.DataFrame | dict[str, pd.DataFrame], runs: pd.DataFrame, window: str) -> pd.Series:
    """frame_ns of every frame inside each run's window (the bounds the manifest reports for that run). frames: the frames
    table or its frames_by_run split."""
    by_run = frames if isinstance(frames, dict) else frames_by_run(frames)
    if not by_run or runs.empty:
        return pd.Series(dtype=float)
    first_col, last_col = f"win_{window}_first", f"win_{window}_last"
    parts = []
    for row in runs.to_dict("records"):
        first, last = row.get(first_col), row.get(last_col)
        own = by_run.get(row["run_key"])
        if _missing(first) or _missing(last) or own is None:
            continue
        sel = own[(own["frame"] >= int(first)) & (own["frame"] <= int(last))]
        parts.append(sel["frame_ns"].astype(float))
    return pd.concat(parts, ignore_index=True) if parts else pd.Series(dtype=float)


def _noisy_keys(sessions: dict[str, dict[str, Any]] | None) -> set[tuple[str, str]]:
    """(session directory, cell key) of every cell the runner's CV gate marked noisy (M10). A cell key is the same in every
    session of one matrix (test, test-avx2 and the test-<engine> --target sessions all run smoke), so the mark belongs to the
    session it was measured in, never to the same key in another session (M10 fix round 0)."""
    out: set[tuple[str, str]] = set()
    for path, document in (sessions or {}).items():
        for cell in document.get("cells", []):
            if cell.get("noisy"):
                out.add((str(path), str(cell.get("key"))))
    return out


def _is_noisy(cell: pd.DataFrame, noisy: set[tuple[str, str]]) -> bool:
    """Whether a series has a run of a session cell the CV gate marked noisy (matched on the run's own session directory)."""
    if not noisy or "cell_key" not in cell.columns or "session_dir" not in cell.columns:
        return False
    return any((str(session), str(key)) in noisy for session, key in zip(cell["session_dir"], cell["cell_key"]) if not _missing(session) and not _missing(key))


def _median_of(values: pd.Series) -> float:
    numeric = pd.to_numeric(values, errors="coerce").dropna()
    return float(numeric.median()) if len(numeric) else math.nan


def cell_stats(classified: pd.DataFrame, frames: pd.DataFrame, window: str = "headline", sessions: dict[str, dict[str, Any]] | None = None) -> pd.DataFrame:
    """One row per (table, series): the statistics of the accepted runs of that cell."""
    rows = []
    by_run = frames_by_run(frames)
    noisy = _noisy_keys(sessions)
    for name, group, error in validate.tables(classified):
        series_column = "series" if "series" in group.columns else "label"
        for label, cell in group.groupby(series_column, sort=True):
            first = cell.iloc[0]
            base = {"table": name, "table_error": error, "table_key": first.get("table_key"), "scene": first["scene"], "scenario": first.get("scenario"),
                    "profile": first.get("profile"), "flag_profile": first.get("flag_profile"), "T": first.get("T"), "N": first.get("N"),
                    "thread_series": first.get("thread_series"), "label": label, "preset": first.get("preset"), "build_sdks": first.get("build_sdks")}
            if error:
                # A refused table: no statistic at all (02 13 validate; the report shows the table as refused).
                rows.append(dict(base, n_runs=int(len(cell)), median_ns=math.nan, mean_ns=math.nan, min_ns=math.nan, max_ns=math.nan, cv_pct=math.nan,
                                 p95_ns=math.nan, p99_ns=math.nan, pooled_frames=0, frame0_median_ns=math.nan, desc_sha256=None, run_keys=list(cell["run_key"]),
                                 values_ns=[], blocks_ns={}, ci_low_ns=math.nan, ci_high_ns=math.nan, cpu_eff=math.nan, foreign_max=math.nan,
                                 batch_ns=math.nan, tick_ns=math.nan, noisy=False))
                continue
            values = run_values(cell, window)
            valid = values.dropna()
            pooled = np.sort(pooled_frames(by_run, cell, window).to_numpy())
            # A block is one repeat of one session (the runner's seeded shuffle): runs of two sessions never share a block, so
            # two sessions merged into one cell pair only within each session (bootstrap.block_bootstrap_ratio).
            blocks: dict[str, float] = {}
            sessions = cell["session"] if "session" in cell.columns else pd.Series([None] * len(cell), index=cell.index)
            for session, block, value in zip(sessions, cell.get("block", pd.Series([None] * len(cell), index=cell.index)), values):
                if not _missing(block) and not _missing(value):
                    blocks[f"{session}#{int(block):04d}"] = float(value)   # one run per block and cell (the last attempt of each repeat)
            interval = bootstrap.percentile_ci(valid.tolist(), seed=bootstrap.seed_for(name, label, window))
            foreign = pd.to_numeric(cell["foreign_fraction"], errors="coerce").dropna() if "foreign_fraction" in cell.columns else pd.Series(dtype=float)
            rows.append(dict(
                base,
                n_runs=int(len(valid)),
                median_ns=float(valid.median()) if len(valid) else math.nan,
                mean_ns=float(valid.mean()) if len(valid) else math.nan,
                min_ns=float(valid.min()) if len(valid) else math.nan,
                max_ns=float(valid.max()) if len(valid) else math.nan,
                cv_pct=common.cv_percent(valid.tolist()),
                ci_low_ns=interval.low if interval else math.nan,
                ci_high_ns=interval.high if interval else math.nan,
                p95_ns=common.nearest_rank(pooled, 95.0) if len(pooled) else math.nan,
                p99_ns=common.nearest_rank(pooled, 99.0) if len(pooled) else math.nan,
                pooled_frames=int(len(pooled)),
                frame0_median_ns=_median_of(cell["frame0_ns"]) if "frame0_ns" in cell.columns else math.nan,
                cpu_eff=_median_of(cell["cpu_eff"]) if "cpu_eff" in cell.columns else math.nan,
                foreign_max=float(foreign.max()) if len(foreign) else math.nan,
                batch_ns=_median_of(cell[f"win_{window}_batch"]) if f"win_{window}_batch" in cell.columns else math.nan,
                tick_ns=_median_of(cell[f"win_{window}_tick_mean"]) if f"win_{window}_tick_mean" in cell.columns else math.nan,
                noisy=_is_noisy(cell, noisy),
                desc_sha256=first.get("desc_sha256"),
                run_keys=list(cell["run_key"]),
                values_ns=valid.tolist(),
                blocks_ns=blocks,
            ))
    return pd.DataFrame(rows)


def frame_series(frames: pd.DataFrame, run_keys: list[str], first: int, last: int) -> pd.DataFrame:
    """Per frame in [first, last]: median, q25, q75 of frame_ns across the runs."""
    sel = frames[frames["run_key"].isin(run_keys) & (frames["frame"] >= first) & (frames["frame"] <= last)]
    if sel.empty:
        return pd.DataFrame(columns=["frame", "median_ns", "q25_ns", "q75_ns", "runs"])
    grouped = sel.groupby("frame")["frame_ns"]
    return pd.DataFrame({
        "frame": grouped.median().index,
        "median_ns": grouped.median().to_numpy(),
        "q25_ns": grouped.quantile(0.25).to_numpy(),
        "q75_ns": grouped.quantile(0.75).to_numpy(),
        "runs": grouped.size().to_numpy(),
    })


def _fit_line(x: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    """Least squares y = a + b x; (a, b, R^2)."""
    b, a = np.polyfit(x, y, 1)
    predicted = a + b * x
    total = float(np.sum((y - y.mean()) ** 2))
    r2 = 1.0 - float(np.sum((y - predicted) ** 2)) / total if total > 0 else 1.0
    return float(a), float(b), r2


def _ok(cells: pd.DataFrame) -> pd.DataFrame:
    return cells[(cells["table_error"] == "") & cells["median_ns"].notna()]


def scaling_alpha(cells: pd.DataFrame) -> pd.DataFrame:
    """alpha per (series, scenario) over the cells that differ in N only (at least two N > 0)."""
    rows = []
    ok = _ok(cells)
    keys = [k for k in SERIES_KEYS if k in ok.columns]
    for key, group in ok.groupby(keys + ["scenario"], dropna=False, sort=True):
        points = group[pd.to_numeric(group["N"], errors="coerce") > 0].sort_values("N")
        if points["N"].nunique() < 2:
            continue
        x = np.log(points["N"].astype(float).to_numpy())
        y = np.log(points["median_ns"].astype(float).to_numpy())
        _, alpha, r2 = _fit_line(x, y)
        rows.append(dict(zip(keys + ["scenario"], key), N=list(points["N"].astype(int)), median_ns=list(points["median_ns"]), alpha=alpha, r2=r2))
    return pd.DataFrame(rows)


def speedup(cells: pd.DataFrame) -> pd.DataFrame:
    """S(T) = t(1) / t(T), E(T) = S / T per (label, profile, flag, scene), with the census's cpu_eff of the T cell (M10)."""
    rows = []
    ok = _ok(cells)
    for key, group in ok.groupby(["label", "profile", "flag_profile", "scene"], dropna=False, sort=True):
        base = group[group["T"] == 1]
        if base.empty or group["T"].nunique() < 2:
            continue
        t1 = float(base["median_ns"].iloc[0])
        for row in group.sort_values("T").to_dict("records"):
            s = t1 / float(row["median_ns"])
            cpu_eff = row.get("cpu_eff")
            rows.append(dict(zip(["label", "profile", "flag_profile", "scene"], key), T=int(row["T"]), median_ns=row["median_ns"], S=s, E=s / int(row["T"]),
                             cpu_eff=cpu_eff if not _missing(cpu_eff) else math.nan,
                             note="cpu_eff = this process's cycles / the cycles of its T cores (census window, M10)"))
    return pd.DataFrame(rows)


PERF_SCENARIOS = ("P0.EmptyStep", "P1.SmallScene", "M3.BoxPile")


def overhead_model(cells: pd.DataFrame) -> pd.DataFrame:
    """t = a + b N per series over the performance scenes P0 / P1 / M3 (02 13: the per-frame fixed cost and the per-body cost)."""
    rows = []
    ok = _ok(cells)
    ok = ok[ok["scenario"].isin(PERF_SCENARIOS)]
    keys = [k for k in SERIES_KEYS if k in ok.columns]
    for key, group in ok.groupby(keys, dropna=False, sort=True):
        if group["N"].nunique() < 2:
            continue
        x = group["N"].astype(float).to_numpy()
        y = group["median_ns"].astype(float).to_numpy()
        a, b, r2 = _fit_line(x, y)
        rows.append(dict(zip(keys, key), a_ns=a, b_ns_per_body=b, r2=r2, points=len(group), scenes=sorted(group["scene"].unique())))
    return pd.DataFrame(rows)


CONTROL_PRESETS = ("test-jolt", "test-physx", "test-bullet")   # the fallback for manifests without harness.sdks (before M10 fix round 1)


def sdk_count(record: dict[str, Any]) -> int | None:
    """How many SDK backends the series' exe was built with (manifest harness.sdks, M10 fix round 1), None when unknown."""
    text = record.get("build_sdks")
    if _missing(text):
        return None
    return len([s for s in str(text).split(",") if s])


def _control_preset(preset: Any) -> bool:
    """The preset-name fallback: test-<engine> and a user preset built on it (test-jolt-local, CMakeUserPresets.json.example)."""
    name = str(preset or "")
    return any(name == c or name.startswith(c + "-") for c in CONTROL_PRESETS)


def is_control(record: dict[str, Any]) -> bool:
    """A series of a single-SDK exe, the binary composition control (02 14.3): by the build's content (one SDK in
    harness.sdks), or - for a manifest written before M10 fix round 1 - by its preset name."""
    count = sdk_count(record)
    if count is not None:
        return count == 1
    return _control_preset(record.get("preset") or str(record.get("label")).partition("@")[2])


def comparison_label(label: Any, record: dict[str, Any] | None = None) -> str | None:
    """The series label the engine comparison uses: a label@preset series (validate.classify: one label from several presets)
    is the base label, except a single-SDK exe's (is_control), which is the binary composition control (composition_table)
    and never a comparison series. Without the cell record (a label alone) the preset name decides."""
    base, separator, preset = str(label).partition("@")
    if not separator:
        return base
    control = is_control(dict(record, label=label)) if record is not None else _control_preset(preset)
    return None if control else base


def _flag(value: Any) -> bool:
    return False if _missing(value) else bool(value)


def ratios(cells: pd.DataFrame) -> pd.DataFrame:
    """M10: every ordered pair (A, B) of series of one table: rho = median(A) / median(B) over their common blocks and its
    block-bootstrap 95 % CI (bootstrap.block_bootstrap_ratio, B = 10000). Series of a single-SDK exe are left out
    (comparison_label): they are the composition control, not an engine. M10 fix round 1: each row carries the CV gate's noisy
    mark of both cells (claim_table gives no claim on a noisy cell)."""
    rows = []
    ok = _ok(cells)
    for table, group in ok.groupby("table", sort=True):
        records = [dict(r, label=comparison_label(r["label"], r)) for r in group.to_dict("records") if comparison_label(r["label"], r) is not None]
        records.sort(key=lambda r: r["label"])
        for a in records:
            for b in records:
                if a["label"] >= b["label"]:
                    continue
                interval = bootstrap.block_bootstrap_ratio(a["blocks_ns"], b["blocks_ns"], seed=bootstrap.seed_for(table, a["label"], b["label"]))
                rows.append({"table": table, "scene": a["scene"], "profile": a["profile"], "flag_profile": a["flag_profile"], "T": a["T"],
                             "thread_series": a.get("thread_series"), "a": a["label"], "b": b["label"], "a_noisy": _flag(a.get("noisy")),
                             "b_noisy": _flag(b.get("noisy")), "rho": interval.estimate if interval else math.nan,
                             "ci_low": interval.low if interval else math.nan, "ci_high": interval.high if interval else math.nan,
                             "blocks": interval.n if interval else 0, "interval": interval})
    return pd.DataFrame(rows)


def _same(column: pd.Series, value: Any) -> pd.Series:
    """column == value, where two missing values are the same (a thread series that neither manifest has)."""
    if _missing(value):
        return column.isna()
    return column == value


def claim_table(ratio_rows: pd.DataFrame) -> pd.DataFrame:
    """M10: the claim rule (claims.evaluate) for every pair that has a Shipped ratio; its MatchedAVX2 counterpart is the ratio of
    the same pair on the same scene, profile, T and thread series in a MatchedAVX2 table (M10 fix round 1: the thread series is
    matched too). A pair whose cell is noisy in either flag profile (the runner's CV gate) gets no claim (claims.evaluate noisy=,
    docs/TimingContract.md 9: only non-noisy cells are reportable)."""
    rows = []
    if ratio_rows.empty:
        return pd.DataFrame(rows)
    shipped = ratio_rows[ratio_rows["flag_profile"] == "Shipped"]
    avx2 = ratio_rows[ratio_rows["flag_profile"] == "MatchedAVX2"]
    has_series = "thread_series" in ratio_rows.columns
    for row in shipped.to_dict("records"):
        selector = (avx2["scene"] == row["scene"]) & (avx2["profile"] == row["profile"]) & (avx2["T"] == row["T"]) & (avx2["a"] == row["a"]) & (avx2["b"] == row["b"])
        if has_series:
            selector &= _same(avx2["thread_series"], row.get("thread_series"))
        match = avx2[selector]
        counterpart = match.iloc[0].to_dict() if not match.empty else {}
        other = counterpart.get("interval")
        noisy = [f"{name} {flag}" for name, flag, flagged in ((row["a"], "Shipped", row.get("a_noisy")), (row["b"], "Shipped", row.get("b_noisy")),
                                                              (row["a"], "MatchedAVX2", counterpart.get("a_noisy")),
                                                              (row["b"], "MatchedAVX2", counterpart.get("b_noisy"))) if _flag(flagged)]
        claim = claims.evaluate(row["a"], row["b"], f"{row['scene']} ({row['profile']}, T{validate.int_text(row['T'])})", row["interval"], other, noisy=noisy)
        rows.append({"scene": row["scene"], "profile": row["profile"], "T": row["T"], "thread_series": row.get("thread_series"), "a": row["a"], "b": row["b"],
                     "rho_shipped": row["rho"], "rho_avx2": other.estimate if other is not None else math.nan, "noisy": ", ".join(noisy),
                     "verdict": claim.verdict, "reason": claim.reason, "text": claim.text})
    return pd.DataFrame(rows)


def composition_table(cells: pd.DataFrame) -> pd.DataFrame:
    """M10 (02 14.3 "바이너리 구성 대조군"): for every series that a table has from a single-SDK exe (is_control: harness.sdks holds one
    SDK - M10 fix round 1; before it the preset name test-<engine>) and from a multi-SDK exe (series label@preset,
    validate.classify), diff = median(single-SDK) / median(reference) - 1 with an independent percentile-bootstrap 95 % CI (the
    two come from different sessions: no shared blocks). The reference is that label's series from the exe with the most SDKs
    (the test exe, whatever its preset is called: test, test-local). M11 judges it against 1 %."""
    rows = []
    ok = _ok(cells)
    for table, group in ok.groupby("table", sort=True):
        by_label = {str(r["label"]): r for r in group.to_dict("records")}
        for label, record in by_label.items():
            base, separator, preset = label.partition("@")
            if not separator or not is_control(record):
                continue
            references = [r for other, r in by_label.items() if other != label and other.partition("@")[0] == base and "@" in other and not is_control(r)]
            if not references:
                continue
            reference = sorted(references, key=lambda r: (-(sdk_count(r) or 0), str(r["label"]).partition("@")[2] != "test", str(r["label"])))[0]
            interval = bootstrap.independent_ratio(record["values_ns"], reference["values_ns"], seed=bootstrap.seed_for(table, label))
            diff = (interval.estimate - 1.0) * 100.0 if interval else math.nan
            rows.append({"table": table, "scene": record["scene"], "profile": record["profile"], "T": record["T"], "label": base, "preset": preset,
                         "reference_preset": str(reference["label"]).partition("@")[2], "n": record["n_runs"], "n_test": reference["n_runs"],
                         "median_ns": record["median_ns"], "median_test_ns": reference["median_ns"],
                         "diff_pct": diff, "ci_low_pct": (interval.low - 1.0) * 100.0 if interval else math.nan,
                         "ci_high_pct": (interval.high - 1.0) * 100.0 if interval else math.nan,
                         "within_1pct": bool(interval) and abs(diff) <= 1.0})
    return pd.DataFrame(rows)


def format_ns(value: Any) -> str:
    if value is None or value is pd.NA or (isinstance(value, float) and math.isnan(value)):
        return "-"
    value = float(value)
    if abs(value) >= 1e6:
        return f"{value / 1e6:.3f} ms"
    if abs(value) >= 1e3:
        return f"{value / 1e3:.2f} µs"
    return f"{value:.0f} ns"


def main(args) -> int:
    from . import load

    missing = [p for p in args.inputs if not pathlib.Path(p).exists()]
    if missing:
        print(f"ptbench perf: no such path: {', '.join(missing)}")
        return 2
    loaded = load.load(args.inputs)
    if loaded.runs.empty:
        print("ptbench perf: no run found under " + ", ".join(args.inputs))
        return 1
    classified = validate.classify(loaded.runs, allow_unstamped=args.allow_unstamped)
    window = args.window or (loaded.session or {}).get("runner", {}).get("window") or "headline"
    cells = cell_stats(classified, loaded.frames, window, loaded.sessions)
    if cells.empty:
        print("no accepted run")
        return 1
    for row in cells.to_dict("records"):
        if row["table_error"]:
            print(f"{row['table']} | {row['label']}: {row['n_runs']} runs, TABLE REFUSED (no statistic): {row['table_error']}")
            continue
        cv = "-" if _missing(row["cv_pct"]) else f"{row['cv_pct']:.2f}%"
        ci = "-" if _missing(row["ci_low_ns"]) else f"[{format_ns(row['ci_low_ns'])}, {format_ns(row['ci_high_ns'])}]"
        print(f"{row['table']} | {row['label']}: n {row['n_runs']}, median {format_ns(row['median_ns'])} (95% CI {ci}), p95 {format_ns(row['p95_ns'])}, "
              f"p99 {format_ns(row['p99_ns'])}, CV {cv}{' NOISY' if row.get('noisy') else ''}")
    for row in scaling_alpha(cells).to_dict("records"):
        print(f"alpha {row['label']} {row['profile']} {row['scenario']} N={row['N']}: {row['alpha']:.3f}")
    for row in overhead_model(cells).to_dict("records"):
        print(f"overhead {row['label']} {row['profile']} T{validate.int_text(row['T'])}: a {format_ns(row['a_ns'])}, b {format_ns(row['b_ns_per_body'])}/body (R^2 {row['r2']:.4f})")
    for row in claim_table(ratios(cells)).to_dict("records"):
        print(f"claim: {row['text']}")
    return 0 if (cells["table_error"] == "").all() else 1
