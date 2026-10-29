# NekoShot recording report - Screen ScreenCast + A/V

> **0.10.3 update:** Region now prefers ScreenCast + `videocrop` (pointer, audio,
> VA-API) and only falls back to wf-recorder for cross-output regions or
> hosts without GStreamer. The 0.10.2 notes below describe the original
> Screen/Window implementation.

## Changes

- `supportedRecordingModes()`: GStreamer available -> `Screen | Window | Region`;
  unavailable -> `Screen | Region` (Screen falls back to wf-recorder). Region is
  always wf-recorder.
- `startRecording()`:
  - Screen obtains the connector from `options["screen"]` or
    `NiriIpc::focusedOutput().name` and calls
    `Session.RecordMonitor(connector, props)`.
  - Window keeps `RecordWindow` + `window-id`.
  - Both share `startScreencastRecording()`, pending/generation state, the 3 s
    `PipeWireStreamAdded` timeout, cancellable stop and session cleanup.
  - `cursor-mode` comes from `includePointer` for Screen and Window.
- Audio:
  - Settings are read on the main thread and mapped to
    `AudioMode::{None,System,Microphone,Both}`.
  - WebM uses `opusenc`, MP4 uses `avenc_aac`; Both uses
    `audiomixer name=amix`; system audio uses
    `pulsesrc device=@DEFAULT_MONITOR@`, microphone uses the default `pulsesrc`
    (or an explicit `device=` when supplied).
  - Missing `pulsesrc`/`opusenc`/`avenc_aac`/`audiomixer` fails before a
    ScreenCast session is created.
  - `isRecordingAudio()` returns true for Screen/Window when either setting is
    enabled.
- Video: `pipewiresrc do-timestamp=true ! videoconvert ! videorate !
  video/x-raw,framerate=30/1 ! <encoder>`. H.264 uses
  `x264enc tune=zerolatency speed-preset=ultrafast` + `mp4mux`.
- `gstArguments()` is a pure builder taking mandatory `GstCapabilities` plus
  `AudioMode`/microphone, unit-tested for None/System/Microphone/Both x
  VP9/H264 and capability gates.

## Tests

- `cmake --build build -j 8`: OK.
- `ctest --test-dir build --output-on-failure`: 6/6 passed.
- New tests: `testGstVp9Arguments`, `testGstVp9AudioArguments`,
  `testGstH264Arguments` (all four audio modes plus explicit microphone),
  `testGstCapabilityGates`, `testScreencastStartGuard`,
  `testScreencastPendingGeneration`.

## Manual A/V evidence (config restored afterwards)

Screen, system audio, VP9/WebM (`Screencast_20261029_124501.webm`):

```
vp9 video 2880x1800; opus audio 48000 Hz mono; duration 3.133333; size 314027
```

Window, system audio, VP9/WebM (`Screencast_20261029_124513.webm`):

```
vp9 video 1409x1759; opus audio 48000 Hz mono; duration 3.133333; size 667957
```

Screen, system audio, H.264/MP4 (`Screencast_20261029_124617.mp4`):

```
h264 video 2880x1800; aac audio 44100 Hz stereo; duration 3.300000; size 718329
```

Audio disabled, Screen VP9 (`Screencast_20261029_124633.webm`):

```
vp9 video only; duration 3.233333; size 607804
```

Microphone only (`Screencast_20261029_124650.webm`):
`vp9 + opus 48000 Hz mono`, 3.133333 s.

System + microphone via `audiomixer` (`Screencast_20261029_124705.webm`):
`vp9 + opus 48000 Hz mono`, 2.066666 s.

Videorate check: `avg_frame_rate=30/1`; 3.13 s files contain 94-97 video
frames, the MP4 99 frames.

## Unverified / caveats

- ffprobe proves the audio tracks exist and decode; it does not prove the
  captured content is audible or correctly mixed.
- Long-duration (>1 min) A/V sync/drift and CPU load under videorate were not
  measured.
