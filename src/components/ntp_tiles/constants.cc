// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_tiles/constants.h"

#include "base/feature_list.h"

namespace ntp_tiles {

const char kPopularSitesFieldTrialName[] = "NTPPopularSites";

extern const base::Feature kPopularSitesBakedInContentFeature{
    "NTPPopularSitesBakedInContent", base::FEATURE_ENABLED_BY_DEFAULT};

}  // namespace ntp_tiles
