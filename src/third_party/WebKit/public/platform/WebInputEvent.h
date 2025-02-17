/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebInputEvent_h
#define WebInputEvent_h

#include "WebCommon.h"
#include "WebPointerProperties.h"
#include "WebRect.h"
#include "WebTouchPoint.h"

#include <string.h>

namespace blink {

// The classes defined in this file are intended to be used with
// WebWidget's handleInputEvent method.  These event types are cross-
// platform and correspond closely to WebCore's Platform*Event classes.
//
// WARNING! These classes must remain PODs (plain old data).  They are
// intended to be "serializable" by copying their raw bytes, so they must
// not contain any non-bit-copyable member variables!
//
// Furthermore, the class members need to be packed so they are aligned
// properly and don't have paddings/gaps, otherwise memory check tools
// like Valgrind will complain about uninitialized memory usage when
// transferring these classes over the wire.

#pragma pack(push, 4)

// WebInputEvent --------------------------------------------------------------

class WebInputEvent {
 public:
  // When we use an input method (or an input method editor), we receive
  // two events for a keypress. The former event is a keydown, which
  // provides a keycode, and the latter is a textinput, which provides
  // a character processed by an input method. (The mapping from a
  // keycode to a character code is not trivial for non-English
  // keyboards.)
  // To support input methods, Safari sends keydown events to WebKit for
  // filtering. WebKit sends filtered keydown events back to Safari,
  // which sends them to input methods.
  // Unfortunately, it is hard to apply this design to Chrome because of
  // our multiprocess architecture. An input method is running in a
  // browser process. On the other hand, WebKit is running in a renderer
  // process. So, this design results in increasing IPC messages.
  // To support input methods without increasing IPC messages, Chrome
  // handles keyboard events in a browser process and send asynchronous
  // input events (to be translated to DOM events) to a renderer
  // process.
  // This design is mostly the same as the one of Windows and Mac Carbon.
  // So, for what it's worth, our Linux and Mac front-ends emulate our
  // Windows front-end. To emulate our Windows front-end, we can share
  // our back-end code among Windows, Linux, and Mac.
  // TODO(hbono): Issue 18064: remove the KeyDown type since it isn't
  // used in Chrome any longer.

  // A Java counterpart will be generated for this enum.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.blink_public.web
  // GENERATED_JAVA_CLASS_NAME_OVERRIDE: WebInputEventType
  enum Type {
    kUndefined = -1,
    kTypeFirst = kUndefined,

    // WebMouseEvent
    kMouseDown,
    kMouseTypeFirst = kMouseDown,
    kMouseUp,
    kMouseMove,
    kMouseEnter,
    kMouseLeave,
    kContextMenu,
    kMouseTypeLast = kContextMenu,

    // WebMouseWheelEvent
    kMouseWheel,

    // WebKeyboardEvent
    kRawKeyDown,
    kKeyboardTypeFirst = kRawKeyDown,
    // KeyDown is a single event combining RawKeyDown and Char.  If KeyDown is
    // sent for a given keystroke, those two other events will not be sent.
    // Platforms tend to prefer sending in one format (Android uses KeyDown,
    // Windows uses RawKeyDown+Char, for example), but this is a weakly held
    // property as tools like WebDriver/DevTools might still send the other
    // format.
    kKeyDown,
    kKeyUp,
    kChar,
    kKeyboardTypeLast = kChar,

    // WebGestureEvent - input interpreted semi-semantically, most commonly from
    // touchscreen but also used for touchpad, mousewheel, and gamepad
    // scrolling.
    kGestureScrollBegin,
    kGestureTypeFirst = kGestureScrollBegin,
    kGestureScrollEnd,
    kGestureScrollUpdate,
    // Fling is a high-velocity and quickly released finger movement.
    // FlingStart is sent once and kicks off a scroll animation.
    kGestureFlingStart,
    kGestureFlingCancel,
    // Pinch is two fingers moving closer or farther apart.
    kGesturePinchBegin,
    kGesturePinchEnd,
    kGesturePinchUpdate,

    // The following types are variations and subevents of single-taps.
    //
    // Sent the moment the user's finger hits the screen.
    kGestureTapDown,
    // Sent a short interval later, after it seems the finger is staying in
    // place.  It's used to activate the link highlight ("show the press").
    kGestureShowPress,
    // Sent on finger lift for a simple, static, quick finger tap.  This is the
    // "main" event which maps to a synthetic mouse click event.
    kGestureTap,
    // Sent when a GestureTapDown didn't turn into any variation of GestureTap
    // (likely it turned into a scroll instead).
    kGestureTapCancel,
    // Sent as soon as the long-press timeout fires, while the finger is still
    // down.
    kGestureLongPress,
    // Sent when the finger is lifted following a GestureLongPress.
    kGestureLongTap,
    // Sent on finger lift when two fingers tapped at the same time without
    // moving.
    kGestureTwoFingerTap,
    // A rare event sent in place of GestureTap on desktop pages viewed on an
    // Android phone.  This tap could not yet be resolved into a GestureTap
    // because it may still turn into a GestureDoubleTap.
    kGestureTapUnconfirmed,

