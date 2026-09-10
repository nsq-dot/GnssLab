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
