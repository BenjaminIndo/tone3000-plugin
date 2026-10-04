#include "PedalsView.h"

#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPad = 24;
constexpr int kPanelGap = 16;
constexpr int kPanelHeader = 52;
constexpr int kKnobCellH = 140;
constexpr int kPollHz = 10;
}  // namespace

// A rotary knob with a caption, bound to one parameter.
class PedalsView::Knob : public juce::Component {
public:
  Knob(Backend& backend, const char* paramId, const juce::String& name, const juce::String& suffix, int decimals)
      : param_(backend.parameter(paramId)) {
    caption_.setText(name, juce::dontSendNotification);
    caption_.setFont(Fonts::sans(12.0f, true));
    caption_.setColour(juce::Label::textColourId, theme::kGray);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(caption_);

    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 18);
    slider_.setTextValueSuffix(suffix);
    slider_.setNumDecimalPlacesToDisplay(decimals);
    slider_.setColour(juce::Slider::rotarySliderFillColourId, theme::kBrandBlue);
    slider_.setColour(juce::Slider::rotarySliderOutlineColourId, theme::kSurfaceRaised);
    slider_.setColour(juce::Slider::thumbColourId, theme::kWhite);
    slider_.setColour(juce::Slider::textBoxTextColourId, theme::kWhite);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setMouseClickGrabsKeyboardFocus(false);
    if (param_ != nullptr) {
      const auto range = param_->getNormalisableRange();
      slider_.setNormalisableRange(juce::NormalisableRange<double>(range.start, range.end, 0.0, range.skew));
      slider_.setDoubleClickReturnValue(true, param_->convertFrom0to1(param_->getDefaultValue()));
      slider_.onDragStart = [this] { param_->beginChangeGesture(); };
      slider_.onDragEnd = [this] { param_->endChangeGesture(); };
      slider_.onValueChange = [this] {
        if (!syncing_) param_->setValueNotifyingHost(param_->convertTo0to1(static_cast<float>(slider_.getValue())));
      };
      sync();
    } else {
      slider_.setEnabled(false);
    }
    addAndMakeVisible(slider_);
  }

  void sync() {
    if (param_ == nullptr || slider_.isMouseButtonDown()) return;
    syncing_ = true;
    slider_.setValue(param_->convertFrom0to1(param_->getValue()), juce::dontSendNotification);
    syncing_ = false;
  }

  void resized() override {
    auto area = getLocalBounds();
    caption_.setBounds(area.removeFromTop(18));
    slider_.setBounds(area);
  }

private:
  juce::RangedAudioParameter* param_;
  juce::Label caption_;
  juce::Slider slider_;
  bool syncing_ = false;
};

// An on/off stomp bound to a boolean parameter.
class PedalsView::Stomp : public juce::TextButton {
public:
  Stomp(Backend& backend, const char* paramId) : param_(backend.parameter(paramId)) {
    setClickingTogglesState(false);
    setMouseClickGrabsKeyboardFocus(false);
    setColour(juce::TextButton::buttonColourId, theme::kSurfaceRaised);
    setColour(juce::TextButton::buttonOnColourId, theme::kBrandBlue);
    setColour(juce::TextButton::textColourOffId, theme::kGray);
    setColour(juce::TextButton::textColourOnId, theme::kWhite);
    onClick = [this] {
      if (param_ == nullptr) return;
      const bool on = param_->getValue() < 0.5f;
      param_->beginChangeGesture();
      param_->setValueNotifyingHost(on ? 1.0f : 0.0f);
      param_->endChangeGesture();
      sync();
    };
    sync();
  }

  void sync() {
    const bool on = param_ != nullptr && param_->getValue() >= 0.5f;
    setToggleState(on, juce::dontSendNotification);
    setButtonText(on ? "ON" : "OFF");
  }

private:
  juce::RangedAudioParameter* param_;
};

