"""perf - timing statistics of accepted runs (02_Architecture 13 "perf"; 03_WorkPlan M8 task 4; docs/TimingContract.md 3).

  run value      the scene window's T_frame mean of the run (manifest summary.windows.<window>.frame_ns.mean)
  cell           median over runs of the run values (the headline statistic), mean, min / max, CV = sample stdev / mean of
                 the run values (reported only: the CV < 2 % gate and the bootstrap CI are M10), p95 / p99 = nearest rank of
                 every window frame of every run of the cell pooled (frames table)
  frame series   per frame of the window: median and IQR (q25, q75) of frame_ns across the runs
  N-scaling      alpha of t = c N^alpha over the cells of one series that differ only in the body count N (least squares in
                 log-log; scene.num_dynamic_bodies)
  S(T), E(T)     t(T = 1) / t(T) and S / T, raw (cpu_eff unverified until M10; series of other thread shapes are not mixed)
  overhead       t = a + b N over the performance scenes of one series (P0 / P1 / M3: per-frame fixed cost a, per-body cost b)
A table that validate refuses (mixed flag profiles or desc_sha256) gets one row per series with its table_error and n_runs
but no statistic (every value NaN, values_ns empty): it is shown as refused, never computed.
"""
from __future__ import annotations

import math
from typing import Any

import numpy as np
import pandas as pd

from .. import common
from . import validate

SERIES_KEYS = ("label", "profile", "flag_profile", "T")


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
    return value is None or (isinstance(value, float) and math.isnan(value))


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


def cell_stats(classified: pd.DataFrame, frames: pd.DataFrame, window: str = "headline") -> pd.DataFrame:
    """One row per (table, series label): the statistics of the accepted runs of that cell."""
    rows = []
    by_run = frames_by_run(frames)
    for name, group, error in validate.tables(classified):
        for label, cell in group.groupby("label", sort=True):
            first = cell.iloc[0]
            if error:
                # A refused table: no statistic at all (02 13 validate; the report shows the table as refused).
                rows.append({"table": name, "table_error": error, "scene": first["scene"], "scenario": first.get("scenario"), "profile": first.get("profile"),
                             "flag_profile": first.get("flag_profile"), "T": first.get("T"), "N": first.get("N"), "label": label, "n_runs": int(len(cell)),
                             "median_ns": math.nan, "mean_ns": math.nan, "min_ns": math.nan, "max_ns": math.nan, "cv_pct": math.nan, "p95_ns": math.nan,
                             "p99_ns": math.nan, "pooled_frames": 0, "frame0_median_ns": math.nan, "desc_sha256": None, "run_keys": list(cell["run_key"]),
                             "values_ns": []})
                continue
            values = run_values(cell, window).dropna()
            pooled = np.sort(pooled_frames(by_run, cell, window).to_numpy())
            rows.append({
                "table": name,
                "table_error": error,
                "scene": first["scene"],
                "scenario": first.get("scenario"),
                "profile": first.get("profile"),
                "flag_profile": first.get("flag_profile"),
                "T": first.get("T"),
                "N": first.get("N"),
                "label": label,
                "n_runs": int(len(values)),
                "median_ns": float(values.median()) if len(values) else math.nan,
                "mean_ns": float(values.mean()) if len(values) else math.nan,
                "min_ns": float(values.min()) if len(values) else math.nan,
                "max_ns": float(values.max()) if len(values) else math.nan,
                "cv_pct": common.cv_percent(values.tolist()),
                "p95_ns": common.nearest_rank(pooled, 95.0) if len(pooled) else math.nan,
                "p99_ns": common.nearest_rank(pooled, 99.0) if len(pooled) else math.nan,
                "pooled_frames": int(len(pooled)),
                "frame0_median_ns": float(pd.to_numeric(cell["frame0_ns"], errors="coerce").median()),
                "desc_sha256": first.get("desc_sha256"),
                "run_keys": list(cell["run_key"]),
                "values_ns": values.tolist(),
            })
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


