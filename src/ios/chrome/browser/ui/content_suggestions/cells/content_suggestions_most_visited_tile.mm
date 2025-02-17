// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/content_suggestions/cells/content_suggestions_most_visited_tile.h"

#import "ios/chrome/browser/ui/favicon/favicon_attributes.h"
#import "ios/chrome/browser/ui/favicon/favicon_view.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#import "ios/third_party/material_components_ios/src/components/Typography/src/MaterialTypography.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
const CGFloat kLabelTextColor = 0.314;
const NSInteger kLabelNumLines = 2;
const CGFloat kFaviconSize = 48;
const CGFloat kSpaceFaviconTitle = 10;

// Size of the tile.
const CGFloat kWidth = 73;
const CGFloat kHeight = 100;
}

@interface ContentSuggestionsMostVisitedTile ()

@property(nonatomic, strong) UILabel* titleLabel;

@end

@implementation ContentSuggestionsMostVisitedTile

@synthesize titleLabel = _titleLabel;
@synthesize faviconView = _faviconView;

+ (CGFloat)width {
  return kWidth;
}

+ (ContentSuggestionsMostVisitedTile*)tileWithTitle:(NSString*)title
                                         attributes:
                                             (FaviconAttributes*)attributes {
  ContentSuggestionsMostVisitedTile* tile =
      [[ContentSuggestionsMostVisitedTile alloc] init];
  [tile.faviconView configureWithAttributes:attributes];
  [tile setTitle:title];
  return tile;
}

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _titleLabel = [[UILabel alloc] init];
    _titleLabel.textColor = [UIColor colorWithWhite:kLabelTextColor alpha:1.0];
    _titleLabel.font = [MDCTypography captionFont];
    _titleLabel.textAlignment = NSTextAlignmentCenter;
    _titleLabel.preferredMaxLayoutWidth = [[self class] width];
    _titleLabel.numberOfLines = kLabelNumLines;

    _faviconView = [[FaviconViewNew alloc] init];
    _faviconView.font = [MDCTypography headlineFont];

    _titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _faviconView.translatesAutoresizingMaskIntoConstraints = NO;

    [self addSubview:_titleLabel];
    [self addSubview:_faviconView];

    [NSLayoutConstraint activateConstraints:@[
      [_faviconView.widthAnchor constraintEqualToConstant:kFaviconSize],
      [_faviconView.heightAnchor
          constraintEqualToAnchor:_faviconView.widthAnchor],
      [_faviconView.centerXAnchor
          constraintEqualToAnchor:_titleLabel.centerXAnchor],
    ]];

    ApplyVisualConstraintsWithMetrics(
        @[ @"V:|[favicon]-(space)-[title]", @"H:|[title]|" ],
        @{ @"favicon" : _faviconView,
           @"title" : _titleLabel },
        @{ @"space" : @(kSpaceFaviconTitle) });

    self.isAccessibilityElement = YES;
  }
  return self;
}

- (void)setTitle:(NSString*)title {
  self.titleLabel.text = title;
  self.accessibilityLabel = title;
}

- (CGSize)intrinsicContentSize {
  return CGSizeMake(kWidth, kHeight);
}

@end
