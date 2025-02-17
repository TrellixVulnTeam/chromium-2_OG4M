// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGInlineNode_h
#define NGInlineNode_h

#include "core/CoreExport.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/ng/ng_layout_input_node.h"
#include "platform/fonts/FontFallbackPriority.h"
#include "platform/fonts/shaping/ShapeResult.h"
#include "platform/heap/Handle.h"
#include "platform/text/TextDirection.h"
#include "platform/wtf/text/WTFString.h"

#include <unicode/ubidi.h>
#include <unicode/uscript.h>

namespace blink {

class ComputedStyle;
class LayoutBlockFlow;
class LayoutObject;
class LayoutUnit;
struct MinMaxContentSize;
class NGConstraintSpace;
class NGLayoutInlineItem;
class NGLayoutInlineItemRange;
class NGLayoutInlineItemsBuilder;
class NGLayoutResult;

// Represents an anonymous block box to be laid out, that contains consecutive
// inline nodes and their descendants.
class CORE_EXPORT NGInlineNode : public NGLayoutInputNode {
 public:
  NGInlineNode(LayoutObject* start_inline, LayoutBlockFlow*);
  ~NGInlineNode() override;

  LayoutBlockFlow* GetLayoutBlockFlow() const { return block_; }
  const ComputedStyle& Style() const override { return block_->StyleRef(); }
  NGLayoutInputNode* NextSibling() override;

  RefPtr<NGLayoutResult> Layout(NGConstraintSpace*, NGBreakToken*) override;
  LayoutObject* GetLayoutObject() override;

  // Computes the value of min-content and max-content for this anonymous block
  // box. min-content is the inline size when lines wrap at every break
  // opportunity, and max-content is when lines do not wrap at all.
  MinMaxContentSize ComputeMinMaxContentSize() override;

  // Copy fragment data of all lines to LayoutBlockFlow.
  void CopyFragmentDataToLayoutBox(const NGConstraintSpace&, NGLayoutResult*);

  // Instruct to re-compute |PrepareLayout| on the next layout.
  void InvalidatePrepareLayout();

  const String& Text() const { return text_content_; }
  StringView Text(unsigned start_offset, unsigned end_offset) const {
    return StringView(text_content_, start_offset, end_offset - start_offset);
  }

  Vector<NGLayoutInlineItem>& Items() { return items_; }
  NGLayoutInlineItemRange Items(unsigned start_index, unsigned end_index);

  void GetLayoutTextOffsets(Vector<unsigned, 32>*);

  bool IsBidiEnabled() const { return is_bidi_enabled_; }

  void AssertOffset(unsigned index, unsigned offset) const;
  void AssertEndOffset(unsigned index, unsigned offset) const;

  DECLARE_VIRTUAL_TRACE();

 protected:
  NGInlineNode();  // This constructor is only for testing.

  // Prepare inline and text content for layout. Must be called before
  // calling the Layout method.
  void PrepareLayout();
  bool IsPrepareLayoutFinished() const { return !text_content_.IsNull(); }

  void CollectInlines(LayoutObject* start, LayoutBlockFlow*);
  LayoutObject* CollectInlines(LayoutObject* start,
                               LayoutBlockFlow*,
                               NGLayoutInlineItemsBuilder*);
  void SegmentText();
  void ShapeText();

  LayoutObject* start_inline_;
  LayoutBlockFlow* block_;
  Member<NGLayoutInputNode> next_sibling_;

  // Text content for all inline items represented by a single NGInlineNode
  // instance. Encoded either as UTF-16 or latin-1 depending on content.
  String text_content_;
  Vector<NGLayoutInlineItem> items_;

  // TODO(kojii): This should move to somewhere else when we move PrepareLayout
  // to the correct place.
  bool is_bidi_enabled_ = false;
};

// Class representing a single text node or styled inline element with text
// content segmented by style, text direction, sideways rotation, font fallback
// priority (text, symbol, emoji, etc) and script (but not by font).
// In this representation TextNodes are merged up into their parent inline
// element where possible.
class NGLayoutInlineItem {
 public:
  enum NGLayoutInlineItemType {
    kText,
    kAtomicInline,
    kOpenTag,
    kCloseTag,
    kFloating,
    kOutOfFlowPositioned,
    kBidiControl
    // When adding new values, make sure the bit size of |type_| is large
    // enough to store.
  };

