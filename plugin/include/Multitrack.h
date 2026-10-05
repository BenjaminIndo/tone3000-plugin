// Multitrack: a small fixed set of tracks that play together while a new one is
// recorded on top (overdub), with a metronome, imported audio, non-destructive
// trim / start / pan per track, a loop region, punch in/out, and a mixdown to
// WAV or MP3.
//
// A track plays an audio file from the recordings folder: the *amped* file of
// a take ("<take> (amp).wav", already rendered with the rig that was active
// when it was recorded) or an imported file ("Imports/<name>"). Tracks are
// mixed into the output after the rig and after the take recorder's output
// tap, so backing tracks and the click never end up inside a new take. The live
// guitar goes through the rig as usual. Recording itself is TakeRecorder's job
// (clean + amped files); Multitrack only decides which part of each block it
// may write (beginBlock) so a count-in or a punch records from an exact frame.
//
// Timeline: the transport position `pos` counts frames and may be negative
// during a count-in. A track reads file frame  q + readOffset  at timeline
// frame q, where readOffset = nudge - start + trimStart, and plays only inside
// [validLo, validHi).
//
// Threading: beginBlock / processOutputMix run on the audio thread and only read
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
#include <limits>
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
      gainL[t].store(0.0f);
      gainR[t].store(0.0f);
      readOffset[t].store(0);
      validLo[t].store(0);
      validHi[t].store(0);
    }
  }

  void prepare(double newSampleRate) {
    if (newSampleRate > 0.0) sampleRate = newSampleRate;
  }

  // ---------------------------------------------------------------- audio

  // Start of processBlock, before the take recorder sees the input: tells it
  // which part of this block it may write while a track is being recorded.
  void beginBlock(int n) noexcept {
    int from = 0;
    int to = 2147483647;
    const int recT = recordingTrack.load();
    if (recT >= 0) {
      from = 0;
      to = 0;  // closed until the transport is running and inside the window
      if (playing.load()) {
        const juce::int64 p = pos.load();
        juce::int64 cs = 0;
        juce::int64 ce = std::numeric_limits<juce::int64>::max() / 4;
        if (punchOn.load()) {
          cs = punchIn.load();
          ce = punchOut.load();
        }
        const juce::int64 a = std::max<juce::int64>(cs - p, 0);
        const juce::int64 b = std::min<juce::int64>(ce - p, n);
        if (b > a) {
          from = static_cast<int>(a);
          to = static_cast<int>(b);
        }
      }
    }
    recorder.setBlockWindow(from, to);
  }

  // End of processBlock, after the take recorder's output tap: adds the
  // playing tracks and the click to the output and advances the transport.
  void processOutputMix(juce::AudioBuffer<float>& buffer) noexcept {
    if (!playing.load()) return;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0) return;

    const int recT = recordingTrack.load();
    const bool isRecording = recT >= 0;
    const juce::int64 len = length.load();
    if (len <= 0 && !isRecording && !metroOn.load()) {
      playing.store(false);
      return;
    }

    const juce::int64 lIn = loopStart.load();
    const juce::int64 lOut = loopEnd.load();
    const bool loopActive = loopOn.load() && !isRecording && lOut > lIn;
    float* outL = buffer.getWritePointer(0);
    float* outR = nch > 1 ? buffer.getWritePointer(1) : nullptr;

    juce::int64 p = pos.load();
    int done = 0;
    while (done < n) {
      if (loopActive && p >= lOut) p = lIn;
      int seg = n - done;
      if (loopActive) seg = static_cast<int>(std::min<juce::int64>(seg, lOut - p));
      if (seg <= 0) break;
      mixSegment(outL, outR, done, seg, p, recT);
      renderClicks(outL, outR, done, seg, p);
      p += seg;
      done += seg;
    }
    if (loopActive && p >= lOut) p = lIn;
    if (!isRecording && !loopActive && len > 0 && p >= len) {
      p = len;
      playing.store(false);
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
    const double num = static_cast<double>(value);
    const bool flag = static_cast<bool>(value);

    if (cmd == "play") {
      play();
    } else if (cmd == "stop") {
      stop();
    } else if (cmd == "rewind") {
      if (recordingTrack.load() < 0) pos.store(loopOn.load() ? loopStart.load() : 0);
    } else if (cmd == "seekSec") {
      if (recordingTrack.load() < 0) pos.store(std::max<juce::int64>(0, toFrames(num)));
    } else if (cmd == "loop") {
      loopOn.store(flag);
      saveProject();
    } else if (cmd == "loopIn") {
      markLoop(true);
    } else if (cmd == "loopOut") {
      markLoop(false);
    } else if (cmd == "punch") {
      punchOn.store(flag);
      saveProject();
    } else if (cmd == "punchIn") {
      markPunch(true);
    } else if (cmd == "punchOut") {
      markPunch(false);
    } else if (cmd == "metro") {
      metroOn.store(flag);
      saveProject();
    } else if (cmd == "bpm") {
      bpm.store(static_cast<float>(juce::jlimit(30.0, 300.0, num)));
      saveProject();
    } else if (cmd == "sig") {
      const juce::String s = value.toString();
      const int beats = s.upToFirstOccurrenceOf("/", false, false).getIntValue();
      const int denom = s.fromFirstOccurrenceOf("/", false, false).getIntValue();
      if (beats >= 1 && beats <= 12 && (denom == 4 || denom == 8)) {
        beatsPerBar.store(beats);
        beatDenom.store(denom);
        saveProject();
      }
    } else if (cmd == "countIn") {
      countInBars.store(juce::jlimit(0, 2, static_cast<int>(num)));
      saveProject();
    } else if (cmd == "clickVol") {
      clickVol.store(static_cast<float>(juce::jlimit(0.0, 1.0, num)));
      saveProject();
    } else if (cmd == "record" && trackOk) {
      startRecording(track);
    } else if (trackOk && (cmd == "volume" || cmd == "mute" || cmd == "solo" || cmd == "nudge" || cmd == "pan" ||
                           cmd == "start" || cmd == "trimStart" || cmd == "trimEnd")) {
      editTrack(track, cmd, num, flag);
    } else if (cmd == "clear" && trackOk) {
      clearTrack(track);
    } else if (cmd == "import" && trackOk) {
      importTrack(track, value.toString());
    } else if (cmd == "peaks" && trackOk) {
      peaksRequest = track;
    } else if (cmd == "mix") {
      exportMix(flag);
    }
    return getState();
  }

  juce::var getState() {
    if (!initialised) init();
    // A punch-in recording ends by itself shortly after its out mark.
    const int rt = recordingTrack.load();
    if (rt >= 0 && punchOn.load() && pos.load() >= punchOut.load() + toFrames(0.15)) finishRecording(rt);

    auto* o = new juce::DynamicObject();
    o->setProperty("playing", playing.load());
    o->setProperty("recording", recordingTrack.load() >= 0);
    o->setProperty("recordingTrack", recordingTrack.load());
    o->setProperty("loop", loopOn.load());
    o->setProperty("loopIn", toSeconds(loopStart.load()));
    o->setProperty("loopOut", toSeconds(loopEnd.load()));
    o->setProperty("punch", punchOn.load());
    o->setProperty("punchIn", toSeconds(punchIn.load()));
    o->setProperty("punchOut", toSeconds(punchOut.load()));
    o->setProperty("metro", metroOn.load());
    o->setProperty("bpm", bpm.load());
    o->setProperty("beats", beatsPerBar.load());
    o->setProperty("denom", beatDenom.load());
    o->setProperty("countIn", countInBars.load());
    o->setProperty("clickVol", clickVol.load());
    o->setProperty("position", toSeconds(pos.load()));
    o->setProperty("length", toSeconds(length.load()));
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
      e->setProperty("imported", tr.source.startsWith("Imports/"));
      e->setProperty("fileSeconds", l != nullptr ? toSeconds(l->length) : 0.0);
      e->setProperty("volume", tr.volume);
      e->setProperty("mute", tr.mute);
      e->setProperty("solo", tr.solo);
      e->setProperty("nudge", tr.nudgeMs);
      e->setProperty("pan", tr.pan);
      e->setProperty("start", tr.startSec);
      e->setProperty("trimStart", tr.trimStartSec);
      e->setProperty("trimEnd", tr.trimEndSec);
      e->setProperty("rev", tr.rev);
      // Where the visible clip sits on the timeline, in seconds.
      const juce::int64 off = readOffset[t].load();
      e->setProperty("clipStart", l != nullptr ? toSeconds(validLo[t].load() - off) : 0.0);
      e->setProperty("clipLength", l != nullptr ? toSeconds(validHi[t].load() - validLo[t].load()) : 0.0);
      list.add(juce::var(e));
    }
    o->setProperty("tracks", list);
    if (peaksRequest >= 0) {
      juce::Array<juce::var> pk;
      for (const float v : tracks[static_cast<size_t>(peaksRequest)].peaks)
        pk.add(v);
      o->setProperty("peaksTrack", peaksRequest);
      o->setProperty("peaks", pk);
      peaksRequest = -1;
    }
    return juce::var(o);
  }

