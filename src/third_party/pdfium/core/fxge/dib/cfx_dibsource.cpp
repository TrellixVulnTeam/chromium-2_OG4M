// Copyright 2017 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxge/dib/cfx_dibsource.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "core/fxcodec/fx_codec.h"
#include "core/fxge/dib/cfx_bitmapstorer.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/dib/cfx_imagestretcher.h"
#include "core/fxge/dib/cfx_imagetransformer.h"
#include "core/fxge/ge/cfx_cliprgn.h"
#include "third_party/base/ptr_util.h"

namespace {

class CFX_Palette {
 public:
  explicit CFX_Palette(const CFX_RetainPtr<CFX_DIBSource>& pBitmap);
  ~CFX_Palette();

  const uint32_t* GetPalette() { return m_Palette.data(); }
  const std::pair<uint32_t, uint32_t>* GetLuts() const { return m_Luts.data(); }
  int32_t GetLutCount() const { return m_lut; }
  void SetAmountLut(int row, uint32_t value) { m_Luts[row].first = value; }

 private:
  std::vector<uint32_t> m_Palette;
  // (Amount, Color) pairs
  std::vector<std::pair<uint32_t, uint32_t>> m_Luts;
  int m_lut;
};

void ColorDecode(uint32_t pal_v, uint8_t* r, uint8_t* g, uint8_t* b) {
  *r = static_cast<uint8_t>((pal_v & 0xf00) >> 4);
  *g = static_cast<uint8_t>(pal_v & 0x0f0);
  *b = static_cast<uint8_t>((pal_v & 0x00f) << 4);
}

void Obtain_Pal(std::pair<uint32_t, uint32_t>* luts,
                uint32_t* dest_pal,
                uint32_t lut) {
  uint32_t lut_1 = lut - 1;
  for (int row = 0; row < 256; row++) {
    int lut_offset = lut_1 - row;
    if (lut_offset < 0)
      lut_offset += 256;
    uint32_t color = luts[lut_offset].second;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    ColorDecode(color, &r, &g, &b);
    dest_pal[row] = (static_cast<uint32_t>(r) << 16) |
                    (static_cast<uint32_t>(g) << 8) | b | 0xff000000;
    luts[lut_offset].first = row;
  }
}

CFX_Palette::CFX_Palette(const CFX_RetainPtr<CFX_DIBSource>& pBitmap)
    : m_Palette(256), m_Luts(4096), m_lut(0) {
  int bpp = pBitmap->GetBPP() / 8;
  int width = pBitmap->GetWidth();
  int height = pBitmap->GetHeight();
  for (int row = 0; row < height; ++row) {
    const uint8_t* scan_line = pBitmap->GetScanline(row);
    for (int col = 0; col < width; ++col) {
      const uint8_t* src_port = scan_line + col * bpp;
      uint32_t b = src_port[0] & 0xf0;
      uint32_t g = src_port[1] & 0xf0;
      uint32_t r = src_port[2] & 0xf0;
      uint32_t index = (r << 4) + g + (b >> 4);
      m_Luts[index].first++;
    }
  }
  // Move non-zeros to the front and count them
  for (int row = 0; row < 4096; ++row) {
    if (m_Luts[row].first != 0) {
      m_Luts[m_lut].first = m_Luts[row].first;
      m_Luts[m_lut].second = row;
      ++m_lut;
    }
  }
  std::sort(m_Luts.begin(), m_Luts.begin() + m_lut,
            [](const std::pair<uint32_t, uint32_t>& arg1,
               const std::pair<uint32_t, uint32_t>& arg2) {
              return arg1.first < arg2.first;
            });
  Obtain_Pal(m_Luts.data(), m_Palette.data(), m_lut);
}

CFX_Palette::~CFX_Palette() {}

bool ConvertBuffer_1bppMask2Gray(uint8_t* dest_buf,
                                 int dest_pitch,
                                 int width,
                                 int height,
                                 const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                 int src_left,
                                 int src_top) {
  uint8_t set_gray, reset_gray;
  set_gray = 0xff;
  reset_gray = 0x00;
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    memset(dest_scan, reset_gray, width);
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row);
    for (int col = src_left; col < src_left + width; col++) {
      if (src_scan[col / 8] & (1 << (7 - col % 8))) {
        *dest_scan = set_gray;
      }
      dest_scan++;
    }
  }
  return true;
}

bool ConvertBuffer_8bppMask2Gray(uint8_t* dest_buf,
                                 int dest_pitch,
                                 int width,
                                 int height,
                                 const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                 int src_left,
                                 int src_top) {
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row) + src_left;
    memcpy(dest_scan, src_scan, width);
  }
  return true;
}

bool ConvertBuffer_1bppPlt2Gray(uint8_t* dest_buf,
                                int dest_pitch,
                                int width,
                                int height,
                                const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                int src_left,
                                int src_top) {
  uint32_t* src_plt = pSrcBitmap->GetPalette();
  uint8_t gray[2];
  uint8_t reset_r;
  uint8_t reset_g;
  uint8_t reset_b;
  uint8_t set_r;
  uint8_t set_g;
  uint8_t set_b;
  if (pSrcBitmap->IsCmykImage()) {
    AdobeCMYK_to_sRGB1(FXSYS_GetCValue(src_plt[0]), FXSYS_GetMValue(src_plt[0]),
                       FXSYS_GetYValue(src_plt[0]), FXSYS_GetKValue(src_plt[0]),
                       reset_r, reset_g, reset_b);
    AdobeCMYK_to_sRGB1(FXSYS_GetCValue(src_plt[1]), FXSYS_GetMValue(src_plt[1]),
                       FXSYS_GetYValue(src_plt[1]), FXSYS_GetKValue(src_plt[1]),
                       set_r, set_g, set_b);
  } else {
    reset_r = FXARGB_R(src_plt[0]);
    reset_g = FXARGB_G(src_plt[0]);
    reset_b = FXARGB_B(src_plt[0]);
    set_r = FXARGB_R(src_plt[1]);
    set_g = FXARGB_G(src_plt[1]);
    set_b = FXARGB_B(src_plt[1]);
  }
  gray[0] = FXRGB2GRAY(reset_r, reset_g, reset_b);
  gray[1] = FXRGB2GRAY(set_r, set_g, set_b);

  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    memset(dest_scan, gray[0], width);
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row);
    for (int col = src_left; col < src_left + width; col++) {
      if (src_scan[col / 8] & (1 << (7 - col % 8))) {
        *dest_scan = gray[1];
      }
      dest_scan++;
    }
  }
  return true;
}

