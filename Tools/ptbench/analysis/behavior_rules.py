"""behavior_rules - the registered tolerances of the behaviour observations and the tag rules of cross-engine differences
(03_WorkPlan M9: "모든 엔진 간 차이에 by-design / mismatch / algorithmic 태그가 붙는다"; 02_Architecture 13).

Tags (one per difference):
  by-design    the engines were given, on purpose, something different: a declared engine or variant setting (contact
               distance, penetration slop, convex radius, iteration counts of a variant), a profile default (UE damping,
               sleep on / off, the restitution threshold mapping of DL35 / Q11), a documented policy (DL41, DL47)
  mismatch     the engines were meant to get the same thing and did not (a harness mapping not done yet - e.g. UEParity's UE
               sleep thresholds on Jolt / Bullet, effective sleep.criteria "not mapped"; different scene inputs), or nothing
               explains the difference yet: rule "unexplained", provisional, to be investigated - never silently accepted
  algorithmic  same input and same settings; the engines' algorithms differ (PGS friction axes, restitution velocity
               models, the contact state Bullet's restitution target uses, solver iterations on a stack)
Confidence of a reason: "verified" (the mechanism is measured in this data, e.g. a model that predicts every impact, or a
probe series that isolates it), "accepted" (a lead decision recorded in docs/Progress.md), "inferred" ([R]: plausible from
the engine's design and the data's shape, not isolated). M9 fix round 0: the confidence belongs to the branch that matched - a
rule's reason function may return (text, confidence) and Rule.confidence is only its default (e.g. bullet-chain: M6c verified by
the Bullet implementer's iteration probes, M6b inferred). M9 fix round 1: a rule claims verified / accepted only where it checks its
mechanism in the data (a magnitude bound, the backend, the swing angle, a probe outcome); a branch without such a guard is inferred,
and a rule whose guard fails returns None, so a regression in a later rerun is not explained away. chaotic-solver speaks only where
both series have Perturb replicas (their spread measured); M3.BoxPile (no replicas) has its own rules and probes. M9 fix round 2: the
four rules still without a guard got one - impact-phase-depth (was contact-distance, by-design: every impact within its contact-distance
window, algorithmic / inferred), bullet-p2p-rows (stretch beyond the hard-joint bound and the hinge's, a longer period), bullet-stack-solver
(the stack fell; verified only where both probe changes together hold it and each alone does not), jolt-heavy-chain (UEParity: every run
apart and the jolt.Damp0 probe equal to Normalized; Normalized: the energy and stretch bounds, inferred).
M9 close (checker / reviewer r3, lead 2026-10-22): every rule now has a data guard. jolt-penetration-slop (by-design, verified) checks the
magnitude, not only the depth: a lower rest height must be the run's own rest penetration, a stack height must stand below the consensus by
no more than Jolt's slop x the contacts below it - a Jolt stack that fell or a box on its edge is not this rule's. The inferred rules that
had none got a minimal signature (chain-iterative-joints: the chain held - stretch below one link - and kept its energy; physx-tgs-*: TGS's
own signature - every run's energy above the release energy, a swing that gains amplitude with a longer period, a creep under 0.2 cm/s
within its registered drift; bullet-sliding-box: Bullet's box rocks - |w| above both other engines' - and lags; bullet-tumble /
edge-landing: the box ends on a face within one edge length of the consensus; bullet-chain M6b: the chain held, kept its energy and Bullet
stretches most; chaotic-solver: consensus observables only). physx-pgs-creep's accepted D-4 branches speak only within the creep and
drift the lead accepted (G5's 3 cm/s, the signed M4 6 cm): with those values signed, a PGS drift difference lies beyond them and
surfaces. What a guard checks differs in strength: verified / accepted = the mechanism is
measured in this data; inferred = the difference has the mechanism's signature, the mechanism itself is not isolated, so an inferred tag can
still absorb a regression that keeps the signature - the 03 criteria rows (analytic bounds, judged on the data) are what catches a
regression, not the count of tags.

Tolerances: the value a deviation must exceed to count as a difference, each with its reason, its source (03 M9's completion
criteria, the gates' tolerances.json values, or this analysis's choice - for the lead to review) and whether it was registered
before the comparisons were made (pre-run) or changed after looking at them (post-run). A post-run override applies to the verdicts only
once signed off (sign_off != "pending"); pending overrides are reported as proposals. M9 fix round 0: a base value raised after
looking at the data is no different - the pre-run value stays the base and the raised one is a pending override of every series
("*"), so no post-run value decides a verdict before the lead signs it. M9 commit: the lead signed every post-run override here and Bullet's
restitution model 2 (now registered in tolerances.json G4) on 2026-10-22 (LEAD_SIGN_OFF; docs/Progress.md M9 리드 결정); a new post-run value
is pending again until signed (tests: no pending entry at a commit). LEAD_DECISIONS holds the lead's reading of the 03 M9 criteria rows.
"""
from __future__ import annotations

import dataclasses
import fnmatch
import math
import re
from typing import Any, Callable

TAGS = ("by-design", "mismatch", "algorithmic")

# The lead's sign-off of the M9 post-run registrations (docs/Progress.md M9 리드 결정; the same text in Config/Golden/tolerances.json).
LEAD_SIGN_OFF = "lead 2026-10-22, M9 commit (accepted as engine behaviour / regression bound; mechanism recorded in Progress M9)"

# Restitution velocity models proposed for registration in Config/Golden/tolerances.json G4.restitution_velocity_model
# (registered: 0 = the velocity before the contact step, 1 = PhysX's pre-solve velocity with the step's gravity, 2 = Bullet's contact
# state at the step start). Until the lead registers one, behaviour verdicts use the registered model and report the proposal as
# "fail (pending override)" (metric e_dev_proposed_max). M9 commit: Bullet's model 2 - proposed here in M9 - is registered in
# tolerances.json G4 and signed off by the lead (2026-10-22): no proposal is open.
PROPOSED_RESTITUTION_MODELS: dict[str, dict[str, Any]] = {}

# The lead's reading of the 03 M9 criteria rows (2026-10-22, docs/Progress.md M9 리드 결정, docs/03_WorkPlan.md M9). criteria() applies
# them: "reported" rows are shown with their measurement but not judged; "accepted" failures stay failures, labelled
# "fail (accepted engine behaviour)", and only where the failing observation is tagged by the named rule (which checks the mechanism's
# signature on the data - a failure with another cause stays a plain "fail"); "separated" turns a trivially held energy check of a chain
# whose joints came apart into a finding instead of a pass.
LEAD_DECISIONS: dict[str, dict[str, Any]] = {
    "M6a.period.ball": {"kind": "reported", "sign_off": "lead 2026-10-22",
                        "text": "03 M9's 5 deg period criterion is judged on the catalogued scene M6a.Pendulum (Joint=Hinge); the Joint=Ball variant "
                                "is reported, not gated (Bullet ball +8.7%: btPoint2PointConstraint, Progress M9 Bullet J-1, algorithmic)"},
    "M6c.energy": {"kind": "accepted", "labels": ("physx.*",), "sign_off": "lead 2026-10-22",
                   "rules": ("physx-pgs-heavy-chain", "physx-tgs-heavy-chain"),
                   "text": "PhysX PGS and TGS energy growth on the 100:1 heavy chain is accepted as an engine-behaviour finding (algorithmic: no "
                           "joint projection in PhysX 5.x, which UE4 relied on; Progress M9 PX-1, PX-1b) - reported as a failure, not tuned away",
                   "separated": "Bullet's joints come apart on the 100:1 chain (stretch beyond one link): its energy check holds trivially (a "
                                "separated chain falls freely), so it is reported as a separate finding - joint separation (Progress M9 Bullet "
                                "J-2) - not as a pass"},
}
PGS_PHYSX = ("physx.UE4Compat", "physx.UE4Compat-Stab", "physx.SdkDefault")
# M9: the joint solvers that work on velocities and correct positions afterwards (Jolt: 10 velocity / 2 position steps; PhysX PGS:
# velocity rows, position iterations); Bullet's sequential impulse is velocity-level too but corrects positions by Baumgarte only.
VELOCITY_JOINT_SOLVERS = ("jolt.Default", "jolt.SharpBoxes") + PGS_PHYSX
CHAIN_SCENES = ("M6b.Chain", "M6c.HeavyChain")


@dataclasses.dataclass(frozen=True)
class Override:
    labels: tuple[str, ...]          # series labels (fnmatch patterns)
    value: float
    registered: str                  # "post-run"
    sign_off: str                    # "pending" or who / when
    reason: str
    profiles: tuple[str, ...] = ("Normalized", "UEParity")


@dataclasses.dataclass(frozen=True)
class Tolerance:
    value: float
    unit: str
    registered: str                  # "pre-run" / "post-run"
    reason: str
    overrides: tuple[Override, ...] = ()
    source: str = "M9 analysis"      # who set the value: "03 M9" (a completion criterion), "G1" / "G4" (tolerances.json), or this analysis

    def resolve(self, label: str, profile: str) -> tuple[float, str, list[Override]]:
        """(tolerance that applies, its source, pending overrides that would apply)."""
        pending = []
        for override in self.overrides:
            if profile in override.profiles and any(fnmatch.fnmatchcase(label, pattern) for pattern in override.labels):
                if override.sign_off == "pending":
                    pending.append(override)
                else:
                    return override.value, f"override ({override.registered}, {override.sign_off})", pending
        return self.value, self.registered, pending


T = Tolerance
O = Override

