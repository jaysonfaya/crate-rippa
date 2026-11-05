/* CRATE RIPPA Desktop (JJ DA PLUG): PLAYER, CHOP, WAVE, ISOLATE, INFO. MIT. */
#include "Tabs.h"
#include "PluginProcessor.h"

namespace cr
{
static juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
static void setParam (CrateRippaProcessor& p, const char* id, float v01)
{
    if (auto* prm = p.params.getParameter (id))
        if (std::abs (prm->getValue() - v01) > 1e-5f) prm->setValueNotifyingHost (v01);
}

/* =================================================================== PLAYER */
PlayerTab::PlayerTab (Ctx& c)
    : Tab (c), platter (c.eng),
      range ({ "8%", "16%", "50%", "OCT" }, [this] (int i) { ctx.eng.setRange (i); }),
      find ({ "SONGS", "LISTS" }, {}),
      klock ("KEY LOCK", [this] { ctx.eng.setKeyLock (! ctx.view.klock); }),
      cue ("CUE", [this] { ctx.eng.deckCue(); }),
      play ("START / STOP", [this] { ctx.eng.playToggle(); }),
      rew ("<<", [this] { ctx.eng.rew(); }),
      fwd (">>", [this] { ctx.eng.fwd(); }),
      stop ("STOP", [this] { ctx.eng.stop(); }),
      loop ("1X", [this] { ctx.eng.setLoop (! ctx.view.loop); }),
      searchB ("SEARCH", [this] { ctx.eng.setQuery (query.getText().toStdString()); ctx.eng.search (find.get() == 1); }),
      history ("HISTORY", [this] { ctx.eng.showHistory(); }),
      saveList ("SAVE AS CRATE", [this] {
          std::vector<CrateSong> songs;
          for (auto& r : rows) { CrateSong s; s.id = r.id; s.title = r.title; s.chan = r.chan; s.secs = r.secs; songs.push_back (s); }
          auto& v = ctx.view;
          int ci = ctx.crates.saveAsCrate (v.lastPlaylistTitle.empty() ? "PLAYLIST" : v.lastPlaylistTitle, "youtube:" + v.lastPlaylistId, songs);
          ctx.eng.setResults (rows, ctx.view.resHead, 0);
          juce::ignoreUnused (ci);
          saveList.setButtonText ("SAVED IN CRATE");
      }),
      songMix ("SONG MIX", 0, 1, 0.001, false, [this] (double x) { setParam (ctx.proc, "vol", (float) x); }),
      padMix ("PAD MIX", 0, 1, 0.001, false, [this] (double x) { setParam (ctx.proc, "padvol", (float) x); })
{
    addAndMakeVisible (platter);
    pitch.setSliderStyle (juce::Slider::LinearVertical);
    pitch.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitch.setRange (0, 1, 0.0005);
    pitch.getProperties().set ("centre", true);
    pitch.setDoubleClickReturnValue (true, 0.5);
    pitch.onValueChange = [this] { setParam (ctx.proc, "deck_pitch", (float) (1.0 - pitch.getValue())); };   /* up = slower, like a Technics */
    addAndMakeVisible (pitch);
    find.set (0);
    for (auto* comp : std::initializer_list<juce::Component*> { &range, &find, &klock, &cue, &play, &rew, &fwd, &stop, &loop, &searchB, &history, &saveList, &songMix, &padMix })
        addAndMakeVisible (comp);
    play.setAccent (true);
    seek.setSliderStyle (juce::Slider::LinearHorizontal);
    seek.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    seek.setRange (0, 1, 0.0001);
    seek.onValueChange = [this] { if (seek.isMouseButtonDown()) ctx.eng.seek (seek.getValue()); };
    addAndMakeVisible (seek);
    query.setFont (sans (20));
    query.setTextToShowWhenEmpty ("Type a song, an artist, a sample...", col::grey);
    query.setIndents (12, 11);
    query.onReturnKey = [this] { searchB.triggerClick(); };
    addAndMakeVisible (query);
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);
    saveList.setVisible (false);
}

void PlayerTab::resized()
{
    platter.setBounds (40, 38, 392, 392);
    pitch.setBounds (450, 70, 60, 330);
    range.setBounds (36, 462, 300, 42);
    klock.setBounds (344, 462, 160, 42);
    cue.setBounds (36, 514, 150, 52);
    play.setBounds (194, 514, 310, 52);
    query.setBounds (556, 38, 540, 48);
    searchB.setBounds (1104, 38, 144, 48);
    find.setBounds (556, 94, 230, 40);
    history.setBounds (794, 94, 140, 40);
    saveList.setBounds (1048, 94, 200, 40);
    list.setBounds (556, 170, 692, 396);
    rew.setBounds (16, 590, 70, 50);
    fwd.setBounds (92, 590, 70, 50);
    stop.setBounds (168, 590, 100, 50);
    loop.setBounds (274, 590, 100, 50);
    seek.setBounds (390, 594, 520, 44);
    songMix.setBounds (930, 580, 160, 66);
    padMix.setBounds (1104, 580, 160, 66);
}

