// Copyright 2014 PDFium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "xfa/fxfa/parser/cxfa_resolveprocessor.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/fxcrt/fx_ext.h"
#include "third_party/base/ptr_util.h"
#include "third_party/base/stl_util.h"
#include "xfa/fxfa/parser/cxfa_document.h"
#include "xfa/fxfa/parser/cxfa_localemgr.h"
#include "xfa/fxfa/parser/cxfa_node.h"
#include "xfa/fxfa/parser/cxfa_nodehelper.h"
#include "xfa/fxfa/parser/cxfa_object.h"
#include "xfa/fxfa/parser/cxfa_scriptcontext.h"
#include "xfa/fxfa/parser/xfa_resolvenode_rs.h"
#include "xfa/fxfa/parser/xfa_utils.h"

CXFA_ResolveProcessor::CXFA_ResolveProcessor()
    : m_iCurStart(0), m_pNodeHelper(new CXFA_NodeHelper) {}

CXFA_ResolveProcessor::~CXFA_ResolveProcessor() {}

int32_t CXFA_ResolveProcessor::Resolve(CXFA_ResolveNodesData& rnd) {
  if (!rnd.m_CurObject)
    return -1;

  if (!rnd.m_CurObject->IsNode()) {
    if (rnd.m_dwStyles & XFA_RESOLVENODE_Attributes) {
      return ResolveForAttributeRs(rnd.m_CurObject, rnd,
                                   rnd.m_wsName.AsStringC());
    }
    return 0;
  }
  if (rnd.m_dwStyles & XFA_RESOLVENODE_AnyChild) {
    return ResolveAnyChild(rnd);
  }
  wchar_t wch = rnd.m_wsName.GetAt(0);
  switch (wch) {
    case '$':
      return ResolveDollar(rnd);
    case '!':
      return ResolveExcalmatory(rnd);
    case '#':
      return ResolveNumberSign(rnd);
    case '*':
      return ResolveAsterisk(rnd);
    // TODO(dsinclair): We could probably remove this.
    case '.':
      return ResolveAnyChild(rnd);
    default:
      break;
  }
  if (rnd.m_uHashName == XFA_HASHCODE_This && rnd.m_nLevel == 0) {
    rnd.m_Objects.push_back(rnd.m_pSC->GetThisObject());
    return 1;
  }
  if (rnd.m_CurObject->GetElementType() == XFA_Element::Xfa) {
    CXFA_Object* pObjNode =
        rnd.m_pSC->GetDocument()->GetXFAObject(rnd.m_uHashName);
    if (pObjNode) {
      rnd.m_Objects.push_back(pObjNode);
    } else if (rnd.m_uHashName == XFA_HASHCODE_Xfa) {
      rnd.m_Objects.push_back(rnd.m_CurObject);
    } else if ((rnd.m_dwStyles & XFA_RESOLVENODE_Attributes) &&
               ResolveForAttributeRs(rnd.m_CurObject, rnd,
                                     rnd.m_wsName.AsStringC())) {
      return 1;
    }
    if (!rnd.m_Objects.empty())
      FilterCondition(rnd, rnd.m_wsCondition);

    return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
  }
  if (ResolveNormal(rnd) < 1 && rnd.m_uHashName == XFA_HASHCODE_Xfa)
    rnd.m_Objects.push_back(rnd.m_pSC->GetDocument()->GetRoot());

  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolveAnyChild(CXFA_ResolveNodesData& rnd) {
  CFX_WideString wsName = rnd.m_wsName;
  CFX_WideString wsCondition = rnd.m_wsCondition;
  CXFA_Node* findNode = nullptr;
  bool bClassName = false;
  if (wsName.GetAt(0) == '#') {
    bClassName = true;
    wsName = wsName.Right(wsName.GetLength() - 1);
  }
  findNode = m_pNodeHelper->ResolveNodes_GetOneChild(
      ToNode(rnd.m_CurObject), wsName.c_str(), bClassName);
  if (!findNode) {
    return 0;
  }
  if (wsCondition.IsEmpty()) {
    rnd.m_Objects.push_back(findNode);
    return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
  }
  std::vector<CXFA_Node*> tempNodes;
  for (CXFA_Object* pObject : rnd.m_Objects)
    tempNodes.push_back(pObject->AsNode());
  m_pNodeHelper->CountSiblings(findNode, XFA_LOGIC_Transparent, &tempNodes,
                               bClassName);
  rnd.m_Objects = std::vector<CXFA_Object*>(tempNodes.begin(), tempNodes.end());
  FilterCondition(rnd, wsCondition);
  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolveDollar(CXFA_ResolveNodesData& rnd) {
  CFX_WideString wsName = rnd.m_wsName;
  CFX_WideString wsCondition = rnd.m_wsCondition;
  int32_t iNameLen = wsName.GetLength();
  if (iNameLen == 1) {
    rnd.m_Objects.push_back(rnd.m_CurObject);
    return 1;
  }
  if (rnd.m_nLevel > 0) {
    return -1;
  }
  XFA_HashCode dwNameHash = static_cast<XFA_HashCode>(FX_HashCode_GetW(
      CFX_WideStringC(wsName.c_str() + 1, iNameLen - 1), false));
  if (dwNameHash == XFA_HASHCODE_Xfa) {
    rnd.m_Objects.push_back(rnd.m_pSC->GetDocument()->GetRoot());
  } else {
    CXFA_Object* pObjNode = rnd.m_pSC->GetDocument()->GetXFAObject(dwNameHash);
    if (pObjNode)
      rnd.m_Objects.push_back(pObjNode);
  }
  if (!rnd.m_Objects.empty())
    FilterCondition(rnd, wsCondition);

  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolveExcalmatory(CXFA_ResolveNodesData& rnd) {
  if (rnd.m_nLevel > 0)
    return 0;

  CXFA_Node* datasets =
      ToNode(rnd.m_pSC->GetDocument()->GetXFAObject(XFA_HASHCODE_Datasets));
  if (!datasets)
    return 0;

  CXFA_ResolveNodesData rndFind;
  rndFind.m_pSC = rnd.m_pSC;
  rndFind.m_CurObject = datasets;
  rndFind.m_wsName = rnd.m_wsName.Right(rnd.m_wsName.GetLength() - 1);
  rndFind.m_uHashName = static_cast<XFA_HashCode>(
      FX_HashCode_GetW(rndFind.m_wsName.AsStringC(), false));
  rndFind.m_nLevel = rnd.m_nLevel + 1;
  rndFind.m_dwStyles = XFA_RESOLVENODE_Children;
  rndFind.m_wsCondition = rnd.m_wsCondition;
  Resolve(rndFind);
  rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                       rndFind.m_Objects.end());
  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolveNumberSign(CXFA_ResolveNodesData& rnd) {
  CFX_WideString wsName = rnd.m_wsName.Right(rnd.m_wsName.GetLength() - 1);
  CFX_WideString wsCondition = rnd.m_wsCondition;
  CXFA_Node* curNode = ToNode(rnd.m_CurObject);
  if (ResolveForAttributeRs(curNode, rnd, wsName.AsStringC()))
    return 1;

  CXFA_ResolveNodesData rndFind;
  rndFind.m_pSC = rnd.m_pSC;
  rndFind.m_nLevel = rnd.m_nLevel + 1;
  rndFind.m_dwStyles = rnd.m_dwStyles;
  rndFind.m_dwStyles |= XFA_RESOLVENODE_TagName;
  rndFind.m_dwStyles &= ~XFA_RESOLVENODE_Attributes;
  rndFind.m_wsName = wsName;
  rndFind.m_uHashName = static_cast<XFA_HashCode>(
      FX_HashCode_GetW(rndFind.m_wsName.AsStringC(), false));
  rndFind.m_wsCondition = wsCondition;
  rndFind.m_CurObject = curNode;
  ResolveNormal(rndFind);
  if (rndFind.m_Objects.empty())
    return 0;

  if (wsCondition.GetLength() == 0 &&
      pdfium::ContainsValue(rndFind.m_Objects, curNode)) {
    rnd.m_Objects.push_back(curNode);
  } else {
    rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                         rndFind.m_Objects.end());
  }
  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolveForAttributeRs(
    CXFA_Object* curNode,
    CXFA_ResolveNodesData& rnd,
    const CFX_WideStringC& strAttr) {
  const XFA_SCRIPTATTRIBUTEINFO* lpScriptAttribute =
      XFA_GetScriptAttributeByName(curNode->GetElementType(), strAttr);
  if (!lpScriptAttribute)
    return 0;

  rnd.m_pScriptAttribute = lpScriptAttribute;
  rnd.m_Objects.push_back(curNode);
  rnd.m_dwFlag = XFA_RESOVENODE_RSTYPE_Attribute;
  return 1;
}

int32_t CXFA_ResolveProcessor::ResolveNormal(CXFA_ResolveNodesData& rnd) {
  if (rnd.m_nLevel > 32 || !rnd.m_CurObject->IsNode())
    return 0;

  CXFA_Node* curNode = rnd.m_CurObject->AsNode();
  size_t nNum = rnd.m_Objects.size();
  uint32_t dwStyles = rnd.m_dwStyles;
  CFX_WideString& wsName = rnd.m_wsName;
  XFA_HashCode uNameHash = rnd.m_uHashName;
  CFX_WideString& wsCondition = rnd.m_wsCondition;
  CXFA_ResolveNodesData rndFind;
  rndFind.m_wsName = rnd.m_wsName;
  rndFind.m_wsCondition = rnd.m_wsCondition;
  rndFind.m_pSC = rnd.m_pSC;
  rndFind.m_nLevel = rnd.m_nLevel + 1;
  rndFind.m_uHashName = uNameHash;
  std::vector<CXFA_Node*> children;
  std::vector<CXFA_Node*> properties;
  CXFA_Node* pVariablesNode = nullptr;
  CXFA_Node* pPageSetNode = nullptr;
  for (CXFA_Node* pChild = curNode->GetNodeItem(XFA_NODEITEM_FirstChild);
       pChild; pChild = pChild->GetNodeItem(XFA_NODEITEM_NextSibling)) {
    if (pChild->GetElementType() == XFA_Element::Variables) {
      pVariablesNode = pChild;
      continue;
    }
    if (pChild->GetElementType() == XFA_Element::PageSet) {
      pPageSetNode = pChild;
      continue;
    }
    const XFA_PROPERTY* pProperty = XFA_GetPropertyOfElement(
        curNode->GetElementType(), pChild->GetElementType(),
        XFA_XDPPACKET_UNKNOWN);
    if (pProperty)
      properties.push_back(pChild);
    else
      children.push_back(pChild);
  }
  if ((dwStyles & XFA_RESOLVENODE_Properties) && pVariablesNode) {
    uint32_t uPropHash = pVariablesNode->GetClassHashCode();
    if (uPropHash == uNameHash) {
      rnd.m_Objects.push_back(pVariablesNode);
    } else {
      rndFind.m_CurObject = pVariablesNode;
      SetStylesForChild(dwStyles, rndFind);
      CFX_WideString wsSaveCondition = rndFind.m_wsCondition;
      rndFind.m_wsCondition.clear();
      ResolveNormal(rndFind);
      rndFind.m_wsCondition = wsSaveCondition;
      rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                           rndFind.m_Objects.end());
      rndFind.m_Objects.clear();
    }
    if (rnd.m_Objects.size() > nNum) {
      FilterCondition(rnd, wsCondition);
      return !rnd.m_Objects.empty() ? 1 : 0;
    }
  }
  if (dwStyles & XFA_RESOLVENODE_Children) {
    bool bSetFlag = false;
    if (pPageSetNode && (dwStyles & XFA_RESOLVENODE_Properties))
      children.push_back(pPageSetNode);

    for (CXFA_Node* child : children) {
      if (dwStyles & XFA_RESOLVENODE_TagName) {
        if (child->GetClassHashCode() == uNameHash)
          rnd.m_Objects.push_back(child);
      } else if (child->GetNameHash() == uNameHash) {
        rnd.m_Objects.push_back(child);
      }
      if (m_pNodeHelper->NodeIsTransparent(child) &&
          child->GetElementType() != XFA_Element::PageSet) {
        if (!bSetFlag) {
          SetStylesForChild(dwStyles, rndFind);
          bSetFlag = true;
        }
        rndFind.m_CurObject = child;
        CFX_WideString wsSaveCondition = rndFind.m_wsCondition;
        rndFind.m_wsCondition.clear();
        ResolveNormal(rndFind);
        rndFind.m_wsCondition = wsSaveCondition;
        rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                             rndFind.m_Objects.end());
        rndFind.m_Objects.clear();
      }
    }
    if (rnd.m_Objects.size() > nNum) {
      if (!(dwStyles & XFA_RESOLVENODE_ALL)) {
        std::vector<CXFA_Node*> upArrayNodes;
        if (m_pNodeHelper->NodeIsTransparent(ToNode(curNode))) {
          m_pNodeHelper->CountSiblings(ToNode(rnd.m_Objects[0]),
                                       XFA_LOGIC_Transparent, &upArrayNodes,
                                       !!(dwStyles & XFA_RESOLVENODE_TagName));
        }
        if (upArrayNodes.size() > rnd.m_Objects.size()) {
          CXFA_Object* pSaveObject = rnd.m_Objects.front();
          rnd.m_Objects = std::vector<CXFA_Object*>(upArrayNodes.begin(),
                                                    upArrayNodes.end());
          rnd.m_Objects.front() = pSaveObject;
        }
      }
      FilterCondition(rnd, wsCondition);
      return !rnd.m_Objects.empty() ? 1 : 0;
    }
  }
  if (dwStyles & XFA_RESOLVENODE_Attributes) {
    if (ResolveForAttributeRs(curNode, rnd, wsName.AsStringC()))
      return 1;
  }
  if (dwStyles & XFA_RESOLVENODE_Properties) {
    for (CXFA_Node* pChildProperty : properties) {
      if (pChildProperty->IsUnnamed()) {
        if (pChildProperty->GetClassHashCode() == uNameHash)
          rnd.m_Objects.push_back(pChildProperty);
        continue;
      }
      if (pChildProperty->GetNameHash() == uNameHash &&
          pChildProperty->GetElementType() != XFA_Element::Extras &&
          pChildProperty->GetElementType() != XFA_Element::Items) {
        rnd.m_Objects.push_back(pChildProperty);
      }
    }
    if (rnd.m_Objects.size() > nNum) {
      FilterCondition(rnd, wsCondition);
      return !rnd.m_Objects.empty() ? 1 : 0;
    }
    CXFA_Node* pProp = nullptr;
    if (XFA_Element::Subform == curNode->GetElementType() &&
        XFA_HASHCODE_Occur == uNameHash) {
      CXFA_Node* pInstanceManager =
          curNode->AsNode()->GetInstanceMgrOfSubform();
      if (pInstanceManager) {
        pProp = pInstanceManager->GetProperty(0, XFA_Element::Occur, true);
      }
    } else {
      XFA_Element eType = XFA_GetElementTypeForName(wsName.AsStringC());
      if (eType != XFA_Element::Unknown) {
        pProp = curNode->AsNode()->GetProperty(0, eType,
                                               eType != XFA_Element::PageSet);
      }
    }
    if (pProp) {
      rnd.m_Objects.push_back(pProp);
      return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
    }
  }
  CXFA_Node* parentNode = m_pNodeHelper->ResolveNodes_GetParent(
      curNode->AsNode(), XFA_LOGIC_NoTransparent);
  uint32_t uCurClassHash = curNode->GetClassHashCode();
  if (!parentNode) {
    if (uCurClassHash == uNameHash) {
      rnd.m_Objects.push_back(curNode->AsNode());
      FilterCondition(rnd, wsCondition);
      if (!rnd.m_Objects.empty())
        return 1;
    }
    return 0;
  }
  if (dwStyles & XFA_RESOLVENODE_Siblings) {
    CXFA_Node* child = parentNode->GetNodeItem(XFA_NODEITEM_FirstChild);
    uint32_t dwSubStyles =
        XFA_RESOLVENODE_Children | XFA_RESOLVENODE_Properties;
    if (dwStyles & XFA_RESOLVENODE_TagName)
      dwSubStyles |= XFA_RESOLVENODE_TagName;
    if (dwStyles & XFA_RESOLVENODE_ALL)
      dwSubStyles |= XFA_RESOLVENODE_ALL;
    rndFind.m_dwStyles = dwSubStyles;
    while (child) {
      if (child == curNode) {
        if (dwStyles & XFA_RESOLVENODE_TagName) {
          if (uCurClassHash == uNameHash) {
            rnd.m_Objects.push_back(curNode);
          }
        } else {
          if (child->GetNameHash() == uNameHash) {
            rnd.m_Objects.push_back(curNode);
            if (rnd.m_nLevel == 0 && wsCondition.GetLength() == 0) {
              rnd.m_Objects.clear();
              rnd.m_Objects.push_back(curNode);
              return 1;
            }
          }
        }
        child = child->GetNodeItem(XFA_NODEITEM_NextSibling);
        continue;
      }
      if (dwStyles & XFA_RESOLVENODE_TagName) {
        if (child->GetClassHashCode() == uNameHash)
          rnd.m_Objects.push_back(child);
      } else if (child->GetNameHash() == uNameHash) {
        rnd.m_Objects.push_back(child);
      }
      const XFA_PROPERTY* pPropert = XFA_GetPropertyOfElement(
          parentNode->GetElementType(), child->GetElementType(),
          XFA_XDPPACKET_UNKNOWN);
      bool bInnerSearch = false;
      if (pPropert) {
        if ((child->GetElementType() == XFA_Element::Variables ||
             child->GetElementType() == XFA_Element::PageSet)) {
          bInnerSearch = true;
        }
      } else {
        if (m_pNodeHelper->NodeIsTransparent(child)) {
          bInnerSearch = true;
        }
      }
      if (bInnerSearch) {
        rndFind.m_CurObject = child;
        CFX_WideString wsOriginCondition = rndFind.m_wsCondition;
        rndFind.m_wsCondition.clear();
        uint32_t dwOriginStyle = rndFind.m_dwStyles;
        rndFind.m_dwStyles = dwOriginStyle | XFA_RESOLVENODE_ALL;
        ResolveNormal(rndFind);
        rndFind.m_dwStyles = dwOriginStyle;
        rndFind.m_wsCondition = wsOriginCondition;
        rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                             rndFind.m_Objects.end());
        rndFind.m_Objects.clear();
      }
      child = child->GetNodeItem(XFA_NODEITEM_NextSibling);
    }
    if (rnd.m_Objects.size() > nNum) {
      if (m_pNodeHelper->NodeIsTransparent(parentNode)) {
        std::vector<CXFA_Node*> upArrayNodes;
        m_pNodeHelper->CountSiblings(ToNode(rnd.m_Objects.front()),
                                     XFA_LOGIC_Transparent, &upArrayNodes,
                                     !!(dwStyles & XFA_RESOLVENODE_TagName));
        if (upArrayNodes.size() > rnd.m_Objects.size()) {
          CXFA_Object* pSaveObject = rnd.m_Objects.front();
          rnd.m_Objects = std::vector<CXFA_Object*>(upArrayNodes.begin(),
                                                    upArrayNodes.end());
          rnd.m_Objects.front() = pSaveObject;
        }
      }
      FilterCondition(rnd, wsCondition);
      return !rnd.m_Objects.empty() ? 1 : 0;
    }
  }
  if (dwStyles & XFA_RESOLVENODE_Parent) {
    uint32_t dwSubStyles = XFA_RESOLVENODE_Siblings | XFA_RESOLVENODE_Parent |
                           XFA_RESOLVENODE_Properties;
    if (dwStyles & XFA_RESOLVENODE_TagName) {
      dwSubStyles |= XFA_RESOLVENODE_TagName;
    }
    if (dwStyles & XFA_RESOLVENODE_ALL) {
      dwSubStyles |= XFA_RESOLVENODE_ALL;
    }
    rndFind.m_dwStyles = dwSubStyles;
    rndFind.m_CurObject = parentNode;
    rnd.m_pSC->GetUpObjectArray()->push_back(parentNode);
    ResolveNormal(rndFind);
    rnd.m_Objects.insert(rnd.m_Objects.end(), rndFind.m_Objects.begin(),
                         rndFind.m_Objects.end());
    rndFind.m_Objects.clear();
    if (rnd.m_Objects.size() > nNum)
      return 1;
  }
  return 0;
}