- Only the focused output was exercised for Screen; selecting a non-focused
  connector through `options["screen"]` was not tested end-to-end.
- The GStreamer-unavailable fallback path (Screen -> wf-recorder, Window
  hidden) is covered by capability/unit logic, not by uninstalling packages.
- Region recording was left untouched and was not re-recorded in this round.
- The temporary config edits (`videoRecordSystemAudio`,
  `videoRecordMicrophone`, `preferredVideoFormat`) were restored from
  `/tmp/nekoshotrc.backup`; no gst process or ScreenCast session remained.

---

# H.265 / AV1 recording report

## Changes

- `VideoPlatform::Format`: added `MP4_HEVC = 1 << 4` and `MP4_AV1 = 1 << 5`.
  Both map to the `mp4` extension and support audio; `.mp4` still defaults to
  `MP4_H264` when no preference is known.
- `VideoFormatModel`: added `MP4/H.265` and `MP4/AV1` entries.
- `VideoPlatformNiri`:
  - `supportedFormats()` advertises HEVC when
    `vah265enc`+`h265parse`+`mp4mux` exist and AV1 when
    `vaav1enc`+`av1parse`+`mp4mux` exist; H.264 stays when `x264enc` or
    `vah264enc` is available.
  - `gstArguments()`: HEVC `vah265enc ! h265parse ! mp4mux`, AV1
    `vaav1enc ! av1parse ! mp4mux`, H.264 prefers `vah264enc ! h264parse` and
    falls back to `x264enc` (h264parse is required to feed mp4mux, discovered
    during validation). AAC for all MP4 formats, Opus for WebM,
    do-timestamp/videorate kept.
  - `effectiveFormat()` uses the preferred MP4 codec when the explicit path's
    extension matches the preferred format's extension.
  - wf-recorder (`argumentsFor`): HEVC `-c libx265`, AV1 `-c libsvtav1`;
    missing ffmpeg encoders produce a clear failure.
- Version/docs: 0.10.2 in CMake/PKGBUILD/README badge/appdata; README and
  CHANGELOG describe H.265/AV1, VA-API, and the H.264 software fallback.

## Probes on this host

`vah264enc`, `h264parse`, `vah265enc`, `h265parse`, `vaav1enc`, `av1parse`,
`mp4mux`, `x264enc` all present; ffmpeg reports `libx265`, `libsvtav1`,
`libx264` and `libvpx-vp9`.

## Tests

- `cmake --build build -j 8`: OK.
- `ctest --test-dir build --output-on-failure`: 6/6 passed.
- New unit tests: HEVC/AV1 x None/System/Microphone/Both gst argument lists,
  H.264 VA-API preference plus x264 fallback, capability gates, wf-recorder
  HEVC/AV1 arguments, and the effectiveFormat preference matrix.

## ffprobe evidence (config restored afterwards)

HEVC Screen with system audio (`Screencast_20261029_125956.mp4`):

```
hevc video 2880x1800; aac audio 44100 Hz stereo; duration 3.000000; size 88916032
```

HEVC Window with system audio (`Screencast_20261029_130024.mp4`):

```
hevc video 1409x1759; aac audio 44100 Hz stereo; duration 3.093220; size 32559816
```

AV1 Screen with system audio (`Screencast_20261029_130039.mp4`):

```
av1 video 2880x1800; aac audio 44100 Hz stereo; duration 3.133333; size 31112720
```

AV1 Window with system audio (`Screencast_20261029_130150.mp4`):

```
av1 video 1409x1759; aac audio 44100 Hz stereo; duration 3.233333; size 15576064
```

H.264 via `vah264enc ! h264parse` with audio (`Screencast_20261029_130459.mp4`):

```
h264 video 2880x1800; aac audio 44100 Hz stereo; duration 2.966667; size 10202998
```

Region HEVC through wf-recorder with the backend's `-c libx265` args
(`/tmp/region-hevc.mp4`): `hevc video, duration 2.853433`, 125885 bytes.

