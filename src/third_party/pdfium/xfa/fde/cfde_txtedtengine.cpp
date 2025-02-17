// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fde/cfde_txtedtengine.h"

#include <algorithm>
#include <utility>

#include "core/fxcrt/ifx_chariter.h"
#include "third_party/base/ptr_util.h"
#include "xfa/fde/cfde_textout.h"
#include "xfa/fde/cfde_txtedtbuf.h"
#include "xfa/fde/cfde_txtedtdorecord_deleterange.h"
#include "xfa/fde/cfde_txtedtdorecord_insert.h"
#include "xfa/fde/cfde_txtedtpage.h"
#include "xfa/fde/cfde_txtedtparag.h"
#include "xfa/fgas/layout/fgas_textbreak.h"
#include "xfa/fwl/cfwl_edit.h"

namespace {

const uint32_t kPageWidthMax = 0xffff;
const uint32_t kUnicodeParagraphSeparator = 0x2029;

}  // namespace

FDE_TXTEDTPARAMS::FDE_TXTEDTPARAMS()
    : fPlateWidth(0),
      fPlateHeight(0),
      nLineCount(0),
      dwLayoutStyles(0),
      dwAlignment(0),
      dwMode(0),
      fFontSize(10.0f),
      dwFontColor(0xff000000),
      fLineSpace(10.0f),
      fTabWidth(36),
      wDefChar(0xFEFF),
      wLineBreakChar('\n'),
      nLineEnd(0),
      nHorzScale(100),
      fCharSpace(0),
      pEventSink(nullptr) {}

FDE_TXTEDTPARAMS::~FDE_TXTEDTPARAMS() {}

FDE_TXTEDT_TEXTCHANGE_INFO::FDE_TXTEDT_TEXTCHANGE_INFO() {}

FDE_TXTEDT_TEXTCHANGE_INFO::~FDE_TXTEDT_TEXTCHANGE_INFO() {}

CFDE_TxtEdtEngine::CFDE_TxtEdtEngine()
    : m_pTxtBuf(pdfium::MakeUnique<CFDE_TxtEdtBuf>()),
      m_nPageLineCount(20),
      m_nLineCount(0),
      m_nAnchorPos(-1),
      m_nLayoutPos(0),
      m_fCaretPosReserve(0.0),
      m_nCaret(0),
      m_bBefore(true),
      m_nCaretPage(0),
      m_dwFindFlags(0),
      m_bLock(false),
      m_nLimit(0),
      m_wcAliasChar(L'*'),
      m_nFirstLineEnd(FDE_TXTEDIT_LINEEND_Auto),
      m_bAutoLineEnd(true),
      m_wLineEnd(kUnicodeParagraphSeparator) {
  m_bAutoLineEnd = (m_Param.nLineEnd == FDE_TXTEDIT_LINEEND_Auto);
}

CFDE_TxtEdtEngine::~CFDE_TxtEdtEngine() {
  RemoveAllParags();
  RemoveAllPages();
  m_Param.pEventSink = nullptr;
  ClearSelection();
}

void CFDE_TxtEdtEngine::SetEditParams(const FDE_TXTEDTPARAMS& params) {
  if (!m_pTextBreak)
    m_pTextBreak = pdfium::MakeUnique<CFX_TxtBreak>();

  m_Param = params;
  m_wLineEnd = params.wLineBreakChar;
  m_bAutoLineEnd = m_Param.nLineEnd == FDE_TXTEDIT_LINEEND_Auto;
  UpdateTxtBreak();
}

FDE_TXTEDTPARAMS* CFDE_TxtEdtEngine::GetEditParams() {
  return &m_Param;
}

int32_t CFDE_TxtEdtEngine::CountPages() const {
  if (m_nLineCount == 0) {
    return 0;
  }
  return ((m_nLineCount - 1) / m_nPageLineCount) + 1;
}

CFDE_TxtEdtPage* CFDE_TxtEdtEngine::GetPage(int32_t nIndex) {
  if (!pdfium::IndexInBounds(m_PagePtrArray, nIndex))
    return nullptr;
  return m_PagePtrArray[nIndex].get();
}

void CFDE_TxtEdtEngine::SetTextByStream(
    const CFX_RetainPtr<IFGAS_Stream>& pStream) {
  ResetEngine();
  int32_t nIndex = 0;
  if (pStream && pStream->GetLength()) {
    int32_t nStreamLength = pStream->GetLength();
    bool bValid = true;
    if (m_nLimit > 0 && nStreamLength > m_nLimit) {
      bValid = false;
    }
    bool bPreIsCR = false;
    if (bValid) {
      int32_t nPos = pStream->GetBOMLength();
      pStream->Seek(FX_STREAMSEEK_Begin, nPos);
      int32_t nPlateSize = std::min(nStreamLength, m_pTxtBuf->GetChunkSize());
      wchar_t* lpwstr = FX_Alloc(wchar_t, nPlateSize);
      bool bEos = false;
      while (!bEos) {
        int32_t nRead = pStream->ReadString(lpwstr, nPlateSize, bEos);
        bPreIsCR = ReplaceParagEnd(lpwstr, nRead, bPreIsCR);
        m_pTxtBuf->Insert(nIndex, lpwstr, nRead);
        nIndex += nRead;
      }
      FX_Free(lpwstr);
    }
  }
  m_pTxtBuf->Insert(nIndex, &m_wLineEnd, 1);
  RebuildParagraphs();
}

void CFDE_TxtEdtEngine::SetText(const CFX_WideString& wsText) {
  ResetEngine();
  int32_t nLength = wsText.GetLength();
  if (nLength > 0) {
    CFX_WideString wsTemp;
    wchar_t* lpBuffer = wsTemp.GetBuffer(nLength);
    memcpy(lpBuffer, wsText.c_str(), nLength * sizeof(wchar_t));
    ReplaceParagEnd(lpBuffer, nLength, false);
    wsTemp.ReleaseBuffer(nLength);
    if (m_nLimit > 0 && nLength > m_nLimit) {
      wsTemp.Delete(m_nLimit, nLength - m_nLimit);
      nLength = m_nLimit;
    }
    m_pTxtBuf->SetText(wsTemp);
  }
  m_pTxtBuf->Insert(nLength, &m_wLineEnd, 1);
  RebuildParagraphs();
}

int32_t CFDE_TxtEdtEngine::GetTextLength() const {
  return GetTextBufLength();
}

CFX_WideString CFDE_TxtEdtEngine::GetText(int32_t nStart,
                                          int32_t nCount) const {
  int32_t nTextBufLength = GetTextBufLength();
  if (nCount == -1)
    nCount = nTextBufLength - nStart;

  CFX_WideString wsText = m_pTxtBuf->GetRange(nStart, nCount);
  RecoverParagEnd(wsText);
  return wsText;
}

void CFDE_TxtEdtEngine::ClearText() {
  DeleteRange(0, -1);
}

int32_t CFDE_TxtEdtEngine::GetCaretRect(CFX_RectF& rtCaret) const {
  rtCaret = m_rtCaret;
  return m_nCaret;
}

int32_t CFDE_TxtEdtEngine::GetCaretPos() const {
  if (IsLocked())
    return 0;

  return m_nCaret + (m_bBefore ? 0 : 1);
}

