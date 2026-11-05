/* CRATE RIPPA Desktop: engine test. Runs the real engine against a fake yt-dlp (test/fake_ytdlp.py) and a synthetic song
 * (kicks at 100 BPM): search, playlists, download, decode, play at 48 kHz, scratch, key lock, HITS / BEATS / LAZY, voices,
 * mute groups, undo, pad and bank actions, LEARN, export, drag bounce, freeze on a new song, state round trip, crates. */
#include "CrEngine.h"
#include "CrateStore.h"
#include "Tools.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace cr;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (! (c)) { fails++; printf ("FAIL %s:%d  ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static bool waitFor (const std::function<bool()>& f, double secs)
{
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (! f())
    {
        if (juce::Time::getMillisecondCounterHiRes() - t0 > secs * 1000) return false;
        juce::Thread::sleep (10);
    }
    return true;
}

static double renderRms (Engine& e, int frames, HostInfo h = {})
{
    std::vector<float> L ((size_t) frames), R ((size_t) frames);
    for (int at = 0; at < frames; at += 512) e.render (L.data() + at, R.data() + at, std::min (512, frames - at), h);
    double s = 0;
    bool finite = true;
    for (int i = 0; i < frames; i++) { s += L[(size_t) i] * L[(size_t) i] + R[(size_t) i] * R[(size_t) i]; finite &= std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]); }
    if (! finite) return -1;
    return std::sqrt (s / (2.0 * frames));
}

static void makeSong (const juce::File& f, double secs)
{
    const int n = (int) (secs * SR);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    const double beat = 0.6;   // 100 BPM
    juce::Random rnd (7);
    for (int i = 0; i < n; i++)
    {
        double t = (double) i / SR, tb = std::fmod (t, beat), te = std::fmod (t, beat / 2);
        float kick = (float) (0.8 * std::exp (-tb * 18) * std::sin (2 * M_PI * (55 + 80 * std::exp (-tb * 30)) * tb));
        float hat = (float) (0.08 * std::exp (-te * 60) * (rnd.nextFloat() * 2 - 1));
        float padL = (float) (0.05 * std::sin (2 * M_PI * 220 * t)), padR = (float) (0.05 * std::sin (2 * M_PI * 330 * t));
        b.setSample (0, i, kick + hat + padL);
        b.setSample (1, i, kick + hat + padR);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (16));
    w->writeFromAudioSampleBuffer (b, 0, n);
}

