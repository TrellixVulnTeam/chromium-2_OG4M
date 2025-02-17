// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/client/ios/session/remoting_client_session_delegate.h"

#include "base/strings/sys_string_conversions.h"
#include "remoting/client/chromoting_client_runtime.h"

using base::SysUTF8ToNSString;

namespace remoting {

RemotingClientSessonDelegate::RemotingClientSessonDelegate(
    RemotingClient* client)
    : client_(client), weak_factory_(this) {
  runtime_ = ChromotingClientRuntime::GetInstance();
}

RemotingClientSessonDelegate::~RemotingClientSessonDelegate() {
  client_ = nil;
}

void RemotingClientSessonDelegate::OnConnectionState(
    protocol::ConnectionToHost::State state,
    protocol::ErrorCode error) {
  DCHECK(runtime_->ui_task_runner()->BelongsToCurrentThread());

  [client_ onConnectionState:state error:error];
}

void RemotingClientSessonDelegate::CommitPairingCredentials(
    const std::string& host,
    const std::string& id,
    const std::string& secret) {
  DCHECK(runtime_->ui_task_runner()->BelongsToCurrentThread());

  [client_ commitPairingCredentialsForHost:SysUTF8ToNSString(host)
                                        id:SysUTF8ToNSString(id)
                                    secret:SysUTF8ToNSString(secret)];
}

void RemotingClientSessonDelegate::FetchThirdPartyToken(
    const std::string& token_url,
    const std::string& client_id,
    const std::string& scope) {
  DCHECK(runtime_->ui_task_runner()->BelongsToCurrentThread());

  [client_ fetchThirdPartyTokenForUrl:SysUTF8ToNSString(token_url)
                             clientId:SysUTF8ToNSString(client_id)
                                scope:SysUTF8ToNSString(scope)];
}

void RemotingClientSessonDelegate::SetCapabilities(
    const std::string& capabilities) {
  DCHECK(runtime_->ui_task_runner()->BelongsToCurrentThread());

  [client_ setCapabilities:SysUTF8ToNSString(capabilities)];
}

void RemotingClientSessonDelegate::HandleExtensionMessage(
    const std::string& type,
    const std::string& message) {
  DCHECK(runtime_->ui_task_runner()->BelongsToCurrentThread());

  [client_ handleExtensionMessageOfType:SysUTF8ToNSString(type)
                                message:SysUTF8ToNSString(message)];
}

base::WeakPtr<RemotingClientSessonDelegate>
RemotingClientSessonDelegate::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace remoting
