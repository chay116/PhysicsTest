"""Unit tests of ptbench without a bench executable (CTest PhysicsTest.Py.Unit):
    python -I -m unittest -v ptbench.tests.test_unit
"""
from __future__ import annotations

import argparse
import io
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import pandas as pd

from ptbench import __main__, analytic, common, envcheck, runner
from ptbench import matrix as matrix_module
from ptbench.analysis import determinism, load, perf, report, validate
from ptbench.matrix import expand_cells, load_matrix, plan_blocks

BALANCED_TEXT = "전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)"
HIGH_TEXT = "Power Scheme GUID: 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c  (High performance)"
CUSTOM_TEXT = "Power Scheme GUID: 11111111-2222-3333-4444-555555555555  (Mine)"


class SeededShuffleTests(unittest.TestCase):
    def test_splitmix64_reference_value(self):
        # splitmix64 seeded with 0: the first output is 0xE220A8397B1DCDAF (the reference implementation's value).
        _, value = common.splitmix64(0)
        self.assertEqual(value, 0xE220A8397B1DCDAF)

    def test_shuffle_is_a_reproducible_permutation(self):
        items = list(range(12))
        a = common.seeded_shuffle(items, 1234)
        self.assertEqual(sorted(a), items)
        self.assertEqual(a, common.seeded_shuffle(items, 1234))
        self.assertNotEqual(a, common.seeded_shuffle(items, 1235))
        self.assertNotEqual(a, items)

    def test_block_seeds_differ_per_repeat(self):
        seeds = {common.block_seed(20261022, r) for r in range(5)}
        self.assertEqual(len(seeds), 5)
        self.assertTrue(all(0 <= s < 2 ** 64 for s in seeds))


class MatrixTests(unittest.TestCase):
    def test_smoke_matrix_is_the_m8_smoke_matrix(self):
        matrix = load_matrix("smoke")
        cells = matrix.cells()
        # 03 M8: jolt.Default, bullet.Default, physx.UE4Compat, physx.SdkDefault x {P0.EmptyStep, M3.BoxPile 1k, 5k} x T=1 x R=5.
        self.assertEqual({c.label for c in cells}, {"jolt.Default", "bullet.Default", "physx.UE4Compat", "physx.SdkDefault"})
        self.assertEqual({c.scene_label for c in cells}, {"P0.EmptyStep", "M3.BoxPile[N=1000]", "M3.BoxPile[N=5000]"})
        self.assertEqual({c.threads for c in cells}, {1})
        self.assertEqual(len(cells), 12)
        self.assertEqual(matrix.repeats, 5)
        plan = matrix.plan()
        self.assertEqual(len(plan), 60)
        for repeat in range(5):
            block = [p.cell.cell_id for p in plan if p.repeat == repeat]
            self.assertEqual(sorted(block), sorted(c.cell_id for c in cells))   # every cell once per block
        orders = [[p.cell.key for p in plan if p.repeat == r] for r in range(5)]
        self.assertGreater(len({tuple(o) for o in orders}), 1)                  # blocks are shuffled differently
        self.assertEqual([p.cell.key for p in matrix.plan()], [p.cell.key for p in plan])   # reproducible

    def test_determinism_matrix(self):
        matrix = load_matrix("determinism")
        self.assertEqual(matrix.purpose, "determinism")
        self.assertEqual(len(matrix.cells()), 16)
        self.assertIn("-HashEvery=1", matrix.get("bench_args"))

    def test_exclude_rule(self):
        document = {"targets": [{"label": "jolt", "backend": "Jolt"}, {"label": "bullet", "backend": "Bullet"}], "profiles": ["Normalized"],
                    "scenes": [{"scenario": "M3.BoxPile", "params": {"N": 1000}}], "threads": [1, 2, 4],
                    "exclude": [{"backend": "Bullet", "T": [2, 4], "reason": "Bullet runs T = 1 only"}]}
        cells = expand_cells(document)
        self.assertEqual(sorted((c.label, c.threads) for c in cells), [("bullet", 1), ("jolt", 1), ("jolt", 2), ("jolt", 4)])
        self.assertEqual(cells[0].bench_args()[:4], ["-Backend=Jolt", "-Profile=Normalized", "-Scenario=M3.BoxPile", "-Param:N=1000"])
        self.assertEqual(len(plan_blocks(cells, 7, 3)), 12)

    def test_target_bench_args(self):
        document = {"schema": "physicstest.matrix/1", "name": "t", "description": "d", "purpose": "perf", "repeats": 1, "block_seed": 1,
                    "targets": [{"label": "jolt.Default", "backend": "Jolt", "variant": "Default"},
                                {"label": "jolt.Iter8", "backend": "Jolt", "variant": "Default", "bench_args": ["-ExecCmds=p.Iterations 8"]}],
                    "profiles": ["Normalized"], "scenes": [{"scenario": "M3.BoxPile", "params": {"N": 1000}}], "threads": [1]}
        self.assertEqual(common.schema_errors(document, "matrix.schema.json"), [])
        plain, tuned = expand_cells(document)
        self.assertNotIn("-ExecCmds=p.Iterations 8", plain.bench_args())
        self.assertEqual(tuned.bench_args()[-1], "-ExecCmds=p.Iterations 8")
        self.assertNotIn("-ExecCmds=p.Iterations 8", tuned.bench_args(include_target_args=False))
        self.assertEqual(tuned.as_json()["bench_args"], ["-ExecCmds=p.Iterations 8"])

    def test_switch_keys(self):
        self.assertEqual(matrix_module.switch_keys("-PreRoll=0.5"), ["preroll"])
        self.assertEqual(matrix_module.switch_keys("-HashEvery=1"), ["hashevery"])
        self.assertEqual(matrix_module.switch_keys("-InjectFatal"), ["injectfatal"])
        self.assertEqual(matrix_module.switch_keys("-ExecCmds=p.A 1, p.B 2"), ["execcmds"])
        self.assertEqual(matrix_module.switch_keys("-Param:N=1000"), ["param:n"])
        self.assertEqual(matrix_module.switch_keys("-ini:PhysicsTest:[PhysicsTest.Backend.Bullet]:ManifoldPoolSize=64,[S]:B=2"),
                         ["ini:physicstest:[physicstest.backend.bullet]:manifoldpoolsize", "ini:physicstest:[s]:b"])

    def test_repeated_or_runner_switches_are_refused(self):
        # M8 review: the bench reads the FIRST occurrence of a switch (FParse::Value), so a later duplicate - a target's or a
        # --bench-arg after the matrix's - would be ignored silently; a switch the runner sets per cell would fight the cell's.
        base = {"bench_args": ["-HostCheck=record", "-PreRoll=0"], "targets": [{"label": "a", "backend": "Null"}, {"label": "b", "backend": "Null",
                                                                                                         "bench_args": ["-ExecCmds=p.X 1"]}]}
        matrix_module.check_switches(base, ["-HashEvery=1"])                      # unique keys: fine
        cases = [
            (dict(base), ["-PreRoll=0.5"], "preroll"),                            # --bench-arg repeats the matrix's (determinism matrix example)
            (dict(base, targets=[{"label": "a", "backend": "Null", "bench_args": ["-hostcheck=enforce"]}]), [], "hostcheck"),   # case-insensitive
            (dict(base, bench_args=["-PreRoll=0", "-PreRoll=1"]), [], "preroll"),  # twice in one source
            (dict(base, targets=[{"label": "a", "backend": "Null", "bench_args": ["-ExecCmds=p.X 1"]}]), ["-ExecCmds=p.Y 2"], "execcmds"),
            (dict(base, bench_args=["-ini:PhysicsTest:[S]:K=1"], targets=[{"label": "a", "backend": "Null", "bench_args": ["-ini:PhysicsTest:[S]:K=2"]}]), [],
             "ini:physicstest:[s]:k"),
            (dict(base), ["-Frames=10"], "frames"),                               # reserved: scenes[].frames
            (dict(base), ["-Out=x"], "out"),
            (dict(base, targets=[{"label": "a", "backend": "Null", "bench_args": ["-Param:Seed=1"]}]), [], "scenes[].params"),
            (dict(base, targets=[{"label": "a", "backend": "Null", "bench_args": ["-Threads=2"]}]), [], "threads"),
            (dict(base), ["-Session=s"], "session"),
            (dict(base), ["-Gate=G8"], "another mode"),
        ]
        for document, extra, needle in cases:
            with self.assertRaises(matrix_module.MatrixError, msg=str((document, extra))) as caught:
                matrix_module.check_switches(document, extra)
            self.assertIn(needle, str(caught.exception))
        # Different -ini: keys and the same switch on two different targets do not clash (each cell gets one target's).
        matrix_module.check_switches(dict(base, bench_args=["-ini:PhysicsTest:[S]:K=1"], targets=[
            {"label": "a", "backend": "Null", "bench_args": ["-ini:PhysicsTest:[S]:L=2", "-ExecCmds=p.X 1"]},
            {"label": "b", "backend": "Null", "bench_args": ["-ExecCmds=p.X 2"]}]), [])

    def test_runner_refuses_a_repeated_bench_arg_before_anything_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            matrix = load_matrix("determinism")   # bench_args -PreRoll=0
            with self.assertRaisesRegex(matrix_module.MatrixError, "preroll"):
                runner.run_matrix(matrix, pathlib.Path(directory) / "none.exe", None, pathlib.Path(directory) / "out",
                                  {"refused": False, "bypasses": [], "power_plan_high_performance": True, "power_scheme": {}},
                                  extra_args=["-PreRoll=0.5"], log=lambda *_: None)
            self.assertFalse((pathlib.Path(directory) / "out").exists())
            args = __main__.build_parser().parse_args(["run", "--matrix", "determinism", "--bench-arg=-PreRoll=0.5", "--dry-run"])
            with mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                self.assertEqual(runner.main(args), 2)
            self.assertIn("appears twice", stderr.getvalue())


