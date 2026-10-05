#include "MultitrackView.h"

#include <algorithm>
#include <cmath>

#include "IosShare.h"
#include "core/Fonts.h"

namespace t3k::ui {

namespace {
constexpr int kBarH = 64;     // control bar
constexpr int kHeaderW = 230;  // track header column
constexpr int kRulerH = 30;   // bar numbers
constexpr int kMarkH = 22;    // loop band (top half) and punch band (bottom half)
constexpr int kPollHz = 15;
constexpr double kMinPxPerSec = 2.0;
constexpr double kMaxPxPerSec = 400.0;

const juce::Colour kBackground{0xff0b0b0c};
const juce::Colour kBarColour{0xff1c1c1e};
const juce::Colour kButtonColour{0xff2c2c2e};
const juce::Colour kGrey{0xff8d8d93};
const juce::Colour kLoopColour{0xffffcc00};
const juce::Colour kPunchColour{0xffff453a};
const juce::Colour kBlue{0xff0a84ff};

juce::Colour trackColour(int t) {
  static const juce::Colour colours[6] = {juce::Colour(0xff3d8bfd), juce::Colour(0xff32d74b), juce::Colour(0xffff9f0a),
                                          juce::Colour(0xffbf5af2), juce::Colour(0xffffd60a), juce::Colour(0xff64d2ff)};
  return colours[((t % 6) + 6) % 6];
}

juce::String mmss(double seconds) {
  const int total = juce::jmax(0, static_cast<int>(seconds));
  return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}

juce::String clockText(double seconds) {
  const double s = juce::jmax(0.0, seconds);
  const int tenths = static_cast<int>(std::floor(s * 10.0)) % 10;
  return mmss(s) + "." + juce::String(tenths);
}

// Sends at most ~12 commands a second while a slider is dragged.
struct Throttle {
  juce::uint32 last = 0;
  bool ready() {
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (now - last < 80) return false;
    last = now;
    return true;
  }
};

void styleSlider(juce::Slider& s, double min, double max, double step, double value,
                 juce::Slider::TextEntryBoxPosition textPos = juce::Slider::NoTextBox, int textWidth = 0,
                 const juce::String& suffix = {}, int decimals = 2) {
  s.setSliderStyle(juce::Slider::LinearHorizontal);
  if (textPos == juce::Slider::NoTextBox)
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  else
    s.setTextBoxStyle(textPos, false, textWidth, 24);
  s.setRange(min, max, step);
  s.setValue(value, juce::dontSendNotification);
  s.setTextValueSuffix(suffix);
  s.setNumDecimalPlacesToDisplay(decimals);
  s.setDoubleClickReturnValue(true, value);
  s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
  s.setColour(juce::Slider::trackColourId, kBlue);
  s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff3a3a3c));
  s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
  s.setMouseClickGrabsKeyboardFocus(false);
}

void styleTextButton(juce::TextButton& b, const juce::String& text) {
  b.setButtonText(text);
  b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3c));
  b.setColour(juce::TextButton::buttonOnColourId, kBlue);
  b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
  b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
  b.setMouseClickGrabsKeyboardFocus(false);
}

void styleCaption(juce::Label& l, const juce::String& text) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(Fonts::sans(14.0f, true));
  l.setColour(juce::Label::textColourId, kGrey);
  l.setInterceptsMouseClicks(false, false);
}

const char* const kSigs[6] = {"4/4", "3/4", "2/4", "6/8", "5/4", "7/8"};
}  // namespace

// ==========================================================================
// A rounded touch button that draws its own icon (or a short caption).
class MultitrackView::GlyphButton : public juce::Button {
public:
  enum class Glyph { none, play, stop, record, rewind, loop, metronome, countIn, zoomIn, zoomOut, more };

  explicit GlyphButton(Glyph g, const juce::String& caption = {}) : juce::Button(caption), glyph_(g), caption_(caption) {
    setMouseClickGrabsKeyboardFocus(false);
    setWantsKeyboardFocus(false);
  }

  juce::Colour activeColour = kBlue;
  juce::String badge;

  void setGlyph(Glyph g) {
    if (glyph_ != g) {
      glyph_ = g;
      repaint();
    }
  }
  void setActive(bool a) {
    if (active_ != a) {
      active_ = a;
      repaint();
    }
  }
  bool isActive() const { return active_; }

  void paintButton(juce::Graphics& g, bool over, bool down) override {
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    juce::Colour bg = active_ ? activeColour : kButtonColour;
    if (down)
      bg = bg.brighter(0.18f);
    else if (over)
      bg = bg.brighter(0.06f);
    g.setColour(bg.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(b, 10.0f);

    const juce::Colour fg = (active_ && activeColour.getBrightness() > 0.75f ? juce::Colours::black : juce::Colours::white)
                                .withMultipliedAlpha(alpha);
    if (caption_.isNotEmpty()) {
      g.setColour(fg);
      g.setFont(Fonts::sans(15.0f, true));
      g.drawText(caption_, b, juce::Justification::centred, false);
      return;
    }
    const float s = std::min(b.getWidth(), b.getHeight()) * 0.42f;
    const auto c = b.getCentre();
    juce::Path p;
    switch (glyph_) {
      case Glyph::play:
        p.addTriangle(c.x - s * 0.4f, c.y - s * 0.5f, c.x - s * 0.4f, c.y + s * 0.5f, c.x + s * 0.55f, c.y);
        g.setColour(fg);
        g.fillPath(p);
        break;
      case Glyph::stop:
        g.setColour(fg);
        g.fillRoundedRectangle(c.x - s * 0.42f, c.y - s * 0.42f, s * 0.84f, s * 0.84f, 3.0f);
        break;
      case Glyph::record:
        g.setColour((active_ ? juce::Colours::white : kPunchColour).withMultipliedAlpha(alpha));
        g.fillEllipse(c.x - s * 0.5f, c.y - s * 0.5f, s, s);
        break;
      case Glyph::rewind:
        g.setColour(fg);
        g.fillRect(c.x - s * 0.62f, c.y - s * 0.45f, s * 0.14f, s * 0.9f);
        p.addTriangle(c.x - s * 0.44f, c.y, c.x + s * 0.06f, c.y - s * 0.45f, c.x + s * 0.06f, c.y + s * 0.45f);
        p.addTriangle(c.x + s * 0.04f, c.y, c.x + s * 0.54f, c.y - s * 0.45f, c.x + s * 0.54f, c.y + s * 0.45f);
        g.fillPath(p);
        break;
      case Glyph::loop: {
        g.setColour(fg);
        juce::Path r;
        r.addRoundedRectangle(c.x - s * 0.62f, c.y - s * 0.36f, s * 1.24f, s * 0.72f, s * 0.3f);
        g.strokePath(r, juce::PathStrokeType(2.0f));
        p.addTriangle(c.x + s * 0.02f, c.y - s * 0.58f, c.x + s * 0.02f, c.y - s * 0.14f, c.x + s * 0.34f, c.y - s * 0.36f);
        g.fillPath(p);
        break;
      }
      case Glyph::metronome: {
        g.setColour(fg);
        juce::Path m;
        m.startNewSubPath(c.x - s * 0.45f, c.y + s * 0.55f);
        m.lineTo(c.x - s * 0.2f, c.y - s * 0.55f);
        m.lineTo(c.x + s * 0.2f, c.y - s * 0.55f);
        m.lineTo(c.x + s * 0.45f, c.y + s * 0.55f);
        m.closeSubPath();
        g.strokePath(m, juce::PathStrokeType(2.0f));
        g.drawLine(c.x, c.y + s * 0.3f, c.x + s * 0.38f, c.y - s * 0.45f, 2.0f);
        break;
      }
      case Glyph::countIn:
        g.setColour(fg);
        g.setFont(Fonts::sans(13.0f, true));
        g.drawText("1 2 3", b, juce::Justification::centred, false);
        break;
      case Glyph::zoomIn:
      case Glyph::zoomOut:
        g.setColour(fg);
        g.drawLine(c.x - s * 0.45f, c.y, c.x + s * 0.45f, c.y, 2.5f);
        if (glyph_ == Glyph::zoomIn) g.drawLine(c.x, c.y - s * 0.45f, c.x, c.y + s * 0.45f, 2.5f);
        break;
      case Glyph::more:
        g.setColour(fg);
        for (int i = -1; i <= 1; ++i)
          g.fillEllipse(c.x + static_cast<float>(i) * s * 0.48f - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
        break;
      case Glyph::none:
        break;
    }
    if (badge.isNotEmpty()) {
      g.setColour(fg);
      g.setFont(Fonts::sans(11.0f, true));
      g.drawText(badge, b.reduced(5.0f, 2.0f), juce::Justification::bottomRight, false);
    }
  }

private:
  Glyph glyph_;
  juce::String caption_;
  bool active_ = false;
};

// ==========================================================================
// The LCD: bar.beat, time, and tempo / signature / status. Tap = song settings.
class MultitrackView::Lcd : public juce::Component {
public:
  explicit Lcd(MultitrackView& o) : owner(o) {}
  std::function<void()> onTap;

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff121214));
    g.fillRoundedRectangle(b, 10.0f);
    g.setColour(juce::Colour(0xff3a3a3c));
    g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);

    const auto& m = owner.model_;
    const double beat = owner.beatSeconds();
    const int beats = std::max(1, m.beats);
    const double pos = std::max(0.0, m.position);
    const int beatIndex = beat > 0.0 ? static_cast<int>(std::floor(pos / beat + 1.0e-6)) : 0;
    const int bar = beatIndex / beats + 1;
    const int inBar = beatIndex % beats + 1;

    auto area = getLocalBounds().reduced(14, 4);
    auto line1 = area.removeFromTop(static_cast<int>(area.getHeight() * 0.62));
    g.setColour(juce::Colours::white);
    g.setFont(Fonts::mono(24.0f, true));
    g.drawText(juce::String(bar) + "." + juce::String(inBar), line1.removeFromLeft(110), juce::Justification::centredLeft,
               false);
    g.setColour(kGrey);
    g.setFont(Fonts::mono(15.0f));
    g.drawText(clockText(m.position), line1, juce::Justification::centredRight, false);

    juce::Colour colour = kGrey;
    const juce::String status = owner.statusText(colour);
    g.setColour(colour);
    g.setFont(Fonts::sans(12.0f, true));
    g.drawText(status, area, juce::Justification::centredLeft, true);
  }

  void mouseUp(const juce::MouseEvent& e) override {
    if (!e.mouseWasDraggedSinceMouseDown() && onTap) onTap();
  }

