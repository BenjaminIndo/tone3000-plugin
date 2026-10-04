#include "MultitrackView.h"

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPad = 24;
constexpr int kTracks = 6;  // matches t3k::Multitrack::kTracks
constexpr int kRowH = 38;
constexpr int kRowGap = 2;
constexpr int kPollHz = 12;

juce::String mmss(double seconds) {
  const int total = juce::jmax(0, static_cast<int>(seconds));
  return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

void styleButton(juce::TextButton& b, const juce::String& text) {
  b.setButtonText(text);
  b.setColour(juce::TextButton::buttonColourId, theme::kSurfaceRaised);
  b.setColour(juce::TextButton::buttonOnColourId, theme::kBrandBlue);
  b.setColour(juce::TextButton::textColourOffId, theme::kWhite);
  b.setColour(juce::TextButton::textColourOnId, theme::kWhite);
  b.setMouseClickGrabsKeyboardFocus(false);
}

void styleSlider(juce::Slider& s, double min, double max, double step, double value, const juce::String& suffix, int decimals) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
  s.setRange(min, max, step);
  s.setValue(value, juce::dontSendNotification);
  s.setTextValueSuffix(suffix);
  s.setNumDecimalPlacesToDisplay(decimals);
  s.setDoubleClickReturnValue(true, value);
  s.setColour(juce::Slider::thumbColourId, theme::kWhite);
  s.setColour(juce::Slider::trackColourId, theme::kBrandBlue);
  s.setColour(juce::Slider::backgroundColourId, theme::kSurfaceRaised);
  s.setColour(juce::Slider::textBoxTextColourId, theme::kWhite);
  s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  s.setMouseClickGrabsKeyboardFocus(false);
}
}  // namespace

MultitrackView::MultitrackView(Services& services) : services_(services) {
  setOpaque(true);

  close_.setName("Close tracks");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(close_);

  title_.setText("TRACKS", juce::dontSendNotification);
  title_.setFont(Fonts::sans(20.0f, true));
  title_.setColour(juce::Label::textColourId, theme::kWhite);
  title_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(title_);

  styleButton(back_, "< Recordings");
  back_.onClick = [this] {
    if (onBack) onBack();
  };
  addAndMakeVisible(back_);

  status_.setText("Ready", juce::dontSendNotification);
  status_.setFont(Fonts::sans(15.0f, false));
  status_.setColour(juce::Label::textColourId, theme::kWhite);
  status_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(status_);

  for (int t = 0; t < kTracks; ++t) {
    auto s = std::make_unique<Strip>();
    s->name.setText("Track " + juce::String(t + 1), juce::dontSendNotification);
    s->name.setFont(Fonts::sans(14.0f, true));
    s->name.setColour(juce::Label::textColourId, theme::kWhite);
    s->name.setInterceptsMouseClicks(false, false);

    styleButton(s->rec, "REC");
    s->rec.onClick = [this, t] {
      if (lastRecordingTrack_ == t)
        command("stop", {});
      else
        trackCommand("record", t, {});
    };
    styleButton(s->mute, "M");
    s->mute.setClickingTogglesState(true);
    s->mute.onClick = [this, t] { trackCommand("mute", t, strips_[static_cast<size_t>(t)]->mute.getToggleState()); };
    styleButton(s->solo, "S");
    s->solo.setClickingTogglesState(true);
    s->solo.onClick = [this, t] { trackCommand("solo", t, strips_[static_cast<size_t>(t)]->solo.getToggleState()); };
    styleButton(s->clear, "Clear");
    s->clear.onClick = [this, t] { trackCommand("clear", t, {}); };

    styleSlider(s->volume, 0.0, 1.5, 0.01, 1.0, "", 2);
    s->volume.onValueChange = [this, t] {
      if (!updating_) trackCommand("volume", t, strips_[static_cast<size_t>(t)]->volume.getValue());
    };
    styleSlider(s->nudge, -300.0, 300.0, 1.0, 0.0, " ms", 0);
    s->nudge.onValueChange = [this, t] {
      if (!updating_) trackCommand("nudge", t, strips_[static_cast<size_t>(t)]->nudge.getValue());
    };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&s->name, &s->rec, &s->mute, &s->solo, &s->clear, &s->volume, &s->nudge})
      addAndMakeVisible(c);
    strips_.push_back(std::move(s));
  }

  styleButton(play_, "Play");
  play_.onClick = [this] { command(lastPlaying_ ? "stop" : "play", {}); };
  addAndMakeVisible(play_);
  styleButton(rewind_, "|<");
  rewind_.onClick = [this] { command("rewind", {}); };
  addAndMakeVisible(rewind_);
  styleButton(loop_, "Loop");
  loop_.setClickingTogglesState(true);
  loop_.onClick = [this] { command("loop", loop_.getToggleState()); };
  addAndMakeVisible(loop_);

  position_.setSliderStyle(juce::Slider::LinearHorizontal);
  position_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  position_.setRange(0.0, 1.0, 0.0);
  position_.setColour(juce::Slider::thumbColourId, theme::kWhite);
  position_.setColour(juce::Slider::trackColourId, theme::kBrandBlue);
  position_.setColour(juce::Slider::backgroundColourId, theme::kSurfaceRaised);
  position_.setMouseClickGrabsKeyboardFocus(false);
  position_.onValueChange = [this] {
    if (!updating_) command("seek", position_.getValue());
  };
  addAndMakeVisible(position_);

  time_.setText("0:00 / 0:00", juce::dontSendNotification);
  time_.setFont(Fonts::sans(14.0f, false));
  time_.setColour(juce::Label::textColourId, theme::kGray);
  time_.setJustificationType(juce::Justification::centredRight);
  time_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(time_);

  styleButton(mix_, "Mix down");
  mix_.onClick = [this] { command("mix", mp3_.getToggleState()); };
  addAndMakeVisible(mix_);
  mp3_.setButtonText("as MP3");
  mp3_.setColour(juce::ToggleButton::textColourId, theme::kWhite);
  mp3_.setColour(juce::ToggleButton::tickColourId, theme::kWhite);
  mp3_.setMouseClickGrabsKeyboardFocus(false);
  addAndMakeVisible(mp3_);
  styleButton(shareMix_, "Share last mix");
  shareMix_.setEnabled(false);
  shareMix_.onClick = [this] {
    if (lastMixPath_.isNotEmpty()) shareFile(juce::File(lastMixPath_));
  };
  addAndMakeVisible(shareMix_);

  command("rewind", {});
  startTimerHz(kPollHz);
}