int32_t CXFA_ResolveProcessor::ResolveAsterisk(CXFA_ResolveNodesData& rnd) {
  CXFA_Node* curNode = ToNode(rnd.m_CurObject);
  std::vector<CXFA_Node*> array =
      curNode->GetNodeList(XFA_NODEFILTER_Children | XFA_NODEFILTER_Properties);
  rnd.m_Objects.insert(rnd.m_Objects.end(), array.begin(), array.end());
  return pdfium::CollectionSize<int32_t>(rnd.m_Objects);
}

int32_t CXFA_ResolveProcessor::ResolvePopStack(std::vector<int32_t>* stack) {
  if (stack->empty())
    return -1;

  int32_t nType = stack->back();
  stack->pop_back();
  return nType;
}

int32_t CXFA_ResolveProcessor::GetFilter(const CFX_WideStringC& wsExpression,
                                         int32_t nStart,
                                         CXFA_ResolveNodesData& rnd) {
  ASSERT(nStart > -1);
  int32_t iLength = wsExpression.GetLength();
  if (nStart >= iLength) {
    return 0;
  }
  CFX_WideString& wsName = rnd.m_wsName;
  CFX_WideString& wsCondition = rnd.m_wsCondition;
  wchar_t* pNameBuf = wsName.GetBuffer(iLength - nStart);
  wchar_t* pConditionBuf = wsCondition.GetBuffer(iLength - nStart);
  int32_t nNameCount = 0;
  int32_t nConditionCount = 0;
  std::vector<int32_t> stack;
  int32_t nType = -1;
  const wchar_t* pSrc = wsExpression.c_str();
  wchar_t wPrev = 0, wCur;
  bool bIsCondition = false;
  while (nStart < iLength) {
    wCur = pSrc[nStart++];
    if (wCur == '.') {
      if (wPrev == '\\') {
        pNameBuf[nNameCount - 1] = wPrev = '.';
        continue;
      }
      if (nNameCount == 0) {
        rnd.m_dwStyles |= XFA_RESOLVENODE_AnyChild;
        continue;
      }
      wchar_t wLookahead = nStart < iLength ? pSrc[nStart] : 0;
      if (wLookahead != '[' && wLookahead != '(') {
        if (nType < 0) {
          break;
        }
      }
    }
    if (wCur == '[' || wCur == '(') {
      bIsCondition = true;
    } else if (wCur == '.' && nStart < iLength &&
               (pSrc[nStart] == '[' || pSrc[nStart] == '(')) {
      bIsCondition = true;
    }
    if (bIsCondition) {
      pConditionBuf[nConditionCount++] = wCur;
    } else {
      pNameBuf[nNameCount++] = wCur;
    }
    bool bRecursive = true;
    switch (nType) {
      case 0:
        if (wCur == ']') {
          nType = ResolvePopStack(&stack);
          bRecursive = false;
        }
        break;
      case 1:
        if (wCur == ')') {
          nType = ResolvePopStack(&stack);
          bRecursive = false;
        }
        break;
      case 2:
        if (wCur == '"') {
          nType = ResolvePopStack(&stack);
          bRecursive = false;
        }
        break;
    }
    if (bRecursive) {
      switch (wCur) {
        case '[':
          stack.push_back(nType);
          nType = 0;
          break;
        case '(':
          stack.push_back(nType);
          nType = 1;
          break;
        case '"':
          stack.push_back(nType);
          nType = 2;
          break;
      }
    }
    wPrev = wCur;
  }
  if (!stack.empty())
    return -1;

  wsName.ReleaseBuffer(nNameCount);
  wsName.TrimLeft();
  wsName.TrimRight();
  wsCondition.ReleaseBuffer(nConditionCount);
  wsCondition.TrimLeft();
  wsCondition.TrimRight();
  rnd.m_uHashName =
      static_cast<XFA_HashCode>(FX_HashCode_GetW(wsName.AsStringC(), false));
  return nStart;
}
void CXFA_ResolveProcessor::ConditionArray(int32_t iCurIndex,
                                           CFX_WideString wsCondition,
                                           int32_t iFoundCount,
                                           CXFA_ResolveNodesData& rnd) {
  int32_t iLen = wsCondition.GetLength();
  bool bRelative = false;
  bool bAll = false;
  int32_t i = 1;
  for (; i < iLen; ++i) {
    wchar_t ch = wsCondition[i];
    if (ch == ' ') {
      continue;
    }
    if (ch == '+' || ch == '-') {
      bRelative = true;
      break;
    } else if (ch == '*') {
      bAll = true;
      break;
    } else {
      break;
    }
  }
  if (bAll) {
    if (rnd.m_dwStyles & XFA_RESOLVENODE_CreateNode) {
      if (rnd.m_dwStyles & XFA_RESOLVENODE_Bind) {
        m_pNodeHelper->m_pCreateParent = ToNode(rnd.m_CurObject);
        m_pNodeHelper->m_iCreateCount = 1;
        rnd.m_Objects.clear();
        m_pNodeHelper->m_iCurAllStart = -1;
        m_pNodeHelper->m_pAllStartParent = nullptr;
      } else {
        if (m_pNodeHelper->m_iCurAllStart == -1) {
          m_pNodeHelper->m_iCurAllStart = m_iCurStart;
          m_pNodeHelper->m_pAllStartParent = ToNode(rnd.m_CurObject);
        }
      }
    } else if (rnd.m_dwStyles & XFA_RESOLVENODE_BindNew) {
      if (m_pNodeHelper->m_iCurAllStart == -1) {
        m_pNodeHelper->m_iCurAllStart = m_iCurStart;
      }
    }
    return;
  }
  if (iFoundCount == 1 && !iLen) {
    return;
  }
  CFX_WideString wsIndex;
  wsIndex = wsCondition.Mid(i, iLen - 1 - i);
  int32_t iIndex = wsIndex.GetInteger();
  if (bRelative) {
    iIndex += iCurIndex;
  }
  if (iFoundCount <= iIndex || iIndex < 0) {
    if (rnd.m_dwStyles & XFA_RESOLVENODE_CreateNode) {
      m_pNodeHelper->m_pCreateParent = ToNode(rnd.m_CurObject);
      m_pNodeHelper->m_iCreateCount = iIndex - iFoundCount + 1;
    }
    rnd.m_Objects.clear();
  } else {
    CXFA_Object* ret = rnd.m_Objects[iIndex];
    rnd.m_Objects.clear();
    rnd.m_Objects.push_back(ret);
  }
}

