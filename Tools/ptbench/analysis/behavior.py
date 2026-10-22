"""behavior - the behaviour analysis of M9 (02_Architecture 13 "behavior"; 03_WorkPlan M9 task 5 and its completion criteria).

    python -I -m ptbench behavior <session | run dirs ...> [--out DIR] [--json FILE]

Inputs are behaviour runs: -RunMode=Behavior with -StatesEvery (states.ptst + scene_desc.json, docs/ResultsSchema.md 1, 6),
usually a session of Config/Matrices/behavior.json. Every number comes from the recorded body states and the harness
geometry of scene_desc.json, never from engine-side reports, so all engines are measured with one ruler. References use
dt_engine (analytic.py). Per scene:
  M1.SingleBoxDrop    fall window: COM z against the damped discrete free fall with the body's damping and the engine's
                      declared formula; first contact frame; settled: final pose, box-corner penetration, resting jitter,
                      time to sleep. And the UEParity vs Normalized table (03 M9): the fall-window difference against the
                      prediction of the profile's damping (UE 0.01, what every engine receives) and against the
                      counterfactual Jolt-native 0.05, the restitution threshold, sleep and the rest state of each profile
  M4.InclineFriction  d(t) down the slope against the discrete (a dt^2 n (n + 1) / 2) and continuous (a t^2 / 2)
                      references (with the body's damping: UEParity's 0.01), d(2 s) = frame 119; the step delay; mu >= 0.6:
                      in-plane drift and creep speed; lateral drift; SAT depth of the box in the incline. BoxYaw 45 runs
                      the same references with the box's axes off the slope (friction isotropy)
  M5.BouncingBalls    every impact: e_k = -v_n(c) / v_n(c - 1) from the normal velocity (c = the frame that left free fall,
                      consecutive contact frames merged), the engine's registered restitution velocity model
                      (tolerances.json G4: 0 = e, 1 = e (1 + g dt / |v|), PhysX, 2 = the contact state at the step start,
                      Bullet - signed off with the M9 commit; judged on the contact step), the threshold judged on that model's
                      approach speed (+2 cm/s band), the touch phase inside the step, sphere-plane depth, the first
                      bounce's apex against the discrete flight from the contact height with the model's v_after
  M6a.Pendulum        period from the zero crossings of the swing angle against Analytic::PendulumPeriod (2.00905 /
                      2.04305 / 2.37023 s), amplitude decay per period, rod stretch, out-of-plane motion (hinge)
  M6b / M6c chains    joint stretch (distance of the two joint frames' world origins), energy growth and late slope
  M2a.Tower, M3.BoxPile, chains (chaotic)
                      aggregate observables only (02 13): final heights, sway, toppled fraction, sleep; same-engine
                      sensitivity from the Perturb replicas (divergence frames, the replicas' spread) and the
                      Wasserstein distance of the final z of the bodies across engines against within one engine
  M7.UEDefaults       per body: rest depth, bounces above the threshold, sleep; the UE mass rule
Generic: penetration from the harness geometry (sphere-plane, box corner, SAT, capsule), resting jitter, time to sleep
(the first frame from which every dynamic body stays asleep), sleep in a sleep-less profile.

Comparisons: every observation has a reference - analytic (the metric is a deviation from an analytic or physical value:
0 overlap at rest, no sideways drift, ...) or the cross-engine consensus (median of the main series jolt.Default,
bullet.Default, physx.UE4Compat; 02 1: UE4Compat is PhysX's main row) - and a tolerance (behavior_rules.TOLERANCES, each with
a reason and whether it was registered before or after the data). Separate tables compare one engine with itself: UEParity
against Normalized (M1), a variant or probe against its engine's main series (bitwise state identity), and the Perturb
replicas (sensitivity). An observation outside its tolerance is a difference and gets exactly one tag - by-design / mismatch / algorithmic -
with a reason from the first matching rule (behavior_rules.RULES); a difference no rule explains is tagged mismatch,
provisional, "unexplained" (to be investigated, never silently accepted). Chaotic observables count as different only
beyond the engines' own perturbation spread. Probe series (a target with its own -ini: switches, e.g. bullet.R0Iter50) are
evidence for rules and are not tagged themselves. Every number is UNOFFICIAL (behaviour runs: no timing claims at all).
"""
from __future__ import annotations

import dataclasses
import fnmatch
import math
import pathlib
import re
from typing import Any, Iterable

import numpy as np
import pandas as pd

from .. import analytic, common
from . import geometry as geo
from . import load as load_module
from . import states as st

MAIN_SERIES = ("jolt.Default", "bullet.Default", "physx.UE4Compat")
THRESHOLD_BAND_CMS = 2.0          # impacts whose approach speed is within this of the threshold are not judged (either side)
FREE_FLIGHT_DETECT_CMS = 1.0      # tolerances.json G4 contact_detect_cms: a frame leaves free flight by more than this ...
FREE_FLIGHT_DETECT_REL = 0.01     # ... or this fraction of the speed (G4 contact_detect_rel)
SLEEP_STATE = st.SLEEPING
UE_MASSES_M7 = (37.384, 177.828, 845.897, 23.011, 109.458, 65.084)   # docs/Scenes.md 2.22 [V]: UE (rho V)^0.75 of bodies 1..6
JOLT_NATIVE_LINEAR_DAMPING = 0.05   # Jolt's BodyCreationSettings::mLinearDamping default (MotionProperties): 03 M9's example


class BehaviorError(ValueError):
    pass


# ---- one run -------------------------------------------------------------------------------------------------------------

@dataclasses.dataclass
class BehaviorRun:
    run_dir: pathlib.Path
    label: str
    manifest: dict[str, Any]
    desc: dict[str, Any]
    states: st.States
    probe: bool = False

    def __post_init__(self) -> None:
        self.effective = {e.get("key"): e for e in (self.manifest.get("effective") or [])}
        self.rotation, self.position = st.body_rotations(self.desc, self.states)

    # identity
    @property
    def backend(self) -> str:
        return str((self.manifest.get("backend") or {}).get("name") or "?")

    @property
    def variant(self) -> str:
        return str((self.manifest.get("backend") or {}).get("variant") or "")

    @property
    def profile(self) -> str:
        return str((self.manifest.get("profile") or {}).get("physics") or "?")

    @property
    def scenario(self) -> str:
        return str((self.manifest.get("scene") or {}).get("name") or self.desc.get("scenario") or "?")

    @property
    def params(self) -> dict[str, str]:
        params = {k: str(v) for k, v in (((self.manifest.get("scene") or {}).get("params")) or self.desc.get("params") or {}).items()}
        if params.get("Seed") == load_module.DEFAULT_SEED:
            params.pop("Seed")
        return params

    @property
    def perturb(self) -> int:
        try:
            return int(self.params.get("Perturb", "0"))
        except ValueError:
            return 0

    @property
    def scene(self) -> str:
        return self.scenario + common.format_params(self.params)

    @property
    def group_scene(self) -> str:
        """The scene without its Perturb parameter: the replicas of one cell share it."""
        params = {k: v for k, v in self.params.items() if k != "Perturb"}
        return self.scenario + common.format_params(params)

    @property
    def chaotic(self) -> bool:
        flag = (self.manifest.get("scene") or {}).get("chaotic")
        return bool(flag) if flag is not None else self.scenario in ("M2a.Tower", "M3.BoxPile", "M6b.Chain", "M6c.HeavyChain", "P1.SmallScene", "M2b.Pyramid1240")

    # physics facts
    @property
    def dt(self) -> float:
        return self.states.dt

    @property
    def gravity(self) -> np.ndarray:
        return st.gravity(self.desc)

    @property
    def g(self) -> float:
        return float(np.linalg.norm(self.gravity))

    def window(self, name: str) -> tuple[int, int] | None:
        for entry in ((self.manifest.get("scene") or {}).get("windows") or []):
            if entry.get("name") == name:
                return int(entry["first"]), int(entry["last"])
        return None

    def effective_value(self, key: str) -> Any:
        entry = self.effective.get(key)
        return None if entry is None else entry.get("canonical")

    def effective_text(self, key: str) -> str:
        entry = self.effective.get(key)
        return "" if entry is None else str(entry.get("native") or "")

    @property
    def damping_formula(self) -> int:
        value = self.effective_value("body.damping_formula")
        return int(value) if isinstance(value, (int, float)) and int(value) in (1, 2) else analytic.UE_ORDER

    @property
    def restitution_threshold(self) -> float | None:
        value = self.effective_value("contact.restitution_threshold")
        return float(value) if isinstance(value, (int, float)) else None

    @property
    def solver_text(self) -> str:
        return self.effective_text("solver.type")

    @property
    def sleep_enabled(self) -> bool:
        value = self.effective_value("sleep.enabled")
        return bool(value) if isinstance(value, (int, float)) else self.profile == "UEParity"

    def com(self, index: int) -> np.ndarray:
        return st.body_com(self.desc["bodies"][index], self.rotation[:, index], self.position[:, index])

    def initial_com(self, index: int) -> np.ndarray:
        body = self.desc["bodies"][index]
        q, t = st.pose_of(body["initial_pose"])
        return st.body_com(body, q, t)

    def rows(self, window: str | tuple[int, int] | None) -> np.ndarray:
        if window is None:
            return np.arange(self.states.num_frames)
        bounds = self.window(window) if isinstance(window, str) else window
        if bounds is None:
            return np.arange(self.states.num_frames)
        return self.states.rows(*bounds)


_RESTITUTION_MODELS: dict[str, Any] | None = None


def restitution_model(backend: str, variant: str) -> tuple[int, str]:
    """The registered restitution velocity model of an engine (Config/Golden/tolerances.json G4.restitution_velocity_model)."""
    global _RESTITUTION_MODELS
    if _RESTITUTION_MODELS is None:
        path = common.repo_root() / "Config" / "Golden" / "tolerances.json"
        try:
            _RESTITUTION_MODELS = common.read_json(path)["gates"]["G4"]["restitution_velocity_model"]
        except (OSError, KeyError, ValueError):
            _RESTITUTION_MODELS = {"default": 0, "overrides": [{"engine": "PhysX", "value": 1}, {"engine": "Bullet", "value": 2}],
                                   "_note": "tolerances.json unreadable: built-in default"}
    rule = _RESTITUTION_MODELS
    for override in rule.get("overrides", []):
        if override.get("engine") == backend and override.get("variant") in (None, variant):
            return int(override["value"]), "tolerances.json G4.restitution_velocity_model " + (f"{backend}.{variant}" if override.get("variant") else backend)
    return int(rule.get("default", 0)), "tolerances.json G4.restitution_velocity_model default"


# ---- generic measurements -------------------------------------------------------------------------------------------------

def time_to_sleep(run: BehaviorRun, bodies: list[int] | None = None) -> tuple[float | None, int | None]:
    """(sim time, frame) from which every listed dynamic body stays asleep to the end; (None, None) when never."""
    bodies = st.dynamic_bodies(run.desc) if bodies is None else bodies
    if not bodies:
        return None, None
    asleep = np.all(run.states.object_state[:, bodies] == SLEEP_STATE, axis=1)
    if not asleep[-1]:
        return None, None
    awake_rows = np.nonzero(~asleep)[0]
    row = int(awake_rows[-1] + 1) if awake_rows.size else 0
    return float(run.states.sim_time[row]), int(run.states.frame[row])


def sleep_speed(run: BehaviorRun, bodies: list[int] | None = None) -> float | None:
    """The fastest COM speed (cm/s) a body had in the last frame before it went to sleep for good (None when nothing sleeps
    to the end): a body put to sleep while it still moves shows an engine sleep rule that is wrong for the scene."""
    bodies = st.dynamic_bodies(run.desc) if bodies is None else bodies
    fastest = None
    for index in bodies:
        asleep = run.states.object_state[:, index] == SLEEP_STATE
        if not asleep[-1]:
            continue
        awake = np.nonzero(~asleep)[0]
        if not awake.size:
            continue
        speed = float(np.linalg.norm(run.states.linear_velocity[awake[-1], index]))
        fastest = speed if fastest is None else max(fastest, speed)
    return fastest


def sleeping_body_frames(run: BehaviorRun) -> int:
    bodies = st.dynamic_bodies(run.desc)
    return int(np.sum(run.states.object_state[:, bodies] == SLEEP_STATE)) if bodies else 0


def resting_jitter(run: BehaviorRun, rows: np.ndarray, bodies: list[int] | None = None) -> dict[str, float]:
    """RMS distance of each body's COM from its window mean (cm) and RMS speed (cm/s); the largest body's values."""
    bodies = st.dynamic_bodies(run.desc) if bodies is None else bodies
    if rows.size < 2 or not bodies:
        return {"jitter_cm": math.nan, "speed_rms_cms": math.nan}
    jitter = []
    speed = []
    for index in bodies:
        com = run.com(index)[rows]
        jitter.append(float(np.sqrt(np.mean(np.sum((com - com.mean(axis=0)) ** 2, axis=-1)))))
        v = run.states.linear_velocity[rows, index]
        speed.append(float(np.sqrt(np.mean(np.sum(v * v, axis=-1)))))
    return {"jitter_cm": max(jitter), "speed_rms_cms": max(speed)}


def static_box_shapes(run: BehaviorRun) -> list[tuple[int, dict[str, Any]]]:
    out = []
    for index in st.static_bodies(run.desc):
        for shape in run.desc["bodies"][index].get("shapes", []):
            if shape.get("type") == "box":
                out.append((index, shape))
    return out


def _within_face(center: np.ndarray, radius: float, face_center: np.ndarray, face_axes: np.ndarray, face_half: np.ndarray, normal_axis: int) -> bool:
    local = (np.asarray(center) - face_center) @ face_axes
    others = [i for i in range(3) if i != normal_axis]
    return bool(np.all(np.abs(local[..., others]) <= face_half[others] - radius))


def static_contact_depth(run: BehaviorRun, index: int, method: str = "auto") -> tuple[np.ndarray, str]:
    """Penetration depth (cm, per recorded frame, the deepest over the body's shapes and the static boxes) of dynamic body
    `index`. auto: against the top-face plane of a static box when the shape stays inside that face (sphere-plane, box
    corner, capsule-plane), else the full test (sphere-box, SAT, capsule-box); "sat": always the full test."""
    best = np.full(run.states.num_frames, -np.inf)
    methods: list[str] = []
    for shape in run.desc["bodies"][index].get("shapes", []):
        radius = geo.bounding_radius(shape)
        for static_index, static_shape in static_box_shapes(run):
            sq = run.rotation[0, static_index]
            stt = run.position[0, static_index]
            center, axes, half = geo.obb(static_shape, sq, stt)
            n, d = geo.top_face_plane(center, axes, half)
            normal_axis = int(np.argmax(np.abs(axes[2, :])))
            _, shape_center = st.shape_world_pose(shape, run.rotation[:, index], run.position[:, index])
            # Only the static boxes this shape can reach (bounding sphere against the box's bounding sphere).
            reach = np.linalg.norm(shape_center - center, axis=-1) <= radius + float(np.linalg.norm(half)) + 1.0
            if not np.any(reach):
                continue
            plane = method == "auto" and _within_face(shape_center[[0, -1]], radius, center, axes, half, normal_axis)
            if plane:
                depth, used = geo.shape_vs_plane(shape, run.rotation[:, index], run.position[:, index], n, d)
            else:
                depth, used = geo.shape_vs_static_box(shape, run.rotation[:, index], run.position[:, index], static_shape, sq, stt)
            depth = np.where(reach, depth, -np.inf)
            best = np.maximum(best, depth)
            if used not in methods:
                methods.append(used)
    return best, "+".join(methods) or "none"


def dynamic_pair_depth(run: BehaviorRun, rows: np.ndarray | None = None, max_bodies: int = 64) -> np.ndarray:
    """The deepest overlap between dynamic shapes per recorded frame (box-box SAT, spheres), pairs inside bounding reach
    only; NaN when the scene has more than max_bodies dynamic bodies (not computed)."""
    bodies = st.dynamic_bodies(run.desc)
    frames = np.arange(run.states.num_frames) if rows is None else rows
    best = np.full(frames.size, -np.inf)
    if len(bodies) > max_bodies:
        return np.full(frames.size, np.nan)
    shapes = [(b, s, geo.bounding_radius(s)) for b in bodies for s in run.desc["bodies"][b].get("shapes", [])]
    centers = {}
    for b, s, _ in shapes:
        _, centers[(b, id(s))] = st.shape_world_pose(s, run.rotation[frames, b], run.position[frames, b])
    disabled = {(j["body1"], j["body2"]) for j in run.desc.get("joints", []) if j.get("disable_collision")}
    for i in range(len(shapes)):
        bi, si, ri = shapes[i]
        for j in range(i + 1, len(shapes)):
            bj, sj, rj = shapes[j]
            if bi == bj or (bi, bj) in disabled or (bj, bi) in disabled:
                continue
            near = np.linalg.norm(centers[(bi, id(si))] - centers[(bj, id(sj))], axis=-1) <= ri + rj
            if not np.any(near):
                continue
            sel = frames[near]
            depth = geo.shape_vs_shape(si, run.rotation[sel, bi], run.position[sel, bi], sj, run.rotation[sel, bj], run.position[sel, bj])
            if depth is None:
                continue
            best[near] = np.maximum(best[near], depth)
    return best


def _finite_stat(values: np.ndarray, how: str) -> float:
    values = np.asarray(values, dtype=float)
    values = values[np.isfinite(values)]
    if values.size == 0:
        return math.nan
    return float(np.median(values) if how == "median" else np.max(values) if how == "max" else np.mean(values))


