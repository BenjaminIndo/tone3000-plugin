// Multitrack: a small fixed set of tracks that play together while a new one is
// recorded on top (overdub), with per-track volume / mute / solo / latency
// nudge, and a mixdown to WAV or MP3.
//
// Each track plays the *amped* file of a take ("<take> (amp).wav", already
// rendered with the rig that was active when it was recorded), mixed into the
// output after the rig and after the take recorder's output tap, so backing
// tracks never end up inside the new take. The live guitar goes through the rig
// as usual. Recording itself is TakeRecorder's job (clean + amped files).
//
// Threading: processOutputMix runs on the audio thread and only reads
// immutable, already loaded buffers through atomic pointers. Everything else
// runs on the message thread; retired buffers stay owned for a few more loads.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "IosAudioRoute.h"
#include "Mp3Encoder.h"
#include "TakeRecorder.h"

namespace t3k {

class Multitrack {
public:
  static constexpr int kTracks = 6;

  explicit Multitrack(TakeRecorder& takeRecorder) : recorder(takeRecorder) {
    for (int t = 0; t < kTracks; ++t) {
      loaded[t].store(nullptr);
      gain[t].store(0.0f);
      offsetFrames[t].store(0);
    }
  }

  void prepare(double newSampleRate) {
    if (newSampleRate > 0.0) sampleRate = newSampleRate;
  }

  // ---------------------------------------------------------------- audio

  // End of processBlock, after the take recorder's output tap: adds the
  // playing tracks to the output.
  void processOutputMix(juce::AudioBuffer<float>& buffer) noexcept {
    if (!playing.load()) return;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0) return;

    const juce::int64 len = length.load();
    const bool isRecording = recordingTrack.load() >= 0;
    juce::int64 p = pos.load();
    if (len <= 0 && !isRecording) {
      playing.store(false);
      return;
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = nch > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int t = 0; t < kTracks; ++t) {
      const Loaded* track = loaded[t].load();
      const float g = gain[t].load();
      if (track == nullptr || g <= 0.0f) continue;
      const juce::int64 off = offsetFrames[t].load();
      const float* l = track->audio.getReadPointer(0);
      const float* r = track->audio.getReadPointer(1);
      for (int i = 0; i < n; ++i) {
        const juce::int64 idx = p + i + off;
        if (idx < 0 || idx >= track->length) continue;
        const float a = l[idx] * g;
        const float b = r[idx] * g;
        if (outR != nullptr) {
          outL[i] += a;
          outR[i] += b;
        } else {
          outL[i] += 0.5f * (a + b);
        }
      }
    }

    p += n;
    if (!isRecording && p >= len) {
      if (loop.load()) {
        p = 0;
      } else {
        p = len;
        playing.store(false);
      }
    }
    pos.store(p);
  }

  // -------------------------------------------------------------- commands

  // arg: for per-track commands an object { track, value }; otherwise a plain value.
  juce::var handleCommand(const juce::String& cmd, const juce::var& arg) {
    error.clear();
    if (!initialised) init();
    const int track = arg.isObject() ? static_cast<int>(arg.getProperty("track", -1)) : -1;
    const juce::var value = arg.isObject() ? arg.getProperty("value", {}) : arg;
    const bool trackOk = track >= 0 && track < kTracks;

    if (cmd == "play") {
      play();
    } else if (cmd == "stop") {
      stop();
    } else if (cmd == "rewind") {
      pos.store(0);
    } else if (cmd == "loop") {
      loop.store(static_cast<bool>(value));
    } else if (cmd == "seek") {
      seek(static_cast<double>(value));
    } else if (cmd == "record" && trackOk) {
      startRecording(track);
    } else if (cmd == "volume" && trackOk) {
      tracks[static_cast<size_t>(track)].volume = juce::jlimit(0.0f, 1.5f, static_cast<float>(static_cast<double>(value)));
      applyMixerState();
      saveProject();
    } else if (cmd == "mute" && trackOk) {
      tracks[static_cast<size_t>(track)].mute = static_cast<bool>(value);
      applyMixerState();
      saveProject();
    } else if (cmd == "solo" && trackOk) {
      tracks[static_cast<size_t>(track)].solo = static_cast<bool>(value);
      applyMixerState();
      saveProject();
    } else if (cmd == "nudge" && trackOk) {
      tracks[static_cast<size_t>(track)].nudgeMs = juce::jlimit(-300.0f, 300.0f, static_cast<float>(static_cast<double>(value)));
      applyMixerState();
      saveProject();
    } else if (cmd == "clear" && trackOk) {
      clearTrack(track);
    } else if (cmd == "mix") {
      exportMix(static_cast<bool>(value));
    }
    return getState();
  }

