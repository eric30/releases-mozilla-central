/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//  * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURECLIENT_H
#define MOZILLA_GFX_TEXTURECLIENT_H

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint8_t, uint64_t
#include "GLContextTypes.h"             // for GLContext (ptr only), etc
#include "GLTextureImage.h"             // for TextureImage
#include "ImageContainer.h"             // for PlanarYCbCrImage, etc
#include "ImageTypes.h"                 // for StereoMode
#include "gfxASurface.h"                // for gfxASurface, etc
#include "gfxImageSurface.h"            // for gfxImageSurface
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/Attributes.h"         // for MOZ_OVERRIDE
#include "mozilla/RefPtr.h"             // for RefPtr, RefCounted
#include "mozilla/gfx/2D.h"             // for DrawTarget
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/gfx/Types.h"          // for SurfaceFormat
#include "mozilla/ipc/Shmem.h"          // for Shmem
#include "mozilla/layers/CompositorTypes.h"  // for TextureFlags, etc
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor
#include "mozilla/mozalloc.h"           // for operator delete
#include "nsAutoPtr.h"                  // for nsRefPtr
#include "nsCOMPtr.h"                   // for already_AddRefed
#include "nsISupportsImpl.h"            // for TextureImage::AddRef, etc

class gfxReusableSurfaceWrapper;

namespace mozilla {
namespace layers {

class ContentClient;
class CompositableForwarder;
class ISurfaceAllocator;
class CompositableClient;

/**
 * TextureClient is the abstraction that allows us to share data between the
 * content and the compositor side.
 * TextureClient can also provide with some more "producer" facing APIs
 * such as TextureClientSurface and TextureClientYCbCr, that can be queried
 * using AsTextureCLientSurface(), etc.
 */

/**
 * Interface for TextureClients that can be updated using a gfxASurface.
 */
class TextureClientSurface
{
public:
  virtual bool UpdateSurface(gfxASurface* aSurface) = 0;
  virtual already_AddRefed<gfxASurface> GetAsSurface() = 0;
  virtual bool AllocateForSurface(gfx::IntSize aSize) = 0;
};

/**
 * Interface for TextureClients that can be updated using YCbCr data.
 */
class TextureClientYCbCr
{
public:
  virtual bool UpdateYCbCr(const PlanarYCbCrImage::Data& aData) = 0;
  virtual bool AllocateForYCbCr(gfx::IntSize aYSize,
                                gfx::IntSize aCbCrSize,
                                StereoMode aStereoMode) = 0;
};


/**
 * TextureClient is a thin abstraction over texture data that need to be shared
 * between the content process and the compositor process. It is the
 * content-side half of a TextureClient/TextureHost pair. A corresponding
 * TextureHost lives on the compositor-side.
 *
 * TextureClient's primary purpose is to present texture data in a way that is
 * understood by the IPC system. There are two ways to use it:
 * - Use it to serialize image data that is not IPC-friendly (most likely
 * involving a copy into shared memory)
 * - preallocate it and paint directly into it, which avoids copy but requires
 * the painting code to be aware of TextureClient (or at least the underlying
 * shared memory).
 *
 * There is always one and only one TextureClient per TextureHost, and the
 * TextureClient/Host pair only owns one buffer of image data through its
 * lifetime. This means that the lifetime of the underlying shared data
 * matches the lifetime of the TextureClient/Host pair. It also means
 * TextureClient/Host do not implement double buffering, which is the
 * responsibility of the compositable (which would use two Texture pairs).
 * In order to send several different buffers to the compositor side, use
 * several TextureClients.
 */
class TextureClient : public AtomicRefCounted<TextureClient>
{
public:
  TextureClient(TextureFlags aFlags = TEXTURE_FLAGS_DEFAULT);
  virtual ~TextureClient();

  virtual TextureClientSurface* AsTextureClientSurface() { return nullptr; }
  virtual TextureClientYCbCr* AsTextureClientYCbCr() { return nullptr; }

  virtual void MarkUnused() {}

  virtual bool Lock(OpenMode aMode)
  {
    return true;
  }

  virtual void Unlock() {}

