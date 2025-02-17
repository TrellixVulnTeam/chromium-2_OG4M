// Copyright 2016 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_RENDER_CPDF_TYPE3CACHE_H_
#define CORE_FPDFAPI_RENDER_CPDF_TYPE3CACHE_H_

#include <map>
#include <memory>

#include "core/fpdfapi/font/cpdf_type3font.h"
#include "core/fxcrt/cfx_retain_ptr.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/fx_system.h"

class CPDF_Type3Glyphs;

class CPDF_Type3Cache : public CFX_Retainable {
 public:
  template <typename T, typename... Args>
  friend CFX_RetainPtr<T> pdfium::MakeRetain(Args&&... args);

  CFX_GlyphBitmap* LoadGlyph(uint32_t charcode,
                             const CFX_Matrix* pMatrix,
                             float retinaScaleX,
                             float retinaScaleY);

 private:
  explicit CPDF_Type3Cache(CPDF_Type3Font* pFont);
  ~CPDF_Type3Cache() override;

  CFX_GlyphBitmap* RenderGlyph(CPDF_Type3Glyphs* pSize,
                               uint32_t charcode,
                               const CFX_Matrix* pMatrix,
                               float retinaScaleX,
                               float retinaScaleY);

  CPDF_Type3Font* const m_pFont;
  std::map<CFX_ByteString, std::unique_ptr<CPDF_Type3Glyphs>> m_SizeMap;
};

#endif  // CORE_FPDFAPI_RENDER_CPDF_TYPE3CACHE_H_
