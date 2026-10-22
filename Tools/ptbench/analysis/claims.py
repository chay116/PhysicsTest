"""claims - the claim rule (02_Architecture 13 "주장 규칙"; 03_WorkPlan M10 task 5).

"A is faster than B on scene S" may be written only when
  1. the block-bootstrap ratio CI of rho = T_frame(A) / T_frame(B) excludes 1,
  2. |rho - 1| >= 3 %,
  3. and both hold with the same direction in the Shipped and the MatchedAVX2 tables (test and test-avx2 builds).
When the two flag profiles point in opposite directions the comparison is "ISA-dependent". Anything else is no claim, with
the reason. M10 fix round 1 (docs/TimingContract.md 9: only cells that passed the CV gate are reportable): a pair with a noisy
cell - the runner's CV gate marked A or B noisy in either flag profile - is no claim either, whatever its intervals say; the
reason names the cells and what the intervals alone would have given. evaluate() is the whole rule; the report applies it to
every pair of series that appears in a Shipped table and its MatchedAVX2 counterpart (same scene, profile, T and thread
series). A session that is unofficial keeps its claims unofficial.
"""
from __future__ import annotations

import dataclasses
from typing import Any, Sequence

from .bootstrap import Interval

MIN_EFFECT = 0.03

VERDICT_FASTER = "faster"                 # A is faster than B (rho < 1)
VERDICT_SLOWER = "slower"                 # A is slower than B (rho > 1)
VERDICT_ISA = "ISA-dependent"
VERDICT_NONE = "no claim"


@dataclasses.dataclass(frozen=True)
class Claim:
    verdict: str
    reason: str
    text: str


def _direction(ratio: Interval) -> int:
    return -1 if ratio.estimate < 1.0 else (1 if ratio.estimate > 1.0 else 0)


def _qualifies(ratio: Interval, min_effect: float) -> str:
    """'' when one flag profile alone supports a claim, else why not."""
    if ratio.low <= 1.0 <= ratio.high:
        return f"the ratio CI [{ratio.low:.4f}, {ratio.high:.4f}] includes 1"
    if abs(ratio.estimate - 1.0) < min_effect:
        return f"|rho - 1| = {abs(ratio.estimate - 1.0) * 100:.2f} % < {min_effect * 100:g} %"
    return ""


def evaluate(a: str, b: str, scene: str, shipped: Interval | None, avx2: Interval | None, min_effect: float = MIN_EFFECT,
             noisy: Sequence[str] = ()) -> Claim:
    """The claim about series a against series b on one scene from the two flag profiles' ratios (rho = A / B). noisy: the
    cells of the pair the CV gate marked noisy ("A Shipped", ...): any one makes the pair no claim (M10 fix round 1)."""
    claim = _evaluate_intervals(a, b, scene, shipped, avx2, min_effect)
    if noisy and claim.verdict != VERDICT_NONE:
        cells = ", ".join(noisy)
        return Claim(VERDICT_NONE, f"noisy cell(s) after the CV gate ({cells}): not reportable; the intervals alone give {claim.verdict}",
                     f"{a} vs {b} on {scene}: no claim (noisy cell(s): {cells}; the intervals alone: {claim.verdict})")
    return claim


def _evaluate_intervals(a: str, b: str, scene: str, shipped: Interval | None, avx2: Interval | None, min_effect: float) -> Claim:
    """The rule's three conditions on the two intervals."""
    if shipped is None:
        return Claim(VERDICT_NONE, "no Shipped ratio (fewer than two common blocks)", f"{a} vs {b} on {scene}: no claim (no Shipped ratio)")
    shipped_why = _qualifies(shipped, min_effect)
    avx2_why = _qualifies(avx2, min_effect) if avx2 is not None else "no MatchedAVX2 ratio (the rule needs test-avx2 too)"
    if avx2 is not None and _direction(shipped) != _direction(avx2) and (not shipped_why or not avx2_why):
        return Claim(VERDICT_ISA, f"Shipped rho {shipped.estimate:.4f}, MatchedAVX2 rho {avx2.estimate:.4f}: opposite directions",
                     f"{a} vs {b} on {scene}: ISA-dependent (Shipped rho {shipped.estimate:.3f}, MatchedAVX2 rho {avx2.estimate:.3f})")
    if shipped_why:
        return Claim(VERDICT_NONE, "Shipped: " + shipped_why, f"{a} vs {b} on {scene}: no claim (Shipped: {shipped_why})")
    if avx2_why:
        return Claim(VERDICT_NONE, ("MatchedAVX2: " if avx2 is not None else "") + avx2_why, f"{a} vs {b} on {scene}: no claim ({avx2_why})")
    faster = shipped.estimate < 1.0
    verdict = VERDICT_FASTER if faster else VERDICT_SLOWER
    word = "faster" if faster else "slower"
    return Claim(verdict, "", f"{a} is {word} than {b} on {scene}: rho {shipped.estimate:.3f} (95 % CI [{shipped.low:.3f}, {shipped.high:.3f}]) Shipped, "
                              f"{avx2.estimate:.3f} ([{avx2.low:.3f}, {avx2.high:.3f}]) MatchedAVX2")


def as_dict(claim: Claim) -> dict[str, Any]:
    return dataclasses.asdict(claim)
