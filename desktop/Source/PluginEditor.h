/* CRATE RIPPA Desktop (JJ DA PLUG): the editor. 1280 x 830 design, scales freely (50-200 %). */
#pragma once
#include "PluginProcessor.h"
#include "Tabs.h"

class CrateRippaEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit CrateRippaEditor (CrateRippaProcessor&);
    ~CrateRippaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int i);
    /* for the screenshot test */
    void tick() { timerCallback(); }
    static constexpr int W = 1280, H = 830;

private:
    class Content;
    CrateRippaProcessor& proc;
    cr::Look look;
    std::unique_ptr<Content> content;
    juce::ComponentBoundsConstrainer cons;
    void timerCallback() override;
};
