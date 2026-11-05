/* CRATE RIPPA Desktop (JJ DA PLUG): the CRATE tab. FAVS and your own playlists: add, reorder (buttons or drag), move between
 * crates (drag onto a crate), BPM / key / tags / note per song, YouTube playlists saved as crates, export and import. MIT. */
#include "Tabs.h"
#include "PluginProcessor.h"
#include <algorithm>

namespace cr
{
static juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
enum { C_FAV = 1, C_POS, C_OFF, C_TITLE, C_CHAN, C_TIME, C_BPM, C_KEY, C_TAGS };

/* ------------------------------------------------------------------ the two list models */
struct CrateTab::CrateModel : juce::ListBoxModel
{
    CrateTab& t;
    explicit CrateModel (CrateTab& o) : t (o) {}
    int getNumRows() override { return (int) t.crates.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= (int) t.crates.size()) return;
        const auto& c = t.crates[(size_t) row];
        bool drop = t.dropRow == -100 - row;
        g.setColour (drop ? col::pink.withAlpha (0.4f) : sel ? col::gold : (row % 2 ? juce::Colour (0xff141413) : col::blk));
        g.fillRect (0, 0, w, h);
        g.setColour (sel ? col::blk : (c.fixed ? col::pink : col::ink));
        g.setFont (bebas (24));
        juce::String nm = c.fixed ? juce::String::fromUTF8 ("\xe2\x99\xa5  FAVS") : U (c.name);
        g.drawFittedText (nm, 12, 0, w - 70, h, juce::Justification::centredLeft, 1);
        g.setColour (sel ? col::blk : col::grey);
        g.setFont (bebas (20));
        g.drawText (juce::String ((int) c.songs.size()), w - 60, 0, 50, h, juce::Justification::centredRight);
        if (! c.source.empty() && ! sel)
        {
            g.setColour (col::mid);
            g.setFont (bebas (13));
            g.drawText ("YT", w - 82, 0, 20, h, juce::Justification::centred);
        }
    }
    void selectedRowsChanged (int last) override { if (last >= 0) t.crateChosen (last); }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) t.crateMenu (row); }
};

struct CrateTab::SongModel : juce::TableListBoxModel
{
    CrateTab& t;
    explicit SongModel (CrateTab& o) : t (o) {}
    int getNumRows() override { return (int) t.order.size(); }
    void paintRowBackground (juce::Graphics& g, int row, int w, int h, bool sel) override
    {
        auto* s = t.songAt (row);
        bool now = s && s->id == t.loadedId;
        g.setColour (sel ? col::gold.withAlpha (0.35f) : now ? col::gold.withAlpha (0.15f) : (row % 2 ? juce::Colour (0xff141413) : col::blk));
        g.fillRect (0, 0, w, h);
        if (t.dropRow == row) { g.setColour (col::pink); g.fillRect (0, 0, w, 3); }
        if (t.dropRow == (int) t.order.size() && row == (int) t.order.size() - 1) { g.setColour (col::pink); g.fillRect (0, h - 3, w, 3); }
    }
    void paintCell (juce::Graphics& g, int row, int c, int w, int h, bool) override
    {
        auto* s = t.songAt (row);
        if (! s) return;
        g.setColour (col::ink);
        g.setFont (sans (15));
        juce::String txt;
        switch (c)
        {
            case C_FAV:
            {
                bool fav = t.favs.count (s->id) > 0;
                g.setColour (fav ? col::pink : col::mid);
                g.setFont (sans (18));
                g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x99\xa5" : "\xe2\x99\xa1"), 0, 0, w, h, juce::Justification::centred);
                return;
            }
            case C_POS: g.setColour (col::grey); g.setFont (bebas (17)); txt = juce::String (t.order[(size_t) row] + 1); break;
            case C_OFF:
                if (t.isCached (s->id)) { g.setColour (col::green); g.fillEllipse (w / 2.0f - 5, h / 2.0f - 5, 10, 10); }
                return;
            case C_TITLE: g.setColour (s->id == t.loadedId ? col::gold : col::ink); g.setFont (sans (15, s->id == t.loadedId)); txt = U (s->title); break;
            case C_CHAN: g.setColour (col::grey); txt = U (s->chan); break;
            case C_TIME: g.setFont (bebas (18)); txt = s->secs > 0 ? U (fmtTime (s->secs, false)) : juce::String(); break;
            case C_BPM: g.setFont (bebas (18)); g.setColour (col::gold); txt = s->bpm > 0 ? juce::String (s->bpm) : juce::String(); break;
            case C_KEY: g.setFont (bebas (18)); g.setColour (col::gold); txt = U (s->key); break;
            case C_TAGS: g.setColour (col::grey); g.setFont (sans (14)); txt = U (s->tags); break;
            default: break;
        }
        g.drawFittedText (txt, 6, 0, w - 10, h, juce::Justification::centredLeft, 1, 0.85f);
    }
    void cellClicked (int row, int c, const juce::MouseEvent& e) override { t.songClicked (row, c, e); }
    void cellDoubleClicked (int row, int, const juce::MouseEvent&) override { t.songDoubleClicked (row); }
    void sortOrderChanged (int c, bool fwd) override { t.sortBy (c, fwd); }
    void selectedRowsChanged (int) override { t.songSelectionChanged(); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.isEmpty()) return {};
        return "crate-songs:" + juce::String (rows[0]);
    }
};

