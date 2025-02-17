/*
 * Copyright (C) 2005, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/commands/SplitElementCommand.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/dom/Element.h"
#include "core/editing/EditingUtilities.h"
#include "platform/wtf/Assertions.h"

namespace blink {

SplitElementCommand::SplitElementCommand(Element* element, Node* at_child)
    : SimpleEditCommand(element->GetDocument()),
      element2_(element),
      at_child_(at_child) {
  DCHECK(element2_);
  DCHECK(at_child_);
  DCHECK_EQ(at_child_->parentNode(), element2_);
}

void SplitElementCommand::ExecuteApply() {
  if (at_child_->parentNode() != element2_)
    return;

  HeapVector<Member<Node>> children;
  for (Node* node = element2_->FirstChild(); node != at_child_;
       node = node->nextSibling())
    children.push_back(node);

  DummyExceptionStateForTesting exception_state;

  ContainerNode* parent = element2_->parentNode();
  if (!parent || !HasEditableStyle(*parent))
    return;
  parent->InsertBefore(element1_.Get(), element2_.Get(), exception_state);
  if (exception_state.HadException())
    return;

  // Delete id attribute from the second element because the same id cannot be
  // used for more than one element
  element2_->removeAttribute(HTMLNames::idAttr);

  for (const auto& child : children)
    element1_->AppendChild(child, exception_state);
}

void SplitElementCommand::DoApply(EditingState*) {
  element1_ = element2_->CloneElementWithoutChildren();

  ExecuteApply();
}

void SplitElementCommand::DoUnapply() {
  if (!element1_ || !HasEditableStyle(*element1_) ||
      !HasEditableStyle(*element2_))
    return;

  NodeVector children;
  GetChildNodes(*element1_, children);

  Node* ref_child = element2_->FirstChild();

  for (const auto& child : children)
    element2_->InsertBefore(child, ref_child, IGNORE_EXCEPTION_FOR_TESTING);

  // Recover the id attribute of the original element.
  const AtomicString& id = element1_->getAttribute(HTMLNames::idAttr);
  if (!id.IsNull())
    element2_->setAttribute(HTMLNames::idAttr, id);

  element1_->remove(IGNORE_EXCEPTION_FOR_TESTING);
}

void SplitElementCommand::DoReapply() {
  if (!element1_)
    return;

  ExecuteApply();
}

DEFINE_TRACE(SplitElementCommand) {
  visitor->Trace(element1_);
  visitor->Trace(element2_);
  visitor->Trace(at_child_);
  SimpleEditCommand::Trace(visitor);
}

}  // namespace blink
