// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/external_data_fetcher.h"
#include "components/policy/core/common/external_data_manager.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/core/common/policy_details.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/core/common/schema.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using testing::Return;
using testing::_;

namespace policy {

namespace {

const char kMainSettingsPage[] = "chrome://settings-frame";

const char kCrosSettingsPrefix[] = "cros.";

std::string GetPolicyName(const std::string& policy_name_decorated) {
  const size_t offset = policy_name_decorated.find('.');
  if (offset != std::string::npos)
    return policy_name_decorated.substr(0, offset);
  return policy_name_decorated;
}

// Contains the details of a single test case verifying that the controlled
// setting indicators for a pref affected by a policy work correctly. This is
// part of the data loaded from chrome/test/data/policy/policy_test_cases.json.
class IndicatorTestCase {
 public:
  IndicatorTestCase(const base::DictionaryValue& policy,
                    const std::string& value,
                    bool readonly)
      : policy_(policy.DeepCopy()), value_(value), readonly_(readonly) {}
  ~IndicatorTestCase() {}

  const base::DictionaryValue& policy() const { return *policy_; }

  const std::string& value() const { return value_; }

  bool readonly() const { return readonly_; }

 private:
  std::unique_ptr<base::DictionaryValue> policy_;
  std::string value_;
  bool readonly_;

  DISALLOW_COPY_AND_ASSIGN(IndicatorTestCase);
};

// Contains the testing details for a single pref affected by a policy. This is
// part of the data loaded from chrome/test/data/policy/policy_test_cases.json.
class PrefMapping {
 public:
  PrefMapping(const std::string& pref,
              bool is_local_state,
              bool check_for_mandatory,
              bool check_for_recommended,
              const std::string& indicator_test_url,
              const std::string& indicator_test_setup_js,
              const std::string& indicator_selector)
      : pref_(pref),
        is_local_state_(is_local_state),
        check_for_mandatory_(check_for_mandatory),
        check_for_recommended_(check_for_recommended),
        indicator_test_url_(indicator_test_url),
        indicator_test_setup_js_(indicator_test_setup_js),
        indicator_selector_(indicator_selector) {}
  ~PrefMapping() {}

  const std::string& pref() const { return pref_; }

  bool is_local_state() const { return is_local_state_; }

  bool check_for_mandatory() const { return check_for_mandatory_; }

  bool check_for_recommended() const { return check_for_recommended_; }

  const std::string& indicator_test_url() const { return indicator_test_url_; }

  const std::string& indicator_test_setup_js() const {
    return indicator_test_setup_js_;
  }

  const std::string& indicator_selector() const {
    return indicator_selector_;
  }

  const std::vector<std::unique_ptr<IndicatorTestCase>>& indicator_test_cases()
      const {
    return indicator_test_cases_;
  }
  void AddIndicatorTestCase(std::unique_ptr<IndicatorTestCase> test_case) {
    indicator_test_cases_.push_back(std::move(test_case));
  }

 private:
  const std::string pref_;
  const bool is_local_state_;
  const bool check_for_mandatory_;
  const bool check_for_recommended_;
  const std::string indicator_test_url_;
  const std::string indicator_test_setup_js_;
  const std::string indicator_selector_;
  std::vector<std::unique_ptr<IndicatorTestCase>> indicator_test_cases_;

  DISALLOW_COPY_AND_ASSIGN(PrefMapping);
};

// Contains the testing details for a single policy. This is part of the data
// loaded from chrome/test/data/policy/policy_test_cases.json.
class PolicyTestCase {
 public:
  PolicyTestCase(const std::string& name,
                 bool is_official_only,
                 bool can_be_recommended,
                 const std::string& indicator_selector)
      : name_(name),
        is_official_only_(is_official_only),
        can_be_recommended_(can_be_recommended),
        indicator_selector_(indicator_selector) {}
  ~PolicyTestCase() {}

  const std::string& name() const { return name_; }

  bool is_official_only() const { return is_official_only_; }

  bool can_be_recommended() const { return can_be_recommended_; }

