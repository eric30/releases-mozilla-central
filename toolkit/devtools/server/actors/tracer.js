/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Cu } = require("chrome");

const { reportException } =
  Cu.import("resource://gre/modules/devtools/DevToolsUtils.jsm", {}).DevToolsUtils;

const { DebuggerServer } = Cu.import("resource://gre/modules/devtools/dbg-server.jsm", {});

Cu.import("resource://gre/modules/jsdebugger.jsm");
addDebuggerToGlobal(this);

/**
 * Creates a TraceActor. TraceActor provides a stream of function
 * call/return packets to a remote client gathering a full trace.
 */
function TraceActor(aConn, aBrowserTabActor)
{
  this._attached = false;
  this._activeTraces = new MapStack();
  this._totalTraces = 0;
  this._startTime = 0;
  this._requestsForTraceType = Object.create(null);
  for (let type of TraceTypes.types) {
    this._requestsForTraceType[type] = 0;
  }
  this._sequence = 0;

  this.global = aBrowserTabActor.contentWindow.wrappedJSObject;
}

TraceActor.prototype = {
  actorPrefix: "trace",

  get attached() { return this._attached; },
  get idle()     { return this._attached && this._activeTraces.size === 0; },
  get tracing()  { return this._attached && this._activeTraces.size > 0; },

  /**
   * Handle a TraceTypes.Events event by calling each handler which has been
   * requested by an active trace and adding its result to the packet.
   *
   * @param aEvent string
   *        The event to dispatch.
   *
   * @param aPacket object
   *        The debugger protocol packet.
   *
   * @param aArgs object
   *        The arguments object for the handler.
   */
  _handleEvent: function(aEvent, aPacket, aArgs) {
    let handlersForEvent = TraceTypes.handlers[aEvent];
    for (let traceType in handlersForEvent) {
      if (this._requestsForTraceType[traceType]) {
        aPacket[traceType] = handlersForEvent[traceType].call(null, aArgs);
      }
    }
  },

  /**
   * Initializes a Debugger instance and adds listeners to it.
   */
  _initDebugger: function() {
    this.dbg = new Debugger();
    this.dbg.onEnterFrame = this.onEnterFrame.bind(this);
    this.dbg.onNewGlobalObject = this.globalManager.onNewGlobal.bind(this);
    this.dbg.enabled = false;
  },

  /**
   * Add a debuggee global to the Debugger object.
   */
  _addDebuggee: function(aGlobal) {
    try {
      this.dbg.addDebuggee(aGlobal);
    } catch (e) {
      // Ignore attempts to add the debugger's compartment as a debuggee.
      reportException("TraceActor",
                      new Error("Ignoring request to add the debugger's "
                                + "compartment as a debuggee"));
    }
  },

  /**
   * Add the provided window and all windows in its frame tree as debuggees.
   */
  _addDebuggees: function(aWindow) {
    this._addDebuggee(aWindow);
    let frames = aWindow.frames;
    if (frames) {
      for (let i = 0; i < frames.length; i++) {
        this._addDebuggees(frames[i]);
      }
    }
  },

  /**
   * An object used by TraceActors to tailor their behavior depending
   * on the debugging context required (chrome or content).
   */
  globalManager: {
    /**
     * Adds all globals in the global object as debuggees.
     */
    findGlobals: function() {
      this._addDebuggees(this.global);
    },

    /**
     * A function that the engine calls when a new global object has been
     * created. Adds the global object as a debuggee if it is in the content
     * window.
     *
     * @param aGlobal Debugger.Object
     *        The new global object that was created.
     */
    onNewGlobal: function(aGlobal) {
      // Content debugging only cares about new globals in the content
      // window, like iframe children.
      if (aGlobal.hostAnnotations &&
          aGlobal.hostAnnotations.type == "document" &&
          aGlobal.hostAnnotations.element === this.global) {
        this._addDebuggee(aGlobal);
      }
    },
  },

  /**
   * Handle a protocol request to attach to the trace actor.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onAttach: function(aRequest) {
    if (this.attached) {
      return {
        error: "wrongState",
        message: "Already attached to a client"
      };
    }

    if (!this.dbg) {
      this._initDebugger();
      this.globalManager.findGlobals.call(this);
    }

    this._attached = true;

    return { type: "attached", traceTypes: TraceTypes.types };
  },

  /**
   * Handle a protocol request to detach from the trace actor.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onDetach: function() {
    while (this.tracing) {
      this.onStopTrace();
    }

    this.dbg = null;

    this._attached = false;
    this.conn.send({ from: this.actorID, type: "detached" });
  },

  /**
   * Handle a protocol request to start a new trace.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onStartTrace: function(aRequest) {
    for (let traceType of aRequest.trace) {
      if (TraceTypes.types.indexOf(traceType) < 0) {
        return {
          error: "badParameterType",
          message: "No such trace type: " + traceType
        };
      }
    }

    if (this.idle) {
      this.dbg.enabled = true;
      this._sequence = 0;
      this._startTime = +new Date;
    }

    // Start recording all requested trace types.
    for (let traceType of aRequest.trace) {
      this._requestsForTraceType[traceType]++;
    }

    this._totalTraces++;
    let name = aRequest.name || "Trace " + this._totalTraces;
    this._activeTraces.push(name, aRequest.trace);

    return { type: "startedTrace", why: "requested", name: name };
  },

  /**
   * Handle a protocol request to end a trace.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onStopTrace: function(aRequest) {
    if (!this.tracing) {
      return {
        error: "wrongState",
        message: "No active traces"
      };
    }

    let stoppedTraceTypes, name;
    if (aRequest && aRequest.name) {
      name = aRequest.name;
      if (!this._activeTraces.has(name)) {
        return {
          error: "noSuchTrace",
          message: "No active trace with name: " + name
        };
      }
      stoppedTraceTypes = this._activeTraces.delete(name);
    } else {
      name = this._activeTraces.peekKey();
      stoppedTraceTypes = this._activeTraces.pop();
    }

    for (let traceType of stoppedTraceTypes) {
      this._requestsForTraceType[traceType]--;
    }

    if (this.idle) {
      this.dbg.enabled = false;
    }

    return { type: "stoppedTrace", why: "requested", name: name };
  },

  // JS Debugger API hooks.

  /**
   * Called by the engine when a frame is entered. Sends an unsolicited packet
   * to the client carrying requested trace information.
   *
   * @param aFrame Debugger.frame
   *        The stack frame that was entered.
   */
  onEnterFrame: function(aFrame) {
    let callee = aFrame.callee;
    let packet = {
      from: this.actorID,
      type: "enteredFrame",
      sequence: this._sequence++
    };

    this._handleEvent(TraceTypes.Events.enterFrame, packet, {
      frame: aFrame,
      startTime: this._startTime
    });

    aFrame.onPop = this.onExitFrame.bind(this);

    this.conn.send(packet);
  },

  /**
   * Called by the engine when a frame is exited. Sends an unsolicited packet to
   * the client carrying requested trace information.
   *
   * @param aValue object
   *        The debugger completion value for the frame.
   */
  onExitFrame: function(aValue) {
    let packet = {
      from: this.actorID,
      type: "exitedFrame",
      sequence: this._sequence++
    };

    this._handleEvent(TraceTypes.Events.exitFrame, packet, { value: aValue });

    this.conn.send(packet);
  }
};