  /**
   * Returns true if this texture has a lock/unlock mechanism.
   * Textures that do not implement locking should be immutable or should
   * use immediate uploads (see TextureFlags in CompositorTypes.h)
   */
  virtual bool ImplementsLocking() const
  {
    return false;
  }

  void SetID(uint64_t aID)
  {
    MOZ_ASSERT(mID == 0 && aID != 0);
    mID = aID;
    mShared = true;
  }
  void ClearID()
  {
    MOZ_ASSERT(mID != 0);
    mID = 0;
  }

  uint64_t GetID() const
  {
    return mID;
  }

  virtual bool IsAllocated() const = 0;

  virtual bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor) = 0;

  virtual gfx::IntSize GetSize() const = 0;

  TextureFlags GetFlags() const { return mFlags; }

  /**
   * After being shared with the compositor side, an immutable texture is never
   * modified, it can only be read. It is safe to not Lock/Unlock immutable
   * textures.
   */
  bool IsImmutable() const { return mFlags & TEXTURE_IMMUTABLE; }

  void MarkImmutable() { AddFlags(TEXTURE_IMMUTABLE); }

  bool IsSharedWithCompositor() const { return mShared; }

  bool ShouldDeallocateInDestructor() const;
protected:
  void AddFlags(TextureFlags  aFlags)
  {
    MOZ_ASSERT(!IsSharedWithCompositor());
    // make sure we don't deallocate on both client and host;
    MOZ_ASSERT(!(aFlags & TEXTURE_DEALLOCATE_CLIENT && aFlags & TEXTURE_DEALLOCATE_HOST));
    if (aFlags & TEXTURE_DEALLOCATE_CLIENT) {
      mFlags &= ~TEXTURE_DEALLOCATE_HOST;
    } else if (aFlags & TEXTURE_DEALLOCATE_HOST) {
      mFlags &= ~TEXTURE_DEALLOCATE_CLIENT;
    }
    mFlags |= aFlags;
  }

  uint64_t mID;
  TextureFlags mFlags;
  bool mShared;
};

/**
 * TextureClient that wraps a random access buffer such as a Shmem or raw memory.
 * This class must be inherited to implement the memory allocation and access bits.
 * (see ShmemTextureClient and MemoryTextureClient)
 */
class BufferTextureClient : public TextureClient
                          , public TextureClientSurface
                          , TextureClientYCbCr
{
public:
  BufferTextureClient(CompositableClient* aCompositable, gfx::SurfaceFormat aFormat,
                      TextureFlags aFlags);

  virtual ~BufferTextureClient();

  virtual bool IsAllocated() const = 0;

  virtual bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor) = 0;

  virtual uint8_t* GetBuffer() const = 0;

  virtual gfx::IntSize GetSize() const { return mSize; }

  // TextureClientSurface

  virtual TextureClientSurface* AsTextureClientSurface() MOZ_OVERRIDE { return this; }

  virtual bool UpdateSurface(gfxASurface* aSurface) MOZ_OVERRIDE;

  virtual already_AddRefed<gfxASurface> GetAsSurface() MOZ_OVERRIDE;

  virtual bool AllocateForSurface(gfx::IntSize aSize) MOZ_OVERRIDE;

  // TextureClientYCbCr

  virtual TextureClientYCbCr* AsTextureClientYCbCr() MOZ_OVERRIDE { return this; }

  virtual bool UpdateYCbCr(const PlanarYCbCrImage::Data& aData) MOZ_OVERRIDE;

  virtual bool AllocateForYCbCr(gfx::IntSize aYSize,
                                gfx::IntSize aCbCrSize,
                                StereoMode aStereoMode) MOZ_OVERRIDE;

  gfx::SurfaceFormat GetFormat() const { return mFormat; }

  // XXX - Bug 908196 - Make Allocate(uint32_t) and GetBufferSize() protected.
  // these two methods should only be called by methods of BufferTextureClient
  // that are overridden in GrallocTextureClient (which does not implement the
  // two methods below)
  virtual bool Allocate(uint32_t aSize) = 0;

  virtual size_t GetBufferSize() const = 0;

protected:
  CompositableClient* mCompositable;
  gfx::SurfaceFormat mFormat;
  gfx::IntSize mSize;
};

