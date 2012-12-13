/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#ifndef mozilla_ipc_Nfc_h
#define mozilla_ipc_Nfc_h
 
#include "mozilla/RefPtr.h"
 
namespace base {
  class MessageLoop;
}
 
namespace mozilla {
namespace ipc {
 
struct NfcData
{
  char* json;
};
 
class NfcConsumer : public RefCounted<NfcConsumer>
{
public:
  virtual ~NfcConsumer() { }
  virtual void MessageReceived(NfcData* aMessage) { }
};
 
bool StartNfc(NfcConsumer* aConsumer);
 
bool SendNfcData(NfcData** aMessage);
 
void StopNfc();
 
} // namespace ipc
} // namepsace mozilla
 
#endif // mozilla_ipc_Nfc_h
