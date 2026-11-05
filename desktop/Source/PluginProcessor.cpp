/* CRATE RIPPA Desktop (JJ DA PLUG): the plug-in. MIT (JUCE under its own licence). */
#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout CrateRippaProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vol", 1 }, "Song Mix", 0.0f, 1.0f, 0.8f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "padvol", 1 }, "Pad Mix", 0.0f, 1.0f, 1.0f));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "deck_pitch", 1 }, "Pitch Fader", 0.0f, 1.0f, 0.5f));
    juce::StringArray iso;
    for (auto* n : cr::ISO_NAMES) iso.add (n);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "iso", 1 }, "Isolate", iso, 0));
    return l;
}

CrateRippaProcessor::CrateRippaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "CrateRippa", layout())
{
    eng = std::make_unique<cr::Engine> (cr::defaultDataDir());
    pVol = params.getRawParameterValue ("vol");
    pPadVol = params.getRawParameterValue ("padvol");
    pPitch = params.getRawParameterValue ("deck_pitch");
    pIso = params.getRawParameterValue ("iso");
}

CrateRippaProcessor::~CrateRippaProcessor() = default;

void CrateRippaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    eng->prepare (sampleRate);
    scratchL.assign ((size_t) std::max (samplesPerBlock, 512) * 2, 0.0f);
    scratchR.assign ((size_t) std::max (samplesPerBlock, 512) * 2, 0.0f);
}

bool CrateRippaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CrateRippaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if ((int) scratchL.size() < n) { scratchL.resize ((size_t) n); scratchR.resize ((size_t) n); }   // a host that sends more than it said
    /* automation -> engine */
    float v = pVol->load(), pv = pPadVol->load(), pi = pPitch->load(), is = pIso->load();
    if (! juce::exactlyEqual (v, lastVol)) { eng->setVol (v); lastVol = v; }
    if (! juce::exactlyEqual (pv, lastPadVol)) { eng->setPadVol (pv); lastPadVol = pv; }
    if (! juce::exactlyEqual (pi, lastPitch)) { eng->setDeckPitch (pi); lastPitch = pi; }
    if (! juce::exactlyEqual (is, lastIso)) { eng->setIso ((int) is); lastIso = is; }

    cr::HostInfo h;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            h.valid = true;
            h.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) h.ppq = *ppq;
            if (auto bpm = pos->getBpm()) h.bpm = *bpm;
        }
    /* MIDI on its sample */
    int at = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > at) { eng->render (scratchL.data() + at, scratchR.data() + at, t - at, h); at = t; }
        if (msg.isNoteOn()) eng->midiNoteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) eng->midiNoteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) eng->midiAllOff();
    }
    if (n > at) eng->render (scratchL.data() + at, scratchR.data() + at, n - at, h);
    midi.clear();
    for (int c = 0; c < buffer.getNumChannels(); c++)
    {
        if (buffer.getNumChannels() == 1)
        {
            auto* o = buffer.getWritePointer (0);
            for (int i = 0; i < n; i++) o[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
        }
        else if (c < 2) buffer.copyFrom (c, 0, c == 0 ? scratchL.data() : scratchR.data(), n);
        else buffer.clear (c, 0, n);
    }
}

juce::AudioProcessorEditor* CrateRippaProcessor::createEditor() { return new CrateRippaEditor (*this); }

void CrateRippaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree t ("CrateRippa");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("tab", lastTab, nullptr);
    t.setProperty ("scale", uiScale, nullptr);
    t.setProperty ("engine", juce::String::fromUTF8 (eng->getState().c_str()), nullptr);
    t.appendChild (params.copyState(), nullptr);
    if (auto xml = t.createXml()) copyXmlToBinary (*xml, destData);
}

void CrateRippaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("CrateRippa")) return;
    lastTab = t.getProperty ("tab", 0);
    uiScale = (float) (double) t.getProperty ("scale", 1.0);
    auto p = t.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);
    eng->setState (t.getProperty ("engine", "").toString().toStdString());
    lastVol = lastPadVol = lastPitch = lastIso = -1;   // the parameters win again on the next block
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CrateRippaProcessor(); }