/**
 * The request types this actor can handle.
 */
TraceActor.prototype.requestTypes = {
  "attach": TraceActor.prototype.onAttach,
  "detach": TraceActor.prototype.onDetach,
  "startTrace": TraceActor.prototype.onStartTrace,
  "stopTrace": TraceActor.prototype.onStopTrace
};

exports.register = function(handle) {
  handle.addTabActor(TraceActor, "traceActor");
};

exports.unregister = function(handle) {
  handle.removeTabActor(TraceActor, "traceActor");
};


/**
 * MapStack is a collection of key/value pairs with stack ordering,
 * where keys are strings and values are any JS value. In addition to
 * the push and pop stack operations, supports a "delete" operation,
 * which removes the value associated with a given key from any
 * location in the stack.
 */
function MapStack()
{
  // Essentially a MapStack is just sugar-coating around a standard JS
  // object, plus the _stack array to track ordering.
  this._stack = [];
  this._map = Object.create(null);
}

MapStack.prototype = {
  get size() { return this._stack.length; },

  /**
   * Return the key for the value on the top of the stack, or
   * undefined if the stack is empty.
   */
  peekKey: function() {
    return this._stack[this.size - 1];
  },

  /**
   * Return true iff a value has been associated with the given key.
   *
   * @param aKey string
   *        The key whose presence is to be tested.
   */
  has: function(aKey) {
    return Object.prototype.hasOwnProperty.call(this._map, aKey);
  },

  /**
   * Return the value associated with the given key, or undefined if
   * no value is associated with the key.
   *
   * @param aKey string
   *        The key whose associated value is to be returned.
   */
  get: function(aKey) {
    return this._map[aKey];
  },

  /**
   * Push a new value onto the stack. If another value with the same
   * key is already on the stack, it will be removed before the new
   * value is pushed onto the top of the stack.
   *
   * @param aKey string
   *        The key of the object to push onto the stack.
   *
   * @param aValue
   *        The value to push onto the stack.
   */
  push: function(aKey, aValue) {
    this.delete(aKey);
    this._stack.push(aKey);
    this._map[aKey] = aValue;
  },

  /**
   * Remove the value from the top of the stack and return it.
   * Returns undefined if the stack is empty.
   */
  pop: function() {
    let key = this.peekKey();
    let value = this.get(key);
    this._stack.pop();
    delete this._map[key];
    return value;
  },

  /**
   * Remove the value associated with the given key from the stack and
   * return it. Returns undefined if no value is associated with the
   * given key.
   *
   * @param aKey string
   *        The key for the value to remove from the stack.
   */
  delete: function(aKey) {
    let value = this.get(aKey);
    if (this.has(aKey)) {
      let keyIndex = this._stack.lastIndexOf(aKey);
      this._stack.splice(keyIndex, 1);
      delete this._map[aKey];
    }
    return value;
  }
};


