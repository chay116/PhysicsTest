"""common - shared helpers of ptbench: repository paths, JSON I/O, the Config/Schemas validators, file hashes, the seeded
shuffle of the runner (02_Architecture 12.5: "반복 r마다 seed 고정 셔플") and the nearest-rank percentile the bench uses.

Every file is read and written as UTF-8 (the bench writes UTF-8; the Windows default code page is not). JSON documents are
written atomically (temporary file + os.replace), like the bench's exit.json.
"""
from __future__ import annotations

import datetime as _dt
import functools
import hashlib
import json
import math
import os
import pathlib
import sys
import time
from typing import Any, Iterable, Sequence

MASK64 = (1 << 64) - 1


def use_utf8_stdio() -> None:
    """stdout / stderr as UTF-8 (errors replaced). A redirected or piped stream of a Windows process uses the ANSI code page
    (CP949 on this Korean machine), which cannot encode 'µs' and turns Korean plan names into mojibake in a UTF-8 log; and
    python -I ignores PYTHONIOENCODING / PYTHONUTF8. An interactive console already writes UTF-16 (PEP 528), so this changes
    nothing there. Every file ptbench writes is UTF-8; the console output now is too."""
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            try:
                reconfigure(encoding="utf-8", errors="replace")
            except (ValueError, OSError):
                pass


def find_executable(name: str) -> str | None:
    """The full path of <name>.exe from the absolute directories of PATH, or None. Never the current directory or the
    interpreter's directory: CreateProcess with a bare name searches those first (and Python 3.11's shutil.which puts the
    current directory first on Windows), which would defeat python -I (no code from the current directory)."""
    if os.path.isabs(name):
        return name if os.path.isfile(name) else None
    filename = name if name.lower().endswith(".exe") else name + ".exe"
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        directory = directory.strip().strip('"')
        if not directory or not os.path.isabs(directory):
            continue
        candidate = os.path.join(directory, filename)
        if os.path.isfile(candidate):
            return candidate
    return None


def system32_executable(name: str) -> str:
    """%SystemRoot%\\System32\\<name> (e.g. powercfg.exe), by its full path."""
    return os.path.join(os.environ.get("SystemRoot") or r"C:\Windows", "System32", name)


def repo_root() -> pathlib.Path:
    """The repository root: PT_REPO_ROOT when set, else two levels above this package (Tools/ptbench -> repo)."""
    override = os.environ.get("PT_REPO_ROOT")
    if override:
        return pathlib.Path(override).resolve()
    return pathlib.Path(__file__).resolve().parent.parent.parent


def schemas_dir() -> pathlib.Path:
    return repo_root() / "Config" / "Schemas"


def matrices_dir() -> pathlib.Path:
    return repo_root() / "Config" / "Matrices"


def default_results_dir() -> pathlib.Path:
    """Where the bench writes its runs by default (02_Architecture 12.3: Saved/Results/<session>/<run_id>)."""
    return repo_root() / "Saved" / "Results"


def default_bench_exe(preset: str) -> pathlib.Path:
    return repo_root() / "out" / preset / "bin" / "PhysicsTestBench.exe"


def read_json(path: os.PathLike | str) -> Any:
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def try_read_json(path: os.PathLike | str) -> tuple[Any, str]:
    """(document, "") or (None, error text) - a missing or half-written file is data, not an exception."""
    try:
        return read_json(path), ""
    except FileNotFoundError:
        return None, "missing"
    except (OSError, ValueError) as error:
        return None, f"unreadable: {error}"


REPLACE_ATTEMPTS = 30     # x REPLACE_WAIT_S: about 3 s
REPLACE_WAIT_S = 0.1


