// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef FXBARCODE_UTILS_H_
#define FXBARCODE_UTILS_H_

#include <vector>

#include "core/fxcrt/fx_basic.h"

bool BC_FX_ByteString_Replace(CFX_ByteString& dst,
                              uint32_t first,
                              uint32_t last,
                              int32_t count,
                              char c);
void BC_FX_ByteString_Append(CFX_ByteString& dst, int32_t count, char c);
void BC_FX_ByteString_Append(CFX_ByteString& dst,
                             const std::vector<uint8_t>& ba);

#if (_FX_OS_ == _FX_WIN32_DESKTOP_ || _FX_OS_ == _FX_WIN64_)
#include <limits>
#elif(_FX_OS_ == _FX_MACOSX_ || _FX_OS_ == _FX_LINUX_DESKTOP_ || \
      _FX_OS_ == _FX_IOS_)
#include <limits.h>
#endif
#if (_FX_OS_ == _FX_WIN32_DESKTOP_ || _FX_OS_ == _FX_WIN64_)
#define FXSYS_isnan(x) _isnan(x)
#elif(_FX_OS_ == _FX_MACOSX_ || _FX_OS_ == _FX_IOS_ || \
      _FX_OS_ == _FX_LINUX_DESKTOP_ || _FX_OS_ == _FX_ANDROID_)
#include <cmath>
#define FXSYS_isnan(x) std::isnan(x)
#endif
#if (_FX_OS_ == _FX_WIN32_DESKTOP_ || _FX_OS_ == _FX_WIN64_)
#define FXSYS_nan() (std::numeric_limits<float>::quiet_NaN())
#elif(_FX_OS_ == _FX_MACOSX_ || _FX_OS_ == _FX_LINUX_DESKTOP_ || \
      _FX_OS_ == _FX_IOS_ || _FX_OS_ == _FX_ANDROID_)
#define FXSYS_nan() NAN
#endif
enum BCFORMAT {
  BCFORMAT_UNSPECIFY = -1,
  BCFORMAT_CODABAR,
  BCFORMAT_CODE_39,
  BCFORMAT_CODE_128,
  BCFORMAT_CODE_128B,
  BCFORMAT_CODE_128C,
  BCFORMAT_EAN_8,
  BCFORMAT_UPC_A,
  BCFORMAT_EAN_13,
  BCFORMAT_PDF_417,
  BCFORMAT_DATAMATRIX,
  BCFORMAT_QR_CODE
};
#define BCFORMAT_ECLEVEL_L 0
#define BCFORMAT_ECLEVEL_M 1
#define BCFORMAT_ECLEVEL_Q 2
#define BCFORMAT_ECLEVEL_H 3
#include <ctype.h>
#define BCExceptionNO 0
#define BCExceptionHeightAndWidthMustBeAtLeast1 5
#define BCExceptionFormatException 8
#define BCExceptionIllegalArgument 16
#define BCExceptionDigitLengthMustBe8 20
#define BCExceptionNoContents 26
#define BCExceptionUnSupportEclevel 27
#define BCExceptionDegreeIsNegative 31
#define BCExceptionAIsZero 37
#define BCExceptionOnlyEncodeCODE_128 41
#define BCExceptionOnlyEncodeCODE_39 42
#define BCExceptionOnlyEncodeEAN_13 43
#define BCExceptionOnlyEncodeEAN_8 44
#define BCExceptionDigitLengthShould13 46
#define BCExceptionOnlyEncodeUPC_A 48
#define BCExceptionValueMustBeEither0or1 50
#define BCExceptionBadIndexException 52
#define BCExceptionNoSuchVersion 58
#define BCExceptionCannotFindBlockInfo 59
#define BCExceptionInvalidQRCode 61
#define BCExceptionDataTooMany 62
#define BCExceptionBitsNotEqualCacity 63
#define BCExceptionUnsupportedMode 64
#define BCExceptionInvalidateCharacter 65
#define BCExceptionBytesNotMatchOffset 66
#define BCExceptionSizeInBytesDiffer 67
#define BCExceptionInvalidateMaskPattern 68
#define BCExceptionNullPointer 69
#define BCExceptionBadMask 70
#define BCExceptionInvalidateImageData 73
#define BCExceptionHeight_8BeZero 74
#define BCExceptionCharacterNotThisMode 75
#define BCExceptionBitsBytesNotMatch 76
#define BCExceptionInvalidateData 77
#define BCExceptionFailToCreateBitmap 80
#define BCExceptionOnlyEncodeCODEBAR 82
#define BCExceptionCharactersOutsideISO88591Encoding 87
#define BCExceptionIllegalDataCodewords 88
#define BCExceptionCannotHandleThisNumberOfDataRegions 89
#define BCExceptionIllegalStateUnexpectedCase 90
#define BCExceptionIllegalStateCountMustNotExceed4 91
#define BCExceptionIllegalStateMessageLengthInvalid 92
#define BCExceptionIllegalArgumentNotGigits 93
#define BCExceptionIllegalStateIllegalMode 94
#define BCExceptionNonEncodableCharacterDetected 96
#define BCExceptionErrorCorrectionLevelMustBeBetween0And8 97
#define BCExceptionNoRecommendationPossible 98
#define BCExceptionIllegalArgumentnMustBeAbove0 99
#define BCExceptionUnableToFitMessageInColumns 100
#define BCExceptionEncodedMessageContainsTooManyCodeWords 101
#define BCExceptionBitmapSizeError 102
#define BCExceptionGeneric 107

#endif  // FXBARCODE_UTILS_H_
