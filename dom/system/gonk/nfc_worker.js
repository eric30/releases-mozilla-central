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

importScripts("systemlibs.js", "nfc_consts.js");
importScripts("resource://gre/modules/workers/require.js");

// set to true in nfc_consts.js to see debug messages
let DEBUG = DEBUG_WORKER;

function getPaddingLen(len) {
  return (len % 4) ? (4 - len % 4) : 0;
}

let Buf = {
  __proto__: (function(){
    return require("resource://gre/modules/workers/worker_buf.js").Buf;
  })(),

  init: function init() {
    this._init();
  },

  /**
   * Process one parcel.
   */
  processParcel: function processParcel() {
    let pduType = this.readInt32();
    Nfc.handleParcel(pduType, this.mCallback, this.readAvailable);
  },

  /**
   * Start a new outgoing parcel.
   *
   * @param type
   *        Integer specifying the request type.
   * @param callback
   */
  newParcel: function newParcel(type, callback) {
    if (DEBUG) debug("New outgoing parcel of type " + type);

    if(this.mCallback) debug("Warning! Callback override :"+ type );
    /**
     * TODO: This needs to be fixed. A map of NFC_RESPONSE_XXX and RequestID
     *       needs to be maintained ?? For Generic Responses (1000) ,
     *       we may still rely on callback approach.
     */
    this.mCallback = callback;
    // We're going to leave room for the parcel size at the beginning.
    this.outgoingIndex = this.PARCEL_SIZE_SIZE;
    this.writeInt32(type);
  },

  simpleRequest: function simpleRequest(type) {
    this.newParcel(type);
    this.sendParcel();
  },

  onSendParcel: function onSendParcel(parcel) {
    postNfcMessage(parcel);
  },

  //TODO maintain callback
  mCallback: null,
};


/**
 * Provide a high-level API representing NFC capabilities. This is
 * where JSON is sent and received from and translated into API calls.
 * For the most part, this object is pretty boring as it simply translates
 * between method calls and NFC JSON. Somebody's gotta do the job...
 */
