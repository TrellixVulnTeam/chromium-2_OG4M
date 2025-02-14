// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fde/cfde_txtedtpage.h"

#include <algorithm>

#include "core/fxcrt/cfx_wordbreak.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"
#include "xfa/fde/cfde_txtedtbuf.h"
#include "xfa/fde/cfde_txtedtengine.h"
#include "xfa/fde/cfde_txtedtparag.h"
#include "xfa/fde/cfde_txtedttextset.h"
#include "xfa/fde/ifde_txtedtengine.h"
#include "xfa/fgas/layout/fgas_textbreak.h"

namespace {

const double kTolerance = 0.1f;

}  // namespace

CFDE_TxtEdtPage::CFDE_TxtEdtPage(CFDE_TxtEdtEngine* pEngine, int32_t nPageIndex)
    : m_pEditEngine(pEngine),
      m_pBgnParag(nullptr),
      m_pEndParag(nullptr),
      m_nRefCount(0),
      m_nPageStart(-1),
      m_nCharCount(0),
      m_nPageIndex(nPageIndex),
      m_bLoaded(false) {
}

CFDE_TxtEdtPage::~CFDE_TxtEdtPage() {}

CFDE_TxtEdtEngine* CFDE_TxtEdtPage::GetEngine() const {
  return m_pEditEngine;
}

FDE_VISUALOBJTYPE CFDE_TxtEdtPage::GetType() {
  return FDE_VISUALOBJ_Text;
}

CFX_RectF CFDE_TxtEdtPage::GetRect(const FDE_TEXTEDITPIECE& hVisualObj) {
  return CFX_RectF();
}

int32_t CFDE_TxtEdtPage::GetCharRect(int32_t nIndex,
                                     CFX_RectF& rect,
                                     bool bBBox) const {
  ASSERT(m_nRefCount > 0);
  ASSERT(nIndex >= 0 && nIndex < m_nCharCount);
  if (m_nRefCount < 1)
    return 0;

  for (const auto& piece : m_Pieces) {
    if (nIndex >= piece.nStart && nIndex < piece.nStart + piece.nCount) {
      std::vector<CFX_RectF> rectArr = m_pTextSet->GetCharRects(&piece, bBBox);
      rect = rectArr[nIndex - piece.nStart];
      return piece.nBidiLevel;
    }
  }
  ASSERT(0);
  return 0;
}

int32_t CFDE_TxtEdtPage::GetCharIndex(const CFX_PointF& fPoint, bool& bBefore) {
  CFX_PointF ptF = fPoint;
  NormalizePt2Rect(ptF, m_rtPageContents, kTolerance);
  int32_t nCount = pdfium::CollectionSize<int32_t>(m_Pieces);
  CFX_RectF rtLine;
  int32_t nBgn = 0;
  int32_t nEnd = 0;
  bool bInLine = false;
  int32_t i = 0;
  for (i = 0; i < nCount; i++) {
    const FDE_TEXTEDITPIECE* pPiece = &m_Pieces[i];
    if (!bInLine &&
        (pPiece->rtPiece.top <= ptF.y && pPiece->rtPiece.bottom() > ptF.y)) {
      nBgn = nEnd = i;
      rtLine = pPiece->rtPiece;
      bInLine = true;
    } else if (bInLine) {
      if (pPiece->rtPiece.bottom() <= ptF.y || pPiece->rtPiece.top > ptF.y) {
        nEnd = i - 1;
        break;
      } else {
        rtLine.Union(pPiece->rtPiece);
      }
    }
  }
  NormalizePt2Rect(ptF, rtLine, kTolerance);
  int32_t nCaret = 0;
  FDE_TEXTEDITPIECE* pPiece = nullptr;
  for (i = nBgn; i <= nEnd; i++) {
    pPiece = &m_Pieces[i];
    nCaret = m_nPageStart + pPiece->nStart;
    if (pPiece->rtPiece.Contains(ptF)) {
      std::vector<CFX_RectF> rectArr = m_pTextSet->GetCharRects(pPiece, false);
      int32_t nRtCount = pdfium::CollectionSize<int32_t>(rectArr);
      for (int32_t j = 0; j < nRtCount; j++) {
        if (rectArr[j].Contains(ptF)) {
          nCaret = m_nPageStart + pPiece->nStart + j;
          if (nCaret >= m_pEditEngine->GetTextBufLength()) {
            bBefore = true;
            return m_pEditEngine->GetTextBufLength();
          }
          wchar_t wChar = m_pEditEngine->GetTextBuf()->GetCharByIndex(nCaret);
          if (wChar == L'\n' || wChar == L'\r') {
            if (wChar == L'\n') {
              if (m_pEditEngine->GetTextBuf()->GetCharByIndex(nCaret - 1) ==
                  L'\r') {
                nCaret--;
              }
            }
            bBefore = true;
            return nCaret;
          }
          if (ptF.x > ((rectArr[j].left + rectArr[j].right()) / 2)) {
            bBefore = FX_IsOdd(pPiece->nBidiLevel);
          } else {
            bBefore = !FX_IsOdd(pPiece->nBidiLevel);
          }
          return nCaret;
        }
      }
    }
  }
  bBefore = true;
  return nCaret;
}

