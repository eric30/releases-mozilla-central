/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothOppManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"
#include "ObexBase.h"

#include "mozilla/RefPtr.h"
#include "nsIInputStream.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE
using namespace mozilla::ipc;

static mozilla::RefPtr<BluetoothOppManager> sInstance;
static bool sPause;

class ReadFileTask : public nsRunnable
{
public:
  ReadFileTask(nsIDOMBlob* aBlob, int aMaxLength) : mBlob(aBlob)
                                               , mMaxLength(aMaxLength)
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    if (NS_IsMainThread()) {
      NS_WARNING("Can't read file from main thread");
      return NS_ERROR_FAILURE;
    }

    nsresult rv;
    nsCOMPtr<nsIDOMFile> file = do_QueryInterface(mBlob);
    nsString fileName;
    if (file) {
      rv = file->GetName(fileName);
    }

    if (!file || fileName.IsEmpty()) {
      fileName.AssignLiteral("Unknown.txt");
    }

    nsString contentType;
    rv = mBlob->GetType(contentType);
    if (NS_FAILED(rv)) {
      NS_WARNING("Can't get content type");
      return NS_ERROR_FAILURE;
    }

    uint64_t fileSize;
    rv = mBlob->GetSize(&fileSize);
    if (NS_FAILED(rv)) {
      NS_WARNING("Can't get size");
      return NS_ERROR_FAILURE;
    }

    LOG("Blob Size: %d", fileSize);
    // xxx 625 is a temp value, use fileSize
    fileSize = 625;

    nsCOMPtr<nsIInputStream> stream;
    rv = mBlob->GetInternalStream(getter_AddRefs(stream));
    if (NS_FAILED(rv)) {
      NS_WARNING("Can't get internal stream of blob");
      return NS_ERROR_FAILURE;
    }
  
    sPause = true;
    sInstance->SendPutHeaderRequest(fileName, fileSize);
    while(sPause) {};

    LOG("Put Header sent");

    // ConsumeStream, referenced from https://developer.mozilla.org/
    // en-US/docs/XPCOM_Interface_Reference/nsIInputStream
    PRUint32 numRead;
    char buf[1024];
    int offset = 0;

    while (offset + numRead < fileSize) {
      // function inputstream->Read() only works on non-main thread
      rv = stream->Read(buf, sizeof(buf), &numRead);
      if (NS_FAILED(rv)) {
        // Needs error handling
        LOG("### error reading stream: %x\n", rv);
        break;
      }

      if (numRead == 0) {
        LOG("No more bytes can be read");
        break;
      }

      if (offset + numRead == fileSize) {
        sInstance->SendPutRequest((uint8_t*)buf, numRead, true);
      } else {
        sInstance->SendPutRequest((uint8_t*)buf, numRead, false);
      }

      offset += numRead;
    }

    return NS_OK;
  };

private:
  nsCOMPtr<nsIDOMBlob> mBlob;
  int mMaxLength;
};

BluetoothOppManager::BluetoothOppManager() : mConnected(false)
                                           , mConnectionId(1)
                                           , mLastCommand(0)
                                           , mBlob(nullptr)
                                           , mRemoteObexVersion(0)
                                           , mRemoteConnectionFlags(0)
                                           , mRemoteMaxPacketLength(0)

{
}

BluetoothOppManager::~BluetoothOppManager()
{
}

//static
BluetoothOppManager*
BluetoothOppManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (sInstance == nullptr) {
    sInstance = new BluetoothOppManager();
  }

  return sInstance;
}

bool
BluetoothOppManager::Connect(const nsAString& aDeviceObjectPath,
                             BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(mozilla::dom::bluetooth::BluetoothServiceUuidStr::ObjectPush);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->GetSocketViaService(aDeviceObjectPath,
                                        serviceUuidStr,
                                        BluetoothSocketType::RFCOMM,
                                        true,
                                        false,
                                        this,
                                        runnable);

  runnable.forget();
  return NS_FAILED(rv) ? false : true;
}

void
BluetoothOppManager::Disconnect()
{
  CloseSocket();
}

bool
BluetoothOppManager::SendFile(BlobParent* aActor,
                              BluetoothReplyRunnable* aRunnable)
{
  if (mBlob) {
    // Means there's a sending process. Reply error.
    return false;
  }

  /*
   * Process of sending a file:
   *  - Keep blob because OPP connection has not been established yet.
   *  - Create an OPP connection by SendConnectRequest() 
   *  - After receiving the response, start to read file and send.
   */
  mBlob = aActor->GetBlob();

  SendConnectRequest();

  return true;
}

bool
BluetoothOppManager::StopSendingFile(BluetoothReplyRunnable* aRunnable)
{
  // will implement in another patch.
  return true;
}

