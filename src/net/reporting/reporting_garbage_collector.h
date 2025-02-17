// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_GARBAGE_COLLECTOR_H_
#define NET_REPORTING_REPORTING_GARBAGE_COLLECTOR_H_

#include <memory>

#include "net/base/net_export.h"

namespace base {
class Timer;
}  // namespace base

namespace net {

class ReportingContext;

// Removes reports that have remained undelivered for too long or that have been
// included in too many failed delivery attempts.
class NET_EXPORT ReportingGarbageCollector {
 public:
  // Creates a ReportingGarbageCollector. |context| must outlive the garbage
  // collector.
  static std::unique_ptr<ReportingGarbageCollector> Create(
      ReportingContext* context);

  virtual ~ReportingGarbageCollector();

  // Initializes the GarbageCollector, which performs an initial garbage
  // collection pass over any data already in the Cache.
  virtual void Initialize() = 0;

  // Replaces the internal Timer used for scheduling garbage collection passes
  // with a caller-specified one so that unittests can provide a MockTimer.
  virtual void SetTimerForTesting(std::unique_ptr<base::Timer> timer) = 0;
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_GARBAGE_COLLECTOR_H_
