/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Services",
                                  "resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "WebConsoleUtils",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "PageErrorListener",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "ConsoleAPIListener",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "ConsoleProgressListener",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "JSTermHelpers",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "JSPropertyProvider",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "NetworkMonitor",
                                  "resource://gre/modules/devtools/WebConsoleUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "ConsoleAPIStorage",
                                  "resource://gre/modules/ConsoleAPIStorage.jsm");


/**
 * The WebConsoleActor implements capabilities needed for the Web Console
 * feature.
 *
 * @constructor
 * @param object aConnection
 *        The connection to the client, DebuggerServerConnection.
 * @param object [aParentActor]
 *        Optional, the parent actor.
 */
function WebConsoleActor(aConnection, aParentActor)
{
  this.conn = aConnection;

  if (aParentActor instanceof BrowserTabActor &&
      aParentActor.browser instanceof Ci.nsIDOMWindow) {
    // B2G tab actor |this.browser| points to a DOM chrome window, not
    // a xul:browser element.
    //
    // TODO: bug 802246 - b2g has only one tab actor, the shell.xul, which is
    // not properly supported by the console actor - see bug for details.
    //
    // Below we work around the problem: selecting the shell.xul tab actor
    // behaves as if the user picked the global console actor.
    //this._window = aParentActor.browser;
    this._window = Services.wm.getMostRecentWindow("navigator:browser");
    this._isGlobalActor = true;
  }
  else if (aParentActor instanceof BrowserTabActor &&
           aParentActor.browser instanceof Ci.nsIDOMElement) {
    // Firefox for desktop tab actor |this.browser| points to the xul:browser
    // element.
    this._window = aParentActor.browser.contentWindow;
  }
  else {
    // In all other cases we should behave as the global console actor.
    this._window = Services.wm.getMostRecentWindow("navigator:browser");
    this._isGlobalActor = true;
  }

  this._actorPool = new ActorPool(this.conn);
  this.conn.addActorPool(this._actorPool);

  this._prefs = {};

  this.dbg = new Debugger();

  this._protoChains = new Map();
  this._dbgGlobals = new Map();
  this._getDebuggerGlobal(this.window);

  this._onObserverNotification = this._onObserverNotification.bind(this);
  Services.obs.addObserver(this._onObserverNotification,
                           "inner-window-destroyed", false);
}

