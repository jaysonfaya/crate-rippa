/* CRATE RIPPA 1.0 by JJ DA PLUG: YouTube on the MPC, on a turntable. Search, play, scratch, chop to 64 pads
 * (HITS / BEATS / LAZY), edit on a waveform (start, end, ADSR, pitch, mute groups), light isolation, export WAVs.
 * Built from TUBE RIPPER (YouTube, chops) and CRATE DIGGA (turntable, pad editor, pad actions).
 *
 * Threads (none of them ever blocks the audio thread or the host's calls):
 *   - the host's calls (set_param / get_param / render / midi) come serialised by the wrapper's lock;
 *   - "net": owns the helper process (bundled Python + yt-dlp, tube_daemon.py) for search and resolve;
 *   - "load": streams one song through the bundled ffmpeg (or reads it from the cache) into the song buffer;
 *   - "job": writes files (frozen pads, exports, history).
 * Children start with posix_spawn and a clean environment (no LD_PRELOAD), fds 3+ closed, stdio to pipes or /dev/null.
 * The song buffer is written only past `avail` by the loader; audio reads only below it. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <stdarg.h>
#include "engine.h"

#define VERSION "1.0.0"
#define SR 44100
#define MAX_SEC 600
#define MAXF (SR * MAX_SEC)
#define HOP 512
#define NHOP (MAXF / HOP + 2)
#define NPAD 64
#define NVOICE 12
#define NRES 40
#define ROWS 7
#define WCOLS 80
#define WLEV 24
#define WFR (WLEV * 4)   /* outside, inside the pad, a pad start, the playhead */
#define NEV 32
#define NPLAT 24
#define NUNDO 24
#define NISO 7
#define TXT 160

extern char **environ;

/* ------------------------------------------------------------------ small helpers */
static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static void cpy(char *d, const char *s, size_t n) {
    if (!n) return;
    size_t i = 0;
    for (; s && s[i] && i + 1 < n; i++) d[i] = s[i];
    d[i] = 0;
}
static void fmt_time(char *b, size_t n, double sec, int ms) {
    if (sec < 0) sec = 0;
    int m = (int)(sec / 60);
    double s = sec - m * 60;
    if (ms) snprintf(b, n, "%d:%06.3f", m, s);
    else snprintf(b, n, "%d:%02d", m, (int)s);
}
static void upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static void mkdirs(const char *path) {
    char t[512];
    cpy(t, path, sizeof t);
    for (char *p = t + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(t, 0755); *p = '/'; }
    mkdir(t, 0755);
}
/* a name that is safe as a folder/file name on FAT and ext4 */
static void safe_name(char *d, size_t n, const char *s) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')') d[j++] = (char)c;
        else if (c >= 0x80) continue;
        else if (j && d[j - 1] != ' ') d[j++] = ' ';
        if (j >= 48) break;
    }
    while (j && d[j - 1] == ' ') j--;
    d[j] = 0;
    if (!j) cpy(d, "Crate Rippa", n);
}

/* ------------------------------------------------------------------ child processes */
static char **clean_env(void) {
    static char *env[512];
    static int done;
    if (!done) {
        int n = 0;
        for (char **e = environ; e && *e && n < 500; e++)
            if (strncmp(*e, "LD_PRELOAD=", 11) && strncmp(*e, "PYTHONPATH=", 11) && strncmp(*e, "PYTHONHOME=", 11)) env[n++] = *e;
        env[n] = NULL;
        done = 1;
    }
    return env;
}

typedef int (*closefrom_fn)(posix_spawn_file_actions_t *, int);

static pid_t spawn(const char *path, char *const argv[], int in_fd, int out_fd) {
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    pid_t pid = -1;
    int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in_fd >= 0 ? in_fd : devnull, 0);
    posix_spawn_file_actions_adddup2(&fa, out_fd >= 0 ? out_fd : devnull, 1);
    posix_spawn_file_actions_adddup2(&fa, devnull, 2);
    /* close everything else MPC has open (audio, display, MIDI): glibc 2.34+ has closefrom_np, older ones get a list */
    closefrom_fn cf = (closefrom_fn)dlsym(RTLD_DEFAULT, "posix_spawn_file_actions_addclosefrom_np");
    if (cf) cf(&fa, 3);
    else {
        DIR *d = opendir("/proc/self/fd");
        if (d) {
            struct dirent *e;
            int dfd = dirfd(d);
            while ((e = readdir(d))) {
                int fd = atoi(e->d_name);
                if (fd > 2 && fd != dfd && e->d_name[0] != '.') posix_spawn_file_actions_addclose(&fa, fd);
            }
            closedir(d);
        }
    }
    posix_spawnattr_init(&at);
    sigset_t none, dflt;
    sigemptyset(&none);
    sigemptyset(&dflt);
    sigaddset(&dflt, SIGPIPE);
    sigaddset(&dflt, SIGCHLD);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setsigdefault(&at, &dflt);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (posix_spawn(&pid, path, &fa, &at, argv, clean_env()) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (devnull >= 0) close(devnull);
    return pid;
}

static void reap(pid_t *pid) {
    if (*pid <= 0) return;
    kill(*pid, SIGKILL);
    for (int i = 0; i < 50; i++) {   /* at most ~0.5 s; an ignored SIGCHLD makes waitpid fail at once */
        pid_t r = waitpid(*pid, NULL, WNOHANG);
        if (r == *pid || r < 0) break;
        usleep(10000);
    }
    *pid = -1;
}

/* ------------------------------------------------------------------ isolation filters */
typedef struct { float b0, b1, b2, a1, a2; } bq_t;
typedef struct { float z1, z2; } bqs_t;
enum { F_LP150, F_LP180, F_HP200, F_LP4K, F_LP100, F_HP6K, F_HP220, F_HP2K5, NF };
static bq_t FILT[NF];
static void bq_make(bq_t *f, int hp, double fc) {
    double w = 2 * M_PI * fc / SR, c = cos(w), s = sin(w), q = 0.7071, al = s / (2 * q);
    double b0 = hp ? (1 + c) / 2 : (1 - c) / 2, b1 = hp ? -(1 + c) : 1 - c, a0 = 1 + al;
    f->b0 = (float)(b0 / a0); f->b1 = (float)(b1 / a0); f->b2 = (float)(b0 / a0);
    f->a1 = (float)(-2 * c / a0); f->a2 = (float)((1 - al) / a0);
}
static inline float bq(const bq_t *f, bqs_t *s, float x) {   /* transposed direct form II */
    float y = f->b0 * x + s->z1;
    s->z1 = f->b1 * x - f->a1 * y + s->z2;
    s->z2 = f->b2 * x - f->a2 * y;
    if (fabsf(s->z1) < 1e-15f) s->z1 = 0;
    if (fabsf(s->z2) < 1e-15f) s->z2 = 0;
    return y;
}
typedef struct { bqs_t s[8]; } iso_t;
static const char *ISO_NAMES[NISO] = {"FULL MIX", "NO VOCALS", "VOCAL BAND", "BASS", "KICK + HATS", "NO BASS", "HIGHS"};
static inline void iso_run(iso_t *st, int mode, float *l, float *r) {
    float L = *l, R = *r, m = (L + R) * 0.5f, sd = (L - R) * 0.5f;
    switch (mode) {
    case 1: { float b = bq(&FILT[F_LP150], &st->s[0], m); b = bq(&FILT[F_LP150], &st->s[1], b);
              *l = b + sd * 1.4f; *r = b - sd * 1.4f; break; }
    case 2: { float v = bq(&FILT[F_HP200], &st->s[0], m); v = bq(&FILT[F_HP200], &st->s[1], v);
              v = bq(&FILT[F_LP4K], &st->s[2], v); v = bq(&FILT[F_LP4K], &st->s[3], v);
              *l = *r = v * 1.3f; break; }
    case 3: { float b = bq(&FILT[F_LP180], &st->s[0], m); b = bq(&FILT[F_LP180], &st->s[1], b);
              b = bq(&FILT[F_LP180], &st->s[2], b); *l = *r = b * 1.4f; break; }
    case 4: { float k = bq(&FILT[F_LP100], &st->s[0], m); k = bq(&FILT[F_LP100], &st->s[1], k);
              float hl = bq(&FILT[F_HP6K], &st->s[2], L), hr = bq(&FILT[F_HP6K], &st->s[3], R);
              *l = k * 1.4f + hl * 1.6f; *r = k * 1.4f + hr * 1.6f; break; }
    case 5: { float a = bq(&FILT[F_HP220], &st->s[0], L), b = bq(&FILT[F_HP220], &st->s[1], R);
              *l = bq(&FILT[F_HP220], &st->s[2], a); *r = bq(&FILT[F_HP220], &st->s[3], b); break; }
    case 6: { float a = bq(&FILT[F_HP2K5], &st->s[0], L), b = bq(&FILT[F_HP2K5], &st->s[1], R);
              *l = bq(&FILT[F_HP2K5], &st->s[2], a) * 1.3f; *r = bq(&FILT[F_HP2K5], &st->s[3], b) * 1.3f; break; }
    default: break;
    }
}

/* ------------------------------------------------------------------ data */
typedef struct { int refs; int16_t *d; int len; char file[48]; } smp_t;   /* a pad's own frozen audio */

typedef struct {
    int used;
    int start, end;      /* frames in the song, or in s */
    smp_t *s;            /* NULL: plays from the loaded song */
    int pitch;           /* semitones */
    float level;
    int att, dec, rel;   /* ms */
    float sus;           /* sustain level 0..1 */
    int grp;             /* mute group 0 (none) .. 8 */
    int mode;            /* 0 one shot, 1 gate */
    int rev, iso;
    int auto_end;        /* end follows the next chop (lazy chops) */
} pad_t;

typedef struct {
    int on, pad, stage;  /* stage 0 attack/decay/sustain, 1 release */
    double pos, rate;
    float env, vel, att_inc, rel_mul, dec_mul, sus, rel_step;
    long age;
    int att_n, fast;
    int rev, iso, start, end;
    smp_t *s;
    iso_t f;
} voice_t;

typedef struct { char id[40]; char title[100]; char chan[48]; int secs; int kind; /* 0 video, 1 playlist, 2 history */ } res_t;

enum { ST_IDLE, ST_RESOLVE, ST_LOAD, ST_READY, ST_ERROR };
enum { J_NONE, J_SEARCH, J_PLISTS, J_OPEN, J_RESOLVE, J_UPDATE };
enum { CH_HITS, CH_BEATS, CH_LAZY };

typedef struct inst {
    char dir[400];
    pthread_mutex_t m;   /* guards results, status text, net/load requests, peaks bookkeeping */
    /* ---- song */
    int16_t *buf;
    volatile int avail, total;     /* frames ready / expected (0 unknown) */
    volatile int sstate, loading;
    volatile unsigned gen;         /* bumped on every new load: an old loader stops writing */
    char song_id[40], song_title[100];
    int song_secs;
    uint8_t *peak;                 /* per HOP */
    float *odf;                    /* onset strength per HOP */
    volatile int nhop;
    /* ---- player */
    int playing, loop;
    double ppos;
    float vol;
    int iso;
    iso_t piso;
    float pfade;                   /* declick on pause/stop */
    int cue;
    /* ---- the turntable (from Crate Digga): pitch fader, range, key lock, the finger on the record */
    int dpitch, prange, klock, scr;
    int jogv, jogref;
    long long jogt;
    double anchor, ang;
    int plat, plat_rep, play_rep, pc_phase, top_rep;
    double rep_t;
    struct { double src; int age; } gr[6];
    int ngr, hopc;
    int was_playing;
    double last_ppq;
    volatile int stop_req;
    /* ---- pad actions */
    int pend_op, pend_src, pend_bk;   /* 1 copy, 2 move, 3 swap: waiting for the target pad / bank */
    char ops[TXT];
    /* ---- the editor's view: frames [win0, win1) of the song or of the pad's own sample */
    double win0, win1;
    int win_sel, win_zoom;
    unsigned win_rev;
    int top;                       /* first result row shown */
    /* ---- pads */
    pad_t pad[NPAD];
    voice_t v[NVOICE];
    int bank, sel, base_note, learn, vel_on, map_first, map_last;
    signed char padmap[160];
    int chop_by, assign, arm, slice, bpm;
    float sens, padvol;
    int lazy_next;                 /* next pad for NEXT PAD */
    pad_t undo[NUNDO][NPAD];
    int nundo;
    int zoom;
    float host_bpm;
    /* ---- ui */
    char query[64];
    int kb, find;
    res_t res[NRES];
    int nres, res_kind, res_playing;
    char res_head[80];
    char status[TXT], hint[TXT];
    double status_t;
    unsigned rev;                  /* display_rev */
    int wv[WCOLS];                 /* waveform frame per column */
    unsigned wave_rev, wave_seen;
    char ytdlp_ver[32];
    char last_pl[40];
    /* ---- net thread */
    pthread_t net_th, load_th, job_th;
    pthread_cond_t net_cv, load_cv, job_cv;
    volatile int quit;
    int job, job_n;
    char job_arg[200];
    pid_t dpid;
    int din, dout;
    char rbuf[16384];
    int rlen;
    /* ---- load thread */
    int load_req;                   /* 1: url, 2: cache file */
    char load_url[4096], load_ua[256];
    unsigned load_gen;
    pid_t fpid;
    /* ---- job thread */
    struct job { int kind; char path[560]; int16_t *d; int len; pad_t pp; struct job *next; } *jobs;
    volatile int exporting;
    char state_buf[8192];
} inst_t;

static void padmap_build(struct inst *t);
static void pad_hit(struct inst *t, int idx, float vel, int from_screen);
static void set_status(inst_t *t, const char *s) {
    pthread_mutex_lock(&t->m);
    cpy(t->status, s, sizeof t->status);
    t->status_t = now_s();
    pthread_mutex_unlock(&t->m);
}
static void bump(inst_t *t) { t->rev++; }
static void wave_dirty(inst_t *t) { t->wave_rev++; t->rev++; }

/* ------------------------------------------------------------------ samples */
static smp_t *smp_new(int16_t *d, int len) {
    smp_t *s = calloc(1, sizeof *s);
    if (!s) { free(d); return NULL; }
    s->refs = 1; s->d = d; s->len = len;
    return s;
}
static void smp_ref(smp_t *s) { if (s) s->refs++; }
static void smp_unref(smp_t *s) {
    if (s && --s->refs <= 0) { free(s->d); free(s); }
}
static void pad_clear(pad_t *p) {
    smp_unref(p->s);
    memset(p, 0, sizeof *p);
    p->level = 1.0f; p->att = 0; p->dec = 300; p->sus = 1.0f; p->rel = 60; p->mode = 0; p->grp = 0;
}
static void pads_ref_all(pad_t *ps) { for (int i = 0; i < NPAD; i++) smp_ref(ps[i].s); }
static void pads_unref_all(pad_t *ps) { for (int i = 0; i < NPAD; i++) smp_unref(ps[i].s); }

static int pad_len_src(inst_t *t, const pad_t *p) { return p->s ? p->s->len : t->avail; }

