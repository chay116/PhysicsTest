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

from ptbench import __main__, analytic, common, envcheck, runner, stamps
from ptbench import matrix as matrix_module
from ptbench.analysis import bootstrap, claims, comparability, determinism, load, perf, report, validate
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
           "profile": "Normalized", "T": 1, "run_key": "k", "schema_errors": "[]", "stamp_valid": True, "stamp_reason": ""}
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
        for expected in ("exit.json 4", "contaminated host", "gate failed: G9", "instrumentation tier counters", "not headline-valid"):
            self.assertIn(expected, text)
        # M8 review: the contamination is named once, not again inside the quoted headline reasons.
        reasons = validate.reject_reasons(_row(exit_code=4, exit_status="contaminated_host", contaminated=True, timing_valid_for_headline=False,
                                               timing_invalid_reasons='["exit code 4 (contaminated_host)", "contaminated host"]'))
        self.assertEqual(" ".join(reasons).count("contaminated host"), 1, reasons)
        self.assertEqual(reasons[-1], "not headline-valid")

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
        with self.assertRaisesRegex(validate.TableError, "flag profiles mixed"):
            validate.check_table(classified[classified["label"] == "jolt.Default"], "a table grouped without the flag profile")


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
            args = argparse.Namespace(inputs=[str(path)], out=None, json=None)
            with mock.patch("sys.stdout", new_callable=io.StringIO) as stdout:
                with mock.patch.object(determinism, "analyze", return_value=[result]):
                    self.assertEqual(determinism.main(args), 3)
                # M10 (M8 review): a path that does not exist is a usage error (exit 2), never an empty result
                self.assertEqual(determinism.main(argparse.Namespace(inputs=[str(path / "no_such_dir")], out=None, json=None)), 2)
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
        # M10 (M8 review): the base interpreter, not the .venv launcher - the launcher runs the real interpreter as a child, which
        # could still hold child.log when the killed launcher's directory is cleaned up (a rare PermissionError in CTest).
        return pathlib.Path(getattr(sys, "_base_executable", None) or sys.executable), ["-I", "-c", code]

    def test_a_teardown_hang_after_exit_json_is_killed_and_keeps_its_verdict(self):
        exe, args = self._fake_bench("import json, time\nopen('exit.json', 'w').write('{}')\ntime.sleep(60)")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            started = time.perf_counter()
            result = runner.execute(exe, args, pathlib.Path(directory) / "run", timeout_s=50.0, teardown_grace_s=0.5, poll_s=0.1)
            self.assertLess(time.perf_counter() - started, 20.0)
            self.assertEqual((result["teardown_killed"], result["timed_out"]), (True, False))
            self.assertIsNotNone(result["exit_json_after_s"])
            self.assertIsNotNone(result["process_exit_code"])

    def test_a_run_without_exit_json_times_out(self):
        exe, args = self._fake_bench("import time\ntime.sleep(60)")
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
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

        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
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

    def test_headline_rule_has_the_m10_fix_round_1_conditions(self):
        # M10 fix round 1 (TimingContract 6 #11, #12): a headline-valid manifest has a calibration residual below 1e-5 and, at T = 1,
        # a G10b verdict or a short-window Skip (no_census / contended carry no single-thread proof). The rule's "then" alone,
        # against the fields it constrains.
        import jsonschema

        schema = common.validator("manifest.schema.json").schema
        [rule] = [r for r in schema["allOf"] if "M8 headline consistency" in r.get("$comment", "")]
        then = jsonschema.Draft202012Validator(rule["then"])
        self.assertTrue(list(then.iter_errors({"clock": {"calibration": {"valid": True, "max_residual": 2e-5}}})))
        self.assertEqual(list(then.iter_errors({"clock": {"calibration": {"valid": True, "max_residual": 2e-6}}})), [])
        for kind, rejected in (("contended", True), ("no_census", True), ("short_window", False), ("", False)):
            errors = list(then.iter_errors({"threads": {"census_window": {"T": 1, "g10b_skip": kind}}}))
            self.assertEqual(bool(errors), rejected, kind)
        self.assertEqual(list(then.iter_errors({"threads": {"census_window": {"T": 2, "g10b_skip": "no_census"}}})), [])   # T > 1: scaling
        self.assertEqual(list(then.iter_errors({"threads": {"census_window": None}})), [])

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


# ---- M10 ----------------------------------------------------------------------------------------------------------------------

def _interval(rho: float, low: float, high: float, n: int = 10) -> bootstrap.Interval:
    return bootstrap.Interval(rho, low, high, n, bootstrap.B_DEFAULT)


