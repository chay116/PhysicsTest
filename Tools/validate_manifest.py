"""validate_manifest.py - validate bench outputs against Config/Schemas (03_WorkPlan M7: "manifest.json이
Config/Schemas/manifest.schema.json을 통과").

    .venv\\Scripts\\python.exe -I Tools\\validate_manifest.py <run dir or manifest.json>... [--require-fields]
    .venv\\Scripts\\python.exe -I Tools\\validate_manifest.py --recursive [--exclude=<text>]... <dir>... [--require-fields]

For every argument: <dir>/manifest.json is validated against manifest.schema.json and <dir>/exit.json (when present) against
exit.schema.json (jsonschema, Draft 2020-12). --recursive (M7 fix round 0) instead walks each directory for every
manifest.json and exit.json below it (a missing directory is skipped with a note; a path containing an --exclude text is
skipped); at least one file must be found. --require-fields additionally checks that the fields 03 M7 names are filled
(mode, instrumentation, timing_valid_for_headline, dt_engine_s, threads, platform with the power plan). Prints one PASS /
FAIL line per file (only FAIL lines and a summary with --recursive); exit code 0 when every file passes, 1 otherwise, 2 for
a usage error.
"""
import json
import pathlib
import sys

import jsonschema

REPO = pathlib.Path(__file__).resolve().parent.parent
SCHEMAS = REPO / "Config" / "Schemas"


def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def check_required_fields(manifest):
    """The 03 M7 criterion-1 fields, filled (not only present)."""
    problems = []
    run = manifest.get("run", {})
    threads = manifest.get("threads", {})
    platform = manifest.get("platform", {})
    if run.get("mode") not in ("Bench", "Behavior", "Viz"):
        problems.append("run.mode")
    if not isinstance(run.get("instrumentation"), list):
        problems.append("run.instrumentation")
    if not isinstance(manifest.get("timing_valid_for_headline"), bool):
        problems.append("timing_valid_for_headline")
    if not (isinstance(run.get("dt_engine_s"), float) and run["dt_engine_s"] > 0.0):
        problems.append("run.dt_engine_s")
    if not (isinstance(threads.get("T"), int) and threads.get("affinity_mask", "").startswith("0x") and threads.get("logical_cpus")):
        problems.append("threads (T, affinity_mask, logical_cpus)")
    if not (platform.get("power_scheme") and platform.get("power_scheme_guid") and isinstance(platform.get("power_scheme_high_performance"), bool)):
        problems.append("platform.power_scheme")
    if not (platform.get("cpu") and platform.get("qpc_hz")):
        problems.append("platform (cpu, qpc_hz)")
    clock = manifest.get("clock", {})
    if not (clock.get("qpc_hz") and isinstance(clock.get("read_overhead_ns"), float) and clock["read_overhead_ns"] > 0.0):
        problems.append("clock (qpc_hz, read_overhead_ns)")
    return problems


def collect_targets(args, recursive, excludes, manifest_validator, exit_validator):
    targets = []
    notes = []
    for arg in args:
        path = pathlib.Path(arg)
        if recursive:
            if not path.is_dir():
                notes.append(f"skipped (no directory): {path}")
                continue
            for name, validator in (("manifest.json", manifest_validator), ("exit.json", exit_validator)):
                for found in sorted(path.rglob(name)):
                    if not any(text in found.as_posix() for text in excludes):
                        targets.append((found, validator))
            continue
        run_dir = path if path.is_dir() else path.parent
        manifest_path = path if path.is_file() else run_dir / "manifest.json"
        targets.append((manifest_path, manifest_validator))
        if (run_dir / "exit.json").exists():
            targets.append((run_dir / "exit.json", exit_validator))
    return targets, notes


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    require_fields = "--require-fields" in argv
    recursive = "--recursive" in argv
    excludes = [a[len("--exclude="):] for a in argv if a.startswith("--exclude=")]
    if not args:
        print(__doc__)
        return 2
    manifest_schema = load(SCHEMAS / "manifest.schema.json")
    exit_schema = load(SCHEMAS / "exit.schema.json")
    jsonschema.Draft202012Validator.check_schema(manifest_schema)
    jsonschema.Draft202012Validator.check_schema(exit_schema)
    manifest_validator = jsonschema.Draft202012Validator(manifest_schema)
    exit_validator = jsonschema.Draft202012Validator(exit_schema)
    targets, notes = collect_targets(args, recursive, excludes, manifest_validator, exit_validator)
    for note in notes:
        print(note)
    if recursive and not targets:
        print("FAIL: --recursive found no manifest.json / exit.json")
        return 1
    failed = 0
    passed = {"manifest.json": 0, "exit.json": 0}
    for target, validator in targets:
        try:
            document = load(target)
        except (OSError, ValueError) as error:
            print(f"FAIL {target}: {error}")
            failed += 1
            continue
        errors = sorted(validator.iter_errors(document), key=lambda e: list(e.absolute_path))
        if target.name == "manifest.json" and require_fields:
            for problem in check_required_fields(document):
                errors.append(jsonschema.ValidationError(f"03 M7 field not filled: {problem}"))
        if errors:
            failed += 1
            print(f"FAIL {target}: {len(errors)} error(s)")
            for error in errors[:20]:
                location = "/".join(str(p) for p in error.absolute_path) or "<root>"
                print(f"  {location}: {error.message[:300]}")
        else:
            passed[target.name] = passed.get(target.name, 0) + 1
            if not recursive:
                print(f"PASS {target} ({document.get('schema')})")
    if recursive:
        print(f"{'PASS' if failed == 0 else 'FAIL'}: {passed['manifest.json']} manifest.json and {passed['exit.json']} exit.json valid, {failed} invalid")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
