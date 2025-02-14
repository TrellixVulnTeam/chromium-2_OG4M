/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef ChildNodeList_h
#define ChildNodeList_h

#include "core/dom/CollectionIndexCache.h"
#include "core/dom/ContainerNode.h"
#include "core/dom/NodeList.h"

namespace blink {

class ChildNodeList final : public NodeList {
 public:
  static ChildNodeList* Create(ContainerNode& root_node) {
    return new ChildNodeList(root_node);
  }

  ~ChildNodeList() override;

  // DOM API.
  unsigned length() const override {
    return collection_index_cache_.NodeCount(*this);
  }
  Node* item(unsigned index) const override {
    return collection_index_cache_.NodeAt(*this, index);
  }

  // Non-DOM API.
  void InvalidateCache() { collection_index_cache_.Invalidate(); }
  ContainerNode& OwnerNode() const { return *parent_; }

  ContainerNode& RootNode() const { return OwnerNode(); }

  // CollectionIndexCache API.
  bool CanTraverseBackward() const { return true; }
  Node* TraverseToFirst() const { return RootNode().FirstChild(); }
  Node* TraverseToLast() const { return RootNode().LastChild(); }
  Node* TraverseForwardToOffset(unsigned offset,
                                Node& current_node,
                                unsigned& current_offset) const;
  Node* TraverseBackwardToOffset(unsigned offset,
                                 Node& current_node,
                                 unsigned& current_offset) const;

  DECLARE_VIRTUAL_TRACE();

 private:
  explicit ChildNodeList(ContainerNode& root_node);

  bool IsChildNodeList() const override { return true; }
  Node* VirtualOwnerNode() const override;

  Member<ContainerNode> parent_;
  mutable CollectionIndexCache<ChildNodeList, Node> collection_index_cache_;
};

DEFINE_TYPE_CASTS(ChildNodeList,
                  NodeList,
                  nodeList,
                  nodeList->IsChildNodeList(),
                  nodeList.IsChildNodeList());

}  // namespace blink

#endif  // ChildNodeList_h
