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
//static bool sStopEventLoopFlag = false;
//static bool sStopAcceptFlag = false;
//static int sCurrentVgs = 7;

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

/*
bool
BluetoothHfpManager::ReachedMaxConnection()
{
  // Now we can only have one conenction at a time
  return mConnected;
}

void
BluetoothHfpManager::Disconnect()
{
  void* ret;

  LOG("Disconnect");

  if (mSocket != NULL)
  {
    sStopEventLoopFlag = true;

    LOG("Before join");
    pthread_join(mEventThread, &ret);
    LOG("After join");

    mSocket->Disconnect();

    usleep(2000);

    BluetoothScoManager* scoManager = BluetoothScoManager::GetManager();
    scoManager->Disconnect();

    delete mSocket;
    mSocket = NULL;
  }

  mConnected = false;
}


void
BluetoothHfpManager::Close()
{
  void* ret;

  if (mServerSocket != NULL) {
    mServerSocket->Disconnect();

    sStopAcceptFlag = true;
    pthread_join(mAcceptThread, &ret);

    delete mServerSocket;
    mServerSocket = NULL;
  }
}

bool
BluetoothHfpManager::Listen(int channel)
{
  if (channel <= 0)
    return false;

  if (mServerSocket != NULL)
    return false;

  while(true) {
    if (mServerSocket == NULL || !mServerSocket->Available()) {
      mServerSocket = new BluetoothSocket(BluetoothSocket::TYPE_RFCOMM);
      LOG("Create a new BluetoothServerSocket for listening");
    }

    LOG("Listening to channel %d", channel);

    mChannel = channel;

    usleep(500);
    int errno = mServerSocket->Listen(channel);

    // 98 :EADDRINUSE
    if (errno == 0) {
      LOG("Channel is OK.");
      break;
    } else if (errno == 98) {
      LOG("Channel is still in use.");
      mServerSocket->Close();
    } else {
      LOG("Unexpected error: %d", errno);
      mServerSocket->Close();
    }
  }

  pthread_create(&(mAcceptThread), NULL, BluetoothHfpManager::AcceptInternal, mServerSocket);

  return true;
}

void reply_ok(int fd)
{
  if (BluetoothSocket::send_line(fd, "OK") != 0) {
    LOG("Reply [OK] failed");
  }
}

void reply_error(int fd)
{
  if (BluetoothSocket::send_line(fd, "ERROR") != 0) {
    LOG("Reply [ERROR] failed");
  }
}

void reply_brsf(int fd)
{
  if (BluetoothSocket::send_line(fd, "+BRSF: 23") != 0) {
    LOG("Reply +BRSF failed");
  }
}

void reply_cind_current_status(int fd)
{
  const char* str = "+CIND: 1,0,0,0,3,0,3";

  if (BluetoothSocket::send_line(fd, str) != 0) {
    LOG("Reply +CIND failed");
  }
}

void reply_cind_range(int fd)
{
  const char* str = "+CIND: (\"service\",(0-1)),(\"call\",(0-1)),(\"callsetup\",(0-3)), \
                     (\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0-1)), \
                     (\"battchg\",(0-5))";

  if (BluetoothSocket::send_line(fd, str) != 0) {
    LOG("Reply +CIND=? failed");
  }
}

void reply_cmer(int fd, bool enableIndicator)
{
  const char* str = enableIndicator ? "+CMER: 3,0,0,1" : "+CMER: 3,0,0,0";

  if (BluetoothSocket::send_line(fd, str) != 0) {
    LOG("Reply +CMER= failed");
  }
}

void reply_chld_range(int fd)
{
  const char* str = "+CHLD: (0,1,2,3)";

  if (BluetoothSocket::send_line(fd, str) != 0) {
    LOG("Reply +CHLD=? failed");
  }
}

void reply_vgs(int fd)
{
  char* str = "AT+VGS=";
  int vol = sCurrentVgs;
  char* vgs = "00";

  vgs[1] = (vol % 10) + '0';
  vgs[0] = (vol / 10) + '0';

  strcat(str, vgs);

  if (BluetoothSocket::send_line(fd, str) != 0) {
    LOG("Reply AT+VGS= failed");
  }
}

void*
BluetoothHfpManager::AcceptInternal(void* ptr)
{
  BluetoothSocket* serverSocket = static_cast<BluetoothSocket*>(ptr);

  // TODO(Eric)
  // Need to let it break the while loop
  sStopAcceptFlag = false;

  while (!sStopAcceptFlag) {
    int newFd = serverSocket->Accept();

    if (newFd <= 0) {
      LOG("Error occurs after accepting:%s", __FUNCTION__);
    }

    BluetoothHfpManager* manager = BluetoothHfpManager::GetManager();

    if (manager->mSocket != NULL) {
      manager->mSocket->Disconnect();
    }

    manager->mSocket = new BluetoothSocket(BluetoothSocket::TYPE_RFCOMM, newFd);

    pthread_create(&manager->mEventThread, NULL, BluetoothHfpManager::MessageHandler, manager->mSocket);

    usleep(5000);

    // Connect ok, next : establish a SCO link
    BluetoothScoManager* scoManager = BluetoothScoManager::GetManager();

    if (!scoManager->IsConnected()) {
      const char* address = serverSocket->GetAddress();
      LOG("[ERIC] SCO address : %s", address);
      scoManager->Connect(address);
    }

    // reply_vgs(newFd);
  }

  return NULL;
}

static int sendEvent(int fd, uint16_t type, uint16_t code, int32_t value)
{
  struct input_event event;

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.code = code;
  event.value = value;

  return write(fd, &event, sizeof(event));
}

static void pressKey(uint16_t keyCode)
{
  sendEvent(uinputFd, EV_KEY, keyCode, true);
  sendEvent(uinputFd, EV_SYN, SYN_REPORT, 0);

  sendEvent(uinputFd, EV_KEY, keyCode, false);
  sendEvent(uinputFd, EV_SYN, SYN_REPORT, 0);
}

void*
BluetoothHfpManager::MessageHandler(void* ptr)
{
  BluetoothSocket* socket = static_cast<BluetoothSocket*>(ptr);
  int err;
  sStopEventLoopFlag = false;

  while (!sStopEventLoopFlag)
  {
    int timeout = 500; //0.5 sec
    char buf[256];

    const char *ret = BluetoothSocket::get_line(socket->mFd,
        buf, sizeof(buf),
        timeout,
        &err);

    if (ret == NULL) {
      LOG("HFP: Read Nothing");
    } else {
      LOG("HFP: Received:%s", ret);

      if (!strncmp(ret, "AT+BRSF=", 8)) {
        reply_brsf(socket->mFd);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+CIND=?", 9)) {
        reply_cind_range(socket->mFd);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+CIND", 7)) {
        reply_cind_current_status(socket->mFd);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+CMER=", 8)) {
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+CHLD=?", 9)) {
        reply_chld_range(socket->mFd);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+CHLD=", 9)) {
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+VGS=", 7)) {
        // Get vgs value in the msg
        int newVgs = ret[7] - '0';

        if (strlen(ret) > 8) {
          newVgs = newVgs * 10 + (ret[8] - '0');
        }

        if (newVgs > sCurrentVgs) {
          pressKey(KEY_VOLUMEUP);
        } else if (newVgs < sCurrentVgs) {
          pressKey(KEY_VOLUMEDOWN);
        }

        sCurrentVgs = newVgs;

        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+VGM=", 7)) {
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "ATA", 3)) {
        LOG("Answer the call!");
        pressKey(KEY_F23);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+BLDN", 7)) {
        // Obvious that's wrong, however it's just for demo.
        LOG("Hang up the call!");
        pressKey(KEY_F24);
        reply_ok(socket->mFd);
      } else if (!strncmp(ret, "AT+BVRA", 7)) {
        reply_error(socket->mFd);
        mozilla::dom::gonk::AudioManager::SetAudioRoute(3);
      } else if (!strncmp(ret, "OK", 2)) {
        // Do nothing
        LOG("Got an OK");
      } else {
        LOG("Not handled.");
        reply_ok(socket->mFd);
      }
    }
  }

  return NULL;
}
*/

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
                                        runnable);

  runnable.forget();
  return NS_FAILED(rv) ? false : true;
}
