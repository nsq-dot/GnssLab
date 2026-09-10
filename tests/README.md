# Tests

```bash
python tests/test_plot_utils.py             # no data, no build needed
python tests/test_baseline_numerics.py      # no data needed (uses tests/baseline/)
python tests/test_regression_pipeline.py --required   # needs build + data/sample/
```

All three also run standalone or under pytest:

```bash
python -m pytest tests/ -q
ctest --test-dir build --output-on-failure     # runs baseline_numerics
```

| File | Needs | Covers |
|---|---|---|
| `test_plot_utils.py` | nothing | Coordinate conversions, statistics, and the string tables |
| `test_baseline_numerics.py` | nothing | The frozen baseline: hashes, statistics, cross-file consistency |
| `test_regression_pipeline.py` | a build + `data/sample/` | The whole chain, end to end |

The first two run on a bare checkout. That is deliberate: a contributor who has
cloned the repository but not downloaded 500 MB of data should still get a
meaningful green result rather than an error. The third **skips** with a clear
message when the sample or the binary is missing, and fails only under
`--required` — which is what CI uses, so a missing sample can never silently
pass there.

## What the tests are actually protecting

**`test_plot_utils.py`** covers the analysis code, including the conversion from
ECEF offsets to local ENU, checked against a rotation built independently in the
test rather than against the implementation. It also asserts that the English
and Chinese string tables have identical key sets and identical format
placeholders — a mismatch there produces a `KeyError` in one language only, at
plot time.

**`test_baseline_numerics.py`** guards `tests/baseline/`. It checks the SHA-256
of both files first, because every other regression claim is measured against
them: if the baseline drifts, a passing regression test means nothing. It then
re-derives the statistics the README quotes, and checks that the two output
files agree to within the 3-decimal rounding of the `.spp.out` (0.000499 m) —
which would catch outputs from two different runs being compared.

**`test_regression_pipeline.py`** runs the built solver on `data/sample/` and
requires **byte-identical** output against the baseline. Exact comparison is
possible here because both sides are written with `fixed`/`setprecision`, and it
is the strongest available statement that a change altered nothing numerically.

If this fails after a change, check `cutOffElevation` and `noiseGPSCode` in
`config/spp.ini` *first*. Those two values sit in the baseline's definition, and
an unintended change to them moves the solution in a way that looks exactly like
a code regression. See `config/README.md`.

## The baseline

`tests/baseline/` holds two files, produced by the original implementation
before the restructure and frozen:

| File | SHA-256 (first 16) | Rows |
|---|---|---|
| `WUH2_20250101_DUAL_IF.spp.out` | `092efbfd74e4d9f6` | 63 |
| `WUH2_20250101_pos_vel.out` | `61a19f2bc0233c9d` | 64 (1 header) |

Station WUH2, 2025-01-01, GPS + BeiDou, 30 s, epochs 00:00:00–00:31:00.

They are covered by a `-text` rule in `.gitattributes`. Without it, git's
`text=auto` would rewrite their CRLF line endings on checkout and the hashes
would stop matching — so if you ever add another baseline, add the rule too.

## Re-baselining

Only if a change is *intended* to alter the solution:

1. Run the solver on the full dataset, or on `data/sample/`.
2. Confirm the difference is the intended one — not a side effect.
3. Replace `tests/baseline/`, update the hashes and expected statistics in
   `test_baseline_numerics.py`, and update the figures and numbers quoted in the
   README.
4. Say so in the commit message, and explain why the numbers moved.
