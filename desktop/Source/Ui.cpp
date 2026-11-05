/* CRATE RIPPA Desktop (JJ DA PLUG): look and widgets. MIT. */
#include "Ui.h"
#include "BinaryData.h"
#include <algorithm>
#include <cmath>

namespace cr
{
/* ------------------------------------------------------------------ fonts, images */
static juce::Typeface::Ptr bebasFace()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::BebasNeue_ttf, BinaryData::BebasNeue_ttfSize);
    return tf;
}
juce::Font bebas (float h) { return juce::Font (juce::FontOptions (bebasFace()).withHeight (h)).withExtraKerningFactor (0.04f); }
juce::Font sans (float h, bool bold) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Image dogLogo()
{
    static juce::Image im = juce::ImageCache::getFromMemory (BinaryData::dog_logo_png, BinaryData::dog_logo_pngSize);
    return im;
}
juce::Image jfLogo()
{
    static juce::Image im = juce::ImageCache::getFromMemory (BinaryData::jf_gold_png, BinaryData::jf_gold_pngSize);
    return im;
}
std::string titleCase (const std::string& s) { return s; }

static float jit (uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return ((seed >> 9) & 0xffff) / 65535.0f - 0.5f;
}
void sketchRect (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour stroke, float width, juce::Colour fill, bool shadow)
{
    if (shadow) { g.setColour (col::shade); g.fillRoundedRectangle (r.translated (4, 5), radius); }
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    uint32_t seed = (uint32_t) (r.getX() * 7 + r.getY() * 13 + r.getWidth() * 3 + r.getHeight());
    for (int pass = 0; pass < 2; pass++)
    {
        juce::Path p;
        float j = pass ? 1.4f : 0.6f;
        auto rr = r.reduced (width * 0.5f).translated (jit (seed) * j, jit (seed) * j);
        p.addRoundedRectangle (rr.getX() + jit (seed) * j, rr.getY() + jit (seed) * j, rr.getWidth() + jit (seed) * j, rr.getHeight() + jit (seed) * j, radius);
        g.setColour (stroke.withAlpha (pass ? 0.35f : 1.0f));
        g.strokePath (p, juce::PathStrokeType (pass ? width * 0.6f : width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
void sketchLine (juce::Graphics& g, float x0, float y0, float x1, float y1, juce::Colour c, float w)
{
    uint32_t seed = (uint32_t) (x0 * 3 + y0 * 5 + x1 * 7 + y1);
    juce::Path p;
    p.startNewSubPath (x0, y0);
    p.quadraticTo ((x0 + x1) / 2 + jit (seed) * 3, (y0 + y1) / 2 + jit (seed) * 3, x1, y1);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void sectionBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    sketchRect (g, r.toFloat(), 18, col::ink, 3.0f, col::blk, false);
    if (title.isNotEmpty())
    {
        auto f = bebas (20);
        int w = juce::GlyphArrangement::getStringWidthInt (f, title) + 18;
        g.setColour (col::blk);
        g.fillRect (r.getX() + 18, r.getY() - 11, w, 22);
        g.setColour (col::gold);
        g.setFont (f);
        g.drawText (title, r.getX() + 26, r.getY() - 11, w, 22, juce::Justification::centredLeft);
    }
}

/* ------------------------------------------------------------------ look */
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::blk);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::gold.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    setColour (juce::CaretComponent::caretColourId, col::gold);
    setColour (juce::ListBox::backgroundColourId, col::blk);
    setColour (juce::ListBox::outlineColourId, col::ink);
    setColour (juce::Label::textColourId, col::ink);
    setColour (juce::PopupMenu::backgroundColourId, col::shade);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::gold);
    setColour (juce::PopupMenu::highlightedTextColourId, col::blk);
    setColour (juce::ComboBox::textColourId, col::ink);
    setColour (juce::ComboBox::backgroundColourId, col::blk);
    setColour (juce::ComboBox::arrowColourId, col::gold);
    setColour (juce::TableListBox::backgroundColourId, col::blk);
    setColour (juce::ScrollBar::thumbColourId, col::gold);
    setColour (juce::AlertWindow::backgroundColourId, col::shade);
    setColour (juce::AlertWindow::textColourId, col::ink);
    setColour (juce::AlertWindow::outlineColourId, col::gold);
    setColour (juce::TooltipWindow::backgroundColourId, col::shade);
    setColour (juce::TooltipWindow::textColourId, col::ink);
    setColour (juce::TooltipWindow::outlineColourId, col::gold);
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2, 2);
    r.removeFromRight (3);
    r.removeFromBottom (3);
    bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
    if (down) r = r.translated (2, 2);
    auto fill = on ? col::gold : (over ? juce::Colour (0xff1c1b1a) : col::blk);
    if (! b.isEnabled()) fill = fill.withAlpha (0.4f);
    sketchRect (g, r, juce::jmin (12.0f, r.getHeight() * 0.3f), b.isEnabled() ? col::ink : col::mid, 2.4f, fill, ! down);
}
juce::Font Look::getTextButtonFont (juce::TextButton&, int h) { return bebas (juce::jmin (26.0f, h * 0.56f)); }
void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
    g.setColour (on ? col::blk : (b.isEnabled() ? col::ink : col::mid));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto r = b.getLocalBounds().reduced (6, 2).withTrimmedRight (3).withTrimmedBottom (3);
    if (down) r = r.translated (2, 2);
    g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 2, 0.8f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s)
{
    bool vert = st == juce::Slider::LinearVertical;
    juce::Rectangle<float> tr;
    if (vert) tr = { x + w / 2.0f - 5, (float) y, 10.0f, (float) h };
    else tr = { (float) x, y + h / 2.0f - 5, (float) w, 10.0f };
    g.setColour (col::dim);
    g.fillRoundedRectangle (tr, 5);
    g.setColour (col::gold.withAlpha (0.85f));
    if (vert)
    {
        if (s.getProperties()["centre"]) { float c = y + h / 2.0f; g.fillRect (tr.withTop (juce::jmin (c, pos)).withBottom (juce::jmax (c, pos)).reduced (2, 0)); }
        else g.fillRoundedRectangle (tr.withTop (pos), 5);
    }
    else g.fillRoundedRectangle (tr.withRight (pos), 5);
    g.setColour (col::ink);
    g.drawRoundedRectangle (tr, 5, 1.5f);
    juce::Rectangle<float> th = vert ? juce::Rectangle<float> (x + w / 2.0f - 22, pos - 13, 44, 26) : juce::Rectangle<float> (pos - 10, y + h / 2.0f - 15, 20, 30);
    g.setColour (col::shade);
    g.fillRoundedRectangle (th.translated (2, 3), 7);
    g.setColour (col::gold);
    g.fillRoundedRectangle (th, 7);
    g.setColour (col::ink);
    g.drawRoundedRectangle (th, 7, 2);
    if (vert) { g.setColour (col::blk); g.drawLine (th.getX() + 6, th.getCentreY(), th.getRight() - 6, th.getCentreY(), 2.5f); }
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
    float d = juce::jmin (r.getWidth(), r.getHeight());
    auto c = r.getCentre();
    float rad = d / 2;
    float a = a0 + pos * (a1 - a0);
    juce::Path bg;
    bg.addCentredArc (c.x, c.y, rad - 3, rad - 3, 0, a0, a1, true);
    g.setColour (col::dim);
    g.strokePath (bg, juce::PathStrokeType (5, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path fg;
    fg.addCentredArc (c.x, c.y, rad - 3, rad - 3, 0, a0, a, true);
    g.setColour (col::gold);
    g.strokePath (fg, juce::PathStrokeType (5, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    float kr = rad - 11;
    g.setColour (col::shade);
    g.fillEllipse (c.x - kr + 2, c.y - kr + 3, 2 * kr, 2 * kr);
    g.setColour (col::blk);
    g.fillEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr);
    g.setColour (col::ink);
    g.drawEllipse (c.x - kr, c.y - kr, 2 * kr, 2 * kr, 2.2f);
    g.setColour (col::gold);
    g.drawLine (c.x + std::sin (a) * kr * 0.3f, c.y - std::cos (a) * kr * 0.3f, c.x + std::sin (a) * kr * 0.9f, c.y - std::cos (a) * kr * 0.9f, 3.5f);
}
juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::textColourId, col::ink);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setFont (bebas (18));
    return l;
}
void Look::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 8);
}
void Look::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& t)
{
    g.setColour (t.hasKeyboardFocus (true) ? col::gold : col::gold.withAlpha (0.6f));
    g.drawRoundedRectangle (1, 1, (float) w - 2, (float) h - 2, 8, 2.5f);
}
void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    g.setColour (col::shade);
    g.fillRoundedRectangle ((float) x + 2, (float) y + 2, (float) w - 4, (float) h - 4, 5);
    juce::Rectangle<float> th = vertical ? juce::Rectangle<float> ((float) x + 2, (float) ts, (float) w - 4, (float) tsz)
                                         : juce::Rectangle<float> ((float) ts, (float) y + 2, (float) tsz, (float) h - 4);
    g.setColour (over ? col::gold : col::gold.withAlpha (0.75f));
    g.fillRoundedRectangle (th.reduced (1), 5);
}
void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    sketchRect (g, { 2, 2, (float) w - 5, (float) h - 5 }, 8, col::ink, 2, col::blk, false);
    juce::Path p;
    p.addTriangle ((float) w - 22, h / 2.0f - 3, (float) w - 12, h / 2.0f - 3, (float) w - 17, h / 2.0f + 4);
    g.setColour (col::gold);
    g.fillPath (p);
}
void Look::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& h)
{
    g.fillAll (col::shade);
    g.setColour (col::gold);
    g.fillRect (0, h.getHeight() - 2, h.getWidth(), 2);
}
void Look::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name, int, int w, int h, bool over, bool, int flags)
{
    if (over) { g.setColour (col::dim); g.fillRect (0, 0, w, h); }
    g.setColour (col::gold);
    g.setFont (bebas (18));
    juce::String t = name;
    if (flags & juce::TableHeaderComponent::sortedForwards) t << "  ^";
    if (flags & juce::TableHeaderComponent::sortedBackwards) t << "  v";
    g.drawText (t, 8, 0, w - 10, h, juce::Justification::centredLeft);
    g.setColour (col::mid);
    g.drawLine ((float) w - 1, 4, (float) w - 1, (float) h - 4);
}

