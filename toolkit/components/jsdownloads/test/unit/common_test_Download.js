/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This script is loaded by "test_DownloadCore.js" and "test_DownloadLegacy.js"
 * with different values of the gUseLegacySaver variable, to apply tests to both
 * the "copy" and "legacy" saver implementations.
 */

"use strict";

////////////////////////////////////////////////////////////////////////////////
//// Globals

/**
 * Creates and starts a new download, using either DownloadCopySaver or
 * DownloadLegacySaver based on the current test run.
 *
 * @return {Promise}
 * @resolves The newly created Download object.  The download may be in progress
 *           or already finished.  The promiseDownloadStopped function can be
 *           used to wait for completion.
 * @rejects JavaScript exception.
 */
function promiseStartDownload(aSourceUrl) {
  if (gUseLegacySaver) {
    return promiseStartLegacyDownload(aSourceUrl);
  }

  return promiseNewDownload(aSourceUrl).then(download => {
    download.start();
    return download;
  });
}

/**
 * Waits for a download to finish, in case it has not finished already.
 *
 * @param aDownload
 *        The Download object to wait upon.
 *
 * @return {Promise}
 * @resolves When the download has finished successfully.
 * @rejects JavaScript exception if the download failed.
 */
function promiseDownloadStopped(aDownload) {
  if (!aDownload.stopped) {
    // The download is in progress, wait for the current attempt to finish and
    // report any errors that may occur.
    return aDownload.start();
  }

  if (aDownload.succeeded) {
    return Promise.resolve();
  }

  // The download failed or was canceled.
  return Promise.reject(aDownload.error || new Error("Download canceled."));
}

////////////////////////////////////////////////////////////////////////////////
//// Tests

/**
 * Executes a download and checks its basic properties after construction.
 * The download is started by constructing the simplest Download object with
 * the "copy" saver, or using the legacy nsITransfer interface.
 */
add_task(function test_basic()
{
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can check its basic properties before it starts.
    download = yield Downloads.createDownload({
      source: { url: httpUrl("source.txt") },
      target: { path: targetFile.path },
      saver: { type: "copy" },
    });

    do_check_eq(download.source.url, httpUrl("source.txt"));
    do_check_eq(download.target.path, targetFile.path);

    yield download.start();
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we must check its basic properties while in progress.
    download = yield promiseStartLegacyDownload(null,
                                                { targetFile: targetFile });

    do_check_eq(download.source.url, httpUrl("source.txt"));
    do_check_eq(download.target.path, targetFile.path);

    yield promiseDownloadStopped(download);
  }

  // Check additional properties on the finished download.
  do_check_true(download.source.referrer === null);

  yield promiseVerifyContents(download.target.path, TEST_DATA_SHORT);
});

/**
 * Checks the referrer for downloads.
 */
add_task(function test_referrer()
{
  let sourcePath = "/test_referrer.txt";
  let sourceUrl = httpUrl("test_referrer.txt");
  let targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }

  do_register_cleanup(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);

    do_check_true(aRequest.hasHeader("Referer"));
    do_check_eq(aRequest.getHeader("Referer"), TEST_REFERRER_URL);
  });
  let download = yield Downloads.createDownload({
    source: { url: sourceUrl, referrer: TEST_REFERRER_URL },
    target: targetPath,
  });
  do_check_eq(download.source.referrer, TEST_REFERRER_URL);
  yield download.start();

  download = yield Downloads.createDownload({
    source: { url: sourceUrl, referrer: TEST_REFERRER_URL,
              isPrivate: true },
    target: targetPath,
  });
  do_check_eq(download.source.referrer, TEST_REFERRER_URL);
  yield download.start();

  // Test the download still works for non-HTTP channel with referrer.
  sourceUrl = "data:text/html,<html><body></body></html>";
  download = yield Downloads.createDownload({
    source: { url: sourceUrl, referrer: TEST_REFERRER_URL },
    target: targetPath,
  });
  do_check_eq(download.source.referrer, TEST_REFERRER_URL);
  yield download.start();

  cleanup();
});