void PlayerTab::paint (juce::Graphics& g)
{
    sectionBox (g, { 16, 14, 504, 566 }, "THE DECK");
    sectionBox (g, { 536, 14, 728, 566 }, "YOUTUBE");
    g.setColour (col::grey);
    g.setFont (bebas (18));
    g.drawText ("PITCH", 440, 40, 80, 24, juce::Justification::centred);
    g.setColour (col::gold);
    g.setFont (bebas (22));
    g.drawText (pitchTxt, 430, 404, 100, 26, juce::Justification::centred);
    g.setColour (col::grey);
    g.setFont (bebas (16));
    g.drawText ("DRAG ON THE RECORD TO SCRATCH", 36, 434, 404, 22, juce::Justification::centred);
    g.setColour (col::gold);
    g.setFont (bebas (21));
    g.drawFittedText (head, 556, 140, 692, 26, juce::Justification::centredLeft, 1);
}

void PlayerTab::refresh()
{
    const auto& v = ctx.view;
    platter.update (v);
    range.set (v.prange);
    klock.setOn (v.klock);
    loop.setButtonText (v.loop ? "LOOP" : "1X");
    loop.setOn (v.loop);
    play.setButtonText (v.playing ? "STOP" : "START");
    if (! pitch.isMouseButtonDown()) pitch.setValue (1.0 - v.dpitch / 1000.0, juce::dontSendNotification);
    juce::String pt = v.prange == 3 ? juce::String (12.0 * std::log2 (v.speed), 1) + " ST" : juce::String ((v.speed - 1.0) * 100.0, 1) + " %";
    if (! pt.startsWith ("-")) pt = "+" + pt;
    if (! seek.isMouseButtonDown()) seek.setValue (v.viewLen > 0 ? v.ppos / v.viewLen : 0, juce::dontSendNotification);
    songMix.set (v.vol);
    songMix.setText (juce::String (juce::roundToInt (v.vol * 100)) + " %");
    padMix.set (v.padvol);
    padMix.setText (juce::String (juce::roundToInt (v.padvol * 100)) + " %");
    bool changed = rows.size() != v.results.size() || playingId != v.songId || favRev != ctx.crates.revision();
    if (! changed)
        for (size_t i = 0; i < rows.size(); i++) if (rows[i].id != v.results[i].id) { changed = true; break; }
    if (changed)
    {
        bool newRows = rows.size() != v.results.size() || (! rows.empty() && ! v.results.empty() && rows[0].id != v.results[0].id);
        rows = v.results;
        playingId = v.songId;
        favRev = ctx.crates.revision();
        favs = ctx.crates.favIds();
        list.updateContent();
        list.repaint();
        if (newRows) { list.scrollToEnsureRowIsOnscreen (0); saveList.setButtonText ("SAVE AS CRATE"); }
    }
    bool isList = v.resKind == 0 && ! v.lastPlaylistId.empty() && juce::String (v.resHead).startsWith ("PLAYLIST:");
    saveList.setVisible (isList);
    if (pt != pitchTxt || U (v.resHead) != head) { pitchTxt = pt; head = U (v.resHead); repaint(); }
}

void PlayerTab::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& r = rows[(size_t) row];
    bool now = r.kind != 1 && r.id == playingId;
    g.setColour (now ? col::gold.withAlpha (0.22f) : (row % 2 ? juce::Colour (0xff141413) : col::blk));
    g.fillRect (0, 0, w, h);
    g.setColour (col::dim);
    g.drawHorizontalLine (h - 1, 0, (float) w);
    int x = 10;
    if (r.kind != 1)
    {   /* the heart: FAVS */
        bool fav = favs.count (r.id) > 0;
        juce::Path heart;
        float cx = 22, cy = h / 2.0f + 1, s = 8;
        heart.startNewSubPath (cx, cy + s);
        heart.cubicTo (cx - 2.2f * s, cy - 0.2f * s, cx - 0.9f * s, cy - 1.7f * s, cx, cy - 0.55f * s);
        heart.cubicTo (cx + 0.9f * s, cy - 1.7f * s, cx + 2.2f * s, cy - 0.2f * s, cx, cy + s);
        heart.closeSubPath();
        g.setColour (fav ? col::pink : col::mid);
        if (fav) g.fillPath (heart);
        else g.strokePath (heart, juce::PathStrokeType (2));
        x = 46;
    }
    g.setFont (bebas (18));
    g.setColour (r.kind == 1 ? col::gold : col::grey);
    juce::String left = r.kind == 1 ? juce::String ("LIST") : (r.secs > 0 ? U (fmtTime (r.secs, false)) : juce::String());
    g.drawText (left, x, 0, 50, h, juce::Justification::centredLeft);
    x += 56;
    g.setColour (now ? col::gold : col::ink);
    g.setFont (sans (16, now));
    int right = r.kind == 1 ? 120 : 52;
    g.drawFittedText (U (r.title), x, 2, w - x - right - 150, h - 4, juce::Justification::centredLeft, 1, 0.9f);
    g.setColour (col::grey);
    g.setFont (sans (14));
    juce::String ch = U (r.chan);
    if (r.kind == 1 && ! r.count.empty()) ch = U (r.count) + " SONGS  " + ch;
    g.drawFittedText (ch, w - right - 150, 2, 146, h - 4, juce::Justification::centredRight, 1, 0.9f);
    if (r.kind != 1)
    {   /* + : into a crate */
        sketchRect (g, { (float) w - 44, 6, 34, (float) h - 12 }, 6, col::ink, 1.6f, col::blk, false);
        g.setColour (col::gold);
        g.setFont (bebas (24));
        g.drawText ("+", w - 44, 4, 34, h - 8, juce::Justification::centred);
    }
    else
    {
        g.setColour (col::gold);
        g.setFont (bebas (18));
        g.drawText ("OPEN >", w - 110, 0, 100, h, juce::Justification::centredRight);
    }
}

