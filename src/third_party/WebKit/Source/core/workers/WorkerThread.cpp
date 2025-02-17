/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "core/workers/WorkerThread.h"

#include <limits.h>
#include <memory>
#include "bindings/core/v8/Microtask.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/WorkerOrWorkletScriptController.h"
#include "core/inspector/ConsoleMessageStorage.h"
#include "core/inspector/InspectorTaskRunner.h"
#include "core/inspector/WorkerInspectorController.h"
#include "core/inspector/WorkerThreadDebugger.h"
#include "core/origin_trials/OriginTrialContext.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/ThreadedWorkletGlobalScope.h"
#include "core/workers/WorkerBackingThread.h"
#include "core/workers/WorkerClients.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerReportingProxy.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/Histogram.h"
#include "platform/WaitableEvent.h"
#include "platform/WebThreadSupportingGC.h"
#include "platform/heap/SafePoint.h"
#include "platform/heap/ThreadState.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/Threading.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

using ExitCode = WorkerThread::ExitCode;

// TODO(nhiroki): Adjust the delay based on UMA.
const long long kForcibleTerminationDelayInMs = 2000;  // 2 secs

static Mutex& ThreadSetMutex() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(Mutex, mutex, new Mutex);
  return mutex;
}

static int GetNextWorkerThreadId() {
  DCHECK(IsMainThread());
  static int next_worker_thread_id = 1;
  CHECK_LT(next_worker_thread_id, std::numeric_limits<int>::max());
  return next_worker_thread_id++;
}

WorkerThreadLifecycleContext::WorkerThreadLifecycleContext() {
  DCHECK(IsMainThread());
}

WorkerThreadLifecycleContext::~WorkerThreadLifecycleContext() {
  DCHECK(IsMainThread());
}

void WorkerThreadLifecycleContext::NotifyContextDestroyed() {
  DCHECK(IsMainThread());
  DCHECK(!was_context_destroyed_);
  was_context_destroyed_ = true;
  LifecycleNotifier::NotifyContextDestroyed();
}

WorkerThread::~WorkerThread() {
  DCHECK(IsMainThread());
  MutexLocker lock(ThreadSetMutex());
  DCHECK(WorkerThreads().Contains(this));
  WorkerThreads().erase(this);

  DCHECK_NE(ExitCode::kNotTerminated, exit_code_);
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      EnumerationHistogram, exit_code_histogram,
      new EnumerationHistogram("WorkerThread.ExitCode",
                               static_cast<int>(ExitCode::kLastEnum)));
  exit_code_histogram.Count(static_cast<int>(exit_code_));
}

void WorkerThread::Start(std::unique_ptr<WorkerThreadStartupData> startup_data,
                         ParentFrameTaskRunners* parent_frame_task_runners) {
  DCHECK(IsMainThread());

  if (requested_to_start_)
    return;

  requested_to_start_ = true;
  parent_frame_task_runners_ = parent_frame_task_runners;
  GetWorkerBackingThread().BackingThread().PostTask(
      BLINK_FROM_HERE, CrossThreadBind(&WorkerThread::InitializeOnWorkerThread,
                                       CrossThreadUnretained(this),
                                       WTF::Passed(std::move(startup_data))));
}

void WorkerThread::Terminate() {
  DCHECK(IsMainThread());
  TerminateInternal(TerminationMode::kGraceful);
}

void WorkerThread::TerminateAndWait() {
  DCHECK(IsMainThread());

  // The main thread will be blocked, so asynchronous graceful shutdown does
  // not work.
  TerminateInternal(TerminationMode::kForcible);
  shutdown_event_->Wait();

  // Destruct base::Thread and join the underlying system thread.
  ClearWorkerBackingThread();
}

void WorkerThread::TerminateAndWaitForAllWorkers() {
  DCHECK(IsMainThread());

  // Keep this lock to prevent WorkerThread instances from being destroyed.
  MutexLocker lock(ThreadSetMutex());
  HashSet<WorkerThread*> threads = WorkerThreads();

  // The main thread will be blocked, so asynchronous graceful shutdown does
  // not work.
  for (WorkerThread* thread : threads)
    thread->TerminateInternal(TerminationMode::kForcible);

  for (WorkerThread* thread : threads)
    thread->shutdown_event_->Wait();

  // Destruct base::Thread and join the underlying system threads.
  for (WorkerThread* thread : threads)
    thread->ClearWorkerBackingThread();
}

