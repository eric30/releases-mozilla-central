/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothSocket.h"

#include "BluetoothServiceBluedroid.h"
#include "BluetoothSocketObserver.h"
#include "mozilla/RefPtr.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#include <hardware/bluetooth.h>
#include "hardware/bt_sock.h"
#include <hardware/hardware.h>
#include "base/message_loop.h"
#include "mozilla/FileUtils.h"

using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

static const size_t MAX_READ_SIZE = 1 << 16;
const btsock_interface_t* sBluetoothSocketInterface = nullptr;

// helper functions
bool EnsureBluetoothSocketHalLoad()
{
  if (sBluetoothSocketInterface) {
    return true;
  }

  const bt_interface_t* btInf = GetBluetoothInterface();
  NS_ENSURE_TRUE(btInf, false);

  sBluetoothSocketInterface =
    (btsock_interface_t *) btInf->get_profile_interface(BT_PROFILE_SOCKETS_ID);
  NS_ENSURE_TRUE(sBluetoothSocketInterface, false);

  return true;
}

class mozilla::dom::bluetooth::DroidSocketImpl : public MessageLoopForIO::Watcher
{
public:
  DroidSocketImpl(BluetoothSocket* aConsumer, int aFd)
    : mConsumer(aConsumer)
    , mIOLoop(nullptr)
    , mFd(aFd)
    , mShuttingDownOnIOThread(false)
    , mDelayedConnectTask(nullptr)
  {
  }

  ~DroidSocketImpl()
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  void QueueWriteData(UnixSocketRawData* aData)
  {
    mOutgoingQ.AppendElement(aData);
    OnFileCanWriteWithoutBlocking(mFd);
  }

  bool isFdValid()
  {
    return (mFd > 0);
  }

  bool IsShutdownOnMainThread()
  {
    MOZ_ASSERT(NS_IsMainThread());
    return mConsumer == nullptr;
  }

  bool IsShutdownOnIOThread()
  {
    return mShuttingDownOnIOThread;
  }

  void ShutdownOnMainThread()
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!IsShutdownOnMainThread());
    mConsumer = nullptr;
  }

  void ShutdownOnIOThread()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(!mShuttingDownOnIOThread);

    mReadWatcher.StopWatchingFileDescriptor();
    mWriteWatcher.StopWatchingFileDescriptor();

    mShuttingDownOnIOThread = true;
  }

  void SetUpIO()
  {
    MOZ_ASSERT(!mIOLoop);
    MOZ_ASSERT(mFd >= 0);
    mIOLoop = MessageLoopForIO::current();
    mIOLoop->WatchFileDescriptor(mFd,
                                 true,
                                 MessageLoopForIO::WATCH_READ,
                                 &mReadWatcher,
                                 this);
  }

  void SetDelayedConnectTask(CancelableTask* aTask)
  {
    MOZ_ASSERT(NS_IsMainThread());
    mDelayedConnectTask = aTask;
  }

  void ClearDelayedConnectTask()
  {
    MOZ_ASSERT(NS_IsMainThread());
    mDelayedConnectTask = nullptr;
  }

  void CancelDelayedConnectTask()
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (!mDelayedConnectTask) {
      return;
    }
    mDelayedConnectTask->Cancel();
    ClearDelayedConnectTask();
  }

  /**
   * Accept an incoming connection
   */
  void Accept();

  /**
   * Run bind/listen for bluedoid
   */
  void Connect();

  /**
   * Consumer pointer. Non-thread safe RefPtr, so should only be manipulated
   * directly from main thread. All non-main-thread accesses should happen with
   * mImpl as container.
   */
  RefPtr<BluetoothSocket> mConsumer;