## Unverified / caveats

- Region HEVC was verified by running wf-recorder with the exact codec
  arguments the backend produces, not by driving the NekoShot selection
  overlay.
- Region AV1 currently fails on this host: wf-recorder + `libsvtav1` reports
  `Error setting encoder parameters: bad parameter (0x80001005)` and
  `avcodec_open2 failed: Invalid argument`, with and without `--no-dmabuf`.
  NekoShot passes `-c libsvtav1` as requested and would surface this failure.
- Explicit `.mp4` preference resolution is unit-tested, not exercised through
  a manual file-path recording.
- The new format entries were not clicked through the settings UI in this
  round.
- Long-duration A/V sync, bitrate quality and CPU/GPU load for HEVC/AV1 were
  not measured; the VA-API HEVC files are notably large at default settings.

---

# Codec hardening report (H.265 / AV1)

## Changes

- **Region AV1** now uses wf-recorder `-c libaom-av1 -p cpu-used=8`; the
  `libsvtav1` path and the "AV1 region unsupported" rejection were removed.
  `encoderAvailability()` probes `libaom-av1`, and a missing encoder fails with
  a clear message via the pure `wfRecorderCodecAvailable()` gate.
- **VA-API profile probe**: `vainfo` is run once (QProcess, 1.5 s timeout) and
  parsed by `VideoPlatformNiri::parseVaProfiles()` for
  `VAProfileH264Main/High`, `VAProfileHEVCMain` and
  `VAProfileAV1Profile0`. `gstVah264Available()`, `gstVah265Available()` and
  `gstVaav1Available()` now require both the GStreamer element and the real VA
  profile. `supportedFormats()` advertises HEVC when ffmpeg has `libx265` or
  the full VA chain exists, and AV1 when ffmpeg has `libaom-av1` or the full VA
  chain exists. H.264 keeps `x264enc` or the VA chain.
- **Bitrate caps**: `vah264enc`, `vah265enc` and `vaav1enc` use
  `bitrate=16000` (kbps, verified property range 0-2048000); `vp9enc` uses
  `target-bitrate=12000000` (bit/s, verified range).
- **VideoPlatform.h** no longer claims HEVC is unconditionally smaller; it
  documents the explicit bitrate cap and driver-dependent behavior.
- **VideoFormatComboBox**: a missing saved format falls back to the first row
  instead of leaving the combo empty / writing `NoFormat`.
- README, CHANGELOG and `docs/validation.md` updated; the 0.10.1 validation
  section is marked as an old record and a 0.10.2 section was added.

## Tests

- `cmake --build build -j 8`: OK.
- `ctest --test-dir build --output-on-failure`: 6/6 passed.
- New/updated tests: exact VA bitrate arguments, libaom-av1 Region arguments,
  `parseVaProfiles()` (including HEVC Main vs Main10 exact matching),
  `wfRecorderCodecAvailable()` (libaom missing -> false), HEVC/AV1
  `mp4mux=false` / `avencAac=false` gates, and the effectiveFormat matrix.

## File-size comparison (3 s, system audio on)

| Recording | Before bitrate cap | After `bitrate=16000` |
|---|---|---|
| Screen HEVC 2880x1800 | 88,916,032 B | 5,195,242 B |
| Window HEVC 1409x1759 | 32,559,816 B | 5,443,704 B |
| Screen AV1 2880x1800 | 31,112,720 B | 5,670,292 B |
| Window AV1 1409x1759 | 15,576,064 B | 5,071,092 B |

ffprobe after the change:

```
Screencast_20261029_131600.mp4: hevc 2880x1800 + aac 44100 Hz stereo, 2.933333 s
Screencast_20261029_131632.mp4: hevc 1409x1759 + aac 44100 Hz stereo, 3.093220 s
Screencast_20261029_131645.mp4: av1  2880x1800 + aac 44100 Hz stereo, 3.833333 s
Screencast_20261029_131658.mp4: av1  1409x1759 + aac 44100 Hz stereo, 3.533333 s
```