private:
  MultitrackView& owner;
};

// ==========================================================================
// Track headers: colour, name, length, mute / solo, volume, meter, settings.
class MultitrackView::Headers : public juce::Component {
public:
  explicit Headers(MultitrackView& o) : owner(o) {
    for (int t = 0; t < kTracks; ++t) {
      auto r = std::make_unique<Row>();
      r->mute = std::make_unique<GlyphButton>(GlyphButton::Glyph::none, "M");
      r->mute->activeColour = kBlue;
      r->mute->onClick = [this, t] { owner.trackCommand("mute", t, !owner.model_.tracks[t].mute); };
      r->solo = std::make_unique<GlyphButton>(GlyphButton::Glyph::none, "S");
      r->solo->activeColour = kLoopColour;
      r->solo->onClick = [this, t] { owner.trackCommand("solo", t, !owner.model_.tracks[t].solo); };
      r->more = std::make_unique<GlyphButton>(GlyphButton::Glyph::more);
      r->more->onClick = [this, t] { owner.openTrackPanel(t); };
      styleSlider(r->volume, 0.0, 1.5, 0.01, 1.0);
      Row* row = r.get();
      r->volume.onValueChange = [this, t, row] {
        if (!syncing_ && row->throttle.ready()) owner.trackCommand("volume", t, row->volume.getValue());
      };
      r->volume.onDragEnd = [this, t, row] { owner.trackCommand("volume", t, row->volume.getValue()); };
      addAndMakeVisible(*r->mute);
      addAndMakeVisible(*r->solo);
      addAndMakeVisible(*r->more);
      addAndMakeVisible(r->volume);
      rows_.push_back(std::move(r));
    }
  }

  int laneHeight() const { return std::max(40, (getHeight() - (kRulerH + kMarkH)) / kTracks); }

  void refresh() {
    syncing_ = true;
    for (int t = 0; t < kTracks; ++t) {
      const auto& tm = owner.model_.tracks[t];
      auto& r = *rows_[static_cast<size_t>(t)];
      r.mute->setActive(tm.mute);
      r.solo->setActive(tm.solo);
      if (!r.volume.isMouseButtonDown()) r.volume.setValue(tm.volume, juce::dontSendNotification);
    }
    syncing_ = false;
    repaint();
  }

  void resized() override {
    const int laneH = laneHeight();
    const int w = getWidth();
    for (int t = 0; t < kTracks; ++t) {
      const int y = kRulerH + kMarkH + t * laneH;
      auto& r = *rows_[static_cast<size_t>(t)];
      r.more->setBounds(w - 22 - 40, y + 6, 40, 30);
      r.mute->setBounds(14, y + laneH - 40, 40, 34);
      r.solo->setBounds(58, y + laneH - 40, 40, 34);
      r.volume.setBounds(104, y + laneH - 40, w - 104 - 24, 34);
    }
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(kBackground);
    g.setColour(kBarColour);
    g.fillRect(0, 0, getWidth(), kRulerH + kMarkH);
    g.setColour(kGrey);
    g.setFont(Fonts::sans(12.0f, true));
    g.drawText("TRACKS", 14, 0, getWidth() - 28, kRulerH + kMarkH, juce::Justification::centredLeft, false);

    const auto& m = owner.model_;
    const int laneH = laneHeight();
    for (int t = 0; t < kTracks; ++t) {
      const auto& tm = m.tracks[t];
      const bool selected = t == owner.selected_;
      const juce::Rectangle<int> r(0, kRulerH + kMarkH + t * laneH, getWidth(), laneH);
      g.setColour(selected ? juce::Colour(0xff2a2a2e) : juce::Colour(0xff1a1a1c));
      g.fillRect(r.reduced(0, 1));
      g.setColour(trackColour(t));
      g.fillRect(r.getX(), r.getY() + 1, 5, r.getHeight() - 2);

      const juce::String name = tm.label.isNotEmpty() ? tm.label : "Track " + juce::String(t + 1);
      g.setColour(tm.loaded ? juce::Colours::white : kGrey);
      g.setFont(Fonts::sans(15.0f, true));
      g.drawText(name, r.getX() + 14, r.getY() + 7, r.getWidth() - 14 - 74, 20, juce::Justification::centredLeft, true);
      if (laneH >= 72) {
        const double length =
            std::max(0.0, (tm.trimEnd > 0.0 ? std::min(tm.trimEnd, tm.fileSec) : tm.fileSec) - tm.trimStart);
        g.setColour(m.recording && m.recTrack == t ? kPunchColour : kGrey);
        g.setFont(Fonts::sans(12.0f, false));
        const juce::String sub = m.recording && m.recTrack == t ? juce::String("Recording")
                                 : tm.loaded                    ? mmss(length)
                                                                : juce::String("Empty");
        g.drawText(sub, r.getX() + 14, r.getY() + 26, r.getWidth() - 14 - 74, 16, juce::Justification::centredLeft, true);
      }

      // Meter: the track's playback, plus the live guitar on the selected track.
      const float level = std::max(tm.meter, selected ? m.liveMeter : 0.0f);
      const float db = level > 1.0e-5f ? 20.0f * std::log10(level) : -100.0f;
      const float frac = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
      const juce::Rectangle<float> meter(static_cast<float>(r.getRight() - 12), static_cast<float>(r.getY() + 8), 6.0f,
                                         static_cast<float>(r.getHeight() - 16));
      g.setColour(juce::Colour(0xff2c2c2e));
      g.fillRoundedRectangle(meter, 3.0f);
      if (frac > 0.0f) {
        g.setColour(db > -1.0f ? kPunchColour : db > -6.0f ? kLoopColour : juce::Colour(0xff32d74b));
        g.fillRoundedRectangle(meter.withTop(meter.getBottom() - meter.getHeight() * frac), 3.0f);
      }
    }
  }

