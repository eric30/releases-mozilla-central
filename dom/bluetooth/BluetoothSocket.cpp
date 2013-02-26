/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothSocket.h"

USING_BLUETOOTH_NAMESPACE

BluetoothSocket::BluetoothSocket(BluetoothSocketType aType,
                                 bool aAuth,
                                 bool aEncrypt,
                                 BluetoothSocketObserver* aObserver)
  : UnixSocketConsumer()
  , mType(aType)
  , mAuth(aAuth)
  , mEncrypt(aEncrypt)
  , mObserver(aObserver)
{
  MOZ_ASSERT(aObserver);
}

BluetoothSocket::~BluetoothSocket()
{
}

bool
BluetoothSocket::Connect(const nsACString& aDeviceAddress, int aChannel)
{
  BluetoothUnixSocketConnector* c =
    new BluetoothUnixSocketConnector(mType, aChannel, mAuth, mEncrypt);

  if (!ConnectSocket(c, aDeviceAddress.BeginReading())) {
    nsAutoString addr;
    GetAddress(addr);
    BT_LOG("Already Connected/Connecting/Listening: %s",
           NS_ConvertUTF16toUTF8(addr).get());
    return false;
  }
  
  return true;
}

bool
BluetoothSocket::Listen(int aChannel)
{
  BluetoothUnixSocketConnector* c =
    new BluetoothUnixSocketConnector(mType, aChannel, mAuth, mEncrypt);

  if (!ListenSocket(c)) {
    nsAutoString addr;
    GetAddress(addr);
    BT_LOG("Already Connected/Connecting/Listening: %s",
           NS_ConvertUTF16toUTF8(addr).get());
    return false;
  }

  return true;
}

void
BluetoothSocket::Disconnect()
{
  CloseSocket();
}

void
BluetoothSocket::ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage)
{
  mObserver->ReceiveSocketData(aMessage);
}

void
BluetoothSocket::OnConnectSuccess()
{
  mObserver->OnConnectSuccess();
}

void
BluetoothSocket::OnConnectError()
{
  mObserver->OnConnectError();
}

void
BluetoothSocket::OnDisconnect()
{
  mObserver->OnDisconnect();
}

void
BluetoothSocket::GetAddress(nsAString& aDeviceAddress)
{
  GetSocketAddr(aDeviceAddress);
}

BluetoothSocketState
BluetoothSocket::GetState() const
{
  return (BluetoothSocketState)GetConnectionStatus();
}
