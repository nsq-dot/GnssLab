# Configuration

The configuration reference lives next to the files it describes:

**→ [`config/README.md`](../config/README.md)**

It covers the file format, every shipped profile (`spp.ini`, `spp.tuned.ini`,
`cs.ini`, `bias.ini`, `eph.ini`), each key with its type and default, which keys
are reserved and have no effect yet, and the changes that will silently break
the regression baseline if made without thinking.

Quick start:

```bash
./build/bin/parse_config config/spp.ini     # print every key and its parsed value
gnss spp config/spp.ini                     # run with a config
gnss spp config/spp.tuned.ini               # the experimental profile
```