int32_t CFDE_TxtEdtEngine::SetCaretPos(int32_t nIndex, bool bBefore) {
  if (IsLocked())
    return 0;

  ASSERT(nIndex >= 0 && nIndex <= GetTextBufLength());
  if (!pdfium::IndexInBounds(m_PagePtrArray, m_nCaretPage))
    return 0;

  m_bBefore = bBefore;
  m_nCaret = nIndex;
  MovePage2Char(m_nCaret);
  GetCaretRect(m_rtCaret, m_nCaretPage, m_nCaret, m_bBefore);
  if (!m_bBefore) {
    m_nCaret++;
    m_bBefore = true;
  }
  m_fCaretPosReserve = m_rtCaret.left;
  m_Param.pEventSink->OnCaretChanged();
  m_nAnchorPos = -1;
  return m_nCaret;
}

int32_t CFDE_TxtEdtEngine::MoveCaretPos(FDE_TXTEDTMOVECARET eMoveCaret,
                                        bool bShift,
                                        bool bCtrl) {
  if (IsLocked() || !pdfium::IndexInBounds(m_PagePtrArray, m_nCaretPage))
    return 0;

  bool bSelChange = false;
  if (IsSelect()) {
    ClearSelection();
    bSelChange = true;
  }
  if (bShift) {
    if (m_nAnchorPos == -1)
      m_nAnchorPos = m_nCaret;
  } else {
    m_nAnchorPos = -1;
  }

  switch (eMoveCaret) {
    case MC_Left: {
      bool bBefore = true;
      int32_t nIndex = MoveBackward(bBefore);
      if (nIndex >= 0)
        UpdateCaretRect(nIndex, bBefore);
      break;
    }
    case MC_Right: {
      bool bBefore = true;
      int32_t nIndex = MoveForward(bBefore);
      if (nIndex >= 0)
        UpdateCaretRect(nIndex, bBefore);
      break;
    }
    case MC_Up: {
      CFX_PointF ptCaret;
      if (MoveUp(ptCaret))
        UpdateCaretIndex(ptCaret);
      break;
    }
    case MC_Down: {
      CFX_PointF ptCaret;
      if (MoveDown(ptCaret))
        UpdateCaretIndex(ptCaret);
      break;
    }
    case MC_WordBackward:
      break;
    case MC_WordForward:
      break;
    case MC_LineStart:
      MoveLineStart();
      break;
    case MC_LineEnd:
      MoveLineEnd();
      break;
    case MC_ParagStart:
      MoveParagStart();
      break;
    case MC_ParagEnd:
      MoveParagEnd();
      break;
    case MC_PageDown:
      break;
    case MC_PageUp:
      break;
    case MC_Home:
      MoveHome();
      break;
    case MC_End:
      MoveEnd();
      break;
    default:
      break;
  }
  if (bShift && m_nAnchorPos != -1 && (m_nAnchorPos != m_nCaret)) {
    AddSelRange(std::min(m_nAnchorPos, m_nCaret), abs(m_nAnchorPos - m_nCaret));
    m_Param.pEventSink->OnSelChanged();
  }
  if (bSelChange)
    m_Param.pEventSink->OnSelChanged();

  return m_nCaret;
}

void CFDE_TxtEdtEngine::Lock() {
  m_bLock = true;
}

void CFDE_TxtEdtEngine::Unlock() {
  m_bLock = false;
}

bool CFDE_TxtEdtEngine::IsLocked() const {
  return m_bLock;
}

int32_t CFDE_TxtEdtEngine::Insert(int32_t nStart,
                                  const wchar_t* lpText,
                                  int32_t nLength) {
  if (IsLocked())
    return FDE_TXTEDT_MODIFY_RET_F_Locked;

  CFX_WideString wsTemp;
  wchar_t* lpBuffer = wsTemp.GetBuffer(nLength);
  memcpy(lpBuffer, lpText, nLength * sizeof(wchar_t));
  ReplaceParagEnd(lpBuffer, nLength, false);
  wsTemp.ReleaseBuffer(nLength);
  bool bPart = false;
  if (m_nLimit > 0) {
    int32_t nTotalLength = GetTextBufLength();
    for (const auto& lpSelRange : m_SelRangePtrArr)
      nTotalLength -= lpSelRange->nCount;

    int32_t nExpectLength = nTotalLength + nLength;
    if (nTotalLength == m_nLimit)
      return FDE_TXTEDT_MODIFY_RET_F_Full;

    if (nExpectLength > m_nLimit) {
      nLength -= (nExpectLength - m_nLimit);
      bPart = true;
    }
  }
  if ((m_Param.dwMode & FDE_TEXTEDITMODE_LimitArea_Vert) ||
      (m_Param.dwMode & FDE_TEXTEDITMODE_LimitArea_Horz)) {
    int32_t nTemp = nLength;
    if (m_Param.dwMode & FDE_TEXTEDITMODE_Password) {
      while (nLength > 0) {
        CFX_WideString wsText = GetPreInsertText(m_nCaret, lpBuffer, nLength);
        int32_t nTotal = wsText.GetLength();
        wchar_t* lpBuf = wsText.GetBuffer(nTotal);
        for (int32_t i = 0; i < nTotal; i++) {
          lpBuf[i] = m_wcAliasChar;
        }
        wsText.ReleaseBuffer(nTotal);
        if (IsFitArea(wsText)) {
          break;
        }
        nLength--;
      }
    } else {
      while (nLength > 0) {
        CFX_WideString wsText = GetPreInsertText(m_nCaret, lpBuffer, nLength);
        if (IsFitArea(wsText)) {
          break;
        }
        nLength--;
      }
    }
    if (nLength == 0) {
      return FDE_TXTEDT_MODIFY_RET_F_Full;
    }
    if (nLength < nTemp) {
      bPart = true;
    }
  }
  if (m_Param.dwMode & FDE_TEXTEDITMODE_Validate) {
    CFX_WideString wsText = GetPreInsertText(m_nCaret, lpBuffer, nLength);
    if (!m_Param.pEventSink->OnValidate(wsText))
      return FDE_TXTEDT_MODIFY_RET_F_Invalidate;
  }
  if (IsSelect()) {
    DeleteSelect();
  }
  m_Param.pEventSink->OnAddDoRecord(
      pdfium::MakeUnique<CFDE_TxtEdtDoRecord_Insert>(this, m_nCaret, lpBuffer,
                                                     nLength));

  m_ChangeInfo.wsPrevText = GetText(0, -1);
  Inner_Insert(m_nCaret, lpBuffer, nLength);
  m_ChangeInfo.nChangeType = FDE_TXTEDT_TEXTCHANGE_TYPE_Insert;
  m_ChangeInfo.wsInsert = CFX_WideString(lpBuffer, nLength);
  nStart = m_nCaret;
  nStart += nLength;
  wchar_t wChar = m_pTxtBuf->GetCharByIndex(nStart - 1);
  bool bBefore = true;
  if (wChar != L'\n' && wChar != L'\r') {
    nStart--;
    bBefore = false;
  }
  SetCaretPos(nStart, bBefore);
  m_Param.pEventSink->OnTextChanged(m_ChangeInfo);
  return bPart ? FDE_TXTEDT_MODIFY_RET_S_Part : FDE_TXTEDT_MODIFY_RET_S_Normal;
}