/**
 * Checks initial and final state and progress for a successful download.
 */
add_task(function test_initial_final_state()
{
  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can check its state before it starts.
    download = yield promiseNewDownload();

    do_check_true(download.stopped);
    do_check_false(download.succeeded);
    do_check_false(download.canceled);
    do_check_true(download.error === null);
    do_check_eq(download.progress, 0);
    do_check_true(download.startTime === null);

    yield download.start();
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, thus we cannot check its initial state.
    download = yield promiseStartLegacyDownload();
    yield promiseDownloadStopped(download);
  }

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);
  do_check_eq(download.progress, 100);
  do_check_true(isValidDate(download.startTime));
});

/**
 * Checks the notification of the final download state.
 */
add_task(function test_final_state_notified()
{
  let deferResponse = deferNextResponse();

  let download = yield promiseStartDownload(httpUrl("interruptible.txt"));

  let onchangeNotified = false;
  let lastNotifiedStopped;
  let lastNotifiedProgress;
  download.onchange = function () {
    onchangeNotified = true;
    lastNotifiedStopped = download.stopped;
    lastNotifiedProgress = download.progress;
  };

  // Allow the download to complete.
  let promiseAttempt = download.start();
  deferResponse.resolve();
  yield promiseAttempt;

  // The view should have been notified before the download completes.
  do_check_true(onchangeNotified);
  do_check_true(lastNotifiedStopped);
  do_check_eq(lastNotifiedProgress, 100);
});

/**
 * Checks intermediate progress for a successful download.
 */
