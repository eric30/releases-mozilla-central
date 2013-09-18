/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MozNdefRecord.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/MozNdefRecordBinding.h"
#include "nsContentUtils.h"

namespace mozilla {
namespace dom {


NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(MozNdefRecord)
NS_IMPL_CYCLE_COLLECTING_ADDREF(MozNdefRecord)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MozNdefRecord)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MozNdefRecord)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/* static */
already_AddRefed<MozNdefRecord>
MozNdefRecord::Constructor(const GlobalObject& aGlobal, JSContext* cx,
  uint8_t aTnf, const nsAString& aType, const nsAString& aId, JS::Handle<JS::Value> aPayload,
  ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aGlobal.GetAsSupports());
  nsIDocument* doc;
  if (!win || !(doc = win->GetExtantDoc())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsRefPtr<MozNdefRecord> ndefrecord = new MozNdefRecord(aTnf, aType, aId, aPayload);

  return ndefrecord.forget();
}

MozNdefRecord::MozNdefRecord(uint8_t aTnf, const nsAString& aType, const nsAString& aId, JS::Value aPayload)
  : mTnf(aTnf)
  , mType(aType)
  , mId(aId)
  , mPayload(aPayload)
{
  SetIsDOMBinding();
  MOZ_COUNT_CTOR(MozNdefRecord);
  HoldData();
}

MozNdefRecord::~MozNdefRecord()
{
  MOZ_COUNT_DTOR(MozNdefRecord);
  DropData();
}

JSObject*
MozNdefRecord::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope)
{
  return MozNdefRecordBinding::Wrap(aCx, aScope, this);
}

void
MozNdefRecord::HoldData()
{
  mozilla::HoldJSObjects(this);
}

void
MozNdefRecord::DropData()
{
  if (!JSVAL_IS_NULL(mPayload)) {
    mPayload = JSVAL_NULL;
    mozilla::DropJSObjects(this);
  }
}


} // namespace dom
} // namespace mozilla