TOLERANCES: dict[str, Tolerance] = {
    # ---- analytic references ------------------------------------------------------------------------------------------
    "M1.fall_dz_max_cm": T(2e-3, "cm", "pre-run", "G1's float-in-cm tolerance (03 M3/M6): the fall window is S00's discrete free fall with the body's damping", source="G1"),
    "M4.d2s_err_pct": T(1.0, "%", "pre-run", "03 M9 completion criterion: d(2 s) within 1% of the discrete reference (Normalized, sliding mu)", (
        O(PGS_PHYSX, 6.5, "post-run", LEAD_SIGN_OFF, "PhysX PGS friction (M6 D-4 mechanism, accepted as engine behaviour): +2.9% at mu 0.55 / +6.3% with BoxYaw 45 "
          "(an early overshoot that decays; Progress M9 준비 F-3)"),
        O(("bullet.Default",), 8.0, "post-run", LEAD_SIGN_OFF, "Bullet's sliding box rocks on its 4-point manifold: -1.3% / -1.9% at mu 0.55 / 0.50, -7.6% at mu 0.50 "
          "BoxYaw 45 (inferred, see rule bullet-sliding-box)"),
    ), source="03 M9"),
    "M4.hold_drift_cm": T(0.1, "cm", "pre-run", "03 M9 completion criterion: mu >= 0.6 holds, drift < 0.1 cm at 2 s (Normalized)", (
        O(PGS_PHYSX, 6.0, "post-run", LEAD_SIGN_OFF, "the M6 D-4 PGS creep (accepted for G5 as engine behaviour: 2.5 cm/s; Progress M9 준비 F-2: 4.65 cm at "
          "frame 120); the M4 value signed off by the lead with the M9 commit"),
        O(("physx.UE4Compat-TGS",), 0.3, "post-run", LEAD_SIGN_OFF, "TGS creeps about 0.065 cm/s on a mu 0.6 incline (M6 G5, F-2): 0.13-0.15 cm at 2 s"),
    ), source="03 M9"),
    # M9 fix round 0: no TGS override (the 0.6 proposed after the data sat on the observed 0.598 and bounded nothing); TGS's split
    # impacts are told apart by a mechanism test in rule physx-tgs-split-contact, which also reports its single-frame impacts' fit.
    "M5.e_dev_model_max": T(0.02, "", "pre-run", "03 M9: e_k of every impact above the threshold within the engine's registered tolerance (default +-0.02) of "
                            "its registered restitution velocity model (tolerances.json G4.restitution_velocity_model)", source="03 M9"),
    "M5.e_dev_nominal_max": T(0.02, "", "pre-run", "the cross-engine comparable: e_k against the material's e (03 default +-0.02)", source="03 M9"),
    "M5.e_dev_proposed_max": T(0.02, "", "post-run", "e_k of the contact step against the engine's proposed model (PROPOSED_RESTITUTION_MODELS): "
                               "not a verdict until the model is registered in tolerances.json G4"),
    "M5.apex1_ratio_err": T(0.02, "", "pre-run", "G4 secondary: first apex against the discrete flight from the contact height with the model's v_after", source="G4"),
    "M6a.period_err_pct": T(0.5, "%", "pre-run", "03 M9 completion criterion: period error < 0.5% at 5 deg (reported for 30 / 90 deg too)", source="03 M9"),
    "M6a.amplitude_decay_pct_per_period": T(1.0, "%", "pre-run", "a hard joint without damping keeps its amplitude (Normalized damping 0)"),
    "M6a.stretch_max_cm": T(0.1, "cm", "pre-run", "hard joint (Abi.md 9.4 4): the two frame origins stay together"),
    "M6a.rod_length_err_max_cm": T(0.1, "cm", "pre-run", "hard joint: the bob's COM stays on its circle"),
    "M6a.out_of_plane_max_cm": T(0.1, "cm", "pre-run", "hinge: the bob stays in the swing plane"),
    "M6b.stretch_max_cm": T(1.0, "cm", "pre-run", "03 M9 reports the chain stretch; 1 cm marks a visible gap (40 cm links)"),
    "M6b.energy_growth_pct": T(1.0, "% of m g span", "pre-run", "03 M9: the energy does not diverge"),
    "M6c.stretch_max_cm": T(1.0, "cm", "pre-run", "as M6b, with the 100:1 end mass"),
    "M6c.energy_growth_pct": T(1.0, "% of m g span", "pre-run", "as M6b"),
    "M7.mass_rule_err_kg": T(1e-3, "kg", "pre-run", "docs/Scenes.md 2.22 UE (rho V)^0.75 masses, quoted to 1e-3 kg"),
    "*.sleeping_body_frames": T(0.0, "body-frames", "pre-run", "a profile with sleep off (Normalized) must never sleep a body (DL48)"),
    # ---- cross-engine consensus (median of jolt.Default, bullet.Default, physx.UE4Compat) -----------------------------------
    "M1.first_contact_frame": T(0.5, "frames", "pre-run", "one frame is a difference"),
    "M1.final_z_cm": T(0.5, "cm", "pre-run", "rest height of a 100 cm cube"),
    "M1.final_tilt_deg": T(2.0, "deg", "pre-run", "a box at rest on a face"),
    "M1.final_xy_cm": T(2.0, "cm", "pre-run", "where the tilted box ends after its edge landing", (
        O(("*",), 10.0, "post-run", LEAD_SIGN_OFF, "the engines spread over a few cm (Jolt's rounded corner 44.6, the sharp boxes 48-52 cm); a difference would be "
          "another landing outcome (raised from 2 cm after the M9 data; a regression bound, signed off by the lead with the M9 commit)"),
    )),
    "*.rest_penetration_cm": T(0.25, "cm", "pre-run", "harness-geometry depth at rest"),
    "*.contact_penetration_cm": T(0.25, "cm", "pre-run", "harness-geometry depth while sliding"),
    "*.rest_jitter_cm": T(0.05, "cm", "pre-run", "RMS COM motion at rest"),
    "*.time_to_sleep_s": T(0.25, "s", "pre-run", "15 frames"),
    "*.sleep_speed_cms": T(5.0, "cm/s", "pre-run", "a body at rest moves well under 5 cm/s: a body put to sleep faster than that was still moving"),
    "M4.lateral_cm": T(0.5, "cm", "pre-run", "sideways drift of a box sliding straight down"),
    "M5.depth_max_cm": T(1.0, "cm", "pre-run", "deepest harness-geometry depth of a ball in its impacts"),
    "M7.bounces": T(0.5, "count", "pre-run", "bounces above the profile's threshold: any difference counts"),
    "M7.final_tilt_deg": T(2.0, "deg", "pre-run", "a box at rest on a face"),
    # chaotic aggregates: the tolerance adds the engines' own perturbation spread (02 13)
    "M2a.top_z_final_cm": T(2.0, "cm", "pre-run", "chaotic: plus the same-engine 1e-4 cm spread"),
    "M2a.mean_z_final_cm": T(1.0, "cm", "pre-run", "chaotic: plus the same-engine spread"),
    "M2a.top_sway_max_cm": T(5.0, "cm", "pre-run", "chaotic: plus the same-engine spread"),
    "M2a.toppled_fraction": T(0.05, "", "pre-run", "chaotic: plus the same-engine spread"),
    "M3.top_z_final_cm": T(5.0, "cm", "pre-run", "chaotic pile (no replicas: Seed changes the layout)", (
        O(("*",), 20.0, "post-run", LEAD_SIGN_OFF, "a standing pile's top box sits 9 box-box contacts up, each with the engine's resting overlap (Jolt up to its 2 cm "
          "slop: 18 cm): 937-950 cm (raised from 5, then 10 cm after the M9 data; a regression bound, signed off by the lead with the M9 commit)"),
    )),
    "M3.mean_z_final_cm": T(5.0, "cm", "pre-run", "chaotic pile", (
        O(("*",), 10.0, "post-run", LEAD_SIGN_OFF, "a standing pile settles at 494-500 cm (raised from 5 cm after the M9 data; a regression bound, signed off by "
          "the lead with the M9 commit)"),
    )),
    "M3.toppled_fraction": T(0.05, "", "pre-run", "chaotic pile"),
    "M6b.final_z_mean_cm": T(5.0, "cm", "pre-run", "chaotic chain: plus the same-engine spread"),
    "M6c.final_z_mean_cm": T(5.0, "cm", "pre-run", "chaotic chain: plus the same-engine spread"),
}


def tolerance_for(scenario: str, metric: str) -> tuple[str, Tolerance] | None:
    """The registered tolerance of <scene prefix>.<metric> (e.g. M4.d2s_err_pct), else the generic *.<metric>. A per-ball M5
    metric "e0.9.e_dev_model_max" and a per-body M7 metric "b1.box.bounces" use their last part."""
    prefix = scenario.split(".", 1)[0]
    base = metric.split(".")[-1] if prefix in ("M5", "M7") and metric.count(".") >= 1 else metric
    for key in (f"{prefix}.{metric}", f"{prefix}.{base}", f"*.{base}"):
        if key in TOLERANCES:
            return key, TOLERANCES[key]
    return None


# ---- tag rules ------------------------------------------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class Rule:
    id: str
    tag: str
    confidence: str
    refs: str
    reason: Callable[[dict[str, Any], Any], str | None]   # (difference, context) -> reason text, or None when the rule does not apply


def _metric(diff: dict[str, Any]) -> str:
    return str(diff["metric"]).split(".")[-1]


def _rule_desc_mismatch(diff, ctx) -> str | None:
    descs = ctx.desc_hashes(diff["scenario_group"], diff["profile"])
    if len(descs) > 1:
        return (f"the runs of this scene and profile did not simulate the same input: {len(descs)} desc_sha256 values "
                f"({', '.join(d[:12] for d in sorted(descs))}) - the comparison itself is invalid (02 12.3)")
    return None


def _rule_sleep_in_sleepless_profile(diff, ctx) -> str | None:
    if _metric(diff) == "sleeping_body_frames" and (diff["value"] or 0) > 0:
        return f"{int(diff['value'])} body-frames asleep although the profile turns sleep off (effective sleep.enabled {ctx.effective(diff, 'sleep.enabled')})"
    return None