/* ------------------------------------------------------------------ Btn, Seg, Knob */
Btn::Btn (const juce::String& label, std::function<void()> f, bool toggle) : juce::TextButton (label)
{
    onClick = std::move (f);
    setClickingTogglesState (false);
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    juce::ignoreUnused (toggle);
}

Seg::Seg (const juce::StringArray& names, std::function<void (int)> f) : onChange (std::move (f))
{
    for (int i = 0; i < names.size(); i++)
    {
        auto* b = bs.add (new Btn (names[i], [this, i] { set (i); if (onChange) onChange (i); }));
        addAndMakeVisible (b);
    }
}
void Seg::set (int i)
{
    cur = i;
    for (int k = 0; k < bs.size(); k++) bs[k]->setOn (k == i);
}
void Seg::resized()
{
    auto r = getLocalBounds();
    int n = bs.size();
    for (int k = 0; k < n; k++) bs[k]->setBounds (r.getX() + r.getWidth() * k / n, r.getY(), r.getWidth() / n, r.getHeight());
}

Knob::Knob (const juce::String& n, double lo, double hi, double step, bool rot, std::function<void (double)> f) : name (n), rotary (rot)
{
    s.setSliderStyle (rot ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (lo, hi, step);
    s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    s.onValueChange = [this, f] { if (f) f (s.getValue()); };
    addAndMakeVisible (s);
}
void Knob::set (double v)
{
    if (! s.isMouseButtonDown()) s.setValue (v, juce::dontSendNotification);
}
void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (20);
    r.removeFromBottom (20);
    s.setBounds (r);
}
void Knob::paint (juce::Graphics& g)
{
    g.setColour (col::grey);
    g.setFont (bebas (18));
    g.drawText (name, getLocalBounds().removeFromTop (20), rotary ? juce::Justification::centred : juce::Justification::centredLeft);
    g.setColour (col::ink);
    g.drawText (valueText, getLocalBounds().removeFromBottom (20), rotary ? juce::Justification::centred : juce::Justification::centredLeft);
}