  bool IsOsSupported() const {
#if defined(OS_WIN)
    const std::string os("win");
#elif defined(OS_MACOSX)
    const std::string os("mac");
#elif defined(OS_CHROMEOS)
    const std::string os("chromeos");
#elif defined(OS_LINUX)
    const std::string os("linux");
#else
#error "Unknown platform"
#endif
    return std::find(supported_os_.begin(), supported_os_.end(), os) !=
        supported_os_.end();
  }
  void AddSupportedOs(const std::string& os) { supported_os_.push_back(os); }

  bool IsSupported() const {
#if !defined(GOOGLE_CHROME_BUILD)
    if (is_official_only())
      return false;
#endif
    return IsOsSupported();
  }

  const base::DictionaryValue& test_policy() const { return test_policy_; }
  void SetTestPolicy(const base::DictionaryValue& policy) {
    test_policy_.Clear();
    test_policy_.MergeDictionary(&policy);
  }

  const std::vector<std::unique_ptr<PrefMapping>>& pref_mappings() const {
    return pref_mappings_;
  }
  void AddPrefMapping(std::unique_ptr<PrefMapping> pref_mapping) {
    pref_mappings_.push_back(std::move(pref_mapping));
  }

  const std::string& indicator_selector() const { return indicator_selector_; }

 private:
  std::string name_;
  bool is_official_only_;
  bool can_be_recommended_;
  std::vector<std::string> supported_os_;
  base::DictionaryValue test_policy_;
  std::vector<std::unique_ptr<PrefMapping>> pref_mappings_;
  std::string indicator_selector_;

  DISALLOW_COPY_AND_ASSIGN(PolicyTestCase);
};

// Parses all policy test cases and makes them available in a map.
class PolicyTestCases {
 public:
  typedef std::vector<PolicyTestCase*> PolicyTestCaseVector;
  typedef std::map<std::string, PolicyTestCaseVector> PolicyTestCaseMap;
  typedef PolicyTestCaseMap::const_iterator iterator;

  PolicyTestCases() {
    base::FilePath path = ui_test_utils::GetTestFilePath(
        base::FilePath(FILE_PATH_LITERAL("policy")),
        base::FilePath(FILE_PATH_LITERAL("policy_test_cases.json")));
    std::string json;
    if (!base::ReadFileToString(path, &json)) {
      ADD_FAILURE();
      return;
    }
    int error_code = -1;
    std::string error_string;
    base::DictionaryValue* dict = NULL;
    std::unique_ptr<base::Value> value = base::JSONReader::ReadAndReturnError(
        json, base::JSON_PARSE_RFC, &error_code, &error_string);
    if (!value.get() || !value->GetAsDictionary(&dict)) {
      ADD_FAILURE() << "Error parsing policy_test_cases.json: " << error_string;
      return;
    }
    Schema chrome_schema = Schema::Wrap(GetChromeSchemaData());
    if (!chrome_schema.valid()) {
      ADD_FAILURE();
      return;
    }
    for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd();
         it.Advance()) {
      const std::string policy_name = GetPolicyName(it.key());
      if (!chrome_schema.GetKnownProperty(policy_name).valid())
        continue;
      PolicyTestCase* policy_test_case = GetPolicyTestCase(dict, it.key());
      if (policy_test_case)
        policy_test_cases_[policy_name].push_back(policy_test_case);
    }
  }

  ~PolicyTestCases() {
    for (iterator policy = policy_test_cases_.begin();
         policy != policy_test_cases_.end();
         ++policy) {
      for (PolicyTestCaseVector::const_iterator test_case =
               policy->second.begin();
           test_case != policy->second.end();
           ++test_case) {
        delete *test_case;
      }
    }
  }

  const PolicyTestCaseVector* Get(const std::string& name) const {
    const iterator it = policy_test_cases_.find(name);
    return it == end() ? NULL : &it->second;
  }

  const PolicyTestCaseMap& map() const { return policy_test_cases_; }
  iterator begin() const { return policy_test_cases_.begin(); }
  iterator end() const { return policy_test_cases_.end(); }

