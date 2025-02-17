// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/V8ScriptRunner.h"

#include "bindings/core/v8/V8Binding.h"
#include "bindings/core/v8/V8BindingForTesting.h"
#include "core/loader/resource/ScriptResource.h"
#include "platform/heap/Handle.h"
#include "platform/loader/fetch/CachedMetadata.h"
#include "platform/loader/fetch/CachedMetadataHandler.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

class V8ScriptRunnerTest : public ::testing::Test {
 public:
  V8ScriptRunnerTest() {}
  ~V8ScriptRunnerTest() override {}

  void SetUp() override {
    // To trick various layers of caching, increment a counter for each
    // test and use it in code(), fielname() and url().
    counter_++;
  }

  void TearDown() override {
    resource_request_ = ResourceRequest();
    resource_.Clear();
  }

  WTF::String Code() const {
    // Simple function for testing. Note:
    // - Add counter to trick V8 code cache.
    // - Pad counter to 1000 digits, to trick minimal cacheability threshold.
    return WTF::String::Format("a = function() { 1 + 1; } // %01000d\n",
                               counter_);
  }
  WTF::String Filename() const {
    return WTF::String::Format("whatever%d.js", counter_);
  }
  WTF::String Url() const {
    return WTF::String::Format("http://bla.com/bla%d", counter_);
  }
  unsigned TagForParserCache(CachedMetadataHandler* cache_handler) const {
    return V8ScriptRunner::TagForParserCache(cache_handler);
  }
  unsigned TagForCodeCache(CachedMetadataHandler* cache_handler) const {
    return V8ScriptRunner::TagForCodeCache(cache_handler);
  }
  void SetCacheTimeStamp(CachedMetadataHandler* cache_handler) {
    V8ScriptRunner::SetCacheTimeStamp(cache_handler);
  }

  bool CompileScript(v8::Isolate* isolate, V8CacheOptions cache_options) {
    return !V8ScriptRunner::CompileScript(
                V8String(isolate, Code()), Filename(), String(),
                WTF::TextPosition(), isolate, resource_.Get(), nullptr,
                resource_.Get() ? resource_->CacheHandler() : nullptr,
                kNotSharableCrossOrigin, cache_options)
                .IsEmpty();
  }

  void SetEmptyResource() {
    resource_request_ = ResourceRequest();
    resource_ = ScriptResource::Create(resource_request_, "UTF-8");
  }

  void SetResource() {
    resource_request_ = ResourceRequest(Url());
    resource_ = ScriptResource::Create(resource_request_, "UTF-8");
  }

  CachedMetadataHandler* CacheHandler() { return resource_->CacheHandler(); }

 protected:
  ResourceRequest resource_request_;
  Persistent<ScriptResource> resource_;

  static int counter_;
};

int V8ScriptRunnerTest::counter_ = 0;

TEST_F(V8ScriptRunnerTest, resourcelessShouldPass) {
  V8TestingScope scope;
  EXPECT_TRUE(CompileScript(scope.GetIsolate(), kV8CacheOptionsNone));
  EXPECT_TRUE(CompileScript(scope.GetIsolate(), kV8CacheOptionsParse));
  EXPECT_TRUE(CompileScript(scope.GetIsolate(), kV8CacheOptionsCode));
}

TEST_F(V8ScriptRunnerTest, emptyResourceDoesNotHaveCacheHandler) {
  SetEmptyResource();
  EXPECT_FALSE(CacheHandler());
}

TEST_F(V8ScriptRunnerTest, parseOption) {
  V8TestingScope scope;
  SetResource();
  EXPECT_TRUE(CompileScript(scope.GetIsolate(), kV8CacheOptionsParse));
  EXPECT_TRUE(
      CacheHandler()->GetCachedMetadata(TagForParserCache(CacheHandler())));
  EXPECT_FALSE(
      CacheHandler()->GetCachedMetadata(TagForCodeCache(CacheHandler())));
  // The cached data is associated with the encoding.
  ResourceRequest request(Url());
  ScriptResource* another_resource = ScriptResource::Create(request, "UTF-16");
  EXPECT_FALSE(CacheHandler()->GetCachedMetadata(
      TagForParserCache(another_resource->CacheHandler())));
}

TEST_F(V8ScriptRunnerTest, codeOption) {
  V8TestingScope scope;
  SetResource();
  SetCacheTimeStamp(CacheHandler());

  EXPECT_TRUE(CompileScript(scope.GetIsolate(), kV8CacheOptionsCode));

  EXPECT_FALSE(
      CacheHandler()->GetCachedMetadata(TagForParserCache(CacheHandler())));
  EXPECT_TRUE(
      CacheHandler()->GetCachedMetadata(TagForCodeCache(CacheHandler())));
  // The cached data is associated with the encoding.
  ResourceRequest request(Url());
  ScriptResource* another_resource = ScriptResource::Create(request, "UTF-16");
  EXPECT_FALSE(CacheHandler()->GetCachedMetadata(
      TagForCodeCache(another_resource->CacheHandler())));
}

}  // namespace

}  // namespace blink
