/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
  * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothnewsocket_h__
#define mozilla_dom_bluetooth_bluetoothnewsocket_h__

#include "mozilla/Attributes.h"
#include "BluetoothCommon.h"
#include "BluetoothSocketObserver.h"
#include "nsCOMPtr.h"
#include "nsDOMEventTargetHelper.h"

namespace mozilla {
namespace dom {
  class DOMRequest;
}
}

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothNewSocket : public nsDOMEventTargetHelper
                         , public BluetoothSocketObserver
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  
  BluetoothNewSocket(nsPIDOMWindow* aOwner, const nsAString& aAddress);
  ~BluetoothNewSocket();

  void GetAddress(nsString& aAddress) const
  {
    aAddress = mAddress;
  }
  
  void GetServiceUuid(nsString& aServiceUuid) const
  {
    aServiceUuid = mServiceUuid;
  }

  already_AddRefed<DOMRequest>
    Open(const nsAString& aServiceUuid, ErrorResult& aRv);

  nsPIDOMWindow* GetParentObject() const
  {
    return GetOwner();
  }

  virtual JSObject*
    WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  // The following functions are inherited from BluetoothSocketObserver
  void ReceiveSocketData(
    BluetoothSocket* aSocket,
    nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage) MOZ_OVERRIDE;
  virtual void OnSocketConnectSuccess(BluetoothSocket* aSocket) MOZ_OVERRIDE;
  virtual void OnSocketConnectError(BluetoothSocket* aSocket) MOZ_OVERRIDE;
  virtual void OnSocketDisconnect(BluetoothSocket* aSocket) MOZ_OVERRIDE;

private:
  nsString mAddress;
  nsString mServiceUuid;
  nsRefPtr<BluetoothReplyRunnable> mRunnable;
};

END_BLUETOOTH_NAMESPACE

#endif

