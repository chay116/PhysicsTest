"""determinism - the DET1 matrix and the first divergent frame (02_Architecture 13 "determinism", 14.3 DET1; 03_WorkPlan M8).

DET1 = the same exe and the same T in fresh processes must give the same final h_native (and the same harness-visible state
hash h_state). A group is one such set of runs:
  - a -Gate=G8 directory (gate_g8.json with run0..runN-1, M7),
  - the runs of one cell of a ptbench session (a determinism matrix: R fresh processes per cell),
  - or run directories that agree on backend, variant, profile, scene (scene_sha256), T and exe_sha256.
Per group the matrix row lists every run's final hashes, the verdict (equal / DIFFERENT), and the first divergent frame:
the earliest frame whose hashes.csv row (h_native, h_state) differs from the reference run. The reference is the run whose
hash sequence (every recorded frame, then the final hashes) the majority of the group shares, so when run 0 is the odd one
out of three it is named as the divergent run (with two runs, or without a majority, the reference is run 0). With
-HashEvery=1 that frame is exact ("per-frame"); a headline run records the final frame only, so a difference can be placed
only at the first recorded frame that differs ("sparse": the true first divergence is at or before it). DET2-DET5 are M15.
Verdicts: "equal" (every run of the group went to its end - exit.json 0, or 4: a contaminated host spoils the timing, not
the hashes - at least two, and all agree), "DIFFERENT" (two complete runs disagree: this wins over a failed run),
"incomplete" (a run failed or crashed - no exit.json, another exit code - or fewer than two runs are complete, while the
complete runs agree: DET1 is N fresh processes that ALL agree, so this is not a pass; M7's -Gate=G8 fails too when a child
fails).

    python -I -m ptbench determinism <gate dirs | session dirs | run dirs> [--out det1.md] [--json det1.json]
exits 0 when every group is equal, 1 when a group differs (the first divergent frame is printed), 2 without a group, 3 when
no group differs but a group is incomplete.
"""
from __future__ import annotations

import csv
import pathlib
from typing import Any

from .. import common
from . import load as load_module


