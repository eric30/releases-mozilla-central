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

const DEBUG = true; // set to true to see debug messages

const NFC_CONTRACTID = "@mozilla.org/nfc;1";
const NFC_CID =
  Components.ID("{2ff24790-5e74-11e1-b86c-0800200c9a66}");

const NFC_IPC_MSG_NAMES = [
  "NFC:SendToNfcd",
  "NFC:WriteNdefTag",
  "NFC:NdefPush"
];

XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageBroadcaster");
XPCOMUtils.defineLazyServiceGetter(this, "gSystemMessenger",
                                   "@mozilla.org/system-message-internal;1",
                                   "nsISystemMessagesInternal");

function Nfc() {
  this.worker = new ChromeWorker("resource://gre/modules/nfc_worker.js");
  this.worker.onerror = this.onerror.bind(this);
  this.worker.onmessage = this.onmessage.bind(this);

  for each (let msgname in NFC_IPC_MSG_NAMES) {
    ppmm.addMessageListener(msgname, this);
  }

  Services.obs.addObserver(this, "xpcom-shutdown", false);
  debug("Starting Worker");
}
Nfc.prototype = {

  classID:   NFC_CID,
  classInfo: XPCOMUtils.generateCI({classID: NFC_CID,
                                    classDescription: "Nfc",
                                    interfaces: [Ci.nsIWorkerHolder,
                                                 Ci.nsINfc]}),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIWorkerHolder,
                                         Ci.nsINfc]),

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
    switch (message.type) {
      case "ndefDiscovered":
        ppmm.broadcastAsyncMessage("NFC:NdefDiscovered", message);
        gSystemMessenger.broadcastMessage("nfc-ndef-discovered", message);
        break;
      case "ndefDisconnected":
        ppmm.broadcastAsyncMessage("NFC:NdefDisconnected", message);
        gSystemMessenger.broadcastMessage("nfc-ndef-disconnected", message);
        break;
      case "requestStatus":
        ppmm.broadcastAsyncMessage("NFC:RequestStatus", message);
        gSystemMessenger.broadcastMessage("nfc-request-status", message);
        break;
      case "secureElementActivated":
        ppmm.broadcastAsyncMessage("NFC:SecureElementActivated", message);
        gSystemMessenger.broadcastMessage("secureelement-activated", message);
        break;
      case "secureElementDeactivated":
        ppmm.broadcastAsyncMessage("NFC:SecureElementDeactivated", message);
        gSystemMessenger.broadcastMessage("secureelement-deactivated", message);
        break;
      case "secureElementTransaction":
        ppmm.broadcastAsyncMessage("NFC:SecureElementTransaction", message);
        gSystemMessenger.broadcastMessage("secureelement-transaction", message);
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

  writeNdefTag: function writeNdefTag(message) {
    var records = message.records;
    var rid = message.requestId;

    var outMessage = {
      type: "ndefWriteRequest",
      requestId: rid,
      content: {
        records: records
      }
    };

    this.worker.postMessage({type: "writeNdefTag", content: outMessage});
  },


  ndefPush: function ndefPush(message) {
    var records = message.records;
    var rid = message.requestId;

    var outMessage = {
      type: "ndefPushRequest",
      requestId: rid,
      content: {
        records: records
      }
    };

    this.worker.postMessage({type: "ndefPush", content: outMessage});
  },

  /**
   * Process the incoming message from a content process (NfcContentHelper.js)
   */
  receiveMessage: function receiveMessage(message) {
    debug("Received '" + message.name + "' message from content process");
    switch (message.name) {
      case "NFC:SendToNfcd":
        this.sendToNfcd(message.json);
        break;
      case "NFC:WriteNdefTag":
        this.writeNdefTag(message.json);
        break;
      case "NFC:NdefPush":
        this.ndefPush(message.json);
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
