// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef RemoteFrameOwner_h
#define RemoteFrameOwner_h

#include "core/frame/FrameOwner.h"
#include "platform/scroll/ScrollTypes.h"
#include "public/web/WebFrameOwnerProperties.h"

namespace blink {

// Helper class to bridge communication for a frame with a remote parent.
// Currently, it serves two purposes:
// 1. Allows the local frame's loader to retrieve sandbox flags associated with
//    its owner element in another process.
// 2. Trigger a load event on its owner element once it finishes a load.
class RemoteFrameOwner final
    : public GarbageCollectedFinalized<RemoteFrameOwner>,
      public FrameOwner {
  USING_GARBAGE_COLLECTED_MIXIN(RemoteFrameOwner);

 public:
  static RemoteFrameOwner* Create(
      SandboxFlags flags,
      const WebFrameOwnerProperties& frame_owner_properties) {
    return new RemoteFrameOwner(flags, frame_owner_properties);
  }

  // FrameOwner overrides:
  Frame* ContentFrame() const override { return frame_.Get(); }
  void SetContentFrame(Frame&) override;
  void ClearContentFrame() override;
  SandboxFlags GetSandboxFlags() const override { return sandbox_flags_; }
  void SetSandboxFlags(SandboxFlags flags) { sandbox_flags_ = flags; }
  void DispatchLoad() override;
  // TODO(dcheng): Implement.
  bool CanRenderFallbackContent() const override { return false; }
  void RenderFallbackContent() override {}

  AtomicString BrowsingContextContainerName() const override {
    return browsing_context_container_name_;
  }
  ScrollbarMode ScrollingMode() const override { return scrolling_; }
  int MarginWidth() const override { return margin_width_; }
  int MarginHeight() const override { return margin_height_; }
  bool AllowFullscreen() const override { return allow_fullscreen_; }
  bool AllowPaymentRequest() const override { return allow_payment_request_; }
  bool IsDisplayNone() const override { return is_display_none_; }
  AtomicString Csp() const override { return csp_; }
  const WebVector<WebFeaturePolicyFeature>& AllowedFeatures() const override {
    return allowed_features_;
  }

  void SetBrowsingContextContainerName(const WebString& name) {
    browsing_context_container_name_ = name;
  }
  void SetScrollingMode(WebFrameOwnerProperties::ScrollingMode);
  void SetMarginWidth(int margin_width) { margin_width_ = margin_width; }
  void SetMarginHeight(int margin_height) { margin_height_ = margin_height; }
  void SetAllowFullscreen(bool allow_fullscreen) {
    allow_fullscreen_ = allow_fullscreen;
  }
  void SetAllowPaymentRequest(bool allow_payment_request) {
    allow_payment_request_ = allow_payment_request;
  }
  void SetIsDisplayNone(bool is_display_none) {
    is_display_none_ = is_display_none;
  }
  void SetCsp(const WebString& csp) { csp_ = csp; }
  void SetAllowedFeatures(
      const WebVector<WebFeaturePolicyFeature>& allowed_features) {
    allowed_features_ = allowed_features;
  }

  DECLARE_VIRTUAL_TRACE();

 private:
  RemoteFrameOwner(SandboxFlags, const WebFrameOwnerProperties&);

  // Intentionally private to prevent redundant checks when the type is
  // already HTMLFrameOwnerElement.
  bool IsLocal() const override { return false; }
  bool IsRemote() const override { return true; }

  Member<Frame> frame_;
  SandboxFlags sandbox_flags_;
  AtomicString browsing_context_container_name_;
  ScrollbarMode scrolling_;
  int margin_width_;
  int margin_height_;
  bool allow_fullscreen_;
  bool allow_payment_request_;
  bool is_display_none_;
  WebString csp_;
  WebVector<WebFeaturePolicyFeature> allowed_features_;
};

DEFINE_TYPE_CASTS(RemoteFrameOwner,
                  FrameOwner,
                  owner,
                  owner->IsRemote(),
                  owner.IsRemote());

}  // namespace blink

#endif  // RemoteFrameOwner_h
