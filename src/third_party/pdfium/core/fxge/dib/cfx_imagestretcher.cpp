// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxge/dib/cfx_imagestretcher.h"

#include <climits>

#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/dib/cfx_dibsource.h"
#include "core/fxge/dib/cstretchengine.h"
#include "core/fxge/fx_dib.h"
#include "third_party/base/ptr_util.h"

namespace {

bool SourceSizeWithinLimit(int width, int height) {
  const int kMaxProgressiveStretchPixels = 1000000;
  return !height || width < kMaxProgressiveStretchPixels / height;
}

FXDIB_Format GetStretchedFormat(const CFX_DIBSource& src) {
  FXDIB_Format format = src.GetFormat();
  if (format == FXDIB_1bppMask)
    return FXDIB_8bppMask;
  if (format == FXDIB_1bppRgb)
    return FXDIB_8bppRgb;
  if (format == FXDIB_8bppRgb && src.GetPalette())
    return FXDIB_Rgb;
  return format;
}

void CmykDecode(uint32_t cmyk, int& c, int& m, int& y, int& k) {
  c = FXSYS_GetCValue(cmyk);
  m = FXSYS_GetMValue(cmyk);
  y = FXSYS_GetYValue(cmyk);
  k = FXSYS_GetKValue(cmyk);
}

}  // namespace

CFX_ImageStretcher::CFX_ImageStretcher(
    IFX_ScanlineComposer* pDest,
    const CFX_RetainPtr<CFX_DIBSource>& pSource,
    int dest_width,
    int dest_height,
    const FX_RECT& bitmap_rect,
    uint32_t flags)
    : m_pDest(pDest),
      m_pSource(pSource),
      m_Flags(flags),
      m_bFlipX(false),
      m_bFlipY(false),
      m_DestWidth(dest_width),
      m_DestHeight(dest_height),
      m_ClipRect(bitmap_rect),
      m_DestFormat(GetStretchedFormat(*pSource)),
      m_DestBPP(m_DestFormat & 0xff),
      m_LineIndex(0) {}

CFX_ImageStretcher::~CFX_ImageStretcher() {}

bool CFX_ImageStretcher::Start() {
  if (m_DestWidth == 0 || m_DestHeight == 0)
    return false;

  if (m_pSource->GetFormat() == FXDIB_1bppRgb && m_pSource->GetPalette()) {
    FX_ARGB pal[256];
    int a0, r0, g0, b0, a1, r1, g1, b1;
    ArgbDecode(m_pSource->GetPaletteEntry(0), a0, r0, g0, b0);
    ArgbDecode(m_pSource->GetPaletteEntry(1), a1, r1, g1, b1);
    for (int i = 0; i < 256; i++) {
      int a = a0 + (a1 - a0) * i / 255;
      int r = r0 + (r1 - r0) * i / 255;
      int g = g0 + (g1 - g0) * i / 255;
      int b = b0 + (b1 - b0) * i / 255;
      pal[i] = ArgbEncode(a, r, g, b);
    }
    if (!m_pDest->SetInfo(m_ClipRect.Width(), m_ClipRect.Height(), m_DestFormat,
                          pal)) {
      return false;
    }
  } else if (m_pSource->GetFormat() == FXDIB_1bppCmyk &&
             m_pSource->GetPalette()) {
    FX_CMYK pal[256];
    int c0, m0, y0, k0, c1, m1, y1, k1;
    CmykDecode(m_pSource->GetPaletteEntry(0), c0, m0, y0, k0);
    CmykDecode(m_pSource->GetPaletteEntry(1), c1, m1, y1, k1);
    for (int i = 0; i < 256; i++) {
      int c = c0 + (c1 - c0) * i / 255;
      int m = m0 + (m1 - m0) * i / 255;
      int y = y0 + (y1 - y0) * i / 255;
      int k = k0 + (k1 - k0) * i / 255;
      pal[i] = CmykEncode(c, m, y, k);
    }
    if (!m_pDest->SetInfo(m_ClipRect.Width(), m_ClipRect.Height(), m_DestFormat,
                          pal)) {
      return false;
    }
  } else if (!m_pDest->SetInfo(m_ClipRect.Width(), m_ClipRect.Height(),
                               m_DestFormat, nullptr)) {
    return false;
  }

  if (m_Flags & FXDIB_DOWNSAMPLE)
    return StartQuickStretch();
  return StartStretch();
}

