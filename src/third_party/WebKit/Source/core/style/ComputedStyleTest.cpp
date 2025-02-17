// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/style/ComputedStyle.h"

#include "core/style/ClipPathOperation.h"
#include "core/style/ShapeValue.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

TEST(ComputedStyleTest, ShapeOutsideBoxEqual) {
  ShapeValue* shape1 = ShapeValue::CreateBoxShapeValue(kContentBox);
  ShapeValue* shape2 = ShapeValue::CreateBoxShapeValue(kContentBox);
  RefPtr<ComputedStyle> style1 = ComputedStyle::Create();
  RefPtr<ComputedStyle> style2 = ComputedStyle::Create();
  style1->SetShapeOutside(shape1);
  style2->SetShapeOutside(shape2);
  ASSERT_EQ(*style1, *style2);
}

TEST(ComputedStyleTest, ShapeOutsideCircleEqual) {
  RefPtr<BasicShapeCircle> circle1 = BasicShapeCircle::Create();
  RefPtr<BasicShapeCircle> circle2 = BasicShapeCircle::Create();
  ShapeValue* shape1 = ShapeValue::CreateShapeValue(circle1, kContentBox);
  ShapeValue* shape2 = ShapeValue::CreateShapeValue(circle2, kContentBox);
  RefPtr<ComputedStyle> style1 = ComputedStyle::Create();
  RefPtr<ComputedStyle> style2 = ComputedStyle::Create();
  style1->SetShapeOutside(shape1);
  style2->SetShapeOutside(shape2);
  ASSERT_EQ(*style1, *style2);
}

TEST(ComputedStyleTest, ClipPathEqual) {
  RefPtr<BasicShapeCircle> shape = BasicShapeCircle::Create();
  RefPtr<ShapeClipPathOperation> path1 = ShapeClipPathOperation::Create(shape);
  RefPtr<ShapeClipPathOperation> path2 = ShapeClipPathOperation::Create(shape);
  RefPtr<ComputedStyle> style1 = ComputedStyle::Create();
  RefPtr<ComputedStyle> style2 = ComputedStyle::Create();
  style1->SetClipPath(path1);
  style2->SetClipPath(path2);
  ASSERT_EQ(*style1, *style2);
}

TEST(ComputedStyleTest, FocusRingWidth) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  style->SetEffectiveZoom(3.5);
#if OS(MACOSX)
  style->SetOutlineStyle(kBorderStyleSolid);
  ASSERT_EQ(3, style->GetOutlineStrokeWidthForFocusRing());
#else
  ASSERT_EQ(3.5, style->GetOutlineStrokeWidthForFocusRing());
  style->SetEffectiveZoom(0.5);
  ASSERT_EQ(1, style->GetOutlineStrokeWidthForFocusRing());
#endif
}

TEST(ComputedStyleTest, FocusRingOutset) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  style->SetOutlineStyle(kBorderStyleSolid);
  style->SetOutlineStyleIsAuto(kOutlineIsAutoOn);
  style->SetEffectiveZoom(4.75);
#if OS(MACOSX)
  ASSERT_EQ(4, style->OutlineOutsetExtent());
#else
  ASSERT_EQ(3, style->OutlineOutsetExtent());
#endif
}

TEST(ComputedStyleTest, Preserve3dForceStackingContext) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  style->SetTransformStyle3D(kTransformStyle3DPreserve3D);
  style->SetOverflowX(EOverflow::kHidden);
  style->SetOverflowY(EOverflow::kHidden);
  style->UpdateIsStackingContext(false, false);
  EXPECT_EQ(kTransformStyle3DFlat, style->UsedTransformStyle3D());
  EXPECT_TRUE(style->IsStackingContext());
}

TEST(ComputedStyleTest, FirstPublicPseudoStyle) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  style->SetHasPseudoStyle(kPseudoIdFirstLine);
  EXPECT_TRUE(style->HasPseudoStyle(kPseudoIdFirstLine));
  EXPECT_TRUE(style->HasAnyPublicPseudoStyles());
}

TEST(ComputedStyleTest, LastPublicPseudoStyle) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  style->SetHasPseudoStyle(kPseudoIdScrollbar);
  EXPECT_TRUE(style->HasPseudoStyle(kPseudoIdScrollbar));
  EXPECT_TRUE(style->HasAnyPublicPseudoStyles());
}

TEST(ComputedStyleTest,
     UpdatePropertySpecificDifferencesRespectsTransformAnimation) {
  RefPtr<ComputedStyle> style = ComputedStyle::Create();
  RefPtr<ComputedStyle> other = ComputedStyle::Clone(*style);
  other->SetHasCurrentTransformAnimation();
  StyleDifference diff;
  style->UpdatePropertySpecificDifferences(*other, diff);
  EXPECT_TRUE(diff.TransformChanged());
}

}  // namespace blink
