/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
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

#include "core/inspector/DOMEditor.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/DOMException.h"
#include "core/dom/Element.h"
#include "core/dom/Node.h"
#include "core/dom/Text.h"
#include "core/editing/serializers/Serialization.h"
#include "core/inspector/DOMPatchSupport.h"
#include "core/inspector/InspectorHistory.h"
#include "core/inspector/protocol/Protocol.h"
#include "wtf/RefPtr.h"

namespace blink {

using protocol::Response;

class DOMEditor::RemoveChildAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(RemoveChildAction);

 public:
  RemoveChildAction(ContainerNode* parent_node, Node* node)
      : InspectorHistory::Action("RemoveChild"),
        parent_node_(parent_node),
        node_(node) {}

  bool Perform(ExceptionState& exception_state) override {
    anchor_node_ = node_->nextSibling();
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    parent_node_->InsertBefore(node_.Get(), anchor_node_.Get(),
                               exception_state);
    return !exception_state.HadException();
  }

  bool Redo(ExceptionState& exception_state) override {
    parent_node_->RemoveChild(node_.Get(), exception_state);
    return !exception_state.HadException();
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(parent_node_);
    visitor->Trace(node_);
    visitor->Trace(anchor_node_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<ContainerNode> parent_node_;
  Member<Node> node_;
  Member<Node> anchor_node_;
};

class DOMEditor::InsertBeforeAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(InsertBeforeAction);

 public:
  InsertBeforeAction(ContainerNode* parent_node, Node* node, Node* anchor_node)
      : InspectorHistory::Action("InsertBefore"),
        parent_node_(parent_node),
        node_(node),
        anchor_node_(anchor_node) {}

  bool Perform(ExceptionState& exception_state) override {
    if (node_->parentNode()) {
      remove_child_action_ =
          new RemoveChildAction(node_->parentNode(), node_.Get());
      if (!remove_child_action_->Perform(exception_state))
        return false;
    }
    parent_node_->InsertBefore(node_.Get(), anchor_node_.Get(),
                               exception_state);
    return !exception_state.HadException();
  }

  bool Undo(ExceptionState& exception_state) override {
    parent_node_->RemoveChild(node_.Get(), exception_state);
    if (exception_state.HadException())
      return false;
    if (remove_child_action_)
      return remove_child_action_->Undo(exception_state);
    return true;
  }

  bool Redo(ExceptionState& exception_state) override {
    if (remove_child_action_ && !remove_child_action_->Redo(exception_state))
      return false;
    parent_node_->InsertBefore(node_.Get(), anchor_node_.Get(),
                               exception_state);
    return !exception_state.HadException();
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(parent_node_);
    visitor->Trace(node_);
    visitor->Trace(anchor_node_);
    visitor->Trace(remove_child_action_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<ContainerNode> parent_node_;
  Member<Node> node_;
  Member<Node> anchor_node_;
  Member<RemoveChildAction> remove_child_action_;
};

class DOMEditor::RemoveAttributeAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(RemoveAttributeAction);

 public:
  RemoveAttributeAction(Element* element, const AtomicString& name)
      : InspectorHistory::Action("RemoveAttribute"),
        element_(element),
        name_(name) {}

  bool Perform(ExceptionState& exception_state) override {
    value_ = element_->getAttribute(name_);
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    element_->setAttribute(name_, value_, exception_state);
    return true;
  }

  bool Redo(ExceptionState&) override {
    element_->removeAttribute(name_);
    return true;
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(element_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<Element> element_;
  AtomicString name_;
  AtomicString value_;
};

class DOMEditor::SetAttributeAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(SetAttributeAction);

 public:
  SetAttributeAction(Element* element,
                     const AtomicString& name,
                     const AtomicString& value)
      : InspectorHistory::Action("SetAttribute"),
        element_(element),
        name_(name),
        value_(value),
        had_attribute_(false) {}

  bool Perform(ExceptionState& exception_state) override {
    const AtomicString& value = element_->getAttribute(name_);
    had_attribute_ = !value.IsNull();
    if (had_attribute_)
      old_value_ = value;
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    if (had_attribute_)
      element_->setAttribute(name_, old_value_, exception_state);
    else
      element_->removeAttribute(name_);
    return true;
  }

  bool Redo(ExceptionState& exception_state) override {
    element_->setAttribute(name_, value_, exception_state);
    return true;
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(element_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<Element> element_;
  AtomicString name_;
  AtomicString value_;
  bool had_attribute_;
  AtomicString old_value_;
};

class DOMEditor::SetOuterHTMLAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(SetOuterHTMLAction);

 public:
  SetOuterHTMLAction(Node* node, const String& html)
      : InspectorHistory::Action("SetOuterHTML"),
        node_(node),
        next_sibling_(node->nextSibling()),
        html_(html),
        new_node_(nullptr),
        history_(new InspectorHistory()),
        dom_editor_(new DOMEditor(history_.Get())) {}

  bool Perform(ExceptionState& exception_state) override {
    old_html_ = CreateMarkup(node_.Get());
    ASSERT(node_->ownerDocument());
    DOMPatchSupport dom_patch_support(dom_editor_.Get(),
                                      *node_->ownerDocument());
    new_node_ =
        dom_patch_support.PatchNode(node_.Get(), html_, exception_state);
    return !exception_state.HadException();
  }

  bool Undo(ExceptionState& exception_state) override {
    return history_->Undo(exception_state);
  }

  bool Redo(ExceptionState& exception_state) override {
    return history_->Redo(exception_state);
  }

  Node* NewNode() { return new_node_; }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(node_);
    visitor->Trace(next_sibling_);
    visitor->Trace(new_node_);
    visitor->Trace(history_);
    visitor->Trace(dom_editor_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<Node> node_;
  Member<Node> next_sibling_;
  String html_;
  String old_html_;
  Member<Node> new_node_;
  Member<InspectorHistory> history_;
  Member<DOMEditor> dom_editor_;
};

class DOMEditor::ReplaceWholeTextAction final
    : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(ReplaceWholeTextAction);

 public:
  ReplaceWholeTextAction(Text* text_node, const String& text)
      : InspectorHistory::Action("ReplaceWholeText"),
        text_node_(text_node),
        text_(text) {}

  bool Perform(ExceptionState& exception_state) override {
    old_text_ = text_node_->wholeText();
    return Redo(exception_state);
  }

  bool Undo(ExceptionState&) override {
    text_node_->ReplaceWholeText(old_text_);
    return true;
  }

  bool Redo(ExceptionState&) override {
    text_node_->ReplaceWholeText(text_);
    return true;
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(text_node_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<Text> text_node_;
  String text_;
  String old_text_;
};

class DOMEditor::ReplaceChildNodeAction final
    : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(ReplaceChildNodeAction);

 public:
  ReplaceChildNodeAction(ContainerNode* parent_node,
                         Node* new_node,
                         Node* old_node)
      : InspectorHistory::Action("ReplaceChildNode"),
        parent_node_(parent_node),
        new_node_(new_node),
        old_node_(old_node) {}

  bool Perform(ExceptionState& exception_state) override {
    return Redo(exception_state);
  }

  bool Undo(ExceptionState& exception_state) override {
    parent_node_->ReplaceChild(old_node_, new_node_.Get(), exception_state);
    return !exception_state.HadException();
  }

  bool Redo(ExceptionState& exception_state) override {
    parent_node_->ReplaceChild(new_node_, old_node_.Get(), exception_state);
    return !exception_state.HadException();
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(parent_node_);
    visitor->Trace(new_node_);
    visitor->Trace(old_node_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<ContainerNode> parent_node_;
  Member<Node> new_node_;
  Member<Node> old_node_;
};

class DOMEditor::SetNodeValueAction final : public InspectorHistory::Action {
  WTF_MAKE_NONCOPYABLE(SetNodeValueAction);

 public:
  SetNodeValueAction(Node* node, const String& value)
      : InspectorHistory::Action("SetNodeValue"), node_(node), value_(value) {}

  bool Perform(ExceptionState&) override {
    old_value_ = node_->nodeValue();
    return Redo(IGNORE_EXCEPTION_FOR_TESTING);
  }

  bool Undo(ExceptionState&) override {
    node_->setNodeValue(old_value_);
    return true;
  }

  bool Redo(ExceptionState&) override {
    node_->setNodeValue(value_);
    return true;
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(node_);
    InspectorHistory::Action::Trace(visitor);
  }

 private:
  Member<Node> node_;
  String value_;
  String old_value_;
};

DOMEditor::DOMEditor(InspectorHistory* history) : history_(history) {}

bool DOMEditor::InsertBefore(ContainerNode* parent_node,
                             Node* node,
                             Node* anchor_node,
                             ExceptionState& exception_state) {
  return history_->Perform(
      new InsertBeforeAction(parent_node, node, anchor_node), exception_state);
}

bool DOMEditor::RemoveChild(ContainerNode* parent_node,
                            Node* node,
                            ExceptionState& exception_state) {
  return history_->Perform(new RemoveChildAction(parent_node, node),
                           exception_state);
}

bool DOMEditor::SetAttribute(Element* element,
                             const String& name,
                             const String& value,
                             ExceptionState& exception_state) {
  return history_->Perform(
      new SetAttributeAction(element, AtomicString(name), AtomicString(value)),
      exception_state);
}

bool DOMEditor::RemoveAttribute(Element* element,
                                const String& name,
                                ExceptionState& exception_state) {
  return history_->Perform(
      new RemoveAttributeAction(element, AtomicString(name)), exception_state);
}

bool DOMEditor::SetOuterHTML(Node* node,
                             const String& html,
                             Node** new_node,
                             ExceptionState& exception_state) {
  SetOuterHTMLAction* action = new SetOuterHTMLAction(node, html);
  bool result = history_->Perform(action, exception_state);
  if (result)
    *new_node = action->NewNode();
  return result;
}

bool DOMEditor::ReplaceWholeText(Text* text_node,
                                 const String& text,
                                 ExceptionState& exception_state) {
  return history_->Perform(new ReplaceWholeTextAction(text_node, text),
                           exception_state);
}

bool DOMEditor::ReplaceChild(ContainerNode* parent_node,
                             Node* new_node,
                             Node* old_node,
                             ExceptionState& exception_state) {
  return history_->Perform(
      new ReplaceChildNodeAction(parent_node, new_node, old_node),
      exception_state);
}

bool DOMEditor::SetNodeValue(Node* node,
                             const String& value,
                             ExceptionState& exception_state) {
  return history_->Perform(new SetNodeValueAction(node, value),
                           exception_state);
}

static Response ToResponse(ExceptionState& exception_state) {
  if (exception_state.HadException()) {
    return Response::Error(DOMException::GetErrorName(exception_state.Code()) +
                           " " + exception_state.Message());
  }
  return Response::OK();
}

Response DOMEditor::InsertBefore(ContainerNode* parent_node,
                                 Node* node,
                                 Node* anchor_node) {
  DummyExceptionStateForTesting exception_state;
  InsertBefore(parent_node, node, anchor_node, exception_state);
  return ToResponse(exception_state);
}

Response DOMEditor::RemoveChild(ContainerNode* parent_node, Node* node) {
  DummyExceptionStateForTesting exception_state;
  RemoveChild(parent_node, node, exception_state);
  return ToResponse(exception_state);
}

Response DOMEditor::SetAttribute(Element* element,
                                 const String& name,
                                 const String& value) {
  DummyExceptionStateForTesting exception_state;
  SetAttribute(element, name, value, exception_state);
  return ToResponse(exception_state);
}

Response DOMEditor::RemoveAttribute(Element* element, const String& name) {
  DummyExceptionStateForTesting exception_state;
  RemoveAttribute(element, name, exception_state);
  return ToResponse(exception_state);
}

Response DOMEditor::SetOuterHTML(Node* node,
                                 const String& html,
                                 Node** new_node) {
  DummyExceptionStateForTesting exception_state;
  SetOuterHTML(node, html, new_node, exception_state);
  return ToResponse(exception_state);
}

Response DOMEditor::ReplaceWholeText(Text* text_node, const String& text) {
  DummyExceptionStateForTesting exception_state;
  ReplaceWholeText(text_node, text, exception_state);
  return ToResponse(exception_state);
}

DEFINE_TRACE(DOMEditor) {
  visitor->Trace(history_);
}

}  // namespace blink