void WorkerThread::WillProcessTask() {
  DCHECK(IsCurrentThread());

  // No tasks should get executed after we have closed.
  DCHECK(!GlobalScope()->IsClosing());

  if (IsForciblyTerminated()) {
    // The script has been terminated forcibly, which means we need to
    // ask objects in the thread to stop working as soon as possible.
    PrepareForShutdownOnWorkerThread();
  }
}

void WorkerThread::DidProcessTask() {
  DCHECK(IsCurrentThread());
  Microtask::PerformCheckpoint(GetIsolate());
  GlobalScope()->ScriptController()->GetRejectedPromises()->ProcessQueue();
  if (GlobalScope()->IsClosing()) {
    // This WorkerThread will eventually be requested to terminate.
    GetWorkerReportingProxy().DidCloseWorkerGlobalScope();

    // Stop further worker tasks to run after this point.
    PrepareForShutdownOnWorkerThread();
  }
}

v8::Isolate* WorkerThread::GetIsolate() {
  return GetWorkerBackingThread().GetIsolate();
}

bool WorkerThread::IsCurrentThread() {
  return GetWorkerBackingThread().BackingThread().IsCurrentThread();
}

void WorkerThread::PostTask(const WebTraceLocation& location,
                            std::unique_ptr<WTF::Closure> task) {
  DCHECK(IsCurrentThread());
  if (IsInShutdown())
    return;
  GetWorkerBackingThread().BackingThread().PostTask(
      location,
      WTF::Bind(
          &WorkerThread::PerformTaskOnWorkerThread<WTF::kSameThreadAffinity>,
          WTF::Unretained(this), WTF::Passed(std::move(task))));
}

void WorkerThread::PostTask(const WebTraceLocation& location,
                            std::unique_ptr<WTF::CrossThreadClosure> task) {
  if (IsInShutdown())
    return;
  GetWorkerBackingThread().BackingThread().PostTask(
      location,
      CrossThreadBind(
          &WorkerThread::PerformTaskOnWorkerThread<WTF::kCrossThreadAffinity>,
          CrossThreadUnretained(this), WTF::Passed(std::move(task))));
}

void WorkerThread::AppendDebuggerTask(
    std::unique_ptr<CrossThreadClosure> task) {
  DCHECK(IsMainThread());
  if (IsInShutdown())
    return;
  inspector_task_runner_->AppendTask(CrossThreadBind(
      &WorkerThread::PerformDebuggerTaskOnWorkerThread,
      CrossThreadUnretained(this), WTF::Passed(std::move(task))));
  {
    MutexLocker lock(thread_state_mutex_);
    if (GetIsolate() && thread_state_ != ThreadState::kReadyToShutdown)
      inspector_task_runner_->InterruptAndRunAllTasksDontWait(GetIsolate());
  }
  GetWorkerBackingThread().BackingThread().PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(&WorkerThread::PerformDebuggerTaskDontWaitOnWorkerThread,
                      CrossThreadUnretained(this)));
}

void WorkerThread::StartRunningDebuggerTasksOnPauseOnWorkerThread() {
  DCHECK(IsCurrentThread());
  if (worker_inspector_controller_)
    worker_inspector_controller_->FlushProtocolNotifications();
  paused_in_debugger_ = true;
  ThreadDebugger::IdleStarted(GetIsolate());
  std::unique_ptr<CrossThreadClosure> task;
  do {
    task =
        inspector_task_runner_->TakeNextTask(InspectorTaskRunner::kWaitForTask);
    if (task)
      (*task)();
    // Keep waiting until execution is resumed.
  } while (task && paused_in_debugger_);
  ThreadDebugger::IdleFinished(GetIsolate());
}

void WorkerThread::StopRunningDebuggerTasksOnPauseOnWorkerThread() {
  DCHECK(IsCurrentThread());
  paused_in_debugger_ = false;
}

