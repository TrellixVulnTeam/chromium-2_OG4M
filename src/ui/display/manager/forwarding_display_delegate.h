// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_MANAGER_FORWARDING_DISPLAY_DELEGATE_H_
#define UI_DISPLAY_MANAGER_FORWARDING_DISPLAY_DELEGATE_H_

#include <memory>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "ui/display/manager/display_manager_export.h"
#include "ui/display/mojo/native_display_delegate.mojom.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/display/types/native_display_observer.h"

namespace display {

class DisplaySnapshotMojo;

// NativeDisplayDelegate implementation that forwards calls to a real
// NativeDisplayDelegate in another process. Only forwards the methods
// implemented by Ozone DRM, other method won't do anything.
class DISPLAY_MANAGER_EXPORT ForwardingDisplayDelegate
    : public NativeDisplayDelegate,
      public NON_EXPORTED_BASE(mojom::NativeDisplayObserver) {
 public:
  explicit ForwardingDisplayDelegate(mojom::NativeDisplayDelegatePtr delegate);
  ~ForwardingDisplayDelegate() override;

  // display::NativeDisplayDelegate:
  void Initialize() override;
  void GrabServer() override;
  void UngrabServer() override;
  void TakeDisplayControl(const DisplayControlCallback& callback) override;
  void RelinquishDisplayControl(
      const DisplayControlCallback& callback) override;
  void SyncWithServer() override;
  void SetBackgroundColor(uint32_t color_argb) override;
  void ForceDPMSOn() override;
  void GetDisplays(const GetDisplaysCallback& callback) override;
  void AddMode(const DisplaySnapshot& output, const DisplayMode* mode) override;
  void Configure(const DisplaySnapshot& output,
                 const DisplayMode* mode,
                 const gfx::Point& origin,
                 const ConfigureCallback& callback) override;
  void CreateFrameBuffer(const gfx::Size& size) override;
  void GetHDCPState(const DisplaySnapshot& output,
                    const GetHDCPStateCallback& callback) override;
  void SetHDCPState(const DisplaySnapshot& output,
                    HDCPState state,
                    const SetHDCPStateCallback& callback) override;
  std::vector<ColorCalibrationProfile> GetAvailableColorCalibrationProfiles(
      const DisplaySnapshot& output) override;
  bool SetColorCalibrationProfile(const DisplaySnapshot& output,
                                  ColorCalibrationProfile new_profile) override;
  bool SetColorCorrection(const DisplaySnapshot& output,
                          const std::vector<GammaRampRGBEntry>& degamma_lut,
                          const std::vector<GammaRampRGBEntry>& gamma_lut,
                          const std::vector<float>& correction_matrix) override;
  void AddObserver(display::NativeDisplayObserver* observer) override;
  void RemoveObserver(display::NativeDisplayObserver* observer) override;
  FakeDisplayController* GetFakeDisplayController() override;

  // display::mojom::NativeDisplayObserver:
  void OnConfigurationChanged() override;

 private:
  // Stores display snapshots and forards them to |callback|.
  void StoreAndForwardDisplays(
      const GetDisplaysCallback& callback,
      std::vector<std::unique_ptr<DisplaySnapshotMojo>> snapshots);

  mojom::NativeDisplayDelegatePtr delegate_;
  mojo::Binding<mojom::NativeDisplayObserver> binding_;

  // Display snapshots are owned here but accessed via raw pointers elsewhere.
  // Call OnDisplaySnapshotsInvalidated() on observers before invalidating them.
  std::vector<std::unique_ptr<DisplaySnapshotMojo>> snapshots_;

  base::ObserverList<display::NativeDisplayObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(ForwardingDisplayDelegate);
};

}  // namespace display

#endif  // UI_DISPLAY_MANAGER_FORWARDING_DISPLAY_DELEGATE_H_
