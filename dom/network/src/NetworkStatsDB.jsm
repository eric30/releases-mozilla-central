/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ['NetworkStatsDB'];

const DEBUG = false;
function debug(s) { dump("-*- NetworkStatsDB: " + s + "\n"); }

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/IndexedDBHelper.jsm");
Cu.importGlobalProperties(["indexedDB"]);

const DB_NAME = "net_stats";
const DB_VERSION = 4;
const STORE_NAME = "net_stats";

// Constant defining the maximum values allowed per interface. If more, older
// will be erased.
const VALUES_MAX_LENGTH = 6 * 30;

// Constant defining the rate of the samples. Daily.
const SAMPLE_RATE = 1000 * 60 * 60 * 24;

this.NetworkStatsDB = function NetworkStatsDB() {
  if (DEBUG) {
    debug("Constructor");
  }
  this.initDBHelper(DB_NAME, DB_VERSION, [STORE_NAME]);
}

NetworkStatsDB.prototype = {
  __proto__: IndexedDBHelper.prototype,

  dbNewTxn: function dbNewTxn(txn_type, callback, txnCb) {
    function successCb(result) {
      txnCb(null, result);
    }
    function errorCb(error) {
      txnCb(error, null);
    }
    return this.newTxn(txn_type, STORE_NAME, callback, successCb, errorCb);
  },

  upgradeSchema: function upgradeSchema(aTransaction, aDb, aOldVersion, aNewVersion) {
    if (DEBUG) {
      debug("upgrade schema from: " + aOldVersion + " to " + aNewVersion + " called!");
    }
    let db = aDb;
    let objectStore;
    for (let currVersion = aOldVersion; currVersion < aNewVersion; currVersion++) {
      if (currVersion == 0) {
        /**
         * Create the initial database schema.
         */

        objectStore = db.createObjectStore(STORE_NAME, { keyPath: ["connectionType", "timestamp"] });
        objectStore.createIndex("connectionType", "connectionType", { unique: false });
        objectStore.createIndex("timestamp", "timestamp", { unique: false });
        objectStore.createIndex("rxBytes", "rxBytes", { unique: false });
        objectStore.createIndex("txBytes", "txBytes", { unique: false });
        objectStore.createIndex("rxTotalBytes", "rxTotalBytes", { unique: false });
        objectStore.createIndex("txTotalBytes", "txTotalBytes", { unique: false });
        if (DEBUG) {
          debug("Created object stores and indexes");
        }
      } else if (currVersion == 2) {
        // In order to support per-app traffic data storage, the original
        // objectStore needs to be replaced by a new objectStore with new
        // key path ("appId") and new index ("appId").
        // Also, since now networks are identified by their
        // [networkId, networkType] not just by their connectionType,
        // to modify the keyPath is mandatory to delete the object store
        // and create it again. Old data is going to be deleted because the
        // networkId for each sample can not be set.
        db.deleteObjectStore(STORE_NAME);

        objectStore = db.createObjectStore(STORE_NAME, { keyPath: ["appId", "network", "timestamp"] });
        objectStore.createIndex("appId", "appId", { unique: false });
        objectStore.createIndex("network", "network", { unique: false });
        objectStore.createIndex("networkType", "networkType", { unique: false });
        objectStore.createIndex("timestamp", "timestamp", { unique: false });
        objectStore.createIndex("rxBytes", "rxBytes", { unique: false });
        objectStore.createIndex("txBytes", "txBytes", { unique: false });
        objectStore.createIndex("rxTotalBytes", "rxTotalBytes", { unique: false });
        objectStore.createIndex("txTotalBytes", "txTotalBytes", { unique: false });

        if (DEBUG) {
          debug("Created object stores and indexes for version 3");
        }
      } else if (currVersion == 3) {
        // Delete redundent indexes (leave "network" only).
        objectStore = aTransaction.objectStore(STORE_NAME);
        if (objectStore.indexNames.contains("appId")) {
          objectStore.deleteIndex("appId");
        }
        if (objectStore.indexNames.contains("networkType")) {
          objectStore.deleteIndex("networkType");
        }
        if (objectStore.indexNames.contains("timestamp")) {
          objectStore.deleteIndex("timestamp");
        }
        if (objectStore.indexNames.contains("rxBytes")) {
          objectStore.deleteIndex("rxBytes");
        }
        if (objectStore.indexNames.contains("txBytes")) {
          objectStore.deleteIndex("txBytes");
        }
        if (objectStore.indexNames.contains("rxTotalBytes")) {
          objectStore.deleteIndex("rxTotalBytes");
        }
        if (objectStore.indexNames.contains("txTotalBytes")) {
          objectStore.deleteIndex("txTotalBytes");
        }

        if (DEBUG) {
          debug("Deleted redundent indexes for version 4");
        }
      }
    }
  },

  importData: function importData(aStats) {
    let stats = { appId:        aStats.appId,
                  network:      [aStats.networkId, aStats.networkType],
                  timestamp:    aStats.timestamp,
                  rxBytes:      aStats.rxBytes,
                  txBytes:      aStats.txBytes,
                  rxTotalBytes: aStats.rxTotalBytes,
                  txTotalBytes: aStats.txTotalBytes };

    return stats;
  },

  exportData: function exportData(aStats) {
    let stats = { appId:        aStats.appId,
                  networkId:    aStats.network[0],
                  networkType:  aStats.network[1],
                  timestamp:    aStats.timestamp,
                  rxBytes:      aStats.rxBytes,
                  txBytes:      aStats.txBytes,
                  rxTotalBytes: aStats.rxTotalBytes,
                  txTotalBytes: aStats.txTotalBytes };

    return stats;
  },

  normalizeDate: function normalizeDate(aDate) {
    // Convert to UTC according to timezone and
    // filter timestamp to get SAMPLE_RATE precission
    let timestamp = aDate.getTime() - aDate.getTimezoneOffset() * 60 * 1000;
    timestamp = Math.floor(timestamp / SAMPLE_RATE) * SAMPLE_RATE;
    return timestamp;
  },

  saveStats: function saveStats(aStats, aResultCb) {
    let timestamp = this.normalizeDate(aStats.date);

    let stats = { appId:        aStats.appId,
                  networkId:    aStats.networkId,
                  networkType:  aStats.networkType,
                  timestamp:    timestamp,
                  rxBytes:      (aStats.appId == 0) ? 0 : aStats.rxBytes,
                  txBytes:      (aStats.appId == 0) ? 0 : aStats.txBytes,
                  rxTotalBytes: (aStats.appId == 0) ? aStats.rxBytes : 0,
                  txTotalBytes: (aStats.appId == 0) ? aStats.txBytes : 0 };

    stats = this.importData(stats);

    this.dbNewTxn("readwrite", function(aTxn, aStore) {
      if (DEBUG) {
        debug("Filtered time: " + new Date(timestamp));
        debug("New stats: " + JSON.stringify(stats));
      }

    let request = aStore.index("network").openCursor(stats.network, "prev");
      request.onsuccess = function onsuccess(event) {
        let cursor = event.target.result;
        if (!cursor) {
          // Empty, so save first element.

          // There could be a time delay between the point when the network
          // interface comes up and the point when the database is initialized.
          // In this short interval some traffic data are generated but are not
          // registered by the first sample.
          if (stats.appId == 0) {
            stats.rxBytes = stats.rxTotalBytes;
            stats.txBytes = stats.txTotalBytes;
          }

          this._saveStats(aTxn, aStore, stats);
          return;
        }

        if (stats.appId != cursor.value.appId) {
          cursor.continue();
          return;
        }

        // There are old samples
        if (DEBUG) {
          debug("Last value " + JSON.stringify(cursor.value));
        }

        // Remove stats previous to now - VALUE_MAX_LENGTH
        this._removeOldStats(aTxn, aStore, stats.appId, stats.network, stats.timestamp);

        // Process stats before save
        this._processSamplesDiff(aTxn, aStore, cursor, stats);
      }.bind(this);
    }.bind(this), aResultCb);
  },

  /*
   * This function check that stats are saved in the database following the sample rate.
   * In this way is easier to find elements when stats are requested.
   */
  _processSamplesDiff: function _processSamplesDiff(aTxn, aStore, aLastSampleCursor, aNewSample) {
    let lastSample = aLastSampleCursor.value;

    // Get difference between last and new sample.
    let diff = (aNewSample.timestamp - lastSample.timestamp) / SAMPLE_RATE;
    if (diff % 1) {
      // diff is decimal, so some error happened because samples are stored as a multiple
      // of SAMPLE_RATE
      aTxn.abort();
      throw new Error("Error processing samples");
    }

    if (DEBUG) {
      debug("New: " + aNewSample.timestamp + " - Last: " +
            lastSample.timestamp + " - diff: " + diff);
    }

    // If the incoming data is obtained from netd (|newSample.appId| is 0),
    // the new |txBytes|/|rxBytes| is assigend by the differnce between the new
    // |txTotalBytes|/|rxTotalBytes| and the last |txTotalBytes|/|rxTotalBytes|.
    // Else, the incoming data is per-app data (|newSample.appId| is not 0),
    // the |txBytes|/|rxBytes| is directly the new |txBytes|/|rxBytes|.
    if (aNewSample.appId == 0) {
      let rxDiff = aNewSample.rxTotalBytes - lastSample.rxTotalBytes;
      let txDiff = aNewSample.txTotalBytes - lastSample.txTotalBytes;
      if (rxDiff < 0 || txDiff < 0) {
        rxDiff = aNewSample.rxTotalBytes;
        txDiff = aNewSample.txTotalBytes;
      }
      aNewSample.rxBytes = rxDiff;
      aNewSample.txBytes = txDiff;
    }

    if (diff == 1) {
      // New element.

      // If the incoming data is per-data data, new |rxTotalBytes|/|txTotalBytes|
      // needs to be obtained by adding new |rxBytes|/|txBytes| to last
      // |rxTotalBytes|/|txTotalBytes|.
      if (aNewSample.appId != 0) {
        aNewSample.rxTotalBytes = aNewSample.rxBytes + lastSample.rxTotalBytes;
        aNewSample.txTotalBytes = aNewSample.txBytes + lastSample.txTotalBytes;
      }

      this._saveStats(aTxn, aStore, aNewSample);
      return;
    }
    if (diff > 1) {
      // Some samples lost. Device off during one or more samplerate periods.
      // Time or timezone changed
      // Add lost samples with 0 bytes and the actual one.
      if (diff > VALUES_MAX_LENGTH) {
        diff = VALUES_MAX_LENGTH;
      }

      let data = [];
      for (let i = diff - 2; i >= 0; i--) {
        let time = aNewSample.timestamp - SAMPLE_RATE * (i + 1);
        let sample = { appId:        aNewSample.appId,
                       network:      aNewSample.network,
                       timestamp:    time,
                       rxBytes:      0,
                       txBytes:      0,
                       rxTotalBytes: lastSample.rxTotalBytes,
                       txTotalBytes: lastSample.txTotalBytes };

        data.push(sample);
      }

      data.push(aNewSample);
      this._saveStats(aTxn, aStore, data);
      return;
    }
    if (diff == 0 || diff < 0) {
      // New element received before samplerate period.
      // It means that device has been restarted (or clock / timezone change).
      // Update element.

      // If diff < 0, clock or timezone changed back. Place data in the last sample.

      lastSample.rxBytes += aNewSample.rxBytes;
      lastSample.txBytes += aNewSample.txBytes;

      // If incoming data is obtained from netd, last |rxTotalBytes|/|txTotalBytes|
      // needs to get updated by replacing the new |rxTotalBytes|/|txTotalBytes|.
      if (aNewSample.appId == 0) {
        lastSample.rxTotalBytes = aNewSample.rxTotalBytes;
        lastSample.txTotalBytes = aNewSample.txTotalBytes;
      } else {
        // Else, the incoming data is per-app data, old |rxTotalBytes|/
        // |txTotalBytes| needs to get updated by adding the new
        // |rxBytes|/|txBytes| to last |rxTotalBytes|/|txTotalBytes|.
        lastSample.rxTotalBytes += aNewSample.rxBytes;
        lastSample.txTotalBytes += aNewSample.txBytes;
      }
      if (DEBUG) {
        debug("Update: " + JSON.stringify(lastSample));
      }
      let req = aLastSampleCursor.update(lastSample);
    }
  },

  _saveStats: function _saveStats(aTxn, aStore, aNetworkStats) {
    if (DEBUG) {
      debug("_saveStats: " + JSON.stringify(aNetworkStats));
    }

    if (Array.isArray(aNetworkStats)) {
      let len = aNetworkStats.length - 1;
      for (let i = 0; i <= len; i++) {
        aStore.put(aNetworkStats[i]);
      }
    } else {
      aStore.put(aNetworkStats);
    }
  },

  _removeOldStats: function _removeOldStats(aTxn, aStore, aAppId, aNetwork, aDate) {
    // Callback function to remove old items when new ones are added.
    let filterDate = aDate - (SAMPLE_RATE * VALUES_MAX_LENGTH - 1);
    let lowerFilter = [aAppId, aNetwork, 0];
    let upperFilter = [aAppId, aNetwork, filterDate];
    let range = IDBKeyRange.bound(lowerFilter, upperFilter, false, false);
    let lastSample = null;
    let self = this;

    aStore.openCursor(range).onsuccess = function(event) {
      var cursor = event.target.result;
      if (cursor) {
        lastSample = cursor.value;
        cursor.delete();
        cursor.continue();
        return;
      }

      // If all samples for a network are removed, an empty sample
      // has to be saved to keep the totalBytes in order to compute
      // future samples because system counters are not set to 0.
      // Thus, if there are no samples left, the last sample removed
      // will be saved again after setting its bytes to 0.
      let request = aStore.index("network").openCursor(aNetwork);
      request.onsuccess = function onsuccess(event) {
        let cursor = event.target.result;
        if (!cursor && lastSample != null) {
          let timestamp = new Date();
          timestamp = self.normalizeDate(timestamp);
          lastSample.timestamp = timestamp;
          lastSample.rxBytes = 0;
          lastSample.txBytes = 0;
          self._saveStats(aTxn, aStore, lastSample);
        }
      };
    };
  },

  clearInterfaceStats: function clearInterfaceStats(aNetwork, aResultCb) {
    let network = [aNetwork.id, aNetwork.type];
    let self = this;

    // Clear and save an empty sample to keep sync with system counters
    this.dbNewTxn("readwrite", function(aTxn, aStore) {
      let sample = null;
      let request = aStore.index("network").openCursor(network, "prev");
      request.onsuccess = function onsuccess(event) {
        let cursor = event.target.result;
        if (cursor) {
          if (!sample) {
            sample = cursor.value;
          }

          cursor.delete();
          cursor.continue();
          return;
        }

        if (sample) {
          let timestamp = new Date();
          timestamp = self.normalizeDate(timestamp);
          sample.timestamp = timestamp;
          sample.appId = 0;
          sample.rxBytes = 0;
          sample.txBytes = 0;

          self._saveStats(aTxn, aStore, sample);
        }
      };
    }, aResultCb);
  },

  clearStats: function clearStats(aNetworks, aResultCb) {
    let index = 0;
    let stats = [];
    let self = this;

    let callback = function(aError, aResult) {
      index++;

      if (!aError && index < aNetworks.length) {
        self.clearInterfaceStats(aNetworks[index], callback);
        return;
      }

      aResultCb(aError, aResult);
    };

    if (!aNetworks[index]) {
      aResultCb(null, true);
      return;
    }
    this.clearInterfaceStats(aNetworks[index], callback);
  },

  find: function find(aResultCb, aNetwork, aStart, aEnd, aAppId, aManifestURL) {
    let offset = (new Date()).getTimezoneOffset() * 60 * 1000;
    let start = this.normalizeDate(aStart);
    let end = this.normalizeDate(aEnd);

    if (DEBUG) {
      debug("Find samples for appId: " + aAppId + " network " +
            JSON.stringify(aNetwork) + " from " + start + " until " + end);
      debug("Start time: " + new Date(start));
      debug("End time: " + new Date(end));
    }

    this.dbNewTxn("readonly", function(aTxn, aStore) {
      let network = [aNetwork.id, aNetwork.type];
      let lowerFilter = [aAppId, network, start];
      let upperFilter = [aAppId, network, end];
      let range = IDBKeyRange.bound(lowerFilter, upperFilter, false, false);

      let data = [];

      if (!aTxn.result) {
        aTxn.result = {};
      }

      let request = aStore.openCursor(range).onsuccess = function(event) {
        var cursor = event.target.result;
        if (cursor){
          data.push({ rxBytes: cursor.value.rxBytes,
                      txBytes: cursor.value.txBytes,
                      date: new Date(cursor.value.timestamp + offset) });
          cursor.continue();
          return;
        }

        // When requested samples (start / end) are not in the range of now and
        // now - VALUES_MAX_LENGTH, fill with empty samples.
        this.fillResultSamples(start + offset, end + offset, data);

        aTxn.result.manifestURL = aManifestURL;
        aTxn.result.network = aNetwork;
        aTxn.result.start = aStart;
        aTxn.result.end = aEnd;
        aTxn.result.data = data;
      }.bind(this);
    }.bind(this), aResultCb);
  },

  /*
   * Fill data array (samples from database) with empty samples to match
   * requested start / end dates.
   */
  fillResultSamples: function fillResultSamples(aStart, aEnd, aData) {
    if (aData.length == 0) {
      aData.push({ rxBytes: undefined,
                  txBytes: undefined,
                  date: new Date(aStart) });
    }

    while (aStart < aData[0].date.getTime()) {
      aData.unshift({ rxBytes: undefined,
                      txBytes: undefined,
                      date: new Date(aData[0].date.getTime() - SAMPLE_RATE) });
    }

    while (aEnd > aData[aData.length - 1].date.getTime()) {
      aData.push({ rxBytes: undefined,
                   txBytes: undefined,
                   date: new Date(aData[aData.length - 1].date.getTime() + SAMPLE_RATE) });
    }
  },

  getAvailableNetworks: function getAvailableNetworks(aResultCb) {
    this.dbNewTxn("readonly", function(aTxn, aStore) {
      if (!aTxn.result) {
        aTxn.result = [];
      }

      let request = aStore.index("network").openKeyCursor(null, "nextunique");
      request.onsuccess = function onsuccess(event) {
        let cursor = event.target.result;
        if (cursor) {
          aTxn.result.push({ id: cursor.key[0],
                             type: cursor.key[1] });
          cursor.continue();
          return;
        }
      };
    }, aResultCb);
  },

  isNetworkAvailable: function isNetworkAvailable(aNetwork, aResultCb) {
    this.dbNewTxn("readonly", function(aTxn, aStore) {
      if (!aTxn.result) {
        aTxn.result = false;
      }

      var network = [aNetwork.id, aNetwork.type];
      let request = aStore.index("network").openKeyCursor(IDBKeyRange.only(network));
      request.onsuccess = function onsuccess(event) {
        if (event.target.result) {
          aTxn.result = true;
        }
      };
    }, aResultCb);
  },

  get sampleRate () {
    return SAMPLE_RATE;
  },

  get maxStorageSamples () {
    return VALUES_MAX_LENGTH;
  },

  logAllRecords: function logAllRecords(aResultCb) {
    this.dbNewTxn("readonly", function(aTxn, aStore) {
      aStore.mozGetAll().onsuccess = function onsuccess(event) {
        aTxn.result = event.target.result;
      };
    }, aResultCb);
  },
};
