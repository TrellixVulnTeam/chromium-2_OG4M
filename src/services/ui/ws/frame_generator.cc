// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/frame_generator.h"

#include <utility>
#include <vector>

#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_sink.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/render_pass_draw_quad.h"
#include "cc/quads/shared_quad_state.h"
#include "cc/quads/surface_draw_quad.h"

namespace ui {

namespace ws {

FrameGenerator::FrameGenerator(
    std::unique_ptr<cc::CompositorFrameSink> compositor_frame_sink)
    : compositor_frame_sink_(std::move(compositor_frame_sink)) {
  compositor_frame_sink_->BindToClient(this);
}

FrameGenerator::~FrameGenerator() {
  compositor_frame_sink_->DetachFromClient();
}

void FrameGenerator::SetDeviceScaleFactor(float device_scale_factor) {
  if (device_scale_factor_ == device_scale_factor)
    return;
  device_scale_factor_ = device_scale_factor;
  SetNeedsBeginFrame(true);
}

void FrameGenerator::SetHighContrastMode(bool enabled) {
  if (high_contrast_mode_enabled_ == enabled)
    return;

  high_contrast_mode_enabled_ = enabled;
  SetNeedsBeginFrame(true);
}

void FrameGenerator::OnSurfaceCreated(const cc::SurfaceInfo& surface_info) {
  DCHECK(surface_info.is_valid());

  // Only handle embedded surfaces changing here. The display root surface
  // changing is handled immediately after the CompositorFrame is submitted.
  if (surface_info != window_manager_surface_info_) {
    window_manager_surface_info_ = surface_info;
    SetNeedsBeginFrame(true);
  }
}

void FrameGenerator::OnWindowDamaged() {
  SetNeedsBeginFrame(true);
}

void FrameGenerator::OnWindowSizeChanged(const gfx::Size& pixel_size) {
  if (pixel_size_ == pixel_size)
    return;

  pixel_size_ = pixel_size;
  SetNeedsBeginFrame(true);
}

void FrameGenerator::SetBeginFrameSource(cc::BeginFrameSource* source) {
  if (begin_frame_source_ && observing_begin_frames_)
    begin_frame_source_->RemoveObserver(this);

  begin_frame_source_ = source;

  if (begin_frame_source_ && observing_begin_frames_)
    begin_frame_source_->AddObserver(this);
}

void FrameGenerator::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  // Nothing to do here because FrameGenerator CompositorFrames don't reference
  // any resources.
  DCHECK(resources.empty());
}

void FrameGenerator::SetTreeActivationCallback(const base::Closure& callback) {}

void FrameGenerator::DidReceiveCompositorFrameAck() {}

void FrameGenerator::DidLoseCompositorFrameSink() {}

void FrameGenerator::OnDraw(const gfx::Transform& transform,
                            const gfx::Rect& viewport,
                            bool resourceless_software_draw) {}

void FrameGenerator::SetMemoryPolicy(const cc::ManagedMemoryPolicy& policy) {}

void FrameGenerator::SetExternalTilePriorityConstraints(
    const gfx::Rect& viewport_rect,
    const gfx::Transform& transform) {}

void FrameGenerator::OnBeginFrame(const cc::BeginFrameArgs& begin_frame_args) {
  current_begin_frame_ack_ = cc::BeginFrameAck(
      begin_frame_args.source_id, begin_frame_args.sequence_number,
      begin_frame_args.sequence_number, false);
  if (begin_frame_args.type == cc::BeginFrameArgs::MISSED) {
    begin_frame_source_->DidFinishFrame(this, current_begin_frame_ack_);
    return;
  }

  current_begin_frame_ack_.has_damage = true;
  last_begin_frame_args_ = begin_frame_args;

  // TODO(fsamuel): We should add a trace for generating a top level frame.
  cc::CompositorFrame frame(GenerateCompositorFrame());

  compositor_frame_sink_->SubmitCompositorFrame(std::move(frame));

  begin_frame_source_->DidFinishFrame(this, current_begin_frame_ack_);
  SetNeedsBeginFrame(false);
}

const cc::BeginFrameArgs& FrameGenerator::LastUsedBeginFrameArgs() const {
  return last_begin_frame_args_;
}

void FrameGenerator::OnBeginFrameSourcePausedChanged(bool paused) {}

cc::CompositorFrame FrameGenerator::GenerateCompositorFrame() {
  const int render_pass_id = 1;
  const gfx::Rect bounds(pixel_size_);
  std::unique_ptr<cc::RenderPass> render_pass = cc::RenderPass::Create();
  render_pass->SetNew(render_pass_id, bounds, bounds, gfx::Transform());

  DrawWindow(render_pass.get());

  cc::CompositorFrame frame;
  frame.render_pass_list.push_back(std::move(render_pass));
  if (high_contrast_mode_enabled_) {
    std::unique_ptr<cc::RenderPass> invert_pass = cc::RenderPass::Create();
    invert_pass->SetNew(2, bounds, bounds, gfx::Transform());
    cc::SharedQuadState* shared_state =
        invert_pass->CreateAndAppendSharedQuadState();
    gfx::Size scaled_bounds = gfx::ScaleToCeiledSize(
        pixel_size_, window_manager_surface_info_.device_scale_factor(),
        window_manager_surface_info_.device_scale_factor());
    shared_state->SetAll(gfx::Transform(), scaled_bounds, bounds, bounds, false,
                         1.f, SkBlendMode::kSrcOver, 0);
    auto* quad = invert_pass->CreateAndAppendDrawQuad<cc::RenderPassDrawQuad>();
    frame.render_pass_list.back()->filters.Append(
        cc::FilterOperation::CreateInvertFilter(1.f));
    quad->SetNew(
        shared_state, bounds, bounds, render_pass_id, 0 /* mask_resource_id */,
        gfx::RectF() /* mask_uv_rect */, gfx::Size() /* mask_texture_size */,
        gfx::Vector2dF() /* filters_scale */,
        gfx::PointF() /* filters_origin */, gfx::RectF() /* tex_coord_rect */);
    frame.render_pass_list.push_back(std::move(invert_pass));
  }
  frame.metadata.device_scale_factor = device_scale_factor_;
  frame.metadata.begin_frame_ack = current_begin_frame_ack_;

  if (window_manager_surface_info_.is_valid()) {
    frame.metadata.referenced_surfaces.push_back(
        window_manager_surface_info_.id());
  }

  return frame;
}

void FrameGenerator::DrawWindow(cc::RenderPass* pass) {
  DCHECK(window_manager_surface_info_.is_valid());

  const gfx::Rect bounds_at_origin(
      window_manager_surface_info_.size_in_pixels());

  gfx::Transform quad_to_target_transform;
  quad_to_target_transform.Translate(bounds_at_origin.x(),
                                     bounds_at_origin.y());

  cc::SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();

  gfx::Size scaled_bounds = gfx::ScaleToCeiledSize(
      bounds_at_origin.size(),
      window_manager_surface_info_.device_scale_factor(),
      window_manager_surface_info_.device_scale_factor());

  // TODO(fsamuel): These clipping and visible rects are incorrect. They need
  // to be populated from CompositorFrame structs.
  sqs->SetAll(quad_to_target_transform, scaled_bounds /* layer_bounds */,
              bounds_at_origin /* visible_layer_bounds */,
              bounds_at_origin /* clip_rect */, false /* is_clipped */,
              1.0f /* opacity */, SkBlendMode::kSrcOver,
              0 /* sorting-context_id */);
  auto* quad = pass->CreateAndAppendDrawQuad<cc::SurfaceDrawQuad>();
  quad->SetAll(sqs, bounds_at_origin /* rect */, gfx::Rect() /* opaque_rect */,
               bounds_at_origin /* visible_rect */, true /* needs_blending*/,
               window_manager_surface_info_.id(),
               cc::SurfaceDrawQuadType::PRIMARY, nullptr);
}

void FrameGenerator::SetNeedsBeginFrame(bool needs_begin_frame) {
  needs_begin_frame &= window_manager_surface_info_.is_valid();
  if (needs_begin_frame == observing_begin_frames_)
    return;

  observing_begin_frames_ = needs_begin_frame;
  if (needs_begin_frame)
    begin_frame_source_->AddObserver(this);
  else
    begin_frame_source_->RemoveObserver(this);
}

}  // namespace ws

}  // namespace ui
