/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothRilListener.h"

#include "BluetoothCommon.h"
#include "BluetoothHfpManager.h"

#include "nsRadioInterfaceLayer.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"

#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "BluetoothRilListener", args)
#else
#define LOG(args...) printf(args); printf("\n");
#endif

USING_BLUETOOTH_NAMESPACE

class BluetoothRILTelephonyCallback : public nsIRILTelephonyCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIRILTELEPHONYCALLBACK

  BluetoothRILTelephonyCallback(BluetoothHfpManager* aHfp) : mHfp(aHfp) {}

private:
  BluetoothHfpManager* mHfp;
};

NS_IMPL_ISUPPORTS1(BluetoothRILTelephonyCallback, nsIRILTelephonyCallback)

NS_IMETHODIMP
BluetoothRILTelephonyCallback::CallStateChanged(PRUint32 aCallIndex,
                                                PRUint16 aCallState,
                                                const nsAString& aNumber,
                                                bool aIsActive)
{
  mHfp->CallStateChanged(aCallIndex, aCallState, 
                         NS_ConvertUTF16toUTF8(aNumber).get(), aIsActive);

  return NS_OK;
}

NS_IMETHODIMP
BluetoothRILTelephonyCallback::EnumerateCallState(PRUint32 aCallIndex,
                                                  PRUint16 aCallState,
                                                  const nsAString_internal& aNumber,
                                                  bool aIsActive,
                                                  bool* aResult)
{
  LOG("Enumerate Call State: call index=%d, call state=%d, active=%d", 
      aCallIndex, aCallState, aIsActive);

  *aResult = true;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothRILTelephonyCallback::NotifyError(PRInt32 aCallIndex, 
                                           const nsAString& aError)
{
  LOG("Error occured in RIL: %s [Call Index = %d]", 
      NS_ConvertUTF16toUTF8(aError).get(), aCallIndex);

  return NS_OK;
}

BluetoothRilListener::BluetoothRilListener(BluetoothHfpManager* aHfp) : mHfp(aHfp)
{
  mRIL = do_GetService(NS_RILCONTENTHELPER_CONTRACTID);
  mRILTelephonyCallback = new BluetoothRILTelephonyCallback(mHfp);
}

void
BluetoothRilListener::StartListening()
{
  nsresult rv = mRIL->EnumerateCalls(mRILTelephonyCallback);
  rv = mRIL->RegisterTelephonyCallback(mRILTelephonyCallback);
}

void
BluetoothRilListener::StopListening()
{
  nsresult rv = mRIL->UnregisterTelephonyCallback(mRILTelephonyCallback);
}
