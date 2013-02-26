/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothSocket_h
#define mozilla_dom_bluetooth_BluetoothSocket_h

#include "BluetoothCommon.h"
#include "BluetoothSocketObserver.h"
#include "BluetoothUnixSocketConnector.h"
#include <mozilla/ipc/UnixSocket.h>

using namespace mozilla::ipc;

BEGIN_BLUETOOTH_NAMESPACE

enum BluetoothSocketState
{
  // Directly mapping to mozilla::ipc::SocketConnectionStatus
  DISCONNECTED = SocketConnectionStatus::SOCKET_DISCONNECTED,
  LISTENING    = SocketConnectionStatus::SOCKET_LISTENING,
  CONNECTING   = SocketConnectionStatus::SOCKET_CONNECTING,
  CONNECTED    = SocketConnectionStatus::SOCKET_CONNECTED
};

class BluetoothSocket : public UnixSocketConsumer
{
public:
  BluetoothSocket(BluetoothSocketType aType,
                  bool aAuth,
                  bool aEncrypt,
                  BluetoothSocketObserver* aObserver);
  ~BluetoothSocket();

  bool Connect(const nsACString& aDeviceAddress, int aChannel);
  bool Listen(int aChannel);
  void Disconnect();

  void OnConnectSuccess() MOZ_OVERRIDE;
  void OnConnectError() MOZ_OVERRIDE;
  void OnDisconnect() MOZ_OVERRIDE;

  void ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage) MOZ_OVERRIDE;

  void GetAddress(nsAString& aDeviceAddress);
  BluetoothSocketState GetState() const;

private:
  BluetoothSocketObserver* mObserver;
  BluetoothSocketType mType;
  bool mAuth;
  bool mEncrypt;
};

END_BLUETOOTH_NAMESPACE

#endif
