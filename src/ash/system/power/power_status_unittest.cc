// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/power/power_status.h"

#include <memory>

#include "ash/resources/vector_icons/vector_icons.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using power_manager::PowerSupplyProperties;

namespace ash {
namespace {

class TestObserver : public PowerStatus::Observer {
 public:
  TestObserver() : power_changed_count_(0) {}
  ~TestObserver() override {}

  int power_changed_count() const { return power_changed_count_; }

  // PowerStatus::Observer overrides:
  void OnPowerStatusChanged() override { ++power_changed_count_; }

 private:
  int power_changed_count_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

}  // namespace

class PowerStatusTest : public testing::Test {
 public:
  PowerStatusTest() : power_status_(NULL) {}
  ~PowerStatusTest() override {}

  void SetUp() override {
    chromeos::DBusThreadManager::Initialize();
    PowerStatus::Initialize();
    power_status_ = PowerStatus::Get();
    test_observer_.reset(new TestObserver);
    power_status_->AddObserver(test_observer_.get());
  }

  void TearDown() override {
    power_status_->RemoveObserver(test_observer_.get());
    test_observer_.reset();
    PowerStatus::Shutdown();
    chromeos::DBusThreadManager::Shutdown();
  }

 protected:
  base::MessageLoopForUI message_loop_;
  PowerStatus* power_status_;  // Not owned.
  std::unique_ptr<TestObserver> test_observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PowerStatusTest);
};

TEST_F(PowerStatusTest, InitializeAndUpdate) {
  // Test that the initial power supply state should be acquired after
  // PowerStatus is instantiated. This depends on
  // PowerManagerClientStubImpl, which responds to power status update
  // requests, pretends there is a battery present, and generates some valid
  // power supply status data.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, test_observer_->power_changed_count());

  // Test RequestUpdate, test_obsever_ should be notified for power suuply
  // status change.
  power_status_->RequestStatusUpdate();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2, test_observer_->power_changed_count());
}

TEST_F(PowerStatusTest, ShouldDisplayBatteryTime) {
  EXPECT_FALSE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(-1)));
  EXPECT_FALSE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(0)));
  EXPECT_FALSE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(59)));
  EXPECT_TRUE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(60)));
  EXPECT_TRUE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(600)));
  EXPECT_TRUE(PowerStatus::ShouldDisplayBatteryTime(
      base::TimeDelta::FromSeconds(3600)));
  EXPECT_TRUE(PowerStatus::ShouldDisplayBatteryTime(
      base::TimeDelta::FromSeconds(PowerStatus::kMaxBatteryTimeToDisplaySec)));
  EXPECT_FALSE(
      PowerStatus::ShouldDisplayBatteryTime(base::TimeDelta::FromSeconds(
          PowerStatus::kMaxBatteryTimeToDisplaySec + 1)));
}

TEST_F(PowerStatusTest, SplitTimeIntoHoursAndMinutes) {
  int hours = 0, minutes = 0;
  PowerStatus::SplitTimeIntoHoursAndMinutes(base::TimeDelta::FromSeconds(0),
                                            &hours, &minutes);
  EXPECT_EQ(0, hours);
  EXPECT_EQ(0, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(base::TimeDelta::FromSeconds(60),
                                            &hours, &minutes);
  EXPECT_EQ(0, hours);
  EXPECT_EQ(1, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(base::TimeDelta::FromSeconds(3600),
                                            &hours, &minutes);
  EXPECT_EQ(1, hours);
  EXPECT_EQ(0, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSeconds(3600 + 60), &hours, &minutes);
  EXPECT_EQ(1, hours);
  EXPECT_EQ(1, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSeconds(7 * 3600 + 23 * 60), &hours, &minutes);
  EXPECT_EQ(7, hours);
  EXPECT_EQ(23, minutes);

  // Check that minutes are rounded.
  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSeconds(2 * 3600 + 3 * 60 + 30), &hours, &minutes);
  EXPECT_EQ(2, hours);
  EXPECT_EQ(4, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSeconds(2 * 3600 + 3 * 60 + 29), &hours, &minutes);
  EXPECT_EQ(2, hours);
  EXPECT_EQ(3, minutes);

  // Check that times close to hour boundaries aren't incorrectly rounded such
  // that they display 60 minutes: http://crbug.com/368261
  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSecondsD(3599.9), &hours, &minutes);
  EXPECT_EQ(1, hours);
  EXPECT_EQ(0, minutes);

  PowerStatus::SplitTimeIntoHoursAndMinutes(
      base::TimeDelta::FromSecondsD(3600.1), &hours, &minutes);
  EXPECT_EQ(1, hours);
  EXPECT_EQ(0, minutes);
}

