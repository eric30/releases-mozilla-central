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

/* Copyright © 2013, Deutsche Telekom, Inc. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/ObjectWrapper.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

var NFC = {};
Cu.import("resource://gre/modules/nfc_consts.js", NFC);

// set to true to in nfc_consts.js to see debug messages
var DEBUG = NFC.DEBUG_CONTENT_HELPER;

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-*- NfcContentHelper: " + s + "\n");
  };
} else {
  debug = function (s) {};
}

const NFCCONTENTHELPER_CID =
  Components.ID("{4d72c120-da5f-11e1-9b23-0800200c9a66}");

const NFC_IPC_MSG_NAMES = [
  "NFC:DetailsNDEFResponse",
  "NFC:ReadNDEFResponse",
  "NFC:WriteNDEFResponse",
  "NFC:MakeReadOnlyNDEFResponse",
  "NFC:ConnectResponse",
  "NFC:CloseResponse"
];

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsISyncMessageSender");

function NfcContentHelper() {
  this.initDOMRequestHelper(/* aWindow */ null, NFC_IPC_MSG_NAMES);
  Services.obs.addObserver(this, "xpcom-shutdown", false);

  this._requestMap = [];
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

  _requestMap: null,

  // FIXME: btoa's will be unneeded when binary nfcd/gonk protocol is merged.
  encodeNdefRecords: function encodeNdefRecords(records) {
    var encodedRecords = [];
    for(var i=0; i < records.length; i++) {
      var record = records[i];
      encodedRecords.push({
        tnf: record.tnf,
        type: record.type,
        id: record.id,
        payload: record.payload,
      });
    }
    return encodedRecords;
  },

  // NFC interface:
  setSessionToken: function setSession(sessionToken) {
    if (sessionToken== null) {
      throw Components.Exception("No session token!",
                                  Cr.NS_ERROR_UNEXPECTED);
      return false;
    }
    // Report session to Nfc.js only.
    cpmm.sendAsyncMessage("NFC:SetSessionToken", {
      sessionToken: sessionToken,
    });
    return true;
  },

  // NFCTag interface
  getDetailsNDEF: function getDetailsNDEF(window, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }
    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:GetDetailsNDEF", {
      requestId: requestId,
      sessionId: sessionToken
    });
    return request;
  },

  readNDEF: function readNDEF(window, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }
    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:ReadNDEF", {
      requestId: requestId,
      sessionId: sessionToken
    });
    return request;
  },

  writeNDEF: function writeNDEF(window, records, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    let encodedRecords = this.encodeNdefRecords(records);

    cpmm.sendAsyncMessage("NFC:WriteNDEF", {
      requestId: requestId,
      sessionId: sessionToken,
      records: encodedRecords
    });
    return request;
  },

  makeReadOnlyNDEF: function makeReadOnlyNDEF(window, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:MakeReadOnlyNDEF", {
      requestId: requestId,
      sessionId: sessionToken
    });
    return request;
  },

  connect: function connect(window, techType, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }
    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:Connect", {
      requestId: requestId,
      sessionId: sessionToken,
      techType: techType
    });
    return request;
  },

  close: function close(window, sessionToken) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }
    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:Close", {
      requestId: requestId,
      sessionId: sessionToken
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
    debug("Registered " + callbackType + " callback: " + callback);
  },

  unregisterCallback: function unregisterCallback(callbackType, callback) {
    let callbacks = this[callbackType];
    if (!callbacks) {
      return;
    }

    let index = callbacks.indexOf(callback);
    if (index != -1) {
      callbacks.splice(index, 1);
      debug("Unregistered telephony callback: " + callback);
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
      debug("not firing success for id: " + requestId +
            ", result: " + JSON.stringify(result));
      return;
    }

    debug("fire request success, id: " + requestId +
          ", result: " + JSON.stringify(result));
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
      debug("not firing error for id: " + requestId +
            ", error: " + JSON.stringify(error));
      return;
    }

    debug("fire request error, id: " + requestId +
          ", result: " + JSON.stringify(error));
    Services.DOMRequest.fireError(request, error);
  },

  dispatchFireRequestError: function dispatchFireRequestError(requestId, error) {
    let currentThread = Services.tm.currentThread;

    currentThread.dispatch(this.fireRequestError.bind(this, requestId, error),
                           Ci.nsIThread.DISPATCH_NORMAL);
  },

  receiveMessage: function receiveMessage(message) {
    debug("Message received: " + JSON.stringify(message));
    switch (message.name) {
      case "NFC:DetailsNDEFResponse":
        this.handleDetailsNDEFResponse(message.json);
        break;
      case "NFC:ReadNDEFResponse":
        this.handleReadNDEFResponse(message.json);
        break;
      case "NFC:WriteNDEFResponse":
        this.handleWriteNDEFResponse(message.json);
        break;
      case "NFC:MakeReadOnlyNDEFResponse":
        this.handleMakeReadOnlyNDEFResponse(message.json);
        break;
      case "NFC:ConnectResponse":
        this.handleConnectResponse(message.json);
        break;
      case "NFC:CloseResponse":
        this.handleCloseResponse(message.json);
        break;
    }
  },

  handleDetailsNDEFResponse: function handleDetailsNDEFResponse(message) {
    debug("DetailsNDEFResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);

    if (message.content.status != "OK") {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleReadNDEFResponse: function handleReadNDEFResponse(message) {
    debug("ReadNDEFResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);
    let records = result.records.map(function(r) {
      let type = "";
      for (let i = 0; i < r.type.length; i++) {
        type += String.fromCharCode(r.type[i]);
      }
      r.type = type;
      let id = "";
      for (let i = 0; i < r.id.length; i++) {
        id += String.fromCharCode(r.id[i]);
      }
      r.id = id;
      let payload = "";
      for (let i = 0; i < r.payload.length; i++) {
        payload += String.fromCharCode(r.payload[i]);
      }
      r.payload = payload;
      return r;
    });
    let resultA = {records: records};

    if (!resultA.records) {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleWriteNDEFResponse: function handleWriteNDEFResponse(message) {
    debug("WriteNDEFResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       debug('Returning: requester: ' + requester);
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);

    if (result.status != "OK") {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleMakeReadOnlyNDEFResponse: function handleReadOnlyNDEFResponse(message) {
    debug("MakeReadOnlyNDEFResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       debug('Returning: requester: ' + requester);
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);

    if (result.status != "OK") {
      this.fireRequestError(requestId, ObjectWrapper.wrap(result, requester.win));
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleConnectResponse: function handleConnectResponse(message) {
    debug("ConnectResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       debug('ConnectResponse return requester='+requester+" message.sessionId="+message.sessionId);
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);

    if (message.content.status != "OK") {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleCloseResponse: function handleCloseResponse(message) {
    debug("CloseResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if (typeof requester === 'undefined') {
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);


    if (message.content.status != "OK") {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
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
