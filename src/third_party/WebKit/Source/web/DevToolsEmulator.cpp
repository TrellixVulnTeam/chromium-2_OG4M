// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/DevToolsEmulator.h"

#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/input/EventHandler.h"
#include "core/page/Page.h"
#include "core/style/ComputedStyle.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/FloatSize.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/IntSize.h"
#include "platform/loader/fetch/MemoryCache.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebLayerTreeView.h"
#include "web/WebInputEventConversion.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebSettingsImpl.h"
#include "web/WebViewImpl.h"

namespace {

static float calculateDeviceScaleAdjustment(int width,
                                            int height,
                                            float deviceScaleFactor) {
  // Chromium on Android uses a device scale adjustment for fonts used in text
  // autosizing for improved legibility. This function computes this adjusted
  // value for text autosizing.
  // For a description of the Android device scale adjustment algorithm, see:
  // chrome/browser/chrome_content_browser_client.cc,
  // GetDeviceScaleAdjustment(...)
  if (!width || !height || !deviceScaleFactor)
    return 1;

  static const float kMinFSM = 1.05f;
  static const int kWidthForMinFSM = 320;
  static const float kMaxFSM = 1.3f;
  static const int kWidthForMaxFSM = 800;

  float minWidth = std::min(width, height) / deviceScaleFactor;
  if (minWidth <= kWidthForMinFSM)
    return kMinFSM;
  if (minWidth >= kWidthForMaxFSM)
    return kMaxFSM;

  // The font scale multiplier varies linearly between kMinFSM and kMaxFSM.
  float ratio = static_cast<float>(minWidth - kWidthForMinFSM) /
                (kWidthForMaxFSM - kWidthForMinFSM);
  return ratio * (kMaxFSM - kMinFSM) + kMinFSM;
}

}  // namespace

