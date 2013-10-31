/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCTreeManager.h"
#include "AsyncCompositionManager.h"    // for ViewTransform
#include "Compositor.h"                 // for Compositor
#include "CompositorParent.h"           // for CompositorParent, etc
#include "InputData.h"                  // for InputData, etc
#include "Layers.h"                     // for ContainerLayer, Layer, etc
#include "gfx3DMatrix.h"                // for gfx3DMatrix
#include "mozilla/dom/Touch.h"          // for Touch
#include "mozilla/gfx/Point.h"          // for Point
#include "mozilla/layers/AsyncPanZoomController.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/mozalloc.h"           // for operator new
#include "mozilla/TouchEvents.h"
#include "nsPoint.h"                    // for nsIntPoint
#include "nsTArray.h"                   // for nsTArray, nsTArray_Impl, etc
#include "nsThreadUtils.h"              // for NS_IsMainThread

#define APZC_LOG(...)
// #define APZC_LOG(...) printf_stderr("APZC: " __VA_ARGS__)

namespace mozilla {
namespace layers {

float APZCTreeManager::sDPI = 72.0;

APZCTreeManager::APZCTreeManager()
    : mTreeLock("APZCTreeLock")
{
  MOZ_ASSERT(NS_IsMainThread());
  AsyncPanZoomController::InitializeGlobalState();
}

APZCTreeManager::~APZCTreeManager()
{
}

void
APZCTreeManager::AssertOnCompositorThread()
{
  Compositor::AssertOnCompositorThread();
}

/* Flatten the tree of APZC instances into the given nsTArray */
static void
Collect(AsyncPanZoomController* aApzc, nsTArray< nsRefPtr<AsyncPanZoomController> >* aCollection)
{
  if (aApzc) {
    aCollection->AppendElement(aApzc);
    Collect(aApzc->GetLastChild(), aCollection);
    Collect(aApzc->GetPrevSibling(), aCollection);
  }
}

void
APZCTreeManager::UpdatePanZoomControllerTree(CompositorParent* aCompositor, Layer* aRoot,
                                             bool aIsFirstPaint, uint64_t aFirstPaintLayersId)
{
  AssertOnCompositorThread();

  MonitorAutoLock lock(mTreeLock);

  // We do this business with collecting the entire tree into an array because otherwise
  // it's very hard to determine which APZC instances need to be destroyed. In the worst
  // case, there are two scenarios: (a) a layer with an APZC is removed from the layer
  // tree and (b) a layer with an APZC is moved in the layer tree from one place to a
  // completely different place. In scenario (a) we would want to destroy the APZC while
  // walking the layer tree and noticing that the layer/APZC is no longer there. But if
  // we do that then we run into a problem in scenario (b) because we might encounter that
  // layer later during the walk. To handle both of these we have to 'remember' that the
  // layer was not found, and then do the destroy only at the end of the tree walk after
  // we are sure that the layer was removed and not just transplanted elsewhere. Doing that
  // as part of a recursive tree walk is hard and so maintaining a list and removing
  // APZCs that are still alive is much simpler.
  nsTArray< nsRefPtr<AsyncPanZoomController> > apzcsToDestroy;
  Collect(mRootApzc, &apzcsToDestroy);
  mRootApzc = nullptr;

  if (aRoot) {
    UpdatePanZoomControllerTree(aCompositor,
                                aRoot,
                                // aCompositor is null in gtest scenarios
                                aCompositor ? aCompositor->RootLayerTreeId() : 0,
                                gfx3DMatrix(), nullptr, nullptr,
                                aIsFirstPaint, aFirstPaintLayersId,
                                &apzcsToDestroy);
  }

  for (size_t i = 0; i < apzcsToDestroy.Length(); i++) {
    APZC_LOG("Destroying APZC at %p\n", apzcsToDestroy[i].get());
    apzcsToDestroy[i]->Destroy();
  }
}

AsyncPanZoomController*
APZCTreeManager::UpdatePanZoomControllerTree(CompositorParent* aCompositor,
                                             Layer* aLayer, uint64_t aLayersId,
                                             gfx3DMatrix aTransform,
                                             AsyncPanZoomController* aParent,
                                             AsyncPanZoomController* aNextSibling,
                                             bool aIsFirstPaint, uint64_t aFirstPaintLayersId,
                                             nsTArray< nsRefPtr<AsyncPanZoomController> >* aApzcsToDestroy)
{
  ContainerLayer* container = aLayer->AsContainerLayer();
  AsyncPanZoomController* apzc = nullptr;
  if (container) {
    if (container->GetFrameMetrics().IsScrollable()) {
      const CompositorParent::LayerTreeState* state = CompositorParent::GetIndirectShadowTree(aLayersId);
      if (state && state->mController.get()) {
        // If we get here, aLayer is a scrollable container layer and somebody
        // has registered a GeckoContentController for it, so we need to ensure
        // it has an APZC instance to manage its scrolling.

        apzc = container->GetAsyncPanZoomController();

        // The APZC we get off the layer may have been destroyed previously if the layer was inactive
        // or omitted from the layer tree for whatever reason from a layers update. If it later comes
        // back it will have a reference to a destroyed APZC and so we need to throw that out and make
        // a new one.
        bool newApzc = (apzc == nullptr || apzc->IsDestroyed());
        if (newApzc) {
          apzc = new AsyncPanZoomController(aLayersId, this, state->mController,
                                            AsyncPanZoomController::USE_GESTURE_DETECTOR);
          apzc->SetCompositorParent(aCompositor);
        } else {
          // If there was already an APZC for the layer clear the tree pointers
          // so that it doesn't continue pointing to APZCs that should no longer
          // be in the tree. These pointers will get reset properly as we continue
          // building the tree. Also remove it from the set of APZCs that are going
          // to be destroyed, because it's going to remain active.
          aApzcsToDestroy->RemoveElement(apzc);
          apzc->SetPrevSibling(nullptr);
          apzc->SetLastChild(nullptr);
        }
        APZC_LOG("Using APZC %p for layer %p with identifiers %lld %lld\n", apzc, aLayer, aLayersId, container->GetFrameMetrics().mScrollId);

        apzc->NotifyLayersUpdated(container->GetFrameMetrics(),
                                        aIsFirstPaint && (aLayersId == aFirstPaintLayersId));

        ScreenRect visible(container->GetFrameMetrics().mCompositionBounds);
        apzc->SetLayerHitTestData(visible, aTransform, aLayer->GetTransform());
        APZC_LOG("Setting rect(%f %f %f %f) as visible region for APZC %p\n", visible.x, visible.y,
                                                                              visible.width, visible.height,
                                                                              apzc);

        // Bind the APZC instance into the tree of APZCs
        if (aNextSibling) {
          aNextSibling->SetPrevSibling(apzc);
        } else if (aParent) {
          aParent->SetLastChild(apzc);
        } else {
          mRootApzc = apzc;
        }

        // Let this apzc be the parent of other controllers when we recurse downwards
        aParent = apzc;

        if (newApzc && apzc->IsRootForLayersId()) {
          // If we just created a new apzc that is the root for its layers ID, then
          // we need to update its zoom constraints which might have arrived before this
          // was created
          bool allowZoom;
          CSSToScreenScale minZoom, maxZoom;
          if (state->mController->GetZoomConstraints(&allowZoom, &minZoom, &maxZoom)) {
            apzc->UpdateZoomConstraints(allowZoom, minZoom, maxZoom);
          }
        }
      }
    }

    container->SetAsyncPanZoomController(apzc);
  }

  // Accumulate the CSS transform between layers that have an APZC, but exclude any
  // any layers that do have an APZC, and reset the accumulation at those layers.
  if (apzc) {
    aTransform = gfx3DMatrix();
  } else {
    // Multiply child layer transforms on the left so they get applied first
    aTransform = aLayer->GetTransform() * aTransform;
  }

  uint64_t childLayersId = (aLayer->AsRefLayer() ? aLayer->AsRefLayer()->GetReferentId() : aLayersId);
  AsyncPanZoomController* next = nullptr;
  for (Layer* child = aLayer->GetLastChild(); child; child = child->GetPrevSibling()) {
    next = UpdatePanZoomControllerTree(aCompositor, child, childLayersId, aTransform, aParent, next,
                                       aIsFirstPaint, aFirstPaintLayersId, aApzcsToDestroy);
  }

  // Return the APZC that should be the sibling of other APZCs as we continue
  // moving towards the first child at this depth in the layer tree.
  // If this layer doesn't have an APZC, we promote any APZCs in the subtree
  // upwards. Otherwise we fall back to the aNextSibling that was passed in.
  if (apzc) {
    return apzc;
  }
  if (next) {
    return next;
  }
  return aNextSibling;
}

/*static*/ template<class T> void
ApplyTransform(gfx::PointTyped<T>* aPoint, const gfx3DMatrix& aMatrix)
{
  gfxPoint result = aMatrix.Transform(gfxPoint(aPoint->x, aPoint->y));
  aPoint->x = result.x;
  aPoint->y = result.y;
}

/*static*/ template<class T> void
ApplyTransform(gfx::IntPointTyped<T>* aPoint, const gfx3DMatrix& aMatrix)
{
  gfxPoint result = aMatrix.Transform(gfxPoint(aPoint->x, aPoint->y));
  aPoint->x = NS_lround(result.x);
  aPoint->y = NS_lround(result.y);
}

/*static*/ void
ApplyTransform(nsIntPoint* aPoint, const gfx3DMatrix& aMatrix)
{
  gfxPoint result = aMatrix.Transform(gfxPoint(aPoint->x, aPoint->y));
  aPoint->x = NS_lround(result.x);
  aPoint->y = NS_lround(result.y);
}

nsEventStatus
APZCTreeManager::ReceiveInputEvent(const InputData& aEvent)
{
  nsEventStatus result = nsEventStatus_eIgnore;
  gfx3DMatrix transformToApzc;
  gfx3DMatrix transformToGecko;
  switch (aEvent.mInputType) {
    case MULTITOUCH_INPUT: {
      const MultiTouchInput& multiTouchInput = aEvent.AsMultiTouchInput();
      if (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_START) {
        mApzcForInputBlock = GetTargetAPZC(ScreenPoint(multiTouchInput.mTouches[0].mScreenPoint));
        for (size_t i = 1; i < multiTouchInput.mTouches.Length(); i++) {
          nsRefPtr<AsyncPanZoomController> apzc2 = GetTargetAPZC(ScreenPoint(multiTouchInput.mTouches[i].mScreenPoint));
          mApzcForInputBlock = CommonAncestor(mApzcForInputBlock.get(), apzc2.get());
          APZC_LOG("Using APZC %p as the common ancestor\n", mApzcForInputBlock.get());
          // For now, we only ever want to do pinching on the root APZC for a given layers id. So
          // when we find the common ancestor of multiple points, also walk up to the root APZC.
          mApzcForInputBlock = RootAPZCForLayersId(mApzcForInputBlock);
          APZC_LOG("Using APZC %p as the root APZC for multi-touch\n", mApzcForInputBlock.get());
        }

        // Cache transformToApzc so it can be used for future events in this block.
        if (mApzcForInputBlock) {
          GetInputTransforms(mApzcForInputBlock, transformToApzc, transformToGecko);
          mCachedTransformToApzcForInputBlock = transformToApzc;
        }
      } else if (mApzcForInputBlock) {
        APZC_LOG("Re-using APZC %p as continuation of event block\n", mApzcForInputBlock.get());
      }
      if (mApzcForInputBlock) {
        // Use the cached transform to compute the point to send to the APZC.
        // This ensures that the sequence of touch points an APZC sees in an
        // input block are all in the same coordinate space.
        transformToApzc = mCachedTransformToApzcForInputBlock;
        MultiTouchInput inputForApzc(multiTouchInput);
        for (size_t i = 0; i < inputForApzc.mTouches.Length(); i++) {
          ApplyTransform(&(inputForApzc.mTouches[i].mScreenPoint), transformToApzc);
        }
        result = mApzcForInputBlock->ReceiveInputEvent(inputForApzc);
        // If we have an mApzcForInputBlock and it's the end of the touch sequence
        // then null it out so we don't keep a dangling reference and leak things.
        if (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_CANCEL ||
            (multiTouchInput.mType == MultiTouchInput::MULTITOUCH_END && multiTouchInput.mTouches.Length() == 1)) {
          mApzcForInputBlock = nullptr;
        }
      }
      break;
    } case PINCHGESTURE_INPUT: {
      const PinchGestureInput& pinchInput = aEvent.AsPinchGestureInput();
      nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(pinchInput.mFocusPoint);
      if (apzc) {
        GetInputTransforms(apzc, transformToApzc, transformToGecko);
        PinchGestureInput inputForApzc(pinchInput);
        ApplyTransform(&(inputForApzc.mFocusPoint), transformToApzc);
        result = apzc->ReceiveInputEvent(inputForApzc);
      }
      break;
    } case TAPGESTURE_INPUT: {
      const TapGestureInput& tapInput = aEvent.AsTapGestureInput();
      nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(ScreenPoint(tapInput.mPoint));
      if (apzc) {
        GetInputTransforms(apzc, transformToApzc, transformToGecko);
        TapGestureInput inputForApzc(tapInput);
        ApplyTransform(&(inputForApzc.mPoint), transformToApzc);
        result = apzc->ReceiveInputEvent(inputForApzc);
      }
      break;
    }
  }
  return result;
}

AsyncPanZoomController*
APZCTreeManager::GetTouchInputBlockAPZC(const WidgetTouchEvent& aEvent,
                                        ScreenPoint aPoint)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aPoint);
  gfx3DMatrix transformToApzc, transformToGecko;
  // Reset the cached apz transform
  mCachedTransformToApzcForInputBlock = transformToApzc;
  if (!apzc) {
    return nullptr;
  }
  for (size_t i = 1; i < aEvent.touches.Length(); i++) {
    nsIntPoint point = aEvent.touches[i]->mRefPoint;
    nsRefPtr<AsyncPanZoomController> apzc2 =
      GetTargetAPZC(ScreenPoint(point.x, point.y));
    apzc = CommonAncestor(apzc.get(), apzc2.get());
    APZC_LOG("Using APZC %p as the common ancestor\n", apzc.get());
    // For now, we only ever want to do pinching on the root APZC for a given layers id. So
    // when we find the common ancestor of multiple points, also walk up to the root APZC.
    apzc = RootAPZCForLayersId(apzc);
    APZC_LOG("Using APZC %p as the root APZC for multi-touch\n", apzc.get());
  }
  if (apzc) {
    // Cache apz transform so it can be used for future events in this block.
    GetInputTransforms(apzc, mCachedTransformToApzcForInputBlock, transformToGecko);
  }
  return apzc.get();
}