bool ConvertBuffer_8bppPlt2Gray(uint8_t* dest_buf,
                                int dest_pitch,
                                int width,
                                int height,
                                const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                int src_left,
                                int src_top) {
  uint32_t* src_plt = pSrcBitmap->GetPalette();
  uint8_t gray[256];
  if (pSrcBitmap->IsCmykImage()) {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    for (size_t i = 0; i < FX_ArraySize(gray); i++) {
      AdobeCMYK_to_sRGB1(
          FXSYS_GetCValue(src_plt[i]), FXSYS_GetMValue(src_plt[i]),
          FXSYS_GetYValue(src_plt[i]), FXSYS_GetKValue(src_plt[i]), r, g, b);
      gray[i] = FXRGB2GRAY(r, g, b);
    }
  } else {
    for (size_t i = 0; i < FX_ArraySize(gray); i++) {
      gray[i] = FXRGB2GRAY(FXARGB_R(src_plt[i]), FXARGB_G(src_plt[i]),
                           FXARGB_B(src_plt[i]));
    }
  }

  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row) + src_left;
    for (int col = 0; col < width; col++) {
      *dest_scan++ = gray[*src_scan++];
    }
  }
  return true;
}

bool ConvertBuffer_RgbOrCmyk2Gray(
    uint8_t* dest_buf,
    int dest_pitch,
    int width,
    int height,
    const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
    int src_left,
    int src_top) {
  int Bpp = pSrcBitmap->GetBPP() / 8;
  if (pSrcBitmap->IsCmykImage()) {
    for (int row = 0; row < height; row++) {
      uint8_t* dest_scan = dest_buf + row * dest_pitch;
      const uint8_t* src_scan =
          pSrcBitmap->GetScanline(src_top + row) + src_left * 4;
      for (int col = 0; col < width; col++) {
        uint8_t r, g, b;
        AdobeCMYK_to_sRGB1(FXSYS_GetCValue(static_cast<uint32_t>(src_scan[0])),
                           FXSYS_GetMValue(static_cast<uint32_t>(src_scan[1])),
                           FXSYS_GetYValue(static_cast<uint32_t>(src_scan[2])),
                           FXSYS_GetKValue(static_cast<uint32_t>(src_scan[3])),
                           r, g, b);
        *dest_scan++ = FXRGB2GRAY(r, g, b);
        src_scan += 4;
      }
    }
  } else {
    for (int row = 0; row < height; row++) {
      uint8_t* dest_scan = dest_buf + row * dest_pitch;
      const uint8_t* src_scan =
          pSrcBitmap->GetScanline(src_top + row) + src_left * Bpp;
      for (int col = 0; col < width; col++) {
        *dest_scan++ = FXRGB2GRAY(src_scan[2], src_scan[1], src_scan[0]);
        src_scan += Bpp;
      }
    }
  }
  return true;
}

void ConvertBuffer_IndexCopy(uint8_t* dest_buf,
                             int dest_pitch,
                             int width,
                             int height,
                             const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                             int src_left,
                             int src_top) {
  if (pSrcBitmap->GetBPP() == 1) {
    for (int row = 0; row < height; row++) {
      uint8_t* dest_scan = dest_buf + row * dest_pitch;
      memset(dest_scan, 0, width);
      const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row);
      for (int col = src_left; col < src_left + width; col++) {
        if (src_scan[col / 8] & (1 << (7 - col % 8))) {
          *dest_scan = 1;
        }
        dest_scan++;
      }
    }
  } else {
    for (int row = 0; row < height; row++) {
      uint8_t* dest_scan = dest_buf + row * dest_pitch;
      const uint8_t* src_scan =
          pSrcBitmap->GetScanline(src_top + row) + src_left;
      memcpy(dest_scan, src_scan, width);
    }
  }
}

bool ConvertBuffer_Plt2PltRgb8(uint8_t* dest_buf,
                               int dest_pitch,
                               int width,
                               int height,
                               const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                               int src_left,
                               int src_top,
                               uint32_t* dst_plt) {
  ConvertBuffer_IndexCopy(dest_buf, dest_pitch, width, height, pSrcBitmap,
                          src_left, src_top);
  uint32_t* src_plt = pSrcBitmap->GetPalette();
  int plt_size = pSrcBitmap->GetPaletteSize();
  if (pSrcBitmap->IsCmykImage()) {
    for (int i = 0; i < plt_size; i++) {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      AdobeCMYK_to_sRGB1(
          FXSYS_GetCValue(src_plt[i]), FXSYS_GetMValue(src_plt[i]),
          FXSYS_GetYValue(src_plt[i]), FXSYS_GetKValue(src_plt[i]), r, g, b);
      dst_plt[i] = FXARGB_MAKE(0xff, r, g, b);
    }
  } else {
    memcpy(dst_plt, src_plt, plt_size * 4);
  }
  return true;
}

