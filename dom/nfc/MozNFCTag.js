/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013, Deutsche Telekom, Inc. */

"use strict";

const DEBUG = true;
function debug(s) {
  if (DEBUG) dump("-*- Nfc MozNFCTag: " + s + "\n");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/ObjectWrapper.jsm");

function MozNFCTag() {
  debug("XXXX In MozNFCTag Constructor");
  this._nfcContentHelper = Cc["@mozilla.org/nfc/content-helper;1"]
                             .getService(Ci.nsINfcContentHelper);
  this.session = null;
}
MozNFCTag.prototype = {
  _nfcContentHelper: null,
  _window: null,
  _wrap: function _wrap(obj) {
    return ObjectWrapper.wrap(obj, this._window);
  },

  init: function init(aWindow) {
    debug("XXXXXXXXXX init called XXXXXXXXXXXX");
    this._window = aWindow;
  },

  __init: function(aSessionToken) {
    debug("Init : " + aSessionToken);
    this.setSessionToken(aSessionToken);
  },

  // ChromeOnly interface
  setSessionToken: function setSessionToken(aSessionToken) {
    debug("Setting session token : " + aSessionToken);
    this.session = aSessionToken;
    // report to NFC worker:
    return this._nfcContentHelper.setSessionToken(aSessionToken);
  },

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
  connect: function connect(int_tech_type) {
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