class ClaimRuleTests(unittest.TestCase):
    """03 M10: "주장 규칙을 합성 데이터 세 경우(CI가 1을 포함, |rho - 1| < 3 %, 방향 불일치)로 검증한다"."""

    @staticmethod
    def _blocks(values: list[float]) -> dict[int, float]:
        return dict(enumerate(values))

    def test_ci_includes_1_is_no_claim(self):
        # Synthetic blocks: A and B overlap (A sometimes faster, sometimes slower): the block-bootstrap CI straddles 1.
        a = self._blocks([100.0, 104.0, 97.0, 103.0, 99.0, 101.0, 96.0, 104.0, 98.0, 102.0])
        b = self._blocks([101.0, 99.0, 103.0, 98.0, 102.0, 100.0, 104.0, 97.0, 101.0, 99.0])
        shipped = bootstrap.block_bootstrap_ratio(a, b, seed=1)
        avx2 = bootstrap.block_bootstrap_ratio(a, b, seed=2)
        self.assertLessEqual(shipped.low, 1.0)
        self.assertGreaterEqual(shipped.high, 1.0)
        claim = claims.evaluate("A", "B", "S", shipped, avx2)
        self.assertEqual(claim.verdict, claims.VERDICT_NONE)
        self.assertIn("includes 1", claim.reason)

    def test_small_effect_is_no_claim(self):
        # A is consistently 1.5 % faster: the CI excludes 1, but |rho - 1| < 3 %.
        b = self._blocks([1000.0 + 2 * i for i in range(10)])
        a = {k: v * 0.985 for k, v in b.items()}
        shipped = bootstrap.block_bootstrap_ratio(a, b, seed=3)
        self.assertTrue(shipped.high < 1.0)
        claim = claims.evaluate("A", "B", "S", shipped, bootstrap.block_bootstrap_ratio(a, b, seed=4))
        self.assertEqual(claim.verdict, claims.VERDICT_NONE)
        self.assertIn("< 3 %", claim.reason)

    def test_direction_mismatch_is_isa_dependent(self):
        b = self._blocks([1000.0 + 3 * i for i in range(10)])
        faster = {k: v * 0.90 for k, v in b.items()}    # Shipped: A 10 % faster
        slower = {k: v * 1.08 for k, v in b.items()}    # MatchedAVX2: A 8 % slower
        claim = claims.evaluate("A", "B", "S", bootstrap.block_bootstrap_ratio(faster, b, seed=5), bootstrap.block_bootstrap_ratio(slower, b, seed=6))
        self.assertEqual(claim.verdict, claims.VERDICT_ISA)
        self.assertIn("ISA-dependent", claim.text)

    def test_a_claim_needs_both_flag_profiles(self):
        good = _interval(0.90, 0.88, 0.92)
        self.assertEqual(claims.evaluate("A", "B", "S", good, _interval(0.91, 0.89, 0.93)).verdict, claims.VERDICT_FASTER)
        self.assertEqual(claims.evaluate("A", "B", "S", _interval(1.10, 1.08, 1.12), _interval(1.09, 1.07, 1.11)).verdict, claims.VERDICT_SLOWER)
        self.assertEqual(claims.evaluate("A", "B", "S", good, None).verdict, claims.VERDICT_NONE)
        self.assertEqual(claims.evaluate("A", "B", "S", None, good).verdict, claims.VERDICT_NONE)
        # Same direction, but MatchedAVX2's own CI includes 1: no claim (not ISA-dependent).
        self.assertEqual(claims.evaluate("A", "B", "S", good, _interval(0.99, 0.97, 1.01)).verdict, claims.VERDICT_NONE)
        # Both inside the noise with opposite signs: no claim, not ISA-dependent.
        self.assertEqual(claims.evaluate("A", "B", "S", _interval(0.995, 0.98, 1.01), _interval(1.004, 0.99, 1.02)).verdict, claims.VERDICT_NONE)

    def test_claim_table_pairs_shipped_with_avx2(self):
        rows = []
        for flag, factor in (("Shipped", 0.9), ("MatchedAVX2", 0.92)):
            for label, scale in (("a", factor), ("b", 1.0)):
                values = [1000.0 * scale * (1 + 0.001 * i) for i in range(6)]
                rows.append({"table": f"scene=S | profile=N | flag_profile={flag} | T=1 | key=k{flag}", "table_error": "", "scene": "S", "profile": "N",
                             "flag_profile": flag, "T": 1, "label": label, "median_ns": values[3], "blocks_ns": dict(enumerate(values)), "thread_series": "p_cores_one_lp"})
        table = perf.claim_table(perf.ratios(pd.DataFrame(rows)))
        self.assertEqual(len(table), 1)
        self.assertEqual(table.iloc[0]["verdict"], claims.VERDICT_FASTER)

    def test_a_noisy_cell_is_no_claim(self):
        # M10 fix round 1 (TimingContract 9: only cells that passed the CV gate are reportable): the intervals alone would claim,
        # but one cell of the pair is noisy in one flag profile.
        good = _interval(0.90, 0.88, 0.92)
        claim = claims.evaluate("A", "B", "S", good, _interval(0.91, 0.89, 0.93), noisy=["B MatchedAVX2"])
        self.assertEqual(claim.verdict, claims.VERDICT_NONE)
        self.assertIn("noisy", claim.reason)
        self.assertIn("B MatchedAVX2", claim.reason)
        self.assertIn(claims.VERDICT_FASTER, claim.reason)   # what the intervals alone give is kept in the reason
        # A pair that is no claim anyway keeps its own reason.
        self.assertIn("includes 1", claims.evaluate("A", "B", "S", _interval(0.99, 0.97, 1.01), good, noisy=["A Shipped"]).reason)
        # Through the report's path: the cell's noisy mark reaches the claim table.
        rows = []
        for flag, factor in (("Shipped", 0.9), ("MatchedAVX2", 0.92)):
            for label, scale in (("a", factor), ("b", 1.0)):
                values = [1000.0 * scale * (1 + 0.001 * i) for i in range(6)]
                rows.append({"table": f"t{flag}", "table_error": "", "scene": "S", "profile": "N", "flag_profile": flag, "T": 1, "label": label,
                             "median_ns": values[3], "blocks_ns": dict(enumerate(values)), "thread_series": "p_cores_one_lp",
                             "noisy": flag == "Shipped" and label == "b"})
        ratio_rows = perf.ratios(pd.DataFrame(rows))
        self.assertEqual(sorted(zip(ratio_rows["flag_profile"], ratio_rows["b_noisy"])), [("MatchedAVX2", False), ("Shipped", True)])
        [row] = perf.claim_table(ratio_rows).to_dict("records")
        self.assertEqual((row["verdict"], row["noisy"]), (claims.VERDICT_NONE, "b Shipped"))

    def test_claim_table_matches_the_thread_series(self):
        # M10 fix round 1: the MatchedAVX2 counterpart has the same thread series (two T = 2 series: one P-core each vs an SMT pair).
        shipped = _interval(0.90, 0.88, 0.92)
        rows = [{"flag_profile": "Shipped", "scene": "S", "profile": "N", "T": 2, "thread_series": "p_cores_smt", "a": "a", "b": "b", "rho": 0.9,
                 "interval": shipped, "a_noisy": False, "b_noisy": False},
                {"flag_profile": "MatchedAVX2", "scene": "S", "profile": "N", "T": 2, "thread_series": "p_cores_one_lp", "a": "a", "b": "b", "rho": 1.1,
                 "interval": _interval(1.10, 1.08, 1.12), "a_noisy": False, "b_noisy": False},
                {"flag_profile": "MatchedAVX2", "scene": "S", "profile": "N", "T": 2, "thread_series": "p_cores_smt", "a": "a", "b": "b", "rho": 0.91,
                 "interval": _interval(0.91, 0.89, 0.93), "a_noisy": False, "b_noisy": False}]
        [row] = perf.claim_table(pd.DataFrame(rows)).to_dict("records")
        self.assertEqual((row["verdict"], row["rho_avx2"], row["thread_series"]), (claims.VERDICT_FASTER, 0.91, "p_cores_smt"))
        # Missing on both sides (an M8 manifest without threads.series) still pairs.
        for r in rows:
            r["thread_series"] = None
        del rows[1]
        self.assertEqual(perf.claim_table(pd.DataFrame(rows)).iloc[0]["verdict"], claims.VERDICT_FASTER)


