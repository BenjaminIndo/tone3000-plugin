#include "MultitrackView.h"

#include <algorithm>
#include <cmath>

#include "IosShare.h"
#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPad = 24;
constexpr int kTracks = 6;  // matches t3k::Multitrack::kTracks
constexpr int kRowH = 56;
constexpr int kPollHz = 12;

const char* const kEditCommand[5] = {"start", "trimStart", "trimEnd", "nudge", "pan"};
const char* const kEditCaption[5] = {"START (s)", "TRIM IN (s)", "TRIM OUT (s)", "NUDGE (ms)", "PAN"};

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

void styleSlider(juce::Slider& s, double min, double max, double step, double value, const juce::String& suffix,
                 int decimals, juce::Slider::TextEntryBoxPosition textPos, int textWidth) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  if (textPos == juce::Slider::NoTextBox)
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  else
    s.setTextBoxStyle(textPos, false, textWidth, 20);
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

void styleCombo(juce::ComboBox& c) {
  c.setColour(juce::ComboBox::backgroundColourId, theme::kSurfaceRaised);
  c.setColour(juce::ComboBox::textColourId, theme::kWhite);
  c.setColour(juce::ComboBox::outlineColourId, theme::kBorder);
  c.setColour(juce::ComboBox::arrowColourId, theme::kWhite);
  c.setMouseClickGrabsKeyboardFocus(false);
}
}  // namespace

// --------------------------------------------------------------------------
// Waveform lane: the clip's overview peaks placed on the project timeline, the
// loop / punch marks and the playhead. Clicking or dragging moves the playhead.
class MultitrackView::Waveform : public juce::Component {
public:
  std::function<void(double seconds)> onSeek;

  void setPeaks(std::vector<float> peaks) {
    peaks_ = std::move(peaks);
    repaint();
  }

  void setLayout(double timelineSec, double clipStart, double clipLength, double fileSec, double trimStart) {
    if (timelineSec == timeline_ && clipStart == clipStart_ && clipLength == clipLength_ && fileSec == fileSec_ &&
        trimStart == trimStart_)
      return;
    timeline_ = timelineSec;
    clipStart_ = clipStart;
    clipLength_ = clipLength;
    fileSec_ = fileSec;
    trimStart_ = trimStart;
    repaint();
  }

  void setMarks(double playhead, bool loopOn, double loopIn, double loopOut, bool punchOn, double punchIn,
                double punchOut) {
    if (playhead == playhead_ && loopOn == loopOn_ && loopIn == loopIn_ && loopOut == loopOut_ && punchOn == punchOn_ &&
        punchIn == punchIn_ && punchOut == punchOut_)
      return;
    playhead_ = playhead;
    loopOn_ = loopOn;
    loopIn_ = loopIn;
    loopOut_ = loopOut;
    punchOn_ = punchOn;
    punchIn_ = punchIn;
    punchOut_ = punchOut;
    repaint();
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(theme::kSurfaceRaised);
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    if (timeline_ <= 0.0 || w <= 0.0f) return;
    auto xOf = [&](double t) { return static_cast<float>(t / timeline_ * static_cast<double>(w)); };

    const int buckets = static_cast<int>(peaks_.size() / 2);
    if (clipLength_ > 0.0 && fileSec_ > 0.0 && buckets > 0) {
      const float x0 = xOf(clipStart_);
      const float x1 = xOf(clipStart_ + clipLength_);
      if (x1 > x0) {
        g.setColour(theme::kBrandBlue.withAlpha(0.22f));
        g.fillRect(x0, 0.0f, x1 - x0, h);
        g.setColour(theme::kBrandBlue.brighter(0.5f));
        const float mid = h * 0.5f;
        const double bucketsPerPixel = (clipLength_ / fileSec_ * buckets) / static_cast<double>(x1 - x0);
        for (float x = std::max(0.0f, x0); x < std::min(w, x1); x += 1.0f) {
          const double frac = static_cast<double>(x - x0) / static_cast<double>(x1 - x0);
          const double fileT = trimStart_ + frac * clipLength_;
          const double fb = fileT / fileSec_ * buckets;
          const int b0 = juce::jlimit(0, buckets - 1, static_cast<int>(fb));
          const int b1 = juce::jlimit(b0, buckets - 1, static_cast<int>(fb + std::max(0.0, bucketsPerPixel - 1.0)));
          float lo = 0.0f, hi = 0.0f;
          for (int b = b0; b <= b1; ++b) {
            lo = std::min(lo, peaks_[static_cast<size_t>(b) * 2]);
            hi = std::max(hi, peaks_[static_cast<size_t>(b) * 2 + 1]);
          }
          g.drawVerticalLine(static_cast<int>(x), mid - hi * mid, mid - lo * mid + 1.0f);
        }
      }
    }

    if (loopOn_ && loopOut_ > loopIn_) {
      g.setColour(theme::kWhite.withAlpha(0.12f));
      g.fillRect(xOf(loopIn_), 0.0f, xOf(loopOut_) - xOf(loopIn_), h);
    }
    if (punchOn_ && punchOut_ > punchIn_) {
      g.setColour(theme::kBrandRed.withAlpha(0.18f));
      g.fillRect(xOf(punchIn_), 0.0f, xOf(punchOut_) - xOf(punchIn_), h);
      g.setColour(theme::kBrandRed);
      g.drawVerticalLine(static_cast<int>(xOf(punchIn_)), 0.0f, h);
      g.drawVerticalLine(static_cast<int>(xOf(punchOut_)), 0.0f, h);
    }
    g.setColour(theme::kWhite);
    g.drawVerticalLine(static_cast<int>(xOf(std::max(0.0, playhead_))), 0.0f, h);
  }

