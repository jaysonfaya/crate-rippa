/* CRATE RIPPA Desktop (JJ DA PLUG): the engine, ported from the MPC engine (engine.c). MIT. */
#include "CrEngine.h"
#include "Tools.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace cr
{
const char* ISO_NAMES[NISO] = { "FULL MIX", "NO VOCALS", "VOCAL BAND", "BASS", "KICK + HATS", "NO BASS", "HIGHS" };
const char* ISO_HINTS[NISO] = { "THE SONG AS IT IS", "CENTRE (VOCALS) REMOVED, BASS KEPT: GOOD FOR INSTRUMENTAL LOOPS",
                                "THE MIDDLE OF THE CENTRE: WHERE VOCALS AND LEADS SIT", "ONLY THE LOW END: BASSLINES AND KICKS",
                                "LOWS AND HIGHS: KICKS AND HATS, LESS MUSIC", "LOW END CUT: CHOPS WITHOUT THE OLD BASS",
                                "ONLY THE TOP: HATS, AIR, SHAKERS" };
const char* SLICE_NAMES[5] = { "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS" };
static const float SLICE_BEATS[5] = { 0.5f, 1, 2, 4, 8 };
static const char* OP_NAMES[5] = { "", "COPY", "MOVE", "SWAP", "DELETE" };
static const int RANGE_PCT[3] = { 8, 16, 50 };
static constexpr double SCR_FRAMES_PER_TURN = 1.8 * SR;   // one turn of a 33 1/3 record = 1.8 s

/* ------------------------------------------------------------------ small helpers */
static int clampi (int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }


std::string fmtTime (double sec, bool ms)
{
    if (sec < 0) sec = 0;
    int m = (int) (sec / 60);
    double s = sec - m * 60;
    char b[32];
    if (ms) snprintf (b, sizeof b, "%d:%06.3f", m, s);
    else snprintf (b, sizeof b, "%d:%02d", m, (int) s);
    return b;
}
std::string upperStr (std::string s)
{
    for (auto& c : s) c = (char) toupper ((unsigned char) c);
    return s;
}
std::string safeName (const std::string& s)   // a name that is safe as a folder/file name everywhere
{
    std::string d;
    for (unsigned char c : s)
    {
        if (isalnum (c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')') d += (char) c;
        else if (c >= 0x80) continue;
        else if (! d.empty() && d.back() != ' ') d += ' ';
        if (d.size() >= 60) break;
    }
    while (! d.empty() && d.back() == ' ') d.pop_back();
    return d.empty() ? "Crate Rippa" : d;
}
juce::File defaultDataDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("CRATE_RIPPA_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("JJ DA PLUG").getChildFile ("Crate Rippa");
}
static std::string pname (int idx)
{
    char b[8];
    snprintf (b, sizeof b, "%c%d", 'A' + idx / 16, idx % 16 + 1);
    return b;
}
static inline void fetch (const int16_t* d, int len, double pos, float& l, float& r)
{
    int i = (int) pos;
    if (i < 0 || i >= len) { l = r = 0; return; }
    float f = (float) (pos - i);
    int j = i + 1 < len ? i + 1 : i;
    l = (d[2 * i] + (d[2 * j] - d[2 * i]) * f) * (1.0f / 32768.0f);
    r = (d[2 * i + 1] + (d[2 * j + 1] - d[2 * i + 1]) * f) * (1.0f / 32768.0f);
}

/* ------------------------------------------------------------------ isolation filters */
enum { F_LP150, F_LP180, F_HP200, F_LP4K, F_LP100, F_HP6K, F_HP220, F_HP2K5, NF };
template <class B> static void bqMake (B& f, bool hp, double fc, double sr)
{
    double w = 2 * juce::MathConstants<double>::pi * fc / sr, c = std::cos (w), s = std::sin (w), q = 0.7071, al = s / (2 * q);
    double b0 = hp ? (1 + c) / 2 : (1 - c) / 2, b1 = hp ? -(1 + c) : 1 - c, a0 = 1 + al;
    f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b0 / a0);
    f.a1 = (float) (-2 * c / a0); f.a2 = (float) ((1 - al) / a0);
}
template <class B> static void bqSet (B* F, double sr)
{
    bqMake (F[F_LP150], false, 150, sr); bqMake (F[F_LP180], false, 180, sr); bqMake (F[F_HP200], true, 200, sr);
    bqMake (F[F_LP4K], false, 4000, sr); bqMake (F[F_LP100], false, 100, sr); bqMake (F[F_HP6K], true, 6000, sr);
    bqMake (F[F_HP220], true, 220, sr); bqMake (F[F_HP2K5], true, 2500, sr);
}
template <class B> static inline float bq (const B& f, float* s, float x)   /* transposed direct form II */
{
    float y = f.b0 * x + s[0];
    s[0] = f.b1 * x - f.a1 * y + s[1];
    s[1] = f.b2 * x - f.a2 * y;
    if (std::fabs (s[0]) < 1e-15f) s[0] = 0;
    if (std::fabs (s[1]) < 1e-15f) s[1] = 0;
    return y;
}
void Engine::isoRun (const Bq* F, float st[8][2], int mode, float& l, float& r) const
{
    float L = l, R = r, m_ = (L + R) * 0.5f, sd = (L - R) * 0.5f;
    switch (mode)
    {
        case 1: { float b = bq (F[F_LP150], st[0], m_); b = bq (F[F_LP150], st[1], b); l = b + sd * 1.4f; r = b - sd * 1.4f; break; }
        case 2: { float v_ = bq (F[F_HP200], st[0], m_); v_ = bq (F[F_HP200], st[1], v_); v_ = bq (F[F_LP4K], st[2], v_); v_ = bq (F[F_LP4K], st[3], v_);
                  l = r = v_ * 1.3f; break; }
        case 3: { float b = bq (F[F_LP180], st[0], m_); b = bq (F[F_LP180], st[1], b); b = bq (F[F_LP180], st[2], b); l = r = b * 1.4f; break; }
        case 4: { float k = bq (F[F_LP100], st[0], m_); k = bq (F[F_LP100], st[1], k);
                  float hl = bq (F[F_HP6K], st[2], L), hr = bq (F[F_HP6K], st[3], R);
                  l = k * 1.4f + hl * 1.6f; r = k * 1.4f + hr * 1.6f; break; }
        case 5: { float a = bq (F[F_HP220], st[0], L), b = bq (F[F_HP220], st[1], R); l = bq (F[F_HP220], st[2], a); r = bq (F[F_HP220], st[3], b); break; }
        case 6: { float a = bq (F[F_HP2K5], st[0], L), b = bq (F[F_HP2K5], st[1], R);
                  l = bq (F[F_HP2K5], st[2], a) * 1.3f; r = bq (F[F_HP2K5], st[3], b) * 1.3f; break; }
        default: break;
    }
}

/* ------------------------------------------------------------------ life */
Engine::Engine (const juce::File& d) : dir (d)
{
    dir.createDirectory();
    bqSet (filt44, 44100.0);
    bqSet (filtRT, 44100.0);
    for (auto& p : pad) padClear (p);
    lazyNext = 0;
    {   /* the pad map learned last time, for every new instance */
        auto txt = dir.getChildFile ("padmap.txt").loadFileAsString();
        int a = 0, b = 0;
        if (sscanf (txt.toRawUTF8(), "%d %d", &a, &b) == 2 && a >= 0 && b > a && b < 128) { mapFirst = a; mapLast = b; }
        padmapBuild();
    }
    status = "TYPE A SONG IN THE SEARCH FIELD";
    resHead = "SEARCH YOUTUBE";
    netTh = std::thread ([this] { netMain(); });
    loadTh = std::thread ([this] { loadMain(); });
    jobTh = std::thread ([this] { jobMain(); });
}

Engine::~Engine()
{
    {
        std::lock_guard<std::mutex> g (m);
        quit = true;
        gen++;
        if (loadProc) loadProc->kill();
        if (netProc) netProc->kill();
    }
    netCv.notify_all();
    loadCv.notify_all();
    jobCv.notify_all();
    netTh.join();
    loadTh.join();
    jobTh.join();
}

void Engine::prepare (double sampleRate)
{
    std::lock_guard<std::recursive_mutex> g (api);
    hsr = sampleRate > 1000 ? sampleRate : 44100.0;
    ratio = (double) SR / hsr;
    bqSet (filtRT, hsr);
    for (auto& vo : v) { vo.on = false; vo.s.reset(); }
}

void Engine::ensureBuffers()
{
    if (buf) return;
    buf.reset (new int16_t[(size_t) MAXF * 2]);   // pages stay untouched until a song fills them
    peak.reset (new uint8_t[NHOP]());
    odf.reset (new float[NHOP]());
}

void Engine::setStatus (const std::string& s)
{
    std::lock_guard<std::mutex> g (m);
    status = s;
}

/* ------------------------------------------------------------------ undo */
void Engine::undoPush()
{
    if ((int) undoStack.size() == NUNDO) undoStack.pop_front();
    undoStack.emplace_back (pad, pad + NPAD);
}
void Engine::undoDrop() { if (! undoStack.empty()) undoStack.pop_back(); }
void Engine::voicesOff()
{
    for (auto& vo : v) { vo.on = false; vo.s.reset(); }
}
void Engine::undo()
{
    std::lock_guard<std::recursive_mutex> g (api);
    if (undoStack.empty()) { setStatus ("NOTHING TO UNDO"); return; }
    voicesOff();
    auto& s = undoStack.back();
    for (int i = 0; i < NPAD; i++) pad[i] = s[(size_t) i];
    undoStack.pop_back();
    setStatus ("UNDONE");
}
void Engine::padClear (Pad& p) { p = Pad(); }

/* ------------------------------------------------------------------ jobs (files) */
void Engine::jobAdd (Job&& j)
{
    {
        std::lock_guard<std::mutex> g (m);
        jobs.push_back (std::move (j));
    }
    jobCv.notify_one();
}

static bool writeWav (const juce::File& f, const int16_t* d, int frames)
{
    juce::File part (f.getFullPathName() + ".part");
    part.deleteFile();
    {
        std::unique_ptr<juce::OutputStream> os (part.createOutputStream());
        if (os == nullptr) return false;
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (16));
        if (w == nullptr) return false;
        const int B = 8192;
        juce::AudioBuffer<float> ab (2, B);
        for (int at = 0; at < frames; at += B)
        {
            int n = std::min (B, frames - at);
            for (int i = 0; i < n; i++)
            {
                ab.setSample (0, i, d[2 * (at + i)] / 32768.0f);
                ab.setSample (1, i, d[2 * (at + i) + 1] / 32768.0f);
            }
            if (! w->writeFromAudioSampleBuffer (ab, 0, n)) return false;
        }
    }
    f.deleteFile();
    return part.moveFileTo (f);
}

