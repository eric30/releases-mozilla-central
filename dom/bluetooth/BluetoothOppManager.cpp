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

class ReadFileTask : public nsRunnable
{
public:
  ReadFileTask(nsIDOMBlob* aBlob) : mBlob(aBlob)
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
      LOG("File name: %s", NS_ConvertUTF16toUTF8(fileName).get());
    }

    if (!file || fileName.IsEmpty()) {
      fileName.AssignLiteral("Unknown.txt");
    }

    nsString contentType;
    rv = mBlob->GetType(contentType);

    uint64_t length;
    rv = mBlob->GetSize(&length);

    nsCOMPtr<nsIInputStream> stream;
    rv = mBlob->GetInternalStream(getter_AddRefs(stream));
    if (NS_FAILED(rv)) {
      NS_WARNING("Can't get internal stream of blob");
      return NS_ERROR_FAILURE;
    }

    // xxxxxxx obviously 255 is not a good choice
    char buffer[255] = {'\0'};
    uint32_t readBytes;
    stream->Read(&buffer[0], 255, &readBytes);
    //LOG("Read bytes count = %u", readBytes);

    const char* fileNameUtf8 = NS_ConvertUTF16toUTF8(fileName).get();
    int charLength = (strlen(fileNameUtf8) + 1)* 2;
    uint8_t* tempName = new uint8_t[charLength];

    for (int i = 0; i < charLength; ++i) {
      if (i % 2 > 0) {
        if (i == charLength - 1) {
          tempName[i] = 0x00;
        } else {
          tempName[i] = fileNameUtf8[i / 2];
        }
      } else {
        tempName[i] = 0x00;
      }

      LOG("Temp[%d]:%x", i, tempName[i]);
    }

    sInstance->SendPutReqeust(tempName, charLength, (uint8_t*)buffer, readBytes);

    return NS_OK;
  };

private:
  nsCOMPtr<nsIDOMBlob> mBlob;
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

  SendConnectReqeust();

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
      mRemoteMaxPacketLength = (((int)(aMessage->mData[5]) << 8) | aMessage->mData[6]);

      if (mBlob) {
        nsCOMPtr<nsIThread> t;
        NS_NewThread(getter_AddRefs(t));

        ReadFileTask* task = new ReadFileTask(mBlob);

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
      // xxxxxx Keep putting?
    }
  } else if (mLastCommand == ObexRequestCode::PutFinal) {
    if (responseCode != ObexResponseCode::Success) {
      // FIXME: Needs error handling here
      NS_WARNING("[OPP] PutFinal failed");
    } else {
      SendDisconnectReqeust();
    }
  }
}

void
BluetoothOppManager::SendConnectReqeust()
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
BluetoothOppManager::SendPutReqeust(uint8_t* fileName, int fileNameLength,
                                    uint8_t* fileBody, int fileBodyLength)
{
  if (!mConnected) return;

  // IrOBEX 1.2 3.3.3
  // [opcode:1][length:2][Headers:var]
  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];

  int sentFileBodyLength = 0;
  int index = 3;

  LOG("Send [Put] Request: fileBodyLength: %d, max length:%d", 
    fileBodyLength, mRemoteMaxPacketLength);

  index += AppendHeaderConnectionId(&req[index], mConnectionId);
  index += AppendHeaderName(&req[index], (char*)fileName, fileNameLength);
  index += AppendHeaderLength(&req[index], fileBodyLength);

  while (fileBodyLength > sentFileBodyLength) {
    int packetLeftSpace = mRemoteMaxPacketLength - index - 3;

    if (fileBodyLength <= packetLeftSpace) {
      index += AppendHeaderBody(&req[index], &fileBody[sentFileBodyLength], fileBodyLength);
      sentFileBodyLength += fileBodyLength;
    } else {
      index += AppendHeaderBody(&req[index], &fileBody[sentFileBodyLength], packetLeftSpace);
      sentFileBodyLength += packetLeftSpace;
    }

    LOG("Sent file body length: %d", sentFileBodyLength);

    if (sentFileBodyLength >= fileBodyLength) {
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
  }

  delete [] req;
}

void
BluetoothOppManager::SendDisconnectReqeust()
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
