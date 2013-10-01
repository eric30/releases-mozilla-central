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
  "NFC:NdefDetails",
  "NFC:NdefRead",
  "NFC:NdefWrite",
  "NFC:NdefMakeReadOnly",
  "NFC:Connect",
  "NFC:Close"
];

const TOPIC_MOZSETTINGS_CHANGED      = "mozsettings-changed";
const TOPIC_XPCOM_SHUTDOWN           = "xpcom-shutdown";


XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageBroadcaster");
XPCOMUtils.defineLazyServiceGetter(this, "gSystemMessenger",
                                   "@mozilla.org/system-message-internal;1",
                                   "nsISystemMessagesInternal");
XPCOMUtils.defineLazyServiceGetter(this, "gSystemWorkerManager",
                                   "@mozilla.org/telephony/system-worker-manager;1",
                                   "nsISystemWorkerManager");
XPCOMUtils.defineLazyServiceGetter(this, "gSettingsService",
                                   "@mozilla.org/settingsService;1",
                                   "nsISettingsService");


function Nfc() {
  this.worker = new ChromeWorker("resource://gre/modules/nfc_worker.js");
  this.worker.onerror = this.onerror.bind(this);
  this.worker.onmessage = this.onmessage.bind(this);

  for each (let msgname in NFC_IPC_MSG_NAMES) {
    ppmm.addMessageListener(msgname, this);
  }

  Services.obs.addObserver(this, TOPIC_MOZSETTINGS_CHANGED, false);
  Services.obs.addObserver(this, TOPIC_XPCOM_SHUTDOWN, false);

  let lock = gSettingsService.createLock();
  lock.get("nfc.powerlevel", this);
  this.powerlevel = 0; // default to off (FIXME: add get preference)

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
    if (this.powerlevel < 1) {
      debug("Nfc is not enabled.");
      return null;
    }


    // TODO: Private API to post to this object which user selected activity launched in response to NFC WebActivity launch.
    // SystemMessenger: function sendMessage(aType, aMessage, aPageURI, aManifestURI), to that specific app, not broadcast.
    switch (message.type) {
      case "techDiscovered":
        this._connectedSessionId = message.sessionId;
        gSystemMessenger.broadcastMessage("nfc-manager-tech-discovered", message);
        ppmm.broadcastAsyncMessage("NFC:TechDiscovered", message);
        break;
      case "techLost":
        this._connectedSessionId = null;
        gSystemMessenger.broadcastMessage("nfc-manager-tech-lost", message);
        ppmm.broadcastAsyncMessage("NFC:TechLost", message);
        break;
      case "NDEFDetailsResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFDetailsResponse", message);
        break;
      case "NDEFReadResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFReadResponse", message);
        break;
      case "NDEFWriteResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFWriteResponse", message);
        break;
      case "NDEFMakeReadOnlyResponse":
        ppmm.broadcastAsyncMessage("NFC:NDEFMakeReadOnlyResponse", message);
        break;
      case "ConnectResponse":
        ppmm.broadcastAsyncMessage("NFC:ConnectResponse", message);
        break;
      case "CloseResponse":
        ppmm.broadcastAsyncMessage("NFC:CloseResponse", message);
        break;
      case "ConfigResponse":
        // Config changes. No notification.
        debug("ConfigResponse" + JSON.stringify(message));
        break;

      default:
        throw new Error("Don't know about this message type: " + message.type);
    }
  },

  // nsINfcWorker

  worker: null,
  powerlevel: 0,

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

  ndefMakeReadOnly: function ndefMakeReadOnly(message) {
    debug("ndefMakeReadOnlyRequest message: " + JSON.stringify(message));
    var outMessage = {
      type: "NDEFMakeReadOnlyRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };
    debug("ndefMakeReadOnlyRequest message out: " + JSON.stringify(outMessage));

    this.worker.postMessage({type: "ndefMakeReadOnly", content: outMessage});
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

    if (this.powerlevel < 1) {
      debug("Nfc is not enabled.");
      return null;
    }

    if (!message.target.assertPermission("nfc-read")) {
      if (DEBUG) {
        debug("Nfc message " + message.name +
              " from a content process with no 'nfc-read' privileges.");
      }
      return null;
    }

    // Enforce NFC Write permissions.
    switch (message.name) {
      case "NFC:NdefWrite":
      case "NFC:NdefMakeReadOnly":
        if (!message.target.assertPermission("nfc-write")) {
          if (DEBUG) {
            debug("Nfc message " + message.name +
                  " from a content process with no 'nfc-write' privileges.");
          }
          return null;
        }
        break;
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
      case "NFC:NdefMakeReadOnly":
        this.ndefMakeReadOnly(message.json);
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
      case TOPIC_XPCOM_SHUTDOWN:
        for each (let msgname in NFC_IPC_MSG_NAMES) {
          ppmm.removeMessageListener(msgname, this);
        }
        ppmm = null;
        Services.obs.removeObserver(this, "xpcom-shutdown");
        break;
      case TOPIC_MOZSETTINGS_CHANGED:
        let setting = JSON.parse(data);
        debug("Setting Changed: " + JSON.stringify(setting));
        if (setting) {
          switch(setting.key) {
            case "nfc.powerlevel":
              debug("Reached NFC powerlevel setting.");
              let powerlevel = (setting.value > 0) ? 1 : 0;
              this.setNfcPowerConfig(powerlevel);
            break;
          }
        } else {
          debug("NFC Setting bad!!!");
        }
        break;
    }
  },

  /**
   * NFC Config API. Properties is a set of name value pairs.
   */
  setNfcPowerConfig: function setNfcPowerConfig(powerlevel) {
    debug("NFC setNfcPowerConfig: " + powerlevel);
    this.powerlevel = powerlevel;
    this.setConfig({powerlevel: powerlevel});
  },

  setConfig: function setConfig(prop) {
    // Add to property set. -1 if no change.
    debug("In Config...");
    let configset = {
      powerlevel: prop.powerlevel
    };
    var outMessage = {
      type: "ConfigRequest",
      powerlevel: prop.powerlevel
    };
    debug("OutMessage: " + JSON.stringify(outMessage));
    this.worker.postMessage({type: "configRequest", content: outMessage});
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
