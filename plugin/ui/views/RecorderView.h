// Recordings takeover: record the clean guitar (plus the amped signal), replay
// a take through the current rig so the amp can be changed afterwards, and
// export a take through the rig to a WAV. Built from stock JUCE widgets on
// purpose; it is driven entirely by Backend::getRecorderState / recorderCommand.
// Replaces the whole meters + chain band, like the tuner.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "services/Services.h"
#include "widgets/IconButton.h"

namespace t3k::ui {

class RecorderView : public juce::Component, private juce::Timer, private juce::ListBoxModel {
public:
  static constexpr int kCloseTop = 16, kCloseRight = 20, kCloseBox = 28, kCloseGlyph = 20;

  explicit RecorderView(Services& services);
  ~RecorderView() override;

  std::function<void()> onClose;
  std::function<void()> onOpenTracks;  // switch to the multitrack screen

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  struct Row {
    juce::String id;
    double seconds = 0.0;
    bool hasAmp = false;
  };

  void timerCallback() override;
  void apply(const juce::var& state);
  void command(const juce::String& cmd, const juce::var& arg = {});
  // Opens the iOS share sheet for a WAV (reveals it in the file browser on desktop).
  void shareFile(const juce::File& file);

  // ListBoxModel
  int getNumRows() override;
  void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
  void selectedRowsChanged(int lastRowSelected) override;

  Services& services_;
  IconButton close_{Icon::X, kCloseBox, kCloseGlyph};
  juce::Label title_, status_, listTitle_, time_, folder_, shareTitle_;
  juce::TextButton shareClean_, shareAmp_, shareExport_, tracks_;
  juce::TextButton record_, play_, loop_, export_, delete_;
  juce::ToggleButton recordAmp_, mp3_;
  juce::ListBox list_;
  juce::Slider position_;

  std::vector<Row> rows_;
  juce::String selectedId_, folderPath_, lastExport_;
  bool selectedHasAmp_ = false;
  int lastSerial_ = -1;
  bool updating_ = false;
};

}  // namespace t3k::ui
