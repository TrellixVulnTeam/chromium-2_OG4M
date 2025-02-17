// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_LAYOUT_LAYOUT_PROVIDER_H_
#define UI_VIEWS_LAYOUT_LAYOUT_PROVIDER_H_

#include "base/macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/views_export.h"

namespace views {

enum InsetsMetric {
  // Embedders can extend this enum with additional values that are understood
  // by the LayoutProvider implementation. Embedders define enum values from
  // VIEWS_INSETS_END. Values named beginning with "INSETS_" represent the
  // actual Insets: the rest are markers.
  VIEWS_INSETS_START = 0,

  // The margins around the contents area of a bubble (popover)-style dialog.
  INSETS_BUBBLE_CONTENTS = VIEWS_INSETS_START,
  // The margins around the button row of a dialog.
  INSETS_DIALOG_BUTTON,
  // The margins around the icon/title of a dialog.
  INSETS_DIALOG_TITLE,
  // The margins that should be applied around a panel GridLayout.
  INSETS_PANEL,
  // Padding to add to vector image buttons to increase their click and touch
  // target size.
  INSETS_VECTOR_IMAGE_BUTTON,

  // Embedders must start Insets enum values from this value.
  VIEWS_INSETS_END,

  // All Insets enum values must be below this value.
  VIEWS_INSETS_MAX = 0x1000
};

enum DistanceMetric {
  // DistanceMetric enum values must always be greater than any InsetsMetric
  // value. This allows the code to verify at runtime that arguments of the
  // two types have not been interchanged.
  VIEWS_DISTANCE_START = VIEWS_INSETS_MAX,

  // The default padding to add on each side of a button's label.
  DISTANCE_BUTTON_HORIZONTAL_PADDING = VIEWS_DISTANCE_START,
  // The distance between a dialog's edge and the close button in the upper
  // trailing corner.
  DISTANCE_CLOSE_BUTTON_MARGIN,
  // The default minimum width of a dialog button.
  DISTANCE_DIALOG_BUTTON_MINIMUM_WIDTH,
  // The spacing between a pair of related horizontal buttons, used for
  // dialog layout.
  DISTANCE_RELATED_BUTTON_HORIZONTAL,
  // Horizontal spacing between controls that are logically related.
  DISTANCE_RELATED_CONTROL_HORIZONTAL,
  // The spacing between a pair of related vertical controls, used for
  // dialog layout.
  DISTANCE_RELATED_CONTROL_VERTICAL,

  // Embedders must start DistanceMetric enum values from here.
  VIEWS_DISTANCE_END
};

class VIEWS_EXPORT LayoutProvider {
 public:
  LayoutProvider();
  virtual ~LayoutProvider();

  // This should never return nullptr.
  static LayoutProvider* Get();

  // Returns the insets metric according to the given enumeration element.
  virtual gfx::Insets GetInsetsMetric(int metric) const;

  // Returns the distance metric between elements according to the given
  // enumeration element.
  virtual int GetDistanceMetric(int metric) const;

  // Returns the TypographyProvider, used to configure text properties such as
  // font, weight, color, size, and line height. Never null.
  virtual const TypographyProvider& GetTypographyProvider() const;

 private:
  DefaultTypographyProvider typography_provider_;

  DISALLOW_COPY_AND_ASSIGN(LayoutProvider);
};

}  // namespace views

#endif  // UI_VIEWS_LAYOUT_LAYOUT_PROVIDER_H_
