# Contributing to NekoShot

NekoShot is a downstream fork of KDE Spectacle for the niri compositor. The
upstream QML UI, annotation, OCR and export code is kept close to Spectacle;
platform work lives under `src/Platforms/`. Please keep both properties in
mind when sending changes.

## Build

Arch Linux example (install the `makedepends` from `packaging/PKGBUILD`):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_X11=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

A plain `cmake -S . -B build` now defaults to `WITH_X11=OFF`; KDE sessions
that need the KWin/X11 backends can opt in with `-DWITH_X11=ON`.

## Tests

- `ctest` runs the unit suite (`filename_test`, `appstreamtest`,
  `niri_ipc_test`, `niri_capture_test`, `clipboard_guard_test`,
  `wf_recorder_args_test`).
- Changes to `NiriIpc`, `NiriCapture`, `ImageComposition` or the argument
  builders must come with unit tests; do not add tests that require a live
  compositor or clipboard.
- Visual changes to the overlay/viewer need a real-display check at 100% and
  one fractional scale (the reference setup is a single 2880×1800 output at
  175%).

## Identity and commits

- Author and committer must be **NekoRain** only. Do not add tool, AI or
  co-author trailers.
- One logical change per commit; task commits use a `task N: <summary>`
  prefix, release fixes use `fix: <summary>`.
- Keep the Spectacle GPL-2.0-or-later headers on files you modify and add
  SPDX headers to new files.

## Architecture rules

- External commands only through `QProcess` with an argument list — never a
  shell string.
- Capture/recording differences belong in `src/Platforms/`; do not spread
  niri-specific branches through the QML or core.
- `NiriCapture` stays free of `KConfig`; read settings on the main thread.
- Temporary files use `QTemporaryFile` and are removed immediately.
- Do not change the retained Spectacle translation domain (`spectacle`) unless
  the whole `po/` tree is migrated in the same change.

## Upstream

See [docs/upstream.md](docs/upstream.md) for the baseline commit, the
divergence map and the merge order when syncing Spectacle.
