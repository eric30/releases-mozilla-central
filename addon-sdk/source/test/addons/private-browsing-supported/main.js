/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
'use strict';

const { merge } = require('sdk/util/object');
const app = require('sdk/system/xul-app');
const { isGlobalPBSupported } = require('sdk/private-browsing/utils');

merge(module.exports,
  require('./test-tabs'),
  require('./test-page-mod'),
  require('./test-selection'),
  require('./test-panel'),
  require('./test-private-browsing'),
  isGlobalPBSupported ? require('./test-global-private-browsing') : {}
);

// Doesn't make sense to test window-utils and windows on fennec,
// as there is only one window which is never private
if (!app.is('Fennec'))
  merge(module.exports, require('./test-windows'));

require('sdk/test/runner').runTestsFromModule(module);