bool ConvertBuffer_Rgb2PltRgb8(uint8_t* dest_buf,
                               int dest_pitch,
                               int width,
                               int height,
                               const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                               int src_left,
                               int src_top,
                               uint32_t* dst_plt) {
  int bpp = pSrcBitmap->GetBPP() / 8;
  if (!pSrcBitmap)
    return false;
  CFX_Palette palette(pSrcBitmap);
  const std::pair<uint32_t, uint32_t>* Luts = palette.GetLuts();
  int lut = palette.GetLutCount();
  const uint32_t* pal = palette.GetPalette();
  if (lut > 256) {
    int err;
    int min_err;
    int lut_256 = lut - 256;
    for (int row = 0; row < lut_256; row++) {
      min_err = 1000000;
      uint8_t r;
      uint8_t g;
      uint8_t b;
      ColorDecode(Luts[row].second, &r, &g, &b);
      uint32_t clrindex = 0;
      for (int col = 0; col < 256; col++) {
        uint32_t p_color = pal[col];
        int d_r = r - static_cast<uint8_t>(p_color >> 16);
        int d_g = g - static_cast<uint8_t>(p_color >> 8);
        int d_b = b - static_cast<uint8_t>(p_color);
        err = d_r * d_r + d_g * d_g + d_b * d_b;
        if (err < min_err) {
          min_err = err;
          clrindex = col;
        }
      }
      palette.SetAmountLut(row, clrindex);
    }
  }
  int32_t lut_1 = lut - 1;
  for (int row = 0; row < height; row++) {
    uint8_t* src_scan =
        const_cast<uint8_t*>(pSrcBitmap->GetScanline(src_top + row)) + src_left;
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    for (int col = 0; col < width; col++) {
      uint8_t* src_port = src_scan + col * bpp;
      int r = src_port[2] & 0xf0;
      int g = src_port[1] & 0xf0;
      int b = src_port[0] & 0xf0;
      uint32_t clrindex = (r << 4) + g + (b >> 4);
      for (int i = lut_1; i >= 0; i--)
        if (clrindex == Luts[i].second) {
          *(dest_scan + col) = static_cast<uint8_t>(Luts[i].first);
          break;
        }
    }
  }
  memcpy(dst_plt, pal, sizeof(uint32_t) * 256);
  return true;
}

bool ConvertBuffer_1bppMask2Rgb(FXDIB_Format dst_format,
                                uint8_t* dest_buf,
                                int dest_pitch,
                                int width,
                                int height,
                                const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                int src_left,
                                int src_top) {
  int comps = (dst_format & 0xff) / 8;
  uint8_t set_gray, reset_gray;
  set_gray = 0xff;
  reset_gray = 0x00;
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row);
    for (int col = src_left; col < src_left + width; col++) {
      if (src_scan[col / 8] & (1 << (7 - col % 8))) {
        dest_scan[0] = set_gray;
        dest_scan[1] = set_gray;
        dest_scan[2] = set_gray;
      } else {
        dest_scan[0] = reset_gray;
        dest_scan[1] = reset_gray;
        dest_scan[2] = reset_gray;
      }
      dest_scan += comps;
    }
  }
  return true;
}

bool ConvertBuffer_8bppMask2Rgb(FXDIB_Format dst_format,
                                uint8_t* dest_buf,
                                int dest_pitch,
                                int width,
                                int height,
                                const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                                int src_left,
                                int src_top) {
  int comps = (dst_format & 0xff) / 8;
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row) + src_left;
    uint8_t src_pixel;
    for (int col = 0; col < width; col++) {
      src_pixel = *src_scan++;
      *dest_scan++ = src_pixel;
      *dest_scan++ = src_pixel;
      *dest_scan = src_pixel;
      dest_scan += comps - 2;
    }
  }
  return true;
}

bool ConvertBuffer_1bppPlt2Rgb(FXDIB_Format dst_format,
                               uint8_t* dest_buf,
                               int dest_pitch,
                               int width,
                               int height,
                               const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                               int src_left,
                               int src_top) {
  int comps = (dst_format & 0xff) / 8;
  uint32_t* src_plt = pSrcBitmap->GetPalette();
  uint32_t plt[2];
  uint8_t* bgr_ptr = reinterpret_cast<uint8_t*>(plt);
  if (pSrcBitmap->IsCmykImage()) {
    plt[0] = FXCMYK_TODIB(src_plt[0]);
    plt[1] = FXCMYK_TODIB(src_plt[1]);
  } else {
    bgr_ptr[0] = FXARGB_B(src_plt[0]);
    bgr_ptr[1] = FXARGB_G(src_plt[0]);
    bgr_ptr[2] = FXARGB_R(src_plt[0]);
    bgr_ptr[3] = FXARGB_B(src_plt[1]);
    bgr_ptr[4] = FXARGB_G(src_plt[1]);
    bgr_ptr[5] = FXARGB_R(src_plt[1]);
  }

  if (pSrcBitmap->IsCmykImage()) {
    AdobeCMYK_to_sRGB1(FXSYS_GetCValue(src_plt[0]), FXSYS_GetMValue(src_plt[0]),
                       FXSYS_GetYValue(src_plt[0]), FXSYS_GetKValue(src_plt[0]),
                       bgr_ptr[2], bgr_ptr[1], bgr_ptr[0]);
    AdobeCMYK_to_sRGB1(FXSYS_GetCValue(src_plt[1]), FXSYS_GetMValue(src_plt[1]),
                       FXSYS_GetYValue(src_plt[1]), FXSYS_GetKValue(src_plt[1]),
                       bgr_ptr[5], bgr_ptr[4], bgr_ptr[3]);
  }

  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row);
    for (int col = src_left; col < src_left + width; col++) {
      if (src_scan[col / 8] & (1 << (7 - col % 8))) {
        *dest_scan++ = bgr_ptr[3];
        *dest_scan++ = bgr_ptr[4];
        *dest_scan = bgr_ptr[5];
      } else {
        *dest_scan++ = bgr_ptr[0];
        *dest_scan++ = bgr_ptr[1];
        *dest_scan = bgr_ptr[2];
      }
      dest_scan += comps - 2;
    }
  }
  return true;
}

