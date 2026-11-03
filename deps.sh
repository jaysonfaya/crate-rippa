#!/usr/bin/env bash
# CRATE RIPPA: fetch the helpers the plugin runs on the MPC into <out dir> (default ./work/bin), for armv7 (MPC Live/One/X/Key, Force):
#   python3/   python-build-standalone CPython 3.11 (PSF licence), its own interpreter, the MPC's own Python is never touched
#   yt-dlp     the newest official yt-dlp release (Unlicense), a zip the bundled Python runs
#   ffmpeg     a static armhf build (GPL): johnvansickle.com, else eugeneware/ffmpeg-static or ffbinaries on GitHub
# Adapted from sd88me's mpc-vst-cratedigger scripts (MIT). Runs on the build machine (GitHub Actions); nothing here runs ARM code.
set -euo pipefail
OUT="${1:-$(pwd)/work/bin}"
WORK="$(mktemp -d)"
mkdir -p "$OUT"
PBS_TAG="20260901"
PY="3.11.16"
TRIPLE="armv7-unknown-linux-gnueabihf"
echo "== Python $PY ($TRIPLE)"
curl -fsSL --retry 3 -o "$WORK/py.tgz" \
  "https://github.com/astral-sh/python-build-standalone/releases/download/$PBS_TAG/cpython-$PY+$PBS_TAG-$TRIPLE-install_only_stripped.tar.gz"
tar -xzf "$WORK/py.tgz" -C "$WORK"
rm -rf "$OUT/python3"
mv "$WORK/python" "$OUT/python3"
# The plugin managers refuse a package with links in it (every file must be in SHA256SUMS, a link never is): no links at all,
# bin/python3 is a small start script. Parts the helper never uses go too.
P="$OUT/python3"
find "$P" -type l -delete
printf '#!/bin/sh\n# starts the bundled Python (a real file instead of a link: the plugin manager does not take links)\nexec "$(dirname "$0")/python3.11" "$@"\n' > "$P/bin/python3"
chmod 755 "$P/bin/python3"
rm -rf "$P/include" "$P/share" "$P/lib/pkgconfig" "$P"/lib/libtcl* "$P"/lib/libtk* "$P"/lib/tcl* "$P"/lib/tk* "$P"/lib/itcl* "$P"/lib/thread* \
  "$P/lib/python3.11/tkinter" "$P/lib/python3.11/idlelib" "$P/lib/python3.11/test" "$P/lib/python3.11/turtledemo" "$P/lib/python3.11/ensurepip" \
  "$P/lib/python3.11/lib2to3" "$P/lib/python3.11/site-packages/pip" "$P"/lib/python3.11/site-packages/pip-* "$P"/lib/python3.11/lib-dynload/_tkinter* \
  "$P/bin/pip" "$P/bin/pip3" "$P/bin/pip3.11" "$P/bin/idle3.11" "$P/bin/2to3-3.11" "$P/bin/pydoc3.11" "$P/bin/python3.11-config"
if [ -n "$(find "$OUT" -type l)" ]; then echo "links left in the bundle"; exit 1; fi
echo "== yt-dlp (latest)"
curl -fsSL --retry 3 -o "$OUT/yt-dlp" "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp"
chmod +x "$OUT/yt-dlp"
python3 "$OUT/yt-dlp" --version
echo "== ffmpeg (armhf static)"
# johnvansickle.com sometimes answers build machines with a small web page instead of the file, so there are two more sources
# (static armhf builds on GitHub). Whatever comes must be a 32-bit ARM program, else the next source is tried.
UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
is_arm() { python3 - "$1" <<'PY'
import sys
b = open(sys.argv[1], "rb").read(20)
sys.exit(0 if len(b) == 20 and b[:4] == b"\x7fELF" and b[4] == 1 and b[18] == 0x28 else 1)
PY
}
got=0
for i in 1 2 3; do
  if curl -fL --retry 2 -A "$UA" -o "$WORK/ff.tar.xz" "https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-armhf-static.tar.xz" \
     && xz -t "$WORK/ff.tar.xz" 2>/dev/null; then
    mkdir -p "$WORK/ff" && tar -xJf "$WORK/ff.tar.xz" -C "$WORK/ff"
    cp "$(find "$WORK/ff" -type f -name ffmpeg | head -n 1)" "$OUT/ffmpeg" && is_arm "$OUT/ffmpeg" && { got=1; echo "ffmpeg from johnvansickle.com"; break; }
  fi
  sleep $((i * 10))
done
if [ "$got" = 0 ]; then   # eugeneware/ffmpeg-static b6.0: ffmpeg 6.0, armhf, gzip
  if curl -fL --retry 3 -o "$WORK/ff.gz" "https://github.com/eugeneware/ffmpeg-static/releases/download/b6.0/ffmpeg-linux-arm.gz" \
     && gunzip -c "$WORK/ff.gz" > "$OUT/ffmpeg" && is_arm "$OUT/ffmpeg"; then got=1; echo "ffmpeg from eugeneware/ffmpeg-static b6.0"; fi
fi
if [ "$got" = 0 ]; then   # ffbinaries 4.4.1 armhf, zip
  if curl -fL --retry 3 -o "$WORK/ff.zip" "https://github.com/ffbinaries/ffbinaries-prebuilt/releases/download/v4.4.1/ffmpeg-4.4.1-linux-armhf-32.zip" \
     && python3 -m zipfile -e "$WORK/ff.zip" "$WORK/ffz" && cp "$(find "$WORK/ffz" -type f -name ffmpeg | head -n 1)" "$OUT/ffmpeg" \
     && is_arm "$OUT/ffmpeg"; then got=1; echo "ffmpeg from ffbinaries 4.4.1"; fi
fi
[ "$got" = 1 ] || { echo "ffmpeg download failed from all three sources"; exit 1; }
chmod +x "$OUT/ffmpeg"
rm -rf "$WORK"
du -sh "$OUT"/*