add_task(function test_intermediate_progress()
{
  let deferResponse = deferNextResponse();

  let download = yield promiseStartDownload(httpUrl("interruptible.txt"));

  let onchange = function () {
    if (download.progress == 50) {
      do_check_true(download.hasProgress);
      do_check_eq(download.currentBytes, TEST_DATA_SHORT.length);
      do_check_eq(download.totalBytes, TEST_DATA_SHORT.length * 2);

      // Continue after the first chunk of data is fully received.
      deferResponse.resolve();
    }
  };

  // Register for the notification, but also call the function directly in case
  // the download already reached the expected progress.
  download.onchange = onchange;
  onchange();

  yield promiseDownloadStopped(download);

  do_check_true(download.stopped);
  do_check_eq(download.progress, 100);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Downloads a file with a "Content-Length" of 0 and checks the progress.
 */
add_task(function test_empty_progress()
{
  let download = yield promiseStartDownload(httpUrl("empty.txt"));
  yield promiseDownloadStopped(download);

  do_check_true(download.stopped);
  do_check_true(download.hasProgress);
  do_check_eq(download.progress, 100);
  do_check_eq(download.currentBytes, 0);
  do_check_eq(download.totalBytes, 0);

  do_check_eq((yield OS.File.stat(download.target.path)).size, 0);
});

/**
 * Downloads an empty file with no "Content-Length" and checks the progress.
 */
add_task(function test_empty_noprogress()
{
  let deferResponse = deferNextResponse();
  let promiseEmptyRequestReceived = promiseNextRequestReceived();

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can hook its onchange callback that will be notified when the
    // download starts.
    download = yield promiseNewDownload(httpUrl("empty-noprogress.txt"));

    download.onchange = function () {
      if (!download.stopped) {
        do_check_false(download.hasProgress);
        do_check_eq(download.currentBytes, 0);
        do_check_eq(download.totalBytes, 0);
      }
    };

    download.start();
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created, and it may have already made all needed property change
    // notifications, thus there is no point in checking the onchange callback.
    download = yield promiseStartLegacyDownload(
                                       httpUrl("empty-noprogress.txt"));
  }

  // Wait for the request to be received by the HTTP server, but don't allow the
  // request to finish yet.  Before checking the download state, wait for the
  // events to be processed by the client.
  yield promiseEmptyRequestReceived;
  yield promiseExecuteSoon();

  // Check that this download has no progress report.
  do_check_false(download.stopped);
  do_check_false(download.hasProgress);
  do_check_eq(download.currentBytes, 0);
  do_check_eq(download.totalBytes, 0);

  // Now allow the response to finish.
  deferResponse.resolve();
  yield promiseDownloadStopped(download);

  // Verify the state of the completed download.
  do_check_true(download.stopped);
  do_check_false(download.hasProgress);
  do_check_eq(download.progress, 100);
  do_check_eq(download.currentBytes, 0);
  do_check_eq(download.totalBytes, 0);

  do_check_eq((yield OS.File.stat(download.target.path)).size, 0);
});

/**
 * Calls the "start" method two times before the download is finished.
 */
add_task(function test_start_twice()
{
  // Ensure that the download cannot complete before start is called twice.
  let deferResponse = deferNextResponse();

  let download;
  if (!gUseLegacySaver) {
    // When testing DownloadCopySaver, we have control over the download, thus
    // we can start the download later during the test.
    download = yield promiseNewDownload(httpUrl("interruptible.txt"));
  } else {
    // When testing DownloadLegacySaver, the download is already started when it
    // is created.  Effectively, we are starting the download three times.
    download = yield promiseStartLegacyDownload(httpUrl("interruptible.txt"));
  }

  // Call the start method two times.
  let promiseAttempt1 = download.start();
  let promiseAttempt2 = download.start();

  // Allow the download to finish.
  deferResponse.resolve();

  // Both promises should now be resolved.
  yield promiseAttempt1;
  yield promiseAttempt2;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download and verifies that its state is reported correctly.
 */
add_task(function test_cancel_midway()
{
  let deferResponse = deferNextResponse();

  // In this test case, we execute different checks that are only possible with
  // DownloadCopySaver or DownloadLegacySaver respectively.
  let download;
  let options = {};
  if (!gUseLegacySaver) {
    download = yield promiseNewDownload(httpUrl("interruptible.txt"));
  } else {
    download = yield promiseStartLegacyDownload(httpUrl("interruptible.txt"),
                                                options);
  }

  try {
    // Cancel the download after receiving the first part of the response.
    let deferCancel = Promise.defer();
    let onchange = function () {
      if (!download.stopped && !download.canceled && download.progress == 50) {
        deferCancel.resolve(download.cancel());

        // The state change happens immediately after calling "cancel", but
        // temporary files or part files may still exist at this point.
        do_check_true(download.canceled);
      }
    };

    // Register for the notification, but also call the function directly in
    // case the download already reached the expected progress.  This may happen
    // when using DownloadLegacySaver.
    download.onchange = onchange;
    onchange();

    let promiseAttempt;
    if (!gUseLegacySaver) {
      promiseAttempt = download.start();
    }

    // Wait on the promise returned by the "cancel" method to ensure that the
    // cancellation process finished and temporary files were removed.
    yield deferCancel.promise;

    if (gUseLegacySaver) {
      // The nsIWebBrowserPersist instance should have been canceled now.
      do_check_eq(options.outPersist.result, Cr.NS_ERROR_ABORT);
    }

    do_check_true(download.stopped);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(yield OS.File.exists(download.target.path));

    // Progress properties are not reset by canceling.
    do_check_eq(download.progress, 50);
    do_check_eq(download.totalBytes, TEST_DATA_SHORT.length * 2);
    do_check_eq(download.currentBytes, TEST_DATA_SHORT.length);

    if (!gUseLegacySaver) {
      // The promise returned by "start" should have been rejected meanwhile.
      try {
        yield promiseAttempt;
        do_throw("The download should have been canceled.");
      } catch (ex if ex instanceof Downloads.Error) {
        do_check_false(ex.becauseSourceFailed);
        do_check_false(ex.becauseTargetFailed);
      }
    }
  } finally {
    deferResponse.resolve();
  }
});

/**
 * Cancels a download right after starting it.
 */
add_task(function test_cancel_immediately()
{
  // Ensure that the download cannot complete before cancel is called.
  let deferResponse = deferNextResponse();
  try {
    let download = yield promiseStartDownload(httpUrl("interruptible.txt"));

    let promiseAttempt = download.start();
    do_check_false(download.stopped);

    let promiseCancel = download.cancel();
    do_check_true(download.canceled);

    // At this point, we don't know whether the download has already stopped or
    // is still waiting for cancellation.  We can wait on the promise returned
    // by the "start" method to know for sure.
    try {
      yield promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex if ex instanceof Downloads.Error) {
      do_check_false(ex.becauseSourceFailed);
      do_check_false(ex.becauseTargetFailed);
    }

    do_check_true(download.stopped);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(yield OS.File.exists(download.target.path));

    // Check that the promise returned by the "cancel" method has been resolved.
    yield promiseCancel;
  } finally {
    deferResponse.resolve();
  }

  // Even if we canceled the download immediately, the HTTP request might have
  // been made, and the internal HTTP handler might be waiting to process it.
  // Thus, we process any pending events now, to avoid that the request is
  // processed during the tests that follow, interfering with them.
  for (let i = 0; i < 5; i++) {
    yield promiseExecuteSoon();
  }
});

/**
 * Cancels and restarts a download sequentially.
 */
add_task(function test_cancel_midway_restart()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload(httpUrl("interruptible.txt"));

  // The first time, cancel the download midway.
  let deferResponse = deferNextResponse();
  try {
    let deferCancel = Promise.defer();
    download.onchange = function () {
      if (!download.stopped && !download.canceled && download.progress == 50) {
        deferCancel.resolve(download.cancel());
      }
    };
    download.start();
    yield deferCancel.promise;
  } finally {
    deferResponse.resolve();
  }

  do_check_true(download.stopped);

  // The second time, we'll provide the entire interruptible response.
  download.onchange = null;
  let promiseAttempt = download.start();

  // Download state should have already been reset.
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  yield promiseAttempt;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download right after starting it, then restarts it immediately.
 */
add_task(function test_cancel_immediately_restart_immediately()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload(httpUrl("interruptible.txt"));

  // Ensure that the download cannot complete before cancel is called.
  let deferResponse = deferNextResponse();

  let promiseAttempt = download.start();
  do_check_false(download.stopped);

  download.cancel();
  do_check_true(download.canceled);

  let promiseRestarted = download.start();
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.hasProgress, false);
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  // Even if we canceled the download immediately, the HTTP request might have
  // been made, and the internal HTTP handler might be waiting to process it.
  // Thus, we process any pending events now, to avoid that the request is
  // processed during the tests that follow, interfering with them.
  for (let i = 0; i < 5; i++) {
    yield promiseExecuteSoon();
  }

  // Ensure the next request is now allowed to complete, regardless of whether
  // the canceled request was received by the server or not.
  deferResponse.resolve();

  try {
    yield promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex if ex instanceof Downloads.Error) {
    do_check_false(ex.becauseSourceFailed);
    do_check_false(ex.becauseTargetFailed);
  }

  yield promiseRestarted;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Cancels a download midway, then restarts it immediately.
 */
add_task(function test_cancel_midway_restart_immediately()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload(httpUrl("interruptible.txt"));

  // The first time, cancel the download midway.
  let deferResponse = deferNextResponse();

  let deferMidway = Promise.defer();
  download.onchange = function () {
    if (!download.stopped && !download.canceled && download.progress == 50) {
      do_check_eq(download.progress, 50);
      deferMidway.resolve();
    }
  };
  let promiseAttempt = download.start();
  yield deferMidway.promise;

  download.cancel();
  do_check_true(download.canceled);

  let promiseRestarted = download.start();
  do_check_false(download.stopped);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  // For the following test, we rely on the network layer reporting its progress
  // asynchronously.  Otherwise, there is nothing stopping the restarted
  // download from reaching the same progress as the first request already.
  do_check_eq(download.hasProgress, false);
  do_check_eq(download.progress, 0);
  do_check_eq(download.totalBytes, 0);
  do_check_eq(download.currentBytes, 0);

  deferResponse.resolve();

  // The second request is allowed to complete.
  try {
    yield promiseAttempt;
    do_throw("The download should have been canceled.");
  } catch (ex if ex instanceof Downloads.Error) {
    do_check_false(ex.becauseSourceFailed);
    do_check_false(ex.becauseTargetFailed);
  }

  yield promiseRestarted;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method on a successful download.
 */
add_task(function test_cancel_successful()
{
  let download = yield promiseStartDownload();
  yield promiseDownloadStopped(download);

  // The cancel method should succeed with no effect.
  yield download.cancel();

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path, TEST_DATA_SHORT);
});

/**
 * Calls the "cancel" method two times in a row.
 */
add_task(function test_cancel_twice()
{
  // Ensure that the download cannot complete before cancel is called.
  let deferResponse = deferNextResponse();
  try {
    let download = yield promiseStartDownload(httpUrl("interruptible.txt"));

    let promiseAttempt = download.start();
    do_check_false(download.stopped);

    let promiseCancel1 = download.cancel();
    do_check_true(download.canceled);
    let promiseCancel2 = download.cancel();

    try {
      yield promiseAttempt;
      do_throw("The download should have been canceled.");
    } catch (ex if ex instanceof Downloads.Error) {
      do_check_false(ex.becauseSourceFailed);
      do_check_false(ex.becauseTargetFailed);
    }

    // Both promises should now be resolved.
    yield promiseCancel1;
    yield promiseCancel2;

    do_check_true(download.stopped);
    do_check_false(download.succeeded);
    do_check_true(download.canceled);
    do_check_true(download.error === null);

    do_check_false(yield OS.File.exists(download.target.path));
  } finally {
    deferResponse.resolve();
  }
});

/**
 * Checks that whenSucceeded returns a promise that is resolved after a restart.
 */
add_task(function test_whenSucceeded_after_restart()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload(httpUrl("interruptible.txt"));

  // Ensure that the download cannot complete before cancel is called.
  let deferResponse = deferNextResponse();

  // Get a reference before the first download attempt.
  let promiseSucceeded = download.whenSucceeded();

  // Cancel the first download attempt.
  download.start();
  yield download.cancel();

  deferResponse.resolve();

  // The second request is allowed to complete.
  download.start();

  // Wait for the download to finish by waiting on the whenSucceeded promise.
  yield promiseSucceeded;

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);

  yield promiseVerifyContents(download.target.path,
                              TEST_DATA_SHORT + TEST_DATA_SHORT);
});

/**
 * Ensures download error details are reported on network failures.
 */
add_task(function test_error_source()
{
  let serverSocket = startFakeServer();
  try {
    let sourceUrl = "http://localhost:" + serverSocket.port + "/source.txt";

    let download;
    try {
      if (!gUseLegacySaver) {
        // When testing DownloadCopySaver, we want to check that the promise
        // returned by the "start" method is rejected.
        download = yield promiseNewDownload(sourceUrl);

        do_check_true(download.error === null);

        yield download.start();
      } else {
        // When testing DownloadLegacySaver, we cannot be sure whether we are
        // testing the promise returned by the "start" method or we are testing
        // the "error" property checked by promiseDownloadStopped.  This happens
        // because we don't have control over when the download is started.
        download = yield promiseStartLegacyDownload(sourceUrl);
        yield promiseDownloadStopped(download);
      }
      do_throw("The download should have failed.");
    } catch (ex if ex instanceof Downloads.Error && ex.becauseSourceFailed) {
      // A specific error object is thrown when reading from the source fails.
    }

    // Check the properties now that the download stopped.
    do_check_true(download.stopped);
    do_check_false(download.canceled);
    do_check_true(download.error !== null);
    do_check_true(download.error.becauseSourceFailed);
    do_check_false(download.error.becauseTargetFailed);
  } finally {
    serverSocket.close();
  }
});

/**
 * Ensures download error details are reported on local writing failures.
 */
add_task(function test_error_target()
{
  // Create a file without write access permissions before downloading.
  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
  targetFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);
  try {
    let download;
    try {
      if (!gUseLegacySaver) {
        // When testing DownloadCopySaver, we want to check that the promise
        // returned by the "start" method is rejected.
        download = yield Downloads.createDownload({
          source: httpUrl("source.txt"),
          target: targetFile,
        });
        yield download.start();
      } else {
        // When testing DownloadLegacySaver, we cannot be sure whether we are
        // testing the promise returned by the "start" method or we are testing
        // the "error" property checked by promiseDownloadStopped.  This happens
        // because we don't have control over when the download is started.
        download = yield promiseStartLegacyDownload(null,
                                                    { targetFile: targetFile });
        yield promiseDownloadStopped(download);
      }
      do_throw("The download should have failed.");
    } catch (ex if ex instanceof Downloads.Error && ex.becauseTargetFailed) {
      // A specific error object is thrown when writing to the target fails.
    }

    // Check the properties now that the download stopped.
    do_check_true(download.stopped);
    do_check_false(download.canceled);
    do_check_true(download.error !== null);
    do_check_true(download.error.becauseTargetFailed);
    do_check_false(download.error.becauseSourceFailed);
  } finally {
    // Restore the default permissions to allow deleting the file on Windows.
    if (targetFile.exists()) {
      targetFile.permissions = FileUtils.PERMS_FILE;
      targetFile.remove(false);
    }
  }
});

/**
 * Restarts a failed download.
 */
add_task(function test_error_restart()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload();

  do_check_true(download.error === null);

  // Create a file without write access permissions before downloading.
  let targetFile = new FileUtils.File(download.target.path);
  targetFile.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0);

  try {
    yield download.start();
    do_throw("The download should have failed.");
  } catch (ex if ex instanceof Downloads.Error && ex.becauseTargetFailed) {
    // A specific error object is thrown when writing to the target fails.
  } finally {
    // Restore the default permissions to allow deleting the file on Windows.
    if (targetFile.exists()) {
      targetFile.permissions = FileUtils.PERMS_FILE;

      // Also for Windows, rename the file before deleting.  This makes the
      // current file name available immediately for a new file, while deleting
      // in place prevents creation of a file with the same name for some time.
      targetFile.moveTo(null, targetFile.leafName + ".delete.tmp");
      targetFile.remove(false);
    }
  }

  // Restart the download and wait for completion.
  yield download.start();

  do_check_true(download.stopped);
  do_check_true(download.succeeded);
  do_check_false(download.canceled);
  do_check_true(download.error === null);
  do_check_eq(download.progress, 100);

  yield promiseVerifyContents(download.target.path, TEST_DATA_SHORT);
});

