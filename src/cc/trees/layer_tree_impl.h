// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_LAYER_TREE_IMPL_H_
#define CC_TREES_LAYER_TREE_IMPL_H_

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "cc/base/synced_property.h"
#include "cc/input/event_listener_properties.h"
#include "cc/input/layer_selection_bound.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/layer_list_iterator.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/swap_promise.h"
#include "cc/resources/ui_resource_client.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/property_tree.h"

namespace base {
namespace trace_event {
class TracedValue;
}
}

namespace cc {

class ContextProvider;
class DebugRectHistory;
class FrameRateCounter;
class HeadsUpDisplayLayerImpl;
class ImageDecodeCache;
class LayerTreeDebugState;
class LayerTreeImpl;
class LayerTreeSettings;
class MemoryHistory;
class PictureLayerImpl;
class TaskRunnerProvider;
class ResourceProvider;
class TileManager;
class UIResourceRequest;
class VideoFrameControllerClient;
struct PendingPageScaleAnimation;

typedef std::vector<UIResourceRequest> UIResourceRequestQueue;
typedef SyncedProperty<AdditionGroup<float>> SyncedBrowserControls;
typedef SyncedProperty<AdditionGroup<gfx::Vector2dF>> SyncedElasticOverscroll;

class LayerTreeLifecycle {
 public:
  enum LifecycleState {
    kNotSyncing,

    // The following states are the steps performed when syncing properties to
    // this tree (see: LayerTreeHost::FinishCommitOnImplThread or
    // LayerTreeHostImpl::ActivateSyncTree).
    kBeginningSync,
    kSyncedPropertyTrees,
    kSyncedLayerProperties,
    kLastSyncState = kSyncedLayerProperties,

    // TODO(pdr): Add states to cover more than just the synchronization steps.
  };

  void AdvanceTo(LifecycleState);

  bool AllowsPropertyTreeAccess() const {
    return state_ == kNotSyncing || state_ >= kSyncedPropertyTrees;
  }
  bool AllowsLayerPropertyAccess() const {
    return state_ == kNotSyncing || state_ >= kSyncedLayerProperties;
  }

 private:
  LifecycleState state_ = kNotSyncing;
};

class CC_EXPORT LayerTreeImpl {
 public:
  // This is the number of times a fixed point has to be hit continuously by a
  // layer to consider it as jittering.
  enum : int { kFixedPointHitsThreshold = 3 };
  LayerTreeImpl(LayerTreeHostImpl* layer_tree_host_impl,
                scoped_refptr<SyncedProperty<ScaleGroup>> page_scale_factor,
                scoped_refptr<SyncedBrowserControls> top_controls_shown_ratio,
                scoped_refptr<SyncedElasticOverscroll> elastic_overscroll);
  virtual ~LayerTreeImpl();

  void Shutdown();
  void ReleaseResources();
  void ReleaseTileResources();
  void RecreateTileResources();

  // Methods called by the layer tree that pass-through or access LTHI.
  // ---------------------------------------------------------------------------
  const LayerTreeSettings& settings() const;
  const LayerTreeDebugState& debug_state() const;
  ContextProvider* context_provider() const;
  ResourceProvider* resource_provider() const;
  TileManager* tile_manager() const;
  ImageDecodeCache* image_decode_cache() const;
  FrameRateCounter* frame_rate_counter() const;
  MemoryHistory* memory_history() const;
  gfx::Size device_viewport_size() const;
  DebugRectHistory* debug_rect_history() const;
  bool IsActiveTree() const;
  bool IsPendingTree() const;
  bool IsRecycleTree() const;
  bool IsSyncTree() const;
  LayerImpl* FindActiveTreeLayerById(int id);
  LayerImpl* FindPendingTreeLayerById(int id);
  bool PinchGestureActive() const;
  BeginFrameArgs CurrentBeginFrameArgs() const;
  base::TimeDelta CurrentBeginFrameInterval() const;
  gfx::Rect DeviceViewport() const;
  const gfx::Rect ViewportRectForTilePriority() const;
  std::unique_ptr<ScrollbarAnimationController>
  CreateScrollbarAnimationController(ElementId scroll_element_id,
                                     float initial_opacity);
  void DidAnimateScrollOffset();
  bool use_gpu_rasterization() const;
  GpuRasterizationStatus GetGpuRasterizationStatus() const;
  bool create_low_res_tiling() const;
  bool RequiresHighResToDraw() const;
  bool SmoothnessTakesPriority() const;
  VideoFrameControllerClient* GetVideoFrameControllerClient() const;
  MutatorHost* mutator_host() const {
    return layer_tree_host_impl_->mutator_host();
  }