namespace blink {

DevToolsEmulator::DevToolsEmulator(WebViewImpl* web_view_impl)
    : web_view_impl_(web_view_impl),
      device_metrics_enabled_(false),
      emulate_mobile_enabled_(false),
      is_overlay_scrollbars_enabled_(false),
      is_orientation_event_enabled_(false),
      is_mobile_layout_theme_enabled_(false),
      original_default_minimum_page_scale_factor_(0),
      original_default_maximum_page_scale_factor_(0),
      embedder_text_autosizing_enabled_(
          web_view_impl->GetPage()->GetSettings().TextAutosizingEnabled()),
      embedder_device_scale_adjustment_(
          web_view_impl->GetPage()->GetSettings().GetDeviceScaleAdjustment()),
      embedder_prefer_compositing_to_lcd_text_enabled_(
          web_view_impl->GetPage()
              ->GetSettings()
              .GetPreferCompositingToLCDTextEnabled()),
      embedder_viewport_style_(
          web_view_impl->GetPage()->GetSettings().GetViewportStyle()),
      embedder_plugins_enabled_(
          web_view_impl->GetPage()->GetSettings().GetPluginsEnabled()),
      embedder_available_pointer_types_(
          web_view_impl->GetPage()->GetSettings().GetAvailablePointerTypes()),
      embedder_primary_pointer_type_(
          web_view_impl->GetPage()->GetSettings().GetPrimaryPointerType()),
      embedder_available_hover_types_(
          web_view_impl->GetPage()->GetSettings().GetAvailableHoverTypes()),
      embedder_primary_hover_type_(
          web_view_impl->GetPage()->GetSettings().GetPrimaryHoverType()),
      embedder_main_frame_resizes_are_orientation_changes_(
          web_view_impl->GetPage()
              ->GetSettings()
              .GetMainFrameResizesAreOrientationChanges()),
      touch_event_emulation_enabled_(false),
      double_tap_to_zoom_enabled_(false),
      original_touch_event_feature_detection_enabled_(false),
      original_device_supports_touch_(false),
      original_max_touch_points_(0),
      embedder_script_enabled_(
          web_view_impl->GetPage()->GetSettings().GetScriptEnabled()),
      script_execution_disabled_(false) {}

DevToolsEmulator::~DevToolsEmulator() {}

DevToolsEmulator* DevToolsEmulator::Create(WebViewImpl* web_view_impl) {
  return new DevToolsEmulator(web_view_impl);
}

DEFINE_TRACE(DevToolsEmulator) {}

void DevToolsEmulator::SetTextAutosizingEnabled(bool enabled) {
  embedder_text_autosizing_enabled_ = enabled;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetTextAutosizingEnabled(enabled);
}

void DevToolsEmulator::SetDeviceScaleAdjustment(float device_scale_adjustment) {
  embedder_device_scale_adjustment_ = device_scale_adjustment;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetDeviceScaleAdjustment(
        device_scale_adjustment);
}

void DevToolsEmulator::SetPreferCompositingToLCDTextEnabled(bool enabled) {
  embedder_prefer_compositing_to_lcd_text_enabled_ = enabled;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()
        ->GetSettings()
        .SetPreferCompositingToLCDTextEnabled(enabled);
}

void DevToolsEmulator::SetViewportStyle(WebViewportStyle style) {
  embedder_viewport_style_ = style;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetViewportStyle(style);
}

void DevToolsEmulator::SetPluginsEnabled(bool enabled) {
  embedder_plugins_enabled_ = enabled;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetPluginsEnabled(enabled);
}

void DevToolsEmulator::SetScriptEnabled(bool enabled) {
  embedder_script_enabled_ = enabled;
  if (!script_execution_disabled_)
    web_view_impl_->GetPage()->GetSettings().SetScriptEnabled(enabled);
}

void DevToolsEmulator::SetDoubleTapToZoomEnabled(bool enabled) {
  double_tap_to_zoom_enabled_ = enabled;
}

bool DevToolsEmulator::DoubleTapToZoomEnabled() const {
  return touch_event_emulation_enabled_ ? true : double_tap_to_zoom_enabled_;
}

void DevToolsEmulator::SetMainFrameResizesAreOrientationChanges(bool value) {
  embedder_main_frame_resizes_are_orientation_changes_ = value;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()
        ->GetSettings()
        .SetMainFrameResizesAreOrientationChanges(value);
}

void DevToolsEmulator::SetAvailablePointerTypes(int types) {
  embedder_available_pointer_types_ = types;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetAvailablePointerTypes(types);
}

void DevToolsEmulator::SetPrimaryPointerType(PointerType pointer_type) {
  embedder_primary_pointer_type_ = pointer_type;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetPrimaryPointerType(
        pointer_type);
}

void DevToolsEmulator::SetAvailableHoverTypes(int types) {
  embedder_available_hover_types_ = types;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetAvailableHoverTypes(types);
}

void DevToolsEmulator::SetPrimaryHoverType(HoverType hover_type) {
  embedder_primary_hover_type_ = hover_type;
  bool emulate_mobile_enabled =
      device_metrics_enabled_ && emulate_mobile_enabled_;
  if (!emulate_mobile_enabled)
    web_view_impl_->GetPage()->GetSettings().SetPrimaryHoverType(hover_type);
}

void DevToolsEmulator::EnableDeviceEmulation(
    const WebDeviceEmulationParams& params) {
  if (device_metrics_enabled_ &&
      emulation_params_.view_size == params.view_size &&
      emulation_params_.screen_position == params.screen_position &&
      emulation_params_.device_scale_factor == params.device_scale_factor &&
      emulation_params_.offset == params.offset &&
      emulation_params_.scale == params.scale) {
    return;
  }
  if (emulation_params_.device_scale_factor != params.device_scale_factor ||
      !device_metrics_enabled_)
    GetMemoryCache()->EvictResources();

  emulation_params_ = params;
  device_metrics_enabled_ = true;

  web_view_impl_->GetPage()->GetSettings().SetDeviceScaleAdjustment(
      calculateDeviceScaleAdjustment(params.view_size.width,
                                     params.view_size.height,
                                     params.device_scale_factor));

  if (params.screen_position == WebDeviceEmulationParams::kMobile)
    EnableMobileEmulation();
  else
    DisableMobileEmulation();

  web_view_impl_->SetCompositorDeviceScaleFactorOverride(
      params.device_scale_factor);
  UpdateRootLayerTransform();
  // TODO(dgozman): mainFrameImpl() is null when it's remote. Figure out how
  // we end up with enabling emulation in this case.
  if (web_view_impl_->MainFrameImpl()) {
    if (Document* document =
            web_view_impl_->MainFrameImpl()->GetFrame()->GetDocument())
      document->MediaQueryAffectingValueChanged();
  }
}

void DevToolsEmulator::DisableDeviceEmulation() {
  if (!device_metrics_enabled_)
    return;

  GetMemoryCache()->EvictResources();
  device_metrics_enabled_ = false;
  web_view_impl_->GetPage()->GetSettings().SetDeviceScaleAdjustment(
      embedder_device_scale_adjustment_);
  DisableMobileEmulation();
  web_view_impl_->SetCompositorDeviceScaleFactorOverride(0.f);
  web_view_impl_->SetPageScaleFactor(1.f);
  UpdateRootLayerTransform();
  // mainFrameImpl() could be null during cleanup or remote <-> local swap.
  if (web_view_impl_->MainFrameImpl()) {
    if (Document* document =
            web_view_impl_->MainFrameImpl()->GetFrame()->GetDocument())
      document->MediaQueryAffectingValueChanged();
  }
}

void DevToolsEmulator::EnableMobileEmulation() {
  if (emulate_mobile_enabled_)
    return;
  emulate_mobile_enabled_ = true;
  is_overlay_scrollbars_enabled_ =
      RuntimeEnabledFeatures::overlayScrollbarsEnabled();
  RuntimeEnabledFeatures::setOverlayScrollbarsEnabled(true);
  is_orientation_event_enabled_ =
      RuntimeEnabledFeatures::orientationEventEnabled();
  RuntimeEnabledFeatures::setOrientationEventEnabled(true);
  is_mobile_layout_theme_enabled_ =
      RuntimeEnabledFeatures::mobileLayoutThemeEnabled();
  RuntimeEnabledFeatures::setMobileLayoutThemeEnabled(true);
  ComputedStyle::InvalidateInitialStyle();
  web_view_impl_->GetPage()->GetSettings().SetViewportStyle(
      WebViewportStyle::kMobile);
  web_view_impl_->GetPage()->GetSettings().SetViewportEnabled(true);
  web_view_impl_->GetPage()->GetSettings().SetViewportMetaEnabled(true);
  web_view_impl_->GetPage()->GetVisualViewport().InitializeScrollbars();
  web_view_impl_->GetSettings()->SetShrinksViewportContentToFit(true);
  web_view_impl_->GetPage()->GetSettings().SetTextAutosizingEnabled(true);
  web_view_impl_->GetPage()->GetSettings().SetPreferCompositingToLCDTextEnabled(
      true);
  web_view_impl_->GetPage()->GetSettings().SetPluginsEnabled(false);
  web_view_impl_->GetPage()->GetSettings().SetAvailablePointerTypes(
      kPointerTypeCoarse);
  web_view_impl_->GetPage()->GetSettings().SetPrimaryPointerType(
      kPointerTypeCoarse);
  web_view_impl_->GetPage()->GetSettings().SetAvailableHoverTypes(
      kHoverTypeNone);
  web_view_impl_->GetPage()->GetSettings().SetPrimaryHoverType(kHoverTypeNone);
  web_view_impl_->GetPage()
      ->GetSettings()
      .SetMainFrameResizesAreOrientationChanges(true);
  web_view_impl_->SetZoomFactorOverride(1);

  original_default_minimum_page_scale_factor_ =
      web_view_impl_->DefaultMinimumPageScaleFactor();
  original_default_maximum_page_scale_factor_ =
      web_view_impl_->DefaultMaximumPageScaleFactor();
  web_view_impl_->SetDefaultPageScaleLimits(0.25f, 5);
  // TODO(dgozman): mainFrameImpl() is null when it's remote. Figure out how
  // we end up with enabling emulation in this case.
  if (web_view_impl_->MainFrameImpl())
    web_view_impl_->MainFrameImpl()->GetFrameView()->UpdateLayout();
}

void DevToolsEmulator::DisableMobileEmulation() {
  if (!emulate_mobile_enabled_)
    return;
  RuntimeEnabledFeatures::setOverlayScrollbarsEnabled(
      is_overlay_scrollbars_enabled_);
  RuntimeEnabledFeatures::setOrientationEventEnabled(
      is_orientation_event_enabled_);
  RuntimeEnabledFeatures::setMobileLayoutThemeEnabled(
      is_mobile_layout_theme_enabled_);
  ComputedStyle::InvalidateInitialStyle();
  web_view_impl_->GetPage()->GetSettings().SetViewportEnabled(false);
  web_view_impl_->GetPage()->GetSettings().SetViewportMetaEnabled(false);
  web_view_impl_->GetPage()->GetVisualViewport().InitializeScrollbars();
  web_view_impl_->GetSettings()->SetShrinksViewportContentToFit(false);
  web_view_impl_->GetPage()->GetSettings().SetTextAutosizingEnabled(
      embedder_text_autosizing_enabled_);
  web_view_impl_->GetPage()->GetSettings().SetPreferCompositingToLCDTextEnabled(
      embedder_prefer_compositing_to_lcd_text_enabled_);
  web_view_impl_->GetPage()->GetSettings().SetViewportStyle(
      embedder_viewport_style_);
  web_view_impl_->GetPage()->GetSettings().SetPluginsEnabled(
      embedder_plugins_enabled_);
  web_view_impl_->GetPage()->GetSettings().SetAvailablePointerTypes(
      embedder_available_pointer_types_);
  web_view_impl_->GetPage()->GetSettings().SetPrimaryPointerType(
      embedder_primary_pointer_type_);
  web_view_impl_->GetPage()->GetSettings().SetAvailableHoverTypes(
      embedder_available_hover_types_);
  web_view_impl_->GetPage()->GetSettings().SetPrimaryHoverType(
      embedder_primary_hover_type_);
  web_view_impl_->GetPage()
      ->GetSettings()
      .SetMainFrameResizesAreOrientationChanges(
          embedder_main_frame_resizes_are_orientation_changes_);
  web_view_impl_->SetZoomFactorOverride(0);
  emulate_mobile_enabled_ = false;
  web_view_impl_->SetDefaultPageScaleLimits(
      original_default_minimum_page_scale_factor_,
      original_default_maximum_page_scale_factor_);
  // mainFrameImpl() could be null during cleanup or remote <-> local swap.
  if (web_view_impl_->MainFrameImpl())
    web_view_impl_->MainFrameImpl()->GetFrameView()->UpdateLayout();
}

float DevToolsEmulator::CompositorDeviceScaleFactor() const {
  if (device_metrics_enabled_)
    return emulation_params_.device_scale_factor;
  return web_view_impl_->GetPage()->DeviceScaleFactorDeprecated();
}

void DevToolsEmulator::ForceViewport(const WebFloatPoint& position,
                                     float scale) {
  GraphicsLayer* container_layer =
      web_view_impl_->GetPage()->GetVisualViewport().ContainerLayer();
  if (!viewport_override_) {
    viewport_override_ = ViewportOverride();

    // Disable clipping on the visual viewport layer, to ensure the whole area
    // is painted.
    if (container_layer) {
      viewport_override_->original_visual_viewport_masking =
          container_layer->MasksToBounds();
      container_layer->SetMasksToBounds(false);
    }
  }

  viewport_override_->position = position;
  viewport_override_->scale = scale;

  // Move the correct (scaled) content area to show in the top left of the
  // CompositorFrame via the root transform.
  UpdateRootLayerTransform();
}

void DevToolsEmulator::ResetViewport() {
  if (!viewport_override_)
    return;

  bool original_masking = viewport_override_->original_visual_viewport_masking;
  viewport_override_ = WTF::kNullopt;

  GraphicsLayer* container_layer =
      web_view_impl_->GetPage()->GetVisualViewport().ContainerLayer();
  if (container_layer)
    container_layer->SetMasksToBounds(original_masking);
  UpdateRootLayerTransform();
}

void DevToolsEmulator::MainFrameScrollOrScaleChanged() {
  // Viewport override has to take current page scale and scroll offset into
  // account. Update the transform if override is active.
  if (viewport_override_)
    UpdateRootLayerTransform();
}

void DevToolsEmulator::ApplyDeviceEmulationTransform(
    TransformationMatrix* transform) {
  if (device_metrics_enabled_) {
    WebSize offset(emulation_params_.offset.x, emulation_params_.offset.y);
    // Scale first, so that translation is unaffected.
    transform->Translate(offset.width, offset.height);
    transform->Scale(emulation_params_.scale);
    if (web_view_impl_->MainFrameImpl())
      web_view_impl_->MainFrameImpl()->SetInputEventsTransformForEmulation(
          offset, emulation_params_.scale);
  } else {
    if (web_view_impl_->MainFrameImpl())
      web_view_impl_->MainFrameImpl()->SetInputEventsTransformForEmulation(
          WebSize(0, 0), 1.0);
  }
}

void DevToolsEmulator::ApplyViewportOverride(TransformationMatrix* transform) {
  if (!viewport_override_)
    return;

  // Transform operations follow in reverse application.
  // Last, scale positioned area according to override.
  transform->Scale(viewport_override_->scale);

  // Translate while taking into account current scroll offset.
  WebSize scroll_offset = web_view_impl_->MainFrame()->GetScrollOffset();
  WebFloatPoint visual_offset = web_view_impl_->VisualViewportOffset();
  float scroll_x = scroll_offset.width + visual_offset.x;
  float scroll_y = scroll_offset.height + visual_offset.y;
  transform->Translate(-viewport_override_->position.x + scroll_x,
                       -viewport_override_->position.y + scroll_y);

  // First, reverse page scale, so we don't have to take it into account for
  // calculation of the translation.
  transform->Scale(1. / web_view_impl_->PageScaleFactor());
}

void DevToolsEmulator::UpdateRootLayerTransform() {
  TransformationMatrix transform;

  // Apply device emulation transform first, so that it is affected by the
  // viewport override.
  ApplyViewportOverride(&transform);
  ApplyDeviceEmulationTransform(&transform);
  web_view_impl_->SetDeviceEmulationTransform(transform);
}

WTF::Optional<IntRect> DevToolsEmulator::VisibleContentRectForPainting() const {
  if (!viewport_override_)
    return WTF::kNullopt;
  FloatSize viewport_size(web_view_impl_->LayerTreeView()->GetViewportSize());
  viewport_size.Scale(1. / CompositorDeviceScaleFactor());
  viewport_size.Scale(1. / viewport_override_->scale);
  return EnclosingIntRect(
      FloatRect(viewport_override_->position.x, viewport_override_->position.y,
                viewport_size.Width(), viewport_size.Height()));
}

void DevToolsEmulator::SetTouchEventEmulationEnabled(bool enabled) {
  if (touch_event_emulation_enabled_ == enabled)
    return;
  if (!touch_event_emulation_enabled_) {
    original_touch_event_feature_detection_enabled_ =
        RuntimeEnabledFeatures::touchEventFeatureDetectionEnabled();
    original_device_supports_touch_ =
        web_view_impl_->GetPage()->GetSettings().GetDeviceSupportsTouch();
    original_max_touch_points_ =
        web_view_impl_->GetPage()->GetSettings().GetMaxTouchPoints();
  }
  RuntimeEnabledFeatures::setTouchEventFeatureDetectionEnabled(
      enabled ? true : original_touch_event_feature_detection_enabled_);
  if (!original_device_supports_touch_) {
    if (enabled && web_view_impl_->MainFrameImpl()) {
      web_view_impl_->MainFrameImpl()
          ->GetFrame()
          ->GetEventHandler()
          .ClearMouseEventManager();
    }
    web_view_impl_->GetPage()->GetSettings().SetDeviceSupportsTouch(
        enabled ? true : original_device_supports_touch_);
    // Currently emulation does not provide multiple touch points.
    web_view_impl_->GetPage()->GetSettings().SetMaxTouchPoints(
        enabled ? 1 : original_max_touch_points_);
  }
  touch_event_emulation_enabled_ = enabled;
  // TODO(dgozman): mainFrameImpl() check in this class should be unnecessary.
  // It is only needed when we reattach and restore InspectorEmulationAgent,
  // which happens before everything has been setup correctly, and therefore
  // fails during remote -> local main frame transition.
  // We should instead route emulation from browser through the WebViewImpl
  // to the local main frame, and remove InspectorEmulationAgent entirely.
  if (web_view_impl_->MainFrameImpl())
    web_view_impl_->MainFrameImpl()->GetFrameView()->UpdateLayout();
}

void DevToolsEmulator::SetScriptExecutionDisabled(
    bool script_execution_disabled) {
  script_execution_disabled_ = script_execution_disabled;
  web_view_impl_->GetPage()->GetSettings().SetScriptEnabled(
      script_execution_disabled_ ? false : embedder_script_enabled_);
}

bool DevToolsEmulator::HandleInputEvent(const WebInputEvent& input_event) {
  Page* page = web_view_impl_->GetPage();
  if (!page)
    return false;

  // FIXME: This workaround is required for touch emulation on Mac, where
  // compositor-side pinch handling is not enabled. See http://crbug.com/138003.
  bool is_pinch = input_event.GetType() == WebInputEvent::kGesturePinchBegin ||
                  input_event.GetType() == WebInputEvent::kGesturePinchUpdate ||
                  input_event.GetType() == WebInputEvent::kGesturePinchEnd;
  if (is_pinch && touch_event_emulation_enabled_) {
    FrameView* frame_view = page->DeprecatedLocalMainFrame()->View();
    WebGestureEvent scaled_event = TransformWebGestureEvent(
        frame_view, static_cast<const WebGestureEvent&>(input_event));
    float page_scale_factor = page->PageScaleFactor();
    if (scaled_event.GetType() == WebInputEvent::kGesturePinchBegin) {
      WebFloatPoint gesture_position = scaled_event.PositionInRootFrame();
      last_pinch_anchor_css_ = WTF::WrapUnique(new IntPoint(
          RoundedIntPoint(gesture_position + frame_view->GetScrollOffset())));
      last_pinch_anchor_dip_ =
          WTF::WrapUnique(new IntPoint(FlooredIntPoint(gesture_position)));
      last_pinch_anchor_dip_->Scale(page_scale_factor, page_scale_factor);
    }
    if (scaled_event.GetType() == WebInputEvent::kGesturePinchUpdate &&
        last_pinch_anchor_css_) {
      float new_page_scale_factor =
          page_scale_factor * scaled_event.PinchScale();
      IntPoint anchor_css(*last_pinch_anchor_dip_.get());
      anchor_css.Scale(1.f / new_page_scale_factor,
                       1.f / new_page_scale_factor);
      web_view_impl_->SetPageScaleFactor(new_page_scale_factor);
      web_view_impl_->MainFrame()->SetScrollOffset(
          ToIntSize(*last_pinch_anchor_css_.get() - ToIntSize(anchor_css)));
    }
    if (scaled_event.GetType() == WebInputEvent::kGesturePinchEnd) {
      last_pinch_anchor_css_.reset();
      last_pinch_anchor_dip_.reset();
    }
    return true;
  }

  return false;
}

}  // namespace blink
