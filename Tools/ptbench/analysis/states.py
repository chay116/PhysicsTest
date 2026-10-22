"""states - the behaviour-run inputs of the analysis: states.ptst (PTST v1), states.dtype.json, scene_desc.json and pose math
(docs/ResultsSchema.md 1 and 6; docs/Scenes.md 1 "거동 run 출력"; 03_WorkPlan M9 task 5).

A behaviour run (-StatesEvery=N) writes, before frame 0, scene_desc.json (physicstest.scene_desc/1: the materials, bodies,
shapes, masses and joints exactly as the backend received them) and, between frames, states.ptst: a 64 B header, then per
recorded frame a 16 B frame header {u32 frame, u32 step, f64 sim_time} and one 112 B record per body in creation order
(f64 position[3] = actor origin cm, f64 rotation[4] = quaternion x y z w, f64 linear_velocity[3] cm/s (the COM of a dynamic
body), f64 angular_velocity[3] rad/s, u8 object_state 1 Sleeping / 2 Kinematic / 3 Static / 4 Dynamic, 7 pad bytes).
The state of frame f is the state after step f + 1 (docs/Progress.md M9 준비 P-9); the frame header carries the step count.

read_states() checks the layout against the constants of the format (and against states.dtype.json when it is there), the
frame count against the file size, and the desc_sha256 prefix against the manifest; a file whose writer did not close it
(header flag bit 0 clear) is read up to its last complete frame and marked incomplete. Nothing here interprets physics.
Quaternions follow UE's convention (x, y, z, w; v' = q v q*), poses are actor origins, a shape's world pose is actor pose x
shape local pose and a body's COM is actor pose x com_local.translation (02_Architecture 5).
"""
from __future__ import annotations

import dataclasses
import json
import pathlib
from typing import Any

import numpy as np

PTST_MAGIC = b"PTST"
PTST_VERSION = 1
HEADER_SIZE = 64
FRAME_HEADER_SIZE = 16
RECORD_STRIDE = 112
UNITS_CM = 1

HEADER_DTYPE = np.dtype([("magic", "S4"), ("version", "<u4"), ("flags", "<u4"), ("body_count", "<u4"), ("frame_count", "<u4"),
                         ("record_stride", "<u4"), ("units", "<u4"), ("frame_header_size", "<u4"), ("dt", "<f8"),
                         ("scene_sha256_prefix", "V8"), ("desc_sha256_prefix", "V8"), ("reserved", "<u8")])
BODY_DTYPE = np.dtype([("position", "<f8", (3,)), ("rotation_xyzw", "<f8", (4,)), ("linear_velocity", "<f8", (3,)),
                       ("angular_velocity", "<f8", (3,)), ("object_state", "u1"), ("pad", "V7")])
assert HEADER_DTYPE.itemsize == HEADER_SIZE and BODY_DTYPE.itemsize == RECORD_STRIDE

SLEEPING, KINEMATIC, STATIC, DYNAMIC = 1, 2, 3, 4
OBJECT_STATE_NAMES = {SLEEPING: "Sleeping", KINEMATIC: "Kinematic", STATIC: "Static", DYNAMIC: "Dynamic"}


class StatesError(ValueError):
    """states.ptst or scene_desc.json cannot be used (wrong magic / version / layout, truncated, another scene)."""