    // Double-tap is two single-taps spread apart in time, like a double-click.
    // This event is only sent on desktop pages viewed on an Android phone, and
    // is always preceded by GestureTapUnconfirmed.  It's an instruction to
    // Blink to perform a PageScaleAnimation zoom onto the double-tapped
    // content.  (It's treated differently from GestureTap with tapCount=2,
    // which can also happen.)
    kGestureDoubleTap,

    kGestureTypeLast = kGestureDoubleTap,

    // WebTouchEvent - raw touch pointers not yet classified into gestures.
    kTouchStart,
    kTouchTypeFirst = kTouchStart,
    kTouchMove,
    kTouchEnd,
    kTouchCancel,
    kTouchScrollStarted,
    kTouchTypeLast = kTouchScrollStarted,

    kTypeLast = kTouchTypeLast
  };

  // The modifier constants cannot change their values since pepper
  // does a 1-1 mapping of its values; see
  // content/renderer/pepper/event_conversion.cc
  //
  // A Java counterpart will be generated for this enum.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.blink_public.web
  // GENERATED_JAVA_CLASS_NAME_OVERRIDE: WebInputEventModifier
  enum Modifiers {
    // modifiers for all events:
    kShiftKey = 1 << 0,
    kControlKey = 1 << 1,
    kAltKey = 1 << 2,
    kMetaKey = 1 << 3,

    // modifiers for keyboard events:
    kIsKeyPad = 1 << 4,
    kIsAutoRepeat = 1 << 5,

    // modifiers for mouse events:
    kLeftButtonDown = 1 << 6,
    kMiddleButtonDown = 1 << 7,
    kRightButtonDown = 1 << 8,

    // Toggle modifers for all events.
    kCapsLockOn = 1 << 9,
    kNumLockOn = 1 << 10,

    kIsLeft = 1 << 11,
    kIsRight = 1 << 12,

    // Indicates that an event was generated on the touch screen while
    // touch accessibility is enabled, so the event should be handled
    // by accessibility code first before normal input event processing.
    kIsTouchAccessibility = 1 << 13,

    kIsComposing = 1 << 14,

    kAltGrKey = 1 << 15,
    kFnKey = 1 << 16,
    kSymbolKey = 1 << 17,

    kScrollLockOn = 1 << 18,

    // Whether this is a compatibility event generated due to a
    // native touch event. Mouse events generated from touch
    // events will set this.
    kIsCompatibilityEventForTouch = 1 << 19,

    kBackButtonDown = 1 << 20,
    kForwardButtonDown = 1 << 21,

    // The set of non-stateful modifiers that specifically change the
    // interpretation of the key being pressed. For example; IsLeft,
    // IsRight, IsComposing don't change the meaning of the key
    // being pressed. NumLockOn, ScrollLockOn, CapsLockOn are stateful
    // and don't indicate explicit depressed state.
    kKeyModifiers = kSymbolKey | kFnKey | kAltGrKey | kMetaKey | kAltKey |
                    kControlKey |
                    kShiftKey,
    kNoModifiers = 0,
  };

  // Indicates whether the browser needs to block on the ACK result for
  // this event, and if not, why (for metrics/diagnostics purposes).
  // These values are direct mappings of the values in PlatformEvent
  // so the values can be cast between the enumerations. static_asserts
  // checking this are in web/WebInputEventConversion.cpp.
  enum DispatchType {
    // Event can be canceled.
    kBlocking,
    // Event can not be canceled.
    kEventNonBlocking,
    // All listeners are passive; not cancelable.
    kListenersNonBlockingPassive,
    // This value represents a state which would have normally blocking
    // but was forced to be non-blocking during fling; not cancelable.
    kListenersForcedNonBlockingDueToFling,
    // This value represents a state which would have normally blocking but
    // was forced to be non-blocking due to the main thread being
    // unresponsive.
    kListenersForcedNonBlockingDueToMainThreadResponsiveness,
  };

  // The rail mode for a wheel event specifies the axis on which scrolling is
  // expected to stick. If this axis is set to Free, then scrolling is not
  // stuck to any axis.
  enum RailsMode {
    kRailsModeFree = 0,
    kRailsModeHorizontal = 1,
    kRailsModeVertical = 2,
  };

  static const int kInputModifiers =
      kShiftKey | kControlKey | kAltKey | kMetaKey;

  static constexpr double kTimeStampForTesting = 123.0;

  // Returns true if the WebInputEvent |type| is a mouse event.
  static bool IsMouseEventType(int type) {
    return kMouseTypeFirst <= type && type <= kMouseTypeLast;
  }

  // Returns true if the WebInputEvent |type| is a keyboard event.
  static bool IsKeyboardEventType(int type) {
    return kKeyboardTypeFirst <= type && type <= kKeyboardTypeLast;
  }