  void mouseDown(const juce::MouseEvent& e) override {
    const int laneH = laneHeight();
    if (e.y < kRulerH + kMarkH) return;
    const int t = (e.y - kRulerH - kMarkH) / laneH;
    if (t < 0 || t >= kTracks) return;
    owner.selectTrack(t);
    if (e.getNumberOfClicks() >= 2) owner.openTrackPanel(t);
  }

private:
  struct Row {
    std::unique_ptr<GlyphButton> mute, solo, more;
    juce::Slider volume;
    Throttle throttle;
  };
  MultitrackView& owner;
  std::vector<std::unique_ptr<Row>> rows_;
  bool syncing_ = false;
};

// ==========================================================================
// The timeline: ruler, loop / punch bands, lanes with regions, playhead, and
// every touch gesture (scrub, move, trim, bands, scroll, pinch).
class MultitrackView::Timeline : public juce::Component {
public:
  explicit Timeline(MultitrackView& o) : owner(o) { setOpaque(true); }

  int laneHeight() const { return laneH_; }

  void zoomBy(double factor) { zoomAround(getWidth() * 0.5f, factor); }

  // After every state poll: fit the first content, follow the playhead.
  void refresh() {
    const auto& m = owner.model_;
    if (!fitted_ && getWidth() > 0) {
      bool any = false;
      for (const auto& t : m.tracks)
        any = any || t.loaded;
      pxPerSec_ = getWidth() / std::max(30.0, m.length * 1.15);
      fitted_ = any;
    }
    if (m.playing && mode_ == Mode::none && pxPerSec_ > 0.0) {
      const double pos = std::max(0.0, m.position);
      const float x = xOf(pos);
      if (x > getWidth() * 0.92f || x < 0.0f) scrollSec_ = std::max(0.0, pos - 0.08 * getWidth() / pxPerSec_);
    }
    repaint();
  }

  void resized() override { laneH_ = std::max(40, (getHeight() - top()) / kTracks); }

  void paint(juce::Graphics& g) override {
    const auto& m = owner.model_;
    const float w = static_cast<float>(getWidth());
    const int h = getHeight();
    g.fillAll(kBackground);

    const double beat = owner.beatSeconds();
    const int beats = std::max(1, m.beats);
    const double bar = beat * beats;

    // ---- ruler
    g.setColour(kBarColour);
    g.fillRect(0, 0, getWidth(), kRulerH);
    if (bar > 0.0 && pxPerSec_ > 0.0) {
      const double barPx = bar * pxPerSec_;
      const int step = std::max(1, static_cast<int>(std::ceil(44.0 / barPx)));
      g.setFont(Fonts::sans(12.0f, true));
      const int first = std::max(0, static_cast<int>(std::floor(scrollSec_ / bar)));
      for (int b = first; b < first + 5000; ++b) {
        const float x = xOf(b * bar);
        if (x > w) break;
        if (b % step == 0) {
          g.setColour(kGrey);
          g.drawVerticalLine(static_cast<int>(x), kRulerH - 12.0f, static_cast<float>(kRulerH));
          g.drawText(juce::String(b + 1), static_cast<int>(x) + 4, 4, 40, kRulerH - 8, juce::Justification::topLeft, false);
        } else {
          g.setColour(juce::Colour(0xff3a3a3c));
          g.drawVerticalLine(static_cast<int>(x), kRulerH - 6.0f, static_cast<float>(kRulerH));
        }
      }
    }

    // ---- loop / punch bands
    g.setColour(juce::Colour(0xff141416));
    g.fillRect(0, kRulerH, getWidth(), kMarkH);
    double la = m.loopIn, lb = m.loopOut, pa = m.punchIn, pb = m.punchOut;
    if (bandPreview_) {
      if (bandLoop_) {
        la = bandA_;
        lb = bandB_;
      } else {
        pa = bandA_;
        pb = bandB_;
      }
    }
    drawBand(g, la, lb, true, m.loopOn || (bandPreview_ && bandLoop_));
    drawBand(g, pa, pb, false, m.punchOn || (bandPreview_ && !bandLoop_));

    // ---- lanes and grid
    for (int t = 0; t < kTracks; ++t) {
      const int y = top() + t * laneH_;
      g.setColour(t == owner.selected_ ? juce::Colour(0xff1f1f23)
                                       : (t % 2 != 0 ? juce::Colour(0xff131315) : juce::Colour(0xff111113)));
      g.fillRect(0, y, getWidth(), laneH_);
      g.setColour(juce::Colour(0xff222225));
      g.drawHorizontalLine(y + laneH_ - 1, 0.0f, w);
    }
    if (beat > 0.0 && pxPerSec_ > 0.0 && bar * pxPerSec_ >= 4.0) {
      const bool drawBeats = beat * pxPerSec_ > 12.0;
      const juce::int64 first = std::max<juce::int64>(0, static_cast<juce::int64>(std::floor(scrollSec_ / beat)));
      for (juce::int64 k = first; k < first + 20000; ++k) {
        const float x = xOf(static_cast<double>(k) * beat);
        if (x > w) break;
        const bool isBar = (k % beats) == 0;
        if (!isBar && !drawBeats) continue;
        g.setColour(isBar ? juce::Colour(0x30ffffff) : juce::Colour(0x12ffffff));
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(top()), static_cast<float>(h));
      }
    }

    // ---- regions
    bool any = false;
    for (int t = 0; t < kTracks; ++t) {
      const auto& tm = m.tracks[t];
      if (!tm.loaded) continue;
      any = true;
      const Clip c = clipOf(t, true);
      if (c.len <= 0.0) continue;
      const float x0 = xOf(c.t0);
      const float x1 = xOf(c.t0 + c.len);
      if (x1 < 0.0f || x0 > w) continue;
      const juce::Rectangle<float> r(x0, static_cast<float>(top() + t * laneH_ + 4), std::max(3.0f, x1 - x0),
                                     static_cast<float>(laneH_ - 8));
      const juce::Colour col = trackColour(t);
      g.setColour(col.darker(0.55f));
      g.fillRoundedRectangle(r, 6.0f);
      const auto titleBar = r.withHeight(std::min(18.0f, r.getHeight() * 0.3f));
      g.setColour(col.darker(0.1f));
      g.fillRoundedRectangle(titleBar, 6.0f);
      g.fillRect(titleBar.withTrimmedTop(titleBar.getHeight() * 0.5f));
      g.setColour(juce::Colours::black.withAlpha(0.8f));
      g.setFont(Fonts::sans(11.0f, true));
      g.drawText(tm.label.isNotEmpty() ? tm.label : "Track " + juce::String(t + 1), titleBar.reduced(6.0f, 0.0f),
                 juce::Justification::centredLeft, true);

      const auto body = r.withTrimmedTop(titleBar.getHeight() + 1.0f).reduced(0.0f, 2.0f);
      const int buckets = static_cast<int>(tm.peaks.size() / 2);
      if (buckets > 0 && tm.fileSec > 0.0 && x1 > x0) {
        g.setColour(col.brighter(0.35f));
        const float mid = body.getCentreY();
        const float half = body.getHeight() * 0.5f;
        const double bucketsPerPixel = (c.len / tm.fileSec * buckets) / static_cast<double>(x1 - x0);
        for (float x = std::max(0.0f, x0); x < std::min(w, x1); x += 1.0f) {
          const double fileT = c.trimStart + static_cast<double>(x - x0) / static_cast<double>(x1 - x0) * c.len;
          const double fb = fileT / tm.fileSec * buckets;
          const int b0 = juce::jlimit(0, buckets - 1, static_cast<int>(fb));
          const int b1 = juce::jlimit(b0, buckets - 1, static_cast<int>(fb + std::max(0.0, bucketsPerPixel - 1.0)));
          float lo = 0.0f, hi = 0.0f;
          for (int k = b0; k <= b1; ++k) {
            lo = std::min(lo, tm.peaks[static_cast<size_t>(k) * 2]);
            hi = std::max(hi, tm.peaks[static_cast<size_t>(k) * 2 + 1]);
          }
          g.drawVerticalLine(static_cast<int>(x), mid - hi * half, mid - lo * half + 1.0f);
        }
      }
      if (t == owner.selected_) {
        g.setColour(juce::Colours::white);
        g.drawRoundedRectangle(r.reduced(1.0f), 6.0f, 2.0f);
        // Trim handles.
        g.fillRoundedRectangle(r.getX() + 3.0f, body.getCentreY() - 10.0f, 4.0f, 20.0f, 2.0f);
        g.fillRoundedRectangle(r.getRight() - 7.0f, body.getCentreY() - 10.0f, 4.0f, 20.0f, 2.0f);
      }
    }