nsEventStatus
APZCTreeManager::ProcessTouchEvent(const WidgetTouchEvent& aEvent,
                                   WidgetTouchEvent* aOutEvent)
{
  // For computing the input for the APZC, used the cached transform.
  // This ensures that the sequence of touch points an APZC sees in an
  // input block are all in the same coordinate space.
  gfx3DMatrix transformToApzc = mCachedTransformToApzcForInputBlock;
  MultiTouchInput inputForApzc(aEvent);
  for (size_t i = 0; i < inputForApzc.mTouches.Length(); i++) {
    ApplyTransform(&(inputForApzc.mTouches[i].mScreenPoint), transformToApzc);
  }
  nsEventStatus ret = mApzcForInputBlock->ReceiveInputEvent(inputForApzc);

  // For computing the event to pass back to Gecko, use the up-to-date transforms.
  // This ensures that transformToApzc and transformToGecko are in sync
  // (note that transformToGecko isn't cached).
  gfx3DMatrix transformToGecko;
  GetInputTransforms(mApzcForInputBlock, transformToApzc, transformToGecko);
  gfx3DMatrix outTransform = transformToApzc * transformToGecko;
  for (size_t i = 0; i < aOutEvent->touches.Length(); i++) {
    ApplyTransform(&(aOutEvent->touches[i]->mRefPoint), outTransform);
  }

  // If we have an mApzcForInputBlock and it's the end of the touch sequence
  // then null it out so we don't keep a dangling reference and leak things.
  if (aEvent.message == NS_TOUCH_CANCEL ||
      (aEvent.message == NS_TOUCH_END && aEvent.touches.Length() == 1)) {
    mApzcForInputBlock = nullptr;
  }
  return ret;
}

