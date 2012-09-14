/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothHfpManager.h"

#include "BluetoothRilListener.h"
#include "BluetoothTypes.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"
#include "AudioManager.h"
#include "mozilla/ipc/Socket.h"
#include <unistd.h> /* usleep() */
#include <linux/ioctl.h>
#include <fcntl.h>

#include "mozilla/Services.h"
#include "nsIObserverService.h"

#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "Bluetooth", args)
#else
#define LOG(args...) printf(args); printf("\n");
#endif

USING_BLUETOOTH_NAMESPACE

static BluetoothHfpManager* sInstance = nullptr;
static bool sStopSendingRingFlag = true;
static nsCOMPtr<nsIThread> sHfpCommandThread;

const char* kHfpCRLF = "\xd\xa";

static void
SendLine(const char* msg)
{
  char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, msg);
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);

  BluetoothHfpManager* hfp = BluetoothHfpManager::GetManager();
  hfp->SendSocketData(s);
}

class SendRingIndicatorTask : public nsRunnable
{
public:
  SendRingIndicatorTask()
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    while (!sStopSendingRingFlag) {
      SendLine("RING");
      LOG("Sending RING...");

      usleep(3000000);
    }

    return NS_OK;
  }
};

BluetoothHfpManager::BluetoothHfpManager() : mConnected(false)
                                           , mChannel(-1)
                                           , mAddress(nullptr)
                                           , mCurrentCallState(0)
                                           , mLocalBrsf(0)
{
  mRilListener = new BluetoothRilListener(this);
  mRilListener->StartListening();

  NS_NewThread(getter_AddRefs(sHfpCommandThread));
}

BluetoothHfpManager*
BluetoothHfpManager::GetManager()
{
  if (sInstance == nullptr)
  {
    sInstance = new BluetoothHfpManager();
  }

  return sInstance;
}

BluetoothHfpManager::~BluetoothHfpManager()
{
  delete mRilListener;
}

void
BluetoothHfpManager::SendLine(const char* msg)
{
  char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, msg);
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyCindCurrentStatus()
{
  SendLine("+CIND: 5,5,1,0,0,0,0");
}

void
BluetoothHfpManager::ReplyCindRange()
{
  SendLine("+CIND: (\"battchg\",(0-5)),(\"signal\",(0-5)),(\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"roam\",(0,1))");
}

void
BluetoothHfpManager::ReplyCmer(bool enableIndicator)
{
  SendLine(enableIndicator ? "+CMER: 3,0,0,1" : "+CMER: 3,0,0,0");
}

void
BluetoothHfpManager::ReplyChldRange()
{
  SendLine("+CHLD: (0,1,2,3)");
}

void
BluetoothHfpManager::ReplyBrsf()
{
  SendLine("+BRSF: 352");
}

void
BluetoothHfpManager::ReplyOk()
{
  SendLine("OK");
}

void
BluetoothHfpManager::ReceiveSocketData(SocketRawData* aMessage)
{
  LOG("Receive message: %s", aMessage->mData);

  const char* msg = (const char*)aMessage->mData;

  if (!strncmp(msg, "AT+BRSF=", 8)) {
    ReplyBrsf();
    ReplyOk();
  } else if (!strncmp(msg, "AT+CIND=?", 9)) {
    ReplyCindRange();
    ReplyOk();
  } else if (!strncmp(msg, "AT+CIND", 7)) {
    ReplyCindCurrentStatus();
    ReplyOk();
  } else if (!strncmp(msg, "AT+CMER=", 8)) {
    ReplyOk();
  } else if (!strncmp(msg, "AT+CHLD=?", 9)) {
    ReplyChldRange();
    ReplyOk();
  } else if (!strncmp(msg, "AT+CHLD=", 8)) {
    ReplyOk();
  } else if (!strncmp(msg, "AT+VGS=", 7)) {
    ReplyOk();

    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    os->NotifyObservers(nullptr, "bluetooth-volume-up", nullptr);
    /*
    os->NotifyObservers(nsContentUtils::GetRootDocument(this),
                        "fullscreen-origin-change",
                        PromiseFlatString(aOrigin).get());
    */
  } else {
    LOG("Unhandled message, reply ok");
    ReplyOk();
  }
}

bool
BluetoothHfpManager::Connect(const nsAString& aObjectPath,
                             BluetoothReplyRunnable* aRunnable)
{
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return nullptr;
  }

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(mozilla::dom::bluetooth::BluetoothServiceUuidStr::Handsfree);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->GetSocketViaService(aObjectPath,
                                        serviceUuidStr,
                                        BluetoothSocketType::RFCOMM,
                                        true,
                                        false,
                                        this,
                                        runnable);

  runnable.forget();
  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothHfpManager::Disconnect(BluetoothReplyRunnable* aRunnable)
{
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return nullptr;
  }

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->CloseSocket(this,
																runnable);
  runnable.forget();

  return NS_FAILED(rv) ? false : true;
}

void
BluetoothHfpManager::CallStateChanged(int aCallIndex, int aCallState, 
                                      const char* aNumber, bool aIsActive)
{
  LOG("Get call state changed: index=%d, state=%d, number=%s, active=%d", 
      aCallIndex, aCallState, aNumber, aIsActive);

  nsRefPtr<nsRunnable> sendRingTask = nullptr;

  switch (aCallState) {
    case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
      // Send "CallSetup = 1"
      SendLine("+CIEV: 5,1");

      // Start sending RING indicator to HF
      sStopSendingRingFlag = false;
      sendRingTask = new SendRingIndicatorTask();
      sHfpCommandThread->Dispatch(sendRingTask, NS_DISPATCH_NORMAL);
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
      // Send "CallSetup = 2"
      SendLine("+CIEV: 5,2");
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
      // Send "CallSetup = 3"
      if (mCurrentCallIndex == nsIRadioInterfaceLayer::CALL_STATE_DIALING) {
        SendLine("+CIEV: 5,3");
      } else {
        LOG("HFP MSG ERROR: Impossible state changed from %d to %d", mCurrentCallState, aCallState);
      }
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
          // Send "Call = 1, CallSetup = 0"
          SendLine("+CIEV: 4,1");
          SendLine("+CIEV: 5,0");
          break;
        default:
          LOG("HFP MSG ERROR: Impossible state changed from %d to %d", mCurrentCallState, aCallState);
          break;
      }

      //AudioOn();
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
        case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
          // Send "CallSetup = 0"
          SendLine("+CIEV: 5,0");
          break;
        case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
          // Send "Call = 0"
          SendLine("+CIEV: 4,0");
          break;
        default:
          LOG("HFP MSG ERROR: Impossible state changed from %d to %d", mCurrentCallState, aCallState);
          break;
      }
      break;

    default:
      LOG("Not handled state.");
      break;
  }

  // Update mCurrentCallState
  mCurrentCallIndex = aCallIndex;
  mCurrentCallState = aCallState;
}
