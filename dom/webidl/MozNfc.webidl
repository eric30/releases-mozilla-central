/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

 /* Copyright © 2013 Deutsche Telekom, Inc. */

[JSImplementation="@mozilla.org/navigatorNfc;1",
 NavigatorProperty="mozNfc"]
interface MozNfc : EventTarget {
   MozNFCTag getNFCTag(unsigned long sessionId);
   /*DOMRequest getNFCPeer(unsigned long sessionId);*/

   /*attribute EventHandler onpeerfound;
   attribute EventHandler onpeerlost;
   attribute EventHandler onforegrounddispatch;*/
};

// Hack expected values
enum NFCTechTypes {
  "P2P",
  "1",
  "NDEF_FORMATTABLE",
  "3",
  "4",
  "5",
  "NDEF",
  "7",
  "8",
  "MIFARE_ULTRALIGHT"
};
