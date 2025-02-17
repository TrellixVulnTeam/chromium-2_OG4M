// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_inline_node.h"

#include "core/layout/BidiRun.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutText.h"
#include "core/layout/line/LineInfo.h"
#include "core/layout/line/RootInlineBox.h"
#include "core/layout/ng/inline/ng_bidi_paragraph.h"
#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_layout_algorithm.h"
#include "core/layout/ng/inline/ng_layout_inline_items_builder.h"
#include "core/layout/ng/inline/ng_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/inline/ng_text_fragment.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_physical_box_fragment.h"
#include "core/style/ComputedStyle.h"
#include "platform/fonts/CharacterRange.h"
#include "platform/fonts/shaping/CachingWordShapeIterator.h"
#include "platform/fonts/shaping/CachingWordShaper.h"
#include "platform/fonts/shaping/HarfBuzzShaper.h"
#include "platform/fonts/shaping/ShapeResultBuffer.h"
#include "platform/wtf/text/CharacterNames.h"

namespace blink {

NGInlineNode::NGInlineNode(LayoutObject* start_inline, LayoutBlockFlow* block)
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyInline),
      start_inline_(start_inline),
      block_(block) {
  DCHECK(start_inline);
  DCHECK(block);
}

NGInlineNode::NGInlineNode()
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyInline),
      start_inline_(nullptr),
      block_(nullptr) {}

NGInlineNode::~NGInlineNode() {}

NGLayoutInlineItemRange NGInlineNode::Items(unsigned start, unsigned end) {
  return NGLayoutInlineItemRange(&items_, start, end);
}

void NGInlineNode::InvalidatePrepareLayout() {
  text_content_ = String();
  items_.Clear();
}

void NGInlineNode::PrepareLayout() {
  // Scan list of siblings collecting all in-flow non-atomic inlines. A single
  // NGInlineNode represent a collection of adjacent non-atomic inlines.
  CollectInlines(start_inline_, block_);
  if (is_bidi_enabled_)
    SegmentText();
  ShapeText();
}

// Depth-first-scan of all LayoutInline and LayoutText nodes that make up this
// NGInlineNode object. Collects LayoutText items, merging them up into the
// parent LayoutInline where possible, and joining all text content in a single
// string to allow bidi resolution and shaping of the entire block.
void NGInlineNode::CollectInlines(LayoutObject* start, LayoutBlockFlow* block) {
  DCHECK(text_content_.IsNull());
  DCHECK(items_.IsEmpty());
  NGLayoutInlineItemsBuilder builder(&items_);
  builder.EnterBlock(block->Style());
  LayoutObject* next_sibling = CollectInlines(start, block, &builder);
  builder.ExitBlock();

  text_content_ = builder.ToString();
  DCHECK(!next_sibling || !next_sibling->IsInline());
  next_sibling_ = next_sibling ? new NGBlockNode(next_sibling) : nullptr;
  is_bidi_enabled_ = !text_content_.IsEmpty() &&
                     !(text_content_.Is8Bit() && !builder.HasBidiControls());
}

LayoutObject* NGInlineNode::CollectInlines(
    LayoutObject* start,
    LayoutBlockFlow* block,
    NGLayoutInlineItemsBuilder* builder) {
  LayoutObject* node = start;
  while (node) {
    if (node->IsText()) {
      builder->SetIsSVGText(node->IsSVGInlineText());
      builder->Append(ToLayoutText(node)->GetText(), node->Style(), node);
      node->ClearNeedsLayout();

    } else if (node->IsFloating()) {
      // Add floats and positioned objects in the same way as atomic inlines.
      // Because these objects need positions, they will be handled in
      // NGInlineLayoutAlgorithm.
      builder->Append(NGLayoutInlineItem::kFloating,
                      kObjectReplacementCharacter, nullptr, node);
    } else if (node->IsOutOfFlowPositioned()) {
      builder->Append(NGLayoutInlineItem::kOutOfFlowPositioned,
                      kObjectReplacementCharacter, nullptr, node);

    } else if (!node->IsInline()) {
      // A block box found. End inline and transit to block layout.
      return node;

    } else {
      builder->EnterInline(node);

      // For atomic inlines add a unicode "object replacement character" to
      // signal the presence of a non-text object to the unicode bidi algorithm.
      if (node->IsAtomicInlineLevel()) {
        builder->Append(NGLayoutInlineItem::kAtomicInline,
                        kObjectReplacementCharacter, nullptr, node);
      }

      // Otherwise traverse to children if they exist.
      else if (LayoutObject* child = node->SlowFirstChild()) {
        node = child;
        continue;

      } else {
        // An empty inline node.
        node->ClearNeedsLayout();
      }

      builder->ExitInline(node);
    }

    // Find the next sibling, or parent, until we reach |block|.
    while (true) {
      if (LayoutObject* next = node->NextSibling()) {
        node = next;
        break;
      }
      node = node->Parent();
      if (node == block)
        return nullptr;
      DCHECK(node->IsInline());
      builder->ExitInline(node);
      node->ClearNeedsLayout();
    }
  }
  return nullptr;
}

