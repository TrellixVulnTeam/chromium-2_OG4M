// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/PropertyTreeState.h"

#include "platform/graphics/paint/GeometryMapper.h"

namespace blink {

const PropertyTreeState& PropertyTreeState::Root() {
  DEFINE_STATIC_LOCAL(
      std::unique_ptr<PropertyTreeState>, root,
      (WTF::WrapUnique(new PropertyTreeState(
          TransformPaintPropertyNode::Root(), ClipPaintPropertyNode::Root(),
          EffectPaintPropertyNode::Root()))));
  return *root;
}

bool PropertyTreeState::HasDirectCompositingReasons() const {
  switch (GetInnermostNode()) {
    case kTransform:
      return Transform()->HasDirectCompositingReasons();
    case kClip:
      return Clip()->HasDirectCompositingReasons();
    case kEffect:
      return Effect()->HasDirectCompositingReasons();
    default:
      return false;
  }
}

template <typename PropertyNode>
bool IsAncestorOf(const PropertyNode* ancestor, const PropertyNode* child) {
  while (child && child != ancestor) {
    child = child->Parent();
  }
  return child == ancestor;
}

const CompositorElementId PropertyTreeState::GetCompositorElementId() const {
// The effect or transform nodes could have a compositor element id. The order
// doesn't matter as the element id should be the same on all that have a
// non-default CompositorElementId.
#if DCHECK_IS_ON()
  CompositorElementId expected_element_id;
  if (CompositorElementId actual_element_id =
          Effect()->GetCompositorElementId()) {
    expected_element_id = actual_element_id;
  }
  if (CompositorElementId actual_element_id =
          Transform()->GetCompositorElementId()) {
    if (expected_element_id)
      DCHECK_EQ(expected_element_id, actual_element_id);
  }
#endif
  if (Effect()->GetCompositorElementId())
    return Effect()->GetCompositorElementId();
  if (Transform()->GetCompositorElementId())
    return Transform()->GetCompositorElementId();
  return CompositorElementId();
}

PropertyTreeState::InnermostNode PropertyTreeState::GetInnermostNode() const {
  // TODO(chrishtr): this is very inefficient when innermostNode() is called
  // repeatedly.
  bool clip_transform_strict_ancestor_of_transform =
      clip_->LocalTransformSpace() != transform_.Get() &&
      IsAncestorOf<TransformPaintPropertyNode>(clip_->LocalTransformSpace(),
                                               transform_.Get());
  bool effect_transform_strict_ancestor_of_transform =
      effect_->LocalTransformSpace() != transform_.Get() &&
      IsAncestorOf<TransformPaintPropertyNode>(effect_->LocalTransformSpace(),
                                               transform_.Get());

  if (!transform_->IsRoot() && clip_transform_strict_ancestor_of_transform &&
      effect_transform_strict_ancestor_of_transform)
    return kTransform;

  bool clip_ancestor_of_effect =
      IsAncestorOf<ClipPaintPropertyNode>(clip_.Get(), effect_->OutputClip());

  if (!effect_->IsRoot() && clip_ancestor_of_effect) {
    return kEffect;
  }
  if (!clip_->IsRoot())
    return kClip;
  return kNone;
}

const PropertyTreeState* PropertyTreeStateIterator::Next() {
  switch (properties_.GetInnermostNode()) {
    case PropertyTreeState::kTransform:
      properties_.SetTransform(properties_.Transform()->Parent());
      return &properties_;
    case PropertyTreeState::kClip:
      properties_.SetClip(properties_.Clip()->Parent());
      return &properties_;
    case PropertyTreeState::kEffect:
      properties_.SetEffect(properties_.Effect()->Parent());
      return &properties_;
    case PropertyTreeState::kNone:
      return nullptr;
  }
  return nullptr;
}

#if DCHECK_IS_ON()

String PropertyTreeState::ToTreeString() const {
  return Transform()->ToTreeString() + "\n" + Clip()->ToTreeString() + "\n" +
         Effect()->ToTreeString();
}

#endif

}  // namespace blink
