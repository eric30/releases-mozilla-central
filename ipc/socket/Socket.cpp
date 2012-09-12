/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Socket.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/sco.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/l2cap.h>

#include "base/eintr_wrapper.h"
#include "base/message_loop.h"

#include "nsDataHashtable.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"
#include "mozilla/Monitor.h"
#include "mozilla/Util.h"
#include "nsXULAppAPI.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk", args)
#else
#define LOG(args...)  printf(args);
#endif
#define TYPE_AS_STR(t)                                                  \
  ((t) == TYPE_RFCOMM ? "RFCOMM" : ((t) == TYPE_SCO ? "SCO" : "L2CAP"))

namespace mozilla {
namespace ipc {
static const int RFCOMM_SO_SNDBUF = 70 * 1024; // 70 KB send buffer

static const int TYPE_RFCOMM = 1;
static const int TYPE_SCO = 2;
static const int TYPE_L2CAP = 3;

static int get_bdaddr(const char *str, bdaddr_t *ba)
{
  char *d = ((char*)ba) + 5, *endp;
  for (int i = 0; i < 6; i++) {
    *d-- = strtol(str, &endp, 16);
    MOZ_ASSERT(!(*endp != ':' && i != 5));
    str = endp + 1;
  }
  return 0;
}

struct SocketWatcher
{
  typedef nsTArray<SocketRawData*> SocketRawDataQueue;

  SocketWatcher(SocketConsumer* aConsumer) : mConsumer(aConsumer)
  {
  }
  SocketRawDataQueue mOutgoingQ;
  nsRefPtr<SocketConsumer> mConsumer;
  MessageLoopForIO::FileDescriptorWatcher mReadWatcher;
  MessageLoopForIO::FileDescriptorWatcher mWriteWatcher;
};

struct SocketManager : public RefCounted<SocketManager>,
                       public MessageLoopForIO::Watcher
{
  SocketManager() : mIOLoop(MessageLoopForIO::current())
                  , mMutex("SocketManager.mMutex")
  {
    mWatchers.Init();
  }

  virtual ~SocketManager()
  {
  }

  virtual void OnFileCanReadWithoutBlocking(int aFd);
  virtual void OnFileCanWriteWithoutBlocking(int aFd);

  bool AddSocket(SocketConsumer* aConsumer, int aFd);
  bool RemoveSocket(SocketConsumer* aConsumer);

  nsTArray<uint32_t> mListeningSockets;
  nsAutoPtr<SocketRawData> mIncoming;
  MessageLoopForIO* mIOLoop;

  nsDataHashtable<nsUint32HashKey, SocketWatcher*> mWatchers;

  Mutex mMutex;
};

static RefPtr<SocketManager> sManager;

class SocketReceiveTask : public nsRunnable
{
public:
  SocketReceiveTask(SocketConsumer* aConsumer, SocketRawData* aData) :
    mConsumer(aConsumer),
    mRawData(aData)
  {
    MOZ_ASSERT(aConsumer);
    MOZ_ASSERT(aData);
  }

  NS_IMETHOD
  Run()
  {
    mConsumer->ReceiveSocketData(mRawData);
    return NS_OK;
  }
private:
  nsRefPtr<SocketConsumer> mConsumer;
  nsAutoPtr<SocketRawData> mRawData;
};

class SocketSendTask : public Task
{
public:
  SocketSendTask(SocketRawData* aData, int aFd)
    : mData(aData),
      mFd(aFd)
  {
    MOZ_ASSERT(aData);
    MOZ_ASSERT(aFd > 0);
  }

  void
  Run()
  {
    SocketWatcher* s = sManager->mWatchers.Get(mFd);
    if (!s) {
      NS_WARNING("No watcher for file descriptor!");
      return;
    }
    s->mOutgoingQ.AppendElement(mData);
    sManager->OnFileCanWriteWithoutBlocking(mFd);
  }

private:
  SocketRawData* mData;
  int mFd;
};

void
SocketConsumer::SendSocketData(SocketRawData* aData)
{
  XRE_GetIOMessageLoop()->PostTask(FROM_HERE,
                                   new SocketSendTask(aData, mFd));
}

int
OpenSocket(int aType, const char* aAddress, int aChannel, bool aAuth, bool aEncrypt, bool aServer)
{
  //MOZ_ASSERT(!NS_IsMainThread());
  int lm = 0;
  int fd = -1;
  int sndbuf;

  switch (aType) {
  case TYPE_RFCOMM:
    fd = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    break;
  case TYPE_SCO:
    fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO);
    break;
  case TYPE_L2CAP:
    fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    break;
  default:
    return -1;
  }