class BootstrapTests(unittest.TestCase):
    def test_percentile_ci_is_reproducible_and_brackets_the_median(self):
        values = [10.0, 11.0, 9.5, 10.2, 10.8, 9.9, 10.4]
        first = bootstrap.percentile_ci(values, seed=7)
        self.assertEqual(first, bootstrap.percentile_ci(values, seed=7))
        self.assertLessEqual(first.low, first.estimate)
        self.assertGreaterEqual(first.high, first.estimate)
        self.assertEqual(first.resamples, 10000)
        self.assertIsNone(bootstrap.percentile_ci([1.0]))

    def test_block_ratio_uses_common_blocks_only(self):
        ratio = bootstrap.block_bootstrap_ratio({0: 90.0, 1: 91.0, 2: 92.0, 9: 1.0}, {0: 100.0, 1: 101.0, 2: 102.0}, seed=1)
        self.assertEqual(ratio.n, 3)
        self.assertAlmostEqual(ratio.estimate, 91.0 / 101.0)
        self.assertIsNone(bootstrap.block_bootstrap_ratio({0: 1.0}, {0: 2.0}))


class CvGateAndSentinelTests(unittest.TestCase):
    """03 M10: "CV 재실행 논리와 sentinel drift 플래그를 합성 결과로 검증한다"."""

    def test_cv_gate_reruns_then_marks_noisy(self):
        values = {"steady": [100.0, 100.5, 99.8, 100.2, 100.1],          # CV ~0.3 %: never gated
                  "settles": [100.0, 100.0, 100.0, 100.0, 106.0],        # CV ~2.6 %: one more block brings it under 2 %
                  "noisy": [100.0, 110.0, 95.0, 108.0, 92.0]}            # CV ~7.6 %: stays above after two blocks
        extra = {"settles": [[100.0, 100.0, 100.0, 100.0], [100.0]], "noisy": [[112.0], [90.0]]}
        calls = []

        def run_block(block, keys):
            calls.append((block, sorted(keys)))
            for key in keys:
                values[key].extend(extra[key][block])

        record = runner.run_cv_gate(lambda: {k: list(v) for k, v in values.items()}, run_block, 2.0, 2)
        self.assertEqual(calls, [(0, ["noisy", "settles"]), (1, ["noisy"])])
        self.assertEqual(record["noisy"], ["noisy"])
        self.assertGreater(record["initial_cv_pct"]["settles"], 2.0)
        self.assertLess(record["final_cv_pct"]["settles"], 2.0)
        self.assertEqual([b["cells"] for b in record["blocks"]], [["settles", "noisy"], ["noisy"]])
        self.assertEqual(runner.cells_over_threshold({"one": [5.0]}, 2.0), [])   # one value: no CV, never gated

    def test_sentinel_drift_flags_the_session(self):
        steady = runner.sentinel_drift([1000.0, 1008.0, 995.0])
        self.assertFalse(steady["flagged"])
        self.assertAlmostEqual(steady["drift_pct"], 0.8)
        drifted = runner.sentinel_drift([1000.0, 1012.0, 1031.0])
        self.assertTrue(drifted["flagged"])
        self.assertAlmostEqual(drifted["drift_pct"], 3.1)
        self.assertIn("drift 3.10 %", drifted["reason"])
        incomplete = runner.sentinel_drift([1000.0, None, 1001.0])
        self.assertTrue(incomplete["flagged"])
        self.assertFalse(incomplete["complete"])
        # M10 fix round 0: 1020 / 1000 - 1 is 0.020000000000000018 in floating point; the rule is "above 2 %".
        exact = runner.sentinel_drift([1000.0, 1010.0, 1020.0])
        self.assertFalse(exact["flagged"], exact)
        self.assertTrue(runner.sentinel_drift([1000.0, 1020.5])["flagged"])

    def test_the_runner_applies_the_gate_and_the_sentinel(self):
        """run_matrix end to end on synthetic results (the bench process and its verdict replaced): the sentinel runs at the
        start, the middle and the end, the noisy cell gets two extra blocks and the session validates against its schema."""
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            document = {"schema": "physicstest.matrix/1", "name": "synthetic", "description": "synthetic CV gate", "purpose": "perf", "repeats": 5, "block_seed": 3,
                        "cooldown_s": 0.0, "timeout_s": 10.0, "contaminated_retries": 0, "bench_args": [],
                        "targets": [{"label": "steady", "backend": "Null"}, {"label": "noisy", "backend": "NullDll"}], "profiles": ["Normalized"],
                        "scenes": [{"scenario": "M3.BoxPile", "params": {"N": "1000"}}], "threads": [1],
                        "sentinel": {"label": "steady", "scenario": "M3.BoxPile", "params": {"N": "1000"}, "drift_pct": 2.0}}
            path = root / "synthetic.json"
            common.write_json_atomic(path, document)
            matrix = matrix_module.Matrix(path=path, document=document, sha256=common.sha256_file(path))
            counters = {"noisy": 0, "sentinel": 0}
            noisy_values = [100.0, 115.0, 92.0, 108.0, 95.0, 111.0, 90.0]
            sentinel_values = [1000.0, 1010.0, 1030.0]   # drift 3 %: the session is flagged

            def fake_execute(exe, args, out_dir, timeout_s, *rest, **kw):
                out_dir.mkdir(parents=True, exist_ok=True)
                return {"command": [str(exe)] + list(args), "timed_out": False, "teardown_killed": False, "process_exit_code": 0, "spawn_error": "",
                        "exit_json_after_s": 0.1, "wall_s": 0.01}

            def fake_judge(out_dir, process, window):
                name = out_dir.name
                if name.startswith("s_"):
                    value = sentinel_values[counters["sentinel"]]
                    counters["sentinel"] += 1
                elif any("-Backend=NullDll" == a for a in process["command"]):
                    value = noisy_values[counters["noisy"]]
                    counters["noisy"] += 1
                else:
                    value = 1000.0 + counters["sentinel"]
                return {"exit_json": True, "schema_errors": [], "exit_json_valid": True, "exit_code": 0, "exit_status": "ok", "exit_category": "", "reason": "",
                        "outcome": "ok", "manifest_valid": True, "timing_valid_for_headline": True, "window_mean_ns": value, "window_count": 599}

            gate = envcheck.run_envcheck(allow_balanced=True, allow_busy=True, allow_concurrent=True, skip_idle=True)
            with mock.patch.object(runner, "execute", fake_execute), mock.patch.object(runner, "judge", fake_judge):
                session_dir, code = runner.run_matrix(matrix, pathlib.Path(sys.executable), None, root / "sessions", gate, log=lambda *_: None,
                                                      stamp_policy="allow", stamp_dir=root / "stamps")
            session = common.read_json(session_dir / "session.json")
            self.assertEqual(common.schema_errors(session, "session.schema.json"), [])
            self.assertEqual(code, 0)   # a noisy cell is ok (marked, not failed); the sentinel flags the session, not the exit code
            cells = {c["label"]: c for c in session["cells"]}
            self.assertEqual(cells["noisy"]["cv_gate"]["extra_blocks"], 2)
            self.assertTrue(cells["noisy"]["noisy"])
            self.assertEqual(len(cells["noisy"]["values_ns"]), 7)
            self.assertEqual(cells["steady"]["cv_gate"]["extra_blocks"], 0)
            self.assertFalse(cells["steady"]["noisy"])
            self.assertEqual([p["point"] for p in session["sentinel"]["points"]], ["start", "middle", "end"])
            self.assertTrue(session["sentinel"]["flagged"])
            self.assertAlmostEqual(session["sentinel"]["drift_pct"], 3.0)
            self.assertTrue(any("sentinel drift" in r for r in session["unofficial_reasons"]))
            self.assertIn("--allow-unstamped", session["bypasses"])
            self.assertEqual(sum(1 for r in session["runs"] if r.get("kind") == "sentinel"), 3)
            self.assertEqual([b.get("cv_gate", False) for b in session["blocks"]], [False] * 5 + [True, True])

    def test_session_unofficial_reasons(self):
        matrix = load_matrix("smoke")
        document = {"environment": {"power_plan_high_performance": True, "power_scheme": {}}, "bypasses": [], "stamps": {"policy": "require"},
                    "sentinel": {"declared": {"cell_id": "x"}, "flagged": False}}
        self.assertEqual(runner.session_unofficial_reasons(document, matrix, 5), [])
        document["sentinel"]["flagged"] = True
        document["sentinel"]["reason"] = "sentinel drift 3.00 % > 2 %"
        self.assertEqual(runner.session_unofficial_reasons(document, matrix, 5), ["sentinel drift 3.00 % > 2 %"])


