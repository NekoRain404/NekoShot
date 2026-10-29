# Changelog

All notable changes to NekoShot are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.10.3] - 2026-10-29

### Fixed

- **Negative-origin and mixed-DPI capture geometry**: the region overlay
  selection is now translated from logical global coordinates to image-local
  coordinates before cropping, using the captured image's logical origin. The
  overlay is only created on outputs that actually intersect the captured
  image. Negative-origin layouts (a monitor left of / above the primary one)
  are no longer rejected in region mode.
- **Window capture DPR**: window screenshots use the scale of the output that
  hosts the window's workspace (`niri msg workspaces`) instead of the focused
  output, which could belong to a different, differently scaled monitor.
- **Explicit grim scale**: every screen capture passes `-s <output scale>`.
  grim otherwise defaults to the greatest scale in the whole layout and would
  upscale a 100% monitor that sits next to a 175% one.
- **All screens (scaled)**: `AllScreensScaled` is advertised when more than one
  output is present and captures each output at logical resolution (`grim -s 1`),
  so `--scaled` works instead of silently producing an unscaled image.
- **Static-window recordings**: Screen/Window pipelines now feed the screencast
  through a live `compositor` before `videorate`, so an idle window keeps
  receiving repeated frames at 30 fps. Previously a static window could produce
  a one-frame, 0.03-second file because niri only pushes screencast frames on
  damage.
- `StopRecording` no longer asserts when it arrives during a pending window
  pick. A pending window picker is now cancelled by StopRecording even in
  daemon mode, and `Rendering` counts as recording so a second stop cannot
  quit the process while the encoder is still writing the MP4 index. D-Bus
  StopRecording also respects `dbusRemoteCapture`.
- Background/DBus capture and recording failures now make the one-shot process
  exit with code 1 instead of 0, so scripts can detect failure.
- Clipboard snapshot/restore is serialised within the process, so a delayed
  restore cannot overwrite a newer capture's clipboard state. The
  cross-process case remains best effort.
- Region cropping uses the output's reported physical mode size, removing the
  1 px mismatch between the selection and the recorded file (400x300 now
  records as exactly 700x525 at 175%). Selection edges that touch an output
  edge snap to the stream size, so a full-output region keeps its last
  physical column/row (2880x1800, not 2879x1799).
- Screenshot pointer is now treated as unsupported under niri: the option is
  disabled with an explanatory tooltip instead of silently doing nothing
  (recording still honours the pointer through `cursor-mode`).

### Added

- A `nekoshot(1)` man page, installed by CMake and included in the package.
- A GitHub Actions workflow (Arch container, build plus CTest) and a live niri
  smoke test (`tests/integration/run-niri.sh`, skipped without `NIRI_SOCKET`).
- Packaging helpers: `makepkg` `check()` runs CTest, `.SRCINFO` is generated,
  `make-release.sh` produces a source tarball and checksum, and
  `packaging/README.md` documents the AUR flow.
- A `dbusRemoteCapture` setting (default on) can disable third-party D-Bus
  capture requests while keeping niri key bindings and the CLI working.


- **Window recording picker**: `nekoshot -R w` now starts niri's
  `pick-window` and records the window the user clicks, instead of requiring
  the target window to be focused first. The pick is asynchronous, cancellable
  with Esc or `--stop-recording`, and a caller can bypass it by passing a
  `window_id` in the recording options.
- **Recording quality presets**: a new `Quality` setting (Low / Medium / High,
  default Medium) maps to 4 / 10 / 18 Mbps and is applied to `vah264enc`,
  `vah265enc`, `vaav1enc`, `x264enc`, `vp9enc` and `wf-recorder`'s ffmpeg
  encoder. The old fixed 16 Mbps / 12 Mbps hardware values are gone.
- **Per-mode format availability**: Window recordings validate the format
  against the GStreamer encoders before starting and fall back to a compatible
  format (H.264, then H.265/AV1, or VP9 for `.webm`) with a notification,
  instead of failing after the window has been picked.
