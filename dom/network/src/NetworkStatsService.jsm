/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const DEBUG = false;
function debug(s) {
  if (DEBUG) {
    dump("-*- NetworkStatsService: " + s + "\n");
  }
}

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

this.EXPORTED_SYMBOLS = ["NetworkStatsService"];

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/NetworkStatsDB.jsm");

const NET_NETWORKSTATSSERVICE_CONTRACTID = "@mozilla.org/network/netstatsservice;1";
const NET_NETWORKSTATSSERVICE_CID = Components.ID("{18725604-e9ac-488a-8aa0-2471e7f6c0a4}");

const TOPIC_INTERFACE_REGISTERED   = "network-interface-registered";
const TOPIC_INTERFACE_UNREGISTERED = "network-interface-unregistered";
const NET_TYPE_WIFI = Ci.nsINetworkInterface.NETWORK_TYPE_WIFI;
const NET_TYPE_MOBILE = Ci.nsINetworkInterface.NETWORK_TYPE_MOBILE;

// The maximum traffic amount can be saved in the |cachedAppStats|.
const MAX_CACHED_TRAFFIC = 500 * 1000 * 1000; // 500 MB

XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageListenerManager");

XPCOMUtils.defineLazyServiceGetter(this, "networkManager",
                                   "@mozilla.org/network/manager;1",
                                   "nsINetworkManager");


XPCOMUtils.defineLazyServiceGetter(this, "networkService",
                                   "@mozilla.org/network/service;1",
                                   "nsINetworkService");

XPCOMUtils.defineLazyServiceGetter(this, "appsService",
                                   "@mozilla.org/AppsService;1",
                                   "nsIAppsService");

XPCOMUtils.defineLazyServiceGetter(this, "gSettingsService",
                                   "@mozilla.org/settingsService;1",
                                   "nsISettingsService");

