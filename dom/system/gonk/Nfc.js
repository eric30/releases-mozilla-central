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
XPCOMUtils.defineLazyGetter(this, "gMessageManager", function () {
  return {
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIMessageListener,
                                           Ci.nsIObserver]),

    nfc: null,

    // Manage message targets in terms of topic. Only the authorized and
    // registered contents can receive related messages.
    targetsByTopic: {},
    topics: [],

    targetMessageQueue: [],
    ready: false,

    init: function init(nfc) {
      this.nfc = nfc;

      Services.obs.addObserver(this, "xpcom-shutdown", false);
      Services.obs.addObserver(this, "system-message-listener-ready", false);
      this._registerMessageListeners();
    },

    _shutdown: function _shutdown() {
      this.nfc = null;

      Services.obs.removeObserver(this, "xpcom-shutdown");
      this._unregisterMessageListeners();
    },

    _registerMessageListeners: function _registerMessageListeners() {
      ppmm.addMessageListener("child-process-shutdown", this);
      for (let msgname of NFC_IPC_MSG_NAMES) {
        ppmm.addMessageListener(msgname, this);
      }
    },

    _unregisterMessageListeners: function _unregisterMessageListeners() {
      ppmm.removeMessageListener("child-process-shutdown", this);
      for (let msgname of NFC_IPC_MSG_NAMES) {
        ppmm.removeMessageListener(msgname, this);
      }
      ppmm = null;
    },

    _registerMessageTarget: function _registerMessageTarget(topic, target) {
      let targets = this.targetsByTopic[topic];
      if (!targets) {
        targets = this.targetsByTopic[topic] = [];
        let list = this.topics;
        if (list.indexOf(topic) == -1) {
          list.push(topic);
        }
      }

      if (targets.indexOf(target) != -1) {
        if (DEBUG) debug("Already registered this target!");
        return;
      }

      targets.push(target);
      if (DEBUG) debug("Registered :" + topic + " target: " + target);
    },

    _unregisterMessageTarget: function _unregisterMessageTarget(topic, target) {
      if (topic == null) {
        // Unregister the target for every topic when no topic is specified.
        for (let type of this.topics) {
          this._unregisterMessageTarget(type, target);
        }
        return;
      }

      // Unregister the target for a specified topic.
      let targets = this.targetsByTopic[topic];
      if (!targets) {
        return;
      }

      if (target == null) {
        if (DEBUG) debug("Unregistered all targets for the " + topic + " targets: " + targets);
        targets = [];
        let list = this.topics;
        if (DEBUG) debug("Topic List : " + list);
        if (topic !== null) {
          var index = list.indexOf(topic);
          if (index > -1) {
            list.splice(index, 1);
            if (DEBUG) debug("Updated topic list : " + list);
          }
        }
        return;
      }

      let index = targets.indexOf(target);
      if (index != -1) {
        targets.splice(index, 1);
        if (DEBUG) debug("Unregistered " + topic + " target: " + target);
      }
    },

    _enqueueTargetMessage: function _enqueueTargetMessage(topic, message, options) {
      let msg = { topic : topic,
                  message : message,
                  options : options };
      // Remove previous queued message of same message type, only one message
      // per message type is allowed in queue.
      let messageQueue = this.targetMessageQueue;
      for(let i = 0; i < messageQueue.length; i++) {
        if (messageQueue[i].message === message) {
          messageQueue.splice(i, 1);
          break;
        }
      }

      messageQueue.push(msg);
    },

    _sendTargetMessage: function _sendTargetMessage(topic, message, options) {
      if (!this.ready) {
        this._enqueueTargetMessage(topic, message, options);
        return;
      }

      let targets = this.targetsByTopic[topic];
      if (!targets) {
        return;
      }

      for (let target of targets) {
        target.sendAsyncMessage(message, options);
      }
    },

    _resendQueuedTargetMessage: function _resendQueuedTargetMessage() {
      this.ready = true;

      // Dequeue and resend messages.
      for each (let msg in this.targetMessageQueue) {
        this._sendTargetMessage(msg.topic, msg.message, msg.options);
      }
      this.targetMessageQueue = null;
    },

    /**
     * nsIMessageListener interface methods.
     */

    receiveMessage: function receiveMessage(msg) {
      if (DEBUG) debug("Received '" + msg.name + "' message from content process");
      if (msg.name == "child-process-shutdown") {
        // By the time we receive child-process-shutdown, the child process has
        // already forgotten its permissions so we need to unregister the target
        // for every permission.
        this._unregisterMessageTarget(null, msg.target);
        return null;
      }

      if (NFC_IPC_MSG_NAMES.indexOf(msg.name) != -1) {
        if (!msg.target.assertPermission("nfc-read")) {
          if (DEBUG) {
            debug("Nfc message " + msg.name +
                  " from a content process with no 'nfc-read' privileges.");
          }
          return null;
        }
      } else {
        if (DEBUG) debug("Ignoring unknown message type: " + msg.name);
        return null;
      }

      switch (msg.name) {
        case "NFC:SetSessionToken":
          this._registerMessageTarget(this.nfc.tokenSessionMap[this.nfc._connectedSessionId], msg.target);
          if (DEBUG) debug("Registering target for this SessionToken / Topic : " + this.nfc.tokenSessionMap[this.nfc._connectedSessionId]);
          return null;
        default:
          if (DEBUG) debug("Not registering target for this SessionToken / Topic : : " + msg.name);
      }

      // what do we do here? Maybe pass to Nfc.receiveMessage(msg)
      return null;
    },

    /**
     * nsIObserver interface methods.
     */

    observe: function observe(subject, topic, data) {
      switch (topic) {
        case "system-message-listener-ready":
          Services.obs.removeObserver(this, "system-message-listener-ready");
          this._resendQueuedTargetMessage();
          break;
        case "xpcom-shutdown":
          this._shutdown();
          break;
      }
    },

    sendNfcResponseMessage: function sendNfcResponseMessage(message, data) {
      if (DEBUG) debug("sendNfcResponseMessage :" + message);
      this._sendTargetMessage(this.nfc.tokenSessionMap[this.nfc._connectedSessionId], message, data);
    }
  };
});


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

  gMessageManager.init(this);
  let lock = gSettingsService.createLock();
  lock.get("nfc.powerlevel", this);
  this.powerLevel = NFC_POWER_LEVEL_DISABLED;
  lock.get(SETTING_NFC_ENABLED, this);
  this.sessionMap = [];
  // Maps sessionIds (that are generated from nfcd) with a unique guid
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
      throw new Error("NFC is not enabled.");
    }

    switch (message.type) {
      case "techDiscovered":
        this._connectedSessionId = message.sessionId;
        this.tokenSessionMap[this._connectedSessionId] = UUIDGenerator.generateUUID().toString();
        // Update the upper layers with a session token (alias)
        message.sessionId = this.tokenSessionMap[this._connectedSessionId];
        gSystemMessenger.broadcastMessage("nfc-manager-tech-discovered", message);
        break;
      case "techLost":
        gMessageManager._unregisterMessageTarget(this.tokenSessionMap[this._connectedSessionId], null);
        // Update the upper layers with a session token (alias)
        message.sessionId = this.tokenSessionMap[this._connectedSessionId];
        gSystemMessenger.broadcastMessage("nfc-manager-tech-lost", message);
        this._connectedSessionId = null;
        delete this.tokenSessionMap[this._connectedSessionId];
        break;
     case "ConfigResponse":
        gSystemMessenger.broadcastMessage("nfc-powerlevel-change", message);
        break;
      case "ConnectResponse":
        // Fall through.
      case "CloseResponse":
      case "DetailsNDEFResponse":
      case "ReadNDEFResponse":
      case "MakeReadOnlyNDEFResponse":
      case "WriteNDEFResponse":
        message.sessionId = this.tokenSessionMap[this._connectedSessionId];
        gMessageManager.sendNfcResponseMessage("NFC:" + message.type, message)
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

  /**
   * Process the incoming message from a content process (NfcContentHelper.js)
   */
  receiveMessage: function receiveMessage(message) {
    debug("Received '" + JSON.stringify(message) + "' message from content process");

    if (!this._enabled) {
      throw new Error("NFC is not enabled.");
    }

    // Enforce bare minimums for NFC permissions
    switch (message.name) {
      case "NFC:Connect": // Fall through
      case "NFC:Close":
      case "NFC:GetDetailsNDEF":
      case "NFC:ReadNDEF":
        if (!message.target.assertPermission("nfc-read")) {
          throw new Error("NFC message " + message.name +
                " from a content process with no 'nfc-read' privileges.");
          return null;
        }
        break;
      case "NFC:WriteNDEF": // Fall through
      case "NFC:MakeReadOnlyNDEF":
        if (!message.target.assertPermission("nfc-write")) {
          throw new Error("NFC message " + message.name +
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
      case "NFC:GetDetailsNDEF":
        this.worker.postMessage({type: "getDetailsNDEF", content: message.json});
        break;
      case "NFC:ReadNDEF":
        this.worker.postMessage({type: "readNDEF", content: message.json});
        break;
      case "NFC:WriteNDEF":
        this.worker.postMessage({type: "writeNDEF", content: message.json});
        break;
      case "NFC:MakeReadOnlyNDEF":
        this.worker.postMessage({type: "makeReadOnlyNDEF", content: message.json});
        break;
      case "NFC:Connect":
        this.worker.postMessage({type: "connect", content: message.json});
        break;
      case "NFC:Close":
        this.worker.postMessage({type: "close", content: message.json});
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
    if (DEBUG) debug("NFC setNFCPowerConfig: " + powerLevel);
    this.powerLevel = powerLevel;
    // Just one param for now.
    this.setConfig({powerLevel: powerLevel});
  },

  setConfig: function setConfig(prop) {
    // Add to property set. -1 if no change.
    let configset = {
      powerLevel: prop.powerLevel
    };
    let outMessage = {
      type: "ConfigRequest",
      powerLevel: prop.powerLevel
    };
    this.worker.postMessage({type: "configRequest", content: outMessage});
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([Nfc]);
