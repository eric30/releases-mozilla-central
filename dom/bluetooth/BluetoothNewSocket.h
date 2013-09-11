/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
  * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothnewsocket_h__
#define mozilla_dom_bluetooth_bluetoothnewsocket_h__

#include "mozilla/Attributes.h"
#include "BluetoothCommon.h"
#include "nsCOMPtr.h"
#include "nsDOMEventTargetHelper.h"

namespace mozilla {
namespace dom {
  class DOMRequest;
}
}

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothNewSocket : public nsDOMEventTargetHelper
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  
  BluetoothNewSocket(nsPIDOMWindow* aOwner);
  ~BluetoothNewSocket();

  void GetName(nsString& aName) const
  {
    aName = mName;
  }

  already_AddRefed<DOMRequest>
    Open(const nsAString& aDeviceAddress, const nsAString& aServiceUuid,
         ErrorResult& aRv);

  nsPIDOMWindow* GetParentObject() const
  {
    return GetOwner();
  }

  virtual JSObject*
    WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

private:
  nsString mName;
};

END_BLUETOOTH_NAMESPACE

#endif

