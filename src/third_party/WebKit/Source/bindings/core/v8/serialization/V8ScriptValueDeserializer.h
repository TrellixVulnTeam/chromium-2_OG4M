// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8ScriptValueDeserializer_h
#define V8ScriptValueDeserializer_h

#include "bindings/core/v8/ScriptState.h"
#include "bindings/core/v8/SerializationTag.h"
#include "bindings/core/v8/SerializedScriptValue.h"
#include "core/CoreExport.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/RefPtr.h"
#include "v8/include/v8.h"

namespace blink {

class File;

// Deserializes V8 values serialized using V8ScriptValueSerializer (or its
// predecessor, ScriptValueSerializer).
//
// Supports only basic JavaScript objects and core DOM types. Support for
// modules types is implemented in a subclass.
//
// A deserializer cannot be used multiple times; it is expected that its
// deserialize method will be invoked exactly once.
class GC_PLUGIN_IGNORE("https://crbug.com/644725") CORE_EXPORT
    V8ScriptValueDeserializer : public v8::ValueDeserializer::Delegate {
  STACK_ALLOCATED();
  WTF_MAKE_NONCOPYABLE(V8ScriptValueDeserializer);

 public:
  using Options = SerializedScriptValue::DeserializeOptions;
  V8ScriptValueDeserializer(RefPtr<ScriptState>,
                            RefPtr<SerializedScriptValue>,
                            const Options& = Options());

  v8::Local<v8::Value> Deserialize();

 protected:
  virtual ScriptWrappable* ReadDOMObject(SerializationTag);

  ScriptState* GetScriptState() const { return script_state_.Get(); }

  uint32_t Version() const { return version_; }
  bool ReadTag(SerializationTag* tag) {
    const void* tag_bytes = nullptr;
    if (!deserializer_.ReadRawBytes(1, &tag_bytes))
      return false;
    *tag = static_cast<SerializationTag>(
        *reinterpret_cast<const uint8_t*>(tag_bytes));
    return true;
  }
  bool ReadUint32(uint32_t* value) { return deserializer_.ReadUint32(value); }
  bool ReadUint64(uint64_t* value) { return deserializer_.ReadUint64(value); }
  bool ReadDouble(double* value) { return deserializer_.ReadDouble(value); }
  bool ReadRawBytes(size_t size, const void** data) {
    return deserializer_.ReadRawBytes(size, data);
  }
  bool ReadUTF8String(String* string_out);

 private:
  void Transfer();

  File* ReadFile();
  File* ReadFileIndex();

  RefPtr<BlobDataHandle> GetOrCreateBlobDataHandle(const String& uuid,
                                                   const String& type,
                                                   uint64_t size);

  // v8::ValueDeserializer::Delegate
  v8::MaybeLocal<v8::Object> ReadHostObject(v8::Isolate*) override;
  v8::MaybeLocal<v8::WasmCompiledModule> GetWasmModuleFromId(v8::Isolate*,
                                                             uint32_t) override;

  RefPtr<ScriptState> script_state_;
  RefPtr<SerializedScriptValue> serialized_script_value_;
  v8::ValueDeserializer deserializer_;

  // Message ports which were transferred in.
  const MessagePortArray* transferred_message_ports_ = nullptr;

  // ImageBitmaps which were transferred in.
  HeapVector<Member<ImageBitmap>> transferred_image_bitmaps_;

  // Blob info for blobs stored by index.
  const WebBlobInfoArray* blob_info_array_ = nullptr;

  // Set during deserialize after the header is read.
  uint32_t version_ = 0;

#if DCHECK_IS_ON()
  bool deserialize_invoked_ = false;
#endif
};

}  // namespace blink

#endif  // V8ScriptValueDeserializer_h