/* ------------------------------------------------------------------ undo */
static void undo_push(inst_t *t) {
    if (t->nundo == NUNDO) {   /* drop the oldest */
        pads_unref_all(t->undo[0]);
        memmove(t->undo[0], t->undo[1], sizeof(t->undo[0]) * (NUNDO - 1));
        t->nundo--;
    }
    memcpy(t->undo[t->nundo], t->pad, sizeof t->pad);
    pads_ref_all(t->undo[t->nundo]);
    t->nundo++;
}
static void voices_off(inst_t *t) { for (int i = 0; i < NVOICE; i++) { if (t->v[i].on) smp_unref(t->v[i].s); t->v[i].on = 0; t->v[i].s = NULL; } }
static void undo_pop(inst_t *t) {
    if (!t->nundo) { set_status(t, "NOTHING TO UNDO"); return; }
    voices_off(t);
    pads_unref_all(t->pad);
    t->nundo--;
    memcpy(t->pad, t->undo[t->nundo], sizeof t->pad);   /* takes over the snapshot's references */
    set_status(t, "UNDONE");
    wave_dirty(t);
}

/* ------------------------------------------------------------------ jobs (files) */
static void job_add(inst_t *t, struct job *j) {
    pthread_mutex_lock(&t->m);
    struct job **pp = &t->jobs;
    while (*pp) pp = &(*pp)->next;
    *pp = j;
    pthread_cond_signal(&t->job_cv);
    pthread_mutex_unlock(&t->m);
}

static int write_wav(const char *path, const int16_t *d, int frames) {
    char tmp[520];
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, fmt = 16, rate = SR, br = SR * 4;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    size_t ok = fwrite(d, 4, (size_t)frames, f);
    int bad = ok != (size_t)frames;
    fflush(f);
    fsync(fileno(f));
    if (fclose(f) || bad) { unlink(tmp); return -1; }
    return rename(tmp, path);
}

enum { JOB_RAW, JOB_WAV, JOB_HIST, JOB_DONE, JOB_FILE };

static inline void fetch(const int16_t *d, int len, double pos, float *l, float *r);

/* a pad as it sounds: start/end, reverse, pitch, isolation, level, a short fade at both ends */
static int16_t *bounce(const pad_t *p, const int16_t *src, int slen, int *out_frames) {
    int s0 = clampi(p->start, 0, slen), s1 = clampi(p->end, s0, slen);
    double rate = pow(2.0, p->pitch / 12.0);
    int frames = (int)((s1 - s0) / rate);
    *out_frames = frames;
    if (frames < 32) return NULL;
    int16_t *o = malloc((size_t)frames * 4);
    if (!o) return NULL;
    iso_t f;
    memset(&f, 0, sizeof f);
    for (int k = 0; k < frames; k++) {
        double pos = p->rev ? s1 - 1 - k * rate : s0 + k * rate;
        float l, r;
        fetch(src, slen, pos, &l, &r);
        iso_run(&f, p->iso, &l, &r);
        float g = p->level;
        if (k < 64) g *= k / 64.0f;
        if (frames - k < 128) g *= (frames - k) / 128.0f;
        o[2 * k] = (int16_t)clampf(l * g * 32767, -32768, 32767);
        o[2 * k + 1] = (int16_t)clampf(r * g * 32767, -32768, 32767);
    }
    return o;
}

static void *job_main(void *arg) {
    inst_t *t = arg;
    pthread_mutex_lock(&t->m);
    while (!t->quit) {
        if (!t->jobs) { pthread_cond_wait(&t->job_cv, &t->m); continue; }
        struct job *j = t->jobs;
        t->jobs = j->next;
        pthread_mutex_unlock(&t->m);
        if (j->kind == JOB_RAW) {   /* a frozen pad's audio, so a project finds it again */
            char tmp[520];
            snprintf(tmp, sizeof tmp, "%s.part", j->path);
            FILE *f = fopen(tmp, "wb");
            if (f) {
                int ok = fwrite(j->d, 4, (size_t)j->len, f) == (size_t)j->len;
                fflush(f); fsync(fileno(f));
                if (!fclose(f) && ok) rename(tmp, j->path); else unlink(tmp);
            }
            free(j->d);
        } else if (j->kind == JOB_WAV) {
            int frames = 0;
            int16_t *o = bounce(&j->pp, j->d, j->len, &frames);
            if (!o || write_wav(j->path, o, frames)) set_status(t, "EXPORT FAILED: DISK FULL?");
            free(o);
            free(j->d);
        } else if (j->kind == JOB_HIST) {
            /* path holds "id\tsecs\ttitle"; keep the newest 40, no duplicates */
            char hp[460], line[400], *old = NULL;
            snprintf(hp, sizeof hp, "%s/history.txt", t->dir);
            FILE *f = fopen(hp, "r");
            size_t cap = 64 * 1024, n = 0;
            old = calloc(1, cap);
            if (f && old) {
                int cnt = 0;
                char id[40];
                sscanf(j->path, "%39[^\t]", id);
                while (fgets(line, sizeof line, f) && cnt < 39) {
                    if (!strncmp(line, id, strlen(id)) && line[strlen(id)] == '\t') continue;
                    size_t l = strlen(line);
                    if (n + l + 1 < cap) { memcpy(old + n, line, l); n += l; cnt++; }
                }
            }
            if (f) fclose(f);
            char tmp[480];
            snprintf(tmp, sizeof tmp, "%s.part", hp);
            f = fopen(tmp, "w");
            if (f) {
                fprintf(f, "%s\n", j->path);
                if (old) fwrite(old, 1, n, f);
                if (!fclose(f)) rename(tmp, hp);
            }
            free(old);
        } else if (j->kind == JOB_FILE) {   /* a small text file, written whole */
            char tmp[600];
            snprintf(tmp, sizeof tmp, "%s.part", j->path);
            FILE *f = fopen(tmp, "w");
            if (f) { fputs((char *)j->d, f); if (!fclose(f)) rename(tmp, j->path); }
            free(j->d);
        } else if (j->kind == JOB_DONE) {
            set_status(t, j->path);
            t->exporting = 0;
        }
        free(j);
        pthread_mutex_lock(&t->m);
    }
    while (t->jobs) {   /* quitting: drop the rest */
        struct job *j = t->jobs;
        t->jobs = j->next;
        free(j->d);
        free(j);
    }
    pthread_mutex_unlock(&t->m);
    return NULL;
}

/* ------------------------------------------------------------------ helper daemon (net thread) */
static void daemon_stop(inst_t *t) {
    if (t->din >= 0) close(t->din);
    if (t->dout >= 0) close(t->dout);
    t->din = t->dout = -1;
    reap(&t->dpid);
    t->rlen = 0;
}

static int daemon_start(inst_t *t) {
    char py[520], ytd[520], scr[520];
    snprintf(py, sizeof py, "%s/bin/python3/bin/python3", t->dir);
    snprintf(ytd, sizeof ytd, "%s/bin/yt-dlp", t->dir);
    snprintf(scr, sizeof scr, "%s/bin/tube_daemon.py", t->dir);
    if (access(py, X_OK) || access(scr, R_OK)) { set_status(t, "HELPER MISSING: REINSTALL THE PLUGIN"); return -1; }
    int a[2], b[2];
    if (pipe2(a, O_CLOEXEC)) return -1;
    if (pipe2(b, O_CLOEXEC)) { close(a[0]); close(a[1]); return -1; }
    char *argv[] = {py, "-u", "-s", scr, ytd, NULL};
    t->dpid = spawn(py, argv, a[0], b[1]);
    close(a[0]); close(b[1]);
    if (t->dpid <= 0) { close(a[1]); close(b[0]); set_status(t, "CANNOT START HELPER"); return -1; }
    t->din = a[1]; t->dout = b[0];
    fcntl(t->dout, F_SETFL, O_NONBLOCK);
    t->rlen = 0;
    return 0;
}

/* one line from the helper, waiting at most `secs`; 1 ok, 0 timeout, -1 gone, -2 quitting */
static int daemon_line(inst_t *t, char *out, int n, double secs) {
    double end = now_s() + secs;
    for (;;) {
        char *nl = memchr(t->rbuf, '\n', t->rlen);
        if (nl) {
            int l = (int)(nl - t->rbuf);
            int c = l < n - 1 ? l : n - 1;
            memcpy(out, t->rbuf, c);
            out[c] = 0;
            memmove(t->rbuf, nl + 1, t->rlen - l - 1);
            t->rlen -= l + 1;
            return 1;
        }
        if (t->rlen == (int)sizeof t->rbuf) t->rlen = 0;   /* an absurd line: drop it */
        if (t->quit) return -2;
        double left = end - now_s();
        if (left <= 0) return 0;
        struct pollfd p = {t->dout, POLLIN, 0};
        int r = poll(&p, 1, left > 0.2 ? 200 : (int)(left * 1000) + 1);
        if (r < 0 && errno != EINTR) return -1;
        if (r > 0) {
            ssize_t k = read(t->dout, t->rbuf + t->rlen, sizeof t->rbuf - t->rlen);
            if (k == 0) return -1;
            if (k > 0) t->rlen += (int)k;
            else if (errno != EAGAIN && errno != EINTR) return -1;
        }
    }
}

static int daemon_send(inst_t *t, const char *line) {
    if (t->dpid <= 0 || t->din < 0) {
        if (daemon_start(t)) return -1;
        char l[256];
        int r = daemon_line(t, l, sizeof l, 25);   /* first start unpacks yt-dlp: slow on an MPC */
        if (r != 1 || strncmp(l, "READY", 5)) { daemon_stop(t); set_status(t, r == 0 ? "HELPER TOO SLOW" : "HELPER FAILED"); return -1; }
        if (strlen(l) > 6) { pthread_mutex_lock(&t->m); cpy(t->ytdlp_ver, l + 6, sizeof t->ytdlp_ver); pthread_mutex_unlock(&t->m); }
    }
    size_t n = strlen(line);
    signal(SIGPIPE, SIG_IGN);
    if (write(t->din, line, n) != (ssize_t)n) { daemon_stop(t); return -1; }
    return 0;
}

static char *field(char **p) {   /* next tab-separated field, in place */
    char *s = *p;
    if (!s) return "";
    char *tb = strchr(s, '\t');
    if (tb) { *tb = 0; *p = tb + 1; } else *p = NULL;
    return s;
}

static void start_load_url(inst_t *t, const char *id, const char *url, const char *ua, int secs, const char *title);

static void *net_main(void *arg) {
    inst_t *t = arg;
    char line[8192], cmd[400];
    pthread_mutex_lock(&t->m);
    while (!t->quit) {
        if (t->job == J_NONE) { pthread_cond_wait(&t->net_cv, &t->m); continue; }
        int job = t->job, n = t->job_n;
        char a[200];
        cpy(a, t->job_arg, sizeof a);
        t->job = J_NONE;
        pthread_mutex_unlock(&t->m);

        for (char *c = a; *c; c++) if (*c == '\t' || *c == '\n') *c = ' ';
        const char *verb = job == J_SEARCH ? "SEARCH" : job == J_PLISTS ? "PLISTS" : job == J_OPEN ? "OPEN" :
                           job == J_RESOLVE ? "RESOLVE" : "UPDATE";
        if (job == J_UPDATE) snprintf(cmd, sizeof cmd, "UPDATE\t%s/bin/yt-dlp\n", t->dir);
        else snprintf(cmd, sizeof cmd, "%s\t%s\t%d\n", verb, a, n);
        if (daemon_send(t, cmd)) { pthread_mutex_lock(&t->m); if (t->sstate == ST_RESOLVE) t->sstate = ST_ERROR; continue; }

        res_t *tmp = (job == J_RESOLVE || job == J_UPDATE) ? NULL : calloc(NRES, sizeof(res_t));
        int cnt = 0, done = 0;
        double limit = job == J_UPDATE ? 120 : 45;
        while (!done) {
            int r = daemon_line(t, line, sizeof line, limit);
            if (r == -2) break;
            if (r != 1) {
                daemon_stop(t);
                set_status(t, r == 0 ? "YOUTUBE DOES NOT ANSWER: CHECK WIFI" : "HELPER STOPPED, TRY AGAIN");
                pthread_mutex_lock(&t->m);
                if (t->sstate == ST_RESOLVE) t->sstate = ST_ERROR;
                pthread_mutex_unlock(&t->m);
                break;
            }
            char *p = line, *k = field(&p);
            if (!strcmp(k, "S") || !strcmp(k, "P")) {
                if (tmp && cnt < NRES) {
                    res_t *e = &tmp[cnt++];
                    e->kind = k[0] == 'P';
                    cpy(e->id, field(&p), sizeof e->id);
                    cpy(e->title, field(&p), sizeof e->title);
                    cpy(e->chan, field(&p), sizeof e->chan);
                    e->secs = atoi(field(&p));
                }
            } else if (!strcmp(k, "END")) {
                done = 1;
                pthread_mutex_lock(&t->m);
                memcpy(t->res, tmp, sizeof(res_t) * NRES);
                t->nres = cnt;
                t->top = 0;
                t->res_kind = job == J_PLISTS;
                if (job == J_OPEN) snprintf(t->res_head, sizeof t->res_head, "PLAYLIST: %d SONGS", cnt);
                else snprintf(t->res_head, sizeof t->res_head, "%d %s FOR \"%s\"", cnt, job == J_PLISTS ? "PLAYLISTS" : "SONGS", a);
                upper(t->res_head);
                pthread_mutex_unlock(&t->m);
                set_status(t, cnt ? (job == J_PLISTS ? "TAP A PLAYLIST TO OPEN IT" : "TAP A SONG TO PLAY IT") : "NOTHING FOUND");
                t->rev++;
            } else if (!strcmp(k, "URL")) {
                done = 1;
                char *url = field(&p), *ua = field(&p);
                int secs = atoi(field(&p));
                char *title = field(&p);
                start_load_url(t, a, url, ua, secs, title);
            } else if (!strcmp(k, "ERR")) {
                done = 1;
                char msg[TXT];
                snprintf(msg, sizeof msg, "%s", field(&p));
                upper(msg);
                set_status(t, msg);
                pthread_mutex_lock(&t->m);
                if (job == J_RESOLVE && t->sstate == ST_RESOLVE) t->sstate = ST_ERROR;
                pthread_mutex_unlock(&t->m);
                t->rev++;
            } else if (!strcmp(k, "UPD")) {
                done = 1;
                set_status(t, field(&p));
                daemon_stop(t);   /* the next request starts the new yt-dlp */
            }
        }
        free(tmp);
        pthread_mutex_lock(&t->m);
    }
    pthread_mutex_unlock(&t->m);
    if (t->din >= 0) { (void)!write(t->din, "QUIT\n", 5); }
    daemon_stop(t);
    return NULL;
}

static void net_request(inst_t *t, int job, const char *arg, int n) {
    pthread_mutex_lock(&t->m);
    t->job = job;
    t->job_n = n;
    cpy(t->job_arg, arg, sizeof t->job_arg);
    pthread_cond_signal(&t->net_cv);
    pthread_mutex_unlock(&t->m);
}

/* ------------------------------------------------------------------ loading a song */
static void cache_path(inst_t *t, const char *id, char *out, size_t n) { snprintf(out, n, "%s/cache/%s.raw", t->dir, id); }

