// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/modules/v8/WebGLAny.h"

#include "bindings/core/v8/ToV8ForCore.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

ScriptValue WebGLAny(ScriptState* script_state, bool value) {
  return ScriptValue(script_state,
                     V8Boolean(value, script_state->GetIsolate()));
}

ScriptValue WebGLAny(ScriptState* script_state,
                     const bool* value,
                     size_t size) {
  v8::Local<v8::Array> array = v8::Array::New(script_state->GetIsolate(), size);
  for (size_t i = 0; i < size; ++i) {
    if (!V8CallBoolean(array->CreateDataProperty(
            script_state->GetContext(), i,
            V8Boolean(value[i], script_state->GetIsolate()))))
      return ScriptValue();
  }
  return ScriptValue(script_state, array);
}

ScriptValue WebGLAny(ScriptState* script_state, const Vector<bool>& value) {
  size_t size = value.size();
  v8::Local<v8::Array> array = v8::Array::New(script_state->GetIsolate(), size);
  for (size_t i = 0; i < size; ++i) {
    if (!V8CallBoolean(array->CreateDataProperty(
            script_state->GetContext(), i,
            V8Boolean(value[i], script_state->GetIsolate()))))
      return ScriptValue();
  }
  return ScriptValue(script_state, array);
}

ScriptValue WebGLAny(ScriptState* script_state, const Vector<unsigned>& value) {
  size_t size = value.size();
  v8::Local<v8::Array> array = v8::Array::New(script_state->GetIsolate(), size);
  for (size_t i = 0; i < size; ++i) {
    if (!V8CallBoolean(array->CreateDataProperty(
            script_state->GetContext(), i,
            v8::Integer::NewFromUnsigned(script_state->GetIsolate(),
                                         value[i]))))
      return ScriptValue();
  }
  return ScriptValue(script_state, array);
}

ScriptValue WebGLAny(ScriptState* script_state, const Vector<int>& value) {
  size_t size = value.size();
  v8::Local<v8::Array> array = v8::Array::New(script_state->GetIsolate(), size);
  for (size_t i = 0; i < size; ++i) {
    if (!V8CallBoolean(array->CreateDataProperty(
            script_state->GetContext(), i,
            v8::Integer::New(script_state->GetIsolate(), value[i]))))
      return ScriptValue();
  }
  return ScriptValue(script_state, array);
}

ScriptValue WebGLAny(ScriptState* script_state, int value) {
  return ScriptValue(script_state,
                     v8::Integer::New(script_state->GetIsolate(), value));
}

ScriptValue WebGLAny(ScriptState* script_state, unsigned value) {
  return ScriptValue(
      script_state, v8::Integer::NewFromUnsigned(script_state->GetIsolate(),
                                                 static_cast<unsigned>(value)));
}

ScriptValue WebGLAny(ScriptState* script_state, int64_t value) {
  return ScriptValue(script_state, v8::Number::New(script_state->GetIsolate(),
                                                   static_cast<double>(value)));
}

ScriptValue WebGLAny(ScriptState* script_state, uint64_t value) {
  return ScriptValue(script_state, v8::Number::New(script_state->GetIsolate(),
                                                   static_cast<double>(value)));
}

ScriptValue WebGLAny(ScriptState* script_state, float value) {
  return ScriptValue(script_state,
                     v8::Number::New(script_state->GetIsolate(), value));
}

ScriptValue WebGLAny(ScriptState* script_state, String value) {
  return ScriptValue(script_state, V8String(script_state->GetIsolate(), value));
}

ScriptValue WebGLAny(ScriptState* script_state, WebGLObject* value) {
  return ScriptValue(script_state,
                     ToV8(value, script_state->GetContext()->Global(),
                          script_state->GetIsolate()));
}

ScriptValue WebGLAny(ScriptState* script_state, DOMFloat32Array* value) {
  return ScriptValue(script_state,
                     ToV8(value, script_state->GetContext()->Global(),
                          script_state->GetIsolate()));
}

ScriptValue WebGLAny(ScriptState* script_state, DOMInt32Array* value) {
  return ScriptValue(script_state,
                     ToV8(value, script_state->GetContext()->Global(),
                          script_state->GetIsolate()));
}

ScriptValue WebGLAny(ScriptState* script_state, DOMUint8Array* value) {
  return ScriptValue(script_state,
                     ToV8(value, script_state->GetContext()->Global(),
                          script_state->GetIsolate()));
}

ScriptValue WebGLAny(ScriptState* script_state, DOMUint32Array* value) {
  return ScriptValue(script_state,
                     ToV8(value, script_state->GetContext()->Global(),
                          script_state->GetIsolate()));
}

}  // namespace blink
