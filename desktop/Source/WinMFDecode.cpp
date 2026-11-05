/* CRATE RIPPA Desktop (JJ DA PLUG): Windows only. Decodes M4A/AAC (and whatever else Windows can play) with Media Foundation,
 * so Windows needs no ffmpeg for YouTube's M4A audio. Float PCM at the file's own rate, resampled here to 44.1 kHz. MIT. */
#include "Tools.h"
#if JUCE_WINDOWS
#include <juce_audio_basics/juce_audio_basics.h>
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <vector>

#if defined(_MSC_VER)
 #pragma comment(lib, "mfplat.lib")
 #pragma comment(lib, "mfreadwrite.lib")
 #pragma comment(lib, "mfuuid.lib")
 #pragma comment(lib, "ole32.lib")
#endif

namespace cr
{
template <class T> struct ComPtr
{
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

bool decodeWithMediaFoundation (const juce::File& file, const std::function<bool (const int16_t*, int)>& sink,
                                const std::function<bool()>& cancelled, std::string& err)
{
    HRESULT hrCo = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    const bool coOwned = SUCCEEDED (hrCo);   // RPC_E_CHANGED_MODE: COM is up already in another mode, that is fine
    if (FAILED (MFStartup (MF_VERSION, MFSTARTUP_LITE))) { if (coOwned) CoUninitialize(); err = "MEDIA FOUNDATION MISSING"; return false; }
    bool ok = false;
    {
        ComPtr<IMFSourceReader> rd;
        if (FAILED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), nullptr, &rd)))
            err = "WINDOWS CANNOT READ THIS FILE";
        else
        {
            rd->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
            rd->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
            ComPtr<IMFMediaType> want;
            MFCreateMediaType (&want);
            want->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            want->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (FAILED (rd->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.p)))
                err = "NO AUDIO DECODER FOR THIS FILE";
            else
            {
                ComPtr<IMFMediaType> got;
                rd->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got);
                UINT32 ch = 2, rate = 44100;
                got->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
                got->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                if (ch < 1) ch = 1;
                const double speed = rate > 0 ? (double) rate / 44100.0 : 1.0;
                juce::LagrangeInterpolator li[2];
                std::vector<float> inL, inR, outL, outR;
                std::vector<int16_t> o;
                long long total = 0;
                for (;;)
                {
                    if (cancelled && cancelled()) break;
                    DWORD flags = 0;
                    ComPtr<IMFSample> smp;
                    if (FAILED (rd->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &smp))) break;
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
                    if (! smp) continue;
                    ComPtr<IMFMediaBuffer> mb;
                    if (FAILED (smp->ConvertToContiguousBuffer (&mb))) break;
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (FAILED (mb->Lock (&data, nullptr, &len))) break;
                    const float* f = reinterpret_cast<const float*> (data);
                    const int frames = (int) (len / (sizeof (float) * ch));
                    for (int i = 0; i < frames; i++)
                    {
                        inL.push_back (f[i * (int) ch]);
                        inR.push_back (f[i * (int) ch + (ch > 1 ? 1 : 0)]);
                    }
                    mb->Unlock();
                    /* resample what we have, keep a few input frames for the interpolator's next call */
                    int avail = (int) inL.size();
                    int nOut = (int) ((avail - 8) / speed);
                    if (nOut <= 0) continue;
                    outL.resize ((size_t) nOut);
                    outR.resize ((size_t) nOut);
                    int used = 0;
                    if (rate == 44100)
                    {
                        nOut = avail;
                        outL.assign (inL.begin(), inL.end());
                        outR.assign (inR.begin(), inR.end());
                        used = avail;
                    }
                    else
                    {
                        used = li[0].process (speed, inL.data(), outL.data(), nOut);
                        li[1].process (speed, inR.data(), outR.data(), nOut);
                    }
                    o.resize ((size_t) nOut * 2);
                    for (int i = 0; i < nOut; i++)
                    {
                        o[(size_t) (2 * i)] = (int16_t) juce::jlimit (-32768.0f, 32767.0f, outL[(size_t) i] * 32767.0f);
                        o[(size_t) (2 * i + 1)] = (int16_t) juce::jlimit (-32768.0f, 32767.0f, outR[(size_t) i] * 32767.0f);
                    }
                    inL.erase (inL.begin(), inL.begin() + juce::jmin (used, avail));
                    inR.erase (inR.begin(), inR.begin() + juce::jmin (used, avail));
                    total += nOut;
                    if (! sink (o.data(), nOut)) break;
                }
                ok = total > 0;
                if (! ok && err.empty()) err = "WINDOWS DECODED NOTHING";
            }
        }
    }
    MFShutdown();
    if (coOwned) CoUninitialize();
    return ok;
}
} // namespace cr
#endif
