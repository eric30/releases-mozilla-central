/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013 Deutsche Telekom, Inc. */

#include "NdefRecord.h"
#include "nsIDOMClassInfo.h"
#include "mozilla/HoldDropJSObjects.h"

using namespace mozilla::dom::nfc;

DOMCI_DATA(NdefRecord, NdefRecord)

NS_IMPL_CYCLE_COLLECTING_ADDREF(NdefRecord)
NS_IMPL_CYCLE_COLLECTING_RELEASE(NdefRecord)

NS_IMPL_CYCLE_COLLECTION_CLASS(NdefRecord)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(NdefRecord)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMNdefRecord)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNdefRecord)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(NdefRecord)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(NdefRecord)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JSVAL_MEMBER_CALLBACK(payload)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(NdefRecord)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(NdefRecord)
  tmp->DropData();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

/********************************
 * Constructors/Destructors
 ********************************/

NdefRecord::NdefRecord()
{
  MOZ_COUNT_CTOR(NdefRecord);
}

NdefRecord::~NdefRecord() {
  MOZ_COUNT_DTOR(NdefRecord);
  DropData();
}

/* static */
nsresult NdefRecord::NewNdefRecord(nsISupports** aRecord)
{
  nsCOMPtr<nsISupports> rec = do_QueryObject(new NdefRecord());
  rec.forget(aRecord);
  return NS_OK;
}

NS_IMETHODIMP
NdefRecord::Initialize(nsISupports* aOwner,
                     JSContext* aContext,
                     JSObject* aObject,
                     const JS::CallArgs& aArgv)
{
  JSString* jsstr;
  size_t length;
  unsigned argc = aArgv.length();

  if (argc != 4) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!JSVAL_IS_INT(aArgv[0])) {
    return NS_ERROR_INVALID_ARG;
  }

  // Tnf: (PRUint8)
  tnf = (PRUint8)JSVAL_TO_INT(aArgv[0]);

  // Type (DOMString)
  if (JSVAL_IS_STRING(aArgv[1])) {
    jsstr = JSVAL_TO_STRING(aArgv[1]);
    const jschar *typechars = JS_GetStringCharsAndLength(aContext, jsstr, &length);
    if (!typechars) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    type.Assign(typechars, length);
  } else if (aArgv[1].isNull()) {
    type.AssignLiteral("");
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  // Id (DOMString)
  if (JSVAL_IS_STRING(aArgv[2])) {
    jsstr = JSVAL_TO_STRING(aArgv[2]);
    const jschar *idchars = JS_GetStringCharsAndLength(aContext, jsstr, &length);
    if (!idchars) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    id.Assign(idchars, length);
  } else if (aArgv[2].isNull()) {
    id.AssignLiteral("");
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  // Payload (jsval), store in JS::Heap<JS::Value>
  if (JSVAL_IS_STRING(aArgv[3])) {
    payload = aArgv[3];
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  HoldData();
  return NS_OK;
}




/************************************
 * NS_METHODIMPs (Getter and Setter)
 ************************************/
NS_IMETHODIMP
NdefRecord::GetTnf(PRUint8* aTnf)
{
  *aTnf = tnf;
  return NS_OK;
}

NS_IMETHODIMP
NdefRecord::GetType(nsAString& aType)
{
  aType = type;
  return NS_OK;
}

NS_IMETHODIMP
NdefRecord::GetId(nsAString& aId)
{
  aId = id;
  return NS_OK;
}

NS_IMETHODIMP
NdefRecord::GetPayload(JS::Value* aPayload)
{
  JS::ExposeValueToActiveJS(payload);
  *aPayload = payload;
  return NS_OK;
}

/**
 * Private, GC
 */
void
NdefRecord::HoldData()
{
  mozilla::HoldJSObjects(this);
}

void
NdefRecord::DropData()
{
  if (!JSVAL_IS_NULL(payload)) {
    payload = JSVAL_NULL;
    mozilla::DropJSObjects(this);
  }
}