bool CFX_ImageStretcher::Continue(IFX_Pause* pPause) {
  if (m_Flags & FXDIB_DOWNSAMPLE)
    return ContinueQuickStretch(pPause);
  return ContinueStretch(pPause);
}

bool CFX_ImageStretcher::StartStretch() {
  m_pStretchEngine = pdfium::MakeUnique<CStretchEngine>(
      m_pDest, m_DestFormat, m_DestWidth, m_DestHeight, m_ClipRect, m_pSource,
      m_Flags);
  m_pStretchEngine->StartStretchHorz();
  if (SourceSizeWithinLimit(m_pSource->GetWidth(), m_pSource->GetHeight())) {
    m_pStretchEngine->Continue(nullptr);
    return false;
  }
  return true;
}

bool CFX_ImageStretcher::ContinueStretch(IFX_Pause* pPause) {
  return m_pStretchEngine && m_pStretchEngine->Continue(pPause);
}

bool CFX_ImageStretcher::StartQuickStretch() {
  if (m_DestWidth < 0) {
    m_bFlipX = true;
    m_DestWidth = -m_DestWidth;
  }
  if (m_DestHeight < 0) {
    m_bFlipY = true;
    m_DestHeight = -m_DestHeight;
  }
  uint32_t size = m_ClipRect.Width();
  if (size && m_DestBPP > static_cast<int>(INT_MAX / size))
    return false;

  size *= m_DestBPP;
  m_pScanline.reset(FX_Alloc(uint8_t, (size / 8 + 3) / 4 * 4));
  if (m_pSource->m_pAlphaMask)
    m_pMaskScanline.reset(FX_Alloc(uint8_t, (m_ClipRect.Width() + 3) / 4 * 4));

  if (SourceSizeWithinLimit(m_pSource->GetWidth(), m_pSource->GetHeight())) {
    ContinueQuickStretch(nullptr);
    return false;
  }
  return true;
}

bool CFX_ImageStretcher::ContinueQuickStretch(IFX_Pause* pPause) {
  if (!m_pScanline)
    return false;

  int result_width = m_ClipRect.Width();
  int result_height = m_ClipRect.Height();
  int src_height = m_pSource->GetHeight();
  for (; m_LineIndex < result_height; m_LineIndex++) {
    int dest_y;
    int src_y;
    if (m_bFlipY) {
      dest_y = result_height - m_LineIndex - 1;
      src_y = (m_DestHeight - (dest_y + m_ClipRect.top) - 1) * src_height /
              m_DestHeight;
    } else {
      dest_y = m_LineIndex;
      src_y = (dest_y + m_ClipRect.top) * src_height / m_DestHeight;
    }
    src_y = pdfium::clamp(src_y, 0, src_height - 1);

    if (m_pSource->SkipToScanline(src_y, pPause))
      return true;

    m_pSource->DownSampleScanline(src_y, m_pScanline.get(), m_DestBPP,
                                  m_DestWidth, m_bFlipX, m_ClipRect.left,
                                  result_width);
    if (m_pMaskScanline) {
      m_pSource->m_pAlphaMask->DownSampleScanline(
          src_y, m_pMaskScanline.get(), 1, m_DestWidth, m_bFlipX,
          m_ClipRect.left, result_width);
    }
    m_pDest->ComposeScanline(dest_y, m_pScanline.get(), m_pMaskScanline.get());
  }
  return false;
}
