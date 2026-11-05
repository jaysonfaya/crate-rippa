#!/usr/bin/env python3
# A stand-in for yt-dlp in the tests (no internet here): answers --version, -U, -J searches/playlists and "downloads" by copying
# the test song ($FAKE_SONG) to the -o template, with progress lines like the real one.
import json, os, shutil, sys, time

a = sys.argv[1:]
if "--version" in a:
    print("2026.10.31-fake"); sys.exit(0)
if "-U" in a:
    print("Latest version: 2026.10.31-fake"); sys.exit(0)
with open(os.environ.get("FAKE_LOG", "/dev/null"), "a") as lg:
    lg.write(" ".join(a) + "\n")
target = a[-1]
if "-J" in a:
    if target.startswith("ytsearch"):
        q = target.split(":", 1)[1]
        n = int(a[a.index("--playlist-end") + 1])
        ents = [{"id": "SONG%07d" % i, "title": "%s song %d" % (q, i), "channel": "Chan %d" % i, "duration": 30 + i, "live_status": "not_live"} for i in range(n)]
        ents.insert(1, {"id": "LIVE0000000", "title": "a live stream", "live_status": "is_live"})
        print(json.dumps({"_type": "playlist", "entries": ents}))
    elif "results?search_query" in target:
        ents = [{"id": "PLabc%d" % i, "url": "https://www.youtube.com/playlist?list=PLlist%02d" % i, "title": "List %d" % i, "channel": "C", "playlist_count": 3 + i} for i in range(4)]
        ents.append({"id": "UCchannel", "url": "https://www.youtube.com/channel/UCchannel", "title": "a channel"})
        print(json.dumps({"entries": ents}))
    elif "playlist?list=" in target:
        print(json.dumps({"entries": [{"id": "PLSONG%05d" % i, "title": "Playlist song %d" % i, "uploader": "Up", "duration": 31} for i in range(3)]}))
    else:
        print("ERROR: unknown target", file=sys.stderr); sys.exit(1)
    sys.exit(0)
tmpl = a[a.index("-o") + 1]
vid = target.split("v=")[1]
if vid == "BADBADBAD00":
    print("ERROR: [youtube] BADBADBAD00: Video unavailable. This video has been removed", file=sys.stderr); sys.exit(1)
src = os.environ["FAKE_SONG"]
ext = os.path.splitext(src)[1][1:]
out = tmpl.replace("%(id)s", vid).replace("%(ext)s", ext)
for p in (10, 55.5, 100):
    print("[download] %5.1f%% of 1.00MiB at 1.00MiB/s ETA 00:00" % p); sys.stdout.flush(); time.sleep(0.05)
shutil.copy(src, out + ".part"); os.replace(out + ".part", out)
print(out)