/**
 * TraceTypes is a collection of handlers which generate optional trace
 * information. Handlers are associated with an event (from TraceTypes.Event)
 * and a trace type, and return a value to be embedded in the packet associated
 * with that event.
 */
let TraceTypes = {
  handlers: {},
  types: [],

  register: function(aType, aEvent, aHandler) {
    if (!this.handlers[aEvent]) {
      this.handlers[aEvent] = {};
    }
    this.handlers[aEvent][aType] = aHandler;
    if (this.types.indexOf(aType) < 0) {
      this.types.push(aType);
    }
  }
};

TraceTypes.Events = {
  "enterFrame": "enterFrame",
  "exitFrame": "exitFrame"
};

TraceTypes.register("name", TraceTypes.Events.enterFrame, function({ frame }) {
  return frame.callee
    ? frame.callee.displayName || "(anonymous function)"
    : "(" + frame.type + ")";
});

TraceTypes.register("callsite", TraceTypes.Events.enterFrame, function({ frame }) {
  if (!frame.script) {
    return undefined;
  }
  return {
    url: frame.script.url,
    line: frame.script.getOffsetLine(frame.offset),
    column: getOffsetColumn(frame.offset, frame.script)
  };
});

TraceTypes.register("time", TraceTypes.Events.enterFrame, timeSinceTraceStarted);
TraceTypes.register("time", TraceTypes.Events.exitFrame, timeSinceTraceStarted);

TraceTypes.register("parameterNames", TraceTypes.Events.enterFrame, function({ frame }) {
  return frame.callee ? frame.callee.parameterNames : undefined;
});

TraceTypes.register("arguments", TraceTypes.Events.enterFrame, function({ frame }) {
  if (!frame.arguments) {
    return undefined;
  }
  let objectPool = [];
  let objToId = new Map();
  let args = Array.prototype.slice.call(frame.arguments);
  let values = args.map(arg => createValueGrip(arg, objectPool, objToId));
  return { values: values, objectPool: objectPool };
});

TraceTypes.register("return", TraceTypes.Events.exitFrame,
                    serializeCompletionValue.bind(null, "return"));

TraceTypes.register("throw", TraceTypes.Events.exitFrame,
                    serializeCompletionValue.bind(null, "throw"));

TraceTypes.register("yield", TraceTypes.Events.exitFrame,
                    serializeCompletionValue.bind(null, "yield"));


// TODO bug 863089: use Debugger.Script.prototype.getOffsetColumn when
// it is implemented.
function getOffsetColumn(aOffset, aScript) {
  let bestOffsetMapping = null;
  for (let offsetMapping of aScript.getAllColumnOffsets()) {
    if (!bestOffsetMapping ||
        (offsetMapping.offset <= aOffset &&
         offsetMapping.offset > bestOffsetMapping.offset)) {
      bestOffsetMapping = offsetMapping;
    }
  }

  if (!bestOffsetMapping) {
    // XXX: Try not to completely break the experience of using the
    // tracer for the user by assuming column 0. Simultaneously,
    // report the error so that there is a paper trail if the
    // assumption is bad and the tracing experience becomes wonky.
    reportException("TraceActor",
                    new Error("Could not find a column for offset " + aOffset +
                              " in the script " + aScript));
    return 0;
  }

  return bestOffsetMapping.columnNumber;
}