/* analysis of frames [from, to) that just arrived: peaks and onset strength per hop */
static void analyse(inst_t *t, int from, int to) {
    int h0 = from / HOP, h1 = to / HOP;   /* only whole hops */
    for (int h = h0; h < h1 && h < NHOP; h++) {
        const int16_t *d = t->buf + (size_t)h * HOP * 2;
        int pk = 0;
        double e = 0;
        int prev = h ? (d[-2] + d[-1]) : 0;
        for (int i = 0; i < HOP; i++) {
            int l = d[2 * i], r = d[2 * i + 1];
            int a = abs(l) > abs(r) ? abs(l) : abs(r);
            if (a > pk) pk = a;
            int m = l + r, df = m - prev;
            prev = m;
            e += (double)df * df;
        }
        t->peak[h] = (uint8_t)(pk >> 7);
        t->odf[h] = (float)log1p(e / HOP / 1e4);
    }
    if (h1 > t->nhop) t->nhop = h1;
}

static void load_finish(inst_t *t, unsigned gen, int ok, const char *why) {
    pthread_mutex_lock(&t->m);
    if (gen == t->gen) {
        t->loading = 0;
        t->total = t->avail;
        t->sstate = ok ? ST_READY : (t->avail > SR ? ST_READY : ST_ERROR);
        if (ok || t->avail > SR) {
            snprintf(t->status, sizeof t->status, "%s", why ? why : "LOADED: PLAY, THEN CHOP");
        } else snprintf(t->status, sizeof t->status, "%s", why ? why : "COULD NOT LOAD THIS SONG");
        t->status_t = now_s();
    }
    pthread_mutex_unlock(&t->m);
    t->wave_rev++; t->rev++;
}

/* read from fd into the song buffer until EOF; cache_fd >= 0 gets a copy. Returns frames read. */
static int pump(inst_t *t, unsigned gen, int fd, int cache_fd, pid_t *child) {
    char tail[4];
    int ntail = 0;
    int pos = 0;
    double t0 = now_s();
    while (!t->quit && gen == t->gen && pos < MAXF) {
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, 200);
        if (r == 0) {
            if (child && now_s() - t0 > 40 && pos == 0) break;   /* nothing after 40 s */
            continue;
        }
        if (r < 0) { if (errno == EINTR) continue; break; }
        uint8_t chunk[65536];
        memcpy(chunk, tail, ntail);
        ssize_t k = read(fd, chunk + ntail, sizeof chunk - ntail);
        if (k <= 0) { if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue; break; }
        int bytes = ntail + (int)k, frames = bytes / 4;
        ntail = bytes - frames * 4;
        memcpy(tail, chunk + frames * 4, ntail);
        if (pos + frames > MAXF) frames = MAXF - pos;
        if (cache_fd >= 0 && frames) { if (write(cache_fd, chunk, (size_t)frames * 4) < 0) { close(cache_fd); cache_fd = -1; } }
        pthread_mutex_lock(&t->m);
        if (gen != t->gen) { pthread_mutex_unlock(&t->m); break; }
        memcpy(t->buf + (size_t)pos * 2, chunk, (size_t)frames * 4);
        analyse(t, pos, pos + frames);
        __atomic_store_n(&t->avail, pos + frames, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&t->m);
        pos += frames;
        if ((pos / (SR * 4)) != ((pos - frames) / (SR * 4))) t->wave_rev++, t->rev++;   /* redraw every 4 s of audio */
    }
    return pos;
}

static void *load_main(void *arg) {
    inst_t *t = arg;
    pthread_mutex_lock(&t->m);
    while (!t->quit) {
        if (!t->load_req) { pthread_cond_wait(&t->load_cv, &t->m); continue; }
        int req = t->load_req;
        unsigned gen = t->load_gen;
        char url[4096], ua[256], id[40];
        cpy(url, t->load_url, sizeof url);
        cpy(ua, t->load_ua, sizeof ua);
        cpy(id, t->song_id, sizeof id);
        t->load_req = 0;
        pthread_mutex_unlock(&t->m);
        if (gen != t->gen) { pthread_mutex_lock(&t->m); continue; }

        char cp[520], part[540];
        cache_path(t, id, cp, sizeof cp);
        snprintf(part, sizeof part, "%s.part", cp);
        if (req == 2) {   /* from the cache */
            int fd = open(cp, O_RDONLY | O_CLOEXEC);
            int got = fd >= 0 ? pump(t, gen, fd, -1, NULL) : 0;
            if (fd >= 0) close(fd);
            load_finish(t, gen, got > 0, got > 0 ? "LOADED FROM CACHE" : "CACHE BROKEN, TAP THE SONG AGAIN");
        } else {
            char ff[520];
            snprintf(ff, sizeof ff, "%s/bin/ffmpeg", t->dir);
            int pp[2];
            if (access(ff, X_OK) || pipe2(pp, O_CLOEXEC)) { load_finish(t, gen, 0, "FFMPEG MISSING: REINSTALL"); pthread_mutex_lock(&t->m); continue; }
            char tsec[16];
            snprintf(tsec, sizeof tsec, "%d", MAX_SEC);
            char *argv[40];
            int a = 0;
            argv[a++] = ff; argv[a++] = "-nostdin"; argv[a++] = "-hide_banner"; argv[a++] = "-loglevel"; argv[a++] = "error";
            argv[a++] = "-reconnect"; argv[a++] = "1"; argv[a++] = "-reconnect_streamed"; argv[a++] = "1";
            argv[a++] = "-reconnect_delay_max"; argv[a++] = "4"; argv[a++] = "-rw_timeout"; argv[a++] = "20000000";
            if (ua[0]) { argv[a++] = "-user_agent"; argv[a++] = ua; }
            argv[a++] = "-i"; argv[a++] = url; argv[a++] = "-t"; argv[a++] = tsec; argv[a++] = "-vn";
            argv[a++] = "-ac"; argv[a++] = "2"; argv[a++] = "-ar"; argv[a++] = "44100"; argv[a++] = "-f"; argv[a++] = "s16le";
            argv[a++] = "pipe:1"; argv[a] = NULL;
            pid_t pid = spawn(ff, argv, -1, pp[1]);
            close(pp[1]);
            pthread_mutex_lock(&t->m);
            t->fpid = pid;
            pthread_mutex_unlock(&t->m);
            int got = 0;
            if (pid > 0) {
                char cd[460];
                snprintf(cd, sizeof cd, "%s/cache", t->dir);
                mkdirs(cd);
                int cfd = open(part, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                got = pump(t, gen, pp[0], cfd, &pid);
                if (cfd >= 0) {
                    fsync(cfd);
                    close(cfd);
                    if (got > SR && gen == t->gen && !t->quit) rename(part, cp); else unlink(part);
                }
            }
            close(pp[0]);
            pthread_mutex_lock(&t->m);
            pid_t kp = t->fpid;
            t->fpid = -1;
            pthread_mutex_unlock(&t->m);
            reap(&kp);
            if (got > SR && gen == t->gen) {   /* remember it */
                struct job *j = calloc(1, sizeof *j);
                if (j) {
                    j->kind = JOB_HIST;
                    pthread_mutex_lock(&t->m);
                    snprintf(j->path, sizeof j->path, "%s\t%d\t%s", t->song_id, got / SR, t->song_title);
                    pthread_mutex_unlock(&t->m);
                    job_add(t, j);
                }
            }
            load_finish(t, gen, got > SR, got > SR ? (got >= MAXF ? "LOADED THE FIRST 10 MINUTES" : "LOADED: PLAY, THEN CHOP")
                                                   : "COULD NOT STREAM THIS SONG, TRY ANOTHER");
        }
        pthread_mutex_lock(&t->m);
    }
    pthread_mutex_unlock(&t->m);
    return NULL;
}

/* keep the cache small: the 8 newest songs */
static void cache_trim(inst_t *t) {
    char cd[460];
    snprintf(cd, sizeof cd, "%s/cache", t->dir);
    DIR *d = opendir(cd);
    if (!d) return;
    struct { char n[64]; time_t m; } e[64];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 64) {
        if (!strstr(de->d_name, ".raw")) continue;
        char p[540];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", cd, de->d_name);
        if (stat(p, &st)) continue;
        cpy(e[n].n, de->d_name, sizeof e[n].n);
        e[n].m = st.st_mtime;
        n++;
    }
    closedir(d);
    while (n > 8) {
        int o = 0;
        for (int i = 1; i < n; i++) if (e[i].m < e[o].m) o = i;
        char p[540];
        snprintf(p, sizeof p, "%s/%s", cd, e[o].n);
        if (strcmp(e[o].n, "") && !strstr(p, t->song_id[0] ? t->song_id : "\x01")) unlink(p);
        e[o] = e[--n];
    }
}

/* freeze every pad that plays from the current song: it gets its own copy (with 1 s of room on each side) */
static void freeze_pads(inst_t *t) {
    for (int i = 0; i < NPAD; i++) {
        pad_t *p = &t->pad[i];
        if (!p->used || p->s) continue;
        int a = clampi(p->start - SR, 0, t->avail), b = clampi(p->end + SR, 0, t->avail);
        if (b - a < 64) { pad_clear(p); continue; }
        int16_t *d = malloc((size_t)(b - a) * 4);
        if (!d) continue;
        memcpy(d, t->buf + (size_t)a * 2, (size_t)(b - a) * 4);
        smp_t *s = smp_new(d, b - a);
        if (!s) continue;
        snprintf(s->file, sizeof s->file, "%s_%d_%d.raw", t->song_id[0] ? t->song_id : "song", a, b);
        p->s = s;
        p->start -= a;
        p->end -= a;
        p->auto_end = 0;
        struct job *j = calloc(1, sizeof *j);
        if (j) {
            j->kind = JOB_RAW;
            snprintf(j->path, sizeof j->path, "%s/pads/%s", t->dir, s->file);
            char pd[460];
            snprintf(pd, sizeof pd, "%s/pads", t->dir);
            mkdirs(pd);
            j->d = malloc((size_t)s->len * 4);
            if (!j->d) { free(j); continue; }
            memcpy(j->d, s->d, (size_t)s->len * 4);
            j->len = s->len;
            job_add(t, j);
        }
    }
    /* undo snapshots that still point into the old song can't come back */
    for (int u = 0; u < t->nundo; u++) pads_unref_all(t->undo[u]);
    t->nundo = 0;
}

/* stop whatever loads now; the caller then sets up the new song (host thread) */
static void new_song(inst_t *t, const char *id, const char *title, int secs) {
    voices_off(t);
    if (t->avail > 0) freeze_pads(t);
    pthread_mutex_lock(&t->m);
    t->gen++;
    if (t->fpid > 0) kill(t->fpid, SIGKILL);   /* the loader reaps it */
    t->avail = 0;
    t->total = secs > 0 ? secs * SR : 0;
    t->nhop = 0;
    t->loading = 1;
    cpy(t->song_id, id, sizeof t->song_id);
    cpy(t->song_title, title, sizeof t->song_title);
    t->song_secs = secs;
    pthread_mutex_unlock(&t->m);
    t->playing = 0;
    t->ppos = 0;
    t->cue = 0;
    t->lazy_next = t->bank * 16;
    memset(&t->piso, 0, sizeof t->piso);
    wave_dirty(t);
}

static void start_load_url(inst_t *t, const char *id, const char *url, const char *ua, int secs, const char *title) {
    pthread_mutex_lock(&t->m);
    if (t->sstate != ST_RESOLVE || strcmp(id, t->song_id)) { pthread_mutex_unlock(&t->m); return; }   /* another song meanwhile */
    t->sstate = ST_LOAD;
    if (secs > 0) { t->total = secs * SR; t->song_secs = secs; }
    if (title && title[0]) cpy(t->song_title, title, sizeof t->song_title);
    cpy(t->load_url, url, sizeof t->load_url);
    cpy(t->load_ua, ua, sizeof t->load_ua);
    t->load_gen = t->gen;
    t->load_req = 1;
    cpy(t->status, "LOADING...", sizeof t->status);
    t->status_t = now_s();
    pthread_cond_signal(&t->load_cv);
    pthread_mutex_unlock(&t->m);
    t->rev++;
}

/* play a song by id: from the cache if it's there, else ask YouTube */
static void open_song(inst_t *t, const char *id, const char *title, int secs, int autoplay) {
    if (!id[0]) return;
    new_song(t, id, title, secs);
    cache_trim(t);
    char cp[520];
    cache_path(t, id, cp, sizeof cp);
    struct stat st;
    pthread_mutex_lock(&t->m);
    if (!stat(cp, &st) && st.st_size > SR * 4) {
        t->sstate = ST_LOAD;
        t->total = (int)(st.st_size / 4);
        t->load_gen = t->gen;
        t->load_req = 2;
        utime(cp, NULL);
        pthread_cond_signal(&t->load_cv);
        pthread_mutex_unlock(&t->m);
        set_status(t, "LOADING FROM CACHE...");
    } else {
        t->sstate = ST_RESOLVE;
        pthread_mutex_unlock(&t->m);
        set_status(t, "ASKING YOUTUBE...");
        net_request(t, J_RESOLVE, id, 1);
    }
    t->playing = autoplay;
    t->pfade = 0;
    bump(t);
}

/* ------------------------------------------------------------------ chopping */
static void fix_auto_ends(inst_t *t, int bank) {
    for (int i = bank * 16; i < bank * 16 + 16; i++) {
        pad_t *p = &t->pad[i];
        if (!p->used || p->s || !p->auto_end) continue;
        int nxt = -1;
        for (int j = bank * 16; j < bank * 16 + 16; j++) {
            pad_t *q = &t->pad[j];
            if (j == i || !q->used || q->s) continue;
            if (q->start > p->start && (nxt < 0 || q->start < nxt)) nxt = q->start;
        }
        int lim = t->loading ? (t->total ? t->total : t->avail) : t->avail;
        int e = nxt > 0 ? nxt : p->start + 8 * SR;
        if (e > lim && lim > p->start) e = lim;
        if (e - p->start > 16 * SR) e = p->start + 16 * SR;
        p->end = e;
    }
}

static void make_chop(inst_t *t, int idx, int start, int end, int auto_end) {
    pad_t *p = &t->pad[idx];
    int keep_pitch = p->used ? p->pitch : 0;
    float keep_lvl = p->used ? p->level : 1.0f;
    int keep_mode = p->used ? p->mode : 0;
    pad_clear(p);
    p->used = 1;
    p->start = start < 0 ? 0 : start;
    p->end = end;
    p->auto_end = auto_end;
    p->pitch = keep_pitch;
    p->level = keep_lvl;
    p->mode = keep_mode;
    p->iso = t->iso;
}

static int song_ok(inst_t *t) { return t->avail > SR / 2; }