MultitrackView::~MultitrackView() { stopTimer(); }

void MultitrackView::command(const juce::String& cmd, const juce::var& arg) {
  apply(services_.backend.multitrackCommand(cmd, arg));
}

void MultitrackView::trackCommand(const juce::String& cmd, int track, const juce::var& value) {
  auto* o = new juce::DynamicObject();
  o->setProperty("track", track);
  o->setProperty("value", value);
  command(cmd, juce::var(o));
}

void MultitrackView::timerCallback() { apply(services_.backend.getMultitrackState()); }

void MultitrackView::shareFile(const juce::File& file) {
  if (!file.existsAsFile()) return;
#if JUCE_IOS
  juce::Array<juce::URL> urls;
  urls.add(juce::URL(file));
  shareBox_ = juce::ContentSharer::shareFilesScoped(urls, [](bool, const juce::String&) {}, this);
#else
  file.revealToUser();
#endif
}

void MultitrackView::apply(const juce::var& state) {
  if (!state.isObject()) return;
  const bool playing = static_cast<bool>(state.getProperty("playing", false));
  const bool recording = static_cast<bool>(state.getProperty("recording", false));
  const int recTrack = static_cast<int>(state.getProperty("recordingTrack", -1));
  const double position = static_cast<double>(state.getProperty("position", 0.0));
  const double length = static_cast<double>(state.getProperty("length", 0.0));
  const juce::String error = state.getProperty("error", {}).toString();
  const juce::String message = state.getProperty("message", {}).toString();
  lastPlaying_ = playing;
  lastRecordingTrack_ = recording ? recTrack : -1;

  juce::String text;
  juce::Colour colour = theme::kWhite;
  if (error.isNotEmpty()) {
    text = error;
    colour = theme::kBrandRed;
  } else if (recording) {
    text = "REC track " + juce::String(recTrack + 1) + "   " + mmss(position);
    colour = theme::kBrandRed;
  } else if (playing) {
    text = "Playing";
  } else if (message.isNotEmpty()) {
    text = message;
  } else {
    text = "Ready";
  }
  if (status_.getText() != text) status_.setText(text, juce::dontSendNotification);
  status_.setColour(juce::Label::textColourId, colour);

  updating_ = true;
  const juce::Array<juce::var>* tracks = state.getProperty("tracks", {}).getArray();
  for (int t = 0; t < kTracks; ++t) {
    auto& s = *strips_[static_cast<size_t>(t)];
    const bool loaded = tracks != nullptr && t < tracks->size() && static_cast<bool>((*tracks)[t].getProperty("loaded", false));
    const double seconds = loaded ? static_cast<double>((*tracks)[t].getProperty("seconds", 0.0)) : 0.0;
    const juce::String label = "Track " + juce::String(t + 1) + (loaded ? "  " + mmss(seconds) : "  (empty)");
    if (s.name.getText() != label) s.name.setText(label, juce::dontSendNotification);

    const bool thisRecording = recording && recTrack == t;
    s.rec.setButtonText(thisRecording ? "STOP" : "REC");
    s.rec.setColour(juce::TextButton::buttonColourId, thisRecording ? theme::kBrandRed : theme::kSurfaceRaised);
    s.rec.setEnabled(!recording || thisRecording);
    s.clear.setEnabled(loaded && !recording);

    if (tracks != nullptr && t < tracks->size()) {
      const juce::var& e = (*tracks)[t];
      s.mute.setToggleState(static_cast<bool>(e.getProperty("mute", false)), juce::dontSendNotification);
      s.solo.setToggleState(static_cast<bool>(e.getProperty("solo", false)), juce::dontSendNotification);
      if (!s.volume.isMouseButtonDown())
        s.volume.setValue(static_cast<double>(e.getProperty("volume", 1.0)), juce::dontSendNotification);
      if (!s.nudge.isMouseButtonDown())
        s.nudge.setValue(static_cast<double>(e.getProperty("nudge", 0.0)), juce::dontSendNotification);
    }
  }

  play_.setButtonText(playing && !recording ? "Stop" : "Play");
  play_.setEnabled(!recording);
  rewind_.setEnabled(!recording);
  loop_.setEnabled(!recording);
  loop_.setToggleState(static_cast<bool>(state.getProperty("loop", false)), juce::dontSendNotification);
  mix_.setEnabled(!recording && length > 0.0);
  position_.setEnabled(!recording && length > 0.0);
  if (!position_.isMouseButtonDown())
    position_.setValue(length > 0.0 ? juce::jlimit(0.0, 1.0, position / length) : 0.0, juce::dontSendNotification);
  time_.setText(mmss(position) + " / " + mmss(length), juce::dontSendNotification);
  updating_ = false;

  // A finished mixdown opens the share sheet straight away (the first poll
  // after opening the screen only sets the baseline).
  lastMixPath_ = state.getProperty("mixPath", {}).toString();
  shareMix_.setEnabled(lastMixPath_.isNotEmpty());
  const int serial = static_cast<int>(state.getProperty("mixSerial", 0));
  if (lastSerial_ < 0) {
    lastSerial_ = serial;
  } else if (serial != lastSerial_) {
    lastSerial_ = serial;
    if (lastMixPath_.isNotEmpty()) shareFile(juce::File(lastMixPath_));
  }
}

