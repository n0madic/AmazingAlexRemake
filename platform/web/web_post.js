// Persist ordinary C++ file writes without changing SaveStore's synchronous API. A final sync is
// requested when the page hides; the periodic checkpoint covers browsers that do not finish unload work.
(function () {
  // The stock Emscripten shell only points at DevTools. Mirror startup failures into its visible
  // output pane as well, which is useful when a locally served build is smoke-tested without DevTools.
  function report(event) {
    var output = document.getElementById('output');
    if (output) output.value += 'WEB ERROR: ' + (event.message || event.reason || event.error || event) + '\n';
  }
  addEventListener('error', report);
  addEventListener('unhandledrejection', report);
  function sync() {
    // A no-op when nothing has changed since the last flush (aaStorageDirty), so an idle tab does not
    // run FS.syncfs every 5 seconds for no reason.
    if (Module.aaFlushPersistentStorage) Module.aaFlushPersistentStorage();
  }
  setInterval(sync, 5000);
  addEventListener('pagehide', sync);
}());
