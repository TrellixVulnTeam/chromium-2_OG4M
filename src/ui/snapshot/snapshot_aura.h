// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_SNAPSHOT_SNAPSHOT_AURA_H_
#define UI_SNAPSHOT_SNAPSHOT_AURA_H_

#include "ui/snapshot/snapshot.h"

namespace ui {

// These functions are identical to those in snapshot.h, except they're
// guaranteed to read the frame using an Aura CopyOutputRequest and not the
// native windowing system. source_rect and target_size are in DIP.

SNAPSHOT_EXPORT void GrabWindowSnapshotAndScaleAsyncAura(
    aura::Window* window,
    const gfx::Rect& source_rect,
    const gfx::Size& target_size,
    scoped_refptr<base::TaskRunner> background_task_runner,
    const GrabWindowSnapshotAsyncCallback& callback);

SNAPSHOT_EXPORT void GrabWindowSnapshotAsyncAura(
    aura::Window* window,
    const gfx::Rect& source_rect,
    const GrabWindowSnapshotAsyncCallback& callback);

}  // namespace ui

#endif  // UI_SNAPSHOT_SNAPSHOT_AURA_H_
