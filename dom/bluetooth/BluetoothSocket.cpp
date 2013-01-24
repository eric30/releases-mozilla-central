/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothSocket.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

BluetoothSocket::BluetoothSocket(BluetoothProfileManager* aManager) : UnixSocketConsumer()
{
  mManager = aManager;
}

BluetoothSocket::~BluetoothSocket()
{
  LOG("Bluetooth Socket dtor");
}

void BluetoothSocket::ReceiveSocketData(UnixSocketRawData* aMessage)
{
  LOG("BluetoothSocket::ReceiveSocketData");
  mManager->ReceiveSocketData(aMessage);
}

void BluetoothSocket::OnConnectSuccess()
{
  LOG("BluetoothSocket::OnConnectSuccess");
  mManager->OnConnectSuccess();
}

void BluetoothSocket::OnConnectError()
{
  LOG("BluetoothSocket::OnConnectError");
  mManager->OnConnectError();
}

void BluetoothSocket::OnDisconnect()
{
  LOG("BluetoothSocket::OnDisconnect");
  mManager->OnDisconnect();
}