 private:
  PolicyTestCase* GetPolicyTestCase(const base::DictionaryValue* tests,
                                    const std::string& name) {
    const base::DictionaryValue* policy_test_dict = NULL;
    if (!tests->GetDictionaryWithoutPathExpansion(name, &policy_test_dict))
      return NULL;
    bool is_official_only = false;
    policy_test_dict->GetBoolean("official_only", &is_official_only);
    bool can_be_recommended = false;
    policy_test_dict->GetBoolean("can_be_recommended", &can_be_recommended);
    std::string indicator_selector;
    policy_test_dict->GetString("indicator_selector", &indicator_selector);
    PolicyTestCase* policy_test_case = new PolicyTestCase(name,
                                                          is_official_only,
                                                          can_be_recommended,
                                                          indicator_selector);
    const base::ListValue* os_list = NULL;
    if (policy_test_dict->GetList("os", &os_list)) {
      for (size_t i = 0; i < os_list->GetSize(); ++i) {
        std::string os;
        if (os_list->GetString(i, &os))
          policy_test_case->AddSupportedOs(os);
      }
    }
    const base::DictionaryValue* policy = NULL;
    if (policy_test_dict->GetDictionary("test_policy", &policy))
      policy_test_case->SetTestPolicy(*policy);
    const base::ListValue* pref_mappings = NULL;
    if (policy_test_dict->GetList("pref_mappings", &pref_mappings)) {
      for (size_t i = 0; i < pref_mappings->GetSize(); ++i) {
        const base::DictionaryValue* pref_mapping_dict = NULL;
        std::string pref;
        if (!pref_mappings->GetDictionary(i, &pref_mapping_dict) ||
            !pref_mapping_dict->GetString("pref", &pref)) {
          ADD_FAILURE() << "Malformed pref_mappings entry in "
                        << "policy_test_cases.json.";
          continue;
        }
        bool is_local_state = false;
        pref_mapping_dict->GetBoolean("local_state", &is_local_state);
        bool check_for_mandatory = true;
        pref_mapping_dict->GetBoolean("check_for_mandatory",
                                      &check_for_mandatory);
        bool check_for_recommended = true;
        pref_mapping_dict->GetBoolean("check_for_recommended",
                                      &check_for_recommended);
        std::string indicator_test_url;
        pref_mapping_dict->GetString("indicator_test_url", &indicator_test_url);
        std::string indicator_test_setup_js;
        pref_mapping_dict->GetString("indicator_test_setup_js",
                                     &indicator_test_setup_js);
        std::string indicator_selector;
        pref_mapping_dict->GetString("indicator_selector", &indicator_selector);
        auto pref_mapping = base::MakeUnique<PrefMapping>(
            pref, is_local_state, check_for_mandatory, check_for_recommended,
            indicator_test_url, indicator_test_setup_js, indicator_selector);
        const base::ListValue* indicator_tests = NULL;
        if (pref_mapping_dict->GetList("indicator_tests", &indicator_tests)) {
          for (size_t i = 0; i < indicator_tests->GetSize(); ++i) {
            const base::DictionaryValue* indicator_test_dict = NULL;
            const base::DictionaryValue* policy = NULL;
            if (!indicator_tests->GetDictionary(i, &indicator_test_dict) ||
                !indicator_test_dict->GetDictionary("policy", &policy)) {
              ADD_FAILURE() << "Malformed indicator_tests entry in "
                            << "policy_test_cases.json.";
              continue;
            }
            std::string value;
            indicator_test_dict->GetString("value", &value);
            bool readonly = false;
            indicator_test_dict->GetBoolean("readonly", &readonly);
            pref_mapping->AddIndicatorTestCase(
                base::MakeUnique<IndicatorTestCase>(*policy, value, readonly));
          }
        }
        policy_test_case->AddPrefMapping(std::move(pref_mapping));
      }
    }
    return policy_test_case;
  }

  PolicyTestCaseMap policy_test_cases_;