/**
 * TextureClient that wraps shared memory.
 * the corresponding texture on the host side is ShmemTextureHost.
 */
class ShmemTextureClient : public BufferTextureClient
{
public:
  ShmemTextureClient(CompositableClient* aCompositable, gfx::SurfaceFormat aFormat,
                     TextureFlags aFlags);

  ~ShmemTextureClient();

  virtual bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE;

  virtual bool Allocate(uint32_t aSize) MOZ_OVERRIDE;

  virtual uint8_t* GetBuffer() const MOZ_OVERRIDE;

  virtual size_t GetBufferSize() const MOZ_OVERRIDE;

  virtual bool IsAllocated() const MOZ_OVERRIDE { return mAllocated; }

  ISurfaceAllocator* GetAllocator() const;

  ipc::Shmem& GetShmem() { return mShmem; }

protected:
  ipc::Shmem mShmem;
  ISurfaceAllocator* mAllocator;
  bool mAllocated;
};

/**
 * TextureClient that wraps raw memory.
 * The corresponding texture on the host side is MemoryTextureHost.
 * Can obviously not be used in a cross process setup.
 */
class MemoryTextureClient : public BufferTextureClient
{
public:
  MemoryTextureClient(CompositableClient* aCompositable, gfx::SurfaceFormat aFormat,
                      TextureFlags aFlags);

  ~MemoryTextureClient();

  virtual bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE;

  virtual bool Allocate(uint32_t aSize) MOZ_OVERRIDE;

  virtual uint8_t* GetBuffer() const MOZ_OVERRIDE { return mBuffer; }

  virtual size_t GetBufferSize() const MOZ_OVERRIDE { return mBufSize; }

  virtual bool IsAllocated() const MOZ_OVERRIDE { return mBuffer != nullptr; }

protected:
  uint8_t* mBuffer;
  size_t mBufSize;
};

struct TextureClientAutoUnlock
{
  TextureClient* mTexture;

  TextureClientAutoUnlock(TextureClient* aTexture)
  : mTexture(aTexture) {}

  ~TextureClientAutoUnlock()
  {
    mTexture->Unlock();
  }
};

/**
 * XXX - This class is deprecated, will be removed soon.
 *
 * This class allows texture clients to draw into textures through Azure or
 * thebes and applies locking semantics to allow GPU or CPU level
 * synchronization.
 * DeprecatedTextureClient's purpose is for the texture data to be
 * forwarded to the right place on the compositor side and with correct locking
 * semantics.
 *
 * When modifying a DeprecatedTextureClient's data, first call LockDescriptor, modify the
 * data in the descriptor, and then call Unlock. This makes sure that if the
 * data is shared with the compositor, the later will not try to read while the
 * data is being modified (on the other side, DeprecatedTextureHost also has Lock/Unlock
 * semantics).
 * After unlocking, call Updated in order to add the modification to the current
 * layer transaction.
 * Depending on whether the data is shared or copied, Lock/Unlock and Updated
 * can be no-ops. What's important is that the Client/Host pair implement the
 * same semantics.
 *
 * Ownership of the surface descriptor depends on how the DeprecatedTextureClient/Host is
 * used by the CompositableClient/Host.
 */
class DeprecatedTextureClient : public RefCounted<DeprecatedTextureClient>
{
public:
  typedef gl::SharedTextureHandle SharedTextureHandle;
  typedef gl::GLContext GLContext;
  typedef gl::TextureImage TextureImage;

  virtual ~DeprecatedTextureClient();

  /* This will return an identifier that can be sent accross a process or
   * thread boundary and used to construct a DeprecatedTextureHost object
   * which can then be used as a texture for rendering by a compatible
   * compositor. This texture should have been created with the
   * DeprecatedTextureHostIdentifier specified by the compositor that this identifier
   * is to be used with.
   */
  virtual const TextureInfo& GetTextureInfo() const
  {
    return mTextureInfo;
  }

  virtual bool SupportsType(DeprecatedTextureClientType aType) { return false; }

