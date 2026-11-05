/* CRATE RIPPA Desktop (JJ DA PLUG): the tabs PLAYER, CHOP, WAVE, ISOLATE, CRATE, INFO. */
#pragma once
#include "Ui.h"
#include <map>
#include <set>

namespace cr
{
class PlayerTab : public Tab, private juce::ListBoxModel
{
public:
    explicit PlayerTab (Ctx& c);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    Platter platter;
    juce::Slider pitch;
    Seg range, find;
    Btn klock, cue, play, rew, fwd, stop, loop, searchB, history, saveList;
    Knob songMix, padMix;
    juce::Slider seek;
    juce::TextEditor query;
    juce::ListBox list;
    juce::String head, pitchTxt;
    std::vector<Result> rows;
    std::string playingId;
    std::set<std::string> favs;
    unsigned favRev = 0;
    int numRows() { return (int) rows.size(); }
    int getNumRows() override { return numRows(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void addMenu (int row);
};

class ChopTab : public Tab
{
public:
    explicit ChopTab (Ctx& c);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    WaveView wave;
    PadGrid pads;
    juce::OwnedArray<Btn> banks;
    Seg chopBy, assign, arm, slice;
    Knob sens, bpm;
    Btn tap, detect, hostBpm, autoB, playB, setCue, goCue, undo, clrPad, clrBank, stopAll, velo, cpy, mov, swp, del, bcpy, bmov, bswp, learn, exportB, openEx;
    juce::String hint;
};

class WaveTab : public Tab
{
public:
    explicit WaveTab (Ctx& c);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    WaveView wave;
    EnvView env;
    PadGrid pads;
    Seg viewSeg, bankSeg, mode;
    Btn fit, prev, next, playPad, rev, grpM, grpP;
    juce::OwnedArray<Btn> fine;
    Knob a, d, s, r, pitch, level;
    juce::String padTxt, posTxt, grpTxt;
};

class IsoTab : public Tab
{
public:
    explicit IsoTab (Ctx& c);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::OwnedArray<Btn> modes;
    Btn toSel, toBank;
    PadGrid pads;
    int cur = -1;
};

class InfoTab : public Tab
{
public:
    explicit InfoTab (Ctx& c);
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    Btn upd, deno, openData, openEx;
    juce::String ytv;
    bool hasFfmpeg = false, hasDeno = false;
    int tick = 0;
};

class CrateTab : public Tab, public juce::DragAndDropTarget
{
public:
    explicit CrateTab (Ctx& c);
    ~CrateTab() override;
    void refresh() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;

    /* used by the two list models */
    std::vector<Crate> crates;
    std::vector<int> order;   // rows of the table -> songs of the crate
    int cur = 0;
    std::set<std::string> favs;
    std::string loadedId;
    int dropRow = -1;
    const CrateSong* songAt (int row) const;
    void crateChosen (int row);
    void crateMenu (int row);
    void songClicked (int row, int col, const juce::MouseEvent&);
    void songDoubleClicked (int row);
    void sortBy (int col, bool fwd);
    void songSelectionChanged();
    bool isCached (const std::string& id);

private:
    struct CrateModel;
    struct SongModel;
    std::unique_ptr<CrateModel> crateModel;
    std::unique_ptr<SongModel> songModel;
    juce::ListBox crateList;
    juce::TableListBox table;
    Btn newB, renB, delB, impB, expB, playB, toPlayer, remB, upB, downB, saveInfo, bpmDeck;
    juce::TextEditor filter, eBpm, eKey, eTags, eNote;
    juce::Label lBpm, lKey, lTags, lNote;
    int sortCol = 1;
    bool sortFwd = true;
    unsigned seenRev = 0;
    std::string infoFor;
    std::map<std::string, bool> cachedMemo;
    juce::uint32 cachedMemoTime = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    void rebuild();
    void showInfo();
    int selSongIndex() const;
    void moveSel (int d);
};
} // namespace cr