nsEventStatus
APZCTreeManager::ProcessMouseEvent(const WidgetMouseEvent& aEvent,
                                   WidgetMouseEvent* aOutEvent)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(ScreenPoint(aEvent.refPoint.x, aEvent.refPoint.y));
  if (!apzc) {
    return nsEventStatus_eIgnore;
  }
  gfx3DMatrix transformToApzc;
  gfx3DMatrix transformToGecko;
  GetInputTransforms(apzc, transformToApzc, transformToGecko);
  MultiTouchInput inputForApzc(aEvent);
  ApplyTransform(&(inputForApzc.mTouches[0].mScreenPoint), transformToApzc);
  gfx3DMatrix outTransform = transformToApzc * transformToGecko;
  ApplyTransform(&aOutEvent->refPoint, outTransform);
  return apzc->ReceiveInputEvent(inputForApzc);
}

nsEventStatus
APZCTreeManager::ProcessEvent(const WidgetInputEvent& aEvent,
                              WidgetInputEvent* aOutEvent)
{
  // Transform the refPoint
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(ScreenPoint(aEvent.refPoint.x, aEvent.refPoint.y));
  if (!apzc) {
    return nsEventStatus_eIgnore;
  }
  gfx3DMatrix transformToApzc;
  gfx3DMatrix transformToGecko;
  GetInputTransforms(apzc, transformToApzc, transformToGecko);
  ApplyTransform(&(aOutEvent->refPoint), transformToApzc);
  gfx3DMatrix outTransform = transformToApzc * transformToGecko;
  ApplyTransform(&(aOutEvent->refPoint), outTransform);
  return nsEventStatus_eIgnore;
}