int32_t CFDE_TxtEdtEngine::Delete(int32_t nStart, bool bBackspace) {
  if (IsLocked()) {
    return FDE_TXTEDT_MODIFY_RET_F_Locked;
  }
  if (IsSelect()) {
    DeleteSelect();
    return FDE_TXTEDT_MODIFY_RET_S_Normal;
  }

  int32_t nCount = 1;
  if (bBackspace) {
    if (nStart == 0) {
      return FDE_TXTEDT_MODIFY_RET_F_Boundary;
    }
    if (nStart > 2 && m_pTxtBuf->GetCharByIndex(nStart - 1) == L'\n' &&
        m_pTxtBuf->GetCharByIndex(nStart - 2) == L'\r') {
      nStart--;
      nCount++;
    }
    nStart--;
  } else {
    if (nStart == GetTextBufLength()) {
      return FDE_TXTEDT_MODIFY_RET_F_Full;
    }
    if ((nStart + 1 < GetTextBufLength()) &&
        (m_pTxtBuf->GetCharByIndex(nStart) == L'\r') &&
        (m_pTxtBuf->GetCharByIndex(nStart + 1) == L'\n')) {
      nCount++;
    }
  }
  if (m_Param.dwMode & FDE_TEXTEDITMODE_Validate) {
    CFX_WideString wsText = GetPreDeleteText(nStart, nCount);
    if (!m_Param.pEventSink->OnValidate(wsText))
      return FDE_TXTEDT_MODIFY_RET_F_Invalidate;
  }
  CFX_WideString wsRange = m_pTxtBuf->GetRange(nStart, nCount);
  m_Param.pEventSink->OnAddDoRecord(
      pdfium::MakeUnique<CFDE_TxtEdtDoRecord_DeleteRange>(this, nStart,
                                                          m_nCaret, wsRange));

  m_ChangeInfo.nChangeType = FDE_TXTEDT_TEXTCHANGE_TYPE_Delete;
  m_ChangeInfo.wsDelete = GetText(nStart, nCount);
  Inner_DeleteRange(nStart, nCount);
  SetCaretPos(nStart + ((!bBackspace && nStart > 0) ? -1 : 0),
              (bBackspace || nStart == 0));
  m_Param.pEventSink->OnTextChanged(m_ChangeInfo);
  return FDE_TXTEDT_MODIFY_RET_S_Normal;
}

int32_t CFDE_TxtEdtEngine::DeleteRange(int32_t nStart, int32_t nCount) {
  if (IsLocked())
    return FDE_TXTEDT_MODIFY_RET_F_Locked;
  if (nCount == -1)
    nCount = GetTextBufLength();
  if (nCount == 0)
    return FDE_TXTEDT_MODIFY_RET_S_Normal;
  if (m_Param.dwMode & FDE_TEXTEDITMODE_Validate) {
    CFX_WideString wsText = GetPreDeleteText(nStart, nCount);
    if (!m_Param.pEventSink->OnValidate(wsText))
      return FDE_TXTEDT_MODIFY_RET_F_Invalidate;
  }
  DeleteRange_DoRecord(nStart, nCount);
  m_Param.pEventSink->OnTextChanged(m_ChangeInfo);
  SetCaretPos(nStart, true);
  return FDE_TXTEDT_MODIFY_RET_S_Normal;
}

int32_t CFDE_TxtEdtEngine::Replace(int32_t nStart,
                                   int32_t nLength,
                                   const CFX_WideString& wsReplace) {
  if (IsLocked())
    return FDE_TXTEDT_MODIFY_RET_F_Locked;
  if (nStart < 0 || (nStart + nLength > GetTextBufLength()))
    return FDE_TXTEDT_MODIFY_RET_F_Boundary;
  if (m_Param.dwMode & FDE_TEXTEDITMODE_Validate) {
    CFX_WideString wsText = GetPreReplaceText(
        nStart, nLength, wsReplace.c_str(), wsReplace.GetLength());
    if (!m_Param.pEventSink->OnValidate(wsText))
      return FDE_TXTEDT_MODIFY_RET_F_Invalidate;
  }
  if (IsSelect())
    ClearSelection();

  m_ChangeInfo.nChangeType = FDE_TXTEDT_TEXTCHANGE_TYPE_Replace;
  m_ChangeInfo.wsDelete = GetText(nStart, nLength);
  if (nLength > 0)
    Inner_DeleteRange(nStart, nLength);

  int32_t nTextLength = wsReplace.GetLength();
  if (nTextLength > 0)
    Inner_Insert(nStart, wsReplace.c_str(), nTextLength);

  m_ChangeInfo.wsInsert = CFX_WideString(wsReplace.c_str(), nTextLength);
  nStart += nTextLength;
  wchar_t wChar = m_pTxtBuf->GetCharByIndex(nStart - 1);
  bool bBefore = true;
  if (wChar != L'\n' && wChar != L'\r') {
    nStart--;
    bBefore = false;
  }
  SetCaretPos(nStart, bBefore);
  m_Param.pEventSink->OnPageUnload(m_nCaretPage);
  m_Param.pEventSink->OnPageLoad(m_nCaretPage);
  m_Param.pEventSink->OnTextChanged(m_ChangeInfo);
  return FDE_TXTEDT_MODIFY_RET_S_Normal;
}

void CFDE_TxtEdtEngine::SetLimit(int32_t nLimit) {
  m_nLimit = nLimit;
}

void CFDE_TxtEdtEngine::SetAliasChar(wchar_t wcAlias) {
  m_wcAliasChar = wcAlias;
}

void CFDE_TxtEdtEngine::RemoveSelRange(int32_t nStart, int32_t nCount) {
  int32_t nRangeCount = pdfium::CollectionSize<int32_t>(m_SelRangePtrArr);
  for (int32_t i = 0; i < nRangeCount; i++) {
    FDE_TXTEDTSELRANGE* lpTemp = m_SelRangePtrArr[i].get();
    if (lpTemp->nStart == nStart && lpTemp->nCount == nCount) {
      m_SelRangePtrArr.erase(m_SelRangePtrArr.begin() + i);
      return;
    }
  }
}

void CFDE_TxtEdtEngine::AddSelRange(int32_t nStart, int32_t nCount) {
  if (nCount == -1)
    nCount = GetTextLength() - nStart;

  if (m_SelRangePtrArr.empty()) {
    auto lpSelRange = pdfium::MakeUnique<FDE_TXTEDTSELRANGE>();
    lpSelRange->nStart = nStart;
    lpSelRange->nCount = nCount;
    m_SelRangePtrArr.push_back(std::move(lpSelRange));
    m_Param.pEventSink->OnSelChanged();
    return;
  }
  auto* lpTemp = m_SelRangePtrArr.back().get();
  if (nStart >= lpTemp->nStart + lpTemp->nCount) {
    auto lpSelRange = pdfium::MakeUnique<FDE_TXTEDTSELRANGE>();
    lpSelRange->nStart = nStart;
    lpSelRange->nCount = nCount;
    m_SelRangePtrArr.push_back(std::move(lpSelRange));
    m_Param.pEventSink->OnSelChanged();
    return;
  }
  int32_t nEnd = nStart + nCount - 1;
  bool bBegin = false;
  int32_t nRangeBgn = 0;
  int32_t nRangeCnt = 0;
  for (int32_t i = 0, nSize = pdfium::CollectionSize<int32_t>(m_SelRangePtrArr);
       i < nSize; i++) {
    lpTemp = m_SelRangePtrArr[i].get();
    int32_t nTempBgn = lpTemp->nStart;
    int32_t nTempEnd = nTempBgn + lpTemp->nCount - 1;
    if (bBegin) {
      if (nEnd < nTempBgn) {
        break;
      } else if (nStart >= nTempBgn && nStart <= nTempEnd) {
        nRangeCnt++;
        break;
      }
      nRangeCnt++;
    } else {
      if (nStart <= nTempEnd) {
        nRangeBgn = i;
        if (nEnd < nTempBgn) {
          break;
        }
        nRangeCnt = 1;
        bBegin = true;
      }
    }
  }
  if (nRangeCnt == 0) {
    auto lpSelRange = pdfium::MakeUnique<FDE_TXTEDTSELRANGE>();
    lpSelRange->nStart = nStart;
    lpSelRange->nCount = nCount;
    m_SelRangePtrArr.insert(m_SelRangePtrArr.begin() + nRangeBgn,
                            std::move(lpSelRange));
  } else {
    lpTemp = m_SelRangePtrArr[nRangeBgn].get();
    lpTemp->nStart = nStart;
    lpTemp->nCount = nCount;
    nRangeCnt--;
    nRangeBgn++;
    m_SelRangePtrArr.erase(m_SelRangePtrArr.begin() + nRangeBgn,
                           m_SelRangePtrArr.begin() + nRangeBgn + nRangeCnt);
  }
  m_Param.pEventSink->OnSelChanged();
}