- **Fragmented MP4 for ScreenCast recordings**: `mp4mux fragment-duration=1000`
  writes an index while recording, so a crash, SIGKILL, lock/DPMS stream error
  or full disk leaves the already-written fragments playable instead of a
  moov-less file. wf-recorder's fallback is not fragmented.
- **Idle inhibitor while recording**: the platform holds a logind `idle`
  inhibitor (ScreenSaver fallback) from Recording until finalisation, so DPMS
  or session idle does not cut a long recording off.
- **Recording preflight**: read-only output folders and less than 64 MB free
  space are rejected before a ScreenCast session starts; a successful encoder
  exit with a zero-byte file is reported as a failure.
- **Region recording through niri ScreenCast**: when the region fits inside one
  output and GStreamer is available, Region records that output through
  `RecordMonitor` and crops the stream with `videocrop`. This gives region
  recordings the pointer (`cursor-mode`), system/microphone audio, and the
  VA-API H.264/H.265/AV1 encoders instead of wf-recorder's software-only
  path. Regions crossing outputs, or hosts without GStreamer, still fall back
  to wf-recorder (no pointer).

### Changed

- **Startup and hotkey latency**: `--daemon` preloads the QML engine and the
  capture/annotation/viewer components, cutting the first hotkey→overlay from
  768 ms to ~245–266 ms and steady-state to ~106–127 ms. Single-output window
  captures take DPR from `QScreen` instead of two niri IPC calls, poll the
  screenshot file every 5 ms instead of 20 ms, and restore the clipboard in a
  short background task after niri's trailing write instead of blocking the
  capture for 150–300 ms (verified with 10 sentinel captures). QR scanning is
  skipped for background/DBus captures where its result is unused. Cold
  `-b -f` is now ~445 ms; `-b -a` ~598 ms.
- Translations moved from the `spectacle` domain to `nekoshot`; all 57 locale
  files were renamed, the template regenerated, and new strings merged (with
  Simplified Chinese translations added).
- The Qt Widgets surfaces (settings dialog, menus, tooltips, inputs, tabs and
  scrollbars) now use the same dark, rounded design tokens as the QML overlay
  through an application-wide palette and stylesheet.
- The annotation color popup gained a saturation/value field and a hue strip,
  so any color can be mixed in-surface without a Widgets color dialog.
- `--stop-recording` now waits for the encoder to flush instead of quitting the
  CLI instance immediately, which previously left long MP4 files without a
  `moov` atom. Finalization runs asynchronously with a 120 s watchdog, so the
  UI stays responsive while a long recording writes its index.
- The recording tray icon is destroyed with `SpectacleCore` instead of during
  static teardown, fixing a `KStatusNotifierItem`/`QEventLoopLocker` crash on
  exit after a recording.
- Recording documentation and validation records updated for the new pipeline.
  Region uses ScreenCast when possible; only its wf-recorder fallback lacks a
  cursor option.

## [0.10.2] - 2026-10-29

### Added

- A `nekoshot(1)` man page, installed by CMake and included in the package.
- A GitHub Actions workflow (Arch container, build plus CTest) and a live niri
  smoke test (`tests/integration/run-niri.sh`, skipped without `NIRI_SOCKET`).
- Packaging helpers: `makepkg` `check()` runs CTest, `.SRCINFO` is generated,
  `make-release.sh` produces a source tarball and checksum, and
  `packaging/README.md` documents the AUR flow.
- A `dbusRemoteCapture` setting (default on) can disable third-party D-Bus
  capture requests while keeping niri key bindings and the CLI working.


- **Screen recording through niri's Mutter ScreenCast**: `-R s` now uses
  `RecordMonitor` plus PipeWire/GStreamer instead of `wf-recorder`, sharing the
  cancellable pending-start, timeout and session handling with Window.
  `cursor-mode` honours the pointer setting. Region keeps using `wf-recorder`,
  and Screen falls back to it when the GStreamer consumer is unavailable.
- **Audio for Screen and Window**: system audio
  (`pulsesrc device=@DEFAULT_MONITOR@`) and microphone capture are encoded as
  Opus in WebM or AAC in MP4; enabling both mixes them with `audiomixer`.
  Missing `opusenc`, `avenc_aac`, `audiomixer` or `pulsesrc` fails with a clear
  message before a session is created instead of writing a file without sound.