/* ------------------------------------------------------------------ the tab */
CrateTab::CrateTab (Ctx& c)
    : Tab (c),
      crateModel (std::make_unique<CrateModel> (*this)),
      songModel (std::make_unique<SongModel> (*this)),
      newB ("NEW", [this] {
          auto* aw = new juce::AlertWindow ("NEW CRATE", "Name of the new crate:", juce::MessageBoxIconType::NoIcon);
          aw->addTextEditor ("n", "MY CRATE");
          aw->addButton ("CREATE", 1, juce::KeyPress (juce::KeyPress::returnKey));
          aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
          aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int ok) {
              if (! ok) return;
              cur = ctx.crates.create (aw->getTextEditorContents ("n").toStdString());
              seenRev = 0;
          }), true);
      }),
      renB ("RENAME", [this] {
          if (cur <= 0 || cur >= (int) crates.size()) return;
          auto* aw = new juce::AlertWindow ("RENAME CRATE", "New name:", juce::MessageBoxIconType::NoIcon);
          aw->addTextEditor ("n", U (crates[(size_t) cur].name));
          aw->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
          aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
          int ci = cur;
          aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, ci] (int ok) {
              if (ok) ctx.crates.rename (ci, aw->getTextEditorContents ("n").toStdString());
          }), true);
      }),
      delB ("DELETE", [this] {
          if (cur <= 0 || cur >= (int) crates.size()) return;
          int ci = cur;
          juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "DELETE CRATE",
                                              "Delete the crate \"" + U (crates[(size_t) ci].name) + "\"? The songs stay on YouTube and in your other crates.",
                                              "DELETE", "CANCEL", this, juce::ModalCallbackFunction::create ([this, ci] (int ok) {
                                                  if (ok) { ctx.crates.erase (ci); cur = 0; seenRev = 0; }
                                              }));
      }),
      impB ("IMPORT", [this] {
          chooser = std::make_unique<juce::FileChooser> ("Import a crate", ctx.crates.file().getParentDirectory(), "*.json");
          chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
              auto f = fc.getResult();
              if (! f.existsAsFile()) return;
              int ci = ctx.crates.importCrate (f);
              if (ci >= 0) { cur = ci; seenRev = 0; }
              else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "IMPORT", "This is not a Crate Rippa crate file.");
          });
      }),
      expB ("EXPORT", [this] {
          if (cur < 0 || cur >= (int) crates.size()) return;
          auto nm = safeName (crates[(size_t) cur].name);
          chooser = std::make_unique<juce::FileChooser> ("Export the crate", ctx.crates.file().getParentDirectory().getChildFile (juce::String (nm) + ".crate.json"), "*.json");
          int ci = cur;
          chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, ci] (const juce::FileChooser& fc) {
              auto f = fc.getResult();
              if (f != juce::File()) ctx.crates.exportCrate (ci, f);
          });
      }),
      playB ("PLAY", [this] { int r = table.getSelectedRow(); if (r >= 0) songDoubleClicked (r); }),
      toPlayer ("SHOW IN PLAYER", [this] {
          if (cur < 0 || cur >= (int) crates.size()) return;
          std::vector<Result> rs;
          for (int i : order)
          {
              const auto& s = crates[(size_t) cur].songs[(size_t) i];
              Result r; r.id = s.id; r.title = s.title; r.chan = s.chan; r.secs = s.secs;
              rs.push_back (r);
          }
          ctx.eng.setResults (rs, upperStr ("CRATE: " + crates[(size_t) cur].name + "  (" + std::to_string (rs.size()) + " SONGS)"), 0);
          ctx.showTab (0);
      }),
      remB ("REMOVE", [this] {
          int si = selSongIndex();
          if (si >= 0) { ctx.crates.remove (cur, si); table.deselectAllRows(); }
      }),
      upB ("UP", [this] { moveSel (-1); }),
      downB ("DOWN", [this] { moveSel (1); }),
      saveInfo ("SAVE", [this] {
          int si = selSongIndex();
          if (si < 0) return;
          ctx.crates.setInfo (cur, si, eBpm.getText().getIntValue(), eKey.getText().toUpperCase().trim().toStdString(), eTags.getText().trim().toStdString(),
                              eNote.getText().trim().toStdString());
          ctx.eng.setResults (ctx.view.results, ctx.view.resHead, ctx.view.resKind);
      }),
      bpmDeck ("BPM FROM THE DECK", [this] {
          auto* s = songAt (table.getSelectedRow());
          if (! s) return;
          if (s->id != ctx.view.songId) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "BPM", "Load this song first, then DETECT the tempo in the CHOP tab (BEATS)."); return; }
          eBpm.setText (juce::String (ctx.view.bpm));
      })
{
    crateList.setModel (crateModel.get());
    crateList.setRowHeight (44);
    crateList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (crateList);
    table.setModel (songModel.get());
    table.setRowHeight (34);
    table.setMultipleSelectionEnabled (false);
    auto& hd = table.getHeader();
    hd.addColumn (juce::String::fromUTF8 ("\xe2\x99\xa5"), C_FAV, 34, 34, 34, juce::TableHeaderComponent::notSortable | juce::TableHeaderComponent::notResizable);
    hd.addColumn ("#", C_POS, 44, 40, 60);
    hd.addColumn ("OFF", C_OFF, 40, 40, 40, juce::TableHeaderComponent::notResizable | juce::TableHeaderComponent::notSortable);
    hd.addColumn ("TITLE", C_TITLE, 300, 120, 700);
    hd.addColumn ("CHANNEL", C_CHAN, 150, 60, 400);
    hd.addColumn ("TIME", C_TIME, 64, 50, 90);
    hd.addColumn ("BPM", C_BPM, 56, 46, 80);
    hd.addColumn ("KEY", C_KEY, 70, 50, 120);
    hd.addColumn ("TAGS", C_TAGS, 110, 60, 400);
    hd.setSortColumnId (C_POS, true);
    hd.setPopupMenuActive (false);
    addAndMakeVisible (table);
    for (auto* b : std::initializer_list<juce::Component*> { &newB, &renB, &delB, &impB, &expB, &playB, &toPlayer, &remB, &upB, &downB, &saveInfo, &bpmDeck })
        addAndMakeVisible (b);
    playB.setAccent (true);
    filter.setFont (sans (17));
    filter.setTextToShowWhenEmpty ("Filter: title, channel, tag, key...", col::grey);
    filter.setIndents (10, 8);
    filter.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (filter);
    for (auto* e : { &eBpm, &eKey, &eTags, &eNote })
    {
        e->setFont (sans (16));
        e->setIndents (8, 6);
        addAndMakeVisible (e);
    }
    eBpm.setInputRestrictions (3, "0123456789");
    eKey.setTextToShowWhenEmpty ("e.g. A MIN", col::mid);
    eTags.setTextToShowWhenEmpty ("soul, keys, drums...", col::mid);
    eNote.setTextToShowWhenEmpty ("a note: \"drums at 1:20\"...", col::mid);
    eNote.setMultiLine (true);
    eNote.setReturnKeyStartsNewLine (true);
    for (auto* l : { &lBpm, &lKey, &lTags, &lNote })
    {
        l->setFont (bebas (18));
        l->setColour (juce::Label::textColourId, col::grey);
        addAndMakeVisible (l);
    }
    lBpm.setText ("BPM", juce::dontSendNotification);
    lKey.setText ("KEY", juce::dontSendNotification);
    lTags.setText ("TAGS", juce::dontSendNotification);
    lNote.setText ("NOTE", juce::dontSendNotification);
}