/* ------------------------------------------------------------------ the turntable */
Platter::Platter (Engine& e) : eng (e)
{
    const int S = 520;
    disc = juce::Image (juce::Image::ARGB, S, S, true);
    juce::Graphics g (disc);
    float c = S / 2.0f;
    g.setColour (col::deep);
    g.fillEllipse (2, 2, S - 4, S - 4);
    for (int r = (int) c - 8; r > 112; r -= 3)
    {
        g.setColour (juce::Colour (0xff1d1d1f).withAlpha (r % 9 == 0 ? 1.0f : 0.55f));
        g.drawEllipse (c - r, c - r, 2.0f * r, 2.0f * r, 1.0f);
    }
    /* light on the grooves */
    juce::ColourGradient sh (juce::Colours::white.withAlpha (0.10f), c, c - 200, juce::Colours::transparentWhite, c + 120, c, true);
    g.setGradientFill (sh);
    g.fillEllipse (8, 8, S - 16, S - 16);
    g.setColour (col::ink);
    g.drawEllipse (3, 3, S - 6, S - 6, 4);
    /* the label: gold with the dog logo */
    float lr = 118;
    g.setColour (col::gold);
    g.fillEllipse (c - lr, c - lr, 2 * lr, 2 * lr);
    g.setColour (col::ink);
    g.drawEllipse (c - lr, c - lr, 2 * lr, 2 * lr, 5);
    auto dog = dogLogo();
    float k = 200.0f / (float) juce::jmax (dog.getWidth(), dog.getHeight());
    g.drawImage (dog, juce::Rectangle<float> (c - dog.getWidth() * k / 2, c - dog.getHeight() * k / 2, dog.getWidth() * k, dog.getHeight() * k));
    g.setColour (col::blk);
    g.fillEllipse (c - 8, c - 8, 16, 16);
    g.setColour (col::ink);
    g.drawEllipse (c - 8, c - 8, 16, 16, 2);
    /* a gold marker on the rim shows the turning */
    juce::Path m;
    m.addTriangle (c - 12, 12, c + 12, 12, c, 40);
    g.setColour (col::gold);
    g.fillPath (m);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}
