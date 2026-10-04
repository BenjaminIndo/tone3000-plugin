// Take recorder: records the clean guitar input (and, at the same time, the
// processed output) to WAV files, plays a recorded take back in place of the
// live input so any rig can be dialled in on it (reamp), and bounces a take
// through the current rig to a new file.
//
// Threading:
//  - processInput / processOutput run on the audio thread. They never
//    allocate, lock or touch files: they copy into a lock-free FIFO (writers)
//    or read an immutable, already loaded Take (playback).
//  - Everything else runs on the message thread (UI commands, the poll timer).
//  - Takes handed to the audio thread are never freed while they may still be
//    in use: a retired take stays owned for a few more loads.
//
// WAV files are written by hand (44-byte header + 24-bit PCM) from a small
// background thread, so the recorder only depends on stable JUCE basics.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "Mp3Encoder.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace t3k {

class TakeRecorder : private juce::Timer {
public:
  TakeRecorder() = default;
  ~TakeRecorder() override {
    stopTimer();
    dryWriter.close();
    wetWriter.close();
  }

  // Called from prepareToPlay (message thread or host thread, never during
  // processing of the same block).
  void prepare(double newSampleRate) {
    if (newSampleRate > 0.0) sampleRate = newSampleRate;
    tailSamples.store(static_cast<juce::int64>(sampleRate * kTailSeconds));
  }

  // ---------------------------------------------------------------- audio

  // Right after the input fold, before gain: records the clean signal, or
  // replaces it with the playing take (or silence during an export tail).
  void processInput(juce::AudioBuffer<float>& buffer) noexcept {
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0) return;

    if (recording.load()) {
      const float* ch0 = buffer.getReadPointer(0);
      dryWriter.push(&ch0, 1, n);
      recordedFrames.fetch_add(n);
      return;
    }

    const bool isExporting = exporting.load();
    if (playing.load()) {
      const Take* t = current.load();
      if (t == nullptr || t->length <= 0) {
        playing.store(false);
        return;
      }
      const juce::int64 len = t->length;
      const float* src = t->mono.getReadPointer(0);
      float* out = buffer.getWritePointer(0);
      juce::int64 pos = playPos.load();
      const bool wrap = loop.load() && !isExporting;
      bool ended = false;
      for (int i = 0; i < n; ++i) {
        if (!ended && pos >= len) {
          if (wrap)
            pos = 0;
          else
            ended = true;
        }
        out[i] = ended ? 0.0f : src[pos++];
      }
      playPos.store(ended ? len : pos);
      for (int ch = 1; ch < nch; ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, n);
      if (ended) {
        playing.store(false);
        if (isExporting) {
          tailLeft.store(tailSamples.load());
          tailElapsed.store(0);
          quietFrames.store(0);
        }
      }
      return;
    }

