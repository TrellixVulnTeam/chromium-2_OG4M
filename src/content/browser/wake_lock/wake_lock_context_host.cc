// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/wake_lock/wake_lock_context_host.h"

#include "base/atomic_sequence_num.h"
#include "base/lazy_instance.h"
#include "content/public/common/service_manager_connection.h"
#include "device/wake_lock/public/interfaces/wake_lock_context_provider.mojom.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/device/public/interfaces/constants.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace content {

namespace {

base::StaticAtomicSequenceNumber g_unique_id;

base::LazyInstance<std::map<int, WakeLockContextHost*>>::Leaky
    g_id_to_context_host = LAZY_INSTANCE_INITIALIZER;

WakeLockContextHost* ContextHostFromId(int id) {
  auto it = g_id_to_context_host.Get().find(id);
  return it != g_id_to_context_host.Get().end() ? it->second : nullptr;
}

}  // namespace

WakeLockContextHost::WakeLockContextHost(WebContents* web_contents)
    : id_(g_unique_id.GetNext()), web_contents_(web_contents) {
  g_id_to_context_host.Get()[id_] = this;

  // Connect to a WakeLockContext, associating it with |id_| (note that in some
  // testing contexts, the service manager connection isn't initialized).
  if (ServiceManagerConnection::GetForProcess()) {
    service_manager::Connector* connector =
        ServiceManagerConnection::GetForProcess()->GetConnector();
    DCHECK(connector);
    device::mojom::WakeLockContextProviderPtr context_provider;
    connector->BindInterface(device::mojom::kServiceName,
                             mojo::MakeRequest(&context_provider));
    context_provider->GetContext(id_, mojo::MakeRequest(&wake_lock_context_));
  }
}

WakeLockContextHost::~WakeLockContextHost() {
  g_id_to_context_host.Get().erase(id_);
}

// static
gfx::NativeView WakeLockContextHost::GetNativeViewForContext(int context_id) {
  WakeLockContextHost* context_host = ContextHostFromId(context_id);
  if (context_host)
    return context_host->web_contents_->GetNativeView();
  return nullptr;
}

}  // namespace content
