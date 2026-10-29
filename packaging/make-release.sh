#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

version="${1:?usage: make-release.sh <version, e.g. 0.10.3>}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/packaging/nekoshot-$version.tar.gz"
prefix="nekoshot-$version/"

git -C "$root" archive --format=tar.gz --prefix="$prefix" -o "$out" "v$version" 2>/dev/null \
  || git -C "$root" archive --format=tar.gz --prefix="$prefix" -o "$out" HEAD

sum=$(sha256sum "$out" | cut -d' ' -f1)
echo "wrote $out"
echo "sha256: $sum"
echo
echo 'source=("nekoshot-$pkgver.tar.gz::https://github.com/NekoRain404/NekoShot/archive/v$pkgver.tar.gz")' 
echo "sha256sums=('$sum')"