bool ConvertBuffer_8bppPlt2Rgb(FXDIB_Format dst_format,
                               uint8_t* dest_buf,
                               int dest_pitch,
                               int width,
                               int height,
                               const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                               int src_left,
                               int src_top) {
  int comps = (dst_format & 0xff) / 8;
  uint32_t* src_plt = pSrcBitmap->GetPalette();
  uint32_t plt[256];
  uint8_t* bgr_ptr = reinterpret_cast<uint8_t*>(plt);
  if (!pSrcBitmap->IsCmykImage()) {
    for (int i = 0; i < 256; i++) {
      *bgr_ptr++ = FXARGB_B(src_plt[i]);
      *bgr_ptr++ = FXARGB_G(src_plt[i]);
      *bgr_ptr++ = FXARGB_R(src_plt[i]);
    }
    bgr_ptr = reinterpret_cast<uint8_t*>(plt);
  }

  if (pSrcBitmap->IsCmykImage()) {
    for (int i = 0; i < 256; i++) {
      AdobeCMYK_to_sRGB1(
          FXSYS_GetCValue(src_plt[i]), FXSYS_GetMValue(src_plt[i]),
          FXSYS_GetYValue(src_plt[i]), FXSYS_GetKValue(src_plt[i]), bgr_ptr[2],
          bgr_ptr[1], bgr_ptr[0]);
      bgr_ptr += 3;
    }
    bgr_ptr = reinterpret_cast<uint8_t*>(plt);
  }

  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan = pSrcBitmap->GetScanline(src_top + row) + src_left;
    for (int col = 0; col < width; col++) {
      uint8_t* src_pixel = bgr_ptr + 3 * (*src_scan++);
      *dest_scan++ = *src_pixel++;
      *dest_scan++ = *src_pixel++;
      *dest_scan = *src_pixel++;
      dest_scan += comps - 2;
    }
  }
  return true;
}

bool ConvertBuffer_24bppRgb2Rgb24(
    uint8_t* dest_buf,
    int dest_pitch,
    int width,
    int height,
    const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
    int src_left,
    int src_top) {
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan =
        pSrcBitmap->GetScanline(src_top + row) + src_left * 3;
    memcpy(dest_scan, src_scan, width * 3);
  }
  return true;
}

bool ConvertBuffer_32bppRgb2Rgb24(
    uint8_t* dest_buf,
    int dest_pitch,
    int width,
    int height,
    const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
    int src_left,
    int src_top) {
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan =
        pSrcBitmap->GetScanline(src_top + row) + src_left * 4;
    for (int col = 0; col < width; col++) {
      *dest_scan++ = *src_scan++;
      *dest_scan++ = *src_scan++;
      *dest_scan++ = *src_scan++;
      src_scan++;
    }
  }
  return true;
}

bool ConvertBuffer_Rgb2Rgb32(uint8_t* dest_buf,
                             int dest_pitch,
                             int width,
                             int height,
                             const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
                             int src_left,
                             int src_top) {
  int comps = pSrcBitmap->GetBPP() / 8;
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan =
        pSrcBitmap->GetScanline(src_top + row) + src_left * comps;
    for (int col = 0; col < width; col++) {
      *dest_scan++ = *src_scan++;
      *dest_scan++ = *src_scan++;
      *dest_scan++ = *src_scan++;
      dest_scan++;
      src_scan += comps - 3;
    }
  }
  return true;
}

bool ConvertBuffer_32bppCmyk2Rgb32(
    uint8_t* dest_buf,
    int dest_pitch,
    int width,
    int height,
    const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
    int src_left,
    int src_top) {
  for (int row = 0; row < height; row++) {
    uint8_t* dest_scan = dest_buf + row * dest_pitch;
    const uint8_t* src_scan =
        pSrcBitmap->GetScanline(src_top + row) + src_left * 4;
    for (int col = 0; col < width; col++) {
      AdobeCMYK_to_sRGB1(src_scan[0], src_scan[1], src_scan[2], src_scan[3],
                         dest_scan[2], dest_scan[1], dest_scan[0]);
      dest_scan += 4;
      src_scan += 4;
    }
  }
  return true;
}

}  // namespace

CFX_DIBSource::CFX_DIBSource()
    : m_Width(0), m_Height(0), m_bpp(0), m_AlphaFlag(0), m_Pitch(0) {}

CFX_DIBSource::~CFX_DIBSource() {}

uint8_t* CFX_DIBSource::GetBuffer() const {
  return nullptr;
}

bool CFX_DIBSource::SkipToScanline(int line, IFX_Pause* pPause) const {
  return false;
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::Clone(const FX_RECT* pClip) const {
  FX_RECT rect(0, 0, m_Width, m_Height);
  if (pClip) {
    rect.Intersect(*pClip);
    if (rect.IsEmpty())
      return nullptr;
  }
  auto pNewBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!pNewBitmap->Create(rect.Width(), rect.Height(), GetFormat()))
    return nullptr;

  pNewBitmap->SetPalette(m_pPalette.get());
  pNewBitmap->SetAlphaMask(m_pAlphaMask, pClip);
  if (GetBPP() == 1 && rect.left % 8 != 0) {
    int left_shift = rect.left % 32;
    int right_shift = 32 - left_shift;
    int dword_count = pNewBitmap->m_Pitch / 4;
    for (int row = rect.top; row < rect.bottom; row++) {
      uint32_t* src_scan = (uint32_t*)GetScanline(row) + rect.left / 32;
      uint32_t* dest_scan = (uint32_t*)pNewBitmap->GetScanline(row - rect.top);
      for (int i = 0; i < dword_count; i++) {
        dest_scan[i] =
            (src_scan[i] << left_shift) | (src_scan[i + 1] >> right_shift);
      }
    }
  } else {
    int copy_len = (pNewBitmap->GetWidth() * pNewBitmap->GetBPP() + 7) / 8;
    if (m_Pitch < static_cast<uint32_t>(copy_len))
      copy_len = m_Pitch;

    for (int row = rect.top; row < rect.bottom; row++) {
      const uint8_t* src_scan = GetScanline(row) + rect.left * m_bpp / 8;
      uint8_t* dest_scan = (uint8_t*)pNewBitmap->GetScanline(row - rect.top);
      memcpy(dest_scan, src_scan, copy_len);
    }
  }
  return pNewBitmap;
}