  // Tree specific methods exposed to layer-impl tree.
  // ---------------------------------------------------------------------------
  void SetNeedsRedraw();

  // Tracing methods.
  // ---------------------------------------------------------------------------
  void GetAllPrioritizedTilesForTracing(
      std::vector<PrioritizedTile>* prioritized_tiles) const;
  void AsValueInto(base::trace_event::TracedValue* dict) const;

  // Other public methods
  // ---------------------------------------------------------------------------
  LayerImpl* root_layer_for_testing() {
    return layer_list_.empty() ? nullptr : layer_list_[0];
  }
  const RenderSurfaceImpl* RootRenderSurface() const;
  bool LayerListIsEmpty() const;
  void SetRootLayerForTesting(std::unique_ptr<LayerImpl>);
  void OnCanDrawStateChangedForTree();
  bool IsRootLayer(const LayerImpl* layer) const;
  std::unique_ptr<OwnedLayerImplList> DetachLayers();

  void SetPropertyTrees(PropertyTrees* property_trees);
  PropertyTrees* property_trees() {
    // TODO(pdr): We should enable this DCHECK because it will catch uses of
    // stale property trees, but it currently fails too many existing tests.
    // DCHECK(lifecycle().AllowsPropertyTreeAccess());
    return &property_trees_;
  }

  void PushPropertyTreesTo(LayerTreeImpl* tree_impl);
  void PushPropertiesTo(LayerTreeImpl* tree_impl);

  void MoveChangeTrackingToLayers();

  void ForceRecalculateRasterScales();

  LayerImplList::const_iterator begin() const;
  LayerImplList::const_iterator end() const;
  LayerImplList::reverse_iterator rbegin();
  LayerImplList::reverse_iterator rend();

  void SetTransformMutated(ElementId element_id,
                           const gfx::Transform& transform);
  void SetOpacityMutated(ElementId element_id, float opacity);
  void SetFilterMutated(ElementId element_id, const FilterOperations& filters);

  int source_frame_number() const { return source_frame_number_; }
  void set_source_frame_number(int frame_number) {
    source_frame_number_ = frame_number;
  }

  bool is_first_frame_after_commit() const {
    return source_frame_number_ != is_first_frame_after_commit_tracker_;
  }

  void set_is_first_frame_after_commit(bool is_first_frame_after_commit) {
    is_first_frame_after_commit_tracker_ =
        is_first_frame_after_commit ? -1 : source_frame_number_;
  }

  HeadsUpDisplayLayerImpl* hud_layer() { return hud_layer_; }
  void set_hud_layer(HeadsUpDisplayLayerImpl* layer_impl) {
    hud_layer_ = layer_impl;
  }

  gfx::ScrollOffset TotalScrollOffset() const;
  gfx::ScrollOffset TotalMaxScrollOffset() const;

  ScrollNode* CurrentlyScrollingNode();
  const ScrollNode* CurrentlyScrollingNode() const;
  int LastScrolledScrollNodeIndex() const;
  void SetCurrentlyScrollingNode(ScrollNode* node);
  void ClearCurrentlyScrollingNode();