WebConsoleActor.prototype =
{
  /**
   * Debugger instance.
   *
   * @see jsdebugger.jsm
   */
  dbg: null,

  /**
   * Tells if this Web Console actor is a global actor or not.
   * @private
   * @type boolean
   */
  _isGlobalActor: false,

  /**
   * Actor pool for all of the actors we send to the client.
   * @private
   * @type object
   * @see ActorPool
   */
  _actorPool: null,

  /**
   * Web Console-related preferences.
   * @private
   * @type object
   */
  _prefs: null,

  /**
   * Tells the current inner ID of |this.window|. When the page is navigated, we
   * need to recreate the jsterm helpers.
   * @private
   * @type number
   */
  _globalWindowId: 0,

  /**
   * Holds a map between inner window IDs and Debugger.Objects for the window
   * objects.
   * @private
   * @type Map
   */
  _dbgGlobals: null,

  /**
   * Object that holds the JSTerm API, the helper functions, for the default
   * window object.
   *
   * @see this._getJSTermHelpers()
   * @private
   * @type object
   */
  _jstermHelpers: null,

  /**
   * A cache of prototype chains for objects that have received a
   * prototypeAndProperties request.
   *
   * @private
   * @type Map
   * @see dbg-script-actors.js, ThreadActor._protoChains
   */
  _protoChains: null,

  /**
   * The debugger server connection instance.
   * @type object
   */
  conn: null,

  /**
   * The content window we work with.
   * @type nsIDOMWindow
   */
  get window() this._window,

  _window: null,

  /**
   * The PageErrorListener instance.
   * @type object
   */
  pageErrorListener: null,

  /**
   * The ConsoleAPIListener instance.
   */
  consoleAPIListener: null,

  /**
   * The NetworkMonitor instance.
   */
  networkMonitor: null,

  /**
   * The ConsoleProgressListener instance.
   */
  consoleProgressListener: null,

  /**
   * Getter for the NetworkMonitor.saveRequestAndResponseBodies preference.
   * @type boolean
   */
  get saveRequestAndResponseBodies()
    this._prefs["NetworkMonitor.saveRequestAndResponseBodies"],

  actorPrefix: "console",

  grip: function WCA_grip()
  {
    return { actor: this.actorID };
  },

  hasNativeConsoleAPI: BrowserTabActor.prototype.hasNativeConsoleAPI,

  _createValueGrip: ThreadActor.prototype.createValueGrip,
  _stringIsLong: ThreadActor.prototype._stringIsLong,
  _findProtoChain: ThreadActor.prototype._findProtoChain,
  _removeFromProtoChain: ThreadActor.prototype._removeFromProtoChain,

  /**
   * Destroy the current WebConsoleActor instance.
   */
  disconnect: function WCA_disconnect()
  {
    if (this.pageErrorListener) {
      this.pageErrorListener.destroy();
      this.pageErrorListener = null;
    }
    if (this.consoleAPIListener) {
      this.consoleAPIListener.destroy();
      this.consoleAPIListener = null;
    }
    if (this.networkMonitor) {
      this.networkMonitor.destroy();
      this.networkMonitor = null;
    }
    if (this.consoleProgressListener) {
      this.consoleProgressListener.destroy();
      this.consoleProgressListener = null;
    }
    this.conn.removeActorPool(this._actorPool);
    Services.obs.removeObserver(this._onObserverNotification,
                                "inner-window-destroyed");
    this._actorPool = null;
    this._protoChains.clear();
    this._dbgGlobals.clear();
    this._jstermHelpers = null;
    this.dbg.enabled = false;
    this.dbg = null;
    this._globalWindowId = 0;
    this.conn = this._window = null;
  },

  /**
   * Create a grip for the given value.
   *
   * @param mixed aValue
   * @return object
   */
  createValueGrip: function WCA_createValueGrip(aValue)
  {
    return this._createValueGrip(aValue, this._actorPool);
  },

  /**
   * Make a debuggee value for the given value.
   *
   * @param mixed aValue
   *        The value you want to get a debuggee value for.
   * @param boolean aUseObjectGlobal
   *        If |true| the object global is determined and added as a debuggee,
   *        otherwise |this.window| is used when makeDebuggeeValue() is invoked.
   * @return object
   *         Debuggee value for |aValue|.
   */
  makeDebuggeeValue: function WCA_makeDebuggeeValue(aValue, aUseObjectGlobal)
  {
    let global = this.window;
    if (aUseObjectGlobal && typeof aValue == "object") {
      try {
        global = Cu.getGlobalForObject(aValue);
      }
      catch (ex) {
        // The above can throw an exception if aValue is not an actual object.
      }
    }
    let dbgGlobal = null;
    try {
      dbgGlobal = this._getDebuggerGlobal(global);
    }
    catch (ex) {
      // The above call can throw in addDebuggee() if the given global object
      // is already in the stackframe of code that is executing now. Console.jsm
      // and the Browser Console can cause this case.
      dbgGlobal = this._getDebuggerGlobal(this.window);
    }
    return dbgGlobal.makeDebuggeeValue(aValue);
  },

  /**
   * Create a grip for the given object.
   *
   * @param object aObject
   *        The object you want.
   * @param object aPool
   *        An ActorPool where the new actor instance is added.
   * @param object
   *        The object grip.
   */
  objectGrip: function WCA_objectGrip(aObject, aPool)
  {
    let actor = new ObjectActor(aObject, this);
    aPool.addActor(actor);
    return actor.grip();
  },

  /**
   * Create a grip for the given string.
   *
   * @param string aString
   *        The string you want to create the grip for.
   * @param object aPool
   *        An ActorPool where the new actor instance is added.
   * @return object
   *         A LongStringActor object that wraps the given string.
   */
  longStringGrip: function WCA_longStringGrip(aString, aPool)
  {
    let actor = new LongStringActor(aString, this);
    aPool.addActor(actor);
    return actor.grip();
  },

  /**
   * Get an object actor by its ID.
   *
   * @param string aActorID
   * @return object
   */
  getActorByID: function WCA_getActorByID(aActorID)
  {
    return this._actorPool.get(aActorID);
  },

  /**
   * Release an actor.
   *
   * @param object aActor
   *        The actor instance you want to release.
   */
  releaseActor: function WCA_releaseActor(aActor)
  {
    this._actorPool.removeActor(aActor.actorID);
  },

  //////////////////
  // Request handlers for known packet types.
  //////////////////

  /**
   * Handler for the "startListeners" request.
   *
   * @param object aRequest
   *        The JSON request object received from the Web Console client.
   * @return object
   *         The response object which holds the startedListeners array.
   */
  onStartListeners: function WCA_onStartListeners(aRequest)
  {
    let startedListeners = [];
    let window = !this._isGlobalActor ? this.window : null;

    while (aRequest.listeners.length > 0) {
      let listener = aRequest.listeners.shift();
      switch (listener) {
        case "PageError":
          if (!this.pageErrorListener) {
            this.pageErrorListener =
              new PageErrorListener(window, this);
            this.pageErrorListener.init();
          }
          startedListeners.push(listener);
          break;
        case "ConsoleAPI":
          if (!this.consoleAPIListener) {
            this.consoleAPIListener =
              new ConsoleAPIListener(window, this);
            this.consoleAPIListener.init();
          }
          startedListeners.push(listener);
          break;
        case "NetworkActivity":
          if (!this.networkMonitor) {
            this.networkMonitor =
              new NetworkMonitor(window, this);
            this.networkMonitor.init();
          }
          startedListeners.push(listener);
          break;
        case "FileActivity":
          if (!this.consoleProgressListener) {
            this.consoleProgressListener =
              new ConsoleProgressListener(this.window, this);
          }
          this.consoleProgressListener.startMonitor(this.consoleProgressListener.
                                                    MONITOR_FILE_ACTIVITY);
          startedListeners.push(listener);
          break;
      }
    }
    return {
      startedListeners: startedListeners,
      nativeConsoleAPI: this.hasNativeConsoleAPI(this.window),
    };
  },

  /**
   * Handler for the "stopListeners" request.
   *
   * @param object aRequest
   *        The JSON request object received from the Web Console client.
   * @return object
   *         The response packet to send to the client: holds the
   *         stoppedListeners array.
   */
  onStopListeners: function WCA_onStopListeners(aRequest)
  {
    let stoppedListeners = [];

    // If no specific listeners are requested to be detached, we stop all
    // listeners.
    let toDetach = aRequest.listeners ||
                   ["PageError", "ConsoleAPI", "NetworkActivity",
                    "FileActivity"];

    while (toDetach.length > 0) {
      let listener = toDetach.shift();
      switch (listener) {
        case "PageError":
          if (this.pageErrorListener) {
            this.pageErrorListener.destroy();
            this.pageErrorListener = null;
          }
          stoppedListeners.push(listener);
          break;
        case "ConsoleAPI":
          if (this.consoleAPIListener) {
            this.consoleAPIListener.destroy();
            this.consoleAPIListener = null;
          }
          stoppedListeners.push(listener);
          break;
        case "NetworkActivity":
          if (this.networkMonitor) {
            this.networkMonitor.destroy();
            this.networkMonitor = null;
          }
          stoppedListeners.push(listener);
          break;
        case "FileActivity":
          if (this.consoleProgressListener) {
            this.consoleProgressListener.stopMonitor(this.consoleProgressListener.
                                                     MONITOR_FILE_ACTIVITY);
          }
          stoppedListeners.push(listener);
          break;
      }
    }

    return { stoppedListeners: stoppedListeners };
  },

  /**
   * Handler for the "getCachedMessages" request. This method sends the cached
   * error messages and the window.console API calls to the client.
   *
   * @param object aRequest
   *        The JSON request object received from the Web Console client.
   * @return object
   *         The response packet to send to the client: it holds the cached
   *         messages array.
   */
  onGetCachedMessages: function WCA_onGetCachedMessages(aRequest)
  {
    let types = aRequest.messageTypes;
    if (!types) {
      return {
        error: "missingParameter",
        message: "The messageTypes parameter is missing.",
      };
    }

    let messages = [];

    while (types.length > 0) {
      let type = types.shift();
      switch (type) {
        case "ConsoleAPI":
          if (this.consoleAPIListener) {
            let cache = this.consoleAPIListener.getCachedMessages();
            cache.forEach(function(aMessage) {
              let message = this.prepareConsoleMessageForRemote(aMessage);
              message._type = type;
              messages.push(message);
            }, this);
          }
          break;
        case "PageError":
          if (this.pageErrorListener) {
            let cache = this.pageErrorListener.getCachedMessages();
            cache.forEach(function(aMessage) {
              let message = this.preparePageErrorForRemote(aMessage);
              message._type = type;
              messages.push(message);
            }, this);
          }
          break;
      }
    }

    messages.sort(function(a, b) { return a.timeStamp - b.timeStamp; });

    return {
      from: this.actorID,
      messages: messages,
    };
  },

  /**
   * Handler for the "evaluateJS" request. This method evaluates the given
   * JavaScript string and sends back the result.
   *
   * @param object aRequest
   *        The JSON request object received from the Web Console client.
   * @return object
   *         The evaluation response packet.
   */
  onEvaluateJS: function WCA_onEvaluateJS(aRequest)
  {
    let input = aRequest.text;
    let timestamp = Date.now();

    let evalOptions = {
      bindObjectActor: aRequest.bindObjectActor,
      frameActor: aRequest.frameActor,
    };
    let evalInfo = this.evalWithDebugger(input, evalOptions);
    let evalResult = evalInfo.result;
    let helperResult = evalInfo.helperResult;

    let result, error, errorMessage;
    if (evalResult) {
      if ("return" in evalResult) {
        result = evalResult.return;
      }
      else if ("yield" in evalResult) {
        result = evalResult.yield;
      }
      else if ("throw" in evalResult) {
        error = evalResult.throw;
        let errorToString = evalInfo.window
                            .evalInGlobalWithBindings("ex + ''", {ex: error});
        if (errorToString && typeof errorToString.return == "string") {
          errorMessage = errorToString.return;
        }
      }
    }

    return {
      from: this.actorID,
      input: input,
      result: this.createValueGrip(result),
      timestamp: timestamp,
      exception: error ? this.createValueGrip(error) : null,
      exceptionMessage: errorMessage,
      helperResult: helperResult,
    };
  },

  /**
   * The Autocomplete request handler.
   *
   * @param object aRequest
   *        The request message - what input to autocomplete.
   * @return object
   *         The response message - matched properties.
   */
  onAutocomplete: function WCA_onAutocomplete(aRequest)
  {
    // TODO: Bug 842682 - use the debugger API for autocomplete in the Web
    // Console, and provide suggestions from the selected debugger stack frame.
    let result = JSPropertyProvider(this.window, aRequest.text) || {};
    return {
      from: this.actorID,
      matches: result.matches || [],
      matchProp: result.matchProp,
    };
  },

  /**
   * The "clearMessagesCache" request handler.
   */
  onClearMessagesCache: function WCA_onClearMessagesCache()
  {
    // TODO: Bug 717611 - Web Console clear button does not clear cached errors
    let windowId = !this._isGlobalActor ?
                   WebConsoleUtils.getInnerWindowId(this.window) : null;
    ConsoleAPIStorage.clearEvents(windowId);
    return {};
  },

  /**
   * The "setPreferences" request handler.
   *
   * @param object aRequest
   *        The request message - which preferences need to be updated.
   */
  onSetPreferences: function WCA_onSetPreferences(aRequest)
  {
    for (let key in aRequest.preferences) {
      this._prefs[key] = aRequest.preferences[key];
    }
    return { updated: Object.keys(aRequest.preferences) };
  },

  //////////////////
  // End of request handlers.
  //////////////////

  /**
   * Get the Debugger.Object for the given global object (usually a window
   * object).
   *
   * @private
   * @param object aGlobal
   *        The global object for which you want a Debugger.Object.
   * @return Debugger.Object
   *         The Debugger.Object for the given global object.
   */
  _getDebuggerGlobal: function WCA__getDebuggerGlobal(aGlobal)
  {
    let windowId = WebConsoleUtils.getInnerWindowId(aGlobal);
    if (!this._dbgGlobals.has(windowId)) {
      let dbgGlobal = this.dbg.addDebuggee(aGlobal);
      this.dbg.removeDebuggee(aGlobal);
      this._dbgGlobals.set(windowId, dbgGlobal);
    }
    return this._dbgGlobals.get(windowId);
  },

  /**
   * Create an object with the API we expose to the Web Console during
   * JavaScript evaluation.
   * This object inherits properties and methods from the Web Console actor.
   *
   * @private
   * @param object aDebuggerGlobal
   *        A Debugger.Object that wraps a content global. This is used for the
   *        JSTerm helpers.
   * @return object
   *         The same object as |this|, but with an added |sandbox| property.
   *         The sandbox holds methods and properties that can be used as
   *         bindings during JS evaluation.
   */
  _getJSTermHelpers: function WCA__getJSTermHelpers(aDebuggerGlobal)
  {
    let helpers = Object.create(this);
    helpers.sandbox = Object.create(null);
    JSTermHelpers(helpers);

    // Make sure the helpers can be used during eval.
    for (let name in helpers.sandbox) {
      let desc = Object.getOwnPropertyDescriptor(helpers.sandbox, name);
      if (desc.get || desc.set) {
        continue;
      }
      helpers.sandbox[name] = aDebuggerGlobal.makeDebuggeeValue(desc.value);
    }
    return helpers;
  },

  /**
   * Evaluates a string using the debugger API.
   *
   * To allow the variables view to update properties from the Web Console we
   * provide the "bindObjectActor" mechanism: the Web Console tells the
   * ObjectActor ID for which it desires to evaluate an expression. The
   * Debugger.Object pointed at by the actor ID is bound such that it is
   * available during expression evaluation (evalInGlobalWithBindings()).
   *
   * Example:
   *   _self['foobar'] = 'test'
   * where |_self| refers to the desired object.
   *
   * The |frameActor| property allows the Web Console client to provide the
   * frame actor ID, such that the expression can be evaluated in the
   * user-selected stack frame.
   *
   * For the above to work we need the debugger and the Web Console to share
   * a connection, otherwise the Web Console actor will not find the frame
   * actor.
   *
   * The Debugger.Frame comes from the jsdebugger's Debugger instance, which
   * is different from the Web Console's Debugger instance. This means that
   * for evaluation to work, we need to create a new instance for the jsterm
   * helpers - they need to be Debugger.Objects coming from the jsdebugger's
   * Debugger instance.
   *
   * When |bindObjectActor| is used objects can come from different iframes,
   * from different domains. To avoid permission-related errors when objects
   * come from a different window, we also determine the object's own global,
   * such that evaluation happens in the context of that global. This means that
   * evaluation will happen in the object's iframe, rather than the top level
   * window.
   *
   * @param string aString
   *        String to evaluate.
   * @param object [aOptions]
   *        Options for evaluation:
   *        - bindObjectActor: the ObjectActor ID to use for evaluation.
   *          |evalWithBindings()| will be called with one additional binding:
   *          |_self| which will point to the Debugger.Object of the given
   *          ObjectActor.
   *        - frameActor: the FrameActor ID to use for evaluation. The given
   *        debugger frame is used for evaluation, instead of the global window.
   * @return object
   *         An object that holds the following properties:
   *         - dbg: the debugger where the string was evaluated.
   *         - frame: (optional) the frame where the string was evaluated.
   *         - window: the Debugger.Object for the global where the string was
   *         evaluated.
   *         - result: the result of the evaluation.
   *         - helperResult: any result coming from a JSTerm helper function.
   */
  evalWithDebugger: function WCA_evalWithDebugger(aString, aOptions = {})
  {
    // The help function needs to be easy to guess, so we make the () optional.
    if (aString.trim() == "help" || aString.trim() == "?") {
      aString = "help()";
    }

    let bindSelf = null;

    if (aOptions.bindObjectActor) {
      let objActor = this.getActorByID(aOptions.bindObjectActor);
      if (objActor) {
        bindSelf = objActor.obj;
      }
    }

    let frame = null, frameActor = null;
    if (aOptions.frameActor) {
      frameActor = this.conn.getActor(aOptions.frameActor);
      if (frameActor) {
        frame = frameActor.frame;
      }
      else {
        Cu.reportError("Web Console Actor: the frame actor was not found: " +
                       aOptions.frameActor);
      }
    }

    let dbg = this.dbg;
    let dbgWindow = null;
    let helpers = null;
    let found$ = false, found$$ = false;

    // Determine which debugger to use, depending on the presence of the
    // stackframe.
    if (frame) {
      // Avoid having bindings from a different Debugger. The Debugger.Frame
      // comes from the jsdebugger's Debugger instance.
      dbg = frameActor.threadActor.dbg;
      dbgWindow = dbg.addDebuggee(this.window);
      helpers = this._getJSTermHelpers(dbgWindow);

      let env = frame.environment;
      if (env) {
        found$ = !!env.find("$");
        found$$ = !!env.find("$$");
      }
    }
    else {
      // Use the Web Console debugger object.
      dbgWindow = this._getDebuggerGlobal(this.window);

      let windowId = WebConsoleUtils.getInnerWindowId(this.window);
      if (this._globalWindowId != windowId) {
        this._jstermHelpers = null;
        this._globalWindowId = windowId;
      }
      if (!this._jstermHelpers) {
        this._jstermHelpers = this._getJSTermHelpers(dbgWindow);
      }

      helpers = this._jstermHelpers;
      found$ = !!dbgWindow.getOwnPropertyDescriptor("$");
      found$$ = !!dbgWindow.getOwnPropertyDescriptor("$$");
    }

    let bindings = helpers.sandbox;
    if (bindSelf) {
      // Determine the global of the given JavaScript object.
      let jsObj = bindSelf.unsafeDereference();
      let global = Cu.getGlobalForObject(jsObj);

      if (global != this.window) {
        dbgWindow = dbg.addDebuggee(global);
        if (dbg == this.dbg) {
          dbg.removeDebuggee(global);
        }
      }

      bindings._self = dbgWindow.makeDebuggeeValue(jsObj);
    }

    let $ = null, $$ = null;
    if (found$) {
      $ = bindings.$;
      delete bindings.$;
    }
    if (found$$) {
      $$ = bindings.$$;
      delete bindings.$$;
    }

    helpers.evalInput = aString;

    let result;
    if (frame) {
      result = frame.evalWithBindings(aString, bindings);
    }
    else {
      result = dbgWindow.evalInGlobalWithBindings(aString, bindings);
    }

    let helperResult = helpers.helperResult;
    delete helpers.evalInput;
    delete helpers.helperResult;

    if ($) {
      bindings.$ = $;
    }
    if ($$) {
      bindings.$$ = $$;
    }

    if (bindings._self) {
      delete bindings._self;
    }

    return {
      result: result,
      helperResult: helperResult,
      dbg: dbg,
      frame: frame,
      window: dbgWindow,
    };
  },

  //////////////////
  // Event handlers for various listeners.
  //////////////////

  /**
   * Handler for page errors received from the PageErrorListener. This method
   * sends the nsIScriptError to the remote Web Console client.
   *
   * @param nsIScriptError aPageError
   *        The page error we need to send to the client.
   */
  onPageError: function WCA_onPageError(aPageError)
  {
    let packet = {
      from: this.actorID,
      type: "pageError",
      pageError: this.preparePageErrorForRemote(aPageError),
    };
    this.conn.send(packet);
  },

  /**
   * Prepare an nsIScriptError to be sent to the client.
   *
   * @param nsIScriptError aPageError
   *        The page error we need to send to the client.
   * @return object
   *         The object you can send to the remote client.
   */
  preparePageErrorForRemote: function WCA_preparePageErrorForRemote(aPageError)
  {
    return {
      message: aPageError.message,
      errorMessage: aPageError.errorMessage,
      sourceName: aPageError.sourceName,
      lineText: aPageError.sourceLine,
      lineNumber: aPageError.lineNumber,
      columnNumber: aPageError.columnNumber,
      category: aPageError.category,
      timeStamp: aPageError.timeStamp,
      warning: !!(aPageError.flags & aPageError.warningFlag),
      error: !!(aPageError.flags & aPageError.errorFlag),
      exception: !!(aPageError.flags & aPageError.exceptionFlag),
      strict: !!(aPageError.flags & aPageError.strictFlag),
    };
  },

  /**
   * Handler for window.console API calls received from the ConsoleAPIListener.
   * This method sends the object to the remote Web Console client.
   *
   * @see ConsoleAPIListener
   * @param object aMessage
   *        The console API call we need to send to the remote client.
   */
  onConsoleAPICall: function WCA_onConsoleAPICall(aMessage)
  {
    let packet = {
      from: this.actorID,
      type: "consoleAPICall",
      message: this.prepareConsoleMessageForRemote(aMessage),
    };
    this.conn.send(packet);
  },

  /**
   * Handler for network events. This method is invoked when a new network event
   * is about to be recorded.
   *
   * @see NetworkEventActor
   * @see NetworkMonitor from WebConsoleUtils.jsm
   *
   * @param object aEvent
   *        The initial network request event information.
   * @return object
   *         A new NetworkEventActor is returned. This is used for tracking the
   *         network request and response.
   */
  onNetworkEvent: function WCA_onNetworkEvent(aEvent)
  {
    let actor = new NetworkEventActor(aEvent, this);
    this._actorPool.addActor(actor);

    let packet = {
      from: this.actorID,
      type: "networkEvent",
      eventActor: actor.grip(),
    };

    this.conn.send(packet);

    return actor;
  },

  /**
   * Handler for file activity. This method sends the file request information
   * to the remote Web Console client.
   *
   * @see ConsoleProgressListener
   * @param string aFileURI
   *        The requested file URI.
   */
  onFileActivity: function WCA_onFileActivity(aFileURI)
  {
    let packet = {
      from: this.actorID,
      type: "fileActivity",
      uri: aFileURI,
    };
    this.conn.send(packet);
  },

  //////////////////
  // End of event handlers for various listeners.
  //////////////////

  /**
   * Prepare a message from the console API to be sent to the remote Web Console
   * instance.
   *
   * @param object aMessage
   *        The original message received from console-api-log-event.
   * @return object
   *         The object that can be sent to the remote client.
   */
  prepareConsoleMessageForRemote:
  function WCA_prepareConsoleMessageForRemote(aMessage)
  {
    let result = WebConsoleUtils.cloneObject(aMessage);
    delete result.wrappedJSObject;

    result.arguments = Array.map(aMessage.arguments || [], (aObj) => {
      let dbgObj = this.makeDebuggeeValue(aObj, true);
      return this.createValueGrip(dbgObj);
    });

    return result;
  },

  /**
   * Find the XUL window that owns the content window.
   *
   * @return Window
   *         The XUL window that owns the content window.
   */
  chromeWindow: function WCA_chromeWindow()
  {
    let window = null;
    try {
      window = this.window.QueryInterface(Ci.nsIInterfaceRequestor)
             .getInterface(Ci.nsIWebNavigation).QueryInterface(Ci.nsIDocShell)
             .chromeEventHandler.ownerDocument.defaultView;
    }
    catch (ex) {
      // The above can fail because chromeEventHandler is not available for all
      // kinds of |this.window|.
    }

    return window;
  },

  /**
   * Notification observer for the "inner-window-destroyed" topic. This function
   * cleans up |this._dbgGlobals| when needed.
   *
   * @private
   * @param object aSubject
   *        Notification subject - in this case it is the inner window ID that
   *        was destroyed.
   */
  _onObserverNotification: function WCA__onObserverNotification(aSubject)
  {
    let windowId = aSubject.QueryInterface(Ci.nsISupportsPRUint64).data;
    if (this._dbgGlobals.has(windowId)) {
      this._dbgGlobals.delete(windowId);
    }
  },
};

