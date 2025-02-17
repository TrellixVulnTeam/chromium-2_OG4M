// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_physical_box_fragment.h"

#include "core/layout/ng/ng_floating_object.h"

namespace blink {

NGPhysicalBoxFragment::NGPhysicalBoxFragment(
    LayoutObject* layout_object,
    NGPhysicalSize size,
    NGPhysicalSize overflow,
    Vector<RefPtr<NGPhysicalFragment>>& children,
    Vector<RefPtr<NGFloatingObject>>& positioned_floats,
    const WTF::Optional<NGLogicalOffset>& bfc_offset,
    const NGMarginStrut& end_margin_strut,
    RefPtr<NGBreakToken> break_token)
    : NGPhysicalFragment(layout_object,
                         size,
                         kFragmentBox,
                         std::move(break_token)),
      overflow_(overflow),
      positioned_floats_(positioned_floats),
      bfc_offset_(bfc_offset),
      end_margin_strut_(end_margin_strut) {
  children_.Swap(children);
}

}  // namespace blink
