// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Iterable_h
#define Iterable_h

#include "bindings/core/v8/V8IteratorResultValue.h"
#include "bindings/core/v8/V8ScriptRunner.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/Iterator.h"

namespace blink {

// Typically, you should use PairIterable<> (below) instead.
// Also, note that value iterators are set up automatically by the bindings
// code and the operations below come directly from V8.
template <typename KeyType, typename ValueType>
class Iterable {
 public:
  Iterator* keysForBinding(ScriptState* script_state,
                           ExceptionState& exception_state) {
    IterationSource* source =
        this->StartIteration(script_state, exception_state);
    if (!source)
      return nullptr;
    return new IterableIterator<KeySelector>(source);
  }

  Iterator* valuesForBinding(ScriptState* script_state,
                             ExceptionState& exception_state) {
    IterationSource* source =
        this->StartIteration(script_state, exception_state);
    if (!source)
      return nullptr;
    return new IterableIterator<ValueSelector>(source);
  }

  Iterator* entriesForBinding(ScriptState* script_state,
                              ExceptionState& exception_state) {
    IterationSource* source =
        this->StartIteration(script_state, exception_state);
    if (!source)
      return nullptr;
    return new IterableIterator<EntrySelector>(source);
  }

  void forEachForBinding(ScriptState* script_state,
                         const ScriptValue& this_value,
                         const ScriptValue& callback,
                         const ScriptValue& this_arg,
                         ExceptionState& exception_state) {
    IterationSource* source =
        this->StartIteration(script_state, exception_state);

    v8::Isolate* isolate = script_state->GetIsolate();
    v8::TryCatch try_catch(isolate);

    v8::Local<v8::Object> creation_context(
        this_value.V8Value().As<v8::Object>());
    v8::Local<v8::Function> v8_callback(callback.V8Value().As<v8::Function>());
    v8::Local<v8::Value> v8_this_arg(this_arg.V8Value());
    v8::Local<v8::Value> args[3];

    args[2] = this_value.V8Value();

    while (true) {
      KeyType key;
      ValueType value;

      if (!source->Next(script_state, key, value, exception_state))
        return;

      ASSERT(!exception_state.HadException());

      args[0] = ToV8(value, creation_context, isolate);
      args[1] = ToV8(key, creation_context, isolate);
      if (args[0].IsEmpty() || args[1].IsEmpty()) {
        if (try_catch.HasCaught())
          exception_state.RethrowV8Exception(try_catch.Exception());
        return;
      }

      v8::Local<v8::Value> result;
      if (!V8ScriptRunner::CallFunction(v8_callback,
                                        ExecutionContext::From(script_state),
                                        v8_this_arg, 3, args, isolate)
               .ToLocal(&result)) {
        exception_state.RethrowV8Exception(try_catch.Exception());
        return;
      }
    }
  }

  class IterationSource : public GarbageCollectedFinalized<IterationSource> {
   public:
    virtual ~IterationSource() {}

    // If end of iteration has been reached or an exception thrown: return
    // false.  Otherwise: set |key| and |value| and return true.
    virtual bool Next(ScriptState*, KeyType&, ValueType&, ExceptionState&) = 0;

    DEFINE_INLINE_VIRTUAL_TRACE() {}
  };

 private:
  virtual IterationSource* StartIteration(ScriptState*, ExceptionState&) = 0;

  struct KeySelector {
    STATIC_ONLY(KeySelector);
    static const KeyType& Select(ScriptState*,
                                 const KeyType& key,
                                 const ValueType& value) {
      return key;
    }
  };
  struct ValueSelector {
    STATIC_ONLY(ValueSelector);
    static const ValueType& Select(ScriptState*,
                                   const KeyType& key,
                                   const ValueType& value) {
      return value;
    }
  };
  struct EntrySelector {
    STATIC_ONLY(EntrySelector);
    static Vector<ScriptValue, 2> Select(ScriptState* script_state,
                                         const KeyType& key,
                                         const ValueType& value) {
      v8::Local<v8::Object> creation_context =
          script_state->GetContext()->Global();
      v8::Isolate* isolate = script_state->GetIsolate();

      Vector<ScriptValue, 2> entry;
      entry.push_back(
          ScriptValue(script_state, ToV8(key, creation_context, isolate)));
      entry.push_back(
          ScriptValue(script_state, ToV8(value, creation_context, isolate)));
      return entry;
    }
  };

  template <typename Selector>
  class IterableIterator final : public Iterator {
   public:
    explicit IterableIterator(IterationSource* source) : source_(source) {}

    ScriptValue next(ScriptState* script_state,
                     ExceptionState& exception_state) override {
      KeyType key;
      ValueType value;

      if (!source_->Next(script_state, key, value, exception_state))
        return V8IteratorResultDone(script_state);

      return V8IteratorResult(script_state,
                              Selector::Select(script_state, key, value));
    }

    ScriptValue next(ScriptState* script_state,
                     ScriptValue,
                     ExceptionState& exception_state) override {
      return next(script_state, exception_state);
    }

    DEFINE_INLINE_VIRTUAL_TRACE() {
      visitor->Trace(source_);
      Iterator::Trace(visitor);
    }

   private:
    Member<IterationSource> source_;
  };
};

// Utiltity mixin base-class for classes implementing IDL interfaces with
// "iterable<T1, T2>".
template <typename KeyType, typename ValueType>
class PairIterable : public Iterable<KeyType, ValueType> {
 public:
  Iterator* GetIterator(ScriptState* script_state,
                        ExceptionState& exception_state) {
    return this->entriesForBinding(script_state, exception_state);
  }
};

}  // namespace blink

#endif  // Iterable_h
