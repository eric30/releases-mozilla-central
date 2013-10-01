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

const DEBUG = true; // set to true to see debug messages

const NFCCONTENTHELPER_CID =
  Components.ID("{4d72c120-da5f-11e1-9b23-0800200c9a66}");


const NFC_IPC_MSG_NAMES = [
  "NFC:TechDiscovered",
  "NFC:TechLost",
  "NFC:NDEFDetailsResponse",
  "NFC:NDEFReadResponse",
  "NFC:NDEFWriteResponse",
  "NFC:NDEFMakeReadOnlyResponse",
  "NFC:ConnectResponse",
  "NFC:CloseResponse"
];

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsISyncMessageSender");

function NfcContentHelper() {
  this.initDOMRequestHelper(/* aWindow */ null, NFC_IPC_MSG_NAMES);
  Services.obs.addObserver(this, "xpcom-shutdown", false);

  this._requestMap = new Array();
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
  _connectedSessionId: null,

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

  ndefDetails: function ndefDetails(window) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:NdefDetails", {
      requestId: requestId,
      sessionId: this._connectedSessionId
    });
    return request;
  },

  ndefRead: function ndefRead(window) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:NdefRead", {
      requestId: requestId,
      sessionId: this._connectedSessionId
    });
    return request;
  },

  ndefWrite: function ndefWrite(window, records) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    let encodedRecords = this.encodeNdefRecords(records);

    cpmm.sendAsyncMessage("NFC:NdefWrite", {
      requestId: requestId,
      sessionId: this._connectedSessionId,
      records: encodedRecords
    });
    return request;
  },

  ndefMakeReadOnly: function ndefMakeReadOnly(window) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:NdefMakeReadOnly", {
      requestId: requestId,
      sessionId: this._connectedSessionId
    });
    return request;
  },

  connect: function connect(window, techType) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:Connect", {
      requestId: requestId,
      sessionId: this._connectedSessionId,
      techType: techType
    });
    return request;
  },

  close: function close(window) {
    if (window == null) {
      throw Components.Exception("Can't get window object",
                                  Cr.NS_ERROR_UNEXPECTED);
    }

    let request = Services.DOMRequest.createRequest(window);
    let requestId = btoa(this.getRequestId(request));
    this._requestMap[requestId] = {win: window};

    cpmm.sendAsyncMessage("NFC:Close", {
      requestId: requestId,
      sessionId: this._connectedSessionId
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
    debug("Message received: " + JSON.stringify(message));
    switch (message.name) {
      case "NFC:TechDiscovered":
        this.handleTechDiscovered(message.json);
        break;
      case "NFC:TechLost":
        this.handleTechLost(message.json);
        break;
      case "NFC:NDEFDetailsResponse":
        this.handleNDEFDetailsResponse(message.json);
        break;
      case "NFC:NDEFReadResponse":
        this.handleNDEFReadResponse(message.json);
        break;
      case "NFC:NDEFWriteResponse":
        this.handleNDEFWriteResponse(message.json);
        break;
      case "NFC:NDEFMakeReadOnlyResponse":
        this.handleNDEFReadOnlyResponse(message.json);
        break;
      case "NFC:ConnectResponse":
        this.handleConnectResponse(message.json);
        break;
      case "NFC:CloseResponse":
        this.handleCloseResponse(message.json);
        break;
    }
  },

  // NFC Notifications
  handleTechDiscovered: function handleTechDiscovered(message) {
    debug('TechDiscovered. Update Session:');
    this._connectedSessionId = message.sessionId;
  },

  handleTechLost: function handleTechLost(message) {
    debug('TechLost. Clear session:');
    this._connectedSessionId = null;
  },

  handleNfcATagDiscoveredNofitication: function handleTechDiscovered(message) {
    debug('NfcATagDiscovered. Check for existing session:');
    this._connectedSessionId = message.sessionId;
  },

  handleNDEFDetailsResponse: function handleNDEFDetailsResponse(message) {
    debug("NDEFDetailsResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);

    if (message.sessionId != this._connectedSessionId) {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleNDEFReadResponse: function handleNDEFReadResponse(message) {
    debug("NDEFReadResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
       return; // Nothing to do in this instance.
    }
    delete this._requestMap[message.requestId];
    let result = message.content;
    let requestId = atob(message.requestId);
    let records = result.records.map(function(r) {
      r.type = atob(r.type);
      r.id = atob(r.id);
      r.payload = atob(r.payload);
      return r;
    });
    let resultA = {records: records};

    if (!resultA.records) {
      this.fireRequestError(requestId, result.status);
    } else  {
      this.fireRequestSuccess(requestId, ObjectWrapper.wrap(result, requester.win));
    }
  },

  handleNDEFWriteResponse: function handleNDEFWriteResponse(message) {
    debug("NDEFWriteResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
       debug('Returning: requester: ' + requester);
       debug('Returning: sessionId matches?: ' + (message.sessionId == this._connectedSessionId));
       debug('Returning: sessionId matches? sessionId: ' + message.sessionId);
       debug('Returning: sessionId matches?: saved sessionId: ' + this._connectedSessionId);
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

  handleNDEFReadOnlyResponse: function handleNDEFReadOnlyResponse(message) {
    debug("handleNDEFReadOnlyResponse(" + JSON.stringify(message) + ")");
    let requester = this._requestMap[message.requestId];
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
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
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
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
    if ((typeof requester === 'undefined') ||
        (message.sessionId != this._connectedSessionId)) {
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

    // Cleanup lost session.
    this._connectedSessionId = null;
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
