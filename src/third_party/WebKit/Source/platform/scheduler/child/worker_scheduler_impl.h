// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_CHILD_WORKER_SCHEDULER_IMPL_H_
#define THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_CHILD_WORKER_SCHEDULER_IMPL_H_

#include "base/macros.h"
#include "platform/scheduler/base/thread_load_tracker.h"
#include "platform/scheduler/child/idle_canceled_delayed_task_sweeper.h"
#include "platform/scheduler/child/idle_helper.h"
#include "platform/scheduler/child/scheduler_helper.h"
#include "platform/scheduler/child/worker_scheduler.h"
#include "public/platform/scheduler/base/task_time_observer.h"

namespace blink {
namespace scheduler {

class SchedulerTqmDelegate;

class BLINK_PLATFORM_EXPORT WorkerSchedulerImpl : public WorkerScheduler,
                                                  public IdleHelper::Delegate,
                                                  public TaskTimeObserver {
 public:
  explicit WorkerSchedulerImpl(
      scoped_refptr<SchedulerTqmDelegate> main_task_runner);
  ~WorkerSchedulerImpl() override;

  // WorkerScheduler implementation:
  scoped_refptr<TaskQueue> DefaultTaskRunner() override;
  scoped_refptr<SingleThreadIdleTaskRunner> IdleTaskRunner() override;
  bool CanExceedIdleDeadlineIfRequired() const override;
  bool ShouldYieldForHighPriorityWork() override;
  void AddTaskObserver(base::MessageLoop::TaskObserver* task_observer) override;
  void RemoveTaskObserver(
      base::MessageLoop::TaskObserver* task_observer) override;
  void Init() override;
  void Shutdown() override;

  // TaskTimeObserver implementation:
  void WillProcessTask(TaskQueue* task_queue, double start_time) override;
  void DidProcessTask(TaskQueue* task_queue,
                      double start_time,
                      double end_time) override;
  void OnBeginNestedMessageLoop() override;

  SchedulerHelper* GetSchedulerHelperForTesting();
  base::TimeTicks CurrentIdleTaskDeadlineForTesting() const;

 protected:
  // IdleHelper::Delegate implementation:
  bool CanEnterLongIdlePeriod(
      base::TimeTicks now,
      base::TimeDelta* next_long_idle_period_delay_out) override;
  void IsNotQuiescent() override {}
  void OnIdlePeriodStarted() override {}
  void OnIdlePeriodEnded() override {}

 private:
  void MaybeStartLongIdlePeriod();

  SchedulerHelper helper_;
  IdleHelper idle_helper_;
  IdleCanceledDelayedTaskSweeper idle_canceled_delayed_task_sweeper_;
  ThreadLoadTracker load_tracker_;
  bool initialized_;
  base::TimeTicks thread_start_time_;

  DISALLOW_COPY_AND_ASSIGN(WorkerSchedulerImpl);
};

}  // namespace scheduler
}  // namespace blink

#endif  // THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_CHILD_WORKER_SCHEDULER_IMPL_H_
