/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothSocketObserver_h
#define mozilla_dom_bluetooth_BluetoothSocketObserver_h

#include "BluetoothCommon.h"
#include <mozilla/ipc/UnixSocket.h>

using namespace mozilla::ipc;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothSocket;

class BluetoothSocketObserver
{
public:
  virtual void ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage,
                                 BluetoothSocket* aSocket) = 0;
  virtual void OnConnectSuccess(BluetoothSocket* aSocket) = 0;
  virtual void OnConnectError(BluetoothSocket* aSocket) = 0;
  virtual void OnDisconnect(BluetoothSocket* aSocket) = 0;
};

END_BLUETOOTH_NAMESPACE

#endif