std::vector<int16_t> Engine::bounce (const Pad& p, const int16_t* src, int slen)
{
    std::vector<int16_t> o;
    int s0 = clampi (p.start, 0, slen), s1 = clampi (p.end, s0, slen);
    double rate = std::pow (2.0, p.pitch / 12.0);
    int frames = (int) ((s1 - s0) / rate);
    if (frames < 32) return o;
    o.resize ((size_t) frames * 2);
    float st[8][2] {};
    for (int k = 0; k < frames; k++)
    {
        double pos = p.rev ? s1 - 1 - k * rate : s0 + k * rate;
        float l, r;
        fetch (src, slen, pos, l, r);
        isoRun (filt44, st, p.iso, l, r);
        float gg = p.level;
        if (k < 64) gg *= k / 64.0f;
        if (frames - k < 128) gg *= (frames - k) / 128.0f;
        o[(size_t) (2 * k)] = (int16_t) clampf (l * gg * 32767, -32768, 32767);
        o[(size_t) (2 * k + 1)] = (int16_t) clampf (r * gg * 32767, -32768, 32767);
    }
    return o;
}

void Engine::jobMain()
{
    std::unique_lock<std::mutex> lk (m);
    while (! quit)
    {
        if (jobs.empty()) { jobCv.wait (lk); continue; }
        Job j = std::move (jobs.front());
        jobs.pop_front();
        lk.unlock();
        if (j.kind == JOB_RAW)   /* a frozen pad's audio, so a project finds it again */
        {
            juce::File f (j.path), part (j.path + ".part");
            f.getParentDirectory().createDirectory();
            if (part.replaceWithData (j.d.data(), j.d.size() * sizeof (int16_t))) { f.deleteFile(); part.moveFileTo (f); }
        }
        else if (j.kind == JOB_WAV)
        {
            auto o = bounce (j.pp, j.d.data(), j.len);
            if (o.empty() || ! writeWav (juce::File (j.path), o.data(), (int) o.size() / 2)) setStatus ("EXPORT FAILED: DISK FULL?");
        }
        else if (j.kind == JOB_HIST)
        {   /* path holds "id\tsecs\ttitle"; keep the newest 100, no duplicates */
            auto hf = dir.getChildFile ("history.txt");
            juce::StringArray lines;
            lines.addLines (hf.loadFileAsString());
            juce::String id = juce::String (j.path).upToFirstOccurrenceOf ("\t", false, false);
            juce::StringArray out;
            out.add (juce::String (j.path));
            for (auto& l : lines)
                if (l.isNotEmpty() && ! l.startsWith (id + "\t") && out.size() < 100) out.add (l);
            juce::File part (hf.getFullPathName() + ".part");
            if (part.replaceWithText (out.joinIntoString ("\n") + "\n")) { hf.deleteFile(); part.moveFileTo (hf); }
        }
        else if (j.kind == JOB_FILE)
        {
            juce::File f (j.path), part (j.path + ".part");
            if (part.replaceWithText (j.text)) { f.deleteFile(); part.moveFileTo (f); }
        }
        else if (j.kind == JOB_DONE)
        {
            setStatus (j.path);
            exporting = false;
        }
        lk.lock();
    }
    jobs.clear();
}

/* ------------------------------------------------------------------ helper programs */
juce::File Engine::ytdlpFile() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("CRATE_YTDLP", {});
    if (env.isNotEmpty()) return juce::File (env);
#if JUCE_WINDOWS
    return dir.getChildFile ("bin").getChildFile ("yt-dlp.exe");
#else
    return dir.getChildFile ("bin").getChildFile ("yt-dlp");
#endif
}
juce::File Engine::ffmpegFile() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("CRATE_FFMPEG", {});
    if (env.isNotEmpty()) return env == "none" ? juce::File() : juce::File (env);
#if JUCE_WINDOWS
    auto f = dir.getChildFile ("bin").getChildFile ("ffmpeg.exe");
    return f.existsAsFile() ? f : juce::File();
#else
    auto f = dir.getChildFile ("bin").getChildFile ("ffmpeg");
    if (f.existsAsFile()) return f;
    for (auto* p : { "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg" })
        if (juce::File (p).existsAsFile()) return juce::File (p);
    return {};
#endif
}
juce::File Engine::denoFile() const
{
#if JUCE_WINDOWS
    return dir.getChildFile ("bin").getChildFile ("deno.exe");
#else
    return dir.getChildFile ("bin").getChildFile ("deno");
#endif
}

static const char* ytdlpAsset()
{
#if JUCE_WINDOWS
    return "yt-dlp.exe";
#elif JUCE_MAC
    return "yt-dlp_macos";
#elif defined(__aarch64__)
    return "yt-dlp_linux_aarch64";
#else
    return "yt-dlp_linux";
#endif
}
static const char* denoAsset()
{
#if JUCE_WINDOWS
    return "deno-x86_64-pc-windows-msvc.zip";
#elif JUCE_MAC
   #if defined(__aarch64__) || defined(__arm64__)
    return "deno-aarch64-apple-darwin.zip";
   #else
    return "deno-x86_64-apple-darwin.zip";
   #endif
#elif defined(__aarch64__)
    return "deno-aarch64-unknown-linux-gnu.zip";
#else
    return "deno-x86_64-unknown-linux-gnu.zip";
#endif
}

/* yt-dlp is downloaded on the first search (one file from yt-dlp's GitHub releases), later kept up to date with UPDATE YT-DLP */
bool Engine::ensureYtDlp()
{
    auto f = ytdlpFile();
    if (f.existsAsFile())
    {
        std::lock_guard<std::mutex> g (m);
        if (ytdlpVer != "-") return true;
    }
    else
    {
        setStatus ("FIRST START: DOWNLOADING YT-DLP...");
        std::string err;
        juce::URL u (juce::String ("https://github.com/yt-dlp/yt-dlp/releases/latest/download/") + ytdlpAsset());
        bool ok = downloadFile (u, f, [this] (double p) { setStatus ("FIRST START: DOWNLOADING YT-DLP " + std::to_string ((int) (p * 100)) + "%"); },
                                [this] { return quit.load(); }, err);
        if (! ok) { setStatus ("YT-DLP DOWNLOAD FAILED: " + err); return false; }
#if ! JUCE_WINDOWS
        f.setExecutePermission (true);
#endif
    }
    std::string ver;
    int rc = runProcess ({ f.getFullPathName(), "--version" }, [&] (const std::string& l) { if (ver.empty()) ver = l; }, 60,
                         [this] { return quit.load(); }, &netProc, &m);
    if (rc != 0 || ver.empty()) { setStatus ("YT-DLP DOES NOT START (" + std::to_string (rc) + ")"); return false; }
    std::lock_guard<std::mutex> g (m);
    ytdlpVer = ver;
    return true;
}

static juce::StringArray ytBase (const juce::File& yt, const juce::File& deno)
{
    juce::StringArray a { yt.getFullPathName(), "--ignore-config", "--no-warnings", "--socket-timeout", "15", "--encoding", "utf-8" };
    if (deno.existsAsFile()) { a.add ("--js-runtimes"); a.add ("deno:" + deno.getFullPathName()); }
    return a;
}

static int jsonInt (const juce::var& v)
{
    if (v.isInt() || v.isInt64() || v.isDouble()) return (int) (double) v;
    return v.toString().getIntValue();
}

void Engine::netRequest (int j, const std::string& arg, int n, const std::string& arg2)
{
    {
        std::lock_guard<std::mutex> g (m);
        job = j;
        jobN = n;
        jobArg = arg;
        jobArg2 = arg2;
    }
    netCv.notify_one();
}

