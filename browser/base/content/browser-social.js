// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// the "exported" symbols
let SocialUI,
    SocialChatBar,
    SocialFlyout,
    SocialMarks,
    SocialShare,
    SocialMenu,
    SocialToolbar,
    SocialSidebar,
    SocialStatus;

(function() {

// The minimum sizes for the auto-resize panel code.
const PANEL_MIN_HEIGHT = 100;
const PANEL_MIN_WIDTH = 330;

XPCOMUtils.defineLazyModuleGetter(this, "SharedFrame",
  "resource:///modules/SharedFrame.jsm");

XPCOMUtils.defineLazyGetter(this, "OpenGraphBuilder", function() {
  let tmp = {};
  Cu.import("resource:///modules/Social.jsm", tmp);
  return tmp.OpenGraphBuilder;
});

XPCOMUtils.defineLazyGetter(this, "DynamicResizeWatcher", function() {
  let tmp = {};
  Cu.import("resource:///modules/Social.jsm", tmp);
  return tmp.DynamicResizeWatcher;
});

XPCOMUtils.defineLazyGetter(this, "sizeSocialPanelToContent", function() {
  let tmp = {};
  Cu.import("resource:///modules/Social.jsm", tmp);
  return tmp.sizeSocialPanelToContent;
});

SocialUI = {
  // Called on delayed startup to initialize the UI
  init: function SocialUI_init() {
    Services.obs.addObserver(this, "social:ambient-notification-changed", false);
    Services.obs.addObserver(this, "social:profile-changed", false);
    Services.obs.addObserver(this, "social:frameworker-error", false);
    Services.obs.addObserver(this, "social:provider-set", false);
    Services.obs.addObserver(this, "social:providers-changed", false);
    Services.obs.addObserver(this, "social:provider-reload", false);
    Services.obs.addObserver(this, "social:provider-enabled", false);
    Services.obs.addObserver(this, "social:provider-disabled", false);

    Services.prefs.addObserver("social.sidebar.open", this, false);
    Services.prefs.addObserver("social.toast-notifications.enabled", this, false);

    gBrowser.addEventListener("ActivateSocialFeature", this._activationEventHandler.bind(this), true, true);

    if (!Social.initialized) {
      Social.init();
    } else if (Social.providers.length > 0) {
      // Social was initialized during startup in a previous window. If we have
      // providers enabled initialize the UI for this window.
      this.observe(null, "social:providers-changed", null);
      this.observe(null, "social:provider-set", Social.provider ? Social.provider.origin : null);
    }
  },

  // Called on window unload
  uninit: function SocialUI_uninit() {
    Services.obs.removeObserver(this, "social:ambient-notification-changed");
    Services.obs.removeObserver(this, "social:profile-changed");
    Services.obs.removeObserver(this, "social:frameworker-error");
    Services.obs.removeObserver(this, "social:provider-set");
    Services.obs.removeObserver(this, "social:providers-changed");
    Services.obs.removeObserver(this, "social:provider-reload");
    Services.obs.removeObserver(this, "social:provider-enabled");
    Services.obs.removeObserver(this, "social:provider-disabled");

    Services.prefs.removeObserver("social.sidebar.open", this);
    Services.prefs.removeObserver("social.toast-notifications.enabled", this);
  },

  _matchesCurrentProvider: function (origin) {
    return Social.provider && Social.provider.origin == origin;
  },

  observe: function SocialUI_observe(subject, topic, data) {
    // Exceptions here sometimes don't get reported properly, report them
    // manually :(
    try {
      switch (topic) {
        case "social:provider-enabled":
          SocialMarks.populateToolbarPalette();
          SocialStatus.populateToolbarPalette();
          break;
        case "social:provider-disabled":
          SocialMarks.removeProvider(data);
          SocialStatus.removeProvider(data);
          break;
        case "social:provider-reload":
          // if the reloaded provider is our current provider, fall through
          // to social:provider-set so the ui will be reset
          if (!Social.provider || Social.provider.origin != data)
            return;
          // be sure to unload the sidebar as it will not reload if the origin
          // has not changed, it will be loaded in provider-set below. Other
          // panels will be unloaded or handle reload.
          SocialSidebar.unloadSidebar();
          // fall through to social:provider-set
        case "social:provider-set":
          // Social.provider has changed (possibly to null), update any state
          // which depends on it.
          this._updateActiveUI();
          this._updateMenuItems();

          SocialFlyout.unload();
          SocialChatBar.update();
          SocialShare.update();
          SocialSidebar.update();
          SocialToolbar.update();
          SocialStatus.populateToolbarPalette();
          SocialMarks.populateToolbarPalette();
          SocialMenu.populate();
          break;
        case "social:providers-changed":
          // the list of providers changed - this may impact the "active" UI.
          this._updateActiveUI();
          // and the multi-provider menu
          SocialToolbar.populateProviderMenus();
          SocialShare.populateProviderMenu();
          SocialStatus.populateToolbarPalette();
          SocialMarks.populateToolbarPalette();
          break;

        // Provider-specific notifications
        case "social:ambient-notification-changed":
          SocialStatus.updateNotification(data);
          if (this._matchesCurrentProvider(data)) {
            SocialToolbar.updateButton();
            SocialMenu.populate();
          }
          break;
        case "social:profile-changed":
          // make sure anything that happens here only affects the provider for
          // which the profile is changing, and that anything we call actually
          // needs to change based on profile data.
          if (this._matchesCurrentProvider(data)) {
            SocialToolbar.updateProvider();
          }
          break;
        case "social:frameworker-error":
          if (this.enabled && Social.provider.origin == data) {
            SocialSidebar.setSidebarErrorMessage();
          }
          break;

        case "nsPref:changed":
          if (data == "social.sidebar.open") {
            SocialSidebar.update();
          } else if (data == "social.toast-notifications.enabled") {
            SocialToolbar.updateButton();
          }
          break;
      }
    } catch (e) {
      Components.utils.reportError(e + "\n" + e.stack);
      throw e;
    }
  },

  nonBrowserWindowInit: function SocialUI_nonBrowserInit() {
    // Disable the social menu item in non-browser windows
    document.getElementById("menu_socialAmbientMenu").hidden = true;
  },

  // Miscellaneous helpers
  showProfile: function SocialUI_showProfile() {
    if (Social.provider.haveLoggedInUser())
      openUILinkIn(Social.provider.profile.profileURL, "tab");
    else {
      // XXX Bug 789585 will implement an API for provider-specified login pages.
      openUILinkIn(Social.provider.origin, "tab");
    }
  },

  _updateActiveUI: function SocialUI_updateActiveUI() {
    // The "active" UI isn't dependent on there being a provider, just on
    // social being "active" (but also chromeless/PB)
    let enabled = Social.providers.length > 0 && !this._chromeless &&
                  !PrivateBrowsingUtils.isWindowPrivate(window);
    let broadcaster = document.getElementById("socialActiveBroadcaster");
    broadcaster.hidden = !enabled;

    let toggleCommand = document.getElementById("Social:Toggle");
    toggleCommand.setAttribute("hidden", enabled ? "false" : "true");

    if (enabled) {
      // enabled == true means we at least have a defaultProvider
      let provider = Social.provider || Social.defaultProvider;
      // We only need to update the command itself - all our menu items use it.
      let label;
      if (Social.providers.length == 1) {
        label = gNavigatorBundle.getFormattedString(Social.provider
                                                    ? "social.turnOff.label"
                                                    : "social.turnOn.label",
                                                    [provider.name]);
      } else {
        label = gNavigatorBundle.getString(Social.provider
                                           ? "social.turnOffAll.label"
                                           : "social.turnOnAll.label");
      }
      let accesskey = gNavigatorBundle.getString(Social.provider
                                                 ? "social.turnOff.accesskey"
                                                 : "social.turnOn.accesskey");
      toggleCommand.setAttribute("label", label);
      toggleCommand.setAttribute("accesskey", accesskey);
    }
  },

  _updateMenuItems: function () {
    let provider = Social.provider || Social.defaultProvider;
    if (!provider)
      return;
    // The View->Sidebar and Menubar->Tools menu.
    for (let id of ["menu_socialSidebar", "menu_socialAmbientMenu"])
      document.getElementById(id).setAttribute("label", provider.name);
  },

  // This handles "ActivateSocialFeature" events fired against content documents
  // in this window.
  _activationEventHandler: function SocialUI_activationHandler(e) {
    let targetDoc;
    let node;
    if (e.target instanceof HTMLDocument) {
      // version 0 support
      targetDoc = e.target;
      node = targetDoc.documentElement
    } else {
      targetDoc = e.target.ownerDocument;
      node = e.target;
    }
    if (!(targetDoc instanceof HTMLDocument))
      return;

    // Ignore events fired in background tabs or iframes
    if (targetDoc.defaultView != content)
      return;

    // If we are in PB mode, we silently do nothing (bug 829404 exists to
    // do something sensible here...)
    if (PrivateBrowsingUtils.isWindowPrivate(window))
      return;

    // If the last event was received < 1s ago, ignore this one
    let now = Date.now();
    if (now - Social.lastEventReceived < 1000)
      return;
    Social.lastEventReceived = now;

    // We only want to activate if it is as a result of user input.
    let dwu = window.QueryInterface(Ci.nsIInterfaceRequestor)
                    .getInterface(Ci.nsIDOMWindowUtils);
    if (!dwu.isHandlingUserInput) {
      Cu.reportError("attempt to activate provider without user input from " + targetDoc.nodePrincipal.origin);
      return;
    }

    let data = node.getAttribute("data-service");
    if (data) {
      try {
        data = JSON.parse(data);
      } catch(e) {
        Cu.reportError("Social Service manifest parse error: "+e);
        return;
      }
    }
    Social.installProvider(targetDoc, data, function(manifest) {
      this.doActivation(manifest.origin);
    }.bind(this));
  },

  doActivation: function SocialUI_doActivation(origin) {
    // Keep track of the old provider in case of undo
    let oldOrigin = Social.provider ? Social.provider.origin : "";

    // Enable the social functionality, and indicate that it was activated
    Social.activateFromOrigin(origin, function(provider) {
      // Provider to activate may not have been found
      if (!provider)
        return;

      // Show a warning, allow undoing the activation
      let description = document.getElementById("social-activation-message");
      let labels = description.getElementsByTagName("label");
      let uri = Services.io.newURI(provider.origin, null, null)
      labels[0].setAttribute("value", uri.host);
      labels[1].setAttribute("onclick", "BrowserOpenAddonsMgr('addons://list/service'); SocialUI.activationPanel.hidePopup();")

      let icon = document.getElementById("social-activation-icon");
      if (provider.icon64URL || provider.icon32URL) {
        icon.setAttribute('src', provider.icon64URL || provider.icon32URL);
        icon.hidden = false;
      } else {
        icon.removeAttribute('src');
        icon.hidden = true;
      }

      let notificationPanel = SocialUI.activationPanel;
      // Set the origin being activated and the previously active one, to allow undo
      notificationPanel.setAttribute("origin", provider.origin);
      notificationPanel.setAttribute("oldorigin", oldOrigin);

      // Show the panel
      notificationPanel.hidden = false;
      setTimeout(function () {
        notificationPanel.openPopup(SocialToolbar.button, "bottomcenter topright");
      }, 0);
    });
  },

  undoActivation: function SocialUI_undoActivation() {
    let origin = this.activationPanel.getAttribute("origin");
    let oldOrigin = this.activationPanel.getAttribute("oldorigin");
    Social.deactivateFromOrigin(origin, oldOrigin);
    this.activationPanel.hidePopup();
    Social.uninstallProvider(origin);
  },

  showLearnMore: function() {
    this.activationPanel.hidePopup();
    let url = Services.urlFormatter.formatURLPref("app.support.baseURL") + "social-api";
    openUILinkIn(url, "tab");
  },

  get activationPanel() {
    return document.getElementById("socialActivatedNotification");
  },

  closeSocialPanelForLinkTraversal: function (target, linkNode) {
    // No need to close the panel if this traversal was not retargeted
    if (target == "" || target == "_self")
      return;

    // Check to see whether this link traversal was in a social panel
    let win = linkNode.ownerDocument.defaultView;
    let container = win.QueryInterface(Ci.nsIInterfaceRequestor)
                                  .getInterface(Ci.nsIWebNavigation)
                                  .QueryInterface(Ci.nsIDocShell)
                                  .chromeEventHandler;
    let containerParent = container.parentNode;
    if (containerParent.classList.contains("social-panel") &&
        containerParent instanceof Ci.nsIDOMXULPopupElement) {
      // allow the link traversal to finish before closing the panel
      setTimeout(() => {
        containerParent.hidePopup();
      }, 0);
    }
  },

  get _chromeless() {
    // Is this a popup window that doesn't want chrome shown?
    let docElem = document.documentElement;
    // extrachrome is not restored during session restore, so we need
    // to check for the toolbar as well.
    let chromeless = docElem.getAttribute("chromehidden").contains("extrachrome") ||
                     docElem.getAttribute('chromehidden').contains("toolbar");
    // This property is "fixed" for a window, so avoid doing the check above
    // multiple times...
    delete this._chromeless;
    this._chromeless = chromeless;
    return chromeless;
  },

  get enabled() {
    // Returns whether social is enabled *for this window*.
    if (this._chromeless || PrivateBrowsingUtils.isWindowPrivate(window))
      return false;
    return !!Social.provider;
  },

  // called on tab/urlbar/location changes and after customization. Update
  // anything that is tab specific.
  updateState: function() {
    if (!this.enabled)
      return;
    SocialMarks.update();
    SocialShare.update();
  }
}

SocialChatBar = {
  get chatbar() {
    return document.getElementById("pinnedchats");
  },
  // Whether the chatbar is available for this window.  Note that in full-screen
  // mode chats are available, but not shown.
  get isAvailable() {
    return SocialUI.enabled;
  },
  // Does this chatbar have any chats (whether minimized, collapsed or normal)
  get hasChats() {
    return !!this.chatbar.firstElementChild;
  },
  openChat: function(aProvider, aURL, aCallback, aMode) {
    if (!this.isAvailable)
      return false;
    this.chatbar.openChat(aProvider, aURL, aCallback, aMode);
    // We only want to focus the chat if it is as a result of user input.
    let dwu = window.QueryInterface(Ci.nsIInterfaceRequestor)
                    .getInterface(Ci.nsIDOMWindowUtils);
    if (dwu.isHandlingUserInput)
      this.chatbar.focus();
    return true;
  },
  update: function() {
    let command = document.getElementById("Social:FocusChat");
    if (!this.isAvailable) {
      this.chatbar.hidden = command.hidden = true;
    } else {
      this.chatbar.hidden = command.hidden = false;
    }
    command.setAttribute("disabled", command.hidden ? "true" : "false");
  },
  focus: function SocialChatBar_focus() {
    this.chatbar.focus();
  }
}

SocialFlyout = {
  get panel() {
    return document.getElementById("social-flyout-panel");
  },

  get iframe() {
    if (!this.panel.firstChild)
      this._createFrame();
    return this.panel.firstChild;
  },

  dispatchPanelEvent: function(name) {
    let doc = this.iframe.contentDocument;
    let evt = doc.createEvent("CustomEvent");
    evt.initCustomEvent(name, true, true, {});
    doc.documentElement.dispatchEvent(evt);
  },

  _createFrame: function() {
    let panel = this.panel;
    if (!SocialUI.enabled || panel.firstChild)
      return;
    // create and initialize the panel for this window
    let iframe = document.createElement("iframe");
    iframe.setAttribute("type", "content");
    iframe.setAttribute("class", "social-panel-frame");
    iframe.setAttribute("flex", "1");
    iframe.setAttribute("tooltip", "aHTMLTooltip");
    iframe.setAttribute("origin", Social.provider.origin);
    panel.appendChild(iframe);
  },

  setFlyoutErrorMessage: function SF_setFlyoutErrorMessage() {
    this.iframe.removeAttribute("src");
    this.iframe.webNavigation.loadURI("about:socialerror?mode=compactInfo", null, null, null, null);
    sizeSocialPanelToContent(this.panel, this.iframe);
  },

  unload: function() {
    let panel = this.panel;
    panel.hidePopup();
    if (!panel.firstChild)
      return
    let iframe = panel.firstChild;
    if (iframe.socialErrorListener)
      iframe.socialErrorListener.remove();
    panel.removeChild(iframe);
  },

  onShown: function(aEvent) {
    let panel = this.panel;
    let iframe = this.iframe;
    this._dynamicResizer = new DynamicResizeWatcher();
    iframe.docShell.isActive = true;
    iframe.docShell.isAppTab = true;
    if (iframe.contentDocument.readyState == "complete") {
      this._dynamicResizer.start(panel, iframe);
      this.dispatchPanelEvent("socialFrameShow");
    } else {
      // first time load, wait for load and dispatch after load
      iframe.addEventListener("load", function panelBrowserOnload(e) {
        iframe.removeEventListener("load", panelBrowserOnload, true);
        setTimeout(function() {
          if (SocialFlyout._dynamicResizer) { // may go null if hidden quickly
            SocialFlyout._dynamicResizer.start(panel, iframe);
            SocialFlyout.dispatchPanelEvent("socialFrameShow");
          }
        }, 0);
      }, true);
    }
  },

  onHidden: function(aEvent) {
    this._dynamicResizer.stop();
    this._dynamicResizer = null;
    this.iframe.docShell.isActive = false;
    this.dispatchPanelEvent("socialFrameHide");
  },

  load: function(aURL, cb) {
    if (!Social.provider)
      return;

    this.panel.hidden = false;
    let iframe = this.iframe;
    // same url with only ref difference does not cause a new load, so we
    // want to go right to the callback
    let src = iframe.contentDocument && iframe.contentDocument.documentURIObject;
    if (!src || !src.equalsExceptRef(Services.io.newURI(aURL, null, null))) {
      iframe.addEventListener("load", function documentLoaded() {
        iframe.removeEventListener("load", documentLoaded, true);
        cb();
      }, true);
      // Force a layout flush by calling .clientTop so
      // that the docShell of this frame is created
      iframe.clientTop;
      Social.setErrorListener(iframe, SocialFlyout.setFlyoutErrorMessage.bind(SocialFlyout))
      iframe.setAttribute("src", aURL);
    } else {
      // we still need to set the src to trigger the contents hashchange event
      // for ref changes
      iframe.setAttribute("src", aURL);
      cb();
    }
  },

  open: function(aURL, yOffset, aCallback) {
    // Hide any other social panels that may be open.
    document.getElementById("social-notification-panel").hidePopup();

    if (!SocialUI.enabled)
      return;
    let panel = this.panel;
    let iframe = this.iframe;

    this.load(aURL, function() {
      sizeSocialPanelToContent(panel, iframe);
      let anchor = document.getElementById("social-sidebar-browser");
      if (panel.state == "open") {
        panel.moveToAnchor(anchor, "start_before", 0, yOffset, false);
      } else {
        panel.openPopup(anchor, "start_before", 0, yOffset, false, false);
      }
      if (aCallback) {
        try {
          aCallback(iframe.contentWindow);
        } catch(e) {
          Cu.reportError(e);
        }
      }
    });
  }
}

SocialShare = {
  get panel() {
    return document.getElementById("social-share-panel");
  },

  get iframe() {
    // first element is our menu vbox.
    if (this.panel.childElementCount == 1)
      return null;
    else
      return this.panel.lastChild;
  },

  uninit: function () {
    if (this.iframe) {
      this.iframe.remove();
    }
  },

  _createFrame: function() {
    let panel = this.panel;
    if (!SocialUI.enabled || this.iframe)
      return;
    this.panel.hidden = false;
    // create and initialize the panel for this window
    let iframe = document.createElement("iframe");
    iframe.setAttribute("type", "content");
    iframe.setAttribute("class", "social-share-frame");
    iframe.setAttribute("context", "contentAreaContextMenu");
    iframe.setAttribute("tooltip", "aHTMLTooltip");
    iframe.setAttribute("flex", "1");
    panel.appendChild(iframe);
    this.populateProviderMenu();
  },

  getSelectedProvider: function() {
    let provider;
    let lastProviderOrigin = this.iframe && this.iframe.getAttribute("origin");
    if (lastProviderOrigin) {
      provider = Social._getProviderFromOrigin(lastProviderOrigin);
    }
    if (!provider)
      provider = Social.provider || Social.defaultProvider;
    // if our provider has no shareURL, select the first one that does
    if (provider && !provider.shareURL) {
      let providers = [p for (p of Social.providers) if (p.shareURL)];
      provider = providers.length > 0  && providers[0];
    }
    return provider;
  },

  populateProviderMenu: function() {
    if (!this.iframe)
      return;
    let providers = [p for (p of Social.providers) if (p.shareURL)];
    let hbox = document.getElementById("social-share-provider-buttons");
    // selectable providers are inserted before the provider-menu seperator,
    // remove any menuitems in that area
    while (hbox.firstChild) {
      hbox.removeChild(hbox.firstChild);
    }
    // reset our share toolbar
    // only show a selection if there is more than one
    if (!SocialUI.enabled || providers.length < 2) {
      this.panel.firstChild.hidden = true;
      return;
    }
    let selectedProvider = this.getSelectedProvider();
    for (let provider of providers) {
      let button = document.createElement("toolbarbutton");
      button.setAttribute("class", "toolbarbutton share-provider-button");
      button.setAttribute("type", "radio");
      button.setAttribute("group", "share-providers");
      button.setAttribute("image", provider.iconURL);
      button.setAttribute("tooltiptext", provider.name);
      button.setAttribute("origin", provider.origin);
      button.setAttribute("oncommand", "SocialShare.sharePage(this.getAttribute('origin')); this.checked=true;");
      if (provider == selectedProvider) {
        this.defaultButton = button;
      }
      hbox.appendChild(button);
    }
    if (!this.defaultButton) {
      this.defaultButton = hbox.firstChild
    }
    this.defaultButton.setAttribute("checked", "true");
    this.panel.firstChild.hidden = false;
  },

  get shareButton() {
    return document.getElementById("social-share-button");
  },

  canSharePage: function(aURI) {
    // we do not enable sharing from private sessions
    if (PrivateBrowsingUtils.isWindowPrivate(window))
      return false;

    if (!aURI || !(aURI.schemeIs('http') || aURI.schemeIs('https')))
      return false;
    return true;
  },

  update: function() {
    let shareButton = this.shareButton;
    shareButton.hidden = !SocialUI.enabled ||
                         [p for (p of Social.providers) if (p.shareURL)].length == 0;
    shareButton.disabled = shareButton.hidden || !this.canSharePage(gBrowser.currentURI);

    // also update the relevent command's disabled state so the keyboard
    // shortcut only works when available.
    let cmd = document.getElementById("Social:SharePage");
    cmd.setAttribute("disabled", shareButton.disabled ? "true" : "false");
  },

  onShowing: function() {
    this.shareButton.setAttribute("open", "true");
  },

  onHidden: function() {
    this.shareButton.removeAttribute("open");
    this.iframe.setAttribute("src", "data:text/plain;charset=utf8,");
    this.currentShare = null;
  },

  setErrorMessage: function() {
    let iframe = this.iframe;
    if (!iframe)
      return;

    iframe.removeAttribute("src");
    iframe.webNavigation.loadURI("about:socialerror?mode=compactInfo&origin=" +
                                 encodeURIComponent(iframe.getAttribute("origin")),
                                 null, null, null, null);
    sizeSocialPanelToContent(this.panel, iframe);
  },

  sharePage: function(providerOrigin, graphData) {
    // if providerOrigin is undefined, we use the last-used provider, or the
    // current/default provider.  The provider selection in the share panel
    // will call sharePage with an origin for us to switch to.
    this._createFrame();
    let iframe = this.iframe;
    let provider;
    if (providerOrigin)
      provider = Social._getProviderFromOrigin(providerOrigin);
    else
      provider = this.getSelectedProvider();
    if (!provider || !provider.shareURL)
      return;

    // graphData is an optional param that either defines the full set of data
    // to be shared, or partial data about the current page. It is set by a call
    // in mozSocial API, or via nsContentMenu calls. If it is present, it MUST
    // define at least url. If it is undefined, we're sharing the current url in
    // the browser tab.
    let sharedURI = graphData ? Services.io.newURI(graphData.url, null, null) :
                                gBrowser.currentURI;
    if (!this.canSharePage(sharedURI))
      return;

    // the point of this action type is that we can use existing share
    // endpoints (e.g. oexchange) that do not support additional
    // socialapi functionality.  One tweak is that we shoot an event
    // containing the open graph data.
    let pageData = graphData ? graphData : this.currentShare;
    if (!pageData || sharedURI == gBrowser.currentURI) {
      pageData = OpenGraphBuilder.getData(gBrowser);
      if (graphData) {
        // overwrite data retreived from page with data given to us as a param
        for (let p in graphData) {
          pageData[p] = graphData[p];
        }
      }
    }
    this.currentShare = pageData;

    let shareEndpoint = OpenGraphBuilder.generateEndpointURL(provider.shareURL, pageData);

    this._dynamicResizer = new DynamicResizeWatcher();
    // if we've already loaded this provider/page share endpoint, we don't want
    // to add another load event listener.
    let reload = true;
    let endpointMatch = shareEndpoint == iframe.getAttribute("src");
    let docLoaded = iframe.contentDocument && iframe.contentDocument.readyState == "complete";
    if (endpointMatch && docLoaded) {
      reload = shareEndpoint != iframe.contentDocument.location.spec;
    }
    if (!reload) {
      this._dynamicResizer.start(this.panel, iframe);
      iframe.docShell.isActive = true;
      iframe.docShell.isAppTab = true;
      let evt = iframe.contentDocument.createEvent("CustomEvent");
      evt.initCustomEvent("OpenGraphData", true, true, JSON.stringify(pageData));
      iframe.contentDocument.documentElement.dispatchEvent(evt);
    } else {
      // first time load, wait for load and dispatch after load
      iframe.addEventListener("load", function panelBrowserOnload(e) {
        iframe.removeEventListener("load", panelBrowserOnload, true);
        iframe.docShell.isActive = true;
        iframe.docShell.isAppTab = true;
        setTimeout(function() {
          if (SocialShare._dynamicResizer) { // may go null if hidden quickly
            SocialShare._dynamicResizer.start(iframe.parentNode, iframe);
          }
        }, 0);
        let evt = iframe.contentDocument.createEvent("CustomEvent");
        evt.initCustomEvent("OpenGraphData", true, true, JSON.stringify(pageData));
        iframe.contentDocument.documentElement.dispatchEvent(evt);
      }, true);
    }
    // always ensure that origin belongs to the endpoint
    let uri = Services.io.newURI(shareEndpoint, null, null);
    iframe.setAttribute("origin", provider.origin);
    iframe.setAttribute("src", shareEndpoint);

    let navBar = document.getElementById("nav-bar");
    let anchor = document.getAnonymousElementByAttribute(this.shareButton, "class", "toolbarbutton-icon");
    this.panel.openPopup(anchor, "bottomcenter topright", 0, 0, false, false);
    Social.setErrorListener(iframe, this.setErrorMessage.bind(this));
  }
};

SocialMenu = {
  populate: function SocialMenu_populate() {
    let submenu = document.getElementById("menu_social-statusarea-popup");
    let ambientMenuItems = submenu.getElementsByClassName("ambient-menuitem");
    while (ambientMenuItems.length)
      submenu.removeChild(ambientMenuItems.item(0));

    let separator = document.getElementById("socialAmbientMenuSeparator");
    separator.hidden = true;
    let provider = SocialUI.enabled ? Social.provider : null;
    if (!provider)
      return;

    let iconNames = Object.keys(provider.ambientNotificationIcons);
    for (let name of iconNames) {
      let icon = provider.ambientNotificationIcons[name];
      if (!icon.label || !icon.menuURL)
        continue;
      separator.hidden = false;
      let menuitem = document.createElement("menuitem");
      menuitem.setAttribute("label", icon.label);
      menuitem.classList.add("ambient-menuitem");
      menuitem.addEventListener("command", function() {
        openUILinkIn(icon.menuURL, "tab");
      }, false);
      submenu.insertBefore(menuitem, separator);
    }
  }
};

// XXX Need to audit that this is being initialized correctly
SocialToolbar = {
  // Called once, after window load, when the Social.provider object is
  // initialized.
  get _dynamicResizer() {
    delete this._dynamicResizer;
    this._dynamicResizer = new DynamicResizeWatcher();
    return this._dynamicResizer;
  },

  update: function() {
    this._updateButtonHiddenState();
    this.updateProvider();
    this.populateProviderMenus();
  },

  // Called when the Social.provider changes
  updateProvider: function () {
    let provider = Social.provider;
    if (provider) {
      this.button.setAttribute("label", provider.name);
      this.button.setAttribute("tooltiptext", provider.name);
      this.button.style.listStyleImage = "url(" + provider.iconURL + ")";

      this.updateProfile();
    } else {
      this.button.setAttribute("label", gNavigatorBundle.getString("service.toolbarbutton.label"));
      this.button.setAttribute("tooltiptext", gNavigatorBundle.getString("service.toolbarbutton.tooltiptext"));
      this.button.style.removeProperty("list-style-image");
    }
    this.updateButton();
  },

  get button() {
    return document.getElementById("social-provider-button");
  },

  // Note: this doesn't actually handle hiding the toolbar button,
  // socialActiveBroadcaster is responsible for that.
  _updateButtonHiddenState: function SocialToolbar_updateButtonHiddenState() {
    let socialEnabled = SocialUI.enabled;
    for (let className of ["social-statusarea-separator", "social-statusarea-user"]) {
      for (let element of document.getElementsByClassName(className))
        element.hidden = !socialEnabled;
    }
    let toggleNotificationsCommand = document.getElementById("Social:ToggleNotifications");
    toggleNotificationsCommand.setAttribute("hidden", !socialEnabled);

    // we need to remove buttons and frames if !socialEnabled or the provider
    // has changed (frame origin does not match current provider). We only
    // remove frames that are "attached" to buttons in this toolbar button since
    // other buttons may also be using grouped frames.
    let tbi = document.getElementById("social-provider-button");
    if (tbi) {
      // buttons after social-provider-button are ambient icons
      let next = tbi.nextSibling;
      let currentOrigin = Social.provider ? Social.provider.origin : null;

      while (next) {
        let button = next;
        next = next.nextSibling;
        // get the frame for this button
        let frameId = button.getAttribute("notificationFrameId");
        let frame = document.getElementById(frameId);
        if (!socialEnabled || frame.getAttribute("origin") != currentOrigin) {
          SharedFrame.forgetGroup(frame.id);
          frame.parentNode.removeChild(frame);
          button.parentNode.removeChild(button);
        }
      }
    }
  },

  updateProfile: function SocialToolbar_updateProfile() {
    // Profile may not have been initialized yet, since it depends on a worker
    // response. In that case we'll be called again when it's available, via
    // social:profile-changed
    if (!Social.provider)
      return;
    let profile = Social.provider.profile || {};
    let userPortrait = profile.portrait;

    let userDetailsBroadcaster = document.getElementById("socialBroadcaster_userDetails");
    let loggedInStatusValue = profile.userName ||
                              userDetailsBroadcaster.getAttribute("notLoggedInLabel");

    // "image" and "label" are used by Mac's native menus that do not render the menuitem's children
    // elements. "src" and "value" are used by the image/label children on the other platforms.
    if (userPortrait) {
      userDetailsBroadcaster.setAttribute("src", userPortrait);
      userDetailsBroadcaster.setAttribute("image", userPortrait);
    } else {
      userDetailsBroadcaster.removeAttribute("src");
      userDetailsBroadcaster.removeAttribute("image");
    }

    userDetailsBroadcaster.setAttribute("value", loggedInStatusValue);
    userDetailsBroadcaster.setAttribute("label", loggedInStatusValue);
  },

  updateButton: function SocialToolbar_updateButton() {
    this._updateButtonHiddenState();
    let panel = document.getElementById("social-notification-panel");
    panel.hidden = !SocialUI.enabled;

    let command = document.getElementById("Social:ToggleNotifications");
    command.setAttribute("checked", Services.prefs.getBoolPref("social.toast-notifications.enabled"));

    const CACHE_PREF_NAME = "social.cached.ambientNotificationIcons";
    // provider.profile == undefined means no response yet from the provider
    // to tell us whether the user is logged in or not.
    if (!SocialUI.enabled ||
        (!Social.provider.haveLoggedInUser() && Social.provider.profile !== undefined)) {
      // Either no enabled provider, or there is a provider and it has
      // responded with a profile and the user isn't loggedin.  The icons
      // etc have already been removed by updateButtonHiddenState, so we want
      // to nuke any cached icons we have and get out of here!
      Services.prefs.clearUserPref(CACHE_PREF_NAME);
      return;
    }

    // If the provider uses the new SocialStatus button, then they do not get
    // to use the ambient icons in the old toolbar button.  Since the status
    // button depends on multiple workers, if not enabled we will ignore this
    // limitation.  That allows a provider to migrate to the new functionality
    // once we enable multiple workers.
    if (Social.provider.statusURL && Social.allowMultipleWorkers)
      return;

    let icons = Social.provider.ambientNotificationIcons;
    let iconNames = Object.keys(icons);

    if (Social.provider.profile === undefined) {
      // provider has not told us about the login state yet - see if we have
      // a cached version for this provider.
      let cached;
      try {
        cached = JSON.parse(Services.prefs.getComplexValue(CACHE_PREF_NAME,
                                                           Ci.nsISupportsString).data);
      } catch (ex) {}
      if (cached && cached.provider == Social.provider.origin && cached.data) {
        icons = cached.data;
        iconNames = Object.keys(icons);
        // delete the counter data as it is almost certainly stale.
        for each(let name in iconNames) {
          icons[name].counter = '';
        }
      }
    } else {
      // We have a logged in user - save the current set of icons back to the
      // "cache" so we can use them next startup.
      let str = Cc["@mozilla.org/supports-string;1"].createInstance(Ci.nsISupportsString);
      str.data = JSON.stringify({provider: Social.provider.origin, data: icons});
      Services.prefs.setComplexValue(CACHE_PREF_NAME,
                                     Ci.nsISupportsString,
                                     str);
    }

    let toolbarButtons = document.createDocumentFragment();

    let createdFrames = [];

    for each(let name in iconNames) {
      let icon = icons[name];

      let notificationFrameId = "social-status-" + icon.name;
      let notificationFrame = document.getElementById(notificationFrameId);

      if (!notificationFrame) {
        notificationFrame = SharedFrame.createFrame(
          notificationFrameId, /* frame name */
          panel, /* parent */
          {
            "type": "content",
            "mozbrowser": "true",
            "class": "social-panel-frame",
            "id": notificationFrameId,
            "tooltip": "aHTMLTooltip",

            // work around bug 793057 - by making the panel roughly the final size
            // we are more likely to have the anchor in the correct position.
            "style": "width: " + PANEL_MIN_WIDTH + "px;",

            "origin": Social.provider.origin,
            "src": icon.contentPanel
          }
        );

        createdFrames.push(notificationFrame);
      } else {
        notificationFrame.setAttribute("origin", Social.provider.origin);
        SharedFrame.updateURL(notificationFrameId, icon.contentPanel);
      }

      let toolbarButtonId = "social-notification-icon-" + icon.name;
      let toolbarButton = document.getElementById(toolbarButtonId);
      if (!toolbarButton) {
        toolbarButton = document.createElement("toolbarbutton");
        toolbarButton.setAttribute("type", "badged");
        toolbarButton.setAttribute("class", "toolbarbutton-1 chromeclass-toolbar-additional");
        toolbarButton.setAttribute("id", toolbarButtonId);
        toolbarButton.setAttribute("notificationFrameId", notificationFrameId);
        toolbarButton.addEventListener("mousedown", function (event) {
          if (event.button == 0 && panel.state == "closed")
            SocialToolbar.showAmbientPopup(toolbarButton);
        });

        toolbarButtons.appendChild(toolbarButton);
      }

      toolbarButton.style.listStyleImage = "url(" + icon.iconURL + ")";
      toolbarButton.setAttribute("label", icon.label);
      toolbarButton.setAttribute("tooltiptext", icon.label);

      let badge = icon.counter || "";
      toolbarButton.setAttribute("badge", badge);
      let ariaLabel = icon.label;
      // if there is a badge value, we must use a localizable string to insert it.
      if (badge)
        ariaLabel = gNavigatorBundle.getFormattedString("social.aria.toolbarButtonBadgeText",
                                                        [ariaLabel, badge]);
      toolbarButton.setAttribute("aria-label", ariaLabel);
    }
    let socialToolbarItem = document.getElementById("social-toolbar-item");
    socialToolbarItem.appendChild(toolbarButtons);

    for (let frame of createdFrames) {
      if (frame.socialErrorListener)
        frame.socialErrorListener.remove();
      if (frame.docShell) {
        frame.docShell.isActive = false;
        Social.setErrorListener(frame, this.setPanelErrorMessage.bind(this));
      }
    }
  },

  showAmbientPopup: function SocialToolbar_showAmbientPopup(aToolbarButton) {
    // Hide any other social panels that may be open.
    SocialFlyout.panel.hidePopup();

    let panel = document.getElementById("social-notification-panel");
    let notificationFrameId = aToolbarButton.getAttribute("notificationFrameId");
    let notificationFrame = document.getElementById(notificationFrameId);

    let wasAlive = SharedFrame.isGroupAlive(notificationFrameId);
    SharedFrame.setOwner(notificationFrameId, notificationFrame);

    // Clear dimensions on all browsers so the panel size will
    // only use the selected browser.
    let frameIter = panel.firstElementChild;
    while (frameIter) {
      frameIter.collapsed = (frameIter != notificationFrame);
      frameIter = frameIter.nextElementSibling;
    }

    function dispatchPanelEvent(name) {
      let evt = notificationFrame.contentDocument.createEvent("CustomEvent");
      evt.initCustomEvent(name, true, true, {});
      notificationFrame.contentDocument.documentElement.dispatchEvent(evt);
    }

    let dynamicResizer = this._dynamicResizer;
    panel.addEventListener("popuphidden", function onpopuphiding() {
      panel.removeEventListener("popuphidden", onpopuphiding);
      aToolbarButton.removeAttribute("open");
      aToolbarButton.parentNode.removeAttribute("open");
      dynamicResizer.stop();
      notificationFrame.docShell.isActive = false;
      dispatchPanelEvent("socialFrameHide");
    });

    panel.addEventListener("popupshown", function onpopupshown() {
      panel.removeEventListener("popupshown", onpopupshown);
      // The "open" attribute is needed on both the button and the containing
      // toolbaritem since the buttons on OS X have moz-appearance:none, while
      // their container gets moz-appearance:toolbarbutton due to the way that
      // toolbar buttons get combined on OS X.
      aToolbarButton.setAttribute("open", "true");
      aToolbarButton.parentNode.setAttribute("open", "true");
      notificationFrame.docShell.isActive = true;
      notificationFrame.docShell.isAppTab = true;
      if (notificationFrame.contentDocument.readyState == "complete" && wasAlive) {
        dynamicResizer.start(panel, notificationFrame);
        dispatchPanelEvent("socialFrameShow");
      } else {
        // first time load, wait for load and dispatch after load
        notificationFrame.addEventListener("load", function panelBrowserOnload(e) {
          notificationFrame.removeEventListener("load", panelBrowserOnload, true);
          dynamicResizer.start(panel, notificationFrame);
          setTimeout(function() {
            dispatchPanelEvent("socialFrameShow");
          }, 0);
        }, true);
      }
    });

    let navBar = document.getElementById("nav-bar");
    let anchor = document.getAnonymousElementByAttribute(aToolbarButton, "class", "toolbarbutton-badge-container");
    // Bug 849216 - open the popup in a setTimeout so we avoid the auto-rollup
    // handling from preventing it being opened in some cases.
    setTimeout(function() {
      panel.openPopup(anchor, "bottomcenter topright", 0, 0, false, false);
    }, 0);
  },

  setPanelErrorMessage: function SocialToolbar_setPanelErrorMessage(aNotificationFrame) {
    if (!aNotificationFrame)
      return;

    let src = aNotificationFrame.getAttribute("src");
    aNotificationFrame.removeAttribute("src");
    aNotificationFrame.webNavigation.loadURI("about:socialerror?mode=tryAgainOnly&url=" +
                                             encodeURIComponent(src), null, null, null, null);
    let panel = aNotificationFrame.parentNode;
    sizeSocialPanelToContent(panel, aNotificationFrame);
  },

  populateProviderMenus: function SocialToolbar_renderProviderMenus() {
    let providerMenuSeps = document.getElementsByClassName("social-provider-menu");
    for (let providerMenuSep of providerMenuSeps)
      this._populateProviderMenu(providerMenuSep);
  },

  _populateProviderMenu: function SocialToolbar_renderProviderMenu(providerMenuSep) {
    let menu = providerMenuSep.parentNode;
    // selectable providers are inserted before the provider-menu seperator,
    // remove any menuitems in that area
    while (providerMenuSep.previousSibling.nodeName == "menuitem") {
      menu.removeChild(providerMenuSep.previousSibling);
    }
    // only show a selection if enabled and there is more than one
    let providers = [p for (p of Social.providers) if (p.workerURL || p.sidebarURL)];
    if (providers.length < 2) {
      providerMenuSep.hidden = true;
      return;
    }
    for (let provider of providers) {
      let menuitem = document.createElement("menuitem");
      menuitem.className = "menuitem-iconic social-provider-menuitem";
      menuitem.setAttribute("image", provider.iconURL);
      menuitem.setAttribute("label", provider.name);
      menuitem.setAttribute("origin", provider.origin);
      if (provider == Social.provider) {
        menuitem.setAttribute("checked", "true");
      } else {
        menuitem.setAttribute("oncommand", "Social.setProviderByOrigin(this.getAttribute('origin'));");
      }
      menu.insertBefore(menuitem, providerMenuSep);
    }
    providerMenuSep.hidden = false;
  }
}

SocialSidebar = {
  // Whether the sidebar can be shown for this window.
  get canShow() {
    return SocialUI.enabled && Social.provider.sidebarURL;
  },

  // Whether the user has toggled the sidebar on (for windows where it can appear)
  get opened() {
    return Services.prefs.getBoolPref("social.sidebar.open") && !document.mozFullScreen;
  },

  setSidebarVisibilityState: function(aEnabled) {
    let sbrowser = document.getElementById("social-sidebar-browser");
    // it's possible we'll be called twice with aEnabled=false so let's
    // just assume we may often be called with the same state.
    if (aEnabled == sbrowser.docShellIsActive)
      return;
    sbrowser.docShellIsActive = aEnabled;
    let evt = sbrowser.contentDocument.createEvent("CustomEvent");
    evt.initCustomEvent(aEnabled ? "socialFrameShow" : "socialFrameHide", true, true, {});
    sbrowser.contentDocument.documentElement.dispatchEvent(evt);
  },

  update: function SocialSidebar_update() {
    clearTimeout(this._unloadTimeoutId);
    // Hide the toggle menu item if the sidebar cannot appear
    let command = document.getElementById("Social:ToggleSidebar");
    command.setAttribute("hidden", this.canShow ? "false" : "true");

    // Hide the sidebar if it cannot appear, or has been toggled off.
    // Also set the command "checked" state accordingly.
    let hideSidebar = !this.canShow || !this.opened;
    let broadcaster = document.getElementById("socialSidebarBroadcaster");
    broadcaster.hidden = hideSidebar;
    command.setAttribute("checked", !hideSidebar);

    let sbrowser = document.getElementById("social-sidebar-browser");

    if (hideSidebar) {
      sbrowser.removeEventListener("load", SocialSidebar._loadListener, true);
      this.setSidebarVisibilityState(false);
      // If we've been disabled, unload the sidebar content immediately;
      // if the sidebar was just toggled to invisible, wait a timeout
      // before unloading.
      if (!this.canShow) {
        this.unloadSidebar();
      } else {
        this._unloadTimeoutId = setTimeout(
          this.unloadSidebar,
          Services.prefs.getIntPref("social.sidebar.unload_timeout_ms")
        );
      }
    } else {
      sbrowser.setAttribute("origin", Social.provider.origin);
      if (Social.provider.errorState == "frameworker-error") {
        SocialSidebar.setSidebarErrorMessage();
        return;
      }

      // Make sure the right sidebar URL is loaded
      if (sbrowser.getAttribute("src") != Social.provider.sidebarURL) {
        Social.setErrorListener(sbrowser, this.setSidebarErrorMessage.bind(this));
        // setting isAppTab causes clicks on untargeted links to open new tabs
        sbrowser.docShell.isAppTab = true;
        sbrowser.setAttribute("src", Social.provider.sidebarURL);
        PopupNotifications.locationChange(sbrowser);
      }

      // if the document has not loaded, delay until it is
      if (sbrowser.contentDocument.readyState != "complete") {
        sbrowser.addEventListener("load", SocialSidebar._loadListener, true);
      } else {
        this.setSidebarVisibilityState(true);
      }
    }
  },

  _loadListener: function SocialSidebar_loadListener() {
    let sbrowser = document.getElementById("social-sidebar-browser");
    sbrowser.removeEventListener("load", SocialSidebar._loadListener, true);
    SocialSidebar.setSidebarVisibilityState(true);
  },

  unloadSidebar: function SocialSidebar_unloadSidebar() {
    let sbrowser = document.getElementById("social-sidebar-browser");
    if (!sbrowser.hasAttribute("origin"))
      return;

    sbrowser.stop();
    sbrowser.removeAttribute("origin");
    sbrowser.setAttribute("src", "about:blank");
    SocialFlyout.unload();
  },

  _unloadTimeoutId: 0,

  setSidebarErrorMessage: function() {
    let sbrowser = document.getElementById("social-sidebar-browser");
    // a frameworker error "trumps" a sidebar error.
    if (Social.provider.errorState == "frameworker-error") {
      sbrowser.setAttribute("src", "about:socialerror?mode=workerFailure");
    } else {
      let url = encodeURIComponent(Social.provider.sidebarURL);
      sbrowser.loadURI("about:socialerror?mode=tryAgain&url=" + url, null, null);
    }
  }
}

// this helper class is used by removable/customizable buttons to handle
// widget creation/destruction

// When a provider is installed we show all their UI so the user will see the
// functionality of what they installed. The user can later customize the UI,
// moving buttons around or off the toolbar.
//
// On startup, we create the button widgets of any enabled provider.
// CustomizableUI handles placement and persistence of placement.
function ToolbarHelper(type, createButtonFn) {
  this._createButton = createButtonFn;
  this._type = type;
}

ToolbarHelper.prototype = {
  idFromOrigin: function(origin) {
    // this id needs to pass the checks in CustomizableUI, so remove characters
    // that wont pass.
    return this._type + "-" + Services.io.newURI(origin, null, null).hostPort.replace(/[\.:]/g,'-');
  },

  // should be called on disable of a provider
  removeProviderButton: function(origin) {
    CustomizableUI.destroyWidget(this.idFromOrigin(origin));
  },

  clearPalette: function() {
    [this.removeProviderButton(p.origin) for (p of Social.providers)];
  },

  // should be called on enable of a provider
  populatePalette: function() {
    if (!Social.enabled) {
      this.clearPalette();
      return;
    }

    // create any buttons that do not exist yet if they have been persisted
    // as a part of the UI (otherwise they belong in the palette).
    for (let provider of Social.providers) {
      let id = this.idFromOrigin(provider.origin);
      let widget = CustomizableUI.getWidget(id);
      // The widget is only null if we've created then destroyed the widget.
      // Once we've actually called createWidget the provider will be set to
      // PROVIDER_API.
      if (!widget || widget.provider != CustomizableUI.PROVIDER_API)
        this._createButton(provider);
    }
  }
}

SocialStatus = {
  populateToolbarPalette: function() {
    if (!Social.allowMultipleWorkers)
      return;
    this._toolbarHelper.populatePalette();
  },

  removeProvider: function(origin) {
    if (!Social.allowMultipleWorkers)
      return;
    this._removeFrame(origin);
    this._toolbarHelper.removeProviderButton(origin);
  },

  _removeFrame: function(origin) {
    let notificationFrameId = "social-status-" + origin;
    let frame = document.getElementById(notificationFrameId);
    if (frame) {
      SharedFrame.forgetGroup(frame.id);
      frame.parentNode.removeChild(frame);
    }
  },

  get _toolbarHelper() {
    delete this._toolbarHelper;
    this._toolbarHelper = new ToolbarHelper("social-status-button", this._createButton.bind(this));
    return this._toolbarHelper;
  },

  get _dynamicResizer() {
    delete this._dynamicResizer;
    this._dynamicResizer = new DynamicResizeWatcher();
    return this._dynamicResizer;
  },

  _createButton: function(provider) {
    if (!provider.statusURL)
      return;
    let aId = this._toolbarHelper.idFromOrigin(provider.origin);
    CustomizableUI.createWidget({
      id: aId,
      type: 'custom',
      removable: true,
      defaultArea: CustomizableUI.AREA_NAVBAR,
      onBuild: function(document) {
        let window = document.defaultView;

        let node = document.createElement('toolbarbutton');

        node.id = this.id;
        node.setAttribute('class', 'toolbarbutton-1 chromeclass-toolbar-additional social-status-button');
        node.setAttribute('type', "badged");
        node.style.listStyleImage = "url(" + provider.iconURL + ")";
        node.setAttribute("origin", provider.origin);

        node.setAttribute("label", provider.name);
        node.setAttribute("tooltiptext", provider.name);
        node.setAttribute("oncommand", "SocialStatus.showPopup(this);");
        return node;
      }
    });
  },

  // status panels are one-per button per-process, we swap the docshells between
  // windows when necessary
  _attachNotificatonPanel: function(aButton, provider) {
    let panel = document.getElementById("social-notification-panel");
    panel.hidden = !SocialUI.enabled;
    let notificationFrameId = "social-status-" + provider.origin;
    let frame = document.getElementById(notificationFrameId);

    if (!frame) {
      frame = SharedFrame.createFrame(
        notificationFrameId, /* frame name */
        panel, /* parent */
        {
          "type": "content",
          "mozbrowser": "true",
          "class": "social-panel-frame",
          "id": notificationFrameId,
          "tooltip": "aHTMLTooltip",
          "context": "contentAreaContextMenu",

          // work around bug 793057 - by making the panel roughly the final size
          // we are more likely to have the anchor in the correct position.
          "style": "width: " + PANEL_MIN_WIDTH + "px;",

          "origin": provider.origin,
          "src": provider.statusURL
        }
      );

      if (frame.socialErrorListener)
        frame.socialErrorListener.remove();
      if (frame.docShell) {
        frame.docShell.isActive = false;
        Social.setErrorListener(frame, this.setPanelErrorMessage.bind(this));
      }
    } else {
      frame.setAttribute("origin", provider.origin);
      SharedFrame.updateURL(notificationFrameId, provider.statusURL);
    }
    aButton.setAttribute("notificationFrameId", notificationFrameId);
  },

  updateNotification: function(origin) {
    if (!Social.allowMultipleWorkers)
      return;
    let provider = Social._getProviderFromOrigin(origin);
    let button = document.getElementById(this._toolbarHelper.idFromOrigin(provider.origin));
    if (button) {
      // we only grab the first notification, ignore all others
      let icons = provider.ambientNotificationIcons;
      let iconNames = Object.keys(icons);
      let notif = icons[iconNames[0]];
      if (!notif) {
        button.setAttribute("badge", "");
        button.setAttribute("aria-label", "");
        button.setAttribute("tooltiptext", "");
        return;
      }

      button.style.listStyleImage = "url(" + notif.iconURL || provider.iconURL + ")";
      button.setAttribute("tooltiptext", notif.label);

      let badge = notif.counter || "";
      button.setAttribute("badge", badge);
      let ariaLabel = notif.label;
      // if there is a badge value, we must use a localizable string to insert it.
      if (badge)
        ariaLabel = gNavigatorBundle.getFormattedString("social.aria.toolbarButtonBadgeText",
                                                        [ariaLabel, badge]);
      button.setAttribute("aria-label", ariaLabel);
    }
  },

  showPopup: function(aToolbarButton) {
    if (!Social.allowMultipleWorkers)
      return;
    // attach our notification panel if necessary
    let origin = aToolbarButton.getAttribute("origin");
    let provider = Social._getProviderFromOrigin(origin);
    this._attachNotificatonPanel(aToolbarButton, provider);

    let panel = document.getElementById("social-notification-panel");
    let notificationFrameId = aToolbarButton.getAttribute("notificationFrameId");
    let notificationFrame = document.getElementById(notificationFrameId);

    let wasAlive = SharedFrame.isGroupAlive(notificationFrameId);
    SharedFrame.setOwner(notificationFrameId, notificationFrame);

    // Clear dimensions on all browsers so the panel size will
    // only use the selected browser.
    let frameIter = panel.firstElementChild;
    while (frameIter) {
      frameIter.collapsed = (frameIter != notificationFrame);
      frameIter = frameIter.nextElementSibling;
    }

    function dispatchPanelEvent(name) {
      let evt = notificationFrame.contentDocument.createEvent("CustomEvent");
      evt.initCustomEvent(name, true, true, {});
      notificationFrame.contentDocument.documentElement.dispatchEvent(evt);
    }

    let dynamicResizer = this._dynamicResizer;
    panel.addEventListener("popuphidden", function onpopuphiding() {
      panel.removeEventListener("popuphidden", onpopuphiding);
      aToolbarButton.removeAttribute("open");
      dynamicResizer.stop();
      notificationFrame.docShell.isActive = false;
      dispatchPanelEvent("socialFrameHide");
    });

    panel.addEventListener("popupshown", function onpopupshown() {
      panel.removeEventListener("popupshown", onpopupshown);
      // This attribute is needed on both the button and the
      // containing toolbaritem since the buttons on OS X have
      // moz-appearance:none, while their container gets
      // moz-appearance:toolbarbutton due to the way that toolbar buttons
      // get combined on OS X.
      aToolbarButton.setAttribute("open", "true");
      notificationFrame.docShell.isActive = true;
      notificationFrame.docShell.isAppTab = true;
      if (notificationFrame.contentDocument.readyState == "complete" && wasAlive) {
        dynamicResizer.start(panel, notificationFrame);
        dispatchPanelEvent("socialFrameShow");
      } else {
        // first time load, wait for load and dispatch after load
        notificationFrame.addEventListener("load", function panelBrowserOnload(e) {
          notificationFrame.removeEventListener("load", panelBrowserOnload, true);
          dynamicResizer.start(panel, notificationFrame);
          dispatchPanelEvent("socialFrameShow");
        }, true);
      }
    });

    let navBar = document.getElementById("nav-bar");
    let anchor = navBar.getAttribute("mode") == "text" ?
                   document.getAnonymousElementByAttribute(aToolbarButton, "class", "toolbarbutton-text") :
                   document.getAnonymousElementByAttribute(aToolbarButton, "class", "toolbarbutton-badge-container");
    // Bug 849216 - open the popup in a setTimeout so we avoid the auto-rollup
    // handling from preventing it being opened in some cases.
    setTimeout(function() {
      panel.openPopup(anchor, "bottomcenter topright", 0, 0, false, false);
    }, 0);
  },

  setPanelErrorMessage: function(aNotificationFrame) {
    if (!aNotificationFrame)
      return;

    let src = aNotificationFrame.getAttribute("src");
    aNotificationFrame.removeAttribute("src");
    aNotificationFrame.webNavigation.loadURI("about:socialerror?mode=tryAgainOnly&url=" +
                                             encodeURIComponent(src),
                                             null, null, null, null);
    let panel = aNotificationFrame.parentNode;
    sizeSocialPanelToContent(panel, aNotificationFrame);
  },

};


/**
 * SocialMarks
 *
 * Handles updates to toolbox and signals all buttons to update when necessary.
 */
SocialMarks = {
  update: function() {
    // signal each button to update itself
    let currentButtons = document.querySelectorAll('toolbarbutton[type="socialmark"]');
    for (let elt of currentButtons)
      elt.update();
  },

  getProviders: function() {
    // only rely on providers that the user has placed in the UI somewhere. This
    // also means that populateToolbarPalette must be called prior to using this
    // method, otherwise you get a big fat zero. For our use case with context
    // menu's, this is ok.
    let tbh = this._toolbarHelper;
    return [p for (p of Social.providers) if (p.markURL &&
                                              document.getElementById(tbh.idFromOrigin(p.origin)))];
  },

  populateContextMenu: function() {
    // only show a selection if enabled and there is more than one
    let providers = this.getProviders();

    // remove all previous entries by class
    let menus = [m for (m of document.getElementsByClassName("context-socialmarks"))];
    [m.parentNode.removeChild(m) for (m of menus)];

    let contextMenus = [
      {
        type: "link",
        id: "context-marklinkMenu",
        label: "social.marklinkMenu.label"
      },
      {
        type: "page",
        id: "context-markpageMenu",
        label: "social.markpageMenu.label"
      }
    ];
    for (let cfg of contextMenus) {
      this._populateContextPopup(cfg, providers);
    }
  },

  MENU_LIMIT: 3, // adjustable for testing
  _populateContextPopup: function(menuInfo, providers) {
    let menu = document.getElementById(menuInfo.id);
    let popup = menu.firstChild;
    for (let provider of providers) {
      // We show up to MENU_LIMIT providers as single menuitems's at the top
      // level of the context menu, if we have more than that, dump them *all*
      // into the menu popup.
      let mi = document.createElement("menuitem");
      mi.setAttribute("oncommand", "gContextMenu.markLink(this.getAttribute('origin'));");
      mi.setAttribute("origin", provider.origin);
      mi.setAttribute("image", provider.iconURL);
      if (providers.length <= this.MENU_LIMIT) {
        // an extra class to make enable/disable easy
        mi.setAttribute("class", "menuitem-iconic context-socialmarks context-mark"+menuInfo.type);
        let menuLabel = gNavigatorBundle.getFormattedString(menuInfo.label, [provider.name]);
        mi.setAttribute("label", menuLabel);
        menu.parentNode.insertBefore(mi, menu);
      } else {
        mi.setAttribute("class", "menuitem-iconic context-socialmarks");
        mi.setAttribute("label", provider.name);
        popup.appendChild(mi);
      }
    }
  },

  populateToolbarPalette: function() {
    this._toolbarHelper.populatePalette();
    this.populateContextMenu();
  },

  removeProvider: function(origin) {
    this._toolbarHelper.removeProviderButton(origin);
  },

  get _toolbarHelper() {
    delete this._toolbarHelper;
    this._toolbarHelper = new ToolbarHelper("social-mark-button", this._createButton.bind(this));
    return this._toolbarHelper;
  },

  _createButton: function(provider) {
    if (!provider.markURL)
      return;
    let aId = this._toolbarHelper.idFromOrigin(provider.origin);
    CustomizableUI.createWidget({
      id: aId,
      type: 'custom',
      removable: true,
      defaultArea: CustomizableUI.AREA_NAVBAR,
      onBuild: function(document) {
        let window = document.defaultView;

        let node = document.createElement('toolbarbutton');

        node.id = this.id;
        node.setAttribute('class', 'toolbarbutton-1 chromeclass-toolbar-additional social-mark-button');
        node.setAttribute('type', "socialmark");
        node.style.listStyleImage = "url(" + provider.iconURL + ")";
        node.setAttribute("origin", provider.origin);

        return node;
      }
    });
  },

  markLink: function(aOrigin, aUrl) {
    // find the button for this provider, and open it
    let id = this._toolbarHelper.idFromOrigin(aOrigin);
    document.getElementById(id).markLink(aUrl);
  }
};

})();