- **30 fps screencast output**: the GStreamer pipeline uses
  `videorate ! video/x-raw,framerate=30/1`, so a static window still produces a
  correctly timed file.
- **H.265 and AV1 recording**: new `MP4_HEVC` and `MP4_AV1` formats use the
  VA-API hardware encoders `vah265enc`/`h265parse` and
  `vaav1enc`/`av1parse` with `mp4mux`; they are advertised when the full VA
  chain (with a `vainfo` encode profile) or the ffmpeg fallback is present. Screen/Window H.264 now prefers `vah264enc` and falls
  back to the software `x264enc`. Region HEVC uses wf-recorder's `libx265`;
  Region AV1 uses `libaom-av1` with `cpu-used=8` (slower software encoding)
  because `libsvtav1` fails with wf-recorder on the reference host.
- If GStreamer cannot build the selected format/audio pipeline, Screen falls
  back to wf-recorder.
- VA-API encoders are only considered available when `vainfo` reports a real
  H.264/HEVC/AV1 profile, not merely when the `vah*enc` element exists.
  Hardware encoders use an explicit 16000 kbps bitrate and VP9 uses
  `target-bitrate=12000000` to avoid the multi-hundred-megabit defaults.
- The settings video-format combo falls back to the first entry when the saved
  format is no longer supported.
- `effectiveFormat()` resolves explicit `.mp4` paths to the preferred MP4
  codec (H.264/H.265/AV1) instead of always defaulting to H.264.
- `gstArguments()` now builds video+audio pipelines and is unit-tested for
  None/System/Microphone/Both across VP9, H.264, H.265 and AV1.

### Changed

- Screen/Window H.264 prefers VA-API `vah264enc ! h264parse` and falls back
  to `x264enc tune=zerolatency speed-preset=ultrafast` from
  `gst-plugins-ugly`; MP4 audio requires `avenc_aac` from
  `gst-libav`.
- Window recording keeps the focused-window requirement and `window-id`
  handling; Screen uses the focused output's connector name.

### Notes

- Region recording is unchanged and still uses `wf-recorder`.
- The Screen/Window pipeline requires `gst-launch-1.0`, `gst-inspect-1.0`,
  `pipewiresrc`, `videoconvert` and `videorate`; the format-specific elements
  are `vp9enc`/`webmmux`, `vah264enc`+`h264parse`/`x264enc`+`mp4mux`,
  `vah265enc`+`h265parse`+`mp4mux`, or `vaav1enc`+`av1parse`+`mp4mux`.
  Audio modes additionally require the elements listed above.

## [0.10.1] - 2026-10-29

### Added

- A `nekoshot(1)` man page, installed by CMake and included in the package.
- A GitHub Actions workflow (Arch container, build plus CTest) and a live niri
  smoke test (`tests/integration/run-niri.sh`, skipped without `NIRI_SOCKET`).
- Packaging helpers: `makepkg` `check()` runs CTest, `.SRCINFO` is generated,
  `make-release.sh` produces a source tarball and checksum, and
  `packaging/README.md` documents the AUR flow.
- A `dbusRemoteCapture` setting (default on) can disable third-party D-Bus
  capture requests while keeping niri key bindings and the CLI working.


- Window recording on niri via `org.gnome.Mutter.ScreenCast` (`RecordWindow`),
  PipeWire and GStreamer (`vp9enc`/`webmmux`, VP9 WebM). H.264/MP4 requires
  `gst-plugins-ugly` (`x264enc`) and fails with a clear message otherwise.
  Window capture follows the focused window; `cursor-mode` maps to the pointer
  option. Audio is not yet supported for window recording.

### Fixed

- Window recording starts are cancellable: a pending PipeWire stream has a
  3-second timeout, stop requests cancel the pending start, and duplicate
  starts are rejected instead of leaking ScreenCast sessions.
- Daemon/D-Bus failures no longer open a blocking modal dialog; they use a
  notification and the D-Bus failure signal.