nsEventStatus
APZCTreeManager::ReceiveInputEvent(const WidgetInputEvent& aEvent,
                                   WidgetInputEvent* aOutEvent)
{
  MOZ_ASSERT(NS_IsMainThread());

  switch (aEvent.eventStructType) {
    case NS_TOUCH_EVENT: {
      const WidgetTouchEvent& touchEvent = *aEvent.AsTouchEvent();
      if (!touchEvent.touches.Length()) {
        return nsEventStatus_eIgnore;
      }
      if (touchEvent.message == NS_TOUCH_START) {
        ScreenPoint point = ScreenPoint(touchEvent.touches[0]->mRefPoint.x, touchEvent.touches[0]->mRefPoint.y);
        mApzcForInputBlock = GetTouchInputBlockAPZC(touchEvent, point);
      }
      if (!mApzcForInputBlock) {
        return nsEventStatus_eIgnore;
      }
      return ProcessTouchEvent(touchEvent, aOutEvent->AsTouchEvent());
    }
    case NS_MOUSE_EVENT: {
      // For b2g emulation
      const WidgetMouseEvent& mouseEvent = *aEvent.AsMouseEvent();
      WidgetMouseEvent* outMouseEvent = aOutEvent->AsMouseEvent();
      return ProcessMouseEvent(mouseEvent, outMouseEvent);
    }
    default: {
      return ProcessEvent(aEvent, aOutEvent);
    }
  }
}

