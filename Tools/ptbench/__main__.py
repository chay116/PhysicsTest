"""python -I -m ptbench <command> ... (see ptbench/__init__.py and docs/ResultsSchema.md 11)."""
from __future__ import annotations

import argparse
import sys

from . import envcheck as envcheck_defaults


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -I -m ptbench", description="PhysicsTest bench orchestration and analysis (03_WorkPlan M8)")
    sub = parser.add_subparsers(dest="command", required=True)

    def gate_flags(p: argparse.ArgumentParser) -> None:
        p.add_argument("--allow-balanced", action="store_true", help="run although the power plan is not High performance / Ultimate (marked in every table)")
        p.add_argument("--allow-busy", action="store_true", help="run although the system is not idle (< 3 %% over 2 s)")
        p.add_argument("--allow-concurrent", action="store_true", help="run although another PhysicsTest process runs (CTest)")
        p.add_argument("--idle-wait", type=float, default=envcheck_defaults.IDLE_WAIT_S, metavar="SECONDS",
                       help="keep trying 2 s idle windows for up to this long before the idle check fails (default %(default)g s; "
                            "the time waited is recorded)")
        p.add_argument("--skip-idle", action="store_true",
                       help="do not measure the idle window (tests): recorded as a bypass of the idle check, like --allow-busy")

    p = sub.add_parser("envcheck", help="the read-only environment gate")
    gate_flags(p)
    p.add_argument("--json", action="store_true", help="print the full result as JSON")

    p = sub.add_parser("run", help="run a matrix (fresh process per run, seeded block shuffle, cooldown, timeout, exit.json verdicts)")
    p.add_argument("--matrix", required=True, help="a name in Config/Matrices (smoke, determinism) or a path")
    p.add_argument("--preset", help="out/<preset>/bin/PhysicsTestBench.exe (default: the matrix's preset, else test)")
    p.add_argument("--exe", help="an explicit PhysicsTestBench.exe")
    p.add_argument("--out", help="the directory that receives the session directory (default Saved/Results)")
    p.add_argument("--session", help="the session directory name (default <matrix>-<UTC time>)")
    p.add_argument("--repeats", type=int, help="override the matrix's R (fewer than 5 marks a perf session)")
    p.add_argument("--cooldown", type=float, help="override the cooldown before each run (s)")
    p.add_argument("--timeout", type=float, help="override the per-run timeout (s)")
    p.add_argument("--bench-arg", action="append", help="an extra bench switch for every run (repeatable), e.g. --bench-arg=-HostCheck=record")
    p.add_argument("--dry-run", action="store_true", help="print the planned order only")
    p.add_argument("--report", action="store_true", help="build the report when the session ends")
    gate_flags(p)

    p = sub.add_parser("load", help="validate the outputs against Config/Schemas and write tidy parquet (runs, frames, effective, gates)")
    p.add_argument("inputs", nargs="+", help="session directories and / or run directories")
    p.add_argument("--out", help="parquet directory (default <session>/analysis)")

    p = sub.add_parser("validate", help="classify the runs (accepted / rejected with reasons) and check every table")
    p.add_argument("inputs", nargs="+")
    p.add_argument("--window", default=None)

    p = sub.add_parser("perf", help="per-cell statistics (median, p95, p99, CV), N-scaling alpha, S(T), the overhead model")
    p.add_argument("inputs", nargs="+")
    p.add_argument("--window", default=None)

    p = sub.add_parser("report", help="report/<date>/index.md + PNG for a session (or run directories, -Gate=G8 directories, several sessions)")
    p.add_argument("inputs", nargs="+", metavar="session", help="a session directory; also run / -Gate=G8 directories or a directory that holds several")
    p.add_argument("--out", help="the report directory (default <first input>/report/<date>)")
    p.add_argument("--window", default=None)

    p = sub.add_parser("determinism", help="the DET1 matrix and the first divergent frame")
    p.add_argument("inputs", nargs="+", help="-Gate=G8 directories (gate_g8.json), sessions of a determinism matrix, or run directories")
    p.add_argument("--out", help="write the matrix as Markdown to this file")
    p.add_argument("--json", help="write the result as JSON to this file")

    p = sub.add_parser("behavior", help="M9 behaviour analysis of -StatesEvery runs: references, cross-engine differences with tags, criteria")
    p.add_argument("inputs", nargs="+", help="a behaviour session (Config/Matrices/behavior.json) and / or run directories")
    p.add_argument("--out", help="write the behaviour section (index.md, PNG, differences.csv, behavior.json) to this directory")
    p.add_argument("--json", help="write the result (physicstest.behavior/1) to this file")

    p = sub.add_parser("analytic", help="analytic references with dt_engine; --check-cpp compares them with the C++ ones")
    p.add_argument("--check-cpp", metavar="EXE", help="PhysicsTestBench.exe (-AnalyticReferences)")
    p.add_argument("--cpp-json", metavar="FILE", help="the saved output of -AnalyticReferences")
    return parser


def main(argv: list[str] | None = None) -> int:
    from .common import use_utf8_stdio

    use_utf8_stdio()   # 'µs' and Korean plan names survive a redirected stdout (CP949) under python -I
    args = build_parser().parse_args(argv)
    if args.command == "envcheck":
        import json

        from . import envcheck

        result = envcheck.run_envcheck(allow_balanced=args.allow_balanced, allow_busy=args.allow_busy, allow_concurrent=args.allow_concurrent,
                                       skip_idle=args.skip_idle, idle_wait_s=args.idle_wait)
        print(json.dumps(result, indent=2, ensure_ascii=False) if args.json else envcheck.describe(result))
        return 3 if result["refused"] else 0
    if args.command == "run":
        from . import runner

        return runner.main(args)
    if args.command == "analytic":
        from . import analytic

        return analytic.main(args)
    if args.command == "determinism":
        from .analysis import determinism

        return determinism.main(args)
    if args.command == "behavior":
        from .analysis import behavior

        return behavior.main(args)
    if args.command == "load":
        from .analysis import load

        return load.main(args)
    if args.command == "validate":
        from .analysis import validate

        return validate.main(args)
    if args.command == "perf":
        from .analysis import perf

        return perf.main(args)
    if args.command == "report":
        from .analysis import report

        return report.main(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
