/* CRATE RIPPA Desktop (JJ DA PLUG): child processes, decoding, downloads. MIT. */
#include "Tools.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <cstring>
#include <condition_variable>
#include <thread>

namespace cr
{
int runProcess (const juce::StringArray& args, const std::function<void (const std::string&)>& onLine, double timeoutSec,
                const std::function<bool()>& cancelled, std::shared_ptr<juce::ChildProcess>* slot, std::mutex* slotLock)
{
    auto p = std::make_shared<juce::ChildProcess>();
    if (! p->start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1;
    if (slot != nullptr)
    {
        std::lock_guard<std::mutex> g (*slotLock);
        *slot = p;
    }
    std::mutex dm;
    std::condition_variable dcv;
    bool done = false;
    std::atomic<int> why { 0 };   // 2 timeout, 3 cancelled
    std::thread dog ([&] {
        auto t0 = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lk (dm);
        while (! done)
        {
            dcv.wait_for (lk, std::chrono::milliseconds (50));
            if (done) break;
            double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            if (cancelled && cancelled()) { why = 3; p->kill(); break; }
            if (el > timeoutSec) { why = 2; p->kill(); break; }
        }
    });
    char b[256];
    std::string acc;
    for (;;)
    {
        int n = p->readProcessOutput (b, (int) sizeof b);
        if (n <= 0) break;
        acc.append (b, (size_t) n);
        size_t nl;
        while ((nl = acc.find_first_of ("\r\n")) != std::string::npos)
        {
            std::string line = acc.substr (0, nl);
            acc.erase (0, nl + 1);
            if (! line.empty() && onLine) onLine (line);
        }
        if (acc.size() > (8u << 20)) acc.clear();   // an absurd line
    }
    if (! acc.empty() && onLine) onLine (acc);
    p->waitForProcessToFinish (3000);
    {
        std::lock_guard<std::mutex> lk (dm);
        done = true;
    }
    dcv.notify_all();
    dog.join();
    if (slot != nullptr)
    {
        std::lock_guard<std::mutex> g (*slotLock);
        if (*slot == p) slot->reset();
    }
    if (why == 2) return -2;
    if (why == 3) return -3;
    return (int) p->getExitCode();
}

static bool decodeFfmpeg (const juce::File& file, const juce::File& ff, const std::function<bool (const int16_t*, int)>& sink,
                          const std::function<bool()>& cancelled, std::shared_ptr<juce::ChildProcess>* slot, std::mutex* slotLock,
                          std::string& err)
{
    juce::StringArray a { ff.getFullPathName(), "-nostdin", "-hide_banner", "-loglevel", "quiet", "-i", file.getFullPathName(),
                          "-vn", "-ac", "2", "-ar", "44100", "-f", "s16le", "pipe:1" };
    auto p = std::make_shared<juce::ChildProcess>();
    if (! p->start (a, juce::ChildProcess::wantStdOut)) { err = "FFMPEG DOES NOT START"; return false; }
    if (slot) { std::lock_guard<std::mutex> g (*slotLock); *slot = p; }
    std::vector<char> chunk (65536 + 4);
    int tail = 0;
    long long frames = 0;
    bool stopped = false;
    for (;;)
    {
        if (cancelled && cancelled()) { p->kill(); stopped = true; break; }
        int n = p->readProcessOutput (chunk.data() + tail, 65536);
        if (n <= 0) break;
        int bytes = tail + n, fr = bytes / 4;
        if (fr > 0 && ! sink (reinterpret_cast<const int16_t*> (chunk.data()), fr)) { p->kill(); stopped = true; break; }
        frames += fr;
        tail = bytes - fr * 4;
        if (tail) memmove (chunk.data(), chunk.data() + fr * 4, (size_t) tail);
    }
    p->waitForProcessToFinish (3000);
    if (slot) { std::lock_guard<std::mutex> g (*slotLock); if (*slot == p) slot->reset(); }
    if (frames <= 0 && ! stopped) { err = "FFMPEG COULD NOT READ THE SONG"; return false; }
    return true;
}

static bool decodeJuce (const juce::File& file, const std::function<bool (const int16_t*, int)>& sink,
                        const std::function<bool()>& cancelled, std::string& err)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) { err = "THIS FORMAT NEEDS FFMPEG (" + file.getFileExtension().toUpperCase().substring (1).toStdString() + ")"; return false; }
    const double srcRate = rd->sampleRate > 0 ? rd->sampleRate : 44100.0;
    const int chans = (int) juce::jmax (1u, rd->numChannels);
    juce::AudioFormatReaderSource src (rd.get(), false);
    juce::ResamplingAudioSource rs (&src, false, 2);
    rs.setResamplingRatio (srcRate / 44100.0);
    const int B = 4096;
    rs.prepareToPlay (B, 44100.0);
    juce::AudioBuffer<float> ab (2, B);
    std::vector<int16_t> o ((size_t) B * 2);
    const long long want = (long long) ((double) rd->lengthInSamples * 44100.0 / srcRate) + 1;
    long long done = 0;
    while (done < want)
    {
        if (cancelled && cancelled()) break;
        int n = (int) juce::jmin<long long> (B, want - done);
        ab.clear();
        juce::AudioSourceChannelInfo info (&ab, 0, n);
        rs.getNextAudioBlock (info);
        const float* l = ab.getReadPointer (0);
        const float* r = ab.getReadPointer (chans > 1 ? 1 : 0);
        if (chans == 1) r = l;
        for (int i = 0; i < n; i++)
        {
            o[(size_t) (2 * i)] = (int16_t) juce::jlimit (-32768.0f, 32767.0f, l[i] * 32767.0f);
            o[(size_t) (2 * i + 1)] = (int16_t) juce::jlimit (-32768.0f, 32767.0f, r[i] * 32767.0f);
        }
        if (! sink (o.data(), n)) break;
        done += n;
    }
    rs.releaseResources();
    return done > 0;
}

