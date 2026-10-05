#include "IosShare.h"

#if JUCE_IOS

#import <UIKit/UIKit.h>

namespace {

UIViewController* topViewController() {
  UIWindow* window = nil;
  for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]]) continue;
    UIWindowScene* windowScene = (UIWindowScene*) scene;
    if (windowScene.activationState != UISceneActivationStateForegroundActive) continue;
    for (UIWindow* w in windowScene.windows) {
      if (w.isKeyWindow) {
        window = w;
        break;
      }
    }
    if (window == nil && windowScene.windows.count > 0) window = windowScene.windows.firstObject;
    if (window != nil) break;
  }
  if (window == nil) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    window = [UIApplication sharedApplication].keyWindow;
#pragma clang diagnostic pop
  }
  UIViewController* controller = window.rootViewController;
  while (controller.presentedViewController != nil)
    controller = controller.presentedViewController;
  return controller;
}

}  // namespace

namespace IosShare {

void shareFile(const juce::File& file) {
  if (!file.existsAsFile()) return;
  UIViewController* top = topViewController();
  if (top == nil) return;

  NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()]];
  UIActivityViewController* sheet = [[UIActivityViewController alloc] initWithActivityItems:@[ url ]
                                                                      applicationActivities:nil];
  // iPad requires an anchor: a centred popover without an arrow.
  UIPopoverPresentationController* popover = sheet.popoverPresentationController;
  if (popover != nil) {
    popover.sourceView = top.view;
    const CGRect bounds = top.view.bounds;
    popover.sourceRect = CGRectMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds), 1.0, 1.0);
    popover.permittedArrowDirections = (UIPopoverArrowDirection) 0;
  }
  [top presentViewController:sheet animated:YES completion:nil];
#if !__has_feature(objc_arc)
  [sheet release];
#endif
}

}  // namespace IosShare

#endif