WorkerOrWorkletGlobalScope* WorkerThread::GlobalScope() {
  DCHECK(IsCurrentThread());
  return global_scope_.Get();
}

WorkerInspectorController* WorkerThread::GetWorkerInspectorController() {
  DCHECK(IsCurrentThread());
  return worker_inspector_controller_.Get();
}

unsigned WorkerThread::WorkerThreadCount() {
  MutexLocker lock(ThreadSetMutex());
  return WorkerThreads().size();
}

HashSet<WorkerThread*>& WorkerThread::WorkerThreads() {
  DCHECK(IsMainThread());
  DEFINE_STATIC_LOCAL(HashSet<WorkerThread*>, threads, ());
  return threads;
}

PlatformThreadId WorkerThread::GetPlatformThreadId() {
  if (!requested_to_start_)
    return 0;
  return GetWorkerBackingThread().BackingThread().PlatformThread().ThreadId();
}

bool WorkerThread::IsForciblyTerminated() {
  MutexLocker lock(thread_state_mutex_);
  switch (exit_code_) {
    case ExitCode::kNotTerminated:
    case ExitCode::kGracefullyTerminated:
      return false;
    case ExitCode::kSyncForciblyTerminated:
    case ExitCode::kAsyncForciblyTerminated:
      return true;
    case ExitCode::kLastEnum:
      NOTREACHED() << static_cast<int>(exit_code_);
      return false;
  }
  NOTREACHED() << static_cast<int>(exit_code_);
  return false;
}

WorkerThread::WorkerThread(PassRefPtr<WorkerLoaderProxy> worker_loader_proxy,
                           WorkerReportingProxy& worker_reporting_proxy)
    : worker_thread_id_(GetNextWorkerThreadId()),
      forcible_termination_delay_in_ms_(kForcibleTerminationDelayInMs),
      inspector_task_runner_(WTF::MakeUnique<InspectorTaskRunner>()),
      worker_loader_proxy_(std::move(worker_loader_proxy)),
      worker_reporting_proxy_(worker_reporting_proxy),
      shutdown_event_(WTF::WrapUnique(
          new WaitableEvent(WaitableEvent::ResetPolicy::kManual,
                            WaitableEvent::InitialState::kNonSignaled))),
      worker_thread_lifecycle_context_(new WorkerThreadLifecycleContext) {
  DCHECK(IsMainThread());
  MutexLocker lock(ThreadSetMutex());
  WorkerThreads().insert(this);
}

void WorkerThread::TerminateInternal(TerminationMode mode) {
  DCHECK(IsMainThread());
  DCHECK(requested_to_start_);

  {
    // Protect against this method, initializeOnWorkerThread() or
    // termination via the global scope racing each other.
    MutexLocker lock(thread_state_mutex_);

    // If terminate has already been called.
    if (requested_to_terminate_) {
      if (running_debugger_task_) {
        // Any debugger task is guaranteed to finish, so we can wait
        // for the completion even if the synchronous forcible
        // termination is requested. Shutdown sequence will start
        // after the task.
        DCHECK(!forcible_termination_task_handle_.IsActive());
        return;
      }

      // The synchronous forcible termination request should overtake the
      // scheduled termination task because the request will block the
      // main thread and the scheduled termination task never runs.
      if (mode == TerminationMode::kForcible &&
          exit_code_ == ExitCode::kNotTerminated) {
        DCHECK(forcible_termination_task_handle_.IsActive());
        ForciblyTerminateExecution(lock, ExitCode::kSyncForciblyTerminated);
      }
      return;
    }
    requested_to_terminate_ = true;

    if (ShouldScheduleToTerminateExecution(lock)) {
      switch (mode) {
        case TerminationMode::kForcible:
          ForciblyTerminateExecution(lock, ExitCode::kSyncForciblyTerminated);
          break;
        case TerminationMode::kGraceful:
          DCHECK(!forcible_termination_task_handle_.IsActive());
          forcible_termination_task_handle_ =
              parent_frame_task_runners_->Get(TaskType::kUnspecedTimer)
                  ->PostDelayedCancellableTask(
                      BLINK_FROM_HERE,
                      WTF::Bind(&WorkerThread::MayForciblyTerminateExecution,
                                WTF::Unretained(this)),
                      forcible_termination_delay_in_ms_);
          break;
      }
    }
  }

  worker_thread_lifecycle_context_->NotifyContextDestroyed();
  inspector_task_runner_->Kill();

  GetWorkerBackingThread().BackingThread().PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(&WorkerThread::PrepareForShutdownOnWorkerThread,
                      CrossThreadUnretained(this)));
  GetWorkerBackingThread().BackingThread().PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(&WorkerThread::PerformShutdownOnWorkerThread,
                      CrossThreadUnretained(this)));
}

