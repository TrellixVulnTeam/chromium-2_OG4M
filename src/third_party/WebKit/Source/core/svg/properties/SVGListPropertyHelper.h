/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SVGListPropertyHelper_h
#define SVGListPropertyHelper_h

#include "bindings/core/v8/ExceptionMessages.h"
#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/ExceptionCode.h"
#include "core/svg/SVGAnimationElement.h"
#include "core/svg/properties/SVGPropertyHelper.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Vector.h"

namespace blink {

// This is an implementation of the SVG*List property spec:
// http://www.w3.org/TR/SVG/single-page.html#types-InterfaceSVGLengthList
template <typename Derived, typename ItemProperty>
class SVGListPropertyHelper : public SVGPropertyHelper<Derived> {
 public:
  typedef ItemProperty ItemPropertyType;

  SVGListPropertyHelper() {}

  ~SVGListPropertyHelper() {}

  // used from Blink C++ code:

  ItemPropertyType* at(size_t index) {
    DCHECK_LT(index, values_.size());
    DCHECK_EQ(values_.at(index)->OwnerList(), this);
    return values_.at(index).Get();
  }

  const ItemPropertyType* at(size_t index) const {
    return const_cast<SVGListPropertyHelper<Derived, ItemProperty>*>(this)->at(
        index);
  }

  class ConstIterator {
    STACK_ALLOCATED();

   private:
    typedef typename HeapVector<Member<ItemPropertyType>>::const_iterator
        WrappedType;

   public:
    ConstIterator(WrappedType it) : it_(it) {}

    ConstIterator& operator++() {
      ++it_;
      return *this;
    }

    bool operator==(const ConstIterator& o) const { return it_ == o.it_; }
    bool operator!=(const ConstIterator& o) const { return it_ != o.it_; }

    ItemPropertyType* operator*() { return *it_; }
    ItemPropertyType* operator->() { return *it_; }

   private:
    WrappedType it_;
  };

  ConstIterator begin() const { return ConstIterator(values_.begin()); }

  ConstIterator LastAppended() const {
    return ConstIterator(values_.begin() + values_.size() - 1);
  }

  ConstIterator end() const { return ConstIterator(values_.end()); }

  void Append(ItemPropertyType* new_item) {
    DCHECK(new_item);
    values_.push_back(new_item);
    new_item->SetOwnerList(this);
  }

  bool operator==(const Derived&) const;
  bool operator!=(const Derived& other) const { return !(*this == other); }

  bool IsEmpty() const { return !length(); }

  virtual Derived* Clone() {
    Derived* svg_list = Derived::Create();
    svg_list->DeepCopy(static_cast<Derived*>(this));
    return svg_list;
  }

  // SVGList*Property DOM spec:

  size_t length() const { return values_.size(); }

  void Clear();

  ItemPropertyType* Initialize(ItemPropertyType*);
  ItemPropertyType* GetItem(size_t, ExceptionState&);
  ItemPropertyType* InsertItemBefore(ItemPropertyType*, size_t);
  ItemPropertyType* RemoveItem(size_t, ExceptionState&);
  ItemPropertyType* AppendItem(ItemPropertyType*);
  ItemPropertyType* ReplaceItem(ItemPropertyType*, size_t, ExceptionState&);

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(values_);
    SVGPropertyHelper<Derived>::Trace(visitor);
  }

 protected:
  void DeepCopy(Derived*);

  bool AdjustFromToListValues(Derived* from_list,
                              Derived* to_list,
                              float percentage,
                              AnimationMode);

  virtual ItemPropertyType* CreatePaddingItem() const {
    return ItemPropertyType::Create();
  }

 private:
  inline bool CheckIndexBound(size_t, ExceptionState&);
  size_t FindItem(ItemPropertyType*);

  HeapVector<Member<ItemPropertyType>> values_;
};

template <typename Derived, typename ItemProperty>
bool SVGListPropertyHelper<Derived, ItemProperty>::operator==(
    const Derived& other) const {
  if (length() != other.length())
    return false;

  size_t size = length();
  for (size_t i = 0; i < size; ++i) {
    if (*at(i) != *other.at(i))
      return false;
  }

  return true;
}

