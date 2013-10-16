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

var NFC = {};
Cu.import("resource://gre/modules/nfc_consts.js", NFC);

// set to true in nfc_consts.js to see debug messages
var DEBUG = NFC.DEBUG_NFC;

let debug;
if (DEBUG) {
  debug = function (s) {
    dump("-*- Nfc: " + s + "\n");
  };
} else {
  debug = function (s) {};
}

const NFC_CONTRACTID = "@mozilla.org/nfc;1";
const NFC_CID =
  Components.ID("{2ff24790-5e74-11e1-b86c-0800200c9a66}");

const NFC_IPC_MSG_NAMES = [
  "NFC:SetSessionToken",
  "NFC:GetDetailsNDEF",
  "NFC:ReadNDEF",
  "NFC:WriteNDEF",
  "NFC:MakeReadOnlyNDEF",
  "NFC:Connect",
  "NFC:Close"
];

// NFC powerlevels must match config PDUs.
const NFC_POWER_LEVEL_DISABLED       = 0;
const NFC_POWER_LEVEL_LOW            = 1;
const NFC_POWER_LEVEL_ENABLED        = 2;

const TOPIC_MOZSETTINGS_CHANGED      = "mozsettings-changed";
const TOPIC_XPCOM_SHUTDOWN           = "xpcom-shutdown";
const SETTING_NFC_ENABLED            = "nfc.enabled";


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
XPCOMUtils.defineLazyServiceGetter(this, "UUIDGenerator",
                                    "@mozilla.org/uuid-generator;1",
                                    "nsIUUIDGenerator");