int32_t CFDE_TxtEdtEngine::CountSelRanges() const {
  return pdfium::CollectionSize<int32_t>(m_SelRangePtrArr);
}

int32_t CFDE_TxtEdtEngine::GetSelRange(int32_t nIndex, int32_t* nStart) const {
  if (nStart)
    *nStart = m_SelRangePtrArr[nIndex]->nStart;
  return m_SelRangePtrArr[nIndex]->nCount;
}

void CFDE_TxtEdtEngine::ClearSelection() {
  if (m_SelRangePtrArr.empty())
    return;
  m_SelRangePtrArr.clear();
  if (m_Param.pEventSink)
    m_Param.pEventSink->OnSelChanged();
}

bool CFDE_TxtEdtEngine::Redo(const IFDE_TxtEdtDoRecord* pDoRecord) {
  if (IsLocked())
    return false;
  return pDoRecord->Redo();
}

bool CFDE_TxtEdtEngine::Undo(const IFDE_TxtEdtDoRecord* pDoRecord) {
  if (IsLocked())
    return false;
  return pDoRecord->Undo();
}

int32_t CFDE_TxtEdtEngine::StartLayout() {
  Lock();
  RemoveAllPages();
  m_nLayoutPos = 0;
  m_nLineCount = 0;
  return 0;
}

int32_t CFDE_TxtEdtEngine::DoLayout(IFX_Pause* pPause) {
  int32_t nCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  CFDE_TxtEdtParag* pParag = nullptr;
  int32_t nLineCount = 0;
  for (; m_nLayoutPos < nCount; m_nLayoutPos++) {
    pParag = m_ParagPtrArray[m_nLayoutPos].get();
    pParag->CalcLines();
    nLineCount += pParag->GetLineCount();
    if (nLineCount > m_nPageLineCount && pPause && pPause->NeedToPauseNow()) {
      m_nLineCount += nLineCount;
      return (++m_nLayoutPos * 100) / nCount;
    }
  }
  m_nLineCount += nLineCount;
  return 100;
}

void CFDE_TxtEdtEngine::EndLayout() {
  UpdatePages();
  int32_t nLength = GetTextLength();
  if (m_nCaret > nLength)
    m_nCaret = nLength;

  int32_t nIndex = m_nCaret;
  if (!m_bBefore)
    nIndex--;

  m_rtCaret = CFX_RectF(0, 0, 1, m_Param.fFontSize);
  Unlock();
}

CFDE_TxtEdtBuf* CFDE_TxtEdtEngine::GetTextBuf() const {
  return m_pTxtBuf.get();
}

int32_t CFDE_TxtEdtEngine::GetTextBufLength() const {
  return m_pTxtBuf->GetTextLength() - 1;
}

CFX_TxtBreak* CFDE_TxtEdtEngine::GetTextBreak() const {
  return m_pTextBreak.get();
}

int32_t CFDE_TxtEdtEngine::GetLineCount() const {
  return m_nLineCount;
}

int32_t CFDE_TxtEdtEngine::GetPageLineCount() const {
  return m_nPageLineCount;
}

int32_t CFDE_TxtEdtEngine::CountParags() const {
  return pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
}

CFDE_TxtEdtParag* CFDE_TxtEdtEngine::GetParag(int32_t nParagIndex) const {
  return m_ParagPtrArray[nParagIndex].get();
}

IFX_CharIter* CFDE_TxtEdtEngine::CreateCharIter() {
  if (!m_pTxtBuf)
    return nullptr;
  return new CFDE_TxtEdtBuf::Iterator(m_pTxtBuf.get());
}

int32_t CFDE_TxtEdtEngine::Line2Parag(int32_t nStartParag,
                                      int32_t nStartLineofParag,
                                      int32_t nLineIndex,
                                      int32_t& nStartLine) const {
  int32_t nLineTotal = nStartLineofParag;
  int32_t nCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  CFDE_TxtEdtParag* pParag = nullptr;
  int32_t i = nStartParag;
  for (; i < nCount; i++) {
    pParag = m_ParagPtrArray[i].get();
    nLineTotal += pParag->GetLineCount();
    if (nLineTotal > nLineIndex) {
      break;
    }
  }
  nStartLine = nLineTotal - pParag->GetLineCount();
  return i;
}

CFX_WideString CFDE_TxtEdtEngine::GetPreDeleteText(int32_t nIndex,
                                                   int32_t nLength) {
  CFX_WideString wsText = GetText(0, GetTextBufLength());
  wsText.Delete(nIndex, nLength);
  return wsText;
}

CFX_WideString CFDE_TxtEdtEngine::GetPreInsertText(int32_t nIndex,
                                                   const wchar_t* lpText,
                                                   int32_t nLength) {
  CFX_WideString wsText = GetText(0, GetTextBufLength());
  int32_t nSelIndex = 0;
  int32_t nSelLength = 0;
  int32_t nSelCount = CountSelRanges();
  while (nSelCount--) {
    nSelLength = GetSelRange(nSelCount, &nSelIndex);
    wsText.Delete(nSelIndex, nSelLength);
    nIndex = nSelIndex;
  }
  CFX_WideString wsTemp;
  int32_t nOldLength = wsText.GetLength();
  const wchar_t* pOldBuffer = wsText.c_str();
  wchar_t* lpBuffer = wsTemp.GetBuffer(nOldLength + nLength);
  memcpy(lpBuffer, pOldBuffer, (nIndex) * sizeof(wchar_t));
  memcpy(lpBuffer + nIndex, lpText, nLength * sizeof(wchar_t));
  memcpy(lpBuffer + nIndex + nLength, pOldBuffer + nIndex,
         (nOldLength - nIndex) * sizeof(wchar_t));
  wsTemp.ReleaseBuffer(nOldLength + nLength);
  wsText = wsTemp;
  return wsText;
}

CFX_WideString CFDE_TxtEdtEngine::GetPreReplaceText(int32_t nIndex,
                                                    int32_t nOriginLength,
                                                    const wchar_t* lpText,
                                                    int32_t nLength) {
  CFX_WideString wsText = GetText(0, GetTextBufLength());
  int32_t nSelIndex = 0;
  int32_t nSelLength = 0;
  int32_t nSelCount = CountSelRanges();
  while (nSelCount--) {
    nSelLength = GetSelRange(nSelCount, &nSelIndex);
    wsText.Delete(nSelIndex, nSelLength);
  }
  wsText.Delete(nIndex, nOriginLength);
  int32_t i = 0;
  for (i = 0; i < nLength; i++)
    wsText.Insert(nIndex++, lpText[i]);

  return wsText;
}

