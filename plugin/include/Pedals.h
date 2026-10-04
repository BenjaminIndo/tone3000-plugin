// Pedalboard effects: compressor (before the amp chain), delay and reverb
// (after it). Header-only, plain DSP, no allocation while processing.
//
// Every pedal fades in and out over ~10 ms when switched (no clicks). Delay
// and reverb keep running with a silent input for a while after switch-off so
// their tails ring out ("spillover") instead of being chopped, then go idle.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace t3k {

// ------------------------------------------------------------------ helpers

namespace pedal_detail {
constexpr double kFadeSeconds = 0.010;

inline float fadeStep(double sampleRate) { return static_cast<float>(1.0 / (kFadeSeconds * sampleRate)); }
}  // namespace pedal_detail

// --------------------------------------------------------------- compressor

class PedalCompressor {
public:
  struct Params {
    bool enabled = false;
    float thresholdDb = -20.0f;
    float ratio = 4.0f;
    float attackMs = 10.0f;
    float releaseMs = 200.0f;
    float makeupDb = 0.0f;
    float mix = 1.0f;  // parallel compression: 1 = fully compressed
  };

  void prepare(double newSampleRate) {
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    envDb = 0.0f;
    weight = 0.0f;
  }

  void process(juce::AudioBuffer<float>& buffer, const Params& p) {
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    if (n <= 0 || nch <= 0) return;
    const float targetWeight = p.enabled ? 1.0f : 0.0f;
    if (weight <= 0.0f && targetWeight <= 0.0f) {
      envDb = 0.0f;
      return;
    }

    const float sr = static_cast<float>(sampleRate);
    const float step = pedal_detail::fadeStep(sampleRate);
    const float atk = std::exp(-1.0f / (0.001f * std::max(0.1f, p.attackMs) * sr));
    const float rel = std::exp(-1.0f / (0.001f * std::max(1.0f, p.releaseMs) * sr));
    const float makeup = std::pow(10.0f, p.makeupDb * 0.05f);
    const float slope = 1.0f - 1.0f / std::max(1.0f, p.ratio);
    constexpr float kKneeDb = 6.0f;
    const float mix = juce::jlimit(0.0f, 1.0f, p.mix);

    for (int i = 0; i < n; ++i) {
      float peak = 0.0f;
      for (int c = 0; c < nch; ++c)
        peak = std::max(peak, std::abs(buffer.getReadPointer(c)[i]));
      const float levelDb = peak > 1.0e-6f ? 20.0f * std::log10(peak) : -120.0f;
      const float over = levelDb - p.thresholdDb;

      float grTarget = 0.0f;  // gain reduction in dB (>= 0)
      if (2.0f * over > kKneeDb)
        grTarget = slope * over;
      else if (2.0f * over > -kKneeDb) {
        const float t = over + kKneeDb * 0.5f;
        grTarget = slope * t * t / (2.0f * kKneeDb);
      }
      envDb = grTarget > envDb ? atk * envDb + (1.0f - atk) * grTarget : rel * envDb + (1.0f - rel) * grTarget;

      const float gain = std::pow(10.0f, -envDb * 0.05f) * makeup;
      weight += juce::jlimit(-step, step, targetWeight - weight);
      const float wet = mix * weight;
      for (int c = 0; c < nch; ++c) {
        float* d = buffer.getWritePointer(c);
        d[i] = d[i] * (1.0f - wet) + d[i] * gain * wet;
      }
    }
  }

private:
  double sampleRate = 48000.0;
  float envDb = 0.0f;
  float weight = 0.0f;
};

// -------------------------------------------------------------------- delay

class PedalDelay {
public:
  struct Params {
    bool enabled = false;
    float timeMs = 380.0f;
    float feedback = 0.35f;
    float toneHz = 5000.0f;  // low-pass in the feedback path
    float mix = 0.3f;
  };

  static constexpr float kMaxTimeMs = 1000.0f;

  void prepare(double newSampleRate) {
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    size = static_cast<int>(sampleRate * (kMaxTimeMs * 0.001 + 0.05)) + 8;
    for (auto& b : line)
      b.assign(static_cast<size_t>(size), 0.0f);
    clear();
  }

