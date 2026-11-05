/* CRATE RIPPA Desktop (JJ DA PLUG): FAVS and crates. MIT. */
#include "CrateStore.h"
#include "CrEngine.h"
#include <algorithm>

namespace cr
{
CrateStore::CrateStore() : CrateStore (defaultDataDir().getChildFile ("Crates").getChildFile ("crates.json")) {}

CrateStore::CrateStore (const juce::File& f) : path (f)
{
    path.getParentDirectory().createDirectory();
    std::lock_guard<std::mutex> g (mx);
    load();
}

juce::var CrateStore::songToVar (const CrateSong& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", juce::String::fromUTF8 (s.id.c_str()));
    o->setProperty ("title", juce::String::fromUTF8 (s.title.c_str()));
    o->setProperty ("channel", juce::String::fromUTF8 (s.chan.c_str()));
    o->setProperty ("seconds", s.secs);
    if (s.bpm > 0) o->setProperty ("bpm", s.bpm);
    if (! s.key.empty()) o->setProperty ("key", juce::String::fromUTF8 (s.key.c_str()));
    if (! s.tags.empty()) o->setProperty ("tags", juce::String::fromUTF8 (s.tags.c_str()));
    if (! s.note.empty()) o->setProperty ("note", juce::String::fromUTF8 (s.note.c_str()));
    o->setProperty ("added", s.added);
    return juce::var (o);
}

CrateSong CrateStore::varToSong (const juce::var& v)
{
    CrateSong s;
    s.id = v.getProperty ("id", "").toString().toStdString();
    s.title = v.getProperty ("title", "").toString().toStdString();
    s.chan = v.getProperty ("channel", "").toString().toStdString();
    s.secs = (int) v.getProperty ("seconds", 0);
    s.bpm = (int) v.getProperty ("bpm", 0);
    s.key = v.getProperty ("key", "").toString().toStdString();
    s.tags = v.getProperty ("tags", "").toString().toStdString();
    s.note = v.getProperty ("note", "").toString().toStdString();
    s.added = (juce::int64) v.getProperty ("added", 0);
    return s;
}

void CrateStore::ensureFavs()
{
    auto it = std::find_if (crates.begin(), crates.end(), [] (const Crate& c) { return c.fixed; });
    if (it == crates.end()) { Crate f; f.name = "FAVS"; f.fixed = true; crates.insert (crates.begin(), f); }
    else if (it != crates.begin()) { Crate f = *it; crates.erase (it); crates.insert (crates.begin(), f); }
}

void CrateStore::load()
{
    crates.clear();
    if (path.existsAsFile())
    {
        loadedTime = path.getLastModificationTime();
        auto root = juce::JSON::parse (path.loadFileAsString());
        if (auto* arr = root.getProperty ("crates", {}).getArray())
            for (auto& c : *arr)
            {
                Crate cr_;
                cr_.name = c.getProperty ("name", "CRATE").toString().toStdString();
                cr_.source = c.getProperty ("source", "").toString().toStdString();
                cr_.fixed = (bool) c.getProperty ("favs", false);
                if (auto* ss = c.getProperty ("songs", {}).getArray())
                    for (auto& s : *ss)
                    {
                        auto song = varToSong (s);
                        if (! song.id.empty()) cr_.songs.push_back (song);
                    }
                crates.push_back (cr_);
            }
    }
    ensureFavs();
    rev++;
}

void CrateStore::save()
{
    juce::Array<juce::var> arr;
    for (auto& c : crates)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String::fromUTF8 (c.name.c_str()));
        if (c.fixed) o->setProperty ("favs", true);
        if (! c.source.empty()) o->setProperty ("source", juce::String::fromUTF8 (c.source.c_str()));
        juce::Array<juce::var> ss;
        for (auto& s : c.songs) ss.add (songToVar (s));
        o->setProperty ("songs", ss);
        arr.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "Crate Rippa Desktop (JJ DA PLUG)");
    root->setProperty ("version", 1);
    root->setProperty ("crates", arr);
    juce::File part (path.getFullPathName() + ".part");
    if (part.replaceWithText (juce::JSON::toString (juce::var (root), false)))
    {
        path.deleteFile();
        part.moveFileTo (path);
    }
    loadedTime = path.getLastModificationTime();
    rev++;
}

void CrateStore::reloadIfChanged()
{
    std::lock_guard<std::mutex> g (mx);
    if (path.existsAsFile() && path.getLastModificationTime() != loadedTime) load();
}

std::vector<Crate> CrateStore::all() { std::lock_guard<std::mutex> g (mx); return crates; }
int CrateStore::size() { std::lock_guard<std::mutex> g (mx); return (int) crates.size(); }

bool CrateStore::isFav (const std::string& id)
{
    std::lock_guard<std::mutex> g (mx);
    for (auto& s : crates[0].songs) if (s.id == id) return true;
    return false;
}

std::set<std::string> CrateStore::favIds()
{
    std::lock_guard<std::mutex> g (mx);
    std::set<std::string> r;
    for (auto& s : crates[0].songs) r.insert (s.id);
    return r;
}

bool CrateStore::toggleFav (const CrateSong& s)
{
    std::lock_guard<std::mutex> g (mx);
    auto& v = crates[0].songs;
    auto it = std::find_if (v.begin(), v.end(), [&] (const CrateSong& x) { return x.id == s.id; });
    bool now;
    if (it != v.end()) { v.erase (it); now = false; }
    else { CrateSong c = s; if (! c.added) c.added = juce::Time::currentTimeMillis(); v.insert (v.begin(), c); now = true; }
    save();
    return now;
}

