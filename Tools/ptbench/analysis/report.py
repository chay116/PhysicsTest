"""report - report/<date>/index.md + PNG for a session (02_Architecture 13 "report"; 03_WorkPlan M8 task 4).

    python -I -m ptbench report <session dir> [--out <dir>] [--window headline]

The report goes to <session dir>/report/<YYYY-MM-DD>/ by default (Saved/ is not committed). Its head says that every number is
UNOFFICIAL and, when the power plan is not High performance / Ultimate, says so in bold together with the environment-gate
bypasses (03 M8: "전원 계획이 High performance가 아니면 그 사실이 리포트 머리에 표시된다"; TimingContract 5.2). The head is built
from every input, not only from one session.json: the power plan of every session's gate and of every run's manifest
(platform.power_scheme_high_performance), so a report over run directories, -Gate=G8 directories or several sessions, and
a plan switched during a session, are marked too; the bypasses of all sessions are listed. The same condition (UNOFFICIAL,
the plan when it is not High performance, the bypasses: conditions()) is repeated under every table heading and above the
title of every figure, so a table or a PNG taken out of the report still carries it (03 M8 "우회한 세션은 모든 표에
표시된다"). One table per scene x profile x flag profile x T (02 13; the comparability key is M10): per engine series n,
median, p95, p99, CV and the run range; a table that fails validate.check_table (mixed flag profiles or desc_sha256) is
shown as refused, never computed (also not in the per-cell CV list), and a table whose runs come from several builds
carries a note (validate.table_notes). A series without a value in the chosen window (--window impact on a scene that
declares only a headline window) is written as such and left out of the figures.
Figures: per table the run values (bars = median, dots = runs), per M3.BoxPile table the per-frame median / IQR, and the
N-scaling of each series. CV is reported only: "CV < 2 %" is a target until the M10 gate.
M9: when the inputs hold behaviour runs (run.mode Behavior) or runs the bench refused as unsupported, section 5 summarises them
in one line each (they never enter a timing table) and section 7 is the behaviour analysis (analysis/behavior.py: criteria,
tagged cross-engine differences, the M1 UEParity vs Normalized table, scene tables, chaotic sensitivity, variant knobs, the
tolerance and rule registry), with its PNGs, differences.csv and behavior.json next to index.md.
"""
from __future__ import annotations

import datetime as _dt
import json
import math
import pathlib
from typing import Any

import pandas as pd

from .. import common
from . import load as load_module
from . import perf, validate

# Eight categorical series colours (blue, orange, green, amber, pink, dark green, violet, red), chosen to stay apart from
# each other on the light background below; assigned to the series in a fixed order so a label keeps its colour in every
# figure of one report.
SERIES_COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
TEXT_WARNING = "#b42318"   # the condition line of every figure (UNOFFICIAL, power plan, gate bypasses)
GRID = "#e4e3df"


def _series_order(loaded: load_module.Loaded, cells: pd.DataFrame) -> list[str]:
    order: list[str] = []
    for session in loaded.sessions.values():
        for cell in session.get("cells", []):
            if cell["label"] not in order:
                order.append(cell["label"])
    for label in sorted(cells["label"].unique()) if not cells.empty else []:
        if label not in order:
            order.append(label)
    return order


def short_title(table: str) -> str:
    """"scene=M3.BoxPile[N=1000] | profile=Normalized | flag_profile=Shipped | T=1" -> "M3.BoxPile[N=1000] · Normalized · Shipped · T1"."""
    parts = dict(part.split("=", 1) for part in table.split(" | ") if "=" in part)
    if not parts:
        return table
    return " · ".join(filter(None, [parts.get("scene"), parts.get("profile"), parts.get("flag_profile"), f"T{parts['T']}" if "T" in parts else ""]))


def _unit(values: list[float]) -> tuple[float, str]:
    finite = [v for v in values if v is not None and not math.isnan(v)]
    top = max(finite) if finite else 0.0
    return (1e6, "ms") if top >= 1e5 else (1e3, "µs")


def _style_axes(ax) -> None:
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=9)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def _figure(width: float, height: float):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(width, height), dpi=110)
    fig.patch.set_facecolor(SURFACE)
    _style_axes(ax)
    return fig, ax, plt


