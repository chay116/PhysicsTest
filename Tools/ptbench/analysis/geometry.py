"""geometry - penetration from the harness geometry (03_WorkPlan M9 "하네스 기하 관통 (sphere-plane, box corner, SAT)").

The depth is computed from scene_desc.json (shapes as the backend received them) and the recorded body poses, never from
an engine's contact data: every engine is measured with the same ruler. Positive = overlap depth (cm), negative = gap.
  sphere_plane       r - (n . c - d)
  sphere_obb         r - signed distance from the centre to the box (inside: minus the distance to the nearest face)
  box_plane          the deepest of the 8 corners below the plane ("box corner")
  capsule_obb        the deepest of points sampled along the segment, as spheres (exact against a face: the depth along
                     a segment is linear while one face is the nearest feature, so the extreme is an end point)
  obb_obb_sat        separating-axis test over the 15 axes (3 + 3 face normals, 9 edge cross products): the smallest
                     overlap. Positive = the minimum translation depth; negative = separated by at least that much
Arrays broadcast over a leading frame axis: centres [..., 3], axes [..., 3, 3] (column i = local axis i in world), half
extents [3].
"""
from __future__ import annotations

from typing import Any

import numpy as np

from . import states as st

PARALLEL_EPS = 1e-9


def obb(shape: dict[str, Any], rotation: np.ndarray, position: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """(centre [..., 3], axes [..., 3, 3], half extent [3]) of a box shape at the given actor poses."""
    q, t = st.shape_world_pose(shape, rotation, position)
    return t, st.quat_to_matrix(q), np.array(shape["half_extent"], dtype=float)


def top_face_plane(center: np.ndarray, axes: np.ndarray, half: np.ndarray) -> tuple[np.ndarray, float]:
    """The plane (unit normal n, offset d: n . x = d) of the box face whose normal points most along +Z (a static ground,
    pad or incline), from a single pose."""
    center = np.asarray(center, dtype=float).reshape(3)
    axes = np.asarray(axes, dtype=float).reshape(3, 3)
    z_components = axes[2, :]
    i = int(np.argmax(np.abs(z_components)))
    n = axes[:, i] * (1.0 if z_components[i] >= 0 else -1.0)
    return n, float(n @ center + half[i])


def sphere_plane(center: np.ndarray, radius: float, n: np.ndarray, d: float) -> np.ndarray:
    return radius - (np.asarray(center, dtype=float) @ np.asarray(n, dtype=float) - d)


def sphere_obb(center: np.ndarray, radius: float, box_center: np.ndarray, box_axes: np.ndarray, box_half: np.ndarray) -> np.ndarray:
    local = np.einsum("...ji,...j->...i", box_axes, np.asarray(center, dtype=float) - box_center)
    q = np.abs(local) - box_half
    outside = np.linalg.norm(np.maximum(q, 0.0), axis=-1)
    inside = np.minimum(np.max(q, axis=-1), 0.0)
    return radius - (outside + inside)


def box_corners(center: np.ndarray, axes: np.ndarray, half: np.ndarray) -> np.ndarray:
    """[..., 8, 3] corners."""
    signs = np.array([[sx, sy, sz] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)], dtype=float)
    offsets = np.einsum("...ij,kj->...ki", axes, signs * half)
    return np.asarray(center, dtype=float)[..., None, :] + offsets


def box_plane(center: np.ndarray, axes: np.ndarray, half: np.ndarray, n: np.ndarray, d: float) -> np.ndarray:
    """Box corner: depth of the deepest corner below the plane (negative = the lowest corner's gap)."""
    corners = box_corners(center, axes, half)
    return -(np.min(corners @ np.asarray(n, dtype=float), axis=-1) - d)


def capsule_obb(p0: np.ndarray, p1: np.ndarray, radius: float, box_center: np.ndarray, box_axes: np.ndarray, box_half: np.ndarray,
                samples: int = 5) -> np.ndarray:
    best = None
    for s in np.linspace(0.0, 1.0, samples):
        point = np.asarray(p0, dtype=float) * (1.0 - s) + np.asarray(p1, dtype=float) * s
        depth = sphere_obb(point, radius, box_center, box_axes, box_half)
        best = depth if best is None else np.maximum(best, depth)
    return best