  juce::var getState() {
    if (!initialised) init();
    auto* o = new juce::DynamicObject();
    o->setProperty("playing", playing.load());
    o->setProperty("recording", recordingTrack.load() >= 0);
    o->setProperty("recordingTrack", recordingTrack.load());
    o->setProperty("loop", loop.load());
    o->setProperty("position", static_cast<double>(pos.load()) / sampleRate);
    o->setProperty("length", static_cast<double>(length.load()) / sampleRate);
    o->setProperty("error", error);
    o->setProperty("message", message);
    o->setProperty("mixPath", lastMix);
    o->setProperty("mixSerial", mixSerial);
    o->setProperty("latencyMs", defaultNudgeMs());
    juce::Array<juce::var> list;
    for (int t = 0; t < kTracks; ++t) {
      const auto& tr = tracks[static_cast<size_t>(t)];
      const Loaded* l = loaded[t].load();
      auto* e = new juce::DynamicObject();
      e->setProperty("loaded", l != nullptr);
      e->setProperty("seconds", l != nullptr ? static_cast<double>(l->length) / sampleRate : 0.0);
      e->setProperty("volume", tr.volume);
      e->setProperty("mute", tr.mute);
      e->setProperty("solo", tr.solo);
      e->setProperty("nudge", tr.nudgeMs);
      list.add(juce::var(e));
    }
    o->setProperty("tracks", list);
    return juce::var(o);
  }

private:
  struct Loaded {
    juce::AudioBuffer<float> audio;  // always 2 channels
    juce::int64 length = 0;
  };
  struct TrackInfo {
    juce::String takeId;
    float volume = 1.0f;
    bool mute = false, solo = false;
    float nudgeMs = 0.0f;
    std::vector<std::unique_ptr<Loaded>> owned;  // current + a few retired
  };

  static constexpr double kMaxTrackSeconds = 600.0;