private:
  struct Loaded {
    juce::AudioBuffer<float> audio;  // always 2 channels
    juce::int64 length = 0;
  };
  struct TrackInfo {
    juce::String source;  // relative to the recordings folder
    float volume = 1.0f;
    bool mute = false, solo = false;
    float nudgeMs = 0.0f;
    float pan = 0.0f;
    double startSec = 0.0;
    double trimStartSec = 0.0;
    double trimEndSec = 0.0;  // 0 = to the end of the file
    int rev = 0;              // bumped whenever the audio changes
    std::vector<float> peaks;  // min / max pairs over the whole file
    std::vector<std::unique_ptr<Loaded>> owned;  // current + a few retired
  };

  static constexpr double kMaxTrackSeconds = 600.0;
  static constexpr int kPeakBuckets = 1000;

  juce::int64 toFrames(double seconds) const { return static_cast<juce::int64>(std::llround(seconds * sampleRate)); }
  double toSeconds(juce::int64 frames) const { return static_cast<double>(frames) / sampleRate; }

  juce::File directory() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Recordings");
  }
  juce::File projectFile() const { return directory().getChildFile("project.json"); }

  // Round-trip latency estimate used as the nudge of a freshly recorded track.
  float defaultNudgeMs() const {
    const double ms = IosAudioRoute::roundTripLatencyMs();
    return ms > 0.0 ? static_cast<float>(ms) : 30.0f;
  }

  double framesPerBeat() const {
    return sampleRate * 60.0 / static_cast<double>(bpm.load()) * 4.0 / static_cast<double>(beatDenom.load());
  }
  juce::int64 framesPerBar() const {
    return static_cast<juce::int64>(std::llround(framesPerBeat() * beatsPerBar.load()));
  }

  // ------------------------------------------------------- audio internals

  void mixSegment(float* outL, float* outR, int outOffset, int count, juce::int64 q0, int recT) noexcept {
    const bool punch = punchOn.load();
    const juce::int64 pIn = punchIn.load();
    const juce::int64 pOut = punchOut.load();
    for (int t = 0; t < kTracks; ++t) {
      const Loaded* track = loaded[t].load();
      if (track == nullptr) continue;
      const float gl = gainL[t].load();
      const float gr = gainR[t].load();
      if (gl <= 0.0f && gr <= 0.0f) continue;
      const juce::int64 off = readOffset[t].load();
      const juce::int64 lo = validLo[t].load();
      const juce::int64 hi = validHi[t].load();
      const float* l = track->audio.getReadPointer(0);
      const float* r = track->audio.getReadPointer(1);
      const bool replacing = (t == recT);
      for (int i = 0; i < count; ++i) {
        const juce::int64 q = q0 + i;
        if (q < 0) continue;
        // The track being re-recorded is silent where its new audio will go:
        // everywhere for a full take, only inside the marks for a punch.
        if (replacing && (!punch || (q >= pIn && q < pOut))) continue;
        const juce::int64 fi = q + off;
        if (fi < lo || fi >= hi) continue;
        const float a = l[fi] * gl;
        const float b = r[fi] * gr;
        if (outR != nullptr) {
          outL[outOffset + i] += a;
          outR[outOffset + i] += b;
        } else {
          outL[outOffset + i] += 0.5f * (a + b);
        }
      }
    }
  }

  // Short sine bursts on every beat (higher on the first of the bar).
  void renderClicks(float* outL, float* outR, int outOffset, int count, juce::int64 q0) noexcept {
    if (!metroOn.load()) return;
    const double spb = framesPerBeat();
    if (spb < 1.0) return;
    const int bpb = juce::jmax(1, beatsPerBar.load());
    const float vol = clickVol.load();
    const float sr = static_cast<float>(sampleRate);

    juce::int64 k = static_cast<juce::int64>(std::ceil(static_cast<double>(q0) / spb));
    juce::int64 nextBeat = static_cast<juce::int64>(std::llround(static_cast<double>(k) * spb));
    for (int i = 0; i < count; ++i) {
      const juce::int64 q = q0 + i;
      while (nextBeat <= q) {
        if (nextBeat == q) {
          const juce::int64 inBar = ((k % bpb) + bpb) % bpb;
          clickFreq = inBar == 0 ? 1500.0f : 1000.0f;
          clickLen = juce::jmax(1, static_cast<int>(0.03f * sr));
          clickLeft = clickLen;
          clickPhase = 0.0f;
        }
        ++k;
        nextBeat = static_cast<juce::int64>(std::llround(static_cast<double>(k) * spb));
      }
      if (clickLeft > 0) {
        float env = static_cast<float>(clickLeft) / static_cast<float>(clickLen);
        env *= env;
        const float s = std::sin(clickPhase) * env * vol;
        clickPhase += 2.0f * juce::MathConstants<float>::pi * clickFreq / sr;
        --clickLeft;
        outL[outOffset + i] += s;
        if (outR != nullptr) outR[outOffset + i] += s;
      }
    }
  }

  // ----------------------------------------------------------- transport

  void play() {
    if (recordingTrack.load() >= 0) return;
    if (length.load() <= 0 && !metroOn.load()) {
      error = "No tracks to play.";
      return;
    }
    const juce::int64 len = length.load();
    if (len > 0 && pos.load() >= len) pos.store(loopOn.load() ? loopStart.load() : 0);
    clickLeft = 0;
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

  void markLoop(bool isIn) {
    if (recordingTrack.load() >= 0) return;
    const juce::int64 p = std::max<juce::int64>(0, pos.load());
    if (isIn) {
      loopStart.store(p);
      if (loopEnd.load() <= p) loopEnd.store(std::max(length.load(), p + toFrames(2.0)));
    } else {
      loopEnd.store(p);
      if (loopStart.load() >= p) loopStart.store(std::max<juce::int64>(0, p - toFrames(2.0)));
    }
    saveProject();
  }

  void markPunch(bool isIn) {
    if (recordingTrack.load() >= 0) return;
    const juce::int64 p = std::max<juce::int64>(0, pos.load());
    if (isIn) {
      punchIn.store(p);
      if (punchOut.load() <= p) punchOut.store(p + toFrames(4.0));
    } else {
      punchOut.store(p);
      if (punchIn.load() >= p) punchIn.store(std::max<juce::int64>(0, p - toFrames(4.0)));
    }
    saveProject();
  }

  // ----------------------------------------------------------- recording

  void startRecording(int track) {
    if (recordingTrack.load() >= 0) return;
    const bool punch = punchOn.load();
    if (punch && punchOut.load() <= punchIn.load()) {
      error = "Set punch in and out first.";
      return;
    }
    if (punch && loaded[track].load() == nullptr) {
      error = "Punch needs a track that already has audio.";
      return;
    }
    // The amped file is what plays back, so it must be recorded.
    recorder.handleCommand("recordAmp", true);
    playing.store(false);
    recordingTrack.store(track);  // closes the capture window until playing starts

    juce::int64 startPos = 0;
    if (punch)
      startPos = std::max<juce::int64>(0, punchIn.load() - toFrames(2.0));  // short pre-roll
    else
      startPos = -static_cast<juce::int64>(countInBars.load()) * framesPerBar();  // count-in
    pos.store(startPos);
    clickLeft = 0;

    const juce::var st = recorder.handleCommand("record", {});
    if (!static_cast<bool>(st.getProperty("recording", false))) {
      recordingTrack.store(-1);
      error = st.getProperty("error", {}).toString();
      if (error.isEmpty()) error = "Could not start recording.";
      return;
    }
    playing.store(true);
    message = punch ? "Punch recording track " + juce::String(track + 1) + "..."
                    : "Recording track " + juce::String(track + 1) + "...";
  }

  void finishRecording(int track) {
    playing.store(false);
    recorder.handleCommand("stop", {});
    recordingTrack.store(-1);
    const bool wasPunch = punchOn.load() && punchOut.load() > punchIn.load();
    pos.store(0);

    const juce::String name = recorder.lastRecordedName();
    const juce::File wet = directory().getChildFile(name + " (amp).wav");
    if (name.isEmpty() || !wet.existsAsFile()) {
      error = "The recording was not saved.";
      applyMixerState();
      return;
    }
    auto& tr = tracks[static_cast<size_t>(track)];
    const bool hadAudio = loaded[track].load() != nullptr;
    const juce::String previous = tr.source;
    if (wasPunch && hadAudio) {
      if (!mergePunch(track, wet)) {
        applyMixerState();
        return;
      }
    } else {
      tr.startSec = wasPunch ? toSeconds(punchIn.load()) : 0.0;
      tr.trimStartSec = 0.0;
      tr.trimEndSec = 0.0;
      tr.nudgeMs = defaultNudgeMs();
    }
    tr.source = name + " (amp).wav";
    if (!loadTrack(track)) {
      tr.source = previous;
      applyMixerState();
      return;
    }
    applyMixerState();
    saveProject();
    message = "Track " + juce::String(track + 1) + " recorded.";
  }

  // The punch recording holds only the punched part: lay it over the track's
  // existing audio (5 ms fades at the edges) and write the result back to the
  // new "(amp)" file, which then becomes the track's source.
  bool mergePunch(int track, const juce::File& segmentFile) {
    const Loaded* old = loaded[track].load();
    juce::String err;
    std::unique_ptr<Loaded> seg = readStereo(segmentFile, err);
    if (old == nullptr || seg == nullptr) {
      error = err.isNotEmpty() ? err : "Could not merge the punch recording.";
      return false;
    }
    const juce::int64 idx0 = punchIn.load() + readOffset[track].load();  // file frame of segment frame 0
    const juce::int64 skip = std::max<juce::int64>(0, -idx0);
    const juce::int64 newLen = std::max(old->length, idx0 + seg->length);
    if (newLen <= 0 || newLen > static_cast<juce::int64>(kMaxTrackSeconds * sampleRate)) {
      error = "The merged track would be too long.";
      return false;
    }
    juce::AudioBuffer<float> merged(2, static_cast<int>(newLen));
    merged.clear();
    for (int c = 0; c < 2; ++c)
      merged.copyFrom(c, 0, old->audio, c, 0, static_cast<int>(old->length));

    const int fade = juce::jmax(1, static_cast<int>(0.005 * sampleRate));
    for (juce::int64 j = skip; j < seg->length; ++j) {
      const juce::int64 dst = idx0 + j;
      if (dst < 0 || dst >= newLen) continue;
      const float wIn = std::min(1.0f, static_cast<float>(j - skip) / static_cast<float>(fade));
      const float wOut = std::min(1.0f, static_cast<float>(seg->length - 1 - j) / static_cast<float>(fade));
      const float w = std::min(wIn, wOut);
      for (int c = 0; c < 2; ++c) {
        const float prev = dst < old->length ? old->audio.getSample(c, static_cast<int>(dst)) : 0.0f;
        merged.setSample(c, static_cast<int>(dst), prev * (1.0f - w) + seg->audio.getSample(c, static_cast<int>(j)) * w);
      }
    }
    if (!writeWav24(segmentFile, merged, juce::roundToInt(sampleRate))) {
      error = "Could not write the merged track.";
      return false;
    }
    return true;
  }

  // ------------------------------------------------------- track editing

  void editTrack(int track, const juce::String& cmd, double num, bool flag) {
    auto& tr = tracks[static_cast<size_t>(track)];
    if (cmd == "volume") tr.volume = static_cast<float>(juce::jlimit(0.0, 1.5, num));
    else if (cmd == "mute") tr.mute = flag;
    else if (cmd == "solo") tr.solo = flag;
    else if (cmd == "nudge") tr.nudgeMs = static_cast<float>(juce::jlimit(-300.0, 300.0, num));
    else if (cmd == "pan") tr.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, num));
    else if (cmd == "start") tr.startSec = juce::jmax(0.0, num);
    else if (cmd == "trimStart") tr.trimStartSec = juce::jmax(0.0, num);
    else if (cmd == "trimEnd") tr.trimEndSec = juce::jmax(0.0, num);
    applyMixerState();
    saveProject();
  }

  void clearTrack(int track) {
    if (recordingTrack.load() >= 0) return;
    auto& tr = tracks[static_cast<size_t>(track)];
    tr.source = {};
    tr.startSec = tr.trimStartSec = tr.trimEndSec = 0.0;
    tr.nudgeMs = 0.0f;
    tr.peaks.clear();
    ++tr.rev;
    loaded[track].store(nullptr);
    applyMixerState();
    saveProject();
  }

  void importTrack(int track, const juce::String& relativePath) {
    if (recordingTrack.load() >= 0) return;
    auto& tr = tracks[static_cast<size_t>(track)];
    const juce::String previous = tr.source;
    tr.source = relativePath;
    if (!loadTrack(track)) {
      tr.source = previous;
      if (error.isEmpty()) error = "Could not read that audio file.";
      return;
    }
    tr.startSec = tr.trimStartSec = tr.trimEndSec = 0.0;
    tr.nudgeMs = 0.0f;
    applyMixerState();
    saveProject();
    message = "Imported into track " + juce::String(track + 1) + ".";
  }

  // ------------------------------------------------------------- loading

  // Reads any supported audio file as stereo at the engine rate.
  std::unique_ptr<Loaded> readStereo(const juce::File& file, juce::String& err) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0 || reader->sampleRate <= 0.0) {
      err = "Could not read that audio file (it may be protected or unsupported).";
      return nullptr;
    }
    const int inLen = static_cast<int>(juce::jmin<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(kMaxTrackSeconds * reader->sampleRate)));
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
    return l;
  }

  bool loadTrack(int track) {
    auto& tr = tracks[static_cast<size_t>(track)];
    juce::String err;
    std::unique_ptr<Loaded> l = readStereo(directory().getChildFile(tr.source), err);
    if (l == nullptr) {
      error = err;
      return false;
    }
    // Waveform overview: min / max over equal slices of the whole file.
    tr.peaks.assign(static_cast<size_t>(kPeakBuckets) * 2, 0.0f);
    const float* a = l->audio.getReadPointer(0);
    const float* b = l->audio.getReadPointer(1);
    for (int k = 0; k < kPeakBuckets; ++k) {
      const juce::int64 from = l->length * k / kPeakBuckets;
      const juce::int64 to = std::max(from + 1, l->length * (k + 1) / kPeakBuckets);
      float lo = 0.0f, hi = 0.0f;
      for (juce::int64 i = from; i < to && i < l->length; ++i) {
        const float v = 0.5f * (a[i] + b[i]);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      tr.peaks[static_cast<size_t>(k) * 2] = lo;
      tr.peaks[static_cast<size_t>(k) * 2 + 1] = hi;
    }
    loaded[track].store(l.get());
    tr.owned.push_back(std::move(l));
    while (tr.owned.size() > 3)
      tr.owned.erase(tr.owned.begin());
    ++tr.rev;
    return true;
  }

  // Pushes volumes / pan / mute / solo / nudge / trim / start to the audio
  // thread and refreshes the project length.
  void applyMixerState() {
    bool anySolo = false;
    for (int t = 0; t < kTracks; ++t)
      if (tracks[static_cast<size_t>(t)].solo && loaded[t].load() != nullptr) anySolo = true;
    juce::int64 maxEnd = 0;
    for (int t = 0; t < kTracks; ++t) {
      const auto& tr = tracks[static_cast<size_t>(t)];
      float g = tr.volume;
      if (tr.mute || (anySolo && !tr.solo)) g = 0.0f;
      // Balance law: the far side is attenuated, the near side keeps its level.
      gainL[t].store(g * (tr.pan > 0.0f ? 1.0f - tr.pan : 1.0f));
      gainR[t].store(g * (tr.pan < 0.0f ? 1.0f + tr.pan : 1.0f));

      const juce::int64 nudge = toFrames(static_cast<double>(tr.nudgeMs) * 0.001);
      const juce::int64 start = toFrames(tr.startSec);
      const juce::int64 lo = toFrames(tr.trimStartSec);
      readOffset[t].store(nudge - start + lo);
      if (const Loaded* l = loaded[t].load()) {
        const juce::int64 fileLo = std::min(lo, l->length);
        juce::int64 hi = l->length;
        if (tr.trimEndSec > 0.0) hi = std::min(hi, toFrames(tr.trimEndSec));
        hi = std::max(hi, fileLo);
        validLo[t].store(fileLo);
        validHi[t].store(hi);
        maxEnd = std::max(maxEnd, hi - (nudge - start + lo));
      } else {
        validLo[t].store(0);
        validHi[t].store(0);
      }
    }
    length.store(maxEnd);
  }

  // ------------------------------------------------------------- project

  void init() {
    initialised = true;
    const juce::var project = juce::JSON::parse(projectFile());
    if (project.isObject()) {
      bpm.store(static_cast<float>(juce::jlimit(30.0, 300.0, static_cast<double>(project.getProperty("bpm", 120.0)))));
      beatsPerBar.store(juce::jlimit(1, 12, static_cast<int>(project.getProperty("beats", 4))));
      const int denom = static_cast<int>(project.getProperty("denom", 4));
      beatDenom.store(denom == 8 ? 8 : 4);
      countInBars.store(juce::jlimit(0, 2, static_cast<int>(project.getProperty("countIn", 0))));
      clickVol.store(static_cast<float>(juce::jlimit(0.0, 1.0, static_cast<double>(project.getProperty("clickVol", 0.5)))));
      metroOn.store(static_cast<bool>(project.getProperty("metro", false)));
      loopOn.store(static_cast<bool>(project.getProperty("loop", false)));
      loopStart.store(toFrames(static_cast<double>(project.getProperty("loopIn", 0.0))));
      loopEnd.store(toFrames(static_cast<double>(project.getProperty("loopOut", 0.0))));
      punchOn.store(static_cast<bool>(project.getProperty("punch", false)));
      punchIn.store(toFrames(static_cast<double>(project.getProperty("punchIn", 0.0))));
      punchOut.store(toFrames(static_cast<double>(project.getProperty("punchOut", 0.0))));
    }
    if (auto* arr = project.getProperty("tracks", {}).getArray()) {
      for (int t = 0; t < kTracks && t < arr->size(); ++t) {
        const juce::var& e = arr->getReference(t);
        auto& tr = tracks[static_cast<size_t>(t)];
        // Older projects stored just the take name.
        tr.source = e.getProperty("source", {}).toString();
        if (tr.source.isEmpty()) {
          const juce::String take = e.getProperty("take", {}).toString();
          if (take.isNotEmpty()) tr.source = take + " (amp).wav";
        }
        tr.volume = static_cast<float>(static_cast<double>(e.getProperty("volume", 1.0)));
        tr.mute = static_cast<bool>(e.getProperty("mute", false));
        tr.solo = static_cast<bool>(e.getProperty("solo", false));
        tr.nudgeMs = static_cast<float>(static_cast<double>(e.getProperty("nudge", 0.0)));
        tr.pan = static_cast<float>(static_cast<double>(e.getProperty("pan", 0.0)));
        tr.startSec = static_cast<double>(e.getProperty("start", 0.0));
        tr.trimStartSec = static_cast<double>(e.getProperty("trimStart", 0.0));
        tr.trimEndSec = static_cast<double>(e.getProperty("trimEnd", 0.0));
        if (tr.source.isNotEmpty() && !loadTrack(t)) tr.source = {};
      }
    }
    error.clear();
    applyMixerState();
  }

  void saveProject() {
    juce::Array<juce::var> list;
    for (const auto& tr : tracks) {
      auto* e = new juce::DynamicObject();
      e->setProperty("source", tr.source);
      e->setProperty("volume", tr.volume);
      e->setProperty("mute", tr.mute);
      e->setProperty("solo", tr.solo);
      e->setProperty("nudge", tr.nudgeMs);
      e->setProperty("pan", tr.pan);
      e->setProperty("start", tr.startSec);
      e->setProperty("trimStart", tr.trimStartSec);
      e->setProperty("trimEnd", tr.trimEndSec);
      list.add(juce::var(e));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("tracks", list);
    o->setProperty("bpm", bpm.load());
    o->setProperty("beats", beatsPerBar.load());
    o->setProperty("denom", beatDenom.load());
    o->setProperty("countIn", countInBars.load());
    o->setProperty("clickVol", clickVol.load());
    o->setProperty("metro", metroOn.load());
    o->setProperty("loop", loopOn.load());
    o->setProperty("loopIn", toSeconds(loopStart.load()));
    o->setProperty("loopOut", toSeconds(loopEnd.load()));
    o->setProperty("punch", punchOn.load());
    o->setProperty("punchIn", toSeconds(punchIn.load()));
    o->setProperty("punchOut", toSeconds(punchOut.load()));
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
    float* dstL = mix.getWritePointer(0);
    float* dstR = mix.getWritePointer(1);
    for (int t = 0; t < kTracks; ++t) {
      const Loaded* track = loaded[t].load();
      if (track == nullptr) continue;
      const float gl = gainL[t].load();
      const float gr = gainR[t].load();
      if (gl <= 0.0f && gr <= 0.0f) continue;
      const juce::int64 off = readOffset[t].load();
      const juce::int64 lo = validLo[t].load();
      const juce::int64 hi = validHi[t].load();
      const float* l = track->audio.getReadPointer(0);
      const float* r = track->audio.getReadPointer(1);
      for (juce::int64 q = 0; q < len; ++q) {
        const juce::int64 fi = q + off;
        if (fi < lo || fi >= hi) continue;
        dstL[q] += l[fi] * gl;
        dstR[q] += r[fi] * gr;
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
  std::atomic<float> gainL[kTracks];
  std::atomic<float> gainR[kTracks];
  std::atomic<juce::int64> readOffset[kTracks];
  std::atomic<juce::int64> validLo[kTracks];
  std::atomic<juce::int64> validHi[kTracks];
  std::atomic<bool> playing{false}, loopOn{false}, punchOn{false}, metroOn{false};
  std::atomic<juce::int64> pos{0}, length{0}, loopStart{0}, loopEnd{0}, punchIn{0}, punchOut{0};
  std::atomic<int> recordingTrack{-1}, beatsPerBar{4}, beatDenom{4}, countInBars{0};
  std::atomic<float> bpm{120.0f}, clickVol{0.5f};

  // Click synthesis state: audio thread only (reset from the message thread
  // only while the transport is stopped).
  float clickPhase = 0.0f;
  float clickFreq = 1000.0f;
  int clickLeft = 0;
  int clickLen = 1;

  std::vector<TrackInfo> tracks = std::vector<TrackInfo>(kTracks);
  juce::String error, message, lastMix;
  int mixSerial = 0;
  int peaksRequest = -1;
  bool initialised = false;
};

}  // namespace t3k
