/* CRATE RIPPA Desktop: host test. Loads the built VST3 like a DAW does and checks: description, parameters, three sample
 * rates and block sizes, a project (state) that brings back a song from the cache and a chop, MIDI note 36 plays pad A1 on
 * its sample, transport stop silences it, state round trip. Usage: cr_host_test <path of Crate Rippa.vst3> <song.wav> */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (! (c)) { fails++; printf ("FAIL %d  ", __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

struct PlayHead : juce::AudioPlayHead
{
    bool playing = false;
    double ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples (0);
        p.setIsPlaying (playing);
        p.setPpqPosition (ppq);
        p.setBpm (93.0);
        return p;
    }
};

/* the hosted VST3's state wraps the plug-in's own bytes: <VST3PluginState><IComponent>base64</IComponent></VST3PluginState> */
static juce::MemoryBlock wrap (const juce::MemoryBlock& inner)
{
    juce::XmlElement x ("VST3PluginState");
    x.createNewChildElement ("IComponent")->addTextElement (inner.toBase64Encoding());
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (x, mb);
    return mb;
}
static std::unique_ptr<juce::XmlElement> unwrap (const juce::MemoryBlock& outer)
{
    auto x = juce::AudioProcessor::getXmlFromBinary (outer.getData(), (int) outer.getSize());
    if (! x || ! x->getChildByName ("IComponent")) return {};
    juce::MemoryBlock in;
    in.fromBase64Encoding (x->getChildByName ("IComponent")->getAllSubText());
    return juce::AudioProcessor::getXmlFromBinary (in.getData(), (int) in.getSize());
}

static double rms (const juce::AudioBuffer<float>& b, int from, int n)
{
    double s = 0;
    for (int c = 0; c < b.getNumChannels(); c++)
        for (int i = from; i < from + n; i++) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return std::sqrt (s / (b.getNumChannels() * (double) n));
}

int main (int argc, char** argv)
{
    if (argc < 3) { printf ("usage: cr_host_test <plugin.vst3> <song.wav>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cr_host_test");
    root.deleteRecursively();
    root.getChildFile ("cache").createDirectory();
    juce::File (argv[2]).copyFileTo (root.getChildFile ("cache").getChildFile ("TESTSONG001.wav"));
    setenv ("CRATE_RIPPA_DIR", root.getFullPathName().toRawUTF8(), 1);
    setenv ("CRATE_FFMPEG", "none", 1);

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, argv[1]);
    CHECK (descs.size() == 1, "%d descriptions", descs.size());
    if (descs.isEmpty()) return 1;
    auto& d = *descs[0];
    CHECK (d.name == "Crate Rippa" && d.manufacturerName == "JJ DA PLUG" && d.isInstrument, "description: %s / %s / %d", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), (int) d.isInstrument);
    CHECK (d.category.contains ("Instrument"), "category %s", d.category.toRawUTF8());

    const struct { double sr; int bs; } cfgs[] = { { 48000, 512 }, { 96000, 333 }, { 44100, 64 } };
    for (auto cfg : cfgs)
    {
        juce::String err;
        auto inst = fmt.createInstanceFromDescription (d, cfg.sr, cfg.bs, err);
        CHECK (inst != nullptr, "instance: %s", err.toRawUTF8());
        if (! inst) return 1;
        PlayHead ph;
        inst->setPlayHead (&ph);
        CHECK (inst->getTotalNumOutputChannels() == 2 && inst->getTotalNumInputChannels() == 0, "busses %d/%d", inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
        CHECK (inst->acceptsMidi(), "accepts MIDI");
        auto& ps = inst->getParameters();
        juce::StringArray names;
        for (auto* p : ps) names.add (p->getName (40));
        CHECK (names.contains ("Song Mix") && names.contains ("Pad Mix") && names.contains ("Pitch Fader") && names.contains ("Isolate"), "parameters: %s", names.joinIntoString (",").toRawUTF8());
        inst->prepareToPlay (cfg.sr, cfg.bs);

        /* a project: the song from the cache, pad A1 chopped at 0.61 s, pad level 1 */
        juce::ValueTree t ("CrateRippa");
        t.setProperty ("version", 1, nullptr);
        t.setProperty ("engine", "CR1\nsong=TESTSONG001|30|Test Song\nmap=36|51\ndeck=500|3|0\n"
                                 "set=0|0|2|0|1|100|0|36|0|0.500|0.800|0|0|1|0\np=0|26901|39690|0|1.000|0|60|0|0|0|0|300|1.000|0|\n", nullptr);
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (*t.createXml(), mb);
        auto outer = wrap (mb);
        inst->setStateInformation (outer.getData(), (int) outer.getSize());

        juce::AudioBuffer<float> buf (2, cfg.bs);
        juce::MidiBuffer midi;
        bool loaded = false;
        for (int k = 0; k < 400 && ! loaded; k++)   /* the song loads in the background */
        {
            juce::MemoryBlock st;
            inst->getStateInformation (st);
            juce::Thread::sleep (10);
            auto x = unwrap (st);
            loaded = x && x->getStringAttribute ("engine").contains ("song=TESTSONG001");
            buf.clear();
            inst->processBlock (buf, midi);
        }
        juce::Thread::sleep (500);
        /* silence first: nothing plays */
        buf.clear();
        inst->processBlock (buf, midi);
        CHECK (rms (buf, 0, cfg.bs) < 1e-6, "silent before the note");
        /* note 36 at sample 20 -> pad A1 */
        int total = (int) (cfg.sr * 0.25);
        juce::AudioBuffer<float> out (2, total + cfg.bs);
        out.clear();
        int first = -1;
        for (int at = 0; at < total; at += cfg.bs)
        {
            buf.clear();
            midi.clear();
            if (at == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 20);
            inst->processBlock (buf, midi);
            for (int c = 0; c < 2; c++) out.copyFrom (c, at, buf, c, 0, cfg.bs);
        }
        for (int i = 0; i < total && first < 0; i++) if (std::abs (out.getSample (0, i)) > 1e-4f) first = i;
        CHECK (first == 20, "%.0f/%d: the pad starts on its sample: %d", cfg.sr, cfg.bs, first);
        CHECK (rms (out, 0, total) > 0.02, "%.0f/%d: the pad sounds %.4f", cfg.sr, cfg.bs, rms (out, 0, total));
        /* the host stops: every sound ends */
        ph.playing = true;
        buf.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
        inst->processBlock (buf, midi);
        ph.playing = false;
        midi.clear();
        for (int k = 0; k < (int) (cfg.sr * 0.25) / cfg.bs; k++) { buf.clear(); inst->processBlock (buf, midi); }
        buf.clear();
        inst->processBlock (buf, midi);
        CHECK (rms (buf, 0, cfg.bs) < 1e-4, "host stop silences: %.6f", rms (buf, 0, cfg.bs));
        /* parameters reach the engine: pad mix 0 = silent pad */
        for (auto* p : ps) if (p->getName (40) == "Pad Mix") p->setValueNotifyingHost (0.0f);
        buf.clear(); midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
        inst->processBlock (buf, midi);
        CHECK (rms (buf, 0, cfg.bs) < 1e-6, "pad mix 0 mutes the pads");
        for (auto* p : ps) if (p->getName (40) == "Pad Mix") p->setValueNotifyingHost (1.0f);
        /* state round trip */
        juce::MemoryBlock st;
        inst->getStateInformation (st);
        auto x = unwrap (st);
        CHECK (x && x->getStringAttribute ("engine").contains ("p=0|26901|39690|"), "state keeps the chop");
        inst->releaseResources();
    }
    printf ("%s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
    return fails ? 1 : 0;
}
