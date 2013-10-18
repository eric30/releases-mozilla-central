/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright © 2013, Deutsche Telekom, Inc.
 */

"use strict";

const DEBUG = false;
function debug(s) {
  if (DEBUG) dump("-*- Nfc MozNFCTag: " + s + "\n");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

function MozNFCTag() {
  debug("XXXX In MozNFCTag Constructor");
  this._nfcContentHelper = Cc["@mozilla.org/nfc/content-helper;1"]
                             .getService(Ci.nsINfcContentHelper);
  this.session = null;
  // Map WebIDL declared enum map names to integer
  this._techTypesMap = [];
  this._techTypesMap['NFC_A'] = 0;
  this._techTypesMap['NFC_B'] = 1;
  this._techTypesMap['NFC_ISO_DEP'] = 2;
  this._techTypesMap['NFC_F'] = 3;
  this._techTypesMap['NFC_V'] = 4;
  this._techTypesMap['NDEF'] = 5;
  this._techTypesMap['NDEF_FORMATABLE'] = 6;
  this._techTypesMap['MIFARE_CLASSIC'] = 7;
  this._techTypesMap['MIFARE_ULTRALIGHT'] = 8;
  this._techTypesMap['NFC_BARCODE'] = 9;
  this._techTypesMap['P2P'] = 10;
}
MozNFCTag.prototype = {
  _nfcContentHelper: null,
  _window: null,

  init: function init(aWindow) {
    this._window = aWindow;
  },

  __init: function(aSessionToken) {
    this.setSessionToken(aSessionToken);
  },

  // ChromeOnly interface
  setSessionToken: function setSessionToken(aSessionToken) {
    debug("Setting session token.");
    this.session = aSessionToken;
    // report to NFC worker:
    return this._nfcContentHelper.setSessionToken(aSessionToken);
  },

  _techTypesMap: null,

  // NFCTag interface:
  getDetailsNDEF: function getDetailsNDEF() {
    return this._nfcContentHelper.getDetailsNDEF(this._window, this.session);
  },
  readNDEF: function readNDEF() {
    return this._nfcContentHelper.readNDEF(this._window, this.session);
  },
  writeNDEF: function writeNDEF(records) {
    return this._nfcContentHelper.writeNDEF(this._window, records, this.session);
  },
  makeReadOnlyNDEF: function makeReadOnlyNDEF() {
    return this._nfcContentHelper.makeReadOnlyNDEF(this._window, this.session);
  },
  connect: function connect(enum_tech_type) {
    let int_tech_type = this._techTypesMap[enum_tech_type];
    return this._nfcContentHelper.connect(this._window, int_tech_type, this.session);
  },
  close: function close() {
    return this._nfcContentHelper.close(this._window, this.session);
  },

  classID: Components.ID("{4e1e2e90-3137-11e3-aa6e-0800200c9a66}"),
  contractID: "@mozilla.org/nfc/NFCTag;1",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([MozNFCTag]);
