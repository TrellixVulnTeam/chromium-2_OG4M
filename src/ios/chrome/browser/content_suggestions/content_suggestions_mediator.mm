// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/content_suggestions/content_suggestions_mediator.h"

#include "base/mac/bind_objc_block.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/ntp_snippets/category.h"
#include "components/ntp_snippets/category_info.h"
#include "components/ntp_snippets/content_suggestion.h"
#include "components/ntp_snippets/reading_list/reading_list_distillation_state_util.h"
#include "components/ntp_tiles/most_visited_sites.h"
#include "components/ntp_tiles/ntp_tile.h"
#import "ios/chrome/browser/content_suggestions/content_suggestions_category_wrapper.h"
#import "ios/chrome/browser/content_suggestions/content_suggestions_service_bridge_observer.h"
#import "ios/chrome/browser/content_suggestions/mediator_util.h"
#include "ios/chrome/browser/ntp_tiles/most_visited_sites_observer_bridge.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestion.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_commands.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_data_sink.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_image_fetcher.h"
#import "ios/chrome/browser/ui/content_suggestions/identifier/content_suggestion_identifier.h"
#import "ios/chrome/browser/ui/content_suggestions/identifier/content_suggestions_section_information.h"
#import "ios/chrome/browser/ui/favicon/favicon_attributes_provider.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_collection_view_item.h"
#import "ios/chrome/browser/ui/reading_list/reading_list_utils.h"
#include "ui/gfx/image/image.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// Size of the favicon returned by the provider.
const CGFloat kDefaultFaviconSize = 16;
// Maximum number of most visited tiles fetched.
const NSInteger kMaxNumMostVisitedTiles = 8;

}  // namespace

@interface ContentSuggestionsMediator ()<ContentSuggestionsImageFetcher,
                                         ContentSuggestionsServiceObserver,
                                         MostVisitedSitesObserving> {
  // Bridge for this class to become an observer of a ContentSuggestionsService.
  std::unique_ptr<ContentSuggestionsServiceBridge> _suggestionBridge;
  std::unique_ptr<ntp_tiles::MostVisitedSites> _mostVisitedSites;
  std::unique_ptr<ntp_tiles::MostVisitedSitesObserverBridge> _mostVisitedBridge;
}

// Most visited data from the MostVisitedSites service (copied upon receiving
// the callback).
@property(nonatomic, assign) std::vector<ntp_tiles::NTPTile> mostVisitedData;
// Section Info for the Most Visited section.
@property(nonatomic, strong)
    ContentSuggestionsSectionInformation* mostVisitedSectionInfo;
// Whether the page impression has been recorded.
@property(nonatomic, assign) BOOL recordedPageImpression;
// The ContentSuggestionsService, serving suggestions.
@property(nonatomic, assign)
    ntp_snippets::ContentSuggestionsService* contentService;
// Map the section information created to the relevant category.
@property(nonatomic, strong, nonnull)
    NSMutableDictionary<ContentSuggestionsCategoryWrapper*,
                        ContentSuggestionsSectionInformation*>*
        sectionInformationByCategory;
// FaviconAttributesProvider to fetch the favicon for the suggestions.
@property(nonatomic, nullable, strong)
    FaviconAttributesProvider* attributesProvider;

// Converts the |suggestions| from |category| to ContentSuggestion and adds them
// to the |contentArray|  if the category is available.
- (void)addSuggestions:
            (const std::vector<ntp_snippets::ContentSuggestion>&)suggestions
          fromCategory:(ntp_snippets::Category&)category
               toArray:(NSMutableArray<ContentSuggestion*>*)contentArray;

// Adds the section information for |category| in
// self.sectionInformationByCategory.
- (void)addSectionInformationForCategory:(ntp_snippets::Category)category;

// Returns a CategoryWrapper acting as a key for this section info.
- (ContentSuggestionsCategoryWrapper*)categoryWrapperForSectionInfo:
    (ContentSuggestionsSectionInformation*)sectionInfo;

@end

@implementation ContentSuggestionsMediator

@synthesize mostVisitedData = _mostVisitedData;
@synthesize mostVisitedSectionInfo = _mostVisitedSectionInfo;
@synthesize recordedPageImpression = _recordedPageImpression;
@synthesize contentService = _contentService;
@synthesize dataSink = _dataSink;
@synthesize sectionInformationByCategory = _sectionInformationByCategory;
@synthesize attributesProvider = _attributesProvider;
@synthesize commandHandler = _commandHandler;

#pragma mark - Public

