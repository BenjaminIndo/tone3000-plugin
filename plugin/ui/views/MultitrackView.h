// Multitrack takeover: six tracks that play together, a metronome with count-in,
// audio import, per-track waveform / trim / start / pan, loop and punch marks,
// record-on-top, and a mixdown to WAV or MP3. Stock JUCE widgets plus one small
// waveform component, driven by Backend::getMultitrackState / multitrackCommand.
// Replaces the whole meters + chain band, like the recordings screen.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
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
  class Waveform;
  class Content;
  struct Strip;
  struct EditControl {
    juce::Label caption;
    juce::Slider slider;
  };

  void timerCallback() override;
  void apply(const juce::var& state);
  void command(const juce::String& cmd, const juce::var& arg = {});
  void trackCommand(const juce::String& cmd, int track, const juce::var& value);
  void selectTrack(int track);
  void startImport(int track);
  void finishImport(int track, const juce::URL& url);
  void tapTempo();
  void shareFile(const juce::File& file);

  Services& services_;
  IconButton close_{Icon::X, kCloseBox, kCloseGlyph};
  juce::TextButton back_;
  juce::Label title_, status_, time_;

  // Transport and loop / punch marks.
  juce::TextButton play_, rewind_, loop_, loopIn_, loopOut_, punch_, punchIn_, punchOut_;
  juce::Slider position_;

  // Metronome, mixdown.
  juce::TextButton click_, tap_, mix_, shareMix_;
  juce::Slider bpm_, clickVol_;
  juce::ComboBox sig_, countIn_;
  juce::ToggleButton mp3_;

  // Tracks and the editor of the selected one.
  juce::Viewport viewport_;
  std::unique_ptr<Content> content_;
  std::vector<std::unique_ptr<Strip>> strips_;
  std::array<EditControl, 5> edit_;
  int selected_ = 0;

  std::unique_ptr<juce::FileChooser> chooser_;
  std::vector<double> taps_;

  juce::String lastMixPath_;
  juce::String localError_;      // import problems found on the UI side
  juce::uint32 localErrorUntil_ = 0;
  bool lastPlaying_ = false;
  int lastRecordingTrack_ = -1;
  int lastSerial_ = -1;
  double timelineSec_ = 8.0;
  double lastTimelineSec_ = -1.0;
  juce::int64 editKey_ = -1;
  bool updating_ = false;
};

}  // namespace t3k::ui