void NGInlineNode::SegmentText() {
  // TODO(kojii): Move this to caller, this will be used again after line break.
  NGBidiParagraph bidi;
  text_content_.Ensure16Bit();
  if (!bidi.SetParagraph(text_content_, Style())) {
    // On failure, give up bidi resolving and reordering.
    is_bidi_enabled_ = false;
    return;
  }
  if (bidi.Direction() == UBIDI_LTR) {
    // All runs are LTR, no need to reorder.
    is_bidi_enabled_ = false;
    return;
  }

  unsigned item_index = 0;
  for (unsigned start = 0; start < text_content_.length();) {
    UBiDiLevel level;
    unsigned end = bidi.GetLogicalRun(start, &level);
    DCHECK_EQ(items_[item_index].start_offset_, start);
    item_index =
        NGLayoutInlineItem::SetBidiLevel(items_, item_index, end, level);
    start = end;
  }
  DCHECK_EQ(item_index, items_.size());
}

// Set bidi level to a list of NGLayoutInlineItem from |index| to the item that
// ends with |end_offset|.
// If |end_offset| is mid of an item, the item is split to ensure each item has
// one bidi level.
// @param items The list of NGLayoutInlineItem.
// @param index The first index of the list to set.
// @param end_offset The exclusive end offset to set.
// @param level The level to set.
// @return The index of the next item.
unsigned NGLayoutInlineItem::SetBidiLevel(Vector<NGLayoutInlineItem>& items,
                                          unsigned index,
                                          unsigned end_offset,
                                          UBiDiLevel level) {
  for (; items[index].end_offset_ < end_offset; index++)
    items[index].bidi_level_ = level;
  items[index].bidi_level_ = level;

  if (items[index].end_offset_ == end_offset) {
    // Let close items have the same bidi-level as the previous item.
    while (index + 1 < items.size() &&
           items[index + 1].Type() == NGLayoutInlineItem::kCloseTag) {
      items[++index].bidi_level_ = level;
    }
  } else {
    Split(items, index, end_offset);
  }

  return index + 1;
}

// Split |items[index]| to 2 items at |offset|.
// All properties other than offsets are copied to the new item and it is
// inserted at |items[index + 1]|.
// @param items The list of NGLayoutInlineItem.
// @param index The index to split.
// @param offset The offset to split at.
void NGLayoutInlineItem::Split(Vector<NGLayoutInlineItem>& items,
                               unsigned index,
                               unsigned offset) {
  DCHECK_GT(offset, items[index].start_offset_);
  DCHECK_LT(offset, items[index].end_offset_);
  items.insert(index + 1, items[index]);
  items[index].end_offset_ = offset;
  items[index + 1].start_offset_ = offset;
}

void NGLayoutInlineItem::SetOffset(unsigned start, unsigned end) {
  DCHECK_GE(end, start);
  start_offset_ = start;
  end_offset_ = end;
}

void NGLayoutInlineItem::SetEndOffset(unsigned end_offset) {
  DCHECK_GE(end_offset, start_offset_);
  end_offset_ = end_offset;
}

LayoutUnit NGLayoutInlineItem::InlineSize() const {
  if (Type() == NGLayoutInlineItem::kText)
    return LayoutUnit(shape_result_->Width());

  DCHECK_NE(Type(), NGLayoutInlineItem::kAtomicInline)
      << "Use NGInlineLayoutAlgorithm::InlineSize";
  // Bidi controls and out-of-flow objects do not have in-flow widths.
  return LayoutUnit();
}

LayoutUnit NGLayoutInlineItem::InlineSize(unsigned start, unsigned end) const {
  DCHECK_GE(start, StartOffset());
  DCHECK_LE(start, end);
  DCHECK_LE(end, EndOffset());

  if (start == end)
    return LayoutUnit();
  if (start == start_offset_ && end == end_offset_)
    return InlineSize();

  DCHECK_EQ(Type(), NGLayoutInlineItem::kText);
  return LayoutUnit(ShapeResultBuffer::GetCharacterRange(
                        shape_result_, Direction(), shape_result_->Width(),
                        start - StartOffset(), end - StartOffset())
                        .Width());
}