void CXFA_ResolveProcessor::DoPredicateFilter(int32_t iCurIndex,
                                              CFX_WideString wsCondition,
                                              int32_t iFoundCount,
                                              CXFA_ResolveNodesData& rnd) {
  ASSERT(iFoundCount == pdfium::CollectionSize<int32_t>(rnd.m_Objects));
  CFX_WideString wsExpression;
  XFA_SCRIPTLANGTYPE eLangType = XFA_SCRIPTLANGTYPE_Unkown;
  if (wsCondition.Left(2) == L".[" && wsCondition.Right(1) == L"]") {
    eLangType = XFA_SCRIPTLANGTYPE_Formcalc;
  } else if (wsCondition.Left(2) == L".(" && wsCondition.Right(1) == L")") {
    eLangType = XFA_SCRIPTLANGTYPE_Javascript;
  } else {
    return;
  }

  CXFA_ScriptContext* pContext = rnd.m_pSC;
  wsExpression = wsCondition.Mid(2, wsCondition.GetLength() - 3);
  for (int32_t i = iFoundCount - 1; i >= 0; i--) {
    auto pRetValue = pdfium::MakeUnique<CFXJSE_Value>(rnd.m_pSC->GetRuntime());
    bool bRet = pContext->RunScript(eLangType, wsExpression.AsStringC(),
                                    pRetValue.get(), rnd.m_Objects[i]);
    if (!bRet || !pRetValue->ToBoolean())
      rnd.m_Objects.erase(rnd.m_Objects.begin() + i);
  }
}