void Platter::update (const View& v)
{
    double a = v.angle - std::floor (v.angle);
    bool ch = std::abs (a - angle) > 1e-4 || playing != v.playing || scratching != v.scratching || hasSong != (v.avail > 0);
    angle = a;
    playing = v.playing;
    scratching = v.scratching;
    hasSong = v.avail > 0;
    if (ch) repaint();
}
void Platter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    float d = juce::jmin (r.getWidth(), r.getHeight()) - 8;
    auto c = r.getCentre();
    g.setColour (col::shade);
    g.fillEllipse (c.x - d / 2 + 5, c.y - d / 2 + 7, d, d);
    float k = d / disc.getWidth();
    g.drawImageTransformed (disc, juce::AffineTransform::translation (-disc.getWidth() / 2.0f, -disc.getHeight() / 2.0f)
                                      .rotated ((float) (angle * juce::MathConstants<double>::twoPi))
                                      .scaled (k)
                                      .translated (c.x, c.y), false);
    if (scratching)
    {
        g.setColour (col::pink.withAlpha (0.9f));
        g.drawEllipse (c.x - d / 2 + 2, c.y - d / 2 + 2, d - 4, d - 4, 4);
    }
    if (! hasSong)
    {
        g.setColour (col::ink.withAlpha (0.85f));
        g.setFont (bebas (22));
        g.drawText ("PICK A SONG ON THE RIGHT", r.withY (c.y + d * 0.30f).withHeight (30), juce::Justification::centred);
    }
}
double Platter::angleAt (juce::Point<float> p) const
{
    auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (p.y - c.y, p.x - c.x);
}
void Platter::mouseDown (const juce::MouseEvent& e)
{
    lastA = angleAt (e.position);
    eng.scratchTouch (true);
}
void Platter::mouseDrag (const juce::MouseEvent& e)
{
    double a = angleAt (e.position), d = a - lastA;
    if (d > juce::MathConstants<double>::pi) d -= juce::MathConstants<double>::twoPi;
    if (d < -juce::MathConstants<double>::pi) d += juce::MathConstants<double>::twoPi;
    lastA = a;
    eng.scratchMove (d / juce::MathConstants<double>::twoPi);
}
void Platter::mouseUp (const juce::MouseEvent&) { eng.scratchTouch (false); }