  juce::File directory() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Recordings");
  }
  juce::File projectFile() const { return directory().getChildFile("project.json"); }

  // Round-trip latency estimate used as the nudge of a freshly recorded track.
  float defaultNudgeMs() const {
    const double ms = IosAudioRoute::roundTripLatencyMs();
    return ms > 0.0 ? static_cast<float>(ms) : 30.0f;
  }

  // ----------------------------------------------------------- transport

  void play() {
    if (recordingTrack.load() >= 0) return;
    if (length.load() <= 0) {
      error = "No tracks to play.";
      return;
    }
    if (pos.load() >= length.load()) pos.store(0);
    playing.store(true);
  }

  void stop() {
    const int rec = recordingTrack.load();
    if (rec >= 0) {
      finishRecording(rec);
      return;
    }
    playing.store(false);
  }

  void seek(double fraction) {
    const juce::int64 len = length.load();
    if (len <= 0 || recordingTrack.load() >= 0) return;
    pos.store(static_cast<juce::int64>(juce::jlimit(0.0, 1.0, fraction) * static_cast<double>(len)));
  }

  // ----------------------------------------------------------- recording

  void startRecording(int track) {
    if (recordingTrack.load() >= 0) return;
    // Amped file is what plays back, so it must be recorded.
    recorder.handleCommand("recordAmp", true);
    const juce::var st = recorder.handleCommand("record", {});
    if (!static_cast<bool>(st.getProperty("recording", false))) {
      error = st.getProperty("error", {}).toString();
      if (error.isEmpty()) error = "Could not start recording.";
      return;
    }
    playing.store(false);
    if (pos.load() >= length.load()) pos.store(0);
    recordingTrack.store(track);
    applyMixerState();  // silences the track being replaced
    playing.store(true);
    message = "Recording track " + juce::String(track + 1) + "...";
  }

  void finishRecording(int track) {
    playing.store(false);
    recorder.handleCommand("stop", {});
    recordingTrack.store(-1);
    pos.store(0);
    const juce::String name = recorder.lastRecordedName();
    const juce::File wet = directory().getChildFile(name + " (amp).wav");
    if (name.isEmpty() || !wet.existsAsFile()) {
      error = "The recording was not saved.";
      applyMixerState();
      return;
    }
    auto& tr = tracks[static_cast<size_t>(track)];
    tr.takeId = name;
    tr.nudgeMs = defaultNudgeMs();
    if (!loadTrack(track)) {
      tr.takeId = {};
      applyMixerState();
      return;
    }
    applyMixerState();
    saveProject();
    message = "Track " + juce::String(track + 1) + " recorded.";
  }

  void clearTrack(int track) {
    if (recordingTrack.load() >= 0) return;
    auto& tr = tracks[static_cast<size_t>(track)];
    tr.takeId = {};
    loaded[track].store(nullptr);
    applyMixerState();
    saveProject();
  }

  // ------------------------------------------------------------- loading

  // Reads "<take> (amp).wav" into memory as stereo at the engine rate.
  bool loadTrack(int track) {
    auto& tr = tracks[static_cast<size_t>(track)];
    const juce::File file = directory().getChildFile(tr.takeId + " (amp).wav");
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0 || reader->sampleRate <= 0.0) {
      error = "Could not read the track file.";
      return false;
    }
    const int inLen = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(kMaxTrackSeconds * reader->sampleRate)));
    const int inCh = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> tmp(inCh, inLen);
    reader->read(&tmp, 0, inLen, 0, true, true);

    const double ratio = reader->sampleRate / sampleRate;
    int outLen = inLen;
    if (std::abs(ratio - 1.0) > 1.0e-4) outLen = static_cast<int>(static_cast<double>(inLen) / ratio);
    auto l = std::make_unique<Loaded>();
    l->audio.setSize(2, juce::jmax(1, outLen));
    for (int c = 0; c < 2; ++c) {
      const float* src = tmp.getReadPointer(juce::jmin(c, inCh - 1));
      float* dst = l->audio.getWritePointer(c);
      for (int i = 0; i < outLen; ++i) {
        if (outLen == inLen) {
          dst[i] = src[i];
        } else {
          const double p = static_cast<double>(i) * ratio;
          const int i0 = juce::jmin(static_cast<int>(p), inLen - 1);
          const int i1 = juce::jmin(i0 + 1, inLen - 1);
          const float f = static_cast<float>(p - static_cast<double>(i0));
          dst[i] = src[i0] * (1.0f - f) + src[i1] * f;
        }
      }
    }
    l->length = outLen;
    loaded[track].store(l.get());
    tr.owned.push_back(std::move(l));
    while (tr.owned.size() > 3)
      tr.owned.erase(tr.owned.begin());
    return true;
  }

  // Pushes volumes / mute / solo / nudges to the audio thread and refreshes the length.
  void applyMixerState() {
    bool anySolo = false;
    for (int t = 0; t < kTracks; ++t)
      if (tracks[static_cast<size_t>(t)].solo && loaded[t].load() != nullptr) anySolo = true;
    juce::int64 maxLen = 0;
    for (int t = 0; t < kTracks; ++t) {
      const auto& tr = tracks[static_cast<size_t>(t)];
      float g = tr.volume;
      if (tr.mute || (anySolo && !tr.solo) || t == recordingTrack.load()) g = 0.0f;
      gain[t].store(g);
      offsetFrames[t].store(static_cast<int>(std::lround(static_cast<double>(tr.nudgeMs) * 0.001 * sampleRate)));
      if (const Loaded* l = loaded[t].load()) maxLen = std::max(maxLen, l->length);
    }
    length.store(maxLen);
  }

  // ------------------------------------------------------------- project

  void init() {
    initialised = true;
    const juce::var project = juce::JSON::parse(projectFile());
    if (auto* arr = project.getProperty("tracks", {}).getArray()) {
      for (int t = 0; t < kTracks && t < arr->size(); ++t) {
        const juce::var& e = arr->getReference(t);
        auto& tr = tracks[static_cast<size_t>(t)];
        tr.takeId = e.getProperty("take", {}).toString();
        tr.volume = static_cast<float>(static_cast<double>(e.getProperty("volume", 1.0)));
        tr.mute = static_cast<bool>(e.getProperty("mute", false));
        tr.solo = static_cast<bool>(e.getProperty("solo", false));
        tr.nudgeMs = static_cast<float>(static_cast<double>(e.getProperty("nudge", 0.0)));
        if (tr.takeId.isNotEmpty() && !loadTrack(t)) tr.takeId = {};
      }
    }
    error.clear();
    applyMixerState();
  }

  void saveProject() {
    juce::Array<juce::var> list;
    for (const auto& tr : tracks) {
      auto* e = new juce::DynamicObject();
      e->setProperty("take", tr.takeId);
      e->setProperty("volume", tr.volume);
      e->setProperty("mute", tr.mute);
      e->setProperty("solo", tr.solo);
      e->setProperty("nudge", tr.nudgeMs);
      list.add(juce::var(e));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("tracks", list);
    directory().createDirectory();
    projectFile().replaceWithText(juce::JSON::toString(juce::var(o)));
  }

  // -------------------------------------------------------------- mixdown

  static bool writeWav24(const juce::File& file, const juce::AudioBuffer<float>& audio, int rate) {
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (out.failedToOpen()) return false;
    const int channels = audio.getNumChannels();
    const int frames = audio.getNumSamples();
    const juce::int64 bytes = static_cast<juce::int64>(frames) * channels * 3;
    out.write("RIFF", 4);
    out.writeInt(static_cast<int>(36 + bytes));
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.writeInt(16);
    out.writeShort(1);
    out.writeShort(static_cast<short>(channels));
    out.writeInt(rate);
    out.writeInt(rate * channels * 3);
    out.writeShort(static_cast<short>(channels * 3));
    out.writeShort(24);
    out.write("data", 4);
    out.writeInt(static_cast<int>(bytes));
    std::vector<uint8_t> chunk;
    chunk.reserve(static_cast<size_t>(4096) * static_cast<size_t>(channels) * 3);
    for (int start = 0; start < frames; start += 4096) {
      const int count = juce::jmin(4096, frames - start);
      chunk.clear();
      for (int i = 0; i < count; ++i) {
        for (int c = 0; c < channels; ++c) {
          const int v = juce::roundToInt(juce::jlimit(-1.0f, 1.0f, audio.getSample(c, start + i)) * 8388607.0f);
          chunk.push_back(static_cast<uint8_t>(v & 0xFF));
          chunk.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
          chunk.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        }
      }
      out.write(chunk.data(), chunk.size());
    }
    out.flush();
    return true;
  }

  void exportMix(bool asMp3) {
    if (recordingTrack.load() >= 0) return;
    const juce::int64 len = length.load();
    if (len <= 0) {
      error = "No tracks to mix.";
      return;
    }
    playing.store(false);
    juce::AudioBuffer<float> mix(2, static_cast<int>(len));
    mix.clear();
    for (int t = 0; t < kTracks; ++t) {
      const Loaded* track = loaded[t].load();
      const float g = gain[t].load();
      if (track == nullptr || g <= 0.0f) continue;
      const juce::int64 off = offsetFrames[t].load();
      for (int c = 0; c < 2; ++c) {
        const float* src = track->audio.getReadPointer(c);
        float* dst = mix.getWritePointer(c);
        for (juce::int64 i = 0; i < len; ++i) {
          const juce::int64 idx = i + off;
          if (idx >= 0 && idx < track->length) dst[i] += src[idx] * g;
        }
      }
    }
    // Keep the sum from clipping.
    const float peak = std::max(mix.getMagnitude(0, 0, mix.getNumSamples()), mix.getMagnitude(1, 0, mix.getNumSamples()));
    if (peak > 0.999f) mix.applyGain(0.999f / peak);

    const auto dir = directory();
    dir.createDirectory();
    const juce::String name = "Mix " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    const juce::File wav = dir.getChildFile(name + ".wav");
    if (!writeWav24(wav, mix, juce::roundToInt(sampleRate))) {
      error = "Could not write the mix file.";
      return;
    }
    juce::File result = wav;
    if (asMp3) {
      const juce::File mp3 = dir.getChildFile(name + ".mp3");
      const juce::String err = encodeWavToMp3(wav, mp3);
      if (err.isNotEmpty()) {
        error = err;
        return;
      }
      result = mp3;
    }
    lastMix = result.getFullPathName();
    ++mixSerial;
    message = "Mix saved: " + result.getFileName();
  }

  // ---------------------------------------------------------------- state
  TakeRecorder& recorder;
  double sampleRate = 48000.0;

  std::atomic<const Loaded*> loaded[kTracks];
  std::atomic<float> gain[kTracks];
  std::atomic<int> offsetFrames[kTracks];
  std::atomic<bool> playing{false}, loop{false};
  std::atomic<juce::int64> pos{0}, length{0};
  std::atomic<int> recordingTrack{-1};

  std::vector<TrackInfo> tracks = std::vector<TrackInfo>(kTracks);
  juce::String error, message, lastMix;
  int mixSerial = 0;
  bool initialised = false;
};

}  // namespace t3k