void PlayerTab::addMenu (int row)
{
    const auto& r = rows[(size_t) row];
    CrateSong s;
    s.id = r.id; s.title = r.title; s.chan = r.chan; s.secs = r.secs;
    juce::PopupMenu m;
    m.addSectionHeader ("ADD TO A CRATE");
    auto all = ctx.crates.all();
    for (int i = 0; i < (int) all.size(); i++)
    {
        bool has = std::any_of (all[(size_t) i].songs.begin(), all[(size_t) i].songs.end(), [&] (const CrateSong& x) { return x.id == s.id; });
        m.addItem (100 + i, U (all[(size_t) i].name) + "  (" + juce::String ((int) all[(size_t) i].songs.size()) + ")", ! has, has);
    }
    m.addSeparator();
    m.addItem (1, "New crate with this song...");
    m.addItem (2, "Copy the YouTube link");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition(), [this, s] (int res) {
        if (res >= 100) ctx.crates.add (res - 100, s);
        else if (res == 1)
        {
            auto* aw = new juce::AlertWindow ("NEW CRATE", "Name of the new crate:", juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("n", "MY CRATE");
            aw->addButton ("CREATE", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, s] (int ok) {
                if (ok) { int ci = ctx.crates.create (aw->getTextEditorContents ("n").toStdString()); ctx.crates.add (ci, s); }
            }), true);
        }
        else if (res == 2) juce::SystemClipboard::copyTextToClipboard ("https://www.youtube.com/watch?v=" + U (s.id));
    });
}

void PlayerTab::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& r = rows[(size_t) row];
    int w = list.getWidth() - list.getVerticalScrollBar().getWidth();
    if (r.kind != 1 && e.x < 40)
    {
        CrateSong s;
        s.id = r.id; s.title = r.title; s.chan = r.chan; s.secs = r.secs;
        bool now = ctx.crates.toggleFav (s);
        ctx.eng.setResults (rows, ctx.view.resHead, ctx.view.resKind);
        juce::ignoreUnused (now);
        favRev = 0;
        return;
    }
    if (r.kind != 1 && (e.x > w - 50 || e.mods.isPopupMenu())) { addMenu (row); return; }
    ctx.eng.tapResult (row);
}

