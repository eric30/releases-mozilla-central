/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothSocket_h
#define mozilla_dom_bluetooth_BluetoothSocket_h

#include "BluetoothCommon.h"
#include "BluetoothProfileManager.h"
#include <mozilla/ipc/UnixSocket.h>

using namespace mozilla::ipc;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothSocket : public UnixSocketConsumer
{
public:
  BluetoothSocket(BluetoothProfileManager* aManager);
  ~BluetoothSocket();

  void ReceiveSocketData(UnixSocketRawData* aMessage) MOZ_OVERRIDE;
  void OnConnectSuccess() MOZ_OVERRIDE;
  void OnConnectError() MOZ_OVERRIDE;
  void OnDisconnect() MOZ_OVERRIDE;

private:
  BluetoothProfileManager* mManager;
};

END_BLUETOOTH_NAMESPACE

#endif