class AnalyticTests(unittest.TestCase):
    def test_engine_dt_is_float_rounded(self):
        self.assertEqual(analytic.engine_dt(1.0 / 60.0), 0.01666666753590106964111328125)
        self.assertNotEqual(analytic.engine_dt(1.0 / 60.0), 1.0 / 60.0)

    def test_documented_values(self):
        for row in analytic.check_documented():
            self.assertTrue(row["ok"], row)
        z, vz = analytic.semi_implicit_euler_free_fall(1000.0, 0.0, -980.0, analytic.engine_dt(1.0 / 60.0), 60)
        self.assertLessEqual(abs(z - 501.8332813705), 1e-9)   # 03 M8: z60 = 501.8332813705 cm
        self.assertGreater(abs(z - 510.0), 8.0)                # the continuous solution is the wrong reference

    def test_closed_form_equals_the_loop(self):
        dt = analytic.engine_dt(1.0 / 60.0)
        z, v = 1000.0, 0.0
        for _ in range(60):
            v += -980.0 * dt
            z += v * dt
        zc, vc = analytic.semi_implicit_euler_free_fall(1000.0, 0.0, -980.0, dt, 60)
        self.assertLessEqual(abs(z - zc), 1e-9)
        self.assertLessEqual(abs(v - vc), 1e-9)

    def test_compare_with_cpp_detects_a_difference(self):
        cpp = {"schema": "physicstest.analytic/1", "cases": [dict(case) for case in analytic.cases()]}
        self.assertTrue(all(r["ok"] for r in analytic.compare_with_cpp(cpp)))
        broken = json.loads(json.dumps(cpp))
        broken["cases"][4]["values"]["z"] += 2e-9
        rows = analytic.compare_with_cpp(broken)
        self.assertEqual([(r["id"], r["key"]) for r in rows if not r["ok"]], [("S00.free_fall_dt_engine", "z")])
        missing = json.loads(json.dumps(cpp))
        missing["cases"].pop()
        self.assertFalse(all(r["ok"] for r in analytic.compare_with_cpp(missing)))

    def test_compare_with_cpp_reports_unknown_inputs_and_values_as_failures(self):
        cpp = {"schema": "physicstest.analytic/1", "cases": [json.loads(json.dumps(case)) for case in analytic.cases()]}
        cpp["cases"][4]["inputs"].pop("v0")                  # an input the Python side needs is missing
        cpp["cases"][5]["inputs"]["restitution"] = 0.5        # an input the Python table lacks
        cpp["cases"][6]["values"]["apex"] = 1.0               # a value Python does not compute
        cpp["cases"][7]["function"] = "Pendulum"              # an unknown function
        rows = analytic.compare_with_cpp(cpp)                 # no KeyError / ValueError: FAIL rows
        failed = {(r["id"], r["key"]) for r in rows if not r["ok"]}
        ids = [case["id"] for case in cpp["cases"]]
        self.assertIn((ids[4], "<evaluate>"), failed)
        self.assertIn((ids[5], "<case table>"), failed)
        self.assertIn((ids[6], "apex"), failed)
        self.assertIn((ids[7], "<evaluate>"), failed)


