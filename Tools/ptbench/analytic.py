"""analytic - the analytic references with dt_engine (DL23; 02_Architecture 13 "analytic"; 03_WorkPlan M8).

Every reference uses the dt the engines receive: dt_engine = (double)(float)dt, emulated with numpy.float32 (the C++ side,
Analytic::EngineDeltaTime in Source/Runtime/PhysicsCore/Public/Physics/AnalyticReference.h, rounds through a volatile float
so /fp:fast cannot fold it). The +-1e-9 tolerance applies only against references computed this way. The functions mirror the
C++ ones expression for expression; cases() is the same case table as PhysicsTestBench -AnalyticReferences, and
`python -I -m ptbench analytic --check-cpp <PhysicsTestBench.exe>` (CTest PhysicsTest.Py.AnalyticMatchesCpp) recomputes every
C++ value from its printed inputs and requires |difference| <= 1e-9.
M9 adds behaviour references used by analysis/behavior.py (incline d(t) discrete and continuous, the step delay of a slide,
the damped fall series, the discrete flight apex, the restitution velocity models, the small-angle pendulum). They are
Python-only and outside cases(), so the C++ comparison table stays the C++ one.

    python -I -m ptbench analytic                       the table (S00 z60 = 501.8332813705 cm, ...)
    python -I -m ptbench analytic --check-cpp <exe>     compare with the C++ references of that build
Exit code: 0 every value within tolerance, 1 a difference (or a documented value) out of tolerance, 2 the C++ references
could not be read (no such exe, it failed, its output or --cpp-json is not a physicstest.analytic/1 document; one line on
stderr, no traceback).
"""
from __future__ import annotations

import json
import math
import subprocess
import sys
from typing import Any

import numpy as np

TOLERANCE = 1e-9

UE_ORDER = 1        # body.damping_formula 1: v <- (v + g dt) * max(0, 1 - c dt), then z <- z + v dt
BEFORE_FORCES = 2   # body.damping_formula 2: v <- v * max(0, 1 - c dt) + g dt, then z <- z + v dt


def engine_dt(dt: float) -> float:
    """dt_engine = (double)(float)dt: 1/60 -> 0.01666666753590107 (not 0.016666666666666666)."""
    return float(np.float32(dt))


def semi_implicit_euler_free_fall(z0: float, v0: float, gravity_z: float, dt: float, steps: int) -> tuple[float, float]:
    """Closed form of semi-implicit Euler under constant acceleration (AnalyticReference.cpp SemiImplicitEulerFreeFall):
    v_n = v0 + n g dt, z_n = z0 + n v0 dt + g dt^2 n (n + 1) / 2."""
    n = float(steps)
    vz = v0 + n * gravity_z * dt
    z = z0 + n * v0 * dt + gravity_z * dt * dt * n * (n + 1.0) * 0.5
    return z, vz


def damped_free_fall(z0: float, gravity_z: float, linear_damping: float, dt: float, steps: int, formula: int) -> tuple[float, float]:
    """G1b (S00d): steps of a linearly damped fall from rest with the declared formula (AnalyticReference.cpp DampedFreeFall)."""
    multiplier = 1.0 - linear_damping * dt if linear_damping * dt < 1.0 else 0.0
    z, vz = z0, 0.0
    for _ in range(steps):
        vz = (vz + gravity_z * dt) * multiplier if formula == UE_ORDER else vz * multiplier + gravity_z * dt
        z += vz * dt
    return z, vz


def friction_slide_stop_distance(v0: float, mu: float, g: float, dt: float) -> float:
    """G5 (S06): every step removes mu g dt of speed (never below 0), then the position advances by the new speed."""
    decrement = mu * g * dt
    speed = v0
    distance = 0.0
    while speed > 0.0 and decrement > 0.0:
        speed = speed - decrement if speed > decrement else 0.0
        distance += speed * dt
    return distance


def step_boundary_drop_height(g: float, dt: float, k: int) -> float:
    """G4 (S05): the drop that touches exactly at the end of step k of the discrete trajectory: g dt^2 k (k + 1) / 2."""
    n = float(k)
    return g * dt * dt * n * (n + 1.0) * 0.5


