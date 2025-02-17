// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_IT2ME_IT2ME_NATIVE_MESSAGING_HOST_H_
#define REMOTING_HOST_IT2ME_IT2ME_NATIVE_MESSAGING_HOST_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "extensions/browser/api/messaging/native_message_host.h"
#include "remoting/host/it2me/it2me_host.h"
#include "remoting/signaling/delegating_signal_strategy.h"

#if !defined(OS_CHROMEOS)
#include "remoting/host/native_messaging/log_message_handler.h"
#endif

namespace base {
class DictionaryValue;
class Value;
class SingleThreadTaskRunner;
}  // namespace base

namespace remoting {

class ChromotingHostContext;
class ElevatedNativeMessagingHost;
class PolicyWatcher;

// Implementation of the native messaging host process.
class It2MeNativeMessagingHost : public It2MeHost::Observer,
                                 public extensions::NativeMessageHost {
 public:
  It2MeNativeMessagingHost(bool needs_elevation,
                           policy::PolicyService* policy_service,
                           std::unique_ptr<ChromotingHostContext> host_context,
                           std::unique_ptr<It2MeHostFactory> host_factory);
  ~It2MeNativeMessagingHost() override;

  // extensions::NativeMessageHost implementation.
  void OnMessage(const std::string& message) override;
  void Start(Client* client) override;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner() const override;

  // It2MeHost::Observer implementation.
  void OnClientAuthenticated(const std::string& client_username)
      override;
  void OnStoreAccessCode(const std::string& access_code,
                                 base::TimeDelta access_code_lifetime) override;
  void OnNatPolicyChanged(bool nat_traversal_enabled) override;
  void OnStateChanged(It2MeHostState state,
                      const std::string& error_message) override;

  static std::string HostStateToString(It2MeHostState host_state);

 private:
  // These "Process.." methods handle specific request types. The |response|
  // dictionary is pre-filled by ProcessMessage() with the parts of the
  // response already known ("id" and "type" fields).
  void ProcessHello(std::unique_ptr<base::DictionaryValue> message,
                    std::unique_ptr<base::DictionaryValue> response) const;
  void ProcessConnect(std::unique_ptr<base::DictionaryValue> message,
                      std::unique_ptr<base::DictionaryValue> response);
  void ProcessDisconnect(std::unique_ptr<base::DictionaryValue> message,
                         std::unique_ptr<base::DictionaryValue> response);
  void ProcessIncomingIq(std::unique_ptr<base::DictionaryValue> message,
                         std::unique_ptr<base::DictionaryValue> response);
  void SendErrorAndExit(std::unique_ptr<base::DictionaryValue> response,
                        const std::string& description) const;
  void SendMessageToClient(std::unique_ptr<base::Value> message) const;

  // Callback for DelegatingSignalStrategy.
  void SendOutgoingIq(const std::string& iq);

  // Called when initial policies are read.
  void OnPolicyUpdate(std::unique_ptr<base::DictionaryValue> policies);

  // Returns whether the request was successfully sent to the elevated host.
  bool DelegateToElevatedHost(std::unique_ptr<base::DictionaryValue> message);

  // Used to determine whether to create and pass messages to an elevated host.
  bool needs_elevation_ = false;

#if defined(OS_WIN)
  // Controls the lifetime of the elevated native messaging host process.
  // Note: 'elevated' in this instance means having the UiAccess privilege, not
  // being run as a higher privilege user.
  std::unique_ptr<ElevatedNativeMessagingHost> elevated_host_;
#endif  // defined(OS_WIN)

  Client* client_ = nullptr;
  DelegatingSignalStrategy::IqCallback incoming_message_callback_;
  std::unique_ptr<ChromotingHostContext> host_context_;
  std::unique_ptr<It2MeHostFactory> factory_;
  scoped_refptr<It2MeHost> it2me_host_;

#if !defined(OS_CHROMEOS)
  // Don't install a log message handler on ChromeOS because we run in the
  // browser process and don't want to intercept all its log messages.
  std::unique_ptr<LogMessageHandler> log_message_handler_;
#endif

  // Cached, read-only copies of |it2me_host_| session state.
  It2MeHostState state_;
  std::string access_code_;
  base::TimeDelta access_code_lifetime_;
  std::string client_username_;

  // Indicates whether or not a policy has ever been read. This is to ensure
  // that on startup, we do not accidentally start a connection before we have
  // queried our policy restrictions.
  bool policy_received_ = false;

  policy::PolicyService* policy_service_ = nullptr;

  // Used to retrieve Chrome policies set for the local machine.
  std::unique_ptr<PolicyWatcher> policy_watcher_;

  // On startup, it is possible to have Connect() called before the policy read
  // is completed.  Rather than just failing, we thunk the connection call so
  // it can be executed after at least one successful policy read. This
  // variable contains the thunk if it is necessary.
  base::Closure pending_connect_;

  base::WeakPtr<It2MeNativeMessagingHost> weak_ptr_;
  base::WeakPtrFactory<It2MeNativeMessagingHost> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(It2MeNativeMessagingHost);
};

}  // namespace remoting

#endif  // REMOTING_HOST_IT2ME_IT2ME_NATIVE_MESSAGING_HOST_H_
