/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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

#ifndef MHTMLArchive_h
#define MHTMLArchive_h

#include "platform/heap/Handle.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/StringHash.h"

namespace blink {

class ArchiveResource;
class KURL;
class SharedBuffer;

struct SerializedResource;

class PLATFORM_EXPORT MHTMLArchive final
    : public GarbageCollected<MHTMLArchive> {
 public:
  static MHTMLArchive* Create(const KURL&, PassRefPtr<const SharedBuffer>);

  // Binary encoding results in smaller MHTML files but they might not work in
  // other browsers.
  enum EncodingPolicy { kUseDefaultEncoding, kUseBinaryEncoding };

  // Generates an MHTML header and appends it to |outputBuffer|.
  //
  // Same |boundary| needs to be used for all generateMHTMLHeader and
  // generateMHTMLPart and generateMHTMLFooter calls that belong to the same
  // MHTML document (see also rfc1341, section 7.2.1, "boundary" description).
  static void GenerateMHTMLHeader(const String& boundary,
                                  const String& title,
                                  const String& mime_type,
                                  Vector<char>& output_buffer);

  // Serializes SerializedResource as an MHTML part and appends it in
  // |outputBuffer|.
  //
  // Same |boundary| needs to be used for all generateMHTMLHeader and
  // generateMHTMLPart and generateMHTMLFooter calls that belong to the same
  // MHTML document (see also rfc1341, section 7.2.1, "boundary" description).
  //
  // If |contentID| is non-empty, then it will be used as a Content-ID header.
  // See rfc2557 - section 8.3 - "Use of the Content-ID header and CID URLs".
  static void GenerateMHTMLPart(const String& boundary,
                                const String& content_id,
                                EncodingPolicy,
                                const SerializedResource&,
                                Vector<char>& output_buffer);

  // Generates an MHTML footer and appends it to |outputBuffer| for testing
  // purposes.
  //
  // Same |boundary| needs to be used for all generateMHTMLHeader and
  // generateMHTMLPart and generateMHTMLFooter calls that belong to the same
  // MHTML document (see also rfc1341, section 7.2.1, "boundary" description).
  static void GenerateMHTMLFooterForTesting(const String& boundary,
                                            Vector<char>& output_buffer);

  typedef HeapHashMap<String, Member<ArchiveResource>> SubArchiveResources;

  ArchiveResource* MainResource() { return main_resource_.Get(); }
  ArchiveResource* SubresourceForURL(const KURL&) const;

  DECLARE_TRACE();

 private:
  MHTMLArchive();

  void SetMainResource(ArchiveResource*);
  void AddSubresource(ArchiveResource*);
  static bool CanLoadArchive(const KURL&);

  Member<ArchiveResource> main_resource_;
  SubArchiveResources subresources_;
};

}  // namespace blink

#endif
