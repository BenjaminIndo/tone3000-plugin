// Pedalboard takeover: compressor (before the amp), delay and reverb (after
// it), each with an on/off stomp and its knobs. Stock JUCE widgets bound
// straight to the processor's parameters through Backend::parameter(); the
// knobs follow outside changes (presets, MIDI) on a poll. Replaces the whole
// meters + chain band, like the tuner.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class PedalsView : public juce::Component, private juce::Timer {
public:
  static constexpr int kCloseTop = 16, kCloseRight = 20, kCloseBox = 28, kCloseGlyph = 20;

  explicit PedalsView(Services& services);
  ~PedalsView() override;

  std::function<void()> onClose;

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class Knob;
  class Stomp;
  struct Panel {
    juce::String title;
    std::unique_ptr<Stomp> stomp;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::Rectangle<int> bounds;
  };

  void timerCallback() override;

  Services& services_;
  IconButton close_{Icon::X, kCloseBox, kCloseGlyph};
  juce::Label title_;
  std::vector<Panel> panels_;
};

}  // namespace t3k::ui
