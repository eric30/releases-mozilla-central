/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothA2dpManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"

#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsContentUtils.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "A2DP", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

namespace {
StaticRefPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
}

bool
BluetoothA2dpManager::Init()
{
  mSocketStatus = GetConnectionStatus();

  return true;
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{
}

//static
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    return gBluetoothA2dpManager;
  }

  // Create new instance, register, return
  nsRefPtr<BluetoothA2dpManager> manager = new BluetoothA2dpManager();
  NS_ENSURE_TRUE(manager, nullptr);

  if (!manager->Init()) {
    return nullptr;
  }

  gBluetoothA2dpManager = manager;
  return gBluetoothA2dpManager;
}

// Virtual function of class SocketConsumer
void
BluetoothA2dpManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  // A2dp socket do nothing here
  MOZ_NOT_REACHED("This should never be called!");
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress,
                              BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    LOG("Couldn't get BluetoothService");
    return false;
  }

  // TODO(Eric)
  // Please decide what should be passed into function ConnectSink()
  bs->ConnectSink();

  return true;
}

bool
BluetoothA2dpManager::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());
  
  // TODO(Eric)
  // Needs implementation to really "Listen to remote A2DP connection request"

  return true;
}

void
BluetoothA2dpManager::Disconnect()
{
  BluetoothService* bs = BluetoothService::Get();
  if(!bs) {
    LOG("Couldn't get BluetoothService");
    return;
  }

  bs->DisconnectSink();
}

void
BluetoothA2dpManager::OnConnectSuccess()
{
  nsString address;
  GetSocketAddr(address);

  mSocketStatus = GetConnectionStatus();
}

void
BluetoothA2dpManager::OnConnectError()
{
  CloseSocket();
  mSocketStatus = GetConnectionStatus();
  Listen();
}

void
BluetoothA2dpManager::OnDisconnect()
{
  if (mSocketStatus == SocketConnectionStatus::SOCKET_CONNECTED) {
    Listen();
  }
}
