// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPRESSOR_ARCHIVE_MINIZIP_H_
#define COMPRESSOR_ARCHIVE_MINIZIP_H_

#include <string>

#include "third_party/zlib/contrib/minizip/unzip.h"
#include "third_party/zlib/contrib/minizip/zip.h"

#include "compressor_archive.h"
#include "compressor_stream.h"

// A namespace with constants used by CompressorArchiveMinizip.
namespace compressor_archive_constants {

const char kCreateArchiveError[] = "Failed to create archive.";
const char kAddToArchiveError[] = "Failed to add entry to archive.";
const char kCloseArchiveError[] = "Failed to close archive.";

}

// A name space with custom functions passed to minizip.
namespace compressor_archive_functions {

  uLong CustomArchiveWrite(void* compressor,
                           void* stream,
                           const void* buffer,
                           uLong length);

  long CustomArchiveTell(void* compressor, void* stream);

  long CustomArchiveSeek(void* compressor,
                         void* stream,
                         uLong offset,
                         int origin);

}  // compressor_archive_functions

class CompressorArchiveMinizip : public CompressorArchive {
 public:
  explicit CompressorArchiveMinizip(CompressorStream* compressor_stream);

  virtual ~CompressorArchiveMinizip();

  // Creates an archive object.
  virtual bool CreateArchive();

  // Releases all resources obtained by minizip.
  virtual bool CloseArchive(bool has_error);

  // Adds an entry to the archive.
  virtual bool AddToArchive(const std::string& filename,
                            int64_t file_size,
                            int64_t modification_time,
                            bool is_directory);

  // A getter function for zip_file_.
  zipFile zip_file() const { return zip_file_; }

  // A getter function for compressor_stream.
  CompressorStream* compressor_stream() const { return compressor_stream_; }

  // Custom functions need to access private variables of
  // CompressorArchiveMinizip frequently.
  friend uLong compressor_archive_functions::CustomArchiveWrite(
      void* compressor, void* stream, const void* buffer, uLong length);

  friend long compressor_archive_functions::CustomArchiveTell(
      void* compressor, void* stream);

  friend long compressor_archive_functions::CustomArchiveSeek(
      void* compressor, void* stream, uLong offset, int origin);

 private:
  // An instance that takes care of all IO operations.
  CompressorStream* compressor_stream_;

  // The minizip correspondent archive object.
  zipFile zip_file_;

  // The buffer used to store the data read from JavaScript.
  char* destination_buffer_;

  // The current offset of the zip archive file.
  int64_t offset_;
  // The size of the zip archive file.
  int64_t length_;
};

#endif  // COMPRESSOR_ARCHIVE_MINIZIP_H_