- (instancetype)
initWithContentService:(ntp_snippets::ContentSuggestionsService*)contentService
      largeIconService:(favicon::LargeIconService*)largeIconService
       mostVisitedSite:
           (std::unique_ptr<ntp_tiles::MostVisitedSites>)mostVisitedSites {
  self = [super init];
  if (self) {
    _suggestionBridge =
        base::MakeUnique<ContentSuggestionsServiceBridge>(self, contentService);
    _contentService = contentService;
    _sectionInformationByCategory = [[NSMutableDictionary alloc] init];
    _attributesProvider = [[FaviconAttributesProvider alloc]
        initWithFaviconSize:kDefaultFaviconSize
             minFaviconSize:1
           largeIconService:largeIconService];

    _mostVisitedSectionInfo = MostVisitedSectionInformation();
    _mostVisitedSites = std::move(mostVisitedSites);
    _mostVisitedBridge =
        base::MakeUnique<ntp_tiles::MostVisitedSitesObserverBridge>(self);
    _mostVisitedSites->SetMostVisitedURLsObserver(_mostVisitedBridge.get(),
                                                  kMaxNumMostVisitedTiles);
  }
  return self;
}

- (void)dismissSuggestion:(ContentSuggestionIdentifier*)suggestionIdentifier {
  ContentSuggestionsCategoryWrapper* categoryWrapper =
      [self categoryWrapperForSectionInfo:suggestionIdentifier.sectionInfo];
  ntp_snippets::ContentSuggestion::ID suggestion_id =
      ntp_snippets::ContentSuggestion::ID([categoryWrapper category],
                                          suggestionIdentifier.IDInSection);

  self.contentService->DismissSuggestion(suggestion_id);
}

#pragma mark - ContentSuggestionsDataSource

- (NSArray<ContentSuggestion*>*)allSuggestions {
  NSMutableArray<ContentSuggestion*>* dataHolders = [NSMutableArray array];

  [self addMostVisitedToArray:dataHolders];

  [self addContentSuggestionsToArray:dataHolders];

  return dataHolders;
}

- (NSArray<ContentSuggestion*>*)suggestionsForSection:
    (ContentSuggestionsSectionInformation*)sectionInfo {
  NSMutableArray* convertedSuggestions = [NSMutableArray array];

  if (sectionInfo == self.mostVisitedSectionInfo) {
    [self addMostVisitedToArray:convertedSuggestions];
  } else {
    ntp_snippets::Category category =
        [[self categoryWrapperForSectionInfo:sectionInfo] category];

    const std::vector<ntp_snippets::ContentSuggestion>& suggestions =
        self.contentService->GetSuggestionsForCategory(category);
    [self addSuggestions:suggestions
            fromCategory:category
                 toArray:convertedSuggestions];
  }

  return convertedSuggestions;
}

- (id<ContentSuggestionsImageFetcher>)imageFetcher {
  return self;
}

- (void)fetchMoreSuggestionsKnowing:
            (NSArray<ContentSuggestionIdentifier*>*)knownSuggestions
                    fromSectionInfo:
                        (ContentSuggestionsSectionInformation*)sectionInfo
                           callback:(MoreSuggestionsFetched)callback {
  if (![self isRelatedToContentSuggestionsService:sectionInfo])
    return;

  std::set<std::string> known_suggestion_ids;
  for (ContentSuggestionIdentifier* identifier in knownSuggestions) {
    if (identifier.sectionInfo != sectionInfo)
      continue;
    known_suggestion_ids.insert(identifier.IDInSection);
  }

  ContentSuggestionsCategoryWrapper* wrapper =
      [self categoryWrapperForSectionInfo:sectionInfo];

  base::Optional<ntp_snippets::CategoryInfo> categoryInfo =
      self.contentService->GetCategoryInfo([wrapper category]);

  if (!categoryInfo) {
    return;
  }
  switch (categoryInfo->additional_action()) {
    case ntp_snippets::ContentSuggestionsAdditionalAction::NONE:
      return;

    case ntp_snippets::ContentSuggestionsAdditionalAction::VIEW_ALL:
      if ([wrapper category].IsKnownCategory(
              ntp_snippets::KnownCategories::READING_LIST)) {
        [self.commandHandler openReadingList];
      }
      break;

    case ntp_snippets::ContentSuggestionsAdditionalAction::FETCH: {
      __weak ContentSuggestionsMediator* weakSelf = self;
      ntp_snippets::FetchDoneCallback serviceCallback = base::Bind(
          &BindWrapper,
          base::BindBlockArc(^void(
              ntp_snippets::Status status,
              const std::vector<ntp_snippets::ContentSuggestion>& suggestions) {
            [weakSelf didFetchMoreSuggestions:suggestions
                               withStatusCode:status
                                     callback:callback];
          }));

      self.contentService->Fetch([wrapper category], known_suggestion_ids,
                                 serviceCallback);

      break;
    }
  }
}