template <typename Derived, typename ItemProperty>
void SVGListPropertyHelper<Derived, ItemProperty>::Clear() {
  // detach all list items as they are no longer part of this list
  typename HeapVector<Member<ItemPropertyType>>::const_iterator it =
      values_.begin();
  typename HeapVector<Member<ItemPropertyType>>::const_iterator it_end =
      values_.end();
  for (; it != it_end; ++it) {
    DCHECK_EQ((*it)->OwnerList(), this);
    (*it)->SetOwnerList(nullptr);
  }

  values_.Clear();
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::Initialize(
    ItemProperty* new_item) {
  // Spec: Clears all existing current items from the list and re-initializes
  // the list to hold the single item specified by the parameter.
  Clear();
  Append(new_item);
  return new_item;
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::GetItem(
    size_t index,
    ExceptionState& exception_state) {
  if (!CheckIndexBound(index, exception_state))
    return nullptr;

  DCHECK_LT(index, values_.size());
  DCHECK_EQ(values_.at(index)->OwnerList(), this);
  return values_.at(index);
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::InsertItemBefore(
    ItemProperty* new_item,
    size_t index) {
  // Spec: If the index is greater than or equal to length, then the new item is
  // appended to the end of the list.
  if (index > values_.size())
    index = values_.size();

  // Spec: Inserts a new item into the list at the specified position. The index
  // of the item before which the new item is to be inserted. The first item is
  // number 0. If the index is equal to 0, then the new item is inserted at the
  // front of the list.
  values_.insert(index, new_item);
  new_item->SetOwnerList(this);

  return new_item;
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::RemoveItem(
    size_t index,
    ExceptionState& exception_state) {
  if (index >= values_.size()) {
    exception_state.ThrowDOMException(
        kIndexSizeError, ExceptionMessages::IndexExceedsMaximumBound(
                             "index", index, values_.size()));
    return nullptr;
  }
  DCHECK_EQ(values_.at(index)->OwnerList(), this);
  ItemPropertyType* old_item = values_.at(index);
  values_.erase(index);
  old_item->SetOwnerList(0);
  return old_item;
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::AppendItem(
    ItemProperty* new_item) {
  // Append the value and wrapper at the end of the list.
  Append(new_item);

  return new_item;
}

template <typename Derived, typename ItemProperty>
ItemProperty* SVGListPropertyHelper<Derived, ItemProperty>::ReplaceItem(
    ItemProperty* new_item,
    size_t index,
    ExceptionState& exception_state) {
  if (!CheckIndexBound(index, exception_state))
    return nullptr;

  if (values_.IsEmpty()) {
    // 'newItem' already lived in our list, we removed it, and now we're empty,
    // which means there's nothing to replace.
    exception_state.ThrowDOMException(
        kIndexSizeError,
        String::Format("Failed to replace the provided item at index %zu.",
                       index));
    return nullptr;
  }

  // Update the value at the desired position 'index'.
  Member<ItemPropertyType>& position = values_[index];
  DCHECK_EQ(position->OwnerList(), this);
  position->SetOwnerList(0);
  position = new_item;
  new_item->SetOwnerList(this);

  return new_item;
}

template <typename Derived, typename ItemProperty>
bool SVGListPropertyHelper<Derived, ItemProperty>::CheckIndexBound(
    size_t index,
    ExceptionState& exception_state) {
  if (index >= values_.size()) {
    exception_state.ThrowDOMException(
        kIndexSizeError, ExceptionMessages::IndexExceedsMaximumBound(
                             "index", index, values_.size()));
    return false;
  }

  return true;
}

template <typename Derived, typename ItemProperty>
size_t SVGListPropertyHelper<Derived, ItemProperty>::FindItem(
    ItemPropertyType* item) {
  return values_.Find(item);
}

template <typename Derived, typename ItemProperty>
void SVGListPropertyHelper<Derived, ItemProperty>::DeepCopy(Derived* from) {
  Clear();
  typename HeapVector<Member<ItemPropertyType>>::const_iterator it =
      from->values_.begin();
  typename HeapVector<Member<ItemPropertyType>>::const_iterator it_end =
      from->values_.end();
  for (; it != it_end; ++it) {
    Append((*it)->Clone());
  }
}

template <typename Derived, typename ItemProperty>
bool SVGListPropertyHelper<Derived, ItemProperty>::AdjustFromToListValues(
    Derived* from_list,
    Derived* to_list,
    float percentage,
    AnimationMode mode) {
  // If no 'to' value is given, nothing to animate.
  size_t to_list_size = to_list->length();
  if (!to_list_size)
    return false;

  // If the 'from' value is given and it's length doesn't match the 'to' value
  // list length, fallback to a discrete animation.
  size_t from_list_size = from_list->length();
  if (from_list_size != to_list_size && from_list_size) {
    if (percentage < 0.5) {
      if (mode != kToAnimation)
        DeepCopy(from_list);
    } else {
      DeepCopy(to_list);
    }

    return false;
  }

  DCHECK(!from_list_size || from_list_size == to_list_size);
  if (length() < to_list_size) {
    size_t padding_count = to_list_size - length();
    for (size_t i = 0; i < padding_count; ++i)
      Append(CreatePaddingItem());
  }

  return true;
}

}  // namespace blink

#endif  // SVGListPropertyHelper_h
