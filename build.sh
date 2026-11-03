#!/usr/bin/env bash
# CRATE RIPPA (JJ DA PLUG): builds the installable plugin zip. Used by the GitHub workflow; locally:
#   MPC_VST=<mpc-vst-plugins checkout> bash build.sh       (needs Docker with QEMU, Python 3 with Pillow and fontTools)
# All source files sit flat next to this script (as uploaded to GitHub). gen_ui.py writes params.json, layout.conf,
# skin.css, vst.json and images/ first.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:?set MPC_VST to an mpc-vst-plugins checkout}"
VERSION="${VERSION:-1.0.0}"
cd "$HERE"
python3 -c "import fontTools, PIL" 2>/dev/null || python3 -m pip install --quiet fonttools pillow || python3 -m pip install --quiet --break-system-packages fonttools pillow
python3 gen_ui.py
rm -rf "$HERE/work" "$HERE/dist" && mkdir -p "$HERE/work/bin" "$HERE/dist"
echo "== host test (x86, sanitizers)"
bash "$MV/tools/test_port.sh" "$HERE/vst.json" | tail -n 3
echo "== device build (armv7) and skin"
bash "$MV/tools/build_port.sh" "$HERE/vst.json"
echo "== helpers (Python, yt-dlp, ffmpeg)"
bash "$HERE/deps.sh" "$HERE/work/bin"
cp tube_daemon.py "$HERE/work/bin/"
B="$HERE/build"
python3 "$MV/tools/release.py" --so "$B/jj_craterippa.so" --skin "$B/skin/JJ DA PLUG - VST - Crate Rippa" \
  --entry "$B/pluginlist-entry.xml" --version "$VERSION" --extra "$HERE/work/bin:craterippa/bin" --id crate-rippa --license MIT \
  --requires "A network connection (Wi-Fi) for YouTube" \
  --about "CRATE RIPPA by JJ DA PLUG: YouTube on a turntable. Search, scratch, chop to 64 pads, edit, export WAVs." -o "$HERE/dist"
# the USB plugin manager drops a package with links or with files missing from SHA256SUMS, without a message: check it here
python3 - "$HERE"/dist/*-mpc-armv7.zip <<'PY'
import sys, zipfile, stat
z = zipfile.ZipFile(sys.argv[1])
names = [i.filename for i in z.infolist() if not i.filename.endswith("/")]
links = [i.filename for i in z.infolist() if stat.S_ISLNK(i.external_attr >> 16)]
top = names[0].split("/")[0]
sums = {l[66:] for l in z.read(top + "/SHA256SUMS").decode().splitlines()}
miss = [n for n in names if n != top + "/SHA256SUMS" and n[len(top) + 1:] not in sums]
if links or miss:
    sys.exit("package would be ignored by the plugin manager: links %s, unchecked %s" % (links[:5], miss[:5]))
print("package check ok: no links, every file in SHA256SUMS")
PY
mkdir -p "$HERE/dist/preview"
python3 "$MV/tools/studio.py" preview "$B/skin/JJ DA PLUG - VST - Crate Rippa/Plugin Skins" -o "$HERE/dist/preview/page_%d.png" >/dev/null 2>&1 || true
ls -la "$HERE/dist"