/**
 * Executes download in both public and private modes.
 */
add_task(function test_public_and_private()
{
  let sourcePath = "/test_public_and_private.txt";
  let sourceUrl = httpUrl("test_public_and_private.txt");
  let testCount = 0;

  // Apply pref to allow all cookies.
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);

  function cleanup() {
    Services.prefs.clearUserPref("network.cookie.cookieBehavior");
    Services.cookies.removeAll();
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  do_register_cleanup(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);

    if (testCount == 0) {
      // No cookies should exist for first public download.
      do_check_false(aRequest.hasHeader("Cookie"));
      aResponse.setHeader("Set-Cookie", "foobar=1", false);
      testCount++;
    } else if (testCount == 1) {
      // The cookie should exists for second public download.
      do_check_true(aRequest.hasHeader("Cookie"));
      do_check_eq(aRequest.getHeader("Cookie"), "foobar=1");
      testCount++;
    } else if (testCount == 2)  {
      // No cookies should exist for first private download.
      do_check_false(aRequest.hasHeader("Cookie"));
    }
  });

  let targetFile = getTempFile(TEST_TARGET_FILE_NAME);
  yield Downloads.simpleDownload(sourceUrl, targetFile);
  yield Downloads.simpleDownload(sourceUrl, targetFile);

  if (!gUseLegacySaver) {
    let download = yield Downloads.createDownload({
      source: { url: sourceUrl, isPrivate: true },
      target: targetFile,
    });
    yield download.start();
  } else {
    let download = yield promiseStartLegacyDownload(sourceUrl,
                                                    { isPrivate: true });
    yield promiseDownloadStopped(download);
  }

  cleanup();
});

