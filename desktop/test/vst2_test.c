/* CRATE RIPPA Desktop: VST2 test with its own, independently typed description of the VST 2.4 ABI (not the headers the
 * plug-in was built with), so a wrong number in vst2abi/ shows up here. Loads the .so like a host (MPC 2 does the same with the
 * Windows .dll): magic, flags, IDs, names, category, MIDI, processing, chunk save/load. Usage: cr_vst2_test <plugin.so> */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fx Fx;
typedef intptr_t (*HostCb) (Fx*, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt);
struct Fx {
    int32_t magic;
    intptr_t (*dispatch) (Fx*, int32_t, int32_t, intptr_t, void*, float);
    void (*oldProcess) (Fx*, float**, float**, int32_t);
    void (*setParam) (Fx*, int32_t, float);
    float (*getParam) (Fx*, int32_t);
    int32_t programs, params, ins, outs, flags;
    intptr_t r1, r2;
    int32_t delay, rq, oq;
    float ratio;
    void* obj;
    void* usr;
    int32_t id, ver;
    void (*process) (Fx*, float**, float**, int32_t);
    void (*processD) (Fx*, double**, double**, int32_t);
    char fut[56];
};
typedef struct { int32_t type, size, delta, flags, len, off; char data[4]; char det, offvel, r1, r2; } MidiEv;
typedef struct { int32_t n; intptr_t res; void* ev[2]; } Events;
typedef struct { double samplePos, sampleRate, nanos, ppq, tempo, bar, c0, c1; int32_t num, den, smpte, rate, clk, flags; } TimeInfo;

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (! (c)) { fails++; printf ("FAIL %d  ", __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)
static TimeInfo ti;
static intptr_t host (Fx* f, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    (void) f; (void) idx; (void) val; (void) opt;
    if (op == 1) return 2400;                              /* audioMasterVersion */
    if (op == 7) { ti.sampleRate = 48000; ti.tempo = 95; ti.flags = (1 << 9) | (1 << 10); return (intptr_t) &ti; }   /* GetTime */
    if (op == 32) { strcpy ((char*) ptr, "TestHost"); return 1; }   /* vendor */
    if (op == 33) { strcpy ((char*) ptr, "cr_vst2_test"); return 1; }
    if (op == 37) return 0;                                /* canDo */
    return 0;
}
#define CC(a, b, c, d) (((int32_t) (a) << 24) | ((int32_t) (b) << 16) | ((int32_t) (c) << 8) | (int32_t) (d))

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    void* h = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    CHECK (h != NULL, "dlopen: %s", dlerror());
    if (! h) return 1;
    Fx* (*entry) (HostCb) = (Fx* (*) (HostCb)) dlsym (h, "VSTPluginMain");
    if (! entry) entry = (Fx* (*) (HostCb)) dlsym (h, "main");
    CHECK (entry != NULL, "entry point");
    Fx* fx = entry (host);
    CHECK (fx && fx->magic == CC ('V', 's', 't', 'P'), "magic");
    if (! fx) return 1;
    CHECK (sizeof (Fx) == (sizeof (void*) == 8 ? 192 : 144), "struct size %d", (int) sizeof (Fx));
    CHECK (fx->id == CC ('J', 'J', 'c', 'R'), "unique id %x", fx->id);
    CHECK (fx->outs == 2 && fx->ins == 0, "outs %d ins %d", fx->outs, fx->ins);
    CHECK (fx->flags & (1 << 8), "IsSynth");
    CHECK (fx->flags & (1 << 4), "CanReplacing");
    CHECK (fx->flags & (1 << 5), "ProgramChunks");
    CHECK (fx->flags & 1, "HasEditor");
    CHECK (fx->params == 4, "params %d", fx->params);
    CHECK (fx->process != NULL, "processReplacing");
    fx->dispatch (fx, 0, 0, 0, NULL, 0);                   /* effOpen */
    char s[256] = "";
    fx->dispatch (fx, 45, 0, 0, s, 0);                     /* effGetEffectName */
    CHECK (strcmp (s, "Crate Rippa") == 0, "name '%s'", s);
    s[0] = 0; fx->dispatch (fx, 47, 0, 0, s, 0);           /* effGetVendorString */
    CHECK (strcmp (s, "JJ DA PLUG") == 0, "vendor '%s'", s);
    s[0] = 0; fx->dispatch (fx, 48, 0, 0, s, 0);           /* effGetProductString */
    CHECK (strcmp (s, "Crate Rippa") == 0, "product '%s'", s);
    CHECK (fx->dispatch (fx, 35, 0, 0, NULL, 0) == 2, "category synth (%d)", (int) fx->dispatch (fx, 35, 0, 0, NULL, 0));   /* effGetPlugCategory */
    CHECK (fx->dispatch (fx, 58, 0, 0, NULL, 0) == 2400, "VST version");   /* effGetVstVersion */
    CHECK (fx->dispatch (fx, 51, 0, 0, (void*) "receiveVstMidiEvent", 0) == 1, "canDo receiveVstMidiEvent");   /* effCanDo */
    CHECK (fx->dispatch (fx, 51, 0, 0, (void*) "receiveVstEvents", 0) == 1, "canDo receiveVstEvents");
    s[0] = 0; fx->dispatch (fx, 8, 0, 0, s, 0);            /* effGetParamName */
    CHECK (strncmp (s, "Song", 4) == 0, "param 0 name '%s'", s);
    fx->setParam (fx, 0, 0.25f);
    CHECK (fabsf (fx->getParam (fx, 0) - 0.25f) < 1e-4f, "param set/get %f", fx->getParam (fx, 0));
    s[0] = 0; fx->dispatch (fx, 7, 0, 0, s, 0);            /* effGetParamDisplay */
    CHECK (s[0] != 0, "param display '%s'", s);
    fx->dispatch (fx, 10, 0, 0, NULL, 48000.0f);           /* effSetSampleRate */
    fx->dispatch (fx, 11, 0, 256, NULL, 0);                /* effSetBlockSize */
    fx->dispatch (fx, 12, 0, 1, NULL, 0);                  /* effMainsChanged on */
    float l[256], r[256], *outs[2] = { l, r };
    MidiEv ev; memset (&ev, 0, sizeof ev);
    ev.type = 1; ev.size = sizeof ev; ev.delta = 10; ev.data[0] = (char) 0x90; ev.data[1] = 36; ev.data[2] = 100;
    Events evs; memset (&evs, 0, sizeof evs);
    evs.n = 1; evs.ev[0] = &ev;
    CHECK (fx->dispatch (fx, 25, 0, 0, &evs, 0) == 1, "effProcessEvents");
    int ok = 1;
    for (int k = 0; k < 40; k++)
    {
        memset (l, 0x7f, sizeof l);
        fx->process (fx, NULL, outs, 256);
        for (int i = 0; i < 256; i++) if (! isfinite (l[i]) || fabsf (l[i]) > 1.0f) ok = 0;
    }
    CHECK (ok, "processReplacing writes clean output (no song: silence)");
    void* chunk = NULL;
    intptr_t n = fx->dispatch (fx, 23, 0, 0, &chunk, 0);   /* effGetChunk */
    CHECK (n > 50 && chunk != NULL, "chunk %d bytes", (int) n);
    char* copy = malloc ((size_t) n);
    memcpy (copy, chunk, (size_t) n);
    fx->setParam (fx, 0, 0.9f);
    fx->dispatch (fx, 24, 0, n, copy, 0);                  /* effSetChunk */
    CHECK (fabsf (fx->getParam (fx, 0) - 0.25f) < 1e-3f, "chunk restores the parameter (%f)", fx->getParam (fx, 0));
    free (copy);
    short* rect = NULL;
    fx->dispatch (fx, 13, 0, 0, &rect, 0);                 /* effEditGetRect */
    CHECK (rect && rect[2] - rect[0] >= 400 && rect[3] - rect[1] >= 600, "editor size %dx%d", rect ? rect[3] - rect[1] : -1, rect ? rect[2] - rect[0] : -1);
    fx->dispatch (fx, 12, 0, 0, NULL, 0);                  /* mains off */
    fx->dispatch (fx, 1, 0, 0, NULL, 0);                   /* effClose */
    printf ("%s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails ? 1 : 0;
}
