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

importScripts("systemlibs.js");

// We leave this as 'undefined' instead of setting it to 'false'. That
// way an outer scope can define it to 'true' (e.g. for testing purposes)
// without us overriding that here.
let DEBUG = true;

/**
 * Provide a high-level API representing NFC capabilities. This is
 * where JSON is sent and received from and translated into API calls.
 * For the most part, this object is pretty boring as it simply translates
 * between method calls and NFC JSON. Somebody's gotta do the job...
 */
let Nfc = {

  /**
   * Process incoming data.
   *
   * @param incoming
   *        nfc JSON message
   */
  processIncoming: function processIncoming(incoming) {
    if (DEBUG) {
      debug("Received: " + incoming);
    }
    let message = JSON.parse(incoming);
    this.sendDOMMessage(message);
  },


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

  directMessage: function directMessage(message) {
    postNfcMessage(message.content);
  },

  writeNdefTag: function writeNdefTag(message) {
    postNfcMessage(JSON.stringify(message.content));
  },

  ndefPush: function ndefPush(message) {
    postNfcMessage(JSON.stringify(message.content));
  },

  /**
   * Send messages to the main UI thread.
   */
  sendDOMMessage: function sendDOMMessage(message) {
    postMessage(message);
  }

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

function onNfcMessage(data) {
  Nfc.processIncoming(data);
};

onmessage = function onmessage(event) {
  Nfc.handleDOMMessage(event.data);
};

onerror = function onerror(event) {
  debug("OnError: event: " + JSON.stringify(event));
  debug("NFC Worker error " + event.message + "\n");
};   