def complete_elliptic_k(modulus: float) -> float:
    """K(k) by the arithmetic-geometric mean (Analytic::CompleteEllipticK, M9)."""
    a, b = 1.0, math.sqrt(1.0 - modulus * modulus)
    for _ in range(64):
        if not abs(a - b) > 1e-16 * a:
            break
        a, b = 0.5 * (a + b), math.sqrt(a * b)
    return 3.14159265358979323846 / (2.0 * a)


def pendulum_period(length: float, bob_radius: float, g: float, amplitude_rad: float) -> float:
    """M6a.Pendulum (Analytic::PendulumPeriod, M9): a solid sphere bob on a massless rod, any amplitude:
    T = 2 pi sqrt((L^2 + 2/5 r^2) / (g L)) * 2 K(sin(a / 2)) / pi."""
    pi = 3.14159265358979323846
    small_angle = 2.0 * pi * math.sqrt((length * length + 0.4 * bob_radius * bob_radius) / (g * length))
    return small_angle * 2.0 * complete_elliptic_k(math.sin(0.5 * amplitude_rad)) / pi


def incline_slide_distance(mu: float, angle_rad: float, g: float, dt: float, steps: int) -> float:
    """M4.InclineFriction (Analytic::InclineSlideDistance, M9): semi-implicit Euler from rest, a = g (sin - mu cos) when > 0."""
    acceleration = g * (math.sin(angle_rad) - mu * math.cos(angle_rad))
    if not acceleration > 0.0:
        return 0.0
    n = float(steps)
    return acceleration * dt * dt * n * (n + 1.0) * 0.5


# ---- M9 behaviour references (Python only: behavior.py compares recorded trajectories with them) ------------------------------
# These are not in cases(): the C++ table of -AnalyticReferences is the gate scenes' and M9's canonical inputs, and
# compare_with_cpp requires both tables to hold the same cases. Every one uses the dt it is given (pass dt_engine).

def incline_acceleration(mu: float, angle_rad: float, g: float) -> float:
    """a = g (sin - mu cos) down the slope, 0 when the box holds (Coulomb friction, static = dynamic)."""
    acceleration = g * (math.sin(angle_rad) - mu * math.cos(angle_rad))
    return acceleration if acceleration > 0.0 else 0.0


def incline_slide_distance_series(mu: float, angle_rad: float, g: float, dt: float, steps: Any) -> np.ndarray:
    """incline_slide_distance for every step count of an array (the discrete reference d_n = a dt^2 n (n + 1) / 2)."""
    n = np.asarray(steps, dtype=float)
    return incline_acceleration(mu, angle_rad, g) * dt * dt * n * (n + 1.0) * 0.5


def incline_slide_distance_damped_series(mu: float, angle_rad: float, g: float, linear_damping: float, dt: float, steps: Any,
                                         formula: int = UE_ORDER) -> np.ndarray:
    """The discrete slide with linear damping on the net acceleration, step by step in the declared formula's order
    (UEParity's 0.01; equal to incline_slide_distance_series for damping 0): d_n for every step count of an array."""
    n = np.asarray(steps, dtype=np.int64)
    if not linear_damping:
        return incline_slide_distance_series(mu, angle_rad, g, dt, n)
    distance, _ = damped_free_fall_series(0.0, 0.0, incline_acceleration(mu, angle_rad, g), linear_damping, dt, int(max(int(n.max()), 1)), formula)
    return distance[np.clip(n, 1, None) - 1] * (n > 0)


def incline_slide_distance_continuous(mu: float, angle_rad: float, g: float, t: Any) -> np.ndarray:
    """The continuous-time reference a t^2 / 2 (an upper bound of the discrete one: d_n / d(t_n) = (n + 1) / n)."""
    t = np.asarray(t, dtype=float)
    return 0.5 * incline_acceleration(mu, angle_rad, g) * t * t


def discrete_step_delay(distance: float, acceleration: float, dt: float, steps: int) -> float:
    """How many steps late a measured slide is against d_n = a dt^2 n (n + 1) / 2: solve d = a dt^2 m (m + 1) / 2 for the
    real m and return steps - m (0 = on the reference, 1 = one step late)."""
    if not acceleration > 0.0 or not distance > 0.0:
        return math.nan
    k = 2.0 * distance / (acceleration * dt * dt)
    m = 0.5 * (-1.0 + math.sqrt(1.0 + 4.0 * k))
    return float(steps) - m