static void lazy_chop(inst_t *t, int hit_pad) {
    if (!song_ok(t)) { set_status(t, "LOAD A SONG FIRST"); return; }
    int pos = (int)t->ppos;
    int idx;
    if (t->assign == 1 && hit_pad >= 0) idx = hit_pad;
    else {
        idx = t->lazy_next;
        if (idx / 16 != t->bank) idx = t->bank * 16;
    }
    undo_push(t);
    /* a hit within 15 ms after the playhead is a late press: snap back to the nearest hop start with energy */
    make_chop(t, idx, pos - 441, pos + 8 * SR, 1);
    fix_auto_ends(t, idx / 16);
    t->sel = idx;
    if (t->assign == 0) {
        int nx = idx + 1;
        if (nx >= t->bank * 16 + 16) nx = t->bank * 16;
        t->lazy_next = nx;
    }
    char b[TXT], tm[24];
    fmt_time(tm, sizeof tm, pos / (double)SR, 1);
    snprintf(b, sizeof b, "CHOP %c%d AT %s", 'A' + idx / 16, idx % 16 + 1, tm);
    set_status(t, b);
    wave_dirty(t);
}

static void auto_hits(inst_t *t) {
    int nh = t->nhop;
    if (nh < 16) { set_status(t, "LOAD A SONG FIRST"); return; }
    /* onset strength: rise of the high-pass energy over the last 3 hops */
    float *on = calloc(nh, sizeof(float));
    if (!on) return;
    double sum = 0, sq = 0;
    for (int h = 3; h < nh; h++) {
        float m = (t->odf[h - 1] + t->odf[h - 2] + t->odf[h - 3]) / 3;
        float v = t->odf[h] - m;
        on[h] = v > 0 ? v : 0;
        sum += on[h]; sq += (double)on[h] * on[h];
    }
    double mean = sum / nh, sd = sqrt(fmax(0, sq / nh - mean * mean));
    double thr = mean + (2.6 - 2.2 * t->sens) * sd;
    int gap = (int)(0.14 * SR / HOP) + 1;
    int h = t->cue / HOP, got = 0, last = -gap;
    int base = t->bank * 16;
    undo_push(t);
    int starts[16];
    for (; h < nh - 1 && got < 16; h++) {
        if (on[h] < thr || on[h] < on[h - 1] || on[h] < on[h + 1] || h - last < gap) continue;
        starts[got++] = h * HOP - HOP / 2;
        last = h;
    }
    free(on);
    if (!got) { set_status(t, "NO HITS FOUND: RAISE SENSITIVITY"); t->nundo--; pads_unref_all(t->undo[t->nundo]); return; }
    for (int i = 0; i < got; i++) {
        int e = i + 1 < got ? starts[i + 1] : starts[i] + 2 * SR;
        if (e - starts[i] > 4 * SR) e = starts[i] + 4 * SR;   /* a hit is short: at most 4 s */
        make_chop(t, base + i, starts[i], e, 0);
    }
    t->sel = base;
    char b[TXT];
    snprintf(b, sizeof b, "%d HITS ON BANK %c (FROM THE CUE)", got, 'A' + t->bank);
    set_status(t, b);
    wave_dirty(t);
}

static const float SLICE_BEATS[5] = {0.5f, 1, 2, 4, 8};
static const char *SLICE_NAMES[5] = {"1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS"};

static void auto_beats(inst_t *t) {
    if (!song_ok(t)) { set_status(t, "LOAD A SONG FIRST"); return; }
    double len = SLICE_BEATS[t->slice] * 60.0 / clampi(t->bpm, 40, 250) * SR;
    int base = t->bank * 16, got = 0;
    int lim = t->loading && t->total ? t->total : t->avail;
    undo_push(t);
    for (int i = 0; i < 16; i++) {
        double a = t->cue + i * len;
        if (a + 64 > lim) break;
        double b = a + len;
        if (b > lim) b = lim;
        make_chop(t, base + i, (int)a, (int)b, 0);
        got++;
    }
    t->sel = base;
    char b[TXT];
    snprintf(b, sizeof b, "%d SLICES OF %s AT %d BPM", got, SLICE_NAMES[t->slice], t->bpm);
    set_status(t, b);
    wave_dirty(t);
}

static void detect_bpm(inst_t *t) {
    int nh = t->nhop;
    if (nh < 400) { set_status(t, "NEED MORE OF THE SONG FIRST"); return; }
    int h0 = t->cue / HOP, n = nh - h0;
    if (n > 2600) n = 2600;   /* ~30 s */
    if (n < 400) { h0 = nh - 400 > 0 ? nh - 400 : 0; n = nh - h0; }
    float *on = calloc(n, sizeof(float));
    if (!on) return;
    for (int i = 3; i < n; i++) {
        int h = h0 + i;
        float v = t->odf[h] - (t->odf[h - 1] + t->odf[h - 2] + t->odf[h - 3]) / 3;
        on[i] = v > 0 ? v : 0;
    }
    double best = -1;
    int bl = 0;
    double hz = (double)SR / HOP;
    int lmin = (int)(hz * 60 / 180), lmax = (int)(hz * 60 / 70) + 1;
    for (int l = lmin; l <= lmax; l++) {
        double s = 0;
        for (int i = 0; i + l < n; i++) s += on[i] * on[i + l];
        s /= (n - l);
        /* prefer the tempo a musician would count: a light bias toward 85-130 BPM */
        double bpm = hz * 60 / l, w = 1.0 - 0.15 * fabs(log2(bpm / 105.0));
        if (s * w > best) { best = s * w; bl = l; }
    }
    free(on);
    if (!bl) return;
    double bpm = hz * 60 / bl;
    while (bpm > 170) bpm /= 2;
    while (bpm < 70) bpm *= 2;
    t->bpm = (int)lround(bpm);
    char b[TXT];
    snprintf(b, sizeof b, "TEMPO ABOUT %d BPM (CHECK IT BY EAR)", t->bpm);
    set_status(t, b);
    bump(t);
}

/* ------------------------------------------------------------------ voices */
static void voice_start(inst_t *t, int idx, float vel) {
    pad_t *p = &t->pad[idx];
    if (!p->used) return;
    int len = pad_len_src(t, p);
    if (p->start >= len - 2) return;
    if (p->grp)   /* mute group: the others in the group let go quickly */
        for (int i = 0; i < NVOICE; i++)
            if (t->v[i].on && t->v[i].pad != idx && t->pad[t->v[i].pad].grp == p->grp) { t->v[i].stage = 1; t->v[i].fast = 1; }
    voice_t *v = NULL;
    for (int i = 0; i < NVOICE; i++) if (t->v[i].on && t->v[i].pad == idx) { v = &t->v[i]; break; }   /* retrigger: same voice */
    if (!v) for (int i = 0; i < NVOICE; i++) if (!t->v[i].on) { v = &t->v[i]; break; }
    if (!v) {   /* steal the quietest */
        v = &t->v[0];
        for (int i = 1; i < NVOICE; i++) if (t->v[i].env < v->env) v = &t->v[i];
    }
    if (v->on) smp_unref(v->s);
    memset(v, 0, sizeof *v);
    v->on = 1;
    v->pad = idx;
    v->s = p->s;
    smp_ref(v->s);
    v->start = p->start;
    v->end = p->end > p->start ? p->end : len;
    v->rev = p->rev;
    v->iso = p->iso;
    v->rate = pow(2.0, p->pitch / 12.0);
    v->pos = v->rev ? v->end - 1 : v->start;
    v->vel = (t->vel_on ? (0.25f + 0.75f * vel * vel) : 1.0f) * p->level;
    float att = p->att < 2 ? 2 : (float)p->att;
    v->att_n = (int)(att * SR / 1000);
    v->att_inc = 1.0f / v->att_n;
    v->env = 0;
    v->sus = p->sus;
    float dec = p->dec < 5 ? 5 : (float)p->dec;
    v->dec_mul = expf(-5.0f / (dec * 0.001f * SR));   /* toward the sustain level, ~99 % of the way in `dec` ms */
    float rel = p->rel < 5 ? 5 : (float)p->rel;
    v->rel_mul = expf(-6.9f / (rel * 0.001f * SR));   /* -60 dB over `rel` ms */
}
static void voice_release(inst_t *t, int idx) {
    for (int i = 0; i < NVOICE; i++)
        if (t->v[i].on && t->v[i].pad == idx && t->pad[idx].mode == 1) t->v[i].stage = 1;
}
static void stop_all(inst_t *t) {   /* every pad ends within ~10 ms, the song fades out */
    for (int i = 0; i < NVOICE; i++) if (t->v[i].on) { t->v[i].stage = 1; t->v[i].fast = 1; }
    t->playing = 0;
}

/* ------------------------------------------------------------------ the turntable */
static long long now_ms(void) { struct timespec q; clock_gettime(CLOCK_MONOTONIC, &q); return (long long)q.tv_sec * 1000 + q.tv_nsec / 1000000; }
static const int RANGE_PCT[3] = {8, 16, 50};
static double deck_speed(inst_t *t) {
    if (t->prange == 3) return pow(2.0, (t->dpitch - 500) / 500.0);   /* +-12 semitones */
    return 1.0 + (t->dpitch - 500) / 500.0 * RANGE_PCT[t->prange] / 100.0;
}

/* ------------------------------------------------------------------ engine API */
static void *create(const char *data_dir) {
    static int filt_done;
    if (!filt_done) {
        bq_make(&FILT[F_LP150], 0, 150); bq_make(&FILT[F_LP180], 0, 180); bq_make(&FILT[F_HP200], 1, 200);
        bq_make(&FILT[F_LP4K], 0, 4000); bq_make(&FILT[F_LP100], 0, 100); bq_make(&FILT[F_HP6K], 1, 6000);
        bq_make(&FILT[F_HP220], 1, 220); bq_make(&FILT[F_HP2K5], 1, 2500);
        filt_done = 1;
    }
    inst_t *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    cpy(t->dir, data_dir && data_dir[0] ? data_dir : "/tmp/craterippa", sizeof t->dir);
    t->buf = malloc((size_t)MAXF * 4);   /* virtual until written */
    t->peak = calloc(NHOP, 1);
    t->odf = calloc(NHOP, sizeof(float));
    if (!t->buf || !t->peak || !t->odf) { free(t->buf); free(t->peak); free(t->odf); free(t); return NULL; }
    pthread_mutex_init(&t->m, NULL);
    pthread_cond_init(&t->net_cv, NULL);
    pthread_cond_init(&t->load_cv, NULL);
    pthread_cond_init(&t->job_cv, NULL);
    t->dpid = t->fpid = -1;
    t->din = t->dout = -1;
    for (int i = 0; i < NPAD; i++) pad_clear(&t->pad[i]);
    t->vol = 0.8f; t->padvol = 1.0f; t->bpm = 90; t->slice = 1; t->sens = 0.5f; t->chop_by = CH_LAZY;
    t->base_note = 36; t->vel_on = 1; t->arm = 1;
    t->dpitch = 500; t->prange = 3; t->jogv = t->jogref = 500; t->play_rep = -1; t->plat_rep = -1; t->pend_src = -1;
    t->map_first = 36; t->map_last = 51;
    {   /* the pad map learned last time, for every new instance */
        char mp[460];
        snprintf(mp, sizeof mp, "%s/padmap.txt", t->dir);
        FILE *f = fopen(mp, "r");
        int a = 0, b = 0;
        if (f) { if (fscanf(f, "%d %d", &a, &b) == 2 && a >= 0 && b > a && b < 128) { t->map_first = a; t->map_last = b; } fclose(f); }
        padmap_build(t);
    }
    cpy(t->status, "TAP THE SEARCH FIELD AND TYPE A SONG", sizeof t->status);
    cpy(t->res_head, "SEARCH YOUTUBE", sizeof t->res_head);
    cpy(t->ytdlp_ver, "-", sizeof t->ytdlp_ver);
    mkdirs(t->dir);
    /* a copy over a FAT stick can lose the execute bits: put them back */
    const char *exe[] = {"bin/ffmpeg", "bin/python3/bin/python3", "bin/python3/bin/python3.11", NULL};
    for (int i = 0; exe[i]; i++) {
        char p[520];
        snprintf(p, sizeof p, "%s/%s", t->dir, exe[i]);
        struct stat st;
        if (!lstat(p, &st) && S_ISREG(st.st_mode) && !(st.st_mode & S_IXUSR)) chmod(p, 0755);
    }
    pthread_create(&t->net_th, NULL, net_main, t);
    pthread_create(&t->load_th, NULL, load_main, t);
    pthread_create(&t->job_th, NULL, job_main, t);
    return t;
}

static void destroy(void *vp) {
    inst_t *t = vp;
    if (!t) return;
    pthread_mutex_lock(&t->m);
    t->quit = 1;
    t->gen++;
    if (t->fpid > 0) kill(t->fpid, SIGKILL);
    if (t->dpid > 0) kill(t->dpid, SIGKILL);
    pthread_cond_broadcast(&t->net_cv);
    pthread_cond_broadcast(&t->load_cv);
    pthread_cond_broadcast(&t->job_cv);
    pthread_mutex_unlock(&t->m);
    pthread_join(t->net_th, NULL);
    pthread_join(t->load_th, NULL);
    pthread_join(t->job_th, NULL);
    voices_off(t);
    pads_unref_all(t->pad);
    for (int u = 0; u < t->nundo; u++) pads_unref_all(t->undo[u]);
    pthread_mutex_destroy(&t->m);
    free(t->buf); free(t->peak); free(t->odf);
    free(t);
}

/* The MPC pads send whatever its Pad Perform settings say: chromatic from any note, a scale in any key, any pad bank. LEARN asks
 * for the first and the last pad (pad 1 is the root in the MPC's scale modes). The span between them tells which scales fit
 * (16 notes of a 7-note scale span 25-28 semitones, of a pentatonic 36, chromatic 15); every note those scales use gets the
 * pad it has in them (a majority vote where two scales differ). Notes past pad 16 (another MPC pad bank) continue the scale and
 * play the bank on screen. Checked for 22 of 24 common scales in tools-free tests; a note no scale knows is placed evenly. */
static const signed char SCALES[][13] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1}, {0, 2, 4, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 8, 10, -1}, {0, 2, 3, 5, 7, 8, 11, -1},
    {0, 2, 3, 5, 7, 9, 11, -1}, {0, 2, 3, 5, 7, 9, 10, -1}, {0, 1, 3, 5, 7, 8, 10, -1}, {0, 2, 4, 6, 7, 9, 11, -1},
    {0, 2, 4, 5, 7, 9, 10, -1}, {0, 1, 3, 5, 6, 8, 10, -1}, {0, 2, 4, 7, 9, -1}, {0, 3, 5, 7, 10, -1}, {0, 3, 5, 6, 7, 10, -1},
    {0, 2, 3, 4, 7, 9, -1}, {0, 2, 4, 6, 8, 10, -1}, {0, 2, 3, 5, 6, 8, 9, 11, -1}, {0, 1, 3, 4, 6, 7, 9, 10, -1},
    {0, 1, 4, 5, 7, 8, 10, -1}, {0, 2, 3, 6, 7, 8, 11, -1}, {0, 1, 5, 7, 10, -1}, {0, 2, 3, 7, 8, -1}, {0, 2, 5, 7, 10, -1},
    {0, 1, 4, 5, 7, 8, 11, -1}, {0, 2, 4, 5, 7, 9, 10, 11, -1}};
