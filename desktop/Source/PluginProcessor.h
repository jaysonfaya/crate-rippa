/* CRATE RIPPA Desktop (JJ DA PLUG): the plug-in. */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "CrEngine.h"
#include "CrateStore.h"

class CrateRippaProcessor : public juce::AudioProcessor
{
public:
    CrateRippaProcessor();
    ~CrateRippaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Crate Rippa"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    cr::Engine& engine() { return *eng; }
    cr::CrateStore& crates() { return *store; }
    juce::AudioProcessorValueTreeState params;
    int lastTab = 0;
    float uiScale = 1.0f;

private:
    std::unique_ptr<cr::Engine> eng;
    juce::SharedResourcePointer<cr::CrateStore> store;
    std::atomic<float>* pVol = nullptr;
    std::atomic<float>* pPadVol = nullptr;
    std::atomic<float>* pPitch = nullptr;
    std::atomic<float>* pIso = nullptr;
    float lastVol = -1, lastPadVol = -1, lastPitch = -1, lastIso = -1;
    std::vector<float> scratchL, scratchR;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrateRippaProcessor)
};
