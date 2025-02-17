/*
 * Copyright (C) 2005, 2006, 2008, 2011 Apple Inc. All rights reserved.
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

#include "core/loader/HistoryItem.h"

#include "core/html/forms/FormController.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/text/CString.h"

namespace blink {

static long long GenerateSequenceNumber() {
  // Initialize to the current time to reduce the likelihood of generating
  // identifiers that overlap with those from past/future browser sessions.
  static long long next = static_cast<long long>(CurrentTime() * 1000000.0);
  return ++next;
}

HistoryItem::HistoryItem()
    : did_save_scroll_or_scale_state_(false),
      page_scale_factor_(0),
      item_sequence_number_(GenerateSequenceNumber()),
      document_sequence_number_(GenerateSequenceNumber()),
      scroll_restoration_type_(kScrollRestorationAuto) {}

HistoryItem::~HistoryItem() {}

const String& HistoryItem::UrlString() const {
  return url_string_;
}

KURL HistoryItem::Url() const {
  return KURL(kParsedURLString, url_string_);
}

const Referrer& HistoryItem::GetReferrer() const {
  return referrer_;
}

void HistoryItem::SetURLString(const String& url_string) {
  if (url_string_ != url_string)
    url_string_ = url_string;
}

void HistoryItem::SetURL(const KURL& url) {
  SetURLString(url.GetString());
}

void HistoryItem::SetReferrer(const Referrer& referrer) {
  // This should be a RELEASE_ASSERT.
  referrer_ = SecurityPolicy::GenerateReferrer(referrer.referrer_policy, Url(),
                                               referrer.referrer);
}

const ScrollOffset& HistoryItem::VisualViewportScrollOffset() const {
  return visual_viewport_scroll_offset_;
}

void HistoryItem::SetVisualViewportScrollOffset(const ScrollOffset& offset) {
  visual_viewport_scroll_offset_ = offset;
  SetDidSaveScrollOrScaleState(true);
}

const ScrollOffset& HistoryItem::GetScrollOffset() const {
  return scroll_offset_;
}

void HistoryItem::SetScrollOffset(const ScrollOffset& offset) {
  scroll_offset_ = offset;
  SetDidSaveScrollOrScaleState(true);
}

float HistoryItem::PageScaleFactor() const {
  return page_scale_factor_;
}

void HistoryItem::SetPageScaleFactor(float scale_factor) {
  page_scale_factor_ = scale_factor;
  SetDidSaveScrollOrScaleState(true);
}

void HistoryItem::SetDocumentState(const Vector<String>& state) {
  DCHECK(!document_state_);
  document_state_vector_ = state;
}

void HistoryItem::SetDocumentState(DocumentState* state) {
  document_state_ = state;
}

const Vector<String>& HistoryItem::GetDocumentState() {
  if (document_state_)
    document_state_vector_ = document_state_->ToStateVector();
  return document_state_vector_;
}

Vector<String> HistoryItem::GetReferencedFilePaths() {
  return FormController::GetReferencedFilePaths(GetDocumentState());
}

void HistoryItem::ClearDocumentState() {
  document_state_.Clear();
  document_state_vector_.Clear();
}

void HistoryItem::SetStateObject(PassRefPtr<SerializedScriptValue> object) {
  state_object_ = std::move(object);
}

const AtomicString& HistoryItem::FormContentType() const {
  return form_content_type_;
}

void HistoryItem::SetFormInfoFromRequest(const ResourceRequest& request) {
  if (DeprecatedEqualIgnoringCase(request.HttpMethod(), "POST")) {
    // FIXME: Eventually we have to make this smart enough to handle the case
    // where we have a stream for the body to handle the "data interspersed with
    // files" feature.
    form_data_ = request.HttpBody();
    form_content_type_ = request.HttpContentType();
  } else {
    form_data_ = nullptr;
    form_content_type_ = g_null_atom;
  }
}

void HistoryItem::SetFormData(PassRefPtr<EncodedFormData> form_data) {
  form_data_ = std::move(form_data);
}

void HistoryItem::SetFormContentType(const AtomicString& form_content_type) {
  form_content_type_ = form_content_type;
}

EncodedFormData* HistoryItem::FormData() {
  return form_data_.Get();
}

ResourceRequest HistoryItem::GenerateResourceRequest(
    WebCachePolicy cache_policy) {
  ResourceRequest request(url_string_);
  request.SetHTTPReferrer(referrer_);
  request.SetCachePolicy(cache_policy);
  if (form_data_) {
    request.SetHTTPMethod(HTTPNames::POST);
    request.SetHTTPBody(form_data_);
    request.SetHTTPContentType(form_content_type_);
    request.AddHTTPOriginIfNeeded(referrer_.referrer);
  }
  return request;
}

DEFINE_TRACE(HistoryItem) {
  visitor->Trace(document_state_);
}

}  // namespace blink
