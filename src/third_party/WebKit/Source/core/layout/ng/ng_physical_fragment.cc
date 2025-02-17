// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_physical_fragment.h"

#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/ng_break_token.h"
#include "core/layout/ng/ng_physical_box_fragment.h"

namespace blink {

NGPhysicalFragment::NGPhysicalFragment(LayoutObject* layout_object,
                                       NGPhysicalSize size,
                                       NGFragmentType type,
                                       RefPtr<NGBreakToken> break_token)
    : layout_object_(layout_object),
      size_(size),
      break_token_(std::move(break_token)),
      type_(type),
      is_placed_(false) {}

void NGPhysicalFragment::Destroy() const {
  switch (Type()) {
    case kFragmentBox:
      delete static_cast<const NGPhysicalBoxFragment*>(this);
      break;
    case kFragmentText:
      delete static_cast<const NGPhysicalTextFragment*>(this);
      break;
    case kFragmentLineBox:
      delete static_cast<const NGPhysicalLineBoxFragment*>(this);
      break;
    default:
      NOTREACHED();
      break;
  }
}

const ComputedStyle& NGPhysicalFragment::Style() const {
  DCHECK(layout_object_);
  return layout_object_->StyleRef();
}

String NGPhysicalFragment::ToString() const {
  return String::Format("Type: '%d' Size: '%s' Offset: '%s' Placed: '%d'",
                        Type(), Size().ToString().Ascii().Data(),
                        Offset().ToString().Ascii().Data(), IsPlaced());
}

}  // namespace blink