void CFX_DIBSource::BuildPalette() {
  if (m_pPalette)
    return;

  if (GetBPP() == 1) {
    m_pPalette.reset(FX_Alloc(uint32_t, 2));
    if (IsCmykImage()) {
      m_pPalette.get()[0] = 0xff;
      m_pPalette.get()[1] = 0;
    } else {
      m_pPalette.get()[0] = 0xff000000;
      m_pPalette.get()[1] = 0xffffffff;
    }
  } else if (GetBPP() == 8) {
    m_pPalette.reset(FX_Alloc(uint32_t, 256));
    if (IsCmykImage()) {
      for (int i = 0; i < 256; i++)
        m_pPalette.get()[i] = 0xff - i;
    } else {
      for (int i = 0; i < 256; i++)
        m_pPalette.get()[i] = 0xff000000 | (i * 0x10101);
    }
  }
}

bool CFX_DIBSource::BuildAlphaMask() {
  if (m_pAlphaMask)
    return true;

  m_pAlphaMask = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!m_pAlphaMask->Create(m_Width, m_Height, FXDIB_8bppMask)) {
    m_pAlphaMask = nullptr;
    return false;
  }
  memset(m_pAlphaMask->GetBuffer(), 0xff,
         m_pAlphaMask->GetHeight() * m_pAlphaMask->GetPitch());
  return true;
}

uint32_t CFX_DIBSource::GetPaletteEntry(int index) const {
  ASSERT((GetBPP() == 1 || GetBPP() == 8) && !IsAlphaMask());
  if (m_pPalette) {
    return m_pPalette.get()[index];
  }
  if (IsCmykImage()) {
    if (GetBPP() == 1) {
      return index ? 0 : 0xff;
    }
    return 0xff - index;
  }
  if (GetBPP() == 1) {
    return index ? 0xffffffff : 0xff000000;
  }
  return index * 0x10101 | 0xff000000;
}

void CFX_DIBSource::SetPaletteEntry(int index, uint32_t color) {
  ASSERT((GetBPP() == 1 || GetBPP() == 8) && !IsAlphaMask());
  if (!m_pPalette) {
    BuildPalette();
  }
  m_pPalette.get()[index] = color;
}

int CFX_DIBSource::FindPalette(uint32_t color) const {
  ASSERT((GetBPP() == 1 || GetBPP() == 8) && !IsAlphaMask());
  if (!m_pPalette) {
    if (IsCmykImage()) {
      if (GetBPP() == 1)
        return (static_cast<uint8_t>(color) == 0xff) ? 0 : 1;

      return 0xff - static_cast<uint8_t>(color);
    }
    if (GetBPP() == 1) {
      return (static_cast<uint8_t>(color) == 0xff) ? 1 : 0;
    }
    return static_cast<uint8_t>(color);
  }
  int palsize = (1 << GetBPP());
  for (int i = 0; i < palsize; i++)
    if (m_pPalette.get()[i] == color) {
      return i;
    }
  return -1;
}

void CFX_DIBSource::GetOverlapRect(int& dest_left,
                                   int& dest_top,
                                   int& width,
                                   int& height,
                                   int src_width,
                                   int src_height,
                                   int& src_left,
                                   int& src_top,
                                   const CFX_ClipRgn* pClipRgn) {
  if (width == 0 || height == 0) {
    return;
  }
  ASSERT(width > 0 && height > 0);
  if (dest_left > m_Width || dest_top > m_Height) {
    width = 0;
    height = 0;
    return;
  }
  int x_offset = dest_left - src_left;
  int y_offset = dest_top - src_top;
  FX_RECT src_rect(src_left, src_top, src_left + width, src_top + height);
  FX_RECT src_bound(0, 0, src_width, src_height);
  src_rect.Intersect(src_bound);
  FX_RECT dest_rect(src_rect.left + x_offset, src_rect.top + y_offset,
                    src_rect.right + x_offset, src_rect.bottom + y_offset);
  FX_RECT dest_bound(0, 0, m_Width, m_Height);
  dest_rect.Intersect(dest_bound);
  if (pClipRgn) {
    dest_rect.Intersect(pClipRgn->GetBox());
  }
  dest_left = dest_rect.left;
  dest_top = dest_rect.top;
  src_left = dest_left - x_offset;
  src_top = dest_top - y_offset;
  width = dest_rect.right - dest_rect.left;
  height = dest_rect.bottom - dest_rect.top;
}

void CFX_DIBSource::SetPalette(const uint32_t* pSrc) {
  static const uint32_t kPaletteSize = 256;
  if (!pSrc || GetBPP() > 8) {
    m_pPalette.reset();
    return;
  }
  uint32_t pal_size = 1 << GetBPP();
  if (!m_pPalette)
    m_pPalette.reset(FX_Alloc(uint32_t, pal_size));
  pal_size = std::min(pal_size, kPaletteSize);
  memcpy(m_pPalette.get(), pSrc, pal_size * sizeof(uint32_t));
}

