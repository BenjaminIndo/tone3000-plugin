#pragma once

#include <juce_core/juce_core.h>

/**
 * Native iOS share sheet for one file (recordings, mixes). JUCE's own
 * ContentSharer opened underneath the app on the iPad, so this presents a
 * UIActivityViewController directly from the top-most view controller, as a
 * centred popover on iPad. Off iOS it just reveals the file in the OS browser.
 */
namespace IosShare {

#if JUCE_IOS

void shareFile(const juce::File& file);

#else

inline void shareFile(const juce::File& file) {
  file.revealToUser();
}

#endif

}  // namespace IosShare