void Engine::netMain()
{
    std::unique_lock<std::mutex> lk (m);
    while (! quit)
    {
        if (job == J_NONE) { netCv.wait (lk); continue; }
        int jb = job, n = jobN;
        std::string a = jobArg, a2 = jobArg2;
        job = J_NONE;
        lk.unlock();
        netBusy = true;
        if (jb == J_DENO)
        {
            setStatus ("DOWNLOADING DENO (JAVASCRIPT FOR YT-DLP)...");
            auto zip = dir.getChildFile ("bin").getChildFile ("deno-download.zip");
            std::string err;
            bool ok = downloadFile (juce::URL (juce::String ("https://github.com/denoland/deno/releases/latest/download/") + denoAsset()), zip,
                                    [this] (double p) { setStatus ("DOWNLOADING DENO " + std::to_string ((int) (p * 100)) + "%"); },
                                    [this] { return quit.load(); }, err);
            if (ok)
            {
                juce::ZipFile z (zip);
                auto r = z.uncompressTo (dir.getChildFile ("bin"), true);
                ok = r.wasOk() && denoFile().existsAsFile();
#if ! JUCE_WINDOWS
                denoFile().setExecutePermission (true);
#endif
                if (! ok) err = r.getErrorMessage().toStdString();
            }
            zip.deleteFile();
            setStatus (ok ? "DENO INSTALLED: YT-DLP USES IT FROM NOW ON" : "DENO DOWNLOAD FAILED: " + err);
        }
        else if (! ensureYtDlp())
        {
            std::lock_guard<std::mutex> g (m);
            if (sstate == ST_RESOLVE) sstate = ST_ERROR;
        }
        else if (jb == J_UPDATE)
        {
            setStatus ("UPDATING YT-DLP...");
            std::string last;
            int rc = runProcess ({ ytdlpFile().getFullPathName(), "-U" }, [&] (const std::string& l) { last = l; }, 180,
                                 [this] { return quit.load(); }, &netProc, &m);
            if (rc != 0)
            {   /* not a self-updating build: fetch the newest release file instead */
                std::string err;
                juce::File f = ytdlpFile();
                if (downloadFile (juce::URL (juce::String ("https://github.com/yt-dlp/yt-dlp/releases/latest/download/") + ytdlpAsset()), f, {},
                                  [this] { return quit.load(); }, err))
                {
#if ! JUCE_WINDOWS
                    f.setExecutePermission (true);
#endif
                    rc = 0;
                    last = "YT-DLP REPLACED WITH THE NEWEST RELEASE";
                }
                else last = "UPDATE FAILED: " + err;
            }
            { std::lock_guard<std::mutex> g (m); ytdlpVer = "-"; }
            ensureYtDlp();
            std::string ver;
            { std::lock_guard<std::mutex> g (m); ver = ytdlpVer; }
            setStatus (rc == 0 ? "YT-DLP IS UP TO DATE: " + ver : upperStr (last));
        }
        else if (jb == J_RESOLVE)
        {
            resolveAndDownload (a);
        }
        else
        {   /* SEARCH / PLISTS / OPEN: one JSON object from yt-dlp */
            juce::StringArray args = ytBase (ytdlpFile(), denoFile());
            args.addArray ({ "-J", "--flat-playlist", "--playlist-end", juce::String (jb == J_SEARCH ? n + 5 : n) });
            if (jb == J_SEARCH) args.add ("ytsearch" + juce::String (n + 5) + ":" + juce::String (a));
            else if (jb == J_PLISTS) args.add ("https://www.youtube.com/results?search_query=" + juce::URL::addEscapeChars (juce::String (a), true) + "&sp=EgIQAw%3D%3D");
            else args.add ("https://www.youtube.com/playlist?list=" + juce::String (a));
            std::string json, errText;
            int rc = runProcess (args, [&] (const std::string& l) {
                if (! l.empty() && l[0] == '{') json = l;
                else if (l.find ("ERROR") != std::string::npos) errText = l;
            }, 60, [this] { return quit.load(); }, &netProc, &m);
            std::vector<Result> out;
            if (! json.empty())
            {
                auto root = juce::JSON::parse (juce::String::fromUTF8 (json.c_str()));
                if (auto* ent = root.getProperty ("entries", {}).getArray())
                {
                    for (auto& e : *ent)
                    {
                        if (! e.isObject()) continue;
                        Result r;
                        if (jb == J_PLISTS)
                        {
                            juce::String pid = e.getProperty ("id", "").toString(), u = e.getProperty ("url", "").toString();
                            if (u.contains ("list=")) pid = u.fromFirstOccurrenceOf ("list=", false, false).upToFirstOccurrenceOf ("&", false, false);
                            if (pid.length() < 2 || ! juce::StringArray ({ "PL", "OL", "RD", "UU", "VL", "FL" }).contains (pid.substring (0, 2))) continue;
                            r.kind = 1;
                            r.id = pid.toStdString();
                            r.title = e.getProperty ("title", pid).toString().toStdString();
                            auto c = e.getProperty ("playlist_count", {});
                            r.count = c.isVoid() ? "" : c.toString().toStdString();
                        }
                        else
                        {
                            juce::String vid = e.getProperty ("id", "").toString(), title = e.getProperty ("title", "").toString();
                            auto ls = e.getProperty ("live_status", "").toString();
                            if (vid.length() != 11 || title.isEmpty() || ls == "is_live" || ls == "is_upcoming") continue;
                            r.id = vid.toStdString();
                            r.title = title.toStdString();
                            r.secs = jsonInt (e.getProperty ("duration", 0));
                        }
                        juce::String ch = e.getProperty ("channel", "").toString();
                        if (ch.isEmpty()) ch = e.getProperty ("uploader", "").toString();
                        r.chan = ch.toStdString();
                        out.push_back (r);
                        if ((int) out.size() >= n) break;
                    }
                }
            }
            if (json.empty())
            {
                std::string why = rc == -2 ? "YOUTUBE DOES NOT ANSWER: CHECK THE INTERNET" : rc == -3 ? "" : ytWhy (errText.empty() ? "NO ANSWER" : errText);
                if (! why.empty()) setStatus (why);
                std::lock_guard<std::mutex> g (m);
                resHead = "NOTHING (" + why + ")";
            }
            else
            {
                std::lock_guard<std::mutex> g (m);
                res = out;
                resKind = jb == J_PLISTS ? 1 : 0;
                if (jb == J_OPEN) { resHead = upperStr ("PLAYLIST: " + a2 + "  (" + std::to_string (out.size()) + " SONGS)"); lastPl = a; lastPlTitle = a2; }
                else resHead = upperStr (std::to_string (out.size()) + (jb == J_PLISTS ? " PLAYLISTS FOR \"" : " SONGS FOR \"") + a + "\"");
                status = out.empty() ? "NOTHING FOUND" : jb == J_PLISTS ? "CLICK A PLAYLIST TO OPEN IT" : "CLICK A SONG TO PLAY IT  (HEART = FAVS, + = CRATE)";
            }
        }
        netBusy = false;
        lk.lock();
    }
}

/* the song comes as a file into the cache (yt-dlp picks M4A if there is one: Windows and macOS decode it without ffmpeg) */
void Engine::resolveAndDownload (const std::string& id)
{
    auto cache = dir.getChildFile ("cache");
    cache.createDirectory();
    juce::StringArray args = ytBase (ytdlpFile(), denoFile());
    args.addArray ({ "-f", "bestaudio[ext=m4a]/bestaudio/best", "--no-playlist", "--no-mtime", "--newline", "--progress",
                     "--no-simulate", "--print", "after_move:filepath", "-o", cache.getChildFile ("%(id)s.%(ext)s").getFullPathName(),
                     "https://www.youtube.com/watch?v=" + juce::String (id) });
    std::string path, errText;
    int lastPct = -1;
    int rc = runProcess (args, [&] (const std::string& l) {
        if (l.rfind ("[download]", 0) == 0)
        {
            auto pc = l.find ('%');
            if (pc != std::string::npos)
            {
                size_t b = pc;
                while (b > 0 && (isdigit ((unsigned char) l[b - 1]) || l[b - 1] == '.')) b--;
                int p = (int) atof (l.substr (b, pc - b).c_str());
                if (p != lastPct) { lastPct = p; setStatus ("DOWNLOADING " + std::to_string (p) + "%"); }
            }
        }
        else if (l.find ("ERROR") != std::string::npos) errText = l;
        else if (! l.empty() && l[0] != '[' && juce::File::isAbsolutePath (juce::String::fromUTF8 (l.c_str()))) path = l;
    }, 300, [this, g = gen.load()] { return quit.load() || g != gen.load(); }, &netProc, &m);
    std::lock_guard<std::mutex> lk (m);
    if (sstate != ST_RESOLVE || songId != id) return;   /* another song meanwhile */
    juce::File f = juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())) ? juce::File (juce::String::fromUTF8 (path.c_str())) : juce::File();
    if (rc == 0 && ! f.existsAsFile()) f = cacheFileFor (id);   /* a path in another console encoding: find it by its id */
    if (rc != 0 || ! f.existsAsFile())
    {
        sstate = ST_ERROR;
        loading = false;
        status = rc == -2 ? "DOWNLOAD TOO SLOW: CHECK THE INTERNET" : ytWhy (errText.empty() ? "COULD NOT DOWNLOAD THIS SONG" : errText);
        return;
    }
    sstate = ST_LOAD;
    loadPath = f.getFullPathName().toStdString();
    loadGen = gen;
    loadReq = 1;   /* 1: fresh download (goes to the history), 2: from the cache */
    status = "DECODING...";
    loadCv.notify_one();
}

juce::File Engine::cacheFileFor (const std::string& id) const
{
    if (id.empty()) return {};
    auto cache = dir.getChildFile ("cache");
    for (auto& f : cache.findChildFiles (juce::File::findFiles, false, juce::String (id) + ".*"))
    {
        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".part" || ext == ".ytdl" || ext == ".tmp" || f.getFileNameWithoutExtension() != juce::String (id)) continue;
        if (f.getSize() > 1000) return f;
    }
    return {};
}

/* ------------------------------------------------------------------ loading a song */
void Engine::analyse (int from, int to)   /* peaks and onset strength per hop of frames that just arrived */
{
    int h0 = from / HOP, h1 = to / HOP;
    for (int h = h0; h < h1 && h < NHOP; h++)
    {
        const int16_t* d = buf.get() + (size_t) h * HOP * 2;
        int pk = 0;
        double e = 0;
        int prev = h ? (d[-2] + d[-1]) : 0;
        for (int i = 0; i < HOP; i++)
        {
            int l = d[2 * i], r = d[2 * i + 1];
            int a = std::max (std::abs (l), std::abs (r));
            if (a > pk) pk = a;
            int mm = l + r, df = mm - prev;
            prev = mm;
            e += (double) df * df;
        }
        peak[h] = (uint8_t) (pk >> 7);
        odf[h] = (float) std::log1p (e / HOP / 1e4);
    }
    if (h1 > nhop) nhop = h1;
}

void Engine::loadFinish (unsigned g, bool ok, const std::string& why)
{
    std::lock_guard<std::mutex> lk (m);
    if (g != gen) return;
    loading = false;
    total = avail.load();
    sstate = (ok || avail > SR) ? ST_READY : ST_ERROR;
    status = why;
}

void Engine::loadMain()
{
    std::unique_lock<std::mutex> lk (m);
    while (! quit)
    {
        if (! loadReq) { loadCv.wait (lk); continue; }
        int req = loadReq;
        unsigned g = loadGen;
        juce::File f (juce::String::fromUTF8 (loadPath.c_str()));
        std::string id = songId, title = songTitle;
        loadReq = 0;
        lk.unlock();
        if (g != gen) { lk.lock(); continue; }
        int pos = 0;
        std::string err;
        bool ok = decodeAudio (f, ffmpegFile(), [&] (const int16_t* d, int frames) {
            if (pos + frames > MAXF) frames = MAXF - pos;
            std::lock_guard<std::mutex> l2 (m);
            if (g != gen || quit) return false;
            memcpy (buf.get() + (size_t) pos * 2, d, (size_t) frames * 4);
            analyse (pos, pos + frames);
            avail.store (pos + frames, std::memory_order_release);
            pos += frames;
            return pos < MAXF;
        }, [this, g] { return quit.load() || g != gen.load(); }, &loadProc, &m, err);
        if (g == gen && pos > SR)
        {
            f.setLastModificationTime (juce::Time::getCurrentTime());   // the cache keeps the newest
            if (req == 1)
            {
                Job j;
                j.kind = JOB_HIST;
                j.path = id + "\t" + std::to_string (pos / SR) + "\t" + title;
                jobAdd (std::move (j));
            }
        }
        loadFinish (g, pos > SR, pos > SR ? (pos >= MAXF ? "LOADED THE FIRST 15 MINUTES" : req == 2 ? "LOADED FROM THE CACHE: PLAY, THEN CHOP" : "LOADED: PLAY, THEN CHOP")
                                          : (err.empty() ? std::string ("COULD NOT DECODE THIS SONG") : err));
        (void) ok;
        lk.lock();
    }
}