  DISALLOW_COPY_AND_ASSIGN(PolicyTestCases);
};

// Returns a pseudo-random integer distributed in [0, range).
int GetRandomNumber(int range) {
  return rand() % range;
}

// Splits all known policies into subsets of the given |chunk_size|. The
// policies are shuffled so that there is no correlation between their initial
// alphabetic ordering and the assignment to chunks. This ensures that the
// expected number of policies with long-running test cases is equal for each
// subset. The shuffle algorithm uses a fixed seed, ensuring that no randomness
// is introduced into the testing process.
std::vector<std::vector<std::string> > SplitPoliciesIntoChunks(int chunk_size) {
  Schema chrome_schema = Schema::Wrap(GetChromeSchemaData());
  if (!chrome_schema.valid())
    ADD_FAILURE();

  std::vector<std::string> policies;
  for (Schema::Iterator it = chrome_schema.GetPropertiesIterator();
       !it.IsAtEnd(); it.Advance()) {
    policies.push_back(it.key());
  }

  // Use a fixed random seed to obtain a reproducible shuffle.
  srand(1);
  std::random_shuffle(policies.begin(), policies.end(), GetRandomNumber);

  std::vector<std::vector<std::string> > chunks;
  std::vector<std::string>::const_iterator it = policies.begin();
  const std::vector<std::string>::const_iterator end = policies.end();
  for ( ; end - it >= chunk_size; it += chunk_size)
    chunks.push_back(std::vector<std::string>(it, it + chunk_size));
  if (it != end)
    chunks.push_back(std::vector<std::string>(it, end));
  return chunks;
}

void VerifyControlledSettingIndicators(Browser* browser,
                                       const std::string& selector,
                                       const std::string& value,
                                       const std::string& controlled_by,
                                       bool readonly) {
  std::stringstream javascript;
  javascript << "var nodes = document.querySelectorAll("
             << "    'span.controlled-setting-indicator"
             <<          selector.c_str() << "');"
             << "var indicators = [];"
             << "for (var i = 0; i < nodes.length; i++) {"
             << "  var node = nodes[i];"
             << "  var indicator = {};"
             << "  indicator.value = node.value || '';"
             << "  indicator.controlledBy = node.controlledBy || '';"
             << "  indicator.readOnly = node.readOnly || false;"
             << "  indicator.visible ="
             << "      window.getComputedStyle(node).display != 'none';"
             << "  indicators.push(indicator)"
             << "}"
             << "domAutomationController.send(JSON.stringify(indicators));";
  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  std::string json;
  // Retrieve the state of all controlled setting indicators matching the
  // |selector| as JSON.
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(contents, javascript.str(),
                                                     &json));
  std::unique_ptr<base::Value> value_ptr = base::JSONReader::Read(json);
  const base::ListValue* indicators = NULL;
  ASSERT_TRUE(value_ptr.get());
  ASSERT_TRUE(value_ptr->GetAsList(&indicators));
  // Verify that controlled setting indicators representing |value| are visible
  // and have the correct state while those not representing |value| are
  // invisible.
  if (!controlled_by.empty()) {
    EXPECT_GT(indicators->GetSize(), 0u)
        << "Expected to find at least one controlled setting indicator.";
  }
  bool have_visible_indicators = false;
  for (base::ListValue::const_iterator indicator = indicators->begin();
       indicator != indicators->end(); ++indicator) {
    const base::DictionaryValue* properties = NULL;
    ASSERT_TRUE(indicator->GetAsDictionary(&properties));
    std::string indicator_value;
    std::string indicator_controlled_by;
    bool indicator_readonly;
    bool indicator_visible;
    EXPECT_TRUE(properties->GetString("value", &indicator_value));
    EXPECT_TRUE(properties->GetString("controlledBy",
                                      &indicator_controlled_by));
    EXPECT_TRUE(properties->GetBoolean("readOnly", &indicator_readonly));
    EXPECT_TRUE(properties->GetBoolean("visible", &indicator_visible));
    if (!controlled_by.empty() && (indicator_value == value)) {
      EXPECT_EQ(controlled_by, indicator_controlled_by);
      EXPECT_EQ(readonly, indicator_readonly);
      EXPECT_TRUE(indicator_visible);
      have_visible_indicators = true;
    } else {
      EXPECT_FALSE(indicator_visible);
    }
  }
  if (!controlled_by.empty()) {
    EXPECT_TRUE(have_visible_indicators)
        << "Expected to find at least one visible controlled setting "
        << "indicator.";
  }
}

}  // namespace

