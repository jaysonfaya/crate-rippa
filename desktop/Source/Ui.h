/* CRATE RIPPA Desktop (JJ DA PLUG): look and widgets. Crate Digga's look: black, white hand-drawn lines, gold, Bebas Neue. */
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "CrEngine.h"
#include "CrateStore.h"

class CrateRippaProcessor;

namespace cr
{
namespace col
{
    const juce::Colour ink { 0xfff4f1ea }, blk { 0xff0e0e0f }, gold { 0xffffb52e }, pink { 0xffff6b8a }, dim { 0xff3a3834 },
                       grey { 0xffa8a49a }, mid { 0xff58554e }, shade { 0xff2c2b28 }, deep { 0xff050505 }, green { 0xff7ee08a };
}

juce::Font bebas (float h);
juce::Font sans (float h, bool bold = false);
juce::Image dogLogo();
juce::Image jfLogo();
/* a hand-drawn rounded rectangle: two slightly wobbly strokes, deterministic per rectangle */
void sketchRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour stroke, float width, juce::Colour fill, bool shadow = true);
void sketchLine (juce::Graphics& g, float x0, float y0, float x1, float y1, juce::Colour c, float w);
void sectionBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title);

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    juce::Font getPopupMenuFont() override { return sans (16.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return bebas (20.0f); }
    juce::Font getLabelFont (juce::Label&) override { return sans (15.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& name, int id, int w, int h, bool over, bool down, int flags) override;
    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
};

/* a button with a lambda; "accent" draws it gold even when off */
class Btn : public juce::TextButton
{
public:
    Btn (const juce::String& label, std::function<void()> f = {}, bool toggle = false);
    void setAccent (bool a) { getProperties().set ("accent", a); repaint(); }
    void setOn (bool on) { if (getToggleState() != on) setToggleState (on, juce::dontSendNotification); }
};

/* a row of buttons, one is on */
class Seg : public juce::Component
{
public:
    Seg (const juce::StringArray& names, std::function<void (int)> f);
    void set (int i);
    int get() const { return cur; }
    void resized() override;
    std::function<void (int)> onChange;
private:
    juce::OwnedArray<Btn> bs;
    int cur = -1;
};

/* a slider with its name above and a value text below */
class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, double lo, double hi, double step, bool rotary, std::function<void (double)> f);
    void set (double v);
    void setText (const juce::String& t) { valueText = t; repaint(); }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider s;
private:
    juce::String name, valueText;
    bool rotary;
};

/* the turntable: the record turns with the song, the mouse on it scratches */
class Platter : public juce::Component
{
public:
    explicit Platter (Engine& e);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void update (const View& v);
private:
    Engine& eng;
    juce::Image disc;
    double angle = 0, lastA = 0;
    bool playing = false, scratching = false, hasSong = false;
    double angleAt (juce::Point<float> p) const;
};

/* a waveform: the whole song with the chops (CHOP), or the editor with start/end handles (WAVE) */
class WaveView : public juce::Component
{
public:
    WaveView (Engine& e, bool editor);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void update (const View& v) { view = &v; repaint(); }
private:
    Engine& eng;
    bool editor;
    const View* view = nullptr;
    int dragging = 0;   // 1 start, 2 end, 3 seek
    std::vector<float> pk;
    double toFrame (float x) const;
    float toX (double f) const;
};

/* 16 pads, pad 1 bottom left like the MPC; drag a chop out into the DAW */
class PadGrid : public juce::Component
{
public:
    PadGrid (Engine& e, bool compact);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void update (const View& v) { view = &v; repaint(); }
private:
    Engine& eng;
    bool compact;
    const View* view = nullptr;
    int down = -1;
    bool dragged = false;
    int padAt (juce::Point<float> p) const;
    juce::Rectangle<float> tile (int i) const;
};

/* the selected pad's ADSR, drawn */
class EnvView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (const View& v) { view = &v; repaint(); }
private:
    const View* view = nullptr;
};

/* the dog logo, the name, the song on the deck */
class Header : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (const View& v);
private:
    juce::String title = "NOTHING ON THE DECK", time;
    double prog = 0, load = 0;
};

/* what every tab gets */
struct Ctx
{
    CrateRippaProcessor& proc;
    Engine& eng;
    CrateStore& crates;
    View view;
    std::function<void (int)> showTab;
};

class Tab : public juce::Component
{
public:
    explicit Tab (Ctx& c) : ctx (c) {}
    virtual void refresh() = 0;
protected:
    Ctx& ctx;
};

std::string titleCase (const std::string& s);
} // namespace cr
