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
        # Plain text, not LaTeX: braces in a format string are read as
        # placeholders, so "$c\cdot\delta\dot{t}_r$" would raise a KeyError the
        # moment this label is formatted with any argument.
        "axis_clk": "Receiver clock drift [m/s]",

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

        # --- cycle-slip figures (chapter 7) ---
        "title_slip_rate": "Reported slip rate by satellite",
        "title_slip_series": "{sat} — GF series and detector statistics ({run})",
        "title_state_composition": "Detector state composition",
        "title_injection_outcomes": "Injected slips: per-case outcome",

        "axis_sat": "Satellite",
        "axis_slip_rate": "Reported slip rate [%]",
        "axis_share": "Share of epoch rows [%]",
        "axis_li": "L_I = L1 - L2 [m]",
        "axis_stat": "|detector statistic| [m]",
        "axis_state": "State",

        "legend_detector_diff": "epoch difference",
        "legend_detector_poly": "polynomial fit",
        "legend_threshold": "threshold {thr:.3f} m",
        "legend_slip_epochs": "slip reported",
        "legend_init_epochs": "arc start",
        "legend_gap_epochs": "data gap",

        "annot_slip_rate": "{nslip} / {ntested} judged epochs = {rate:.2f}%",
        "annot_slip_series": "{nslip} slips over {ntested} judged epochs, threshold {thr:.3f} m",
        "annot_injection": "{run}: {tp} / {n} detected ({rate:.1f}%)",

        "lbl_state_ok": "OK",
        "lbl_state_slip": "SLIP",
        "lbl_state_init": "INIT arc start",
        "lbl_state_gap": "GAP data gap",
        "lbl_state_warmup": "WARMUP window filling",

        "lbl_outcome_hit": "detected",
        "lbl_outcome_miss": "missed",
        "lbl_outcome_decline": "declined (correct)",
        "lbl_outcome_wrong": "slip reported (wrong)",

        # --- MW figures (chapter 7) ---
        "title_slip_rate_mw": "Reported slip rate by satellite — MW",
        "title_mw_series": "{sat} — wide-lane ambiguity N_W and reported flags ({run})",
        "title_crosscheck": "GF and MW on the same epochs",
        "title_nullspace": "{sat} — the GF null space ({dn1}, {dn2})",
        "title_nullspace_mw": "{sat} — the MW null space ({dn1}, {dn2})",

        # MW is plotted as the wide-lane ambiguity, so the axis names it. The
        # combination is metres on disk and cycles on the axis - see
        # cycleslip.mw_wavelength.
        "axis_mw": "N_W = N1 - N2 [cycles]",
        # The MW deviation panel. Separate from `axis_stat` because that one is
        # shared with the two GF detectors, which stay in metres.
        "axis_stat_mw": "|N_W - mean| [cycles]",

        "legend_detector_mw": "Melbourne-Wübbena",
        "legend_xcheck_both": "both report a slip",
        "legend_xcheck_gf_only": "GF only — invisible to MW",
        "legend_xcheck_mw_only": "MW only — invisible to GF",

        "annot_crosscheck": "{gf} GF flags, {mw} MW flags, {both} in common",
        "annot_nullspace": "a slip of {dn1} / {dn2} cycles is injected here",
    },
    "zh": {
        "axis_sod": "秒内时刻 sod [s]",
        "axis_east_err": "东向误差 [m]",
        "axis_north_err": "北向误差 [m]",
        "axis_up_err": "天向误差 [m]",
        "axis_east_vel": "vE 东向 [m/s]",
        "axis_north_vel": "vN 北向 [m/s]",
        "axis_up_vel": "vU 天向 [m/s]",
        "axis_clk": "接收机钟漂 [m/s]",

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

        # --- 第7章周跳图 ---
        "title_slip_rate": "各卫星的周跳标记比例",
        "title_slip_series": "{sat} —— GF 序列与探测器统计量（{run}）",
        "title_state_composition": "探测器状态构成",
        "title_injection_outcomes": "注入周跳：逐用例判定结果",

        "axis_sat": "卫星",
        "axis_slip_rate": "标记比例 [%]",
        "axis_share": "占历元行数比例 [%]",
        "axis_li": "L_I = L1 - L2 [m]",
        "axis_stat": "|探测器统计量| [m]",
        "axis_state": "状态",

        "legend_detector_diff": "历元一次差分",
        "legend_detector_poly": "多项式拟合",
        "legend_threshold": "阈值 {thr:.3f} m",
        "legend_slip_epochs": "报出周跳",
        "legend_init_epochs": "弧段首历元",
        "legend_gap_epochs": "数据中断",

        "annot_slip_rate": "{nslip} / {ntested} 个已判定历元 = {rate:.2f}%",
        "annot_slip_series": "{ntested} 个已判定历元中报出 {nslip} 次，阈值 {thr:.3f} m",
        "annot_injection": "{run}：检出 {tp} / {n}（{rate:.1f}%）",

        "lbl_state_ok": "OK",
        "lbl_state_slip": "SLIP 周跳",
        "lbl_state_init": "INIT 弧段首历元",
        "lbl_state_gap": "GAP 数据中断",
        "lbl_state_warmup": "WARMUP 窗口预热",

        "lbl_outcome_hit": "检出",
        "lbl_outcome_miss": "漏检",
        "lbl_outcome_decline": "正确拒判",
        "lbl_outcome_wrong": "误报周跳",

        # --- MW 图（第 7 章）---
        "title_slip_rate_mw": "各卫星的周跳标记比例 —— MW 组合",
        "title_mw_series": "{sat} —— 宽巷模糊度 N_W 与标记（{run}）",
        "title_crosscheck": "GF 与 MW 在相同历元上的判定对照",
        "title_nullspace": "{sat} —— GF 的零空间（{dn1}, {dn2}）",
        "title_nullspace_mw": "{sat} —— MW 的零空间（{dn1}, {dn2}）",

        "axis_mw": "宽巷模糊度 N_W [周]",
        "axis_stat_mw": "|N_W 偏离均值| [周]",

        "legend_detector_mw": "MW 组合",
        "legend_xcheck_both": "两者都报周跳",
        "legend_xcheck_gf_only": "仅 GF —— MW 看不到",
        "legend_xcheck_mw_only": "仅 MW —— GF 看不到",

        "annot_crosscheck": "GF 标记 {gf} 个，MW 标记 {mw} 个，重合 {both} 个",
        "annot_nullspace": "此历元注入 {dn1} / {dn2} 周",
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
