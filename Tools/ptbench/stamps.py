"""stamps - conformance stamps: the runner's check before it starts a process, and `ptbench stamp` (03_WorkPlan M10 task 4;
02_Architecture 12.4, R9).

A stamp (Saved/Conformance/<stamp_key_sha256>.json, schema physicstest.stamp/1, Config/Schemas/stamp.schema.json) is written
by PhysicsTestBench -Conformance after the gate suite ran on the binary's own backend code. Its key is
    {bench_exe_sha256, backend_module_sha256 (a DLL backend's file, "" for a static one), variant_sha256, physics_sha256,
     tolerances_sha256, gate_suite_version, config_overrides (the sorted -ini: / -ExecCmds= switches; an M10 addition)}
hashed as canonical JSON (keys sorted, no whitespace, UTF-8: ConformanceStamp.cpp builds the same bytes).

The runner never trusts a stamp's own claim. Before any process of a session starts, for every cell it re-derives what the
key depends on from the files themselves:
  - the bench exe: SHA-256 of the file (a rebuilt exe is another key);
  - a DLL backend: SHA-256 of the DLL next to the exe (PhysicsTestBench.backends.json says which backends are DLLs);
  - the configuration: the raw-byte SHA-256 of every input the stamped conformance read (BasePhysicsTest.ini, the physics
    profile, the backend variant ini, tolerances.json - recorded in the stamp with their paths). variant_sha256 and
    physics_sha256 are the harness's canonical hashes of that content (the C++ profile resolver); unchanged inputs and an
    unchanged exe give unchanged values, so the inputs are the runner's recomputation of them. tolerances_sha256 is the raw
    hash of tolerances.json and is compared directly;
  - the cell's -ini: / -ExecCmds= switches (config_overrides) and -ConfigDir;
  - the stamp itself: the key's hash equals stamp_key_sha256 and the file name, the suite passed (result pass, valid) and no
    verdict rests on an unsigned post-run registration (pending_sign_offs 0).
A cell whose stamp is missing, stale or invalid is refused before any process starts. The bench also records the stamp it
finds for its own run (manifest conformance_stamp, computed exactly by the C++ code) and validate rejects a run without a
valid one, so a run that slipped past the runner (a direct bench call) never enters a table either.
"""
from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import subprocess
import sys
from typing import Any, Iterable

from . import common

STAMP_SCHEMA = "stamp.schema.json"
CATALOG_FILE = "PhysicsTestBench.backends.json"
INPUT_ROLES = ("base_ini", "profile_ini", "variant_ini", "tolerances")


def default_stamp_dir() -> pathlib.Path:
    return common.repo_root() / "Saved" / "Conformance"


def default_config_dir() -> pathlib.Path:
    return common.repo_root() / "Config"


