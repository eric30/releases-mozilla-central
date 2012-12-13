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
 
#include "jsapi.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "SystemWorkerManager.h"

#include "nsIRadioInterfaceLayer.h"
 
#include "NfcNdefEvent.h"

using namespace mozilla::dom::nfc;

#if defined(NFC_DEBUG)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk NFC", args)
#else
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Gonk NFC", args)
#endif

static int voo = 0;
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
 
  LOG("nsNfc::Create %d", voo=1);
  return nullptr;
  nsRefPtr<nsNfc> nfc = new nsNfc();
 
  LOG("nsNfc::Create %d", ++voo);
  nfc->BindToOwner(aOwner);
 
  LOG("nsNfc::Create %d", ++voo);
  nfc->mNfc = aNfc;
  LOG("nsNfc::Create %d", ++voo);
  nfc->mNfcCallback = new NfcCallback(nfc);
 
  LOG("nsNfc::Create %d", ++voo); return nfc.forget();
  nsresult rv = aNfc->RegisterNfcCallback(nfc->mNfcCallback);
  LOG("nsNfc::Create %d", ++voo);
  NS_ENSURE_SUCCESS(rv, nullptr);
 
  LOG("nsNfc::Create %d", ++voo);
  return nfc.forget();
}

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
  NS_INTERFACE_MAP_ENTRY(nsIDOMNfc)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Nfc)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsNfc, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsNfc, nsDOMEventTargetHelper)

DOMCI_DATA(Nfc, nsNfc)

NS_IMPL_ISUPPORTS1(nsNfc::NfcCallback, nsINfcCallback)

NS_IMPL_EVENT_HANDLER(nsNfc, ndefdiscovered)
NS_IMPL_EVENT_HANDLER(nsNfc, ndefdisconnected)


