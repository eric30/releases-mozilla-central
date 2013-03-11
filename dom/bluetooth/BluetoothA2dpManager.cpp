/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothA2dpManager.h"
#include "BluetoothService.h"
#include "gonk/AudioSystem.h"
#include "nsThreadUtils.h"

#include <utils/String8.h>

USING_BLUETOOTH_NAMESPACE

namespace {
  static nsAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
  mConnectedDeviceAddress.AssignLiteral(BLUETOOTH_INVALID_ADDRESS);
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

static void
SetParameter(const nsAString& aParameter)
{
  android::String8 cmd;
  cmd.appendFormat(NS_ConvertUTF16toUTF8(aParameter).get());
  android::AudioSystem::setParameters(0, cmd);
}

static void
SetupA2dpDevice(const nsAString& aDeviceAddress)
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));

  // Make a2dp device available
  android::AudioSystem::
    setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                             AUDIO_POLICY_DEVICE_STATE_AVAILABLE,
                             NS_ConvertUTF16toUTF8(aDeviceAddress).get());
}

static void
TeardownA2dpDevice(const nsAString& aDeviceAddress)
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=false"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));

  // Make a2dp device unavailable
  android::AudioSystem::
    setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                             AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
                             NS_ConvertUTF16toUTF8(aDeviceAddress).get());
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if ((mConnectedDeviceAddress != aDeviceAddress) &&
      (mConnectedDeviceAddress != NS_LITERAL_STRING(BLUETOOTH_INVALID_ADDRESS))) {
    BT_LOG("[A2DP] Connection already exists");
    return false;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!bs->ConnectSink(aDeviceAddress, nullptr)) {
    BT_LOG("[A2DP] Connect failed!");
    return false;
  }

  SetupA2dpDevice(aDeviceAddress);
  BT_LOG("[A2DP] Connect successfully!");

  mConnectedDeviceAddress = aDeviceAddress;

  return true;
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mConnectedDeviceAddress == NS_LITERAL_STRING(BLUETOOTH_INVALID_ADDRESS)) {
    return;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs->DisconnectSink(aDeviceAddress, nullptr)) {
    BT_LOG("[A2DP] Disconnect failed!");
    return;
  }

  TeardownA2dpDevice(aDeviceAddress);
  BT_LOG("[A2DP] Disconnect successfully!");

  mConnectedDeviceAddress.AssignLiteral(BLUETOOTH_INVALID_ADDRESS);
}

bool
BluetoothA2dpManager::Listen()
{
  BT_LOG("[A2DP] Listen");
  return true;
}

