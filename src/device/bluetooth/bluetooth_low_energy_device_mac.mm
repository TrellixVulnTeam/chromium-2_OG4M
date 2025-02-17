// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_low_energy_device_mac.h"

#import <CoreFoundation/CoreFoundation.h>
#include <stddef.h>

#include "base/mac/mac_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/mac/sdk_forward_declarations.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "device/bluetooth/bluetooth_adapter_mac.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_low_energy_peripheral_delegate.h"
#include "device/bluetooth/bluetooth_remote_gatt_characteristic_mac.h"
#include "device/bluetooth/bluetooth_remote_gatt_descriptor_mac.h"
#include "device/bluetooth/bluetooth_remote_gatt_service_mac.h"

using device::BluetoothDevice;
using device::BluetoothLowEnergyDeviceMac;

BluetoothLowEnergyDeviceMac::BluetoothLowEnergyDeviceMac(
    BluetoothAdapterMac* adapter,
    CBPeripheral* peripheral)
    : BluetoothDeviceMac(adapter),
      peripheral_(peripheral, base::scoped_policy::RETAIN),
      discovery_pending_count_(0) {
  DCHECK(BluetoothAdapterMac::IsLowEnergyAvailable());
  DCHECK(peripheral_.get());
  peripheral_delegate_.reset([[BluetoothLowEnergyPeripheralDelegate alloc]
      initWithBluetoothLowEnergyDeviceMac:this]);
  [peripheral_ setDelegate:peripheral_delegate_];
  identifier_ = GetPeripheralIdentifier(peripheral);
  hash_address_ = GetPeripheralHashAddress(peripheral);
  UpdateTimestamp();
}

BluetoothLowEnergyDeviceMac::~BluetoothLowEnergyDeviceMac() {
  if (IsGattConnected()) {
    GetMacAdapter()->DisconnectGatt(this);
  }
}

std::string BluetoothLowEnergyDeviceMac::GetIdentifier() const {
  return identifier_;
}

uint32_t BluetoothLowEnergyDeviceMac::GetBluetoothClass() const {
  return 0x1F00;  // Unspecified Device Class
}

std::string BluetoothLowEnergyDeviceMac::GetAddress() const {
  return hash_address_;
}

BluetoothDevice::VendorIDSource BluetoothLowEnergyDeviceMac::GetVendorIDSource()
    const {
  return VENDOR_ID_UNKNOWN;
}

uint16_t BluetoothLowEnergyDeviceMac::GetVendorID() const {
  return 0;
}

uint16_t BluetoothLowEnergyDeviceMac::GetProductID() const {
  return 0;
}

uint16_t BluetoothLowEnergyDeviceMac::GetDeviceID() const {
  return 0;
}

uint16_t BluetoothLowEnergyDeviceMac::GetAppearance() const {
  // TODO(crbug.com/588083): Implementing GetAppearance()
  // on mac, win, and android platforms for chrome
  NOTIMPLEMENTED();
  return 0;
}

base::Optional<std::string> BluetoothLowEnergyDeviceMac::GetName() const {
  if ([peripheral_ name])
    return base::SysNSStringToUTF8([peripheral_ name]);
  return base::nullopt;
}

bool BluetoothLowEnergyDeviceMac::IsPaired() const {
  return false;
}

bool BluetoothLowEnergyDeviceMac::IsConnected() const {
  return IsGattConnected();
}

bool BluetoothLowEnergyDeviceMac::IsGattConnected() const {
  return ([peripheral_ state] == CBPeripheralStateConnected);
}

bool BluetoothLowEnergyDeviceMac::IsConnectable() const {
  return connectable_;
}

bool BluetoothLowEnergyDeviceMac::IsConnecting() const {
  return ([peripheral_ state] == CBPeripheralStateConnecting);
}

bool BluetoothLowEnergyDeviceMac::ExpectingPinCode() const {
  return false;
}

bool BluetoothLowEnergyDeviceMac::ExpectingPasskey() const {
  return false;
}

bool BluetoothLowEnergyDeviceMac::ExpectingConfirmation() const {
  return false;
}

