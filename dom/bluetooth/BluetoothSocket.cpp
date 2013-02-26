/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothSocket.h"

USING_BLUETOOTH_NAMESPACE

BluetoothSocket::BluetoothSocket(BluetoothUnixSocketConnector* aConnector,
                                 BluetoothSocketObserver* aObserver)
  : UnixSocketConsumer()
{
  mObserver = aObserver;
  mConnector = aConnector;

  mDeviceAddress.AssignLiteral("00:00:00:00:00:00");
}

BluetoothSocket::~BluetoothSocket()
{
}

bool
BluetoothSocket::Connect(const nsAString& aDeviceAddress)
{
  mDeviceAddress = aDeviceAddress;

  return ConnectSocket(mConnector,
                       NS_ConvertUTF16toUTF8(mDeviceAddress).get());
}

bool
BluetoothSocket::Listen()
{
  mDeviceAddress.AssignLiteral("00:00:00:00:00:00");

  return ListenSocket(mConnector);
}

void
BluetoothSocket::Disconnect()
{
  if (GetConnectionStatus() != SOCKET_DISCONNECTED) {
    CloseSocket();
  }
}

void
BluetoothSocket::ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage)
{
  if (mObserver) {
    mObserver->ReceiveSocketData(aMessage);
  }
}

void
BluetoothSocket::OnConnectSuccess()
{
  if (mObserver) {
    mObserver->OnConnectSuccess();
  }
}

void
BluetoothSocket::OnConnectError()
{
  if (mObserver) {
    mObserver->OnConnectError();
  }
}

void
BluetoothSocket::OnDisconnect()
{
  if (mObserver) {
    mObserver->OnDisconnect();
  }
}