    // ---- the take being recorded grows in red
    if (m.recording && m.recTrack >= 0 && m.recTrack < kTracks) {
      const double cs = m.punchOn ? m.punchIn : 0.0;
      const double ce = std::max(cs, m.position);
      if (ce > cs) {
        const float x0 = xOf(cs), x1 = xOf(ce);
        const juce::Rectangle<float> r(x0, static_cast<float>(top() + m.recTrack * laneH_ + 4), std::max(3.0f, x1 - x0),
                                       static_cast<float>(laneH_ - 8));
        g.setColour(kPunchColour.withAlpha(0.55f));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colours::white);
        g.setFont(Fonts::sans(11.0f, true));
        g.drawText("Recording", r.reduced(6.0f, 2.0f), juce::Justification::topLeft, true);
      }
    }

    if (!any && !m.recording) {
      g.setColour(kGrey);
      g.setFont(Fonts::sans(15.0f, false));
      g.drawText("Select a track and press Record, or open its settings (...) to import a song.",
                 juce::Rectangle<int>(0, top(), getWidth(), laneH_ * kTracks), juce::Justification::centred, true);
    }

    // ---- playhead
    const float px = xOf(std::max(0.0, m.position));
    if (px >= 0.0f && px <= w) {
      g.setColour(juce::Colours::white);
      g.drawVerticalLine(static_cast<int>(px), 0.0f, static_cast<float>(h));
      juce::Path tri;
      tri.addTriangle(px - 7.0f, 0.0f, px + 7.0f, 0.0f, px, 10.0f);
      g.fillPath(tri);
    }
  }

  // ------------------------------------------------------------- gestures

  void mouseDown(const juce::MouseEvent& e) override {
    trackTouch(e);
    if (touches_.size() >= 2) {
      startPinch();
      return;
    }
    if (mode_ == Mode::pinch) return;
    downX_ = e.position.x;
    downTime_ = tOf(downX_);
    downScroll_ = scrollSec_;
    const float y = e.position.y;

    if (y < kRulerH) {
      mode_ = Mode::scrub;
      owner.command("seekSec", std::max(0.0, downTime_));
      return;
    }
    if (y < top()) {
      beginBand(y < kRulerH + kMarkH * 0.5f, downX_);
      repaint();
      return;
    }
    const int t = laneAt(y);
    if (t < 0) {
      mode_ = Mode::scroll;
      return;
    }
    if (t != owner.selected_) owner.selectTrack(t);
    const Mode hit = regionHitMode(t, downX_);
    if (e.getNumberOfClicks() >= 2 && hit != Mode::scroll) {
      mode_ = Mode::none;
      owner.openTrackPanel(t);
      return;
    }
    mode_ = hit;
    if (mode_ == Mode::move || mode_ == Mode::trimL || mode_ == Mode::trimR) {
      const auto& tm = owner.model_.tracks[t];
      editTrack_ = t;
      origStart_ = prevStart_ = tm.start;
      origTrimStart_ = prevTrimStart_ = tm.trimStart;
      origTrimEnd_ = prevTrimEnd_ = tm.trimEnd;
      preview_ = true;
    }
  }

  void mouseDrag(const juce::MouseEvent& e) override {
    trackTouch(e);
    if (mode_ == Mode::pinch) {
      updatePinch();
      return;
    }
    const double dt = (e.position.x - downX_) / pxPerSec_;
    switch (mode_) {
      case Mode::scrub:
        owner.command("seekSec", std::max(0.0, tOf(e.position.x)));
        break;
      case Mode::scroll:
        scrollSec_ = std::max(0.0, downScroll_ - dt);
        break;
      case Mode::move: {
        const auto& tm = owner.model_.tracks[editTrack_];
        const double nudge = tm.nudge / 1000.0;
        const double edge = snapT(std::max(0.0, origStart_ + dt) - nudge);
        prevStart_ = std::max(0.0, edge + nudge);
        break;
      }
      case Mode::trimL: {
        const auto& tm = owner.model_.tracks[editTrack_];
        const double nudge = tm.nudge / 1000.0;
        const double endEff = origTrimEnd_ > 0.0 ? std::min(origTrimEnd_, tm.fileSec) : tm.fileSec;
        const double lo = std::max(0.0, origTrimStart_ - origStart_);  // the start cannot go below zero
        const double hi = std::max(lo, endEff - 0.05);
        double nt = juce::jlimit(lo, hi, origTrimStart_ + dt);
        const double edge = snapT(origStart_ + (nt - origTrimStart_) - nudge);
        nt = juce::jlimit(lo, hi, origTrimStart_ + (edge + nudge - origStart_));
        prevTrimStart_ = nt;
        prevStart_ = std::max(0.0, origStart_ + (nt - origTrimStart_));
        break;
      }
      case Mode::trimR: {
        const auto& tm = owner.model_.tracks[editTrack_];
        const double nudge = tm.nudge / 1000.0;
        const double endEff = origTrimEnd_ > 0.0 ? std::min(origTrimEnd_, tm.fileSec) : tm.fileSec;
        const double lo = origTrimStart_ + 0.05;
        const double hi = std::max(lo, tm.fileSec);
        double ne = juce::jlimit(lo, hi, endEff + dt);
        const double edge = snapT((origStart_ - nudge) + (ne - origTrimStart_));
        ne = juce::jlimit(lo, hi, edge - (origStart_ - nudge) + origTrimStart_);
        prevTrimEnd_ = ne >= tm.fileSec - 0.001 ? 0.0 : ne;
        break;
      }
      case Mode::bandNew: {
        const double a = snapT(std::max(0.0, downTime_));
        const double b = snapT(std::max(0.0, tOf(e.position.x)));
        bandA_ = std::min(a, b);
        bandB_ = std::max(a, b);
        break;
      }
      case Mode::bandIn:
        bandA_ = juce::jlimit(0.0, std::max(0.0, origB_ - 0.05), snapT(origA_ + dt));
        break;
      case Mode::bandOut:
        bandB_ = std::max(origA_ + 0.05, snapT(origB_ + dt));
        break;
      case Mode::bandMove: {
        const double len = origB_ - origA_;
        bandA_ = std::max(0.0, snapT(origA_ + dt));
        bandB_ = bandA_ + len;
        break;
      }
      case Mode::none:
      case Mode::pinch:
        break;
    }
    repaint();
  }

  void mouseUp(const juce::MouseEvent& e) override {
    untrackTouch(e);
    if (mode_ == Mode::pinch) {
      if (touches_.empty()) mode_ = Mode::none;
      return;
    }
    const bool dragged = e.mouseWasDraggedSinceMouseDown();
    switch (mode_) {
      case Mode::move:
      case Mode::trimL:
      case Mode::trimR:
        if (dragged && editTrack_ >= 0) owner.commitRegion(editTrack_, prevStart_, prevTrimStart_, prevTrimEnd_);
        break;
      case Mode::bandNew:
      case Mode::bandIn:
      case Mode::bandOut:
      case Mode::bandMove:
        if (dragged) {
          if (bandB_ - bandA_ > 0.05) owner.commitBand(bandLoop_, bandA_, bandB_);
        } else if (mode_ == Mode::bandMove) {
          // A tap on an existing band switches it on or off.
          const bool on = bandLoop_ ? owner.model_.loopOn : owner.model_.punchOn;
          owner.command(bandLoop_ ? "loop" : "punch", !on);
        }
        break;
      case Mode::scroll:
        // A tap on an empty spot of a lane moves the playhead there.
        if (!dragged && laneAt(e.position.y) >= 0) owner.command("seekSec", std::max(0.0, tOf(e.position.x)));
        break;
      case Mode::scrub:
      case Mode::none:
      case Mode::pinch:
        break;
    }
    preview_ = false;
    bandPreview_ = false;
    editTrack_ = -1;
    mode_ = Mode::none;
    repaint();
  }

  void mouseMagnify(const juce::MouseEvent& e, float scale) override { zoomAround(e.position.x, scale); }

  void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
    const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    scrollSec_ = std::max(0.0, scrollSec_ - static_cast<double>(delta) * 300.0 / pxPerSec_);
    repaint();
  }

