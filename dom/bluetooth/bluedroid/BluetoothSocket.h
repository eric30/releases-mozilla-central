/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothSocket_h
#define mozilla_dom_bluetooth_BluetoothSocket_h

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothSocketObserver;
class DroidSocketImpl;

class BluetoothSocket : public mozilla::ipc::UnixSocketConsumer
{
public:
  BluetoothSocket(BluetoothSocketObserver* aObserver,
                  BluetoothSocketType aType,
                  bool aAuth,
                  bool aEncrypt);

  bool Connect(const nsAString& aDeviceAddress, int aChannel);
  bool Listen(int aChannel);
  inline void Disconnect()
  {
    CloseDroidSocket();
  }

  virtual void OnConnectSuccess() MOZ_OVERRIDE;
  virtual void OnConnectError() MOZ_OVERRIDE;
  virtual void OnDisconnect() MOZ_OVERRIDE;
  virtual void ReceiveSocketData(
    nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage) MOZ_OVERRIDE;

  inline void GetAddress(nsAString& aDeviceAddress)
  {
    aDeviceAddress = mDeviceAddress;
  }

  inline int GetChannel()
  {
    return mChannel;
  }

  inline int GetStatus()
  {
    return mStatus;
  }

  void CloseDroidSocket();
  bool SendDroidSocketData(mozilla::ipc::UnixSocketRawData* aData);

private:
  BluetoothSocketObserver* mObserver;
  BluetoothSocketType mType;
  bool mAuth;
  bool mEncrypt;

  bool CreateDroidSocket(bool aListen, int aFd);
  int16_t ReadInt16(const uint8_t* aData, size_t* aOffset);
  int32_t ReadInt32(const uint8_t* aData, size_t* aOffset);
  void ReadBdAddress(const uint8_t* aData, size_t* aOffset);
  bool IsSocketStatus(nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage);

  DroidSocketImpl* mImpl;
  nsString mDeviceAddress;
  int mChannel;
  int mStatus;
  int mReceivedStatusLength;
};

END_BLUETOOTH_NAMESPACE

#endif
