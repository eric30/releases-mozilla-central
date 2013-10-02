/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013, Deutsche Telekom, Inc. */

"use strict";

const DEBUG = true;
function debug(s) {
  if (DEBUG) dump("-*- Nfc DOM: " + s + "\n");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/ObjectWrapper.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsIMessageSender");


let myGlobal = this;

function mozNfc() {
  debug("XXXX In mozNfc Constructor");
  this._nfcContentHelper = Cc["@mozilla.org/nfc/content-helper;1"].getService(Ci.nsINfcContentHelper);
}

mozNfc.prototype = {
  _nfcContentHelper: null,
  _window: null,
  _wrap: function _wrap(obj) {
    return ObjectWrapper.wrap(obj, this._window);
  },

  init: function init(aWindow) {
    debug("XXXXXXXXXX init called XXXXXXXXXXXX");
    this._window = aWindow;
  },

  getDetailsNDEF: function getDetailsNDEF() {
    return this._nfcContentHelper.getDetailsNDEF(this._window);
  },
  readNDEF: function readNDEF() {
    return this._nfcContentHelper.readNDEF(this._window);
  },
  writeNDEF: function writeNDEF(records) {
    return this._nfcContentHelper.writeNDEF(this._window, records);
  },
  makeReadOnlyNDEF: function makeReadOnlyNDEF() {
    return this._nfcContentHelper.makeReadOnlyNDEF(this._window);
  },

  connect: function connect_with_type(int_tech_type) {
    return this._nfcContentHelper.connect(this._window, int_tech_type);
  },
  close: function close() {
    return this._nfcContentHelper.close(this._window);
  },

  classID: Components.ID("{6ff2b290-2573-11e3-8224-0800200c9a66}"),
  contractID: "@mozilla.org/navigatorNfc;1",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([mozNfc]);