  struct ViewportLayerIds {
    int overscroll_elasticity = Layer::INVALID_ID;
    int page_scale = Layer::INVALID_ID;
    int inner_viewport_container = Layer::INVALID_ID;
    int outer_viewport_container = Layer::INVALID_ID;
    int inner_viewport_scroll = Layer::INVALID_ID;
    int outer_viewport_scroll = Layer::INVALID_ID;
  };
  void SetViewportLayersFromIds(const ViewportLayerIds& viewport_layer_ids);
  void ClearViewportLayers();
  LayerImpl* OverscrollElasticityLayer() const {
    return LayerById(viewport_layer_ids_.overscroll_elasticity);
  }
  LayerImpl* PageScaleLayer() const {
    return LayerById(viewport_layer_ids_.page_scale);
  }
  LayerImpl* InnerViewportContainerLayer() const {
    return LayerById(viewport_layer_ids_.inner_viewport_container);
  }
  LayerImpl* OuterViewportContainerLayer() const {
    return LayerById(viewport_layer_ids_.outer_viewport_container);
  }
  LayerImpl* InnerViewportScrollLayer() const {
    return LayerById(viewport_layer_ids_.inner_viewport_scroll);
  }
  LayerImpl* OuterViewportScrollLayer() const {
    return LayerById(viewport_layer_ids_.outer_viewport_scroll);
  }

  void ApplySentScrollAndScaleDeltasFromAbortedCommit();

  SkColor background_color() const { return background_color_; }
  void set_background_color(SkColor color) { background_color_ = color; }

  bool has_transparent_background() const {
    return has_transparent_background_;
  }
  void set_has_transparent_background(bool transparent) {
    has_transparent_background_ = transparent;
  }

  void UpdatePropertyTreeScrollingAndAnimationFromMainThread(
      bool is_impl_side_update);
  void SetPageScaleOnActiveTree(float active_page_scale);
  void PushPageScaleFromMainThread(float page_scale_factor,
                                   float min_page_scale_factor,
                                   float max_page_scale_factor);
  float current_page_scale_factor() const {
    return page_scale_factor()->Current(IsActiveTree());
  }
  float min_page_scale_factor() const { return min_page_scale_factor_; }
  float max_page_scale_factor() const { return max_page_scale_factor_; }

  float page_scale_delta() const { return page_scale_factor()->Delta(); }

  SyncedProperty<ScaleGroup>* page_scale_factor();
  const SyncedProperty<ScaleGroup>* page_scale_factor() const;

  void SetDeviceScaleFactor(float device_scale_factor);
  float device_scale_factor() const { return device_scale_factor_; }

  void set_painted_device_scale_factor(float painted_device_scale_factor) {
    painted_device_scale_factor_ = painted_device_scale_factor;
  }
  float painted_device_scale_factor() const {
    return painted_device_scale_factor_;
  }

  void set_content_source_id(uint32_t id) { content_source_id_ = id; }
  uint32_t content_source_id() { return content_source_id_; }

  void set_local_surface_id(const LocalSurfaceId& id) {
    local_surface_id_ = id;
  }
  const LocalSurfaceId& local_surface_id() const { return local_surface_id_; }

  void SetRasterColorSpace(const gfx::ColorSpace& raster_color_space);
  const gfx::ColorSpace& raster_color_space() const {
    return raster_color_space_;
  }

  SyncedElasticOverscroll* elastic_overscroll() {
    return elastic_overscroll_.get();
  }
  const SyncedElasticOverscroll* elastic_overscroll() const {
    return elastic_overscroll_.get();
  }

  SyncedBrowserControls* top_controls_shown_ratio() {
    return top_controls_shown_ratio_.get();
  }
  const SyncedBrowserControls* top_controls_shown_ratio() const {
    return top_controls_shown_ratio_.get();
  }

  void SetElementIdsForTesting();

  // Updates draw properties and render surface layer list, as well as tile
  // priorities. Returns false if it was unable to update.  Updating lcd
  // text may cause invalidations, so should only be done after a commit.
  bool UpdateDrawProperties(bool update_lcd_text);
  void BuildPropertyTreesForTesting();
  void BuildLayerListAndPropertyTreesForTesting();

  void set_needs_update_draw_properties() {
    needs_update_draw_properties_ = true;
  }
  bool needs_update_draw_properties() const {
    return needs_update_draw_properties_;
  }

  bool is_in_resourceless_software_draw_mode() {
    return (layer_tree_host_impl_->GetDrawMode() ==
            DRAW_MODE_RESOURCELESS_SOFTWARE);
  }

  void set_needs_full_tree_sync(bool needs) { needs_full_tree_sync_ = needs; }
  bool needs_full_tree_sync() const { return needs_full_tree_sync_; }

  void ForceRedrawNextActivation() { next_activation_forces_redraw_ = true; }