@dataclasses.dataclass
class States:
    """Every recorded frame of one run. Arrays are indexed [recorded frame, body]."""
    path: pathlib.Path
    dt: float                     # dt_engine of the run (header)
    body_count: int
    frame: np.ndarray             # [F] frame index (0-based)
    step: np.ndarray              # [F] steps taken after that frame (frame f -> step f + 1 for FixedStep)
    sim_time: np.ndarray          # [F] s
    position: np.ndarray          # [F, B, 3] actor origin, cm
    rotation: np.ndarray          # [F, B, 4] quaternion x y z w
    linear_velocity: np.ndarray   # [F, B, 3] cm/s (COM of a dynamic body)
    angular_velocity: np.ndarray  # [F, B, 3] rad/s
    object_state: np.ndarray      # [F, B] uint8
    complete: bool                # header flag bit 0: the writer closed the file (frame_count final)
    scene_prefix: str
    desc_prefix: str

    @property
    def num_frames(self) -> int:
        return int(self.frame.shape[0])

    def rows(self, first: int, last: int) -> np.ndarray:
        """Indices of the recorded frames inside [first, last] (frame numbers, inclusive)."""
        return np.nonzero((self.frame >= first) & (self.frame <= last))[0]

    def row_of(self, frame: int) -> int | None:
        hits = np.nonzero(self.frame == frame)[0]
        return int(hits[0]) if hits.size else None


def _check_dtype_json(path: pathlib.Path, body_count: int) -> None:
    """states.dtype.json, when present, must describe the same layout this reader assumes."""
    if not path.is_file():
        return
    with open(path, encoding="utf-8") as stream:
        document = json.load(stream)
    if document.get("schema") != "physicstest.states_dtype/1":
        raise StatesError(f"{path}: schema {document.get('schema')!r}, expected physicstest.states_dtype/1")
    body = [(entry[0], entry[1], tuple(entry[2]) if len(entry) > 2 else ()) for entry in document.get("body", [])]
    expected = [("position", "<f8", (3,)), ("rotation_xyzw", "<f8", (4,)), ("linear_velocity", "<f8", (3,)), ("angular_velocity", "<f8", (3,)),
                ("object_state", "u1", ()), ("pad", "V7", ())]
    if body != expected:
        raise StatesError(f"{path}: body layout {body} differs from the PTST v1 record this reader knows")
    if int(document.get("header_size", -1)) != HEADER_SIZE or int(document.get("frame_header_size", -1)) != FRAME_HEADER_SIZE \
            or int(document.get("record_stride", -1)) != RECORD_STRIDE:
        raise StatesError(f"{path}: header / frame header / record sizes differ from PTST v1")
    if int(document.get("body_count", body_count)) != body_count:
        raise StatesError(f"{path}: body_count {document.get('body_count')} != states.ptst {body_count}")


def read_states(run_dir: pathlib.Path | str, manifest: dict[str, Any] | None = None) -> States:
    run_dir = pathlib.Path(run_dir)
    path = run_dir / "states.ptst"
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise StatesError(f"{path}: {error}") from None
    if len(raw) < HEADER_SIZE:
        raise StatesError(f"{path}: {len(raw)} bytes, shorter than the 64 B header")
    header = np.frombuffer(raw, dtype=HEADER_DTYPE, count=1)[0]
    if bytes(header["magic"]) != PTST_MAGIC:
        raise StatesError(f"{path}: magic {bytes(header['magic'])!r}, expected b'PTST'")
    if int(header["version"]) != PTST_VERSION:
        raise StatesError(f"{path}: PTST version {int(header['version'])}, this reader knows 1")
    if int(header["record_stride"]) != RECORD_STRIDE or int(header["frame_header_size"]) != FRAME_HEADER_SIZE:
        raise StatesError(f"{path}: record stride {int(header['record_stride'])} / frame header {int(header['frame_header_size'])}, expected 112 / 16")
    if int(header["units"]) != UNITS_CM:
        raise StatesError(f"{path}: units {int(header['units'])}, expected 1 (cm, kg, s, rad)")
    bodies = int(header["body_count"])
    _check_dtype_json(run_dir / "states.dtype.json", bodies)
    frame_bytes = FRAME_HEADER_SIZE + RECORD_STRIDE * bodies
    available = (len(raw) - HEADER_SIZE) // frame_bytes
    complete = bool(int(header["flags"]) & 1)
    count = int(header["frame_count"]) if complete else available
    if count > available:
        raise StatesError(f"{path}: header says {count} frames, the file holds {available} (truncated)")
    frame_dtype = np.dtype([("frame", "<u4"), ("step", "<u4"), ("sim_time", "<f8"), ("bodies", BODY_DTYPE, (bodies,))])
    assert frame_dtype.itemsize == frame_bytes
    blocks = np.frombuffer(raw, dtype=frame_dtype, offset=HEADER_SIZE, count=count)
    data = blocks["bodies"]
    desc_prefix = bytes(header["desc_sha256_prefix"]).hex()
    scene_prefix = bytes(header["scene_sha256_prefix"]).hex()
    if manifest is not None:
        desc = ((manifest.get("scene") or {}).get("desc_sha256")) or ""
        if desc and not desc.lower().startswith(desc_prefix):
            raise StatesError(f"{path}: desc_sha256 prefix {desc_prefix} does not match the manifest's {desc[:16]}")
    return States(path=path, dt=float(header["dt"]), body_count=bodies, frame=blocks["frame"].astype(np.int64), step=blocks["step"].astype(np.int64),
                  sim_time=blocks["sim_time"].astype(float), position=np.array(data["position"]), rotation=np.array(data["rotation_xyzw"]),
                  linear_velocity=np.array(data["linear_velocity"]), angular_velocity=np.array(data["angular_velocity"]),
                  object_state=np.array(data["object_state"]), complete=complete, scene_prefix=scene_prefix, desc_prefix=desc_prefix)