void CFDE_TxtEdtEngine::Inner_Insert(int32_t nStart,
                                     const wchar_t* lpText,
                                     int32_t nLength) {
  ASSERT(nLength > 0);
  FDE_TXTEDTPARAGPOS ParagPos;
  TextPos2ParagPos(nStart, ParagPos);
  m_Param.pEventSink->OnPageUnload(m_nCaretPage);
  int32_t nParagCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  for (int32_t i = ParagPos.nParagIndex + 1; i < nParagCount; i++)
    m_ParagPtrArray[i]->IncrementStartIndex(nLength);

  CFDE_TxtEdtParag* pParag = m_ParagPtrArray[ParagPos.nParagIndex].get();
  int32_t nReserveLineCount = pParag->GetLineCount();
  int32_t nReserveCharStart = pParag->GetStartIndex();
  int32_t nLeavePart = ParagPos.nCharIndex;
  int32_t nCutPart = pParag->GetTextLength() - ParagPos.nCharIndex;
  int32_t nTextStart = 0;
  wchar_t wCurChar = L' ';
  const wchar_t* lpPos = lpText;
  bool bFirst = true;
  int32_t nParagIndex = ParagPos.nParagIndex;
  for (int32_t i = 0; i < nLength; i++, lpPos++) {
    wCurChar = *lpPos;
    if (wCurChar == m_wLineEnd) {
      if (bFirst) {
        pParag->SetTextLength(nLeavePart + (i - nTextStart + 1));
        pParag->SetLineCount(-1);
        nReserveCharStart += pParag->GetTextLength();
        bFirst = false;
      } else {
        auto pParag2 = pdfium::MakeUnique<CFDE_TxtEdtParag>(this);
        pParag2->SetLineCount(-1);
        pParag2->SetTextLength(i - nTextStart + 1);
        pParag2->SetStartIndex(nReserveCharStart);
        nReserveCharStart += pParag2->GetTextLength();
        m_ParagPtrArray.insert(m_ParagPtrArray.begin() + ++nParagIndex,
                               std::move(pParag2));
      }
      nTextStart = i + 1;
    }
  }
  if (bFirst) {
    pParag->IncrementTextLength(nLength);
    pParag->SetLineCount(-1);
    bFirst = false;
  } else {
    auto pParag2 = pdfium::MakeUnique<CFDE_TxtEdtParag>(this);
    pParag2->SetLineCount(-1);
    pParag2->SetTextLength(nLength - nTextStart + nCutPart);
    pParag2->SetStartIndex(nReserveCharStart);
    m_ParagPtrArray.insert(m_ParagPtrArray.begin() + ++nParagIndex,
                           std::move(pParag2));
  }
  m_pTxtBuf->Insert(nStart, lpText, nLength);
  int32_t nTotalLineCount = 0;
  for (int32_t i = ParagPos.nParagIndex; i <= nParagIndex; i++) {
    pParag = m_ParagPtrArray[i].get();
    pParag->CalcLines();
    nTotalLineCount += pParag->GetLineCount();
  }
  m_nLineCount += nTotalLineCount - nReserveLineCount;
  m_Param.pEventSink->OnPageLoad(m_nCaretPage);
  UpdatePages();
}

void CFDE_TxtEdtEngine::Inner_DeleteRange(int32_t nStart, int32_t nCount) {
  if (nCount == -1) {
    nCount = m_pTxtBuf->GetTextLength() - nStart;
  }
  int32_t nEnd = nStart + nCount - 1;
  ASSERT(nStart >= 0 && nEnd < m_pTxtBuf->GetTextLength());
  m_Param.pEventSink->OnPageUnload(m_nCaretPage);
  FDE_TXTEDTPARAGPOS ParagPosBgn, ParagPosEnd;
  TextPos2ParagPos(nStart, ParagPosBgn);
  TextPos2ParagPos(nEnd, ParagPosEnd);
  CFDE_TxtEdtParag* pParag = m_ParagPtrArray[ParagPosEnd.nParagIndex].get();
  bool bLastParag = false;
  if (ParagPosEnd.nCharIndex == pParag->GetTextLength() - 1) {
    if (ParagPosEnd.nParagIndex <
        pdfium::CollectionSize<int32_t>(m_ParagPtrArray) - 1) {
      ParagPosEnd.nParagIndex++;
    } else {
      bLastParag = true;
    }
  }
  int32_t nTotalLineCount = 0;
  int32_t nTotalCharCount = 0;
  int32_t i = 0;
  for (i = ParagPosBgn.nParagIndex; i <= ParagPosEnd.nParagIndex; i++) {
    CFDE_TxtEdtParag* pTextParag = m_ParagPtrArray[i].get();
    pTextParag->CalcLines();
    nTotalLineCount += pTextParag->GetLineCount();
    nTotalCharCount += pTextParag->GetTextLength();
  }
  m_pTxtBuf->Delete(nStart, nCount);
  int32_t nNextParagIndex = (ParagPosBgn.nCharIndex == 0 && bLastParag)
                                ? ParagPosBgn.nParagIndex
                                : (ParagPosBgn.nParagIndex + 1);
  m_ParagPtrArray.erase(m_ParagPtrArray.begin() + nNextParagIndex,
                        m_ParagPtrArray.begin() + ParagPosEnd.nParagIndex + 1);

  if (!(bLastParag && ParagPosBgn.nCharIndex == 0)) {
    pParag = m_ParagPtrArray[ParagPosBgn.nParagIndex].get();
    pParag->SetTextLength(nTotalCharCount - nCount);
    pParag->CalcLines();
    nTotalLineCount -= pParag->GetTextLength();
  }
  int32_t nParagCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  for (i = nNextParagIndex; i < nParagCount; i++)
    m_ParagPtrArray[i]->DecrementStartIndex(nCount);

  m_nLineCount -= nTotalLineCount;
  UpdatePages();
  int32_t nPageCount = CountPages();
  if (m_nCaretPage >= nPageCount) {
    m_nCaretPage = nPageCount - 1;
  }
  m_Param.pEventSink->OnPageLoad(m_nCaretPage);
}

void CFDE_TxtEdtEngine::DeleteRange_DoRecord(int32_t nStart,
                                             int32_t nCount,
                                             bool bSel) {
  ASSERT(nStart >= 0);
  if (nCount == -1) {
    nCount = GetTextLength() - nStart;
  }
  ASSERT((nStart + nCount) <= m_pTxtBuf->GetTextLength());

  CFX_WideString wsRange = m_pTxtBuf->GetRange(nStart, nCount);
  m_Param.pEventSink->OnAddDoRecord(
      pdfium::MakeUnique<CFDE_TxtEdtDoRecord_DeleteRange>(
          this, nStart, m_nCaret, wsRange, bSel));

  m_ChangeInfo.nChangeType = FDE_TXTEDT_TEXTCHANGE_TYPE_Delete;
  m_ChangeInfo.wsDelete = GetText(nStart, nCount);
  Inner_DeleteRange(nStart, nCount);
}

void CFDE_TxtEdtEngine::ResetEngine() {
  RemoveAllPages();
  RemoveAllParags();
  ClearSelection();
  m_nCaret = 0;
  m_pTxtBuf->Clear(false);
  m_nCaret = 0;
}