/* ------------------------------------------------------------------ waveform */
WaveView::WaveView (Engine& e, bool ed) : eng (e), editor (ed) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

double WaveView::toFrame (float x) const
{
    if (! view) return 0;
    double a = editor ? view->win0 : 0, b = editor ? view->win1 : std::max (1, view->viewLen);
    return a + (b - a) * juce::jlimit (0.0, 1.0, (double) (x - 10) / std::max (1, getWidth() - 20));
}
float WaveView::toX (double f) const
{
    if (! view) return 0;
    double a = editor ? view->win0 : 0, b = editor ? view->win1 : std::max (1, view->viewLen);
    return 10 + (float) ((f - a) / std::max (1.0, b - a) * (getWidth() - 20));
}

void WaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    sketchRect (g, r.reduced (1), 12, col::ink, 2.2f, juce::Colours::black, false);
    if (! view || view->avail <= 0)
    {
        g.setColour (col::grey);
        g.setFont (bebas (24));
        g.drawText (view && view->loading ? "LOADING..." : "NO SONG ON THE DECK", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto& v = *view;
    const auto& sp = v.pads[v.sel];
    bool own = editor && sp.used && sp.own;
    double a = editor ? v.win0 : 0, b = editor ? v.win1 : std::max (1, v.viewLen);
    int cols = std::max (8, (getWidth() - 20) / 2);
    eng.peaks (a, b, cols, own, pk);
    float mid = getHeight() / 2.0f, hh = getHeight() / 2.0f - 12;
    /* the selected pad's region */
    if (sp.used && (! sp.own || own))
    {
        float x0 = juce::jmax (10.0f, toX (sp.start)), x1 = juce::jmin ((float) getWidth() - 10, toX (sp.end));
        if (x1 > x0) { g.setColour (col::gold.withAlpha (0.16f)); g.fillRect (x0, 6.0f, x1 - x0, getHeight() - 12.0f); }
    }
    /* loaded part vs still loading */
    float xl = toX (v.avail);
    for (int c = 0; c < cols; c++)
    {
        float x = 10 + (getWidth() - 20) * (c + 0.5f) / cols;
        float h = pk[(size_t) c] * hh;
        bool in = sp.used && (! sp.own || own) && toX (sp.start) <= x && x <= toX (sp.end);
        g.setColour (x > xl ? col::dim : (in ? col::gold : col::ink.withAlpha (0.85f)));
        g.fillRect (x - 0.8f, mid - h, 1.6f, juce::jmax (1.0f, 2 * h));
    }
    g.setColour (col::dim);
    g.drawHorizontalLine ((int) mid, 10, (float) getWidth() - 10);
    /* chop starts on this bank, with names */
    if (! own)
    {
        g.setFont (bebas (16));
        std::vector<std::pair<float, int>> marks;
        for (int i = v.bank * 16; i < v.bank * 16 + 16; i++)
        {
            const auto& p = v.pads[i];
            if (! p.used || p.own) continue;
            float x = toX (p.start);
            if (x < 10 || x > getWidth() - 10) continue;
            g.setColour (i == v.sel ? col::gold : col::pink);
            g.drawLine (x, 6, x, (float) getHeight() - 6, i == v.sel ? 2.5f : 1.5f);
            marks.push_back ({ x, i });
        }
        std::sort (marks.begin(), marks.end());
        float lastX = -100;
        for (auto& mk : marks)   /* names only where they fit (the selected pad always) */
        {
            if (mk.first - lastX < 26 && mk.second != v.sel) continue;
            g.setColour (mk.second == v.sel ? col::gold : col::pink);
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + mk.second / 16)) + juce::String (mk.second % 16 + 1), (int) mk.first + 3, 6, 40, 16, juce::Justification::left);
            lastX = mk.first;
        }
        if (v.cuePos > 0)
        {
            float x = toX (v.cuePos);
            g.setColour (col::green);
            g.drawLine (x, 6, x, (float) getHeight() - 6, 1.5f);
            g.drawText ("CUE", (int) x + 3, getHeight() - 22, 40, 16, juce::Justification::left);
        }
    }
    /* the editor's handles */
    if (editor && sp.used && (! sp.own || own))
    {
        for (int k = 0; k < 2; k++)
        {
            float x = toX (k == 0 ? sp.start : sp.end);
            g.setColour (col::gold);
            g.drawLine (x, 4, x, (float) getHeight() - 4, 3);
            juce::Path t;
            if (k == 0) t.addTriangle (x, 4, x + 16, 4, x, 24);
            else t.addTriangle (x, 4, x - 16, 4, x, 24);
            g.fillPath (t);
            g.setFont (bebas (16));
            if (toX (sp.end) - toX (sp.start) > 110)
                g.drawText (k == 0 ? "START" : "END", k == 0 ? (int) x + 4 : (int) x - 54, getHeight() - 22, 50, 16, k == 0 ? juce::Justification::left : juce::Justification::right);
        }
    }
    /* the playhead: the selected pad while it sounds, else the song */
    double ph = v.selPlayPos >= 0 && (! sp.own || own) ? v.selPlayPos : (! own && (v.playing || v.scratching) ? v.ppos : -1);
    if (! editor && ph < 0) ph = v.ppos;
    if (ph >= 0)
    {
        float x = toX (ph);
        if (x >= 10 && x <= getWidth() - 10) { g.setColour (juce::Colours::white); g.drawLine (x, 2, x, (float) getHeight() - 2, 2); }
    }
}
void WaveView::mouseMove (const juce::MouseEvent& e)
{
    if (! editor || ! view) return;
    const auto& sp = view->pads[view->sel];
    bool nearH = sp.used && (std::abs (e.position.x - toX (sp.start)) < 10 || std::abs (e.position.x - toX (sp.end)) < 10);
    setMouseCursor (nearH ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}
void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (! view || view->avail <= 0) return;
    if (! editor) { dragging = 3; eng.seek ((toFrame (e.position.x)) / std::max (1, view->viewLen)); return; }
    const auto& sp = view->pads[view->sel];
    if (! sp.used) return;
    float xs = toX (sp.start), xe = toX (sp.end);
    dragging = std::abs (e.position.x - xs) <= std::abs (e.position.x - xe) ? 1 : 2;
    mouseDrag (e);
}
void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! view) return;
    double f = juce::jlimit (0.0, 1.0, (double) (e.position.x - 10) / std::max (1, getWidth() - 20));
    if (dragging == 1) eng.setStartFrac (f);
    else if (dragging == 2) eng.setEndFrac (f);
    else if (dragging == 3) eng.seek (f);
}
void WaveView::mouseUp (const juce::MouseEvent&) { dragging = 0; }

