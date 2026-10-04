// Multitrack takeover: six tracks that play together, record a new one on top,
// per-track volume / mute / solo / latency nudge, and a mixdown to WAV or MP3.
// Stock JUCE widgets, driven by Backend::getMultitrackState / multitrackCommand.
// Replaces the whole meters + chain band, like the recordings screen.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class MultitrackView : public juce::Component, private juce::Timer {
public:
  static constexpr int kCloseTop = 16, kCloseRight = 20, kCloseBox = 28, kCloseGlyph = 20;

  explicit MultitrackView(Services& services);
  ~MultitrackView() override;

  std::function<void()> onClose;
  std::function<void()> onBack;  // back to the recordings screen

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  struct Strip {
    juce::Label name;
    juce::TextButton rec, mute, solo, clear;
    juce::Slider volume, nudge;
  };

  void timerCallback() override;
  void apply(const juce::var& state);
  void command(const juce::String& cmd, const juce::var& arg = {});
  void trackCommand(const juce::String& cmd, int track, const juce::var& value);
  void shareFile(const juce::File& file);

  Services& services_;
  IconButton close_{Icon::X, kCloseBox, kCloseGlyph};
  juce::TextButton back_;
  juce::Label title_, status_, time_;
  std::vector<std::unique_ptr<Strip>> strips_;
  juce::TextButton play_, rewind_, loop_, mix_, shareMix_;
  juce::ToggleButton mp3_;
  juce::Slider position_;
  juce::ScopedMessageBox shareBox_;

  juce::String lastMixPath_;
  bool lastPlaying_ = false;
  int lastRecordingTrack_ = -1;
  int lastSerial_ = -1;
  bool updating_ = false;
};

}  // namespace t3k::ui
