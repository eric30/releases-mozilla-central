/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

 /* Copyright © 2013 Deutsche Telekom, Inc. */

enum NFCTechType {
  "P2P",
  "NDEF_FORMATTABLE",
  "NDEF",
  "MIFARE_ULTRALIGHT"
};

[Constructor(DOMString sessionId),
 JSImplementation="@mozilla.org/nfc/NFCTag;1"]
interface MozNFCTag : EventTarget {
  [ChromeOnly]
  attribute DOMString session;
  [ChromeOnly]
  boolean setSessionToken(DOMString sessionToken);

  DOMRequest getDetailsNDEF();
  DOMRequest readNDEF();
  DOMRequest writeNDEF();
  DOMRequest makeReadOnlyNDEF();

  DOMRequest connect(NFCTechType techType);
  DOMRequest close();
};
