/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_nfc_nfc_h__
#define mozilla_dom_nfc_nfc_h__

#include "nsCOMPtr.h"
#include "nsDOMEventTargetHelper.h"

#include "nsIDOMNfc.h"
#include "nsINfcContentHelper.h"
#include "nsINfcCallback.h"

#define NS_NFC_CONTRACTID "@mozilla.org/nfc/nfc;1"

class nsPIDOMWindow;

nsresult
NS_NewNfc(nsPIDOMWindow* aWindow, nsIDOMNfc** aNfc);

namespace mozilla {
namespace dom {
namespace nfc {

class nsNfc : public nsDOMEventTargetHelper
            , public nsIDOMNfc
{
  nsCOMPtr<nsINfcContentHelper> mNfc;
  nsCOMPtr<nsINfcCallback> mNfcCallback;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMNFC
  NS_DECL_NSINFCCALLBACK
  NS_REALLY_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
                                                   nsNfc,
                                                   nsDOMEventTargetHelper)

  static already_AddRefed<nsNfc>
  Create(nsPIDOMWindow* aOwner, nsINfcContentHelper* aNfc);

  nsIDOMEventTarget*
  ToIDOMEventTarget() const
  {
    return static_cast<nsDOMEventTargetHelper*>(
             const_cast<nsNfc*>(this));
  }

  nsISupports*
  ToISupports() const
  {
    return ToIDOMEventTarget();
  }

private:
  nsNfc();
  ~nsNfc();

  class NfcCallback : public nsINfcCallback
  {
    nsNfc* mNfc;

  public:
    NS_DECL_ISUPPORTS
    NS_FORWARD_NSINFCCALLBACK(mNfc->)

    NfcCallback(nsNfc* aNfc)
    : mNfc(aNfc)
    {
      NS_ASSERTION(mNfc, "Null pointer!");
    }
  };
};

} /* namespace nfc */
} /* namespace dom */
} /* namespace mozilla */

#endif // mozilla_dom_nfc_nfc_h__