void CXFA_ResolveProcessor::FilterCondition(CXFA_ResolveNodesData& rnd,
                                            CFX_WideString wsCondition) {
  int32_t iCurrIndex = 0;
  const std::vector<CXFA_Node*>* pArray = rnd.m_pSC->GetUpObjectArray();
  if (!pArray->empty()) {
    CXFA_Node* curNode = pArray->back();
    bool bIsProperty = m_pNodeHelper->NodeIsProperty(curNode);
    if (curNode->IsUnnamed() ||
        (bIsProperty && curNode->GetElementType() != XFA_Element::PageSet)) {
      iCurrIndex = m_pNodeHelper->GetIndex(curNode, XFA_LOGIC_Transparent,
                                           bIsProperty, true);
    } else {
      iCurrIndex = m_pNodeHelper->GetIndex(curNode, XFA_LOGIC_Transparent,
                                           bIsProperty, false);
    }
  }
  int32_t iFoundCount = pdfium::CollectionSize<int32_t>(rnd.m_Objects);
  wsCondition.TrimLeft();
  wsCondition.TrimRight();
  int32_t iLen = wsCondition.GetLength();
  if (!iLen) {
    if (rnd.m_dwStyles & XFA_RESOLVENODE_ALL) {
      return;
    }
    if (iFoundCount == 1) {
      return;
    }
    if (iFoundCount <= iCurrIndex) {
      if (rnd.m_dwStyles & XFA_RESOLVENODE_CreateNode) {
        m_pNodeHelper->m_pCreateParent = ToNode(rnd.m_CurObject);
        m_pNodeHelper->m_iCreateCount = iCurrIndex - iFoundCount + 1;
      }
      rnd.m_Objects.clear();
      return;
    } else {
      CXFA_Object* ret = rnd.m_Objects[iCurrIndex];
      rnd.m_Objects.clear();
      rnd.m_Objects.push_back(ret);
      return;
    }
  }
  wchar_t wTypeChar = wsCondition[0];
  switch (wTypeChar) {
    case '[':
      ConditionArray(iCurrIndex, wsCondition, iFoundCount, rnd);
      return;
    case '(':
      return;
    case '"':
      return;
    case '.':
      if (iLen > 1 && (wsCondition[1] == '[' || wsCondition[1] == '(')) {
        DoPredicateFilter(iCurrIndex, wsCondition, iFoundCount, rnd);
      }
    default:
      return;
  }
}
void CXFA_ResolveProcessor::SetStylesForChild(uint32_t dwParentStyles,
                                              CXFA_ResolveNodesData& rnd) {
  uint32_t dwSubStyles = XFA_RESOLVENODE_Children;
  if (dwParentStyles & XFA_RESOLVENODE_TagName) {
    dwSubStyles |= XFA_RESOLVENODE_TagName;
  }
  dwSubStyles &= ~XFA_RESOLVENODE_Parent;
  dwSubStyles &= ~XFA_RESOLVENODE_Siblings;
  dwSubStyles &= ~XFA_RESOLVENODE_Properties;
  dwSubStyles |= XFA_RESOLVENODE_ALL;
  rnd.m_dwStyles = dwSubStyles;
}