class AnalyticCliTests(unittest.TestCase):
    def test_unreadable_cpp_references_exit_2_with_one_line(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = str(pathlib.Path(directory) / "NoSuchBench.exe")
            bad_json = pathlib.Path(directory) / "bad.json"
            bad_json.write_text("{not json", encoding="utf-8")
            for argv in (["analytic", "--check-cpp", missing], ["analytic", "--cpp-json", str(bad_json)]):
                args = __main__.build_parser().parse_args(argv)
                with mock.patch("sys.stdout", new_callable=io.StringIO), mock.patch("sys.stderr", new_callable=io.StringIO) as stderr:
                    self.assertEqual(analytic.main(args), 2, argv)
                self.assertIn("ptbench analytic:", stderr.getvalue())
                self.assertNotIn("Traceback", stderr.getvalue())


class StatisticsTests(unittest.TestCase):
    def test_nearest_rank(self):
        values = sorted(range(1, 101))
        self.assertEqual(common.nearest_rank(values, 95.0), 95.0)
        self.assertEqual(common.nearest_rank(values, 99.0), 99.0)
        self.assertEqual(common.nearest_rank([5.0], 95.0), 5.0)
        self.assertTrue(math.isnan(common.nearest_rank([], 50.0)))

    def test_cv(self):
        self.assertAlmostEqual(common.cv_percent([10.0, 10.0, 10.0]), 0.0)
        self.assertAlmostEqual(common.cv_percent([9.0, 11.0]), 100.0 * math.sqrt(2.0) / 10.0)
        self.assertTrue(math.isnan(common.cv_percent([1.0])))

    def test_refused_table_has_no_statistics(self):
        rows = [_row(run_key="a", label="jolt.Default", win_headline_mean=1000.0, frame0_ns=5.0),
                _row(run_key="b", label="jolt.Default", win_headline_mean=1010.0, frame0_ns=5.0, desc_sha256="b" * 64),
                _row(run_key="c", label="bullet.Default", win_headline_mean=2000.0, frame0_ns=5.0, scene="P0.EmptyStep"),
                _row(run_key="d", label="bullet.Default", win_headline_mean=2020.0, frame0_ns=5.0, scene="P0.EmptyStep")]
        classified = validate.classify(pd.DataFrame(rows))
        cells = perf.cell_stats(classified, pd.DataFrame(columns=["run_key", "frame", "frame_ns"]))
        refused = cells[cells["table_error"] != ""]
        self.assertEqual(len(refused), 1)
        row = refused.iloc[0]
        self.assertEqual((row["label"], row["n_runs"], row["values_ns"]), ("jolt.Default", 2, []))
        for column in ("median_ns", "cv_pct", "p95_ns", "min_ns", "max_ns"):
            self.assertTrue(math.isnan(row[column]), column)
        sound = cells[cells["table_error"] == ""].iloc[0]
        self.assertAlmostEqual(sound["median_ns"], 2010.0)
        self.assertTrue(perf.scaling_alpha(cells).empty)
        # The report's per-cell CV list (§3) names the refused cell without a number.
        lines = report.section_cv(cells)
        [refused_line] = [line for line in lines if line.startswith("| jolt.Default")]
        self.assertIn("표 거부", refused_line)
        self.assertNotIn("충족", refused_line)
        self.assertNotIn("µs", refused_line)
        self.assertEqual(refused_line.count(" - "), 2)
        [sound_line] = [line for line in lines if line.startswith("| bullet.Default")]
        self.assertIn("2.01 µs", sound_line)

    def test_pooled_frames_use_each_runs_window(self):
        frames = pd.DataFrame({"run_key": ["a"] * 5 + ["b"] * 5, "frame": list(range(5)) * 2, "frame_ns": [1, 2, 3, 4, 5, 10, 20, 30, 40, 50]})
        runs = pd.DataFrame([{"run_key": "a", "win_headline_first": 1, "win_headline_last": 3}, {"run_key": "b", "win_headline_first": 4, "win_headline_last": 4},
                             {"run_key": "c", "win_headline_first": None, "win_headline_last": None}])
        self.assertEqual(sorted(perf.pooled_frames(frames, runs, "headline").tolist()), [2.0, 3.0, 4.0, 50.0])
        self.assertEqual(sorted(perf.pooled_frames(perf.frames_by_run(frames), runs, "headline").tolist()), [2.0, 3.0, 4.0, 50.0])

    def test_scaling_and_overhead_fits(self):
        rows = []
        for n, t in ((0, 2000.0), (1000, 1_002_000.0), (5000, 5_002_000.0)):
            rows.append({"table_error": "", "label": "x", "profile": "Normalized", "flag_profile": "Shipped", "T": 1, "scenario": "M3.BoxPile" if n else "P0.EmptyStep",
                         "scene": f"s{n}", "N": n, "median_ns": t})
        cells = pd.DataFrame(rows)
        alpha = perf.scaling_alpha(cells)
        self.assertEqual(len(alpha), 1)
        self.assertAlmostEqual(alpha.iloc[0]["alpha"], math.log(5_002_000 / 1_002_000) / math.log(5), places=9)
        model = perf.overhead_model(cells)
        self.assertAlmostEqual(model.iloc[0]["a_ns"], 2000.0, places=3)
        self.assertAlmostEqual(model.iloc[0]["b_ns_per_body"], 1000.0, places=6)


def _row(**overrides):
    row = {"exit_json": True, "exit_code": 0, "exit_status": "ok", "exit_json_valid": True, "manifest": True, "manifest_valid": True, "mode": "Bench",
           "instrumentation": "[]", "gates_failed": "[]", "contaminated": False, "timing_valid_for_headline": True, "timing_invalid_reasons": "[]",
           "flag_profile": "Shipped", "desc_sha256": "a" * 64, "process_exit_code": 0, "label": "jolt.Default", "scene": "M3.BoxPile[N=1000]",
           "profile": "Normalized", "T": 1, "run_key": "k", "schema_errors": "[]"}
    row.update(overrides)
    return row


class ValidateTests(unittest.TestCase):
    def test_accepts_a_headline_valid_run(self):
        self.assertEqual(validate.reject_reasons(_row()), [])

    def test_viz_manifest_is_rejected(self):
        reasons = validate.reject_reasons(_row(mode="Viz", timing_valid_for_headline=False, timing_invalid_reasons='["G10a: run mode Viz"]'))
        self.assertTrue(any("viz-mode manifest" in r for r in reasons), reasons)

    def test_missing_exit_json_is_a_failure_whatever_the_raw_code(self):
        reasons = validate.reject_reasons(_row(exit_json=False, exit_code=None, process_exit_code=0, manifest=False, mode=None, timing_valid_for_headline=None))
        self.assertTrue(any("exit.json missing" in r and "raw exit code 0 is not trusted" in r for r in reasons), reasons)

    def test_nan_from_pandas_is_missing(self):
        frame = pd.DataFrame([_row(exit_json=False, exit_code=None, manifest=False, mode=None)])
        reasons = validate.classify(frame).iloc[0]["reject_reasons"]
        self.assertFalse(any("nan" in r for r in reasons), reasons)

    def test_contaminated_gate_and_tier_reasons(self):
        reasons = validate.reject_reasons(_row(exit_code=4, exit_status="contaminated_host", contaminated=True, gates_failed='["G9"]', instrumentation='["counters"]',
                                               timing_valid_for_headline=False))
        text = " ".join(reasons)
        for expected in ("exit.json 4", "contaminated host", "preflight gate failed: G9", "instrumentation tier counters", "not headline-valid"):
            self.assertIn(expected, text)

    def test_mixed_flag_profiles_are_an_error(self):
        table = pd.DataFrame([_row(), _row(flag_profile="MatchedAVX2", label="physx.UE4Compat")])
        with self.assertRaisesRegex(validate.TableError, "flag profiles mixed"):
            validate.check_table(table, "t")

    def test_desc_mismatch_is_an_error(self):
        table = pd.DataFrame([_row(), _row(desc_sha256="b" * 64, label="bullet.Default")])
        with self.assertRaisesRegex(validate.TableError, "desc_sha256 differs"):
            validate.check_table(table, "t")

    def test_fatal_run_without_manifest_is_not_a_schema_error(self):
        # A Fatal run (exit 3, status fatal) writes exit.json only (ResultsSchema 1): no "manifest.json missing".
        reasons = validate.reject_reasons(_row(exit_code=3, exit_status="fatal", manifest=False, mode=None, timing_valid_for_headline=None))
        self.assertEqual(reasons, ["exit.json 3 (fatal)"])
        reasons = validate.reject_reasons(_row(exit_code=3, exit_status="preflight_failed", manifest=False, mode=None, timing_valid_for_headline=None))
        self.assertIn("manifest.json missing", reasons)
        self.assertFalse(runner.manifest_expected(3, "fatal", True))
        self.assertFalse(runner.manifest_expected(2, "usage_error", True))
        self.assertTrue(runner.manifest_expected(4, "contaminated_host", True))
        self.assertFalse(runner.manifest_expected(None, None, False))
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            common.write_json_atomic(path / "exit.json", {"schema": "physicstest.exit/1", "exit_code": 3, "meaning": "fatal", "status": "fatal", "category": "",
                                                          "reason": "check failed", "run_id": "x", "utc": "2026-10-22T00:00:00Z", "pid": 1,
                                                          "terminate_process": True, "outputs": ["exit.json"]})
            verdict = runner.judge(path, {"process_exit_code": 3, "timed_out": False}, "headline")
            self.assertEqual(verdict["schema_errors"], [])
            self.assertEqual(verdict["outcome"], "exit_3_fatal")

    def test_unsupported_scene_is_reported_not_failed(self):
        # M9: the bench refuses a scene whose required capability (joints) the backend lacks: exit.json 2, status unsupported,
        # no manifest. The run's outcome is "unsupported", the cell is "unsupported" and does not fail the session.
        self.assertFalse(runner.manifest_expected(2, "unsupported", True))
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            common.write_json_atomic(path / "exit.json", {"schema": "physicstest.exit/1", "exit_code": 2, "meaning": "gate_or_preflight_or_usage",
                                                          "status": "unsupported", "category": "unsupported",
                                                          "reason": "backend 'Null' does not support scenario M6a.Pendulum", "run_id": "x",
                                                          "utc": "2026-10-22T00:00:00Z", "pid": 1, "terminate_process": False, "outputs": []})
            verdict = runner.judge(path, {"process_exit_code": 2, "timed_out": False}, "headline")
            self.assertEqual(verdict["schema_errors"], [])
            self.assertEqual(verdict["outcome"], "unsupported")
        cells = [{"key": "k", "cell_id": "c"}]
        runs = [{"cell_key": "k", "repeat": 0, "attempt": 0, "outcome": "unsupported"}, {"cell_key": "k", "repeat": 1, "attempt": 0, "outcome": "unsupported"}]
        runner.summarize_cells(cells, runs, 2)
        self.assertEqual(cells[0]["state"], "unsupported")

    def test_table_notes_for_mixed_builds(self):
        table = validate.classify(pd.DataFrame([_row(exe_sha256="1" * 64, git_sha="g1", preset="test"), _row(exe_sha256="2" * 64, git_sha="g1", preset="test")]))
        notes = validate.table_notes(table)
        self.assertEqual(len(notes), 1)
        self.assertIn("2 exe values", notes[0])
        self.assertEqual(validate.table_notes(validate.classify(pd.DataFrame([_row(exe_sha256="1" * 64)]))), [])

    def test_tables_split_by_flag_profile_and_report_errors(self):
        classified = validate.classify(pd.DataFrame([_row(), _row(flag_profile="MatchedAVX2"), _row(label="b", desc_sha256="c" * 64)]))
        built = validate.tables(classified)
        self.assertEqual(len(built), 2)   # Shipped and MatchedAVX2 are separate tables
        errors = [error for _, _, error in built if error]
        self.assertEqual(len(errors), 1)  # the Shipped table mixes two desc_sha256 values
        self.assertIn("desc_sha256 differs", errors[0])
        mixed = validate.tables(classified, keys=("scene", "profile", "T"))
        self.assertIn("flag profiles mixed", mixed[0][2])


class DeterminismTests(unittest.TestCase):
    def test_first_divergence(self):
        a = {f: (f"0x{f:016x}", "q", f"0x{f + 1:016x}") for f in range(10)}
        b = dict(a)
        self.assertIsNone(determinism.first_divergence(a, b)["frame"])
        b[6] = ("0xdead", "q", a[6][2])
        b[8] = (a[8][0], "q", "0xbeef")
        result = determinism.first_divergence(a, b)
        self.assertEqual((result["frame"], result["field"]), (6, "h_native"))
        c = dict(a)
        c[3] = (a[3][0], "q", "0xbeef")
        self.assertEqual(determinism.first_divergence(a, c)["field"], "h_state")

    def _write_run(self, directory: pathlib.Path, name: str, rows: dict[int, tuple[str, str, str]]) -> dict:
        run_dir = directory / name
        run_dir.mkdir()
        lines = ["frame,h_native,h_quantized,h_state"] + [f"{f},{a},{b},{c}" for f, (a, b, c) in sorted(rows.items())]
        (run_dir / "hashes.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")
        last = rows[max(rows)]
        return {"run_name": name, "run_dir": str(run_dir), "exit_json": True, "exit_code": 0, "h_native": last[0], "h_state": last[2], "frames_run": len(rows),
                "exe_sha256": "e", "T": 1, "outcome": "ok"}

    def test_reference_is_the_majority(self):
        good = {f: (f"0x{f:04x}", "q", f"0x{f + 7:04x}") for f in range(4)}
        odd = dict(good)
        odd[2] = (good[2][0], "q", "0xbeef")
        odd[3] = ("0xdead", "q", good[3][2])
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            runs = [self._write_run(path, "r0", odd), self._write_run(path, "r1", good), self._write_run(path, "r2", good)]
            result = determinism.analyze_group({"name": "g", "source": "s", "runs": runs})
            self.assertEqual(result["verdict"], "DIFFERENT")
            self.assertEqual((result["reference_run"], result["reference_is_majority"], result["divergent_runs"]), ("r1", True, ["r0"]))
            self.assertEqual((result["first_divergence"]["frame"], result["first_divergence"]["field"], result["first_divergence"]["run"]), (2, "h_state", "r0"))
            self.assertIn("r1 (majority)", determinism.to_markdown([result]))
            two = determinism.analyze_group({"name": "g", "source": "s", "runs": runs[:2]})
            self.assertEqual((two["reference_run"], two["reference_is_majority"], two["divergent_runs"]), ("r0", False, ["r1"]))

    def test_a_failed_run_makes_the_group_incomplete(self):
        good = {f: (f"0x{f:04x}", "q", f"0x{f + 7:04x}") for f in range(4)}
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            runs = [self._write_run(path, "r0", good), self._write_run(path, "r1", good)]
            died = {"run_name": "r2", "run_dir": str(path / "r2"), "exit_json": False, "exit_code": None, "outcome": "no_exit_json", "T": 1}
            contaminated = dict(self._write_run(path, "r3", good), exit_code=4, outcome="contaminated")
            # Two ok runs agree, the third process died: DET1 (all N agree) is not shown - "incomplete", never "equal".
            result = determinism.analyze_group({"name": "g", "source": "s", "runs": runs + [died]})
            self.assertEqual(result["verdict"], "incomplete")
            self.assertIn("r2: no_exit_json", result["incomplete_reason"])
            self.assertIn("**incomplete**", determinism.to_markdown([result]))
            one = determinism.analyze_group({"name": "g", "source": "s", "runs": runs[:1] + [died]})
            self.assertEqual(one["verdict"], "incomplete")
            # An exit-4 attempt ran to its end (only its timing is contaminated): it counts for DET1.
            self.assertEqual(determinism.analyze_group({"name": "g", "source": "s", "runs": runs + [contaminated]})["verdict"], "equal")
            # A real difference wins over a failed run.
            odd = dict(good)
            odd[3] = ("0xdead", "q", good[3][2])
            different = determinism.analyze_group({"name": "g", "source": "s", "runs": runs + [self._write_run(path, "r4", odd), died]})
            self.assertEqual(different["verdict"], "DIFFERENT")
            args = argparse.Namespace(inputs=["x"], out=None, json=None)
            with mock.patch("sys.stdout", new_callable=io.StringIO) as stdout:
                with mock.patch.object(determinism, "analyze", return_value=[result]):
                    self.assertEqual(determinism.main(args), 3)
                with mock.patch.object(determinism, "analyze", return_value=[result, different]):
                    self.assertEqual(determinism.main(args), 1)
            self.assertIn("DET1 INCOMPLETE", stdout.getvalue())

    def test_read_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "hashes.csv").write_text("frame,h_native,h_quantized,h_state\n0,0x1,0x2,0x3\n1,0x4,0x5,0x6\n", encoding="utf-8")
            self.assertEqual(determinism.read_hashes(path), {0: ("0x1", "0x2", "0x3"), 1: ("0x4", "0x5", "0x6")})


