# gnss_plot

Accuracy analysis and plotting for the GnssLab C++ engine, plus the unified
`gnss` command-line interface.

This package reads the text output that `apps/spp_if` writes, and turns it into
a console accuracy report and four figures. It does not do any GNSS estimation
itself — the numerics live in the C++ engine, and the interface between the two
is a pair of plain text files (documented in `../docs/data-format.md`).

## Install

```bash
pip install -e python/          # from the repository root
```

That registers the `gnss` console script. If you would rather not install the
package, the module entry point always works:

```bash
python -m gnss_plot --help
```

Both need `PYTHONPATH=python/src` when run without installing.

## Use

The intended first command needs no arguments and no downloaded data:

```bash
gnss demo
```

For real work:

```bash
gnss build                      # configure and build the C++ targets
gnss spp   config/spp.ini       # run the solver
gnss plot  --rinex data/WUH200CHN_R_20250010000_01D_30S_MO.rnx --out-dir output
gnss run   config/spp.ini       # spp + plot in one go
```

The plotting step is also usable on its own against an existing run:

```bash
python -m gnss_plot plot --rinex data/WUH200CHN_....rnx \
                         --out-dir output --lang en
```

`--lang zh` switches the figures and report to Chinese. English is the default
because the figures are part of the repository's public documentation.

### As a library

```python
import sys; sys.path.insert(0, "python/src")
import gnss_plot as gp

ref = gp.read_approx_position("data/WUH200CHN_R_20250010000_01D_30S_MO.rnx")
sod, X, Y, Z = gp.load_spp_xyz("output/WUH200CHN_....rnx_DUAL_IF.spp.out")
dE, dN, dU = gp.enu_position_error(X, Y, Z, ref)

gp.figures.configure(lang="en", backend="Agg")
gp.fig_pos_horizontal(dE, dN, "horizontal.png")
```

Two things are deliberately *not* done at import time: matplotlib is not forced
to a backend, and no font is installed globally. Both would break notebook or
interactive use. Call `figures.configure()` when you want them.

## Module layout

| Module | Contents |
|---|---|
| `io.py` | Readers for the solver output, the RINEX header, the manifest, and the chapter-7 cycle-slip output |
| `coords.py` | ECEF ↔ geodetic, and ECEF deltas → local ENU |
| `stats.py` | bias / sigma / RMS statistics and the console report |
| `cycleslip.py` | Reading a whole cycle-slip detector run, and scoring it against injected truth |
| `figures.py` | The `fig_*` plotting functions and the matplotlib setup |
| `_strings.py` | Every user-visible string, in English and Chinese |
| `_find.py` | Locates CMake, Ninja, and the compiled executables |
| `cli.py` | Argument parsing and the subcommands |

`cycleslip.py` is separate from `io.py` on purpose: `io` reads one file into
arrays, `cycleslip` walks a directory and decides what the numbers mean. Both the
`gnss cs-plot` command and `scripts/check_cycle_slips.py` use it, so the figure
and the printed scoreboard cannot drift apart.

## Statistics conventions

**bias** is the mean of the series. **sigma** is its standard deviation with
`ddof=1` — this measures *precision*, how repeatable the solution is, and is
unaffected by a constant offset. **RMS** is the root-mean-square about the
reference value, so it includes any systematic offset and is always ≥ sigma.

For velocity the static-station truth is exactly zero, so bias and RMS coincide
in meaning. For position, the reference is the RINEX header
`APPROX POSITION XYZ`, which is itself only metre-to-decametre accurate — so the
position RMS here combines repeatability with a reference-coordinate offset and
is **not** absolute accuracy. The main README states this, and any rigorous
accuracy claim needs an IGS `.snx` truth coordinate.

## Requirements

Python 3.9+, `numpy`, `matplotlib`. See `requirements.txt`.