def damped_free_fall_series(z0: float, v0: float, gravity_z: float, linear_damping: float, dt: float, steps: int,
                            formula: int = UE_ORDER) -> tuple[np.ndarray, np.ndarray]:
    """z_n, v_n for n = 1..steps of damped_free_fall (any v0): index n - 1 holds the state after step n."""
    multiplier = 1.0 - linear_damping * dt if linear_damping * dt < 1.0 else 0.0
    z, vz = z0, v0
    zs = np.empty(steps)
    vs = np.empty(steps)
    for i in range(steps):
        vz = (vz + gravity_z * dt) * multiplier if formula == UE_ORDER else vz * multiplier + gravity_z * dt
        z += vz * dt
        zs[i] = z
        vs[i] = vz
    return zs, vs


def discrete_flight_apex(h0: float, v0: float, g: float, dt: float) -> tuple[float, int]:
    """The highest point of a semi-implicit Euler flight from height h0 with upward speed v0: h_n = h0 + n v0 dt -
    g dt^2 n (n + 1) / 2 is largest at the integer n nearest to v0 / (g dt) - 1/2. Returns (apex height, n)."""
    if not v0 > 0.0:
        return h0, 0
    best_n = max(0, int(math.floor(v0 / (g * dt) - 0.5)))
    best, chosen = -math.inf, 0
    for n in (best_n - 1, best_n, best_n + 1, best_n + 2):
        if n < 0:
            continue
        h = h0 + n * v0 * dt - g * dt * dt * n * (n + 1) * 0.5
        if h > best:
            best, chosen = h, n
    return best, chosen


def continuous_flight_apex(h0: float, v0: float, g: float) -> float:
    return h0 + (v0 * v0) / (2.0 * g) if v0 > 0.0 else h0


RESTITUTION_MODEL_BEFORE_STEP = 0     # tolerances.json G4.restitution_velocity_model 0: -e x the velocity before the contact step
RESTITUTION_MODEL_PRESOLVE = 1        # 1: -e x the pre-solve velocity that holds the contact step's gravity (PhysX, M6 D-3)
RESTITUTION_MODEL_CONTACT_STATE = 2   # 2: -e x the velocity before the step, corrected by the contact state at the step start (Bullet,
                                      # M9 S-3; signed off with the M9 commit): behavior.contact_state_restitution, which needs the gap


def restitution_prediction(e: float, speed_before: float, g: float, dt: float, model: int) -> float:
    """The e an engine shows as -v_after / v_before (frames c and c - 1) under its registered velocity model. Model 2's gap term needs
    the contact state (behavior.contact_state_restitution); without it, this is its touch-at-a-step-boundary value e (= model 0)."""
    if model == RESTITUTION_MODEL_PRESOLVE:
        return e * (1.0 + g * dt / speed_before) if speed_before > 0.0 else math.nan
    return e


def restitution_approach_speed(speed_before: float, g: float, dt: float, model: int) -> float:
    """The normal speed the engine compares with its restitution threshold: |v(c - 1)| (models 0 and 2: Bullet's restitution curve
    takes the velocity before the step) or |v(c - 1)| + g dt (1)."""
    return speed_before + g * dt if model == RESTITUTION_MODEL_PRESOLVE else speed_before


def pendulum_small_angle_period(length: float, bob_radius: float, g: float) -> float:
    """The physical pendulum's small-angle period 2 pi sqrt((L^2 + 2/5 r^2) / (g L)) (pendulum_period at amplitude 0)."""
    return 2.0 * 3.14159265358979323846 * math.sqrt((length * length + 0.4 * bob_radius * bob_radius) / (g * length))


def simple_pendulum_period(length: float, g: float, amplitude_rad: float) -> float:
    """A point mass on a massless rod (r = 0): for the record, 03 M9's references are the physical pendulum's."""
    return pendulum_period(length, 0.0, g, amplitude_rad)