    if (isExporting && exportStarted.load()) {
      // Playback finished: feed silence while the rig's tail rings out.
      buffer.clear();
      const juce::int64 left = tailLeft.load() - n;
      tailLeft.store(left);
      if (left <= 0) {
        captureWet.store(false);
        exportDone.store(true);
      }
    }
  }

  // End of processBlock: records the final output.
  void processOutput(const juce::AudioBuffer<float>& buffer) noexcept {
    if (!captureWet.load()) return;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0) return;
    const float* chans[2] = {buffer.getReadPointer(0), buffer.getReadPointer(nch > 1 ? 1 : 0)};
    wetWriter.push(chans, 2, n);

    // Export tail: stop early once the rig has gone quiet.
    if (exporting.load() && exportStarted.load() && !playing.load()) {
      float peak = 0.0f;
      for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
          peak = std::max(peak, std::abs(chans[c][i]));
      const juce::int64 elapsed = tailElapsed.fetch_add(n) + n;
      juce::int64 quiet = 0;
      if (peak < kQuietLinear)
        quiet = quietFrames.fetch_add(n) + n;
      else
        quietFrames.store(0);
      if (quiet >= static_cast<juce::int64>(sampleRate * kQuietHoldSeconds) &&
          elapsed >= static_cast<juce::int64>(sampleRate * kMinTailSeconds)) {
        captureWet.store(false);
        exportDone.store(true);
      }
    }
  }

  // True while an export is rendering: the processor then runs the rig several
  // times per device callback (faster than real time) and mutes the device.
  bool fastExportActive() const noexcept {
    return exporting.load() && exportStarted.load() && !exportDone.load();
  }

  // Name (without extension) of the take recorded last, "" if none.
  juce::String lastRecordedName() const { return lastRecorded; }

  // -------------------------------------------------------------- commands

  // One entry point for the UI. Returns the state after the command.
  juce::var handleCommand(const juce::String& cmd, const juce::var& arg) {
    error.clear();
    if (!timerStarted) {
      timerStarted = true;
      startTimerHz(10);
    }
    if (!scanned) scan();

    if (cmd == "record") {
      if (recording.load())
        stopRecording();
      else
        startRecording();
    } else if (cmd == "stop") {
      stopAll();
    } else if (cmd == "select") {
      selectTake(arg.toString());
    } else if (cmd == "play") {
      togglePlay();
    } else if (cmd == "loop") {
      loop.store(static_cast<bool>(arg));
    } else if (cmd == "recordAmp") {
      recordAmp = static_cast<bool>(arg);
    } else if (cmd == "seek") {
      seek(static_cast<double>(arg));
    } else if (cmd == "export") {
      startExport();
    } else if (cmd == "delete") {
      deleteTake(arg.toString());
    } else if (cmd == "refresh") {
      scan();
    } else if (cmd == "mp3") {
      makeMp3(arg.toString());
    }
    return getState();
  }

  juce::var getState() {
    if (!scanned) scan();
    auto* o = new juce::DynamicObject();
    const Take* t = current.load();
    const double length = (t != nullptr && sampleRate > 0.0) ? static_cast<double>(t->length) / sampleRate : 0.0;
    o->setProperty("recording", recording.load());
    o->setProperty("playing", playing.load());
    o->setProperty("exporting", exporting.load());
    o->setProperty("loop", loop.load());
    o->setProperty("recordAmp", recordAmp);
    o->setProperty("elapsed", static_cast<double>(recordedFrames.load()) / sampleRate);
    o->setProperty("position", static_cast<double>(playPos.load()) / sampleRate);
    o->setProperty("length", length);
    o->setProperty("selected", currentId);
    o->setProperty("dropped", static_cast<int>(dryWriter.droppedFrames() + wetWriter.droppedFrames()));
    o->setProperty("error", error);
    o->setProperty("message", message);
    o->setProperty("folder", directory().getFullPathName());
    o->setProperty("lastExport", lastExport);
    o->setProperty("mp3Path", lastMp3);
    o->setProperty("exportSerial", exportSerial);
    juce::Array<juce::var> list;
    for (const auto& info : takes) {
      auto* e = new juce::DynamicObject();
      e->setProperty("id", info.id);
      e->setProperty("seconds", info.seconds);
      e->setProperty("hasAmp", info.hasAmp);
      list.add(juce::var(e));
    }
    o->setProperty("takes", list);
    return juce::var(o);
  }