class EnvcheckTests(unittest.TestCase):
    def test_parse_power_scheme(self):
        balanced = envcheck.parse_power_scheme("전원 구성표 GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (균형 조정)")
        self.assertEqual((balanced["kind"], balanced["name"], balanced["high_performance"]), ("Balanced", "균형 조정", False))
        high = envcheck.parse_power_scheme("Power Scheme GUID: 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c  (High performance)")
        self.assertTrue(high["high_performance"])
        ultimate = envcheck.parse_power_scheme("Power Scheme GUID: e9a42b02-d5df-448d-aa00-03f14749eb61  (Ultimate Performance)")
        self.assertTrue(ultimate["high_performance"])
        custom = envcheck.parse_power_scheme("Power Scheme GUID: 11111111-2222-3333-4444-555555555555  (Mine)")
        self.assertEqual((custom["kind"], custom["high_performance"]), ("custom", False))
        self.assertTrue(envcheck.parse_power_scheme("garbage", 1)["error"])

    def test_gate_refuses_balanced_unless_bypassed(self):
        # The plan is injected, so the refusal path is tested on any machine (also after M11's High performance switch).
        for text in (BALANCED_TEXT, CUSTOM_TEXT):
            reader = lambda text=text: envcheck.parse_power_scheme(text)
            refused = envcheck.run_envcheck(allow_concurrent=True, skip_idle=True, scheme_reader=reader, process_finder=lambda: [])
            self.assertTrue(refused["refused"], text)
            self.assertEqual([c["id"] for c in refused["checks"] if not c["pass"] and not c["bypassed"]], ["power_plan"])
            self.assertFalse(refused["power_plan_high_performance"])
            allowed = envcheck.run_envcheck(allow_balanced=True, skip_idle=True, scheme_reader=reader, process_finder=lambda: [])
            self.assertFalse(allowed["refused"])
            self.assertEqual(allowed["bypasses"], ["--allow-balanced", "--skip-idle"])
        high = envcheck.run_envcheck(skip_idle=True, scheme_reader=lambda: envcheck.parse_power_scheme(HIGH_TEXT), process_finder=lambda: [])
        self.assertFalse(high["refused"])
        self.assertEqual(high["bypasses"], ["--skip-idle"])
        busy = envcheck.run_envcheck(skip_idle=True, scheme_reader=lambda: envcheck.parse_power_scheme(HIGH_TEXT),
                                     process_finder=lambda: [{"pid": 1, "name": "PhysicsTestBench.exe"}])
        self.assertTrue(busy["refused"])

    def test_skip_idle_is_a_recorded_bypass(self):
        result = envcheck.run_envcheck(allow_balanced=True, skip_idle=True, scheme_reader=lambda: envcheck.parse_power_scheme(BALANCED_TEXT), process_finder=lambda: [])
        [idle] = [c for c in result["checks"] if c["id"] == "idle"]
        self.assertEqual((idle["pass"], idle["bypassed"], idle["bypass_flag"]), (False, True, "--skip-idle"))
        self.assertIn("--skip-idle", result["bypasses"])
        self.assertTrue(result["idle"]["skipped"])
        session_schema = common.validator("session.schema.json").schema
        self.assertIn("--skip-idle", session_schema["properties"]["bypasses"]["items"]["enum"])

    def test_idle_wait_keeps_trying_windows_until_one_passes(self):
        now = [0.0]

        def clock():
            return now[0]

        def fake(values):
            values = list(values)

            def measure(window_s):
                now[0] += window_s
                return values.pop(0) if values else 4.0
            return measure

        passed = envcheck.measure_idle(3.0, 2.0, 5, 120.0, measure=fake([4.1, 3.5, 3.9, 4.4, 3.3, 3.8, 2.9]), clock=clock)
        self.assertEqual((passed["pass"], passed["windows"], passed["waited_s"]), (True, 7, 14.0))   # past the old five-window limit
        now[0] = 0.0
        failed = envcheck.measure_idle(3.0, 2.0, 5, 20.0, measure=fake([]), clock=clock)
        self.assertEqual((failed["pass"], failed["windows"], failed["waited_s"]), (False, 10, 20.0))   # bounded by the wait limit
        now[0] = 0.0
        no_wait = envcheck.measure_idle(3.0, 2.0, 5, 0.0, measure=fake([]), clock=clock)
        self.assertEqual(no_wait["windows"], 5)                                                         # at least the minimum

    def test_system_busy_sampler(self):
        sampler = envcheck.SystemBusySampler()
        sampler.start()
        busy = sampler.stop()
        self.assertGreaterEqual(busy, 0.0)
        self.assertLessEqual(busy, 100.0)


