/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013 Deutsche Telekom, Inc. */

#include "nsContentUtils.h"
#include "NfcEvent.h"

#include "nsJSON.h"
#include "jsapi.h"
#include "jsfriendapi.h"

namespace mozilla {
namespace dom {
namespace nfc {

already_AddRefed<NfcEvent>
NfcEvent::Create(mozilla::dom::EventTarget* aOwner,
                        const nsAString& aMessage)
{
  nsRefPtr<NfcEvent> event = new NfcEvent(aOwner);
  event->mMessage = aMessage;
  return event.forget();
}

NS_IMPL_ADDREF_INHERITED(NfcEvent, nsDOMEvent)
NS_IMPL_RELEASE_INHERITED(NfcEvent, nsDOMEvent)

NS_INTERFACE_MAP_BEGIN(NfcEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMMozNfcEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEvent)

NS_IMETHODIMP
NfcEvent::GetMessage(JSContext* aCx, JS::Value* aMessage)

{
  nsCOMPtr<nsIJSON> json(new nsJSON());

  if (!mMessage.IsEmpty()) {
    nsresult rv = json->DecodeToJSVal(mMessage, aCx, aMessage);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    *aMessage = JSVAL_VOID;
  }

  return NS_OK;
}

}
}
}