bool decodeAudio (const juce::File& file, const juce::File& ffmpeg, const std::function<bool (const int16_t*, int)>& sink,
                  const std::function<bool()>& cancelled, std::shared_ptr<juce::ChildProcess>* slot, std::mutex* slotLock,
                  std::string& err)
{
    if (ffmpeg.existsAsFile() || ffmpeg.getFileName() == "ffmpeg")
    {
        std::string e;
        if (decodeFfmpeg (file, ffmpeg, sink, cancelled, slot, slotLock, e)) return true;
        err = e;
    }
#if JUCE_WINDOWS
    {
        std::string e;
        if (decodeWithMediaFoundation (file, sink, cancelled, e)) return true;
        err = e;
    }
#endif
    std::string e;
    if (decodeJuce (file, sink, cancelled, e)) return true;
    if (err.empty()) err = e;
    return false;
}

bool downloadFile (const juce::URL& url, const juce::File& dest, const std::function<void (double)>& progress,
                   const std::function<bool()>& cancelled, std::string& err)
{
    int status = 0;
    juce::StringPairArray hdr;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (20000)
                    .withNumRedirectsToFollow (8)
                    .withStatusCode (&status)
                    .withResponseHeaders (&hdr)
                    .withExtraHeaders ("User-Agent: CrateRippa-Desktop/1.0\r\n");
    std::unique_ptr<juce::InputStream> in (url.createInputStream (opts));
    if (in == nullptr) { err = "NO CONNECTION"; return false; }
    if (status >= 400) { err = "DOWNLOAD FAILED (HTTP " + std::to_string (status) + ")"; return false; }
    dest.getParentDirectory().createDirectory();
    juce::File part (dest.getFullPathName() + ".part");
    part.deleteFile();
    {
        juce::FileOutputStream out (part);
        if (! out.openedOk()) { err = "CANNOT WRITE " + dest.getFileName().toStdString(); return false; }
        const juce::int64 total = in->getTotalLength();
        juce::int64 got = 0;
        juce::HeapBlock<char> b (65536);
        for (;;)
        {
            if (cancelled && cancelled()) { err = "CANCELLED"; break; }
            int n = in->read (b, 65536);
            if (n <= 0) break;
            if (! out.write (b, (size_t) n)) { err = "DISK FULL?"; break; }
            got += n;
            if (progress && total > 0) progress ((double) got / (double) total);
        }
        out.flush();
        if (! err.empty()) { part.deleteFile(); return false; }
        if (got < 1000) { part.deleteFile(); err = "DOWNLOAD TOO SMALL"; return false; }
    }
    dest.deleteFile();
    if (! part.moveFileTo (dest)) { err = "CANNOT REPLACE " + dest.getFileName().toStdString(); return false; }
    return true;
}

std::string ytWhy (const std::string& text)
{
    std::string low = juce::String (text).toLowerCase().toStdString();
    static const char* K[][2] = {
        { "private video", "PRIVATE VIDEO" }, { "sign in to confirm your age", "AGE LIMIT" },
        { "not available in your country", "REGION BLOCKED" }, { "copyright", "COPYRIGHT BLOCK" },
        { "video unavailable", "VIDEO REMOVED" }, { "live event", "LIVE, NOT A VIDEO" }, { "timed out", "NETWORK TIMEOUT" },
        { "name or service not known", "NO INTERNET" }, { "temporary failure in name resolution", "NO INTERNET" },
        { "getaddrinfo failed", "NO INTERNET" }, { "network is unreachable", "NO INTERNET" },
        { "http error 429", "YOUTUBE BUSY, TRY LATER" }, { "unable to connect", "NO INTERNET" },
        { "unable to download api page", "NO INTERNET" }, { "connection refused", "NO INTERNET" },
        { "page needs to be reloaded", "UPDATE YT-DLP (INFO TAB)" }, { "sign in to confirm you", "YOUTUBE ASKS FOR LOGIN (INSTALL DENO, INFO TAB)" },
        { "requested format is not available", "NO AUDIO FORMAT: UPDATE YT-DLP OR INSTALL DENO (INFO TAB)" },
        { "javascript runtime", "INSTALL DENO (INFO TAB)" } };
    for (auto& k : K)
        if (low.find (k[0]) != std::string::npos) return k[1];
    std::string t = text;
    auto p = t.find ("ERROR: ");
    if (p != std::string::npos) t = t.substr (p + 7);
    if (t.size() > 90) t = t.substr (0, 90);
    return t.empty() ? "UNKNOWN ERROR" : juce::String (t).toUpperCase().toStdString();
}
} // namespace cr
