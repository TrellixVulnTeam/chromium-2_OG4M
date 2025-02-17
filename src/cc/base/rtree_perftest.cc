// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/base/lap_timer.h"
#include "cc/base/rtree.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace cc {
namespace {

static const int kTimeLimitMillis = 2000;
static const int kWarmupRuns = 5;
static const int kTimeCheckInterval = 10;

class RTreePerfTest : public testing::Test {
 public:
  RTreePerfTest()
      : timer_(kWarmupRuns,
               base::TimeDelta::FromMilliseconds(kTimeLimitMillis),
               kTimeCheckInterval) {}

  void RunConstructTest(const std::string& test_name, int rect_count) {
    std::vector<gfx::Rect> rects = BuildRects(rect_count);
    timer_.Reset();
    do {
      RTree rtree;
      rtree.Build(rects);
      timer_.NextLap();
    } while (!timer_.HasTimeLimitExpired());

    perf_test::PrintResult("rtree_construct", "", test_name,
                           timer_.LapsPerSecond(), "runs/s", true);
  }

  void RunSearchTest(const std::string& test_name, int rect_count) {
    int large_query = std::sqrt(rect_count);

    std::vector<gfx::Rect> queries = {
        gfx::Rect(0, 0, 1, 1), gfx::Rect(100, 100, 2, 2),
        gfx::Rect(-10, -10, 1, 1), gfx::Rect(0, 0, 1000, 1000),
        gfx::Rect(large_query - 2, large_query - 2, 1, 1)};
    size_t query_index = 0;

    std::vector<gfx::Rect> rects = BuildRects(rect_count);
    RTree rtree;
    rtree.Build(rects);

    timer_.Reset();
    do {
      std::vector<size_t> results;
      rtree.Search(queries[query_index], &results);
      query_index = (query_index + 1) % queries.size();
      timer_.NextLap();
    } while (!timer_.HasTimeLimitExpired());

    perf_test::PrintResult("rtree_search", "", test_name,
                           timer_.LapsPerSecond(), "runs/s", true);
  }

  std::vector<gfx::Rect> BuildRects(int count) {
    std::vector<gfx::Rect> result;
    int width = std::sqrt(count);
    int x = 0;
    int y = 0;
    for (int i = 0; i < count; ++i) {
      result.push_back(gfx::Rect(x, y, 1, 1));
      if (++x > width) {
        x = 0;
        ++y;
      }
    }
    return result;
  }

 protected:
  LapTimer timer_;
};

TEST_F(RTreePerfTest, Construct) {
  RunConstructTest("100", 100);
  RunConstructTest("1000", 1000);
  RunConstructTest("10000", 10000);
  RunConstructTest("100000", 100000);
}

TEST_F(RTreePerfTest, Search) {
  RunSearchTest("100", 100);
  RunSearchTest("1000", 1000);
  RunSearchTest("10000", 10000);
  RunSearchTest("100000", 100000);
}

}  // namespace
}  // namespace cc
