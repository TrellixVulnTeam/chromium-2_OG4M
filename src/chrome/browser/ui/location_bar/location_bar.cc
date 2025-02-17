// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/location_bar/location_bar.h"

#include "base/scoped_observer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/manifest_handlers/ui_overrides_handler.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/permissions/permissions_data.h"

class LocationBar::ExtensionLoadObserver
    : public extensions::ExtensionRegistryObserver {
 public:
  ExtensionLoadObserver(LocationBar* location_bar,
                        extensions::ExtensionRegistry* registry)
      : location_bar_(location_bar), scoped_observer_(this) {
    scoped_observer_.Add(registry);
  }

  ~ExtensionLoadObserver() override = default;

  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override {
    if (extensions::UIOverrides::RemovesBookmarkButton(extension))
      location_bar_->UpdateBookmarkStarVisibility();
  }

  void OnExtensionUnloaded(
      content::BrowserContext* browser_context,
      const extensions::Extension* extension,
      extensions::UnloadedExtensionInfo::Reason reason) override {
    if (extensions::UIOverrides::RemovesBookmarkButton(extension))
      location_bar_->UpdateBookmarkStarVisibility();
  }

 private:
  // The location bar that owns this object (thus always safe to use).
  LocationBar* const location_bar_;

  ScopedObserver<extensions::ExtensionRegistry,
                 extensions::ExtensionRegistryObserver>
      scoped_observer_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionLoadObserver);
};

LocationBar::LocationBar(Profile* profile) : profile_(profile) {
  if (profile_) {  // profile_ can be null in tests.
    extension_load_observer_ = base::MakeUnique<ExtensionLoadObserver>(
        this, extensions::ExtensionRegistry::Get(profile_));
  }
}

LocationBar::~LocationBar() {
}

bool LocationBar::IsBookmarkStarHiddenByExtension() const {
  const extensions::ExtensionSet& extension_set =
      extensions::ExtensionRegistry::Get(profile_)->enabled_extensions();
  for (extensions::ExtensionSet::const_iterator i = extension_set.begin();
       i != extension_set.end(); ++i) {
    if (extensions::UIOverrides::RemovesBookmarkButton(i->get()) &&
        ((*i)->permissions_data()->HasAPIPermission(
             extensions::APIPermission::kBookmarkManagerPrivate) ||
         extensions::FeatureSwitch::enable_override_bookmarks_ui()
             ->IsEnabled()))
      return true;
  }

  return false;
}

// static
base::string16 LocationBar::GetExtensionName(
    const GURL& url,
    content::WebContents* web_contents) {
  // On ChromeOS, this function can be called using web_contents from
  // SimpleWebViewDialog::GetWebContents() which always returns null.
  // TODO(crbug.com/680329) Remove the null check and make
  // SimpleWebViewDialog::GetWebContents return the proper web contents instead.
  if (!web_contents || !url.SchemeIs(extensions::kExtensionScheme))
    return base::string16();

  content::BrowserContext* browser_context = web_contents->GetBrowserContext();
  extensions::ExtensionRegistry* extension_registry =
      extensions::ExtensionRegistry::Get(browser_context);
  const extensions::Extension* extension =
      extension_registry->enabled_extensions().GetByID(url.host());
  return extension ? base::CollapseWhitespace(
                         base::UTF8ToUTF16(extension->name()), false)
                   : base::string16();
}
