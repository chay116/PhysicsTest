"""Unit tests of the M9 behaviour analysis on synthetic data (no bench executable). Run by CTest PhysicsTest.Py.Unit through
test_unit (which imports these classes), or alone:
    python -I -m unittest -v ptbench.tests.test_behavior

  - analytic: the behaviour references (incline discrete / continuous / damped, step delay, damped fall series, discrete apex,
    restitution models, pendulum) and that analytic.cases() still equals the C++ table's case list (no Python-only case)
  - states: a PTST v1 file written here reads back bit for bit; a truncated file, a wrong magic, another scene's desc prefix
    and an unclosed file are handled
  - geometry: sphere-plane, sphere-box, box corner, SAT (equal to the box corner for a box on a large box; negative when
    separated; a rotated box's corner depth), capsule
  - analyzers on synthetic runs: an M4 slide on the discrete reference and one step late, M5 bounces with model 0 / model 1
    and Bullet's contact-state model, an M6a pendulum (RK4 truth) and its period from the zero crossings, the M9 decay models
    (velocity projection, UE damping) against a velocity-level stepper, a chain's joint stretch, sleep timing, the consensus
    with "never"
  - tags: the first matching rule wins, an unexplained difference is mismatch / provisional, probes are never tagged, a
    chaotic replica spread never widens an analytic bound (M9); M9 fix round 0: the restitution-threshold rule needs another
    threshold than the consensus engines', a branch's confidence wins over the rule's default, post-run values are pending
    overrides of a pre-run base; M9 fix round 1: chaotic-solver makes no claim without Perturb replicas, a standing stack against
    Jolt's slop-lowered consensus is stack-consensus-slop, bullet-chain needs its signature (joints apart), the stack details
    (layers, overlap-free height, bodies off their slot) and the like-with-like Wasserstein cross term; M9 fix round 2: regression-shaped
    differences (a 40 cm impact depth, a 0.001 cm ball stretch, probes where both changes still collapse the pile, a Jolt chain whose
    energy grows) are not explained by impact-phase-depth / bullet-p2p-rows / bullet-stack-solver / jolt-heavy-chain; M9 close (lead
    2026-10-22): no post-run value is pending (TOLERANCES, the restitution proposals, tolerances.json), Bullet's registered model 2
    is judged on the contact step, jolt-penetration-slop checks the slop's magnitude (a fallen Jolt tower, a box on its edge are not
    it), the inferred rules' minimal guards, and the criteria rows follow the lead's decisions (M6a ball reported, PhysX M6c an
    accepted failure only on its mechanism's tag, Bullet's separated chain a finding)
  - the matrix: scene bench_args reach the cell and take part in the duplicate-key check; Config/Matrices/behavior.json
"""
from __future__ import annotations

import json
import math
import pathlib
import tempfile
import unittest

import numpy as np
import pandas as pd

from ptbench import analytic, common
from ptbench import matrix as matrix_module
from ptbench.analysis import behavior as bh
from ptbench.analysis import behavior_rules as rules
from ptbench.analysis import geometry as geo
from ptbench.analysis import states as st

DT = analytic.engine_dt(1.0 / 60.0)
IDENTITY = [0.0, 0.0, 0.0, 1.0]


def quat_axis_angle(axis, angle) -> list[float]:
    axis = np.asarray(axis, dtype=float) / np.linalg.norm(axis)
    s = math.sin(angle / 2.0)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2.0)]


def make_states(position, rotation=None, velocity=None, state=None, frames=None, dt=DT) -> st.States:
    position = np.asarray(position, dtype=float)
    f, b = position.shape[:2]
    rotation = np.tile(np.array(IDENTITY), (f, b, 1)) if rotation is None else np.asarray(rotation, dtype=float)
    velocity = np.zeros((f, b, 3)) if velocity is None else np.asarray(velocity, dtype=float)
    state = np.full((f, b), st.DYNAMIC, dtype=np.uint8) if state is None else np.asarray(state, dtype=np.uint8)
    frame = np.arange(f) if frames is None else np.asarray(frames)
    return st.States(path=pathlib.Path("synthetic"), dt=dt, body_count=b, frame=frame, step=frame + 1, sim_time=(frame + 1) * dt, position=position,
                     rotation=rotation, linear_velocity=velocity, angular_velocity=np.zeros((f, b, 3)), object_state=state, complete=True,
                     scene_prefix="00" * 8, desc_prefix="00" * 8)


def body(index, motion, shape, mass=1.0, pose=None, damping=0.0, inertia=(1.0, 1.0, 1.0), material=0):
    q, t = (IDENTITY, [0.0, 0.0, 0.0]) if pose is None else pose
    return {"index": index, "name": f"b{index}", "motion": motion, "mass": mass, "inertia_diagonal": list(inertia),
            "com_local": {"rotation": IDENTITY, "translation": [0.0, 0.0, 0.0]}, "initial_pose": {"rotation": list(q), "translation": list(t)},
            "linear_damping": damping, "angular_damping": 0.0, "shapes": [dict(shape, local={"rotation": IDENTITY, "translation": [0.0, 0.0, 0.0]}, material=material)]}


def make_desc(scenario, bodies, materials=None, joints=None, params=None) -> dict:
    return {"schema": "physicstest.scene_desc/1", "scenario": scenario, "version": 1, "params": params or {}, "desc_sha256": "0" * 64, "units": "cm",
            "gravity": [0.0, 0.0, -980.0], "materials": materials or [{"index": 0, "name": "m", "friction": 0.5, "static_friction": 0.5, "restitution": 0.0,
                                                                        "friction_combine": "Average", "restitution_combine": "Average"}],
            "bodies": bodies, "joints": joints or []}


def make_manifest(scenario, params, backend="Jolt", variant="Default", profile="Normalized", windows=None, effective=None, chaotic=False) -> dict:
    return {"backend": {"name": backend, "variant": variant}, "profile": {"physics": profile, "resolved": []},
            "scene": {"name": scenario, "params": params, "desc_sha256": "0" * 64, "chaotic": chaotic,
                      "windows": [{"name": n, "first": a, "last": b} for n, (a, b) in (windows or {}).items()]},
            "run": {"mode": "Behavior"},
            "effective": [{"key": k, "canonical": v[0], "native": v[1], "source": "READBACK"} for k, v in (effective or {}).items()]}


def make_run(scenario, params, desc, states, label="jolt.Default", **manifest) -> bh.BehaviorRun:
    backend = manifest.pop("backend", label.split(".")[0].capitalize().replace("Physx", "PhysX"))
    variant = manifest.pop("variant", label.split(".", 1)[1] if "." in label else "")
    return bh.BehaviorRun(run_dir=pathlib.Path("synthetic"), label=label, manifest=make_manifest(scenario, params, backend=backend, variant=variant, **manifest),
                          desc=desc, states=states)


class AnalyticBehaviourTests(unittest.TestCase):
    def test_incline_references(self):
        angle = math.radians(30.0)
        steps = np.arange(0, 121)
        series = analytic.incline_slide_distance_series(0.30, angle, 980.0, DT, steps)
        self.assertAlmostEqual(series[120], analytic.incline_slide_distance(0.30, angle, 980.0, DT, 120), places=12)
        self.assertAlmostEqual(series[120], 474.700, places=3)            # Scenes.md 2.17: d(2 s) at mu 0.30
        self.assertEqual(analytic.incline_slide_distance_series(0.60, angle, 980.0, DT, steps)[120], 0.0)
        damped0 = analytic.incline_slide_distance_damped_series(0.30, angle, 980.0, 0.0, DT, steps)
        np.testing.assert_allclose(damped0, series, rtol=0, atol=1e-12)
        damped = analytic.incline_slide_distance_damped_series(0.30, angle, 980.0, 0.01, DT, steps)
        a = analytic.incline_acceleration(0.30, angle, 980.0)
        v = d = 0.0
        for _ in range(120):
            v = (v + a * DT) * (1.0 - 0.01 * DT)
            d += v * DT
        self.assertAlmostEqual(damped[120], d, places=10)
        self.assertLess(damped[120], series[120])
        continuous = analytic.incline_slide_distance_continuous(0.30, angle, 980.0, 120 * DT)
        self.assertAlmostEqual(series[120] / continuous, 121.0 / 120.0, places=12)   # d_n / d(t_n) = (n + 1) / n

    def test_step_delay(self):
        a = analytic.incline_acceleration(0.30, math.radians(30.0), 980.0)
        late = analytic.incline_slide_distance(0.30, math.radians(30.0), 980.0, DT, 119)
        self.assertAlmostEqual(analytic.discrete_step_delay(late, a, DT, 120), 1.0, places=9)
        self.assertAlmostEqual(analytic.discrete_step_delay(analytic.incline_slide_distance(0.30, math.radians(30.0), 980.0, DT, 120), a, DT, 120), 0.0, places=9)
        self.assertTrue(math.isnan(analytic.discrete_step_delay(1.0, 0.0, DT, 120)))

    def test_damped_fall_series_matches_g1b(self):
        z, v = analytic.damped_free_fall_series(1000.0, 0.0, -980.0, 0.5, DT, 60, analytic.UE_ORDER)
        zf, vf = analytic.damped_free_fall(1000.0, -980.0, 0.5, DT, 60, analytic.UE_ORDER)
        self.assertEqual((z[-1], v[-1]), (zf, vf))
        z0, _ = analytic.damped_free_fall_series(1000.0, 0.0, -980.0, 0.0, DT, 60)
        self.assertAlmostEqual(z0[-1], 501.8332813705, places=9)

    def test_discrete_apex(self):
        for h0, v0 in ((8.29, 496.5), (0.0, 100.0), (3.0, 16.0), (5.0, 0.0)):
            apex, n = analytic.discrete_flight_apex(h0, v0, 980.0, DT)
            brute = max(h0 + k * v0 * DT - 980.0 * DT * DT * k * (k + 1) / 2 for k in range(0, 200))
            self.assertAlmostEqual(apex, brute, places=12)
            self.assertLessEqual(apex, analytic.continuous_flight_apex(h0, v0, 980.0) + 1e-12)

    def test_restitution_models(self):
        speed = 980.0 * DT * 38          # S05: 620.67 cm/s at step 38
        self.assertAlmostEqual(analytic.restitution_prediction(0.8, speed, 980.0, DT, 1), 0.82105, places=5)   # M6 D-3: PhysX 0.82105
        self.assertEqual(analytic.restitution_prediction(0.8, speed, 980.0, DT, 0), 0.8)
        self.assertAlmostEqual(analytic.restitution_approach_speed(40.0, 980.0, DT, 1), 40.0 + 980.0 * DT, places=12)

    def test_pendulum_references(self):
        self.assertAlmostEqual(analytic.pendulum_small_angle_period(100.0, 5.0, 980.0), analytic.pendulum_period(100.0, 5.0, 980.0, 0.0), places=12)
        self.assertAlmostEqual(analytic.simple_pendulum_period(100.0, 980.0, math.radians(5.0)), 2.00805, places=5)   # P-10: not 03's value

    def test_case_table_is_unchanged_by_the_behaviour_references(self):
        # the C++ comparison requires the same case list on both sides; the M9 helpers must not add a Python-only case
        ids = [case["id"] for case in analytic.cases()]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(case["function"] in ("EngineDeltaTime", "SemiImplicitEulerFreeFall", "DampedFreeFall", "FrictionSlideStopDistance",
                                                 "StepBoundaryDropHeight", "PendulumPeriod", "InclineSlideDistance") for case in analytic.cases()))


