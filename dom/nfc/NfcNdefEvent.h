/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_nfc_ndefevent_h__
#define mozilla_dom_nfc_ndefevent_h__

#include "nsIDOMNfcNdefEvent.h"
#include "nsIDOMEventTarget.h"

#include "nsDOMEvent.h"

namespace mozilla {
namespace dom {
namespace nfc {

class NfcNdefEvent : public nsDOMEvent,
                     public nsIDOMNfcNdefEvent
{
  const JS::Value& mNdefMessages;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_TO_NSDOMEVENT
  NS_DECL_NSIDOMNFCNDEFEVENT
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(NfcNdefEvent, nsDOMEvent)

  static already_AddRefed<nsDOMEvent>
  Create(mozilla::dom::EventTarget* aOwner, const JS::Value& aNdefMessages);

  nsresult
  Dispatch(nsIDOMEventTarget* aTarget, const nsAString& aEventType)
  {
    NS_ASSERTION(aTarget, "Null pointer!");
    NS_ASSERTION(!aEventType.IsEmpty(), "Empty event type!");

    nsresult rv = InitEvent(aEventType, false, false);
    NS_ENSURE_SUCCESS(rv, rv);

    SetTrusted(true);

    nsIDOMEvent* thisEvent =
      static_cast<nsDOMEvent*>(const_cast<NfcNdefEvent*>(this));

    bool dummy;
    rv = aTarget->DispatchEvent(thisEvent, &dummy);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

private:
  NfcNdefEvent(mozilla::dom::EventTarget* aOwner, const JS::Value& aNdefMessages)
  : nsDOMEvent(aOwner, nullptr, nullptr), mNdefMessages(aNdefMessages)
  { }

  ~NfcNdefEvent()
  { }
};

} /* namespace nfc */
} /* namespace dom */
} /* namespace mozilla */

#endif // mozilla_dom_nfc_nfcndefevent_h__
