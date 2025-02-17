// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ScrollManager_h
#define ScrollManager_h

#include "core/CoreExport.h"
#include "core/page/EventWithHitTestResults.h"
#include "platform/geometry/LayoutSize.h"
#include "platform/heap/Handle.h"
#include "platform/heap/Visitor.h"
#include "platform/scroll/ScrollTypes.h"
#include "public/platform/WebInputEventResult.h"
#include "wtf/Allocator.h"
#include <deque>

namespace blink {

class AutoscrollController;
class LayoutBox;
class LayoutObject;
class LocalFrame;
class PaintLayer;
class PaintLayerScrollableArea;
class Page;
class Scrollbar;
class ScrollState;
class WebGestureEvent;

// This class takes care of scrolling and resizing and the related states. The
// user action that causes scrolling or resizing is determined in other *Manager
// classes and they call into this class for doing the work.
class CORE_EXPORT ScrollManager
    : public GarbageCollectedFinalized<ScrollManager> {
  WTF_MAKE_NONCOPYABLE(ScrollManager);

 public:
  explicit ScrollManager(LocalFrame&);
  DECLARE_TRACE();

  void Clear();

  bool MiddleClickAutoscrollInProgress() const;
  AutoscrollController* GetAutoscrollController() const;
  void StopAutoscroll();

  // Performs a chaining logical scroll, within a *single* frame, starting
  // from either a provided starting node or a default based on the focused or
  // most recently clicked node, falling back to the frame.
  // Returns true if the scroll was consumed.
  // direction - The logical direction to scroll in. This will be converted to
  //             a physical direction for each LayoutBox we try to scroll
  //             based on that box's writing mode.
  // granularity - The units that the  scroll delta parameter is in.
  // startNode - Optional. If provided, start chaining from the given node.
  //             If not, use the current focus or last clicked node.
  bool LogicalScroll(ScrollDirection,
                     ScrollGranularity,
                     Node* start_node,
                     Node* mouse_press_node);

  // Performs a logical scroll that chains, crossing frames, starting from
  // the given node or a reasonable default (focus/last clicked).
  bool BubblingScroll(ScrollDirection,
                      ScrollGranularity,
                      Node* starting_node,
                      Node* mouse_press_node);

  void SetFrameWasScrolledByUser();

  // TODO(crbug.com/616491): Consider moving all gesture related functions to
  // another class.

  // Handle the provided scroll gesture event, propagating down to child frames
  // as necessary.
  WebInputEventResult HandleGestureScrollEvent(const WebGestureEvent&);

  WebInputEventResult HandleGestureScrollEnd(const WebGestureEvent&);

  bool IsScrollbarHandlingGestures() const;

  // Returns true if the gesture event should be handled in ScrollManager.
  bool CanHandleGestureEvent(const GestureEventWithHitTestResults&);

  // These functions are related to |m_resizeScrollableArea|.
  bool InResizeMode() const;
  void Resize(const WebMouseEvent&);
  // Clears |m_resizeScrollableArea|. if |shouldNotBeNull| is true this
  // function DCHECKs to make sure that variable is indeed not null.
  void ClearResizeScrollableArea(bool should_not_be_null);
  void SetResizeScrollableArea(PaintLayer*, IntPoint);

 private:
  WebInputEventResult HandleGestureScrollUpdate(const WebGestureEvent&);
  WebInputEventResult HandleGestureScrollBegin(const WebGestureEvent&);

  WebInputEventResult PassScrollGestureEvent(const WebGestureEvent&,
                                             LayoutObject*);

  void ClearGestureScrollState();

  void CustomizedScroll(const Node& start_node, ScrollState&);

  Page* GetPage() const;

  bool IsViewportScrollingElement(const Element&) const;

  bool HandleScrollGestureOnResizer(Node*, const WebGestureEvent&);

  void RecomputeScrollChain(const Node& start_node,
                            std::deque<int>& scroll_chain);

  uint32_t ComputeNonCompositedMainThreadScrollingReasons();
  void RecordNonCompositedMainThreadScrollingReasons(const WebGestureDevice);

  // NOTE: If adding a new field to this class please ensure that it is
  // cleared in |ScrollManager::clear()|.

  const Member<LocalFrame> frame_;

  // Only used with the ScrollCustomization runtime enabled feature.
  std::deque<int> current_scroll_chain_;

  Member<Node> scroll_gesture_handling_node_;

  bool last_gesture_scroll_over_frame_view_base_;

  // The most recent element to scroll natively during this scroll
  // sequence. Null if no native element has scrolled this scroll
  // sequence, or if the most recent element to scroll used scroll
  // customization.
  Member<Element> previous_gesture_scrolled_element_;

  // True iff some of the delta has been consumed for the current
  // scroll sequence in this frame, or any child frames. Only used
  // with ScrollCustomization. If some delta has been consumed, a
  // scroll which shouldn't propagate can't cause any element to
  // scroll other than the |m_previousGestureScrolledNode|.
  bool delta_consumed_for_scroll_sequence_;

  Member<Scrollbar> scrollbar_handling_scroll_gesture_;

  Member<PaintLayerScrollableArea> resize_scrollable_area_;

  LayoutSize
      offset_from_resize_corner_;  // In the coords of m_resizeScrollableArea.
};

}  // namespace blink

#endif  // ScrollManager_h
