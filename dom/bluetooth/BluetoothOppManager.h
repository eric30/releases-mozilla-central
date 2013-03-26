/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothoppmanager_h__
#define mozilla_dom_bluetooth_bluetoothoppmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothSocket.h"
#include "BluetoothSocketObserver.h"
#include "mozilla/dom/ipc/Blob.h"
#include "mozilla/ipc/UnixSocket.h"
#include "DeviceStorage.h"

class nsIOutputStream;
class nsIInputStream;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;
class ObexHeaderSet;

class BluetoothOppManager : public BluetoothSocketObserver
{
public:
  /*
   * Channel of reserved services are fixed values, please check
   * function add_reserved_service_records() in
   * external/bluetooth/bluez/src/adapter.c for more information.
   */
  static const int DEFAULT_OPP_CHANNEL = 10;
  static const int MAX_PACKET_LENGTH = 0xFFFE;

  ~BluetoothOppManager();
  static BluetoothOppManager* Get();
  void ClientDataHandler(mozilla::ipc::UnixSocketRawData* aMessage,
                         BluetoothSocket* aSocket);
  void ServerDataHandler(mozilla::ipc::UnixSocketRawData* aMessage,
                         BluetoothSocket* aSocket);

  /*
   * If a application wnats to send a file, first, it needs to
   * call Connect() to create a valid RFCOMM connection. After
   * that, call SendFile()/StopSendingFile() to control file-sharing
   * process. During the file transfering process, the application
   * will receive several system messages which contain the processed
   * percentage of file. At the end, the application will get another
   * system message indicating that te process is complete, then it can
   * either call Disconnect() to close RFCOMM connection or start another
   * file-sending thread via calling SendFile() again.
   */
  bool Connect(const nsAString& aDeviceObjectPath,
               BluetoothReplyRunnable* aRunnable);
  void Disconnect();
  bool Listen();

  bool SendFile(const nsAString& aDeviceAddress, BlobParent* aBlob);
  bool StopSendingFile();
  bool ConfirmReceivingFile(const nsAString& aDeviceAddress, bool aConfirm);

  void SendConnectRequest(BluetoothSocket* aSocket);
  void SendPutHeaderRequest(BluetoothSocket* aSocket, const nsAString& aFileName, int aFileSize);
  void SendPutRequest(BluetoothSocket* aSocket, uint8_t* aFileBody, int aFileBodyLength);
  void SendPutFinalRequest(BluetoothSocket* aSocket);
  void SendDisconnectRequest(BluetoothSocket* aSocket);
  void SendAbortRequest(BluetoothSocket* aSocket);

  void ExtractPacketHeaders(const ObexHeaderSet& aHeader);
  bool ExtractBlobHeaders(BluetoothSocket* aSocket);
  nsresult HandleShutdown();

  // Return true if there is an ongoing file-transfer session, please see
  // Bug 827267 for more information.
  bool IsTransferring();

  // Implement interface BluetoothSocketObserver
  void ReceiveSocketData(nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage,
                         BluetoothSocket* aSocket) MOZ_OVERRIDE;
  void OnConnectSuccess(BluetoothSocket* aSocket) MOZ_OVERRIDE;
  void OnConnectError(BluetoothSocket* aSocket) MOZ_OVERRIDE;
  void OnDisconnect(BluetoothSocket* aSocket) MOZ_OVERRIDE;

private:
  BluetoothOppManager();
  void StartFileTransfer();
  void FileTransferComplete();
  void UpdateProgress();
  void ReceivingFileConfirmation();
  bool CreateFile();
  bool WriteToFile(const uint8_t* aData, int aDataLength);
  void DeleteReceivedFile();
  void ReplyToConnect(BluetoothSocket* aSocket);
  void ReplyToDisconnect(BluetoothSocket* aSocket);
  void ReplyToPut(BluetoothSocket* aSocket, bool aFinal, bool aContinue);
  void AfterOppConnected();
  void AfterFirstPut();
  void AfterOppDisconnected();
  void ValidateFileName();
  bool IsReservedChar(PRUnichar c);

  /**
   * RFCOMM socket status.
   */
  enum BluetoothSocketState mSocketStatus;

  /**
   * OBEX session status.
   * Set when OBEX session is established.
   */
  bool mConnected;
  nsString mConnectedDeviceAddress;

  /**
   * Remote information
   */
  uint8_t mRemoteObexVersion;
  uint8_t mRemoteConnectionFlags;
  int mRemoteMaxPacketLength;

  /**
   * For sending files, we decide our next action based on current command and
   * previous one.
   * For receiving files, we don't need previous command and it is set to 0
   * as a default value.
   */
  int mLastCommand;

  int mPacketLeftLength;
  int mBodySegmentLength;
  int mReceivedDataBufferOffset;
  int mUpdateProgressCounter;

  /**
   * Set when StopSendingFile() is called.
   */
  bool mAbortFlag;

  /**
   * Set when receiving the first PUT packet of a new file
   */
  bool mNewFileFlag;

  /**
   * Set when receiving a PutFinal packet
   */
  bool mPutFinalFlag;

  /**
   * Set when FileTransferComplete() is called
   */
  bool mSendTransferCompleteFlag;

  /**
   * Set when a transfer is successfully completed.
   */
  bool mSuccessFlag;

  /**
   * True: Receive file (Server)
   * False: Send file (Client)
   */
  bool mTransferMode;

  /**
   * Set when receiving the first PUT packet and wait for
   * ConfirmReceivingFile() to be called.
   */
  bool mWaitingForConfirmationFlag;

  nsAutoArrayPtr<uint8_t> mBodySegment;
  nsAutoArrayPtr<uint8_t> mReceivedDataBuffer;

  nsCOMPtr<nsIDOMBlob> mBlob;
  nsCOMPtr<nsIThread> mReadFileThread;
  nsCOMPtr<nsIOutputStream> mOutputStream;
  nsCOMPtr<nsIInputStream> mInputStream;

  nsRefPtr<BluetoothReplyRunnable> mRunnable;
  nsRefPtr<DeviceStorageFile> mDsFile;

  RefPtr<BluetoothSocket> mRfcommServerSocket;
  RefPtr<BluetoothSocket> mL2capServerSocket;

  nsTArray<nsRefPtr<BluetoothSocket> > mConnectedSockets;
};

END_BLUETOOTH_NAMESPACE

#endif