void NGLayoutInlineItem::GetFallbackFonts(
    HashSet<const SimpleFontData*>* fallback_fonts,
    unsigned start,
    unsigned end) const {
  DCHECK_GE(start, StartOffset());
  DCHECK_LE(start, end);
  DCHECK_LE(end, EndOffset());

  // TODO(kojii): Implement |start| and |end|.
  shape_result_->FallbackFonts(fallback_fonts);
}

void NGInlineNode::ShapeText() {
  // TODO(eae): Add support for shaping latin-1 text?
  text_content_.Ensure16Bit();

  // Shape each item with the full context of the entire node.
  HarfBuzzShaper shaper(text_content_.Characters16(), text_content_.length());
  for (auto& item : items_) {
    if (item.Type() != NGLayoutInlineItem::kText)
      continue;

    item.shape_result_ =
        shaper.Shape(&item.Style()->GetFont(), item.Direction(),
                     item.StartOffset(), item.EndOffset());
  }
}

RefPtr<NGLayoutResult> NGInlineNode::Layout(NGConstraintSpace* constraint_space,
                                            NGBreakToken* break_token) {
  // TODO(kojii): Invalidate PrepareLayout() more efficiently.
  InvalidatePrepareLayout();
  PrepareLayout();

  NGInlineLayoutAlgorithm algorithm(this, constraint_space,
                                    ToNGInlineBreakToken(break_token));
  RefPtr<NGLayoutResult> result = algorithm.Layout();
  CopyFragmentDataToLayoutBox(*constraint_space, result.Get());
  return result;
}

MinMaxContentSize NGInlineNode::ComputeMinMaxContentSize() {
  if (!IsPrepareLayoutFinished())
    PrepareLayout();

  // Compute the max of inline sizes of all line boxes with 0 available inline
  // size. This gives the min-content, the width where lines wrap at every break
  // opportunity.
  NGWritingMode writing_mode =
      FromPlatformWritingMode(Style().GetWritingMode());
  RefPtr<NGConstraintSpace> constraint_space =
      NGConstraintSpaceBuilder(writing_mode)
          .SetTextDirection(Style().Direction())
          .SetAvailableSize({LayoutUnit(), NGSizeIndefinite})
          .ToConstraintSpace(writing_mode);
  NGInlineLayoutAlgorithm algorithm(this, constraint_space.Get());
  return algorithm.ComputeMinMaxContentSizeByLayout();
}

NGLayoutInputNode* NGInlineNode::NextSibling() {
  if (!IsPrepareLayoutFinished())
    PrepareLayout();
  return next_sibling_;
}

LayoutObject* NGInlineNode::GetLayoutObject() {
  return GetLayoutBlockFlow();
}