- (void)fetchFaviconAttributesForURL:(const GURL&)URL
                          completion:(void (^)(FaviconAttributes*))completion {
  [self.attributesProvider fetchFaviconAttributesForURL:URL
                                             completion:completion];
}

#pragma mark - ContentSuggestionsServiceObserver

- (void)contentSuggestionsService:
            (ntp_snippets::ContentSuggestionsService*)suggestionsService
         newSuggestionsInCategory:(ntp_snippets::Category)category {
  ContentSuggestionsCategoryWrapper* wrapper =
      [ContentSuggestionsCategoryWrapper wrapperWithCategory:category];
  if (!self.sectionInformationByCategory[wrapper]) {
    [self addSectionInformationForCategory:category];
  }
  [self.dataSink
      dataAvailableForSection:self.sectionInformationByCategory[wrapper]];
}

- (void)contentSuggestionsService:
            (ntp_snippets::ContentSuggestionsService*)suggestionsService
                         category:(ntp_snippets::Category)category
                  statusChangedTo:(ntp_snippets::CategoryStatus)status {
  if (!ntp_snippets::IsCategoryStatusInitOrAvailable(status)) {
    // Remove the category from the UI if it is not available.
    ContentSuggestionsCategoryWrapper* wrapper =
        [[ContentSuggestionsCategoryWrapper alloc] initWithCategory:category];
    ContentSuggestionsSectionInformation* sectionInfo =
        self.sectionInformationByCategory[wrapper];

    [self.dataSink clearSection:sectionInfo];
    [self.sectionInformationByCategory removeObjectForKey:wrapper];
  }
}

- (void)contentSuggestionsService:
            (ntp_snippets::ContentSuggestionsService*)suggestionsService
            suggestionInvalidated:
                (const ntp_snippets::ContentSuggestion::ID&)suggestion_id {
  ContentSuggestionsCategoryWrapper* wrapper =
      [[ContentSuggestionsCategoryWrapper alloc]
          initWithCategory:suggestion_id.category()];
  ContentSuggestionIdentifier* suggestionIdentifier =
      [[ContentSuggestionIdentifier alloc] init];
  suggestionIdentifier.IDInSection = suggestion_id.id_within_category();
  suggestionIdentifier.sectionInfo = self.sectionInformationByCategory[wrapper];

  [self.dataSink clearSuggestion:suggestionIdentifier];
}

- (void)contentSuggestionsServiceFullRefreshRequired:
    (ntp_snippets::ContentSuggestionsService*)suggestionsService {
  [self.dataSink reloadAllData];
}

- (void)contentSuggestionsServiceShutdown:
    (ntp_snippets::ContentSuggestionsService*)suggestionsService {
  // Update dataSink.
}

#pragma mark - ContentSuggestionsImageFetcher

- (void)fetchImageForSuggestion:
            (ContentSuggestionIdentifier*)suggestionIdentifier
                       callback:(void (^)(UIImage*))callback {
  self.contentService->FetchSuggestionImage(
      SuggestionIDForSectionID(
          [self categoryWrapperForSectionInfo:suggestionIdentifier.sectionInfo],
          suggestionIdentifier.IDInSection),
      base::BindBlockArc(^(const gfx::Image& image) {
        if (image.IsEmpty() || !callback) {
          return;
        }

        callback([image.ToUIImage() copy]);
      }));
}

#pragma mark - MostVisitedSitesObserving

- (void)onMostVisitedURLsAvailable:
    (const ntp_tiles::NTPTilesVector&)mostVisited {
  self.mostVisitedData = mostVisited;
  [self.dataSink reloadSection:self.mostVisitedSectionInfo];

  if (mostVisited.size() && !self.recordedPageImpression) {
    self.recordedPageImpression = YES;
    RecordPageImpression(mostVisited);
  }
}

- (void)onIconMadeAvailable:(const GURL&)siteURL {
  [self.dataSink faviconAvailableForURL:siteURL];
}

#pragma mark - Private

