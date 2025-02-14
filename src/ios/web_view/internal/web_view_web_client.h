// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_VIEW_INTERNAL_WEB_VIEW_WEB_CLIENT_H_
#define IOS_WEB_VIEW_INTERNAL_WEB_VIEW_WEB_CLIENT_H_

#include <memory>

#include "base/compiler_specific.h"
#import "ios/web/public/web_client.h"

namespace ios_web_view {
class WebViewBrowserState;
class WebViewWebMainParts;

// WebView implementation of WebClient.
class WebViewWebClient : public web::WebClient {
 public:
  explicit WebViewWebClient(const std::string& user_agent_product);
  ~WebViewWebClient() override;

  // WebClient implementation.
  web::WebMainParts* CreateWebMainParts() override;
  std::string GetProduct() const override;
  std::string GetUserAgent(web::UserAgentType type) const override;
  NSString* GetEarlyPageScript(web::BrowserState* browser_state) const override;

  // Normal browser state associated with the receiver.
  WebViewBrowserState* browser_state() const;
  // Off the record browser state  associated with the receiver.
  WebViewBrowserState* off_the_record_browser_state() const;

 private:
  // The name of the product to be used in the User Agent string.
  std::string user_agent_product_;

  // The WebMainParts created by |CreateWebMainParts()|.
  WebViewWebMainParts* web_main_parts_;

  DISALLOW_COPY_AND_ASSIGN(WebViewWebClient);
};

}  // namespace ios_web_view

#endif  // IOS_WEB_VIEW_INTERNAL_WEB_VIEW_WEB_CLIENT_H_