void CFX_DIBSource::GetPalette(uint32_t* pal, int alpha) const {
  ASSERT(GetBPP() <= 8 && !IsCmykImage());
  if (GetBPP() == 1) {
    pal[0] = ((m_pPalette ? m_pPalette.get()[0] : 0xff000000) & 0xffffff) |
             (alpha << 24);
    pal[1] = ((m_pPalette ? m_pPalette.get()[1] : 0xffffffff) & 0xffffff) |
             (alpha << 24);
    return;
  }
  if (m_pPalette) {
    for (int i = 0; i < 256; i++) {
      pal[i] = (m_pPalette.get()[i] & 0x00ffffff) | (alpha << 24);
    }
  } else {
    for (int i = 0; i < 256; i++) {
      pal[i] = (i * 0x10101) | (alpha << 24);
    }
  }
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::CloneAlphaMask(
    const FX_RECT* pClip) const {
  ASSERT(GetFormat() == FXDIB_Argb);
  FX_RECT rect(0, 0, m_Width, m_Height);
  if (pClip) {
    rect.Intersect(*pClip);
    if (rect.IsEmpty())
      return nullptr;
  }
  auto pMask = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!pMask->Create(rect.Width(), rect.Height(), FXDIB_8bppMask))
    return nullptr;

  for (int row = rect.top; row < rect.bottom; row++) {
    const uint8_t* src_scan = GetScanline(row) + rect.left * 4 + 3;
    uint8_t* dest_scan =
        const_cast<uint8_t*>(pMask->GetScanline(row - rect.top));
    for (int col = rect.left; col < rect.right; col++) {
      *dest_scan++ = *src_scan;
      src_scan += 4;
    }
  }
  return pMask;
}

