"""validate - which runs may enter a timing table, and whether a table is sound (02_Architecture 13 "validate"; 03_WorkPlan
M8 task 4; DL21).

Run level (classify): a run is rejected, with every reason listed, when
  - exit.json is missing (the process died half-way or timed out; its raw exit code is never trusted, 02 12.2) or its exit
    code is not 0 (preflight / gate failure, engine error, contaminated host = exit 4, capacity, no backend),
  - exit.json or manifest.json does not match Config/Schemas (or manifest.json is missing),
  - the manifest is a viz-mode run (run.mode Viz: never a timing table) or any other non-Bench mode,
  - a preflight gate failed, the host was contaminated (also when only recorded), an instrumentation tier was on,
  - it is not headline-valid (timing_valid_for_headline false: its reasons are quoted).
Table level (check_table, raises TableError): one table must not mix flag profiles (Shipped with MatchedAVX2), and every run
in it must have the same desc_sha256 (DL21: the engine-independent scene input; dbg and the release presets differ). Tables
are scene x params x profile x flag profile x T until the M10 comparability key (02 13 "report"). Until then a table that
mixes builds (exe_sha256, git_sha, preset) - e.g. a report over two sessions - is built but carries a note (table_notes).
"""
from __future__ import annotations

import json
import math
from typing import Any, Iterable

import pandas as pd

TABLE_KEYS = ("scene", "profile", "flag_profile", "T")


class TableError(ValueError):
    """A table that must not be built: mixed flag profiles or desc_sha256 values (02 13 validate)."""


def _list(text: Any) -> list:
    if isinstance(text, list):
        return text
    if not isinstance(text, str) or not text:
        return []
    try:
        value = json.loads(text)
    except ValueError:
        return [text]
    return value if isinstance(value, list) else [value]


def _clean(row: dict[str, Any]) -> dict[str, Any]:
    """NaN (a missing value after pandas) -> None."""
    return {k: (None if isinstance(v, float) and math.isnan(v) else v) for k, v in row.items()}


def reject_reasons(row: dict[str, Any]) -> list[str]:
    row = _clean(row)
    reasons = []
    if not row.get("exit_json"):
        raw = row.get("process_exit_code")
        what = "timed out" if row.get("timed_out") or row.get("outcome") == "timeout" else "died before the end"
        reasons.append(f"exit.json missing: the process {what} (raw exit code {raw if raw is not None else '?'} is not trusted)")
    elif row.get("exit_code") != 0:
        reasons.append(f"exit.json {row.get('exit_code')} ({row.get('exit_status')})")
    if row.get("exit_json") and row.get("exit_json_valid") is False:
        reasons.append("exit.json does not match exit.schema.json")
    if not row.get("manifest"):
        # usage errors, exit 6 and a Fatal run (exit 3, status fatal) write exit.json only (ResultsSchema 1)
        if row.get("exit_json") and row.get("exit_code") in (0, 3, 4, 5) and row.get("exit_status") not in ("fatal", "usage_error"):
            reasons.append("manifest.json missing")
    elif row.get("manifest_valid") is False:
        reasons.append("manifest.json does not match manifest.schema.json: " + "; ".join(_list(row.get("schema_errors"))[:2]))
    mode = row.get("mode")
    if mode == "Viz":
        reasons.append("viz-mode manifest (run.mode Viz): never in a timing table")
    elif mode is not None and mode != "Bench":
        reasons.append(f"run.mode {mode} (timing tables take Bench runs only)")
    tiers = _list(row.get("instrumentation"))
    if tiers:
        reasons.append("instrumentation tier " + ",".join(map(str, tiers)))
    failed = _list(row.get("gates_failed"))
    if failed:
        reasons.append("preflight gate failed: " + ",".join(map(str, failed)))
    if row.get("contaminated") is True:
        reasons.append("contaminated host")
    if row.get("manifest") and row.get("timing_valid_for_headline") is not True:
        quoted = _list(row.get("timing_invalid_reasons"))
        reasons.append("not headline-valid" + (": " + "; ".join(map(str, quoted)) if quoted else ""))
    return reasons