private:
  enum class Mode { none, scrub, scroll, move, trimL, trimR, bandNew, bandIn, bandOut, bandMove, pinch };
  struct Clip {
    double t0 = 0.0, len = 0.0, trimStart = 0.0;
  };
  struct Touch {
    int index;
    juce::Point<float> pos;
  };

  static int top() { return kRulerH + kMarkH; }
  float xOf(double t) const { return static_cast<float>((t - scrollSec_) * pxPerSec_); }
  double tOf(float x) const { return scrollSec_ + static_cast<double>(x) / pxPerSec_; }
  double snapT(double t) const {
    if (!owner.snap_) return t;
    const double b = owner.beatSeconds();
    return b > 0.0 ? std::round(t / b) * b : t;
  }
  int laneAt(float y) const {
    if (y < top()) return -1;
    const int t = static_cast<int>((y - top()) / static_cast<float>(laneH_));
    return t >= 0 && t < kTracks ? t : -1;
  }

  Clip clipOf(int t, bool usePreview) const {
    const auto& tm = owner.model_.tracks[t];
    double start = tm.start, ts = tm.trimStart, te = tm.trimEnd;
    if (usePreview && preview_ && editTrack_ == t) {
      start = prevStart_;
      ts = prevTrimStart_;
      te = prevTrimEnd_;
    }
    const double endEff = te > 0.0 ? std::min(te, tm.fileSec) : tm.fileSec;
    Clip c;
    c.t0 = start - tm.nudge / 1000.0;
    c.len = std::max(0.0, endEff - ts);
    c.trimStart = ts;
    return c;
  }

  Mode regionHitMode(int t, float x) const {
    if (!owner.model_.tracks[t].loaded) return Mode::scroll;
    const Clip c = clipOf(t, false);
    const float x0 = xOf(c.t0), x1 = xOf(c.t0 + c.len);
    if (x < x0 - 6.0f || x > x1 + 6.0f) return Mode::scroll;
    const float edge = std::min(20.0f, (x1 - x0) / 3.0f);
    if (x <= x0 + edge) return Mode::trimL;
    if (x >= x1 - edge) return Mode::trimR;
    return Mode::move;
  }

  void drawBand(juce::Graphics& g, double a, double b, bool loopBand, bool on) const {
    if (b <= a) return;
    const float x0 = xOf(a), x1 = xOf(b);
    const float y0 = loopBand ? kRulerH + 2.0f : kRulerH + kMarkH * 0.5f + 1.0f;
    const juce::Colour c = loopBand ? kLoopColour : kPunchColour;
    g.setColour(c.withAlpha(on ? 0.95f : 0.3f));
    g.fillRoundedRectangle(x0, y0, std::max(2.0f, x1 - x0), kMarkH * 0.5f - 3.0f, 3.0f);
  }

  void beginBand(bool loopHalf, float x) {
    const auto& m = owner.model_;
    bandLoop_ = loopHalf;
    origA_ = bandA_ = loopHalf ? m.loopIn : m.punchIn;
    origB_ = bandB_ = loopHalf ? m.loopOut : m.punchOut;
    const bool exists = origB_ > origA_;
    const float xa = xOf(origA_), xb = xOf(origB_);
    if (exists && std::abs(x - xa) <= 18.0f)
      mode_ = Mode::bandIn;
    else if (exists && std::abs(x - xb) <= 18.0f)
      mode_ = Mode::bandOut;
    else if (exists && x > xa && x < xb)
      mode_ = Mode::bandMove;
    else {
      mode_ = Mode::bandNew;
      bandA_ = bandB_ = snapT(std::max(0.0, tOf(x)));
    }
    bandPreview_ = true;
  }

  void zoomAround(float x, double factor) {
    const double anchor = tOf(x);
    pxPerSec_ = juce::jlimit(kMinPxPerSec, kMaxPxPerSec, pxPerSec_ * factor);
    scrollSec_ = std::max(0.0, anchor - static_cast<double>(x) / pxPerSec_);
    fitted_ = true;
    repaint();
  }

  void trackTouch(const juce::MouseEvent& e) {
    const int index = e.source.getIndex();
    for (auto& t : touches_)
      if (t.index == index) {
        t.pos = e.position;
        return;
      }
    touches_.push_back({index, e.position});
  }
  void untrackTouch(const juce::MouseEvent& e) {
    const int index = e.source.getIndex();
    touches_.erase(std::remove_if(touches_.begin(), touches_.end(), [index](const Touch& t) { return t.index == index; }),
                   touches_.end());
  }

  void startPinch() {
    preview_ = false;
    bandPreview_ = false;
    editTrack_ = -1;
    mode_ = Mode::pinch;
    const auto p0 = touches_[0].pos, p1 = touches_[1].pos;
    pinchStartDist_ = std::max(10.0f, p0.getDistanceFrom(p1));
    pinchStartPx_ = pxPerSec_;
    pinchAnchor_ = tOf((p0.x + p1.x) * 0.5f);
    fitted_ = true;
  }
  void updatePinch() {
    if (touches_.size() < 2) return;
    const auto p0 = touches_[0].pos, p1 = touches_[1].pos;
    const float d = std::max(10.0f, p0.getDistanceFrom(p1));
    pxPerSec_ = juce::jlimit(kMinPxPerSec, kMaxPxPerSec, pinchStartPx_ * static_cast<double>(d) / pinchStartDist_);
    const float mid = (p0.x + p1.x) * 0.5f;
    scrollSec_ = std::max(0.0, pinchAnchor_ - static_cast<double>(mid) / pxPerSec_);
    repaint();
  }

  MultitrackView& owner;
  double pxPerSec_ = 20.0, scrollSec_ = 0.0;
  bool fitted_ = false;
  int laneH_ = 64;

  Mode mode_ = Mode::none;
  float downX_ = 0.0f;
  double downTime_ = 0.0, downScroll_ = 0.0;
  int editTrack_ = -1;
  double origStart_ = 0.0, origTrimStart_ = 0.0, origTrimEnd_ = 0.0;
  double prevStart_ = 0.0, prevTrimStart_ = 0.0, prevTrimEnd_ = 0.0;
  bool preview_ = false;
  bool bandLoop_ = true, bandPreview_ = false;
  double origA_ = 0.0, origB_ = 0.0, bandA_ = 0.0, bandB_ = 0.0;
  std::vector<Touch> touches_;
  double pinchStartDist_ = 1.0, pinchStartPx_ = 20.0, pinchAnchor_ = 0.0;
};

// ==========================================================================
// Floating panels and the dim layer that closes them.
class MultitrackView::Scrim : public juce::Component {
public:
  explicit Scrim(MultitrackView& o) : owner(o) {}
  void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black.withAlpha(0.35f)); }
  void mouseDown(const juce::MouseEvent&) override { owner.closePanel(); }

private:
  MultitrackView& owner;
};

class MultitrackView::Panel : public juce::Component {
public:
  Panel(MultitrackView& o, const juce::String& title) : owner(o), title_(title) {}
  virtual void refresh() {}

  void paint(juce::Graphics& g) override {
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff2c2c2e));
    g.fillRoundedRectangle(b, 14.0f);
    g.setColour(juce::Colour(0xff48484a));
    g.drawRoundedRectangle(b.reduced(0.5f), 14.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.setFont(Fonts::sans(16.0f, true));
    g.drawText(title_, kPadX, 0, getWidth() - 2 * kPadX, kTitleH, juce::Justification::centredLeft, true);
  }

protected:
  static constexpr int kTitleH = 44, kRowH = 50, kPadX = 18, kCaptionW = 96;

  // Next content row below the title.
  juce::Rectangle<int> row(int index) const {
    return juce::Rectangle<int>(kPadX, kTitleH + index * kRowH, getWidth() - 2 * kPadX, kRowH).reduced(0, 5);
  }

  MultitrackView& owner;
  juce::String title_;
  bool syncing_ = false;
};

