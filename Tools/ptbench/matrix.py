"""matrix - a run matrix (Config/Matrices/<name>.json, schema physicstest.matrix/1) and its expansion into cells and blocks
(02_Architecture 12.5 steps 3-4; 03_WorkPlan M8 task 3).

A cell is one (target = backend + variant, physics profile, scene + parameters, T) combination. A block is one repeat: every
cell once, in an order shuffled with the block's own seed (common.block_seed(matrix seed, r) and common.seeded_shuffle), so a
session is reproducible from the matrix file alone. The bench records the repeat index, the position in the block and the
block seed in manifest.order (-Repeat, -Order, -BlockSeed).

Extra bench switches come from four places: the matrix's bench_args, a target's bench_args, a scene's bench_args (M9: e.g.
-StatesEvery=1 for the behaviour scenes and a sparse -StatesEvery=100 for a 1000-body pile in the same matrix) and the
runner's --bench-arg. The bench reads a switch with FParse::Value, which takes the FIRST occurrence on the command line (UE
Parse.cpp:265 through FCString::Strifind; our Source/Runtime/Core/Private/Misc/Parse.cpp FindKey), and -Param: takes the
last; so a switch given twice would be ignored silently on one side, and a switch the runner sets per cell would lose to (or
override) the cell's own value. check_switches therefore refuses (MatrixError) a switch key that appears more than once over
the sources of one cell (matrix + --bench-arg + that target + that scene), and every switch the runner itself sets
(RESERVED_SWITCHES, -Param:<k>) or that turns the process into another mode (MODE_SWITCHES). With every key unique, the order
of the command line does not matter. A scene's bench_args do not enter the cell id: two scene entries that differ only in
their switches are duplicate cells (load_matrix refuses them).
"""
from __future__ import annotations

import dataclasses
import pathlib
from typing import Any

from . import common

MATRIX_SCHEMA = "matrix.schema.json"

# Set by the runner for every run: the cell (backend, variant, profile, scene, parameters, T, frames) and the session
# bookkeeping (-Repeat -Order -BlockSeed -Session -Out). Keys are lower case, without the leading '-'.
RESERVED_SWITCHES = {
    "out": "the runner gives every run its own -Out run directory",
    "backend": "targets[].backend", "backendprofile": "targets[].variant", "profile": "profiles[]", "scenario": "scenes[].scenario",
    "threads": "threads[]", "frames": "scenes[].frames", "repeat": "the runner's block bookkeeping", "order": "the runner's block bookkeeping",
    "blockseed": "the runner's block bookkeeping", "session": "the runner's session id",
    "stampdir": "the runner passes its --stamp-dir to every run (M10: the run records the stamp it finds there)",
}
# Switches that make the process something other than one matrix run (no run directory the runner could judge).
MODE_SWITCHES = ("gate", "list", "describe", "conformance", "analyticreferences", "scenehashes", "busyhelper")


class MatrixError(ValueError):
    pass


def switch_keys(arg: str) -> list[str]:
    """The keys a bench switch sets, lower case (FParse matching is case-insensitive): "-PreRoll=0.5" -> ["preroll"],
    "-Param:N=1000" -> ["param:n"], "-ini:PhysicsTest:[S]:A=1,[S]:B=2" -> ["ini:physicstest:[s]:a", "ini:physicstest:[s]:b"],
    "-Foo" -> ["foo"]."""
    body = arg.lstrip("-/")
    lower = body.lower()
    if lower.startswith("ini:"):
        name, _, settings = body[4:].partition(":")
        return [f"ini:{name.lower()}:{pair.split('=', 1)[0].strip().lower()}" for pair in settings.split(",") if pair.strip()] or [f"ini:{name.lower()}"]
    if lower.startswith("param:"):
        return ["param:" + body[6:].split("=", 1)[0].strip().lower()]
    return [lower.split("=", 1)[0]]


def _scene_source(scene: dict[str, Any]) -> str:
    return f"scenes[{scene.get('scenario')}{common.format_params({str(k): str(v) for k, v in (scene.get('params') or {}).items()})}].bench_args"