def _finite(value: Any) -> bool:
    try:
        return value is not None and value is not pd.NA and math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def _titles(fig, ax, title: str, condition: str) -> None:
    """The figure's title, and above it the condition every figure carries (UNOFFICIAL, the power plan when it is not High
    performance, the gate bypasses), so a PNG viewed on its own still says under which condition it was measured."""
    ax.set_title(title, color=TEXT_PRIMARY, fontsize=10, loc="left")
    if condition:
        fig.suptitle(condition, x=0.01, y=0.995, ha="left", va="top", color=TEXT_WARNING, fontsize=8.5, fontweight="bold")


def plot_table(table_cells: pd.DataFrame, colors: dict[str, str], title: str, path: pathlib.Path, condition: str = "") -> bool:
    """Bars = median of the runs, dots = runs. Rows without a finite median (no run value in the window) are left out;
    returns False (no figure written) when no row is left."""
    rows = table_cells.sort_values("label", key=lambda s: s.map(lambda v: list(colors).index(v) if v in colors else 99)).to_dict("records")
    rows = [dict(r, values_ns=[v for v in r["values_ns"] if _finite(v)]) for r in rows if _finite(r["median_ns"])]
    if not rows:
        return False
    scale, unit = _unit([r["median_ns"] for r in rows] + [v for r in rows for v in r["values_ns"]])
    fig, ax, plt = _figure(7.2, 0.62 * len(rows) + 1.55)
    ax.grid(True, axis="x", color=GRID, linewidth=0.8)
    ax.grid(False, axis="y")
    for index, row in enumerate(rows):
        y = len(rows) - 1 - index
        color = colors.get(row["label"], TEXT_SECONDARY)
        ax.barh(y, row["median_ns"] / scale, height=0.42, color=color, edgecolor=SURFACE, linewidth=2)
        for value in row["values_ns"]:
            ax.plot(value / scale, y, "o", markersize=4.5, markerfacecolor=SURFACE, markeredgecolor=TEXT_PRIMARY, markeredgewidth=1.0)
        ax.text(row["median_ns"] / scale, y + 0.3, f" {row['median_ns'] / scale:.3g} {unit}", color=TEXT_PRIMARY, fontsize=8.5, va="bottom")
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([r["label"] for r in reversed(rows)], color=TEXT_PRIMARY, fontsize=9)
    ax.set_xlabel(f"T_frame window mean per run ({unit}); bar = median of the runs, dots = runs", color=TEXT_SECONDARY, fontsize=9)
    top = max([r["median_ns"] for r in rows] + [v for r in rows for v in r["values_ns"]]) / scale
    ax.set_xlim(0, top * 1.18)
    _titles(fig, ax, title, condition)
    fig.tight_layout()
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)
    return True


def plot_series(series: dict[str, pd.DataFrame], colors: dict[str, str], title: str, path: pathlib.Path, condition: str = "") -> bool:
    """Per-frame median and IQR band of each series; returns False (no figure) when no series has a frame."""
    series = {label: frame for label, frame in series.items() if not frame.empty}
    values = [v for frame in series.values() for v in frame["q75_ns"].tolist() if _finite(v)]
    if not values:
        return False
    scale, unit = _unit(values)
    fig, ax, plt = _figure(7.6, 3.8)
    for label, frame in series.items():
        color = colors.get(label, TEXT_SECONDARY)
        ax.fill_between(frame["frame"], frame["q25_ns"] / scale, frame["q75_ns"] / scale, color=color, alpha=0.18, linewidth=0)
        ax.plot(frame["frame"], frame["median_ns"] / scale, color=color, linewidth=1.6, label=label)
    ax.set_xlabel("frame", color=TEXT_SECONDARY, fontsize=9)
    ax.set_ylabel(f"frame_ns ({unit}): median, IQR band", color=TEXT_SECONDARY, fontsize=9)
    top = max([v / scale for v in values] or [1.0])
    ax.set_ylim(0, top * 1.28)   # headroom: the legend sits above the data, never on it
    legend = ax.legend(frameon=False, fontsize=8.5, loc="upper left", ncol=min(4, max(1, len(series))))
    for text in legend.get_texts():
        text.set_color(TEXT_PRIMARY)
    _titles(fig, ax, title, condition)
    fig.tight_layout()
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)
    return True


