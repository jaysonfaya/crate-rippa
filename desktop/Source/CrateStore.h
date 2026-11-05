/* CRATE RIPPA Desktop (JJ DA PLUG): the CRATE tab's data. FAVS plus your own playlists ("crates"), each song with BPM, key,
 * tags and a note. One JSON file: Documents/JJ DA PLUG/Crate Rippa/Crates/crates.json, shared by every instance (and the
 * standalone app); written atomically, read again when another program changed it. MIT. */
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace cr
{
struct CrateSong
{
    std::string id, title, chan, key, tags, note;
    int secs = 0, bpm = 0;
    juce::int64 added = 0;   // ms since 1970
};

struct Crate
{
    std::string name, source;   // source: "youtube:<playlist id>" if it came from YouTube
    bool fixed = false;         // FAVS: always first, cannot be renamed or deleted
    std::vector<CrateSong> songs;
};

class CrateStore
{
public:
    CrateStore();
    explicit CrateStore (const juce::File& file);

    std::vector<Crate> all();
    int size();
    unsigned revision() const { return rev.load(); }

    bool isFav (const std::string& id);
    bool toggleFav (const CrateSong& s);                  // true: now a fav
    bool add (int crate, const CrateSong& s);             // false: it was there already
    void remove (int crate, int song);
    void move (int crate, int from, int to);
    void moveToCrate (int crate, int song, int target);
    int create (const std::string& name);                 // index of the new crate
    void rename (int crate, const std::string& name);
    void erase (int crate);
    void setInfo (int crate, int song, int bpm, const std::string& key, const std::string& tags, const std::string& note);
    void setInfoById (const std::string& id, int bpm, const std::string& key);   // every crate that has this song
    int saveAsCrate (const std::string& name, const std::string& source, const std::vector<CrateSong>& songs);
    bool exportCrate (int crate, const juce::File& f);
    int importCrate (const juce::File& f);                // index, -1 on error
    std::set<std::string> favIds();
    void reloadIfChanged();
    juce::File file() const { return path; }

private:
    juce::File path;
    std::mutex mx;
    std::vector<Crate> crates;
    juce::Time loadedTime;
    std::atomic<unsigned> rev { 1 };
    void load();
    void save();
    void ensureFavs();
    std::string uniqueName (const std::string& want, int except = -1);
    static juce::var songToVar (const CrateSong& s);
    static CrateSong varToSong (const juce::var& v);
};
} // namespace cr