  /**
   * The Lock* methods lock the texture client for drawing into, providing some
   * object that can be used for drawing to. Once the user is finished
   * with the object it should call Unlock.
   */
  virtual gfxImageSurface* LockImageSurface() { return nullptr; }
  virtual gfxASurface* LockSurface() { return nullptr; }
  // If you implement LockDrawTarget, you MUST implement BackendType()
  virtual gfx::DrawTarget* LockDrawTarget() { return nullptr; }

  // The type of draw target returned by LockDrawTarget.
  virtual gfx::BackendType BackendType()
  {
    return gfx::BACKEND_NONE;
  }


  virtual SurfaceDescriptor* LockSurfaceDescriptor() { return GetDescriptor(); }
  virtual void ReleaseResources() {}
  /**
   * This unlocks the current DrawableTexture and allows the host to composite
   * it directly.
   */
  virtual void Unlock() {}

  /**
   * Ensure that the texture client is suitable for the given size and content
   * type and that any initialisation has taken place.
   * Returns true if succeeded, false if failed.
   */
  virtual bool EnsureAllocated(gfx::IntSize aSize,
                               gfxASurface::gfxContentType aType) = 0;

  /**
   * _Only_ used at the end of the layer transaction when receiving a reply from
   *  the compositor.
   */
  virtual void SetDescriptorFromReply(const SurfaceDescriptor& aDescriptor)
  {
    // default implementation
    SetDescriptor(aDescriptor);
  }
  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor)
  {
    mDescriptor = aDescriptor;
  }
  SurfaceDescriptor* GetDescriptor() { return &mDescriptor; }

  CompositableForwarder* GetForwarder() const
  {
    return mForwarder;
  }

  void SetFlags(TextureFlags aFlags)
  {
    mTextureInfo.mTextureFlags = aFlags;
  }

  enum AccessMode
  {
    ACCESS_NONE = 0x0,
    ACCESS_READ_ONLY  = 0x1,
    ACCESS_READ_WRITE = 0x2
  };

  void SetAccessMode(AccessMode aAccessMode)
  {
    mAccessMode = aAccessMode;
  }

  AccessMode GetAccessMode() const
  {
    return mAccessMode;
  }

  virtual gfxASurface::gfxContentType GetContentType() = 0;

protected:
  DeprecatedTextureClient(CompositableForwarder* aForwarder,
                const TextureInfo& aTextureInfo);

  CompositableForwarder* mForwarder;
  // So far all DeprecatedTextureClients use a SurfaceDescriptor, so it makes sense to
  // keep the reference here.
  SurfaceDescriptor mDescriptor;
  TextureInfo mTextureInfo;
  AccessMode mAccessMode;
};

class DeprecatedTextureClientShmem : public DeprecatedTextureClient
{
public:
  DeprecatedTextureClientShmem(CompositableForwarder* aForwarder, const TextureInfo& aTextureInfo);
  ~DeprecatedTextureClientShmem() { ReleaseResources(); }

  virtual bool SupportsType(DeprecatedTextureClientType aType) MOZ_OVERRIDE
  {
    return aType == TEXTURE_SHMEM || aType == TEXTURE_CONTENT || aType == TEXTURE_FALLBACK;
  }
  virtual gfxImageSurface* LockImageSurface() MOZ_OVERRIDE;
  virtual gfxASurface* LockSurface() MOZ_OVERRIDE { return GetSurface(); }
  virtual gfx::DrawTarget* LockDrawTarget();
  virtual gfx::BackendType BackendType() MOZ_OVERRIDE
  {
    return gfx::BACKEND_CAIRO;
  }
  virtual void Unlock() MOZ_OVERRIDE;
  virtual bool EnsureAllocated(gfx::IntSize aSize, gfxASurface::gfxContentType aType) MOZ_OVERRIDE;

  virtual void ReleaseResources() MOZ_OVERRIDE;
  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE;
  virtual gfxASurface::gfxContentType GetContentType() MOZ_OVERRIDE { return mContentType; }
private:
  gfxASurface* GetSurface();

  nsRefPtr<gfxASurface> mSurface;
  nsRefPtr<gfxImageSurface> mSurfaceAsImage;
  RefPtr<gfx::DrawTarget> mDrawTarget;

  gfxASurface::gfxContentType mContentType;
  gfx::IntSize mSize;

  friend class CompositingFactory;
};