- Annotation color and font pickers now open inside the overlay/viewer
  (in-surface QML popups) instead of a Qt Widgets dialog hidden beneath the
  layer-shell overlay (also shipped in 0.10.0-2).

## [0.10.0] - 2026-10-28

First NekoShot release, based on KDE Spectacle 6.8.80.

### Added

- A `nekoshot(1)` man page, installed by CMake and included in the package.
- A GitHub Actions workflow (Arch container, build plus CTest) and a live niri
  smoke test (`tests/integration/run-niri.sh`, skipped without `NIRI_SOCKET`).
- Packaging helpers: `makepkg` `check()` runs CTest, `.SRCINFO` is generated,
  `make-release.sh` produces a source tarball and checksum, and
  `packaging/README.md` documents the AUR flow.
- A `dbusRemoteCapture` setting (default on) can disable third-party D-Bus
  capture requests while keeping niri key bindings and the CLI working.


- **niri capture backend** (`ImagePlatformNiri`): grim/wlr-screencopy for
  region, full desktop and current output; niri IPC `screenshot-window` for
  the active window and `pick-window` for window-under-cursor.
- **Layer-shell capture overlay**: real `wlr-layer-shell` surface on the
  Overlay layer with exclusive keyboard focus, no tiling, no window-open
  animation, no workspace switch.
- **Multi-output composition** with per-output DPR metadata and the shared
  `ImageComposition::combinedImage` helper.
- **Clipboard guard** around niri's window capture side effect: MIME
  snapshot/restore with a 32 MiB cap, bounded commands, deterministic 300 ms
  settle after a successful capture.
- **Resident daemon** (`--daemon`) with `KDBusService` single-instance,
  per-request state reset, forwarded-CLI semantics by flag, and cancel/
  failure window teardown.
- **D-Bus API** `org.nekoshot.NekoShot` (Spectacle-compatible methods plus
  `StopRecording`) and CLI options including `--daemon`, `--stop-recording`.
- **niri keybindings** in `contrib/niri-binds.kdl` and a README quick start.
- **Recording backend** (`VideoPlatformNiri`, wf-recorder) for Screen and
  Region; **experimental/deferred** in this release.
- **NekoShot identity**: `nekoshot` binary, `org.nekoshot.NekoShot` app/D-Bus
  ID, `nekoshotrc` config, icon, appdata, notifyrc, desktop entry.
- **Arch packaging** (`packaging/PKGBUILD`), branded README with SVG diagrams,
  design/plan/validation/upstream documentation.

### Changed

- Product identity and configuration renamed from Spectacle to NekoShot.
- `WITH_X11` defaults to OFF; the XCB backend is kept but not built.
- Negative-origin monitor layouts are rejected only for overlays that need
  logical-coordinate mapping; plain screen captures still work.
- `AllScreensScaled` is no longer advertised by the niri backend; Spectacle
  falls back to the unscaled all-screens mode.

### Fixed

- Fill, stroke and text color pickers now open an in-surface QML popup instead
  of a Qt Widgets `QColorDialog` that was mapped underneath the layer-shell
  overlay and looked frozen.
- Font picker uses the same in-surface popup for the same reason.

### Removed

- Upstream Spectacle HTML handbooks and localized man pages are no longer
  installed; the package contains only NekoShot content.
- KDE Purpose share menu (not compiled; no KDE services on niri).
- The KWin-only hard requirement on Wayland (niri sessions are accepted).
- Dead `kconf_update` scripts that targeted the old `spectaclerc`.

### Known limitations

- Window recording is not available in 0.10.0 (the KWin-private screencast
  backend cannot run on niri; no portal/Mutter client is integrated yet).
- Pointer inclusion may not appear in screenshots on niri because the
  compositor may not composite the cursor into screencopies.
- Window shadow/decorations cannot be toggled on niri.
- Negative-origin multi-monitor layouts are rejected for croppable overlays.
- Cold-start latency is above the original target; the resident daemon is the
  recommended hotkey setup.
- Translations keep the upstream `spectacle` domain so existing KDE
  translations continue to work.