private:
  /**
   * libevent triggered functions that reads data from socket when available and
   * guarenteed non-blocking. Only to be called on IO thread.
   *
   * @param aFd File descriptor to read from
   */
  virtual void OnFileCanReadWithoutBlocking(int aFd);

  /**
   * libevent or developer triggered functions that writes data to socket when
   * available and guarenteed non-blocking. Only to be called on IO thread.
   *
   * @param aFd File descriptor to read from
   */
  virtual void OnFileCanWriteWithoutBlocking(int aFd);

  /**
   * IO Loop pointer. Must be initalized and called from IO thread only.
   */
  MessageLoopForIO* mIOLoop;

  /**
   * Raw data queue. Must be pushed/popped from IO thread only.
   */
  typedef nsTArray<UnixSocketRawData* > UnixSocketRawDataQueue;
  UnixSocketRawDataQueue mOutgoingQ;

  /**
   * Read watcher for libevent. Only to be accessed on IO Thread.
   */
  MessageLoopForIO::FileDescriptorWatcher mReadWatcher;

  /**
   * Write watcher for libevent. Only to be accessed on IO Thread.
   */
  MessageLoopForIO::FileDescriptorWatcher mWriteWatcher;

  /**
   * File descriptor to read from/write to. Connection happens on user provided
   * thread. Read/write/close happens on IO thread.
   */
  mozilla::ScopedClose mFd;

  /**
   * If true, do not requeue whatever task we're running
   */
  bool mShuttingDownOnIOThread;

  /**
   * Task member for delayed connect task. Should only be access on main thread.
   */
  CancelableTask* mDelayedConnectTask;
};

template<class T>
class DeleteInstanceRunnable : public nsRunnable
{
public:
  DeleteInstanceRunnable(T* aInstance)
  : mInstance(aInstance)
  { }

  NS_IMETHOD Run()
  {
    delete mInstance;

    return NS_OK;
  }

private:
  T* mInstance;
};

class OnSocketEventTask : public nsRunnable
{
public:
  enum SocketEvent {
    CONNECT_SUCCESS,
    CONNECT_ERROR,
    DISCONNECT
  };

  OnSocketEventTask(DroidSocketImpl* aImpl, SocketEvent e) :
    mImpl(aImpl),
    mEvent(e)
  {
    MOZ_ASSERT(aImpl);
    MOZ_ASSERT(!NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (mImpl->IsShutdownOnMainThread()) {
      NS_WARNING("CloseSocket has already been called!");
      // Since we've already explicitly closed and the close happened before
      // this, this isn't really an error. Since we've warned, return OK.
      return NS_OK;
    }
    if (mEvent == CONNECT_SUCCESS) {
      mImpl->mConsumer->NotifySuccess();
    } else if (mEvent == CONNECT_ERROR) {
      mImpl->mConsumer->NotifyError();
    } else if (mEvent == DISCONNECT) {
      mImpl->mConsumer->NotifyDisconnect();
    }
    return NS_OK;
  }
private:
  DroidSocketImpl* mImpl;
  SocketEvent mEvent;
};

class SocketReceiveTask : public nsRunnable
{
public:
  SocketReceiveTask(DroidSocketImpl* aImpl, UnixSocketRawData* aData) :
    mImpl(aImpl),
    mRawData(aData)
  {
    MOZ_ASSERT(aImpl);
    MOZ_ASSERT(aData);
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (mImpl->IsShutdownOnMainThread()) {
      NS_WARNING("mConsumer is null, aborting receive!");
      // Since we've already explicitly closed and the close happened before
      // this, this isn't really an error. Since we've warned, return OK.
      return NS_OK;
    }

    MOZ_ASSERT(mImpl->mConsumer);
    mImpl->mConsumer->ReceiveSocketData(mRawData);
    return NS_OK;
  }
private:
  DroidSocketImpl* mImpl;
  nsAutoPtr<UnixSocketRawData> mRawData;
};

class SocketSendTask : public Task
{
public:
  SocketSendTask(BluetoothSocket* aConsumer, DroidSocketImpl* aImpl,
                 UnixSocketRawData* aData)
    : mConsumer(aConsumer),
      mImpl(aImpl),
      mData(aData)
  {
    MOZ_ASSERT(aConsumer);
    MOZ_ASSERT(aImpl);
    MOZ_ASSERT(aData);
  }

  void
  Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(!mImpl->IsShutdownOnIOThread());

    mImpl->QueueWriteData(mData);
  }

private:
  nsRefPtr<BluetoothSocket> mConsumer;
  DroidSocketImpl* mImpl;
  UnixSocketRawData* mData;
};

