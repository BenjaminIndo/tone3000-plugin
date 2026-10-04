// WAV -> MP3 with the vendored shine encoder (third_party/shine, LGPL-2.0).
// Constant bitrate, joint stereo (mono stays mono). Synchronous: meant for
// the short takes this app records, called from the message thread.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include "../third_party/shine/layer3.h"
}

namespace t3k {

// Returns an empty string on success, otherwise a short user-facing error.
inline juce::String encodeWavToMp3(const juce::File& wav, const juce::File& mp3, int bitrateKbps = 192) {
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
  if (!reader || reader->lengthInSamples <= 0 || reader->numChannels <= 0) return "Could not read the WAV file.";

  const int rate = static_cast<int>(reader->sampleRate + 0.5);
  const int readerChannels = static_cast<int>(reader->numChannels);
  const int outChannels = readerChannels >= 2 ? 2 : 1;
  if (shine_check_config(rate, bitrateKbps) < 0) return "MP3 does not support this sample rate.";

  shine_config_t config;
  shine_set_config_mpeg_defaults(&config.mpeg);
  config.wave.channels = outChannels == 2 ? PCM_STEREO : PCM_MONO;
  config.wave.samplerate = rate;
  config.mpeg.bitr = bitrateKbps;
  config.mpeg.mode = outChannels == 2 ? JOINT_STEREO : MONO;

  shine_t encoder = shine_initialise(&config);
  if (encoder == nullptr) return "Could not start the MP3 encoder.";

  mp3.deleteFile();
  juce::FileOutputStream out(mp3);
  if (out.failedToOpen()) {
    shine_close(encoder);
    return "Could not create the MP3 file.";
  }

  const int perPass = shine_samples_per_pass(encoder);  // frames per channel
  juce::AudioBuffer<float> chunk(readerChannels, perPass);
  std::vector<int16_t> pcm(static_cast<size_t>(perPass) * static_cast<size_t>(outChannels), 0);

  auto toPcm = [](float v) { return static_cast<int16_t>(juce::roundToInt(juce::jlimit(-1.0f, 1.0f, v) * 32767.0f)); };

  for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += perPass) {
    const int count = static_cast<int>(juce::jmin<juce::int64>(perPass, reader->lengthInSamples - pos));
    chunk.clear();
    reader->read(&chunk, 0, count, pos, true, true);
    std::fill(pcm.begin(), pcm.end(), static_cast<int16_t>(0));
    for (int i = 0; i < count; ++i) {
      const float l = chunk.getSample(0, i);
      const float r = readerChannels >= 2 ? chunk.getSample(1, i) : l;
      if (outChannels == 2) {
        pcm[static_cast<size_t>(i) * 2] = toPcm(l);
        pcm[static_cast<size_t>(i) * 2 + 1] = toPcm(r);
      } else {
        pcm[static_cast<size_t>(i)] = toPcm(l);
      }
    }
    int written = 0;
    const unsigned char* data = shine_encode_buffer_interleaved(encoder, pcm.data(), &written);
    if (data != nullptr && written > 0) out.write(data, static_cast<size_t>(written));
  }

  int written = 0;
  const unsigned char* tail = shine_flush(encoder, &written);
  if (tail != nullptr && written > 0) out.write(tail, static_cast<size_t>(written));
  out.flush();
  shine_close(encoder);
  return {};
}

}  // namespace t3k
