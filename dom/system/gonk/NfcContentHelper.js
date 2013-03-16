/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

const DEBUG = true; // set to true to see debug messages

const NFCCONTENTHELPER_CID =
  Components.ID("{4d72c120-da5f-11e1-9b23-0800200c9a66}");


const NFC_IPC_MSG_NAMES = [
  "NFC:NdefDiscovered",
  "NFC:NdefDisconnected",
  "NFC:RequestStatus",
  "NFC:SecureElementActivated",
  "NFC:SecureElementDeactivated",
  "NFC:SecureElementTransaction"
];

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsISyncMessageSender");

function NfcContentHelper() {
  this.initRequests();
  this.initMessageListener(NFC_IPC_MSG_NAMES);
  Services.obs.addObserver(this, "xpcom-shutdown", false);
}

NfcContentHelper.prototype = {
  __proto__: DOMRequestIpcHelper.prototype,

  QueryInterface: XPCOMUtils.generateQI([Ci.nsINfcContentHelper,
                                         Ci.nsIObserver]),
  classID:   NFCCONTENTHELPER_CID,
  classInfo: XPCOMUtils.generateCI({
    classID:          NFCCONTENTHELPER_CID,
    classDescription: "NfcContentHelper",
    interfaces:       [Ci.nsINfcContentHelper]
  }),

  sendToNfcd: function sendToNfcd(message) {
    cpmm.sendAsyncMessage("NFC:SendToNfcd", message);
  },

  encodeNdefRecords: function encodeNdefRecords(records) {
    var encodedRecords = new Array();
    for(var i=0; i < records.length; i++) {
      var record = records[i];
      encodedRecords.push({
        tnf: record.tnf,
        type: btoa(record.type),
        id: btoa(record.id),
        payload: btoa(record.payload),
      });
    }
    return encodedRecords;
  },

  ndefPush: function ndefPush(window, records) {
    debug("ndefPush");
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));

    let encodedRecords = this.encodeNdefRecords(records);

    cpmm.sendAsyncMessage("NFC:NdefPush", {
      requestId: requestId,
      records: encodedRecords
    });
    return request;
  },

  writeNdefTag: function writeNdefTag(window, records) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));

    let encodedRecords = this.encodeNdefRecords(records);

    cpmm.sendAsyncMessage("NFC:WriteNdefTag", {
      requestId: requestId,
      records: encodedRecords
    });
    return request;
  },

  registerCallback: function registerCallback(callbackType, callback) {
    let callbacks = this[callbackType];
    if (!callbacks) {
      callbacks = this[callbackType] = [];
    }

    if (callbacks.indexOf(callback) != -1) {
      throw new Error("Already registered this callback!");
    }

    callbacks.push(callback);
    if (DEBUG) debug("Registered " + callbackType + " callback: " + callback);
  },

  unregisterCallback: function unregisterCallback(callbackType, callback) {
    let callbacks = this[callbackType];
    if (!callbacks) {
      return;
    }

    let index = callbacks.indexOf(callback);
    if (index != -1) {
      callbacks.splice(index, 1);
      if (DEBUG) debug("Unregistered telephony callback: " + callback);
    }
  },

  registerNfcCallback: function registerNfcCallback(callback) {
    this.registerCallback("_nfcCallbacks", callback);
  },

  unregisterNfcCallback: function unregisterNfcCallback(callback) {
    this.unregisterCallback("_nfcCallbacks", callback);
  },


  // nsIObserver

  observe: function observe(subject, topic, data) {
    if (topic == "xpcom-shutdown") {
      this.removeMessageListener();
      Services.obs.removeObserver(this, "xpcom-shutdown");
      cpmm = null;
    }
  },

  // nsIMessageListener


  fireRequestSuccess: function fireRequestSuccess(requestId, result) {
    let request = this.takeRequest(requestId);
    if (!request) {
      if (DEBUG) {
        debug("not firing success for id: " + requestId +
              ", result: " + JSON.stringify(result));
      }
      return;
    }

    if (DEBUG) {
      debug("fire request success, id: " + requestId +
            ", result: " + JSON.stringify(result));
    }
    Services.DOMRequest.fireSuccess(request, result);
  },

  dispatchFireRequestSuccess: function dispatchFireRequestSuccess(requestId, result) {
    let currentThread = Services.tm.currentThread;

    currentThread.dispatch(this.fireRequestSuccess.bind(this, requestId, result),
                           Ci.nsIThread.DISPATCH_NORMAL);
  },

  fireRequestError: function fireRequestError(requestId, error) {
    let request = this.takeRequest(requestId);
    if (!request) {
      if (DEBUG) {
        debug("not firing error for id: " + requestId +
              ", error: " + JSON.stringify(error));
      }
      return;
    }

    if (DEBUG) {
      debug("fire request error, id: " + requestId +
            ", result: " + JSON.stringify(error));
    }
    Services.DOMRequest.fireError(request, error);
  },

  dispatchFireRequestError: function dispatchFireRequestError(requestId, error) {
    let currentThread = Services.tm.currentThread;

    currentThread.dispatch(this.fireRequestError.bind(this, requestId, error),
                           Ci.nsIThread.DISPATCH_NORMAL);
  },

  receiveMessage: function receiveMessage(message) {
    let request;
    switch (message.name) {
      case "NFC:NdefDiscovered":
        this.handleNdefDiscovered(message.json);
        break;
      case "NFC:NdefDisconnected":
        this.handleNdefDisconnected(message.json);
        break;
      case "NFC:RequestStatus":
        this.handleRequestStatus(message.json);
        break;
      case "NFC:SecureElementActivated":
        this.handleSecureElementActivated(message.json);
        break;
      case "NFC:SecureElementDeactivated":
        this.handleSecureElementDeactivated(message.json);
        break;
      case "NFC:SecureElementTransaction":
        this.handleSecureElementTransaction(message.json);
        break;
    }
  },

  handleNdefDiscovered: function handleNdefDiscovered(message) {
    let records = message.content.records;
    for (var i = 0; i < records.length; i++) {
      records[i].tnf = records[i].tnf;
      records[i].type = atob(records[i].type);
      records[i].id = atob(records[i].id);
      records[i].payload = atob(records[i].payload);
    }
    this._deliverCallback("_nfcCallbacks", "ndefDiscovered", [JSON.stringify(records)]);
  },

  handleNdefDisconnected: function handleNdefDisconnected(message) {
    this._deliverCallback("_nfcCallbacks", "ndefDisconnected", [JSON.stringify(message)]);
  },

  handleRequestStatus: function handleRequestStatus(message) {
    let response = message.content; // Subfields of content: requestId, status, optional message
    let requestId = atob(response.requestId);
    debug("handleRequestStatus (" + response.requestId + ", " + response.status + ")");
    if (response.status == "OK") {
      this.fireRequestSuccess(requestId, response.message);
    } else {
      this.fireRequestError(requestId, response.message);
    }
  },

  handleSecureElementActivated: function handleSecureElementActivated(message) {
    this._deliverCallback("_nfcCallbacks", "secureElementActivated", [JSON.stringify(message)]);
  },

  handleSecureElementDeactivated: function handleSecureElementDeactivated(message) {
    this._deliverCallback("_nfcCallbacks", "secureElementDeactivated", [JSON.stringify(message)]);
  },

  handleSecureElementTransaction: function handleSecureElementTransaction(message) {
    this._deliverCallback("_nfcCallbacks", "secureElementTransaction", [JSON.stringify(message)]);
  },

  _deliverCallback: function _deliverCallback(callbackType, name, args) {
    let thisCallbacks = this[callbackType];
    if (!thisCallbacks) {
      return;
    }

    let callbacks = thisCallbacks.slice();
    for each (let callback in callbacks) {
      if (thisCallbacks.indexOf(callback) == -1) {
        continue;
      }
      let handler = callback[name];
      if (typeof handler != "function") {
        throw new Error("No handler for " + name);
      }
      try {
        handler.apply(callback, args);
      } catch (e) {
        debug("callback handler for " + name + " threw an exception: " + e);
      }
    }
  },


};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([NfcContentHelper]);

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-*- NfcContentHelper: " + s + "\n");
  };
} else {
  debug = function (s) {};
}