/* =================================================================== CHOP */
ChopTab::ChopTab (Ctx& c)
    : Tab (c), wave (c.eng, false), pads (c.eng, false),
      chopBy ({ "HITS", "BEATS", "LAZY" }, [this] (int i) { ctx.eng.setChopBy (i); }),
      assign ({ "NEXT PAD", "PAD HIT" }, [this] (int i) { ctx.eng.setAssign (i); }),
      arm ({ "PADS PLAY", "PADS CHOP" }, [this] (int i) { ctx.eng.setArm (i == 1); }),
      slice ({ "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS" }, [this] (int i) { ctx.eng.setSlice (i); }),
      sens ("SENSITIVITY", 0, 1, 0.01, false, [this] (double x) { ctx.eng.setSens ((float) x); }),
      bpm ("TEMPO", 70, 170, 1, false, [this] (double x) { ctx.eng.setBpm ((int) x); }),
      tap ("TAP CHOP", [this] { ctx.eng.tapChop(); }),
      detect ("DETECT", [this] { ctx.eng.detectBpm(); }),
      hostBpm ("HOST BPM", [this] { ctx.eng.bpmFromHost(); }),
      autoB ("AUTO CHOP", [this] { ctx.eng.autoChop(); }),
      playB ("PLAY", [this] { ctx.eng.playToggle(); }),
      setCue ("SET CUE", [this] { ctx.eng.setCue(); }),
      goCue ("GO TO CUE", [this] { ctx.eng.goCue(); }),
      undo ("UNDO", [this] { ctx.eng.undo(); }),
      clrPad ("CLEAR PAD", [this] { ctx.eng.clearPad(); }),
      clrBank ("CLEAR BANK", [this] { ctx.eng.clearBank(); }),
      stopAll ("STOP ALL", [this] { ctx.eng.stopAll(); }),
      velo ("VELOCITY", [this] { ctx.eng.setVelocity (! ctx.view.velOn); }),
      cpy ("COPY", [this] { ctx.eng.padOp (1); }),
      mov ("MOVE", [this] { ctx.eng.padOp (2); }),
      swp ("SWAP", [this] { ctx.eng.padOp (3); }),
      del ("DELETE", [this] { ctx.eng.padOp (4); }),
      bcpy ("BANK COPY", [this] { ctx.eng.bankOp (1); }),
      bmov ("BANK MOVE", [this] { ctx.eng.bankOp (2); }),
      bswp ("BANK SWAP", [this] { ctx.eng.bankOp (3); }),
      learn ("LEARN PADS", [this] { ctx.eng.learnToggle(); }),
      exportB ("EXPORT WAVS", [this] { ctx.eng.exportPads(); }),
      openEx ("OPEN EXPORTS", [this] {
          juce::File d (U (ctx.view.exportDir));
          if (! d.isDirectory()) d = ctx.eng.dataDir().getChildFile ("Exports");
          d.createDirectory();
          d.startAsProcess();
      })
{
    addAndMakeVisible (wave);
    addAndMakeVisible (pads);
    for (int b = 0; b < 4; b++)
    {
        auto* bt = banks.add (new Btn (juce::String::charToString ((juce::juce_wchar) ('A' + b)), [this, b] { ctx.eng.selectBank (b); }));
        addAndMakeVisible (bt);
    }
    for (auto* comp : std::initializer_list<juce::Component*> { &chopBy, &assign, &arm, &slice, &sens, &bpm, &tap, &detect, &hostBpm, &autoB, &playB, &setCue,
                                                                 &goCue, &undo, &clrPad, &clrBank, &stopAll, &velo, &cpy, &mov, &swp, &del, &bcpy, &bmov,
                                                                 &bswp, &learn, &exportB, &openEx })
        addAndMakeVisible (comp);
    autoB.setAccent (true);
    tap.setAccent (true);
}

void ChopTab::resized()
{
    wave.setBounds (16, 8, 1248, 132);
    for (int b = 0; b < 4; b++) banks[b]->setBounds (16 + b * 154, 150, 150, 46);
    pads.setBounds (16, 206, 616, 440);
    int x0 = 664, cw = 116;
    auto C = [&] (int i, int y, int span = 1, int h = 48) { return juce::Rectangle<int> (x0 + i * cw, y, cw * span - 6, h); };
    chopBy.setBounds (C (0, 170, 5));
    assign.setBounds (C (0, 268, 2, 44));
    arm.setBounds (C (2, 268, 2, 44));
    tap.setBounds (C (4, 268, 1, 44));
    sens.setBounds (C (0, 260, 3, 64));
    bpm.setBounds (C (0, 260, 3, 64));
    detect.setBounds (C (3, 268, 1, 44));
    hostBpm.setBounds (C (4, 268, 1, 44));
    slice.setBounds (C (0, 330, 5, 40));
    autoB.setBounds (C (0, 384, 2, 52));
    playB.setBounds (C (2, 384, 1, 52));
    setCue.setBounds (C (3, 384, 1, 52));
    goCue.setBounds (C (4, 384, 1, 52));
    undo.setBounds (C (0, 446));
    clrPad.setBounds (C (1, 446));
    clrBank.setBounds (C (2, 446));
    stopAll.setBounds (C (3, 446));
    velo.setBounds (C (4, 446));
    cpy.setBounds (C (0, 502));
    mov.setBounds (C (1, 502));
    swp.setBounds (C (2, 502));
    del.setBounds (C (3, 502));
    learn.setBounds (C (4, 502));
    bcpy.setBounds (C (0, 558));
    bmov.setBounds (C (1, 558));
    bswp.setBounds (C (2, 558));
    exportB.setBounds (C (3, 558));
    openEx.setBounds (C (4, 558));
}

void ChopTab::paint (juce::Graphics& g)
{
    sectionBox (g, { 648, 152, 616, 482 }, "CHOP");
    g.setColour (col::grey);
    g.setFont (bebas (18));
    g.drawFittedText (hint, 664, 222, 576, 40, juce::Justification::centredLeft, 2);
    g.setFont (bebas (15));
    g.setColour (col::mid);
    g.drawText ("DRAG A PAD INTO YOUR DAW = A WAV OF THE CHOP", 16, 640, 616, 16, juce::Justification::centredLeft);
}