int32_t CXFA_ResolveProcessor::SetResultCreateNode(
    XFA_RESOLVENODE_RS& resolveNodeRS,
    CFX_WideString& wsLastCondition) {
  if (m_pNodeHelper->m_pCreateParent)
    resolveNodeRS.objects.push_back(m_pNodeHelper->m_pCreateParent);
  else
    m_pNodeHelper->CreateNode_ForCondition(wsLastCondition);

  resolveNodeRS.dwFlags = m_pNodeHelper->m_iCreateFlag;
  if (resolveNodeRS.dwFlags == XFA_RESOLVENODE_RSTYPE_CreateNodeOne) {
    if (m_pNodeHelper->m_iCurAllStart != -1)
      resolveNodeRS.dwFlags = XFA_RESOLVENODE_RSTYPE_CreateNodeMidAll;
  }
  return pdfium::CollectionSize<int32_t>(resolveNodeRS.objects);
}

void CXFA_ResolveProcessor::SetIndexDataBind(CFX_WideString& wsNextCondition,
                                             int32_t& iIndex,
                                             int32_t iCount) {
  if (m_pNodeHelper->CreateNode_ForCondition(wsNextCondition)) {
    if (m_pNodeHelper->m_eLastCreateType == XFA_Element::DataGroup) {
      iIndex = 0;
    } else {
      iIndex = iCount - 1;
    }
  } else {
    iIndex = iCount - 1;
  }
}

CXFA_ResolveNodesData::CXFA_ResolveNodesData(CXFA_ScriptContext* pSC)
    : m_pSC(pSC),
      m_CurObject(nullptr),
      m_wsName(),
      m_uHashName(XFA_HASHCODE_None),
      m_wsCondition(),
      m_nLevel(0),
      m_Objects(),
      m_dwStyles(XFA_RESOLVENODE_Children),
      m_pScriptAttribute(nullptr),
      m_dwFlag(XFA_RESOVENODE_RSTYPE_Nodes) {}

CXFA_ResolveNodesData::~CXFA_ResolveNodesData() {}
