// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef StorageClientImpl_h
#define StorageClientImpl_h

#include "modules/storage/StorageClient.h"
#include <memory>

namespace blink {

class WebViewImpl;

class StorageClientImpl : public StorageClient {
 public:
  explicit StorageClientImpl(WebViewImpl*);

  std::unique_ptr<StorageNamespace> CreateSessionStorageNamespace() override;
  bool CanAccessStorage(LocalFrame*, StorageType) const override;

 private:
  WebViewImpl* web_view_;
};

}  // namespace blink

#endif  // StorageClientImpl_h
