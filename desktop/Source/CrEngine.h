/* CRATE RIPPA Desktop (JJ DA PLUG): the engine. Ported from the MPC engine (engine.c, CRATE RIPPA 1.0): the same turntable,
 * scratch, key lock, chops (HITS / BEATS / LAZY), pad voices with ADSR, isolation filters, undo, LEARN, export and state.
 * Desktop changes: the host's sample rate (the song stays 44.1 kHz inside, playback converts), float output, yt-dlp runs as
 * a standalone program (no Python), the song is downloaded then decoded (ffmpeg, Media Foundation, Core Audio or JUCE),
 * scratching follows the mouse on the record, every call from the editor is serialised with the audio callback by one lock.
 *
 * Threads: audio (render), the editor (all public calls), "net" (yt-dlp: search, playlists, download), "load" (decode into
 * the song buffer), "job" (files: frozen pads, exports, history). Nothing blocks the audio thread except the short api lock. */
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cr
{
constexpr int SR = 44100;
constexpr int MAX_SEC = 900;              // 15 minutes of one song
constexpr int MAXF = SR * MAX_SEC;
constexpr int HOP = 512;
constexpr int NHOP = MAXF / HOP + 2;
constexpr int NPAD = 64;
constexpr int NVOICE = 16;
constexpr int NUNDO = 24;
constexpr int NISO = 7;
constexpr int A_MAX = 2000, D_MAX = 3000, R_MAX = 4000;
extern const char *ISO_NAMES[NISO];
extern const char *ISO_HINTS[NISO];
extern const char *SLICE_NAMES[5];

struct Sample   // a pad's own frozen audio (16-bit stereo, 44.1 kHz)
{
    std::vector<int16_t> d;
    int len = 0;
    std::string file;
};
using SampleP = std::shared_ptr<Sample>;

struct Pad
{
    bool used = false;
    int start = 0, end = 0;   // frames in the song, or in s
    SampleP s;                // null: plays from the loaded song
    int pitch = 0;            // semitones
    float level = 1.0f;
    int att = 0, dec = 300, rel = 60;   // ms
    float sus = 1.0f;
    int grp = 0;              // mute group 0 (none) .. 8
    int mode = 0;             // 0 one shot, 1 gate
    bool rev = false;
    int iso = 0;
    bool autoEnd = false;     // end follows the next chop (lazy chops)
};

struct Result
{
    std::string id, title, chan;
    int secs = 0;
    int kind = 0;             // 0 video, 1 playlist, 2 history
    std::string count;        // playlists: number of videos
};

enum { ST_IDLE, ST_RESOLVE, ST_LOAD, ST_READY, ST_ERROR };
enum { CH_HITS, CH_BEATS, CH_LAZY };

struct HostInfo
{
    bool valid = false, playing = false;
    double ppq = -1, bpm = 0;
};

/* Everything the editor shows, copied in one go (about 30 times a second). */
struct View
{
    std::string status, resHead, songId, songTitle, ytdlpVer, ops, lastPlaylistId, lastPlaylistTitle, exportDir;
    std::vector<Result> results;
    int resKind = 0, sstate = ST_IDLE;
    bool loading = false, playing = false, loop = false, scratching = false;
    double ppos = 0, cuePos = 0, angle = 0;
    int avail = 0, total = 0, viewLen = 0;
    float vol = 0.8f, padvol = 1.0f;
    int dpitch = 500, prange = 3;
    bool klock = false;
    double speed = 1.0;
    int chopBy = CH_LAZY, assign = 0, slice = 1, bpm = 90, iso = 0, bank = 0, sel = 0, zoom = 1, learn = 0;
    bool arm = true, velOn = true;
    float sens = 0.5f;
    int mapFirst = 36, mapLast = 51;
    int pendOp = 0, pendSrc = -1, pendBank = 0;
    struct PadView { bool used = false, own = false, rev = false; int start = 0, end = 0, pitch = 0, att = 0, dec = 0, rel = 0, grp = 0, mode = 0, iso = 0; float level = 1, sus = 1; bool sounding = false; };
    PadView pads[NPAD];
    double win0 = 0, win1 = 1;
    double selPlayPos = -1;   // the selected pad's voice position, -1 none
    bool exporting = false, helperBusy = false;
    float hostBpm = 0;
};

class Engine
{
public:
    explicit Engine (const juce::File& dataDir);
    ~Engine();

    void prepare (double sampleRate);
    /* audio thread */
    void render (float* outL, float* outR, int frames, const HostInfo& host);
    void midiNoteOn (int note, float vel);
    void midiNoteOff (int note);
    void midiAllOff();

    /* search and songs */
    void setQuery (const std::string& q);
    std::string query();
    void search (bool playlists);
    void showHistory();
    void tapResult (int index);
    void openPlaylist (const std::string& id, const std::string& title);
    void loadSong (const std::string& id, const std::string& title, int secs, bool autoplay);
    void setResults (const std::vector<Result>& r, const std::string& head, int kind);   // the CRATE tab shows a crate in the list

    /* transport and deck */
    void playToggle();
    void stop();
    void rew();
    void fwd();
    void seek (double frac);
    void setLoop (bool on);
    void deckCue();
    void setVol (float v);
    void setPadVol (float v);
    void setDeckPitch (float v01);
    void setRange (int r);
    void setKeyLock (bool on);
    void scratchTouch (bool down);
    void scratchMove (double turns);

    /* chops and pads */
    void setChopBy (int c);
    void setAssign (int a);
    void setArm (bool on);
    void setSens (float s);
    void setBpm (int b);
    void setSlice (int s);
    void tapChop();
    void autoChop();
    void detectBpm();
    void bpmFromHost();
    void setCue();
    void goCue();
    void undo();
    void clearPad();
    void clearBank();
    void learnToggle();
    void exportPads();
    void padHit (int idx, float vel, bool fromScreen);
    void padRelease (int idx);
    void selectPad (int idx);
    void selectBank (int b);
    void padOp (int op);     // 1 copy, 2 move, 3 swap, 4 delete
    void bankOp (int op);    // 1 copy, 2 move, 3 swap
    void stopAll();
    void setVelocity (bool on);

    /* the editor (WAVE) */
    void setZoom (int z);
    void fit();
    void padStep (int dir);
    void setStartFrac (double f);
    void setEndFrac (double f);
    void moveBounds (int which, int ms);
    void setPitch (int st);
    void setLevel (float v);
    void setMode (int m);
    void setReverse (bool on);
    void setAttack (int ms);
    void setDecay (int ms);
    void setSustain (float s);
    void setRelease (int ms);
    void groupStep (int d);
    void playSelected();
    bool bouncePadToFile (int idx, const juce::File& f);   // for dragging a chop into the DAW

    /* isolation */
    void setIso (int i);
    void isoBank();
    void isoSelected();

    /* helpers */
    void updateYtDlp();
    void installDeno();

    /* state */
    std::string getState();
    void setState (const std::string& s);

    /* the editor reads */
    View view();
    /* peaks for `cols` columns of [f0, f1) of the song (own = false) or of the selected pad's own sample; 0..1 */
    void peaks (double f0, double f1, int cols, bool selPadOwn, std::vector<float>& out);
    juce::File dataDir() const { return dir; }
    juce::File cacheFileFor (const std::string& id) const;   // a cached song, or File() if none

    /* tests reach in */
    int availFrames() const { return avail.load(); }
    int songState() { std::lock_guard<std::mutex> g (m); return sstate; }
    std::string statusText() { std::lock_guard<std::mutex> g (m); return status; }
    Pad padCopy (int i) { std::lock_guard<std::recursive_mutex> g (api); return pad[i]; }

private:
    struct Voice
    {
        bool on = false;
        int pad = 0, stage = 0;
        double pos = 0, rate = 1;
        float env = 0, vel = 1, attInc = 0, relMul = 0, decMul = 0, sus = 1;
        long age = 0;
        int attN = 1;
        bool fast = false, rev = false;
        int iso = 0, start = 0, end = 0;
        SampleP s;
        float fz[8][2] {};
    };
    struct Job { int kind = 0; std::string path; std::vector<int16_t> d; int len = 0; Pad pp; std::string text; };
    enum { J_NONE, J_SEARCH, J_PLISTS, J_OPEN, J_RESOLVE, J_UPDATE, J_DENO };
    enum { JOB_RAW, JOB_WAV, JOB_HIST, JOB_DONE, JOB_FILE };

    juce::File dir;
    double hsr = 44100.0, ratio = 1.0;
    std::recursive_mutex api;   // the editor's calls and the audio callback
    std::mutex m;               // results, status, net/load requests (like the MPC engine)

    /* song */
    std::unique_ptr<int16_t[]> buf;
    std::unique_ptr<uint8_t[]> peak;
    std::unique_ptr<float[]> odf;
    std::atomic<int> avail { 0 }, total { 0 }, nhop { 0 };
    int sstate = ST_IDLE;
    std::atomic<bool> loading { false };
    std::atomic<unsigned> gen { 0 };
    std::string songId, songTitle;
    int songSecs = 0;

    /* player and deck */
    bool playing = false, loop = false;
    double ppos = 0;
    float vol = 0.8f, padvol = 1.0f;
    int iso = 0;
    float piso[8][2] {};
    float pfade = 0;
    int cue = 0;
    int dpitch = 500, prange = 3;
    bool klock = false;
    bool scrDown = false, scr = false;
    double scrTurns = 0, anchor = 0, ang = 0;
    struct Grain { double src; double age; } gr[6] {};
    int ngr = 0;
    double hopc = 0;
    bool wasPlaying = false;
    double lastPpq = 0;
    std::atomic<bool> stopReq { false };
    float hostBpm = 0;

    /* pads */
    Pad pad[NPAD];
    Voice v[NVOICE];
    int bank = 0, sel = 0, baseNote = 36, learn = 0, mapFirst = 36, mapLast = 51;
    bool velOn = true;
    signed char padmap[160] {};
    int chopBy = CH_LAZY, assign = 0, slice = 1, bpm = 90;
    bool arm = true;
    float sens = 0.5f;
    int lazyNext = 0;
    std::deque<std::vector<Pad>> undoStack;
    int pendOp = 0, pendSrc = -1, pendBk = 0;
    std::string ops;
    int zoom = 1, winSel = -1, winZoom = -1;   /* the editor opens in PAD view */
    double win0 = 0, win1 = 1;

    /* ui */
    std::string q;
    std::vector<Result> res;
    int resKind = 0;
    std::string resHead, status, ytdlpVer = "-", lastPl, lastPlTitle, exportDir;

    /* threads */
    std::thread netTh, loadTh, jobTh;
    std::condition_variable netCv, loadCv, jobCv;
    std::atomic<bool> quit { false };
    int job = J_NONE, jobN = 0;
    std::string jobArg, jobArg2;
    std::atomic<bool> netBusy { false };
    int loadReq = 0;
    std::string loadPath;
    unsigned loadGen = 0;
    std::deque<Job> jobs;
    std::atomic<bool> exporting { false };
    std::shared_ptr<juce::ChildProcess> loadProc, netProc;   // killed when a new song comes or on quit

    /* filters, designed for the host rate (playback) and for 44.1 kHz (bounces) */
    struct Bq { float b0, b1, b2, a1, a2; };
    Bq filtRT[8] {}, filt44[8] {};

    /* internals (api lock held unless said) */
    void setStatus (const std::string& s);
    void netMain();
    void loadMain();
    void jobMain();
    void netRequest (int j, const std::string& arg, int n, const std::string& arg2 = {});
    void jobAdd (Job&& j);
    void ensureBuffers();
    void analyse (int from, int to);
    void loadFinish (unsigned g, bool ok, const std::string& why);
    void newSong (const std::string& id, const std::string& title, int secs);
    void openSong (const std::string& id, const std::string& title, int secs, bool autoplay);
    void freezePads();
    void cacheTrim();
    void voicesOff();
    void undoPush();
    void undoDrop();
    void padClear (Pad& p);
    void fixAutoEnds (int bank);
    void makeChop (int idx, int start, int end, bool autoEnd);
    bool songOk() const { return avail.load() > SR / 2; }
    void lazyChop (int hitPad);
    void autoHits();
    void autoBeats();
    void voiceStart (int idx, float vel);
    void voiceRelease (int idx);
    void stopAllInt();
    double deckSpeed() const;
    void padmapBuild();
    int noteToPad (int note);
    void doPadOp (int op, int a, int b);
    void doBankOp (int op, int a, int b);
    void selectPadInt (int idx);
    int viewLen() const;
    void waveFit();
    void waveCheck();
    void renderBlock (float* L, float* R, int n);
    void isoRun (const Bq* f, float st[8][2], int mode, float& l, float& r) const;
    std::vector<int16_t> bounce (const Pad& p, const int16_t* src, int slen);
    void resolveAndDownload (const std::string& id);
    juce::File ytdlpFile() const;
    juce::File ffmpegFile() const;
    juce::File denoFile() const;
    bool ensureYtDlp();
};

/* small shared helpers */
juce::File defaultDataDir();
std::string fmtTime (double sec, bool ms);
std::string upperStr (std::string s);
std::string safeName (const std::string& s);
} // namespace cr
