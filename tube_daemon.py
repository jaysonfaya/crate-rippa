#!/usr/bin/env python3
# CRATE RIPPA helper (JJ DA PLUG). Runs under the plugin's own bundled Python, started once by the plugin and kept
# alive, so every search does not pay Python's start-up again. Talks on stdin/stdout, one tab-separated line each.
#
#   PING                          -> PONG
#   SEARCH <query> <n>            -> S <id> <title> <channel> <seconds>  (n times)  END <count>
#   PLISTS <query> <n>            -> P <id> <title> <channel> <videos>   ...        END <count>
#   OPEN <playlist id> <n>        -> S ...                                          END <count>
#   RESOLVE <video id>            -> URL <media url> <user agent> <seconds> <title> | ERR <why>
#   UPDATE <path of yt-dlp>       -> UPD <text>     (downloads the newest yt-dlp next to the old one)
#   QUIT
# Any failure answers ERR <short reason>. Uses the open-source yt-dlp (Unlicense). Part of the plugin, MIT.
import os
import sys
import urllib.parse

YT = None


def out(*fields):
    sys.stdout.write("\t".join(str(f).replace("\t", " ").replace("\n", " ").replace("\r", " ").strip() for f in fields) + "\n")
    sys.stdout.flush()


def why(exc):
    t = str(exc)
    low = t.lower()
    for key, short in (("private video", "PRIVATE VIDEO"), ("sign in to confirm your age", "AGE LIMIT"),
                       ("not available in your country", "REGION BLOCKED"), ("copyright", "COPYRIGHT BLOCK"),
                       ("video unavailable", "VIDEO REMOVED"), ("live event", "LIVE, NOT A VIDEO"),
                       ("timed out", "NETWORK TIMEOUT"), ("name or service not known", "NO INTERNET"),
                       ("temporary failure in name resolution", "NO INTERNET"), ("network is unreachable", "NO INTERNET"),
                       ("http error 429", "YOUTUBE BUSY, TRY LATER"), ("unable to connect", "NO INTERNET"),
                       ("unable to download api page", "NO INTERNET"), ("connection refused", "NO INTERNET"), ("page needs to be reloaded", "UPDATE YT-DLP (INFO)"),
                       ("sign in to confirm you", "YOUTUBE ASKS FOR LOGIN")):
        if key in low:
            return short
    t = t.replace("ERROR: ", "")
    return t[:90] or "UNKNOWN ERROR"


def base_opts():
    return {"quiet": True, "no_warnings": True, "socket_timeout": 12, "extractor_args": {"youtube": {"player_skip": ["js"]}}}


def flat(target, n):
    o = base_opts()
    o.update({"extract_flat": "in_playlist", "playlistend": n, "skip_download": True})
    with YT.YoutubeDL(o) as y:
        d = y.extract_info(target, download=False)
    return (d or {}).get("entries") or []


def secs(e):
    try:
        return int(float(e.get("duration") or 0))
    except Exception:
        return 0


def emit_videos(entries, n):
    c = 0
    for e in entries:
        if not isinstance(e, dict):
            continue
        vid = e.get("id") or ""
        title = e.get("title") or ""
        if not vid or not title or len(vid) != 11:
            continue
        if e.get("live_status") in ("is_live", "is_upcoming"):
            continue
        out("S", vid, title, e.get("channel") or e.get("uploader") or "", secs(e))
        c += 1
        if c >= n:
            break
    out("END", c)


def search(q, n):
    emit_videos(flat("ytsearch%d:%s" % (n + 5, q), n + 5), n)


def plists(q, n):
    url = "https://www.youtube.com/results?search_query=" + urllib.parse.quote_plus(q) + "&sp=EgIQAw%3D%3D"
    c = 0
    for e in flat(url, n):
        if not isinstance(e, dict):
            continue
        pid = e.get("id") or ""
        u = e.get("url") or ""
        if "list=" in u:
            pid = urllib.parse.parse_qs(urllib.parse.urlparse(u).query).get("list", [pid])[0]
        if not pid or not pid[:2] in ("PL", "OL", "RD", "UU", "VL", "FL"):
            continue
        out("P", pid, e.get("title") or pid, e.get("channel") or e.get("uploader") or "", e.get("playlist_count") or "")
        c += 1
        if c >= n:
            break
    out("END", c)


def open_list(pid, n):
    emit_videos(flat("https://www.youtube.com/playlist?list=" + pid, n), n)


def resolve(vid):
    o = base_opts()
    o.update({"format": "bestaudio[ext=m4a]/bestaudio/best", "noplaylist": True})
    try:
        with YT.YoutubeDL(o) as y:
            d = y.extract_info("https://www.youtube.com/watch?v=" + vid, download=False)
    except Exception:
        o.pop("extractor_args", None)   # second try with yt-dlp's full player handling
        with YT.YoutubeDL(o) as y:
            d = y.extract_info("https://www.youtube.com/watch?v=" + vid, download=False)
    url = d.get("url") or ""
    if not url:
        for f in reversed(d.get("formats") or []):
            if f.get("url") and f.get("acodec") not in (None, "none"):
                url = f["url"]
                break
    if not url:
        out("ERR", "NO AUDIO FOUND")
        return
    ua = (d.get("http_headers") or {}).get("User-Agent", "")
    out("URL", url, ua, secs(d), d.get("title") or vid)


def update(path):
    import urllib.request
    tmp = path + ".new"
    urllib.request.urlretrieve("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp", tmp)
    if os.path.getsize(tmp) < 1000000:
        raise RuntimeError("download too small")
    os.chmod(tmp, 0o755)
    os.replace(tmp, path)
    out("UPD", "YT-DLP UPDATED, RELOAD THE PLUGIN")


def main():
    global YT
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        sys.path.insert(0, sys.argv[1])
    try:
        import yt_dlp
        YT = yt_dlp
        out("READY", getattr(getattr(yt_dlp, "version", None), "__version__", "?"))
    except Exception as exc:
        out("READY", "NONE")
        out("ERR", "YT-DLP MISSING: " + why(exc))
    for raw in sys.stdin:
        p = raw.rstrip("\r\n").split("\t")
        cmd = p[0]
        try:
            if cmd == "PING":
                out("PONG")
            elif cmd == "QUIT":
                break
            elif YT is None and cmd != "UPDATE":
                out("ERR", "YT-DLP MISSING")
            elif cmd == "SEARCH":
                search(p[1], max(1, min(40, int(p[2]))))
            elif cmd == "PLISTS":
                plists(p[1], max(1, min(40, int(p[2]))))
            elif cmd == "OPEN":
                open_list(p[1], max(1, min(100, int(p[2]))))
            elif cmd == "RESOLVE":
                resolve(p[1])
            elif cmd == "UPDATE":
                update(p[1])
            else:
                out("ERR", "UNKNOWN COMMAND")
        except Exception as exc:
            out("ERR", why(exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