NS_IMETHODIMP
nsNfc::NdefDiscovered(const nsAString &aNdefRecords)
{
  // Parse JSON
  jsval result;
  nsresult rv;
  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!JS_ParseJSON(sc->GetNativeContext(), static_cast<const jschar*>(PromiseFlatString(aNdefRecords).get()),
       aNdefRecords.Length(), &result)) {
    LOG("DOM: Couldn't parse JSON for NDEF discovered");
    return NS_ERROR_UNEXPECTED;
  }
 
  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(result);
  NS_ASSERTION(event, "This should never fail!");
 
  rv = event->InitEvent(NS_LITERAL_STRING("ndefdiscovered"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);
 
  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);
 
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefDisconnected(const nsAString &aNfcHandle) {
  jsval result;
  nsresult rv;
  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  int length = aNfcHandle.Length();
  if (!length || !JS_ParseJSON(sc->GetNativeContext(), static_cast<const jschar*>(PromiseFlatString(aNfcHandle).get()),
       aNfcHandle.Length(), &result)) {
    LOG("DOM: Couldn't parse JSON");
    return NS_ERROR_UNEXPECTED;
  }

  // Dispatch incoming event.
  nsRefPtr<nsDOMEvent> event = NfcNdefEvent::Create(result);
  NS_ASSERTION(event, "This should never fail!");

  rv = event->InitEvent(NS_LITERAL_STRING("ndefdisconnected"), false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNfc::ValidateNdefTag(const jsval& aRecords, JSContext* aCx, bool* result)
{
  JSObject &obj = aRecords.toObject();
  // Check if array
  if (!JS_IsArrayObject(aCx, &obj)) {
    LOG("error: MozNdefRecord object array is required.");
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
    jsval val;
    uint32_t namelen;
    if (JS_GetElement(aCx, &obj, index, &val)) {
      const char *name = JS_GetClass(JSVAL_TO_OBJECT(val))->name;
      namelen = strlen(name);
      if (strncmp(ndefRecordName, name, namelen)) {
        LOG("error: WriteNdefTag requires MozNdefRecord array item(s). Item[%d] is type: (%s)", index, name);
        *result = false;
        return NS_OK;
      }
    }
  }
  *result = true;
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::WriteNdefTag(const jsval& aRecords, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  bool isValid;

  // First parameter needs to be an array, and of type MozNdefRecord
  if (ValidateNdefTag(aRecords, aCx, &isValid) != NS_OK) {
    if (!isValid) {
      LOG("Error: WriteNdefTag requires an MozNdefRecord array type.");
      return NS_ERROR_INVALID_ARG;
    }
  }

  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  LOG("Calling WriteNdefTag");
  nsresult rv = mNfc->WriteNdefTag(GetOwner(), aRecords, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
nsNfc::NdefPush(const jsval& aRecords, JSContext* aCx, nsIDOMDOMRequest** aRequest)
{
  bool isValid;

  // First parameter needs to be an array, and of type MozNdefRecord
  if (ValidateNdefTag(aRecords, aCx, &isValid) != NS_OK) {
    if (!isValid) {
      LOG("Error: WriteNdefTag requires an MozNdefRecord array type.");
      return NS_ERROR_INVALID_ARG;
    }
  }

  // Call to NfcContentHelper.js
  *aRequest = nullptr;
  LOG("Calling NdefPush");
  nsresult rv = mNfc->NdefPush(GetOwner(), aRecords, aRequest);
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
 
  voo = 1; 
  LOG("YYYYY NS_NewNfc %d", voo);
  // Check if Nfc exists and return null if it doesn't
  if(!mozilla::dom::gonk::SystemWorkerManager::IsNfcEnabled()) {
    *aNfc = nullptr; 
  LOG("YYYYY NS_NewNfc %d", ++voo);
    return NS_OK;
  }
	 
  // Make sure we're dealing with an inner window.
  LOG("YYYYY NS_NewNfc %d", ++voo);
  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
                               aWindow :
                               aWindow->GetCurrentInnerWindow();
  LOG("YYYYY NS_NewNfc %d", ++voo);
  NS_ENSURE_TRUE(innerWindow, NS_ERROR_FAILURE);
  LOG("YYYYY NS_NewNfc Got Inner Window! %d", ++voo);
 
  LOG("NS_NewNfc %d", ++voo);
  // Make sure we're being called from a window that we have permission to
  // access.
  if (!nsContentUtils::CanCallerAccess(innerWindow)) {
  LOG("NS_NewNfc %d", ++voo);
    return NS_ERROR_DOM_SECURITY_ERR;
  }
	 
  LOG("NS_NewNfc %d", ++voo);
  nsCOMPtr<nsIDocument> document =
    do_QueryInterface(innerWindow->GetExtantDocument());
  LOG("NS_NewNfc %d", voo++);
  NS_ENSURE_TRUE(document, NS_NOINTERFACE);
 
  // Do security checks.
  nsCOMPtr<nsIURI> uri;
  LOG("NS_NewNfc %d", ++voo);
  document->NodePrincipal()->GetURI(getter_AddRefs(uri));

  LOG("YYYYY NS_NewNfc %d", ++voo);
  // Security checks passed, make a NFC object.
  nsCOMPtr<nsINfcContentHelper> nfc =
    do_GetService(NS_NFCCONTENTHELPER_CONTRACTID);
 // nsCOMPtr<nsINfcContentHelper> nfc =
 //   do_GetService("@mozilla.org/nfc/content-helper;1");
    //do_GetService(NS_NFCCONTENTHELPER_CONTRACTID);
  LOG("YYYYY NS_NewNfc %d, returned from get NFCContentHelper!", ++voo);
  nsCOMPtr<nsIRILContentHelper> ril = 
    do_GetService(NS_RILCONTENTHELPER_CONTRACTID);
  NS_ENSURE_TRUE(ril, NS_ERROR_UNEXPECTED);
  LOG("YYYYY NS_NewNfc %d, got ril ContentHelper!", ++voo);
  NS_ENSURE_TRUE(nfc, NS_ERROR_UNEXPECTED);
  LOG("YYYYY NS_NewNfc %d, got NFCContentHelper!", ++voo);
 
  LOG("YYYYY NS_NewNfc %d", ++voo);
  nsRefPtr<nsNfc> domNfc = nsNfc::Create(innerWindow, nfc);
  LOG("YYYYY NS_NewNfc %d", ++voo);
  NS_ENSURE_TRUE(domNfc, NS_ERROR_UNEXPECTED);
 
  domNfc.forget(aNfc);
  return NS_OK;
}