/**
 * Returns elapsed time since the given start time.
 */
function timeSinceTraceStarted({ startTime }) {
  return +new Date - startTime;
}

/**
 * Creates a pool of object descriptors and a value grip for the given
 * completion value, to be serialized by JSON.stringify.
 *
 * @param aType string
 *        The type of completion value to serialize (return, throw, or yield).
 */
function serializeCompletionValue(aType, { value }) {
  if (typeof value[aType] === "undefined") {
    return undefined;
  }
  let objectPool = [];
  let objToId = new Map();
  let valueGrip = createValueGrip(value[aType], objectPool, objToId);
  return { value: valueGrip, objectPool: objectPool };
}


// Serialization helper functions. Largely copied from script.js and modified
// for use in serialization rather than object actor requests.

/**
 * Create a grip for the given debuggee value. If the value is an object, will
 * create an object descriptor in the given object pool.
 *
 * @param aValue Debugger.Object|primitive
 *        The value to describe with the created grip.
 *
 * @param aPool [ObjectDescriptor]
 *        The pool of objects that may be referenced by |aValue|.
 *
 * @param aObjectToId Map
 *        A map from Debugger.Object instances to indices into the pool.
 *
 * @return ValueGrip
 *        A primitive value or a grip object.
 */
function createValueGrip(aValue, aPool, aObjectToId) {
  let type = typeof aValue;

  if (type === "string" && aValue.length >= DebuggerServer.LONG_STRING_LENGTH) {
    return {
      type: "longString",
      initial: aValue.substring(0, DebuggerServer.LONG_STRING_INITIAL_LENGTH),
      length: aValue.length
    };
  }

  if (type === "boolean" || type === "string" || type === "number") {
    return aValue;
  }

  if (aValue === null) {
    return { type: "null" };
  }

  if (aValue === undefined) {
    return { type: "undefined" };
  }

  if (typeof(aValue) === "object") {
    createObjectDescriptor(aValue, aPool, aObjectToId);
    return { type: "object", objectId: aObjectToId.get(aValue) };
  }

  reportException("TraceActor",
                  new Error("Failed to provide a grip for: " + aValue));
  return null;
}

/**
 * Create an object descriptor in the object pool for the given debuggee object,
 * if it is not already present.
 *
 * @param aObject Debugger.Object
 *        The object to describe with the created descriptor.
 *
 * @param aPool [ObjectDescriptor]
 *        The pool of objects that may be referenced by |aObject|.
 *
 * @param aObjectToId Map
 *        A map from Debugger.Object instances to indices into the pool.
 */
function createObjectDescriptor(aObject, aPool, aObjectToId) {
  if (aObjectToId.has(aObject)) {
    return;
  }

  aObjectToId.set(aObject, aPool.length);
  let desc = Object.create(null);
  aPool.push(desc);

  // Add properties which object actors include in object grips.
  desc.class = aObject.class;
  desc.extensible = aObject.isExtensible();
  desc.frozen = aObject.isFrozen();
  desc.sealed = aObject.isSealed();

  // Add additional properties for functions.
  if (aObject.class === "Function") {
    if (aObject.name) {
      desc.name = aObject.name;
    }
    if (aObject.displayName) {
      desc.displayName = aObject.displayName;
    }

    // Check if the developer has added a de-facto standard displayName
    // property for us to use.
    let name = aObject.getOwnPropertyDescriptor("displayName");
    if (name && name.value && typeof name.value == "string") {
      desc.userDisplayName = createValueGrip(name.value, aObject, aPool, aObjectToId);
    }
  }

  let ownProperties = Object.create(null);
  let propNames;
  try {
    propNames = aObject.getOwnPropertyNames();
  } catch(ex) {
    // The above can throw if aObject points to a dead object.
    // TODO: we should use Cu.isDeadWrapper() - see bug 885800.
    desc.prototype = createValueGrip(null);
    desc.ownProperties = ownProperties;
    desc.safeGetterValues = Object.create(null);
    return;
  }

  for (let name of propNames) {
    ownProperties[name] = createPropertyDescriptor(name, aObject, aPool, aObjectToId);
  }

  desc.prototype = createValueGrip(aObject.proto, aPool, aObjectToId);
  desc.ownProperties = ownProperties;
  desc.safeGetterValues = findSafeGetterValues(ownProperties, aObject, aPool, aObjectToId);
}

