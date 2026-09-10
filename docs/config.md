# Configuration

The configuration reference lives next to the files it describes:

**→ [`config/README.md`](../config/README.md)**

It covers the file format, both shipped profiles, every key with its type and
default, which keys are reserved and have no effect yet, and the one change
that will silently break the regression baseline if made without thinking.

Quick start:

```bash
./build/bin/parse_config config/spp.ini     # print every key and its parsed value
gnss spp config/spp.ini                     # run with a config
gnss spp config/spp.tuned.ini               # the experimental profile
```
