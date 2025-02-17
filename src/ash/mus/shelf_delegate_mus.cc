// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mus/shelf_delegate_mus.h"

#include "ash/shelf/shelf_controller.h"
#include "ash/shell.h"
#include "base/strings/string_util.h"

namespace ash {

ShelfDelegateMus::ShelfDelegateMus() {}

ShelfDelegateMus::~ShelfDelegateMus() {}

///////////////////////////////////////////////////////////////////////////////
// ShelfDelegate:

ShelfID ShelfDelegateMus::GetShelfIDForAppID(const std::string& app_id) {
  if (Shell::Get()->shelf_controller()->app_id_to_shelf_id().count(app_id))
    return Shell::Get()->shelf_controller()->app_id_to_shelf_id().at(app_id);
  return 0;
}

ShelfID ShelfDelegateMus::GetShelfIDForAppIDAndLaunchID(
    const std::string& app_id,
    const std::string& launch_id) {
  return ShelfDelegateMus::GetShelfIDForAppID(app_id);
}

const std::string& ShelfDelegateMus::GetAppIDForShelfID(ShelfID id) {
  if (Shell::Get()->shelf_controller()->shelf_id_to_app_id().count(id))
    return Shell::Get()->shelf_controller()->shelf_id_to_app_id().at(id);
  return base::EmptyString();
}

void ShelfDelegateMus::PinAppWithID(const std::string& app_id) {
  NOTIMPLEMENTED();
}

bool ShelfDelegateMus::IsAppPinned(const std::string& app_id) {
  NOTIMPLEMENTED();
  return false;
}

void ShelfDelegateMus::UnpinAppWithID(const std::string& app_id) {
  NOTIMPLEMENTED();
}

}  // namespace ash