typedef InProcessBrowserTest PolicyPrefsTestCoverageTest;

IN_PROC_BROWSER_TEST_F(PolicyPrefsTestCoverageTest, AllPoliciesHaveATestCase) {
  // Verifies that all known policies have a test case in the JSON file.
  // This test fails when a policy is added to
  // components/policy/resources/policy_templates.json but a test case is not
  // added to chrome/test/data/policy/policy_test_cases.json.
  Schema chrome_schema = Schema::Wrap(GetChromeSchemaData());
  ASSERT_TRUE(chrome_schema.valid());

  PolicyTestCases policy_test_cases;
  for (Schema::Iterator it = chrome_schema.GetPropertiesIterator();
       !it.IsAtEnd(); it.Advance()) {
    EXPECT_TRUE(base::ContainsKey(policy_test_cases.map(), it.key()))
        << "Missing policy test case for: " << it.key();
  }
}

// Base class for tests that change policy.
class PolicyPrefsTest : public InProcessBrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    EXPECT_CALL(provider_, IsInitializationComplete(_))
        .WillRepeatedly(Return(true));
    BrowserPolicyConnector::SetPolicyProviderForTesting(&provider_);
  }

  void SetUpOnMainThread() override {
    search_test_utils::WaitForTemplateURLServiceToLoad(
        TemplateURLServiceFactory::GetForProfile(browser()->profile()));
  }

  void TearDownOnMainThread() override { ClearProviderPolicy(); }

  void ClearProviderPolicy() {
    provider_.UpdateChromePolicy(PolicyMap());
    base::RunLoop().RunUntilIdle();
  }

  void SetProviderPolicy(const base::DictionaryValue& policies,
                         PolicyLevel level) {
    PolicyMap policy_map;
    for (base::DictionaryValue::Iterator it(policies);
         !it.IsAtEnd(); it.Advance()) {
      const PolicyDetails* policy_details = GetChromePolicyDetails(it.key());
      ASSERT_TRUE(policy_details);
      policy_map.Set(
          it.key(), level, POLICY_SCOPE_USER, POLICY_SOURCE_CLOUD,
          it.value().CreateDeepCopy(),
          base::WrapUnique(policy_details->max_external_data_size
                               ? new ExternalDataFetcher(nullptr, it.key())
                               : nullptr));
    }
    provider_.UpdateChromePolicy(policy_map);
    base::RunLoop().RunUntilIdle();
  }

  MockConfigurationPolicyProvider provider_;
};