class MultitrackView::SongPanel : public MultitrackView::Panel {
public:
  static constexpr int kHeight = 44 + 5 * 50 + 12;

  explicit SongPanel(MultitrackView& o) : Panel(o, "Song") {
    styleCaption(tempoCap_, "Tempo");
    styleSlider(tempo_, 30.0, 300.0, 1.0, 120.0, juce::Slider::TextBoxLeft, 86, " BPM", 0);
    tempo_.onValueChange = [this] {
      if (!syncing_ && throttle_.ready()) owner.command("bpm", tempo_.getValue());
    };
    tempo_.onDragEnd = [this] { owner.command("bpm", tempo_.getValue()); };
    styleTextButton(tap_, "Tap");
    tap_.onClick = [this] { owner.tapTempo(); };

    styleCaption(sigCap_, "Time");
    for (int i = 0; i < 6; ++i) {
      styleTextButton(sig_[i], kSigs[i]);
      sig_[i].onClick = [this, i] { owner.command("sig", juce::String(kSigs[i])); };
    }

    styleCaption(countCap_, "Count-in");
    const char* const counts[3] = {"Off", "1 bar", "2 bars"};
    for (int i = 0; i < 3; ++i) {
      styleTextButton(count_[i], counts[i]);
      count_[i].onClick = [this, i] { owner.command("countIn", i); };
    }

    styleCaption(clickCap_, "Click");
    styleSlider(click_, 0.0, 1.0, 0.01, 0.5);
    click_.onValueChange = [this] {
      if (!syncing_ && clickThrottle_.ready()) owner.command("clickVol", click_.getValue());
    };
    click_.onDragEnd = [this] { owner.command("clickVol", click_.getValue()); };

    styleCaption(snapCap_, "Grid");
    styleTextButton(snap_, "Snap to beats");
    snap_.onClick = [this] {
      owner.snap_ = !owner.snap_;
      refresh();
    };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&tempoCap_, &tempo_, &tap_, &sigCap_, &countCap_,
                                                                      &clickCap_, &click_, &snapCap_, &snap_})
      addAndMakeVisible(c);
    for (auto& b : sig_)
      addAndMakeVisible(b);
    for (auto& b : count_)
      addAndMakeVisible(b);
    refresh();
  }

  void refresh() override {
    const auto& m = owner.model_;
    syncing_ = true;
    if (!tempo_.isMouseButtonDown()) tempo_.setValue(m.bpm, juce::dontSendNotification);
    const juce::String sig = juce::String(m.beats) + "/" + juce::String(m.denom);
    for (int i = 0; i < 6; ++i)
      sig_[i].setToggleState(sig == kSigs[i], juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
      count_[i].setToggleState(m.countIn == i, juce::dontSendNotification);
    if (!click_.isMouseButtonDown()) click_.setValue(m.clickVol, juce::dontSendNotification);
    snap_.setToggleState(owner.snap_, juce::dontSendNotification);
    syncing_ = false;
  }

  void resized() override {
    auto r = row(0);
    tempoCap_.setBounds(r.removeFromLeft(kCaptionW));
    tap_.setBounds(r.removeFromRight(70));
    r.removeFromRight(8);
    tempo_.setBounds(r);

    r = row(1);
    sigCap_.setBounds(r.removeFromLeft(kCaptionW));
    const int sigW = (r.getWidth() - 5 * 4) / 6;
    for (auto& b : sig_) {
      b.setBounds(r.removeFromLeft(sigW));
      r.removeFromLeft(4);
    }

    r = row(2);
    countCap_.setBounds(r.removeFromLeft(kCaptionW));
    const int countW = (r.getWidth() - 2 * 4) / 3;
    for (auto& b : count_) {
      b.setBounds(r.removeFromLeft(countW));
      r.removeFromLeft(4);
    }

    r = row(3);
    clickCap_.setBounds(r.removeFromLeft(kCaptionW));
    click_.setBounds(r);

    r = row(4);
    snapCap_.setBounds(r.removeFromLeft(kCaptionW));
    snap_.setBounds(r.removeFromLeft(180));
  }

private:
  juce::Label tempoCap_, sigCap_, countCap_, clickCap_, snapCap_;
  juce::Slider tempo_, click_;
  juce::TextButton tap_, snap_;
  juce::TextButton sig_[6];
  juce::TextButton count_[3];
  Throttle throttle_, clickThrottle_;
};

class MultitrackView::TrackPanel : public MultitrackView::Panel {
public:
  static constexpr int kHeight = 44 + 4 * 50 + 12;

  TrackPanel(MultitrackView& o, int track) : Panel(o, "Track " + juce::String(track + 1)), track_(track) {
    styleCaption(volumeCap_, "Volume");
    styleSlider(volume_, 0.0, 1.5, 0.01, 1.0, juce::Slider::TextBoxRight, 56, "", 2);
    bind(volume_, "volume", volumeThrottle_);
    styleCaption(panCap_, "Pan");
    styleSlider(pan_, -1.0, 1.0, 0.01, 0.0, juce::Slider::TextBoxRight, 56, "", 2);
    bind(pan_, "pan", panThrottle_);
    styleCaption(nudgeCap_, "Latency");
    styleSlider(nudge_, -300.0, 300.0, 1.0, 0.0, juce::Slider::TextBoxRight, 76, " ms", 0);
    bind(nudge_, "nudge", nudgeThrottle_);

    styleTextButton(import_, "Import audio...");
    import_.onClick = [this] {
      owner.startImport(track_);
      owner.closePanelSoon();
    };
    styleTextButton(delete_, "Delete region");
    delete_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff5a1f1c));
    delete_.onClick = [this] {
      owner.trackCommand("clear", track_, {});
      owner.closePanelSoon();
    };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&volumeCap_, &volume_, &panCap_, &pan_, &nudgeCap_,
                                                                      &nudge_, &import_, &delete_})
      addAndMakeVisible(c);
    refresh();
  }

  void refresh() override {
    const auto& tm = owner.model_.tracks[track_];
    syncing_ = true;
    if (!volume_.isMouseButtonDown()) volume_.setValue(tm.volume, juce::dontSendNotification);
    if (!pan_.isMouseButtonDown()) pan_.setValue(tm.pan, juce::dontSendNotification);
    if (!nudge_.isMouseButtonDown()) nudge_.setValue(tm.nudge, juce::dontSendNotification);
    delete_.setEnabled(tm.loaded && !owner.model_.recording);
    import_.setEnabled(!owner.model_.recording);
    syncing_ = false;
  }

  void resized() override {
    auto r = row(0);
    volumeCap_.setBounds(r.removeFromLeft(kCaptionW));
    volume_.setBounds(r);
    r = row(1);
    panCap_.setBounds(r.removeFromLeft(kCaptionW));
    pan_.setBounds(r);
    r = row(2);
    nudgeCap_.setBounds(r.removeFromLeft(kCaptionW));
    nudge_.setBounds(r);
    r = row(3);
    const int w = (r.getWidth() - 10) / 2;
    import_.setBounds(r.removeFromLeft(w));
    r.removeFromLeft(10);
    delete_.setBounds(r);
  }

private:
  void bind(juce::Slider& s, const char* cmd, Throttle& throttle) {
    juce::Slider* slider = &s;
    Throttle* th = &throttle;
    const juce::String name(cmd);
    s.onValueChange = [this, slider, th, name] {
      if (!syncing_ && th->ready()) owner.trackCommand(name, track_, slider->getValue());
    };
    s.onDragEnd = [this, slider, name] { owner.trackCommand(name, track_, slider->getValue()); };
  }

  int track_;
  juce::Label volumeCap_, panCap_, nudgeCap_;
  juce::Slider volume_, pan_, nudge_;
  juce::TextButton import_, delete_;
  Throttle volumeThrottle_, panThrottle_, nudgeThrottle_;
};

class MultitrackView::MixPanel : public MultitrackView::Panel {
public:
  static constexpr int kHeight = 44 + 3 * 50 + 12;