Region AV1 via libaom (`/tmp/region-aom.mp4`): `av1 700x700`, 2.738767 s,
49,273 B.

## Unverified / caveats

- Region AV1 was validated by running wf-recorder with the backend's exact
  arguments, not through the NekoShot selection overlay.
- `libaom-av1` is a software encoder: CPU load and encode latency are much
  higher than VA-API; only a short clip was measured.
- The VA-profile fallback (element present but no profile) is unit-tested via
  `parseVaProfiles()`; a real driver without those profiles was not available
  to test end-to-end.
- Bitrate quality at 16 Mbps was not compared subjectively; file sizes and
  decode were checked.
- The 0.10.2 package was not rebuilt, only the PKGBUILD version bumped.

---

# 0.10.3 additions - window picker, static frames, capture geometry

## Window picker

- `VideoPlatformNiri::startRecording()` accepts an optional `window_id` in the
  options map. Without it, Window mode starts `niri msg --json pick-window` in a
  member `QProcess` and returns; the finished handler parses the JSON, injects
  the id and re-enters `startRecording()`.
- The pick blocks a second start (`m_windowPickPending`), has a 10 minute
  timeout, and is cancelled by `finishRecording()` (Esc or `--stop-recording`).
- `startScreencastRecording()` now receives the window id explicitly instead of
  reading `NiriIpc::focusedWindow()`, so the window no longer has to be focused
  first.
- ScreenCast availability is checked before the picker opens, so a host without
  GStreamer fails immediately instead of asking for a window it cannot record.

## Static frame pacing

- Pipeline shape is now
  `compositor name=comp background=black ! videoconvert ! videorate !
  video/x-raw,framerate=30/1 ! <encoder> ...` with
  `pipewiresrc path=N do-timestamp=true ! videoconvert ! comp.sink_0` as a
  second chain (appended after the audio branches when audio is enabled).
- niri only pushes a screencast frame on damage. `compositor` is a live
  aggregator that repeats the last buffer of an idle pad at the negotiated
  rate, so a static window produces a normal 30 fps file instead of a
  one-frame, 0.03 s file. Measured: 3.4 s / 102 frames on a static tiled
  window; 2.8 s / 84 frames for Window H.264 + AAC.

## Capture geometry

- `NiriCapture::grimArguments()` takes an explicit scale and emits
  `-s <scale>`; `captureOutput()` passes the output's own scale, or `1` for
  `AllScreensScaled`.
- `ImagePlatformNiri` advertises `AllScreensScaled` only with more than one
  QScreen and re-emits `supportedGrabModesChanged` on screen add/remove.
- `ImageMetaData` gained `hasLogicalXY()`, `logicalRect()` and
  `imageLocalRect()`. `SpectacleCore` subtracts the base image's logical origin
  before `cropCanvas()` and filters overlay screens by the image's logical
  rectangle, which removes the negative-origin rejection.
- Window capture resolves DPR through `NiriIpc::outputForWindow()`
  (`niri msg workspaces` -> output), with the focused output and the primary
  screen as fallbacks.

## Verification

- `ctest`: 6/6; new tests `testWorkspaces`, `testWindowWorkspaceId`,
  `testImageLocalRect`, `testCombineMixedDprNegativeOrigin`, and updated gst
  argument lists including the compositor/source split.
- Manual: region 400x300 selection -> 700x525 PNG; empty selection ->
  2880x1800; `--scaled` -> 1646x1029; Window picker -> 1409x1759 H.264+AAC;
  Esc and `--stop-recording` both cancel the pick without leftovers.

## 0.10.3 quality presets and long-run stop

- `VideoPlatformNiri::bitrateKbpsForQuality()` maps the new
  `Settings::videoQuality` enum to 4000 / 10000 / 18000 kbit/s (Low / Medium /
  High, default Medium). The value is forwarded as
  `bitrate=<kbps>` to `vah264enc`/`vah265enc`/`vaav1enc` and `x264enc`,
  `target-bitrate=<bps>` to `vp9enc`, and `-p b=<bps>` to wf-recorder.
