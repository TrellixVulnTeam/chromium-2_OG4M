// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/modules/v8/wasm/WasmResponseExtensions.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/ScriptState.h"
#include "bindings/modules/v8/V8Response.h"
#include "core/dom/ExecutionContext.h"
#include "modules/fetch/BodyStreamBuffer.h"
#include "modules/fetch/FetchDataLoader.h"
#include "platform/heap/Handle.h"
#include "wtf/RefPtr.h"

namespace blink {

namespace {

class FetchDataLoaderAsWasmModule final : public FetchDataLoader,
                                          public BytesConsumer::Client {
  USING_GARBAGE_COLLECTED_MIXIN(FetchDataLoaderAsWasmModule);

 public:
  FetchDataLoaderAsWasmModule(ScriptPromiseResolver* resolver,
                              ScriptState* script_state)
      : resolver_(resolver),
        builder_(script_state->GetIsolate()),
        script_state_(script_state) {}

  void Start(BytesConsumer* consumer,
             FetchDataLoader::Client* client) override {
    DCHECK(!consumer_);
    DCHECK(!client_);
    client_ = client;
    consumer_ = consumer;
    consumer_->SetClient(this);
    OnStateChange();
  }

  void OnStateChange() override {
    while (true) {
      // {buffer} is owned by {m_consumer}.
      const char* buffer = nullptr;
      size_t available = 0;
      BytesConsumer::Result result = consumer_->BeginRead(&buffer, &available);

      if (result == BytesConsumer::Result::kShouldWait)
        return;
      if (result == BytesConsumer::Result::kOk) {
        if (available > 0) {
          DCHECK_NE(buffer, nullptr);
          builder_.OnBytesReceived(reinterpret_cast<const uint8_t*>(buffer),
                                   available);
        }
        result = consumer_->EndRead(available);
      }
      switch (result) {
        case BytesConsumer::Result::kShouldWait:
          NOTREACHED();
          return;
        case BytesConsumer::Result::kOk: {
          break;
        }
        case BytesConsumer::Result::kDone: {
          v8::Isolate* isolate = script_state_->GetIsolate();
          ScriptState::Scope scope(script_state_.Get());

          {
            // The TryCatch destructor will clear the exception. We
            // scope the block here to ensure tight control over the
            // lifetime of the exception.
            v8::TryCatch trycatch(isolate);
            v8::Local<v8::WasmCompiledModule> module;
            if (builder_.Finish().ToLocal(&module)) {
              DCHECK(!trycatch.HasCaught());
              ScriptValue script_value(script_state_.Get(), module);
              resolver_->Resolve(script_value);
            } else {
              DCHECK(trycatch.HasCaught());
              resolver_->Reject(trycatch.Exception());
            }
          }

          client_->DidFetchDataLoadedCustomFormat();
          return;
        }
        case BytesConsumer::Result::kError: {
          // TODO(mtrofin): do we need an abort on the wasm side?
          // Something like "m_outStream->abort()" maybe?
          return RejectPromise();
        }
      }
    }
  }

  void Cancel() override {
    consumer_->Cancel();
    return RejectPromise();
  }

  DEFINE_INLINE_TRACE() {
    visitor->Trace(consumer_);
    visitor->Trace(resolver_);
    visitor->Trace(client_);
    FetchDataLoader::Trace(visitor);
    BytesConsumer::Client::Trace(visitor);
  }

 private:
  // TODO(mtrofin): replace with spec-ed error types, once spec clarifies
  // what they are.
  void RejectPromise() {
    resolver_->Reject(V8ThrowException::CreateTypeError(
        script_state_->GetIsolate(), "Could not download wasm module"));
  }
  Member<BytesConsumer> consumer_;
  Member<ScriptPromiseResolver> resolver_;
  Member<FetchDataLoader::Client> client_;
  v8::WasmModuleObjectBuilder builder_;
  const RefPtr<ScriptState> script_state_;
};

// TODO(mtrofin): WasmDataLoaderClient is necessary so we may provide an
// argument to BodyStreamBuffer::startLoading, however, it fulfills
// a very small role. Consider refactoring to avoid it.
class WasmDataLoaderClient final
    : public GarbageCollectedFinalized<WasmDataLoaderClient>,
      public FetchDataLoader::Client {
  WTF_MAKE_NONCOPYABLE(WasmDataLoaderClient);
  USING_GARBAGE_COLLECTED_MIXIN(WasmDataLoaderClient);

 public:
  explicit WasmDataLoaderClient() {}
  void DidFetchDataLoadedCustomFormat() override {}
  void DidFetchDataLoadFailed() override { NOTREACHED(); }
};

// This callback may be entered as a promise is resolved, or directly
// from the overload callback.
// See
// https://github.com/WebAssembly/design/blob/master/Web.md#webassemblycompile
void CompileFromResponseCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  ExceptionState exception_state(args.GetIsolate(),
                                 ExceptionState::kExecutionContext,
                                 "WebAssembly", "compile");
  ExceptionToRejectPromiseScope reject_promise_scope(args, exception_state);