nsEventStatus
APZCTreeManager::ReceiveInputEvent(WidgetInputEvent& aEvent)
{
  MOZ_ASSERT(NS_IsMainThread());

  switch (aEvent.eventStructType) {
    case NS_TOUCH_EVENT: {
      WidgetTouchEvent& touchEvent = *aEvent.AsTouchEvent();
      if (!touchEvent.touches.Length()) {
        return nsEventStatus_eIgnore;
      }
      if (touchEvent.message == NS_TOUCH_START) {
        ScreenPoint point = ScreenPoint(touchEvent.touches[0]->mRefPoint.x, touchEvent.touches[0]->mRefPoint.y);
        mApzcForInputBlock = GetTouchInputBlockAPZC(touchEvent, point);
      }
      if (!mApzcForInputBlock) {
        return nsEventStatus_eIgnore;
      }
      return ProcessTouchEvent(touchEvent, &touchEvent);
    }
    default: {
      return ProcessEvent(aEvent, &aEvent);
    }
  }
}

void
APZCTreeManager::UpdateCompositionBounds(const ScrollableLayerGuid& aGuid,
                                         const ScreenIntRect& aCompositionBounds)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->UpdateCompositionBounds(aCompositionBounds);
  }
}

void
APZCTreeManager::ZoomToRect(const ScrollableLayerGuid& aGuid,
                            const CSSRect& aRect)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->ZoomToRect(aRect);
  }
}

void
APZCTreeManager::ContentReceivedTouch(const ScrollableLayerGuid& aGuid,
                                      bool aPreventDefault)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->ContentReceivedTouch(aPreventDefault);
  }
}

void
APZCTreeManager::UpdateZoomConstraints(const ScrollableLayerGuid& aGuid,
                                       bool aAllowZoom,
                                       const CSSToScreenScale& aMinScale,
                                       const CSSToScreenScale& aMaxScale)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->UpdateZoomConstraints(aAllowZoom, aMinScale, aMaxScale);
  }
}

void
APZCTreeManager::UpdateScrollOffset(const ScrollableLayerGuid& aGuid,
                                    const CSSPoint& aScrollOffset)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->UpdateScrollOffset(aScrollOffset);
  }
}

void
APZCTreeManager::CancelAnimation(const ScrollableLayerGuid &aGuid)
{
  nsRefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (apzc) {
    apzc->CancelAnimation();
  }
}

void
APZCTreeManager::ClearTree()
{
  MonitorAutoLock lock(mTreeLock);

  // This can be done as part of a tree walk but it's easier to
  // just re-use the Collect method that we need in other places.
  // If this is too slow feel free to change it to a recursive walk.
  nsTArray< nsRefPtr<AsyncPanZoomController> > apzcsToDestroy;
  Collect(mRootApzc, &apzcsToDestroy);
  for (size_t i = 0; i < apzcsToDestroy.Length(); i++) {
    apzcsToDestroy[i]->Destroy();
  }
  mRootApzc = nullptr;
}