def canonical_key(key: dict[str, Any]) -> str:
    """The exact bytes the key hash covers (FConformanceStampKey::ToCanonicalJson)."""
    return json.dumps(key, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def key_sha256(key: dict[str, Any]) -> str:
    return common.sha256_text(canonical_key(key))


def _switch_body(arg: str) -> str:
    return arg[1:] if arg[:1] in ("-", "/") else ""


def config_overrides(args: Iterable[str]) -> list[str]:
    """The -ini: and -ExecCmds= switches of a cell (case-insensitive keys, double quotes removed, sorted), exactly as the bench
    extracts them from its command line (ExtractConfigOverrides)."""
    out = []
    for arg in args:
        body = _switch_body(arg)
        lower = body.lower()
        if lower.startswith("ini:") or lower.startswith("execcmds="):
            out.append("-" + body.replace('"', ""))
    return sorted(out)


def config_dir_of(args: Iterable[str]) -> pathlib.Path:
    """The -ConfigDir= of a cell's switches, else the repository's Config."""
    for arg in args:
        body = _switch_body(arg)
        if body.lower().startswith("configdir="):
            return pathlib.Path(body.split("=", 1)[1].strip('"'))
    return default_config_dir()


def _norm(path: os.PathLike | str) -> str:
    return os.path.normcase(os.path.normpath(os.path.abspath(str(path))))


def load_catalog(exe: pathlib.Path) -> dict[str, dict[str, Any]]:
    """name -> {"kind": "static" | "dll", "file": DLL file name} from the build's catalog next to the exe ({} when absent)."""
    document, _ = common.try_read_json(exe.parent / CATALOG_FILE)
    if not isinstance(document, dict):
        return {}
    return {entry["name"]: entry for entry in document.get("backends", []) if isinstance(entry, dict) and "name" in entry}


@dataclasses.dataclass
class StampTarget:
    """What one stamp covers: a backend, its requested variant, a physics profile, the configuration switches and directory."""
    backend: str
    variant: str             # as the cell requests it ("" = the bench's default resolution)
    profile: str
    overrides: tuple[str, ...]
    config_dir: str          # normalised

    @property
    def label(self) -> str:
        variant = f".{self.variant}" if self.variant else ""
        extra = f" {' '.join(self.overrides)}" if self.overrides else ""
        return f"{self.backend}{variant}/{self.profile}{extra}"


def target_of(cell, shared_args: Iterable[str]) -> StampTarget:
    args = list(shared_args) + list(cell.target_args) + list(cell.scene_args)
    return StampTarget(backend=cell.backend, variant=cell.variant, profile=cell.profile, overrides=tuple(config_overrides(args)),
                       config_dir=_norm(config_dir_of(args)))


class StampCache:
    """The stamps of one directory and the file hashes of one check, read once."""

    def __init__(self, stamp_dir: pathlib.Path) -> None:
        self.stamp_dir = pathlib.Path(stamp_dir)
        self._file_hashes: dict[str, str | None] = {}
        self.stamps: list[tuple[pathlib.Path, dict[str, Any], list[str]]] = []
        if self.stamp_dir.is_dir():
            for path in sorted(self.stamp_dir.glob("*.json")):
                document, error = common.try_read_json(path)
                if document is None:
                    continue
                errors = common.schema_errors(document, STAMP_SCHEMA) if isinstance(document, dict) else ["not an object"]
                self.stamps.append((path, document if isinstance(document, dict) else {}, errors))

    def file_sha256(self, path: os.PathLike | str) -> str | None:
        key = _norm(path)
        if key not in self._file_hashes:
            try:
                self._file_hashes[key] = common.sha256_file(path)
            except OSError:
                self._file_hashes[key] = None
        return self._file_hashes[key]


def _matches_target(document: dict[str, Any], target: StampTarget) -> bool:
    stamp_target = document.get("target") or {}
    key = document.get("key") or {}
    if stamp_target.get("backend") != target.backend or stamp_target.get("profile") != target.profile:
        return False
    if (stamp_target.get("variant_requested") or "") != target.variant:
        return False
    if tuple(key.get("config_overrides") or ()) != target.overrides:
        return False
    return _norm(document.get("config_dir") or "") == target.config_dir


STATE_VALID, STATE_STALE, STATE_INVALID = "valid", "stale", "invalid"


def verify_stamp(path: pathlib.Path, document: dict[str, Any], schema_errors: list[str], exe: pathlib.Path, exe_sha: str,
                 catalog: dict[str, dict[str, Any]], backend: str, cache: StampCache) -> tuple[str, str]:
    """(state, reason) of a stamp for this exe and these files now: ("valid", "") or the first reason it is not, with its state -
    "invalid" when the stamp itself cannot vouch for anything (it does not match its schema, its key does not hash to its name,
    it records no input it must, its suite failed or rests on an unsigned registration), "stale" when it was valid for other
    binaries or inputs than the ones there now (M10 fix round 1: the state is decided here, not read back from the text)."""
    if schema_errors:
        return STATE_INVALID, f"{path.name} does not match Config/Schemas/{STAMP_SCHEMA}: {schema_errors[0]}"
    key = document["key"]
    recomputed = key_sha256(key)
    if recomputed != document.get("stamp_key_sha256") or path.stem != recomputed or document.get("key_canonical") != canonical_key(key):
        return STATE_INVALID, f"{path.name}: the key does not hash to its name / stamp_key_sha256 (edited or renamed stamp)"
    if key.get("bench_exe_sha256") != exe_sha:
        return STATE_STALE, f"the bench exe changed since the stamp (stamp {str(key.get('bench_exe_sha256'))[:12]}, exe now {exe_sha[:12]}): rebuilt"
    entry = catalog.get(backend)
    if entry is None:
        return STATE_STALE, f"backend '{backend}' is not in {CATALOG_FILE} next to the exe (another build?)"
    if entry.get("kind") == "dll":
        module = exe.parent / str(entry.get("file"))
        module_sha = cache.file_sha256(module)
        if module_sha is None:
            return STATE_STALE, f"the DLL backend file {module.name} is missing"
        if key.get("backend_module_sha256") != module_sha:
            return STATE_STALE, (f"the DLL backend {module.name} changed since the stamp (stamp {str(key.get('backend_module_sha256'))[:12]}, "
                                 f"file now {module_sha[:12]}): rebuilt")
    elif key.get("backend_module_sha256"):
        return STATE_STALE, f"the stamp names a backend module file for '{backend}', which this build links statically"
    roles = {}
    for item in document.get("inputs", []):
        roles[item.get("role")] = item
        now = cache.file_sha256(item.get("path", ""))
        if now is None:
            return STATE_STALE, f"the stamped input {item.get('path')} ({item.get('role')}) is missing"
        if now != item.get("sha256"):
            return STATE_STALE, f"{item.get('path')} ({item.get('role')}) changed since the stamp (stamp {str(item.get('sha256'))[:12]}, file now {now[:12]})"
    for role in ("base_ini", "profile_ini", "tolerances"):
        if role not in roles:
            return STATE_INVALID, f"the stamp records no {role} input"
    tolerances_now = cache.file_sha256(roles["tolerances"]["path"])
    if key.get("tolerances_sha256") != tolerances_now:
        return STATE_STALE, f"tolerances.json changed since the stamp (key {str(key.get('tolerances_sha256'))[:12]}, file now {str(tolerances_now)[:12]})"
    if (document.get("target") or {}).get("variant") and "variant_ini" not in roles:
        return STATE_INVALID, "the stamp records no variant ini for a variant"
    if not document.get("valid") or document.get("result") != "pass":
        return STATE_INVALID, f"the stamp is not valid: {document.get('invalid_reason') or document.get('result')}"
    if int(document.get("pending_sign_offs", 1)) != 0:
        return STATE_INVALID, f"{document.get('pending_sign_offs')} verdict(s) rest on an unsigned post-run registration (pending_sign_offs)"
    return STATE_VALID, ""


def _stamp_exe_path(document: dict[str, Any]) -> str:
    return _norm((document.get("build") or {}).get("exe_path") or "")


def check_target(target: StampTarget, exe: pathlib.Path, exe_sha: str, catalog: dict[str, dict[str, Any]], cache: StampCache) -> dict[str, Any]:
    """{"state": valid | missing | stale | invalid, "reason", "stamp", "key_sha256", "utc"} for one stamp target.
    One stamp directory holds the stamps of every exe (test, test-avx2, test-<engine>, earlier builds), so the reason comes from
    this exe's own stamps (M10 fix round 0): first those keyed by this exe's SHA-256 (stale inputs, invalid), then those
    written by an exe at this path (rebuilt since); a target with stamps for other exes only is 'missing' for this exe."""
    if catalog and target.backend not in catalog:
        return {"state": "missing", "reason": f"backend '{target.backend}' is not in this build ({CATALOG_FILE} next to the exe: {', '.join(sorted(catalog))})",
                "stamp": None, "key_sha256": None, "utc": None}
    candidates = [(path, document, errors) for path, document, errors in cache.stamps if _matches_target(document, target)]
    if not candidates:
        return {"state": "missing", "reason": f"no stamp for {target.label} in {cache.stamp_dir} (python -I -m ptbench stamp --matrix <matrix>)",
                "stamp": None, "key_sha256": None, "utc": None}
    candidates.sort(key=lambda item: str(item[1].get("utc") or ""), reverse=True)
    exe_path = _norm(exe)
    same_sha = [c for c in candidates if (c[1].get("key") or {}).get("bench_exe_sha256") == exe_sha]
    same_path = [c for c in candidates if c not in same_sha and _stamp_exe_path(c[1]) == exe_path]
    for path, document, errors in same_sha + same_path:
        state, _ = verify_stamp(path, document, errors, exe, exe_sha, catalog, target.backend, cache)
        if state == STATE_VALID:
            return {"state": STATE_VALID, "reason": "", "stamp": str(path), "key_sha256": document.get("stamp_key_sha256"), "utc": document.get("utc")}
    own = same_sha or same_path
    if not own:
        others = sorted({f"{(c[1].get('build') or {}).get('preset') or '?'} {str((c[1].get('key') or {}).get('bench_exe_sha256'))[:12]}" for c in candidates})
        return {"state": "missing", "reason": f"no stamp for this exe ({exe_sha[:12]}); {len(candidates)} stamp(s) of {target.label} exist for other exes "
                f"({', '.join(others[:4])}{', ...' if len(others) > 4 else ''}): python -I -m ptbench stamp --matrix <matrix> --preset <preset>",
                "stamp": None, "key_sha256": None, "utc": None}
    newest = own[0]
    state, reason = verify_stamp(newest[0], newest[1], newest[2], exe, exe_sha, catalog, target.backend, cache)
    return {"state": state, "reason": reason, "stamp": str(newest[0]), "key_sha256": newest[1].get("stamp_key_sha256"), "utc": newest[1].get("utc")}


def stale_binary_reason(document: dict[str, Any], cache: StampCache) -> str:
    """'' while the stamp's own exe (build.exe_path) and DLL backend file (backend_module.path) still exist with the SHA-256 of
    its key, else why not: such a stamp can never become valid again (a relinked binary gets a new SHA without /Brepro)."""
    key = document.get("key") or {}
    exe_path = (document.get("build") or {}).get("exe_path")
    if not exe_path or cache.file_sha256(exe_path) is None:
        return f"its exe {exe_path} is gone"
    if cache.file_sha256(exe_path) != key.get("bench_exe_sha256"):
        return f"its exe {exe_path} was rebuilt"
    module_path = (document.get("backend_module") or {}).get("path")
    if key.get("backend_module_sha256"):
        if not module_path or cache.file_sha256(module_path) is None:
            return f"its DLL backend {module_path} is gone"
        if cache.file_sha256(module_path) != key.get("backend_module_sha256"):
            return f"its DLL backend {module_path} was rebuilt"
    return ""


def prune(stamp_dir: pathlib.Path, dry_run: bool = False, log=print) -> int:
    """ptbench stamp --prune: removes the stamps whose exe or DLL backend no longer exists or was rebuilt (stale_binary_reason),
    with their conformance run directory when it lies under <stamp_dir>/runs. Stamps that are stale for a changed configuration
    file are kept (reverting the file makes them valid again). Returns the number of stamps removed (or listed, dry_run)."""
    import shutil

    stamp_dir = pathlib.Path(stamp_dir)
    cache = StampCache(stamp_dir)
    runs_root = _norm(stamp_dir / "runs")
    removed = 0
    for path, document, errors in cache.stamps:
        reason = f"does not match Config/Schemas/{STAMP_SCHEMA}" if errors else stale_binary_reason(document, cache)
        if not reason:
            continue
        removed += 1
        run_dir = pathlib.Path(str(document.get("conformance_dir") or ""))
        inside = bool(document.get("conformance_dir")) and _norm(run_dir).startswith(runs_root + os.sep)
        target = document.get("target") or {}
        log(f"{'would remove' if dry_run else 'removed'} {path.name} ({target.get('backend')}/{target.get('profile')}, "
            f"{(document.get('build') or {}).get('preset')}): {reason}" + (f" + runs/{run_dir.name}" if inside else ""))
        if dry_run:
            continue
        path.unlink(missing_ok=True)
        if inside:
            shutil.rmtree(run_dir, ignore_errors=True)
            pathlib.Path(str(run_dir) + ".log").unlink(missing_ok=True)
    log(f"{removed} of {len(cache.stamps)} stamp(s) {'to remove' if dry_run else 'removed'} ({stamp_dir})")
    return removed


def check_cells(cells, shared_args: Iterable[str], exe: pathlib.Path, stamp_dir: pathlib.Path) -> dict[str, Any]:
    """Every cell's stamp state (cell key -> result) before a session starts; also the targets checked."""
    shared_args = list(shared_args)
    exe = pathlib.Path(exe)
    exe_sha = common.sha256_file(exe) if exe.is_file() else ""
    catalog = load_catalog(exe)
    cache = StampCache(stamp_dir)
    by_target: dict[StampTarget, dict[str, Any]] = {}
    cells_out: dict[str, Any] = {}
    for cell in cells:
        target = target_of(cell, shared_args)
        frozen = StampTarget(target.backend, target.variant, target.profile, target.overrides, target.config_dir)
        key = (frozen.backend, frozen.variant, frozen.profile, frozen.overrides, frozen.config_dir)
        if key not in by_target:
            by_target[key] = dict(check_target(frozen, exe, exe_sha, catalog, cache), target=frozen.label)
        cells_out[cell.key] = by_target[key]
    return {"stamp_dir": str(stamp_dir), "exe": str(exe), "exe_sha256": exe_sha, "catalog": bool(catalog), "cells": cells_out,
            "targets": list(by_target.values())}


# ---- ptbench stamp ---------------------------------------------------------------------------------------------------------

def stamp_targets(matrix, extra_args: list[str] | None = None) -> list[tuple[StampTarget, list[str]]]:
    """The distinct stamp targets of a matrix with the switches their conformance run needs (backend, variant, profile and the
    configuration switches), in matrix order."""
    shared = list(matrix.get("bench_args", [])) + list(extra_args or [])
    out: list[tuple[StampTarget, list[str]]] = []
    seen = set()
    for cell in matrix.cells():
        target = target_of(cell, shared)
        key = (target.backend, target.variant, target.profile, target.overrides, target.config_dir)
        if key in seen:
            continue
        seen.add(key)
        args = [f"-Backend={cell.backend}"] + ([f"-BackendProfile={cell.variant}"] if cell.variant else []) + [f"-Profile={cell.profile}"]
        all_args = shared + list(cell.target_args) + list(cell.scene_args)
        args += [a for a in all_args if _switch_body(a).lower().startswith(("ini:", "execcmds=", "configdir="))]
        out.append((target, args))
    return out


def run_stamp(matrix, exe: pathlib.Path, stamp_dir: pathlib.Path, extra_args: list[str] | None = None, check_only: bool = False, force: bool = False,
              timeout_s: float = 900.0, log=print) -> int:
    """Runs PhysicsTestBench -Conformance for every stamp target of the matrix that has no valid stamp (all with force) and
    prints each target's state. Exit code 0 when every target ends valid, 1 otherwise, 2 without an exe."""
    exe = pathlib.Path(exe)
    if not exe.is_file():
        log(f"ptbench stamp: no bench executable at {exe}")
        return 2
    exe_sha = common.sha256_file(exe)
    catalog = load_catalog(exe)
    targets = stamp_targets(matrix, extra_args)
    runs_dir = stamp_dir / "runs"
    bad = 0
    for index, (target, args) in enumerate(targets):
        state = check_target(target, exe, exe_sha, catalog, StampCache(stamp_dir))
        if check_only or (state["state"] == "valid" and not force):
            log(f"[{index + 1}/{len(targets)}] {target.label}: {state['state']}{(' - ' + state['reason']) if state['reason'] else ''}")
            bad += state["state"] != "valid"
            continue
        out_dir = runs_dir / f"{common.utc_stamp()}_{common.short_hash(target.label + str(exe))}"
        command = [str(exe), "-Conformance"] + args + [f"-StampDir={stamp_dir}", f"-Out={out_dir}"]
        out_dir.mkdir(parents=True, exist_ok=True)
        with open(out_dir.parent / f"{out_dir.name}.log", "wb") as stream:
            completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=timeout_s, check=False)
        state = check_target(target, exe, exe_sha, catalog, StampCache(stamp_dir))
        exit_doc, _ = common.try_read_json(out_dir / "exit.json")
        log(f"[{index + 1}/{len(targets)}] {target.label}: -Conformance exit {completed.returncode} (exit.json {None if exit_doc is None else exit_doc.get('exit_code')}) "
            f"-> {state['state']}{(' - ' + state['reason']) if state['reason'] else ''}")
        bad += state["state"] != "valid"
    log(f"{len(targets) - bad}/{len(targets)} stamp target(s) valid ({stamp_dir})")
    return 0 if bad == 0 else 1


def main(args) -> int:
    from .matrix import MatrixError, load_matrix, select_targets

    if getattr(args, "prune", False):
        if args.matrix or args.target or args.force:
            print("ptbench stamp --prune: takes no --matrix, --target or --force (it looks at every stamp of --stamp-dir)", file=sys.stderr)
            return 2
        prune(pathlib.Path(args.stamp_dir).resolve() if args.stamp_dir else default_stamp_dir(), dry_run=args.check)
        return 0
    if not args.matrix:
        print("ptbench stamp: --matrix is required (or --prune)", file=sys.stderr)
        return 2
    try:
        matrix = select_targets(load_matrix(args.matrix), args.target)
    except (MatrixError, OSError, ValueError) as error:
        print(f"ptbench stamp: {error}", file=sys.stderr)
        return 2
    preset = args.preset or matrix.get("preset") or "test"
    exe = pathlib.Path(args.exe).resolve() if args.exe else common.default_bench_exe(preset)
    stamp_dir = pathlib.Path(args.stamp_dir).resolve() if args.stamp_dir else default_stamp_dir()
    return run_stamp(matrix, exe, stamp_dir, extra_args=list(args.bench_arg or []), check_only=args.check, force=args.force)