void ChopTab::refresh()
{
    const auto& v = ctx.view;
    wave.update (v);
    pads.update (v);
    for (int b = 0; b < 4; b++)
    {
        int used = 0;
        for (int i = 0; i < 16; i++) used += v.pads[b * 16 + i].used;
        juce::String t = juce::String::charToString ((juce::juce_wchar) ('A' + b)) + "  " + juce::String (used);
        if (v.pendBank && b != v.bank) t = juce::String::charToString ((juce::juce_wchar) ('A' + b)) + "  ?";
        banks[b]->setButtonText (t);
        banks[b]->setOn (b == v.bank);
    }
    chopBy.set (v.chopBy);
    assign.set (v.assign);
    arm.set (v.arm ? 1 : 0);
    slice.set (v.slice);
    sens.set (v.sens);
    sens.setText (juce::String (juce::roundToInt (v.sens * 100)) + " %");
    bpm.set (v.bpm);
    bpm.setText (juce::String (v.bpm) + " BPM" + (v.hostBpm > 0 ? "   (HOST " + juce::String (v.hostBpm, 1) + ")" : juce::String()));
    bool lazy = v.chopBy == CH_LAZY, hits = v.chopBy == CH_HITS, beats = v.chopBy == CH_BEATS;
    assign.setVisible (lazy); arm.setVisible (lazy); tap.setVisible (lazy);
    sens.setVisible (hits);
    bpm.setVisible (beats); detect.setVisible (beats); hostBpm.setVisible (beats); slice.setVisible (beats);
    autoB.setEnabled (! lazy);
    playB.setButtonText (v.playing ? "STOP" : "PLAY");
    playB.setOn (v.playing);
    velo.setOn (v.velOn);
    cpy.setOn (v.pendOp == 1); mov.setOn (v.pendOp == 2); swp.setOn (v.pendOp == 3);
    bcpy.setOn (v.pendBank == 1); bmov.setOn (v.pendBank == 2); bswp.setOn (v.pendBank == 3);
    learn.setOn (v.learn != 0);
    learn.setButtonText (v.learn == 1 ? "HIT PAD 1" : v.learn == 2 ? "HIT PAD 16" : "LEARN PADS");
    exportB.setButtonText (v.exporting ? "EXPORTING..." : "EXPORT WAVS");
    juce::String h = lazy ? (v.assign ? "LAZY: PLAY THE SONG, THE PAD YOU HIT GETS THE CHOP RIGHT THERE"
                                      : "LAZY: PLAY THE SONG, EVERY PAD HIT (OR TAP) CHOPS TO THE NEXT PAD")
                          : hits ? "HITS: SET THE CUE, THEN AUTO CHOP FINDS 16 DRUM HITS FROM THERE"
                                 : "BEATS: SET THE CUE ON A DOWNBEAT, THEN AUTO CHOP CUTS EVEN SLICES";
    h << "     PADS: NOTES " << v.mapFirst << "-" << v.mapLast;
    if (h != hint) { hint = h; repaint(); }
}

/* =================================================================== WAVE */
WaveTab::WaveTab (Ctx& c)
    : Tab (c), wave (c.eng, true), pads (c.eng, true),
      viewSeg ({ "SONG VIEW", "PAD VIEW" }, [this] (int i) { ctx.eng.setZoom (i); }),
      bankSeg ({ "A", "B", "C", "D" }, [this] (int i) { ctx.eng.selectBank (i); }),
      mode ({ "ONE SHOT", "GATE" }, [this] (int i) { ctx.eng.setMode (i); }),
      fit ("FIT", [this] { ctx.eng.fit(); }),
      prev ("< PAD", [this] { ctx.eng.padStep (-1); }),
      next ("PAD >", [this] { ctx.eng.padStep (1); }),
      playPad ("PLAY PAD", [this] { ctx.eng.playSelected(); }),
      rev ("REVERSE", [this] { ctx.eng.setReverse (! ctx.view.pads[ctx.view.sel].rev); }),
      grpM ("-", [this] { ctx.eng.groupStep (-1); }),
      grpP ("+", [this] { ctx.eng.groupStep (1); }),
      a ("ATTACK", 0, 1, 0.001, true, [this] (double x) { ctx.eng.setAttack ((int) std::lround (x * x * A_MAX)); }),
      d ("DECAY", 0, 1, 0.001, true, [this] (double x) { ctx.eng.setDecay ((int) std::lround (x * x * D_MAX)); }),
      s ("SUSTAIN", 0, 1, 0.01, true, [this] (double x) { ctx.eng.setSustain ((float) x); }),
      r ("RELEASE", 0, 1, 0.001, true, [this] (double x) { ctx.eng.setRelease ((int) std::lround (x * x * R_MAX)); }),
      pitch ("PITCH", -24, 24, 1, true, [this] (double x) { ctx.eng.setPitch ((int) x); }),
      level ("LEVEL", 0, 1, 0.01, true, [this] (double x) { ctx.eng.setLevel ((float) x); })
{
    addAndMakeVisible (wave);
    addAndMakeVisible (env);
    addAndMakeVisible (pads);
    for (auto* comp : std::initializer_list<juce::Component*> { &viewSeg, &bankSeg, &mode, &fit, &prev, &next, &playPad, &rev, &grpM, &grpP, &a, &d, &s, &r, &pitch, &level })
        addAndMakeVisible (comp);
    playPad.setAccent (true);
    const int steps[4] = { -50, -5, 5, 50 };
    for (int k = 0; k < 8; k++)
    {
        int which = k / 4, ms = steps[k % 4];
        auto* b = fine.add (new Btn ((ms > 0 ? "+" : "") + juce::String (ms), [this, which, ms] { ctx.eng.moveBounds (which, ms); }));
        addAndMakeVisible (b);
    }
}

