/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ContentHost.h"
#include "LayersLogging.h"              // for AppendToString
#include "gfx2DGlue.h"                  // for ContentForFormat
#include "gfxPoint.h"                   // for gfxIntSize
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/gfx/BaseRect.h"       // for BaseRect
#include "mozilla/layers/Compositor.h"  // for Compositor
#include "mozilla/layers/Effects.h"     // for TexturedEffect, Effect, etc
#include "mozilla/layers/LayersMessages.h"  // for ThebesBufferData
#include "nsAString.h"
#include "nsPrintfCString.h"            // for nsPrintfCString
#include "nsString.h"                   // for nsAutoCString

class gfxImageSurface;

namespace mozilla {
namespace gfx {
class Matrix4x4;
}
using namespace gfx;

namespace layers {

ContentHostBase::ContentHostBase(const TextureInfo& aTextureInfo)
  : ContentHost(aTextureInfo)
  , mPaintWillResample(false)
  , mInitialised(false)
{}

ContentHostBase::~ContentHostBase()
{}

DeprecatedTextureHost*
ContentHostBase::GetDeprecatedTextureHost()
{
  return mDeprecatedTextureHost;
}

void
ContentHostBase::DestroyFrontHost()
{
  MOZ_ASSERT(!mDeprecatedTextureHost || mDeprecatedTextureHost->GetDeAllocator(),
             "We won't be able to destroy our SurfaceDescriptor");
  MOZ_ASSERT(!mDeprecatedTextureHostOnWhite || mDeprecatedTextureHostOnWhite->GetDeAllocator(),
             "We won't be able to destroy our SurfaceDescriptor");
  mDeprecatedTextureHost = nullptr;
  mDeprecatedTextureHostOnWhite = nullptr;
}

void
ContentHostBase::Composite(EffectChain& aEffectChain,
                           float aOpacity,
                           const gfx::Matrix4x4& aTransform,
                           const Filter& aFilter,
                           const Rect& aClipRect,
                           const nsIntRegion* aVisibleRegion,
                           TiledLayerProperties* aLayerProperties)
{
  NS_ASSERTION(aVisibleRegion, "Requires a visible region");

  AutoLockDeprecatedTextureHost lock(mDeprecatedTextureHost);
  AutoLockDeprecatedTextureHost lockOnWhite(mDeprecatedTextureHostOnWhite);

  if (!mDeprecatedTextureHost ||
      !lock.IsValid() ||
      !lockOnWhite.IsValid()) {
    return;
  }

  RefPtr<TexturedEffect> effect =
    CreateTexturedEffect(mDeprecatedTextureHost, mDeprecatedTextureHostOnWhite, aFilter);

  aEffectChain.mPrimaryEffect = effect;

  nsIntRegion tmpRegion;
  const nsIntRegion* renderRegion;
  if (PaintWillResample()) {
    // If we're resampling, then the texture image will contain exactly the
    // entire visible region's bounds, and we should draw it all in one quad
    // to avoid unexpected aliasing.
    tmpRegion = aVisibleRegion->GetBounds();
    renderRegion = &tmpRegion;
  } else {
    renderRegion = aVisibleRegion;
  }

  nsIntRegion region(*renderRegion);
  nsIntPoint origin = GetOriginOffset();
  region.MoveBy(-origin);           // translate into TexImage space, buffer origin might not be at texture (0,0)

  // Figure out the intersecting draw region
  TextureSource* source = mDeprecatedTextureHost;
  MOZ_ASSERT(source);
  gfx::IntSize texSize = source->GetSize();
  nsIntRect textureRect = nsIntRect(0, 0, texSize.width, texSize.height);
  textureRect.MoveBy(region.GetBounds().TopLeft());
  nsIntRegion subregion;
  subregion.And(region, textureRect);
  if (subregion.IsEmpty()) {
    // Region is empty, nothing to draw
    return;
  }

  nsIntRegion screenRects;
  nsIntRegion regionRects;

  // Collect texture/screen coordinates for drawing
  nsIntRegionRectIterator iter(subregion);
  while (const nsIntRect* iterRect = iter.Next()) {
    nsIntRect regionRect = *iterRect;
    nsIntRect screenRect = regionRect;
    screenRect.MoveBy(origin);

    screenRects.Or(screenRects, screenRect);
    regionRects.Or(regionRects, regionRect);
  }

  TileIterator* tileIter = source->AsTileIterator();
  TileIterator* iterOnWhite = nullptr;
  if (tileIter) {
    tileIter->BeginTileIteration();
  }

  if (mDeprecatedTextureHostOnWhite) {
    iterOnWhite = mDeprecatedTextureHostOnWhite->AsTileIterator();
    MOZ_ASSERT(!tileIter || tileIter->GetTileCount() == iterOnWhite->GetTileCount(),
               "Tile count mismatch on component alpha texture");
    if (iterOnWhite) {
      iterOnWhite->BeginTileIteration();
    }
  }

  bool usingTiles = (tileIter && tileIter->GetTileCount() > 1);
  do {
    if (iterOnWhite) {
      MOZ_ASSERT(iterOnWhite->GetTileRect() == tileIter->GetTileRect(),
                 "component alpha textures should be the same size.");
    }

    nsIntRect texRect = tileIter ? tileIter->GetTileRect()
                                 : nsIntRect(0, 0,
                                             texSize.width,
                                             texSize.height);

    // Draw texture. If we're using tiles, we do repeating manually, as texture
    // repeat would cause each individual tile to repeat instead of the
    // compound texture as a whole. This involves drawing at most 4 sections,
    // 2 for each axis that has texture repeat.
    for (int y = 0; y < (usingTiles ? 2 : 1); y++) {
      for (int x = 0; x < (usingTiles ? 2 : 1); x++) {
        nsIntRect currentTileRect(texRect);
        currentTileRect.MoveBy(x * texSize.width, y * texSize.height);

        nsIntRegionRectIterator screenIter(screenRects);
        nsIntRegionRectIterator regionIter(regionRects);

        const nsIntRect* screenRect;
        const nsIntRect* regionRect;
        while ((screenRect = screenIter.Next()) &&
               (regionRect = regionIter.Next())) {
            nsIntRect tileScreenRect(*screenRect);
            nsIntRect tileRegionRect(*regionRect);

            // When we're using tiles, find the intersection between the tile
            // rect and this region rect. Tiling is then handled by the
            // outer for-loops and modifying the tile rect.
            if (usingTiles) {
                tileScreenRect.MoveBy(-origin);
                tileScreenRect = tileScreenRect.Intersect(currentTileRect);
                tileScreenRect.MoveBy(origin);

                if (tileScreenRect.IsEmpty())
                  continue;

                tileRegionRect = regionRect->Intersect(currentTileRect);
                tileRegionRect.MoveBy(-currentTileRect.TopLeft());
            }
            gfx::Rect rect(tileScreenRect.x, tileScreenRect.y,
                           tileScreenRect.width, tileScreenRect.height);

            effect->mTextureCoords = Rect(Float(tileRegionRect.x) / texRect.width,
                                          Float(tileRegionRect.y) / texRect.height,
                                          Float(tileRegionRect.width) / texRect.width,
                                          Float(tileRegionRect.height) / texRect.height);
            GetCompositor()->DrawQuad(rect, aClipRect, aEffectChain, aOpacity, aTransform);
            if (usingTiles) {
              DiagnosticTypes diagnostics = DIAGNOSTIC_CONTENT | DIAGNOSTIC_BIGIMAGE;
              diagnostics |= iterOnWhite ? DIAGNOSTIC_COMPONENT_ALPHA : 0;
              GetCompositor()->DrawDiagnostics(diagnostics, rect, aClipRect,
                                               aTransform);
            }
        }
      }
    }

    if (iterOnWhite) {
      iterOnWhite->NextTile();
    }
  } while (usingTiles && tileIter->NextTile());

  if (tileIter) {
    tileIter->EndTileIteration();
  }
  if (iterOnWhite) {
    iterOnWhite->EndTileIteration();
  }

  DiagnosticTypes diagnostics = DIAGNOSTIC_CONTENT;
  diagnostics |= iterOnWhite ? DIAGNOSTIC_COMPONENT_ALPHA : 0;
  GetCompositor()->DrawDiagnostics(diagnostics, *aVisibleRegion, aClipRect, aTransform);
}

void
ContentHostBase::SetCompositor(Compositor* aCompositor)
{
  CompositableHost::SetCompositor(aCompositor);
  if (mDeprecatedTextureHost) {
    mDeprecatedTextureHost->SetCompositor(aCompositor);
  }
  if (mDeprecatedTextureHostOnWhite) {
    mDeprecatedTextureHostOnWhite->SetCompositor(aCompositor);
  }
}

#ifdef MOZ_DUMP_PAINTING

void
ContentHostBase::Dump(FILE* aFile,
                      const char* aPrefix,
                      bool aDumpHtml)
{
  if (!aDumpHtml) {
    return;
  }
  if (!aFile) {
    aFile = stderr;
  }
  fprintf(aFile, "<ul>");
  if (mDeprecatedTextureHost) {
    fprintf(aFile, "%s", aPrefix);
    fprintf(aFile, "<li> <a href=");
    DumpDeprecatedTextureHost(aFile, mDeprecatedTextureHost);
    fprintf(aFile, "> Front buffer </a></li> ");
  }
  if (mDeprecatedTextureHostOnWhite) {
    fprintf(aFile, "%s", aPrefix);
    fprintf(aFile, "<li> <a href=");
    DumpDeprecatedTextureHost(aFile, mDeprecatedTextureHostOnWhite);
    fprintf(aFile, "> Front buffer on white </a> </li> ");
  }
  fprintf(aFile, "</ul>");
}

#endif

ContentHostSingleBuffered::~ContentHostSingleBuffered()
{
  DestroyTextures();
  DestroyFrontHost();
}

void
ContentHostSingleBuffered::EnsureDeprecatedTextureHost(TextureIdentifier aTextureId,
                                             const SurfaceDescriptor& aSurface,
                                             ISurfaceAllocator* aAllocator,
                                             const TextureInfo& aTextureInfo)
{
  MOZ_ASSERT(aTextureId == TextureFront ||
             aTextureId == TextureOnWhiteFront);
  RefPtr<DeprecatedTextureHost> *newHost =
    (aTextureId == TextureFront) ? &mNewFrontHost : &mNewFrontHostOnWhite;

  *newHost = DeprecatedTextureHost::CreateDeprecatedTextureHost(aSurface.type(),
                                            aTextureInfo.mDeprecatedTextureHostFlags,
                                            aTextureInfo.mTextureFlags,
                                            this);

  (*newHost)->SetBuffer(new SurfaceDescriptor(aSurface), aAllocator);
  Compositor* compositor = GetCompositor();
  if (compositor) {
    (*newHost)->SetCompositor(compositor);
  }
}

void
ContentHostSingleBuffered::DestroyTextures()
{
  MOZ_ASSERT(!mNewFrontHost || mNewFrontHost->GetDeAllocator(),
             "We won't be able to destroy our SurfaceDescriptor");
  MOZ_ASSERT(!mNewFrontHostOnWhite || mNewFrontHostOnWhite->GetDeAllocator(),
             "We won't be able to destroy our SurfaceDescriptor");
  mNewFrontHost = nullptr;
  mNewFrontHostOnWhite = nullptr;

  // don't touch mDeprecatedTextureHost, we might need it for compositing
}

void
ContentHostSingleBuffered::UpdateThebes(const ThebesBufferData& aData,
                                        const nsIntRegion& aUpdated,
                                        const nsIntRegion& aOldValidRegionBack,
                                        nsIntRegion* aUpdatedRegionBack)
{
  aUpdatedRegionBack->SetEmpty();

  if (!mDeprecatedTextureHost && !mNewFrontHost) {
    mInitialised = false;
    return;
  }

  if (mNewFrontHost) {
    DestroyFrontHost();
    mDeprecatedTextureHost = mNewFrontHost;
    mNewFrontHost = nullptr;
    if (mNewFrontHostOnWhite) {
      mDeprecatedTextureHostOnWhite = mNewFrontHostOnWhite;
      mNewFrontHostOnWhite = nullptr;
    }
  }

  MOZ_ASSERT(mDeprecatedTextureHost);
  MOZ_ASSERT(!mNewFrontHostOnWhite, "New white host without a new black?");

  // updated is in screen coordinates. Convert it to buffer coordinates.
  nsIntRegion destRegion(aUpdated);
  destRegion.MoveBy(-aData.rect().TopLeft());

  // Correct for rotation
  destRegion.MoveBy(aData.rotation());

  gfxIntSize size = aData.rect().Size();
  nsIntRect destBounds = destRegion.GetBounds();
  destRegion.MoveBy((destBounds.x >= size.width) ? -size.width : 0,
                    (destBounds.y >= size.height) ? -size.height : 0);

  // There's code to make sure that updated regions don't cross rotation
  // boundaries, so assert here that this is the case
  MOZ_ASSERT((destBounds.x % size.width) + destBounds.width <= size.width,
               "updated region lies across rotation boundaries!");
  MOZ_ASSERT((destBounds.y % size.height) + destBounds.height <= size.height,
               "updated region lies across rotation boundaries!");

  mDeprecatedTextureHost->Update(*mDeprecatedTextureHost->LockSurfaceDescriptor(), &destRegion);
  if (mDeprecatedTextureHostOnWhite) {
    mDeprecatedTextureHostOnWhite->Update(*mDeprecatedTextureHostOnWhite->LockSurfaceDescriptor(), &destRegion);
  }
  mInitialised = true;

  mBufferRect = aData.rect();
  mBufferRotation = aData.rotation();
}

ContentHostDoubleBuffered::~ContentHostDoubleBuffered()
{
  DestroyTextures();
  DestroyFrontHost();
}

void
ContentHostDoubleBuffered::EnsureDeprecatedTextureHost(TextureIdentifier aTextureId,
                                             const SurfaceDescriptor& aSurface,
                                             ISurfaceAllocator* aAllocator,
                                             const TextureInfo& aTextureInfo)
{
  RefPtr<DeprecatedTextureHost> newHost = DeprecatedTextureHost::CreateDeprecatedTextureHost(aSurface.type(),
                                                               aTextureInfo.mDeprecatedTextureHostFlags,
                                                               aTextureInfo.mTextureFlags,
                                                               this);

  newHost->SetBuffer(new SurfaceDescriptor(aSurface), aAllocator);

  Compositor* compositor = GetCompositor();
  if (compositor) {
    newHost->SetCompositor(compositor);
  }

  if (aTextureId == TextureFront) {
    mNewFrontHost = newHost;
    return;
  }
  if (aTextureId == TextureOnWhiteFront) {
    mNewFrontHostOnWhite = newHost;
    return;
  }
  if (aTextureId == TextureBack) {
    mBackHost = newHost;
    mBufferRect = nsIntRect();
    mBufferRotation = nsIntPoint();
    return;
  }
  if (aTextureId == TextureOnWhiteBack) {
    mBackHostOnWhite = newHost;
    return;
  }

  NS_ERROR("Bad texture identifier");
}

void
ContentHostDoubleBuffered::DestroyTextures()
{
  if (mNewFrontHost) {
    MOZ_ASSERT(mNewFrontHost->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mNewFrontHost = nullptr;
  }

  if (mNewFrontHostOnWhite) {
    MOZ_ASSERT(mNewFrontHostOnWhite->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mNewFrontHostOnWhite = nullptr;
  }

  if (mBackHost) {
    MOZ_ASSERT(mBackHost->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mBackHost = nullptr;
  }

  if (mBackHostOnWhite) {
    MOZ_ASSERT(mBackHostOnWhite->GetDeAllocator(),
               "We won't be able to destroy our SurfaceDescriptor");
    mBackHostOnWhite = nullptr;
  }

  // don't touch mDeprecatedTextureHost, we might need it for compositing
}

void
ContentHostDoubleBuffered::UpdateThebes(const ThebesBufferData& aData,
                                        const nsIntRegion& aUpdated,
                                        const nsIntRegion& aOldValidRegionBack,
                                        nsIntRegion* aUpdatedRegionBack)
{
  if (!mDeprecatedTextureHost && !mNewFrontHost) {
    mInitialised = false;

    *aUpdatedRegionBack = aUpdated;
    return;
  }

  if (mNewFrontHost) {
    DestroyFrontHost();
    mDeprecatedTextureHost = mNewFrontHost;
    mNewFrontHost = nullptr;
    if (mNewFrontHostOnWhite) {
      mDeprecatedTextureHostOnWhite = mNewFrontHostOnWhite;
      mNewFrontHostOnWhite = nullptr;
    }
  }

  MOZ_ASSERT(mDeprecatedTextureHost);
  MOZ_ASSERT(!mNewFrontHostOnWhite, "New white host without a new black?");
  MOZ_ASSERT(mBackHost);

  RefPtr<DeprecatedTextureHost> oldFront = mDeprecatedTextureHost;
  mDeprecatedTextureHost = mBackHost;
  mBackHost = oldFront;

  oldFront = mDeprecatedTextureHostOnWhite;
  mDeprecatedTextureHostOnWhite = mBackHostOnWhite;
  mBackHostOnWhite = oldFront;

  mDeprecatedTextureHost->Update(*mDeprecatedTextureHost->LockSurfaceDescriptor());
  if (mDeprecatedTextureHostOnWhite) {
    mDeprecatedTextureHostOnWhite->Update(*mDeprecatedTextureHostOnWhite->LockSurfaceDescriptor());
  }
  mInitialised = true;

  mBufferRect = aData.rect();
  mBufferRotation = aData.rotation();

  *aUpdatedRegionBack = aUpdated;

  // Save the current valid region of our front buffer, because if
  // we're double buffering, it's going to be the valid region for the
  // next back buffer sent back to the renderer.
  //
  // NB: we rely here on the fact that mValidRegion is initialized to
  // empty, and that the first time Swap() is called we don't have a
  // valid front buffer that we're going to return to content.
  mValidRegionForNextBackBuffer = aOldValidRegionBack;
}

void
ContentHostIncremental::EnsureDeprecatedTextureHostIncremental(ISurfaceAllocator* aAllocator,
                                                     const TextureInfo& aTextureInfo,
                                                     const nsIntRect& aBufferRect)
{
  mUpdateList.AppendElement(new TextureCreationRequest(aTextureInfo,
                                                       aBufferRect));
  mDeAllocator = aAllocator;
}

void
ContentHostIncremental::UpdateIncremental(TextureIdentifier aTextureId,
                                          SurfaceDescriptor& aSurface,
                                          const nsIntRegion& aUpdated,
                                          const nsIntRect& aBufferRect,
                                          const nsIntPoint& aBufferRotation)
{
  mUpdateList.AppendElement(new TextureUpdateRequest(mDeAllocator,
                                                     aTextureId,
                                                     aSurface,
                                                     aUpdated,
                                                     aBufferRect,
                                                     aBufferRotation));
}

void
ContentHostIncremental::ProcessTextureUpdates()
{
  for (uint32_t i = 0; i < mUpdateList.Length(); i++) {
    mUpdateList[i]->Execute(this);
  }
  mUpdateList.Clear();
}

void
ContentHostIncremental::TextureCreationRequest::Execute(ContentHostIncremental* aHost)
{
  RefPtr<DeprecatedTextureHost> newHost =
    DeprecatedTextureHost::CreateDeprecatedTextureHost(SurfaceDescriptor::TShmem,
                                   mTextureInfo.mDeprecatedTextureHostFlags,
                                   mTextureInfo.mTextureFlags,
                                   nullptr);
  Compositor* compositor = aHost->GetCompositor();
  if (compositor) {
    newHost->SetCompositor(compositor);
  }
  RefPtr<DeprecatedTextureHost> newHostOnWhite;
  if (mTextureInfo.mTextureFlags & TEXTURE_COMPONENT_ALPHA) {
    newHostOnWhite =
      DeprecatedTextureHost::CreateDeprecatedTextureHost(SurfaceDescriptor::TShmem,
                                     mTextureInfo.mDeprecatedTextureHostFlags,
                                     mTextureInfo.mTextureFlags,
                                     nullptr);
    Compositor* compositor = aHost->GetCompositor();
    if (compositor) {
      newHostOnWhite->SetCompositor(compositor);
    }
  }

  if (mTextureInfo.mDeprecatedTextureHostFlags & TEXTURE_HOST_COPY_PREVIOUS) {
    nsIntRect bufferRect = aHost->mBufferRect;
    nsIntPoint bufferRotation = aHost->mBufferRotation;
    nsIntRect overlap;

    // The buffer looks like:
    //  ______
    // |1  |2 |  Where the center point is offset by mBufferRotation from the top-left corner.
    // |___|__|
    // |3  |4 |
    // |___|__|
    //
    // This is drawn to the screen as:
    //  ______
    // |4  |3 |  Where the center point is { width - mBufferRotation.x, height - mBufferRotation.y } from
    // |___|__|  from the top left corner - rotationPoint.
    // |2  |1 |
    // |___|__|
    //

    // The basic idea below is to take all quadrant rectangles from the src and transform them into rectangles
    // in the destination. Unfortunately, it seems it is overly complex and could perhaps be simplified.

    nsIntRect srcBufferSpaceBottomRight(bufferRotation.x, bufferRotation.y, bufferRect.width - bufferRotation.x, bufferRect.height - bufferRotation.y);
    nsIntRect srcBufferSpaceTopRight(bufferRotation.x, 0, bufferRect.width - bufferRotation.x, bufferRotation.y);
    nsIntRect srcBufferSpaceTopLeft(0, 0, bufferRotation.x, bufferRotation.y);
    nsIntRect srcBufferSpaceBottomLeft(0, bufferRotation.y, bufferRotation.x, bufferRect.height - bufferRotation.y);

    overlap.IntersectRect(bufferRect, mBufferRect);

    nsIntRect srcRect(overlap), dstRect(overlap);
    srcRect.MoveBy(- bufferRect.TopLeft() + bufferRotation);

    nsIntRect srcRectDrawTopRight(srcRect);
    nsIntRect srcRectDrawTopLeft(srcRect);
    nsIntRect srcRectDrawBottomLeft(srcRect);
    // transform into the different quadrants
    srcRectDrawTopRight  .MoveBy(-nsIntPoint(0, bufferRect.height));
    srcRectDrawTopLeft   .MoveBy(-nsIntPoint(bufferRect.width, bufferRect.height));
    srcRectDrawBottomLeft.MoveBy(-nsIntPoint(bufferRect.width, 0));

    // Intersect with the quadrant
    srcRect               = srcRect              .Intersect(srcBufferSpaceBottomRight);
    srcRectDrawTopRight   = srcRectDrawTopRight  .Intersect(srcBufferSpaceTopRight);
    srcRectDrawTopLeft    = srcRectDrawTopLeft   .Intersect(srcBufferSpaceTopLeft);
    srcRectDrawBottomLeft = srcRectDrawBottomLeft.Intersect(srcBufferSpaceBottomLeft);

    dstRect = srcRect;
    nsIntRect dstRectDrawTopRight(srcRectDrawTopRight);
    nsIntRect dstRectDrawTopLeft(srcRectDrawTopLeft);
    nsIntRect dstRectDrawBottomLeft(srcRectDrawBottomLeft);

    // transform back to src buffer space
    dstRect              .MoveBy(-bufferRotation);
    dstRectDrawTopRight  .MoveBy(-bufferRotation + nsIntPoint(0, bufferRect.height));
    dstRectDrawTopLeft   .MoveBy(-bufferRotation + nsIntPoint(bufferRect.width, bufferRect.height));
    dstRectDrawBottomLeft.MoveBy(-bufferRotation + nsIntPoint(bufferRect.width, 0));

    // transform back to draw coordinates
    dstRect              .MoveBy(bufferRect.TopLeft());
    dstRectDrawTopRight  .MoveBy(bufferRect.TopLeft());
    dstRectDrawTopLeft   .MoveBy(bufferRect.TopLeft());
    dstRectDrawBottomLeft.MoveBy(bufferRect.TopLeft());

    // transform to destBuffer space
    dstRect              .MoveBy(-mBufferRect.TopLeft());
    dstRectDrawTopRight  .MoveBy(-mBufferRect.TopLeft());
    dstRectDrawTopLeft   .MoveBy(-mBufferRect.TopLeft());
    dstRectDrawBottomLeft.MoveBy(-mBufferRect.TopLeft());

    newHost->EnsureBuffer(mBufferRect.Size(),
                          ContentForFormat(aHost->mDeprecatedTextureHost->GetFormat()));

    aHost->mDeprecatedTextureHost->CopyTo(srcRect, newHost, dstRect);
    if (bufferRotation != nsIntPoint(0, 0)) {
      // Draw the remaining quadrants. We call BlitTextureImage 3 extra
      // times instead of doing a single draw call because supporting that
      // with a tiled source is quite tricky.

      if (!srcRectDrawTopRight.IsEmpty())
        aHost->mDeprecatedTextureHost->CopyTo(srcRectDrawTopRight,
                                          newHost, dstRectDrawTopRight);
      if (!srcRectDrawTopLeft.IsEmpty())
        aHost->mDeprecatedTextureHost->CopyTo(srcRectDrawTopLeft,
                                          newHost, dstRectDrawTopLeft);
      if (!srcRectDrawBottomLeft.IsEmpty())
        aHost->mDeprecatedTextureHost->CopyTo(srcRectDrawBottomLeft,
                                          newHost, dstRectDrawBottomLeft);
    }

    if (newHostOnWhite) {
      newHostOnWhite->EnsureBuffer(mBufferRect.Size(),
                                   ContentForFormat(aHost->mDeprecatedTextureHostOnWhite->GetFormat()));
      aHost->mDeprecatedTextureHostOnWhite->CopyTo(srcRect, newHostOnWhite, dstRect);
      if (bufferRotation != nsIntPoint(0, 0)) {
        // draw the remaining quadrants
        if (!srcRectDrawTopRight.IsEmpty())
          aHost->mDeprecatedTextureHostOnWhite->CopyTo(srcRectDrawTopRight,
                                                   newHostOnWhite, dstRectDrawTopRight);
        if (!srcRectDrawTopLeft.IsEmpty())
          aHost->mDeprecatedTextureHostOnWhite->CopyTo(srcRectDrawTopLeft,
                                                   newHostOnWhite, dstRectDrawTopLeft);
        if (!srcRectDrawBottomLeft.IsEmpty())
          aHost->mDeprecatedTextureHostOnWhite->CopyTo(srcRectDrawBottomLeft,
                                                   newHostOnWhite, dstRectDrawBottomLeft);
      }
    }
  }

  aHost->mDeprecatedTextureHost = newHost;
  aHost->mDeprecatedTextureHostOnWhite = newHostOnWhite;

  aHost->mBufferRect = mBufferRect;
  aHost->mBufferRotation = nsIntPoint();
}

nsIntRect
ContentHostIncremental::TextureUpdateRequest::GetQuadrantRectangle(XSide aXSide,
                                                                   YSide aYSide) const
{
  // quadrantTranslation is the amount we translate the top-left
  // of the quadrant by to get coordinates relative to the layer
  nsIntPoint quadrantTranslation = -mBufferRotation;
  quadrantTranslation.x += aXSide == LEFT ? mBufferRect.width : 0;
  quadrantTranslation.y += aYSide == TOP ? mBufferRect.height : 0;
  return mBufferRect + quadrantTranslation;
}

void
ContentHostIncremental::TextureUpdateRequest::Execute(ContentHostIncremental* aHost)
{
  nsIntRect drawBounds = mUpdated.GetBounds();

  aHost->mBufferRect = mBufferRect;
  aHost->mBufferRotation = mBufferRotation;

  // Figure out which quadrant to draw in
  int32_t xBoundary = mBufferRect.XMost() - mBufferRotation.x;
  int32_t yBoundary = mBufferRect.YMost() - mBufferRotation.y;
  XSide sideX = drawBounds.XMost() <= xBoundary ? RIGHT : LEFT;
  YSide sideY = drawBounds.YMost() <= yBoundary ? BOTTOM : TOP;
  nsIntRect quadrantRect = GetQuadrantRectangle(sideX, sideY);
  NS_ASSERTION(quadrantRect.Contains(drawBounds), "Messed up quadrants");

  mUpdated.MoveBy(-nsIntPoint(quadrantRect.x, quadrantRect.y));

  nsIntPoint offset = -mUpdated.GetBounds().TopLeft();

  if (mTextureId == TextureFront) {
    aHost->mDeprecatedTextureHost->Update(mDescriptor, &mUpdated, &offset);
  } else {
    aHost->mDeprecatedTextureHostOnWhite->Update(mDescriptor, &mUpdated, &offset);
  }
}

#ifdef MOZ_LAYERS_HAVE_LOG
void
ContentHostSingleBuffered::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("ContentHostSingleBuffered (0x%p)", this);

  AppendToString(aTo, mBufferRect, " [buffer-rect=", "]");
  AppendToString(aTo, mBufferRotation, " [buffer-rotation=", "]");
  if (PaintWillResample()) {
    aTo += " [paint-will-resample]";
  }

  nsAutoCString pfx(aPrefix);
  pfx += "  ";

  if (mDeprecatedTextureHost) {
    aTo += "\n";
    mDeprecatedTextureHost->PrintInfo(aTo, pfx.get());
  }
}

void
ContentHostDoubleBuffered::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("ContentHostDoubleBuffered (0x%p)", this);

  AppendToString(aTo, mBufferRect, " [buffer-rect=", "]");
  AppendToString(aTo, mBufferRotation, " [buffer-rotation=", "]");
  if (PaintWillResample()) {
    aTo += " [paint-will-resample]";
  }

  nsAutoCString prefix(aPrefix);
  prefix += "  ";

  if (mDeprecatedTextureHost) {
    aTo += "\n";
    mDeprecatedTextureHost->PrintInfo(aTo, prefix.get());
  }

  if (mBackHost) {
    aTo += "\n";
    mBackHost->PrintInfo(aTo, prefix.get());
  }
}
#endif

#ifdef MOZ_DUMP_PAINTING
void
ContentHostDoubleBuffered::Dump(FILE* aFile,
                                const char* aPrefix,
                                bool aDumpHtml)
{
  ContentHostBase::Dump(aFile, aPrefix, aDumpHtml);
  if (!aDumpHtml) {
    return;
  }
  if (!aFile) {
    aFile = stderr;
  }
  fprintf(aFile, "<ul>");
  if (mBackHost) {
    fprintf(aFile, "%s", aPrefix);
    fprintf(aFile, "<li> <a href=");
    DumpDeprecatedTextureHost(aFile, mBackHost);
    fprintf(aFile, " >Back buffer</a></li>");
  }
  if (mBackHostOnWhite) {
    fprintf(aFile, "%s", aPrefix);
    fprintf(aFile, "<li> <a href=");
    DumpDeprecatedTextureHost(aFile, mBackHostOnWhite);
    fprintf(aFile, " >Back buffer on white</a> </li>");
  }
  fprintf(aFile, "</ul>");
}
#endif

LayerRenderState
ContentHostBase::GetRenderState()
{
  LayerRenderState result = mDeprecatedTextureHost->GetRenderState();

  if (mBufferRotation != nsIntPoint()) {
    result.mFlags |= LAYER_RENDER_STATE_BUFFER_ROTATION;
  }
  result.SetOffset(GetOriginOffset());
  return result;
}

#ifdef MOZ_DUMP_PAINTING
already_AddRefed<gfxImageSurface>
ContentHostBase::GetAsSurface()
{
  return mDeprecatedTextureHost->GetAsSurface();
}
#endif


} // namespace
} // namespace
