"""ptbench - orchestration and analysis of PhysicsTestBench runs (03_WorkPlan M8; 02_Architecture 12.5, 13).

    python -I -m ptbench envcheck                     the read-only environment gate (power plan, idle, other processes)
    python -I -m ptbench run --matrix smoke           a matrix of fresh-process runs -> session.json, index.csv
    python -I -m ptbench report <session dir>         load -> validate -> perf -> report/<date>/index.md + PNG
    python -I -m ptbench determinism <dirs...>        the DET1 matrix and the first divergent frame
    python -I -m ptbench analytic [--check-cpp EXE]   the analytic references with dt_engine (equal to C++ within 1e-9)

Modules: common (paths, JSON, schemas, the seeded shuffle), envcheck, matrix, runner, analytic, analysis/{load, validate,
perf, report, determinism}. Every timing these tools report is UNOFFICIAL until M10 (stamps, CV gate, claim rules) and a
High performance power plan session (M11).
"""

__version__ = "0.8.0"
