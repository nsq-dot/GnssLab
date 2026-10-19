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
        # The four figures that stop before their run does: figure 8-1 and its
        # three directional siblings, which draw the same clipped span
        # (figures._WINDOW_8_1_SOD_MAX). Their x label has to say where the axis
        # stops, so that a short curve cannot be read as a short run; every
        # other figure keeps the plain axis_sod above.
        "axis_sod_to": "Second of day [s] (axis drawn to {to})",
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
        "title_rtk_ts": "RTK float ENU error against the reference ({rover}, {n} epochs)",
        "title_rtk_bars": "RTK float accuracy by constellation ({rover})",

        # Three decimals, not the two the SPP figure uses: an RTK sigma is
        # tens of millimetres, and two decimals would print it as 0.05 for both
        # the good and the mediocre constellation.
        "annot_stats_rtk": "{mode}: bias={bias:+.3f}  σ={std:.3f}  RMS={rms:.3f} m",
        # The bar chart is logarithmic so that bias and sigma stay comparable
        # across two orders of magnitude; the sign of a bias is carried by the
        # bar label and the table instead, which is what this axis label says.
        "axis_rtk_abs": "|value| [m] — bias is signed in the label",
        "annot_bar_signed": "{v:+.4f}",
        "annot_bar_plain": "{v:.4f}",

        # --- the SPP/RTK comparison figure (8-1) ---
        # SPP and RTK are two *methods*, so they get their own two legend
        # entries rather than reusing an axis or a constellation slot: in this
        # chapter blue/orange/aqua already mean E/N/U in one figure and the
        # three constellations in another.
        "title_rtk_spp": "SPP vs RTK deviation from the reference, one panel per "
                         "constellation ({rover}, {n} epochs each)",
        # Both halves are load-bearing. "one window for all panels" is why
        # BDS-2's 1.13 m and BDS-3's 0.076 m do not look alike, and "deviation"
        # rather than "error" is what the axis actually measures: a distance
        # from the base header coordinate, which this dataset cannot check.
        "axis_rtk_mag": "deviation |d| [m] — each panel its own scale",
        # Short, as in the sibling figures (8-4's "RTK float").
        "legend_spp": "SPP",
        "legend_rtk": "RTK float",
        # {rms} is about the reference; {off} is the distance between the
        # series' own mean and that reference, and is what separates the two
        # bands. Printing it is the whole caveat on this figure, in one number:
        # the SPP side is mostly offset, the RTK side's near-zero one is
        # structural (double differencing resolves the rover relative to the
        # base, whose position is the reference itself).
        "annot_rtk_vs_rms": "{method}: 3-D RMS {rms:.3f} m  ·  offset {off:.3f} m",
        # The raw ratio, the console table's rightmost column. The centred and
        # epoch-to-epoch ratios live in section four, which says what each one
        # is a ratio of.
        "annot_rtk_vs_gain": "SPP/RTK 3-D RMS ratio: {ratio}",

        # --- the fixed solution (figures 8-4 and 8-5) ---
        "title_rtk_fix": "RTK float vs fixed 3-D error, one panel per "
                         "constellation ({rover}, {n} epochs each, ratio > {thr})",
        # Deliberately the same length as axis_rtk_mag, which figure 8-1 uses:
        # a longer label runs off the bottom of a three-panel figure once
        # tight_layout has taken its share. What grey and red mean is the
        # legend's job, not the axis label's.
        "axis_rtk_mag_fix": "3-D error |d| [m] — each panel its own scale",
        "legend_float": "RTK float",
        "legend_fixed": "RTK fixed (accepted)",
        # {rate} and the RMS values are numbers, not preformatted strings: the
        # "worst accepted" figure is the one that says whether any fix was
        # wrong, so it is printed to a tenth of a millimetre.
        "annot_rtk_fix": "{mode}: {rate:.1f}% fixed  ·  float RMS {frms:.3f} m  "
                         "·  fixed RMS {xrms:.4f} m  ·  worst accepted {worst:.4f} m",
        "title_rtk_ratio": "Ratio-test distribution by constellation "
                           "({rover}, threshold {thr})",
        # "Ω2/Ω1", not the subscript forms: the CJK font families this package
        # falls back to have no glyph for U+2082/U+2081 and render them as
        # boxes, in both languages.
        "axis_ratio": "ratio Ω2/Ω1 — log axis",
        "axis_ratio_share": "share of the run [%]",
        "annot_ratio": "{mode}: {rate:.1f}% accepted ({n} epochs)",
        "annot_ratio_thr": "threshold {thr} — left of this line is rejected",

        # --- the estimator comparison (figure 8-6) ---
        # Same length as axis_rtk_mag_fix, for the reason given above it.
        "axis_rtk_mag_kalman": "3-D error |d| [m] — each panel its own scale",
        "title_rtk_kalman": "RTK float: single-epoch LSQ vs Kalman filter, one "
                            "panel per constellation ({rover}, {n} epochs each)",
        # Named for the estimators, not for "float" against "something else":
        # both series here are float solutions, so 8-4's legend_float would read
        # as if one of the two had been fixed.
        "legend_lsq_float": "single-epoch LSQ (float)",
        "legend_kalman_float": "Kalman filter (float, ambiguities carried)",
        # {gain} arrives preformatted ("89.1×", or "-" when the Kalman RMS is
        # zero), the convention annot_rtk_vs_gain uses, so that no zero or NaN
        # can reach a format spec inside this string.
        "annot_rtk_kalman": "{mode}: LSQ RMS {lrms:.3f} m  ·  Kalman RMS "
                            "{krms:.3f} m  ·  gain {gain}  ·  |d|>1 m  {n1}/{n2}",

        # --- the DD-weighting comparison (figure 8-7, exercise 3) ---
        "title_rtk_dd_cov": "The ratio test under both DD weightings, one panel "
                            "per constellation ({rover}, {n} epochs each)",
        "axis_rtk_ratio": "ratio test statistic Ω2/Ω1 — linear, with the "
                          "threshold line on the bottom edge",
        "legend_dd_indep": "double differences independent (the default)",
        "legend_dd_cov": "double differences correlated (Q_DD)",
        # {m1}/{m2} arrive preformatted ("522", or "-"), the convention
        # annot_rtk_vs_gain uses, so no NaN or zero reaches a format spec.
        # The two counts are named rather than left as "n1 / n2", which reads
        # as a fraction of a whole; they are one count per run.
        "annot_rtk_dd_cov": "{mode}: ratio median {m1} → {m2}  ·  above "
                            "threshold, independent {n1} · correlated {n2}",

        # --- the directional siblings (figures 8-1b/c/d, 8-4b/c/d, 8-6b/c/d) ---
        # Nine figures: 8-1, 8-4 and 8-6 each redrawn one direction at a time.
        # {dir} is a direction's name, {win} the span as a clause, and {n} how
        # many epochs are *drawn* - not what the span asked for, because 8-4's
        # and 8-6's six fall back to the whole series on a run too short to
        # contain their window, and a title promising 3060 s of it while the axis
        # carries two epochs would be the one lie the degraded path cannot
        # afford. The fallback says so on stdout instead.
        #
        # Two lines, on purpose. All of that on one line runs wider than the
        # figure and is clipped at both ends - which is worse than long, because
        # the clipped end carries the direction the figure is about.
        "title_rtk_spp_zoom": "SPP vs RTK: {dir} error, one panel per "
                              "constellation\n({rover}, {win}, {n} epochs drawn)",
        "title_rtk_fix_zoom": "RTK float vs fixed: {dir} error, one panel per "
                              "constellation\n({rover}, {win}, {n} epochs drawn)",
        "title_rtk_kalman_zoom": "RTK float: single-epoch LSQ vs Kalman filter, "
                                 "{dir} error, one panel per constellation\n"
                                 "({rover}, {win}, {n} epochs drawn)",
        # The window as a clause, so a title can end with it. Kept out of the
        # title keys above because the fallback prints the other of the two.
        "zoom_win": "window {lo}-{hi} s after the first epoch, every {step}th "
                    "epoch",
        "zoom_win_all": "the whole run",
        # 8-1b/c/d's span as a clause, for the same purpose. Not a window, and
        # not "the whole run" either - it is the whole of what figure 8-1 draws,
        # which is *shorter* than the run, so the cap has to be named here
        # exactly as it is on the x label (axis_sod_to).
        "zoom_win_span": "figure 8-1's own span, to sod {to}",
        # The three directions, and the axis label every panel of one figure
        # carries - a figure's panels are its constellations, so all three
        # labels of one figure read the same.
        "dir_e": "East",
        "dir_n": "North",
        "dir_u": "Up",
        "axis_rtk_zoom_err": "{dir} error [m]",
        # One line per curve, drawn in that curve's own colour. The numbers
        # arrive preformatted - two to four decimals, following the data's own
        # scale, because two is right for a metre-level solution and prints a
        # fixed solution at 0.015 m as three zeroes - so nothing but digits
        # reaches a format spec.
        "annot_rtk_zoom_stat": "bias={bias}  σ={std}  RMS={rms} m",
        "note_rtk_zoom_fallback": "note: figure {fig}: the run has {n} "
                                  "epoch(s), shorter than the {lo}-{hi} s "
                                  "window; drawing the whole run instead",

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
        "rpt_rtk_title": "RTK float accuracy against the reference coordinate ({rover})",
        "rpt_rtk_ref": "Reference coordinate (ECEF): "
                       "{x:.4f} {y:.4f} {z:.4f} m  [{src}]",
        "rpt_rtk_epochs": "{n} epochs per constellation, {nmodes} constellations",

        # Not "improvement", and not "reference-independent": the number is a
        # ratio of two raw RMS figures, and the datum term is fully present in
        # one of them. Note 2 says so at the foot of the same table.
        "rpt_rtk_head": "SPP/RTK 3-D RMS ratio: {ratio}   "
                        "(offsets included; see note 2)",
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

        "rpt_rtk_fix_head": "[Ambiguity resolution]  ratio > {thr} is accepted;  "
                            "the two RMS columns are the SAME epochs, float vs fixed",
        "rpt_rtk_c_fixedrate": "fixed",
        "rpt_rtk_c_float": "float 3D RMS",
        "rpt_rtk_c_fixed": "fixed 3D RMS",
        "rpt_rtk_c_worstfix": "worst accepted",
        "rpt_rtk_c_gain": "gain",
        "rpt_rtk_c_ratiop50": "ratio p50",
        "rpt_rtk_note_fix":
            "Note 3 — 'fixed 3D RMS' counts only the epochs the ratio test "
            "accepted, and 'worst accepted' is the largest error among them: if "
            "it is centimetres then no fix in the run was wrong. Both are needed, "
            "because the fixed file ALSO contains the float solution on every "
            "epoch that was rejected — reading it without applying the threshold "
            "is how a solution ends up metres out.",
        "rpt_rtk_note_bias":
            "Note 1 — the bias is a datum offset, not an accuracy figure. "
            "SPPUCCodePhase::solve builds the base station's equations at the "
            "coordinate it is given — the RINEX header by default, --base-xyz if "
            "supplied — and returns without iterating, and the rover is accepted "
            "as soon as |dxyz| < 0.1 m. Every epoch therefore reproduces that "
            "base coordinate plus the rover's last, unconverged correction — up "
            "to 0.1 m, and near-constant over the run. Read sigma as the "
            "precision and the bias as that offset. This describes the RTK "
            "column only; the SPP column's offset is its own error, not a datum "
            "term (0.01 / 3.0 / 1.3 m on this data — section 4).",
        "rpt_rtk_note_ratio":
            "Note 2 — the SPP/RTK ratio is NOT reference-independent. Both "
            "columns are measured against the same reference, but the RTK "
            "column is built on the base station's assumed coordinate: double "
            "differencing resolves the rover relative to the base, so that "
            "coordinate enters the RTK solution by construction. Measure against "
            "a different reference — the RINEX header, say, which on this "
            "equipment sits 24.9 m from the antenna — and both columns move. "
            "The ratio carries that dependence with it; section 4 and figure "
            "8-1 separate each column's offset from its residual.",
    },
    "zh": {
        "axis_sod": "秒内时刻 sod [s]",
        # 四张图用这条：图 8-1 和它那三张按方向拆开的孪生图，画的都是同一段截过尾的
        # 区间（figures._WINDOW_8_1_SOD_MAX）。横轴标签必须说明截到哪，否则一段短
        # 曲线会被读成一次短 run；其余图仍用上面不带截断的那条。
        "axis_sod_to": "秒内时刻 sod [s]（横轴截至 {to}）",
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
        "title_rtk_ts": "RTK 浮点解 ENU 误差时序（相对参考坐标，{rover}，{n} 个历元）",
        "title_rtk_bars": "各星座 RTK 浮点解精度（{rover}）",

        "annot_stats_rtk": "{mode}: 偏差={bias:+.3f}  σ={std:.3f}  RMS={rms:.3f} m",
        "axis_rtk_abs": "|数值| [m] —— 偏差的正负见柱上标注",
        "annot_bar_signed": "{v:+.4f}",
        "annot_bar_plain": "{v:.4f}",

        # --- SPP 与 RTK 对比图（图 8-1）---
        "title_rtk_spp": "SPP 与 RTK 相对参考坐标的三维偏差时序，每个星座一个面板"
                         "（{rover}，各 {n} 个历元）",
        "axis_rtk_mag": "相对参考坐标的偏差 |d| [m] —— 每面板各自缩放",
        # 与同章的图 8-4 一样取短标签。
        "legend_spp": "SPP",
        "legend_rtk": "RTK 浮点解",
        # {rms} 是相对参考坐标的；{off} 是该序列自己的均值离参考有多远，也正是把两组
        # 带子拉开的那一项。把它印出来就是这张图全部的免责声明：SPP 那一侧主要是偏移，
        # RTK 那侧贴近零是结构性的（双差把流动站解到基准站上，而基准站的位置就是参考）。
        "annot_rtk_vs_rms": "{method}: 三维 RMS {rms:.3f} m · 偏移 {off:.3f} m",
        # 原始 RMS 比——控制台表格最右列那个数。去均值比与逐历元比在第四节，
        # 那里说清每个比值到底是谁比谁。
        "annot_rtk_vs_gain": "SPP/RTK 三维 RMS 比: {ratio}",

        # --- 固定解（图 8-4、图 8-5）---
        "title_rtk_fix": "RTK 浮点解与固定解三维误差时序，每个星座一个面板"
                         "（{rover}，各 {n} 个历元，ratio > {thr}）",
        "axis_rtk_mag_fix": "三维误差 |d| [m] —— 每面板各自缩放",
        "legend_float": "RTK 浮点解",
        "legend_fixed": "RTK 固定解（已接受）",
        "annot_rtk_fix": "{mode}: 固定率 {rate:.1f}%  ·  浮点 RMS {frms:.3f} m  "
                         "·  固定 RMS {xrms:.4f} m  ·  已接受的最坏 {worst:.4f} m",
        "title_rtk_ratio": "ratio 检验分布，每个星座一条"
                           "（{rover}，阈值 {thr}）",
        "axis_ratio": "ratio Ω2/Ω1 —— 对数轴",
        "axis_ratio_share": "占本星座历元的比例 [%]",
        "annot_ratio": "{mode}: 接受 {rate:.1f}%（共 {n} 个历元）",
        "annot_ratio_thr": "阈值 {thr} —— 该线左侧全部被拒",

        # --- 解算器对比（图 8-6）---
        "axis_rtk_mag_kalman": "三维误差 |d| [m] —— 每面板各自缩放",
        "title_rtk_kalman": "RTK 浮点解：单历元最小二乘与卡尔曼滤波对比，"
                            "每个星座一个面板（{rover}，各 {n} 个历元）",
        "legend_lsq_float": "单历元最小二乘（浮点解）",
        "legend_kalman_float": "卡尔曼滤波（浮点解，模糊度跨历元）",
        "annot_rtk_kalman": "{mode}: 最小二乘 RMS {lrms:.3f} m  ·  卡尔曼 RMS "
                            "{krms:.3f} m  ·  提升 {gain}  ·  |d|>1 m {n1}/{n2} 个",

        # --- 双差加权对比（图 8-7，习题 3）---
        "title_rtk_dd_cov": "两种双差加权下的 ratio 检验，每个星座一个面板"
                            "（{rover}，各 {n} 个历元）",
        "axis_rtk_ratio": "ratio 检验统计量 Ω2/Ω1 —— 线性，阈值线贴在底边",
        "legend_dd_indep": "双差按独立加权（默认）",
        "legend_dd_cov": "双差按相关加权（Q_DD）",
        "annot_rtk_dd_cov": "{mode}: ratio 中位数 {m1} → {m2}  ·  过阈值："
                            "独立 {n1} · 相关 {n2} 个历元",

        # --- 按方向拆开的孪生图（图 8-1b/c/d、图 8-4b/c/d、图 8-6b/c/d）---
        # 九张：8-1、8-4、8-6 各按方向重画一遍。{dir} 是方向名，{win} 是说明横轴范围的
        # 那句话，{n} 是**绘出**了多少历元——不是范围要了多少：8-4/8-6 那六张在 run 短于
        # 窗口时会退回整段，标题若仍写着"3060 s 的窗口"而横轴只有 2 个历元，那就是退化
        # 路径唯一不能容忍的谎，退回时改由 stdout 说明。8-1 那三张没有窗口也没有回退，
        # 它们的横轴就是父图画的那一段（见 zoom_win_span）。
        "title_rtk_spp_zoom": "SPP 与 RTK 的{dir}误差，每个星座一个面板"
                              "\n（{rover}，{win}，绘出 {n} 个历元）",
        "title_rtk_fix_zoom": "RTK 浮点解与固定解的{dir}误差，每个星座一个面板"
                              "\n（{rover}，{win}，绘出 {n} 个历元）",
        "title_rtk_kalman_zoom": "RTK 浮点解：单历元最小二乘与卡尔曼滤波的{dir}误差，"
                                 "每个星座一个面板\n（{rover}，{win}，绘出 {n} 个历元）",
        # 窗口写成一句话，好让标题用它收尾。单列出来是因为整段回退时要用下面另一条。
        "zoom_win": "首历元后 {lo}-{hi} s，每 {step} 个历元取一点",
        "zoom_win_all": "整段",
        # 8-1b/c/d 的横轴范围，同样为了收尾。既不是窗口、也不是"整段"：那正是图 8-1 画
        # 出来的全部，而它比整个 run 短，所以这里必须点名截到哪，跟横轴标签（axis_sod_to）
        # 说的是同一件事。
        "zoom_win_span": "与图 8-1 同一段，截至 sod {to}",
        # 三个方向，以及同一张图每个面板都带的那条纵轴标签——一张图的面板是它的三个星座，
        # 所以同一张图的三条标签是同一句话。
        "dir_e": "东向",
        "dir_n": "北向",
        "dir_u": "天顶",
        "axis_rtk_zoom_err": "{dir}误差 [m]",
        # 每条曲线一行，用该曲线自己的颜色画。数值预先格式化好再传进来——位数取 2 到 4 位、
        # 跟着数据自己的量级走：两位对米级解正合适，而把 0.015 m 的固定解印成三个 0.00 就
        # 毫无用处——所以格式说明符里只会出现数字。
        "annot_rtk_zoom_stat": "偏差={bias}  σ={std}  RMS={rms} m",
        "note_rtk_zoom_fallback": "提示：图 {fig}：该 run 只有 {n} 个历元，短于 "
                                  "{lo}-{hi} s 的窗口；改为绘制整段",

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
        "rpt_rtk_title": "RTK 浮点解精度评估（相对参考坐标，{rover}）",
        "rpt_rtk_ref": "参考坐标 (ECEF): "
                       "{x:.4f} {y:.4f} {z:.4f} m  [{src}]",
        "rpt_rtk_epochs": "每个星座 {n} 个历元，共 {nmodes} 个星座",

        "rpt_rtk_head": "SPP/RTK 三维 RMS 比: {ratio}   （含偏移，见说明 2）",
        "rpt_rtk_c_sys": "星座",
        "rpt_rtk_c_system": "信号组合",
        "rpt_rtk_c_epochs": "历元数",
        "rpt_rtk_c_spp": "SPP 三维RMS",
        "rpt_rtk_c_rtk": "RTK 三维RMS",
        "rpt_rtk_c_ratio": "SPP/RTK 比",

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

        "rpt_rtk_fix_head": "【模糊度固定】ratio > {thr} 判为接受；两列 RMS 是同一批历元，"
                            "浮点解与固定解各算一次",
        "rpt_rtk_c_fixedrate": "固定率",
        "rpt_rtk_c_float": "浮点 三维RMS",
        "rpt_rtk_c_fixed": "固定 三维RMS",
        "rpt_rtk_c_worstfix": "已接受最坏",
        "rpt_rtk_c_gain": "提升",
        "rpt_rtk_c_ratiop50": "ratio 中位数",
        "rpt_rtk_note_fix":
            "说明 3 —— “固定 三维RMS”只统计被 ratio 检验接受的历元，“已接受最坏”是这批历元里"
            "最大的误差：若它是厘米级，说明整段没有一个固定错。两列缺一不可，因为固定解文件在"
            "被拒历元上写的仍是浮点解——不按阈值过滤就直接采用，会得到米级的解。",
        "rpt_rtk_note_bias":
            "说明 1 —— 偏差不是精度指标，而是基准偏移。SPPUCCodePhase::solve 用交给它的那个"
            "基准站坐标（默认是 RINEX 表头，给了 --base-xyz 就是那个值）建立基准站方程后直接"
            "返回（不再迭代），流动站则迭代到 |dxyz| < 0.1 m 即判收敛。因此每个历元的结果 = "
            "该基准坐标 + 流动站最后一次未收敛的改正数，最大 0.1 m 且整段近似为常数。σ 才是"
            "精度，偏差应理解为这一偏移量。这一条只说 RTK 那一列；SPP 那一列的偏移是它自己的"
            "误差，不是基准项（本数据 0.01 / 3.0 / 1.3 m，见第四节）。",
        "rpt_rtk_note_ratio":
            "说明 2 —— SPP/RTK 比值不是与参考坐标无关的数。两列都对同一个参考坐标度量，但 "
            "RTK 那一列是建立在这个基准站坐标之上的：双差把流动站解到基准站上去，所以那个坐标"
            "按构造进了 RTK 解。换一个参考去量（比如 RINEX 表头——本设备上它离天线 24.9 m），"
            "两列都会跟着动。比值带着这个依赖，第四节与图 8-1 把两列各自的偏移与残余分开来写。",
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