void WaveTab::resized()
{
    viewSeg.setBounds (884, 4, 260, 40);
    fit.setBounds (1150, 4, 114, 40);
    wave.setBounds (16, 50, 1248, 250);
    for (int k = 0; k < 4; k++) fine[k]->setBounds (96 + k * 74, 310, 70, 42);
    for (int k = 0; k < 4; k++) fine[4 + k]->setBounds (968 + k * 74, 310, 70, 42);
    prev.setBounds (406, 310, 110, 42);
    playPad.setBounds (524, 310, 232, 42);
    next.setBounds (764, 310, 110, 42);
    env.setBounds (32, 388, 528, 112);
    a.setBounds (32, 504, 126, 126);
    d.setBounds (166, 504, 126, 126);
    s.setBounds (300, 504, 126, 126);
    r.setBounds (434, 504, 126, 126);
    pitch.setBounds (604, 386, 160, 150);
    level.setBounds (772, 386, 160, 150);
    mode.setBounds (604, 544, 328, 42);
    rev.setBounds (604, 594, 150, 42);
    grpM.setBounds (762, 594, 44, 42);
    grpP.setBounds (888, 594, 44, 42);
    bankSeg.setBounds (968, 372, 296, 36);
    pads.setBounds (968, 414, 296, 226);
}

void WaveTab::paint (juce::Graphics& g)
{
    g.setColour (col::gold);
    g.setFont (bebas (28));
    g.drawText (padTxt, 16, 6, 520, 36, juce::Justification::centredLeft);
    g.setColour (col::ink);
    g.setFont (bebas (22));
    g.drawText (posTxt, 520, 6, 360, 36, juce::Justification::centredLeft);
    g.setColour (col::grey);
    g.setFont (bebas (20));
    g.drawText ("START", 16, 310, 76, 42, juce::Justification::centredLeft);
    g.drawText ("END", 884, 310, 80, 42, juce::Justification::centredRight);
    sectionBox (g, { 16, 372, 560, 266 }, "ENVELOPE");
    sectionBox (g, { 592, 372, 360, 266 }, "SOUND");
    g.setColour (col::ink);
    g.setFont (bebas (19));
    g.drawText (grpTxt, 806, 594, 82, 42, juce::Justification::centred);
}

void WaveTab::refresh()
{
    const auto& v = ctx.view;
    const auto& p = v.pads[v.sel];
    if (p.used && p.own && v.zoom == 0) ctx.eng.setZoom (1);   /* a pad with its own audio is shown in PAD view */
    wave.update (v);
    env.update (v);
    pads.update (v);
    viewSeg.set (v.zoom);
    bankSeg.set (v.bank);
    mode.set (p.mode);
    rev.setOn (p.rev);
    a.set (std::sqrt (p.att / (double) A_MAX)); a.setText (juce::String (p.att) + " MS");
    d.set (std::sqrt (p.dec / (double) D_MAX)); d.setText (juce::String (p.dec) + " MS");
    s.set (p.sus); s.setText (juce::String (juce::roundToInt (p.sus * 100)) + " %");
    r.set (std::sqrt (p.rel / (double) R_MAX)); r.setText (juce::String (p.rel) + " MS");
    pitch.set (p.pitch); pitch.setText ((p.pitch > 0 ? "+" : "") + juce::String (p.pitch) + " ST");
    level.set (p.level); level.setText (juce::String (juce::roundToInt (p.level * 100)) + " %");
    juce::String nm = juce::String::charToString ((juce::juce_wchar) ('A' + v.sel / 16)) + juce::String (v.sel % 16 + 1);
    juce::String pt = p.used ? "PAD " + nm + (p.own ? "  (OWN SAMPLE)" : "") : "PAD " + nm + " IS EMPTY: < PAD / PAD > OR A CHOPPED PAD";
    juce::String ps = p.used ? U (fmtTime (p.start / (double) SR, true)) + "  -  " + U (fmtTime (p.end / (double) SR, true)) + "   (" + juce::String ((p.end - p.start) / (double) SR, 3) + " S)" : juce::String();
    juce::String gt = p.grp ? "GROUP " + juce::String (p.grp) : "NO GROUP";
    for (auto* comp : std::initializer_list<juce::Component*> { &mode, &rev, &grpM, &grpP, &a, &d, &s, &r, &pitch, &level, &playPad })
        comp->setEnabled (p.used);
    for (auto* b : fine) b->setEnabled (p.used);
    if (pt != padTxt || ps != posTxt || gt != grpTxt) { padTxt = pt; posTxt = ps; grpTxt = gt; repaint(); }
}