  void mouseDown(const juce::MouseEvent& e) override { seekFrom(e); }
  void mouseDrag(const juce::MouseEvent& e) override { seekFrom(e); }

private:
  void seekFrom(const juce::MouseEvent& e) {
    if (!onSeek || getWidth() <= 0 || timeline_ <= 0.0) return;
    onSeek(juce::jlimit(0.0, timeline_, static_cast<double>(e.position.x) / getWidth() * timeline_));
  }

  std::vector<float> peaks_;
  double timeline_ = 0.0, clipStart_ = 0.0, clipLength_ = 0.0, fileSec_ = 0.0, trimStart_ = 0.0;
  double playhead_ = 0.0, loopIn_ = 0.0, loopOut_ = 0.0, punchIn_ = 0.0, punchOut_ = 0.0;
  bool loopOn_ = false, punchOn_ = false;
};

// The scrolling list of tracks: only paints the highlight of the selected one.
class MultitrackView::Content : public juce::Component {
public:
  int selected = -1;
  void paint(juce::Graphics& g) override {
    g.fillAll(theme::kBlack);
    if (selected >= 0) {
      g.setColour(theme::kBrandBlue.withAlpha(0.14f));
      g.fillRect(0, selected * kRowH, getWidth(), kRowH);
    }
    g.setColour(theme::kBorder);
    for (int t = 1; t < kTracks; ++t)
      g.drawHorizontalLine(t * kRowH - 1, 0.0f, static_cast<float>(getWidth()));
  }
};

struct MultitrackView::Strip {
  juce::TextButton name, rec, mute, solo, importBtn, clear;
  juce::Slider volume;
  std::unique_ptr<Waveform> wave = std::make_unique<Waveform>();
  int rev = -1;
  bool needPeaks = false;
};

// --------------------------------------------------------------------------