class RequestClosingSocketTask : public nsRunnable
{
public:
  RequestClosingSocketTask(DroidSocketImpl* aImpl) : mImpl(aImpl)
  {
    MOZ_ASSERT(aImpl);
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (mImpl->IsShutdownOnMainThread()) {
      NS_WARNING("CloseSocket has already been called!");
      // Since we've already explicitly closed and the close happened before
      // this, this isn't really an error. Since we've warned, return OK.
      return NS_OK;
    }

    // Start from here, same handling flow as calling CloseSocket() from
    // upper layer
    mImpl->mConsumer->CloseDroidSocket();
    return NS_OK;
  }
private:
  DroidSocketImpl* mImpl;
};

class SocketConnectTask : public Task
{
  virtual void Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    mImpl->Connect();
  }

  DroidSocketImpl* mImpl;
public:
  SocketConnectTask(DroidSocketImpl* aImpl) : mImpl(aImpl) { }
};

class SocketAcceptTask : public CancelableTask {
  virtual void Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    if (mImpl) {
      mImpl->Accept();
    }
  }

  DroidSocketImpl* mImpl;
public:
  SocketAcceptTask(DroidSocketImpl* aImpl) : mImpl(aImpl) { }

  virtual void Cancel()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    mImpl = nullptr;
  }
};

class ShutdownSocketTask : public Task {
  virtual void Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    // At this point, there should be no new events on the IO thread after this
    // one with the possible exception of a SocketAcceptTask that
    // ShutdownOnIOThread will cancel for us. We are now fully shut down, so we
    // can send a message to the main thread that will delete mImpl safely knowing
    // that no more tasks reference it.
    mImpl->ShutdownOnIOThread();

    nsRefPtr<nsIRunnable> t(new DeleteInstanceRunnable<
                                  mozilla::dom::bluetooth::DroidSocketImpl>(mImpl));
    nsresult rv = NS_DispatchToMainThread(t);
    NS_ENSURE_SUCCESS_VOID(rv);
  }

  DroidSocketImpl* mImpl;

public:
  ShutdownSocketTask(DroidSocketImpl* aImpl) : mImpl(aImpl) { }
};

void
DroidSocketImpl::Connect()
{
  MOZ_ASSERT(!NS_IsMainThread());

  // Set up a write watch to make sure we receive the connect signal
  MessageLoopForIO::current()->WatchFileDescriptor(
                                  mFd.get(),
                                  false,
                                  MessageLoopForIO::WATCH_WRITE,
                                  &mWriteWatcher,
                                  this);

  /*nsRefPtr<OnSocketEventTask> t =
    new OnSocketEventTask(this, OnSocketEventTask::CONNECT_SUCCESS);
  NS_DispatchToMainThread(t);
*/

  SetUpIO();
}

void
DroidSocketImpl::Accept()
{
  MOZ_ASSERT(!NS_IsMainThread());
  BT_LOGR("%s: DroidSocket E", __FUNCTION__);

  SetUpIO();
}

void
DroidSocketImpl::OnFileCanReadWithoutBlocking(int aFd)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!mShuttingDownOnIOThread);

  // Read all of the incoming data.
  while (true) {
    nsAutoPtr<UnixSocketRawData> incoming(new UnixSocketRawData(MAX_READ_SIZE));

    ssize_t ret = read(aFd, incoming->mData, incoming->mSize);
    if (ret <= 0) {
      if (ret == -1) {
        if (errno == EINTR) {
          continue; // retry system call when interrupted
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return; // no data available: return and re-poll
        }

        BT_WARNING("Cannot read from network");
        // else fall through to error handling on other errno's
      }

      // We're done with our descriptors. Ensure that spurious events don't
      // cause us to end up back here.
      mReadWatcher.StopWatchingFileDescriptor();
      mWriteWatcher.StopWatchingFileDescriptor();
      nsRefPtr<RequestClosingSocketTask> t = new RequestClosingSocketTask(this);
      NS_DispatchToMainThread(t);
      return;
    }

    incoming->mSize = ret;
    nsRefPtr<SocketReceiveTask> t =
      new SocketReceiveTask(this, incoming.forget());
    NS_DispatchToMainThread(t);

    // If ret is less than MAX_READ_SIZE, there's no
    // more data in the socket for us to read now.
    if (ret < ssize_t(MAX_READ_SIZE)) {
      return;
    }
  }

  MOZ_CRASH("We returned early");
}