  // Returns true if the WebInputEvent |type| is a touch event.
  static bool IsTouchEventType(int type) {
    return kTouchTypeFirst <= type && type <= kTouchTypeLast;
  }

  // Returns true if the WebInputEvent is a gesture event.
  static bool IsGestureEventType(int type) {
    return kGestureTypeFirst <= type && type <= kGestureTypeLast;
  }

  bool IsSameEventClass(const WebInputEvent& other) const {
    if (IsMouseEventType(type_))
      return IsMouseEventType(other.type_);
    if (IsGestureEventType(type_))
      return IsGestureEventType(other.type_);
    if (IsTouchEventType(type_))
      return IsTouchEventType(other.type_);
    if (IsKeyboardEventType(type_))
      return IsKeyboardEventType(other.type_);
    return type_ == other.type_;
  }

  static const char* GetName(WebInputEvent::Type type) {
#define CASE_TYPE(t)        \
  case WebInputEvent::k##t: \
    return #t
    switch (type) {
      CASE_TYPE(Undefined);
      CASE_TYPE(MouseDown);
      CASE_TYPE(MouseUp);
      CASE_TYPE(MouseMove);
      CASE_TYPE(MouseEnter);
      CASE_TYPE(MouseLeave);
      CASE_TYPE(ContextMenu);
      CASE_TYPE(MouseWheel);
      CASE_TYPE(RawKeyDown);
      CASE_TYPE(KeyDown);
      CASE_TYPE(KeyUp);
      CASE_TYPE(Char);
      CASE_TYPE(GestureScrollBegin);
      CASE_TYPE(GestureScrollEnd);
      CASE_TYPE(GestureScrollUpdate);
      CASE_TYPE(GestureFlingStart);
      CASE_TYPE(GestureFlingCancel);
      CASE_TYPE(GestureShowPress);
      CASE_TYPE(GestureTap);
      CASE_TYPE(GestureTapUnconfirmed);
      CASE_TYPE(GestureTapDown);
      CASE_TYPE(GestureTapCancel);
      CASE_TYPE(GestureDoubleTap);
      CASE_TYPE(GestureTwoFingerTap);
      CASE_TYPE(GestureLongPress);
      CASE_TYPE(GestureLongTap);
      CASE_TYPE(GesturePinchBegin);
      CASE_TYPE(GesturePinchEnd);
      CASE_TYPE(GesturePinchUpdate);
      CASE_TYPE(TouchStart);
      CASE_TYPE(TouchMove);
      CASE_TYPE(TouchEnd);
      CASE_TYPE(TouchCancel);
      CASE_TYPE(TouchScrollStarted);
      default:
        NOTREACHED();
        return "";
    }
#undef CASE_TYPE
  }

  float FrameScale() const { return frame_scale_; }
  void SetFrameScale(float scale) { frame_scale_ = scale; }

  WebFloatPoint FrameTranslate() const { return frame_translate_; }
  void SetFrameTranslate(WebFloatPoint translate) {
    frame_translate_ = translate;
  }

  Type GetType() const { return type_; }
  void SetType(Type type_param) { type_ = type_param; }

  int GetModifiers() const { return modifiers_; }
  void SetModifiers(int modifiers_param) { modifiers_ = modifiers_param; }

  double TimeStampSeconds() const { return time_stamp_seconds_; }
  void SetTimeStampSeconds(double seconds) { time_stamp_seconds_ = seconds; }

  unsigned size() const { return size_; }

 protected:
  // The root frame scale.
  float frame_scale_;

  // The root frame translation (applied post scale).
  WebFloatPoint frame_translate_;

  WebInputEvent(unsigned size_param,
                Type type_param,
                int modifiers_param,
                double time_stamp_seconds_param) {
    // TODO(dtapuska): Remove this memset when we remove the chrome IPC of this
    // struct.
    memset(this, 0, size_param);
    time_stamp_seconds_ = time_stamp_seconds_param;
    size_ = size_param;
    type_ = type_param;
    modifiers_ = modifiers_param;
#if DCHECK_IS_ON()
    // If dcheck is on force failures if frame scale is not initialized
    // correctly by causing DIV0.
    frame_scale_ = 0;
#else
    frame_scale_ = 1;
#endif
  }

  WebInputEvent(unsigned size_param) {
    // TODO(dtapuska): Remove this memset when we remove the chrome IPC of this
    // struct.
    memset(this, 0, size_param);
    time_stamp_seconds_ = 0.0;
    size_ = size_param;
    type_ = kUndefined;
#if DCHECK_IS_ON()
    // If dcheck is on force failures if frame scale is not initialized
    // correctly by causing DIV0.
    frame_scale_ = 0;
#else
    frame_scale_ = 1;
#endif
  }

  double time_stamp_seconds_;  // Seconds since platform start with microsecond
                               // resolution.
  unsigned size_;              // The size of this structure, for serialization.
  Type type_;
  int modifiers_;
};

#pragma pack(pop)

}  // namespace blink

#endif