def evaluate(function: str, inputs: dict[str, Any]) -> dict[str, float]:
    """One case of the C++ table, recomputed from its inputs."""
    if function == "EngineDeltaTime":
        return {"dt_engine": engine_dt(inputs["dt"])}
    if function == "SemiImplicitEulerFreeFall":
        z, vz = semi_implicit_euler_free_fall(inputs["z0"], inputs["v0"], inputs["gravity_z"], inputs["dt"], int(inputs["steps"]))
        return {"z": z, "vz": vz}
    if function == "DampedFreeFall":
        z, vz = damped_free_fall(inputs["z0"], inputs["gravity_z"], inputs["linear_damping"], inputs["dt"], int(inputs["steps"]), int(inputs["formula"]))
        return {"z": z, "vz": vz}
    if function == "FrictionSlideStopDistance":
        return {"distance": friction_slide_stop_distance(inputs["v0"], inputs["mu"], inputs["g"], inputs["dt"])}
    if function == "StepBoundaryDropHeight":
        g, dt, k = inputs["g"], inputs["dt"], int(inputs["k"])
        return {"height": step_boundary_drop_height(g, dt, k), "touch_speed": g * dt * float(k)}
    if function == "PendulumPeriod":
        amplitude = inputs["amplitude_deg"] * 3.14159265358979323846 / 180.0
        return {"period": pendulum_period(inputs["length"], inputs["bob_radius"], inputs["g"], amplitude)}
    if function == "InclineSlideDistance":
        angle = inputs["angle_deg"] * 3.14159265358979323846 / 180.0
        return {"distance": incline_slide_distance(inputs["mu"], angle, inputs["g"], inputs["dt"], int(inputs["steps"]))}
    raise ValueError(f"unknown analytic function '{function}'")


def cases() -> list[dict[str, Any]]:
    """The case table of PhysicsTestBench -AnalyticReferences (the gate scenes' canonical inputs, GateScenes.h)."""
    dte = engine_dt(1.0 / 60.0)
    table = []

    def add(case_id: str, function: str, **inputs: Any) -> None:
        table.append({"id": case_id, "function": function, "inputs": inputs, "values": evaluate(function, inputs)})

    for case_id, dt in (("dt_engine.1_60", 1.0 / 60.0), ("dt_engine.1_30", 1.0 / 30.0), ("dt_engine.1_120", 1.0 / 120.0), ("dt_engine.1_50", 1.0 / 50.0)):
        add(case_id, "EngineDeltaTime", dt=dt)
    add("S00.free_fall_dt_engine", "SemiImplicitEulerFreeFall", z0=1000.0, v0=0.0, gravity_z=-980.0, dt=dte, steps=60)
    add("S00.free_fall_exact_1_60", "SemiImplicitEulerFreeFall", z0=1000.0, v0=0.0, gravity_z=-980.0, dt=1.0 / 60.0, steps=60)
    add("free_fall_v0_30_steps", "SemiImplicitEulerFreeFall", z0=500.0, v0=250.0, gravity_z=-980.0, dt=dte, steps=30)
    add("S00d.damped_ue_order", "DampedFreeFall", z0=1000.0, gravity_z=-980.0, linear_damping=0.5, dt=dte, steps=60, formula=UE_ORDER)
    add("S00d.damped_before_forces", "DampedFreeFall", z0=1000.0, gravity_z=-980.0, linear_damping=0.5, dt=dte, steps=60, formula=BEFORE_FORCES)
    add("damped_heavy_c80", "DampedFreeFall", z0=1000.0, gravity_z=-980.0, linear_damping=80.0, dt=dte, steps=60, formula=UE_ORDER)
    add("S06.friction_stop", "FrictionSlideStopDistance", v0=500.0, mu=0.5, g=980.0, dt=dte)
    add("friction_stop_mu_0_3", "FrictionSlideStopDistance", v0=300.0, mu=0.3, g=980.0, dt=dte)
    add("S05.drop_height_k38", "StepBoundaryDropHeight", g=980.0, dt=dte, k=38)
    add("drop_height_k10", "StepBoundaryDropHeight", g=980.0, dt=dte, k=10)
    # M9 behaviour references (BehaviorScenes.h): M6a periods, M4 discrete d(2 s) = 120 steps.
    for case_id, angle in (("M6a.period_5deg", 5.0), ("M6a.period_30deg", 30.0), ("M6a.period_90deg", 90.0)):
        add(case_id, "PendulumPeriod", length=100.0, bob_radius=5.0, g=980.0, amplitude_deg=angle)
    for case_id, mu in (("M4.d2s_mu_0_30", 0.30), ("M4.d2s_mu_0_50", 0.50), ("M4.d2s_mu_0_55", 0.55), ("M4.d2s_mu_0_60", 0.60)):
        add(case_id, "InclineSlideDistance", mu=mu, angle_deg=30.0, g=980.0, dt=dte, steps=120)
    return table