int32_t CFDE_TxtEdtPage::GetCharStart() const {
  return m_nPageStart;
}

int32_t CFDE_TxtEdtPage::GetCharCount() const {
  return m_nCharCount;
}

int32_t CFDE_TxtEdtPage::GetDisplayPos(const CFX_RectF& rtClip,
                                       FXTEXT_CHARPOS*& pCharPos,
                                       CFX_RectF* pBBox) const {
  pCharPos = FX_Alloc(FXTEXT_CHARPOS, m_nCharCount);
  int32_t nCharPosCount = 0;
  FXTEXT_CHARPOS* pos = pCharPos;
  for (const auto& piece : m_Pieces) {
    if (!rtClip.IntersectWith(m_pTextSet->GetRect(piece)))
      continue;

    int32_t nCount = m_pTextSet->GetDisplayPos(piece, pos, false);
    nCharPosCount += nCount;
    pos += nCount;
  }
  if ((nCharPosCount * 5) < (m_nCharCount << 2)) {
    FXTEXT_CHARPOS* pTemp = FX_Alloc(FXTEXT_CHARPOS, nCharPosCount);
    memcpy(pTemp, pCharPos, sizeof(FXTEXT_CHARPOS) * nCharPosCount);
    FX_Free(pCharPos);
    pCharPos = pTemp;
  }
  return nCharPosCount;
}

void CFDE_TxtEdtPage::CalcRangeRectArray(
    int32_t nStart,
    int32_t nCount,
    std::vector<CFX_RectF>* pRectFArr) const {
  int32_t nEnd = nStart + nCount - 1;
  bool bInRange = false;
  for (const auto& piece : m_Pieces) {
    if (!bInRange) {
      if (nStart >= piece.nStart && nStart < piece.nStart + piece.nCount) {
        int32_t nRangeEnd = piece.nCount - 1;
        bool bEnd = false;
        if (nEnd >= piece.nStart && nEnd < piece.nStart + piece.nCount) {
          nRangeEnd = nEnd - piece.nStart;
          bEnd = true;
        }
        std::vector<CFX_RectF> rcArr = m_pTextSet->GetCharRects(&piece, false);
        CFX_RectF rectPiece = rcArr[nStart - piece.nStart];
        rectPiece.Union(rcArr[nRangeEnd]);
        pRectFArr->push_back(rectPiece);
        if (bEnd)
          return;

        bInRange = true;
      }
    } else {
      if (nEnd >= piece.nStart && nEnd < piece.nStart + piece.nCount) {
        std::vector<CFX_RectF> rcArr = m_pTextSet->GetCharRects(&piece, false);
        CFX_RectF rectPiece = rcArr[0];
        rectPiece.Union(rcArr[nEnd - piece.nStart]);
        pRectFArr->push_back(rectPiece);
        return;
      }
      pRectFArr->push_back(piece.rtPiece);
    }
  }
}