#define NSCALE (int)(sizeof SCALES / sizeof SCALES[0])
static int scale_note(int s, int k) {   /* semitones above the root of note k (0..) of scale s */
    int n = 0;
    while (SCALES[s][n] >= 0) n++;
    return SCALES[s][k % n] + 12 * (k / n);
}
static void padmap_build(inst_t *t) {
    int span = t->map_last - t->map_first;
    static short votes[160][64];
    memset(votes, 0, sizeof votes);
    int any = 0;
    for (int s = 0; s < NSCALE; s++) {
        if (scale_note(s, 15) != span) continue;
        any = 1;
        for (int k = 0; k < 64; k++) { int o = scale_note(s, k); if (o < 160) votes[o][k]++; }
    }
    for (int o = 0; o < 160; o++) {
        int best = -1, bv = 0;
        for (int k = 0; k < 64; k++) if (votes[o][k] > bv) { bv = votes[o][k]; best = k; }
        if (!any || best < 0) {   /* no known scale: evenly spread */
            double sp = span > 0 ? span : 15, period = sp + sp / 15.0, r = fmod(o, period);
            best = (int)lround(r * 15.0 / sp);
        }
        t->padmap[o] = (signed char)(best % 16);
    }
}
static int note_to_pad(inst_t *t, int note) {
    int o = note - t->map_first;
    if (o < 0) o = ((o % 12) + 12) % 12;   /* below pad 1: play its neighbours rather than nothing */
    if (o >= 160) o = 159;
    return t->bank * 16 + t->padmap[o];
}

static void midi(void *vp, const uint8_t *m, int len) {
    inst_t *t = vp;
    if (len < 3) return;
    int st = m[0] & 0xF0, note = m[1], vel = m[2];
    if (st == 0x90 && vel > 0) {
        if (t->learn == 1) {   /* first pad */
            t->base_note = note;
            t->learn = 2;
            char b[TXT];
            snprintf(b, sizeof b, "FIRST PAD = NOTE %d. NOW HIT THE LAST PAD (PAD 16)", note);
            set_status(t, b);
            bump(t);
            return;
        }
        if (t->learn == 2) {   /* last pad */
            char b[TXT];
            if (note <= t->base_note) {
                snprintf(b, sizeof b, "THE LAST PAD MUST BE HIGHER THAN THE FIRST (NOTE %d). HIT PAD 16", t->base_note);
                set_status(t, b);
                return;
            }
            t->map_first = t->base_note;
            t->map_last = note;
            t->learn = 0;
            padmap_build(t);
            snprintf(b, sizeof b, "PADS LEARNED: NOTES %d TO %d ON ALL 16 PADS", t->map_first, t->map_last);
            set_status(t, b);
            struct job *j = calloc(1, sizeof *j);
            if (j) {
                j->kind = JOB_FILE;
                snprintf(j->path, sizeof j->path, "%s/padmap.txt", t->dir);
                j->d = malloc(32);
                if (j->d) { snprintf((char *)j->d, 32, "%d %d\n", t->map_first, t->map_last); job_add(t, j); } else free(j);
            }
            bump(t);
            return;
        }
        pad_hit(t, note_to_pad(t, note), vel / 127.0f, 0);
    } else if (st == 0x80 || (st == 0x90 && vel == 0)) {
        voice_release(t, note_to_pad(t, note));
    } else if (st == 0xB0 && (note == 120 || note == 123)) {
        for (int i = 0; i < NVOICE; i++) t->v[i].stage = 1;
    }
}

static inline void fetch(const int16_t *d, int len, double pos, float *l, float *r) {
    int i = (int)pos;
    if (i < 0 || i >= len) { *l = *r = 0; return; }
    float f = (float)(pos - i);
    int j = i + 1 < len ? i + 1 : i;
    *l = (d[2 * i] + (d[2 * j] - d[2 * i]) * f) * (1.0f / 32768.0f);
    *r = (d[2 * i + 1] + (d[2 * j + 1] - d[2 * i + 1]) * f) * (1.0f / 32768.0f);
}

static void render(void *vp, int16_t *out, int frames) {
    inst_t *t = vp;
    int avail = __atomic_load_n(&t->avail, __ATOMIC_ACQUIRE);
    float pl[2 * 128];
    if (frames > 128) frames = 128;
    memset(pl, 0, sizeof pl);
    if (__atomic_exchange_n(&t->stop_req, 0, __ATOMIC_ACQ_REL)) stop_all(t);
    /* ---- the player on the turntable: plays, pitches (or stretches with key lock), scratches under the finger */
    int scr = avail > 0 && now_ms() - t->jogt < 250;
    if (scr && !t->scr) t->anchor = t->ppos;
    t->scr = scr;
    float want = (t->playing || scr) ? 1.0f : 0.0f;
    if (avail > 0 && (want > 0 || t->pfade > 0.0005f)) {
        double pos = t->ppos, p0 = pos, speed = deck_speed(t), rate = speed;
        if (scr) {
            double target = t->anchor + (t->jogv - t->jogref) / 1000.0 * 88200.0;
            if (target < 0) target = 0;
            if (target > avail - 2) target = avail - 2;
            rate = (target - pos) / (3.0 * frames);
            if (rate > 8) rate = 8;
            if (rate < -8) rate = -8;
        }
        int gran = t->klock && !scr && fabs(speed - 1.0) > 0.0005;
        if (!gran) { t->ngr = 0; t->hopc = 0; }
        enum { GL = 1024, HS = 256 };
        static float win[GL];
        static int win_ok;
        if (!win_ok) { for (int k = 0; k < GL; k++) win[k] = 0.5f - 0.5f * cosf(6.2831853f * k / GL); win_ok = 1; }
        for (int i = 0; i < frames; i++) {
            t->pfade += (want - t->pfade) * 0.004f;
            if (pos >= avail - 2 && rate >= 0) {
                if (t->loading) continue;   /* still downloading: wait for the data */
                if (t->loop && !scr) { pos = t->cue < avail ? t->cue : 0; t->ngr = 0; }
                else { if (t->playing) { t->playing = 0; t->rev++; } want = 0; pos = avail - 2 > 0 ? avail - 2 : 0; continue; }
            }
            float l = 0, r = 0;
            if (gran) {   /* time stretch: grains read at normal pitch, the grain start moves with the tempo */
                if (t->hopc <= 0) {
                    if (t->ngr < 6) { t->gr[t->ngr].src = pos; t->gr[t->ngr].age = 0; t->ngr++; }
                    pos += HS * speed;
                    t->hopc += HS;
                }
                t->hopc--;
                for (int k = 0; k < t->ngr; k++) {
                    long ix = (long)t->gr[k].src + t->gr[k].age;
                    if (ix >= 0 && ix < avail) { float w = win[t->gr[k].age]; l += w * t->buf[2 * ix]; r += w * t->buf[2 * ix + 1]; }
                    t->gr[k].age++;
                }
                for (int k = 0; k < t->ngr;) { if (t->gr[k].age >= GL) t->gr[k] = t->gr[--t->ngr]; else k++; }
                l *= 0.5f / 32768.0f; r *= 0.5f / 32768.0f;
            } else {
                fetch(t->buf, avail, pos, &l, &r);
                pos += rate;
                if (pos < 0) pos = 0;
            }
            iso_run(&t->piso, t->iso, &l, &r);
            pl[2 * i] = l * t->vol * t->pfade;
            pl[2 * i + 1] = r * t->vol * t->pfade;
        }
        double moved = gran ? frames * speed : pos - p0;
        if (t->playing || scr) t->ang += moved / SR * (100.0 / 3.0 / 60.0);   /* 33 1/3 turns a minute at normal speed */
        t->ppos = pos;
    }
    double fa = t->ang - floor(t->ang);
    t->plat = (int)(fa * NPLAT) % NPLAT;
    for (int n = 0; n < frames; n++) {
        float L = pl[2 * n], R = pl[2 * n + 1];
        /* pads */
        for (int k = 0; k < NVOICE; k++) {
            voice_t *v = &t->v[k];
            if (!v->on) continue;
            const int16_t *d = v->s ? v->s->d : t->buf;
            int len = v->s ? v->s->len : avail;
            float l, r;
            int done = v->rev ? (v->pos < v->start) : (v->pos >= v->end || (int)v->pos >= len - 1);
            if (done) { v->on = 0; smp_unref(v->s); v->s = NULL; continue; }
            fetch(d, len, v->pos, &l, &r);
            if (v->stage == 0) {
                if (v->age < v->att_n) v->env = (v->age + 1) * v->att_inc;   /* attack */
                else v->env = v->sus + (v->env - v->sus) * v->dec_mul;     /* decay to sustain */
            } else {
                v->env *= v->fast ? 0.9985f : v->rel_mul;
                if (v->env < 0.0005f) { v->on = 0; smp_unref(v->s); v->s = NULL; continue; }
            }
            v->age++;
            /* a short fade at the end of the region against clicks */
            double left = v->rev ? v->pos - v->start : v->end - v->pos;
            float tail = left < 96 ? (float)(left / 96.0) : 1.0f;
            iso_run(&v->f, v->iso, &l, &r);
            float g = v->env * v->vel * tail * t->padvol;
            L += l * g; R += r * g;
            v->pos += v->rev ? -v->rate : v->rate;
        }
        /* soft clip */
        L = L > 1.5f ? 1.0f : L < -1.5f ? -1.0f : L - L * L * L * (4.0f / 27.0f);
        R = R > 1.5f ? 1.0f : R < -1.5f ? -1.0f : R - R * R * R * (4.0f / 27.0f);
        out[2 * n] = (int16_t)clampf(L * 32767.0f, -32768, 32767);
        out[2 * n + 1] = (int16_t)clampf(R * 32767.0f, -32768, 32767);
    }
}

/* the host's transport: MPC STOP ends every sound, as it does for the rest of the project */
static void song_pos(void *vp, double ppq, double bpm, int playing, int frames) {
    (void)bpm; (void)frames;
    inst_t *t = vp;
    if ((t->was_playing && !playing) || (!playing && ppq >= 0 && ppq + 0.001 < t->last_ppq)) stop_all(t);
    t->last_ppq = ppq;
    t->was_playing = playing;
}

/* ------------------------------------------------------------------ waveform + envelope pictures */
/* the editor's window: the whole song (SONG view), or the selected pad with room around it (PAD view). It only moves when the
 * pad, the view or FIT changes, or an end leaves it: so a start/end slider never runs away under the finger. */
static int view_len(inst_t *t) { return t->loading && t->total ? t->total : t->avail; }
static void wave_fit(inst_t *t) {
    pad_t *sp = &t->pad[t->sel];
    t->win_sel = t->sel; t->win_zoom = t->zoom;
    if (t->zoom == 1 && sp->used) {
        double l = sp->end - sp->start, m = l * 0.35 + SR / 4;
        double lim = sp->s ? sp->s->len : view_len(t);
        t->win0 = sp->start - m; t->win1 = sp->end + m;
        if (t->win0 < 0) t->win0 = 0;
        if (t->win1 > lim) t->win1 = lim;
    } else { t->win0 = 0; t->win1 = view_len(t); }
    if (t->win1 < t->win0 + WCOLS) t->win1 = t->win0 + WCOLS;
    t->wave_rev++;
}
static void wave_check(inst_t *t) {   /* the window follows changes that make it wrong */
    pad_t *sp = &t->pad[t->sel];
    if (t->win_sel != t->sel || t->win_zoom != t->zoom) { wave_fit(t); return; }
    if (t->zoom == 0) { double l = view_len(t); if (fabs(t->win1 - l) > 1) { t->win1 = l > WCOLS ? l : WCOLS; t->wave_rev++; } return; }
    if (sp->used && (sp->start < t->win0 || sp->end > t->win1)) wave_fit(t);
}
static void wave_build(inst_t *t) {
    wave_check(t);
    if (t->wave_seen == t->wave_rev) return;
    t->wave_seen = t->wave_rev;
    pad_t *sp = &t->pad[t->sel];
    int pad_view = t->zoom == 1 && sp->used;
    double a = t->win0, b = t->win1;
    const int16_t *d = pad_view && sp->s ? sp->s->d : t->buf;
    int dlen = pad_view && sp->s ? sp->s->len : t->avail;
    for (int c = 0; c < WCOLS; c++) {
        int lev = 0, state = 0;
        int f0 = (int)(a + (b - a) * c / WCOLS), f1 = (int)(a + (b - a) * (c + 1) / WCOLS);
        if (f1 <= f0) f1 = f0 + 1;
        int pk = 0;
        if (d != t->buf || f1 - f0 < HOP * 2) {
            int step = (f1 - f0) / 48 + 1;
            for (int f = f0; f < f1 && f < dlen; f += step) {
                int x = abs(d[2 * f]), y = abs(d[2 * f + 1]);
                if (x > pk) pk = x;
                if (y > pk) pk = y;
            }
            pk >>= 7;
        } else
            for (int h = f0 / HOP; h <= f1 / HOP && h < t->nhop; h++) if (t->peak[h] > pk) pk = t->peak[h];
        if (pk > 0) lev = 1 + (int)(sqrtf(pk / 255.0f) * (WLEV - 2) + 0.5f);
        if (lev > WLEV - 1) lev = WLEV - 1;
        if (pad_view) {
            if ((sp->start >= f0 && sp->start < f1) || (sp->end >= f0 && sp->end < f1)) state = 2;
            else if (f1 > sp->start && f0 < sp->end) state = 1;
        } else {
            for (int i = t->bank * 16; i < t->bank * 16 + 16; i++) {
                pad_t *p = &t->pad[i];
                if (!p->used || p->s) continue;
                if (p->start >= f0 && p->start < f1) state = 2;
                else if (state == 0 && i == t->sel && f1 > p->start && f0 < p->end) state = 1;
            }
            if (state == 0 && t->cue > 0 && t->cue >= f0 && t->cue < f1) state = 2;
        }
        t->wv[c] = state * WLEV + lev;
    }
}
/* the column the playhead is in (-1: not in view): the selected pad while it sounds, else the song while it plays */
static int play_col(inst_t *t) {
    pad_t *sp = &t->pad[t->sel];
    double pos = -1;
    for (int i = 0; i < NVOICE; i++) if (t->v[i].on && t->v[i].pad == t->sel) { pos = t->v[i].pos; break; }
    if (pos < 0) {
        if (!(t->playing || t->scr)) return -1;
        if (t->zoom == 1 && sp->used && sp->s) return -1;   /* the pad's own sample: the song is not in this view */
        pos = t->ppos;
    }
    if (pos < t->win0 || pos >= t->win1) return -1;
    return (int)((pos - t->win0) * WCOLS / (t->win1 - t->win0));
}
static int env_level(inst_t *t, int c) {   /* the selected pad's envelope, 24 heights, over a time axis that fits it */
    pad_t *p = &t->pad[t->sel];
    if (!p->used) return 0;
    double a = p->att, d = p->dec, r = p->rel, hold = (a + d + r) * 0.35 + 60, tot = a + d + hold + r;
    double x = (c + 0.5) * tot / NEV, e;
    if (x < a) e = a > 0 ? x / a : 1;
    else if (x < a + d) e = 1 - (1 - p->sus) * (x - a) / (d > 0 ? d : 1);
    else if (x < a + d + hold) e = p->sus;
    else e = p->sus * (1 - (x - a - d - hold) / (r > 0 ? r : 1));
    if (e < 0) e = 0;
    return (int)(e * 23 + 0.5);
}

