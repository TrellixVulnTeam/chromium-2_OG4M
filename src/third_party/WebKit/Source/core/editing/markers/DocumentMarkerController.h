/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef DocumentMarkerController_h
#define DocumentMarkerController_h

#include "core/CoreExport.h"
#include "core/dom/SynchronousMutationObserver.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/DocumentMarker.h"
#include "platform/geometry/IntRect.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/Vector.h"

namespace blink {

class Node;
class RenderedDocumentMarker;
class Text;

class MarkerRemoverPredicate final {
 public:
  explicit MarkerRemoverPredicate(const Vector<String>& words);
  bool operator()(const DocumentMarker&, const Text&) const;

 private:
  Vector<String> words_;
};

class CORE_EXPORT DocumentMarkerController final
    : public GarbageCollected<DocumentMarkerController>,
      public SynchronousMutationObserver {
  WTF_MAKE_NONCOPYABLE(DocumentMarkerController);
  USING_GARBAGE_COLLECTED_MIXIN(DocumentMarkerController);

 public:
  explicit DocumentMarkerController(Document&);

  void Clear();
  void AddMarker(const Position& start,
                 const Position& end,
                 DocumentMarker::MarkerType,
                 const String& description = g_empty_string);
  void AddTextMatchMarker(const EphemeralRange&, DocumentMarker::MatchStatus);
  void AddCompositionMarker(const Position& start,
                            const Position& end,
                            Color underline_color,
                            bool thick,
                            Color background_color);

  void MoveMarkers(Node* src_node, int length, Node* dst_node);

  void PrepareForDestruction();
  void RemoveMarkers(
      const EphemeralRange&,
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  void RemoveMarkers(
      Node*,
      unsigned start_offset,
      int length,
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  void RemoveMarkers(
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  void RemoveMarkers(
      Node*,
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  void RemoveMarkers(const MarkerRemoverPredicate& should_remove_marker);
  void RepaintMarkers(
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  // Returns true if markers within a range are found.
  bool SetMarkersActive(const EphemeralRange&, bool);
  // Returns true if markers within a range defined by a node, |startOffset| and
  // |endOffset| are found.
  bool SetMarkersActive(Node*,
                        unsigned start_offset,
                        unsigned end_offset,
                        bool);
  bool HasMarkers(Node* node) const { return markers_.Contains(node); }

  DocumentMarkerVector MarkersFor(
      Node*,
      DocumentMarker::MarkerTypes = DocumentMarker::AllMarkers());
  DocumentMarkerVector MarkersInRange(const EphemeralRange&,
                                      DocumentMarker::MarkerTypes);
  DocumentMarkerVector Markers();
  Vector<IntRect> RenderedRectsForMarkers(DocumentMarker::MarkerType);
  void UpdateMarkerRenderedRectIfNeeded(const Node&, RenderedDocumentMarker&);
  void InvalidateRectsForAllMarkers();
  void InvalidateRectsForMarkersInNode(const Node&);

  DECLARE_TRACE();

#ifndef NDEBUG
  void ShowMarkers() const;
#endif

  // SynchronousMutationObserver
  void DidUpdateCharacterData(CharacterData*,
                              unsigned offset,
                              unsigned old_length,
                              unsigned new_length) final;

 private:
  void AddMarker(Node*, const DocumentMarker&);

  using MarkerList = HeapVector<Member<RenderedDocumentMarker>>;
  using MarkerLists =
      HeapVector<Member<MarkerList>, DocumentMarker::kMarkerTypeIndexesCount>;
  using MarkerMap = HeapHashMap<WeakMember<const Node>, Member<MarkerLists>>;
  void MergeOverlapping(MarkerList*, RenderedDocumentMarker*);
  bool PossiblyHasMarkers(DocumentMarker::MarkerTypes);
  void RemoveMarkersFromList(MarkerMap::iterator, DocumentMarker::MarkerTypes);
  void RemoveMarkers(TextIterator&, DocumentMarker::MarkerTypes);

  MarkerMap markers_;
  // Provide a quick way to determine whether a particular marker type is absent
  // without going through the map.
  DocumentMarker::MarkerTypes possibly_existing_marker_types_;
  const Member<const Document> document_;
};

}  // namespace blink

#ifndef NDEBUG
void showDocumentMarkers(const blink::DocumentMarkerController*);
#endif

#endif  // DocumentMarkerController_h
