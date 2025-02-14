// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/tiles/prioritized_tile.h"

#include "cc/debug/traced_value.h"
#include "cc/tiles/picture_layer_tiling.h"

namespace cc {

PrioritizedTile::PrioritizedTile() = default;

PrioritizedTile::PrioritizedTile(Tile* tile,
                                 const PictureLayerTiling* source_tiling,
                                 const TilePriority& priority,
                                 bool is_occluded,
                                 bool is_process_for_images_only)
    : tile_(tile),
      source_tiling_(source_tiling),
      priority_(priority),
      is_occluded_(is_occluded),
      is_process_for_images_only_(is_process_for_images_only) {}

PrioritizedTile::~PrioritizedTile() = default;

void PrioritizedTile::AsValueInto(base::trace_event::TracedValue* value) const {
  tile_->AsValueInto(value);

  TracedValue::SetIDRef(raster_source().get(), value, "picture_pile");

  value->BeginDictionary("combined_priority");
  priority().AsValueInto(value);
  value->SetBoolean("is_occluded", is_occluded_);
  value->EndDictionary();

  value->SetString("resolution", TileResolutionToString(priority().resolution));
}

}  // namespace cc
