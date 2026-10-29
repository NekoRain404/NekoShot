# Live niri smoke test

`run-niri.sh [nekoshot-binary]` exercises the real session:

1. full-screen screenshot
2. region screenshot (needs `wtype`; otherwise skipped)
3. 3-second Screen recording when GStreamer or wf-recorder is present

It exits 77 when `NIRI_SOCKET` is unset or `grim`/`niri` are missing, which
CTest treats as a skip. Run it manually after installing a new build:

```bash
tests/integration/run-niri.sh ./build/bin/nekoshot
```
