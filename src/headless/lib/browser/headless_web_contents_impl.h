// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_
#define HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "headless/lib/browser/headless_window_tree_host.h"
#include "headless/public/headless_devtools_target.h"
#include "headless/public/headless_export.h"
#include "headless/public/headless_web_contents.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}

namespace gfx {
class Size;
}

namespace headless {
class HeadlessBrowserImpl;
class HeadlessTabSocketImpl;
class WebContentsObserverAdapter;

// Exported for tests.
class HEADLESS_EXPORT HeadlessWebContentsImpl
    : public HeadlessWebContents,
      public HeadlessDevToolsTarget,
      public content::RenderProcessHostObserver,
      public content::WebContentsObserver {
 public:
  ~HeadlessWebContentsImpl() override;

  static HeadlessWebContentsImpl* From(HeadlessWebContents* web_contents);

  static std::unique_ptr<HeadlessWebContentsImpl> Create(
      HeadlessWebContents::Builder* builder);

  // Takes ownership of |web_contents|.
  static std::unique_ptr<HeadlessWebContentsImpl> CreateFromWebContents(
      content::WebContents* web_contents,
      HeadlessBrowserContextImpl* browser_context);

  // HeadlessWebContents implementation:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  HeadlessDevToolsTarget* GetDevToolsTarget() override;
  HeadlessTabSocket* GetHeadlessTabSocket() const override;

  // HeadlessDevToolsTarget implementation:
  bool AttachClient(HeadlessDevToolsClient* client) override;
  void ForceAttachClient(HeadlessDevToolsClient* client) override;
  void DetachClient(HeadlessDevToolsClient* client) override;
  bool IsAttached() override;

  // RenderProcessHostObserver implementation:
  void RenderProcessExited(content::RenderProcessHost* host,
                           base::TerminationStatus status,
                           int exit_code) override;
  void RenderProcessHostDestroyed(content::RenderProcessHost* host) override;

  // content::WebContentsObserver implementation:
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;

  content::WebContents* web_contents() const;
  bool OpenURL(const GURL& url);

  void Close() override;

  std::string GetDevToolsAgentHostId();

  HeadlessBrowserImpl* browser() const;
  HeadlessBrowserContextImpl* browser_context() const;

  void set_window_tree_host(std::unique_ptr<HeadlessWindowTreeHost> host) {
    window_tree_host_ = std::move(host);
  }
  HeadlessWindowTreeHost* window_tree_host() const {
    return window_tree_host_.get();
  }

 private:
  // Takes ownership of |web_contents|.
  HeadlessWebContentsImpl(content::WebContents* web_contents,
                          HeadlessBrowserContextImpl* browser_context);

  void InitializeScreen(const gfx::Size& initial_size);
  using MojoService = HeadlessWebContents::Builder::MojoService;

  class Delegate;
  std::unique_ptr<Delegate> web_contents_delegate_;
  std::unique_ptr<HeadlessWindowTreeHost> window_tree_host_;
  std::unique_ptr<content::WebContents> web_contents_;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  std::list<MojoService> mojo_services_;
  std::unique_ptr<HeadlessTabSocketImpl> headless_tab_socket_;

  HeadlessBrowserContextImpl* browser_context_;      // Not owned.
  content::RenderProcessHost* render_process_host_;  // Not owned.

  using ObserverMap =
      std::unordered_map<HeadlessWebContents::Observer*,
                         std::unique_ptr<WebContentsObserverAdapter>>;
  ObserverMap observer_map_;

  DISALLOW_COPY_AND_ASSIGN(HeadlessWebContentsImpl);
};

}  // namespace headless

#endif  // HEADLESS_LIB_BROWSER_HEADLESS_WEB_CONTENTS_IMPL_H_