int main (int argc, char** argv)
{
    juce::ignoreUnused (argc);
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cr_engine_test");
    root.deleteRecursively();
    root.createDirectory();
    auto song = root.getChildFile ("song.wav");
    makeSong (song, 30.0);
    auto here = juce::File::getCurrentWorkingDirectory();
    juce::String fake = argc > 1 ? juce::String (argv[1]) : here.getChildFile ("test/fake_ytdlp.py").getFullPathName();
    setenv ("CRATE_YTDLP", fake.toRawUTF8(), 1);
    setenv ("FAKE_SONG", song.getFullPathName().toRawUTF8(), 1);
    setenv ("FAKE_LOG", root.getChildFile ("ytdlp.log").getFullPathName().toRawUTF8(), 1);
    const char* ff = getenv ("CR_TEST_FFMPEG");
    setenv ("CRATE_FFMPEG", ff ? ff : "none", 1);
    auto data = root.getChildFile ("data");
    {
        Engine e (data);
        e.prepare (48000.0);
        /* ---- search */
        e.setQuery ("boom bap");
        e.search (false);
        CHECK (waitFor ([&] { return e.view().results.size() == 40; }, 20), "search results: %d", (int) e.view().results.size());
        auto v = e.view();
        CHECK (v.ytdlpVer == "2026.10.31-fake", "yt-dlp version '%s'", v.ytdlpVer.c_str());
        CHECK (v.results.size() > 1 && v.results[1].id != "LIVE0000000", "live stream filtered");
        CHECK (v.resHead.find ("40 SONGS FOR \"BOOM BAP\"") != std::string::npos, "head '%s'", v.resHead.c_str());
        /* ---- playlists */
        e.search (true);
        CHECK (waitFor ([&] { auto w = e.view(); return w.resKind == 1 && w.results.size() == 4; }, 20), "playlists: %d", (int) e.view().results.size());
        v = e.view();
        CHECK (v.results[0].id == "PLlist00" && v.results[0].kind == 1 && v.results[0].count == "3", "playlist id '%s' count '%s'", v.results[0].id.c_str(), v.results[0].count.c_str());
        e.tapResult (0);
        CHECK (waitFor ([&] { auto w = e.view(); return w.resKind == 0 && w.results.size() == 3; }, 20), "playlist opened: %d", (int) e.view().results.size());
        v = e.view();
        CHECK (v.lastPlaylistId == "PLlist00" && v.lastPlaylistTitle == "List 0", "last playlist '%s'", v.lastPlaylistId.c_str());
        /* ---- a broken video gives a clear message */
        e.loadSong ("BADBADBAD00", "bad", 10, true);
        CHECK (waitFor ([&] { return e.songState() == ST_ERROR; }, 20), "bad video -> error");
        CHECK (e.statusText() == "VIDEO REMOVED", "status '%s'", e.statusText().c_str());
        /* ---- download, decode, play */
        e.setQuery ("drums");
        e.search (false);
        CHECK (waitFor ([&] { return e.view().results.size() == 40; }, 20), "search 2");
        e.tapResult (0);
        CHECK (waitFor ([&] { return e.songState() == ST_READY; }, 30), "song ready (state %d, '%s')", e.songState(), e.statusText().c_str());
        CHECK (std::abs (e.availFrames() - 30 * SR) < 64, "frames %d", e.availFrames());
        CHECK (e.cacheFileFor ("SONG0000000").existsAsFile(), "cached file");
        CHECK (waitFor ([&] { return data.getChildFile ("history.txt").loadFileAsString().startsWith ("SONG0000000\t30\tdrums song 0"); }, 5), "history");
        v = e.view();
        CHECK (v.playing, "autoplay");
        double rms = renderRms (e, 48000);
        CHECK (rms > 0.05, "song plays, rms %.3f", rms);
        v = e.view();
        CHECK (std::abs (v.ppos - 44100) < 300, "1 s of output moves 1 s of song at 48 kHz: %.0f", v.ppos);
        CHECK (v.angle > 0.5 && v.angle < 0.6, "platter turned %.3f turns (33 1/3 rpm)", v.angle);
        /* ---- pitch fader, key lock */
        e.setRange (0);
        e.setDeckPitch (1.0f);   /* +8 % */
        double p0 = e.view().ppos;
        renderRms (e, 48000);
        CHECK (std::abs ((e.view().ppos - p0) - 44100 * 1.08) < 400, "+8%% speed: %.0f", e.view().ppos - p0);
        e.setKeyLock (true);
        p0 = e.view().ppos;
        rms = renderRms (e, 48000);
        CHECK (rms > 0.03 && std::abs ((e.view().ppos - p0) - 44100 * 1.08) < 1500, "key lock tempo %.0f rms %.3f", e.view().ppos - p0, rms);
        e.setKeyLock (false);
        e.setDeckPitch (0.5f);
        e.setRange (3);
        /* ---- scratch: the record follows the mouse, stands still under a still finger */
        p0 = e.view().ppos;
        e.scratchTouch (true);
        e.scratchMove (-0.5);   /* half a turn back = 0.9 s */
        renderRms (e, 9600);
        CHECK (std::abs (e.view().ppos - (p0 - 0.9 * SR)) < 300, "scratch back: %.0f vs %.0f", e.view().ppos, p0 - 0.9 * SR);
        double held = e.view().ppos;
        renderRms (e, 9600);
        CHECK (std::abs (e.view().ppos - held) < 50, "finger holds the record");
        e.scratchTouch (false);
        renderRms (e, 4800);
        CHECK (e.view().ppos > held + 3000, "plays on after the scratch");
        /* ---- isolation filters: every mode finite and audible */
        for (int i = 0; i < NISO; i++)
        {
            e.setIso (i);
            rms = renderRms (e, 9600);
            CHECK (rms > 0.001 && rms < 2, "iso %d rms %.4f", i, rms);
        }
        e.setIso (0);
        /* ---- HITS: 16 kicks from the cue, 0.6 s apart */
        e.stop();
        e.setCue();
        e.setChopBy (CH_HITS);
        e.autoChop();
        v = e.view();
        int used = 0;
        for (int i = 0; i < 16; i++) used += v.pads[i].used;
        CHECK (used == 16, "HITS found %d", used);
        bool onBeat = true;
        for (int i = 0; i < used; i++)
        {
            double t = v.pads[i].start / (double) SR, d = std::fmod (t + 0.05, 0.3) - 0.05;
            if (std::abs (d) > 0.03) onBeat = false;
        }
        CHECK (onBeat, "hits sit on the kicks/hats");
        /* ---- tempo */
        e.detectBpm();
        CHECK (e.view().bpm == 100, "detected %d BPM", e.view().bpm);
        /* ---- BEATS on bank B */
        e.selectBank (1);
        e.setChopBy (CH_BEATS);
        e.setSlice (1);
        e.autoChop();
        v = e.view();
        CHECK (v.pads[16].used && v.pads[31].used && std::abs (v.pads[17].start - (int) (0.6 * SR)) < 4, "BEATS slices: %d", v.pads[17].start);
        /* ---- voices: a pad sounds, ADSR, mute group cuts, gate releases */
        e.selectBank (0);
        e.padHit (0, 1.0f, true);
        rms = renderRms (e, 4800);
        CHECK (rms > 0.02, "pad A1 sounds %.3f", rms);
        CHECK (e.view().pads[0].sounding, "A1 sounding");
        e.selectPad (1); e.groupStep (1);
        e.selectPad (2); e.groupStep (1);
        e.padHit (1, 1.0f, true);
        renderRms (e, 480);
        e.padHit (2, 1.0f, true);
        renderRms (e, 9600);
        CHECK (! e.view().pads[1].sounding && e.view().pads[2].sounding, "mute group 1 cut A2");
        e.stopAll();
        renderRms (e, 9600);
        bool any = false;
        for (auto& p : e.view().pads) any |= p.sounding;
        CHECK (! any, "STOP ALL silences everything");
        /* ---- undo, pad ops, bank ops */
        e.selectPad (3);
        e.clearPad();
        CHECK (! e.view().pads[3].used, "clear pad");
        e.undo();
        CHECK (e.view().pads[3].used, "undo clear");
        e.selectPad (0);
        e.padOp (1);
        e.padHit (40, 1, true);   /* copy A1 -> C9 */
        CHECK (e.view().pads[40].used && e.view().pads[40].start == e.view().pads[0].start, "copy A1 -> C9");
        e.selectPad (40);
        e.padOp (2);
        e.padHit (47, 1, true);   /* move C9 -> C16 */
        CHECK (! e.view().pads[40].used && e.view().pads[47].used, "move");
        e.selectBank (1);
        e.bankOp (3);
        e.selectBank (3);         /* swap B <-> D */
        CHECK (! e.view().pads[16].used && e.view().pads[48].used, "bank swap");
        e.undo();
        CHECK (e.view().pads[16].used && ! e.view().pads[48].used, "undo bank swap");
        /* ---- LAZY: hits while playing chop at the playhead */
        e.selectBank (3);
        e.setChopBy (CH_LAZY);
        e.goCue();
        e.playToggle();
        renderRms (e, 24000);
        e.padHit (50, 1, false);
        renderRms (e, 24000);
        e.tapChop();
        v = e.view();
        CHECK (v.pads[48].used && v.pads[49].used, "lazy chops on D1, D2");
        CHECK (v.pads[48].end == v.pads[49].start, "auto end follows the next chop");
        e.stop();
        /* ---- MIDI: LEARN a scale layout and play */
        e.learnToggle();
        e.midiNoteOn (48, 1);
        e.midiNoteOn (74, 1);   /* 16 notes of a major scale span 26 semitones */
        v = e.view();
        CHECK (v.mapFirst == 48 && v.mapLast == 74 && v.learn == 0, "learned %d..%d", v.mapFirst, v.mapLast);
        e.selectBank (0);
        e.midiNoteOn (50, 1);   /* 2nd scale note -> pad 2 */
        CHECK (e.view().sel == 1, "MIDI note 50 -> A2 (sel %d)", e.view().sel);
        e.midiNoteOff (50);
        /* ---- WAVE editing */
        e.selectPad (0);
        e.setZoom (1);
        v = e.view();
        int s0 = v.pads[0].start;
        e.moveBounds (0, 50);
        CHECK (e.view().pads[0].start == s0 + SR / 20, "start +50 ms");
        e.setStartFrac (0.0);
        CHECK (std::abs (e.view().pads[0].start - (int) e.view().win0) < 2, "start slider to the window's left");
        e.setPitch (-12); e.setAttack (100); e.setDecay (500); e.setSustain (0.5f); e.setRelease (300); e.setReverse (true); e.setMode (1);
        v = e.view();
        CHECK (v.pads[0].pitch == -12 && v.pads[0].att == 100 && v.pads[0].rev && v.pads[0].mode == 1 && std::abs (v.pads[0].sus - 0.5f) < 1e-6, "pad params");
        e.playSelected();
        rms = renderRms (e, 4800);
        CHECK (rms > 0.0005 && rms < 1, "reversed, pitched pad plays %.4f", rms);
        /* ---- drag a chop into the DAW: a WAV */
        auto dragF = root.getChildFile ("drag A1.wav");
        CHECK (e.bouncePadToFile (0, dragF) && dragF.getSize() > 10000, "bounce for drag");
        /* ---- export */
        e.exportPads();
        CHECK (waitFor ([&] { return ! e.view().exporting; }, 20), "export finished");
        auto ex = data.getChildFile ("Exports").getChildFile ("drums song 0");
        int wavs = ex.getNumberOfChildFiles (juce::File::findFiles, "*.wav");
        CHECK (wavs >= 34, "exported %d wavs to %s", wavs, ex.getFullPathName().toRawUTF8());
        CHECK (ex.getChildFile ("A01 drums song 0.wav").existsAsFile() && ex.getChildFile ("D02 drums song 0.wav").existsAsFile(), "names A01/D02");
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (ex.getChildFile ("A01 drums song 0.wav")));
            CHECK (r && r->sampleRate == 44100 && r->numChannels == 2 && r->bitsPerSample == 16, "export format");
            /* A1: pitch -12 -> twice as long as the chop */
            int len = v.pads[0].end - v.pads[0].start;
            CHECK (r && std::abs ((int) r->lengthInSamples - 2 * len) < 8, "A1 bounced at -12: %d vs %d", r ? (int) r->lengthInSamples : -1, 2 * len);
        }
        e.exportPads();
        CHECK (waitFor ([&] { return ! e.view().exporting; }, 20) && data.getChildFile ("Exports").getChildFile ("drums song 0 2").isDirectory(), "second export never overwrites");
        /* ---- state round trip */
        std::string st = e.getState();
        CHECK (st.rfind ("CR1\nsong=SONG0000000|30|drums song 0", 0) == 0, "state head: %s", st.substr (0, 60).c_str());
        /* ---- a new song: the old chops get their own audio */
        e.setQuery ("next");
        e.search (false);
        CHECK (waitFor ([&] { return e.view().results.size() == 40; }, 20), "search 3");
        e.tapResult (3);
        CHECK (waitFor ([&] { return e.songState() == ST_READY && e.view().songId == "SONG0000003"; }, 30), "second song");
        v = e.view();
        CHECK (v.pads[0].used && v.pads[0].own && v.pads[16].own, "pads frozen");
        CHECK (waitFor ([&] { return data.getChildFile ("pads").getNumberOfChildFiles (juce::File::findFiles, "*.raw") >= 30; }, 10), "frozen audio on disk");
        e.stop();   /* while the song plays, LAZY pads chop: stop first */
        e.padHit (0, 1, true);
        CHECK (renderRms (e, 4800) > 0.0005, "frozen pad plays");
        std::string st2 = e.getState();
        Pad a1 = e.padCopy (0);
        {   /* a fresh instance loads the project: same pads, own samples from disk, song from the cache */
            Engine e2 (data);
            e2.prepare (44100);
            e2.setState (st2);
            CHECK (waitFor ([&] { return e2.songState() == ST_READY; }, 20), "project song from the cache");
            Pad b1 = e2.padCopy (0);
            CHECK (b1.used && b1.s && a1.s && b1.s->len == a1.s->len && b1.start == a1.start && b1.pitch == -12 && b1.rev, "pad A1 restored: used %d s %p/%p len %d/%d start %d/%d pitch %d rev %d",
                   (int) b1.used, (void*) b1.s.get(), (void*) a1.s.get(), b1.s ? b1.s->len : -1, a1.s ? a1.s->len : -1, b1.start, a1.start, b1.pitch, (int) b1.rev);
            CHECK (e2.getState() == st2, "state identical after the round trip");
            CHECK (e2.statusText() == "LOADED FROM THE CACHE: PLAY, THEN CHOP", "'%s'", e2.statusText().c_str());
            /* the yt-dlp log: the second instance did not download again */
        }
        e.updateYtDlp();
        CHECK (waitFor ([&] { return e.statusText().find ("UP TO DATE") != std::string::npos; }, 20), "update: '%s'", e.statusText().c_str());
        e.showHistory();
        v = e.view();
        CHECK (v.resKind == 2 && v.results.size() == 2 && v.results[0].id == "SONG0000003", "history list %d", (int) v.results.size());
    }
    {
        auto log = root.getChildFile ("ytdlp.log").loadFileAsString();
        int downloads = 0;
        for (auto& l : juce::StringArray::fromLines (log)) downloads += l.contains ("after_move:filepath");
        CHECK (downloads == 3, "downloads: %d (bad, song 0, song 3; the project reload used the cache)", downloads);
    }
    /* ---- crates */
    {
        auto cf = root.getChildFile ("crates").getChildFile ("crates.json");
        CrateStore cs (cf);
        CHECK (cs.size() == 1 && cs.all()[0].name == "FAVS", "favs exist");
        CrateSong s1; s1.id = "SONG0000001"; s1.title = "Tïtle ünïcode"; s1.chan = "C"; s1.secs = 61;
        CrateSong s2 = s1; s2.id = "SONG0000002"; s2.title = "two";
        CHECK (cs.toggleFav (s1) && cs.isFav ("SONG0000001"), "fav on");
        int c = cs.create ("Soul Loops");
        CHECK (c == 1 && cs.add (c, s1) && cs.add (c, s2) && ! cs.add (c, s1), "crate add, no duplicates");
        cs.move (c, 1, 0);
        CHECK (cs.all()[1].songs[0].id == "SONG0000002", "reorder");
        cs.setInfo (c, 1, 92, "A MIN", "soul, keys", "drums at 1:20");
        CHECK (cs.all()[0].songs[0].bpm == 92 && cs.all()[0].songs[0].key == "A MIN", "info shared with FAVS");
        int c2 = cs.create ("soul loops");
        CHECK (cs.all()[(size_t) c2].name == "soul loops 2", "unique names: '%s'", cs.all()[(size_t) c2].name.c_str());
        cs.rename (0, "NOPE");
        CHECK (cs.all()[0].name == "FAVS", "FAVS keeps its name");
        auto exf = root.getChildFile ("soul.crate.json");
        CHECK (cs.exportCrate (c, exf), "export crate");
        int ci = cs.importCrate (exf);
        CHECK (ci == 3 && cs.all()[3].songs.size() == 2 && cs.all()[3].songs[1].note == "drums at 1:20", "import crate");
        cs.erase (c2);
        CHECK (cs.size() == 3, "erase");
        CrateStore cs2 (cf);   /* another instance reads the same file */
        CHECK (cs2.size() == 3 && cs2.all()[1].songs[1].title == "Tïtle ünïcode", "persisted (utf-8)");
        cs.toggleFav (s2);
        juce::Thread::sleep (1100);   /* mtime resolution */
        cs.toggleFav (s1);
        cs2.reloadIfChanged();
        CHECK (cs2.isFav ("SONG0000002") && ! cs2.isFav ("SONG0000001"), "other instance sees the change");
        std::vector<CrateSong> pl { s1, s2, s1 };
        int pc = cs.saveAsCrate ("List 0", "youtube:PLlist00", pl);
        CHECK (cs.all()[(size_t) pc].songs.size() == 2 && cs.all()[(size_t) pc].source == "youtube:PLlist00", "playlist saved as a crate");
    }
    /* ---- decoding: a 48 kHz mono file becomes 44.1 kHz stereo; AAC/M4A through ffmpeg when there is one */
    {
        auto f48 = root.getChildFile ("t48.wav");
        {
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < 48000; i++) b.setSample (0, i, 0.5f * (float) std::sin (2 * M_PI * 1000 * i / 48000.0));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f48));
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
            w->writeFromAudioSampleBuffer (b, 0, 48000);
        }
        long long frames = 0; double peak = 0; std::string err;
        bool ok = decodeAudio (f48, {}, [&] (const int16_t* d, int n) { for (int i = 0; i < 2 * n; i++) peak = std::max (peak, std::abs (d[i]) / 32768.0); frames += n; return true; }, {}, nullptr, nullptr, err);
        CHECK (ok && std::abs (frames - 44100) < 64 && peak > 0.45 && peak < 0.55, "48k mono -> 44.1k stereo: %lld frames, peak %.3f", frames, peak);
        if (juce::File ("/usr/bin/ffmpeg").existsAsFile())
        {
            auto m4a = root.getChildFile ("t.m4a");
            juce::ChildProcess cp;
            cp.start (juce::StringArray { "/usr/bin/ffmpeg", "-y", "-loglevel", "quiet", "-i", f48.getFullPathName(), "-c:a", "aac", "-b:a", "128k", m4a.getFullPathName() });
            cp.waitForProcessToFinish (30000);
            frames = 0; err.clear();
            std::mutex mx; std::shared_ptr<juce::ChildProcess> slot;
            ok = decodeAudio (m4a, juce::File ("/usr/bin/ffmpeg"), [&] (const int16_t*, int n) { frames += n; return true; }, {}, &slot, &mx, err);
            CHECK (ok && std::abs (frames - 44100) < 3000, "m4a via ffmpeg: %lld frames %s", frames, err.c_str());
            frames = 0; err.clear();
            ok = decodeAudio (m4a, {}, [&] (const int16_t*, int n) { frames += n; return true; }, {}, nullptr, nullptr, err);
            CHECK (! ok && err.find ("FFMPEG") != std::string::npos, "m4a without ffmpeg on Linux: a clear message '%s'", err.c_str());
        }
    }
    printf ("%s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails ? 1 : 0;
}