bool CrateStore::add (int ci, const CrateSong& s)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size() || s.id.empty()) return false;
    auto& v = crates[(size_t) ci].songs;
    if (std::any_of (v.begin(), v.end(), [&] (const CrateSong& x) { return x.id == s.id; })) return false;
    CrateSong c = s;
    if (! c.added) c.added = juce::Time::currentTimeMillis();
    v.push_back (c);
    save();
    return true;
}

void CrateStore::remove (int ci, int si)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size()) return;
    auto& v = crates[(size_t) ci].songs;
    if (si < 0 || si >= (int) v.size()) return;
    v.erase (v.begin() + si);
    save();
}

void CrateStore::move (int ci, int from, int to)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size()) return;
    auto& v = crates[(size_t) ci].songs;
    if (from < 0 || from >= (int) v.size()) return;
    to = juce::jlimit (0, (int) v.size() - 1, to);
    if (from == to) return;
    auto s = v[(size_t) from];
    v.erase (v.begin() + from);
    v.insert (v.begin() + to, s);
    save();
}

void CrateStore::moveToCrate (int ci, int si, int target)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size() || target < 0 || target >= (int) crates.size() || ci == target) return;
    auto& v = crates[(size_t) ci].songs;
    if (si < 0 || si >= (int) v.size()) return;
    auto s = v[(size_t) si];
    auto& t = crates[(size_t) target].songs;
    if (std::none_of (t.begin(), t.end(), [&] (const CrateSong& x) { return x.id == s.id; })) t.push_back (s);
    v.erase (v.begin() + si);
    save();
}

std::string CrateStore::uniqueName (const std::string& want, int except)
{
    std::string base = juce::String (want).trim().substring (0, 40).toStdString();
    if (base.empty()) base = "NEW CRATE";
    std::string n = base;
    for (int k = 2;; k++)
    {
        bool clash = false;
        for (int i = 0; i < (int) crates.size(); i++)
            if (i != except && juce::String (crates[(size_t) i].name).equalsIgnoreCase (juce::String (n))) clash = true;
        if (! clash) return n;
        n = base + " " + std::to_string (k);
    }
}

int CrateStore::create (const std::string& name)
{
    std::lock_guard<std::mutex> g (mx);
    Crate c;
    c.name = uniqueName (name);
    crates.push_back (c);
    save();
    return (int) crates.size() - 1;
}

void CrateStore::rename (int ci, const std::string& name)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci <= 0 || ci >= (int) crates.size()) return;   /* FAVS keeps its name */
    crates[(size_t) ci].name = uniqueName (name, ci);
    save();
}

void CrateStore::erase (int ci)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci <= 0 || ci >= (int) crates.size()) return;
    crates.erase (crates.begin() + ci);
    save();
}

void CrateStore::setInfo (int ci, int si, int bpm, const std::string& key, const std::string& tags, const std::string& note)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size()) return;
    auto& v = crates[(size_t) ci].songs;
    if (si < 0 || si >= (int) v.size()) return;
    auto& s = v[(size_t) si];
    s.bpm = juce::jlimit (0, 300, bpm);
    s.key = key;
    s.tags = tags;
    s.note = note;
    /* the same song in other crates gets the same BPM and key */
    for (auto& c : crates)
        for (auto& o : c.songs)
            if (o.id == s.id) { o.bpm = s.bpm; o.key = s.key; }
    save();
}

void CrateStore::setInfoById (const std::string& id, int bpm, const std::string& key)
{
    std::lock_guard<std::mutex> g (mx);
    bool any = false;
    for (auto& c : crates)
        for (auto& o : c.songs)
            if (o.id == id) { if (bpm > 0) o.bpm = bpm; if (! key.empty()) o.key = key; any = true; }
    if (any) save();
}

int CrateStore::saveAsCrate (const std::string& name, const std::string& source, const std::vector<CrateSong>& songs)
{
    std::lock_guard<std::mutex> g (mx);
    Crate c;
    c.name = uniqueName (name);
    c.source = source;
    auto now = juce::Time::currentTimeMillis();
    for (auto s : songs)
        if (! s.id.empty() && std::none_of (c.songs.begin(), c.songs.end(), [&] (const CrateSong& x) { return x.id == s.id; }))
        {
            if (! s.added) s.added = now;
            c.songs.push_back (s);
        }
    crates.push_back (c);
    save();
    return (int) crates.size() - 1;
}

bool CrateStore::exportCrate (int ci, const juce::File& f)
{
    std::lock_guard<std::mutex> g (mx);
    if (ci < 0 || ci >= (int) crates.size()) return false;
    auto& c = crates[(size_t) ci];
    auto* o = new juce::DynamicObject();
    o->setProperty ("app", "Crate Rippa Desktop (JJ DA PLUG)");
    o->setProperty ("name", juce::String::fromUTF8 (c.name.c_str()));
    if (! c.source.empty()) o->setProperty ("source", juce::String::fromUTF8 (c.source.c_str()));
    juce::Array<juce::var> ss;
    for (auto& s : c.songs) ss.add (songToVar (s));
    o->setProperty ("songs", ss);
    return f.replaceWithText (juce::JSON::toString (juce::var (o), false));
}

int CrateStore::importCrate (const juce::File& f)
{
    auto root = juce::JSON::parse (f.loadFileAsString());
    auto* ss = root.getProperty ("songs", {}).getArray();
    if (ss == nullptr) return -1;
    std::vector<CrateSong> songs;
    for (auto& s : *ss) songs.push_back (varToSong (s));
    auto nm = root.getProperty ("name", f.getFileNameWithoutExtension()).toString().toStdString();
    return saveAsCrate (nm, root.getProperty ("source", "").toString().toStdString(), songs);
}
} // namespace cr