/**
 * Checks the startTime gets updated even after a restart.
 */
add_task(function test_cancel_immediately_restart_and_check_startTime()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload();

  download.start();
  let startTime = download.startTime;
  do_check_true(isValidDate(download.startTime));

  yield download.cancel();
  do_check_eq(download.startTime.getTime(), startTime.getTime());

  // Wait for a timeout.
  yield promiseTimeout(10);

  yield download.start();
  do_check_true(download.startTime.getTime() > startTime.getTime());
});

/**
 * Executes download with content-encoding.
 */
add_task(function test_with_content_encoding()
{
  let sourcePath = "/test_with_content_encoding.txt";
  let sourceUrl = httpUrl("test_with_content_encoding.txt");

  function cleanup() {
    gHttpServer.registerPathHandler(sourcePath, null);
  }
  do_register_cleanup(cleanup);

  gHttpServer.registerPathHandler(sourcePath, function (aRequest, aResponse) {
    aResponse.setHeader("Content-Type", "text/plain", false);
    aResponse.setHeader("Content-Encoding", "gzip", false);
    aResponse.setHeader("Content-Length",
                        "" + TEST_DATA_SHORT_GZIP_ENCODED.length, false);

    let bos =  new BinaryOutputStream(aResponse.bodyOutputStream);
    bos.writeByteArray(TEST_DATA_SHORT_GZIP_ENCODED,
                       TEST_DATA_SHORT_GZIP_ENCODED.length);
  });

  let download = yield promiseStartDownload(sourceUrl);
  yield promiseDownloadStopped(download);

  do_check_eq(download.progress, 100);
  do_check_eq(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);

  // Ensure the content matches the decoded test data.
  yield promiseVerifyContents(download.target.path, TEST_DATA_SHORT);
});