  NGLayoutInlineItem(NGLayoutInlineItemType type,
                     unsigned start,
                     unsigned end,
                     const ComputedStyle* style = nullptr,
                     LayoutObject* layout_object = nullptr)
      : start_offset_(start),
        end_offset_(end),
        bidi_level_(UBIDI_LTR),
        script_(USCRIPT_INVALID_CODE),
        fallback_priority_(FontFallbackPriority::kInvalid),
        rotate_sideways_(false),
        style_(style),
        layout_object_(layout_object),
        type_(type) {
    DCHECK_GE(end, start);
  }

  NGLayoutInlineItemType Type() const {
    return static_cast<NGLayoutInlineItemType>(type_);
  }

  unsigned StartOffset() const { return start_offset_; }
  unsigned EndOffset() const { return end_offset_; }
  unsigned Length() const { return end_offset_ - start_offset_; }
  TextDirection Direction() const {
    return bidi_level_ & 1 ? TextDirection::kRtl : TextDirection::kLtr;
  }
  UBiDiLevel BidiLevel() const { return bidi_level_; }
  UScriptCode GetScript() const { return script_; }
  const ComputedStyle* Style() const { return style_; }
  LayoutObject* GetLayoutObject() const { return layout_object_; }

  void SetOffset(unsigned start, unsigned end);
  void SetEndOffset(unsigned);

  LayoutUnit InlineSize() const;
  LayoutUnit InlineSize(unsigned start, unsigned end) const;

  void GetFallbackFonts(HashSet<const SimpleFontData*>*,
                        unsigned start,
                        unsigned end) const;

  static void Split(Vector<NGLayoutInlineItem>&,
                    unsigned index,
                    unsigned offset);
  static unsigned SetBidiLevel(Vector<NGLayoutInlineItem>&,
                               unsigned index,
                               unsigned end_offset,
                               UBiDiLevel);

  void AssertOffset(unsigned offset) const;
  void AssertEndOffset(unsigned offset) const;

 private:
  unsigned start_offset_;
  unsigned end_offset_;
  UBiDiLevel bidi_level_;
  UScriptCode script_;
  FontFallbackPriority fallback_priority_;
  bool rotate_sideways_;
  const ComputedStyle* style_;
  RefPtr<const ShapeResult> shape_result_;
  LayoutObject* layout_object_;

  unsigned type_ : 3;

  friend class NGInlineNode;
};

inline void NGLayoutInlineItem::AssertOffset(unsigned offset) const {
  DCHECK((offset >= start_offset_ && offset < end_offset_) ||
         (offset == start_offset_ && start_offset_ == end_offset_));
}

inline void NGLayoutInlineItem::AssertEndOffset(unsigned offset) const {
  DCHECK_GE(offset, start_offset_);
  DCHECK_LE(offset, end_offset_);
}

inline void NGInlineNode::AssertOffset(unsigned index, unsigned offset) const {
  items_[index].AssertOffset(offset);
}

inline void NGInlineNode::AssertEndOffset(unsigned index,
                                          unsigned offset) const {
  items_[index].AssertEndOffset(offset);
}

DEFINE_TYPE_CASTS(NGInlineNode,
                  NGLayoutInputNode,
                  node,
                  node->IsInline(),
                  node.IsInline());

// A vector-like object that points to a subset of an array of
// |NGLayoutInlineItem|.
// The source vector must keep alive and must not resize while this object
// is alive.
class NGLayoutInlineItemRange {
  STACK_ALLOCATED();

 public:
  NGLayoutInlineItemRange(Vector<NGLayoutInlineItem>*,
                          unsigned start_index,
                          unsigned end_index);

  unsigned StartIndex() const { return start_index_; }
  unsigned EndIndex() const { return start_index_ + size_; }
  unsigned Size() const { return size_; }

  NGLayoutInlineItem& operator[](unsigned index) {
    CHECK_LT(index, size_);
    return start_item_[index];
  }
  const NGLayoutInlineItem& operator[](unsigned index) const {
    CHECK_LT(index, size_);
    return start_item_[index];
  }

  using iterator = NGLayoutInlineItem*;
  using const_iterator = const NGLayoutInlineItem*;
  iterator begin() { return start_item_; }
  iterator end() { return start_item_ + size_; }
  const_iterator begin() const { return start_item_; }
  const_iterator end() const { return start_item_ + size_; }

 private:
  NGLayoutInlineItem* start_item_;
  unsigned size_;
  unsigned start_index_;
};

}  // namespace blink

#endif  // NGInlineNode_h