// Verifies that policies make their corresponding preferences become managed,
// and that the user can't override that setting.
IN_PROC_BROWSER_TEST_F(PolicyPrefsTest, PolicyToPrefsMapping) {
  PrefService* local_state = g_browser_process->local_state();
  PrefService* user_prefs = browser()->profile()->GetPrefs();

  const PolicyTestCases test_cases;
  for (PolicyTestCases::iterator policy = test_cases.begin();
       policy != test_cases.end();
       ++policy) {
    for (PolicyTestCases::PolicyTestCaseVector::const_iterator test_case =
             policy->second.begin();
         test_case != policy->second.end();
         ++test_case) {
      const auto& pref_mappings = (*test_case)->pref_mappings();
      if (!(*test_case)->IsSupported() || pref_mappings.empty())
        continue;

      LOG(INFO) << "Testing policy: " << policy->first;

      for (const auto& pref_mapping : pref_mappings) {
        // Skip Chrome OS preferences that use a different backend and cannot be
        // retrieved through the prefs mechanism.
        if (base::StartsWith(pref_mapping->pref(), kCrosSettingsPrefix,
                             base::CompareCase::SENSITIVE))
          continue;

        // Skip preferences that should not be checked when the policy is set to
        // a mandatory value.
        if (!pref_mapping->check_for_mandatory())
          continue;

        PrefService* prefs =
            pref_mapping->is_local_state() ? local_state : user_prefs;
        // The preference must have been registered.
        const PrefService::Preference* pref =
            prefs->FindPreference(pref_mapping->pref().c_str());
        ASSERT_TRUE(pref);

        // Verify that setting the policy overrides the pref.
        ClearProviderPolicy();
        prefs->ClearPref(pref_mapping->pref().c_str());
        EXPECT_TRUE(pref->IsDefaultValue());
        EXPECT_TRUE(pref->IsUserModifiable());
        EXPECT_FALSE(pref->IsUserControlled());
        EXPECT_FALSE(pref->IsManaged());

        SetProviderPolicy((*test_case)->test_policy(), POLICY_LEVEL_MANDATORY);
        EXPECT_FALSE(pref->IsDefaultValue());
        EXPECT_FALSE(pref->IsUserModifiable());
        EXPECT_FALSE(pref->IsUserControlled());
        EXPECT_TRUE(pref->IsManaged());
      }
    }
  }
}

class PolicyPrefIndicatorTest
    : public PolicyPrefsTest,
      public testing::WithParamInterface<std::vector<std::string> > {
};

