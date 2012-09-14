/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothrillistener_h__
#define mozilla_dom_bluetooth_bluetoothrillistener_h__

#include "BluetoothCommon.h"

#include "nsCOMPtr.h"
#include "nsIDOMTelephony.h"
#include "nsIDOMTelephonyCall.h"
#include "nsIRadioInterfaceLayer.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothHfpManager;

class BluetoothRilListener
{
  public:
    BluetoothRilListener(BluetoothHfpManager*);

    void StartListening();
    void StopListening();

  private:
    nsCOMPtr<nsIRILContentHelper> mRIL;
    nsCOMPtr<nsIRILTelephonyCallback> mRILTelephonyCallback;
    BluetoothHfpManager* mHfp;
};

END_BLUETOOTH_NAMESPACE

#endif