void NGInlineNode::CopyFragmentDataToLayoutBox(
    const NGConstraintSpace& constraint_space,
    NGLayoutResult* layout_result) {
  LayoutBlockFlow* block_flow = GetLayoutBlockFlow();
  block_flow->DeleteLineBoxTree();

  Vector<NGLayoutInlineItem>& items = Items();
  Vector<unsigned, 32> text_offsets(items.size());
  GetLayoutTextOffsets(&text_offsets);

  FontBaseline baseline_type =
      IsHorizontalWritingMode(constraint_space.WritingMode())
          ? FontBaseline::kAlphabeticBaseline
          : FontBaseline::kIdeographicBaseline;

  Vector<const NGPhysicalFragment*, 32> fragments_for_bidi_runs;
  fragments_for_bidi_runs.ReserveInitialCapacity(items.size());
  BidiRunList<BidiRun> bidi_runs;
  LineInfo line_info;
  NGPhysicalBoxFragment* box_fragment =
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get());
  for (const auto& container_child : box_fragment->Children()) {
    NGPhysicalLineBoxFragment* physical_line_box =
        ToNGPhysicalLineBoxFragment(container_child.Get());
    // Create a BidiRunList for this line.
    for (const auto& line_child : physical_line_box->Children()) {
      const auto* text_fragment = ToNGPhysicalTextFragment(line_child.Get());
      const NGLayoutInlineItem& item = items[text_fragment->ItemIndex()];
      BidiRun* run;
      if (item.Type() == NGLayoutInlineItem::kText) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsText());
        unsigned text_offset = text_offsets[text_fragment->ItemIndex()];
        run = new BidiRun(text_fragment->StartOffset() - text_offset,
                          text_fragment->EndOffset() - text_offset,
                          item.BidiLevel(), LineLayoutItem(layout_object));
        layout_object->ClearNeedsLayout();
      } else if (item.Type() == NGLayoutInlineItem::kAtomicInline) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsAtomicInlineLevel());
        run =
            new BidiRun(0, 1, item.BidiLevel(), LineLayoutItem(layout_object));
      } else {
        continue;
      }
      bidi_runs.AddRun(run);
      fragments_for_bidi_runs.push_back(text_fragment);
    }
    // TODO(kojii): bidi needs to find the logical last run.
    bidi_runs.SetLogicallyLastRun(bidi_runs.LastRun());

    // Create a RootInlineBox from BidiRunList. InlineBoxes created for the
    // RootInlineBox are set to Bidirun::m_box.
    line_info.SetEmpty(false);
    // TODO(kojii): Implement setFirstLine, LastLine, etc.
    RootInlineBox* root_line_box =
        block_flow->ConstructLine(bidi_runs, line_info);

    // Copy fragments data to InlineBoxes.
    DCHECK_EQ(fragments_for_bidi_runs.size(), bidi_runs.RunCount());
    BidiRun* run = bidi_runs.FirstRun();
    for (auto* physical_fragment : fragments_for_bidi_runs) {
      DCHECK(run);
      NGTextFragment fragment(constraint_space.WritingMode(),
                              ToNGPhysicalTextFragment(physical_fragment));
      InlineBox* inline_box = run->box_;
      inline_box->SetLogicalWidth(fragment.InlineSize());
      inline_box->SetLogicalLeft(fragment.InlineOffset());
      inline_box->SetLogicalTop(fragment.BlockOffset());
      if (inline_box->GetLineLayoutItem().IsBox()) {
        LineLayoutBox box(inline_box->GetLineLayoutItem());
        box.SetLocation(inline_box->Location());
      }
      run = run->Next();
    }
    DCHECK(!run);

    // Copy to RootInlineBox.
    NGLineBoxFragment line_box(constraint_space.WritingMode(),
                               physical_line_box);
    root_line_box->SetLogicalWidth(line_box.InlineSize());
    LayoutUnit line_top = line_box.BlockOffset();
    root_line_box->SetLogicalTop(line_top);
    NGLineHeightMetrics line_metrics(Style(), baseline_type);
    const NGLineHeightMetrics& max_with_leading = physical_line_box->Metrics();
    LayoutUnit baseline = line_top + line_metrics.ascent;
    root_line_box->SetLineTopBottomPositions(
        line_top, baseline + line_metrics.descent,
        baseline - max_with_leading.ascent,
        baseline + max_with_leading.descent);

    bidi_runs.DeleteRuns();
    fragments_for_bidi_runs.Clear();
  }
}

// Compute the delta of text offsets between NGInlineNode and LayoutText.
// This map is needed to produce InlineTextBox since its offsets are to
// LayoutText.
// TODO(kojii): Since NGInlineNode has text after whitespace collapsed, the
// length may not match with LayoutText. This function updates LayoutText to
// match, but this needs more careful coding, if we keep copying to layoutobject
// tree.
void NGInlineNode::GetLayoutTextOffsets(
    Vector<unsigned, 32>* text_offsets_out) {
  LayoutText* current_text = nullptr;
  unsigned current_offset = 0;
  for (unsigned i = 0; i < items_.size(); i++) {
    const NGLayoutInlineItem& item = items_[i];
    LayoutObject* next_object = item.GetLayoutObject();
    LayoutText* next_text = next_object && next_object->IsText()
                                ? ToLayoutText(next_object)
                                : nullptr;
    if (next_text != current_text) {
      if (current_text &&
          current_text->TextLength() != item.StartOffset() - current_offset) {
        current_text->SetTextInternal(
            Text(current_offset, item.StartOffset()).ToString().Impl());
      }
      current_text = next_text;
      current_offset = item.StartOffset();
    }
    (*text_offsets_out)[i] = current_offset;
  }
  if (current_text &&
      current_text->TextLength() != text_content_.length() - current_offset) {
    current_text->SetTextInternal(
        Text(current_offset, text_content_.length()).ToString().Impl());
  }
}

DEFINE_TRACE(NGInlineNode) {
  visitor->Trace(next_sibling_);
  NGLayoutInputNode::Trace(visitor);
}

NGLayoutInlineItemRange::NGLayoutInlineItemRange(
    Vector<NGLayoutInlineItem>* items,
    unsigned start_index,
    unsigned end_index)
    : start_item_(&(*items)[start_index]),
      size_(end_index - start_index),
      start_index_(start_index) {
  CHECK_LE(start_index, end_index);
  CHECK_LE(end_index, items->size());
}

}  // namespace blink