  void process(juce::AudioBuffer<float>& buffer, const Params& p) {
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin(2, buffer.getNumChannels());
    if (n <= 0 || nch <= 0 || size <= 0) return;

    if (p.enabled) {
      tailLeft = static_cast<long long>(sampleRate * 8.0);
      active = true;
    } else if (tailLeft > 0) {
      tailLeft -= n;
    }
    if (!active) return;
    if (!p.enabled && tailLeft <= 0) {
      clear();
      return;
    }

    const float sr = static_cast<float>(sampleRate);
    const float targetDelay = juce::jlimit(1.0f, static_cast<float>(size - 2), p.timeMs * 0.001f * sr);
    if (delaySmoothed < 0.0f) delaySmoothed = targetDelay;
    const float lpCoef = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * juce::jlimit(200.0f, 16000.0f, p.toneHz) / sr);
    const float fb = juce::jlimit(0.0f, 0.95f, p.feedback);
    const float mix = juce::jlimit(0.0f, 1.0f, p.mix);
    const float inGain = p.enabled ? 1.0f : 0.0f;

    for (int i = 0; i < n; ++i) {
      delaySmoothed += (targetDelay - delaySmoothed) * 0.0004f;
      float rp = static_cast<float>(writePos) - delaySmoothed;
      if (rp < 0.0f) rp += static_cast<float>(size);
      const int i0 = juce::jlimit(0, size - 1, static_cast<int>(rp));
      const int i1 = (i0 + 1) % size;
      const float frac = rp - static_cast<float>(i0);

      for (int c = 0; c < nch; ++c) {
        float* d = buffer.getWritePointer(c);
        auto& b = line[static_cast<size_t>(c)];
        const float delayed = b[static_cast<size_t>(i0)] * (1.0f - frac) + b[static_cast<size_t>(i1)] * frac;
        lp[c] += lpCoef * (delayed - lp[c]);
        b[static_cast<size_t>(writePos)] = d[i] * inGain + lp[c] * fb;
        d[i] += delayed * mix;
      }
      writePos = (writePos + 1) % size;
    }
  }

private:
  void clear() {
    for (auto& b : line)
      std::fill(b.begin(), b.end(), 0.0f);
    writePos = 0;
    lp[0] = lp[1] = 0.0f;
    delaySmoothed = -1.0f;
    tailLeft = 0;
    active = false;
  }

  double sampleRate = 48000.0;
  std::vector<float> line[2];
  int size = 0;
  int writePos = 0;
  float lp[2] = {0.0f, 0.0f};
  float delaySmoothed = -1.0f;
  long long tailLeft = 0;
  bool active = false;
};

// ------------------------------------------------------------------- reverb

class PedalReverb {
public:
  struct Params {
    bool enabled = false;
    float size = 0.5f;
    float damp = 0.5f;
    float mix = 0.25f;
  };

  void prepare(double newSampleRate) {
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    reverb.setSampleRate(sampleRate);
    reverb.reset();
    tailLeft = 0;
    active = false;
  }

  void process(juce::AudioBuffer<float>& buffer, const Params& p) {
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin(2, buffer.getNumChannels());
    if (n <= 0 || nch <= 0) return;

    if (p.enabled) {
      tailLeft = static_cast<long long>(sampleRate * 10.0);
      active = true;
    } else if (tailLeft > 0) {
      tailLeft -= n;
    }
    if (!active) return;
    if (!p.enabled && tailLeft <= 0) {
      reverb.reset();
      active = false;
      return;
    }

    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit(0.0f, 1.0f, p.size);
    rp.damping = juce::jlimit(0.0f, 1.0f, p.damp);
    rp.wetLevel = juce::jlimit(0.0f, 1.0f, p.mix);
    rp.dryLevel = 0.0f;  // wet only: it is added to the untouched dry below
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters(rp);

    const float inGain = p.enabled ? 1.0f : 0.0f;
    for (int start = 0; start < n; start += kChunk) {
      const int len = juce::jmin(kChunk, n - start);
      for (int i = 0; i < len; ++i) {
        const float l = buffer.getReadPointer(0)[start + i] * inGain;
        scratchL[i] = l;
        scratchR[i] = nch > 1 ? buffer.getReadPointer(1)[start + i] * inGain : l;
      }
      reverb.processStereo(scratchL, scratchR, len);
      float* outL = buffer.getWritePointer(0);
      float* outR = nch > 1 ? buffer.getWritePointer(1) : nullptr;
      for (int i = 0; i < len; ++i) {
        outL[start + i] += scratchL[i];
        if (outR != nullptr) outR[start + i] += scratchR[i];
      }
    }
  }

private:
  static constexpr int kChunk = 512;

  double sampleRate = 48000.0;
  juce::Reverb reverb;
  float scratchL[kChunk] = {};
  float scratchR[kChunk] = {};
  long long tailLeft = 0;
  bool active = false;
};

}  // namespace t3k
