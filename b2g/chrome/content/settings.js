/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- /
/* vim: set shiftwidth=2 tabstop=2 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict;"

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');

// Once Bug 731746 - Allow chrome JS object to implement nsIDOMEventTarget
// is resolved this helper could be removed.
var SettingsListener = {
  _callbacks: {},

  init: function sl_init() {
    if ('mozSettings' in navigator && navigator.mozSettings) {
      navigator.mozSettings.onsettingchange = this.onchange.bind(this);
    }
  },

  onchange: function sl_onchange(evt) {
    var callback = this._callbacks[evt.settingName];
    if (callback) {
      callback(evt.settingValue);
    }
  },

  observe: function sl_observe(name, defaultValue, callback) {
    var settings = window.navigator.mozSettings;
    if (!settings) {
      window.setTimeout(function() { callback(defaultValue); });
      return;
    }

    if (!callback || typeof callback !== 'function') {
      throw new Error('Callback is not a function');
    }

    var req = settings.createLock().get(name);
    req.addEventListener('success', (function onsuccess() {
      callback(typeof(req.result[name]) != 'undefined' ?
        req.result[name] : defaultValue);
    }));

    this._callbacks[name] = callback;
  }
};

SettingsListener.init();

// =================== Audio ====================
SettingsListener.observe('audio.volume.master', 0.5, function(value) {
  let audioManager = Services.audioManager;
  if (!audioManager)
    return;

  audioManager.masterVolume = Math.max(0.0, Math.min(value, 1.0));
});

let audioSettings = [];

if ("nsIAudioManager" in Ci) {
  const nsIAudioManager = Ci.nsIAudioManager;
  audioSettings = [
    // settings name, default value, stream type
    ['audio.volume.voice_call', 10, nsIAudioManager.STREAM_TYPE_VOICE_CALL],
    ['audio.volume.system', 10,  nsIAudioManager.STREAM_TYPE_SYSTEM],
    ['audio.volume.ring', 7, nsIAudioManager.STREAM_TYPE_RING],
    ['audio.volume.music', 15, nsIAudioManager.STREAM_TYPE_MUSIC],
    ['audio.volume.alarm', 7, nsIAudioManager.STREAM_TYPE_ALARM],
    ['audio.volume.notification', 7, nsIAudioManager.STREAM_TYPE_NOTIFICATION],
    ['audio.volume.bt_sco', 15, nsIAudioManager.STREAM_TYPE_BLUETOOTH_SCO],
    ['audio.volume.enforced_audible', 7, nsIAudioManager.STREAM_TYPE_ENFORCED_AUDIBLE],
    ['audio.volume.dtmf', 15, nsIAudioManager.STREAM_TYPE_DTMF],
    ['audio.volume.tts', 15, nsIAudioManager.STREAM_TYPE_TTS],
    ['audio.volume.fm', 10, nsIAudioManager.STREAM_TYPE_FM],
  ];
}

for each (let [setting, defaultValue, streamType] in audioSettings) {
  (function AudioStreamSettings(s, d, t) {
    SettingsListener.observe(s, d, function(value) {
      let audioManager = Services.audioManager;
      if (!audioManager)
        return;

      audioManager.setStreamVolumeIndex(t, Math.min(value, d));
    });
  })(setting, defaultValue, streamType);
}

// =================== Console ======================

SettingsListener.observe('debug.console.enabled', true, function(value) {
  Services.prefs.setBoolPref('consoleservice.enabled', value);
});

// =================== Languages ====================
SettingsListener.observe('language.current', 'en-US', function(value) {
  Services.prefs.setCharPref('general.useragent.locale', value);

  let prefName = 'intl.accept_languages';
  if (Services.prefs.prefHasUserValue(prefName)) {
    Services.prefs.clearUserPref(prefName);
  }

  let intl = '';
  try {
    intl = Services.prefs.getComplexValue(prefName,
                                          Ci.nsIPrefLocalizedString).data;
  } catch(e) {}

  if (!((new RegExp('^' + value + '[^a-z-_] *[,;]?', 'i')).test(intl))) {
    Services.prefs.setCharPref(prefName, value + ', ' + intl);
  }

  shell.start();
});

// =================== RIL ====================
(function RILSettingsToPrefs() {
  let strPrefs = ['ril.data.mmsc', 'ril.data.mmsproxy'];
  strPrefs.forEach(function(key) {
    SettingsListener.observe(key, "", function(value) {
      Services.prefs.setCharPref(key, value);
    });
  });

  ['ril.data.mmsport'].forEach(function(key) {
    SettingsListener.observe(key, null, function(value) {
      if (value != null) {
        Services.prefs.setIntPref(key, value);
      }
    });
  });

  SettingsListener.observe('ril.sms.strict7BitEncoding.enabled', false,
    function(value) {
      Services.prefs.setBoolPref('dom.sms.strict7BitEncoding', value);
  });
})();

//=================== DeviceInfo ====================
Components.utils.import('resource://gre/modules/XPCOMUtils.jsm');
Components.utils.import('resource://gre/modules/ctypes.jsm');
(function DeviceInfoToSettings() {
  XPCOMUtils.defineLazyServiceGetter(this, 'gSettingsService',
                                     '@mozilla.org/settingsService;1',
                                     'nsISettingsService');
  let lock = gSettingsService.createLock();
  //MOZ_B2G_VERSION is set in b2g/confvars.sh, and is outputed as a #define value
  //from configure.in, defaults to 1.0.0 if this value is not exist
#filter attemptSubstitution
  let os_version = '@MOZ_B2G_VERSION@';
#unfilter attemptSubstitution
  lock.set('deviceinfo.os', os_version, null, null);

  let appInfo = Cc["@mozilla.org/xre/app-info;1"]
                  .getService(Ci.nsIXULAppInfo);
  lock.set('deviceinfo.platform_version', appInfo.platformVersion, null, null);
  lock.set('deviceinfo.platform_build_id', appInfo.platformBuildID, null, null);

  let update_channel = Services.prefs.getCharPref('app.update.channel');
  lock.set('deviceinfo.update_channel', update_channel, null, null);

  //Get the hardware info from android properties
  let hardware_version = null;
  try {
    let cutils = ctypes.open('libcutils.so');
    let cbuf = ctypes.char.array(128)();
    let c_property_get = cutils.declare('property_get', ctypes.default_abi,
                                        ctypes.int,       // return value: length
                                        ctypes.char.ptr,  // key
                                        ctypes.char.ptr,  // value
                                        ctypes.char.ptr); // default
    let property_get = function (key, defaultValue) {
      if (defaultValue === undefined) {
        defaultValue = null;
      }
      c_property_get(key, cbuf, defaultValue);
      return cbuf.readString();
    }
    hardware_version = property_get('ro.hardware');
    cutils.close();
  } catch(e) {
    //Error
  }
  lock.set('deviceinfo.hardware', hardware_version, null, null);
})();

// =================== Debugger ====================
SettingsListener.observe('devtools.debugger.remote-enabled', false, function(value) {
  Services.prefs.setBoolPref('devtools.debugger.remote-enabled', value);
  // This preference is consulted during startup
  Services.prefs.savePrefFile(null);
});

SettingsListener.observe('devtools.debugger.log', false, function(value) {
  Services.prefs.setBoolPref('devtools.debugger.log', value);
});

SettingsListener.observe('devtools.debugger.remote-port', 6000, function(value) {
  Services.prefs.setIntPref('devtools.debugger.remote-port', value);
});

SettingsListener.observe('devtools.debugger.force-local', true, function(value) {
  Services.prefs.setBoolPref('devtools.debugger.force-local', value);
});

SettingsListener.observe('debug.log-animations.enabled', false, function(value) {
  Services.prefs.setBoolPref('layers.offmainthreadcomposition.log-animations', value);
});

SettingsListener.observe('debug.dev-mode', false, function(value) {
  Services.prefs.setBoolPref('dom.mozApps.dev_mode', value);
});

// =================== Privacy ====================
SettingsListener.observe('privacy.donottrackheader.enabled', false, function(value) {
  Services.prefs.setBoolPref('privacy.donottrackheader.enabled', value);
});

// =================== Crash Reporting ====================
SettingsListener.observe('app.reportCrashes', 'ask', function(value) {
  if (value == 'always') {
    Services.prefs.setBoolPref('app.reportCrashes', true);
  } else if (value == 'never') {
    Services.prefs.setBoolPref('app.reportCrashes', false);
  } else {
    Services.prefs.clearUserPref('app.reportCrashes');
  }
});

// ================ Updates ================
SettingsListener.observe('app.update.interval', 86400, function(value) {
  Services.prefs.setIntPref('app.update.interval', value);
});
