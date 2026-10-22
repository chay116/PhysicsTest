"""validate - which runs may enter a timing table, and whether a table is sound (02_Architecture 13 "validate"; 03_WorkPlan M8
task 4, M10 task 5; DL21).

Run level (classify): a run is rejected, with every reason listed, when
  - exit.json is missing (the process died half-way or timed out; its raw exit code is never trusted, 02 12.2) or its exit
    code is not 0 (preflight / gate failure, engine error, contaminated host = exit 4, capacity, no backend),
  - exit.json or manifest.json does not match Config/Schemas (or manifest.json is missing), or the manifest is not a run
    manifest at all (M10, M8 review: the viewer's physicstest.viewer/1 with top-level mode viz is refused by its schema),
  - the manifest is a viz-mode run (run.mode Viz, any case: never a timing table) or any other non-Bench mode,
  - a gate failed (the preflight's G0 / G9 / G10a, M10's G10b), the host was contaminated (also when only recorded), an
    instrumentation tier was on,
  - it is not headline-valid (timing_valid_for_headline false: its reasons are quoted),
  - M10: its comparability key does not match the manifest's own fields (a copied or edited manifest), or it has no valid
    conformance stamp (manifest conformance_stamp; R9) - allow_unstamped (--allow-unstamped) accepts M7-M9 runs and test
    sessions and marks every table instead,
  - M10: it is a sentinel run (the session's drift check, never a cell value).
Table level: one table per comparability key (M10, 02 13: scene_sha256, desc_sha256, physics_sha256, mass rule, flag profile,
tick mode, dt, T, thread series, sync policy, mode, instrumentation, harness commit); engines and variants are its series. A
manifest without a key (M7-M9) falls back to scene x profile x flag profile x T. check_table still asserts what the key
implies (one flag profile, one desc_sha256) and raises TableError otherwise. A series label that comes from several presets in
one table (test and test-jolt: the binary composition control) becomes label@preset; other build mixes get a note.
"""
from __future__ import annotations

import json
import math
import pathlib
from typing import Any, Iterable

import pandas as pd

TABLE_KEYS = ("scene", "profile", "flag_profile", "T")   # the M8 table of a manifest without a comparability key
RUN_SCHEMA = "physicstest.run/2"


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
    """NaN / pd.NA (a missing value after pandas) -> None."""
    return {k: (None if (isinstance(v, float) and math.isnan(v)) or v is pd.NA else v) for k, v in row.items()}


def int_text(value: Any) -> str:
    """A code or count as text: 4.0 (a float column after pandas) -> "4"."""
    if isinstance(value, float) and not math.isnan(value) and value.is_integer():
        return str(int(value))
    return str(value)


