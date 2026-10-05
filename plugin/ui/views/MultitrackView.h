// Studio: a full-window, GarageBand-style tracks view. A control bar with
// transport icons and an LCD (bar.beat, time, tempo), track headers with
// colour, mute / solo, volume and a meter on the left, and on the right a
// timeline with a bar ruler, loop / punch bands, coloured regions showing their
// waveform, and a playhead. Regions are moved and trimmed by dragging them;
// the view scrolls by dragging the background and zooms with a pinch or the
// +/- buttons. Song, track and mix settings open in floating panels.
//
// Driven entirely by Backend::getMultitrackState / multitrackCommand.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "services/Services.h"

namespace t3k::ui {

class MultitrackView : public juce::Component, private juce::Timer {
public:
  explicit MultitrackView(Services& services);
  ~MultitrackView() override;

  std::function<void()> onClose;  // back to the amp
  std::function<void()> onBack;   // to the recordings (takes) screen

  void paint(juce::Graphics& g) override;
  void resized() override;

private:
  class GlyphButton;
  class Lcd;
  class Headers;
  class Timeline;
  class Scrim;
  class Panel;
  class SongPanel;
  class TrackPanel;
  class MixPanel;

  static constexpr int kTracks = 6;  // matches t3k::Multitrack::kTracks

  struct TrackModel {
    bool loaded = false, imported = false, mute = false, solo = false;
    juce::String label;
    double fileSec = 0.0, start = 0.0, trimStart = 0.0, trimEnd = 0.0, nudge = 0.0, pan = 0.0, volume = 1.0;
    float meter = 0.0f;  // smoothed for display
    int rev = -1;
    bool needPeaks = false;
    std::vector<float> peaks;  // min / max pairs over the whole file
  };
  struct Model {
    bool playing = false, recording = false, loopOn = false, punchOn = false, metro = false;
    int recTrack = -1, beats = 4, denom = 4, countIn = 0, mixSerial = 0;
    double position = 0.0, length = 0.0, loopIn = 0.0, loopOut = 0.0, punchIn = 0.0, punchOut = 0.0;
    double bpm = 120.0, clickVol = 0.5;
    float liveMeter = 0.0f;
    juce::String error, message, mixPath;
    TrackModel tracks[kTracks];
  };

  void timerCallback() override;
  void apply(const juce::var& state);
  void command(const juce::String& cmd, const juce::var& arg = {});
  void trackCommand(const juce::String& cmd, int track, const juce::var& value);

  void selectTrack(int track);
  void togglePlay();
  void toggleRecord();
  void commitRegion(int track, double start, double trimStart, double trimEnd);
  void commitBand(bool loop, double in, double out);
  void startImport(int track);
  void finishImport(int track, const juce::URL& url);
  void tapTempo();
  void shareFile(const juce::File& file);

  void openSongPanel();
  void openTrackPanel(int track);
  void openMixPanel();
  void showPanel(std::unique_ptr<Panel> panel, juce::Rectangle<int> bounds);
  void closePanel();
  void closePanelSoon();

  double beatSeconds() const;
  juce::String statusText(juce::Colour& colour) const;
  void showLocalError(const juce::String& text);

  Services& services_;
  Model model_;
  int selected_ = 0;
  bool snap_ = true;

  std::unique_ptr<GlyphButton> amp_, takes_, rewind_, play_, record_, loop_, metro_, countIn_, zoomOut_, zoomIn_, mix_;
  std::unique_ptr<Lcd> lcd_;
  std::unique_ptr<Headers> headers_;
  std::unique_ptr<Timeline> timeline_;
  std::unique_ptr<Scrim> scrim_;
  std::unique_ptr<Panel> panel_;

  std::unique_ptr<juce::FileChooser> chooser_;
  std::vector<double> taps_;
  juce::String localError_, lastMessage_;
  juce::uint32 localErrorUntil_ = 0, messageUntil_ = 0;
  int lastSerial_ = -1;
};

}  // namespace t3k::ui