  void set_has_ever_been_drawn(bool has_drawn) {
    has_ever_been_drawn_ = has_drawn;
  }
  bool has_ever_been_drawn() const { return has_ever_been_drawn_; }

  void set_ui_resource_request_queue(UIResourceRequestQueue queue);

  const RenderSurfaceList& GetRenderSurfaceList() const;
  const Region& UnoccludedScreenSpaceRegion() const;

  // These return the size of the root scrollable area and the size of
  // the user-visible scrolling viewport, in CSS layout coordinates.
  gfx::SizeF ScrollableSize() const;
  gfx::SizeF ScrollableViewportSize() const;

  gfx::Rect RootScrollLayerDeviceViewportBounds() const;

  LayerImpl* LayerById(int id) const;

  int LayerIdByElementId(ElementId element_id) const;
  // TODO(jaydasika): this is deprecated. It is used by
  // animation/compositor-worker to look up layers to mutate, but in future, we
  // will update property trees.
  LayerImpl* LayerByElementId(ElementId element_id) const;
  void AddToElementMap(LayerImpl* layer);
  void RemoveFromElementMap(LayerImpl* layer);

  void AddLayerShouldPushProperties(LayerImpl* layer);
  void RemoveLayerShouldPushProperties(LayerImpl* layer);
  std::unordered_set<LayerImpl*>& LayersThatShouldPushProperties();
  bool LayerNeedsPushPropertiesForTesting(LayerImpl* layer);

  // These should be called by LayerImpl's ctor/dtor.
  void RegisterLayer(LayerImpl* layer);
  void UnregisterLayer(LayerImpl* layer);

  // These manage ownership of the LayerImpl.
  void AddLayer(std::unique_ptr<LayerImpl> layer);
  std::unique_ptr<LayerImpl> RemoveLayer(int id);

  size_t NumLayers();

  void DidBecomeActive();

  // Set on the active tree when the viewport size recently changed
  // and the active tree's size is now out of date.
  bool ViewportSizeInvalid() const;
  void SetViewportSizeInvalid();
  void ResetViewportSizeInvalid();

  // Used for accessing the task runner and debug assertions.
  TaskRunnerProvider* task_runner_provider() const;

  // Distribute the root scroll between outer and inner viewport scroll layer.
  // The outer viewport scroll layer scrolls first.
  bool DistributeRootScrollOffset(const gfx::ScrollOffset& root_offset);

  void ApplyScroll(ScrollNode* scroll_node, ScrollState* scroll_state) {
    layer_tree_host_impl_->ApplyScroll(scroll_node, scroll_state);
  }

  // Call this function when you expect there to be a swap buffer.
  // See swap_promise.h for how to use SwapPromise.
  //
  // A swap promise queued by QueueSwapPromise travels with the layer
  // information currently associated with the tree. For example, when
  // a pending tree is activated, the swap promise is passed to the
  // active tree along with the layer information. Similarly, when a
  // new activation overwrites layer information on the active tree,
  // queued swap promises are broken.
  void QueueSwapPromise(std::unique_ptr<SwapPromise> swap_promise);

  // Queue a swap promise, pinned to this tree. Pinned swap promises
  // may only be queued on the active tree.
  //
  // An active tree pinned swap promise will see only DidSwap() or
  // DidNotSwap(SWAP_FAILS). No DidActivate() will be seen because
  // that has already happened prior to queueing of the swap promise.
  //
  // Pinned active tree swap promises will not be broken prematurely
  // on the active tree if a new tree is activated.
  void QueuePinnedSwapPromise(std::unique_ptr<SwapPromise> swap_promise);

  // Takes ownership of |new_swap_promises|. Existing swap promises in
  // |swap_promise_list_| are cancelled (SWAP_FAILS).
  void PassSwapPromises(
      std::vector<std::unique_ptr<SwapPromise>> new_swap_promises);
  void AppendSwapPromises(
      std::vector<std::unique_ptr<SwapPromise>> new_swap_promises);
  void FinishSwapPromises(CompositorFrameMetadata* metadata);
  void ClearSwapPromises();
  void BreakSwapPromises(SwapPromise::DidNotSwapReason reason);

  void DidModifyTilePriorities();

  ResourceId ResourceIdForUIResource(UIResourceId uid) const;
  void ProcessUIResourceRequestQueue();

