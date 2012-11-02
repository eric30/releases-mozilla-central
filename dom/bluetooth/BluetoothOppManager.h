/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothoppmanager_h__
#define mozilla_dom_bluetooth_bluetoothoppmanager_h__

#include "BluetoothCommon.h"
#include "mozilla/dom/ipc/Blob.h"
#include "mozilla/ipc/UnixSocket.h"

class nsIOutputStream;
class nsIInputStream;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothOppManager : public mozilla::ipc::UnixSocketConsumer
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
  void ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
    MOZ_OVERRIDE;

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

  bool SendFile(BlobParent* aBlob);
  bool StopSendingFile();
  bool ConfirmReceivingFile(bool aConfirm);

  void SendConnectRequest();
  void SendPutHeaderRequest(const nsAString& aFileName, int aFileSize);
  void SendPutRequest(uint8_t* aFileBody, int aFileBodyLength,
                      bool aFinal);
  void SendDisconnectRequest();
  void SendAbortRequest();

  nsresult HandleShutdown();
private:
  BluetoothOppManager();
  void StartFileTransfer(const nsString& aDeviceAddress,
                         bool aReceived,
                         const nsString& aFileName,
                         uint32_t aFileLength,
                         const nsString& aContentType);
  void FileTransferComplete(const nsString& aDeviceAddress,
                            bool aSuccess,
                            bool aReceived,
                            const nsString& aFileName,
                            uint32_t aFileLength,
                            const nsString& aContentType);
  void UpdateProgress(const nsString& aDeviceAddress,
                      bool aReceived,
                      uint32_t aProcessedLength,
                      uint32_t aFileLength);
  void ReceivingFileConfirmation(const nsString& aAddress,
                                 const nsString& aFileName,
                                 uint32_t aFileLength,
                                 const nsString& aContentType);
  void DeleteReceivedFile();
  void ReplyToConnect();
  void ReplyToDisconnect();
  void ReplyToPut(bool aFinal, bool aContinue);
  void AfterOppConnected();
  void AfterOppDisconnected();
  virtual void OnConnectSuccess() MOZ_OVERRIDE;
  virtual void OnConnectError() MOZ_OVERRIDE;
  virtual void OnDisconnect() MOZ_OVERRIDE;

  bool mConnected;
  int mConnectionId;
  int mLastCommand;
  uint8_t mRemoteObexVersion;
  uint8_t mRemoteConnectionFlags;
  int mRemoteMaxPacketLength;
  bool mAbortFlag;
  int mPacketLeftLength;
  nsString mConnectedDeviceAddress;
  bool mReceiving;
  bool mPutFinal;
  bool mWaitingForConfirmationFlag;
  int mUpdateProgressCounter;
  enum mozilla::ipc::SocketConnectionStatus mSocketStatus;

  nsCOMPtr<nsIDOMBlob> mBlob;
  nsCOMPtr<nsIThread> mReadFileThread;
  nsCOMPtr<nsIOutputStream> mOutputStream;
  nsCOMPtr<nsIInputStream> mInputStream;
};

END_BLUETOOTH_NAMESPACE

#endif