/* parameters that change by themselves, reported to the host about 9 times a second (the wrapper asks once per block):
 * the platter picture, the playhead column, the result list's scroll bar */
int mpc_engine_changed(void *vp, char *key, int len);
int mpc_engine_changed(void *vp, char *key, int len) {
    inst_t *t = vp;
    if (!t) return 0;
    double nw = now_s();
    if (nw - t->rep_t < 0.11) return 0;
    if (t->plat != t->plat_rep) { t->rep_t = nw; t->plat_rep = t->plat; snprintf(key, (size_t)len, "plat"); return 1; }
    if (t->top != t->top_rep) { t->rep_t = nw; t->top_rep = t->top; snprintf(key, (size_t)len, "scroll"); return 1; }
    int c = play_col(t);
    if (c != t->play_rep || t->pc_phase) {   /* first the column it left, then the one it entered */
        t->rep_t = nw;
        if (t->pc_phase == 0 && t->play_rep >= 0) { t->pc_phase = 1; snprintf(key, (size_t)len, "wv%d", t->play_rep); return 1; }
        t->pc_phase = 0;
        t->play_rep = c;
        if (c >= 0) { snprintf(key, (size_t)len, "wv%d", c); return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------------ state */
static int get_state(inst_t *t, char *o, int n) {
    int k = 0;
#define PUT(...) do { int w_ = snprintf(o + k, n > k ? n - k : 0, __VA_ARGS__); if (w_ > 0) k += w_; } while (0)
    char title[100];
    cpy(title, t->song_title, sizeof title);
    for (char *c = title; *c; c++) if (*c == '\n' || *c == '|') *c = ' ';
    PUT("CR1\nsong=%s|%d|%s\n", t->song_id, t->song_secs, title);
    PUT("map=%d|%d\n", t->map_first, t->map_last);
    PUT("deck=%d|%d|%d\n", t->dpitch, t->prange, t->klock);
    PUT("set=%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f|%d|%d|%d|%d\n", t->bank, t->sel, t->chop_by, t->assign, t->slice, t->bpm,
        t->iso, t->base_note, t->loop, t->sens, t->vol, t->cue, t->zoom, t->vel_on, t->arm);
    for (int i = 0; i < NPAD && k < n - 200; i++) {
        pad_t *p = &t->pad[i];
        if (!p->used) continue;
        PUT("p=%d|%d|%d|%d|%.3f|%d|%d|%d|%d|%d|%d|%d|%.3f|%d|%s\n", i, p->start, p->end, p->pitch, p->level, p->att, p->rel, p->mode,
            p->rev, p->iso, p->auto_end, p->dec, p->sus, p->grp, p->s ? p->s->file : "");
    }
#undef PUT
    return k < n ? k : n - 1;
}

static void set_state(inst_t *t, const char *s) {
    if (strncmp(s, "CR1", 3)) return;
    voices_off(t);
    pads_unref_all(t->pad);
    for (int i = 0; i < NPAD; i++) { t->pad[i].s = NULL; pad_clear(&t->pad[i]); }
    for (int u = 0; u < t->nundo; u++) pads_unref_all(t->undo[u]);
    t->nundo = 0;
    char id[40] = "", title[100] = "";
    int secs = 0;
    const char *l = s;
    while (l && *l) {
        const char *e = strchr(l, '\n');
        char line[400];
        int ln = e ? (int)(e - l) : (int)strlen(l);
        if (ln >= (int)sizeof line) ln = sizeof line - 1;
        memcpy(line, l, ln);
        line[ln] = 0;
        l = e ? e + 1 : NULL;
        if (!strncmp(line, "song=", 5)) {
            sscanf(line + 5, "%39[^|]|%d|%99[^\n]", id, &secs, title);
            if (id[0] == '|') { id[0] = 0; }
        } else if (!strncmp(line, "deck=", 5)) {
            int a = 500, b = 3, c = 0;
            sscanf(line + 5, "%d|%d|%d", &a, &b, &c);
            t->dpitch = clampi(a, 0, 1000); t->prange = clampi(b, 0, 3); t->klock = !!c;
        } else if (!strncmp(line, "map=", 4)) {
            int a = 0, b = 0;
            if (sscanf(line + 4, "%d|%d", &a, &b) == 2 && a >= 0 && b > a && b < 128) { t->map_first = a; t->map_last = b; padmap_build(t); }
        } else if (!strncmp(line, "set=", 4)) {
            float sens = 0.5f, vol = 0.8f;
            sscanf(line + 4, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%f|%f|%d|%d|%d|%d", &t->bank, &t->sel, &t->chop_by, &t->assign, &t->slice,
                   &t->bpm, &t->iso, &t->base_note, &t->loop, &sens, &vol, &t->cue, &t->zoom, &t->vel_on, &t->arm);
            t->bank = clampi(t->bank, 0, 3); t->sel = clampi(t->sel, 0, NPAD - 1); t->chop_by = clampi(t->chop_by, 0, 2);
            t->assign = clampi(t->assign, 0, 1); t->slice = clampi(t->slice, 0, 4); t->bpm = clampi(t->bpm, 70, 170);
            t->iso = clampi(t->iso, 0, NISO - 1); t->base_note = clampi(t->base_note, 0, 127); t->sens = clampf(sens, 0, 1);
            t->vol = clampf(vol, 0, 1); t->zoom = clampi(t->zoom, 0, 1); t->cue = clampi(t->cue, 0, MAXF);
        } else if (!strncmp(line, "p=", 2)) {
            pad_t p;
            memset(&p, 0, sizeof p);
            int i = -1;
            char file[48] = "";
            p.dec = 300; p.sus = 1.0f;
            sscanf(line + 2, "%d|%d|%d|%d|%f|%d|%d|%d|%d|%d|%d|%d|%f|%d|%47[^\n]", &i, &p.start, &p.end, &p.pitch, &p.level, &p.att,
                   &p.rel, &p.mode, &p.rev, &p.iso, &p.auto_end, &p.dec, &p.sus, &p.grp, file);
            p.dec = clampi(p.dec, 0, 3000); p.sus = clampf(p.sus, 0, 1); p.grp = clampi(p.grp, 0, 8);
            if (i < 0 || i >= NPAD || p.end <= p.start) continue;
            p.used = 1;
            p.pitch = clampi(p.pitch, -24, 24); p.level = clampf(p.level, 0, 1); p.att = clampi(p.att, 0, 2000);
            p.rel = clampi(p.rel, 0, 4000); p.mode = clampi(p.mode, 0, 1); p.rev = !!p.rev; p.iso = clampi(p.iso, 0, NISO - 1);
            if (file[0] && !strstr(file, "/") && !strstr(file, "..")) {
                char path[520];
                snprintf(path, sizeof path, "%s/pads/%s", t->dir, file);
                FILE *f = fopen(path, "rb");
                if (!f) continue;
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (sz < 256 || sz > 64L * 1024 * 1024) { fclose(f); continue; }
                int16_t *d = malloc(sz);
                if (!d || fread(d, 1, sz, f) != (size_t)sz) { free(d); fclose(f); continue; }
                fclose(f);
                p.s = smp_new(d, (int)(sz / 4));
                if (!p.s) continue;
                cpy(p.s->file, file, sizeof p.s->file);
                p.start = clampi(p.start, 0, p.s->len - 1);
                p.end = clampi(p.end, p.start + 1, p.s->len);
            }
            t->pad[i] = p;
        }
    }
    t->lazy_next = t->bank * 16;
    if (id[0] && strcmp(id, t->song_id)) {
        /* new_song() would freeze the restored pads against the wrong song: load without it */
        int keep = t->avail;
        t->avail = 0;
        open_song(t, id, title, secs, 0);
        (void)keep;
    }
    wave_dirty(t);
}

/* ------------------------------------------------------------------ params */
static int is(const char *k, const char *x) { return !strcmp(k, x); }

static void show_history(inst_t *t) {
    char hp[460], line[400];
    snprintf(hp, sizeof hp, "%s/history.txt", t->dir);
    FILE *f = fopen(hp, "r");
    int n = 0;
    pthread_mutex_lock(&t->m);
    if (f) {
        while (fgets(line, sizeof line, f) && n < NRES) {
            line[strcspn(line, "\n")] = 0;
            char *p = line;
            res_t *e = &t->res[n];
            memset(e, 0, sizeof *e);
            cpy(e->id, field(&p), sizeof e->id);
            e->secs = atoi(field(&p));
            cpy(e->title, field(&p), sizeof e->title);
            e->kind = 2;
            if (e->id[0]) n++;
        }
        fclose(f);
    }
    t->nres = n;
    t->top = 0;
    t->res_kind = 2;
    cpy(t->res_head, n ? "HISTORY: PLAYED BEFORE" : "HISTORY IS EMPTY", sizeof t->res_head);
    pthread_mutex_unlock(&t->m);
    set_status(t, n ? "TAP A SONG (CACHED SONGS LOAD AT ONCE)" : "PLAY SOMETHING FIRST");
}

static void do_search(inst_t *t) {
    if (!t->query[0]) { t->kb = 1; set_status(t, "TYPE SOMETHING FIRST"); bump(t); return; }
    t->kb = 0;
    pthread_mutex_lock(&t->m);
    t->nres = 0;
    t->top = 0;
    snprintf(t->res_head, sizeof t->res_head, "SEARCHING \"%s\"...", t->query);
    upper(t->res_head);
    pthread_mutex_unlock(&t->m);
    set_status(t, "SEARCHING...");
    net_request(t, t->find ? J_PLISTS : J_SEARCH, t->query, t->find ? 16 : 32);
    bump(t);
}

static void row_tap(inst_t *t, int row) {
    int i = t->top + row;
    pthread_mutex_lock(&t->m);
    if (i < 0 || i >= t->nres) { pthread_mutex_unlock(&t->m); return; }
    res_t e = t->res[i];
    pthread_mutex_unlock(&t->m);
    if (e.kind == 1) {
        cpy(t->last_pl, e.id, sizeof t->last_pl);
        pthread_mutex_lock(&t->m);
        t->nres = 0;
        t->top = 0;
        snprintf(t->res_head, sizeof t->res_head, "OPENING %s...", e.title);
        upper(t->res_head);
        pthread_mutex_unlock(&t->m);
        set_status(t, "OPENING PLAYLIST...");
        net_request(t, J_OPEN, e.id, 40);
    } else {
        if (!strcmp(e.id, t->song_id) && t->sstate != ST_ERROR && t->sstate != ST_IDLE) {
            t->playing = !t->playing;   /* the song that is loaded: play/pause */
        } else {
            open_song(t, e.id, e.title, e.secs, 1);
        }
    }
    bump(t);
}

static void kb_key(inst_t *t, const char *k) {
    size_t n = strlen(t->query);
    if (is(k, "spc")) { if (n && n < sizeof t->query - 1 && t->query[n - 1] != ' ') { t->query[n] = ' '; t->query[n + 1] = 0; } }
    else if (is(k, "del")) { if (n) t->query[n - 1] = 0; }
    else if (is(k, "clr")) t->query[0] = 0;
    else if (is(k, "go")) { do_search(t); return; }
    else if (is(k, "close")) t->kb = 0;
    else if (strlen(k) == 1 && n < sizeof t->query - 1) { t->query[n] = (char)tolower((unsigned char)k[0]); t->query[n + 1] = 0; }
    bump(t);
}

static void export_pads(inst_t *t) {
    if (t->exporting) { set_status(t, "EXPORT RUNNING..."); return; }
    int used = 0;
    for (int i = 0; i < NPAD; i++) if (t->pad[i].used) used++;
    if (!used) { set_status(t, "NO CHOPS TO EXPORT"); return; }
    char name[64], root[300] = "", dir[460];
    safe_name(name, sizeof name, t->song_title[0] ? t->song_title : "chops");
    const char *roots[] = {"/sdcard/MPC Documents", "/sdcard/Force Documents", NULL};
    struct stat st;
    for (int i = 0; roots[i]; i++) if (!stat(roots[i], &st) && S_ISDIR(st.st_mode)) { snprintf(root, sizeof root, "%s/CRATE RIPPA", roots[i]); break; }
    if (!root[0]) snprintf(root, sizeof root, "%s/exports", t->dir);
    snprintf(dir, sizeof dir, "%s/%s", root, name);
    for (int k = 2; !stat(dir, &st) && k < 100; k++) snprintf(dir, sizeof dir, "%s/%s %d", root, name, k);   /* never overwrite */
    mkdirs(dir);
    t->exporting = 1;
    int n = 0;
    for (int i = 0; i < NPAD; i++) {
        pad_t *p = &t->pad[i];
        if (!p->used) continue;
        const int16_t *src = p->s ? p->s->d : t->buf;
        int slen = pad_len_src(t, p);
        int s0 = clampi(p->start, 0, slen), s1 = clampi(p->end, s0, slen);
        if (s1 - s0 < 32) continue;
        int16_t *o = malloc((size_t)(s1 - s0) * 4);
        if (!o) continue;
        memcpy(o, src + (size_t)s0 * 2, (size_t)(s1 - s0) * 4);
        struct job *j = calloc(1, sizeof *j);
        if (!j) { free(o); continue; }
        j->kind = JOB_WAV;
        snprintf(j->path, sizeof j->path, "%s/%c%02d %s.wav", dir, 'A' + i / 16, i % 16 + 1, name);
        j->d = o;
        j->len = s1 - s0;
        j->pp = *p;
        j->pp.s = NULL;
        j->pp.start = 0;
        j->pp.end = s1 - s0;
        job_add(t, j);
        n++;
    }
    struct job *j = calloc(1, sizeof *j);
    if (j) {
        j->kind = JOB_DONE;
        const char *shown = strstr(dir, "Documents/") ? strstr(dir, "Documents/") + 10 : dir;
        snprintf(j->path, sizeof j->path, "EXPORTED %d WAVS TO %s", n, shown);
        upper(j->path);
        job_add(t, j);
    } else t->exporting = 0;
    set_status(t, "EXPORTING...");
}

/* ------------------------------------------------------------------ pad actions (from Crate Digga) */
static const char *OP_NAMES[4] = {"", "COPY", "MOVE", "SWAP"};
static void pad_copy_to(inst_t *t, int dst, const pad_t *src) {
    pad_clear(&t->pad[dst]);
    t->pad[dst] = *src;
    smp_ref(t->pad[dst].s);
}
static void do_pad_op(inst_t *t, int op, int a, int b) {   /* a -> b */
    char m[TXT];
    if (a == b) { set_status(t, "PICK ANOTHER PAD AS TARGET"); return; }
    undo_push(t);
    voices_off(t);
    pad_t tmp = t->pad[a];
    if (op == 1) pad_copy_to(t, b, &tmp);
    else if (op == 2) { pad_t x = t->pad[a]; memset(&t->pad[a], 0, sizeof(pad_t)); pad_clear(&t->pad[a]); smp_unref(t->pad[b].s); t->pad[b] = x; }
    else { pad_t x = t->pad[a]; t->pad[a] = t->pad[b]; t->pad[b] = x; }
    snprintf(m, sizeof m, "%s %c%d -> %c%d DONE (UNDO BRINGS IT BACK)", OP_NAMES[op], 'A' + a / 16, a % 16 + 1, 'A' + b / 16, b % 16 + 1);
    set_status(t, m);
    fix_auto_ends(t, a / 16);
    fix_auto_ends(t, b / 16);
    t->sel = b;
    wave_dirty(t);
}
static void do_bank_op(inst_t *t, int op, int a, int b) {
    char m[TXT];
    if (a == b) { set_status(t, "PICK ANOTHER BANK AS TARGET"); return; }
    undo_push(t);
    voices_off(t);
    for (int i = 0; i < 16; i++) {
        pad_t *pa = &t->pad[a * 16 + i], *pb = &t->pad[b * 16 + i];
        if (op == 1) { pad_t x = *pa; pad_copy_to(t, b * 16 + i, &x); }
        else if (op == 2) { pad_t x = *pa; smp_unref(pb->s); *pb = x; memset(pa, 0, sizeof *pa); pad_clear(pa); }
        else { pad_t x = *pa; *pa = *pb; *pb = x; }
    }
    snprintf(m, sizeof m, "BANK %s %c -> %c DONE (UNDO BRINGS IT BACK)", OP_NAMES[op], 'A' + a, 'A' + b);
    set_status(t, m);
    wave_dirty(t);
}
static void select_pad(inst_t *t, int idx) {
    if (t->sel != idx) { t->sel = idx; wave_dirty(t); }
}
/* a pad tapped on the screen or hit on the MPC: a waiting action takes it as its target, a lazy chop cuts, else it plays */
static void pad_hit(inst_t *t, int idx, float vel, int from_screen) {
    if (t->pend_op) {
        int op = t->pend_op, a = t->pend_src;
        t->pend_op = 0;
        t->ops[0] = 0;
        do_pad_op(t, op, a, idx);
        return;
    }
    if (t->chop_by == CH_LAZY && t->arm && t->playing && song_ok(t)) { lazy_chop(t, idx); return; }
    select_pad(t, idx);
    voice_start(t, idx, vel);
    if (from_screen && !t->pad[idx].used) {
        char m[TXT];
        snprintf(m, sizeof m, "%c%d IS EMPTY: CHOP IT FIRST", 'A' + idx / 16, idx % 16 + 1);
        set_status(t, m);
    }
}

static void pad_edit_bounds(inst_t *t, int which, int delta_ms) {
    pad_t *p = &t->pad[t->sel];
    if (!p->used) { set_status(t, "THIS PAD IS EMPTY: PICK A CHOPPED PAD (< PAD / PAD > OR THE MPC PADS)"); return; }
    int len = pad_len_src(t, p);
    int d = delta_ms * SR / 1000;
    if (which == 0) p->start = clampi(p->start + d, 0, p->end - 64);
    else p->end = clampi(p->end + d, p->start + 64, len);
    p->auto_end = 0;
    if (which == 0 && !p->s) fix_auto_ends(t, t->sel / 16);
    wave_dirty(t);
}
static void pad_step(inst_t *t, int dir) {   /* the next pad of the bank that has a chop (or the next one, if none has) */
    int b = t->bank * 16, cur = t->sel % 16;
    for (int k = 1; k <= 16; k++) {
        int i = (cur + dir * k + 32) % 16;
        if (t->pad[b + i].used) { select_pad(t, b + i); return; }
    }
    select_pad(t, b + (cur + dir + 16) % 16);
}
static float ms_to_slider(int ms, int max) { return (float)sqrt(clampf((float)ms / max, 0, 1)); }
static int slider_to_ms(float v, int max) { v = clampf(v, 0, 1); return (int)lround(v * v * max); }
#define A_MAX 2000
#define D_MAX 3000
#define R_MAX 4000

static void set_param(void *vp, const char *key, const char *val) {
    inst_t *t = vp;
    float f = (float)atof(val);
    int on = f > 0.5f;
    pad_t *sp = &t->pad[t->sel];
    if (is(key, "state")) { set_state(t, val); return; }
    if (is(key, "lfo_bpm")) { t->host_bpm = f; return; }
    if (!strncmp(key, "k_", 2)) { if (on) kb_key(t, key + 2); return; }
    if (key[0] == 'r' && key[1] == 't' && isdigit((unsigned char)key[2])) { if (on) row_tap(t, atoi(key + 2) - 1); return; }
    if (key[0] == 'p' && key[1] == 'd' && isdigit((unsigned char)key[2])) {
        if (on) pad_hit(t, t->bank * 16 + clampi(atoi(key + 2), 1, 16) - 1, 0.9f, 1);
        return;
    }
    if (key[0] == 'b' && key[1] == 'k' && isdigit((unsigned char)key[2])) {
        if (!on) return;
        int b = clampi(atoi(key + 2), 1, 4) - 1;
        if (t->pend_bk) { int op = t->pend_bk; t->pend_bk = 0; t->ops[0] = 0; do_bank_op(t, op, t->bank, b); return; }
        if (b != t->bank) { t->bank = b; t->sel = b * 16 + t->sel % 16; t->lazy_next = b * 16; wave_dirty(t); }
        return;
    }
    /* sliders and knobs */
    if (is(key, "kb")) { t->kb = on; bump(t); return; }
    if (is(key, "find")) { t->find = clampi((int)lroundf(f), 0, 1); return; }
    if (is(key, "vol")) { t->vol = clampf(f, 0, 1); return; }
    if (is(key, "padvol")) { t->padvol = clampf(f, 0, 1); return; }
    if (is(key, "loop")) { t->loop = on; return; }
    if (is(key, "seek")) {
        int len = view_len(t);
        int p = (int)(clampf(f, 0, 1) * len);
        t->ppos = p < t->avail ? p : (t->avail > 0 ? t->avail - 1 : 0);
        return;
    }
    if (is(key, "scroll")) {   /* the scroll bar: up = the top of the list */
        int maxtop = t->nres > ROWS ? t->nres - ROWS : 0;
        int top = (int)lround((1.0 - clampf(f, 0, 1)) * maxtop);
        if (top != t->top) { t->top = top; t->top_rep = top; bump(t); }
        return;
    }
    if (is(key, "deck_pitch")) {
        int v = (int)(clampf(f, 0, 1) * 1000 + 0.5f);
        if (v > 485 && v < 515) v = 500;   /* detent in the middle: exactly 0 */
        t->dpitch = v;
        return;
    }
    if (is(key, "prange")) { t->prange = clampi((int)lroundf(f), 0, 3); bump(t); return; }
    if (is(key, "klock")) { t->klock = on; bump(t); return; }
    if (is(key, "jog")) {   /* the finger on the record */
        int v = (int)(clampf(f, 0, 1) * 1000 + 0.5f);
        long long ms = now_ms();
        if (ms - t->jogt > 250) t->jogref = v;   /* a new touch starts where the finger is: no jump */
        t->jogv = v;
        t->jogt = ms;
        return;
    }
    if (is(key, "plat")) return;
    if (is(key, "chop_by")) { t->chop_by = clampi((int)lroundf(f), 0, 2); bump(t); return; }
    if (is(key, "assign")) { t->assign = clampi((int)lroundf(f), 0, 1); return; }
    if (is(key, "arm")) { t->arm = on; return; }
    if (is(key, "sens")) { t->sens = clampf(f, 0, 1); return; }
    if (is(key, "bpm")) { t->bpm = clampi((int)lroundf(f), 70, 170); return; }
    if (is(key, "slice")) { t->slice = clampi((int)lroundf(f), 0, 4); return; }
    if (is(key, "zoom")) { t->zoom = clampi((int)lroundf(f), 0, 1); wave_fit(t); bump(t); return; }
    if (is(key, "iso")) { t->iso = clampi((int)lroundf(f), 0, NISO - 1); return; }
    if (is(key, "velo")) { t->vel_on = on; return; }
    if (!strncmp(key, "wv", 2) || !strncmp(key, "ev", 2)) return;   /* pictures: read only */
    /* the selected pad */
    if (is(key, "pitch") || is(key, "level") || is(key, "pmode") || is(key, "rev") || is(key, "iso_pad") || !strncmp(key, "ed_", 3)) {
        if (is(key, "ed_play")) { if (on) { if (!sp->used) set_status(t, "THIS PAD IS EMPTY"); voice_start(t, t->sel, 0.9f); } return; }
        if (!sp->used) return;
        if (is(key, "pitch")) sp->pitch = clampi((int)lroundf(f), -24, 24);
        else if (is(key, "level")) sp->level = clampf(f, 0, 1);
        else if (is(key, "pmode")) sp->mode = clampi((int)lroundf(f), 0, 1);
        else if (is(key, "rev")) sp->rev = on;
        else if (is(key, "iso_pad")) sp->iso = clampi((int)lroundf(f), 0, NISO - 1);
        else if (is(key, "ed_start") || is(key, "ed_end")) {
            wave_check(t);
            int len = pad_len_src(t, sp);
            int x = (int)(t->win0 + clampf(f, 0, 1) * (t->win1 - t->win0));
            if (is(key, "ed_start")) { sp->start = clampi(x, 0, sp->end - 64); if (!sp->s) fix_auto_ends(t, t->sel / 16); }
            else sp->end = clampi(x, sp->start + 64, len);
            sp->auto_end = 0;
            t->wave_rev++;
        }
        else if (is(key, "ed_a")) sp->att = slider_to_ms(f, A_MAX);
        else if (is(key, "ed_d")) sp->dec = slider_to_ms(f, D_MAX);
        else if (is(key, "ed_s")) sp->sus = clampf(f, 0, 1);
        else if (is(key, "ed_r")) sp->rel = slider_to_ms(f, R_MAX);
        return;
    }
    if (!on) return;   /* everything below is a trigger */
    if (is(key, "kb_open")) { t->kb = !t->kb; bump(t); }
    else if (is(key, "search")) do_search(t);
    else if (is(key, "history")) { show_history(t); bump(t); }
    else if (is(key, "play")) {
        if (t->avail <= 0 && t->sstate != ST_LOAD && t->sstate != ST_RESOLVE) { set_status(t, "PICK A SONG FIRST"); return; }
        t->playing = !t->playing;
        bump(t);
    } else if (is(key, "stop")) { t->playing = 0; t->ppos = t->cue; for (int i = 0; i < NVOICE; i++) t->v[i].stage = 1; bump(t); }
    else if (is(key, "stop_all")) { stop_all(t); set_status(t, "ALL STOPPED"); bump(t); }
    else if (is(key, "deck_cue")) { t->playing = 0; t->ppos = t->cue < t->avail ? t->cue : 0; bump(t); }
    else if (is(key, "rew")) t->ppos = t->ppos > 5 * SR ? t->ppos - 5 * SR : 0;
    else if (is(key, "fwd")) { double p = t->ppos + 5 * SR; t->ppos = p < t->avail ? p : t->ppos; }
    else if (is(key, "cue_set")) {
        t->cue = (int)t->ppos;
        char b[TXT], tm[24];
        fmt_time(tm, sizeof tm, t->cue / (double)SR, 1);
        snprintf(b, sizeof b, "CUE AT %s", tm);
        set_status(t, b);
        wave_dirty(t);
    } else if (is(key, "cue_go")) { t->ppos = t->cue < t->avail ? t->cue : 0; }
    else if (is(key, "tap")) lazy_chop(t, -1);
    else if (is(key, "auto")) {
        if (t->chop_by == CH_HITS) auto_hits(t);
        else if (t->chop_by == CH_BEATS) auto_beats(t);
        else set_status(t, "LAZY: PLAY THE SONG AND HIT PADS (OR TAP)");
    } else if (is(key, "bpm_detect")) detect_bpm(t);
    else if (is(key, "bpm_mpc")) {
        if (t->host_bpm > 20) { t->bpm = clampi((int)lroundf(t->host_bpm), 70, 170); set_status(t, "TEMPO FROM THE MPC"); bump(t); }
        else set_status(t, "NO TEMPO FROM THE MPC");
    } else if (is(key, "undo")) undo_pop(t);
    else if (is(key, "clr_pad")) {
        if (sp->used) { undo_push(t); voices_off(t); pad_clear(sp); if (t->chop_by == CH_LAZY) fix_auto_ends(t, t->bank); set_status(t, "PAD CLEARED (UNDO BRINGS IT BACK)"); wave_dirty(t); }
        else set_status(t, "THIS PAD IS ALREADY EMPTY");
    } else if (is(key, "clr_bank")) {
        undo_push(t);
        voices_off(t);
        for (int i = t->bank * 16; i < t->bank * 16 + 16; i++) pad_clear(&t->pad[i]);
        t->lazy_next = t->bank * 16;
        set_status(t, "BANK CLEARED (UNDO BRINGS IT BACK)");
        wave_dirty(t);
    } else if (is(key, "learn")) {
        t->learn = t->learn ? 0 : 1;
        set_status(t, t->learn ? "LEARN: HIT THE FIRST PAD (PAD 1, BOTTOM LEFT)" : "LEARN CANCELLED, PADS AS BEFORE");
        bump(t);
    } else if (is(key, "export")) export_pads(t);
    else if (is(key, "op_copy") || is(key, "op_move") || is(key, "op_swap")) {
        int op = is(key, "op_copy") ? 1 : is(key, "op_move") ? 2 : 3;
        t->pend_bk = 0;
        if (t->pend_op == op) { t->pend_op = 0; t->ops[0] = 0; set_status(t, "CANCELLED"); bump(t); return; }
        if (!sp->used && op != 3) { set_status(t, "PICK A PAD WITH A CHOP FIRST"); return; }
        t->pend_op = op; t->pend_src = t->sel;
        snprintf(t->ops, sizeof t->ops, "%s %c%d: TAP THE TARGET PAD", OP_NAMES[op], 'A' + t->sel / 16, t->sel % 16 + 1);
        set_status(t, t->ops);
        bump(t);
    } else if (is(key, "op_del")) {
        if (sp->used) { undo_push(t); voices_off(t); pad_clear(sp); fix_auto_ends(t, t->bank); set_status(t, "PAD DELETED (UNDO BRINGS IT BACK)"); wave_dirty(t); }
    } else if (is(key, "bk_copy") || is(key, "bk_move") || is(key, "bk_swap")) {
        int op = is(key, "bk_copy") ? 1 : is(key, "bk_move") ? 2 : 3;
        t->pend_op = 0;
        if (t->pend_bk == op) { t->pend_bk = 0; t->ops[0] = 0; set_status(t, "CANCELLED"); bump(t); return; }
        t->pend_bk = op;
        snprintf(t->ops, sizeof t->ops, "BANK %s %c: TAP THE TARGET BANK", OP_NAMES[op], 'A' + t->bank);
        set_status(t, t->ops);
        bump(t);
    }
    else if (is(key, "pad_prev")) pad_step(t, -1);
    else if (is(key, "pad_next")) pad_step(t, 1);
    else if (is(key, "pad_play")) voice_start(t, t->sel, 0.9f);
    else if (is(key, "fit")) { wave_fit(t); bump(t); }
    else if (is(key, "grp_m")) { if (sp->used) { sp->grp = clampi(sp->grp - 1, 0, 8); bump(t); } }
    else if (is(key, "grp_p")) { if (sp->used) { sp->grp = clampi(sp->grp + 1, 0, 8); bump(t); } }
    else if (is(key, "s_mm")) pad_edit_bounds(t, 0, -50);
    else if (is(key, "s_m")) pad_edit_bounds(t, 0, -5);
    else if (is(key, "s_p")) pad_edit_bounds(t, 0, 5);
    else if (is(key, "s_pp")) pad_edit_bounds(t, 0, 50);
    else if (is(key, "e_mm")) pad_edit_bounds(t, 1, -50);
    else if (is(key, "e_m")) pad_edit_bounds(t, 1, -5);
    else if (is(key, "e_p")) pad_edit_bounds(t, 1, 5);
    else if (is(key, "e_pp")) pad_edit_bounds(t, 1, 50);
    else if (is(key, "iso_bank")) {
        for (int i = t->bank * 16; i < t->bank * 16 + 16; i++) if (t->pad[i].used) t->pad[i].iso = t->iso;
        char b[TXT];
        snprintf(b, sizeof b, "BANK %c NOW PLAYS %s", 'A' + t->bank, ISO_NAMES[t->iso]);
        set_status(t, b);
        bump(t);
    } else if (is(key, "iso_sel")) {
        if (sp->used) { sp->iso = t->iso; set_status(t, "SELECTED PAD UPDATED"); bump(t); }
        else set_status(t, "THIS PAD IS EMPTY");
    } else if (is(key, "ytdlp_update")) { set_status(t, "UPDATING YT-DLP..."); net_request(t, J_UPDATE, "", 0); }
}

static int put(char *b, int n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int put(char *b, int n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    if (k < 0) return 0;
    return k < n ? (k > 0 ? k : 1) : n - 1;
}

static void pad_label(inst_t *t, int idx, char *b, int n) {
    pad_t *p = &t->pad[idx];
    char nm[8], body[40];
    snprintf(nm, sizeof nm, "%c%d", 'A' + idx / 16, idx % 16 + 1);
    if (!p->used) snprintf(body, sizeof body, "%s", nm);
    else if (p->s) snprintf(body, sizeof body, "%s  %.1fS", nm, (p->end - p->start) / (double)SR);
    else { char tm[24]; fmt_time(tm, sizeof tm, p->start / (double)SR, 0); snprintf(body, sizeof body, "%s  %s", nm, tm); }
    if (t->pend_op && idx != t->pend_src) snprintf(b, n, "%s ?", body);
    else if (idx == t->sel) snprintf(b, n, "> %s <", body);
    else snprintf(b, n, "%s", body);
}
static const char *mode_names[2] = {"ONE SHOT", "GATE"};

static int get_param(void *vp, const char *key, char *b, int n) {
    inst_t *t = vp;
    pad_t *sp = &t->pad[t->sel];
    if (n < 2) return 0;
    if (is(key, "state")) return get_state(t, b, n);
    if (is(key, "display_rev")) return put(b, n, "%u", t->rev);
    /* the result rows */
    if (key[0] == 'r' && isdigit((unsigned char)key[1])) {
        int i = t->top + atoi(key + 1) - 1;
        pthread_mutex_lock(&t->m);
        int k;
        if (i < 0 || i >= t->nres) k = put(b, n, " ");
        else {
            res_t *e = &t->res[i];
            char tm[16] = "";
            int now = e->kind != 1 && !strcmp(e->id, t->song_id);
            if (e->secs > 0) fmt_time(tm, sizeof tm, e->secs, 0);
            if (e->kind == 1) k = put(b, n, "[LIST]  %.56s  -  %.22s", e->title, e->chan);
            else if (e->chan[0]) k = put(b, n, "%s%s  %.56s  -  %.20s", now ? "> " : "", tm, e->title, e->chan);
            else k = put(b, n, "%s%s  %.76s", now ? "> " : "", tm, e->title);
        }
        pthread_mutex_unlock(&t->m);
        return k;
    }
    if (key[0] == 'r' && key[1] == 't') return put(b, n, "0");
    if (key[0] == 'p' && key[1] == 'n' && isdigit((unsigned char)key[2])) {
        pad_label(t, t->bank * 16 + clampi(atoi(key + 2), 1, 16) - 1, b, n);
        return b[0] ? (int)strlen(b) : put(b, n, " ");
    }
    if (key[0] == 'b' && key[1] == 't' && isdigit((unsigned char)key[2])) {
        int bk = clampi(atoi(key + 2), 1, 4) - 1, used = 0;
        for (int i = 0; i < 16; i++) used += t->pad[bk * 16 + i].used;
        if (t->pend_bk && bk != t->bank) return put(b, n, "%c ?", 'A' + bk);
        return bk == t->bank ? put(b, n, "> %c %d <", 'A' + bk, used) : put(b, n, "%c %d", 'A' + bk, used);
    }
    if (key[0] == 'w' && key[1] == 'v' && isdigit((unsigned char)key[2])) {
        wave_build(t);
        int c = clampi(atoi(key + 2), 0, WCOLS - 1), f = t->wv[c];
        if (c == play_col(t)) f = 3 * WLEV + f % WLEV;
        return put(b, n, "%.5f", f / (float)(WFR - 1));
    }
    if (key[0] == 'e' && key[1] == 'v' && isdigit((unsigned char)key[2])) return put(b, n, "%.5f", env_level(t, clampi(atoi(key + 2), 0, NEV - 1)) / 23.0f);
    if (is(key, "plat")) return put(b, n, "%.4f", t->plat / (double)(NPLAT - 1));
    if (is(key, "scroll")) { int maxtop = t->nres > ROWS ? t->nres - ROWS : 0; return put(b, n, "%.4f", maxtop ? 1.0 - (double)t->top / maxtop : 1.0); }
    if (is(key, "q_txt")) return put(b, n, "%s", t->query[0] ? t->query : "TAP HERE AND TYPE A SONG...");
    if (is(key, "query_kb")) return put(b, n, "%s_", t->query);
    if (is(key, "status")) { pthread_mutex_lock(&t->m); int k = put(b, n, "%s", t->status); pthread_mutex_unlock(&t->m); return k; }
    if (is(key, "res_head")) {
        pthread_mutex_lock(&t->m);
        int k = t->nres > ROWS ? put(b, n, "%s   %d-%d OF %d", t->res_head, t->top + 1, t->top + ROWS, t->nres) : put(b, n, "%s", t->res_head);
        pthread_mutex_unlock(&t->m);
        return k;
    }
    if (is(key, "now")) {
        pthread_mutex_lock(&t->m);
        int k = t->song_title[0] ? put(b, n, "%s", t->song_title) : put(b, n, "NOTHING ON THE DECK");
        pthread_mutex_unlock(&t->m);
        return k;
    }
    if (is(key, "time")) {
        char a[24], c[24];
        int len = view_len(t);
        fmt_time(a, sizeof a, t->ppos / SR, 0);
        fmt_time(c, sizeof c, len / (double)SR, 0);
        if (t->loading && t->total) return put(b, n, "%s / %s  (%d%%)", a, c, (int)(100.0 * t->avail / t->total));
        return put(b, n, "%s / %s", a, c);
    }
    if (is(key, "seek")) { int len = view_len(t); return put(b, n, "%.4f", len > 0 ? t->ppos / len : 0.0); }
    if (is(key, "pitch_txt")) {
        double sp_ = deck_speed(t);
        if (t->prange == 3) return put(b, n, "%+.1f ST", 12.0 * log2(sp_));
        return put(b, n, "%+.1f %%", (sp_ - 1.0) * 100.0);
    }
    if (is(key, "sel")) {
        if (!sp->used) return put(b, n, "%c%d  EMPTY", 'A' + t->sel / 16, t->sel % 16 + 1);
        char a[24];
        fmt_time(a, sizeof a, sp->start / (double)SR, 1);
        if (sp->s) return put(b, n, "%c%d  OWN SAMPLE  %.3f S", 'A' + t->sel / 16, t->sel % 16 + 1, (sp->end - sp->start) / (double)SR);
        return put(b, n, "%c%d  AT %s  %.3f S", 'A' + t->sel / 16, t->sel % 16 + 1, a, (sp->end - sp->start) / (double)SR);
    }
    if (is(key, "ed_pad_txt")) {
        if (!sp->used) return put(b, n, "PAD %c%d IS EMPTY: < PAD / PAD > OR HIT A CHOPPED PAD", 'A' + t->sel / 16, t->sel % 16 + 1);
        return put(b, n, "PAD %c%d%s   %s VIEW", 'A' + t->sel / 16, t->sel % 16 + 1, sp->s ? " (OWN SAMPLE)" : "", t->zoom ? "PAD" : "SONG");
    }
    if (is(key, "ed_pos_txt")) {
        if (!sp->used) return put(b, n, " ");
        char a[24], c[24];
        fmt_time(a, sizeof a, sp->start / (double)SR, 1);
        fmt_time(c, sizeof c, sp->end / (double)SR, 1);
        return put(b, n, "%s  -  %s   (%.3f S)", a, c, (sp->end - sp->start) / (double)SR);
    }
    if (is(key, "ed_start")) { wave_check(t); return put(b, n, "%.5f", sp->used ? clampf((float)((sp->start - t->win0) / (t->win1 - t->win0)), 0, 1) : 0.0f); }
    if (is(key, "ed_end")) { wave_check(t); return put(b, n, "%.5f", sp->used ? clampf((float)((sp->end - t->win0) / (t->win1 - t->win0)), 0, 1) : 1.0f); }
    if (is(key, "ed_a")) return put(b, n, "%.4f", sp->used ? ms_to_slider(sp->att, A_MAX) : 0.0f);
    if (is(key, "ed_d")) return put(b, n, "%.4f", sp->used ? ms_to_slider(sp->dec, D_MAX) : 0.3f);
    if (is(key, "ed_s")) return put(b, n, "%.4f", sp->used ? sp->sus : 1.0f);
    if (is(key, "ed_r")) return put(b, n, "%.4f", sp->used ? ms_to_slider(sp->rel, R_MAX) : 0.12f);
    if (is(key, "ed_a_txt")) return put(b, n, "A %d MS", sp->used ? sp->att : 0);
    if (is(key, "ed_d_txt")) return put(b, n, "D %d MS", sp->used ? sp->dec : 0);
    if (is(key, "ed_s_txt")) return put(b, n, "S %d %%", sp->used ? (int)lround(sp->sus * 100) : 100);
    if (is(key, "ed_r_txt")) return put(b, n, "R %d MS", sp->used ? sp->rel : 0);
    if (is(key, "grp_txt")) return sp->used && sp->grp ? put(b, n, "GROUP %d", sp->grp) : put(b, n, "NO GROUP");
    if (is(key, "ops_txt")) {
        if (t->ops[0]) return put(b, n, "%s", t->ops);
        if (t->learn == 1) return put(b, n, "LEARN: HIT THE FIRST PAD");
        if (t->learn == 2) return put(b, n, "LEARN: NOW THE LAST PAD (16)");
        return put(b, n, "PADS: NOTES %d-%d", t->map_first, t->map_last);
    }
    if (is(key, "hint")) {
        const char *h = t->chop_by == CH_LAZY ? (t->assign ? "LAZY: PLAY THE SONG, THE PAD YOU HIT GETS THE CHOP RIGHT THERE"
                                                           : "LAZY: PLAY THE SONG, EVERY PAD HIT (OR TAP) CHOPS TO THE NEXT PAD")
                      : t->chop_by == CH_HITS ? "HITS: SET THE CUE, THEN AUTO CHOP FINDS 16 DRUM HITS FROM THERE"
                                              : "BEATS: SET THE CUE ON A DOWNBEAT, THEN AUTO CHOP CUTS EVEN SLICES";
        return put(b, n, "%s", h);
    }
    if (is(key, "hint_iso")) {
        static const char *H[NISO] = {"THE SONG AS IT IS", "CENTRE (VOCALS) REMOVED, BASS KEPT: GOOD FOR INSTRUMENTAL LOOPS",
                                      "THE MIDDLE OF THE CENTRE: WHERE VOCALS AND LEADS SIT", "ONLY THE LOW END: BASSLINES AND KICKS",
                                      "LOWS AND HIGHS: KICKS AND HATS, LESS MUSIC", "LOW END CUT: CHOPS WITHOUT THE OLD BASS",
                                      "ONLY THE TOP: HATS, AIR, SHAKERS"};
        return put(b, n, "%s", H[clampi(t->iso, 0, NISO - 1)]);
    }
    if (is(key, "cue_txt")) { char a[24]; fmt_time(a, sizeof a, t->cue / (double)SR, 1); return put(b, n, "CUE %s", a); }
    if (is(key, "ytdlp_txt")) { pthread_mutex_lock(&t->m); int k = put(b, n, "YT-DLP %s", t->ytdlp_ver); pthread_mutex_unlock(&t->m); return k; }
    if (is(key, "version")) return put(b, n, "V%s", VERSION);
    /* values */
    if (is(key, "kb")) return put(b, n, "%d", t->kb);
    if (is(key, "find")) return put(b, n, "%d", t->find);
    if (is(key, "vol")) return put(b, n, "%.3f", t->vol);
    if (is(key, "padvol")) return put(b, n, "%.3f", t->padvol);
    if (is(key, "loop")) return put(b, n, "%d", t->loop);
    if (is(key, "deck_pitch")) return put(b, n, "%.4f", t->dpitch / 1000.0);
    if (is(key, "prange")) return put(b, n, "%d", t->prange);
    if (is(key, "klock")) return put(b, n, "%d", t->klock);
    if (is(key, "jog")) return put(b, n, "%.4f", t->jogv / 1000.0);
    if (is(key, "chop_by")) return put(b, n, "%d", t->chop_by);
    if (is(key, "assign")) return put(b, n, "%d", t->assign);
    if (is(key, "arm")) return put(b, n, "%d", t->arm);
    if (is(key, "sens")) return put(b, n, "%.3f", t->sens);
    if (is(key, "bpm")) return put(b, n, "%d", t->bpm);
    if (is(key, "slice")) return put(b, n, "%d", t->slice);
    if (is(key, "zoom")) return put(b, n, "%d", t->zoom);
    if (is(key, "iso")) return put(b, n, "%d", t->iso);
    if (is(key, "velo")) return put(b, n, "%d", t->vel_on);
    if (is(key, "pitch")) return put(b, n, "%d", sp->used ? sp->pitch : 0);
    if (is(key, "level")) return put(b, n, "%.3f", sp->used ? sp->level : 1.0f);
    if (is(key, "pmode")) return put(b, n, "%d", sp->used ? sp->mode : 0);
    if (is(key, "rev")) return put(b, n, "%d", sp->used ? sp->rev : 0);
    if (is(key, "iso_pad")) return put(b, n, "%d", sp->used ? sp->iso : 0);
    (void)mode_names;
    return put(b, n, "0");   /* triggers read back as 0 */
}

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL, song_pos};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

#ifdef TR_TEST_HOOKS   /* test/tr_test.c reaches inside */
inst_t *tr_inst(void *p) { return p; }
#endif
