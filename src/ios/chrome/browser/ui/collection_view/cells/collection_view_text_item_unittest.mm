// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_item.h"

#import <CoreGraphics/CoreGraphics.h>
#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/collection_view/cells/collection_view_text_cell.h"
#import "ios/chrome/browser/ui/collection_view/cells/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

TEST(CollectionViewTextItemTest, ConfigureCellPortsAccessoryType) {
  CollectionViewTextItem* item =
      [[CollectionViewTextItem alloc] initWithType:0];
  item.accessoryType = MDCCollectionViewCellAccessoryCheckmark;
  CollectionViewTextCell* cell = [[[item cellClass] alloc] init];
  EXPECT_TRUE([cell isMemberOfClass:[CollectionViewTextCell class]]);
  EXPECT_EQ(MDCCollectionViewCellAccessoryNone, [cell accessoryType]);
  [item configureCell:cell];
  EXPECT_EQ(MDCCollectionViewCellAccessoryCheckmark, [cell accessoryType]);
}

TEST(CollectionViewTextItemTest, ConfigureCellPortsTextCellProperties) {
  CollectionViewTextItem* item =
      [[CollectionViewTextItem alloc] initWithType:0];
  item.text = @"some text";
  item.detailText = @"some detail text";
  CollectionViewTextCell* cell = [[[item cellClass] alloc] init];
  EXPECT_TRUE([cell isMemberOfClass:[CollectionViewTextCell class]]);
  EXPECT_FALSE([cell textLabel].text);
  EXPECT_FALSE([cell detailTextLabel].text);
  [item configureCell:cell];
  EXPECT_NSEQ(@"some text", [cell textLabel].text);
  EXPECT_NSEQ(@"some detail text", [cell detailTextLabel].text);
}

}  // namespace
