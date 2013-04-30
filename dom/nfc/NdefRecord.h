/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _NDEFRECORD_H
#define _NDEFRECORD_H

#include "mozilla/Attributes.h"
#include "nsString.h"
#include "jsapi.h"
#include "nsIDOMNdefRecord.h"
#include "nsIJSNativeInitializer.h"

namespace mozilla {
namespace dom {
namespace nfc {

/**
 *
 */
class NdefRecord MOZ_FINAL : public nsIDOMNdefRecord,
                             public nsIJSNativeInitializer
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMNDEFRECORD

  NdefRecord();
  static nsresult NewNdefRecord(nsISupports* *aNewRecord);
  NS_IMETHOD Initialize(nsISupports* aOwner,
                     JSContext* aContext,
                     JSObject* aObject,
                     PRUint32 aArgc,
                     JS::Value* aArgv);

  virtual ~NdefRecord() {}

  PRUint8 tnf;
  nsString type;
  nsString id;
  jsval payload;
};

} // namespace nfc
} // namespace dom
} // namespace mozilla

#endif /* _NDEFRECORD_H */
