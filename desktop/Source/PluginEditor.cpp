/* CRATE RIPPA Desktop (JJ DA PLUG): the editor. MIT. */
#include "PluginEditor.h"

using namespace cr;

class CrateRippaEditor::Content : public juce::Component
{
public:
    Content (CrateRippaProcessor& p, CrateRippaEditor& ed)
        : ctx { p, p.engine(), p.crates(), {}, [&ed] (int i) { ed.showTab (i); } },
          player (ctx), chop (ctx), wave (ctx), iso (ctx), crate (ctx), info (ctx)
    {
        addAndMakeVisible (header);
        const char* names[] = { "PLAYER", "CHOP", "WAVE", "ISOLATE", "CRATE", "INFO" };
        for (int i = 0; i < 6; i++)
        {
            auto* b = tabBtn.add (new Btn (names[i], [&ed, i] { ed.showTab (i); }));
            addAndMakeVisible (b);
        }
        tabs = { &player, &chop, &wave, &iso, &crate, &info };
        for (auto* t : tabs) addChildComponent (t);
        stopAll.onClick = [this] { ctx.eng.stopAll(); };
        addAndMakeVisible (stopAll);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::blk);
        /* status line */
        g.setColour (col::shade);
        g.fillRect (0, H - 34, W, 34);
        g.setColour (col::gold);
        g.fillRect (0, H - 34, W, 2);
        g.setColour (busy ? col::gold : col::ink);
        g.setFont (bebas (22));
        g.drawFittedText ((busy ? juce::String::fromUTF8 ("\xe2\x97\x8f  ") : juce::String()) + status, 18, H - 32, W - 200, 32, juce::Justification::centredLeft, 1);
    }
    void resized() override
    {
        header.setBounds (0, 0, W, 96);
        for (int i = 0; i < 6; i++) tabBtn[i]->setBounds (16 + i * 178, 100, 172, 46);
        stopAll.setBounds (W - 176, 100, 160, 46);
        for (auto* t : tabs) t->setBounds (0, 152, W, 644);
    }
    void show (int i)
    {
        cur = juce::jlimit (0, 5, i);
        for (int k = 0; k < 6; k++) { tabs[(size_t) k]->setVisible (k == cur); tabBtn[k]->setOn (k == cur); }
        tick();
    }
    void tick()
    {
        ctx.view = ctx.eng.view();
        header.update (ctx.view);
        tabs[(size_t) cur]->refresh();
        juce::String st = juce::String::fromUTF8 (ctx.view.status.c_str());
        bool b = ctx.view.helperBusy || ctx.view.loading || ctx.view.exporting;
        if (st != status || b != busy) { status = st; busy = b; repaint (0, H - 34, W, 34); }
    }
    int cur = 0;

private:
    Ctx ctx;
    Header header;
    juce::OwnedArray<Btn> tabBtn;
    Btn stopAll { "STOP ALL" };
    PlayerTab player;
    ChopTab chop;
    WaveTab wave;
    IsoTab iso;
    CrateTab crate;
    InfoTab info;
    std::vector<Tab*> tabs;
    juce::String status;
    bool busy = false;
};

CrateRippaEditor::CrateRippaEditor (CrateRippaProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    content = std::make_unique<Content> (p, *this);
    content->setBounds (0, 0, W, H);
    addAndMakeVisible (*content);
    cons.setFixedAspectRatio ((double) W / H);
    cons.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&cons);
    setResizable (true, true);
    float s = juce::jlimit (0.5f, 2.0f, p.uiScale);
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    content->show (p.lastTab);
    startTimerHz (30);
}

CrateRippaEditor::~CrateRippaEditor()
{
    stopTimer();
    content.reset();
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &look) juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void CrateRippaEditor::paint (juce::Graphics& g) { g.fillAll (col::blk); }

void CrateRippaEditor::resized()
{
    float s = getWidth() / (float) W;
    content->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale = s;
}

void CrateRippaEditor::showTab (int i)
{
    proc.lastTab = i;
    content->show (i);
}

void CrateRippaEditor::timerCallback() { content->tick(); }
