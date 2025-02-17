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

#include "modules/encoding/TextEncoder.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/ExecutionContext.h"
#include "modules/encoding/Encoding.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/TextEncodingRegistry.h"

namespace blink {

TextEncoder* TextEncoder::Create(ExecutionContext* context,
                                 ExceptionState& exception_state) {
  WTF::TextEncoding encoding("UTF-8");
  return new TextEncoder(encoding);
}

TextEncoder::TextEncoder(const WTF::TextEncoding& encoding)
    : encoding_(encoding), codec_(NewTextCodec(encoding)) {
  String name(encoding_.GetName());
  DCHECK_EQ(name, "UTF-8");
}

TextEncoder::~TextEncoder() {}

String TextEncoder::encoding() const {
  String name = String(encoding_.GetName()).DeprecatedLower();
  DCHECK_EQ(name, "utf-8");
  return name;
}

NotShared<DOMUint8Array> TextEncoder::encode(const String& input) {
  CString result;
  if (input.Is8Bit())
    result = codec_->Encode(input.Characters8(), input.length(),
                            WTF::kQuestionMarksForUnencodables);
  else
    result = codec_->Encode(input.Characters16(), input.length(),
                            WTF::kQuestionMarksForUnencodables);

  const char* buffer = result.Data();
  const unsigned char* unsigned_buffer =
      reinterpret_cast<const unsigned char*>(buffer);

  return NotShared<DOMUint8Array>(
      DOMUint8Array::Create(unsigned_buffer, result.length()));
}

}  // namespace blink
