/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/LayoutTableRow.h"

#include "core/layout/LayoutTestHelper.h"

namespace blink {

namespace {

class LayoutTableRowDeathTest : public RenderingTest {
 protected:
  virtual void SetUp() {
    RenderingTest::SetUp();
    row_ = LayoutTableRow::CreateAnonymous(&GetDocument());
  }

  virtual void TearDown() { row_->Destroy(); }

  LayoutTableRow* row_;
};

TEST_F(LayoutTableRowDeathTest, CanSetRow) {
  static const unsigned kRowIndex = 10;
  row_->SetRowIndex(kRowIndex);
  EXPECT_EQ(kRowIndex, row_->RowIndex());
}

TEST_F(LayoutTableRowDeathTest, CanSetRowToMaxRowIndex) {
  row_->SetRowIndex(kMaxRowIndex);
  EXPECT_EQ(kMaxRowIndex, row_->RowIndex());
}

// FIXME: Re-enable these tests once ASSERT_DEATH is supported for Android.
// See: https://bugs.webkit.org/show_bug.cgi?id=74089
// TODO(dgrogan): These tests started flaking on Mac try bots around 2016-07-28.
// https://crbug.com/632816
#if !OS(ANDROID) && !OS(MACOSX)

TEST_F(LayoutTableRowDeathTest, CrashIfRowOverflowOnSetting) {
  ASSERT_DEATH(row_->SetRowIndex(kMaxRowIndex + 1), "");
}

TEST_F(LayoutTableRowDeathTest, CrashIfSettingUnsetRowIndex) {
  ASSERT_DEATH(row_->SetRowIndex(kUnsetRowIndex), "");
}

#endif

class LayoutTableRowTest : public RenderingTest {
 protected:
  LayoutTableRow* GetRowByElementId(const char* id) {
    return ToLayoutTableRow(GetLayoutObjectByElementId(id));
  }
};

TEST_F(LayoutTableRowTest,
       BackgroundIsKnownToBeOpaqueWithLayerAndCollapsedBorder) {
  SetBodyInnerHTML(
      "<table style='border-collapse: collapse'>"
      "  <tr id='row' style='will-change: transform;"
      "      background-color: blue'>"
      "    <td>Cell</td>"
      "  </tr>"
      "</table>");

  EXPECT_FALSE(GetRowByElementId("row")->BackgroundIsKnownToBeOpaqueInRect(
      LayoutRect(0, 0, 1, 1)));
}

TEST_F(LayoutTableRowTest, BackgroundIsKnownToBeOpaqueWithBorderSpacing) {
  SetBodyInnerHTML(
      "<table style='border-spacing: 10px'>"
      "  <tr id='row' style='background-color: blue'><td>Cell</td></tr>"
      "</table>");

  EXPECT_FALSE(GetRowByElementId("row")->BackgroundIsKnownToBeOpaqueInRect(
      LayoutRect(0, 0, 1, 1)));
}

TEST_F(LayoutTableRowTest, BackgroundIsKnownToBeOpaqueWithEmptyCell) {
  SetBodyInnerHTML(
      "<table style='border-spacing: 10px'>"
      "  <tr id='row' style='background-color: blue'><td>Cell</td></tr>"
      "  <tr style='background-color: blue'><td>Cell</td><td>Cell</td></tr>"
      "</table>");

  EXPECT_FALSE(GetRowByElementId("row")->BackgroundIsKnownToBeOpaqueInRect(
      LayoutRect(0, 0, 1, 1)));
}

TEST_F(LayoutTableRowTest, VisualOverflow) {
  // +---+---+---+
  // | A |   |   |      row1
  // |---| B |   |---+
  // | D |   | C |   |  row2
  // |---|---|   | E |
  // | F |   |   |   |  row3
  // +---+   +---+---+
  // Cell D has an outline which creates overflow.
  SetBodyInnerHTML(
      "<style>"
      "  td { width: 100px; height: 100px; padding: 0 }"
      "</style>"
      "<table style='border-spacing: 10px'>"
      "  <tr id='row1'>"
      "    <td>A</td>"
      "    <td rowspan='2'>B</td>"
      "    <td rowspan='3'>C</td>"
      "  </tr>"
      "  <tr id='row2'>"
      "    <td style='outline: 10px solid blue'>D</td>"
      "    <td rowspan='2'>E</td>"
      "  </tr>"
      "  <tr id='row3'>"
      "    <td>F</td>"
      "  </tr>"
      "</table>");

  auto* row1 = GetRowByElementId("row1");
  EXPECT_EQ(LayoutRect(120, 0, 210, 320), row1->ContentsVisualOverflowRect());
  EXPECT_EQ(LayoutRect(0, 0, 450, 320), row1->SelfVisualOverflowRect());

  auto* row2 = GetRowByElementId("row2");
  EXPECT_EQ(LayoutRect(0, -10, 440, 220), row2->ContentsVisualOverflowRect());
  EXPECT_EQ(LayoutRect(0, 0, 450, 210), row2->SelfVisualOverflowRect());

  auto* row3 = GetRowByElementId("row3");
  EXPECT_EQ(LayoutRect(), row3->ContentsVisualOverflowRect());
  EXPECT_EQ(LayoutRect(0, 0, 450, 100), row3->SelfVisualOverflowRect());
}

TEST_F(LayoutTableRowTest, LayoutOverflow) {
  SetBodyInnerHTML(
      "<table style='border-spacing: 0'>"
      "  <tr id='row'>"
      "    <td style='100px; height: 100px; padding: 0'>"
      "      <div style='position: relative; top: 50px; left: 50px;"
      "          width: 100px; height: 100px'></div>"
      "    </td>"
      "  </tr>"
      "</table>");

  EXPECT_EQ(LayoutRect(0, 0, 150, 150),
            GetRowByElementId("row")->LayoutOverflowRect());
}

}  // anonymous namespace

}  // namespace blink