CrateTab::~CrateTab()
{
    table.setModel (nullptr);
    crateList.setModel (nullptr);
}

void CrateTab::resized()
{
    crateList.setBounds (28, 40, 306, 470);
    newB.setBounds (28, 520, 100, 46);
    renB.setBounds (132, 520, 100, 46);
    delB.setBounds (236, 520, 98, 46);
    impB.setBounds (28, 574, 152, 46);
    expB.setBounds (184, 574, 150, 46);
    filter.setBounds (378, 36, 330, 42);
    playB.setBounds (716, 36, 100, 42);
    toPlayer.setBounds (822, 36, 170, 42);
    upB.setBounds (998, 36, 70, 42);
    downB.setBounds (1072, 36, 84, 42);
    remB.setBounds (1160, 36, 92, 42);
    table.setBounds (378, 88, 874, 360);
    lBpm.setBounds (378, 462, 60, 26);
    eBpm.setBounds (378, 488, 90, 38);
    lKey.setBounds (478, 462, 80, 26);
    eKey.setBounds (478, 488, 130, 38);
    lTags.setBounds (618, 462, 80, 26);
    eTags.setBounds (618, 488, 330, 38);
    bpmDeck.setBounds (958, 484, 200, 44);
    saveInfo.setBounds (1164, 484, 88, 44);
    lNote.setBounds (378, 534, 80, 26);
    eNote.setBounds (378, 560, 874, 64);
}

