#include "base/basictypes.h"
#include "BluetoothNewSocket.h"
#include "DOMRequest.h"
#include "mozilla/dom/BluetoothNewSocketBinding.h"
#include "nsDOMClassInfo.h"

USING_BLUETOOTH_NAMESPACE
using namespace mozilla::dom;

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothNewSocket)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothNewSocket, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothNewSocket, nsDOMEventTargetHelper)

BluetoothNewSocket::BluetoothNewSocket(nsPIDOMWindow* aWindow)
  : nsDOMEventTargetHelper(aWindow)
{
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(IsDOMBinding());

  BindToOwner(aWindow);
}

BluetoothNewSocket::~BluetoothNewSocket()
{
}

already_AddRefed<DOMRequest>
BluetoothNewSocket::Open(const nsAString& aDeviceAddress,
                         const nsAString& aServiceUuid,
                         ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> win = GetOwner();
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsRefPtr<DOMRequest> request = new DOMRequest(win);
  return request.forget();
}

JSObject*
BluetoothNewSocket::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope)
{
  return BluetoothNewSocketBinding::Wrap(aCx, aScope, this);
}

