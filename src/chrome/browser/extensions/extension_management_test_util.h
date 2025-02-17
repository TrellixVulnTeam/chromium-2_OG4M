// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_TEST_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_TEST_UTIL_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_management_constants.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/extension_id.h"

namespace policy {
class MockConfigurationPolicyProvider;
class PolicyBundle;
}  // namespace policy

namespace extensions {

// Base class for essential routines on preference manipulation.
class ExtensionManagementPrefUpdaterBase {
 public:
  ExtensionManagementPrefUpdaterBase();
  virtual ~ExtensionManagementPrefUpdaterBase();

  // Helper functions for per extension settings.
  void UnsetPerExtensionSettings(const ExtensionId& id);
  void ClearPerExtensionSettings(const ExtensionId& id);

  // Helper functions for 'installation_mode' manipulation.
  void SetBlacklistedByDefault(bool value);
  void ClearInstallationModesForIndividualExtensions();
  void SetIndividualExtensionInstallationAllowed(const ExtensionId& id,
                                                 bool allowed);
  void SetIndividualExtensionAutoInstalled(const ExtensionId& id,
                                           const std::string& update_url,
                                           bool forced);

  // Helper functions for 'install_sources' manipulation.
  void UnsetInstallSources();
  void ClearInstallSources();
  void AddInstallSource(const std::string& install_source);
  void RemoveInstallSource(const std::string& install_source);

  // Helper functions for 'allowed_types' manipulation.
  void UnsetAllowedTypes();
  void ClearAllowedTypes();
  void AddAllowedType(const std::string& allowed_type);
  void RemoveAllowedType(const std::string& allowed_type);

  // Helper functions for 'blocked_permissions' manipulation. |prefix| can be
  // kWildCard or a valid extension ID.
  void UnsetBlockedPermissions(const std::string& prefix);
  void ClearBlockedPermissions(const std::string& prefix);
  void AddBlockedPermission(const std::string& prefix,
                            const std::string& permission);
  void RemoveBlockedPermission(const std::string& prefix,
                               const std::string& permission);

  // Helper functions for 'runtime_blocked_hosts' manipulation. |prefix| can be
  // kWildCard or a valid extension ID.
  void UnsetRuntimeBlockedHosts(const std::string& prefix);
  void ClearRuntimeBlockedHosts(const std::string& prefix);
  void AddRuntimeBlockedHost(const std::string& prefix,
                             const std::string& host);
  void RemoveRuntimeBlockedHost(const std::string& prefix,
                                const std::string& host);

  // Helper functions for 'allowed_permissions' manipulation. |id| must be a
  // valid extension ID.
  void UnsetAllowedPermissions(const std::string& id);
  void ClearAllowedPermissions(const std::string& id);
  void AddAllowedPermission(const std::string& id,
                            const std::string& permission);
  void RemoveAllowedPermission(const std::string& id,
                               const std::string& permission);

  // Helper functions for 'minimum_version_required' manipulation. |id| must be
  // a valid extension ID.
  void SetMinimumVersionRequired(const std::string& id,
                                 const std::string& version);
  void UnsetMinimumVersionRequired(const std::string& id);

  // Expose a read-only preference to user.
  const base::DictionaryValue* GetPref();

 protected:
  // Set the preference with |pref|, pass the ownership of it as well.
  // This function must be called before accessing publicly exposed functions,
  // for example in constructor of subclass.
  void SetPref(base::DictionaryValue* pref);

  // Take the preference. Caller takes ownership of it as well.
  // This function must be called after accessing publicly exposed functions,
  // for example in destructor of subclass.
  std::unique_ptr<base::DictionaryValue> TakePref();

 private:
  // Helper functions for manipulating sub properties like list of strings.
  void ClearList(const std::string& path);
  void AddStringToList(const std::string& path, const std::string& str);
  void RemoveStringFromList(const std::string& path, const std::string& str);

  std::unique_ptr<base::DictionaryValue> pref_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionManagementPrefUpdaterBase);
};

// A helper class to manipulate the extension management preference in unit
// tests.
template <class TestingPrefService>
class ExtensionManagementPrefUpdater
    : public ExtensionManagementPrefUpdaterBase {
 public:
  explicit ExtensionManagementPrefUpdater(TestingPrefService* service)
      : service_(service) {
    const base::Value* pref_value =
        service_->GetManagedPref(pref_names::kExtensionManagement);
    const base::DictionaryValue* dict_value = nullptr;
    if (pref_value && pref_value->GetAsDictionary(&dict_value))
      SetPref(dict_value->DeepCopy());
    else
      SetPref(new base::DictionaryValue);
  }

  virtual ~ExtensionManagementPrefUpdater() {
    service_->SetManagedPref(pref_names::kExtensionManagement, TakePref());
  }

 private:
  TestingPrefService* service_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionManagementPrefUpdater);
};

// A helper class to manipulate the extension management policy in browser
// tests.
class ExtensionManagementPolicyUpdater
    : public ExtensionManagementPrefUpdaterBase {
 public:
  explicit ExtensionManagementPolicyUpdater(
      policy::MockConfigurationPolicyProvider* provider);
  ~ExtensionManagementPolicyUpdater() override;

 private:
  policy::MockConfigurationPolicyProvider* provider_;
  std::unique_ptr<policy::PolicyBundle> policies_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionManagementPolicyUpdater);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_MANAGEMENT_TEST_UTIL_H_