/* keep the cache to the 120 newest songs; songs a crate wants kept stay */
void Engine::cacheTrim()
{
    auto files = dir.getChildFile ("cache").findChildFiles (juce::File::findFiles, false, "*");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    int kept = 0;
    for (auto& f : files)
    {
        auto nm = f.getFileNameWithoutExtension().toStdString();
        if (nm == songId) continue;
        if (++kept > 120) f.deleteFile();
    }
}

/* freeze every pad that plays from the current song: it gets its own copy (with 1 s of room on each side) */
void Engine::freezePads()
{
    for (int i = 0; i < NPAD; i++)
    {
        Pad& p = pad[i];
        if (! p.used || p.s) continue;
        int a = clampi (p.start - SR, 0, avail), b = clampi (p.end + SR, 0, avail);
        if (b - a < 64) { padClear (p); continue; }
        auto s = std::make_shared<Sample>();
        s->d.assign (buf.get() + (size_t) a * 2, buf.get() + (size_t) b * 2);
        s->len = b - a;
        s->file = (songId.empty() ? std::string ("song") : songId) + "_" + std::to_string (a) + "_" + std::to_string (b) + ".raw";
        p.s = s;
        p.start -= a;
        p.end -= a;
        p.autoEnd = false;
        Job j;
        j.kind = JOB_RAW;
        j.path = dir.getChildFile ("pads").getChildFile (s->file).getFullPathName().toStdString();
        j.d = s->d;
        jobAdd (std::move (j));
    }
    undoStack.clear();   /* snapshots that still point into the old song can't come back */
}

void Engine::newSong (const std::string& id, const std::string& title, int secs)
{
    voicesOff();
    if (avail > 0) freezePads();
    ensureBuffers();
    {
        std::lock_guard<std::mutex> lk (m);
        gen++;
        if (loadProc) loadProc->kill();
        avail = 0;
        total = secs > 0 ? std::min (secs, MAX_SEC) * SR : 0;
        nhop = 0;
        loading = true;
        songId = id;
        songTitle = title;
        songSecs = secs;
    }
    playing = false;
    ppos = 0;
    cue = 0;
    lazyNext = bank * 16;
    memset (piso, 0, sizeof piso);
}

void Engine::openSong (const std::string& id, const std::string& title, int secs, bool autoplay)
{
    if (id.empty()) return;
    newSong (id, title, secs);
    cacheTrim();
    auto cf = cacheFileFor (id);
    {
        std::lock_guard<std::mutex> lk (m);
        if (cf.existsAsFile())
        {
            sstate = ST_LOAD;
            loadPath = cf.getFullPathName().toStdString();
            loadGen = gen;
            loadReq = 2;
            status = "LOADING FROM THE CACHE...";
            loadCv.notify_one();
        }
        else
        {
            sstate = ST_RESOLVE;
            status = "ASKING YOUTUBE...";
        }
    }
    if (! cf.existsAsFile()) netRequest (J_RESOLVE, id, 1);
    playing = autoplay;
    pfade = 0;
}

void Engine::loadSong (const std::string& id, const std::string& title, int secs, bool autoplay)
{
    std::lock_guard<std::recursive_mutex> g (api);
    int st;
    { std::lock_guard<std::mutex> lk (m); st = sstate; }
    if (id == songId && st != ST_ERROR && st != ST_IDLE) { playing = autoplay ? true : playing; return; }
    openSong (id, title, secs, autoplay);
}

/* ------------------------------------------------------------------ chopping */
void Engine::fixAutoEnds (int bk)
{
    for (int i = bk * 16; i < bk * 16 + 16; i++)
    {
        Pad& p = pad[i];
        if (! p.used || p.s || ! p.autoEnd) continue;
        int nxt = -1;
        for (int j = bk * 16; j < bk * 16 + 16; j++)
        {
            const Pad& o = pad[j];
            if (j == i || ! o.used || o.s) continue;
            if (o.start > p.start && (nxt < 0 || o.start < nxt)) nxt = o.start;
        }
        int lim = loading ? (total ? total.load() : avail.load()) : avail.load();
        int e = nxt > 0 ? nxt : p.start + 8 * SR;
        if (e > lim && lim > p.start) e = lim;
        if (e - p.start > 16 * SR) e = p.start + 16 * SR;
        p.end = e;
    }
}

void Engine::makeChop (int idx, int start, int end, bool autoEnd)
{
    Pad& p = pad[idx];
    int kp = p.used ? p.pitch : 0;
    float kl = p.used ? p.level : 1.0f;
    int km = p.used ? p.mode : 0;
    padClear (p);
    p.used = true;
    p.start = std::max (0, start);
    p.end = end;
    p.autoEnd = autoEnd;
    p.pitch = kp;
    p.level = kl;
    p.mode = km;
    p.iso = iso;
}

void Engine::lazyChop (int hitPad)
{
    if (! songOk()) { setStatus ("LOAD A SONG FIRST"); return; }
    int pos = (int) ppos;
    int idx;
    if (assign == 1 && hitPad >= 0) idx = hitPad;
    else
    {
        idx = lazyNext;
        if (idx / 16 != bank) idx = bank * 16;
    }
    undoPush();
    makeChop (idx, pos - 441, pos + 8 * SR, true);   /* 10 ms early: a press comes a little late */
    fixAutoEnds (idx / 16);
    sel = idx;
    if (assign == 0)
    {
        int nx = idx + 1;
        if (nx >= bank * 16 + 16) nx = bank * 16;
        lazyNext = nx;
    }
    setStatus ("CHOP " + pname (idx) + " AT " + fmtTime (pos / (double) SR, true));
}

void Engine::autoHits()
{
    int nh = nhop;
    if (nh < 16) { setStatus ("LOAD A SONG FIRST"); return; }
    std::vector<float> on ((size_t) nh, 0.0f);
    double sum = 0, sq = 0;
    for (int h = 3; h < nh; h++)
    {
        float mm = (odf[h - 1] + odf[h - 2] + odf[h - 3]) / 3;
        float x = odf[h] - mm;
        on[(size_t) h] = x > 0 ? x : 0;
        sum += on[(size_t) h];
        sq += (double) on[(size_t) h] * on[(size_t) h];
    }
    double mean = sum / nh, sd = std::sqrt (std::max (0.0, sq / nh - mean * mean));
    double thr = mean + (2.6 - 2.2 * sens) * sd;
    int gap = (int) (0.14 * SR / HOP) + 1;
    int h = std::max (1, cue / HOP), got = 0, last = -gap;
    int base = bank * 16;
    int starts[16];
    for (; h < nh - 1 && got < 16; h++)
    {
        float x = on[(size_t) h];
        if (x < thr || x < on[(size_t) h - 1] || x < on[(size_t) h + 1] || h - last < gap) continue;
        starts[got++] = std::max (0, h * HOP - HOP / 2);
        last = h;
    }
    if (! got) { setStatus ("NO HITS FOUND: RAISE SENSITIVITY"); return; }
    undoPush();
    for (int i = 0; i < got; i++)
    {
        int e = i + 1 < got ? starts[i + 1] : starts[i] + 2 * SR;
        if (e - starts[i] > 4 * SR) e = starts[i] + 4 * SR;   /* a hit is short: at most 4 s */
        makeChop (base + i, starts[i], e, false);
    }
    sel = base;
    setStatus (std::to_string (got) + " HITS ON BANK " + std::string (1, (char) ('A' + bank)) + " (FROM THE CUE)");
}

void Engine::autoBeats()
{
    if (! songOk()) { setStatus ("LOAD A SONG FIRST"); return; }
    double len = SLICE_BEATS[slice] * 60.0 / clampi (bpm, 40, 250) * SR;
    int base = bank * 16, got = 0;
    int lim = loading && total ? total.load() : avail.load();
    undoPush();
    for (int i = 0; i < 16; i++)
    {
        double a = cue + i * len;
        if (a + 64 > lim) break;
        double b = a + len;
        if (b > lim) b = lim;
        makeChop (base + i, (int) a, (int) b, false);
        got++;
    }
    sel = base;
    setStatus (std::to_string (got) + " SLICES OF " + SLICE_NAMES[slice] + " AT " + std::to_string (bpm) + " BPM");
}

void Engine::detectBpm()
{
    std::lock_guard<std::recursive_mutex> g (api);
    int nh = nhop;
    if (nh < 400) { setStatus ("NEED MORE OF THE SONG FIRST"); return; }
    int h0 = cue / HOP, n = nh - h0;
    if (n > 2600) n = 2600;   /* ~30 s */
    if (n < 400) { h0 = std::max (0, nh - 400); n = nh - h0; }
    std::vector<float> on ((size_t) n, 0.0f);
    for (int i = 3; i < n; i++)
    {
        int h = h0 + i;
        float x = odf[h] - (odf[h - 1] + odf[h - 2] + odf[h - 3]) / 3;
        on[(size_t) i] = x > 0 ? x : 0;
    }
    double best = -1;
    int bl = 0;
    double hz = (double) SR / HOP;
    int lmin = (int) (hz * 60 / 180), lmax = (int) (hz * 60 / 70) + 1;
    for (int l = lmin; l <= lmax; l++)
    {
        double s = 0;
        for (int i = 0; i + l < n; i++) s += on[(size_t) i] * on[(size_t) (i + l)];
        s /= (n - l);
        double b = hz * 60 / l, w = 1.0 - 0.15 * std::fabs (std::log2 (b / 105.0));   /* a light bias toward 85-130 BPM */
        if (s * w > best) { best = s * w; bl = l; }
    }
    if (! bl) return;
    /* the best lag between two hops: a parabola through its neighbours (the MPC version rounded to whole hops: 99 for 100 BPM) */
    auto score = [&] (int l) { double s = 0; for (int i = 0; i + l < n; i++) s += on[(size_t) i] * on[(size_t) (i + l)]; return s / (n - l); };
    double ym = score (bl - 1), y0 = score (bl), yp = score (bl + 1), den = ym - 2 * y0 + yp, lag = bl;
    if (den < 0) lag = bl + juce::jlimit (-0.5, 0.5, 0.5 * (ym - yp) / den);
    double b = hz * 60 / lag;
    while (b > 170) b /= 2;
    while (b < 70) b *= 2;
    bpm = (int) std::lround (b);
    setStatus ("TEMPO ABOUT " + std::to_string (bpm) + " BPM (CHECK IT BY EAR)");
}