void BluetoothLowEnergyDeviceMac::GetConnectionInfo(
    const ConnectionInfoCallback& callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::Connect(
    PairingDelegate* pairing_delegate,
    const base::Closure& callback,
    const ConnectErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::SetPinCode(const std::string& pincode) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::SetPasskey(uint32_t passkey) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::ConfirmPairing() {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::RejectPairing() {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::CancelPairing() {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::Disconnect(
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::Forget(const base::Closure& callback,
                                         const ErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::ConnectToService(
    const BluetoothUUID& uuid,
    const ConnectToServiceCallback& callback,
    const ConnectToServiceErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::ConnectToServiceInsecurely(
    const device::BluetoothUUID& uuid,
    const ConnectToServiceCallback& callback,
    const ConnectToServiceErrorCallback& error_callback) {
  NOTIMPLEMENTED();
}

void BluetoothLowEnergyDeviceMac::CreateGattConnectionImpl() {
  if (!IsGattConnected()) {
    GetMacAdapter()->CreateGattConnection(this);
  }
}

void BluetoothLowEnergyDeviceMac::DisconnectGatt() {
  GetMacAdapter()->DisconnectGatt(this);
}

void BluetoothLowEnergyDeviceMac::DidDiscoverPrimaryServices(NSError* error) {
  --discovery_pending_count_;
  if (discovery_pending_count_ < 0) {
    // This should never happens, just in case it happens with a device,
    // discovery_pending_count_ is set back to 0.
    VLOG(1) << *this
            << ": BluetoothLowEnergyDeviceMac::discovery_pending_count_ "
            << discovery_pending_count_;
    discovery_pending_count_ = 0;
    return;
  }
  if (error) {
    // TODO(http://crbug.com/609320): Need to pass the error.
    // TODO(http://crbug.com/609844): Decide what to do if discover failed
    // a device services.
    VLOG(1) << *this << ": Can't discover primary services: "
            << BluetoothAdapterMac::String(error);
    return;
  }

  if (!IsGattConnected()) {
    // Don't create services if the device disconnected.
    VLOG(1) << *this << ": DidDiscoverPrimaryServices, gatt not connected.";
    return;
  }
  VLOG(1) << *this << ": DidDiscoverPrimaryServices, pending count: "
          << discovery_pending_count_;

  for (CBService* cb_service in GetPeripheral().services) {
    BluetoothRemoteGattServiceMac* gatt_service =
        GetBluetoothRemoteGattService(cb_service);
    if (!gatt_service) {
      gatt_service = new BluetoothRemoteGattServiceMac(this, cb_service,
                                                       true /* is_primary */);
      auto result_iter = gatt_services_.insert(std::make_pair(
          gatt_service->GetIdentifier(), base::WrapUnique(gatt_service)));
      DCHECK(result_iter.second);
      VLOG(1) << *gatt_service << ": New service.";
      adapter_->NotifyGattServiceAdded(gatt_service);
    } else {
      VLOG(1) << *gatt_service << ": Known service.";
    }
  }
  if (discovery_pending_count_ == 0) {
    for (auto it = gatt_services_.begin(); it != gatt_services_.end(); ++it) {
      device::BluetoothRemoteGattService* gatt_service = it->second.get();
      device::BluetoothRemoteGattServiceMac* gatt_service_mac =
          static_cast<BluetoothRemoteGattServiceMac*>(gatt_service);
      gatt_service_mac->DiscoverCharacteristics();
    }
    SendNotificationIfDiscoveryComplete();
  }
}

void BluetoothLowEnergyDeviceMac::DidDiscoverCharacteristics(
    CBService* cb_service,
    NSError* error) {
  if (error) {
    // TODO(http://crbug.com/609320): Need to pass the error.
    // TODO(http://crbug.com/609844): Decide what to do if discover failed
    VLOG(1) << *this << ": Can't discover characteristics: "
            << BluetoothAdapterMac::String(error);
    return;
  }

  if (!IsGattConnected()) {
    VLOG(1) << *this << ": DidDiscoverCharacteristics, gatt disconnected.";
    // Don't create characteristics if the device disconnected.
    return;
  }

  BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(cb_service);
  DCHECK(gatt_service);
  gatt_service->DidDiscoverCharacteristics();
  SendNotificationIfDiscoveryComplete();
}

void BluetoothLowEnergyDeviceMac::DidModifyServices(
    NSArray* invalidatedServices) {
  VLOG(1) << *this << ": DidModifyServices: "
          << " invalidated services "
          << base::SysNSStringToUTF8([invalidatedServices description]);
  for (CBService* cb_service in invalidatedServices) {
    BluetoothRemoteGattServiceMac* gatt_service =
        GetBluetoothRemoteGattService(cb_service);
    DCHECK(gatt_service);
    VLOG(1) << gatt_service->GetUUID().canonical_value();
    std::unique_ptr<BluetoothRemoteGattService> scoped_service =
        std::move(gatt_services_[gatt_service->GetIdentifier()]);
    gatt_services_.erase(gatt_service->GetIdentifier());
    adapter_->NotifyGattServiceRemoved(scoped_service.get());
  }
  device_uuids_.ClearServiceUUIDs();
  SetGattServicesDiscoveryComplete(false);
  adapter_->NotifyDeviceChanged(this);
  DiscoverPrimaryServices();
}

void BluetoothLowEnergyDeviceMac::DidUpdateValue(
    CBCharacteristic* characteristic,
    NSError* error) {
  BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(characteristic.service);
  DCHECK(gatt_service);
  gatt_service->DidUpdateValue(characteristic, error);
}

void BluetoothLowEnergyDeviceMac::DidWriteValue(
    CBCharacteristic* characteristic,
    NSError* error) {
  BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(characteristic.service);
  DCHECK(gatt_service);
  gatt_service->DidWriteValue(characteristic, error);
}

void BluetoothLowEnergyDeviceMac::DidUpdateNotificationState(
    CBCharacteristic* characteristic,
    NSError* error) {
  BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(characteristic.service);
  DCHECK(gatt_service);
  gatt_service->DidUpdateNotificationState(characteristic, error);
}

void BluetoothLowEnergyDeviceMac::DidDiscoverDescriptors(
    CBCharacteristic* cb_characteristic,
    NSError* error) {
  if (error) {
    // TODO(http://crbug.com/609320): Need to pass the error.
    // TODO(http://crbug.com/609844): Decide what to do if discover failed
    VLOG(1) << *this << ": Can't discover descriptors: "
            << BluetoothAdapterMac::String(error);
    return;
  }
  if (!IsGattConnected()) {
    VLOG(1) << *this << ": DidDiscoverDescriptors, disconnected.";
    // Don't discover descriptors if the device disconnected.
    return;
  }
  BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(cb_characteristic.service);
  DCHECK(gatt_service);
  gatt_service->DidDiscoverDescriptors(cb_characteristic);
  SendNotificationIfDiscoveryComplete();
}

void BluetoothLowEnergyDeviceMac::DidUpdateValueForDescriptor(
    CBDescriptor* cb_descriptor,
    NSError* error) {
  BluetoothRemoteGattDescriptorMac* gatt_descriptor =
      GetBluetoothRemoteGattDescriptor(cb_descriptor);
  DCHECK(gatt_descriptor);
  gatt_descriptor->DidUpdateValueForDescriptor(error);
}

void BluetoothLowEnergyDeviceMac::DidWriteValueForDescriptor(
    CBDescriptor* cb_descriptor,
    NSError* error) {
  BluetoothRemoteGattDescriptorMac* gatt_descriptor =
      GetBluetoothRemoteGattDescriptor(cb_descriptor);
  DCHECK(gatt_descriptor);
  gatt_descriptor->DidWriteValueForDescriptor(error);
}

// static
std::string BluetoothLowEnergyDeviceMac::GetPeripheralIdentifier(
    CBPeripheral* peripheral) {
  DCHECK(BluetoothAdapterMac::IsLowEnergyAvailable());
  NSUUID* uuid = [peripheral identifier];
  NSString* uuidString = [uuid UUIDString];
  return base::SysNSStringToUTF8(uuidString);
}

// static
std::string BluetoothLowEnergyDeviceMac::GetPeripheralHashAddress(
    CBPeripheral* peripheral) {
  const size_t kCanonicalAddressNumberOfBytes = 6;
  char raw[kCanonicalAddressNumberOfBytes];
  crypto::SHA256HashString(GetPeripheralIdentifier(peripheral), raw,
                           sizeof(raw));
  std::string hash = base::HexEncode(raw, sizeof(raw));
  return BluetoothDevice::CanonicalizeAddress(hash);
}

void BluetoothLowEnergyDeviceMac::DiscoverPrimaryServices() {
  VLOG(1) << *this << ": DiscoverPrimaryServices, pending count "
          << discovery_pending_count_;
  ++discovery_pending_count_;
  [GetPeripheral() discoverServices:nil];
}

void BluetoothLowEnergyDeviceMac::SendNotificationIfDiscoveryComplete() {
  // Notify when all services have been discovered.
  bool discovery_complete =
      discovery_pending_count_ == 0 &&
      std::find_if_not(
          gatt_services_.begin(),
          gatt_services_.end(), [](GattServiceMap::value_type & pair) {
            BluetoothRemoteGattService* gatt_service = pair.second.get();
            return static_cast<BluetoothRemoteGattServiceMac*>(gatt_service)
                ->IsDiscoveryComplete();
          }) == gatt_services_.end();
  if (discovery_complete) {
    VLOG(1) << *this << ": Discovery complete.";
    device_uuids_.ReplaceServiceUUIDs(gatt_services_);
    SetGattServicesDiscoveryComplete(true);
    adapter_->NotifyGattServicesDiscovered(this);
    adapter_->NotifyDeviceChanged(this);
  }
}

device::BluetoothAdapterMac* BluetoothLowEnergyDeviceMac::GetMacAdapter() {
  return static_cast<BluetoothAdapterMac*>(this->adapter_);
}

CBPeripheral* BluetoothLowEnergyDeviceMac::GetPeripheral() {
  return peripheral_;
}

device::BluetoothRemoteGattServiceMac*
BluetoothLowEnergyDeviceMac::GetBluetoothRemoteGattService(
    CBService* cb_service) const {
  for (auto it = gatt_services_.begin(); it != gatt_services_.end(); ++it) {
    device::BluetoothRemoteGattService* gatt_service = it->second.get();
    device::BluetoothRemoteGattServiceMac* gatt_service_mac =
        static_cast<BluetoothRemoteGattServiceMac*>(gatt_service);
    if (gatt_service_mac->GetService() == cb_service)
      return gatt_service_mac;
  }
  return nullptr;
}

device::BluetoothRemoteGattDescriptorMac*
BluetoothLowEnergyDeviceMac::GetBluetoothRemoteGattDescriptor(
    CBDescriptor* cb_descriptor) const {
  CBCharacteristic* cb_characteristic = cb_descriptor.characteristic;
  device::BluetoothRemoteGattServiceMac* gatt_service =
      GetBluetoothRemoteGattService(cb_characteristic.service);
  DCHECK(gatt_service);
  device::BluetoothRemoteGattCharacteristicMac* gatt_characteristic =
      gatt_service->GetBluetoothRemoteGattCharacteristicMac(cb_characteristic);
  DCHECK(gatt_characteristic);
  return gatt_characteristic->GetBluetoothRemoteGattDescriptorMac(
      cb_descriptor);
}

void BluetoothLowEnergyDeviceMac::DidDisconnectPeripheral(NSError* error) {
  VLOG(1) << *this << ": Disconnected from peripheral.";
  if (error) {
    VLOG(1) << *this
            << ": Bluetooth error: " << BluetoothAdapterMac::String(error);
  }
  SetGattServicesDiscoveryComplete(false);
  // Removing all services at once to ensure that calling GetGattService on
  // removed service in GattServiceRemoved returns null.
  GattServiceMap gatt_services_swapped;
  gatt_services_swapped.swap(gatt_services_);
  gatt_services_swapped.clear();
  device_uuids_.ClearServiceUUIDs();
  // There are two cases in which this function will be called:
  //   1. When the connection to the device breaks (either because
  //      we closed it or the device closed it).
  //   2. When we cancel a pending connection request.
  if (create_gatt_connection_error_callbacks_.empty()) {
    // If there are no pending callbacks then the connection broke (#1).
    DidDisconnectGatt(true /* notifyDeviceChanged */);
    return;
  }
  // Else we canceled the connection request (#2).
  // TODO(http://crbug.com/585897): Need to pass the error.
  DidFailToConnectGatt(BluetoothDevice::ConnectErrorCode::ERROR_FAILED);
}

namespace device {

std::ostream& operator<<(std::ostream& out,
                         const BluetoothLowEnergyDeviceMac& device) {
  // TODO(crbug.com/703878): Should use
  // BluetoothLowEnergyDeviceMac::GetNameForDisplay() instead.
  base::Optional<std::string> name = device.GetName();
  const char* name_cstr = name ? name->c_str() : "";
  return out << "<BluetoothLowEnergyDeviceMac " << device.GetAddress() << "/"
             << &device << ", \"" << name_cstr << "\">";
}
}  // namespace device
