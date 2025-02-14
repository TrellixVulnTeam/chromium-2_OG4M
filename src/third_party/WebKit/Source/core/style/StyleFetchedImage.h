/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
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

#ifndef StyleFetchedImage_h
#define StyleFetchedImage_h

#include "core/loader/resource/ImageResourceObserver.h"
#include "core/style/StyleImage.h"
#include "platform/weborigin/KURL.h"

namespace blink {

class Document;

class StyleFetchedImage final : public StyleImage,
                                public ImageResourceObserver {
  USING_PRE_FINALIZER(StyleFetchedImage, Dispose);

 public:
  static StyleFetchedImage* Create(ImageResourceContent* image,
                                   const Document& document,
                                   const KURL& url) {
    return new StyleFetchedImage(image, document, url);
  }
  ~StyleFetchedImage() override;

  WrappedImagePtr Data() const override;

  CSSValue* CssValue() const override;
  CSSValue* ComputedCSSValue() const override;

  bool CanRender() const override;
  bool IsLoaded() const override;
  bool ErrorOccurred() const override;
  LayoutSize ImageSize(const LayoutObject&,
                       float multiplier,
                       const LayoutSize& default_object_size) const override;
  bool ImageHasRelativeSize() const override;
  bool UsesImageContainerSize() const override;
  void AddClient(LayoutObject*) override;
  void RemoveClient(LayoutObject*) override;
  void ImageNotifyFinished(ImageResourceContent*) override;
  String DebugName() const override { return "StyleFetchedImage"; }
  PassRefPtr<Image> GetImage(const LayoutObject&,
                             const IntSize&,
                             float zoom) const override;
  bool KnownToBeOpaque(const LayoutObject&) const override;
  ImageResourceContent* CachedImage() const override;

  DECLARE_VIRTUAL_TRACE();

 private:
  StyleFetchedImage(ImageResourceContent*, const Document&, const KURL&);

  void Dispose();

  Member<ImageResourceContent> image_;
  Member<const Document> document_;
  const KURL url_;
};

DEFINE_STYLE_IMAGE_TYPE_CASTS(StyleFetchedImage, IsImageResource());

}  // namespace blink
#endif
