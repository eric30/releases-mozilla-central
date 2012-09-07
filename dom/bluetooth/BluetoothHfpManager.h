/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothhfpmanager_h__
#define mozilla_dom_bluetooth_bluetoothhfpmanager_h__

#include "BluetoothCommon.h"
#include <pthread.h>

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothHfpManager
{
public:
  static BluetoothHfpManager* GetManager();
  /*
  bool Connect(int channel, const char* asciiAddress);
  void Disconnect();
  bool ReachedMaxConnection();
  bool Listen(int channel);
  void Close();
  pthread_t mEventThread;
  */

protected:
  BluetoothHfpManager();
  /*
  static void* MessageHandler(void* ptr);
  static void* AcceptInternal(void* ptr);

  pthread_t mAcceptThread;
  */

  bool mConnected;
  int mChannel;
  char* mAddress;
  int mCurrentVgs;
};

END_BLUETOOTH_NAMESPACE

#endif
