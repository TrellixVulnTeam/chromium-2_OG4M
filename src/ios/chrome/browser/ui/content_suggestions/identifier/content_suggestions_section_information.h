// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_IDENTIFIER_CONTENT_SUGGESTIONS_SECTION_INFORMATION_H_
#define IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_IDENTIFIER_CONTENT_SUGGESTIONS_SECTION_INFORMATION_H_

#import <Foundation/Foundation.h>

// Layout for the section and its items.
typedef NS_ENUM(NSInteger, ContentSuggestionsSectionLayout) {
  // Follows the card layout.
  ContentSuggestionsSectionLayoutCard,
  // No specific layout.
  ContentSuggestionsSectionLayoutCustom,
};

// This enum is used for ordering the sections and as ID for the section. Make
// all sections in the same collection have different ID.
// When adding a new kind of suggestions, add a new corresponding section. The
// ordering is not persisted between launch, reordering is possible.
typedef NS_ENUM(NSInteger, ContentSuggestionsSectionID) {
  ContentSuggestionsSectionMostVisited = 0,
  ContentSuggestionsSectionArticles,
  ContentSuggestionsSectionReadingList,

  // Do not use this. It will trigger a DCHECK.
  // Do not add value after this one.
  ContentSuggestionsSectionUnknown
};

// Contains the information needed to display the section.
@interface ContentSuggestionsSectionInformation : NSObject

- (instancetype)initWithSectionID:(ContentSuggestionsSectionID)sectionID
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

// Layout to display the content of the section.
@property(nonatomic, assign) ContentSuggestionsSectionLayout layout;
// ID of the section. Used for ordering.
@property(nonatomic, assign, readonly) ContentSuggestionsSectionID sectionID;
// Title for the section.
@property(nonatomic, copy) NSString* title;
// Title of the section's footer. If it is nil, no footer is created.
@property(nonatomic, copy) NSString* footerTitle;
// Text to be displayed when the section is empty.
@property(nonatomic, copy) NSString* emptyText;
// If the section should be shown when empty at loading. If the section becomes
// empty because the user dismissed suggestions, the |emptyText| must be shown
// in both cases.
@property(nonatomic, assign) BOOL showIfEmpty;

@end

#endif  // IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_IDENTIFIER_CONTENT_SUGGESTIONS_SECTION_INFORMATION_H_