this.NetworkStatsService = {
  init: function() {
    debug("Service started");

    Services.obs.addObserver(this, "xpcom-shutdown", false);
    Services.obs.addObserver(this, TOPIC_INTERFACE_REGISTERED, false);
    Services.obs.addObserver(this, TOPIC_INTERFACE_UNREGISTERED, false);
    Services.obs.addObserver(this, "profile-after-change", false);

    this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);

    // Object to store network interfaces, each network interface is composed
    // by a network object (network type and network Id) and a interfaceName
    // that contains the name of the physical interface (wlan0, rmnet0, etc.).
    // The network type can be 0 for wifi or 1 for mobile. On the other hand,
    // the network id is '0' for wifi or the iccid for mobile (SIM).
    // Each networkInterface is placed in the _networks object by the index of
    // 'networkId + networkType'.
    //
    // _networks object allows to map available network interfaces at low level
    // (wlan0, rmnet0, etc.) to a network. It's not mandatory to have a
    // networkInterface per network but can't exist a networkInterface not
    // being mapped to a network.

    this._networks = Object.create(null);

    // There is no way to know a priori if wifi connection is available,
    // just when the wifi driver is loaded, but it is unloaded when
    // wifi is switched off. So wifi connection is hardcoded
    let netId = this.getNetworkId('0', NET_TYPE_WIFI);
    this._networks[netId] = { network:       { id: '0',
                                               type: NET_TYPE_WIFI },
                              interfaceName: null };

    this.messages = ["NetworkStats:Get",
                     "NetworkStats:Clear",
                     "NetworkStats:ClearAll",
                     "NetworkStats:Networks",
                     "NetworkStats:SampleRate",
                     "NetworkStats:MaxStorageAge"];

    this.messages.forEach(function(aMsgName) {
      ppmm.addMessageListener(aMsgName, this);
    }, this);

    this._db = new NetworkStatsDB();

    // Stats for all interfaces are updated periodically
    this.timer.initWithCallback(this, this._db.sampleRate,
                                Ci.nsITimer.TYPE_REPEATING_PRECISE);

    // App stats are firstly stored in the cached.
    this.cachedAppStats = Object.create(null);
    this.cachedAppStatsDate = new Date();

    this.updateQueue = [];
    this.isQueueRunning = false;
  },

  receiveMessage: function(aMessage) {
    if (!aMessage.target.assertPermission("networkstats-manage")) {
      return;
    }

    debug("receiveMessage " + aMessage.name);

    let mm = aMessage.target;
    let msg = aMessage.json;

    switch (aMessage.name) {
      case "NetworkStats:Get":
        this.getSamples(mm, msg);
        break;
      case "NetworkStats:Clear":
        this.clearInterfaceStats(mm, msg);
        break;
      case "NetworkStats:ClearAll":
        this.clearDB(mm, msg);
        break;
      case "NetworkStats:Networks":
        return this.availableNetworks();
      case "NetworkStats:SampleRate":
        // This message is sync.
        return this._db.sampleRate;
      case "NetworkStats:MaxStorageAge":
        // This message is sync.
        return this._db.maxStorageSamples * this._db.sampleRate;
    }
  },

  observe: function observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case TOPIC_INTERFACE_REGISTERED:
      case TOPIC_INTERFACE_UNREGISTERED:

        // If new interface is registered (notified from NetworkManager),
        // the stats are updated for the new interface without waiting to
        // complete the updating period.

        let network = aSubject.QueryInterface(Ci.nsINetworkInterface);
        debug("Network " + network.name + " of type " + network.type + " status change");

        let netId = this.convertNetworkInterface(network);
        if (!netId) {
          break;
        }

        this.updateStats(netId);
        break;
      case "xpcom-shutdown":
        debug("Service shutdown");

        this.messages.forEach(function(aMsgName) {
          ppmm.removeMessageListener(aMsgName, this);
        }, this);

        Services.obs.removeObserver(this, "xpcom-shutdown");
        Services.obs.removeObserver(this, "profile-after-change");
        Services.obs.removeObserver(this, TOPIC_INTERFACE_REGISTERED);
        Services.obs.removeObserver(this, TOPIC_INTERFACE_UNREGISTERED);

        this.timer.cancel();
        this.timer = null;

        // Update stats before shutdown
        this.updateAllStats();
        break;
    }
  },

  /*
   * nsITimerCallback
   * Timer triggers the update of all stats
   */
  notify: function(aTimer) {
    this.updateAllStats();
  },

  /*
   * nsINetworkStatsService
   */

  convertNetworkInterface: function(aNetwork) {
    if (aNetwork.type != NET_TYPE_MOBILE &&
        aNetwork.type != NET_TYPE_WIFI) {
      return null;
    }

    let id = '0';
    if (aNetwork.type == NET_TYPE_MOBILE) {
      if (!(aNetwork instanceof Ci.nsIRilNetworkInterface)) {
        debug("Error! Mobile network should be an nsIRilNetworkInterface!");
        return null;
      }

      let rilNetwork = aNetwork.QueryInterface(Ci.nsIRilNetworkInterface);
      id = rilNetwork.iccId;
    }

    let netId = this.getNetworkId(id, aNetwork.type);

    if (!this._networks[netId]) {
      this._networks[netId] = Object.create(null);
      this._networks[netId].network = { id: id,
                                        type: aNetwork.type };
    }

    this._networks[netId].interfaceName = aNetwork.name;
    return netId;
  },

  getNetworkId: function getNetworkId(aIccId, aNetworkType) {
    return aIccId + '' + aNetworkType;
  },

  availableNetworks: function availableNetworks() {
    let result = [];
    for (let netId in this._networks) {
      result.push(this._networks[netId].network);
    }

    return result;
  },

  /*
   * Function called from manager to get stats from database.
   * In order to return updated stats, first is performed a call to
   * updateAllStats function, which will get last stats from netd
   * and update the database.
   * Then, depending on the request (stats per appId or total stats)
   * it retrieve them from database and return to the manager.
   */
  getSamples: function getSamples(mm, msg) {
    let self = this;
    let network = msg.network;
    let netId = this.getNetworkId(network.id, network.type);

    if (!this._networks[netId]) {
      mm.sendAsyncMessage("NetworkStats:Get:Return",
                          { id: msg.id, error: "Invalid connectionType", result: null });
      return;
    }

    let appId = 0;
    let manifestURL = msg.manifestURL;
    if (manifestURL) {
      appId = appsService.getAppLocalIdByManifestURL(manifestURL);

      if (!appId) {
        mm.sendAsyncMessage("NetworkStats:Get:Return",
                            { id: msg.id, error: "Invalid manifestURL", result: null });
        return;
      }
    }

    let start = new Date(msg.start);
    let end = new Date(msg.end);

    this.updateStats(netId, function onStatsUpdated(aResult, aMessage) {
      debug("getstats for network " + network.id + " of type " + network.type);
      debug("appId: " + appId + " from manifestURL: " + manifestURL);

      this.updateCachedAppStats(function onAppStatsUpdated(aResult, aMessage) {
        self._db.find(function onStatsFound(aError, aResult) {
          mm.sendAsyncMessage("NetworkStats:Get:Return",
                              { id: msg.id, error: aError, result: aResult });
        }, network, start, end, appId, manifestURL);
      });
    }.bind(this));
  },

  clearInterfaceStats: function clearInterfaceStats(mm, msg) {
    let network = msg.network;
    let netId = this.getNetworkId(network.id, network.type);

    debug("clear stats for network " + network.id + " of type " + network.type);

    if (!this._networks[netId]) {
      mm.sendAsyncMessage("NetworkStats:Clear:Return",
                          { id: msg.id, error: "Invalid networkType", result: null });
      return;
    }

    this._db.clearInterfaceStats(network, function onDBCleared(aError, aResult) {
        mm.sendAsyncMessage("NetworkStats:Clear:Return",
                            { id: msg.id, error: aError, result: aResult });
    });
  },

  clearDB: function clearDB(mm, msg) {
    let networks = this.availableNetworks();
    this._db.clearStats(networks, function onDBCleared(aError, aResult) {
      mm.sendAsyncMessage("NetworkStats:ClearAll:Return",
                          { id: msg.id, error: aError, result: aResult });
    });
  },

  updateAllStats: function updateAllStats(aCallback) {
    // Update |cachedAppStats|.
    this.updateCachedAppStats();

    let elements = [];
    let lastElement;

    // For each connectionType create an object containning the type
    // and the 'queueIndex', the 'queueIndex' is an integer representing
    // the index of a connection type in the global queue array. So, if
    // the connection type is already in the queue it is not appended again,
    // else it is pushed in 'elements' array, which later will be pushed to
    // the queue array.
    for (let netId in this._networks) {
      lastElement = { netId: netId,
                      queueIndex: this.updateQueueIndex(netId)};

      if (lastElement.queueIndex == -1) {
        elements.push({netId: lastElement.netId, callbacks: []});
      }
    }

    if (elements.length > 0) {
      // If length of elements is greater than 0, callback is set to
      // the last element.
      elements[elements.length - 1].callbacks.push(aCallback);
      this.updateQueue = this.updateQueue.concat(elements);
    } else {
      // Else, it means that all connection types are already in the queue to
      // be updated, so callback for this request is added to
      // the element in the main queue with the index of the last 'lastElement'.
      // But before is checked that element is still in the queue because it can
      // be processed while generating 'elements' array.
      let element = this.updateQueue[lastElement.queueIndex];
      if (aCallback &&
         (!element || element.netId != lastElement.netId)) {
        aCallback();
        return;
      }

      this.updateQueue[lastElement.queueIndex].callbacks.push(aCallback);
    }

    // Call the function that process the elements of the queue.
    this.processQueue();

    if (DEBUG) {
      this.logAllRecords();
    }
  },

  updateStats: function updateStats(aNetId, aCallback) {
    // Check if the connection is in the main queue, push a new element
    // if it is not being processed or add a callback if it is.
    let index = this.updateQueueIndex(aNetId);
    if (index == -1) {
      this.updateQueue.push({netId: aNetId, callbacks: [aCallback]});
    } else {
      this.updateQueue[index].callbacks.push(aCallback);
      return;
    }

    // Call the function that process the elements of the queue.
    this.processQueue();
  },

  /*
   * Find if a connection is in the main queue array and return its
   * index, if it is not in the array return -1.
   */
  updateQueueIndex: function updateQueueIndex(aNetId) {
    return this.updateQueue.map(function(e) { return e.netId; }).indexOf(aNetId);
  },

  /*
   * Function responsible of process all requests in the queue.
   */
  processQueue: function processQueue(aResult, aMessage) {
    // If aResult is not undefined, the caller of the function is the result
    // of processing an element, so remove that element and call the callbacks
    // it has.
    if (aResult != undefined) {
      let item = this.updateQueue.shift();
      for (let callback of item.callbacks) {
        if(callback) {
          callback(aResult, aMessage);
        }
      }
    } else {
      // The caller is a function that has pushed new elements to the queue,
      // if isQueueRunning is false it means there is no processing currently being
      // done, so start.
      if (this.isQueueRunning) {
        if(this.updateQueue.length > 1) {
          return;
        }
      } else {
        this.isQueueRunning = true;
      }
    }

    // Check length to determine if queue is empty and stop processing.
    if (this.updateQueue.length < 1) {
      this.isQueueRunning = false;
      return;
    }

    // Call the update function for the next element.
    this.update(this.updateQueue[0].netId, this.processQueue.bind(this));
  },

  update: function update(aNetId, aCallback) {
    // Check if connection type is valid.
    if (!this._networks[aNetId]) {
      if (aCallback) {
        aCallback(false, "Invalid network " + aNetId);
      }
      return;
    }

    let interfaceName = this._networks[aNetId].interfaceName;
    debug("Update stats for " + interfaceName);

    // Request stats to NetworkManager, which will get stats from netd, passing
    // 'networkStatsAvailable' as a callback.
    if (interfaceName) {
      networkService.getNetworkInterfaceStats(interfaceName,
                this.networkStatsAvailable.bind(this, aCallback, aNetId));
      return;
    }

    if (aCallback) {
      aCallback(true, "ok");
    }
  },

  /*
   * Callback of request stats. Store stats in database.
   */
  networkStatsAvailable: function networkStatsAvailable(aCallback, aNetId,
                                                        aResult, aRxBytes,
                                                        aTxBytes, aDate) {
    if (!aResult) {
      if (aCallback) {
        aCallback(false, "Netd IPC error");
      }
      return;
    }

    let stats = { appId:       0,
                  networkId:   this._networks[aNetId].network.id,
                  networkType: this._networks[aNetId].network.type,
                  date:        aDate,
                  rxBytes:     aTxBytes,
                  txBytes:     aRxBytes };

    debug("Update stats for: " + JSON.stringify(stats));

    this._db.saveStats(stats, function onSavedStats(aError, aResult) {
      if (aCallback) {
        if (aError) {
          aCallback(false, aError);
          return;
        }

        aCallback(true, "OK");
      }
    });
  },

  /*
   * Function responsible for receiving per-app stats.
   */
  saveAppStats: function saveAppStats(aAppId, aNetwork, aTimeStamp, aRxBytes, aTxBytes, aCallback) {
    let netId = this.convertNetworkInterface(aNetwork);
    if (!netId) {
      if (aCallback) {
        aCallback.notify(false, "Invalid network type");
      }
      return;
    }

    debug("saveAppStats: " + aAppId + " " + netId + " " +
          aTimeStamp + " " + aRxBytes + " " + aTxBytes);

    // Check if |aAppId| and |aConnectionType| are valid.
    if (!aAppId || !this._networks[netId]) {
      debug("Invalid appId or network interface");
      return;
    }

    let stats = { appId: aAppId,
                  networkId: this._networks[netId].network.id,
                  networkType: this._networks[netId].network.type,
                  date: new Date(aTimeStamp),
                  rxBytes: aRxBytes,
                  txBytes: aTxBytes };

    // Generate an unique key from |appId| and |connectionType|,
    // which is used to retrieve data in |cachedAppStats|.
    let key = stats.appId + "" + netId;

    // |cachedAppStats| only keeps the data with the same date.
    // If the incoming date is different from |cachedAppStatsDate|,
    // both |cachedAppStats| and |cachedAppStatsDate| will get updated.
    let diff = (this._db.normalizeDate(stats.date) -
                this._db.normalizeDate(this.cachedAppStatsDate)) /
               this._db.sampleRate;
    if (diff != 0) {
      this.updateCachedAppStats(function onUpdated(success, message) {
        this.cachedAppStatsDate = stats.date;
        this.cachedAppStats[key] = stats;

        if (!aCallback) {
          return;
        }

        if (!success) {
          aCallback.notify(false, message);
          return;
        }

        aCallback.notify(true, "ok");
      }.bind(this));

      return;
    }

    // Try to find the matched row in the cached by |appId| and |connectionType|.
    // If not found, save the incoming data into the cached.
    let appStats = this.cachedAppStats[key];
    if (!appStats) {
      this.cachedAppStats[key] = stats;
      return;
    }

    // Find matched row, accumulate the traffic amount.
    appStats.rxBytes += stats.rxBytes;
    appStats.txBytes += stats.txBytes;

    // If new rxBytes or txBytes exceeds MAX_CACHED_TRAFFIC
    // the corresponding row will be saved to indexedDB.
    // Then, the row will be removed from the cached.
    if (appStats.rxBytes > MAX_CACHED_TRAFFIC ||
        appStats.txBytes > MAX_CACHED_TRAFFIC) {
      this._db.saveStats(appStats,
        function (error, result) {
          debug("Application stats inserted in indexedDB");
        }
      );
      delete this.cachedAppStats[key];
    }
  },

  updateCachedAppStats: function updateCachedAppStats(aCallback) {
    debug("updateCachedAppStats: " + this.cachedAppStatsDate);

    let stats = Object.keys(this.cachedAppStats);
    if (stats.length == 0) {
      // |cachedAppStats| is empty, no need to update.
      if (aCallback) {
        aCallback(true, "no need to update");
      }

      return;
    }

    let index = 0;
    this._db.saveStats(this.cachedAppStats[stats[index]],
      function onSavedStats(error, result) {
        if (DEBUG) {
          debug("Application stats inserted in indexedDB");
        }

        // Clean up the |cachedAppStats| after updating.
        if (index == stats.length - 1) {
          this.cachedAppStats = Object.create(null);

          if (!aCallback) {
            return;
          }

          if (error) {
            aCallback(false, error);
            return;
          }

          aCallback(true, "ok");
          return;
        }

        // Update is not finished, keep updating.
        index += 1;
        this._db.saveStats(this.cachedAppStats[stats[index]],
                           onSavedStats.bind(this, error, result));
      }.bind(this));
  },

  get maxCachedTraffic () {
    return MAX_CACHED_TRAFFIC;
  },

  logAllRecords: function logAllRecords() {
    this._db.logAllRecords(function onResult(aError, aResult) {
      if (aError) {
        debug("Error: " + aError);
        return;
      }

      debug("===== LOG =====");
      debug("There are " + aResult.length + " items");
      debug(JSON.stringify(aResult));
    });
  },
};

NetworkStatsService.init();