def plot_scaling(alpha: pd.DataFrame, colors: dict[str, str], path: pathlib.Path, condition: str = "") -> None:
    fig, ax, plt = _figure(6.4, 4.0)
    order = list(colors)
    for row in sorted(alpha.to_dict("records"), key=lambda r: order.index(r["label"]) if r["label"] in order else 99):
        color = colors.get(row["label"], TEXT_SECONDARY)
        n = row["N"]
        t = [v / 1e6 for v in row["median_ns"]]
        ax.plot(n, t, "-o", color=color, linewidth=2, markersize=8, markeredgecolor=SURFACE, markeredgewidth=2,
                label=f"{row['label']} {row['scenario']} (α {row['alpha']:.2f})")
    ax.set_xscale("log")
    ax.set_yscale("log")
    from matplotlib.ticker import NullFormatter, ScalarFormatter

    ticks = sorted({int(n) for row in alpha.to_dict("records") for n in row["N"]})
    ax.set_xticks(ticks)
    ax.xaxis.set_major_formatter(ScalarFormatter())
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.set_xlabel("N (dynamic bodies)", color=TEXT_SECONDARY, fontsize=9)
    ax.set_ylabel("median T_frame (ms)", color=TEXT_SECONDARY, fontsize=9)
    legend = ax.legend(frameon=False, fontsize=8)
    for text in legend.get_texts():
        text.set_color(TEXT_PRIMARY)
    _titles(fig, ax, "N-scaling: t = c N^α (log-log)", condition)
    fig.tight_layout()
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)


def _esc(text: Any) -> str:
    """Markdown table cell text: a '|' inside a cell id would split the cell."""
    return str(text).replace("|", r"\|")


def _fmt(value: Any) -> str:
    return perf.format_ns(value)


def _cv(value: Any) -> str:
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "-"
    return f"{value:.2f}%" + (" ⚠" if value >= 2.0 else "")


def _scheme_text(name: Any, kind: Any = None, guid: Any = None) -> str:
    details = ", ".join(str(v) for v in (kind, guid) if v)
    return f"{name or '?'}" + (f" ({details})" if details else "")


def _missing(value: Any) -> bool:
    return value is None or value is pd.NA or (isinstance(value, float) and math.isnan(value))


def power_facts(sessions: dict[str, dict[str, Any]], runs: pd.DataFrame) -> dict[str, Any]:
    """The power plan over every input: the gate of each session and platform.power_scheme* of each run's manifest.
    {"known", "not_high_performance", "schemes": distinct descriptions, "runs_not_hp", "runs_with_plan"}."""
    from .. import envcheck

    schemes: list[str] = []
    kinds_not_hp: list[str] = []   # the plan kinds (GUID-based, not localised: "Balanced", "custom") that are not High performance
    not_hp = False
    known = False
    for document in sessions.values():
        env = document.get("environment") or {}
        scheme = env.get("power_scheme") or {}
        flag = env.get("power_plan_high_performance")
        if flag is not None:
            known = True
            not_hp = not_hp or flag is False
            text = _scheme_text(scheme.get("name"), scheme.get("kind"), scheme.get("guid"))
            if text not in schemes:
                schemes.append(text)
            if flag is False and (scheme.get("kind") or "unknown") not in kinds_not_hp:
                kinds_not_hp.append(scheme.get("kind") or "unknown")
    runs_not_hp = 0
    runs_with_plan = 0
    if not runs.empty and "power_scheme_high_performance" in runs.columns:
        for row in runs.to_dict("records"):
            flag = row.get("power_scheme_high_performance")
            if _missing(flag):
                continue
            known = True
            runs_with_plan += 1
            if not bool(flag):
                runs_not_hp += 1
                not_hp = True
            guid = row.get("power_scheme_guid")
            guid = None if _missing(guid) else str(guid).strip("{}").lower()   # the manifest writes {381B4222-...}
            kind = envcheck.KNOWN_GUIDS.get(guid, "custom") if guid else None
            text = _scheme_text(None if _missing(row.get("power_scheme")) else row.get("power_scheme"), kind, guid)
            if text not in schemes:
                schemes.append(text)
            if not bool(flag) and (kind or "unknown") not in kinds_not_hp:
                kinds_not_hp.append(kind or "unknown")
    return {"known": known, "not_high_performance": not_hp, "schemes": schemes, "kinds_not_hp": kinds_not_hp, "runs_not_hp": runs_not_hp,
            "runs_with_plan": runs_with_plan}