// XXX - this class can be removed as soon as we remove DeprecatedImageClientSingle
class DeprecatedTextureClientShmemYCbCr : public DeprecatedTextureClient
{
public:
  DeprecatedTextureClientShmemYCbCr(CompositableForwarder* aForwarder, const TextureInfo& aTextureInfo)
    : DeprecatedTextureClient(aForwarder, aTextureInfo)
  { }
  ~DeprecatedTextureClientShmemYCbCr() { ReleaseResources(); }

  virtual bool SupportsType(DeprecatedTextureClientType aType) MOZ_OVERRIDE { return aType == TEXTURE_YCBCR; }
  bool EnsureAllocated(gfx::IntSize aSize, gfxASurface::gfxContentType aType) MOZ_OVERRIDE;
  virtual void SetDescriptorFromReply(const SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE;
  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE;
  virtual void ReleaseResources();
  virtual gfxASurface::gfxContentType GetContentType() MOZ_OVERRIDE { return gfxASurface::CONTENT_COLOR_ALPHA; }
};

class DeprecatedTextureClientTile : public DeprecatedTextureClient
{
public:
  DeprecatedTextureClientTile(const DeprecatedTextureClientTile& aOther);
  DeprecatedTextureClientTile(CompositableForwarder* aForwarder,
                              const TextureInfo& aTextureInfo,
                              gfxReusableSurfaceWrapper* aSurface = nullptr);
  ~DeprecatedTextureClientTile();

  virtual bool EnsureAllocated(gfx::IntSize aSize,
                               gfxASurface::gfxContentType aType) MOZ_OVERRIDE;

  virtual gfxImageSurface* LockImageSurface() MOZ_OVERRIDE;

  gfxReusableSurfaceWrapper* GetReusableSurfaceWrapper()
  {
    return mSurface;
  }

  virtual void SetDescriptor(const SurfaceDescriptor& aDescriptor) MOZ_OVERRIDE
  {
    MOZ_ASSERT(false, "Tiled texture clients don't use SurfaceDescriptors.");
  }


  virtual gfxASurface::gfxContentType GetContentType() { return mContentType; }

private:
  gfxASurface::gfxContentType mContentType;
  nsRefPtr<gfxReusableSurfaceWrapper> mSurface;

  friend class CompositingFactory;
};

/**
 * Base class for AutoLock*Client.
 * handles lock/unlock
 * XXX - this can be removed as soon as we remove DeprecatedImageClientSingle
 */
class AutoLockDeprecatedTextureClient
{
public:
  AutoLockDeprecatedTextureClient(DeprecatedTextureClient* aTexture)
  {
    mDeprecatedTextureClient = aTexture;
    mDescriptor = aTexture->LockSurfaceDescriptor();
  }

  SurfaceDescriptor* GetSurfaceDescriptor()
  {
    return mDescriptor;
  }

  virtual ~AutoLockDeprecatedTextureClient()
  {
    mDeprecatedTextureClient->Unlock();
  }
protected:
  DeprecatedTextureClient* mDeprecatedTextureClient;
  SurfaceDescriptor* mDescriptor;
};

/**
 * Writes the content of a PlanarYCbCrImage into a SurfaceDescriptor.
 * XXX - this can be removed as soon as we remove DeprecatedImageClientSingle
 */
class AutoLockYCbCrClient : public AutoLockDeprecatedTextureClient
{
public:
  AutoLockYCbCrClient(DeprecatedTextureClient* aTexture) : AutoLockDeprecatedTextureClient(aTexture) {}
  bool Update(PlanarYCbCrImage* aImage);
protected:
  bool EnsureDeprecatedTextureClient(PlanarYCbCrImage* aImage);
};

/**
 * Writes the content of a gfxASurface into a SurfaceDescriptor.
 * XXX - this can be removed as soon as we remove DeprecatedImageClientSingle
 */
class AutoLockShmemClient : public AutoLockDeprecatedTextureClient
{
public:
  AutoLockShmemClient(DeprecatedTextureClient* aTexture) : AutoLockDeprecatedTextureClient(aTexture) {}
  bool Update(Image* aImage, uint32_t aContentFlags, gfxASurface *aSurface);
};

}
}
#endif