function Nfc() {
  debug("Starting Worker");
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
  this.powerLevel = NFC_POWER_LEVEL_DISABLED;
  lock.get(SETTING_NFC_ENABLED, this);
  this.sessionMap = [];
  // Maps sessionIds (that generate from nfcd) with a unique guid
  this.tokenSessionMap = {};

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
                                         Ci.nsIObserver,
                                         Ci.nsISettingsServiceCallback]),

  _connectedSessionId: null,
  _enabled: false,
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
    if (!this._enabled) {
      debug("NFC is not enabled.");
      return null;
    }

    // TODO: Private API to post to this object which user selected activity launched in response to NFC WebActivity launch.
    // SystemMessenger: function sendMessage(aType, aMessage, aPageURI, aManifestURI), to that specific app, not broadcast.
    switch (message.type) {
      case "techDiscovered":
        this._connectedSessionId = message.sessionId;
        this.tokenSessionMap[this._connectedSessionId] = UUIDGenerator.generateUUID().toString();
        // Update the upper layers with a session token (alias)
        message.sessionId = this.tokenSessionMap[this._connectedSessionId];
        gSystemMessenger.broadcastMessage("nfc-manager-tech-discovered", message);
        break;
      case "techLost":
        this._connectedSessionId = null;
        delete this.tokenSessionMap[this._connectedSessionId];
        gSystemMessenger.broadcastMessage("nfc-manager-tech-lost", message);
        break;
      case "NDEFDetailsResponse":
        this.handleNDEFDetailsResponse(message);
        break;
      case "NDEFReadResponse":
        this.handleNDEFReadResponse(message);
        break;
      case "NDEFWriteResponse":
        this.handleNDEFWriteResponse(message);
        break;
      case "NDEFMakeReadOnlyResponse":
        this.handleNDEFReadOnlyResponse(message);
        break;
      case "ConnectResponse":
        this.handleConnectResponse(message);
        break;
      case "CloseResponse":
        this.handleCloseResponse(message);
        break;
      case "ConfigResponse":
        gSystemMessenger.broadcastMessage("nfc-powerlevel-change", message);
        debug("ConfigResponse" + JSON.stringify(message));
        break;
      default:
        throw new Error("Don't know about this message type: " + message.type);
    }
  },

  // nsINfcWorker

  worker: null,
  powerLevel: NFC_POWER_LEVEL_DISABLED,

  sessionMap: null,
  tokenSessionMap: null,

  setSessionToken: function setSessionToken(message) {
    debug("setSessionToken" + JSON.stringify(message));
    // store session and event target
    this.sessionMap[message.sessionId] = null; // Need AppID
  },

  // Response Handlers from 'nfcd'

  handleNDEFDetailsResponse: function handleNDEFDetailsResponse(message) {
    debug("handleNDEFDetailsResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId );

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:NDEFDetailsResponse", message);
  },

  handleNDEFReadResponse: function handleNDEFReadResponse(message) {
    debug("handleNDEFReadResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId);

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:NDEFReadResponse", message)
  },

  handleNDEFWriteResponse: function handleNDEFWriteResponse(message) {
    debug("handleNDEFWriteResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId);

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:NDEFWriteResponse", message);
  },

  handleNDEFReadOnlyResponse: function handleNDEFReadOnlyResponse(message) {
    debug("handleNDEFReadOnlyResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId);

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:NDEFMakeReadOnlyResponse", message);
  },

  handleConnectResponse: function handleConnectResponse(message) {
    debug("handleConnectResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId);

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:ConnectResponse", message);
  },

  handleCloseResponse: function handleCloseResponse(message) {
    debug("handleCloseResponse : message.sessionId   " + message.sessionId + "this._connectedSessionId   " + this._connectedSessionId);

    message.sessionId = this.tokenSessionMap[this._connectedSessionId];
    debug("Received message: " + JSON.stringify(message));
    ppmm.broadcastAsyncMessage("NFC:CloseResponse", message);
  },

  // End of Response Handlers

  // Request Handlers to 'nfcd'

  getDetailsNDEF: function getDetailsNDEF(message) {
    let outMessage = {
      type: "NDEFDetailsRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };
    debug("NDEFDetailsRequest message out: " + JSON.stringify(outMessage));

    this.worker.postMessage({type: "getDetailsNDEF", content: outMessage});
  },

  readNDEF: function readNDEF(message) {
    let outMessage = {
      type: "NDEFReadRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };

    debug("NDEFReadRequest message out: " + JSON.stringify(outMessage));
    this.worker.postMessage({type: "readNDEF", content: outMessage});
  },

  writeNDEF: function writeNDEF(message) {
    let records = message.records;

    let outMessage = {
      type: "NDEFWriteRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      content: {
        records: records
      }
    };

    this.worker.postMessage({type: "writeNDEF", content: outMessage});
  },

  makeReadOnlyNDEF: function makeReadOnlyNDEF(message) {
    let outMessage = {
      type: "NDEFMakeReadOnlyRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };
    debug("NDEFMakeReadOnlyRequest message out: " + JSON.stringify(outMessage));

    this.worker.postMessage({type: "makeReadOnlyNDEF", content: outMessage});
  },

  // tag read/write command message handler.
  connect: function connect(message, techType) {
    let outMessage = {
      type: "ConnectRequest",
      sessionId: message.sessionId,
      requestId: message.requestId,
      techType: message.techType
    };

    this.worker.postMessage({type: "connect", content: outMessage});
  },

  // tag read/write command message handler.
  close: function close(message) {
    let outMessage = {
      type: "CloseRequest",
      sessionId: message.sessionId,
      requestId: message.requestId
    };

    this.worker.postMessage({type: "close", content: outMessage});
  },

  // End of Request Handlers

  /**
   * Process the incoming message from a content process (NfcContentHelper.js)
   */
  receiveMessage: function receiveMessage(message) {
    debug("Received '" + JSON.stringify(message) + "' message from content process");

    if (!this._enabled) {
      debug("NFC is not enabled.");
      return null;
    }

    if (!message.target.assertPermission("nfc-read")) {
      debug("NFC message " + message.name +
            " from a content process with no 'nfc-read' privileges.");
      return null;
    }

    // Enforce NFC Write permissions.
    switch (message.name) {
      case "NFC:WriteNDEF":
        // Fall through...
      case "NFC:MakeReadOnlyNDEF":
        if (!message.target.assertPermission("nfc-write")) {
          debug("NFC message " + message.name +
                " from a content process with no 'nfc-write' privileges.");
          return null;
        }
        break;
    }

    // Check the sessionId except for message(s) : "NFC:SetSessionToken"
    switch (message.name) {
       case "NFC:SetSessionToken":
         break;
       default:
         if (message.json.sessionId !== this.tokenSessionMap[this._connectedSessionId]) {
           debug("Invalid Session : " + message.sessionId + " Expected Session: " +
                 this._connectedSessionId);
           return;
         }

         debug("Received Message: " + message.name + "Session Token: " +
               this.tokenSessionMap[this._connectedSessionId]);
    }

    switch (message.name) {
      case "NFC:SetSessionToken":
        this.setSessionToken(message.json);
        break;
      case "NFC:GetDetailsNDEF":
        this.getDetailsNDEF(message.json);
        break;
      case "NFC:ReadNDEF":
        this.readNDEF(message.json);
        break;
      case "NFC:WriteNDEF":
        this.writeNDEF(message.json);
        break;
      case "NFC:MakeReadOnlyNDEF":
        this.makeReadOnlyNDEF(message.json);
        break;
      case "NFC:Connect":
        this.connect(message.json);
        break;
      case "NFC:Close":
        this.close(message.json);
        break;
      default:
        debug("UnSupported : Message Name " + message.name);
        break;
    }
  },

  /**
   * nsISettingsServiceCallback
   */

  handle: function handle(aName, aResult) {
    switch(aName) {
      case SETTING_NFC_ENABLED:
        debug("'nfc.enabled' is now " + aResult);
        this._enabled = aResult;
        // General power setting
        if (this._enabled) {
          this.setNFCPowerConfig(NFC_POWER_LEVEL_ENABLED);
          this._enabled = true;
        } else {
          this.setNFCPowerConfig(NFC_POWER_LEVEL_DISABLED);
          this._enabled = false;
        }
      break;
    }
  },

  /**
   * nsIObserver
   */

  observe: function observe(subject, topic, data) {
    switch (topic) {
      case TOPIC_XPCOM_SHUTDOWN:
        for each (let msgname in NFC_IPC_MSG_NAMES) {
          ppmm.removeMessageListener(msgname, this);
        }
        ppmm = null;
        Services.obs.removeObserver(this, TOPIC_XPCOM_SHUTDOWN);
        break;
      case TOPIC_MOZSETTINGS_CHANGED:
        let setting = JSON.parse(data);
        if (setting) {
          let setting = JSON.parse(data);
          this.handle(setting.key, setting.value);
        }
        break;
    }
  },

  /**
   * NFC Config API. Properties is a set of name value pairs.
   */
  setNFCPowerConfig: function setNFCPowerConfig(powerLevel) {
    debug("NFC setNFCPowerConfig: " + powerLevel);
    this.powerLevel = powerLevel;
    // Just one param for now.
    this.setConfig({powerLevel: powerLevel});
  },

  setConfig: function setConfig(prop) {
    // Add to property set. -1 if no change.
    debug("setConfig...");
    let configset = {
      powerLevel: prop.powerLevel
    };
    let outMessage = {
      type: "ConfigRequest",
      powerLevel: prop.powerLevel
    };
    debug("OutMessage: " + JSON.stringify(outMessage));
    this.worker.postMessage({type: "configRequest", content: outMessage});
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([Nfc]);