/* =================================================================== ISOLATE */
IsoTab::IsoTab (Ctx& c)
    : Tab (c),
      toSel ("APPLY TO THE SELECTED PAD", [this] { ctx.eng.isoSelected(); }),
      toBank ("APPLY TO THE WHOLE BANK", [this] { ctx.eng.isoBank(); }),
      pads (c.eng, true)
{
    for (int i = 0; i < NISO; i++)
    {
        auto* b = modes.add (new Btn (ISO_NAMES[i], [this, i] { setParam (ctx.proc, "iso", i / (float) (NISO - 1)); ctx.eng.setIso (i); }));
        addAndMakeVisible (b);
    }
    addAndMakeVisible (toSel);
    addAndMakeVisible (toBank);
    addAndMakeVisible (pads);
}
void IsoTab::resized()
{
    for (int i = 0; i < NISO; i++)
    {
        int row = i / 4, cc = i % 4;
        modes[i]->setBounds (40 + cc * 222, 54 + row * 104, 212, 92);
    }
    toSel.setBounds (40, 384, 430, 60);
    toBank.setBounds (482, 384, 430, 60);
    pads.setBounds (948, 54, 300, 300);
}
void IsoTab::paint (juce::Graphics& g)
{
    sectionBox (g, { 16, 14, 1248, 620 }, "ISOLATE");
    g.setColour (col::gold);
    g.setFont (bebas (30));
    if (cur >= 0) g.drawFittedText (ISO_HINTS[cur], 40, 270, 880, 90, juce::Justification::centredLeft, 2);
    g.setColour (col::grey);
    g.setFont (bebas (20));
    g.drawFittedText ("THE SONG ON THE DECK PLAYS THROUGH THE FILTER AT ONCE, AND EVERY NEW CHOP TAKES IT. FOR CHOPS YOU MADE BEFORE, PICK THE PAD (RIGHT) "
                      "AND APPLY. EXPORTED WAVS AND DRAGGED CHOPS KEEP THEIR FILTER.", 40, 470, 1200, 60, juce::Justification::topLeft, 3);
    g.setColour (col::mid);
    g.drawText ("LIGHT FILTERS, NO AI STEMS: WORKS BEST ON WIDE STEREO MIXES", 40, 590, 1200, 26, juce::Justification::left);
}
void IsoTab::refresh()
{
    const auto& v = ctx.view;
    for (int i = 0; i < NISO; i++) modes[i]->setOn (i == v.iso);
    pads.update (v);
    if (v.iso != cur) { cur = v.iso; repaint(); }
}

