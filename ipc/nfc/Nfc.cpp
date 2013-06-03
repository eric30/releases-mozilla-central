/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et ft=cpp: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/un.h>

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk", args)
#else
#define LOG(args...)  printf(args);
#endif

#include "jsfriendapi.h"
#include "nsThreadUtils.h" // For NS_IsMainThread.
#include "Nfc.h"

USING_WORKERS_NAMESPACE
using namespace mozilla::ipc;

namespace {

const char* NFC_SOCKET_NAME = "/dev/socket/nfcd";

// Network port to connect to for adb forwarded sockets when doing
// desktop development.
const uint32_t NFCD_TEST_PORT = 6300;

class DispatchNfcEvent : public WorkerTask
{
public:
  DispatchNfcEvent(UnixSocketRawData* aMessage)
      : mMessage(aMessage)
    { }
  virtual bool RunTask(JSContext *aCx);
private:
  nsAutoPtr<UnixSocketRawData> mMessage;
};

bool
DispatchNfcEvent::RunTask(JSContext *aCx)
{
    JSObject *obj = JS_GetGlobalObject(aCx);
    JSObject *array = JS_NewUint8Array(aCx, mMessage->mSize);
    if (!array) {
        return false;
    }
    memcpy(JS_GetArrayBufferViewData(array), mMessage->mData, mMessage->mSize);
    jsval argv[] = { OBJECT_TO_JSVAL(array) };
    return JS_CallFunctionName(aCx, obj, "onNfcMessage", NS_ARRAY_LENGTH(argv),
                               argv, argv);
}

class NfcConnector : public mozilla::ipc::UnixSocketConnector
{
public:
  virtual ~NfcConnector()
  {}

 virtual int Create();
  virtual void CreateAddr(bool aIsServer,
                          socklen_t& aAddrSize,
                          struct sockaddr *aAddr,
                          const char* aAddress);
  virtual bool SetUp(int aFd);
  virtual void GetSocketAddr(const sockaddr& aAddr,
                             nsAString& aAddrStr);
};

int
NfcConnector::Create()
{
    MOZ_ASSERT(!NS_IsMainThread());

    int fd = -1;

#if defined(MOZ_WIDGET_GONK)
    fd = socket(AF_LOCAL, SOCK_STREAM, 0);
#else
    struct hostent *hp;

    hp = gethostbyname("localhost");
    if (hp) {
        fd = socket(hp->h_addrtype, SOCK_STREAM, 0);
    }
#endif

    if (fd < 0) {
        NS_WARNING("Could not open Nfc socket!");
        return -1;
    }

    if (!SetUp(fd)) {
        NS_WARNING("Could not set up socket!");
    }
    return fd;
}

void
NfcConnector::CreateAddr(bool aIsServer,
                         socklen_t& aAddrSize,
                         struct sockaddr *aAddr,
                         const char* aAddress)
{
    // We never open Nfc socket as server.
    MOZ_ASSERT(!aIsServer);

#if defined(MOZ_WIDGET_GONK)
    struct sockaddr_un addr_un;

    memset(&addr_un, 0, sizeof(addr_un));
    strcpy(addr_un.sun_path, aAddress);
    addr_un.sun_family = AF_LOCAL;

    aAddrSize = strlen(aAddress) + offsetof(struct sockaddr_un, sun_path) + 1;
    memcpy(aAddr, &addr_un, aAddrSize);
#else
    struct hostent *hp;
    struct sockaddr_in addr_in;

    hp = gethostbyname("localhost");
    if (!hp) {
    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family = hp->h_addrtype;
    addr_in.sin_port = htons(Nfc_TEST_PORT);
    memcpy(&addr_in.sin_addr, hp->h_addr, hp->h_length);
    aAddrSize = sizeof(addr_in);
    memcpy(aAddr, &addr_in, aAddrSize);
    }
#endif
}

bool
NfcConnector::SetUp(int aFd)
{
    // Nothing to do here.
    return true;
}

void
NfcConnector::GetSocketAddr(const sockaddr& aAddr,
                            nsAString& aAddrStr)
{
    MOZ_NOT_REACHED("This should never be called!");
}

} // anonymous namespace


namespace mozilla {
namespace ipc {

NfcConsumer::NfcConsumer(WorkerCrossThreadDispatcher* aDispatcher)
    : mDispatcher(aDispatcher)
    , mShutdown(false)
{
    ConnectSocket(new NfcConnector(), NFC_SOCKET_NAME);
}

void
NfcConsumer::Shutdown()
{
    mShutdown = true;
    CloseSocket();
}

void
NfcConsumer::ReceiveSocketData(nsAutoPtr<UnixSocketRawData>& aMessage)
{
    MOZ_ASSERT(NS_IsMainThread());
    LOG("ReceiveSocketData\n");
    nsRefPtr<DispatchNfcEvent> dre(new DispatchNfcEvent(aMessage.forget()));
    mDispatcher->PostTask(dre);
}

void
NfcConsumer::OnConnectSuccess()
{
    // Nothing to do here.
    LOG("Socket open for Nfc\n");
}

void
NfcConsumer::OnConnectError()
{
    LOG("%s\n", __FUNCTION__);
    CloseSocket();
}

void
NfcConsumer::OnDisconnect()
{
    LOG("%s\n", __FUNCTION__);
    if (!mShutdown) {
        ConnectSocket(new NfcConnector(), NFC_SOCKET_NAME, 1000);
    }
}

} // namespace ipc
} // namespace mozilla