bool CFX_DIBSource::SetAlphaMask(const CFX_RetainPtr<CFX_DIBSource>& pAlphaMask,
                                 const FX_RECT* pClip) {
  if (!HasAlpha() || GetFormat() == FXDIB_Argb)
    return false;

  if (!pAlphaMask) {
    m_pAlphaMask->Clear(0xff000000);
    return true;
  }
  FX_RECT rect(0, 0, pAlphaMask->m_Width, pAlphaMask->m_Height);
  if (pClip) {
    rect.Intersect(*pClip);
    if (rect.IsEmpty() || rect.Width() != m_Width ||
        rect.Height() != m_Height) {
      return false;
    }
  } else {
    if (pAlphaMask->m_Width != m_Width || pAlphaMask->m_Height != m_Height)
      return false;
  }
  for (int row = 0; row < m_Height; row++) {
    memcpy(const_cast<uint8_t*>(m_pAlphaMask->GetScanline(row)),
           pAlphaMask->GetScanline(row + rect.top) + rect.left,
           m_pAlphaMask->m_Pitch);
  }
  return true;
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::FlipImage(bool bXFlip,
                                                     bool bYFlip) const {
  auto pFlipped = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!pFlipped->Create(m_Width, m_Height, GetFormat()))
    return nullptr;

  pFlipped->SetPalette(m_pPalette.get());
  uint8_t* pDestBuffer = pFlipped->GetBuffer();
  int Bpp = m_bpp / 8;
  for (int row = 0; row < m_Height; row++) {
    const uint8_t* src_scan = GetScanline(row);
    uint8_t* dest_scan =
        pDestBuffer + m_Pitch * (bYFlip ? (m_Height - row - 1) : row);
    if (!bXFlip) {
      memcpy(dest_scan, src_scan, m_Pitch);
      continue;
    }
    if (m_bpp == 1) {
      memset(dest_scan, 0, m_Pitch);
      for (int col = 0; col < m_Width; col++)
        if (src_scan[col / 8] & (1 << (7 - col % 8))) {
          int dest_col = m_Width - col - 1;
          dest_scan[dest_col / 8] |= (1 << (7 - dest_col % 8));
        }
    } else {
      dest_scan += (m_Width - 1) * Bpp;
      if (Bpp == 1) {
        for (int col = 0; col < m_Width; col++) {
          *dest_scan = *src_scan;
          dest_scan--;
          src_scan++;
        }
      } else if (Bpp == 3) {
        for (int col = 0; col < m_Width; col++) {
          dest_scan[0] = src_scan[0];
          dest_scan[1] = src_scan[1];
          dest_scan[2] = src_scan[2];
          dest_scan -= 3;
          src_scan += 3;
        }
      } else {
        ASSERT(Bpp == 4);
        for (int col = 0; col < m_Width; col++) {
          *(uint32_t*)dest_scan = *(uint32_t*)src_scan;
          dest_scan -= 4;
          src_scan += 4;
        }
      }
    }
  }
  if (m_pAlphaMask) {
    pDestBuffer = pFlipped->m_pAlphaMask->GetBuffer();
    uint32_t dest_pitch = pFlipped->m_pAlphaMask->GetPitch();
    for (int row = 0; row < m_Height; row++) {
      const uint8_t* src_scan = m_pAlphaMask->GetScanline(row);
      uint8_t* dest_scan =
          pDestBuffer + dest_pitch * (bYFlip ? (m_Height - row - 1) : row);
      if (!bXFlip) {
        memcpy(dest_scan, src_scan, dest_pitch);
        continue;
      }
      dest_scan += (m_Width - 1);
      for (int col = 0; col < m_Width; col++) {
        *dest_scan = *src_scan;
        dest_scan--;
        src_scan++;
      }
    }
  }
  return pFlipped;
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::CloneConvert(
    FXDIB_Format dest_format) {
  if (dest_format == GetFormat())
    return Clone(nullptr);

  auto pClone = pdfium::MakeRetain<CFX_DIBitmap>();
  if (!pClone->Create(m_Width, m_Height, dest_format))
    return nullptr;

  CFX_RetainPtr<CFX_DIBitmap> pSrcAlpha;
  if (HasAlpha()) {
    if (GetFormat() == FXDIB_Argb)
      pSrcAlpha = CloneAlphaMask();
    else
      pSrcAlpha = m_pAlphaMask;

    if (!pSrcAlpha)
      return nullptr;
  }
  bool ret = true;
  if (dest_format & 0x0200) {
    if (dest_format == FXDIB_Argb) {
      ret = pSrcAlpha ? pClone->LoadChannel(FXDIB_Alpha, pSrcAlpha, FXDIB_Alpha)
                      : pClone->LoadChannel(FXDIB_Alpha, 0xff);
    } else {
      ret = pClone->SetAlphaMask(pSrcAlpha);
    }
  }
  if (!ret)
    return nullptr;

  CFX_RetainPtr<CFX_DIBSource> holder(this);
  std::unique_ptr<uint32_t, FxFreeDeleter> pal_8bpp;
  if (!ConvertBuffer(dest_format, pClone->GetBuffer(), pClone->GetPitch(),
                     m_Width, m_Height, holder, 0, 0, &pal_8bpp)) {
    return nullptr;
  }
  if (pal_8bpp)
    pClone->SetPalette(pal_8bpp.get());

  return pClone;
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::SwapXY(
    bool bXFlip,
    bool bYFlip,
    const FX_RECT* pDestClip) const {
  FX_RECT dest_clip(0, 0, m_Height, m_Width);
  if (pDestClip)
    dest_clip.Intersect(*pDestClip);
  if (dest_clip.IsEmpty())
    return nullptr;

  auto pTransBitmap = pdfium::MakeRetain<CFX_DIBitmap>();
  int result_height = dest_clip.Height();
  int result_width = dest_clip.Width();
  if (!pTransBitmap->Create(result_width, result_height, GetFormat()))
    return nullptr;

  pTransBitmap->SetPalette(m_pPalette.get());
  int dest_pitch = pTransBitmap->GetPitch();
  uint8_t* dest_buf = pTransBitmap->GetBuffer();
  int row_start = bXFlip ? m_Height - dest_clip.right : dest_clip.left;
  int row_end = bXFlip ? m_Height - dest_clip.left : dest_clip.right;
  int col_start = bYFlip ? m_Width - dest_clip.bottom : dest_clip.top;
  int col_end = bYFlip ? m_Width - dest_clip.top : dest_clip.bottom;
  if (GetBPP() == 1) {
    memset(dest_buf, 0xff, dest_pitch * result_height);
    for (int row = row_start; row < row_end; row++) {
      const uint8_t* src_scan = GetScanline(row);
      int dest_col = (bXFlip ? dest_clip.right - (row - row_start) - 1 : row) -
                     dest_clip.left;
      uint8_t* dest_scan = dest_buf;
      if (bYFlip) {
        dest_scan += (result_height - 1) * dest_pitch;
      }
      int dest_step = bYFlip ? -dest_pitch : dest_pitch;
      for (int col = col_start; col < col_end; col++) {
        if (!(src_scan[col / 8] & (1 << (7 - col % 8)))) {
          dest_scan[dest_col / 8] &= ~(1 << (7 - dest_col % 8));
        }
        dest_scan += dest_step;
      }
    }
  } else {
    int nBytes = GetBPP() / 8;
    int dest_step = bYFlip ? -dest_pitch : dest_pitch;
    if (nBytes == 3) {
      dest_step -= 2;
    }
    for (int row = row_start; row < row_end; row++) {
      int dest_col = (bXFlip ? dest_clip.right - (row - row_start) - 1 : row) -
                     dest_clip.left;
      uint8_t* dest_scan = dest_buf + dest_col * nBytes;
      if (bYFlip) {
        dest_scan += (result_height - 1) * dest_pitch;
      }
      if (nBytes == 4) {
        uint32_t* src_scan = (uint32_t*)GetScanline(row) + col_start;
        for (int col = col_start; col < col_end; col++) {
          *(uint32_t*)dest_scan = *src_scan++;
          dest_scan += dest_step;
        }
      } else {
        const uint8_t* src_scan = GetScanline(row) + col_start * nBytes;
        if (nBytes == 1) {
          for (int col = col_start; col < col_end; col++) {
            *dest_scan = *src_scan++;
            dest_scan += dest_step;
          }
        } else {
          for (int col = col_start; col < col_end; col++) {
            *dest_scan++ = *src_scan++;
            *dest_scan++ = *src_scan++;
            *dest_scan = *src_scan++;
            dest_scan += dest_step;
          }
        }
      }
    }
  }
  if (m_pAlphaMask) {
    dest_pitch = pTransBitmap->m_pAlphaMask->GetPitch();
    dest_buf = pTransBitmap->m_pAlphaMask->GetBuffer();
    int dest_step = bYFlip ? -dest_pitch : dest_pitch;
    for (int row = row_start; row < row_end; row++) {
      int dest_col = (bXFlip ? dest_clip.right - (row - row_start) - 1 : row) -
                     dest_clip.left;
      uint8_t* dest_scan = dest_buf + dest_col;
      if (bYFlip) {
        dest_scan += (result_height - 1) * dest_pitch;
      }
      const uint8_t* src_scan = m_pAlphaMask->GetScanline(row) + col_start;
      for (int col = col_start; col < col_end; col++) {
        *dest_scan = *src_scan++;
        dest_scan += dest_step;
      }
    }
  }
  return pTransBitmap;
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::TransformTo(
    const CFX_Matrix* pDestMatrix,
    int& result_left,
    int& result_top,
    uint32_t flags,
    const FX_RECT* pDestClip) {
  CFX_RetainPtr<CFX_DIBSource> holder(this);
  CFX_ImageTransformer transformer(holder, pDestMatrix, flags, pDestClip);
  transformer.Start();
  transformer.Continue(nullptr);
  result_left = transformer.result().left;
  result_top = transformer.result().top;
  return transformer.DetachBitmap();
}

CFX_RetainPtr<CFX_DIBitmap> CFX_DIBSource::StretchTo(int dest_width,
                                                     int dest_height,
                                                     uint32_t flags,
                                                     const FX_RECT* pClip) {
  CFX_RetainPtr<CFX_DIBSource> holder(this);
  FX_RECT clip_rect(0, 0, abs(dest_width), abs(dest_height));
  if (pClip)
    clip_rect.Intersect(*pClip);

  if (clip_rect.IsEmpty())
    return nullptr;

  if (dest_width == m_Width && dest_height == m_Height)
    return Clone(&clip_rect);

  CFX_BitmapStorer storer;
  CFX_ImageStretcher stretcher(&storer, holder, dest_width, dest_height,
                               clip_rect, flags);
  if (stretcher.Start())
    stretcher.Continue(nullptr);

  return storer.Detach();
}

// static
bool CFX_DIBSource::ConvertBuffer(
    FXDIB_Format dest_format,
    uint8_t* dest_buf,
    int dest_pitch,
    int width,
    int height,
    const CFX_RetainPtr<CFX_DIBSource>& pSrcBitmap,
    int src_left,
    int src_top,
    std::unique_ptr<uint32_t, FxFreeDeleter>* p_pal) {
  FXDIB_Format src_format = pSrcBitmap->GetFormat();
  switch (dest_format) {
    case FXDIB_Invalid:
    case FXDIB_1bppCmyk:
    case FXDIB_1bppMask:
    case FXDIB_1bppRgb:
      ASSERT(false);
      return false;
    case FXDIB_8bppMask: {
      if ((src_format & 0xff) == 1) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_1bppPlt2Gray(dest_buf, dest_pitch, width, height,
                                            pSrcBitmap, src_left, src_top);
        }
        return ConvertBuffer_1bppMask2Gray(dest_buf, dest_pitch, width, height,
                                           pSrcBitmap, src_left, src_top);
      }
      if ((src_format & 0xff) == 8) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_8bppPlt2Gray(dest_buf, dest_pitch, width, height,
                                            pSrcBitmap, src_left, src_top);
        }
        return ConvertBuffer_8bppMask2Gray(dest_buf, dest_pitch, width, height,
                                           pSrcBitmap, src_left, src_top);
      }
      if ((src_format & 0xff) >= 24) {
        return ConvertBuffer_RgbOrCmyk2Gray(dest_buf, dest_pitch, width, height,
                                            pSrcBitmap, src_left, src_top);
      }
      return false;
    }
    case FXDIB_8bppRgb:
    case FXDIB_8bppRgba: {
      if ((src_format & 0xff) == 8 && !pSrcBitmap->GetPalette()) {
        return ConvertBuffer(FXDIB_8bppMask, dest_buf, dest_pitch, width,
                             height, pSrcBitmap, src_left, src_top, p_pal);
      }
      p_pal->reset(FX_Alloc(uint32_t, 256));
      if (((src_format & 0xff) == 1 || (src_format & 0xff) == 8) &&
          pSrcBitmap->GetPalette()) {
        return ConvertBuffer_Plt2PltRgb8(dest_buf, dest_pitch, width, height,
                                         pSrcBitmap, src_left, src_top,
                                         p_pal->get());
      }
      if ((src_format & 0xff) >= 24) {
        return ConvertBuffer_Rgb2PltRgb8(dest_buf, dest_pitch, width, height,
                                         pSrcBitmap, src_left, src_top,
                                         p_pal->get());
      }
      return false;
    }
    case FXDIB_Rgb:
    case FXDIB_Rgba: {
      if ((src_format & 0xff) == 1) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_1bppPlt2Rgb(dest_format, dest_buf, dest_pitch,
                                           width, height, pSrcBitmap, src_left,
                                           src_top);
        }
        return ConvertBuffer_1bppMask2Rgb(dest_format, dest_buf, dest_pitch,
                                          width, height, pSrcBitmap, src_left,
                                          src_top);
      }
      if ((src_format & 0xff) == 8) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_8bppPlt2Rgb(dest_format, dest_buf, dest_pitch,
                                           width, height, pSrcBitmap, src_left,
                                           src_top);
        }
        return ConvertBuffer_8bppMask2Rgb(dest_format, dest_buf, dest_pitch,
                                          width, height, pSrcBitmap, src_left,
                                          src_top);
      }
      if ((src_format & 0xff) == 24) {
        return ConvertBuffer_24bppRgb2Rgb24(dest_buf, dest_pitch, width, height,
                                            pSrcBitmap, src_left, src_top);
      }
      if ((src_format & 0xff) == 32) {
        return ConvertBuffer_32bppRgb2Rgb24(dest_buf, dest_pitch, width, height,
                                            pSrcBitmap, src_left, src_top);
      }
      return false;
    }
    case FXDIB_Argb:
    case FXDIB_Rgb32: {
      if ((src_format & 0xff) == 1) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_1bppPlt2Rgb(dest_format, dest_buf, dest_pitch,
                                           width, height, pSrcBitmap, src_left,
                                           src_top);
        }
        return ConvertBuffer_1bppMask2Rgb(dest_format, dest_buf, dest_pitch,
                                          width, height, pSrcBitmap, src_left,
                                          src_top);
      }
      if ((src_format & 0xff) == 8) {
        if (pSrcBitmap->GetPalette()) {
          return ConvertBuffer_8bppPlt2Rgb(dest_format, dest_buf, dest_pitch,
                                           width, height, pSrcBitmap, src_left,
                                           src_top);
        }
        return ConvertBuffer_8bppMask2Rgb(dest_format, dest_buf, dest_pitch,
                                          width, height, pSrcBitmap, src_left,
                                          src_top);
      }
      if ((src_format & 0xff) >= 24) {
        if (src_format & 0x0400) {
          return ConvertBuffer_32bppCmyk2Rgb32(dest_buf, dest_pitch, width,
                                               height, pSrcBitmap, src_left,
                                               src_top);
        }
        return ConvertBuffer_Rgb2Rgb32(dest_buf, dest_pitch, width, height,
                                       pSrcBitmap, src_left, src_top);
      }
      return false;
    }
    default:
      return false;
  }
}