  if (fd < 0) {
    NS_WARNING("Could not open bluetooth socket!");
    return -1;
  }

  /* kernel does not yet support LM for SCO */
  switch (aType) {
  case TYPE_RFCOMM:
    lm |= aAuth ? RFCOMM_LM_AUTH : 0;
    lm |= aEncrypt ? RFCOMM_LM_ENCRYPT : 0;
    lm |= (aAuth && aEncrypt) ? RFCOMM_LM_SECURE : 0;
    break;
  case TYPE_L2CAP:
    lm |= aAuth ? L2CAP_LM_AUTH : 0;
    lm |= aEncrypt ? L2CAP_LM_ENCRYPT : 0;
    lm |= (aAuth && aEncrypt) ? L2CAP_LM_SECURE : 0;
    break;
  }

  if (lm) {
    if (setsockopt(fd, SOL_RFCOMM, RFCOMM_LM, &lm, sizeof(lm))) {
      LOG("setsockopt(RFCOMM_LM) failed, throwing");
      return -1;
    }
  }

  if (aType == TYPE_RFCOMM) {
    sndbuf = RFCOMM_SO_SNDBUF;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf))) {
      LOG("setsockopt(SO_SNDBUF) failed, throwing");
      return -1;
    }
  }

  int n = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));

  socklen_t addr_sz;
  struct sockaddr *addr;
  // Create default as any address for server
  bdaddr_t bd_address_obj = {{0, 0, 0, 0, 0, 0}};

  // If we have a specific client socket, get address for that instead
  if(!aServer) {
    if (get_bdaddr(aAddress, &bd_address_obj)) {
      NS_WARNING("Can't get bluetooth address!");
      return -1;
    }
  }
  

  switch (aType) {
  case TYPE_RFCOMM:
    struct sockaddr_rc addr_rc;
    addr = (struct sockaddr *)&addr_rc;
    addr_sz = sizeof(addr_rc);

    memset(addr, 0, addr_sz);
    addr_rc.rc_family = AF_BLUETOOTH;
    addr_rc.rc_channel = aChannel;
    memcpy(&addr_rc.rc_bdaddr, &bd_address_obj, sizeof(bdaddr_t));
    break;
  case TYPE_SCO:
    struct sockaddr_sco addr_sco;
    addr = (struct sockaddr *)&addr_sco;
    addr_sz = sizeof(addr_sco);

    memset(addr, 0, addr_sz);
    addr_sco.sco_family = AF_BLUETOOTH;
    memcpy(&addr_sco.sco_bdaddr, &bd_address_obj, sizeof(bdaddr_t));
    break;
  default:
    NS_WARNING("Socket type unknown!");
    return -1;
  }

  if (!aServer) {
    if(connect(fd, addr, addr_sz)) {
#if DEBUG
      LOG("Socket connect errno=%d\n", errno);
#endif
      NS_WARNING("Socket connect error!");
      return -1;
    }
  } else {    
    if (bind(fd, addr, addr_sz)) {
      LOG("...bind(%d) gave errno %d", fd, errno);
      return -1;
    }

    if (listen(fd, 1)) {
      LOG("...listen(%d) gave errno %d", fd, errno);
      return -1;
    }
  }
  
  return fd;
}

int
CloseSocketInternal(int aFd)
{
  // This won't block since we are making sockets O_NONBLOCK
  return close(aFd);
}

bool
SocketManager::AddSocket(SocketConsumer* aConsumer, int aFd)
{
  // Set close-on-exec bit.
  int flags = fcntl(aFd, F_GETFD);
  if (-1 == flags) {
    return false;
  }

  flags |= FD_CLOEXEC;
  if (-1 == fcntl(aFd, F_SETFD, flags)) {
    return false;
  }

  // Select non-blocking IO.
  if (-1 == fcntl(aFd, F_SETFL, O_NONBLOCK)) {
    return false;
  }

  aConsumer->mFd = aFd;
  SocketWatcher* w = new SocketWatcher(aConsumer);
  mWatchers.Put(aFd, w);
  if (!mIOLoop->WatchFileDescriptor(aFd,
                                    true,
                                    MessageLoopForIO::WATCH_READ,
                                    &(mWatchers.Get(aFd)->mReadWatcher),
                                    this)) {
    return false;
  }
  return true;
}

bool
SocketManager::RemoveSocket(SocketConsumer* aConsumer)
{
  mWatchers.Remove(aConsumer->mFd);
  CloseSocketInternal(aConsumer->mFd);
  return true;
}