void CFDE_TxtEdtEngine::RebuildParagraphs() {
  RemoveAllParags();
  wchar_t wChar = L' ';
  int32_t nParagStart = 0;
  int32_t nIndex = 0;
  auto pIter = pdfium::MakeUnique<CFDE_TxtEdtBuf::Iterator>(m_pTxtBuf.get());
  pIter->SetAt(0);
  do {
    wChar = pIter->GetChar();
    nIndex = pIter->GetAt();
    if (wChar == m_wLineEnd) {
      auto pParag = pdfium::MakeUnique<CFDE_TxtEdtParag>(this);
      pParag->SetStartIndex(nParagStart);
      pParag->SetTextLength(nIndex - nParagStart + 1);
      pParag->SetLineCount(-1);
      m_ParagPtrArray.push_back(std::move(pParag));
      nParagStart = nIndex + 1;
    }
  } while (pIter->Next());
}

void CFDE_TxtEdtEngine::RemoveAllParags() {
  m_ParagPtrArray.clear();
}

void CFDE_TxtEdtEngine::RemoveAllPages() {
  m_PagePtrArray.clear();
}

void CFDE_TxtEdtEngine::UpdateParags() {
  if (m_ParagPtrArray.empty())
    return;

  int32_t nLineCount = 0;
  for (auto& pParag : m_ParagPtrArray) {
    if (pParag->GetLineCount() == -1)
      pParag->CalcLines();
    nLineCount += pParag->GetLineCount();
  }
  m_nLineCount = nLineCount;
}

void CFDE_TxtEdtEngine::UpdatePages() {
  if (m_nLineCount == 0)
    return;

  int32_t nPageCount = (m_nLineCount - 1) / (m_nPageLineCount) + 1;
  int32_t nSize = pdfium::CollectionSize<int32_t>(m_PagePtrArray);
  if (nSize == nPageCount)
    return;

  if (nSize > nPageCount) {
    m_PagePtrArray.erase(m_PagePtrArray.begin() + nPageCount,
                         m_PagePtrArray.end());
    return;
  }
  for (int32_t i = nSize; i < nPageCount; i++)
    m_PagePtrArray.push_back(pdfium::MakeUnique<CFDE_TxtEdtPage>(this, i));
}

void CFDE_TxtEdtEngine::UpdateTxtBreak() {
  uint32_t dwStyle = m_pTextBreak->GetLayoutStyles();
  if (m_Param.dwMode & FDE_TEXTEDITMODE_MultiLines)
    dwStyle &= ~FX_LAYOUTSTYLE_SingleLine;
  else
    dwStyle |= FX_LAYOUTSTYLE_SingleLine;

  if (m_Param.dwLayoutStyles & FDE_TEXTEDITLAYOUT_CombText)
    dwStyle |= FX_LAYOUTSTYLE_CombText;
  else
    dwStyle &= ~FX_LAYOUTSTYLE_CombText;

  m_pTextBreak->SetLayoutStyles(dwStyle);
  uint32_t dwAligment = 0;
  if (m_Param.dwAlignment & FDE_TEXTEDITALIGN_Justified)
    dwAligment |= CFX_TxtLineAlignment_Justified;

  if (m_Param.dwAlignment & FDE_TEXTEDITALIGN_Center)
    dwAligment |= CFX_TxtLineAlignment_Center;
  else if (m_Param.dwAlignment & FDE_TEXTEDITALIGN_Right)
    dwAligment |= CFX_TxtLineAlignment_Right;

  m_pTextBreak->SetAlignment(dwAligment);

  if (m_Param.dwMode & FDE_TEXTEDITMODE_AutoLineWrap)
    m_pTextBreak->SetLineWidth(m_Param.fPlateWidth);
  else
    m_pTextBreak->SetLineWidth(kPageWidthMax);

  m_nPageLineCount = m_Param.nLineCount;
  if (m_Param.dwLayoutStyles & FDE_TEXTEDITLAYOUT_CombText) {
    float fCombWidth = m_Param.fPlateWidth;
    if (m_nLimit > 0)
      fCombWidth /= m_nLimit;

    m_pTextBreak->SetCombWidth(fCombWidth);
  }
  m_pTextBreak->SetFont(m_Param.pFont);
  m_pTextBreak->SetFontSize(m_Param.fFontSize);
  m_pTextBreak->SetTabWidth(m_Param.fTabWidth);
  m_pTextBreak->SetDefaultChar(m_Param.wDefChar);
  m_pTextBreak->SetParagraphBreakChar(m_Param.wLineBreakChar);
  m_pTextBreak->SetLineBreakTolerance(m_Param.fFontSize * 0.2f);
  m_pTextBreak->SetHorizontalScale(m_Param.nHorzScale);
  m_pTextBreak->SetCharSpace(m_Param.fCharSpace);
}

bool CFDE_TxtEdtEngine::ReplaceParagEnd(wchar_t*& lpText,
                                        int32_t& nLength,
                                        bool bPreIsCR) {
  for (int32_t i = 0; i < nLength; i++) {
    wchar_t wc = lpText[i];
    switch (wc) {
      case L'\r': {
        lpText[i] = m_wLineEnd;
        bPreIsCR = true;
      } break;
      case L'\n': {
        if (bPreIsCR == true) {
          int32_t nNext = i + 1;
          if (nNext < nLength) {
            memmove(lpText + i, lpText + nNext,
                    (nLength - nNext) * sizeof(wchar_t));
          }
          i--;
          nLength--;
          bPreIsCR = false;
          if (m_bAutoLineEnd) {
            m_nFirstLineEnd = FDE_TXTEDIT_LINEEND_CRLF;
            m_bAutoLineEnd = false;
          }
        } else {
          lpText[i] = m_wLineEnd;
          if (m_bAutoLineEnd) {
            m_nFirstLineEnd = FDE_TXTEDIT_LINEEND_LF;
            m_bAutoLineEnd = false;
          }
        }
      } break;
      default: {
        if (bPreIsCR && m_bAutoLineEnd) {
          m_nFirstLineEnd = FDE_TXTEDIT_LINEEND_CR;
          m_bAutoLineEnd = false;
        }
        bPreIsCR = false;
      } break;
    }
  }
  return bPreIsCR;
}

void CFDE_TxtEdtEngine::RecoverParagEnd(CFX_WideString& wsText) const {
  wchar_t wc = (m_nFirstLineEnd == FDE_TXTEDIT_LINEEND_CR) ? L'\n' : L'\r';
  if (m_nFirstLineEnd == FDE_TXTEDIT_LINEEND_CRLF) {
    std::vector<int32_t> PosArr;
    int32_t nLength = wsText.GetLength();
    wchar_t* lpPos = const_cast<wchar_t*>(wsText.c_str());
    for (int32_t i = 0; i < nLength; i++, lpPos++) {
      if (*lpPos == m_wLineEnd) {
        *lpPos = wc;
        PosArr.push_back(i);
      }
    }
    const wchar_t* lpSrcBuf = wsText.c_str();
    CFX_WideString wsTemp;
    int32_t nCount = pdfium::CollectionSize<int32_t>(PosArr);
    wchar_t* lpDstBuf = wsTemp.GetBuffer(nLength + nCount);
    int32_t nDstPos = 0;
    int32_t nSrcPos = 0;
    for (int32_t i = 0; i < nCount; i++) {
      int32_t nPos = PosArr[i];
      int32_t nCopyLen = nPos - nSrcPos + 1;
      memcpy(lpDstBuf + nDstPos, lpSrcBuf + nSrcPos,
             nCopyLen * sizeof(wchar_t));
      nDstPos += nCopyLen;
      nSrcPos += nCopyLen;
      lpDstBuf[nDstPos] = L'\n';
      nDstPos++;
    }
    if (nSrcPos < nLength) {
      memcpy(lpDstBuf + nDstPos, lpSrcBuf + nSrcPos,
             (nLength - nSrcPos) * sizeof(wchar_t));
    }
    wsTemp.ReleaseBuffer(nLength + nCount);
    wsText = wsTemp;
  } else {
    int32_t nLength = wsText.GetLength();
    wchar_t* lpBuf = const_cast<wchar_t*>(wsText.c_str());
    for (int32_t i = 0; i < nLength; i++, lpBuf++) {
      if (*lpBuf == m_wLineEnd)
        *lpBuf = wc;
    }
  }
}

