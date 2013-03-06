/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothA2dpManager.h"
#include "BluetoothService.h"
#include "nsThreadUtils.h"

USING_BLUETOOTH_NAMESPACE

namespace {
  static nsAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{

}

BluetoothA2dpManager::~BluetoothA2dpManager()
{

}

/*static*/
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    BT_LOG("return existing BluetoothA2dpManager");
    return gBluetoothA2dpManager;
  }

  gBluetoothA2dpManager = new BluetoothA2dpManager();
  BT_LOG("A new BluetoothA2dpManager");
  return gBluetoothA2dpManager;
};

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!bs->ConnectSink(aDeviceAddress, nullptr)) {
    BT_LOG("[A2DP] Connect failed!");
    return false;
  }

  BT_LOG("[A2DP] Connect successfully!");
  return true;
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if (!bs->DisconnectSink(aDeviceAddress, nullptr)) {
    BT_LOG("[A2DP] Disconnect failed!");
    return;
  }

  BT_LOG("[A2DP] Disconnect successfully!");
}

bool
BluetoothA2dpManager::Listen()
{
  BT_LOG("[A2DP] Listen");
  return true;
}