/* =================================================================== INFO */
InfoTab::InfoTab (Ctx& c)
    : Tab (c),
      upd ("UPDATE YT-DLP", [this] { ctx.eng.updateYtDlp(); }),
      deno ("INSTALL DENO", [this] { ctx.eng.installDeno(); }),
      openData ("OPEN DATA FOLDER", [this] { ctx.eng.dataDir().startAsProcess(); }),
      openEx ("OPEN EXPORTS", [this] { auto d = ctx.eng.dataDir().getChildFile ("Exports"); d.createDirectory(); d.startAsProcess(); })
{
    for (auto* b : { &upd, &deno, &openData, &openEx }) addAndMakeVisible (b);
}
void InfoTab::resized()
{
    upd.setBounds (400, 572, 200, 50);
    deno.setBounds (610, 572, 200, 50);
    openData.setBounds (820, 572, 220, 50);
    openEx.setBounds (1050, 572, 200, 50);
}
void InfoTab::paint (juce::Graphics& g)
{
    sectionBox (g, { 16, 14, 1248, 620 }, {});
    g.setOpacity (1.0f);
    auto jf = jfLogo();
    float h = 420, w = h * jf.getWidth() / (float) jf.getHeight();
    g.drawImage (jf, juce::Rectangle<float> (190 - w / 2, 40, w, h));
    g.setColour (col::gold);
    g.setFont (bebas (40));
    g.drawText ("CREATOR: JJ DA PLUG", 30, 470, 320, 46, juce::Justification::centred);
    g.setColour (col::grey);
    g.setFont (bebas (22));
    g.drawText ("CRATE RIPPA DESKTOP  V" JucePlugin_VersionString, 30, 516, 320, 30, juce::Justification::centred);

    g.setColour (col::gold);
    g.setFont (bebas (30));
    g.drawText ("SO GEHT'S", 400, 30, 600, 36, juce::Justification::left);
    g.setColour (col::ink);
    g.setFont (sans (16));
    const char* lines[] = {
        "1.  PLAYER: Song oder Künstler suchen (SONGS) oder Playlists (LISTS). Ein Klick spielt den Song auf dem Plattenspieler.",
        "2.  Auf der Platte ziehen = scratchen. Pitch-Fader rechts, RANGE 8 / 16 / 50 % oder OCT, KEY LOCK hält die Tonhöhe.",
        "3.  Herz = FAVS, + = in eine eigene Crate. Im Reiter CRATE: Playlists anlegen, sortieren, BPM / Key / Tags / Notizen.",
        "4.  CHOP: HITS (findet 16 Drum-Hits ab dem Cue), BEATS (gleiche Slices im Tempo) oder LAZY (Pads beim Abspielen drücken).",
        "5.  WAVE: Start und Ende ziehen, ADSR, Pitch, Reverse, Mute Groups. ISOLATE: leichte Filter für Song und Pads.",
        "6.  EXPORT WAVS speichert alle Chops (A01 ... D16). Oder ein Pad direkt in die DAW ziehen: das ist eine WAV des Chops.",
        "7.  MIDI: C1 (36) = A1 ... chromatisch. LEARN PADS: erstes und letztes Pad drücken, auch Tonleiter-Layouts.",
    };
    int y = 72;
    for (auto* l : lines) { g.drawFittedText (juce::String::fromUTF8 (l), 400, y, 850, 44, juce::Justification::topLeft, 2); y += 48; }
    g.setColour (col::gold);
    g.setFont (bebas (22));
    g.drawText ("HELFER", 400, y + 4, 600, 26, juce::Justification::left);
    g.setColour (col::ink);
    g.setFont (sans (15));
    juce::String helper = "yt-dlp: " + ytv + "   |   ffmpeg: " + (hasFfmpeg ? "found" : "not needed on Windows/macOS (M4A)") + "   |   Deno: " + (hasDeno ? "installed" : "not installed");
    g.drawFittedText (helper, 400, y + 32, 850, 22, juce::Justification::left, 1);
    g.setColour (col::grey);
    g.drawFittedText (juce::String::fromUTF8 ("yt-dlp lädt beim ersten Suchen automatisch (GitHub). Bricht YouTube etwas, hilft meist UPDATE YT-DLP. "
                                              "Sagt der Status \"INSTALL DENO\", einmal INSTALL DENO drücken. Daten: Dokumente/JJ DA PLUG/Crate Rippa."),
                      400, y + 58, 850, 44, juce::Justification::topLeft, 2);
    g.setColour (col::mid);
    g.setFont (sans (13));
    g.drawFittedText (juce::String::fromUTF8 ("yt-dlp (Unlicense), Deno (MIT), ffmpeg (LGPL/GPL) laufen als eigene Programme. Bebas Neue: SIL Open Font License. "
                                              "Nur Inhalte nutzen, an denen du die Rechte hast oder die frei sind."),
                      400, 540, 850, 30, juce::Justification::topLeft, 2);
}
void InfoTab::refresh()
{
    if (tick++ % 30) return;   /* once a second is plenty */
    auto bin = ctx.eng.dataDir().getChildFile ("bin");
    bool ff = bin.getChildFile ("ffmpeg").existsAsFile() || bin.getChildFile ("ffmpeg.exe").existsAsFile() || juce::File ("/opt/homebrew/bin/ffmpeg").existsAsFile()
              || juce::File ("/usr/local/bin/ffmpeg").existsAsFile() || juce::File ("/usr/bin/ffmpeg").existsAsFile();
    bool dn = bin.getChildFile ("deno").existsAsFile() || bin.getChildFile ("deno.exe").existsAsFile();
    juce::String y = U (ctx.view.ytdlpVer == "-" ? std::string ("loads on the first search") : ctx.view.ytdlpVer);
    if (ff != hasFfmpeg || dn != hasDeno || y != ytv) { hasFfmpeg = ff; hasDeno = dn; ytv = y; repaint(); }
    deno.setButtonText (dn ? "DENO INSTALLED" : "INSTALL DENO");
}
} // namespace cr