def write_text_atomic(path: os.PathLike | str, text: str) -> None:
    """Temporary file + os.replace. On Windows the replace fails with PermissionError while another process (a viewer, an
    antivirus scan, an editor) holds the target open; it is retried for about 3 s before the error is raised, so one
    such moment does not abort an unattended session."""
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    for attempt in range(REPLACE_ATTEMPTS):
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            if attempt == REPLACE_ATTEMPTS - 1:
                raise
            time.sleep(REPLACE_WAIT_S)


def write_json_atomic(path: os.PathLike | str, document: Any) -> None:
    write_text_atomic(path, json.dumps(document, indent=2, ensure_ascii=False) + "\n")


def sha256_file(path: os.PathLike | str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def utc_now() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def utc_stamp() -> str:
    """A compact UTC time for directory names (20261022T093000Z)."""
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


@functools.lru_cache(maxsize=None)
def validator(schema_file: str):
    """A Draft 2020-12 validator for Config/Schemas/<schema_file> (the schema itself is checked first)."""
    import jsonschema

    schema = read_json(schemas_dir() / schema_file)
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def schema_errors(document: Any, schema_file: str, limit: int = 5) -> list[str]:
    """The first validation errors of document against Config/Schemas/<schema_file> ("path: message"), [] when valid."""
    errors = sorted(validator(schema_file).iter_errors(document), key=lambda e: list(e.absolute_path))
    out = []
    for error in errors[:limit]:
        location = "/".join(str(p) for p in error.absolute_path) or "<root>"
        out.append(f"{location}: {error.message[:240]}")
    if len(errors) > limit:
        out.append(f"... {len(errors) - limit} more")
    return out


# ---- the runner's seeded shuffle ----------------------------------------------------------------------------------------
# splitmix64 (Steele, Lea, Flood 2014) drives a Fisher-Yates shuffle, so a block's order depends only on (block seed, the
# cell list) - not on the Python version (random.shuffle's algorithm is not a documented contract).

def splitmix64(state: int) -> tuple[int, int]:
    """One splitmix64 step: (next state, output)."""
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


def block_seed(base_seed: int, repeat_index: int) -> int:
    """The seed of block (repeat) r: splitmix64's output for state base_seed + r (64-bit, recorded in manifest.order)."""
    _, value = splitmix64((base_seed + repeat_index) & MASK64)
    return value


def seeded_shuffle(items: Sequence[Any], seed: int) -> list[Any]:
    """Fisher-Yates with splitmix64; the bound uses a 128-bit multiply-shift (no modulo bias worth noting for n < 2^32)."""
    out = list(items)
    state = seed & MASK64
    for i in range(len(out) - 1, 0, -1):
        state, value = splitmix64(state)
        j = (value * (i + 1)) >> 64
        out[i], out[j] = out[j], out[i]
    return out


# ---- statistics shared with the bench ------------------------------------------------------------------------------------

def nearest_rank(sorted_values: Sequence[float], percent: float) -> float:
    """Nearest-rank percentile of an ascending sequence (the bench's summary statistics use the same rule)."""
    if len(sorted_values) == 0:
        return math.nan
    rank = max(1, math.ceil(percent / 100.0 * len(sorted_values)))
    return float(sorted_values[min(rank, len(sorted_values)) - 1])


def cv_percent(values: Iterable[float]) -> float:
    """Coefficient of variation in % (sample standard deviation, ddof = 1, over the mean); nan for fewer than 2 values."""
    data = [float(v) for v in values]
    if len(data) < 2:
        return math.nan
    mean = sum(data) / len(data)
    if mean == 0.0:
        return math.nan
    variance = sum((v - mean) ** 2 for v in data) / (len(data) - 1)
    return 100.0 * math.sqrt(variance) / mean


def short_hash(text: str, length: int = 8) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:length]


def format_params(params: dict[str, str]) -> str:
    """M3.BoxPile + {"N": "1000"} -> "[N=1000]" (sorted; "" without parameters)."""
    if not params:
        return ""
    return "[" + ",".join(f"{k}={params[k]}" for k in sorted(params)) + "]"