class StatesFileTests(unittest.TestCase):
    def _write(self, directory: pathlib.Path, frames: int = 4, bodies: int = 2, closed: bool = True, magic: bytes = b"PTST", desc_prefix: bytes = b"\x11" * 8) -> np.ndarray:
        header = np.zeros(1, dtype=st.HEADER_DTYPE)
        header["magic"], header["version"], header["flags"], header["body_count"] = magic, 1, 1 if closed else 0, bodies
        header["frame_count"], header["record_stride"], header["units"], header["frame_header_size"], header["dt"] = frames if closed else 0, 112, 1, 16, DT
        header["desc_sha256_prefix"] = np.frombuffer(desc_prefix, dtype="V8")[0]
        frame_dtype = np.dtype([("frame", "<u4"), ("step", "<u4"), ("sim_time", "<f8"), ("bodies", st.BODY_DTYPE, (bodies,))])
        blocks = np.zeros(frames, dtype=frame_dtype)
        blocks["frame"] = np.arange(frames)
        blocks["step"] = np.arange(frames) + 1
        blocks["sim_time"] = (np.arange(frames) + 1) * DT
        rng = np.random.default_rng(7)
        blocks["bodies"]["position"] = rng.normal(size=(frames, bodies, 3))
        blocks["bodies"]["rotation_xyzw"] = np.array(IDENTITY)
        blocks["bodies"]["object_state"] = st.DYNAMIC
        (directory / "states.ptst").write_bytes(header.tobytes() + blocks.tobytes())
        return blocks

    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            blocks = self._write(directory)
            states = st.read_states(directory, {"scene": {"desc_sha256": "11" * 32}})
            self.assertEqual((states.num_frames, states.body_count), (4, 2))
            np.testing.assert_array_equal(states.position, blocks["bodies"]["position"])
            self.assertEqual(states.dt, DT)
            self.assertTrue(states.complete)
            self.assertEqual(states.row_of(2), 2)
            np.testing.assert_array_equal(states.rows(1, 2), [1, 2])

    def test_rejections(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            self._write(directory, magic=b"XXXX")
            with self.assertRaisesRegex(st.StatesError, "magic"):
                st.read_states(directory)
            self._write(directory)
            with self.assertRaisesRegex(st.StatesError, "desc_sha256 prefix"):
                st.read_states(directory, {"scene": {"desc_sha256": "22" * 32}})
            raw = (directory / "states.ptst").read_bytes()
            (directory / "states.ptst").write_bytes(raw[:-10])
            with self.assertRaisesRegex(st.StatesError, "truncated"):
                st.read_states(directory)

    def test_unclosed_file_reads_complete_frames(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            self._write(directory, frames=5, closed=False)
            raw = (directory / "states.ptst").read_bytes()
            (directory / "states.ptst").write_bytes(raw[:-30])   # the last frame is half written
            states = st.read_states(directory)
            self.assertFalse(states.complete)
            self.assertEqual(states.num_frames, 4)

    def test_dtype_json_must_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            self._write(directory)
            document = {"schema": "physicstest.states_dtype/1", "header_size": 64, "frame_header_size": 16, "record_stride": 112, "body_count": 2,
                        "body": [["position", "<f8", [3]], ["rotation_xyzw", "<f8", [4]], ["linear_velocity", "<f8", [3]], ["angular_velocity", "<f8", [3]],
                                 ["object_state", "u1"], ["pad", "V7"]]}
            (directory / "states.dtype.json").write_text(json.dumps(document), encoding="utf-8")
            st.read_states(directory)
            document["body"][0] = ["position", "<f4", [3]]
            (directory / "states.dtype.json").write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(st.StatesError, "body layout"):
                st.read_states(directory)


class GeometryTests(unittest.TestCase):
    def test_sphere(self):
        self.assertAlmostEqual(float(geo.sphere_plane(np.array([0.0, 0.0, 24.0]), 25.0, np.array([0.0, 0.0, 1.0]), 0.0)), 1.0)
        box_c, box_a, box_h = np.zeros(3), np.eye(3), np.array([100.0, 100.0, 50.0])
        self.assertAlmostEqual(float(geo.sphere_obb(np.array([0.0, 0.0, 70.0]), 25.0, box_c, box_a, box_h)), 5.0)
        self.assertAlmostEqual(float(geo.sphere_obb(np.array([0.0, 0.0, 40.0]), 25.0, box_c, box_a, box_h)), 35.0)    # centre inside: 25 + 10
        self.assertAlmostEqual(float(geo.sphere_obb(np.array([130.0, 0.0, 80.0]), 25.0, box_c, box_a, box_h)), 25.0 - math.hypot(30.0, 30.0))

    def test_box_corner_and_sat_agree_on_a_ground(self):
        ground = (np.array([0.0, 0.0, -50.0]), np.eye(3), np.array([10000.0, 10000.0, 50.0]))
        n, d = geo.top_face_plane(*ground)
        np.testing.assert_allclose(n, [0.0, 0.0, 1.0])
        self.assertAlmostEqual(d, 0.0)
        for angle in (0.0, 0.3, 0.7):
            q = quat_axis_angle([1.0, 0.5, 0.0], angle)
            axes = st.quat_to_matrix(np.array(q))
            center = np.array([5.0, 3.0, 48.0])
            half = np.array([50.0, 50.0, 50.0])
            corner = float(geo.box_plane(center, axes, half, n, d))
            expected = -(center[2] - np.sum(np.abs(axes[2, :]) * half))
            self.assertAlmostEqual(corner, expected, places=9)
            self.assertAlmostEqual(float(geo.obb_obb_sat(center, axes, half, *ground)), corner, places=6)

    def test_sat_separated_and_overlapping_boxes(self):
        a = (np.array([0.0, 0.0, 0.0]), np.eye(3), np.array([50.0, 50.0, 50.0]))
        b = (np.array([0.0, 0.0, 99.0]), np.eye(3), np.array([50.0, 50.0, 50.0]))
        self.assertAlmostEqual(float(geo.obb_obb_sat(*a, *b)), 1.0)
        c = (np.array([0.0, 0.0, 103.0]), np.eye(3), np.array([50.0, 50.0, 50.0]))
        self.assertAlmostEqual(float(geo.obb_obb_sat(*a, *c)), -3.0)
        rotated = (np.array([0.0, 0.0, 120.0]), st.quat_to_matrix(np.array(quat_axis_angle([1.0, 0.0, 0.0], math.pi / 4))), np.array([50.0, 50.0, 50.0]))
        self.assertAlmostEqual(float(geo.obb_obb_sat(*a, *rotated)), 50.0 + 50.0 * math.sqrt(2.0) - 120.0, places=6)

    def test_capsule(self):
        box = (np.array([0.0, 0.0, -50.0]), np.eye(3), np.array([1000.0, 1000.0, 50.0]))
        depth = geo.capsule_obb(np.array([-50.0, 0.0, 24.0]), np.array([50.0, 0.0, 25.5]), 25.0, *box)
        self.assertAlmostEqual(float(depth), 1.0)


class AnalyzerTests(unittest.TestCase):
    def _m4_run(self, mu: float, delay: int = 0, label: str = "jolt.Default", damping: float = 0.0) -> bh.BehaviorRun:
        angle = math.radians(30.0)
        q = quat_axis_angle([0.0, 1.0, 0.0], -angle)              # FRotator(Pitch 30): +X up the slope
        axes = st.quat_to_matrix(np.array(q))
        start = np.array([0.0, 0.0, 1000.0]) + axes @ np.array([1300.0, 0.0, 35.0])
        frames = np.arange(180)
        steps = np.maximum(frames + 1 - delay, 0)
        d = analytic.incline_slide_distance_damped_series(mu, angle, 980.0, damping, DT, steps)
        position = np.zeros((180, 2, 3))
        position[:, 0] = [0.0, 0.0, 1000.0]
        position[:, 1] = start - np.outer(d, axes[:, 0])
        rotation = np.tile(np.array(q), (180, 2, 1))
        desc = make_desc("M4.InclineFriction", [body(0, "static", {"type": "box", "half_extent": [1500.0, 300.0, 10.0]}, mass=2613.5, pose=(q, [0.0, 0.0, 1000.0])),
                                                body(1, "dynamic", {"type": "box", "half_extent": [25.0, 25.0, 25.0]}, mass=10.0, pose=(q, start.tolist()), damping=damping)])
        return make_run("M4.InclineFriction", {"Mu": f"{mu:.2f}", "BoxYaw": "0"}, desc, make_states(position, rotation), label=label,
                        windows={"headline": (1, 179), "2s": (1, 119)})

    def test_m4_slide_on_the_discrete_reference(self):
        result = bh.analyze_m4(self._m4_run(0.30))
        self.assertAlmostEqual(result["metrics"]["d2s_err_pct"], 0.0, places=9)
        self.assertAlmostEqual(result["details"]["step_delay"], 0.0, places=6)
        self.assertLess(result["metrics"]["lateral_cm"], 1e-9)
        self.assertAlmostEqual(result["metrics"]["contact_penetration_cm"], 0.0, places=6)   # the box sits exactly on the incline
        late = bh.analyze_m4(self._m4_run(0.30, delay=1))
        self.assertAlmostEqual(late["details"]["step_delay"], 1.0, places=6)
        self.assertLess(late["metrics"]["d2s_err_pct"], -1.0)                                # one step late: -2/121
        damped = bh.analyze_m4(self._m4_run(0.30, damping=0.01))
        self.assertAlmostEqual(damped["metrics"]["d2s_err_pct"], 0.0, places=9)             # UEParity: the reference carries the damping

    def test_m4_holding(self):
        result = bh.analyze_m4(self._m4_run(0.60))
        self.assertEqual(result["metrics"]["hold_drift_cm"], 0.0)
        self.assertNotIn("d2s_err_pct", result["metrics"])

    def _m5_run(self, model: int, label: str, e0: float = 0.5, gap_before: dict | None = None) -> bh.BehaviorRun:
        """One ball over a pad (top at z = 0): a discrete flight whose bounces follow `model` exactly."""
        g, r = 980.0, 25.0
        h = analytic.step_boundary_drop_height(g, DT, 38) + 0.01
        z, v = h + r, 0.0
        zs, vs = [], []
        for _ in range(240):
            v_new = v - g * DT
            z_new = z + v_new * DT
            if z_new - r < 0.0 and v_new < 0.0:
                speed = -v
                if model == analytic.RESTITUTION_MODEL_CONTACT_STATE:
                    e = bh.contact_state_restitution(e0, z - r, speed, DT, 0.2, -4.0)[0]   # the gap at the step start (> 0 here)
                else:
                    e = analytic.restitution_prediction(e0, speed, g, DT, model)
                v_new = e * speed
                z_new = z + v_new * DT
            z, v = z_new, v_new
            zs.append(z)
            vs.append(v)
        position = np.zeros((240, 2, 3))
        position[:, 0] = [0.0, 0.0, -50.0]
        position[:, 1, 2] = zs
        velocity = np.zeros((240, 2, 3))
        velocity[:, 1, 2] = vs
        materials = [{"index": 0, "name": "ball", "friction": 0.5, "static_friction": 0.5, "restitution": e0, "friction_combine": "Average", "restitution_combine": "Average"}]
        desc = make_desc("M5.BouncingBalls", [body(0, "static", {"type": "box", "half_extent": [100.0, 100.0, 50.0]}, pose=(IDENTITY, [0.0, 0.0, -50.0])),
                                              body(1, "dynamic", {"type": "sphere", "radius": r}, mass=10.0, pose=(IDENTITY, [0.0, 0.0, h + r]))], materials=materials)
        return make_run("M5.BouncingBalls", {}, desc, make_states(position, velocity=velocity), label=label,
                        effective={"contact.restitution_threshold": (50.0, "50 cm/s")}, windows={"headline": (1, 239)})

    def test_m5_models(self):
        for model, label in ((0, "jolt.Default"), (1, "physx.UE4Compat"), (2, "bullet.Default")):
            run = self._m5_run(model, label)
            result = bh.analyze_m5(run)
            ball = result["details"]["balls"][0]
            self.assertEqual(result["details"]["model"], model)          # tolerances.json G4: Jolt 0, PhysX 1, Bullet 2 (M9 commit)
            self.assertNotIn("e0.5.e_dev_proposed_max", result["metrics"])   # no open proposal since the M9 commit
            self.assertGreaterEqual(ball["n_judged"], 2 if model == 2 else 3)     # model 2 loses gap / dt: fewer bounces above 50 cm/s
            self.assertEqual(ball["impacts"][0]["frame"], 38)           # step 39 (frame 38): the S05 / M5 contact step
            self.assertLess(ball["e_dev_model_max"], 1e-9)
            if model == 1:
                self.assertGreater(ball["e_dev_nominal_max"], 0.02)     # model 1 grows as the bounces slow
            self.assertLess(abs(ball["apex1_ratio_err"]), 1e-9)         # the first apex is the discrete flight with the model's v_after

    def test_bullet_contact_state_model(self):
        e0, speed = 0.9, 413.79
        cases = [(0.775, 0.9 - 0.775 / (DT * speed), "speculative"), (-2.793, 0.9 + 0.2 * 2.793 / (DT * speed), "shallow"), (-6.4, 0.9, "deep")]
        for gap, expected, kind in cases:
            value, got = bh.contact_state_restitution(e0, gap, speed, DT, 0.2, -4.0)
            self.assertAlmostEqual(value, expected, places=12)
            self.assertEqual(got, kind)
        # M9 data point (Bullet, ball 0.9, frame 282): depth 2.793 cm at 413.79 cm/s -> e 0.981
        self.assertAlmostEqual(bh.contact_state_restitution(0.9, -2.793, 413.79, DT, 0.2, -4.0)[0], 0.981, places=3)

    def test_m6a_pendulum_period(self):
        """A solid-sphere physical pendulum integrated with RK4 (16 substeps per frame) and sampled per frame: the zero
        crossings give the period of Analytic::PendulumPeriod."""
        length, radius, g = 100.0, 5.0, 980.0
        amplitude = math.radians(30.0)
        k = g * length / (length * length + 0.4 * radius * radius)
        theta, omega = amplitude, 0.0
        sub = 16
        h = DT / sub
        positions = []
        for _ in range(600):
            for _ in range(sub):
                def f(th, om):
                    return om, -k * math.sin(th)
                k1 = f(theta, omega)
                k2 = f(theta + 0.5 * h * k1[0], omega + 0.5 * h * k1[1])
                k3 = f(theta + 0.5 * h * k2[0], omega + 0.5 * h * k2[1])
                k4 = f(theta + h * k3[0], omega + h * k3[1])
                theta += h / 6 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0])
                omega += h / 6 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1])
            positions.append([length * math.sin(theta), 0.0, 1000.0 - length * math.cos(theta)])
        position = np.array(positions)[:, None, :]
        start = [length * math.sin(amplitude), 0.0, 1000.0 - length * math.cos(amplitude)]
        hinge = quat_axis_angle([0.0, 0.0, 1.0], math.pi / 2)          # frame X = world Y (the hinge axis)
        joint = {"index": 0, "name": "j", "type": "hinge", "body1": 0, "body2": -1, "frame1": {"rotation": hinge, "translation": [0.0, 0.0, 0.0]},
                 "frame2": {"rotation": hinge, "translation": [0.0, 0.0, 1000.0]}, "disable_collision": False, "simulated": True}
        # frame1 at the bob's actor origin is only right at rest; this test checks the period and the rod, not the stretch
        desc = make_desc("M6a.Pendulum", [body(0, "dynamic", {"type": "sphere", "radius": radius}, pose=(IDENTITY, start))], joints=[joint])
        run = make_run("M6a.Pendulum", {"Angle": "30", "Joint": "Hinge"}, desc, make_states(position), windows={"headline": (1, 599)})
        result = bh.analyze_m6a(run)
        self.assertGreaterEqual(result["details"]["crossings"], 8)
        self.assertLess(abs(result["metrics"]["period_err_pct"]), 0.01)
        self.assertAlmostEqual(result["details"]["reference_s"], 2.04305, places=5)
        self.assertLess(result["metrics"]["rod_length_err_max_cm"], 1e-9)
        self.assertLess(result["metrics"]["out_of_plane_max_cm"], 1e-9)
        self.assertLess(abs(result["metrics"]["amplitude_decay_pct_per_period"]), 0.01)

    def _projected_pendulum(self, amplitude_deg: float, damping: float = 0.0) -> bh.BehaviorRun:
        """A point-like bob on a 100 cm rod stepped like a velocity-level joint solver (M9 integration): gravity, UE-order damping,
        the velocity put back on the tangent, the position moved and put back on the circle. Its amplitude loss is the
        velocity-projection model of analyze_m6a (pendulum-velocity-projection) plus the damping model (pendulum-ue-damping)."""
        length = 100.0
        a0 = math.radians(amplitude_deg)
        p = np.array([length * math.sin(a0), -length * math.cos(a0)])     # (x, z) relative to the pivot
        v = np.zeros(2)
        positions = []
        for _ in range(600):
            v = (v + np.array([0.0, -980.0]) * DT) * max(0.0, 1.0 - damping * DT)
            radial = p / np.linalg.norm(p)
            v = v - (v @ radial) * radial
            p = p + v * DT
            p = p / np.linalg.norm(p) * length
            positions.append([p[0], 0.0, 1000.0 + p[1]])
        start = [length * math.sin(a0), 0.0, 1000.0 - length * math.cos(a0)]
        hinge = quat_axis_angle([0.0, 0.0, 1.0], math.pi / 2)
        joint = {"index": 0, "name": "j", "type": "hinge", "body1": 0, "body2": -1, "frame1": {"rotation": hinge, "translation": [0.0, 0.0, 0.0]},
                 "frame2": {"rotation": hinge, "translation": [0.0, 0.0, 1000.0]}, "disable_collision": False, "simulated": True}
        desc = make_desc("M6a.Pendulum", [body(0, "dynamic", {"type": "sphere", "radius": 0.0}, pose=(IDENTITY, start), damping=damping, inertia=(0.0, 0.0, 0.0))],
                         joints=[joint])
        return make_run("M6a.Pendulum", {"Angle": f"{amplitude_deg:g}", "Joint": "Hinge"}, desc, make_states(np.array(positions)[:, None, :]),
                        windows={"headline": (1, 599)})

    def test_m6a_velocity_projection_and_damping_models(self):
        """M9: the small-swing velocity-projection loss (3/4) pi^2 a0^2 dt / T per period and UE damping's 1 - exp(-c T / 2) are what a
        velocity-level stepper gives (the models behind the pendulum-velocity-projection / pendulum-ue-damping rules)."""
        for angle in (5.0, 30.0):
            result = bh.analyze_m6a(self._projected_pendulum(angle))
            measured, model = result["metrics"]["amplitude_decay_pct_per_period"], result["details"]["projection_decay_pct_per_period"]
            self.assertAlmostEqual(measured / model, 1.0, delta=0.15, msg=(angle, measured, model))
            self.assertEqual(result["details"]["damping_decay_pct_per_period"], 0.0)
        damped = bh.analyze_m6a(self._projected_pendulum(5.0, damping=0.01))
        details = damped["details"]
        self.assertAlmostEqual(details["damping_decay_pct_per_period"], 100.0 * (1.0 - math.exp(-0.5 * 0.01 * details["reference_s"])), delta=1e-3)
        expected = details["damping_decay_pct_per_period"] + details["projection_decay_pct_per_period"]
        self.assertAlmostEqual(damped["metrics"]["amplitude_decay_pct_per_period"], expected, delta=0.1 * expected)
        # the rotation-linearization bound 2 (w_max dt)^2 L of a 90 deg swing (Progress M9 Bullet J-3: 1.01 cm with the bob's inertia)
        swing = bh.analyze_m6a(self._projected_pendulum(90.0))["details"]
        self.assertAlmostEqual(swing["omega_max"], math.sqrt(2.0 * 980.0 / 100.0), places=6)
        self.assertAlmostEqual(swing["linearization_stretch_cm"], 2.0 * (swing["omega_max"] * DT) ** 2 * 100.0, places=9)

    def test_zero_crossings(self):
        t = np.linspace(0.0, 10.0, 6001)
        x = np.cos(2 * math.pi * t / 2.5)
        period, n = bh.pendulum_period(t, x)
        self.assertAlmostEqual(period, 2.5, places=6)
        self.assertEqual(n, 8)
        self.assertTrue(math.isnan(bh.pendulum_period(t[:100], x[:100])[0]))

    def test_chain_stretch_and_energy(self):
        """Two links hanging still, the joint frames 0.3 cm apart: stretch 0.3; a still chain keeps its energy."""
        capsule = {"type": "capsule", "radius": 5.0, "half_length": 15.0}
        position = np.zeros((10, 2, 3))
        position[:, 0] = [0.0, 0.0, 980.0]
        position[:, 1] = [0.0, 0.0, 939.7]
        joints = [{"index": 0, "type": "ball", "body1": 0, "body2": -1, "frame1": {"rotation": IDENTITY, "translation": [0.0, 0.0, 20.0]},
                   "frame2": {"rotation": IDENTITY, "translation": [0.0, 0.0, 1000.0]}, "disable_collision": True, "simulated": True},
                  {"index": 1, "type": "ball", "body1": 1, "body2": 0, "frame1": {"rotation": IDENTITY, "translation": [0.0, 0.0, 20.0]},
                   "frame2": {"rotation": IDENTITY, "translation": [0.0, 0.0, -20.0]}, "disable_collision": True, "simulated": True}]
        desc = make_desc("M6b.Chain", [body(0, "dynamic", capsule, pose=(IDENTITY, [0.0, 0.0, 980.0])), body(1, "dynamic", capsule, pose=(IDENTITY, [0.0, 0.0, 939.7]))],
                         joints=joints)
        run = make_run("M6b.Chain", {"Perturb": "0"}, desc, make_states(position), windows={"headline": (1, 9), "late": (5, 9)}, chaotic=True)
        result = bh.analyze_chain(run)
        self.assertAlmostEqual(result["metrics"]["stretch_max_cm"], 0.3, places=9)
        self.assertAlmostEqual(result["metrics"]["energy_growth_pct"], 0.0, places=9)

    def test_sleep_timing(self):
        position = np.zeros((10, 1, 3))
        velocity = np.zeros((10, 1, 3))
        velocity[3, 0] = [7.0, 0.0, 0.0]
        state = np.array([[st.DYNAMIC]] * 4 + [[st.SLEEPING]] * 6)
        desc = make_desc("M1.SingleBoxDrop", [body(0, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]})])
        run = make_run("M1.SingleBoxDrop", {"Case": "a"}, desc, make_states(position, velocity=velocity, state=state), profile="UEParity")
        self.assertEqual(bh.time_to_sleep(run), (5 * DT, 4))
        self.assertAlmostEqual(bh.sleep_speed(run), 7.0)
        state[-1] = st.DYNAMIC
        awake = make_run("M1.SingleBoxDrop", {"Case": "a"}, desc, make_states(position, state=state))
        self.assertEqual(bh.time_to_sleep(awake), (None, None))

    def test_median_with_never(self):
        self.assertEqual(bh.median_with_inf([0.5, 2.0, math.inf]), 2.0)
        self.assertEqual(bh.median_with_inf([0.5, math.inf]), 0.5)
        self.assertEqual(bh.median_with_inf([math.inf, math.inf]), math.inf)
        self.assertEqual(bh.deviation_of(math.inf, 2.0), math.inf)
        self.assertEqual(bh.deviation_of(math.inf, math.inf), 0.0)