/* ------------------------------------------------------------------ voices */
void Engine::voiceStart (int idx, float vel)
{
    const Pad& p = pad[idx];
    if (! p.used) return;
    int len = p.s ? p.s->len : avail.load();
    if (p.start >= len - 2) return;
    if (p.grp)   /* mute group: the others in the group let go quickly */
        for (auto& vo : v)
            if (vo.on && vo.pad != idx && pad[vo.pad].grp == p.grp) { vo.stage = 1; vo.fast = true; }
    Voice* vv = nullptr;
    for (auto& vo : v) if (vo.on && vo.pad == idx) { vv = &vo; break; }   /* retrigger: same voice */
    if (! vv) for (auto& vo : v) if (! vo.on) { vv = &vo; break; }
    if (! vv)
    {
        vv = &v[0];
        for (auto& vo : v) if (vo.env < vv->env) vv = &vo;   /* steal the quietest */
    }
    *vv = Voice();
    vv->on = true;
    vv->pad = idx;
    vv->s = p.s;
    vv->start = p.start;
    vv->end = p.end > p.start ? p.end : len;
    vv->rev = p.rev;
    vv->iso = p.iso;
    vv->rate = std::pow (2.0, p.pitch / 12.0) * ratio;
    vv->pos = vv->rev ? vv->end - 1 : vv->start;
    vv->vel = (velOn ? (0.25f + 0.75f * vel * vel) : 1.0f) * p.level;
    float att = p.att < 2 ? 2.0f : (float) p.att;
    vv->attN = std::max (1, (int) (att * hsr / 1000));
    vv->attInc = 1.0f / vv->attN;
    vv->env = 0;
    vv->sus = p.sus;
    float dec = p.dec < 5 ? 5.0f : (float) p.dec;
    vv->decMul = std::exp (-5.0f / (float) (dec * 0.001f * hsr));
    float rel = p.rel < 5 ? 5.0f : (float) p.rel;
    vv->relMul = std::exp (-6.9f / (float) (rel * 0.001f * hsr));
}
void Engine::voiceRelease (int idx)
{
    for (auto& vo : v)
        if (vo.on && vo.pad == idx && pad[idx].mode == 1) vo.stage = 1;
}
void Engine::stopAllInt()
{
    for (auto& vo : v) if (vo.on) { vo.stage = 1; vo.fast = true; }
    playing = false;
}

/* ------------------------------------------------------------------ the turntable */
double Engine::deckSpeed() const
{
    if (prange == 3) return std::pow (2.0, (dpitch - 500) / 500.0);   /* +-12 semitones */
    return 1.0 + (dpitch - 500) / 500.0 * RANGE_PCT[prange] / 100.0;
}

/* ------------------------------------------------------------------ pad map (LEARN), as on the MPC */
static const signed char SCALES[][13] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1 }, { 0, 2, 4, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 8, 10, -1 }, { 0, 2, 3, 5, 7, 8, 11, -1 },
    { 0, 2, 3, 5, 7, 9, 11, -1 }, { 0, 2, 3, 5, 7, 9, 10, -1 }, { 0, 1, 3, 5, 7, 8, 10, -1 }, { 0, 2, 4, 6, 7, 9, 11, -1 },
    { 0, 2, 4, 5, 7, 9, 10, -1 }, { 0, 1, 3, 5, 6, 8, 10, -1 }, { 0, 2, 4, 7, 9, -1 }, { 0, 3, 5, 7, 10, -1 }, { 0, 3, 5, 6, 7, 10, -1 },
    { 0, 2, 3, 4, 7, 9, -1 }, { 0, 2, 4, 6, 8, 10, -1 }, { 0, 2, 3, 5, 6, 8, 9, 11, -1 }, { 0, 1, 3, 4, 6, 7, 9, 10, -1 },
    { 0, 1, 4, 5, 7, 8, 10, -1 }, { 0, 2, 3, 6, 7, 8, 11, -1 }, { 0, 1, 5, 7, 10, -1 }, { 0, 2, 3, 7, 8, -1 }, { 0, 2, 5, 7, 10, -1 },
    { 0, 1, 4, 5, 7, 8, 11, -1 }, { 0, 2, 4, 5, 7, 9, 10, 11, -1 } };
static constexpr int NSCALE = (int) (sizeof SCALES / sizeof SCALES[0]);
static int scaleNote (int s, int k)
{
    int n = 0;
    while (SCALES[s][n] >= 0) n++;
    return SCALES[s][k % n] + 12 * (k / n);
}
void Engine::padmapBuild()
{
    int span = mapLast - mapFirst;
    std::vector<std::array<short, 64>> votes (160);
    for (auto& r : votes) r.fill (0);
    bool any = false;
    for (int s = 0; s < NSCALE; s++)
    {
        if (scaleNote (s, 15) != span) continue;
        any = true;
        for (int k = 0; k < 64; k++) { int o = scaleNote (s, k); if (o < 160) votes[o][k]++; }
    }
    for (int o = 0; o < 160; o++)
    {
        int best = -1, bv = 0;
        for (int k = 0; k < 64; k++) if (votes[o][k] > bv) { bv = votes[o][k]; best = k; }
        if (! any || best < 0)
        {
            double sp = span > 0 ? span : 15, period = sp + sp / 15.0, r = std::fmod (o, period);
            best = (int) std::lround (r * 15.0 / sp);
        }
        padmap[o] = (signed char) (best % 16);
    }
}
int Engine::noteToPad (int note)
{
    int o = note - mapFirst;
    if (o < 0) o = ((o % 12) + 12) % 12;
    if (o >= 160) o = 159;
    return bank * 16 + padmap[o];
}

void Engine::midiNoteOn (int note, float vel)   /* audio thread, api lock held by the caller (processBlock) */
{
    if (learn == 1)
    {
        baseNote = note;
        learn = 2;
        setStatus ("FIRST PAD = NOTE " + std::to_string (note) + ". NOW HIT THE LAST PAD (PAD 16)");
        return;
    }
    if (learn == 2)
    {
        if (note <= baseNote) { setStatus ("THE LAST PAD MUST BE HIGHER THAN THE FIRST (NOTE " + std::to_string (baseNote) + "). HIT PAD 16"); return; }
        mapFirst = baseNote;
        mapLast = note;
        learn = 0;
        padmapBuild();
        setStatus ("PADS LEARNED: NOTES " + std::to_string (mapFirst) + " TO " + std::to_string (mapLast) + " ON ALL 16 PADS");
        Job j;
        j.kind = JOB_FILE;
        j.path = dir.getChildFile ("padmap.txt").getFullPathName().toStdString();
        j.text = std::to_string (mapFirst) + " " + std::to_string (mapLast) + "\n";
        jobAdd (std::move (j));
        return;
    }
    padHit (noteToPad (note), vel, false);
}
void Engine::midiNoteOff (int note) { voiceRelease (noteToPad (note)); }
void Engine::midiAllOff() { for (auto& vo : v) vo.stage = 1; }

/* ------------------------------------------------------------------ render */
void Engine::render (float* outL, float* outR, int frames, const HostInfo& host)
{
    std::lock_guard<std::recursive_mutex> g (api);
    if (host.valid)
    {   /* the host's transport: STOP ends every sound, as it does for the rest of the project */
        if ((wasPlaying && ! host.playing) || (! host.playing && host.ppq >= 0 && host.ppq + 0.001 < lastPpq)) stopAllInt();
        lastPpq = host.ppq;
        wasPlaying = host.playing;
        if (host.bpm > 0) hostBpm = (float) host.bpm;
    }
    for (int at = 0; at < frames; at += 128)
        renderBlock (outL + at, outR + at, std::min (128, frames - at));
}

void Engine::renderBlock (float* L, float* R, int frames)
{
    int av = avail.load (std::memory_order_acquire);
    float pl[2 * 128];
    memset (pl, 0, sizeof pl);
    if (stopReq.exchange (false)) stopAllInt();
    /* ---- the player on the turntable: plays, pitches (or stretches with key lock), scratches under the mouse */
    bool s = av > 0 && scrDown;
    if (s && ! scr) { anchor = ppos; scrTurns = 0; }
    scr = s;
    float want = (playing || scr) ? 1.0f : 0.0f;
    const float fadeK = (float) std::min (1.0, 0.004 * ratio);
    if (av > 0 && (want > 0 || pfade > 0.0005f))
    {
        double pos = ppos, p0 = pos, speed = deckSpeed(), rate = speed * ratio;
        if (scr)
        {
            double target = anchor + scrTurns * SCR_FRAMES_PER_TURN;
            target = juce::jlimit (0.0, (double) std::max (0, av - 2), target);
            rate = (target - pos) / (3.0 * frames);
            rate = juce::jlimit (-8.0 * ratio, 8.0 * ratio, rate);
        }
        bool gran = klock && ! scr && std::fabs (speed - 1.0) > 0.0005;
        if (! gran) { ngr = 0; hopc = 0; }
        enum { GL = 1024, HS = 256 };
        static float win[GL + 1];
        static bool winOk = false;
        if (! winOk) { for (int k = 0; k <= GL; k++) win[k] = 0.5f - 0.5f * std::cos (6.2831853f * k / GL); winOk = true; }
        for (int i = 0; i < frames; i++)
        {
            pfade += (want - pfade) * fadeK;
            if (pos >= av - 2 && rate >= 0)
            {
                if (loading) continue;   /* still decoding: wait for the data */
                if (loop && ! scr) { pos = cue < av ? cue : 0; ngr = 0; }
                else { playing = false; want = 0; pos = av - 2 > 0 ? av - 2 : 0; continue; }
            }
            float l = 0, r = 0;
            if (gran)
            {   /* time stretch: grains read at normal pitch, the grain start moves with the tempo */
                if (hopc <= 0)
                {
                    if (ngr < 6) { gr[ngr].src = pos; gr[ngr].age = 0; ngr++; }
                    pos += HS * speed;
                    hopc += HS / ratio;
                }
                hopc -= 1;
                for (int k = 0; k < ngr; k++)
                {
                    double ix = gr[k].src + gr[k].age;
                    if (ix >= 0 && ix < av - 1)
                    {
                        float a, b;
                        fetch (buf.get(), av, ix, a, b);
                        float w = win[std::min ((int) GL, (int) gr[k].age)];
                        l += w * a;
                        r += w * b;
                    }
                    gr[k].age += ratio;
                }
                for (int k = 0; k < ngr;) { if (gr[k].age >= GL) gr[k] = gr[--ngr]; else k++; }
                l *= 0.5f;
                r *= 0.5f;
            }
            else
            {
                fetch (buf.get(), av, pos, l, r);
                pos += rate;
                if (pos < 0) pos = 0;
            }
            isoRun (filtRT, piso, iso, l, r);
            pl[2 * i] = l * vol * pfade;
            pl[2 * i + 1] = r * vol * pfade;
        }
        double moved = gran ? frames * ratio * speed : pos - p0;
        if (playing || scr) ang += moved / SR * (100.0 / 3.0 / 60.0);   /* 33 1/3 turns a minute at normal speed */
        ppos = pos;
    }
    const float fastMul = (float) std::pow (0.9985, ratio);
    for (int n = 0; n < frames; n++)
    {
        float l0 = pl[2 * n], r0 = pl[2 * n + 1];
        for (auto& vo : v)
        {
            if (! vo.on) continue;
            const int16_t* d = vo.s ? vo.s->d.data() : buf.get();
            int len = vo.s ? vo.s->len : av;
            if (d == nullptr) { vo.on = false; continue; }
            bool done = vo.rev ? (vo.pos < vo.start) : (vo.pos >= vo.end || (int) vo.pos >= len - 1);
            if (done) { vo.on = false; vo.s.reset(); continue; }
            float l, r;
            fetch (d, len, vo.pos, l, r);
            if (vo.stage == 0)
            {
                if (vo.age < vo.attN) vo.env = (vo.age + 1) * vo.attInc;
                else vo.env = vo.sus + (vo.env - vo.sus) * vo.decMul;
            }
            else
            {
                vo.env *= vo.fast ? fastMul : vo.relMul;
                if (vo.env < 0.0005f) { vo.on = false; vo.s.reset(); continue; }
            }
            vo.age++;
            double left = vo.rev ? vo.pos - vo.start : vo.end - vo.pos;
            float tail = left < 96 ? (float) (left / 96.0) : 1.0f;
            isoRun (filtRT, vo.fz, vo.iso, l, r);
            float gg = vo.env * vo.vel * tail * padvol;
            l0 += l * gg;
            r0 += r * gg;
            vo.pos += vo.rev ? -vo.rate : vo.rate;
        }
        /* soft clip */
        l0 = l0 > 1.5f ? 1.0f : l0 < -1.5f ? -1.0f : l0 - l0 * l0 * l0 * (4.0f / 27.0f);
        r0 = r0 > 1.5f ? 1.0f : r0 < -1.5f ? -1.0f : r0 - r0 * r0 * r0 * (4.0f / 27.0f);
        L[n] = l0;
        R[n] = r0;
    }
}

