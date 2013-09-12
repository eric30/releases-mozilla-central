#include "base/basictypes.h"
#include "BluetoothNewSocket.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothSocket.h"
#include "BluetoothUtils.h"
#include "DOMRequest.h"
#include "mozilla/dom/BluetoothNewSocketBinding.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsDOMClassInfo.h"

USING_BLUETOOTH_NAMESPACE
using namespace mozilla::dom;

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothNewSocket)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothNewSocket, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothNewSocket, nsDOMEventTargetHelper)

BluetoothNewSocket::BluetoothNewSocket(nsPIDOMWindow* aWindow,
                                       const nsAString& aAddress)
  : nsDOMEventTargetHelper(aWindow)
  , mAddress(aAddress)
{
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(IsDOMBinding());

  BindToOwner(aWindow);
}

BluetoothNewSocket::~BluetoothNewSocket()
{
}

// The following functions are inherited from BluetoothSocketObserver
void
BluetoothNewSocket::ReceiveSocketData(
    BluetoothSocket* aSocket,
    nsAutoPtr<mozilla::ipc::UnixSocketRawData>& aMessage)
{
  BT_LOG("NewSocket - receiveSocketData");
}

void
BluetoothNewSocket::OnSocketConnectSuccess(BluetoothSocket* aSocket)
{
  BT_LOG("NewSocket - OnSocketConnectSuccess");

  DispatchBluetoothReply(mRunnable, BluetoothValue(true), EmptyString());

  mRunnable = nullptr;
}

void
BluetoothNewSocket::OnSocketConnectError(BluetoothSocket* aSocket)
{
  BT_LOG("NewSocket - OnSocketConnectError");
}

void
BluetoothNewSocket::OnSocketDisconnect(BluetoothSocket* aSocket)
{
  BT_LOG("NewSocket - OnSocketDisconnect");
}

already_AddRefed<DOMRequest>
BluetoothNewSocket::Open(const nsAString& aServiceUuid,
                         ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> win = GetOwner();
  if (!win) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsRefPtr<DOMRequest> request = new DOMRequest(win);
  mRunnable = new BluetoothVoidReplyRunnable(request);
  nsRefPtr<BluetoothSocket> s =
    new BluetoothSocket(nullptr, BluetoothSocketType::RFCOMM, true, true);

  BT_LOG("Ready to connect with: %s", NS_ConvertUTF16toUTF8(mAddress).get());

  s->Connect(NS_ConvertUTF16toUTF8(mAddress), 2);

  return request.forget();
}

JSObject*
BluetoothNewSocket::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope)
{
  return BluetoothNewSocketBinding::Wrap(aCx, aScope, this);
}