def reject_reasons(row: dict[str, Any], allow_unstamped: bool = False) -> list[str]:
    row = _clean(row)
    reasons = []
    if row.get("kind") == "sentinel":
        reasons.append("sentinel run: the session's drift check (02 12.5), never a cell value")
    if not row.get("exit_json"):
        raw = row.get("process_exit_code")
        what = "timed out" if row.get("timed_out") or row.get("outcome") == "timeout" else "died before the end"
        reasons.append(f"exit.json missing: the process {what} (raw exit code {int_text(raw) if raw is not None else '?'} is not trusted)")
    elif row.get("exit_code") != 0:
        reasons.append(f"exit.json {int_text(row.get('exit_code'))} ({row.get('exit_status')})")
    if row.get("exit_json") and row.get("exit_json_valid") is False:
        reasons.append("exit.json does not match exit.schema.json")
    schema = row.get("manifest_schema")
    if row.get("manifest") and schema not in (None, RUN_SCHEMA):
        reasons.append(f"not a run manifest (schema {schema}, mode {row.get('mode')}): a viewer capture is never in a timing table")
    elif not row.get("manifest"):
        # usage errors, exit 6 and a Fatal run (exit 3, status fatal) write exit.json only (ResultsSchema 1)
        if row.get("exit_json") and row.get("exit_code") in (0, 3, 4, 5) and row.get("exit_status") not in ("fatal", "usage_error"):
            reasons.append("manifest.json missing")
    elif row.get("manifest_valid") is False:
        reasons.append("manifest.json does not match manifest.schema.json: " + "; ".join(_list(row.get("schema_errors"))[:2]))
    mode = row.get("mode")
    if schema in (None, RUN_SCHEMA) and isinstance(mode, str) and mode.lower() == "viz":
        reasons.append(f"viz-mode manifest (run.mode {mode}): never in a timing table")
    elif schema in (None, RUN_SCHEMA) and mode is not None and str(mode).lower() != "bench":
        reasons.append(f"run.mode {mode} (timing tables take Bench runs only)")
    tiers = _list(row.get("instrumentation"))
    if tiers:
        reasons.append("instrumentation tier " + ",".join(map(str, tiers)))
    failed = _list(row.get("gates_failed"))
    if failed:
        reasons.append("gate failed: " + ",".join(map(str, failed)))
    if row.get("contaminated") is True:
        reasons.append("contaminated host")
    if row.get("manifest") and schema in (None, RUN_SCHEMA) and row.get("timing_valid_for_headline") is not True:
        quoted = _list(row.get("timing_invalid_reasons"))
        # The contamination and the gate failures have their own reason above: quote the others once.
        quoted = [q for q in quoted if not (str(q).startswith("contaminated host") and row.get("contaminated") is True)
                  and not (str(q).startswith("exit code ") and row.get("exit_json") and row.get("exit_code") != 0)
                  and not (str(q).startswith("G10b failed") and "G10b" in failed)]
        reasons.append("not headline-valid" + (": " + "; ".join(map(str, quoted)) if quoted else ""))
    stored, recomputed = row.get("comparability_key"), row.get("comparability_key_recomputed")
    if row.get("manifest") and stored and recomputed and stored != recomputed:
        reasons.append(f"comparability_key {str(stored)[:12]} does not match the manifest's fields ({str(recomputed)[:12]}): copied or edited")
    if row.get("manifest") and schema in (None, RUN_SCHEMA) and row.get("stamp_valid") is not True and not allow_unstamped:
        reasons.append("no valid conformance stamp (R9): " + str(row.get("stamp_reason") or "?"))
    return reasons


def classify(runs: pd.DataFrame, allow_unstamped: bool = False) -> pd.DataFrame:
    """runs + accepted (bool) + reject_reasons (list of text) + series (the table's series label)."""
    out = runs.copy()
    if out.empty:
        out["accepted"] = pd.Series(dtype=bool)
        out["reject_reasons"] = pd.Series(dtype=object)
        out["series"] = pd.Series(dtype=object)
        return out
    reasons = [reject_reasons(row, allow_unstamped) for row in out.to_dict("records")]
    out["reject_reasons"] = reasons
    out["accepted"] = [not r for r in reasons]
    out["unstamped_accepted"] = [bool(allow_unstamped and r.get("manifest") and r.get("stamp_valid") is not True) for r in out.to_dict("records")]
    out["table_key"] = [table_key(r) for r in out.to_dict("records")]
    out["series"] = out["label"] if "label" in out.columns else ""
    accepted = out[out["accepted"]]
    if not accepted.empty and "preset" in out.columns:
        # The binary composition control (M10, 02 14.3): the same label from two presets in one table (test, test-jolt) is two
        # series, label@preset.
        for (key, label), group in accepted.groupby(["table_key", "label"], dropna=False):
            presets = sorted({str(v) for v in group["preset"].dropna()})
            if len(presets) > 1:
                out.loc[group.index, "series"] = [f"{label}@{p}" for p in group["preset"]]
    return out


