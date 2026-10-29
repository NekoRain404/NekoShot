# Packaging NekoShot

## Arch Linux (local tree)

```bash
cd packaging
makepkg -si
```

`makepkg` builds from the checkout in the parent directory (`$startdir/..`).
`check()` runs the CTest suite.  The package installs the binary, desktop
file, D-Bus/systemd units, KConfig schema, notifications, translations, icon
and the `nekoshot(1)` man page.

## Release tarball / AUR

`make-release.sh` creates a reproducible source tarball with `git archive`,
prints its checksum, and shows the `source=()` / `sha256sums=()` lines to paste
into an AUR PKGBUILD:

```bash
./make-release.sh 0.10.3
```

For an AUR submission, replace the local build in `PKGBUILD` with:

```bash
source=("$pkgname-$pkgver.tar.gz::https://github.com/NekoRain404/NekoShot/archive/v$pkgver.tar.gz")
sha256sums=('<checksum from make-release.sh>')

build() {
  cmake -S "$srcdir/$pkgname-$pkgver" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}
```

Then regenerate metadata with `makepkg --printsrcinfo > .SRCINFO`.

## AUR release assets

Each GitHub release includes `nekoshot-<version>-aur.tar.gz`. It contains:

- `PKGBUILD` — `source=()` points at the release source tarball and carries its
  SHA-256; builds from `$srcdir/$pkgname-$pkgver`.
- `.SRCINFO` — generated with `makepkg --printsrcinfo`.

To update it for a new release, regenerate the source tarball with
`make-release.sh`, paste its checksum into the AUR PKGBUILD, refresh `.SRCINFO`
and rebuild the tarball.

## Notes

- `conflicts=('spectacle')`: both applications own the same desktop/D-Bus
  identifiers conceptually, so they should not be installed at once.
- `WITH_X11=OFF` is intentional; the KWin/XCB backends remain in the source
  but are not part of the niri package.
- Optional dependencies are runtime-only; the package advertises them but does
  not pull GStreamer/VA-API automatically.
