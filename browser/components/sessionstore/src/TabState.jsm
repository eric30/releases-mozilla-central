/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["TabState"];

const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm", this);
Cu.import("resource://gre/modules/Promise.jsm", this);
Cu.import("resource://gre/modules/Task.jsm", this);

XPCOMUtils.defineLazyModuleGetter(this, "Messenger",
  "resource:///modules/sessionstore/Messenger.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TabStateCache",
  "resource:///modules/sessionstore/TabStateCache.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TabAttributes",
  "resource:///modules/sessionstore/TabAttributes.jsm");

/**
 * Module that contains tab state collection methods.
 */
this.TabState = Object.freeze({
  setSyncHandler: function (browser, handler) {
    TabStateInternal.setSyncHandler(browser, handler);
  },

  onSwapDocShells: function (browser, otherBrowser) {
    TabStateInternal.onSwapDocShells(browser, otherBrowser);
  },

  collect: function (tab) {
    return TabStateInternal.collect(tab);
  },

  collectSync: function (tab) {
    return TabStateInternal.collectSync(tab);
  },

  clone: function (tab) {
    return TabStateInternal.clone(tab);
  },

  dropPendingCollections: function (browser) {
    TabStateInternal.dropPendingCollections(browser);
  }
});

let TabStateInternal = {
  // A map (xul:browser -> promise) that keeps track of tabs and
  // their promises when collecting tab data asynchronously.
  _pendingCollections: new WeakMap(),

  // A map (xul:browser -> handler) that maps a tab to the
  // synchronous collection handler object for that tab.
  // See SyncHandler in content-sessionStore.js.
  _syncHandlers: new WeakMap(),

  /**
   * Install the sync handler object from a given tab.
   */
  setSyncHandler: function (browser, handler) {
    this._syncHandlers.set(browser, handler);
  },

  /**
   * When a docshell swap happens, a xul:browser element will be
   * associated with a different content-sessionStore.js script
   * global. In this case, the sync handler for the element needs to
   * be swapped just like the docshell.
   */
  onSwapDocShells: function (browser, otherBrowser) {
    // Data collected while docShells have been swapped should not go into
    // the TabStateCache. Collections will most probably time out but we want
    // to make sure.
    this.dropPendingCollections(browser);
    this.dropPendingCollections(otherBrowser);

    // Make sure that one or the other of these has a sync handler,
    // and let it be |browser|.
    if (!this._syncHandlers.has(browser)) {
      [browser, otherBrowser] = [otherBrowser, browser];
      if (!this._syncHandlers.has(browser)) {
        return;
      }
    }

    // At this point, browser is guaranteed to have a sync handler,
    // although otherBrowser may not. Perform the swap.
    let handler = this._syncHandlers.get(browser);
    if (this._syncHandlers.has(otherBrowser)) {
      let otherHandler = this._syncHandlers.get(otherBrowser);
      this._syncHandlers.set(browser, otherHandler);
      this._syncHandlers.set(otherBrowser, handler);
    } else {
      this._syncHandlers.set(otherBrowser, handler);
      this._syncHandlers.delete(browser);
    }
  },

  /**
   * Collect data related to a single tab, asynchronously.
   *
   * @param tab
   *        tabbrowser tab
   *
   * @returns {Promise} A promise that will resolve to a TabData instance.
   */
  collect: function (tab) {
    if (!tab) {
      throw new TypeError("Expecting a tab");
    }

    // Don't collect if we don't need to.
    if (TabStateCache.has(tab)) {
      return Promise.resolve(TabStateCache.get(tab));
    }

    // If the tab was recently added, or if it's being restored, we
    // just collect basic data about it and skip the cache.
    if (!this._tabNeedsExtraCollection(tab)) {
      let tabData = this._collectBaseTabData(tab);
      return Promise.resolve(tabData);
    }

    let browser = tab.linkedBrowser;

    let promise = Task.spawn(function task() {
      // Collect session history data asynchronously. Also collects
      // text and scroll data.
      let history = yield Messenger.send(tab, "SessionStore:collectSessionHistory");

      // Collected session storage data asynchronously.
      let storage = yield Messenger.send(tab, "SessionStore:collectSessionStorage");

      // Collect docShell capabilities asynchronously.
      let disallow = yield Messenger.send(tab, "SessionStore:collectDocShellCapabilities");

      let pageStyle = yield Messenger.send(tab, "SessionStore:collectPageStyle");

      // Collect basic tab data, without session history and storage.
      let tabData = this._collectBaseTabData(tab);

      // Apply collected data.
      tabData.entries = history.entries;
      if ("index" in history) {
        tabData.index = history.index;
      }

      if (Object.keys(storage).length) {
        tabData.storage = storage;
      }

      if (disallow.length > 0) {
        tabData.disallow = disallow.join(",");
      }

      if (pageStyle) {
        tabData.pageStyle = pageStyle;
      }

      // If we're still the latest async collection for the given tab and
      // the cache hasn't been filled by collect() in the meantime, let's
      // fill the cache with the data we received.
      if (this._pendingCollections.get(browser) == promise) {
        TabStateCache.set(tab, tabData);
        this._pendingCollections.delete(browser);
      }

      throw new Task.Result(tabData);
    }.bind(this));

    // Save the current promise as the latest asynchronous collection that is
    // running. This will be used to check whether the collected data is still
    // valid and will be used to fill the tab state cache.
    this._pendingCollections.set(browser, promise);

    return promise;
  },

  /**
   * Collect data related to a single tab, synchronously.
   *
   * @param tab
   *        tabbrowser tab
   *
   * @returns {TabData} An object with the data for this tab.  If the
   * tab has not been invalidated since the last call to
   * collectSync(aTab), the same object is returned.
   */
  collectSync: function (tab) {
    if (!tab) {
      throw new TypeError("Expecting a tab");
    }
    if (TabStateCache.has(tab)) {
      return TabStateCache.get(tab);
    }

    let tabData = this._collectSyncUncached(tab);

    if (this._tabCachingAllowed(tab)) {
      TabStateCache.set(tab, tabData);
    }

    // Prevent all running asynchronous collections from filling the cache.
    // Every asynchronous data collection started before a collectSync() call
    // can't expect to retrieve different data than the sync call. That's why
    // we just fill the cache with the data collected from the sync call and
    // discard any data collected asynchronously.
    this.dropPendingCollections(tab.linkedBrowser);

    return tabData;
  },

  /**
   * Drop any pending calls to TabState.collect. These calls will
   * continue to run, but they won't store their results in the
   * TabStateCache.
   *
   * @param browser
   *        xul:browser
   */
  dropPendingCollections: function (browser) {
    this._pendingCollections.delete(browser);
  },

  /**
   * Collect data related to a single tab, including private data.
   * Use with caution.
   *
   * @param tab
   *        tabbrowser tab
   *
   * @returns {object} An object with the data for this tab. This data is never
   *                   cached, it will always be read from the tab and thus be
   *                   up-to-date.
   */
  clone: function (tab) {
    return this._collectSyncUncached(tab, {includePrivateData: true});
  },

  /**
   * Synchronously collect all session data for a tab. The
   * TabStateCache is not consulted, and the resulting data is not put
   * in the cache.
   */
  _collectSyncUncached: function (tab, options = {}) {
    // Collect basic tab data, without session history and storage.
    let tabData = this._collectBaseTabData(tab);

    // If we don't need any other data, return what we have.
    if (!this._tabNeedsExtraCollection(tab)) {
      return tabData;
    }

    // In multiprocess Firefox, there is a small window of time after
    // tab creation when we haven't received a sync handler object. In
    // this case the tab shouldn't have any history or cookie data, so we
    // just return the base data already collected.
    if (!this._syncHandlers.has(tab.linkedBrowser)) {
      return tabData;
    }

    let syncHandler = this._syncHandlers.get(tab.linkedBrowser);

    let includePrivateData = options && options.includePrivateData;

    let history, storage, disallow, pageStyle;
    try {
      history = syncHandler.collectSessionHistory(includePrivateData);
      storage = syncHandler.collectSessionStorage();
      disallow = syncHandler.collectDocShellCapabilities();
      pageStyle = syncHandler.collectPageStyle();
    } catch (e) {
      // This may happen if the tab has crashed.
      Cu.reportError(e);
      return tabData;
    }

    tabData.entries = history.entries;
    if ("index" in history) {
      tabData.index = history.index;
    }

    if (Object.keys(storage).length) {
      tabData.storage = storage;
    }

    if (disallow.length > 0) {
      tabData.disallow = disallow.join(",");
    }

    if (pageStyle) {
      tabData.pageStyle = pageStyle;
    }

    return tabData;
  },

  /*
   * Returns true if the xul:tab element is newly added (i.e., if it's
   * showing about:blank with no history).
   */
  _tabIsNew: function (tab) {
    let browser = tab.linkedBrowser;
    return (!browser || !browser.currentURI);
  },

  /*
   * Returns true if the xul:tab element is in the process of being
   * restored.
   */
  _tabIsRestoring: function (tab) {
    let browser = tab.linkedBrowser;
    return (browser.__SS_data && browser.__SS_tabStillLoading);
  },

  /**
   * This function returns true if we need to collect history, page
   * style, and text and scroll data from the tab. Normally we do. The
   * cases when we don't are:
   * 1. the tab is about:blank with no history, or
   * 2. the tab is waiting to be restored.
   *
   * @param tab   A xul:tab element.
   * @returns     True if the tab is in the process of being restored.
   */
  _tabNeedsExtraCollection: function (tab) {
    if (this._tabIsNew(tab)) {
      // Tab is about:blank with no history.
      return false;
    }

    if (this._tabIsRestoring(tab)) {
      // Tab is waiting to be restored.
      return false;
    }

    // Otherwise we need the extra data.
    return true;
  },

  /*
   * Returns true if we should cache the tabData for the given the
   * xul:tab element.
   */
  _tabCachingAllowed: function (tab) {
    if (this._tabIsNew(tab)) {
      // No point in caching data for newly created tabs.
      return false;
    }

    if (this._tabIsRestoring(tab)) {
      // If the tab is being restored, we just return the data being
      // restored. This data may be incomplete (if supplied by
      // setBrowserState, for example), so we don't want to cache it.
      return false;
    }

    return true;
  },

  /**
   * Collects basic tab data for a given tab.
   *
   * @param tab
   *        tabbrowser tab
   *
   * @returns {object} An object with the basic data for this tab.
   */
  _collectBaseTabData: function (tab) {
    let tabData = {entries: [], lastAccessed: tab.lastAccessed };
    let browser = tab.linkedBrowser;

    if (!browser || !browser.currentURI) {
      // can happen when calling this function right after .addTab()
      return tabData;
    }
    if (browser.__SS_data && browser.__SS_tabStillLoading) {
      // Use the data to be restored when the tab hasn't been
      // completely loaded. We clone the data, since we're updating it
      // here and the caller may update it further.
      tabData = JSON.parse(JSON.stringify(browser.__SS_data));
      if (tab.pinned)
        tabData.pinned = true;
      else
        delete tabData.pinned;
      tabData.hidden = tab.hidden;

      // If __SS_extdata is set then we'll use that since it might be newer.
      if (tab.__SS_extdata)
        tabData.extData = tab.__SS_extdata;
      // If it exists but is empty then a key was likely deleted. In that case just
      // delete extData.
      if (tabData.extData && !Object.keys(tabData.extData).length)
        delete tabData.extData;
      return tabData;
    }

    // If there is a userTypedValue set, then either the user has typed something
    // in the URL bar, or a new tab was opened with a URI to load. userTypedClear
    // is used to indicate whether the tab was in some sort of loading state with
    // userTypedValue.
    if (browser.userTypedValue) {
      tabData.userTypedValue = browser.userTypedValue;
      tabData.userTypedClear = browser.userTypedClear;
    } else {
      delete tabData.userTypedValue;
      delete tabData.userTypedClear;
    }

    if (tab.pinned)
      tabData.pinned = true;
    else
      delete tabData.pinned;
    tabData.hidden = tab.hidden;

    // Save tab attributes.
    tabData.attributes = TabAttributes.get(tab);

    // Store the tab icon.
    let tabbrowser = tab.ownerDocument.defaultView.gBrowser;
    tabData.image = tabbrowser.getIcon(tab);

    if (tab.__SS_extdata)
      tabData.extData = tab.__SS_extdata;
    else if (tabData.extData)
      delete tabData.extData;

    return tabData;
  }
};
