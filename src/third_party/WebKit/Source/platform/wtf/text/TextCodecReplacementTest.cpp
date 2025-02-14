// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/wtf/text/TextCodecReplacement.h"

#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/TextCodec.h"
#include "platform/wtf/text/TextEncoding.h"
#include "platform/wtf/text/TextEncodingRegistry.h"
#include "platform/wtf/text/WTFString.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <memory>

namespace WTF {

namespace {

// Just one example, others are listed in the codec implementation.
const char* g_replacement_alias = "iso-2022-kr";

TEST(TextCodecReplacement, Aliases) {
  // "replacement" is not a valid alias for itself
  EXPECT_FALSE(TextEncoding("replacement").IsValid());
  EXPECT_FALSE(TextEncoding("rEpLaCeMeNt").IsValid());

  EXPECT_TRUE(TextEncoding(g_replacement_alias).IsValid());
  EXPECT_STREQ("replacement", TextEncoding(g_replacement_alias).GetName());
}

TEST(TextCodecReplacement, DecodesToFFFD) {
  TextEncoding encoding(g_replacement_alias);
  std::unique_ptr<TextCodec> codec(NewTextCodec(encoding));

  bool saw_error = false;
  const char kTestCase[] = "hello world";
  size_t test_case_size = sizeof(kTestCase) - 1;

  const String result =
      codec->Decode(kTestCase, test_case_size, kDataEOF, false, saw_error);
  EXPECT_TRUE(saw_error);
  ASSERT_EQ(1u, result.length());
  EXPECT_EQ(0xFFFDU, result[0]);
}

TEST(TextCodecReplacement, EncodesToUTF8) {
  TextEncoding encoding(g_replacement_alias);
  std::unique_ptr<TextCodec> codec(NewTextCodec(encoding));

  // "Kanji" in Chinese characters.
  const UChar kTestCase[] = {0x6F22, 0x5B57};
  size_t test_case_size = WTF_ARRAY_LENGTH(kTestCase);
  CString result =
      codec->Encode(kTestCase, test_case_size, kQuestionMarksForUnencodables);

  EXPECT_STREQ("\xE6\xBC\xA2\xE5\xAD\x97", result.Data());
}

}  // namespace

}  // namespace WTF
