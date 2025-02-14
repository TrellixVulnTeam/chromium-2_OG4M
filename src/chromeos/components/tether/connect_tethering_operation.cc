// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/connect_tethering_operation.h"

#include "chromeos/components/tether/host_scan_device_prioritizer.h"
#include "chromeos/components/tether/message_wrapper.h"
#include "chromeos/components/tether/proto/tether.pb.h"
#include "components/proximity_auth/logging/logging.h"

namespace chromeos {

namespace tether {

// static
ConnectTetheringOperation::Factory*
    ConnectTetheringOperation::Factory::factory_instance_ = nullptr;

// static
std::unique_ptr<ConnectTetheringOperation>
ConnectTetheringOperation::Factory::NewInstance(
    const cryptauth::RemoteDevice& device_to_connect,
    BleConnectionManager* connection_manager,
    HostScanDevicePrioritizer* host_scan_device_prioritizer) {
  if (!factory_instance_) {
    factory_instance_ = new Factory();
  }
  return factory_instance_->BuildInstance(device_to_connect, connection_manager,
                                          host_scan_device_prioritizer);
}

// static
void ConnectTetheringOperation::Factory::SetInstanceForTesting(
    Factory* factory) {
  factory_instance_ = factory;
}

std::unique_ptr<ConnectTetheringOperation>
ConnectTetheringOperation::Factory::BuildInstance(
    const cryptauth::RemoteDevice& device_to_connect,
    BleConnectionManager* connection_manager,
    HostScanDevicePrioritizer* host_scan_device_prioritizer) {
  return base::MakeUnique<ConnectTetheringOperation>(
      device_to_connect, connection_manager, host_scan_device_prioritizer);
}

ConnectTetheringOperation::ConnectTetheringOperation(
    const cryptauth::RemoteDevice& device_to_connect,
    BleConnectionManager* connection_manager,
    HostScanDevicePrioritizer* host_scan_device_prioritizer)
    : MessageTransferOperation(
          std::vector<cryptauth::RemoteDevice>{device_to_connect},
          connection_manager),
      remote_device_(device_to_connect),
      host_scan_device_prioritizer_(host_scan_device_prioritizer),
      error_code_to_return_(
          ConnectTetheringResponse_ResponseCode::
              ConnectTetheringResponse_ResponseCode_UNKNOWN_ERROR) {}

ConnectTetheringOperation::~ConnectTetheringOperation() {}

void ConnectTetheringOperation::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void ConnectTetheringOperation::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void ConnectTetheringOperation::OnDeviceAuthenticated(
    const cryptauth::RemoteDevice& remote_device) {
  DCHECK(remote_devices().size() == 1u && remote_devices()[0] == remote_device);
  SendMessageToDevice(remote_device, base::MakeUnique<MessageWrapper>(
                                         ConnectTetheringRequest()));
}

void ConnectTetheringOperation::OnMessageReceived(
    std::unique_ptr<MessageWrapper> message_wrapper,
    const cryptauth::RemoteDevice& remote_device) {
  if (message_wrapper->GetMessageType() !=
      MessageType::CONNECT_TETHERING_RESPONSE) {
    // If another type of message has been received, ignore it.
    return;
  }

  if (!(remote_device == remote_device_)) {
    // If the message came from another device, ignore it.
    return;
  }

  ConnectTetheringResponse* response =
      static_cast<ConnectTetheringResponse*>(message_wrapper->GetProto().get());
  if (response->response_code() ==
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_SUCCESS) {
    if (response->has_ssid() && response->has_password()) {
      PA_LOG(INFO) << "Received ConnectTetheringResponse from device with ID "
                   << remote_device.GetTruncatedDeviceIdForLogs() << " and "
                   << "response_code == SUCCESS. Config: {ssid: \""
                   << response->ssid() << "\", password: \""
                   << response->password() << "\"}";

      host_scan_device_prioritizer_->RecordSuccessfulConnectTetheringResponse(
          remote_device);

      ssid_to_return_ = response->ssid();
      password_to_return_ = response->password();
    } else {
      PA_LOG(ERROR) << "Received ConnectTetheringResponse from device with ID "
                    << remote_device.GetTruncatedDeviceIdForLogs() << " and "
                    << "response_code == SUCCESS, but the response did not "
                    << "contain a Wi-Fi SSID and/or password.";
    }
  } else {
    PA_LOG(INFO) << "Received ConnectTetheringResponse from device with ID "
                 << remote_device.GetTruncatedDeviceIdForLogs() << " and "
                 << "response_code == " << response->response_code() << ".";
    error_code_to_return_ = response->response_code();
  }

  // Now that a response has been received, the device can be unregistered.
  UnregisterDevice(remote_device);
}

void ConnectTetheringOperation::OnOperationFinished() {
  // Notify observers of the results of this operation in OnOperationFinished()
  // instead of in OnMessageReceived() because observers may delete this
  // ConnectTetheringOperation instance. If this happens, the UnregisterDevice()
  // call in OnMessageReceived() will cause a crash.

  if (!ssid_to_return_.empty()) {
    NotifyObserversOfSuccessfulResponse(ssid_to_return_, password_to_return_);
  } else {
    // At this point, either the operation finished with a failed response or
    // no connection succeeded at all. In these cases, notify observers of a
    // failure.
    NotifyObserversOfConnectionFailure(error_code_to_return_);
  }
}

MessageType ConnectTetheringOperation::GetMessageTypeForConnection() {
  return MessageType::CONNECT_TETHERING_REQUEST;
}

void ConnectTetheringOperation::NotifyObserversOfSuccessfulResponse(
    const std::string& ssid,
    const std::string& password) {
  for (auto& observer : observer_list_) {
    observer.OnSuccessfulConnectTetheringResponse(remote_device_, ssid,
                                                  password);
  }
}

void ConnectTetheringOperation::NotifyObserversOfConnectionFailure(
    ConnectTetheringResponse_ResponseCode error_code) {
  for (auto& observer : observer_list_) {
    observer.OnConnectTetheringFailure(remote_device_, error_code);
  }
}

}  // namespace tether

}  // namespace chromeos
