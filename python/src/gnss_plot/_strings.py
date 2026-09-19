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

        # --- RTK figures (chapter 8) ---
        "title_rtk_ts": "RTK float ENU error against the base header ({rover}, {n} epochs)",
        "title_rtk_bars": "RTK float accuracy by constellation ({rover})",

        # Three decimals, not the two the SPP figure uses: an RTK sigma is
        # tens of millimetres, and two decimals would print it as 0.05 for both
        # the good and the mediocre constellation.
        "annot_stats_rtk": "{mode}: bias={bias:+.3f}  σ={std:.3f}  RMS={rms:.3f} m",
        # The bar chart is logarithmic so that bias and sigma stay comparable
        # across two orders of magnitude; the sign of a bias is carried by the
        # bar label and the table instead, which is what this axis label says.
        "axis_rtk_abs": "|value| [m] — log axis, bias is signed in the label",
        "annot_bar_signed": "{v:+.4f}",
        "annot_bar_plain": "{v:.4f}",

        "legend_sys_gps": "GPS",
        "legend_sys_bds2": "BDS-2",
        "legend_sys_bds3": "BDS-3",

        "annot_rtk_plotted": "({n} epochs per constellation, every epoch drawn "
                             "- nothing is downsampled)",

        "lbl_bar_bias_e": "bias E",
        "lbl_bar_bias_n": "bias N",
        "lbl_bar_bias_u": "bias U",
        "lbl_bar_sigma_e": "σ E",
        "lbl_bar_sigma_n": "σ N",
        "lbl_bar_sigma_u": "σ U",
        "lbl_bar_rms3d": "3-D RMS",

        "legend_bar_bias": "bias — the datum offset, not accuracy",
        "legend_bar_sigma": "sigma — the precision",
        "legend_bar_rms": "3-D RMS about the reference",

        # --- RTK console report (chapter 8) ---
        "rpt_rtk_title": "RTK float accuracy against the base header position ({rover})",
        "rpt_rtk_ref": "Reference = base header APPROX POSITION XYZ (ECEF): "
                       "{x:.4f} {y:.4f} {z:.4f} m  [{src}]",
        "rpt_rtk_epochs": "{n} epochs per constellation, {nmodes} constellations",

        "rpt_rtk_head": "SPP/RTK 3-D RMS improvement: {ratio}   "
                        "(the one reference-independent number here)",
        "rpt_rtk_c_sys": "constellation",
        "rpt_rtk_c_system": "system",
        "rpt_rtk_c_epochs": "epochs",
        "rpt_rtk_c_spp": "SPP 3D RMS",
        "rpt_rtk_c_rtk": "RTK 3D RMS",
        "rpt_rtk_c_ratio": "SPP/RTK",

        "rpt_rtk_pct_head": "[3-D error magnitude |d|, percentiles]  "
                            "the last column counts the epochs unlike the rest",
        "rpt_rtk_c_p50": "p50",
        "rpt_rtk_c_p68": "p68",
        "rpt_rtk_c_p95": "p95",
        "rpt_rtk_c_max": "max",
        "rpt_rtk_c_outliers": "sigma0>10xmed",

        "rpt_rtk_axis_head": "[Per-axis bias and sigma, metres]  "
                             "bias and sigma are different quantities — see note 1.  "
                             "The ECEF rows are the same quantities on the raw X/Y/Z "
                             "axes, which is the frame the .out columns are in; either "
                             "frame's three values square-sum to the 3-D RMS.",
        "rpt_rtk_c_frame": "frame",
        "rpt_rtk_c_quantity": "quantity",
        "rpt_rtk_q_spp_bias": "SPP bias",
        "rpt_rtk_q_spp_sigma": "SPP sigma",
        "rpt_rtk_q_bias": "RTK bias",
        "rpt_rtk_q_sigma": "RTK sigma",
        "rpt_rtk_q_rms": "RTK RMS",
        "rpt_rtk_q_rms3d": "RTK 3-D RMS",

        "rpt_rtk_note_bias":
            "Note 1 — the bias is a datum offset, not an accuracy figure. "
            "SPPUCCodePhase::solve builds the base station's equations at its "
            "RINEX header coordinate and returns without iterating, and the rover "
            "is accepted as soon as |dxyz| < 0.1 m. Every epoch therefore "
            "reproduces the base header position plus the rover's last, "
            "unconverged correction — up to 0.1 m, and near-constant over the "
            "run. Read sigma as the precision and the bias as that offset.",
        "rpt_rtk_note_ratio":
            "Note 2 — the SPP/RTK ratio is the one reference-independent number "
            "in the table: both columns are differenced against the same base "
            "header, so the header's own error (metre-to-decametre) cancels in "
            "the ratio even though it does not cancel in either RMS.",
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

        # --- RTK 图（第 8 章）---
        "title_rtk_ts": "RTK 浮点解 ENU 误差时序（相对基准站表头，{rover}，{n} 个历元）",
        "title_rtk_bars": "各星座 RTK 浮点解精度（{rover}）",

        "annot_stats_rtk": "{mode}: 偏差={bias:+.3f}  σ={std:.3f}  RMS={rms:.3f} m",
        "axis_rtk_abs": "|数值| [m] —— 对数轴，偏差的正负见柱上标注",
        "annot_bar_signed": "{v:+.4f}",
        "annot_bar_plain": "{v:.4f}",

        "legend_sys_gps": "GPS",
        "legend_sys_bds2": "BDS-2",
        "legend_sys_bds3": "BDS-3",

        "annot_rtk_plotted": "（每个星座 {n} 个历元，全部绘出，未做任何抽稀）",

        "lbl_bar_bias_e": "偏差 E",
        "lbl_bar_bias_n": "偏差 N",
        "lbl_bar_bias_u": "偏差 U",
        "lbl_bar_sigma_e": "σ E",
        "lbl_bar_sigma_n": "σ N",
        "lbl_bar_sigma_u": "σ U",
        "lbl_bar_rms3d": "三维 RMS",

        "legend_bar_bias": "偏差 —— 基准偏移，不是精度",
        "legend_bar_sigma": "σ —— 内符合精度",
        "legend_bar_rms": "相对参考的外符合三维 RMS",

        # --- RTK 控制台报告（第 8 章）---
        "rpt_rtk_title": "RTK 浮点解精度评估（相对基准站表头坐标，{rover}）",
        "rpt_rtk_ref": "参考坐标 = 基准站表头 APPROX POSITION XYZ (ECEF): "
                       "{x:.4f} {y:.4f} {z:.4f} m  [{src}]",
        "rpt_rtk_epochs": "每个星座 {n} 个历元，共 {nmodes} 个星座",

        "rpt_rtk_head": "SPP/RTK 三维 RMS 改善比: {ratio}   （本表中唯一与参考坐标无关的数）",
        "rpt_rtk_c_sys": "星座",
        "rpt_rtk_c_system": "信号组合",
        "rpt_rtk_c_epochs": "历元数",
        "rpt_rtk_c_spp": "SPP 三维RMS",
        "rpt_rtk_c_rtk": "RTK 三维RMS",
        "rpt_rtk_c_ratio": "改善比",

        "rpt_rtk_pct_head": "【三维误差模 |d| 的分位数】最后一列是与众不同的历元个数",
        "rpt_rtk_c_p50": "p50",
        "rpt_rtk_c_p68": "p68",
        "rpt_rtk_c_p95": "p95",
        "rpt_rtk_c_max": "最大值",
        "rpt_rtk_c_outliers": "σ₀>10×中位数",

        "rpt_rtk_axis_head": "【各分量偏差与内符合σ，单位 m】偏差与σ是两种量，见说明 1；"
                             "标 ECEF 的行是同样两个量在原始 X/Y/Z 轴上的值（即 .out "
                             "文件各列的坐标系），两种坐标系三者的平方和都等于三维 RMS",
        "rpt_rtk_c_frame": "坐标系",
        "rpt_rtk_c_quantity": "统计量",
        "rpt_rtk_q_spp_bias": "SPP 偏差",
        "rpt_rtk_q_spp_sigma": "SPP σ",
        "rpt_rtk_q_bias": "RTK 偏差",
        "rpt_rtk_q_sigma": "RTK σ",
        "rpt_rtk_q_rms": "RTK RMS",
        "rpt_rtk_q_rms3d": "RTK 三维 RMS",

        "rpt_rtk_note_bias":
            "说明 1 —— 偏差不是精度指标，而是基准偏移。SPPUCCodePhase::solve 用基准站 "
            "RINEX 表头坐标建立基准站方程后直接返回（不再迭代），流动站则迭代到 "
            "|dxyz| < 0.1 m 即判收敛。因此每个历元的结果 = 基准站表头坐标 + 流动站最后一次"
            "未收敛的改正数，最大 0.1 m 且整段近似为常数。σ 才是精度，偏差应理解为这一偏移量。",
        "rpt_rtk_note_ratio":
            "说明 2 —— SPP/RTK 改善比是本表中唯一与参考坐标无关的数：两列都对同一个基准站"
            "表头作差，表头自身的误差（米级至十米级）在比值中被约掉，而在各自的 RMS 中"
            "并不能被约掉。",
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