def conditions(sessions: dict[str, dict[str, Any]], runs: pd.DataFrame) -> dict[str, str]:
    """The measurement condition every table and figure carries (03 M8 "우회한 세션은 모든 표에 표시된다", TimingContract 5.2):
    {"figure": one ASCII line for the PNG titles (matplotlib's default font has no Hangul, so the GUID-based plan kind is
    used, not the localised name), "markdown": one line under every table heading}."""
    power = power_facts(sessions, runs)
    bypasses = _union(document.get("bypasses", []) for document in sessions.values())
    figure = ["UNOFFICIAL"]
    markdown = ["**비공식 (UNOFFICIAL)**"]
    if power["not_high_performance"]:
        kinds = ", ".join(power["kinds_not_hp"]) or "unknown"
        figure.append(f"power plan {kinds}, not High performance")
        markdown.append(f"**전원 계획 {'; '.join(power['schemes'])} — High performance 아님**")
    elif not power["known"]:
        figure.append("power plan unknown")
        markdown.append("**전원 계획을 알 수 없음**")
    if bypasses:
        figure.append("gate bypassed: " + ", ".join(bypasses))
        markdown.append(f"환경 게이트 우회 **`{', '.join(bypasses)}`**")
    return {"figure": " · ".join(figure), "markdown": "> 조건: " + " · ".join(markdown) + "."}


def _union(lists) -> list[str]:
    out: list[str] = []
    for items in lists:
        for item in items or []:
            if item not in out:
                out.append(item)
    return out


def _session_header(sessions: dict[str, dict[str, Any]], runs: pd.DataFrame) -> list[str]:
    """The report head: the power plan (bold when not High performance, from every session and every run), the gate
    bypasses of every session, which inputs the report covers, and UNOFFICIAL with its reasons."""
    documents = list(sessions.values())
    power = power_facts(sessions, runs)
    bypasses = _union(document.get("bypasses", []) for document in documents)
    lines = []
    if power["not_high_performance"]:
        where = []
        if documents:
            where.append("세션의 환경 게이트" if len(documents) == 1 else f"세션 {len(documents)}개의 환경 게이트")
        if power["runs_with_plan"]:
            where.append(f"run {power['runs_not_hp']}/{power['runs_with_plan']}개의 manifest")
        line = f"> **전원 계획이 High performance가 아니다: {'; '.join(power['schemes'])}** ({', '.join(where)} 기준)."
        if len(power["schemes"]) > 1:
            line += f" **입력 안에서 전원 계획이 {len(power['schemes'])}가지다** (세션 도중에 바뀌었거나 여러 세션을 합쳤다)."
        if bypasses:
            line += f" 환경 게이트를 **`{', '.join(bypasses)}`**로 우회했다."
        elif not documents:
            line += " run 디렉터리에는 환경 게이트 기록이 없다."
        line += " Balanced에서는 run 간 노이즈가 크다 (01 Q9). 아래 모든 표가 이 조건의 값이다."
        lines.append(line)
    elif bypasses:
        lines.append(f"> **환경 게이트 우회: `{', '.join(bypasses)}`.** 아래 모든 표가 이 조건의 값이다.")
    if not power["known"]:
        lines.append("> **전원 계획을 알 수 없다** (session.json도 manifest의 `platform.power_scheme_high_performance`도 없다).")
    if not documents:
        lines.append("> 세션 파일 없이 run 디렉터리에서 만든 리포트다. 전원 계획은 각 run의 manifest에서 읽었다.")
    elif len(documents) > 1:
        lines.append(f"> 세션 {len(documents)}개를 합친 리포트다: " + ", ".join(f"`{d.get('session_id')}`" for d in documents) + ".")
    reasons = _union(document.get("unofficial_reasons", []) for document in documents)
    if not documents:
        reasons = ["no conformance stamp (M10)", "no CV gate / claim rules (M10)"]
    if power["not_high_performance"] and not any("power plan" in r for r in reasons):
        reasons.append("power plan is not High performance: " + "; ".join(power["schemes"]))
    lines.append("> **비공식 (UNOFFICIAL)** — " + "; ".join(reasons) + ". CV < 2 %는 목표로만 적는다 (게이트는 M10). "
                 "이 조건은 아래 모든 표의 제목 밑과 모든 그림의 제목 위에도 적는다.")
    # One paragraph per line: adjacent '>' lines would render as one blockquote paragraph.
    separated: list[str] = []
    for line in lines:
        if separated:
            separated.append(">")
        separated.append(line)
    return separated