- (void)addSuggestions:
            (const std::vector<ntp_snippets::ContentSuggestion>&)suggestions
          fromCategory:(ntp_snippets::Category&)category
               toArray:(NSMutableArray<ContentSuggestion*>*)contentArray {
  if (!ntp_snippets::IsCategoryStatusAvailable(
          self.contentService->GetCategoryStatus(category))) {
    return;
  }

  ContentSuggestionsCategoryWrapper* categoryWrapper =
      [ContentSuggestionsCategoryWrapper wrapperWithCategory:category];
  if (!self.sectionInformationByCategory[categoryWrapper]) {
    [self addSectionInformationForCategory:category];
  }

  for (auto& contentSuggestion : suggestions) {
    ContentSuggestion* suggestion = ConvertContentSuggestion(contentSuggestion);

    suggestion.type = TypeForCategory(category);

    suggestion.suggestionIdentifier.sectionInfo =
        self.sectionInformationByCategory[categoryWrapper];

    if (category.IsKnownCategory(ntp_snippets::KnownCategories::READING_LIST)) {
      ReadingListUIDistillationStatus status =
          reading_list::UIStatusFromModelStatus(
              ReadingListStateFromSuggestionState(
                  contentSuggestion.reading_list_suggestion_extra()
                      ->distilled_state));
      suggestion.readingListExtra = [ContentSuggestionReadingListExtra
          extraWithDistillationStatus:status];
    }

    [contentArray addObject:suggestion];
  }

  if (suggestions.size() == 0) {
    ContentSuggestion* suggestion = EmptySuggestion();
    suggestion.suggestionIdentifier.sectionInfo =
        self.sectionInformationByCategory[categoryWrapper];

    [contentArray addObject:suggestion];
  }
}

- (void)addSectionInformationForCategory:(ntp_snippets::Category)category {
  base::Optional<ntp_snippets::CategoryInfo> categoryInfo =
      self.contentService->GetCategoryInfo(category);

  ContentSuggestionsSectionInformation* sectionInfo =
      SectionInformationFromCategoryInfo(categoryInfo, category);

  self.sectionInformationByCategory[[ContentSuggestionsCategoryWrapper
      wrapperWithCategory:category]] = sectionInfo;
}

- (ContentSuggestionsCategoryWrapper*)categoryWrapperForSectionInfo:
    (ContentSuggestionsSectionInformation*)sectionInfo {
  return [[self.sectionInformationByCategory allKeysForObject:sectionInfo]
      firstObject];
}

// If the |statusCode| is a success and |suggestions| is not empty, runs the
// |callback| with the |suggestions| converted to Objective-C.
- (void)didFetchMoreSuggestions:
            (const std::vector<ntp_snippets::ContentSuggestion>&)suggestions
                 withStatusCode:(ntp_snippets::Status)statusCode
                       callback:(MoreSuggestionsFetched)callback {
  if (statusCode.IsSuccess() && !suggestions.empty() && callback) {
    NSMutableArray<ContentSuggestion*>* contentSuggestions =
        [NSMutableArray array];
    ntp_snippets::Category category = suggestions[0].id().category();
    [self addSuggestions:suggestions
            fromCategory:category
                 toArray:contentSuggestions];
    callback(contentSuggestions);
  }
}

// Adds all the suggestions from the |contentService| to |suggestions|.
- (void)addContentSuggestionsToArray:
    (NSMutableArray<ContentSuggestion*>*)arrayToFill {
  std::vector<ntp_snippets::Category> categories =
      self.contentService->GetCategories();

  for (auto& category : categories) {
    const std::vector<ntp_snippets::ContentSuggestion>& suggestions =
        self.contentService->GetSuggestionsForCategory(category);
    [self addSuggestions:suggestions fromCategory:category toArray:arrayToFill];
  }
}

// Adds all the suggestions for the |mostVisitedData| to |suggestions|.
- (void)addMostVisitedToArray:(NSMutableArray<ContentSuggestion*>*)arrayToFill {
  if (self.mostVisitedData.empty()) {
    ContentSuggestion* suggestion = EmptySuggestion();
    suggestion.suggestionIdentifier.sectionInfo = self.mostVisitedSectionInfo;
    [arrayToFill addObject:suggestion];

    return;
  }

  for (const ntp_tiles::NTPTile& tile : self.mostVisitedData) {
    ContentSuggestion* suggestion = ConvertNTPTile(tile);
    suggestion.suggestionIdentifier.sectionInfo = self.mostVisitedSectionInfo;
    [arrayToFill addObject:suggestion];
  }
}

// Returns whether the |sectionInfo| is associated with a category from the
// content suggestions service.
- (BOOL)isRelatedToContentSuggestionsService:
    (ContentSuggestionsSectionInformation*)sectionInfo {
  return sectionInfo != self.mostVisitedSectionInfo;
}

@end
