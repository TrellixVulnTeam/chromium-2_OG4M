/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#ifndef MIDIPort_h
#define MIDIPort_h

#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/TraceWrapperMember.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/dom/ExceptionCode.h"
#include "media/midi/midi_service.mojom-blink.h"
#include "modules/EventTargetModules.h"
#include "modules/webmidi/MIDIAccessor.h"
#include "platform/heap/Handle.h"

namespace blink {

class MIDIAccess;

class MIDIPort : public EventTargetWithInlineData,
                 public ActiveScriptWrappable<MIDIPort>,
                 public ContextLifecycleObserver {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(MIDIPort);

 public:
  enum ConnectionState {
    kConnectionStateOpen,
    kConnectionStateClosed,
    kConnectionStatePending
  };

  enum TypeCode { kTypeInput, kTypeOutput };

  ~MIDIPort() override {}

  String connection() const;
  String id() const { return id_; }
  String manufacturer() const { return manufacturer_; }
  String name() const { return name_; }
  String state() const;
  String type() const;
  String version() const { return version_; }

  ScriptPromise open(ScriptState*);
  ScriptPromise close(ScriptState*);

  midi::mojom::PortState GetState() const { return state_; }
  void SetState(midi::mojom::PortState);
  ConnectionState GetConnection() const { return connection_; }

  DECLARE_VIRTUAL_TRACE();

  DECLARE_VIRTUAL_TRACE_WRAPPERS();

  DEFINE_ATTRIBUTE_EVENT_LISTENER(statechange);

  // EventTarget
  const AtomicString& InterfaceName() const override {
    return EventTargetNames::MIDIPort;
  }
  ExecutionContext* GetExecutionContext() const final;

  // ScriptWrappable
  bool HasPendingActivity() const final;

  // ContextLifecycleObserver
  void ContextDestroyed(ExecutionContext*) override;

 protected:
  MIDIPort(MIDIAccess*,
           const String& id,
           const String& manufacturer,
           const String& name,
           TypeCode,
           const String& version,
           midi::mojom::PortState);

  void open();
  MIDIAccess* midiAccess() const { return access_; }

 private:
  ScriptPromise Accept(ScriptState*);
  ScriptPromise Reject(ScriptState*, ExceptionCode, const String& message);

  void SetStates(midi::mojom::PortState, ConnectionState);

  String id_;
  String manufacturer_;
  String name_;
  TypeCode type_;
  String version_;
  TraceWrapperMember<MIDIAccess> access_;
  midi::mojom::PortState state_;
  ConnectionState connection_;
};

}  // namespace blink

#endif  // MIDIPort_h
