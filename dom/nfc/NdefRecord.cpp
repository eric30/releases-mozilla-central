/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NdefRecord.h"
#include "nsIDOMClassInfo.h"

using namespace mozilla::dom::nfc;

DOMCI_DATA(NdefRecord, NdefRecord)

NS_INTERFACE_MAP_BEGIN(NdefRecord)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMNdefRecord)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNdefRecord)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(NdefRecord)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(NdefRecord)
NS_IMPL_RELEASE(NdefRecord)

/********************************
 * Constructors/Destructors
 ********************************/

NdefRecord::NdefRecord()
{
  tnf = 0;
  type.AssignLiteral("");
  id.AssignLiteral("");
  payload = JSVAL_NULL;
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
                     PRUint32 aArgc,
                     JS::Value* aArgv)
{
  JSString* jsstr;
  size_t length;

  if (aArgc != 4) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!JSVAL_IS_INT(aArgv[0])) {
    return NS_ERROR_INVALID_ARG;
  }

  // Tnf: (PRUint8)
  tnf = (PRUint8)JSVAL_TO_INT(aArgv[0]);

  // Type (DOMString)
  if ((aArgv[1] != JSVAL_NULL) && JSVAL_IS_STRING(aArgv[1])) {
    jsstr = JSVAL_TO_STRING(aArgv[1]);
    const jschar *typechars = JS_GetStringCharsAndLength(aContext, jsstr, &length);
    if (!typechars) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    type.Assign(typechars, length);
  } else if (aArgv[1] == JSVAL_NULL) {
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
  } else if (aArgv[2] == JSVAL_NULL) {
    id.AssignLiteral("");
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  // Payload (jsval), is already a JSVAL
  payload = aArgv[3];

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
  *aPayload = payload;
  return NS_OK;
}