class TagTests(unittest.TestCase):
    def _observations(self) -> tuple[list, dict, pd.DataFrame]:
        desc = make_desc("M1.SingleBoxDrop", [body(0, "static", {"type": "box", "half_extent": [10000.0, 10000.0, 50.0]}, pose=(IDENTITY, [0.0, 0.0, -50.0])),
                                              body(1, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]}, pose=(IDENTITY, [0.0, 0.0, 150.0]))])
        runs, results = [], {}
        for label, depth in (("jolt.Default", 2.0), ("bullet.Default", 0.0), ("physx.UE4Compat", 0.0), ("bullet.R0Iter50", 1.0)):
            position = np.zeros((5, 2, 3))
            position[:, 0] = [0.0, 0.0, -50.0]
            position[:, 1] = [0.0, 0.0, 50.0 - depth]
            run = make_run("M1.SingleBoxDrop", {"Case": "a"}, desc, make_states(position), label=label,
                           effective={"jolt.penetration_slop": (2.0, "0.02 m")} if label.startswith("jolt") else {})
            run.probe = label == "bullet.R0Iter50"
            runs.append(run)
            results[id(run)] = {"metrics": {"rest_penetration_cm": depth, "final_tilt_deg": 0.0 if label != "physx.UE4Compat" else 9.0}, "details": {}}
        return runs, results, bh.build_observations(runs, results)

    def test_rules_and_fallback(self):
        runs, results, observations = self._observations()
        differences = bh.tag_differences(runs, results, observations)
        by_label = {(r["label"], r["metric"]): r for r in differences.to_dict("records")}
        slop = by_label[("jolt.Default", "rest_penetration_cm")]
        self.assertEqual((slop["tag"], slop["rule"]), ("by-design", "jolt-penetration-slop"))
        tilt = by_label[("physx.UE4Compat", "final_tilt_deg")]
        self.assertEqual((tilt["tag"], tilt["rule"], tilt["confidence"]), ("mismatch", "unexplained", "provisional"))
        self.assertNotIn(("bullet.R0Iter50", "rest_penetration_cm"), by_label)          # a probe is evidence, never a tagged difference
        self.assertTrue(set(differences["tag"]) <= set(rules.TAGS))

    def test_chaotic_spread_never_widens_an_analytic_bound(self):
        """M9: a chain whose energy diverges in every replica has a wide replica spread; the energy bound (an analytic reference: it
        cannot grow) stays the registered 1%, while a consensus metric of the same chaotic scene is widened by the spread."""
        desc = make_desc("M6c.HeavyChain", [body(0, "dynamic", {"type": "sphere", "radius": 5.0})])
        runs, results = [], {}
        for label, growth in (("jolt.Default", (0.0, 0.0)), ("bullet.Default", (0.0, 0.0)), ("physx.UE4Compat", (4000.0, 9000.0))):
            for perturb, value in enumerate(growth):
                run = make_run("M6c.HeavyChain", {"Perturb": str(perturb)}, desc, make_states(np.zeros((3, 1, 3))), label=label, chaotic=True)
                runs.append(run)
                results[id(run)] = {"metrics": {"energy_growth_pct": value, "final_z_mean_cm": 600.0 + (0.0 if label != "physx.UE4Compat" else 3.0 * perturb)},
                                    "details": {}}
        observations = bh.build_observations(runs, results)
        energy = observations[(observations["metric"] == "energy_growth_pct") & (observations["label"] == "physx.UE4Compat")].iloc[0]
        self.assertEqual(energy["effective_tolerance"], energy["tolerance"])
        self.assertFalse(energy["within"])
        final = observations[(observations["metric"] == "final_z_mean_cm") & (observations["label"] == "physx.UE4Compat")].iloc[0]
        self.assertGreater(final["effective_tolerance"], final["tolerance"])          # consensus: the replicas' spread widens it

    def test_every_rule_has_a_tag_and_reason_text(self):
        self.assertEqual(len({rule.id for rule in rules.RULES}), len(rules.RULES))
        for rule in rules.RULES + [rules.UNEXPLAINED]:
            self.assertIn(rule.tag, rules.TAGS)
            self.assertIn(rule.confidence, ("verified", "accepted", "inferred", "provisional"))
        for key, tolerance in rules.TOLERANCES.items():
            self.assertTrue(tolerance.reason, key)
            self.assertIn(tolerance.registered, ("pre-run", "post-run"), key)
            for override in tolerance.overrides:
                self.assertEqual(override.registered, "post-run", key)
                self.assertTrue(override.sign_off and override.reason, key)

    def test_pending_override_does_not_change_the_verdict(self):
        """A pending post-run override is reported, never used; once signed it is the value (M9 commit: the lead signed M4's)."""
        tolerance = rules.Tolerance(0.1, "cm", "pre-run", "test", (rules.Override(("physx.*",), 6.0, "post-run", "pending", "test"),))
        value, source, pending = tolerance.resolve("physx.UE4Compat", "Normalized")
        self.assertEqual((value, source, [o.value for o in pending]), (0.1, "pre-run", [6.0]))
        self.assertEqual(tolerance.resolve("jolt.Default", "Normalized")[2], [])
        value, source, pending = rules.TOLERANCES["M4.hold_drift_cm"].resolve("physx.UE4Compat", "Normalized")
        self.assertEqual((value, pending), (6.0, []))
        self.assertIn(rules.LEAD_SIGN_OFF, source)
        self.assertEqual(rules.TOLERANCES["M4.hold_drift_cm"].resolve("jolt.Default", "Normalized")[:2], (0.1, "pre-run"))

    def test_post_run_values_wait_for_sign_off(self):
        """M9 fix round 0: a value raised after looking at the data is a post-run override, never the base; the base is the pre-run
        value (the one exception is the proposal metric e_dev_proposed_max, which only marks a failure as pending, never passes one).
        M9 commit: the lead signed every one (2026-10-22) - none is pending in the analysis, its restitution proposals or tolerances.json."""
        for key, tolerance in rules.TOLERANCES.items():
            if key != "M5.e_dev_proposed_max":
                self.assertEqual(tolerance.registered, "pre-run", key)
            for override in tolerance.overrides:
                self.assertEqual(override.sign_off, rules.LEAD_SIGN_OFF, key)
        value, source, pending = rules.TOLERANCES["M1.final_xy_cm"].resolve("physx.UE4Compat", "Normalized")
        self.assertEqual((value, pending), (10.0, []))
        self.assertIn("post-run", source)
        self.assertEqual(rules.TOLERANCES["M3.top_z_final_cm"].resolve("jolt.Default", "UEParity")[0], 20.0)
        self.assertEqual(rules.TOLERANCES["M3.mean_z_final_cm"].resolve("bullet.Default", "Normalized")[0], 10.0)
        self.assertEqual(rules.TOLERANCES["M4.d2s_err_pct"].resolve("bullet.Default", "Normalized")[0], 8.0)
        self.assertEqual(rules.TOLERANCES["M4.d2s_err_pct"].resolve("physx.UE4Compat-Stab", "UEParity")[0], 6.5)
        self.assertEqual(rules.TOLERANCES["M4.hold_drift_cm"].resolve("physx.UE4Compat-TGS", "Normalized")[0], 0.3)
        self.assertEqual(rules.TOLERANCES["M5.e_dev_model_max"].overrides, ())          # no TGS 0.6: the split impacts are a mechanism test
        self.assertEqual(rules.PROPOSED_RESTITUTION_MODELS, {})                          # Bullet's model 2 is registered (G4)
        self.assertEqual(bh.restitution_model("Bullet", "Default")[0], 2)
        tolerances = json.loads((common.repo_root() / "Config" / "Golden" / "tolerances.json").read_text(encoding="utf-8"))
        signed = []
        for gate in tolerances["gates"].values():
            for check in gate.values():
                for override in (check.get("overrides", []) if isinstance(check, dict) else []):
                    if override.get("registered") == "post-run":
                        signed.append(override["sign_off"])
        self.assertNotIn("pending", signed)
        self.assertEqual(sum(1 for s in signed if s == rules.LEAD_SIGN_OFF), 2)          # J1 Bullet ball, G4 Bullet model 2

    def test_restitution_threshold_rule_needs_another_threshold(self):
        """M9 fix round 0: the by-design threshold rule tags a bounce count only when the run counts above another threshold than the
        engines that set the consensus; with the same threshold the count is the engines' restitution (algorithmic), and Jolt's exact
        model-0 rebounds make its rule verified."""
        desc = make_desc("M7.UEDefaults", [body(0, "static", {"type": "box", "half_extent": [1000.0, 1000.0, 50.0]}),
                                           body(1, "dynamic", {"type": "box", "half_extent": [25.0, 25.0, 25.0]})],
                         materials=[{"index": 0, "name": "m", "friction": 0.7, "static_friction": 0.7, "restitution": 0.3, "friction_combine": "Average",
                                     "restitution_combine": "Average"}])

        def sequence(approaches, threshold, e):
            return [{"frame": 10 * k, "approach": a, "rebound": e * a, "e": e, "above": a > threshold + bh.THRESHOLD_BAND_CMS,
                     "counted": a > threshold + bh.THRESHOLD_BAND_CMS and e * a > 1.0} for k, a in enumerate(approaches)]

        runs, results = [], {}
        for label, threshold, count, approaches, e in (("jolt.Default", 16.7, 4, (764.6, 195.4, 72.0, 27.4, 8.1), 0.3),
                                                       ("bullet.Default", 16.7, 3, (764.6, 211.7, 19.1, 0.2), 0.3),
                                                       ("physx.UE4Compat", 200.0, 2, (764.6, 206.8, 20.3), 0.31)):
            run = make_run("M7.UEDefaults", {}, desc, make_states(np.zeros((3, 2, 3))), label=label, profile="UEParity",
                           effective={"contact.restitution_threshold": (threshold, f"{threshold} cm/s")})
            runs.append(run)
            results[id(run)] = {"metrics": {"b1.box.bounces": count}, "details": {"bounce_sequences": {"b1.box": sequence(approaches, threshold, e)}}}
        differences = bh.tag_differences(runs, results, bh.build_observations(runs, results))
        by_label = {r["label"]: r for r in differences.to_dict("records")}
        self.assertEqual(set(by_label), {"jolt.Default", "physx.UE4Compat"})          # the consensus 3 is Bullet's
        physx = by_label["physx.UE4Compat"]
        self.assertEqual((physx["rule"], physx["tag"]), ("restitution-threshold", "by-design"))
        jolt = by_label["jolt.Default"]
        self.assertEqual((jolt["rule"], jolt["tag"], jolt["confidence"]), ("jolt-restitution-low-speed", "algorithmic", "verified"))
        self.assertIn("bullet.Default", jolt["reason"])

    def test_confidence_follows_the_branch(self):
        """M9 fix round 0: a rule's reason may carry the confidence of its branch (bullet-chain: M6c verified, M6b inferred). M9 fix
        round 1: the M6c branch needs its signature (the joints apart: stretch beyond one link) and its energy branch is inferred."""
        class ChainContext:
            def __init__(self, stretch):
                self.stretch, self.runs, self.results = stretch, [], {}

            def metric_value(self, diff, metric):
                return {"stretch_max_cm": self.stretch, "energy_growth_pct": -0.02}.get(metric)

            def detail(self, diff, name):
                return 40.0 if name == "link_length_cm" else None

            def result_of_label(self, diff, label):
                return {"metrics": {"stretch_max_cm": {"jolt.Default": 10.8, "physx.UE4Compat": 11.6}.get(label, self.stretch)}}

        for scenario, metric, stretch, expected in (("M6b.Chain", "stretch_max_cm", 20.0, "inferred"), ("M6c.HeavyChain", "stretch_max_cm", 1135.0, "verified"),
                                                    ("M6c.HeavyChain", "energy_growth_pct", 1135.0, "inferred")):
            diff = {"backend": "Bullet", "label": "bullet.Default", "scenario": scenario, "metric": metric, "profile": "Normalized",
                    "scene": scenario, "value": 20.0}
            reason, confidence = rules._rule_bullet_chain(diff, ChainContext(stretch))
            self.assertEqual(confidence, expected, reason)
        held = {"backend": "Bullet", "label": "bullet.Default", "scenario": "M6c.HeavyChain", "metric": "stretch_max_cm", "profile": "Normalized",
                "scene": "M6c.HeavyChain", "value": 20.0}
        self.assertIsNone(rules._rule_bullet_chain(held, ChainContext(20.0)))           # joints together: not this mechanism
        # tag_with_confidence takes a branch's confidence over the rule's default, and a plain text keeps the default
        saved = rules.RULES[:]
        try:
            rules.RULES[:] = [rules.Rule("branchy", "algorithmic", "verified", "test", lambda d, c: ("why", "inferred")),
                              rules.Rule("plain", "algorithmic", "verified", "test", lambda d, c: "why")]
            self.assertEqual(rules.tag_with_confidence({}, None)[2], "inferred")
            rules.RULES[:] = rules.RULES[1:]
            self.assertEqual(rules.tag_with_confidence({}, None)[2], "verified")
        finally:
            rules.RULES[:] = saved

    def test_tolerance_lookup(self):
        self.assertEqual(rules.tolerance_for("M5.BouncingBalls", "e0.9.e_dev_model_max")[0], "M5.e_dev_model_max")
        self.assertEqual(rules.tolerance_for("M7.UEDefaults", "b1.box.bounces")[0], "M7.bounces")
        self.assertEqual(rules.tolerance_for("M2a.Tower", "rest_penetration_cm")[0], "*.rest_penetration_cm")
        self.assertIsNone(rules.tolerance_for("M4.InclineFriction", "creep_cms"))

    def _pile(self, with_shape: bool):
        """M3-like pile cells without Perturb replicas: Jolt 922 (its 2 cm slop x 15 contacts), Bullet collapsed (50), PhysX standing at
        947.7 - beyond the signed 20 cm M3 tolerance (M9 commit), within the slop band."""
        desc = make_desc("M3.BoxPile", [body(0, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]})], params={"N": "1000"})
        runs, results = [], {}
        for label, top in (("jolt.Default", 922.0), ("bullet.Default", 50.0), ("physx.UE4Compat", 947.7)):
            run = make_run("M3.BoxPile", {"N": "1000"}, desc, make_states(np.zeros((3, 1, 3))), label=label, chaotic=True,
                           effective={"jolt.penetration_slop": (2.0, "0.02 m")} if label.startswith("jolt") else {})
            runs.append(run)
            details = {"ideal_top_z_cm": 950.0, "layers": 15, "ideal_mean_z_cm": 500.0, "mean_layers_below": 5.5} if with_shape else {}
            results[id(run)] = {"metrics": {"top_z_final_cm": top}, "details": details}
        return runs, results

    def test_chaotic_rule_needs_replicas_and_slop_consensus(self):
        """M9 fix round 1: in a chaotic cell without Perturb replicas the sensitivity is unknown - chaotic-solver makes no claim; a standing
        stack measured against Jolt's slop-lowered consensus is the by-design rule stack-consensus-slop."""
        runs, results = self._pile(with_shape=True)
        differences = bh.tag_differences(runs, results, bh.build_observations(runs, results))
        physx = differences[differences["label"] == "physx.UE4Compat"].iloc[0]
        self.assertEqual((physx["rule"], physx["tag"]), ("stack-consensus-slop", "by-design"))
        self.assertIn("jolt.Default 922", physx["reason"])
        runs, results = self._pile(with_shape=False)
        differences = bh.tag_differences(runs, results, bh.build_observations(runs, results))
        physx = differences[differences["label"] == "physx.UE4Compat"].iloc[0]
        self.assertEqual(physx["rule"], "unexplained")                                   # not chaotic-solver: no replicas, no claim

    def test_stack_shape_details(self):
        """M9 fix round 1: layers, the overlap-free height and the bodies off their slot of a stack (analyze_stack details)."""
        cube = {"type": "box", "half_extent": [50.0, 50.0, 50.0]}
        initial = [[0.0, 0.0, 100.0], [0.0, 0.0, 250.0], [300.0, 0.0, 100.0]]
        desc = make_desc("M3.BoxPile", [body(i, "dynamic", cube, pose=(IDENTITY, p)) for i, p in enumerate(initial)], params={"N": "3"})
        position = np.array([initial, [[0.0, 0.0, 50.0], [0.0, 0.0, 150.0], [300.0, 0.0, 50.0]], [[0.0, 0.0, 50.0], [0.0, 0.0, 150.0], [900.0, 0.0, 50.0]]])
        velocity = np.zeros((3, 3, 3))
        velocity[2, 2] = [500.0, 0.0, 0.0]
        run = make_run("M3.BoxPile", {"N": "3"}, desc, make_states(position, velocity=velocity), chaotic=True, windows={"impact": (0, 0)})
        details = bh.analyze_stack(run)["details"]
        self.assertEqual((details["layers"], details["ideal_top_z_cm"], details["far_bodies"], details["first_far_frame"]), (2, 150.0, 1, 2))
        self.assertAlmostEqual(details["speed_max_after_impact_cms"], 500.0)

    def test_round2_guards_do_not_explain_regressions(self):
        """M9 fix round 2 (checker r2): the four rules that still claimed their mechanism without looking at it now check it; a
        regression-shaped difference gets None (it stays unexplained) and a branch whose mechanism is not isolated in the data is
        inferred."""
        class FakeRun:
            def __init__(self, label, profile, scenario, scene, perturb=0, params=None, dt=DT, g=980.0):
                self.label, self.profile, self.scenario, self.scene, self.perturb = label, profile, scenario, scene, perturb
                self.params, self.dt, self.g = params or {}, dt, g

        class FakeContext:
            """Just what the four rules look up (behavior.Context's methods of the same names)."""
            def __init__(self):
                self.runs, self.results, self.index = [], {}, {}
                self.probes, self.effective_values, self.balls, self.main = [], {}, {}, []

            def add(self, run, metrics, details=None):
                self.runs.append(run)
                self.results[id(run)] = {"metrics": metrics, "details": details or {}}
                self.index[(run.label, run.profile, run.scene)] = run
                return id(run)

            def run(self, diff):
                return next(r for r in self.runs if id(r) == diff["run_key"])

            def metric_value(self, diff, metric):
                value = self.results[diff["run_key"]]["metrics"].get(metric)
                return None if value is None else float(value)

            def detail(self, diff, name):
                return self.results[diff["run_key"]]["details"].get(name)

            def effective(self, diff, key):
                return self.effective_values.get(key)

            def ball(self, diff):
                return self.balls.get(diff["run_key"])

            def probe_outcomes(self, diff):
                return [(diff["label"], float(diff["value"]))] + self.probes if self.probes else []

            def result_of_label(self, diff, label):
                run = self.index.get((label, diff["profile"], diff["scene"]))
                return None if run is None else self.results.get(id(run))

            def main_values(self, diff):
                return self.main

        def difference(run_key, label, backend, profile, scenario, scene, metric, value, reference, tolerance):
            return {"run_key": run_key, "label": label, "backend": backend, "profile": profile, "scenario": scenario, "scene": scene,
                    "metric": metric, "value": value, "reference": reference, "tolerance": tolerance, "effective_tolerance": tolerance}

        # impact-phase-depth: every impact inside [-d, max(travel - d, 0)] -> algorithmic / inferred; a ball that sinks 40 cm -> None
        ctx = FakeContext()
        ctx.effective_values["contact.speculative_distance"] = 2.0
        key = ctx.add(FakeRun("jolt.Default", "Normalized", "M5.BouncingBalls", "M5.BouncingBalls"), {"e0.7.depth_max_cm": 3.0})
        diff = difference(key, "jolt.Default", "Jolt", "Normalized", "M5.BouncingBalls", "M5.BouncingBalls", "e0.7.depth_max_cm", 3.0, 0.0, 1.0)
        ctx.balls[key] = {"impacts": [{"frame": 40, "speed_before": 600.0, "depth_max_cm": 3.0}, {"frame": 90, "speed_before": 300.0, "depth_max_cm": -1.5}]}
        reason, confidence = rules._rule_impact_phase_depth(diff, ctx)
        self.assertEqual(confidence, "inferred", reason)
        self.assertEqual(next(r for r in rules.RULES if r.id == "impact-phase-depth").tag, "algorithmic")
        ctx.balls[key]["impacts"][0]["depth_max_cm"] = 40.0
        self.assertIsNone(rules._rule_impact_phase_depth(dict(diff, value=40.0), ctx))
        ctx.balls[key]["impacts"][0]["depth_max_cm"] = -10.0                              # stopped 10 cm above the pad: not within d
        self.assertIsNone(rules._rule_impact_phase_depth(dict(diff, value=-10.0), ctx))

        # bullet-p2p-rows: stretch beyond 0.5 cm and the hinge's, a longer period; a 0.001 cm stretch or a shorter period -> None
        ctx = FakeContext()
        ball_scene, hinge_scene = "M6a.Pendulum[Angle=5,Joint=Ball]", "M6a.Pendulum[Angle=5,Joint=Hinge]"
        ctx.add(FakeRun("bullet.Default", "Normalized", "M6a.Pendulum", hinge_scene, params={"Joint": "Hinge"}), {"stretch_max_cm": 0.005})
        key = ctx.add(FakeRun("bullet.Default", "Normalized", "M6a.Pendulum", ball_scene, params={"Joint": "Ball"}), {"stretch_max_cm": 2.345})
        diff = difference(key, "bullet.Default", "Bullet", "Normalized", "M6a.Pendulum", ball_scene, "period_err_pct", 8.7, 0.0, 0.5)
        self.assertIn("2.35 cm", rules._rule_bullet_p2p(diff, ctx))
        self.assertIsNone(rules._rule_bullet_p2p(dict(diff, value=-5.0), ctx))           # a shorter period is not a longer rod
        ctx.results[key]["metrics"]["stretch_max_cm"] = 0.001
        self.assertIsNone(rules._rule_bullet_p2p(dict(diff, value=5.0), ctx))            # the anchor holds: not the p2p rows

        # bullet-stack-solver: verified only where R0Iter50 holds and each alone does not; the pile must have fallen
        ctx = FakeContext()
        key = ctx.add(FakeRun("bullet.Default", "Normalized", "M3.BoxPile", "M3.BoxPile[N=1000]"), {"toppled_fraction": 0.3})
        diff = difference(key, "bullet.Default", "Bullet", "Normalized", "M3.BoxPile", "M3.BoxPile[N=1000]", "toppled_fraction", 0.3, 0.0, 0.05)
        ctx.probes = [("bullet.R0Iter50", 0.0), ("bullet.R0", 0.32), ("bullet.Iter50", 0.09)]
        self.assertEqual(rules._rule_bullet_stack(diff, ctx)[1], "verified")
        ctx.probes = [("bullet.R0Iter50", 0.25), ("bullet.R0", 0.32), ("bullet.Iter50", 0.09)]   # both changes together still collapse
        reason, confidence = rules._rule_bullet_stack(diff, ctx)
        self.assertEqual(confidence, "inferred")
        self.assertNotIn("isolate the mechanism", reason)
        ctx.probes = []
        self.assertEqual(rules._rule_bullet_stack(diff, ctx)[1], "inferred")               # no probe run
        self.assertIsNone(rules._rule_bullet_stack(dict(diff, value=0.0, reference=0.3), ctx))   # less toppled than the consensus

        # jolt-heavy-chain: Normalized needs the energy bound and a stretch below one link; UEParity needs every run apart and the
        # jolt.Damp0 probe equal to the Normalized run
        ctx = FakeContext()
        scene = "M6c.HeavyChain[Perturb=0]"
        normalized = ctx.add(FakeRun("jolt.Default", "Normalized", "M6c.HeavyChain", scene), {"stretch_max_cm": 35.3, "energy_growth_pct": -0.03},
                             {"link_length_cm": 40.0, "joints_separated": False})
        diff = difference(normalized, "jolt.Default", "Jolt", "Normalized", "M6c.HeavyChain", scene, "stretch_max_cm", 35.3, 0.0, 1.0)
        self.assertEqual(rules._rule_jolt_heavy_chain(diff, ctx)[1], "inferred")
        ctx.results[normalized]["metrics"]["energy_growth_pct"] = 500.0
        self.assertIsNone(rules._rule_jolt_heavy_chain(diff, ctx))                         # energy +500 %: not "without diverging"
        ctx.results[normalized]["metrics"]["energy_growth_pct"] = -0.03
        ue = ctx.add(FakeRun("jolt.Default", "UEParity", "M6c.HeavyChain", scene), {"stretch_max_cm": 3400.0}, {"link_length_cm": 40.0, "joints_separated": True})
        replica = ctx.add(FakeRun("jolt.Default", "UEParity", "M6c.HeavyChain", "M6c.HeavyChain[Perturb=1]", perturb=1), {"stretch_max_cm": 3300.0},
                          {"link_length_cm": 40.0, "joints_separated": True})
        probe = ctx.add(FakeRun("jolt.Damp0", "UEParity", "M6c.HeavyChain", scene), {"stretch_max_cm": 35.3}, {"link_length_cm": 40.0})
        diff = difference(ue, "jolt.Default", "Jolt", "UEParity", "M6c.HeavyChain", scene, "stretch_max_cm", 3400.0, 0.0, 1.0)
        self.assertEqual(rules._rule_jolt_heavy_chain(diff, ctx)[1], "verified")
        ctx.results[probe]["metrics"]["stretch_max_cm"] = 900.0
        self.assertIsNone(rules._rule_jolt_heavy_chain(diff, ctx))                         # the probe does not give the Normalized result
        ctx.results[probe]["metrics"]["stretch_max_cm"] = 35.3
        ctx.results[replica]["details"]["joints_separated"] = False
        self.assertIsNone(rules._rule_jolt_heavy_chain(diff, ctx))                         # not every run tore apart

    @staticmethod
    def _tag_cell(scenario, params, metric, values, extra=None, details=None, chaotic=False):
        """The tagged differences of one synthetic cell: one run per series with one metric (plus extra metrics / details)."""
        desc = make_desc(scenario, [body(0, "static", {"type": "box", "half_extent": [10000.0, 10000.0, 50.0]}, pose=(IDENTITY, [0.0, 0.0, -50.0])),
                                    body(1, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]}, pose=(IDENTITY, [0.0, 0.0, 150.0]))], params=params)
        runs, results = [], {}
        for label, value in values.items():
            run = make_run(scenario, params, desc, make_states(np.zeros((3, 2, 3))), label=label, chaotic=chaotic,
                           effective={"jolt.penetration_slop": (2.0, "0.02 m")} if label.startswith("jolt") else {})
            runs.append(run)
            metrics = {metric: value}
            metrics.update((extra or {}).get(label, {}))
            results[id(run)] = {"metrics": metrics, "details": dict(details or {})}
        differences = bh.tag_differences(runs, results, bh.build_observations(runs, results))
        return {(r["label"], r["metric"]): r for r in differences.to_dict("records")}

    def test_m9_close_guards_do_not_explain_regressions(self):
        """M9 close (reviewer r3): jolt-penetration-slop (by-design, verified) checks the slop's magnitude - a Jolt tower that fell or a
        box resting on its edge is not Jolt's slop - and the inferred rules that had no guard check their signature, so these
        regression-shaped differences get None (they surface as unexplained) while the M9 shapes keep their tags."""
        stack = {"ideal_top_z_cm": 950.0, "layers": 10}
        # a Jolt tower that fell (the reviewer's probe: 650 cm against two standing engines) - not the slop
        cell = self._tag_cell("M2a.Tower", {"N": "10"}, "top_z_final_cm", {"jolt.Default": 650.0, "bullet.Default": 948.0, "physx.UE4Compat": 948.5},
                              details=stack, chaotic=True)
        self.assertEqual(cell[("jolt.Default", "top_z_final_cm")]["rule"], "unexplained")
        # a standing Jolt tower 6.4 cm under the consensus: within slop x 10 contacts, standing - the slop, verified
        cell = self._tag_cell("M2a.Tower", {"N": "10"}, "top_z_final_cm", {"jolt.Default": 941.6, "bullet.Default": 948.0, "physx.UE4Compat": 948.5},
                              details=stack, chaotic=True)
        slop = cell[("jolt.Default", "top_z_final_cm")]
        self.assertEqual((slop["rule"], slop["tag"], slop["confidence"]), ("jolt-penetration-slop", "by-design", "verified"))
        # a box on its edge (70 cm high) with no rest penetration (the reviewer's probe), and one that sank 10 cm - not the slop
        rest = {"bullet.Default": {"rest_penetration_cm": 0.0}, "physx.UE4Compat": {"rest_penetration_cm": 0.0}}
        for height, depth in ((70.0, 0.0), (40.0, 2.0)):
            cell = self._tag_cell("M1.SingleBoxDrop", {"Case": "a"}, "final_z_cm", {"jolt.Default": height, "bullet.Default": 50.0, "physx.UE4Compat": 50.0},
                                  extra=dict(rest, **{"jolt.Default": {"rest_penetration_cm": depth}}))
            self.assertNotEqual(cell[("jolt.Default", "final_z_cm")]["rule"], "jolt-penetration-slop", height)
        cell = self._tag_cell("M1.SingleBoxDrop", {"Case": "a"}, "final_z_cm", {"jolt.Default": 48.0, "bullet.Default": 50.0, "physx.UE4Compat": 50.0},
                              extra=dict(rest, **{"jolt.Default": {"rest_penetration_cm": 2.0}}))
        self.assertEqual(cell[("jolt.Default", "final_z_cm")]["rule"], "jolt-penetration-slop")   # M9: 48.0 against 50.0 with 2.0 cm of depth

        class Ctx:
            """What the guarded rules look up (behavior.Context's methods of the same names)."""
            def __init__(self, metrics=None, details=None, others=None, runs=()):
                self.metrics, self.details, self.others, self.runs, self.results = metrics or {}, details or {}, others or {}, list(runs), {}

            def metric_value(self, diff, metric):
                value = self.metrics.get(metric)
                return None if value is None else float(value)

            def detail(self, diff, name):
                return self.details.get(name)

            def effective(self, diff, key):
                return "10 / 1"

            def result_of_label(self, diff, label):
                return self.others.get(label)

            def detail_of_main(self, diff, name):
                return "-"

            def run(self, diff):
                return self.runs[0]

            def mu(self, diff):
                return 0.6

        def diff(label, backend, scenario, metric, value, reference=0.0, **more):
            return dict({"label": label, "backend": backend, "scenario": scenario, "scene": scenario, "metric": metric, "profile": "Normalized",
                         "value": value, "reference": reference, "tolerance": 1.0, "effective_tolerance": 1.0, "ref_kind": "analytic"}, **more)

        # chain-iterative-joints: a chain that held (10 cm, no growth) - yes; energy +500 % (the reviewer's probe) or torn apart - None
        chain = {"link_length_cm": 40.0}
        self.assertIsNotNone(rules._rule_chain_iterative_joints(diff("jolt.Default", "Jolt", "M6b.Chain", "stretch_max_cm", 10.0),
                                                                Ctx({"stretch_max_cm": 10.0, "energy_growth_pct": -0.02}, chain)))
        self.assertIsNone(rules._rule_chain_iterative_joints(diff("jolt.Default", "Jolt", "M6b.Chain", "energy_growth_pct", 500.0),
                                                             Ctx({"stretch_max_cm": 10.0, "energy_growth_pct": 500.0}, chain)))
        self.assertIsNone(rules._rule_chain_iterative_joints(diff("jolt.Default", "Jolt", "M6b.Chain", "stretch_max_cm", 900.0),
                                                             Ctx({"stretch_max_cm": 900.0, "energy_growth_pct": -0.02}, chain)))
        # bullet-chain M6b: Bullet must stretch most and hold
        others = {"jolt.Default": {"metrics": {"stretch_max_cm": 10.8}}, "physx.UE4Compat": {"metrics": {"stretch_max_cm": 23.1}}}
        self.assertIsNone(rules._rule_bullet_chain(diff("bullet.Default", "Bullet", "M6b.Chain", "stretch_max_cm", 19.3),
                                                   Ctx({"stretch_max_cm": 19.3, "energy_growth_pct": -0.02}, chain, others)))
        others["physx.UE4Compat"]["metrics"]["stretch_max_cm"] = 11.6
        self.assertEqual(rules._rule_bullet_chain(diff("bullet.Default", "Bullet", "M6b.Chain", "stretch_max_cm", 20.3),
                                                  Ctx({"stretch_max_cm": 20.3, "energy_growth_pct": -0.02}, chain, others))[1], "inferred")

        # physx-tgs-heavy-chain: every run above the release energy - else None
        class TgsRun:
            def __init__(self, perturb):
                self.label, self.profile, self.scenario, self.perturb = "physx.UE4Compat-TGS", "Normalized", "M6c.HeavyChain", perturb

        tgs_runs = [TgsRun(0), TgsRun(1)]
        ctx = Ctx(runs=tgs_runs)
        ctx.results = {id(tgs_runs[0]): {"metrics": {"energy_growth_pct": 21.0}}, id(tgs_runs[1]): {"metrics": {"energy_growth_pct": 1077.0}}}
        heavy = diff("physx.UE4Compat-TGS", "PhysX", "M6c.HeavyChain", "stretch_max_cm", 25.5)
        self.assertIsNotNone(rules._rule_physx_tgs_heavy_chain(heavy, ctx))
        ctx.results[id(tgs_runs[1])]["metrics"]["energy_growth_pct"] = 0.2
        self.assertIsNone(rules._rule_physx_tgs_heavy_chain(heavy, ctx))

        # physx-tgs-pendulum: a large swing that gains amplitude, with a longer period - else None
        swing = {"amplitude_deg": 90.0, "damping_decay_pct_per_period": 0.0}
        pendulum = diff("physx.UE4Compat-TGS", "PhysX", "M6a.Pendulum", "period_err_pct", 3.5)
        self.assertIsNotNone(rules._rule_physx_tgs_pendulum(pendulum, Ctx({"amplitude_decay_pct_per_period": -4.6}, swing)))
        self.assertIsNone(rules._rule_physx_tgs_pendulum(pendulum, Ctx({"amplitude_decay_pct_per_period": 9.0}, swing)))     # loses it
        self.assertIsNone(rules._rule_physx_tgs_pendulum(dict(pendulum, value=-3.5), Ctx({"amplitude_decay_pct_per_period": -4.6}, swing)))

        # physx-pgs-creep (accepted D-4): a creep within G5's registered 3 cm/s and a drift within the signed M4 6 cm - else None
        # (M9 close, verifier: with the M4 value signed, a PGS drift difference lies beyond it and must surface)
        never = diff("physx.UE4Compat", "PhysX", "M4.InclineFriction", "time_to_sleep_s", math.inf)
        self.assertIsNotNone(rules._rule_physx_pgs_creep(never, Ctx({"creep_cms": 2.503, "hold_drift_cm": 4.97})))
        self.assertIsNone(rules._rule_physx_pgs_creep(never, Ctx({"creep_cms": 30.0, "hold_drift_cm": 60.0})))
        slid = diff("physx.UE4Compat", "PhysX", "M4.InclineFriction", "hold_drift_cm", 60.0)
        self.assertIsNone(rules._rule_physx_pgs_creep(slid, Ctx({"creep_cms": 30.0, "hold_drift_cm": 60.0})))
        self.assertIsNone(rules._rule_physx_pgs_creep(dict(slid, value=7.0), Ctx({"creep_cms": 2.9, "hold_drift_cm": 7.0})))

        # physx-tgs-creep: under 0.2 cm/s within the registered drift - else None
        creep = diff("physx.UE4Compat-TGS", "PhysX", "M4.InclineFriction", "hold_drift_cm", 0.16)
        self.assertIsNotNone(rules._rule_physx_tgs_creep(creep, Ctx({"creep_cms": 0.073, "hold_drift_cm": 0.16})))
        self.assertIsNone(rules._rule_physx_tgs_creep(dict(creep, value=5.0), Ctx({"creep_cms": 2.5, "hold_drift_cm": 5.0})))

        # bullet-sliding-box: Bullet's box rocks (|w| >= 2x the others') and lags - else None
        slide = diff("bullet.Default", "Bullet", "M4.InclineFriction", "d2s_err_pct", -1.9)
        rocking = {"jolt.Default": {"details": {"angular_speed_max": 2e-4}}, "physx.UE4Compat": {"details": {"angular_speed_max": 0.05}}}
        self.assertIsNotNone(rules._rule_bullet_sliding_box(slide, Ctx(details={"angular_speed_max": 0.42}, others=rocking)))
        self.assertIsNone(rules._rule_bullet_sliding_box(dict(slide, value=3.0), Ctx(details={"angular_speed_max": 0.42}, others=rocking)))   # runs ahead
        self.assertIsNone(rules._rule_bullet_sliding_box(slide, Ctx(details={"angular_speed_max": 0.06}, others=rocking)))                   # no rocking

        # bullet-tumble / edge-landing: the box ends on a face within one edge length of the consensus - else None
        cube = make_desc("M1.SingleBoxDrop", [body(1, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]})])
        landing_run = make_run("M1.SingleBoxDrop", {"Case": "b"}, cube, make_states(np.zeros((2, 1, 3))), label="bullet.Default")
        landing = diff("bullet.Default", "Bullet", "M1.SingleBoxDrop", "final_xy_cm", 15.9, reference=44.6)
        self.assertIsNotNone(rules._rule_bullet_tumble(landing, Ctx({"final_tilt_deg": 0.0}, runs=[landing_run])))
        self.assertIsNone(rules._rule_bullet_tumble(landing, Ctx({"final_tilt_deg": 45.0}, runs=[landing_run])))                   # on its edge
        self.assertIsNone(rules._rule_bullet_tumble(dict(landing, value=400.0), Ctx({"final_tilt_deg": 0.0}, runs=[landing_run])))  # thrown off
        self.assertIsNone(rules._landing_guard(dict(landing, value=400.0), Ctx({"final_tilt_deg": 0.0}, runs=[landing_run])))

        # chaotic-solver speaks of aggregate outcomes against the consensus only, never of an analytic bound
        self.assertIsNone(rules._rule_chaotic(dict(diff("jolt.Default", "Jolt", "M6b.Chain", "energy_growth_pct", 500.0), chaotic=True), Ctx()))

    def test_criteria_follow_the_lead_decisions(self):
        """M9 commit (lead 2026-10-22): the M6a ball variant is reported, not gated; PhysX's M6c energy growth is a failure labelled
        accepted engine behaviour only where its mechanism's rule tags it (joints apart, energy far above the release energy); a chain
        whose joints came apart is a finding, not a pass."""
        runs, results = [], {}
        desc = make_desc("M6a.Pendulum", [body(0, "dynamic", {"type": "sphere", "radius": 5.0})])
        for label, error in (("jolt.Default", -0.012), ("bullet.Default", 8.7), ("physx.UE4Compat", -0.011)):
            for joint in ("Hinge", "Ball"):
                run = make_run("M6a.Pendulum", {"Angle": "5", "Joint": joint}, desc, make_states(np.zeros((3, 1, 3))), label=label)
                runs.append(run)
                results[id(run)] = {"metrics": {"period_err_pct": error if joint == "Ball" else -0.01}, "details": {}}

        def chains(physx_stretch):
            out_runs, out_results = list(runs), dict(results)
            chain_desc = make_desc("M6c.HeavyChain", [body(0, "dynamic", {"type": "sphere", "radius": 5.0})])
            for label, growth, stretch in (("jolt.Default", (-0.03, -0.03), 35.0), ("bullet.Default", (-0.03, -0.03), 1135.0),
                                           ("physx.UE4Compat", (4357.0, 13900.0), physx_stretch)):
                for perturb, value in enumerate(growth):
                    run = make_run("M6c.HeavyChain", {"Perturb": str(perturb)}, chain_desc, make_states(np.zeros((3, 1, 3))), label=label, chaotic=True)
                    out_runs.append(run)
                    out_results[id(run)] = {"metrics": {"energy_growth_pct": value, "stretch_max_cm": stretch},
                                            "details": {"link_length_cm": 40.0, "joints_separated": stretch > 40.0}}
            observations = bh.build_observations(out_runs, out_results)
            differences = bh.tag_differences(out_runs, out_results, observations)
            return {(row["id"], row["series"]): row for row in bh.criteria(observations, differences, [], out_runs, out_results, [])}

        verdicts = chains(989.6)
        self.assertEqual(verdicts[("M6a.period", "bullet.Default")]["verdict"], "pass")
        self.assertEqual(verdicts[("M6a.period.ball", "bullet.Default")]["verdict"], "reported (beyond 0.5%)")
        self.assertEqual(verdicts[("M6a.period.ball", "jolt.Default")]["verdict"], "reported")
        self.assertEqual(verdicts[("M6c.energy", "physx.UE4Compat")]["verdict"], "fail (accepted engine behaviour)")
        self.assertIn("physx-pgs-heavy-chain", verdicts[("M6c.energy", "physx.UE4Compat")]["detail"])
        self.assertEqual(verdicts[("M6c.energy", "bullet.Default")]["verdict"], "finding: joints separated")
        self.assertEqual(verdicts[("M6c.energy", "jolt.Default")]["verdict"], "pass")
        # the same energy growth with the joints held is not PX-1's mechanism: a plain failure
        self.assertEqual(chains(12.0)[("M6c.energy", "physx.UE4Compat")]["verdict"], "fail")

    def test_wasserstein_compares_single_runs(self):
        """M9 fix round 1: the cross-engine W1 is the mean over pairs of single runs (as the own W1), not the W1 of pooled replicas."""
        from scipy.stats import wasserstein_distance

        desc = make_desc("M2a.Tower", [body(i, "dynamic", {"type": "box", "half_extent": [50.0, 50.0, 50.0]}) for i in range(3)], params={"N": "3"})
        heights = {"jolt.Default": ([0.0, 1.0, 2.0], [0.0, 1.0, 3.0]), "bullet.Default": ([5.0, 6.0, 7.0], [5.0, 6.0, 9.0])}
        runs, results = [], {}
        for label, members in heights.items():
            for perturb, z in enumerate(members):
                position = np.zeros((2, 3, 3))
                position[:, :, 2] = z
                run = make_run("M2a.Tower", {"N": "3", "Perturb": str(perturb)}, desc, make_states(position), label=label, chaotic=True)
                runs.append(run)
                results[id(run)] = {"metrics": {}, "details": {}}
        row = bh.wasserstein_table(runs, results, [])[0]
        expected = np.mean([wasserstein_distance(a, b) for a in heights["jolt.Default"] for b in heights["bullet.Default"]])
        self.assertAlmostEqual(row["w_cross_cm"], float(expected))


