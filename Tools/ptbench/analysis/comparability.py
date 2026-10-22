"""comparability - the comparability key of a run (02_Architecture 13 "Comparability key"; 03_WorkPlan M10 task 5).

    key = SHA-256 of the canonical JSON (keys sorted, no whitespace, UTF-8) of
          {"schema": "physicstest.comparability/1", scene_sha256, desc_sha256, physics_sha256, mass_rule, flag_profile, tick_mode,
           dt_bits, T, thread_series, sync_policy, mode, instrumentation (sorted), harness_commit}
    dt_bits         the IEEE-754 bits of run.dt_s as 16 hex digits (a float in text is not a stable key)
    harness_commit  harness.git_sha, "+dirty" when harness.dirty
    thread_series   threads.series (an M10 addition: p_cores_one_lp / p_cores_smt / with_e_cores runs never share a table,
                    docs/TimingContract.md 7)
The bench computes it (FRunManifest::ComputeComparabilityKey) and writes it into the manifest; key_from_manifest recomputes it
from the same fields, and validate rejects a manifest whose stored key does not match (a copied or edited manifest). One
timing table = one key: engines and variants are the series of the table (their options_sha256 / variant_sha256 may
differ), frames are inside desc_sha256 (the dt sequence) and the scene's windows inside scene_sha256 (name, version,
parameters, seed). desc_sha256 is in the key, so every run of a table has the same scene input by construction; validate
still asserts it (DL21).
"""
from __future__ import annotations

import hashlib
import json
import struct
from typing import Any

SCHEMA = "physicstest.comparability/1"
FIELDS = ("scene_sha256", "desc_sha256", "physics_sha256", "mass_rule", "flag_profile", "tick_mode", "dt_bits", "T", "thread_series", "sync_policy", "mode",
          "instrumentation", "harness_commit")


def dt_bits(value: Any) -> str | None:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    return f"{struct.unpack('<Q', struct.pack('<d', float(value)))[0]:016x}"


def key_fields(manifest: dict[str, Any]) -> dict[str, Any]:
    """The key's fields from a manifest (missing groups give null, as in the C++)."""
    def field(group: str, key: str) -> Any:
        value = manifest.get(group)
        return value.get(key) if isinstance(value, dict) else None

    instrumentation = field("run", "instrumentation")
    if isinstance(instrumentation, list):
        instrumentation = sorted(str(v) if isinstance(v, str) else json.dumps(v) for v in instrumentation)
    sha = field("harness", "git_sha")
    dirty = field("harness", "dirty")
    return {
        "schema": SCHEMA,
        "scene_sha256": field("scene", "scene_sha256"),
        "desc_sha256": field("scene", "desc_sha256"),
        "physics_sha256": field("profile", "physics_sha256"),
        "mass_rule": field("profile", "mass_rule"),
        "flag_profile": field("build", "flag_profile"),
        "tick_mode": field("run", "tick_mode"),
        "dt_bits": dt_bits(field("run", "dt_s")),
        "T": field("threads", "T"),
        "thread_series": field("threads", "series"),
        "sync_policy": field("run", "sync_policy"),
        "mode": field("run", "mode"),
        "instrumentation": instrumentation,
        "harness_commit": (sha if isinstance(sha, str) else "") + ("+dirty" if dirty is True else ""),
    }


def canonical(fields: dict[str, Any]) -> str:
    return json.dumps(fields, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def key_from_manifest(manifest: dict[str, Any]) -> str | None:
    """The comparability key of a run that built its scene (None without a desc_sha256: nothing to compare)."""
    if not isinstance(manifest, dict) or not (manifest.get("scene") or {}).get("desc_sha256"):
        return None
    return hashlib.sha256(canonical(key_fields(manifest)).encode("utf-8")).hexdigest()


def describe(row: dict[str, Any]) -> str:
    """A readable table title for a key: scene, profile, flag profile, T, thread series (when not the default) and the key's
    first 8 hex digits."""
    series = row.get("thread_series")
    parts = [str(row.get("scene")), str(row.get("profile")), str(row.get("flag_profile")), f"T{row.get('T')}"]
    if series and series != "p_cores_one_lp":
        parts.append(str(series))
    key = row.get("comparability_key") or ""
    return " · ".join(parts) + (f" · key {str(key)[:8]}" if key else "")