def read_hashes(run_dir: pathlib.Path) -> dict[int, tuple[str, str, str]]:
    """frame -> (h_native, h_quantized, h_state) from hashes.csv (empty when absent)."""
    path = pathlib.Path(run_dir) / "hashes.csv"
    out: dict[int, tuple[str, str, str]] = {}
    if not path.is_file():
        return out
    with open(path, encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            try:
                out[int(row["frame"])] = (row.get("h_native", ""), row.get("h_quantized", ""), row.get("h_state", ""))
            except (KeyError, ValueError):
                continue
    return out


def first_divergence(reference: dict[int, tuple[str, str, str]], other: dict[int, tuple[str, str, str]]) -> dict[str, Any]:
    """The earliest common frame whose h_native or h_state differs (h_quantized is not a DET1 hash: same engine noise filter)."""
    common_frames = sorted(set(reference) & set(other))
    result = {"frame": None, "field": None, "reference": None, "other": None, "frames_compared": len(common_frames),
              "missing_frames": len(set(reference) ^ set(other))}
    for frame in common_frames:
        a, b = reference[frame], other[frame]
        if a[0] != b[0]:
            result.update(frame=frame, field="h_native", reference=a[0], other=b[0])
            return result
        if a[2] != b[2]:
            result.update(frame=frame, field="h_state", reference=a[2], other=b[2])
            return result
    return result


def _group_label(row: dict[str, Any]) -> str:
    return f"{row.get('label')}|{row.get('profile')}|{row.get('scene')}|T{row.get('T')}"


def build_groups(loaded: load_module.Loaded) -> list[dict[str, Any]]:
    """DET1 groups: one per G8 directory, one per (session, cell), else per (identity, exe)."""
    runs = loaded.runs
    groups: dict[tuple, dict[str, Any]] = {}
    for row in runs.to_dict("records"):
        if row.get("source_kind") == "gate_g8":
            key = ("gate", row["source"])
            name = f"{_group_label(row)} ({pathlib.Path(row['source']).name})"
        elif row.get("source_kind") == "session":
            key = ("session", row["source"], row["cell_id"])
            name = row["cell_id"]
        else:
            key = ("runs", row.get("backend"), row.get("variant"), row.get("module_sha256"), row.get("profile"), row.get("scene_sha256"), row.get("T"), row.get("exe_sha256"))
            name = _group_label(row)
        group = groups.setdefault(key, {"name": name, "source": row.get("source"), "runs": []})
        group["runs"].append(row)
    return list(groups.values())


def choose_reference(runs: list[dict[str, Any]], hashes: list[dict[int, tuple[str, str, str]]]) -> tuple[int, bool]:
    """(index of the reference run, whether it is a strict majority). The signature of a run is its recorded hash rows
    (h_native, h_state per frame) plus its final hashes; the first run of the signature that more than half of the runs
    share is the reference, else run 0."""
    signatures = []
    for run, rows in zip(runs, hashes):
        recorded = tuple(sorted((frame, row[0], row[2]) for frame, row in rows.items()))
        signatures.append((recorded, str(run.get("h_native")), str(run.get("h_state"))))
    for index, signature in enumerate(signatures):
        if signatures.count(signature) * 2 > len(signatures):
            return index, True
    return 0, False


# Exit codes of a run that went to its end with its final hashes: 0, and 4 (contaminated host - only its timing is
# contaminated; a perf session's retried exit-4 attempt is a complete fresh process for DET1).
COMPLETE_EXIT_CODES = (0, 4)


def analyze_group(group: dict[str, Any]) -> dict[str, Any]:
    runs = [r for r in group["runs"] if r.get("exit_json") and r.get("exit_code") in COMPLETE_EXIT_CODES]
    failed = [r for r in group["runs"] if not (r.get("exit_json") and r.get("exit_code") in COMPLETE_EXIT_CODES)]
    result = {
        "group": group["name"],
        "source": group["source"],
        "runs": len(group["runs"]),
        "runs_ok": len(runs),
        "runs_failed": [f"{r.get('run_name')}: {r.get('outcome')}" for r in failed],
        "h_native": [r.get("h_native") for r in runs],
        "h_state": [r.get("h_state") for r in runs],
        "exe_sha256": sorted({str(r.get("exe_sha256")) for r in runs}),
        "T": sorted({str(r.get("T")) for r in runs}),
    }
    if len(runs) < 2:
        result.update(verdict="incomplete", first_divergence=None, resolution=None,
                      incomplete_reason=f"{len(runs)} ok run(s) of {len(group['runs'])}: DET1 needs every run ok and at least two")
        return result
    equal_final = len(set(result["h_native"])) == 1 and len(set(result["h_state"])) == 1
    hashes = [read_hashes(pathlib.Path(run["run_dir"])) for run in runs]
    ref_index, majority = choose_reference(runs, hashes)
    reference = hashes[ref_index]
    reference_run = runs[ref_index]
    frames_run = int(reference_run.get("frames_run") or 0)
    resolution = "per-frame" if reference and len(reference) >= frames_run > 0 else "sparse"
    first = None
    divergent = []
    for index, other in enumerate(runs):
        if index == ref_index:
            continue
        divergence = first_divergence(reference, hashes[index])
        final_differs = (str(other.get("h_native")), str(other.get("h_state"))) != (str(reference_run.get("h_native")), str(reference_run.get("h_state")))
        if divergence["frame"] is not None or final_differs:
            divergent.append(other.get("run_name"))
        if divergence["frame"] is not None and (first is None or divergence["frame"] < first["frame"]):
            first = dict(divergence, run=other.get("run_name"), reference_run=reference_run.get("run_name"), reference_is_majority=majority)
    mixed_build = len(result["exe_sha256"]) > 1 or len(result["T"]) > 1
    agree = equal_final and first is None
    if agree and failed:
        verdict = "incomplete"
        result["incomplete_reason"] = f"{len(failed)} of {len(group['runs'])} run(s) did not end ok ({'; '.join(result['runs_failed'])}); the ok runs agree"
    else:
        verdict = "equal" if agree else "DIFFERENT"
    result.update(
        verdict=verdict,
        first_divergence=first,
        reference_run=reference_run.get("run_name"),
        reference_is_majority=majority,
        divergent_runs=divergent,
        resolution=resolution,
        recorded_frames=len(reference),
        frames_run=frames_run,
        note="not a DET1 group: different exe or T" if mixed_build else "",
    )
    return result


def analyze(inputs: list[str]) -> list[dict[str, Any]]:
    loaded = load_module.load(inputs, with_frames=False)
    if loaded.runs.empty:
        return []
    return sorted((analyze_group(group) for group in build_groups(loaded)), key=lambda r: str(r["group"]))


def to_markdown(results: list[dict[str, Any]]) -> str:
    lines = ["| group | runs ok | final h_native | final h_state | DET1 | first divergent frame | resolution |", "|---|---|---|---|---|---|---|"]
    for r in results:
        natives = sorted(set(map(str, r["h_native"])))
        states = sorted(set(map(str, r["h_state"])))
        first = r.get("first_divergence")
        reference = f"{first['reference_run']}{' (majority)' if first.get('reference_is_majority') else ''}" if first else ""
        where = "-" if not first else f"{first['frame']} ({first['field']}: {reference} {first['reference']} vs {first['run']} {first['other']})"
        if r["verdict"] == "DIFFERENT" and not first:
            where = "final hashes differ, no common hash row differs"
        verdict = r["verdict"]
        if verdict == "incomplete":
            verdict = "**incomplete**"
            where = str(r.get("incomplete_reason") or "-").replace("|", r"\|")
        group = str(r["group"]).replace("|", r"\|")
        lines.append(f"| {group} | {r['runs_ok']}/{r['runs']} | {'<br>'.join(natives)} | {'<br>'.join(states)} | {verdict} | {where} | {r.get('resolution') or '-'} |")
    return "\n".join(lines) + "\n"


def write_report(session_dir: pathlib.Path, out_dir: pathlib.Path | None = None) -> tuple[pathlib.Path, int]:
    """M10 (M8 review): the report of a determinism session (`ptbench run --matrix determinism --report`): the DET1 matrix as
    <session>/report/<date>/determinism.md and determinism.json. Returns (the Markdown path, the CLI code: 0 every group equal,
    1 a group differs, 2 no group, 3 incomplete)."""
    import datetime as _dt

    session_dir = pathlib.Path(session_dir)
    out_dir = pathlib.Path(out_dir) if out_dir else session_dir / "report" / _dt.date.today().isoformat()
    out_dir.mkdir(parents=True, exist_ok=True)
    results = analyze([str(session_dir)])
    path = out_dir / "determinism.md"
    body = to_markdown(results) if results else "no run found\n"
    common.write_text_atomic(path, f"# DET1 — {session_dir.name} (UNOFFICIAL)\n\n" + body)
    common.write_json_atomic(out_dir / "determinism.json", results)
    if not results:
        return path, 2
    if any(r["verdict"] == "DIFFERENT" for r in results):
        return path, 1
    return path, 3 if any(r["verdict"] == "incomplete" for r in results) else 0


def main(args) -> int:
    missing = [p for p in args.inputs if not pathlib.Path(p).exists()]
    if missing:
        print(f"ptbench determinism: no such path: {', '.join(missing)}")
        return 2
    results = analyze(args.inputs)
    if not results:
        print("ptbench determinism: no run found")
        return 2
    text = to_markdown(results)
    print(text)
    if args.out:
        common.write_text_atomic(args.out, "# DET1 (UNOFFICIAL)\n\n" + text)
    if args.json:
        common.write_json_atomic(args.json, results)
    different = [r for r in results if r["verdict"] == "DIFFERENT"]
    for r in different:
        first = r.get("first_divergence")
        reference = f"{r.get('reference_run')}{' (majority)' if r.get('reference_is_majority') else ' (run 0, no majority)'}"
        runs = ", ".join(map(str, r.get("divergent_runs") or [])) or "-"
        if first:
            print(f"DET1 DIFFERENT {r['group']}: first divergent frame {first['frame']} ({first['field']}, {first['run']} vs reference {reference}, {r['resolution']}); "
                  f"runs differing from the reference: {runs}")
        else:
            print(f"DET1 DIFFERENT {r['group']}: final hashes differ (reference {reference}; differing: {runs})")
    incomplete = [r for r in results if r["verdict"] == "incomplete"]
    for r in incomplete:
        print(f"DET1 INCOMPLETE {r['group']}: {r.get('incomplete_reason')}")
    equal = sum(1 for r in results if r["verdict"] == "equal")
    print(f"{len(results)} group(s): {equal} equal, {len(different)} different, {len(incomplete)} incomplete (a run failed or fewer than 2 ok runs)")
    if different:
        return 1
    return 3 if incomplete else 0
