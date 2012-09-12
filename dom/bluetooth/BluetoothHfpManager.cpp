#include "base/basictypes.h"

#include "BluetoothTypes.h"
#include "BluetoothHfpManager.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"
#include "AudioManager.h"
#include "mozilla/ipc/Socket.h"
#include <unistd.h> /* usleep() */
#include <linux/ioctl.h>
#include <fcntl.h>

#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "Bluetooth", args)
#else
#define LOG(args...) printf(args); printf("\n");
#endif

USING_BLUETOOTH_NAMESPACE

static BluetoothHfpManager* sInstance = nullptr;

const char* kHfpCRLF = "\xd\xa";

BluetoothHfpManager::BluetoothHfpManager() : mConnected(false)
                                           , mChannel(-1)
                                           , mAddress(nullptr)
{
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

}

void
BluetoothHfpManager::ReplyCindCurrentStatus()
{
  const char* str = "+CIND: 5,5,1,0,0,0,0";
	char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, str);
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyCindRange()
{
  const char* str = "+CIND: (\"battchg\",(0-5)),(\"signal\",(0-5)),(\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"roam\",(0,1))";

	char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, str);
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyCmer(bool enableIndicator)
{
  const char* str = enableIndicator ? "+CMER: 3,0,0,1" : "+CMER: 3,0,0,0";
	char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, str);
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyChldRange()
{
	char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, "+CHLD: (0,1,2,3)");
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyBrsf()
{
	char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, "+BRSF: 352");
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReplyOk()
{
  char response[256] = {'\0'};

  strcat(response, kHfpCRLF);
  strcat(response, "OK");
  strcat(response, kHfpCRLF);

  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void
BluetoothHfpManager::ReceiveSocketData(SocketRawData* aMessage)
{
  LOG("Receive message: %s", aMessage->mData);

  const char* msg = (const char*)aMessage->mData;

  if (!strncmp(msg, "AT+BRSF=", 8)) {
    ReplyBrsf();
    ReplyOk();
  }else if (!strncmp(msg, "AT+CIND=?", 9)) {
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
  } else if (!strncmp(msg, "AT+CHLD=", 9)) {
    ReplyOk();
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

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(mozilla::dom::bluetooth::BluetoothServiceUuidStr::Handsfree);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->CloseSocket(this,
																runnable);
  runnable.forget();

  return NS_FAILED(rv) ? false : true;
}