def _session_rows(session: dict[str, Any], classified: pd.DataFrame, window: str, prefix: str = "") -> list[str]:
    md = []
    matrix = session["matrix"]
    bench = session["bench"]
    env = session.get("environment", {})
    host = env.get("host", {})
    mine = classified[classified["source"] == session.get("session_id")] if not classified.empty and "source" in classified.columns else classified
    flags = sorted({str(v) for v in mine.get("flag_profile", pd.Series(dtype=object)).dropna()})
    presets = sorted({str(v) for v in mine.get("preset", pd.Series(dtype=object)).dropna()})
    summary = session.get("summary", {})
    runner = session.get("runner", {})
    md.append(f"| {prefix}세션 | `{session['session_id']}` ({session['status']}, {session['started_utc']} → {session.get('finished_utc')}, {(session.get('wall_s') or 0) / 60:.1f} 분) |")
    md.append(f"| {prefix}매트릭스 | `{matrix['name']}` ({matrix['purpose']}, R = {matrix['repeats']}, block seed {matrix['block_seed']}, sha256 `{matrix['sha256'][:12]}`) |")
    md.append(f"| {prefix}벤치 exe | `{bench['exe']}` (preset {', '.join(presets) or bench.get('preset')}, flag profile {', '.join(flags) or '-'}, sha256 `{(bench.get('exe_sha256') or '-')[:12]}`) |")
    md.append(f"| {prefix}git | `{(session['git'].get('sha') or '-')[:12]}`{' (dirty)' if session['git'].get('dirty') else ''} |")
    md.append(f"| {prefix}호스트 | {host.get('cpu')}, 논리 CPU {host.get('logical_processors')}, {host.get('os')} |")
    for check in env.get("checks", []):
        state = "통과" if check["pass"] else ("**우회** " + check["bypass_flag"] if check["bypassed"] else "실패")
        md.append(f"| {prefix}환경 게이트: {check['id']} | {state} — {check['detail']} |")
    md.append(f"| {prefix}러너 | 쿨다운 {runner.get('cooldown_s')} s, 타임아웃 {runner.get('timeout_s')} s, 종료 코드 4 재시도 {runner.get('contaminated_retries')}회, "
              f"윈도 `{window}`, 판정 = `exit.json` (원시 종료 코드는 기록만) |")
    md.append(f"| {prefix}run | 계획 {summary.get('planned_runs')}, 시도 {summary.get('attempts')} (재시도 {summary.get('retries')}), 결과 {json.dumps(summary.get('outcomes', {}), ensure_ascii=False)}, "
              f"셀 상태 {json.dumps(summary.get('cells_by_state', {}), ensure_ascii=False)}, exit.json 없음 {summary.get('no_exit_json')}, 타임아웃 {summary.get('timed_out')} |")
    return md


def section_cv(cells: pd.DataFrame, condition: str = "", window: str = "headline") -> list[str]:
    """§3: the CV of every cell. A cell of a refused table is listed as refused, with no number (never computed); a cell
    without a value in the window says so."""
    md = ["## 3. 셀별 CV (M3.BoxPile 5k 포함 모든 셀)", ""]
    if condition:
        md += [condition, ""]
    md += ["| 셀 | n | median | CV | 목표 (< 2 %) |", "|---|---|---|---|---|"]
    if not cells.empty:
        for row in cells.sort_values(["table", "label"]).to_dict("records"):
            name = f"{row['label']} · {row['scene']} · {row['profile']} · {row.get('flag_profile')} · T{row['T']}"
            if row["table_error"]:
                md.append(f"| {_esc(name)} | {row['n_runs']} | - | - | **표 거부** (§2) |")
                continue
            if not _finite(row["median_ns"]):
                md.append(f"| {_esc(name)} | 0 | - | - | 윈도 `{window}`에 값 없음 |")
                continue
            target = "-" if row["cv_pct"] is None or math.isnan(row["cv_pct"]) else ("충족" if row["cv_pct"] < 2.0 else "미달 (보고만)")
            md.append(f"| {_esc(name)} | {row['n_runs']} | {_fmt(row['median_ns'])} | {_cv(row['cv_pct'])} | {target} |")
    md.append("")
    return md


def has_behaviour_runs(classified: pd.DataFrame) -> bool:
    """Whether the inputs hold behaviour runs (run.mode Behavior) or runs refused as unsupported (the joint scenes of a
    behaviour matrix): then the report carries the behaviour section (§7, analysis/behavior.py)."""
    if classified.empty:
        return False
    mode = classified["mode"] if "mode" in classified.columns else pd.Series(dtype=object)
    status = classified["exit_status"] if "exit_status" in classified.columns else pd.Series(dtype=object)
    return bool((mode == "Behavior").any() or (status == "unsupported").any())