  bool IsUIResourceOpaque(UIResourceId uid) const;

  void RegisterPictureLayerImpl(PictureLayerImpl* layer);
  void UnregisterPictureLayerImpl(PictureLayerImpl* layer);
  const std::vector<PictureLayerImpl*>& picture_layers() const {
    return picture_layers_;
  }

  void RegisterScrollbar(ScrollbarLayerImplBase* scrollbar_layer);
  void UnregisterScrollbar(ScrollbarLayerImplBase* scrollbar_layer);
  ScrollbarSet ScrollbarsFor(ElementId scroll_element_id) const;

  void RegisterScrollLayer(LayerImpl* layer);
  void UnregisterScrollLayer(LayerImpl* layer);

  void AddSurfaceLayer(LayerImpl* layer);
  void RemoveSurfaceLayer(LayerImpl* layer);
  const LayerImplList& SurfaceLayers() const { return surface_layers_; }

  LayerImpl* FindFirstScrollingLayerOrDrawnScrollbarThatIsHitByPoint(
      const gfx::PointF& screen_space_point);

  LayerImpl* FindLayerThatIsHitByPoint(const gfx::PointF& screen_space_point);

  LayerImpl* FindLayerThatIsHitByPointInTouchHandlerRegion(
      const gfx::PointF& screen_space_point);

  void RegisterSelection(const LayerSelection& selection);

  bool GetAndResetHandleVisibilityChanged();

  // Compute the current selection handle location and visbility with respect to
  // the viewport.
  void GetViewportSelection(Selection<gfx::SelectionBound>* selection);

  void set_browser_controls_shrink_blink_size(bool shrink);
  bool browser_controls_shrink_blink_size() const {
    return browser_controls_shrink_blink_size_;
  }
  bool SetCurrentBrowserControlsShownRatio(float ratio);
  float CurrentBrowserControlsShownRatio() const {
    return top_controls_shown_ratio_->Current(IsActiveTree());
  }
  void set_top_controls_height(float top_controls_height);
  float top_controls_height() const { return top_controls_height_; }
  void PushBrowserControlsFromMainThread(float top_controls_shown_ratio);
  void set_bottom_controls_height(float bottom_controls_height);
  float bottom_controls_height() const { return bottom_controls_height_; }

  void SetPendingPageScaleAnimation(
      std::unique_ptr<PendingPageScaleAnimation> pending_animation);
  std::unique_ptr<PendingPageScaleAnimation> TakePendingPageScaleAnimation();

  void DidUpdateScrollOffset(int layer_id);
  void DidUpdateScrollState(int layer_id);

  bool have_scroll_event_handlers() const {
    return have_scroll_event_handlers_;
  }
  void set_have_scroll_event_handlers(bool have_event_handlers) {
    have_scroll_event_handlers_ = have_event_handlers;
  }

  EventListenerProperties event_listener_properties(
      EventListenerClass event_class) const {
    return event_listener_properties_[static_cast<size_t>(event_class)];
  }
  void set_event_listener_properties(EventListenerClass event_class,
                                     EventListenerProperties event_properties) {
    event_listener_properties_[static_cast<size_t>(event_class)] =
        event_properties;
  }

  void ResetAllChangeTracking();

  void AddToLayerList(LayerImpl* layer);

  void ClearLayerList();

  void BuildLayerListForTesting();
  void HandleScrollbarShowRequestsFromMain();

  void InvalidateRegionForImages(
      const PaintImageIdFlatSet& images_to_invalidate);

  LayerTreeLifecycle& lifecycle() { return lifecycle_; }

 protected:
  float ClampPageScaleFactorToLimits(float page_scale_factor) const;
  void PushPageScaleFactorAndLimits(const float* page_scale_factor,
                                    float min_page_scale_factor,
                                    float max_page_scale_factor);
  bool SetPageScaleFactorLimits(float min_page_scale_factor,
                                float max_page_scale_factor);
  bool IsViewportLayerId(int id) const;
  void UpdateScrollbars(int scroll_layer_id, int clip_layer_id);
  void DidUpdatePageScale();
  void PushBrowserControls(const float* top_controls_shown_ratio);
  bool ClampBrowserControlsShownRatio();