bool WorkerThread::ShouldScheduleToTerminateExecution(const MutexLocker& lock) {
  DCHECK(IsMainThread());
  DCHECK(IsThreadStateMutexLocked(lock));

  switch (thread_state_) {
    case ThreadState::kNotStarted:
      // Shutdown sequence will surely start during initialization sequence
      // on the worker thread. Don't have to schedule a termination task.
      return false;
    case ThreadState::kRunning:
      // Terminating during debugger task may lead to crash due to heavy use
      // of v8 api in debugger. Any debugger task is guaranteed to finish, so
      // we can wait for the completion.
      return !running_debugger_task_;
    case ThreadState::kReadyToShutdown:
      // Shutdown sequence will surely start soon. Don't have to schedule a
      // termination task.
      return false;
  }
  NOTREACHED();
  return false;
}

void WorkerThread::MayForciblyTerminateExecution() {
  DCHECK(IsMainThread());
  MutexLocker lock(thread_state_mutex_);
  if (thread_state_ == ThreadState::kReadyToShutdown) {
    // Shutdown sequence is now running. Just return.
    return;
  }
  if (running_debugger_task_) {
    // Any debugger task is guaranteed to finish, so we can wait for the
    // completion. Shutdown sequence will start after that.
    return;
  }

  ForciblyTerminateExecution(lock, ExitCode::kAsyncForciblyTerminated);
}

void WorkerThread::ForciblyTerminateExecution(const MutexLocker& lock,
                                              ExitCode exit_code) {
  DCHECK(IsMainThread());
  DCHECK(IsThreadStateMutexLocked(lock));

  DCHECK(exit_code == ExitCode::kSyncForciblyTerminated ||
         exit_code == ExitCode::kAsyncForciblyTerminated);
  SetExitCode(lock, exit_code);

  GetIsolate()->TerminateExecution();
  forcible_termination_task_handle_.Cancel();
}

bool WorkerThread::IsInShutdown() {
  // Check if we've started termination or shutdown sequence. Avoid acquiring
  // a lock here to avoid introducing a risk of deadlock. Note that accessing
  // |m_requestedToTerminate| on the main thread or |m_threadState| on the
  // worker thread is safe as the flag is set only on the thread.
  if (IsMainThread() && requested_to_terminate_)
    return true;
  if (IsCurrentThread() && thread_state_ == ThreadState::kReadyToShutdown)
    return true;
  return false;
}