/**
 * Cancels and restarts a download sequentially with content-encoding.
 */
add_task(function test_cancel_midway_restart_with_content_encoding()
{
  // TODO: Enable all the restart tests for DownloadLegacySaver.
  if (gUseLegacySaver) {
    return;
  }

  let download = yield promiseNewDownload(httpUrl("interruptible_gzip.txt"));

  // The first time, cancel the download midway.
  let deferResponse = deferNextResponse();
  try {
    let deferCancel = Promise.defer();
    download.onchange = function () {
      if (!download.stopped && !download.canceled &&
          download.currentBytes == TEST_DATA_SHORT_GZIP_ENCODED_FIRST.length) {
        deferCancel.resolve(download.cancel());
      }
    };
    download.start();
    yield deferCancel.promise;
  } finally {
    deferResponse.resolve();
  }

  do_check_true(download.stopped);

  // The second time, we'll provide the entire interruptible response.
  download.onchange = null;
  yield download.start()

  do_check_eq(download.progress, 100);
  do_check_eq(download.totalBytes, TEST_DATA_SHORT_GZIP_ENCODED.length);

  yield promiseVerifyContents(download.target.path, TEST_DATA_SHORT);
});

/**
 * Download with parental controls enabled.
 */
add_task(function test_blocked_parental_controls()
{
  function cleanup() {
    DownloadIntegration.shouldBlockInTest = false;
  }
  do_register_cleanup(cleanup);
  DownloadIntegration.shouldBlockInTest = true;

  let download;
  try {
    if (!gUseLegacySaver) {
      // When testing DownloadCopySaver, we want to check that the promise
      // returned by the "start" method is rejected.
      download = yield promiseNewDownload();
      yield download.start();
    } else {
      // When testing DownloadLegacySaver, we cannot be sure whether we are
      // testing the promise returned by the "start" method or we are testing
      // the "error" property checked by promiseDownloadStopped.  This happens
      // because we don't have control over when the download is started.
      download = yield promiseStartLegacyDownload();
      yield promiseDownloadStopped(download);
    }
    do_throw("The download should have blocked.");
  } catch (ex if ex instanceof Downloads.Error && ex.becauseBlocked) {
    do_check_true(ex.becauseBlockedByParentalControls);
  }

  // Now that the download stopped, the target file should not exist.
  do_check_false(yield OS.File.exists(download.target.path));

  cleanup();
});