/* ------------------------------------------------------------------ the editor's window */
int Engine::viewLen() const { return loading && total ? total.load() : avail.load(); }
void Engine::waveFit()
{
    const Pad& sp = pad[sel];
    winSel = sel;
    winZoom = zoom;
    if (zoom == 1 && sp.used)
    {
        double l = sp.end - sp.start, mm = l * 0.35 + SR / 4;
        double lim = sp.s ? sp.s->len : viewLen();
        win0 = std::max (0.0, sp.start - mm);
        win1 = std::min (lim, sp.end + mm);
    }
    else { win0 = 0; win1 = viewLen(); }
    if (win1 < win0 + 64) win1 = win0 + 64;
}
void Engine::waveCheck()
{
    const Pad& sp = pad[sel];
    if (winSel != sel || winZoom != zoom) { waveFit(); return; }
    if (zoom == 0) { double l = viewLen(); if (std::fabs (win1 - l) > 1) win1 = std::max (l, 64.0); return; }
    if (sp.used && (sp.start < win0 || sp.end > win1)) waveFit();
}

void Engine::peaks (double f0, double f1, int cols, bool own, std::vector<float>& out)
{
    std::lock_guard<std::recursive_mutex> g (api);
    out.assign ((size_t) std::max (0, cols), 0.0f);
    const Pad& sp = pad[sel];
    const int16_t* d = own && sp.s ? sp.s->d.data() : buf.get();
    int dlen = own && sp.s ? sp.s->len : avail.load();
    if (d == nullptr || dlen <= 0 || cols <= 0) return;
    int nh = nhop;
    for (int c = 0; c < cols; c++)
    {
        int a = (int) (f0 + (f1 - f0) * c / cols), b = (int) (f0 + (f1 - f0) * (c + 1) / cols);
        if (b <= a) b = a + 1;
        int pk = 0;
        if (d != buf.get() || b - a < HOP * 2)
        {
            int step = (b - a) / 64 + 1;
            for (int f = std::max (0, a); f < b && f < dlen; f += step)
                pk = std::max (pk, std::max (std::abs ((int) d[2 * f]), std::abs ((int) d[2 * f + 1])));
            pk >>= 7;
        }
        else
            for (int h = a / HOP; h <= b / HOP && h < nh; h++) pk = std::max (pk, (int) peak[h]);
        out[(size_t) c] = std::sqrt (pk / 255.0f);
    }
}

/* ------------------------------------------------------------------ public calls (the editor) */
#define API std::lock_guard<std::recursive_mutex> apiGuard_ (api)

void Engine::setQuery (const std::string& s) { API; q = s; }
std::string Engine::query() { API; return q; }

void Engine::search (bool lists)
{
    API;
    if (q.empty()) { setStatus ("TYPE SOMETHING FIRST"); return; }
    {
        std::lock_guard<std::mutex> lk (m);
        res.clear();
        resHead = upperStr ("SEARCHING \"" + q + "\"...");
        status = "SEARCHING...";
    }
    netRequest (lists ? J_PLISTS : J_SEARCH, q, lists ? 20 : 40);
}

void Engine::showHistory()
{
    API;
    juce::StringArray lines;
    lines.addLines (dir.getChildFile ("history.txt").loadFileAsString());
    std::vector<Result> out;
    for (auto& l : lines)
    {
        auto f = juce::StringArray::fromTokens (l, "\t", "");
        if (f.size() < 3 || f[0].isEmpty()) continue;
        Result r;
        r.id = f[0].toStdString();
        r.secs = f[1].getIntValue();
        r.title = f[2].toStdString();
        r.kind = 2;
        out.push_back (r);
    }
    std::lock_guard<std::mutex> lk (m);
    res = out;
    resKind = 2;
    resHead = out.empty() ? "HISTORY IS EMPTY" : "HISTORY: PLAYED BEFORE";
    status = out.empty() ? "PLAY SOMETHING FIRST" : "CLICK A SONG (CACHED SONGS LOAD AT ONCE)";
}

void Engine::setResults (const std::vector<Result>& r, const std::string& head, int kind)
{
    std::lock_guard<std::mutex> lk (m);
    res = r;
    resKind = kind;
    resHead = head;
}

void Engine::tapResult (int i)
{
    API;
    Result e;
    {
        std::lock_guard<std::mutex> lk (m);
        if (i < 0 || i >= (int) res.size()) return;
        e = res[(size_t) i];
    }
    if (e.kind == 1) openPlaylist (e.id, e.title);
    else
    {
        int st;
        { std::lock_guard<std::mutex> lk (m); st = sstate; }
        if (e.id == songId && st != ST_ERROR && st != ST_IDLE) playing = ! playing;   /* the song that is loaded: play/pause */
        else openSong (e.id, e.title, e.secs, true);
    }
}

void Engine::openPlaylist (const std::string& id, const std::string& title)
{
    API;
    {
        std::lock_guard<std::mutex> lk (m);
        res.clear();
        resHead = upperStr ("OPENING " + title + "...");
        status = "OPENING PLAYLIST...";
    }
    netRequest (J_OPEN, id, 200, title);
}

void Engine::playToggle()
{
    API;
    int st;
    { std::lock_guard<std::mutex> lk (m); st = sstate; }
    if (avail <= 0 && st != ST_LOAD && st != ST_RESOLVE) { setStatus ("PICK A SONG FIRST"); return; }
    playing = ! playing;
}
void Engine::stop() { API; playing = false; ppos = cue; for (auto& vo : v) vo.stage = 1; }
void Engine::rew() { API; ppos = ppos > 5 * SR ? ppos - 5 * SR : 0; }
void Engine::fwd() { API; double p = ppos + 5 * SR; if (p < avail) ppos = p; }
void Engine::seek (double f)
{
    API;
    int len = viewLen();
    int p = (int) (juce::jlimit (0.0, 1.0, f) * len);
    ppos = p < avail ? p : (avail > 0 ? avail - 1 : 0);
}
void Engine::setLoop (bool on) { API; loop = on; }
void Engine::deckCue() { API; playing = false; ppos = cue < avail ? cue : 0; }
void Engine::setVol (float x) { API; vol = clampf (x, 0, 1); }
void Engine::setPadVol (float x) { API; padvol = clampf (x, 0, 1); }
void Engine::setDeckPitch (float x)
{
    API;
    int val = (int) (clampf (x, 0, 1) * 1000 + 0.5f);
    if (val > 490 && val < 510) val = 500;   /* detent in the middle: exactly 0 */
    dpitch = val;
}
void Engine::setRange (int r) { API; prange = clampi (r, 0, 3); }
void Engine::setKeyLock (bool on) { API; klock = on; }
void Engine::scratchTouch (bool down) { API; scrDown = down; if (down) { anchor = ppos; scrTurns = 0; scr = avail > 0; } }
void Engine::scratchMove (double turns) { API; scrTurns += turns; }

