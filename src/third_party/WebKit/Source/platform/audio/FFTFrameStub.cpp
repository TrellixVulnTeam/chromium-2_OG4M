/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// FFTFrame stub implementation to avoid link errors during bringup

#include "platform/wtf/build_config.h"

#if !OS(MACOSX) && !USE(WEBAUDIO_FFMPEG) && !USE(WEBAUDIO_OPENMAX_DL_FFT)

#include "platform/audio/FFTFrame.h"

namespace blink {

// Normal constructor: allocates for a given fftSize.
FFTFrame::FFTFrame(unsigned /*fftSize*/) : m_FFTSize(0), m_log2FFTSize(0) {
  NOTREACHED();
}

// Creates a blank/empty frame (interpolate() must later be called).
FFTFrame::FFTFrame() : m_FFTSize(0), m_log2FFTSize(0) {
  NOTREACHED();
}

// Copy constructor.
FFTFrame::FFTFrame(const FFTFrame& frame)
    : m_FFTSize(frame.m_FFTSize), m_log2FFTSize(frame.m_log2FFTSize) {
  NOTREACHED();
}

FFTFrame::~FFTFrame() {
  NOTREACHED();
}

void FFTFrame::doFFT(const float* data) {
  NOTREACHED();
}

void FFTFrame::doInverseFFT(float* data) {
  NOTREACHED();
}

void FFTFrame::initialize() {}

void FFTFrame::cleanup() {
  NOTREACHED();
}

}  // namespace blink

#endif  // !OS(MACOSX) && !USE(WEBAUDIO_FFMPEG) && !USE(WEBAUDIO_OPENMAX_DL_FFT)