def read_scene_desc(run_dir: pathlib.Path | str) -> dict[str, Any]:
    path = pathlib.Path(run_dir) / "scene_desc.json"
    try:
        with open(path, encoding="utf-8") as stream:
            document = json.load(stream)
    except (OSError, ValueError) as error:
        raise StatesError(f"{path}: {error}") from None
    if document.get("schema") != "physicstest.scene_desc/1":
        raise StatesError(f"{path}: schema {document.get('schema')!r}, expected physicstest.scene_desc/1")
    return document


# ---- quaternions and poses (x, y, z, w; UE: v' = q v q*) -------------------------------------------------------------------

def quat_rotate(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Rotate v [..., 3] by the unit quaternion q [..., 4] (broadcasting): v + 2 w (u x v) + 2 u x (u x v)."""
    q = np.asarray(q, dtype=float)
    v = np.asarray(v, dtype=float)
    u = q[..., :3]
    w = q[..., 3:4]
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def quat_conjugate(q: np.ndarray) -> np.ndarray:
    q = np.asarray(q, dtype=float)
    return np.concatenate([-q[..., :3], q[..., 3:4]], axis=-1)


def quat_multiply(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """a * b (apply b first, then a), broadcasting."""
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    ax, ay, az, aw = (a[..., i] for i in range(4))
    bx, by, bz, bw = (b[..., i] for i in range(4))
    return np.stack([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz], axis=-1)


def quat_to_matrix(q: np.ndarray) -> np.ndarray:
    """[..., 3, 3] rotation matrices; column i is the rotated local axis i."""
    q = np.asarray(q, dtype=float)
    eye = np.eye(3)
    return np.stack([quat_rotate(q, eye[i]) for i in range(3)], axis=-1)


def pose_apply(q: np.ndarray, t: np.ndarray, local: np.ndarray) -> np.ndarray:
    """A point given in a frame (q, t) -> world: t + q local."""
    return np.asarray(t, dtype=float) + quat_rotate(q, local)


def pose_compose(q1: np.ndarray, t1: np.ndarray, q2: np.ndarray, t2: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(q1, t1) x (q2, t2): the pose of frame 2 given in frame 1, in world."""
    return quat_multiply(q1, q2), pose_apply(q1, t1, t2)


def tilt_deg(q: np.ndarray, axis: np.ndarray | None = None) -> np.ndarray:
    """Angle between the rotated local axis (default +Z) and world +Z, in degrees."""
    axis = np.array([0.0, 0.0, 1.0]) if axis is None else np.asarray(axis, dtype=float)
    z = quat_rotate(q, axis)[..., 2]
    return np.degrees(np.arccos(np.clip(z, -1.0, 1.0)))


def rotation_angle_deg(qa: np.ndarray, qb: np.ndarray) -> np.ndarray:
    """The angle of the rotation that takes qa to qb (degrees, sign of q ignored)."""
    dot = np.abs(np.sum(np.asarray(qa, dtype=float) * np.asarray(qb, dtype=float), axis=-1))
    return np.degrees(2.0 * np.arccos(np.clip(dot, -1.0, 1.0)))


# ---- scene_desc helpers ------------------------------------------------------------------------------------------------------

def pose_of(entry: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    return np.array(entry["rotation"], dtype=float), np.array(entry["translation"], dtype=float)


def dynamic_bodies(desc: dict[str, Any]) -> list[int]:
    return [b["index"] for b in desc["bodies"] if b.get("motion") == "dynamic"]


def static_bodies(desc: dict[str, Any]) -> list[int]:
    return [b["index"] for b in desc["bodies"] if b.get("motion") == "static"]


def gravity(desc: dict[str, Any]) -> np.ndarray:
    return np.array(desc.get("gravity") or [0.0, 0.0, -980.0], dtype=float)


def body_com(desc_body: dict[str, Any], rotation: np.ndarray, position: np.ndarray) -> np.ndarray:
    """World COM of a body over frames: actor pose x com_local.translation."""
    _, com_t = pose_of(desc_body["com_local"])
    return pose_apply(rotation, position, com_t)


def shape_world_pose(shape: dict[str, Any], rotation: np.ndarray, position: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    lq, lt = pose_of(shape["local"])
    return pose_compose(rotation, position, lq, lt)


def initial_arrays(desc: dict[str, Any]) -> tuple[np.ndarray, np.ndarray]:
    """[B, 4] rotations and [B, 3] translations of every body's initial pose (static bodies keep it)."""
    q = np.array([b["initial_pose"]["rotation"] for b in desc["bodies"]], dtype=float)
    t = np.array([b["initial_pose"]["translation"] for b in desc["bodies"]], dtype=float)
    return q, t


def body_rotations(desc: dict[str, Any], states: States) -> tuple[np.ndarray, np.ndarray]:
    """Poses of every body over the recorded frames. A static body's record is its pose too; when a record of a static body
    is all zero (no write) the scene_desc pose is used."""
    rotation = states.rotation.copy()
    position = states.position.copy()
    q0, t0 = initial_arrays(desc)
    for index in static_bodies(desc):
        if index < states.body_count and not np.any(rotation[:, index]):
            rotation[:, index] = q0[index]
            position[:, index] = t0[index]
    return rotation, position


def kinetic_and_potential_energy(desc: dict[str, Any], states: States) -> dict[str, np.ndarray]:
    """Per frame: translational KE (1/2 m v_com^2), rotational KE (1/2 w_p^T diag(I) w_p in the principal frame,
    com_local.rotation), potential energy (-m g . com) of every dynamic body, summed. Units kg cm^2 / s^2."""
    g = gravity(desc)
    rotation, position = body_rotations(desc, states)
    ke_lin = np.zeros(states.num_frames)
    ke_rot = np.zeros(states.num_frames)
    pe = np.zeros(states.num_frames)
    for index in dynamic_bodies(desc):
        body = desc["bodies"][index]
        mass = float(body["mass"])
        inertia = np.array(body["inertia_diagonal"], dtype=float)
        com_q, _ = pose_of(body["com_local"])
        com = body_com(body, rotation[:, index], position[:, index])
        v = states.linear_velocity[:, index]
        w = states.angular_velocity[:, index]
        principal = quat_multiply(rotation[:, index], com_q)
        w_p = quat_rotate(quat_conjugate(principal), w)
        ke_lin += 0.5 * mass * np.sum(v * v, axis=-1)
        ke_rot += 0.5 * np.sum(inertia * w_p * w_p, axis=-1)
        pe += -mass * (com @ g)
    return {"ke_lin": ke_lin, "ke_rot": ke_rot, "pe": pe, "total": ke_lin + ke_rot + pe}
