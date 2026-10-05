#include "RecorderView.h"

#include "IosShare.h"
#include "core/Fonts.h"
#include "core/Theme.h"

namespace t3k::ui {

namespace {
constexpr int kPad = 24;
constexpr int kLeftW = 360;
constexpr int kGap = 12;
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

void styleLabel(juce::Label& l, float px, bool bold, juce::Colour colour) {
  l.setFont(Fonts::sans(px, bold));
  l.setColour(juce::Label::textColourId, colour);
  l.setInterceptsMouseClicks(false, false);
}
}  // namespace

RecorderView::RecorderView(Services& services) : services_(services) {
  setOpaque(true);

  close_.setName("Close recordings");
  close_.onClick = [this] {
    if (onClose) onClose();
  };
  addAndMakeVisible(close_);

  title_.setText("RECORDINGS", juce::dontSendNotification);
  styleLabel(title_, 20.0f, true, theme::kWhite);
  addAndMakeVisible(title_);

  styleButton(tracks_, "Tracks >");
  tracks_.onClick = [this] {
    if (onOpenTracks) onOpenTracks();
  };
  addAndMakeVisible(tracks_);

  status_.setText("Ready", juce::dontSendNotification);
  styleLabel(status_, 16.0f, false, theme::kWhite);
  status_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(status_);

  listTitle_.setText("TAKES", juce::dontSendNotification);
  styleLabel(listTitle_, 13.0f, true, theme::kGray);
  addAndMakeVisible(listTitle_);

  time_.setText("0:00 / 0:00", juce::dontSendNotification);
  styleLabel(time_, 14.0f, false, theme::kGray);
  time_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(time_);

  folder_.setText({}, juce::dontSendNotification);
  styleLabel(folder_, 12.0f, false, theme::kGray);
  addAndMakeVisible(folder_);

  styleButton(record_, "Record");
  record_.onClick = [this] { command("record"); };
  addAndMakeVisible(record_);

  styleButton(play_, "Play");
  play_.onClick = [this] { command("play"); };
  addAndMakeVisible(play_);

  styleButton(loop_, "Loop");
  loop_.setClickingTogglesState(true);
  loop_.onClick = [this] { command("loop", loop_.getToggleState()); };
  addAndMakeVisible(loop_);

  styleButton(export_, "Export with amp");
  export_.onClick = [this] { command("export"); };
  addAndMakeVisible(export_);

  styleButton(delete_, "Delete take");
  delete_.onClick = [this] {
    if (selectedId_.isNotEmpty()) command("delete", selectedId_);
  };
  addAndMakeVisible(delete_);

  shareTitle_.setText("SHARE AS WAV", juce::dontSendNotification);  // follows the MP3 toggle
  styleLabel(shareTitle_, 12.0f, true, theme::kGray);
  addAndMakeVisible(shareTitle_);

  styleButton(shareClean_, "Clean");
  shareClean_.onClick = [this] {
    if (selectedId_.isNotEmpty()) shareFile(juce::File(folderPath_).getChildFile(selectedId_ + ".wav"));
  };
  addAndMakeVisible(shareClean_);

  styleButton(shareAmp_, "Amped");
  shareAmp_.onClick = [this] {
    if (selectedId_.isNotEmpty()) shareFile(juce::File(folderPath_).getChildFile(selectedId_ + " (amp).wav"));
  };
  addAndMakeVisible(shareAmp_);

  styleButton(shareExport_, "Export");
  shareExport_.onClick = [this] {
    if (lastExport_.isNotEmpty()) shareFile(juce::File(folderPath_).getChildFile(lastExport_ + ".wav"));
  };
  addAndMakeVisible(shareExport_);

  mp3_.setButtonText("Share as MP3");
  mp3_.setColour(juce::ToggleButton::textColourId, theme::kWhite);
  mp3_.setColour(juce::ToggleButton::tickColourId, theme::kWhite);
  mp3_.setMouseClickGrabsKeyboardFocus(false);
  mp3_.onClick = [this] {
    shareTitle_.setText(mp3_.getToggleState() ? "SHARE AS MP3" : "SHARE AS WAV", juce::dontSendNotification);
  };
  addAndMakeVisible(mp3_);

  recordAmp_.setButtonText("Also record amped sound");
  recordAmp_.setToggleState(true, juce::dontSendNotification);
  recordAmp_.setColour(juce::ToggleButton::textColourId, theme::kWhite);
  recordAmp_.setColour(juce::ToggleButton::tickColourId, theme::kWhite);
  recordAmp_.setMouseClickGrabsKeyboardFocus(false);
  recordAmp_.onClick = [this] { command("recordAmp", recordAmp_.getToggleState()); };
  addAndMakeVisible(recordAmp_);

  list_.setModel(this);
  list_.setRowHeight(44);
  list_.setColour(juce::ListBox::backgroundColourId, theme::kSurfaceRaised);
  list_.setColour(juce::ListBox::outlineColourId, theme::kBorder);
  list_.setOutlineThickness(1);
  list_.setMouseClickGrabsKeyboardFocus(false);
  addAndMakeVisible(list_);

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

  command("refresh");
  startTimerHz(kPollHz);
}

RecorderView::~RecorderView() {
  stopTimer();
  list_.setModel(nullptr);
}

void RecorderView::command(const juce::String& cmd, const juce::var& arg) {
  apply(services_.backend.recorderCommand(cmd, arg));
}

void RecorderView::shareFile(const juce::File& wav) {
  if (!wav.existsAsFile()) return;
  juce::File file = wav;
  if (mp3_.getToggleState()) {
    // Encodes next to the WAV (reused when up to date); a few seconds at most
    // for a typical take.
    const juce::var state = services_.backend.recorderCommand("mp3", wav.getFullPathName());
    apply(state);
    const juce::String path = state.getProperty("mp3Path", {}).toString();
    if (path.isEmpty()) return;  // the status line shows the error
    file = juce::File(path);
    if (!file.existsAsFile()) return;
  }
  IosShare::shareFile(file);
}

void RecorderView::timerCallback() { apply(services_.backend.getRecorderState()); }

void RecorderView::apply(const juce::var& state) {
  if (!state.isObject()) return;
  const bool recording = static_cast<bool>(state.getProperty("recording", false));
  const bool playing = static_cast<bool>(state.getProperty("playing", false));
  const bool exporting = static_cast<bool>(state.getProperty("exporting", false));
  const double elapsed = static_cast<double>(state.getProperty("elapsed", 0.0));
  const double position = static_cast<double>(state.getProperty("position", 0.0));
  const double length = static_cast<double>(state.getProperty("length", 0.0));
  const int dropped = static_cast<int>(state.getProperty("dropped", 0));
  const juce::String error = state.getProperty("error", {}).toString();
  const juce::String message = state.getProperty("message", {}).toString();
  const juce::String selected = state.getProperty("selected", {}).toString();

  // Status line.
  juce::String text;
  juce::Colour colour = theme::kWhite;
  if (error.isNotEmpty()) {
    text = error;
    colour = theme::kBrandRed;
  } else if (recording) {
    text = "REC  " + mmss(elapsed);
    if (dropped > 0) text += "   (audio dropped: disk too slow)";
    colour = theme::kBrandRed;
  } else if (exporting) {
    text = "Exporting...  " + mmss(position) + " / " + mmss(length);
  } else if (playing) {
    text = "Playing through the amp";
  } else if (message.isNotEmpty()) {
    text = message;
  } else {
    text = "Ready";
  }
  if (status_.getText() != text) status_.setText(text, juce::dontSendNotification);
  status_.setColour(juce::Label::textColourId, colour);

  // Buttons.
  record_.setButtonText(recording ? "Stop recording" : "Record");
  record_.setColour(juce::TextButton::buttonColourId, recording ? theme::kBrandRed : theme::kSurfaceRaised);
  record_.setEnabled(!playing && !exporting);
  play_.setButtonText(playing && !exporting ? "Pause" : "Play");
  play_.setEnabled(!recording && !exporting);
  loop_.setEnabled(!exporting);
  loop_.setToggleState(static_cast<bool>(state.getProperty("loop", false)), juce::dontSendNotification);
  export_.setButtonText(exporting ? "Cancel export" : "Export with amp");
  export_.setEnabled(!recording && (exporting || selected.isNotEmpty()));
  delete_.setEnabled(!recording && !exporting && selectedId_.isNotEmpty());
  recordAmp_.setEnabled(!recording);
  recordAmp_.setToggleState(static_cast<bool>(state.getProperty("recordAmp", true)), juce::dontSendNotification);

  // Position.
  updating_ = true;
  position_.setEnabled(length > 0.0 && !recording);
  if (!position_.isMouseButtonDown()) position_.setValue(length > 0.0 ? juce::jlimit(0.0, 1.0, position / length) : 0.0, juce::dontSendNotification);
  updating_ = false;
  time_.setText(mmss(position) + " / " + mmss(length), juce::dontSendNotification);

  const juce::String folder = state.getProperty("folder", {}).toString();
  folderPath_ = folder;
  lastExport_ = state.getProperty("lastExport", {}).toString();
  // A finished export opens the share sheet straight away (the first poll
  // after opening the screen only sets the baseline).
  const int serial = static_cast<int>(state.getProperty("exportSerial", 0));
  if (lastSerial_ < 0) {
    lastSerial_ = serial;
  } else if (serial != lastSerial_) {
    lastSerial_ = serial;
    if (lastExport_.isNotEmpty()) shareFile(juce::File(folderPath_).getChildFile(lastExport_ + ".wav"));
  }
  if (folder.isNotEmpty() && folder_.getText().isEmpty())
    folder_.setText("Files app > TONE3000 > Recordings   (" + folder + ")", juce::dontSendNotification);

  // Take list: rebuild only when it changed.
  std::vector<Row> rows;
  if (auto* arr = state.getProperty("takes", {}).getArray()) {
    for (const auto& item : *arr) {
      Row r;
      r.id = item.getProperty("id", {}).toString();
      r.seconds = static_cast<double>(item.getProperty("seconds", 0.0));
      r.hasAmp = static_cast<bool>(item.getProperty("hasAmp", false));
      rows.push_back(r);
    }
  }
  bool changed = rows.size() != rows_.size();
  for (size_t i = 0; !changed && i < rows.size(); ++i)
    changed = rows[i].id != rows_[i].id || rows[i].hasAmp != rows_[i].hasAmp;
  if (changed) {
    rows_ = rows;
    list_.updateContent();
    list_.repaint();
  }
  selectedHasAmp_ = false;
  for (const auto& r : rows_)
    if (r.id == selected) selectedHasAmp_ = r.hasAmp;
  shareClean_.setEnabled(!recording && selected.isNotEmpty());
  shareAmp_.setEnabled(!recording && selectedHasAmp_);
  shareExport_.setEnabled(!recording && !exporting && lastExport_.isNotEmpty());

  if (selected != selectedId_) {
    selectedId_ = selected;
    int index = -1;
    for (size_t i = 0; i < rows_.size(); ++i)
      if (rows_[i].id == selectedId_) index = static_cast<int>(i);
    updating_ = true;
    if (index >= 0)
      list_.selectRow(index, true, false);
    else
      list_.deselectAllRows();
    updating_ = false;
  }
}

int RecorderView::getNumRows() { return static_cast<int>(rows_.size()); }

void RecorderView::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
  if (row < 0 || row >= static_cast<int>(rows_.size())) return;
  const auto& r = rows_[static_cast<size_t>(row)];
  if (selected) {
    g.setColour(theme::kBrandBlue);
    g.fillRect(0, 0, width, height);
  }
  g.setColour(theme::kWhite);
  g.setFont(Fonts::sans(15.0f, false));
  g.drawText(r.id, 12, 0, width - 140, height, juce::Justification::centredLeft, true);
  g.setColour(selected ? theme::kWhite : theme::kGray);
  g.setFont(Fonts::sans(13.0f, false));
  g.drawText(mmss(r.seconds) + (r.hasAmp ? "  +amp" : ""), width - 124, 0, 112, height,
             juce::Justification::centredRight, true);
}

