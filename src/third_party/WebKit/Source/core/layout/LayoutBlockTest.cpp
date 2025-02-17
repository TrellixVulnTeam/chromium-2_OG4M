// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/ElementTraversal.h"
#include "core/layout/LayoutBlock.h"

#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutTestHelper.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class LayoutBlockTest : public RenderingTest {};

TEST_F(LayoutBlockTest, LayoutNameCalledWithNullStyle) {
  LayoutObject* obj = LayoutBlockFlow::CreateAnonymous(&GetDocument());
  EXPECT_FALSE(obj->Style());
  EXPECT_STREQ("LayoutBlockFlow (anonymous)",
               obj->DecoratedName().Ascii().Data());
  obj->Destroy();
}

TEST_F(LayoutBlockTest, WidthAvailableToChildrenChanged) {
  RuntimeEnabledFeatures::setOverlayScrollbarsEnabled(false);
  SetBodyInnerHTML(
      "<!DOCTYPE html>"
      "<div id='list' style='overflow-y:auto; width:150px; height:100px'>"
      "  <div style='height:20px'>Item</div>"
      "  <div style='height:20px'>Item</div>"
      "  <div style='height:20px'>Item</div>"
      "  <div style='height:20px'>Item</div>"
      "  <div style='height:20px'>Item</div>"
      "  <div style='height:20px'>Item</div>"
      "</div>");
  Element* list_element = GetDocument().GetElementById("list");
  ASSERT_TRUE(list_element);
  LayoutBox* list_box = ToLayoutBox(list_element->GetLayoutObject());
  Element* item_element = ElementTraversal::FirstChild(*list_element);
  ASSERT_TRUE(item_element);
  ASSERT_GT(list_box->VerticalScrollbarWidth(), 0);
  ASSERT_EQ(item_element->OffsetWidth(),
            150 - list_box->VerticalScrollbarWidth());

  DummyExceptionStateForTesting exception_state;
  list_element->style()->setCSSText("width:150px;height:100px;",
                                    exception_state);
  ASSERT_FALSE(exception_state.HadException());
  GetDocument().View()->UpdateAllLifecyclePhases();
  ASSERT_EQ(list_box->VerticalScrollbarWidth(), 0);
  ASSERT_EQ(item_element->OffsetWidth(), 150);
}

}  // namespace blink