WebConsoleActor.prototype.requestTypes =
{
  startListeners: WebConsoleActor.prototype.onStartListeners,
  stopListeners: WebConsoleActor.prototype.onStopListeners,
  getCachedMessages: WebConsoleActor.prototype.onGetCachedMessages,
  evaluateJS: WebConsoleActor.prototype.onEvaluateJS,
  autocomplete: WebConsoleActor.prototype.onAutocomplete,
  clearMessagesCache: WebConsoleActor.prototype.onClearMessagesCache,
  setPreferences: WebConsoleActor.prototype.onSetPreferences,
};

/**
 * Creates an actor for a network event.
 *
 * @constructor
 * @param object aNetworkEvent
 *        The network event you want to use the actor for.
 * @param object aWebConsoleActor
 *        The parent WebConsoleActor instance for this object.
 */
function NetworkEventActor(aNetworkEvent, aWebConsoleActor)
{
  this.parent = aWebConsoleActor;
  this.conn = this.parent.conn;

  this._startedDateTime = aNetworkEvent.startedDateTime;
  this._isXHR = aNetworkEvent.isXHR;

  this._request = {
    method: aNetworkEvent.method,
    url: aNetworkEvent.url,
    httpVersion: aNetworkEvent.httpVersion,
    headers: [],
    cookies: [],
    headersSize: aNetworkEvent.headersSize,
    postData: {},
  };

  this._response = {
    headers: [],
    cookies: [],
    content: {},
  };

  this._timings = {};
  this._longStringActors = new Set();

  this._discardRequestBody = aNetworkEvent.discardRequestBody;
  this._discardResponseBody = aNetworkEvent.discardResponseBody;
}