void Engine::setChopBy (int c) { API; chopBy = clampi (c, 0, 2); }
void Engine::setAssign (int a) { API; assign = clampi (a, 0, 1); }
void Engine::setArm (bool on) { API; arm = on; }
void Engine::setSens (float s) { API; sens = clampf (s, 0, 1); }
void Engine::setBpm (int b) { API; bpm = clampi (b, 70, 170); }
void Engine::setSlice (int s) { API; slice = clampi (s, 0, 4); }
void Engine::setVelocity (bool on) { API; velOn = on; }
void Engine::tapChop() { API; lazyChop (-1); }
void Engine::autoChop()
{
    API;
    if (chopBy == CH_HITS) autoHits();
    else if (chopBy == CH_BEATS) autoBeats();
    else setStatus ("LAZY: PLAY THE SONG AND HIT PADS (OR TAP)");
}
void Engine::bpmFromHost()
{
    API;
    if (hostBpm > 20) { bpm = clampi ((int) std::lround (hostBpm), 70, 170); setStatus ("TEMPO FROM THE HOST: " + std::to_string (bpm) + " BPM"); }
    else setStatus ("THE HOST SENDS NO TEMPO");
}
void Engine::setCue() { API; cue = (int) ppos; setStatus ("CUE AT " + fmtTime (cue / (double) SR, true)); }
void Engine::goCue() { API; ppos = cue < avail ? cue : 0; }
void Engine::clearPad()
{
    API;
    Pad& sp = pad[sel];
    if (sp.used) { undoPush(); voicesOff(); padClear (sp); if (chopBy == CH_LAZY) fixAutoEnds (bank); setStatus ("PAD CLEARED (UNDO BRINGS IT BACK)"); }
    else setStatus ("THIS PAD IS ALREADY EMPTY");
}
void Engine::clearBank()
{
    API;
    undoPush();
    voicesOff();
    for (int i = bank * 16; i < bank * 16 + 16; i++) padClear (pad[i]);
    lazyNext = bank * 16;
    setStatus ("BANK CLEARED (UNDO BRINGS IT BACK)");
}
void Engine::learnToggle()
{
    API;
    learn = learn ? 0 : 1;
    setStatus (learn ? "LEARN: HIT THE FIRST PAD ON YOUR CONTROLLER (PAD 1, BOTTOM LEFT)" : "LEARN CANCELLED, PADS AS BEFORE");
}

void Engine::exportPads()
{
    API;
    if (exporting) { setStatus ("EXPORT RUNNING..."); return; }
    int used = 0;
    for (auto& p : pad) used += p.used;
    if (! used) { setStatus ("NO CHOPS TO EXPORT"); return; }
    std::string name = safeName (songTitle.empty() ? "chops" : songTitle);
    auto root = dir.getChildFile ("Exports");
    auto d = root.getChildFile (juce::String (name));
    for (int k = 2; d.exists() && k < 1000; k++) d = root.getChildFile (juce::String (name) + " " + juce::String (k));   /* never overwrite */
    d.createDirectory();
    exporting = true;
    int n = 0;
    for (int i = 0; i < NPAD; i++)
    {
        const Pad& p = pad[i];
        if (! p.used) continue;
        const int16_t* src = p.s ? p.s->d.data() : buf.get();
        int slen = p.s ? p.s->len : avail.load();
        int s0 = clampi (p.start, 0, slen), s1 = clampi (p.end, s0, slen);
        if (s1 - s0 < 32 || src == nullptr) continue;
        Job j;
        j.kind = JOB_WAV;
        {   /* A01 .. D16 */
            char nm[8];
            snprintf (nm, sizeof nm, "%c%02d", 'A' + i / 16, i % 16 + 1);
            j.path = d.getChildFile (juce::String (nm) + " " + juce::String (name) + ".wav").getFullPathName().toStdString();
        }
        j.d.assign (src + (size_t) s0 * 2, src + (size_t) s1 * 2);
        j.len = s1 - s0;
        j.pp = p;
        j.pp.s.reset();
        j.pp.start = 0;
        j.pp.end = s1 - s0;
        jobAdd (std::move (j));
        n++;
    }
    {
        std::lock_guard<std::mutex> lk (m);
        exportDir = d.getFullPathName().toStdString();
    }
    Job j;
    j.kind = JOB_DONE;
    j.path = "EXPORTED " + std::to_string (n) + " WAVS TO " + upperStr (d.getFullPathName().toStdString());
    jobAdd (std::move (j));
    setStatus ("EXPORTING...");
}

bool Engine::bouncePadToFile (int idx, const juce::File& f)
{
    std::vector<int16_t> o;
    {
        API;
        if (idx < 0 || idx >= NPAD || ! pad[idx].used) return false;
        const Pad& p = pad[idx];
        const int16_t* src = p.s ? p.s->d.data() : buf.get();
        int slen = p.s ? p.s->len : avail.load();
        if (src == nullptr) return false;
        o = bounce (p, src, slen);
    }
    return ! o.empty() && writeWav (f, o.data(), (int) o.size() / 2);
}

/* ------------------------------------------------------------------ pad actions */
void Engine::doPadOp (int op, int a, int b)   /* a -> b */
{
    if (a == b) { setStatus ("PICK ANOTHER PAD AS TARGET"); return; }
    undoPush();
    voicesOff();
    if (op == 1) pad[b] = pad[a];
    else if (op == 2) { pad[b] = pad[a]; padClear (pad[a]); }
    else std::swap (pad[a], pad[b]);
    setStatus (std::string (OP_NAMES[op]) + " " + pname (a) + " -> " + pname (b) + " DONE (UNDO BRINGS IT BACK)");
    fixAutoEnds (a / 16);
    fixAutoEnds (b / 16);
    sel = b;
}
void Engine::doBankOp (int op, int a, int b)
{
    if (a == b) { setStatus ("PICK ANOTHER BANK AS TARGET"); return; }
    undoPush();
    voicesOff();
    for (int i = 0; i < 16; i++)
    {
        Pad& pa = pad[a * 16 + i];
        Pad& pb = pad[b * 16 + i];
        if (op == 1) pb = pa;
        else if (op == 2) { pb = pa; padClear (pa); }
        else std::swap (pa, pb);
    }
    setStatus ("BANK " + std::string (OP_NAMES[op]) + " " + std::string (1, (char) ('A' + a)) + " -> " + std::string (1, (char) ('A' + b)) + " DONE (UNDO BRINGS IT BACK)");
}
void Engine::selectPadInt (int idx) { sel = clampi (idx, 0, NPAD - 1); }
void Engine::selectPad (int idx) { API; selectPadInt (idx); }

/* a pad clicked on the screen or hit on a controller: a waiting action takes it as its target, a lazy chop cuts, else it plays */
void Engine::padHit (int idx, float vel, bool fromScreen)
{
    API;
    idx = clampi (idx, 0, NPAD - 1);
    if (pendOp)
    {
        int op = pendOp, a = pendSrc;
        pendOp = 0;
        ops.clear();
        doPadOp (op, a, idx);
        return;
    }
    if (chopBy == CH_LAZY && arm && playing && songOk()) { lazyChop (idx); return; }
    selectPadInt (idx);
    voiceStart (idx, vel);
    if (fromScreen && ! pad[idx].used) setStatus (pname (idx) + " IS EMPTY: CHOP IT FIRST");
}
void Engine::padRelease (int idx) { API; voiceRelease (clampi (idx, 0, NPAD - 1)); }

void Engine::selectBank (int b)
{
    API;
    b = clampi (b, 0, 3);
    if (pendBk) { int op = pendBk; pendBk = 0; ops.clear(); doBankOp (op, bank, b); return; }
    if (b != bank) { bank = b; sel = b * 16 + sel % 16; lazyNext = b * 16; }
}

void Engine::padOp (int op)
{
    API;
    Pad& sp = pad[sel];
    pendBk = 0;
    if (op == 4)
    {
        if (sp.used) { undoPush(); voicesOff(); padClear (sp); fixAutoEnds (bank); setStatus ("PAD DELETED (UNDO BRINGS IT BACK)"); }
        return;
    }
    if (pendOp == op) { pendOp = 0; ops.clear(); setStatus ("CANCELLED"); return; }
    if (! sp.used && op != 3) { setStatus ("PICK A PAD WITH A CHOP FIRST"); return; }
    pendOp = op;
    pendSrc = sel;
    ops = std::string (OP_NAMES[op]) + " " + pname (sel) + ": CLICK THE TARGET PAD";
    setStatus (ops);
}
void Engine::bankOp (int op)
{
    API;
    pendOp = 0;
    if (pendBk == op) { pendBk = 0; ops.clear(); setStatus ("CANCELLED"); return; }
    pendBk = op;
    ops = "BANK " + std::string (OP_NAMES[op]) + " " + std::string (1, (char) ('A' + bank)) + ": CLICK THE TARGET BANK";
    setStatus (ops);
}
void Engine::stopAll() { API; stopAllInt(); setStatus ("ALL STOPPED"); }

/* ------------------------------------------------------------------ the editor */
void Engine::setZoom (int z) { API; zoom = clampi (z, 0, 1); waveFit(); }
void Engine::fit() { API; waveFit(); }
void Engine::padStep (int dir_)   /* the next pad of the bank that has a chop */
{
    API;
    int b = bank * 16, cur = sel % 16;
    for (int k = 1; k <= 16; k++)
    {
        int i = (cur + dir_ * k + 32) % 16;
        if (pad[b + i].used) { selectPadInt (b + i); return; }
    }
    selectPadInt (b + (cur + dir_ + 16) % 16);
}
void Engine::setStartFrac (double f)
{
    API;
    Pad& sp = pad[sel];
    if (! sp.used) return;
    waveCheck();
    int x = (int) (win0 + juce::jlimit (0.0, 1.0, f) * (win1 - win0));
    sp.start = clampi (x, 0, sp.end - 64);
    if (! sp.s) fixAutoEnds (sel / 16);
    sp.autoEnd = false;
}
void Engine::setEndFrac (double f)
{
    API;
    Pad& sp = pad[sel];
    if (! sp.used) return;
    waveCheck();
    int len = sp.s ? sp.s->len : avail.load();
    int x = (int) (win0 + juce::jlimit (0.0, 1.0, f) * (win1 - win0));
    sp.end = clampi (x, sp.start + 64, len);
    sp.autoEnd = false;
}
void Engine::moveBounds (int which, int ms)
{
    API;
    Pad& p = pad[sel];
    if (! p.used) { setStatus ("THIS PAD IS EMPTY: PICK A CHOPPED PAD"); return; }
    int len = p.s ? p.s->len : avail.load();
    int d = ms * SR / 1000;
    if (which == 0) p.start = clampi (p.start + d, 0, p.end - 64);
    else p.end = clampi (p.end + d, p.start + 64, len);
    p.autoEnd = false;
    if (which == 0 && ! p.s) fixAutoEnds (sel / 16);
}
void Engine::setPitch (int st) { API; if (pad[sel].used) pad[sel].pitch = clampi (st, -24, 24); }
void Engine::setLevel (float x) { API; if (pad[sel].used) pad[sel].level = clampf (x, 0, 1); }
void Engine::setMode (int mo) { API; if (pad[sel].used) pad[sel].mode = clampi (mo, 0, 1); }
void Engine::setReverse (bool on) { API; if (pad[sel].used) pad[sel].rev = on; }
void Engine::setAttack (int ms) { API; if (pad[sel].used) pad[sel].att = clampi (ms, 0, A_MAX); }
void Engine::setDecay (int ms) { API; if (pad[sel].used) pad[sel].dec = clampi (ms, 0, D_MAX); }
void Engine::setSustain (float s) { API; if (pad[sel].used) pad[sel].sus = clampf (s, 0, 1); }
void Engine::setRelease (int ms) { API; if (pad[sel].used) pad[sel].rel = clampi (ms, 0, R_MAX); }
void Engine::groupStep (int d) { API; if (pad[sel].used) pad[sel].grp = clampi (pad[sel].grp + d, 0, 8); }
void Engine::playSelected() { API; if (! pad[sel].used) setStatus ("THIS PAD IS EMPTY"); voiceStart (sel, 0.9f); }

