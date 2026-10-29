# Tracking upstream Spectacle

NekoShot is a fork of KDE Spectacle. This document records where the fork
diverges, how to merge upstream changes, and what to do when conflicts appear.

## Upstream remote

| Remote     | URL                                            |
| ---------- | ---------------------------------------------- |
| `upstream` | https://invent.kde.org/graphics/spectacle.git  |

Baseline: Spectacle **6.8.80**, commit `d3f3be5` (`SVN_SILENT made messages
(.desktop file) - always resolve ours`). NekoShot's first sync point is that
commit; later merges move the baseline forward.

```bash
git remote -v          # should list upstream -> invent.kde.org/graphics/spectacle.git
git fetch upstream
```

## Where NekoShot diverges

NekoShot replaces Spectacle's KWin/Wayland capture path with a niri backend
built on `grim` and niri's IPC, and renames the application identity.

| Area | Files | Nature of change |
| ---- | ----- | ---------------- |
| Niri backend | `src/Platforms/NiriCapture.{h,cpp}`, `NiriIpc.{h,cpp}`, `NiriPlatform.{h,cpp}`, `ImagePlatformNiri.{h,cpp}` | New, NekoShot-only. Do not accept upstream deletions of these files. |
| Image composition | `src/Platforms/ImageComposition.{h,cpp}` | New helpers that crop/scale/assemble niri output images. NekoShot-only. |
| Platform selection | `src/Platforms/PlatformLoader.cpp` | Chooses the niri image platform when the niri socket is available, falling back to the existing platforms. |
| KWin image platform | `src/Platforms/ImagePlatformKWin.{h,cpp}` | Kept for reference/compatibility but adjusted to share composition helpers. Upstream changes here should usually be taken, then re-checked against our helpers. |
| Capture windows | `src/Gui/CaptureWindow.{h,cpp}` and related QML | Uses a layer-shell overlay on niri instead of KWin capture windows. |
| Application entry point | `src/Main.cpp` | niri-specific startup and guards, application domain/desktop file name, D-Bus service registration. |
| Command line | `src/CommandLineOptions.{h,cpp}` | NekoShot-specific options/help strings; upstream option changes must be merged by hand. |
| Build identity | `CMakeLists.txt`, `src/CMakeLists.txt`, `icons/` | Project/version and target naming, QML URI `org.nekoshot.NekoShot.private`, renamed config `src/Gui/SettingsDialog/nekoshot.kcfg`, renamed icon assets. |
| Desktop identity | `desktop/` | `org.nekoshot.NekoShot.desktop.cmake`, `org.nekoshot.NekoShot.appdata.xml`, `nekoshot.notifyrc` replacing the `org.kde.spectacle` / `spectacle` files. |
| D-Bus identity | `dbus/` | `org.nekoshot.NekoShot.xml` and `app-org.nekoshot.NekoShot.service.in`; `SpectacleDBusAdapter` interface name `org.nekoshot.NekoShot`. |
| Packaging | `packaging/PKGBUILD` | Arch package; NekoShot-only. |
| Docs and assets | `docs/` (workspace plans/specs, `docs/upstream.md`) and `assets/` at the repository root (architecture, banner and flow SVGs) | NekoShot-only. Upstream `doc/` (handbook) is still shared and should be taken from upstream unless it references Spectacle-only behaviour. |
| Tests | `tests/NiriIpcTest.cpp`, `tests/NiriCaptureTest.cpp`, `tests/CMakeLists.txt` | NekoShot-only tests; keep the extra `ecm_add_test` targets when upstream edits `tests/CMakeLists.txt`. |

Note that many `src/Gui/*.qml` files differ from upstream by only one or two
lines (resource/import URI or context strings). Expect these to conflict on
almost every merge; re-apply the NekoShot-specific lines after taking the
upstream body.

## Translation domain

The translation domain is intentionally still `spectacle`
(`KLocalizedString::setApplicationDomain("spectacle")` in `src/Main.cpp`, and
`$podir/spectacle.pot` in `Messages.sh`). This keeps the existing `po/`
translations working without a mass rename. Do **not** change it while merging
upstream; take upstream `po/` updates as-is.

## Syncing with upstream

```bash
# 1. Fetch the latest upstream history
git fetch upstream

# 2. Create a dedicated sync branch from NekoShot's mainline
git switch -c sync/spectacle-<version> master

# 3. Merge the upstream point you want (branch or release tag)
git merge upstream/master
# or: git merge v6.8.80
```

Resolve conflicts in this order:

1. **Keep NekoShot platform code.** `src/Platforms/Niri*`,
   `ImageComposition.*`, the platform-selection logic in `PlatformLoader.cpp`,
   and NekoShot-only tests stay as they are. If upstream refactors
   `ImagePlatform`/`PlatformLoader` APIs, adapt the NekoShot side to the new
   signatures rather than dropping the niri backend.
2. **Keep NekoShot identity.** Desktop file, appdata, notifyrc, D-Bus XML and
   service file, icons, CMake target/QML-URI/config names, and
   `packaging/PKGBUILD`. Upstream renames or new files in `desktop/`/`dbus/`
   must be ported to the `org.nekoshot.NekoShot` naming.
3. **Take upstream UI and core changes.** `src/Gui/` (except the
   NekoShot-specific overlay bits in `CaptureWindow`), annotation/editing code,
   `ExportManager`, `OcrManager`, recording/video code, the handbook in `doc/`,
   and `po/` translations. Re-apply the one-line NekoShot QML adjustments
   afterwards (`git diff` against the previous NekoShot commit is the fastest
   way to find them).
4. **Merge `src/Main.cpp` and `src/CommandLineOptions.cpp` by hand.** Keep
   NekoShot's niri startup/guards and options, but adopt upstream's new
   options, help text, and startup fixes.
5. **Check newly added upstream files** for identity leaks (`org.kde.spectacle`
   strings, QML URIs, desktop file names) and for assumptions about KWin that
   the niri backend does not provide.

Then rebuild and test:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DWITH_X11=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Finally, run the application on niri and smoke-test capture (full screen,
active window, region), OCR, and export before committing the merge.

## Packaging check

After a merge, re-check `packaging/PKGBUILD`:

- dependency list still matches the CMake `find_package` calls;
- `pkgver` follows the upstream release being merged;
- build still succeeds with `-DWITH_X11=OFF`.

Validate the package against a complete tree before releasing:

```bash
cd packaging && makepkg -f --nodeps
pacman -Qlp nekoshot-*.pkg.tar.zst | grep -E 'bin/nekoshot|org.nekoshot.NekoShot.desktop'
```