int32_t CFDE_TxtEdtEngine::MovePage2Char(int32_t nIndex) {
  ASSERT(nIndex >= 0);
  ASSERT(nIndex <= m_pTxtBuf->GetTextLength());
  if (m_nCaretPage >= 0) {
    CFDE_TxtEdtPage* pPage = m_PagePtrArray[m_nCaretPage].get();
    m_Param.pEventSink->OnPageLoad(m_nCaretPage);
    int32_t nPageCharStart = pPage->GetCharStart();
    int32_t nPageCharCount = pPage->GetCharCount();
    if (nIndex >= nPageCharStart && nIndex < nPageCharStart + nPageCharCount) {
      m_Param.pEventSink->OnPageUnload(m_nCaretPage);
      return m_nCaretPage;
    }
    m_Param.pEventSink->OnPageUnload(m_nCaretPage);
  }
  CFDE_TxtEdtParag* pParag = nullptr;
  int32_t nLineCount = 0;
  int32_t nParagCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  int32_t i = 0;
  for (i = 0; i < nParagCount; i++) {
    pParag = m_ParagPtrArray[i].get();
    if (pParag->GetStartIndex() <= nIndex &&
        nIndex < (pParag->GetStartIndex() + pParag->GetTextLength())) {
      break;
    }
    nLineCount += pParag->GetLineCount();
  }
  pParag->LoadParag();
  int32_t nLineStart = -1;
  int32_t nLineCharCount = -1;
  for (i = 0; i < pParag->GetLineCount(); i++) {
    pParag->GetLineRange(i, nLineStart, nLineCharCount);
    if (nLineStart <= nIndex && nIndex < (nLineStart + nLineCharCount))
      break;
  }
  ASSERT(i < pParag->GetLineCount());
  nLineCount += (i + 1);
  m_nCaretPage = (nLineCount - 1) / m_nPageLineCount + 1 - 1;
  pParag->UnloadParag();
  return m_nCaretPage;
}

void CFDE_TxtEdtEngine::TextPos2ParagPos(int32_t nIndex,
                                         FDE_TXTEDTPARAGPOS& ParagPos) const {
  ASSERT(nIndex >= 0 && nIndex < m_pTxtBuf->GetTextLength());
  int32_t nCount = pdfium::CollectionSize<int32_t>(m_ParagPtrArray);
  int32_t nBgn = 0;
  int32_t nMid = 0;
  int32_t nEnd = nCount - 1;
  while (nEnd > nBgn) {
    nMid = (nBgn + nEnd) / 2;
    CFDE_TxtEdtParag* pParag = m_ParagPtrArray[nMid].get();
    if (nIndex < pParag->GetStartIndex())
      nEnd = nMid - 1;
    else if (nIndex >= (pParag->GetStartIndex() + pParag->GetTextLength()))
      nBgn = nMid + 1;
    else
      break;
  }
  if (nBgn == nEnd)
    nMid = nBgn;

  ASSERT(nIndex >= m_ParagPtrArray[nMid]->GetStartIndex() &&
         (nIndex < m_ParagPtrArray[nMid]->GetStartIndex() +
                       m_ParagPtrArray[nMid]->GetTextLength()));
  ParagPos.nParagIndex = nMid;
  ParagPos.nCharIndex = nIndex - m_ParagPtrArray[nMid]->GetStartIndex();
}

int32_t CFDE_TxtEdtEngine::MoveForward(bool& bBefore) {
  if (m_nCaret == m_pTxtBuf->GetTextLength() - 1)
    return -1;

  int32_t nCaret = m_nCaret;
  if ((nCaret + 1 < m_pTxtBuf->GetTextLength()) &&
      (m_pTxtBuf->GetCharByIndex(nCaret) == L'\r') &&
      (m_pTxtBuf->GetCharByIndex(nCaret + 1) == L'\n')) {
    nCaret++;
  }
  nCaret++;
  bBefore = true;
  return nCaret;
}

int32_t CFDE_TxtEdtEngine::MoveBackward(bool& bBefore) {
  if (m_nCaret == 0)
    return false;

  int32_t nCaret = m_nCaret;
  if (nCaret > 2 && m_pTxtBuf->GetCharByIndex(nCaret - 1) == L'\n' &&
      m_pTxtBuf->GetCharByIndex(nCaret - 2) == L'\r') {
    nCaret--;
  }
  nCaret--;
  bBefore = true;
  return nCaret;
}

bool CFDE_TxtEdtEngine::MoveUp(CFX_PointF& ptCaret) {
  CFDE_TxtEdtPage* pPage = GetPage(m_nCaretPage);
  const CFX_RectF& rtContent = pPage->GetContentsBox();
  ptCaret.x = m_fCaretPosReserve;
  ptCaret.y = m_rtCaret.top + m_rtCaret.height / 2 - m_Param.fLineSpace;
  if (ptCaret.y < rtContent.top) {
    if (m_nCaretPage == 0) {
      return false;
    }
    ptCaret.y -= rtContent.top;
    m_nCaretPage--;
    CFDE_TxtEdtPage* pCurPage = GetPage(m_nCaretPage);
    ptCaret.y += pCurPage->GetContentsBox().bottom();
  }

  return true;
}

bool CFDE_TxtEdtEngine::MoveDown(CFX_PointF& ptCaret) {
  CFDE_TxtEdtPage* pPage = GetPage(m_nCaretPage);
  const CFX_RectF& rtContent = pPage->GetContentsBox();
  ptCaret.x = m_fCaretPosReserve;
  ptCaret.y = m_rtCaret.top + m_rtCaret.height / 2 + m_Param.fLineSpace;
  if (ptCaret.y >= rtContent.bottom()) {
    if (m_nCaretPage == CountPages() - 1) {
      return false;
    }
    ptCaret.y -= rtContent.bottom();
    m_nCaretPage++;
    CFDE_TxtEdtPage* pCurPage = GetPage(m_nCaretPage);
    ptCaret.y += pCurPage->GetContentsBox().top;
  }
  return true;
}

bool CFDE_TxtEdtEngine::MoveLineStart() {
  int32_t nIndex = m_bBefore ? m_nCaret : m_nCaret - 1;
  FDE_TXTEDTPARAGPOS ParagPos;
  TextPos2ParagPos(nIndex, ParagPos);
  CFDE_TxtEdtParag* pParag = m_ParagPtrArray[ParagPos.nParagIndex].get();
  pParag->LoadParag();
  int32_t nLineCount = pParag->GetLineCount();
  int32_t i = 0;
  int32_t nStart = 0;
  int32_t nCount = 0;
  for (; i < nLineCount; i++) {
    pParag->GetLineRange(i, nStart, nCount);
    if (nIndex >= nStart && nIndex < nStart + nCount) {
      break;
    }
  }
  UpdateCaretRect(nStart, true);
  pParag->UnloadParag();
  return true;
}

