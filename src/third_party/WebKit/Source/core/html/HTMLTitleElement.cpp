/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2010 Apple Inc. All rights reserved.
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
 */

#include "core/html/HTMLTitleElement.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/dom/ChildListMutationScope.h"
#include "core/dom/Document.h"
#include "core/dom/Text.h"
#include "core/style/ComputedStyle.h"
#include "core/style/StyleInheritedData.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/text/StringBuilder.h"

namespace blink {

using namespace HTMLNames;

inline HTMLTitleElement::HTMLTitleElement(Document& document)
    : HTMLElement(titleTag, document),
      ignore_title_updates_when_children_change_(false) {}

DEFINE_NODE_FACTORY(HTMLTitleElement)

Node::InsertionNotificationRequest HTMLTitleElement::InsertedInto(
    ContainerNode* insertion_point) {
  HTMLElement::InsertedInto(insertion_point);
  if (IsInDocumentTree())
    GetDocument().SetTitleElement(this);
  return kInsertionDone;
}

void HTMLTitleElement::RemovedFrom(ContainerNode* insertion_point) {
  HTMLElement::RemovedFrom(insertion_point);
  if (insertion_point->IsInDocumentTree())
    GetDocument().RemoveTitle(this);
}

void HTMLTitleElement::ChildrenChanged(const ChildrenChange& change) {
  HTMLElement::ChildrenChanged(change);
  if (IsInDocumentTree() && !ignore_title_updates_when_children_change_)
    GetDocument().SetTitleElement(this);
}

String HTMLTitleElement::text() const {
  StringBuilder result;

  for (Node* n = FirstChild(); n; n = n->nextSibling()) {
    if (n->IsTextNode())
      result.Append(ToText(n)->data());
  }

  return result.ToString();
}

void HTMLTitleElement::setText(const String& value) {
  ChildListMutationScope mutation(*this);

  {
    // Avoid calling Document::setTitleElement() during intermediate steps.
    AutoReset<bool> inhibit_title_update_scope(
        &ignore_title_updates_when_children_change_, !value.IsEmpty());
    RemoveChildren(kOmitSubtreeModifiedEvent);
  }

  if (!value.IsEmpty()) {
    AppendChild(GetDocument().createTextNode(value.Impl()),
                IGNORE_EXCEPTION_FOR_TESTING);
  }
}

}  // namespace blink
