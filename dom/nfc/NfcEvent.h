/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013 Deutsche Telekom, Inc. */

#ifndef mozilla_dom_nfc_nfcevent_h
#define mozilla_dom_nfc_nfcevent_h

#include "nsDOMEvent.h"
#include "nsIDOMNfcEvent.h"
#include "mozilla/dom/MozNfcEventBinding.h"

namespace mozilla {
namespace dom {
namespace nfc {

class NfcEvent : public nsDOMEvent,
                        public nsIDOMMozNfcEvent
{
  nsString mMessage;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_TO_NSDOMEVENT
  NS_DECL_NSIDOMMOZNFCEVENT
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(NfcEvent, nsDOMEvent)

  static already_AddRefed<NfcEvent>
  Create(EventTarget* aOwner, const nsAString& aMessage);

  nsresult
  Dispatch(EventTarget* aTarget, const nsAString& aEventType)
  {
    NS_ASSERTION(aTarget, "Null pointer!");
    NS_ASSERTION(!aEventType.IsEmpty(), "Empty event type!");

    nsresult rv = InitEvent(aEventType, false, false);
    NS_ENSURE_SUCCESS(rv, rv);

    SetTrusted(true);

    nsDOMEvent* thisEvent = this;

    bool dummy;
    rv = aTarget->DispatchEvent(thisEvent, &dummy);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aScope) MOZ_OVERRIDE
  {
    return mozilla::dom::MozNfcEventBinding::Wrap(aCx, aScope, this);
  }

  JS::Value GetMessage(JSContext* aCx, mozilla::ErrorResult& aRv)
  {
    JS::Rooted<JS::Value> retVal(aCx);
    aRv = GetMessage(aCx, retVal.address());
    return retVal;
  }

private:
  NfcEvent(mozilla::dom::EventTarget* aOwner)
  : nsDOMEvent(aOwner, nullptr, nullptr)
  {
    SetIsDOMBinding();
  }

  ~NfcEvent()
  { }
};

}
}
}

#endif // mozilla_dom_nfc_nfcevent_h