class StampCheckTests(unittest.TestCase):
    """The runner's recomputation of a stamp (stamps.py) on synthetic files; the real bench's stamps are in test_integration."""

    def _setup(self, root: pathlib.Path):
        exe = root / "bin" / "PhysicsTestBench.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"MZ exe v1")
        (root / "bin" / "PtBackend_Null.dll").write_bytes(b"MZ dll v1")
        common.write_json_atomic(root / "bin" / stamps.CATALOG_FILE, {"schema": "physicstest.backends/1", "preset": "t", "backends": [
            {"name": "Jolt", "kind": "static", "file": None}, {"name": "NullDll", "kind": "dll", "file": "PtBackend_Null.dll"}]})
        config = root / "Config"
        (config / "Profiles" / "Backend").mkdir(parents=True)
        (config / "Golden").mkdir(parents=True)
        files = {"base_ini": config / "BasePhysicsTest.ini", "profile_ini": config / "Profiles" / "Normalized.ini",
                 "variant_ini": config / "Profiles" / "Backend" / "Jolt.Default.ini", "tolerances": config / "Golden" / "tolerances.json"}
        for role, path in files.items():
            path.write_text(f"[{role}]\nA=1\n", encoding="utf-8")
        return exe, config, files

    def _stamp(self, root, exe, config, files, backend="Jolt", variant="", module_sha="", overrides=(), pending=0, valid=True, roles=None):
        key = {"bench_exe_sha256": common.sha256_file(exe), "backend_module_sha256": module_sha, "variant_sha256": "c" * 64, "physics_sha256": "d" * 64,
               "tolerances_sha256": common.sha256_file(files["tolerances"]), "gate_suite_version": 9, "config_overrides": list(overrides)}
        sha = stamps.key_sha256(key)
        document = {"schema": "physicstest.stamp/1", "stamp_key_sha256": sha, "valid": valid and pending == 0, "result": "pass" if valid else "fail",
                    "invalid_reason": "" if valid and pending == 0 else "x", "pending_sign_offs": pending, "summary": {"gates": 12, "failed": 0 if valid else 1, "skipped": 0},
                    "key_canonical": stamps.canonical_key(key), "key": key, "target": {"backend": backend, "variant_requested": variant, "variant": "Default" if "variant_ini" in (roles or files) else "", "profile": "Normalized"},
                    "build": {"exe": exe.name, "exe_path": str(exe), "preset": "t", "flag_profile": "Shipped", "configuration": "Release", "git_sha": "g", "git_dirty": False},
                    "backend_module": {"kind": "dll" if module_sha else "static", "file": "PtBackend_Null.dll" if module_sha else None, "path": None},
                    "config_dir": str(config), "inputs": [{"role": r, "path": str(p), "sha256": common.sha256_file(p)} for r, p in files.items() if r in (roles or files)],
                    "gates": [{"id": "G1", "result": "pass", "reason": "", "failures": []}], "conformance_dir": "", "command_line": "", "utc": "2026-10-22T00:00:00Z"}
        self.assertEqual(common.schema_errors(document, "stamp.schema.json"), [])
        common.write_json_atomic(root / "stamps" / f"{sha}.json", document)
        return document

    def _cell(self, backend="Jolt", variant="", target_args=()):
        return matrix_module.Cell(label="x", backend=backend, variant=variant, profile="Normalized", scenario="S", params=(), threads=1, frames=0,
                                  target_args=tuple(target_args))

    def _check(self, root, exe, config, cell):
        result = stamps.check_cells([cell], [f"-ConfigDir={config}"], exe, root / "stamps")
        return result["cells"][cell.key]

    def test_valid_then_each_invalidation(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = self._setup(root)
            self._stamp(root, exe, config, files)
            self.assertEqual(self._check(root, exe, config, self._cell())["state"], "valid")
            # A key of the variant ini changes: stale, the file named.
            files["variant_ini"].write_text("[variant_ini]\nA=2\n", encoding="utf-8")
            result = self._check(root, exe, config, self._cell())
            self.assertEqual(result["state"], "stale")
            self.assertIn("Jolt.Default.ini", result["reason"])
            files["variant_ini"].write_text("[variant_ini]\nA=1\n", encoding="utf-8")
            self.assertEqual(self._check(root, exe, config, self._cell())["state"], "valid")
            # tolerances.json changes: stale.
            files["tolerances"].write_text("[tolerances]\nA=2\n", encoding="utf-8")
            self.assertIn("tolerances", self._check(root, exe, config, self._cell())["reason"])
            files["tolerances"].write_text("[tolerances]\nA=1\n", encoding="utf-8")
            # A rebuilt exe: stale.
            exe.write_bytes(b"MZ exe v2")
            self.assertIn("bench exe changed", self._check(root, exe, config, self._cell())["reason"])

    def test_dll_backend_and_overrides(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = self._setup(root)
            dll = root / "bin" / "PtBackend_Null.dll"
            self._stamp(root, exe, config, files, backend="NullDll", module_sha=common.sha256_file(dll), roles=("base_ini", "profile_ini", "tolerances"))
            cell = self._cell(backend="NullDll")
            self.assertEqual(self._check(root, exe, config, cell)["state"], "valid")
            dll.write_bytes(b"MZ dll v2")   # rebuilt
            result = self._check(root, exe, config, cell)
            self.assertEqual(result["state"], "stale")
            self.assertIn("PtBackend_Null.dll changed", result["reason"])
            # -ini: switches are part of the key: a target with an engine key needs its own stamp.
            override = self._cell(target_args=["-ini:PhysicsTest:[PhysicsTest.Backend.Jolt]:X=1"])
            self.assertEqual(self._check(root, exe, config, override)["state"], "missing")
            self._stamp(root, exe, config, files, overrides=stamps.config_overrides(override.target_args))
            self.assertEqual(self._check(root, exe, config, override)["state"], "valid")

    def test_invalid_stamps(self):
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = self._setup(root)
            self._stamp(root, exe, config, files, pending=1)
            result = self._check(root, exe, config, self._cell())
            self.assertEqual(result["state"], "invalid")
            self.assertIn("not valid", result["reason"])

    def test_the_state_comes_with_the_reason(self):
        # M10 fix round 1: verify_stamp returns (state, reason); the state is no longer read back from the reason's words. An
        # edited stamp (its key no longer hashes to its name) cannot vouch for anything: invalid, not stale.
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = self._setup(root)
            document = self._stamp(root, exe, config, files)
            path = root / "stamps" / f"{document['stamp_key_sha256']}.json"
            cache = stamps.StampCache(root / "stamps")
            [(stamp_path, loaded, errors)] = cache.stamps
            catalog = stamps.load_catalog(exe)
            self.assertEqual(stamps.verify_stamp(stamp_path, loaded, errors, exe, common.sha256_file(exe), catalog, "Jolt", cache), ("valid", ""))
            state, reason = stamps.verify_stamp(stamp_path, loaded, errors, exe, "0" * 64, catalog, "Jolt", cache)
            self.assertEqual(state, "stale")
            self.assertIn("rebuilt", reason)
            document["key"]["physics_sha256"] = "e" * 64   # edited in place, name and stamp_key_sha256 kept
            common.write_json_atomic(path, document)
            result = self._check(root, exe, config, self._cell())
            self.assertEqual(result["state"], "invalid")
            self.assertIn("edited or renamed", result["reason"])

    def test_config_overrides_match_the_bench(self):
        self.assertEqual(stamps.config_overrides(["-HostCheck=record", "-ini:A:[S]:K=1", '-ExecCmds="p.X 1, p.Y 2"', "/INI:B:[S]:K=2"]),
                         sorted(["-ini:A:[S]:K=1", "-ExecCmds=p.X 1, p.Y 2", "-INI:B:[S]:K=2"]))
        self.assertEqual(stamps.canonical_key({"b": 1, "a": [], "T": "x"}), '{"T":"x","a":[],"b":1}')


class ComparabilityTests(unittest.TestCase):
    MANIFEST = {"scene": {"scene_sha256": "a" * 64, "desc_sha256": "b" * 64}, "profile": {"physics_sha256": "c" * 64, "mass_rule": "UEBodyUtils"},
                "build": {"flag_profile": "Shipped"}, "run": {"tick_mode": "FixedStep", "dt_s": 0.01666666753590107, "sync_policy": "s", "mode": "Bench",
                "instrumentation": []}, "threads": {"T": 1, "series": "p_cores_one_lp"}, "harness": {"git_sha": "g", "dirty": False}}

    def test_key_changes_with_every_field(self):
        base = comparability.key_from_manifest(self.MANIFEST)
        self.assertRegex(base, "^[0-9a-f]{64}$")
        self.assertEqual(comparability.dt_bits(0.01666666753590107), "3f91111120000000")
        for group, field, value in (("scene", "desc_sha256", "e" * 64), ("threads", "series", "p_cores_smt"), ("threads", "T", 2), ("build", "flag_profile", "MatchedAVX2"),
                                    ("harness", "dirty", True), ("run", "dt_s", 1.0 / 30.0)):
            changed = json.loads(json.dumps(self.MANIFEST))
            changed[group][field] = value
            self.assertNotEqual(comparability.key_from_manifest(changed), base, (group, field))
        self.assertIsNone(comparability.key_from_manifest({"scene": {"desc_sha256": None}}))

    def test_a_key_that_does_not_match_its_fields_is_rejected(self):
        reasons = validate.reject_reasons(_row(comparability_key="1" * 64, comparability_key_recomputed="2" * 64))
        self.assertTrue(any("does not match the manifest's fields" in r for r in reasons), reasons)
        unstamped = _row(stamp_valid=False, stamp_reason="no stamp for this key")
        self.assertTrue(any("no valid conformance stamp" in r for r in validate.reject_reasons(unstamped)))
        self.assertEqual(validate.reject_reasons(unstamped, allow_unstamped=True), [])

    def test_the_binary_composition_control_splits_series_by_preset(self):
        rows = [_row(run_key="a", comparability_key="k" * 64, preset="test"), _row(run_key="b", comparability_key="k" * 64, preset="test-jolt"),
                _row(run_key="c", comparability_key="k" * 64, preset="test", label="bullet.Default")]
        classified = validate.classify(pd.DataFrame(rows))
        self.assertEqual(sorted(classified["series"]), ["bullet.Default", "jolt.Default@test", "jolt.Default@test-jolt"])
        [(name, group, error)] = validate.tables(classified)
        self.assertEqual(error, "")
        self.assertIn("key=kkkkkkkk", name)


class ViewerManifestTests(unittest.TestCase):
    def test_a_viewer_manifest_is_rejected_by_its_schema(self):
        # M10 (M8 review): the viewer's capture manifest (physicstest.viewer/1, top-level mode viz) saved as manifest.json.
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            run = pathlib.Path(directory) / "viewer"
            run.mkdir()
            common.write_json_atomic(run / "manifest.json", {"schema": "physicstest.viewer/1", "mode": "viz", "backend": "Jolt"})
            common.write_json_atomic(run / "exit.json", {"schema": "physicstest.exit/1", "exit_code": 0, "meaning": "ok", "status": "ok", "category": "", "reason": "",
                                                        "run_id": "x", "utc": "2026-10-22T00:00:00Z", "pid": 1, "terminate_process": False, "outputs": []})
            classified = validate.classify(load.load([run]).runs)
            reasons = classified.iloc[0]["reject_reasons"]
            self.assertFalse(classified.iloc[0]["accepted"])
            self.assertTrue(any("not a run manifest (schema physicstest.viewer/1, mode viz)" in r for r in reasons), reasons)
        # The bench's own Viz manifest, any case.
        self.assertTrue(any("viz-mode" in r for r in validate.reject_reasons(_row(mode="viz"))))


class SpeedupTests(unittest.TestCase):
    def test_speedup_and_efficiency(self):
        # M8 review: S(T) / E(T) had no unit test. t(1) = 10, t(2) = 6, t(4) = 4: S 1.667 / 2.5, E 0.833 / 0.625.
        rows = [{"table_error": "", "label": "jolt.Default", "profile": "N", "flag_profile": "Shipped", "scene": "S", "T": t, "median_ns": v, "cpu_eff": e}
                for t, v, e in ((1, 10.0, 1.0), (2, 6.0, 0.97), (4, 4.0, 0.9))]
        table = perf.speedup(pd.DataFrame(rows)).set_index("T")
        self.assertAlmostEqual(table.loc[2, "S"], 10.0 / 6.0)
        self.assertAlmostEqual(table.loc[2, "E"], 10.0 / 12.0)
        self.assertAlmostEqual(table.loc[4, "S"], 2.5)
        self.assertAlmostEqual(table.loc[4, "E"], 0.625)
        self.assertAlmostEqual(table.loc[4, "cpu_eff"], 0.9)


class CliPathTests(unittest.TestCase):
    def test_analysis_commands_refuse_a_missing_path(self):
        # M10 (M8 review): load / validate / perf / report / determinism exit 2 on a path that does not exist (no empty report).
        missing = str(pathlib.Path(tempfile.gettempdir()) / "ptbench_no_such_session_dir_m10")
        for argv in (["load", missing], ["validate", missing], ["perf", missing], ["report", missing], ["determinism", missing], ["behavior", missing]):
            with self.subTest(argv=argv), mock.patch("sys.stdout", new_callable=io.StringIO) as stdout:
                self.assertEqual(__main__.main(argv), 2, argv)
                self.assertIn("no such path", stdout.getvalue())
        self.assertFalse(pathlib.Path(missing).exists())   # report created nothing

    def test_behavior_on_a_directory_without_runs(self):
        # M10 fix round 0: the M9 behavior command ended in a KeyError traceback on an empty directory.
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory, mock.patch("sys.stdout", new_callable=io.StringIO) as stdout:
            self.assertEqual(__main__.main(["behavior", directory]), 2)
            self.assertIn("no run found", stdout.getvalue())

class CompositionControlTests(unittest.TestCase):
    def test_test_engine_against_test(self):
        # 02 14.3: the single-SDK exe's series against the test exe's in one table (label@preset), diff and an independent CI.
        rows = []
        for label, preset, scale in (("jolt.Default@test", "test", 1.0), ("jolt.Default@test-jolt", "test-jolt", 1.005), ("physx.UE4Compat@test", "test", 0.6),
                                     ("physx.UE4Compat@test-physx", "test-physx", 0.63)):
            values = [1000.0 * scale * (1 + 0.0005 * i) for i in range(5)]
            rows.append({"table": "scene=S | profile=N | flag_profile=Shipped | T=1 | key=k", "table_error": "", "scene": "S", "profile": "N", "T": 1, "label": label,
                         "preset": preset, "n_runs": 5, "median_ns": values[2], "values_ns": values})
        table = perf.composition_table(pd.DataFrame(rows)).set_index("label")
        self.assertAlmostEqual(table.loc["jolt.Default", "diff_pct"], 0.5, places=6)
        self.assertTrue(table.loc["jolt.Default", "within_1pct"])
        self.assertAlmostEqual(table.loc["physx.UE4Compat", "diff_pct"], 5.0, places=6)
        self.assertFalse(table.loc["physx.UE4Compat", "within_1pct"])
        self.assertLess(table.loc["physx.UE4Compat", "ci_low_pct"], table.loc["physx.UE4Compat", "ci_high_pct"])

    def test_control_series_stay_out_of_the_engine_comparison(self):
        # A table with jolt from test and test-jolt (label@preset) and physx from test only: the ratios compare jolt.Default (the
        # test exe's series, base label) with physx.UE4Compat; jolt.Default@test-jolt is the composition control only. Blocks are
        # per session: the two sessions' repeat 0 never pair.
        rows = []
        for label, preset, session, scale in (("jolt.Default@test", "test", "s1", 1.0), ("jolt.Default@test-jolt", "test-jolt", "s2", 1.0),
                                              ("physx.UE4Compat", "test", "s1", 0.6)):
            values = [1000.0 * scale * (1 + 0.001 * i) for i in range(5)]
            rows.append({"table": "t", "table_error": "", "scene": "S", "profile": "N", "flag_profile": "Shipped", "T": 1, "label": label, "preset": preset,
                         "median_ns": values[2], "values_ns": values, "n_runs": 5, "thread_series": "p_cores_one_lp",
                         "blocks_ns": {f"{session}#{i:04d}": v for i, v in enumerate(values)}})
        table = perf.ratios(pd.DataFrame(rows))
        self.assertEqual(list(zip(table["a"], table["b"])), [("jolt.Default", "physx.UE4Compat")])
        self.assertEqual(int(table.iloc[0]["blocks"]), 5)
        self.assertIsNone(perf.comparison_label("bullet.Default@test-bullet"))
        self.assertEqual(perf.comparison_label("bullet.Default@test"), "bullet.Default")
        self.assertIsNone(bootstrap.block_bootstrap_ratio({"s1#0000": 1.0, "s1#0001": 2.0}, {"s2#0000": 1.0, "s2#0001": 2.0}))

    def test_control_is_decided_by_the_build_content(self):
        # M10 fix round 1: harness.sdks (build_sdks) decides, so user presets (CMakeUserPresets.json.example: test-local,
        # test-jolt-local) work: the single-SDK exe's series is the control, the exe with the most SDKs is its reference.
        rows = []
        for label, preset, sdks, session, scale in (("jolt.Default@test-local", "test-local", "Jolt,Bullet,PhysX", "s1", 1.0),
                                                    ("jolt.Default@test-jolt-local", "test-jolt-local", "Jolt", "s2", 1.01),
                                                    ("physx.UE4Compat", "test-local", "Jolt,Bullet,PhysX", "s1", 0.6)):
            values = [1000.0 * scale * (1 + 0.001 * i) for i in range(5)]
            rows.append({"table": "t", "table_error": "", "scene": "S", "profile": "N", "flag_profile": "Shipped", "T": 1, "label": label, "preset": preset,
                         "build_sdks": sdks, "median_ns": values[2], "values_ns": values, "n_runs": 5, "thread_series": "p_cores_one_lp",
                         "blocks_ns": {f"{session}#{i:04d}": v for i, v in enumerate(values)}})
        ratio_rows = perf.ratios(pd.DataFrame(rows))
        self.assertEqual(list(zip(ratio_rows["a"], ratio_rows["b"])), [("jolt.Default", "physx.UE4Compat")])
        [row] = perf.composition_table(pd.DataFrame(rows)).to_dict("records")
        self.assertEqual((row["label"], row["preset"], row["reference_preset"]), ("jolt.Default", "test-jolt-local", "test-local"))
        self.assertAlmostEqual(row["diff_pct"], 1.0, places=6)
        # The content wins over the name: a single-SDK build under a preset called "test" is the control, a full build called
        # "test-jolt" is not.
        self.assertTrue(perf.is_control({"label": "x@test", "preset": "test", "build_sdks": "Jolt"}))
        self.assertFalse(perf.is_control({"label": "x@test-jolt", "preset": "test-jolt", "build_sdks": "Jolt,PhysX"}))
        self.assertTrue(perf.is_control({"label": "x@test-physx-local", "preset": "test-physx-local", "build_sdks": None}))   # older manifest: the name

    def test_target_selection(self):
        matrix = load_matrix("smoke")
        selected = matrix_module.select_targets(matrix, ["physx.UE4Compat"])
        self.assertEqual({c.label for c in selected.cells()}, {"physx.UE4Compat"})
        self.assertNotIn("sentinel", selected.document)        # the jolt sentinel is outside the selection
        self.assertIn("sentinel", matrix.document)              # the loaded matrix is untouched
        self.assertIs(matrix_module.select_targets(matrix, None), matrix)
        with self.assertRaises(matrix_module.MatrixError):
            matrix_module.select_targets(matrix, ["no.such"])



class FixRound0Tests(unittest.TestCase):
    """M10 fix round 0: the noisy mark per session, the stamp policy per matrix purpose, the stamp reason per exe and --prune."""

    def test_noisy_mark_belongs_to_its_own_session(self):
        # Two sessions of one matrix share every cell key (test and test-avx2 both run smoke): the CV gate marked the cell noisy in
        # the Shipped session only, so the MatchedAVX2 table must not show it noisy.
        rows = [_row(run_key=f"a{i}", session_dir="S1", cell_key="abc", win_headline_mean=1000.0 + i) for i in range(3)]
        rows += [_row(run_key=f"b{i}", session_dir="S2", cell_key="abc", flag_profile="MatchedAVX2", win_headline_mean=1000.0 + i) for i in range(3)]
        sessions = {"S1": {"cells": [{"key": "abc", "noisy": True}]}, "S2": {"cells": [{"key": "abc", "noisy": False}]}}
        cells = perf.cell_stats(validate.classify(pd.DataFrame(rows)), pd.DataFrame(columns=["run_key", "frame", "frame_ns"]), sessions=sessions)
        self.assertEqual(dict(zip(cells["flag_profile"], cells["noisy"])), {"Shipped": True, "MatchedAVX2": False})
        lines = [line for line in report.section_cv(cells) if line.startswith("| jolt.Default")]
        self.assertEqual(sum("**noisy**" in line for line in lines), 1, lines)
        # Without a session directory (plain run directories) nothing is noisy.
        cells = perf.cell_stats(validate.classify(pd.DataFrame([dict(r, session_dir=None) for r in rows])), pd.DataFrame(columns=["run_key", "frame", "frame_ns"]),
                                sessions=sessions)
        self.assertFalse(cells["noisy"].any())

    def test_stamp_policy_follows_the_matrix_purpose(self):
        self.assertEqual(runner.stamp_policy_of(load_matrix("smoke")), "require")
        self.assertEqual(runner.stamp_policy_of(load_matrix("determinism")), "record")
        self.assertEqual(runner.stamp_policy_of(load_matrix("behavior")), "record")
        matrix = load_matrix("determinism")
        declared = matrix_module.Matrix(path=matrix.path, document=dict(matrix.document, stamps="require"), sha256=matrix.sha256)
        self.assertEqual(runner.stamp_policy_of(declared), "require")
        self.assertEqual(common.schema_errors(declared.document, "matrix.schema.json"), [])
        self.assertNotEqual(common.schema_errors(dict(matrix.document, stamps="maybe"), "matrix.schema.json"), [])

    def _determinism_session(self, root: pathlib.Path, stamp_policy):
        document = {"schema": "physicstest.matrix/1", "name": "syndet", "description": "synthetic determinism", "purpose": "determinism", "repeats": 2,
                    "block_seed": 5, "cooldown_s": 0.0, "timeout_s": 10.0, "contaminated_retries": 0, "bench_args": [],
                    "targets": [{"label": "jolt.Default", "backend": "Jolt", "variant": "Default"}], "profiles": ["Normalized"],
                    "scenes": [{"scenario": "M3.BoxPile", "params": {"N": "1000"}}], "threads": [1], "cv_gate": False}
        path = root / "syndet.json"
        common.write_json_atomic(path, document)
        matrix = matrix_module.Matrix(path=path, document=document, sha256=common.sha256_file(path))

        def fake_execute(exe, args, out_dir, timeout_s, *rest, **kw):
            out_dir.mkdir(parents=True, exist_ok=True)
            return {"command": [str(exe)] + list(args), "timed_out": False, "teardown_killed": False, "process_exit_code": 0, "spawn_error": "",
                    "exit_json_after_s": 0.1, "wall_s": 0.01}

        def fake_judge(out_dir, process, window):
            return {"exit_json": True, "schema_errors": [], "exit_json_valid": True, "exit_code": 0, "exit_status": "ok", "exit_category": "", "reason": "",
                    "outcome": "ok", "manifest_valid": True, "timing_valid_for_headline": False, "window_mean_ns": None, "window_count": 599}

        gate = envcheck.run_envcheck(allow_balanced=True, allow_busy=True, allow_concurrent=True, skip_idle=True)
        lines = []
        with mock.patch.object(runner, "execute", fake_execute), mock.patch.object(runner, "judge", fake_judge):
            session_dir, code = runner.run_matrix(matrix, pathlib.Path(sys.executable), None, root / "sessions", gate, log=lambda *parts: lines.append(" ".join(map(str, parts))),
                                                  stamp_policy=stamp_policy, stamp_dir=root / "stamps", session_name=f"s-{stamp_policy}")
        return common.read_json(session_dir / "session.json"), code, "\n".join(lines)

    def test_determinism_session_records_stamps_instead_of_refusing(self):
        # M10 fix round 0: R9 is about reportable timing; a determinism (or behaviour) session without stamps runs and records the
        # stamp state, while --require-stamps (or a perf matrix) still refuses before any process.
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            session, code, text = self._determinism_session(root, None)
            self.assertEqual(common.schema_errors(session, "session.schema.json"), [])
            self.assertEqual((code, session["status"], session["stamps"]["policy"]), (0, "complete", "record"))
            self.assertEqual(len(session["stamps"]["unstamped_cells"]), 1)
            self.assertEqual(session["cells"][0]["stamp"]["state"], "missing")
            self.assertNotIn("--allow-unstamped", session["bypasses"])
            self.assertTrue(any("recorded, not required" in r for r in session["unofficial_reasons"]), session["unofficial_reasons"])
            self.assertIn("CV gate off (the matrix sets cv_gate false)", text)
            session, code, text = self._determinism_session(root, "require")
            self.assertEqual((code, session["status"], session["runs"]), (3, "refused", []))
            self.assertIn("no valid conformance stamp", text)
            self.assertIn('--matrix "', text)   # a matrix outside Config/Matrices is named by its path

    def test_stamp_reason_comes_from_this_exe(self):
        helper = StampCheckTests()
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = helper._setup(root)
            helper._stamp(root, exe, config, files)
            # Another preset's exe (other path, other bytes) with no stamp of its own: missing for this exe, never "rebuilt".
            other = root / "bin2" / "PhysicsTestBench.exe"
            other.parent.mkdir()
            other.write_bytes(b"MZ another preset")
            (root / "bin2" / stamps.CATALOG_FILE).write_bytes((root / "bin" / stamps.CATALOG_FILE).read_bytes())
            result = stamps.check_cells([helper._cell()], [f"-ConfigDir={config}"], other, root / "stamps")["cells"][helper._cell().key]
            self.assertEqual(result["state"], "missing")
            self.assertIn("no stamp for this exe", result["reason"])
            self.assertIn("1 stamp(s)", result["reason"])
            # The stamped exe rebuilt in place: stale, "rebuilt".
            exe.write_bytes(b"MZ exe v2")
            result = helper._check(root, exe, config, helper._cell())
            self.assertEqual(result["state"], "stale")
            self.assertIn("bench exe changed", result["reason"])

    def test_prune_removes_stamps_of_rebuilt_binaries_only(self):
        helper = StampCheckTests()
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as directory:
            root = pathlib.Path(directory)
            exe, config, files = helper._setup(root)
            kept = helper._stamp(root, exe, config, files)
            files["variant_ini"].write_text("[variant_ini]\nA=2\n", encoding="utf-8")   # stale for a configuration file: kept
            self.assertEqual(stamps.prune(root / "stamps", log=lambda *_: None), 0)
            self.assertTrue((root / "stamps" / f"{kept['stamp_key_sha256']}.json").is_file())
            exe.write_bytes(b"MZ exe v2")                                                  # rebuilt: can never be valid again
            self.assertEqual(stamps.prune(root / "stamps", dry_run=True, log=lambda *_: None), 1)
            self.assertTrue((root / "stamps" / f"{kept['stamp_key_sha256']}.json").is_file())
            self.assertEqual(stamps.prune(root / "stamps", log=lambda *_: None), 1)
            self.assertFalse((root / "stamps" / f"{kept['stamp_key_sha256']}.json").exists())


if __name__ == "__main__":
    unittest.main()