# The values the documents quote (02 14.3, 03 M6 / M8, Scenes.md), with the precision they are quoted to.
DOCUMENTED = [
    ("dt_engine.1_60", "dt_engine", 0.01666666753590106964111328125, 1e-18),
    ("S00.free_fall_dt_engine", "z", 501.8332813705, 1e-9),         # 03 M8: "z60 = 501.8332813705 cm"
    ("S00.free_fall_dt_engine", "vz", -980.0000511110, 1e-9),       # 02 14.3 G1: v60 = -980.0000511
    ("S00.free_fall_exact_1_60", "z", 501.8333333333, 1e-9),        # the exact 1/60 reference (wrong for the gates)
    ("S06.friction_stop", "distance", 250.947, 5e-4),               # G5: 250.947 cm (continuous 255.10 is wrong)
    ("S05.drop_height_k38", "height", 201.7167, 5e-5),              # G4: 201.7167 cm
    ("S05.drop_height_k38", "touch_speed", 620.67, 5e-3),           # G4: 620.67 cm/s
    ("M6a.period_5deg", "period", 2.00905, 5e-6),                   # 03 M9: 2.00905 s (g 980, L 100, r 5)
    ("M6a.period_30deg", "period", 2.04305, 5e-6),                  # 03 M9: 2.04305 s
    ("M6a.period_90deg", "period", 2.37023, 5e-6),                  # 03 M9: 2.37023 s
    ("M4.d2s_mu_0_60", "distance", 0.0, 0.0),                       # 03 M9: mu >= 0.6 holds (tan 30 = 0.577)
]


def check_documented() -> list[dict[str, Any]]:
    table = {case["id"]: case for case in cases()}
    out = []
    for case_id, key, expected, tolerance in DOCUMENTED:
        value = table[case_id]["values"][key]
        out.append({"id": case_id, "key": key, "python": value, "documented": expected, "difference": value - expected, "tolerance": tolerance,
                    "ok": abs(value - expected) <= tolerance})
    return out


def compare_with_cpp(cpp: dict[str, Any], tolerance: float = TOLERANCE) -> list[dict[str, Any]]:
    """Every C++ case recomputed from its printed inputs, plus the case table itself: the same ids and inputs both sides."""
    out = []
    if cpp.get("schema") != "physicstest.analytic/1":
        raise ValueError(f"not a physicstest.analytic/1 document: {cpp.get('schema')}")
    mine = {case["id"]: case for case in cases()}
    seen = set()
    for case in cpp["cases"]:
        seen.add(case["id"])
        try:
            values = evaluate(case["function"], case["inputs"])
        except (KeyError, ValueError, TypeError) as error:
            # An unknown function or an input the Python side does not take: a FAIL row, not an exception.
            out.append({"id": case["id"], "key": "<evaluate>", "python": None, "cpp": None, "difference": math.nan, "tolerance": tolerance, "ok": False,
                        "note": f"cannot recompute the C++ case in Python: {type(error).__name__} {error}"})
            values = {}
        for key, cpp_value in case["values"].items():
            if key not in values:
                if values:
                    out.append({"id": case["id"], "key": key, "python": None, "cpp": None, "difference": math.nan, "tolerance": tolerance, "ok": False,
                                "note": f"the C++ case has a value '{key}' that analytic.evaluate does not compute"})
                continue
            difference = values[key] - cpp_value
            out.append({"id": case["id"], "key": key, "python": values[key], "cpp": cpp_value, "difference": difference, "tolerance": tolerance,
                        "ok": math.isfinite(difference) and abs(difference) <= tolerance})
        table_case = mine.get(case["id"])
        same_inputs = (table_case is not None and table_case["function"] == case["function"]
                       and set(table_case["inputs"]) == set(case["inputs"])
                       and all(abs(float(table_case["inputs"][k]) - float(v)) <= 0.0 for k, v in case["inputs"].items()))
        out.append({"id": case["id"], "key": "<case table>", "python": None, "cpp": None, "difference": 0.0, "tolerance": 0.0, "ok": same_inputs,
                    "note": "" if same_inputs else "the C++ case's function / inputs differ from analytic.cases()"})
    for case_id in sorted(set(mine) - seen):
        out.append({"id": case_id, "key": "<missing in C++>", "python": None, "cpp": None, "difference": math.nan, "tolerance": 0.0, "ok": False,
                    "note": "analytic.cases() has a case the C++ table lacks"})
    return out


