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

const DEBUG = true; // set to true to see debug messages

const NFC_CONTRACTID = "@mozilla.org/nfc;1";
const NFC_CID =
  Components.ID("{2ff24790-5e74-11e1-b86c-0800200c9a66}");

const NFC_IPC_MSG_NAMES = [
  "NFC:SendToNfcd",
  "NFC:NdefDetails",
  "NFC:NdefRead",
  "NFC:NdefWrite",
  "NFC:NdefPush",
  "NFC:NfcATagDetails",
  "NFC:NfcATagTransceive",
  "NFC:Connect",
  "NFC:Close"
];

XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageBroadcaster");
XPCOMUtils.defineLazyServiceGetter(this, "gSystemMessenger",
                                   "@mozilla.org/system-message-internal;1",
                                   "nsISystemMessagesInternal");
XPCOMUtils.defineLazyServiceGetter(this, "gSystemWorkerManager",
                                   "@mozilla.org/telephony/system-worker-manager;1",
                                   "nsISystemWorkerManager");
function Nfc() {
  this.worker = new ChromeWorker("resource://gre/modules/nfc_worker.js");
  this.worker.onerror = this.onerror.bind(this);
  this.worker.onmessage = this.onmessage.bind(this);

  for each (let msgname in NFC_IPC_MSG_NAMES) {
    ppmm.addMessageListener(msgname, this);
  }

  Services.obs.addObserver(this, "xpcom-shutdown", false);
  debug("Starting Worker");
  gSystemWorkerManager.registerNfcWorker(this.worker);
}
Nfc.prototype = {

  classID:   NFC_CID,
  classInfo: XPCOMUtils.generateCI({classID: NFC_CID,
                                    classDescription: "Nfc",
                                    interfaces: [Ci.nsIWorkerHolder,
                                                 Ci.nsINfc]}),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIWorkerHolder,
                                         Ci.nsINfc,
                                         Ci.nsIObserver]),

  _connectedSessionId: null,
  onerror: function onerror(event) {
    debug("Got an error: " + event.filename + ":" +
          event.lineno + ": " + event.message + "\n");
    event.preventDefault();
  },

  /**
   * Process the incoming message from the NFC worker
   */
  onmessage: function onmessage(event) {
    let message = event.data;
    debug("Received message: " + JSON.stringify(message));

    // TODO: Private API to post to this object which user selected activity launched in response to NFC WebActivity launch.
    // SystemMessenger: function sendMessage(aType, aMessage, aPageURI, aManifestURI), to that specific app, not broadcast.
    switch (message.type) {
      case "techDiscovered":
        this._connectedSessionId = message.sessionId;
        ppmm.broadcastAsyncMessage("NFC:TechDiscovered", message);
        gSystemMessenger.broadcastMessage("nfc-manager-tech-discovered", message);
        break;
      case "techLost":
        this._connectedSessionId = null;
        ppmm.broadcastAsyncMessage("NFC:TechLost", message);
        gSystemMessenger.broadcastMessage("nfc-manager-tech-lost", message);
        break;

      case "NDEFDetailsResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFDetailsResponse", message);
        break;
      case "NDEFReadResponse":
        debug("NFC.js ReadResponse.");
        ppmm.broadcastAsyncMessage("NFC:NDEFReadResponse", message);
        break;
      case "NDEFWriteResponse":
        debug("NFC.js WriteResponse.");
        ppmm.broadcastAsyncMessage("NFC:NDEFWriteResponse", message);
        break;
      case "NDEFPushResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFPushResponse", message);
        break;

      default:
        throw new Error("Don't know about this message type: " + message.type);
    }
  },

  // nsINfcWorker

  worker: null,

  sendToNfcd: function sendToNfcd(message) {
    this.worker.postMessage({type: "directMessage", content: message});
  },

  ndefDetails: function ndefDetails(message) {
    debug("ndefDetailsRequest message: " + JSON.stringify(message));
    var outMessage = {
      type: "NDEFDetailsRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };
    debug("ndefDetailsRequest message out: " + JSON.stringify(outMessage));

    this.worker.postMessage({type: "ndefDetails", content: outMessage});
  },

  ndefRead: function ndefRead(message) {
    debug("ndefReadRequest message: " + JSON.stringify(message));
    var outMessage = {
      type: "NDEFReadRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };

    debug("ndefReadRequest message out: " + JSON.stringify(outMessage));
    this.worker.postMessage({type: "ndefRead", content: outMessage});
  },

  ndefWrite: function ndefWrite(message) {
    var records = message.records;

    debug("ndefWriteRequest message: " + JSON.stringify(message));
    var outMessage = {
      type: "NDEFWriteRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      content: {
        records: records
      }
    };

    this.worker.postMessage({type: "ndefWrite", content: outMessage});
  },

  ndefPush: function ndefPush(message) {
    var records = message.records;

    debug("ndefPushRequest message: " + JSON.stringify(message));
    var outMessage = {
      type: "NDEFPushRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      content: {
        records: records
      }
    };

    this.worker.postMessage({type: "ndefPush", content: outMessage});
  },

  // tag read/write command message handler.
  nfcATagDetails: function nfcATagDetails(message) {
    var params = message.params;

    debug("nfcATagDetails: " + JSON.stringify(message));
    var outMessage = {
      type: "NfcATagDetailsRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };

    this.worker.postMessage({type: "nfcATagDetails", content: outMessage});
  },

  // tag read/write command message handler.
  nfcATagTransceive: function nfcATagTransceive(params) {
    var params = message.params;

    debug("nfcATagTransceive: " + JSON.stringify(message));
    var outMessage = {
      type: "NfcATagTransceiveRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      content: {
        params: params
      }
    };

    this.worker.postMessage({type: "nfcATagTransceive", content: outMessage});
  },

  // tag read/write command message handler.
  connect: function connect(message, techType) {

    debug("NFC connect: " + JSON.stringify(message));
    var outMessage = {
      type: "ConnectRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      techType: message.techType
    };

    this.worker.postMessage({type: "connect", content: outMessage});
  },

  // tag read/write command message handler.
  close: function close(message) {
    debug("NFC close: " + JSON.stringify(message));
    var outMessage = {
      type: "CloseRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };

    this.worker.postMessage({type: "close", content: outMessage});
  },

  /**
   * Process the incoming message from a content process (NfcContentHelper.js)
   */
  receiveMessage: function receiveMessage(message) {
    debug("Received '" + message.name + "' message from content process");

    if (!message.target.assertPermission("nfc")) {
      if (DEBUG) {
        debug("Nfc message " + message.name +
              " from a content process with no 'nfc' privileges.");
      }
      return null;
    }

    switch (message.name) {
      case "NFC:NdefDetails":
        this.ndefDetails(message.json);
        break;
      case "NFC:NdefRead":
        this.ndefRead(message.json);
        break;
      case "NFC:NdefWrite":
        this.ndefWrite(message.json);
        break;
      case "NFC:NdefPush":
        this.ndefPush(message.json);
        break;
      case "NFC:NfcATagDetails":
        this.nfcATagDetails(message.json);
        break;
      case "NFC:NfcATagTransceive":
        this.nfcATagTransceive(message.json);
        break;
      case "NFC:Connect":
        this.connect(message.json);
        break;
      case "NFC:Close":
        this.close(message.json);
        break;
    }
  },

  // nsIObserver

  observe: function observe(subject, topic, data) {
    switch (topic) {
      case "xpcom-shutdown":
        for each (let msgname in NFC_IPC_MSG_NAMES) {
          ppmm.removeMessageListener(msgname, this);
        }
        ppmm = null;
        Services.obs.removeObserver(this, "xpcom-shutdown");
        break;
    }
  }

};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([Nfc]);

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-*- Nfc: " + s + "\n");
  };
} else {
  debug = function (s) {};
}