def obb_obb_sat(ca: np.ndarray, aa: np.ndarray, ha: np.ndarray, cb: np.ndarray, ab: np.ndarray, hb: np.ndarray) -> np.ndarray:
    """Smallest overlap over the 15 SAT axes (positive = penetration depth, negative = separation)."""
    ca, aa, cb, ab = (np.asarray(x, dtype=float) for x in (ca, aa, cb, ab))
    shape = np.broadcast_shapes(ca.shape[:-1], cb.shape[:-1])
    ca = np.broadcast_to(ca, shape + (3,))
    cb = np.broadcast_to(cb, shape + (3,))
    aa = np.broadcast_to(aa, shape + (3, 3))
    ab = np.broadcast_to(ab, shape + (3, 3))
    t = cb - ca
    axes = [aa[..., :, i] for i in range(3)] + [ab[..., :, j] for j in range(3)]
    axes += [np.cross(aa[..., :, i], ab[..., :, j]) for i in range(3) for j in range(3)]
    best = np.full(shape, np.inf)
    for axis in axes:
        norm = np.linalg.norm(axis, axis=-1)
        valid = norm > PARALLEL_EPS
        unit = axis / np.where(valid, norm, 1.0)[..., None]
        ra = np.sum(ha * np.abs(np.einsum("...ji,...j->...i", aa, unit)), axis=-1)
        rb = np.sum(hb * np.abs(np.einsum("...ji,...j->...i", ab, unit)), axis=-1)
        overlap = ra + rb - np.abs(np.sum(t * unit, axis=-1))
        best = np.where(valid, np.minimum(best, overlap), best)
    return best


def capsule_segment(shape: dict[str, Any], rotation: np.ndarray, position: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    """End points of a capsule's segment (local Z, half_length) and its radius."""
    q, t = st.shape_world_pose(shape, rotation, position)
    axis = st.quat_rotate(q, np.array([0.0, 0.0, 1.0]))
    half = float(shape.get("half_length", 0.0))
    return t - axis * half, t + axis * half, float(shape["radius"])


def bounding_radius(shape: dict[str, Any]) -> float:
    kind = shape.get("type")
    local_offset = float(np.linalg.norm(np.array(shape["local"]["translation"], dtype=float)))
    if kind == "sphere":
        return local_offset + float(shape["radius"])
    if kind == "box":
        return local_offset + float(np.linalg.norm(np.array(shape["half_extent"], dtype=float)))
    if kind == "capsule":
        return local_offset + float(shape["radius"]) + float(shape.get("half_length", 0.0))
    return np.inf


def shape_vs_static_box(shape: dict[str, Any], rotation: np.ndarray, position: np.ndarray, static_shape: dict[str, Any],
                        static_rotation: np.ndarray, static_position: np.ndarray) -> tuple[np.ndarray, str]:
    """Depth of a dynamic shape (over frames) against one static box shape (one pose). Returns (depth [F], method)."""
    sc, sa, sh = obb(static_shape, static_rotation, static_position)
    kind = shape.get("type")
    if kind == "sphere":
        q, t = st.shape_world_pose(shape, rotation, position)
        return sphere_obb(t, float(shape["radius"]), sc, sa, sh), "sphere-box"
    if kind == "box":
        c, a, h = obb(shape, rotation, position)
        return obb_obb_sat(c, a, h, sc, sa, sh), "SAT"
    if kind == "capsule":
        p0, p1, radius = capsule_segment(shape, rotation, position)
        return capsule_obb(p0, p1, radius, sc, sa, sh), "capsule-box"
    raise ValueError(f"no penetration test for a {kind} shape")


def shape_vs_plane(shape: dict[str, Any], rotation: np.ndarray, position: np.ndarray, n: np.ndarray, d: float) -> tuple[np.ndarray, str]:
    """Depth against the infinite plane of a static box's top face (ground, pad): sphere-plane, box corner, capsule ends."""
    kind = shape.get("type")
    if kind == "sphere":
        q, t = st.shape_world_pose(shape, rotation, position)
        return sphere_plane(t, float(shape["radius"]), n, d), "sphere-plane"
    if kind == "box":
        c, a, h = obb(shape, rotation, position)
        return box_plane(c, a, h, n, d), "box corner"
    if kind == "capsule":
        p0, p1, radius = capsule_segment(shape, rotation, position)
        return np.maximum(sphere_plane(p0, radius, n, d), sphere_plane(p1, radius, n, d)), "capsule-plane"
    raise ValueError(f"no plane test for a {kind} shape")


def shape_vs_shape(shape_a: dict[str, Any], qa: np.ndarray, ta: np.ndarray, shape_b: dict[str, Any], qb: np.ndarray, tb: np.ndarray) -> np.ndarray | None:
    """Depth between two dynamic shapes over frames (box-box SAT, sphere-sphere, sphere-box); None for other pairs."""
    kinds = (shape_a.get("type"), shape_b.get("type"))
    if kinds == ("box", "box"):
        return obb_obb_sat(*obb(shape_a, qa, ta), *obb(shape_b, qb, tb))
    if kinds == ("sphere", "sphere"):
        _, ca = st.shape_world_pose(shape_a, qa, ta)
        _, cb = st.shape_world_pose(shape_b, qb, tb)
        return float(shape_a["radius"]) + float(shape_b["radius"]) - np.linalg.norm(ca - cb, axis=-1)
    if kinds in (("sphere", "box"), ("box", "sphere")):
        sphere, sq, stt, box, bq, bt = (shape_a, qa, ta, shape_b, qb, tb) if kinds[0] == "sphere" else (shape_b, qb, tb, shape_a, qa, ta)
        _, c = st.shape_world_pose(sphere, sq, stt)
        return sphere_obb(c, float(sphere["radius"]), *obb(box, bq, bt))
    return None