- `gstArguments()` and `argumentsFor()` take the bitrate as a parameter so the
  builders stay pure; `m_pendingBitrateKbps` carries it from start to the
  delayed PipeWire stream.
- `finishRecording()` no longer blocks the GUI thread in
  `waitForFinished()`. It sets `Rendering`, sends SIGINT and lets the finished
  handler finalize the container; a 120 s watchdog kills a stuck encoder.
- The `--stop-recording` activation path returns after `finishRecording()` and
  waits for `recordingSaved` before emitting `allDone`; the previous immediate
  `allDone` killed gst-launch before mp4mux wrote its index (moov atom).
- `~SpectacleCore()` resets the static recording tray icon while
  QCoreApplication is alive, fixing a crash in `KStatusNotifierItem`'s
  `QEventLoopLocker` during static destruction.

### Measured on the reference host

- 67 s Screen H.264 + AAC, Medium: video 67.67 s / 2030 frames, audio
  67.61 s, drift ≈ 53 ms; gst-launch ≈ 38–42% of one core, RSS ≈ 103 MB;
  file 133 MB.
- Medium preset: 4.47 s H.264, 9.5 Mbps, 5.3 MB. High: 4.57 s H.264,
  17.0 Mbps, 9.7 MB.
- Region VP9 (400×300 logical) with `-p b=10000000`: 700×524 WebM written and
  decodable.
- CLI `--stop-recording` on a 6 s recording now produces a valid MP4
  (5.49 s) instead of a moov-less file.

## 0.10.3 Region via ScreenCast + videocrop

- `regionCaptureFor(region, outputs)` finds the single output fully containing
  the logical region and returns the connector plus physical `QMargins`
  (`left/top/right/bottom`) for `videocrop`. It uses the output's
  `modes[current_mode]` physical size (falling back to `qRound(logical * scale)`
  for rotated outputs), so unrotated recordings match the selection exactly.
- `startRecording()` builds a `RegionCapture` before the encoder decision. If
  the region is contained in one output and `gstArguments()` is buildable, it
  calls `startScreencastRecording(Region, ...)` with `RecordMonitor` and the
  crop margins instead of wf-recorder. Region then gets `cursor-mode`,
  `pulsesrc` audio and the VA-API encoders. Cross-output regions and hosts
  without GStreamer keep the wf-recorder path.
- `gstArguments()` inserts `videocrop left= right= top= bottom=` between
  `videoconvert` and `videorate` only when margins are non-zero.
- `m_pendingCropMargins` carries the crop through the delayed PipeWire stream.

### Evidence

- Region H.264 (400x300 logical at 1.75): MP4 701x526, 3.9 s / 117 frames.
- Region with pointer: extracted frame shows the cursor inside the cropped
  area, proving `cursor-mode` reaches region recordings.
- Region H.264 + AAC: 701x526 video 2.83 s, AAC 44.1 kHz stereo 2.72 s.
- Region AV1 with `preferredVideoFormat=32` (MP4_AV1): AV1 701x526, 2.87 s,
  6.5 Mbps, encoded by `vaav1enc` instead of the old libaom software path.
- Region crop frame inspection found no overlay dim/toolbar inside the crop.

## 0.10.3 robustness notes

- MP4 ScreenCast output is fragmented (`mp4mux fragment-duration=1000`) so a
  crash/SIGKILL/lock-screen stream error leaves the written fragments playable.
- `IdleInhibitor` holds a logind `idle` inhibitor (ScreenSaver fallback) from
  Recording until finalisation.
- Output folders are rejected when read-only or below 64 MB free; an encoder
  exit with a zero-byte file is treated as a failure.
- wf-recorder fallback is still non-fragmented; see
  [robustness.md](robustness.md) for the full failure matrix.