void
APZCTreeManager::HandleOverscroll(AsyncPanZoomController* aChild, ScreenPoint aStartPoint, ScreenPoint aEndPoint)
{
  AsyncPanZoomController* parent = aChild->GetParent();
  if (parent == nullptr)
    return;

  gfx3DMatrix transformToApzc;
  gfx3DMatrix transformToGecko;  // ignored

  // Convert start and end points to untransformed screen coordinates.
  GetInputTransforms(aChild, transformToApzc, transformToGecko);
  ApplyTransform(&aStartPoint, transformToApzc.Inverse());
  ApplyTransform(&aEndPoint, transformToApzc.Inverse());

  // Convert start and end points to parent's transformed screen coordinates.
  GetInputTransforms(parent, transformToApzc, transformToGecko);
  ApplyTransform(&aStartPoint, transformToApzc);
  ApplyTransform(&aEndPoint, transformToApzc);

  parent->AttemptScroll(aStartPoint, aEndPoint);
}

bool
APZCTreeManager::HitTestAPZC(const ScreenPoint& aPoint)
{
  MonitorAutoLock lock(mTreeLock);
  nsRefPtr<AsyncPanZoomController> target;
  // The root may have siblings, so check those too
  gfxPoint point(aPoint.x, aPoint.y);
  for (AsyncPanZoomController* apzc = mRootApzc; apzc; apzc = apzc->GetPrevSibling()) {
    target = GetAPZCAtPoint(apzc, point);
    if (target) {
      return true;
    }
  }
  return false;
}

already_AddRefed<AsyncPanZoomController>
APZCTreeManager::GetTargetAPZC(const ScrollableLayerGuid& aGuid)
{
  MonitorAutoLock lock(mTreeLock);
  nsRefPtr<AsyncPanZoomController> target;
  // The root may have siblings, check those too
  for (AsyncPanZoomController* apzc = mRootApzc; apzc; apzc = apzc->GetPrevSibling()) {
    target = FindTargetAPZC(apzc, aGuid);
    if (target) {
      break;
    }
  }
  return target.forget();
}

already_AddRefed<AsyncPanZoomController>
APZCTreeManager::GetTargetAPZC(const ScreenPoint& aPoint)
{
  MonitorAutoLock lock(mTreeLock);
  nsRefPtr<AsyncPanZoomController> target;
  // The root may have siblings, so check those too
  gfxPoint point(aPoint.x, aPoint.y);
  for (AsyncPanZoomController* apzc = mRootApzc; apzc; apzc = apzc->GetPrevSibling()) {
    target = GetAPZCAtPoint(apzc, point);
    if (target) {
      break;
    }
  }
  return target.forget();
}

AsyncPanZoomController*
APZCTreeManager::FindTargetAPZC(AsyncPanZoomController* aApzc, const ScrollableLayerGuid& aGuid) {
  // This walks the tree in depth-first, reverse order, so that it encounters
  // APZCs front-to-back on the screen.
  for (AsyncPanZoomController* child = aApzc->GetLastChild(); child; child = child->GetPrevSibling()) {
    AsyncPanZoomController* match = FindTargetAPZC(child, aGuid);
    if (match) {
      return match;
    }
  }

  if (aApzc->Matches(aGuid)) {
    return aApzc;
  }
  return nullptr;
}

