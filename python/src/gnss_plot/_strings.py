# -*- coding: utf-8 -*-
"""Translatable strings for figures and the console report.

Figures default to English so the repository reads to an international
audience; ``--lang zh`` switches them to Chinese. Keeping every user-visible
string in one place means a figure and its console report can never drift apart
in wording.
"""

from __future__ import annotations

LANGS = ("en", "zh")
DEFAULT_LANG = "en"

STRINGS: dict[str, dict[str, str]] = {
    "en": {
        # --- axis / figure labels ---
        "axis_sod": "Second of day [s]",
        "axis_east_err": "East error [m]",
        "axis_north_err": "North error [m]",
        "axis_up_err": "Up error [m]",
        "axis_east_vel": "vE East [m/s]",
        "axis_north_vel": "vN North [m/s]",
        "axis_up_vel": "vU Up [m/s]",
        "axis_clk": r"Receiver clock drift $c\cdot\delta\dot{t}_r$ [m/s]",

        "lbl_east": "East",
        "lbl_north": "North",
        "lbl_up": "Up",

        "title_pos_ts": "SPP positioning error ({mode}, reference = RINEX header)",
        "title_pos_h": "SPP horizontal error distribution",
        "title_vel_ts": "Doppler velocity ({mode}, static station, truth = 0)",
        "title_clock": "Receiver clock drift ({mode})",

        "legend_epochs": "epochs ({n})",
        "legend_rms_circle": "2D RMS circle (r={r:.2f} m)",
        "legend_mean_bias": "mean bias ({e:+.2f}, {n:+.2f})",
        "annot_stats": "bias={bias:+.2f}  σ={std:.2f}  RMS={rms:.2f} m",
        "annot_stats_vel": "bias={bias:+.4f}  σ={std:.4f}  RMS={rms:.4f} m/s",
        "annot_clk": "range {lo:.4f} - {hi:.4f} m/s, mean {mean:.4f}",
        "saved_to": "Figures written to {dir}",

        # --- console report ---
        "rpt_title": "GNSS static-point positioning ({mode}) + velocity accuracy report",
        "rpt_station": "station",
        "rpt_epochs": "Valid epochs: position {np} / velocity {nv}",
        "rpt_ref": "Reference (ECEF): {x:.1f} {y:.1f} {z:.1f} m",
        "rpt_pos_head": "[Position - local ENU error] (relative to RINEX reference, metres)",
        "rpt_vel_head": "[Velocity - local vE/vN/vU] (static truth = 0, m/s)",
        "rpt_cols": "  comp      bias        sigma         RMS",
        "rpt_2d3d": "Horizontal 2D RMS = {h:.3f} m    Vertical 3D RMS = {d:.3f} m",
        "rpt_2d3d_vel": "Horizontal 2D RMS = {h:.5f} m/s  Vertical 3D RMS = {d:.5f} m/s",
        "rpt_mean_xyz": "Mean solution = ({x:.3f}, {y:.3f}, {z:.3f})",
        "rpt_clk": "Clock drift recClkDot: {lo:.4f} - {hi:.4f} m/s, mean {mean:.4f}"
                   " (~{secs:.1e} s/s)",

        # --- errors ---
        "err_no_approx": "APPROX POSITION XYZ not found in the header of {path}",
        "err_missing_file": "File not found: {path}",
        "err_empty": "No data rows parsed from {path}",
    },
    "zh": {
        "axis_sod": "秒内时刻 sod [s]",
        "axis_east_err": "东向误差 [m]",
        "axis_north_err": "北向误差 [m]",
        "axis_up_err": "天向误差 [m]",
        "axis_east_vel": "vE 东向 [m/s]",
        "axis_north_vel": "vN 北向 [m/s]",
        "axis_up_vel": "vU 天向 [m/s]",
        "axis_clk": "接收机钟漂 c·δt_r [m/s]",

        "lbl_east": "东",
        "lbl_north": "北",
        "lbl_up": "天",

        "title_pos_ts": "SPP 单点定位误差时序 ({mode}, 参考 = RINEX 表头)",
        "title_pos_h": "SPP 定位水平误差分布",
        "title_vel_ts": "单点测速时序 ({mode}, 静态站真值 = 0)",
        "title_clock": "接收机钟漂时序 ({mode})",

        "legend_epochs": "历元点 ({n})",
        "legend_rms_circle": "2D RMS 圆 (r={r:.2f} m)",
        "legend_mean_bias": "均值偏差 ({e:+.2f}, {n:+.2f})",
        "annot_stats": "bias={bias:+.2f}  σ={std:.2f}  RMS={rms:.2f} m",
        "annot_stats_vel": "bias={bias:+.4f}  σ={std:.4f}  RMS={rms:.4f} m/s",
        "annot_clk": "范围 {lo:.4f} ~ {hi:.4f} m/s, 均值 {mean:.4f}",
        "saved_to": "图片已保存到 {dir}",

        "rpt_title": "GNSS 单点定位 ({mode}) + 单点测速 精度评估报告",
        "rpt_station": "站点",
        "rpt_epochs": "有效历元: 定位 {np} / 测速 {nv}",
        "rpt_ref": "参考坐标(ECEF): {x:.1f} {y:.1f} {z:.1f} m",
        "rpt_pos_head": "【定位 · 站心 ENU 误差】(相对 RINEX 参考, 单位 m)",
        "rpt_vel_head": "【测速 · 站心 vE/vN/vU】(静态真值 = 0, 单位 m/s)",
        "rpt_cols": "  分量      均值偏差     内符合σ     外符合RMS",
        "rpt_2d3d": "水平 2D RMS = {h:.3f} m    三维 3D RMS = {d:.3f} m",
        "rpt_2d3d_vel": "水平 2D RMS = {h:.5f} m/s  三维 3D RMS = {d:.5f} m/s",
        "rpt_mean_xyz": "均值解坐标 = ({x:.3f}, {y:.3f}, {z:.3f})",
        "rpt_clk": "钟漂 recClkDot: {lo:.4f} ~ {hi:.4f} m/s, 均值 {mean:.4f}"
                   "  (≈{secs:.1e} s/s)",

        "err_no_approx": "在 {path} 表头找不到 APPROX POSITION XYZ",
        "err_missing_file": "文件不存在: {path}",
        "err_empty": "{path} 未解析到任何数据行",
    },
}


def normalise_lang(lang: str | None) -> str:
    """Map a user-supplied language tag onto a supported one."""
    if not lang:
        return DEFAULT_LANG
    tag = str(lang).strip().lower().replace("_", "-")
    if tag.startswith("zh"):
        return "zh"
    if tag.startswith("en"):
        return "en"
    return DEFAULT_LANG


def t(lang: str, key: str, **kw) -> str:
    """Look up `key` in the language table and format it.

    Falls back to English, then to the bare key, so a missing translation is
    visible but never fatal.
    """
    table = STRINGS.get(lang) or STRINGS[DEFAULT_LANG]
    text = table.get(key) or STRINGS[DEFAULT_LANG].get(key) or key
    return text.format(**kw) if kw else text
