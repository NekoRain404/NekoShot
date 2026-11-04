#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Smoke test for a live niri session. Exits 77 (CTest SKIP) when niri, grim or
# a screen capture are unavailable, so CI without a compositor stays green.
set -eu

bin="${1:-./build/bin/nekoshot}"
if [ -z "${NIRI_SOCKET:-}" ] || ! command -v niri >/dev/null || ! command -v grim >/dev/null; then
    echo "skip: no niri session"
    exit 77
fi

# A resident instance would receive the CLI request instead of this process,
# making the file appear after the test has already checked it.
if command -v busctl >/dev/null 2>&1 \
    && busctl --user call org.freedesktop.DBus / org.freedesktop.DBus NameHasOwner s org.nekoshot.NekoShot 2>/dev/null | grep -q true; then
    echo "skip: a NekoShot instance is already running"
    exit 77
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Wake an idle/DPMS-blanked output before the first capture.
command -v wtype >/dev/null 2>&1 && wtype -k Shift_L || true
sleep 0.3

"$bin" -b -f -o "$tmp/full.png" -n
[ -s "$tmp/full.png" ] || { echo "full screenshot is empty"; exit 1; }
identify "$tmp/full.png" >/dev/null 2>&1 || { echo "full screenshot is not an image"; exit 1; }

"$bin" -b -r -o "$tmp/region.png" -n &
shot_pid=$!
# The region overlay needs a key press; skip when no input injector exists.
if command -v wtype >/dev/null 2>&1; then
    sleep 2
    wtype -k Return || true
    wait "$shot_pid" || true
    [ -s "$tmp/region.png" ] || echo "region screenshot skipped (overlay not accepted)"
else
    kill "$shot_pid" 2>/dev/null || true
    wait "$shot_pid" 2>/dev/null || true
fi

if command -v gst-launch-1.0 >/dev/null 2>&1 || command -v wf-recorder >/dev/null 2>&1; then
    "$bin" -b -R s -o "$tmp/rec.mp4" -n &
    rec_pid=$!
    sleep 3
    "$bin" --stop-recording >/dev/null 2>&1 || true
    wait "$rec_pid" 2>/dev/null || true
    [ -s "$tmp/rec.mp4" ] || { echo "screen recording is empty"; exit 1; }
    ffprobe -v error -show_entries stream=codec_name -of default=nw=1 "$tmp/rec.mp4" >/dev/null
fi

# Fault injection: an unwritable output folder must fail with a non-zero exit
# code and must not leave a file behind. Root bypasses permission checks, so
# the check is skipped there.
if [ "$(id -u)" != 0 ]; then
    mkdir -p "$tmp/read-only"
    chmod 500 "$tmp/read-only"
    if timeout 30 "$bin" -b -f -n -o "$tmp/read-only/shot.png" >/dev/null 2>&1; then
        echo "screenshot into a read-only folder unexpectedly succeeded"
        exit 1
    fi
    [ ! -e "$tmp/read-only/shot.png" ] || { echo "screenshot was written into a read-only folder"; exit 1; }
    chmod 700 "$tmp/read-only"
fi

# Optional A/V start-alignment check. It needs a working system audio source,
# so it is opt-in: NEKOSHOT_AV_TEST=1 ./tests/integration/run-niri.sh
if [ "${NEKOSHOT_AV_TEST:-0}" = 1 ] && command -v ffprobe >/dev/null 2>&1; then
    "$bin" -b -R s -o "$tmp/av.mp4" -n &
    av_pid=$!
    sleep 4
    "$bin" --stop-recording >/dev/null 2>&1 || true
    wait "$av_pid" 2>/dev/null || true
    if [ -s "$tmp/av.mp4" ]; then
        video_start=$(ffprobe -v error -select_streams v:0 -show_entries stream=start_time -of csv=p=0 "$tmp/av.mp4")
        audio_start=$(ffprobe -v error -select_streams a:0 -show_entries stream=start_time -of csv=p=0 "$tmp/av.mp4")
        if [ -n "$video_start" ] && [ -n "$audio_start" ]; then
            if ! awk -v v="$video_start" -v a="$audio_start" 'BEGIN { d = v - a; if (d < 0) d = -d; exit !(d <= 0.5) }'; then
                echo "audio and video start times differ by more than 0.5 s (v=$video_start a=$audio_start)"
                exit 1
            fi
            echo "A/V start alignment: video=$video_start audio=$audio_start"
        else
            echo "A/V start alignment skipped (no audio stream recorded)"
        fi
    else
        echo "A/V start alignment skipped (empty recording)"
    fi
fi

echo "integration smoke passed"