// Verifies that controlled setting indicators correctly show whether a pref's
// value is recommended or enforced by a corresponding policy.
IN_PROC_BROWSER_TEST_P(PolicyPrefIndicatorTest, CheckPolicyIndicators) {
  const PolicyTestCases test_cases;
  PrefService* local_state = g_browser_process->local_state();
  PrefService* user_prefs = browser()->profile()->GetPrefs();

  ui_test_utils::NavigateToURL(browser(), GURL(kMainSettingsPage));

  for (std::vector<std::string>::const_iterator policy = GetParam().begin();
       policy != GetParam().end();
       ++policy) {
    const std::vector<PolicyTestCase*>* policy_test_cases =
        test_cases.Get(*policy);
    ASSERT_TRUE(policy_test_cases) << "PolicyTestCase not found for "
                                   << *policy;
    for (std::vector<PolicyTestCase*>::const_iterator test_case =
             policy_test_cases->begin();
         test_case != policy_test_cases->end();
         ++test_case) {
      PolicyTestCase* policy_test_case = *test_case;
      if (!policy_test_case->IsSupported())
        continue;
      const auto& pref_mappings = policy_test_case->pref_mappings();
      if (policy_test_case->indicator_selector().empty()) {
        bool has_pref_indicator_tests = false;
        for (const auto& pref_mapping : pref_mappings) {
          PrefService* prefs =
              pref_mapping->is_local_state() ? local_state : user_prefs;
          if (prefs->FindPreference(pref_mapping->pref()))
            prefs->ClearPref(pref_mapping->pref());
          if (!pref_mapping->indicator_test_cases().empty()) {
            has_pref_indicator_tests = true;
            break;
          }
        }
        if (!has_pref_indicator_tests)
          continue;
      }

      LOG(INFO) << "Testing policy: " << *policy;

      if (!policy_test_case->indicator_selector().empty()) {
        // Check that no controlled setting indicator is visible when no value
        // is set by policy.
        ClearProviderPolicy();
        VerifyControlledSettingIndicators(
            browser(),
            policy_test_case->indicator_selector(),
            std::string(),
            std::string(),
            false);
        // Check that the appropriate controlled setting indicator is shown when
        // a value is enforced by policy.
        SetProviderPolicy(policy_test_case->test_policy(),
                          POLICY_LEVEL_MANDATORY);
        VerifyControlledSettingIndicators(
            browser(),
            policy_test_case->indicator_selector(),
            std::string(),
            "policy",
            false);
        // Check that no controlled setting indicator is visible when previously
        // enforced value is removed.
        ClearProviderPolicy();
        VerifyControlledSettingIndicators(
            browser(),
            policy_test_case->indicator_selector(),
            std::string(),
            std::string(),
            false);
      }

      for (const auto& pref_mapping : pref_mappings) {
        const auto& indicator_test_cases = pref_mapping->indicator_test_cases();
        if (indicator_test_cases.empty())
          continue;

        if (!pref_mapping->indicator_test_setup_js().empty()) {
          ASSERT_TRUE(content::ExecuteScript(
              browser()->tab_strip_model()->GetActiveWebContents(),
              pref_mapping->indicator_test_setup_js()));
        }

        // A non-empty indicator_test_url is expected to be used in very
        // few cases, so it's currently implemented by navigating to the URL
        // right before the test and navigating back afterwards.
        // If you introduce many test cases with the same non-empty
        // indicator_test_url, this would be inefficient. We could consider
        // navigting to a specific indicator_test_url once for many test cases
        // instead.
        if (!pref_mapping->indicator_test_url().empty()) {
          ui_test_utils::NavigateToURL(
              browser(), GURL(pref_mapping->indicator_test_url()));
        }

        std::string indicator_selector = pref_mapping->indicator_selector();
        if (indicator_selector.empty())
          indicator_selector = "[pref=\"" + pref_mapping->pref() + "\"]";
        for (auto indicator_test_case = indicator_test_cases.begin();
             indicator_test_case != indicator_test_cases.end();
             ++indicator_test_case) {
          // Check that no controlled setting indicator is visible when no value
          // is set by policy.
          ClearProviderPolicy();
          VerifyControlledSettingIndicators(browser(),
                                            indicator_selector,
                                            std::string(),
                                            std::string(),
                                            false);

          if (pref_mapping->check_for_mandatory()) {
            // Check that the appropriate controlled setting indicator is shown
            // when a value is enforced by policy.
            SetProviderPolicy((*indicator_test_case)->policy(),
                              POLICY_LEVEL_MANDATORY);

            VerifyControlledSettingIndicators(
                browser(),
                indicator_selector,
                (*indicator_test_case)->value(),
                "policy",
                (*indicator_test_case)->readonly());
          }

          if (!policy_test_case->can_be_recommended() ||
              !pref_mapping->check_for_recommended()) {
            continue;
          }

          PrefService* prefs =
              pref_mapping->is_local_state() ? local_state : user_prefs;
          // The preference must have been registered.
          const PrefService::Preference* pref =
              prefs->FindPreference(pref_mapping->pref().c_str());
          ASSERT_TRUE(pref);

          // Check that the appropriate controlled setting indicator is shown
          // when a value is recommended by policy and the user has not
          // overridden the recommendation.
          SetProviderPolicy((*indicator_test_case)->policy(),
                            POLICY_LEVEL_RECOMMENDED);
          VerifyControlledSettingIndicators(browser(),
                                            indicator_selector,
                                            (*indicator_test_case)->value(),
                                            "recommended",
                                            (*indicator_test_case)->readonly());
          // Check that the appropriate controlled setting indicator is shown
          // when a value is recommended by policy and the user has overridden
          // the recommendation.
          prefs->Set(pref_mapping->pref().c_str(), *pref->GetValue());
          VerifyControlledSettingIndicators(browser(),
                                            indicator_selector,
                                            (*indicator_test_case)->value(),
                                            "hasRecommendation",
                                            (*indicator_test_case)->readonly());
          prefs->ClearPref(pref_mapping->pref().c_str());
        }

        if (!pref_mapping->indicator_test_url().empty())
          ui_test_utils::NavigateToURL(browser(), GURL(kMainSettingsPage));
      }
    }
  }
}

INSTANTIATE_TEST_CASE_P(PolicyPrefIndicatorTestInstance,
                        PolicyPrefIndicatorTest,
                        testing::ValuesIn(SplitPoliciesIntoChunks(10)));

}  // namespace policy
