/*
   Copyright (C) 2000 Simon Hausmann <hausmann@kde.org>
   Copyright (C) 2006 Apple Computer, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef EventWithHitTestResults_h
#define EventWithHitTestResults_h

#include "core/layout/HitTestResult.h"
#include "public/platform/WebGestureEvent.h"
#include "public/platform/WebMouseEvent.h"

namespace blink {

class Scrollbar;

template <typename EventType>
class EventWithHitTestResults {
  STACK_ALLOCATED();

 public:
  EventWithHitTestResults(const EventType& event,
                          const HitTestResult& hit_test_result)
      : event_(event), hit_test_result_(hit_test_result) {}

  const EventType& Event() const { return event_; }
  const HitTestResult& GetHitTestResult() const { return hit_test_result_; }
  LayoutPoint LocalPoint() const { return hit_test_result_.LocalPoint(); }
  Scrollbar* GetScrollbar() const { return hit_test_result_.GetScrollbar(); }
  bool IsOverLink() const { return hit_test_result_.IsOverLink(); }
  bool IsOverFrameViewBase() const {
    return hit_test_result_.IsOverFrameViewBase();
  }
  Node* InnerNode() const { return hit_test_result_.InnerNode(); }
  const String& CanvasRegionId() const {
    return hit_test_result_.CanvasRegionId();
  }

 private:
  EventType event_;
  HitTestResult hit_test_result_;
};

using MouseEventWithHitTestResults = EventWithHitTestResults<WebMouseEvent>;

using GestureEventWithHitTestResults = EventWithHitTestResults<WebGestureEvent>;

}  // namespace blink

#endif  // EventWithHitTestResults_h