def check_switches(document: dict[str, Any], extra_args: list[str] | None = None) -> None:
    """Raises MatrixError when a switch of the matrix's bench_args, a target's or a scene's bench_args or --bench-arg
    (extra_args) is one the runner sets itself or a mode switch, or when one key appears twice for a cell (FParse::Value
    reads the first occurrence only, so the other would be ignored silently)."""
    problems: list[str] = []
    shared = [("matrix bench_args", arg) for arg in document.get("bench_args", [])] + [("--bench-arg", arg) for arg in (extra_args or [])]
    targets = document.get("targets", [])
    scenes = document.get("scenes", [])
    every = (shared + [(f"targets[{t.get('label')}].bench_args", arg) for t in targets for arg in t.get("bench_args", [])]
             + [(_scene_source(s), arg) for s in scenes for arg in s.get("bench_args", [])])
    for source, arg in every:
        for key in switch_keys(arg):
            if key in RESERVED_SWITCHES:
                problem = f"{source} '{arg}': -{key} is set by the runner for every run ({RESERVED_SWITCHES[key]})"
            elif key.startswith("param:"):
                problem = f"{source} '{arg}': scene parameters belong in scenes[].params (the runner passes -Param: per cell)"
            elif key in MODE_SWITCHES:
                problem = f"{source} '{arg}': -{key} turns the process into another mode, not one matrix run"
            else:
                continue
            if problem not in problems:
                problems.append(problem)
    for target in targets or [{}]:
        own = [(f"targets[{target.get('label')}].bench_args", arg) for arg in target.get("bench_args", [])]
        for scene in scenes or [{}]:
            seen: dict[str, tuple[str, str]] = {}
            scene_own = [(_scene_source(scene), arg) for arg in scene.get("bench_args", [])]
            for source, arg in shared + own + scene_own:
                for key in switch_keys(arg):
                    if key in seen:
                        first_source, first_arg = seen[key]
                        message = (f"switch key '{key}' appears twice: {first_source} '{first_arg}' and {source} '{arg}' (the bench reads the first "
                                   f"occurrence only - FParse::Value, UE Parse.cpp:265 - so one of them would be ignored silently)")
                        if message not in problems:
                            problems.append(message)
                    else:
                        seen[key] = (source, arg)
    if problems:
        raise MatrixError("bench switches: " + "; ".join(problems))


@dataclasses.dataclass(frozen=True)
class Cell:
    label: str            # the engine series label of the tables, e.g. jolt.Default, physx.UE4Compat
    backend: str          # registry name: Jolt, Bullet, PhysX, Null, NullDll
    variant: str          # Config/Profiles/Backend/<backend>.<variant>.ini ("" = the bench's default)
    profile: str          # Normalized / UEParity
    scenario: str
    params: tuple[tuple[str, str], ...]
    threads: int
    frames: int           # 0 = the scene's default
    target_args: tuple[str, ...] = ()   # the target's own bench switches (e.g. -ini: / -ExecCmds of a variant); its label tells it apart
    scene_args: tuple[str, ...] = ()    # the scene entry's own bench switches (M9, e.g. -StatesEvery=1); part of the cell's switches

    @property
    def params_dict(self) -> dict[str, str]:
        return dict(self.params)

    @property
    def scene_label(self) -> str:
        return self.scenario + common.format_params(self.params_dict)

    @property
    def cell_id(self) -> str:
        frames = f"|F{self.frames}" if self.frames else ""
        return f"{self.label}|{self.profile}|{self.scene_label}|T{self.threads}{frames}"

    @property
    def key(self) -> str:
        """A short stable id for directory names (LongPathsEnabled=0: keep run paths short)."""
        return common.short_hash(self.cell_id)

    def bench_args(self, include_target_args: bool = True) -> list[str]:
        """The cell's switches (with the scene entry's bench_args), then (include_target_args) the target's own bench_args.
        No key of the target's or the scene's switches repeats another switch of the cell (check_switches), so their
        position does not decide anything."""
        args = [f"-Backend={self.backend}"]
        if self.variant:
            args.append(f"-BackendProfile={self.variant}")
        args += [f"-Profile={self.profile}", f"-Scenario={self.scenario}"]
        args += [f"-Param:{k}={v}" for k, v in self.params]
        args.append(f"-Threads={self.threads}")
        if self.frames:
            args.append(f"-Frames={self.frames}")
        args += list(self.scene_args)
        return args + (list(self.target_args) if include_target_args else [])

    def as_json(self) -> dict[str, Any]:
        out = {"cell_id": self.cell_id, "key": self.key, "label": self.label, "backend": self.backend, "variant": self.variant, "profile": self.profile,
               "scenario": self.scenario, "params": self.params_dict, "T": self.threads, "frames": self.frames, "bench_args": list(self.target_args)}
        if self.scene_args:
            out["scene_bench_args"] = list(self.scene_args)
        return out


@dataclasses.dataclass(frozen=True)
class PlannedRun:
    repeat: int
    order: int
    block_seed: int
    cell: Cell

    @property
    def run_name(self) -> str:
        return f"r{self.repeat}_{self.order:03d}_{self.cell.key}"


@dataclasses.dataclass
class Matrix:
    path: pathlib.Path
    document: dict[str, Any]
    sha256: str

    @property
    def name(self) -> str:
        return self.document["name"]

    @property
    def purpose(self) -> str:
        return self.document.get("purpose", "perf")

    @property
    def repeats(self) -> int:
        return int(self.document["repeats"])

    @property
    def seed(self) -> int:
        return int(self.document["block_seed"])

    def get(self, key: str, default: Any = None) -> Any:
        return self.document.get(key, default)

    def cells(self) -> list[Cell]:
        return expand_cells(self.document)

    def plan(self, repeats: int | None = None) -> list[PlannedRun]:
        return plan_blocks(self.cells(), self.seed, self.repeats if repeats is None else repeats)