MultitrackView::MultitrackView(Services& services) : services_(services) {
  setOpaque(true);
  content_ = std::make_unique<Content>();

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

  // ---- transport row
  styleButton(play_, "Play");
  play_.onClick = [this] { command(lastPlaying_ ? "stop" : "play"); };
  styleButton(rewind_, "|<");
  rewind_.onClick = [this] { command("rewind"); };
  styleButton(loop_, "Loop");
  loop_.setClickingTogglesState(true);
  loop_.onClick = [this] { command("loop", loop_.getToggleState()); };
  styleButton(loopIn_, "Loop in");
  loopIn_.onClick = [this] { command("loopIn"); };
  styleButton(loopOut_, "Loop out");
  loopOut_.onClick = [this] { command("loopOut"); };
  styleButton(punch_, "Punch");
  punch_.setClickingTogglesState(true);
  punch_.onClick = [this] { command("punch", punch_.getToggleState()); };
  styleButton(punchIn_, "In");
  punchIn_.onClick = [this] { command("punchIn"); };
  styleButton(punchOut_, "Out");
  punchOut_.onClick = [this] { command("punchOut"); };
  for (juce::Component* c : std::initializer_list<juce::Component*>{&play_, &rewind_, &loop_, &loopIn_, &loopOut_, &punch_,
                                                                    &punchIn_, &punchOut_})
    addAndMakeVisible(c);

  styleSlider(position_, 0.0, timelineSec_, 0.01, 0.0, "", 2, juce::Slider::NoTextBox, 0);
  position_.setDoubleClickReturnValue(false, 0.0);
  position_.onValueChange = [this] {
    if (!updating_) command("seekSec", position_.getValue());
  };
  addAndMakeVisible(position_);

  time_.setText("0:00 / 0:00", juce::dontSendNotification);
  time_.setFont(Fonts::sans(14.0f, false));
  time_.setColour(juce::Label::textColourId, theme::kGray);
  time_.setJustificationType(juce::Justification::centredRight);
  time_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(time_);

  // ---- metronome / mixdown row
  styleButton(click_, "Click");
  click_.setClickingTogglesState(true);
  click_.onClick = [this] { command("metro", click_.getToggleState()); };
  addAndMakeVisible(click_);

  styleSlider(bpm_, 30.0, 300.0, 1.0, 120.0, " BPM", 0, juce::Slider::TextBoxLeft, 76);
  bpm_.onValueChange = [this] {
    if (!updating_) command("bpm", bpm_.getValue());
  };
  addAndMakeVisible(bpm_);

  styleButton(tap_, "Tap");
  tap_.onClick = [this] { tapTempo(); };
  addAndMakeVisible(tap_);

  styleCombo(sig_);
  const char* const kSigs[6] = {"4/4", "3/4", "2/4", "6/8", "5/4", "7/8"};
  for (int i = 0; i < 6; ++i)
    sig_.addItem(kSigs[i], i + 1);
  sig_.setSelectedItemIndex(0, juce::dontSendNotification);
  sig_.onChange = [this] {
    if (!updating_) command("sig", sig_.getText());
  };
  addAndMakeVisible(sig_);

  styleCombo(countIn_);
  countIn_.addItem("No count-in", 1);
  countIn_.addItem("1 bar count-in", 2);
  countIn_.addItem("2 bars count-in", 3);
  countIn_.setSelectedItemIndex(0, juce::dontSendNotification);
  countIn_.onChange = [this] {
    if (!updating_) command("countIn", countIn_.getSelectedItemIndex());
  };
  addAndMakeVisible(countIn_);

  styleSlider(clickVol_, 0.0, 1.0, 0.01, 0.5, "", 2, juce::Slider::NoTextBox, 0);
  clickVol_.onValueChange = [this] {
    if (!updating_) command("clickVol", clickVol_.getValue());
  };
  addAndMakeVisible(clickVol_);

  styleButton(mix_, "Mix down");
  mix_.onClick = [this] { command("mix", mp3_.getToggleState()); };
  addAndMakeVisible(mix_);
  mp3_.setButtonText("MP3");
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

  // ---- editor of the selected track
  for (int i = 0; i < 5; ++i) {
    auto& ctl = edit_[static_cast<size_t>(i)];
    ctl.caption.setText(kEditCaption[i], juce::dontSendNotification);
    ctl.caption.setFont(Fonts::sans(11.0f, true));
    ctl.caption.setColour(juce::Label::textColourId, theme::kGray);
    ctl.caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(ctl.caption);
    switch (i) {
      case 0: styleSlider(ctl.slider, 0.0, 120.0, 0.01, 0.0, "", 2, juce::Slider::TextBoxRight, 54); break;
      case 1:
      case 2: styleSlider(ctl.slider, 0.0, 1.0, 0.01, 0.0, "", 2, juce::Slider::TextBoxRight, 54); break;
      case 3: styleSlider(ctl.slider, -300.0, 300.0, 1.0, 0.0, "", 0, juce::Slider::TextBoxRight, 54); break;
      default: styleSlider(ctl.slider, -1.0, 1.0, 0.01, 0.0, "", 2, juce::Slider::TextBoxRight, 54); break;
    }
    ctl.slider.onValueChange = [this, i] {
      if (!updating_ && selected_ >= 0) trackCommand(kEditCommand[i], selected_, edit_[static_cast<size_t>(i)].slider.getValue());
    };
    addAndMakeVisible(ctl.slider);
  }

  // ---- tracks
  for (int t = 0; t < kTracks; ++t) {
    auto s = std::make_unique<Strip>();
    styleButton(s->name, "T" + juce::String(t + 1));
    s->name.onClick = [this, t] { selectTrack(t); };
    styleButton(s->rec, "REC");
    s->rec.onClick = [this, t] {
      if (lastRecordingTrack_ == t)
        command("stop");
      else
        trackCommand("record", t, {});
    };
    styleButton(s->mute, "M");
    s->mute.setClickingTogglesState(true);
    s->mute.onClick = [this, t] { trackCommand("mute", t, strips_[static_cast<size_t>(t)]->mute.getToggleState()); };
    styleButton(s->solo, "S");
    s->solo.setClickingTogglesState(true);
    s->solo.onClick = [this, t] { trackCommand("solo", t, strips_[static_cast<size_t>(t)]->solo.getToggleState()); };
    styleButton(s->importBtn, "Import");
    s->importBtn.onClick = [this, t] { startImport(t); };
    styleButton(s->clear, "Clear");
    s->clear.onClick = [this, t] { trackCommand("clear", t, {}); };
    styleSlider(s->volume, 0.0, 1.5, 0.01, 1.0, "", 2, juce::Slider::NoTextBox, 0);
    s->volume.onValueChange = [this, t] {
      if (!updating_) trackCommand("volume", t, strips_[static_cast<size_t>(t)]->volume.getValue());
    };
    s->wave->onSeek = [this](double seconds) { command("seekSec", seconds); };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&s->name, &s->rec, &s->mute, &s->solo, &s->importBtn,
                                                                      &s->clear, &s->volume, s->wave.get()})
      content_->addAndMakeVisible(c);
    strips_.push_back(std::move(s));
  }
  content_->selected = 0;
  viewport_.setViewedComponent(content_.get(), false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);

  apply(services_.backend.getMultitrackState());
  startTimerHz(kPollHz);
}

