/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013 Deutsche Telekom, Inc. */

#include "nsNfc.h"
#include "Nfc.h"

#include "nsIDocument.h"
#include "nsIURI.h"
#include "nsPIDOMWindow.h"
#include "nsJSON.h"

#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsIDOMClassInfo.h"
#include "nsDOMEvent.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "SystemWorkerManager.h"

#include "NfcEvent.h"

using namespace mozilla::dom;

nsNfc::nsNfc()
{
}

nsNfc::~nsNfc()
{
  if (mNfc && mNfcCallback) {
    mNfc->UnregisterNfcCallback(mNfcCallback);
  }
}

// static
already_AddRefed<nsNfc>
nsNfc::Create(nsPIDOMWindow* aOwner, nsINfcContentHelper* aNfc)
{
  NS_ASSERTION(aOwner, "Null owner!");
  NS_ASSERTION(aNfc, "Null NFC!");

  nsRefPtr<nsNfc> nfc = new nsNfc();

  nfc->BindToOwner(aOwner);

  nfc->mNfc = aNfc;
  nfc->mNfcCallback = new NfcCallback(nfc);

  nsresult rv = aNfc->RegisterNfcCallback(nfc->mNfcCallback);
  NS_ENSURE_SUCCESS(rv, nullptr);

  return nfc.forget();
}

DOMCI_DATA(MozNfc, nsNfc)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsNfc)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsNfc,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(nsNfc,
                                               nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsNfc,
                                                nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsNfc)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMozNfc)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(MozNfc)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsNfc, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsNfc, nsDOMEventTargetHelper)

NS_IMPL_ISUPPORTS1(nsNfc::NfcCallback, nsINfcCallback)

NS_IMETHODIMP
nsNfc::ValidateNdefTag(const JS::Value& aRecords, JSContext* aCx, bool* result)
{
  NS_WARNING("error: ValidateNdefTag.");
  JSObject &obj = aRecords.toObject();
  // Check if array
  if (!JS_IsArrayObject(aCx, &obj)) {
    NS_WARNING("error: MozNdefRecord object array is required.");
    *result = false;
    return NS_OK;
  }

  // Check length
  uint32_t length;
  if (!JS_GetArrayLength(aCx, &obj, &length) || (length < 1)) {
    *result = false;
    return NS_OK;
  }

  if (!length) {
    *result = false;
    return NS_OK;
  }

  // Check object type (by name), (TODO: by signature)
  const char *ndefRecordName = "MozNdefRecord";
  for (uint32_t index = 0; index < length; index++) {
    JS::Rooted<JS::Value> val(aCx);
    uint32_t namelen;
    if (JS_GetElement(aCx, &obj, index, &val)) {
      const char *name = JS_GetClass(JSVAL_TO_OBJECT(val))->name;
      namelen = strlen(name);
      if (strncmp(ndefRecordName, name, namelen)) {
        NS_WARNING("error: WriteNdefTag requires NdefRecord array item(s).");
        *result = false;
        return NS_OK;
      }
    }
  }
  *result = true;
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefDetails(JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NdefDetails(GetOwner(), aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefRead(JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NdefRead(GetOwner(), aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefWrite(const JS::Value& aRecords, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  bool isValid;

  // First parameter needs to be an array, and of type NdefRecord
  if (ValidateNdefTag(aRecords, aCx, &isValid) != NS_OK) {
    if (!isValid) {
      NS_WARNING("Error: WriteNdefTag requires an NdefRecord array type.");
      return NS_ERROR_INVALID_ARG;
    }
  }

  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NdefWrite(GetOwner(), aRecords, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefPush(const JS::Value& aRecords, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  bool isValid;

  // First parameter needs to be an array, and of type NdefRecord
  if (ValidateNdefTag(aRecords, aCx, &isValid) != NS_OK) {
    if (!isValid) {
      NS_WARNING("Error: NdefPush requires an NdefRecord array type.");
      return NS_ERROR_INVALID_ARG;
    }
  }

  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NdefPush(GetOwner(), aRecords, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefMakeReadOnly(JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  *aRequest = nullptr;
  nsresult rv = mNfc->NdefMakeReadOnly(GetOwner(), aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NfcATagDetails(JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NfcATagDetails(GetOwner(), aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NfcATagTransceive(const JS::Value& aParams, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  nsresult rv = mNfc->NfcATagTransceive(GetOwner(), aParams, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::Connect(uint32_t techType, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  *aRequest = nullptr;
  nsresult rv = mNfc->Connect(GetOwner(), techType, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::Close(JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  *aRequest = nullptr;
  nsresult rv = mNfc->Close(GetOwner(), aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult
NS_NewNfc(nsPIDOMWindow* aWindow, nsIDOMMozNfc** aNfc)
{
  NS_ASSERTION(aWindow, "Null pointer!");

  // Make sure we're dealing with an inner window.
  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
                               aWindow :
                               aWindow->GetCurrentInnerWindow();
  NS_ENSURE_TRUE(innerWindow, NS_ERROR_FAILURE);

  // Make sure we're being called from a window that we have permission to
  // access.
  if (!nsContentUtils::CanCallerAccess(innerWindow)) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsCOMPtr<nsIDocument> document =
    do_QueryInterface(innerWindow->GetExtantDoc());
  NS_ENSURE_TRUE(document, NS_NOINTERFACE);

  // Do security checks.
  nsCOMPtr<nsIURI> uri;
  document->NodePrincipal()->GetURI(getter_AddRefs(uri));

  // Security checks passed, make a NFC object.
  nsresult rv;
  nsCOMPtr<nsINfcContentHelper> nfc =
    do_GetService(NS_NFCCONTENTHELPER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  nsRefPtr<nsNfc> domNfc = nsNfc::Create(innerWindow, nfc);
  NS_ENSURE_TRUE(domNfc, NS_ERROR_UNEXPECTED);

  domNfc.forget(aNfc);
  return NS_OK;
}
