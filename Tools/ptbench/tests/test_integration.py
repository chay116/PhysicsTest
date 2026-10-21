"""Integration tests of ptbench with a real PhysicsTestBench.exe (CTest PhysicsTest.Py.Integration, 03_WorkPlan M8 "테스트로 확인한다").

    set PT_BENCH_EXE=D:\\PhysicsTest\\out\\test\\bin\\PhysicsTestBench.exe
    set PT_TEST_OUT=D:\\PhysicsTest\\out\\test\\_py\\Integration
    python -I -m unittest -v ptbench.tests.test_integration

  - a refused environment gate starts no run (session.json status refused, exit code 3); the Balanced plan is injected, so
    this runs on a High performance machine too
  - a run that dies half-way (-InjectSilentExit: raw exit code 0, no exit.json) is a failed run in session.json / index.csv
    and is rejected by validate, never judged by its raw exit code; a timed-out run likewise; its parquet rows keep their
    types (missing flags are missing, not the text "nan")
  - exit code 4 is retried in fresh processes and the cell is 'contaminated' when every attempt ends with 4 (never skipped)
  - the matrix's, a target's and --bench-arg switches reach the bench (read back from the manifests), and a repeated
    switch key is refused before a run starts
  - a viz-mode manifest is rejected; Shipped + MatchedAVX2 in one table and two desc_sha256 values in one table are errors
    (the report's handling of a refused table is in test_unit: these PreRoll=0 runs are never accepted)
  - the DET1 matrix of -Gate=G8 runs, and the first divergent frame of a copy with one run's hashes changed on purpose
  - a short session end to end: session.json and every manifest validate, the report is written with its UNOFFICIAL head,
    a report over the session's run directories alone still marks the power plan, and `ptbench perf` with a captured
    (redirected) stdout ends normally
The runner tests pass the environment-gate bypasses (CTest runs in parallel on a desktop in use) and -HostCheck=record.
"""
from __future__ import annotations

import csv
import json
import os
import pathlib
import shutil
import subprocess
import sys
import unittest

import pandas as pd

from ptbench import common, envcheck, runner
from ptbench.analysis import determinism, load, report, validate
from ptbench.matrix import Matrix, MatrixError

EXE = os.environ.get("PT_BENCH_EXE", "")
OUT = pathlib.Path(os.environ.get("PT_TEST_OUT", "")) if os.environ.get("PT_TEST_OUT") else None


def _gate() -> dict:
    return envcheck.run_envcheck(allow_balanced=True, allow_busy=True, allow_concurrent=True, skip_idle=True)


def _matrix(directory: pathlib.Path, name: str, scenes: list, bench_args: list, repeats: int = 1, retries: int = 2, timeout: float = 120.0,
            targets: list | None = None) -> Matrix:
    document = {
        "schema": "physicstest.matrix/1", "name": name, "description": "integration test", "purpose": "perf", "repeats": repeats, "block_seed": 42,
        "cooldown_s": 0.0, "timeout_s": timeout, "contaminated_retries": retries, "bench_args": bench_args,
        "targets": targets or [{"label": "null", "backend": "Null"}], "profiles": ["Normalized"], "scenes": scenes, "threads": [1],
    }
    path = directory / f"{name}.json"
    common.write_json_atomic(path, document)
    errors = common.schema_errors(document, "matrix.schema.json")
    if errors:
        raise AssertionError(errors)
    return Matrix(path=path, document=document, sha256=common.sha256_file(path))


def _bench(*args: str) -> int:
    return subprocess.run([EXE, *args], capture_output=True, timeout=300, check=False).returncode