/**
 * download.showContainingDirectory() action
 */
add_task(function test_showContainingDirectory() {
  DownloadIntegration._deferTestShowDir = Promise.defer();

  let targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  let download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: ""
  });

  try {
    yield download.showContainingDirectory();
    do_throw("Should have failed because of an invalid path.");
  } catch (ex if ex instanceof Components.Exception) {
    // Invalid paths on Windows are reported with NS_ERROR_FAILURE,
    // but with NS_ERROR_FILE_UNRECOGNIZED_PATH on Mac/Linux
    let validResult = ex.result == Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH ||
                      ex.result == Cr.NS_ERROR_FAILURE;
    do_check_true(validResult);
  }

  download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: targetPath
  });


  DownloadIntegration._deferTestShowDir = Promise.defer();
  download.showContainingDirectory();
  let result = yield DownloadIntegration._deferTestShowDir.promise;
  do_check_eq(result, "success");
});

/**
 * download.launch() action
 */
add_task(function test_launch() {
  let targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  let download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: targetPath }
  });

  // Test that file is not launched if this.succeeded is not set.
  // i.e., download has not yet completed.
  try {
    yield download.launch();
    do_throw("Can't launch download file as it has not completed yet");
  } catch (ex) {
    do_check_eq(ex.message, "launch can only be called if the download succeeded")
  }

  // Test that the file can be launched after download is completed.
  DownloadIntegration._deferTestOpenFile = Promise.defer();
  yield download.start();
  download.launch();
  let result = yield DownloadIntegration._deferTestOpenFile.promise;
  do_check_eq(result, "default-handler");


  // Test that a proper error will be thrown if an invalid
  // custom handler was chosen.
  download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: targetPath },
    launcherPath: " "
  });

  DownloadIntegration._deferTestOpenFile = Promise.defer();
  yield download.start();
  try {
    download.launch();
    result = yield DownloadIntegration._deferTestOpenFile.promise;
    do_throw("Can't launch file with invalid custom launcher")
  } catch (ex if ex instanceof Components.Exception) {
    // Invalid paths on Windows are reported with NS_ERROR_FAILURE,
    // but with NS_ERROR_FILE_UNRECOGNIZED_PATH on Mac/Linux
    let validResult = ex.result == Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH ||
                      ex.result == Cr.NS_ERROR_FAILURE;
    do_check_true(validResult);
  }

  // Test that the custom app chosen will be used
  // if launcherPath is set.
  download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: targetPath },
    launcherPath: getTempFile("app-launcher").path
  });

  DownloadIntegration._deferTestOpenFile = Promise.defer();
  yield download.start();
  download.launch();
  result = yield DownloadIntegration._deferTestOpenFile.promise;
  do_check_eq(result, "chosen-app");

});