void RecorderView::selectedRowsChanged(int lastRowSelected) {
  if (updating_) return;
  if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(rows_.size())) return;
  const auto& id = rows_[static_cast<size_t>(lastRowSelected)].id;
  if (id != selectedId_) command("select", id);
}

void RecorderView::paint(juce::Graphics& g) { g.fillAll(theme::kBlack); }

void RecorderView::resized() {
  auto area = getLocalBounds();
  close_.setBounds(area.getRight() - kCloseRight - kCloseBox, area.getY() + kCloseTop, kCloseBox, kCloseBox);

  auto inner = area.reduced(kPad, 0);
  title_.setBounds(inner.getX(), area.getY() + 14, 170, 32);
  tracks_.setBounds(inner.getX() + 190, area.getY() + 14, 130, 32);
  inner.removeFromTop(54);

  auto left = inner.removeFromLeft(kLeftW);
  inner.removeFromLeft(kPad);
  auto right = inner;

  record_.setBounds(left.removeFromTop(64));
  left.removeFromTop(kGap);
  auto row = left.removeFromTop(48);
  play_.setBounds(row.removeFromLeft((row.getWidth() - kGap) * 2 / 3));
  row.removeFromLeft(kGap);
  loop_.setBounds(row);
  left.removeFromTop(kGap);
  export_.setBounds(left.removeFromTop(48));
  left.removeFromTop(kGap);
  delete_.setBounds(left.removeFromTop(36));
  left.removeFromTop(kGap);
  {
    auto toggles = left.removeFromTop(28);
    mp3_.setBounds(toggles.removeFromRight(130));
    recordAmp_.setBounds(toggles);
  }
  left.removeFromTop(kGap);
  shareTitle_.setBounds(left.removeFromTop(16));
  left.removeFromTop(4);
  auto shareRow = left.removeFromTop(40);
  const int shareW = (shareRow.getWidth() - 2 * kGap) / 3;
  shareClean_.setBounds(shareRow.removeFromLeft(shareW));
  shareRow.removeFromLeft(kGap);
  shareAmp_.setBounds(shareRow.removeFromLeft(shareW));
  shareRow.removeFromLeft(kGap);
  shareExport_.setBounds(shareRow);

  status_.setBounds(right.removeFromTop(32));
  right.removeFromTop(6);
  listTitle_.setBounds(right.removeFromTop(18));
  right.removeFromTop(4);
  folder_.setBounds(right.removeFromBottom(18));
  right.removeFromBottom(6);
  auto bottom = right.removeFromBottom(36);
  time_.setBounds(bottom.removeFromRight(120));
  position_.setBounds(bottom);
  right.removeFromBottom(8);
  list_.setBounds(right);
}

}  // namespace t3k::ui