void WorkerThread::InitializeOnWorkerThread(
    std::unique_ptr<WorkerThreadStartupData> startup_data) {
  DCHECK(IsCurrentThread());
  DCHECK_EQ(ThreadState::kNotStarted, thread_state_);

  KURL script_url = startup_data->script_url_;
  String source_code = startup_data->source_code_;
  WorkerThreadStartMode start_mode = startup_data->start_mode_;
  std::unique_ptr<Vector<char>> cached_meta_data =
      std::move(startup_data->cached_meta_data_);
  V8CacheOptions v8_cache_options =
      startup_data->worker_v8_settings_.v8_cache_options_;
  bool heap_limit_increased_for_debugging =
      startup_data->worker_v8_settings_.heap_limit_mode_ ==
      WorkerV8Settings::HeapLimitMode::kIncreasedForDebugging;

  {
    MutexLocker lock(thread_state_mutex_);

    if (IsOwningBackingThread())
      GetWorkerBackingThread().Initialize();
    GetWorkerBackingThread().BackingThread().AddTaskObserver(this);

    // Optimize for memory usage instead of latency for the worker isolate.
    GetIsolate()->IsolateInBackgroundNotification();

    if (heap_limit_increased_for_debugging) {
      GetIsolate()->IncreaseHeapLimitForDebugging();
    }

    console_message_storage_ = new ConsoleMessageStorage();
    global_scope_ = CreateWorkerGlobalScope(std::move(startup_data));
    worker_reporting_proxy_.DidCreateWorkerGlobalScope(GlobalScope());
    worker_inspector_controller_ = WorkerInspectorController::Create(this);

    // TODO(nhiroki): Handle a case where the script controller fails to
    // initialize the context.
    if (GlobalScope()->ScriptController()->InitializeContextIfNeeded()) {
      worker_reporting_proxy_.DidInitializeWorkerContext();
      v8::HandleScope handle_scope(GetIsolate());
      Platform::Current()->WorkerContextCreated(
          GlobalScope()->ScriptController()->GetContext());
    }

    SetThreadState(lock, ThreadState::kRunning);
  }

  if (start_mode == kPauseWorkerGlobalScopeOnStart)
    StartRunningDebuggerTasksOnPauseOnWorkerThread();

  if (CheckRequestedToTerminateOnWorkerThread()) {
    // Stop further worker tasks from running after this point. WorkerThread
    // was requested to terminate before initialization or during running
    // debugger tasks. performShutdownOnWorkerThread() will be called soon.
    PrepareForShutdownOnWorkerThread();
    return;
  }

  if (GlobalScope()->IsWorkerGlobalScope()) {
    WorkerGlobalScope* worker_global_scope = ToWorkerGlobalScope(GlobalScope());
    CachedMetadataHandler* handler =
        worker_global_scope->CreateWorkerScriptCachedMetadataHandler(
            script_url, cached_meta_data.get());
    worker_reporting_proxy_.WillEvaluateWorkerScript(
        source_code.length(),
        cached_meta_data.get() ? cached_meta_data->size() : 0);
    bool success = worker_global_scope->ScriptController()->Evaluate(
        ScriptSourceCode(source_code, script_url), nullptr, handler,
        v8_cache_options);
    worker_reporting_proxy_.DidEvaluateWorkerScript(success);
  }
}

void WorkerThread::PrepareForShutdownOnWorkerThread() {
  DCHECK(IsCurrentThread());
  {
    MutexLocker lock(thread_state_mutex_);
    if (thread_state_ == ThreadState::kReadyToShutdown)
      return;
    SetThreadState(lock, ThreadState::kReadyToShutdown);
    if (exit_code_ == ExitCode::kNotTerminated)
      SetExitCode(lock, ExitCode::kGracefullyTerminated);
  }

  inspector_task_runner_->Kill();
  GetWorkerReportingProxy().WillDestroyWorkerGlobalScope();
  probe::AllAsyncTasksCanceled(GlobalScope());

  GlobalScope()->NotifyContextDestroyed();
  if (worker_inspector_controller_) {
    worker_inspector_controller_->Dispose();
    worker_inspector_controller_.Clear();
  }
  GlobalScope()->Dispose();
  console_message_storage_.Clear();
  GetWorkerBackingThread().BackingThread().RemoveTaskObserver(this);
}

void WorkerThread::PerformShutdownOnWorkerThread() {
  DCHECK(IsCurrentThread());
  DCHECK(CheckRequestedToTerminateOnWorkerThread());
  DCHECK_EQ(ThreadState::kReadyToShutdown, thread_state_);

  // The below assignment will destroy the context, which will in turn notify
  // messaging proxy. We cannot let any objects survive past thread exit,
  // because no other thread will run GC or otherwise destroy them. If Oilpan
  // is enabled, we detach of the context/global scope, with the final heap
  // cleanup below sweeping it out.
  global_scope_ = nullptr;

  if (IsOwningBackingThread())
    GetWorkerBackingThread().Shutdown();
  // We must not touch workerBackingThread() from now on.

  // Notify the proxy that the WorkerOrWorkletGlobalScope has been disposed
  // of. This can free this thread object, hence it must not be touched
  // afterwards.
  GetWorkerReportingProxy().DidTerminateWorkerThread();

  shutdown_event_->Signal();
}

