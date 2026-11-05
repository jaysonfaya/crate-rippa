/* CRATE RIPPA Desktop (JJ DA PLUG): child processes (yt-dlp, ffmpeg), audio decoding, downloads. */
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace cr
{
/* Runs a program, hands every output line (stdout and stderr) to onLine. The process can be killed through `slot` from another
 * thread. Returns the exit code, -1 if it could not start, -2 on timeout, -3 if cancelled. */
int runProcess (const juce::StringArray& args, const std::function<void (const std::string&)>& onLine, double timeoutSec,
                const std::function<bool()>& cancelled, std::shared_ptr<juce::ChildProcess>* slot, std::mutex* slotLock);

/* Decodes a file to 16-bit stereo 44.1 kHz and hands it to sink in chunks (sink returns false to stop).
 * Order: ffmpeg (if given), then Media Foundation (Windows), then JUCE's readers (Core Audio on macOS, WAV/AIFF/FLAC/Ogg/MP3). */
bool decodeAudio (const juce::File& file, const juce::File& ffmpeg, const std::function<bool (const int16_t*, int)>& sink,
                  const std::function<bool()>& cancelled, std::shared_ptr<juce::ChildProcess>* slot, std::mutex* slotLock,
                  std::string& err);

#if JUCE_WINDOWS
bool decodeWithMediaFoundation (const juce::File& file, const std::function<bool (const int16_t*, int)>& sink,
                                const std::function<bool()>& cancelled, std::string& err);
#endif

/* Downloads url to dest (via dest.part, then rename). progress gets 0..1 when the size is known. */
bool downloadFile (const juce::URL& url, const juce::File& dest, const std::function<void (double)>& progress,
                   const std::function<bool()>& cancelled, std::string& err);

/* The short reason for a yt-dlp error text, in capitals. */
std::string ytWhy (const std::string& text);
} // namespace cr
