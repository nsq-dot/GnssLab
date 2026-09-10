# Tools

Helpers for converting RINEX files between their plain and Hatanaka-compressed
forms.

## What is here

| File | Purpose |
|---|---|
| `CRZ2RNX.BAT` | Windows: expand a `.crx` file to `.rnx` |
| `RNX2CRZ.BAT` | Windows: compress a `.rnx` file to `.crx` |
| `CRZ2RNX_linux` | POSIX wrapper for the same |

The decompressor executables themselves (`CRX2RNX`, `RNX2CRX`) are **not**
redistributed in this repository. They are the work of Y. Hatanaka and are
distributed by the Geospatial Information Authority of Japan (GSI); their
redistribution terms are not clearly established, so only the wrappers are
committed. See §3 of `../NOTICE`.

## Getting the binaries

Download RNXCMP from the GSI:

<https://terras.gsi.go.jp/ja/crx2rnx.html>

Then place the executables next to the wrapper scripts:

```
tools/CRX2RNX.exe     # Windows
tools/RNX2CRX.exe
tools/CRX2RNX         # Linux/macOS
tools/RNX2CRX
```

`CRZ2RNX.BAT` looks for `CRX2RNX.exe` in this directory first, then on `PATH`.
Once the binaries are present they are covered by `.gitignore`, so they will not
be committed by accident.

## Use

IGS distributes daily observation files Hatanaka-compressed, as `.crx` in the
long-name convention (or `.d` in the short-name one). Expand before use, since
the reader expects uncompressed RINEX:

```bash
tools/CRZ2RNX.BAT data/WUH200CHN_R_20250010000_01D_30S_MO.crx
# writes data/WUH200CHN_R_20250010000_01D_30S_MO.rnx
```

Note that `data/README.md` explains how to obtain the already-expanded file for
the sample dataset, so this is only necessary for new data.

## Compression

Compression is worth it for archival — a RINEX observation file is typically
3–5× smaller — but development data should stay expanded, since the readers do
not decompress on the fly.