template <WTF::FunctionThreadAffinity threadAffinity>
void WorkerThread::PerformTaskOnWorkerThread(
    std::unique_ptr<Function<void(), threadAffinity>> task) {
  DCHECK(IsCurrentThread());
  if (thread_state_ != ThreadState::kRunning)
    return;

  {
    DEFINE_THREAD_SAFE_STATIC_LOCAL(
        CustomCountHistogram, scoped_us_counter,
        new CustomCountHistogram("WorkerThread.Task.Time", 0, 10000000, 50));
    ScopedUsHistogramTimer timer(scoped_us_counter);
    (*task)();
  }
}

void WorkerThread::PerformDebuggerTaskOnWorkerThread(
    std::unique_ptr<CrossThreadClosure> task) {
  DCHECK(IsCurrentThread());
  InspectorTaskRunner::IgnoreInterruptsScope scope(
      inspector_task_runner_.get());
  {
    MutexLocker lock(thread_state_mutex_);
    DCHECK_EQ(ThreadState::kRunning, thread_state_);
    running_debugger_task_ = true;
  }
  ThreadDebugger::IdleFinished(GetIsolate());
  {
    DEFINE_THREAD_SAFE_STATIC_LOCAL(
        CustomCountHistogram, scoped_us_counter,
        new CustomCountHistogram("WorkerThread.DebuggerTask.Time", 0, 10000000,
                                 50));
    ScopedUsHistogramTimer timer(scoped_us_counter);
    (*task)();
  }
  ThreadDebugger::IdleStarted(GetIsolate());
  {
    MutexLocker lock(thread_state_mutex_);
    running_debugger_task_ = false;
    if (!requested_to_terminate_)
      return;
    // termiante() was called while a debugger task is running. Shutdown
    // sequence will start soon.
  }
  // Stop further debugger tasks from running after this point.
  inspector_task_runner_->Kill();
}

void WorkerThread::PerformDebuggerTaskDontWaitOnWorkerThread() {
  DCHECK(IsCurrentThread());
  std::unique_ptr<CrossThreadClosure> task =
      inspector_task_runner_->TakeNextTask(
          InspectorTaskRunner::kDontWaitForTask);
  if (task)
    (*task)();
}

void WorkerThread::SetThreadState(const MutexLocker& lock,
                                  ThreadState next_thread_state) {
  DCHECK(IsThreadStateMutexLocked(lock));
  switch (next_thread_state) {
    case ThreadState::kNotStarted:
      NOTREACHED();
      return;
    case ThreadState::kRunning:
      DCHECK_EQ(ThreadState::kNotStarted, thread_state_);
      thread_state_ = next_thread_state;
      return;
    case ThreadState::kReadyToShutdown:
      DCHECK_EQ(ThreadState::kRunning, thread_state_);
      thread_state_ = next_thread_state;
      return;
  }
}

void WorkerThread::SetExitCode(const MutexLocker& lock, ExitCode exit_code) {
  DCHECK(IsThreadStateMutexLocked(lock));
  DCHECK_EQ(ExitCode::kNotTerminated, exit_code_);
  exit_code_ = exit_code;
}

bool WorkerThread::IsThreadStateMutexLocked(const MutexLocker& /* unused */) {
#if DCHECK_IS_ON()
  // Mutex::locked() is available only if DCHECK_IS_ON() is true.
  return thread_state_mutex_.Locked();
#else
  // Otherwise, believe the given MutexLocker holds |m_threadStateMutex|.
  return true;
#endif
}

bool WorkerThread::CheckRequestedToTerminateOnWorkerThread() {
  MutexLocker lock(thread_state_mutex_);
  return requested_to_terminate_;
}

ExitCode WorkerThread::GetExitCodeForTesting() {
  MutexLocker lock(thread_state_mutex_);
  return exit_code_;
}

}  // namespace blink