TEST_F(PowerStatusTest, GetBatteryImageInfo) {
  PowerSupplyProperties prop;
  prop.set_external_power(PowerSupplyProperties::AC);
  prop.set_battery_state(PowerSupplyProperties::CHARGING);
  prop.set_battery_percent(98.0);
  power_status_->SetProtoForTesting(prop);
  const PowerStatus::BatteryImageInfo info_charging_98 =
      power_status_->GetBatteryImageInfo();

  // 99% should use the same icon as 98%.
  prop.set_battery_percent(99.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(info_charging_98, power_status_->GetBatteryImageInfo());

  // A different icon should be used when the battery is full.
  prop.set_battery_state(PowerSupplyProperties::FULL);
  prop.set_battery_percent(100.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_NE(info_charging_98, power_status_->GetBatteryImageInfo());

  // A much-lower battery level should use a different icon.
  prop.set_battery_state(PowerSupplyProperties::CHARGING);
  prop.set_battery_percent(20.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_NE(info_charging_98, power_status_->GetBatteryImageInfo());

  // Ditto for 98%, but on USB instead of AC.
  prop.set_external_power(PowerSupplyProperties::USB);
  prop.set_battery_percent(98.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_NE(info_charging_98, power_status_->GetBatteryImageInfo());
}

// Tests that the |icon_badge| member of BatteryImageInfo is set correctly
// with various power supply property values.
TEST_F(PowerStatusTest, BatteryImageInfoIconBadge) {
  PowerSupplyProperties prop;

  // A charging battery connected to AC power should have a bolt badge.
  prop.set_external_power(PowerSupplyProperties::AC);
  prop.set_battery_state(PowerSupplyProperties::CHARGING);
  prop.set_battery_percent(98.0);
  power_status_->SetProtoForTesting(prop);
  const gfx::VectorIcon* bolt_icon =
      power_status_->GetBatteryImageInfo().icon_badge;
  EXPECT_TRUE(bolt_icon);

  // A discharging battery connected to AC should also have a bolt badge.
  prop.set_battery_state(PowerSupplyProperties::DISCHARGING);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(bolt_icon, power_status_->GetBatteryImageInfo().icon_badge);

  // A charging battery connected to USB power should have an
  // unreliable badge.
  prop.set_external_power(PowerSupplyProperties::USB);
  prop.set_battery_state(PowerSupplyProperties::CHARGING);
  power_status_->SetProtoForTesting(prop);
  const gfx::VectorIcon* unreliable_icon =
      power_status_->GetBatteryImageInfo().icon_badge;
  EXPECT_NE(unreliable_icon, bolt_icon);
  EXPECT_TRUE(unreliable_icon);

  // A discharging battery connected to USB power should also have an
  // unreliable badge.
  prop.set_battery_state(PowerSupplyProperties::DISCHARGING);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(unreliable_icon, power_status_->GetBatteryImageInfo().icon_badge);

  // Show the right icon when no battery is present.
  prop.set_external_power(PowerSupplyProperties::DISCONNECTED);
  prop.set_battery_state(PowerSupplyProperties::NOT_PRESENT);
  power_status_->SetProtoForTesting(prop);
  const gfx::VectorIcon* x_icon =
      power_status_->GetBatteryImageInfo().icon_badge;
  EXPECT_TRUE(x_icon);
  EXPECT_NE(bolt_icon, x_icon);
  EXPECT_NE(unreliable_icon, x_icon);

  // Do not show a badge when the battery is discharging.
  prop.set_battery_state(PowerSupplyProperties::DISCHARGING);
  power_status_->SetProtoForTesting(prop);
  EXPECT_FALSE(power_status_->GetBatteryImageInfo().icon_badge);

  // Show the right icon for a discharging battery when it falls below
  // a charge level of PowerStatus::kCriticalBatteryChargePercentage.
  prop.set_battery_percent(PowerStatus::kCriticalBatteryChargePercentage);
  power_status_->SetProtoForTesting(prop);
  EXPECT_FALSE(power_status_->GetBatteryImageInfo().icon_badge);
  prop.set_battery_percent(PowerStatus::kCriticalBatteryChargePercentage - 1);
  power_status_->SetProtoForTesting(prop);
  const gfx::VectorIcon* alert_icon =
      power_status_->GetBatteryImageInfo().icon_badge;
  EXPECT_TRUE(alert_icon);
  EXPECT_NE(bolt_icon, alert_icon);
  EXPECT_NE(unreliable_icon, alert_icon);
  EXPECT_NE(x_icon, alert_icon);
}

// Tests that the |charge_level| member of BatteryImageInfo is set correctly
// with various power supply property values.
TEST_F(PowerStatusTest, BatteryImageInfoChargeLevel) {
  PowerSupplyProperties prop;

  // No charge level is drawn when the battery is not present.
  prop.set_external_power(PowerSupplyProperties::DISCONNECTED);
  prop.set_battery_state(PowerSupplyProperties::NOT_PRESENT);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(0, power_status_->GetBatteryImageInfo().charge_level);

  // A charge level of 0 when the battery is 0% full.
  prop.set_external_power(PowerSupplyProperties::AC);
  prop.set_battery_state(PowerSupplyProperties::CHARGING);
  prop.set_battery_percent(0.0);
  EXPECT_EQ(0, power_status_->GetBatteryImageInfo().charge_level);

  // A charge level of 1 when the battery is up to 16% full, and a level of 2
  // for 17% full.
  prop.set_battery_percent(16.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(1, power_status_->GetBatteryImageInfo().charge_level);
  prop.set_battery_percent(17.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(2, power_status_->GetBatteryImageInfo().charge_level);

  // A charge level of 6 when the battery is 50% full.
  prop.set_battery_percent(50.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(6, power_status_->GetBatteryImageInfo().charge_level);

  // A charge level of 11 when the battery is 99% full, and a level of 12 when
  // the battery is 100% full.
  prop.set_battery_percent(99.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(11, power_status_->GetBatteryImageInfo().charge_level);
  prop.set_battery_percent(100.0);
  power_status_->SetProtoForTesting(prop);
  EXPECT_EQ(12, power_status_->GetBatteryImageInfo().charge_level);
}

}  // namespace ash
