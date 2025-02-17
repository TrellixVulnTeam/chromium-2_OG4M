// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_floats_utils.h"

#include "core/layout/ng/ng_box_fragment.h"

namespace blink {
namespace {

// Adjusts the provided offset to the top edge alignment rule.
// Top edge alignment rule: the outer top of a floating box may not be higher
// than the outer top of any block or floated box generated by an element
// earlier in the source document.
NGLogicalOffset AdjustToTopEdgeAlignmentRule(const NGConstraintSpace& space,
                                             const NGLogicalOffset& offset) {
  NGLogicalOffset adjusted_offset = offset;
  LayoutUnit& adjusted_block_offset = adjusted_offset.block_offset;
  if (space.Exclusions()->last_left_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_left_float->rect.BlockStartOffset());
  if (space.Exclusions()->last_right_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_right_float->rect.BlockStartOffset());
  return adjusted_offset;
}

// Finds a layout opportunity for the fragment.
// It iterates over all layout opportunities in the constraint space and returns
// the first layout opportunity that is wider than the fragment or returns the
// last one which is always the widest.
//
// @param space Constraint space that is used to find layout opportunity for
//              the fragment.
// @param fragment Fragment that needs to be placed.
// @param floating_object Floating object for which we need to find a layout
//                        opportunity.
// @return Layout opportunity for the fragment.
const NGLayoutOpportunity FindLayoutOpportunityForFragment(
    const NGConstraintSpace* space,
    const NGFragment& fragment,
    const NGFloatingObject* floating_object) {
  NGLogicalOffset adjusted_origin_point =
      AdjustToTopEdgeAlignmentRule(*space, floating_object->origin_offset);

  NGLayoutOpportunityIterator opportunity_iter(
      space, floating_object->available_size, adjusted_origin_point);
  NGLayoutOpportunity opportunity;
  NGLayoutOpportunity opportunity_candidate = opportunity_iter.Next();

  NGBoxStrut margins = floating_object->margins;
  while (!opportunity_candidate.IsEmpty()) {
    opportunity = opportunity_candidate;
    // Checking opportunity's block size is not necessary as a float cannot be
    // positioned on top of another float inside of the same constraint space.
    auto fragment_inline_size = fragment.InlineSize() + margins.InlineSum();
    if (opportunity.size.inline_size >= fragment_inline_size)
      break;

    opportunity_candidate = opportunity_iter.Next();
  }
  return opportunity;
}

// Calculates the logical offset for opportunity.
NGLogicalOffset CalculateLogicalOffsetForOpportunity(
    const NGLayoutOpportunity& opportunity,
    const LayoutUnit float_offset,
    const NGFloatingObject* floating_object) {
  DCHECK(floating_object);
  auto margins = floating_object->margins;
  // Adjust to child's margin.
  NGLogicalOffset result = margins.InlineBlockStartOffset();

  // Offset from the opportunity's block/inline start.
  result += opportunity.offset;

  // Adjust to float: right offset if needed.
  result.inline_offset += float_offset;

  result -= floating_object->from_offset;
  return result;
}

// Creates an exclusion from the fragment that will be placed in the provided
// layout opportunity.
NGExclusion CreateExclusion(const NGFragment& fragment,
                            const NGLayoutOpportunity& opportunity,
                            const LayoutUnit float_offset,
                            const NGBoxStrut& margins,
                            NGExclusion::Type exclusion_type) {
  NGExclusion exclusion;
  exclusion.type = exclusion_type;
  NGLogicalRect& rect = exclusion.rect;
  rect.offset = opportunity.offset;
  rect.offset.inline_offset += float_offset;

  rect.size.inline_size = fragment.InlineSize() + margins.InlineSum();
  rect.size.block_size = fragment.BlockSize() + margins.BlockSum();
  return exclusion;
}

// Updates the Floating Object's left offset from the provided parent_space
// and {@code floating_object}'s space and margins.
void UpdateFloatingObjectLeftOffset(const NGConstraintSpace& new_parent_space,
                                    const NGLogicalOffset& float_logical_offset,
                                    NGFloatingObject* floating_object) {
  DCHECK(floating_object);
  // TODO(glebl): We should use physical offset here.
  floating_object->left_offset = floating_object->from_offset.inline_offset -
                                 new_parent_space.BfcOffset().inline_offset +
                                 float_logical_offset.inline_offset;
}
}  // namespace

NGLogicalOffset PositionFloat(NGFloatingObject* floating_object,
                              NGConstraintSpace* new_parent_space) {
  DCHECK(floating_object);
  DCHECK(floating_object->fragment) << "Fragment cannot be null here";

  // TODO(ikilpatrick): The writing mode switching here looks wrong.
  NGBoxFragment float_fragment(
      floating_object->writing_mode,
      ToNGPhysicalBoxFragment(floating_object->fragment.Get()));

  // Find a layout opportunity that will fit our float.
  NGLayoutOpportunity opportunity = FindLayoutOpportunityForFragment(
      new_parent_space, float_fragment, floating_object);

  // TODO(glebl): This should check for infinite opportunity instead.
  if (opportunity.IsEmpty()) {
    // Because of the implementation specific of the layout opportunity iterator
    // an empty opportunity can mean 2 things:
    // - search for layout opportunities is exhausted.
    // - opportunity has an infinite size. That's because CS is infinite.
    opportunity = NGLayoutOpportunity(
        NGLogicalOffset(),
        NGLogicalSize(float_fragment.InlineSize(), float_fragment.BlockSize()));
  }

  // Calculate the float offset if needed.
  LayoutUnit float_offset;
  if (floating_object->exclusion_type == NGExclusion::kFloatRight) {
    LayoutUnit float_margin_box_inline_size =
        float_fragment.InlineSize() + floating_object->margins.InlineSum();
    float_offset = opportunity.size.inline_size - float_margin_box_inline_size;
  }

  // Add the float as an exclusion.
  const NGExclusion exclusion = CreateExclusion(
      float_fragment, opportunity, float_offset, floating_object->margins,
      floating_object->exclusion_type);
  new_parent_space->AddExclusion(exclusion);

  NGLogicalOffset logical_offset = CalculateLogicalOffsetForOpportunity(
      opportunity, float_offset, floating_object);
  UpdateFloatingObjectLeftOffset(*new_parent_space, logical_offset,
                                 floating_object);
  return logical_offset;
}

void PositionPendingFloats(const LayoutUnit& origin_block_offset,
                           NGConstraintSpace* space,
                           NGFragmentBuilder* builder) {
  DCHECK(builder) << "Builder cannot be null here";
  DCHECK(builder->BfcOffset()) << "Parent BFC offset should be known here";
  LayoutUnit bfc_block_offset = builder->BfcOffset().value().block_offset;

  for (auto& floating_object : builder->UnpositionedFloats()) {
    floating_object->origin_offset.block_offset = origin_block_offset;
    floating_object->from_offset.block_offset = bfc_block_offset;

    NGLogicalOffset offset = PositionFloat(floating_object.Get(), space);
    builder->AddFloatingObject(floating_object, offset);
  }
  builder->MutableUnpositionedFloats().Clear();
}

}  // namespace blink
