/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothhfpmanager_h__
#define mozilla_dom_bluetooth_bluetoothhfpmanager_h__

#include "BluetoothCommon.h"
#include "mozilla/ipc/Socket.h"
#include <pthread.h>

BEGIN_BLUETOOTH_NAMESPACE

using namespace mozilla::ipc;

class BluetoothReplyRunnable;

class BluetoothHfpManager : public SocketConsumer
{
public:
  ~BluetoothHfpManager();
  static BluetoothHfpManager* GetManager();

  bool Connect(const nsAString& aObjectPath,
               BluetoothReplyRunnable* aRunnable);

	bool Disconnect(BluetoothReplyRunnable* aRunnable);
  void ReceiveSocketData(SocketRawData* aMessage);
	void Listen();
protected:
  BluetoothHfpManager();

  bool mConnected;
  int mChannel;
  char* mAddress;
  int mCurrentVgs;

private:
  void ReplyCindCurrentStatus();
  void ReplyCindRange();
  void ReplyCmer(bool enableIndicator);
  void ReplyChldRange();
  void ReplyBrsf();
  void ReplyOk();
};

END_BLUETOOTH_NAMESPACE

#endif