def resolve_matrix_path(name_or_path: str) -> pathlib.Path:
    """'smoke' -> Config/Matrices/smoke.json; a path is used as given."""
    path = pathlib.Path(name_or_path)
    if path.suffix.lower() == ".json" or path.exists():
        return path.resolve()
    candidate = common.matrices_dir() / f"{name_or_path}.json"
    if not candidate.exists():
        raise MatrixError(f"no matrix '{name_or_path}' (looked for {candidate})")
    return candidate


def load_matrix(name_or_path: str) -> Matrix:
    path = resolve_matrix_path(name_or_path)
    document = common.read_json(path)
    errors = common.schema_errors(document, MATRIX_SCHEMA)
    if errors:
        raise MatrixError(f"{path}: does not match Config/Schemas/{MATRIX_SCHEMA}: " + "; ".join(errors))
    matrix = Matrix(path=path, document=document, sha256=common.sha256_file(path))
    cells = matrix.cells()
    if not cells:
        raise MatrixError(f"{path}: the matrix expands to no cell (every combination excluded?)")
    ids = [c.cell_id for c in cells]
    if len(set(ids)) != len(ids):
        raise MatrixError(f"{path}: duplicate cells")
    keys = [c.key for c in cells]
    if len(set(keys)) != len(keys):
        raise MatrixError(f"{path}: two cells share a short key (rename a label)")
    try:
        check_switches(document)
    except MatrixError as error:
        raise MatrixError(f"{path}: {error}") from None
    return matrix


def select_targets(matrix: Matrix, labels: list[str] | None) -> Matrix:
    """M10: the matrix with only the targets named (--target, repeatable): e.g. the binary composition control runs the Jolt targets
    of a matrix with the test-jolt exe. The document keeps its name, seed and every other field; its "$comment" gets
    "[ptbench --target: <labels>]" appended (session.json keeps the document, so the selection is on record). A sentinel outside
    the selection is dropped (the session then says "no sentinel cell declared"). MatrixError for an unknown label."""
    if not labels:
        return matrix
    known = [t["label"] for t in matrix.document["targets"]]
    unknown = [label for label in labels if label not in known]
    if unknown:
        raise MatrixError(f"--target {', '.join(unknown)}: not a target of matrix '{matrix.name}' ({', '.join(known)})")
    document = dict(matrix.document)
    document["targets"] = [t for t in matrix.document["targets"] if t["label"] in labels]
    sentinel = document.get("sentinel")
    if isinstance(sentinel, dict) and sentinel.get("label") not in labels:
        document.pop("sentinel")
    selected = Matrix(path=matrix.path, document=document, sha256=matrix.sha256)
    selected.document["$comment"] = (str(matrix.document.get("$comment", "")) + f" [ptbench --target: {', '.join(labels)}]").strip()
    return selected


def _matches(rule: dict[str, Any], cell: Cell) -> bool:
    """An exclude rule matches when every field it names matches (lists mean any of)."""
    values = {"label": cell.label, "backend": cell.backend, "variant": cell.variant, "profile": cell.profile, "scenario": cell.scenario, "T": cell.threads}
    for key, expected in rule.items():
        if key == "reason":
            continue
        if key == "params":
            params = cell.params_dict
            if any(params.get(k) != str(v) for k, v in expected.items()):
                return False
            continue
        allowed = expected if isinstance(expected, list) else [expected]
        if values.get(key) not in allowed:
            return False
    return True


def expand_cells(document: dict[str, Any]) -> list[Cell]:
    """target x profile x scene x T (02 12.5 step 3), minus the exclude rules; the order is the matrix file's order."""
    cells = []
    excludes = document.get("exclude", [])
    for target in document["targets"]:
        for profile in document["profiles"]:
            for scene in document["scenes"]:
                for threads in document.get("threads", [1]):
                    params = tuple(sorted((str(k), str(v)) for k, v in scene.get("params", {}).items()))
                    cell = Cell(label=target["label"], backend=target["backend"], variant=target.get("variant", ""), profile=profile,
                                scenario=scene["scenario"], params=params, threads=int(threads), frames=int(scene.get("frames") or 0),
                                target_args=tuple(target.get("bench_args", [])), scene_args=tuple(scene.get("bench_args", [])))
                    if any(_matches(rule, cell) for rule in excludes):
                        continue
                    cells.append(cell)
    return cells


def plan_blocks(cells: list[Cell], base_seed: int, repeats: int) -> list[PlannedRun]:
    """Block r = every cell once, shuffled with block_seed(base_seed, r) (02 12.5 step 4)."""
    planned = []
    for repeat in range(repeats):
        seed = common.block_seed(base_seed, repeat)
        for order, cell in enumerate(common.seeded_shuffle(cells, seed)):
            planned.append(PlannedRun(repeat=repeat, order=order, block_seed=seed, cell=cell))
    return planned