  explicit MixPanel(MultitrackView& o) : Panel(o, "Mix") {
    styleTextButton(wav_, "Mix down to WAV");
    wav_.onClick = [this] {
      owner.command("mix", false);
      owner.closePanelSoon();
    };
    styleTextButton(mp3_, "Mix down to MP3");
    mp3_.onClick = [this] {
      owner.command("mix", true);
      owner.closePanelSoon();
    };
    styleTextButton(share_, "Share last mix");
    share_.onClick = [this] {
      if (owner.model_.mixPath.isNotEmpty()) owner.shareFile(juce::File(owner.model_.mixPath));
      owner.closePanelSoon();
    };
    addAndMakeVisible(wav_);
    addAndMakeVisible(mp3_);
    addAndMakeVisible(share_);
    refresh();
  }

  void refresh() override {
    const bool canMix = owner.model_.length > 0.0 && !owner.model_.recording;
    wav_.setEnabled(canMix);
    mp3_.setEnabled(canMix);
    share_.setEnabled(owner.model_.mixPath.isNotEmpty());
  }

  void resized() override {
    wav_.setBounds(row(0));
    mp3_.setBounds(row(1));
    share_.setBounds(row(2));
  }

private:
  juce::TextButton wav_, mp3_, share_;
};

// ==========================================================================

MultitrackView::MultitrackView(Services& services) : services_(services) {
  setOpaque(true);
  using G = GlyphButton::Glyph;

  // Navigation is deferred: it destroys this view, whose button is still in
  // its click callback.
  juce::Component::SafePointer<MultitrackView> self(this);
  amp_ = std::make_unique<GlyphButton>(G::none, "< Amp");
  amp_->onClick = [self] {
    juce::MessageManager::callAsync([self] {
      if (self != nullptr && self->onClose) self->onClose();
    });
  };
  takes_ = std::make_unique<GlyphButton>(G::none, "Takes");
  takes_->onClick = [self] {
    juce::MessageManager::callAsync([self] {
      if (self != nullptr && self->onBack) self->onBack();
    });
  };

  rewind_ = std::make_unique<GlyphButton>(G::rewind);
  rewind_->onClick = [this] { command("rewind"); };
  play_ = std::make_unique<GlyphButton>(G::play);
  play_->onClick = [this] { togglePlay(); };
  record_ = std::make_unique<GlyphButton>(G::record);
  record_->activeColour = kPunchColour;
  record_->onClick = [this] { toggleRecord(); };
  loop_ = std::make_unique<GlyphButton>(G::loop);
  loop_->activeColour = kLoopColour;
  loop_->onClick = [this] { command("loop", !model_.loopOn); };
  metro_ = std::make_unique<GlyphButton>(G::metronome);
  metro_->onClick = [this] { command("metro", !model_.metro); };
  countIn_ = std::make_unique<GlyphButton>(G::countIn);
  countIn_->onClick = [this] { command("countIn", (model_.countIn + 1) % 3); };
  zoomOut_ = std::make_unique<GlyphButton>(G::zoomOut);
  zoomOut_->onClick = [this] { timeline_->zoomBy(1.0 / 1.5); };
  zoomIn_ = std::make_unique<GlyphButton>(G::zoomIn);
  zoomIn_->onClick = [this] { timeline_->zoomBy(1.5); };
  mix_ = std::make_unique<GlyphButton>(G::none, "Mix");
  mix_->onClick = [this] { openMixPanel(); };

  lcd_ = std::make_unique<Lcd>(*this);
  lcd_->onTap = [this] { openSongPanel(); };
  headers_ = std::make_unique<Headers>(*this);
  timeline_ = std::make_unique<Timeline>(*this);
  scrim_ = std::make_unique<Scrim>(*this);

  for (GlyphButton* b : {amp_.get(), takes_.get(), rewind_.get(), play_.get(), record_.get(), loop_.get(), metro_.get(),
                         countIn_.get(), zoomOut_.get(), zoomIn_.get(), mix_.get()})
    addAndMakeVisible(b);
  addAndMakeVisible(*lcd_);
  addAndMakeVisible(*headers_);
  addAndMakeVisible(*timeline_);
  addChildComponent(*scrim_);

  apply(services_.backend.getMultitrackState());
  startTimerHz(kPollHz);
}

MultitrackView::~MultitrackView() {
  stopTimer();
  panel_.reset();
}

// ------------------------------------------------------------------ commands

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
    if (model_.tracks[t].needPeaks) trackCommand("peaks", t, {});
}

void MultitrackView::selectTrack(int track) {
  selected_ = juce::jlimit(0, kTracks - 1, track);
  headers_->repaint();
  timeline_->repaint();
}

void MultitrackView::togglePlay() {
  if (model_.recording)
    command("stop");
  else
    command(model_.playing ? "stop" : "play");
}

void MultitrackView::toggleRecord() {
  if (model_.recording)
    command("stop");
  else
    trackCommand("record", selected_, {});
}

void MultitrackView::commitRegion(int track, double start, double trimStart, double trimEnd) {
  auto& tm = model_.tracks[track];
  tm.start = start;
  tm.trimStart = trimStart;
  tm.trimEnd = trimEnd;
  auto* o = new juce::DynamicObject();
  o->setProperty("start", start);
  o->setProperty("trimStart", trimStart);
  o->setProperty("trimEnd", trimEnd);
  trackCommand("region", track, juce::var(o));
}

void MultitrackView::commitBand(bool loop, double in, double out) {
  if (loop) {
    model_.loopIn = in;
    model_.loopOut = out;
    model_.loopOn = true;
  } else {
    model_.punchIn = in;
    model_.punchOut = out;
    model_.punchOn = true;
  }
  auto* o = new juce::DynamicObject();
  o->setProperty("in", in);
  o->setProperty("out", out);
  o->setProperty("on", true);
  command(loop ? "loopRange" : "punchRange", juce::var(o));
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

void MultitrackView::showLocalError(const juce::String& text) {
  localError_ = text;
  localErrorUntil_ = juce::Time::getMillisecondCounter() + 4000;
  lcd_->repaint();
}

// -------------------------------------------------------------------- import

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
  if (!dir.createDirectory().wasOk()) {
    showLocalError("Could not create the imports folder.");
    return;
  }
  juce::String name = juce::File::createLegalFileName(url.getFileName());
  if (name.isEmpty()) name = "Imported audio";
  const juce::File dest = dir.getChildFile(name).getNonexistentSibling();
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
    showLocalError("Could not read the selected file.");
    return;
  }
  selectTrack(track);
  trackCommand("import", track, "Imports/" + dest.getFileName());
}

// -------------------------------------------------------------------- panels

void MultitrackView::showPanel(std::unique_ptr<Panel> panel, juce::Rectangle<int> bounds) {
  closePanel();
  scrim_->setBounds(getLocalBounds());
  scrim_->setVisible(true);
  scrim_->toFront(false);
  panel_ = std::move(panel);
  addAndMakeVisible(*panel_);
  panel_->setBounds(bounds.constrainedWithin(getLocalBounds().reduced(8)));
  panel_->toFront(false);
}

void MultitrackView::closePanel() {
  if (panel_ != nullptr) {
    removeChildComponent(panel_.get());
    panel_.reset();
  }
  scrim_->setVisible(false);
}

void MultitrackView::closePanelSoon() {
  juce::Component::SafePointer<MultitrackView> self(this);
  juce::MessageManager::callAsync([self] {
    if (self != nullptr) self->closePanel();
  });
}

void MultitrackView::openSongPanel() {
  const auto lcd = lcd_->getBounds();
  showPanel(std::make_unique<SongPanel>(*this),
            juce::Rectangle<int>(lcd.getCentreX() - 240, lcd.getBottom() + 8, 480, SongPanel::kHeight));
}

void MultitrackView::openTrackPanel(int track) {
  selectTrack(track);
  const int laneH = timeline_->laneHeight();
  const int y = timeline_->getY() + kRulerH + kMarkH + track * laneH;
  showPanel(std::make_unique<TrackPanel>(*this, track),
            juce::Rectangle<int>(kHeaderW + 10, y, 460, TrackPanel::kHeight));
}

void MultitrackView::openMixPanel() {
  const auto b = mix_->getBounds();
  showPanel(std::make_unique<MixPanel>(*this),
            juce::Rectangle<int>(b.getRight() - 300, b.getBottom() + 8, 300, MixPanel::kHeight));
}

