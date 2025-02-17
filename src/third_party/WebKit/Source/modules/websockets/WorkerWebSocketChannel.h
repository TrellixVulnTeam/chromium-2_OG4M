/*
 * Copyright (C) 2011 Google Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WorkerWebSocketChannel_h
#define WorkerWebSocketChannel_h

#include <stdint.h>
#include <memory>
#include "bindings/core/v8/SourceLocation.h"
#include "core/workers/WorkerThreadLifecycleObserver.h"
#include "modules/websockets/WebSocketChannel.h"
#include "modules/websockets/WebSocketChannelClient.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebTraceLocation.h"

namespace blink {

class BlobDataHandle;
class KURL;
class ThreadableLoadingContext;
class WebSocketChannelSyncHelper;
class WorkerGlobalScope;
class WorkerLoaderProxy;

class WorkerWebSocketChannel final : public WebSocketChannel {
  WTF_MAKE_NONCOPYABLE(WorkerWebSocketChannel);

 public:
  static WebSocketChannel* Create(WorkerGlobalScope& worker_global_scope,
                                  WebSocketChannelClient* client,
                                  std::unique_ptr<SourceLocation> location) {
    return new WorkerWebSocketChannel(worker_global_scope, client,
                                      std::move(location));
  }
  ~WorkerWebSocketChannel() override;

  // WebSocketChannel functions.
  bool Connect(const KURL&, const String& protocol) override;
  void Send(const CString&) override;
  void Send(const DOMArrayBuffer&,
            unsigned byte_offset,
            unsigned byte_length) override;
  void Send(PassRefPtr<BlobDataHandle>) override;
  void SendTextAsCharVector(std::unique_ptr<Vector<char>>) override {
    NOTREACHED();
  }
  void SendBinaryAsCharVector(std::unique_ptr<Vector<char>>) override {
    NOTREACHED();
  }
  void Close(int code, const String& reason) override;
  void Fail(const String& reason,
            MessageLevel,
            std::unique_ptr<SourceLocation>) override;
  void Disconnect() override;  // Will suppress didClose().

  DECLARE_VIRTUAL_TRACE();

  class Bridge;
  // Allocated and used in the main thread.
  class Peer final : public GarbageCollectedFinalized<Peer>,
                     public WebSocketChannelClient,
                     public WorkerThreadLifecycleObserver {
    USING_GARBAGE_COLLECTED_MIXIN(Peer);
    WTF_MAKE_NONCOPYABLE(Peer);

   public:
    Peer(Bridge*, PassRefPtr<WorkerLoaderProxy>, WorkerThreadLifecycleContext*);
    ~Peer() override;

    // SourceLocation parameter may be shown when the connection fails.
    bool Initialize(std::unique_ptr<SourceLocation>, ThreadableLoadingContext*);

    bool Connect(const KURL&, const String& protocol);
    void SendTextAsCharVector(std::unique_ptr<Vector<char>>);
    void SendBinaryAsCharVector(std::unique_ptr<Vector<char>>);
    void SendBlob(PassRefPtr<BlobDataHandle>);
    void Close(int code, const String& reason);
    void Fail(const String& reason,
              MessageLevel,
              std::unique_ptr<SourceLocation>);
    void Disconnect();

    DECLARE_VIRTUAL_TRACE();
    // Promptly clear connection to bridge + loader proxy.
    EAGERLY_FINALIZE();

    // WebSocketChannelClient functions.
    void DidConnect(const String& subprotocol,
                    const String& extensions) override;
    void DidReceiveTextMessage(const String& payload) override;
    void DidReceiveBinaryMessage(std::unique_ptr<Vector<char>>) override;
    void DidConsumeBufferedAmount(uint64_t) override;
    void DidStartClosingHandshake() override;
    void DidClose(ClosingHandshakeCompletionStatus,
                  unsigned short code,
                  const String& reason) override;
    void DidError() override;

    // WorkerThreadLifecycleObserver function.
    void ContextDestroyed(WorkerThreadLifecycleContext*) override;

   private:
    CrossThreadWeakPersistent<Bridge> bridge_;
    RefPtr<WorkerLoaderProxy> loader_proxy_;
    Member<WebSocketChannel> main_web_socket_channel_;
  };

  // Bridge for Peer. Running on the worker thread.
  class Bridge final : public GarbageCollectedFinalized<Bridge> {
    WTF_MAKE_NONCOPYABLE(Bridge);

   public:
    Bridge(WebSocketChannelClient*, WorkerGlobalScope&);
    ~Bridge();

    // SourceLocation parameter may be shown when the connection fails.
    bool Connect(std::unique_ptr<SourceLocation>,
                 const KURL&,
                 const String& protocol);

    void Send(const CString& message);
    void Send(const DOMArrayBuffer&,
              unsigned byte_offset,
              unsigned byte_length);
    void Send(PassRefPtr<BlobDataHandle>);
    void Close(int code, const String& reason);
    void Fail(const String& reason,
              MessageLevel,
              std::unique_ptr<SourceLocation>);
    void Disconnect();

    void ConnectOnMainThread(std::unique_ptr<SourceLocation>,
                             RefPtr<WorkerLoaderProxy>,
                             WorkerThreadLifecycleContext*,
                             const KURL&,
                             const String& protocol,
                             WebSocketChannelSyncHelper*);

    // Returns null when |disconnect| has already been called.
    WebSocketChannelClient* Client() { return client_; }

    DECLARE_TRACE();
    // Promptly clear connection to peer + loader proxy.
    EAGERLY_FINALIZE();

   private:
    Member<WebSocketChannelClient> client_;
    Member<WorkerGlobalScope> worker_global_scope_;
    RefPtr<WorkerLoaderProxy> loader_proxy_;
    CrossThreadPersistent<Peer> peer_;
  };

 private:
  WorkerWebSocketChannel(WorkerGlobalScope&,
                         WebSocketChannelClient*,
                         std::unique_ptr<SourceLocation>);

  Member<Bridge> bridge_;
  std::unique_ptr<SourceLocation> location_at_connection_;
};

}  // namespace blink

#endif  // WorkerWebSocketChannel_h