void MultitrackView::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void MultitrackView::resized() {
  auto area = getLocalBounds();
  close_.setBounds(area.getRight() - kCloseRight - kCloseBox, area.getY() + kCloseTop, kCloseBox, kCloseBox);
  title_.setBounds(kPad, area.getY() + 14, 120, 32);
  back_.setBounds(150, area.getY() + 14, 140, 32);
  status_.setBounds(310, area.getY() + 14, area.getWidth() - 310 - 70, 32);

  auto inner = area.reduced(kPad, 0);
  inner.removeFromTop(54);

  for (auto& sp : strips_) {
    auto row = inner.removeFromTop(kRowH);
    inner.removeFromTop(kRowGap);
    auto& s = *sp;
    s.name.setBounds(row.removeFromLeft(170));
    s.rec.setBounds(row.removeFromLeft(64));
    row.removeFromLeft(6);
    s.mute.setBounds(row.removeFromLeft(40));
    row.removeFromLeft(6);
    s.solo.setBounds(row.removeFromLeft(40));
    row.removeFromLeft(6);
    s.clear.setBounds(row.removeFromRight(64));
    row.removeFromRight(6);
    const int half = row.getWidth() / 2;
    s.volume.setBounds(row.removeFromLeft(half - 3));
    row.removeFromLeft(6);
    s.nudge.setBounds(row);
  }

  inner.removeFromTop(8);
  auto transport = inner.removeFromTop(44);
  play_.setBounds(transport.removeFromLeft(110));
  transport.removeFromLeft(8);
  rewind_.setBounds(transport.removeFromLeft(60));
  transport.removeFromLeft(8);
  loop_.setBounds(transport.removeFromLeft(80));
  transport.removeFromLeft(12);
  time_.setBounds(transport.removeFromRight(120));
  position_.setBounds(transport);

  inner.removeFromTop(8);
  auto exportRow = inner.removeFromTop(40);
  mix_.setBounds(exportRow.removeFromLeft(150));
  exportRow.removeFromLeft(12);
  mp3_.setBounds(exportRow.removeFromLeft(110));
  exportRow.removeFromLeft(12);
  shareMix_.setBounds(exportRow.removeFromLeft(160));
}

}  // namespace t3k::ui