let Nfc = {
  /**
   * Handle incoming messages from the main UI thread.
   *
   * @param message
   *        Object containing the message. Messages are supposed
   */
  handleDOMMessage: function handleMessage(message) {
    if (DEBUG) debug("Received DOM message " + JSON.stringify(message));
    let method = this[message.type];
    if (typeof method != "function") {
      if (DEBUG) {
        debug("Don't know what to do with message " + JSON.stringify(message));
      }
      return;
    }
    method.call(this, message);
  },

  /**
   * Read and return NDEF data, if present.
   */
  readNDEF: function readNDEF(message) {
    let cb = function callback() {
      let erorr = Buf.readInt32();
      let sessionId = Buf.readInt32();
      let numOfRecords = Buf.readInt32();
      debug("numOfRecords="+numOfRecords);
      let records = [];
      for (let i = 0; i < numOfRecords; i++) {
        let tnf = Buf.readInt32();
        debug("tnf="+tnf.toString(16));
        let typeLength = Buf.readInt32();
        debug("typeLength="+typeLength);
        let type = Buf.readUint8Array(typeLength);
        debug("type="+type);
        let padding = getPaddingLen(typeLength);
        debug("type padding ="+padding);
        for (let i = 0; i < padding; i++) {
          Buf.readUint8();
        }

        let idLength = Buf.readInt32();
        debug("idLength="+idLength);
        let id = Buf.readUint8Array(idLength);
        debug("id="+id);
        padding = getPaddingLen(idLength);
        debug("id padding ="+padding);
        for (let i = 0; i < padding; i++) {
          Buf.readUint8();
        }

        let payloadLength = Buf.readInt32();
        debug("payloadLength="+payloadLength);
        //TODO using Uint8Array will make the payload become an object, not an array.
        let payload = [];
        for (let i = 0; i < payloadLength; i++) {
          payload.push(Buf.readUint8());
        }

        padding = getPaddingLen(payloadLength);
        debug("payload padding ="+padding);
        for (let i = 0; i < padding; i++) {
          Buf.readUint8();
        }
        debug("payload="+payload);

        records.push({tnf: tnf,
                      type: type,
                      id: id,
                      payload: payload});
      }

      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.content.records = records;
      message.type = "ReadNDEFResponse";
      this.sendDOMMessage(message);
    }

    Buf.newParcel(NFC_REQUEST_READ_NDEF, cb);
    Buf.writeInt32(message.content.sessionId);
    Buf.sendParcel();
  },

  /**
   * Write to a target that accepts NDEF formattable data
   */
  writeNDEF: function writeNDEF(message) {
    let cb = function callback() {
      debug("ndefWrite callback");
      message.type = "WriteNDEFResponse";
      let error = Buf.readInt32();
      let sessionId = Buf.readInt32();
      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.content.status = (error === 0) ? GECKO_NFC_ERROR_SUCCESS : GECKO_NFC_ERROR_GENERIC_FAILURE;
      this.sendDOMMessage(message);
    };

    debug("ndefWrite message="+JSON.stringify(message));
    Buf.newParcel(NFC_REQUEST_WRITE_NDEF, cb);
    Buf.writeInt32(message.content.sessionId);
    let records = message.content.records;
    let numRecords = records.length;
    debug("ndefWrite numRecords="+numRecords);
    Buf.writeInt32(numRecords);

    for (let i = 0; i < numRecords; i++) {
      let record = records[i];
      debug("tnf="+record.tnf);
      Buf.writeInt32(record.tnf);

      let typeLength = record.type.length;
      debug("typeLength="+typeLength);
      Buf.writeInt32(typeLength);
      for (let j = 0; j < typeLength; j++) {
        debug("type ["+j+"]=  "+record.type[j]);
        debug("type ["+j+"]=  "+record.type.charCodeAt(j));
        Buf.writeUint8(record.type.charCodeAt(j));
      }
      let padding = getPaddingLen(typeLength);
      debug("type padding ="+padding);
      for (let i = 0; i < padding; i++) {
        Buf.writeUint8(0x00);
      }

      let idLength = record.id.length;
      debug("idLength="+idLength);
      Buf.writeInt32(idLength);
      for (let j = 0; j < idLength; j++) {
        debug("id ["+j+"]=  "+record.id[j]);
        debug("id ["+j+"]=  "+record.id.charCodeAt(j));
        Buf.writeUint8(record.id.charCodeAt(j));
      }
      padding = getPaddingLen(idLength);
      debug("id padding ="+padding);
      for (let i = 0; i < padding; i++) {
        Buf.writeUint8(0x00);
      }

      let payloadLength = record.payload && record.payload.length;
      debug("payloadLength="+payloadLength);
      Buf.writeInt32(payloadLength);
      for (let j = 0; j < payloadLength; j++) {
        debug("payload ["+j+"]=  "+record.payload[j]);
        debug("payload ["+j+"]=  "+record.payload.charCodeAt(j));
        Buf.writeUint8(record.payload.charCodeAt(j));
      }
      padding = getPaddingLen(payloadLength);
      debug("payload padding ="+padding);
      for (let i = 0; i < padding; i++) {
        Buf.writeUint8(0x00);
      }

    }

    Buf.sendParcel();
  },

  /**
   * Make the NFC NDEF tag permanently read only
   */
  makeReadOnlyNDEF: function makeReadOnlyNDEF(message) {
    let cb = function callback() {
      debug("ndefWrite callback");
      message.type = "MakeReadOnlyNDEFResponse";
      let error = Buf.readInt32();
      let sessionId = Buf.readInt32();
      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.content.status = (error === 0) ? GECKO_NFC_ERROR_SUCCESS : GECKO_NFC_ERROR_GENERIC_FAILURE;
      this.sendDOMMessage(message);
    };
    Buf.newParcel(NFC_REQUEST_MAKE_NDEF_READ_ONLY, cb);
    Buf.writeInt32(message.content.sessionId);
    Buf.sendParcel();
  },

  /**
   * Retrieve metadata describing the NDEF formatted data, if present.
   */
  getDetailsNDEF: function getDetailsNDEF(message) {
    let cb = function callback() {
      debug("ndefWrite callback");
      message.type = "DetailsNDEFResponse";
      let error = Buf.readInt32();
      let sessionId = Buf.readInt32();
      let maxSupportedLength = Buf.readInt32();
      let mode = Buf.readInt32();
      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.maxSupportedLength = maxSupportedLength;
      message.mode = mode;
      message.content.status = (error === 0) ? GECKO_NFC_ERROR_SUCCESS : GECKO_NFC_ERROR_GENERIC_FAILURE;
      this.sendDOMMessage(message);
    };
    Buf.newParcel(NFC_REQUEST_GET_DETAILS, cb);
    Buf.writeInt32(message.content.sessionId);
    Buf.sendParcel();
  },


  /**
   * Open a connection to the NFC target. Request ID is required.
   */
  connect: function connect(message) {
    debug("connect message=.."+JSON.stringify(message));
    let cb = function callback() {
      message.type = "ConnectResponse";
      let error = Buf.readInt32();
      let sessionId = Buf.readInt32();
      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.content.status = (error === 0) ? GECKO_NFC_ERROR_SUCCESS : GECKO_NFC_ERROR_GENERIC_FAILURE;
      this.sendDOMMessage(message);
    };
    Buf.newParcel(NFC_REQUEST_CONNECT, cb);
    Buf.writeInt32(message.content.sessionId);
    Buf.writeInt32(message.content.techType);
    Buf.sendParcel();
  },

  /**
   * NFC Configuration
   */
  configRequest: function configRequest(message) {
    debug("config:"+JSON.stringify(message.content));
  },

  /**
   * Close connection to the NFC target. Request ID is required.
   */
  close: function close(message) {
    let cb = function callback() {
      message.type = "CloseResponse";
      let error = Buf.readInt32();
      let sessionId = Buf.readInt32();
      message.requestId = message.content.requestId;
      message.sessionId = sessionId;
      message.content.status = (error === 0) ? GECKO_NFC_ERROR_SUCCESS : GECKO_NFC_ERROR_GENERIC_FAILURE;
      this.sendDOMMessage(message);
    };

    Buf.newParcel(NFC_REQUEST_CLOSE , cb);
    Buf.writeInt32(message.content.sessionId);
    Buf.sendParcel();
  },

  handleParcel: function handleParcel(request_type, callback, length) {
    let method = this[request_type];
    if (typeof method == "function") {
      if (DEBUG) debug("Handling parcel as " + method.name);
      method.call(this, length);
    } else if (callback && typeof callback == "function") {
      callback.call(this, request_type);
      delete this.mCallback;
      this.mCallback = null;
    } else {
      debug("Unable to handle ReqType:"+request_type);
    }
  },

  /**
   * Send messages to the main UI thread.
   */
  sendDOMMessage: function sendDOMMessage(message) {
    postMessage(message);
  }
};

