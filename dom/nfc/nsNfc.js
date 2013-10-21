/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright © 2013, Deutsche Telekom, Inc. */

"use strict";

const DEBUG = false;
function debug(s) {
  if (DEBUG) dump("-*- Nfc DOM: " + s + "\n");
}

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/ObjectWrapper.jsm");

function mozNfc() {
  debug("XXXX In mozNfc Constructor");
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

  getNFCTag: function getNFCTag(sessionToken) {
    let nfcTag = new this._window.MozNFCTag(sessionToken);
    if (nfcTag) {
      return nfcTag;
    } else {
      debug("Error: Unable to create NFCTag");
      return null;
    }
  },

  getNFCPeer: function getNFCPeer(sessionToken) {
    let nfcPeer = new this._window.MozNFCPeer(sessionToken);
    if (nfcPeer) {
      return nfcPeer;
    } else {
      debug("Error: Unable to create NFCPeer");
      return null;
    }
  },

  // get/set onpeerfound/lost onforegrounddispatch

  classID: Components.ID("{6ff2b290-2573-11e3-8224-0800200c9a66}"),
  contractID: "@mozilla.org/navigatorNfc;1",
  QueryInterface: XPCOMUtils.generateQI([Ci.nsISupports,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([mozNfc]);