/* ------------------------------------------------------------------ pads */
PadGrid::PadGrid (Engine& e, bool c) : eng (e), compact (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

juce::Rectangle<float> PadGrid::tile (int i) const
{
    float gap = compact ? 6.0f : 10.0f;
    float w = (getWidth() - 3 * gap) / 4, h = (getHeight() - 3 * gap) / 4;
    int row = 3 - i / 4, cc = i % 4;
    return { cc * (w + gap), row * (h + gap), w - 4, h - 4 };
}
int PadGrid::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; i++) if (tile (i).expanded (3).contains (p)) return i;
    return -1;
}
void PadGrid::paint (juce::Graphics& g)
{
    if (! view) return;
    const auto& v = *view;
    for (int i = 0; i < 16; i++)
    {
        int idx = v.bank * 16 + i;
        const auto& p = v.pads[idx];
        auto r = tile (i);
        bool sel = idx == v.sel, target = v.pendOp && idx != v.pendSrc;
        auto fill = p.sounding ? col::gold : (p.used ? juce::Colour (0xff221f1a) : col::blk);
        sketchRect (g, r, 10, sel ? col::gold : (target ? col::pink : col::ink), sel ? 3.6f : 2.2f, fill);
        auto tc = p.sounding ? col::blk : col::ink;
        g.setColour (sel && ! p.sounding ? col::gold : tc);
        g.setFont (bebas (compact ? 20.0f : 30.0f));
        auto in = r.reduced (8, 4);
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + v.bank)) + juce::String (i + 1), in, juce::Justification::topLeft);
        if (p.used)
        {
            g.setFont (bebas (compact ? 14.0f : 18.0f));
            g.setColour (tc.withAlpha (0.85f));
            juce::String info = p.own ? juce::String ((p.end - p.start) / (double) SR, 2) + "S  OWN" : juce::String (fmtTime (p.start / (double) SR, false));
            g.drawText (info, in, juce::Justification::bottomLeft);
            juce::String tags;
            if (p.grp) tags << "G" << p.grp << " ";
            if (p.rev) tags << "REV ";
            if (p.pitch) tags << (p.pitch > 0 ? "+" : "") << p.pitch;
            g.drawText (tags.trim(), in, juce::Justification::topRight);
            if (! compact && p.iso) { g.setFont (bebas (14)); g.drawText (ISO_NAMES[p.iso], in, juce::Justification::bottomRight); }
        }
        else if (target)
        {
            g.setColour (col::pink);
            g.setFont (bebas (compact ? 18.0f : 26.0f));
            g.drawText ("?", in, juce::Justification::centred);
        }
    }
}
void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    if (! view) return;
    int i = padAt (e.position);
    if (i < 0) return;
    down = view->bank * 16 + i;
    dragged = false;
    if (e.mods.isPopupMenu()) { eng.selectPad (down); down = -1; return; }
    float vel = juce::jlimit (0.3f, 1.0f, 1.0f - (e.position.y - tile (i).getY()) / tile (i).getHeight() * 0.5f);
    eng.padHit (down, vel, true);
}
void PadGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (down < 0 || dragged || ! view || e.getDistanceFromDragStart() < 12) return;
    if (! view->pads[down].used) return;
    dragged = true;
    eng.padRelease (down);
    /* drag the chop out: a WAV as it sounds (pitch, reverse, filter, level) */
    char nm[8];
    snprintf (nm, sizeof nm, "%c%02d", 'A' + down / 16, down % 16 + 1);
    auto f = eng.dataDir().getChildFile ("Drag").getChildFile (juce::String (nm) + " " + juce::String (safeName (view->songTitle.empty() ? "chop" : view->songTitle)) + ".wav");
    f.getParentDirectory().createDirectory();
    if (eng.bouncePadToFile (down, f))
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}
void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (down >= 0 && ! dragged) eng.padRelease (down);
    down = -1;
}