int32_t CFDE_TxtEdtPage::SelectWord(const CFX_PointF& fPoint, int32_t& nCount) {
  if (m_nRefCount < 0) {
    return -1;
  }
  CFDE_TxtEdtBuf* pBuf = m_pEditEngine->GetTextBuf();
  bool bBefore;
  int32_t nIndex = GetCharIndex(fPoint, bBefore);
  if (nIndex == m_pEditEngine->GetTextBufLength()) {
    nIndex = m_pEditEngine->GetTextBufLength() - 1;
  }
  if (nIndex < 0) {
    return -1;
  }
  auto pIter = pdfium::MakeUnique<CFX_WordBreak>();
  pIter->Attach(new CFDE_TxtEdtBuf::Iterator(pBuf));
  pIter->SetAt(nIndex);
  nCount = pIter->GetWordLength();
  return pIter->GetWordPos();
}

bool CFDE_TxtEdtPage::IsLoaded(const CFX_RectF* pClipBox) {
  return m_bLoaded;
}

int32_t CFDE_TxtEdtPage::LoadPage(const CFX_RectF* pClipBox,
                                  IFX_Pause* pPause) {
  if (m_nRefCount > 0) {
    m_nRefCount++;
    return m_nRefCount;
  }
  CFDE_TxtEdtBuf* pBuf = m_pEditEngine->GetTextBuf();
  const FDE_TXTEDTPARAMS* pParams = m_pEditEngine->GetEditParams();
  wchar_t wcAlias = 0;
  if (pParams->dwMode & FDE_TEXTEDITMODE_Password) {
    wcAlias = m_pEditEngine->GetAliasChar();
  }
  m_pIter = pdfium::MakeUnique<CFDE_TxtEdtBuf::Iterator>(
      static_cast<CFDE_TxtEdtBuf*>(pBuf), wcAlias);
  CFX_TxtBreak* pBreak = m_pEditEngine->GetTextBreak();
  pBreak->EndBreak(CFX_BreakType::Paragraph);
  pBreak->ClearBreakPieces();
  int32_t nPageLineCount = m_pEditEngine->GetPageLineCount();
  int32_t nStartLine = nPageLineCount * m_nPageIndex;
  int32_t nEndLine = std::min((nStartLine + nPageLineCount - 1),
                              (m_pEditEngine->GetLineCount() - 1));
  int32_t nPageStart, nPageEnd, nTemp, nBgnParag, nStartLineInParag, nEndParag,
      nEndLineInParag;
  nBgnParag = m_pEditEngine->Line2Parag(0, 0, nStartLine, nStartLineInParag);
  m_pBgnParag =
      static_cast<CFDE_TxtEdtParag*>(m_pEditEngine->GetParag(nBgnParag));
  m_pBgnParag->LoadParag();
  m_pBgnParag->GetLineRange(nStartLine - nStartLineInParag, nPageStart, nTemp);
  nEndParag = m_pEditEngine->Line2Parag(nBgnParag, nStartLineInParag, nEndLine,
                                        nEndLineInParag);
  m_pEndParag =
      static_cast<CFDE_TxtEdtParag*>(m_pEditEngine->GetParag(nEndParag));
  m_pEndParag->LoadParag();
  m_pEndParag->GetLineRange(nEndLine - nEndLineInParag, nPageEnd, nTemp);
  nPageEnd += (nTemp - 1);

  float fLineStart = 0.0f;
  float fLineStep = pParams->fLineSpace;
  float fLinePos = fLineStart;
  if (!m_pTextSet)
    m_pTextSet = pdfium::MakeUnique<CFDE_TxtEdtTextSet>(this);

  m_Pieces.clear();
  CFX_BreakType dwBreakStatus = CFX_BreakType::None;
  int32_t nPieceStart = 0;

  m_CharWidths.resize(nPageEnd - nPageStart + 1, 0);
  pBreak->EndBreak(CFX_BreakType::Paragraph);
  pBreak->ClearBreakPieces();
  m_nPageStart = nPageStart;
  m_nCharCount = nPageEnd - nPageStart + 1;
  bool bReload = false;
  float fDefCharWidth = 0;
  std::unique_ptr<IFX_CharIter> pIter(m_pIter->Clone());
  pIter->SetAt(nPageStart);
  m_pIter->SetAt(nPageStart);
  bool bFirstPiece = true;
  do {
    if (bReload) {
      dwBreakStatus = pBreak->EndBreak(CFX_BreakType::Paragraph);
    } else {
      wchar_t wAppend = pIter->GetChar();
      dwBreakStatus = pBreak->AppendChar(wAppend);
    }
    if (pIter->GetAt() == nPageEnd && CFX_BreakTypeNoneOrPiece(dwBreakStatus))
      dwBreakStatus = pBreak->EndBreak(CFX_BreakType::Paragraph);

    if (!CFX_BreakTypeNoneOrPiece(dwBreakStatus)) {
      int32_t nPieceCount = pBreak->CountBreakPieces();
      for (int32_t j = 0; j < nPieceCount; j++) {
        const CFX_BreakPiece* pPiece = pBreak->GetBreakPieceUnstable(j);
        FDE_TEXTEDITPIECE TxtEdtPiece;
        memset(&TxtEdtPiece, 0, sizeof(FDE_TEXTEDITPIECE));
        TxtEdtPiece.nBidiLevel = pPiece->m_iBidiLevel;
        TxtEdtPiece.nCount = pPiece->GetLength();
        TxtEdtPiece.nStart = nPieceStart;
        TxtEdtPiece.dwCharStyles = pPiece->m_dwCharStyles;
        if (FX_IsOdd(pPiece->m_iBidiLevel))
          TxtEdtPiece.dwCharStyles |= FX_TXTCHARSTYLE_OddBidiLevel;

        float fParaBreakWidth = 0.0f;
        if (!CFX_BreakTypeNoneOrPiece(pPiece->m_dwStatus)) {
          wchar_t wRtChar = pParams->wLineBreakChar;
          if (TxtEdtPiece.nCount >= 2) {
            wchar_t wChar = pBuf->GetCharByIndex(
                m_nPageStart + TxtEdtPiece.nStart + TxtEdtPiece.nCount - 1);
            wchar_t wCharPre = pBuf->GetCharByIndex(
                m_nPageStart + TxtEdtPiece.nStart + TxtEdtPiece.nCount - 2);
            if (wChar == wRtChar) {
              fParaBreakWidth += fDefCharWidth;
            }
            if (wCharPre == wRtChar) {
              fParaBreakWidth += fDefCharWidth;
            }
          } else if (TxtEdtPiece.nCount >= 1) {
            wchar_t wChar = pBuf->GetCharByIndex(
                m_nPageStart + TxtEdtPiece.nStart + TxtEdtPiece.nCount - 1);
            if (wChar == wRtChar) {
              fParaBreakWidth += fDefCharWidth;
            }
          }
        }

        TxtEdtPiece.rtPiece.left = (float)pPiece->m_iStartPos / 20000.0f;
        TxtEdtPiece.rtPiece.top = fLinePos;
        TxtEdtPiece.rtPiece.width =
            (float)pPiece->m_iWidth / 20000.0f + fParaBreakWidth;
        TxtEdtPiece.rtPiece.height = pParams->fLineSpace;

        if (bFirstPiece) {
          m_rtPageContents = TxtEdtPiece.rtPiece;
          bFirstPiece = false;
        } else {
          m_rtPageContents.Union(TxtEdtPiece.rtPiece);
        }
        nPieceStart += TxtEdtPiece.nCount;
        m_Pieces.push_back(TxtEdtPiece);
        for (int32_t k = 0; k < TxtEdtPiece.nCount; k++) {
          m_CharWidths[TxtEdtPiece.nStart + k] =
              pPiece->GetChar(k)->m_iCharWidth;
        }
      }
      fLinePos += fLineStep;
      pBreak->ClearBreakPieces();
    }
    if (pIter->GetAt() == nPageEnd && dwBreakStatus == CFX_BreakType::Line) {
      bReload = true;
      pIter->Next(true);
    }
  } while (pIter->Next(false) && (pIter->GetAt() <= nPageEnd));
  if (m_rtPageContents.left != 0) {
    float fDelta = 0.0f;
    if (m_rtPageContents.width < pParams->fPlateWidth) {
      if (pParams->dwAlignment & FDE_TEXTEDITALIGN_Right) {
        fDelta = pParams->fPlateWidth - m_rtPageContents.width;
      } else if (pParams->dwAlignment & FDE_TEXTEDITALIGN_Center) {
        if ((pParams->dwLayoutStyles & FDE_TEXTEDITLAYOUT_CombText) &&
            m_nCharCount > 1) {
          int32_t nCount = m_nCharCount - 1;
          int32_t n = (m_pEditEngine->m_nLimit - nCount) / 2;
          fDelta = (m_rtPageContents.width / nCount) * n;
        } else {
          fDelta = (pParams->fPlateWidth - m_rtPageContents.width) / 2;
        }
      }
    }
    float fOffset = m_rtPageContents.left - fDelta;
    for (auto& piece : m_Pieces)
      piece.rtPiece.Offset(-fOffset, 0.0f);

    m_rtPageContents.Offset(-fOffset, 0.0f);
  }
  if (m_pEditEngine->GetEditParams()->dwLayoutStyles &
      FDE_TEXTEDITLAYOUT_LastLineHeight) {
    m_rtPageContents.height -= pParams->fLineSpace - pParams->fFontSize;
    m_Pieces.back().rtPiece.height = pParams->fFontSize;
  }
  m_nRefCount = 1;
  m_bLoaded = true;
  return 0;
}

