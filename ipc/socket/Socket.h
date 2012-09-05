/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_Socket_h
#define mozilla_ipc_Socket_h

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mozilla/RefPtr.h"

namespace mozilla {
namespace ipc {

struct SocketRawData
{
  static const size_t MAX_DATA_SIZE = 1024;
  uint8_t mData[MAX_DATA_SIZE];

  // Number of octets in mData.
  size_t mSize;
  size_t mCurrentWriteOffset;

  SocketRawData() :
    mSize(0),
    mCurrentWriteOffset(0)
  {
  }
  
  SocketRawData(const char* str) :
    mCurrentWriteOffset(0)
  {
    memcpy(mData, str, strlen(str));
    mSize = strlen(str);   
  }
};

void
StartSocketManager();

void
StopSocketManager();

class SocketConsumer;

bool
ConnectSocket(SocketConsumer* s, int aType, const char* aAddress, int aChannel, bool aAuth, bool aEncrypt);

bool
CloseSocket(SocketConsumer* s);

class SocketConsumer : public RefCounted<SocketConsumer>
{
public:
  SocketConsumer()
  {}
  virtual ~SocketConsumer()
  {
    CloseSocket(this);
  }
  virtual void ReceiveSocketData(SocketRawData* aMessage) = 0;
  void SendSocketData(SocketRawData* aMessage);
  int mFd;
};

} // namespace ipc
} // namepsace mozilla

#endif // mozilla_ipc_Socket_h
