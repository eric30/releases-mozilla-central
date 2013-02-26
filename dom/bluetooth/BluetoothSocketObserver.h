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

class BluetoothSocketObserver
{
public:
  /**
   * A function which would be called when data is received by observed socket.
   *
   * @param aMessage Data received from the socket.
   */
  virtual void ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage) = 0;

  /**
   * A callback function which would be called when a socket connection
   * is established successfully. To be more specific, this would be called
   * when socket state changes from CONNECTING/LISTENING to CONNECTED.
   */
  virtual void OnConnectSuccess() = 0;

  /**
   * A callback function which would be called when BluetoothSocket::Connect()
   * fails.
   */
  virtual void OnConnectError() = 0;

  /**
   * A callback function which would be called when a socket connection
   * is dropped. To be more specific, this would be called when socket state
   * changes from CONNECTED/LISTENING to DISCONNECTED.
   */
  virtual void OnDisconnect() = 0;
};

END_BLUETOOTH_NAMESPACE

#endif