void
DroidSocketImpl::OnFileCanWriteWithoutBlocking(int aFd)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!mShuttingDownOnIOThread);
  MOZ_ASSERT(aFd >= 0);

  // Try to write the bytes of mCurrentRilRawData.  If all were written, continue.
  //
  // Otherwise, save the byte position of the next byte to write
  // within mCurrentWriteOffset, and request another write when the
  // system won't block.
  //
  while (true) {
    UnixSocketRawData* data;
    if (mOutgoingQ.IsEmpty()) {
      return;
    }
    data = mOutgoingQ.ElementAt(0);
    const uint8_t *toWrite;
    toWrite = data->mData;

    while (data->mCurrentWriteOffset < data->mSize) {
      ssize_t write_amount = data->mSize - data->mCurrentWriteOffset;
      ssize_t written;
      written = write (aFd, toWrite + data->mCurrentWriteOffset,
                       write_amount);
      if (written > 0) {
        data->mCurrentWriteOffset += written;
      }
      if (written != write_amount) {
        break;
      }
    }

    if (data->mCurrentWriteOffset != data->mSize) {
      MessageLoopForIO::current()->WatchFileDescriptor(
        aFd,
        false,
        MessageLoopForIO::WATCH_WRITE,
        &mWriteWatcher,
        this);
      return;
    }
    mOutgoingQ.RemoveElementAt(0);
    delete data;
  }
}

BluetoothSocket::BluetoothSocket(BluetoothSocketObserver* aObserver,
                                 BluetoothSocketType aType,
                                 bool aAuth,
                                 bool aEncrypt)
  : mObserver(aObserver)
  , mType(aType)
  , mAuth(aAuth)
  , mEncrypt(aEncrypt)
  , mImpl(nullptr)
{
  MOZ_ASSERT(aObserver);

  EnsureBluetoothSocketHalLoad();
}

void
BluetoothSocket::CloseDroidSocket()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mImpl) {
    return;
  }
  BT_LOGR("%s", __FUNCTION__);

  mImpl->CancelDelayedConnectTask();

  // From this point on, we consider mImpl as being deleted.
  // We sever the relationship here so any future calls to listen or connect
  // will create a new implementation.
  mImpl->ShutdownOnMainThread();

  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new ShutdownSocketTask(mImpl));

  mImpl = nullptr;

  NotifyDisconnect();
}

bool
BluetoothSocket::CreateDroidSocket(bool aListen, int aFd)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mImpl) {
    BT_LOGR("Socket already connecting/connected!");
    return false;
  }

  mImpl = new DroidSocketImpl(this, aFd);

  mReceivedStatusLength = 0;
  if (aListen) {
    XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                     new SocketAcceptTask(mImpl));
  } else {
    XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                     new SocketConnectTask(mImpl));
  }

  return true;
}

bool
BluetoothSocket::Connect(const nsAString& aDeviceAddress, int aChannel)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aDeviceAddress.IsEmpty());

  NS_ENSURE_TRUE(sBluetoothSocketInterface, false);

  // We don't support L2CAP here
  bt_bdaddr_t remote_addr;
  StringToBdAddressType(aDeviceAddress, &remote_addr);

  int fd;
  uint8_t UUID_OBEX_OBJECT_PUSH[] = {0x00, 0x00, 0x11, 0x05, 0x00, 0x00, 0x10, 0x00,
                                     0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
  // TODO: uuid as argument
  NS_ENSURE_TRUE(BT_STATUS_SUCCESS ==
    sBluetoothSocketInterface->connect((bt_bdaddr_t *) &remote_addr,
                                       (btsock_type_t) BTSOCK_RFCOMM,
                                       UUID_OBEX_OBJECT_PUSH,
                                       aChannel, &fd, 0),
    false);
  NS_ENSURE_TRUE(fd >= 0, false);

  BT_LOGR("%s: BluetoothSocket socket fd: %d", __FUNCTION__, fd);
  return CreateDroidSocket(false, fd);
}

