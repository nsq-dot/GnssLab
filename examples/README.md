# Examples

Small, interactive programs demonstrating individual parts of the library. They
read from standard input so you can try a value and see the result immediately,
which makes them useful for checking your understanding of a conversion before
trusting it inside a larger program.

None of them do any file I/O or need a dataset.

| Program | Demonstrates |
|---|---|
| `parse_opt` | Command-line argument parsing |
| `parse_config` | Reading a `key = value` configuration file |
| `gpst_to_utc` | GPS time → UTC, including leap seconds |
| `bdweek_to_commontime` | BeiDou week/second → `CommonTime`, and on to GPS and UTC |
| `jd2020_test` | BeiDou week/second → JD2020, and the round trip back |
| `ecef_enu_test` | ECEF ↔ geodetic conversion, and satellite elevation/azimuth |
| `sync_obs` | Epoch alignment between two receivers, including the case where the reference stream has already passed the epoch being sought (chapter 8.4, step 4) |
| `diff_station` | Between-station and between-satellite differencing: what `differenceStation` and `differenceSat` do to an equation system, and what rank deficiency looks like (chapter 8.3.1–8.3.2) |
| `mlambda` | MLAMBDA ambiguity resolution and the ratio test (chapter 8.3.5). Three cases: the notes' example 8-1, a ratio that fails the test, and a covariance no search can use |

`sync_obs` and `diff_station` read a small text description of their input from
standard input, and each falls back to a built-in example when given none — so
they still do no file I/O and need no dataset:

```bash
./build/bin/diff_station          # two examples: one well posed, one rank deficient
./build/bin/sync_obs              # a match, a skipped epoch, and running out of data
```

## Running

```bash
./build/bin/gpst_to_utc
# or, if the package is installed:
gnss ex gpst_to_utc
```

`parse_config` takes an optional path and defaults to `config/spp.ini`, so it
doubles as a way to check what the solver will read:

```bash
./build/bin/parse_config config/spp.ini
./build/bin/parse_config config/spp.tuned.ini
```

## Relationship to the course chapters

These began as the `exam-<chapter>.<section>-<topic>.cpp` teaching programs from
the *gnssLab-2.4* framework. The chapter correspondence is recorded in
[`docs/chapter-mapping.md`](../docs/chapter-mapping.md), since the filenames no
longer carry the chapter number.

The larger, non-interactive programs — the ones that read data files and produce
output — are in [`apps/`](../apps/).
