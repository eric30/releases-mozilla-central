/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothScoManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"

#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsContentUtils.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"

#define BLUETOOTH_SCO_STATUS_CHANGED "bluetooth-sco-status-changed"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

class mozilla::dom::bluetooth::BluetoothScoManagerObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  BluetoothScoManagerObserver()
  {
  }

  bool Init()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    MOZ_ASSERT(obs);

    if (NS_FAILED(obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false))) {
      NS_WARNING("Failed to add shutdown observer!");
      return false;
    }
    return true;
  }

  bool Shutdown()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (!obs ||
        (NS_FAILED(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID)))) {
      NS_WARNING("Can't unregister observers!");
      return false;
    }
    return true;
  }

  ~BluetoothScoManagerObserver()
  {
    Shutdown();
  }
};

void
BluetoothScoManager::NotifyAudioManager(const nsAString& aAddress) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE_VOID(obs);

  if (aAddress.IsEmpty()) {
    if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_SCO_STATUS_CHANGED, nullptr))) {
      NS_WARNING("Failed to notify bluetooth-sco-status-changed observsers!");
      return;
    }
  } else {
    if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_SCO_STATUS_CHANGED, aAddress.BeginReading()))) {
      NS_WARNING("Failed to notify bluetooth-sco-status-changed observsers!");
      return;
    }
  }

  nsCOMPtr<nsIAudioManager> am =
    do_GetService("@mozilla.org/telephony/audiomanager;1");
  NS_ENSURE_TRUE_VOID(am);
  am->SetForceForUse(am->USE_COMMUNICATION, am->FORCE_BT_SCO);
}

NS_IMPL_ISUPPORTS1(BluetoothScoManagerObserver, nsIObserver)

namespace {
nsAutoPtr<BluetoothScoManager> gBluetoothScoManager;
StaticRefPtr<BluetoothScoManagerObserver> sScoObserver;
bool gInShutdown = false;
} // anonymous namespace

NS_IMETHODIMP
BluetoothScoManagerObserver::Observe(nsISupports* aSubject,
                                     const char* aTopic,
                                     const PRUnichar* aData)
{
  MOZ_ASSERT(gBluetoothScoManager);
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {    
    return gBluetoothScoManager->HandleShutdown();
  }

  MOZ_ASSERT(false, "BluetoothScoManager got unexpected topic!");
  return NS_ERROR_UNEXPECTED;
}

BluetoothScoManager::BluetoothScoManager()
{
}

bool
BluetoothScoManager::Init()
{
  Listen();

  sScoObserver = new BluetoothScoManagerObserver();
  if (!sScoObserver->Init()) {
    NS_WARNING("Cannot set up SCO observers!");
  }
  return true;
}

BluetoothScoManager::~BluetoothScoManager()
{
  Cleanup();
}

void
BluetoothScoManager::Cleanup()
{
  sScoObserver->Shutdown();
  sScoObserver = nullptr;
}

//static
BluetoothScoManager*
BluetoothScoManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothScoManager) {
    return gBluetoothScoManager;
  }

  // If we're in shutdown, don't create a new instance
  if (gInShutdown) {
    NS_WARNING("BluetoothScoManager can't be created during shutdown");
    return nullptr;
  }

  gBluetoothScoManager = new BluetoothScoManager();
  if (!gBluetoothScoManager->Init()) {
    return nullptr;
  }

  return gBluetoothScoManager;
}

// Virtual function of class SocketConsumer
void
BluetoothScoManager::ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage)
{
  // SCO socket do nothing here
  MOZ_NOT_REACHED("This should never be called!");
}

nsresult
BluetoothScoManager::HandleShutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  gInShutdown = true;
  mSocket->CloseSocket();
  gBluetoothScoManager = nullptr;
  return NS_OK;
}

bool
BluetoothScoManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (gInShutdown) {
    MOZ_ASSERT(false, "Connect called while in shutdown!");
    return false;
  }

  if (mSocket->GetConnectionStatus() == SocketConnectionStatus::SOCKET_CONNECTED ||
      mSocket->GetConnectionStatus() == SocketConnectionStatus::SOCKET_CONNECTING) {
    NS_WARNING("Sco socket has been connected");
    return false;
  }

  if (mSocket->GetConnectionStatus() != SocketConnectionStatus::SOCKET_DISCONNECTED) {
    LOG("Close socket");
    mSocket->CloseSocket();
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);


  BluetoothUnixSocketConnector* c =
    new BluetoothUnixSocketConnector(BluetoothSocketType::SCO, -1, true, true);
  mSocket = new BluetoothSocket(c, this);

  return mSocket->Connect(aDeviceAddress);
}

bool
BluetoothScoManager::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (gInShutdown) {
    MOZ_ASSERT(false, "Listen called while in shutdown!");
    return false;
  }

  if (mSocket) {
    if (mSocket->GetConnectionStatus() == SocketConnectionStatus::SOCKET_LISTENING) {
      NS_WARNING("BluetoothHfpManager has been already listening");
      return true;
    }

    mSocket->CloseSocket();
  }

  // Initialize listening socket
  BluetoothUnixSocketConnector* c = new BluetoothUnixSocketConnector(
    BluetoothSocketType::SCO, -1, true, true);
  mSocket = new BluetoothSocket(c, this);

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!mSocket->Listen()) {
    LOG("[SCO] Can't listen on socket!");
    return false;
  }

  mSocketStatus = mSocket->GetConnectionStatus();

  return true;

}

void
BluetoothScoManager::Disconnect()
{
  if (mSocket->GetConnectionStatus() == SocketConnectionStatus::SOCKET_DISCONNECTED) {
    NS_WARNING("BluetoothScoManager has been disconnected!");
    return;
  }

  mSocket->CloseSocket();
}

void
BluetoothScoManager::OnConnectSuccess()
{
  nsString address;
  mSocket->GetSocketAddr(address);
  NotifyAudioManager(address);

  mSocketStatus = mSocket->GetConnectionStatus();
}

void
BluetoothScoManager::OnConnectError()
{
  mSocket->CloseSocket();
  mSocketStatus = mSocket->GetConnectionStatus();
  Listen();
}

void
BluetoothScoManager::OnDisconnect()
{
  if (mSocketStatus == SocketConnectionStatus::SOCKET_CONNECTED) {
    Listen();

    nsString address = NS_LITERAL_STRING("");
    NotifyAudioManager(address);
  }
}