bool CFDE_TxtEdtEngine::MoveLineEnd() {
  int32_t nIndex = m_bBefore ? m_nCaret : m_nCaret - 1;
  FDE_TXTEDTPARAGPOS ParagPos;
  TextPos2ParagPos(nIndex, ParagPos);
  CFDE_TxtEdtParag* pParag = m_ParagPtrArray[ParagPos.nParagIndex].get();
  pParag->LoadParag();
  int32_t nLineCount = pParag->GetLineCount();
  int32_t i = 0;
  int32_t nStart = 0;
  int32_t nCount = 0;
  for (; i < nLineCount; i++) {
    pParag->GetLineRange(i, nStart, nCount);
    if (nIndex >= nStart && nIndex < nStart + nCount) {
      break;
    }
  }
  nIndex = nStart + nCount - 1;
  ASSERT(nIndex <= GetTextBufLength());
  wchar_t wChar = m_pTxtBuf->GetCharByIndex(nIndex);
  bool bBefore = false;
  if (nIndex <= GetTextBufLength()) {
    if (wChar == L'\r') {
      bBefore = true;
    } else if (wChar == L'\n' && nIndex > nStart) {
      bBefore = true;
      nIndex--;
      wChar = m_pTxtBuf->GetCharByIndex(nIndex);
      if (wChar != L'\r') {
        nIndex++;
      }
    }
  }
  UpdateCaretRect(nIndex, bBefore);
  pParag->UnloadParag();
  return true;
}

bool CFDE_TxtEdtEngine::MoveParagStart() {
  FDE_TXTEDTPARAGPOS ParagPos;
  TextPos2ParagPos(m_bBefore ? m_nCaret : m_nCaret - 1, ParagPos);
  UpdateCaretRect(m_ParagPtrArray[ParagPos.nParagIndex]->GetStartIndex(), true);
  return true;
}

bool CFDE_TxtEdtEngine::MoveParagEnd() {
  int32_t nIndex = m_bBefore ? m_nCaret : m_nCaret - 1;
  FDE_TXTEDTPARAGPOS ParagPos;
  TextPos2ParagPos(nIndex, ParagPos);
  CFDE_TxtEdtParag* pParag = m_ParagPtrArray[ParagPos.nParagIndex].get();
  nIndex = pParag->GetStartIndex() + pParag->GetTextLength() - 1;
  wchar_t wChar = m_pTxtBuf->GetCharByIndex(nIndex);
  if (wChar == L'\n' && nIndex > 0) {
    nIndex--;
    wChar = m_pTxtBuf->GetCharByIndex(nIndex);
    if (wChar != L'\r') {
      nIndex++;
    }
  }
  UpdateCaretRect(nIndex, true);
  return true;
}

bool CFDE_TxtEdtEngine::MoveHome() {
  UpdateCaretRect(0, true);
  return true;
}

bool CFDE_TxtEdtEngine::MoveEnd() {
  UpdateCaretRect(GetTextBufLength(), true);
  return true;
}

bool CFDE_TxtEdtEngine::IsFitArea(CFX_WideString& wsText) {
  auto pTextOut = pdfium::MakeUnique<CFDE_TextOut>();
  pTextOut->SetLineSpace(m_Param.fLineSpace);
  pTextOut->SetFont(m_Param.pFont);
  pTextOut->SetFontSize(m_Param.fFontSize);
  uint32_t dwStyle = 0;
  if (!(m_Param.dwMode & FDE_TEXTEDITMODE_MultiLines))
    dwStyle |= FDE_TTOSTYLE_SingleLine;

  CFX_RectF rcText;
  if (m_Param.dwMode & FDE_TEXTEDITMODE_AutoLineWrap) {
    dwStyle |= FDE_TTOSTYLE_LineWrap;
    rcText.width = m_Param.fPlateWidth;
  } else {
    rcText.width = 65535;
  }
  pTextOut->SetStyles(dwStyle);
  wsText += L"\n";
  pTextOut->CalcLogicSize(wsText.c_str(), wsText.GetLength(), rcText);
  wsText.Delete(wsText.GetLength() - 1);
  if ((m_Param.dwMode & FDE_TEXTEDITMODE_LimitArea_Horz) &&
      (rcText.width > m_Param.fPlateWidth)) {
    return false;
  }
  if ((m_Param.dwMode & FDE_TEXTEDITMODE_LimitArea_Vert) &&
      (rcText.height > m_Param.fLineSpace * m_Param.nLineCount)) {
    return false;
  }
  return true;
}

void CFDE_TxtEdtEngine::UpdateCaretRect(int32_t nIndex, bool bBefore) {
  MovePage2Char(nIndex);
  GetCaretRect(m_rtCaret, m_nCaretPage, nIndex, bBefore);
  m_nCaret = nIndex;
  m_bBefore = bBefore;
  if (!m_bBefore) {
    m_nCaret++;
    m_bBefore = true;
  }
  m_fCaretPosReserve = m_rtCaret.left;
  m_Param.pEventSink->OnCaretChanged();
}

void CFDE_TxtEdtEngine::GetCaretRect(CFX_RectF& rtCaret,
                                     int32_t nPageIndex,
                                     int32_t nCaret,
                                     bool bBefore) {
  CFDE_TxtEdtPage* pPage = m_PagePtrArray[m_nCaretPage].get();
  m_Param.pEventSink->OnPageLoad(m_nCaretPage);

  bool bCombText = !!(m_Param.dwLayoutStyles & FDE_TEXTEDITLAYOUT_CombText);
  int32_t nIndexInpage = nCaret - pPage->GetCharStart();
  if (bBefore && bCombText && nIndexInpage > 0) {
    nIndexInpage--;
    bBefore = false;
  }
  int32_t nBIDILevel = pPage->GetCharRect(nIndexInpage, rtCaret, bCombText);
  if ((!FX_IsOdd(nBIDILevel) && !bBefore) ||
      (FX_IsOdd(nBIDILevel) && bBefore)) {
    rtCaret.Offset(rtCaret.width - 1.0f, 0);
  }
  if (rtCaret.width == 0 && rtCaret.left > 1.0f)
    rtCaret.left -= 1.0f;

  rtCaret.width = 1.0f;

  m_Param.pEventSink->OnPageUnload(m_nCaretPage);
}

void CFDE_TxtEdtEngine::UpdateCaretIndex(const CFX_PointF& ptCaret) {
  CFDE_TxtEdtPage* pPage = m_PagePtrArray[m_nCaretPage].get();
  m_Param.pEventSink->OnPageLoad(m_nCaretPage);
  m_nCaret = pPage->GetCharIndex(ptCaret, m_bBefore);
  GetCaretRect(m_rtCaret, m_nCaretPage, m_nCaret, m_bBefore);
  if (!m_bBefore) {
    m_nCaret++;
    m_bBefore = true;
  }
  m_Param.pEventSink->OnCaretChanged();
  m_Param.pEventSink->OnPageUnload(m_nCaretPage);
}

bool CFDE_TxtEdtEngine::IsSelect() {
  return !m_SelRangePtrArr.empty();
}

void CFDE_TxtEdtEngine::DeleteSelect() {
  int32_t nCountRange = CountSelRanges();
  if (nCountRange <= 0)
    return;

  int32_t nSelStart = 0;
  while (nCountRange > 0) {
    int32_t nSelCount = GetSelRange(--nCountRange, &nSelStart);
    m_SelRangePtrArr.erase(m_SelRangePtrArr.begin() + nCountRange);
    DeleteRange_DoRecord(nSelStart, nSelCount, true);
  }
  ClearSelection();
  m_Param.pEventSink->OnTextChanged(m_ChangeInfo);
  m_Param.pEventSink->OnSelChanged();
  SetCaretPos(nSelStart, true);
}
