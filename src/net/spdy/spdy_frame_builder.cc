// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_frame_builder.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "base/logging.h"
#include "net/spdy/spdy_bug_tracker.h"
#include "net/spdy/spdy_framer.h"
#include "net/spdy/spdy_protocol.h"
#include "net/spdy/zero_copy_output_buffer.h"

namespace net {

SpdyFrameBuilder::SpdyFrameBuilder(size_t size)
    : buffer_(new char[size]), capacity_(size), length_(0), offset_(0) {}

SpdyFrameBuilder::SpdyFrameBuilder(size_t size, ZeroCopyOutputBuffer* output)
    : buffer_(output == nullptr ? new char[size] : nullptr),
      output_(output),
      capacity_(size),
      length_(0),
      offset_(0) {}

SpdyFrameBuilder::~SpdyFrameBuilder() {
}

char* SpdyFrameBuilder::GetWritableBuffer(size_t length) {
  if (!CanWrite(length)) {
    return nullptr;
  }
  return buffer_.get() + offset_ + length_;
}

char* SpdyFrameBuilder::GetWritableOutput(size_t length,
                                          size_t* actual_length) {
  char* dest = nullptr;
  int size = 0;

  if (!CanWrite(length)) {
    return nullptr;
  }
  output_->Next(&dest, &size);
  *actual_length = std::min(length, (size_t)size);
  return dest;
}

bool SpdyFrameBuilder::Seek(size_t length) {
  if (!CanWrite(length)) {
    return false;
  }
  if (output_ == nullptr) {
    length_ += length;
  } else {
    output_->AdvanceWritePtr(length);
    length_ += length;
  }
  return true;
}

bool SpdyFrameBuilder::BeginNewFrame(const SpdyFramer& framer,
                                     SpdyFrameType type,
                                     uint8_t flags,
                                     SpdyStreamId stream_id) {
  uint8_t raw_frame_type = SerializeFrameType(type);
  DCHECK(IsDefinedFrameType(raw_frame_type));
  DCHECK_EQ(0u, stream_id & ~kStreamIdMask);
  bool success = true;
  if (length_ > 0) {
    // Update length field for previous frame.
    OverwriteLength(framer, length_ - kFrameHeaderSize);
    SPDY_BUG_IF(framer.GetFrameMaximumSize() < length_)
        << "Frame length  " << length_
        << " is longer than the maximum allowed length.";
  }

  offset_ += length_;
  length_ = 0;

  // TODO(yasong): remove after OverwriteLength() is deleted.
  bool length_written = false;
  // Remember where the length field is written. Used for OverwriteLength().
  if (output_ != nullptr && CanWrite(kLengthFieldLength)) {
    // Can write the length field.
    char* dest = nullptr;
    // |size| is the available bytes in the current memory block.
    int size = 0;
    output_->Next(&dest, &size);
    start_of_current_frame_ = dest;
    bytes_of_length_written_in_first_block_ =
        size > (int)kLengthFieldLength ? kLengthFieldLength : size;
    // If the current block is not enough for the length field, write the
    // length field here, and remember the pointer to the next block.
    if (size < (int)kLengthFieldLength) {
      // Write the first portion of the length field.
      int value = base::HostToNet32(capacity_ - offset_ - kFrameHeaderSize);
      memcpy(dest, reinterpret_cast<char*>(&value) + 1, size);
      Seek(size);
      output_->Next(&dest, &size);
      start_of_current_frame_in_next_block_ = dest;
      int size_left =
          kLengthFieldLength - bytes_of_length_written_in_first_block_;
      memcpy(dest, reinterpret_cast<char*>(&value) + 1 + size, size_left);
      Seek(size_left);
      length_written = true;
    }
  }

  // Assume all remaining capacity will be used for this frame. If not,
  // the length will get overwritten when we begin the next frame.
  // Don't check for length limits here because this may be larger than the
  // actual frame length.
  if (!length_written) {
    success &= WriteUInt24(capacity_ - offset_ - kFrameHeaderSize);
  }
  success &= WriteUInt8(raw_frame_type);
  success &= WriteUInt8(flags);
  success &= WriteUInt32(stream_id);
  DCHECK_EQ(framer.GetDataFrameMinimumSize(), length_);
  return success;
}

bool SpdyFrameBuilder::BeginNewFrame(const SpdyFramer& framer,
                                     SpdyFrameType type,
                                     uint8_t flags,
                                     SpdyStreamId stream_id,
                                     size_t length) {
  uint8_t raw_frame_type = SerializeFrameType(type);
  DCHECK(IsDefinedFrameType(raw_frame_type));
  return BeginNewFrameInternal(framer, raw_frame_type, flags, stream_id,
                               length);
}

bool SpdyFrameBuilder::BeginNewExtensionFrame(const SpdyFramer& framer,
                                              uint8_t raw_frame_type,
                                              uint8_t flags,
                                              SpdyStreamId stream_id,
                                              size_t length) {
  DCHECK(!IsDefinedFrameType(raw_frame_type));
  return BeginNewFrameInternal(framer, raw_frame_type, flags, stream_id,
                               length);
}

bool SpdyFrameBuilder::BeginNewFrameInternal(const SpdyFramer& framer,
                                             uint8_t raw_frame_type,
                                             uint8_t flags,
                                             SpdyStreamId stream_id,
                                             size_t length) {
  DCHECK_EQ(0u, stream_id & ~kStreamIdMask);
  bool success = true;
  SPDY_BUG_IF(framer.GetFrameMaximumSize() < length_)
      << "Frame length  " << length_
      << " is longer than the maximum allowed length.";

  offset_ += length_;
  length_ = 0;

  success &= WriteUInt24(length);
  success &= WriteUInt8(raw_frame_type);
  success &= WriteUInt8(flags);
  success &= WriteUInt32(stream_id);
  DCHECK_EQ(framer.GetDataFrameMinimumSize(), length_);
  return success;
}

bool SpdyFrameBuilder::WriteStringPiece16(const SpdyStringPiece& value) {
  if (value.size() > 0xffff) {
    DCHECK(false) << "Tried to write string with length > 16bit.";
    return false;
  }

  if (!WriteUInt16(static_cast<uint16_t>(value.size()))) {
    return false;
  }

  return WriteBytes(value.data(), static_cast<uint16_t>(value.size()));
}

bool SpdyFrameBuilder::WriteStringPiece32(const SpdyStringPiece& value) {
  if (!WriteUInt32(value.size())) {
    return false;
  }

  return WriteBytes(value.data(), value.size());
}

bool SpdyFrameBuilder::WriteBytes(const void* data, uint32_t data_len) {
  if (!CanWrite(data_len)) {
    return false;
  }

  if (output_ == nullptr) {
    char* dest = GetWritableBuffer(data_len);
    memcpy(dest, data, data_len);
    Seek(data_len);
  } else {
    char* dest = nullptr;
    size_t size = 0;
    size_t total_written = 0;
    const char* data_ptr = reinterpret_cast<const char*>(data);
    while (data_len > 0) {
      dest = GetWritableOutput(data_len, &size);
      if (dest == nullptr || size == 0) {
        // Unable to make progress.
        return false;
      }
      uint32_t to_copy = std::min<uint32_t>(data_len, size);
      const char* src = data_ptr + total_written;
      memcpy(dest, src, to_copy);
      Seek(to_copy);
      data_len -= to_copy;
      total_written += to_copy;
    }
  }
  return true;
}

bool SpdyFrameBuilder::OverwriteLength(const SpdyFramer& framer,
                                       size_t length) {
  if (output_ != nullptr) {
    size_t value = base::HostToNet32(length);
    if (start_of_current_frame_ != nullptr &&
        bytes_of_length_written_in_first_block_ == kLengthFieldLength) {
      // Length field of the current frame is within one memory block.
      memcpy(start_of_current_frame_, reinterpret_cast<char*>(&value) + 1,
             kLengthFieldLength);
      return true;
    } else if (start_of_current_frame_ != nullptr &&
               start_of_current_frame_in_next_block_ != nullptr &&
               bytes_of_length_written_in_first_block_ < kLengthFieldLength) {
      // Length field of the current frame crosses two memory blocks.
      memcpy(start_of_current_frame_, reinterpret_cast<char*>(&value) + 1,
             bytes_of_length_written_in_first_block_);
      memcpy(start_of_current_frame_in_next_block_,
             reinterpret_cast<char*>(&value) + 1 +
                 bytes_of_length_written_in_first_block_,
             kLengthFieldLength - bytes_of_length_written_in_first_block_);
      return true;
    } else {
      return false;
    }
  }

  DCHECK_GE(framer.GetFrameMaximumSize(), length);
  bool success = false;
  const size_t old_length = length_;

  length_ = 0;
  success = WriteUInt24(length);

  length_ = old_length;
  return success;
}

bool SpdyFrameBuilder::CanWrite(size_t length) const {
  if (length > kLengthMask) {
    DCHECK(false);
    return false;
  }

  if (output_ == nullptr) {
    if (offset_ + length_ + length > capacity_) {
      DLOG(FATAL) << "Requested: " << length << " capacity: " << capacity_
                  << " used: " << offset_ + length_;
      return false;
    }
  } else {
    if (length > output_->BytesFree()) {
      return false;
    }
  }

  return true;
}

}  // namespace net