NetworkEventActor.prototype =
{
  _request: null,
  _response: null,
  _timings: null,
  _longStringActors: null,

  actorPrefix: "netEvent",

  /**
   * Returns a grip for this actor for returning in a protocol message.
   */
  grip: function NEA_grip()
  {
    return {
      actor: this.actorID,
      startedDateTime: this._startedDateTime,
      url: this._request.url,
      method: this._request.method,
      isXHR: this._isXHR
    };
  },

  /**
   * Releases this actor from the pool.
   */
  release: function NEA_release()
  {
    for (let grip of this._longStringActors) {
      let actor = this.parent.getActorByID(grip.actor);
      if (actor) {
        this.parent.releaseActor(actor);
      }
    }
    this._longStringActors = new Set();
    this.parent.releaseActor(this);
  },

  /**
   * Handle a protocol request to release a grip.
   */
  onRelease: function NEA_onRelease()
  {
    this.release();
    return {};
  },

  /**
   * The "getRequestHeaders" packet type handler.
   *
   * @return object
   *         The response packet - network request headers.
   */
  onGetRequestHeaders: function NEA_onGetRequestHeaders()
  {
    return {
      from: this.actorID,
      headers: this._request.headers,
      headersSize: this._request.headersSize,
    };
  },

  /**
   * The "getRequestCookies" packet type handler.
   *
   * @return object
   *         The response packet - network request cookies.
   */
  onGetRequestCookies: function NEA_onGetRequestCookies()
  {
    return {
      from: this.actorID,
      cookies: this._request.cookies,
    };
  },

  /**
   * The "getRequestPostData" packet type handler.
   *
   * @return object
   *         The response packet - network POST data.
   */
  onGetRequestPostData: function NEA_onGetRequestPostData()
  {
    return {
      from: this.actorID,
      postData: this._request.postData,
      postDataDiscarded: this._discardRequestBody,
    };
  },

  /**
   * The "getResponseHeaders" packet type handler.
   *
   * @return object
   *         The response packet - network response headers.
   */
  onGetResponseHeaders: function NEA_onGetResponseHeaders()
  {
    return {
      from: this.actorID,
      headers: this._response.headers,
      headersSize: this._response.headersSize,
    };
  },

  /**
   * The "getResponseCookies" packet type handler.
   *
   * @return object
   *         The response packet - network response cookies.
   */
  onGetResponseCookies: function NEA_onGetResponseCookies()
  {
    return {
      from: this.actorID,
      cookies: this._response.cookies,
    };
  },

  /**
   * The "getResponseContent" packet type handler.
   *
   * @return object
   *         The response packet - network response content.
   */
  onGetResponseContent: function NEA_onGetResponseContent()
  {
    return {
      from: this.actorID,
      content: this._response.content,
      contentDiscarded: this._discardResponseBody,
    };
  },

  /**
   * The "getEventTimings" packet type handler.
   *
   * @return object
   *         The response packet - network event timings.
   */
  onGetEventTimings: function NEA_onGetEventTimings()
  {
    return {
      from: this.actorID,
      timings: this._timings,
      totalTime: this._totalTime,
    };
  },

  /******************************************************************
   * Listeners for new network event data coming from NetworkMonitor.
   ******************************************************************/

  /**
   * Add network request headers.
   *
   * @param array aHeaders
   *        The request headers array.
   */
  addRequestHeaders: function NEA_addRequestHeaders(aHeaders)
  {
    this._request.headers = aHeaders;
    this._prepareHeaders(aHeaders);

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "requestHeaders",
      headers: aHeaders.length,
      headersSize: this._request.headersSize,
    };

    this.conn.send(packet);
  },

  /**
   * Add network request cookies.
   *
   * @param array aCookies
   *        The request cookies array.
   */
  addRequestCookies: function NEA_addRequestCookies(aCookies)
  {
    this._request.cookies = aCookies;
    this._prepareHeaders(aCookies);

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "requestCookies",
      cookies: aCookies.length,
    };

    this.conn.send(packet);
  },

  /**
   * Add network request POST data.
   *
   * @param object aPostData
   *        The request POST data.
   */
  addRequestPostData: function NEA_addRequestPostData(aPostData)
  {
    this._request.postData = aPostData;
    aPostData.text = this._createStringGrip(aPostData.text);
    if (typeof aPostData.text == "object") {
      this._longStringActors.add(aPostData.text);
    }

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "requestPostData",
      dataSize: aPostData.text.length,
      discardRequestBody: this._discardRequestBody,
    };

    this.conn.send(packet);
  },

  /**
   * Add the initial network response information.
   *
   * @param object aInfo
   *        The response information.
   */
  addResponseStart: function NEA_addResponseStart(aInfo)
  {
    this._response.httpVersion = aInfo.httpVersion;
    this._response.status = aInfo.status;
    this._response.statusText = aInfo.statusText;
    this._response.headersSize = aInfo.headersSize;
    this._discardResponseBody = aInfo.discardResponseBody;

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "responseStart",
      response: aInfo,
    };

    this.conn.send(packet);
  },

  /**
   * Add network response headers.
   *
   * @param array aHeaders
   *        The response headers array.
   */
  addResponseHeaders: function NEA_addResponseHeaders(aHeaders)
  {
    this._response.headers = aHeaders;
    this._prepareHeaders(aHeaders);

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "responseHeaders",
      headers: aHeaders.length,
      headersSize: this._response.headersSize,
    };

    this.conn.send(packet);
  },

  /**
   * Add network response cookies.
   *
   * @param array aCookies
   *        The response cookies array.
   */
  addResponseCookies: function NEA_addResponseCookies(aCookies)
  {
    this._response.cookies = aCookies;
    this._prepareHeaders(aCookies);

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "responseCookies",
      cookies: aCookies.length,
    };

    this.conn.send(packet);
  },

  /**
   * Add network response content.
   *
   * @param object aContent
   *        The response content.
   * @param boolean aDiscardedResponseBody
   *        Tells if the response content was recorded or not.
   */
  addResponseContent:
  function NEA_addResponseContent(aContent, aDiscardedResponseBody)
  {
    this._response.content = aContent;
    aContent.text = this._createStringGrip(aContent.text);
    if (typeof aContent.text == "object") {
      this._longStringActors.add(aContent.text);
    }

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "responseContent",
      mimeType: aContent.mimeType,
      contentSize: aContent.text.length,
      discardResponseBody: aDiscardedResponseBody,
    };

    this.conn.send(packet);
  },

  /**
   * Add network event timing information.
   *
   * @param number aTotal
   *        The total time of the network event.
   * @param object aTimings
   *        Timing details about the network event.
   */
  addEventTimings: function NEA_addEventTimings(aTotal, aTimings)
  {
    this._totalTime = aTotal;
    this._timings = aTimings;

    let packet = {
      from: this.actorID,
      type: "networkEventUpdate",
      updateType: "eventTimings",
      totalTime: aTotal,
    };

    this.conn.send(packet);
  },

  /**
   * Prepare the headers array to be sent to the client by using the
   * LongStringActor for the header values, when needed.
   *
   * @private
   * @param array aHeaders
   */
  _prepareHeaders: function NEA__prepareHeaders(aHeaders)
  {
    for (let header of aHeaders) {
      header.value = this._createStringGrip(header.value);
      if (typeof header.value == "object") {
        this._longStringActors.add(header.value);
      }
    }
  },

  /**
   * Create a long string grip if needed for the given string.
   *
   * @private
   * @param string aString
   *        The string you want to create a long string grip for.
   * @return string|object
   *         A string is returned if |aString| is not a long string.
   *         A LongStringActor grip is returned if |aString| is a long string.
   */
  _createStringGrip: function NEA__createStringGrip(aString)
  {
    if (this.parent._stringIsLong(aString)) {
      return this.parent.longStringGrip(aString, this.parent._actorPool);
    }
    return aString;
  },
};

NetworkEventActor.prototype.requestTypes =
{
  "release": NetworkEventActor.prototype.onRelease,
  "getRequestHeaders": NetworkEventActor.prototype.onGetRequestHeaders,
  "getRequestCookies": NetworkEventActor.prototype.onGetRequestCookies,
  "getRequestPostData": NetworkEventActor.prototype.onGetRequestPostData,
  "getResponseHeaders": NetworkEventActor.prototype.onGetResponseHeaders,
  "getResponseCookies": NetworkEventActor.prototype.onGetResponseCookies,
  "getResponseContent": NetworkEventActor.prototype.onGetResponseContent,
  "getEventTimings": NetworkEventActor.prototype.onGetEventTimings,
};

DebuggerServer.addTabActor(WebConsoleActor, "consoleActor");
DebuggerServer.addGlobalActor(WebConsoleActor, "consoleActor");