class CppReferenceError(RuntimeError):
    """The C++ references could not be read: no such exe, it failed, or its output is not JSON (exit code 2, one line)."""


def run_cpp(exe: str) -> dict[str, Any]:
    try:
        completed = subprocess.run([exe, "-AnalyticReferences"], capture_output=True, timeout=120, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CppReferenceError(f"cannot run '{exe} -AnalyticReferences': {error}") from None
    if completed.returncode != 0:
        raise CppReferenceError(f"{exe} -AnalyticReferences exited {completed.returncode}: {completed.stderr.decode('utf-8', 'replace')[-2000:]}")
    try:
        return json.loads(completed.stdout.decode("utf-8"))
    except ValueError as error:
        raise CppReferenceError(f"{exe} -AnalyticReferences did not print JSON: {error}") from None


def read_cpp_json(path: str) -> dict[str, Any]:
    try:
        with open(path, encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, ValueError) as error:
        raise CppReferenceError(f"cannot read the C++ references from '{path}': {error}") from None


def main(args) -> int:
    documented = check_documented()
    print("analytic references (dt_engine = (double)(float)dt):")
    for case in cases():
        values = ", ".join(f"{k} {v:.13g}" for k, v in case["values"].items())
        print(f"  {case['id']:<28} {values}")
    failed = 0
    print("documented values:")
    for row in documented:
        failed += 0 if row["ok"] else 1
        print(f"  {'ok  ' if row['ok'] else 'FAIL'} {row['id']}.{row['key']}: {row['python']:.13g} vs {row['documented']:.13g} (|d| {abs(row['difference']):.3g} <= {row['tolerance']:g})")
    cpp = None
    try:
        if args.cpp_json:
            cpp = read_cpp_json(args.cpp_json)
        elif args.check_cpp:
            cpp = run_cpp(args.check_cpp)
    except CppReferenceError as error:
        print(f"ptbench analytic: {error}", file=sys.stderr)
        return 2
    if cpp is not None:
        try:
            rows = compare_with_cpp(cpp)
        except (ValueError, KeyError, TypeError) as error:
            print(f"ptbench analytic: not a usable physicstest.analytic/1 document: {type(error).__name__} {error}", file=sys.stderr)
            return 2
        worst = max((abs(r["difference"]) for r in rows if r["cpp"] is not None), default=0.0)
        print(f"C++ comparison ({cpp.get('preset', '?')} build, {len(cpp['cases'])} cases, tolerance {TOLERANCE:g}):")
        for row in rows:
            if row["ok"] and row["cpp"] is None:
                continue
            failed += 0 if row["ok"] else 1
            if row["cpp"] is None:
                print(f"  FAIL {row['id']} {row['key']}: {row.get('note', '')}")
            else:
                print(f"  {'ok  ' if row['ok'] else 'FAIL'} {row['id']}.{row['key']}: python {row['python']:.17g}, C++ {row['cpp']:.17g}, |d| {abs(row['difference']):.3g}")
        print(f"largest |python - C++| = {worst:.3g} ({'PASS' if failed == 0 else 'FAIL'})")
    return 0 if failed == 0 else 1