def table_key(row: dict[str, Any]) -> str:
    """The comparability key, or for an M7-M9 manifest the M8 tuple (scene x profile x flag profile x T)."""
    row = _clean(row)
    key = row.get("comparability_key")
    if key:
        return str(key)
    return "m8:" + "|".join(int_text(row.get(k)) for k in TABLE_KEYS)


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
    if "comparability_key" in accepted.columns:
        keys = sorted({str(v) for v in accepted["comparability_key"].dropna()})
        if len(keys) > 1:
            raise TableError(f"{name}: comparability keys mixed in one table ({', '.join(k[:12] for k in keys)})")


BUILD_IDENTITY = (("exe_sha256", "exe"), ("git_sha", "git"), ("preset", "preset"))


def table_notes(table: pd.DataFrame) -> list[str]:
    """Notes for a sound table: runs from more than one build (exe_sha256, git_sha, preset) - the comparability key holds the
    harness commit, so different exes of one commit are the binary composition control (test vs test-<engine>, series
    label@preset) or a rebuild - and runs accepted without a valid conformance stamp (--allow-unstamped)."""
    accepted = table[table["accepted"]] if "accepted" in table.columns else table
    notes = []
    for column, what in BUILD_IDENTITY:
        if column not in accepted.columns:
            continue
        values = sorted({str(v) for v in accepted[column].dropna()})
        if len(values) > 1:
            shown = ", ".join(v[:12] for v in values[:4]) + (", ..." if len(values) > 4 else "")
            notes.append(f"{len(values)} {what} values in one table ({shown}): the runs come from different builds of one harness commit")
    if "unstamped_accepted" in accepted.columns and accepted["unstamped_accepted"].any():
        notes.append(f"{int(accepted['unstamped_accepted'].sum())} run(s) accepted without a valid conformance stamp (--allow-unstamped): not reportable (R9)")
    return notes


def table_name(row: dict[str, Any]) -> str:
    """"scene=.. | profile=.. | flag_profile=.. | T=.. | series=.. | key=.." of a table's first row (report.short_title reads it)."""
    row = _clean(row)
    parts = [f"scene={row.get('scene')}", f"profile={row.get('profile')}", f"flag_profile={row.get('flag_profile')}", f"T={int_text(row.get('T'))}"]
    series = row.get("thread_series")
    if series and series != "p_cores_one_lp":
        parts.append(f"threads={series}")
    key = str(row.get("table_key") or "")
    parts.append(f"key={key[:8] if not key.startswith('m8:') else 'M8'}")
    return " | ".join(parts)


def tables(classified: pd.DataFrame) -> list[tuple[str, pd.DataFrame, str]]:
    """(name, accepted runs, error text) per table (one comparability key); a table that fails check_table carries its error
    instead of statistics."""
    if classified.empty or "accepted" not in classified.columns:
        return []
    accepted = classified[classified["accepted"]]
    out = []
    if accepted.empty:
        return out
    for _, group in sorted(accepted.groupby("table_key", dropna=False, sort=False), key=lambda item: table_name(item[1].iloc[0].to_dict())):
        name = table_name(group.iloc[0].to_dict())
        try:
            check_table(group, name)
            out.append((name, group, ""))
        except TableError as error:
            out.append((name, group, str(error)))
    return out


def main(args) -> int:
    from . import load

    missing = [p for p in args.inputs if not pathlib.Path(p).exists()]
    if missing:
        print(f"ptbench validate: no such path: {', '.join(missing)}")
        return 2
    loaded = load.load(args.inputs, with_frames=False)
    if loaded.runs.empty:
        print("ptbench validate: no run found under " + ", ".join(args.inputs))
        return 1
    classified = classify(loaded.runs, allow_unstamped=args.allow_unstamped)
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
            print(f"  table ok: {name}: {len(group)} runs, {group['series'].nunique()} series")
            for note in table_notes(group):
                print(f"    NOTE {note}")
    return 1 if errors else 0
