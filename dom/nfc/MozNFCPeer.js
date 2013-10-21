/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013, Deutsche Telekom, Inc. */

"use strict";

const DEBUG = false;
function debug(s) {
  if (DEBUG) dump("-*- Nfc MozNFCPeer: " + s + "\n");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

function MozNFCPeer() {
  debug("XXXX In MozNFCPeer Constructor");
  this._nfcContentHelper = Cc["@mozilla.org/nfc/content-helper;1"]
                             .getService(Ci.nsINfcContentHelper);
  this.session = null;
}
MozNFCPeer.prototype = {
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

  // NFCPeer interface:
  sendNDEF: function sendNDEF(records) {
    // Just forward sendNDEF to writeNDEF
    return this._nfcContentHelper.writeNDEF(this._window, records);
  },

  classID: Components.ID("{c1b2bcf0-35eb-11e3-aa6e-0800200c9a66}"),
  contractID: "@mozilla.org/nfc/NFCPeer;1",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([MozNFCPeer]);