MultitrackView::~MultitrackView() {
  stopTimer();
  viewport_.setViewedComponent(nullptr, false);
}

void MultitrackView::command(const juce::String& cmd, const juce::var& arg) {
  apply(services_.backend.multitrackCommand(cmd, arg));
}

void MultitrackView::trackCommand(const juce::String& cmd, int track, const juce::var& value) {
  auto* o = new juce::DynamicObject();
  o->setProperty("track", track);
  o->setProperty("value", value);
  command(cmd, juce::var(o));
}

void MultitrackView::timerCallback() {
  apply(services_.backend.getMultitrackState());
  // Waveform overviews are fetched once per change of a track's audio.
  for (int t = 0; t < kTracks; ++t)
    if (strips_[static_cast<size_t>(t)]->needPeaks) trackCommand("peaks", t, {});
}

void MultitrackView::selectTrack(int track) {
  selected_ = track;
  content_->selected = track;
  content_->repaint();
  editKey_ = -1;  // refill the editor on the next poll
}

void MultitrackView::tapTempo() {
  const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
  if (!taps_.empty() && now - taps_.back() > 2.0) taps_.clear();
  taps_.push_back(now);
  if (taps_.size() > 5) taps_.erase(taps_.begin());
  if (taps_.size() < 2) return;
  const double interval = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
  if (interval <= 0.0) return;
  command("bpm", std::round(60.0 / interval * 10.0) / 10.0);
}

void MultitrackView::shareFile(const juce::File& file) { IosShare::shareFile(file); }

void MultitrackView::startImport(int track) {
  if (chooser_ != nullptr) return;
  chooser_ = std::make_unique<juce::FileChooser>("Import audio", juce::File{},
                                                 juce::String("*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.caf;*.flac"));
  const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
  juce::Component::SafePointer<MultitrackView> self(this);
  chooser_->launchAsync(flags, [self, track](const juce::FileChooser& chooser) {
    if (self == nullptr) return;
    // Release the chooser once its callback unwinds (it is the caller).
    juce::MessageManager::callAsync([self] {
      if (self != nullptr) self->chooser_.reset();
    });
#if JUCE_IOS
    // URL results: the picked files live outside the sandbox and are only
    // readable through the security scope JUCE bookmarked.
    const auto results = chooser.getURLResults();
    if (results.isEmpty()) return;
    self->finishImport(track, results.getReference(0));
#else
    const auto results = chooser.getResults();
    if (results.isEmpty()) return;
    self->finishImport(track, juce::URL(results.getReference(0)));
#endif
  });
}