class RunnerTests(unittest.TestCase):
    def test_cell_values_are_headline_valid_runs_only(self):
        cells = [{"key": "k", "cell_id": "c", "label": "x"}]
        runs = [{"cell_key": "k", "repeat": 0, "attempt": 0, "outcome": "ok", "timing_valid_for_headline": True, "window_mean_ns": 100.0},
                {"cell_key": "k", "repeat": 1, "attempt": 0, "outcome": "ok", "timing_valid_for_headline": False, "window_mean_ns": 500.0},
                {"cell_key": "k", "repeat": 2, "attempt": 0, "outcome": "ok", "timing_valid_for_headline": True, "window_mean_ns": 110.0}]
        runner.summarize_cells(cells, runs, 3)
        cell = cells[0]
        self.assertEqual((cell["state"], cell["n_ok"], cell["n_not_headline_valid"], cell["values_ns"]), ("ok", 3, 1, [100.0, 110.0]))
        self.assertAlmostEqual(cell["cv_pct"], common.cv_percent([100.0, 110.0]))
        self.assertIn("not headline-valid", runner.cv_table({"runner": {"window": "headline"}, "cells": cells}))

    def _fake_bench(self, code: str) -> tuple[pathlib.Path, list[str]]:
        # The interpreter stands in for the bench: execute appends -Out=<dir> (sys.argv[1] of -c) and runs it in that directory.
        return pathlib.Path(sys.executable), ["-I", "-c", code]

    def test_a_teardown_hang_after_exit_json_is_killed_and_keeps_its_verdict(self):
        exe, args = self._fake_bench("import json, time\nopen('exit.json', 'w').write('{}')\ntime.sleep(60)")
        with tempfile.TemporaryDirectory() as directory:
            started = time.perf_counter()
            result = runner.execute(exe, args, pathlib.Path(directory) / "run", timeout_s=50.0, teardown_grace_s=0.5, poll_s=0.1)
            self.assertLess(time.perf_counter() - started, 20.0)
            self.assertEqual((result["teardown_killed"], result["timed_out"]), (True, False))
            self.assertIsNotNone(result["exit_json_after_s"])
            self.assertIsNotNone(result["process_exit_code"])

    def test_a_run_without_exit_json_times_out(self):
        exe, args = self._fake_bench("import time\ntime.sleep(60)")
        with tempfile.TemporaryDirectory() as directory:
            result = runner.execute(exe, args, pathlib.Path(directory) / "run", timeout_s=1.0, teardown_grace_s=0.5, poll_s=0.1)
            self.assertEqual((result["teardown_killed"], result["timed_out"]), (False, True))

    def test_the_child_is_killed_when_the_wait_is_interrupted(self):
        exe, args = self._fake_bench("import time\ntime.sleep(60)")
        spawned = []
        popen_class = subprocess.Popen
        real_wait = popen_class.wait

        def popen(*a, **k):
            spawned.append(popen_class(*a, **k))
            return spawned[-1]

        def interrupted_wait(self, timeout=None):
            if timeout is not None:   # the runner's polling wait: Ctrl+C arrives here
                raise KeyboardInterrupt
            return real_wait(self, timeout)

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(popen_class, "wait", interrupted_wait), mock.patch.object(runner.subprocess, "Popen", popen):
                with self.assertRaises(KeyboardInterrupt):
                    runner.execute(exe, args, pathlib.Path(directory) / "run", timeout_s=30.0)
            self.assertEqual(len(spawned), 1)
            self.assertIsNotNone(spawned[0].poll())   # the finally block killed it (and reaped it)

    def test_kill_on_close_job(self):
        job = runner.KillOnCloseJob()
        self.assertIsNotNone(job.handle, job.error)
        exe, args = self._fake_bench("import time\ntime.sleep(60)")
        child = subprocess.Popen([str(exe), *args], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            self.assertTrue(job.assign(child), job.error)
        finally:
            child.kill()
            child.wait(timeout=10)

    def test_git_and_powercfg_by_full_path(self):
        git = common.find_executable("git")
        if git is not None:
            self.assertTrue(os.path.isabs(git) and git.lower().endswith(".exe"), git)
        with tempfile.TemporaryDirectory() as directory, mock.patch.dict(os.environ, {"PATH": f".{os.pathsep}{directory}"}):
            pathlib.Path(directory, "fakegit.exe").write_bytes(b"")
            self.assertEqual(common.find_executable("fakegit"), os.path.join(directory, "fakegit.exe"))
            self.assertIsNone(common.find_executable("python"))   # "." is never searched
        self.assertTrue(os.path.isfile(common.system32_executable("powercfg.exe")))


class LoadTests(unittest.TestCase):
    def test_parquet_types_survive_a_died_run(self):
        died = {"run_key": "d", "exit_json": False, "exit_json_valid": None, "manifest": False, "manifest_valid": None, "timing_valid_for_headline": None,
                "contaminated": None, "power_scheme_high_performance": None, "exit_code": None, "process_exit_code": 0, "T": None, "attempt": 0, "repeat": 1,
                "win_headline_first": None, "win_headline_mean": None, "params": "{}", "label": "jolt.Default", "outcome": "no_exit_json", "power_scheme": None}
        ok = {"run_key": "o", "exit_json": True, "exit_json_valid": True, "manifest": True, "manifest_valid": True, "timing_valid_for_headline": True,
              "contaminated": False, "power_scheme_high_performance": False, "exit_code": 0, "process_exit_code": 0, "T": 1, "attempt": 0, "repeat": 0,
              "win_headline_first": 1, "win_headline_mean": 1234.5, "params": "{}", "label": "jolt.Default", "outcome": "ok", "power_scheme": "균형 조정"}
        loaded = load.Loaded(runs=pd.DataFrame([died, ok]), frames=pd.DataFrame(columns=["run_key"] + load.TIMING_COLUMNS),
                             effective=pd.DataFrame(columns=["run_key", "key"]), gates=pd.DataFrame(columns=["run_key", "id"]), sessions={}, sources=[])
        with tempfile.TemporaryDirectory() as directory:
            load.write_parquet(loaded, pathlib.Path(directory))
            back = pd.read_parquet(pathlib.Path(directory) / "runs.parquet")
        for column in ("exit_json", "exit_json_valid", "manifest_valid", "timing_valid_for_headline", "contaminated", "power_scheme_high_performance"):
            self.assertEqual(str(back[column].dtype), "boolean", column)
            self.assertTrue(pd.isna(back[column].iloc[0]) or column == "exit_json", column)
        self.assertEqual(back["contaminated"].iloc[1], False)
        self.assertEqual(back["timing_valid_for_headline"].iloc[1], True)
        self.assertEqual(back["exit_json"].tolist(), [False, True])
        for column in ("exit_code", "process_exit_code", "T", "attempt", "repeat", "win_headline_first"):
            self.assertEqual(str(back[column].dtype), "Int64", column)
        self.assertTrue(pd.isna(back["exit_code"].iloc[0]))
        self.assertEqual(int(back["exit_code"].iloc[1]), 0)
        self.assertTrue(pd.isna(back["power_scheme"].iloc[0]))
        self.assertEqual(back["power_scheme"].iloc[1], "균형 조정")
        self.assertNotIn("nan", [str(v) for v in back["outcome"]])

    def test_explicit_seed_stays_in_the_scene_label(self):
        self.assertEqual(load._scene_params({"scene": {"params": {"N": "1000", "Seed": "1"}, "seed": 1}}), {"N": "1000", "Seed": "1"})
        default = {"scene": {"params": {"N": "1000", "Seed": "24301"}, "seed": 24301}}   # the manifest lists every resolved parameter
        self.assertEqual(load.scene_label("M3.BoxPile", load._scene_params(default)), "M3.BoxPile[N=1000]")
        self.assertEqual(load.scene_label("P0.EmptyStep", load._scene_params({"scene": {"params": {}, "seed": 24301}})), "P0.EmptyStep")


class ReportHeaderTests(unittest.TestCase):
    def _session(self, session_id, hp, bypasses):
        return {"session_id": session_id, "bypasses": bypasses, "unofficial_reasons": ["no conformance stamp (M10)"],
                "environment": {"power_plan_high_performance": hp, "power_scheme": {"name": "균형 조정" if not hp else "High performance",
                                                                                       "kind": "Balanced" if not hp else "High performance",
                                                                                       "guid": "381b4222-f694-41f0-9685-ff5bb260df2e" if not hp else "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"}}}

    def test_run_directories_without_a_session_are_marked(self):
        runs = pd.DataFrame([{"power_scheme_high_performance": False, "power_scheme": "균형 조정", "power_scheme_guid": "{381B4222-F694-41F0-9685-FF5BB260DF2E}"}] * 3)
        head = "\n".join(report._session_header({}, runs))
        self.assertIn("**전원 계획이 High performance가 아니다: 균형 조정 (Balanced, 381b4222-f694-41f0-9685-ff5bb260df2e)**", head)
        self.assertIn("run 3/3개의 manifest", head)
        self.assertIn("세션 파일 없이", head)
        self.assertIn("power plan is not High performance", head)

    def test_several_sessions_union_bypasses_and_a_plan_switch_is_caught(self):
        sessions = {"a": self._session("s-a", False, ["--allow-balanced"]), "b": self._session("s-b", True, ["--allow-busy"])}
        head = "\n".join(report._session_header(sessions, pd.DataFrame()))
        self.assertIn("전원 계획이 High performance가 아니다", head)
        self.assertIn("`--allow-balanced, --allow-busy`", head)
        self.assertIn("세션 2개를 합친 리포트다: `s-a`, `s-b`", head)
        self.assertIn("2가지", head)
        # A session whose gate saw High performance but whose runs did not (the plan was switched during the session).
        runs = pd.DataFrame([{"power_scheme_high_performance": True, "power_scheme": "High performance", "power_scheme_guid": "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"},
                             {"power_scheme_high_performance": False, "power_scheme": "균형 조정", "power_scheme_guid": "381b4222-f694-41f0-9685-ff5bb260df2e"}])
        head = "\n".join(report._session_header({"b": self._session("s-b", True, [])}, runs))
        self.assertIn("전원 계획이 High performance가 아니다", head)
        self.assertIn("run 1/2개의 manifest", head)
        quiet = "\n".join(report._session_header({"b": self._session("s-b", True, [])}, pd.DataFrame()))
        self.assertNotIn("High performance가 아니다", quiet)
        self.assertIn("UNOFFICIAL", quiet)

    def test_head_lines_are_separate_paragraphs(self):
        lines = report._session_header({"a": self._session("s-a", False, ["--allow-balanced"])}, pd.DataFrame())
        self.assertTrue(lines[0].startswith("> **전원 계획이"))
        for before, after in zip(lines, lines[1:]):
            self.assertFalse(before.startswith("> ") and after.startswith("> "), (before, after))   # a bare '>' between them

    def test_conditions(self):
        condition = report.conditions({"a": self._session("s-a", False, ["--allow-balanced"])}, pd.DataFrame())
        self.assertEqual(condition["figure"], "UNOFFICIAL · power plan Balanced, not High performance · gate bypassed: --allow-balanced")
        self.assertIn("전원 계획 균형 조정", condition["markdown"])
        self.assertIn("`--allow-balanced`", condition["markdown"])
        quiet = report.conditions({"b": self._session("s-b", True, [])}, pd.DataFrame())
        self.assertEqual(quiet["figure"], "UNOFFICIAL")

    def _loaded(self, with_frames: bool = True) -> load.Loaded:
        runs, frames = [], []
        for label in ("jolt.Default", "physx.UE4Compat"):
            for repeat in range(3):
                key = f"{label}-{repeat}"
                runs.append(_row(run_key=key, run_name=key, label=label, scenario="M3.BoxPile", scene="M3.BoxPile[N=1000]", N=1000, frame0_ns=5000.0,
                                 cell_id=f"{label}|Normalized|M3.BoxPile[N=1000]|T1", outcome="ok", win_headline_mean=1.0e6 + 1000.0 * repeat,
                                 win_headline_first=1, win_headline_last=3, power_scheme_high_performance=False, power_scheme="균형 조정",
                                 power_scheme_guid="{381B4222-F694-41F0-9685-FF5BB260DF2E}"))
                frames += [{"run_key": key, "frame": f, "frame_ns": 1.0e6 + 10.0 * f} for f in range(4)]
        return load.Loaded(runs=pd.DataFrame(runs), frames=pd.DataFrame(frames if with_frames else [], columns=["run_key", "frame", "frame_ns"]),
                           effective=pd.DataFrame(columns=["run_key", "key"]), gates=pd.DataFrame(columns=["run_key", "id"]), sessions={}, sources=[])

    def test_every_table_and_figure_carries_the_condition(self):
        titles = []
        real_titles = report._titles

        def record(fig, ax, title, condition):
            titles.append(condition)
            real_titles(fig, ax, title, condition)

        with tempfile.TemporaryDirectory() as directory, mock.patch.object(report.load_module, "load", return_value=self._loaded()), \
                mock.patch.object(report, "_titles", record):
            text = report.build_report(directory, pathlib.Path(directory) / "h").read_text(encoding="utf-8")
            pngs = sorted(p.name for p in (pathlib.Path(directory) / "h").glob("*.png"))
        self.assertEqual(pngs, ["frames_01.png", "table_01.png"])
        self.assertEqual(titles, ["UNOFFICIAL · power plan Balanced, not High performance"] * 2)
        lines = text.splitlines()
        headings = [i for i, line in enumerate(lines) if line.startswith("### ") or line.startswith("## 3.") or line.startswith("## 4.")]
        self.assertEqual(len(headings), 3)
        for index in headings:
            following = next(line for line in lines[index + 1:] if line.strip())
            self.assertTrue(following.startswith("> 조건: **비공식 (UNOFFICIAL)** · **전원 계획 균형 조정"), following)
        self.assertIn("재시도 횟수는 세션 파일이 없어 알 수 없다", text)

    def test_a_window_without_values_writes_no_figure(self):
        # A window no run has (e.g. --window impact over P0.EmptyStep, which declares only a headline window): no value in
        # any run (M8 review: ValueError "Axis limits cannot be NaN or Inf" aborted the whole report).
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(report.load_module, "load", return_value=self._loaded()):
            text = report.build_report(directory, pathlib.Path(directory) / "i", window="impact").read_text(encoding="utf-8")
            self.assertEqual(list((pathlib.Path(directory) / "i").glob("*.png")), [])
        self.assertIn("윈도 `impact`에 값 없음", text)
        self.assertIn("그림 없음", text)
        self.assertFalse(report.plot_table(pd.DataFrame([{"label": "x", "median_ns": math.nan, "values_ns": []}]), {}, "t", pathlib.Path("unused.png")))
        self.assertFalse(report.plot_series({"x": pd.DataFrame(columns=["frame", "median_ns", "q25_ns", "q75_ns"])}, {}, "t", pathlib.Path("unused.png")))


class LabelTests(unittest.TestCase):
    def test_null_dll_has_its_own_label(self):
        self.assertEqual(load.default_label({"backend": {"name": "Null", "variant": "", "module_file": None}}), "null")
        self.assertEqual(load.default_label({"backend": {"name": "Null", "variant": "", "module_file": "PtBackend_Null.dll"}}), "null.Dll")
        self.assertEqual(load.default_label({"backend": {"name": "PhysX", "variant": "UE4Compat", "module_file": "PtBackend_PhysX.dll"}}), "physx.UE4Compat")


class ConsoleTests(unittest.TestCase):
    def test_redirected_stdout_is_utf8(self):
        # python -I ignores PYTHONIOENCODING: a redirected stdout is the ANSI code page (CP949 here) unless ptbench reconfigures it.
        env = {k: v for k, v in os.environ.items() if k not in ("PYTHONIOENCODING", "PYTHONUTF8")}
        code = "from ptbench.common import use_utf8_stdio; use_utf8_stdio(); print('5.40 \u00b5s \uade0\ud615 \uc870\uc815')"
        completed = subprocess.run([sys.executable, "-I", "-c", code], capture_output=True, timeout=60, check=False, env=env)
        self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", "replace"))
        self.assertEqual(completed.stdout.decode("utf-8").strip(), "5.40 \u00b5s \uade0\ud615 \uc870\uc815")

    def test_envcheck_json_through_a_pipe(self):
        env = {k: v for k, v in os.environ.items() if k not in ("PYTHONIOENCODING", "PYTHONUTF8")}
        completed = subprocess.run([sys.executable, "-I", "-m", "ptbench", "envcheck", "--allow-balanced", "--allow-concurrent", "--skip-idle", "--json"],
                                   capture_output=True, timeout=120, check=False, env=env)
        self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", "replace"))
        document = json.loads(completed.stdout.decode("utf-8"))   # UTF-8, whatever the console code page
        self.assertIn("--skip-idle", document["bypasses"])


class AtomicWriteTests(unittest.TestCase):
    def test_replace_is_retried_while_the_target_is_held(self):
        real = os.replace
        calls = []

        def flaky(source, target):
            calls.append(target)
            if len(calls) < 3:
                raise PermissionError(13, "held by another process")
            return real(source, target)

        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "session.json"
            with mock.patch.object(common.os, "replace", flaky), mock.patch.object(common, "REPLACE_WAIT_S", 0.0):
                common.write_json_atomic(path, {"a": 1})
            self.assertEqual(common.read_json(path), {"a": 1})
            self.assertEqual(len(calls), 3)


class SchemaTests(unittest.TestCase):
    def test_schemas_are_valid_draft_2020_12(self):
        for name in ("matrix.schema.json", "session.schema.json", "manifest.schema.json", "exit.schema.json"):
            common.validator(name)

    def test_every_matrix_validates(self):
        for path in sorted(common.matrices_dir().glob("*.json")):
            self.assertEqual(common.schema_errors(common.read_json(path), "matrix.schema.json"), [], path)

    def test_session_schema_rejects_an_ok_run_without_exit_json(self):
        run = {"run_name": "r0_000_x", "attempt": 0, "cell_id": "c", "cell_key": "x", "repeat": 0, "order": 0, "block_seed": 1, "out_dir": "runs/x", "wall_s": 1.0,
               "timed_out": False, "process_exit_code": 0, "exit_json": False, "exit_code": None, "outcome": "ok", "retried": False}
        validator = common.validator("session.schema.json")
        item_schema = validator.schema["properties"]["runs"]["items"]
        import jsonschema

        errors = list(jsonschema.Draft202012Validator(item_schema).iter_errors(run))
        self.assertTrue(errors)   # an attempt without exit.json can never be "ok" (raw exit code 0 notwithstanding)
        run["outcome"] = "no_exit_json"
        self.assertEqual(list(jsonschema.Draft202012Validator(item_schema).iter_errors(run)), [])


# M9: the behaviour analysis tests live in test_behavior; CTest PhysicsTest.Py.Unit runs this module, so its test classes
# are imported here (unittest collects every TestCase in the module namespace).
from ptbench.tests.test_behavior import (AnalyticBehaviourTests, AnalyzerTests, BehaviourMatrixTests, GeometryTests,  # noqa: E402,F401
                                         StatesFileTests, TagTests)


if __name__ == "__main__":
    unittest.main()
