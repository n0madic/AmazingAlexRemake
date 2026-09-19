// Runs before main. Loading the saved IDBFS tree is a run dependency, so C++ never observes an
// empty directory merely because IndexedDB's asynchronous read has not completed yet.
var Module = typeof Module !== 'undefined' ? Module : {};
Module.preRun = Module.preRun || [];
Module.aaStorageReady = false;
Module.aaStorageDirty = false;
Module.aaStorageSyncing = false;
// Called by C++ (save_store.cpp) after every write: marks the tree dirty and flushes right away.
Module.aaSyncPersistentStorage = function () {
  Module.aaStorageDirty = true;
  Module.aaFlushPersistentStorage();
};
// The web_post.js periodic checkpoint and pagehide handler call this instead of aaSyncPersistentStorage:
// it costs nothing when nothing has changed since the last flush, rather than running FS.syncfs on a
// timer regardless of whether any save actually happened.
Module.aaFlushPersistentStorage = function () {
  if (!Module.aaStorageDirty || !Module.aaStorageReady || Module.aaStorageSyncing) return;
  Module.aaStorageDirty = false;
  Module.aaStorageSyncing = true;
  FS.syncfs(false, function (error) {
    Module.aaStorageSyncing = false;
    if (error) console.error('Amazing Alex: cannot save progress', error);
    if (Module.aaStorageDirty) Module.aaFlushPersistentStorage();
  });
};
Module.preRun.push(function () {
  FS.mkdir('/persistent');
  FS.mount(IDBFS, {}, '/persistent');
  addRunDependency('amazing-alex-saves');
  FS.syncfs(true, function (error) {
    if (error) console.error('Amazing Alex: cannot load saved progress', error);
    Module.aaStorageReady = true;
    removeRunDependency('amazing-alex-saves');
    Module.aaFlushPersistentStorage();
  });
});