/**
 * Notification Handlers
 */
Nfc[NFC_NOTIFICATION_INITIALIZED] = function NFC_NOTIFICATION_INITIALIZED (length) {
  let status = Buf.readInt32();
  let majorVersion = Buf.readInt32();
  let minorVersion = Buf.readInt32();
  debug("NFC_NOTIFICATION_INITIALIZED status:"+status+" major:"+majorVersion+" minor:"+minorVersion);
};

Nfc[NFC_NOTIFICATION_TECH_DISCOVERED] = function NFC_NOTIFICATION_TECH_DISCOVERED(length) {
  debug("NFC_NOTIFICATION_TECH_DISCOVERED");
  let sessionId = Buf.readInt32();
  let num = Buf.readInt32();

  let techs = [];
  for (let i = 0; i < num; i++) {
    techs.push(NFC_TECHS[Buf.readUint8()]);
  }
  debug("techs = "+techs);
  this.sendDOMMessage({type: "techDiscovered",
                       sessionId: sessionId,
                       content: { tech: techs}
                       });
};

Nfc[NFC_NOTIFICATION_TECH_LOST] = function NFC_NOTIFICATION_TECH_LOST(length) {
  debug("NFC_NOTIFICATION_TECH_LOST");
  let sessionId = Buf.readInt32();
  debug("sessionId="+sessionId);
  this.sendDOMMessage({type: "techLost",
                       sessionId: sessionId,
                       });
};

/**
 * Global stuff.
 */

if (!this.debug) {
  // Debugging stub that goes nowhere.
  this.debug = function debug(message) {
    dump("Nfc Worker: " + message + "\n");
  };
}

// Initialize buffers. This is a separate function so that unit tests can
// re-initialize the buffers at will.
Buf.init();

function onNfcMessage(data) {
  Buf.processIncoming(data);
};

onmessage = function onmessage(event) {
  Nfc.handleDOMMessage(event.data);
};

onerror = function onerror(event) {
  debug("OnError: event: " + JSON.stringify(event));
  debug("NFC Worker error " + event.message + " " + event.filename + ":" +
        event.lineno + ":\n");
};