void
SocketManager::OnFileCanReadWithoutBlocking(int aFd)
{
  // Keep reading data until either
  //
  //   - mIncoming is completely read
  //     If so, sConsumer->MessageReceived(mIncoming.forget())
  //
  //   - mIncoming isn't completely read, but there's no more
  //     data available on the socket
  //     If so, break;

  if(mListeningSockets.Contains(aFd)) {
    NS_WARNING("LIstening socket was connected to!");
  }
  
  while (true) {
    if (!mIncoming) {
      mIncoming = new SocketRawData();
      ssize_t ret = read(aFd, mIncoming->mData, SocketRawData::MAX_DATA_SIZE);
      if (ret <= 0) {
        if (ret == -1) {
          if (errno == EINTR) {
            continue; // retry system call when interrupted
          }
          else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // no data available: return and re-poll
          }
          // else fall through to error handling on other errno's
        }
        LOG("Cannot read from network, error %d\n", (int)ret);
        // At this point, assume that we can't actually access
        // the socket anymore, and start a reconnect loop.
        mIncoming.forget();
        // mReadWatchers.Get(aFd)->StopWatchingFileDescriptor();
        // mWriteWatchers.Get(aFd)->StopWatchingFileDescriptor();
        close(aFd);
        return;
      }
      mIncoming->mData[ret] = 0;
      mIncoming->mSize = ret;
      nsRefPtr<SocketReceiveTask> t =
        new SocketReceiveTask(mWatchers.Get(aFd)->mConsumer, mIncoming.forget());
      NS_DispatchToMainThread(t);
      if (ret < ssize_t(SocketRawData::MAX_DATA_SIZE)) {
        return;
      }
    }
  }
}

void
SocketManager::OnFileCanWriteWithoutBlocking(int aFd)
{
  // Try to write the bytes of mCurrentRilRawData.  If all were written, continue.
  //
  // Otherwise, save the byte position of the next byte to write
  // within mCurrentRilRawData, and request another write when the
  // system won't block.
  //
  while (true) {
    SocketRawData* data;
    SocketWatcher* w;
    {
      MutexAutoLock lock(mMutex);
      w = mWatchers.Get(aFd);
      if (w->mOutgoingQ.IsEmpty()) {
        return;
      }
      data = w->mOutgoingQ.ElementAt(0);
    }
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
        &w->mWriteWatcher,
        this);
      return;
    }
    {
      MutexAutoLock lock(mMutex);
      w->mOutgoingQ.RemoveElementAt(0);
    }
    delete data;
  }
}

static void
StartManager(Monitor* aMonitor)
{
  MOZ_ASSERT(!sManager);
  sManager = new SocketManager();
  {
    MonitorAutoLock lock(*aMonitor);
    lock.Notify();
  }
}

void
StartSocketManager()
{
  if (sManager) {
    return;
  }
  Monitor monitor("StartSocketManager.monitor");
  {
    MonitorAutoLock lock(monitor);

    XRE_GetIOMessageLoop()->PostTask(
      FROM_HERE,
      NewRunnableFunction(StartManager, &monitor));

    lock.Wait();
  }
};

PLDHashOperator
IterateCloseSocket(const uint32_t &aKey, SocketWatcher* &aData, void *userArg)
{
  CloseSocket(aData->mConsumer);
  return PL_DHASH_REMOVE;
}

void
StopSocketManager()
{
  if (!sManager) {
    return;
  }
  sManager->mWatchers.Enumerate(IterateCloseSocket, nullptr);
  sManager = nullptr;
};

bool
ConnectSocket(SocketConsumer* aConsumer, int aType, const char* aAddress, int aChannel, bool aAuth, bool aEncrypt)
{
  if (!sManager) {
    NS_WARNING("Manager not yet started!");
    return false;
  }
  int fd = OpenSocket(aType, aAddress, aChannel, aAuth, aEncrypt, false);
  if (fd <= 0) {
    return false;
  }
  return sManager->AddSocket(aConsumer, fd);
}

bool
ListenSocket(SocketConsumer* aConsumer, int aType, int aChannel, bool aAuth, bool aEncrypt)
{
  if (!sManager) {
    NS_WARNING("Manager not yet started!");
    return false;
  }
  int fd = OpenSocket(aType, nullptr, aChannel, aAuth, aEncrypt, true);
  if (fd <= 0) {
    NS_WARNING("LIstening socket fucked up!");
    return false;
  }
  sManager->mListeningSockets.AppendElement(fd);
  NS_WARNING("LIstening socket!");
  return sManager->AddSocket(aConsumer, fd);
}

bool
CloseSocket(SocketConsumer* aConsumer)
{
  if (!sManager) {
    return false;
  }
  return sManager->RemoveSocket(aConsumer);
}
} // namespace ipc
} // namespace mozilla