class BehaviourMatrixTests(unittest.TestCase):
    def test_scene_bench_args(self):
        document = {"bench_args": ["-RunMode=Behavior"], "targets": [{"label": "jolt", "backend": "Jolt", "bench_args": ["-ini:PhysicsTest:[S]:K=1"]}],
                    "profiles": ["Normalized"], "scenes": [{"scenario": "M4.InclineFriction", "params": {"Mu": "0.30"}, "bench_args": ["-StatesEvery=1"]},
                                                           {"scenario": "M3.BoxPile", "bench_args": ["-StatesEvery=100"]}]}
        matrix_module.check_switches(document)
        cells = matrix_module.expand_cells(document)
        self.assertIn("-StatesEvery=1", cells[0].bench_args(include_target_args=False))
        self.assertEqual(cells[1].bench_args()[-2:], ["-StatesEvery=100", "-ini:PhysicsTest:[S]:K=1"])
        self.assertEqual(cells[0].as_json()["scene_bench_args"], ["-StatesEvery=1"])
        for bad, key in ((dict(document, bench_args=["-StatesEvery=1"]), "statesevery"),
                         (dict(document, scenes=[{"scenario": "M5.BouncingBalls", "bench_args": ["-Frames=10"]}]), "frames")):
            with self.assertRaisesRegex(matrix_module.MatrixError, key):
                matrix_module.check_switches(bad)

    def test_behavior_matrix_file(self):
        document = common.read_json(common.matrices_dir() / "behavior.json")
        purpose = common.read_json(common.schemas_dir() / "matrix.schema.json")["properties"]["purpose"]["enum"]
        matrix_module.check_switches(document)
        cells = matrix_module.expand_cells(document)
        self.assertEqual(len({c.cell_id for c in cells}), len(cells))
        self.assertTrue(all(any(a.startswith("-StatesEvery=") for a in c.bench_args()) for c in cells))
        scenes = {c.scenario for c in cells}
        self.assertEqual(scenes, {"M1.SingleBoxDrop", "M2a.Tower", "M3.BoxPile", "M4.InclineFriction", "M5.BouncingBalls", "M6a.Pendulum", "M6b.Chain",
                                  "M6c.HeavyChain", "M7.UEDefaults"})
        self.assertTrue({"jolt.Default", "bullet.Default", "physx.UE4Compat"} <= {c.label for c in cells})
        if "behavior" not in purpose:
            self.skipTest("Config/Schemas/matrix.schema.json has no purpose 'behavior' yet (M9 shared-file request): structure checked, schema not")
        self.assertEqual(common.schema_errors(document, "matrix.schema.json"), [])
        self.assertEqual(matrix_module.load_matrix("behavior").purpose, "behavior")


if __name__ == "__main__":
    unittest.main()