/**
 * Tests that download.launch() is automatically called after
 * the download finishes if download.launchWhenSucceeded = true
 */
add_task(function test_launchWhenSucceeded() {
  let targetPath = getTempFile(TEST_TARGET_FILE_NAME).path;

  let download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: targetPath },
    launchWhenSucceeded: true,
  });

  // Test that the default handler will be used if no
  // custom handler was chosen.
  DownloadIntegration._deferTestOpenFile = Promise.defer();
  download.start();
  let result = yield DownloadIntegration._deferTestOpenFile.promise;
  do_check_eq(result, "default-handler");


  // Test that the custom app chosen will be used
  // if launcherPath is set.
  download = yield Downloads.createDownload({
    source: { url: httpUrl("source.txt") },
    target: { path: targetPath },
    launchWhenSucceeded: true,
    launcherPath: getTempFile("app-launcher").path
  });

  DownloadIntegration._deferTestOpenFile = Promise.defer();
  yield download.start();
  result = yield DownloadIntegration._deferTestOpenFile.promise;
  do_check_eq(result, "chosen-app");
});

/**
 * Tests that the proper content type is set for a download.
 */
add_task(function test_contentType() {
  let download = yield promiseStartDownload(httpUrl("source.txt"));

  yield promiseDownloadStopped(download);
  do_check_eq("text/plain", download.contentType);
});