def build_report(session_or_inputs, out_dir: pathlib.Path | None = None, window: str | None = None) -> pathlib.Path:
    inputs = [str(session_or_inputs)] if not isinstance(session_or_inputs, (list, tuple)) else [str(p) for p in session_or_inputs]
    loaded = load_module.load(inputs)
    session = loaded.session
    session_windows = sorted({(d.get("runner") or {}).get("window") or "headline" for d in loaded.sessions.values()})
    window = window or (session_windows[0] if len(session_windows) == 1 else "headline")
    base = pathlib.Path(inputs[0])
    date = _dt.date.today().isoformat()
    out_dir = pathlib.Path(out_dir) if out_dir else base / "report" / date
    out_dir.mkdir(parents=True, exist_ok=True)
    classified = validate.classify(loaded.runs) if not loaded.runs.empty else loaded.runs
    cells = perf.cell_stats(classified, loaded.frames, window) if not classified.empty else pd.DataFrame()
    labels = _series_order(loaded, cells) if not cells.empty else []
    colors = {label: SERIES_COLORS[i] for i, label in enumerate(labels[: len(SERIES_COLORS)])}

    title = session.get("session_id") if session else base.name
    condition = conditions(loaded.sessions, loaded.runs)
    md = [f"# 벤치 리포트 — {title} (UNOFFICIAL)", ""]
    md += _session_header(loaded.sessions, loaded.runs)
    md.append("")
    md.append("## 1. 세션")
    md.append("")
    md.append("| 항목 | 값 |")
    md.append("|---|---|")
    documents = list(loaded.sessions.values())
    for index, document in enumerate(documents):
        md += _session_rows(document, classified, window, f"[{index + 1}] " if len(documents) > 1 else "")
    if not documents:
        md.append(f"| 입력 | {', '.join(inputs)} |")
    if not classified.empty:
        for column, what in (("exe_sha256", "exe sha256"), ("git_sha", "git"), ("preset", "preset"), ("power_scheme", "전원 계획 (manifest)")):
            if column in classified.columns:
                values = sorted({str(v) for v in classified[column].dropna()})
                if len(values) > 1 or not documents:
                    shown = ", ".join(f"`{v if column == 'power_scheme' else v[:12]}`" for v in values) or "-"
                    md.append(f"| run의 {what} | {shown}{' (여러 개)' if len(values) > 1 else ''} |")
    accepted = int(classified["accepted"].sum()) if not classified.empty else 0
    md.append(f"| 분석 | run {len(classified)}개 중 {accepted}개 채택, {len(classified) - accepted}개 거부 (§5) |")
    md.append("")

    md.append(f"## 2. 표 (scene × profile × flag profile × T, 윈도 `{window}`)")
    md.append("")
    md.append("값은 run마다 윈도의 `T_frame` 평균이다. median = run 값의 중앙값, CV = run 값의 표본 표준편차 / 평균, p95·p99 = 셀의 모든 run의 윈도 프레임을 합친 nearest-rank. "
              "⚠는 CV ≥ 2 % (목표 미달, M8은 보고만). 엔진 간 비교는 `T_frame`만 한다 (TimingContract §2).")
    md.append("")
    figure_index = 0
    tables_out = validate.tables(classified) if not classified.empty else []
    for name, group, error in tables_out:
        table_cells = cells[cells["table"] == name] if not cells.empty else pd.DataFrame()
        md.append(f"### {short_title(name)}")
        md.append("")
        md.append(condition["markdown"])
        md.append("")
        if error:
            md.append(f"**표 거부:** {error}")
            md.append("")
            continue
        desc = sorted({str(v) for v in group["desc_sha256"].dropna()})
        md.append(f"desc_sha256 `{desc[0][:16] if desc else '-'}` (표 안에서 하나), run {len(group)}개.")
        md.append("")
        for note in validate.table_notes(group):
            md.append(f"> **주의:** {note}")
            md.append("")
        md.append("| series | n | median | p95 | p99 | CV | min – max | frame 0 (median) |")
        md.append("|---|---|---|---|---|---|---|---|")
        ordered = table_cells.sort_values("label", key=lambda s: s.map(lambda v: labels.index(v) if v in labels else 99))
        for row in ordered.to_dict("records"):
            if not _finite(row["median_ns"]):
                md.append(f"| {row['label']} | 0 | 윈도 `{window}`에 값 없음 | - | - | - | - | {_fmt(row['frame0_median_ns'])} |")
                continue
            md.append(f"| {row['label']} | {row['n_runs']} | {_fmt(row['median_ns'])} | {_fmt(row['p95_ns'])} | {_fmt(row['p99_ns'])} | {_cv(row['cv_pct'])} | "
                      f"{_fmt(row['min_ns'])} – {_fmt(row['max_ns'])} | {_fmt(row['frame0_median_ns'])} |")
        md.append("")
        figure_index += 1
        figure = f"table_{figure_index:02d}.png"
        if not plot_table(ordered, colors, short_title(name), out_dir / figure, condition["figure"]):
            # e.g. --window impact on a scene that declares only a headline window, or settled on runs shorter than it
            md.append(f"그림 없음: 이 표의 어떤 run에도 윈도 `{window}`의 값이 없다.")
            md.append("")
            continue
        md.append(f"![{short_title(name)}]({figure})")
        md.append("")
        first = ordered.iloc[0] if not ordered.empty else None
        if first is not None and str(first["scenario"]).startswith("M3."):
            series = {}
            for row in ordered.to_dict("records"):
                cell_runs = group[group["label"] == row["label"]]
                first_col, last_col = f"win_{window}_first", f"win_{window}_last"
                if first_col not in cell_runs.columns or last_col not in cell_runs.columns:
                    continue
                lo = pd.to_numeric(cell_runs[first_col], errors="coerce").min()
                hi = pd.to_numeric(cell_runs[last_col], errors="coerce").max()
                if not (_finite(lo) and _finite(hi)):
                    continue   # no run of this series has the window
                series[row["label"]] = perf.frame_series(loaded.frames, row["run_keys"], int(lo), int(hi))
            figure = f"frames_{figure_index:02d}.png"
            if plot_series(series, colors, f"{short_title(name)}: per-frame median, IQR across runs", out_dir / figure, condition["figure"]):
                md.append(f"![{short_title(name)} per frame]({figure})")
                md.append("")

    md += section_cv(cells, condition["markdown"], window)

    md.append("## 4. 스케일링과 오버헤드 모델 (원시, 비공식)")
    md.append("")
    md.append(condition["markdown"])
    md.append("")
    alpha = perf.scaling_alpha(cells) if not cells.empty else pd.DataFrame()
    overhead = perf.overhead_model(cells) if not cells.empty else pd.DataFrame()
    speed = perf.speedup(cells) if not cells.empty else pd.DataFrame()
    if not alpha.empty:
        md.append("| series | scene | N | median | α (t = c·N^α) |")
        md.append("|---|---|---|---|---|")
        for row in alpha.to_dict("records"):
            md.append(f"| {row['label']} · {row['profile']} · T{row['T']} | {row['scenario']} | {', '.join(map(str, row['N']))} | "
                      f"{', '.join(_fmt(v) for v in row['median_ns'])} | {row['alpha']:.3f} |")
        md.append("")
        plot_scaling(alpha, colors, out_dir / "scaling.png", condition["figure"])
        md.append("![N-scaling](scaling.png)")
        md.append("")
    if not overhead.empty:
        md.append("오버헤드 모델 t = a + b·N (P0/P1/M3 셀의 median, 최소제곱):")
        md.append("")
        md.append("| series | a (프레임당 고정) | b (바디당) | R² | 점 |")
        md.append("|---|---|---|---|---|")
        for row in overhead.to_dict("records"):
            md.append(f"| {row['label']} · {row['profile']} · T{row['T']} | {_fmt(row['a_ns'])} | {row['b_ns_per_body']:.1f} ns | {row['r2']:.4f} | {', '.join(row['scenes'])} |")
        md.append("")
        if (overhead["a_ns"] < 0).any():
            md.append("a < 0은 비용이 N에 대해 선형보다 빨리 자란다는 뜻이다 (α > 1, 접촉 쌍이 바디 수보다 빨리 는다). 선형 모델은 M8 형식일 뿐 물리적 고정 비용의 추정이 아니다. "
                      "P1 셀(N 10, 100)이 있으면 a가 작은 N 쪽에 맞는다.")
            md.append("")
    if not speed.empty:
        md.append("S(T) = t(1)/t(T), E(T) = S/T (원시, cpu_eff 미검증 (M10)):")
        md.append("")
        md.append("| series | scene | T | median | S | E |")
        md.append("|---|---|---|---|---|---|")
        for row in speed.to_dict("records"):
            md.append(f"| {row['label']} · {row['profile']} | {row['scene']} | {row['T']} | {_fmt(row['median_ns'])} | {row['S']:.2f} | {row['E']:.2f} |")
        md.append("")
    if alpha.empty and overhead.empty and speed.empty:
        md.append("해당 없음 (N이나 T가 하나뿐).")
        md.append("")

    md.append("## 5. 거부된 run과 재시도")
    md.append("")
    rejected = classified[~classified["accepted"]] if not classified.empty else classified
    # M9: behaviour runs (run.mode Behavior) and runs the bench refused as unsupported (a scene needs a capability the backend
    # lacks) never enter a timing table by design: one line each instead of a row per run.
    outside = pd.Series(False, index=rejected.index)
    if not rejected.empty:
        behaviour_mask = rejected["mode"] == "Behavior" if "mode" in rejected.columns else outside
        unsupported_mask = rejected["exit_status"] == "unsupported" if "exit_status" in rejected.columns else outside
        behaviour_mask = behaviour_mask.fillna(False).astype(bool)
        unsupported_mask = unsupported_mask.fillna(False).astype(bool)
        outside = behaviour_mask | unsupported_mask
        if behaviour_mask.any():
            md.append(f"- 거동 run {int(behaviour_mask.sum())}개 (`run.mode` Behavior): 타이밍 표에 넣지 않는다. 분석은 §7.")
        if unsupported_mask.any():
            md.append(f"- `unsupported` run {int(unsupported_mask.sum())}개: 씬이 요구하는 capability(조인트)가 백엔드에 없어 벤치가 초기화 전에 거부했다 (실패가 아니다).")
        if outside.any():
            md.append("")
        rejected = rejected[~outside]
    if rejected.empty:
        md.append("거부된 run 없음." if not outside.any() else "그 밖에 거부된 run 없음.")
    else:
        md.append("| run | 셀 | 결과 | 사유 |")
        md.append("|---|---|---|---|")
        for row in rejected.to_dict("records"):
            md.append(f"| `{row['run_name']}` | {_esc(row['cell_id'])} | {row.get('outcome')} | {_esc('; '.join(row['reject_reasons']))} |")
    md.append("")

    md.append("## 6. 방법")
    md.append("")
    retries = sorted({int((d.get("runner") or {}).get("contaminated_retries")) for d in documents
                      if isinstance((d.get("runner") or {}).get("contaminated_retries"), int)})
    if not retries:
        retry_text = "종료 코드 4의 재시도 횟수는 세션 파일이 없어 알 수 없다 (러너 기본값 2)"
    elif retries == [0]:
        retry_text = "종료 코드 4는 다시 돌리지 않았고 (`contaminated_retries` 0) 그 run의 셀이 `contaminated`다"
    else:
        retry_text = (f"종료 코드 4는 같은 run을 fresh process로 최대 {'/'.join(map(str, retries))}번 다시 돌리고"
                      f"{' (세션마다 다르다)' if len(retries) > 1 else ''}, 모든 시도가 4이면 셀이 `contaminated`다")
    md.append("- 러너: 셀 = target × profile × scene × T, 반복 r마다 seed 고정 블록 셔플(splitmix64 + Fisher-Yates), run마다 fresh process, 쿨다운, 타임아웃. "
              "판정은 `exit.json`이다: 없으면 프로세스가 끝까지 가지 못한 실패이고 원시 종료 코드로 성공을 판정하지 않는다 (02 §12.2, DL52). "
              f"{retry_text}.")
    md.append("- 채택 조건 (validate): `exit.json` 0, 두 스키마 통과, Bench 모드, 계측 tier 0, preflight 게이트 통과, 오염 없음, `timing_valid_for_headline` 참. "
              "한 표에 플래그 프로필이 섞이거나 `desc_sha256`이 다르면 표 자체를 거부하고 통계를 내지 않는다 (§3에도 '표 거부'로만 적는다). "
              "한 표의 run이 여러 빌드(exe, git, preset)에서 왔으면 표 아래에 주의를 적는다 (comparability key는 M10).")
    md.append("- bootstrap CI, comparability key, CV 게이트, sentinel, 주장 규칙은 M10이다. 이 리포트의 비율과 순위는 주장이 아니다.")
    md.append("")
    if has_behaviour_runs(classified):
        # M9: the behaviour section (02 13 behavior) over the same inputs; its PNGs, differences.csv and behavior.json go
        # next to index.md.
        from . import behavior

        analysis = behavior.analyze(inputs)
        if analysis.results or analysis.unsupported:
            md += behavior.markdown_section(analysis, out_dir, condition, number="7")
    path = out_dir / "index.md"
    common.write_text_atomic(path, "\n".join(md) + "\n")
    return path


def main(args) -> int:
    inputs = args.inputs if len(args.inputs) > 1 else args.inputs[0]
    path = build_report(inputs, pathlib.Path(args.out) if args.out else None, args.window)
    print(f"report: {path}")
    return 0
