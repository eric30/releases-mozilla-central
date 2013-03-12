/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMClassInfo.h"

#include "Nfc.h"
#include "NfcNdefEvent.h"

DOMCI_DATA(MozStkCommandEvent, mozilla::dom::nfc::NfcNdefEvent)

using namespace mozilla::dom::nfc;

// static
already_AddRefed<nsDOMEvent>
NfcNdefEvent::Create(mozilla::dom::EventTarget* aOwner, const JS::Value& aNdefMessages)
{
  nsRefPtr<NfcNdefEvent> event = new NfcNdefEvent(aOwner, aNdefMessages);
  return event.forget();
}

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(NfcNdefEvent,
                                                  nsDOMEvent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(NfcNdefEvent,
                                                nsDOMEvent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(NfcNdefEvent, nsDOMEvent)
NS_IMPL_RELEASE_INHERITED(NfcNdefEvent, nsDOMEvent)

NS_INTERFACE_MAP_BEGIN(NfcNdefEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNfcNdefEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(NfcNdefEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEvent)

DOMCI_DATA(NfcNdefEvent, NfcNdefEvent)

NS_IMETHODIMP
NfcNdefEvent::GetNdefMessages(jsval* aNdefMessages)
{
  aNdefMessages->setObjectOrNull(JSVAL_TO_OBJECT(mNdefMessages));
  return NS_OK;
}