def classify(runs: pd.DataFrame) -> pd.DataFrame:
    """runs + accepted (bool) + reject_reasons (list of text)."""
    out = runs.copy()
    if out.empty:
        out["accepted"] = pd.Series(dtype=bool)
        out["reject_reasons"] = pd.Series(dtype=object)
        return out
    reasons = [reject_reasons(row) for row in out.to_dict("records")]
    out["reject_reasons"] = reasons
    out["accepted"] = [not r for r in reasons]
    return out


def check_table(table: pd.DataFrame, name: str = "table") -> None:
    """Raises TableError when the runs of one table mix flag profiles or desc_sha256 values (accepted runs only)."""
    accepted = table[table["accepted"]] if "accepted" in table.columns else table
    if accepted.empty:
        return
    flags = sorted({str(v) for v in accepted["flag_profile"].dropna()})
    if len(flags) > 1:
        raise TableError(f"{name}: flag profiles mixed in one table ({', '.join(flags)}): Shipped and MatchedAVX2 are separate tables (02 13 validate, DL21)")
    descs = sorted({str(v) for v in accepted["desc_sha256"].dropna()})
    if len(descs) > 1:
        raise TableError(f"{name}: desc_sha256 differs inside one table ({', '.join(d[:12] for d in descs)}): the runs did not simulate the same "
                         "scene input (02 12.3 / 13, DL21)")
    if accepted["desc_sha256"].isna().any():
        raise TableError(f"{name}: a run without desc_sha256")


BUILD_IDENTITY = (("exe_sha256", "exe"), ("git_sha", "git"), ("preset", "preset"))


def table_notes(table: pd.DataFrame) -> list[str]:
    """Notes for a sound table whose accepted runs come from more than one build (exe_sha256, git_sha, preset): not an error
    before the M10 comparability key, but the reader must know that the series were not measured with one binary."""
    accepted = table[table["accepted"]] if "accepted" in table.columns else table
    notes = []
    for column, what in BUILD_IDENTITY:
        if column not in accepted.columns:
            continue
        values = sorted({str(v) for v in accepted[column].dropna()})
        if len(values) > 1:
            shown = ", ".join(v[:12] for v in values[:4]) + (", ..." if len(values) > 4 else "")
            notes.append(f"{len(values)} {what} values in one table ({shown}): the runs come from different builds (comparability key: M10)")
    return notes


def table_name(key: Iterable[Any], keys: Iterable[str] = TABLE_KEYS) -> str:
    return " | ".join(f"{k}={v}" for k, v in zip(keys, key))


def tables(classified: pd.DataFrame, keys: tuple[str, ...] = TABLE_KEYS) -> list[tuple[str, pd.DataFrame, str]]:
    """(name, accepted runs, error text) per table; a table that fails check_table carries its error instead of statistics."""
    accepted = classified[classified["accepted"]]
    out = []
    if accepted.empty:
        return out
    for key, group in accepted.groupby(list(keys), dropna=False, sort=True):
        key = key if isinstance(key, tuple) else (key,)
        name = table_name(key, keys)
        try:
            check_table(group, name)
            out.append((name, group, ""))
        except TableError as error:
            out.append((name, group, str(error)))
    return out


def main(args) -> int:
    from . import load

    loaded = load.load(args.inputs, with_frames=False)
    classified = classify(loaded.runs)
    accepted = int(classified["accepted"].sum()) if not classified.empty else 0
    print(f"{len(classified)} runs: {accepted} accepted, {len(classified) - accepted} rejected")
    for row in classified[~classified["accepted"]].to_dict("records"):
        print(f"  REJECT {row['cell_id']} ({row['run_name']}): " + "; ".join(row["reject_reasons"]))
    errors = 0
    for name, group, error in tables(classified):
        if error:
            errors += 1
            print(f"  TABLE ERROR {error}")
        else:
            print(f"  table ok: {name}: {len(group)} runs, {group['label'].nunique()} series")
            for note in table_notes(group):
                print(f"    NOTE {note}")
    return 1 if errors else 0
