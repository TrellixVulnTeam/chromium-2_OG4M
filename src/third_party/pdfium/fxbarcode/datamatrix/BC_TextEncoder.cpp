// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com
// Original code is licensed as follows:
/*
 * Copyright 2006-2007 Jeremias Maerki.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fxbarcode/BC_Dimension.h"
#include "fxbarcode/common/BC_CommonBitMatrix.h"
#include "fxbarcode/datamatrix/BC_C40Encoder.h"
#include "fxbarcode/datamatrix/BC_Encoder.h"
#include "fxbarcode/datamatrix/BC_EncoderContext.h"
#include "fxbarcode/datamatrix/BC_HighLevelEncoder.h"
#include "fxbarcode/datamatrix/BC_SymbolInfo.h"
#include "fxbarcode/datamatrix/BC_SymbolShapeHint.h"
#include "fxbarcode/datamatrix/BC_TextEncoder.h"

CBC_TextEncoder::CBC_TextEncoder() {}
CBC_TextEncoder::~CBC_TextEncoder() {}
int32_t CBC_TextEncoder::getEncodingMode() {
  return TEXT_ENCODATION;
}
int32_t CBC_TextEncoder::encodeChar(wchar_t c, CFX_WideString& sb, int32_t& e) {
  if (c == ' ') {
    sb += (wchar_t)'\3';
    return 1;
  }
  if (c >= '0' && c <= '9') {
    sb += (wchar_t)(c - 48 + 4);
    return 1;
  }
  if (c >= 'a' && c <= 'z') {
    sb += (wchar_t)(c - 97 + 14);
    return 1;
  }
  if (c <= 0x1f) {
    sb += (wchar_t)'\0';
    sb += c;
    return 2;
  }
  if (c >= '!' && c <= '/') {
    sb += (wchar_t)'\1';
    sb += (wchar_t)(c - 33);
    return 2;
  }
  if (c >= ':' && c <= '@') {
    sb += (wchar_t)'\1';
    sb += (wchar_t)(c - 58 + 15);
    return 2;
  }
  if (c >= '[' && c <= '_') {
    sb += (wchar_t)'\1';
    sb += (wchar_t)(c - 91 + 22);
    return 2;
  }
  if (c == 0x0060) {
    sb += (wchar_t)'\2';
    sb += (wchar_t)(c - 96);
    return 2;
  }
  if (c >= 'A' && c <= 'Z') {
    sb += (wchar_t)'\2';
    sb += (wchar_t)(c - 65 + 1);
    return 2;
  }
  if (c >= '{' && c <= 0x007f) {
    sb += (wchar_t)'\2';
    sb += (wchar_t)(c - 123 + 27);
    return 2;
  }
  if (c >= 0x0080) {
    sb += (wchar_t)'\1';
    sb += (wchar_t)0x001e;
    int32_t len = 2;
    len += encodeChar((wchar_t)(c - 128), sb, e);
    if (e != BCExceptionNO)
      return -1;
    return len;
  }
  CBC_HighLevelEncoder::illegalCharacter(c, e);
  return -1;
}