void CrateTab::paint (juce::Graphics& g)
{
    sectionBox (g, { 16, 14, 330, 620 }, "CRATES");
    juce::String nm = cur >= 0 && cur < (int) crates.size() ? (crates[(size_t) cur].fixed ? juce::String ("FAVS") : U (crates[(size_t) cur].name)) : juce::String();
    sectionBox (g, { 362, 14, 902, 620 }, nm.toUpperCase());
    if (order.empty())
    {
        g.setColour (col::grey);
        g.setFont (bebas (22));
        g.drawFittedText (filter.isEmpty() ? (cur == 0 ? "NO FAVS YET: CLICK THE HEART NEXT TO A SONG IN THE PLAYER" : "EMPTY: ADD SONGS WITH + IN THE PLAYER, OR DRAG THEM HERE FROM ANOTHER CRATE")
                                           : "NOTHING MATCHES THE FILTER",
                          400, 220, 830, 60, juce::Justification::centred, 2);
    }
}

const CrateSong* CrateTab::songAt (int row) const
{
    if (cur < 0 || cur >= (int) crates.size() || row < 0 || row >= (int) order.size()) return nullptr;
    return &crates[(size_t) cur].songs[(size_t) order[(size_t) row]];
}
int CrateTab::selSongIndex() const
{
    int r = table.getSelectedRow();
    return r >= 0 && r < (int) order.size() ? order[(size_t) r] : -1;
}

bool CrateTab::isCached (const std::string& id)
{
    auto it = cachedMemo.find (id);
    if (it != cachedMemo.end()) return it->second;
    bool c = ctx.eng.cacheFileFor (id).existsAsFile();
    cachedMemo[id] = c;
    return c;
}