void CFDE_TxtEdtPage::UnloadPage(const CFX_RectF* pClipBox) {
  ASSERT(m_nRefCount > 0);
  m_nRefCount--;
  if (m_nRefCount != 0)
    return;

  m_Pieces.clear();
  m_pTextSet.reset();
  m_CharWidths.clear();
  if (m_pBgnParag) {
    m_pBgnParag->UnloadParag();
    m_pBgnParag = nullptr;
  }
  if (m_pEndParag) {
    m_pEndParag->UnloadParag();
    m_pEndParag = nullptr;
  }
  m_pIter.reset();
}

const CFX_RectF& CFDE_TxtEdtPage::GetContentsBox() {
  return m_rtPageContents;
}

size_t CFDE_TxtEdtPage::GetFirstPosition() {
  return m_Pieces.empty() ? 0 : 1;
}

FDE_TEXTEDITPIECE* CFDE_TxtEdtPage::GetNext(size_t* pos,
                                            IFDE_VisualSet*& pVisualSet) {
  ASSERT(pos);

  if (!m_pTextSet) {
    *pos = 0;
    return nullptr;
  }

  size_t nPos = *pos;
  pVisualSet = m_pTextSet.get();
  if (nPos + 1 > m_Pieces.size())
    *pos = 0;
  else
    *pos = nPos + 1;

  return &m_Pieces[nPos - 1];
}

wchar_t CFDE_TxtEdtPage::GetChar(const FDE_TEXTEDITPIECE* pIdentity,
                                 int32_t index) const {
  int32_t nIndex = m_nPageStart + pIdentity->nStart + index;
  if (nIndex != m_pIter->GetAt())
    m_pIter->SetAt(nIndex);

  wchar_t wChar = m_pIter->GetChar();
  m_pIter->Next();
  return wChar;
}

int32_t CFDE_TxtEdtPage::GetWidth(const FDE_TEXTEDITPIECE* pIdentity,
                                  int32_t index) const {
  int32_t nWidth = m_CharWidths[pIdentity->nStart + index];
  return nWidth;
}

void CFDE_TxtEdtPage::NormalizePt2Rect(CFX_PointF& ptF,
                                       const CFX_RectF& rtF,
                                       float fTolerance) const {
  if (rtF.Contains(ptF))
    return;
  if (ptF.x < rtF.left)
    ptF.x = rtF.left;
  else if (ptF.x >= rtF.right())
    ptF.x = rtF.right() - fTolerance;

  if (ptF.y < rtF.top)
    ptF.y = rtF.top;
  else if (ptF.y >= rtF.bottom())
    ptF.y = rtF.bottom() - fTolerance;
}
