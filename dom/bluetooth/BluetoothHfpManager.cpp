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

*/

BluetoothHfpManager::~BluetoothHfpManager()
{

}

void 
BluetoothHfpManager::ReplyCindCurrentStatus()
{
  const char* str = "+CIND: 1,0,0,0,3,0,3";
  char response[256];

  strcat(response, kHfpCRLF);
  strcat(response, str);
  strcat(response, kHfpCRLF);
    
  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void 
BluetoothHfpManager::ReplyCindRange()
{
  const char* str = "+CIND: (\"service\",(0-1)),(\"call\",(0-1)),(\"callsetup\",(0-3)), \
                     (\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0-1)), \
                     (\"battchg\",(0-5))";  
                     
  char response[256];

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
  char response[256];

  strcat(response, kHfpCRLF);
  strcat(response, str);
  strcat(response, kHfpCRLF);
    
  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void 
BluetoothHfpManager::ReplyChldRange()
{  
  char response[256];

  strcat(response, kHfpCRLF);
  strcat(response, "+CHLD: (0,1,2,3)");
  strcat(response, kHfpCRLF);
    
  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void 
BluetoothHfpManager::ReplyBrsf()
{    
  char response[256];

  strcat(response, kHfpCRLF);
  strcat(response, "+BRSF: 23");
  strcat(response, kHfpCRLF);
    
  mozilla::ipc::SocketRawData* s = new mozilla::ipc::SocketRawData(response);
  SendSocketData(s);
}

void 
BluetoothHfpManager::ReplyOk()
{    
  char response[256];

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
