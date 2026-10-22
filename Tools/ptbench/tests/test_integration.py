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
  - M10: the conformance stamps with this build's bench (03 M10 criterion 3): stamped cells run and their manifests find
    their stamp; one edited key value of the variant ini (PhysX.UE4Compat.ini BounceThresholdVelocity, or the first SDK
    variant of a single-SDK build) refuses that variant's cells before any process starts and gives a direct bench run another
    variant_sha256 and no stamp; a different Null DLL ("rebuilt") refuses the Null DLL cells; an
    edited tolerances.json refuses every cell; --skip-unstamped runs only the stamped cells. On copies (Config, the exe and
    the DLL in the test directory), never the repository's files
  - M10: every manifest's comparability key equals the one recomputed from its fields; a run of the timing protocol proves
    G10b and the TSC calibration residual stays below 1e-5
The runner tests pass the environment-gate bypasses (CTest runs in parallel on a desktop in use) and -HostCheck=record; every
session except the stamp tests' runs with --allow-unstamped (stamp_policy "allow").
"""
from __future__ import annotations

import csv
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import unittest

import pandas as pd

from ptbench import common, envcheck, runner, stamps
from ptbench.analysis import comparability, determinism, load, report, validate
from ptbench.matrix import Matrix, MatrixError

EXE = os.environ.get("PT_BENCH_EXE", "")
# M10 fix round 1: the invalidation case edits one engine key's value of the SDK variant (not a comment: the bench's canonical
# variant_sha256 has to change too, not only the file's raw hash).
VARIANT_KEY_EDITS = {"PhysX": ("BounceThresholdVelocity", "200", "250"), "Jolt": ("Baumgarte", "0.2", "0.25"), "Bullet": ("WarmstartingFactor", "0.85", "0.8")}
OUT = pathlib.Path(os.environ.get("PT_TEST_OUT", "")) if os.environ.get("PT_TEST_OUT") else None


def _gate() -> dict:
    return envcheck.run_envcheck(allow_balanced=True, allow_busy=True, allow_concurrent=True, skip_idle=True)


def _matrix(directory: pathlib.Path, name: str, scenes: list, bench_args: list, repeats: int = 1, retries: int = 2, timeout: float = 120.0,
            targets: list | None = None) -> Matrix:
    document = {
        "schema": "physicstest.matrix/1", "name": name, "description": "integration test", "purpose": "perf", "repeats": repeats, "block_seed": 42,
        "cooldown_s": 0.0, "timeout_s": timeout, "contaminated_retries": retries, "bench_args": bench_args,
        "targets": targets or [{"label": "null", "backend": "Null"}], "profiles": ["Normalized"], "scenes": scenes, "threads": [1],
        "cv_gate": False,   # M10: the runner mechanics with a fixed run count (the CV gate's logic: test_unit CvGateAndSentinelTests)
    }
    path = directory / f"{name}.json"
    common.write_json_atomic(path, document)
    errors = common.schema_errors(document, "matrix.schema.json")
    if errors:
        raise AssertionError(errors)
    return Matrix(path=path, document=document, sha256=common.sha256_file(path))


def _bench(*args: str, exe: str | None = None) -> int:
    return subprocess.run([exe or EXE, *args], capture_output=True, timeout=300, check=False).returncode


def _available_backends(exe: str) -> list[str]:
    completed = subprocess.run([exe, "-List"], capture_output=True, timeout=120, check=False)
    return list(json.loads(completed.stdout.decode("utf-8")).get("backends", [])) if completed.returncode == 0 else []


@unittest.skipUnless(EXE and OUT, "PT_BENCH_EXE and PT_TEST_OUT are required (CTest PhysicsTest.Py.Integration sets them)")
class IntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = OUT
        shutil.rmtree(cls.out, ignore_errors=True)
        cls.out.mkdir(parents=True)
        cls.exe = pathlib.Path(EXE)

    def _session(self, matrix: Matrix) -> tuple[pathlib.Path, int, dict]:
        # The test's own (empty) stamp directory: the user's Saved/Conformance must never decide what a test sees (M10).
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), log=lambda *_: None, stamp_policy="allow",
                                              stamp_dir=self.out / "no_stamps")
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
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", gate, log=lambda *_: None, stamp_policy="allow",
                              stamp_dir=self.out / "no_stamps")
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
        session_dir, code = runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), extra_args=["-HashEvery=1"], log=lambda *_: None, stamp_policy="allow",
                              stamp_dir=self.out / "no_stamps")
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
            runner.run_matrix(matrix, self.exe, None, self.out / "sessions", _gate(), extra_args=["-PreRoll=0.5"], log=lambda *_: None, stamp_policy="allow",
                              stamp_dir=self.out / "no_stamps")

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
        # M10: the bench's comparability key equals the analysis's recomputation from the manifest's fields.
        self.assertTrue((loaded.runs["comparability_key"] == loaded.runs["comparability_key_recomputed"]).all())
        self.assertEqual(loaded.runs["comparability_key"].nunique(), 2)   # P0 and M3: one table each (Null and the Null DLL share it)
        # Without stamps no run is accepted (R9); --allow-unstamped accepts them and every table says so.
        self.assertFalse(validate.classify(loaded.runs)["accepted"].any())
        path = report.build_report(session_dir, allow_unstamped=True)
        text = path.read_text(encoding="utf-8")
        self.assertIn("UNOFFICIAL", text)
        self.assertIn("적합성 스탬프 없는 run도 받음", text)
        self.assertIn("## 2b. 비교와 주장 규칙", text)
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

    def test_behaviour_session(self):
        """M9: a small behaviour session (the three main engines x both profiles x M1 a, M4 mu 0.30, M5, M6a 5 deg) through the
        runner, then analysis/behavior.py: the references the engines must meet, the M1 profile table, the tags, and the
        joint scene either refused as unsupported (no joint capability) or meeting 03's 5 deg period criterion."""
        from ptbench.analysis import behavior, behavior_rules

        available = _available_backends(EXE)
        targets = [t for t in ({"label": "jolt.Default", "backend": "Jolt", "variant": "Default"}, {"label": "bullet.Default", "backend": "Bullet", "variant": "Default"},
                               {"label": "physx.UE4Compat", "backend": "PhysX", "variant": "UE4Compat"}) if t["backend"] in available]
        if not targets:
            self.skipTest("no SDK backend in this build")
        scenes = [{"scenario": "M1.SingleBoxDrop", "params": {"Case": "a"}}, {"scenario": "M4.InclineFriction", "params": {"Mu": "0.30"}},
                  {"scenario": "M5.BouncingBalls"}, {"scenario": "M6a.Pendulum", "params": {"Angle": "5", "Joint": "Hinge"}}]
        document = {
            "schema": "physicstest.matrix/1", "name": "behave", "description": "integration test (behaviour)", "purpose": "perf", "repeats": 1, "block_seed": 9,
            "cooldown_s": 0.0, "timeout_s": 120.0, "contaminated_retries": 0,
            # PT_TEST_BENCH_ARGS (';'-separated, optional): extra switches, e.g. -ConfigDir=<a frozen bench's Config> when PT_BENCH_EXE is
            # not this build's bench (CTest does not set it)
            "bench_args": ["-RunMode=Behavior", "-StatesEvery=1", "-HostCheck=record", "-PreRoll=0"] + [a for a in os.environ.get("PT_TEST_BENCH_ARGS", "").split(";") if a],
            "targets": targets, "profiles": ["Normalized", "UEParity"], "scenes": scenes, "threads": [1],
        }
        path = self.out / "behave.json"
        common.write_json_atomic(path, document)
        session_dir, code, session = self._session(Matrix(path=path, document=document, sha256=common.sha256_file(path)))
        self.assertEqual(code, 0, [(r["cell_id"], r["outcome"]) for r in session["runs"]])
        analysis = behavior.analyze([session_dir])
        self.assertEqual(analysis.errors, [])
        self.assertEqual(analysis.skipped, [])
        self.assertEqual(len(analysis.results) + len(analysis.unsupported), 8 * len(targets))
        obs = analysis.observations

        def value(label, profile, scenario, metric):
            rows = obs[(obs["label"] == label) & (obs["profile"] == profile) & (obs["scenario"] == scenario) & (obs["metric"] == metric)]
            self.assertEqual(len(rows), 1, (label, profile, scenario, metric))
            return float(rows.iloc[0]["value"])

        labels = [t["label"] for t in targets]
        for label in labels:
            for profile in ("Normalized", "UEParity"):
                self.assertLess(value(label, profile, "M1.SingleBoxDrop", "fall_dz_max_cm"), 2e-3)        # the fall is the damped discrete fall
        if "jolt.Default" in labels:
            self.assertLess(abs(value("jolt.Default", "Normalized", "M4.InclineFriction", "d2s_err_pct")), 0.05)
            self.assertLess(abs(value("jolt.Default", "UEParity", "M4.InclineFriction", "d2s_err_pct")), 0.05)   # the damped reference (UE 0.01)
        for e in ("0.3", "0.5", "0.7", "0.9"):
            if "jolt.Default" in labels:
                self.assertLess(value("jolt.Default", "Normalized", "M5.BouncingBalls", f"e{e}.e_dev_model_max"), 1e-4)       # model 0
            if "physx.UE4Compat" in labels:
                self.assertLess(value("physx.UE4Compat", "Normalized", "M5.BouncingBalls", f"e{e}.e_dev_model_max"), 1e-4)    # model 1 (M6 D-3)
            if "bullet.Default" in labels:
                self.assertLess(value("bullet.Default", "Normalized", "M5.BouncingBalls", f"e{e}.e_dev_model_max"), 1e-3)     # model 2 (M9 commit)
        self.assertEqual(len(analysis.ue_vs_normalized), len(targets))
        for row in analysis.ue_vs_normalized:
            self.assertLess(abs(row["dz_residual_cm"]), 2e-3)                    # the profile difference in the fall window is UE's damping
            self.assertGreater(row["dz_if_jolt_native_cm"], 4 * row["dz_measured_cm"])   # 0.05 would show about 5x the measured difference
        differences = analysis.differences
        self.assertTrue(set(differences["tag"]) <= set(behavior_rules.TAGS))
        if len(targets) == 3:
            # The tag rules explain a difference against the other engines' runs (the consensus and the engine-pair rules); a
            # single-SDK build (test-<engine>, M10) has no other engine, so only the full set must leave nothing unexplained.
            self.assertEqual(int((differences["rule"] == "unexplained").sum()), 0, differences[differences["rule"] == "unexplained"][["label", "scene", "metric"]].to_dict("records"))
        # The criteria name every main engine; a single-SDK build (test-<engine>, M10) has not run the others ("not run").
        period = [c for c in analysis.criteria if c["id"] == "M6a.period" and c["series"] in labels]
        self.assertEqual(len(period), len(targets))
        for row in analysis.criteria:
            if row["id"] == "M6a.period" and row["series"] not in labels:
                self.assertEqual(row["verdict"], "not run", row)
        for row in period:
            self.assertIn(row["verdict"], ("unsupported", "pass"), row)          # joints land in M9: then 03's 5 deg criterion must hold
        text = report.build_report(session_dir).read_text(encoding="utf-8")
        self.assertIn("## 7. 거동", text)
        self.assertIn("거동 run", text.split("## 6.")[0])                        # section 5 summarises the behaviour runs instead of listing them
        self.assertTrue((next((session_dir / "report").iterdir()) / "behavior.json").is_file())

    # ---- M10: conformance stamps with this build's bench (03 M10 criterion 3) ------------------------------------------------
    def _stamp_fixture(self, name: str) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path, dict, str]:
        """A copy of Config, the exe, the Null DLL and the backend catalog in the test directory, and the SDK variant the
        invalidation case edits (PhysX.UE4Compat when this build has PhysX, else the first SDK backend's Default)."""
        root = self.out / name
        shutil.rmtree(root, ignore_errors=True)
        config = root / "Config"
        shutil.copytree(common.repo_root() / "Config", config)
        bin_dir = root / "bin"
        bin_dir.mkdir(parents=True)
        source_bin = pathlib.Path(EXE).parent
        for file in ("PhysicsTestBench.exe", "PtBackend_Null.dll", stamps.CATALOG_FILE):
            shutil.copy2(source_bin / file, bin_dir / file)
        available = _available_backends(EXE)
        sdk = next(((b, v) for b, v in (("PhysX", "UE4Compat"), ("Jolt", "Default"), ("Bullet", "Default")) if b in available), None)
        if sdk is None:
            self.skipTest("no SDK backend in this build")
        targets = [{"label": "null.Dll", "backend": "NullDll"}, {"label": f"{sdk[0].lower()}.{sdk[1]}", "backend": sdk[0], "variant": sdk[1]}]
        document = {"schema": "physicstest.matrix/1", "name": name, "description": "integration test (stamps)", "purpose": "perf", "repeats": 1, "block_seed": 5,
                    "cooldown_s": 0.0, "timeout_s": 120.0, "contaminated_retries": 0, "bench_args": [f"-ConfigDir={config}", "-HostCheck=record", "-PreRoll=0"],
                    "targets": targets, "profiles": ["Normalized"], "scenes": [{"scenario": "S00.FreeFall", "frames": 5}], "threads": [1], "cv_gate": False}
        path = root / f"{name}.json"
        common.write_json_atomic(path, document)
        matrix = Matrix(path=path, document=document, sha256=common.sha256_file(path))
        return root, bin_dir / "PhysicsTestBench.exe", config, {"matrix": matrix, "sdk": sdk}, targets[1]["label"]

    def _stamped_session(self, root, exe, matrix, name, policy="require"):
        return runner.run_matrix(matrix, exe, None, root / "sessions", _gate(), log=lambda *_: None, stamp_policy=policy, stamp_dir=root / "stamps",
                                 session_name=name)

    def test_stamps_refuse_stale_cells_before_any_process(self):
        root, exe, config, fixture, sdk_label = self._stamp_fixture("stamps")
        matrix = fixture["matrix"]
        self.assertEqual(stamps.run_stamp(matrix, exe, root / "stamps", log=lambda *_: None), 0)
        stamp_files = sorted((root / "stamps").glob("*.json"))
        self.assertEqual(len(stamp_files), 2)
        for path in stamp_files:
            document = common.read_json(path)
            self.assertEqual(common.schema_errors(document, "stamp.schema.json"), [])
            self.assertTrue(document["valid"])
            self.assertEqual(stamps.key_sha256(document["key"]), path.stem)   # Python and C++ hash the same canonical key
            self.assertIn("G10b", [g["id"] for g in document["gates"]])
            self.assertEqual(document["pending_sign_offs"], 0)
            # M10 fix round 0: a DLL backend's stamp certifies the ABI gates on that DLL (one process each); a static one's does not.
            abi = [g["id"] for g in document["gates"] if g["id"].startswith("A")]
            self.assertEqual(abi, ["A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A10"] if document["target"]["backend"] == "NullDll" else [])
        # Stamped: the session runs, and every run finds its stamp (the bench recomputes the key in-process).
        session_dir, code = self._stamped_session(root, exe, matrix, "ok")
        self.assertEqual(code, 0)
        session = common.read_json(session_dir / "session.json")
        self.assertEqual(common.schema_errors(session, "session.schema.json"), [])
        self.assertEqual(len(session["runs"]), 2)
        for run in session["runs"]:
            stamp = common.read_json(session_dir / run["out_dir"] / "manifest.json")["conformance_stamp"]
            self.assertTrue(stamp["valid"], stamp)
            self.assertIn(stamp["key_sha256"], [p.stem for p in stamp_files])

        def refused(name, policy="require"):
            session_dir, code = self._stamped_session(root, exe, matrix, name, policy)
            document = common.read_json(session_dir / "session.json")
            self.assertEqual(common.schema_errors(document, "session.schema.json"), [])
            return session_dir, code, document, {c["label"]: c["stamp"] for c in document["cells"]}

        # (1) one key of the variant ini changed (its value): that variant's cells are refused, nothing starts.
        sdk_backend, sdk_variant = fixture["sdk"]
        ini = config / "Profiles" / "Backend" / f"{sdk_backend}.{sdk_variant}.ini"
        original = ini.read_bytes()
        key_name, old_value, new_value = VARIANT_KEY_EDITS[sdk_backend]
        edited, count = re.subn(rb"(?m)^" + re.escape(f"{key_name}={old_value}".encode()) + rb"(\r?)$", f"{key_name}={new_value}".encode() + rb"\1", original, count=1)
        self.assertEqual(count, 1, f"{ini.name} has no line {key_name}={old_value}")
        ini.write_bytes(edited)
        session_dir, code, document, states = refused("variant")
        self.assertEqual((code, document["status"], document["runs"]), (3, "refused", []))
        self.assertFalse((session_dir / "runs").exists())
        self.assertEqual((states["null.Dll"]["state"], states[sdk_label]["state"]), ("valid", "stale"))
        self.assertIn(ini.name, states[sdk_label]["reason"])
        # The bench's own check agrees: under the edited key its canonical variant_sha256 differs from the stamp's, so a direct run
        # finds no stamp for its key (whatever its preflight says about the new value).
        [sdk_stamp] = [common.read_json(path) for path in stamp_files if common.read_json(path)["target"]["backend"] == sdk_backend]
        direct_variant = root / "direct_variant"
        rc = _bench(f"-Backend={sdk_backend}", f"-BackendProfile={sdk_variant}", "-Scenario=S00.FreeFall", "-Frames=5", "-HostCheck=record", "-PreRoll=0",
                    f"-ConfigDir={config}", f"-StampDir={root / 'stamps'}", f"-Out={direct_variant}", exe=str(exe))
        edited_manifest = common.read_json(direct_variant / "manifest.json")
        self.assertNotEqual(edited_manifest["profile"]["variant_sha256"], sdk_stamp["key"]["variant_sha256"], rc)
        self.assertFalse(edited_manifest["conformance_stamp"]["valid"], edited_manifest["conformance_stamp"])
        self.assertNotIn(edited_manifest["conformance_stamp"]["key_sha256"], [p.stem for p in stamp_files])
        # --skip-unstamped runs the stamped cell only.
        session_dir, code, document, states = refused("variant_skip", "skip")
        self.assertEqual(code, 1)
        self.assertEqual([r["label"] for r in document["runs"]], ["null.Dll"])
        self.assertEqual({c["label"]: c["state"] for c in document["cells"]}[sdk_label], "unstamped")
        self.assertIn("--skip-unstamped", document["bypasses"])
        ini.write_bytes(original)
        # (2) the Null DLL "rebuilt" (other bytes): the Null DLL cells are refused, the SDK cell is not.
        dll = exe.parent / "PtBackend_Null.dll"
        dll_bytes = dll.read_bytes()
        dll.write_bytes(dll_bytes + b"\0rebuilt")
        session_dir, code, document, states = refused("dll")
        self.assertEqual((code, document["runs"]), (3, []))
        self.assertEqual((states["null.Dll"]["state"], states[sdk_label]["state"]), ("stale", "valid"))
        self.assertIn("PtBackend_Null.dll changed", states["null.Dll"]["reason"])
        dll.write_bytes(dll_bytes)
        # (3) tolerances.json changed: every cell is refused.
        tolerances = config / "Golden" / "tolerances.json"
        original_tolerances = tolerances.read_bytes()
        tolerances.write_bytes(original_tolerances + b"\n")
        session_dir, code, document, states = refused("tolerances")
        self.assertEqual((code, document["runs"]), (3, []))
        self.assertTrue(all(state["state"] == "stale" and "tolerances" in state["reason"] for state in states.values()), states)
        # The bench's own check agrees: a run under the edited tolerances finds no stamp for its key.
        direct = root / "direct"
        self.assertEqual(_bench("-Backend=NullDll", "-Scenario=S00.FreeFall", "-Frames=5", "-HostCheck=record", "-PreRoll=0", f"-ConfigDir={config}",
                                f"-StampDir={root / 'stamps'}", f"-Out={direct}", exe=str(exe)), 0)
        self.assertFalse(common.read_json(direct / "manifest.json")["conformance_stamp"]["valid"])
        tolerances.write_bytes(original_tolerances)
        session_dir, code = self._stamped_session(root, exe, matrix, "restored")
        self.assertEqual(code, 0)

    def test_protocol_run_proves_g10b_and_calibrates_the_tsc(self):
        run_dir = self.out / "g10b"
        backend = next((b for b in ("Jolt", "PhysX", "Bullet") if b in _available_backends(EXE)), "Null")
        frames = "300" if backend != "Null" else "3000"
        self.assertEqual(_bench(f"-Backend={backend}", "-Scenario=M3.BoxPile", f"-Frames={frames}", "-HostCheck=record", "-PreRoll=0.5", f"-Out={run_dir}"), 0)
        manifest = common.read_json(run_dir / "manifest.json")
        [g10b] = [g for g in manifest["gates"] if g["id"] == "G10b"]
        window = manifest["threads"]["census_window"]
        # M10 fix round 1: G10b's minimum is 50 ms of the process's own CPU time (own_s, not wall_s), and a window long enough
        # for a verdict can still have none when parallel CTest benches starve CPU 0 (g10b_skip "contended": the dedicated,
        # RUN_SERIAL PhysicsTest.Bench.G10b<engine> tests prove the Pass).
        if window["own_s"] < 0.050:
            self.assertEqual((g10b["result"], window["g10b_skip"]), ("Skip", "short_window"), g10b)
        elif g10b["result"] == "Skip":
            self.assertEqual(window["g10b_skip"], "contended", g10b)
            self.assertGreater(window["other_on_mask_fraction"], 0.05, g10b)
        else:
            self.assertEqual((g10b["result"], window["g10b_skip"]), ("Pass", ""), g10b)
            self.assertLess(window["foreign_cycles_fraction"] + window["unattributed_fraction"], 0.005)
        calibration = manifest["clock"]["calibration"]
        self.assertTrue(calibration["valid"])
        self.assertLess(calibration["max_residual"], 1e-5)
        self.assertTrue(manifest["clock"]["tsc_invariant"])
        self.assertGreater(manifest["clock"]["tsc_hz"], 1e8)
        self.assertEqual(manifest["comparability_key"], comparability.key_from_manifest(manifest))


if __name__ == "__main__":
    unittest.main()