PedalsView::PedalsView(Services& services) : services_(services) {
  setOpaque(true);
  Backend& b = services.backend;

  close_.setName("Close pedals");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(close_);

  title_.setText("PEDALS", juce::dontSendNotification);
  title_.setFont(Fonts::sans(20.0f, true));
  title_.setColour(juce::Label::textColourId, theme::kWhite);
  title_.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(title_);

  auto addPanel = [&](const char* title, const char* enabledId) -> Panel& {
    panels_.emplace_back();
    Panel& p = panels_.back();
    p.title = title;
    p.stomp = std::make_unique<Stomp>(b, enabledId);
    addAndMakeVisible(*p.stomp);
    return p;
  };
  auto addKnob = [&](Panel& p, const char* id, const char* name, const char* suffix, int decimals) {
    p.knobs.push_back(std::make_unique<Knob>(b, id, name, suffix, decimals));
    addAndMakeVisible(*p.knobs.back());
  };

  Panel& comp = addPanel("COMPRESSOR  (before amp)", "compEnabled");
  addKnob(comp, "compThreshold", "THRESHOLD", " dB", 0);
  addKnob(comp, "compRatio", "RATIO", ":1", 1);
  addKnob(comp, "compMakeup", "LEVEL", " dB", 1);
  addKnob(comp, "compAttack", "ATTACK", " ms", 0);
  addKnob(comp, "compRelease", "RELEASE", " ms", 0);
  addKnob(comp, "compMix", "BLEND", "", 2);

  Panel& delay = addPanel("DELAY  (after amp)", "delayEnabled");
  addKnob(delay, "delayTime", "TIME", " ms", 0);
  addKnob(delay, "delayFeedback", "REPEATS", "", 2);
  addKnob(delay, "delayMix", "MIX", "", 2);
  addKnob(delay, "delayTone", "TONE", " Hz", 0);

  Panel& reverb = addPanel("REVERB  (after amp)", "reverbEnabled");
  addKnob(reverb, "reverbSize", "SIZE", "", 2);
  addKnob(reverb, "reverbDamp", "DAMPING", "", 2);
  addKnob(reverb, "reverbMix", "MIX", "", 2);

  startTimerHz(kPollHz);
}

PedalsView::~PedalsView() { stopTimer(); }

void PedalsView::timerCallback() {
  for (auto& p : panels_) {
    p.stomp->sync();
    for (auto& k : p.knobs)
      k->sync();
  }
}

void PedalsView::paint(juce::Graphics& g) {
  g.fillAll(theme::kBlack);
  for (const auto& p : panels_) {
    g.setColour(theme::kSurfaceRaised);
    g.fillRoundedRectangle(p.bounds.toFloat(), 10.0f);
    g.setColour(theme::kWhite);
    g.setFont(Fonts::sans(13.0f, true));
    g.drawText(p.title, p.bounds.getX() + 16, p.bounds.getY(), p.bounds.getWidth() - 100, kPanelHeader,
               juce::Justification::centredLeft, true);
  }
}

void PedalsView::resized() {
  auto area = getLocalBounds();
  close_.setBounds(area.getRight() - kCloseRight - kCloseBox, area.getY() + kCloseTop, kCloseBox, kCloseBox);
  title_.setBounds(kPad, area.getY() + 14, 300, 32);

  auto inner = area.reduced(kPad, 0);
  inner.removeFromTop(54);
  const int count = static_cast<int>(panels_.size());
  const int panelW = (inner.getWidth() - kPanelGap * (count - 1)) / juce::jmax(1, count);
  for (auto& p : panels_) {
    auto box = inner.removeFromLeft(panelW).withHeight(inner.getHeight());
    inner.removeFromLeft(kPanelGap);
    p.bounds = box;
    auto top = box.removeFromTop(kPanelHeader);
    p.stomp->setBounds(top.removeFromRight(72).withSizeKeepingCentre(60, 32));

    const int perRow = 3;
    const int cellW = box.getWidth() / perRow;
    for (size_t i = 0; i < p.knobs.size(); ++i) {
      const int col = static_cast<int>(i) % perRow;
      const int row = static_cast<int>(i) / perRow;
      p.knobs[i]->setBounds(box.getX() + col * cellW, box.getY() + row * kKnobCellH, cellW, kKnobCellH);
    }
  }
  repaint();
}

}  // namespace t3k::ui
