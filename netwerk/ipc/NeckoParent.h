/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/PNeckoParent.h"
#include "mozilla/net/NeckoCommon.h"

#ifndef mozilla_net_NeckoParent_h
#define mozilla_net_NeckoParent_h

namespace mozilla {
namespace net {

// Used to override channel Private Browsing status if needed.
enum PBOverrideStatus {
  kPBOverride_Unset = 0,
  kPBOverride_Private,
  kPBOverride_NotPrivate
};

// Header file contents
class NeckoParent :
  public PNeckoParent
{
public:
  NeckoParent();
  virtual ~NeckoParent();

  MOZ_WARN_UNUSED_RESULT
  static const char *
  GetValidatedAppInfo(const SerializedLoadContext& aSerialized,
                      PBrowserParent* aBrowser,
                      uint32_t* aAppId,
                      bool* aInBrowserElement);

  MOZ_WARN_UNUSED_RESULT
  static const char *
  GetValidatedAppInfo(const SerializedLoadContext& aSerialized,
                      PContentParent* aBrowser,
                      uint32_t* aAppId,
                      bool* aInBrowserElement);

  /*
   * Creates LoadContext for parent-side of an e10s channel.
   *
   * PContentParent corresponds to the process that is requesting the load.
   *
   * Returns null if successful, or an error string if failed.
   */
  MOZ_WARN_UNUSED_RESULT
  static const char*
  CreateChannelLoadContext(PBrowserParent* aBrowser,
                           PContentParent* aContent,
                           const SerializedLoadContext& aSerialized,
                           nsCOMPtr<nsILoadContext> &aResult);

  virtual void
  CloneManagees(ProtocolBase* aSource,
              mozilla::ipc::ProtocolCloneContext* aCtx) MOZ_OVERRIDE;
  virtual PCookieServiceParent* AllocPCookieServiceParent() MOZ_OVERRIDE;
  virtual bool
  RecvPCookieServiceConstructor(PCookieServiceParent* aActor) MOZ_OVERRIDE
  {
    return PNeckoParent::RecvPCookieServiceConstructor(aActor);
  }

protected:
  virtual PHttpChannelParent*
    AllocPHttpChannelParent(PBrowserParent*, const SerializedLoadContext&,
                            const HttpChannelCreationArgs& aOpenArgs);
  virtual bool
    RecvPHttpChannelConstructor(
                      PHttpChannelParent* aActor,
                      PBrowserParent* aBrowser,
                      const SerializedLoadContext& aSerialized,
                      const HttpChannelCreationArgs& aOpenArgs);
  virtual bool DeallocPHttpChannelParent(PHttpChannelParent*);
  virtual bool DeallocPCookieServiceParent(PCookieServiceParent*);
  virtual PWyciwygChannelParent* AllocPWyciwygChannelParent();
  virtual bool DeallocPWyciwygChannelParent(PWyciwygChannelParent*);
  virtual PFTPChannelParent*
    AllocPFTPChannelParent(PBrowserParent* aBrowser,
                           const SerializedLoadContext& aSerialized,
                           const FTPChannelCreationArgs& aOpenArgs);
  virtual bool
    RecvPFTPChannelConstructor(
                      PFTPChannelParent* aActor,
                      PBrowserParent* aBrowser,
                      const SerializedLoadContext& aSerialized,
                      const FTPChannelCreationArgs& aOpenArgs);
  virtual bool DeallocPFTPChannelParent(PFTPChannelParent*);
  virtual PWebSocketParent* AllocPWebSocketParent(PBrowserParent* browser,
                                                  const SerializedLoadContext& aSerialized);
  virtual bool DeallocPWebSocketParent(PWebSocketParent*);
  virtual PTCPSocketParent* AllocPTCPSocketParent();

  virtual PRemoteOpenFileParent* AllocPRemoteOpenFileParent(const URIParams& aFileURI,
                                                            const OptionalURIParams& aAppURI)
                                                            MOZ_OVERRIDE;
  virtual bool RecvPRemoteOpenFileConstructor(PRemoteOpenFileParent* aActor,
                                              const URIParams& aFileURI,
                                              const OptionalURIParams& aAppURI)
                                              MOZ_OVERRIDE;
  virtual bool DeallocPRemoteOpenFileParent(PRemoteOpenFileParent* aActor)
                                            MOZ_OVERRIDE;

  virtual bool DeallocPTCPSocketParent(PTCPSocketParent*);
  virtual PTCPServerSocketParent* AllocPTCPServerSocketParent(const uint16_t& aLocalPort,
                                                        const uint16_t& aBacklog,
                                                        const nsString& aBinaryType);
  virtual bool RecvPTCPServerSocketConstructor(PTCPServerSocketParent*,
                                               const uint16_t& aLocalPort,
                                               const uint16_t& aBacklog,
                                               const nsString& aBinaryType);
  virtual bool DeallocPTCPServerSocketParent(PTCPServerSocketParent*);
  virtual PUDPSocketParent* AllocPUDPSocketParent(const nsCString& aHost,
                                                  const uint16_t& aPort);
  virtual bool RecvPUDPSocketConstructor(PUDPSocketParent*,
                                         const nsCString& aHost,
                                         const uint16_t& aPort);
  virtual bool DeallocPUDPSocketParent(PUDPSocketParent*);
  virtual bool RecvHTMLDNSPrefetch(const nsString& hostname,
                                   const uint16_t& flags);
  virtual bool RecvCancelHTMLDNSPrefetch(const nsString& hostname,
                                         const uint16_t& flags,
                                         const nsresult& reason);

  virtual mozilla::ipc::IProtocol*
  CloneProtocol(Channel* aChannel,
                mozilla::ipc::ProtocolCloneContext* aCtx) MOZ_OVERRIDE;
  virtual PRtspControllerParent* AllocPRtspControllerParent();
  virtual bool DeallocPRtspControllerParent(PRtspControllerParent*);

private:
  nsCString mCoreAppsBasePath;
  nsCString mWebAppsBasePath;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_NeckoParent_h
