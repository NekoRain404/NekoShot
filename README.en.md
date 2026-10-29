<div align="center">
  <img src="assets/nekoshot-banner.svg" alt="NekoShot — Spectacle-grade screenshots and recording, natively on niri" width="100%">

  <h1>NekoShot</h1>

  <p><b>Spectacle-grade screenshots and screen recording, built for niri</b></p>

  <p>
    <a href="https://github.com/NekoRain404/NekoShot/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/NekoRain404/NekoShot?style=flat-square&color=0A84FF"></a>
    <a href="https://github.com/NekoRain404/NekoShot/actions/workflows/build.yml"><img alt="Build" src="https://img.shields.io/github/actions/workflow/status/NekoRain404/NekoShot/build.yml?branch=master&style=flat-square&label=build"></a>
    <img alt="License" src="https://img.shields.io/badge/license-GPL--2.0--or--later-8B5CF6?style=flat-square">
    <img alt="Platform" src="https://img.shields.io/badge/platform-niri%20%2F%20Wayland-22D3EE?style=flat-square">
  </p>

  <p><a href="README.md">简体中文</a> · English</p>
</div>

> **NekoShot is a downstream port of [KDE Spectacle](https://invent.kde.org/graphics/spectacle) 6.8.80.**
> It keeps Spectacle's overlay, region editor, annotations, OCR, export and viewer flow, and replaces the
> KWin-only capture layer with `grim` + niri IPC. Screen, window and region recording use niri's Mutter
> ScreenCast with GStreamer, falling back to `wf-recorder` only for cross-output regions or hosts without
> GStreamer. NekoShot is not an official KDE project and is not affiliated with KDE e.V.

NekoShot exists so niri users can press a key, select a region, annotate, copy or save — instead of
stitching together `grim | slurp | satty` for every capture.

## Screenshots

<p align="center">
  <img src="assets/screenshots/overlay.png" alt="Frozen layer-shell region overlay with annotation toolbar" width="49%">
  <img src="assets/screenshots/viewer.png" alt="NekoShot viewer with annotations and export actions" width="49%">
</p>
<p align="center"><sub><b>Left:</b> frozen layer-shell overlay, selection and annotation toolbar. <b>Right:</b> viewer, annotations and export.</sub></p>

## Workflow

<p align="center">
  <img src="assets/nekoshot-flow.svg" alt="Five-step flow from hotkey to export" width="100%">
</p>

## Highlights

- **All Spectacle capture modes**: region, all screens, current screen, active window, window under
  cursor, scaled all-screens, delay and pointer options.
- **Native layer-shell overlay**: frozen image, magnifier, keyboard nudging, selection memory,
  annotations, OCR and QR scanning from KQuickImageEditor / tesseract / Prison.
- **Recording**: Screen, window (picked with `niri msg pick-window`) and region. WebM/VP9+Opus,
  MP4/H.264+AAC, MP4/H.265+AAC, MP4/AV1+AAC; system audio, microphone or both; pointer via
  `cursor-mode`; low/medium/high quality presets (4/10/18 Mbps).
- **Robust recording**: fragmented MP4 survives crashes, an idle inhibitor prevents DPMS/lock
  interruptions, output folders are preflighted, and one-shot failures return exit code 1.
- **Fast hotkeys**: `--daemon` preloads QML; hotkey→overlay is ~0.25 s on the first press and
  ~0.11 s afterwards on the reference machine.

## Install

Download `nekoshot-<version>-1-x86_64.pkg.tar.zst` from
[Releases](https://github.com/NekoRain404/NekoShot/releases/latest):

```bash
sudo pacman -U nekoshot-0.10.3-1-x86_64.pkg.tar.zst
```

The package installs the binary, desktop file, D-Bus/systemd units, KConfig schema, notifications,
translations, icon and the `nekoshot(1)` man page. It conflicts with `spectacle`.

Build from source:

```bash
git clone https://github.com/NekoRain404/NekoShot.git
cd nekoshot
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Each release also ships `nekoshot-<version>-aur.tar.gz` with an AUR `PKGBUILD` and `.SRCINFO`
that build from the release source tarball. See [`packaging/README.md`](packaging/README.md).

Build dependencies: `cmake ninja extra-cmake-modules qt6-tools wayland plasma-wayland-protocols`.
Runtime dependencies are listed in the Chinese [README](README.md#安装); `wf-recorder`, GStreamer,
VA-API and tesseract language packs are optional.

## Quick start

Add the daemon to niri's startup and merge [`contrib/niri-binds.kdl`](contrib/niri-binds.kdl):

```kdl
spawn-at-startup "nekoshot" "--daemon"
```

Remove niri's default `Print { screenshot; }` to avoid a conflict. A one-shot invocation also works,
at the cost of a cold start (~0.4–0.6 s).

```text
nekoshot -r           region screenshot
nekoshot -m           current output
nekoshot -a           active window
nekoshot -u           pick a window with the pointer
nekoshot -f           all outputs
nekoshot -l           open the UI only
nekoshot -R r|s|w     record region / screen / picked window
nekoshot --stop-recording
nekoshot --daemon     stay resident for D-Bus requests
```

D-Bus interface: `org.nekoshot.NekoShot`, object path `/`, Spectacle-compatible method names
(`RectangularRegion`, `ActiveWindow`, `RecordRegion`, ...).

## Recording matrix

| Mode | Backend | Pointer | Audio | Notes |
|---|---|---|---|---|
| Screen | Mutter ScreenCast + GStreamer | yes | system/mic/both | focused output |
| Window | Mutter ScreenCast + GStreamer | yes | system/mic/both | click to pick |
| Region | ScreenCast + `videocrop` | yes | system/mic/both | region inside one output |
| Region fallback | `wf-recorder` | no | audio flags | cross-output or no GStreamer |

ScreenCast paths use `compositor + videorate` at 30 fps, so a static window keeps producing a normal
file instead of a one-frame clip. MP4 output is fragmented (`fragment-duration=1000`), so a crash or
SIGKILL leaves the written fragments playable.

## Performance

Reference: single 2880×1800 output at 175%, niri 26.04, release build.

| Scenario | Median |
|---|---:|
| cold `-b -f` | **445 ms** |
| cold `-b -a` | **598 ms** |
| daemon hotkey→overlay, first | **245–266 ms** |
| daemon hotkey→overlay, steady | **~115 ms** |

See [`docs/validation.md`](docs/validation.md) for the full matrix and
[`docs/robustness.md`](docs/robustness.md) for failure handling.

## Known limitations

- **Screenshot pointer**: niri's screencopy does not composite the cursor. The screenshot pointer
  option is disabled under niri; recordings still support the pointer through `cursor-mode`.
- **Multi-monitor / mixed DPI / negative origins**: geometry is computed per output and cropped in
  image-local coordinates, but only unit-tested; a real multi-monitor setup has not been validated.
- **Cross-output regions** fall back to `wf-recorder` (no pointer, non-fragmented file).
- **Window picker** only offers currently visible windows.
- **Recording start latency**: ScreenCast negotiation can make the file 4–10 s shorter than the wait;
  there is no "recording started" indicator yet.
- **A/V start alignment** has not been measured; total drift over 67 s was ~53 ms.
- **Forwarded CLI**: a request sent to a resident instance returns before the capture is saved;
  `--stop-recording` also returns before finalisation. Direct one-shot exit codes are reliable.
- **D-Bus** is open to same-session applications by default; disable with
  `[General] dbusRemoteCapture=false`.
- **Purpose/share** is removed; X11/KWin backends are retained but not built or regression-tested.
- The PKGBUILD currently builds from the local source tree; AUR publication is not done yet.

## Development

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

GitHub Actions builds in an Arch container and runs CTest. See
[`CONTRIBUTING.md`](CONTRIBUTING.md). All commits must be authored and committed by **NekoRain**
only; no tool or AI co-author trailers.

## Attribution

NekoShot is based on Spectacle 6.8.80 by Boudhayan Gupta, David Redondo, Noah Davis and the KDE
community. Thanks also to KQuickImageEditor, KDE Frameworks, Qt, niri, grim, wlroots, wf-recorder,
GStreamer/PipeWire and tesseract.

Licensed under **GPL-2.0-or-later**. NekoShot is not affiliated with KDE e.V.
