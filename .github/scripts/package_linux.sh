#!/usr/bin/env bash
# Lays the built client out the way a Linux desktop expects it, then wraps
# that tree in a tarball with both ways of installing it: install.sh for any
# distribution, and a PKGBUILD for Arch and Manjaro.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build=""
for candidate in "$root/out/Release" "$root/out/bin" "$root/out"; do
	if [ -f "$candidate/HoursGram" ]; then
		build="$candidate"
		break
	fi
done
version="$(awk '$1 == "AppVersionStr" { print $2 }' "$root/Telegram/build/version" | tr -d '[:space:]')"
name="HorusGram-$version-x86_64"
stage="$root/package/$name"

if [ -z "$build" ]; then
	echo "The client was not built: no HoursGram under $root/out." >&2
	find "$root/out" -maxdepth 2 -type f -name 'HoursGram*' >&2 || true
	exit 1
fi
echo "Packaging $build/HoursGram as $name."

rm -rf "$root/package"
mkdir -p "$stage/icons"

cp "$build/HoursGram" "$stage/"
if [ -f "$build/Updater" ]; then
	cp "$build/Updater" "$stage/"
fi
cp "$root/lib/xdg/com.hoursgram.desktop.desktop" "$stage/"
cp "$root/lib/xdg/com.hoursgram.desktop.metainfo.xml" "$stage/"
cp "$root/lib/xdg/com.hoursgram.desktop.service" "$stage/"

for size in 16 32 48 64 128 256 512; do
	cp "$root/Telegram/Resources/art/icon$size.png" \
		"$stage/icons/${size}x${size}.png"
done

cat > "$stage/install.sh" <<'INSTALL'
#!/usr/bin/env bash
# Installs HorusGram for every user on this machine. Run it with sudo.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${PREFIX:-/usr}"

install -Dm755 "$here/HoursGram" "$prefix/bin/HoursGram"
if [ -f "$here/Updater" ]; then
	install -Dm755 "$here/Updater" "$prefix/bin/HoursGramUpdater"
fi
install -Dm644 "$here/com.hoursgram.desktop.desktop" \
	"$prefix/share/applications/com.hoursgram.desktop.desktop"
install -Dm644 "$here/com.hoursgram.desktop.metainfo.xml" \
	"$prefix/share/metainfo/com.hoursgram.desktop.metainfo.xml"
install -Dm644 "$here/com.hoursgram.desktop.service" \
	"$prefix/share/dbus-1/services/com.hoursgram.desktop.service"
for size in 16 32 48 64 128 256 512; do
	install -Dm644 "$here/icons/${size}x${size}.png" \
		"$prefix/share/icons/hicolor/${size}x${size}/apps/com.hoursgram.desktop.png"
done

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database -q "$prefix/share/applications" || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
	gtk-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" || true
fi

echo "HorusGram is installed. Start it from the menu or run HoursGram."
INSTALL
chmod +x "$stage/install.sh"

cat > "$stage/PKGBUILD" <<PKGBUILD
# Builds an Arch/Manjaro package out of this folder. Run makepkg from here:
#   makepkg -si
pkgname=horusgram-bin
pkgver=${version}
pkgrel=1
pkgdesc='Telegram Desktop client with the HorusGram interface'
arch=('x86_64')
url='https://github.com/sixseven67123/HoursGram-Desktop'
license=('GPL-3.0-or-later')
depends=('glibc' 'glib2' 'gtk3' 'libx11' 'libxcb' 'fontconfig' 'freetype2'
         'libglvnd' 'alsa-lib' 'libpulse' 'hicolor-icon-theme')
provides=('horusgram')
conflicts=('horusgram')
options=('!strip')
source=()
sha256sums=()

package() {
	cd "\$startdir"
	install -Dm755 HoursGram "\$pkgdir/usr/bin/HoursGram"
	if [ -f Updater ]; then
		install -Dm755 Updater "\$pkgdir/usr/bin/HoursGramUpdater"
	fi
	install -Dm644 com.hoursgram.desktop.desktop \\
		"\$pkgdir/usr/share/applications/com.hoursgram.desktop.desktop"
	install -Dm644 com.hoursgram.desktop.metainfo.xml \\
		"\$pkgdir/usr/share/metainfo/com.hoursgram.desktop.metainfo.xml"
	install -Dm644 com.hoursgram.desktop.service \\
		"\$pkgdir/usr/share/dbus-1/services/com.hoursgram.desktop.service"
	for size in 16 32 48 64 128 256 512; do
		install -Dm644 "icons/\${size}x\${size}.png" \\
			"\$pkgdir/usr/share/icons/hicolor/\${size}x\${size}/apps/com.hoursgram.desktop.png"
	done
}
PKGBUILD

cat > "$stage/README.md" <<'README'
# HorusGram for Linux

Built on Rocky Linux 8, so it runs on anything with a glibc that new or
newer - Arch, Manjaro, Fedora, Ubuntu and the rest.

## Arch / Manjaro

```
makepkg -si
```

from this folder. That builds `horusgram-bin` and hands it to pacman, so
`pacman -R horusgram-bin` removes it again cleanly.

## Anything else

```
sudo ./install.sh
```

## Without installing

```
./HoursGram
```
README

cd "$root/package"
tar -cJf "$root/$name.tar.xz" "$name"
echo "PACKAGE_NAME=$name" >> "${GITHUB_ENV:-/dev/null}"
ls -lh "$root/$name.tar.xz"