// Virtual function of class SocketConsumer
void
BluetoothOppManager::ReceiveSocketData(UnixSocketRawData* aMessage)
{
  uint8_t responseCode = aMessage->mData[0];
  int packetLength = (((int)aMessage->mData[1]) << 8) | aMessage->mData[2];
  int receivedLength = aMessage->mSize;

  if (mLastCommand == ObexRequestCode::Connect) {
    if (responseCode == ObexResponseCode::Success) {
      mConnected = true;

      // Keep remote information
      mRemoteObexVersion = aMessage->mData[3];
      mRemoteConnectionFlags = aMessage->mData[4];
      mRemoteMaxPacketLength = 
        (((int)(aMessage->mData[5]) << 8) | aMessage->mData[6]);

      if (mBlob) {
        nsCOMPtr<nsIThread> t;
        NS_NewThread(getter_AddRefs(t));

        nsRefPtr<ReadFileTask> task 
          = new ReadFileTask(mBlob, mRemoteMaxPacketLength);

        if (NS_FAILED(t->Dispatch(task, NS_DISPATCH_NORMAL))) {
          LOG("Can't dispatch");
          NS_WARNING("Cannot dispatch ring task!");
        }
      }
    }
  } else if (mLastCommand == ObexRequestCode::Disconnect) {
    if (responseCode != ObexResponseCode::Success) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] Disconnect failed");
    } else {
      mConnected = false;
      mBlob = nullptr;
    }
  } else if (mLastCommand == ObexRequestCode::Put) {
    if (responseCode != ObexResponseCode::Continue) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] Put failed");
    } else {
      // Keep putting
      sPause = false;

      LOG("Set Pause to false");
    }
  } else if (mLastCommand == ObexRequestCode::PutFinal) {
    if (responseCode != ObexResponseCode::Success) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] PutFinal failed");
    } else {
      SendDisconnectRequest();
    }
  }
}

void
BluetoothOppManager::SendConnectRequest()
{
  if (mConnected) return;

  // Section 3.3.1 "Connect", IrOBEX 1.2
  // [opcode:1][length:2][version:1][flags:1][MaxPktSizeWeCanReceive:2][Headers:var]
  uint8_t req[255];
  int index = 7;

  req[3] = 0x10; // version=1.0
  req[4] = 0x00; // flag=0x00
  req[5] = BluetoothOppManager::MAX_PACKET_LENGTH >> 8;
  req[6] = BluetoothOppManager::MAX_PACKET_LENGTH;

  index += AppendHeaderConnectionId(&req[index], mConnectionId++);
  SetObexPacketInfo(req, ObexRequestCode::Connect, index);
  mLastCommand = ObexRequestCode::Connect;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}

void
BluetoothOppManager::SendPutHeaderRequest(const nsAString& aFileName, int aFileSize)
{
  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];
  
  const PRUnichar* fileNamePtr = aFileName.BeginReading();
  uint32_t len = aFileName.Length();
  uint8_t* fileName = new uint8_t[(len + 1) * 2];
  for (int i = 0; i < len; i++) {
    fileName[i * 2] = (uint8_t)(fileNamePtr[i] >> 8);
    fileName[i * 2 + 1] = (uint8_t)fileNamePtr[i];
  }

  fileName[len * 2] = 0x00;
  fileName[len * 2 + 1] = 0x00;

  int index = 3;
  index += AppendHeaderConnectionId(&req[index], mConnectionId);
  index += AppendHeaderName(&req[index], (char*)fileName, (len + 1) * 2);
  index += AppendHeaderLength(&req[index], aFileSize);

  SetObexPacketInfo(req, ObexRequestCode::Put, index);
  mLastCommand = ObexRequestCode::Put;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);

  delete [] req;
}

void
BluetoothOppManager::SendPutRequest(uint8_t* aFileBody, int aFileBodyLength,
                                    bool aFinal)
{
  if (!mConnected) return;

  // IrOBEX 1.2 3.3.3
  // [opcode:1][length:2][Headers:var]
  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];

  int sentFileBodyLength = 0;
  int index = 3;

  LOG("Send [Put] Request: fileBodyLength: %d, max length:%d", 
    aFileBodyLength, mRemoteMaxPacketLength);

  sPause = false;

  while (aFileBodyLength > sentFileBodyLength) {
    LOG("Prepare to send ...");
    while (sPause) {}
    sPause = true;
    LOG("Starting ...");

    int packetLeftSpace = mRemoteMaxPacketLength - index - 3;
    LOG("Packet left space: %d", packetLeftSpace);

    if (aFileBodyLength <= packetLeftSpace) {
      index += AppendHeaderBody(&req[index], &aFileBody[sentFileBodyLength], aFileBodyLength);
      sentFileBodyLength += aFileBodyLength;
    } else {
      index += AppendHeaderBody(&req[index], &aFileBody[sentFileBodyLength], packetLeftSpace);
      sentFileBodyLength += packetLeftSpace;
    }

    LOG("Sent file body length: %d", sentFileBodyLength);

    if (aFinal && sentFileBodyLength >= aFileBodyLength) {
      SetObexPacketInfo(req, ObexRequestCode::PutFinal, index);
      mLastCommand = ObexRequestCode::PutFinal;
    } else {
      SetObexPacketInfo(req, ObexRequestCode::Put, index);
      mLastCommand = ObexRequestCode::Put;
    }

    UnixSocketRawData* s = new UnixSocketRawData(index);
    memcpy(s->mData, req, s->mSize);
    SendSocketData(s);

    index = 3;

    LOG("before deleting");

    delete [] req;
    req = new uint8_t[mRemoteMaxPacketLength];
  }

  delete [] req;
}

void
BluetoothOppManager::SendDisconnectRequest()
{
  // IrOBEX 1.2 3.3.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int index = 3;

  SetObexPacketInfo(req, ObexRequestCode::Disconnect, index);
  mLastCommand = ObexRequestCode::Disconnect;

  UnixSocketRawData* s = new UnixSocketRawData(index);
  memcpy(s->mData, req, s->mSize);
  SendSocketData(s);
}