AsyncPanZoomController*
APZCTreeManager::GetAPZCAtPoint(AsyncPanZoomController* aApzc, const gfxPoint& aHitTestPoint)
{
  // The comments below assume there is a chain of layers L..R with L and P having APZC instances as
  // explained in the comment on GetInputTransforms. This function will recurse with aApzc at L and P, and the
  // comments explain what values are stored in the variables at these two levels. All the comments
  // use standard matrix notation where the leftmost matrix in a multiplication is applied first.

  // ancestorUntransform takes points from aApzc's parent APZC's screen coordinates
  // to aApzc's screen coordinates.
  // It is OC.Inverse() * NC.Inverse() * MC.Inverse() at recursion level for L,
  //   and RC.Inverse() * QC.Inverse()                at recursion level for P.
  gfx3DMatrix ancestorUntransform = aApzc->GetAncestorTransform().Inverse();

  // Hit testing for this layer is performed in aApzc's screen coordinates.
  gfxPoint hitTestPointForThisLayer = ancestorUntransform.ProjectPoint(aHitTestPoint);
  APZC_LOG("Untransformed %f %f to screen coordinates %f %f for hit-testing APZC %p\n",
           aHitTestPoint.x, aHitTestPoint.y,
           hitTestPointForThisLayer.x, hitTestPointForThisLayer.y, aApzc);

  // myUntransform takes points from aApzc's screen coordinates
  // to aApzc's layer coordinates (which are aApzc's children's screen coordinates).
  // It is LA.Inverse() * LC.Inverse() at L
  //   and PA.Inverse() * PC.Inverse() at P.
  gfx3DMatrix myUntransform = gfx3DMatrix(aApzc->GetCurrentAsyncTransform()).Inverse()
                            * aApzc->GetCSSTransform().Inverse();

  // Hit testing for child layers is performed in aApzc's layer coordinates.
  gfxPoint hitTestPointForChildLayers = myUntransform.ProjectPoint(hitTestPointForThisLayer);
  APZC_LOG("Untransformed %f %f to layer coordinates %f %f for APZC %p\n",
           aHitTestPoint.x, aHitTestPoint.y,
           hitTestPointForChildLayers.x, hitTestPointForChildLayers.y, aApzc);

  // This walks the tree in depth-first, reverse order, so that it encounters
  // APZCs front-to-back on the screen.
  for (AsyncPanZoomController* child = aApzc->GetLastChild(); child; child = child->GetPrevSibling()) {
    AsyncPanZoomController* match = GetAPZCAtPoint(child, hitTestPointForChildLayers);
    if (match) {
      return match;
    }
  }
  if (aApzc->VisibleRegionContains(ScreenPoint(hitTestPointForThisLayer.x, hitTestPointForThisLayer.y))) {
    APZC_LOG("Successfully matched untransformed point %f %f to visible region for APZC %p\n",
             hitTestPointForThisLayer.x, hitTestPointForThisLayer.y, aApzc);
    return aApzc;
  }
  return nullptr;
}

/* This function sets the aTransformToApzcOut and aTransformToGeckoOut out-parameters
   to some useful transformations that input events may need applied. This is best
   illustrated with an example. Consider a chain of layers, L, M, N, O, P, Q, R. Layer L
   is the layer that corresponds to the returned APZC instance, and layer R is the root
   of the layer tree. Layer M is the parent of L, N is the parent of M, and so on.
   When layer L is displayed to the screen by the compositor, the set of transforms that
   are applied to L are (in order from top to bottom):

        L's CSS transform      (hereafter referred to as transform matrix LC)
        L's async transform    (hereafter referred to as transform matrix LA)
        M's CSS transform      (hereafter referred to as transform matrix MC)
        M's async transform    (hereafter referred to as transform matrix MA)
        ...
        R's CSS transform      (hereafter referred to as transform matrix RC)
        R's async transform    (hereafter referred to as transform matrix RA)

   Therefore, if we want user input to modify L's async transform, we have to first convert
   user input from screen space to the coordinate space of L's async transform. Doing this
   involves applying the following transforms (in order from top to bottom):
        RA.Inverse()
        RC.Inverse()
        ...
        MA.Inverse()
        MC.Inverse()
   This combined transformation is returned in the aTransformToApzcOut out-parameter.

   Next, if we want user inputs sent to gecko for event-dispatching, we will need to strip
   out all of the async transforms that are involved in this chain. This is because async
   transforms are stored only in the compositor and gecko does not account for them when
   doing display-list-based hit-testing for event dispatching. Therefore, given a user input
   in screen space, the following transforms need to be applied (in order from top to bottom):
        RA.Inverse()
        RC.Inverse()
        ...
        MA.Inverse()
        MC.Inverse()
        LA.Inverse()
        LC.Inverse()
        LC
        MC
        ...
        RC
   This sequence can be simplified and refactored to the following:
        aTransformToApzcOut
        LA.Inverse()
        MC
        ...
        RC
   Since aTransformToApzcOut is already one of the out-parameters, we set aTransformToGeckoOut
   to the remaining transforms (LA.Inverse() * MC * ... * RC), so that the caller code can
   combine it with aTransformToApzcOut to get the final transform required in this case.

   Note that for many of these layers, there will be no AsyncPanZoomController attached, and
   so the async transform will be the identity transform. So, in the example above, if layers
   L and P have APZC instances attached, MA, NA, OA, QA, and RA will be identity transforms.
   Additionally, for space-saving purposes, each APZC instance stores its layers individual
   CSS transform and the accumulation of CSS transforms to its parent APZC. So the APZC for
   layer L would store LC and (MC * NC * OC), and the layer P would store PC and (QC * RC).
   The APZCs also obviously have LA and PA, so all of the above transformation combinations
   required can be generated.
 */