@unittest.skipUnless(EXE and OUT, "PT_BENCH_EXE and PT_TEST_OUT are required (CTest PhysicsTest.Py.Integration sets them)")
class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = OUT
        shutil.rmtree(cls.out, ignore_errors=True)
        cls.out.mkdir(parents=True)
        cls.exe = pathlib.Path(EXE)

    def _session(self, matrix: Matrix) -> tuple[pathlib.Path, int, dict]:
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), log=lambda *_: None)
        document = common.read_json(session_dir / "session.json")
        self.assertEqual(common.schema_errors(document, "session.schema.json"), [])
        return session_dir, code, document

    def test_died_half_way_is_a_failure_not_the_raw_code(self):
        matrix = _matrix(self.out, "silent", [{"scenario": "S00.FreeFall"}], ["-HostCheck=record", "-PreRoll=0", "-InjectSilentExit=2"])
        session_dir, code, document = self._session(matrix)
        self.assertEqual(code, 1)
        [run] = document["runs"]
        self.assertEqual(run["process_exit_code"], 0)          # the raw code says "ok" ...
        self.assertFalse(run["exit_json"])
        self.assertEqual(run["outcome"], "no_exit_json")       # ... the runner says failed
        self.assertEqual(document["cells"][0]["state"], "failed")
        self.assertEqual(document["summary"]["no_exit_json"], 1)
        with open(session_dir / "index.csv", encoding="utf-8", newline="") as stream:
            [row] = list(csv.DictReader(stream))
        self.assertEqual((row["process_exit_code"], row["exit_json"], row["outcome"]), ("0", "False", "no_exit_json"))
        classified = validate.classify(load.load([session_dir]).runs)
        reasons = classified.iloc[0]["reject_reasons"]
        self.assertFalse(classified.iloc[0]["accepted"])
        self.assertTrue(any("exit.json missing" in r and "raw exit code 0 is not trusted" in r for r in reasons), reasons)
        # parquet: the died run next to a complete one keeps typed columns (M8 review: no 'True' / 'nan' strings)
        ok_dir = self.out / "silent_ok"
        self.assertEqual(_bench("-Backend=Null", "-Scenario=S00.FreeFall", "-Frames=5", "-HostCheck=record", "-PreRoll=0", f"-Out={ok_dir}"), 0)
        loaded = load.load([session_dir, ok_dir])
        load.write_parquet(loaded, self.out / "silent_parquet")
        back = pd.read_parquet(self.out / "silent_parquet" / "runs.parquet")
        for column in ("exit_json_valid", "manifest_valid", "timing_valid_for_headline", "contaminated", "power_scheme_high_performance"):
            self.assertEqual(str(back[column].dtype), "boolean", column)
            self.assertEqual(int(back[column].isna().sum()), 1, column)   # the died run: missing, not the text "nan"
        for column in ("exit_code", "process_exit_code", "T", "attempt"):
            self.assertEqual(str(back[column].dtype), "Int64", column)
        self.assertEqual(sorted(back["exit_json"].tolist()), [False, True])

    def test_refused_gate_runs_nothing(self):
        balanced = "Power Scheme GUID: 381b4222-f694-41f0-9685-ff5bb260df2e  (Balanced)"
        gate = envcheck.run_envcheck(allow_balanced=False, allow_concurrent=True, skip_idle=True, scheme_reader=lambda: envcheck.parse_power_scheme(balanced))
        self.assertTrue(gate["refused"])
        matrix = _matrix(self.out, "refused", [{"scenario": "S00.FreeFall"}], ["-HostCheck=record"])
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", gate, log=lambda *_: None)
        document = common.read_json(session_dir / "session.json")
        self.assertEqual((code, document["status"], document["runs"]), (3, "refused", []))
        self.assertEqual(common.schema_errors(document, "session.schema.json"), [])
        self.assertFalse((session_dir / "runs").exists())

    def test_timeout_is_a_failure(self):
        matrix = _matrix(self.out, "timeout", [{"scenario": "S00.FreeFall"}], ["-HostCheck=record", "-PreRoll=20"], timeout=1.5)
        _, code, document = self._session(matrix)
        self.assertEqual(code, 1)
        [run] = document["runs"]
        self.assertTrue(run["timed_out"])
        self.assertEqual(run["outcome"], "timeout")
        self.assertFalse(run["exit_json"])

    def test_exit_4_is_retried_then_contaminated(self):
        # -ContaminationPct=0: any foreign cycle on the run's core contaminates it (exit 4), so every attempt ends with 4.
        matrix = _matrix(self.out, "contaminated", [{"scenario": "S00.FreeFall", "frames": 20}], ["-HostCheck=enforce", "-PreRoll=0.2", "-ContaminationPct=0"])
        _, code, document = self._session(matrix)
        outcomes = [r["outcome"] for r in document["runs"]]
        # No skip (M8 review: a skipped unittest still passes in CTest). The first attempt must be contaminated, or nothing
        # of the retry path was exercised.
        self.assertEqual(outcomes[0], "contaminated", f"no foreign cycle at all on the run's core: exit 4 could not be forced ({outcomes})")
        self.assertEqual([r["attempt"] for r in document["runs"]], list(range(len(outcomes))))
        if outcomes[-1] == "ok":
            # A retry that ended ok (very rare at 0 %): the retry path ran in fresh processes and the repeat is ok.
            self.assertEqual(outcomes[:-1], ["contaminated"] * (len(outcomes) - 1))
            self.assertEqual([r["retried"] for r in document["runs"]], [True] * (len(outcomes) - 1) + [False])
            self.assertEqual((document["cells"][0]["state"], code), ("ok", 0))
            return
        self.assertEqual(outcomes, ["contaminated"] * 3)        # the run and 2 fresh-process retries
        self.assertEqual([r["retried"] for r in document["runs"]], [True, True, False])
        self.assertEqual(document["cells"][0]["state"], "contaminated")
        self.assertEqual(code, 1)

    def test_bench_args_reach_the_bench_and_repeats_are_refused(self):
        # Every source of extra switches reaches the bench (read back from the manifest), and a repeated key is refused before
        # a run starts (the bench would read only the first occurrence: FParse::Value).
        targets = [{"label": "null", "backend": "Null"}, {"label": "null.Pct7", "backend": "Null", "bench_args": ["-ContaminationPct=7"]}]
        matrix = _matrix(self.out, "switches", [{"scenario": "S00.FreeFall", "frames": 5}], ["-HostCheck=record", "-PreRoll=0"], targets=targets)
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), extra_args=["-HashEvery=1"], log=lambda *_: None)
        document = common.read_json(session_dir / "session.json")
        self.assertEqual(common.schema_errors(document, "session.schema.json"), [])
        self.assertEqual(len(document["runs"]), 2)
        for run in document["runs"]:
            self.assertTrue(run["exit_json"], run)
            manifest = common.read_json(session_dir / run["out_dir"] / "manifest.json")
            self.assertEqual(manifest["run"]["pre_roll_s"], 0.0)                         # the matrix's -PreRoll=0
            self.assertEqual(manifest["platform"]["host_load"]["policy"], "record")      # the matrix's -HostCheck=record
            self.assertEqual(manifest["run"]["hash_every"], 1)                           # --bench-arg=-HashEvery=1
            self.assertEqual(manifest["platform"]["host_load"]["threshold_pct"], 7.0 if run["label"] == "null.Pct7" else 5.0)   # the target's
            self.assertFalse(run["teardown_killed"])
        with self.assertRaisesRegex(MatrixError, "appears twice"):
            runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), extra_args=["-PreRoll=0.5"], log=lambda *_: None)

    def test_viz_manifest_is_rejected(self):
        run_dir = self.out / "viz"
        self.assertEqual(_bench("-Backend=Null", "-Scenario=S00.FreeFall", "-Frames=5", "-RunMode=Viz", "-HostCheck=record", "-PreRoll=0", f"-Out={run_dir}"), 0)
        classified = validate.classify(load.load([run_dir]).runs)
        self.assertFalse(classified.iloc[0]["accepted"])
        self.assertTrue(any("viz-mode manifest" in r for r in classified.iloc[0]["reject_reasons"]))

    def test_mixed_tables_are_errors(self):
        base = self.out / "tables" / "base"
        self.assertEqual(_bench("-Backend=Null", "-Scenario=S00.FreeFall", "-Frames=5", "-HostCheck=record", "-PreRoll=0", f"-Out={base}"), 0)
        manifest = common.read_json(base / "manifest.json")
        own_flag = manifest["build"]["flag_profile"]   # Shipped (test, dev, dbg) or MatchedAVX2 (test-avx2)
        other_flag = "MatchedAVX2" if own_flag == "Shipped" else "Shipped"
        avx2 = self.out / "tables" / "other_flag"
        other_desc = self.out / "tables" / "desc"
        for directory, edit in ((avx2, ("build", "flag_profile", other_flag)), (other_desc, ("scene", "desc_sha256", "0" * 64))):
            shutil.copytree(base, directory)
            copy = json.loads(json.dumps(manifest))
            copy[edit[0]][edit[1]] = edit[2]
            common.write_json_atomic(directory / "manifest.json", copy)
        runs = load.load([base, avx2]).runs
        self.assertEqual(sorted(runs["flag_profile"]), ["MatchedAVX2", "Shipped"])
        with self.assertRaisesRegex(validate.TableError, "flag profiles mixed"):
            validate.check_table(runs, "Shipped + MatchedAVX2")
        with self.assertRaisesRegex(validate.TableError, "desc_sha256 differs"):
            validate.check_table(load.load([base, other_desc]).runs, "two desc_sha256")
        validate.check_table(load.load([base]).runs, "one run")   # a sound table passes

    def test_det1_matrix_and_first_divergent_frame(self):
        gate_dir = self.out / "g8" / "gate"
        self.assertEqual(_bench("-Gate=G8", "-Runs=3", "-Backend=Null", "-Scenario=M3.BoxPile", "-Frames=30", "-HashEvery=1", "-HostCheck=record", "-PreRoll=0",
                                f"-Out={gate_dir}"), 0)
        [result] = determinism.analyze([str(gate_dir)])
        self.assertEqual((result["verdict"], result["runs_ok"], result["resolution"]), ("equal", 3, "per-frame"))
        self.assertEqual(len(set(result["h_native"])), 1)
        tampered = self.out / "g8" / "tampered"
        shutil.copytree(gate_dir, tampered)
        path = tampered / "run2" / "hashes.csv"
        lines = path.read_text(encoding="utf-8").splitlines()
        fields = lines[1 + 17].split(",")                          # data row of frame 17
        self.assertEqual(fields[0], "17")
        fields[1] = "0x" + format(int(fields[1], 16) ^ 1, "016x")  # one bit of h_native
        lines[1 + 17] = ",".join(fields)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        [changed] = determinism.analyze([str(tampered)])
        self.assertEqual(changed["verdict"], "DIFFERENT")
        self.assertEqual((changed["first_divergence"]["frame"], changed["first_divergence"]["field"], changed["first_divergence"]["run"]), (17, "h_native", "run2"))
        state_only = self.out / "g8" / "tampered_state"
        shutil.copytree(gate_dir, state_only)
        path = state_only / "run1" / "hashes.csv"
        lines = path.read_text(encoding="utf-8").splitlines()
        fields = lines[1 + 9].split(",")
        fields[3] = "0x" + format(int(fields[3], 16) ^ 1, "016x")  # one bit of h_state
        lines[1 + 9] = ",".join(fields)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        [changed] = determinism.analyze([str(state_only)])
        self.assertEqual((changed["first_divergence"]["frame"], changed["first_divergence"]["field"]), (9, "h_state"))

    def test_session_end_to_end(self):
        matrix = _matrix(self.out, "e2e", [{"scenario": "P0.EmptyStep", "frames": 60}, {"scenario": "M3.BoxPile", "params": {"N": "1000"}, "frames": 60}],
                         ["-HostCheck=record", "-PreRoll=0.5"], repeats=2, targets=[{"label": "null", "backend": "Null"}, {"label": "null.Dll", "backend": "NullDll"}])
        session_dir, code, document = self._session(matrix)
        self.assertEqual(document["status"], "complete")
        self.assertEqual(len(document["runs"]), 8)
        self.assertTrue(all(r["outcome"] == "ok" for r in document["runs"]), [r["outcome"] for r in document["runs"]])
        self.assertEqual(code, 0)
        orders = [b["order"] for b in document["blocks"]]
        self.assertEqual(len(orders), 2)
        loaded = load.load([session_dir])
        self.assertTrue(loaded.runs["manifest_valid"].all())
        self.assertTrue(loaded.runs["exit_json_valid"].all())
        self.assertEqual(sorted(set(loaded.runs["repeat"])), [0, 1])
        written = load.write_parquet(loaded, session_dir / "analysis")
        self.assertEqual(len(written), 4)
        path = report.build_report(session_dir)
        text = path.read_text(encoding="utf-8")
        self.assertIn("UNOFFICIAL", text)
        not_hp = not document["environment"]["power_plan_high_performance"]
        if not_hp:
            self.assertIn("전원 계획이 High performance가 아니다", text.split("## 1.")[0])   # the head of the report says so
        self.assertIn("--skip-idle", text.split("## 1.")[0])                                # the test's gate bypass is in the head
        # Without session.json (the run directories alone) the head still marks the plan, from the manifests.
        runs_only = report.build_report(session_dir / "runs", self.out / "e2e_runs_report").read_text(encoding="utf-8")
        head = runs_only.split("## 1.")[0]
        self.assertIn("세션 파일 없이", head)
        if not_hp:
            self.assertIn("전원 계획이 High performance가 아니다", head)
            self.assertIn("run 8/8개의 manifest", head)
        for cell in document["cells"]:
            # every ok run is a value unless it is not headline-valid (dev / dbg PT_DO_CHECK, a recorded contamination)
            self.assertEqual(len(cell["values_ns"]) + cell["n_not_headline_valid"], 2)
        # `ptbench perf` with a redirected stdout (CP949 on a Korean Windows; python -I ignores PYTHONIOENCODING)
        env = {k: v for k, v in os.environ.items() if k not in ("PYTHONIOENCODING", "PYTHONUTF8")}
        completed = subprocess.run([sys.executable, "-I", "-m", "ptbench", "perf", str(session_dir)], capture_output=True, timeout=300, check=False, env=env)
        stderr = completed.stderr.decode("utf-8", "replace")
        self.assertNotIn("Traceback", stderr)
        self.assertIn(completed.returncode, (0, 1), stderr)   # 1 = no headline-valid run (dev / dbg)
        completed.stdout.decode("utf-8")                       # UTF-8 ('µs'), never the console code page


if __name__ == "__main__":
    unittest.main()