void Engine::setIso (int i) { API; iso = clampi (i, 0, NISO - 1); }
void Engine::isoBank()
{
    API;
    for (int i = bank * 16; i < bank * 16 + 16; i++) if (pad[i].used) pad[i].iso = iso;
    setStatus ("BANK " + std::string (1, (char) ('A' + bank)) + " NOW PLAYS " + ISO_NAMES[iso]);
}
void Engine::isoSelected()
{
    API;
    if (pad[sel].used) { pad[sel].iso = iso; setStatus ("PAD " + pname (sel) + " NOW PLAYS " + ISO_NAMES[iso]); }
    else setStatus ("THIS PAD IS EMPTY");
}

void Engine::updateYtDlp() { netRequest (J_UPDATE, "", 0); }
void Engine::installDeno() { netRequest (J_DENO, "", 0); }

/* ------------------------------------------------------------------ state (same text format as the MPC version, "CR1") */
std::string Engine::getState()
{
    API;
    std::string o;
    char b[512];
    std::string title = songTitle;
    for (auto& c : title) if (c == '\n' || c == '|') c = ' ';
    o += "CR1\nsong=" + songId + "|" + std::to_string (songSecs) + "|" + title + "\n";
    snprintf (b, sizeof b, "map=%d|%d\ndeck=%d|%d|%d\n", mapFirst, mapLast, dpitch, prange, (int) klock);
    o += b;
    snprintf (b, sizeof b, "set=%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f|%d|%d|%d|%d\n", bank, sel, chopBy, assign, slice, bpm, iso, baseNote,
              (int) loop, sens, vol, cue, zoom, (int) velOn, (int) arm);
    o += b;
    snprintf (b, sizeof b, "mix=%.3f\n", padvol);
    o += b;
    for (int i = 0; i < NPAD; i++)
    {
        const Pad& p = pad[i];
        if (! p.used) continue;
        snprintf (b, sizeof b, "p=%d|%d|%d|%d|%.3f|%d|%d|%d|%d|%d|%d|%d|%.3f|%d|%s\n", i, p.start, p.end, p.pitch, p.level, p.att, p.rel, p.mode,
                  (int) p.rev, p.iso, (int) p.autoEnd, p.dec, p.sus, p.grp, p.s ? p.s->file.c_str() : "");
        o += b;
        if (p.s)
        {   /* the frozen audio must be on disk for the project (it is written when frozen; write it again if it went missing) */
            auto f = dir.getChildFile ("pads").getChildFile (juce::String (p.s->file));
            if (! f.existsAsFile())
            {
                Job j;
                j.kind = JOB_RAW;
                j.path = f.getFullPathName().toStdString();
                j.d = p.s->d;
                jobAdd (std::move (j));
            }
        }
    }
    return o;
}

void Engine::setState (const std::string& s)
{
    API;
    if (s.rfind ("CR1", 0) != 0) return;
    voicesOff();
    for (auto& p : pad) padClear (p);
    undoStack.clear();
    char id[64] = "", title[256] = "";
    int secs = 0;
    juce::StringArray lines;
    lines.addLines (juce::String::fromUTF8 (s.c_str()));
    for (auto& ln : lines)
    {
        std::string line = ln.toStdString();
        if (line.rfind ("song=", 0) == 0)
        {
            sscanf (line.c_str() + 5, "%63[^|]|%d|%255[^\n]", id, &secs, title);
            if (id[0] == '|') id[0] = 0;
        }
        else if (line.rfind ("deck=", 0) == 0)
        {
            int a = 500, b = 3, c = 0;
            sscanf (line.c_str() + 5, "%d|%d|%d", &a, &b, &c);
            dpitch = clampi (a, 0, 1000); prange = clampi (b, 0, 3); klock = c != 0;
        }
        else if (line.rfind ("map=", 0) == 0)
        {
            int a = 0, b = 0;
            if (sscanf (line.c_str() + 4, "%d|%d", &a, &b) == 2 && a >= 0 && b > a && b < 128) { mapFirst = a; mapLast = b; padmapBuild(); }
        }
        else if (line.rfind ("mix=", 0) == 0) padvol = clampf ((float) atof (line.c_str() + 4), 0, 1);
        else if (line.rfind ("set=", 0) == 0)
        {
            float se = 0.5f, vo = 0.8f;
            int lp = 0, vl = 1, ar = 1;
            sscanf (line.c_str() + 4, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%d|%d|%d", &bank, &sel, &chopBy, &assign, &slice, &bpm, &iso, &baseNote,
                    &lp, &se, &vo, &cue, &zoom, &vl, &ar);
            bank = clampi (bank, 0, 3); sel = clampi (sel, 0, NPAD - 1); chopBy = clampi (chopBy, 0, 2); assign = clampi (assign, 0, 1);
            slice = clampi (slice, 0, 4); bpm = clampi (bpm, 70, 170); iso = clampi (iso, 0, NISO - 1); baseNote = clampi (baseNote, 0, 127);
            sens = clampf (se, 0, 1); vol = clampf (vo, 0, 1); zoom = clampi (zoom, 0, 1); cue = clampi (cue, 0, MAXF);
            loop = lp != 0; velOn = vl != 0; arm = ar != 0;
        }
        else if (line.rfind ("p=", 0) == 0)
        {
            Pad p;
            int i = -1, rv = 0, ae = 0;
            char file[128] = "";
            sscanf (line.c_str() + 2, "%d|%d|%d|%d|%f|%d|%d|%d|%d|%d|%d|%d|%f|%d|%127[^\n]", &i, &p.start, &p.end, &p.pitch, &p.level, &p.att,
                    &p.rel, &p.mode, &rv, &p.iso, &ae, &p.dec, &p.sus, &p.grp, file);
            p.rev = rv != 0;
            p.autoEnd = ae != 0;
            p.dec = clampi (p.dec, 0, D_MAX); p.sus = clampf (p.sus, 0, 1); p.grp = clampi (p.grp, 0, 8);
            if (i < 0 || i >= NPAD || p.end <= p.start) continue;
            p.used = true;
            p.pitch = clampi (p.pitch, -24, 24); p.level = clampf (p.level, 0, 1); p.att = clampi (p.att, 0, A_MAX);
            p.rel = clampi (p.rel, 0, R_MAX); p.mode = clampi (p.mode, 0, 1); p.iso = clampi (p.iso, 0, NISO - 1);
            std::string fn = file;
            if (! fn.empty() && fn.find ('/') == std::string::npos && fn.find ('\\') == std::string::npos && fn.find ("..") == std::string::npos)
            {
                juce::MemoryBlock mb;
                auto f = dir.getChildFile ("pads").getChildFile (juce::String (fn));
                if (! f.loadFileAsData (mb) || mb.getSize() < 256 || mb.getSize() > (size_t) 256 * 1024 * 1024) continue;
                auto smp = std::make_shared<Sample>();
                smp->len = (int) (mb.getSize() / 4);
                smp->d.assign ((const int16_t*) mb.getData(), (const int16_t*) mb.getData() + (size_t) smp->len * 2);
                smp->file = fn;
                p.s = smp;
                p.start = clampi (p.start, 0, smp->len - 1);
                p.end = clampi (p.end, p.start + 1, smp->len);
            }
            pad[i] = p;
        }
    }
    lazyNext = bank * 16;
    if (id[0] && songId != id)
    {   /* newSong() would freeze the restored pads against the old song: load without it */
        avail = 0;
        openSong (id, title, secs, false);
    }
}

/* ------------------------------------------------------------------ the editor reads */
View Engine::view()
{
    API;
    waveCheck();
    View w;
    {
        std::lock_guard<std::mutex> lk (m);
        w.status = status;
        w.resHead = resHead;
        w.results = res;
        w.resKind = resKind;
        w.songId = songId;
        w.songTitle = songTitle;
        w.sstate = sstate;
        w.ytdlpVer = ytdlpVer;
        w.lastPlaylistId = lastPl;
        w.lastPlaylistTitle = lastPlTitle;
        w.exportDir = exportDir;
    }
    w.loading = loading;
    w.playing = playing;
    w.loop = loop;
    w.scratching = scr;
    w.ppos = ppos;
    w.cuePos = cue;
    w.angle = ang;
    w.avail = avail;
    w.total = total;
    w.viewLen = viewLen();
    w.vol = vol;
    w.padvol = padvol;
    w.dpitch = dpitch;
    w.prange = prange;
    w.klock = klock;
    w.speed = deckSpeed();
    w.chopBy = chopBy; w.assign = assign; w.slice = slice; w.bpm = bpm; w.iso = iso; w.bank = bank; w.sel = sel; w.zoom = zoom; w.learn = learn;
    w.arm = arm; w.velOn = velOn; w.sens = sens; w.mapFirst = mapFirst; w.mapLast = mapLast;
    w.pendOp = pendOp; w.pendSrc = pendSrc; w.pendBank = pendBk; w.ops = ops;
    for (int i = 0; i < NPAD; i++)
    {
        const Pad& p = pad[i];
        auto& o = w.pads[i];
        o.used = p.used; o.own = p.s != nullptr; o.rev = p.rev; o.start = p.start; o.end = p.end; o.pitch = p.pitch; o.att = p.att;
        o.dec = p.dec; o.rel = p.rel; o.grp = p.grp; o.mode = p.mode; o.iso = p.iso; o.level = p.level; o.sus = p.sus;
    }
    for (auto& vo : v)
        if (vo.on)
        {
            w.pads[vo.pad].sounding = true;
            if (vo.pad == sel) w.selPlayPos = vo.pos;
        }
    w.win0 = win0;
    w.win1 = win1;
    w.exporting = exporting;
    w.helperBusy = netBusy;
    w.hostBpm = hostBpm;
    return w;
}
} // namespace cr
