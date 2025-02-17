// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef FXBARCODE_PDF417_BC_PDF417BARCODEMATRIX_H_
#define FXBARCODE_PDF417_BC_PDF417BARCODEMATRIX_H_

#include <vector>

#include "core/fxcrt/fx_basic.h"

class CBC_BarcodeRow;

class CBC_BarcodeMatrix {
 public:
  CBC_BarcodeMatrix();
  CBC_BarcodeMatrix(int32_t height, int32_t width);
  virtual ~CBC_BarcodeMatrix();

  void set(int32_t x, int32_t y, uint8_t value);
  void setMatrix(int32_t x, int32_t y, bool black);
  void startRow();
  CBC_BarcodeRow* getCurrentRow();
  std::vector<uint8_t>& getMatrix();
  std::vector<uint8_t>& getScaledMatrix(int32_t scale);
  std::vector<uint8_t>& getScaledMatrix(int32_t xScale, int32_t yScale);
  int32_t getWidth();
  int32_t getHeight();

 private:
  std::vector<CBC_BarcodeRow*> m_matrix;
  std::vector<uint8_t> m_matrixOut;
  int32_t m_currentRow;
  int32_t m_height;
  int32_t m_width;
  int32_t m_outWidth;
  int32_t m_outHeight;
};

#endif  // FXBARCODE_PDF417_BC_PDF417BARCODEMATRIX_H_
