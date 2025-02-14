// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/stacking_controller.h"

#include "ash/wm/container_finder.h"
#include "ash/wm_window.h"

namespace ash {

////////////////////////////////////////////////////////////////////////////////
// StackingController, public:

StackingController::StackingController() {}

StackingController::~StackingController() {}

////////////////////////////////////////////////////////////////////////////////
// StackingController, aura::client::WindowParentingClient implementation:

aura::Window* StackingController::GetDefaultParent(aura::Window* window,
                                                   const gfx::Rect& bounds) {
  return WmWindow::GetAuraWindow(
      wm::GetDefaultParent(WmWindow::Get(window), bounds));
}

}  // namespace ash