void CrateTab::rebuild()
{
    order.clear();
    if (cur < 0 || cur >= (int) crates.size()) cur = 0;
    const auto& songs = crates[(size_t) cur].songs;
    auto f = filter.getText().trim().toLowerCase();
    for (int i = 0; i < (int) songs.size(); i++)
    {
        const auto& s = songs[(size_t) i];
        if (f.isNotEmpty())
        {
            auto hay = (U (s.title) + " " + U (s.chan) + " " + U (s.tags) + " " + U (s.key) + " " + U (s.note)).toLowerCase();
            if (! hay.contains (f)) continue;
        }
        order.push_back (i);
    }
    auto key = [&] (int i) -> const CrateSong& { return songs[(size_t) i]; };
    auto cmpS = [] (const std::string& a, const std::string& b) { return juce::String::fromUTF8 (a.c_str()).compareNatural (juce::String::fromUTF8 (b.c_str())) < 0; };
    switch (sortCol)
    {
        case C_TITLE: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return cmpS (key (a).title, key (b).title); }); break;
        case C_CHAN: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return cmpS (key (a).chan, key (b).chan); }); break;
        case C_TIME: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return key (a).secs < key (b).secs; }); break;
        case C_BPM: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return key (a).bpm < key (b).bpm; }); break;
        case C_KEY: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return cmpS (key (a).key, key (b).key); }); break;
        case C_TAGS: std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return cmpS (key (a).tags, key (b).tags); }); break;
        default: break;
    }
    if (! sortFwd) std::reverse (order.begin(), order.end());
    bool manual = sortCol == C_POS && sortFwd && filter.isEmpty();
    upB.setEnabled (manual);
    downB.setEnabled (manual);
    renB.setEnabled (cur > 0);
    delB.setEnabled (cur > 0);
    table.updateContent();
    table.repaint();
    crateList.repaint();
    repaint();
}

void CrateTab::refresh()
{
    const auto& v = ctx.view;
    static juce::uint32 lastCheck = 0;
    auto nowMs = juce::Time::getMillisecondCounter();
    if (nowMs - lastCheck > 1500) { ctx.crates.reloadIfChanged(); lastCheck = nowMs; }
    if (nowMs - cachedMemoTime > 3000) { cachedMemo.clear(); cachedMemoTime = nowMs; table.repaint(); }
    if (v.songId != loadedId) { loadedId = v.songId; cachedMemo.clear(); table.repaint(); }
    if (ctx.crates.revision() != seenRev)
    {
        seenRev = ctx.crates.revision();
        std::string keepId;
        if (auto* s = songAt (table.getSelectedRow())) keepId = s->id;
        crates = ctx.crates.all();
        favs = ctx.crates.favIds();
        if (cur >= (int) crates.size()) cur = (int) crates.size() - 1;
        crateList.updateContent();
        crateList.selectRow (cur, true, true);
        rebuild();
        table.deselectAllRows();
        for (int r = 0; r < (int) order.size(); r++)
            if (! keepId.empty() && crates[(size_t) cur].songs[(size_t) order[(size_t) r]].id == keepId) { table.selectRow (r); break; }
        showInfo();
    }
}

void CrateTab::crateChosen (int row)
{
    if (row == cur && ! order.empty()) return;
    cur = row;
    table.deselectAllRows();
    rebuild();
    showInfo();
}

void CrateTab::crateMenu (int row)
{
    if (row < 0 || row >= (int) crates.size()) return;
    crateList.selectRow (row);
    juce::PopupMenu m;
    m.addItem (1, "Show in the player");
    m.addItem (2, "Rename...", row > 0);
    m.addItem (3, "Export...");
    m.addItem (4, "Delete...", row > 0);
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r) {
        if (r == 1) toPlayer.triggerClick();
        else if (r == 2) renB.triggerClick();
        else if (r == 3) expB.triggerClick();
        else if (r == 4) delB.triggerClick();
    });
}

void CrateTab::songClicked (int row, int c, const juce::MouseEvent& e)
{
    auto* s = songAt (row);
    if (! s) return;
    if (c == C_FAV) { ctx.crates.toggleFav (*s); return; }
    if (e.mods.isPopupMenu())
    {
        CrateSong song = *s;
        int si = order[(size_t) row];
        juce::PopupMenu m, add, mv;
        for (int i = 0; i < (int) crates.size(); i++)
        {
            if (i == cur) continue;
            add.addItem (100 + i, U (crates[(size_t) i].name));
            mv.addItem (200 + i, U (crates[(size_t) i].name));
        }
        m.addItem (1, "Play");
        m.addItem (2, favs.count (song.id) ? "Remove from FAVS" : "Add to FAVS");
        m.addSubMenu ("Add to crate", add);
        m.addSubMenu ("Move to crate", mv);
        m.addItem (3, "Copy the YouTube link");
        m.addSeparator();
        m.addItem (4, "Remove from this crate");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, song, si, row] (int r) {
            if (r == 1) songDoubleClicked (row);
            else if (r == 2) ctx.crates.toggleFav (song);
            else if (r == 3) juce::SystemClipboard::copyTextToClipboard ("https://www.youtube.com/watch?v=" + U (song.id));
            else if (r == 4) ctx.crates.remove (cur, si);
            else if (r >= 200) ctx.crates.moveToCrate (cur, si, r - 200);
            else if (r >= 100) ctx.crates.add (r - 100, song);
        });
    }
}

