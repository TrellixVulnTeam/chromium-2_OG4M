// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/scheduler/renderer/renderer_web_scheduler_impl.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "public/platform/scheduler/base/task_queue.h"
#include "platform/scheduler/renderer/renderer_scheduler_impl.h"
#include "platform/scheduler/renderer/web_view_scheduler_impl.h"
#include "platform/RuntimeEnabledFeatures.h"

namespace blink {
namespace scheduler {

RendererWebSchedulerImpl::RendererWebSchedulerImpl(
    RendererSchedulerImpl* renderer_scheduler)
    : WebSchedulerImpl(renderer_scheduler,
                       renderer_scheduler->IdleTaskRunner(),
                       renderer_scheduler->LoadingTaskRunner(),
                       renderer_scheduler->TimerTaskRunner()),
      renderer_scheduler_(renderer_scheduler) {}

RendererWebSchedulerImpl::~RendererWebSchedulerImpl() {}

void RendererWebSchedulerImpl::SuspendTimerQueue() {
  renderer_scheduler_->SuspendTimerQueue();
}

void RendererWebSchedulerImpl::ResumeTimerQueue() {
  renderer_scheduler_->ResumeTimerQueue();
}

std::unique_ptr<blink::WebViewScheduler>
RendererWebSchedulerImpl::CreateWebViewScheduler(
    InterventionReporter* intervention_reporter,
    WebViewScheduler::WebViewSchedulerSettings* settings) {
  return base::WrapUnique(new WebViewSchedulerImpl(
      intervention_reporter, settings, renderer_scheduler_,
      !blink::RuntimeEnabledFeatures::
          timerThrottlingForBackgroundTabsEnabled()));
}

}  // namespace scheduler
}  // namespace blink
