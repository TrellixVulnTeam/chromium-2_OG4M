// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef FXBARCODE_ONED_BC_ONEDEAN13WRITER_H_
#define FXBARCODE_ONED_BC_ONEDEAN13WRITER_H_

#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/fx_system.h"
#include "fxbarcode/oned/BC_OneDimWriter.h"

class CFX_DIBitmap;
class CFX_RenderDevice;

class CBC_OnedEAN13Writer : public CBC_OneDimWriter {
 public:
  CBC_OnedEAN13Writer();
  ~CBC_OnedEAN13Writer() override;

  // CBC_OneDimWriter
  uint8_t* Encode(const CFX_ByteString& contents,
                  BCFORMAT format,
                  int32_t& outWidth,
                  int32_t& outHeight,
                  int32_t& e) override;
  uint8_t* Encode(const CFX_ByteString& contents,
                  BCFORMAT format,
                  int32_t& outWidth,
                  int32_t& outHeight,
                  int32_t hints,
                  int32_t& e) override;
  uint8_t* Encode(const CFX_ByteString& contents,
                  int32_t& outLength,
                  int32_t& e) override;
  void RenderResult(const CFX_WideStringC& contents,
                    uint8_t* code,
                    int32_t codeLength,
                    bool isDevice,
                    int32_t& e) override;
  bool CheckContentValidity(const CFX_WideStringC& contents) override;
  CFX_WideString FilterContents(const CFX_WideStringC& contents) override;

  int32_t CalcChecksum(const CFX_ByteString& contents);

 protected:
  void ShowChars(const CFX_WideStringC& contents,
                 const CFX_RetainPtr<CFX_DIBitmap>& pOutBitmap,
                 CFX_RenderDevice* device,
                 const CFX_Matrix* matrix,
                 int32_t barWidth,
                 int32_t multiple,
                 int32_t& e) override;

 private:
  int32_t m_codeWidth;
};

#endif  // FXBARCODE_ONED_BC_ONEDEAN13WRITER_H_