def scaling_alpha(cells: pd.DataFrame) -> pd.DataFrame:
    """alpha per (series, scenario) over the cells that differ in N only (at least two N > 0)."""
    rows = []
    ok = cells[(cells["table_error"] == "") & cells["median_ns"].notna()]
    for key, group in ok.groupby(list(SERIES_KEYS) + ["scenario"], dropna=False, sort=True):
        points = group[pd.to_numeric(group["N"], errors="coerce") > 0].sort_values("N")
        if points["N"].nunique() < 2:
            continue
        x = np.log(points["N"].astype(float).to_numpy())
        y = np.log(points["median_ns"].astype(float).to_numpy())
        _, alpha, r2 = _fit_line(x, y)
        rows.append(dict(zip(list(SERIES_KEYS) + ["scenario"], key), N=list(points["N"].astype(int)), median_ns=list(points["median_ns"]), alpha=alpha, r2=r2))
    return pd.DataFrame(rows)


def speedup(cells: pd.DataFrame) -> pd.DataFrame:
    """S(T) = t(1) / t(T), E(T) = S / T per (label, profile, flag, scene), raw (cpu_eff unverified until M10)."""
    rows = []
    ok = cells[(cells["table_error"] == "") & cells["median_ns"].notna()]
    for key, group in ok.groupby(["label", "profile", "flag_profile", "scene"], dropna=False, sort=True):
        base = group[group["T"] == 1]
        if base.empty or group["T"].nunique() < 2:
            continue
        t1 = float(base["median_ns"].iloc[0])
        for row in group.sort_values("T").to_dict("records"):
            s = t1 / float(row["median_ns"])
            rows.append(dict(zip(["label", "profile", "flag_profile", "scene"], key), T=int(row["T"]), median_ns=row["median_ns"], S=s, E=s / int(row["T"]),
                             note="cpu_eff 미검증 (M10)"))
    return pd.DataFrame(rows)


PERF_SCENARIOS = ("P0.EmptyStep", "P1.SmallScene", "M3.BoxPile")


def overhead_model(cells: pd.DataFrame) -> pd.DataFrame:
    """t = a + b N per series over the performance scenes P0 / P1 / M3 (02 13: the per-frame fixed cost and the per-body cost)."""
    rows = []
    ok = cells[(cells["table_error"] == "") & cells["median_ns"].notna() & cells["scenario"].isin(PERF_SCENARIOS)]
    for key, group in ok.groupby(list(SERIES_KEYS), dropna=False, sort=True):
        if group["N"].nunique() < 2:
            continue
        x = group["N"].astype(float).to_numpy()
        y = group["median_ns"].astype(float).to_numpy()
        a, b, r2 = _fit_line(x, y)
        rows.append(dict(zip(SERIES_KEYS, key), a_ns=a, b_ns_per_body=b, r2=r2, points=len(group), scenes=sorted(group["scene"].unique())))
    return pd.DataFrame(rows)


def format_ns(value: Any) -> str:
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "-"
    value = float(value)
    if abs(value) >= 1e6:
        return f"{value / 1e6:.3f} ms"
    if abs(value) >= 1e3:
        return f"{value / 1e3:.2f} µs"
    return f"{value:.0f} ns"


def main(args) -> int:
    from . import load

    loaded = load.load(args.inputs)
    classified = validate.classify(loaded.runs)
    window = args.window or (loaded.session or {}).get("runner", {}).get("window") or "headline"
    cells = cell_stats(classified, loaded.frames, window)
    if cells.empty:
        print("no accepted run")
        return 1
    for row in cells.to_dict("records"):
        if row["table_error"]:
            print(f"{row['table']} | {row['label']}: {row['n_runs']} runs, TABLE REFUSED (no statistic): {row['table_error']}")
            continue
        cv = "-" if _missing(row["cv_pct"]) else f"{row['cv_pct']:.2f}%"
        print(f"{row['table']} | {row['label']}: n {row['n_runs']}, median {format_ns(row['median_ns'])}, p95 {format_ns(row['p95_ns'])}, "
              f"p99 {format_ns(row['p99_ns'])}, CV {cv}")
    for row in scaling_alpha(cells).to_dict("records"):
        print(f"alpha {row['label']} {row['profile']} {row['scenario']} N={row['N']}: {row['alpha']:.3f}")
    for row in overhead_model(cells).to_dict("records"):
        print(f"overhead {row['label']} {row['profile']} T{row['T']}: a {format_ns(row['a_ns'])}, b {format_ns(row['b_ns_per_body'])}/body (R^2 {row['r2']:.4f})")
    return 0 if (cells["table_error"] == "").all() else 1