def joint_world_origins(run: BehaviorRun, joint: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    """World origins of a joint's two frames over the frames (Frame1 in Body1's actor space; Frame2 in Body2's, or a world
    pose when Body2 is the world, docs/Abi.md 9.4 3)."""
    b1 = int(joint["body1"])
    _, f1 = st.pose_of(joint["frame1"])
    p1 = st.pose_apply(run.rotation[:, b1], run.position[:, b1], f1)
    _, f2 = st.pose_of(joint["frame2"])
    b2 = int(joint.get("body2", -1))
    p2 = np.broadcast_to(f2, p1.shape) if b2 < 0 else st.pose_apply(run.rotation[:, b2], run.position[:, b2], f2)
    return p1, p2


def face_tilt_deg(q: np.ndarray) -> np.ndarray:
    """The smallest angle between world Z and any of the body's local axes (0 = a box resting on one of its faces)."""
    best = None
    for axis in np.eye(3):
        tilt = st.tilt_deg(q, axis)
        tilt = np.minimum(tilt, 180.0 - tilt)
        best = tilt if best is None else np.minimum(best, tilt)
    return best


# ---- scene analyzers: each returns {"metrics": {name: value}, "details": {...}, "series": {...}} -------------------------------

def analyze_m1(run: BehaviorRun) -> dict[str, Any]:
    box = st.dynamic_bodies(run.desc)[0]
    body = run.desc["bodies"][box]
    c = float(body.get("linear_damping") or 0.0)
    z = run.com(box)[:, 2]
    z0 = float(run.initial_com(box)[2])
    steps = run.states.step
    max_step = int(steps.max())
    gz = float(run.gravity[2])
    ref, _ = analytic.damped_free_fall_series(z0, 0.0, gz, c, run.dt, max_step, run.damping_formula)
    ref_undamped, _ = analytic.damped_free_fall_series(z0, 0.0, gz, 0.0, run.dt, max_step, run.damping_formula)
    ref_jolt_native, _ = analytic.damped_free_fall_series(z0, 0.0, gz, JOLT_NATIVE_LINEAR_DAMPING, run.dt, max_step, run.damping_formula)
    at = ref[steps - 1]
    fall = run.window("fall") or (1, 20)
    fall_rows = run.rows(fall)
    dz = z - at
    leaves = np.nonzero(np.abs(dz) > 0.05)[0]
    first_contact = int(run.states.frame[leaves[0]]) if leaves.size else None
    end_row = run.states.row_of(fall[1])
    final = -1
    q_final = run.rotation[final, box]
    com_final = run.com(box)[final]
    com_initial = run.initial_com(box)
    settled_rows = run.rows("settled")
    depth, method = static_contact_depth(run, box)
    sleep_t, sleep_f = time_to_sleep(run)
    metrics = {
        "fall_dz_max_cm": float(np.max(np.abs(dz[fall_rows]))) if fall_rows.size else math.nan,
        "first_contact_frame": first_contact,
        "final_z_cm": float(com_final[2]),
        "final_tilt_deg": float(face_tilt_deg(q_final)),
        "final_xy_cm": float(np.linalg.norm(com_final[:2] - com_initial[:2])),
        "rest_penetration_cm": _finite_stat(depth[settled_rows], "median"),
        "rest_jitter_cm": resting_jitter(run, settled_rows)["jitter_cm"],
        "time_to_sleep_s": sleep_t,
    }
    details = {
        "damping": c, "damping_formula": run.damping_formula, "fall_window": list(fall), "fall_end_frame": fall[1],
        "fall_end_step": int(steps[end_row]) if end_row is not None else None,
        "z_fall_end": float(z[end_row]) if end_row is not None else math.nan,
        "ref_fall_end": float(at[end_row]) if end_row is not None else math.nan,
        "ref_fall_end_undamped": float(ref_undamped[steps[end_row] - 1]) if end_row is not None else math.nan,
        "ref_fall_end_c005": float(ref_jolt_native[steps[end_row] - 1]) if end_row is not None else math.nan,
        "penetration_method": method, "sleep_frame": sleep_f, "sleep_time": sleep_t, "restitution_threshold_cms": run.restitution_threshold,
        "sleep_criteria": run.effective_text("sleep.criteria"), "effective_damping": run.effective_value("body.damping"),
        "effective_damping_source": (run.effective.get("body.damping") or {}).get("source"),
    }
    series = {"frame": run.states.frame[fall_rows], "z": z[fall_rows], "ref": at[fall_rows]}
    return {"metrics": metrics, "details": details, "series": series}


def _incline_frame(run: BehaviorRun) -> tuple[np.ndarray, float]:
    incline = st.static_bodies(run.desc)[0]
    axes = st.quat_to_matrix(run.rotation[0, incline])
    angle = float(np.degrees(np.arccos(np.clip(axes[2, 2], -1.0, 1.0))))
    return axes, round(angle, 4)


def analyze_m4(run: BehaviorRun) -> dict[str, Any]:
    box = st.dynamic_bodies(run.desc)[0]
    axes, angle_deg = _incline_frame(run)
    angle = math.radians(angle_deg)
    mu = float(run.params.get("Mu", "nan"))
    com = run.com(box)
    delta = com - run.initial_com(box)
    down = -(delta @ axes[:, 0])
    lateral = delta @ axes[:, 1]
    steps = run.states.step
    acceleration = analytic.incline_acceleration(mu, angle, run.g)
    damping = float(run.desc["bodies"][box].get("linear_damping") or 0.0)
    # the discrete reference with the body's damping (UEParity 0.01; Normalized 0 = the closed form of Analytic::InclineSlideDistance)
    discrete = analytic.incline_slide_distance_damped_series(mu, angle, run.g, damping, run.dt, steps, run.damping_formula)
    continuous = analytic.incline_slide_distance_continuous(mu, angle, run.g, steps * run.dt)
    window = run.window("2s") or (1, 119)
    row = run.states.row_of(window[1])
    if row is None:
        raise BehaviorError(f"{run.run_dir}: frame {window[1]} (d(2 s)) not recorded")
    depth, method = static_contact_depth(run, box, method="sat")
    win_rows = run.rows(window)
    sleep_t, sleep_f = time_to_sleep(run)
    metrics: dict[str, Any] = {"lateral_cm": float(abs(lateral[row])), "contact_penetration_cm": _finite_stat(depth[win_rows], "median"),
                               "time_to_sleep_s": sleep_t}
    details: dict[str, Any] = {"mu": mu, "angle_deg": angle_deg, "acceleration_cms2": acceleration, "frame": window[1], "steps": int(steps[row]),
                               "d_cm": float(down[row]), "d_discrete_cm": float(discrete[row]), "d_continuous_cm": float(continuous[row]),
                               "lateral_signed_cm": float(lateral[row]), "penetration_method": method, "sleep_frame": sleep_f, "sleep_time": sleep_t,
                               "box_yaw": run.params.get("BoxYaw", "0"), "damping": damping,
                               "angular_speed_max": float(np.max(np.linalg.norm(run.states.angular_velocity[win_rows, box], axis=-1))) if win_rows.size else math.nan}
    if acceleration > 0.0:
        metrics["d2s_err_pct"] = 100.0 * (down[row] - discrete[row]) / discrete[row]
        details["d2s_err_continuous_pct"] = 100.0 * (down[row] - continuous[row]) / continuous[row]
        details["step_delay"] = analytic.discrete_step_delay(float(down[row]), acceleration, run.dt, int(steps[row])) if not damping else math.nan
        early = run.states.row_of(30)
        details["err_frame30_pct"] = 100.0 * (down[early] - discrete[early]) / discrete[early] if early is not None and discrete[early] > 0 else math.nan
    else:
        in_plane = np.sqrt(down ** 2 + lateral ** 2)
        metrics["hold_drift_cm"] = float(in_plane[row])
        first = run.states.row_of(max(window[0], window[1] - 60))
        if first is not None and row > first:
            span = float(run.states.sim_time[row] - run.states.sim_time[first])
            metrics["creep_cms"] = float((in_plane[row] - in_plane[first]) / span)
        details["d_signed_cm"] = float(down[row])
    series = {"frame": run.states.frame, "d": down, "discrete": discrete, "continuous": continuous, "lateral": lateral}
    return {"metrics": metrics, "details": details, "series": series}


def _support_plane(run: BehaviorRun, index: int) -> tuple[np.ndarray, float]:
    """The top-face plane of the static box under a body's initial COM (nearest in the horizontal plane)."""
    com0 = run.initial_com(index)
    best = None
    for static_index, shape in static_box_shapes(run):
        center, axes, half = geo.obb(shape, run.rotation[0, static_index], run.position[0, static_index])
        distance = float(np.linalg.norm((com0 - center)[:2]))
        if best is None or distance < best[0]:
            best = (distance, geo.top_face_plane(center, axes, half))
    if best is None:
        raise BehaviorError(f"{run.run_dir}: no static box under body {index}")
    return best[1]


def detect_impacts(run: BehaviorRun, index: int, e_nominal: float, model: int, threshold: float | None) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    """Every impact of a falling body on its support plane: frames that leave free flight, consecutive ones merged."""
    n, d = _support_plane(run, index)
    shape = run.desc["bodies"][index]["shapes"][0]
    radius = float(shape.get("radius", 0.0))
    com = run.com(index)
    height = com @ n - d - radius                 # gap of the lowest point (sphere); negative = inside
    vn = run.states.linear_velocity[:, index] @ n
    g_n = float(-(run.gravity @ n))
    dt = run.dt
    expected = vn[:-1] - g_n * dt
    off = np.abs(vn[1:] - expected) > np.maximum(FREE_FLIGHT_DETECT_CMS, FREE_FLIGHT_DETECT_REL * np.abs(expected))
    off = np.concatenate([[False], off])
    impacts = []
    c = 1
    frames = run.states.frame
    h0 = float(run.initial_com(index) @ n - d - radius)
    contact_constants: tuple[float, float] | None = None
    while c < len(vn):
        if off[c] and vn[c - 1] < -FREE_FLIGHT_DETECT_CMS and vn[c] > vn[c - 1]:
            end = c
            while end + 1 < len(vn) and off[end + 1] and vn[end] < 0.0 and vn[end + 1] > vn[end]:
                end += 1
            before = float(-vn[c - 1])
            after = float(vn[end])
            approach = analytic.restitution_approach_speed(before, g_n, dt, model)
            judged = threshold is not None and approach > threshold + THRESHOLD_BAND_CMS
            band = threshold is not None and abs(approach - threshold) <= THRESHOLD_BAND_CMS
            v_pre = vn[c - 1] - g_n * dt
            phase = float(height[c - 1] / (-v_pre * dt)) if height[c - 1] > 0.0 and v_pre < 0.0 else 0.0
            if model == analytic.RESTITUTION_MODEL_CONTACT_STATE:
                # registered model 2 (Bullet, tolerances.json G4): the prediction needs the contact state at the step start
                if contact_constants is None:
                    contact_constants = bullet_solver_constants(run)
                e_pred = contact_state_restitution(e_nominal, float(height[c - 1]), before, dt, *contact_constants)[0] if before > 0 else math.nan
            else:
                e_pred = analytic.restitution_prediction(e_nominal, before, g_n, dt, model)
            impacts.append({"frame": int(frames[c]), "frames_in_contact": end - c + 1, "speed_before": before, "speed_after": after,
                            "e": after / before if before > 0 else math.nan, "e_step": float(vn[c]) / before if before > 0 else math.nan,
                            "e_pred": e_pred, "approach": approach,
                            "judged": bool(judged), "threshold_band": bool(band), "gap_before_cm": float(height[c - 1]),
                            "depth_max_cm": float(-np.min(height[c - 1:end + 1])), "touch_phase": phase, "row_end": end})
            c = end + 1
        else:
            c += 1
    # apexes between impacts
    for k, impact in enumerate(impacts):
        start = impact["row_end"]
        stop = impacts[k + 1]["row_end"] - impacts[k + 1]["frames_in_contact"] if k + 1 < len(impacts) else len(vn) - 1
        impact["apex_cm"] = float(np.max(height[start:stop + 1])) if stop >= start else math.nan
        impact["height_after_cm"] = float(height[start])
    return impacts, {"h0_cm": h0, "g_n": g_n, "radius": radius}


def _material_restitution(run: BehaviorRun, index: int) -> float:
    shape = run.desc["bodies"][index]["shapes"][0]
    material = run.desc["materials"][int(shape.get("material", 0))]
    return round(float(material.get("restitution", 0.0)), 6)


def analyze_m5(run: BehaviorRun) -> dict[str, Any]:
    from . import behavior_rules as rules

    model, model_source = restitution_model(run.backend, run.variant)
    threshold = run.restitution_threshold
    balls = st.dynamic_bodies(run.desc)
    # an open proposal (behavior_rules.PROPOSED_RESTITUTION_MODELS, none since the M9 commit registered Bullet's model 2) is measured
    # beside the registered model; it never passes a verdict, it only marks a failure as pending
    proposal = rules.PROPOSED_RESTITUTION_MODELS.get(run.backend)
    proposed_model = proposal["model"] if proposal and proposal["model"] != model else None
    # the contact-state model describes the contact step itself: its measured e is that step's (e_step; a contact over several frames
    # is judged on its first frame, as the model 2 fit of M9 was)
    measured_key = "e_step" if model == analytic.RESTITUTION_MODEL_CONTACT_STATE else "e"
    per_ball = []
    for index in balls:
        e_nominal = _material_restitution(run, index)
        impacts, facts = detect_impacts(run, index, e_nominal, model, threshold)
        judged = [i for i in impacts if i["judged"]]
        first = impacts[0] if impacts else None
        apex_err = math.nan
        if first is not None and facts["h0_cm"] > 0:
            v_after_model = first["e_pred"] * first["speed_before"]
            predicted, _ = analytic.discrete_flight_apex(first["height_after_cm"], v_after_model, facts["g_n"], run.dt)
            apex_err = (first["apex_cm"] - predicted) / facts["h0_cm"]
        proposed = math.nan
        if proposed_model == analytic.RESTITUTION_MODEL_CONTACT_STATE and judged:
            erp2, split = bullet_solver_constants(run)
            proposed = max(abs(i["e_step"] - contact_state_restitution(e_nominal, i["gap_before_cm"], i["speed_before"], run.dt, erp2, split)[0]) for i in judged)
        per_ball.append({
            "e_dev_proposed_max": proposed,
            "body": index, "e_nominal": e_nominal, "impacts": [{k: v for k, v in i.items() if k != "row_end"} for i in impacts],
            "n_judged": len(judged),
            "e_first": first["e"] if first else math.nan,
            "e_dev_model_max": max((abs(i[measured_key] - i["e_pred"]) for i in judged), default=math.nan),
            "e_dev_nominal_max": max((abs(i["e"] - e_nominal) for i in judged), default=math.nan),
            "apex1_ratio_err": apex_err,
            "depth_max_cm": max((i["depth_max_cm"] for i in impacts), default=math.nan),
            "last_impact_frame": impacts[-1]["frame"] if impacts else None,
            "h0_cm": facts["h0_cm"],
        })
    metrics = {}
    for ball in per_ball:
        tag = f"e{ball['e_nominal']:.1f}"
        metrics[f"{tag}.e_dev_model_max"] = ball["e_dev_model_max"]
        metrics[f"{tag}.e_dev_nominal_max"] = ball["e_dev_nominal_max"]
        if not math.isnan(ball["e_dev_proposed_max"]):
            metrics[f"{tag}.e_dev_proposed_max"] = ball["e_dev_proposed_max"]
        metrics[f"{tag}.e_first"] = ball["e_first"]
        metrics[f"{tag}.apex1_ratio_err"] = ball["apex1_ratio_err"]
        metrics[f"{tag}.judged_impacts"] = ball["n_judged"]
        metrics[f"{tag}.depth_max_cm"] = ball["depth_max_cm"]
    details = {"model": model, "model_source": model_source, "threshold_cms": threshold, "balls": per_ball}
    return {"metrics": metrics, "details": details, "series": {}}


def _swing_angle(run: BehaviorRun, pivot: np.ndarray, axis: np.ndarray, com: np.ndarray) -> np.ndarray:
    down = run.gravity / run.g
    r = com - pivot
    return np.arctan2(np.cross(down, r) @ axis, r @ down)


def zero_crossings(t: np.ndarray, x: np.ndarray) -> np.ndarray:
    """Times where x changes sign, by linear interpolation between samples (exact zeros count once)."""
    s = np.sign(x)
    out = []
    for i in range(1, len(x)):
        if s[i - 1] == 0:
            continue
        if s[i] == 0 or s[i] != s[i - 1]:
            if s[i] == 0:
                out.append(t[i])
            else:
                out.append(t[i - 1] + (t[i] - t[i - 1]) * x[i - 1] / (x[i - 1] - x[i]))
    return np.array(out)


def pendulum_period(times: np.ndarray, angle: np.ndarray) -> tuple[float, int]:
    """Period from the zero crossings: 2 (t_last - t_first) / (k - 1) over k crossings (k - 1 half periods)."""
    crossings = zero_crossings(times, angle)
    if crossings.size < 3:
        return math.nan, int(crossings.size)
    return float(2.0 * (crossings[-1] - crossings[0]) / (crossings.size - 1)), int(crossings.size)


def analyze_m6a(run: BehaviorRun) -> dict[str, Any]:
    joints = run.desc.get("joints") or []
    if not joints:
        raise BehaviorError(f"{run.run_dir}: M6a without a joint in scene_desc.json")
    joint = joints[0]
    bob = int(joint["body1"])
    f2q, pivot = st.pose_of(joint["frame2"])
    com = run.com(bob)
    com0 = run.initial_com(bob)
    length = float(np.linalg.norm(com0 - pivot))
    if joint.get("type") == "hinge":
        axis = st.quat_rotate(f2q, np.array([1.0, 0.0, 0.0]))
    else:
        axis = np.cross(com0 - pivot, run.gravity)
        axis = axis / np.linalg.norm(axis)
    angle = _swing_angle(run, pivot, axis, com)
    # the time of a recorded frame (its state is after its step); prepend the initial state at t = 0
    times = np.concatenate([[0.0], run.states.sim_time])
    angles = np.concatenate([[float(_swing_angle(run, pivot, axis, com0[None, :])[0])], angle])
    period, crossings = pendulum_period(times, angles)
    amplitude_deg = float(run.params.get("Angle", math.degrees(abs(angles[0]))))
    shape = run.desc["bodies"][bob]["shapes"][0]
    reference = analytic.pendulum_period(length, float(shape.get("radius", 0.0)), run.g, math.radians(amplitude_deg))
    # amplitude of every complete half swing (between two crossings), the peak refined by a parabola through the largest
    # sample and its neighbours; the decay per period compares the first and the last complete half swing
    zc = zero_crossings(times, angles)
    peaks = []
    for a, b in zip(zc[:-1], zc[1:]):
        idx = np.nonzero((times >= a) & (times <= b))[0]
        if idx.size < 3:
            continue
        k = int(idx[np.argmax(np.abs(angles[idx]))])
        peak = abs(float(angles[k]))
        if 0 < k < len(angles) - 1:
            y0, y1, y2 = abs(angles[k - 1]), abs(angles[k]), abs(angles[k + 1])
            curvature = y0 - 2.0 * y1 + y2
            if curvature < 0.0:
                peak = float(y1 - 0.125 * (y2 - y0) ** 2 / curvature)
        peaks.append(peak)
    decay = math.nan
    if len(peaks) >= 2 and peaks[0] > 0:
        decay = 100.0 * (1.0 - (peaks[-1] / peaks[0]) ** (2.0 / (len(peaks) - 1)))
    # M9 models for the tag rules (behavior_rules: pendulum-*), from the scene alone:
    #   damping   the bob's UE-order linear damping (v *= 1 - c dt per step) removes kinetic energy at 2c of its rate; over a period
    #             the mean kinetic energy is half the total (small swing), so the amplitude decays by 1 - exp(-c' T / 2), with
    #             c' = -ln(1 - c dt) / dt and the linear share m L^2 / (m L^2 + I) of the swing's kinetic energy
    #   projection a velocity-level joint solve (Jolt, PhysX PGS) puts the bob's velocity back on the tangent every step and drops the
    #             radial part the free step gained (v^2 dt / L): a kinetic energy loss of (w dt)^2 per step. Summed over a small
    #             swing w = a0 W cos(W t): dE / E per period = (3/2) pi^2 a0^2 dt / T, amplitude decay half of it
    #   stretch   integrating the rotation moves the anchor about (w dt)^2 L / 2 per step off the circle; a Baumgarte-only correction
    #             (Bullet: ERP 0.2, no position iterations) settles near 2 (w_max dt)^2 L (Progress M9 Bullet J-3)
    linear_damping = float(run.desc["bodies"][bob].get("linear_damping") or 0.0)
    mass = float(run.desc["bodies"][bob]["mass"])
    inertia = float(np.mean(run.desc["bodies"][bob]["inertia_diagonal"]))
    linear_share = mass * length ** 2 / (mass * length ** 2 + inertia)
    damping_rate = -math.log(max(1e-12, 1.0 - linear_damping * run.dt)) / run.dt if linear_damping > 0.0 else 0.0
    amplitude0 = math.radians(amplitude_deg)
    damping_decay = 100.0 * (1.0 - math.exp(-0.5 * damping_rate * linear_share * reference))
    projection_decay = 100.0 * 0.75 * math.pi ** 2 * amplitude0 ** 2 * run.dt / reference
    omega_max = math.sqrt(max(0.0, 2.0 * run.g * length * (1.0 - math.cos(amplitude0)) * mass / (mass * length ** 2 + inertia)))
    linearization_stretch = 2.0 * (omega_max * run.dt) ** 2 * length
    r = com - pivot
    p1, p2 = joint_world_origins(run, joint)
    metrics = {
        "period_err_pct": 100.0 * (period - reference) / reference if period == period else math.nan,
        "amplitude_decay_pct_per_period": decay,
        "stretch_max_cm": float(np.max(np.linalg.norm(p1 - p2, axis=-1))),
        "rod_length_err_max_cm": float(np.max(np.abs(np.linalg.norm(r, axis=-1) - length))),
        "out_of_plane_max_cm": float(np.max(np.abs(r @ axis))) if joint.get("type") == "hinge" else math.nan,
    }
    details = {"period_s": period, "reference_s": reference, "crossings": crossings, "length_cm": length, "amplitude_deg": amplitude_deg,
               "joint_type": joint.get("type"), "simulated": joint.get("simulated"), "linear_damping": linear_damping,
               "damping_decay_pct_per_period": damping_decay, "projection_decay_pct_per_period": projection_decay,
               "omega_max": omega_max, "linearization_stretch_cm": linearization_stretch}
    return {"metrics": metrics, "details": details, "series": {"t": times, "angle": angles}}


def analyze_chain(run: BehaviorRun) -> dict[str, Any]:
    joints = run.desc.get("joints") or []
    if not joints:
        raise BehaviorError(f"{run.run_dir}: a chain scene without joints in scene_desc.json")
    stretch = np.stack([np.linalg.norm(np.subtract(*joint_world_origins(run, j)), axis=-1) for j in joints], axis=1)
    energy = st.kinetic_and_potential_energy(run.desc, run.states)["total"]
    bodies = st.dynamic_bodies(run.desc)
    mass = sum(float(run.desc["bodies"][b]["mass"]) for b in bodies)
    span = 0.0
    coms = [run.initial_com(b) for b in bodies]
    for b in coms:
        span = max(span, float(np.max([np.linalg.norm(b - c) for c in coms])))
    scale = mass * run.g * max(span, 1.0)
    # initial energy: at rest (KE 0) in the initial pose
    e0 = sum(-float(run.desc["bodies"][b]["mass"]) * float(run.initial_com(b) @ run.gravity) for b in bodies)
    late = run.rows("late")
    slope = math.nan
    if late.size > 2:
        slope = float(np.polyfit(run.states.sim_time[late], energy[late], 1)[0]) / scale * 100.0
    metrics = {
        "stretch_max_cm": float(np.max(stretch)),
        "stretch_late_mean_cm": float(np.mean(np.max(stretch[late], axis=1))) if late.size else math.nan,
        "energy_growth_pct": 100.0 * float(np.max(energy) - e0) / scale,
        "energy_late_slope_pct_per_s": slope,
        "final_z_mean_cm": float(np.mean([run.com(b)[-1, 2] for b in bodies])),
    }
    # M9 fix round 1: the link length (median distance of neighbouring links' initial COMs): a stretch beyond it means the joints came
    # apart, and a separated chain falls freely with its energy conserved - the energy criterion then holds trivially: criteria() reports
    # it as a finding (joint separation, lead 2026-10-22), not a pass.
    link = float(np.median([np.linalg.norm(a - b) for a, b in zip(coms[:-1], coms[1:])])) if len(coms) > 1 else math.nan
    details = {"joints": len(joints), "energy_scale": scale, "simulated": [bool(j.get("simulated")) for j in joints], "link_length_cm": link,
               "joints_separated": bool(link == link and metrics["stretch_max_cm"] > link)}
    return {"metrics": metrics, "details": details, "series": {"t": run.states.sim_time, "energy": energy, "stretch_max": np.max(stretch, axis=1)}}


def analyze_stack(run: BehaviorRun) -> dict[str, Any]:
    """M2a.Tower / M3.BoxPile: aggregate observables of a chaotic stack (02 13)."""
    bodies = st.dynamic_bodies(run.desc)
    coms = np.stack([run.com(b) for b in bodies], axis=1)          # [F, N, 3]
    initial = np.stack([run.initial_com(b) for b in bodies], axis=0)
    final = coms[-1]
    tilt = np.array([float(face_tilt_deg(run.rotation[-1, b])) for b in bodies])
    top = int(np.argmax(initial[:, 2]))
    sway = np.linalg.norm(coms[:, top, :2] - initial[top, :2], axis=-1)
    sleep_t, sleep_f = time_to_sleep(run)
    metrics = {
        "top_z_final_cm": float(final[top, 2]),
        "mean_z_final_cm": float(np.mean(final[:, 2])),
        "top_sway_max_cm": float(np.max(sway)),
        "toppled_fraction": float(np.mean(tilt > 20.0)),
        "drift_xy_max_cm": float(np.max(np.linalg.norm(final[:, :2] - initial[:, :2], axis=-1))),
        "time_to_sleep_s": sleep_t,
    }
    late = run.rows("late") if run.window("late") else run.rows("settled")
    if run.states.num_frames > 20 and late.size > 1:
        metrics["rest_jitter_cm"] = resting_jitter(run, late)["jitter_cm"]
        pair = dynamic_pair_depth(run, late)
        ground = np.max(np.stack([static_contact_depth(run, b)[0][late] for b in bodies]), axis=0)
        metrics["rest_penetration_cm"] = _finite_stat(np.fmax(pair, ground), "median")   # fmax: pairs are NaN (not computed) for big piles
    details = {"bodies": len(bodies), "top_body": bodies[top], "sleep_frame": sleep_f, "sleep_time": sleep_t, "final_z": final[:, 2].tolist(),
               "recorded_frames": run.states.num_frames}
    details.update(stack_shape(run, bodies, coms, initial, top))
    return {"metrics": metrics, "details": details, "series": {"t": run.states.sim_time, "top_z": coms[:, top, 2], "top_sway": sway}}


FAR_FROM_SLOT_CM = 50.0     # a stacked body this far (xy) from where it started has left its slot (half a 100 cm box)


def stack_shape(run: BehaviorRun, bodies: list[int], coms: np.ndarray, initial: np.ndarray, top: int) -> dict[str, Any]:
    """M9 fix round 1: what the stack tag rules check (behavior_rules: stack-consensus-slop, physx-tgs-pile-ejection,
    physx-sdkdefault-pile). Layers: the distinct initial COM heights (1 cm bins); the overlap-free rest height of a column of the
    stack's cubes (the top box's full height h): h/2 + h (layer index); the bodies that left their slot (final xy drift >
    FAR_FROM_SLOT_CM), the first recorded frame with one, and the largest COM speed of a recorded frame after the impact window
    (a pile at rest or settling moves well under the fall speed; an ejected box does not)."""
    out: dict[str, Any] = {}
    levels = np.unique(np.round(initial[:, 2]))
    shape = (run.desc["bodies"][bodies[top]].get("shapes") or [{}])[0]
    if shape.get("type") == "box" and shape.get("half_extent"):
        height = 2.0 * float(shape["half_extent"][2])
        index = np.searchsorted(levels, np.round(initial[:, 2]))
        out.update(layers=int(levels.size), box_height_cm=height, ideal_top_z_cm=0.5 * height + height * float(np.max(index)),
                   ideal_mean_z_cm=float(np.mean(0.5 * height + height * index)), mean_layers_below=float(np.mean(index + 1)))
    drift = np.linalg.norm(coms[:, :, :2] - initial[None, :, :2], axis=-1)          # [F, N]
    far = np.any(drift > FAR_FROM_SLOT_CM, axis=1)
    out["far_bodies"] = int(np.sum(drift[-1] > FAR_FROM_SLOT_CM))
    out["first_far_frame"] = int(run.states.frame[int(np.argmax(far))]) if bool(np.any(far)) else None
    impact = run.window("impact")
    after = run.states.frame > (impact[1] if impact else 0)
    speed = np.linalg.norm(run.states.linear_velocity[:, bodies, :], axis=-1)
    out["speed_max_after_impact_cms"] = float(np.max(speed[after])) if bool(np.any(after)) else math.nan
    return out


def bounce_sequence(run: BehaviorRun, index: int, limit: int = 8) -> list[dict[str, Any]]:
    """The first reversals of a body's vertical velocity (falling -> rising): approach and rebound speed (cm/s), their ratio,
    and whether the approach is above the run's restitution threshold (+ THRESHOLD_BAND_CMS) - what analyze_m7 counts as a
    bounce when the rebound is also above 1 cm/s."""
    threshold = run.restitution_threshold
    vn = run.states.linear_velocity[:, index, 2]
    out = []
    for c in range(1, len(vn)):
        if vn[c - 1] < 0.0 and vn[c] > 0.0:
            approach = float(-vn[c - 1])
            rebound = float(vn[c])
            out.append({"frame": int(run.states.frame[c]), "approach": approach, "rebound": rebound, "e": rebound / approach if approach > 0.0 else math.nan,
                        "above": bool(threshold is not None and approach > threshold + THRESHOLD_BAND_CMS),
                        "counted": bool(threshold is not None and approach > threshold + THRESHOLD_BAND_CMS and rebound > 1.0)})
            if len(out) >= limit:
                break
    return out


def analyze_m7(run: BehaviorRun) -> dict[str, Any]:
    threshold = run.restitution_threshold
    settled = run.rows("settled")
    metrics: dict[str, Any] = {}
    bodies = st.dynamic_bodies(run.desc)
    masses = [float(run.desc["bodies"][b]["mass"]) for b in bodies]
    sequences: dict[str, list[dict[str, Any]]] = {}
    for index in bodies:
        shape = run.desc["bodies"][index]["shapes"][0]
        name = f"b{index}.{shape.get('type')}"
        depth, _ = static_contact_depth(run, index)
        metrics[f"{name}.rest_penetration_cm"] = _finite_stat(depth[settled], "median")
        vn = run.states.linear_velocity[:, index, 2]
        bounces = 0
        for c in range(1, len(vn)):
            if vn[c - 1] < 0.0 and vn[c] > 0.0 and threshold is not None and -vn[c - 1] > threshold + THRESHOLD_BAND_CMS and vn[c] > 1.0:
                bounces += 1
        metrics[f"{name}.bounces"] = bounces
        sequences[name] = bounce_sequence(run, index)
        metrics[f"{name}.final_tilt_deg"] = float(face_tilt_deg(run.rotation[-1, index])) if shape.get("type") == "box" else math.nan
    sleep_t, sleep_f = time_to_sleep(run)
    metrics["time_to_sleep_s"] = sleep_t
    mass_rule = (run.manifest.get("profile") or {}).get("mass_rule")
    if len(masses) == len(UE_MASSES_M7) and mass_rule in (None, "UEBodyUtils"):   # the table is UE's rule; Physical (rho V) has other masses
        metrics["mass_rule_err_kg"] = max(abs(m - ue) for m, ue in zip(masses, UE_MASSES_M7))
    return {"metrics": metrics, "details": {"masses": masses, "mass_rule": (run.manifest.get("profile") or {}).get("mass_rule"), "sleep_frame": sleep_f, "sleep_time": sleep_t,
                                            "threshold_cms": threshold, "bounce_sequences": sequences}, "series": {}}


ANALYZERS = {
    "M1.SingleBoxDrop": analyze_m1,
    "M4.InclineFriction": analyze_m4,
    "M5.BouncingBalls": analyze_m5,
    "M6a.Pendulum": analyze_m6a,
    "M6b.Chain": analyze_chain,
    "M6c.HeavyChain": analyze_chain,
    "M2a.Tower": analyze_stack,
    "M3.BoxPile": analyze_stack,
    "P1.SmallScene": analyze_stack,
    "M2b.Pyramid1240": analyze_stack,
    "M7.UEDefaults": analyze_m7,
}


# ---- loading ----------------------------------------------------------------------------------------------------------------

@dataclasses.dataclass
class Loaded:
    runs: list[BehaviorRun]
    unsupported: list[dict[str, Any]]      # cells the bench refused for a missing capability (joint scenes)
    skipped: list[dict[str, Any]]          # other runs that cannot be analysed (no states.ptst, failed, ...), with the reason
    sessions: dict[str, dict[str, Any]]
    table: pd.DataFrame                    # load.load's runs table (labels, verdicts, power plan)


def _is_probe(label: str, manifest: dict[str, Any]) -> bool:
    """A series whose label is not its engine.variant (a target with its own switches, e.g. bullet.R0Iter50)."""
    return label != load_module.default_label(manifest)


def load_runs(inputs: Iterable[str | pathlib.Path]) -> Loaded:
    loaded = load_module.load(inputs, with_frames=False)
    runs: list[BehaviorRun] = []
    unsupported: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []
    for row in loaded.runs.to_dict("records") if not loaded.runs.empty else []:
        run_dir = pathlib.Path(row["run_dir"])
        identity = {"run_dir": str(run_dir), "label": row.get("label"), "profile": row.get("profile"), "scene": row.get("scene"),
                    "outcome": row.get("outcome")}
        if row.get("exit_status") == "unsupported":
            unsupported.append(dict(identity, reason=row.get("exit_reason") or "unsupported"))
            continue
        if row.get("exit_code") != 0 or not row.get("manifest"):
            skipped.append(dict(identity, reason=f"exit.json {row.get('exit_code')} ({row.get('exit_status')}), outcome {row.get('outcome')}"))
            continue
        if not (run_dir / "states.ptst").is_file() or not (run_dir / "scene_desc.json").is_file():
            skipped.append(dict(identity, reason="no states.ptst / scene_desc.json (not a -StatesEvery run)"))
            continue
        manifest = common.read_json(run_dir / "manifest.json")
        try:
            states = st.read_states(run_dir, manifest)
            desc = st.read_scene_desc(run_dir)
        except st.StatesError as error:
            skipped.append(dict(identity, reason=str(error)))
            continue
        label = str(row.get("label") or load_module.default_label(manifest))
        runs.append(BehaviorRun(run_dir=run_dir, label=label, manifest=manifest, desc=desc, states=states, probe=_is_probe(label, manifest)))
    return Loaded(runs=runs, unsupported=unsupported, skipped=skipped, sessions=loaded.sessions, table=loaded.runs)


def analyze_run(run: BehaviorRun) -> dict[str, Any] | None:
    analyzer = ANALYZERS.get(run.scenario)
    if analyzer is None:
        return None
    result = analyzer(run)
    result["metrics"]["sleeping_body_frames"] = sleeping_body_frames(run) if not run.sleep_enabled else None
    dense = bool(run.states.num_frames > 1 and np.all(np.diff(run.states.frame) == 1))
    if run.sleep_enabled and dense:
        result["metrics"]["sleep_speed_cms"] = sleep_speed(run)
    elif not dense:
        result["metrics"].pop("time_to_sleep_s", None)   # sparse recording (e.g. -StatesEvery=100): no sleep timing
        result["details"]["sparse_recording"] = True
    return result


# ---- comparisons --------------------------------------------------------------------------------------------------------------

# Observations compared with an analytic reference of 0 (the metric is already a deviation from the reference).
ANALYTIC_METRICS = ("fall_dz_max_cm", "d2s_err_pct", "hold_drift_cm", "e_dev_model_max", "e_dev_nominal_max", "apex1_ratio_err", "period_err_pct",
                    "amplitude_decay_pct_per_period", "stretch_max_cm", "rod_length_err_max_cm", "out_of_plane_max_cm", "energy_growth_pct",
                    "mass_rule_err_kg", "sleeping_body_frames")
# ... and these too, unless the scene is chaotic: the physical reference is 0 (no overlap at rest, a box sliding straight
# down has no sideways drift, a body at rest is slower than any sleep threshold); in a chaotic stack they are consensus metrics.
ANALYTIC_UNLESS_CHAOTIC = ("rest_penetration_cm", "contact_penetration_cm", "lateral_cm", "sleep_speed_cms")
MAIN_OF_BACKEND = {"Jolt": "jolt.Default", "Bullet": "bullet.Default", "PhysX": "physx.UE4Compat"}
EFFECTIVE_KEYS_OF_INTEREST = ("solver.type", "solver.iterations", "shape.box_margin", "contact.speculative_distance", "jolt.penetration_slop",
                              "physx.length_scale", "physx.contact_offset_min", "physx.scene_flags", "sleep.criteria", "contact.restitution_threshold")


def _metric_base(metric: str) -> str:
    return metric.split(".")[-1]


def _value(value: Any) -> float:
    if value is None:
        return math.nan
    try:
        return float(value)
    except (TypeError, ValueError):
        return math.nan


OBSERVATION_COLUMNS = ["run_key", "label", "backend", "variant", "probe", "profile", "scenario", "scene", "scenario_group", "perturb", "chaotic", "metric", "value"]


def _observation_rows(runs: list[BehaviorRun], results: dict[int, dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for run in runs:
        result = results.get(id(run))
        if result is None:
            continue
        for metric, raw in result["metrics"].items():
            base = _metric_base(metric)
            if base == "time_to_sleep_s" and not run.sleep_enabled:
                continue   # a sleep-less profile has no time to sleep (sleeping_body_frames covers it)
            if base == "sleeping_body_frames" and run.sleep_enabled:
                continue
            value = math.inf if (raw is None and base == "time_to_sleep_s") else _value(raw)
            rows.append({"run_key": id(run), "label": run.label, "backend": run.backend, "variant": run.variant, "probe": run.probe, "profile": run.profile,
                         "scenario": run.scenario, "scene": run.scene, "scenario_group": run.group_scene, "perturb": run.perturb, "chaotic": run.chaotic,
                         "metric": metric, "value": value})
    return rows


def _main_values(group: pd.DataFrame) -> pd.Series:
    main = group[group["label"].isin(MAIN_SERIES) & ~group["probe"]]
    if main["label"].nunique() >= 2:
        return main["value"]
    return group[~group["probe"]]["value"]


def median_with_inf(values: Iterable[float]) -> float:
    """The median of values that may hold +inf ("never", e.g. a body that never sleeps); NaN values are ignored."""
    data = sorted(float(v) for v in values if not (isinstance(v, float) and math.isnan(v)))
    if not data:
        return math.nan
    mid = len(data) // 2
    if len(data) % 2:
        return data[mid]
    a, b = data[mid - 1], data[mid]
    if math.isinf(b):
        return a if not math.isinf(a) else b
    return 0.5 * (a + b)


def deviation_of(value: float, reference: float) -> float:
    if math.isnan(value) or math.isnan(reference):
        return math.nan
    if math.isinf(value) or math.isinf(reference):
        return 0.0 if value == reference else math.inf
    return abs(value - reference)


def build_observations(runs: list[BehaviorRun], results: dict[int, dict[str, Any]]) -> pd.DataFrame:
    """Every metric of every run with its reference, tolerance and verdict. Chaotic scenes: the base run (Perturb 0) is the
    series' value, and the replicas give the series' spread (max - min over the base and its replicas)."""
    from . import behavior_rules as rules

    # M10 fix round 0: an empty table keeps its columns, so the criteria code (scene filters) runs on it instead of a KeyError
    frame = pd.DataFrame(_observation_rows(runs, results), columns=OBSERVATION_COLUMNS)
    if frame.empty:
        return frame
    # a run that put a body to sleep while it still moved is no reference for the time to sleep
    moving_sleepers = {id(run) for run in runs if id(run) in results
                       and _value(results[id(run)]["metrics"].get("sleep_speed_cms")) > rules.TOLERANCES["*.sleep_speed_cms"].value}
    out = []
    for (group_scene, profile, metric), group in frame.groupby(["scenario_group", "profile", "metric"], sort=True):
        scenario = group["scenario"].iloc[0]
        found = rules.tolerance_for(scenario, metric)
        chaotic = bool(group["chaotic"].iloc[0])
        spreads: dict[str, float] = {}
        if chaotic:
            for label, own in group.groupby("label"):
                values = [float(v) for v in own["value"]]
                finite = [v for v in values if math.isfinite(v)]
                if len(finite) == len(values) and finite:
                    spreads[label] = max(finite) - min(finite)
                else:
                    spreads[label] = 0.0 if len(set(values)) <= 1 else math.inf
            series = group[group["perturb"] == 0]
        else:
            series = group
        analytic_ref = _metric_base(metric) in ANALYTIC_METRICS or (_metric_base(metric) in ANALYTIC_UNLESS_CHAOTIC and not chaotic)
        candidates = series[~series["run_key"].isin(moving_sleepers)] if _metric_base(metric) == "time_to_sleep_s" else series
        reference = 0.0 if analytic_ref else median_with_inf(_main_values(candidates if not candidates.empty else series))
        spread_ref = max((spreads.get(label, 0.0) for label in MAIN_SERIES if label in spreads), default=0.0)
        for row in series.to_dict("records"):
            value = row["value"]
            deviation = abs(value) if analytic_ref and not math.isnan(value) else deviation_of(value, reference)
            tol_value, tol_source, pending, tol_key = math.nan, "", [], ""
            if found is not None:
                tol_key, tolerance = found
                tol_value, tol_source, pending = tolerance.resolve(row["label"], profile)
            spread_self = spreads.get(row["label"], 0.0) if chaotic else 0.0
            # M9: the replicas' spread widens a consensus comparison only. An analytic bound (energy cannot grow, a hard joint does not
            # stretch) stays the registered value: a chain that diverges in every replica has a wide spread and still diverges.
            effective_tol = tol_value + (spread_self + spread_ref if chaotic and not analytic_ref else 0.0)
            compared = found is not None and not math.isnan(deviation)
            within = bool(compared and deviation <= effective_tol)
            row.update(ref_kind="analytic" if analytic_ref else "consensus", reference=reference, deviation=deviation, tolerance=tol_value,
                       tolerance_key=tol_key, tolerance_source=tol_source, effective_tolerance=effective_tol, spread_self=spread_self, spread_ref=spread_ref,
                       compared=compared, within=within if compared else None,
                       pending_overrides=[{"value": o.value, "sign_off": o.sign_off, "reason": o.reason} for o in pending],
                       within_if_pending=bool(compared and pending and deviation <= max(o.value for o in pending)))
            out.append(row)
    return pd.DataFrame(out)


class Context:
    """What the tag rules may look up about a difference (behavior_rules.Rule.reason(difference, context))."""

    def __init__(self, runs: list[BehaviorRun], results: dict[int, dict[str, Any]], observations: pd.DataFrame, differences: pd.DataFrame):
        self.runs = runs
        self.results = results
        self.by_key = {id(run): run for run in runs}
        self.observations = observations
        self.differences = differences
        self.index: dict[tuple[str, str, str], BehaviorRun] = {(run.label, run.profile, run.scene): run for run in runs}

    def run(self, diff: dict[str, Any]) -> BehaviorRun:
        return self.by_key[diff["run_key"]]

    def effective(self, diff: dict[str, Any], key: str) -> Any:
        value = self.run(diff).effective_value(key)
        return round(value, 4) if isinstance(value, float) else value

    def effective_text(self, diff: dict[str, Any], key: str) -> str:
        return self.run(diff).effective_text(key)

    def desc_hashes(self, group_scene: str, profile: str) -> set[str]:
        return {str((run.manifest.get("scene") or {}).get("desc_sha256")) for run in self.runs
                if run.group_scene == group_scene and run.profile == profile and run.perturb == 0 and not run.probe}

    def metric_value(self, diff: dict[str, Any], metric: str) -> float | None:
        value = self.results[diff["run_key"]]["metrics"].get(metric)
        return None if value is None else float(value)

    def detail(self, diff: dict[str, Any], name: str) -> Any:
        value = self.results[diff["run_key"]]["details"].get(name)
        return round(value, 4) if isinstance(value, float) else value

    def detail_of_main(self, diff: dict[str, Any], name: str) -> str:
        parts = []
        for label in MAIN_SERIES:
            if label == diff["label"]:
                continue
            run = self.index.get((label, diff["profile"], diff["scene"]))
            if run is not None and id(run) in self.results:
                value = self.results[id(run)]["details"].get(name)
                parts.append(f"{label} {value:.3g}" if isinstance(value, float) else f"{label} {value}")
        return ", ".join(parts) or "-"

    def same_engine_value(self, diff: dict[str, Any], label: str) -> float | None:
        run = self.index.get((label, diff["profile"], diff["scene"]))
        if run is None or id(run) not in self.results:
            return None
        value = self.results[id(run)]["metrics"].get(diff["metric"])
        return None if value is None else float(value)

    def mu(self, diff: dict[str, Any]) -> float:
        return float(self.run(diff).params.get("Mu", "nan"))

    def slept_during(self, diff: dict[str, Any]) -> bool:
        details = self.results[diff["run_key"]]["details"]
        frame = details.get("sleep_frame")
        if frame is None:
            return False
        limit = details.get("frame")
        return limit is None or frame <= limit

    def ball(self, diff: dict[str, Any]) -> dict[str, Any] | None:
        """The M5 ball of a per-ball metric "e0.9.<name>"."""
        match = re.match(r"e(\d+\.\d+)\.", str(diff["metric"]))
        if not match:
            return None
        nominal = float(match.group(1))
        for ball in self.results[diff["run_key"]]["details"].get("balls", []):
            if abs(ball["e_nominal"] - nominal) < 1e-6:
                return ball
        return None

    def deviating_impacts(self, diff: dict[str, Any]) -> list[dict[str, Any]]:
        ball = self.ball(diff)
        if ball is None:
            return []
        return [i for i in ball["impacts"] if i["judged"] and abs(i["e"] - i["e_pred"]) > 0.02]

    def bullet_restitution_check(self, diff: dict[str, Any]) -> dict[str, Any] | None:
        ball = self.ball(diff)
        if ball is None:
            return None
        return bullet_contact_restitution(self.run(diff), ball)

    def max_step_travel(self, diff: dict[str, Any]) -> float:
        ball = self.ball(diff)
        speeds = [i["speed_before"] for i in (ball or {}).get("impacts", [])] or [0.0]
        return max(speeds) * self.run(diff).dt

    def probe_outcomes(self, diff: dict[str, Any]) -> list[tuple[str, float]]:
        """The difference's own value and the same metric of the probe series of its engine (M9 fix round 1: the engine's probes
        only - a PhysX probe on M3 is no evidence for a Bullet rule)."""
        obs = self.observations
        rows = obs[(obs["probe"]) & (obs["backend"] == diff["backend"]) & (obs["scenario_group"] == diff["scenario_group"]) & (obs["profile"] == diff["profile"])
                   & (obs["metric"] == diff["metric"])]
        out = [(diff["label"], float(diff["value"]))] + [(row["label"], float(row["value"])) for row in rows.to_dict("records")]
        return out if len(out) > 1 else []

    def result_of_label(self, diff: dict[str, Any], label: str) -> dict[str, Any] | None:
        """{"metrics", "details"} of another series' run of the difference's scene and profile (a probe, the main series), or None."""
        run = self.index.get((label, diff["profile"], diff["scene"]))
        return None if run is None else self.results.get(id(run))

    def replica_count(self, label: str, profile: str, group_scene: str) -> int:
        """The Perturb replicas (Perturb != 0) of one series' chaotic cell: 0 = its sensitivity was not measured."""
        return sum(1 for run in self.runs if run.label == label and run.profile == profile and run.group_scene == group_scene and run.perturb != 0
                   and id(run) in self.results)

    def main_values(self, diff: dict[str, Any]) -> list[tuple[str, float]]:
        """The main series' values of the difference's metric in its scene and profile (what the consensus median is taken of)."""
        out = []
        for label in MAIN_SERIES:
            result = self.result_of_label(diff, label)
            if result is not None and result["metrics"].get(diff["metric"]) is not None:
                out.append((label, _value(result["metrics"][diff["metric"]])))
        return out

    def main_of(self, diff: dict[str, Any]) -> str | None:
        return MAIN_OF_BACKEND.get(diff["backend"])

    def consensus_runs(self, diff: dict[str, Any]) -> list[BehaviorRun]:
        """The main-series runs of the difference's scene and profile whose value of its metric is the consensus value (within the
        tolerance): the engines that set the reference the difference is measured against."""
        out = []
        for label in MAIN_SERIES:
            run = self.index.get((label, diff["profile"], diff["scene"]))
            if run is None or id(run) not in self.results or label == diff["label"]:
                continue
            value = _value(self.results[id(run)]["metrics"].get(diff["metric"]))
            if math.isfinite(value) and math.isfinite(float(diff["reference"])) and abs(value - float(diff["reference"])) <= float(diff["tolerance"]):
                out.append(run)
        return out

    def bounce_sequence_of(self, run: BehaviorRun, metric: str) -> list[dict[str, Any]]:
        """M7: the bounce sequence of the body of a per-body metric "b1.box.bounces" in a run."""
        name = ".".join(str(metric).split(".")[:2])
        return ((self.results.get(id(run)) or {}).get("details") or {}).get("bounce_sequences", {}).get(name, [])

    def difference_of(self, diff: dict[str, Any], label: str) -> dict[str, Any] | None:
        if self.differences.empty:
            return None
        d = self.differences
        rows = d[(d["label"] == label) & (d["scene"] == diff["scene"]) & (d["profile"] == diff["profile"]) & (d["metric"] == diff["metric"])]
        return rows.iloc[0].to_dict() if not rows.empty else None

    def effective_differences(self, diff: dict[str, Any], label: str) -> list[tuple[str, Any, Any]]:
        other = self.index.get((label, diff["profile"], diff["scene"]))
        if other is None:
            return []
        return effective_differences(self.run(diff), other)


def effective_differences(run: BehaviorRun, other: BehaviorRun) -> list[tuple[str, str, str]]:
    """The effective settings of interest whose values differ between two runs (key, this run's, the other's)."""
    out = []
    for key in EFFECTIVE_KEYS_OF_INTEREST:
        a, b = run.effective.get(key), other.effective.get(key)
        if a is None or b is None:
            continue
        va = a.get("canonical") if a.get("canonical") is not None else a.get("native")
        vb = b.get("canonical") if b.get("canonical") is not None else b.get("native")
        if isinstance(va, (int, float)) and isinstance(vb, (int, float)) and abs(float(va) - float(vb)) <= 1e-6 * max(1.0, abs(float(va))):
            continue
        if va != vb:
            out.append((key, str(a.get("native") if a.get("native") is not None else va)[:70], str(b.get("native") if b.get("native") is not None else vb)[:70]))
    return out


def bullet_solver_constants(run: BehaviorRun) -> tuple[float, float]:
    """(erp2, split-impulse penetration threshold in cm) of a Bullet run: the resolved variant key Backend.Bullet.Erp2
    (default 0.2) and the effective bullet.split_impulse_penetration_threshold (default -4 cm)."""
    erp2 = 0.2
    for entry in ((run.manifest.get("profile") or {}).get("resolved") or []):
        if entry.get("key") == "Backend.Bullet.Erp2":
            erp2 = float(entry.get("value"))
    threshold = run.effective_value("bullet.split_impulse_penetration_threshold")
    return erp2, float(threshold) if isinstance(threshold, (int, float)) else -4.0


def contact_state_restitution(e0: float, gap: float, speed: float, dt: float, erp2: float, threshold: float) -> tuple[float, str]:
    """Bullet's e for one contact step from the contact state at the step start (btSequentialImpulseConstraintSolver.cpp:
    944-972): gap > 0 (speculative) -> e0 - gap / (dt |v|) (the restitution target loses gap / dt, :953); a penetration
    shallower than the split-impulse threshold -> e0 + erp2 depth / (dt |v|) (the Baumgarte term goes into the velocity rhs,
    :957, :963); deeper -> e0 (split impulse: the push-out does not touch the velocity)."""
    if gap > 0.0:
        return e0 - gap / (dt * speed), "speculative"
    if gap > threshold:
        return e0 + erp2 * (-gap) / (dt * speed), "shallow"
    return e0, "deep"


def bullet_contact_restitution(run: BehaviorRun, ball: dict[str, Any]) -> dict[str, Any]:
    """contact_state_restitution against every judged impact of a ball (its first contact frame: e_step)."""
    erp2, threshold = bullet_solver_constants(run)
    out = {"n": 0, "worst_residual": 0.0, "n_speculative": 0, "n_baumgarte": 0, "n_split": 0, "erp2": erp2, "threshold": threshold}
    for impact in ball["impacts"]:
        if not impact["judged"]:
            continue
        predicted, kind = contact_state_restitution(ball["e_nominal"], impact["gap_before_cm"], impact["speed_before"], run.dt, erp2, threshold)
        measured = impact.get("e_step", impact["e"])   # the contact step itself (a contact over several frames: its first frame)
        out["n_" + {"speculative": "speculative", "shallow": "baumgarte", "deep": "split"}[kind]] += 1
        out["n"] += 1
        out["worst_residual"] = max(out["worst_residual"], abs(measured - predicted))
    return out


def tag_differences(runs: list[BehaviorRun], results: dict[int, dict[str, Any]], observations: pd.DataFrame) -> pd.DataFrame:
    """The observations outside their tolerance (probe series excluded), each with exactly one tag and its reason."""
    from . import behavior_rules as rules

    if observations.empty:
        return observations.assign(tag=[], reason=[], rule=[], confidence=[], refs=[])
    differences = observations[(observations["compared"]) & (observations["within"] == False) & (~observations["probe"])].copy()  # noqa: E712
    context = Context(runs, results, observations, differences)
    # M9 fix round 0: a rule may give the confidence of the branch that matched (rules.tag returns it; the rule's own is the default)
    tagged = [rules.tag_with_confidence(row, context) for row in differences.to_dict("records")]
    differences["tag"] = [rule.tag for rule, _, _ in tagged]
    differences["reason"] = [reason for _, reason, _ in tagged]
    differences["rule"] = [rule.id for rule, _, _ in tagged]
    differences["confidence"] = [confidence for _, _, confidence in tagged]
    differences["refs"] = [rule.refs for rule, _, _ in tagged]
    return differences


@dataclasses.dataclass
class Analysis:
    runs: list[BehaviorRun]
    results: dict[int, dict[str, Any]]
    errors: list[dict[str, Any]]
    observations: pd.DataFrame
    differences: pd.DataFrame
    sensitivity: list[dict[str, Any]]
    wasserstein: list[dict[str, Any]]
    ue_vs_normalized: list[dict[str, Any]]
    variant_knobs: list[dict[str, Any]]
    criteria: list[dict[str, Any]]
    unsupported: list[dict[str, Any]]
    skipped: list[dict[str, Any]]
    sessions: dict[str, dict[str, Any]]
    table: pd.DataFrame

    def result_of(self, run: BehaviorRun) -> dict[str, Any] | None:
        return self.results.get(id(run))


# ---- UEParity vs Normalized (03 M9: "M1.SingleBoxDrop UEParity 대 Normalized 표가 기본값이 만든 차이를 보여준다") ----------------


def ue_vs_normalized(runs: list[BehaviorRun], results: dict[int, dict[str, Any]]) -> list[dict[str, Any]]:
    """One row per series and M1 case with both profiles: what the UEParity defaults change against Normalized, each item
    with its tag. The fall window isolates the damping (no contact yet): measured dz = z_UEParity - z_Normalized at the
    window's last frame, predicted from the discrete damped fall with the damping each profile gave the body."""
    from . import behavior_rules as rules

    by_key: dict[tuple[str, str, str], BehaviorRun] = {}
    for run in runs:
        if run.scenario == "M1.SingleBoxDrop" and not run.probe and id(run) in results:
            by_key[(run.label, run.params.get("Case", "a"), run.profile)] = run
    rows = []
    for (label, case, profile), uep in sorted(by_key.items()):
        if profile != "UEParity" or (label, case, "Normalized") not in by_key:
            continue
        norm = by_key[(label, case, "Normalized")]
        u, n = results[id(uep)], results[id(norm)]
        du, dn = u["details"], n["details"]
        measured = du["z_fall_end"] - dn["z_fall_end"]
        predicted = du["ref_fall_end"] - dn["ref_fall_end"]
        jolt_native = du["ref_fall_end_c005"] - du["ref_fall_end_undamped"]
        residual = measured - predicted
        items = []
        damping_ok = abs(residual) <= rules.TOLERANCES["M1.fall_dz_max_cm"].value
        items.append({"item": "linear damping", "uep": du["damping"], "norm": dn["damping"], "tag": "by-design" if damping_ok else "mismatch",
                      "reason": (f"UEParity gives every body UE's BodyInstance LinearDamping {du['damping']:g} on every engine (effective body.damping "
                                 f"{_fmt(uep.effective_value('body.damping'))} {du.get('effective_damping_source')}), Normalized 0: at frame {du['fall_end_frame']} "
                                 f"the fall differs by {measured:+.4f} cm, the discrete damped fall predicts {predicted:+.4f} (residual {residual:+.1e} cm). "
                                 f"Jolt's native default 0.05 does not reach the engine (the harness sets the body's damping): it would give {jolt_native:+.4f} cm")
                      if damping_ok else f"the measured fall difference {measured:+.4f} cm is not the damping's {predicted:+.4f} cm (residual {residual:+.1e})"})
        sleep_u = u["metrics"].get("time_to_sleep_s")
        text = uep.effective_text("sleep.criteria")
        unmapped = "not mapped" in text
        items.append({"item": "sleep", "uep": "never" if sleep_u is None else f"{sleep_u:.3f} s", "norm": "off",
                      "tag": "mismatch" if unmapped else "by-design",
                      "reason": ("UEParity sleeps (Normalized never, DL48); " + (f"{uep.backend} still uses its own thresholds, not UE's (sleep.criteria "
                                 "'not mapped', Profiles.md 5)" if unmapped else "the engine's UE4-rule thresholds (Profiles.md 5)"))})
        items.append({"item": "restitution threshold", "uep": uep.restitution_threshold, "norm": norm.restitution_threshold, "tag": "by-design",
                      "reason": "DL35 / Q11 mapping (UEParity) vs 50 cm/s (Normalized); no effect in M1 (restitution 0)"})
        moved = abs(u["metrics"]["final_xy_cm"] - n["metrics"]["final_xy_cm"])
        items.append({"item": "rest state", "uep": f"z {u['metrics']['final_z_cm']:.3f}, xy {u['metrics']['final_xy_cm']:.3f}",
                      "norm": f"z {n['metrics']['final_z_cm']:.3f}, xy {n['metrics']['final_xy_cm']:.3f}", "tag": "by-design",
                      "reason": f"the damping and sleep above move the rest position by {moved:.3f} cm" + (" (the tumble integrates the damping)" if case == "b" else "")})
        rows.append({"label": label, "case": case, "damping_uep": du["damping"], "damping_norm": dn["damping"], "fall_end_frame": du["fall_end_frame"],
                     "dz_measured_cm": measured, "dz_predicted_cm": predicted, "dz_residual_cm": residual, "dz_if_jolt_native_cm": jolt_native,
                     "threshold_uep_cms": uep.restitution_threshold, "threshold_norm_cms": norm.restitution_threshold, "sleep_uep_s": sleep_u,
                     "final_z_uep": u["metrics"]["final_z_cm"], "final_z_norm": n["metrics"]["final_z_cm"], "final_xy_uep": u["metrics"]["final_xy_cm"],
                     "final_xy_norm": n["metrics"]["final_xy_cm"], "items": items})
    return rows


# ---- chaotic scenes: same-engine sensitivity and the Wasserstein distance of the final heights ------------------------------

SENSITIVITY_METRICS = ("top_z_final_cm", "mean_z_final_cm", "top_sway_max_cm", "toppled_fraction", "final_z_mean_cm", "stretch_max_cm", "energy_growth_pct")


def final_heights(run: BehaviorRun) -> np.ndarray:
    return np.array([float(run.com(b)[-1, 2]) for b in st.dynamic_bodies(run.desc)])


def divergence(base: BehaviorRun, replica: BehaviorRun) -> np.ndarray:
    """Per recorded frame: the largest COM distance of a body between two runs of one engine (same frames, same bodies)."""
    if base.states.num_frames != replica.states.num_frames or base.states.body_count != replica.states.body_count:
        raise BehaviorError(f"{replica.run_dir}: not comparable with {base.run_dir} (frames / bodies)")
    bodies = st.dynamic_bodies(base.desc)
    a = np.stack([base.com(b) for b in bodies], axis=1)
    b = np.stack([replica.com(k) for k in bodies], axis=1)
    return np.max(np.linalg.norm(a - b, axis=-1), axis=1)


def _wasserstein(a: np.ndarray, b: np.ndarray) -> float:
    from scipy.stats import wasserstein_distance

    return float(wasserstein_distance(a, b))


def sensitivity(runs: list[BehaviorRun], results: dict[int, dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str], list[BehaviorRun]] = {}
    for run in runs:
        if run.chaotic and id(run) in results:
            groups.setdefault((run.label, run.profile, run.group_scene), []).append(run)
    rows = []
    for (label, profile, scene), members in sorted(groups.items()):
        members = sorted(members, key=lambda r: r.perturb)
        base = members[0] if members[0].perturb == 0 else None
        replicas = [r for r in members if r.perturb != 0]
        row: dict[str, Any] = {"label": label, "profile": profile, "scene": scene, "probe": members[0].probe, "replicas": len(replicas),
                               "recorded_frames": members[0].states.num_frames}
        if base is not None and replicas:
            curves = [divergence(base, r) for r in replicas]
            frames = base.states.frame
            first_tiny = [int(frames[np.argmax(c > 1e-3)]) if np.any(c > 1e-3) else None for c in curves]
            first_big = [int(frames[np.argmax(c > 1.0)]) if np.any(c > 1.0) else None for c in curves]
            row.update(divergence_final_max_cm=float(max(c[-1] for c in curves)),
                       divergence_first_frame_cm=float(max(c[0] for c in curves)),    # after one step, from a 1e-4 cm start offset
                       first_frame_over_1e3cm=min((f for f in first_tiny if f is not None), default=None),
                       first_frame_over_1cm=min((f for f in first_big if f is not None), default=None),
                       replicas_over_1cm=sum(1 for f in first_big if f is not None))
        heights = [final_heights(r) for r in members]
        pairs = [(i, j) for i in range(len(heights)) for j in range(i + 1, len(heights))]
        row["w_within_cm"] = float(np.mean([_wasserstein(heights[i], heights[j]) for i, j in pairs])) if pairs else math.nan
        for metric in SENSITIVITY_METRICS:
            values = [results[id(r)]["metrics"].get(metric) for r in members]
            values = [float(v) for v in values if v is not None and math.isfinite(float(v))]
            if values:
                row[f"{metric}.base"] = float(results[id(base)]["metrics"].get(metric)) if base is not None and results[id(base)]["metrics"].get(metric) is not None else math.nan
                row[f"{metric}.spread"] = max(values) - min(values)
        rows.append(row)
    return rows


def wasserstein_table(runs: list[BehaviorRun], results: dict[int, dict[str, Any]], sens: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Cross-engine W1 of the final body heights against each series' own W1 between its runs (base + replicas): a cross-engine
    distance within the larger own one is indistinguishable from the 1e-4 cm sensitivity. M9 fix round 1: like with like - the
    cross term is the mean W1 over every pair of single runs (one of each series), as the own term is the mean over pairs of single
    runs of one series; the first version pooled each series' runs (4N against 4N samples), whose smaller sampling noise biased
    the verdict toward "within sensitivity"."""
    per_run: dict[tuple[str, str, str], list[np.ndarray]] = {}
    for run in sorted(runs, key=lambda r: r.perturb):
        if run.chaotic and id(run) in results and not run.probe:
            per_run.setdefault((run.label, run.profile, run.group_scene), []).append(final_heights(run))
    within = {(r["label"], r["profile"], r["scene"]): r.get("w_within_cm", math.nan) for r in sens}
    rows = []
    scenes = sorted({(profile, scene) for _, profile, scene in per_run})
    for profile, scene in scenes:
        labels = [label for label, p, s in per_run if p == profile and s == scene]
        pairs = [(a, b) for i, a in enumerate(MAIN_SERIES) for b in MAIN_SERIES[i + 1:] if a in labels and b in labels]
        pairs += [(label, MAIN_OF_BACKEND.get(_backend_of(label), "")) for label in labels if label not in MAIN_SERIES
                  and MAIN_OF_BACKEND.get(_backend_of(label)) in labels]
        for a, b in pairs:
            cross = float(np.mean([_wasserstein(x, y) for x in per_run[(a, profile, scene)] for y in per_run[(b, profile, scene)]]))
            wa, wb = within.get((a, profile, scene), math.nan), within.get((b, profile, scene), math.nan)
            own = max(v for v in (wa, wb) if not math.isnan(v)) if not (math.isnan(wa) and math.isnan(wb)) else math.nan
            if math.isnan(own):
                verdict = "no replicas"
            else:
                verdict = "within sensitivity" if cross <= own else "beyond sensitivity"
            rows.append({"profile": profile, "scene": scene, "a": a, "b": b, "w_cross_cm": cross, "w_within_a_cm": wa, "w_within_b_cm": wb,
                         "ratio": cross / own if own and not math.isnan(own) and own > 0 else (math.inf if cross > 0 else math.nan), "verdict": verdict})
    return rows


def _backend_of(label: str) -> str:
    head = label.split(".", 1)[0]
    return {"jolt": "Jolt", "bullet": "Bullet", "physx": "PhysX"}.get(head, head)


# ---- variant knobs: a variant or probe against its engine's main series ------------------------------------------------------

KNOB_PAIRS = (("jolt.SharpBoxes", "jolt.Default"), ("bullet.StaticAabbOff", "bullet.Default"), ("physx.UE4Compat-Stab", "physx.UE4Compat"),
              ("physx.UE4Compat-TGS", "physx.UE4Compat"), ("physx.SdkDefault", "physx.UE4Compat"), ("bullet.R0", "bullet.Default"),
              ("bullet.Iter50", "bullet.Default"), ("bullet.R0Iter50", "bullet.Default"),
              # M9 fix round 1: the PhysX M3.BoxPile probes against the variant they probe
              ("physx.TGS-R0", "physx.UE4Compat-TGS"), ("physx.TGS-Pos32", "physx.UE4Compat-TGS"), ("physx.SdkDefault-Pos8", "physx.SdkDefault"))


def variant_knobs(runs: list[BehaviorRun]) -> list[dict[str, Any]]:
    """Bitwise state identity of a variant against its engine's main series per scene and profile, with the first frame
    whose states differ (G11 "결과 중립 knob" asks this of Bullet.StaticAabbOff, Progress M9 준비 F-1)."""
    index = {(run.label, run.profile, run.scene): run for run in runs}
    rows = []
    for variant, main in KNOB_PAIRS:
        for (label, profile, scene), run in sorted(index.items()):
            if label != variant or (main, profile, scene) not in index:
                continue
            other = index[(main, profile, scene)]
            if run.states.num_frames != other.states.num_frames or run.states.body_count != other.states.body_count:
                continue
            same = np.all(run.states.position == other.states.position, axis=(1, 2)) & np.all(run.states.rotation == other.states.rotation, axis=(1, 2)) \
                & np.all(run.states.linear_velocity == other.states.linear_velocity, axis=(1, 2)) & np.all(run.states.object_state == other.states.object_state, axis=1)
            first = None if bool(np.all(same)) else int(run.states.frame[int(np.argmin(same))])
            gap = float(np.max(np.linalg.norm(run.states.position[-1] - other.states.position[-1], axis=-1)))
            rows.append({"variant": variant, "main": main, "profile": profile, "scene": scene, "identical": first is None, "first_divergent_frame": first,
                         "final_max_position_cm": gap, "desc_equal": (run.manifest.get("scene") or {}).get("desc_sha256") == (other.manifest.get("scene") or {}).get("desc_sha256"),
                         "h_state_equal": (run.manifest.get("determinism") or {}).get("h_state") == (other.manifest.get("determinism") or {}).get("h_state"),
                         "recorded_frames": run.states.num_frames})
    return rows


# ---- 03 M9 completion criteria ------------------------------------------------------------------------------------------------

def _obs(observations: pd.DataFrame, **match: Any) -> pd.DataFrame:
    if observations.empty:
        return observations
    mask = pd.Series(True, index=observations.index)
    for key, value in match.items():
        if callable(value):
            mask &= observations[key].map(value)
        else:
            mask &= observations[key] == value
    return observations[mask]


def _matching_differences(differences: pd.DataFrame, rows: pd.DataFrame) -> pd.DataFrame:
    """The tagged differences of the given observation rows (same series, profile, scene and metric)."""
    if differences.empty or rows.empty:
        return differences.iloc[0:0]
    keys = {(r["label"], r["profile"], r["scene"], r["metric"]) for r in rows.to_dict("records")}
    mask = [(r["label"], r["profile"], r["scene"], r["metric"]) in keys for r in differences.to_dict("records")]
    return differences[mask]


def _difference_rules(differences: pd.DataFrame, rows: pd.DataFrame) -> set[str]:
    """The rules that tagged the failing observations among the rows (every failing row must be a tagged difference)."""
    failing = rows[rows["within"] == False]  # noqa: E712
    matched = _matching_differences(differences, failing)
    if len(matched) != len(failing):
        return set()
    return set(matched["rule"])


def _tag_text(differences: pd.DataFrame, rows: pd.DataFrame) -> str:
    matched = _matching_differences(differences, rows[rows["within"] == False])  # noqa: E712
    return "".join(f"; tagged {r['tag']} ({r['rule']}, {r['confidence']})" for r in matched.to_dict("records"))


def criteria(analysis_obs: pd.DataFrame, differences: pd.DataFrame, ue_rows: list[dict[str, Any]], runs: list[BehaviorRun],
             results: dict[int, dict[str, Any]], unsupported: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """The behaviour completion criteria of 03 M9 that this analysis can judge, per main series (Normalized), with the lead's reading
    of them (behavior_rules.LEAD_DECISIONS, M9 commit)."""
    from . import behavior_rules as rules

    out = []

    def verdict(rows: pd.DataFrame) -> tuple[str, str]:
        if rows.empty:
            return "not run", "no observation"
        failed = rows[rows["within"] == False]  # noqa: E712
        if failed.empty:
            return "pass", f"{len(rows)} observations within"
        pending = failed[failed["within_if_pending"]]
        text = "; ".join(f"{r['scene']} {r['metric']} {r['value']:.4g} (tol {r['effective_tolerance']:.3g})" for r in failed.to_dict("records")[:4])
        if len(pending) == len(failed):
            return "fail (pending override)", text + " - within the post-run override proposed for sign-off"
        return "fail", text

    for label in MAIN_SERIES:
        # M9 fix round 1: 03 lists the isotropy variant (BoxYaw 45) with the scene; it is judged on its own lines with the same limits
        # (the first version judged BoxYaw 0 only and did not say so).
        for yaw, suffix, scope in (("0", "", "BoxYaw 0 (box axes along the slope)"), ("45", ".yaw45", "isotropy variant BoxYaw 45 (same limit)")):
            sliding = _obs(analysis_obs, label=label, profile="Normalized", scenario="M4.InclineFriction", metric="d2s_err_pct")
            sliding = sliding[sliding["scene"].str.contains(f"BoxYaw={yaw},")]
            state, text = verdict(sliding)
            out.append({"id": "M4.slide" + suffix, "criterion": f"M4 sliding mu (0.30, 0.50, 0.55): d(2 s) within 1% of the discrete reference, {scope}",
                        "series": label, "verdict": state, "detail": text})
            holding = _obs(analysis_obs, label=label, profile="Normalized", scenario="M4.InclineFriction", metric="hold_drift_cm")
            holding = holding[holding["scene"].str.contains(f"BoxYaw={yaw},")]
            state, text = verdict(holding)
            out.append({"id": "M4.hold" + suffix, "criterion": f"M4 mu >= 0.6 holds: drift < 0.1 cm at 2 s, {scope}", "series": label, "verdict": state,
                        "detail": text})
        bounce = _obs(analysis_obs, label=label, profile="Normalized", scenario="M5.BouncingBalls", metric=lambda m: str(m).endswith("e_dev_model_max"))
        state, text = verdict(bounce)
        proposed = _obs(analysis_obs, label=label, profile="Normalized", scenario="M5.BouncingBalls", metric=lambda m: str(m).endswith("e_dev_proposed_max"))
        if state == "fail" and not proposed.empty and bool((proposed["within"] == True).all()):  # noqa: E712
            state = "fail (pending override)"
            text += (f" - the engine's proposed restitution model (behavior_rules.PROPOSED_RESTITUTION_MODELS) predicts every judged impact "
                     f"within {proposed['value'].max():.2g}")
        models = sorted({str(results[id(run)]["details"].get("model")) for run in runs if run.label == label and run.profile == "Normalized"
                         and run.scenario == "M5.BouncingBalls" and id(run) in results})
        if models and not bounce.empty:
            text += f"; registered model {', '.join(models)} (tolerances.json G4.restitution_velocity_model)"
        out.append({"id": "M5.e", "criterion": "M5 every impact above 50 cm/s: e_k within the registered tolerance (+-0.02) of the registered model",
                    "series": label, "verdict": state, "detail": text})
        period = _obs(analysis_obs, label=label, profile="Normalized", scenario="M6a.Pendulum", metric="period_err_pct")
        period = period[period["scene"].str.contains("Angle=5,")]
        # M9 fix round 0: 03's criterion names the catalogued scene, whose joint is the hinge (docs/Scenes.md 2.19, Joint=Hinge default);
        # the ball variant is judged on its own line with the same limit, so a ball-only engine behaviour (Bullet J-1) is visible on
        # its own instead of hiding in or failing the scene's line.
        # M9 commit (lead decision 2026-10-22, behavior_rules.LEAD_DECISIONS): the criterion is judged on the catalogued hinge scene; the
        # ball variant is reported with its measurement, not gated.
        for joint, criterion_id, criterion_text in (
                ("Hinge", "M6a.period", "M6a 5 deg period error < 0.5% (2.00905 s), the catalogued scene (Joint=Hinge, Scenes.md 2.19)"),
                ("Ball", "M6a.period.ball", "M6a 5 deg period error (2.00905 s), the Joint=Ball variant: reported, not gated (lead 2026-10-22)")):
            rows = period[period["scene"].str.contains(f"Joint={joint}") | (joint == "Hinge" and ~period["scene"].str.contains("Joint="))]
            if rows.empty:
                refused = [u for u in unsupported if u.get("label") == label and str(u.get("scene", "")).startswith("M6a.")]
                out.append({"id": criterion_id, "criterion": criterion_text, "series": label,
                            "verdict": "unsupported" if refused else "not run",
                            "detail": f"{len(refused)} M6a runs refused: the bench has no joint capability for {label}" if refused else "no run"})
            else:
                state, text = verdict(rows)
                decision = rules.LEAD_DECISIONS.get(criterion_id)
                if decision is not None and decision["kind"] == "reported":
                    state, text = ("reported" if state == "pass" else "reported (beyond 0.5%)"), (
                        ("within 0.5%: " if state == "pass" else "beyond 0.5%, not gated: ") + text + _tag_text(differences, rows) + " - " + decision["text"])
                out.append({"id": criterion_id, "criterion": criterion_text, "series": label, "verdict": state, "detail": text})
        for chain in ("M6b.Chain", "M6c.HeavyChain"):
            energy = _obs(analysis_obs, label=label, profile="Normalized", scenario=chain, metric="energy_growth_pct")
            # M9: every replica counts (the observation table holds the base run of a chaotic scene)
            replicas = sorted((run.perturb, _value(results[id(run)]["metrics"].get("energy_growth_pct"))) for run in runs
                              if run.label == label and run.profile == "Normalized" and run.scenario == chain and id(run) in results)
            if energy.empty:
                refused = [u for u in unsupported if u.get("label") == label and str(u.get("scene", "")).startswith(chain)]
                out.append({"id": f"{chain.split('.')[0]}.energy", "criterion": f"{chain}: stretch reported, energy does not diverge", "series": label,
                            "verdict": "unsupported" if refused else "not run", "detail": f"{len(refused)} runs refused (no joint capability)" if refused else "no run"})
            else:
                state, text = verdict(energy)
                limit = float(energy["tolerance"].iloc[0])
                grown = [(p, v) for p, v in replicas if v == v and v > limit]
                if grown and state == "pass":
                    state = "fail"
                if replicas:
                    text += ("; replicas (Perturb: growth % of m g span) " + ", ".join(f"{p}: {v:.3g}" for p, v in replicas)
                             + (f" - {len(grown)} of {len(replicas)} grow beyond {limit:g}" if grown else " - none grows"))
                stretch = _obs(analysis_obs, label=label, profile="Normalized", scenario=chain, metric="stretch_max_cm")
                if not stretch.empty:
                    text += f"; max stretch (base) {float(stretch['value'].iloc[0]):.4g} cm"
                # M9 fix round 1: a chain whose joints came apart (stretch beyond one link) falls freely and keeps its energy, so "the energy
                # does not diverge" holds trivially there: not a plain pass - the lead reads it with the stretch (Progress M9).
                separated = [(run.perturb, results[id(run)]["details"]) for run in runs if run.label == label and run.profile == "Normalized"
                             and run.scenario == chain and id(run) in results and results[id(run)]["details"].get("joints_separated")]
                criterion_id = f"{chain.split('.')[0]}.energy"
                decision = rules.LEAD_DECISIONS.get(criterion_id)
                if separated and state == "pass":
                    # M9 fix round 1: a chain whose joints came apart keeps its energy trivially. M9 commit (lead 2026-10-22): that is a
                    # separate finding - joint separation - reported as such, not a pass
                    link = float(separated[0][1].get("link_length_cm") or math.nan)
                    state = "finding: joints separated"
                    text += (f"; the joints came apart in {len(separated)} of {len(replicas)} runs (stretch beyond one link, {link:.3g} cm): the energy check "
                             f"is trivial there (a separated chain falls freely)" + (f" - {decision['separated']}" if decision and decision.get("separated") else ""))
                elif state == "fail" and decision is not None and decision["kind"] == "accepted" and any(fnmatch.fnmatchcase(label, p) for p in decision["labels"]):
                    # M9 commit (lead 2026-10-22): an accepted engine behaviour stays a failure, labelled - and only where the failing
                    # observation is tagged by the rule that checks that mechanism's signature on the data
                    tagged = _difference_rules(differences, energy)
                    if tagged and tagged <= set(decision["rules"]):
                        state = "fail (accepted engine behaviour)"
                        text += f"; tagged {', '.join(sorted(tagged))} - {decision['text']} ({decision['sign_off']})"
                out.append({"id": criterion_id, "criterion": f"{chain}: stretch reported, energy does not diverge", "series": label,
                            "verdict": state, "detail": text})
    # every cross-engine difference tagged
    if differences.empty:
        out.append({"id": "tags", "criterion": "every cross-engine difference has a by-design / mismatch / algorithmic tag", "series": "all",
                    "verdict": "pass", "detail": "no difference"})
    else:
        untagged = differences[~differences["tag"].isin(("by-design", "mismatch", "algorithmic"))]
        provisional = differences[differences["rule"] == "unexplained"]
        counts = ", ".join(f"{tag} {n}" for tag, n in differences["tag"].value_counts().items())
        out.append({"id": "tags", "criterion": "every cross-engine difference has a by-design / mismatch / algorithmic tag", "series": "all",
                    "verdict": "pass" if untagged.empty else "fail",
                    "detail": f"{len(differences)} differences: {counts}; {len(provisional)} provisional (unexplained)"})
    # the M1 UEParity vs Normalized table
    damping_rows = [r for r in ue_rows if r["case"] == "a"]
    ok = damping_rows and all(abs(r["dz_residual_cm"]) <= 2e-3 for r in damping_rows)
    out.append({"id": "M1.profiles", "criterion": "M1 UEParity vs Normalized table shows the default-driven differences", "series": "all",
                "verdict": "pass" if ok else ("fail" if damping_rows else "not run"),
                "detail": (f"{len(ue_rows)} rows; fall-window difference = UE damping 0.01 on every engine (largest residual "
                           f"{max(abs(r['dz_residual_cm']) for r in damping_rows):.1e} cm); Jolt's native 0.05 would give "
                           f"{damping_rows[0]['dz_if_jolt_native_cm']:+.3f} cm and is not what the engines receive") if damping_rows else "no M1 pair"})
    # M2a N=10 UEParity: UE4Compat stabilises last (or not within 5 s)
    sleep = {}
    for run in runs:
        if run.scenario == "M2a.Tower" and run.profile == "UEParity" and run.params.get("N") == "10" and run.label.startswith("physx.UE4Compat") and id(run) in results:
            value = results[id(run)]["metrics"].get("time_to_sleep_s")
            sleep.setdefault(run.label, []).append(math.inf if value is None else float(value))
    if {"physx.UE4Compat", "physx.UE4Compat-Stab", "physx.UE4Compat-TGS"} <= set(sleep):
        compat = min(sleep["physx.UE4Compat"])
        others = max(max(sleep["physx.UE4Compat-Stab"]), max(sleep["physx.UE4Compat-TGS"]))
        ok = compat >= others or compat > 5.0
        detail = ", ".join(f"{k} {', '.join('never' if math.isinf(v) else f'{v:.2f}' for v in sorted(vals))} s" for k, vals in sorted(sleep.items()))
        out.append({"id": "M2a.stabilization", "criterion": "M2a N=10 UEParity: PhysX UE4Compat stabilises last (or not within 5 s) of UE4Compat / -Stab / -TGS",
                    "series": "physx", "verdict": "pass" if ok else "fail", "detail": "all sleep (base + replicas): " + detail})
    else:
        out.append({"id": "M2a.stabilization", "criterion": "M2a N=10 UEParity: PhysX UE4Compat stabilises last", "series": "physx", "verdict": "not run",
                    "detail": "needs physx.UE4Compat, -Stab and -TGS runs of M2a.Tower N=10 UEParity"})
    return out


# ---- the whole analysis ---------------------------------------------------------------------------------------------------------

def analyze(inputs: Iterable[str | pathlib.Path]) -> Analysis:
    loaded = load_runs(inputs)
    results: dict[int, dict[str, Any]] = {}
    errors = []
    for run in loaded.runs:
        try:
            result = analyze_run(run)
        except (BehaviorError, st.StatesError, KeyError, IndexError, ValueError) as error:
            errors.append({"run_dir": str(run.run_dir), "label": run.label, "scene": run.scene, "profile": run.profile, "error": f"{type(error).__name__}: {error}"})
            continue
        if result is not None:
            results[id(run)] = result
    observations = build_observations(loaded.runs, results)
    differences = tag_differences(loaded.runs, results, observations)
    ue_rows = ue_vs_normalized(loaded.runs, results)
    sens = sensitivity(loaded.runs, results)
    wass = wasserstein_table(loaded.runs, results, sens)
    knobs = variant_knobs(loaded.runs)
    crit = criteria(observations, differences, ue_rows, loaded.runs, results, loaded.unsupported)
    return Analysis(runs=loaded.runs, results=results, errors=errors, observations=observations, differences=differences, sensitivity=sens,
                    wasserstein=wass, ue_vs_normalized=ue_rows, variant_knobs=knobs, criteria=crit, unsupported=loaded.unsupported,
                    skipped=loaded.skipped, sessions=loaded.sessions, table=loaded.table)


def _jsonable(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, (np.integer,)):
        return int(value)
    if isinstance(value, (np.floating, float)):
        value = float(value)
        return None if math.isnan(value) else ("inf" if value == math.inf else "-inf" if value == -math.inf else value)
    if isinstance(value, np.bool_):
        return bool(value)
    if isinstance(value, np.ndarray):
        return _jsonable(value.tolist())
    return value


def to_json(analysis: Analysis) -> dict[str, Any]:
    """physicstest.behavior/1: everything the report shows, without the per-frame series."""
    from . import behavior_rules as rules

    runs = []
    for run in analysis.runs:
        result = analysis.results.get(id(run))
        runs.append({"run_dir": str(run.run_dir), "label": run.label, "probe": run.probe, "backend": run.backend, "variant": run.variant, "profile": run.profile,
                     "scene": run.scene, "desc_sha256": (run.manifest.get("scene") or {}).get("desc_sha256"),
                     "metrics": result["metrics"] if result else None, "details": result["details"] if result else None})
    drop = ["run_key"]
    return _jsonable({
        "schema": "physicstest.behavior/1",
        "unofficial": True,
        "runs": runs,
        "observations": analysis.observations.drop(columns=[c for c in drop if c in analysis.observations.columns]).to_dict("records") if not analysis.observations.empty else [],
        "differences": analysis.differences.drop(columns=[c for c in drop if c in analysis.differences.columns]).to_dict("records") if not analysis.differences.empty else [],
        "ue_vs_normalized": analysis.ue_vs_normalized,
        "sensitivity": analysis.sensitivity,
        "wasserstein": analysis.wasserstein,
        "variant_knobs": analysis.variant_knobs,
        "criteria": analysis.criteria,
        "unsupported": analysis.unsupported,
        "skipped": analysis.skipped,
        "errors": analysis.errors,
        "tolerances": {key: dataclasses.asdict(t) for key, t in rules.TOLERANCES.items()},
        "rules": [{"id": r.id, "tag": r.tag, "confidence": r.confidence, "refs": r.refs} for r in rules.RULES + [rules.UNEXPLAINED]],
    })


# ---- report section (Markdown + PNG) ------------------------------------------------------------------------------------------

def _fmt(value: Any, digits: int = 4) -> str:
    if value is None:
        return "-"
    if isinstance(value, str):
        return value
    try:
        number = float(value)
    except (TypeError, ValueError):
        return str(value)
    if math.isnan(number):
        return "-"
    if math.isinf(number):
        return "never" if number > 0 else "-inf"
    return f"{number:.{digits}g}"


def _esc(text: Any) -> str:
    return str(text).replace("|", r"\|").replace("\n", " ")


def series_colors() -> dict[str, str]:
    """Fixed colours: the three main series take the first three categorical slots (they validate all-pairs, so they can
    share a scatter plot); a label keeps its colour in every behaviour figure."""
    from . import report as _report

    return {label: _report.SERIES_COLORS[i] for i, label in enumerate(MAIN_SERIES)}


MARKERS = {"jolt.Default": "o", "bullet.Default": "s", "physx.UE4Compat": "^"}


def _legend(ax) -> None:
    from . import report as _report

    legend = ax.legend(frameon=False, fontsize=8, loc="best")
    for text in legend.get_texts():
        text.set_color(_report.TEXT_PRIMARY)


def plot_m1_profiles(analysis: Analysis, path: pathlib.Path, condition: str) -> bool:
    """UEParity - Normalized z of the falling box over the fall window (case a) against the damped-fall prediction."""
    from . import report as _report

    pairs = {}
    for run in analysis.runs:
        if run.scenario == "M1.SingleBoxDrop" and run.params.get("Case") == "a" and run.label in MAIN_SERIES and id(run) in analysis.results:
            pairs.setdefault(run.label, {})[run.profile] = analysis.results[id(run)]["series"]
    pairs = {k: v for k, v in pairs.items() if {"UEParity", "Normalized"} <= set(v)}
    if not pairs:
        return False
    colors = series_colors()
    fig, ax, plt = _report._figure(7.2, 3.9)
    first = next(iter(pairs.values()))
    frames = first["UEParity"]["frame"]
    predicted = first["UEParity"]["ref"] - first["Normalized"]["ref"]
    ax.plot(frames, predicted, color=_report.TEXT_SECONDARY, linewidth=2, linestyle="--", label="predicted, UE damping 0.01 (formula 1)")
    run0 = next(r for r in analysis.runs if r.scenario == "M1.SingleBoxDrop" and r.label in pairs and r.profile == "UEParity")
    z0 = float(run0.initial_com(st.dynamic_bodies(run0.desc)[0])[2])
    steps = np.asarray(frames) + 1
    c005, _ = analytic.damped_free_fall_series(z0, 0.0, float(run0.gravity[2]), JOLT_NATIVE_LINEAR_DAMPING, run0.dt, int(steps.max()))
    c0, _ = analytic.damped_free_fall_series(z0, 0.0, float(run0.gravity[2]), 0.0, run0.dt, int(steps.max()))
    ax.plot(frames, (c005 - c0)[steps - 1], color=_report.TEXT_SECONDARY, linewidth=1.2, linestyle=":", label="if damping were Jolt's native 0.05")
    for size, label in zip((9.0, 6.5, 4.0), [l for l in MAIN_SERIES if l in pairs]):   # largest first: the three overlap
        both = pairs[label]
        ax.plot(both["UEParity"]["frame"], both["UEParity"]["z"] - both["Normalized"]["z"], MARKERS.get(label, "o"), color=colors.get(label),
                markersize=size, markeredgecolor=_report.SURFACE, markeredgewidth=1.0, label=f"{label} measured", linestyle="none")
    ax.set_xlabel("frame (fall window: before the first contact)", color=_report.TEXT_SECONDARY, fontsize=9)
    ax.set_ylabel("z(UEParity) - z(Normalized) (cm)", color=_report.TEXT_SECONDARY, fontsize=9)
    _legend(ax)
    _report._titles(fig, ax, "M1.SingleBoxDrop a: the profile difference is UE's damping 0.01", condition)
    fig.tight_layout()
    fig.savefig(path, facecolor=_report.SURFACE)
    plt.close(fig)
    return True


def plot_m4(analysis: Analysis, path: pathlib.Path, condition: str) -> bool:
    from . import report as _report

    obs = analysis.observations
    if obs.empty:
        return False
    rows = obs[(obs["scenario"] == "M4.InclineFriction") & (obs["metric"] == "d2s_err_pct") & (obs["profile"] == "Normalized") & obs["label"].isin(MAIN_SERIES)]
    if rows.empty:
        return False
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    colors = series_colors()
    fig, axes = plt.subplots(1, 2, figsize=(8.4, 3.7), dpi=110, sharey=True)
    fig.patch.set_facecolor(_report.SURFACE)
    for ax, yaw in zip(axes, ("0", "45")):
        _report._style_axes(ax)
        ax.axhspan(-1.0, 1.0, color=_report.GRID, alpha=0.6, linewidth=0, label="03 M9: +-1%")
        part = rows[rows["scene"].str.contains(f"BoxYaw={yaw},")]
        for label in MAIN_SERIES:
            own = part[part["label"] == label]
            if own.empty:
                continue
            mu = own["scene"].str.extract(r"Mu=([0-9.]+)")[0].astype(float)
            order = np.argsort(mu.values)
            ax.plot(mu.values[order], own["value"].values[order], "-" + MARKERS.get(label, "o"), color=colors.get(label), linewidth=2, markersize=6,
                    markeredgecolor=_report.SURFACE, markeredgewidth=1.0, label=label)
        ax.axhline(0.0, color=_report.TEXT_SECONDARY, linewidth=0.8)
        ax.set_title(f"BoxYaw {yaw}", color=_report.TEXT_PRIMARY, fontsize=9.5, loc="left")
        ax.set_xlabel("mu (sliding: tan 30 = 0.577)", color=_report.TEXT_SECONDARY, fontsize=9)
    axes[0].set_ylabel("d(2 s) - discrete reference (%)", color=_report.TEXT_SECONDARY, fontsize=9)
    _legend(axes[1])
    fig.suptitle(condition, x=0.01, y=0.995, ha="left", va="top", color=_report.TEXT_WARNING, fontsize=8.5, fontweight="bold")
    fig.text(0.01, 0.91, "M4.InclineFriction (Normalized): slide distance error at 2 s", color=_report.TEXT_PRIMARY, fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.9))
    fig.savefig(path, facecolor=_report.SURFACE)
    plt.close(fig)
    return True


def plot_m5(analysis: Analysis, path: pathlib.Path, condition: str) -> bool:
    from . import report as _report

    runs = [r for r in analysis.runs if r.scenario == "M5.BouncingBalls" and r.profile == "Normalized" and r.label in MAIN_SERIES and id(r) in analysis.results]
    if not runs:
        return False
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    colors = series_colors()
    balls = analysis.results[id(runs[0])]["details"]["balls"]
    fig, axes = plt.subplots(2, 2, figsize=(8.6, 6.4), dpi=110)
    fig.patch.set_facecolor(_report.SURFACE)
    dt = runs[0].dt
    g = runs[0].g
    for ax, ball in zip(axes.flat, balls):
        _report._style_axes(ax)
        e0 = ball["e_nominal"]
        speeds = np.linspace(55.0, 650.0, 200)
        ax.plot(speeds, np.full_like(speeds, e0), color=_report.TEXT_SECONDARY, linewidth=1.5, label="e (model 0)")
        ax.plot(speeds, e0 * (1.0 + g * dt / speeds), color=_report.TEXT_SECONDARY, linewidth=1.2, linestyle="--", label="e (1 + g dt / |v|) (model 1)")
        for run in runs:
            own = next((b for b in analysis.results[id(run)]["details"]["balls"] if abs(b["e_nominal"] - e0) < 1e-6), None)
            if own is None:
                continue
            judged = [i for i in own["impacts"] if i["judged"]]
            ax.plot([i["speed_before"] for i in judged], [i["e"] for i in judged], MARKERS.get(run.label, "o"), color=colors.get(run.label), markersize=6,
                    markeredgecolor=_report.SURFACE, markeredgewidth=1.0, label=run.label, linestyle="none")
        ax.set_title(f"ball e = {e0:g}", color=_report.TEXT_PRIMARY, fontsize=9.5, loc="left")
        ax.set_xlabel("impact speed |v(c-1)| (cm/s)", color=_report.TEXT_SECONDARY, fontsize=8.5)
        ax.set_ylabel("e_k = -v(c) / v(c-1)", color=_report.TEXT_SECONDARY, fontsize=8.5)
    _legend(axes.flat[0])
    fig.suptitle(condition, x=0.01, y=0.995, ha="left", va="top", color=_report.TEXT_WARNING, fontsize=8.5, fontweight="bold")
    fig.text(0.01, 0.945, "M5.BouncingBalls (Normalized): e of every judged impact", color=_report.TEXT_PRIMARY, fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(path, facecolor=_report.SURFACE)
    plt.close(fig)
    return True


def plot_tower(analysis: Analysis, path: pathlib.Path, condition: str, scene: str = "M2a.Tower[N=30]", profile: str = "Normalized") -> bool:
    from . import report as _report

    runs = [r for r in analysis.runs if r.group_scene == scene and r.profile == profile and r.label in MAIN_SERIES and id(r) in analysis.results]
    if not runs:
        return False
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    colors = series_colors()
    labels = [label for label in MAIN_SERIES if any(r.label == label for r in runs)]
    fig, axes = plt.subplots(1, len(labels), figsize=(3.0 * len(labels) + 0.6, 3.6), dpi=110, sharey=True, squeeze=False)
    fig.patch.set_facecolor(_report.SURFACE)
    for ax, label in zip(axes[0], labels):
        _report._style_axes(ax)
        for run in sorted((r for r in runs if r.label == label), key=lambda r: r.perturb):
            series = analysis.results[id(run)]["series"]
            ax.plot(series["t"], series["top_z"], color=colors.get(label), linewidth=2.0 if run.perturb == 0 else 1.0,
                    alpha=1.0 if run.perturb == 0 else 0.55, label="Perturb 0" if run.perturb == 0 else ("Perturb 1-3 (1e-4 cm)" if run.perturb == 1 else None))
        ax.set_title(label, color=_report.TEXT_PRIMARY, fontsize=9.5, loc="left")
        ax.set_xlabel("t (s)", color=_report.TEXT_SECONDARY, fontsize=9)
    axes[0][0].set_ylabel("top box z (cm)", color=_report.TEXT_SECONDARY, fontsize=9)
    _legend(axes[0][0])
    fig.suptitle(condition, x=0.01, y=0.995, ha="left", va="top", color=_report.TEXT_WARNING, fontsize=8.5, fontweight="bold")
    fig.text(0.01, 0.9, f"{scene} ({profile}): the base run and its same-engine replicas", color=_report.TEXT_PRIMARY, fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.89))
    fig.savefig(path, facecolor=_report.SURFACE)
    plt.close(fig)
    return True


def markdown_section(analysis: Analysis, out_dir: pathlib.Path, condition: dict[str, str] | None = None, heading: str = "##", number: str = "7") -> list[str]:
    """The behaviour section of the report: criteria, tagged differences, the M1 profile table, the scene tables, chaotic
    sensitivity, variant knobs, the tolerance and rule registry; PNGs into out_dir; differences.csv and behavior.json too."""
    from . import behavior_rules as rules

    condition = condition or {"markdown": "> 조건: **비공식 (UNOFFICIAL)**.", "figure": "UNOFFICIAL"}
    h2, h3 = heading, heading + "#"
    out_dir.mkdir(parents=True, exist_ok=True)
    md = [f"{h2} {number}. 거동 (03 M9, 02 §13 behavior)", "", condition["markdown"], ""]
    n_unsupported = len(analysis.unsupported)
    md.append(f"분석한 거동 run {len(analysis.results)}개 (states.ptst + scene_desc.json), 조인트 capability가 없어 벤치가 거부한 run {n_unsupported}개 "
              f"(`unsupported`, §{number}.6), 분석하지 못한 run {len(analysis.skipped) + len(analysis.errors)}개 (§{number}.10). 모든 수치는 기록된 바디 상태와 "
              "`scene_desc.json`의 하네스 기하로 계산했다 (엔진의 접촉 보고는 쓰지 않는다). 해석 기준은 dt_engine 이산해다 (`analytic.py`). "
              "기준이 없는 관측량은 주 series(jolt.Default, bullet.Default, physx.UE4Compat)의 중앙값(합의값)과 비교한다. 등록 허용치를 넘는 차이마다 "
              "태그 하나와 사유가 붙는다 (§" + number + ".2, 규칙과 허용치는 §" + number + ".9). 혼돈계 씬은 같은 엔진 1e-4 cm 섭동 복제의 퍼짐을 허용치에 더한다.")
    md.append("")
    # 1. criteria
    md += [f"{h3} {number}.1 03 M9 완료 기준 (Normalized, 주 series)", "", condition["markdown"], "", "| 기준 | series | 판정 | 내용 |", "|---|---|---|---|"]
    for row in analysis.criteria:
        verdict = row["verdict"]
        shown = verdict if verdict in ("pass", "reported", "not run", "unsupported") else f"**{verdict}**"   # failures, findings, qualified verdicts
        md.append(f"| {_esc(row['criterion'])} | {row['series']} | {shown} | {_esc(row['detail'])} |")
    md += ["", "판정은 리드 결정(2026-10-22, `behavior_rules.LEAD_DECISIONS`, Progress M9 리드 결정)을 따른다. "
           "`fail (accepted engine behaviour)`: 실패이고, 리드가 엔진 거동으로 받아들인 실패다 (조정해서 숨기지 않았다). 실패한 관측량이 그 메커니즘의 "
           "데이터 서명을 확인하는 규칙으로 태그될 때만 이 표시가 붙고, 다른 원인의 실패는 그냥 `fail`이다. "
           "`finding: joints separated`: 에너지는 오르지 않지만 조인트가 한 고리 길이 넘게 벌어졌다 - 떨어져 나간 체인은 자유 낙하로 에너지를 지키므로 "
           "에너지 검사가 자명하다. 통과가 아니라 별도 발견(조인트 분리)으로 보고한다. "
           "`reported` / `reported (beyond 0.5%)`: 판정하지 않고 측정값만 보고하는 줄이다 (`M6a` `Joint=Ball` 변형: 03의 5° 주기 기준은 카탈로그 씬 "
           "`Joint=Hinge`로 판정한다). "
           "`fail (pending override)`: 실패한 값이 실행 뒤 등록을 제안한 엔진별 허용치 안이다 (리드 승인 전에는 판정에 쓰지 않는다, §" + number + ".9; "
           "M9 커밋에서 모든 등록이 승인되어 지금은 없다). "
           "`M4` 줄은 `BoxYaw` 0(상자 축이 경사 방향)과 등방성 변형 `BoxYaw` 45를 같은 한계로 따로 판정한다. "
           "`unsupported`: 이 벤치의 백엔드가 조인트 capability를 선언하지 않았다 (조인트 구현 뒤 같은 매트릭스로 다시 돌린다).", ""]
    # 2. differences
    diffs = analysis.differences
    md += [f"{h3} {number}.2 엔진 간 차이와 태그", "", condition["markdown"], ""]
    if diffs.empty:
        md += ["허용치를 넘는 차이가 없다.", ""]
    else:
        counts = diffs["tag"].value_counts()
        md.append("차이 " + str(len(diffs)) + "개: " + ", ".join(f"**{tag}** {counts.get(tag, 0)}" for tag in rules.TAGS) +
                  f". 규칙 없이 남은 차이(`unexplained`, mismatch 잠정) {int((diffs['rule'] == 'unexplained').sum())}개. 차이 전체는 `differences.csv`에 있다.")
        md += ["", "| 규칙 | 태그 | 확신 | 차이 수 | 예 (series · profile · scene · metric = 값 / 기준, 허용치) | 사유 (첫 차이) |", "|---|---|---|---|---|---|"]
        for (rule_id, tag), group in diffs.groupby(["rule", "tag"], sort=False):
            first = group.iloc[0]
            examples = "; ".join(f"{r['label']} · {r['profile'][:4]} · {r['scene']} · {r['metric']} = {_fmt(r['value'])} / {_fmt(r['reference'])}, {_fmt(r['effective_tolerance'], 3)}"
                                 for r in group.head(3).to_dict("records"))
            more = f" (+{len(group) - 3})" if len(group) > 3 else ""
            # M9 fix round 0: the confidence is per difference (the branch of the rule that matched): every level with its count
            levels = group["confidence"].value_counts()
            confidence = " / ".join(f"{level} {count}" for level, count in levels.items()) if len(levels) > 1 else str(first["confidence"])
            md.append(f"| `{rule_id}` | {tag} | {confidence} | {len(group)} | {_esc(examples)}{more} | {_esc(first['reason'])} |")
        md.append("")
        drop = [c for c in ("run_key", "pending_overrides") if c in diffs.columns]
        diffs.drop(columns=drop).to_csv(out_dir / "differences.csv", index=False)
    # 3. M1 profiles
    md += [f"{h3} {number}.3 `M1.SingleBoxDrop` UEParity 대 Normalized (기본값이 만든 차이)", "", condition["markdown"], ""]
    if analysis.ue_vs_normalized:
        md += ["| series | case | damping U / N | Δz fall 측정 | Δz 예측 (0.01) | 잔차 | Jolt 기본 0.05였다면 | sleep (U) | 반발 임계 U / N (cm/s) | 정지 xy U / N (cm) | 태그 |",
               "|---|---|---|---|---|---|---|---|---|---|---|"]
        for row in analysis.ue_vs_normalized:
            tags = ", ".join(f"{i['item']}: {i['tag']}" for i in row["items"])
            md.append(f"| {row['label']} | {row['case']} | {_fmt(row['damping_uep'])} / {_fmt(row['damping_norm'])} | {row['dz_measured_cm']:+.4f} | {row['dz_predicted_cm']:+.4f} | "
                      f"{row['dz_residual_cm']:+.1e} | {row['dz_if_jolt_native_cm']:+.4f} | {_fmt(row['sleep_uep_s'])} | {_fmt(row['threshold_uep_cms'])} / "
                      f"{_fmt(row['threshold_norm_cms'])} | {_fmt(row['final_xy_uep'])} / {_fmt(row['final_xy_norm'])} | {tags} |")
        reasons: dict[str, str] = {}
        for row in analysis.ue_vs_normalized:
            for item in row["items"]:
                reasons.setdefault(f"{item['item']} ({item['tag']}, {row['label'] if item['tag'] == 'mismatch' else 'all'})", item["reason"])
        md.append("")
        for key, reason in list(reasons.items())[:8]:
            md.append(f"- **{_esc(key)}:** {_esc(reason)}")
        md.append("")
        if plot_m1_profiles(analysis, out_dir / "behavior_m1_profiles.png", condition["figure"]):
            md += ["![M1 UEParity - Normalized](behavior_m1_profiles.png)", ""]
    else:
        md += ["두 프로필의 `M1.SingleBoxDrop` run이 함께 있는 series가 없다.", ""]
    # 4. scene tables
    md += [f"{h3} {number}.4 결정적 씬 (바디별 비교)", "", condition["markdown"], ""]
    md += _scene_tables(analysis)
    if plot_m4(analysis, out_dir / "behavior_m4.png", condition["figure"]):
        md += ["![M4 d(2 s) error](behavior_m4.png)", ""]
    if plot_m5(analysis, out_dir / "behavior_m5.png", condition["figure"]):
        md += ["![M5 e_k](behavior_m5.png)", ""]
    # 5. chaotic
    md += [f"{h3} {number}.5 혼돈계 씬: 같은 엔진 섭동 민감도와 최종 높이의 Wasserstein 거리", "", condition["markdown"], ""]
    md += _chaotic_tables(analysis)
    if plot_tower(analysis, out_dir / "behavior_tower.png", condition["figure"]):
        md += ["![M2a tower sensitivity](behavior_tower.png)", ""]
    # 6. joints
    md += [f"{h3} {number}.6 조인트 씬 (`M6a.Pendulum`, `M6b.Chain`, `M6c.HeavyChain`)", "", condition["markdown"], ""]
    md += _joint_tables(analysis)
    # 7. variant knobs
    md += [f"{h3} {number}.7 변형 knob: 엔진 주 series와의 상태 동일성", "", condition["markdown"], ""]
    md += _knob_table(analysis)
    # 8. M7
    md += [f"{h3} {number}.8 `M7.UEDefaults`", "", condition["markdown"], ""]
    md += _m7_table(analysis)
    # 9. registry
    md += [f"{h3} {number}.9 허용치와 태그 규칙 (`analysis/behavior_rules.py`)", ""]
    md += ["| 관측량 | 허용치 | 출처 | 등록 | 사유 | 엔진별 (등록, 승인) |", "|---|---|---|---|---|---|"]
    for key, tol in rules.TOLERANCES.items():
        overrides = "; ".join(f"{', '.join(o.labels)}: {o.value:g} ({o.registered}, {o.sign_off})" for o in tol.overrides)
        md.append(f"| `{key}` | {tol.value:g} {tol.unit} | {tol.source} | {tol.registered} | {_esc(tol.reason)} | {_esc(overrides) or '-'} |")
    for engine, proposal in rules.PROPOSED_RESTITUTION_MODELS.items():
        md.append(f"| 반발 속도 모델 제안 ({engine}) | model {proposal['model']} | M9 analysis | {proposal['registered']} ({proposal['sign_off']}) | "
                  f"{_esc(proposal['reason'])} | tolerances.json G4에 등록하면 판정에 쓴다 |")
    for key, decision in rules.LEAD_DECISIONS.items():
        text = decision["text"] + (f" / {decision['separated']}" if decision.get("separated") else "")
        md.append(f"| 리드 결정 (`{key}`) | {decision['kind']} | 03 M9 기준 | {decision['sign_off']} | {_esc(text)} | "
                  f"{_esc(', '.join(decision.get('labels', ())) or '-')}{' (규칙 ' + ', '.join(decision['rules']) + ')' if decision.get('rules') else ''} |")
    md += ["", "| 규칙 | 태그 | 확신 | 근거 |", "|---|---|---|---|"]
    for rule in rules.RULES + [rules.UNEXPLAINED]:
        md.append(f"| `{rule.id}` | {rule.tag} | {rule.confidence} | {_esc(rule.refs)} |")
    md += ["", "태그: **by-design** = 엔진에 일부러 다른 것을 줬다 (설정·변형·프로필 기본값·정책). **mismatch** = 같은 것을 주려 했는데 못 줬다 (아직 하지 않은 매핑, 다른 입력), "
           "또는 설명이 없다 (`unexplained`, 잠정). **algorithmic** = 같은 입력·설정에서 알고리즘이 다르다. 확신: verified = 이 데이터에서 메커니즘을 쟀다, "
           "accepted = 리드가 받아들인 결정 (Progress), inferred = 추정 [R]. 위 표의 확신은 규칙의 기본값이고, 차이마다 맞은 분기의 확신은 §" + number + ".2 표와 "
           "`differences.csv`에 있다.", ""]
    # 10. not analysed
    md += [f"{h3} {number}.10 분석하지 못한 run", ""]
    if not (analysis.skipped or analysis.errors):
        md += ["없음.", ""]
    else:
        md += ["| run | 사유 |", "|---|---|"]
        for row in analysis.skipped + analysis.errors:
            md.append(f"| `{_esc(row.get('run_dir'))}` | {_esc(row.get('reason') or row.get('error'))} |")
        md.append("")
    common.write_json_atomic(out_dir / "behavior.json", to_json(analysis))
    return md


def _metric_table(analysis: Analysis, scenario: str, metrics: list[tuple[str, str]], profile: str = "Normalized") -> list[str]:
    obs = analysis.observations
    if obs.empty:
        return []
    rows = obs[(obs["scenario"] == scenario) & (obs["profile"] == profile)]
    if rows.empty:
        return []
    scenes = sorted(rows["scene"].unique())
    labels = [l for l in MAIN_SERIES if l in set(rows["label"])] + sorted(l for l in set(rows["label"]) if l not in MAIN_SERIES)
    md = ["| scene | series | " + " | ".join(_esc(title) for _, title in metrics) + " |", "|---|---|" + "---|" * len(metrics)]
    for scene in scenes:
        for label in labels:
            own = rows[(rows["scene"] == scene) & (rows["label"] == label)]
            if own.empty:
                continue
            cells = []
            for metric, _ in metrics:
                hit = own[own["metric"] == metric]
                if hit.empty:
                    cells.append("-")
                    continue
                r = hit.iloc[0]
                text = _fmt(r["value"])
                if r["compared"] and r["within"] is False:
                    text = f"**{text}**"
                cells.append(text)
            md.append(f"| {scene} | {label} | " + " | ".join(cells) + " |")
    md.append("")
    return md


def _scene_tables(analysis: Analysis) -> list[str]:
    md = ["굵은 값은 등록 허용치 밖이다 (태그가 붙은 차이). 표는 Normalized다 (UEParity는 `behavior.json`, `differences.csv`).", ""]
    md += ["**`M1.SingleBoxDrop`** (fall: 감쇠 이산 낙하와의 최대 차이, 접촉 전)", ""]
    md += _metric_table(analysis, "M1.SingleBoxDrop", [("fall_dz_max_cm", "fall |dz| (cm)"), ("first_contact_frame", "첫 접촉 frame"), ("final_z_cm", "정지 z (cm)"),
                                                        ("final_xy_cm", "정지 xy (cm)"), ("final_tilt_deg", "기울기 (°)"), ("rest_penetration_cm", "정지 관통 (cm)"),
                                                        ("rest_jitter_cm", "jitter (cm)")])
    md += ["**`M4.InclineFriction`** (d(2 s) = frame 119, 이산 기준 a·dt²·n(n+1)/2 대비; μ ≥ 0.6은 경사면 위 이동)", ""]
    md += _metric_table(analysis, "M4.InclineFriction", [("d2s_err_pct", "d(2 s) 오차 (%)"), ("hold_drift_cm", "정지 μ drift (cm)"), ("creep_cms", "creep (cm/s)"),
                                                          ("lateral_cm", "옆 이동 (cm)"), ("contact_penetration_cm", "SAT 관통 (cm)")])
    md += ["**`M5.BouncingBalls`** (e_k = −v_n(c) / v_n(c−1); 모델 = 등록된 반발 속도 모델)", ""]
    metrics = []
    for e in ("0.3", "0.5", "0.7", "0.9"):
        metrics += [(f"e{e}.e_dev_model_max", f"e{e} max|e−모델|"), (f"e{e}.e_dev_nominal_max", f"e{e} max|e−e0|")]
    md += _metric_table(analysis, "M5.BouncingBalls", metrics)
    md += _metric_table(analysis, "M5.BouncingBalls", [(f"e{e}.judged_impacts", f"e{e} 판정 충돌") for e in ("0.3", "0.5", "0.7", "0.9")] +
                        [(f"e{e}.apex1_ratio_err", f"e{e} apex1 오차") for e in ("0.3", "0.9")] + [(f"e{e}.depth_max_cm", f"e{e} 최대 관통 (cm)") for e in ("0.7", "0.9")])
    return md


def _chaotic_tables(analysis: Analysis) -> list[str]:
    md = []
    def order(r: dict[str, Any]) -> tuple:
        rank = 0 if r["label"] in MAIN_SERIES else (2 if r["probe"] else 1)
        return (r["scene"], r["profile"], rank, MAIN_SERIES.index(r["label"]) if r["label"] in MAIN_SERIES else 0, r["label"])

    rows = sorted(analysis.sensitivity, key=order)
    if rows:
        md += ["섭동 복제(`Perturb` 1~3)는 모든 동적 바디를 1e-4 cm 옮긴다 (Scenes.md §1). 발산 = 기준 run과 복제의 바디 COM 거리 최대값. "
               "첫 스텝 뒤 발산 / 1e-4 cm가 첫 스텝의 증폭이다 (정확히 맞닿게 쌓은 탑은 첫 스텝에 접촉 집합이 섭동에 따라 바뀌면 크게 증폭된다).", "",
               "| scene | profile | series | 복제 | 첫 스텝 뒤 발산 (cm) | 1 cm 넘는 첫 frame | 최종 최대 발산 (cm) | 맨 위 z (기준 run / 퍼짐) | 평균 z (기준 / 퍼짐) | 흔들림 최대 (기준 / 퍼짐) | 넘어진 비율 | W₁ 자기 (cm) |",
               "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            md.append(f"| {r['scene']} | {r['profile']} | {r['label']}{' (probe)' if r['probe'] else ''} | {r['replicas']} | {_fmt(r.get('divergence_first_frame_cm'), 3)} | "
                      f"{_fmt(r.get('first_frame_over_1cm'))} | "
                      f"{_fmt(r.get('divergence_final_max_cm'))} | {_fmt(r.get('top_z_final_cm.base'))} / {_fmt(r.get('top_z_final_cm.spread'))} | "
                      f"{_fmt(r.get('mean_z_final_cm.base'))} / {_fmt(r.get('mean_z_final_cm.spread'))} | {_fmt(r.get('top_sway_max_cm.base'))} / "
                      f"{_fmt(r.get('top_sway_max_cm.spread'))} | {_fmt(r.get('toppled_fraction.base'))} | {_fmt(r.get('w_within_cm'))} |")
        md.append("")
    if analysis.wasserstein:
        md += ["최종 바디 높이 분포의 1-Wasserstein 거리: 엔진 간 W₁(두 series의 단일 run 쌍마다 잰 W₁의 평균)이 두 series 중 큰 자기 W₁(한 series의 "
               "단일 run 쌍 평균, 기준 run과 복제) 이하이면 같은 엔진 1e-4 cm 민감도와 구별되지 않는다. 두 항 모두 단일 run끼리 잰다 (M9 수정 라운드 1: 처음 판은 "
               "엔진 간 항만 복제를 합친 분포로 재서 표본 잡음이 작아 \"within\" 쪽으로 기울었다).", "", "| scene | profile | a | b | W₁ a↔b (cm) | W₁ a 자기 | W₁ b 자기 | 비 | 판정 |", "|---|---|---|---|---|---|---|---|---|"]
        for r in analysis.wasserstein:
            md.append(f"| {r['scene']} | {r['profile']} | {r['a']} | {r['b']} | {_fmt(r['w_cross_cm'])} | {_fmt(r['w_within_a_cm'])} | {_fmt(r['w_within_b_cm'])} | "
                      f"{_fmt(r['ratio'], 3)} | {r['verdict']} |")
        md.append("")
    return md or ["혼돈계 씬 run이 없다.", ""]


def _joint_tables(analysis: Analysis) -> list[str]:
    md = []
    joint_runs = [r for r in analysis.runs if r.scenario in ("M6a.Pendulum", "M6b.Chain", "M6c.HeavyChain") and id(r) in analysis.results]
    if joint_runs:
        md += _metric_table(analysis, "M6a.Pendulum", [("period_err_pct", "주기 오차 (%)"), ("amplitude_decay_pct_per_period", "진폭 감소 (%/주기)"),
                                                        ("stretch_max_cm", "조인트 신장 (cm)"), ("rod_length_err_max_cm", "막대 길이 오차 (cm)"), ("out_of_plane_max_cm", "평면 밖 (cm)")])
        for chain in ("M6b.Chain", "M6c.HeavyChain"):
            md += _metric_table(analysis, chain, [("stretch_max_cm", "신장 최대 (cm)"), ("stretch_late_mean_cm", "late 신장 (cm)"), ("energy_growth_pct", "에너지 증가 (%)"),
                                                  ("energy_late_slope_pct_per_s", "late 기울기 (%/s)")])
    refused = [u for u in analysis.unsupported if str(u.get("scene", "")).startswith(("M6a.", "M6b.", "M6c."))]
    if refused:
        labels = sorted({str(u.get("label")) for u in refused})
        md += [f"벤치가 `unsupported`로 거부한 조인트 run {len(refused)}개 (series: {', '.join(labels)}). 이 벤치의 백엔드는 `PT_CAP_JOINT_*`을 선언하지 않는다. "
               "분석 코드(주기 영점 교차, 조인트 프레임 원점 거리, 에너지)는 합성 데이터로 시험했고 (`ptbench.tests.test_behavior`), 조인트 구현이 들어온 벤치로 같은 매트릭스를 "
               "돌리면 이 절이 채워진다.", ""]
    return md or ["조인트 씬 run이 없다.", ""]


def _knob_table(analysis: Analysis) -> list[str]:
    if not analysis.variant_knobs:
        return ["비교할 변형 쌍이 없다.", ""]
    md = ["| 변형 | 주 series | profile | 같은 scene (모든 frame 비트 동일) | 갈린 scene (첫 frame) |", "|---|---|---|---|---|"]
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    for row in analysis.variant_knobs:
        groups.setdefault((row["variant"], row["main"], row["profile"]), []).append(row)
    def summary(rows: list[dict[str, Any]], identical: bool) -> str:
        by_scene: dict[str, list[dict[str, Any]]] = {}
        for r in rows:
            if r["identical"] == identical:
                by_scene.setdefault(r["scene"].split("[")[0], []).append(r)   # one entry per scenario (params and replicas counted)
        parts = []
        for scene, members in by_scene.items():
            if identical:
                parts.append(f"{scene}{f' ×{len(members)}' if len(members) > 1 else ''}")
            else:
                frames = sorted(int(m["first_divergent_frame"]) for m in members)
                shown = f"{frames[0]}" if frames[0] == frames[-1] else f"{frames[0]}-{frames[-1]}"
                parts.append(f"{scene}{f' ×{len(members)}' if len(members) > 1 else ''} ({shown})")
        return ", ".join(parts) or "-"

    for (variant, main, profile), rows in groups.items():
        md.append(f"| {variant} | {main} | {profile} | {_esc(summary(rows, True))} | {_esc(summary(rows, False))} |")
    md += ["", "씬마다 한 칸: ×n은 그 씬의 run 수(파라미터·섭동 복제 포함), 괄호는 처음 갈린 frame(범위)이다. 행별 값은 `behavior.json`의 `variant_knobs`에 있다. "
           "`bullet.StaticAabbOff`는 한 바디 씬(M1)과 `M2a.Tower` N=10의 기준 run에서 모든 frame이 같고, 여러 바디가 닿는 더미·무너지는 탑(N=30)과 "
           "섭동 복제 하나(N=10 Perturb 3)에서는 갈린다 (Progress M9 준비 F-1: 강제 AABB 갱신이 static proxy를 dynamic 트리로 옮겨 쌍 순서가 바뀐다). "
           "**리드 결정 (2026-10-22):** 03의 G11 문장(해시가 같다)은 좁힌 범위(한 바디·조인트·탑 N=10 기준 run, 결정성 켠 `M3.BoxPile` 1k Normalized)에서 "
           "판정하고, 더미에서는 결과 중립이 아닌 by-design knob이다 - 비교에서 `bullet.Default`와 같은 것으로 다루지 않는다 (Progress M9 리드 결정). "
           "`M3.BoxPile`은 100 frame마다 기록하므로 첫 갈림 frame은 그 해상도다. "
           "`physx.TGS-R0`·`physx.TGS-Pos32`·`physx.SdkDefault-Pos8`은 M9 수정 라운드 1의 `M3.BoxPile` probe다 (규칙 physx-tgs-pile-ejection, physx-sdkdefault-pile).", ""]
    return md


def _m7_table(analysis: Analysis) -> list[str]:
    obs = analysis.observations
    if obs.empty:
        return ["`M7.UEDefaults` run이 없다.", ""]
    rows = obs[obs["scenario"] == "M7.UEDefaults"]
    if rows.empty:
        return ["`M7.UEDefaults` run이 없다.", ""]
    md = ["| profile | series | 튐 (바디 1-6) | 잠들기 (s) | 질량 규칙 오차 (kg) |", "|---|---|---|---|---|"]
    for (profile, label), own in rows.groupby(["profile", "label"]):
        bounces = own[own["metric"].str.endswith(".bounces")].sort_values("metric")
        sleep = own[own["metric"] == "time_to_sleep_s"]
        mass = own[own["metric"] == "mass_rule_err_kg"]
        md.append(f"| {profile} | {label} | {' / '.join(_fmt(v) for v in bounces['value'])} | {_fmt(sleep['value'].iloc[0]) if not sleep.empty else 'off'} | "
                  f"{_fmt(mass['value'].iloc[0], 3) if not mass.empty else '-'} |")
    md += ["", "질량은 모든 엔진에서 같은 `scene_desc.json` 값이다 (UE (ρV)^0.75, Scenes.md §2.22). 튐 수는 프로필의 반발 임계(UEParity: 엔진별 매핑)와 각 엔진의 반발 모델이 정한다.", ""]
    return md


# ---- CLI ----------------------------------------------------------------------------------------------------------------------

def main(args) -> int:
    """python -I -m ptbench behavior <inputs> [--out DIR] [--json FILE]: the console summary; with --out also the Markdown
    section (index.md), its PNGs, differences.csv and behavior.json. Exit code 0 analysed, 1 a criterion failed (fail, not
    'fail (pending override)' or 'fail (accepted engine behaviour)' - the lead's decisions, behavior_rules.LEAD_DECISIONS),
    2 a path that does not exist (M10 fix round 0, like the other analysis commands) or no behaviour run in the inputs."""
    from . import load as _load

    missing = [p for p in args.inputs if not pathlib.Path(p).exists()]
    if missing:
        print(f"ptbench behavior: no such path: {', '.join(missing)}")
        return 2
    if not _load.discover(args.inputs):
        print("ptbench behavior: no run found under " + ", ".join(args.inputs))
        return 2
    analysis = analyze(args.inputs)
    if not analysis.results:
        print("ptbench behavior: no behaviour run (states.ptst + scene_desc.json) under " + ", ".join(args.inputs)
              + (f" ({len(analysis.unsupported)} unsupported, {len(analysis.skipped)} skipped)" if analysis.unsupported or analysis.skipped else ""))
        return 2
    print(f"behaviour runs analysed: {len(analysis.results)}; unsupported: {len(analysis.unsupported)}; skipped: {len(analysis.skipped)}; errors: {len(analysis.errors)}")
    for row in analysis.criteria:
        print(f"  {row['verdict']:<24} {row['id']:<18} {row['series']:<16} {row['detail'][:150]}")
    if not analysis.differences.empty:
        print("differences by tag: " + ", ".join(f"{k} {v}" for k, v in analysis.differences["tag"].value_counts().items()))
        for (rule_id, tag), group in analysis.differences.groupby(["rule", "tag"]):
            print(f"  {tag:<12} {rule_id:<34} {len(group):>4}")
    if args.out:
        from . import report as _report

        out = pathlib.Path(args.out)
        condition = _report.conditions(analysis.sessions, analysis.table)
        md = ["# 거동 분석 (UNOFFICIAL)", ""] + markdown_section(analysis, out, condition)
        common.write_text_atomic(out / "index.md", "\n".join(md) + "\n")
        print(f"behaviour section: {out / 'index.md'}")
    if args.json:
        common.write_json_atomic(args.json, to_json(analysis))
        print(f"json: {args.json}")
    return 1 if any(row["verdict"] == "fail" for row in analysis.criteria) else 0
