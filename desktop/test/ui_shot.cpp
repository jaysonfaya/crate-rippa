/* CRATE RIPPA Desktop: renders every tab to a PNG (no window needed), with a song from the fake yt-dlp, chops and crates. */
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File out (argc > 1 ? argv[1] : "/tmp/cr_shots");
    out.createDirectory();
    auto proc = std::make_unique<CrateRippaProcessor>();
    proc->prepareToPlay (48000, 512);
    auto& e = proc->engine();
    auto& cs = proc->crates();
    e.setQuery ("j dilla donuts");
    e.search (false);
    for (int k = 0; k < 500 && e.view().results.size() < 10; k++) juce::Thread::sleep (10);
    e.tapResult (2);
    for (int k = 0; k < 1000 && e.songState() != cr::ST_READY; k++) juce::Thread::sleep (10);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    for (int k = 0; k < 200; k++) proc->processBlock (b, m);   /* ~2 s into the song */
    e.setChopBy (cr::CH_HITS);
    e.autoChop();
    e.selectPad (5);
    e.setPitch (-3); e.groupStep (1); e.setAttack (12); e.setRelease (220); e.setSustain (0.7f);
    auto v = e.view();
    for (int i = 0; i < 6; i++)
    {
        cr::CrateSong s; s.id = v.results[(size_t) i].id; s.title = v.results[(size_t) i].title; s.chan = v.results[(size_t) i].chan; s.secs = v.results[(size_t) i].secs;
        if (i % 2 == 0) cs.toggleFav (s);
        s.bpm = 88 + i * 3; s.key = i % 2 ? "A MIN" : "F MAJ"; s.tags = i % 2 ? "soul, keys" : "drums, break";
        static int crate = -1;
        if (crate < 0) crate = cs.create ("SOUL LOOPS");
        cs.add (crate, s);
    }
    cs.create ("BREAKS 70s");
    auto* ed = dynamic_cast<CrateRippaEditor*> (proc->createEditor());
    const char* names[] = { "player", "chop", "wave", "isolate", "crate", "info" };
    int only = argc > 2 ? atoi (argv[2]) : -1;
    for (int t = 0; t < 6; t++)
    {
        if (only >= 0 && t != only) continue;
        ed->showTab (t);

        for (int k = 0; k < 3; k++) { proc->processBlock (b, m); ed->tick(); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile (juce::String (t) + "_" + names[t] + ".png"));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (img, os);
    }
    delete ed;
    printf ("shots in %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