void CrateTab::songDoubleClicked (int row)
{
    auto* s = songAt (row);
    if (! s) return;
    ctx.eng.loadSong (s->id, s->title, s->secs, true);
}

void CrateTab::sortBy (int c, bool fwd)
{
    sortCol = c;
    sortFwd = fwd;
    rebuild();
}

void CrateTab::songSelectionChanged() { showInfo(); }

void CrateTab::showInfo()
{
    auto* s = songAt (table.getSelectedRow());
    std::string id = s ? s->id : std::string();
    bool en = s != nullptr;
    for (auto* e : { &eBpm, &eKey, &eTags, &eNote }) e->setEnabled (en);
    saveInfo.setEnabled (en);
    bpmDeck.setEnabled (en);
    playB.setEnabled (en);
    remB.setEnabled (en);
    if (id == infoFor && en) return;
    infoFor = id;
    eBpm.setText (s && s->bpm > 0 ? juce::String (s->bpm) : juce::String(), false);
    eKey.setText (s ? U (s->key) : juce::String(), false);
    eTags.setText (s ? U (s->tags) : juce::String(), false);
    eNote.setText (s ? U (s->note) : juce::String(), false);
}

void CrateTab::moveSel (int d)
{
    int r = table.getSelectedRow();
    if (r < 0 || cur < 0) return;
    int si = order[(size_t) r], to = si + d;
    if (to < 0 || to >= (int) crates[(size_t) cur].songs.size()) return;
    ctx.crates.move (cur, si, to);
    crates = ctx.crates.all();
    seenRev = ctx.crates.revision();
    rebuild();
    table.selectRow (r + d);
}

/* ------------------------------------------------------------------ drag: reorder in the table, or onto a crate on the left */
bool CrateTab::isInterestedInDragSource (const SourceDetails& d) { return d.description.toString().startsWith ("crate-songs:"); }

void CrateTab::itemDragMove (const SourceDetails& d)
{
    int old = dropRow;
    auto p = d.localPosition.toInt();
    if (crateList.getBounds().contains (p))
    {
        int row = crateList.getRowContainingPosition (p.x - crateList.getX(), p.y - crateList.getY());
        dropRow = row >= 0 && row != cur ? -100 - row : -1;
    }
    else if (table.getBounds().contains (p) && sortCol == C_POS && sortFwd && filter.isEmpty())
    {
        int row = table.getRowContainingPosition (p.x - table.getX(), p.y - table.getY());
        dropRow = row < 0 ? (int) order.size() : row;
    }
    else dropRow = -1;
    if (old != dropRow) { table.repaint(); crateList.repaint(); }
}

void CrateTab::itemDragExit (const SourceDetails&)
{
    dropRow = -1;
    table.repaint();
    crateList.repaint();
}

void CrateTab::itemDropped (const SourceDetails& d)
{
    int fromRow = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    int dr = dropRow;
    dropRow = -1;
    if (fromRow < 0 || fromRow >= (int) order.size()) return;
    int si = order[(size_t) fromRow];
    if (dr <= -100)
    {   /* onto a crate: copy it there (Shift/Alt: move) */
        int target = -100 - dr;
        if (juce::ModifierKeys::currentModifiers.isShiftDown() || juce::ModifierKeys::currentModifiers.isAltDown()) ctx.crates.moveToCrate (cur, si, target);
        else ctx.crates.add (target, crates[(size_t) cur].songs[(size_t) si]);
    }
    else if (dr >= 0)
    {
        int to = dr >= (int) order.size() ? (int) order.size() - 1 : (dr > fromRow ? dr - 1 : dr);
        ctx.crates.move (cur, si, order[(size_t) juce::jlimit (0, (int) order.size() - 1, to)]);
    }
    table.repaint();
    crateList.repaint();
}
} // namespace cr
