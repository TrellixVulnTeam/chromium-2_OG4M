// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_display_service_factory.h"

#include "base/command_line.h"
#include "base/memory/singleton.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/message_center_display_service.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/features.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"

#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
#include "chrome/browser/notifications/native_notification_display_service.h"
#endif

// static
NotificationDisplayService* NotificationDisplayServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<NotificationDisplayService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true /* create */));
}

// static
NotificationDisplayServiceFactory*
NotificationDisplayServiceFactory::GetInstance() {
  return base::Singleton<NotificationDisplayServiceFactory>::get();
}

NotificationDisplayServiceFactory::NotificationDisplayServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "NotificationDisplayService",
          BrowserContextDependencyManager::GetInstance()) {}

// Selection of the implementation works as follows:
//   - Android always uses the NativeNotificationDisplayService.
//   - Mac uses the MessageCenterDisplayService by default, but can use the
//     NativeNotificationDisplayService by using the chrome://flags or via
//     the --enable-features=NativeNotifications command line flag.
//   - All other platforms always use the MessageCenterDisplayService.
KeyedService* NotificationDisplayServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
#if defined(OS_ANDROID)
  return new NativeNotificationDisplayService(
      Profile::FromBrowserContext(context),
      g_browser_process->notification_platform_bridge());
#else   // defined(OS_ANDROID)
  if (base::FeatureList::IsEnabled(features::kNativeNotifications) &&
      g_browser_process->notification_platform_bridge()) {
    return new NativeNotificationDisplayService(
        Profile::FromBrowserContext(context),
        g_browser_process->notification_platform_bridge());
  }
#endif  // defined(OS_ANDROID)
#endif  // BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
  return new MessageCenterDisplayService(
      Profile::FromBrowserContext(context),
      g_browser_process->notification_ui_manager());
}

content::BrowserContext*
NotificationDisplayServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextOwnInstanceInIncognito(context);
}
