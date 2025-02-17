/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#ifndef WebData_h
#define WebData_h

#include "WebCommon.h"
#include "WebPrivatePtr.h"

#if INSIDE_BLINK
#include "wtf/PassRefPtr.h"
#endif

namespace blink {

class SharedBuffer;

// A container for raw bytes.  It is inexpensive to copy a WebData object.
//
// WARNING: It is not safe to pass a WebData across threads!!!
//
class BLINK_PLATFORM_EXPORT WebData {
 public:
  ~WebData() { Reset(); }

  WebData() {}

  WebData(const char* data, size_t size) { Assign(data, size); }

  template <int N>
  WebData(const char (&data)[N]) {
    Assign(data, N - 1);
  }

  WebData(const WebData& d) { Assign(d); }

  WebData& operator=(const WebData& d) {
    Assign(d);
    return *this;
  }

  void Reset();
  void Assign(const WebData&);
  void Assign(const char* data, size_t size);

  size_t size() const;
  const char* Data() const;

  bool IsEmpty() const { return !size(); }
  bool IsNull() const { return private_.IsNull(); }

#if INSIDE_BLINK
  WebData(PassRefPtr<SharedBuffer>);
  WebData& operator=(PassRefPtr<SharedBuffer>);
  operator PassRefPtr<SharedBuffer>() const;
#else
  template <class C>
  WebData(const C& c) {
    Assign(c.data(), c.size());
  }

  template <class C>
  WebData& operator=(const C& c) {
    Assign(c.data(), c.size());
    return *this;
  }
#endif

 private:
  WebPrivatePtr<SharedBuffer> private_;
};

}  // namespace blink

#endif
