// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/bluetooth/BluetoothDevice.h"

#include "bindings/core/v8/CallbackPromiseAdapter.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/DOMException.h"
#include "core/events/Event.h"
#include "modules/bluetooth/Bluetooth.h"
#include "modules/bluetooth/BluetoothAttributeInstanceMap.h"
#include "modules/bluetooth/BluetoothError.h"
#include "modules/bluetooth/BluetoothRemoteGATTServer.h"
#include <memory>
#include <utility>

namespace blink {

BluetoothDevice::BluetoothDevice(ExecutionContext* context,
                                 mojom::blink::WebBluetoothDevicePtr device,
                                 Bluetooth* bluetooth)
    : ContextLifecycleObserver(context),
      attribute_instance_map_(new BluetoothAttributeInstanceMap(this)),
      device_(std::move(device)),
      gatt_(BluetoothRemoteGATTServer::Create(context, this)),
      bluetooth_(bluetooth) {}

// static
BluetoothDevice* BluetoothDevice::Take(
    ScriptPromiseResolver* resolver,
    mojom::blink::WebBluetoothDevicePtr device,
    Bluetooth* bluetooth) {
  return new BluetoothDevice(resolver->GetExecutionContext(), std::move(device),
                             bluetooth);
}

BluetoothRemoteGATTService* BluetoothDevice::GetOrCreateRemoteGATTService(
    mojom::blink::WebBluetoothRemoteGATTServicePtr service,
    bool is_primary,
    const String& device_instance_id) {
  return attribute_instance_map_->GetOrCreateRemoteGATTService(
      std::move(service), is_primary, device_instance_id);
}

bool BluetoothDevice::IsValidService(const String& service_instance_id) {
  return attribute_instance_map_->ContainsService(service_instance_id);
}

BluetoothRemoteGATTCharacteristic*
BluetoothDevice::GetOrCreateRemoteGATTCharacteristic(
    ExecutionContext* context,
    mojom::blink::WebBluetoothRemoteGATTCharacteristicPtr characteristic,
    BluetoothRemoteGATTService* service) {
  return attribute_instance_map_->GetOrCreateRemoteGATTCharacteristic(
      context, std::move(characteristic), service);
}

bool BluetoothDevice::IsValidCharacteristic(
    const String& characteristic_instance_id) {
  return attribute_instance_map_->ContainsCharacteristic(
      characteristic_instance_id);
}

BluetoothRemoteGATTDescriptor*
BluetoothDevice::GetOrCreateBluetoothRemoteGATTDescriptor(
    mojom::blink::WebBluetoothRemoteGATTDescriptorPtr descriptor,
    BluetoothRemoteGATTCharacteristic* characteristic) {
  return attribute_instance_map_->GetOrCreateBluetoothRemoteGATTDescriptor(
      std::move(descriptor), characteristic);
}

bool BluetoothDevice::IsValidDescriptor(const String& descriptor_instance_id) {
  return attribute_instance_map_->ContainsDescriptor(descriptor_instance_id);
}

void BluetoothDevice::ClearAttributeInstanceMapAndFireEvent() {
  attribute_instance_map_->Clear();
  DispatchEvent(Event::CreateBubble(EventTypeNames::gattserverdisconnected));
}

const WTF::AtomicString& BluetoothDevice::InterfaceName() const {
  return EventTargetNames::BluetoothDevice;
}

ExecutionContext* BluetoothDevice::GetExecutionContext() const {
  return ContextLifecycleObserver::GetExecutionContext();
}

DEFINE_TRACE(BluetoothDevice) {
  visitor->Trace(attribute_instance_map_);
  visitor->Trace(gatt_);
  visitor->Trace(bluetooth_);
  EventTargetWithInlineData::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
}

}  // namespace blink