/**
 * A helper method that creates a property descriptor for the provided object,
 * properly formatted for sending in a protocol response.
 *
 * @param aName string
 *        The property that the descriptor is generated for.
 *
 * @param aObject Debugger.Object
 *        The object whose property the descriptor is generated for.
 *
 * @param aPool [ObjectDescriptor]
 *        The pool of objects that may be referenced by this property.
 *
 * @param aObjectToId Map
 *        A map from Debugger.Object instances to indices into the pool.
 *
 * @return object
 *         The property descriptor for the property |aName| in |aObject|.
 */
function createPropertyDescriptor(aName, aObject, aPool, aObjectToId) {
  let desc;
  try {
    desc = aObject.getOwnPropertyDescriptor(aName);
  } catch (e) {
    // Calling getOwnPropertyDescriptor on wrapped native prototypes is not
    // allowed (bug 560072). Inform the user with a bogus, but hopefully
    // explanatory, descriptor.
    return {
      configurable: false,
      writable: false,
      enumerable: false,
      value: e.name
    };
  }

  let retval = {
    configurable: desc.configurable,
    enumerable: desc.enumerable
  };

  if ("value" in desc) {
    retval.writable = desc.writable;
    retval.value = createValueGrip(desc.value, aPool, aObjectToId);
  } else {
    if ("get" in desc) {
      retval.get = createValueGrip(desc.get, aPool, aObjectToId);
    }
    if ("set" in desc) {
      retval.set = createValueGrip(desc.set, aPool, aObjectToId);
    }
  }
  return retval;
}

/**
 * Find the safe getter values for the given Debugger.Object.
 *
 * @param aOwnProperties object
 *        The object that holds the list of known ownProperties for |aObject|.
 *
 * @param Debugger.Object object
 *        The object to find safe getter values for.
 *
 * @param aPool [ObjectDescriptor]
 *        The pool of objects that may be referenced by |aObject| getters.
 *
 * @param aObjectToId Map
 *        A map from Debugger.Object instances to indices into the pool.
 *
 * @return object
 *         An object that maps property names to safe getter descriptors.
 */
function findSafeGetterValues(aOwnProperties, aObject, aPool, aObjectToId) {
  let safeGetterValues = Object.create(null);
  let obj = aObject;
  let level = 0;

  while (obj) {
    let getters = findSafeGetters(obj);
    for (let name of getters) {
      // Avoid overwriting properties from prototypes closer to this.obj. Also
      // avoid providing safeGetterValues from prototypes if property |name|
      // is already defined as an own property.
      if (name in safeGetterValues ||
          (obj != aObject && name in aOwnProperties)) {
        continue;
      }

      let desc = null, getter = null;
      try {
        desc = obj.getOwnPropertyDescriptor(name);
        getter = desc.get;
      } catch (ex) {
        // The above can throw if the cache becomes stale.
      }
      if (!getter) {
        continue;
      }

      let result = getter.call(aObject);
      if (result && !("throw" in result)) {
        let getterValue = undefined;
        if ("return" in result) {
          getterValue = result.return;
        } else if ("yield" in result) {
          getterValue = result.yield;
        }
        // WebIDL attributes specified with the LenientThis extended attribute
        // return undefined and should be ignored.
        if (getterValue !== undefined) {
          safeGetterValues[name] = {
            getterValue: createValueGrip(getterValue, aPool, aObjectToId),
            getterPrototypeLevel: level,
            enumerable: desc.enumerable,
            writable: level == 0 ? desc.writable : true,
          };
        }
      }
    }

    obj = obj.proto;
    level++;
  }

  return safeGetterValues;
}

/**
 * Find the safe getters for a given Debugger.Object. Safe getters are native
 * getters which are safe to execute.
 *
 * @param Debugger.Object aObject
 *        The Debugger.Object where you want to find safe getters.
 *
 * @return Set
 *         A Set of names of safe getters.
 */
function findSafeGetters(aObject) {
  let getters = new Set();
  for (let name of aObject.getOwnPropertyNames()) {
    let desc = null;
    try {
      desc = aObject.getOwnPropertyDescriptor(name);
    } catch (e) {
      // Calling getOwnPropertyDescriptor on wrapped native prototypes is not
      // allowed (bug 560072).
    }
    if (!desc || desc.value !== undefined || !("get" in desc)) {
      continue;
    }

    let fn = desc.get;
    if (fn && fn.callable && fn.class == "Function" &&
        fn.script === undefined) {
      getters.add(name);
    }
  }

  return getters;
}
