/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

XPCOMUtils.defineLazyModuleGetter(this, "CustomizableUI",
                                  "resource:///modules/CustomizableUI.jsm");
/**
 * Maintains the state and dispatches events for the main menu panel.
 */

const PanelUI = {
  /** Panel events that we listen for. **/
  get kEvents() ["popupshowing", "popupshown", "popuphiding", "popuphidden"],
  /**
   * Used for lazily getting and memoizing elements from the document. Lazy
   * getters are set in init, and memoizing happens after the first retrieval.
   */
  get kElements() {
    return {
      clickCapturer: "PanelUI-clickCapturer",
      container: "PanelUI-container",
      contents: "PanelUI-contents",
      mainView: "PanelUI-mainView",
      mainViewSpring: "PanelUI-mainView-spring",
      menuButton: "PanelUI-menu-button",
      panel: "PanelUI-popup",
      subViews: "PanelUI-subViews",
      viewStack: "PanelUI-viewStack"
    };
  },

  /**
   * Returns whether we're in subview mode. This can return true even if
   * the transition to subview mode is not yet complete.
   **/
  get _showingSubView() {
    return (this.viewStack.hasAttribute("view") &&
            this.viewStack.getAttribute("view") == "subview");
  },

  init: function() {
    for (let [k, v] of Iterator(this.kElements)) {
      // Need to do fresh let-bindings per iteration
      let getKey = k;
      let id = v;
      this.__defineGetter__(getKey, function() {
        delete this[getKey];
        return this[getKey] = document.getElementById(id);
      });
    }

    for (let event of this.kEvents) {
      this.panel.addEventListener(event, this);
    }

    this.clickCapturer.addEventListener("click", this._onCapturerClick,
                                        true);

    var self = this;
    this.subViews.addEventListener("overflow", function() {
      // Resize the subview on the next tick.
      Services.tm.currentThread.dispatch(self._syncContainerWithSubView.bind(self),
        Ci.nsIThread.DISPATCH_NORMAL);
    });

    // Get a MutationObserver ready to react to subview size changes. We
    // only attach this MutationObserver when a subview is being displayed.
    this._subViewObserver = new MutationObserver(function(aMutations) {
      this._syncContainerWithSubView();
    }.bind(this));
  },

  uninit: function() {
    for (let event of this.kEvents) {
      this.panel.removeEventListener(event, this);
    }

    this.clickCapturer.removeEventListener("click", this._onCapturerClick,
                                           true);
  },

  /**
   * Customize mode extracts the mainView and puts it somewhere else while the
   * user customizes. Upon completion, this function can be called to put the
   * panel back to where it belongs in normal browsing mode.
   *
   * @param aMainView
   *        The mainView node to put back into place.
   */
  replaceMainView: function(aMainView) {
    this.viewStack.insertBefore(aMainView, this.viewStack.firstChild);
    this._syncContainerWithMainView();
  },

  /**
   * Opens the menu panel if it's closed, or closes it if it's
   * open. If the event target has a child with the toolbarbutton-icon
   * attribute, the panel will be anchored on that child. Otherwise, the panel
   * is anchored on the event target itself.
   *
   * @param aEvent the event that triggers the toggle.
   */
  toggle: function(aEvent) {
    if (this.panel.state == "open") {
      this.hide();
    } else if (this.panel.state == "closed") {
      let anchor = aEvent.target;
      let iconAnchor =
        document.getAnonymousElementByAttribute(anchor, "class",
                                                "toolbarbutton-icon");
      this.panel.openPopup(iconAnchor || anchor, "bottomcenter topright");
    }
  },

  /**
   * If the menu panel is being shown, hide it.
   */
  hide: function() {
    this.panel.hidePopup();
  },

  handleEvent: function(aEvent) {
    switch (aEvent.type) {
      case "popupshowing":
        this.ensureRegistered();
        let cstyle = window.getComputedStyle(this.viewStack, null);
        this.container.style.height = cstyle.getPropertyValue("height");
        this.container.style.width = cstyle.getPropertyValue("width");
        // Fall through
      case "popupshown":
        // Fall through
      case "popuphiding":
        // Fall through
      case "popuphidden": {
        this.showMainView();
        this._updatePanelButton(aEvent.target);
        break;
      }
    }
  },

  /**
   * Registering the menu panel is done lazily for performance reasons. This
   * method is exposed so that CustomizationMode can force registration in the
   * event that customization mode is started before the panel has had a chance
   * to register itself.
   */
  ensureRegistered: function() {
    CustomizableUI.registerMenuPanel(this.contents);
  },

  /**
   * Switch the panel to the main view if it's not already
   * in that view.
   */
  showMainView: function() {
    // Are we showing a subview? If so, fire the ViewHiding event on it.
    if (this._showingSubView) {
      let viewNode = this._currentSubView;
      let evt = document.createEvent("CustomEvent");
      evt.initCustomEvent("ViewHiding", true, true, viewNode);
      viewNode.dispatchEvent(evt);

      viewNode.removeAttribute("current");
      this._currentSubView = null;
      this._subViewObserver.disconnect();
    }

    this.viewStack.setAttribute("view", "main");
    this._syncContainerWithMainView();

    this._shiftMainView();
  },

  /**
   * Shows a subview in the panel with a given ID.
   *
   * @param aViewId the ID of the subview to show.
   * @param aAnchor the element that spawned the subview.
   * @param aPlacementArea the CustomizableUI area that aAnchor is in.
   */
  showSubView: function(aViewId, aAnchor, aPlacementArea) {
    let viewNode = document.getElementById(aViewId);
    if (!viewNode) {
      Cu.reportError("Could not show panel subview with id: " + aViewId);
      return;
    }

    if (!aAnchor) {
      Cu.reportError("Expected an anchor when opening subview with id: " + aViewId);
      return;
    }

    if (aPlacementArea == CustomizableUI.AREA_PANEL) {
      // The subview can get moved if the widget is not in the
      // panel. For robustness, check that the subview is in the
      // right place and move it if necessary.
      if (viewNode.parentElement != this.subViews) {
        this.subViews.appendChild(viewNode);
      }

      let oldHeight = this.mainView.clientHeight;
      viewNode.setAttribute("current", true);
      this._currentSubView = viewNode;

      // Emit the ViewShowing event so that the widget definition has a chance
      // to lazily populate the subview with things.
      let evt = document.createEvent("CustomEvent");
      evt.initCustomEvent("ViewShowing", true, true, viewNode);
      viewNode.dispatchEvent(evt);

      this.viewStack.setAttribute("view", "subview");
      this.mainViewSpring.style.height = this.subViews.scrollHeight - oldHeight + "px";
      this.container.style.height = this.subViews.scrollHeight + "px";

      // Now we have to transition to transition the panel. There are a few parts
      // to this:
      //
      // 1) The main view content gets shifted so that the center of the anchor
      //    node is at the left-most edge of the panel.
      // 2) The subview deck slides in so that it takes up almost all of the
      //    panel.
      // 3) If the subview is taller then the main panel contents, then the panel
      //    must grow to meet that new height. Otherwise, it must shrink.
      //
      // All three of these actions make use of CSS transformations, so they
      // should all occur simultaneously.
      this._shiftMainView(aAnchor);
      this._subViewObserver.observe(viewNode, {
        attributes: true,
        characterData: true,
        childList: true,
        subtree: true
      });
    } else {
      // Emit the ViewShowing event so that the widget definition has a chance
      // to lazily populate the subview with things.
      let evt = document.createEvent("CustomEvent");
      evt.initCustomEvent("ViewShowing", true, true, viewNode);
      viewNode.dispatchEvent(evt);

      let panel = document.createElement("panel");
      panel.appendChild(viewNode);
      panel.setAttribute("type", "arrow");
      panel.setAttribute("id", "customizationui-widget-panel");
      document.getElementById(CustomizableUI.AREA_NAVBAR).appendChild(panel);
      panel.addEventListener("popuphidden", function() {
        this.subViews.appendChild(viewNode);
        panel.parentElement.removeChild(panel);
      });

      panel.openPopup(aAnchor, "bottomcenter topright");
    }
  },

  /**
   * Ensures that the container has the same height as the main view, minus
   * any spring height. This is usually done after customization completes, or
   * when switching back from a subview.
   */
  _syncContainerWithMainView: function() {
    let springHeight = this.mainViewSpring.getBoundingClientRect().height;
    this.container.style.height = (this.mainView.scrollHeight - springHeight) + "px";
    this.mainViewSpring.style.height = "";
  },

  /**
   * Ensures that the container has the same height as the subview holder.
   * This is usually done when switching to a subview, or if a subview changes
   * in size.
   */
  _syncContainerWithSubView: function() {
    let springHeight = this.mainViewSpring.getBoundingClientRect().height;
    let mainViewHeight = this.mainView.clientHeight - springHeight;
    this.container.style.height = this.subViews.scrollHeight + "px";
    this.mainViewSpring.style.height = (this.subViews.scrollHeight - mainViewHeight) + "px";
  },

  /**
   * Sets the anchor node into the open or closed state, depending
   * on the state of the panel.
   */
  _updatePanelButton: function() {
    this.menuButton.open = this.panel.state == "open" ||
                           this.panel.state == "showing";
  },

  /**
   * Used to keep a reference to the toolbarbutton that the subview spawned from.
   */
  anchorElement: null,

  /**
   * If aAnchor is not null, this shifts the main view content so that it is
   * partially clipped by the panel boundaries, placing the center of aAnchor
   * at the clipping edge. If aAnchor is undefined or null, the main view
   * content is shifted back to its original position.
   */
  _shiftMainView: function(aAnchor) {
    if (aAnchor) {
      // We need to find the left edge of the anchor, relative to the main
      // panel. Then we need to add half the width of the anchor. This is the
      // target that we need to transition to.
      let anchorRect = aAnchor.getBoundingClientRect();
      let mainViewRect = this.mainView.getBoundingClientRect();
      let leftEdge = anchorRect.left - mainViewRect.left;
      let center = aAnchor.clientWidth / 2;
      let target = leftEdge + center;
      this.mainView.style.transform = "translateX(-" + target + "px)";
      aAnchor.classList.add("panelui-mainview-anchor");
    } else {
      this.mainView.style.transform = "";
      if (this.anchorElement)
        this.anchorElement.classList.remove("panelui-mainview-anchor");
    }
    this.anchorElement = aAnchor;
  },

  /**
   * When viewing a subview, any click on the main view gets intercepted by our
   * capturer, and then we switch to the main view.
   */
  _onCapturerClick: function(aEvent) {
    PanelUI.showMainView();
  },
};