void
APZCTreeManager::GetInputTransforms(AsyncPanZoomController *aApzc, gfx3DMatrix& aTransformToApzcOut,
                                    gfx3DMatrix& aTransformToGeckoOut)
{
  // The comments below assume there is a chain of layers L..R with L and P having APZC instances as
  // explained in the comment above. This function is called with aApzc at L, and the loop
  // below performs one iteration, where parent is at P. The comments explain what values are stored
  // in the variables at these two levels. All the comments use standard matrix notation where the
  // leftmost matrix in a multiplication is applied first.

  // ancestorUntransform is OC.Inverse() * NC.Inverse() * MC.Inverse()
  gfx3DMatrix ancestorUntransform = aApzc->GetAncestorTransform().Inverse();
  // asyncUntransform is LA.Inverse()
  gfx3DMatrix asyncUntransform = gfx3DMatrix(aApzc->GetCurrentAsyncTransform()).Inverse();

  // aTransformToApzcOut is initialized to OC.Inverse() * NC.Inverse() * MC.Inverse()
  aTransformToApzcOut = ancestorUntransform;
  // aTransformToGeckoOut is initialized to LA.Inverse() * MC * NC * OC
  aTransformToGeckoOut = asyncUntransform * aApzc->GetAncestorTransform();

  for (AsyncPanZoomController* parent = aApzc->GetParent(); parent; parent = parent->GetParent()) {
    // ancestorUntransform is updated to RC.Inverse() * QC.Inverse() when parent == P
    ancestorUntransform = parent->GetAncestorTransform().Inverse();
    // asyncUntransform is updated to PA.Inverse() when parent == P
    asyncUntransform = gfx3DMatrix(parent->GetCurrentAsyncTransform()).Inverse();
    // untransformSinceLastApzc is RC.Inverse() * QC.Inverse() * PA.Inverse() * PC.Inverse()
    gfx3DMatrix untransformSinceLastApzc = ancestorUntransform * asyncUntransform * parent->GetCSSTransform().Inverse();

    // aTransformToApzcOut is RC.Inverse() * QC.Inverse() * PA.Inverse() * PC.Inverse() * OC.Inverse() * NC.Inverse() * MC.Inverse()
    aTransformToApzcOut = untransformSinceLastApzc * aTransformToApzcOut;
    // aTransformToGeckoOut is LA.Inverse() * MC * NC * OC * PC * QC * RC
    aTransformToGeckoOut = aTransformToGeckoOut * parent->GetCSSTransform() * parent->GetAncestorTransform();

    // The above values for aTransformToApzcOut and aTransformToGeckoOut when parent == P match
    // the required output as explained in the comment above GetTargetAPZC. Note that any missing terms
    // are async transforms that are guaranteed to be identity transforms.
  }
}

AsyncPanZoomController*
APZCTreeManager::CommonAncestor(AsyncPanZoomController* aApzc1, AsyncPanZoomController* aApzc2)
{
  // If either aApzc1 or aApzc2 is null, min(depth1, depth2) will be 0 and this function
  // will return null.

  // Calculate depth of the APZCs in the tree
  int depth1 = 0, depth2 = 0;
  for (AsyncPanZoomController* parent = aApzc1; parent; parent = parent->GetParent()) {
    depth1++;
  }
  for (AsyncPanZoomController* parent = aApzc2; parent; parent = parent->GetParent()) {
    depth2++;
  }

  // At most one of the following two loops will be executed; the deeper APZC pointer
  // will get walked up to the depth of the shallower one.
  int minDepth = depth1 < depth2 ? depth1 : depth2;
  while (depth1 > minDepth) {
    depth1--;
    aApzc1 = aApzc1->GetParent();
  }
  while (depth2 > minDepth) {
    depth2--;
    aApzc2 = aApzc2->GetParent();
  }

  // Walk up the ancestor chains of both APZCs, always staying at the same depth for
  // either APZC, and return the the first common ancestor encountered.
  while (true) {
    if (aApzc1 == aApzc2) {
      return aApzc1;
    }
    if (depth1 <= 0) {
      break;
    }
    aApzc1 = aApzc1->GetParent();
    aApzc2 = aApzc2->GetParent();
  }
  return nullptr;
}

AsyncPanZoomController*
APZCTreeManager::RootAPZCForLayersId(AsyncPanZoomController* aApzc)
{
  while (aApzc && !aApzc->IsRootForLayersId()) {
    aApzc = aApzc->GetParent();
  }
  return aApzc;
}

}
}