// --------------------------------------------------------------------- state

double MultitrackView::beatSeconds() const {
  return 60.0 / std::max(1.0, model_.bpm) * 4.0 / static_cast<double>(std::max(1, model_.denom));
}

juce::String MultitrackView::statusText(juce::Colour& colour) const {
  const juce::uint32 now = juce::Time::getMillisecondCounter();
  colour = kGrey;
  if (localError_.isNotEmpty() && now < localErrorUntil_) {
    colour = kPunchColour;
    return localError_;
  }
  if (model_.error.isNotEmpty()) {
    colour = kPunchColour;
    return model_.error;
  }
  if (model_.recording && model_.position < 0.0) {
    colour = kPunchColour;
    return "COUNT-IN";
  }
  if (model_.recording) {
    colour = kPunchColour;
    return "REC  Track " + juce::String(model_.recTrack + 1);
  }
  if (model_.message.isNotEmpty() && now < messageUntil_) {
    colour = juce::Colours::white;
    return model_.message;
  }
  const bool whole = std::abs(model_.bpm - std::round(model_.bpm)) < 0.05;
  return juce::String(model_.bpm, whole ? 0 : 1) + " BPM    " + juce::String(model_.beats) + "/" +
         juce::String(model_.denom) + (snap_ ? "    Snap" : "");
}

void MultitrackView::apply(const juce::var& state) {
  if (!state.isObject()) return;
  auto num = [&](const char* key, double def) { return static_cast<double>(state.getProperty(key, def)); };
  auto flag = [&](const char* key) { return static_cast<bool>(state.getProperty(key, false)); };

  auto& m = model_;
  m.playing = flag("playing");
  m.recording = flag("recording");
  m.recTrack = static_cast<int>(num("recordingTrack", -1));
  m.position = num("position", 0.0);
  m.length = num("length", 0.0);
  m.loopOn = flag("loop");
  m.loopIn = num("loopIn", 0.0);
  m.loopOut = num("loopOut", 0.0);
  m.punchOn = flag("punch");
  m.punchIn = num("punchIn", 0.0);
  m.punchOut = num("punchOut", 0.0);
  m.metro = flag("metro");
  m.bpm = num("bpm", 120.0);
  m.beats = static_cast<int>(num("beats", 4));
  m.denom = static_cast<int>(num("denom", 4));
  m.countIn = static_cast<int>(num("countIn", 0));
  m.clickVol = num("clickVol", 0.5);
  m.liveMeter = std::max(static_cast<float>(num("liveMeter", 0.0)), m.liveMeter * 0.82f);
  m.error = state.getProperty("error", {}).toString();
  m.message = state.getProperty("message", {}).toString();
  m.mixPath = state.getProperty("mixPath", {}).toString();
  m.mixSerial = static_cast<int>(num("mixSerial", 0));
  if (m.message != lastMessage_) {
    lastMessage_ = m.message;
    messageUntil_ = juce::Time::getMillisecondCounter() + 5000;
  }

  if (auto* arr = state.getProperty("tracks", {}).getArray()) {
    for (int t = 0; t < kTracks && t < arr->size(); ++t) {
      const juce::var e = (*arr)[t];
      auto& tm = m.tracks[t];
      tm.loaded = static_cast<bool>(e.getProperty("loaded", false));
      tm.imported = static_cast<bool>(e.getProperty("imported", false));
      tm.mute = static_cast<bool>(e.getProperty("mute", false));
      tm.solo = static_cast<bool>(e.getProperty("solo", false));
      tm.label = e.getProperty("label", {}).toString();
      tm.fileSec = static_cast<double>(e.getProperty("fileSeconds", 0.0));
      tm.start = static_cast<double>(e.getProperty("start", 0.0));
      tm.trimStart = static_cast<double>(e.getProperty("trimStart", 0.0));
      tm.trimEnd = static_cast<double>(e.getProperty("trimEnd", 0.0));
      tm.nudge = static_cast<double>(e.getProperty("nudge", 0.0));
      tm.pan = static_cast<double>(e.getProperty("pan", 0.0));
      tm.volume = static_cast<double>(e.getProperty("volume", 1.0));
      tm.meter = std::max(static_cast<float>(static_cast<double>(e.getProperty("meter", 0.0))), tm.meter * 0.82f);
      const int rev = static_cast<int>(e.getProperty("rev", 0));
      if (rev != tm.rev) {
        tm.rev = rev;
        tm.needPeaks = tm.loaded;
        if (!tm.loaded) tm.peaks.clear();
      }
    }
  }

  // A waveform overview delivered for one track.
  if (state.hasProperty("peaks")) {
    const int pt = static_cast<int>(num("peaksTrack", -1));
    if (pt >= 0 && pt < kTracks) {
      auto& tm = m.tracks[pt];
      tm.peaks.clear();
      if (auto* arr = state.getProperty("peaks", {}).getArray()) {
        tm.peaks.reserve(static_cast<size_t>(arr->size()));
        for (const auto& item : *arr)
          tm.peaks.push_back(static_cast<float>(static_cast<double>(item)));
      }
      tm.needPeaks = false;
    }
  }

  // Control bar.
  play_->setGlyph(m.playing && !m.recording ? GlyphButton::Glyph::stop : GlyphButton::Glyph::play);
  play_->setEnabled(!m.recording);
  record_->setActive(m.recording);
  loop_->setActive(m.loopOn);
  loop_->setEnabled(!m.recording);
  metro_->setActive(m.metro);
  countIn_->setActive(m.countIn > 0);
  countIn_->badge = m.countIn > 0 ? juce::String(m.countIn) : juce::String();
  countIn_->repaint();
  rewind_->setEnabled(!m.recording);
  takes_->setEnabled(!m.recording);
  mix_->setEnabled(!m.recording);

  headers_->refresh();
  timeline_->refresh();
  lcd_->repaint();
  if (panel_ != nullptr) panel_->refresh();

  // A finished mixdown opens the share sheet straight away (the first poll
  // after opening the screen only sets the baseline).
  if (lastSerial_ < 0) {
    lastSerial_ = m.mixSerial;
  } else if (m.mixSerial != lastSerial_) {
    lastSerial_ = m.mixSerial;
    if (m.mixPath.isNotEmpty()) shareFile(juce::File(m.mixPath));
  }
}

// -------------------------------------------------------------------- layout

void MultitrackView::paint(juce::Graphics& g) {
  g.fillAll(kBackground);
  g.setColour(kBarColour);
  g.fillRect(0, 0, getWidth(), kBarH);
  g.setColour(juce::Colour(0xff2c2c2e));
  g.drawHorizontalLine(kBarH - 1, 0.0f, static_cast<float>(getWidth()));
}

void MultitrackView::resized() {
  auto area = getLocalBounds();
  scrim_->setBounds(area);

  auto bar = area.removeFromTop(kBarH).reduced(14, 10);
  amp_->setBounds(bar.removeFromLeft(88));
  bar.removeFromLeft(8);
  takes_->setBounds(bar.removeFromLeft(80));
  mix_->setBounds(bar.removeFromRight(76));
  bar.removeFromRight(10);
  zoomIn_->setBounds(bar.removeFromRight(48));
  bar.removeFromRight(6);
  zoomOut_->setBounds(bar.removeFromRight(48));

  constexpr int kButtonW = 54, kGap = 8, kLcdW = 250;
  const int groupW = 6 * kButtonW + 5 * kGap + 16 + kLcdW;
  auto group = bar.withSizeKeepingCentre(std::min(groupW, bar.getWidth()), bar.getHeight());
  for (GlyphButton* b : {rewind_.get(), play_.get(), record_.get(), loop_.get(), metro_.get(), countIn_.get()}) {
    b->setBounds(group.removeFromLeft(kButtonW));
    group.removeFromLeft(kGap);
  }
  group.removeFromLeft(16 - kGap);
  lcd_->setBounds(group.withY(bar.getY() - 4).withHeight(bar.getHeight() + 8));

  headers_->setBounds(area.removeFromLeft(kHeaderW));
  timeline_->setBounds(area);
  if (panel_ != nullptr) closePanel();
}

}  // namespace t3k::ui