def _rule_bullet_restitution(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or not str(diff["metric"]).endswith(("e_dev_model_max", "e_dev_nominal_max")):
        return None
    check = ctx.bullet_restitution_check(diff)
    if check is None or check["n"] == 0 or check["worst_residual"] > 0.02:
        return None
    return (f"Bullet's restitution target follows the contact state at the start of the step (btSequentialImpulseConstraintSolver.cpp): a "
            f"speculative contact (gap > 0) subtracts gap/dt (:953), a penetration shallower than the split-impulse threshold "
            f"({check['threshold']:g} cm, effective bullet.split_impulse_penetration_threshold) puts erp2 x depth / dt ({check['erp2']:g}) into the "
            f"velocity (:957, :963), a deeper one goes to the split impulse and keeps e (registered restitution model 2, tolerances.json G4, signed "
            f"off by the lead with the M9 commit). That model predicts all {check['n']} judged impacts of this ball "
            f"(worst residual {check['worst_residual']:.4f}; {check['n_speculative']} speculative, {check['n_baumgarte']} shallow, {check['n_split']} deep). "
            f"Only the first impact touches at a step boundary; later ones start mid-step because Bullet's contact distance is "
            f"{ctx.effective(diff, 'contact.speculative_distance')} cm")


def _rule_physx_restitution(diff, ctx) -> str | None:
    if diff["backend"] != "PhysX" or not str(diff["metric"]).endswith("e_dev_nominal_max"):
        return None
    model_dev = ctx.metric_value(diff, str(diff["metric"]).replace("e_dev_nominal_max", "e_dev_model_max"))
    if model_dev is None or not model_dev <= 0.02:
        return None
    return (f"PhysX applies -e to the pre-solve normal velocity that already holds the contact step's gravity (DyDynamics.cpp:1979, "
            f"DyContactPrepShared.h:305-323): e_meas = e (1 + g dt / |v|), so e grows as the impacts slow (registered model 1, tolerances.json "
            f"G4; M6 D-3). Every judged impact is within {model_dev:.2g} of that model")


def _rule_physx_tgs_split_contact(diff, ctx) -> str | None:
    if diff["label"] != "physx.UE4Compat-TGS" or not str(diff["metric"]).endswith(("e_dev_model_max", "e_dev_nominal_max")):
        return None
    bad = ctx.deviating_impacts(diff)
    if not bad or not all(i["frames_in_contact"] >= 2 or i["touch_phase"] > 1.0 for i in bad):
        return None
    shown = ", ".join(f"frame {i['frame']} ({i['frames_in_contact']} frames, touch phase {i['touch_phase']:.2f}, e {i['e']:.3f})" for i in bad[:3])
    ball = ctx.ball(diff) or {}
    single = [i for i in ball.get("impacts", []) if i["judged"] and i["frames_in_contact"] < 2 and i["touch_phase"] <= 1.0]
    fit = max((abs(i["e"] - i["e_pred"]) for i in single), default=math.nan)
    return (f"TGS resolves an impact that starts as a speculative contact (touch phase > 1: the discrete path reaches the pad only in the next "
            f"step) over two frames: the first removes approach speed without restitution, the second bounces the rest ({shown}). Told apart by "
            f"that mechanism test (frames_in_contact >= 2 or touch phase > 1), not by a wider tolerance: the {len(single)} single-frame judged "
            f"impacts of this ball follow registered model 1 within {fit:.2g}")


IMPACT_DEPTH_SLACK_CM = 0.1   # float poses and the recorded state's rounding around an impact's window (cm)


def _rule_impact_phase_depth(diff, ctx):
    # M9 fix round 2 (checker r2): the depth of an impact is the travel of the step that reaches the pad minus the gap at that step's
    # start, so it follows the impact's phase, which follows each engine's bounce history (restitution model, rebound heights):
    # algorithmic, inferred - no contact-distance probe isolates it. The configured contact distance d only bounds the window: with a gap
    # above d no contact exists and the ball goes at most (travel - d) deep before the next step's contact stops it; with a gap within d
    # the contact exists from the step start and stops the ball at the pad or above it, at most d above. Data guard: every impact of this
    # ball lies in its window [-d, max(travel - d, 0)], else the depth is not this mechanism (None - e.g. a ball that sinks 40 cm). The
    # round-1 rule (contact-distance: "the engines differ by their configured distance", by-design) was contradicted by the data: the
    # same engine's depths vary from impact to impact with the phase.
    if _metric(diff) != "depth_max_cm":
        return None
    spec = ctx.effective(diff, "contact.speculative_distance")
    ball = ctx.ball(diff)
    impacts = (ball or {}).get("impacts") or []
    if not isinstance(spec, (int, float)) or not impacts:
        return None
    run = ctx.run(diff)
    dt, g = run.dt, run.g
    windows = []
    for impact in impacts:
        travel = (impact["speed_before"] + g * dt) * dt          # bounds the free step that reached the pad (speed_before x dt) from above
        lower, upper = -float(spec), max(travel - float(spec), 0.0)
        if not (lower - IMPACT_DEPTH_SLACK_CM <= impact["depth_max_cm"] <= upper + IMPACT_DEPTH_SLACK_CM):
            return None
        windows.append((impact, travel, upper))
    deepest, travel, upper = max(windows, key=lambda w: w[0]["depth_max_cm"])
    shown = ", ".join(f"frame {i['frame']} {i['depth_max_cm']:.2f}" for i, _, _ in windows[:8])
    return (f"an impact's depth is the travel of the step that reaches the pad minus the gap at that step's start: no contact exists while "
            f"that gap is above the engine's contact distance d = {spec} cm (effective contact.speculative_distance), so the ball goes at most "
            f"(travel - d) deep before the contact of the next step stops it, and with a gap within d the contact stops it at the pad or at "
            f"most d above it. The deepest impact of this ball (frame {deepest['frame']}) goes {deepest['depth_max_cm']:.3g} cm with a step of "
            f"at most {travel:.3g} cm (window up to {upper:.3g} cm); every impact lies in its window (depth cm by impact: {shown}). Where in the "
            f"window an impact lands is its phase, which follows the engine's bounce history (restitution model and rebound heights), so the "
            f"engines' deepest impacts differ (consensus {float(diff['reference']):.3g} cm). Not isolated by a contact-distance probe [R]", "inferred")


JOLT_SLOP_DEPTH_SLACK_CM = 0.25   # the harness-geometry depth tolerance (*.rest_penetration_cm) on top of Jolt's slop


def _rule_jolt_slop(diff, ctx) -> str | None:
    # M9 close (reviewer r3, major): the rule is by-design / verified, so every metric checks the slop's magnitude on the data, not only
    # that some depth is small. Depths: at most slop + 0.25 cm. A rest height (final_z_cm): lower than the consensus by the run's own rest
    # penetration (within the tolerance) and that penetration within the slop - a box on its edge (higher) or one that sank further is
    # not this. A stack height (top_z_final_cm / mean_z_final_cm): below the consensus by no more than slop x the contacts below it (the
    # run's stack_shape layers) and still standing - at least the overlap-free height less that much; a Jolt stack that fell is not this.
    if diff["backend"] != "Jolt" or _metric(diff) not in ("rest_penetration_cm", "contact_penetration_cm", "final_z_cm", "top_z_final_cm", "mean_z_final_cm"):
        return None
    slop = ctx.effective(diff, "jolt.penetration_slop")
    if not isinstance(slop, (int, float)) or slop <= 0.0:
        return None
    metric = _metric(diff)
    value, reference, tol = float(diff["value"]), float(diff["reference"]), float(diff["effective_tolerance"])
    head = (f"Jolt leaves an overlap up to mPenetrationSlop unresolved (effective jolt.penetration_slop {slop:g} cm, Jolt.Default.ini; "
            f"PhysicsSettings.h:53)")
    if metric in ("rest_penetration_cm", "contact_penetration_cm"):
        if not (finite(value) and value <= slop + JOLT_SLOP_DEPTH_SLACK_CM):
            return None
        return (f"{head}: a box that lands flat keeps about {slop:g} cm of depth, a box that rolls onto its face does not - here "
                f"{value:.3g} cm, within the slop + {JOLT_SLOP_DEPTH_SLACK_CM:g} cm")
    if metric == "final_z_cm":
        depth = ctx.metric_value(diff, "rest_penetration_cm")
        if not (finite(depth) and finite(value) and finite(reference) and value < reference and depth <= slop + JOLT_SLOP_DEPTH_SLACK_CM
                and abs((reference - value) - depth) <= tol):
            return None
        return (f"{head}: the box rests {reference - value:.3g} cm lower than the consensus {reference:.4g} cm, which is its own rest "
                f"penetration {depth:.3g} cm (within the slop; tolerance {tol:.3g} cm)")
    details = ctx.results[diff["run_key"]]["details"]
    top = metric == "top_z_final_cm"
    ideal, layers = details.get("ideal_top_z_cm" if top else "ideal_mean_z_cm"), details.get("layers" if top else "mean_layers_below")
    if not (finite(ideal) and finite(layers) and finite(value) and finite(reference)):
        return None
    bound = float(slop) * float(layers)
    if not (value < reference and reference - value <= bound + tol and value >= float(ideal) - bound - tol):
        return None
    return (f"{head}: {'the top box rests on' if top else 'a body rests on (mean)'} {float(layers):.3g} contacts, so Jolt's standing stack is up "
            f"to {bound:.3g} cm lower than an overlap-free one ({float(ideal):.4g} cm); here {value:.4g} cm, {reference - value:.3g} cm below the "
            f"consensus {reference:.4g} cm - within that bound and still standing")


def _rule_jolt_convex_radius(diff, ctx) -> str | None:
    if diff["backend"] != "Jolt" or diff["scenario"] != "M1.SingleBoxDrop" or _metric(diff) not in ("first_contact_frame", "final_xy_cm", "final_tilt_deg"):
        return None
    other = ctx.same_engine_value(diff, "jolt.SharpBoxes" if diff["label"] == "jolt.Default" else "jolt.Default")
    if other is None or other == diff["value"]:
        return None
    radius = ctx.effective(diff, "shape.box_margin")
    return (f"Jolt rounds box edges by the convex radius (effective shape.box_margin {radius} cm in {diff['label']}): the tilted box touches "
            f"and pivots on a rounded corner; jolt.SharpBoxes (ConvexRadius 0) gives {other:.4g} here (Profiles.md 4)")


PGS_CREEP_MAX_CMS = 3.0   # M9 close guard: the creep the lead accepted for D-4 - G5.hold_speed_cms of the PGS variants (tolerances.json, M6: 2.50 cm/s)


def _rule_physx_pgs_creep(diff, ctx) -> str | None:
    if diff["label"] not in PGS_PHYSX or diff["scenario"] != "M4.InclineFriction" or _metric(diff) not in ("hold_drift_cm", "creep_cms", "lateral_cm", "time_to_sleep_s"):
        return None
    if _metric(diff) in ("lateral_cm", "time_to_sleep_s") and ctx.mu(diff) < 0.58:
        return None
    creep = ctx.metric_value(diff, "creep_cms")
    creeping = creep is not None and abs(creep) >= 0.1
    # M9 fix round 1: the accepted D-4 branch only where the box creeps (>= 0.1 cm/s at 2 s). A drift without creep (BoxYaw 45, or a body
    # that UEParity put to sleep) is an early slip that stops; a time to sleep without creep is the sleep rule's (sleep-engine-rule);
    # the sideways drift shares the per-axis mechanism but the lead accepted the creep only.
    # M9 close guard (verifier): D-4 is a creep within G5's registered bound (3 cm/s) and a drift within the signed M4 value (6 cm at 2 s);
    # since that value is signed, a PGS drift difference lies beyond it - a faster creep or a larger drift is not D-4 (None: unexplained)
    if creeping and not abs(creep) <= PGS_CREEP_MAX_CMS:
        return None
    if creeping and _metric(diff) == "hold_drift_cm":
        signed = [o.value for o in TOLERANCES["M4.hold_drift_cm"].overrides if o.sign_off != "pending" and diff["label"] in o.labels]
        if not (signed and float(diff["value"]) <= max(signed)):
            return None
    if _metric(diff) == "time_to_sleep_s":
        if not creeping:
            return None
        return (f"PhysX PGS creep (Progress M6 D-4, accepted): the box keeps creeping at {creep:.3g} cm/s and never goes below the sleep threshold "
                f"(time to sleep {diff['value']:.3g} s)")
    if _metric(diff) == "lateral_cm":
        return (f"PhysX PGS on a holding incline: the per-axis friction rows (PxSceneDesc.h:30-42, the mechanism of M6 D-4) let the box drift "
                f"{diff['value']:.3g} cm sideways (creep {creep if creep is None else round(creep, 3)} cm/s down the slope) - not isolated, D-4 accepted "
                f"the creep only [R]", "inferred")
    if not creeping:
        return (f"PhysX PGS on a holding incline: the box slips {ctx.metric_value(diff, 'hold_drift_cm'):.3g} cm in the first steps while the "
                f"per-axis friction rows converge (PxSceneDesc.h:30-42) and then holds (creep {creep if creep is None else round(creep, 3)} cm/s; "
                f"BoxYaw 45, or UEParity put it to sleep) - the mechanism of M6 D-4 without its creep, not isolated [R]", "inferred")
    return (f"PhysX PGS creep on a holding incline (Progress M6 D-4, accepted as engine behaviour, algorithmic): the velocity gained in step 1 is "
            f"never removed by PGS friction (last 3 position iterations, per axis, PxSceneDesc.h:30-42) - creep "
            f"{creep:.3g} cm/s here (G5: 2.5 cm/s); TGS and the other engines hold")


TGS_CREEP_MAX_CMS = 0.2   # M9 close guard: TGS's creep on a mu >= 0.6 incline (M6 G5 0.065 cm/s, M9 0.073 cm/s), with margin


def _rule_physx_tgs_creep(diff, ctx) -> str | None:
    if diff["label"] != "physx.UE4Compat-TGS" or diff["scenario"] != "M4.InclineFriction" or _metric(diff) not in ("hold_drift_cm", "creep_cms"):
        return None
    # M9 close guard (reviewer r3): the signature of TGS's slow creep - a speed under 0.2 cm/s and a drift within the registered TGS value
    # (M4.hold_drift_cm, 0.3 cm, signed off with the M9 commit); a box that slides off or creeps faster is not this
    creep, drift = ctx.metric_value(diff, "creep_cms"), ctx.metric_value(diff, "hold_drift_cm")
    registered = max((o.value for o in TOLERANCES["M4.hold_drift_cm"].overrides if "physx.UE4Compat-TGS" in o.labels), default=math.nan)
    if not (finite(creep) and 0.0 <= creep <= TGS_CREEP_MAX_CMS and finite(drift) and drift <= registered):
        return None
    return (f"TGS creeps slowly on a holding incline ({creep:.3g} cm/s, drift {drift:.3g} cm within the registered TGS {registered:g} cm; M6 G5 "
            f"measured 0.065 cm/s, Progress M9 준비 F-2): its friction anchors drift a little each step [R]")


def _rule_physx_pgs_friction(diff, ctx):
    if diff["label"] not in PGS_PHYSX or diff["scenario"] != "M4.InclineFriction" or _metric(diff) not in ("d2s_err_pct", "lateral_cm"):
        return None
    # M9 fix round 1 (checker r1): data guards instead of a scene / metric match. The lead accepted D-4 for the creep only, so these
    # branches are inferred: the same per-axis PGS friction, not isolated for the slide or the sideways drift.
    tgs = ctx.same_engine_value(diff, "physx.UE4Compat-TGS")
    early = ctx.detail(diff, "err_frame30_pct")
    if _metric(diff) == "lateral_cm":
        # the drift belongs to the PGS rows: the same engine with TGS (same scene, profile, settings but the solver) slides straight
        if (tgs is not None and not tgs <= 0.5) or not diff["value"] <= 10.0:
            return None
        switch = (f"the same engine with TGS drifts {tgs:.2g} cm" if tgs is not None else
                  "no TGS run in this session to check the solver switch")
        return (f"PhysX PGS friction is solved per axis in the last position iterations (PxSceneDesc.h:30-42, the mechanism of M6 D-4), not along "
                f"the sliding direction: the box drifts {diff['value']:.2f} cm sideways while sliding; {switch}. "
                f"Not isolated beyond that solver switch [R] (D-4 accepted the creep, not this drift)", "inferred")
    # the slide runs ahead of the reference (extra speed while the contact forms) and stays within the per-engine value proposed for it
    pending = [o.value for o in TOLERANCES["M4.d2s_err_pct"].overrides if any(fnmatch.fnmatchcase(diff["label"], p) for p in o.labels)]
    if not (diff["value"] > 0.0 and pending and diff["value"] <= max(pending)) or (tgs is not None and abs(tgs) > TOLERANCES["M4.d2s_err_pct"].value):
        return None
    return (f"PhysX PGS friction (per-axis rows in the last position iterations, PxSceneDesc.h:30-42, the mechanism of M6 D-4): the box gains "
            f"extra speed while the contact forms - error {early if early is None else round(early, 2)}% at frame 30, {diff['value']:.2f}% at 2 s - "
            f"largest where the net acceleration is small (mu 0.55) and with the box's axes off the slope (BoxYaw 45); "
            + (f"TGS here {tgs:.3g}% (within 1%)" if tgs is not None else "no TGS run in this session to check the solver switch")
            + ". Not isolated beyond that solver switch [R]", "inferred")


BULLET_ROCKING_RATIO = 2.0          # M9 close guard: Bullet's sliding box turns at least this many times faster than both other engines'
BULLET_SLIDE_LATERAL_MAX_CM = 5.0   # ... and wanders sideways at most this much (M9: up to 0.69 cm)
BULLET_SLIDE_DEPTH_MAX_CM = 1.0     # ... with a contact depth of at most this much (M9: up to 0.27 cm)


def _rule_bullet_sliding_box(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or diff["scenario"] != "M4.InclineFriction" or _metric(diff) not in ("d2s_err_pct", "lateral_cm", "contact_penetration_cm"):
        return None
    # M9 close guard (reviewer r3): the rocking signature - Bullet's box turns (|w| max) at least twice as fast as the other main
    # engines' boxes on the same incline - and the direction or size the mechanism gives: the slide lags (a negative d(2 s) error), the
    # sideways drift and the contact depth stay small. A box that runs ahead, flies off sideways or sinks is not this.
    w = ctx.detail(diff, "angular_speed_max")
    others = [float((ctx.result_of_label(diff, label) or {}).get("details", {}).get("angular_speed_max", math.nan))
              for label in ("jolt.Default", "physx.UE4Compat") if label != diff["label"]]
    others = [o for o in others if finite(o)]
    if not (finite(w) and others and float(w) >= BULLET_ROCKING_RATIO * max(others) and float(w) > 0.0):
        return None
    metric, value = _metric(diff), float(diff["value"])
    if (metric == "d2s_err_pct" and not value < 0.0) or (metric == "lateral_cm" and not value <= BULLET_SLIDE_LATERAL_MAX_CM) or (
            metric == "contact_penetration_cm" and not value <= BULLET_SLIDE_DEPTH_MAX_CM):
        return None
    return (f"Bullet's sliding box rocks on its 4-point manifold (|w| up to {float(w):.3g} rad/s; the other engines "
            f"{ctx.detail_of_main(diff, 'angular_speed_max')}): sequential impulse with one friction direction per point (solver mode 0x104, no "
            f"SOLVER_USE_2_FRICTION_DIRECTIONS), 10 iterations, margin-rounded box (shape.box_margin 4 cm) - the slide lags the reference and "
            f"wanders sideways [R]")


def _box_edge_cm(run) -> float:
    """The longest edge of the first dynamic box of a run's scene_desc (M1: the 100 cm cube), NaN without one."""
    for body in (run.desc or {}).get("bodies", []):
        if body.get("motion") != "dynamic":
            continue
        for shape in body.get("shapes", []):
            if shape.get("type") == "box" and shape.get("half_extent"):
                return 2.0 * max(float(h) for h in shape["half_extent"])
    return math.nan


def _landing_guard(diff, ctx) -> float | None:
    """M9 close guard (reviewer r3) of the M1 b landing rules: the box ends at rest on a face (final tilt within the M1 tolerance) and
    within one edge length of the consensus - a tumble or a pivot ends there; a box left on its edge, or thrown further, is another
    outcome. Returns the box's edge length, or None when the guard fails."""
    tilt = ctx.metric_value(diff, "final_tilt_deg")
    edge = _box_edge_cm(ctx.run(diff))
    if not (finite(tilt) and tilt <= TOLERANCES["M1.final_tilt_deg"].value and finite(edge)):
        return None
    if _metric(diff) == "final_xy_cm" and not abs(float(diff["value"]) - float(diff["reference"])) <= edge:
        return None
    return edge


def _rule_bullet_tumble(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or diff["scenario"] != "M1.SingleBoxDrop" or _metric(diff) not in ("final_xy_cm", "final_tilt_deg"):
        return None
    edge = _landing_guard(diff, ctx)
    if edge is None:
        return None
    return (f"the tilted box lands on an edge and tumbles: where it ends depends on how the solver resolves the edge impact and the pivoting "
            f"contact (Bullet: sequential impulse, 10 iterations, one friction direction per point, a 4 cm margin-rounded box); the other "
            f"engines agree among themselves within a few cm, and this box ends on a face within one {edge:.3g} cm edge of them [R]")


def _rule_edge_landing(diff, ctx):
    # M9 fix round 0: with the pre-run 2 cm base (the raised 10 cm waits for sign-off) the sharp-box engines' landings differ from the
    # consensus too - the consensus being Jolt.Default's rounded-corner landing. M9 commit: the 10 cm is signed off, so this rule speaks
    # only beyond it; M9 close guard: the box ends on a face within one edge length of the consensus (_landing_guard).
    if diff["scenario"] != "M1.SingleBoxDrop" or ctx.run(diff).params.get("Case") != "b" or _metric(diff) != "final_xy_cm":
        return None
    if _landing_guard(diff, ctx) is None:
        return None
    obs = ctx.observations
    rows = obs[(obs["scene"] == diff["scene"]) & (obs["profile"] == diff["profile"]) & (obs["metric"] == diff["metric"]) & (~obs["probe"])]
    shown = ", ".join(f"{row['label']} {row['value']:.3g}" for row in sorted(rows.to_dict("records"), key=lambda r: r["value"]))
    return (f"the tilted box lands on an edge and tumbles; where it stops depends on how each solver resolves the edge impact and the pivoting "
            f"contact's friction, so the engines spread (cm: {shown}) - the consensus {diff['reference']:.3g} is Jolt.Default's rounded-corner "
            f"landing (rule jolt-convex-radius), the sharp boxes land a few cm further [R]", "inferred")


def _rule_sleep_unmapped(diff, ctx) -> str | None:
    if diff["profile"] != "UEParity":
        return None
    if _metric(diff) not in ("time_to_sleep_s", "sleep_speed_cms", "d2s_err_pct", "hold_drift_cm", "top_sway_max_cm", "top_z_final_cm", "mean_z_final_cm",
                             "toppled_fraction", "final_xy_cm", "rest_jitter_cm"):
        return None
    text = ctx.effective_text(diff, "sleep.criteria")
    if "not mapped" not in text:
        return None
    if _metric(diff) not in ("time_to_sleep_s", "sleep_speed_cms") and not ctx.slept_during(diff):
        return None
    return (f"UEParity asks for UE's sleep thresholds but {diff['backend']} still uses its own (effective sleep.criteria: \"{text[:150]}\"; "
            f"Profiles.md 5.1: Jolt / Bullet map them since M9; this backend still reports 'not mapped')" +
            ("" if _metric(diff) == "time_to_sleep_s" else
             f" - put to sleep at {diff['value']:.3g} cm/s, still moving" if _metric(diff) == "sleep_speed_cms" else
             f" - the body went to sleep at {ctx.detail(diff, 'sleep_time')} s and froze this observable"))


def _rule_ue4compat_tower(diff, ctx) -> str | None:
    if diff["label"] != "physx.UE4Compat" or diff["profile"] != "UEParity" or diff["scenario"] not in ("M2a.Tower", "M3.BoxPile") or _metric(diff) not in (
            "top_sway_max_cm", "time_to_sleep_s", "rest_jitter_cm", "rest_penetration_cm", "top_z_final_cm", "mean_z_final_cm"):
        return None
    stab = ctx.same_engine_value(diff, "physx.UE4Compat-Stab")
    if stab is None:
        return None
    if math.isfinite(stab) and math.isfinite(diff["reference"]):
        stab_within = abs(stab - diff["reference"]) <= diff["effective_tolerance"]
    else:
        stab_within = stab == diff["reference"]
    if not stab_within:
        return None
    return (f"UE4's PhysX settings: PGS with 8 position / 1 velocity iterations and stabilization off (UE4Compat) let the stack keep rocking and "
            f"sleep late or never; the same engine with stabilization (physx.UE4Compat-Stab: {stab:.4g}) matches the other engines "
            f"(01 probe direction; Progress M9 준비 F-5; stabilization acts only with sleep on, Checkpoints N-3)")


def _rule_sleep_engine(diff, ctx):
    # M9 fix round 1 (checker r1): the UE4 rule on PhysX only (the text is PhysX's), and only where the data obeys that rule: a body put
    # to sleep below the speed of the configured kinetic-energy threshold (1/2 v^2 < threshold per unit mass), a time to sleep no shorter
    # than the wake counter. Jolt's / Bullet's UE mappings would need their own rule (none differs in M9).
    if _metric(diff) not in ("time_to_sleep_s", "sleep_speed_cms") or diff["profile"] != "UEParity" or diff["backend"] != "PhysX":
        return None
    text = ctx.effective_text(diff, "sleep.criteria")
    threshold = ctx.run(diff).effective_value("physx.sleep_threshold_max")
    match = re.search(r"wake counter \(([0-9.]+) s", text)
    counter = float(match.group(1)) if match else None
    stab = "stabilization on" if "Stab" in diff["label"] else ""
    if _metric(diff) == "sleep_speed_cms":
        if not isinstance(threshold, (int, float)) or not diff["value"] <= math.sqrt(2.0 * float(threshold)) + 1e-6:
            return None
        check = (f"put to sleep at {diff['value']:.3g} cm/s, under the {math.sqrt(2.0 * float(threshold)):.3g} cm/s of the configured threshold "
                 f"({float(threshold):.4g} cm^2/s^2 = v^2 / 2)")
    else:
        value = float(diff["value"])
        if counter is None or not (math.isinf(value) or value >= counter - ctx.run(diff).dt):
            return None
        check = ("never asleep in the run" if math.isinf(value) else
                 f"asleep at {value:.3g} s, after the {counter:.3g} s wake counter below the threshold that UE4's PhysX needs")
    return (f"UE4's sleep rule on PhysX (PhysX threshold x SleepFamily multiplier, wake counter {counter} s; effective sleep.criteria: "
            f"\"{text[:120]}\"){'; ' + stab if stab else ''} is the UEParity design (Profiles.md 5); the other engines sleep by their UE "
            f"mappings (Jolt: its own time-before-sleep rule with the mapped UE threshold, Bullet: UE's sleep counter, Profiles.md 5.1). Here {check}", "verified")


BULLET_STACK_PROBES = ("bullet.R0", "bullet.Iter50", "bullet.R0Iter50")   # Checkpoints N-1: restitution 0, 50 iterations, both


def _rule_bullet_stack(diff, ctx):
    if diff["backend"] != "Bullet" or diff["scenario"] not in ("M3.BoxPile", "M2a.Tower") or _metric(diff) not in (
            "mean_z_final_cm", "top_z_final_cm", "toppled_fraction", "top_sway_max_cm", "rest_penetration_cm", "time_to_sleep_s"):
        return None
    # M9 fix round 2 (checker r2): the rule claims that the stack did not hold, so where the observable tells (the heights, the toppled
    # fraction) it must say so - lower than the consensus, or more toppled - else None. "The probes isolate it" (verified) needs their
    # outcome: both changes together (bullet.R0Iter50) hold the stack like the consensus while each alone (bullet.R0, bullet.Iter50) does
    # not; any other outcome, or no probe run, is inferred and says what the probes gave.
    metric = _metric(diff)
    value, reference, tol = float(diff["value"]), float(diff["reference"]), float(diff["effective_tolerance"])
    guarded = metric in ("mean_z_final_cm", "top_z_final_cm", "toppled_fraction")
    if guarded:
        collapsed = value > reference + tol if metric == "toppled_fraction" else value < reference - tol
        if not collapsed:
            return None
    probes = {label: probe for label, probe in ctx.probe_outcomes(diff) if label in BULLET_STACK_PROBES}
    head = "Bullet's sequential impulse with 10 iterations and the default restitution 0.3 does not hold this stack (Checkpoints N-1)"
    if not probes:
        return head + " - no probe run in this session [R]", "inferred"
    holds = {label: _stack_holds_like_consensus(ctx, diff, probe) for label, probe in probes.items()}
    shown = "; ".join(f"{label} {probes[label]:.4g} ({'holds' if holds[label] else 'does not hold'})" for label in BULLET_STACK_PROBES if label in probes)
    text = f"{head}; probes ({metric}, consensus {reference:.4g}): {shown}"
    if guarded and holds.get("bullet.R0Iter50") and holds.get("bullet.R0") is False and holds.get("bullet.Iter50") is False:
        return text + ". Only both changes together restore the consensus: the probes isolate the mechanism", "verified"
    if not guarded:
        return text + ". This observable does not tell a held stack from a fallen one, so the probes are shown, not used [R]", "inferred"
    return text + ". The probes do not separate the two causes on this observable [R]", "inferred"


def _stack_holds_like_consensus(ctx, diff, value: float) -> bool:
    """A probe's value of the difference's metric holds the stack like the consensus: within the effective tolerance of it, or - for the
    heights - between Jolt's slop-lowered consensus and the overlap-free height (the band of rule stack-consensus-slop)."""
    reference, tol = float(diff["reference"]), float(diff["effective_tolerance"])
    if not finite(value):
        return False
    if abs(value - reference) <= tol:
        return True
    return _within_slop_band(ctx, diff, value)


def _stack_values_text(ctx, diff) -> str:
    return ", ".join(f"{label} {value:.4g}" for label, value in ctx.main_values(diff))


def _slop_band(ctx, diff) -> tuple[float, float, float, float] | None:
    """(Jolt's slop, the overlap-free height, the contacts below, slop x contacts) when the difference's consensus is Jolt.Default's
    stack height (top_z_final_cm / mean_z_final_cm), else None. The consensus value is Jolt's when, of the three main series' values,
    the median is jolt.Default's (another main series may agree with it within the tolerance - PhysX UE4Compat's standing pile - but the
    reference itself is Jolt's slop-lowered height)."""
    if _metric(diff) not in ("top_z_final_cm", "mean_z_final_cm"):
        return None
    jolt = [value for label, value in ctx.main_values(diff) if label == "jolt.Default"]
    reference_value = float(diff["reference"])
    if not jolt or not finite(jolt[0]) or abs(jolt[0] - reference_value) > 1e-6 * max(1.0, abs(reference_value)):
        return None
    jolt_run = ctx.index.get(("jolt.Default", diff["profile"], diff["scene"]))
    slop = None if jolt_run is None else jolt_run.effective_value("jolt.penetration_slop")
    details = ctx.results[diff["run_key"]]["details"]
    top = _metric(diff) == "top_z_final_cm"
    ideal = details.get("ideal_top_z_cm" if top else "ideal_mean_z_cm")
    layers = details.get("layers" if top else "mean_layers_below")
    if not isinstance(slop, (int, float)) or not finite(ideal) or not finite(layers) or slop <= 0.0:
        return None
    return float(slop), float(ideal), float(layers), float(slop) * float(layers)


def _within_slop_band(ctx, diff, value: float) -> bool:
    """A stack height between Jolt's slop-lowered consensus and the overlap-free height, within the slop the consensus carries."""
    band = _slop_band(ctx, diff)
    if band is None or not finite(value):
        return False
    _, ideal, _, bound = band
    reference, tol = float(diff["reference"]), float(diff["effective_tolerance"])
    return reference < value <= ideal + tol and value - reference <= bound + tol


def _rule_stack_consensus_slop(diff, ctx):
    # M9 fix round 1: a standing stack measured against a consensus that is Jolt's stack. Jolt leaves up to its penetration slop per
    # contact unresolved (rule jolt-penetration-slop), so its standing stack is up to (contacts below) x slop lower than an
    # overlap-free one; when another main series is an outlier (Bullet's collapsed pile, N-1) the median of three is Jolt's value.
    if diff["scenario"] not in ("M3.BoxPile", "M2a.Tower") or _metric(diff) not in ("top_z_final_cm", "mean_z_final_cm"):
        return None
    band = _slop_band(ctx, diff)
    if band is None:
        return None
    slop, ideal, layers, bound = band
    value, reference = float(diff["value"]), float(diff["reference"])
    # this stack stands between the consensus (Jolt with its slop) and the overlap-free height, within the slop the consensus carries
    if not _within_slop_band(ctx, diff, value):
        return None
    top = _metric(diff) == "top_z_final_cm"
    return (f"the consensus is Jolt.Default's standing stack (main series: {_stack_values_text(ctx, diff)}; the median of three is Jolt's value when "
            f"another main series is an outlier, e.g. Bullet's collapsed pile, Checkpoints N-1). Jolt leaves up to its penetration slop "
            f"{float(slop):g} cm per contact unresolved (effective jolt.penetration_slop, rule jolt-penetration-slop): "
            f"{'the top box rests on' if top else 'a body rests on (mean)'} {float(layers):.3g} contacts, so Jolt's stack stands up to "
            f"{bound:.3g} cm lower than an overlap-free one ({float(ideal):.4g} cm). This stack stands at {value:.4g} cm, between the two: "
            f"the difference is Jolt's declared slop, not this engine's solver")


def _probe_far(ctx, diff, label: str) -> dict[str, Any] | None:
    result = ctx.result_of_label(diff, label)
    if result is None:
        return None
    details, metrics = result["details"], result["metrics"]
    return {"label": label, "far": details.get("far_bodies"), "mean_z": metrics.get("mean_z_final_cm"), "toppled": metrics.get("toppled_fraction"),
            "speed": details.get("speed_max_after_impact_cms")}


def _probe_text(probe: dict[str, Any] | None) -> str:
    if probe is None:
        return "not run"
    return (f"{probe['far']} bodies off their slot, mean z {probe['mean_z']:.4g} cm, toppled {probe['toppled']:.3g}, "
            f"largest speed after the impact window {probe['speed']:.0f} cm/s")


def _rule_physx_tgs_pile_ejection(diff, ctx):
    # M9 fix round 1 (checker r1): PhysX TGS throws boxes out of the headline pile. A dedicated rule with the ejection signature and
    # the probes of the behaviour matrix (physx.TGS-R0: material restitution 0; physx.TGS-Pos32: 32 position iterations).
    if diff["label"] != "physx.UE4Compat-TGS" or diff["scenario"] != "M3.BoxPile" or _metric(diff) not in (
            "mean_z_final_cm", "top_z_final_cm", "toppled_fraction", "rest_penetration_cm", "rest_jitter_cm"):
        return None
    details = ctx.results[diff["run_key"]]["details"]
    far, speed = details.get("far_bodies"), details.get("speed_max_after_impact_cms")
    drift = ctx.metric_value(diff, "drift_xy_max_cm")
    # the ejection signature: boxes far off their slot, one thrown more than two box sizes, speeds after the impact window that a
    # pile at rest or settling never has (the fall itself ends below 10 m/s inside the impact window)
    if not (isinstance(far, int) and far >= 5 and finite(drift) and drift > 200.0 and finite(speed) and speed > 300.0):
        return None
    r0, pos = _probe_far(ctx, diff, "physx.TGS-R0"), _probe_far(ctx, diff, "physx.TGS-Pos32")
    pgs = _probe_far(ctx, diff, "physx.UE4Compat")
    holds_r0 = r0 is not None and r0["far"] is not None and r0["far"] <= max(1, far // 10)
    keeps_pos = pos is not None and pos["far"] is not None and pos["far"] >= far // 2
    text = (f"PhysX TGS throws boxes out of the pile inside the 600-frame window: {far} of {details.get('bodies')} bodies end more than "
            f"50 cm from their slot (first at recorded frame {details.get('first_far_frame')}), one {drift:.0f} cm away, speeds up to "
            f"{speed:.0f} cm/s after the impact window (PGS physx.UE4Compat: {_probe_text(pgs)}). Probes of this session: material restitution 0 "
            f"(physx.TGS-R0): {_probe_text(r0)}; 32 position iterations (physx.TGS-Pos32): {_probe_text(pos)}. ")
    if holds_r0 and keeps_pos:
        return (text + "The ejection needs the restitution 0.3 of the pile's impacts and is not an iteration budget limit: TGS's handling of "
                "restitution in a pile of simultaneous impacts (the mechanism inside the solver is not isolated further). M11: on this scene "
                "TGS times another workload than the other rows (Checkpoints N-4)", "verified")
    return text + "Without both probes (or with another outcome) the mechanism is not isolated [R]", "inferred"


def _rule_physx_sdkdefault_pile(diff, ctx):
    # M9 fix round 1 (checker r1): PhysX SdkDefault's pile spreads and, after frame 600, collapses (Normalized). Probe
    # physx.SdkDefault-Pos8: the same variant with UE4's 8 position iterations instead of PhysX's own default 4.
    if diff["label"] != "physx.SdkDefault" or diff["scenario"] != "M3.BoxPile" or _metric(diff) not in (
            "mean_z_final_cm", "top_z_final_cm", "toppled_fraction", "rest_penetration_cm", "rest_jitter_cm"):
        return None
    details = ctx.results[diff["run_key"]]["details"]
    far = details.get("far_bodies")
    toppled = ctx.metric_value(diff, "toppled_fraction")
    if not (isinstance(far, int) and far >= 5 or (toppled is not None and toppled > 0.05)):
        return None
    probe = _probe_far(ctx, diff, "physx.SdkDefault-Pos8")
    iters = ctx.effective_text(diff, "solver.iterations").split(" (")[0]
    text = (f"PhysX SdkDefault (PhysX 5.11's own defaults: {str(iters)[:80]}) does not hold the 1k pile: {far} bodies end off their slot "
            f"(first at recorded frame {details.get('first_far_frame')}), toppled {toppled if toppled is None else round(toppled, 3)}; probe with UE4's "
            f"8 position iterations and every other SdkDefault setting (physx.SdkDefault-Pos8): {_probe_text(probe)}. ")
    if probe is not None and probe["far"] is not None and probe["far"] <= max(1, far // 10) and (probe["toppled"] or 0.0) <= 0.05:
        return text + "The variant's declared iteration count (4 position / 1 velocity) is the cause (Profiles.md 4)", "verified"
    return text + "Without the probe (or with another outcome) the cause is not isolated [R]", "inferred"


def _rule_variant_config(diff, ctx) -> str | None:
    if diff["label"] in ("jolt.Default", "bullet.Default", "physx.UE4Compat") or diff.get("probe"):
        return None
    # M9 fix round 1: in a chaotic cell without Perturb replicas (M3.BoxPile) the engines' own sensitivity is not measured, so "differs
    # only by its variant settings" cannot be told from a chaotic outcome: such a difference needs its own rule (or stays unexplained).
    if diff.get("chaotic") and ctx.replica_count(diff["label"], diff["profile"], diff["scenario_group"]) == 0:
        return None
    main = ctx.main_of(diff)
    if main is None:
        return None
    main_diff = ctx.difference_of(diff, main)
    if main_diff is not None:
        return None   # the engine's main series differs too: the engine's rule explains it, not the variant
    keys = ctx.effective_differences(diff, main)
    if not keys:
        return None
    return (f"{diff['label']} differs from {main} (which agrees with the consensus) only by its variant settings: "
            + "; ".join(f"{k} {a} vs {b}" for k, a, b in keys[:4]) + " (Profiles.md 4)")


def _rule_chaotic(diff, ctx) -> str | None:
    # M9 fix round 1 (checker r1): the spread is a measurement only where the cell has Perturb replicas. Without them (M3.BoxPile) the
    # spread 0 is a placeholder and the engines' sensitivity is unknown: no claim (the difference needs its own rule or stays
    # "unexplained"). A variant series does not have its main series' settings: say which differ instead of "same settings".
    if not diff.get("chaotic"):
        return None
    # M9 close guard (reviewer r3): an aggregate outcome against the engines' consensus only - an analytic bound (energy growth, a hard
    # joint's stretch) is not a chaotic outcome and a breach of it is not explained by the engines' sensitivity
    if diff.get("ref_kind", "consensus") != "consensus":
        return None
    own = ctx.replica_count(diff["label"], diff["profile"], diff["scenario_group"])
    reference = [label for label, _ in ctx.main_values(diff) if label != diff["label"]]
    ref_replicas = min((ctx.replica_count(label, diff["profile"], diff["scenario_group"]) for label in reference), default=0)
    if own == 0 or ref_replicas == 0:
        return None
    iters = ctx.effective(diff, "solver.iterations")
    solver = ctx.effective_text(diff, "solver.type")
    main = ctx.main_of(diff)
    if diff["label"] == main:
        settings = "each engine's main settings"
    else:
        keys = ctx.effective_differences(diff, main) if main else []
        settings = (f"a variant: its settings differ from {main} ({'; '.join(f'{k} {a} vs {b}' for k, a, b in keys[:3])}) and {main} is beyond "
                    f"the consensus too" if keys else f"a variant with {main}'s effective settings of interest")
    return (f"chaotic stack (02 13): same input (one desc_sha256), {settings}; a different aggregate outcome beyond both series' own 1e-4 cm "
            f"spread ({diff.get('spread_self'):.3g} / {diff.get('spread_ref'):.3g}, {own} / {ref_replicas}+ Perturb replicas); the contact solvers "
            f"differ ({solver[:90]}, {iters} iterations) [R]")


def _same_threshold(a: float | None, b: float | None) -> bool:
    return a is not None and b is not None and abs(a - b) <= 1e-3 * max(1.0, abs(b))


def _rule_restitution_threshold(diff, ctx) -> str | None:
    # M9 fix round 0: by-design only when this run counts above another threshold than every engine that set the consensus (the
    # UEParity mapping, DL35 / Q11). With the same threshold the difference is the engines' restitution (the two rules below).
    if _metric(diff) not in ("bounces", "judged_impacts", "last_impact_frame"):
        return None
    own = ctx.run(diff).restitution_threshold
    consensus = ctx.consensus_runs(diff)
    thresholds = [run.restitution_threshold for run in consensus]
    if own is None or not consensus or any(t is None or _same_threshold(own, t) for t in thresholds):
        return None
    shown = ", ".join(f"{run.label} {run.restitution_threshold:.4g}" for run in consensus)
    return (f"the threshold differs by design: this engine counts bounces above its effective contact.restitution_threshold {own:.4g} cm/s, the "
            f"engines that set the consensus above another ({shown} cm/s) - the UEParity mapping gives Jolt / Bullet Chaos' 1000 dt = 16.7 and "
            f"PhysX UE4's project 200 cm/s (DL35 / Q11, Profiles.md 5)")


def _m7_body_restitution(run, metric: str) -> float | None:
    """The material restitution of the M7 body of a per-body metric "b1.box.bounces"."""
    try:
        index = int(str(metric).split(".")[0][1:])
        shape = run.desc["bodies"][index]["shapes"][0]
        return round(float(run.desc["materials"][int(shape.get("material", 0))].get("restitution", 0.0)), 6)
    except (KeyError, IndexError, ValueError, TypeError):
        return None


def _sequence_text(sequence: list[dict[str, Any]], limit: int = 5) -> str:
    return ", ".join(f"{i['approach']:.4g}->{i['rebound']:.3g}{'*' if i['counted'] else ''}" for i in sequence[:limit]) or "none"


RESTITUTION_MODEL_TEXT = {
    "Jolt": "Jolt applies -e to the velocity before the contact step at every speed (registered model 0; M5: every judged impact within 2e-7)",
    "PhysX": "PhysX applies -e to the pre-solve velocity that already holds the step's gravity, e (1 + g dt / |v|), growing as the impacts slow "
             "(registered model 1, rule physx-restitution-presolve, M6 D-3)",
    "Bullet": "Bullet's restitution target follows the contact state at the step start: a speculative contact (gap > 0) loses gap / dt - an approach "
              "killed in the step before the touch -, a shallow penetration gains erp2 depth / dt (registered model 2; rule bullet-restitution-contact-state, "
              "verified on M5)",
}


def _restitution_low_speed(diff, ctx):
    """(the consensus run with the same threshold, this run's bounce sequence, its) for an M7 bounce count, or None."""
    if _metric(diff) != "bounces":
        return None
    run = ctx.run(diff)
    same = [other for other in ctx.consensus_runs(diff) if _same_threshold(run.restitution_threshold, other.restitution_threshold)]
    if not same:
        return None
    return same[0], ctx.bounce_sequence_of(run, diff["metric"]), ctx.bounce_sequence_of(same[0], diff["metric"])


def _rule_jolt_restitution_low_speed(diff, ctx):
    # The Jolt rule of the M9 review: UEParity Jolt counts one bounce more than Bullet at the same 16.7 cm/s threshold.
    if diff["backend"] != "Jolt":
        return None
    found = _restitution_low_speed(diff, ctx)
    if found is None:
        return None
    other, sequence, other_sequence = found
    e0 = _m7_body_restitution(ctx.run(diff), diff["metric"])
    above = [i for i in sequence if i["above"]]
    if e0 is None or not above:
        return None
    worst = max(abs(i["e"] - e0) for i in above)
    text = (f"same threshold as the consensus engine {other.label} ({ctx.run(diff).restitution_threshold:.4g} cm/s): Jolt keeps e = {e0:g} on every "
            f"impact above it (worst |e_k - e| {worst:.1e}; approach->rebound cm/s, * = counted: {_sequence_text(sequence)}), so its low-speed bounces "
            f"stay above the threshold one bounce longer than {other.label}'s ({_sequence_text(other_sequence)}). "
            + RESTITUTION_MODEL_TEXT["Jolt"] + "; " + RESTITUTION_MODEL_TEXT.get(other.backend, other.backend))
    return text, ("verified" if worst <= 1e-3 else "inferred")


def _rule_restitution_low_speed(diff, ctx):
    found = _restitution_low_speed(diff, ctx)
    if found is None:
        return None
    other, sequence, other_sequence = found
    run = ctx.run(diff)
    models = "; ".join(RESTITUTION_MODEL_TEXT.get(backend, backend) for backend in dict.fromkeys((run.backend, other.backend)))
    return ((f"same threshold as the consensus engine {other.label} ({run.restitution_threshold:.4g} cm/s), another count: approach->rebound cm/s "
             f"(* = counted) here {_sequence_text(sequence)}, {other.label} {_sequence_text(other_sequence)}. The count follows each engine's "
             f"restitution at low speed: {models}"), "inferred")


# ---- M9 joints (first real joint data: Progress M9) ------------------------------------------------------------------------

def _m6a(diff) -> bool:
    return diff["scenario"] == "M6a.Pendulum"


def _value_in_profile(ctx, diff, profile: str) -> float | None:
    run = ctx.index.get((diff["label"], profile, diff["scene"]))
    if run is None or id(run) not in ctx.results:
        return None
    value = ctx.results[id(run)]["metrics"].get(diff["metric"])
    return None if value is None else float(value)


J1_HARD_JOINT_STRETCH_CM = 0.5   # tolerances.json J1.ball_stretch_cm / hinge_stretch_cm default: a hard joint's frames stay this close


def _rule_bullet_p2p(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or not _m6a(diff) or ctx.run(diff).params.get("Joint") != "Ball":
        return None
    # M9 fix round 2 (checker r2): data guards. The p2p rows let the anchor stretch beyond the joint gate's hard-joint bound (J1, 0.5 cm)
    # and beyond Bullet's hinge at the same swing (its frame-offset rows hold the anchor); the bob then swings on a longer effective rod,
    # so a period difference must be a longer period. A Bullet ball difference without that signature is not this mechanism (None).
    stretch = ctx.metric_value(diff, "stretch_max_cm")
    hinge_run = ctx.index.get((diff["label"], diff["profile"], str(diff["scene"]).replace("Joint=Ball", "Joint=Hinge")))
    hinge = None
    if hinge_run is not None and id(hinge_run) in ctx.results:
        hinge = ctx.results[id(hinge_run)]["metrics"].get("stretch_max_cm")
    if not (finite(stretch) and stretch > J1_HARD_JOINT_STRETCH_CM and (not finite(hinge) or stretch > float(hinge))):
        return None
    if _metric(diff) == "period_err_pct" and not diff["value"] > 0.0:
        return None
    hinge_text = f"Bullet's hinge at this swing {float(hinge):.3g} cm" if finite(hinge) else "no hinge run of this swing in the session"
    return (f"Bullet's btPoint2PointConstraint is three scalar rows along the world axes; for this bob (m L^2 / I = 1000) each row's "
            f"angular term dominates, so Bullet's 10 sequential-impulse iterations mostly spin the bob about its COM instead of holding the "
            f"anchor: the joint stretches {stretch:.3g} cm here (beyond the hard-joint bound {J1_HARD_JOINT_STRETCH_CM:g} cm; {hinge_text}) and "
            f"the bob swings on a longer rod (a longer period, a faster amplitude loss, the rod length follows). 50 iterations: 0.025 cm at 5 deg; "
            f"the hinge's frame-offset rows and btGeneric6DofSpring2Constraint keep 0.005 cm (Progress M9 Bullet J-1; "
            f"PhysicsTest.BulletBackend.BallJointRowCoupling; the joint gate J1 registers 3 cm for it, tolerances.json J1.ball_stretch_cm, post-run, "
            f"signed off by the lead 2026-10-22; 03 M9 judges the 5 deg period on the hinge scene, this ball variant is reported, not gated)")


def _rule_pendulum_ue_damping(diff, ctx) -> str | None:
    if not _m6a(diff) or diff["profile"] != "UEParity" or _metric(diff) != "amplitude_decay_pct_per_period":
        return None
    damping = ctx.detail(diff, "damping_decay_pct_per_period")
    own = _value_in_profile(ctx, diff, "Normalized")
    if not finite(damping) or not finite(own) or damping <= 0.0 or abs(own) > diff["tolerance"]:
        return None   # without damping, or the engine's own decay is a difference already (pendulum-velocity-projection)
    if abs(diff["value"] - (own + damping)) > 0.2 * damping + 0.05:
        return None
    return (f"UE's linear damping {ctx.detail(diff, 'linear_damping')} /s (UEParity gives every engine UE's body default; effective "
            f"body.damping) takes {damping:.3g}%/period off the amplitude (v x (1 - c dt) per step on the swing's kinetic energy): "
            f"measured {diff['value']:.3g} = the engine's own {own:.3g} (Normalized, no damping) + {damping:.3g} predicted")


def _rule_pendulum_velocity_projection(diff, ctx) -> str | None:
    if diff["label"] not in VELOCITY_JOINT_SOLVERS or not _m6a(diff) or _metric(diff) not in ("amplitude_decay_pct_per_period", "period_err_pct"):
        return None
    projection = ctx.detail(diff, "projection_decay_pct_per_period")
    damping = ctx.detail(diff, "damping_decay_pct_per_period") or 0.0
    decay = ctx.metric_value(diff, "amplitude_decay_pct_per_period")
    if not finite(projection) or not finite(decay):
        return None
    own = decay - damping
    if not (0.4 * projection <= own <= 1.3 * projection):
        return None
    angle = ctx.detail(diff, "amplitude_deg")
    text = (f"a velocity-level joint solve puts the bob's velocity back on the tangent every step and drops the radial part the free step "
            f"gained (v^2 dt / L): a kinetic energy loss of (w dt)^2 per step, (3/4) pi^2 a0^2 dt / T = {projection:.3g}%/period of amplitude "
            f"for a small swing - measured {own:.3g}%/period without damping (the small-swing formula overestimates a 90 deg swing). Jolt "
            f"and the PhysX PGS variants agree to within 2%; PhysX TGS (sub-stepped) does not lose it (Progress M9 Jolt J-1, PhysX PX-2)")
    if _metric(diff) == "period_err_pct":
        if not (finite(angle) and angle >= 30.0 and diff["value"] < 0.0):
            return None
        return (f"the swing loses amplitude ({decay:.3g}%/period) and a smaller swing is faster: the mean period over the run is shorter "
                f"than the reference at the initial {angle:g} deg amplitude. Cause: " + text)
    return text


def _rule_physx_tgs_pendulum(diff, ctx) -> str | None:
    if diff["label"] != "physx.UE4Compat-TGS" or not _m6a(diff) or _metric(diff) not in ("amplitude_decay_pct_per_period", "period_err_pct"):
        return None
    decay = ctx.metric_value(diff, "amplitude_decay_pct_per_period")
    damping = ctx.detail(diff, "damping_decay_pct_per_period") or 0.0
    angle = ctx.detail(diff, "amplitude_deg")
    # M9 close guard (reviewer r3): the signature the text states - a large swing (30 deg and more) whose amplitude grows once UE's
    # damping is taken out (own decay <= 0) and, for the period, a longer one. A TGS swing that loses amplitude or runs fast is not this.
    if not (finite(decay) and finite(angle) and angle >= 30.0 and decay - float(damping) <= 0.0):
        return None
    if _metric(diff) == "period_err_pct" and not diff["value"] > 0.0:
        return None
    return (f"TGS keeps the swing's energy (no velocity-projection loss: {decay:.3g}%/period of which {float(damping):.3g} is UE damping in "
            f"UEParity) and at this {float(angle):g} deg swing gains amplitude, with a longer period; its anchor lags the bob's rotation (stretch up "
            f"to about 1 cm while the COM stays on its circle within 0.1 cm). Not isolated [R] (Progress M9 PX-2)")


def _rule_bullet_hinge_large_swing(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or not _m6a(diff) or ctx.run(diff).params.get("Joint") != "Hinge" or _metric(diff) not in (
            "amplitude_decay_pct_per_period", "period_err_pct"):
        return None
    # M9 fix round 1: the mechanism is a large swing's (the anchor drift grows with (w dt)^2): at 5 deg the hinge meets 03's period
    # criterion, so a 5 deg difference is not explained by this rule
    angle = ctx.detail(diff, "amplitude_deg")
    if not (finite(angle) and angle >= 30.0):
        return None
    if diff["profile"] == "UEParity" and _metric(diff) == "amplitude_decay_pct_per_period":
        own = _value_in_profile(ctx, diff, "Normalized")
        damping = ctx.detail(diff, "damping_decay_pct_per_period")
        if finite(own) and finite(damping) and abs(diff["value"] - (own + damping)) <= 0.2 * damping + 0.05:
            return (f"Bullet's hinge at a large swing: {own:.3g}%/period of its own (Normalized) plus UE damping's {damping:.3g} predicted - "
                    f"the Baumgarte-only joint correction (ERP 0.2, no position iterations) feeds the stretch back as velocity [R] "
                    f"(Progress M9 Bullet J-3)")
    return ("Bullet's hinge at a large swing: the velocity rows hold, the anchor drifts outward by about 2 (w dt)^2 L per step and "
            "Baumgarte (ERP 0.2, no position iterations) feeds it back as velocity, which changes the swing's energy and period [R] "
            "(Progress M9 Bullet J-3; the 5 deg swing meets 03's period criterion)")


def _rule_joint_rotation_linearization(diff, ctx) -> str | None:
    if not _m6a(diff) or _metric(diff) not in ("stretch_max_cm", "rod_length_err_max_cm"):
        return None
    bound = ctx.detail(diff, "linearization_stretch_cm")
    if not finite(bound) or not diff["value"] <= 1.5 * bound + 0.01:
        return None
    text = (f"integrating the bob's rotation moves the joint anchor about (w dt)^2 L / 2 per step off the circle; the engine's position "
            f"correction removes part of it: Baumgarte only (Bullet, ERP 0.2) settles near 2 (w_max dt)^2 L = {bound:.3g} cm (measured 1.14 "
            f"against 1.01 cm in Progress M9 Bullet J-3, independent of the iteration count), Jolt's 2 position steps and PhysX's position "
            f"iterations leave less; here {diff['value']:.3g} cm at w_max {ctx.detail(diff, 'omega_max')} rad/s")
    # M9 fix round 0: verified for Bullet only (J-3 measured the bound and its independence of the iteration count); on the other
    # engines the stretch merely lies under the bound - the mechanism is not isolated there (PhysX TGS: PX-2b, inferred).
    return text, ("verified" if diff["backend"] == "Bullet" else "inferred")


def _chain(diff) -> bool:
    return diff["scenario"] in CHAIN_SCENES


def _rule_physx_pgs_heavy_chain(diff, ctx) -> str | None:
    if diff["label"] not in PGS_PHYSX or diff["scenario"] != "M6c.HeavyChain":
        return None
    # M9 fix round 1: the data guard of the mechanism PX-1 describes - the joints tear apart (stretch beyond one link) and the energy
    # grows far above the release energy. A PGS run that holds the chain is not explained by this rule.
    stretch, growth = ctx.metric_value(diff, "stretch_max_cm"), ctx.metric_value(diff, "energy_growth_pct")
    link = ctx.detail(diff, "link_length_cm")
    if not (finite(stretch) and finite(link) and stretch > link and finite(growth) and growth > 100.0):
        return None
    return (f"PhysX PGS cannot hold the 100:1 chain: the joints tear apart (stretch {stretch:.4g} cm here, beyond one {link:.3g} cm link) and the "
            f"energy grows to {growth:.4g}% of m g span - with collisions off, either actor order, the Native recipe, 32 position iterations and in "
            "both profiles (Progress M9 PhysX PX-1, isolated by the PhysX implementer). UE4 leaned on joint projection, which PhysX 5.11 no "
            "longer has (joint.projection 0, CHANGELOG.md:1703): the missing projection is by-design, the divergence itself is PGS's "
            "mass-ratio limit")


def _rule_physx_tgs_heavy_chain(diff, ctx) -> str | None:
    if diff["label"] != "physx.UE4Compat-TGS" or diff["scenario"] != "M6c.HeavyChain":
        return None
    # M9 fix round 0: the energy of every replica, not only the base run's (the first text said "some replicas diverge"; all rise)
    growth = sorted((run.perturb, ctx.results[id(run)]["metrics"].get("energy_growth_pct")) for run in ctx.runs
                    if run.label == diff["label"] and run.profile == diff["profile"] and run.scenario == diff["scenario"] and id(run) in ctx.results)
    # M9 close guard (reviewer r3): the signature the text states - every run of this series rises above the release energy (beyond the
    # energy bound); a TGS chain that keeps its energy in some run is not this (a stretch difference there needs another rule)
    limit = TOLERANCES["M6c.energy_growth_pct"].value
    if not growth or not all(finite(g) and float(g) > limit for _, g in growth):
        return None
    shown = ", ".join(f"{p}: {g:+.3g}" for p, g in growth if g is not None)
    return ("TGS holds the 100:1 chain's joints better than PGS in some runs (base run about 25 cm) but its energy is not bounded: every run "
            f"rises above the release energy (Perturb: % of m g span {shown}; the runs above 10x tear apart to 2+ m) - a sub-stepped solver "
            "at this mass ratio without projection [R] (Progress M9: the PhysX implementer saw the base run only)")


def _rule_bullet_chain(diff, ctx) -> str | None:
    if diff["backend"] != "Bullet" or not _chain(diff):
        return None
    if diff["scenario"] == "M6c.HeavyChain":
        # M9 fix round 1: guarded by the mechanism's signature (the joints came apart: stretch beyond one link) and the energy stated
        # in the analysis unit (% of m g span) per run, not as a static text; the UEParity energy rise is inferred (one probe run each).
        stretch, link = ctx.metric_value(diff, "stretch_max_cm"), ctx.detail(diff, "link_length_cm")
        if not (finite(stretch) and finite(link) and stretch > link):
            return None
        growth = sorted((run.perturb, ctx.results[id(run)]["metrics"].get("energy_growth_pct")) for run in ctx.runs
                        if run.label == diff["label"] and run.profile == diff["profile"] and run.scenario == diff["scenario"] and id(run) in ctx.results)
        shown = ", ".join(f"{p}: {g:+.3g}" for p, g in growth if g is not None)
        text = (f"Bullet cannot hold the 100:1 chain's end: the joints come apart (stretch {stretch:.4g} cm, beyond one {link:.3g} cm link) - the "
                f"light links spin past Bullet's pi/4-per-step rotation limit (DL16; thousands of body-steps, M6b: 3) and the sequential impulse rows "
                f"cannot pass the end's momentum through them, independent of the iteration count (50 / 200 iterations: 9-12 m; Progress M9 Bullet "
                f"J-2). Energy, % of m g span per Perturb run: {shown}")
        if _metric(diff) == "energy_growth_pct":
            return (text + ". The rise is not isolated: with the angular velocity clamp or the damping removed the base run did not rise "
                    "(integration probe, one run each, chaotic) [R]", "inferred")
        return text + " (a separated chain falls freely: its energy check is trivial)", "verified"
    # M9 close guard (reviewer r3), M6b: the chain held (stretch below one link), kept its energy (within the bound) and Bullet's stretch is
    # the largest of the main engines' - what the text says; anything else is not this rule's
    if not _chain_held(diff, ctx):
        return None
    stretch = ctx.metric_value(diff, "stretch_max_cm")
    others = [v for label, v in _main_metric_values(ctx, diff, "stretch_max_cm") if label != diff["label"]]
    if not (others and float(stretch) >= max(others)):
        return None
    return (f"Bullet's chain stretches most among the engines ({float(stretch):.3g} cm, the others up to {max(others):.3g} cm, below one link and "
            "without energy growth): sequential impulse, 10 iterations, Baumgarte-only joint correction (ERP 0.2) and the rotation linearization "
            "of fast links (Progress M9 Bullet J-3) [R]", "inferred")


def _main_metric_values(ctx, diff, metric: str) -> list[tuple[str, float]]:
    """The main series' values of another metric of the difference's scene and profile (finite ones only)."""
    out = []
    for label in ("jolt.Default", "bullet.Default", "physx.UE4Compat"):
        result = ctx.result_of_label(diff, label)
        value = None if result is None else result["metrics"].get(metric)
        if finite(value):
            out.append((label, float(value)))
    return out


def _chain_held(diff, ctx) -> bool:
    """M9 close guard of the chain rules: this run's chain held - its stretch below one link - and kept its energy (growth within the
    analytic bound of the scene)."""
    stretch, energy, link = ctx.metric_value(diff, "stretch_max_cm"), ctx.metric_value(diff, "energy_growth_pct"), ctx.detail(diff, "link_length_cm")
    bound = TOLERANCES.get(f"{str(diff['scenario']).split('.')[0]}.energy_growth_pct")
    return (finite(stretch) and finite(link) and float(stretch) < float(link) and finite(energy) and bound is not None
            and float(energy) <= bound.value)


JOLT_DAMP0_PROBE = "jolt.Damp0"   # UEParity M6c with ForcedOverrides BodyInstance.LinearDamping=0 (Config/Matrices/behavior.json)


def _rule_jolt_heavy_chain(diff, ctx):
    if diff["backend"] != "Jolt" or diff["scenario"] != "M6c.HeavyChain":
        return None
    # M9 fix round 2 (checker r2): each branch checks what it states. UEParity: every run of this series tore apart (joints_separated)
    # and the jolt.Damp0 probe of this scene is within the tolerance of the same series' Normalized run - else None. Normalized: the
    # energy stays within the analytic bound and the stretch below one link - else None; its explanation (a convergence limit of Jolt's
    # default steps) is the Jolt implementer's iteration probe, not this session's data, so it is inferred.
    link = ctx.detail(diff, "link_length_cm")
    if diff["profile"] == "UEParity":
        runs = [run for run in ctx.runs if run.label == diff["label"] and run.profile == diff["profile"] and run.scenario == diff["scenario"]
                and id(run) in ctx.results]
        if not runs or not all(ctx.results[id(run)]["details"].get("joints_separated") for run in runs):
            return None
        probe = ctx.result_of_label(diff, JOLT_DAMP0_PROBE)
        probe_value = None if probe is None else probe["metrics"].get(diff["metric"])
        normalized = _value_in_profile(ctx, diff, "Normalized")
        if not (finite(probe_value) and finite(normalized) and abs(float(probe_value) - normalized) <= float(diff["tolerance"])):
            return None
        stretch = sorted((run.perturb, ctx.results[id(run)]["metrics"].get("stretch_max_cm")) for run in runs)
        shown = ", ".join(f"{p}: {s:.4g}" for p, s in stretch if finite(s))
        return (f"Jolt's 100:1 chain is marginal at its default 10 velocity / 2 position steps (Progress M9 Jolt J-2: raising the velocity "
                f"steps alone tears it). UEParity tips it over: every run tears apart (stretch cm per Perturb run: {shown}; one link "
                f"{float(link):.3g} cm), while the {JOLT_DAMP0_PROBE} probe (UEParity with UE's linear damping forced to 0) gives {float(probe_value):.4g} "
                f"here - the Normalized run's {normalized:.4g}: UE's damping 0.01 is the trigger. It is a trigger, not a damping-specific mechanism "
                f"(M9 fix round 2 probes, Normalized: gravity -979 / -981 cm/s^2 without damping, angular damping 0.01 alone, and linear + angular "
                f"0.01 - a uniform velocity scaling that keeps every joint's velocity constraint - tear it at frame 86-115 too; gravity -979.99 / "
                f"-980.1 holds, 36.7-37.2 cm). The adapter passes UE's damping to Jolt unchanged (MotionProperties, READBACK body.damping)", "verified")
    energy, stretch = ctx.metric_value(diff, "energy_growth_pct"), ctx.metric_value(diff, "stretch_max_cm")
    if not (finite(energy) and energy <= TOLERANCES["M6c.energy_growth_pct"].value and finite(stretch) and finite(link) and stretch < float(link)):
        return None
    return (f"Jolt's 100:1 chain stretches {stretch:.3g} cm (below one {float(link):.3g} cm link) at its default 10 velocity / 2 position steps "
            f"without diverging (energy growth {energy:+.3g}% of m g span): a convergence limit - 40 velocity / 10 position steps leave 8.4 cm "
            f"(Progress M9 Jolt J-2, the implementer's probe; not in this session). Marginal: gravity changed by 0.1% tears it (M9 fix round 2) [R]",
            "inferred")


def _rule_chain_iterative_joints(diff, ctx) -> str | None:
    if not _chain(diff) or _metric(diff) not in ("stretch_max_cm", "stretch_late_mean_cm", "final_z_mean_cm", "energy_growth_pct"):
        return None
    # M9 close guard (reviewer r3): the text explains a chain that held - stretched within one link - and kept its energy; a chain that
    # tore apart or whose energy grew (an energy difference itself, e.g. +500% in a rerun) is not this rule's: None, so it surfaces
    if not _chain_held(diff, ctx):
        return None
    iters = ctx.effective(diff, "solver.iterations")
    stretch, link = ctx.metric_value(diff, "stretch_max_cm"), ctx.detail(diff, "link_length_cm")
    return (f"a chain released horizontally: the engine's iterative joint solver with its default budget ({iters} iterations) leaves the "
            f"far links behind in the first swing, so every engine stretches beyond a hard joint's 1 cm (TGS least, PhysX SdkDefault's 4 / 1 "
            f"most; here {float(stretch):.3g} cm, below one {float(link):.3g} cm link, energy within its bound); where the chain hangs afterwards "
            f"follows that and the energy each solver keeps (03 M9: report the stretch) [R]")


RULES: list[Rule] = [
    Rule("desc-mismatch", "mismatch", "verified", "02 12.3, DL21", _rule_desc_mismatch),
    Rule("sleep-in-sleepless-profile", "mismatch", "verified", "DL48, G6", _rule_sleep_in_sleepless_profile),
    Rule("bullet-restitution-contact-state", "algorithmic", "verified", "btSequentialImpulseConstraintSolver.cpp:944-972; Progress M9 준비 F-4; tolerances.json G4 model 2 (signed off 2026-10-22)", _rule_bullet_restitution),
    Rule("physx-restitution-presolve", "algorithmic", "accepted", "Progress M6 D-3; tolerances.json G4.restitution_velocity_model", _rule_physx_restitution),
    Rule("physx-tgs-split-contact", "algorithmic", "inferred", "M9 data", _rule_physx_tgs_split_contact),
    Rule("impact-phase-depth", "algorithmic", "inferred", "M9 fix round 2 (was contact-distance, by-design): every impact within [-d, max(travel - d, 0)], d = effective contact.speculative_distance; no contact-distance probe", _rule_impact_phase_depth),
    Rule("jolt-penetration-slop", "by-design", "verified", "Jolt.Default.ini PenetrationSlop; Scenes.md 2.2, 2.14 (guard, M9 close: depth <= slop + 0.25 cm; a lower rest height = the run's own rest penetration; a stack below the consensus by <= slop x contacts and standing)", _rule_jolt_slop),
    Rule("jolt-convex-radius", "by-design", "verified", "Profiles.md 4 (Jolt.SharpBoxes)", _rule_jolt_convex_radius),
    Rule("physx-pgs-creep", "algorithmic", "accepted", "Progress M6 D-4; M9 준비 F-2", _rule_physx_pgs_creep),
    Rule("physx-tgs-creep", "algorithmic", "inferred", "M6 G5; Progress M9 준비 F-2 (guard: creep <= 0.2 cm/s, drift within the registered TGS value)", _rule_physx_tgs_creep),
    Rule("physx-pgs-friction", "algorithmic", "inferred", "Progress M6 D-4 (mechanism; D-4 accepted the creep, the M9 commit the M4 bound 6.5%); M9 준비 F-3; guarded by TGS's value and the override", _rule_physx_pgs_friction),
    Rule("bullet-sliding-box", "algorithmic", "inferred", "M9 data (guard: |w| >= 2x the other engines', the slide lags, drift <= 5 cm, depth <= 1 cm)", _rule_bullet_sliding_box),
    Rule("bullet-tumble", "algorithmic", "inferred", "M9 data (guard: ends on a face within one edge length of the consensus)", _rule_bullet_tumble),
    Rule("edge-landing", "algorithmic", "inferred", "M9 data (M1 b beyond the signed 10 cm; guard: ends on a face within one edge length of the consensus)", _rule_edge_landing),
    Rule("sleep-thresholds-unmapped", "mismatch", "verified", "Profiles.md 5; effective sleep.criteria", _rule_sleep_unmapped),
    Rule("ue4compat-tower", "by-design", "verified", "Progress M9 준비 F-5; Checkpoints N-3; 01 probe", _rule_ue4compat_tower),
    Rule("sleep-engine-rule", "by-design", "verified", "Profiles.md 5; Checkpoints N-2, N-3 (PhysX only; the speed under the configured threshold, the time after the wake counter)", _rule_sleep_engine),
    Rule("restitution-threshold", "by-design", "verified", "DL35, Q11; Profiles.md 5 (only where the run's threshold differs from the consensus engines')", _rule_restitution_threshold),
    Rule("bullet-stack-solver", "algorithmic", "verified", "Checkpoints N-1; M9 probes bullet.R0 / Iter50 / R0Iter50 (verified only where R0Iter50 holds and each alone does not; guard: the stack fell)", _rule_bullet_stack),
    Rule("stack-consensus-slop", "by-design", "verified", "M9 fix round 1; rule jolt-penetration-slop (a standing stack between Jolt's consensus and the overlap-free height)", _rule_stack_consensus_slop),
    Rule("physx-tgs-pile-ejection", "algorithmic", "verified", "M9 fix round 1; Checkpoints N-4; probes physx.TGS-R0 / TGS-Pos32 (inferred without them)", _rule_physx_tgs_pile_ejection),
    Rule("physx-sdkdefault-pile", "by-design", "verified", "M9 fix round 1; Checkpoints N-5; probe physx.SdkDefault-Pos8 (inferred without it)", _rule_physx_sdkdefault_pile),
    Rule("bullet-p2p-rows", "algorithmic", "verified", "Progress M9 Bullet J-1; BulletBackend.BallJointRowCoupling (guard: stretch beyond 0.5 cm and the hinge's, a longer period)", _rule_bullet_p2p),
    Rule("pendulum-ue-damping", "by-design", "verified", "Profiles.md 5 (UEParity damping 0.01); M9 damping model", _rule_pendulum_ue_damping),
    Rule("pendulum-velocity-projection", "algorithmic", "verified", "Progress M9 Jolt J-1, PhysX PX-2; M9 projection model", _rule_pendulum_velocity_projection),
    Rule("physx-tgs-pendulum", "algorithmic", "inferred", "Progress M9 PhysX PX-2 (guard: >= 30 deg, amplitude grows without damping, a longer period)", _rule_physx_tgs_pendulum),
    Rule("bullet-hinge-large-swing", "algorithmic", "inferred", "Progress M9 Bullet J-3 (swings of 30 deg and more)", _rule_bullet_hinge_large_swing),
    Rule("joint-rotation-linearization", "algorithmic", "verified", "Progress M9 Bullet J-3 (2 (w dt)^2 L); verified for Bullet, inferred elsewhere", _rule_joint_rotation_linearization),
    Rule("physx-pgs-heavy-chain", "algorithmic", "verified", "Progress M9 PhysX PX-1 (guard: joints apart and energy above 100% of m g span); lead 2026-10-22: accepted engine behaviour (M6c)", _rule_physx_pgs_heavy_chain),
    Rule("physx-tgs-heavy-chain", "algorithmic", "inferred", "M9 replicas (guard: every run above the release energy); lead 2026-10-22: accepted engine behaviour (M6c)", _rule_physx_tgs_heavy_chain),
    Rule("bullet-chain", "algorithmic", "verified", "Progress M9 Bullet J-2 (M6c with the joints apart: stretch verified, energy inferred; lead 2026-10-22: a finding, joint separation), J-3 (M6b, inferred; guard: held, energy within its bound, the largest stretch)", _rule_bullet_chain),
    Rule("jolt-heavy-chain", "algorithmic", "verified", "Progress M9 Jolt J-2; M9 probe jolt.Damp0 (UEParity: every run apart, the probe = Normalized; Normalized: energy and stretch bounds, inferred)", _rule_jolt_heavy_chain),
    Rule("chain-iterative-joints", "algorithmic", "inferred", "03 M9 (report the stretch); Progress M9 (guard: stretch below one link, energy within its bound)", _rule_chain_iterative_joints),
    Rule("variant-config", "by-design", "verified", "Profiles.md 4; effective settings (not in a chaotic cell without replicas)", _rule_variant_config),
    Rule("jolt-restitution-low-speed", "algorithmic", "verified", "registered G4 model 0 (Jolt); rules physx-restitution-presolve, bullet-restitution-contact-state", _rule_jolt_restitution_low_speed),
    Rule("restitution-model-low-speed", "algorithmic", "inferred", "rules physx-restitution-presolve, bullet-restitution-contact-state; M6 D-3", _rule_restitution_low_speed),
    Rule("chaotic-solver", "algorithmic", "inferred", "02 13; 01 7 (only with Perturb replicas of both series; consensus observables only)", _rule_chaotic),
]
UNEXPLAINED = Rule("unexplained", "mismatch", "provisional", "no registered mechanism: investigate", lambda d, c: None)


def tag_with_confidence(diff: dict[str, Any], ctx: Any) -> tuple[Rule, str, str]:
    """The first rule that explains the difference, its reason and the confidence of the branch that matched (a reason function
    returns a text, or (text, confidence) when its branches differ in how well the mechanism is established)."""
    for rule in RULES:
        try:
            reason = rule.reason(diff, ctx)
        except (KeyError, TypeError, ValueError, ZeroDivisionError):
            reason = None
        if isinstance(reason, tuple):
            reason, confidence = reason
        else:
            confidence = rule.confidence
        if reason:
            return rule, reason, confidence
    return UNEXPLAINED, ("no registered rule explains this difference: tagged mismatch (provisional) until a mechanism is found and registered "
                         "in behavior_rules.RULES"), UNEXPLAINED.confidence


def tag(diff: dict[str, Any], ctx: Any) -> tuple[Rule, str]:
    rule, reason, _ = tag_with_confidence(diff, ctx)
    return rule, reason


def finite(value: Any) -> bool:
    try:
        return value is not None and math.isfinite(float(value))
    except (TypeError, ValueError):
        return False