/* ------------------------------------------------------------------ envelope */
void EnvView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    sketchRect (g, r.reduced (1), 10, col::ink, 2, juce::Colours::black, false);
    if (! view) return;
    const auto& p = view->pads[view->sel];
    if (! p.used) return;
    double a = p.att, d = p.dec, rl = p.rel, hold = (a + d + rl) * 0.35 + 60, tot = a + d + hold + rl;
    auto in = r.reduced (12, 10);
    auto X = [&] (double ms) { return in.getX() + (float) (ms / tot) * in.getWidth(); };
    auto Y = [&] (double lv) { return in.getBottom() - (float) lv * in.getHeight(); };
    juce::Path path;
    path.startNewSubPath (X (0), Y (0));
    path.lineTo (X (a), Y (1));
    path.quadraticTo (X (a + d * 0.3), Y (p.sus + (1 - p.sus) * 0.15), X (a + d), Y (p.sus));
    path.lineTo (X (a + d + hold), Y (p.sus));
    path.quadraticTo (X (a + d + hold + rl * 0.25), Y (p.sus * 0.15), X (tot), Y (0));
    juce::Path fill (path);
    fill.lineTo (X (tot), Y (0));
    fill.closeSubPath();
    g.setColour (col::gold.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (col::gold);
    g.strokePath (path, juce::PathStrokeType (3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::grey);
    g.setFont (bebas (15));
    g.drawText ("A", (int) X (a / 2) - 10, (int) in.getBottom() - 16, 20, 16, juce::Justification::centred);
    g.drawText ("D", (int) X (a + d / 2) - 10, (int) in.getBottom() - 16, 20, 16, juce::Justification::centred);
    g.drawText ("S", (int) X (a + d + hold / 2) - 10, (int) in.getBottom() - 16, 20, 16, juce::Justification::centred);
    g.drawText ("R", (int) X (a + d + hold + rl / 2) - 10, (int) in.getBottom() - 16, 20, 16, juce::Justification::centred);
}

/* ------------------------------------------------------------------ header */
void Header::update (const View& v)
{
    juce::String t = v.songTitle.empty() ? juce::String ("NOTHING ON THE DECK") : juce::String::fromUTF8 (v.songTitle.c_str());
    juce::String tm;
    if (v.avail > 0 || v.loading)
    {
        tm = juce::String (fmtTime (v.ppos / SR, false)) + " / " + juce::String (fmtTime (v.viewLen / (double) SR, false));
        if (v.loading && v.total > 0) tm << "   " << (int) (100.0 * v.avail / v.total) << "%";
    }
    double pr = v.viewLen > 0 ? v.ppos / v.viewLen : 0, ld = v.total > 0 ? (double) v.avail / v.total : (v.avail > 0 ? 1 : 0);
    if (t != title || tm != time || std::abs (pr - prog) > 0.001 || std::abs (ld - load) > 0.001) { title = t; time = tm; prog = pr; load = ld; repaint(); }
}
void Header::paint (juce::Graphics& g)
{
    auto dog = dogLogo();
    g.drawImage (dog, juce::Rectangle<float> (14, 6, 84 * dog.getWidth() / (float) dog.getHeight(), 84));
    g.setColour (col::gold);
    g.setFont (bebas (64));
    g.drawText ("CRATE RIPPA", 108, 4, 360, 62, juce::Justification::left);
    sketchLine (g, 110, 64, 400, 64, col::gold, 4);
    g.setFont (bebas (19));
    g.drawText ("DIG IT  -  RIP IT  -  FLIP IT", 110, 68, 360, 22, juce::Justification::left);
    /* the deck readout */
    juce::Rectangle<float> box (500, 12, 766, 76);
    sketchRect (g, box, 14, col::ink, 2.5f, juce::Colours::black, false);
    g.setColour (col::ink);
    g.setFont (sans (19, true));
    g.drawFittedText (title, juce::Rectangle<int> (518, 18, 560, 30), juce::Justification::centredLeft, 1, 0.85f);
    g.setColour (col::gold);
    g.setFont (bebas (26));
    g.drawText (time, 1080, 18, 176, 30, juce::Justification::centredRight);
    juce::Rectangle<float> bar (518, 58, 730, 14);
    g.setColour (col::dim);
    g.fillRoundedRectangle (bar, 6);
    g.setColour (col::mid);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, load)), 6);
    g.setColour (col::gold);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, prog)), 6);
    g.setColour (col::ink);
    g.drawRoundedRectangle (bar, 6, 1.4f);
}
} // namespace cr