private:
  // Export tail: ends as soon as the output has been quiet for a moment (after a
  // minimum), or at the maximum, whichever comes first.
  static constexpr double kTailSeconds = 3.0;
  static constexpr double kMinTailSeconds = 0.3;
  static constexpr double kQuietHoldSeconds = 0.25;
  static constexpr float kQuietLinear = 0.001f;  // -60 dBFS
  static constexpr double kMaxTakeSeconds = 600.0;

  struct Take {
    juce::String id;
    juce::AudioBuffer<float> mono;
    juce::int64 length = 0;
  };
  struct TakeInfo {
    juce::String id;
    double seconds = 0.0;
    bool hasAmp = false;
  };

  // ------------------------------------------------------------ WAV writer
  class WavWriter : private juce::Thread {
  public:
    WavWriter() : juce::Thread("T3K wav writer"), fifo(kFifoFrames) {
      ring.assign(static_cast<size_t>(kFifoFrames) * 2, 0.0f);
      scratch.assign(static_cast<size_t>(kChunkFrames) * 2 * 3, 0);
    }
    ~WavWriter() override { close(); }

    bool open(const juce::File& file, int numChannels, int rate) {
      close();
      file.deleteFile();
      auto s = std::make_unique<juce::FileOutputStream>(file);
      if (s->failedToOpen()) return false;
      s->setPosition(0);
      s->truncate();
      stream = std::move(s);
      channels = juce::jlimit(1, 2, numChannels);
      sampleRateHz = rate;
      dataBytes = 0;
      dropped.store(0);
      writeHeader(0);
      fifo.reset();
      active.store(true);
      startThread();
      return true;
    }

    // Audio thread. Interleaves `n` frames of `numIn` source channels.
    void push(const float* const* ch, int numIn, int n) noexcept {
      if (!active.load()) return;
      if (fifo.getFreeSpace() < n) {
        dropped.fetch_add(n);
        return;
      }
      int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
      fifo.prepareToWrite(n, s1, n1, s2, n2);
      copyIn(ch, numIn, s1, n1, 0);
      copyIn(ch, numIn, s2, n2, n1);
      fifo.finishedWrite(n1 + n2);
    }

    void close() {
      if (stream == nullptr) return;
      active.store(false);
      signalThreadShouldExit();
      notify();
      stopThread(3000);
      while (drainSome() > 0) {
      }
      writeHeader(dataBytes);
      stream->flush();
      stream.reset();
    }

    bool isOpen() const { return stream != nullptr; }
    int droppedFrames() const { return dropped.load(); }

  private:
    static constexpr int kFifoFrames = 1 << 18;  // ~5.4 s at 48 kHz
    static constexpr int kChunkFrames = 8192;

    void run() override {
      while (!threadShouldExit()) {
        if (drainSome() == 0) wait(15);
      }
    }

    void copyIn(const float* const* ch, int numIn, int ringStart, int count, int srcOffset) noexcept {
      for (int i = 0; i < count; ++i)
        for (int c = 0; c < channels; ++c)
          ring[static_cast<size_t>(ringStart + i) * static_cast<size_t>(channels) + static_cast<size_t>(c)] =
              ch[juce::jmin(c, numIn - 1)][srcOffset + i];
    }

    int drainSome() {
      const int ready = fifo.getNumReady();
      if (ready <= 0 || stream == nullptr) return 0;
      const int toRead = juce::jmin(ready, kChunkFrames);
      int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
      fifo.prepareToRead(toRead, s1, n1, s2, n2);
      writeFrames(s1, n1);
      writeFrames(s2, n2);
      fifo.finishedRead(n1 + n2);
      return n1 + n2;
    }

    void writeFrames(int start, int count) {
      if (count <= 0) return;
      const size_t total = static_cast<size_t>(count) * static_cast<size_t>(channels);
      const float* src = ring.data() + static_cast<size_t>(start) * static_cast<size_t>(channels);
      uint8_t* p = scratch.data();
      for (size_t i = 0; i < total; ++i) {
        const int v = juce::roundToInt(juce::jlimit(-1.0f, 1.0f, src[i]) * 8388607.0f);
        *p++ = static_cast<uint8_t>(v & 0xFF);
        *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
        *p++ = static_cast<uint8_t>((v >> 16) & 0xFF);
      }
      stream->write(scratch.data(), total * 3);
      dataBytes += static_cast<juce::int64>(total) * 3;
    }

    void writeHeader(juce::int64 bytes) {
      const auto pos = stream->getPosition();
      stream->setPosition(0);
      stream->write("RIFF", 4);
      stream->writeInt(static_cast<int>(36 + bytes));
      stream->write("WAVE", 4);
      stream->write("fmt ", 4);
      stream->writeInt(16);
      stream->writeShort(1);  // PCM
      stream->writeShort(static_cast<short>(channels));
      stream->writeInt(sampleRateHz);
      stream->writeInt(sampleRateHz * channels * 3);
      stream->writeShort(static_cast<short>(channels * 3));
      stream->writeShort(24);
      stream->write("data", 4);
      stream->writeInt(static_cast<int>(bytes));
      if (pos > 44) stream->setPosition(pos);
    }

    juce::AbstractFifo fifo;
    std::vector<float> ring;
    std::vector<uint8_t> scratch;
    std::unique_ptr<juce::FileOutputStream> stream;
    std::atomic<bool> active{false};
    std::atomic<int> dropped{0};
    int channels = 1;
    int sampleRateHz = 48000;
    juce::int64 dataBytes = 0;
  };

  // --------------------------------------------------------- message thread

  juce::File directory() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Recordings");
  }

  static juce::String stamp() { return juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"); }

  void startRecording() {
    if (playing.load() || exporting.load()) {
      error = "Stop playback first.";
      return;
    }
    const auto dir = directory();
    if (!dir.createDirectory().wasOk()) {
      error = "Could not create the recordings folder.";
      return;
    }
    const juce::String name = "Take " + stamp();
    const int rate = juce::roundToInt(sampleRate);
    if (!dryWriter.open(dir.getChildFile(name + ".wav"), 1, rate)) {
      error = "Could not create the take file.";
      return;
    }
    if (recordAmp && !wetWriter.open(dir.getChildFile(name + " (amp).wav"), 2, rate)) {
      dryWriter.close();
      error = "Could not create the amp file.";
      return;
    }
    recordedFrames.store(0);
    lastRecorded = name;
    captureWet.store(recordAmp);
    recording.store(true);
    message = "Recording...";
  }

  void stopRecording() {
    recording.store(false);
    captureWet.store(false);
    dryWriter.close();
    wetWriter.close();
    scan();
    message = "Saved: " + lastRecorded;
    if (lastRecorded.isNotEmpty()) selectTake(lastRecorded);
    message = "Saved: " + lastRecorded;
  }

  void stopAll() {
    if (recording.load()) {
      stopRecording();
      return;
    }
    if (exporting.load()) {
      finishExport(true);
      return;
    }
    playing.store(false);
  }

  void togglePlay() {
    if (recording.load() || exporting.load()) return;
    if (current.load() == nullptr) {
      error = "Pick a take from the list.";
      return;
    }
    if (playing.load()) {
      playing.store(false);
    } else {
      if (playPos.load() >= current.load()->length) playPos.store(0);
      playing.store(true);
    }
  }

  void seek(double fraction) {
    const Take* t = current.load();
    if (t == nullptr) return;
    playPos.store(static_cast<juce::int64>(juce::jlimit(0.0, 1.0, fraction) * static_cast<double>(t->length)));
  }

  void selectTake(const juce::String& id) {
    if (recording.load() || exporting.load() || id.isEmpty()) return;
    playing.store(false);
    const auto file = directory().getChildFile(id + ".wav");
    if (!file.existsAsFile()) {
      error = "Take not found.";
      return;
    }
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0 || reader->sampleRate <= 0.0) {
      error = "Could not read the file.";
      return;
    }
    const juce::int64 maxIn = static_cast<juce::int64>(kMaxTakeSeconds * reader->sampleRate);
    const int inLen = static_cast<int>(juce::jmin(reader->lengthInSamples, maxIn));
    const int inCh = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> tmp(inCh, inLen);
    reader->read(&tmp, 0, inLen, 0, true, true);

    // Mono mix.
    std::vector<float> mono(static_cast<size_t>(inLen), 0.0f);
    for (int ch = 0; ch < inCh; ++ch) {
      const float* d = tmp.getReadPointer(ch);
      for (int i = 0; i < inLen; ++i)
        mono[static_cast<size_t>(i)] += d[i] / static_cast<float>(inCh);
    }

    // Linear resample to the engine rate when the file differs.
    const double ratio = reader->sampleRate / sampleRate;
    int outLen = inLen;
    if (std::abs(ratio - 1.0) > 1.0e-4) outLen = static_cast<int>(static_cast<double>(inLen) / ratio);
    auto take = std::make_unique<Take>();
    take->id = id;
    take->mono.setSize(1, juce::jmax(1, outLen));
    float* out = take->mono.getWritePointer(0);
    if (outLen == inLen) {
      for (int i = 0; i < outLen; ++i)
        out[i] = mono[static_cast<size_t>(i)];
    } else {
      for (int i = 0; i < outLen; ++i) {
        const double p = static_cast<double>(i) * ratio;
        const int i0 = juce::jmin(static_cast<int>(p), inLen - 1);
        const int i1 = juce::jmin(i0 + 1, inLen - 1);
        const float f = static_cast<float>(p - static_cast<double>(i0));
        out[i] = mono[static_cast<size_t>(i0)] * (1.0f - f) + mono[static_cast<size_t>(i1)] * f;
      }
    }
    take->length = outLen;
    playPos.store(0);
    current.store(take.get());
    currentId = id;
    owned.push_back(std::move(take));
    // Keep the last few takes alive: the audio thread may still be reading the
    // one it just let go of.
    while (owned.size() > 3)
      owned.erase(owned.begin());
    message = {};
  }

  void deleteTake(const juce::String& id) {
    if (recording.load() || exporting.load() || id.isEmpty()) return;
    if (id == currentId) {
      playing.store(false);
      current.store(nullptr);
      currentId = {};
      playPos.store(0);
    }
    directory().getChildFile(id + ".wav").deleteFile();
    directory().getChildFile(id + " (amp).wav").deleteFile();
    directory().getChildFile(id + ".mp3").deleteFile();
    directory().getChildFile(id + " (amp).mp3").deleteFile();
    scan();
  }

  // Encodes a WAV from the recordings folder to an MP3 next to it (reused when
  // already up to date). The result path is reported as "mp3Path" in the state.
  void makeMp3(const juce::String& wavPath) {
    lastMp3 = {};
    const juce::File wav(wavPath);
    if (!wav.existsAsFile()) {
      error = "File not found.";
      return;
    }
    const juce::File mp3 = wav.withFileExtension("mp3");
    if (!mp3.existsAsFile() || mp3.getLastModificationTime() < wav.getLastModificationTime()) {
      const juce::String err = encodeWavToMp3(wav, mp3);
      if (err.isNotEmpty()) {
        error = err;
        return;
      }
    }
    lastMp3 = mp3.getFullPathName();
    message = "MP3 ready: " + mp3.getFileName();
  }

  void startExport() {
    if (recording.load() || exporting.load()) return;
    const Take* t = current.load();
    if (t == nullptr) {
      error = "Pick a take from the list.";
      return;
    }
    const auto dir = directory();
    exportName = currentId + " (export " + stamp() + ")";
    if (!wetWriter.open(dir.getChildFile(exportName + ".wav"), 2, juce::roundToInt(sampleRate))) {
      error = "Could not create the export file.";
      return;
    }
    playing.store(false);
    loop.store(false);
    playPos.store(0);
    exportDone.store(false);
    tailElapsed.store(0);
    quietFrames.store(0);
    exportStarted.store(true);
    tailLeft.store(tailSamples.load());
    exporting.store(true);
    captureWet.store(true);
    playing.store(true);
    message = "Exporting...";
  }

  void finishExport(bool cancelled) {
    captureWet.store(false);
    playing.store(false);
    exporting.store(false);
    exportStarted.store(false);
    exportDone.store(false);
    wetWriter.close();
    scan();
    message = cancelled ? "Export cancelled: " + exportName : "Exported: " + exportName;
    if (!cancelled) {
      lastExport = exportName;
      ++exportSerial;
    }
  }

  void scan() {
    scanned = true;
    takes.clear();
    const auto dir = directory();
    if (!dir.isDirectory()) return;
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.wav");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
      return a.getLastModificationTime() > b.getLastModificationTime();
    });
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& f : files) {
      const auto name = f.getFileNameWithoutExtension();
      if (name.endsWith(" (amp)") || name.contains(" (export ") || name.startsWith("Mix ")) continue;
      TakeInfo info;
      info.id = name;
      info.hasAmp = dir.getChildFile(name + " (amp).wav").existsAsFile();
      std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
      if (reader && reader->sampleRate > 0.0)
        info.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      takes.push_back(info);
    }
  }

  void timerCallback() override {
    if (exporting.load() && exportDone.load()) finishExport(false);
  }

  // ---------------------------------------------------------------- state
  double sampleRate = 48000.0;
  std::atomic<juce::int64> tailSamples{96000};

  std::atomic<bool> recording{false}, playing{false}, loop{false};
  std::atomic<bool> exporting{false}, exportStarted{false}, exportDone{false}, captureWet{false};
  std::atomic<juce::int64> playPos{0}, tailLeft{0}, recordedFrames{0}, tailElapsed{0}, quietFrames{0};
  std::atomic<const Take*> current{nullptr};

  WavWriter dryWriter, wetWriter;
  std::vector<std::unique_ptr<Take>> owned;
  std::vector<TakeInfo> takes;
  juce::String currentId, lastRecorded, exportName, error, message;
  juce::String lastExport, lastMp3;
  int exportSerial = 0;
  bool recordAmp = true;
  bool scanned = false;
  bool timerStarted = false;
};

}  // namespace t3k
