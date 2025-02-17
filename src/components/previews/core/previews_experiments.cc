// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/previews/core/previews_experiments.h"

#include <string>

#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"

namespace previews {

namespace {

// The group of client-side previews experiments. Actually, this group is only
// expected to control one PreviewsType (OFFLINE) as well as the blacklist.
// Other PreviewsType's will be control by different field trial groups.
const char kClientSidePreviewsFieldTrial[] = "ClientSidePreviews";

const char kEnabled[] = "Enabled";

// Allow offline pages to show for prohibitively slow networks.
const char kOfflinePagesSlowNetwork[] = "show_offline_pages";

// Name for the version parameter of a field trial. Version changes will
// result in older blacklist entries being removed.
const char kVersion[] = "version";

// The threshold of EffectiveConnectionType above which previews will not be
// served.
// See net/nqe/effective_connection_type.h for mapping from string to value.
const char kEffectiveConnectionTypeThreshold[] =
    "max_allowed_effective_connection_type";

// The string that corresponds to enabled for the variation param experiments.
const char kExperimentEnabled[] = "true";

const char kClientLoFiExperimentName[] = "PreviewsClientLoFi";

size_t GetParamValueAsSizeT(const std::string& trial_name,
                            const std::string& param_name,
                            size_t default_value) {
  size_t value;
  if (!base::StringToSizeT(
          base::GetFieldTrialParamValue(trial_name, param_name), &value)) {
    return default_value;
  }
  return value;
}

int GetParamValueAsInt(const std::string& trial_name,
                       const std::string& param_name,
                       int default_value) {
  int value;
  if (!base::StringToInt(base::GetFieldTrialParamValue(trial_name, param_name),
                         &value)) {
    return default_value;
  }
  return value;
}

net::EffectiveConnectionType GetParamValueAsECT(
    const std::string& trial_name,
    const std::string& param_name,
    net::EffectiveConnectionType default_value) {
  net::EffectiveConnectionType value;
  if (!net::GetEffectiveConnectionTypeForName(
          base::GetFieldTrialParamValue(trial_name, param_name), &value)) {
    return default_value;
  }
  return value;
}

}  // namespace

namespace params {

size_t MaxStoredHistoryLengthForPerHostBlackList() {
  return GetParamValueAsSizeT(kClientSidePreviewsFieldTrial,
                              "per_host_max_stored_history_length", 4);
}

size_t MaxStoredHistoryLengthForHostIndifferentBlackList() {
  return GetParamValueAsSizeT(kClientSidePreviewsFieldTrial,
                              "host_indifferent_max_stored_history_length", 10);
}

size_t MaxInMemoryHostsInBlackList() {
  return GetParamValueAsSizeT(kClientSidePreviewsFieldTrial,
                              "max_hosts_in_blacklist", 100);
}

int PerHostBlackListOptOutThreshold() {
  return GetParamValueAsInt(kClientSidePreviewsFieldTrial,
                            "per_host_opt_out_threshold", 2);
}

int HostIndifferentBlackListOptOutThreshold() {
  return GetParamValueAsInt(kClientSidePreviewsFieldTrial,
                            "host_indifferent_opt_out_threshold", 4);
}

base::TimeDelta PerHostBlackListDuration() {
  return base::TimeDelta::FromDays(
      GetParamValueAsInt(kClientSidePreviewsFieldTrial,
                         "per_host_black_list_duration_in_days", 30));
}

base::TimeDelta HostIndifferentBlackListPerHostDuration() {
  return base::TimeDelta::FromDays(GetParamValueAsInt(
      kClientSidePreviewsFieldTrial,
      "host_indifferent_black_list_duration_in_days", 365 * 100));
}

base::TimeDelta SingleOptOutDuration() {
  return base::TimeDelta::FromSeconds(
      GetParamValueAsInt(kClientSidePreviewsFieldTrial,
                         "single_opt_out_duration_in_seconds", 60 * 5));
}

base::TimeDelta OfflinePreviewFreshnessDuration() {
  return base::TimeDelta::FromDays(
      GetParamValueAsInt(kClientSidePreviewsFieldTrial,
                         "offline_preview_freshness_duration_in_days", 7));
}

net::EffectiveConnectionType EffectiveConnectionTypeThresholdForOffline() {
  return GetParamValueAsECT(kClientSidePreviewsFieldTrial,
                            kEffectiveConnectionTypeThreshold,
                            net::EFFECTIVE_CONNECTION_TYPE_SLOW_2G);
}

bool IsOfflinePreviewsEnabled() {
  //  Check if "show_offline_pages" is set to "true".
  return IsIncludedInClientSidePreviewsExperimentsFieldTrial() &&
         base::GetFieldTrialParamValue(kClientSidePreviewsFieldTrial,
                                       kOfflinePagesSlowNetwork) ==
             kExperimentEnabled;
}

int OfflinePreviewsVersion() {
  return GetParamValueAsInt(kClientSidePreviewsFieldTrial, kVersion, 0);
}

bool IsClientLoFiEnabled() {
  return base::StartsWith(
      base::FieldTrialList::FindFullName(kClientLoFiExperimentName), kEnabled,
      base::CompareCase::SENSITIVE);
}

int ClientLoFiVersion() {
  return GetParamValueAsInt(kClientLoFiExperimentName, kVersion, 0);
}

net::EffectiveConnectionType EffectiveConnectionTypeThresholdForClientLoFi() {
  return GetParamValueAsECT(kClientLoFiExperimentName,
                            kEffectiveConnectionTypeThreshold,
                            net::EFFECTIVE_CONNECTION_TYPE_2G);
}

}  // namespace params

bool IsIncludedInClientSidePreviewsExperimentsFieldTrial() {
  // By convention, an experiment in the client-side previews study enables use
  // of at least one client-side previews optimization if its name begins with
  // "Enabled."
  return base::StartsWith(
      base::FieldTrialList::FindFullName(kClientSidePreviewsFieldTrial),
      kEnabled, base::CompareCase::SENSITIVE);
}

}  // namespace previews