  ScriptState* script_state = ScriptState::ForReceiverObject(args);
  if (!ExecutionContext::From(script_state)) {
    V8SetReturnValue(args, ScriptPromise().V8Value());
    return;
  }

  if (args.Length() < 1 || !args[0]->IsObject() ||
      !V8Response::hasInstance(args[0], args.GetIsolate())) {
    V8SetReturnValue(
        args, ScriptPromise::Reject(
                  script_state, V8ThrowException::CreateTypeError(
                                    script_state->GetIsolate(),
                                    "Promise argument must be called with a "
                                    "Promise<Response> object"))
                  .V8Value());
    return;
  }

  Response* response = V8Response::toImpl(v8::Local<v8::Object>::Cast(args[0]));
  ScriptPromise promise;
  if (response->IsBodyLocked() || response->bodyUsed()) {
    promise = ScriptPromise::Reject(
        script_state,
        V8ThrowException::CreateTypeError(
            script_state->GetIsolate(),
            "Cannot compile WebAssembly.Module from an already read Response"));
  } else {
    ScriptPromiseResolver* resolver =
        ScriptPromiseResolver::Create(script_state);
    if (response->BodyBuffer()) {
      promise = resolver->Promise();
      response->BodyBuffer()->StartLoading(
          new FetchDataLoaderAsWasmModule(resolver, script_state),
          new WasmDataLoaderClient());
    } else {
      promise = ScriptPromise::Reject(
          script_state,
          V8ThrowException::CreateTypeError(
              script_state->GetIsolate(), "Response object has a null body."));
    }
  }
  V8SetReturnValue(args, promise.V8Value());
}

// See https://crbug.com/708238 for tracking avoiding the hand-generated code.
bool WasmCompileOverload(const v8::FunctionCallbackInfo<v8::Value>& args) {
  if (args.Length() < 1 || !args[0]->IsObject())
    return false;

  if (!args[0]->IsPromise() &&
      !V8Response::hasInstance(args[0], args.GetIsolate()))
    return false;

  v8::Isolate* isolate = args.GetIsolate();
  ScriptState* script_state = ScriptState::ForReceiverObject(args);

  v8::Local<v8::Function> compile_callback =
      v8::Function::New(isolate, CompileFromResponseCallback);

  // treat either case of parameter as
  // Promise.resolve(parameter)
  // as per https://www.w3.org/2001/tag/doc/promises-guide#resolve-arguments

  // Ending with:
  //    return Promise.resolve(parameter).then(compileCallback);
  V8SetReturnValue(args, ScriptPromise::Cast(script_state, args[0])
                             .Then(compile_callback)
                             .V8Value());

  return true;
}

}  // namespace

void WasmResponseExtensions::Initialize(v8::Isolate* isolate) {
  isolate->SetWasmCompileCallback(WasmCompileOverload);
}

}  // namespace blink
