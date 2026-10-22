"""bootstrap - confidence intervals of the timing statistics (02_Architecture 13 perf; 03_WorkPlan M10 task 5).

  percentile_ci          the percentile bootstrap 95 % CI of one cell's median (B = 10000 resamples of its run values)
  block_bootstrap_ratio  the ratio rho = median(A) / median(B) of two series of one table and its 95 % CI by a block bootstrap:
                         a block is one repeat of the runner (every cell once, in a seeded shuffle, 02 12.5), so A's and B's runs
                         of one block ran minutes apart under the same machine state; resampling whole blocks keeps that pairing
                         (a slow block slows both) instead of treating the runs as independent.
Resampling is seeded from the inputs' identity (seed_for), so a report built twice from the same runs shows the same CI.
"""
from __future__ import annotations

import dataclasses
import hashlib
import math
from typing import Hashable, Iterable, Mapping

import numpy as np

B_DEFAULT = 10000
ALPHA_DEFAULT = 0.05


def seed_for(*parts: object) -> int:
    return int(hashlib.sha256("|".join(map(str, parts)).encode("utf-8")).hexdigest()[:16], 16)


@dataclasses.dataclass(frozen=True)
class Interval:
    estimate: float
    low: float
    high: float
    n: int
    resamples: int


def percentile_ci(values: Iterable[float], resamples: int = B_DEFAULT, alpha: float = ALPHA_DEFAULT, seed: int = 0) -> Interval | None:
    """The median of the values and its percentile-bootstrap (1 - alpha) interval; None for fewer than 2 values."""
    data = np.asarray([float(v) for v in values if v is not None and math.isfinite(float(v))], dtype=float)
    if data.size < 2:
        return None
    rng = np.random.default_rng(seed)
    medians = np.median(data[rng.integers(0, data.size, size=(resamples, data.size))], axis=1)
    low, high = np.quantile(medians, [alpha / 2.0, 1.0 - alpha / 2.0])
    return Interval(float(np.median(data)), float(low), float(high), int(data.size), resamples)


def block_bootstrap_ratio(a: Mapping[Hashable, float], b: Mapping[Hashable, float], resamples: int = B_DEFAULT, alpha: float = ALPHA_DEFAULT,
                          seed: int = 0) -> Interval | None:
    """rho = median(A) / median(B) over the blocks both series have (block key -> run value; perf keys a block by session and
    repeat, so two sessions never share one), and its block-bootstrap interval:
    each resample draws whole blocks with replacement and takes the ratio of the two medians. None with fewer than 2 common
    blocks or a non-positive median."""
    blocks = sorted(set(a) & set(b))
    if len(blocks) < 2:
        return None
    va = np.asarray([float(a[k]) for k in blocks], dtype=float)
    vb = np.asarray([float(b[k]) for k in blocks], dtype=float)
    if not (np.all(np.isfinite(va)) and np.all(np.isfinite(vb))) or np.median(vb) <= 0:
        return None
    rng = np.random.default_rng(seed)
    index = rng.integers(0, len(blocks), size=(resamples, len(blocks)))
    denominators = np.median(vb[index], axis=1)
    ratios = np.median(va[index], axis=1) / np.where(denominators > 0, denominators, np.nan)
    ratios = ratios[np.isfinite(ratios)]
    if ratios.size == 0:
        return None
    low, high = np.quantile(ratios, [alpha / 2.0, 1.0 - alpha / 2.0])
    return Interval(float(np.median(va) / np.median(vb)), float(low), float(high), len(blocks), resamples)


def independent_ratio(a: Iterable[float], b: Iterable[float], resamples: int = B_DEFAULT, alpha: float = ALPHA_DEFAULT, seed: int = 0) -> Interval | None:
    """rho = median(A) / median(B) of two independent samples (runs of two sessions: no shared blocks) and its percentile-bootstrap
    interval, each sample resampled on its own. Used by the binary composition control (test-<engine> vs test, M10 / M11)."""
    va = np.asarray([float(v) for v in a if v is not None and math.isfinite(float(v))], dtype=float)
    vb = np.asarray([float(v) for v in b if v is not None and math.isfinite(float(v))], dtype=float)
    if va.size < 2 or vb.size < 2 or np.median(vb) <= 0:
        return None
    rng = np.random.default_rng(seed)
    ma = np.median(va[rng.integers(0, va.size, size=(resamples, va.size))], axis=1)
    mb = np.median(vb[rng.integers(0, vb.size, size=(resamples, vb.size))], axis=1)
    ratios = ma / mb
    low, high = np.quantile(ratios, [alpha / 2.0, 1.0 - alpha / 2.0])
    return Interval(float(np.median(va) / np.median(vb)), float(low), float(high), int(min(va.size, vb.size)), resamples)