  LayerTreeHostImpl* layer_tree_host_impl_;
  int source_frame_number_;
  int is_first_frame_after_commit_tracker_;
  LayerImpl* root_layer_for_testing_;
  HeadsUpDisplayLayerImpl* hud_layer_;
  PropertyTrees property_trees_;
  SkColor background_color_;
  bool has_transparent_background_;

  int last_scrolled_scroll_node_index_;

  ViewportLayerIds viewport_layer_ids_;

  LayerSelection selection_;

  scoped_refptr<SyncedProperty<ScaleGroup>> page_scale_factor_;
  float min_page_scale_factor_;
  float max_page_scale_factor_;

  float device_scale_factor_;
  float painted_device_scale_factor_;
  gfx::ColorSpace raster_color_space_;

  uint32_t content_source_id_;
  LocalSurfaceId local_surface_id_;

  scoped_refptr<SyncedElasticOverscroll> elastic_overscroll_;

  std::unique_ptr<OwnedLayerImplList> layers_;
  LayerImplMap layer_id_map_;
  LayerImplList layer_list_;
  // Set of layers that need to push properties.
  std::unordered_set<LayerImpl*> layers_that_should_push_properties_;

  std::unordered_map<ElementId, int, ElementIdHash> element_layers_map_;

  std::unordered_map<ElementId, float, ElementIdHash>
      element_id_to_opacity_animations_;
  std::unordered_map<ElementId, gfx::Transform, ElementIdHash>
      element_id_to_transform_animations_;
  std::unordered_map<ElementId, FilterOperations, ElementIdHash>
      element_id_to_filter_animations_;

  // Maps from clip layer ids to scroll layer ids.  Note that this only includes
  // the subset of clip layers that act as scrolling containers.  (This is
  // derived from LayerImpl::scroll_clip_layer_ and exists to avoid O(n) walks.)
  std::unordered_map<int, int> clip_scroll_map_;

  struct ScrollbarLayerIds {
    int horizontal = Layer::INVALID_ID;
    int vertical = Layer::INVALID_ID;
  };
  // Each scroll layer can have up to two scrollbar layers (vertical and
  // horizontal). This mapping is maintained as part of scrollbar registration.
  base::flat_map<ElementId, ScrollbarLayerIds>
      element_id_to_scrollbar_layer_ids_;

  std::vector<PictureLayerImpl*> picture_layers_;
  LayerImplList surface_layers_;

  // List of render surfaces for the most recently prepared frame.
  RenderSurfaceList render_surface_list_;
  // After drawing the |render_surface_list_| the areas in this region
  // would not be fully covered by opaque content.
  Region unoccluded_screen_space_region_;

  bool viewport_size_invalid_;
  bool needs_update_draw_properties_;

  // In impl-side painting mode, this is true when the tree may contain
  // structural differences relative to the active tree.
  bool needs_full_tree_sync_;

  bool next_activation_forces_redraw_;

  bool has_ever_been_drawn_;

  bool handle_visibility_changed_;

  std::vector<std::unique_ptr<SwapPromise>> swap_promise_list_;
  std::vector<std::unique_ptr<SwapPromise>> pinned_swap_promise_list_;

  UIResourceRequestQueue ui_resource_request_queue_;

  bool have_scroll_event_handlers_;
  EventListenerProperties event_listener_properties_[static_cast<size_t>(
      EventListenerClass::kNumClasses)];

  // Whether or not Blink's viewport size was shrunk by the height of the top
  // controls at the time of the last layout.
  bool browser_controls_shrink_blink_size_;
  float top_controls_height_;
  float bottom_controls_height_;

  // The amount that the browser controls are shown from 0 (hidden) to 1 (fully
  // shown).
  scoped_refptr<SyncedBrowserControls> top_controls_shown_ratio_;

  std::unique_ptr<PendingPageScaleAnimation> pending_page_scale_animation_;

  // Tracks the lifecycle which is used for enforcing dependencies between
  // lifecycle states. See: |LayerTreeLifecycle|.
  LayerTreeLifecycle lifecycle_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LayerTreeImpl);
};

}  // namespace cc

#endif  // CC_TREES_LAYER_TREE_IMPL_H_