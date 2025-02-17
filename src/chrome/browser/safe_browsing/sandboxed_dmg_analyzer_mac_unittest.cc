// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/sandboxed_dmg_analyzer_mac.h"

#include <mach-o/loader.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/safe_browsing/zip_analyzer_results.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace safe_browsing {
namespace {

class SandboxedDMGAnalyzerTest : public testing::Test {
 public:
  SandboxedDMGAnalyzerTest()
      : browser_thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP) {
  }

  void AnalyzeFile(const base::FilePath& path,
                   zip_analyzer::Results* results) {
    base::RunLoop run_loop;
    ResultsGetter results_getter(run_loop.QuitClosure(), results);
    scoped_refptr<SandboxedDMGAnalyzer> analyzer(
        new SandboxedDMGAnalyzer(path, results_getter.GetCallback()));
    analyzer->Start();
    run_loop.Run();
  }

  base::FilePath GetFilePath(const char* file_name) {
    base::FilePath test_data;
    EXPECT_TRUE(PathService::Get(chrome::DIR_GEN_TEST_DATA, &test_data));
    return test_data.AppendASCII("chrome")
                    .AppendASCII("safe_browsing_dmg")
                    .AppendASCII(file_name);
  }

 private:
  // A helper that provides a SandboxedDMGAnalyzer::ResultCallback that will
  // store a copy of an analyzer's results and then run a closure.
  class ResultsGetter {
   public:
    ResultsGetter(const base::Closure& next_closure,
                  zip_analyzer::Results* results)
        : next_closure_(next_closure), results_(results) {}

    SandboxedDMGAnalyzer::ResultCallback GetCallback() {
      return base::Bind(&ResultsGetter::ResultsCallback,
                        base::Unretained(this));
    }

   private:
    void ResultsCallback(const zip_analyzer::Results& results) {
      *results_ = results;
      next_closure_.Run();
    }

    base::Closure next_closure_;
    zip_analyzer::Results* results_;

    DISALLOW_COPY_AND_ASSIGN(ResultsGetter);
  };

  content::TestBrowserThreadBundle browser_thread_bundle_;
  content::InProcessUtilityThreadHelper utility_thread_helper_;
};

TEST_F(SandboxedDMGAnalyzerTest, AnalyzeDMG) {
  base::FilePath path;
  ASSERT_NO_FATAL_FAILURE(path = GetFilePath("mach_o_in_dmg.dmg"));

  zip_analyzer::Results results;
  AnalyzeFile(path, &results);

  EXPECT_TRUE(results.success);
  EXPECT_TRUE(results.has_executable);
  EXPECT_EQ(2, results.archived_binary.size());

  bool got_executable = false, got_dylib = false;
  for (const auto& binary : results.archived_binary) {
    const std::string& file_name = binary.file_basename();
    const google::protobuf::RepeatedPtrField<
        ClientDownloadRequest_MachOHeaders>& headers =
            binary.image_headers().mach_o_headers();

    EXPECT_EQ(ClientDownloadRequest_DownloadType_MAC_EXECUTABLE,
              binary.download_type());

    if (file_name.find("executablefat") != std::string::npos) {
      got_executable = true;
      ASSERT_EQ(2, headers.size());

      const ClientDownloadRequest_MachOHeaders& arch32 = headers.Get(0);
      EXPECT_EQ(15, arch32.load_commands().size());
      EXPECT_EQ(MH_MAGIC,
          *reinterpret_cast<const uint32_t*>(arch32.mach_header().c_str()));

      const ClientDownloadRequest_MachOHeaders& arch64 = headers.Get(1);
      EXPECT_EQ(15, arch64.load_commands().size());
      EXPECT_EQ(MH_MAGIC_64,
          *reinterpret_cast<const uint32_t*>(arch64.mach_header().c_str()));

      const std::string& sha256_bytes = binary.digests().sha256();
      std::string actual_sha256 = base::HexEncode(sha256_bytes.c_str(),
                                                  sha256_bytes.size());
      EXPECT_EQ(
          "E462FF752FF9D84E34D843E5D46E2012ADCBD48540A8473FB794B286A389B945",
          actual_sha256);
    } else if (file_name.find("lib64.dylib") != std::string::npos) {
      got_dylib = true;
      ASSERT_EQ(1, headers.size());

      const ClientDownloadRequest_MachOHeaders& arch = headers.Get(0);
      EXPECT_EQ(13, arch.load_commands().size());
      EXPECT_EQ(MH_MAGIC_64,
          *reinterpret_cast<const uint32_t*>(arch.mach_header().c_str()));

      const std::string& sha256_bytes = binary.digests().sha256();
      std::string actual_sha256 = base::HexEncode(sha256_bytes.c_str(),
                                                  sha256_bytes.size());
      EXPECT_EQ(
          "2012CE4987B0FA4A5D285DF7E810560E841CFAB3054BC19E1AAB345F862A6C4E",
          actual_sha256);
    } else {
      ADD_FAILURE() << "Unepxected result file " << binary.file_basename();
    }
  }

  EXPECT_TRUE(got_executable);
  EXPECT_TRUE(got_dylib);
}

}  // namespace
}  // namespace safe_browsing