void MultitrackView::finishImport(int track, const juce::URL& url) {
  const auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                       .getChildFile("Recordings")
                       .getChildFile("Imports");
  juce::String problem;
  juce::File dest;
  if (!dir.createDirectory().wasOk()) {
    problem = "Could not create the imports folder.";
  } else {
    juce::String name = juce::File::createLegalFileName(url.getFileName());
    if (name.isEmpty()) name = "Imported audio";
    dest = dir.getChildFile(name).getNonexistentSibling();
    bool ok = false;
    const auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
    if (in != nullptr) {
      juce::FileOutputStream out(dest);
      if (!out.failedToOpen()) {
        out.setPosition(0);
        out.truncate();
        ok = out.writeFromInputStream(*in, -1) > 0;
        out.flush();
      }
    }
    if (!ok) {
      dest.deleteFile();
      problem = "Could not read the selected file.";
    }
  }
  if (problem.isNotEmpty()) {
    localError_ = problem;
    localErrorUntil_ = juce::Time::getMillisecondCounter() + 4000;
    return;
  }
  trackCommand("import", track, "Imports/" + dest.getFileName());
}

void MultitrackView::apply(const juce::var& state) {
  if (!state.isObject()) return;
  auto num = [&](const char* key, double def) { return static_cast<double>(state.getProperty(key, def)); };
  auto flag = [&](const char* key) { return static_cast<bool>(state.getProperty(key, false)); };

  const bool playing = flag("playing");
  const bool recording = flag("recording");
  const int recTrack = static_cast<int>(num("recordingTrack", -1));
  const double position = num("position", 0.0);
  const double length = num("length", 0.0);
  const double loopIn = num("loopIn", 0.0), loopOut = num("loopOut", 0.0);
  const double punchIn = num("punchIn", 0.0), punchOut = num("punchOut", 0.0);
  const bool loopOn = flag("loop"), punchOn = flag("punch");
  const juce::String error = state.getProperty("error", {}).toString();
  const juce::String message = state.getProperty("message", {}).toString();
  lastPlaying_ = playing;
  lastRecordingTrack_ = recording ? recTrack : -1;
  timelineSec_ = std::max({8.0, length, loopOut, punchOut});

  // ---- status line
  juce::String text;
  juce::Colour colour = theme::kWhite;
  const bool showLocal = localError_.isNotEmpty() && juce::Time::getMillisecondCounter() < localErrorUntil_;
  if (showLocal) {
    text = localError_;
    colour = theme::kBrandRed;
  } else if (error.isNotEmpty()) {
    text = error;
    colour = theme::kBrandRed;
  } else if (recording && position < 0.0) {
    text = "Count-in...";
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

  // ---- transport
  play_.setButtonText(playing && !recording ? "Stop" : "Play");
  play_.setEnabled(!recording);
  rewind_.setEnabled(!recording);
  loop_.setEnabled(!recording);
  loop_.setToggleState(loopOn, juce::dontSendNotification);
  loopIn_.setEnabled(!recording);
  loopOut_.setEnabled(!recording);
  punch_.setEnabled(!recording);
  punch_.setToggleState(punchOn, juce::dontSendNotification);
  punchIn_.setEnabled(!recording);
  punchOut_.setEnabled(!recording);
  if (timelineSec_ != lastTimelineSec_) {
    position_.setRange(0.0, timelineSec_, 0.01);
    lastTimelineSec_ = timelineSec_;
  }
  position_.setEnabled(!recording);
  if (!position_.isMouseButtonDown()) position_.setValue(std::max(0.0, position), juce::dontSendNotification);
  time_.setText(mmss(std::max(0.0, position)) + " / " + mmss(length), juce::dontSendNotification);

  // ---- metronome
  click_.setToggleState(flag("metro"), juce::dontSendNotification);
  if (!bpm_.isMouseButtonDown()) bpm_.setValue(num("bpm", 120.0), juce::dontSendNotification);
  const juce::String sig = juce::String(static_cast<int>(num("beats", 4))) + "/" + juce::String(static_cast<int>(num("denom", 4)));
  for (int i = 0; i < sig_.getNumItems(); ++i)
    if (sig_.getItemText(i) == sig) sig_.setSelectedItemIndex(i, juce::dontSendNotification);
  countIn_.setSelectedItemIndex(juce::jlimit(0, 2, static_cast<int>(num("countIn", 0))), juce::dontSendNotification);
  if (!clickVol_.isMouseButtonDown()) clickVol_.setValue(num("clickVol", 0.5), juce::dontSendNotification);
  mix_.setEnabled(!recording && length > 0.0);

  // ---- tracks
  const juce::Array<juce::var>* tracks = state.getProperty("tracks", {}).getArray();
  for (int t = 0; t < kTracks; ++t) {
    auto& s = *strips_[static_cast<size_t>(t)];
    if (tracks == nullptr || t >= tracks->size()) continue;
    const juce::var e = (*tracks)[t];
    const bool loaded = static_cast<bool>(e.getProperty("loaded", false));
    const bool imported = static_cast<bool>(e.getProperty("imported", false));
    const double fileSec = static_cast<double>(e.getProperty("fileSeconds", 0.0));
    const double clipStart = static_cast<double>(e.getProperty("clipStart", 0.0));
    const double clipLength = static_cast<double>(e.getProperty("clipLength", 0.0));
    const double trimStart = static_cast<double>(e.getProperty("trimStart", 0.0));
    const int rev = static_cast<int>(e.getProperty("rev", 0));

    const juce::String label = "T" + juce::String(t + 1) + (loaded ? "  " + mmss(clipLength) + (imported ? "  song" : "") : "  empty");
    if (s.name.getButtonText() != label) s.name.setButtonText(label);

    const bool thisRecording = recording && recTrack == t;
    s.rec.setButtonText(thisRecording ? "STOP" : "REC");
    s.rec.setColour(juce::TextButton::buttonColourId, thisRecording ? theme::kBrandRed : theme::kSurfaceRaised);
    s.rec.setEnabled(!recording || thisRecording);
    s.importBtn.setEnabled(!recording);
    s.clear.setEnabled(loaded && !recording);
    s.mute.setToggleState(static_cast<bool>(e.getProperty("mute", false)), juce::dontSendNotification);
    s.solo.setToggleState(static_cast<bool>(e.getProperty("solo", false)), juce::dontSendNotification);
    if (!s.volume.isMouseButtonDown())
      s.volume.setValue(static_cast<double>(e.getProperty("volume", 1.0)), juce::dontSendNotification);

    if (rev != s.rev) {
      s.rev = rev;
      s.needPeaks = loaded;
      if (!loaded) s.wave->setPeaks({});
    }
    s.wave->setLayout(timelineSec_, clipStart, clipLength, fileSec, trimStart);
    s.wave->setMarks(position, loopOn, loopIn, loopOut, punchOn, punchIn, punchOut);

    // The editor follows the selected track.
    if (t == selected_) {
      const juce::int64 key = static_cast<juce::int64>(rev) + static_cast<juce::int64>(t) * 1000000 +
                              static_cast<juce::int64>(fileSec * 1000.0) * 10000000;
      if (key != editKey_) {
        editKey_ = key;
        const double top = fileSec > 0.0 ? fileSec : 1.0;
        edit_[1].slider.setRange(0.0, top, 0.01);
        edit_[2].slider.setRange(0.0, top, 0.01);
      }
      const char* const keys[5] = {"start", "trimStart", "trimEnd", "nudge", "pan"};
      for (int i = 0; i < 5; ++i) {
        auto& slider = edit_[static_cast<size_t>(i)].slider;
        slider.setEnabled(loaded && !recording);
        if (!slider.isMouseButtonDown())
          slider.setValue(static_cast<double>(e.getProperty(keys[i], 0.0)), juce::dontSendNotification);
      }
    }
  }

  // ---- waveform overview delivered for one track
  if (state.hasProperty("peaks")) {
    const int pt = static_cast<int>(num("peaksTrack", -1));
    if (pt >= 0 && pt < kTracks) {
      std::vector<float> v;
      if (auto* arr = state.getProperty("peaks", {}).getArray()) {
        v.reserve(static_cast<size_t>(arr->size()));
        for (const auto& item : *arr)
          v.push_back(static_cast<float>(static_cast<double>(item)));
      }
      strips_[static_cast<size_t>(pt)]->wave->setPeaks(std::move(v));
      strips_[static_cast<size_t>(pt)]->needPeaks = false;
    }
  }
  updating_ = false;

  // A finished mixdown opens the share sheet straight away (the first poll
  // after opening the screen only sets the baseline).
  lastMixPath_ = state.getProperty("mixPath", {}).toString();
  shareMix_.setEnabled(lastMixPath_.isNotEmpty());
  const int serial = static_cast<int>(num("mixSerial", 0));
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

  // Transport and marks.
  auto row = inner.removeFromTop(36);
  inner.removeFromTop(4);
  play_.setBounds(row.removeFromLeft(80));
  row.removeFromLeft(6);
  rewind_.setBounds(row.removeFromLeft(48));
  row.removeFromLeft(14);
  loop_.setBounds(row.removeFromLeft(62));
  row.removeFromLeft(4);
  loopIn_.setBounds(row.removeFromLeft(70));
  row.removeFromLeft(4);
  loopOut_.setBounds(row.removeFromLeft(76));
  row.removeFromLeft(14);
  punch_.setBounds(row.removeFromLeft(66));
  row.removeFromLeft(4);
  punchIn_.setBounds(row.removeFromLeft(44));
  row.removeFromLeft(4);
  punchOut_.setBounds(row.removeFromLeft(48));
  row.removeFromLeft(14);
  time_.setBounds(row.removeFromRight(104));
  position_.setBounds(row);

  // Metronome and mixdown.
  row = inner.removeFromTop(36);
  inner.removeFromTop(4);
  click_.setBounds(row.removeFromLeft(64));
  row.removeFromLeft(6);
  bpm_.setBounds(row.removeFromLeft(210));
  row.removeFromLeft(6);
  tap_.setBounds(row.removeFromLeft(54));
  row.removeFromLeft(6);
  sig_.setBounds(row.removeFromLeft(72));
  row.removeFromLeft(6);
  countIn_.setBounds(row.removeFromLeft(130));
  row.removeFromLeft(6);
  clickVol_.setBounds(row.removeFromLeft(80));
  row.removeFromLeft(14);
  shareMix_.setBounds(row.removeFromRight(120));
  row.removeFromRight(6);
  mp3_.setBounds(row.removeFromRight(62));
  row.removeFromRight(6);
  mix_.setBounds(row.removeFromRight(90));

  // Editor of the selected track: caption above, slider below.
  auto editRow = inner.removeFromTop(44);
  inner.removeFromTop(4);
  const int editW = (editRow.getWidth() - 4 * 12) / 5;
  for (int i = 0; i < 5; ++i) {
    auto cell = editRow.removeFromLeft(editW);
    editRow.removeFromLeft(12);
    edit_[static_cast<size_t>(i)].caption.setBounds(cell.removeFromTop(14));
    edit_[static_cast<size_t>(i)].slider.setBounds(cell);
  }

  // The tracks scroll in what is left.
  viewport_.setBounds(inner);
  const int contentW = std::max(1, viewport_.getMaximumVisibleWidth());
  content_->setSize(contentW, kTracks * kRowH);
  for (int t = 0; t < kTracks; ++t) {
    auto& s = *strips_[static_cast<size_t>(t)];
    auto line = juce::Rectangle<int>(0, t * kRowH + 2, contentW, 28);
    s.name.setBounds(line.removeFromLeft(190));
    line.removeFromLeft(4);
    s.rec.setBounds(line.removeFromLeft(56));
    line.removeFromLeft(4);
    s.mute.setBounds(line.removeFromLeft(36));
    line.removeFromLeft(4);
    s.solo.setBounds(line.removeFromLeft(36));
    line.removeFromLeft(8);
    s.clear.setBounds(line.removeFromRight(56));
    line.removeFromRight(4);
    s.importBtn.setBounds(line.removeFromRight(70));
    line.removeFromRight(8);
    s.volume.setBounds(line);
    s.wave->setBounds(0, t * kRowH + 32, contentW, 22);
  }
}

}  // namespace t3k::ui
