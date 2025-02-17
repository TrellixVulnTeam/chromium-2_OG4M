// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/WindowProxyManager.h"

#include "bindings/core/v8/DOMWrapperWorld.h"

namespace blink {

DEFINE_TRACE(WindowProxyManager) {
  visitor->Trace(frame_);
  visitor->Trace(window_proxy_);
  visitor->Trace(isolated_worlds_);
}

void WindowProxyManager::ClearForClose() {
  window_proxy_->ClearForClose();
  for (auto& entry : isolated_worlds_)
    entry.value->ClearForClose();
}

void WindowProxyManager::ClearForNavigation() {
  window_proxy_->ClearForNavigation();
  for (auto& entry : isolated_worlds_)
    entry.value->ClearForNavigation();
}

void WindowProxyManager::ReleaseGlobalProxies(
    GlobalProxyVector& global_proxies) {
  DCHECK(global_proxies.IsEmpty());
  global_proxies.ReserveInitialCapacity(1 + isolated_worlds_.size());
  global_proxies.emplace_back(&window_proxy_->World(),
                              window_proxy_->ReleaseGlobalProxy());
  for (auto& entry : isolated_worlds_) {
    global_proxies.emplace_back(
        &entry.value->World(),
        WindowProxyMaybeUninitialized(entry.value->World())
            ->ReleaseGlobalProxy());
  }
}

void WindowProxyManager::SetGlobalProxies(
    const GlobalProxyVector& global_proxies) {
  for (const auto& entry : global_proxies)
    WindowProxyMaybeUninitialized(*entry.first)->SetGlobalProxy(entry.second);
}

WindowProxyManager::WindowProxyManager(Frame& frame, FrameType frame_type)
    : isolate_(v8::Isolate::GetCurrent()),
      frame_(&frame),
      frame_type_(frame_type),
      window_proxy_(CreateWindowProxy(DOMWrapperWorld::MainWorld())) {}

WindowProxy* WindowProxyManager::CreateWindowProxy(DOMWrapperWorld& world) {
  switch (frame_type_) {
    case FrameType::kLocal:
      // Directly use static_cast instead of toLocalFrame because
      // WindowProxyManager gets instantiated during a construction of
      // LocalFrame and at that time virtual member functions are not yet
      // available (we cannot use LocalFrame::isLocalFrame).  Ditto for
      // RemoteFrame.
      return LocalWindowProxy::Create(
          isolate_, *static_cast<LocalFrame*>(frame_.Get()), &world);
    case FrameType::kRemote:
      return RemoteWindowProxy::Create(
          isolate_, *static_cast<RemoteFrame*>(frame_.Get()), &world);
  }
  NOTREACHED();
  return nullptr;
}

WindowProxy* WindowProxyManager::WindowProxyMaybeUninitialized(
    DOMWrapperWorld& world) {
  WindowProxy* window_proxy = nullptr;
  if (world.IsMainWorld()) {
    window_proxy = window_proxy_.Get();
  } else {
    IsolatedWorldMap::iterator iter = isolated_worlds_.Find(world.GetWorldId());
    if (iter != isolated_worlds_.end()) {
      window_proxy = iter->value.Get();
    } else {
      window_proxy = CreateWindowProxy(world);
      isolated_worlds_.Set(world.GetWorldId(), window_proxy);
    }
  }
  return window_proxy;
}

void LocalWindowProxyManager::UpdateSecurityOrigin(
    SecurityOrigin* security_origin) {
  static_cast<LocalWindowProxy*>(window_proxy_.Get())
      ->UpdateSecurityOrigin(security_origin);

  for (auto& entry : isolated_worlds_) {
    auto* isolated_window_proxy =
        static_cast<LocalWindowProxy*>(entry.value.Get());
    SecurityOrigin* isolated_security_origin =
        isolated_window_proxy->World().IsolatedWorldSecurityOrigin();
    isolated_window_proxy->UpdateSecurityOrigin(isolated_security_origin);
  }
}

}  // namespace blink
