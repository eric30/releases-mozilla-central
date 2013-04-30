/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNfc.h"
#include "Nfc.h"

#include "nsIDocument.h"
#include "nsIURI.h"
#include "nsPIDOMWindow.h"
#include "nsJSON.h"

#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "SystemWorkerManager.h"

#include "NfcNdefEvent.h"

using namespace mozilla::dom::nfc;

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
  NS_INTERFACE_MAP_ENTRY(nsIDOMNfc)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Nfc)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsNfc, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsNfc, nsDOMEventTargetHelper)

DOMCI_DATA(Nfc, nsNfc)

NS_IMPL_ISUPPORTS1(nsNfc::NfcCallback, nsINfcCallback)

NS_IMPL_EVENT_HANDLER(nsNfc, ndefdiscovered)
NS_IMPL_EVENT_HANDLER(nsNfc, ndefdisconnected)
NS_IMPL_EVENT_HANDLER(nsNfc, secureelementactivated)
NS_IMPL_EVENT_HANDLER(nsNfc, secureelementdeactivated)
NS_IMPL_EVENT_HANDLER(nsNfc, secureelementtransaction)


NS_IMETHODIMP
nsNfc::NdefDiscovered(const JS::Value& aNdefRecords, JSContext* aCx)
{
  // Parse JSON
  nsString result;
  nsresult rv;

  nsCOMPtr<nsIJSON> json(new nsJSON());
  rv = json->EncodeFromJSVal((jsval*)&aNdefRecords, aCx, result); // EncodeJSVal param1 not const...
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(this, result);
  NS_ASSERTION(event, "This should never fail!");

  rv = event->InitEvent(NS_LITERAL_STRING("ndefdiscovered"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefDisconnected(const JS::Value& aNfcHandle, JSContext* aCx) {
  nsString result;
  nsresult rv;

  nsCOMPtr<nsIJSON> json(new nsJSON());
  rv = json->EncodeFromJSVal((jsval*)&aNfcHandle, aCx, result);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(this, result);
  NS_ASSERTION(event, "This should never fail!");

  rv = event->InitEvent(NS_LITERAL_STRING("ndefdisconnected"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNfc::ValidateNdefTag(const JS::Value& aRecords, JSContext* aCx, bool* result)
{
  JSObject &obj = aRecords.toObject();
  // Check if array
  if (!JS_IsArrayObject(aCx, &obj)) {
    NS_WARNING("error: NdefRecord object array is required.");
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
  const char *ndefRecordName = "NdefRecord";
  for (uint32_t index = 0; index < length; index++) {
    JS::Value val;
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
nsNfc::WriteNdefTag(const JS::Value& aRecords, JSContext* aCx, nsIDOMDOMRequest** aRequest)
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
  nsresult rv = mNfc->WriteNdefTag(GetOwner(), aRecords, aRequest);
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
      NS_WARNING("Error: WriteNdefTag requires an NdefRecord array type.");
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
nsNfc::SecureElementActivated(const JS::Value& aSEMessage, JSContext* aCx) {
  nsString result;
  nsresult rv;

  nsCOMPtr<nsIJSON> json(new nsJSON());
  rv = json->EncodeFromJSVal((jsval*)&aSEMessage, aCx, result);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(this, result);
  NS_ASSERTION(event, "This should never fail!");
  rv = event->InitEvent(NS_LITERAL_STRING("secureelementactivated"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNfc::SecureElementDeactivated(const JS::Value& aSEMessage, JSContext* aCx) {
  nsString result;
  nsresult rv;

  nsCOMPtr<nsIJSON> json(new nsJSON());
  rv = json->EncodeFromJSVal((jsval*)&aSEMessage, aCx, result);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(this, result);
  NS_ASSERTION(event, "This should never fail!");

  rv = event->InitEvent(NS_LITERAL_STRING("secureelementdeactivated"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNfc::SecureElementTransaction(const JS::Value& aSETransactionMessage, JSContext* aCx)
{
  nsString result;
  nsresult rv;

  nsCOMPtr<nsIJSON> json(new nsJSON());
  rv = json->EncodeFromJSVal((jsval*)&aSETransactionMessage, aCx, result);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(this, result);
  NS_ASSERTION(event, "This should never fail!");

  rv = event->InitEvent(NS_LITERAL_STRING("secureelementtransaction"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// TODO: make private
NS_IMETHODIMP
nsNfc::SendToNfcd(const nsAString& message)
{
  nsresult rv = mNfc->SendToNfcd(message);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
NS_NewNfc(nsPIDOMWindow* aWindow, nsIDOMNfc** aNfc)
{
  NS_ASSERTION(aWindow, "Null pointer!");

  // Check if Nfc exists and return null if it doesn't
  if(!mozilla::dom::gonk::SystemWorkerManager::IsNfcEnabled()) {
    *aNfc = nullptr;
    return NS_OK;
  }

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
    do_QueryInterface(innerWindow->GetExtantDocument());
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