bool
BluetoothSocket::Listen(int aChannel)
{
  MOZ_ASSERT(NS_IsMainThread());
  BT_LOGR("%s: %d", __FUNCTION__, aChannel);

  NS_ENSURE_TRUE(sBluetoothSocketInterface, false);

  // FIXME: remove hardcode here
  int fd;
  nsAutoCString service_name("OBEX Object Push");
  uint8_t UUID_OBEX_OBJECT_PUSH[] = {0x00, 0x00, 0x11, 0x05, 0x00, 0x00, 0x10, 0x00,
                                     0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
  NS_ENSURE_TRUE(BT_STATUS_SUCCESS ==
    sBluetoothSocketInterface->listen((btsock_type_t) BTSOCK_RFCOMM,
                                      service_name.get(),
                                      UUID_OBEX_OBJECT_PUSH,
                                      aChannel, &fd, 0),
    false);
  NS_ENSURE_TRUE(fd >= 0, false);

  BT_LOGR("BluetoothSocket::Listen open socket file fd: %d", fd);
  return CreateDroidSocket(true, fd);
}

bool
BluetoothSocket::SendDroidSocketData(UnixSocketRawData* aData)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mImpl, false);

  MOZ_ASSERT(!mImpl->IsShutdownOnMainThread());
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new SocketSendTask(this, mImpl, aData));
  return true;
}

int16_t
BluetoothSocket::ReadInt16(const uint8_t* aData, size_t* aOffset)
{
  int16_t value = (aData[*aOffset + 1] << 8) | aData[*aOffset];

  *aOffset += 2;
  return value;
}

int32_t
BluetoothSocket::ReadInt32(const uint8_t* aData, size_t* aOffset)
{
  int32_t value = (aData[*aOffset + 3] << 24) |
                   (aData[*aOffset + 2] << 16) |
                   (aData[*aOffset + 1] << 8) |
                   aData[*aOffset];
  *aOffset += 4;
  return value;
}

void
BluetoothSocket::ReadBdAddress(const uint8_t* aData, size_t* aOffset)
{
  char bdstr[18];
  sprintf(bdstr, "%02x:%02x:%02x:%02x:%02x:%02x",
          aData[0], aData[1], aData[2],
          aData[3], aData[4], aData[5]);

  mDeviceAddress.AssignLiteral(bdstr);
  *aOffset += 6;
}

bool
BluetoothSocket::IsSocketStatus(nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage)
{
  /**
   * 2 messages (20 bytes in total) to receive socket status at the beginning
   * - message 1: [channel:4]
   * - message 2: [size:2][bd address:6][channel:4][connection status:4]
   */
  if (mReceivedStatusLength >= 20) {
    return false;
  }

  size_t offset = 0;
  if (mReceivedStatusLength == 0 && aMessage->mSize == 4) {
    // message 1: [channel:4]
    mChannel = ReadInt32(aMessage->mData, &offset);
  } else if (aMessage->mSize >= 16) {
    // message 2: [size:2][bd address:6][channel:4][connection status:4]
    int16_t size = ReadInt16(aMessage->mData, &offset);
    ReadBdAddress(aMessage->mData, &offset);
    mChannel = ReadInt32(aMessage->mData, &offset);
    mStatus = ReadInt32(aMessage->mData, &offset);

    BT_LOGR("%s: size %d channel %d remote bd address %s status %d",
      __FUNCTION__, size, mChannel,
      NS_ConvertUTF16toUTF8(mDeviceAddress).get(), mStatus);

    if (mStatus != 0) {
      OnConnectError();
    } else {
      OnConnectSuccess();
    }
  }

  mReceivedStatusLength += aMessage->mSize;
  return true;
}

void
BluetoothSocket::ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage)
{
  BT_LOGR("%s", __FUNCTION__);
  for (uint8_t i = 0; i < aMessage->mSize; i++)
    BT_LOGR("  %x", aMessage->mData[i]);

  if (IsSocketStatus(aMessage)) {
    return;
  }

  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mObserver);
  mObserver->ReceiveSocketData(this, aMessage);
}

void
BluetoothSocket::OnConnectSuccess()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mObserver);
  mObserver->OnSocketConnectSuccess(this);
}

void
BluetoothSocket::OnConnectError()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mObserver);
  mObserver->OnSocketConnectError(this);
}

void
BluetoothSocket::OnDisconnect()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mObserver);
  mObserver->OnSocketDisconnect(this);
}
