// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DisplayItemClient_h
#define DisplayItemClient_h

#include "platform/PlatformExport.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/PaintInvalidationReason.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/text/WTFString.h"

#define CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS DCHECK_IS_ON()

namespace blink {

// The class for objects that can be associated with display items. A
// DisplayItemClient object should live at least longer than the document cycle
// in which its display items are created during painting. After the document
// cycle, a pointer/reference to DisplayItemClient should be no longer
// dereferenced unless we can make sure the client is still valid.
class PLATFORM_EXPORT DisplayItemClient {
 public:
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  DisplayItemClient();
  virtual ~DisplayItemClient();

  // Tests if this DisplayItemClient object has been created and has not been
  // deleted yet.
  bool IsAlive() const;

  // Called when any DisplayItem of this DisplayItemClient is added into
  // PaintController using PaintController::createAndAppend() or into a cached
  // subsequence.
  void BeginShouldKeepAlive(const void* owner) const;

  // Called when the DisplayItemClient is sure that it can safely die before its
  // owners have chance to remove it from the aliveness control.
  void EndShouldKeepAlive() const;

  // Clears all should-keep-alive DisplayItemClients of a PaintController.
  // Called after PaintController commits new display items or the subsequence
  // owner is invalidated.
  static void EndShouldKeepAliveAllClients(const void* owner);
  static void EndShouldKeepAliveAllClients();
#else
  DisplayItemClient() {}
  virtual ~DisplayItemClient() {}
#endif

  virtual String DebugName() const = 0;

  // The visual rect of this DisplayItemClient, in the object space of the
  // object that owns the GraphicsLayer, i.e. offset by
  // offsetFromLayoutObjectWithSubpixelAccumulation().
  virtual LayoutRect VisualRect() const = 0;

  // This is declared here instead of in LayoutObject for verifying the
  // condition in DrawingRecorder.
  // Returns true if the object itself will not generate any effective painted
  // output no matter what size the object is. For example, this function can
  // return false for an object whose size is currently 0x0 but would have
  // effective painted output if it was set a non-empty size. It's used to skip
  // unforced paint invalidation of LayoutObjects (which is when
  // shouldDoFullPaintInvalidation is false, but mayNeedPaintInvalidation or
  // childShouldCheckForPaintInvalidation is true) to avoid unnecessary paint
  // invalidations of empty areas covered by such objects.
  virtual bool PaintedOutputOfObjectHasNoEffectRegardlessOfSize() const {
    return false;
  }

  void SetDisplayItemsUncached(
      PaintInvalidationReason reason = kPaintInvalidationFull) const {
    cache_generation_or_invalidation_reason_.Invalidate(reason);
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
    // Clear should-keep-alive of DisplayItemClients in a subsequence if this
    // object is a subsequence.
    EndShouldKeepAliveAllClients(this);
#endif
  }

  PaintInvalidationReason GetPaintInvalidationReason() const {
    return cache_generation_or_invalidation_reason_
        .GetPaintInvalidationReason();
  }

  // A client is considered "just created" if its display items have never been
  // committed.
  bool IsJustCreated() const {
    return cache_generation_or_invalidation_reason_.IsJustCreated();
  }
  void ClearIsJustCreated() const {
    cache_generation_or_invalidation_reason_.ClearIsJustCreated();
  }

 private:
  friend class FakeDisplayItemClient;
  friend class PaintController;

  // Holds a unique cache generation id of DisplayItemClients and
  // PaintControllers, or PaintInvalidationReason if the DisplayItemClient or
  // PaintController is invalidated.
  //
  // A paint controller sets its cache generation to
  // DisplayItemCacheGeneration::next() at the end of each
  // commitNewDisplayItems, and updates the cache generation of each client with
  // cached drawings by calling DisplayItemClient::setDisplayItemsCached(). A
  // display item is treated as validly cached in a paint controller if its
  // cache generation matches the paint controller's cache generation.
  //
  // SPv1 only: If a display item is painted on multiple paint controllers,
  // because cache generations are unique, the client's cache generation matches
  // the last paint controller only. The client will be treated as invalid on
  // other paint controllers regardless if it's validly cached by these paint
  // controllers. This situation is very rare (about 0.07% of clients were
  // painted on multiple paint controllers) so the performance penalty is
  // trivial.
  class PLATFORM_EXPORT CacheGenerationOrInvalidationReason {
    DISALLOW_NEW();

   public:
    CacheGenerationOrInvalidationReason() : value_(kJustCreated) {}

    void Invalidate(PaintInvalidationReason reason = kPaintInvalidationFull) {
      if (value_ != kJustCreated)
        value_ = static_cast<ValueType>(reason);
    }

    static CacheGenerationOrInvalidationReason Next() {
      // In case the value overflowed in the previous call.
      if (next_generation_ < kFirstValidGeneration)
        next_generation_ = kFirstValidGeneration;
      return CacheGenerationOrInvalidationReason(next_generation_++);
    }

    bool Matches(const CacheGenerationOrInvalidationReason& other) const {
      return value_ >= kFirstValidGeneration &&
             other.value_ >= kFirstValidGeneration && value_ == other.value_;
    }

    PaintInvalidationReason GetPaintInvalidationReason() const {
      return value_ < kJustCreated
                 ? static_cast<PaintInvalidationReason>(value_)
                 : kPaintInvalidationNone;
    }

    bool IsJustCreated() const { return value_ == kJustCreated; }
    void ClearIsJustCreated() {
      value_ = static_cast<ValueType>(kPaintInvalidationFull);
    }

   private:
    typedef uint32_t ValueType;
    explicit CacheGenerationOrInvalidationReason(ValueType value)
        : value_(value) {}

    static const ValueType kJustCreated =
        static_cast<ValueType>(kPaintInvalidationReasonMax) + 1;
    static const ValueType kFirstValidGeneration =
        static_cast<ValueType>(kPaintInvalidationReasonMax) + 2;
    static ValueType next_generation_;
    ValueType value_;
  };

  bool DisplayItemsAreCached(CacheGenerationOrInvalidationReason other) const {
    return cache_generation_or_invalidation_reason_.Matches(other);
  }
  void SetDisplayItemsCached(
      CacheGenerationOrInvalidationReason cache_generation) const {
    cache_generation_or_invalidation_reason_ = cache_generation;
  }

  mutable CacheGenerationOrInvalidationReason
      cache_generation_or_invalidation_reason_;

  DISALLOW_COPY_AND_ASSIGN(DisplayItemClient);
};

inline bool operator==(const DisplayItemClient& client1,
                       const DisplayItemClient& client2) {
  return &client1 == &client2;
}
inline bool operator!=(const DisplayItemClient& client1,
                       const DisplayItemClient& client2) {
  return &client1 != &client2;
}

}  // namespace blink

#endif  // DisplayItemClient_h
